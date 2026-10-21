// Screens area: title (1138) and Club Sign-In (1160). Pure parts (list formatting, ranks, the
// name buffer, the caret blink on a manual clock) run without game data; the scripted runs of
// the screens on a headless engine need TM_DATA.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "app/app_context.h"
#include "data/exe_tables.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/rules.h"
#include "game/screens_common.h"
#include "game/signin.h"
#include "game/title.h"
#include "input/win_msg.h"
#include "mischief/timing/clock.h"
#include "mischief/timing/deadline_timers.h"
#include "mischief/util/file_util.h"
#include "platform/engine.h"
#include "save/settings.h"
#include "save/users_file.h"
#include "test_data.h"
#include "text/font.h"
#include "text/text_renderer.h"
#include "ui/game_dialog.h"

using namespace tmport;
using namespace tmport::game;

// ------------------------------------------------------------------------------- pure parts

TEST_CASE("Sign-in: ULongToDec and the thousands commas of the totals (1160:0000 / 0cdb)") {
    CHECK(signin::ulongToDec(0) == "0");
    CHECK(signin::ulongToDec(7) == "7");
    CHECK(signin::ulongToDec(1234567) == "1234567");
    CHECK(signin::ulongToDec(4294967295u) == "4294967295");

    CHECK(signin::formatTotal(0) == "0");
    CHECK(signin::formatTotal(5) == "5");
    CHECK(signin::formatTotal(42) == "42");
    CHECK(signin::formatTotal(999) == "999");
    CHECK(signin::formatTotal(1000) == "1,000");
    CHECK(signin::formatTotal(12345) == "12,345");
    CHECK(signin::formatTotal(123456) == "123,456");
    CHECK(signin::formatTotal(1234567) == "1,234,567");
    CHECK(signin::formatTotal(9999999) == "9,999,999");
    // Quirk: 8 or more digits fall to the default case of the jump table: an empty string.
    CHECK(signin::formatTotal(10000000) == "");
    CHECK(signin::formatTotal(99999999) == "");
    CHECK(signin::formatTotal(4294967295u) == "");
}

TEST_CASE("Sign-in: rank column by total (Rank_FromTotal thresholds, 1160:0b1b)") {
    GameTables t;
    t.rankThresholds = {0, 5, 25, 70, 115, 170, 230, 300};  // the values of DS:0ea0
    auto mark = [&](uint32_t total) { return signin::rankMark(rankFromTotal(total, t)); };
    CHECK(mark(0).kind == signin::RankMark::Trainee);
    CHECK(mark(4).kind == signin::RankMark::Trainee);
    CHECK(mark(5).kind == signin::RankMark::Stars);
    CHECK(mark(5).stars == 1);
    CHECK(mark(24).stars == 1);
    CHECK(mark(25).stars == 2);
    CHECK(mark(229).stars == 5);
    CHECK(mark(230).stars == 6);
    CHECK(mark(299).kind == signin::RankMark::Stars);
    CHECK(mark(300).kind == signin::RankMark::Champion);
    CHECK(mark(4294967295u).kind == signin::RankMark::Champion);
}

TEST_CASE("Sign-in: row geometry, 8-bit row y, mouse row") {
    for (int r = 0; r <= 7; ++r) CHECK(signin::rowY(r, 25) == 57 + 25 * r);
    CHECK(signin::rowY(8, 25) == 1);  // (8*25+57) & 0xFF: computed in 8 bits (never reached)
    CHECK(signin::rowFromMouseY(57, 25) == 0);
    CHECK(signin::rowFromMouseY(81, 25) == 0);
    CHECK(signin::rowFromMouseY(82, 25) == 1);
    CHECK(signin::rowFromMouseY(231, 25) == 6);
    CHECK(signin::rowFromMouseY(56, 25) == 0);   // -1 / 25 truncates toward zero
    CHECK(signin::rowFromMouseY(30, 25) == -1);  // release outside: not clamped
    CHECK(signin::rowFromMouseYClamped(30, 25, 3) == 0);
    CHECK(signin::rowFromMouseYClamped(200, 25, 3) == 2);
}

