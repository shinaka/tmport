#pragma once
// The device side of the single wave voice: what waveOutOpen/PrepareHeader/Write (start),
// waveOutReset/Unprepare/Close (stop) and MM_WOM_DONE (done) are to the original's segment 1088
// (re/docs/fmt_sound.md §5.2). Implementations: SdlWaveOutput (SDL3 audio stream), NoWaveOutput
// (no device: every sound is finished at once, as in the original), and fakes in the tests.

#include "audio/sound.h"

namespace tmport::audio {

class WaveOutput {
public:
    virtual ~WaveOutput() = default;
    // A wave device exists (Wave_Init 1088:0000: waveOutGetNumDevs() != 0).
    virtual bool available() const = 0;
    // Starts playing s from its first sample. The voice has already stopped the previous sound.
    virtual void start(const Sound& s) = 0;
    // Cuts the current sound at once (no fade).
    virtual void stop() = 0;
    // True once the last started sound has played to its end (MM_WOM_DONE).
    virtual bool done() = 0;
};

// No wave device: nothing plays and speech costs no time (fmt_sound.md §5.2).
class NoWaveOutput final : public WaveOutput {
public:
    bool available() const override { return false; }
    void start(const Sound&) override {}
    void stop() override {}
    bool done() override { return true; }
};

}  // namespace tmport::audio
