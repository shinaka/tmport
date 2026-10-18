#pragma once
// Headless runs of the title and Club Sign-In screens (area screens) with scripted input on a
// manual clock, for comparison with reference captures of the original
// (re/docs/REFERENCE_CAPTURES.md E1/E2). No window, no audible sound: the audio system (when
// enabled) uses a silent wave output timed on the manual clock and the null MIDI back end in
// manual mode, so narration lengths and the OPEN1 -> OPEN2 switch follow simulated time.
//
//   tmport --screen <title|signin|attract> [options]
//     title    Title_Run; prints the result (continue / quit)
//     signin   SignIn_Run; prints the chosen or created player
//     attract  title, then the sign-in as at program start (back page = title picture)
//   Options:
//     --keys "<script>"     input script (tokens below); --keys-file <file> reads it from a file
//     --out <dir>           directory for shot:NAME (NAME.png, the presented 512x384 game area)
//     --users <file>        users.dat used read-write (default: <out>/users.dat)
//     --restart             sign-in after "Start Another Game" (backdrop PMAP 2100)
//     --player <name>       a signed-in player for --restart: loaded from the users file if the
//                           name is on it (its slot becomes the current one), else a new player
//     --voice 0|1 --sound 0|1   initial settings (default 1 / 1); --no-audio: no audio system
//     --max-ms <n>          stop after n ms of simulated time (default 900000)
//     --trace               print screen events with the simulated time
//     --data <path>, --16, -v, --mods <dir> (repeatable, cli/tool_mods.h)
//   Script tokens (whitespace separated; times are relative to the start of the run):
//     wait:MS               advance the script time
//     shot:NAME             write <out>/NAME.png at the current script time
//     enter esc space tab bs ctrl+r ctrl+s     WM_CHAR keys
//     up down left right pgup pgdn home end   WM_KEYDOWN + WM_KEYUP
//     char:C                one WM_CHAR byte (C is a character, or 0xNN)
//     text:STRING           WM_CHAR per character, 60 ms apart (advances the script time)
//     code:0xNNNN           Evt_PostKey with a raw input code
//     click:X,Y rclick:X,Y  mouse move + button down + up at game coordinates
//     press:X,Y release:X,Y move:X,Y
//     end                   stop the run here

#include <optional>
#include <string>

namespace tmport::game {

std::optional<int> runScreensCommand(int argc, char** argv);
std::string screensUsage();

}  // namespace tmport::game
