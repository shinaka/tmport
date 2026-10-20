// The shared engine core's parameters for the second game (cmake/portcore.cmake, docs/DESIGN.md
// "Core and games"): the PIT time base and virtual PIT, deadline timers in PIT ticks with G&G's
// Timer_Expired, the INT 33h mouse with an event handler, the PC keyboard with a game's own INT 09h
// handler, the chip renderer and audio device ticked by a game's driver, the OPL write log and the
// text engine's dialects. Data-free; nothing opens a window or an audio device. The defaults (TM's)
// are covered by the TM tests and the byte-identical TM baselines.
#include <doctest/doctest.h>

#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "dos/audio/chip_audio_device.h"
#include "dos/audio/chip_renderer.h"
#include "dos/audio/opl_chip.h"
#include "dos/audio/opl_log.h"
#include "dos/input/int33_mouse.h"
#include "dos/input/pc_keyboard.h"
#include "text/text_engine.h"
#include "timing/deadline_timers.h"
#include "timing/pit_clock.h"

using namespace tmport;

TEST_CASE("portcore: the PIT time base at both games' divisors") {
    const PitTimeBase tm;  // SST.EXE 0x13B1
    CHECK(tm.divisor() == 0x13B1);
    CHECK(tm.hz() == doctest::Approx(236.6954308).epsilon(1e-9));
    CHECK(tm.timeOfTick(1) == 4'224'839);
    CHECK(tm.timeOfTick(1000) == 4'224'838'632);
    CHECK(tm.ticksAt(3600'000'000'000ull) == 852'103);

    const PitTimeBase gg(kPitDivisorGg);  // ssg.exe 1454:0046: 0x2000
    CHECK(gg.hz() == doctest::Approx(145.6520589).epsilon(1e-9));
    CHECK(gg.hz() / 2 == doctest::Approx(72.82602946).epsilon(1e-9));  // the sound tick
    CHECK(gg.timeOfTick(1) == 6'865'678);                               // 6.8657 ms, rounded up
    CHECK(gg.timeOfTick(1000) == 6'865'677'063);
    CHECK(gg.ticksAt(3600'000'000'000ull) == 524'347);
    for (uint64_t k : {1ull, 2ull, 7ull, 145ull, 146ull, 715'909ull, 1'000'003ull, 123'456'789ull}) {
        for (const PitTimeBase* b : {&tm, &gg}) {
            const uint64_t t = b->timeOfTick(k);
            CHECK(b->ticksAt(t) == k);
            CHECK(b->ticksAt(t - 1) == k - 1);
        }
    }
    CHECK_THROWS(PitTimeBase(0));
    CHECK_THROWS(PitTimeBase(0x10000));  // beyond the exact arithmetic
}

TEST_CASE("portcore: virtual PIT, the sound tick on even counter values and the counter write") {
    VirtualPit pit(kPitDivisorGg);
    std::vector<uint32_t> sound;
    pit.onTick(2, [&](uint32_t c) { sound.push_back(c); });  // Timer_ISR 1454:0096: Snd_Tick on even g_ticks
    int every = 0;
    const int id = pit.onTick(1, [&](uint32_t) { ++every; });
    pit.step(5);
    CHECK(pit.ticks() == 5);
    CHECK(sound == std::vector<uint32_t>{2, 4});
    CHECK(every == 5);
    // The dialog freeze (Dlg_RunModal 3f56:08c3) writes the saved tick back: the counter, and so the
    // parity of the sound tick, follows the written value.
    pit.setTicks(2);
    pit.step(2);
    CHECK(pit.ticks() == 4);
    CHECK(pit.interrupts() == 7);
    CHECK(sound == std::vector<uint32_t>{2, 4, 4});
    pit.remove(id);
    pit.step(1);
    CHECK(every == 7);
    // Real time: 100 ms = 14 interrupts (14 * 6.8657 ms = 96.1 ms), the phase is kept.
    VirtualPit rt(kPitDivisorGg);
    CHECK(rt.advanceNs(100'000'000) == 14);
    CHECK(rt.nowNs() == 100'000'000);
    CHECK(rt.advanceNs(1'000'000'000, 10) == 10);  // a stall: the rest is dropped
    CHECK(rt.droppedTicks() == 160 - 14 - 10);       // 1.1 s = 160 interrupts
}

TEST_CASE("portcore: deadline timers in PIT ticks with G&G's strict Timer_Expired") {
    VirtualPit pit(kPitDivisorGg);
    PitTickClock clock(pit);
    DeadlineTimers t(&clock, ExpiryRule::AfterDeadline);
    CHECK(t.rule() == ExpiryRule::AfterDeadline);
    t.set(0, static_cast<uint16_t>(4));  // Timer_Set(0, 4): the frame pacer
    pit.step(3);
    CHECK_FALSE(t.reached(0));
    CHECK_FALSE(t.expired(0));
    pit.step(1);
    CHECK(t.reached(0));        // Timer_Wait 152c:0029: tick >= deadline
    CHECK_FALSE(t.expired(0));  // Timer_Expired 152c:004a: tick > deadline
    clock.sleepMs(1);           // one more interrupt
    CHECK(t.expired(0));
    // TM's rule (the default): expired at the deadline.
    ManualClock ms(1000);
    DeadlineTimers w(&ms);
    w.set(7, 100);
    ms.advance(100);
    CHECK(w.expired(7));
    CHECK(w.reached(7));
}

TEST_CASE("portcore: INT 33h mouse in mode 13h with an event handler") {
    using dos::Int33Mouse;
    // G&G: the driver counts 640 virtual pixels in mode 13h (re/gg/docs/code_runtime.md §7).
    Int33Mouse m(dos::Int33MouseConfig{0, 639, 0, 199, 2, 1, 320, 100});
    std::vector<Int33Mouse::Event> events;
    m.setEventHandler(0x1F, [&](const Int33Mouse::Event& e) { events.push_back(e); });  // 1454:0000 AX=0Ch CX=1Fh
    m.setLogicalPosition(138, 120);
    REQUIRE(events.size() == 1);
    CHECK(events[0].events == Int33Mouse::kEventMove);
    CHECK(events[0].x == 276);  // the game halves it: 138
    CHECK(events[0].y == 120);
    m.setLogicalPosition(138, 120);  // no change, no event
    CHECK(events.size() == 1);
    m.setButton(Int33Mouse::kLeft, true);
    m.setButton(Int33Mouse::kRight, true);
    m.setButtons(0);
    REQUIRE(events.size() == 4);
    CHECK(events[1].events == Int33Mouse::kEventLeftDown);
    CHECK(events[1].buttons == 1);
    CHECK(events[2].events == Int33Mouse::kEventRightDown);
    CHECK(events[2].buttons == 3);
    CHECK(events[3].events == (Int33Mouse::kEventLeftUp | Int33Mouse::kEventRightUp));
    CHECK(events[3].buttons == 0);
    // Mouse_SetBounds 17ab:017f: AX=07h CX = x, DX = 2 * maxX; AX=08h CX = y, DX = maxY.
    m.setRangeX(0, 2 * (319 - 9));
    m.setRangeY(0, 199 - 9);
    m.setLogicalPosition(319, 199);
    CHECK(m.x() == 620);
    CHECK(m.y() == 190);
    // A mask without the move bit: moves are not reported; middle buttons are, when subscribed.
    m.setEventHandler(Int33Mouse::kEventMiddleDown, [&](const Int33Mouse::Event& e) { events.push_back(e); });
    m.setLogicalPosition(10, 10);
    m.setButton(Int33Mouse::kMiddle, true);
    REQUIRE(events.size() == 6);
    CHECK(events[5].events == Int33Mouse::kEventMiddleDown);
    m.setEventHandler(0, {});
    m.setButton(Int33Mouse::kMiddle, false);
    CHECK(events.size() == 6);
    // The defaults are TM DOS's: x 0..159 (one unit = 2 pixels), no handler.
    Int33Mouse tm;
    tm.setLogicalPosition(319, 250);
    CHECK(tm.x() == 159);
    CHECK(tm.y() == 199);
    CHECK(tm.eventMask() == 0);
}

TEST_CASE("portcore: PC keyboard with a game's own INT 09h handler") {
    using namespace dos;
    // A handler like G&G's Kbd_ISR 1454:00e4: arrows swallowed in play mode, the rest passed on.
    struct Handler final : ScanByteHandler {
        std::vector<uint8_t> seen;
        bool onScanByte(uint8_t b) override {
            seen.push_back(b);
            return (b & 0x7F) != 0x4B;  // swallow Left
        }
    } h;
    ScanKeyboard kb;
    kb.setHandler(&h);
    const PcKey left = *pcKeyByName("left"), a = *pcKeyByName("a");
    kb.keyDown(left, 1'000'000);
    kb.keyUp(left, 2'000'000);
    kb.keyDown(a, 3'000'000);
    kb.keyUp(a, 4'000'000);
    kb.deliverUntil(10'000'000);
    // The E0 prefixes of the grey Left reach the BIOS only.
    CHECK(h.seen == std::vector<uint8_t>{0x4B, 0xCB, 0x1E, 0x9E});
    CHECK(kb.bios().read() == 0x1E61);  // 'a'
    CHECK(kb.bios().read() == 0);       // Left was swallowed
    // Typematic repeats at the DOSBox-X rate.
    kb.keyDown(a, 20'000'000);
    kb.deliverUntil(20'000'000 + 600'000'000ull);  // 500 ms delay, then every 33 ms: 4 repeats
    CHECK(kb.repeatsGenerated() == 4);
    CHECK(kb.bios().size() == 5);
}

namespace {

// A driver that toggles the speaker gate every tick and writes one OPL register through a tap.
struct TestDriver final : dos::SoundTickSource {
    dos::ChipRenderer* r = nullptr;
    dos::OplWriter* opl = nullptr;
    int ticks = 0;
    void soundTick() override {
        ++ticks;
        r->speaker().setDivisor(1193);
        r->speaker().setEnabled(ticks % 2 == 1);
        if (opl) opl->oplWrite(0xA0, static_cast<uint8_t>(ticks));
    }
};

}  // namespace

TEST_CASE("portcore: chip renderer ticked by a game's driver at G&G's sound rate, OPL log") {
    using namespace dos;
    ChipRenderConfig cfg;
    cfg.tickHz = PitTimeBase(kPitDivisorGg).hz() / 2;  // Snd_Tick on every even PIT tick
    cfg.mode = SynthMode::Raw;
    OplChip chip(cfg.sampleRate);
    ChipRenderer r(cfg, &chip);
    CHECK(r.samplesPerTick() == doctest::Approx(659.105).epsilon(1e-6));
    OplWriteLog log;
    OplLogTap tap(chip, log, [&] { return r.ticks(); });
    TestDriver d;
    d.r = &r;
    d.opl = &tap;
    r.setTickSource(&d);
    std::vector<double> out;
    size_t n = 0;
    for (int i = 0; i < 73; ++i) n += r.renderTick(out);  // about one second
    CHECK(d.ticks == 73);
    CHECK(r.ticks() == 73);
    CHECK(n == out.size());
    CHECK(n == 48114);  // 73 * 659.105 = 48114.7
    REQUIRE(log.entries().size() == 73);
    CHECK(log.entries()[0] == OplWriteLog::Entry{0, 0xA0, 0x01, false});  // logged before the tick counts
    log.setCommand(true);
    tap.oplWrite(0xB0, 0x2A);
    CHECK(log.text().ends_with("72 a0 49\n73 b0 2a c\n"));
    OplWriteLog off(false);
    off.write(1, 2, 3);
    CHECK(off.entries().empty());
}

TEST_CASE("portcore: chip audio device runs posted calls before the next tick") {
    using namespace dos;
    ChipRenderConfig rc;
    rc.tickHz = 100.0;  // 480 samples per tick
    ChipRenderer r(rc);
    std::vector<int> order;
    int tick = 0;
    struct Counter final : SoundTickSource {
        int* t;
        void soundTick() override { ++*t; }
    } c;
    c.t = &tick;
    r.setTickSource(&c);
    ChipAudioConfig cfg;
    cfg.render = rc;
    ChipAudioDevice dev(r, cfg);
    CHECK_FALSE(dev.isOpen());
    dev.setTickHook([&] { order.push_back(-tick); });
    dev.post([&] { order.push_back(100 + tick); });
    dev.post([&] { order.push_back(200 + tick); }, 2);
    dev.post([&] { order.push_back(300 + tick); });
    std::vector<float> out;
    CHECK(dev.renderOfflineTicks(4, out) == 4 * 480);
    // Tick 1: the two undelayed calls in their order, then the hook; the delayed call 2 ticks later.
    CHECK(order == std::vector<int>{100, 300, 0, -1, 202, -2, -3});
    CHECK(tick == 4);
    int after = 0;
    dev.setAfterRender([&] { ++after; });
    float buf[100];
    CHECK(dev.renderOffline(buf, 100) == 100);
    CHECK(after == 1);
}

namespace {

// A font of fixed widths: every glyph 5 pixels plus 2 of spacing (like G&G's font 9), space 3.
struct TestFont final : text::TextFont {
    bool gg = false;
    int lineHeight() const override { return 10; }
    int charWidth(uint8_t c) const override { return c == ' ' ? 3 : 7; }
    int runWidth(uint8_t c) const override { return gg ? 6 : charWidth(c); }  // G&G: raw width + 1
};
struct TestCanvas final : text::TextCanvas {
    std::vector<std::pair<int, uint8_t>> glyphs;
    int colour = -1;
    int drawGlyph(int x, int, uint8_t c) override {
        glyphs.push_back({x, c});
        return c == ' ' ? 3 : 7;
    }
    void underline(int, int, int) override {}
    void setColour(int c) override { colour = c; }
};

}  // namespace

TEST_CASE("portcore: text engine dialects (TM Windows and G&G)") {
    using namespace text;
    TestFont font, ggFont;
    ggFont.gg = true;
    TestCanvas canvas;
    const std::string s = "ab\tc\xFE" "d\xFF\x04 xy";
    // TM: TAB jumps to the next tab stop (40), FE returns to x0, FF 04 reads tab stops up to the
    // next FF (none: the rest is swallowed).
    TextEngine tm(font, &canvas);
    TextExtent e = tm.render(10, 0, 200, asBytes(s));
    CHECK(canvas.glyphs == std::vector<std::pair<int, uint8_t>>{{10, 'a'}, {17, 'b'}, {50, 'c'}, {10, 'd'}});
    CHECK(e == TextExtent{47, 10});  // maxX 57 - minX 10
    // G&G: TAB and FE are glyphs, FF 04 consumes only itself; the width counts from x0.
    canvas.glyphs.clear();
    TextEngine gg(ggFont, &canvas, TextDialect::gizmos());
    e = gg.render(10, 0, 200, asBytes(s));
    REQUIRE(canvas.glyphs.size() == 9);
    CHECK(canvas.glyphs[2] == std::pair<int, uint8_t>{24, '\t'});
    CHECK(canvas.glyphs[4] == std::pair<int, uint8_t>{38, uint8_t{0xFE}});
    CHECK(canvas.glyphs[6] == std::pair<int, uint8_t>{52, ' '});
    CHECK(e == TextExtent{59, 10});  // maxX 69 - x0 10
    // An empty text: TM 1 - w, G&G 0.
    CHECK(tm.render(0, 0, 100, asBytes("")).width == -99);
    CHECK(gg.render(0, 0, 100, asBytes("")).width == 0);
    // The wrap test adds the run width of the next word: "aaaa aaa" in a box of 52 (right edge 51):
    // TM 28 + 3 + 3 * 7 = 52 > 51 wraps, G&G 28 + 3 + 3 * 6 = 49 stays on one line.
    CHECK(tm.measureRun(' ', asBytes("aaa"), 0) == 21);
    CHECK(gg.measureRun(' ', asBytes("aaa"), 0) == 18);
    CHECK(tm.render(0, 0, 52, asBytes("aaaa aaa"), {}, false, false).height == 20);
    CHECK(gg.render(0, 0, 52, asBytes("aaaa aaa"), {}, false, false).height == 10);
    // Centring (FF 05): G&G counts the centred start for the width.
    const std::string c = "\xFF\x05\xFF" "ab";
    CHECK(tm.render(0, 0, 100, asBytes(c), {}, false, false).width == 14);  // glyphs at 43 and 50
    CHECK(gg.render(0, 0, 100, asBytes(c), {}, false, false).width == 58);  // start (100 - 12) / 2 = 44, + 14
    // FF 03 sets the canvas colour (c - 1), also when only measuring.
    gg.render(0, 0, 100, asBytes("\xFF\x03\x05\xFF" "a"), {}, false, false);
    CHECK(canvas.colour == 4);
}
