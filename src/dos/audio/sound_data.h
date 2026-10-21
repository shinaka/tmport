#pragma once
// The sound data of the 1990 DOS edition (re/dos/docs/fmt_sound.md §2-§5): the effect and music
// files as the driver sees them in its two static buffers, and the driver tables in SST.EXE's
// DGROUP (PIT and PSG period tables, the 36 Tandy volume envelopes).
//
//   device   effects buffer 415f:0000   music buffer 41ca:0000   (Sound_LoadData 1460:022a)
//   speaker  SFX.IBM                    TUNES.IBM
//   Tandy    SFX.TAN                    TUNES.TAN
//   AdLib    SFX.IBM (on the speaker)   TUNES.TAN (on the OPL2)
//
// Nothing is copied into the engine: the bytes come from the user's TM-DOS folder at run time.

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dos/data/dgroup_offsets.h"

namespace mischief::vfs {
class DataSource;
}

namespace tmport::dos {

class DosExe;

// The sound hardware as the driver sees it: g_soundDevice DS:63bc. The PS/1 audio card (3) is
// rewritten to Tandy by Sound_Init 491b:06bd and is not a separate value here. (The engine option
// dos::SoundDevice of edition/dos_options.h has the same values 0..2 plus Off.)
enum class SoundCard : uint8_t { Speaker = 0, Tandy = 1, AdLib = 2 };

std::string_view soundCardName(SoundCard d);  // "speaker", "tandy", "adlib"

// One effect or music file in its driver buffer. Stream offsets are buffer (= file) offsets, as the
// driver stores them (491b:0727 adds the buffer offset DS:63a4/63a8, which is 0).
class SoundFile {
public:
    static constexpr size_t kSfxBufferSize = 1712;    // FARDATA_415f
    static constexpr size_t kMusicBufferSize = 5056;  // FARDATA_41ca
    static constexpr int kSfxCount = 12;              // the game asks for effects 0..11
    static constexpr int kTuneCount = 3;              // and tunes 0..2 (fmt_sound.md §4.1)

    // Validates the header (first word 0x96 for 1 voice per entry, 0x230 for 4) and that the first
    // `entriesNeeded` entries point into the file; throws mischief::FormatError naming `name`.
    static SoundFile parse(std::span<const uint8_t> file, int voicesPerEntry, int entriesNeeded, size_t bufferSize,
                           std::string_view name);

    const std::string& name() const { return name_; }
    int voicesPerEntry() const { return voices_; }
    uint16_t headerBytes() const { return header_; }
    size_t fileSize() const { return fileSize_; }
    // Leading run of entries whose offsets are ascending and inside the file (the count is not
    // stored; later header slots hold stale data).
    int entryCount() const { return entries_; }
    // Header slot `slot` (u16 at byte 2*slot), read like the driver: entry e voice v is slot
    // e * voicesPerEntry + v (Sfx_Play 491b:0727, Music_Play 491b:07ed).
    uint16_t headerSlot(int slot) const;
    // Byte at a buffer offset: the file, then the zero-filled rest of the static buffer; past the
    // buffer 0xFF (end of stream), so a stream that runs off its buffer stops instead of reading on.
    uint8_t byteAt(uint32_t off) const { return off < buffer_.size() ? buffer_[off] : uint8_t{0xFF}; }
    std::span<const uint8_t> fileBytes() const { return std::span<const uint8_t>(buffer_).first(fileSize_); }

private:
    std::string name_;
    std::vector<uint8_t> buffer_;
    size_t fileSize_ = 0;
    uint16_t header_ = 0;
    int voices_ = 1;
    int entries_ = 0;
};

// The driver's tables in DGROUP (fmt_sound.md §4.2, §5.4).
struct SoundTables {
    static constexpr uint16_t kDsSpeakerPeriods = dg::kSpeakerPeriods;  // u16[12] PIT divisors, C1..B1
    static constexpr uint16_t kDsPsgPeriods = dg::kPsgPeriods;      // u16[12] SN76496 periods
    static constexpr uint16_t kDsPeriodTables = dg::kPeriodTables;    // u16[2] near pointers to the two tables, by device
    static constexpr uint16_t kDsEnvelopes = dg::kEnvelopes;       // u16[36] near pointers, 0xFF-terminated strings
    static constexpr int kEnvelopeCount = 36;

    // Copies DGROUP (DS:0000..FFFF, zero past the initialised data) and checks the tables; throws
    // mischief::FormatError for an SST.EXE whose sound tables are not where V 1.0 has them.
    static SoundTables fromExe(const DosExe& exe);
    // For tests: tables built from a 64 KB DGROUP image.
    static SoundTables fromDgroup(std::vector<uint8_t> dgroup);

    uint8_t ds8(uint16_t off) const { return dgroup[off]; }
    uint16_t ds16(uint16_t off) const {
        return static_cast<uint16_t>(dgroup[off] | (dgroup[static_cast<uint16_t>(off + 1)] << 8));
    }
    uint16_t speakerPeriod(int i) const { return ds16(static_cast<uint16_t>(kDsSpeakerPeriods + 2 * i)); }
    uint16_t psgPeriod(int i) const { return ds16(static_cast<uint16_t>(kDsPsgPeriods + 2 * i)); }
    uint16_t envelope(int i) const { return ds16(static_cast<uint16_t>(kDsEnvelopes + 2 * i)); }

    // The whole 64 KB data segment: envelope bytes are read through it at the driver's near
    // pointers, exactly as Tandy_UpdateVolumes 491b:03d2 does (also for a voice that never set one).
    std::vector<uint8_t> dgroup;
};

// Everything the driver needs for one device.
struct SoundBank {
    SoundCard device = SoundCard::Speaker;
    SoundFile sfx;    // effects buffer (DS:63a4/63a6)
    SoundFile music;  // music buffer (DS:63a8/63aa)
    std::shared_ptr<const SoundTables> tables;

    // The files of 1460:022a for `device`, read from the DOS folder (names looked up without regard
    // to case). Throws mischief::MissingFileError / FormatError.
    static SoundBank load(const mischief::vfs::DataSource& dosFolder, SoundCard device, std::shared_ptr<const SoundTables> tables);
    static SoundBank fromBytes(SoundCard device, std::span<const uint8_t> sfxFile, std::span<const uint8_t> musicFile,
                               std::shared_ptr<const SoundTables> tables);
};

// File names per device (game file table DS:0476 indices 10..13).
std::string_view sfxFileName(SoundCard d);
std::string_view musicFileName(SoundCard d);
// Voices per entry of the effect / music file of a device (1 for .IBM, 4 for .TAN).
int sfxVoices(SoundCard d);
int musicVoices(SoundCard d);

}  // namespace tmport::dos