TEST_CASE("Sign-in: Borland ctype test used for names") {
    const CType ct = CType::borland();
    for (unsigned c = 0; c < 0x100; ++c) CHECK(ct.printable(c) == (c >= 0x20 && c <= 0x7E));
    CHECK_FALSE(ct.printable(0x13));
    CHECK_FALSE(ct.printable(0x4B0));
    CHECK(ct.toUpper('a') == 'A');
    CHECK(ct.toUpper('z') == 'Z');
    CHECK(ct.toUpper('A') == 'A');
    CHECK(ct.toUpper('1') == '1');
    CHECK(ct.toUpper(0xE9) == 0xE9);  // no locale: high bytes stay
}

TEST_CASE("Sign-in: name buffer (g_playerName / g_nameCursor, 1160:0957)") {
    const CType ct = CType::borland();
    NameBuffer n;
    n.reset();
    CHECK(n.empty());
    CHECK(n.string() == "________");
    CHECK(n.caretIndex() == 0);
    CHECK_FALSE(n.backspace());

    CHECK(n.append('t', ct));
    n.upperFirst(ct);  // SignIn_DrawNameBox
    CHECK(n.string() == "T_______");
    CHECK_FALSE(n.append('_', ct));   // '_' marks an empty cell
    CHECK_FALSE(n.append(0x7F, ct));
    CHECK_FALSE(n.append(0xE9, ct));  // high bytes are not "printable"
    CHECK_FALSE(n.append(0x4B0, ct));
    for (char c : std::string("om Bo"))
        CHECK(n.append(static_cast<uint8_t>(c), ct));
    CHECK(n.string() == "Tom Bo__");
    CHECK(n.caretIndex() == 6);
    CHECK(n.append('b', ct));
    CHECK(n.append(' ', ct));
    CHECK(n.cursor == 8);
    CHECK_FALSE(n.append('x', ct));  // 8 characters at most
    CHECK(n.caretIndex() == 8);

    // Enter: trailing spaces become '_' and a NUL ends the name.
    n.trimForSubmit();
    CHECK(n.string() == "Tom Bob");
    CHECK(n.cursor == 7);
    // Duplicate: the NUL becomes '_' again.
    n.restoreAfterDuplicate();
    CHECK(n.string() == "Tom Bob_");
    CHECK(n.backspace());
    CHECK(n.string() == "Tom Bo__");

    NameBuffer m;
    m.reset();
    for (char c : std::string("Ab  ")) m.append(static_cast<uint8_t>(c), ct);
    m.trimForSubmit();
    CHECK(m.string() == "Ab");
    CHECK(m.cursor == 2);
    m.restoreAfterDuplicate();
    CHECK(m.string() == "Ab______");

    // Full 8 characters: the NUL goes into the 9th byte and stays after a duplicate.
    NameBuffer f;
    f.reset();
    for (char c : std::string("Abcdefgh")) f.append(static_cast<uint8_t>(c), ct);
    f.trimForSubmit();
    CHECK(f.string() == "Abcdefgh");
    f.restoreAfterDuplicate();
    CHECK(f.string() == "Abcdefgh");

    // The list's type-to-create stores the upper-cased key at the cursor.
    NameBuffer p;
    p.reset();
    p.put(ct.toUpper('q'));
    CHECK(p.string() == "Q_______");
    CHECK(p.cursor == 1);
}

