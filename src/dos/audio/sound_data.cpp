#include "dos/audio/sound_data.h"

#include <algorithm>
#include <format>

#include "dos/data/dos_exe.h"
#include "util/error.h"
#include "vfs/data_source.h"

namespace tmport::dos {

namespace {

constexpr size_t kDgroupSize = 0x10000;
constexpr size_t kReadLimit = 9999;  // File_Read limit of Sound_LoadData (1460:07b4)

uint16_t rd16(std::span<const uint8_t> b, size_t at) { return static_cast<uint16_t>(b[at] | (b[at + 1] << 8)); }

[[noreturn]] void badTables(const std::string& why) {
    throw FormatError("SST.EXE: the sound driver tables are not those of V 1.0 (" + why + ")");
}

void checkTables(const SoundTables& t) {
    if (t.ds16(SoundTables::kDsPeriodTables) != SoundTables::kDsSpeakerPeriods ||
        t.ds16(SoundTables::kDsPeriodTables + 2) != SoundTables::kDsPsgPeriods)
        badTables("period table pointers at DS:6568");
    for (int i = 1; i < 12; ++i) {
        if (t.speakerPeriod(i) >= t.speakerPeriod(i - 1)) badTables("PIT periods at DS:6538 not descending");
        if (t.psgPeriod(i) >= t.psgPeriod(i - 1)) badTables("PSG periods at DS:6550 not descending");
    }
    // Every envelope is a 0xFF-terminated string inside the initialised data (DS:65c3..6bbd).
    for (int e = 0; e < SoundTables::kEnvelopeCount; ++e) {
        const uint16_t p = t.envelope(e);
        if (p < 0x6000 || p >= 0x7000) badTables(std::format("envelope {} at DS:{:04x}", e, p));
        int n = 0;
        while (t.ds8(static_cast<uint16_t>(p + n)) != 0xFF)
            if (++n > 1024) badTables(std::format("envelope {} is not terminated", e));
    }
}

}  // namespace

std::string_view soundCardName(SoundCard d) {
    switch (d) {
        case SoundCard::Speaker: return "speaker";
        case SoundCard::Tandy: return "tandy";
        case SoundCard::AdLib: return "adlib";
    }
    return "?";
}

// ------------------------------------------------------------------------------------ SoundFile

SoundFile SoundFile::parse(std::span<const uint8_t> file, int voicesPerEntry, int entriesNeeded, size_t bufferSize,
                           std::string_view name) {
    const auto fail = [&](const std::string& why) -> FormatError {
        return FormatError(std::format("{}: {}", name, why));
    };
    if (voicesPerEntry != 1 && voicesPerEntry != 4) throw fail("bad voice count");
    const uint16_t expectHeader = voicesPerEntry == 1 ? 0x96 : 0x230;  // 75 or 70 x 4 slots
    file = file.first(std::min(file.size(), kReadLimit));
    if (file.size() > bufferSize) throw fail(std::format("{} bytes do not fit the driver's {}-byte buffer", file.size(), bufferSize));
    if (file.size() < expectHeader + 2u) throw fail("file is too short");
    if (rd16(file, 0) != expectHeader) throw fail(std::format("header length {:#x}, expected {:#x}", rd16(file, 0), expectHeader));

    SoundFile f;
    f.name_ = std::string(name);
    f.buffer_.assign(bufferSize, 0);  // the static buffer is zero in the EXE image
    std::copy(file.begin(), file.end(), f.buffer_.begin());
    f.fileSize_ = file.size();
    f.header_ = expectHeader;
    f.voices_ = voicesPerEntry;

    // The leading run of plausible entries (tools/dos/dos_sound.py parse_file).
    const int slots = expectHeader / 2;
    uint16_t last = 0;
    for (int e = 0; (e + 1) * voicesPerEntry <= slots; ++e) {
        bool ok = true;
        for (int v = 0; v < voicesPerEntry && ok; ++v) {
            const uint16_t o = f.headerSlot(e * voicesPerEntry + v);
            ok = o >= expectHeader && o < file.size() && (v == 0 ? o >= last : o >= f.headerSlot(e * voicesPerEntry + v - 1));
        }
        if (!ok) break;
        last = f.headerSlot(e * voicesPerEntry + voicesPerEntry - 1);
        f.entries_ = e + 1;
    }
    if (f.entries_ < entriesNeeded) throw fail(std::format("{} valid entries, the game needs {}", f.entries_, entriesNeeded));
    return f;
}

uint16_t SoundFile::headerSlot(int slot) const {
    const uint32_t at = 2u * static_cast<uint32_t>(slot);
    return static_cast<uint16_t>(byteAt(at) | (byteAt(at + 1) << 8));
}

// ---------------------------------------------------------------------------------- SoundTables

SoundTables SoundTables::fromExe(const DosExe& exe) {
    const auto ds = exe.dgroup();
    std::vector<uint8_t> copy(kDgroupSize, 0);  // past the initialised data: BSS, zero at start-up
    std::copy_n(ds.begin(), std::min(ds.size(), kDgroupSize), copy.begin());
    return fromDgroup(std::move(copy));
}

SoundTables SoundTables::fromDgroup(std::vector<uint8_t> dgroup) {
    if (dgroup.size() != kDgroupSize) badTables("DGROUP image is not 64 KB");
    SoundTables t;
    t.dgroup = std::move(dgroup);
    checkTables(t);
    return t;
}

// ------------------------------------------------------------------------------------ SoundBank

std::string_view sfxFileName(SoundCard d) { return d == SoundCard::Tandy ? "SFX.TAN" : "SFX.IBM"; }
std::string_view musicFileName(SoundCard d) { return d == SoundCard::Speaker ? "TUNES.IBM" : "TUNES.TAN"; }
int sfxVoices(SoundCard d) { return d == SoundCard::Tandy ? 4 : 1; }
int musicVoices(SoundCard d) { return d == SoundCard::Speaker ? 1 : 4; }

SoundBank SoundBank::fromBytes(SoundCard device, std::span<const uint8_t> sfxFile, std::span<const uint8_t> musicFile,
                               std::shared_ptr<const SoundTables> tables) {
    SoundBank b;
    b.device = device;
    b.sfx = SoundFile::parse(sfxFile, sfxVoices(device), SoundFile::kSfxCount, SoundFile::kSfxBufferSize, sfxFileName(device));
    b.music = SoundFile::parse(musicFile, musicVoices(device), SoundFile::kTuneCount, SoundFile::kMusicBufferSize,
                               musicFileName(device));
    b.tables = std::move(tables);
    return b;
}

// 1460:022a Sound_LoadData
SoundBank SoundBank::load(const vfs::DataSource& dosFolder, SoundCard device, std::shared_ptr<const SoundTables> tables) {
    const auto read = [&](std::string_view name) {
        auto f = dosFolder.open(name);
        if (!f) throw MissingFileError(std::string(name), std::format("{} is missing from the DOS game folder", name));
        return f->readRange(0, kReadLimit);
    };
    const auto sfx = read(sfxFileName(device));
    const auto music = read(musicFileName(device));
    return fromBytes(device, sfx, music, std::move(tables));
}

}  // namespace tmport::dos
