// New Player Hints and the start- / end-of-game messages of the DOS edition
// (src/dos/game/hints.cpp, code_elves_layout_hints.md §6): Hints_PickNext order, the four triggers
// of Hints_CheckTriggers, opening and closing the bubble, the rank and end messages with their
// number patches in the session copy of the texts (never in the EXE's data), the crown messages
// and Flutter_Show. Only flags, modes, counts and the patched digit bytes are compared.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "dos/game/game.h"
#include "dos/game/hints.h"
#include "dos_game_fixture.h"

using namespace tmport;
using namespace tmport::dos;

namespace {

struct HintFixture {
    dosgame::GameFixture f;
    DosGame& g;
    explicit HintFixture(const DosGfxFiles& files) : f(files), g(f.g()) {
        f.placePlayer(0x400, 0x770, 0x380, 0x700, 0, 10);
        f.selectRecord(0);
        g.rec().clear();
        g.rec().clueWords.fill(0x85);  // nothing revealed
        g.core.uiMode = 0;
        g.core.slowCpu = 0;
        g.core.menuLock = 0;
        g.world.transition = 0;
        g.ui.hudRedraw = 0;
        g.elves.hintsOn = 1;
        g.elves.hintTimerReset = 0;
        g.elves.hintTimerBase = 0;
        g.elves.hint3Start = 0;
        g.elves.hintFlags.fill(1);
        g.counters().seconds = 0;
        g.keys() = 0;
        g.input.cmd = -1;
        g.mouse.buttons = 0;
        g.mouse.prevButtons = 0;
        g.ui.promptIcon = 2;
        g.core.area = 1;
    }
    // The byte of message `msg` at `off` in the session copy and in the EXE.
    uint8_t text(int msg, int off) const {
        return g.elves.messageText[g.tables.elves.messageOffset[static_cast<size_t>(msg)] + off - kMessageTextStart];
    }
    uint8_t exeText(int msg, int off) const {
        return g.exe().ds8(static_cast<uint16_t>(g.tables.elves.messageOffset[static_cast<size_t>(msg)] + off));
    }
};

}  // namespace

TEST_CASE("dos hints: Hints_PickNext returns the highest due hint once") {
    TM_REQUIRE_DOS_GAME_FILES();
    HintFixture fx(*gameFiles);
    DosGame& g = fx.g;
    auto& fl = g.elves.hintFlags;
    CHECK(hintsPickNext(g) == -1);
    CHECK(g.elves.hintsOn == 1);
    fl[2] = 0;
    fl[9] = 0;
    CHECK(hintsPickNext(g) == 9);
    CHECK(fl[9] == 0x80);
    CHECK(hintsPickNext(g) == 2);
    CHECK(hintsPickNext(g) == -1);
    // With every flag shown the hints switch off (never reached in the game: 0, 10-12, 14 stay 1).
    fl.fill(0x80);
    CHECK(hintsPickNext(g) == -1);
    CHECK(g.elves.hintsOn == 0);
}

TEST_CASE("dos hints: the triggers of hints 1, 2, 3 and 5") {
    TM_REQUIRE_DOS_GAME_FILES();
    HintFixture fx(*gameFiles);
    DosGame& g = fx.g;
    auto& fl = g.elves.hintFlags;
    auto& sec = g.counters().seconds;
    // Hint 1: 3 seconds in the start area without walking.
    g.core.area = 5;
    sec = 2;
    hintsCheckTriggers(g);
    CHECK(fl[1] == 1);
    sec = 3;
    hintsCheckTriggers(g);
    CHECK(fl[1] == 0);
    fl[1] = 1;
    g.keys() = 4;  // Left held: cancelled for good
    hintsCheckTriggers(g);
    CHECK(fl[1] == 0x80);
    g.keys() = 0;
    fl[1] = 1;
    g.core.area = 0;  // another area cancels it too
    hintsCheckTriggers(g);
    CHECK(fl[1] == 0x80);
    // Hint 2: 31 seconds without the net, on the level-0 path (area 0).
    sec = 30;
    hintsCheckTriggers(g);
    CHECK(fl[2] == 1);
    sec = 31;
    hintsCheckTriggers(g);
    CHECK(fl[2] == 0);
    fl[2] = 1;
    g.core.area = 1;
    hintsCheckTriggers(g);
    CHECK(fl[2] == 1);
    g.keys() = 0x10;
    hintsCheckTriggers(g);
    CHECK(fl[2] == 0x80);
    g.keys() = 0;
    // Hint 3: two revealed clue words of the level, then 5 more seconds without a coin drop.
    g.core.level = 1;
    g.rec().clueWords.fill(0x85);
    g.rec().clueWords[3] = 0x05;
    sec = 40;
    hintsCheckTriggers(g);
    CHECK(g.elves.hint3Start == 0);
    g.rec().clueWords[5] = 0x07;
    hintsCheckTriggers(g);
    CHECK(g.elves.hint3Start == 41);
    CHECK(g.elves.hint3Level == 1);
    sec = 45;
    hintsCheckTriggers(g);
    CHECK(fl[3] == 1);
    sec = 46;
    hintsCheckTriggers(g);
    CHECK(fl[3] == 0);
    fl[3] = 1;
    g.keys() = 0x40;  // dropping a coin cancels it
    hintsCheckTriggers(g);
    CHECK(fl[3] == 0x80);
    g.keys() = 0;
    fl[3] = 1;
    g.core.level = 0;  // so does a level change
    hintsCheckTriggers(g);
    CHECK(fl[3] == 0x80);
    // Hint 5: the net retracted empty (slot 1's last script 0x16 / 0x19) with no nets.
    g.player.slots[1].prev = 0x16;
    g.core.nets = 1;
    hintsCheckTriggers(g);
    CHECK(fl[5] == 1);
    g.core.nets = 0;
    hintsCheckTriggers(g);
    CHECK(fl[5] == 0);
    fl[5] = 0x80;
    g.player.slots[1].prev = 0x19;
    hintsCheckTriggers(g);
    CHECK(fl[5] == 0x80);
}

