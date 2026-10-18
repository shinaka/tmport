#pragma once
// Millisecond clock used by the deadline timers and the engine's wait primitives. The original
// reads timeGetTime() (1 ms, timeBeginPeriod(16)); the engine uses SDL_GetTicks. Tests install a
// ManualClock to make waits deterministic.

#include <cstdint>
#include <functional>

namespace tmport {

class Clock {
public:
    virtual ~Clock() = default;
    // Milliseconds since an arbitrary start.
    virtual uint64_t nowMs() const = 0;
    // Gives the CPU away for about ms milliseconds (waits use it between polls).
    virtual void sleepMs(uint32_t ms) = 0;

    // The process-wide SDL clock (SDL_GetTicks / SDL_Delay).
    static Clock& system();
};

// A clock that only moves when told to: sleepMs() advances it and then calls onSleep (tests use
// it to inject input "during" a wait).
class ManualClock final : public Clock {
public:
    explicit ManualClock(uint64_t start = 1000) : now_(start) {}
    uint64_t nowMs() const override { return now_; }
    void sleepMs(uint32_t ms) override {
        now_ += ms == 0 ? 1 : ms;
        ++sleeps_;
        if (onSleep) onSleep(now_);
    }
    void advance(uint64_t ms) { now_ += ms; }
    void set(uint64_t t) { now_ = t; }
    uint64_t sleeps() const { return sleeps_; }

    std::function<void(uint64_t now)> onSleep;

private:
    uint64_t now_;
    uint64_t sleeps_ = 0;
};

}  // namespace tmport
