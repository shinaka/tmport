// DOS edition, sign-in and USERS.DAT (src/dos/game/signin.cpp): the roster order, name entry,
// remove mode, the list-full and identical-name boxes, the saved-game question, the record
// pack / restore, the save moments, and USERS.DAT files written byte for byte like the original's
// (compared with the reference snapshots of extracted/captures/dos). Only counts, indices, key codes
// and numbers are stored here.
#include <doctest/doctest.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <vector>

#include "dos/game/game.h"
#include "dos/game/hud.h"
#include "dos/game/signin.h"
#include "dos/game/ui.h"
#include "dos/save/dosbox_state.h"
#include "dos/save/users_dat.h"
#include "dos_game_fixture.h"
#include "dos_test_data.h"
#include "mischief/util/file_util.h"

using namespace tmport;
using namespace tmport::dos;
namespace fs = std::filesystem;

namespace {

std::optional<fs::path> capturesFile(const char* rel) {
    const auto caps = testdata::dosCapturesPath();
    if (!caps) return std::nullopt;
    const fs::path p = *caps / rel;
    std::error_code ec;
    if (!fs::exists(p, ec)) return std::nullopt;
    return p;
}

bool storeUsers(dosgame::GameFixture& f, const fs::path& src) {
    std::error_code ec;
    fs::copy_file(src, f.users().path(), fs::copy_options::overwrite_existing, ec);
    return !ec;
}

void command(DosGame& g, int cmd) {
    g.input.cmd = static_cast<int16_t>(cmd);
    signInUpdate(g);
}

// The first sign-in frame after the title with the store's file = `users` (painter 2 included).
bool startSignIn(dosgame::GameFixture& f, const DosGfxFiles& files, const char* users) {
    const auto src = capturesFile(users);
    if (!src || !storeUsers(f, *src)) return false;
    DosGame& g = f.g();
    g.pan = &files.pan("OPENING");
    g.bank = &files.pak("BEGIN");
    g.core.area = 6;
    g.core.uiMode = 8;
    g.ui.signinInit = 1;
    signInUpdate(g);
    screenDrawOverlay(g);
    return true;
}

// A sign-in box's Enter: the Button_Flash wait (the PIT runs on) and the confirm handler.
void confirmBox(dosgame::GameFixture& f, int button) {
    DosGame& g = f.g();
    g.ui.buttonSel = static_cast<uint16_t>(button);
    command(g, 8);
    REQUIRE(uiFlashPending(g));
    f.runtime().setClient(nullptr);
    f.runtime().stepPitTicks(75);
    signInUpdate(g);
    REQUIRE_FALSE(uiFlashPending(g));
}

int differingBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, std::vector<size_t>* where = nullptr) {
    int n = 0;
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        if (i >= a.size() || i >= b.size() || a[i] != b[i]) {
            ++n;
            if (where) where->push_back(i);
        }
    }
    return n;
}

constexpr int kA = 97, kD = 100;  // the name typed in the reference run (key codes)

}  // namespace

TEST_CASE("DOS sign-in: roster order is the original's shell sort; rows, zones, highlight") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    if (!startSignIn(f, *gameFiles, "USERS_crafted_tierb_layout.DAT")) return;
    DosGame& g = f.g();
    // Totals 0, 0, 120, 390 in records 0..3: descending, the two zeros as the gap sequence leaves them.
    REQUIRE(g.ui.signinCount == 4);
    CHECK(g.ui.signinRecIdx[0] == 3);
    CHECK(g.ui.signinRecIdx[1] == 2);
    CHECK(g.ui.signinRecIdx[2] == 0);
    CHECK(g.ui.signinRecIdx[3] == 1);
    CHECK(g.ui.signinRow == 4);  // the highlight starts on "New Player"
    CHECK(g.ui.signinState == 1);
    CHECK(g.ui.screen == 2);
    CHECK(g.core.area == 4);
    CHECK(g.input.context == 4);
    const InputZoneTable& z = g.inputTables.zones(4);
    CHECK(z.count == 14);  // c6cc + 10: rows 0..4 (with "New Player") are zones 9..13
    for (int row = 0; row <= 4; ++row) CHECK(z.records[static_cast<size_t>(9 + row)].x1 == 6);
    // Up / Down wrap over the players and "New Player".
    command(g, 7);
    CHECK(g.ui.signinRow == 0);
    command(g, 6);
    CHECK(g.ui.signinRow == 4);
    command(g, 6);
    CHECK(g.ui.signinRow == 3);
    command(g, 9 + 1);  // a click on row 1
    CHECK(g.ui.signinRow == 1);
    // The highlight is an exchange: toggling twice leaves the screen as it was.
    const auto before = g.back.logicalPixels();
    signInMoveHighlight(g);
    CHECK(g.back.logicalPixels() != before);
    signInMoveHighlight(g);
    CHECK(g.back.logicalPixels() == before);
}

