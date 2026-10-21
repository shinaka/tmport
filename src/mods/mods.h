#pragma once
// The mod layer's entry points (docs/MODDING.md, re/docs/PORTING_NOTES.md §7).
//
// Mod folders come from the [mods] section of tmport.ini:
//
//   [mods]
//   enabled = true                 ; false: ignore all mods
//   paths = mods ; D:\MoreMods     ; ';'-separated, relative to the ini's folder; later folders
//                                  ; override earlier ones
//
// Without `paths` the folders are <preference dir>/mods, then <executable dir>/mods. Folders that
// do not exist are skipped.

#include <filesystem>
#include <string>
#include <vector>

#include "res/game_files.h"
#include "save/settings.h"

namespace tmport::mods {

// The mod folders configured by an ini file (nullptr = no file: the defaults).
std::vector<std::filesystem::path> modDirsFromIni(const mischief::save::IniFile* ini, const std::filesystem::path& iniDir,
                                                  const std::filesystem::path& prefDir, const std::filesystem::path& exeDir);
// The same for the game's tmport.ini (paths.iniPath).
std::vector<std::filesystem::path> configuredModDirs(const save::Paths& paths);

struct ModsReport {
    std::vector<std::filesystem::path> folders;  // existing folders, lowest precedence first
    size_t resourceOverrides = 0;                // (type, id) pairs served by a mod
    size_t tableFiles = 0;                       // table files applied
    size_t tunes = 0;                            // MIDI files installed
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

// Scans `dirs` (lowest precedence first), applies the table overrides to files.exe, installs the
// resource overrides in files.res and the MIDI overrides in front of files.source. Call right
// after GameFiles::open, before anything reads tables or resources. Problems are logged and
// returned; they never stop the game (a broken override falls back to the original data).
ModsReport applyMods(GameFiles& files, const std::vector<std::filesystem::path>& dirs);

// Converts and checks every file of every installed folder now (tmport --check-mods); appends
// failures to report.errors and validation warnings to report.warnings.
void validateInstalledMods(const GameFiles& files, ModsReport& report);

// Compares what the installed mods serve with the game's own data (tmport --check-mods
// --compare): every winning resource override (both colour modes), every MIDI override and the
// EXE tables. An exported mod kit installed as a mod must compare identical everywhere.
struct ModComparison {
    size_t identical = 0;
    size_t different = 0;
    size_t failed = 0;                  // overrides that did not convert
    std::vector<std::string> differing; // "PMAP 1400 (C:\...\1400.png)"
    bool tablesIdentical = true;
};
ModComparison compareWithOriginals(const GameFiles& files);

}  // namespace tmport::mods
