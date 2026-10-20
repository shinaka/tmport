#pragma once
// The original's 8 deadline timers (segment 10a8, DS:4f62 u32[8]; re/docs/code_1098-10d8.md §2.2
// and §6). A slot holds an absolute clock value; all compares are unsigned "now >= deadline".
// Slots in use: 0 caret blink (333 ms), 4 title page timeout (60 s) / rank band step (50 ms),
// 6 waited on by the text renderer but never armed (always expired), 7 frame timer (100 ms room
// tick, 1000/fps in Anim_PlayModal). A never-armed slot holds 0 and is expired.
//
// The same module with the same 8 slots is in Gizmos & Gadgets (ssg.exe 152c, DS:6578 u32[8];
// re/gg/docs/code_runtime.md §5), with two differences, both parameters here:
//   * the unit is the PIT tick, not the millisecond: the timers count in the unit of their Clock
//     (TM: the millisecond clock; G&G: timing/pit_clock.h PitTickClock), and G&G's Timer_Set reads a
//     u16 count (the caller passes static_cast<uint16_t>(n));
//   * Timer_Expired 152c:004a is true only when now > deadline (ExpiryRule::AfterDeadline), while
//     its Timer_Wait 152c:0029 and Delay 152c:0072 end at now >= deadline (reached()).
//
// These are pure clock queries. The waits that pump events and present (Timer_Wait, Delay_ms)
// live on Engine (Engine::timerWait / delayMs / timerExpired).

#include <array>
#include <cstdint>

#include "timing/clock.h"

namespace tmport {

// Timer_Expired's compare: TM Windows 10a8:0066 now >= deadline; G&G 152c:004a now > deadline.
enum class ExpiryRule : uint8_t { AtDeadline, AfterDeadline };

class DeadlineTimers {
public:
    static constexpr int kSlots = 8;
    static constexpr int kCaretSlot = 0;
    static constexpr int kPageSlot = 4;
    static constexpr int kTextSlot = 6;
    static constexpr int kFrameSlot = 7;  // 100 ms room tick, 1000/fps in Anim_PlayModal

    explicit DeadlineTimers(Clock* clock = nullptr, ExpiryRule rule = ExpiryRule::AtDeadline)
        : clock_(clock ? clock : &Clock::system()), rule_(rule) {}

    ExpiryRule rule() const { return rule_; }

    // System milliseconds (SDL_GetTicks); kept for milestone-1 callers.
    static uint64_t now();
    // Now in the unit of this timer set's clock (TM: milliseconds; G&G: PIT ticks).
    uint64_t clockNow() const { return clock_->nowMs(); }
    Clock& clock() const { return *clock_; }
    void setClock(Clock* clock) { clock_ = clock ? clock : &Clock::system(); }

    // 10a8:002a Timer_Set(slot, ms): deadline = now + ms (ms is a signed 32-bit value).
    void set(int slot, int32_t ms) {
        const int64_t d = static_cast<int64_t>(clockNow()) + ms;
        deadline_[index(slot)] = d < 0 ? 0 : static_cast<uint64_t>(d);
    }
    // 10a8:0066 Timer_Expired(slot): now >= deadline (G&G 152c:004a with AfterDeadline: now > deadline).
    bool expired(int slot) const {
        const uint64_t t = clockNow(), d = deadline_[index(slot)];
        return rule_ == ExpiryRule::AtDeadline ? t >= d : t > d;
    }
    // The end of a wait on the slot, in both games: now >= deadline (G&G Timer_Wait 152c:0029 and
    // Delay 152c:0072; TM's waits end when expired(), the same compare).
    bool reached(int slot) const { return clockNow() >= deadline_[index(slot)]; }
    // Clock units until the deadline (0 when reached).
    uint64_t remaining(int slot) const {
        const uint64_t t = clockNow(), d = deadline_[index(slot)];
        return d > t ? d - t : 0;
    }
    uint64_t deadline(int slot) const { return deadline_[index(slot)]; }
    // Back to the start-up state (all slots 0 = expired).
    void reset() { deadline_.fill(0); }

private:
    static size_t index(int slot) { return static_cast<size_t>(slot & (kSlots - 1)); }

    Clock* clock_;
    ExpiryRule rule_;
    std::array<uint64_t, kSlots> deadline_{};
};

}  // namespace tmport
