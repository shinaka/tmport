// The G&G runtime: the virtual PIT and Timer_ISR (1454:0096), the deadline timers (152c) and every
// wait and poll of GgRuntime in deterministic and real time, scripted input in PIT ticks, the run
// limit, host events, Seq_Run and the exit paths (re/gg/docs/code_runtime.md §1-§5, §20,
// PORTING_NOTES.md §2). Data-free; no window.

#include <doctest/doctest.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>

#include <string>
#include <vector>

#include "gg/app/gg_runtime.h"
#include "gg/app/seq_run.h"
#include "platform/host.h"
#include "timing/clock.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

dos::ScriptedInput script(const char* text) {
    std::string error;
    auto s = dos::ScriptedInput::parse(text, error);
    INFO(error);
    REQUIRE(s.has_value());
    return std::move(*s);
}

}  // namespace

// ------------------------------------------------------------------------------------ the clock

TEST_CASE("gg clock: 145.652 Hz exactly, no drift over a long run") {
    GgClock c;
    CHECK(c.timeBase().divisor() == 0x2000);
    CHECK(c.timeBase().hz() == doctest::Approx(145.65205891927084));
    // ceil(k * 3 * 8192 * 1e9 / 3579545) ns, from exact rational arithmetic.
    CHECK(c.timeOfTick(1) == 6'865'678u);
    CHECK(c.timeOfTick(4) == 27'462'709u);
    CHECK(c.timeOfTick(146) == 1'002'388'852u);
    CHECK(c.timeOfTick(145'652) == 999'999'595'480u);
    CHECK(c.timeOfTick(1'000'000'000) == 6'865'677'062'308'199u);
    // A million interrupts: g_ticks, the time and the sound ticks agree with the formula.
    uint64_t sound = 0;
    c.setSoundTick([&sound](uint32_t t) {
        CHECK(t % 2 == 0);
        ++sound;
    });
    for (int i = 0; i < 1'000'000; ++i) c.tick();
    CHECK(c.ticks() == 1'000'000u);
    CHECK(c.nowNs() == 6'865'677'062'309u);
    CHECK(sound == 500'000u);
    CHECK(c.soundTicks() == 500'000u);
    CHECK(c.ticks16() == static_cast<uint16_t>(1'000'000));
    // The BIOS chain ran on every interrupt (the 8x BIOS clock).
    CHECK(c.biosTicks() == 1'000'000u);
}

TEST_CASE("gg clock: the sound tick follows g_ticks' parity after the dialog write-back") {
    GgClock c;
    std::vector<uint32_t> at;
    c.setSoundTick([&at](uint32_t t) { at.push_back(t); });
    for (int i = 0; i < 5; ++i) c.tick();
    CHECK(at == std::vector<uint32_t>{2, 4});
    c.setTicks(10);  // Dlg_RunModal writes back an even tick: the next interrupt makes 11
    c.tick();
    c.tick();
    CHECK(at == std::vector<uint32_t>{2, 4, 12});
    CHECK(c.interrupts() == 7u);
    c.setSoundTick({});
    c.tick();
    c.tick();
    CHECK(at.size() == 3);
}

TEST_CASE("gg clock: the 70.086 Hz retrace on the PIT's time base") {
    GgClock c;
    CHECK(c.retraceTimeNs(1) == 14'268'124u);
    CHECK(c.retraceTimeNs(2) == 28'536'247u);
    CHECK(c.retraceTimeNs(1007) == 14'368'000'000u);
    CHECK(c.nextRetraceAfter(0) == 14'268'124u);
    CHECK(c.nextRetraceAfter(14'268'123) == 14'268'124u);
    CHECK(c.nextRetraceAfter(14'268'124) == 28'536'247u);  // strictly after
    c.setRetracePhaseNs(1000);
    CHECK(c.nextRetraceAfter(0) == 1000u);
    CHECK(c.nextRetraceAfter(1000) == 14'269'124u);
}

// ----------------------------------------------------------------------------------- the timers

TEST_CASE("gg timers: Timer_Wait ends at >=, Timer_Expired needs >, u16 counts") {
    GgClock c;
    GgTimers t(c);
    t.set(0, 4);
    for (int i = 0; i < 4; ++i) {
        CHECK_FALSE(t.reached(0));
        c.tick();
    }
    CHECK(t.reached(0));
    CHECK_FALSE(t.expired(0));
    c.tick();
    CHECK(t.expired(0));
    // A never-set slot (0) is reached and, after the first tick, expired.
    CHECK(t.reached(5));
    CHECK(t.expired(5));
    // The count is a u16.
    t.set(1, static_cast<uint16_t>(0x10005));
    CHECK(t.deadline(1) == c.ticks() + 5u);
    CHECK(t.remaining(1) == 5u);
}

// -------------------------------------------------------------------- the waits, deterministic

TEST_CASE("gg runtime: a frame loop is exactly 4 ticks per frame") {
    GgRuntime rt;
    std::vector<uint32_t> starts;
    for (int f = 0; f < 100; ++f) {
        starts.push_back(rt.ticks());
        rt.timers().set(0, 4);
        rt.timerWait(0);
        (void)rt.getInputCode();
        (void)rt.getInputCode();  // a second poll costs 1/16 tick, not a frame edge
    }
    CHECK(rt.ticks() == 400u);
    for (size_t i = 0; i < starts.size(); ++i) CHECK(starts[i] == 4 * i);
    // Races: 8 ticks.
    rt.timers().set(0, 8);
    rt.timerWait(0);
    CHECK(rt.ticks() == 408u);
    CHECK(rt.nowNs() == rt.clock().timeOfTick(408));
}

TEST_CASE("gg runtime: Timer_Set(n) and an Expired loop last n + 1 ticks") {
    GgRuntime rt;
    rt.timers().set(1, 10);
    int polls = 0;
    while (!rt.timerExpired(1)) ++polls;
    CHECK(rt.ticks() == 11u);
    CHECK(polls == 11 * 16);  // the free first poll, then 16 polls per tick
}

TEST_CASE("gg runtime: Delay(n) polls Input_GetCode for n ticks; Delay(0) returns at once") {
    GgRuntime rt;
    rt.installHandlers();
    int sound = 0;
    rt.input().hooks().toggleSound = [&sound] { ++sound; };
    // Ctrl+S typed during the delay acts; an 'a' typed during it is lost.
    rt.setScript(script("3:down:lctrl, 4:tap:s, 6:up:lctrl, 8:tap:a"));
    rt.delay(20);
    CHECK(rt.ticks() == 20u);
    CHECK(sound == 1);
    CHECK(rt.keyboard().bios().size() == 0);
    CHECK(rt.keyboard().ring().empty());
    const uint64_t polls = rt.polls();
    rt.delay(0);
    CHECK(rt.polls() == polls);
    CHECK(rt.ticks() == 20u);
}

TEST_CASE("gg runtime: unpaced polls move on 1/16 tick at a time, landing on tick edges") {
    GgRuntime rt;
    (void)rt.getInputCode();  // free
    for (int i = 0; i < 16; ++i) (void)rt.getInputCode();
    CHECK(rt.ticks() == 1u);
    CHECK(rt.nowNs() == rt.clock().timeOfTick(1));
    // idle() is a poll too (busy-waits on sound).
    for (int i = 0; i < 32; ++i) rt.idle();
    CHECK(rt.ticks() == 3u);
    // A wait that ends at once counts as a poll, so such a loop still moves on.
    rt.timers().set(2, 0);
    for (int i = 0; i < 16; ++i) rt.timerWait(2);
    CHECK(rt.ticks() == 4u);
}

TEST_CASE("gg runtime: Input_WaitCode waits for a scripted key") {
    GgRuntime rt;
    rt.installHandlers();
    rt.setScript(script("50:tap:enter"));
    CHECK(rt.waitInputCode() == 0x0D);
    CHECK(rt.ticks() == 50u);  // the key arrived right before interrupt 50
}

TEST_CASE("gg runtime: a palette load waits for two retraces while the PIT runs") {
    GgRuntime rt;
    rt.waitRetrace();
    CHECK(rt.nowNs() == 14'268'124u);
    CHECK(rt.ticks() == 2u);  // 2 * 6.866 ms <= 14.27 ms
    rt.waitRetrace();
    CHECK(rt.nowNs() == 28'536'247u);
    CHECK(rt.ticks() == 4u);
    // A fade-out: 12 loads = 24 retraces.
    for (int i = 0; i < 22; ++i) rt.waitRetrace();
    CHECK(rt.nowNs() == rt.clock().retraceTimeNs(24));
    CHECK(rt.ticks() == 49u);
}

// ----------------------------------------------------------------- scripted input per tick

TEST_CASE("gg runtime: scripted keys give the held bits and the ring per tick") {
    GgRuntime rt;
    rt.installHandlers();
    rt.setScript(script("10:down:left, 20:up:left, 30:tap:space:2, 40:down:lctrl, 41:up:lctrl"));
    std::vector<uint16_t> heldAt;
    for (int i = 0; i < 45; ++i) {
        rt.advanceTicks(1);
        heldAt.push_back(rt.keyboard().heldKeys());
    }
    // heldAt[i] is after interrupt i + 1; an event of tick n comes right before interrupt n.
    CHECK(heldAt[8] == 0);
    CHECK(heldAt[9] == held::kLeft);
    CHECK(heldAt[18] == held::kLeft);
    CHECK(heldAt[19] == 0);
    CHECK(heldAt[29] == held::kSpace);
    CHECK(heldAt[30] == held::kSpace);
    CHECK(heldAt[31] == 0);
    CHECK(heldAt[39] == held::kCtrl);
    CHECK(heldAt[40] == 0);
    // Outside play mode the makes reach the ring, stamped with the g_ticks before their interrupt.
    const KeyRing& ring = rt.keyboard().ring();
    REQUIRE(ring.pending() == 3);
    CHECK(ring.record(0).tick == 9u);
    CHECK(ring.record(1).tick == 29u);
    CHECK(ring.record(2).tick == 39u);
    CHECK(rt.keyboard().biosGetKey() == 0x14B);
    CHECK(rt.keyboard().biosGetKey() == 0x20);
}

TEST_CASE("gg runtime: in play mode a held arrow repeats without keys; a fraction tick time") {
    GgRuntime rt;
    rt.installHandlers();
    rt.keyboard().setPlayMode(1);
    // Down 10.4 ms after interrupt 100: typematic from there (500 ms = 72.8 ticks).
    rt.setScript(script("100.5:down:right, 300:up:right"));
    rt.advanceTicks(101);
    CHECK(rt.keyboard().heldKeys() == held::kRight);
    rt.advanceTicks(199);
    CHECK(rt.keyboard().heldKeys() == 0);
    CHECK(rt.keyboard().ring().empty());
    CHECK(rt.keyboard().bios().size() == 0);
    // Repeats from 500 ms after the press, every 33 ms, until the release at tick 300.
    const double pressMs = (rt.clock().timeOfTick(100) + (rt.clock().timeOfTick(101) - rt.clock().timeOfTick(100)) / 2) / 1e6;
    const double releaseMs = rt.clock().timeOfTick(300) / 1e6;
    const int expected = static_cast<int>((releaseMs - pressMs - 500.0) / 33.0) + 1;
    CHECK(static_cast<int>(rt.keyboard().repeatsGenerated()) == expected);
}

TEST_CASE("gg runtime: a scripted click gives the codes of a frame loop") {
    GgRuntime rt;
    rt.installHandlers();
    rt.mouse().readDriverPosition();
    Zone button = Zone::rect(146, 170, 41, 23, 0, 0x708, 0x704);  // the sign-in's Start Game (REFERENCE_CAPTURES §2.2)
    rt.zones().addLocal(button);
    rt.setScript(script("5:move:160:180, 9:buttons:1, 18:buttons:0"));
    std::vector<uint16_t> codes;
    for (int f = 0; f < 6; ++f) {
        rt.timers().set(0, 4);
        rt.timerWait(0);
        codes.push_back(rt.getInputCode());
    }
    CHECK(codes == std::vector<uint16_t>{0, 0, 0x4708, 0x8708, 0x704, 0});
    CHECK(rt.mouse().x() == 160);
    CHECK(rt.mouse().y() == 180);
    CHECK(rt.mouse().release().tick == 17u);
}

TEST_CASE("gg runtime: the run limit ends a run at a yield point") {
    GgRuntime rt;
    rt.setRunLimit(100);
    int frames = 0;
    try {
        while (true) {
            rt.timers().set(0, 4);
            ++frames;
            rt.timerWait(0);
        }
    } catch (const GgRunEnd& e) {
        CHECK(e.reason == GgRunEnd::Reason::Limit);
    }
    CHECK(rt.clock().interrupts() == 100u);
    CHECK(frames == 26);  // the frame of tick 100 began; its wait would pass the limit
}

TEST_CASE("gg runtime: pictures go to the frame sink with the interrupt count") {
    GgRuntime rt;
    std::vector<uint64_t> shownAt;
    rt.setFrameSink([&shownAt](uint64_t i, std::span<const uint32_t> rgba) {
        CHECK(rgba.size() == 64000u);
        shownAt.push_back(i);
    });
    std::vector<uint32_t> pic(64000, 0xFF0000FFu);
    rt.showFrame(pic);
    rt.waitTicks(10);
    rt.showFrame(pic);
    rt.showFrame(std::vector<uint32_t>(10));  // a wrong size is ignored
    CHECK(shownAt == std::vector<uint64_t>{0, 10});
    CHECK(rt.framesShown() == 2u);
    CHECK(rt.frame().size() == 64000u);
}

// ------------------------------------------------------------------------------- real time

TEST_CASE("gg runtime: real time follows the host clock, drops long stalls") {
    ManualClock host(0);
    RuntimeConfig cfg;
    cfg.deterministic = false;
    cfg.clock = &host;
    GgRuntime rt(cfg);
    rt.pump();  // the first pump only syncs
    for (int i = 0; i < 100; ++i) {  // one second in host frames of 10 ms
        host.advance(10);
        rt.pump();
    }
    CHECK(rt.clock().interrupts() == rt.clock().timeBase().ticksAt(1'000'000'000));
    CHECK(rt.ticks() == 145u);
    // Timer_Wait sleeps on the clock (ManualClock: 1 ms per sleep) until its deadline.
    rt.timers().set(0, 4);
    rt.timerWait(0);
    CHECK(rt.ticks() == 149u);
    // A stall of 10 s is cut to 32 ticks.
    host.advance(10'000);
    rt.pump();
    CHECK(rt.ticks() >= 180u);
    CHECK(rt.ticks() <= 181u);
    CHECK(rt.droppedNs() > 9'000'000'000u);
    // No drift: an hour in 1 ms steps.
    ManualClock h2(0);
    cfg.clock = &h2;
    GgRuntime rt2(cfg);
    rt2.pump();
    for (int i = 0; i < 3'600'000; ++i) {
        h2.advance(1);
        rt2.pump();
    }
    CHECK(rt2.clock().interrupts() == rt2.clock().timeBase().ticksAt(3'600'000'000'000ull));
    CHECK(rt2.clock().interrupts() == 524'347u);
}

// -------------------------------------------------------------------------------- host events

TEST_CASE("gg runtime: host key and mouse events through a headless host") {
    platform::HostConfig hc;
    hc.layout = gfx::PresenterLayout::dos1990();
    platform::Host host(hc);
    GgRuntime rt({}, &host);
    rt.installHandlers();
    rt.mouse().readDriverPosition();
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = SDL_SCANCODE_A;
    e.key.down = true;
    host.postEvent(e);
    e.key.repeat = true;  // a host auto-repeat: ignored
    host.postEvent(e);
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    e.key.repeat = false;
    host.postEvent(e);
    SDL_Event m{};
    m.type = SDL_EVENT_MOUSE_MOTION;
    m.motion.x = 100.5f;
    m.motion.y = 50.0f;
    host.postEvent(m);
    SDL_Event b{};
    b.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    b.button.button = SDL_BUTTON_LEFT;
    b.button.down = true;
    b.button.x = 100.5f;
    b.button.y = 50.0f;
    host.postEvent(b);
    // The poll pumps first; the press latch has no zone (0), then the 'a' record brings its key.
    CHECK(rt.getInputCode() == 'a');
    CHECK(rt.keyboard().bios().size() == 0);
    CHECK(rt.mouse().x() == 100);
    CHECK(rt.mouse().y() == 50);
    CHECK(rt.mouse().press().kind == 1);
    // A quit request ends the run at the next yield point.
    SDL_Event q{};
    q.type = SDL_EVENT_QUIT;
    host.postEvent(q);
    CHECK_THROWS_AS(rt.timerWait(0), GgRunEnd);
}

// ------------------------------------------------------------------------------ Seq_Run, exit

TEST_CASE("gg Seq_Run: next, jump, back, stop and the NULL end") {
    std::vector<int> trace;
    int calls3 = 0;
    std::vector<StateFn> table(7);
    table[1] = [&] { trace.push_back(1); return int16_t{0}; };             // -> 2
    table[2] = [&] { trace.push_back(2); return int16_t{4}; };             // -> 4
    table[4] = [&] { trace.push_back(4); return int16_t{3}; };             // -> 3
    table[3] = [&] { trace.push_back(3); return int16_t(++calls3 == 1 ? -2 : 5); };  // back to 4, then to 5
    // 5 is empty: the run ends with 0.
    int16_t state = 1;
    CHECK(seqRun(table, state) == 0);
    CHECK(trace == std::vector<int>{1, 2, 4, 3, 4, 3});
    CHECK(state == 5);
    // -1 stops and is returned; two -2 in a row stay on prev.
    trace.clear();
    int backs = 0;
    std::vector<StateFn> t2(4);
    t2[1] = [&] { trace.push_back(1); return int16_t(backs >= 2 ? -1 : 0); };
    t2[2] = [&] { trace.push_back(2); ++backs; return int16_t{-2}; };
    state = 1;
    CHECK(seqRun(t2, state) == -1);
    CHECK(trace == std::vector<int>{1, 2, 1, 2, 1});
    // A -2 from the first state leaves the table: ended with 0.
    std::vector<StateFn> t3(2);
    t3[1] = [] { return int16_t{-2}; };
    state = 1;
    CHECK(seqRun(t3, state) == 0);
    CHECK(state == -1);
}

TEST_CASE("gg exit: GgExit carries the code; the atexit list runs newest first") {
    ExitHandlers h;
    std::vector<int> order;
    h.atExit([&] { order.push_back(1); });
    h.atExit([&] { order.push_back(2); });
    h.atExit([] { throw std::runtime_error("shutdown failure"); });
    h.run();
    CHECK(order == std::vector<int>{2, 1});
    CHECK(h.size() == 0);
    try {
        gameExit(exit_code::kNo286);
        FAIL("no exception");
    } catch (const GgExit& e) {
        CHECK(e.code == 5);
    }
}
