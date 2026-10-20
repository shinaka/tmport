// The TLC UI library on the game (wave 3, area ui3): the sign-in (state 2 with SignIn_Run, the chimps
// question, PLAYERS.DAT at the original's moments), the modal loop and the prebuilt boxes (time frozen,
// the sound paused, keys and clicks), the menu bar and its drop-downs with the commands, the help pager,
// the text viewer, New Player Hints and the UI's part of starting from a DOSBox-X state. Driven
// headlessly through the game fixture with scripted keys and mouse in PIT ticks and compared with the
// DOSBox-X captures of w0 and round 2 (REFERENCE_CAPTURES.md §3, §9.5, §9.6). Every run takes a scratch
// preference folder; only counts, ticks, coordinates and hashes are kept here.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "gg/app/seq_run.h"
#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/game/game.h"
#include "gg/save/gg_state_file.h"
#include "gg/save/players_dat.h"
#include "gg/save/players_store.h"
#include "gg/ui/dialog.h"
#include "gg/ui/help.h"
#include "gg/ui/hints.h"
#include "gg/ui/menu_bar.h"
#include "gg/ui/menu_commands.h"
#include "gg/ui/players.h"
#include "gg/ui/signin.h"
#include "gg/ui/text_viewer.h"
#include "gg/ui/ui_objects.h"
#include "gg_game_fixture.h"
#include "gg_test_data.h"
#include "mods/png_codec.h"
#include "util/file_util.h"
#include "util/hash.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

// Runs the fixture's function until `ticks` more PIT interrupts have run (or it ends).
std::vector<GgTraceRecord> runTicks(gggame::GameFixture& f, double ticks) {
    const auto until = static_cast<uint64_t>(static_cast<double>(f.runtime().clock().interrupts()) + ticks);
    return f.runUntil([&](const GgTraceRecord& r) { return r.interrupts >= until; }, 4'000'000);
}

// Runs until the function has returned (or `maxTicks` have run).
void runToEnd(gggame::GameFixture& f, double maxTicks = 20000) {
    const auto until = static_cast<uint64_t>(static_cast<double>(f.runtime().clock().interrupts()) + maxTicks);
    f.runUntil([&](const GgTraceRecord& r) { return r.interrupts >= until; }, 8'000'000);
}

struct Box {
    int x = 0, y = 0, w = 0, h = 0;
};

// Differing pixels between the fixture's last picture and a capture (640 x 400: 2 x 2 blocks), inside
// `boxes` only (the whole frame when empty); -1 when the capture is missing or unreadable.
int differingIn(const gggame::GameFixture& f, const std::filesystem::path& png, const std::vector<Box>& boxes = {}) {
    const auto bytes = readWholeFile(png);
    if (!bytes) return -1;
    const auto img = mods::decodePng(*bytes);
    const int step = img.width == 640 ? 2 : 1;
    if (img.width != 320 * step || img.height != 200 * step) return -1;
    const std::vector<uint32_t> frame = f.frame();
    if (frame.size() != 320u * 200u) return -1;
    int n = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            bool in = boxes.empty();
            for (const Box& b : boxes) in = in || (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h);
            if (!in) continue;
            const uint32_t p = frame[static_cast<size_t>(y) * 320 + static_cast<size_t>(x)];
            const uint8_t* c = &img.rgba[(static_cast<size_t>(y * step) * img.width + static_cast<size_t>(x * step)) * 4];
            n += (p & 0xFF) != c[0] || ((p >> 8) & 0xFF) != c[1] || ((p >> 16) & 0xFF) != c[2];
        }
    return n;
}

// The differing pixels' bounding box (test messages).
std::string diffBounds(const gggame::GameFixture& f, const std::filesystem::path& png, const std::vector<Box>& boxes = {}) {
    const auto bytes = readWholeFile(png);
    if (!bytes) return "no capture";
    const auto img = mods::decodePng(*bytes);
    const int step = img.width == 640 ? 2 : 1;
    const std::vector<uint32_t> frame = f.frame();
    int x0 = 999, y0 = 999, x1 = -1, y1 = -1;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            bool in = boxes.empty();
            for (const Box& b : boxes) in = in || (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h);
            if (!in) continue;
            const uint32_t p = frame[static_cast<size_t>(y) * 320 + static_cast<size_t>(x)];
            const uint8_t* c = &img.rgba[(static_cast<size_t>(y * step) * img.width + static_cast<size_t>(x * step)) * 4];
            if ((p & 0xFF) != c[0] || ((p >> 8) & 0xFF) != c[1] || ((p >> 16) & 0xFF) != c[2]) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
        }
    return x1 < 0 ? "none" : std::format("({}, {})..({}, {})", x0, y0, x1, y1);
}

int differingIn(const gggame::GameFixture& f, const char* capture, const std::vector<Box>& boxes = {}) {
    const auto p = testdata::ggCapture(capture);
    if (!p) return -1;
    const int n = differingIn(f, *p, boxes);
    if (n != 0) MESSAGE(capture << ": " << n << " px differ in " << diffBounds(f, *p, boxes));
    return n;
}

uint64_t hashOf(std::span<const uint8_t> b) { return fnv1a64(b); }

uint64_t fileHash(const std::filesystem::path& p) {
    const auto b = readWholeFile(p);
    return b ? hashOf(*b) : 0;
}

// The byte offsets where two files differ.
std::vector<size_t> fileDiff(const std::filesystem::path& a, const std::filesystem::path& b) {
    const auto x = readWholeFile(a), y = readWholeFile(b);
    std::vector<size_t> out;
    if (!x || !y) return {SIZE_MAX};
    for (size_t i = 0; i < std::max(x->size(), y->size()); ++i)
        if (i >= x->size() || i >= y->size() || (*x)[i] != (*y)[i]) out.push_back(i);
    return out;
}

int countCalls(const RecordingSound& s, const std::string& op) {
    return static_cast<int>(std::count_if(s.calls().begin(), s.calls().end(), [&](const auto& c) { return c.op == op; }));
}

// The pointer (the cursor's top-left, game pixels) before a function runs.
void pointerAt(gggame::GameFixture& f, int x, int y) {
    f.g().display.flush();  // a move is dropped while a frame is drawing (g_mouseHideState): end it first
    f.runtime().hostPointer(static_cast<float>(x), static_cast<float>(y));
    CHECK(f.g().mouse().x() == x);
    CHECK(f.g().mouse().y() == y);
}

