// The G&G input models: Kbd_ISR and the key ring (1454, 198d), the BIOS key words through the
// shared BIOS model with synthesised typematic, the INT 33h event handler (17a0) and the pointer
// limits (17ab), the click zones (17ab, 198d:0008) and Input_GetCode with its global keys (198d)
// (re/gg/docs/code_runtime.md §6-§9). Data-free except the checks against ssg.exe at the end.

#include <doctest/doctest.h>

#include <SDL3/SDL_scancode.h>

#include <cstring>
#include <format>
#include <initializer_list>
#include <utility>
#include <string>
#include <vector>

#include "gg/data/dgroup_offsets.h"
#include "gg/input/input_codes.h"
#include "gg/input/keyboard.h"
#include "gg/input/mouse.h"
#include "gg/input/zones.h"
#include "gg_test_data.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

constexpr uint64_t kMs = 1'000'000;

dos::PcKey key(const char* name) {
    const auto k = dos::pcKeyByName(name);
    REQUIRE(k.has_value());
    return *k;
}

// A keyboard at a fixed g_ticks, keys pressed and released at given milliseconds.
struct Kbd {
    GgKeyboard kb;
    uint32_t ticks = 0;
    Kbd() { kb.setTickSource([this] { return ticks; }); }
    void down(const char* name, uint64_t ms) {
        kb.keyDown(key(name), ms * kMs);
        kb.deliverUntil(ms * kMs);
    }
    void up(const char* name, uint64_t ms) {
        kb.keyUp(key(name), ms * kMs);
        kb.deliverUntil(ms * kMs);
    }
    void tap(const char* name, uint64_t ms) {
        down(name, ms);
        up(name, ms + 50);
    }
};

// A software cursor stand-in that owns g_mouseX/Y and records what the handler asks of it, as the
// graphics module's Video::moveCursor does: restore under at the old place, move, flush.
class FakeScreen final : public MouseScreen {
public:
    bool drawingNow = false;
    int16_t hx = 0, hy = 0;
    int16_t mx = 0, my = 0;
    std::vector<std::string> calls;
    bool drawing() const override { return drawingNow; }
    int16_t hotX() const override { return hx; }
    int16_t hotY() const override { return hy; }
    int16_t cursorX() const override { return mx; }
    int16_t cursorY() const override { return my; }
    void setCursorPosition(int16_t x, int16_t y) override {
        mx = x;
        my = y;
    }
    void moveCursor(int16_t x, int16_t y) override {
        calls.push_back(std::format("restore {},{}", mx, my));
        mx = x;
        my = y;
        calls.push_back(std::format("flush {},{}", mx, my));
    }
};

}  // namespace

// ------------------------------------------------------------------------------------- Kbd_ISR

TEST_CASE("gg Kbd_ISR: held bits, pass-on and the ring outside play mode") {
    KbdIsr isr;
    // Make Left: held bit 1, passed on, one ring record stamped with g_ticks.
    CHECK(isr.onScanByte(0x4B, 7));
    CHECK(isr.heldKeys == held::kLeft);
    CHECK(isr.ring.pending() == 1);
    CHECK(isr.ring.record(0).kind == 3);
    CHECK(isr.ring.record(0).tick == 7);
    // Break Left: bit cleared, passed on, no record.
    CHECK(isr.onScanByte(0xCB, 8));
    CHECK(isr.heldKeys == 0);
    CHECK(isr.ring.pending() == 1);
    // The six other held keys.
    const std::pair<uint8_t, uint16_t> keys[] = {{uint8_t{0x4D}, held::kRight}, {uint8_t{0x48}, held::kUp},
                                                 {uint8_t{0x50}, held::kDown}, {uint8_t{0x39}, held::kSpace},
                                                 {uint8_t{0x1D}, held::kCtrl}};
    for (const auto& [scan, bit] : keys) {
        CHECK(isr.onScanByte(scan, 9));
        CHECK((isr.heldKeys & bit) == bit);
        CHECK(isr.onScanByte(static_cast<uint8_t>(scan | 0x80), 9));
        CHECK((isr.heldKeys & bit) == 0);
    }
    // A key without a handler: passed on, a record for the make only.
    const int before = isr.ring.pending();
    CHECK(isr.onScanByte(0x1E, 10));
    CHECK(isr.onScanByte(0x9E, 10));
    CHECK(isr.ring.pending() == before + 1);
    // Scan 0 matches only the table's trailing 0: no handler, passed on.
    CHECK(isr.onScanByte(0x00, 10));
    CHECK(isr.heldKeys == 0);
}

