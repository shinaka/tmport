// ui.c (segment 43cb) of the 1990 DOS edition: the menu bar and its drop-downs, message boxes and
// Yes/No buttons, the dialog dispatch with the confirm / cancel handlers, the menu commands, the
// help pager, the sound hot key, and the text and line primitives with the DGROUP ink (DS:9215)
// and line height (DS:5B3C). The drawing itself is wave 2's (dos/text/ui_draw.h, text_draw.h);
// this file owns the state machine, the box table logic and the input zones.
// Spec: re/dos/docs/code_ui.md §2-§6; runtime model of Button_Flash: ENGINE_MAP.md §3.4.
#include "dos/game/ui.h"

#include <algorithm>

#include "dos/data/dgroup_offsets.h"
#include "dos/game/anim.h"
#include "dos/game/game.h"
#include "dos/game/hud.h"
#include "dos/game/scene.h"
#include "dos/game/signin.h"
#include "dos/game/sound.h"
#include "dos/gfx/blitter.h"
#include "dos/gfx/pak.h"
#include "dos/text/text_draw.h"
#include "dos/text/ui_draw.h"

namespace tmport::dos {

namespace {

constexpr uint8_t kModeTitle = 0x80, kModeMenu = 0x40, kModeDialog = 0x20, kModeSignIn = 0x08, kModeQuiz = 0x02;
constexpr int kCtxPlay = 0, kCtxMenu = 1, kCtxDialog = 2, kCtxSignIn = 4;
constexpr int kCmdEsc = 0, kCmdEnter = 8;
constexpr int kBoxDropDown = 0, kBoxYesNo = 1, kBoxHelp = 2, kBoxEnterLine = 3, kBoxDisk = 4, kBoxName = 6;
constexpr uint8_t kZoneDisabled = 200;   // x1 = 200: the mouse (x <= 159) never hits the zone
constexpr int kFlashToggles = 4;         // Button_Flash 43cb:0eb6: 4 x Delay_Ticks(0x12)
constexpr uint32_t kFlashTicks = 0x12;
constexpr int kFirstSignInDialog = 14;   // dialogs 14..18 are opened by the sign-in (code_ui.md §9.6)
constexpr uint16_t kAreaCastle = 0x0D;

bool slowCpu(const DosGame& g) { return g.core.slowCpu == 1; }

// Signed byte of the EXE's DGROUP image (tables read past their end, as the original indexes them).
int8_t dsI8(const DosGame& g, int off) {
    if (off < 0 || static_cast<size_t>(off) >= g.tables.dgroup.size()) return 0;
    return static_cast<int8_t>(g.tables.dgroup[static_cast<size_t>(off)]);
}
int16_t dsI16(const DosGame& g, int off) {
    if (off < 0 || static_cast<size_t>(off) + 1 >= g.tables.dgroup.size()) return 0;
    return static_cast<int16_t>(g.tables.dgroup[static_cast<size_t>(off)] |
                                (g.tables.dgroup[static_cast<size_t>(off) + 1] << 8));
}

TextStyle inkStyle(const DosGame& g) { return TextStyle{g.ui.textInk, static_cast<int16_t>(g.ui.lineHeight)}; }

// A far char* of the box and help tables (stored as a DS offset, 0 = NULL).
std::optional<std::string_view> farString(const DosGame& g, uint16_t off) {
    if (off == 0) return std::nullopt;
    return g.dsString(off);
}

// The default-button byte of box n: the record's, except boxes 6..8 whose bytes the option
// commands and Game_Start rewrite (UiState keeps those words, code_ui.md §4.1).
uint8_t boxDefaultButton(const DosGame& g, int n) {
    switch (n) {
        case 6: return static_cast<uint8_t>(g.ui.hintsBoxDefault);
        case 7: return static_cast<uint8_t>(g.ui.musicBoxDefault);
        case 8: return static_cast<uint8_t>(g.ui.soundBoxDefault);
        default: return g.tables.ui.msgBoxes[static_cast<size_t>(n)].defaultButton;
    }
}

// The key list of the dialog context (2) with `count` keys. Box_SetupInput only rewrites entries
// 2, 3 and 5..9 and the count; entries past a smaller count keep their bytes in the original, and
// nothing ever rewrites entries 10 and 11, so entries that come back are read from the EXE.
InputKeyMap dialogKeys(const DosGame& g, size_t count) {
    InputKeyMap m = g.inputTables.keyMap(kCtxDialog);
    const uint16_t keys = g.inputTables.keysAddress(kCtxDialog);
    const uint16_t cmds = g.exe().readFarPtr(static_cast<uint16_t>(dg::kCmdLists + 4 * kCtxDialog)).off;
    const size_t old = m.keys.size();
    m.keys.resize(count + 1, 0);
    m.commands.resize(count + 1, -1);
    for (size_t i = std::max<size_t>(old, 1); i <= count && keys != 0; ++i) {
        m.keys[i] = g.exe().ds16(static_cast<uint16_t>(keys + 2 * i));
        m.commands[i] = g.exe().dsS16(static_cast<uint16_t>(cmds + 2 * i));
    }
    return m;
}

// Menu_Update's tail (43cb:0109..0148) after Dialog_Update: the command is consumed, and the HUD
// prompt shows the question prompt (2) or nothing while the menu bar is open (0).
void menuUpdateTail(DosGame& g) {
    g.input.cmd = g.ui.uiCmd;
    g.input.deliverNext = true;
    if (g.core.uiMode & kModeQuiz) g.ui.promptIcon = 2;
    else if (g.core.uiMode & kModeMenu) g.ui.promptIcon = 0;
}

// One blink of Button_Flash: the selection toggles and the flashing button is redrawn and shown.
void flashToggle(DosGame& g) {
    g.ui.buttonSel ^= 1;
    buttonDraw(g, g.ui.flashButton);
    presentDirty(g.back, g.screen);
    ++g.screenVersion;
    ++g.ui.flashStep;
}

}  // namespace

// Sprite_GetHeader 1000:2159 + Sprite_Draw 1000:1b8d of a frame code of the loaded bank.
void drawBankFrame(DosGame& g, uint16_t frameCode, int x, int y, uint8_t flags) {
    if (const PakFrame* f = g.bankFrame(frameCode)) drawSprite(g.back, *f, x, y, flags);
}

// ---- menu bar, dialogs, commands (§5) --------------------------------------------------------

// 43cb:0001 Menu_Update
void menuUpdate(DosGame& g) {
    if (uiFlashPending(g)) {  // the original is still inside Button_Flash (Dialog_HandleButtons)
        if (g.ui.dialogId < kFirstSignInDialog) uiFlashContinue(g);
        return;
    }
    UiState& ui = g.ui;
    CoreState& core = g.core;
    const int16_t old = ui.menuOpen;
    if ((core.uiMode & kModeTitle) || !(core.uiMode & (kModeMenu | kModeDialog)) || core.menuLock != 0) return;
    ui.uiCmd = g.input.cmd;  // read with interrupts off
    if (ui.uiCmd == -1) return;
    if (core.uiMode & kModeMenu) {
        const int cmd = ui.uiCmd;
        if (cmd == kCmdEnter || cmd == kCmdEsc) {
            if (cmd == kCmdEnter) {
                ui.dialogId = static_cast<uint16_t>(dsI16(g, dg::kMenuFirstDialog + 2 * ui.menuOpen) + ui.menuItem);
                core.uiMode |= kModeDialog;
                ui.hudRedraw = 2;
            } else {
                g.input.context = kCtxPlay;
                ui.hudRedraw = 1;
            }
            screenDrawOverlay(g);  // the full redraw removes the drop-down
            core.uiMode &= static_cast<uint8_t>(~kModeMenu);
            g.input.queuedCmd = -1;
            g.keys() &= 0xFFF0;
        } else {
            // F1..F3 pick a menu; Left/Right step with the wrap table (code_ui.md §5.2).
            const int t0 = cmd < 4 ? cmd - 1 : dsI8(g, dg::kMenuStepH + cmd) + ui.menuOpen;
            const int t = menuWrapAt(g.tables, t0);
            if (ui.menuOpen != t) {
                if (old != -1 || !slowCpu(g)) {  // slow CPU: the first opening skips the redraw
                    ui.hudRedraw = 2;
                    ui.menuOpen = static_cast<int16_t>(t);
                    screenDrawOverlay(g);
                    if (old != -1) menuBarDrawTitle(g, old, false);
                } else {
                    ui.menuOpen = static_cast<int16_t>(t);
                }
                menuBarDrawTitle(g, ui.menuOpen, true);
                msgBoxDraw(g, ui.menuOpen);
                ui.menuItem = 0;
                menuHighlightItem(g);
            }
            ui.menuOpen = static_cast<int16_t>(t);
            menuMoveSelection(g);
        }
        ui.uiCmd = -1;
    }
    dialogUpdate(g);
    if (uiFlashPending(g)) return;  // Enter on a box: the rest of this call runs when the flash ends
    menuUpdateTail(g);
}

// 43cb:014a Menu_MoveSelection
void menuMoveSelection(DosGame& g) {
    UiState& ui = g.ui;
    const int cmd = ui.uiCmd;
    int n = cmd < 9 ? dsI8(g, dg::kMenuStepV + cmd) + ui.menuItem : cmd - 9;  // Up -1, Down +1; mouse row
    if (n == -1) n = ui.boxLines - 1;
    else if (n == ui.boxLines) n = 0;
    if (n != ui.menuItem) {
        menuHighlightItem(g);
        ui.menuItem = static_cast<uint16_t>(n);
        menuHighlightItem(g);
    }
    g.core.dirty = 1;
}

// 43cb:019a Dialog_Update
void dialogUpdate(DosGame& g) {
    UiState& ui = g.ui;
    CoreState& core = g.core;
    if (!(core.uiMode & kModeDialog)) return;
    const int16_t box = dsI16(g, dg::kDialogBox + 2 * ui.dialogId);  // -1: a help topic
    if (ui.dialogShown == 0) {
        if (box == -1) helpOpen(g, ui.dialogId);
        else msgBoxDraw(g, box);
        ui.dialogShown = 1;
        core.dirty = 1;
    } else {
        if (ui.uiCmd == kCmdEnter || ui.uiCmd == kCmdEsc) {
            ui.dialogShown = 0;
            g.input.context = kCtxPlay;
            if (box != -1 || ui.uiCmd == kCmdEsc) {  // help stays open on Enter (the page turns)
                core.uiMode &= static_cast<uint8_t>(~kModeDialog);
                ui.hudRedraw = 1;
            }
            g.keys() &= 0xFFF0;
        }
        dialogHandleButtons(g);
        if (uiFlashPending(g)) return;
    }
    ui.uiCmd = -1;
}

// DS:62B7 g_dialogYesHandlers: the confirm handler of a dialog id (a switch instead of the far
// pointer table; dialog ids past 18 have no handler).
void callDialogConfirm(DosGame& g, int dialogId) {
    switch (dialogId) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: menuCmdHelpPage(g); break;
        case 8: menuCmdStartOver(g); break;
        case 9: menuCmdStartNewPlayer(g); break;
        case 10: menuCmdExitAndSave(g); break;
        case 11: menuCmdNewPlayerHints(g); break;
        case 12: menuCmdMusic(g); break;
        case 13: menuCmdSound(g); break;
        case 14: signInDlgExitYes(g); break;
        case 15: signInDlgRemoveYes(g); break;
        case 16: signInDlgContinueAnswer(g); break;
        case 17: signInDlgReset(g); break;
        case 18: signInDlgListFull(g); break;
        default: break;
    }
}

