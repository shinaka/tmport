#pragma once
// MidiBackend: where the sequencer sends channel messages. The original hands whole files to the
// Windows MCI sequencer, which drives the MIDI Mapper (re/docs/fmt_sound.md §8.2-8.3). Back ends:
//   * WinMM midiOut on MIDI_MAPPER (Windows; the closest to the original path),
//   * Null (no output; used by tests, headless runs and platforms without a synth),
//   * Recording (keeps every message with its clock time; tests and offline checks),
//   * SoundFont (software synthesizer on a General MIDI .sf2 file, rendered into an SDL audio
//     stream next to the wave voice; every platform: midi_backend_soundfont.h).

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tmport::audio {

// Microsecond clock (monotonic). The real one is SDL_GetTicksNS() / 1000; tests use fakes.
using MicroClock = std::function<uint64_t()>;
uint64_t realClockUs();

class MidiBackend {
public:
    virtual ~MidiBackend() = default;
    virtual std::string name() const = 0;
    // Opens the output. Returns false if it is unusable (music then stays silent).
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    // One channel message (status 0x80..0xEF; d2 is ignored for 2-byte messages).
    virtual void send(uint8_t status, uint8_t d1, uint8_t d2) = 0;
    // Stop/pause silence: sustain off (CC 64) and all notes off (CC 123) on all 16 channels.
    // WinMM additionally resets the device (midiOutReset).
    virtual void allNotesOff();

    uint64_t messageCount() const { return messages_; }

protected:
    uint64_t messages_ = 0;
};

class NullMidiBackend final : public MidiBackend {
public:
    std::string name() const override { return "null"; }
    bool open() override {
        open_ = true;
        return true;
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    void send(uint8_t, uint8_t, uint8_t) override { ++messages_; }

private:
    bool open_ = false;
};

class RecordingMidiBackend final : public MidiBackend {
public:
    struct Message {
        uint64_t timeUs;
        uint8_t status, d1, d2;
    };
    explicit RecordingMidiBackend(MicroClock clock) : clock_(std::move(clock)) {}
    std::string name() const override { return "recording"; }
    bool open() override {
        open_ = true;
        return true;
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    void send(uint8_t status, uint8_t d1, uint8_t d2) override {
        ++messages_;
        log.push_back({clock_ ? clock_() : 0, status, d1, d2});
    }
    void allNotesOff() override {
        ++silences;
        MidiBackend::allNotesOff();
    }

    std::vector<Message> log;
    int silences = 0;

private:
    MicroClock clock_;
    bool open_ = false;
};

enum class MidiBackendKind {
    Auto,       // WinMM on Windows; elsewhere SoundFont if one is configured, else Null
    Null,
    WinMM,      // Windows only
    SoundFont,  // software synthesizer on a .sf2 file (any platform)
};

struct MidiBackendConfig {
    MidiBackendKind kind = MidiBackendKind::Auto;
    std::string soundFont;   // .sf2 path (UTF-8) for the SoundFont back end; empty = none configured
    uint32_t sdlDevice = 0;  // SDL logical playback device to share (the wave voice's); 0 = default device
    float musicVolume = 1.0f;  // 0..1, SoundFont back end only
};

// What Auto means on this platform for a configuration (never returns Auto).
MidiBackendKind resolveMidiBackendKind(MidiBackendKind kind, bool soundFontConfigured);
// Creates (does not open) a back end. WinMM off Windows yields nullptr. Auto without a SoundFont
// off Windows yields Null and logs once how to configure one.
std::unique_ptr<MidiBackend> createMidiBackend(const MidiBackendConfig& config);
std::unique_ptr<MidiBackend> createMidiBackend(MidiBackendKind kind);  // no SoundFont configured
// WinMM midiOut on MIDI_MAPPER (midi_backend_winmm.cpp); nullptr off Windows.
std::unique_ptr<MidiBackend> createWinMmMidiBackend();

}  // namespace tmport::audio