TEST_CASE("gg Kbd_ISR: play mode swallows the arrows and Space, never Ctrl") {
    KbdIsr isr;
    isr.playMode = 1;
    for (const uint8_t scan : std::initializer_list<uint8_t>{0x4B, 0x4D, 0x48, 0x50, 0x39}) {
        CHECK_FALSE(isr.onScanByte(scan, 1));  // held bit set, swallowed
        CHECK(isr.heldKeys != 0);
        CHECK_FALSE(isr.onScanByte(static_cast<uint8_t>(scan | 0x80), 1));
        CHECK(isr.heldKeys == 0);
    }
    CHECK(isr.ring.empty());
    // Ctrl: result 1, always passed on (Ctrl+S, Ctrl+R in the warehouse).
    CHECK(isr.onScanByte(0x1D, 1));
    CHECK(isr.ctrlHeld == 1);
    CHECK(isr.ring.pending() == 1);
    // Other keys pass in play mode too.
    CHECK(isr.onScanByte(0x1F, 1));
    CHECK(isr.ring.pending() == 2);
    CHECK(isr.swallowed == 10);
}

TEST_CASE("gg Kbd_ISR: C is swallowed while Ctrl is held; E0 and E1 always") {
    KbdIsr isr;
    CHECK(isr.onScanByte(0x2E, 1));   // C alone
    CHECK(isr.onScanByte(0xAE, 1));
    CHECK(isr.onScanByte(0x1D, 1));   // Ctrl
    CHECK_FALSE(isr.onScanByte(0x2E, 2));  // Ctrl+C: make and break swallowed, no record
    CHECK_FALSE(isr.onScanByte(0xAE, 2));
    CHECK(isr.ring.pending() == 2);
    CHECK(isr.onScanByte(0x9D, 3));   // Ctrl up
    CHECK(isr.ctrlHeld == 0);
    CHECK(isr.onScanByte(0x2E, 3));
    CHECK_FALSE(isr.onScanByte(0xE0, 3));
    CHECK_FALSE(isr.onScanByte(0xE1, 3));
    CHECK(isr.heldKeys == 0);
}

TEST_CASE("gg key ring: 16 records, KbdRing_Pop, silent overrun") {
    KeyRing ring;
    CHECK(ring.pop() == nullptr);
    for (uint32_t i = 0; i < 15; ++i) ring.push(100 + i);
    CHECK(ring.pending() == 15);
    const InputRecord* r = ring.pop();
    REQUIRE(r != nullptr);
    CHECK(r->tick == 100);
    CHECK(ring.pending() == 14);
    // Two more: the 16th unread record makes write == read and the ring looks empty.
    ring.push(200);
    ring.push(201);
    CHECK(ring.empty());
    CHECK(ring.overruns() == 1);
    ring.push(202);
    CHECK(ring.pending() == 1);
    CHECK(ring.pop()->tick == 202);
}

// ---------------------------------------------------------------------------------- GgKeyboard

TEST_CASE("gg keyboard: BIOS key words through Kbd_ISR (grey keys as keypad keys)") {
    Kbd k;
    k.tap("enter", 0);
    k.tap("esc", 100);
    k.tap("up", 200);    // grey Up: E0 swallowed, 0x48 reaches the BIOS
    k.tap("kp8", 300);   // keypad 8 (Num Lock off)
    k.tap("f1", 400);
    CHECK(k.kb.biosGetKey() == 0x0D);
    CHECK(k.kb.biosGetKey() == 0x1B);
    CHECK(k.kb.biosGetKey() == 0x148);
    CHECK(k.kb.biosGetKey() == 0x148);
    CHECK(k.kb.biosGetKey() == 0x13B);
    CHECK(k.kb.biosGetKey() == 0);
    CHECK(k.kb.isr().swallowed == 2);  // the two E0 bytes of grey Up
    // Ctrl+S, Alt+H, Alt+F, Alt+O.
    k.down("lctrl", 500);
    k.tap("s", 510);
    k.up("lctrl", 600);
    k.down("lalt", 700);
    k.tap("h", 710);
    k.tap("f", 720);
    k.tap("o", 730);
    k.up("lalt", 800);
    CHECK(k.kb.biosGetKey() == 0x13);
    CHECK(k.kb.biosGetKey() == 0x123);
    CHECK(k.kb.biosGetKey() == 0x121);
    CHECK(k.kb.biosGetKey() == 0x118);
    // 5 + 1 (Ctrl) + 1 (S) + 1 (Alt) + 3 makes reached the BIOS, one ring record each.
    CHECK(k.kb.ring().pending() == 11);
}

