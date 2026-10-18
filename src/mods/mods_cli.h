#pragma once
// Command-line tools of the mod layer:
//   tmport --export-mod-kit <dir> [--data <path>] [--force] [--no-verify] [--verbose]
//   tmport --check-mods [<dir> ...] [--compare] [--data <path>] [--config <ini>] [--16] [--verbose]

#include <optional>
#include <string>

namespace tmport::mods {

// Runs a mods command if argv holds one; nullopt otherwise (the caller tries other parsers).
std::optional<int> runModsCommand(int argc, char** argv);
std::string modsUsage();

}  // namespace tmport::mods
