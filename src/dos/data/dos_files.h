#pragma once
// DosFiles: the user's 1990 DOS game folder (TM-DOS: SST.EXE and its data files), opened and checked
// once at start-up. File names are looked up without regard to case, every file named by the
// original's file table (DS:0476) must be present and pass a size/header check, and the error names
// the missing or broken file. USERS.DAT is optional (it is only imported). Other modules take the
// bytes of a file from here by name or by file-table index.
// Spec: re/dos/docs/PORTING_NOTES.md §1, fmt_data.md §1, fmt_graphics.md §3.1/§5.1, fmt_sound.md §2.3.

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dos/data/dos_exe.h"
#include "dos/data/file_table.h"

namespace tmport::dos {

// The data files by file-table index, as DosFiles holds them (USERS.DAT may be empty).
using DosFileSet = std::array<std::vector<uint8_t>, FileTable::kCount>;

// The hook of the DOS mod layer (dos/mods/dos_mods.h, docs/MODDING.md "The 1990 DOS edition"):
// replacement data files and changes to SST.EXE's DGROUP that DosFiles::open applies after it has
// loaded and checked the originals, before anything reads them.
class DosOverrides {
public:
    virtual ~DosOverrides() = default;
    // `exe` and `files` hold the originals; an implementation patches the DGROUP and replaces files
    // in place. Every replaced file must pass checkDosDataFile (ALL_PUZS also checkPuzzleWindows): an
    // implementation keeps the original of a file whose override fails and reports why itself. It
    // never throws for a broken mod (a broken mod never stops the game).
    virtual void apply(DosExe& exe, DosFileSet& files) const = 0;
};

// The overrides DosFiles::open(folder) applies: none until the start-up installs the mod folders
// (dos::installDosMods). Not thread-safe; set before any DosFiles is opened.
void setDosOverrides(std::shared_ptr<const DosOverrides> overrides);
const std::shared_ptr<const DosOverrides>& dosOverrides();

class DosFiles {
public:
    static constexpr std::string_view kExeName = "SST.EXE";
    static constexpr size_t kUsersDatSize = 3 + 9 * 360;  // fmt_data.md §4

    // Loads SST.EXE (DosExe: V 1.0 only) and every file of its file table from `folder`. Throws
    // tmport::MissingFileError (file() = the missing name) or tmport::FormatError (message starts
    // with the file name). A USERS.DAT of the wrong size is ignored with a warning. The installed
    // mod overrides (dosOverrides()) are applied to the checked originals.
    static DosFiles open(const std::filesystem::path& folder);
    // The same with explicit overrides (nullptr: the original files only).
    static DosFiles open(const std::filesystem::path& folder, const DosOverrides* overrides);

    // True when `folder` holds SST.EXE, SPRITES.PAK and ALL_PUZS (the DOS edition's signature,
    // SCOPING.md §5.1); nothing is read or validated.
    static bool looksLikeDosFolder(const std::filesystem::path& folder);

    const std::filesystem::path& folder() const { return folder_; }
    const DosExe& exe() const { return exe_; }
    const FileTable& fileTable() const { return table_; }

    // Bytes of a file by its file-table index or by name ("SST.EXE" gives the packed EXE file).
    // Throws tmport::MissingFileError for USERS.DAT when the folder has none (see hasUsersDat) and
    // std::out_of_range for a name that is not a game file.
    std::span<const uint8_t> bytes(FileIndex index) const;
    std::span<const uint8_t> bytes(std::string_view name) const;

    bool hasUsersDat() const { return !files_[static_cast<size_t>(FileIndex::UsersDat)].empty(); }
    // The file's path on disk with its real spelling (e.g. for exporting USERS.DAT back), nullopt if
    // it does not exist.
    std::optional<std::filesystem::path> pathOf(std::string_view name) const;

    // Every data file by file-table index (USERS.DAT empty when the folder has none).
    const DosFileSet& fileSet() const { return files_; }

private:
    DosFiles(std::filesystem::path folder, DosExe exe, FileTable table, std::vector<uint8_t> exeFile);

    std::filesystem::path folder_;
    DosExe exe_;
    FileTable table_;
    std::vector<uint8_t> exeFile_;
    DosFileSet files_;
};

// The size/header checks of DosFiles::open for one data file (exposed for the tests and the mod
// layer, whose replacements pass the same checks). Throws tmport::FormatError naming the file.
void checkDosDataFile(FileIndex index, std::string_view name, std::span<const uint8_t> bytes);

// The cross-file check of DosFiles::open: the questions of every quiz tier (DS:5fd2, tier 5 from the
// file length) lie inside the one 0x7B00-byte read Quiz_LoadQuestions 46ce:09ab makes from the
// tier's first question. Throws tmport::FormatError naming ALL_PUZS.
void checkPuzzleWindows(const DosExe& exe, std::span<const uint8_t> allPuzs);

}  // namespace tmport::dos