TEST_CASE("gg keyboard: synthesised typematic (500 ms, then 33 ms) through the swallow rules") {
    SUBCASE("outside play mode every repeat is a BIOS key and a ring record") {
        Kbd k;
        k.down("right", 0);
        k.kb.deliverUntil(600 * kMs);  // repeats at 500, 533, 566, 599 ms
        CHECK(k.kb.repeatsGenerated() == 4);
        CHECK(k.kb.ring().pending() == 5);
        CHECK(k.kb.bios().size() == 5);
        CHECK(k.kb.heldKeys() == held::kRight);
        k.up("right", 610);
        CHECK(k.kb.heldKeys() == 0);
    }
    SUBCASE("in play mode the repeats only keep the held bit") {
        Kbd k;
        k.kb.setPlayMode(1);
        k.down("right", 0);
        k.kb.deliverUntil(600 * kMs);
        CHECK(k.kb.repeatsGenerated() == 4);
        CHECK(k.kb.ring().empty());
        CHECK(k.kb.bios().size() == 0);
        CHECK(k.kb.heldKeys() == held::kRight);
    }
}

TEST_CASE("gg keyboard: Kbd_PopQueuedKey and the LZH refill's drain") {
    Kbd k;
    k.tap("a", 0);
    k.down("lshift", 100);  // a ring record without a BIOS key of its own
    k.tap("b", 200);        // 'B'
    CHECK(k.kb.ring().pending() == 3);
    CHECK(k.kb.bios().size() == 2);
    // A record only allows one BIOS key to be taken: the Shift record takes 'B', and the record of
    // 'b' finds no key and stops the drain (it is popped all the same).
    CHECK(k.kb.popQueuedKey() == 'a');
    CHECK(k.kb.popQueuedKey() == 'B');
    CHECK(k.kb.popQueuedKey() == 0);
    CHECK(k.kb.ring().empty());
    CHECK(k.kb.popQueuedKey() == 0);  // empty ring
    // The refill's drain: keys typed while a resource decompresses are thrown away.
    k.tap("c", 300);
    k.tap("d", 400);
    k.up("lshift", 500);
    k.tap("e", 600);
    CHECK(k.kb.drainQueuedKeys() == 3);
    CHECK(k.kb.ring().empty());
    CHECK(k.kb.bios().size() == 0);
}

// --------------------------------------------------------------------------------------- mouse

TEST_CASE("gg mouse: mode 13h driver, x halved, moves through Mouse_MoveCursor") {
    GgMouse m;
    FakeScreen s;
    m.setScreen(&s);
    CHECK(m.driver().x() == 320);  // the driver's reset position
    CHECK(m.driver().y() == 100);
    m.readDriverPosition();
    CHECK(m.x() == 160);
    CHECK(m.y() == 100);
    m.installHandler();
    m.driver().setLogicalPosition(101, 51);
    CHECK(m.driver().x() == 202);
    CHECK(m.x() == 101);
    CHECK(m.y() == 51);
    // Mouse_RestoreUnder at the old position, then the new one, then Dirty_Flush.
    REQUIRE(s.calls.size() == 2);
    CHECK(s.calls[0] == "restore 160,100");
    CHECK(s.calls[1] == "flush 101,51");
    // Clamped to the driver's range.
    m.driver().setLogicalPosition(400, 300);
    CHECK(m.x() == 319);
    CHECK(m.y() == 199);
}

