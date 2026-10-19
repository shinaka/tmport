#pragma once
// Shared pieces of the DOS sound synths (speaker_synth, psg_synth, the OPL stand-in): clock
// constants, the render mode and a box-filtered square oscillator.

#include <cmath>
#include <cstddef>

namespace tmport::dos {

inline constexpr double kPitHz = 1193182.0;     // 8253 input clock (14.31818 MHz / 12)
inline constexpr int kTimerDivisor = 0x13B1;    // 1000:03f8: INT 8 reprogrammed to 236.70 Hz
inline constexpr double kPsgHz = 3579545.0;     // SN76496 clock on the Tandy 1000 / PCjr
inline constexpr double kPitTickHz = kPitHz / kTimerDivisor;

enum class SynthMode {
    // Sample for sample what tools/dos/dos_sound.py renders (box-filtered squares, TI noise,
    // unfiltered): the reference for the comparison tests.
    Raw,
    // For listening: the same oscillators with continuous phase wrapping, PIT gate restarts,
    // box-filtered noise with the chosen LFSR, then a gentle low-pass and a DC blocker.
    Smooth,
};

// A square wave (+amp for the first half period, -amp for the second) averaged over each output
// sample: the exact integral of the ideal wave, a first-order band limit. render() adds to out.
class BoxSquare {
public:
    void render(double freq, double amp, size_t n, double* out, double rate) {
        if (amp == 0.0 || freq <= 0.0) {
            if (freq > 0.0) phase_ += freq * static_cast<double>(n) / rate;  // keep the phase running
            return;
        }
        const double d = freq / rate;
        double ph = phase_;
        if (d >= 0.5) {  // above the Nyquist frequency: averages to about 0
            phase_ = ph + d * static_cast<double>(n);
            return;
        }
        double g0 = integral(ph);
        const double inv = amp / d;
        for (size_t i = 0; i < n; ++i) {
            ph += d;
            const double g1 = integral(ph);
            out[i] += (g1 - g0) * inv;
            g0 = g1;
        }
        phase_ = ph;
    }
    // Smooth mode: the same, with the hardware's counter reload. A PIT channel in mode 3 and an
    // SN76496 tone counter take a new period only when the current half period ends, so a note
    // that follows a long period (e.g. the period 0 = 1024 the driver writes when it turns a PSG
    // voice off) starts up to that half period late, and sweeps change pitch on half-period
    // boundaries. The phase keeps running while the voice is silent, as the counters do.
    void renderCounter(double freq, double amp, size_t n, double* out, double rate) {
        if (freq != target_) {
            if (cur_ <= 0.0) {
                cur_ = freq;  // first period after a restart: loaded at once
            } else if (!pending_) {
                pending_ = true;
                boundary_ = (std::floor(phase_ * 2.0) + 1.0) / 2.0;  // end of the current half period
            }
            target_ = freq;
        }
        double ph = phase_;
        if (amp == 0.0 && !pending_) {
            phase_ = ph + cur_ / rate * static_cast<double>(n);
            wrap();
            return;
        }
        for (size_t i = 0; i < n; ++i) {
            const double d = cur_ / rate;
            if (pending_ && ph + d >= boundary_) {
                const double tau = (boundary_ - ph) / d;  // part of this sample before the reload
                const double d2 = target_ / rate;
                const double end = boundary_ + (1.0 - tau) * d2;
                out[i] += amp * ((integral(boundary_) - integral(ph)) / d + (integral(end) - integral(boundary_)) / d2);
                ph = end;
                cur_ = target_;
                pending_ = false;
                continue;
            }
            out[i] += amp * (integral(ph + d) - integral(ph)) / d;
            ph += d;
        }
        if (pending_) boundary_ -= std::floor(ph);
        phase_ = ph - std::floor(ph);
    }
    // A counter restart (PIT GATE2 rising edge): a new period starts now with the next frequency.
    void restart() {
        phase_ = 0.0;
        cur_ = 0.0;
        target_ = 0.0;
        pending_ = false;
    }

    // Drops whole periods (Smooth mode, so a long session keeps its precision).
    void wrap() { phase_ -= std::floor(phase_); }
    void reset() { phase_ = 0.0; }
    double phase() const { return phase_; }

private:
    // Integral over one period-normalised square wave from 0 to x (a triangle).
    static double integral(double x) {
        const double f = x - std::floor(x);
        return f < 0.5 ? f : 1.0 - f;
    }
    double phase_ = 0.0;
    // renderCounter state
    double cur_ = 0.0;     // frequency in effect
    double target_ = 0.0;  // last frequency written
    double boundary_ = 0.0;
    bool pending_ = false;
};

}  // namespace tmport::dos