TEST_CASE("Sign-in: caret blink every 333 ms on a manual clock (timer 0, XOR 15, no catch-up)") {
    mischief::ManualClock clock(5000);
    mischief::DeadlineTimers timers(&clock);
    CaretBlink caret;
    caret.start(timers);
    CHECK(caret.colour() == 0);  // black first
    std::vector<std::pair<uint64_t, int>> toggles;
    for (int ms = 1; ms <= 1000; ++ms) {
        clock.advance(1);
        if (caret.update(timers.expired(signin::kCaretSlot), timers)) toggles.emplace_back(clock.nowMs() - 5000, caret.colour());
    }
    REQUIRE(toggles.size() == 3);
    CHECK(toggles[0] == std::make_pair(uint64_t{333}, 15));
    CHECK(toggles[1] == std::make_pair(uint64_t{666}, 0));
    CHECK(toggles[2] == std::make_pair(uint64_t{999}, 15));

    // A late poll re-arms from the poll time: every later toggle moves (no catch-up).
    mischief::ManualClock c2(0);
    mischief::DeadlineTimers t2(&c2);
    CaretBlink k;
    k.start(t2);
    c2.set(400);
    CHECK(k.update(t2.expired(0), t2));
    CHECK(k.colour() == 15);
    c2.set(732);
    CHECK_FALSE(k.update(t2.expired(0), t2));
    c2.set(733);
    CHECK(k.update(t2.expired(0), t2));
    CHECK(k.colour() == 0);
}

// ------------------------------------------------------------------------ scripted screens

namespace {

std::filesystem::path tempDir(const char* name) {
    auto d = std::filesystem::temp_directory_path() / "tmport_screens_tests" / name;
    std::error_code ec;
    std::filesystem::remove_all(d, ec);
    std::filesystem::create_directories(d, ec);
    return d;
}

struct StopRun {};

// A headless engine on a manual clock with a script of window messages at given times.
struct Rig {
    mischief::ManualClock clock{1000};
    EngineConfig cfg;
    Engine engine;
    text::Font font;
    text::TextRenderer text;
    ui::DialogSystem dialogs;
    GameTables tables;
    GameState state;
    save::Paths paths;
    AppContext ctx;
    std::vector<std::pair<uint64_t, WinMsg>> script;
    std::vector<std::pair<uint64_t, std::function<void()>>> checks;
    size_t next = 0, nextCheck = 0;
    uint64_t start = 0;
    uint64_t limit = 400000;

    static EngineConfig makeCfg(mischief::ManualClock& c) {
        EngineConfig e;
        e.clock = &c;
        return e;
    }

    explicit Rig(GameFiles& g, const std::filesystem::path& dir)
        : cfg(makeCfg(clock)),
          engine(g, mischief::gfx::ColorMode::Colour256, cfg),
          font(text::Font::load(*g.res)),
          text(font, &engine.frame(), &engine.presenter()),
          dialogs(engine, font),
          tables(GameTables::fromExe(*g.exe)),
          state(GameState::initial(tables)),
          ctx{engine, g, font, text, dialogs} {
        paths.usersDat = dir / "users.dat";
        ctx.state = &state;
        ctx.paths = &paths;
        dialogs.hooks().getInputCode = [this]() -> uint16_t {
            const uint16_t c = engine.getInputCode();
            if (!c) clock.sleepMs(1);
            return c;
        };
        start = clock.nowMs();
        clock.onSleep = [this](uint64_t now) {
            const uint64_t t = now - start;
            while (next < script.size() && script[next].first <= t) engine.input().post(script[next++].second);
            while (nextCheck < checks.size() && checks[nextCheck].first <= t) checks[nextCheck++].second();
            if (t > limit) throw StopRun{};
        };
    }

    uint64_t t = 0;  // script time cursor
    void wait(uint64_t ms) { t += ms; }
    void chr(uint8_t c) { script.emplace_back(t, WinMsg::chr(c)); }
    void text_(const std::string& s) {
        for (char c : s) {
            chr(static_cast<uint8_t>(c));
            t += 60;
        }
    }
    void vk(uint16_t v) {
        script.emplace_back(t, WinMsg::keyDown(v));
        script.emplace_back(t, WinMsg::keyUp(v));
    }
    void click(int x, int y) {
        script.emplace_back(t, WinMsg::mouse(WinMsg::Type::MouseMove, x, y));
        script.emplace_back(t, WinMsg::mouse(WinMsg::Type::LButtonDown, x, y));
        script.emplace_back(t, WinMsg::mouse(WinMsg::Type::LButtonUp, x, y));
    }
    void at(std::function<void()> f) { checks.emplace_back(t, std::move(f)); }