// The sign-in's globals against a DOSBox-X state of the same moment: g_rosterIndex, g_typedName (+ DS:a064),
// g_typedLen, the vestiges, g_saveDisabled, the scroll bar's pos / max / thumb y (its repeat stamp is a tick
// of the emulator's clock) and the roster's heap copy.
void checkSignInGlobals(const GgGame& g, const char* savName) {
    CAPTURE(savName);
    const auto sav = testdata::ggCapture(savName);
    if (!sav) return;
    const GgStateFile s = GgStateFile::load(*sav);
    const UiState& u = g.ui;
    CHECK(u.rosterIndex == static_cast<int16_t>(s.dsWord(dg::kRosterIndex)));
    CHECK(u.typedLen == static_cast<int16_t>(s.dsWord(dg::kTypedLen)));
    for (int i = 0; i < 16; ++i) CHECK(u.typedName[static_cast<size_t>(i)] == s.dsByte(static_cast<uint16_t>(dg::kTypedName + i)));
    CHECK(u.typedName16 == s.dsByte(0xA064));
    CHECK(u.signinArrowRepeat == static_cast<int16_t>(s.dsWord(dg::kSigninArrowRepeat)));
    CHECK(u.signinArrowCount == static_cast<int16_t>(s.dsWord(dg::kSigninArrowCount)));
    CHECK(u.signinClicked == static_cast<int16_t>(s.dsWord(dg::kSigninClicked)));
    CHECK(u.signinLastClick == s.dsDword(dg::kSigninLastClick));
    CHECK(u.signinPrevClick == s.dsDword(dg::kSigninPrevClick));
    CHECK(u.saveDisabled == static_cast<int16_t>(s.dsWord(dg::kSaveDisabled)));
    CHECK(u.rosterScrollBar.pos == static_cast<int16_t>(s.dsWord(dg::kRosterScrollBar + 8)));
    CHECK(u.rosterScrollBar.max == static_cast<int16_t>(s.dsWord(dg::kRosterScrollBar + 0xA)));
    CHECK(u.rosterScrollBar.thumbY == static_cast<int16_t>(s.dsWord(dg::kRosterScrollBar + 0x12)));
    const uint32_t at = s.farPointerAt(dg::kRoster);
    const auto bytes = s.bytesAt(at, kRosterSize);
    REQUIRE(bytes.size() == kRosterSize);
    CHECK(u.roster.bytes() == std::vector<uint8_t>(bytes.begin(), bytes.end()));
    // Every other UI global of the dump (the dialog layout, the menu words, the text viewer's) as well.
    std::vector<DgroupField> fields;
    for (const DgroupField& fl : gameStateFields()) {
        const std::string_view n = fl.name;
        if (n == "g_savedTicks" || n == "g_rosterScrollBar" || n == "g_hintSlots") continue;  // tick stamps
        if (n.starts_with("g_dlg") || n.starts_with("g_signin") || n.starts_with("g_typed") || n.starts_with("DS_a064") ||
            n.starts_with("g_roster") || n.starts_with("g_menu") || n.starts_with("g_view") || n == "g_saveDisabled")
            fields.push_back(fl);
    }
    CHECK(fields.size() >= 25);
    const std::vector<uint8_t> image = dumpToDgroup(g, s.dgroup());
    const auto diff = diffDgroup(image, s.dgroup(), &fields);
    CHECK(diff.empty());
    for (const auto n : diff) MESSAGE("differs: " << std::string(n));
}

}  // namespace

// ------------------------------------------------------------------------------------------- sign-in

TEST_CASE("ui3: the w0 sign-in: an empty roster, 'Ada' typed, With Chimps (w0/03, 04, 05, the PLAYERS.DAT write)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto before = testdata::ggCapture("savestates/w0/03_signin.players");
    const auto after = testdata::ggCapture("savestates/w0/06_hub.players");
    REQUIRE(before);
    REQUIRE(after);
    gggame::GameFixture f(*ggFiles);
    REQUIRE(f.loadPlayers(*before));
    CHECK(f.g().mouse().x() == 160);  // the driver's reset position, as in w0/03 and 04
    f.polls(true);
    f.start([](GgGame& g) { state02SignIn(g); });
    runTicks(f, 200);  // the screen, its fade-in, the empty roster's automatic New Player
    REQUIRE_FALSE(f.done());
    CHECK(differingIn(f, "w0/03_signin.png") == 0);
    checkSignInGlobals(f.g(), "savestates/w0/03_signin.sav");
    CHECK(f.g().ui.roster.count == 1);  // the -2 entry New Player inserted
    CHECK(f.g().ui.typedLen == 0);

    REQUIRE(f.keys("1:tap:a, 20:tap:d, 40:tap:a"));
    runTicks(f, 80);
    CHECK(differingIn(f, "w0/04_signin_name.png") == 0);
    CHECK(f.g().ui.typedLen == 3);
    CHECK(countCalls(f.sound(), "playEffect") == 3);  // effect 0 of 0x102f per accepted character

    REQUIRE(f.mouse("1:click:164:180"));  // Start Game
    runTicks(f, 40);
    CHECK(differingIn(f, "w0/05_chimps_dialog.png") == 0);
    const uint32_t frozen = f.g().ticks();

    REQUIRE(f.mouse("10:click:93:137"));  // With Chimps
    runToEnd(f);
    REQUIRE(f.done());
    f.finish();
    CHECK(f.g().ticks() >= frozen);
    const GgGame& g = f.g();
    CHECK(g.player.nameString() == std::string("Ad") + "a");
    CHECK(g.player.withChimps == 1);
    CHECK(g.player.slot == 98);  // the creation loop's anomaly: the first new player gets slot 98
    CHECK(g.ui.rosterIndex == 0);
    CHECK(f.players().writes() >= 2);
    // Players_SaveCurrent then Roster_Save: the file equals the copy DOSBox-X kept after the hub appeared.
    const auto diff = fileDiff(f.players().path(), *after);
    CHECK(diff.empty());
    if (!diff.empty()) MESSAGE("first differing byte: " << diff.front() << " of " << diff.size());
    CHECK(countCalls(f.sound(), "loadEffectBank") == 1);
    CHECK(countCalls(f.sound(), "freeEffectBank") == 1);
    CHECK(countCalls(f.sound(), "pause") == 1);   // the chimps box
    CHECK(countCalls(f.sound(), "resume") == 1);
}

