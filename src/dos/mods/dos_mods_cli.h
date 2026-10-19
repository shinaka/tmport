#pragma once
// The mod tools for the 1990 DOS edition (docs/MODDING.md, "The 1990 DOS edition"), reached through
// mods/mods_cli.h when --edition dos is given or --data names a DOS folder:
//   tmport --export-mod-kit <dir> --edition dos [--data <DOS folder>] [--force] [--no-verify]
//   tmport --check-mods [<dir> ...] --edition dos [--data <DOS folder>] [--compare] [--config <ini>]

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "save/settings.h"

namespace tmport::dos {

struct DosModsCommand {
    bool exportKit = false;                       // else --check-mods
    std::string outDir;                           // --export-mod-kit <dir>
    std::string dataArg;                          // --data (empty: TM_DATA_DOS, [dos] data, auto-detection)
    std::vector<std::filesystem::path> modDirs;   // --check-mods / --mods folders (empty: the configured ones)
    bool force = false, verify = true, compare = false;
    save::Paths paths;
};

// Runs the command; prints to stdout, returns the exit code (0 OK, 1 problems, 2 usage, 3 data).
int runDosModsCommand(const DosModsCommand& c);

// The DOS folder: --data (a folder holding SST.EXE or one of its subfolders), else TM_DATA_DOS, else
// [dos] data of the paths' tmport.ini, else the auto-detection of the start-up.
std::optional<std::filesystem::path> dosModsDataPath(const std::string& dataArg, const save::Paths& paths,
                                                     const std::filesystem::path& exeDir);

}  // namespace tmport::dos
