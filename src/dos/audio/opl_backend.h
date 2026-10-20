#pragma once
// The AdLib music path (fmt_sound.md §5.5): the interface the sequencer (491b) drives on the AdLib
// device, and AdLibOpl, the original's AdLib driver (segment 4ac9, adlib_driver.h) writing to an
// emulated OPL2 (opl_chip.h).
//
// With the AdLib device the original plays the effects (SFX.IBM) on the PC speaker and the tunes
// (TUNES.TAN, voices 0-2; voice 3 is ignored) on the OPL2, and the music goes on under effects.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

#include "dos/audio/adlib_driver.h"
#include "dos/audio/opl_chip.h"
#include "dos/audio/opl_output.h"

namespace tmport::dos {

struct SoundTables;

// The OPL output stage (render, skip: dos/audio/opl_output.h) with the driver calls of the sequencer.
class OplBackend : public OplOutput {
public:
    virtual void init() {}                                  // 4ac9:00b7 AdLib_Init
    virtual void playTune(int tune, int variant) = 0;       // 4ac9:0002 AdLib_PlayTune (variant = rand() & 3)
    virtual void noteOn(int voice, uint8_t semitone) = 0;   // 4ac9:029e AdLib_NoteOn (semitone = note - 1 + 12 * oct)
    virtual void noteOff(int voice) = 0;                    // 4ac9:0432 AdLib_NoteOff
    // A DOSBox-X state's AdLib driver from its DGROUP image (DS:0000..); false if the back end has none.
    virtual bool restore(std::span<const uint8_t> /*dgroup*/) { return false; }
    // PIT ticks the last playTune() keeps the music block from starting (the AdLib driver's set-up on
    // the reference machine, see AdLibOpl).
    virtual int playTuneTicks() const { return 0; }
};

// The original's AdLib driver on an emulated OPL2.
//
// Set-up time: Music_Play (491b:0791) clears the music block, runs AdLib_PlayTune and only then
// sets the block's status to 1, so the timer interrupts that come while the instruments are set up
// (15 register writes per used channel) play no music. On the reference machine AdLib_PlayTune took
// 76 us per register write (the 14 set-ups of the .dro capture, fmt_sound.md §11.7): 6.9 ms for 6
// channels, 10.3 ms for 9; a call came about 0.2 PIT tick after its interrupt, so the interrupts held
// back are ceil(duration / tick + 0.2) - 1: 1 for 3 to 6 used channels, 2 for 7 to 9 (all 14 tune
// starts of the capture; the orchestrations use 6 to 9). playTuneTicks() reports it to the sequencer,
// which holds the tune back as long.
class AdLibOpl final : public OplBackend, private OplWriter {
public:
    static constexpr double kPlayTuneWriteSeconds = 76.3e-6;  // AdLib_PlayTune per register write
    static constexpr double kCallPhase = 0.2;                  // PIT ticks from the interrupt to the call
    // Timer interrupts a set-up of `writes` register writes holds the tune back.
    static int playTuneHold(size_t writes);

    // `tables` (SST.EXE's DGROUP) must outlive the back end.
    AdLibOpl(const SoundTables& tables, double sampleRate);

    void init() override { driver_.init(); }
    void playTune(int tune, int variant) override;
    void noteOn(int voice, uint8_t semitone) override { driver_.noteOn(voice, semitone); }
    void noteOff(int voice) override { driver_.noteOff(voice); }
    void render(double* out, size_t n) override { chip_.render(out, n); }
    void skip(size_t n) override { chip_.skip(n); }
    bool restore(std::span<const uint8_t> dgroup) override;
    int playTuneTicks() const override { return hold_; }

    // Every register write, in the driver's order (tools: the --opl-log of --dos-sound, tests).
    void setWriteObserver(std::function<void(uint8_t reg, uint8_t value)> f) { observer_ = std::move(f); }

private:
    void oplWrite(uint8_t reg, uint8_t value) override;

    OplChip chip_;
    AdLibDriver driver_;
    std::function<void(uint8_t, uint8_t)> observer_;
    size_t writes_ = 0;  // register writes so far
    int hold_ = 0;       // of the last playTune()
};

}  // namespace tmport::dos
