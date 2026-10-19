#pragma once
// ui.c (segment 43cb): the menu bar and drop-downs, message boxes and buttons, the dialog dispatch
// (confirm / cancel handlers by dialog id), the menu commands, the help pager, the sound hot key,
// and the game-side text and line primitives (wrappers over wave 2's dos/text/text_draw.h and
// ui_draw.h that use the DGROUP ink g_textInk and line height g_lineHeight). Sound_SyncState
// (43cb:046e) is in main_loop.h. Strings are DS offsets (g.dsString) or views of game bytes.
// Spec: re/dos/docs/code_ui.md §2-§6. Owner: ui agent.

#include <cstdint>
#include <optional>
#include <string_view>

namespace tmport::dos {

class DosGame;

// ---- menu bar, dialogs, commands (§5) --------------------------------------------------------
// 43cb:0001 Menu_Update (per frame): with the menu bar or a dialog open, no menu lock and a command
// waiting (DS:930C = g_cmd): menu bar Enter -> dialog DS:5B69[menu] + item (g_uiMode |= 0x20), Esc
// closes; other commands switch / move; then Dialog_Update; g_cmd = DS:930C, DS:9238 = 1; prompt
// icon 2 (quiz) or 0 (menu).
void menuUpdate(DosGame& g);
// 43cb:014a Menu_MoveSelection: item += DS:5B51[cmd] (cmd < 9) or = cmd - 9 (mouse), wrapping over
// g_boxLines; re-highlights.
void menuMoveSelection(DosGame& g);
// 43cb:019a Dialog_Update: g_uiMode & 0x20: first frame draws the box (or help, DS:5BA7 == -1);
// later Enter / Esc close it (help stays open on Enter), then Dialog_HandleButtons.
void dialogUpdate(DosGame& g);
// Confirm handler DS:62B7[g_dialogId] / cancel handler DS:6303[g_dialogId] (dialogs 0..18): a
// switch on the id instead of far pointers. Engine helpers.
void callDialogConfirm(DosGame& g, int dialogId);
void callDialogCancel(DosGame& g, int dialogId);
// 43cb:021d MenuCmd_HelpPage (confirm, dialogs 0..7): page += DS:5BC3[button]; past the last page
// closes help and reopens the Help drop-down (queued F1); else draws the page.
void menuCmdHelpPage(DosGame& g);
// 43cb:028b MenuCmd_StartOver (dialog 8, Yes): castle: restore the CPU flags; record +9 = 0;
// Users_Pack, write (StartOver), Users_LoadPlayer, Game_Start, Scene_Update, Anim_InitSlots; clear
// the shelf-room flags.
void menuCmdStartOver(DosGame& g);
// 43cb:02f8 MenuCmd_StartNewPlayer (dialog 9, Yes): record +9 = g_gameInProgress, Users_Pack, write
// (StartNewPlayer), g_uiMode = 8, Anim_InitSlots.
void menuCmdStartNewPlayer(DosGame& g);
// 43cb:032c MenuCmd_ExitAndSave (dialog 10): Yes: record +9 = g_gameInProgress, Users_Pack, write
// (ExitAndSave); always g_quit = g_buttonSel.
void menuCmdExitAndSave(DosGame& g);
// 43cb:034f MenuCmd_NewPlayerHints (dialog 11): DS:913F = g_hintsOn = DS:5A32 = button; all 15 hint
// flags = 1.
void menuCmdNewPlayerHints(DosGame& g);
// 43cb:0371 MenuCmd_Music (dialog 12): g_musicOn = DS:5A42 = button (Yes also sound on); changed ->
// g_soundChanged = 1.
void menuCmdMusic(DosGame& g);
// 43cb:039d MenuCmd_Sound (dialog 13): music = sound = DS:5A42 = DS:5A52 = button; changed ->
// g_soundChanged = 1.
void menuCmdSound(DosGame& g);
// 43cb:03cb Game_HandleHotkeys (per frame and in the title loop; g_uiMode == 0 or area 6): command
// 14 (Ctrl+S) toggles music and sound (off: stop and silence; on: the area's tune); command 15 the
// slow-CPU flag (no key produces it).
void gameHandleHotkeys(DosGame& g);

// ---- boxes and buttons (§4) ------------------------------------------------------------------
// 43cb:0592 MsgBox_Draw(n): box n of g_msgBoxes: geometry (lines counted by their terminators),
// slow-CPU background save, Box_DrawFrame, Yes/No buttons (type 1), Box_SetupInput, text; g_boxWidth,
// g_boxTextY, g_boxLines.
void msgBoxDraw(DosGame& g, int n);
// 43cb:0848 Box_DrawFrame(type, x1, y1, x2, y2, title): white box from y1 - 2, frame, shadow, title
// bar, footer by type, in g_textInk.
void boxDrawFrame(DosGame& g, int type, int x1, int y1, int x2, int y2, std::optional<std::string_view> title);
// 43cb:12cf Box_SetupInput(type, x1, x2, y2): input context 1 / 4 / 2 by type and the zone / key
// list rewrites of that context (footer, buttons, drop-down rows, PgUp/PgDn); g_cmd = -1, DS:90F6 = -1.
void boxSetupInput(DosGame& g, int type, uint8_t x1, uint8_t x2, int y2);
// 43cb:0d50 Button_Draw(i): rounded button i (labels g_buttonLabel, positions g_buttonX/Y); selected
// (g_buttonSel) = black face, white text.
void buttonDraw(DosGame& g, int i);
// 43cb:0eb6 Button_Flash: the chosen button blinks 4 times, Delay_Ticks(18) each (72 PIT ticks,
// blocking in the original while the game ticks run on): the engine turns the wait into frames
// (ENGINE_MAP §3.4) and runs the confirm handler afterwards. This call starts the wait and draws
// (and presents) the first toggle; uiFlashContinue does the rest.
void buttonFlash(DosGame& g);
// Engine: true while a Button_Flash wait is running. The original's main loop is then blocked
// inside Menu_Update (dialogs 0..13) or SignIn_Update (dialogs 14..18) while the game ticks run on,
// so the frame loop must not run a normal frame: DosGame::frameBegin skips Frame_Begin, and the
// frame body calls only uiFlashContinue and stops there while it returns false (when it returns
// true the wait is over and the frame continues after the interrupted updater). menuUpdate,
// signInUpdate, quizUpdate, gameHandleHotkeys and screenDrawOverlay do nothing while it is true
// (menuUpdate / signInUpdate continue their own flash instead), so a frame body that calls them
// anyway does not draw over the blinking box.
bool uiFlashPending(const DosGame& g);
// Engine: continues a running Button_Flash: draws the toggles due by g_ticks (start + 18k, k = 1..3,
// each presented at once like Button_Flash's Present_Dirty) and at start + 72 runs the confirm handler
// of g_dialogId and the rest of the interrupted call: Dialog_Update's and Menu_Update's tails, or
// SignIn_HandleInput's game start and SignIn_Update's tail. Returns true when the wait ended in this
// call (also when none was running).
bool uiFlashContinue(DosGame& g);
// 43cb:0ee8 Delay_Ticks(n): busy-wait n PIT ticks. Not usable as a blocking call in the engine
// (lockstep); kept for the record: returns at once. Callers model the wait as a state.
void delayTicks(DosGame& g, uint32_t n);
// 43cb:0f1d MenuBar_DrawTitle(i, on): menu title i (x DS:5BC7/5BCD, glyphs DS:5BD3/5BDF); sets
// g_textInk (0xFFFF off, 0xCCCC on).
void menuBarDrawTitle(DosGame& g, int i, bool on);
// 43cb:04ff Menu_HighlightItem: exchanges white and black over the highlighted drop-down row.
void menuHighlightItem(DosGame& g);

// ---- help pager (§6.1) -----------------------------------------------------------------------
// 43cb:107a Help_LoadFile: HELP.DAT is loaded with the game (DosGame::help); returns 1.
int helpLoadFile(DosGame& g);
// 43cb:1273 Help_Open(topic): page count, page 0, the help box (type 2), button 1 selected,
// Help_DrawPage.
void helpOpen(DosGame& g, int topic);
// 43cb:10c4 Help_DrawPage(topic, page): text (line height 10), the page buttons (labels DS:5D0B /
// 5D16 / 5D24), Box_SetupInput(2, ...), the page's pictures from the loaded sprite bank.
void helpDrawPage(DosGame& g, int topic, int page);

// ---- drawing primitives with the DGROUP ink (§2.5, §3) ---------------------------------------
// 43cb:0b63 Text_Draw(x, y, s): wave 2's textDraw with ink g_textInk and height g_lineHeight;
// returns the line count.
int textDraw(DosGame& g, int x, int y, std::string_view s);
// 43cb:0cfc Text_Width(s): advances up to NUL or CR, control bytes included.
int textWidth(DosGame& g, std::string_view s);
// 43cb:0ab9 Gfx_HLine(x, y, w, c) = fill (x, y, x + w - 1, y).
void gfxHLine(DosGame& g, int x, int y, int w, int c);
// 43cb:0ada Gfx_VLine(x, y, h, c) = fill (x, y, x, y + h - 1).
void gfxVLine(DosGame& g, int x, int y, int h, int c);
// 43cb:0afb Gfx_FillRectColor(x1, y1, x2, y2, c).
void gfxFillRectColor(DosGame& g, int x1, int y1, int x2, int y2, int c);
// 43cb:0b18 Gfx_FrameRect(x1, y1, x2, y2, c): 1-byte sides, 2-row top and bottom.
void gfxFrameRect(DosGame& g, int x1, int y1, int x2, int y2, int c);
// Sprite_GetHeader 1000:2159 + Sprite_Draw 1000:1b8d as the UI code calls them: frame code
// `frameCode` (PAK frame * 2) of the loaded sprite bank at (x, y) with `flags`; nothing when the
// bank has no such frame. Engine helper of the ui modules.
void drawBankFrame(DosGame& g, uint16_t frameCode, int x, int y, uint8_t flags);

// 43cb:156b Null_Handler2: the "no handler" entry of the elf table (RETF); no engine function.

}  // namespace tmport::dos