TEST_CASE("dos hints: Hints_Update opens and closes the bubble") {
    TM_REQUIRE_DOS_GAME_FILES();
    HintFixture fx(*gameFiles);
    DosGame& g = fx.g;
    auto& es = g.elves;
    // Gates: hints off, another UI mode, a pending HUD redraw, the menu lock, a transition.
    es.hintFlags[7] = 0;
    for (int gate = 0; gate < 5; ++gate) {
        CAPTURE(gate);
        es.hintsOn = gate == 0 ? 0 : 1;
        g.core.uiMode = gate == 1 ? 8 : 0;
        g.ui.hudRedraw = gate == 2 ? 1 : 0;
        g.core.menuLock = gate == 3 ? 1 : 0;
        g.world.transition = gate == 4 ? 1 : 0;
        hintsUpdate(g);
        CHECK(es.hintFlags[7] == 0);
    }
    es.hintsOn = 1;
    g.core.uiMode = 0;
    g.ui.hudRedraw = 0;
    g.core.menuLock = 0;
    g.world.transition = 0;
    // The timer reset (Game_Start / Options).
    es.hintTimerReset = 1;
    es.hint3Start = 9;
    g.counters().seconds = 17;
    g.core.keyState = 0x0B;
    g.mouse.buttons = 1;
    g.mouse.prevButtons = 1;
    hintsUpdate(g);
    CHECK(es.hintTimerReset == 0);
    CHECK(g.counters().seconds == 0);
    CHECK(es.hint3Start == 0);
    CHECK(es.hintKeyState == 0x0B);
    // Hint 7 opened: g_uiMode | 0x10, prompt 6, the old one saved (the HUD redraw request 2 is
    // consumed by Screen_DrawOverlay at once).
    CHECK(es.hintFlags[7] == 0x80);
    CHECK(g.core.uiMode == 0x10);
    CHECK(g.ui.promptIcon == 6);
    CHECK(es.hintSavedPrompt == 2);
    CHECK(es.hintButtons == 1);
    CHECK(g.input.deliverNext);
    // Another command does not close it; a held button released re-arms the mouse close.
    g.ui.hudRedraw = 0;
    g.input.cmd = 3;
    hintsUpdate(g);
    CHECK(g.core.uiMode == 0x10);
    g.mouse.buttons = 0;
    hintsUpdate(g);
    CHECK(es.hintButtons == 0);
    CHECK(g.mouse.prevButtons == 0);
    CHECK(g.core.uiMode == 0x10);
    // Enter closes it: full redraw, prompt restored, command consumed.
    g.input.cmd = 8;
    g.world.fullRedraw = 0;
    hintsUpdate(g);
    CHECK(g.core.uiMode == 0);
    CHECK(g.world.fullRedraw == 1);
    CHECK(g.ui.promptIcon == 2);
    CHECK(g.input.cmd == -1);
    // Esc and a button change close too; on a slow CPU no HUD redraw is requested.
    for (int how = 0; how < 2; ++how) {
        CAPTURE(how);
        es.hintFlags[4] = 0;
        g.core.slowCpu = 1;
        g.ui.hudRedraw = 0;
        g.mouse.buttons = 0;
        g.mouse.prevButtons = 0;
        hintsUpdate(g);
        CHECK(g.core.uiMode == 0x10);
        CHECK(g.ui.hudRedraw == 0);
        if (how == 0) {
            g.input.cmd = 0;
        } else {
            g.mouse.buttons = 2;
        }
        hintsUpdate(g);
        CHECK(g.core.uiMode == 0);
    }
    // Hint 13 with the buttons changing as it opens: the previous buttons are forgotten.
    g.core.slowCpu = 0;
    es.hintFlags[13] = 0;
    g.mouse.buttons = 1;
    g.mouse.prevButtons = 2;
    hintsUpdate(g);
    CHECK(g.core.uiMode == 0x10);
    CHECK(g.mouse.prevButtons == 0);
    CHECK(es.hintButtons == 1);
}

