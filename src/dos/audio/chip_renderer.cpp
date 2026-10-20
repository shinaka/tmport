#include "dos/audio/chip_renderer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tmport::dos {

ChipRenderer::ChipRenderer(const ChipRenderConfig& cfg, OplOutput* opl)
    : cfg_(cfg),
      speaker_(cfg.mode, cfg.sampleRate),
      psg_(cfg.mode, cfg.sampleRate, cfg.noise),
      opl_(opl),
      // dos_sound.py: acc += RATE / TICK_HZ, with TICK_HZ = PIT_HZ / TIMER_DIVISOR
      samplesPerTick_(cfg.sampleRate / cfg.tickHz) {
    if (cfg_.mode == SynthMode::Smooth && cfg_.lowPassHz > 0.0 && cfg_.lowPassHz < cfg_.sampleRate * 0.45) {
        const double w = 2.0 * std::numbers::pi * cfg_.lowPassHz / cfg_.sampleRate;
        const double alpha = std::sin(w) / std::numbers::sqrt2;  // sin(w) / (2 Q), Q = 1/sqrt(2) (Butterworth)
        const double c = std::cos(w);
        const double a0 = 1.0 + alpha;
        b0_ = (1.0 - c) / 2.0 / a0;
        b1_ = (1.0 - c) / a0;
        b2_ = b0_;
        a1_ = -2.0 * c / a0;
        a2_ = (1.0 - alpha) / a0;
    }
    if (cfg_.mode == SynthMode::Smooth && cfg_.dcBlockHz > 0.0)
        dcR_ = std::exp(-2.0 * std::numbers::pi * cfg_.dcBlockHz / cfg_.sampleRate);
}

void ChipRenderer::beginTick() {
    if (tickHook_) tickHook_();
    if (source_) source_->soundTick();
    ++ticks_;
    acc_ += samplesPerTick_;
    const auto k = static_cast<size_t>(acc_);
    acc_ -= static_cast<double>(k);
    left_ = k;
}

void ChipRenderer::synthesize(double* out, size_t n) {
    speaker_.render(out, n);
    psg_.render(out, n);
}

void ChipRenderer::synthesizeOpl(double* out, size_t n) {
    if (oplSynthesis_)
        opl_->render(out, n);
    else
        opl_->skip(n);
}

void ChipRenderer::render(double* out, size_t n) {
    std::fill(out, out + n, 0.0);
    if (opl_) oplBuf_.assign(n, 0.0);
    size_t done = 0;
    while (done < n) {
        if (left_ == 0) {
            beginTick();
            continue;
        }
        const size_t k = std::min(left_, n - done);
        synthesize(out + done, k);
        if (opl_) synthesizeOpl(oplBuf_.data() + done, k);
        left_ -= k;
        done += k;
    }
    filter(out, n);
    if (opl_)
        for (size_t i = 0; i < n; ++i) out[i] = (out[i] + oplBuf_[i]) * cfg_.oplMixLevel;
}

size_t ChipRenderer::renderTick(std::vector<double>& out) {
    if (left_ == 0) beginTick();  // else: finish the current block first
    const size_t at = out.size(), k = left_;
    out.resize(at + k, 0.0);
    synthesize(out.data() + at, k);
    if (opl_) {
        oplBuf_.assign(k, 0.0);
        synthesizeOpl(oplBuf_.data(), k);
    }
    left_ = 0;
    filter(out.data() + at, k);
    if (opl_)
        for (size_t i = 0; i < k; ++i) out[at + i] = (out[at + i] + oplBuf_[i]) * cfg_.oplMixLevel;
    return k;
}

void ChipRenderer::filter(double* out, size_t n) {
    if (cfg_.mode != SynthMode::Smooth) return;
    const bool lp = a1_ != 0.0 || a2_ != 0.0;
    for (size_t i = 0; i < n; ++i) {
        double x = out[i];
        if (lp) {  // transposed direct form II
            const double y = b0_ * x + z1_;
            z1_ = b1_ * x - a1_ * y + z2_;
            z2_ = b2_ * x - a2_ * y;
            x = y;
        }
        if (dcR_ != 0.0) {
            const double y = x - dcX_ + dcR_ * dcY_;
            dcX_ = x;
            dcY_ = y;
            x = y;
        }
        out[i] = x;
    }
}

}  // namespace tmport::dos
