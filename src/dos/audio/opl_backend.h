#pragma once
// The AdLib music path (SST.EXE segment 4ac9, fmt_sound.md §5.5): the interface the sequencer
// drives, and a stand-in until the OPL2 driver exists.
//
// TODO(wave5): OplDriver = a port of 4ac9 (AdLib_Init 00b7, AdLib_PlayTune 0002 with its 4
// orchestrations per tune at DS:891c/8988, the 24 instrument records at DS:88bc, the 25-steps-per-
// semitone F-number table of AdLib_BuildFreqTable 067e, note on/off 029e/0432) writing registers
// to a pinned OPL2 core (ymfm, BSD). The register order can be checked against the .dro logs in
// extracted/captures/dos/audio/.
//
// With the AdLib device the original plays the effects (SFX.IBM) on the PC speaker and the tunes
// (TUNES.TAN, voices 0-2; voice 3 is ignored) on the OPL2, and the music goes on under effects.
// Until wave 5, OplFallbackSynth plays those same note-on/note-off calls as soft square tones
// (the Tandy arrangement's pitches, one tone per voice, a decaying envelope), so `--sound adlib`
// keeps the AdLib timing rules (music not frozen by effects, one rand() per tune start) and only
// the timbre differs.

#include <array>
#include <cstddef>
#include <cstdint>

#include "dos/audio/synth_common.h"

namespace tmport::dos {

class OplBackend {
public:
    virtual ~OplBackend() = default;
    virtual void init() {}                                  // 4ac9:00b7 AdLib_Init
    virtual void playTune(int tune, int variant) = 0;       // 4ac9:0002 AdLib_PlayTune (variant = rand() & 3)
    virtual void noteOn(int voice, uint8_t semitone) = 0;   // 4ac9:029e AdLib_NoteOn (semitone = note - 1 + 12 * oct)
    virtual void noteOff(int voice) = 0;                    // 4ac9:0432 AdLib_NoteOff
    // Adds n samples at the renderer's rate.
    virtual void render(double* out, size_t n) = 0;
};

class OplFallbackSynth final : public OplBackend {
public:
    explicit OplFallbackSynth(double sampleRate) : rate_(sampleRate) {}

    void playTune(int tune, int variant) override;
    void noteOn(int voice, uint8_t semitone) override;
    void noteOff(int voice) override;
    void render(double* out, size_t n) override;

    // Pitch of a sequencer semitone: semitone 36 (octave nibble 3, C) = C4, as the PSG plays it.
    static double frequency(uint8_t semitone);

private:
    struct Voice {
        BoxSquare osc;
        double freq = 0.0;
        double level = 0.0;  // current envelope level
        bool on = false;
    };
    double rate_;
    std::array<Voice, 3> voices_;
};

}  // namespace tmport::dos