TEST_CASE("DOS sign-in: name entry (first letter upper-cased, 8 characters, Backspace, caret blink)") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    if (!startSignIn(f, *gameFiles, "USERS_crafted_tierb_layout.DAT")) return;
    DosGame& g = f.g();
    command(g, 8);  // Enter on "New Player"
    CHECK(g.ui.signinState == 3);
    CHECK(g.input.context == 4);
    command(g, 32);  // a leading space is not taken
    CHECK(g.ui.newNameLen == 0);
    command(g, 113);  // q -> Q: the first letter is upper-cased (code & 0xDF)
    REQUIRE(g.ui.newNameLen == 1);
    CHECK(static_cast<uint8_t>(g.ui.newName[0]) == 81);
    for (int c : {119, 32, 101, 114, 116, 121, 117, 105}) command(g, c);  // the 8th character is the last one taken
    CHECK(g.ui.newNameLen == 8);
    CHECK(static_cast<uint8_t>(g.ui.newName[2]) == 32);
    command(g, 31);  // Backspace
    CHECK(g.ui.newNameLen == 7);
    CHECK(g.ui.newName[7] == 0);
    CHECK(g.ui.caretOn == 1);
    // The caret blinks on DS:90FE: armed while the counter is above 6, toggled at or below it.
    g.counters().cycle13 = 10;
    command(g, -1);
    CHECK(g.ui.caretArmed == 1);
    CHECK(g.ui.caretOn == 1);
    g.counters().cycle13 = 6;
    command(g, -1);
    CHECK(g.ui.caretOn == 0);
    CHECK(g.ui.caretArmed == 0);
    command(g, -1);  // once per cycle
    CHECK(g.ui.caretOn == 0);
    // Esc with players listed: back to the list.
    command(g, 0);
    CHECK(g.ui.signinState == 1);
    CHECK(g.ui.signinInit == 1);
}

