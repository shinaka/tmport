#pragma once
// init.c (segment 1460), the parts the engine keeps: the start-up values of the game globals and the
// BEGIN.PAK load. Files, paths, disks, hardware detection and switches are wave 2's (DosFiles,
// edition/dos_options.h, MouseDriver). Spec: re/dos/docs/code_runtime.md §4. Owner: mainloop agent.

namespace tmport::dos {

class DosGame;

// 1460:0003 Game_Init, the game parts after the files are open (DosGame's constructor did the rest):
// g_videoType / g_soundDevice / g_slowCpu from the options (ParseCommandLine's effect), g_machineId,
// g_startDrive = 2 (hard disk), g_mouse (MouseDriver::present), Video_Init's dirty tracking (DS:37AE
// = 0xFF), Help_LoadFile, Quiz_LoadDictionary, Sprites_LoadBegin, Game_InitVars. Pal_Apply,
// Mouse_Setup and Sound_LoadData have no engine step.
void gameInit(DosGame& g);
// 1460:033a Game_InitVars: the initial values of code_runtime.md §4.4 (g_uiMode 0x80, g_loadedScene
// 0xFF, g_cmd -1, DS:90F6 -1, DS:9238 1, DS:90FC -1, DS:9111 1, DS:90FE 12, DS:9100 60, clip window,
// DS:8FB4 0x32, DS:928B 6, shown -1, DS:9254 0xFF, DS:9213 0x1A, DS:9211 0x44, g_slowScroll,
// Anim_InitSlots, DS:8FA5 = g_slowCpu, ...).
void gameInitVars(DosGame& g);
// 1460:02c6 Sprites_LoadBegin: the sprite bank = BEGIN.PAK (loadSpriteBank); returns 1. Also called
// by SignIn_Update.
int spritesLoadBegin(DosGame& g);

// Not ported (no engine counterpart; ENGINE_MAP §2): 1460:022a Sound_LoadData (SoundBank /
// DosAudioDevice), 1460:0437 StrIndexOf, 1460:0463 ParseCommandLine (applyDosSwitches),
// 1460:0567 Video_GetMode, 1460:056e Video_RestoreMode, 1460:057d HardErr_Handler,
// 1460:05a3 Net_IsNetworkDrive, 1460:06c8 File_OpenByIndex, 1460:07b4 File_Read, 1460:080b File_Write,
// 1460:0849 File_Close (DosFiles), 1460:0887 Fatal_Print (errors are exceptions naming the file),
// 1460:08cc Path_InitDrives, 1460:09b7 StrCopyBounded, 1460:09f2 StrConcatList, 1460:0a5e
// Mouse_Detect, 1460:0a74 Mouse_Setup (MouseDriver). 1460:05e8 Scene_LoadPan is in scene.h.

}  // namespace tmport::dos
