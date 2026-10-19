#include "dos/data/dos_files.h"

#include <stdexcept>
#include <utility>

#include "dos/data/help_file.h"
#include "dos/data/puzzles.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/log.h"
#include "vfs/directory_source.h"

namespace tmport::dos {

namespace {

uint16_t le16(std::span<const uint8_t> b, size_t at) { return static_cast<uint16_t>(b[at] | (b[at + 1] << 8)); }

[[noreturn]] void bad(std::string_view name, const std::string& why) {
    throw FormatError(toUpperAscii(name) + ": " + why);
}

// Buffers of the original that the files are read into (fmt_graphics.md §3.1, §5.1; fmt_sound.md §2.3).
constexpr size_t kPanMapBuffer = 0xA400;    // segment 2897 (DS:37b8)
constexpr size_t kPanTileBuffer = 0xE880;   // segment 32d7 (DS:37b4)
constexpr size_t kSpriteBank = 0x1C908;     // farmalloc in Game_Init 1460:0003 (DS:37bc)
constexpr size_t kSoundReadLimit = 9999;    // Sound_LoadData 1460:022a
constexpr size_t kSfxBuffer = 1712;         // segment 415f
constexpr size_t kTunesBuffer = 5056;       // segment 41ca

// PAN: u16 map size, width, height, tile size; map and tiles follow (fmt_graphics.md §3.1).
void checkPan(FileIndex index, std::string_view name, std::span<const uint8_t> b) {
    if (b.size() < 8) bad(name, "too short");
    const uint16_t mapSize = le16(b, 0), width = le16(b, 2), height = le16(b, 4), tileSize = le16(b, 6);
    if (8u + mapSize + tileSize != b.size()) bad(name, "header sizes do not add up to the file size");
    const bool opening = index == FileIndex::OpeningPan;
    if (width != (opening ? 40 : 16) || height != (opening ? 25 : 16)) bad(name, "unexpected picture size");
    if (mapSize > kPanMapBuffer || tileSize > kPanTileBuffer) bad(name, "larger than the original's buffers");
}

// PAK: u16 count, 3 header words, count paragraph offsets, FFFF FFFF (fmt_graphics.md §5.1).
void checkPak(std::string_view name, std::span<const uint8_t> b) {
    if (b.size() < 12) bad(name, "too short");
    const size_t count = le16(b, 0);
    const size_t trailer = 8 + 2 * count;
    if (count == 0 || trailer + 4 > b.size()) bad(name, "bad frame count");
    if (le16(b, trailer) != 0xFFFF || le16(b, trailer + 2) != 0xFFFF) bad(name, "no FFFF FFFF after the frame table");
    if (b.size() > kSpriteBank) bad(name, "larger than the sprite bank");
    size_t prev = (trailer + 4 + 15) / 16;
    for (size_t k = 0; k < count; ++k) {
        const size_t para = le16(b, 8 + 2 * k);
        if (para < prev || para * 16 + 16 > b.size()) bad(name, "frame " + std::to_string(k) + " outside the file");
        prev = para;
    }
}

// TUNES/SFX: u16 offset table whose byte length is its first word (fmt_sound.md §4.1).
void checkSound(FileIndex index, std::string_view name, std::span<const uint8_t> b) {
    const bool tan = index == FileIndex::SfxTan || index == FileIndex::TunesTan;
    const bool sfx = index == FileIndex::SfxIbm || index == FileIndex::SfxTan;
    if (b.size() < 2 || le16(b, 0) != (tan ? 0x230 : 0x96) || b.size() < le16(b, 0)) bad(name, "bad header");
    if (b.size() > kSoundReadLimit || b.size() > (sfx ? kSfxBuffer : kTunesBuffer))
        bad(name, "larger than the original's buffer");
}

}  // namespace

void checkDosDataFile(FileIndex index, std::string_view name, std::span<const uint8_t> b) {
    switch (index) {
    case FileIndex::MountainPan:
    case FileIndex::Mount2n3Pan:
    case FileIndex::Mount3Pan:
    case FileIndex::ClosePan:
    case FileIndex::ShackPan:
    case FileIndex::OpeningPan: checkPan(index, name, b); break;
    case FileIndex::SpritesPak:
    case FileIndex::BeginPak:
    case FileIndex::ClosePak:
    case FileIndex::ShackPak: checkPak(name, b); break;
    case FileIndex::SfxIbm:
    case FileIndex::TunesIbm:
    case FileIndex::SfxTan:
    case FileIndex::TunesTan: checkSound(index, name, b); break;
    case FileIndex::HelpDat: (void)HelpFile::parse(b); break;
    case FileIndex::PrgDict: (void)Dictionary::parse(b); break;
    case FileIndex::AllPuzs: (void)PuzzleFile::parse(b); break;
    case FileIndex::UsersDat:
        if (b.size() != DosFiles::kUsersDatSize) bad(name, "is not " + std::to_string(DosFiles::kUsersDatSize) + " bytes");
        break;
    }
}

void checkPuzzleWindows(const DosExe& exe, std::span<const uint8_t> allPuzs) {
    // The quiz reads every tier's texts with one 0x7B00-byte read (Quiz_LoadQuestions 46ce:09ab).
    const auto puzzles = PuzzleFile::parse(allPuzs);
    const auto tiers = QuizTiers::read(exe, puzzles);
    for (const int tier : {0, 1, 2, 3, 5}) {
        const auto [first, end] = tiers.range(tier);
        for (int q = first; q < end; ++q)
            if (!puzzles.fitsWindow(first, q)) bad("ALL_PUZS", "question " + std::to_string(q) + " outside its tier's read");
    }
}

namespace {

std::shared_ptr<const DosOverrides>& overridesSlot() {
    static std::shared_ptr<const DosOverrides> slot;
    return slot;
}

}  // namespace

void setDosOverrides(std::shared_ptr<const DosOverrides> overrides) { overridesSlot() = std::move(overrides); }

const std::shared_ptr<const DosOverrides>& dosOverrides() { return overridesSlot(); }

DosFiles::DosFiles(std::filesystem::path folder, DosExe exe, FileTable table, std::vector<uint8_t> exeFile)
    : folder_(std::move(folder)), exe_(std::move(exe)), table_(std::move(table)), exeFile_(std::move(exeFile)) {}

bool DosFiles::looksLikeDosFolder(const std::filesystem::path& folder) {
    const vfs::DirectorySource dir(folder);
    return dir.resolve(kExeName, false) && dir.resolve("SPRITES.PAK", false) && dir.resolve("ALL_PUZS", false);
}

DosFiles DosFiles::open(const std::filesystem::path& folder) { return open(folder, dosOverrides().get()); }

DosFiles DosFiles::open(const std::filesystem::path& folder, const DosOverrides* overrides) {
    const vfs::DirectorySource dir(folder);
    const auto read = [&](std::string_view name) -> std::optional<std::vector<uint8_t>> {
        const auto p = dir.resolve(name, false);
        if (!p) return std::nullopt;
        auto bytes = readWholeFile(*p);
        if (!bytes) bad(name, "cannot be read");
        return bytes;
    };
    const auto missing = [&](std::string_view name) -> MissingFileError {
        return MissingFileError(toUpperAscii(name), "The DOS game folder " + pathToUtf8(folder) + " has no " +
                                                        toUpperAscii(name) + " (copy all files of Treasure Mountain! 1990)");
    };

    auto exeFile = read(kExeName);
    if (!exeFile) throw missing(kExeName);
    DosExe exe;
    FileTable table;
    try {
        exe = DosExe::load(*exeFile);
        table = FileTable::read(exe);
    } catch (const std::runtime_error& e) {
        bad(kExeName, e.what());
    }

    DosFiles files(folder, std::move(exe), std::move(table), std::move(*exeFile));
    for (int i = 0; i < FileTable::kCount; ++i) {
        const auto index = static_cast<FileIndex>(i);
        const std::string& name = files.table_.entry(i).name;
        auto bytes = read(name);
        if (index == FileIndex::UsersDat) {
            if (!bytes) continue;  // optional: only imported
            if (bytes->size() != kUsersDatSize) {
                log::warn("{}: {} bytes instead of {}, ignored", toUpperAscii(name), bytes->size(), kUsersDatSize);
                continue;
            }
        }
        if (!bytes) throw missing(name);
        checkDosDataFile(index, name, *bytes);
        files.files_[static_cast<size_t>(i)] = std::move(*bytes);
    }
    checkPuzzleWindows(files.exe_, files.bytes(FileIndex::AllPuzs));

    // The mod layer works on the checked originals; what it hands back passed the same checks.
    if (overrides) overrides->apply(files.exe_, files.files_);
    return files;
}

std::span<const uint8_t> DosFiles::bytes(FileIndex index) const {
    const auto& f = files_.at(static_cast<size_t>(index));
    if (index == FileIndex::UsersDat && f.empty())
        throw MissingFileError(toUpperAscii(table_.name(index)), "no USERS.DAT in " + pathToUtf8(folder_));
    return f;
}

std::span<const uint8_t> DosFiles::bytes(std::string_view name) const {
    if (iequalsAscii(name, kExeName)) return exeFile_;
    const auto i = table_.indexOf(name);
    if (!i) throw std::out_of_range("not a file of the DOS edition: " + std::string(name));
    return bytes(static_cast<FileIndex>(*i));
}

std::optional<std::filesystem::path> DosFiles::pathOf(std::string_view name) const {
    return vfs::DirectorySource(folder_).resolve(name, false);
}

}  // namespace tmport::dos
