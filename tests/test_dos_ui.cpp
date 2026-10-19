// DOS edition, ui area (src/dos/game/ui.cpp, title.cpp, hud.cpp): the box input zones and keys, the
// menu bar and dialog state machine, Button_Flash as a wait over frames, the menu commands, the
// sound hot key, the help pager, the HUD (counters, count-up, clue box, prompt), the title phases,
// and reference renders of the screens these modules draw.
//
// The reference renders drive the modules (not the wave-2 composer) and store FNV-1a hashes of the
// result on a blank background. With TM_DOS_UI_VERIFY=<dir> each render is also drawn over
// <dir>/<name>_bg.bin (the DOSBox-X capture in logical colours) and written to <dir>/<name>.bin;
// extracted/scratch/dos_w3_ui/verify.py makes the backgrounds and counts the pixels that differ
// from the captures (0 for every check, see the area report). Only hashes, counts and numbers are
// stored here.
#include <doctest/doctest.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <vector>

#include "dos/data/dgroup_offsets.h"
#include "dos/game/game.h"
#include "dos/game/hud.h"
#include "dos/game/quiz.h"
#include "dos/game/signin.h"
#include "dos/game/sound.h"
#include "dos/game/title.h"
#include "dos/game/ui.h"
#include "dos/gfx/blitter.h"
#include "dos_game_fixture.h"
#include "util/file_util.h"
#include "util/hash.h"

using namespace tmport;
using namespace tmport::dos;
namespace fs = std::filesystem;

