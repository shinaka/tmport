// signin.c (segment 47c7) of the 1990 DOS edition: the Club Sign-In (the roster sorted by treasures,
// new names, remove mode, the sign-in dialogs), the USERS.DAT image and its writes at the original's
// moments, the record pack / restore field by field, and Game_Start / Game_Restore / Game_End. The
// image is DosGame::users (DosUsersStore: the engine's dos_users.dat, byte for byte the DOS format).
// Spec: re/dos/docs/code_ui.md §9; fmt_data.md §4.
#include "dos/game/signin.h"

#include <string>
#include <utility>

#include "dos/data/dgroup_offsets.h"
#include "dos/game/anim.h"
#include "dos/game/camera.h"
#include "dos/game/game.h"
#include "dos/game/hud.h"
#include "dos/game/init.h"
#include "dos/game/layout.h"
#include "dos/game/main_loop.h"
#include "dos/game/quiz.h"
#include "dos/game/scene.h"
#include "dos/game/sound.h"
#include "dos/game/ui.h"
#include "dos/gfx/blitter.h"
#include "dos/gfx/scenery.h"
#include "dos/text/ui_draw.h"

namespace tmport::dos {

namespace {

constexpr uint8_t kModeSignIn = 0x08, kModeQuiz = 0x02;
constexpr uint16_t kAreaSignIn = 4, kAreaNewGame = 5, kAreaTitle = 6, kAreaCastle = 0x0D;
constexpr int kPanOpening = 14;
constexpr int kCtxPlay = 0, kCtxSignIn = 4;
constexpr int kCmdEsc = 0, kCmdUp = 6, kCmdDown = 7, kCmdEnter = 8;
constexpr int kFirstRowCmd = 9, kLastRowCmd = 0x11;  // mouse rows 0..8 (zones 9..17)
constexpr uint8_t kCharCtrlR = 0x1E, kCharBackspace = 0x1F, kCharSpace = 0x20, kCharLast = 0x7A;
constexpr int kMaxNameLength = 8;
constexpr int kRows = 9;
constexpr int kRowY = 0x41, kRowStep = 11;
constexpr uint16_t kFrameSignInTitle = 0x3E;  // BEGIN frame 31: "Club Sign-In"

// Sign-in sub-states DS:BA07 (code_ui.md §9.1).
enum : uint16_t {
    kStateList = 1,
    kStateTyping = 3,      // 1 | 2 (SignIn_DrawNameEntry ORs 2 in)
    kStateRemove = 5,
    kStateExitBox = 9,
    kStateRemoveBox = 0x0D,
    kStateSavedBox = 0x11,
    kStateOkBox = 0x21,
};

// Message boxes and dialog ids of the sign-in (code_ui.md §4.1, §5.2).
constexpr int kBoxExit = 9, kBoxRemove = 10, kBoxSavedGame = 11, kBoxListFull = 13, kBoxNameExists = 14,
              kBoxNewName = 15, kBoxOneMoment = 22;
constexpr uint16_t kDlgExit = 14, kDlgRemove = 15, kDlgSavedGame = 16, kDlgNameExists = 17, kDlgListFull = 18;

UsersDat& image(DosGame& g) { return g.users.users(); }
UserRecord& recordOfRow(DosGame& g, int row) {
    return g.users.record(g.ui.signinRecIdx[static_cast<size_t>(row) % kRows] % kRows);
}

// The list's record name (g_userNamePtrs DS:6144[i]) as a string.
std::string_view nameOf(const UserRecord& r) {
    size_t n = 0;
    while (n < r.name.size() && r.name[n] != 0) ++n;
    return {reinterpret_cast<const char*>(r.name.data()), n};
}

std::string_view newNameView(const DosGame& g) {
    size_t n = 0;
    while (n < g.ui.newName.size() && g.ui.newName[n] != 0) ++n;
    return {g.ui.newName.data(), n};
}

// The list-full box (from the list's Enter on "New Player" and from a typed first letter).
void openListFullBox(DosGame& g) {
    g.ui.signinState = kStateOkBox;
    g.ui.dialogId = kDlgListFull;
    msgBoxDraw(g, kBoxListFull);
    g.core.dirty = 1;
}

// A mouse row: the highlight moves there (rows press / held 9 + row).
void highlightRow(DosGame& g, int row) {
    signInMoveHighlight(g);
    g.ui.signinRow = static_cast<int8_t>(row);
    signInMoveHighlight(g);
}

// SignIn_Update's tail (47c7:00d5): the command is consumed.
void signInUpdateTail(DosGame& g) {
    g.input.cmd = -1;
    g.input.deliverNext = true;
}

// SignIn_HandleInput's tail (47c7:0ff3): a chosen player starts.
void signInHandleInputTail(DosGame& g) {
    if (g.ui.signinStart != 0) {
        usersLoadPlayer(g);
        gameStart(g);
        g.ui.hudRedraw = 1;
    }
}

// g_musicOn / g_soundOn = the record's words (the driver reads the DGROUP flags: the facade gets them).
void setSoundWords(DosGame& g, uint16_t music, uint16_t sound) {
    g.core.musicOn = music;
    g.core.soundOn = sound;
    g.sound.setMusicOn(music != 0);
    g.sound.setSoundOn(sound != 0);
}

// The record cell index tables can only name cells of the 256-word grids.
bool cellOk(uint16_t cell) { return cell < 256; }

// g_levelPicData DS:122E as the flat word array the pack / restore tables index.
uint16_t* levelPicWord(DosGame& g, int8_t flat) {
    if (flat < 0 || flat >= 12) return nullptr;
    return &g.world.levelPicData[static_cast<size_t>(flat) / 4][static_cast<size_t>(flat) % 4];
}

}  // namespace

// ---- sign-in screen (§9.2-§9.6) ---------------------------------------------------------------

// 47c7:0007 SignIn_Update
void signInUpdate(DosGame& g) {
    if (uiFlashPending(g)) {  // a sign-in box's Button_Flash is still waiting
        if (g.ui.dialogId >= kDlgExit) uiFlashContinue(g);
        return;
    }
    UiState& ui = g.ui;
    CoreState& core = g.core;
    if (!(core.uiMode & kModeSignIn)) return;
    if (ui.signinInit == 0) {
        ui.textInk = 0xFFFF;
        signInHandleInput(g);
        if (uiFlashPending(g)) return;  // the rest runs when the flash ends (signInFlashDone)
    } else {
        if (core.area != kAreaTitle && core.area != kAreaSignIn) {  // coming back from a game
            core.quit = 1;
            if (core.startDrive < 2) {  // floppies only (the engine always runs as from drive C:)
                msgBoxDraw(g, kBoxOneMoment);
                presentDirty(g.back, g.screen);
                ++g.screenVersion;
                ui.boxSaved = 0;
                g.boxSave.reset();
                g.sprites.heapOffset = g.sprites.heapSegment = g.sprites.heapBase = 0;
                g.input.context = kCtxPlay;
            }
            sceneLoadPan(g, kPanOpening);
            spritesLoadBegin(g);
            core.quit = 0;
            screenDrawBackdrop(g);
        }
        sfxStop(g);
        core.area = kAreaSignIn;
        ui.signinState = kStateList;
        ui.textInk = 0xFFFF;
        ui.signinInit = 0;
        ui.signinStart = 0;
        ui.newName.fill(0);
        ui.newNameLen = 0;
        ui.signinAny = static_cast<uint8_t>(usersEnsureLoaded(g));
        uiForceRedraw(g);
        ui.hudRedraw = 1;
        ui.screen = 2;
    }
    signInUpdateTail(g);
}

void signInFlashDone(DosGame& g) {
    signInHandleInputTail(g);
    signInUpdateTail(g);
}

// 47c7:0f8b SignIn_HandleInput
void signInHandleInput(DosGame& g) {
    UiState& ui = g.ui;
    ui.uiCmd = g.input.cmd;
    g.input.deliverNext = false;
    if (ui.signinState == kStateRemove || ui.signinState == kStateList)  // the rows that take clicks
        g.inputTables.zones(kCtxSignIn).count =
            static_cast<uint8_t>((ui.signinState != kStateRemove ? 1 : 0) + ui.signinCount + 9);
    // The character of the command (its low byte); the first letter of a name is upper-cased. "No
    // command" (-1) gives 0xFF, or 0xDF before the first letter: not printable for every handler.
    ui.signinChar = static_cast<uint8_t>(g.input.cmd);
    if (ui.signinChar > 0x60 && ui.newNameLen == 0) ui.signinChar &= 0xDF;
    // The handler of the sub-state (DS:6186 -> DS:616A; the scan's entries 7..13 read the zone
    // table DS:6194 and never equal a sub-state).
    switch (ui.signinState) {
        case kStateList: signInCmdSelect(g); break;
        case kStateTyping: signInCmdNewName(g); break;
        case kStateRemove: signInCmdRemove(g); break;
        case kStateExitBox: case kStateRemoveBox: case kStateSavedBox: dialogHandleButtons(g); break;
        case kStateOkBox: dialogHandleOk(g); break;
        default: break;
    }
    if (uiFlashPending(g)) return;
    signInHandleInputTail(g);
}

// 47c7:03cf SignIn_Draw
void signInDraw(DosGame& g) {
    UiState& ui = g.ui;
    if (ui.hudRedraw == 0) return;
    signInDrawPanel(g);
    if (ui.signinAny == 0) {  // nobody signed up yet: straight to the name entry
        ui.signinChar = 0;
        signInDrawNameEntry(g);
    } else {
        textDraw(g, 4, 0xBD, g.dsString(dg::kStrSigninKeyLine));
        ui.signinRow = ui.signinState == kStateRemove ? int8_t{0} : ui.signinCount;  // "New Player", or the first player
        signInSetupZones(g);
        presentDirty(g.back, g.screen);
        ++g.screenVersion;
        signInMoveHighlight(g);
        presentDirty(g.back, g.screen);
        ++g.screenVersion;
    }
    ui.hudRedraw = 0;
}

// 47c7:00e7 SignIn_DrawPanel
void signInDrawPanel(DosGame& g) {
    UiState& ui = g.ui;
    if (g.core.slowCpu != 1) screenDrawBackdrop(g);
    g.back.fillRect(5, 0x1E, 0x6F, 0xAD, 0xAA);
    drawBankFrame(g, kFrameSignInTitle, 0, 0, sprite_flags::kMasked);
    const bool remove = ui.signinState == kStateRemove;
    ui.textInk = remove ? 0x4444 : 0x0000;
    textDraw(g, remove ? 0x16 : 0x18, 0x34, g.dsString(g.tables.ui.signinHeadings[remove ? 0 : 1]));
    ui.textInk = 0xFFFF;
    if (ui.signinAny != 0) signInDrawList(g);
    g.core.dirty = 1;
}

// 47c7:0189 SignIn_DrawNameEntry
void signInDrawNameEntry(DosGame& g) {
    UiState& ui = g.ui;
    signInDrawPanel(g);
    msgBoxDraw(g, kBoxNewName);
    ui.signinState |= 2;  // a word OR at DS:BA06: 2 into the sub-state byte DS:BA07
    gfxFrameRect(g, 0x30, 0x6F, 0x70, 0x84, 0xFFFF);
    if (ui.signinChar != 0) signInDrawTypedChar(g, 0x35, 0x75, ui.signinChar);  // the letter that started it
    ui.caretOn = 1;
    signInDrawField(g, true);
}

// 47c7:01e0 SignIn_DrawList
void signInDrawList(DosGame& g) {
    UiState& ui = g.ui;
    int16_t total[kRows] = {};
    int n = 0;
    for (int i = 0; i < kRows; ++i) {
        const UserRecord& r = g.users.record(i);
        if (r.name[0] == 0) continue;
        ui.signinRecIdx[static_cast<size_t>(n)] = static_cast<uint8_t>(i);
        total[n] = r.totalTreasures;
        ++n;
    }
    ui.signinCount = static_cast<int8_t>(n);
    // Shell sort by total, descending, exactly as the original (ties keep this order's outcome).
    for (int gap = n / 2; gap > 0; gap /= 2) {
        for (int i = gap; i < n; ++i) {
            for (int j = i - gap; j >= 0 && total[j] < total[j + gap]; j -= gap) {
                std::swap(total[j], total[j + gap]);
                std::swap(ui.signinRecIdx[static_cast<size_t>(j)], ui.signinRecIdx[static_cast<size_t>(j + gap)]);
            }
        }
    }
    int y = kRowY;
    for (int k = 0; k < n; ++k, y += kRowStep) {
        int rank = 0;  // the thresholds DS:629D the total reaches (signed)
        const auto& thresholds = g.tables.ui.signinRankThresholds;
        while (rank < 7 && static_cast<int16_t>(thresholds[static_cast<size_t>(rank)]) <= total[k]) ++rank;
        textDraw(g, 8, y, nameOf(g.users.record(ui.signinRecIdx[static_cast<size_t>(k)])));
        textDraw(g, 0x3B, y, g.dsString(static_cast<uint16_t>(dg::kRankNames + dg::kRankNamesStride * rank)));
        textDraw(g, 0x5E, y, std::to_string(total[k]));  // itoa, signed
    }
    if (ui.signinState != kStateRemove) textDraw(g, 8, y, g.dsString(dg::kStrNewPlayer));
}

// 47c7:042d Screen_DrawBackdrop
void screenDrawBackdrop(DosGame& g) {
    if (g.pan) drawFullScreenPicture(g.back, *g.pan);  // 40 Tile_DrawColumn calls of the loaded PAN
}

// 47c7:05a9 SignIn_DrawTypedChar
void signInDrawTypedChar(DosGame& g, uint8_t x, uint8_t y, uint8_t ch) {
    g.font().drawChar(g.back, g.ui.textInk, x, y, ch);
    g.core.dirty = 1;
}

// 47c7:05d4 SignIn_DrawField
void signInDrawField(DosGame& g, bool on) {
    const UiState& ui = g.ui;
    const auto w = static_cast<uint8_t>(textWidth(g, newNameView(g)));
    auto x1 = static_cast<uint8_t>(w + 0x36);
    auto x2 = static_cast<uint8_t>(x1 + 4);
    g.back.fillRect(x1, 0x76, x2, 0x7E, ui.caretOn == 1 ? 0xFF : 0xCC);  // the block cursor
    if (on) {
        g.back.fillRect(x2 + 1, 0x7E, 0x6F, 0x7E, 0xCC);
        for (int k = ui.newNameLen + 1; k < kMaxNameLength; ++k) {  // one underscore per free cell
            x1 = static_cast<uint8_t>(x2 + 2);
            x2 = static_cast<uint8_t>(x1 + 4);
            g.back.fillRect(x1, 0x7E, x2, 0x7E, 0xFF);
        }
    }
    g.core.dirty = 1;
}

// 47c7:06d4 SignIn_HighlightRow
void signInHighlightRow(DosGame& g, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint16_t a, uint16_t b) {
    exchangeColors(g.back, x1, y1, x2, y2, a, b);  // a -> 3, b -> a, 3 -> b
}

// 47c7:071a SignIn_MoveHighlight
void signInMoveHighlight(DosGame& g) {
    if (g.ui.signinAny == 0) return;
    const auto y = static_cast<uint8_t>(g.ui.signinRow * kRowStep + kRowY);
    signInHighlightRow(g, 6, y, 0x6E, static_cast<uint8_t>(y + 9), 0xFFFF, 0xAAAA);
    g.core.dirty = 1;
}

// 47c7:0831 SignIn_SetupZones
void signInSetupZones(DosGame& g) {
    UiState& ui = g.ui;
    InputZoneTable& zones = g.inputTables.zones(kCtxSignIn);
    auto setZone = [&zones](size_t i, const uint8_t* b) {
        if (i >= zones.records.size()) return;
        InputZone& z = zones.records[i];
        z.x1 = b[0], z.y1 = b[1], z.x2 = b[2], z.y2 = b[3];
        z.hover = static_cast<int8_t>(b[4]), z.press = static_cast<int8_t>(b[5]);
        z.held = static_cast<int8_t>(b[6]), z.release = static_cast<int8_t>(b[7]);
    };
    // Templates DS:622D -> zone 0, DS:6235 -> zones 8..11 (their y ranges put rows 1 and 2 12 px
    // apart while the rows are drawn 11 px apart: the original's shifted click zones, kept).
    setZone(0, g.tables.ui.signinZone0Template.data());
    for (size_t k = 0; k < 4; ++k) setZone(8 + k, g.tables.ui.signinZone8Template.data() + 8 * k);
    const int last = ui.signinCount - (ui.signinState == kStateRemove ? 1 : 0);
    zones.count = static_cast<uint8_t>(last + 10);
    for (int row = 0; row <= last; ++row)  // x1 = 6 enables the row (press / held 9 + row, release 8)
        if (static_cast<size_t>(9 + row) < zones.records.size()) zones.records[static_cast<size_t>(9 + row)].x1 = 6;
    g.input.context = kCtxSignIn;
    g.input.cmd = -1;
    g.input.queuedCmd = -1;
    g.input.deliverNext = true;
}

// 47c7:1011 SignInCmd_Select
void signInCmdSelect(DosGame& g) {
    UiState& ui = g.ui;
    const int cmd = ui.uiCmd;
    if (cmd == kCmdEnter) {
        if (ui.signinRow == ui.signinCount) {  // "New Player"
            if (ui.signinCount < kRows) {
                ui.signinChar = 0;
                signInDrawNameEntry(g);
            } else {
                openListFullBox(g);
            }
        } else {
            usersSelectRecord(g);
            if (g.rec().savedGame == 0) {
                ui.signinStart = 1;
            } else {  // "continue the saved game?"
                ui.signinState = kStateSavedBox;
                msgBoxDraw(g, kBoxSavedGame);
                g.core.dirty = 1;
                ui.dialogId = kDlgSavedGame;
            }
        }
    } else if (cmd == kCmdEsc) {
        ui.signinState = kStateExitBox;
        msgBoxDraw(g, kBoxExit);
        g.core.dirty = 1;
        ui.dialogId = kDlgExit;
    } else if (cmd == kCmdUp || cmd == kCmdDown) {
        signInMoveHighlight(g);
        ui.signinRow = static_cast<int8_t>(ui.signinRow + (cmd == kCmdUp ? -1 : 1));
        if (ui.signinRow < 0) ui.signinRow = ui.signinCount;  // "New Player" is part of the wrap
        else if (ui.signinCount < ui.signinRow) ui.signinRow = 0;
        signInMoveHighlight(g);
    } else if (cmd < kFirstRowCmd || cmd > kLastRowCmd) {
        const uint8_t ch = ui.signinChar;
        if (ch < kCharSpace || ch > kCharLast) {
            if (ch == kCharCtrlR) {  // remove mode
                ui.signinState = kStateRemove;
                uiForceRedraw(g);
                ui.hudRedraw = 1;
            }
        } else if (ui.signinCount < kRows && ch != kCharSpace) {  // a letter or digit starts a new name
            signInMoveHighlight(g);
            ui.signinRow = ui.signinCount;
            signInMoveHighlight(g);
            ui.newName[static_cast<size_t>(ui.newNameLen) % ui.newName.size()] = static_cast<char>(ch);
            ++ui.newNameLen;
            signInDrawNameEntry(g);
        } else if (ch != kCharSpace) {
            openListFullBox(g);
        }
    } else {
        highlightRow(g, ui.uiCmd - kFirstRowCmd);
    }
}

// 47c7:1199 SignInCmd_NewName
void signInCmdNewName(DosGame& g) {
    UiState& ui = g.ui;
    const int cmd = ui.uiCmd;
    const uint8_t ch = ui.signinChar;
    if (cmd == kCmdEnter) {
        if (ui.newNameLen != 0) {  // an empty name is ignored
            uiForceRedraw(g);
            ui.hudRedraw = 1;
            if (usersNameExists(g) == 0) {
                ui.signinStart = 1;
                usersAddPlayer(g);
            } else {
                signInDraw(g);
                ui.signinState = kStateOkBox;  // a byte write at DS:BA07 (the word at DS:BA06)
                ui.dialogId = kDlgNameExists;
                msgBoxDraw(g, kBoxNameExists);
            }
        }
    } else if (cmd == kCmdEsc) {
        if (ui.signinAny == 0) {  // nobody to go back to: the exit question
            ui.newName.fill(0);
            ui.newNameLen = 0;
            uiForceRedraw(g);
            signInDrawPanel(g);
            ui.signinState = kStateExitBox;
            msgBoxDraw(g, kBoxExit);
            g.core.dirty = 1;
            ui.dialogId = kDlgExit;
        } else {
            ui.signinState = kStateList;
            ui.signinInit = 1;  // back to the list
        }
    } else if (ch < kCharSpace || ch > kCharLast) {
        if (ch == kCharBackspace && ui.newNameLen != 0) {
            // The deleted glyph is only covered by the cursor block that moves back over it (its
            // first pixel column and its top row stay: the original's remnant, kept).
            ui.caretOn = 0;
            signInDrawField(g, false);
            --ui.newNameLen;
            ui.newName[static_cast<size_t>(ui.newNameLen) % ui.newName.size()] = 0;
            ui.caretOn = 1;
            signInDrawField(g, true);
        } else if (ui.caretArmed == 0 || g.counters().cycle13 > 6) {
            // The caret blinks on the 13-tick counter DS:90FE: one toggle per cycle (26 ticks).
            if (g.counters().cycle13 > 6) ui.caretArmed = 1;
        } else {
            ui.caretOn ^= 1;
            signInDrawField(g, false);
            ui.caretArmed = 0;
        }
    } else if (ui.newNameLen < kMaxNameLength && (ui.newNameLen != 0 || ch != kCharSpace)) {
        const auto w = static_cast<uint8_t>(textWidth(g, newNameView(g)));
        ui.caretOn = 0;
        signInDrawField(g, false);
        signInDrawTypedChar(g, static_cast<uint8_t>(w + 0x35), 0x75, ch);
        ui.newName[static_cast<size_t>(ui.newNameLen) % ui.newName.size()] = static_cast<char>(ch);
        ++ui.newNameLen;
        ui.caretOn = 1;
        signInDrawField(g, true);
    }
}

// 47c7:1328 SignInCmd_Remove
void signInCmdRemove(DosGame& g) {
    UiState& ui = g.ui;
    const int cmd = ui.uiCmd;
    if (cmd == kCmdEnter) {
        ui.signinState = kStateRemoveBox;
        msgBoxDraw(g, kBoxRemove);  // default No
        g.core.dirty = 1;
        ui.dialogId = kDlgRemove;
    } else if (cmd == kCmdEsc) {
        ui.signinState = kStateList;
        uiForceRedraw(g);
        ui.hudRedraw = 1;
    } else if (cmd < kFirstRowCmd || cmd > kLastRowCmd) {
        if (cmd == kCmdUp || cmd == kCmdDown) {  // players only: no "New Player" row here
            signInMoveHighlight(g);
            ui.signinRow = static_cast<int8_t>(ui.signinRow + (cmd == kCmdUp ? -1 : 1));
            if (ui.signinRow < 0) ui.signinRow = static_cast<int8_t>(ui.signinCount - 1);
            else if (ui.signinCount - 1 < ui.signinRow) ui.signinRow = 0;
            signInMoveHighlight(g);
        }
    } else {
        highlightRow(g, cmd - kFirstRowCmd);
    }
}

// 47c7:13d7 Dialog_CloseRedraw
void dialogCloseRedraw(DosGame& g) { uiForceRedraw(g); }

// 47c7:13dd SignInDlg_ExitYes
void signInDlgExitYes(DosGame& g) {
    g.core.quit = g.ui.buttonSel;  // Yes: back to DOS after this frame
    signInDlgBack(g);
}

// 47c7:13e9 SignInDlg_Back
void signInDlgBack(DosGame& g) {
    g.ui.signinState &= 7;  // the box's state goes back to 1, 3 or 5
    uiForceRedraw(g);
    g.ui.hudRedraw = 1;
}

// 47c7:13fa SignInDlg_RemoveYes
void signInDlgRemoveYes(DosGame& g) {
    if (g.ui.buttonSel != 0) {
        usersClearRecord(g);
        g.ui.usersLoaded = 0;  // re-read the file
        g.ui.signinInit = 1;
    }
    signInDlgBack(g);
}

// 47c7:1414 SignInDlg_ContinueAnswer
void signInDlgContinueAnswer(DosGame& g) {
    // "No" drops the saved game silently (box 12, the warning, is never shown).
    if (g.ui.buttonSel == 0 && g.hasRec()) g.rec().savedGame = 0;
    g.ui.signinStart = 1;
    uiForceRedraw(g);
}

// 47c7:1436 SignInDlg_Reset
void signInDlgReset(DosGame& g) {
    g.ui.signinState = kStateList;
    g.ui.signinInit = 1;
}

// 47c7:1442 SignInDlg_ListFull
void signInDlgListFull(DosGame& g) {
    g.ui.signinState = kStateRemove;
    uiForceRedraw(g);
    g.ui.hudRedraw = 1;
}

// 47c7:1454 Dialog_HandleButtons
void dialogHandleButtons(DosGame& g) {
    UiState& ui = g.ui;
    const int cmd = ui.uiCmd;
    if (cmd == kCmdEnter) {
        // Button_Flash (72 PIT ticks) and then the confirm handler: uiFlashContinue (ui.h).
        buttonFlash(g);
    } else if (cmd == kCmdEsc) {
        callDialogCancel(g, ui.dialogId);
    } else if (cmd > 3 && cmd < 16) {
        if (cmd < 6) {
            ui.buttonSel ^= 1;  // Left / Right / Tab / Up / Down toggle
        } else if (cmd < 9 || cmd > 10) {
            if (cmd == 15) {  // N: No and Enter
                ui.buttonSel = 0;
                g.input.queuedCmd = kCmdEnter;
            } else if (cmd == 14) {  // Y: Yes and Enter
                ui.buttonSel = 1;
                g.input.queuedCmd = kCmdEnter;
            }
        } else {
            ui.buttonSel = static_cast<uint16_t>(cmd - 9);  // a click on a button
        }
        buttonDraw(g, 0);
        buttonDraw(g, 1);
        g.core.dirty = 1;
    }
}

// 47c7:1502 Dialog_HandleOk
void dialogHandleOk(DosGame& g) {
    if (g.ui.uiCmd == kCmdEnter) callDialogConfirm(g, g.ui.dialogId);
    else if (g.ui.uiCmd == kCmdEsc) callDialogCancel(g, g.ui.dialogId);
}

// 47c7:152b UI_ForceRedraw
void uiForceRedraw(DosGame& g) {
    if (g.ui.boxSaved == 0) return;  // fast CPU: nothing (DS:0102 stays 0)
    g.ui.hudRedraw = 1;
    screenDrawOverlay(g);
    presentDirty(g.back, g.screen);
    ++g.screenVersion;
}

// ---- USERS.DAT and the player record (§9.7-§9.9) ----------------------------------------------

// 47c7:0452 Users_EnsureLoaded
int usersEnsureLoaded(DosGame& g) {
    if (g.ui.usersLoaded == 0) {
        // The image is zeroed, then read. Without a preference folder the store has no file and
        // nothing is kept between sessions; the image in memory then stays the session's roster
        // (engine: the original would re-read the file it wrote).
        if (!g.users.path().empty()) {
            image(g) = UsersDat{};
            usersReadFile(g);
        }
        g.ui.usersLoaded = 1;
    }
    return image(g).anyPlayer() ? 1 : 0;
}

// 47c7:04a4 Users_NameExists
int usersNameExists(DosGame& g) {
    UiState& ui = g.ui;
    // Trailing blanks of the typed name are dropped from the buffer itself (indices 8 down to 0).
    for (int i = static_cast<int>(ui.newName.size()) - 1; i >= 0; --i) {
        char& c = ui.newName[static_cast<size_t>(i)];
        if (c != 0 && c != ' ') break;
        c = 0;
    }
    return image(g).findName(newNameView(g)) >= 0 ? 1 : 0;  // stricmp with every listed name
}

// 47c7:0514 Users_ReadFile
void usersReadFile(DosGame& g) {
    g.core.quit = 1;
    uiForceRedraw(g);
    ++g.world.loadedScene;
    g.users.load();  // the engine's dos_users.dat (a missing file reads as the zeroed image)
    --g.world.loadedScene;
    g.core.quit = 0;
}

// 47c7:055a Users_WriteFile
void usersWriteFile(DosGame& g, DosSaveMoment moment) {
    g.core.quit = 1;
    uiForceRedraw(g);
    ++g.world.loadedScene;
    g.users.write(moment);  // always the whole 3243-byte image
    reportUsersWrite(g);    // the reference machine's time for it (the frame timing)
    g.core.quit = 0;
    --g.world.loadedScene;
}

// 47c7:075d Users_ClearRecord
void usersClearRecord(DosGame& g) {
    recordOfRow(g, g.ui.signinRow).clear();  // 360 zero bytes, then the answer masks = 7
    usersWriteFile(g, DosSaveMoment::RemovePlayer);
}

// 47c7:07b9 Users_AddPlayer
void usersAddPlayer(DosGame& g) {
    UiState& ui = g.ui;
    const int free = image(g).firstFreeRecord();
    if (free < 0) return;
    ui.signinRow = ui.signinCount;
    ui.signinRecIdx[static_cast<size_t>(ui.signinCount) % kRows] = static_cast<uint8_t>(free);
    ++ui.signinCount;
    usersSelectRecord(g);
    usersInitNewPlayer(g);
    UserRecord& r = g.users.record(free);
    for (size_t i = 0; i < r.name.size(); ++i) r.name[i] = static_cast<uint8_t>(ui.newName[i]);  // movmem, 9 bytes
    usersWriteFile(g, DosSaveMoment::NewPlayer);
}

// 47c7:09dd Users_SelectRecord
void usersSelectRecord(DosGame& g) {
    // g_userRec and the field pointers DS:8FD2, 921A, 9107, 91FD, 9138, 9101, 927E, 9209, 8FB6 all
    // point into record DS:6132[row's record]; the engine keeps the record index.
    g.ui.userRec = static_cast<int16_t>(g.ui.signinRecIdx[static_cast<size_t>(g.ui.signinRow) % kRows] % kRows);
}

// 47c7:0a8b Users_InitNewPlayer
void usersInitNewPlayer(DosGame& g) {
    CoreState& core = g.core;
    UserRecord& r = g.rec();
    core.tier = 0;
    core.level = 0;
    r.bankedCoins = 5;
    core.coins = 5;
    r.bankedNets = 10;
    core.nets = 10;
    g.player.obj.x = 0x140;
    g.player.obj.y = 0x70;
    g.world.cameraX = 0xC0;
    g.world.cameraY = 0;
    core.gamesPlayed = 0;
    g.player.playerState = 3;
    r.collected.fill(0x80);
    r.shelfBits.fill(0);
    g.player.shownTreasure = -1;
    frameBegin(g);
    usersPack(g);  // fields not set here come from the globals (record +9 = 0 starts a new game anyway)
}

// 47c7:0b19 Users_LoadPlayer
void usersLoadPlayer(DosGame& g) {
    if (!g.hasRec()) return;
    CoreState& core = g.core;
    const UserRecord& r = g.rec();
    core.tier = r.tier;
    core.level = 0;
    core.coins = r.bankedCoins;  // the banked values of the last Game_End
    core.nets = r.bankedNets;
    g.player.shownTreasure = r.shownTreasure;
    g.player.storedShowcase = static_cast<int8_t>(r.shownTreasure);
    core.gamesPlayed = r.gamesPlayed;
    if (core.musicOn != 0) {  // once the music is off, later players' settings are not loaded
        setSoundWords(g, r.musicOn, r.soundOn);
    }
    g.player.obj.x = 0x140;
    g.player.obj.y = 0x70;
    g.world.cameraX = 0xC0;
    g.world.cameraY = 0;
    g.player.playerState = 3;
    core.area = 0;
    g.world.loadedScene = 0xFF;
    g.elves.hintsOn = r.totalTreasures < 5 ? 1 : 0;
    g.elves.hintFlags.fill(1);
    frameBegin(g);
}

// 47c7:0e0e Users_Pack
void usersPack(DosGame& g) {
    if (!g.hasRec()) return;
    UserRecord& r = g.rec();
    const WorldTables& wt = g.tables.world;
    const WorldState& w = g.world;
    const CoreState& core = g.core;
    for (size_t i = 0; i < wt.hidingCells.size(); ++i) {  // low bytes of the hiding places' grid words
        const uint16_t cell = wt.hidingCells[i];
        if (!cellOk(cell)) continue;
        r.hidingPictures[i] = static_cast<uint8_t>(w.cellPicture[cell]);
        r.hidingContents[i] = static_cast<uint8_t>(w.cellContent[cell]);
    }
    for (size_t j = 0; j < wt.variableCells.size(); ++j)
        if (cellOk(wt.variableCells[j])) r.variablePictures[j] = static_cast<uint8_t>(w.cellPicture[wt.variableCells[j]]);
    for (int k = 5; k >= 0; --k)  // g_levelPicData words {1, 2, 5, 6, 9, 10} (DS:62B1)
        if (const uint16_t* p = levelPicWord(g, static_cast<int8_t>(g.tables.ui.recPicDataPack[static_cast<size_t>(k)])))
            r.levelPicWords[static_cast<size_t>(k)] = *p;
    r.tier = core.tier;
    r.level = core.level;
    r.coins = core.coins;
    r.nets = core.nets;
    r.treasuresFound = core.treasuresFound;
    r.keyState = core.keyState;
    r.promptIcon = g.ui.promptIcon;
    r.playerX = static_cast<int16_t>(g.player.obj.x);
    r.playerY = static_cast<int16_t>(g.player.obj.y);
    r.cameraX = static_cast<int16_t>(w.cameraX);
    r.cameraY = static_cast<int16_t>(w.cameraY);
    r.area = core.area;
    r.shopCoinsOwed = core.shopCoinsOwed;
    r.musicOn = core.musicOn;
    r.soundOn = core.soundOn;
    r.gamesPlayed = core.gamesPlayed;
    r.shownTreasure = g.player.shownTreasure;
    r.castleLayout = static_cast<int8_t>(w.castleLayout);  // sign-extended
    image(g).spriteHighWater = g.sprites.saveHeapHigh;     // header bytes 1-2 (47c7:0f80)
}

// 47c7:0c62 Game_Restore
void gameRestore(DosGame& g) {
    if (!g.hasRec()) return;
    const UserRecord& r = g.rec();
    const WorldTables& wt = g.tables.world;
    WorldState& w = g.world;
    CoreState& core = g.core;
    for (size_t i = 0; i < wt.hidingCells.size(); ++i) {  // bytes zero-extended into the grid words
        const uint16_t cell = wt.hidingCells[i];
        if (!cellOk(cell)) continue;
        w.cellPicture[cell] = r.hidingPictures[i];
        w.cellContent[cell] = r.hidingContents[i];
    }
    for (size_t j = 0; j < wt.variableCells.size(); ++j)
        if (cellOk(wt.variableCells[j])) w.cellPicture[wt.variableCells[j]] = r.variablePictures[j];
    for (int k = 5; k >= 0; --k)  // DS:62AB
        if (uint16_t* p = levelPicWord(g, static_cast<int8_t>(g.tables.ui.recPicDataRestore[static_cast<size_t>(k)])))
            *p = r.levelPicWords[static_cast<size_t>(k)];
    layoutMirrorSharedCells(g);
    core.tier = r.tier;
    core.level = r.level;
    core.coins = r.coins;
    core.nets = r.nets;
    core.treasuresFound = r.treasuresFound;
    core.keyState = r.keyState;
    g.ui.promptIcon = r.promptIcon;
    g.player.obj.x = static_cast<uint16_t>(r.playerX);
    g.player.obj.y = static_cast<uint16_t>(r.playerY);
    w.cameraX = static_cast<uint16_t>(r.cameraX);
    w.cameraY = static_cast<uint16_t>(r.cameraY);
    core.area = r.area;
    core.shopCoinsOwed = r.shopCoinsOwed;
    setSoundWords(g, r.musicOn, r.soundOn);  // unconditionally (unlike Users_LoadPlayer)
    core.gamesPlayed = r.gamesPlayed;
    g.player.shownTreasure = r.shownTreasure;
    w.castleLayout = static_cast<uint8_t>(r.castleLayout);  // low byte only
    g.sprites.saveHeapHigh = image(g).spriteHighWater;      // 47c7:0de0
    w.slowCpuSaved = core.slowCpu;
    if (core.area == kAreaCastle) core.slowCpu = 0;  // the castle always runs the fast-CPU paths
    w.slowScroll = core.slowCpu;
    castleApplyLayout(g);
    frameBegin(g);
}

// 47c7:0bd6 Game_NewSetup
void gameNewSetup(DosGame& g) {
    CoreState& core = g.core;
    core.treasuresFound = 0;
    core.keyState = 8;
    g.ui.promptIcon = 4;
    core.shopCoinsOwed = 4;
    if (g.hasRec()) {
        UserRecord& r = g.rec();
        r.quizSlots.fill(0xFFFF);
        r.answerMasks.fill(7);
        r.clueWords.fill(0);
        r.foundMasks.fill(0);
    }
    layoutClearSlots(g);
    cellsResetVariable(g);
    layoutGenerate(g);
    g.world.slowCpuSaved = core.slowCpu;
}

// 47c7:08b5 Game_Start
int gameStart(DosGame& g) {
    UiState& ui = g.ui;
    CoreState& core = g.core;
    const uint8_t saved = g.hasRec() ? g.rec().savedGame : 0;
    if (saved == 0) {
        gameNewSetup(g);
        core.area = kAreaNewGame;  // the clubhouse
    } else {
        gameRestore(g);
    }
    cellsAdjustForCpu(g);
    musicStop(g);
    sfxStop(g);
    core.quit = 1;
    const int loaded = quizLoadQuestions(g, saved != 0);
    reportQuizLoad(g);  // the reference machine's time for it (the frame timing)
    core.quit = 0;
    core.uiMode &= static_cast<uint8_t>(~(kModeSignIn | kModeQuiz));
    ui.signinInit = 1;
    ui.usersLoaded = 0;
    ui.screen = 0;
    g.input.context = kCtxPlay;
    g.elves.hintTimerReset = 1;
    ui.quizClosed = 1;
    g.elves.messagePending = 1;  // the start-of-game message
    core.gameInProgress = 0;     // until that message is closed
    ui.coinsShown = core.coins;  // no count-up at the start
    musicPlay(g, tuneForArea(core.area));
    ui.soundChanged = 1;  // Sound_SyncState starts the tune once more (as the original)
    ui.hintsBoxDefault = g.elves.hintsOn;
    ui.musicBoxDefault = core.musicOn;
    ui.soundBoxDefault = core.soundOn;
    return loaded != 0 ? 1 : 0;
}

// 47c7:09a2 Game_End
void gameEnd(DosGame& g) {
    CoreState& core = g.core;
    ++core.gamesPlayed;
    g.elves.lastGameTreasures = core.treasuresFound;
    g.elves.lastGameTier = core.tier;
    if (g.hasRec()) {
        UserRecord& r = g.rec();
        r.savedGame = 0;
        r.bankedNets = core.nets;  // banked for the next game
        r.bankedCoins = core.coins;
    }
    usersPack(g);
    usersWriteFile(g, DosSaveMoment::GameEnd);
}

}  // namespace tmport::dos
