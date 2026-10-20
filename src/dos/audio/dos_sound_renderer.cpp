#include "dos/audio/dos_sound_renderer.h"

#include <algorithm>
#include <cmath>

namespace tmport::dos {

namespace {

std::unique_ptr<OplBackend> defaultOpl(const SoundBank& bank, const DosRenderConfig& cfg, std::unique_ptr<OplBackend> given) {
    if (given || bank.device != SoundCard::AdLib) return given;
    return std::make_unique<AdLibOpl>(*bank.tables, cfg.sampleRate);
}

}  // namespace

DosSoundRenderer::DosSoundRenderer(const SoundBank& bank, DosRenderConfig cfg, std::unique_ptr<OplBackend> opl)
    : opl_(defaultOpl(bank, cfg, std::move(opl))), chips_(cfg, opl_.get()), seq_(bank, *this) {
    chips_.setTickSource(this);
}

// ------------------------------------------------------------------------- offline helpers

OfflineRender renderEntry(DosSoundRenderer& r, bool tune, int id, int loops, double maxSeconds,
                          const std::function<void()>& afterTick, int variant) {
    Sequencer& s = r.sequencer();
    if (tune)
        s.musicPlay(id, variant);
    else
        s.sfxPlay(id);
    OfflineRender res;
    const auto maxTicks = static_cast<uint64_t>(maxSeconds * kPitTickHz);
    while (r.ticks() < maxTicks) {
        if (tune) s.setMusicLoop(s.musicLoops() + 1 < static_cast<uint32_t>(std::max(loops, 1)));
        r.renderTick(res.samples);
        if (afterTick) afterTick();
        if (!(tune ? s.musicActive() : s.sfxActive())) break;
    }
    res.ticks = r.ticks();
    const int rate = r.config().sampleRate;
    if (r.config().mode == SynthMode::Raw) {
        res.samples.resize(res.samples.size() + static_cast<size_t>(rate * 0.05), 0.0);  // int(RATE * 0.05)
    } else {
        std::vector<double> tail(static_cast<size_t>(rate * 0.2));
        r.render(tail.data(), tail.size());
        res.samples.insert(res.samples.end(), tail.begin(), tail.end());
    }
    return res;
}

std::vector<int16_t> toPcm16Normalized(std::span<const double> samples) {
    double peak = 1e-9;
    for (double v : samples) peak = std::max(peak, std::abs(v));
    return toPcm16(samples, std::min(1.0, 0.9 / peak));
}

std::vector<int16_t> toPcm16(std::span<const double> samples, double gain) {
    const double g = gain * 32767;  // dos_sound.py: min(1.0, 0.9 / peak) * 32767
    std::vector<int16_t> pcm(samples.size());
    for (size_t i = 0; i < samples.size(); ++i)  // nearbyint: round half to even, like Python's round()
        pcm[i] = static_cast<int16_t>(std::clamp(std::nearbyint(samples[i] * g), -32768.0, 32767.0));
    return pcm;
}

}  // namespace tmport::dos