// DS:6303 g_dialogNoHandlers: the cancel handler of a dialog id.
void callDialogCancel(DosGame& g, int dialogId) {
    if (dialogId >= 0 && dialogId < kFirstSignInDialog) dialogCloseRedraw(g);
    else if (dialogId == 17) signInDlgReset(g);
    else if (dialogId >= kFirstSignInDialog && dialogId <= 18) signInDlgBack(g);
}

// 43cb:021d MenuCmd_HelpPage
void menuCmdHelpPage(DosGame& g) {
    UiState& ui = g.ui;
    ui.helpPage = static_cast<int8_t>(ui.helpPage + dsI8(g, dg::kHelpPageStep + (ui.buttonSel & 1)));
    if (ui.helpPage == ui.helpPageCount) {  // signed char against the count byte
        // Past the last page: help closes and the Help drop-down reopens with item 0 (queued F1).
        g.input.context = kCtxMenu;
        g.core.uiMode = static_cast<uint8_t>((g.core.uiMode & ~kModeDialog) | kModeMenu);
        g.input.queuedCmd = 1;
        ui.menuOpen = -1;
        if (slowCpu(g)) ui.hudRedraw = 1;
    } else {
        if (ui.helpPage == 0) ui.buttonSel = 1;
        ui.dialogShown = 1;
        helpDrawPage(g, ui.dialogId, ui.helpPage);
        g.core.dirty = 1;
    }
}

