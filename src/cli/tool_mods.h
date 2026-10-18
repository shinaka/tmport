#pragma once
// `--mods <dir>` for the command-line tools (docs/MODDING.md "Tools").
//
// Every tool that opens the game files itself (--info and the PMAP dumps, --headless-frame,
// --view, --render-dialog / --render-help / --render-text / --render-font / --text-metrics,
// --render-dam / --dam-info / --dam-check, --screen, --scene, --room-run) accepts `--mods <dir>`,
// repeatable, and calls applyToolMods right after GameFiles::open. The tools load only the
// folders named on their command line, never the [mods] folders of tmport.ini (the game loads
// those; `--check-mods` without folders checks them).

#include <string>
#include <vector>

#include "res/game_files.h"

namespace tmport::cli {

// Applies the mod folders (lowest precedence first, as given on the command line) to files with
// mods::applyMods. A folder that does not exist is reported on stderr (a tool user named it
// explicitly); problems inside the mods are logged and fall back to the original data, as in the
// game. Does nothing when dirs is empty. Returns the number of folders loaded.
size_t applyToolMods(GameFiles& files, const std::vector<std::string>& dirs);

}  // namespace tmport::cli
