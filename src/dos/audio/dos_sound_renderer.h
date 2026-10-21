#pragma once
// The DOS sound driver as a sample source: the sequencer (491b) advanced by sample count at the
// PIT rate 1193182 / 5041 Hz, and the device models it writes to (fmt_sound.md §9). Single
// threaded; DosAudioDevice adds the SDL stream and the command queue around it.
//
// The chips, the clock and the mix are the engine's ChipRenderer (mischief/dos/audio/chip_renderer.h),
// with the sequencer as its tick source: the sequencer ticks at the start of each tick's block of
// samples; block lengths come from a running fractional accumulator (samples per tick = rate * 5041
// / 1193182, about 202.8 at 48 kHz), the same arithmetic as tools/dos/dos_sound.py, so timing never
// depends on the video frame rate and Raw renders match the oracle sample for sample.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "dos/audio/opl_backend.h"
#include "dos/audio/sequencer.h"
#include "dos/audio/sound_data.h"
#include "mischief/dos/audio/chip_renderer.h"
#include "mischief/dos/audio/psg_synth.h"
#include "mischief/dos/audio/speaker_synth.h"

namespace tmport::dos {

// The AdLib device's mix (the speaker's effects and the OPL's tunes, in DOSBox-X's balance) at this
// level: the loudest tune passages and an effect over them stay below full scale at the device's
// default volume. The speaker and the Tandy devices are not affected.
inline constexpr double kAdLibMixLevel = 0.75;

// The render settings: the engine's (sample rate, synth mode, noise, filters; the tick rate and the
// OPL mix level keep their defaults, kPitTickHz and kAdLibMixLevel).
using DosRenderConfig = mischief::dos::ChipRenderConfig;
static_assert(mischief::dos::ChipRenderConfig{}.oplMixLevel == kAdLibMixLevel && mischief::dos::ChipRenderConfig{}.tickHz == mischief::dos::kPitTickHz);

class DosSoundRenderer final : private SoundPorts, private mischief::dos::SoundTickSource {
public:
    // `bank` must outlive the renderer. With the AdLib device and no back end given, the tunes play
    // on the original's AdLib driver and an emulated OPL2 (AdLibOpl, opl_backend.h). The OPL's output
    // is added after the speaker's Smooth-mode filters (they model the speaker, not the AdLib card).
    DosSoundRenderer(const SoundBank& bank, DosRenderConfig cfg, std::unique_ptr<OplBackend> opl = nullptr);

    Sequencer& sequencer() { return seq_; }
    const Sequencer& sequencer() const { return seq_; }
    const DosRenderConfig& config() const { return chips_.config(); }
    double samplesPerTick() const { return chips_.samplesPerTick(); }
    // The chips and the clock (the engine's renderer; ChipAudioDevice plays it).
    mischief::dos::ChipRenderer& chips() { return chips_; }

    // Called right before each PIT tick: the moment commands from the game take effect (between two
    // interrupts, like the original's calls from the main loop).
    void setTickHook(std::function<void()> hook) { chips_.setTickHook(std::move(hook)); }

    // Writes n mono samples (synth levels: the speaker square is +-0.5, a PSG voice +-0.25; in
    // Smooth mode after the filters). Ticks happen at block boundaries inside the call.
    void render(double* out, size_t n) { chips_.render(out, n); }
    // Runs the next PIT tick and appends exactly its block of samples to out (Raw-mode tools: this
    // keeps the arithmetic identical to dos_sound.py, which renders one block per tick). Returns
    // the number of samples appended.
    size_t renderTick(std::vector<double>& out) { return chips_.renderTick(out); }

    uint64_t ticks() const { return chips_.ticks(); }
    // Samples left in the current tick's block (0 = the next render starts with a tick).
    size_t samplesToNextTick() const { return chips_.samplesToNextTick(); }

    mischief::dos::SpeakerSynth& speaker() { return chips_.speaker(); }
    mischief::dos::PsgSynth& psg() { return chips_.psg(); }
    // The AdLib back end (nullptr on the speaker and the Tandy).
    OplBackend* opl() { return opl_.get(); }
    // A DOSBox-X state's AdLib driver from its DGROUP image (before Sequencer::restore with
    // oplRestored); false without an AdLib back end.
    bool restoreOpl(std::span<const uint8_t> dgroup) { return opl_ && opl_->restore(dgroup); }
    // Off: the OPL is not synthesised (its registers are still written): for runs whose samples are
    // thrown away. The speaker and the PSG are not affected.
    void setOplSynthesis(bool on) { chips_.setOplSynthesis(on); }

private:
    // SoundTickSource: Timer_ISR's Sound_Tick (491b:0004), every PIT tick.
    void soundTick() override { seq_.timerTick(); }

    // SoundPorts
    void speakerDivisor(uint16_t divisor) override { chips_.speaker().setDivisor(divisor); }
    void speakerEnable(bool on) override { chips_.speaker().setEnabled(on); }
    void psgWrite(uint8_t value) override { chips_.psg().write(value); }
    void oplInit() override {
        if (opl_) opl_->init();
    }
    void oplPlayTune(int tune, int variant) override {
        if (opl_) opl_->playTune(tune, variant);
    }
    void oplNoteOn(int voice, uint8_t semitone) override {
        if (opl_) opl_->noteOn(voice, semitone);
    }
    void oplNoteOff(int voice) override {
        if (opl_) opl_->noteOff(voice);
    }
    int oplPlayTuneTicks() const override { return opl_ ? opl_->playTuneTicks() : 0; }

    std::unique_ptr<OplBackend> opl_;
    mischief::dos::ChipRenderer chips_;  // after the OPL back end (its output stage)
    Sequencer seq_;       // after the synths and the OPL back end: its constructor calls oplInit()
};

// ---- offline helpers (tools and tests)

struct OfflineRender {
    std::vector<double> samples;
    uint64_t ticks = 0;  // PIT ticks until the block stopped
};
// One effect (tune = false) or tune on a fresh renderer, from its start until its block stops, one
// block per PIT tick like tools/dos/dos_sound.py render(); the music loops `loops - 1` times.
// `afterTick` (optional) runs after each tick. Raw mode then appends dos_sound.py's 50 ms of
// silence, Smooth mode 200 ms of filter ring-out.
// `variant`: the AdLib orchestration of a tune.
OfflineRender renderEntry(DosSoundRenderer& r, bool tune, int id, int loops = 1, double maxSeconds = 600.0,
                          const std::function<void()>& afterTick = {}, int variant = 0);
// tools/dos/dos_sound.py write_wav: peak-normalised to 0.9 of full scale, rounded half to even.
std::vector<int16_t> toPcm16Normalized(std::span<const double> samples);
// A fixed gain on the synth levels, clipped.
std::vector<int16_t> toPcm16(std::span<const double> samples, double gain);

}  // namespace tmport::dos
