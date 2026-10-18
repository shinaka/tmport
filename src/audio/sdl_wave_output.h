#pragma once
// SdlWaveOutput: the wave voice on an SDL3 audio device. It opens a logical playback device
// (the default device, in its own format) and binds one SDL_AudioStream to it; the stream's input
// format is 8-bit unsigned mono at the sound's header rate (SDL_AUDIO_U8), and SDL converts to the
// device format. A new sound clears the stream (cut) and queues the whole PCM block, like the
// original's single WAVEHDR. Other streams may join the device (deviceId()): the SoundFont music
// stream does, and SDL mixes them.

#include <string>

#include "audio/wave_output.h"

struct SDL_AudioStream;

namespace tmport::audio {

class SdlWaveOutput final : public WaveOutput {
public:
    // driver: SDL audio driver to force (e.g. "dummy" for tests and headless runs, which plays
    // nothing audibly but consumes the data in real time); empty = SDL's choice (the
    // SDL_AUDIO_DRIVER environment variable still applies).
    explicit SdlWaveOutput(std::string driver = {});
    ~SdlWaveOutput() override;
    SdlWaveOutput(const SdlWaveOutput&) = delete;
    SdlWaveOutput& operator=(const SdlWaveOutput&) = delete;

    // Initialises the SDL audio subsystem and opens the default playback device. Returns false
    // (and logs) if there is no usable device; the voice is then unavailable.
    bool open();
    void close();
    // Name of the SDL audio driver in use (empty when closed).
    std::string driverName() const;
    // The SDL logical playback device (SDL_AudioDeviceID) while open, else 0.
    uint32_t deviceId() const { return device_; }

    bool available() const override { return stream_ != nullptr; }
    void start(const Sound& s) override;
    void stop() override;
    bool done() override;

private:
    std::string driver_;
    SDL_AudioStream* stream_ = nullptr;
    uint32_t device_ = 0;
    bool sdlInit_ = false;
    uint32_t rate_ = 0;
};

}  // namespace tmport::audio
