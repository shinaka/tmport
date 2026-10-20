#pragma once
// The command-line helpers every G&G tool shares (the <area>Cli functions of ggport-cli, gg/app/
// cli_dispatch.h): option lookup in the argument list, the game folder (--data, else GG_DATA, else
// next to the program) and the guard that keeps tool output out of the user's game folder.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gg/data/gg_files.h"

namespace tmport::gg::cli {

// The value after the first `name` in `args` (nullopt when absent or last).
std::optional<std::string> optionValue(const std::vector<std::string>& args, std::string_view name);
// The values after every `name`.
std::vector<std::string> optionValues(const std::vector<std::string>& args, std::string_view name);
bool hasFlag(const std::vector<std::string>& args, std::string_view name);

// The folder of the running program (SDL_GetBasePath), empty when unknown.
std::filesystem::path programDir();
// The game folder of a tool: --data DIR, else GG_DATA, else next to the program (detectGgFolder);
// throws std::runtime_error when none is found and GgFiles' errors for a broken one.
GgFiles openFolder(const std::vector<std::string>& args);
// True when `file` is `folder` or inside it (case-insensitive, after resolving . and ..).
bool insideFolder(const std::filesystem::path& file, const std::filesystem::path& folder);

}  // namespace tmport::gg::cli