TEST_CASE("ui3: r2/player2: a second player 'Bob' Without Chimps (02, 03, 04, 05 and the PLAYERS.DAT write)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    // From the hub with Ada signed in (the state's globals: g_typedName still holds the name typed at the
    // previous sign-in, the original never clears it) through the sign-in booth (state 2).
    const auto hub = testdata::ggCapture("savestates/r2/player2/01_hub_ada.sav");
    const auto after = testdata::ggCapture("savestates/r2/player2/06_hub_bob.players");
    REQUIRE(hub);
    REQUIRE(after);
    gggame::GameFixture f(*ggFiles);
    REQUIRE(f.loadState(*hub));
    pointerAt(f, 60, 150);
    f.polls(true);
    f.start([](GgGame& g) { state02SignIn(g); });
    runTicks(f, 200);
    CHECK(differingIn(f, "r2/player2/02_signin_roster.png") == 0);
    checkSignInGlobals(f.g(), "savestates/r2/player2/02_signin_roster.sav");
    CHECK(f.g().ui.roster.count == 1);

    REQUIRE(f.mouse("1:click:219:180"));  // New Player
    runTicks(f, 20);
    CHECK(differingIn(f, "r2/player2/03_new_player.png") == 0);
    CHECK(f.g().ui.roster.count == 2);

    REQUIRE(f.keys("1:tap:b, 20:tap:o, 40:tap:b"));
    runTicks(f, 80);
    CHECK(differingIn(f, "r2/player2/04_name_typed.png") == 0);
    checkSignInGlobals(f.g(), "savestates/r2/player2/04_name_typed.sav");

    REQUIRE(f.mouse("1:click:164:180"));  // Start Game
    runTicks(f, 40);
    CHECK(differingIn(f, "r2/player2/05_chimps_dialog.png") == 0);

    REQUIRE(f.keys("1:tap:n"));  // Without Chimps (its key)
    runToEnd(f);
    REQUIRE(f.done());
    f.finish();
    CHECK(f.g().player.withChimps == 0);
    CHECK(f.g().player.slot == 1);  // entry 1 of the shipped index holds slot 1
    CHECK(f.g().ui.rosterIndex == 1);
    const auto diff = fileDiff(f.players().path(), *after);
    CHECK(diff.empty());
    if (!diff.empty()) MESSAGE("first differing byte: " << diff.front() << " of " << diff.size());
}

TEST_CASE("ui3: r2/resume: Bob selected with a click and signed in from his record (05, 06; nothing written)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto before = testdata::ggCapture("savestates/r2/resume/05_signin_after_restart.players");
    REQUIRE(before);
    gggame::GameFixture f(*ggFiles);
    REQUIRE(f.loadPlayers(*before));
    const uint64_t fileBefore = fileHash(f.players().path());
    const int writes = f.players().writes();
    pointerAt(f, 160, 100);
    f.polls(true);
    f.start([](GgGame& g) { state02SignIn(g); });
    runTicks(f, 200);
    CHECK(differingIn(f, "r2/resume/05_signin.png") == 0);
    REQUIRE(f.mouse("1:click:100:62"));  // Bob's row
    runTicks(f, 20);
    CHECK(differingIn(f, "r2/resume/06_bob_selected.png") == 0);
    CHECK(f.g().ui.rosterIndex == 1);
    REQUIRE(f.mouse("1:click:164:180"));  // Start Game
    runToEnd(f);
    REQUIRE(f.done());
    f.finish();
    const GgGame& g = f.g();
    CHECK(g.player.nameString() == std::string("Bo") + "b");
    CHECK(g.player.points == 1720);
    CHECK(f.players().writes() == writes);  // Players_LoadRecord only
    CHECK(fileHash(f.players().path()) == fileBefore);
    // The record's sound option became the live switch (DS:1ad4 = g_soundOff, applied).
    const auto& calls = f.sound().calls();
    CHECK(std::any_of(calls.begin(), calls.end(), [&](const auto& c) {
        return c.op == "setSoundMuted" && c.args.size() == 1 && c.args[0] == g.player.soundOff;
    }));
}

TEST_CASE("ui3: Ctrl+R removes a player and writes the index at once; Esc asks Save & Exit with saving disabled") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto before = testdata::ggCapture("savestates/r2/player2/02_signin_roster.players");
    REQUIRE(before);
    {
        gggame::GameFixture f(*ggFiles);
        REQUIRE(f.loadPlayers(*before));
        const auto fileBefore = readWholeFile(f.players().path());
        REQUIRE(fileBefore);
        const int writes = f.players().writes();
        f.polls(true);
        f.start([](GgGame& g) { state02SignIn(g); });
        runTicks(f, 200);
        const auto lag = [&f] { return static_cast<int64_t>(f.runtime().clock().interrupts()) - f.g().ticks(); };
        const int64_t lag0 = lag();
        REQUIRE(f.keys("1:down:lctrl, 3:tap:r, 6:up:lctrl"));  // Ctrl+R on Ada: the Yes/No box 0x106a
        runTicks(f, 60);
        REQUIRE(f.keys("1:tap:y"));
        runTicks(f, 60);
        CHECK(lag() - lag0 >= 55);  // the box's time was taken back (g_ticks = g_savedTicks at its exit)
        CHECK(f.g().ui.roster.count == 0);
        CHECK(f.players().writes() == writes + 1);
        CHECK(f.players().lastMoment() == GgSaveMoment::RemovePlayer);
        f.finish();
        const auto fileAfter = readWholeFile(f.players().path());
        REQUIRE(fileAfter);
        // Only the header and index changed: the count, and entry 0 keeps the removed name with points -1
        // and its slot (the record stays in slot 98).
        size_t outside = 0, inIndex = 0;
        for (size_t i = 0; i < fileAfter->size(); ++i) {
            if ((*fileAfter)[i] == (*fileBefore)[i]) continue;
            (i < kRosterSize ? inIndex : outside) += 1;
        }
        CHECK(outside == 0);
        CHECK(inIndex > 0);
        CHECK(((*fileAfter)[6] | ((*fileAfter)[7] << 8)) == 0);
    }
    {
        gggame::GameFixture f(*ggFiles);
        REQUIRE(f.loadPlayers(*before));
        const uint64_t fileBefore = fileHash(f.players().path());
        const int writes = f.players().writes();
        f.polls(true);
        f.start([](GgGame& g) { state02SignIn(g); });
        runTicks(f, 200);
        REQUIRE(f.keys("1:tap:esc"));  // Save & Exit (default No)
        runTicks(f, 40);
        CHECK(f.g().ui.saveDisabled == 1);
        REQUIRE(f.keys("1:tap:n"));
        runTicks(f, 40);
        CHECK(f.g().ui.saveDisabled == 0);
        CHECK_FALSE(f.done());
        REQUIRE(f.keys("1:tap:esc, 30:tap:y"));  // Yes: exit(0) without writing anything
        runToEnd(f, 200);
        REQUIRE(f.done());
        f.finish();
        CHECK(f.exitCode() == std::optional<int>(0));
        CHECK(f.players().writes() == writes);
        CHECK(fileHash(f.players().path()) == fileBefore);
    }
}

