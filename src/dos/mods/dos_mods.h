#pragma once
// The mod layer of the 1990 DOS edition (docs/MODDING.md, "The 1990 DOS edition"): mod folders
// (dos/mods/dos_mod_folder.h) are converted back to the original binary formats and applied to
// SST.EXE's DGROUP and the data files when DosFiles::open loads them (the DosOverrides hook of
// dos/data/dos_files.h), so the game code cannot tell a modded file from an original.
//
// Order: FILES/ (whole files), EXE/palettes.json, the PAK frames and hotspots, the PAN pictures,
// SOUND/, TEXT/ (PRG_DICT, ALL_PUZS, HELP.DAT), EXE/strings.json. Folders are applied lowest
// precedence first; for one frame, picture or file the last folder wins, strings.json and
// palettes.json merge entry by entry. Every replaced file passes the checks of an original
// (checkDosDataFile, checkPuzzleWindows) plus the layout rules of its format; a mod file that breaks
// one is reported and the original data stays. A broken mod never stops the game.

#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "dos/data/dos_files.h"
#include "dos/mods/dos_mod_folder.h"

namespace tmport::dos {

struct DosModsReport {
    std::vector<std::filesystem::path> folders;  // existing folders, lowest precedence first
    size_t modFiles = 0;                          // files in them
    size_t applied = 0;                           // mod files converted and accepted
    std::vector<std::string> changedFiles;        // data files whose bytes differ ("SPRITES.PAK")
    bool exeChanged = false;                      // the DGROUP differs
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

// Applies folders (lowest precedence first) to an EXE and its data files (the originals, as
// DosFiles::open has checked them). Never throws for a broken mod.
DosModsReport applyDosModFolders(const std::vector<DosModFolder>& folders, DosExe& exe, DosFileSet& files);

// The installed mod folders as DosFiles::open's overrides. The result of a conversion is kept for
// the same original files (the start-up opens the folder twice); the report is logged once.
class DosModSet : public DosOverrides {
public:
    explicit DosModSet(std::vector<DosModFolder> folders);
    void apply(DosExe& exe, DosFileSet& files) const override;
    const std::vector<DosModFolder>& folders() const { return folders_; }
    // The report of the last conversion (empty before the first DosFiles::open).
    DosModsReport lastReport() const;

private:
    std::vector<DosModFolder> folders_;
    mutable std::mutex mutex_;
    mutable uint64_t cacheKey_ = 0;
    mutable bool cached_ = false;
    mutable std::unique_ptr<DosExe> cachedExe_;
    mutable DosFileSet cachedFiles_;
    mutable DosModsReport report_;
};

// Scans `dirs` (lowest precedence first; missing folders are skipped) and installs the existing ones
// for DosFiles::open (setDosOverrides; nothing is installed without one). Returns the scan's report
// (folders, layout warnings, broken mod.json).
DosModsReport installDosMods(const std::vector<std::filesystem::path>& dirs);
// Removes the installed DOS mods (tests).
void uninstallDosMods();

}  // namespace tmport::dos
