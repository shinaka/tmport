#pragma once
// The four MRn resource archives gizmo000..003.dat (re/gg/docs/fmt_data.md §2): Res_OpenFiles
// 14ff:0167 opens them, Res_Read 14ff:000d reads an entry (method 0 stored, any other method
// through Lzh_Decompress 1b48:02aa, gg/data/lzh.h).
//
//   +0   'M' 'R' <volume digit> 0x1A
//   +4   u32 off[n + 1], relative to file offset 4: off[0] = 4 (n + 1), off[n] = file length - 4
//   entry k = file[4 + off[k] .. 4 + off[k + 1]) = u8 type, u8 method, payload
//   resource id = volume << 12 | k; the last entry of every volume is type 'I', stored, 5A A5
//
// Entries are decoded on demand from the volume bytes kept in memory; each load returns a private
// copy (the original reads into one shared 0xFFFF-byte buffer, Res_LoadCopy 14ff:00fe copies it).
// An Archive does not change after construction: its const members may be called from any thread.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "gg/data/lzh.h"

namespace tmport::gg {

// Type bytes of entries and bundle members (fmt_data.md §5). The letter says what the loader
// expects; the same letter serves different record kinds in different places.
enum class ResType : uint8_t {
    Bundle = 0x01,  // members inline (fmt_data.md §4)
    A = 'A',        // room sets, screen-animation sets
    C = 'C',        // FF: a hole in a speaker effect bank
    D = 'D',        // DAM scripts, movie frames, level descriptions, puzzle tables
    F = 'F',        // fonts (ids 9, 10), per-variant puzzle data
    G = 'G',        // LPX pictures
    H = 'H',        // New Player Hints, help page texts
    I = 'I',        // small integer arrays, the integrity words, list ends
    L = 'L',        // sprite-set end markers, music-bank song lengths
    M = 'M',        // music-bank mute lists, part-list records
    P = 'P',        // palette ranges or LPX pictures
    R = 'R',        // race paths
    S = 'S',        // BMF songs, short strings, binary records
    T = 'T',        // texts
    W = 'W',        // wipe patterns, aircraft scenery
};

inline constexpr uint8_t kMethodStored = 0;
inline constexpr uint8_t kMethodLzh = 3;  // the only other method in the data; any non-zero is compressed

// One entry as the index describes it.
struct EntryInfo {
    uint16_t id = 0;
    int volume = 0;
    int index = 0;
    uint8_t type = 0;
    uint8_t method = 0;
    size_t fileOffset = 0;  // of the type byte in the volume file
    size_t storedSize = 0;  // payload bytes in the file (without the type and method bytes)
};

// A decoded entry.
struct Resource {
    uint16_t id = 0;
    uint8_t type = 0;
    uint8_t method = 0;
    std::vector<uint8_t> bytes;  // the payload, decompressed
    std::span<const uint8_t> data() const { return bytes; }
    ResType resType() const { return static_cast<ResType>(type); }
};

// The LZH input refill 1b48:002e (code_runtime.md §6.4, §16.2): Lzh_Decompress reads a method-3
// stream in blocks of 4000 bytes (1b48:0002, the first one too, after the 4-byte size word) and after
// each read drains the keyboard ring (Kbd_PopQueuedKey until it returns 0), so keys typed while a
// resource is decoded are lost. The engine decodes at once: a load on a thread with a hook calls it
// once with the number of those reads. The game installs one for its own thread (game/flow.h
// installLzhKeyDrain); loads on other threads (tools, tests) and stored entries never call it.
inline constexpr size_t kLzhReadBlock = 4000;
using LzhRefillHook = std::function<void(int reads)>;
// For the calling thread; an empty hook removes it. Returns the previous one.
LzhRefillHook setLzhRefillHook(LzhRefillHook hook);
// The 4000-byte reads Lzh_Decompress makes for a stream of which it used `usedBytes` (the size word
// included): ceil((usedBytes - 4) / 4000).
int lzhReadCount(size_t usedBytes);

class Archive {
public:
    static constexpr int kVolumes = 4;
    // Entries per volume in v1.0 (fmt_data.md §2.1): ids 0x000..0x36d, 0x1000..0x11de, ...
    static constexpr std::array<int, kVolumes> kEntryCounts = {878, 479, 335, 205};
    static constexpr uint16_t kIntegrityWord = 0xA55A;

    // "gizmo000.dat" .. "gizmo003.dat" (the names of DS:0550, lower case as on the disk).
    static std::string volumeName(int volume);
    // The checks of open (magic and digit, offset table, entry count, the 0xA55A last entry) for one
    // volume file. Throws tmport::FormatError whose message starts with the file name.
    static void checkVolume(int volume, std::span<const uint8_t> bytes);

    // Takes the four volume files (checked with checkVolume) and the default distance lengths of
    // the user's ssg.exe (LzhDistanceRows::fromExe).
    Archive(std::array<std::vector<uint8_t>, kVolumes> volumes, const LzhDistanceRows& rows);

    int entryCount(int volume) const;
    size_t totalEntries() const;
    bool exists(uint16_t id) const;
    // Every id in volume order.
    std::vector<uint16_t> ids() const;
    // The ids of every entry of one type (e.g. all 'T' texts), in id order.
    std::vector<uint16_t> idsOfType(ResType type) const;

    // Throws std::out_of_range for an id that does not exist.
    EntryInfo info(uint16_t id) const;
    // The entry as stored: type, method, payload (still compressed).
    std::span<const uint8_t> stored(uint16_t id) const;
    // The unpacked payload size (stored: the payload; method 3: its size word).
    uint32_t unpackedSize(uint16_t id) const;
    // Res_Read's return value: a stored entry reads the payload plus the 2 header bytes of the next
    // entry, so it returns payload + 2 (fmt_data.md §2.2 quirk; nothing seen depends on it).
    uint32_t originalReadSize(uint16_t id) const;

    // Res_Read / Res_LoadCopy: the decoded entry. Throws std::out_of_range for a missing id and
    // tmport::FormatError for a broken one (`info`, if given, receives the method-3 details). A method-3
    // entry calls the thread's LzhRefillHook (see above).
    Resource load(uint16_t id, LzhInfo* info = nullptr) const;
    // The same, checking the type: throws tmport::FormatError when the entry has another type.
    Resource load(uint16_t id, ResType expected) const;
    std::vector<uint8_t> read(uint16_t id) const { return load(id).bytes; }

    const LzhDistanceRows& distanceRows() const { return rows_; }
    std::span<const uint8_t> volumeBytes(int volume) const { return volumes_.at(static_cast<size_t>(volume)); }

private:
    struct Volume {
        std::vector<uint32_t> offsets;  // off[0..n], relative to file offset 4
    };
    const std::vector<uint8_t>& volumeOf(uint16_t id, size_t& index) const;

    std::array<std::vector<uint8_t>, kVolumes> volumes_;
    std::array<Volume, kVolumes> index_;
    LzhDistanceRows rows_;
};

}  // namespace tmport::gg