// 43cb:028b MenuCmd_StartOver
void menuCmdStartOver(DosGame& g) {
    if (g.ui.buttonSel == 0) return;
    if (g.core.area == kAreaCastle) {  // the castle forces the fast-CPU paths; restore the switch
        g.core.slowCpu = g.world.slowCpuSaved;
        g.world.slowScroll = g.world.slowCpuSaved;
    }
    uiForceRedraw(g);
    g.ui.hudRedraw = 1;
    if (g.hasRec()) g.rec().savedGame = 0;  // the abandoned game is dropped
    usersPack(g);
    usersWriteFile(g, DosSaveMoment::StartOver);
    usersLoadPlayer(g);  // banked coins and nets: the abandoned game's are lost
    gameStart(g);
    sceneUpdate(g);
    animInitSlots(g);
    g.world.inShelfRoom.fill(0);
    g.world.frameInShelfRoom.fill(0);
}

// 43cb:02f8 MenuCmd_StartNewPlayer
void menuCmdStartNewPlayer(DosGame& g) {
    if (g.ui.buttonSel == 0) return;
    if (g.core.area == kAreaCastle) g.core.slowCpu = g.world.slowCpuSaved;
    if (g.hasRec()) g.rec().savedGame = static_cast<uint8_t>(g.core.gameInProgress);
    usersPack(g);
    usersWriteFile(g, DosSaveMoment::StartNewPlayer);
    g.core.uiMode = kModeSignIn;
    animInitSlots(g);
}