TEST_CASE("gg mouse: press and release latches with the cursor's hot spot") {
    GgMouse m;
    FakeScreen s;
    s.hx = 36;  // the wire spool of the electric puzzle (REFERENCE_CAPTURES §11)
    s.hy = 8;
    m.setScreen(&s);
    uint32_t ticks = 42;
    m.setTickSource([&ticks] { return ticks; });
    m.installHandler();
    m.driver().setLogicalPosition(138, 120);
    m.driver().setButton(dos::Int33Mouse::kLeft, true);
    CHECK(m.press().kind == 1);
    CHECK(m.press().first == 1);
    CHECK(m.press().x == 174);
    CHECK(m.press().y == 128);
    CHECK(m.release().kind == 0);
    // A drag with the left button alone moves the press latch.
    m.driver().setLogicalPosition(140, 121);
    CHECK(m.press().x == 176);
    CHECK(m.press().y == 129);
    ticks = 50;
    m.driver().setButton(dos::Int33Mouse::kLeft, false);
    CHECK(m.press().kind == 0);
    CHECK(m.release().kind == 2);
    CHECK(m.release().x == 176);
    CHECK(m.release().y == 129);
    CHECK(m.release().tick == 50);
    // The right button only changes g_mouseButtons.
    m.driver().setButton(dos::Int33Mouse::kRight, true);
    CHECK(m.buttons() == dos::Int33Mouse::kRight);
    CHECK(m.press().kind == 0);
    CHECK(m.release().kind == 2);
}

TEST_CASE("gg mouse: moves are dropped while a frame is drawing (optional)") {
    GgMouse m;
    FakeScreen s;
    m.setScreen(&s);
    m.installHandler();
    m.driver().setLogicalPosition(10, 10);
    s.drawingNow = true;
    m.driver().setLogicalPosition(20, 20);
    CHECK(m.x() == 10);
    CHECK(m.movesDropped() == 1);
    // A press still carries its own position (the driver's, not g_mouseX).
    m.driver().setButton(dos::Int33Mouse::kLeft, true);
    CHECK(m.press().x == 20);
    CHECK(m.x() == 10);
    m.setOptions({.dropMovesWhileDrawing = false});
    m.driver().setLogicalPosition(30, 30);
    CHECK(m.x() == 30);
}

TEST_CASE("gg mouse: one event that moved and pressed adds the hot spot twice") {
    GgMouse m;
    FakeScreen s;
    s.hx = 5;
    s.hy = 3;
    m.setScreen(&s);
    // 17a0:0029 and 17a0:004e both add the hot spot to CX / DX in place.
    m.onEvent({static_cast<uint16_t>(dos::Int33Mouse::kEventMove | dos::Int33Mouse::kEventLeftDown), 1, 200, 50});
    CHECK(m.x() == 100);
    CHECK(m.press().x == 110);
    CHECK(m.press().y == 56);
    // Without the left button alone (both buttons), the move does not touch the latch.
    m.onEvent({static_cast<uint16_t>(dos::Int33Mouse::kEventMove | dos::Int33Mouse::kEventLeftDown), 3, 200, 50});
    CHECK(m.press().x == 105);
    CHECK(m.press().y == 53);
}

TEST_CASE("gg mouse: Mouse_SetBounds and the puzzles' warp") {
    GgMouse m;
    m.installHandler();
    m.readDriverPosition();
    m.setPosition(318, 199);
    // The full screen with the 9x9 arrow: x up to 310 (driver 620), y up to 190 (as captured).
    m.setBounds(0, 0, 319, 199, 9, 9);
    CHECK(m.maxX() == 310);
    CHECK(m.maxY() == 190);
    CHECK(m.x() == 310);
    CHECK(m.y() == 190);
    CHECK(m.driver().maxX() == 620);
    CHECK(m.driver().maxY() == 190);
    m.driver().setLogicalPosition(319, 199);
    CHECK(m.x() == 310);
    // Hud_SelectPart's bounds (0, 165, 319, 34) with a 49-wide cursor: driver x up to 540.
    m.setBounds(0, 165, 319, 34, 49, 9);
    CHECK(m.driver().maxX() == 540);
    CHECK(m.driver().minY() == 165);
    m.warp(50, 170);
    CHECK(m.x() == 50);
    CHECK(m.y() == 170);
    CHECK(m.driver().x() == 100);
}

TEST_CASE("gg mouse: no handler, no latches") {
    GgMouse m;
    m.driver().setButton(dos::Int33Mouse::kLeft, true);
    CHECK(m.press().kind == 0);
    m.installHandler();
    m.driver().setButton(dos::Int33Mouse::kLeft, false);
    CHECK(m.release().kind == 2);
    m.removeHandler();
    m.driver().setButton(dos::Int33Mouse::kLeft, true);
    CHECK(m.press().kind == 0);
}

// --------------------------------------------------------------------------------------- zones

