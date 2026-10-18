// DAM animation engine and modal player (src/anim; re/docs/code_1098-10d8.md §8-9,
// fmt_othr_tables.md §4, PORTING_NOTES.md §4.6). Synthetic scripts need no game data; the
// shipped-script tests read TMT.DLL from TM_DATA and skip without it.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "anim/anim_bundle.h"
#include "anim/anim_engine.h"
#include "anim/anim_player.h"
#include "anim/dam_script.h"
#include "gfx/frame_buffer.h"
#include "input/input_codes.h"
#include "platform/engine.h"
#include "test_data.h"
#include "timing/clock.h"
#include "timing/deadline_timers.h"

using namespace tmport;
using namespace tmport::anim;

namespace {

// Builds a DAM script byte by byte.
struct Dam {
    std::vector<uint8_t> b{'D', 'A', 'M', 0x1A};
    Dam& u8(int v) {
        b.push_back(static_cast<uint8_t>(v));
        return *this;
    }
    Dam& i16(int v) { return u8(v & 0xFF).u8((v >> 8) & 0xFF); }
    Dam& sprite(int dx, int dy, int frame, int mode = 0) { return u8(0xFD).i16(dx).i16(dy).u8(frame).u8(mode); }
    Dam& fe() { return u8(0xFE); }
    Dam& ff() { return u8(0xFF); }
    Dam& loop(int target, int count, int counter = 0) { return u8(0xFA).i16(target).u8((counter << 7) | (count & 0x7F)); }
    Dam& sfx(int n, int pad = 0) { return u8(0xFC).u8(pad).u8(n); }
    Dam& sfxStop(int pad = 0) { return u8(0xFB).u8(pad); }
    Dam& rect(int dx, int dy, int w, int h) { return u8(0xF9).i16(dx).u8(dy).i16(w).u8(h); }
    size_t pos() const { return b.size(); }
    ScriptPtr make(int id = 1) const { return makeScript(id, b); }
};

std::shared_ptr<const gfx::PixMap> solid(int w, int h, uint8_t v) {
    auto pm = std::make_shared<gfx::PixMap>();
    pm->width = w;
    pm->height = h;
    pm->pixels.assign(static_cast<size_t>(w) * static_cast<size_t>(h), v);
    return pm;
}

// A frame buffer whose key colour is index 15 (as RRGB 900's (0,0,AA)).
gfx::FrameBuffer keyedBuffer() {
    gfx::FrameBuffer fb;
    gfx::KeyTable key{};
    key[15] = true;
    gfx::KeyTable white{};
    white[255] = true;
    fb.setKeys(key, white);
    return fb;
}

// Fills the screen and the back page with a background index (as a scene set-up would).
void background(gfx::FrameBuffer& fb, uint8_t v) {
    std::memset(fb.row(0), v, static_cast<size_t>(gfx::FrameBuffer::kWidth) * 768);
}

std::vector<int> spriteFrames(const AnimEngine& a) {
    std::vector<int> f;
    for (const auto& s : a.sprites()) f.push_back(s.frame);
    return f;
}

}  // namespace

// ------------------------------------------------------------------------------- script format

TEST_CASE("DAM: disassembler and statistics on a synthetic script") {
    Dam d;
    d.sprite(10, -5, 3, 1).sfx(16, 9).fe().u8(0x00).u8(0xF8).rect(1, 2, 3, 4).sfxStop(7).loop(4, 2, 1).fe().ff();
    const auto ops = disassemble(*d.make());
    REQUIRE(ops.size() == 10);
    CHECK(ops[0].op == kOpSprite);
    CHECK(ops[0].x == 10);
    CHECK(ops[0].y == -5);
    CHECK(ops[0].frame == 3);
    CHECK(ops[0].mode == 1);
    CHECK(ops[1].op == kOpSfx);
    CHECK(ops[1].pad == 9);
    CHECK(ops[1].sound == 16);
    CHECK(ops[3].op == 0x00);
    CHECK(ops[3].size == 1);
    CHECK(ops[4].op == 0xF8);
    CHECK(ops[5].op == kOpUpdateRect);
    CHECK(ops[5].w == 3);
    CHECK(ops[5].h == 4);
    CHECK(ops[6].op == kOpSfxStop);
    CHECK(ops[7].op == kOpLoop);
    CHECK(ops[7].counter == 1);
    CHECK(ops[7].count == 2);
    CHECK(ops[7].target == 4);
    const DamStats st = analyze(*d.make());
    CHECK(st.sprites == 1);
    CHECK(st.noOps == 2);
    CHECK(st.endsWithFeFf);
    CHECK(st.nonZeroMode);
    CHECK(st.maxFrame == 3);

    // Bad tag, truncated operand, missing FF.
    CHECK_THROWS(disassemble(*makeScript(2, std::vector<uint8_t>{'X', 'A', 'M', 0x1A, 0xFF})));
    CHECK_THROWS(disassemble(*makeScript(3, std::vector<uint8_t>{'D', 'A', 'M', 0x1A, 0xFD, 0x00})));
    CHECK_THROWS(disassemble(*makeScript(4, std::vector<uint8_t>{'D', 'A', 'M', 0x1A, 0xFE})));
    CHECK(makeScript(5, std::vector<uint8_t>{'D', 'O', 'P', 0, 0xFF})->isDop());
}

// ------------------------------------------------------------------------------------ opcodes

