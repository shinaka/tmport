// DOS edition input (dos/input/*): Kbd_ISR rules, the BIOS key buffer, synthesised typematic
// repeats, the per-tick g_keys facts of the reference logs (code_runtime.md §5.3, §9;
// extracted/captures/dos/savestates/r2/fl_*), Input_Poll / Input_HitZone / Input_Tick /
// Input_MouseWalk (4a85) on the user's SST.EXE tables, Mouse_Poll.
#include <doctest/doctest.h>

#include <SDL3/SDL_scancode.h>

#include <map>
#include <string>
#include <vector>

#include "dos/app/dos_runtime.h"
#include "dos/input/input_poll.h"
#include "dos/input/keyboard.h"
#include "dos/input/mouse.h"
#include "dos_test_data.h"
#include "mischief/dos/input/bios_keyboard.h"

using namespace tmport::dos;
using namespace tmport::dos::keybits;

namespace {

constexpr uint64_t kMs = 1'000'000;

mischief::dos::PcKey key(const char* name) {
    const auto k = mischief::dos::pcKeyByName(name);
    REQUIRE(k);
    return *k;
}

// Feeds a key press and release through a BIOS model with optional modifiers; returns the word read.
uint16_t biosWordFor(uint8_t scan, bool extended, uint8_t modifierScan = 0) {
    mischief::dos::BiosKeyboard b;
    if (modifierScan) b.onScanByte(modifierScan);
    if (extended) b.onScanByte(0xE0);
    b.onScanByte(scan);
    if (extended) b.onScanByte(0xE0);
    b.onScanByte(static_cast<uint8_t>(scan | 0x80));
    if (modifierScan) b.onScanByte(static_cast<uint8_t>(modifierScan | 0x80));
    return b.read();
}

// A stand-in for the game's tick: records g_keys at Game_Tick entry (as the frame logger does at
// 130f:0112), then masks the held bits the terrain forbids, as Player_Update writes them back.
struct TerrainClient : RuntimeClient {
    DosRuntime* rt = nullptr;
    uint16_t forbidden = kUp | kDown;  // flat ground: no ladder
    std::map<uint64_t, uint16_t> log;
    std::vector<uint16_t> biosKeys;    // one BIOS key per tick, as Input_Poll takes them
    void gameTick(const TickInfo& t) override {
        uint16_t& keys = rt->keyboard().isr().keys;
        log[t.gameTick] = keys;
        keys &= static_cast<uint16_t>(~forbidden);
        mischief::dos::BiosKeyboard& b = rt->keyboard().bios();
        if (b.peek()) biosKeys.push_back(b.read());
    }
};

struct Harness {
    DosRuntime rt;
    TerrainClient client;
    explicit Harness(RuntimeOptions o = {}) : rt(o) {
        client.rt = &rt;
        rt.setClient(&client);
    }
    void run(const char* script, uint64_t ticks) {
        std::string err;
        auto s = mischief::dos::ScriptedInput::parse(script, err);
        REQUIRE_MESSAGE(s, err);
        rt.setScript(*s);
        rt.stepGameTicks(ticks);
    }
    uint16_t at(uint64_t tick) const { return client.log.at(tick); }
};

}  // namespace

// ------------------------------------------------------------------------------------- mapping

// --------------------------------------------------------------------------------------- Kbd_ISR

TEST_CASE("DOS Kbd_ISR: press and release tables") {
    KbdIsr k;
    k.onScanByte(0x48);
    CHECK(k.keys == kUp);
    k.onScanByte(0xC8);
    CHECK(k.keys == 0);
    k.onScanByte(0x50);
    k.onScanByte(0x39);
    CHECK(k.keys == (kDown | kNet));
    k.onScanByte(0xD0);
    k.onScanByte(0xB9);
    CHECK(k.keys == 0);
    k.onScanByte(0x4B);
    CHECK(k.keys == kLeft);
    k.onScanByte(0xCB);
    k.onScanByte(0x4D);
    CHECK(k.keys == kRight);
    k.onScanByte(0xCD);
    CHECK(k.keys == 0);
    // Ctrl only toggles DS:92FA; the swallowed scans are not passed to the BIOS.
    CHECK(k.onScanByte(0x1D));
    CHECK_FALSE(k.ctrlReleased);
    CHECK(k.onScanByte(0x9D));
    CHECK(k.ctrlReleased);
    for (uint8_t s : {uint8_t{0x37}, uint8_t{0x45}, uint8_t{0x46}, uint8_t{0x54}}) {
        CHECK_FALSE(k.onScanByte(s));
        CHECK_FALSE(k.onScanByte(static_cast<uint8_t>(s | 0x80)));
    }
    CHECK(k.onScanByte(0x1E));  // other keys go to the BIOS unchanged
    CHECK(k.keys == 0);
}

