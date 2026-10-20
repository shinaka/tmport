#include "dos/audio/chip_audio_device.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <iterator>

#include "util/log.h"

namespace tmport::dos {

ChipAudioDevice::ChipAudioDevice(ChipRenderer& renderer, const ChipAudioConfig& cfg, std::string logName)
    : renderer_(renderer), cfg_(cfg), logName_(std::move(logName)), volume_(cfg.volume) {
    renderer_.setTickHook([this] { beforeTick(); });
}

ChipAudioDevice::~ChipAudioDevice() {
    close();
    renderer_.setTickHook({});
}

// ----------------------------------------------------------------------------------- SDL side

bool ChipAudioDevice::open(uint32_t sdlDevice, const std::string& what) {
    if (stream_) return true;
    if (!cfg_.sdlDriver.empty()) SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, cfg_.sdlDriver.c_str(), SDL_HINT_OVERRIDE);
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log::warn("{}: SDL audio initialisation failed ({}); sound is off", logName_, SDL_GetError());
        return false;
    }
    sdlInit_ = true;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, sampleRate()};
    if (sdlDevice) {
        stream_ = SDL_CreateAudioStream(&spec, nullptr);
        if (stream_ && (!SDL_SetAudioStreamGetCallback(stream_, &ChipAudioDevice::callback, this) ||
                        !SDL_BindAudioStream(sdlDevice, stream_))) {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
        ownsDevice_ = false;
    } else {
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &ChipAudioDevice::callback, this);
        ownsDevice_ = true;
    }
    if (!stream_) {
        log::warn("{}: no playback stream ({}); sound is off", logName_, SDL_GetError());
        close();
        return false;
    }
    if (ownsDevice_) SDL_ResumeAudioStreamDevice(stream_);
    log::debug("{}: {} sound on SDL driver '{}' at {} Hz", logName_, what, driverName(), sampleRate());
    return true;
}

void ChipAudioDevice::close() {
    if (stream_) SDL_DestroyAudioStream(stream_);  // a stream from SDL_OpenAudioDeviceStream closes its device
    stream_ = nullptr;
    if (sdlInit_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sdlInit_ = false;
}

std::string ChipAudioDevice::driverName() const {
    if (!sdlInit_) return {};
    const char* n = SDL_GetCurrentAudioDriver();
    return n ? n : "";
}

void ChipAudioDevice::callback(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto* self = static_cast<ChipAudioDevice*>(user);
    if (additional <= 0) return;
    const size_t n = static_cast<size_t>(additional) / sizeof(float);
    std::lock_guard lock(self->renderMutex_);
    if (self->floatBuf_.size() < n) self->floatBuf_.resize(n);
    self->renderFloat(self->floatBuf_.data(), n);
    SDL_PutAudioStreamData(stream, self->floatBuf_.data(), static_cast<int>(n * sizeof(float)));
}

size_t ChipAudioDevice::renderOffline(float* out, size_t n) {
    if (stream_) return 0;
    std::lock_guard lock(renderMutex_);
    renderFloat(out, n);
    return n;
}

size_t ChipAudioDevice::renderOfflineTicks(uint32_t ticks, std::vector<float>& out) {
    if (stream_) return 0;
    std::lock_guard lock(renderMutex_);
    scratch_.clear();
    if (renderer_.samplesToNextTick() != 0) renderer_.renderTick(scratch_);  // the current tick's rest
    for (uint32_t t = 0; t < ticks; ++t) renderer_.renderTick(scratch_);
    const float g = outputEnabled_.load() ? volume_.load() : 0.0f;
    const size_t at = out.size();
    out.resize(at + scratch_.size());
    for (size_t i = 0; i < scratch_.size(); ++i) out[at + i] = std::clamp(static_cast<float>(scratch_[i]) * g, -1.0f, 1.0f);
    if (afterRender_) afterRender_();
    return scratch_.size();
}

void ChipAudioDevice::renderFloat(float* out, size_t n) {
    if (scratch_.size() < n) scratch_.resize(n);
    renderer_.render(scratch_.data(), n);
    const float g = outputEnabled_.load() ? volume_.load() : 0.0f;
    for (size_t i = 0; i < n; ++i) out[i] = std::clamp(static_cast<float>(scratch_[i]) * g, -1.0f, 1.0f);
    if (afterRender_) afterRender_();
}

void ChipAudioDevice::withRenderer(const std::function<void(ChipRenderer&)>& fn) {
    std::lock_guard lock(renderMutex_);
    fn(renderer_);
}

void ChipAudioDevice::setTickHook(std::function<void()> hook) {
    std::lock_guard lock(renderMutex_);
    tickHook_ = std::move(hook);
}

void ChipAudioDevice::setAfterRender(std::function<void()> fn) {
    std::lock_guard lock(renderMutex_);
    afterRender_ = std::move(fn);
}

// ---------------------------------------------------------------------------- posted calls

void ChipAudioDevice::post(std::function<void()> fn, uint32_t delay) {
    std::lock_guard lock(queueMutex_);
    posted_.push_back({std::move(fn), delay});
    pending_.store(true);
}

void ChipAudioDevice::beforeTick() {
    if (pending_.load()) {
        std::vector<Posted> calls;
        {
            std::lock_guard lock(queueMutex_);
            calls.swap(posted_);
            pending_.store(false);
        }
        std::vector<Posted> later;  // delayed calls, one tick closer now (kept in their order)
        for (Posted& c : calls) {
            if (c.delay > 0) {
                --c.delay;
                later.push_back(std::move(c));
                continue;
            }
            if (c.fn) c.fn();
        }
        if (!later.empty()) {
            // Back in front of whatever the game posted meanwhile (posted after them).
            std::lock_guard lock(queueMutex_);
            posted_.insert(posted_.begin(), std::make_move_iterator(later.begin()), std::make_move_iterator(later.end()));
            pending_.store(true);
        }
    }
    if (tickHook_) tickHook_();
}

}  // namespace tmport::dos
