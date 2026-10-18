// Deadline timers (timing/) and the engine's wait primitives, on a manual clock. Headless: the
// engine never opens a window; events are injected with Engine::postEvent.
#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include "platform/engine.h"
#include "test_data.h"
#include "timing/clock.h"
#include "timing/deadline_timers.h"

using namespace tmport;

namespace {

SDL_Event keyDown(SDL_Keycode key) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = key;
    e.key.down = true;
    return e;
}
SDL_Event textInput(const char* s) {
    SDL_Event e{};
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = s;
    return e;
}

}  // namespace

TEST_CASE("deadline timers: set / expired / remaining on a manual clock") {
    ManualClock clock(5000);
    DeadlineTimers t(&clock);
    for (int s = 0; s < DeadlineTimers::kSlots; ++s) CHECK(t.expired(s));  // never armed = expired
    CHECK(t.expired(DeadlineTimers::kTextSlot));
    t.set(DeadlineTimers::kFrameSlot, 100);
    CHECK_FALSE(t.expired(7));
    CHECK(t.remaining(7) == 100);
    clock.advance(99);
    CHECK_FALSE(t.expired(7));
    CHECK(t.remaining(7) == 1);
    clock.advance(1);
    CHECK(t.expired(7));  // now >= deadline
    CHECK(t.remaining(7) == 0);
    clock.advance(1000);
    CHECK(t.expired(7));  // stays expired; no catch-up, no wrap
    // Re-arming is relative to now; slots are independent; the index wraps like the 8-entry table.
    t.set(7, 100);
    t.set(0, 333);
    clock.advance(150);
    CHECK(t.expired(7));
    CHECK_FALSE(t.expired(0));
    t.set(12, 10);  // slot 12 & 7 = 4
    CHECK_FALSE(t.expired(4));
    t.set(4, -50);  // a negative time is already expired
    CHECK(t.expired(4));
    t.reset();
    CHECK(t.expired(0));
}

TEST_CASE("engine waits: delayMs pumps but delivers input only to the next fetch") {
    TM_REQUIRE_GAME_DATA();
    ManualClock clock(10000);
    EngineConfig cfg;
    cfg.clock = &clock;
    Engine engine(*g, gfx::ColorMode::Colour256, cfg);
    CHECK_FALSE(engine.hasWindow());

    // Keys "pressed" during the delay (posted from inside the waits' sleeps).
    int sleeps = 0;
    clock.onSleep = [&](uint64_t) {
        if (++sleeps == 3) {
            engine.postEvent(keyDown(SDLK_UP));
            std::string tmp = "x";  // postEvent copies the text
            engine.postEvent(textInput(tmp.c_str()));
        }
    };
    const uint64_t t0 = clock.nowMs();
    engine.delayMs(62);
    CHECK(clock.nowMs() == t0 + 62);  // exact length, no overshoot
    CHECK(sleeps >= 3);
    // Pumped (posted to the message queue) but not dispatched: the held flag is not set yet.
    CHECK(engine.input().pendingMessages() == 2);
    CHECK_FALSE(engine.input().upHeld);
    clock.onSleep = nullptr;
    CHECK(engine.getInputCode() == input::kUp);
    CHECK(engine.input().upHeld);
    CHECK(engine.getInputCode() == 'x');
    CHECK(engine.getInputCode() == 0);

    // delayMs(0) returns at once.
    const uint64_t t1 = clock.nowMs();
    engine.delayMs(0);
    CHECK(clock.nowMs() == t1);

    // Timer_Wait waits until the deadline; Timer_Expired yields at most 1 ms per call.
    engine.timerSet(7, 100);
    const uint64_t t2 = clock.nowMs();
    engine.timerWait(7);
    CHECK(clock.nowMs() == t2 + 100);
    engine.timerSet(7, 5);
    int polls = 0;
    while (!engine.timerExpired(7)) ++polls;
    CHECK(polls == 4);  // 1 ms per poll
    CHECK(engine.timers().expired(7));

    // Input_WaitCode returns the first non-zero code.
    clock.onSleep = [&](uint64_t) { engine.postKey(0x4AE); };
    CHECK(engine.waitInputCode() == 0x4AE);
    clock.onSleep = nullptr;

    // Input_Flush via the engine discards what was posted.
    engine.postEvent(textInput("abc"));
    engine.flushInput();
    CHECK(engine.getInputCode() == 0);
    CHECK(engine.input().empty());
}

TEST_CASE("engine: input mode and close requests headless") {
    TM_REQUIRE_GAME_DATA();
    ManualClock clock;
    EngineConfig cfg;
    cfg.clock = &clock;
    cfg.inputMode = InputMode::FirstEventPerDrain;
    Engine engine(*g, gfx::ColorMode::Colour256, cfg);
    CHECK(engine.input().mode() == InputMode::FirstEventPerDrain);
    engine.postEvent(textInput("AB"));
    CHECK(engine.getInputCode() == 'A');
    CHECK(engine.getInputCode() == 0);  // B lost, as in the original

    SDL_Event close{};
    close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    engine.postEvent(close);
    CHECK(engine.getInputCode() == input::kEsc);
    CHECK(engine.quitRequested());
    CHECK_FALSE(engine.paused());  // headless never pauses
    CHECK(engine.appActive());
    // The default cursor set was decoded from the EXE.
    CHECK(engine.cursors().loadedCount() == 3);
    // Engine::hotspots() is the table the input fetch hit-tests against.
    engine.hotspots().set(0x0D, 0, 0, 512, 384);
    SDL_Event b{};
    b.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    b.button.button = SDL_BUTTON_LEFT;
    b.button.x = 100.0f;
    b.button.y = 100.0f;
    engine.postEvent(b);
    CHECK(engine.getInputCode() == 0x800D);
}
