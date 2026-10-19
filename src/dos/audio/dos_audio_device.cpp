#include "dos/audio/dos_audio_device.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>

#include <algorithm>

#include "util/log.h"

namespace tmport::dos {

DosAudioDevice::DosAudioDevice(std::shared_ptr<const SoundBank> bank, DosAudioConfig cfg, std::function<int()> rand)
    : bank_(std::move(bank)), cfg_(std::move(cfg)), rand_(std::move(rand)), volume_(cfg_.volume) {
    renderer_ = std::make_unique<DosSoundRenderer>(*bank_, cfg_.render);
    renderer_->setTickHook([this] { applyPending(); });
    publishStatus();
}

DosAudioDevice::~DosAudioDevice() { close(); }

// ----------------------------------------------------------------------------------- SDL side

bool DosAudioDevice::open(uint32_t sdlDevice) {
    if (stream_) return true;
    if (!cfg_.sdlDriver.empty()) SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, cfg_.sdlDriver.c_str(), SDL_HINT_OVERRIDE);
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log::warn("dos audio: SDL audio initialisation failed ({}); sound is off", SDL_GetError());
        return false;
    }
    sdlInit_ = true;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, cfg_.render.sampleRate};
    if (sdlDevice) {
        stream_ = SDL_CreateAudioStream(&spec, nullptr);
        if (stream_ && (!SDL_SetAudioStreamGetCallback(stream_, &DosAudioDevice::callback, this) ||
                        !SDL_BindAudioStream(sdlDevice, stream_))) {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
        ownsDevice_ = false;
    } else {
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &DosAudioDevice::callback, this);
        ownsDevice_ = true;
    }
    if (!stream_) {
        log::warn("dos audio: no playback stream ({}); sound is off", SDL_GetError());
        close();
        return false;
    }
    if (ownsDevice_) SDL_ResumeAudioStreamDevice(stream_);
    log::debug("dos audio: {} sound on SDL driver '{}' at {} Hz", soundCardName(bank_->device), driverName(),
               cfg_.render.sampleRate);
    return true;
}

