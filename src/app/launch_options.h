#pragma once
// Command line of the game itself (`tmport` with no tool command). The tools (--info, --dump-*,
// --render-*, --view, ...) keep their own parsers in src/cli and the other areas; main() runs the
// game only when every argument is one of the options below (or there are none, or --play).

#include <optional>
#include <string>
#include <vector>

namespace tmport::app {

struct LaunchOptions {
    std::string dataPath;       // --data <dir|cue|bin|iso>; default TM_DATA, tmport.ini, auto-detect
    std::string configPath;     // --config <ini>: tmport.ini to use
    std::string prefDir;        // --pref-dir <dir>: saves/settings directory (default: SDL pref path)
    std::string screenshotDir;  // --screenshot-dir <dir>
    bool colour16 = false;      // --16
    bool integerScale = false;  // --integer-scale
    bool fullscreen = false;    // --fullscreen (also [engine] fullscreen in tmport.ini; Alt+Enter / F11 toggle)
    bool windowed = false;      // --windowed: start in a window even if tmport.ini says fullscreen
    bool noPause = false;       // --no-pause
    bool tapLatch = false;      // --tap-latch
    std::string inputMode;      // --input-mode lossless|first-event
    std::string midiSet;        // --midi-set E|B
    std::string midiBackend;    // --midi auto|winmm|soundfont|null
    std::string soundFont;      // --soundfont <file.sf2> (selects the soundfont output unless --midi is given)
    bool noAudio = false;       // --no-audio: no wave device, null MIDI
    bool verbose = false;       // --verbose
    bool debugKeys = false;     // --debug: F9 toggles the treasure boxes
    bool showTreasure = false;  // --show-treasure: start with the treasure boxes on (implies --debug)
    // Verification / automation:
    bool headless = false;      // --headless: no window, no message boxes, silent audio; the run
                                // ends when the --keys script is used up
    std::string keys;           // --keys <script> (app/script_input.h)
    std::string frameDump;      // --frame-dump <png>: the 640x480 canvas when the game ends
    // Mods (area mods, docs/MODDING.md):
    std::vector<std::string> modDirs;  // --mods <dir> (repeatable): extra mod folders, after tmport.ini's
    bool noMods = false;               // --no-mods: load no mods at all
    // Editions (edition/edition.h) and the 1990 DOS edition (edition/dos_options.h); the values are
    // checked by the parser, empty = not given:
    std::string edition;        // --edition win|dos|auto: which edition to play when the data holds both
    std::string dosLook;        // --look ega|tandy|mcga|cga (hercules = cga)
    std::string dosSound;       // --sound tandy|speaker|adlib|off
    bool dosSlowCpu = false;    // --slow-cpu (the original's -X)
    std::string frameSpeed;     // --frame-speed reference|period
    std::string dosArgs;        // --dos-args "<switches>": the original's switches (-E -C -T -H -M, -X, -Si/-St/-Sa)
    bool squarePixels = false;  // --square-pixels: show the DOS picture with square pixels (16:10)
    // DOS game diagnostics (dos/game/trace.h):
    std::string dosTrace;                    // --dos-trace <csv>: per game tick trace
    std::vector<std::string> dosDumpStates;  // --dos-dump-state <tick>:<file> (repeatable): DGROUP image
    std::string dosTicks;                    // --dos-ticks <n>: a headless run lasts at least n game ticks
    bool dosPreview = false;                 // --dos-preview (not in the help): the wave-2 screen preview
    std::string dosLayoutSeed;               // --dos-layout-seed <n>: srand value of new games instead of time()
};

// True when argv should start the game: no arguments, --play, or only options of LaunchOptions.
bool isGameInvocation(int argc, char** argv);
// Parses the game's command line (--play is accepted and ignored). nullopt + error on bad input.
std::optional<LaunchOptions> parseLaunchArgs(int argc, char** argv, std::string& error);
std::string launchUsage();

}  // namespace tmport::app
