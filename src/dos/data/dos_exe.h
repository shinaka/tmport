#pragma once
// SST.EXE of the 1990 DOS edition (V 1.0): the EXEPACK-unpacked load image and typed access to it.
//
// Addresses follow the Ghidra project (re/dos/ghidra_export): code and data are named SSSS:OOOO
// with the load image based at segment 0x1000, so image offset = (SSSS - 0x1000) * 16 + OOOO.
// DGROUP is segment 0x4d94 (DS:XXXX). Far pointers stored in the image are {u16 off, u16 seg}
// with image-relative segments (0x0568 for code segment 1568), exactly as left by the unpacker.
// re/dos/docs/fmt_data.md §1 and tools/unexepack.py describe the format.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace tmport::dos {

class DosExe {
public:
    static constexpr uint16_t kGhidraBase = 0x1000;
    static constexpr uint16_t kDgroupSeg = 0x4d94;
    static constexpr size_t kImageSize = 288992;  // unpacked load image of V 1.0

    // Unpacks EXEPACK in memory and validates the image (MZ, "RB" signature, image size, the
    // Turbo C copyright at DS:0004, EGA palette sanity). Throws std::runtime_error with a message
    // a player can act on ("not the 1990 DOS SST.EXE V 1.0", "file is truncated", ...).
    static DosExe load(std::span<const uint8_t> exeFile);
    static DosExe loadFile(const std::filesystem::path& sstExe);

    std::span<const uint8_t> image() const { return image_; }

    // Image bytes from Ghidra address seg:off to the end of the image (empty when out of range).
    std::span<const uint8_t> at(uint16_t seg, uint16_t off) const;
    // DGROUP from DS:0000 to the end of the image.
    std::span<const uint8_t> dgroup() const { return at(kDgroupSeg, 0); }

    uint8_t ds8(uint16_t off) const;
    uint16_t ds16(uint16_t off) const;
    int16_t dsS16(uint16_t off) const { return static_cast<int16_t>(ds16(off)); }
    // NUL-terminated string at DS:off (without the NUL).
    std::string_view dsString(uint16_t off) const;
    // The n bytes at DS:off (a table of dgroup_offsets.h). Throws std::out_of_range past the image.
    std::span<const uint8_t> dsBytes(uint16_t off, size_t n) const;

    // The initialised part of DGROUP (DS:0000..8F9F, dg::kInitSize bytes): the only part the
    // image holds (BSS follows at run time).
    size_t dgroupInitSize() const { return dgroup().size(); }
    // Overwrites bytes of the initialised DGROUP: the DOS mod layer's string and palette overrides
    // (dos/mods/dos_exe_strings.h), applied by DosFiles::open before anything reads a table, so
    // every reader (dsString, the tables, the writable copies of the game state) sees them. Throws
    // std::out_of_range past the initialised part.
    void writeDs(uint16_t off, std::span<const uint8_t> bytes);
    void writeDs16(uint16_t off, uint16_t v);

    // Resolves the far pointer stored at DS:off to image bytes (empty when it points outside).
    std::span<const uint8_t> farPtrAtDs(uint16_t off) const;
    // The Ghidra-style segment and offset of the far pointer stored at DS:off.
    struct FarPtr {
        uint16_t seg = 0;  // Ghidra segment (stored segment + kGhidraBase)
        uint16_t off = 0;
    };
    FarPtr readFarPtr(uint16_t dsOff) const;

private:
    std::vector<uint8_t> image_;
};

}  // namespace tmport::dos