namespace {

std::optional<fs::path> verifyDir() {
    const char* d = std::getenv("TM_DOS_UI_VERIFY");
    if (!d || !*d) return std::nullopt;
    return fs::path(d);
}

dosgame::FixtureOptions optionsFor(Look look) {
    dosgame::FixtureOptions o;
    o.options.look = look;
    return o;
}

uint64_t hashOf(const BackBuffer& b) { return fnv1a64(b.logicalPixels()); }

// A reference USERS.DAT of extracted/captures/dos (nullopt without the captures).
std::optional<fs::path> capturesFile(const char* rel) {
    const auto caps = testdata::dosCapturesPath();
    if (!caps) return std::nullopt;
    const fs::path p = *caps / rel;
    std::error_code ec;
    if (!fs::exists(p, ec)) return std::nullopt;
    return p;
}

// The store's file = a reference USERS.DAT (the sign-in reads it with Users_ReadFile).
bool storeUsers(dosgame::GameFixture& f, const char* rel) {
    const auto src = capturesFile(rel);
    if (!src) return false;
    std::error_code ec;
    fs::copy_file(*src, f.users().path(), fs::copy_options::overwrite_existing, ec);
    return !ec;
}

// The raw seed whose next rand() % 6 is `order` (the answer order Quiz_PickQuestion draws).
void seedForOrder(DosGame& g, int order) {
    for (uint32_t s = 1;; ++s) {
        BorlandRand r;
        r.setRawSeed(s);
        if (r.rand() % 6 == order) {
            g.rand.setRawSeed(s);
            return;
        }
    }
}

// ---- scenarios of the reference renders ------------------------------------------------------

// TitleStep_DrawTitle, then the title loop's actors as the capture shows them (the figure of
// script 0x5B, a sparkle, the mouse pointer: the anim module's part, placed here as stand-ins).
bool drawTitle(dosgame::GameFixture& f, const DosGfxFiles& files, Look look) {
    DosGame& g = f.g();
    g.pan = &files.pan("OPENING");
    g.bank = &files.pak("BEGIN");
    titleStart(g);
    callTitlePhase(g, 0);
    const DosScreenSpec spec = defaultScreenSpec(DosScreen::Title, look);
    SpriteRenderer actors;
    actors.records[1].frame = 6;
    if (spec.sparkleSpot >= 0) {
        DrawRecord& s = actors.records[2];
        s.x = g.tables.ui.titleSparkleX[static_cast<size_t>(spec.sparkleSpot)];
        s.y = g.tables.ui.titleSparkleY[static_cast<size_t>(spec.sparkleSpot)];
        s.frame = static_cast<uint16_t>(spec.sparkleFrame * 2);
    }
    if (spec.pointer) {
        actors.records[0].x = static_cast<int16_t>(spec.pointer->first);
        actors.records[0].y = static_cast<int16_t>(spec.pointer->second);
        actors.records[0].frame = 4;
    }
    actors.drawSorted(g.back, *g.bank, 0, 3, true, true);
    return true;
}

// A story / series page as its phase draws it after the title page (blue ink, line height 10).
bool drawTitlePhase(dosgame::GameFixture& f, const DosGfxFiles& files, int phase) {
    DosGame& g = f.g();
    g.pan = &files.pan("OPENING");
    g.bank = &files.pak("BEGIN");
    titleStart(g);
    g.ui.textInk = 0x0000;
    g.ui.titlePhase = static_cast<uint16_t>(phase);
    callTitlePhase(g, phase);
    return true;
}

// The first sign-in frame after the title: SignIn_Update's set-up, then painter 2.
bool drawSignIn(dosgame::GameFixture& f, const DosGfxFiles& files, const char* users) {
    if (!storeUsers(f, users)) return false;
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

void signInCommand(DosGame& g, int cmd) {
    g.input.cmd = static_cast<int16_t>(cmd);
    signInUpdate(g);
}

// Enter on "New Player", the typed key codes, optionally one caret blink (off phase).
bool drawNameEntry(dosgame::GameFixture& f, const DosGfxFiles& files, const char* users, std::vector<int> typed,
                   bool caretOff) {
    if (!drawSignIn(f, files, users)) return false;
    DosGame& g = f.g();
    signInCommand(g, 8);
    for (int c : typed) signInCommand(g, c);
    if (caretOff) {
        g.counters().cycle13 = 12;  // arms the blink
        signInCommand(g, -1);
        g.counters().cycle13 = 3;   // toggles it
        signInCommand(g, -1);
    }
    return true;
}

// The saved-game question: Up to the row of the player with a saved game, Enter.
bool drawContinuePrompt(dosgame::GameFixture& f, const DosGfxFiles& files) {
    if (!drawSignIn(f, files, "USERS_after_exit_level2_Ada.DAT")) return false;
    DosGame& g = f.g();
    int row = -1;
    for (int k = 0; k < g.ui.signinCount; ++k)
        if (g.users.record(g.ui.signinRecIdx[static_cast<size_t>(k)]).savedGame != 0) row = k;
    REQUIRE(row >= 0);
    while (g.ui.signinRow != row) signInCommand(g, 6);
    signInCommand(g, 8);
    return true;
}

// The sign-in list, then commands (and the painter of the next frame when `paint`).
bool drawSignInCommands(dosgame::GameFixture& f, const DosGfxFiles& files, const char* users, std::vector<int> cmds,
                        bool paint) {
    if (!drawSignIn(f, files, users)) return false;
    DosGame& g = f.g();
    for (int c : cmds) signInCommand(g, c);
    if (paint) screenDrawOverlay(g);
    return true;
}

constexpr const char* kCrafted = "USERS_crafted_tierb_layout.DAT";      // 4 players, two crafted
constexpr const char* kNinePlayers = "../../scratch/dos_w1_runtime/USERS_9players.DAT";  // the list-full test file

// Menu commands as Input_Tick hands them over (F1..F3: menu bar open, none chosen yet). The HUD
// under the menu is the background: no painter (scene 3).
bool drawMenu(dosgame::GameFixture& f, const DosGfxFiles& files, std::vector<int> cmds) {
    DosGame& g = f.g();
    g.bank = &files.pak("SPRITES");
    g.world.sceneWanted = 3;
    g.core.uiMode = 0x40;
    g.ui.menuOpen = -1;
    g.ui.menuItem = 0;
    for (int c : cmds) {
        g.input.cmd = static_cast<int16_t>(c);
        menuUpdate(g);
    }
    return true;
}

// Enter on item `item` of menu `menu` (Esc in play is Enter on File item 2 this way).
bool drawBox(dosgame::GameFixture& f, const DosGfxFiles& files, int menu, int item) {
    DosGame& g = f.g();
    g.bank = &files.pak("SPRITES");
    g.world.sceneWanted = 3;
    g.core.uiMode = 0x40;
    g.ui.menuOpen = static_cast<int16_t>(menu);
    g.ui.menuItem = static_cast<uint16_t>(item);
    // The defaults Game_Start leaves for the players of the captures: hints off (the Round 2 player
    // has 5 treasures or more), music and sound on.
    g.ui.hintsBoxDefault = 0;
    g.ui.musicBoxDefault = g.ui.soundBoxDefault = 1;
    g.input.cmd = 8;
    menuUpdate(g);
    return true;
}

// Help topic `topic` from the Help menu, then Enter (with its Button_Flash wait) page by page.
// One command in a dialog as the frames hand it over: Menu_Update, a Button_Flash wait (the PIT
// runs 75 ticks, i.e. 15 game ticks), and each command the code queued (DS:90F6: the Enter of Y /
// N / PgDn / PgUp, the F1 that reopens the Help menu) delivered on the next tick.
void dialogCommand(dosgame::GameFixture& f, int cmd) {
    DosGame& g = f.g();
    f.runtime().setClient(nullptr);  // only the PIT runs during the waits
    for (int next = cmd; next != -1;) {
        g.input.cmd = static_cast<int16_t>(next);
        menuUpdate(g);
        if (uiFlashPending(g)) {
            f.runtime().stepPitTicks(75);
            menuUpdate(g);
            REQUIRE_FALSE(uiFlashPending(g));
        }
        next = g.input.queuedCmd;
        g.input.queuedCmd = -1;
    }
}

// Help topic `topic` from the Help menu, then dialog commands (8 = Enter on the selected button).
bool drawHelp(dosgame::GameFixture& f, const DosGfxFiles& files, int topic, std::vector<int> cmds) {
    drawBox(f, files, 0, topic);
    for (int c : cmds) dialogCommand(f, c);
    return true;
}

// A record with a dealt question: question `q` in slot 0, the 9 questions expanded, slot 0 caught.
void setupQuestion(DosGame& g, int q) {
    UserRecord& r = g.rec();
    r.quizSlots[0] = static_cast<uint16_t>(q);
    r.answerMasks[0] = 7;
    quizLoadQuestions(g, true);
    g.core.caughtSlot = 1;
    g.core.uiMode = 2;
    g.ui.quizClosed = 1;
}

// The question screen of ega/030 (question 61 in answer order 0; the HUD under the panel is the
// background, so the painter's Hud_Redraw is not repeated).
bool drawQuizQuestion(dosgame::GameFixture& f, const DosGfxFiles& files) {
    const auto users = capturesFile("USERS_after_exit_level2_Ada.DAT");
    if (!users || !f.loadUsers(*users)) return false;
    f.selectRecord(1);
    DosGame& g = f.g();
    g.bank = &files.pak("SPRITES");
    setupQuestion(g, 61);
    seedForOrder(g, 0);
    g.input.cmd = -1;
    quizUpdate(g);
    quizDrawFrame(g);
    quizDrawQuestion(g);
    quizDrawAnswers(g);
    g.ui.hudRedraw = 0;  // as the painter's Hud_Redraw leaves it
    return true;
}

// ega/031: the right answer chosen; Clue_Reveal finds clue word 40 hidden on level 0.
bool drawQuizRight(dosgame::GameFixture& f, const DosGfxFiles& files) {
    if (!drawQuizQuestion(f, files)) return false;
    DosGame& g = f.g();
    g.core.level = 0;
    g.rec().clueWords = {40 | 0x80, 40 | 0x80, 40 | 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
    UiState& ui = g.ui;
    ui.hudCoins = ui.coinsShown = g.core.coins;  // the counters on screen are current
    ui.hudNets = g.core.nets;
    ui.hudFound = g.core.treasuresFound;
    ui.hudKeyState = g.core.keyState;
    ui.hudPrompt = 2;
    auto focusedAnswer = [&] { return g.tables.ui.answerPerms[ui.quizOrder][static_cast<size_t>(ui.quizFocus)]; };
    for (int k = 0; k < 3 && focusedAnswer() != 0; ++k) {
        g.input.cmd = 7;
        quizUpdate(g);
    }
    g.input.cmd = 8;
    quizUpdate(g);
    return true;
}

// The HUD clue box (and with `counters` the whole status bar) over the capture.
bool drawHud(dosgame::GameFixture& f, const DosGfxFiles& files, int level, int keyState, std::array<int, 3> words,
             bool counters) {
    const auto users = capturesFile("USERS_after_exit_level2_Ada.DAT");
    if (!users || !f.loadUsers(*users)) return false;
    f.selectRecord(1);
    DosGame& g = f.g();
    g.bank = &files.pak("SPRITES");
    UiState& ui = g.ui;
    CoreState& core = g.core;
    core.level = static_cast<uint16_t>(level);
    g.world.sceneWanted = static_cast<uint16_t>(level);
    core.keyState = static_cast<uint16_t>(keyState);
    auto& clue = g.rec().clueWords;
    clue.fill(0x80);
    for (size_t k = 0; k < 3; ++k) clue[3 * static_cast<size_t>(level) + k] = static_cast<uint8_t>(words[k]);
    ui.hudKeyState = static_cast<uint16_t>(keyState + 1);
    if (counters) {  // a new game in the clubhouse: 5 coins, 10 nets, tier 0, prompt 0
        core.coins = 5;
        core.nets = 10;
        core.treasuresFound = 0;
        core.tier = 0;
        g.rec().foundMasks = {0, 0, 0};
        ui.coinsShown = 5;
        ui.hudCoins = 0;
        ui.promptIcon = 0;
        ui.hudPrompt = 1;
    } else {
        ui.hudCoins = ui.coinsShown = core.coins;
        ui.hudNets = core.nets;
        ui.hudFound = core.treasuresFound;
        ui.hudPrompt = ui.promptIcon;
    }
    hudRedraw(g);
    return true;
}

// The scenario of a check as a captureless lambda over the fixture `f` and the files `d`.
#define DRAW(...) [](dosgame::GameFixture& f, const DosGfxFiles& d) { return __VA_ARGS__; }

struct RenderCheck {
    const char* name;  // <name>_bg.bin / <name>.bin in the verify folder (verify.py's list)
    Look look;
    uint64_t hash;     // the render on a blank background
    bool (*draw)(dosgame::GameFixture&, const DosGfxFiles&);
};

constexpr int kA = 97, kD = 100, kB = 98, kE = 101, kN = 110;  // the names typed in the capture runs

const RenderCheck kRenderChecks[] = {
    {"title_ega", Look::Ega, 0xbb32d331665e00d2ull, DRAW(drawTitle(f, d, Look::Ega))},
    {"title_tandy", Look::Tandy, 0x739e943c49593068ull, DRAW(drawTitle(f, d, Look::Tandy))},
    {"story1_tandy", Look::Tandy, 0x36bb3206b81c2930ull, DRAW(drawTitlePhase(f, d, 2))},
    {"story2_ega", Look::Ega, 0xb826eb80a3a7d5b1ull, DRAW(drawTitlePhase(f, d, 4))},
    {"story2_tandy", Look::Tandy, 0xb826eb80a3a7d5b1ull, DRAW(drawTitlePhase(f, d, 4))},
    {"sspage_tandy", Look::Tandy, 0x9f9bf085f6ba8834ull, DRAW(drawTitlePhase(f, d, 10))},
    {"signin_ega", Look::Ega, 0x3575c75c7ae54d23ull, DRAW(drawSignIn(f, d, "original/USERS.DAT"))},
    {"signin_tandy", Look::Tandy, 0x68e0dfbc86d0f461ull, DRAW(drawSignIn(f, d, "USERS_after_exit_level2_Ada.DAT"))},
    {"newname_empty_ega", Look::Ega, 0x7ea28dff1101928dull, DRAW(drawNameEntry(f, d, "original/USERS.DAT", {}, false))},
    {"newname_typed_ega", Look::Ega, 0x8f6494dd803e81d3ull,
     DRAW(drawNameEntry(f, d, "original/USERS.DAT", {kA, kD, kA}, false))},
    {"newname_typed_tandy", Look::Tandy, 0x11abe5d0bd738234ull,
     DRAW(drawNameEntry(f, d, "USERS_after_castle_climb_Ada.DAT", {kB, kE, kN}, true))},
    {"continue_tandy", Look::Tandy, 0x78eefe90b285d59full, DRAW(drawContinuePrompt(f, d))},
    {"signin_crafted_ega", Look::Ega, 0x6d3404034cca3b08ull,
     DRAW(drawSignIn(f, d, kCrafted))},
    {"signin_esc_box_ega", Look::Ega, 0xb6371bb401d5af06ull,
     DRAW(drawSignInCommands(f, d, kCrafted, {0}, false))},
    {"signin_esc_esc_ega", Look::Ega, 0x6d3404034cca3b08ull,
     DRAW(drawSignInCommands(f, d, kCrafted, {0, 0}, true))},
    {"signin_remove_list_ega", Look::Ega, 0x31a8f0bff8481aeaull,
     DRAW(drawSignInCommands(f, d, kCrafted, {30}, true))},
    {"signin_remove_box_ega", Look::Ega, 0x3efdd12bacbfdb4bull,
     [](dosgame::GameFixture& f, const DosGfxFiles& d) {
         return drawSignInCommands(f, d, kCrafted, {30}, true) && (signInCommand(f.g(), 8), true);
     }},
    {"signin_identical_ega", Look::Ega, 0x5ff12109d3f1f97aull,
     DRAW(drawSignInCommands(f, d, kCrafted, {8, kA, kD, kA, 8}, false))},
    {"signin_nine_ega", Look::Ega, 0xb6bb153104092406ull,
     DRAW(drawSignIn(f, d, kNinePlayers))},
    {"signin_full_box_ega", Look::Ega, 0xe4c65f97b5871b03ull,
     DRAW(drawSignInCommands(f, d, kNinePlayers, {8}, false))},
    {"signin_full_typed_ega", Look::Ega, 0xe4c65f97b5871b03ull,
     DRAW(drawSignInCommands(f, d, kNinePlayers, {kA}, false))},
    {"signin_full_remove_ega", Look::Ega, 0x5da5cc10e20bc940ull,
     DRAW(drawSignInCommands(f, d, kNinePlayers, {8, 8}, true))},
    {"shift_name_ega", Look::Ega, 0xaf1025a0cf93294dull,
     DRAW(drawNameEntry(f, d, kCrafted, {88, 89, 122}, false))},
    {"shift_first_ega", Look::Ega, 0xf11fed4ffa57a4eaull,
     DRAW(drawNameEntry(f, d, kCrafted, {66, 99}, true))},
    {"menu_help_ega", Look::Ega, 0x000073bb973156d0ull, DRAW(drawMenu(f, d, {1}))},
    {"menu_file_ega", Look::Ega, 0x981c181ebd8dd04full, DRAW(drawMenu(f, d, {2}))},
    {"menu_options_ega", Look::Ega, 0x35976e414a82c220ull, DRAW(drawMenu(f, d, {3}))},
    {"menu_file_exit_ega", Look::Ega, 0xcc09f841fa17e65full, DRAW(drawMenu(f, d, {2, 7, 7}))},
    {"menu_help_tandy", Look::Tandy, 0x000073bb973156d0ull, DRAW(drawMenu(f, d, {1}))},
    {"menu_file_tandy", Look::Tandy, 0x981c181ebd8dd04full, DRAW(drawMenu(f, d, {2}))},
    {"box_music_ega", Look::Ega, 0x56504d4d8c88300full, DRAW(drawBox(f, d, 2, 1))},
    {"box_exit_ega", Look::Ega, 0x82b2ea9c837a0e51ull, DRAW(drawBox(f, d, 1, 2))},
    {"box_exit_tandy", Look::Tandy, 0x82b2ea9c837a0e51ull, DRAW(drawBox(f, d, 1, 2))},
    {"box_newplayer_tandy", Look::Tandy, 0xd89c4f666139e795ull, DRAW(drawBox(f, d, 1, 1))},
    {"box_startover_ega", Look::Ega, 0xd2f0b032447b50a0ull, DRAW(drawBox(f, d, 1, 0))},
    {"box_hints_ega", Look::Ega, 0x53651941e8d2f125ull, DRAW(drawBox(f, d, 2, 0))},
    {"box_startover_left_ega", Look::Ega, 0x7efc30de2465a0ccull, [](dosgame::GameFixture& f, const DosGfxFiles& d) {
         drawBox(f, d, 1, 0);
         f.g().input.cmd = 4;  // Left toggles the selection
         menuUpdate(f.g());
         return true;
     }},
    {"box_sound_ega", Look::Ega, 0x0f0dd825d0362c07ull, DRAW(drawBox(f, d, 2, 2))},
    {"help0_0_ega", Look::Ega, 0xce058197f6633f92ull, DRAW(drawHelp(f, d, 0, {}))},
    {"help0_1_ega", Look::Ega, 0x3240c70b047bedcaull, DRAW(drawHelp(f, d, 0, {8}))},
    {"help0_2_ega", Look::Ega, 0x5d631d20a7b43b28ull, DRAW(drawHelp(f, d, 0, {8, 8}))},
    {"help0_3_ega", Look::Ega, 0xe2cacf16973b2d7dull, DRAW(drawHelp(f, d, 0, {8, 8, 8}))},
    {"help0_4_ega", Look::Ega, 0x60c2312758b5ae84ull, DRAW(drawHelp(f, d, 0, {8, 8, 8, 8}))},
    {"help0_5_ega", Look::Ega, 0xca73920062899131ull, DRAW(drawHelp(f, d, 0, {8, 8, 8, 8, 8}))},
    {"help1_0_ega", Look::Ega, 0xf87b7ced13b55e5aull, DRAW(drawHelp(f, d, 1, {}))},
    {"help1_1_ega", Look::Ega, 0x52bf81ce3968b36aull, DRAW(drawHelp(f, d, 1, {8}))},
    {"help1_2_ega", Look::Ega, 0xb15f44ffb25b777aull, DRAW(drawHelp(f, d, 1, {8, 8}))},
    {"help1_3_ega", Look::Ega, 0xf2eb3b33a6d1d18full, DRAW(drawHelp(f, d, 1, {8, 8, 8}))},
    {"help2_0_ega", Look::Ega, 0xedf4f8ae3a165fcfull, DRAW(drawHelp(f, d, 2, {}))},
    {"help2_p2_ega", Look::Ega, 0xa3c07a4d91d31a4eull, DRAW(drawHelp(f, d, 2, {8}))},
    {"help2_pgdn_ega", Look::Ega, 0xa3c07a4d91d31a4eull, DRAW(drawHelp(f, d, 2, {14}))},
    {"help2_pgup_ega", Look::Ega, 0xedf4f8ae3a165fcfull, DRAW(drawHelp(f, d, 2, {8, 15}))},
    {"help2_next_ega", Look::Ega, 0x0ada3bcd870ff7c4ull, DRAW(drawHelp(f, d, 2, {8, 8}))},
    {"help2_next2_ega", Look::Ega, 0x0150c963f55b6e54ull, DRAW(drawHelp(f, d, 2, {8, 8, 8}))},
    {"help2_done_ega", Look::Ega, 0x000073bb973156d0ull, [](dosgame::GameFixture& f, const DosGfxFiles& d) {
         // Done on the last page: help closes and the queued F1 reopens the Help menu; the full
         // redraw of the play screen under it (the painter's part) is the background again.
         const BackBuffer playScreen = f.g().back;
         drawHelp(f, d, 2, {8, 8, 8});
         DosGame& g = f.g();
         g.input.cmd = 8;
         menuUpdate(g);
         f.runtime().stepPitTicks(75);
         menuUpdate(g);
         REQUIRE(g.input.queuedCmd == 1);
         g.back = playScreen;
         dialogCommand(f, 1);
         return true;
     }},
    {"help3_0_ega", Look::Ega, 0x554731b3031590bbull, DRAW(drawHelp(f, d, 3, {}))},
    {"help0_0_tandy", Look::Tandy, 0xce058197f6633f92ull, DRAW(drawHelp(f, d, 0, {}))},
    {"quiz_question_ega", Look::Ega, 0x6dbf8521d7036b35ull, DRAW(drawQuizQuestion(f, d))},
    {"quiz_right_ega", Look::Ega, 0x6e03de3d15ca0e2aull, DRAW(drawQuizRight(f, d))},
    {"hud_008_ega", Look::Ega, 0x036acfa84db2a4f2ull, DRAW(drawHud(f, d, 0, 8, {0x80, 0x80, 0x80}, true))},
    {"hud_032_ega", Look::Ega, 0x7f108f7a03f43679ull, DRAW(drawHud(f, d, 0, 8, {0x80, 0x80, 40}, false))},
    {"hud_049_ega", Look::Ega, 0x7aa1b7cd62d58a81ull, DRAW(drawHud(f, d, 0, 2, {1, 47, 40}, false))},
    {"hud_076_ega", Look::Ega, 0x817a616ed72127abull, DRAW(drawHud(f, d, 1, 10, {1, 45, 41}, false))},
    {"hud_094_ega", Look::Ega, 0x1ecfcd8bb649d6f9ull, DRAW(drawHud(f, d, 2, 12, {3, 46, 42}, false))},
};

}  // namespace

TEST_CASE("DOS ui: reference renders of the module paths") {
    TM_REQUIRE_DOS_GAME_FILES();
    const auto dir = verifyDir();
    for (const RenderCheck& c : kRenderChecks) {
        CAPTURE(c.name);
        // On a blank background: the stored hash.
        {
            dosgame::GameFixture f(*gameFiles, optionsFor(c.look));
            if (!c.draw(f, *gameFiles)) {
                MESSAGE(std::format("{}: reference files missing, skipped", c.name));
                continue;
            }
            const uint64_t h = hashOf(f.g().back);
            if (dir) MESSAGE(std::format("{} {:#018x}", c.name, h));
            if (c.hash != 0) CHECK(h == c.hash);
        }
        // Over the capture (verify.py): the render that is compared pixel for pixel.
        if (dir) {
            const auto bg = readWholeFile(*dir / (std::string(c.name) + "_bg.bin"));
            if (!bg || bg->size() != static_cast<size_t>(BackBuffer::kWidth) * BackBuffer::kHeight) continue;
            dosgame::GameFixture f(*gameFiles, optionsFor(c.look));
            BackBuffer& b = f.g().back;
            for (int y = 0; y < BackBuffer::kHeight; ++y)
                for (int x = 0; x < BackBuffer::kWidth; ++x)
                    b.setPixel(x, y, (*bg)[static_cast<size_t>(y) * BackBuffer::kWidth + x]);
            REQUIRE(c.draw(f, *gameFiles));
            CHECK(writeWholeFile(*dir / (std::string(c.name) + ".bin"), b.logicalPixels()));
        }
    }
}

// ---- logic of the modules ---------------------------------------------------------------------

namespace {

dosgame::FixtureOptions periodOptions() {
    dosgame::FixtureOptions o;
    o.options.frameSpeed = FrameSpeed::Period;  // one step per original frame
    return o;
}

// The menu bar as Input_Tick opens it for F1..F3 (DS:90FC = -1, DS:9111 = 0); the painter is
// skipped (scene 3) so only the menu code draws.
void openMenuBar(DosGame& g, const DosGfxFiles& files) {
    g.bank = &files.pak("SPRITES");
    g.world.sceneWanted = 3;
    g.core.uiMode = 0x40;
    g.ui.menuOpen = -1;
    g.ui.menuItem = 0;
}

void menuCommand(DosGame& g, int cmd) {
    g.input.cmd = static_cast<int16_t>(cmd);
    menuUpdate(g);
}

}  // namespace

TEST_CASE("DOS ui: Box_SetupInput rewrites the zones and keys of the box's context") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    const UiTables& t = g.tables.ui;
    const uint16_t dialogKeysAt = g.inputTables.keysAddress(2);
    SUBCASE("a Yes/No box: context 2, button zones, 11 keys with Left/Right/Tab/Up/Down/Y/N") {
        msgBoxDraw(g, 5);
        CHECK(g.input.context == 2);
        CHECK(g.input.cmd == -1);
        CHECK(g.input.queuedCmd == -1);
        const InputZoneTable& z = g.inputTables.zones(2);
        CHECK(z.count == 11);
        CHECK(z.records[8].x1 == 200);
        for (int b = 0; b < 2; ++b) {
            const InputZone& bz = z.records[static_cast<size_t>(9 + b)];
            CHECK(bz.x1 == static_cast<uint8_t>(g.ui.buttonX[static_cast<size_t>(b)] - 2));
            CHECK(bz.y1 == static_cast<uint8_t>(g.ui.buttonY[static_cast<size_t>(b)] + 3));
            CHECK(bz.x2 == static_cast<uint8_t>(t.buttonZoneWidths[1] + g.ui.buttonX[static_cast<size_t>(b)]));
            CHECK(bz.y2 == static_cast<uint8_t>(g.ui.buttonY[static_cast<size_t>(b)] + 13));
        }
        CHECK(z.records[0].release == 0);  // the footer: Esc
        const InputKeyMap& k = g.inputTables.keyMap(2);
        REQUIRE(k.keys.size() == 12);
        CHECK(k.keys[2] == 0x4B00);
        CHECK(k.keys[3] == 0x4D00);
        CHECK(k.keys[5] == 0x0F09);
        CHECK(k.keys[6] == 0x4800);
        CHECK(k.keys[7] == 0x5000);
        CHECK(k.keys[8] == 0x1559);
        CHECK(k.keys[9] == 0x314E);
        CHECK(g.ui.boxWidth == 0x6C);  // the Exit-and-Save box (26,16)-(134,184)
        CHECK(g.ui.buttonY[0] == 184 - 0x23);
    }
    SUBCASE("help pages: 9 keys, PgDn; page 0 keeps only Esc, Enter, PgDn; entries 10, 11 come back") {
        helpOpen(g, 0);
        const InputKeyMap& k0 = g.inputTables.keyMap(2);
        REQUIRE(k0.keys.size() == 10);
        CHECK(k0.keys[8] == 0x5100);
        for (int i : {2, 3, 5, 6, 7, 9}) CHECK(k0.keys[static_cast<size_t>(i)] == 0xFFFF);
        CHECK(g.ui.buttonX[0] == g.ui.buttonX[1]);
        helpDrawPage(g, 0, 1);
        const InputKeyMap& k1 = g.inputTables.keyMap(2);
        CHECK(k1.keys[9] == 0x4900);
        CHECK(k1.keys[2] == 0x4B00);
        CHECK(k1.keys[5] == 0x0F09);
        CHECK(g.ui.buttonX[0] == 0x10);
        CHECK(g.inputTables.zones(2).records[0].x1 == static_cast<uint8_t>(t.footerZoneX[2] + 8));  // disabled
        msgBoxDraw(g, 3);
        const InputKeyMap& k2 = g.inputTables.keyMap(2);
        REQUIRE(k2.keys.size() == 12);
        CHECK(k2.keys[10] == g.exe().ds16(static_cast<uint16_t>(dialogKeysAt + 20)));
        CHECK(k2.keys[11] == g.exe().ds16(static_cast<uint16_t>(dialogKeysAt + 22)));
        CHECK(k2.commands[10] == 14);
        CHECK(k2.commands[11] == 15);
    }
    SUBCASE("a drop-down: context 1, its zone count, the rows as wide as the box") {
        g.ui.menuOpen = 1;
        msgBoxDraw(g, 1);
        CHECK(g.input.context == 1);
        const InputZoneTable& z = g.inputTables.zones(1);
        CHECK(z.count == t.menuZoneCounts[1]);
        for (int i = 9; i <= 16; ++i) {
            CHECK(z.records[static_cast<size_t>(i)].x1 == 26);
            CHECK(z.records[static_cast<size_t>(i)].x2 == 26 + g.ui.boxWidth);
        }
        CHECK(g.ui.boxLines == 3);
    }
    SUBCASE("the name box and an OK box") {
        msgBoxDraw(g, 15);
        CHECK(g.input.context == 4);
        const InputZoneTable& z4 = g.inputTables.zones(4);
        CHECK(z4.count == 9);
        CHECK(z4.records[8].x1 == 25 + 0x40);
        CHECK(z4.records[8].y1 == 160 - 0x48);
        msgBoxDraw(g, 13);
        const InputZoneTable& z2 = g.inputTables.zones(2);
        CHECK(z2.count == 9);
        CHECK(z2.records[8].y2 == static_cast<uint8_t>(z2.records[0].y1 + 0x11 - 0x16));
    }
}

TEST_CASE("DOS ui: the menu bar opens, moves, wraps and opens dialogs") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    openMenuBar(g, *gameFiles);
    menuCommand(g, 1);  // F1
    CHECK(g.ui.menuOpen == 0);
    CHECK(g.ui.menuItem == 0);
    CHECK(g.input.context == 1);
    CHECK(g.ui.promptIcon == 0);
    CHECK(g.input.cmd == -1);
    CHECK(g.input.deliverNext);
    CHECK(g.ui.boxLines == 4);
    menuCommand(g, 5);  // Right
    CHECK(g.ui.menuOpen == 1);
    menuCommand(g, 4);  // Left
    CHECK(g.ui.menuOpen == 0);
    menuCommand(g, 4);  // Left wraps to Options
    CHECK(g.ui.menuOpen == 2);
    menuCommand(g, 5);  // Right wraps to Help
    CHECK(g.ui.menuOpen == 0);
    menuCommand(g, 6);  // Up wraps to the last item
    CHECK(g.ui.menuItem == 3);
    menuCommand(g, 7);  // Down wraps to the first
    CHECK(g.ui.menuItem == 0);
    menuCommand(g, 9 + 2);  // the mouse over item 2
    CHECK(g.ui.menuItem == 2);
    menuCommand(g, 3);  // F3
    CHECK(g.ui.menuOpen == 2);
    CHECK(g.ui.menuItem == 0);
    menuCommand(g, 7);
    g.keys() = 0x1F;
    menuCommand(g, 8);  // Enter: Options item 1 = dialog 12 (Music), drawn in the same call
    CHECK(g.ui.dialogId == 12);
    CHECK(g.core.uiMode == 0x20);
    CHECK(g.keys() == 0x10);  // movement bits dropped
    CHECK(g.ui.dialogShown == 1);
    CHECK(g.input.context == 2);
    // Esc closes the box without effect.
    const uint16_t music = g.core.musicOn;
    menuCommand(g, 0);
    CHECK(g.core.uiMode == 0);
    CHECK(g.ui.hudRedraw == 1);
    CHECK(g.core.musicOn == music);
    CHECK(g.input.context == 0);
    // Esc on the open bar closes it.
    openMenuBar(g, *gameFiles);
    menuCommand(g, 2);
    menuCommand(g, 0);
    CHECK(g.core.uiMode == 0);
    CHECK(g.input.context == 0);
    // A menu lock (end sequences, searches) blocks the bar.
    openMenuBar(g, *gameFiles);
    g.core.menuLock = 1;
    menuCommand(g, 1);
    CHECK(g.ui.menuOpen == -1);
}