TEST_CASE("DAM: FD queues sprites from the frame table, FE ends the tick") {
    auto fb = keyedBuffer();
    AnimEngine a(fb);
    auto pm0 = solid(4, 4, 1), pm1 = solid(4, 4, 2);
    a.setFrame(20, pm0);
    a.setFrame(21, pm1);
    Dam d;
    d.sprite(5, 6, 0).sprite(-3, 300, 1, 2).fe().sprite(0, 0, 1).fe().ff();
    const auto s = d.make();
    const int ch = a.start(layer::kHall, 100, 50, 20, s, 7, 8, 1);
    CHECK(ch == 0);
    CHECK(a.channel(0).user0 == 7);
    CHECK(a.channel(0).user1 == 8);

    a.step();
    REQUIRE(a.sprites().size() == 2);
    const AnimSprite& s0 = a.sprites()[0];
    CHECK(s0.layer == layer::kHall);
    CHECK(s0.x == 105);
    CHECK(s0.y == 56);
    CHECK(s0.frame == 20);
    CHECK(s0.pixmap == pm0);
    CHECK(s0.mode == 0);
    CHECK(s0.sflags == 1);
    CHECK_FALSE(s0.noDirty);
    CHECK(a.sprites()[1].x == 97);
    CHECK(a.sprites()[1].y == 350);
    CHECK(a.sprites()[1].mode == 2);
    CHECK(a.sprites()[1].pixmap == pm1);

    a.step();  // the queue is rebuilt every step
    CHECK(spriteFrames(a) == std::vector<int>{21});
    CHECK(a.isRunning(ch, s));
    a.step();  // FF alone
    CHECK(a.sprites().empty());
    CHECK_FALSE(a.isRunning(ch, s));
    CHECK(a.channel(0).isFree());

    // The pixmap is captured when the FD runs: changing the slot later does not change the queue.
    a.start(0, 0, 0, 20, s);
    a.step();
    a.setFrame(20, pm1);
    CHECK(a.sprites()[0].pixmap == pm0);
}

TEST_CASE("DAM: FA loops n times, two independent counters, count 0 loops forever without ending the tick") {
    auto fb = keyedBuffer();
    AnimEngine a(fb);
    {
        // Body: frame 0 (one tick), looped 3 times, then frame 1, end.
        Dam d;
        const size_t body = d.pos();
        d.sprite(0, 0, 0).fe().loop(static_cast<int>(body), 3).sprite(0, 0, 1).fe().ff();
        const auto s = d.make();
        a.start(0, 0, 0, 0, s);
        std::vector<int> seq;
        while (a.isRunning(0, s)) {
            a.step();
            seq.push_back(a.sprites().empty() ? -1 : a.sprites()[0].frame);
        }
        // The body runs 3 times; in the 4th tick the FA falls through and frame 1 is shown in
        // that same tick; the 5th runs the END alone.
        CHECK(seq == std::vector<int>{0, 0, 0, 1, -1});
        CHECK(a.channel(0).loopCount[0] == -1);
    }
    {
        // Nested: inner loop (counter 0) x2 inside outer loop (counter 1) x2 -> 4 inner ticks.
        Dam d;
        const size_t outer = d.pos();
        const size_t inner = d.pos();
        d.sprite(0, 0, 5).fe().loop(static_cast<int>(inner), 2, 0).sprite(0, 0, 6).fe().loop(static_cast<int>(outer), 2, 1).ff();
        const auto s = d.make();
        const int ch = a.start(0, 0, 0, 0, s);
        std::vector<int> seq;
        while (a.isRunning(ch, s)) {
            a.step();
            seq.push_back(a.sprites().empty() ? -1 : a.sprites()[0].frame);
        }
        CHECK(seq == std::vector<int>{5, 5, 6, 5, 5, 6, -1});
    }
    {
        // Count 0: forever. The FA does not end the tick: the jump target's FD is in the same tick.
        Dam d;
        d.sprite(0, 0, 1).fe().sprite(0, 0, 2).fe().loop(4, 0).sprite(0, 0, 9).fe().ff();
        const auto s = d.make();
        const int ch = a.start(0, 0, 0, 0, s);
        std::vector<int> seq;
        for (int i = 0; i < 9; ++i) {
            a.step();
            seq.push_back(a.sprites().empty() ? -1 : a.sprites()[0].frame);
            CHECK(a.sprites().size() == 1);
        }
        CHECK(seq == std::vector<int>{1, 2, 1, 2, 1, 2, 1, 2, 1});
        CHECK(a.isRunning(ch, s));
        CHECK(a.channel(ch).loopCount[0] == -1);  // -1 -> 0 -> -1 each pass
        // Only the stop flag leaves the loop: the next step runs FF directly (no sprites).
        a.stop(ch, s);
        a.step();
        CHECK(a.sprites().empty());
        CHECK_FALSE(a.isRunning(ch, s));
    }
}