    // Presented pixel (window picture) as the palette index's RGB.
    uint32_t presentedRgb(int x, int y) {
        const auto rgba = engine.presenter().gameAreaRgba();
        const size_t o = (static_cast<size_t>(y) * 512 + static_cast<size_t>(x)) * 4;
        return static_cast<uint32_t>(rgba[o]) << 16 | static_cast<uint32_t>(rgba[o + 1]) << 8 | rgba[o + 2];
    }
};

// A users.dat with the given names (totals as given), every other byte zero.
void writeRoster(const std::filesystem::path& p, const std::vector<std::pair<std::string, uint32_t>>& players,
                 uint8_t soundOn = 1) {
    save::UsersTable t;
    for (size_t i = 0; i < players.size(); ++i) {
        save::PlayerRecord r;
        r.name = players[i].first;
        r.totalTreasures = players[i].second;
        r.soundOn = soundOn;
        r.location = 9;
        r.writeTo(t.slots[i]);
    }
    t.recount();
    REQUIRE(save::writeUsersFile(p, t));
}

save::UsersTable readRoster(const std::filesystem::path& p) {
    save::UsersTable t;
    save::readUsersFile(p, t);
    return t;
}

}  // namespace

TEST_CASE("Sign-in data: the EXE's _ctype equals the Borland table") {
    TM_REQUIRE_GAME_DATA();
    CHECK(CType::fromExe(*g->exe).flags == CType::borland().flags);
}

TEST_CASE("Sign-in run: keyboard selection loads the record (wrap from the new row)") {
    TM_REQUIRE_GAME_DATA();
    const auto dir = tempDir("keyboard");
    Rig r(*g, dir);
    writeRoster(r.paths.usersDat, {{"Tester", 0}, {"Lpha", 0}, {"Bravo", 0}});
    r.wait(300);
    r.vk(0x28);  // Down from "Start New Player" -> player 0
    r.wait(100);
    r.vk(0x28);  // -> player 1
    r.wait(100);
    r.chr(0x0D);
    SignInScreen s(r.ctx);
    const SignInResult res = s.run(false);
    CHECK_FALSE(res.newPlayer);
    CHECK(res.name == "Lpha");
    CHECK(res.userSel == 1);
    CHECK(s.userSel() == 1);
    CHECK(r.state.player.nameString() == "Lpha");
    CHECK(r.state.player.room == 9);
    CHECK(r.engine.hotspots().count() == 0);
}

TEST_CASE("Sign-in run: Up from the new row wraps to the last player; mouse release on a row") {
    TM_REQUIRE_GAME_DATA();
    {
        const auto dir = tempDir("up");
        Rig r(*g, dir);
        writeRoster(r.paths.usersDat, {{"Tester", 0}, {"Lpha", 0}, {"Bravo", 0}});
        r.wait(300);
        r.vk(0x26);
        r.wait(100);
        r.chr(0x0D);
        SignInScreen s(r.ctx);
        CHECK(s.run(false).name == "Bravo");
    }
    {
        const auto dir = tempDir("mouse");
        Rig r(*g, dir);
        writeRoster(r.paths.usersDat, {{"Tester", 0}, {"Lpha", 0}, {"Bravo", 0}});
        r.wait(300);
        r.click(100, 57 + 25 * 2 + 10);  // hot spot 0x4AE, row 2
        SignInScreen s(r.ctx);
        const SignInResult res = s.run(false);
        CHECK(res.name == "Bravo");
        CHECK(res.userSel == 2);
    }
}