TEST_CASE("DOS ui: Yes/No buttons and the Button_Flash wait over frames") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    f.runtime().setClient(nullptr);
    openMenuBar(g, *gameFiles);
    g.ui.menuOpen = 2;
    g.ui.menuItem = 1;
    g.ui.musicBoxDefault = 1;
    g.core.musicOn = 1;
    menuCommand(g, 8);  // the Music box, default Yes
    REQUIRE(g.ui.dialogId == 12);
    CHECK(g.ui.buttonSel == 1);
    menuCommand(g, 4);  // Left toggles
    CHECK(g.ui.buttonSel == 0);
    menuCommand(g, 9 + 1);  // a click on button 1
    CHECK(g.ui.buttonSel == 1);
    menuCommand(g, 15);  // N: No, and Enter queued
    CHECK(g.ui.buttonSel == 0);
    CHECK(g.input.queuedCmd == 8);
    g.input.queuedCmd = -1;
    // Enter: the box closes for the next redraw at once, the flash starts (first toggle shown).
    const uint64_t shown = g.screenVersion;
    menuCommand(g, 8);
    CHECK(uiFlashPending(g));
    CHECK(g.core.uiMode == 0);
    CHECK(g.ui.hudRedraw == 1);
    CHECK(g.ui.buttonSel == 1);  // toggle 1 of 4
    CHECK(g.screenVersion == shown + 1);
    CHECK(g.core.musicOn == 1);  // the handler waits for the flash
    // Nothing else runs while the original is stuck in Button_Flash.
    screenDrawOverlay(g);
    CHECK(g.ui.hudRedraw == 1);
    g.input.cmd = 14;
    gameHandleHotkeys(g);
    CHECK(g.core.musicOn == 1);
    // Toggles at +18, +36, +54 PIT ticks; the handler at +72.
    f.runtime().stepPitTicks(17);
    CHECK_FALSE(uiFlashContinue(g));
    CHECK(g.screenVersion == shown + 1);
    f.runtime().stepPitTicks(1);
    CHECK_FALSE(uiFlashContinue(g));
    CHECK(g.screenVersion == shown + 2);
    CHECK(g.ui.buttonSel == 0);
    f.runtime().stepPitTicks(36);
    CHECK_FALSE(uiFlashContinue(g));
    CHECK(g.screenVersion == shown + 4);
    CHECK(g.ui.buttonSel == 0);  // ends as it was chosen (No)
    f.runtime().stepPitTicks(17);
    CHECK_FALSE(uiFlashContinue(g));
    f.runtime().stepPitTicks(1);
    menuUpdate(g);  // Menu_Update continues its own flash
    CHECK_FALSE(uiFlashPending(g));
    CHECK(g.core.musicOn == 0);  // MenuCmd_Music with No
    CHECK(g.ui.musicBoxDefault == 0);
    CHECK(g.ui.soundChanged == 1);
    CHECK(g.input.cmd == -1);
    CHECK(g.input.deliverNext);
    CHECK(g.ui.uiCmd == -1);
}