TEST_CASE("gg zones: rectangles are half-open, circles exclude their rim") {
    const Zone r = Zone::rect(10, 20, 30, 40, 0, 1, 2);
    CHECK(r.contains(10, 20));
    CHECK(r.contains(39, 59));
    CHECK_FALSE(r.contains(40, 59));
    CHECK_FALSE(r.contains(39, 60));
    CHECK_FALSE(r.contains(9, 20));
    CHECK_FALSE(r.contains(10, 19));
    const Zone c = Zone::circle(100, 100, 10, 0, 1, 2);
    CHECK(c.contains(100, 100));
    CHECK(c.contains(109, 100));
    CHECK_FALSE(c.contains(110, 100));  // 100 is not < 100
    CHECK(c.contains(107, 107));        // 98
    CHECK_FALSE(c.contains(108, 106));  // 100
    CHECK_FALSE(c.contains(90, 100));
    CHECK(c.contains(91, 100));
    // A radius above 181: the 16-bit IMUL low word is negative, nothing is ever inside.
    const Zone big = Zone::circle(100, 100, 200, 0, 1, 2);
    CHECK_FALSE(big.contains(100, 100));
    const Zone r181 = Zone::circle(100, 100, 181, 0, 1, 2);
    CHECK(r181.contains(100, 100));
}

TEST_CASE("gg zones: Zones_HitTest priorities, ties and local before global") {
    ClickZones zs;
    Zone a = Zone::rect(0, 0, 100, 100, 1, 0x10, 0x11);
    Zone b = Zone::rect(50, 50, 100, 100, 2, 0x20, 0x21);
    Zone c = Zone::rect(0, 0, 320, 200, -1, 0x30, 0x31);  // negative priority: never chosen
    zs.addLocal(a);
    zs.addLocal(b);
    zs.addLocal(c);
    CHECK(ClickZones::hitTest(10, 10, zs.local()) == &a);
    CHECK(ClickZones::hitTest(60, 60, zs.local()) == &b);  // higher priority
    CHECK(ClickZones::hitTest(200, 180, zs.local()) == nullptr);
    // A tie keeps the zone found first: the one added last.
    Zone d = Zone::rect(0, 0, 100, 100, 1, 0x40, 0x41);
    zs.addLocal(d);
    CHECK(ClickZones::hitTest(10, 10, zs.local()) == &d);
    // Input_HitZones: a local zone beats any global zone.
    Zone g = Zone::rect(0, 0, 320, 200, 100, 0x50, 0x51);
    zs.addGlobal(g);
    InputRecord press{1, 0, 10, 10, 0};
    InputRecord release{0, 0, 10, 10, 0};
    CHECK(zs.hit(press) == 0x40);
    CHECK(zs.hit(release) == 0x41);
    press.x = 300;
    press.y = 190;
    CHECK(zs.hit(press) == 0x50);
    // Nothing hit: 0.
    zs.removeGlobal(&g);
    CHECK(zs.hit(press) == 0);
}

TEST_CASE("gg zones: add, remove, groups, clear, push and pop") {
    ClickZones zs;
    std::vector<Zone> arr = {Zone::rect(0, 0, 1, 1, 0, 1, 1), Zone::rect(0, 0, 1, 1, 0, 2, 2), Zone::rect(0, 0, 1, 1, 0, 3, 3)};
    arr[1].group = 7;
    zs.addLocalArray(arr);
    auto list = ClickZones::listOf(zs.local());
    REQUIRE(list.size() == 3);
    CHECK(list[0] == &arr[2]);  // the last one ends at the head
    CHECK(list[2] == &arr[0]);
    // Zones_RemoveLocalGroup.
    zs.removeLocalGroup(7);
    list = ClickZones::listOf(zs.local());
    REQUIRE(list.size() == 2);
    CHECK(arr[1].next == nullptr);
    // Zones_RemoveLocal clears the removed zone's next.
    zs.removeLocal(&arr[2]);
    CHECK(zs.local() == &arr[0]);
    CHECK(arr[2].next == nullptr);
    zs.removeLocal(nullptr);
    zs.removeLocal(&arr[2]);  // not linked: nothing
    // Global: unlinked, its next kept.
    Zone g1 = Zone::rect(0, 0, 1, 1, 0, 9, 9), g2 = Zone::rect(0, 0, 1, 1, 0, 8, 8);
    zs.addGlobal(g1);
    zs.addGlobal(g2);
    zs.removeGlobal(&g2);
    CHECK(zs.global() == &g1);
    CHECK(g2.next == &g1);
    // Push / pop.
    zs.addLocal(arr[1]);
    zs.push();
    CHECK(zs.local() == nullptr);
    CHECK(zs.depth() == 1);
    Zone inner = Zone::rect(0, 0, 320, 200, 0, 0x99, 0x9A);
    zs.addLocal(inner);
    zs.push();
    CHECK(zs.local() == nullptr);
    zs.pop();
    CHECK(zs.local() == &inner);
    zs.pop();
    CHECK(zs.local() == &arr[1]);
    CHECK(zs.depth() == 0);
    // Clear: every next cleared.
    zs.clearLocal();
    CHECK(zs.local() == nullptr);
    CHECK(arr[1].next == nullptr);
    CHECK(arr[0].next == nullptr);
}