namespace {

// A PLAYERS.DAT with `n` players named "Paa", "Pab", ... (name order) in the shipped layout's free slots.
std::filesystem::path rosterFile(const char* dirName, int n) {
    PlayersDat d = PlayersDat::create();
    for (int i = 0; i < n; ++i) {
        RosterEntry& e = d.roster.entries[static_cast<size_t>(i)];
        PlayerRecord& r = d.records.at(e.slot);
        r.setName(std::format("P{:c}{:c}", 'a' + i / 26, 'a' + i % 26));
        r.points = static_cast<uint32_t>(100 * i);
        r.slot = static_cast<int16_t>(e.slot);
        e = RosterEntry::ofRecord(r);
    }
    d.roster.count = static_cast<uint16_t>(n);
    const auto dir = testdata::scratchDir(dirName);
    const auto path = dir / "PLAYERS.DAT";
    const auto bytes = d.bytes();
    REQUIRE(writeWholeFile(path, bytes));
    return path;
}

}  // namespace

TEST_CASE("ui3: the sign-in's scroll bar on a long roster: arrows, repeat, page, drag, the thumb click quirk") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    REQUIRE(f.loadPlayers(rosterFile("gg_ui3_roster12", 12)));
    f.polls(true);
    f.start([](GgGame& g) { state02SignIn(g); });
    runTicks(f, 200);
    const ScrollBar& bar = f.g().ui.rosterScrollBar;
    REQUIRE(bar.max == 4);  // 12 players, 8 rows
    CHECK(bar.pos == 0);
    // The down arrow (the bar's last 11 rows): the first poll of the press acts at once.
    const int downY = bar.y + bar.h - 5, x = bar.x + 5;
    REQUIRE(f.mouse(std::format("1:click:{}:{}", x, downY)));
    runTicks(f, 10);
    CHECK(f.g().ui.rosterScrollBar.pos == 1);
    // Held: a repeat when g_ticks has passed the deadline (27 ticks), clamped at the end.
    REQUIRE(f.mouse(std::format("1:move:{}:{}, 2:buttons:1, 70:buttons:0", x, downY)));
    runTicks(f, 80);
    CHECK(f.g().ui.rosterScrollBar.pos == 4);
    // The up arrow, then the track below the thumb: a page (8) clamped.
    REQUIRE(f.mouse(std::format("1:click:{}:{}", x, bar.y + 3)));
    runTicks(f, 10);
    CHECK(f.g().ui.rosterScrollBar.pos == 3);
    REQUIRE(f.mouse(std::format("1:click:{}:{}", x, bar.y + 12)));  // above the thumb: a page up
    runTicks(f, 10);
    CHECK(f.g().ui.rosterScrollBar.pos == 0);
    // A drag of the thumb to the middle of the track.
    const int thumbY = f.g().ui.rosterScrollBar.thumbY;
    REQUIRE(f.mouse(std::format("1:move:{}:{}, 2:buttons:1, 4:move:{}:{}, 6:move:{}:{}, 8:buttons:0", x, thumbY + 3, x,
                                thumbY + 20, x, thumbY + 40)));
    runTicks(f, 20);
    const int afterDrag = f.g().ui.rosterScrollBar.pos;
    CHECK(afterDrag > 0);
    CHECK(afterDrag < 4);
    // A click on the thumb without a move: the drag's end divides the input code (the original's DI):
    // the list scrolls to the end (kept, code_ui.md §9).
    const int thumbY2 = f.g().ui.rosterScrollBar.thumbY;
    REQUIRE(f.mouse(std::format("1:move:{}:{}, 3:buttons:1, 6:buttons:0", x, thumbY2 + 3)));
    runTicks(f, 20);
    CHECK(f.g().ui.rosterScrollBar.pos == 4);
    // Scrolling does not move the selection.
    CHECK(f.g().ui.rosterIndex == 0);
    f.finish();
}

TEST_CASE("ui3: the duplicate name and the list-full boxes") {
    GG_REQUIRE_DATA();
    {
        // "Paa" exists: New Player, "paa", Enter -> the duplicate box 0x1068 (the next entry has the name).
        gggame::GameFixture f(*ggFiles);
        REQUIRE(f.loadPlayers(rosterFile("gg_ui3_roster3", 3)));
        f.polls(true);
        f.start([](GgGame& g) { state02SignIn(g); });
        runTicks(f, 200);
        REQUIRE(f.mouse("1:click:219:180"));  // New Player
        runTicks(f, 20);
        REQUIRE(f.keys("1:tap:p, 20:tap:a, 40:tap:a, 60:tap:enter"));
        runTicks(f, 100);
        CHECK_FALSE(f.done());
        CHECK(f.g().ui.roster.count == 4);
        CHECK(f.g().ui.rosterIndex == 0);  // the new entry sorts before the old "Paa"
        CHECK(countCalls(f.sound(), "pause") == 1);  // the box is open
        REQUIRE(f.keys("1:tap:enter"));  // the box closes; the sign-in goes on
        runTicks(f, 30);
        CHECK_FALSE(f.done());
        CHECK(countCalls(f.sound(), "resume") == 1);
        f.finish();
    }
    {
        // 99 players: New Player shows the list-full box 0x1069 and adds nothing.
        gggame::GameFixture f(*ggFiles);
        REQUIRE(f.loadPlayers(rosterFile("gg_ui3_roster99", 99)));
        f.polls(true);
        f.start([](GgGame& g) { state02SignIn(g); });
        runTicks(f, 200);
        REQUIRE(f.mouse("1:click:219:180"));
        runTicks(f, 30);
        CHECK(countCalls(f.sound(), "pause") == 1);
        REQUIRE(f.keys("1:tap:enter"));
        runTicks(f, 30);
        CHECK(f.g().ui.roster.count == 99);
        CHECK(countCalls(f.sound(), "resume") == 1);
        f.finish();
    }
}

// --------------------------------------------------------------------------------- the boxes