TEST_CASE("DOS ui: menu commands and the Ctrl+S hot key") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    SUBCASE("Music: Yes also turns the effects on; a change asks Sound_SyncState") {
        g.core.musicOn = 0;
        g.core.soundOn = 0;
        g.ui.soundChanged = 0;
        g.ui.buttonSel = 1;
        menuCmdMusic(g);
        CHECK(g.core.musicOn == 1);
        CHECK(g.core.soundOn == 1);
        CHECK(g.ui.musicBoxDefault == 1);
        CHECK(g.ui.soundBoxDefault == 1);
        CHECK(g.ui.soundChanged == 1);
    }
    SUBCASE("Sound: both off") {
        g.core.musicOn = 1;
        g.core.soundOn = 1;
        g.ui.soundChanged = 0;
        g.ui.buttonSel = 0;
        menuCmdSound(g);
        CHECK(g.core.musicOn == 0);
        CHECK(g.core.soundOn == 0);
        CHECK(g.ui.soundChanged == 1);
    }
    SUBCASE("New Player Hints re-arms all 15 hints, also for No") {
        g.elves.hintFlags.fill(0x80);
        g.ui.buttonSel = 0;
        menuCmdNewPlayerHints(g);
        CHECK(g.elves.hintsOn == 0);
        CHECK(g.elves.hintTimerReset == 0);
        for (uint8_t h : g.elves.hintFlags) CHECK(h == 1);
    }
    SUBCASE("Ctrl+S in play toggles music and effects together") {
        g.core.uiMode = 0;
        g.core.area = 7;
        g.core.musicOn = 1;
        g.core.soundOn = 1;
        f.sound().clear();
        g.input.cmd = 14;
        gameHandleHotkeys(g);
        CHECK(g.core.musicOn == 0);
        CHECK(g.core.soundOn == 0);
        CHECK(g.input.cmd == -1);
        bool stopped = false;
        for (const auto& c : f.sound().calls()) stopped |= c.op == RecordingSound::Op::MusicStop;
        CHECK(stopped);
        f.sound().clear();
        g.input.cmd = 14;
        gameHandleHotkeys(g);
        CHECK(g.core.musicOn == 1);
        bool played = false;
        for (const auto& c : f.sound().calls()) played |= c.op == RecordingSound::Op::MusicPlay && c.arg == 2;  // area 7
        CHECK(played);
        // Not with a UI mode open (except on the title, area 6).
        g.core.uiMode = 2;
        g.input.cmd = 14;
        gameHandleHotkeys(g);
        CHECK(g.input.cmd == 14);
        g.core.uiMode = 0x80;
        g.core.area = 6;
        gameHandleHotkeys(g);
        CHECK(g.core.musicOn == 0);
    }
}

