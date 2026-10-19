// DOS edition runtime (dos/app/dos_runtime.h, script_input.h): the virtual PIT and its accuracy over
// long runs, lockstep stepping (frameBegin -> gameTick -> frameBody per game tick), the slow-machine
// step gate, the 13-tick and 60-tick counters, the tick cap, determinism under any host chunking,
// the original frame clock, scripted input, and Turbo C 2.0 rand == rtl/borland_rand.
#include <doctest/doctest.h>

#include <SDL3/SDL_scancode.h>

#include <cmath>
#include <string>
#include <vector>

#include "dos/app/dos_runtime.h"
#include "dos/app/script_input.h"
#include "dos_test_data.h"
#include "rtl/borland_rand.h"

using namespace tmport;
using namespace tmport::dos;

namespace {

constexpr uint64_t kSec = 1'000'000'000ull;

// floor(t * 3579545 / (15123 * 1e9)), computed independently of PitClock.
uint64_t expectedPitTicks(uint64_t tNs) {
    const uint64_t a = tNs / kSec, b = tNs % kSec;
    const uint64_t whole = a * 3579545ull;
    const uint64_t q1 = whole / 15123ull, r1 = whole % 15123ull;
    return q1 + (r1 * kSec + b * 3579545ull) / (15123ull * kSec);
}

uint32_t lcg(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}

struct Recorder : RuntimeClient {
    std::string trace;
    std::vector<TickInfo> ticks;
    int sounds = 0;
    bool markGateInTick = false;
    DosRuntime* rt = nullptr;
    uint8_t mode = 0;
    bool quitAfterTick = false;
    bool quit = false;
    void soundTick() override {
        ++sounds;
        trace += 's';
    }
    void frameBegin() override { trace += 'B'; }
    void gameTick(const TickInfo& t) override {
        ticks.push_back(t);
        trace += t.gated ? 'g' : 'T';
        if (markGateInTick && rt) rt->stepGate().mark();
        if (quitAfterTick) quit = true;
    }
    void frameBody() override { trace += 'F'; }
    uint8_t uiMode() const override { return mode; }
    bool quitRequested() const override { return quit; }
};

// Logs what a game tick sees of the input (g_keys at entry, one BIOS key per tick), masks Up/Down
// like flat ground.
struct TickSeen {
    uint64_t tick = 0;
    uint16_t keys = 0;  // g_keys at Game_Tick entry
    uint16_t bios = 0;  // the BIOS key Input_Poll would take
    bool operator==(const TickSeen&) const = default;
};
struct InputLogger : RuntimeClient {
    DosRuntime* rt = nullptr;
    std::vector<TickSeen> log;
    void gameTick(const TickInfo& t) override {
        uint16_t& keys = rt->keyboard().isr().keys;
        TickSeen seen{t.gameTick, keys, 0};
        if (rt->keyboard().bios().peek()) seen.bios = rt->keyboard().bios().read();
        log.push_back(seen);
        keys &= 0xFFFC;
    }
};

}  // namespace

