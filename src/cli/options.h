#pragma once
// Command-line options of tmport's data-inspection tools (--info, --dump-pmap, --dump-sheet,
// --headless-frame, --view) and of --help. The game (app/launch_options.h) and the other tool
// groups (text/UI, DAM, screens, scenes, room loop, audio, mods) have their own parsers; cli::run
// (cli/commands.h) dispatches between them.

#include <optional>
#include <string>
#include <vector>

namespace tmport::cli {

enum class Command { Help, Info, DumpPmap, DumpSheet, View, HeadlessFrame };

struct Options {
    Command command = Command::Help;
    std::string helpTopic;       // --help [topic]: empty = the overview (see helpTopics())
    std::string dataPath;        // --data <dir|cue|bin|iso>; default: TM_DATA, then auto-detect
    bool colour16 = false;       // --16: use TMT16.DLL art and the VGA 16-colour palette
    bool opaque = false;         // --opaque: dumps keep the key colour opaque
    bool integerScale = false;   // --integer-scale
    bool verbose = false;        // --verbose
    std::string screenshotDir;   // --screenshot-dir <dir>
    int pmapId = 0;              // --dump-pmap <id> / --view [id]
    int firstId = 0, lastId = 0; // --dump-sheet <first> <last>
    std::string outPath;         // output PNG of the dump commands / --headless-frame
    std::string fbDumpPath;      // --fb-dump <png>: also write the whole 512x1055 frame buffer
    // Engine settings (area input_save): override tmport.ini.
    std::string configPath;      // --config <ini>: tmport.ini to use (default: next to the exe, else the pref dir)
    std::string inputMode;       // --input-mode lossless|first-event (empty: tmport.ini)
    bool tapLatch = false;       // --tap-latch
    std::vector<std::string> modDirs;  // --mods <dir> (repeatable): mod folders to load (area mods)
};

// Parses argv. Returns nullopt and sets error on invalid input.
std::optional<Options> parseArgs(int argc, char** argv, std::string& error);

// Help texts. toolsOverview(): one line per tool command, grouped, with the options common to the
// tools; inspectUsage(): the options of the data-inspection tools (tmport --help inspect).
std::string toolsOverview();
std::string inspectUsage();

}  // namespace tmport::cli
