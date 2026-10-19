#include "dos/audio/psg_synth.h"

#include <cmath>

namespace tmport::dos {

PsgSynth::PsgSynth(SynthMode mode, double sampleRate, NoiseType noise)
    : mode_(mode), rate_(sampleRate), noiseType_(noise) {
    switch (noiseType_) {
        case NoiseType::Ti15: feedbackMask_ = 0x4000; tap1_ = 0x01; tap2_ = 0x02; xnor_ = false; break;
        case NoiseType::Ncr8496: feedbackMask_ = 0x8000; tap1_ = 0x02; tap2_ = 0x20; xnor_ = true; break;
        case NoiseType::Sn76496: feedbackMask_ = 0x10000; tap1_ = 0x04; tap2_ = 0x08; xnor_ = false; break;
    }
    // 2 dB per attenuation step, 15 = off.
    for (int a = 0; a < 15; ++a) volume_[static_cast<size_t>(a)] = std::pow(10.0, -2.0 * a / 20.0);
    volume_[15] = 0.0;
    resetNoise();
}

void PsgSynth::resetNoise() { lfsr_ = feedbackMask_; }

void PsgSynth::write(uint8_t b) {
    ++writes_;
    const bool latch = (b & 0x80) != 0;
    if (latch) latch_ = static_cast<uint8_t>((b >> 4) & 7);
    const int r = latch_;
    if (r == 0 || r == 2 || r == 4) {
        uint16_t& p = period_[static_cast<size_t>(r >> 1)];
        p = latch ? static_cast<uint16_t>((p & 0x3F0) | (b & 0x0F)) : static_cast<uint16_t>((p & 0x00F) | ((b & 0x3F) << 4));
    } else if (r == 6) {
        // TI chips restart the shift register on every noise-control write; the NCR 8496 only on
        // a latch write that flips the white/periodic bit (as MAME models it, and as the DOSBox-X
        // recording shows: the tied noise notes of effect 1 continue one register sequence).
        const bool restart = noiseType_ != NoiseType::Ncr8496 || (latch && ((b ^ noiseCtrl_) & 4) != 0);
        noiseCtrl_ = static_cast<uint8_t>(b & 7);
        if (restart) resetNoise();
    } else {
        att_[static_cast<size_t>(r >> 1)] = static_cast<uint8_t>(b & 0x0F);
    }
}

double PsgSynth::toneFrequency(int voice) const {
    const int p = period_[static_cast<size_t>(voice)] ? period_[static_cast<size_t>(voice)] : 0x400;
    return kPsgHz / (32.0 * p);
}

bool PsgSynth::shiftNoise() {
    const bool white = (noiseCtrl_ & 4) != 0;
    bool fb = (lfsr_ & tap1_) != 0;
    if (white) {
        fb = fb != ((lfsr_ & tap2_) != 0);
        if (xnor_) fb = !fb;
    }
    lfsr_ = (lfsr_ >> 1) | (fb ? feedbackMask_ : 0u);
    return (lfsr_ & 1) != 0;
}

void PsgSynth::render(double* out, size_t n) {
    for (size_t v = 0; v < 3; ++v) {
        const double f = toneFrequency(static_cast<int>(v)), amp = kVoiceAmplitude * volume_[att_[v]];
        if (mode_ == SynthMode::Raw)
            osc_[v].render(f, amp, n, out, rate_);
        else
            osc_[v].renderCounter(f, amp, n, out, rate_);
    }
    const double amp = kVoiceAmplitude * volume_[att_[3]];
    const int nf = noiseCtrl_ & 3;
    const double shiftRate = nf == 3 ? toneFrequency(2) : kPsgHz / (512 << nf);  // shifts per second
    const double step = shiftRate / rate_;
    double ph = noisePhase_;
    if (mode_ == SynthMode::Raw) {
        // tools/dos/dos_sound.py SN76496.render: the output bit sampled once per sample.
        for (size_t i = 0; i < n; ++i) {
            ph += step;
            while (ph >= 1.0) {
                ph -= 1.0;
                shiftNoise();
            }
            if (amp != 0.0) out[i] += (lfsr_ & 1) ? amp : -amp;
        }
    } else {
        // The output bit averaged over each sample.
        for (size_t i = 0; i < n; ++i) {
            double rem = step, sum = 0.0;
            while (ph + rem >= 1.0) {
                const double seg = 1.0 - ph;
                sum += (lfsr_ & 1) ? seg : -seg;
                rem -= seg;
                ph = 0.0;
                shiftNoise();
            }
            ph += rem;
            sum += (lfsr_ & 1) ? rem : -rem;
            if (amp != 0.0) out[i] += amp * sum / step;
        }
    }
    noisePhase_ = ph;
}

}  // namespace tmport::dos