TEST_CASE("DAM: FC / FB sound callbacks, F9 update rects, no-op bytes") {
    auto fb = keyedBuffer();
    std::vector<int> played;
    int stops = 0;
    AnimSound snd;
    snd.play = [&](int n) { played.push_back(n); };
    snd.stop = [&] { ++stops; };
    std::vector<gfx::Rect> presented;
    AnimEngine a(fb, [&](int x, int y, int w, int h) { presented.push_back({x, y, w, h}); }, snd);

    Dam d;
    d.u8(0x00).u8(0x42).u8(0xF8).sfx(16, 0xAB).sfx(200).sfxStop(0x55).rect(10, 250, 20, 254).rect(-5, 0, 3, 3).sprite(0, 0, 0).fe().ff();
    const auto s = d.make();
    a.start(0, 100, 0x1F0, 0, s);  // y low byte 0xF0
    a.setFrame(0, solid(2, 2, 3));
    a.step();
    CHECK(played == std::vector<int>{16, 200});  // n is zero-extended; the pad is ignored
    CHECK(stops == 1);
    CHECK(a.sprites().size() == 1);  // the no-op bytes were skipped
    REQUIRE(a.updateRects().size() == 2);
    const AnimUpdateRect& r = a.updateRects()[0];
    CHECK(r.x == 110);
    CHECK(r.y == static_cast<uint8_t>(250 + 0xF0));  // only the low byte of y, 8-bit wrap
    CHECK(r.w == 22);
    CHECK(r.h == static_cast<uint8_t>(254 + 2));     // (u8)(h + 2) = 0
    CHECK(a.updateRects()[1].x == 95);

    // Flush: clipped to the inclusive clip rectangle (top, bottom, left, right), empty ones
    // skipped, list cleared. Rect 0 has h = 0; rect 1 (x 95, y 0xF0, 5x5) loses 2 columns.
    fb.setClip({0, 383, 97, 511});
    a.flushUpdateRects();
    REQUIRE(presented.size() == 1);
    CHECK(presented[0] == gfx::Rect{97, 0xF0, 3, 5});
    CHECK(a.updateRects().empty());
}

TEST_CASE("DAM: F9 presentation and clipping") {
    auto fb = keyedBuffer();
    std::vector<gfx::Rect> presented;
    AnimEngine a(fb, [&](int x, int y, int w, int h) { presented.push_back({x, y, w, h}); });
    Dam d;
    d.rect(-10, 5, 30, 8).rect(500, 250, 40, 200).fe().ff();
    a.start(0, 0, 0, 0, d.make());
    a.step();
    fb.resetClip();  // {0..383, 0..511}
    a.flushUpdateRects();
    REQUIRE(presented.size() == 2);
    CHECK(presented[0] == gfx::Rect{0, 5, 22, 10});      // x -10 w 32 -> x 0 w 22
    CHECK(presented[1] == gfx::Rect{500, 250, 12, 134}); // right/bottom clipped (inclusive 511/383)
}

TEST_CASE("DAM: the stop flag turns the next opcode into FF; onEnd is called") {
    auto fb = keyedBuffer();
    AnimEngine a(fb);
    a.setFrame(0, solid(2, 2, 3));
    Dam d;
    d.sprite(0, 0, 0).fe().sprite(0, 0, 0).fe().sprite(0, 0, 0).fe().ff();
    const auto s = d.make();
    const auto other = d.make(2);  // same bytes, different script object
    int ended = 0;
    const int ch = a.start(0, 0, 0, 0, s, 0, 0, 1, [&] { ++ended; });
    a.step();
    a.stop(ch, other);  // wrong script: ignored
    a.stop(ch + 1, s);  // wrong channel: ignored
    a.stop(-1, s);
    a.stop(16, s);
    a.step();
    CHECK(a.sprites().size() == 1);
    CHECK(a.isRunning(ch, s));
    a.stop(ch, s);
    CHECK(a.channel(ch).stop);
    CHECK(a.isRunning(ch, s));  // still running until the next step
    a.step();
    CHECK(a.sprites().empty());
    CHECK_FALSE(a.isRunning(ch, s));
    CHECK(ended == 1);
    a.stop(ch, s);  // not running: no effect
    CHECK(ended == 1);
}

TEST_CASE("DAM: channel allocation") {
    auto fb = keyedBuffer();
    AnimEngine a(fb);
    Dam d;
    d.fe().fe().fe().ff();
    std::vector<ScriptPtr> scripts;
    for (int i = 0; i < 17; ++i) scripts.push_back(d.make(100 + i));
    for (int i = 0; i < 16; ++i) CHECK(a.start(0, 0, 0, 0, scripts[static_cast<size_t>(i)]) == i);
    CHECK(a.runningChannels() == 16);
    // No free channel: 0, indistinguishable from channel 0; channel 0 keeps its script.
    CHECK(a.start(0, 0, 0, 0, scripts[16]) == 0);
    CHECK(a.isRunning(0, scripts[0]));
    CHECK_FALSE(a.isRunning(0, scripts[16]));
    CHECK_FALSE(a.isRunning(-1, scripts[0]));
    CHECK_FALSE(a.isRunning(16, scripts[0]));
    // A freed channel is reused, lowest index first.
    a.stop(5, scripts[5]);
    a.stop(3, scripts[3]);
    a.step();
    CHECK(a.runningChannels() == 14);
    CHECK(a.start(0, 0, 0, 0, scripts[16]) == 3);
    // A null script leaves the channel free but returns its index.
    CHECK(a.start(0, 0, 0, 0, nullptr) == 5);
    CHECK(a.channel(5).isFree());
    CHECK(a.start(0, 0, 0, 0, scripts[5]) == 5);
    // Anim_ResetAll frees everything without callbacks.
    a.resetAll();
    CHECK(a.runningChannels() == 0);
    CHECK(a.start(0, 0, 0, 0, scripts[1]) == 0);
}

