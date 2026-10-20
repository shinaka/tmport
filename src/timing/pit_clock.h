#pragma once
// The PC's programmable interval timer (8253) channel 0 as a time base, for the DOS games. Its
// input clock is 14.31818 MHz / 12 = 3579545 / 3 Hz exactly, so interrupt k of a channel programmed
// with divisor D comes at k * 3 * D / 3579545 s. Both games reprogram it and count its interrupts:
//
//   TM DOS (SST.EXE Timer_Install 1000:03C5): divisor 0x13B1 = 236.695 Hz; every interrupt runs the
//     sound driver, every 5th the game tick (dos/app/dos_runtime.h, which keeps its own loop).
//   G&G (ssg.exe Input_InstallHandlers 1454:0046, Timer_ISR 1454:0096; re/gg/docs/code_runtime.md
//     §1): divisor 0x2000 = 145.652 Hz; the u32 g_ticks DS:021a counts every interrupt and the sound
//     driver runs on every even value, after the increment (72.826 Hz).
//
// PitTimeBase: the exact rational time of interrupt k (no drift).
// VirtualPit:  the interrupt counter driven by host time or stepped, with callbacks on every n-th
//              counter value (the sound tick) and a writable counter (G&G's dialog time freeze
//              writes the tick it saved back, Dlg_RunModal 3f56:08c3).
// PitTickClock: a Clock whose unit is the PIT tick, for DeadlineTimers in G&G's unit.

#include <cstdint>
#include <functional>
#include <vector>

#include "timing/clock.h"

namespace tmport {

inline constexpr uint32_t kPitDivisorTmDos = 0x13B1;  // 5041: SST.EXE (236.695 Hz)
inline constexpr uint32_t kPitDivisorGg = 0x2000;     // 8192: ssg.exe (145.652 Hz)

class PitTimeBase {
public:
    static constexpr double kInputHz = 3579545.0 / 3.0;  // 1193181.67 Hz

    // Divisor as written to port 40h (1..0xA7C5; larger ones would overflow the exact arithmetic
    // and throw std::invalid_argument).
    explicit PitTimeBase(uint32_t divisor = kPitDivisorTmDos);

    uint32_t divisor() const { return divisor_; }
    double hz() const { return kInputHz / divisor_; }
    // Virtual time (ns) of interrupt k (k = 1 is the first interrupt after the PIT was programmed):
    // ceil(k * 3 * divisor * 1e9 / 3579545), exact for any k below 10^12.
    uint64_t timeOfTick(uint64_t k) const;
    // The number of interrupts whose time is <= tNs.
    uint64_t ticksAt(uint64_t tNs) const;

private:
    uint32_t divisor_;
    uint64_t num_, den_;  // one period = num_ / den_ ns, reduced
    uint64_t numQ_, numR_;  // num_ = numQ_ * den_ + numR_
};

class VirtualPit {
public:
    explicit VirtualPit(uint32_t divisor = kPitDivisorTmDos) : base_(divisor) {}

    const PitTimeBase& timeBase() const { return base_; }

    // `fn(counter)` runs on every interrupt whose new counter value c (ticks(), after the
    // increment) has c % every == phase; the callbacks of one interrupt run in the order they were
    // added. G&G's sound tick: onTick(2, soundTick, 0) (even values). Returns an id for remove().
    int onTick(uint32_t every, std::function<void(uint32_t counter)> fn, uint32_t phase = 0);
    void remove(int id);

    // Real time: host time moved on by hostDeltaNs; runs every interrupt that became due, at most
    // maxTicks of them (0 = no limit; the time of the others is dropped, keeping the sub-tick phase).
    // Returns the interrupts run.
    uint64_t advanceNs(uint64_t hostDeltaNs, uint64_t maxTicks = 0);
    // Deterministic: n more interrupts; or up to interrupt k (counted by interrupts(), not ticks()).
    void step(uint64_t n = 1);
    void runTo(uint64_t k);

    // Interrupts run so far (k of the time base).
    uint64_t interrupts() const { return run_; }
    // The game's u32 counter (g_ticks): the interrupts plus what setTicks() changed; wraps at 2^32.
    uint32_t ticks() const { return static_cast<uint32_t>(run_) + offset_; }
    // The game writes its counter (G&G: the dialog loop restores the tick of its entry).
    void setTicks(uint32_t value) { offset_ = value - static_cast<uint32_t>(run_); }
    // Virtual time now (ns).
    uint64_t nowNs() const { return base_.timeOfTick(run_) + phaseNs_; }
    uint64_t droppedTicks() const { return dropped_; }

private:
    struct Callback {
        int id;
        uint32_t every, phase;
        std::function<void(uint32_t)> fn;
    };
    void runInterrupt();

    PitTimeBase base_;
    std::vector<Callback> callbacks_;
    int nextId_ = 1;
    uint64_t run_ = 0;
    uint32_t offset_ = 0;
    uint64_t phaseNs_ = 0;
    uint64_t dropped_ = 0;
};

// A Clock in PIT ticks over a VirtualPit, for DeadlineTimers whose unit is the tick (G&G's
// Timer_Set counts in ticks, re/gg/docs/code_runtime.md §5): nowMs() is the counter ticks() and
// sleepMs(n) steps n interrupts (at least one; for deterministic runs: a real-time runtime advances
// the PIT from host time instead and never sleeps through this clock).
class PitTickClock final : public Clock {
public:
    explicit PitTickClock(VirtualPit& pit) : pit_(pit) {}
    uint64_t nowMs() const override { return pit_.ticks(); }
    void sleepMs(uint32_t ticks) override { pit_.step(ticks == 0 ? 1 : ticks); }

private:
    VirtualPit& pit_;
};

}  // namespace tmport
