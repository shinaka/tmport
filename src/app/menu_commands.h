#pragma once
// The game's menu command handlers (segment 1140 "menu commands", 1180 help topics, 10f0:0487 See
// Rank; re/docs/code_10f8-1148.md §13, code_1168-1198.md §5, sys_resources_saves.md §6.5) and their
// registration with the in-engine menu bar.
//
// The handlers are free functions because the game also calls some of them directly: MenuCmd_Exit
// is what Esc / window close does in the room loop (10e0:0191), the rank screen and the castle
// climb, and Climb_Celebrate passes it as the Esc callback of Anim_PlayModal.

#include "app/app_context.h"

namespace tmport::app {

// 1140:0000 MenuCmd_AboutTM -> 1140:0008 About_Show: GameDlg_RunModal(DS:1919).
void menuCmdAbout(AppContext& ctx);
// 1140:0015 MenuCmd_NewPlayerMessages: Yes/No DS:15cc preset to g_hintsOn; Yes: hints on and every
// once-only hint shown again (g_hintUnseen = 0xFFFFFFFF); No: hints off, pending hint dropped,
// g_hintUnseen = 0; Esc: unchanged.
void menuCmdNewPlayerMessages(AppContext& ctx);
// 1140:00cc MenuCmd_MusicAndSound: Yes/No DS:1625 preset to g_soundOn; an answer that differs
// toggles (Audio_ToggleSound).
void menuCmdMusicAndSound(AppContext& ctx);
// 1140:00f7 MenuCmd_Voice: Yes/No DS:163f preset to g_voiceOn; Yes/No sets it (a clip already
// playing continues).
void menuCmdVoice(AppContext& ctx);
// 1140:0062 MenuCmd_StartAnotherGame: Yes/No DS:14d6; Yes: hint dropped, Music_StopScene, throws
// RestartToSignIn (longjmp(DS:5eda, 1)).
void menuCmdStartAnotherGame(AppContext& ctx);
// 1140:0098 MenuCmd_Exit: Yes/No DS:1533; Yes: (Scene_FreeCommon, Treasures_Free: RAII in the
// port) Users_SaveCurrent via ctx.services.saveCurrentPlayer, then throws QuitGame (Game_ExitNow
// 1140:008c, longjmp(DS:5eda, 2)).
void menuCmdExit(AppContext& ctx);
// 1180:0117 / 0128 / 0139 / 014a -> 1180:0000 Help_ShowTopic(topic 0..3): the help pager.
void menuCmdHelp(AppContext& ctx, int topic);
// 10f0:0487 Menu_SeeRank: the game area's ctx.services.seeRank (nothing while it is empty).
void menuCmdSeeRank(AppContext& ctx);

// Registers every handler above with ctx.menu (no-op when ctx.menu is null).
void registerMenuCommands(AppContext& ctx);

}  // namespace tmport::app