TEST_CASE("Sign-in run: Ctrl+R writes users.dat even after No; Yes removes and shifts") {
    TM_REQUIRE_GAME_DATA();
    const auto dir = tempDir("remove");
    Rig r(*g, dir);
    writeRoster(r.paths.usersDat, {{"Tester", 30}, {"Lpha", 20}, {"Bravo", 10}});
    {
        // Trailing junk: the load ignores it, a rewrite drops it.
        std::ofstream f(r.paths.usersDat, std::ios::binary | std::ios::app);
        f << "JUNK";
    }
    REQUIRE(std::filesystem::file_size(r.paths.usersDat) == save::kFileSize + 4);
    r.wait(300);
    r.vk(0x28);    // Tester
    r.wait(100);
    r.chr(0x12);   // Ctrl+R
    r.wait(400);
    r.chr('n');    // No
    r.wait(600);
    r.at([&] { CHECK(std::filesystem::file_size(r.paths.usersDat) == save::kFileSize); });
    r.vk(0x28);    // list rebuilt with "Start New Player" selected: Down -> Tester
    r.wait(100);
    r.vk(0x28);    // Lpha
    r.wait(100);
    r.chr(0x12);
    r.wait(400);
    r.chr('y');    // Yes: removed
    r.wait(600);
    r.at([&] {
        const save::UsersTable t = readRoster(r.paths.usersDat);
        CHECK(t.count == 2);
        CHECK(t.slots[0].name() == "Tester");
        CHECK(t.slots[1].name() == "Bravo");
        CHECK(t.slots[2].empty());
    });
    r.vk(0x26);    // Up from the new row -> Bravo (the last player)
    r.wait(100);
    r.chr(0x0D);
    SignInScreen s(r.ctx);
    const SignInResult res = s.run(false);
    CHECK(res.name == "Bravo");
    CHECK(res.userSel == 1);
    CHECK(r.nextCheck == r.checks.size());
}

TEST_CASE("Sign-in run: Esc on the list asks to exit; Yes throws QuitGame") {
    TM_REQUIRE_GAME_DATA();
    const auto dir = tempDir("exit");
    Rig r(*g, dir);
    writeRoster(r.paths.usersDat, {{"Tester", 0}});
    r.wait(300);
    r.chr(0x1B);
    r.wait(400);
    r.chr('n');  // No: back to the list
    r.wait(400);
    r.chr(0x1B);
    r.wait(400);
    r.chr('y');
    SignInScreen s(r.ctx);
    CHECK_THROWS_AS(s.run(false), QuitGame);
    CHECK(r.state.player.nameString() == "________");  // nobody signed in: the shutdown save is a no-op
}

TEST_CASE("Sign-in run: new player from an empty roster (trim, first letter upper-cased, sound on)") {
    TM_REQUIRE_GAME_DATA();
    const auto dir = tempDir("newplayer");
    Rig r(*g, dir);
    r.state.options.soundOn = 0;  // a new player always starts with Music and Sound on
    r.wait(300);
    r.text_("zed  ");
    r.wait(100);
    r.chr(0x0D);
    SignInScreen s(r.ctx);
    const SignInResult res = s.run(false);
    CHECK(res.newPlayer);
    CHECK(res.name == "Zed");
    CHECK(res.userSel == 0);
    CHECK(r.state.player.nameString() == "Zed");
    CHECK(r.state.player.room == kRoomNewClimb);
    CHECK(r.state.options.soundOn == 1);
    CHECK(r.state.inv.coins == 10);
    CHECK(res.record.name == "Zed");
    // Users_InitNewPlayer does not save: the file is still missing.
    CHECK_FALSE(std::filesystem::exists(r.paths.usersDat));
    // The next Users_SaveCurrent appends the player.
    s.saveCurrent();
    const save::UsersTable t = readRoster(r.paths.usersDat);
    CHECK(t.count == 1);
    CHECK(t.slots[0].name() == "Zed");
    CHECK(t.slots[0].location() == kRoomNewClimb);
}