// 43cb:032c MenuCmd_ExitAndSave
void menuCmdExitAndSave(DosGame& g) {
    if (g.ui.buttonSel != 0) {
        if (g.hasRec()) g.rec().savedGame = static_cast<uint8_t>(g.core.gameInProgress);
        usersPack(g);
        usersWriteFile(g, DosSaveMoment::ExitAndSave);
    }
    g.core.quit = g.ui.buttonSel;  // Yes: the main loop ends after this frame
}

// 43cb:034f MenuCmd_NewPlayerHints
void menuCmdNewPlayerHints(DosGame& g) {
    const uint16_t sel = g.ui.buttonSel;
    g.elves.hintTimerReset = sel;
    g.elves.hintsOn = sel;
    g.ui.hintsBoxDefault = sel;
    g.elves.hintFlags.fill(1);  // re-armed, also for "No"
}

// 43cb:0371 MenuCmd_Music
void menuCmdMusic(DosGame& g) {
    const uint16_t old = g.core.musicOn, sel = g.ui.buttonSel;
    setMusicOn(g, sel != 0);
    g.ui.musicBoxDefault = sel;
    if (sel != 0) {
        setSoundOn(g, true);
        g.ui.soundBoxDefault = sel;
    }
    if (old != sel) g.ui.soundChanged = 1;
}

// 43cb:039d MenuCmd_Sound
void menuCmdSound(DosGame& g) {
    const uint16_t oldMusic = g.core.musicOn, oldSound = g.core.soundOn, sel = g.ui.buttonSel;
    setMusicOn(g, sel != 0);
    setSoundOn(g, sel != 0);
    g.ui.musicBoxDefault = sel;
    g.ui.soundBoxDefault = sel;
    if (oldSound != sel || oldMusic != sel) g.ui.soundChanged = 1;
}

