#pragma once
// The PC speaker as the DOS driver uses it: PIT channel 2 in mode 3 (square wave of
// 1193182 / divisor Hz, divisor 0 = 65536) gated by port 0x61 bits 0-1 (fmt_sound.md §6.1).

#include <cstddef>
#include <cstdint>

#include "dos/audio/synth_common.h"

namespace tmport::dos {

class SpeakerSynth {
public:
    static constexpr double kAmplitude = 0.5;  // as tools/dos/dos_sound.py

    SpeakerSynth(SynthMode mode, double sampleRate) : mode_(mode), rate_(sampleRate) {}

    void setDivisor(uint16_t divisor) { divisor_ = divisor; }  // OUT 0x42 low, high
    // Port 0x61 bits 0-1. In Smooth mode a rising gate restarts the counter (a new period starts),
    // as GATE2 does in mode 3.
    void setEnabled(bool on);

    uint16_t divisor() const { return divisor_; }
    bool enabled() const { return on_; }
    double frequency() const { return kPitHz / (divisor_ ? divisor_ : 0x10000); }

    // Adds n samples of the current state to out.
    void render(double* out, size_t n);

private:
    SynthMode mode_;
    double rate_;
    uint16_t divisor_ = 0;
    bool on_ = false;
    BoxSquare osc_;
};

}  // namespace tmport::dos
