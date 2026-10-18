#include "audio/midi_backend_soundfont.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <cmath>

#include "util/file_util.h"
#include "util/log.h"

namespace tmport::audio {

namespace {

constexpr int kFrameBytes = 2 * static_cast<int>(sizeof(float));  // F32 stereo
constexpr int kCallbackChunk = 1024;           // frames rendered per piece in the SDL callback
constexpr size_t kMaxQueued = 8192;            // beyond this the renderer is not running: apply at once
constexpr uint64_t kResyncUs = 200'000;        // device clock drifted this far from the stream: resync

uint64_t framesToUs(uint64_t frames, int rate) { return frames * 1'000'000ull / static_cast<uint64_t>(rate); }

}  // namespace

SoundFontMidiBackend::SoundFontMidiBackend(Config config) : cfg_(std::move(config)) {
    if (!cfg_.clock) cfg_.clock = realClockUs;
    synth_ = cfg_.synth;
    volume_ = std::clamp(cfg_.volume, 0.0f, 1.0f);
    cbBuffer_.resize(static_cast<size_t>(kCallbackChunk) * 2);
}

SoundFontMidiBackend::~SoundFontMidiBackend() { close(); }

bool SoundFontMidiBackend::open() {
    if (open_) return true;
    if (!synth_ && cfg_.soundFont.empty()) return false;  // nothing to play with (already reported)
    if (cfg_.output == Output::Device) {
        if (!openDevice()) {
            closeDevice();
            return false;
        }
    } else if (!synth_) {
        std::string error;
        synth_ = Sf2Synth::loadFile(cfg_.soundFont, cfg_.sampleRate > 0 ? cfg_.sampleRate : 44100, error);
        if (!synth_) {
            log::warn("midi: SoundFont {}: {}; music is disabled", pathToUtf8(cfg_.soundFont), error);
            return false;
        }
    }
    {
        std::lock_guard lock(queueMu_);
        queue_.clear();
    }
    synth_->reset();
    {
        std::lock_guard lock(statsMu_);
        framesRendered_ = 0;
        peak_ = 0.0f;
        rtValid_ = false;
    }
    open_ = true;
    if (stream_) {
        // Start pulling only now that the synthesizer is ready.
        SDL_SetAudioStreamGetCallback(stream_, &SoundFontMidiBackend::streamCallback, this);
        if (!SDL_BindAudioStream(device_, stream_)) {
            log::warn("midi: cannot attach the music stream to the audio device ({}); music is disabled", SDL_GetError());
            open_ = false;
            closeDevice();
            return false;
        }
        if (ownDevice_) SDL_ResumeAudioDevice(device_);
    }
    return true;
}

bool SoundFontMidiBackend::openDevice() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log::warn("midi: SDL audio initialisation failed ({}); music is disabled", SDL_GetError());
        return false;
    }
    sdlInit_ = true;
    device_ = cfg_.device;
    ownDevice_ = false;
    if (device_ == 0) {
        device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (device_ == 0) {
            log::warn("midi: no playback device ({}); music is disabled", SDL_GetError());
            return false;
        }
        ownDevice_ = true;
    }
    SDL_AudioSpec devSpec{};
    int devFrames = 0;
    int rate = cfg_.sampleRate;
    if (rate <= 0) rate = SDL_GetAudioDeviceFormat(device_, &devSpec, &devFrames) && devSpec.freq > 0 ? devSpec.freq : 44100;
    if (!synth_) {
        std::string error;
        synth_ = Sf2Synth::loadFile(cfg_.soundFont, rate, error);
        if (!synth_) {
            log::warn("midi: SoundFont {}: {}; music is disabled", pathToUtf8(cfg_.soundFont), error);
            return false;
        }
        log::info("midi: SoundFont {} ({} presets), {} Hz", pathToUtf8(cfg_.soundFont), synth_->presetCount(), rate);
    }
    const SDL_AudioSpec src{SDL_AUDIO_F32, 2, synth_->sampleRate()};
    stream_ = SDL_CreateAudioStream(&src, nullptr);
    if (!stream_) {
        log::warn("midi: cannot create the music stream ({}); music is disabled", SDL_GetError());
        return false;
    }
    return true;
}