// 43cb:03cb Game_HandleHotkeys
void gameHandleHotkeys(DosGame& g) {
    if (uiFlashPending(g)) return;
    CoreState& core = g.core;
    if (core.uiMode != 0 && core.area != 6) return;
    if (g.input.cmd == 14) {  // Ctrl+S: music and effects on / off together
        const auto v = static_cast<uint16_t>(core.musicOn ^ 1);
        g.ui.musicBoxDefault = v;
        g.ui.soundBoxDefault = v;
        setSoundOn(g, v != 0);
        setMusicOn(g, v != 0);
        if (v == 0) {
            musicStop(g);
            sfxStop(g);
            soundSilenceAll(g);
        } else {
            musicPlay(g, tuneForArea(core.area));
        }
        g.input.cmd = -1;
        g.input.deliverNext = true;
    } else if (g.input.cmd == 15 && core.slowCpu != 2) {
        core.slowCpu ^= 1;  // no key produces command 15 in contexts 0 and 5: dead code
    }
}

// ---- boxes and buttons (§4) ------------------------------------------------------------------

// 43cb:0592 MsgBox_Draw
void msgBoxDraw(DosGame& g, int n) {
    if (n < 0 || n >= static_cast<int>(g.tables.ui.msgBoxes.size())) return;
    UiState& ui = g.ui;
    ui.textInk = 0xFFFF;
    const UiTables::MsgBox& rec = g.tables.ui.msgBoxes[static_cast<size_t>(n)];
    const auto title = farString(g, rec.title);
    const std::string_view text = farString(g, rec.text).value_or(std::string_view{});
    // Width from the title and the widest line (every byte's advance), height from the line count
    // (lines end at CR, LF or the final NUL) and the type (wave 2's msgBoxGeometry, verified).
    const BoxGeometry geo = msgBoxGeometry(g.font(), rec.type, rec.x1, rec.y1, title, text);
    ui.boxWidth = static_cast<uint16_t>(geo.width);
    if (slowCpu(g)) {
        // Sprite_SaveUnder 1000:2056 of the box (from 2 rows above it); Screen_DrawOverlay puts it
        // back instead of a full redraw. (The save-heap words DS:37C8/37CC it also sets are not
        // modelled: the background is kept in DosGame::boxSave.)
        g.boxSave = saveUnder(g.back, geo.x1, geo.y1 - 2, geo.width + 1, geo.height + 3);
        ui.boxSaved = 1;
    }
    boxDrawFrame(g, rec.type, geo.x1, geo.y1, geo.x2, geo.y2, title);
    if (rec.type == kBoxYesNo) {
        ui.buttonSel = boxDefaultButton(g, n);
        ui.buttonX[1] = static_cast<uint16_t>(geo.x2 - 0x3A);
        ui.buttonX[0] = static_cast<uint16_t>(geo.x2 - 0x1C);
        ui.buttonY[1] = static_cast<uint16_t>(geo.y2 - 0x23);
        ui.buttonY[0] = static_cast<uint16_t>(geo.y2 - 0x23);
        ui.buttonLabel[0] = DsPtr{dg::kStrBtnNo};
        ui.buttonLabel[1] = DsPtr{dg::kStrBtnYes};
        buttonDraw(g, 0);
        buttonDraw(g, 1);
    }
    boxSetupInput(g, rec.type, static_cast<uint8_t>(geo.x1), static_cast<uint8_t>(geo.x2), geo.y2);
    ui.boxTextY = static_cast<uint16_t>(geo.textY);
    ui.boxLines = static_cast<uint16_t>(textDraw(g, geo.x1 + 7, geo.textY, text));  // = items of a drop-down
    if (slowCpu(g) && !(g.core.uiMode & kModeMenu)) {
        presentDirty(g.back, g.screen);
        ++g.screenVersion;
    }
}

// 43cb:0848 Box_DrawFrame
void boxDrawFrame(DosGame& g, int type, int x1, int y1, int x2, int y2, std::optional<std::string_view> title) {
    drawBoxFrame(g.back, g.font(), inkStyle(g), BoxTexts::fromExe(g.exe()), type, x1, y1, x2, y2, title);
}

