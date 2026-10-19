#pragma once
// main.c (segment 130f), main-loop parts: the game tick, the frame body, the present, the mouse poll,
// the shutdown; plus Sound_SyncState (43cb:046e) and the game-side wrappers of input.c (4a85, wave 2:
// dos/input/input_poll.h). DosGame's RuntimeClient methods call these (ENGINE_MAP §3).
// Specs: re/dos/docs/code_runtime.md §3, §6; code_ui.md §6.3. Owner: mainloop agent.

#include "dos/data/file_table.h"

namespace tmport::dos {

class DosGame;
struct TickInfo;

// 130f:0007 main, up to its frame loop: g_quit = 0, Game_Init (gameInit; Kbd_Install and
// Timer_Install are the runtime's), then the start of Title_Run (titleStart, with its OPENING load
// reported to the runtime's frame timing: the first game tick falls into it): the frames that follow
// run the title loop (DosGame::titleRunning) until it ends, then main's loop with g_uiMode = 8.
void mainStart(DosGame& g);
// 130f:0007 main, the frame loop body (130f:003D..00D1) after Frame_Begin: g_dirty = 0 is done
// before Frame_Begin; with no redraw latched: SignIn_Update, Gameover_UpdateMessages, Hints_Update,
// Menu_Update; then Scene_Update, Scroll_Update, Search_Update, Quiz_Update, Sound_SyncState,
// Game_HandleHotkeys, Screen_DrawOverlay, Playfield_Redraw, ShelfRoom_DrawShelf, RankChest_Update;
// g_spriteSlotsDrawn = 13 or 1; Sprites_DrawSorted, Frame_Present, Sprites_EraseAll.
// While the title runs (DosGame::titleRunning) the frame is one iteration of Title_Run's loop
// (titleFrame; when it ends: titleFinish, g_uiMode = 8). While a dissolve spans frames
// (DosGame::dissolveRunning, one long Frame_Present of the original) the frame only continues it;
// Sprites_EraseAll follows its last cell. At the reference frame speed the frame runs as many
// Search_Update calls as reference search frames fit into it, and reports what it cost on the
// reference machine to the runtime (DosRuntime::frameTiming: USERS.DAT writes, Quiz_LoadQuestions and
// PAN loads with g_quit = 1, a full HUD redraw, a bubble opening; PORTING_NOTES.md §2.5) as the work
// happens, in the original's order (wave 5: a sound call reaches the driver where the frame stands,
// DosRuntime::soundDelay).
void mainFrameBody(DosGame& g);
// What Frame_Begin's latches start, for the reference frame timing (DosRuntime::frameTiming,
// PORTING_NOTES.md §2.5): a scene the ticks asked for (g_sceneWanted != g_loadedScene with no UI mode
// open; Scene_Update loads it with g_quit = 1 and its load reports itself) and a full play-field
// redraw (its cost is reported before Playfield_Redraw). On the reference machine that frame began
// right after the tick that asked for it (or when a longer frame before it ended), and so the ticks
// after it fall into the load. Called by DosGame::frameBegin after Frame_Begin; sets
// DosGame::latchedLoad, latchedRedraw and redrawCostDue.
void frameBeginTiming(DosGame& g);
// The reference cost of the frame's I/O (a g_quit = 1 section: the game ticks inside it do not run),
// reported by the routine that does it, where it does it, while the runtime runs a frame or main's
// start-up (FrameTiming::isOpen; nothing otherwise):
//   reportPanLoad     Scene_LoadPan 1460:05e8: 3.4 PIT ticks + 0.35 per 1000 bytes of the file
//   reportBankLoad    the sprite bank Scene_Update reads with a scene change (CLOSE.PAK, SHACK.PAK,
//                     SPRITES.PAK after them): 0.019 PIT ticks per 1000 bytes (SPRITES.PAK 2.2)
//   reportUsersWrite  Users_WriteFile 47c7:055a: 4.5 PIT ticks
//   reportQuizLoad    Quiz_LoadQuestions in Game_Start 47c7:08b5: 7 PIT ticks
void reportPanLoad(DosGame& g, FileIndex pan);
void reportBankLoad(DosGame& g, FileIndex pak);
void reportUsersWrite(DosGame& g);
void reportQuizLoad(DosGame& g);
// 130f:00fe Game_Tick (per game tick; the runtime handles g_quit and the two counters): not gated:
// player screen offsets (DS:8FA7/8FA9, wrapped rows), Mouse_Poll, Input_Tick, Anim_Tick, and with the
// scene loaded (or the title) Input_MouseWalk, Player_Update, Elves_Update, Cells_UpdateVisible;
// gated (info.gated): Anim_Tick, Input_Tick.
void gameTick(DosGame& g, const TickInfo& info);
// 130f:01c2 Frame_Present: nothing drawn: no copy (the 1000-iteration delay is dropped); no
// transition: Present_Dirty (g.back -> g.screen); a latched transition: the dissolve
// (transitionDissolve, camera.h), then Present_Full and g_transition = 0. A dissolve that needs
// more frames sets DosGame::dissolveRunning (mainFrameBody continues it); at the reference frame
// speed its first frame's cells make it end in the frame of the last game tick before its end on the
// reference machine (PORTING_NOTES.md §2.5). Each copy into g.screen counts DosGame::screenVersion.
void framePresent(DosGame& g);
// 130f:00db Game_Shutdown: Music_Stop, Sfx_Stop; the timer / keyboard / video restores have no
// engine part; ends the run (g_quit = 1). Runs once (DosGame::shutdownDone): from TitleStep_Wait
// (Esc on the title) or after the frame loop ended.
void gameShutdown(DosGame& g);
// 130f:0d94 Mouse_Poll (per executed game tick, with a mouse): g.mouse.poll(...) and its calls on
// slot 0 (Anim_Set 1/7/8, Anim_SetIfIdle(0, slot 0's script), Anim_Clear(0)), slot 0 at the pointer,
// hidden after 600 idle ticks.
void mousePoll(DosGame& g);
// 43cb:046e Sound_SyncState (per frame): g_uiMode changed (vs g_lastUiMode): music runs (gate 0xFFFF)
// or pauses (gate 0, Sfx_Stop, Sound_SilenceAll); g_soundChanged: stop or the area's tune.
void soundSyncState(DosGame& g);
// 4a85:03ac Input_Tick (both Game_Tick paths): g.input.tick(...) and its menu / dialog effects on
// g_uiMode, DS:9323, DS:90FC, DS:9111.
void inputTick(DosGame& g);
// 4a85:0208 Input_MouseWalk (before Player_Update): g.input.mouseWalk(...) with the player's screen
// offsets and the previous tick's mask; sets g_promptIcon when it returns one.
void inputMouseWalk(DosGame& g);

// 4a85:000e Input_Poll, 4a85:0107 Input_LookupKey, 4a85:0130 Input_HitZone, 4a85:00cd
// Mouse_ChangedMask (no caller): wave 2's InputPoll / inputLookupKey / inputHitZone; no game wrapper.
// 43a0:0002 Kbd_Install, 43a0:0043 Kbd_Remove, 43a0:0071 Kbd_ISR, 43a0:02ab Kbd_CtrlBreakCb:
// wave 2's PcKeyboard / KbdIsr / BiosKeyboard (dos/input/keyboard.h); no game wrapper.

}  // namespace tmport::dos