TEST_CASE("ui3: Dlg_RunModal: keys, clicks, the focus, Esc rules, time frozen and the sound paused") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    f.polls(true);
    int answer = 99;
    const auto run = [&](std::function<int(GgGame&)> box, const char* script) {
        answer = 99;
        f.sound().clear();
        const uint32_t t0 = f.g().ticks();
        const uint64_t i0 = f.runtime().clock().interrupts();
        f.start([&](GgGame& g) { answer = box(g); });
        runTicks(f, 2);
        REQUIRE(f.keys(script));
        runToEnd(f, 400);
        REQUIRE(f.done());
        f.finish();
        CHECK(f.g().ticks() == t0);                                // time stood still in the box
        CHECK(f.runtime().clock().interrupts() > i0);             // while the PIT ran
        CHECK(countCalls(f.sound(), "pause") == 1);
        CHECK(countCalls(f.sound(), "resume") == 1);
        CHECK(f.g().zones().depth() == 0);
        CHECK(f.g().zones().local() == nullptr);
    };
    const auto saveExit = [](GgGame& g) { return dlgYesNoRes(g, kMsgSaveAndExit); };  // default No
    run(saveExit, "1:tap:enter");
    CHECK(answer == 0);
    run(saveExit, "1:tap:left, 10:tap:enter");
    CHECK(answer == 1);
    run(saveExit, "1:tap:right, 10:tap:right, 20:tap:enter");  // wraps: No -> Yes -> No
    CHECK(answer == 0);
    run(saveExit, "1:down:lshift, 2:tap:y, 4:up:lshift");  // upper case works (tolower)
    CHECK(answer == 1);
    run(saveExit, "1:tap:esc");
    CHECK(answer == -1);
    // A click on the footer line is Esc (DS:3dd4: the box's width, 10 rows at the footer's y).
    f.start([&](GgGame& g) { answer = dlgYesNoRes(g, kMsgSaveAndExit); });
    runTicks(f, 2);
    {
        const UiState& u = f.g().ui;
        REQUIRE(f.mouse(std::format("1:click:{}:{}", u.dlgX + 30, u.dlgFooterY + 4)));
    }
    runToEnd(f, 400);
    f.finish();
    CHECK(answer == -1);
    // The chimps question: Esc is disabled; a click on Without Chimps (its release over the button).
    const auto chimps = [](GgGame& g) { return dlgAskChimps(g); };
    run(chimps, "1:tap:esc, 20:tap:n");
    CHECK(answer == 0);
    run(chimps, "1:tap:enter");
    CHECK(answer == 1);
    // A message box: Enter answers 0, Esc -1; Ctrl+S inside a box toggles the sound (Input_GetCode's key).
    const auto message = [](GgGame& g) { return dlgMessageRes(g, 0x1068); };
    const int16_t soundOff = f.g().player.soundOff;
    run(message, "1:down:lctrl, 3:tap:s, 6:up:lctrl, 20:tap:enter");
    CHECK(answer == 0);
    CHECK(countCalls(f.sound(), "toggleSound") == 1);
    CHECK(f.g().player.soundOff == (soundOff ^ 1));
    run(message, "1:tap:esc");
    CHECK(answer == -1);
}

TEST_CASE("ui3: the help pager: page 0 (r2/menus/03), PgDn / PgUp, Done, Esc; time frozen per page") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto sav = testdata::ggCapture("savestates/r2/menus/03_help_topic1.sav");
    REQUIRE(sav);
    gggame::GameFixture f(*ggFiles);
    REQUIRE(f.loadState(*sav));
    f.present();
    const std::vector<uint32_t> start = f.frame();
    pointerAt(f, 160, 17);
    f.polls(true);
    int answer = 99;
    const uint32_t t0 = f.g().ticks();
    f.start([&](GgGame& g) { answer = helpShowTopic(g, kHelpAboutGame); });
    runTicks(f, 4);
    CHECK(differingIn(f, "r2/menus/03_help_topic1.png", {{19, 14, 282, 177}}) == 0);
    // See Next Page, See Next Page, Go Back a Page, See Next Page x 2 (the last page), Done.
    REQUIRE(f.keys("1:tap:pgdn, 30:tap:pgdn, 60:tap:pgup, 90:tap:pgdn, 120:tap:pgdn, 150:tap:pgdn"));
    runToEnd(f, 600);
    REQUIRE(f.done());
    f.finish();
    CHECK(answer == 1);
    CHECK(f.g().ticks() == t0);
    // The area is restored once at the end (the cursor aside).
    const std::vector<uint32_t> end = f.frame();
    int changed = 0;
    for (int y = 14; y < 14 + 177; ++y)
        for (int x = 19; x < 19 + 282; ++x)
            if (!(x >= 150 && x < 175 && y >= 10 && y < 35))
                changed += start[static_cast<size_t>(y) * 320 + x] != end[static_cast<size_t>(y) * 320 + x];
    CHECK(changed == 0);
    // Esc on the first page: 0.
    f.start([&](GgGame& g) { answer = helpShowTopic(g, kHelpAboutGame); });
    runTicks(f, 4);
    REQUIRE(f.keys("1:tap:esc"));
    runToEnd(f, 200);
    f.finish();
    CHECK(answer == 0);
    // A one-page topic: Done (PgDn) is 1.
    f.start([&](GgGame& g) { answer = helpShowTopic(g, kHelpSolvingPuzzles); });
    runTicks(f, 4);
    REQUIRE(f.keys("1:tap:pgdn"));
    runToEnd(f, 200);
    f.finish();
    CHECK(answer == 1);
}

// --------------------------------------------------------------------------------- the menu bar

namespace {

// The bar and the open drop-down (or box) of the last layout: g_dlgX .. g_dlgH.
std::vector<Box> barAndBox(const GgGame& g) { return {{0, 0, 320, 9}, {g.ui.dlgX, g.ui.dlgY, g.ui.dlgW, g.ui.dlgH}}; }
std::vector<Box> lastBox(const GgGame& g) { return {{g.ui.dlgX, g.ui.dlgY, g.ui.dlgW, g.ui.dlgH}}; }

// A room of r2/menus with the bar installed (the state is inside the Help drop-down: its nesting is
// cleared; the bar is drawn again over the state's screen).
void menusRoom(gggame::GameFixture& f) {
    const auto sav = testdata::ggCapture("savestates/r2/menus/02_help_menu.sav");
    REQUIRE(sav);
    REQUIRE(f.loadState(*sav));
    f.g().ui.menuNesting = 0;
    menuSetMenuBar(f.g(), true);
    f.present();
}

// The layout globals of the last box (Dlg_Layout's g_dlgX .. g_dlgFooterY, the title's with a title) and
// g_menuNesting against the DOSBox-X state of the same moment.
void checkLayoutGlobals(const GgGame& g, const char* savName, bool title) {
    CAPTURE(savName);
    const auto sav = testdata::ggCapture(savName);
    if (!sav) return;
    const GgStateFile s = GgStateFile::load(*sav);
    std::vector<DgroupField> fields;
    for (const DgroupField& fl : gameStateFields()) {
        const std::string_view n = fl.name;
        if (n == "g_dlgPicX" || n == "g_dlgPicY") continue;
        if (!title && (n == "g_dlgTitleX" || n == "g_dlgTitleY")) continue;
        if (n.starts_with("g_dlg") || n == "g_menuNesting") fields.push_back(fl);
    }
    const std::vector<uint8_t> image = dumpToDgroup(g, s.dgroup());
    const auto diff = diffDgroup(image, s.dgroup(), &fields);
    CHECK(diff.empty());
    for (const auto n : diff) {
        for (const DgroupField& fl : fields)
            if (fl.name == n)
                MESSAGE(std::string(savName) << ": " << std::string(n) << " engine " << (image[fl.ds] | (image[fl.ds + 1u] << 8))
                                             << " state " << s.dsWord(fl.ds));
    }
}

}  // namespace

