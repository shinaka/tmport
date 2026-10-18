#pragma once
// GameMain (segment 1118, 1118:0000): the game's main program from the title to the shutdown, the
// restart point and the climb loop. Specs: re/docs/ARCHITECTURE.md §2.1-§2.12 (states S4-S13),
// code_10f8-1148.md §8, PORTING_NOTES.md §3 / §4.12.
//
//   S4  start       palette (set once by the Engine: Pal_SetFromRGB is one-shot), Anim_ResetAll,
//                   Elf_InitSystem (the RoomLoop's ElfSystem), cursors (the Engine's
//                   Cursor_LoadSet DS:1292), arrow cursor
//   S5  title       Title_Run (game/title.h); Esc + Yes -> S12
//   S6  restart     the setjmp at 1118:004c as app::runRestartPoint: r = 0 the first time, 1 after
//                   File > Start Another Game (RestartToSignIn), QuitGame (longjmp 2) -> S12;
//                   the reset block: g_rankPromoted = 0, SetCursorForced(1), Music_ResumeForce,
//                   RoomSlots_Free, Treasures_Free, Scene_FreeCommon, Elf_UnloadAll,
//                   Anim_ResetAll, TreasureHall_UnloadRoom, g_caughtElf = g_playerHidden =
//                   g_doorStep = g_coinStep = g_netStep = 0, g_coinTargetSlot = 0xFF; a black
//                   screen when r != 0; GameMenu_Remove
//   S7  sign-in     SignIn_Run(r) (game/signin.h; it saves a signed-in player first)
//   S8  climb set-up  srand(low 16 bits of the clock) on the engine's RNG (the one RoomLoop and
//                   the scenes use); black screen + back page; wait cursor; Scene_LoadCommon;
//                   game::setupClimb (TreasureHall_BuildRooms, new climb only when room = 0xFE,
//                   clue texts / voices, elves, treasures, pose, gems); Music_PlayZone(room
//                   zone); GameMenu_Install; RoomLoop::startClimb (slots, hall, HUD,
//                   Room_Present(1), SetCursorForced(0))
//   S9  room        RoomLoop::visitRoom (Room_Run + the room change, the music deferral of rooms
//                   0 / 9) with RoomHooks {exitPrompt = MenuCmd_Exit, askQuestion = the quiz
//                   screen, showHint = the hint popup}
//   S10 cut-scene   ClimbHooks::levelCutscene = LevelCutscene_Play(oldLevel)
//   S11 castle      room 49: ClimbScene_Run, then room = 0xFE
//   S13 climb end   Music_StopScene, Treasures_Free, Elf_UnloadAll, clues / key / gems reset,
//                   GameMenu_Remove, SetCursorForced(1), Users_SaveCurrent (location 0xFE, the
//                   old total: the score is added afterwards), Scene_FreeCommon, black screen,
//                   Climb_Celebrate (+ rank screen), Rank_UpdateTier -> S8 (a new climb)
//   S12 shutdown    Users_SaveCurrent, arrow, Scene_FreeCommon, Elf_UnloadAll, Treasures_Free,
//                   RoomSlots_Free, TreasureHall_UnloadRoom, Music_StopScene (File > Exit has
//                   saved once already: Exit saves twice, as in the original)
//
// While it runs, GameMain fills AppContext::services (File > See Rank = the rank screen with the
// play-field saved and restored; Users_SaveCurrent for Exit) and uses AppContext::state (its own
// GameState when that is null); both are restored when it returns.

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

#include "app/app_context.h"

namespace tmport::game {

struct GameMainOptions {
    // srand value for every climb set-up instead of the low 16 bits of the clock (tests and
    // replays; the original seeds from timeGetTime(), 1118:00e5).
    std::optional<uint16_t> seed;
    // State-machine trace, one line per transition ("S8 climb set-up: ...", "S13 climb end",
    // "save: climb end", ...), plus the scenes' own trace lines prefixed "scene: ". Every line also
    // goes to the debug log (--verbose).
    std::function<void(std::string_view line)> trace;
};

// 1118:0000 GameMain. Returns when the game ends (title Esc + Yes, File > Exit / Esc + Yes, the
// sign-in's exit prompt, or the end of a headless script). This is the bootstrap's GameMainFn.
void gameMain(AppContext& ctx);
void gameMain(AppContext& ctx, const GameMainOptions& options);

}  // namespace tmport::game
