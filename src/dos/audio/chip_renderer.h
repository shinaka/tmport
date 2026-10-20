#pragma once
// The sound hardware of a DOS PC as a sample source, ticked like the original's timer interrupt
// (both DOS games). A game's sound driver is the tick source: at the start of each tick's block of
// samples the renderer calls it, and it writes the chips through their ports: the PC speaker's PIT
// divisor and gate (speaker()), the SN76496 (psg()), an OPL2 (the OplOutput given; an OplChip, or a
// driver that owns one). Single threaded; ChipAudioDevice (chip_audio_device.h) adds the SDL stream
// and the queue of the game's calls.
//
//   TM DOS:  the sequencer (SST.EXE 491b) on every PIT interrupt, 1193182 / 5041 Hz (the defaults;
//            dos/audio/dos_sound_renderer.h)
//   G&G:     the BMF driver Snd_Tick (ssg.exe 1bb4:0db3) on every 2nd interrupt at divisor 0x2000,
//            1193181.67 / 16384 = 72.826 Hz (re/gg/docs/code_sound.md §5, §17)
//
// Clock: block lengths come from a running fractional accumulator (samples per tick = rate /
// tickHz, about 202.8 at 48 kHz for TM DOS), the arithmetic of tools/dos/dos_sound.py, so timing
// never depends on the video frame rate.
//
// Mix: the speaker and the PSG are added, then (Smooth mode) low-pass and DC-blocked; with an OPL the
// result is (speaker/PSG + OPL) * oplMixLevel.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "dos/audio/opl_output.h"
#include "dos/audio/psg_synth.h"
#include "dos/audio/speaker_synth.h"
#include "dos/audio/synth_common.h"

namespace tmport::dos {

struct ChipRenderConfig {
    int sampleRate = 48000;  // 44100 and 49716 (the OPL2's native rate) work as well
    SynthMode mode = SynthMode::Smooth;
    NoiseType noise = NoiseType::Ncr8496;  // Raw + Ti15 = tools/dos/dos_sound.py exactly
    double lowPassHz = 7000.0;             // Smooth mode: 2-pole low-pass (0 = off)
    double dcBlockHz = 12.0;               // Smooth mode: 1-pole high-pass (0 = off)
    // Driver ticks per second (TM DOS: every PIT interrupt at divisor 0x13B1; G&G:
    // PitTimeBase(kPitDivisorGg).hz() / 2).
    double tickHz = kPitTickHz;
    // The OPL's mix with the speaker and the PSG (TM DOS: the balance of the DOSBox-X AdLib recordings,
    // dos_sound_renderer.h kAdLibMixLevel).
    double oplMixLevel = 0.75;
};

// The game's sound driver, called once per tick (the original's timer interrupt).
class SoundTickSource {
public:
    virtual ~SoundTickSource() = default;
    virtual void soundTick() = 0;
};

class ChipRenderer {
public:
    // `opl` (optional, not owned) must outlive the renderer.
    explicit ChipRenderer(const ChipRenderConfig& cfg, OplOutput* opl = nullptr);

    const ChipRenderConfig& config() const { return cfg_; }
    double samplesPerTick() const { return samplesPerTick_; }

    // The driver (not owned; nullptr: ticks only count).
    void setTickSource(SoundTickSource* source) { source_ = source; }
    // Called right before each tick, before the driver: the moment commands from the game take
    // effect (between two interrupts, like the original's calls from the main loop).
    void setTickHook(std::function<void()> hook) { tickHook_ = std::move(hook); }

    // Writes n mono samples (synth levels: the speaker square is +-0.5, a PSG voice +-0.25; in Smooth
    // mode after the filters). Ticks happen at block boundaries inside the call.
    void render(double* out, size_t n);
    // Runs the next tick and appends exactly its block of samples to out (if a render() stopped
    // inside a block, that block's rest instead, without a tick). Returns the samples appended.
    size_t renderTick(std::vector<double>& out);

    uint64_t ticks() const { return ticks_; }
    // Samples left in the current tick's block (0 = the next render starts with a tick).
    size_t samplesToNextTick() const { return left_; }

    // The chips' ports.
    SpeakerSynth& speaker() { return speaker_; }
    const SpeakerSynth& speaker() const { return speaker_; }
    PsgSynth& psg() { return psg_; }
    const PsgSynth& psg() const { return psg_; }
    OplOutput* opl() const { return opl_; }
    // Off: the OPL is not synthesised (its registers are still written): for runs whose samples are
    // thrown away. The speaker and the PSG are not affected.
    void setOplSynthesis(bool on) { oplSynthesis_ = on; }

private:
    void beginTick();
    void synthesize(double* out, size_t n);
    void synthesizeOpl(double* out, size_t n);
    void filter(double* out, size_t n);

    ChipRenderConfig cfg_;
    SpeakerSynth speaker_;
    PsgSynth psg_;
    OplOutput* opl_;
    SoundTickSource* source_ = nullptr;
    std::function<void()> tickHook_;
    bool oplSynthesis_ = true;
    std::vector<double> oplBuf_;  // the OPL's samples of the current render call

    double samplesPerTick_;
    double acc_ = 0.0;
    size_t left_ = 0;
    uint64_t ticks_ = 0;

    // Smooth-mode filters (RBJ biquad low-pass, one-pole DC blocker).
    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
    double z1_ = 0, z2_ = 0;
    double dcR_ = 0, dcX_ = 0, dcY_ = 0;
};

}  // namespace tmport::dos