void DosAudioDevice::close() {
    if (stream_) SDL_DestroyAudioStream(stream_);  // a stream from SDL_OpenAudioDeviceStream closes its device
    stream_ = nullptr;
    if (sdlInit_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sdlInit_ = false;
}

std::string DosAudioDevice::driverName() const {
    if (!sdlInit_) return {};
    const char* n = SDL_GetCurrentAudioDriver();
    return n ? n : "";
}

void DosAudioDevice::callback(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto* self = static_cast<DosAudioDevice*>(user);
    if (additional <= 0) return;
    const size_t n = static_cast<size_t>(additional) / sizeof(float);
    std::lock_guard lock(self->renderMutex_);
    if (self->floatBuf_.size() < n) self->floatBuf_.resize(n);
    self->renderFloat(self->floatBuf_.data(), n);
    SDL_PutAudioStreamData(stream, self->floatBuf_.data(), static_cast<int>(n * sizeof(float)));
}

size_t DosAudioDevice::renderOffline(float* out, size_t n) {
    if (stream_) return 0;
    std::lock_guard lock(renderMutex_);
    renderFloat(out, n);
    return n;
}

size_t DosAudioDevice::renderOfflineTicks(uint32_t ticks, std::vector<float>& out) {
    if (stream_) return 0;
    std::lock_guard lock(renderMutex_);
    scratch_.clear();
    if (renderer_->samplesToNextTick() != 0) renderer_->renderTick(scratch_);  // the current tick's rest
    for (uint32_t t = 0; t < ticks; ++t) renderer_->renderTick(scratch_);
    const float g = outputEnabled_.load() ? volume_.load() : 0.0f;
    const size_t at = out.size();
    out.resize(at + scratch_.size());
    for (size_t i = 0; i < scratch_.size(); ++i) out[at + i] = std::clamp(static_cast<float>(scratch_[i]) * g, -1.0f, 1.0f);
    publishStatus();
    return scratch_.size();
}

void DosAudioDevice::renderFloat(float* out, size_t n) {
    if (scratch_.size() < n) scratch_.resize(n);
    renderer_->render(scratch_.data(), n);
    const float g = outputEnabled_.load() ? volume_.load() : 0.0f;
    for (size_t i = 0; i < n; ++i) out[i] = std::clamp(static_cast<float>(scratch_[i]) * g, -1.0f, 1.0f);
    publishStatus();
}

// ---------------------------------------------------------------------------- command queue

void DosAudioDevice::push(Command c) {
    std::lock_guard lock(queueMutex_);
    queue_.push_back(c);
    pending_.store(true);
}

void DosAudioDevice::applyPending() {
    if (!pending_.load()) return;
    std::vector<Command> cmds;
    std::vector<Restore> restores;
    {
        std::lock_guard lock(queueMutex_);
        cmds.swap(queue_);
        restores.swap(restores_);
        pending_.store(false);
    }
    Sequencer& s = renderer_->sequencer();
    size_t nextRestore = 0;
    for (const Command& c : cmds) {
        switch (c.kind) {
            case Command::Kind::SfxPlay: s.sfxPlay(c.a); break;
            case Command::Kind::SfxStop: s.sfxStop(); break;
            case Command::Kind::MusicPlay: s.musicPlay(c.a, c.b); break;
            case Command::Kind::MusicStop: s.musicStop(); break;
            case Command::Kind::SilenceAll: s.silenceAll(); break;
            case Command::Kind::SoundOn: s.setSoundOn(c.a != 0); break;
            case Command::Kind::MusicOn: s.setMusicOn(c.a != 0); break;
            case Command::Kind::MusicPaused: s.setMusicPaused(c.a != 0); break;
            case Command::Kind::MusicLoop: s.setMusicLoop(c.a != 0); break;
            case Command::Kind::SfxLoop: s.setSfxLoop(c.a != 0); break;
            case Command::Kind::Restore:
                if (nextRestore < restores.size()) {
                    const Restore& r = restores[nextRestore++];
                    s.restore(r.sfx, r.music, r.voices, r.variant);
                }
                break;
        }
    }
}

void DosAudioDevice::sfxPlay(int id) { push({Command::Kind::SfxPlay, id}); }
void DosAudioDevice::sfxStop() { push({Command::Kind::SfxStop}); }
void DosAudioDevice::musicStop() { push({Command::Kind::MusicStop}); }
void DosAudioDevice::silenceAll() { push({Command::Kind::SilenceAll}); }
void DosAudioDevice::setSoundOn(bool on) { push({Command::Kind::SoundOn, on ? 1 : 0}); }
void DosAudioDevice::setMusicPaused(bool paused) { push({Command::Kind::MusicPaused, paused ? 1 : 0}); }
void DosAudioDevice::setMusicLoop(bool on) { push({Command::Kind::MusicLoop, on ? 1 : 0}); }
void DosAudioDevice::setSfxLoop(bool on) { push({Command::Kind::SfxLoop, on ? 1 : 0}); }

void DosAudioDevice::restoreBlocks(std::span<const uint8_t> sfxBlock, std::span<const uint8_t> musicBlock, int voices,
                                   int adlibVariant) {
    std::lock_guard lock(queueMutex_);
    restores_.push_back({Sequencer::blockFromDgroup(sfxBlock), Sequencer::blockFromDgroup(musicBlock), voices, adlibVariant});
    queue_.push_back({Command::Kind::Restore});
    pending_.store(true);
}

void DosAudioDevice::setMusicOn(bool on) {
    musicOnMirror_ = on;
    push({Command::Kind::MusicOn, on ? 1 : 0});
}

void DosAudioDevice::musicPlay(int tune) {
    int variant = 0;
    // 4ac9:0002 AdLib_PlayTune: rand() & 3 picks one of 4 orchestrations (only reached when the
    // music is on, 491b:07b9).
    if (bank_->device == SoundCard::AdLib && musicOnMirror_ && rand_) variant = rand_() & 3;
    push({Command::Kind::MusicPlay, tune, variant});
}

// ----------------------------------------------------------------------------------- status

void DosAudioDevice::publishStatus() {
    const Sequencer& s = renderer_->sequencer();
    const uint64_t w = static_cast<uint64_t>(s.sfxBlock().status) | (static_cast<uint64_t>(s.sfxBlock().id) << 16) |
                       (static_cast<uint64_t>(s.musicBlock().status) << 32) | (static_cast<uint64_t>(s.musicBlock().id) << 48);
    statusWord_.store(w);
    statusTicks_.store(renderer_->ticks());
}

DosAudioDevice::Status DosAudioDevice::status() const {
    const uint64_t w = statusWord_.load();
    Status st;
    st.sfxStatus = static_cast<uint16_t>(w);
    st.sfxId = static_cast<uint16_t>(w >> 16);
    st.musicStatus = static_cast<uint16_t>(w >> 32);
    st.musicId = static_cast<uint16_t>(w >> 48);
    st.ticks = statusTicks_.load();
    return st;
}

}  // namespace tmport::dos
