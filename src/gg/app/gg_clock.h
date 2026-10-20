#pragma once
// The time base of Gizmos & Gadgets (ssg.exe 1454 Input_InstallHandlers / Timer_ISR; re/gg/docs/
// code_runtime.md §1, §13.2, §20.1-3, PORTING_NOTES.md §2):
//
//   PIT channel 0, divisor 0x2000: 1193181.67 Hz / 8192 = 145.6521 Hz (6.8657 ms per tick), on the
//   shared core's exact rational time base (timing/pit_clock.h: no drift over any run length).
//
//   Timer_ISR 1454:0096, per interrupt, in this order:
//     1. the BIOS chain: DS:0216 += 1; when the result is >= 0 (it is 0 in the file and only ever
//        reset to 0, so always) DS:0216 = 0 and the old INT 8 runs: the BIOS clock goes 8 times too
//        fast. Kept as a counter (biosTicks()); nothing in the game reads it (the seed is taken once,
//        right after the handlers are installed).
//     2. g_ticks DS:021a (u32) += 1, g_ticks16 DS:021e (u16, no reader) += 1;
//     3. the sound tick Snd_Tick 1bb4:0db3 when the new g_ticks is even (72.826 Hz);
//     4. EOI (the second, double EOI is dropped).
//   The deadline timers (152c) read g_ticks; the dialog loop writes it back (setTicks: time stands
//   still in dialog boxes, and the sound-tick parity follows the written value, as in the original).
//
//   The VGA's vertical retrace (mode 13h: 25.175 MHz / (800 x 449) = 70.0863 Hz) on the same
//   virtual time base: retrace k at retracePhase + k * 14.268 ms. Pal_SetRange 1399:05d6 waits for
//   the start of the next one (twice per palette load).
//
// The clock holds the virtual time (ns since the PIT was programmed); GgRuntime moves it (from the
// host clock in real time, by the waits in deterministic runs) and runs the interrupts that fall due.

#include <cstdint>
#include <functional>

#include "timing/pit_clock.h"

namespace tmport::gg {

class GgClock {
public:
    static constexpr uint32_t kDivisor = kPitDivisorGg;  // 0x2000 (1454:004a)
    // Retrace period = 800 * 449 / 25.175 MHz = 14368000000 / 1007 ns.
    static constexpr uint64_t kRetraceNum = 14'368'000'000ull;
    static constexpr uint64_t kRetraceDen = 1007ull;

    GgClock();
    GgClock(const GgClock&) = delete;
    GgClock& operator=(const GgClock&) = delete;

    const PitTimeBase& timeBase() const { return pit_.timeBase(); }
    VirtualPit& pit() { return pit_; }
    // A Clock in PIT ticks (g_ticks) for the core DeadlineTimers.
    PitTickClock& tickClock() { return tickClock_; }

    // ---- the interrupt
    // Timer_ISR 1454:0096 once: the virtual time moves to the interrupt's time.
    void tick();
    // Interrupts so far (the PIT's own count; script ticks and run limits count these).
    uint64_t interrupts() const { return pit_.interrupts(); }
    // g_ticks DS:021a (wraps at 2^32).
    uint32_t ticks() const { return pit_.ticks(); }
    // The dialog loop's write-back (Dlg_RunModal 3f56:08c3): g_ticks reads `value` from now on.
    void setTicks(uint32_t value) { pit_.setTicks(value); }
    // g_ticks16 DS:021e (no reader in the game).
    uint16_t ticks16() const { return ticks16_; }
    // Old INT 8 calls made by the BIOS chain (every interrupt: the 8x BIOS clock).
    uint64_t biosTicks() const { return biosTicks_; }

    // Snd_Tick: runs after the increment on every interrupt whose new g_ticks is even, with that
    // g_ticks (the sound driver installs it; an empty function removes it).
    void setSoundTick(std::function<void(uint32_t ticks)> fn);
    uint64_t soundTicks() const { return soundTicks_; }

    // ---- virtual time (ns since the PIT was programmed)
    uint64_t nowNs() const { return nowNs_; }
    // Moves the time within the current tick interval: at least the time of the last interrupt and
    // before the next one (the runtime runs the interrupts in between with tick()).
    void setNowNs(uint64_t t);
    uint64_t timeOfTick(uint64_t k) const { return pit_.timeBase().timeOfTick(k); }
    uint64_t nextTickNs() const { return timeOfTick(interrupts() + 1); }
    uint64_t tickPeriodNs() const { return timeOfTick(1); }

    // ---- the vertical retrace
    void setRetracePhaseNs(uint64_t phase) { retracePhase_ = phase; }
    uint64_t retracePhaseNs() const { return retracePhase_; }
    // Time of retrace k (k >= 0): phase + ceil(k * 14368000000 / 1007).
    uint64_t retraceTimeNs(uint64_t k) const;
    // The start of the first retrace strictly after t.
    uint64_t nextRetraceAfter(uint64_t t) const;

private:
    VirtualPit pit_;
    PitTickClock tickClock_;
    uint64_t nowNs_ = 0;
    int16_t biosCounter_ = 0;  // DS:0216
    uint64_t biosTicks_ = 0;
    uint16_t ticks16_ = 0;
    int soundId_ = 0;
    uint64_t soundTicks_ = 0;
    std::function<void(uint32_t)> sound_;
    uint64_t retracePhase_ = 0;
};

}  // namespace tmport::gg