TEST_CASE("Sign-in run: duplicate check is case-sensitive; the duplicate box returns to the name") {
    TM_REQUIRE_GAME_DATA();
    {
        const auto dir = tempDir("dup_case");
        Rig r(*g, dir);
        writeRoster(r.paths.usersDat, {{"Tester", 0}});
        r.wait(300);
        r.text_("TESTER");  // type-to-create; differs from "Tester" byte for byte
        r.wait(100);
        r.chr(0x0D);
        SignInScreen s(r.ctx);
        const SignInResult res = s.run(false);
        CHECK(res.newPlayer);
        CHECK(res.name == "TESTER");
        CHECK(res.userSel == 1);
    }
    {
        const auto dir = tempDir("dup");
        Rig r(*g, dir);
        writeRoster(r.paths.usersDat, {{"Tester", 0}});
        r.wait(300);
        r.text_("tester");  // first letter upper-cased: "Tester" is on the list
        r.wait(100);
        r.chr(0x0D);
        r.wait(500);
        r.chr(0x0D);  // close the "already on the list" box
        r.wait(300);
        for (int i = 0; i < 6; ++i) {
            r.chr(0x08);
            r.wait(30);
        }
        r.text_("ann");
        r.wait(100);
        r.chr(0x0D);
        SignInScreen s(r.ctx);
        const SignInResult res = s.run(false);
        CHECK(res.newPlayer);
        CHECK(res.name == "Ann");
    }
}

TEST_CASE("Sign-in run: list full (7 players) refuses a new name") {
    TM_REQUIRE_GAME_DATA();
    const auto dir = tempDir("full");
    Rig r(*g, dir);
    writeRoster(r.paths.usersDat, {{"A", 0}, {"B", 0}, {"C", 0}, {"D", 0}, {"E", 0}, {"F", 0}, {"G", 0}});
    r.wait(300);
    r.chr(0x0D);  // Enter on "Start New Player": the list-full box
    r.wait(400);
    r.at([&] { CHECK(r.dialogs.hasSaveUnder()); });
    r.chr(0x0D);  // close it
    r.wait(300);
    r.chr('x');   // type-to-create: list full again
    r.wait(400);
    r.chr(0x0D);
    r.wait(300);
    r.vk(0x26);   // Up -> G
    r.wait(100);
    r.chr(0x0D);
    SignInScreen s(r.ctx);
    const SignInResult res = s.run(false);
    CHECK_FALSE(res.newPlayer);
    CHECK(res.name == "G");
    CHECK(r.nextCheck == r.checks.size());
}

TEST_CASE("Sign-in run: loading a player can switch sound off but never on") {
    TM_REQUIRE_GAME_DATA();
    {
        const auto dir = tempDir("sound_on_stays_off");
        Rig r(*g, dir);
        writeRoster(r.paths.usersDat, {{"Tester", 0}}, /*soundOn*/ 1);
        r.state.options.soundOn = 0;
        r.wait(300);
        r.vk(0x28);
        r.wait(100);
        r.chr(0x0D);
        SignInScreen s(r.ctx);
        s.run(false);
        CHECK(r.state.options.soundOn == 0);
    }
    {
        const auto dir = tempDir("sound_off");
        Rig r(*g, dir);
        writeRoster(r.paths.usersDat, {{"Tester", 0}}, /*soundOn*/ 0);
        r.state.options.soundOn = 1;
        r.wait(300);
        r.vk(0x28);
        r.wait(100);
        r.chr(0x0D);
        SignInScreen s(r.ctx);
        s.run(false);
        CHECK(r.state.options.soundOn == 0);
    }
}

TEST_CASE("Sign-in run: restart saves the signed-in player first and shows the merged roster") {
    TM_REQUIRE_GAME_DATA();
    const auto dir = tempDir("restart");
    Rig r(*g, dir);
    writeRoster(r.paths.usersDat, {{"Lpha", 10}});
    r.state.player.setName("Tester");
    initNewPlayer(r.state);
    r.state.rank.totalTreasures = 1234;  // sorts first
    SignInScreen s(r.ctx);
    s.setUserSel(1);  // appended after Lpha, then sorted
    r.wait(300);
    r.vk(0x28);  // player 0 = Tester (sorted by total)
    r.wait(100);
    r.chr(0x0D);
    const SignInResult res = s.run(true);
    CHECK(res.name == "Tester");
    CHECK(res.userSel == 0);
    const save::UsersTable t = readRoster(r.paths.usersDat);
    CHECK(t.count == 2);
    CHECK(t.slots[0].name() == "Tester");
    CHECK(t.slots[0].totalTreasures() == 1234);
    CHECK(t.slots[1].name() == "Lpha");
}