// 43cb:12cf Box_SetupInput
void boxSetupInput(DosGame& g, int type, uint8_t x1, uint8_t x2, int y2) {
    const UiState& ui = g.ui;
    const int ctx = type == kBoxDropDown ? kCtxMenu : type == kBoxName ? kCtxSignIn : kCtxDialog;
    g.input.context = ctx;
    InputZoneTable& zones = g.inputTables.zones(ctx);
    auto zone = [&zones](int i) -> InputZone& { return zones.records.at(static_cast<size_t>(i)); };
    const auto bottom = static_cast<uint8_t>(y2);
    // Zone 0: the footer line, release = Esc (Enter for the disk boxes); types 2, 5, 7 disabled.
    InputZone& footer = zone(0);
    footer.x1 = static_cast<uint8_t>(dsI8(g, dg::kFooterZoneX + type) + x1);
    footer.y1 = static_cast<uint8_t>(bottom - 0x11);
    footer.x2 = static_cast<uint8_t>(x2 - 2);
    footer.y2 = static_cast<uint8_t>(bottom - 4);
    footer.release = static_cast<int8_t>(type == kBoxDisk ? kCmdEnter : kCmdEsc);
    if (type == kBoxDropDown) {
        zones.count = static_cast<uint8_t>(dsI8(g, dg::kMenuZoneCounts + ui.menuOpen));
        for (int i = 9; i <= 16; ++i) {  // the item rows (y ranges and commands are static)
            zone(i).x1 = x1;
            zone(i).x2 = x2;
        }
    } else if (type == kBoxYesNo || type == kBoxHelp) {
        zones.count = 11;
        zone(8).x1 = kZoneDisabled;
        for (int b = 0; b < 2; ++b) {  // zones 9 and 10: buttons 0 and 1
            InputZone& z = zone(9 + b);
            z.x1 = static_cast<uint8_t>(ui.buttonX[static_cast<size_t>(b)] - 2);
            z.y1 = static_cast<uint8_t>(ui.buttonY[static_cast<size_t>(b)] + 3);
            z.x2 = static_cast<uint8_t>(dsI8(g, dg::kButtonZoneWidths + type) + ui.buttonX[static_cast<size_t>(b)]);
            z.y2 = static_cast<uint8_t>(ui.buttonY[static_cast<size_t>(b)] + 0x0D);
        }
        // The key list: Tab, Up, Down toggle; Yes/No boxes also Left/Right and Y/N; help pages PgDn
        // (command 14) and PgUp (15), and on page 0 (both buttons at one x) only Esc, Enter, PgDn.
        InputKeyMap keys = dialogKeys(g, type == kBoxYesNo ? 11 : 9);
        keys.keys[5] = 0x0F09;
        keys.keys[6] = 0x4800;
        keys.keys[7] = 0x5000;
        if (type == kBoxYesNo) {
            keys.keys[2] = 0x4B00;
            keys.keys[3] = 0x4D00;
            keys.keys[8] = 0x1559;
            keys.keys[9] = 0x314E;
        } else {
            keys.keys[8] = 0x5100;
            if (ui.buttonX[0] == ui.buttonX[1]) {
                for (int i : {9, 3, 2, 7, 6, 5}) keys.keys[static_cast<size_t>(i)] = 0xFFFF;
            } else {
                keys.keys[9] = 0x4900;
                keys.keys[2] = 0x4B00;
                keys.keys[3] = 0x4D00;
            }
        }
        g.inputTables.setKeyMap(kCtxDialog, std::move(keys));
    } else if (type == kBoxEnterLine) {
        zones.count = 9;
        InputZone& z = zone(8);  // the "Press Enter" line (release 8 is static)
        z.x1 = static_cast<uint8_t>(x1 + 0x1E);
        z.y1 = static_cast<uint8_t>(bottom - 0x1F);
        z.x2 = static_cast<uint8_t>(x1 + 0x32);
        z.y2 = static_cast<uint8_t>(bottom - 0x16);
    } else if (type == kBoxName) {
        zones.count = 9;
        InputZone& z = zone(8);
        if (y2 < 0x82) {
            z.x1 = kZoneDisabled;
        } else {
            z.x1 = static_cast<uint8_t>(x1 + 0x40);
            z.y1 = static_cast<uint8_t>(bottom - 0x48);
            z.x2 = static_cast<uint8_t>(x1 + 0x54);
            z.y2 = static_cast<uint8_t>(bottom - 0x3F);
        }
    }
    g.input.cmd = -1;
    g.input.queuedCmd = -1;
}