TEST_CASE("DAM: robustness for data the shipped game never has") {
    auto fb = keyedBuffer();
    AnimEngine a(fb);
    // Runs past the end of the data: ends the channel.
    {
        const auto s = makeScript(1, std::vector<uint8_t>{'D', 'A', 'M', 0x1A, 0x00, 0x01});
        a.start(0, 0, 0, 0, s);
        a.step();
        CHECK_FALSE(a.isRunning(0, s));
    }
    // A loop without FE (the original hangs): ended after kMaxOpsPerTick opcodes.
    {
        Dam d;
        d.loop(4, 0).ff();
        const auto s = d.make();
        a.start(0, 0, 0, 0, s);
        a.step();
        CHECK_FALSE(a.isRunning(0, s));
    }
    // An empty frame slot: queued, not drawn, no erase rect.
    {
        Dam d;
        d.sprite(0, 0, 99).fe().ff();
        const auto s = d.make();
        a.start(0, 0, 0, 0, s);
        a.step();
        CHECK(a.sprites().size() == 1);
        CHECK(a.sprites()[0].pixmap == nullptr);
        a.drawSprites(0);
        CHECK(a.eraseRects().empty());
    }
    // The frame table grows on demand and starts with 100 slots.
    CHECK(a.frameSlots() == static_cast<size_t>(kFrameSlots));
    a.setFrame(150, solid(1, 1, 1));
    CHECK(a.frameSlots() == 151);
    CHECK(a.frame(150) != nullptr);
    CHECK(a.frame(151) == nullptr);
    CHECK(a.frame(-1) == nullptr);
    a.clearFrames();
    CHECK(a.frame(150) == nullptr);
}

// --------------------------------------------------------------------------- drawing pipeline

TEST_CASE("DAM: draw, erase from the back page, and the two script endings") {
    auto fb = keyedBuffer();
    background(fb, 7);
    fb.resetClip();
    fb.setDirtyEnabled(true);
    AnimEngine a(fb);
    a.setFrame(0, solid(4, 3, 40));
    a.setFrame(1, solid(4, 3, 41));

    // ... FE FF: the END runs in a tick of its own, which erases the last frame.
    {
        Dam d;
        d.sprite(0, 0, 0).fe().sprite(10, 0, 1).fe().ff();
        const auto s = d.make();
        a.start(0, 100, 50, 0, s);
        a.tick();
        CHECK(fb.at(100, 50) == 40);
        CHECK(fb.at(103, 52) == 40);
        CHECK(fb.at(104, 50) == 7);
        REQUIRE(a.eraseRects().size() == 1);
        CHECK(a.eraseRects()[0].x == 100);
        CHECK(a.eraseRects()[0].y == 50);
        CHECK(a.eraseRects()[0].w == 6);  // width + 2
        CHECK(a.eraseRects()[0].h == 5);  // height + 2
        CHECK_FALSE(fb.dirtyRects().empty());
        a.tick();
        CHECK(fb.at(100, 50) == 7);  // erased
        CHECK(fb.at(110, 50) == 41);
        a.tick();                    // END alone
        CHECK_FALSE(a.isRunning(0, s));
        CHECK(fb.at(110, 50) == 7);  // the last frame is gone
        CHECK(a.eraseRects().empty());
    }
    // ... FD FF (like 20300): the last frame is drawn in the END's tick and stays; its erase
    // rect stays queued for the next eraseSprites caller.
    {
        Dam d;
        d.sprite(0, 0, 0).fe().sprite(20, 0, 1).ff();
        const auto s = d.make();
        a.start(0, 100, 50, 0, s);
        a.tick();
        a.tick();
        CHECK_FALSE(a.isRunning(0, s));
        CHECK(fb.at(120, 50) == 41);
        CHECK(fb.at(100, 50) == 7);
        CHECK(a.eraseRects().size() == 1);
        a.eraseSprites();
        CHECK(fb.at(120, 50) == 7);
    }
}

