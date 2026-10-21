#include "audio/sdl_wave_output.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>

#include "mischief/util/log.h"

namespace tmport::audio {

namespace {
constexpr uint32_t kDefaultRate = 22050;  // every shipped SSND (fmt_sound.md §3.2)
}

SdlWaveOutput::SdlWaveOutput(std::string driver) : driver_(std::move(driver)) {}

SdlWaveOutput::~SdlWaveOutput() { close(); }

bool SdlWaveOutput::open() {
    if (stream_) return true;
    if (!driver_.empty()) SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, driver_.c_str(), SDL_HINT_OVERRIDE);
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        mischief::log::warn("audio: SDL audio initialisation failed ({}); sounds are disabled", SDL_GetError());
        return false;
    }
    sdlInit_ = true;
    // A logical device of our own (not SDL_OpenAudioDeviceStream's "simplified" one, which takes
    // no further streams), so the music stream can join it.
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device_) {
        mischief::log::warn("audio: no playback device ({}); sounds are disabled", SDL_GetError());
        close();
        return false;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_U8, 1, static_cast<int>(kDefaultRate)};
    stream_ = SDL_CreateAudioStream(&spec, nullptr);
    if (!stream_ || !SDL_BindAudioStream(device_, stream_)) {
        mischief::log::warn("audio: cannot attach the wave voice to the playback device ({}); sounds are disabled", SDL_GetError());
        close();
        return false;
    }
    rate_ = kDefaultRate;
    SDL_ResumeAudioDevice(device_);
    mischief::log::debug("audio: wave voice on SDL driver '{}'", driverName());
    return true;
}

void SdlWaveOutput::close() {
    if (stream_) SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    if (device_) SDL_CloseAudioDevice(device_);  // other streams on it are unbound, not destroyed
    device_ = 0;
    if (sdlInit_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sdlInit_ = false;
}

std::string SdlWaveOutput::driverName() const {
    if (!sdlInit_) return {};
    const char* n = SDL_GetCurrentAudioDriver();
    return n ? n : "";
}

void SdlWaveOutput::start(const Sound& s) {
    if (!stream_) return;
    SDL_ClearAudioStream(stream_);
    if (s.sampleRate != rate_ && s.sampleRate != 0) {
        const SDL_AudioSpec src{SDL_AUDIO_U8, 1, static_cast<int>(s.sampleRate)};
        if (SDL_SetAudioStreamFormat(stream_, &src, nullptr)) rate_ = s.sampleRate;
    }
    if (!s.pcm.empty()) {
        // The last few samples go through a faded copy (fadeOutTail: no click at the end).
        const size_t tail = std::min(s.pcm.size(), kFadeOutSamples);
        const size_t head = s.pcm.size() - tail;
        if (head) SDL_PutAudioStreamData(stream_, s.pcm.data(), static_cast<int>(head));
        std::array<uint8_t, kFadeOutSamples> faded{};
        std::copy_n(s.pcm.data() + head, tail, faded.data());
        fadeOutTail({faded.data(), tail});
        SDL_PutAudioStreamData(stream_, faded.data(), static_cast<int>(tail));
    }
    // Push the resampler tail through as well: the whole block is one "buffer".
    SDL_FlushAudioStream(stream_);
    const uint64_t rate = rate_ ? rate_ : kDefaultRate;
    deadlineNs_ = SDL_GetTicksNS() + static_cast<uint64_t>(s.pcm.size()) * 1'000'000'000ull / rate +
                  kDoneMarginMs * 1'000'000ull;
}

void SdlWaveOutput::stop() {
    if (stream_) SDL_ClearAudioStream(stream_);
}

bool SdlWaveOutput::done() {
    if (!stream_) return true;
    if (SDL_GetAudioStreamQueued(stream_) <= 0) return true;
    if (SDL_GetTicksNS() < deadlineNs_) return false;
    // The device stopped consuming (removed, driver reset): give the sound up instead of waiting
    // forever (REVIEW_1 L-3).
    mischief::log::warn("audio: the playback device stopped playing the wave voice; the sound is cut");
    SDL_ClearAudioStream(stream_);
    return true;
}

}  // namespace tmport::audio