TEST_CASE("DOS sign-in: identical name, list full, exit and remove boxes") {
    TM_REQUIRE_DOS_GAME_FILES();
    SUBCASE("a name already on the list (any case, trailing blanks dropped)") {
        dosgame::GameFixture f(*gameFiles);
        if (!startSignIn(f, *gameFiles, "USERS_crafted_tierb_layout.DAT")) return;
        DosGame& g = f.g();
        for (int c : {8, kA, kD, kA, 32}) command(g, c);
        CHECK(g.ui.newNameLen == 4);
        command(g, 8);
        CHECK(g.ui.newName[3] == 0);  // Users_NameExists cut the blank
        CHECK(g.ui.signinState == 0x21);
        CHECK(g.ui.dialogId == 17);
        CHECK(f.users().writes() == 0);
        command(g, 8);  // Dialog_HandleOk -> SignInDlg_Reset
        CHECK(g.ui.signinState == 1);
        CHECK(g.ui.signinInit == 1);
    }
    SUBCASE("nine players: a letter or New Player gives the list-full box, Enter remove mode") {
        dosgame::GameFixture f(*gameFiles);
        if (!startSignIn(f, *gameFiles, "../../scratch/dos_w1_runtime/USERS_9players.DAT")) return;
        DosGame& g = f.g();
        CHECK(g.ui.signinCount == 9);
        command(g, kA);
        CHECK(g.ui.signinState == 0x21);
        CHECK(g.ui.dialogId == 18);
        CHECK(g.ui.newNameLen == 0);
        command(g, 8);
        CHECK(g.ui.signinState == 5);
        CHECK(g.ui.hudRedraw == 1);
    }
    SUBCASE("Esc: the exit question; Yes quits") {
        dosgame::GameFixture f(*gameFiles);
        if (!startSignIn(f, *gameFiles, "USERS_crafted_tierb_layout.DAT")) return;
        DosGame& g = f.g();
        command(g, 0);
        CHECK(g.ui.signinState == 9);
        CHECK(g.ui.dialogId == 14);
        CHECK(g.ui.buttonSel == 0);  // default No
        command(g, 14);              // Y: Yes and a queued Enter
        CHECK(g.ui.buttonSel == 1);
        CHECK(g.input.queuedCmd == 8);
        confirmBox(f, 1);
        CHECK(g.core.quit == 1);
        CHECK(g.ui.signinState == 1);
    }
    SUBCASE("Ctrl+R: remove mode, the box, Yes clears the record and writes") {
        dosgame::GameFixture f(*gameFiles);
        if (!startSignIn(f, *gameFiles, "USERS_crafted_tierb_layout.DAT")) return;
        DosGame& g = f.g();
        command(g, 30);
        CHECK(g.ui.signinState == 5);
        screenDrawOverlay(g);
        CHECK(g.ui.signinRow == 0);
        CHECK(g.inputTables.zones(4).count == 13);  // players only
        command(g, 6);                               // wraps over the players only
        CHECK(g.ui.signinRow == 3);
        command(g, 8);
        CHECK(g.ui.signinState == 0x0D);
        CHECK(g.ui.dialogId == 15);
        const int rec = g.ui.signinRecIdx[3];
        confirmBox(f, 1);
        CHECK(f.users().writes() == 1);
        CHECK(f.users().lastMoment() == DosSaveMoment::RemovePlayer);
        CHECK(g.users.record(rec).empty());
        CHECK(g.users.record(rec).answerMasks[0] == 7);
        CHECK(g.ui.usersLoaded == 0);
        CHECK(g.ui.signinInit == 1);
        CHECK(g.ui.signinState == 5);  // SignInDlg_Back: state & 7
    }
}

TEST_CASE("DOS sign-in: the saved-game question") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    if (!startSignIn(f, *gameFiles, "USERS_after_exit_level2_Ada.DAT")) return;
    DosGame& g = f.g();
    int row = -1;
    for (int k = 0; k < g.ui.signinCount; ++k)
        if (g.users.record(g.ui.signinRecIdx[static_cast<size_t>(k)]).savedGame != 0) row = k;
    REQUIRE(row >= 0);
    while (g.ui.signinRow != row) command(g, 6);
    command(g, 8);
    CHECK(g.ui.signinState == 0x11);
    CHECK(g.ui.dialogId == 16);
    CHECK(g.ui.buttonSel == 1);  // box 11: default Yes
    REQUIRE(g.hasRec());
    CHECK(g.ui.userRec == g.ui.signinRecIdx[static_cast<size_t>(row)]);
    // Esc: back to the list, nothing lost.
    command(g, 0);
    CHECK(g.ui.signinState == 1);
    CHECK(g.rec().savedGame != 0);
    // "No" drops the saved game without the warning box and starts (handler only here).
    g.ui.buttonSel = 0;
    signInDlgContinueAnswer(g);
    CHECK(g.rec().savedGame == 0);
    CHECK(g.ui.signinStart == 1);
}

