#include "timing/deadline_timers.h"

#include <SDL3/SDL_timer.h>

namespace tmport {

namespace {

class SdlClock final : public Clock {
public:
    uint64_t nowMs() const override { return SDL_GetTicks(); }
    void sleepMs(uint32_t ms) override { SDL_Delay(ms); }
};

}  // namespace

Clock& Clock::system() {
    static SdlClock clock;
    return clock;
}

uint64_t DeadlineTimers::now() { return SDL_GetTicks(); }

}  // namespace tmport