TEST_CASE("ui3: the Options and Help drop-downs, Sound & Music, New Player Hints, a help topic (r2/menus 01-05)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    menusRoom(f);
    CHECK(f.g().zones().depth() == 0);
    CHECK(f.g().input().menuHandler() != nullptr);
    f.polls(true);

    // Options (r2/menus/01): time runs in a drop-down, the sound is paused.
    pointerAt(f, 80, 4);
    f.sound().clear();
    const uint32_t t0 = f.g().ticks();
    f.start([](GgGame& g) { menuRunDropDown(g, kMenuOptions); });
    runTicks(f, 6);
    CHECK(differingIn(f, "r2/menus/01_options_menu.png", barAndBox(f.g())) == 0);
    CHECK(f.g().ui.menuNesting == 1);
    CHECK(f.g().zones().depth() == 1);
    CHECK(f.g().input().menuHandler() == nullptr);
    // Sound & Music (row 0, Enter): the Yes/No box 0x1065 (r2/menus/04); No -> the sound off.
    REQUIRE(f.mouse("1:move:100:16, 3:tap:enter"));
    runTicks(f, 30);
    CHECK(differingIn(f, "r2/menus/04_sound_music_dialog.png", lastBox(f.g())) == 0);
    checkLayoutGlobals(f.g(), "savestates/r2/menus/04_sound_music_dialog.sav", true);
    REQUIRE(f.keys("1:tap:n"));
    runToEnd(f, 300);
    REQUIRE(f.done());
    f.finish();
    CHECK(f.g().ticks() > t0);  // the drop-down's time ran
    CHECK(f.g().player.soundOff == 1);
    REQUIRE_FALSE(f.sound().calls().empty());
    CHECK(f.sound().calls().back().op == "resume");
    CHECK(countCalls(f.sound(), "setSoundMuted") == 1);
    CHECK(countCalls(f.sound(), "pause") == 2);  // the drop-down's and the box's (not nested)
    CHECK(f.g().ui.menuNesting == 0);
    CHECK(f.g().zones().depth() == 0);
    CHECK(f.g().input().menuHandler() != nullptr);  // the drop-down sets the hook at its exit

    // New Player Hints (row 1; r2/menus/05): Yes -> hints off.
    f.start([](GgGame& g) { menuRunDropDown(g, kMenuOptions); });
    runTicks(f, 6);
    REQUIRE(f.keys("1:tap:down, 10:tap:enter"));
    runTicks(f, 40);
    CHECK(differingIn(f, "r2/menus/05_hints_dialog.png", lastBox(f.g())) == 0);
    checkLayoutGlobals(f.g(), "savestates/r2/menus/05_hints_dialog.sav", true);
    REQUIRE(f.keys("1:tap:y"));
    runToEnd(f, 300);
    f.finish();
    CHECK(f.g().player.hintsOff == 1);

    // Help (r2/menus/02), About the Game (r2/menus/03); Done reopens the Help menu, Esc closes it.
    pointerAt(f, 136, 4);
    f.start([](GgGame& g) { menuRunDropDown(g, kMenuHelp); });
    runTicks(f, 6);
    CHECK(differingIn(f, "r2/menus/02_help_menu.png", barAndBox(f.g())) == 0);
    checkLayoutGlobals(f.g(), "savestates/r2/menus/02_help_menu.sav", false);
    REQUIRE(f.mouse("1:move:160:17, 3:tap:enter"));
    runTicks(f, 30);
    CHECK(differingIn(f, "r2/menus/03_help_topic1.png", {{19, 14, 282, 177}}) == 0);
    checkLayoutGlobals(f.g(), "savestates/r2/menus/03_help_topic1.sav", true);
    REQUIRE(f.keys("1:tap:pgdn, 30:tap:pgdn, 60:tap:pgdn, 90:tap:pgdn"));
    runTicks(f, 130);
    CHECK_FALSE(f.done());
    CHECK(f.g().ui.menuNesting == 1);  // the Help menu again
    REQUIRE(f.keys("1:tap:esc"));
    runToEnd(f, 300);
    REQUIRE(f.done());
    f.finish();
    CHECK(f.g().ui.menuNesting == 0);
}

TEST_CASE("ui3: F2 through Input_GetCode's hook: the File menu (w0/23), Return to Technology Center (w0/24)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto sav = testdata::ggCapture("savestates/w0/21_room_behind_door.sav");
    REQUIRE(sav);
    gggame::GameFixture f(*ggFiles);
    REQUIRE(f.loadState(*sav));
    std::string why;
    REQUIRE(uiRestoreFromState(f.g(), GgStateFile::load(*sav), why));
    CHECK(f.g().uiObjects().titleZonesLinked);
    REQUIRE(f.g().input().menuHandler() != nullptr);
    f.present();
    const std::vector<uint32_t> start = f.frame();
    pointerAt(f, 14, 4);
    f.polls(true);
    uint16_t last = 0;
    // A loop that polls like the warehouse's input fetch until it sees g_menuRequest's code.
    f.start([&last](GgGame& g) {
        for (;;) {
            const uint16_t c = g.rt.getInputCode();
            if (c == code::kMenuRequest1 || c == code::kMenuRequest2 || c == code::kEsc) {
                last = c;
                return;
            }
        }
    });
    REQUIRE(f.keys("1:tap:f2"));
    runTicks(f, 10);
    CHECK(differingIn(f, "w0/23_file_menu.png", {{0, 0, 320, 9}, {0, 9, 242, 54}}) == 0);
    CHECK(f.g().ui.dlgX == 0);
    CHECK(f.g().ui.dlgY == 9);
    CHECK(f.g().ui.dlgW == 242);
    CHECK(f.g().ui.dlgH == 54);
    REQUIRE(f.mouse("1:click:107:27"));  // the item Return to Technology Center (released over it)
    runTicks(f, 30);
    CHECK(differingIn(f, "w0/24_return_dialog.png", {{0, 0, 320, 9}, {5, 38, 310, 124}}) == 0);
    REQUIRE(f.mouse("1:click:221:133"));  // Yes
    runToEnd(f, 300);
    REQUIRE(f.done());
    f.finish();
    CHECK(last == code::kMenuRequest1);  // g_menuRequest 1 -> 0x17a2 from the Input_GetCode that ran the menu
    CHECK(f.g().input().menuRequest == 0);
    // The drop-down's and the box's areas are back (the cursor's corner aside).
    const std::vector<uint32_t> end = f.frame();
    int changed = 0;
    for (int y = 9; y < 9 + 54; ++y)
        for (int x = 0; x < 242; ++x)
            if (!(x >= 100 && x < 125 && y >= 20 && y < 45))
                changed += start[static_cast<size_t>(y) * 320 + static_cast<size_t>(x)] != end[static_cast<size_t>(y) * 320 + static_cast<size_t>(x)];
    CHECK(changed == 0);
}

