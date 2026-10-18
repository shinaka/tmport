#pragma once
// Headless runs of the modal scenes (area "scenes") with scripted input on a manual clock, for
// comparison with reference captures of the original (re/docs/REFERENCE_CAPTURES.md). No window,
// no audible sound: the wave voice is a silent output timed on the manual clock (speech lengths
// follow the real SSND lengths), MIDI goes to the null back end in manual mode.
//
//   tmport --scene <kind> [args] [options]
//     quiz <q>          Quiz_AskQuestion on slot --slot (default 0) holding question q
//                       (--perm abc answer order per display position, default 012; --tried abc
//                       flags, default 000; --clues n words already earned)
//     hint <n>          Hint_Show(n) over the room background (--room, default 9)
//     cutscene <level>  LevelCutscene_Play(level) over the gate room of that level (11 / 25)
//     celebrate <n>     Climb_Celebrate with climbsDone n-1 before (--total T before, --points P)
//     rank <total>      RankScreen_Show
//     seerank <total>   Menu_SeeRank over the room background
//     castle            ClimbScene_Run from room 49's start pose (--tier)
//   Options:
//     --keys "<script>"   input script (tokens below); --keys-file <file>
//     --out <dir>         directory for shots (default extracted/scratch/scenes/out)
//     --shot-every <ms>   also write every-<ms> shots NNNN.png (clock time from the start)
//     --seed <n>          srand(n) before the scene (default 1)
//     --tier <t>          rank tier (castle layout and creature chance, gems)
//     --coins <n> --nets <n>   inventory (HUD), --name <player> (rank screen)
//     --voice 0|1 --sound 0|1   (default 1 / 1); --no-audio
//     --max-ms <n>        stop after n ms of simulated time (default 600000)
//     --trace             print scene events with the simulated time
//     --data <path>, -v
//   Script tokens (whitespace separated; times relative to the start):
//     wait:MS  shot:NAME  end
//     enter esc space tab ctrl+s y n c key press (down + up)
//     up down left right               key press (down + up) of an arrow
//     hold:KEY:MS                      arrow held for MS (advances the script time)
//     down:KEY up:KEY                  arrow key down / key up only
//     code:0xNNNN                      Evt_PostKey with a raw input code
//     click:X,Y rclick:X,Y press:X,Y release:X,Y move:X,Y   (game coordinates)
// At the end the presented game area is written to <out>/final.png.

#include <optional>
#include <string>

namespace tmport::game::scenes {

std::optional<int> runScenesCommand(int argc, char** argv);
std::string scenesUsage();

}  // namespace tmport::game::scenes