TEST_CASE("Sign-in run: the name caret blinks every 333 ms (black first)") {
    TM_REQUIRE_GAME_DATA();
    const auto dir = tempDir("caret");
    Rig r(*g, dir);
    const uint32_t black = 0x000000, white = 0xFFFFFF;
    std::vector<uint32_t> seen;
    for (uint64_t ms : {100, 330, 340, 660, 670, 990, 1000}) {
        r.t = ms;
        r.at([&] { seen.push_back(r.presentedRgb(signin::kCaretX + 2, signin::kCaretY + 5)); });
    }
    r.t = 1100;
    r.chr(0x1B);  // empty roster: Esc asks to exit
    r.wait(300);
    r.chr('y');
    SignInScreen s(r.ctx);
    CHECK_THROWS_AS(s.run(false), QuitGame);
    REQUIRE(seen.size() == 7);
    CHECK(seen[0] == black);
    CHECK(seen[1] == black);
    CHECK(seen[2] == white);
    CHECK(seen[3] == white);
    CHECK(seen[4] == black);
    CHECK(seen[5] == black);
    CHECK(seen[6] == white);
}

TEST_CASE("Sign-in run: Esc in the name box with an empty roster keeps the typed name for the exit save") {
    TM_REQUIRE_GAME_DATA();
    const auto dir = tempDir("exit_typed");
    Rig r(*g, dir);
    r.wait(300);
    r.text_("Te");
    r.wait(100);
    r.chr(0x1B);
    r.wait(400);
    r.chr('y');
    SignInScreen s(r.ctx);
    CHECK_THROWS_AS(s.run(false), QuitGame);
    // g_playerName still holds "Te": the original's shutdown Users_SaveCurrent then saves it.
    CHECK(r.state.player.name[0] == 'T');
    CHECK(r.state.player.name[1] == 'e');
}

TEST_CASE("Title run: Enter on every page ends after page 4, then the ending animation") {
    TM_REQUIRE_GAME_DATA();
    const auto dir = tempDir("title_enter");
    Rig r(*g, dir);
    std::vector<std::string> events;
    for (int i = 0; i < 5; ++i) {
        r.wait(300);
        r.chr(0x0D);
    }
    TitleScreen t(r.ctx);
    t.setTrace([&](std::string_view e) { events.emplace_back(e); });
    CHECK(t.run() == TitleResult::Continue);
    auto has = [&](const std::string& e) { return std::find(events.begin(), events.end(), e) != events.end(); };
    CHECK(has("page 0 drawn"));
    CHECK(has("page 4 drawn"));
    CHECK(has("end animation"));
    CHECK(std::count(events.begin(), events.end(), std::string("page 0 drawn")) == 1);
    CHECK_FALSE(r.engine.hotspots().contains(title::kHotspot));
    CHECK(r.clock.nowMs() - r.start >= 1500 + 2000);  // DAM 20101 runs 20 ticks
}

