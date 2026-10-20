#pragma once
// DOS sound output (both DOS games): a ChipRenderer run inside an SDL3 audio stream's pull callback,
// so the game's sound driver advances by sample count at its tick rate whatever the frame rate is.
// The game calls the driver from its own thread; post() queues such a call to run right before the
// next tick on the render side (or a number of ticks later), as the original's calls from the main
// loop take effect at the next timer interrupt. A game may keep its own command queue instead and
// apply it from setTickHook (TM DOS: dos/audio/dos_audio_device.h).
//
// Offline (headless) use opens no device: the caller pulls samples with renderOffline(), or with
// renderOfflineTicks() in whole ticks for deterministic captures.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "dos/audio/chip_renderer.h"

struct SDL_AudioStream;

namespace tmport::dos {

struct ChipAudioConfig {
    ChipRenderConfig render;
    // SDL audio driver to force ("dummy" in tests: never audible); empty = SDL's choice.
    std::string sdlDriver;
    float volume = 0.7f;  // master gain on the synth levels (speaker square +-0.5)
};

class ChipAudioDevice {
public:
    // `renderer` (its config's sample rate is the stream's) must outlive the device. `logName`
    // prefixes the device's log lines ("dos audio").
    ChipAudioDevice(ChipRenderer& renderer, const ChipAudioConfig& cfg, std::string logName = "dos audio");
    ~ChipAudioDevice();
    ChipAudioDevice(const ChipAudioDevice&) = delete;
    ChipAudioDevice& operator=(const ChipAudioDevice&) = delete;

    // Opens an SDL playback stream (float mono at the configured rate) with the pull callback, on
    // the default device or, with `sdlDevice` != 0, bound to that already open logical device.
    // Returns false (and logs) when there is no device; renderOffline() can still be used. `what`
    // names the sound in the debug log line ("AdLib").
    bool open(uint32_t sdlDevice = 0, const std::string& what = {});
    void close();
    bool isOpen() const { return stream_ != nullptr; }
    std::string driverName() const;

    // ---- calls from the game thread (thread safe)
    // Runs fn on the render side right before the next tick, or `delay` ticks after it; calls of
    // the same tick run in the order they were posted.
    void post(std::function<void()> fn, uint32_t delay = 0);
    // Called on the render side before each tick, after the posted calls due (the game's own queue).
    void setTickHook(std::function<void()> hook);
    // Called on the render side after each rendered block (status for the game thread).
    void setAfterRender(std::function<void()> fn);
    // Runs fn with the renderer locked (no block is rendered meanwhile).
    void withRenderer(const std::function<void(ChipRenderer&)>& fn);

    // ---- engine-side controls (no effect on the driver)
    void setVolume(float v) { volume_.store(v); }
    float volume() const { return volume_.load(); }
    // Off: the output is silent but the driver keeps running (timing is unchanged).
    void setOutputEnabled(bool on) { outputEnabled_.store(on); }
    bool outputEnabled() const { return outputEnabled_.load(); }

    // ---- headless (only while no SDL stream is open)
    // Renders n float samples.
    size_t renderOffline(float* out, size_t n);
    // Appends the rest of the current tick's samples (if renderOffline stopped inside one) and those
    // of the next `ticks` ticks; returns their number. Calls posted before take effect at the first
    // of these ticks (a delayed one that many ticks later).
    size_t renderOfflineTicks(uint32_t ticks, std::vector<float>& out);

    int sampleRate() const { return renderer_.config().sampleRate; }
    ChipRenderer& renderer() { return renderer_; }

private:
    struct Posted {
        std::function<void()> fn;
        uint32_t delay = 0;
    };
    void beforeTick();  // render side: the posted calls due, the others one tick closer
    void renderFloat(float* out, size_t n);
    static void callback(void* user, SDL_AudioStream* stream, int additional, int total);

    ChipRenderer& renderer_;
    ChipAudioConfig cfg_;
    std::string logName_;
    std::function<void()> tickHook_;
    std::function<void()> afterRender_;

    std::mutex renderMutex_;  // the renderer: audio thread, or the offline caller
    std::vector<double> scratch_;
    std::vector<float> floatBuf_;  // the callback's output block

    std::mutex queueMutex_;
    std::vector<Posted> posted_;
    std::atomic<bool> pending_{false};

    std::atomic<float> volume_;
    std::atomic<bool> outputEnabled_{true};

    SDL_AudioStream* stream_ = nullptr;
    bool ownsDevice_ = false;
    bool sdlInit_ = false;
};

}  // namespace tmport::dos
