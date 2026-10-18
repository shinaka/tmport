#pragma once
// Sf2Synth: a General MIDI software synthesizer playing a SoundFont 2 file (.sf2), built on
// TinySoundFont (tsf.h by Bernhard Schelling, MIT license, fetched by CMake at a pinned commit;
// see cmake/sources_audio.cmake). It turns MIDI channel messages into interleaved stereo float
// PCM. It is the sound source of the SoundFont MIDI back end (midi_backend_soundfont.h), which
// lets the music play on Linux and macOS, where there is no system MIDI synthesizer.
//
// MIDI handling (everything the game's tunes use, re/docs/fmt_sound.md §8.1, plus the GM basics):
//   * note on / note off (note on with velocity 0 is a note off);
//   * program change; channel 10 (index 9) plays the drum kits (SoundFont bank 128);
//   * bank select (CC 0 / 32), with TinySoundFont's fallback to bank 0;
//   * volume (CC 7) and expression (CC 11) with the GM/DLS curve: gain = (vol/127)^2 * (expr/127)^2;
//   * pan (CC 10), sustain pedal (CC 64);
//   * data entry (CC 6 / 38) for RPN 0 (pitch-bend range), 1 (fine tuning), 2 (coarse tuning);
//   * all sound off (CC 120, fast release), reset all controllers (CC 121, as GM RP-015:
//     expression, sustain, pitch bend and the RPN selection reset; volume, pan, bank and program
//     stay), all notes off (CC 123; CC 124..127 imply it): notes end with their release;
//   * pitch bend (14 bit, default range +/- 2 semitones).
// Not supported (TinySoundFont has no modulators or effects): channel / key pressure, the
// modulation wheel, reverb and chorus sends.
//
// Thread-safe: messages and rendering may come from different threads (one internal mutex).

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>

struct tsf;  // TinySoundFont's handle (global namespace, as tsf.h declares it)

namespace tmport::audio {

class Sf2Synth {
public:
    // Loads a SoundFont. nullptr (and a reason in `error`) if the file cannot be read or is not
    // a usable .sf2 (the Ogg-compressed .sf3 variant is not supported). sampleRate = output rate.
    static std::unique_ptr<Sf2Synth> loadFile(const std::filesystem::path& path, int sampleRate, std::string& error);
    static std::unique_ptr<Sf2Synth> loadMemory(std::span<const uint8_t> sf2, int sampleRate, std::string& error);
    ~Sf2Synth();
    Sf2Synth(const Sf2Synth&) = delete;
    Sf2Synth& operator=(const Sf2Synth&) = delete;

    int sampleRate() const { return rate_; }
    int presetCount() const;
    // Name of the preset at bank / program ("" if the SoundFont has none there).
    std::string presetName(int bank, int program) const;

    // Applies one channel message (status 0x80..0xEF) at once. Others are ignored.
    void send(uint8_t status, uint8_t d1, uint8_t d2);
    // GM reset: every voice stops at once; all 16 channels get program 0 (channel 10 the
    // standard drum kit), volume 100, expression 127, pan centre, no sustain, pitch bend centre,
    // bend range 2 semitones, no tuning.
    void reset();
    // Renders `frames` stereo frames (interleaved L/R floats) into out, overwriting it.
    void render(float* out, int frames);
    int activeVoices() const;

    // Overall gain in dB (applies to notes started afterwards). The default leaves headroom for
    // a dozen voices at GM volumes, like TinySoundFont's own examples (FluidSynth uses -14 dB).
    static constexpr float kDefaultGainDb = -10.0f;
    void setGainDb(float db);

    // Current channel state (tests and diagnostics).
    struct ChannelInfo {
        int bank = 0, program = 0;
        uint8_t volume = 100, expression = 127;
        int pitchWheel = 8192;
    };
    ChannelInfo channel(int ch) const;

private:
    Sf2Synth(::tsf* f, int rate);
    void initChannelsLocked();
    void applyVolumeLocked(int ch);
    void programLocked(int ch, int program);

    mutable std::mutex mu_;
    ::tsf* f_ = nullptr;
    int rate_ = 44100;
    uint8_t volume_[16]{};
    uint8_t expression_[16]{};
};

}  // namespace tmport::audio
