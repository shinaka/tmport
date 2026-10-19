#include "dos/audio/opl_backend.h"

#include <cmath>

namespace tmport::dos {

namespace {

constexpr double kPeak = 0.18;          // per voice, below the PSG's 0.25 (three voices sound at once)
constexpr double kSustain = 0.35;       // fraction of the peak the decay settles at
constexpr double kDecaySeconds = 0.45;  // time constant of the decay
constexpr double kReleaseSeconds = 0.03;

}  // namespace

double OplFallbackSynth::frequency(uint8_t semitone) { return 32.703195662574829 * std::pow(2.0, semitone / 12.0); }

void OplFallbackSynth::playTune(int /*tune*/, int /*variant*/) {
    for (Voice& v : voices_) {
        v.on = false;
        v.level = 0.0;
    }
}

void OplFallbackSynth::noteOn(int voice, uint8_t semitone) {
    if (voice < 0 || voice > 2) return;  // the noise voice has no OPL channel (4ac9:029e)
    Voice& v = voices_[static_cast<size_t>(voice)];
    v.freq = frequency(semitone);
    v.on = true;
    v.level = kPeak;
}

void OplFallbackSynth::noteOff(int voice) {
    if (voice < 0 || voice > 2) return;
    voices_[static_cast<size_t>(voice)].on = false;
}

void OplFallbackSynth::render(double* out, size_t n) {
    // The envelope moves once per call (the renderer calls once per PIT tick or less): coarse but
    // smooth enough for a stand-in.
    const double dt = static_cast<double>(n) / rate_;
    for (Voice& v : voices_) {
        if (v.level <= 1e-5 && !v.on) {
            v.level = 0.0;
            continue;
        }
        v.osc.render(v.freq, v.level, n, out, rate_);
        v.osc.wrap();
        if (v.on) {
            const double target = kPeak * kSustain;
            v.level = target + (v.level - target) * std::exp(-dt / kDecaySeconds);
        } else {
            v.level *= std::exp(-dt / kReleaseSeconds);
        }
    }
}

}  // namespace tmport::dos
