#include "dos/data/file_table.h"

#include <stdexcept>

#include "dos/data/dgroup_offsets.h"
#include "mischief/util/file_util.h"

namespace tmport::dos {

namespace {

// Upper-case names in FileIndex order; the EXE stores them in lower case.
constexpr std::array<std::string_view, FileTable::kCount> kExpectedNames = {
    "MOUNTAIN.PAN", "MOUNT2N3.PAN", "MOUNT3.PAN", "CLOSE.PAN", "SHACK.PAN", "HELP.DAT",
    "SPRITES.PAK",  "ALL_PUZS",     "PRG_DICT",   "USERS.DAT", "SFX.IBM",   "TUNES.IBM",
    "SFX.TAN",      "TUNES.TAN",    "OPENING.PAN", "BEGIN.PAK", "CLOSE.PAK", "SHACK.PAK",
};

[[noreturn]] void bad(int index, const std::string& why) {
    throw std::runtime_error("SST.EXE file table (DS:0476) record " + std::to_string(index) + ": " + why);
}

}  // namespace

// DS:0476 g_fileTable, read by File_OpenByIndex 1460:06c8 (name at +0, disk at +0x0D).
FileTable FileTable::read(const DosExe& exe) {
    FileTable t;
    for (int i = 0; i <= kCount; ++i) {
        const auto rec = exe.at(DosExe::kDgroupSeg, static_cast<uint16_t>(dg::kFileTable + i * dg::filerec::kSize));
        if (rec.size() < dg::filerec::kSize) bad(i, "outside the image");
        std::string name;
        for (size_t k = 0; k < dg::filerec::kNameSize && rec[k] != 0; ++k) {
            const uint8_t c = rec[k];
            if (c <= 0x20 || c >= 0x7F) bad(i, "unprintable name");
            name.push_back(static_cast<char>(c));
        }
        const uint8_t disk = rec[dg::filerec::kDisk];
        if (i == kCount) {  // the record after the last file is all zero
            for (size_t k = 0; k < dg::filerec::kSize; ++k)
                if (rec[k] != 0) bad(i, "expected the all-zero end record");
            break;
        }
        if (name.empty() || name.size() > 12) bad(i, "bad name length");
        if (disk != 1 && disk != 2) bad(i, "disk number " + std::to_string(disk));
        if (!mischief::iequalsAscii(name, kExpectedNames[static_cast<size_t>(i)]))
            bad(i, "expected " + std::string(kExpectedNames[static_cast<size_t>(i)]));
        t.entries_[static_cast<size_t>(i)] = {std::move(name), disk};
    }
    return t;
}

std::optional<int> FileTable::indexOf(std::string_view name) const {
    for (int i = 0; i < kCount; ++i)
        if (mischief::iequalsAscii(entries_[static_cast<size_t>(i)].name, name)) return i;
    return std::nullopt;
}

}  // namespace tmport::dos