// 43cb:0d50 Button_Draw
void buttonDraw(DosGame& g, int i) {
    if (i < 0 || i > 1) return;
    const UiState& ui = g.ui;
    const auto k = static_cast<size_t>(i);
    drawButton(g.back, g.font(), inkStyle(g), g.dsString(ui.buttonLabel[k].off), ui.buttonX[k], ui.buttonY[k],
               ui.buttonSel == i);
}

// 43cb:0eb6 Button_Flash: the start of the wait (the first toggle); uiFlashContinue does the rest.
void buttonFlash(DosGame& g) {
    UiState& ui = g.ui;
    ui.flashButton = static_cast<uint8_t>(ui.buttonSel & 1);
    ui.flashStart = g.ticks();
    ui.flashStep = 0;
    flashToggle(g);
}

bool uiFlashPending(const DosGame& g) { return g.ui.flashStep != 0; }

bool uiFlashContinue(DosGame& g) {
    UiState& ui = g.ui;
    if (ui.flashStep == 0) return true;
    // Delay_Ticks(0x12) after each toggle: toggles at start + 18k, the end at start + 72 (the
    // original's busy waits return exactly at those g_ticks values).
    const uint32_t elapsed = g.ticks() - ui.flashStart;
    while (ui.flashStep < kFlashToggles && elapsed >= kFlashTicks * ui.flashStep) flashToggle(g);
    if (ui.flashStep < kFlashToggles || elapsed < kFlashTicks * kFlashToggles) return false;
    ui.flashStep = 0;
    // Dialog_HandleButtons 47c7:1454 after Button_Flash: the confirm handler, then the callers.
    const int id = ui.dialogId;
    callDialogConfirm(g, id);
    if (id >= kFirstSignInDialog) {
        signInFlashDone(g);  // SignIn_HandleInput's game start, SignIn_Update's tail
    } else {
        ui.uiCmd = -1;  // Dialog_Update 43cb:0217
        menuUpdateTail(g);
    }
    return true;
}

// 43cb:0ee8 Delay_Ticks: only Button_Flash calls it; its wait is the flash state above.
void delayTicks(DosGame&, uint32_t) {}

// 43cb:0f1d MenuBar_DrawTitle
void menuBarDrawTitle(DosGame& g, int i, bool on) {
    if (i < 0 || i > 2) return;
    TextStyle s = inkStyle(g);
    drawMenuTitle(g.back, g.font(), s, g.exe(), i, on);
    g.ui.textInk = s.ink;  // 0xFFFF off, 0xCCCC on: DS:9215 keeps it
}

// 43cb:04ff Menu_HighlightItem
void menuHighlightItem(DosGame& g) {
    const UiState& ui = g.ui;
    if (ui.menuOpen < 0 || ui.menuOpen >= static_cast<int>(g.tables.ui.msgBoxes.size())) return;
    const int bx = g.tables.ui.msgBoxes[static_cast<size_t>(ui.menuOpen)].x1;
    const int x1 = bx + 2;
    const auto y1 = static_cast<int16_t>(ui.menuItem * 12 + ui.boxTextY - 1);
    const int x2 = bx + ui.boxWidth - 3;
    exchangeColors(g.back, x1, y1, x2, y1 + 12, 0xCCCC, 0xFFFF);  // white <-> black through colour 3
    if (slowCpu(g)) {
        presentDirty(g.back, g.screen);
        ++g.screenVersion;
    }
}

// ---- help pager (§6.1) -----------------------------------------------------------------------

// 43cb:107a Help_LoadFile: HELP.DAT is DosGame::help (read and checked at start-up).
int helpLoadFile(DosGame& g) {
    g.ui.helpLoaded = 1;
    return 1;
}