TEST_CASE("DOS ui: the help pager turns pages, Done reopens the Help menu, Esc closes") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    f.runtime().setClient(nullptr);
    openMenuBar(g, *gameFiles);
    g.ui.menuOpen = 0;
    g.ui.menuItem = 3;  // "Getting More Hints": one page
    menuCommand(g, 8);
    REQUIRE(g.ui.dialogId == 3);
    CHECK(g.ui.helpPageCount == g.help.pageCount(3));
    CHECK(g.ui.helpPage == 0);
    CHECK(g.core.uiMode == 0x20);  // help stays a dialog
    CHECK(g.ui.buttonSel == 1);
    REQUIRE(g.ui.helpPageCount == 1);
    menuCommand(g, 8);  // Done
    f.runtime().stepPitTicks(75);
    menuUpdate(g);
    CHECK(g.core.uiMode == 0x40);
    CHECK(g.input.context == 1);
    CHECK(g.input.queuedCmd == 1);  // F1: the Help drop-down opens again with item 0
    CHECK(g.ui.menuOpen == -1);
    g.input.queuedCmd = -1;
    menuCommand(g, 1);
    CHECK(g.ui.menuOpen == 0);
    // About the Game (6 pages): Enter, Enter, then the back button.
    menuCommand(g, 8);
    REQUIRE(g.ui.dialogId == 0);
    for (int p = 1; p <= 2; ++p) {
        menuCommand(g, 8);
        f.runtime().stepPitTicks(75);
        menuUpdate(g);
        CHECK(g.ui.helpPage == p);
    }
    menuCommand(g, 4);  // toggle to "Go Back a Page"
    CHECK(g.ui.buttonSel == 0);
    menuCommand(g, 8);
    f.runtime().stepPitTicks(75);
    menuUpdate(g);
    CHECK(g.ui.helpPage == 1);
    CHECK(g.ui.buttonSel == 0);  // the selection persists between pages
    menuCommand(g, 0);           // Esc
    CHECK(g.core.uiMode == 0);
    CHECK(g.ui.hudRedraw == 1);
}