TEST_CASE("ui3: Menu_SetMenuBar installs and removes the bar, its title zones and the hook") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    CHECK(g.ui.menuBarCur.off == 0);
    menuSetMenuBar(g, true);
    CHECK(g.ui.menuBarCur.off == dg::kMenuBar);
    CHECK(ClickZones::listOf(g.zones().global()).size() == 3);
    CHECK(g.input().menuHandler() != nullptr);
    menuSetMenuBar(g, false);
    CHECK(g.ui.menuBarCur.off == dg::kMenuBarNone);
    CHECK(ClickZones::listOf(g.zones().global()).empty());
    CHECK(g.input().menuHandler() == nullptr);
    menuRunDropDown(g, 0);  // no bar: nothing
    CHECK(g.ui.menuNesting == 0);
}

// --------------------------------------------------------------------------------- the text viewer

TEST_CASE("ui3: TextView_Run: the k0 hint text (r2/puzzle/k0_02), the arrows, the bar, Esc; time runs") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto sav = testdata::ggCapture("savestates/r2/puzzle/k0_screen.sav");
    REQUIRE(sav);
    gggame::GameFixture f(*ggFiles);
    REQUIRE(f.loadState(*sav));
    f.present();
    const std::vector<uint32_t> start = f.frame();
    const std::vector<uint8_t> text = f.g().archive().read(0x20DD);
    pointerAt(f, 44, 20);
    f.polls(true);
    const uint32_t t0 = f.g().ticks();
    f.start([&text](GgGame& g) { textViewRun(g, text::untilNul(text)); });
    runTicks(f, 4);
    // The whole frame but the HINT button, which the puzzle shows pressed in the capture, and the arrow
    // the state's screen buffer holds at (269, 150) (left by the puzzle's hint box; the state has no
    // save-under for it, so the engine cannot take it away when the pointer moves).
    const std::vector<gggame::Mask> masks = {{20, 10, 69, 31}, {269, 150, 277, 158}};
    const auto k0 = testdata::ggCapture("r2/puzzle/k0_02_hint_text.png");
    REQUIRE(k0);
    CHECK(f.differingPixels(*k0, masks) == 0);
    GgGame& g = f.g();
    REQUIRE(g.ui.viewScrollMax > 1);  // a long text: the bar
    CHECK(g.ui.viewThumbY == 45);
    CHECK(g.ui.viewThumbStep == 104 / g.ui.viewScrollMax);
    CHECK(g.zones().depth() == 1);
    // A click on the down arrow: its release flashes it for 7 ticks, a line down, the thumb a step down.
    REQUIRE(f.mouse("1:click:121:155"));
    runTicks(f, 20);
    CHECK(f.g().ui.viewThumbY == 45 + f.g().ui.viewThumbStep);
    // Holding the up arrow: the first repeat after 146 ticks, then every 29 (timer slot 6).
    REQUIRE(f.mouse("1:move:121:35, 2:buttons:1, 240:buttons:0"));
    runTicks(f, 260);
    CHECK(f.g().ui.viewThumbY == 45);
    // The track below the thumb: 10 steps down.
    REQUIRE(f.mouse(std::format("1:click:121:{}", 45 + 11 + 20)));
    runTicks(f, 10);
    CHECK(f.g().ui.viewThumbY > 45 + 5 * f.g().ui.viewThumbStep);
    REQUIRE(f.keys("1:tap:esc"));
    runToEnd(f, 100);
    REQUIRE(f.done());
    f.finish();
    CHECK(f.g().ticks() > t0 + 250);  // no freeze
    CHECK(f.g().zones().depth() == 0);
    CHECK(f.g().video.cursor().forceDefault(0) == 0);  // Mouse_ForceDefault(0) at the exit
    // The panel's area is back.
    const std::vector<uint32_t> end = f.frame();
    int changed = 0;
    for (int y = 28; y < 28 + 138; ++y)
        for (int x = 0; x < 129; ++x)
            if (!(x >= 110 && y >= 25 && y < 100))
                changed += start[static_cast<size_t>(y) * 320 + static_cast<size_t>(x)] != end[static_cast<size_t>(y) * 320 + static_cast<size_t>(x)];
    CHECK(changed == 0);
}

TEST_CASE("ui3: TextView_Run on the hint texts of gears, balance, launch and magnets (r2/puzzle k3, k4, k5, k7)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    struct Case {
        const char* state;
        const char* shot;
        uint16_t text;  // the puzzle's first hint text + variant 0
        int px, py;     // the pointer of the shot
    };
    for (const Case c : {Case{"savestates/r2/puzzle/k3_screen.sav", "r2/puzzle/k3_09_hint_text.png", 0x202B, 41, 31},
                         Case{"savestates/r2/puzzle/k4_screen.sav", "r2/puzzle/k4_09_hint_text.png", 0x212D, 24, 21},
                         Case{"savestates/r2/puzzle/k5_screen.sav", "r2/puzzle/k5_09_hint_text.png", 0x1163, 28, 21},
                         Case{"savestates/r2/puzzle/k7_screen.sav", "r2/puzzle/k7_09_hint_text.png", 0x11A7, 31, 20}}) {
        CAPTURE(c.shot);
        const auto sav = testdata::ggCapture(c.state);
        if (!sav) continue;
        gggame::GameFixture f(*ggFiles);
        REQUIRE(f.loadState(*sav));
        f.present();
        const std::vector<uint8_t> text = f.g().archive().read(c.text);
        pointerAt(f, c.px, c.py);
        f.polls(true);
        f.start([&text](GgGame& g) { textViewRun(g, text::untilNul(text)); });
        runTicks(f, 4);
        CHECK(differingIn(f, c.shot, {{0, 28, 129, 138}}) == 0);  // the panel, the arrows, the text, the bar
        REQUIRE(f.keys("1:tap:esc"));
        runToEnd(f, 100);
        CHECK(f.done());
        f.finish();
    }
}

