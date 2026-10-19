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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dos/data/dos_exe.h"
#include "dos/data/file_table.h"

namespace tmport::dos {

class DosFiles {
public:
    static constexpr std::string_view kExeName = "SST.EXE";
    static constexpr size_t kUsersDatSize = 3 + 9 * 360;  // fmt_data.md §4

    // Loads SST.EXE (DosExe: V 1.0 only) and every file of its file table from `folder`. Throws
    // tmport::MissingFileError (file() = the missing name) or tmport::FormatError (message starts
    // with the file name). A USERS.DAT of the wrong size is ignored with a warning.
    static DosFiles open(const std::filesystem::path& folder);

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

private:
    DosFiles(std::filesystem::path folder, DosExe exe, FileTable table, std::vector<uint8_t> exeFile);

    std::filesystem::path folder_;
    DosExe exe_;
    FileTable table_;
    std::vector<uint8_t> exeFile_;
    std::array<std::vector<uint8_t>, FileTable::kCount> files_;
};

// The size/header checks of DosFiles::open for one data file (exposed for the tests). Throws
// tmport::FormatError naming the file.
void checkDosDataFile(FileIndex index, std::string_view name, std::span<const uint8_t> bytes);

}  // namespace tmport::dos
