#pragma once
// ExeTables: DGROUP (the automatic data segment, NE segment 56) of TMTWINCD.EXE with its far
// pointers resolved through the segment's relocation records (in the file each far pointer holds
// a fix-up chain placeholder FF FF 00 00). Raw typed access plus decoders for the tables the
// engine uses; offsets are in data/dgroup_offsets.h. Spec: re/docs/dgroup_map.md.

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ne/ne_image.h"
#include "util/rgb.h"
#include "vfs/data_source.h"

namespace tmport {

// A far pointer stored in DGROUP, after relocation.
struct FarPtr {
    enum class Kind {
        Null,         // 00 00 00 00 without a relocation
        DGroup,       // points into DGROUP: offset is a DGROUP offset
        Segment,      // points into another segment of the EXE (code)
        Import,       // imported entry point
        Unrelocated,  // non-zero raw value without a relocation (unexpected)
    };
    Kind kind = Kind::Null;
    uint16_t segment = 0;  // NE segment number (DGroup: 56)
    uint16_t offset = 0;
    std::string module;    // Import
    uint16_t ordinal = 0;  // Import by ordinal
    std::string name;      // Import by name

    // Ghidra-style selector of the segment (0x1000 + 8*(segment-1)).
    uint16_t selector() const { return static_cast<uint16_t>(0x1000 + 8 * (segment - 1)); }
    bool isNull() const { return kind == Kind::Null; }
};

// One RoomDef record (DS:23d8 + 0x1c*room; dgroup_map.md §4.16).
struct RoomDef {
    int16_t startX = 0;
    int16_t startY = 0;
    uint8_t startFrame = 0;
    uint8_t startFacingLeft = 0;
    uint8_t exitLeft = 0;   // 0xFF = wall
    uint8_t exitRight = 0;  // 0xFF = wall
    uint8_t exitDoor = 0;   // 0xFF = none
    uint8_t unused09 = 0;
    uint8_t typeLeft = 0;
    uint8_t typeRight = 0;
    uint8_t typeDoor = 0;
    uint8_t unused0D = 0;
    int16_t doorX = 0;      // -100 = no door
    int16_t unused10 = 0;
    uint16_t bgPmap = 0;
    std::array<uint8_t, 3> slotObject{};   // runtime fields (0 in the image)
    std::array<uint8_t, 3> slotContent{};  // runtime fields (0 in the image)
    uint8_t level = 0;
    uint8_t music = 0;
};

class ExeTables {
public:
    // Loads DGROUP of the given EXE image. Throws tmport::Error.
    explicit ExeTables(std::shared_ptr<const ne::NeImage> exe);
    static std::unique_ptr<ExeTables> load(const vfs::DataSource& source);

    const ne::NeImage& exe() const { return *exe_; }
    uint16_t segmentNumber() const { return segNum_; }

    // Raw DGROUP image (initialised data + zero BSS, as in the file). The typed reads below use it.
    std::span<const uint8_t> raw() const { return data_; }
    // The same image with the relocations applied: every relocated far pointer holds its target
    // as offset:selector (Ghidra selector numbering, DGROUP = 11b8), as at run time.
    std::span<const uint8_t> patched() const { return patched_; }
    size_t size() const { return data_.size(); }

    // Typed reads (little-endian). Throw tmport::FormatError outside DGROUP.
    uint8_t u8(uint32_t off) const;
    int8_t i8(uint32_t off) const;
    uint16_t u16(uint32_t off) const;
    int16_t i16(uint32_t off) const;
    uint32_t u32(uint32_t off) const;
    int32_t i32(uint32_t off) const;
    std::span<const uint8_t> bytes(uint32_t off, size_t count) const;
    template <class T, size_t N>
    std::array<T, N> array(uint32_t off) const;

    // NUL-terminated byte string at off (without the NUL).
    std::span<const uint8_t> cstr(uint32_t off) const;

    // Far pointers.
    FarPtr farPtr(uint32_t off) const;
    // DGROUP target of the far pointer at off; nullopt for NULL or a non-DGROUP target.
    std::optional<uint16_t> dgPtr(uint32_t off) const;
    // String the far pointer at off points to (empty span for NULL / non-DGROUP).
    std::span<const uint8_t> farCstr(uint32_t off) const;
    const std::map<uint16_t, FarPtr>& farPointers() const { return farPtrs_; }
    size_t relocationCount() const { return relocCount_; }

    struct Validation {
        bool ok = true;
        std::vector<std::string> messages;  // one line per check ("ok: ..." / "FAIL: ...")
    };
    Validation validate() const;

    // --- Typed tables --------------------------------------------------------------------------
    std::array<RoomDef, 50> rooms() const;          // DS:23d8
    std::array<Rgb8, 16> egaColours() const;        // DS:07d8, {u16 r, g, b}[16]
    std::array<uint8_t, 16> vga16Index() const;     // DS:0838, u16[16] (low bytes)
    // Later milestones add typed decoders here (player tables, clue words, menus, dialogs, ...).

    // --- Table overrides of mods (src/mods/table_overrides.h, docs/MODDING.md) ----------------
    // Apply them right after loading, before anything keeps views into the image. Readers then
    // see the mod's values through the accessors above.
    // Overwrites bytes of the image (raw and patched copies). Throws FormatError outside the
    // segment's original image or over a relocated far pointer.
    void patch(uint32_t off, std::span<const uint8_t> bytes);
    // Appends a NUL-terminated string after the image and points the DGROUP far pointer at `site`
    // to it. Returns the string's DGROUP offset. Throws if `site` is not a relocated far pointer
    // or the segment would exceed 64 KB.
    uint16_t repointString(uint32_t site, std::span<const uint8_t> text);
    // Size of the image as loaded from the EXE (size() grows when strings are appended).
    size_t originalSize() const { return baseSize_; }

private:
    std::shared_ptr<const ne::NeImage> exe_;
    uint16_t segNum_ = 0;
    std::vector<uint8_t> data_;
    std::vector<uint8_t> patched_;
    std::map<uint16_t, FarPtr> farPtrs_;  // site -> resolved far pointer
    size_t relocCount_ = 0;
    size_t baseSize_ = 0;
};

template <class T, size_t N>
std::array<T, N> ExeTables::array(uint32_t off) const {
    std::array<T, N> out{};
    for (size_t i = 0; i < N; ++i) {
        const uint32_t o = off + static_cast<uint32_t>(i * sizeof(T));
        if constexpr (sizeof(T) == 1)
            out[i] = static_cast<T>(u8(o));
        else if constexpr (sizeof(T) == 2)
            out[i] = static_cast<T>(u16(o));
        else
            out[i] = static_cast<T>(u32(o));
    }
    return out;
}

}  // namespace tmport
