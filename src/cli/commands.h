#pragma once
// The command-line tools of tmport: the dispatcher between the tool groups' parsers, --help,
// and the data-inspection tools (--info, --dump-pmap, --dump-sheet, --headless-frame, --view).

#include <filesystem>
#include <string>

#include "cli/options.h"

namespace tmport::cli {

// Entry point used by main() when argv is not a game invocation (app::isGameInvocation): runs the
// tool command, returns the process exit code (2: bad command line, 3: game data error).
int run(int argc, char** argv);
// tmport --help [topic]: empty = the game options and the tool overview; "all" = every option
// list; else one group (game, inspect, text, anim, screen, scene, room, audio, mods). Returns 0,
// or 2 for an unknown topic.
int printHelp(const std::string& topic);

int runInfo(const Options& o, const std::filesystem::path& dataPath);
int runDumpPmap(const Options& o, const std::filesystem::path& dataPath);
int runDumpSheet(const Options& o, const std::filesystem::path& dataPath);
int runHeadlessFrame(const Options& o, const std::filesystem::path& dataPath);
int runView(const Options& o, const std::filesystem::path& dataPath);

}  // namespace tmport::cli