TEST_CASE("DOS ui: title phases, the 7200-tick page timer, keys and sparkle attempts") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles, periodOptions());
    DosGame& g = f.g();
    f.runtime().setClient(nullptr);
    g.pan = &gameFiles->pan("OPENING");
    g.bank = &gameFiles->pak("BEGIN");
    g.ui.lineHeight = 12;
    titleStart(g);
    CHECK(g.core.uiMode == 0x80);
    CHECK(g.core.area == 6);
    CHECK(g.ui.lineHeight == 10);
    CHECK(g.core.musicLoop == 1);
    CHECK(g.ui.titlePhase == 0);
    // One frame per call; with period frame speed each frame tries one sparkle while slot 2 is idle.
    auto frame = [&](int cmd) {
        g.input.cmd = static_cast<int16_t>(cmd);
        const bool idle = g.player.slots[2].anim == 0 && g.ui.ssPage == 0;
        BorlandRand replay;
        replay.setRawSeed(g.rand.seed());
        if (idle && (replay.rand() & 0x7F) == 0) replay.rand();
        g.frameClock().beginFrame(5);
        const bool more = titleFrame(g);
        if (idle && g.ui.ssPage == 0) CHECK(g.rand.seed() == replay.seed());
        f.runtime().stepPitTicks(5);
        return more;
    };
    CHECK(frame(-1));
    CHECK(g.ui.titlePhase == 1);
    CHECK(g.input.context == 5);
    CHECK(g.inputTables.zones(5).records[1].release == 8);
    CHECK(frame(-1));  // the wait arms its timer
    CHECK(g.ui.titleTimer == g.ticks() - 5 + 7200);
    CHECK(frame(4));  // any command restarts the wait without changing the page
    CHECK(g.ui.titlePhase == 1);
    CHECK(g.ui.titleTimer == 0);
    CHECK(frame(-1));
    f.runtime().stepPitTicks(7200);
    CHECK(frame(-1));  // time-out: Enter
    CHECK(g.ui.titlePhase == 2);
    CHECK(frame(-1));  // story 1
    CHECK(g.ui.titlePhase == 3);
    CHECK(frame(8));
    CHECK(frame(-1));  // story 2
    CHECK(g.ui.titlePhase == 5);
    CHECK(g.inputTables.zones(5).records[1].release == 13);
    CHECK(frame(13));  // S: the Super Solvers page
    CHECK(g.ui.titlePhase == 10);
    CHECK(frame(-1));
    CHECK(g.ui.titlePhase == 11);
    CHECK(g.ui.ssPage == 1);
    CHECK(g.ui.lineHeight == 10);  // restored after the page's own 12
    CHECK(frame(0));               // Esc on the series page: back to the title
    CHECK(g.ui.titlePhase == 0);
    CHECK(g.ui.ssPage == 0);
    for (int cmd : {-1, 8, -1, 8, -1}) CHECK(frame(cmd));  // title, Enter, story 1, Enter, story 2
    CHECK(g.ui.titlePhase == 5);
    CHECK(frame(8));  // Enter on story 2: the walk-off
    CHECK(frame(-1));
    CHECK(g.ui.titlePhase == 7);
    g.player.slots[1].anim = 0x5A;
    CHECK(frame(8));  // Enter cuts the walk short
    CHECK(g.ui.titlePhase == 8);
    CHECK(g.player.slots[1].frame == 0xFFFF);
    g.sprites.records[1].frame = 0xFFFF;
    CHECK_FALSE(frame(-1));  // the actor is gone: phase 9 ends the title
    CHECK(g.ui.titlePhase == 9);
    titleFinish(g);
    CHECK(g.core.uiMode == 0);
    CHECK(g.ui.lineHeight == 12);
    CHECK(g.core.musicLoop == 0);
}

