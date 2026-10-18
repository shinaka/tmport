#pragma once
// Headless runs of the room loop (area room) with scripted input on a manual clock, for comparison
// with the reference captures of the original (re/docs/REFERENCE_CAPTURES.md E3-E6, E13). No
// window, no audio (sound effects are only logged), no modal scenes (hints / questions / the quit
// prompt are logged; a question can be answered by rule, see --quiz).
//
//   tmport --room-run [options]            (dispatched from tmport's CLI, cli/commands.cpp)
//   Player (one of):
//     --record <users.dat> <slot|name>   load a saved player (Users_LoadSelected)
//     --new-player <name>                a new player (Users_InitNewPlayer, room 0xFE)
//   Climb set-up (GameMain 1118:00e5): srand(--seed n) (default 1), Scene_LoadCommon,
//     setupClimb (new climb when room = 0xFE), Hud_SetClueBoxMode(0), Room_Present(1); then
//     Room_Run and the room changes until the script or the tick limit ends.
//     --climbs n                   g_climbsDone before the set-up (treasure hall size and pedestals)
//   Overrides applied after the set-up (to reproduce a save state):
//     --start <room> <x> <frame> <facing 0|1>   player pose (y = frameY[frame])
//     --coins n  --nets n  --key 0|1  --shop-coins n  --gems n  --clues n (earned clue words)
//     --pile <room> <x>             the rescue coin pile in that room at x
//     --no-elves                    never spawn elves (set-up and level changes; no elf random numbers)
//     --elf set:room:x:facing:step[:item]   place an elf by hand (repeatable; implies --no-elves;
//                                   y = the script's y of step-1)
//     --quiz correct|wrong|esc      how a caught carrier's question is answered (default esc)
//     --clue-box-mode m             Hud_SetClueBoxMode(m) after the set-up (1 prompt, 2 CONTINUE)
//   Output:
//     --dump-every N <dir>          the presented 512x384 game area every N ticks (tick_NNNNN.png;
//                                   tick_00000.png = after Room_Present(1); tick_N_pauseK.png at
//                                   each presentation pause; final.png when room 49 is reached)
//     --out <dir>                   directory of shot:NAME
//     --log                         one line per tick: room, frame, x, y, facing, coins, nets,
//                                   steps and the sound effects of the tick
//     --ticks N                     stop after N ticks (default 600)
//   Script (--keys "<tokens>", --keys-file <file>); times in ms after the climb start (one tick =
//   100 ms; input posted at time t is fetched by the first tick that starts at or after t):
//     wait:MS  tick:N (wait until N*100 ms)  shot:NAME  end
//     enter esc space c tab           WM_CHAR keys
//     up down left right              key down + up (a tap; may be missed by the 100 ms poll)
//     hold:KEY:MS                     key down now, key up MS later (does not advance the time)
//     kd:KEY ku:KEY                   key down / key up (KEY = up down left right)
//     click:X,Y press:X,Y release:X,Y move:X,Y   mouse at game coordinates
//     --data <path>, --16, -v

#include <optional>
#include <string>

namespace tmport::game::room {

std::optional<int> runRoomCommand(int argc, char** argv);
std::string roomUsage();

}  // namespace tmport::game::room