TEST_CASE("DOS Kbd_ISR: diagonals form a jump that the next key event clears") {
    KbdIsr k;
    // Home = Up+Left -> Left + jump; PgUp = Up+Right -> Right + jump; F12 = Tandy Home.
    k.onScanByte(0x47);
    CHECK(k.keys == (kLeft | kJump));
    CHECK(k.jumpFromDiagonal);
    k.onScanByte(0xC7);
    CHECK(k.keys == 0);
    CHECK_FALSE(k.jumpFromDiagonal);
    k.onScanByte(0x49);
    CHECK(k.keys == (kRight | kJump));
    k.onScanByte(0x1E);  // any other key event clears the jump bit
    CHECK(k.keys == kRight);
    k.onScanByte(0xC9);
    CHECK(k.keys == 0);
    k.onScanByte(0x58);
    CHECK(k.keys == (kLeft | kJump));
    k.onScanByte(0xD8);
    CHECK(k.keys == 0);
    // Right then Up: a jump, Up/Down dropped.
    k.onScanByte(0x4D);
    k.onScanByte(0x50);
    k.onScanByte(0x48);
    CHECK(k.keys == (kRight | kJump));
    // A repeat of Up re-forms the diagonal (the jump stays).
    k.onScanByte(0x48);
    CHECK(k.keys == (kRight | kJump));
    // Releasing Up clears the jump.
    k.onScanByte(0xC8);
    CHECK(k.keys == kRight);
    // The E0 prefix takes the break path with scan 0x60 and clears a diagonal jump (the artefact).
    k.onScanByte(0x48);
    CHECK(k.keys == (kRight | kJump));
    CHECK(k.onScanByte(0xE0));
    CHECK(k.keys == kRight);
}

TEST_CASE("DOS Kbd_ISR: the Tandy arrow codes only count on a Tandy") {
    KbdIsr pc;
    for (uint8_t s : {uint8_t{0x29}, uint8_t{0x4A}, uint8_t{0x2B}, uint8_t{0x4E}}) pc.onScanByte(s);
    CHECK(pc.keys == 0);
    KbdIsr tandy;
    tandy.tandy = true;
    tandy.onScanByte(0x4A);
    tandy.onScanByte(0x2B);
    CHECK(tandy.keys == (kDown | kLeft));
    tandy.onScanByte(0x29);  // Up+Left -> jump
    CHECK(tandy.keys == (kLeft | kJump));
    tandy.onScanByte(0xAB);
    tandy.onScanByte(0xCA);
    tandy.onScanByte(0x4E);
    CHECK(tandy.keys == kRight);
    tandy.onScanByte(0xCE);
    CHECK(tandy.keys == 0);
}

TEST_CASE("DOS Kbd_ISR: the scan tables match the user's SST.EXE (CS 43a0:019F / 00DC)") {
    TM_REQUIRE_DOS_DATA();
    for (uint16_t table : {uint16_t{0x019F}, uint16_t{0x00DC}}) {
        const auto bytes = dosExe->at(0x43a0, table);
        REQUIRE(bytes.size() >= 68);
        std::vector<uint16_t> scans;
        for (int i = 0; i < 17; ++i) scans.push_back(static_cast<uint16_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8)));
        CHECK(scans == std::vector<uint16_t>{0x1D, 0x29, 0x2B, 0x37, 0x39, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B,
                                             0x4D, 0x4E, 0x50, 0x54, 0x58});
    }
}

// ------------------------------------------------------------------------------------------ BIOS