TEST_CASE("DAM: keyed draw, mirror, stamp, opaque channels, DOP, yOff and erase clipping") {
    auto fb = keyedBuffer();
    background(fb, 7);
    fb.resetClip();
    fb.setDirtyEnabled(true);
    std::vector<gfx::Rect> presented;
    AnimEngine a(fb, [&](int x, int y, int w, int h) { presented.push_back({x, y, w, h}); });
    // 3x1 picture: 50, key (15), 52.
    auto pm = std::make_shared<gfx::PixMap>();
    pm->width = 3;
    pm->height = 1;
    pm->pixels = {50, 15, 52};
    a.setFrame(0, pm);

    // Keyed, then mirrored.
    {
        Dam d;
        d.sprite(0, 0, 0).sprite(0, 10, 0, kModeMirror).fe().ff();
        a.start(0, 10, 10, 0, d.make());
        a.tick();
        CHECK(fb.at(10, 10) == 50);
        CHECK(fb.at(11, 10) == 7);  // key colour: transparent
        CHECK(fb.at(12, 10) == 52);
        CHECK(fb.at(10, 20) == 52);  // mirrored
        CHECK(fb.at(12, 20) == 50);
        CHECK(presented.empty());
        a.resetAll();
        a.eraseSprites();
    }
    // Stamp: copied into the back page, no erase rect, survives the next erase.
    {
        Dam d;
        d.sprite(0, 0, 0, kModeStamp).fe().fe().ff();
        a.start(0, 30, 30, 0, d.make());
        a.tick();
        CHECK(a.eraseRects().empty());
        CHECK(fb.at(30, 30 + kBgPageOffset) == 50);
        CHECK(fb.at(31, 30 + kBgPageOffset) == 7);
        a.tick();
        CHECK(fb.at(30, 30) == 50);
        a.resetAll();
    }
    // Opaque channel (flags bit 1): opaque copy, presented at once when yOff == 0.
    {
        Dam d;
        d.sprite(0, 0, 0).fe().ff();
        a.start(0, 40, 40, 0, d.make(), 0, 0, kChannelFlagErase | kChannelFlagOpaque);
        a.tick();
        CHECK(fb.at(41, 40) == 15);  // the key pixel is copied
        REQUIRE(presented.size() == 1);
        CHECK(presented[0] == gfx::Rect{40, 40, 3, 1});
        CHECK(a.eraseRects().size() == 1);
        a.resetAll();
        a.eraseSprites();
        presented.clear();
    }
    // yOff: drawn into the page below; the erase rect keeps the unshifted y.
    {
        Dam d;
        d.sprite(0, 0, 0).fe().ff();
        a.start(0, 60, 60, 0, d.make());
        a.eraseSprites();
        a.step();
        fb.setFullClip();
        a.drawSprites(0, kBgPageOffset);
        CHECK(fb.at(60, 60 + kBgPageOffset) == 50);
        CHECK(fb.at(60, 60) == 7);
        REQUIRE(a.eraseRects().size() == 1);
        CHECK(a.eraseRects()[0].y == 60);
        fb.resetClip();
        a.resetAll();
        a.eraseSprites();
    }
    // Layers: drawSprites draws only the matching layer.
    {
        Dam d;
        d.sprite(0, 0, 0).fe().ff();
        a.start(layer::kHall, 80, 80, 0, d.make());
        a.eraseSprites();
        a.step();
        a.drawSprites(layer::kDefault);
        CHECK(fb.at(80, 80) == 7);
        a.drawSprites(layer::kHall);
        CHECK(fb.at(80, 80) == 50);
        a.resetAll();
        a.eraseSprites();
    }
    // DOP: no dirty rects for the sprite or its erasure; g_dirtyEnabled is restored.
    {
        Dam d;
        d.sprite(0, 0, 0).fe().ff();
        d.b[1] = 'O';
        d.b[2] = 'P';
        fb.clearDirtyRects();
        a.start(0, 90, 90, 0, d.make());
        a.tick();
        CHECK(a.sprites()[0].noDirty);
        CHECK(a.eraseRects()[0].noDirty);
        CHECK(fb.at(90, 90) == 50);
        CHECK(fb.dirtyRects().empty());
        CHECK(fb.dirtyEnabled());
        a.tick();
        CHECK(fb.at(90, 90) == 7);
        CHECK(fb.dirtyRects().empty());
        a.resetAll();
    }
    // Erase rects are clipped to the clip rectangle before restoring.
    {
        background(fb, 7);
        Dam d;
        d.sprite(0, 0, 0).fe().ff();
        a.start(0, 100, 19, 0, d.make());
        a.tick();
        fb.setPlayClip(0);  // top 20
        std::memset(fb.row(19) + 100, 99, 3);  // pretend something else drew above the play-field
        std::memset(fb.row(20) + 100, 99, 3);
        a.eraseSprites();
        CHECK(fb.at(100, 19) == 99);  // outside the clip: untouched
        CHECK(fb.at(100, 20) == 7);   // inside: restored
    }
}

// ------------------------------------------------------------------------------- modal player

namespace {

// ModalHost on a fake clock: Timer_Expired yields 1 ms when not expired (as Engine does), keys
// are delivered at given times, and each Gfx_FlushDirtyRects records the time of the tick.
struct FakeHost final : ModalHost {
    ManualClock clock{10000};
    DeadlineTimers timers{&clock};
    std::multimap<uint64_t, uint16_t> keys;  // time -> code
    std::vector<uint64_t> ticks;
    int flushes = 0;
    void timerSet(int slot, int32_t ms) override { timers.set(slot, ms); }
    bool timerExpired(int slot) override {
        if (timers.expired(slot)) return true;
        clock.advance(1);
        return timers.expired(slot);
    }
    void flushInput() override {
        ++flushes;
        // Discard what is due now.
        keys.erase(keys.begin(), keys.upper_bound(clock.nowMs()));
    }
    uint16_t getInputCode() override {
        auto it = keys.begin();
        if (it == keys.end() || it->first > clock.nowMs()) return 0;
        const uint16_t k = it->second;
        keys.erase(it);
        return k;
    }
    void flushDirtyRects() override { ticks.push_back(clock.nowMs()); }
};

ScriptPtr fiveFrames() {
    Dam d;
    for (int i = 0; i < 5; ++i) d.sprite(0, 0, i).fe();
    d.ff();
    return d.make(20000);
}

}  // namespace

