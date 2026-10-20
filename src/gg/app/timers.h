#pragma once
// The deadline timers of Gizmos & Gadgets (ssg.exe 152c; re/gg/docs/code_runtime.md §5): eight u32
// deadlines g_timers DS:6578 in PIT ticks, on the shared core's DeadlineTimers (timing/
// deadline_timers.h) with G&G's two differences: the unit is the PIT tick (GgClock::tickClock) and
// Timer_Expired is strict (ExpiryRule::AfterDeadline).
//
//   Timer_Set(slot, n)   152c:0009  deadline = g_ticks + n, n a u16 (only the low word is read)
//   Timer_Wait(slot)     152c:0029  waits until g_ticks >= deadline          -> reached()
//   Timer_Expired(slot)  152c:004a  g_ticks > deadline (strict)              -> expired()
//   Delay(n)             152c:0072  end = g_ticks + n; while g_ticks < end: Input_GetCode()
//
// So a Timer_Set(n) followed by an Expired loop lasts n + 1 ticks, one more than a Timer_Wait. The
// waits themselves pump the host and let time pass: GgRuntime::timerWait / timerExpired / delay.
// Slots in use: 0 the frame pacer, 1 attract pages and title phases, 2 the wipe, 6 the text viewer
// and the launch puzzle, 7 puzzle animations (3, 4, 5 unused).
//
// Deviation: the core keeps 64-bit deadlines, so a deadline that would wrap past 2^32 ticks (after
// 341 days of play) is not reached at the wrap as in the original.

#include <cstdint>

#include "gg/app/gg_clock.h"
#include "timing/deadline_timers.h"

namespace tmport::gg {

class GgTimers {
public:
    static constexpr int kSlots = DeadlineTimers::kSlots;
    static constexpr int kFrameSlot = 0;   // the frame pacer: Timer_Set(0, 4) ... Timer_Wait(0)
    static constexpr int kAttractSlot = 1;
    static constexpr int kWipeSlot = 2;
    static constexpr int kTextViewSlot = 6;
    static constexpr int kAnimSlot = 7;

    explicit GgTimers(GgClock& clock) : timers_(&clock.tickClock(), ExpiryRule::AfterDeadline) {}

    // 152c:0009 Timer_Set: deadline = g_ticks + n (u16).
    void set(int slot, uint16_t n) { timers_.set(slot, static_cast<int32_t>(n)); }
    // The end test of Timer_Wait 152c:0029 and of a wait on the slot: g_ticks >= deadline.
    bool reached(int slot) const { return timers_.reached(slot); }
    // 152c:004a Timer_Expired: g_ticks > deadline.
    bool expired(int slot) const { return timers_.expired(slot); }
    uint64_t deadline(int slot) const { return timers_.deadline(slot); }
    // Ticks until the slot is reached (0 when reached).
    uint64_t remaining(int slot) const { return timers_.remaining(slot); }
    // Start-up state: every slot 0 (BSS), i.e. reached.
    void reset() { timers_.reset(); }

    DeadlineTimers& core() { return timers_; }

private:
    DeadlineTimers timers_;
};

}  // namespace tmport::gg
