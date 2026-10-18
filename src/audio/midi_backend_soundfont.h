#pragma once
// SoundFontMidiBackend: the cross-platform MIDI output. Channel messages from the sequencer go
// to a software synthesizer (Sf2Synth: TinySoundFont on a user-supplied General MIDI .sf2),
// which renders into an SDL3 audio stream bound to the AudioSystem's playback device, where SDL
// mixes it with the wave voice. Rendering applies the music volume.
//
// Timing: the sequencer runs on its own thread and the device pulls audio on SDL's thread in
// blocks of several milliseconds. To keep note timing independent of the block size, send()
// stamps every message with the clock and queues it; rendering places each message at its own
// sample: in device mode a block covers the clock interval ending `latencyUs` before "now" (a
// constant delay instead of block-sized jitter), in offline mode the caller says where the block
// starts (renderAt) and drives the clock itself (the --midi-render tool, the tests).
//
// Stop / pause / resume: MidiSequencer::stop() sends note-offs for the sounding notes and
// allNotesOff() (sustain off + all notes off on every channel): notes end with their release, as
// on a hardware synth. A resume re-sends the chased controllers and programs before the notes.
// close() silences everything at once and releases the SDL stream.

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audio/midi_backend.h"
#include "audio/sf2_synth.h"

struct SDL_AudioStream;

namespace tmport::audio {

class SoundFontMidiBackend final : public MidiBackend {
public:
    enum class Output {
        Device,   // SDL playback stream (the game)
        Offline,  // no device: the owner calls renderAt() (render tool, tests)
    };
    struct Config {
        std::filesystem::path soundFont;   // loaded by open() unless `synth` is given
        std::shared_ptr<Sf2Synth> synth;   // an already loaded synthesizer
        Output output = Output::Device;
        // Device mode: the SDL logical playback device to join (the wave voice's, so SDL mixes
        // both). 0 = open the default playback device itself.
        uint32_t device = 0;
        float volume = 1.0f;       // music volume: 0..1 (a factor on the rendered samples)
        int sampleRate = 44100;    // rate for loading `soundFont`; device mode: 0 = the device's rate
        MicroClock clock;          // stamps messages; default realClockUs
        uint64_t latencyUs = 50'000;  // device mode: constant scheduling delay
    };

    explicit SoundFontMidiBackend(Config config);
    ~SoundFontMidiBackend() override;
    SoundFontMidiBackend(const SoundFontMidiBackend&) = delete;
    SoundFontMidiBackend& operator=(const SoundFontMidiBackend&) = delete;

    std::string name() const override { return "soundfont"; }
    // Loads the SoundFont (if needed), resets the synthesizer to GM defaults and, in device mode,
    // binds the music stream to the playback device. false (logged) if any of it fails.
    bool open() override;
    void close() override;
    bool isOpen() const override { return open_.load(); }
    void send(uint8_t status, uint8_t d1, uint8_t d2) override;

    // Renders `frames` stereo frames whose first frame is at clock time blockStartUs. Queued
    // messages stamped before the block apply at its start, those inside it at their own frame;
    // later ones stay queued. Works in both modes (device mode calls it from renderRealtime).
    void renderAt(float* out, int frames, uint64_t blockStartUs);
    // Device mode: renders the next block of the real-time stream (called by the SDL callback;
    // public for tests with a fake clock).
    void renderRealtime(float* out, int frames);

    void setVolume(float volume);
    float volume() const;
    Sf2Synth* synth() const { return synth_.get(); }
    int sampleRate() const { return synth_ ? synth_->sampleRate() : 0; }
    uint32_t device() const { return device_; }
    size_t queuedMessages() const;
    uint64_t framesRendered() const;
    float peakSinceOpen() const;  // largest |sample| rendered since open (diagnostics, tests)

private:
    struct Stamped {
        uint64_t us;
        uint8_t status, d1, d2;
    };
    bool openDevice();
    void closeDevice();
    static void streamCallback(void* user, SDL_AudioStream* stream, int additional, int total);

    Config cfg_;
    std::shared_ptr<Sf2Synth> synth_;
    std::atomic<bool> open_{false};

    mutable std::mutex queueMu_;
    std::deque<Stamped> queue_;

    // Render state (render thread; the stats are read under statsMu_).
    mutable std::mutex statsMu_;
    uint64_t framesRendered_ = 0;
    float peak_ = 0.0f;
    float volume_ = 1.0f;
    bool rtValid_ = false;
    uint64_t rtBaseUs_ = 0;     // clock time of rtFrames_ == 0
    uint64_t rtFrames_ = 0;     // frames rendered since rtBaseUs_
    std::vector<float> cbBuffer_;

    // SDL.
    SDL_AudioStream* stream_ = nullptr;
    uint32_t device_ = 0;
    bool ownDevice_ = false;
    bool sdlInit_ = false;
};

// The back end for a SoundFont file on an SDL device (0 = the default device). Not opened yet.
std::unique_ptr<MidiBackend> createSoundFontMidiBackend(const std::string& soundFontUtf8, uint32_t sdlDevice, float volume);

}  // namespace tmport::audio
