#pragma once
// The original's table of data files (DS:0476, `g_fileTable`): 18 records of 22 bytes, name[13] and
// the floppy disk number, used by File_OpenByIndex (1460:06c8) to build every path. The index is
// how the original names a file (Scene_LoadPan(index), scene + 13 for the PAKs of scenes 3/4), so
// the engine looks files up through it. Spec: re/dos/docs/fmt_data.md §1.

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "dos/data/dos_exe.h"

namespace tmport::dos {

// File-table indices of V 1.0 (fmt_data.md §1). The names come from the EXE; FileTable::read checks
// that they match this numbering.
enum class FileIndex : int {
    MountainPan = 0,  // scene 0 (level 0)
    Mount2n3Pan = 1,  // scene 1
    Mount3Pan = 2,    // scene 2
    ClosePan = 3,     // scene 3, castle top
    ShackPan = 4,     // scene 4, treasure room
    HelpDat = 5,
    SpritesPak = 6,   // sprites of scenes 0..2
    AllPuzs = 7,
    PrgDict = 8,
    UsersDat = 9,
    SfxIbm = 10,
    TunesIbm = 11,
    SfxTan = 12,
    TunesTan = 13,
    OpeningPan = 14,  // title and sign-in backdrop
    BeginPak = 15,    // title, sign-in, menus
    ClosePak = 16,    // scene 3 (index = scene + 13, Scene_Update 1437:0034)
    ShackPak = 17,    // scene 4
};

struct FileTableEntry {
    std::string name;  // as stored (lower case in V 1.0), at most 12 characters
    uint8_t disk = 0;  // 1 or 2: only matters on floppies (Disk_PromptForDisk 150a:00f5)
};

class FileTable {
public:
    static constexpr int kCount = 18;

    // Reads DS:0476 and checks every record (a printable 8.3 name, disk 1 or 2, the V 1.0 numbering
    // of FileIndex, the all-zero record 18). Throws std::runtime_error naming the bad record.
    static FileTable read(const DosExe& exe);

    const FileTableEntry& entry(int index) const { return entries_.at(static_cast<size_t>(index)); }
    const FileTableEntry& entry(FileIndex index) const { return entry(static_cast<int>(index)); }
    const std::string& name(FileIndex index) const { return entry(index).name; }

    // Index of a file name (compared without regard to case), e.g. "SPRITES.PAK" -> 6.
    std::optional<int> indexOf(std::string_view name) const;

private:
    std::array<FileTableEntry, kCount> entries_;
};

}  // namespace tmport::dos