TEST_CASE("DOS PIT: exact time base (1193181.67 / 5041 Hz)") {
    CHECK(kPitDivisor == 5041);
    CHECK(PitClock::kPitHz == doctest::Approx(236.6954308).epsilon(1e-9));
    CHECK(PitClock::kGameTickHz == doctest::Approx(47.33908616).epsilon(1e-9));
    CHECK(PitClock::timeOfPitTick(0) == 0);
    CHECK(PitClock::timeOfPitTick(1) == 4'224'839);    // 4.2248386 ms, rounded up
    CHECK(PitClock::timeOfGameTick(1) == 21'124'194);  // 21.1241932 ms, rounded up
    CHECK(PitClock::pitTicksAt(3600 * kSec) == 852'103);  // one hour
    CHECK(expectedPitTicks(3600 * kSec) == 852'103);
    for (uint64_t k : {1ull, 2ull, 715'908ull, 715'909ull, 715'910ull, 123'456'789ull, 10'000'000'000ull}) {
        const uint64_t t = PitClock::timeOfPitTick(k);
        CHECK(PitClock::pitTicksAt(t) == k);
        CHECK(PitClock::pitTicksAt(t - 1) == k - 1);
        CHECK(expectedPitTicks(t) == k);
        CHECK(expectedPitTicks(t - 1) == k - 1);
    }
}

TEST_CASE("DOS PIT: no drift over a 10-hour run fed in irregular host steps") {
    RuntimeOptions o;
    o.maxCatchUpGameTicks = 0;  // feed up to a second at a time
    DosRuntime rt(o);
    Recorder rec;
    rt.setClient(&rec);
    uint64_t total = 0;
    uint32_t seed = 12345;
    while (total < 10 * 3600 * kSec) {
        const uint64_t dt = 1 + (lcg(seed) % 997'000'000u);
        rt.advance(dt);
        total += dt;
        REQUIRE(rt.pitTicks() == expectedPitTicks(total));
    }
    CHECK(rt.nowNs() == total);
    CHECK(rt.gameTicks() == rt.pitTicks() / 5);
    CHECK(rt.droppedPitTicks() == 0);
    CHECK(static_cast<uint64_t>(rec.sounds) == rt.pitTicks());
    CHECK(rec.ticks.size() == rt.gameTicks());
    const double secs = static_cast<double>(total) / 1e9;
    CHECK(static_cast<double>(rt.pitTicks()) / secs == doctest::Approx(236.6954308).epsilon(1e-6));
    CHECK(static_cast<double>(rt.gameTicks()) / secs == doctest::Approx(47.33908616).epsilon(1e-6));
    MESSAGE("10 h: " << rt.pitTicks() << " PIT ticks, " << rt.gameTicks() << " game ticks");
}

TEST_CASE("DOS PIT: a 60 Hz host for an hour runs every tick, with the default cap") {
    DosRuntime rt;
    uint64_t total = 0;
    for (int frame = 0; frame < 60 * 3600; ++frame) {
        const uint64_t dt = (frame % 3 == 2) ? 16'666'666 : 16'666'667;  // 1/60 s on average
        rt.advance(dt);
        total += dt;
    }
    CHECK(total == 3600 * kSec);
    CHECK(rt.droppedPitTicks() == 0);
    CHECK(rt.pitTicks() == 852'103);
    CHECK(rt.gameTicks() == 170'420);
}

TEST_CASE("DOS runtime: lockstep order, one original frame per game tick") {
    DosRuntime rt;
    Recorder rec;
    rec.rt = &rt;
    rec.markGateInTick = true;  // Anim_Tick / Player_Move mark the gate inside the tick
    rt.setClient(&rec);
    rt.stepGameTicks(3);
    // Timer_ISR: sound every PIT tick, Game_Tick on interrupts 5, 10, 15; the gate never blocks.
    CHECK(rec.trace == "sssssBTFsssssBTFsssssBTF");
    REQUIRE(rec.ticks.size() == 3);
    CHECK(rec.ticks[0].gameTick == 1);
    CHECK(rec.ticks[0].ticks == 5);
    CHECK(rec.ticks[2].ticks == 15);
    CHECK(rec.ticks[1].timeNs == PitClock::timeOfGameTick(2));
    for (const auto& t : rec.ticks) CHECK_FALSE(t.gated);
    CHECK(rt.frames() == 3);
    CHECK(rt.nowNs() == PitClock::timeOfGameTick(3));
    rt.stepPitTicks(2);
    CHECK(rt.pitTicks() == 17);
    CHECK(rec.sounds == 17);
}

TEST_CASE("DOS runtime: slow-machine mode keeps the step gate (DS:910D)") {
    RuntimeOptions o;
    o.frameMode = FrameMode::SlowMachine;
    o.slowMachineTicksPerFrame = 3;
    DosRuntime rt(o);
    Recorder rec;
    rec.rt = &rt;
    rec.markGateInTick = true;
    rt.setClient(&rec);
    rt.stepGameTicks(6);
    std::string ticksOnly;
    for (char c : rec.trace)
        if (c != 's') ticksOnly += c;
    // A frame spans 3 ticks: the first runs the logic step, the next two find g_stepDone set.
    CHECK(ticksOnly == "BTggFBTggF");
    CHECK(rt.frames() == 2);
    // Without a step in the first tick, no tick is gated.
    Recorder idle;
    DosRuntime rt2(o);
    rt2.setClient(&idle);
    rt2.stepGameTicks(3);
    for (const auto& t : idle.ticks) CHECK_FALSE(t.gated);
}

namespace {

// A client whose frame reports a cost (DosRuntime::frameTiming) on chosen game ticks.
struct CostClient : Recorder {
    struct Cost {
        uint64_t tick = 0;      // the frame of this game tick reports it
        uint32_t quit = 0;      // a g_quit section first (FrameTiming units)
        uint32_t spend = 0;     // then drawing
        bool previous = false;  // fromPreviousTick()
        bool atBegin = false;   // reported by frameBegin instead of the body
    };
    std::vector<Cost> costs;
    void post(bool begin) {
        const uint64_t tick = rt->gameTicks();  // the runtime counts the tick before its frame begins
        for (const Cost& c : costs) {
            if (c.tick != tick || c.atBegin != begin) continue;
            rt->frameTiming().quitSection(c.quit);
            rt->frameTiming().spend(c.spend);
            if (c.previous) rt->frameTiming().fromPreviousTick();
        }
    }
    void frameBegin() override {
        trace += 'B';
        post(true);
    }
    void frameBody() override {
        trace += 'F';
        post(false);
    }
};

std::string withoutSound(const std::string& t) {
    std::string out;
    for (char c : t)
        if (c != 's') out += c;
    return out;
}

}  // namespace

TEST_CASE("DOS runtime: long frames at the reference speed (FrameTiming), strict lockstep in period mode") {
    SUBCASE("drawing past the next ticks: they run without a frame, gated after a step") {
        DosRuntime rt;
        CostClient c;
        c.rt = &rt;
        c.markGateInTick = true;
        rt.setClient(&c);
        // Tick 2's frame (asked for by tick 2 itself) draws for 12.5 PIT ticks: ticks 3 and 4 fall
        // inside it; Frame_Begin came after tick 2, so tick 3 starts with the gate clear.
        c.costs.push_back({2, 0, 12500});
        rt.stepGameTicks(6);
        CHECK(withoutSound(c.trace) == "BTFBTFTgBTFBTF");
        CHECK(rt.ticksInLongFrames() == 2);
        CHECK(rt.ticksFrozen() == 0);
        CHECK(rt.frames() == 4);
    }
    SUBCASE("asked for before the tick: the frame began after the previous tick, the gate carries") {
        DosRuntime rt;
        CostClient c;
        c.rt = &rt;
        c.markGateInTick = true;
        rt.setClient(&c);
        c.costs.push_back({2, 0, 12500, true});  // counts from tick 1: tick 3 (offset 10) inside
        rt.stepGameTicks(5);
        CHECK(withoutSound(c.trace) == "BTFBTFgBTFBTF");
    }
    SUBCASE("I/O: the ticks inside a g_quit section do not run Game_Tick nor its counters") {
        DosRuntime rt;
        CostClient c;
        c.rt = &rt;
        rt.setClient(&c);
        c.costs.push_back({3, 21000, 6000});  // 21 PIT ticks of I/O: ticks 4..7 frozen, 8 inside the drawing
        rt.stepGameTicks(10);
        CHECK(withoutSound(c.trace) == "BTFBTFBTFTBTFBTF");
        CHECK(rt.ticksFrozen() == 4);
        CHECK(rt.ticksInLongFrames() == 5);
        CHECK(rt.counters().cycle13 == 12 - 6);  // 6 Game_Ticks ran
        REQUIRE(c.ticks.size() == 6);
        CHECK(c.ticks[3].gameTick == 8);
        CHECK(c.ticks[3].inLongFrame);
    }
    SUBCASE("I/O that Frame_Begin's latches start covers the frame's own tick") {
        DosRuntime rt;
        CostClient c;
        c.rt = &rt;
        rt.setClient(&c);
        c.costs.push_back({3, 20309, 0, true, true});  // from tick 2: ticks 3..6 frozen, 7 runs
        rt.stepGameTicks(8);
        CHECK(withoutSound(c.trace) == "BTFBTFBFBTFBTF");
        CHECK(rt.ticksFrozen() == 4);
    }
    SUBCASE("a cost reported before the first frame counts from PIT tick 0") {
        DosRuntime rt;
        Recorder c;
        rt.setClient(&c);
        rt.frameTiming().quitSection(9850);  // Title_Run's OPENING load: game tick 1 falls into it
        rt.stepGameTicks(3);
        CHECK(withoutSound(c.trace) == "BTFBTF");
        CHECK(rt.ticksFrozen() == 1);
        REQUIRE_FALSE(c.ticks.empty());
        CHECK(c.ticks[0].gameTick == 2);
    }
    SUBCASE("period mode and the slow-machine mode ignore the costs") {
        for (const bool slow : {false, true}) {
            RuntimeOptions o;
            if (slow) {
                o.frameMode = FrameMode::SlowMachine;
                o.slowMachineTicksPerFrame = 1;
            } else {
                o.frameSpeed = FrameSpeed::Period;
            }
            DosRuntime rt(o);
            CostClient c;
            c.rt = &rt;
            rt.setClient(&c);
            c.costs.push_back({2, 21000, 12500});
            rt.stepGameTicks(5);
            CHECK(withoutSound(c.trace) == "BTFBTFBTFBTFBTF");
            CHECK(rt.ticksFrozen() == 0);
        }
    }
}

TEST_CASE("DOS runtime: resumeAt starts the time base at a saved g_ticks and PIT phase") {
    DosRuntime rt;
    Recorder c;
    rt.setClient(&c);
    rt.resumeAt(0xFFFFFFF0u, 3);  // the countdown DS:3D34 was 2: the next game tick 3 PIT ticks later
    CHECK(rt.ticks() == 0xFFFFFFF0u);
    CHECK(c.sounds == 2);
    CHECK(rt.gameTicks() == 0);
    rt.stepGameTicks(2);
    REQUIRE(c.ticks.size() == 2);
    CHECK(c.ticks[0].gameTick == 1);
    CHECK(c.ticks[0].ticks == 0xFFFFFFF3u);
    CHECK(c.ticks[1].ticks == 0xFFFFFFF8u);
    CHECK(rt.ticks() == 0xFFFFFFF8u);
    rt.resumeAt(5, 5);  // only before the first tick
    CHECK(rt.ticks() == 0xFFFFFFF8u);
}

TEST_CASE("DOS runtime: the 13-tick cycle and the 60-tick hint second (130f:0186)") {
    DosRuntime rt;
    Recorder rec;
    rt.setClient(&rec);
    CHECK(rt.counters().cycle13 == 12);
    std::vector<int> cycle;
    for (int i = 0; i < 14; ++i) {
        rt.stepGameTicks(1);
        cycle.push_back(rt.counters().cycle13);
    }
    CHECK(cycle == std::vector<int>{11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 12, 11});
    rt.counters() = TickCounters{};
    rt.stepGameTicks(59);
    CHECK(rt.counters().seconds == 0);
    rt.stepGameTicks(1);
    CHECK(rt.counters().seconds == 1);
    CHECK(rt.counters().secondCountdown == 60);
    rt.stepGameTicks(120);
    CHECK(rt.counters().seconds == 3);
    // With a UI mode open the byte runs through 0 and wraps: no second for 256 ticks.
    rec.mode = 0x40;
    rt.stepGameTicks(300);
    CHECK(rt.counters().seconds == 3);
    CHECK(rt.counters().secondCountdown == static_cast<uint8_t>(60 - 300));
    rec.mode = 0;
    rt.stepGameTicks(static_cast<uint8_t>(60 - 300));
    CHECK(rt.counters().seconds == 4);
}

TEST_CASE("DOS runtime: g_quit skips game ticks and ends the loop after the frame") {
    RuntimeOptions o;
    o.frameMode = FrameMode::SlowMachine;
    o.slowMachineTicksPerFrame = 2;
    DosRuntime rt(o);
    Recorder rec;
    rec.quitAfterTick = true;
    rt.setClient(&rec);
    rt.stepGameTicks(10);
    CHECK(rec.ticks.size() == 1);  // the second tick of the frame returned at once
    CHECK_FALSE(rt.running());
    CHECK(rt.frames() == 1);
    CHECK(rt.gameTicks() == 2);
    CHECK(rt.advance(kSec) == 0);
}

TEST_CASE("DOS runtime: a long host stall is dropped, not caught up") {
    DosRuntime rt;
    Recorder rec;
    rt.setClient(&rec);
    rt.advance(10 * kSec);
    CHECK(rt.gameTicks() == 4);  // maxCatchUpGameTicks
    CHECK(rt.droppedPitTicks() > 2000);
    CHECK(rt.nowNs() < PitClock::timeOfGameTick(5));
    // Afterwards the time base runs normally again.
    const uint64_t before = rt.gameTicks();
    for (int i = 0; i < 60; ++i) rt.advance(kSec / 60);
    CHECK(rt.gameTicks() - before >= 47);
    CHECK(rt.gameTicks() - before <= 48);
}

TEST_CASE("DOS runtime: the same script gives the same ticks for any host chunking") {
    const char* script = "10:down:right 30:down:up 40:up:up 70:tap:enter 80:down:a 200:up:a 210:up:right "
                         "220:tap:pgup:30 260:move:100:50 261:buttons:1 262:buttons:0";
    auto run = [&](auto chunk) {
        DosRuntime rt;
        InputLogger log;
        log.rt = &rt;
        rt.setClient(&log);
        std::string err;
        auto s = ScriptedInput::parse(script, err);
        REQUIRE_MESSAGE(s, err);
        rt.setScript(*s);
        uint32_t seed = 7;
        while (rt.gameTicks() < 400) rt.advance(chunk(seed));
        log.log.resize(400);
        return log.log;
    };
    const auto a = run([](uint32_t&) { return uint64_t{16'666'667}; });
    const auto b = run([](uint32_t&) { return uint64_t{6'944'444}; });  // 144 Hz
    const auto c = run([](uint32_t& s) { return uint64_t{1 + lcg(s) % 60'000'000u}; });
    DosRuntime rt;
    InputLogger log;
    log.rt = &rt;
    rt.setClient(&log);
    std::string err;
    rt.setScript(*ScriptedInput::parse(script, err));
    rt.stepGameTicks(400);
    CHECK(a == log.log);
    CHECK(b == log.log);
    CHECK(c == log.log);
    // Spot checks of what the ticks saw: Right then Up = jump at tick 30, Enter's key word at 70.
    CHECK(log.log[29].tick == 30);
    CHECK(log.log[29].keys == 0x28);
    CHECK(log.log[69].bios == 0x1C0D);
    CHECK(rt.mouse().x() == 50);
    CHECK(rt.mouse().buttons() == 0);
}

TEST_CASE("DOS original frame clock: reference rates and period mode") {
    OriginalFrameClock ref(FrameSpeed::Reference);
    long long coins = 0, flash = 0, sparkle = 0, search = 0, dissolve = 0;
    const int frames = 47339;  // ~1000 s of lockstep frames
    for (int i = 0; i < frames; ++i) {
        ref.beginFrame(5);
        coins += ref.steps(FrameFeature::CoinCountUp);
        flash += ref.steps(FrameFeature::ChampionFlash);
        sparkle += ref.steps(FrameFeature::TitleSparkle);
        search += ref.steps(FrameFeature::Search);
        dissolve += ref.steps(FrameFeature::Dissolve);
    }
    // Exactly floor(rate * time): steps per 1000 s * frames * 5 PIT ticks * 15123 / 3579545 s.
    auto expected = [&](uint64_t rateMilli) {
        return static_cast<long long>(rateMilli * static_cast<uint64_t>(frames) * 5u * 15123u / (1000ull * 3579545u));
    };
    CHECK(coins == expected(71'300));
    CHECK(flash == expected(918'200));
    CHECK(sparkle == expected(1'730'000));
    CHECK(dissolve == expected(2'755'906));
    const double secs = frames * 5 * 15123.0 / 3579545.0;
    CHECK(static_cast<double>(coins) / secs == doctest::Approx(71.3).epsilon(1e-4));
    CHECK(search == frames);  // one per frame (at the reference speed main_loop.cpp times the search frames)
    // The slow-machine count-up alternates 1 and 2 coins per 21.1 ms frame (14.0 ms per coin).
    OriginalFrameClock c;
    c.beginFrame(5);
    CHECK(c.steps(FrameFeature::CoinCountUp) == 1);
    // The dissolve takes 6 or 7 lockstep frames (127 ms).
    OriginalFrameClock d;
    int cells = 0, dframes = 0;
    while (cells < kDissolveCells) {
        d.beginFrame(5);
        cells += d.steps(FrameFeature::Dissolve);
        ++dframes;
    }
    CHECK(dframes == 7);
    OriginalFrameClock period(FrameSpeed::Period);
    period.beginFrame(5);
    CHECK(period.steps(FrameFeature::CoinCountUp) == 1);
    CHECK(period.steps(FrameFeature::ChampionFlash) == 1);
    CHECK(period.steps(FrameFeature::Dissolve) == kDissolveCells);
    // Through the runtime: one beginFrame per original frame.
    RuntimeOptions o;
    o.frameSpeed = FrameSpeed::Period;
    DosRuntime rt(o);
    rt.stepGameTicks(2);
    CHECK(rt.frameClock().steps(FrameFeature::TitleSparkle) == 1);
}

TEST_CASE("DOS scripted input: parsing") {
    std::string err;
    auto s = ScriptedInput::parse("55:down:right, 103:up:right\n+92:tap:pgup:95 300:click:10:20 301:buttons:2 302:move:5:6", err);
    REQUIRE_MESSAGE(s, err);
    const auto& e = s->events();
    REQUIRE(e.size() == 9);
    CHECK(e[0] == ScriptEvent{55, ScriptEvent::Kind::KeyDown, PcKey{0x4D, true}});
    CHECK(e[1].tick == 103);
    CHECK(e[2] == ScriptEvent{195, ScriptEvent::Kind::KeyDown, PcKey{0x49, true}});
    CHECK(e[3] == ScriptEvent{290, ScriptEvent::Kind::KeyUp, PcKey{0x49, true}});
    CHECK(e[4].kind == ScriptEvent::Kind::MouseMove);
    CHECK(e[4].x == 10);
    CHECK(e[5].buttons == 1);
    CHECK(e[6].tick == 301);
    CHECK(e[6].buttons == 0);
    CHECK(e[7].buttons == 2);
    CHECK(s->lastTick() == 302);
    CHECK(s->take(54).empty());
    CHECK(s->take(103).size() == 2);
    CHECK(s->take(300).size() == 4);
    CHECK_FALSE(s->finished());
    CHECK(s->take(1000).size() == 3);
    CHECK(s->finished());
    // Sub-tick times: N.F lies between game ticks N and N + 1 (applied before N + 1, its own time).
    auto sub = ScriptedInput::parse("806.4923:down:down 902.2367:up:down +0.5:tap:a:2 20.0:down:up 3.000001:up:up", err);
    REQUIRE_MESSAGE(sub, err);
    const auto& f = sub->events();
    REQUIRE(f.size() == 6);
    CHECK(f[0].tick == 4);  // 3.000001
    CHECK(f[0].phasePpm == 1);
    CHECK(f[1].tick == 20);  // 20.0 is the whole tick
    CHECK(f[1].phasePpm == ScriptEvent::kOnTick);
    CHECK(f[2].tick == 807);
    CHECK(f[2].phasePpm == 492300);
    CHECK(f[3].tick == 903);
    CHECK(f[3].phasePpm == 236700);
    CHECK(f[4].tick == 903);  // 902.7367 (+0.5)
    CHECK(f[4].phasePpm == 736700);
    CHECK(f[4].kind == ScriptEvent::Kind::KeyDown);
    CHECK(f[5].tick == 905);  // up two ticks later, same phase
    CHECK(f[5].phasePpm == 736700);
    for (const char* bad : {"down:right", "x:down:right", "5:down:nokey", "5:jump:up", "5:tap:up:0", "5:move:1", "+x:up:up",
                            "5.1234567:down:up", "5.:down:up", ".5:down:up"}) {
        INFO(bad);
        CHECK_FALSE(ScriptedInput::parse(bad, err));
        CHECK_FALSE(err.empty());
    }
    CHECK(ScriptedInput::parse("  ", err)->events().empty());
}

TEST_CASE("DOS scripted input: a sub-tick key down sets the phase of its typematic repeats") {
    // A key held from 10.0 or from 10.5 game ticks: the first repeat 500 ms later, then every 33 ms;
    // the repeat bytes reach the keyboard before the first tick at or after their time.
    auto repeatsBy = [](const char* script, uint64_t ticks) {
        DosRuntime rt;
        std::string err;
        rt.setScript(*ScriptedInput::parse(script, err));
        rt.stepGameTicks(ticks);
        return rt.keyboard().repeatsGenerated();
    };
    // 500 ms = 23.67 game ticks: from 10.0 the first repeat is due by tick 34, from 10.5 by tick 35.
    CHECK(repeatsBy("10:down:a", 33) == 0);
    CHECK(repeatsBy("10:down:a", 34) == 1);
    CHECK(repeatsBy("10.5:down:a", 34) == 0);
    CHECK(repeatsBy("10.5:down:a", 35) == 1);
    // The key itself is seen by tick 11.
    DosRuntime rt;
    InputLogger log;
    log.rt = &rt;
    rt.setClient(&log);
    std::string err;
    rt.setScript(*ScriptedInput::parse("10.5:down:right", err));
    rt.stepGameTicks(11);
    CHECK(log.log[9].keys == 0);
    CHECK(log.log[10].keys == 0x08);
}

TEST_CASE("DOS rand: Turbo C 2.0 rand is rtl/borland_rand (seed*0x015A4E35+1, bits 16..30, seed 1)") {
    BorlandRand r;
    for (int v : {346, 130, 10982, 1090, 11656, 7117, 17595, 6415, 22948, 31126}) CHECK(r.rand() == v);
    // Seeds read out of DOSBox-X save states (DS:8F48, u32) lie on the unseeded sequence from 1:
    // ega/signin after 13894 calls, tandy/signin after 49972, tandy/level2_resumed (a resumed game:
    // no srand) after 50429 (PORTING_NOTES.md §2.4).
    BorlandRand s;
    int calls = 0;
    auto advanceTo = [&](int n) {
        while (calls < n) {
            s.rand();
            ++calls;
        }
        return s.seed();
    };
    CHECK(advanceTo(13894) == 0x2EA1217Bu);
    CHECK(advanceTo(49972) == 0xB957427Du);
    CHECK(advanceTo(50429) == 0x74265B9Au);
}

TEST_CASE("DOS rand: the LCG of the user's SST.EXE (rand 4d12:001F, srand 4d12:000E, seed DS:8F48)") {
    TM_REQUIRE_DOS_DATA();
    CHECK(dosExe->ds16(0x8F48) == 1);  // initial seed, u32 = 1
    CHECK(dosExe->ds16(0x8F4A) == 0);
    auto code = [&](uint16_t off, std::vector<uint8_t> expect) {
        const auto b = dosExe->at(0x4d12, off);
        REQUIRE(b.size() >= expect.size());
        CHECK(std::vector<uint8_t>(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(expect.size())) == expect);
    };
    code(0x0026, {0xB9, 0x5A, 0x01, 0xBB, 0x35, 0x4E});  // MOV CX,015A; MOV BX,4E35 (multiplier)
    code(0x0031, {0x05, 0x01, 0x00, 0x83, 0xD2, 0x00});  // ADD AX,1; ADC DX,0 (+1)
    code(0x003E, {0xA1, 0x4A, 0x8F, 0x25, 0xFF, 0x7F});  // MOV AX,[8F4A]; AND AX,7FFF (bits 16..30)
    code(0x0011, {0x8B, 0x46, 0x06, 0x33, 0xD2, 0x89, 0x16, 0x4A, 0x8F, 0xA3, 0x48, 0x8F});  // srand: seed = (u16)arg
}

TEST_CASE("DOS runtime: host key events go through the PC keyboard") {
    DosRuntime rt;
    CHECK(rt.hostKey(SDL_SCANCODE_RIGHT, true, false));
    CHECK(rt.hostKey(SDL_SCANCODE_RIGHT, true, true));  // a host auto-repeat: ignored
    CHECK_FALSE(rt.hostKey(SDL_SCANCODE_LGUI, true, false));
    rt.stepGameTicks(1);
    CHECK(rt.keyboard().isr().keys == keybits::kRight);
    CHECK(rt.keyboard().bios().size() == 1);
    CHECK(rt.keyboard().bios().read() == 0x4D00);
    rt.releaseAllKeys();  // focus loss
    rt.stepGameTicks(1);
    CHECK(rt.keyboard().isr().keys == 0);
    CHECK(rt.keyboard().bios().size() == 0);
}