TEST_CASE("Title run: a page times out after 60 s; Space ends the loop; Esc + Yes quits") {
    TM_REQUIRE_GAME_DATA();
    {
        const auto dir = tempDir("title_timeout");
        Rig r(*g, dir);
        std::vector<std::pair<uint64_t, std::string>> events;
        r.t = 61000;
        r.chr(0x20);  // Space on page 1
        TitleScreen t(r.ctx);
        t.setTrace([&](std::string_view e) { events.emplace_back(r.clock.nowMs() - r.start, std::string(e)); });
        CHECK(t.run() == TitleResult::Continue);
        uint64_t page1 = 0;
        for (const auto& [ms, e] : events)
            if (e == "page 1 drawn") page1 = ms;
        CHECK(page1 >= 60000);
        CHECK(page1 <= 60010);
    }
    {
        const auto dir = tempDir("title_quit");
        Rig r(*g, dir);
        r.wait(500);
        r.chr(0x1B);
        r.wait(400);
        r.chr('y');
        TitleScreen t(r.ctx);
        CHECK(t.run() == TitleResult::Quit);
        CHECK(r.clock.nowMs() - r.start < 2000);
    }
}

TEST_CASE("Sign-in run (REVIEW_1 H-1): a damaged users.dat without a backup is kept; the Write Error box shows") {
    TM_REQUIRE_GAME_DATA();
    const auto dir = tempDir("damaged_users");
    Rig r(*g, dir);
    writeRoster(r.paths.usersDat, {{"Lpha", 10}, {"Bravo", 5}});
    REQUIRE_FALSE(std::filesystem::exists(save::usersBackupPath(r.paths.usersDat)));
    auto bytes = *mischief::readWholeFile(r.paths.usersDat);
    bytes.resize(5000);  // an interrupted write
    REQUIRE(mischief::writeWholeFile(r.paths.usersDat, bytes));
    int modals = 0;
    r.dialogs.hooks().menuIsEnabled = [&] {
        ++modals;
        return false;
    };
    r.state.player.setName("Tester");  // Start Another Game: the signed-in player is saved first
    initNewPlayer(r.state);
    SignInScreen s(r.ctx);
    s.setUserSel(2);
    r.wait(300);
    r.chr(0x0D);  // OK on the Write Error box (DS:2084)
    r.wait(400);
    r.vk(0x28);   // the roster in memory holds the player only
    r.wait(100);
    r.chr(0x0D);
    const SignInResult res = s.run(true);
    CHECK(modals == 1);
    CHECK(res.name == "Tester");
    CHECK(res.userSel == 0);
    CHECK(mischief::readWholeFile(r.paths.usersDat) == bytes);  // Lpha and Bravo's bytes are not destroyed
}

TEST_CASE("Sign-in run (REVIEW_1 H-1): a damaged users.dat is listed and saved from users.dat.bak") {
    TM_REQUIRE_GAME_DATA();
    const auto dir = tempDir("damaged_users_bak");
    Rig r(*g, dir);
    writeRoster(r.paths.usersDat, {{"Lpha", 10}, {"Bravo", 5}});
    writeRoster(r.paths.usersDat, {{"Lpha", 12}, {"Bravo", 5}});  // the first file becomes users.dat.bak
    auto bytes = *mischief::readWholeFile(r.paths.usersDat);
    bytes.resize(5000);
    REQUIRE(mischief::writeWholeFile(r.paths.usersDat, bytes));
    int modals = 0;
    r.dialogs.hooks().menuIsEnabled = [&] {
        ++modals;
        return false;
    };
    r.wait(300);
    r.vk(0x28);  // Lpha
    r.wait(100);
    r.vk(0x28);  // Bravo
    r.wait(100);
    r.chr(0x0D);
    SignInScreen s(r.ctx);
    const SignInResult res = s.run(false);
    CHECK(res.name == "Bravo");  // the roster came from the backup (the original would list nobody)
    CHECK(res.userSel == 1);
    r.state.rank.totalTreasures = 50;
    s.saveCurrent();  // e.g. the end of a climb
    CHECK(modals == 0);
    CHECK(std::filesystem::file_size(r.paths.usersDat) == save::kFileSize);
    const save::UsersTable t = readRoster(r.paths.usersDat);
    REQUIRE(t.count == 2);
    CHECK(t.slots[0].name() == "Bravo");
    CHECK(t.slots[0].totalTreasures() == 50);
    CHECK(t.slots[1].name() == "Lpha");
    CHECK(t.slots[1].totalTreasures() == 10);
}
