#pragma once
// Headless tools of the G&G runtime (the ggport-cli dispatcher calls runtimeCli with the program's
// arguments, without argv[0]; it returns false when none of them is its command). No game files are
// needed.
//
//   --gg-clock [--ticks N]       the time base: PIT divisor and rate, tick period, sound tick, the
//                                retrace, the time of tick N and the retraces and sound ticks by then
//   --gg-input-trace [--keys S] [--mouse S] [--ticks N] [--poll-every K] [--play-mode]
//                    [--zone X,Y,W,H,PRI,PRESS,RELEASE[,SHAPE]]... [--hot X,Y]
//                                runs the keyboard, mouse and zone model on a script in PIT ticks
//                                (deterministic) and prints one TSV row per poll: every K ticks
//                                (default 4: a frame loop) Input_GetCode, with g_ticks, the held bits,
//                                the ring indices, the BIOS buffer, the pointer, the latches and the
//                                code; SHAPE 1 makes a circle (W = radius); --hot sets the cursor's
//                                hot spot

#include <string>
#include <vector>

namespace tmport::gg {

bool runtimeCli(const std::vector<std::string>& args, int& exitCode);

}  // namespace tmport::gg
