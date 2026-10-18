#pragma once
// The original's 8 deadline timers (segment 10a8, DS:4f62 u32[8]; re/docs/code_1098-10d8.md §2.2
// and §6). A slot holds an absolute clock value; all compares are unsigned "now >= deadline".
// Slots in use: 0 caret blink (333 ms), 4 title page timeout (60 s) / rank band step (50 ms),
// 6 waited on by the text renderer but never armed (always expired), 7 frame timer (100 ms room
// tick, 1000/fps in Anim_PlayModal). A never-armed slot holds 0 and is expired.
//
// These are pure clock queries. The waits that pump events and present (Timer_Wait, Delay_ms)
// live on Engine (Engine::timerWait / delayMs / timerExpired).

#include <array>
#include <cstdint>

#include "timing/clock.h"

namespace tmport {

class DeadlineTimers {
public:
    static constexpr int kSlots = 8;
    static constexpr int kCaretSlot = 0;
    static constexpr int kPageSlot = 4;
    static constexpr int kTextSlot = 6;
    static constexpr int kFrameSlot = 7;  // 100 ms room tick, 1000/fps in Anim_PlayModal

    explicit DeadlineTimers(Clock* clock = nullptr) : clock_(clock ? clock : &Clock::system()) {}

    // System milliseconds (SDL_GetTicks); kept for milestone-1 callers.
    static uint64_t now();
    // Milliseconds of this timer set's clock.
    uint64_t clockNow() const { return clock_->nowMs(); }
    Clock& clock() const { return *clock_; }
    void setClock(Clock* clock) { clock_ = clock ? clock : &Clock::system(); }

    // 10a8:002a Timer_Set(slot, ms): deadline = now + ms (ms is a signed 32-bit value).
    void set(int slot, int32_t ms) {
        const int64_t d = static_cast<int64_t>(clockNow()) + ms;
        deadline_[index(slot)] = d < 0 ? 0 : static_cast<uint64_t>(d);
    }
    // 10a8:0066 Timer_Expired(slot): now >= deadline.
    bool expired(int slot) const { return clockNow() >= deadline_[index(slot)]; }
    // Milliseconds until the deadline (0 when expired).
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
    std::array<uint64_t, kSlots> deadline_{};
};

}  // namespace tmport