void SoundFontMidiBackend::closeDevice() {
    if (stream_) SDL_DestroyAudioStream(stream_);  // unbinds; waits for a running callback
    stream_ = nullptr;
    if (ownDevice_ && device_) SDL_CloseAudioDevice(device_);
    ownDevice_ = false;
    device_ = 0;
    if (sdlInit_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sdlInit_ = false;
}

void SoundFontMidiBackend::close() {
    if (!open_ && !stream_ && !sdlInit_) return;
    closeDevice();
    open_ = false;
    {
        std::lock_guard lock(queueMu_);
        queue_.clear();
    }
    if (synth_) synth_->reset();
}

void SoundFontMidiBackend::send(uint8_t status, uint8_t d1, uint8_t d2) {
    ++messages_;
    if (!open_ || !synth_) return;
    const uint64_t now = cfg_.clock();
    std::lock_guard lock(queueMu_);
    queue_.push_back({now, status, d1, d2});
    // Nobody renders (device stalled): keep the synthesizer state current and the queue bounded.
    while (queue_.size() > kMaxQueued) {
        const Stamped m = queue_.front();
        queue_.pop_front();
        synth_->send(m.status, m.d1, m.d2);
    }
}

void SoundFontMidiBackend::renderAt(float* out, int frames, uint64_t blockStartUs) {
    if (frames <= 0) return;
    if (!synth_) {
        std::fill(out, out + static_cast<size_t>(frames) * 2, 0.0f);
        return;
    }
    const uint64_t rate = static_cast<uint64_t>(synth_->sampleRate());
    int done = 0;
    for (;;) {
        Stamped m{};
        bool have = false;
        int at = frames;
        {
            std::lock_guard lock(queueMu_);
            if (!queue_.empty()) {
                m = queue_.front();
                const uint64_t off = m.us <= blockStartUs ? 0 : (m.us - blockStartUs) * rate / 1'000'000ull;
                if (off < static_cast<uint64_t>(frames)) {
                    have = true;
                    at = std::max(done, static_cast<int>(off));
                    queue_.pop_front();
                }
            }
        }
        if (at > done) {
            synth_->render(out + static_cast<size_t>(done) * 2, at - done);
            done = at;
        }
        if (!have) break;
        synth_->send(m.status, m.d1, m.d2);
    }
    float vol, peak = 0.0f;
    {
        std::lock_guard lock(statsMu_);
        vol = volume_;
    }
    float* const end = out + static_cast<size_t>(frames) * 2;
    if (vol != 1.0f)
        for (float* p = out; p != end; ++p) *p *= vol;
    for (const float* p = out; p != end; ++p) peak = std::max(peak, std::fabs(*p));
    std::lock_guard lock(statsMu_);
    framesRendered_ += static_cast<uint64_t>(frames);
    peak_ = std::max(peak_, peak);
}

void SoundFontMidiBackend::renderRealtime(float* out, int frames) {
    if (!synth_) {
        std::fill(out, out + static_cast<size_t>(frames) * 2, 0.0f);
        return;
    }
    const int rate = synth_->sampleRate();
    const uint64_t now = cfg_.clock();
    const uint64_t target = now > cfg_.latencyUs ? now - cfg_.latencyUs : 0;
    uint64_t start;
    {
        std::lock_guard lock(statsMu_);
        start = rtBaseUs_ + framesToUs(rtFrames_, rate);
        // First block, or the stream and the clock drifted apart (device stalls, clock drift):
        // restart the mapping at "now - latency".
        if (!rtValid_ || start > target + kResyncUs || start + kResyncUs < target) {
            rtValid_ = true;
            rtBaseUs_ = target;
            rtFrames_ = 0;
            start = target;
        }
        rtFrames_ += static_cast<uint64_t>(frames);
    }
    renderAt(out, frames, start);
}

void SoundFontMidiBackend::streamCallback(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto* self = static_cast<SoundFontMidiBackend*>(user);
    int frames = (additional + kFrameBytes - 1) / kFrameBytes;
    while (frames > 0) {
        const int n = std::min(frames, kCallbackChunk);
        self->renderRealtime(self->cbBuffer_.data(), n);
        SDL_PutAudioStreamData(stream, self->cbBuffer_.data(), n * kFrameBytes);
        frames -= n;
    }
}

void SoundFontMidiBackend::setVolume(float volume) {
    std::lock_guard lock(statsMu_);
    volume_ = std::clamp(volume, 0.0f, 1.0f);
}

float SoundFontMidiBackend::volume() const {
    std::lock_guard lock(statsMu_);
    return volume_;
}

size_t SoundFontMidiBackend::queuedMessages() const {
    std::lock_guard lock(queueMu_);
    return queue_.size();
}

uint64_t SoundFontMidiBackend::framesRendered() const {
    std::lock_guard lock(statsMu_);
    return framesRendered_;
}

float SoundFontMidiBackend::peakSinceOpen() const {
    std::lock_guard lock(statsMu_);
    return peak_;
}

std::unique_ptr<MidiBackend> createSoundFontMidiBackend(const std::string& soundFontUtf8, uint32_t sdlDevice, float volume) {
    SoundFontMidiBackend::Config c;
    c.soundFont = pathFromUtf8(soundFontUtf8);
    c.output = SoundFontMidiBackend::Output::Device;
    c.device = sdlDevice;
    c.volume = volume;
    c.sampleRate = 0;  // the device's rate
    return std::make_unique<SoundFontMidiBackend>(std::move(c));
}

}  // namespace tmport::audio