TEST_CASE("gg zones: records, Zone_Init and a looping list") {
    // A 22-byte record: shape 0, (0, 0, 320, 200), priority 5, press 0, release 0x0D, group 3.
    const uint8_t rec[22] = {0, 0, 0, 0, 0, 0, 0x40, 1, 200, 0, 5, 0, 0x12, 0x34, 0x56, 0x78, 0, 0, 0x0D, 0, 3, 0};
    const Zone z = Zone::fromRecord(rec);
    CHECK(z.shape == 0);
    CHECK(z.w == 320);
    CHECK(z.h == 200);
    CHECK(z.priority == 5);
    CHECK(z.next == nullptr);
    CHECK(z.releaseCode == 0x0D);
    CHECK(z.group == 3);
    Zone y = z;
    y.shape = 1;
    y.init(1, 2, 3, 4, 5, 6, 7);
    CHECK(y.shape == 0);
    CHECK(y.group == 3);  // Zone_Init leaves +14h alone
    // A zone linked twice loops; the walk stops.
    ClickZones zs;
    Zone loop = Zone::rect(0, 0, 10, 10, 0, 1, 2);
    zs.addLocal(loop);
    zs.addLocal(loop);
    CHECK(ClickZones::hitTest(5, 5, zs.local()) == &loop);
}

// --------------------------------------------------------------------------------- Input_GetCode

namespace {

struct InputRig {
    GgKeyboard kb;
    GgMouse mouse;
    ClickZones zones;
    uint32_t ticks = 0;
    InputCodes in{kb, mouse, zones, [this] { return ticks; }};
    uint64_t t = 0;
    InputRig() {
        kb.setTickSource([this] { return ticks; });
        mouse.setTickSource([this] { return ticks; });
        mouse.installHandler();
    }
    void key(const char* name, bool down) {
        t += 10 * kMs;
        if (down) kb.keyDown(::key(name), t);
        else kb.keyUp(::key(name), t);
        kb.deliverUntil(t);
    }
    void tap(const char* name) {
        key(name, true);
        key(name, false);
    }
    void at(int x, int y) { mouse.driver().setLogicalPosition(x, y); }
    void button(bool down) { mouse.driver().setButton(dos::Int33Mouse::kLeft, down); }
};

}  // namespace

TEST_CASE("gg Input_GetCode: press codes with 0x4000 then 0x8000, the release code once") {
    InputRig r;
    Zone z = Zone::rect(100, 100, 20, 20, 0, 0x100, 0x101);
    r.zones.addLocal(z);
    r.at(105, 105);
    r.button(true);
    CHECK(r.in.getCode() == 0x4100);
    CHECK(r.in.getCode() == 0x8100);
    CHECK(r.in.getCode() == 0x8100);
    r.button(false);
    CHECK(r.in.getCode() == 0x101);
    CHECK(r.in.getCode() == 0);
    // A press outside every zone: 0, and the first-poll flag is cleared anyway.
    r.at(10, 10);
    r.button(true);
    CHECK(r.in.getCode() == 0);
    CHECK(r.mouse.press().first == 0);
    r.at(105, 105);  // a drag onto the zone: held, not first
    CHECK(r.in.getCode() == 0x8100);
    r.button(false);
    CHECK(r.in.getCode() == 0x101);
    // Press and release between two polls: only the release code; the first flag stays set.
    r.button(true);
    r.button(false);
    CHECK(r.in.getCode() == 0x101);
    CHECK(r.mouse.press().first == 1);
    // A release outside every zone: 0.
    r.at(10, 10);
    r.button(true);
    r.button(false);
    CHECK(r.in.getCode() == 0);
}