TEST_CASE("DOS ui: Esc on a title page quits") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles, periodOptions());
    DosGame& g = f.g();
    g.pan = &gameFiles->pan("OPENING");
    g.bank = &gameFiles->pak("BEGIN");
    titleStart(g);
    titleFrame(g);
    g.input.cmd = 0;
    titleFrame(g);
    CHECK(g.core.quit != 0);
}

TEST_CASE("DOS ui: the HUD counts coins up one per frame-clock step with effect 4") {
    TM_REQUIRE_DOS_GAME_FILES();
    SUBCASE("one coin per original frame (period)") {
        dosgame::GameFixture f(*gameFiles, periodOptions());
        DosGame& g = f.g();
        g.bank = &gameFiles->pak("SPRITES");
        g.core.coins = 8;
        g.ui.coinsShown = 5;
        g.ui.hudRedraw = 0;
        f.sound().clear();
        g.frameClock().beginFrame(5);
        for (int k = 1; k <= 3; ++k) {
            hudRedraw(g);
            CHECK(g.ui.coinsShown == 5 + k);
        }
        int effects = 0;
        for (const auto& c : f.sound().calls()) effects += c.op == RecordingSound::Op::SfxPlay && c.arg == 4 ? 1 : 0;
        CHECK(effects == 3);
        CHECK(g.ui.hudCoins == 8);
        hudRedraw(g);
        CHECK(g.ui.coinsShown == 8);
        g.core.coins = 6;  // a loss counts down silently
        f.sound().clear();
        hudRedraw(g);
        CHECK(g.ui.coinsShown == 7);
        CHECK(f.sound().calls().empty());
    }
    SUBCASE("the reference rate (76.3 coins per second) gives 1 or 2 per 47.3 Hz frame") {
        dosgame::GameFixture f(*gameFiles);
        DosGame& g = f.g();
        g.bank = &gameFiles->pak("SPRITES");
        g.core.coins = 200;
        g.ui.coinsShown = 0;
        int frames = 0;
        while (g.ui.coinsShown < 76 && frames < 100) {
            g.frameClock().beginFrame(5);
            hudRedraw(g);
            ++frames;
        }
        CHECK(frames >= 46);
        CHECK(frames <= 48);  // about one second of game ticks
    }
    SUBCASE("the prompt icon and the clue box follow their caches") {
        dosgame::GameFixture f(*gameFiles);
        DosGame& g = f.g();
        g.bank = &gameFiles->pak("SPRITES");
        g.ui.hudCoins = g.ui.coinsShown = g.core.coins;
        g.ui.hudNets = g.core.nets;
        g.ui.hudFound = g.core.treasuresFound;
        g.ui.hudKeyState = g.core.keyState;
        g.ui.promptIcon = 4;
        g.ui.hudPrompt = 0;
        const uint16_t dirty = g.core.dirty;
        hudRedraw(g);
        CHECK(g.ui.hudPrompt == 4);
        CHECK(g.core.dirty == dirty + 1);
        g.core.keyState = 6;
        hudRedraw(g);
        CHECK(g.ui.hudKeyState == 6);
        CHECK(g.ui.textInk == 0xCCCC);
    }
}