TEST_CASE("dos hints: rank start messages patch the number of treasures to the next rank") {
    TM_REQUIRE_DOS_GAME_FILES();
    HintFixture fx(*gameFiles);
    DosGame& g = fx.g;
    auto& es = g.elves;
    const auto& t = g.tables.elves;
    // First game, total 0: the Flutter introduction (message 0), no patch.
    es.messagePending = 1;
    g.core.gamesPlayed = 0;
    g.core.area = 5;
    gameoverUpdateMessages(g);
    CHECK(g.core.uiMode == 4);
    CHECK(g.core.gameInProgress == 1);
    CHECK(g.ui.promptIcon == 6);
    CHECK(es.messagePending == 1);
    // Enter closes it: pending - 1, HUD redraw 1, keys unmasked 0.
    g.input.cmd = 8;
    g.player.keysUnmasked = 5;
    gameoverUpdateMessages(g);
    CHECK(g.core.uiMode == 0);
    CHECK(es.messagePending == 0);
    CHECK(g.ui.hudRedraw == 1);
    CHECK(g.player.keysUnmasked == 0);
    CHECK(g.ui.promptIcon == 2);
    g.ui.hudRedraw = 0;

    // Later games wait until the player leaves the start area.
    g.core.gamesPlayed = 3;
    es.messagePending = 1;
    gameoverUpdateMessages(g);
    CHECK(g.core.uiMode == 0);
    g.core.area = 0;
    struct Case {
        int16_t total;
        int rank;
    };
    dosgame::GameFixture fresh(*gameFiles);
    for (const Case c : {Case{100, 3}, Case{199, 5}, Case{114, 3}, Case{3, 0}, Case{299, 6}}) {
        CAPTURE(c.total);
        g.core.uiMode = 0;
        g.ui.hudRedraw = 0;
        es.messagePending = 1;
        g.rec().totalTreasures = c.total;
        int rank = 0;
        while (rank < 7 && t.hintRankThresholds[static_cast<size_t>(rank)] <= c.total) ++rank;
        REQUIRE(rank == c.rank);
        const int msg = t.rankStartMsg[static_cast<size_t>(rank)];
        gameoverUpdateMessages(g);
        CHECK(g.core.uiMode == 4);
        const int off = t.msgDigitOffsets[static_cast<size_t>(rank)];
        if (off != -1) {
            const int n = t.hintRankThresholds[static_cast<size_t>(rank)] - c.total;
            const std::string digits = std::to_string(n);
            CHECK(fx.text(msg, off) == static_cast<uint8_t>(digits[0]));
            CHECK(fx.text(msg, off + 1) == static_cast<uint8_t>(digits.size() > 1 ? digits[1] : ' '));
            CHECK(fx.text(msg, t.msgPluralOffsets[static_cast<size_t>(rank)]) == (n == 1 ? ' ' : 's'));
            // The patch stays in this session's copy: another game starts from the EXE's bytes.
            const size_t at = t.messageOffset[static_cast<size_t>(msg)] + static_cast<size_t>(off) - kMessageTextStart;
            CHECK(fresh.g().elves.messageText[at] == fx.exeText(msg, off));
            CHECK(fresh.g().elves.messageText[at + 1] == fx.exeText(msg, off + 1));
        }
    }
    // Patch offsets exist for ranks 3, 5 and 6 only (and the end messages "some" and "all").
    int patched = 0;
    for (int r = 0; r < 8; ++r) patched += t.msgDigitOffsets[static_cast<size_t>(r)] != -1;
    CHECK(patched == 3);
}