TEST_CASE("modal player: first tick at once, then every 1000/fps ms; FE FF ends one tick later") {
    CHECK(modalTickMs(4) == 250);
    CHECK(modalTickMs(7) == 142);  // integer division
    CHECK(modalTickMs(3) == 333);
    CHECK(modalTickMs(0) == 1000); // guarded (the original would fault)

    auto fb = keyedBuffer();
    fb.setDirtyEnabled(false);
    AnimEngine a(fb);
    for (int i = 0; i < 5; ++i) a.setFrame(i, solid(2, 2, static_cast<uint8_t>(60 + i)));
    FakeHost host;
    ModalParams p;
    p.script = fiveFrames();
    p.fps = 7;
    const uint64_t t0 = host.clock.nowMs();
    const int r = playModal(a, host, p);
    CHECK(r == kModalFinished);
    REQUIRE(host.ticks.size() == 6);  // 5 frames + the END tick
    for (size_t i = 0; i < host.ticks.size(); ++i) CHECK(host.ticks[i] == t0 + 142 * i);
    CHECK(host.flushes == 1);
    CHECK_FALSE(fb.dirtyEnabled());  // restored
    CHECK(a.runningChannels() == 0);
    CHECK(a.eraseRects().empty());   // the END tick erased the last frame
}

TEST_CASE("modal player: Enter returns 1 and Space 2 when skippable; ignored otherwise") {
    auto fb = keyedBuffer();
    AnimEngine a(fb);
    for (int i = 0; i < 5; ++i) a.setFrame(i, solid(2, 2, 1));
    {
        FakeHost host;
        const uint64_t t0 = host.clock.nowMs();
        host.keys.emplace(t0 + 300, input::kEnter);
        ModalParams p;
        p.script = fiveFrames();
        p.fps = 4;
        CHECK(playModal(a, host, p) == kModalEnter);
        // Ticks at 0 and 250; Enter arrives at 300, so the 500 tick runs FF only.
        REQUIRE(host.ticks.size() == 3);
        CHECK(host.ticks[2] == t0 + 500);
        CHECK(a.runningChannels() == 0);
        CHECK(a.eraseRects().empty());
    }
    {
        FakeHost host;
        const uint64_t t0 = host.clock.nowMs();
        host.keys.emplace(t0 + 10, input::kSpace);
        ModalParams p;
        p.script = fiveFrames();
        CHECK(playModal(a, host, p) == kModalSpace);
        CHECK(host.ticks.size() == 2);
    }
    {
        FakeHost host;
        const uint64_t t0 = host.clock.nowMs();
        host.keys.emplace(t0 + 10, input::kSpace);
        host.keys.emplace(t0 + 20, input::kEnter);
        ModalParams p;
        p.script = fiveFrames();
        p.skippable = false;
        CHECK(playModal(a, host, p) == kModalFinished);
        CHECK(host.ticks.size() == 6);
    }
    {
        // Keys pending when the player starts are flushed (Input_Flush).
        FakeHost host;
        host.keys.emplace(host.clock.nowMs(), input::kEnter);
        ModalParams p;
        p.script = fiveFrames();
        CHECK(playModal(a, host, p) == kModalFinished);
    }
}

TEST_CASE("modal player: Esc calls onEsc; the animation continues on No") {
    auto fb = keyedBuffer();
    AnimEngine a(fb);
    for (int i = 0; i < 5; ++i) a.setFrame(i, solid(2, 2, 1));
    {
        FakeHost host;
        const uint64_t t0 = host.clock.nowMs();
        host.keys.emplace(t0 + 100, input::kEsc);
        host.keys.emplace(t0 + 600, input::kEsc);
        int asked = 0;
        ModalParams p;
        p.script = fiveFrames();
        p.onEsc = [&] {
            ++asked;
            return false;
        };
        CHECK(playModal(a, host, p) == kModalFinished);
        CHECK(asked == 2);
        CHECK(host.ticks.size() == 6);
    }
    {
        FakeHost host;
        const uint64_t t0 = host.clock.nowMs();
        host.keys.emplace(t0 + 100, input::kEsc);
        ModalParams p;
        p.script = fiveFrames();
        p.onEsc = [] { return true; };
        CHECK(playModal(a, host, p) == kModalQuit);
        CHECK(host.ticks.size() == 1);
        CHECK(a.runningChannels() == 0);
    }
    {
        // No handler: Esc is ignored.
        FakeHost host;
        host.keys.emplace(host.clock.nowMs() + 100, input::kEsc);
        ModalParams p;
        p.script = fiveFrames();
        CHECK(playModal(a, host, p) == kModalFinished);
    }
}

TEST_CASE("modal player: no free channel plays nothing and returns at once") {
    auto fb = keyedBuffer();
    AnimEngine a(fb);
    Dam d;
    d.fe().ff();
    std::vector<ScriptPtr> keep;
    for (int i = 0; i < kChannelCount; ++i) {
        keep.push_back(d.make(i));
        a.start(0, 0, 0, 0, keep.back());
    }
    FakeHost host;
    ModalParams p;
    p.script = fiveFrames();
    CHECK(playModal(a, host, p) == kModalFinished);
    CHECK(host.ticks.size() == 1);  // one pass: channel 0 does not run this script
}

// ---------------------------------------------------------------------------- shipped scripts