TEST_CASE("DOS BIOS keyboard: every key word of the game's key lists can be typed") {
    TM_REQUIRE_DOS_DATA();
    const InputTables t = InputTables::load(*dosExe);
    int words = 0;
    for (int ctx = 0; ctx < kInputContexts; ++ctx) {
        const auto& km = t.keyMap(ctx);
        for (size_t i = 1; i < km.keys.size(); ++i) {
            const uint16_t w = km.keys[i];
            bool found = false;
            for (int scan = 1; scan <= 0x58 && !found; ++scan)
                for (uint8_t mod : {uint8_t{0}, uint8_t{0x2A}, uint8_t{0x1D}, uint8_t{0x38}})
                    for (bool ext : {false, true})
                        if (!found && biosWordFor(static_cast<uint8_t>(scan), ext, mod) == w) found = true;
            INFO("context " << ctx << " key word " << w);
            CHECK(found);
            ++words;
        }
    }
    CHECK(words == 13 + 9 + 11 + 13 + 69 + 13);
}

// ----------------------------------------------------------------------------- PC keyboard device

TEST_CASE("DOS keyboard: typematic repeats are synthesised (500 ms, then every 33 ms)") {
    PcKeyboard kb;
    const mischief::dos::PcKey a = key("a");
    kb.keyDown(a, 0);
    kb.keyDown(a, 30 * kMs);  // host auto-repeat: ignored
    kb.deliverUntil(0);
    CHECK(kb.bios().size() == 1);
    kb.deliverUntil(500 * kMs - 1);
    CHECK(kb.bios().size() == 1);
    kb.deliverUntil(500 * kMs);
    CHECK(kb.bios().size() == 2);
    kb.deliverUntil(533 * kMs - 1);
    CHECK(kb.bios().size() == 2);
    kb.deliverUntil(533 * kMs);
    CHECK(kb.bios().size() == 3);
    kb.bios().flush();
    kb.deliverUntil(1000 * kMs);
    CHECK(kb.repeatsGenerated() == 1 + (1000 - 500) / 33);
    kb.keyUp(a, 1000 * kMs);
    const uint64_t before = kb.repeatsGenerated();
    kb.deliverUntil(5000 * kMs);
    CHECK(kb.repeatsGenerated() == before);
    CHECK_FALSE(kb.nextDue());
}

TEST_CASE("DOS keyboard: only the last key pressed repeats") {
    PcKeyboard kb;
    const mischief::dos::PcKey a = key("a"), b = key("b");
    kb.keyDown(a, 0);
    kb.keyDown(b, 100 * kMs);
    kb.keyUp(a, 200 * kMs);  // releasing another key does not stop B's repeat
    kb.deliverUntil(1000 * kMs);
    std::vector<uint16_t> words;
    while (kb.bios().peek()) words.push_back(kb.bios().read());
    REQUIRE(words.size() >= 3);
    CHECK(words[0] == 0x1E61);
    CHECK(words[1] == 0x3062);
    for (size_t i = 2; i < words.size(); ++i) CHECK(words[i] == 0x3062);
    CHECK(words.size() == 2 + 1 + (1000 - 600) / 33);
    // B released: nothing repeats any more, A does not take over.
    kb.keyUp(b, 1000 * kMs);
    kb.keyDown(a, 1100 * kMs);
    kb.keyDown(b, 1200 * kMs);
    kb.keyUp(b, 1300 * kMs);
    kb.deliverUntil(1300 * kMs);
    CHECK(kb.bios().read() == 0x1E61);
    CHECK(kb.bios().read() == 0x3062);
    CHECK(kb.bios().size() == 0);
    kb.deliverUntil(4000 * kMs);
    CHECK(kb.bios().size() == 0);
    CHECK(kb.isHeld(a));
    kb.releaseAll(4000 * kMs);
    CHECK_FALSE(kb.isHeld(a));
}