TEST_CASE("DOS sign-in: Users_Pack and Game_Restore round-trip the reference records") {
    TM_REQUIRE_DOS_GAME_FILES();
    for (const char* file : {"USERS_after_exit_level2_Ada.DAT", "USERS_after_castle_climb_Ada.DAT",
                             "USERS_after_exit_post_castle_Ada.DAT", "USERS_crafted_tierb_layout.DAT",
                             "USERS_crafted_tierc_layout.DAT", "USERS_after_tandy_newplayer_Ben.DAT"}) {
        const auto path = capturesFile(file);
        if (!path) continue;
        const std::string name = file;
        CAPTURE(name);
        dosgame::GameFixture f(*gameFiles);
        REQUIRE(f.loadUsers(*path));
        const UsersDat original = f.users().users();
        for (int i = 0; i < kUserRecordCount; ++i) {
            if (original.records[static_cast<size_t>(i)].empty()) continue;
            CAPTURE(i);
            f.selectRecord(i);
            gameRestore(f.g());
            usersPack(f.g());
            std::array<uint8_t, kUserRecordSize> a{}, b{};
            f.users().users().records[static_cast<size_t>(i)].toBytes(a);
            original.records[static_cast<size_t>(i)].toBytes(b);
            // Hiding places that are mirror cells (0x86-0x88, 0xC0-0xC2, 0xFD-0xFF) take their
            // source's picture and content on restore (Layout_MirrorSharedCells), as in the original.
            std::vector<bool> mirrored(kUserRecordSize, false);
            for (size_t h = 0; h < f.g().tables.world.hidingCells.size(); ++h) {
                const uint16_t cell = f.g().tables.world.hidingCells[h];
                if ((cell >= 0x86 && cell <= 0x88) || (cell >= 0xC0 && cell <= 0xC2) || (cell >= 0xFD && cell <= 0xFF))
                    mirrored[urec::kHidingPictures + h] = mirrored[urec::kHidingContents + h] = true;
            }
            std::string where;
            for (size_t k = 0; k < a.size(); ++k)
                if (a[k] != b[k] && !mirrored[k]) where += std::format(" +{:#x}", k);
            CHECK_MESSAGE(where.empty(), where);
            CHECK(f.users().users().spriteHighWater == original.spriteHighWater);  // DS:929F <-> header bytes 1-2
        }
    }
}

TEST_CASE("DOS sign-in: a new player writes USERS.DAT like the original") {
    TM_REQUIRE_DOS_GAME_FILES();
    const auto before = capturesFile("original/USERS.DAT");
    const auto after = capturesFile("USERS_after_signin_Ada.DAT");
    if (!before || !after) return;
    dosgame::GameFixture f(*gameFiles);
    REQUIRE(startSignIn(f, *gameFiles, "original/USERS.DAT"));
    DosGame& g = f.g();
    // DS:929F (header bytes 1-2) is the sprite save heap's high-water mark of the frames drawn so far
    // (the anim module's); the reference run's value is put in so every other byte is compared.
    const auto ref = mischief::readWholeFile(*after);
    REQUIRE(ref);
    g.sprites.saveHeapHigh = static_cast<uint16_t>((*ref)[1] | ((*ref)[2] << 8));
    for (int c : {8, kA, kD, kA}) command(g, c);
    REQUIRE(g.ui.newNameLen == 3);
    // Enter: Users_AddPlayer (InitNewPlayer, Users_Pack, the name, the write) and the game start.
    g.input.cmd = 8;
    g.ui.uiCmd = 8;
    g.input.deliverNext = false;
    signInCmdNewName(g);  // the handler alone: the file is written here, before Game_Start
    CHECK(g.ui.signinStart == 1);
    CHECK(f.users().writes() == 1);
    CHECK(f.users().lastMoment() == DosSaveMoment::NewPlayer);
    const auto written = mischief::readWholeFile(f.users().path());
    REQUIRE(written);
    std::vector<size_t> where;
    const int n = differingBytes(*written, *ref, &where);
    if (n != 0) {
        std::string list;
        for (size_t i = 0; i < where.size() && i < 24; ++i) list += std::format(" {}", where[i]);
        MESSAGE(std::format("{} bytes differ:{}", n, list));
    }
    CHECK(n == 0);
}

