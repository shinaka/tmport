// hints.c (segment 49a6): the New Player Hints and the start- and end-of-game messages in Flutter's
// bubble (code_elves_layout_hints.md §6). Both run per frame; their timers count hint "seconds" of
// 60 ticks (g_seconds, paused and phase-shifted while a UI mode is open: TickCounters::advance).
// The message texts are patched in the session's writable copy ElfState::messageText, never in the
// EXE's data.
#include "dos/game/hints.h"

#include <string>

#include "dos/game/game.h"
#include "dos/game/hud.h"
#include "dos/game/ui.h"
#include "dos/gfx/blitter.h"

namespace tmport::dos {

namespace {

constexpr uint8_t kHintDue = 0, kHintDone = 0x80;  // g_hintFlags values (1 = waiting)
constexpr int kHintIdle = 1, kHintNoNet = 2, kHintClues = 3, kHintNoNets = 5, kHintEndMessage = 13;
constexpr int kHintCount = 15;
constexpr uint16_t kAreaStart = 5, kAreaLevel0 = 0;  // g_area: start area of a new game, level-0 path
constexpr uint16_t kKeyLeft = 4, kKeyRight = 8, kKeyNet = 0x10, kKeyCoin = 0x40;  // g_keys bits
constexpr uint16_t kNetRetractLeft = 0x16, kNetRetractRight = 0x19;  // slot 1 scripts of an empty net
constexpr int16_t kCmdEnter = 8, kCmdEsc = 0;
constexpr uint8_t kModeHint = 0x10, kModeMessage = 4;
constexpr uint16_t kPromptEnter = 6;  // "Press Enter to go on" (SPRITES 63)
constexpr uint16_t kFrameCloud = 0x8E, kFrameFlutter = 0x90;  // SPRITES 71 and 72 (frame codes)
constexpr int kMsgFlutterIntro = 0, kMsgCrown = 35, kMsgCrownAgain = 36;
constexpr int kEndIndexNone = 8, kEndIndexOne = 9, kEndIndexSome = 10, kEndIndexAll = 11;
// Message 35's 8 name bytes: a fixed address in the code (49a6:04xx writes DS:7BB2..7BB9).
constexpr uint16_t kCrownNameText = 0x7BB2;

// Byte i of the clue words of the record (+0x81) as the hint-3 count reads them: g_level * 3 + i
// runs past the 9 words into the collected list (+0x8A) in levels 3 and 4.
uint8_t clueWordByte(const DosGame& g, int i) {
    if (!g.hasRec()) return 0x80;
    const UserRecord& r = g.rec();
    if (i < static_cast<int>(r.clueWords.size())) return r.clueWords[static_cast<size_t>(i)];
    const int c = i - static_cast<int>(r.clueWords.size());
    return c < static_cast<int>(r.collected.size()) ? r.collected[static_cast<size_t>(c)] : 0x80;
}

// itoa(n) into the text of `msg` at `offset`: the first digit, then the second one or a space
// (a third digit is dropped).
void patchNumber(DosGame& g, int msg, int16_t offset, int n) {
    const std::string digits = std::to_string(static_cast<int16_t>(n));
    const auto at = static_cast<uint16_t>(g.tables.elves.messageOffset[static_cast<size_t>(msg)] + offset);
    messageTextByte(g, at) = static_cast<uint8_t>(digits[0]);
    messageTextByte(g, static_cast<uint16_t>(at + 1)) = static_cast<uint8_t>(digits.size() > 1 ? digits[1] : ' ');
}

// The bubble close rule shared by the hint and the message handlers: Enter, Esc, or a change of the
// mouse buttons when no button was held as the bubble opened.
bool closeRequested(const DosGame& g, int16_t cmd) {
    return cmd == kCmdEnter || cmd == kCmdEsc || (g.mouse.prevButtons != g.mouse.buttons && g.elves.hintButtons == 0);
}

// A button held as the bubble opened has been released: from now on a press closes it.
void releaseHeldButton(DosGame& g) {
    if (g.mouse.buttons == 0 && g.elves.hintButtons == 1) {
        g.elves.hintButtons = 0;
        g.mouse.prevButtons = 0;
    }
}

// Opens the bubble with text `msg` (both handlers): overlay redraw first on a fast CPU, Flutter,
// prompt "Enter".
void openBubble(DosGame& g, int msg) {
    if (g.core.slowCpu != 1) {
        g.ui.hudRedraw = 2;
        screenDrawOverlay(g);
    }
    flutterShow(g, msg);
    g.elves.hintSavedPrompt = g.ui.promptIcon;
    g.ui.promptIcon = kPromptEnter;
}

}  // namespace

// 49a6:000f Hints_Update
void hintsUpdate(DosGame& g) {
    ElfState& es = g.elves;
    if (es.hintsOn != 1 || (g.core.uiMode & 0xEF) != 0 || g.ui.hudRedraw != 0 || g.core.menuLock != 0 ||
        g.world.transition != 0)
        return;
    // g_cmd is read once, with g_cmdConsumed = 0 so that Input_Poll queues behind it.
    const int16_t cmd = g.input.cmd;
    g.input.deliverNext = false;
    if (es.hintTimerReset == 1) {
        g.counters().seconds = 0;
        es.hintTimerBase = 0;
        es.hintTimerReset = 0;
        es.hintKeyState = g.core.keyState;
        es.hint3Start = 0;
    }
    if ((g.core.uiMode & kModeHint) == 0) {
        hintsCheckTriggers(g);
        const int id = hintsPickNext(g);
        if (id != -1) {
            g.core.uiMode |= kModeHint;
            openBubble(g, id);
            if (id == kHintEndMessage && g.mouse.buttons != g.mouse.prevButtons) g.mouse.prevButtons = 0;
            es.hintButtons = g.mouse.buttons;
        }
    } else if (closeRequested(g, cmd)) {
        // The full play-field redraw removes the bubble.
        g.input.cmd = -1;
        g.world.fullRedraw = 1;
        g.core.uiMode &= static_cast<uint8_t>(~kModeHint);
        g.ui.promptIcon = es.hintSavedPrompt;
    } else {
        releaseHeldButton(g);
    }
    g.input.deliverNext = true;
}

// 49a6:0121 Hints_CheckTriggers
void hintsCheckTriggers(DosGame& g) {
    ElfState& es = g.elves;
    auto& flags = es.hintFlags;
    const uint16_t keys = g.keys();
    const uint16_t seconds = g.counters().seconds;
    // Hint 1: 3 seconds in the start area without walking; walking or leaving cancels it.
    if ((flags[kHintIdle] & kHintDone) == 0) {
        if ((keys & kKeyLeft) == 0 && (keys & kKeyRight) == 0 && g.core.area == kAreaStart) {
            if (static_cast<uint16_t>(es.hintTimerBase + 3) <= seconds) flags[kHintIdle] = kHintDue;
        } else {
            flags[kHintIdle] = kHintDone;
        }
    }
    // Hint 2: 31 seconds without a net swing, shown on the level-0 path.
    if ((flags[kHintNoNet] & kHintDone) == 0) {
        if ((keys & kKeyNet) == 0) {
            if (static_cast<uint16_t>(es.hintTimerBase + 0x1F) <= seconds && g.core.area == kAreaLevel0)
                flags[kHintNoNet] = kHintDue;
        } else {
            flags[kHintNoNet] = kHintDone;
        }
    }
    // Hint 3: two clue words of the level known, then 5 more seconds without dropping a coin on the
    // same level.
    if ((flags[kHintClues] & kHintDone) == 0) {
        if (es.hint3Start == 0) {
            int known = 0;
            for (int i = 0; i < 3; ++i)
                if ((clueWordByte(g, g.core.level * 3 + i) & 0x80) == 0) ++known;
            if (known > 1) {
                es.hint3Start = static_cast<uint16_t>(seconds + 1);
                es.hint3Level = g.core.level;
            }
        } else if ((keys & kKeyCoin) == 0 && es.hint3Level == g.core.level) {
            if (static_cast<uint16_t>(es.hint3Start + 5) <= seconds) flags[kHintClues] = kHintDue;
        } else {
            flags[kHintClues] = kHintDone;
        }
    }
    // Hint 5: the net retracted empty with no nets left.
    const uint16_t prev = g.player.slots[1].prev;
    if ((flags[kHintNoNets] & kHintDone) == 0 && (prev == kNetRetractLeft || prev == kNetRetractRight) &&
        g.core.nets == 0)
        flags[kHintNoNets] = kHintDue;
}

// 49a6:022b Hints_PickNext
int hintsPickNext(DosGame& g) {
    auto& flags = g.elves.hintFlags;
    int id = kHintCount - 1;
    while (id > -1 && flags[static_cast<size_t>(id)] != kHintDue) --id;
    if (id == -1) {
        // Dead branch in practice: flags 0, 10, 11, 12 and 14 are never armed (§6.6).
        int i = kHintCount - 1;
        while (i > -1 && (flags[static_cast<size_t>(i)] & kHintDone) != 0) --i;
        if (i == -1) g.elves.hintsOn = 0;
    } else {
        flags[static_cast<size_t>(id)] = kHintDone;
    }
    return id;
}

// 49a6:026f Gameover_UpdateMessages
void gameoverUpdateMessages(DosGame& g) {
    ElfState& es = g.elves;
    const ElfTables& t = g.tables.elves;
    if ((g.core.uiMode & 0xFB) != 0) return;
    if ((g.core.uiMode & kModeMessage) != 0) {
        const int16_t cmd = g.input.cmd;
        g.input.deliverNext = false;
        if (closeRequested(g, cmd)) {
            es.messagePending = static_cast<uint16_t>(es.messagePending - 1);
            g.input.cmd = -1;
            g.ui.hudRedraw = 1;
            g.core.uiMode &= static_cast<uint8_t>(~kModeMessage);
            g.ui.promptIcon = es.hintSavedPrompt;
            g.player.keysUnmasked = 0;
        } else {
            releaseHeldButton(g);
        }
        g.input.deliverNext = true;
        return;
    }
    // After the first game the start message waits until the player leaves the start area.
    if (es.messagePending == 0 || g.ui.hudRedraw != 0 || g.core.menuLock != 0 || g.world.transition != 0) return;
    if (g.core.area == kAreaStart && g.core.gamesPlayed != 0 && es.messagePending != 2) return;
    const uint16_t total = g.hasRec() ? static_cast<uint16_t>(g.rec().totalTreasures) : 0;
    int msg = 0;  // pending is 1 or 2 here (3 only lasts while message 35 is open)
    if (es.messagePending == 1) {
        // The rank start message: rank = thresholds passed (unsigned compares), with the number of
        // treasures to the next rank and its plural letter patched in.
        int rank = 0;
        while (rank < static_cast<int>(t.hintRankThresholds.size()) && t.hintRankThresholds[static_cast<size_t>(rank)] <= total)
            ++rank;
        msg = (rank == 0 && g.core.gamesPlayed == 0) ? kMsgFlutterIntro : t.rankStartMsg[static_cast<size_t>(rank)];
        const int16_t digitOff = t.msgDigitOffsets[static_cast<size_t>(rank)];
        if (digitOff != -1 && rank < static_cast<int>(t.hintRankThresholds.size())) {
            const int n = static_cast<int16_t>(t.hintRankThresholds[static_cast<size_t>(rank)] - total);
            patchNumber(g, msg, digitOff, n);
            const auto plural = static_cast<uint16_t>(t.messageOffset[static_cast<size_t>(msg)] +
                                                      t.msgPluralOffsets[static_cast<size_t>(rank)]);
            messageTextByte(g, plural) = static_cast<uint8_t>(n == 1 ? ' ' : 's');
        }
        g.core.gameInProgress = 1;
    } else if (es.messagePending == 2) {
        int index = kEndIndexOne;
        if (total < 300 || total > 0x13A) {
            // The end message by the treasures of the last game: none, one, all of the tier, some.
            const uint16_t n = es.lastGameTreasures;
            if (n == 0) {
                msg = t.endMsgIds[0];
                index = kEndIndexNone;
            } else if (n == 1) {
                msg = t.endMsgIds[1];
                index = kEndIndexOne;
            } else if (static_cast<uint16_t>((es.lastGameTier + 2) * 3) <= n) {
                msg = t.endMsgIds[3];
                index = kEndIndexAll;
            } else {
                msg = t.endMsgIds[2];
                index = kEndIndexSome;
            }
        } else if (es.crownMsgToggle == 0) {
            // Totals 300..314 (the crown was won back): message 35 with the name, then 36.
            msg = kMsgCrownAgain;
            es.crownMsgToggle = 1;
        } else {
            msg = kMsgCrown;
            es.crownMsgToggle = 0;
            es.messagePending = 3;
            for (int i = 0; i < 8; ++i) {
                const uint8_t c = g.hasRec() ? g.rec().name[static_cast<size_t>(i)] : 0;
                messageTextByte(g, static_cast<uint16_t>(kCrownNameText + i)) = static_cast<uint8_t>(c == 0 ? ' ' : c);
            }
        }
        const int16_t digitOff = t.msgDigitOffsets[static_cast<size_t>(index)];
        if (digitOff != -1) patchNumber(g, msg, digitOff, es.lastGameTreasures);
        if ((es.hintFlags[kHintEndMessage] & kHintDone) == 0) es.hintFlags[kHintEndMessage] = kHintDue;
    }
    g.core.uiMode = kModeMessage;  // an assignment
    openBubble(g, msg);
    es.hintButtons = g.mouse.buttons;
}

// 49a6:0565 Flutter_Show
void flutterShow(DosGame& g, int msg) {
    // The cloud (its header offsets are the screen position), with Flutter in the hint and message
    // modes (the quiz's praise and oops messages show the cloud alone).
    if (const PakFrame* cloud = g.bankFrame(kFrameCloud)) drawSprite(g.back, *cloud, 0, 0, sprite_flags::kMasked);
    if ((g.core.uiMode & kModeHint) != 0 || (g.core.uiMode & kModeMessage) != 0) {
        if (const PakFrame* flutter = g.bankFrame(kFrameFlutter)) drawSprite(g.back, *flutter, 0, 0, sprite_flags::kMasked);
    }
    g.ui.textInk = 0xFFFF;
    const auto& texts = g.tables.elves.messageOffset;
    if (msg >= 0 && msg < static_cast<int>(texts.size())) textDraw(g, 0x14, 0x22, g.dsString(texts[static_cast<size_t>(msg)]));
    g.core.dirty = 1;
}

}  // namespace tmport::dos