TEST_CASE("dos hints: end-of-game messages, hint 13, and the crown messages 35 / 36") {
    TM_REQUIRE_DOS_GAME_FILES();
    HintFixture fx(*gameFiles);
    DosGame& g = fx.g;
    auto& es = g.elves;
    const auto& t = g.tables.elves;
    g.core.area = 5;
    g.core.gamesPlayed = 4;
    g.rec().totalTreasures = 50;
    struct Case {
        uint16_t found, tier;
        int idIndex, patchIndex;
    };
    // none, one, all of the tier ((tier + 2) * 3), some.
    for (const Case c : {Case{0, 0, 0, 8}, Case{1, 0, 1, 9}, Case{6, 0, 3, 11}, Case{3, 0, 2, 10}, Case{12, 2, 3, 11},
                         Case{11, 2, 2, 10}}) {
        CAPTURE(c.found);
        g.core.uiMode = 0;
        g.ui.hudRedraw = 0;
        es.messagePending = 2;
        es.lastGameTreasures = c.found;
        es.lastGameTier = c.tier;
        es.hintFlags[13] = 1;
        gameoverUpdateMessages(g);
        CHECK(g.core.uiMode == 4);
        CHECK(es.hintFlags[13] == 0);  // hint 13 armed
        const int msg = t.endMsgIds[static_cast<size_t>(c.idIndex)];
        const int off = t.msgDigitOffsets[static_cast<size_t>(c.patchIndex)];
        if (off != -1) {
            const std::string digits = std::to_string(c.found);
            CHECK(fx.text(msg, off) == static_cast<uint8_t>(digits[0]));
            CHECK(fx.text(msg, off + 1) == static_cast<uint8_t>(digits.size() > 1 ? digits[1] : ' '));
        }
    }
    CHECK(t.msgDigitOffsets[10] != -1);
    CHECK(t.msgDigitOffsets[11] != -1);
    // Hint 13 already shown: stays.
    g.core.uiMode = 0;
    g.ui.hudRedraw = 0;
    es.messagePending = 2;
    es.hintFlags[13] = 0x80;
    gameoverUpdateMessages(g);
    CHECK(es.hintFlags[13] == 0x80);

    // Totals 300..314: message 35 with the player's name (toggle 1 -> 0, pending 3), then 36.
    g.rec().setName("Zed");
    g.rec().totalTreasures = 300;
    es.crownMsgToggle = 1;
    g.core.uiMode = 0;
    g.ui.hudRedraw = 0;
    es.messagePending = 2;
    gameoverUpdateMessages(g);
    CHECK(es.messagePending == 3);
    CHECK(es.crownMsgToggle == 0);
    const auto name = [&](int i) { return g.elves.messageText[0x7BB2 - kMessageTextStart + static_cast<size_t>(i)]; };
    CHECK(name(0) == 'Z');
    CHECK(name(2) == 'd');
    CHECK(name(3) == ' ');
    CHECK(name(7) == ' ');
    CHECK(0x7BB2 >= t.messageOffset[35]);
    g.input.cmd = 0;
    gameoverUpdateMessages(g);
    CHECK(es.messagePending == 2);
    g.ui.hudRedraw = 0;
    gameoverUpdateMessages(g);  // message 36 even in the start area (pending 2)
    CHECK(g.core.uiMode == 4);
    CHECK(es.crownMsgToggle == 1);
    CHECK(es.messagePending == 2);
    g.rec().totalTreasures = 315;  // past the crown range: the normal end messages
    g.input.cmd = 8;
    gameoverUpdateMessages(g);
    CHECK(es.messagePending == 1);
    // Message gates: a HUD redraw, another UI mode (other than 4).
    g.core.area = 0;
    g.ui.hudRedraw = 1;
    gameoverUpdateMessages(g);
    CHECK(g.core.uiMode == 0);
    g.ui.hudRedraw = 0;
    g.core.uiMode = 0x40;
    gameoverUpdateMessages(g);
    CHECK(g.core.uiMode == 0x40);
}

TEST_CASE("dos hints: Flutter_Show draws the cloud and Flutter, ink black, g_dirty") {
    TM_REQUIRE_DOS_GAME_FILES();
    HintFixture fx(*gameFiles);
    DosGame& g = fx.g;
    REQUIRE(g.bank != nullptr);
    auto pixels = [&]() { return g.back.logicalPixels(); };
    g.back.clear(0x33);
    const auto before = pixels();
    g.core.dirty = 0;
    g.core.uiMode = 2;  // quiz: the cloud alone
    flutterShow(g, 31);
    const auto cloudOnly = pixels();
    CHECK(g.ui.textInk == 0xFFFF);
    CHECK(g.core.dirty == 1);
    const auto changed = [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
        size_t n = 0;
        for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
        return n;
    };
    CHECK(changed(before, cloudOnly) > 1000);
    g.back.clear(0x33);
    g.core.uiMode = 0x10;
    flutterShow(g, 31);
    const auto withFlutter = pixels();
    CHECK(changed(cloudOnly, withFlutter) > 0);
}
