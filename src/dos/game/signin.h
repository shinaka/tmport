#pragma once
// signin.c (segment 47c7): the Club Sign-In (list, new name, remove mode, its dialogs), the
// USERS.DAT image (g.users: DosUsersStore) and its writes at the original's moments, the record
// pack / restore, and Game_Start / Game_Restore / Game_End. Spec: re/dos/docs/code_ui.md §9.
// Owner: ui agent.

#include <cstdint>

#include "dos/save/users_store.h"

namespace tmport::dos {

class DosGame;

// ---- sign-in screen (§9.2-§9.6) ---------------------------------------------------------------
// 47c7:0007 SignIn_Update (per frame while g_uiMode & 8): (re)initialise (OPENING.PAN and BEGIN.PAK
// when coming from a game, Sfx_Stop, area 4, sub-state 1, Users_EnsureLoaded, painter 2) or
// SignIn_HandleInput; g_cmd = -1, DS:9238 = 1.
void signInUpdate(DosGame& g);
// 47c7:0f8b SignIn_HandleInput: zone count, g_signinChar (first letter upper-cased), the handler of
// g_signinState (DS:6186 -> DS:616A); a chosen player: Users_LoadPlayer, Game_Start, HUD redraw.
void signInHandleInput(DosGame& g);
// 47c7:03cf SignIn_Draw (screen painter 2, when DS:922E != 0): panel, name entry if no player, key
// line, zones, highlight.
void signInDraw(DosGame& g);
// 47c7:00e7 SignIn_DrawPanel: backdrop (fast CPU), the light-cyan panel, BEGIN frame 31, heading by
// mode, the list.
void signInDrawPanel(DosGame& g);
// 47c7:0189 SignIn_DrawNameEntry: panel, box 15, the field frame, a first typed character, cursor.
void signInDrawNameEntry(DosGame& g);
// 47c7:01e0 SignIn_DrawList: players sorted by total (the original's shell sort), name, rank label
// (DS:625D by DS:629D), total; the new-player row (DS:6375) unless in remove mode.
void signInDrawList(DosGame& g);
// 47c7:042d Screen_DrawBackdrop: OPENING.PAN full screen (40 tile columns).
void screenDrawBackdrop(DosGame& g);
// 47c7:05a9 SignIn_DrawTypedChar(x, y, ch): one glyph in g_textInk; g_dirty = 1.
void signInDrawTypedChar(DosGame& g, uint8_t x, uint8_t y, uint8_t ch);
// 47c7:05d4 SignIn_DrawField(on): the block cursor (black when g_caretOn) after the typed name, the
// underscores of the free character cells.
void signInDrawField(DosGame& g, bool on);
// 47c7:06d4 SignIn_HighlightRow(x1, y1, x2, y2, a, b): exchanges colours a and b through colour 3.
void signInHighlightRow(DosGame& g, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint16_t a, uint16_t b);
// 47c7:071a SignIn_MoveHighlight: toggles the highlight of row g_signinRow (colours 15 <-> 10).
void signInMoveHighlight(DosGame& g);
// 47c7:0831 SignIn_SetupZones: zone templates DS:622D / 6235, row zones, input context 4, g_cmd = -1.
void signInSetupZones(DosGame& g);
// 47c7:1011 SignInCmd_Select (sub-state 1): Enter chooses (new name / list full box / saved-game box
// 11 / start), Esc the exit box, Up/Down/mouse rows, Ctrl+R remove mode, a printable starts a name.
void signInCmdSelect(DosGame& g);
// 47c7:1199 SignInCmd_NewName (sub-state 3): Enter accepts (identical name box 14), Esc back,
// Backspace, caret blink on DS:90FE, printable characters (8 max, no leading space).
void signInCmdNewName(DosGame& g);
// 47c7:1328 SignInCmd_Remove (sub-state 5): Enter the remove box 10, Esc back, Up/Down over players.
void signInCmdRemove(DosGame& g);
// 47c7:13d7 Dialog_CloseRedraw (cancel of dialogs 0..13): UI_ForceRedraw.
void dialogCloseRedraw(DosGame& g);
// 47c7:13dd SignInDlg_ExitYes (dialog 14): g_quit = g_buttonSel; SignInDlg_Back.
void signInDlgExitYes(DosGame& g);
// 47c7:13e9 SignInDlg_Back: sub-state &= 7, UI_ForceRedraw, DS:922E = 1.
void signInDlgBack(DosGame& g);
// 47c7:13fa SignInDlg_RemoveYes (dialog 15): Yes: Users_ClearRecord (write), re-read, re-init;
// SignInDlg_Back.
void signInDlgRemoveYes(DosGame& g);
// 47c7:1414 SignInDlg_ContinueAnswer (dialog 16): No drops the saved game (record +9 = 0); start.
void signInDlgContinueAnswer(DosGame& g);
// 47c7:1436 SignInDlg_Reset (dialog 17): sub-state 1, re-init.
void signInDlgReset(DosGame& g);
// 47c7:1442 SignInDlg_ListFull (dialog 18): remove mode.
void signInDlgListFull(DosGame& g);
// 47c7:1454 Dialog_HandleButtons: Enter flashes and runs the confirm handler, Esc the cancel
// handler; Left/Right/Tab toggle, Y/N select and queue Enter, mouse buttons; redraws both buttons.
void dialogHandleButtons(DosGame& g);
// 47c7:1502 Dialog_HandleOk: boxes without buttons: Enter -> confirm, Esc -> cancel.
void dialogHandleOk(DosGame& g);
// 47c7:152b UI_ForceRedraw: with a saved box background (slow CPU): DS:922E = 1, Screen_DrawOverlay,
// Present_Dirty; nothing on a fast CPU.
void uiForceRedraw(DosGame& g);
// Engine: what SignIn_HandleInput and SignIn_Update do after a sign-in box's Button_Flash wait and
// confirm handler (ui.h uiFlashContinue): a chosen player starts (Users_LoadPlayer, Game_Start,
// DS:922E = 1), then g_cmd = -1 and DS:9238 = 1.
void signInFlashDone(DosGame& g);

// ---- USERS.DAT and the player record (§9.7-§9.9) ----------------------------------------------
// 47c7:0452 Users_EnsureLoaded: loads the image once (g_usersLoaded); 1 if any player exists.
int usersEnsureLoaded(DosGame& g);
// 47c7:04a4 Users_NameExists: trailing blanks dropped from g_newName, case-insensitive compare.
int usersNameExists(DosGame& g);
// 47c7:0514 Users_ReadFile: the whole image from the engine's dos_users.dat (g.users.load()).
void usersReadFile(DosGame& g);
// 47c7:055a Users_WriteFile: the whole image, at one of the original's moments (the engine's store
// needs the moment for its log; the bytes are the original's).
void usersWriteFile(DosGame& g, DosSaveMoment moment);
// 47c7:075d Users_ClearRecord: record of row g_signinRow cleared (masks 7); write.
void usersClearRecord(DosGame& g);
// 47c7:07b9 Users_AddPlayer: first free record, list entry, Users_SelectRecord, Users_InitNewPlayer,
// the name; write.
void usersAddPlayer(DosGame& g);
// 47c7:09dd Users_SelectRecord: g_userRec = record of row g_signinRow (g.ui.userRec).
void usersSelectRecord(DosGame& g);
// 47c7:0a8b Users_InitNewPlayer: tier = level = 0, coins 5 and nets 10 (also banked), player
// (0x140,0x70), camera (0xC0,0), games 0, state 3, collected 0x80, shelf bits 0, shown -1;
// Frame_Begin; Users_Pack.
void usersInitNewPlayer(DosGame& g);
// 47c7:0b19 Users_LoadPlayer: tier, level 0, banked coins / nets, shown treasure (DS:9254), games,
// sound settings (only while music is on), player and camera of a new game, state 3, area 0,
// g_loadedScene 0xFF, hints on below 5, all hint flags 1; Frame_Begin.
void usersLoadPlayer(DosGame& g);
// 47c7:0e0e Users_Pack: the game into the record (hiding places, variable cells, level picture
// words, counters, position, settings, header word = g_saveHeapHigh).
void usersPack(DosGame& g);
// 47c7:0c62 Game_Restore: the record into the game (the inverse of Users_Pack), then
// Layout_MirrorSharedCells, the slow-CPU flags (castle: fast), Castle_ApplyLayout, Frame_Begin.
void gameRestore(DosGame& g);
// 47c7:0bd6 Game_NewSetup: found 0, key state 8, prompt 4, shop 4, slots 0xFFFF, masks 7, clue words
// and found masks 0, Layout_ClearSlots, Cells_ResetVariable, Layout_Generate, DS:8FA5 = g_slowCpu.
void gameNewSetup(DosGame& g);
// 47c7:08b5 Game_Start: new (Game_NewSetup, area 5) or saved (Game_Restore) game,
// Cells_AdjustForCpu, Music_Stop, Sfx_Stop, Quiz_LoadQuestions, mode / painter / context resets,
// start message, coins shown = coins, the area's tune (twice: directly and via DS:5BC5), box
// defaults DS:5A32/5A42/5A52. Returns 1.
int gameStart(DosGame& g);
// 47c7:09a2 Game_End: games played +1, DS:9232 / 9289, record +9 = 0, banked nets / coins,
// Users_Pack, write (GameEnd).
void gameEnd(DosGame& g);

}  // namespace tmport::dos