TEST_CASE("gg Input_GetCode: one BIOS key per poll, and a key record erases a click") {
    InputRig r;
    r.tap("a");
    r.tap("b");
    r.tap("c");
    CHECK(r.in.getCode() == 'a');
    CHECK(r.in.getCode() == 'b');
    CHECK(r.in.getCode() == 'c');
    CHECK(r.in.getCode() == 0);
    // A Shift make (a record, no BIOS key) in the same poll as a release: the click is lost.
    Zone z = Zone::rect(0, 0, 320, 200, 5, 0, 0x0D);
    r.zones.addLocal(z);
    r.button(true);
    r.button(false);
    r.key("lshift", true);
    CHECK(r.in.getCode() == 0);
    CHECK(r.in.clicksErased() == 1);
    r.key("lshift", false);
    // A key and a click in the same poll: the key wins.
    r.button(true);
    r.button(false);
    r.tap("x");
    CHECK(r.in.getCode() == 'x');
    CHECK(r.in.getCode() == 0);  // the click is gone for good
}

TEST_CASE("gg Input_GetCode: the 12 global keys and the HUD range") {
    InputRig r;
    int sound = 0, esc = 0, trash = 0, putBack = 0, left = 0, right = 0;
    std::vector<int> menus;
    std::vector<uint16_t> parts;
    r.in.hooks().toggleSound = [&] { ++sound; };
    r.in.hooks().hudTrash = [&] { ++trash; };
    r.in.hooks().hudPutBack = [&] { ++putBack; };
    r.in.hooks().hudScrollLeft = [&] { ++left; };
    r.in.hooks().hudScrollRight = [&] { ++right; };
    r.in.hooks().hudSelectPart = [&](uint16_t p) { parts.push_back(p); };

    // Ctrl+S: the sound toggle, code 0.
    r.key("lctrl", true);
    CHECK(r.in.getCode() == 0);  // the Ctrl make record: no BIOS key
    r.tap("s");
    CHECK(r.in.getCode() == 0);
    CHECK(sound == 1);
    r.key("lctrl", false);
    // Esc without a hook: 0x1B; with: the hook runs and 0x1B is returned.
    r.tap("esc");
    CHECK(r.in.getCode() == 0x1B);
    r.in.setEscHandler([&] { ++esc; });
    r.tap("esc");
    CHECK(r.in.getCode() == 0x1B);
    CHECK(esc == 1);
    // Menu keys without a menu hook: 0x13B + index; the Esc hook runs for them too.
    r.key("lalt", true);
    CHECK(r.in.getCode() == 0);
    r.tap("f");
    CHECK(r.in.getCode() == 0x13C);
    CHECK(esc == 2);
    r.tap("h");
    CHECK(r.in.getCode() == 0x13B);
    r.tap("o");
    CHECK(r.in.getCode() == 0x13D);
    r.key("lalt", false);
    // With the menu hook: it gets the index and the code becomes 0 (or the menu request).
    r.in.setMenuHandler([&](int i) { menus.push_back(i); });
    r.tap("f3");
    CHECK(r.in.getCode() == 0);
    r.in.setMenuHandler([&](int i) {
        menus.push_back(i);
        r.in.menuRequest = 1;
    });
    r.tap("f1");
    CHECK(r.in.getCode() == 0x17A2);
    r.in.setMenuHandler([&](int i) {
        menus.push_back(i);
        r.in.menuRequest = 2;
    });
    r.tap("f2");
    CHECK(r.in.getCode() == 0x17A3);
    CHECK(r.in.menuRequest == 0);
    CHECK(menus == std::vector<int>{2, 0, 1});
    // g_menuDisabled: the code comes back, the hook is not called.
    r.in.menuDisabled = 1;
    r.tap("f2");
    CHECK(r.in.getCode() == 0x13C);
    CHECK(menus.size() == 3);
    r.in.menuDisabled = 0;
    // A menu title zone (release code 0x13B + i) is the same as its F key.
    Zone title = Zone::rect(0, 0, 40, 9, 0, 0, 0x13C);
    r.zones.addGlobal(title);
    r.in.setMenuHandler([&](int i) { menus.push_back(i); });
    r.at(10, 4);
    r.button(true);
    CHECK(r.in.getCode() == 0);  // the press code 0 of the title
    r.button(false);
    CHECK(r.in.getCode() == 0);
    CHECK(menus.back() == 1);
    r.zones.removeGlobal(&title);
    // HUD zones: 6000..6003 and 5000..5100; a press code with 0x4000 matches nothing.
    std::vector<Zone> hud;
    for (const uint16_t c : std::initializer_list<uint16_t>{6000, 6001, 6002, 6003, 5000, 5100, 5101}) hud.push_back(Zone::rect(0, 0, 320, 200, 0, c, c));
    for (Zone& z : hud) {
        r.zones.addLocal(z);
        r.at(5, 5);
        r.button(true);
        const uint16_t pressCode = r.in.getCode();
        r.button(false);
        const uint16_t releaseCode = r.in.getCode();
        r.zones.removeLocal(&z);
        CHECK(pressCode == (z.pressCode | 0x4000));
        CHECK(releaseCode == (z.releaseCode == 5101 ? 5101 : 0));
    }
    CHECK(trash == 1);
    CHECK(putBack == 1);
    CHECK(left == 1);
    CHECK(right == 1);
    CHECK(parts == std::vector<uint16_t>{0, 100});
}