TEST_CASE("DOS sign-in: Game_End and the menu save moments") {
    TM_REQUIRE_DOS_GAME_FILES();
    const auto path = capturesFile("USERS_after_exit_level2_Ada.DAT");
    if (!path) return;
    dosgame::GameFixture f(*gameFiles);
    REQUIRE(f.loadUsers(*path));
    f.selectRecord(1);
    DosGame& g = f.g();
    SUBCASE("Game_End banks coins and nets and drops the saved game") {
        g.core.coins = 17;
        g.core.nets = 3;
        g.core.gamesPlayed = 4;
        g.core.treasuresFound = 2;
        g.core.tier = 1;
        gameEnd(g);
        CHECK(g.core.gamesPlayed == 5);
        CHECK(g.elves.lastGameTreasures == 2);
        CHECK(g.elves.lastGameTier == 1);
        CHECK(g.rec().savedGame == 0);
        CHECK(g.rec().bankedCoins == 17);
        CHECK(g.rec().bankedNets == 3);
        CHECK(g.rec().coins == 17);  // Users_Pack
        CHECK(g.rec().gamesPlayed == 5);
        CHECK(f.users().lastMoment() == DosSaveMoment::GameEnd);
        const auto written = mischief::readWholeFile(f.users().path());
        REQUIRE(written);
        CHECK(written->size() == kUsersFileSize);
    }
    SUBCASE("Exit and Save: Yes writes with the game-in-progress byte, both answers set g_quit") {
        g.core.gameInProgress = 1;
        g.ui.buttonSel = 0;
        menuCmdExitAndSave(g);
        CHECK(g.core.quit == 0);
        CHECK(f.users().writes() == 0);
        g.ui.buttonSel = 1;
        menuCmdExitAndSave(g);
        CHECK(g.core.quit == 1);
        CHECK(g.rec().savedGame == 1);
        CHECK(f.users().lastMoment() == DosSaveMoment::ExitAndSave);
    }
    SUBCASE("Start New Player saves and returns to the sign-in") {
        g.core.gameInProgress = 0;
        g.ui.buttonSel = 1;
        menuCmdStartNewPlayer(g);
        CHECK(g.rec().savedGame == 0);  // left before the start message closed: no saved game
        CHECK(g.core.uiMode == 8);
        CHECK(f.users().lastMoment() == DosSaveMoment::StartNewPlayer);
    }
    SUBCASE("Users_LoadPlayer takes the record's sound settings only while the music is on") {
        g.rec().musicOn = 0;
        g.rec().soundOn = 0;
        g.core.musicOn = 1;
        usersLoadPlayer(g);
        CHECK(g.core.musicOn == 0);
        CHECK(g.core.soundOn == 0);
        g.rec().musicOn = 1;
        g.rec().soundOn = 1;
        usersLoadPlayer(g);
        CHECK(g.core.musicOn == 0);  // once off, later players' settings are not loaded
        CHECK(g.core.coins == g.rec().bankedCoins);
        CHECK(g.core.level == 0);
        CHECK(g.player.obj.x == 0x140);
        CHECK(g.world.cameraX == 0xC0);
        CHECK(g.core.area == 0);
        CHECK(g.world.loadedScene == 0xFF);
        CHECK(g.elves.hintsOn == (g.rec().totalTreasures < 5 ? 1 : 0));
    }
}