TEST_CASE("DAM: every script in TMT.DLL parses (20000-20308)") {
    TM_REQUIRE_GAME_DATA();
    const ResourceManager& res = *g->res;
    int scripts = 0, fd = 0, fe = 0, ff = 0, fa = 0, fc = 0, other = 0, maxPerTick = 0, maxFrame = 0;
    std::vector<int> fdff;
    for (int id : res.ids(ResType::OTHR, DllId::TMT)) {
        if (id < 20000 || id > 20399) continue;
        const auto s = makeScript(id, *res.loadRaw(ResType::OTHR, id));
        if (!s->hasDamTag()) {
            CHECK((id >= 20105 && id <= 20108));  // the intro story pages
            continue;
        }
        ++scripts;
        DamStats st;
        REQUIRE_NOTHROW(st = analyze(*s));
        fd += st.sprites;
        fe += st.endFrames;
        ff += st.ends;
        fa += st.loops;
        fc += st.sounds;
        other += st.sfxStops + st.updateRects + st.noOps;
        maxPerTick = std::max(maxPerTick, st.maxSpritesPerTick);
        maxFrame = std::max(maxFrame, st.maxFrame);
        CHECK_FALSE(st.nonZeroMode);
        CHECK_FALSE(s->isDop());
        if (!st.endsWithFeFf) fdff.push_back(id);
        CHECK(st.loopsForever == (id <= 20050 || id == 20100));
    }
    CHECK(scripts == 63);
    CHECK(fd == 1027);
    CHECK(fe == 589);
    CHECK(ff == 63);
    CHECK(fa == 52);
    CHECK(fc == 32);
    CHECK(other == 0);
    CHECK(maxPerTick == 6);
    CHECK(maxFrame == 80);
    CHECK(fdff == std::vector<int>{20300});
}

TEST_CASE("DAM: shipped scripts run to their end with the catalogue's tick counts and sounds") {
    TM_REQUIRE_GAME_DATA();
    const ResourceManager& res = *g->res;
    gfx::FrameBuffer fb;
    std::vector<std::pair<int, int>> soundTicks;
    int tick = 0;
    AnimSound snd;
    snd.play = [&](int n) { soundTicks.emplace_back(tick, kSfxBase + n); };
    AnimEngine a(fb, {}, snd);
    // fmt_othr_tables.md §4.5 ("Ticks" = steps until FF; sound ticks there count from 0).
    const std::map<int, int> ticks = {{20101, 20}, {20200, 12}, {20201, 18}, {20204, 7}, {20205, 9},
                                      {20300, 24}, {20302, 33}, {20301, 21}, {20305, 49}, {20308, 12}};
    for (const auto& [id, n] : ticks) {
        const auto s = loadScript(res, id);
        const int ch = a.start(0, 0, 0, 0, s);
        soundTicks.clear();
        tick = 0;
        while (a.isRunning(ch, s) && tick < 1000) {
            a.step();
            ++tick;
        }
        CHECK_MESSAGE(tick == n, "DAM ", id);
        if (id == 20300)
            CHECK(soundTicks == std::vector<std::pair<int, int>>{{2, 7016}, {5, 7018}, {15, 7017}, {18, 7014}, {22, 7010}});
        if (id == 20305)
            CHECK(soundTicks == std::vector<std::pair<int, int>>{
                                    {1, 7011}, {3, 7014}, {5, 7009}, {8, 7020}, {21, 7002}, {30, 7040}, {32, 7021}});
    }
}

TEST_CASE("DAM: Anim_LoadBundle fills the frame table; every FD finds a frame") {
    TM_REQUIRE_GAME_DATA();
    const ResourceManager& res = *g->res;
    gfx::FrameBuffer fb;
    AnimEngine a(fb);
    // Title: BNDL 400 at 0 (10 frames), BNDL 401 after it (Title_LoadAnims 1138:0000).
    const LoadedBundle b400 = loadBundle(res, a, 400, 0);
    CHECK(b400.script->id == 20100);
    CHECK(b400.frameCount == 10);
    const LoadedBundle b401 = loadBundle(res, a, 401, b400.frameCount);
    CHECK(b401.script->id == 20101);
    CHECK(b401.frameCount == 19);
    for (int i = 0; i < 29; ++i) CHECK(a.frame(i) != nullptr);
    CHECK(a.frame(29) == nullptr);
    CHECK(a.frame(0) == res.pixmap(2101));

    // Animation bundles: run every script over its own frames; no sprite may miss its frame.
    const std::vector<std::pair<int, int>> bundles = {{400, 0}, {401, 0}, {402, 0}, {410, 0},
                                                      {412, 0}, {323, 0}, {324, 0}, {325, 0}};
    for (const auto& [bndl, base] : bundles) {
        a.clearFrames();
        const LoadedBundle b = loadBundle(res, a, bndl, base);
        a.setFrame(kDeliveredTreasureSlot, bundleMember(res, 330, 0));
        std::vector<ScriptPtr> scripts = {b.script};
        if (bndl == 410) scripts.push_back(loadScript(res, 20201));
        if (bndl == 412) scripts.push_back(loadScript(res, 20205));
        if (bndl == 323) {
            scripts.push_back(loadScript(res, 20302));
            scripts.push_back(loadScript(res, 20301));
        }
        for (const auto& s : scripts) {
            a.resetAll();
            const int ch = a.start(0, 0, 20, base, s);
            int t = 0, missing = 0;
            while (a.isRunning(ch, s) && t++ < 200) {
                a.step();
                for (const auto& sp : a.sprites()) missing += sp.pixmap ? 0 : 1;
            }
            CHECK_MESSAGE(missing == 0, "DAM ", s->id, " over BNDL ", bndl);
        }
    }
    // Treasure-hall picture bundles 330+t: all members are frames (TreasureHall_LoadRoom).
    a.clearFrames();
    for (int t = 0; t < 51; ++t) {
        const int n = loadFrames(res, a, 330 + t, kHallFrameBase);
        const auto s = loadScript(res, 20000 + t);
        const DamStats st = analyze(*s);
        CHECK_MESSAGE(st.maxFrame < n, "treasure ", t);
    }
    CHECK_THROWS(loadBundle(res, a, 9999, 0));
}