// 43cb:1273 Help_Open
void helpOpen(DosGame& g, int topic) {
    if (topic < 0 || topic >= HelpFile::kTopics) return;
    UiState& ui = g.ui;
    ui.helpPageCount = static_cast<uint8_t>(g.help.pageCount(topic));
    ui.helpPage = 0;
    ui.textInk = 0xFFFF;
    boxDrawFrame(g, kBoxHelp, 8, 0x13, 0x96, 0xBD, farString(g, g.tables.ui.helpTopicTitles[static_cast<size_t>(topic)]));
    ui.buttonSel = 1;
    helpDrawPage(g, topic, ui.helpPage);
}

// 43cb:10c4 Help_DrawPage
void helpDrawPage(DosGame& g, int topic, int page) {
    if (topic < 0 || topic >= HelpFile::kTopics) return;
    UiState& ui = g.ui;
    ui.textInk = 0xFFFF;
    gfxFillRectColor(g, 10, 0x23, 0x93, 0xA7, 0xCCCC);
    gfxFillRectColor(g, 10, 0xAA, 0x93, 0xB7, 0xCCCC);
    const int idx = g.help.pageIndex(topic, page);
    if (idx < 0 || idx >= g.help.totalPages()) return;
    ui.lineHeight = 10;
    textDraw(g, 0x0E, 0x25, g.help.pageText(idx));
    ui.lineHeight = 12;
    ui.buttonY[1] = ui.buttonY[0] = 0xA9;
    ui.buttonX[1] = 0x5C;
    ui.buttonLabel[1] = DsPtr{g.help.pageCount(topic) - 1 == page ? dg::kStrBtnDone : dg::kStrBtnNextPage};
    buttonDraw(g, 1);
    if (page == 0) {  // no "back" button: button 0 is button 1 (same x, so Box_SetupInput drops the arrows)
        ui.buttonX[0] = ui.buttonX[1];
        ui.buttonLabel[0] = ui.buttonLabel[1];
    } else {
        ui.buttonX[0] = 0x10;
        ui.buttonLabel[0] = DsPtr{dg::kStrBtnPrevPage};
        buttonDraw(g, 0);
    }
    boxSetupInput(g, kBoxHelp, 8, 0x96, 0xBD);
    // The page's pictures from the loaded sprite bank (DS:5BED..5C65, flags 0x40).
    if (idx < HelpFile::kMaxPages) {
        const HelpPagePictures pics = helpPagePictures(g.exe(), idx);
        ui.helpPicFrame1 = pics.frame1;
        ui.helpPicFrame2 = pics.frame2;
        ui.helpPicX1 = pics.x1;
        ui.helpPicX2 = pics.x2;
        ui.helpPicY = pics.y;
        if (pics.frame1 != -1) drawBankFrame(g, static_cast<uint16_t>(pics.frame1), pics.x1, pics.y, sprite_flags::kMasked);
        if (pics.frame2 != -1) drawBankFrame(g, static_cast<uint16_t>(pics.frame2), pics.x2, pics.y, sprite_flags::kMasked);
    }
}

// ---- drawing primitives with the DGROUP ink (§2.5, §3) ---------------------------------------

// 43cb:0b63 Text_Draw
int textDraw(DosGame& g, int x, int y, std::string_view s) { return textDraw(g.back, g.font(), inkStyle(g), x, y, s); }

// 43cb:0cfc Text_Width
int textWidth(DosGame& g, std::string_view s) { return textWidth(g.font(), s); }

// 43cb:0ab9 Gfx_HLine
void gfxHLine(DosGame& g, int x, int y, int w, int c) { hline(g.back, x, y, w, static_cast<uint8_t>(c)); }

// 43cb:0ada Gfx_VLine
void gfxVLine(DosGame& g, int x, int y, int h, int c) { vline(g.back, x, y, h, static_cast<uint8_t>(c)); }

// 43cb:0afb Gfx_FillRectColor
void gfxFillRectColor(DosGame& g, int x1, int y1, int x2, int y2, int c) {
    g.back.fillRect(x1, y1, x2, y2, static_cast<uint8_t>(c));
}

// 43cb:0b18 Gfx_FrameRect
void gfxFrameRect(DosGame& g, int x1, int y1, int x2, int y2, int c) {
    frameRect(g.back, x1, y1, x2, y2, static_cast<uint8_t>(c));
}

}  // namespace tmport::dos