// Exit and Save from DOSBox-X states of the reference runs (their DGROUP, read with the shared
// reader from TM_DOS_CAPTURES).
TEST_CASE("DOS sign-in: Exit and Save from reference states writes the reference USERS.DAT") {
    TM_REQUIRE_DOS_GAME_FILES();
    TM_REQUIRE_DOS_CAPTURES();
    struct Case {
        const char* state;
        const char* expected;
        std::vector<size_t> later;  // bytes the reference run changed after the state was saved
    };
    // after_castle was saved while the end message was still up; the player closed it and pressed
    // Esc before the reference write, which changed two bytes: the header's DS:929F (frames drew
    // sprites meanwhile) and the record's prompt icon (+0x14C of record 1 = file offset 695).
    for (const Case& c : {Case{"savestates/r2/tierb_exitbox.sav", "USERS_crafted_tierb_layout.DAT", {}},
                          Case{"savestates/ega/after_castle.sav", "USERS_after_exit_post_castle_Ada.DAT", {1, 695}}}) {
        const std::string name = c.state;
        CAPTURE(name);
        const auto sav = testdata::dosCapture(c.state);
        const auto ref = capturesFile(c.expected);
        if (!sav || !ref) continue;
        const DosboxState st = DosboxState::load(*sav);
        const std::vector<uint8_t> image(st.dgroup().begin(), st.dgroup().end());
        const auto* dgroup = &image;
        dosgame::GameFixture f(*gameFiles);
        DosGame& g = f.g();
        loadFromDgroup(g, *dgroup);
        // The USERS.DAT image in RAM (DS:BA14) and DS:929F are held by the store and the sprites.
        g.users.users() = *UsersDat::parse(std::span<const uint8_t>(*dgroup).subspan(dg::kUsersFile, kUsersFileSize));
        const auto& d = *dgroup;
        g.sprites.saveHeapHigh = static_cast<uint16_t>(d[dg::kSaveHeapHigh] | (d[dg::kSaveHeapHigh + 1] << 8));
        g.ui.buttonSel = 1;
        menuCmdExitAndSave(g);
        const auto written = mischief::readWholeFile(f.users().path());
        const auto expected = mischief::readWholeFile(*ref);
        REQUIRE(written);
        REQUIRE(expected);
        std::vector<size_t> where;
        const int n = differingBytes(*written, *expected, &where);
        std::string list;
        for (size_t i = 0; i < where.size() && i < 32; ++i) list += std::format(" {}", where[i]);
        MESSAGE(std::format("{}: {} bytes differ{}", c.state, n, list));
        CHECK(where == c.later);
    }
}

// Game_End at the end of a climb (the end-of-game hook of Scene_Update) from the rank-chest state of
// the Round 2 run: the record it writes against the file of the following clubhouse state. Leaving
// the rank screen (Cell_P72_RankScreen, the end flow) moves the player to the clubhouse first, so
// the level, position, camera and area fields of the record and the header word may differ.
TEST_CASE("DOS sign-in: Game_End from the rank-chest state writes the following state's USERS.DAT") {
    TM_REQUIRE_DOS_GAME_FILES();
    TM_REQUIRE_DOS_CAPTURES();
    const auto sav = testdata::dosCapture("savestates/r2/end_t6_rankchest.sav");
    const auto ref = capturesFile("savestates/r2/end_t6_clubhouse.users");
    if (!sav || !ref) return;
    const DosboxState st = DosboxState::load(*sav);
    const std::vector<uint8_t> image(st.dgroup().begin(), st.dgroup().end());
    const auto* dgroup = &image;
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    loadFromDgroup(g, *dgroup);
    g.users.users() = *UsersDat::parse(std::span<const uint8_t>(*dgroup).subspan(dg::kUsersFile, kUsersFileSize));
    const auto& d = *dgroup;
    g.sprites.saveHeapHigh = static_cast<uint16_t>(d[dg::kSaveHeapHigh] | (d[dg::kSaveHeapHigh + 1] << 8));
    gameEnd(g);
    const auto written = mischief::readWholeFile(f.users().path());
    const auto expected = mischief::readWholeFile(*ref);
    REQUIRE(written);
    REQUIRE(expected);
    std::vector<size_t> where;
    const int n = differingBytes(*written, *expected, &where);
    std::string list;
    for (size_t i = 0; i < where.size() && i < 40; ++i) list += std::format(" {}", where[i]);
    MESSAGE(std::format("{} bytes differ{}", n, list));
    // Every other byte (the banked coins and nets, the dropped saved game, the games played, the
    // history, slots, masks, clue words, collected treasures, the hiding places) is the original's.
    const size_t rec = UsersDat::recordOffset(g.ui.userRec);
    for (size_t i : where) {
        CAPTURE(i);
        const size_t field = i - rec;
        CHECK(i >= rec);
        CHECK(((field >= urec::kLevel && field < urec::kLevel + 2) || (field >= urec::kPlayerX && field < urec::kShopCoinsOwed)));
    }
    CHECK(n <= 12);
}