// --------------------------------------------------------------------------------- hints

TEST_CASE("ui3: Hint_Show: the building hint (w0/08), the chime cut by the box, the seen flag; hints off") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto sav = testdata::ggCapture("savestates/w0/09_blueprint.sav");
    REQUIRE(sav);
    gggame::GameFixture f(*ggFiles);
    REQUIRE(f.loadState(*sav));
    f.present();
    GgGame& g = f.g();
    hintSeen(g.player, 0x1043) = 0;
    g.player.hintsOff = 0;
    pointerAt(f, 62, 85);
    f.polls(true);
    f.sound().clear();
    const uint32_t t0 = g.ticks();
    f.start([](GgGame& gg) { hintShow(gg, 0x1043); });
    runTicks(f, 6);
    CHECK(differingIn(f, "w0/08_building_hint.png", {{7, 48, 306, 104}}) == 0);
    REQUIRE(f.keys("1:tap:enter"));
    runToEnd(f, 200);
    REQUIRE(f.done());
    f.finish();
    CHECK(g.ticks() == t0);
    CHECK(hintSeen(g.player, 0x1043) == 1);
    const auto& calls = f.sound().calls();
    REQUIRE(calls.size() >= 5);
    CHECK(calls[0].op == "loadSong");
    REQUIRE(calls[0].args.size() == 2);
    CHECK(calls[0].args[0] == kHintChime);
    CHECK(calls[1].op == "playEffect");
    REQUIRE(calls[1].args.size() == 3);
    CHECK(calls[1].args[0] == calls[0].args[1]);
    CHECK(calls[1].args[1] == 0);
    CHECK(calls[1].args[2] == 0x32);
    CHECK(calls[2].op == "pause");
    CHECK(countCalls(f.sound(), "freeSong") == 1);
    // Shown once per player; with hints off nothing is shown but the hint is marked seen all the same.
    f.start([](GgGame& gg) { hintShow(gg, 0x1043); });
    f.step(1);
    CHECK(f.done());
    f.finish();
    g.player.hintsOff = 1;
    hintSeen(g.player, 0x1044) = 0;
    f.start([](GgGame& gg) { hintShow(gg, 0x1044); });
    f.step(1);
    CHECK(f.done());
    f.finish();
    CHECK(hintSeen(g.player, 0x1044) == 1);
}

TEST_CASE("ui3: Hint_Queue / Hint_Update: due strictly after the delay, cancel flags, at most one per call") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    g.player.hintsOff = 0;
    hintSeen(g.player, 0x1048) = 0;
    hintSeen(g.player, 0x1049) = 0;
    hintSeen(g.player, 0x1046) = 0;
    hintInit(g);
    // 0x1048 in 0x2238 ticks with its cancel flag DS:458c; 0x1046 with DS:458a.
    hintQueue(g, 0x1048, 0x2238, dg::kHintCancel1048);
    hintQueue(g, 0x1046, 0x490, dg::kHintCancel1046);
    CHECK(g.ui.hints.pendingIds() == std::vector<uint16_t>{0x1046, 0x1048});
    g.player.live.hintCancel1046 = 1;  // the player left through the tube
    f.runtime().waitTicks(0x491);
    hintUpdate(g);  // 0x1046 due and cancelled: dropped unseen, no box
    CHECK(g.ui.hints.pendingIds() == std::vector<uint16_t>{0x1048});
    CHECK(hintSeen(g.player, 0x1046) == 0);
    // Seen: not queued.
    hintSeen(g.player, 0x1049) = 1;
    hintQueue(g, 0x1049, 0);
    CHECK(g.ui.hints.pending() == 1);
    // 0x1048 is due only once g_ticks is past its time (strict).
    f.runtime().waitTicks(0x2238 - 0x491);
    hintUpdate(g);
    CHECK(g.ui.hints.pending() == 1);
    f.polls(true);
    f.start([](GgGame& gg) {
        gg.rt.waitTicks(2);
        hintUpdate(gg);  // the box of 0x1048
    });
    runTicks(f, 10);
    CHECK_FALSE(f.done());
    REQUIRE(f.keys("1:tap:enter"));
    runToEnd(f, 200);
    REQUIRE(f.done());
    f.finish();
    CHECK(g.ui.hints.pending() == 0);
    CHECK(hintSeen(g.player, 0x1048) == 1);
}

// --------------------------------------------------------------------------------- a DOSBox-X state

TEST_CASE("ui3: uiRestoreFromState: the bar of a room, a drop-down or a box refused, the sign-in's roster") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    struct Case {
        const char* sav;
        bool ok;
    };
    for (const Case c : {Case{"savestates/r2/menus/02_help_menu.sav", false}, Case{"savestates/r2/menus/04_sound_music_dialog.sav", false},
                         Case{"savestates/r2/menus/03_help_topic1.sav", false}, Case{"savestates/w0/14_warehouse.sav", true},
                         Case{"savestates/r2/puzzle/k0_screen.sav", true}, Case{"savestates/r2/puzzle/k0_hint_text.sav", false},
                         Case{"savestates/w0/06_hub.sav", true}, Case{"savestates/r2/player2/02_signin_roster.sav", true}}) {
        CAPTURE(c.sav);
        const auto sav = testdata::ggCapture(c.sav);
        if (!sav) continue;
        gggame::GameFixture f(*ggFiles);
        REQUIRE(f.loadState(*sav));
        const GgStateFile s = GgStateFile::load(*sav);
        std::string why;
        CHECK(uiRestoreFromState(f.g(), s, why) == c.ok);
        if (!c.ok) CHECK_FALSE(why.empty());
        if (c.ok && f.g().ui.menuBarCur.off == dg::kMenuBar) CHECK(ClickZones::listOf(f.g().zones().global()).size() >= 3);
        if (f.g().flow.state == 2) CHECK(f.g().ui.roster.count == f.players().roster().count);
    }
}