TEST_CASE("DOS keyboard: a full BIOS buffer drops further keys") {
    PcKeyboard kb;
    const char* names[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t"};
    for (const char* n : names) {
        kb.keyDown(key(n), 0);
        kb.keyUp(key(n), 0);
    }
    kb.deliverUntil(0);
    CHECK(kb.bios().size() == 15);
    CHECK(kb.bios().dropped() == 5);
    CHECK(kb.bios().read() == 0x1E61);
}

// ------------------------------------------------------------------- per-tick facts of the logs

TEST_CASE("DOS keys per tick: 'Up then Right' does not jump, 'Right then Up' does (r2/fl_jumps)") {
    {
        Harness h;
        h.run("100:down:up 110:down:right 200:up:up 200:up:right", 220);
        CHECK(h.at(100) == kUp);
        CHECK(h.at(101) == 0);  // the terrain dropped Up at tick 100
        for (uint64_t t = 110; t < 200; ++t) {
            INFO("tick " << t);
            CHECK(h.at(t) == kRight);
        }
        CHECK(h.at(201) == 0);
    }
    {
        Harness h;
        h.run("100:down:right 110:down:up 150:up:up 160:up:right", 170);
        CHECK(h.at(109) == kRight);
        // Up's repeats (from tick 110 + 24) re-form the diagonal: the jump stays while Up is held.
        for (uint64_t t = 110; t < 150; ++t) {
            INFO("tick " << t);
            CHECK(h.at(t) == (kRight | kJump));
        }
        CHECK(h.at(150) == kRight);  // releasing Up (before tick 150) cleared the jump
        CHECK(h.at(161) == 0);
    }
    {
        Harness h;  // PgUp alone, tapped: jump at once (r2/fl_jumps t=386: 0x28 for 4 ticks)
        h.run("386:down:pgup 390:up:pgup", 400);
        for (uint64_t t = 386; t < 390; ++t) CHECK(h.at(t) == (kRight | kJump));
        CHECK(h.at(390) == 0);
    }
}

TEST_CASE("DOS keys per tick: the first repeat comes 24 ticks after the press, then ~2 in 3 ticks") {
    // r2/fl_typematic: Down pressed at tick 807 shows again (a repeat) first at 831; r2/fl_shack_left:
    // Left against a wall re-set in 95 of 148 ticks (145..292), r2/fl_typematic in 31 of 48 (831..878).
    Harness h;
    h.client.forbidden = kUp | kDown | kLeft;  // a wall on the left
    h.run("807:down:left", 1100);
    CHECK(h.at(807) == kLeft);
    for (uint64_t t = 808; t < 831; ++t) CHECK(h.at(t) == 0);
    CHECK(h.at(831) == kLeft);
    auto count = [&](uint64_t from, uint64_t to) {
        int n = 0;
        for (uint64_t t = from; t <= to; ++t) n += (h.at(t) & kLeft) ? 1 : 0;
        return n;
    };
    const int c148 = count(831, 831 + 147);
    CHECK(c148 >= 94);
    CHECK(c148 <= 95);
    const int c48 = count(831, 878);
    CHECK(c48 >= 30);
    CHECK(c48 <= 31);
    // Never two ticks in a row without a repeat, never three with one (33 ms vs 21.1 ms ticks).
    for (uint64_t t = 831; t < 1090; ++t) {
        const bool a = h.at(t) & kLeft, b = h.at(t + 1) & kLeft, c = h.at(t + 2) & kLeft;
        CHECK((a || b));
        CHECK(!(a && b && c));
    }
}

TEST_CASE("DOS keys per tick: the grey-key E0 artefact is optional") {
    // r2/fl_jumps t=274: holding grey PgUp, a tick between a repeat's E0 and its 0x49 sees 0x08.
    auto blips = [](bool artefact, const char* keyName) {
        RuntimeOptions o;
        o.keyboard.greyKeyE0Artefact = artefact;
        Harness h(o);
        h.run((std::string("100:down:") + keyName + " 400:up:" + keyName).c_str(), 400);
        std::vector<uint64_t> ticks;
        for (uint64_t t = 100; t < 400; ++t) {
            if (h.at(t) == kRight) ticks.push_back(t);
            else CHECK(h.at(t) == (kRight | kJump));
        }
        return ticks;
    };
    CHECK(blips(false, "pgup").empty());
    CHECK(blips(true, "kp9").empty());  // keypad keys send no E0
    // Expected: ticks n with r - 1 ms <= T(n) < r for a repeat r = T(100) + 500 ms + k * 33 ms.
    std::vector<uint64_t> expected;
    const uint64_t press = PitClock::timeOfGameTick(100);
    for (uint64_t r = press + 500 * kMs; r < PitClock::timeOfGameTick(400); r += 33 * kMs)
        for (uint64_t n = 100; n < 400; ++n)
            if (PitClock::timeOfGameTick(n) + kMs >= r && PitClock::timeOfGameTick(n) < r) expected.push_back(n);
    const auto got = blips(true, "pgup");
    CHECK(got == expected);
    MESSAGE("E0 artefact ticks in 300: " << got.size());
}

// ------------------------------------------------------------------------- 4a85 input.c tables

TEST_CASE("DOS input tables: key lists, commands and zones from SST.EXE (code_runtime.md §6.1)") {
    TM_REQUIRE_DOS_DATA();
    const InputTables t = InputTables::load(*dosExe);
    CHECK(t.keysAddress(0) == 0x7D84);
    CHECK(t.keysAddress(3) == 0x7D84);
    CHECK(t.keysAddress(5) == 0x7D84);
    CHECK(t.keysAddress(1) == 0x7DBE);
    CHECK(t.keysAddress(2) == 0x7DE6);
    CHECK(t.keysAddress(4) == 0x7E16);
    const std::array<size_t, kInputContexts> counts = {13, 9, 11, 13, 69, 13};
    for (int c = 0; c < kInputContexts; ++c) CHECK(t.keyMap(c).keys.size() == counts[static_cast<size_t>(c)] + 1);
    auto cmd = [&](int ctx, uint16_t w) { return t.keyMap(ctx).commands[static_cast<size_t>(inputLookupKey(w, t.keyMap(ctx)))]; };
    CHECK(cmd(0, 0x011B) == 0);
    CHECK(cmd(0, 0x3B00) == 1);
    CHECK(cmd(0, 0x3D00) == 3);
    CHECK(cmd(0, 0x4B00) == 4);
    CHECK(cmd(0, 0x5000) == 7);
    CHECK(cmd(0, 0x1C0D) == 8);
    CHECK(cmd(0, 0x3200) == 12);
    CHECK(cmd(0, 0x1F73) == 13);
    CHECK(cmd(0, 0x1F53) == 13);
    CHECK(cmd(0, 0x1F13) == 14);
    CHECK(cmd(0, 0x1E61) == -1);
    CHECK(cmd(1, 0x1F73) == -1);  // the menu has only the first 9
    CHECK(cmd(2, 0x0F09) == 4);
    CHECK(cmd(2, 0x1559) == 14);
    CHECK(cmd(2, 0x316E) == 15);
    CHECK(cmd(4, 0x3920) == 32);
    CHECK(cmd(4, 0x1312) == 30);
    CHECK(cmd(4, 0x0E08) == 31);
    CHECK(cmd(4, 0x0B30) == 48);
    CHECK(cmd(4, 0x1E41) == 65);
    CHECK(cmd(4, 0x2C7A) == 122);
    // Zone tables: addresses, counts and room up to the next table.
    const std::array<uint16_t, kInputContexts> addr = {0x7F2E, 0x7F87, 0x8010, 0x8069, 0x6194, 0x80CA};
    const std::array<int, kInputContexts> zoneCounts = {11, 9, 9, 12, 9, 2};
    for (int c = 0; c < kInputContexts; ++c) {
        CHECK(t.zonesAddress(c) == addr[static_cast<size_t>(c)]);
        CHECK(t.zones(c).count == zoneCounts[static_cast<size_t>(c)]);
        CHECK(t.zones(c).records.size() == static_cast<size_t>(InputTables::kZoneCapacity[static_cast<size_t>(c)]));
    }
    CHECK(0x7F2E + 1 + 8 * 11 == 0x7F87);
    CHECK(0x7F87 + 1 + 8 * 17 == 0x8010);
    CHECK(0x8010 + 1 + 8 * 11 == 0x8069);
    CHECK(0x8069 + 1 + 8 * 12 == 0x80CA);
    CHECK(0x80CA + 1 + 8 * 2 == 0x80DB);
    CHECK(0x6194 + 1 + 8 * 19 == 0x622D);  // SignIn_SetupZones' template follows
    const InputZone field = t.zones(0).records[9];  // the play-field
    CHECK(field == InputZone{0, 0x10, 0x9F, 0x8F, -1, 9, 9, 10});
    CHECK(t.zones(0).records[10] == InputZone{0, 0x92, 0x9F, 0xC7, -1, 11, 11, 10});
    CHECK(t.zones(5).records[1].release == 13);
}

TEST_CASE("DOS Input_HitZone and Input_Poll (4a85:0130, 4a85:000E)") {
    InputTables t;
    InputKeyMap km;
    km.keys = {0, 0x011B, 0x1C0D};
    km.commands = {-1, 0, 8};
    t.setKeyMap(0, km);
    InputZoneTable& z = t.zones(0);
    z.count = 2;
    z.records[0] = {0, 0, 159, 199, 20, 21, 22, 23};
    z.records[1] = {10, 10, 20, 20, 30, 31, 32, 33};  // tested first (the last record wins)

    MousePointer m;
    m.x = 15;
    m.y = 15;
    CHECK(inputHitZone(z, m) == 30);  // hover
    m.buttons = 1;
    CHECK(inputHitZone(z, m) == 31);  // press
    m.prevButtons = 2;
    CHECK(inputHitZone(z, m) == 32);  // held (any button)
    m.buttons = 0;
    CHECK(inputHitZone(z, m) == 33);  // release
    m.x = 100;
    CHECK(inputHitZone(z, m) == 23);
    z.count = 0;
    CHECK(inputHitZone(z, m) == -1);
    z.count = 2;

    mischief::dos::BiosKeyboard bios;
    InputPoll ip(t);
    m.prevButtons = 0;
    m.buttons = 0;
    // One key per tick; zones only in ticks without a key.
    bios.push(0x011B);
    bios.push(0x1C0D);
    ip.poll(bios, m, 0);
    CHECK(ip.cmd == 0);
    CHECK(ip.lastKey == 0x011B);
    CHECK_FALSE(ip.deliverNext);
    ip.poll(bios, m, 0);
    CHECK(ip.cmd == 8);
    ip.poll(bios, m, 0);
    CHECK(ip.cmd == 20);  // hover command of the big zone
    CHECK(ip.lastKey == 0);
    // An unknown key gives no command.
    bios.push(0x1E61);
    ip.poll(bios, m, 0);
    CHECK(ip.cmd == -1);

    // In a UI mode with the previous command not consumed, a new command is queued and goes first
    // once the UI asks for the next command (DS:9238).
    m.x = 200;  // outside every zone
    ip.cmd = 8;
    ip.deliverNext = false;
    bios.push(0x011B);
    ip.poll(bios, m, 0x40);
    CHECK(ip.cmd == 8);
    CHECK(ip.queuedCmd == 0);
    ip.cmd = -1;
    ip.deliverNext = true;
    bios.push(0x1C0D);
    ip.poll(bios, m, 0x40);
    CHECK(ip.cmd == 0);
    CHECK(ip.queuedCmd == 8);
}

TEST_CASE("DOS Input_Tick: Esc and F1-F3 in play (4a85:03AC)") {
    InputTables t;
    InputKeyMap km;
    km.keys = {0, 0x011B, 0x3B00, 0x1F73};
    km.commands = {-1, 0, 1, 13};
    t.setKeyMap(0, km);
    mischief::dos::BiosKeyboard bios;
    MousePointer m;
    m.x = 200;  // outside every zone
    InputPoll ip(t);
    bios.push(0x011B);
    CHECK(ip.tick(bios, m, 0, false, false) == InputTickAction::MenuEscape);
    CHECK(ip.cmd == 8);
    ip.cmd = -1;
    bios.push(0x011B);
    CHECK(ip.tick(bios, m, 0, false, true) == InputTickAction::ExitDialog);
    ip.cmd = -1;
    bios.push(0x3B00);
    CHECK(ip.tick(bios, m, 0, false, false) == InputTickAction::MenuOpen);
    ip.cmd = -1;
    bios.push(0x3B00);  // no menus during a scripted move
    CHECK(ip.tick(bios, m, 0, true, false) == InputTickAction::None);
    CHECK(ip.cmd == -1);
    bios.push(0x1F73);  // 13 > 11: dropped during a scripted move
    ip.tick(bios, m, 0, true, false);
    CHECK(ip.cmd == -1);
    bios.push(0x011B);  // Esc is kept during a scripted move
    ip.tick(bios, m, 0, true, false);
    CHECK(ip.cmd == 0);
}

TEST_CASE("DOS Input_MouseWalk (4a85:0208)") {
    InputTables t;
    InputPoll ip(t);
    KbdIsr kbd;
    MousePointer m;
    MouseWalkParams p;
    p.playerScreenX = 0x40;  // px = 0x42 bytes
    p.playerScreenY = 0x60;  // feet at 0x70
    // Down where Down cannot climb becomes "drop a coin"; the high byte is cleared too.
    p.terrainMask = 0;
    kbd.keys = 0x0302;
    CHECK(ip.mouseWalk(kbd, m, p) == 4);
    CHECK(kbd.keys == kCoin);
    kbd.keys = 0x0340;
    ip.mouseWalk(kbd, m, p);
    CHECK(kbd.keys == 0);
    p.terrainMask = 3;
    ip.cmd = 11;  // a status-bar click is ignored where Down climbs
    CHECK(ip.mouseWalk(kbd, m, p) == 0);
    CHECK(ip.cmd == -1);
    ip.cmd = 5;  // arrow commands are dropped in play
    ip.mouseWalk(kbd, m, p);
    CHECK(ip.cmd == -1);
    // Play-field pressed or held: walk, climb, jump or net relative to the player.
    auto press = [&](int mx, int my) {
        ip.cmd = 9;
        kbd.keys = 0;
        kbd.jumpFromDiagonal = false;
        m.x = static_cast<uint16_t>(mx);
        m.y = static_cast<uint16_t>(my);
        ip.mouseWalk(kbd, m, p);
        CHECK(ip.cmd == -1);
        return kbd.keys;
    };
    CHECK(press(0x30, 0x60) == kLeft);
    CHECK(press(0x50, 0x60) == kRight);
    CHECK(press(0x40, 0x60) == 0);
    CHECK(press(0x42, 0x20) == kUp);
    CHECK(press(0x42, 0x80) == kDown);
    CHECK(press(0x10, 0x20) == (kJump | kLeft));
    CHECK(kbd.jumpFromDiagonal);
    CHECK(press(0x70, 0x20) == (kJump | kRight));
    CHECK(press(0x10, 0x80) == (kNet | kLeft));
    CHECK(press(0x70, 0x80) == (kNet | kRight));
    // Near the left edge px - 8 wraps to 0xFFFA (unsigned compares): a click right above him is
    // "up-left", a jump, instead of a climb.
    p.playerScreenX = 2;
    CHECK(press(0x02, 0x20) == (kJump | kLeft));
    p.playerScreenX = 0x40;
    // Release stops; a status-bar press drops a coin.
    ip.cmd = 10;
    kbd.keys = kRight;
    ip.mouseWalk(kbd, m, p);
    CHECK(kbd.keys == 0);
    ip.cmd = 11;
    p.terrainMask = 0;
    ip.mouseWalk(kbd, m, p);
    CHECK(kbd.keys == kCoin);
    // Without a mouse driver the mouse part is skipped.
    p.mousePresent = false;
    ip.cmd = 10;
    kbd.keys = kRight;
    ip.mouseWalk(kbd, m, p);
    CHECK(kbd.keys == kRight);
    CHECK(ip.cmd == 10);
    // Nothing at all in a UI mode.
    p.uiMode = 0x40;
    CHECK_FALSE(ip.mouseWalk(kbd, m, p));
}

TEST_CASE("DOS mouse: the driver's start position per look, as the DOSBox-X captures show the pointer") {
    // REFERENCE_CAPTURES.md §6: EGA and Tandy (159, 101), MCGA (158, 82), CGA (159, 146).
    struct Start {
        Look look;
        uint16_t x, y;
    };
    for (const Start s : {Start{Look::Ega, 159, 101}, Start{Look::Tandy, 159, 101}, Start{Look::Mcga, 158, 82},
                          Start{Look::Cga, 159, 146}}) {
        MouseDriver d;
        d.setLogicalPosition(10, 20);
        d.resetPosition(s.look);
        CAPTURE(static_cast<int>(s.look));
        CHECK(d.x() == s.x);
        CHECK(d.y() == s.y);
    }
}

TEST_CASE("DOS mouse: logical coordinates and Mouse_Poll's 600-tick hide (130f:0D94)") {
    MouseDriver d;
    CHECK(d.x() == 159);
    CHECK(d.y() == 101);
    d.setLogicalPosition(319, 250);
    CHECK(d.x() == 159);
    CHECK(d.y() == 199);
    d.setLogicalPosition(101, 50);
    CHECK(d.x() == 50);
    CHECK(d.y() == 50);
    d.setLogicalPosition(-5, -5);
    CHECK(d.x() == 0);
    CHECK(d.y() == 0);

    MousePointer p;
    CHECK_FALSE(p.visible);
    d.setLogicalPosition(100, 60);
    auto r = p.poll(d, 0, 0);
    CHECK(r.polled);
    CHECK(p.visible);  // the first change shows the pointer
    CHECK(r.callCount == 0);
    d.setLogicalPosition(90, 60);  // moving left over the play-field
    r = p.poll(d, 0, 0);
    REQUIRE(r.callCount == 1);
    CHECK(r.calls[0].op == PointerAnimCall::Op::Set);
    CHECK(r.calls[0].script == MousePointer::kScriptFaceLeft);
    d.setLogicalPosition(120, 60);
    r = p.poll(d, 0, 7);
    CHECK(r.calls[0].script == MousePointer::kScriptFaceRight);
    r = p.poll(d, 0, 1);  // still, script 1: face left, then keep the current script
    REQUIRE(r.callCount == 2);
    CHECK(r.calls[0].script == MousePointer::kScriptFaceLeft);
    CHECK(r.calls[1].op == PointerAnimCall::Op::SetIfIdleCurrent);
    r = p.poll(d, 0x40, 0);  // a UI mode: the arrow
    CHECK(r.calls[0].script == MousePointer::kScriptArrow);
    // Unchanged for 600 executed ticks: hidden (the two still polls above already counted).
    int hiddenAt = -1;
    for (int i = 0; i < 700 && hiddenAt < 0; ++i) {
        r = p.poll(d, 0, 0);
        if (!p.visible) hiddenAt = i;
    }
    CHECK(hiddenAt == 600 - 2 - 1);
    CHECK(r.calls[static_cast<size_t>(r.callCount - 1)].op == PointerAnimCall::Op::Clear);
    p.poll(d, 0, 0);
    CHECK_FALSE(p.visible);
    d.setButtons(MouseDriver::kLeft);  // a button change shows it again
    p.poll(d, 0, 0);
    CHECK(p.visible);
    CHECK(p.idleTicks == 0);
    d.present = false;
    CHECK_FALSE(p.poll(d, 0, 0).polled);
}

TEST_CASE("DOS keys per tick: the typematic pattern of the reference logs, tick for tick") {
    // Left against the clubhouse wall (r2/fl_shack_left, ticks 145..292) and Down where it drops a
    // coin (r2/fl_typematic, ticks 831..878): the game clears the bit every tick, so a tick sees it
    // exactly when a repeat arrived since the previous tick. The bit patterns are the frame logger's
    // records; a 500 ms delay and a 33 ms period reproduce both exactly for a press inside the
    // fitted window (1.20..2.04 ms after tick 55; 9.82..10.97 ms after tick 806). 32.9 ms misses 22.
    struct Case {
        const char* key;
        uint64_t pressTick;  // first tick that saw the key
        uint64_t pressUs;    // press time after the tick before it
        uint16_t bit;
        uint64_t from, to;
        const char* hex;     // tick `from` = most significant bit of the first digit
    };
    const Case cases[] = {
        {"left", 56, 1620, kLeft, 145, 292, "d6db5b6b6dadb5b6d6dadb6b6d6db5b6b6dad"},
        {"down", 807, 10400, kDown, 831, 878, "db5b6b6dadb5"},
    };
    for (const Case& c : cases) {
        INFO(std::string(c.key));
        PcKeyboard kb;
        kb.keyDown(key(c.key), PitClock::timeOfGameTick(c.pressTick - 1) + c.pressUs * 1000);
        int ones = 0, mismatches = 0;
        for (uint64_t n = c.pressTick; n <= c.to; ++n) {
            kb.deliverUntil(PitClock::timeOfGameTick(n));
            const bool seen = (kb.isr().keys & c.bit) != 0;
            if (n == c.pressTick) CHECK(seen);
            if (n >= c.from) {
                const uint64_t i = n - c.from;
                const char digit = c.hex[i / 4];
                const int v = digit <= '9' ? digit - '0' : digit - 'a' + 10;
                const bool logged = ((v >> (3 - i % 4)) & 1) != 0;
                if (seen != logged) ++mismatches;
                ones += seen ? 1 : 0;
            }
            kb.isr().keys &= static_cast<uint16_t>(~c.bit);
        }
        CHECK(mismatches == 0);
        MESSAGE(std::string(c.key) << ": " << ones << " of " << (c.to - c.from + 1) << " ticks, " << mismatches << " differ from the log");
    }
}
