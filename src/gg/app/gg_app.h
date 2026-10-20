#pragma once
// The start-up of ggport (main 12e0:0b38 and Init_CommandLine 303a:005c as the engine does them;
// re/gg/docs/code_runtime.md §2, §4, PORTING_NOTES.md §1, §7):
//
//   options (gg_app's command line) > ggport.ini in the preference folder > defaults
//   the game folder: --data, ggport.ini's data, GG_DATA, next to the program (detectGgFolder);
//     GgFiles::open checks ssg.exe and the archives and names a missing or broken file
//   the original's switches (--gg-args): -S.. -> the sound option, -GV ignored, -? the help
//   the host: PresenterLayout::dos1990() (320x200 at 4:3, or square pixels), the window title, the
//     icon from the folder's ssg.ico, F12 screenshots into <pref>/screenshots
//   the runtime (GgRuntime): deterministic when headless, the keyboard's typematic from ggport.ini,
//     --keys / --mouse scripts, the run limit (headless: --ticks and the script's end + 32 ticks;
//     in a window only --ticks), Input_InstallHandlers
//   srand: --seed, else the low word of time() (303a:0229)
//   the entry (the game, gg/game/flow.h gameMain; --gg-preview the wave 2 preview), with GgExit /
//     GgRunEnd caught here and the atexit functions run
//   --frame-dump: a folder gets one PNG per new picture (NNNNN_tTICKS.png) and end.png; a file the
//     last picture
//
// Dropped (PORTING_NOTES §11): the CPU, display, mouse-driver and memory refusals, the overlay
// manager, gizmo004.dat. Start-up errors go to stderr (and a message box when not headless) with
// exit code 1; an error inside the game ends with exit code 3.

#include <filesystem>
#include <functional>
#include <span>
#include <string>

#include "gg/app/gg_runtime.h"
#include "gg/app/options.h"
#include "gg/app/seq_run.h"
#include "gg/app/settings.h"
#include "gg/data/gg_files.h"
#include "rtl/borland_rand.h"

namespace tmport::platform {
class Host;
}

namespace tmport::gg {

// The resolved settings of one run.
struct GgLaunch {
    GgOptions options;           // the command line as given
    GgSettings settings;         // ggport.ini (after defaults)
    OriginalSwitches switches;   // --gg-args
    std::filesystem::path prefDir;  // empty: none (no saves)
    std::filesystem::path folder;   // the game folder
    GgSound sound = GgSound::Fm;
    GgCpu cpu = GgCpu::Cpu386;
    uint16_t seed = 0;           // srand's argument (DS:2454)
    bool seedFromClock = true;
    bool headless = false;
};

// What the entry gets.
struct GgContext {
    const GgFiles& files;
    GgRuntime& runtime;
    platform::Host& host;
    const GgLaunch& launch;
    BorlandRand& rand;        // the RTL generator (seed DS:442a), seeded as at 303a:0229
    ExitHandlers& atExit;     // run when the entry returns or exits (Game_Shutdown 12e0:0b19 goes here)
};

// The game: returns the exit code (or throws GgExit / GgRunEnd).
using GgEntry = std::function<int(GgContext&)>;

// The program (arguments without argv[0]); `entry` empty: the test pattern (the runtime's tests).
// ggport's main passes the game (gg/game/flow.h gameMain), or with --gg-preview the wave 2 preview
// (gg/app/preview.h).
int runGgApp(std::span<const std::string> args, const GgEntry& entry = {});

// The window title.
inline constexpr const char* kGgWindowTitle = "Gizmos & Gadgets! (GG Port)";

}  // namespace tmport::gg