TEST_CASE("DAM: title tick 1 composes PMAP 2101 keyed over PMAP 2100") {
    TM_REQUIRE_GAME_DATA();
    const ResourceManager& res = *g->res;
    const gfx::Palette pal = g->makePalette(gfx::ColorMode::Colour256);
    gfx::FrameBuffer fb;
    fb.setPalette(pal);
    fb.resetClip();
    auto bg = res.pixmap(2100);
    REQUIRE(bg);
    fb.drawOpaque(0, 20, *bg);
    fb.copyRect(0, 0, 512, 384, 0, 384);
    gfx::FrameBuffer expect = fb;
    expect.drawKeyed(338, 20, *res.pixmap(2101));

    AnimEngine a(fb);
    const LoadedBundle b = loadBundle(res, a, 400, 0);
    const int ch = a.start(0, 0, 20, 0, b.script);  // Title_Run 1138:00ee
    a.tick();
    CHECK(std::memcmp(fb.row(0), expect.row(0), 512 * 384) == 0);
    // Ten ticks later the loop is back at frame 0 (FA 04 00 00 forever); still running.
    for (int i = 0; i < 10; ++i) a.tick();
    CHECK(a.sprites()[0].frame == 0);
    CHECK(a.isRunning(ch, b.script));
    CHECK(std::memcmp(fb.row(0), expect.row(0), 512 * 384) == 0);
}

TEST_CASE("modal player through Engine: celebration endings, timing and skip (shipped data)") {
    TM_REQUIRE_GAME_DATA();
    ManualClock clock(50000);
    EngineConfig cfg;
    cfg.clock = &clock;
    Engine engine(*g, gfx::ColorMode::Colour256, cfg);  // headless
    gfx::FrameBuffer& fb = engine.frame();
    const ResourceManager& res = *g->res;

    // Climb_Celebrate (10f0:0000): PMAP 2299 at (0,20), black line at y 287, play-field copied
    // to the back page, presented; BNDL 323 at 0; slot 80 = the delivered treasure.
    auto setUp = [&] {
        fb.setPlayClip(0);
        fb.setDirtyEnabled(false);
        fb.drawOpaqueOrFlippedWhiteKey(0, 20, *res.pixmap(2299), false);
        fb.hline(0, 0x11F, 0x200);
        fb.copyRect(0, 0x14, 0x200, 0x10B, 0, 0x194);
        engine.presenter().blitToWindow(fb, 0, 0x14, 0x200, 0x10C);
    };
    setUp();
    const auto before = engine.presenter().gameAreaRgba();
    AnimEngine anim(fb, enginePresent(engine));
    const LoadedBundle b = loadBundle(res, anim, 323, 0);
    anim.setFrame(kDeliveredTreasureSlot, bundleMember(res, 330, 0));
    REQUIRE(b.script->id == 20300);

    // 20300 (ends FD..FF): 24 ticks at 250 ms; its last frame stays on screen and presented,
    // with its 4 erase rects still queued.
    ModalParams p;
    p.script = b.script;
    p.x = 0;
    p.y = 20;
    p.fps = 4;
    p.onEsc = [] { return false; };
    uint64_t t0 = clock.nowMs();
    CHECK(playModal(anim, engine, p) == kModalFinished);
    CHECK(clock.nowMs() - t0 == 23 * 250);
    CHECK(anim.eraseRects().size() == 4);
    CHECK(engine.presenter().gameAreaRgba() != before);
    CHECK_FALSE(fb.dirtyEnabled());  // restored

    // 20302 (ends FE FF): the first tick erases 20300's leftovers; the END tick erases its own
    // last frame, so the screen and the window are back to the background.
    p.script = loadScript(res, 20302);
    t0 = clock.nowMs();
    CHECK(playModal(anim, engine, p) == kModalFinished);
    CHECK(clock.nowMs() - t0 == 32 * 250);
    CHECK(anim.eraseRects().empty());
    for (int y = 20; y < 287; ++y) CHECK(std::memcmp(fb.row(y), fb.row(y + kBgPageOffset), 512) == 0);
    CHECK(engine.presenter().gameAreaRgba() == before);

    // 20301 skipped with Space after 1 s: returns 2, the next tick runs FF and erases.
    p.script = loadScript(res, 20301);
    t0 = clock.nowMs();
    clock.onSleep = [&](uint64_t now) {
        if (now == t0 + 1000) engine.postKey(input::kSpace);
    };
    CHECK(playModal(anim, engine, p) == kModalSpace);
    clock.onSleep = nullptr;
    CHECK(clock.nowMs() - t0 == 5 * 250);  // ticks at 0..1000, Space at 1000 is read after the 1000 tick; FF at 1250
    CHECK(engine.presenter().gameAreaRgba() == before);
    CHECK(anim.runningChannels() == 0);
}