TEST_CASE("gg Input_Flush, Input_StripHeld and Input_GetHeldKeys' idle bit") {
    InputRig r;
    int sound = 0;
    r.in.hooks().toggleSound = [&] { ++sound; };
    r.tap("a");
    r.key("lctrl", true);
    r.tap("s");
    r.key("lctrl", false);
    r.in.flush();  // codes discarded, Ctrl+S still acts
    CHECK(r.kb.ring().empty());
    CHECK(sound == 1);
    CHECK(InputCodes::stripHeld(0x4123) == 0x123);
    CHECK(InputCodes::stripHeld(0x8123) == 0);
    CHECK(InputCodes::stripHeld(0x123) == 0x123);

    // The idle bit: once, strictly after 0x111C ticks without a held key.
    r.ticks = 0x111C;
    CHECK(r.in.getHeldKeys(0x1234) == 0x12340000u);
    r.ticks = 0x111D;
    CHECK(r.in.getHeldKeys() == held::kIdle);
    CHECK(r.in.getHeldKeys() == 0);
    // A held key re-arms it and restarts the count.
    r.key("left", true);
    CHECK(r.in.getHeldKeys() == held::kLeft);
    CHECK(r.in.keyHeldSeen == 1);
    CHECK(r.in.lastHeldTick() == 0x111D);
    r.key("left", false);
    r.ticks = 0x111D + 0x111C + 1;
    CHECK(r.in.getHeldKeys() == held::kIdle);
}

// ------------------------------------------------------------------------------ against ssg.exe

TEST_CASE("gg input tables match ssg.exe") {
    GG_REQUIRE_DATA();
    const GgExe& exe = ggFiles->exe();
    // Kbd_ISR's scan table DS:0224.
    const auto scans = exe.dsBytes(dg::kKbdScanCodes, 8);
    CHECK(std::memcmp(scans.data(), KbdIsr::kScanTable.data(), 8) == 0);
    // Input_GetCode's 12-entry switch table CS:0275.
    const auto table = exe.at(0x198d, 0x0275);
    REQUIRE(table.size() >= 24);
    for (int i = 0; i < 12; ++i)
        CHECK(static_cast<uint16_t>(table[2 * i] | (table[2 * i + 1] << 8)) == kGlobalKeyCodes[i]);
    // The 5000..5100 range compares of 198d:0256 / 025c.
    const auto range = exe.at(0x198d, 0x0256);
    CHECK((range[2] | (range[3] << 8)) == code::kHudPartFirst);
    CHECK((range[8] | (range[9] << 8)) == code::kHudPartLast);
    // The attract's full-screen zone DS:0a3a: (0, 0, 320, 200), priority 5, release 0x0D.
    const Zone z = Zone::fromRecord(exe.dsBytes(dg::kZoneClickEnter, Zone::kRecordSize));
    CHECK(z.shape == 0);
    CHECK(z.x == 0);
    CHECK(z.y == 0);
    CHECK(z.w == 320);
    CHECK(z.h == 200);
    CHECK(z.priority == 5);
    CHECK(z.pressCode == 0);
    CHECK(z.releaseCode == code::kEnter);
    // The event mask of Input_InstallHandlers 1454:0008 (MOV CX,1Fh).
    const auto install = exe.at(0x1454, 0x0008);
    CHECK(install[0] == 0xB9);
    CHECK((install[1] | (install[2] << 8)) == GgMouse::kEventMask);
}
