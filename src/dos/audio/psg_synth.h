#pragma once
// SN76496-family PSG (Tandy 1000: NCR 8496; PCjr: SN76496; PS/1 audio card on port 0x205):
// three square-wave tone voices and a noise voice, 4-bit attenuation in 2 dB steps
// (fmt_sound.md §6.2). Written one byte at a time like the chip's port.
//
// Register byte: latch `1 rrr dddd` (rrr: 0/2/4 tone period low bits of voice 0/1/2, 1/3/5/7
// attenuation of voice 0..3, 6 noise control), data `0 x dddddd` = the upper 6 period bits of the
// latched register (a data byte after a noise or attenuation latch rewrites that register).
// Tone frequency = clock / (32 N), N = 0 taken as 1024 (TI behaviour). Noise shift rate =
// clock / 512, / 1024, / 2048, or the tone-3 frequency (voice 2). In Smooth mode a new tone
// period starts when the current half period ends (BoxSquare::renderCounter).
//
// Noise shift register (fmt_sound.md §6.2, open in §10 before the recording settled it):
//   Ti15     15 bits, feedback bit0 ^ bit1 (TI SN76489), restarted by every noise-control write:
//            what tools/dos/dos_sound.py renders (use it with Raw mode for the oracle);
//   Ncr8496  16 bits, taps 0x0002 and 0x0020 with XNOR feedback, restarted only by a latch write
//            that flips the white/periodic bit (MAME's NCR 8496 model): the default. The DOSBox-X
//            Tandy recording decodes to exactly this register: its effect-1 noise satisfies
//            o[n+16] = !(o[n+1] ^ o[n+5]) for all 3368 decoded bits, the first 506 bits after the
//            restart equal the engine's sequence from 0x8000, and the later tied noise notes
//            continue the same sequence (no restart);
//   Sn76496  17 bits, taps 0x0004 and 0x0008, restarted by every write (the PCjr's chip in MAME).
// Only effect 1 uses the noise voice (white noise clocked by the inaudible voice 2).

#include <array>
#include <cstddef>
#include <cstdint>

#include "dos/audio/synth_common.h"

namespace tmport::dos {

enum class NoiseType { Ti15, Ncr8496, Sn76496 };

class PsgSynth {
public:
    static constexpr double kVoiceAmplitude = 0.25;  // per voice at attenuation 0 (as dos_sound.py)

    PsgSynth(SynthMode mode, double sampleRate, NoiseType noise = NoiseType::Ncr8496);

    void write(uint8_t value);  // one byte to the chip's port

    uint16_t period(int voice) const { return period_[static_cast<size_t>(voice)]; }
    uint8_t attenuation(int voice) const { return att_[static_cast<size_t>(voice)]; }
    uint8_t noiseControl() const { return noiseCtrl_; }
    double toneFrequency(int voice) const;
    uint32_t writes() const { return writes_; }

    // Adds n samples of the current state to out (voices 0, 1, 2, then noise).
    void render(double* out, size_t n);

private:
    void resetNoise();
    bool shiftNoise();  // one shift; returns the new output bit

    SynthMode mode_;
    double rate_;
    NoiseType noiseType_;
    uint32_t feedbackMask_, tap1_, tap2_;
    bool xnor_;

    std::array<uint16_t, 3> period_{};
    std::array<uint8_t, 4> att_{15, 15, 15, 15};
    uint8_t noiseCtrl_ = 0;
    uint8_t latch_ = 0;
    uint32_t writes_ = 0;
    std::array<BoxSquare, 3> osc_;
    std::array<double, 16> volume_{};
    uint32_t lfsr_ = 0;
    double noisePhase_ = 0.0;
};

}  // namespace tmport::dos
