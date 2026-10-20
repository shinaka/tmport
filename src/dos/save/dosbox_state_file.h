#pragma once
// DOSBox-X save states (.sav), the game-neutral part of the reader (shared core, portcore): the zip
// archive and its entries, the guest's RAM, a program's data segment found by a signature, the CPU
// registers and the live stack, the BIOS keyboard buffer, and the VGA's mode 13h video memory and DAC.
// The games' readers sit on it: Treasure Mountain's DOS edition (dos/save/dosbox_state.h: SST.EXE,
// EGA / Tandy screens) and Gizmos & Gadgets (gg/save/gg_state_file.h: ssg.exe, mode 13h). Engine
// tooling only: the reader takes the user's own state files at run time.
//
// Format (DOSBox-X 2026.10.01, observed): a zip archive of deflated (or stored) entries.
//   "Machine_Type"  the machine's name (MCH_EGA, MCH_TANDY, MCH_VGA, ...)
//   "Memory"        a 0x88-byte header, then the guest's physical RAM from address 0
//   "CPU"           the general registers and EIP first; the segment values later as 8-byte words
//                   ES, CS, SS, DS (found by the stack segment the caller expects)
//   "Vga"           the VGA state; with 256 KB of video memory (machine=vgaonly, the Gizmos & Gadgets
//                   profile) its last 0x40000 + 2576 bytes are the video memory (0x40000) and a tail,
//                   and the DAC's 256 RGB triples (6-bit) lie 6767 bytes before the video memory. The
//                   video memory interleaves the four planes, one 4-byte group per planar address; in
//                   mode 13h (chain-4) the byte at A000:a is at video memory offset 16 * (a >> 2) + (a & 3).
//                   [O: 143 states of extracted/captures/gg/savestates, every one 808 016 bytes; the
//                   screen buffer in RAM equals that view of the video memory in all but the movie
//                   state, which decodes straight into VRAM, and the DAC equals the game's shadow
//                   palette.] Other video memory sizes are not handled (mode13() returns nullopt).

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tmport::dos {

// A zip archive's entry as stored (method 0 or 8) with its uncompressed size.
struct ZipEntry {
    uint16_t method = 0;
    std::span<const uint8_t> packed;
    uint32_t size = 0;
};
// The entry `name` of a zip archive (central directory lookup); nullopt if absent or broken.
std::optional<ZipEntry> findZipEntry(std::span<const uint8_t> zip, std::string_view name);
// The bytes of an entry, at most `limit` of them (the start of a large entry). Throws
// tmport::FormatError for a compression method other than 0 or 8.
std::vector<uint8_t> readZipEntry(const ZipEntry& e, size_t limit);

class DosboxStateFile {
public:
    static constexpr size_t kMemoryHeader = 0x88;  // "Memory" entry: header before physical address 0
    static constexpr uint32_t kRamBytes = 0xA0000; // conventional memory: everything a DOS game uses
    // Mode 13h in the "Vga" entry (see the file comment).
    static constexpr size_t kVgaMemoryBytes = 0x40000;
    static constexpr size_t kVgaTailBytes = 2576;
    static constexpr size_t kVgaDacBeforeMemory = 6767;
    static constexpr int kMode13Width = 320;
    static constexpr int kMode13Height = 200;

    // Reads a state file: the archive, the RAM (the "Memory" entry up to kRamBytes) and the machine
    // name. Throws tmport::Error (unreadable, not a zip, no "Memory" entry) or tmport::FormatError
    // (the "Memory" entry is shorter than 64 KB of RAM).
    static DosboxStateFile load(const std::filesystem::path& path);

    const std::filesystem::path& path() const { return path_; }
    const std::string& machineName() const { return machineName_; }  // "MCH_EGA", "MCH_VGA", ...

    // An entry of the archive, at most `limit` bytes of it; nullopt if the archive has no such entry.
    std::optional<std::vector<uint8_t>> entry(std::string_view name, size_t limit = SIZE_MAX) const;
    bool hasEntry(std::string_view name) const;

    // ---- RAM
    // Guest RAM from physical address 0 (the conventional 640 KB).
    std::span<const uint8_t> memory() const { return mem_; }
    // `n` bytes at a physical address; empty if they are not all inside memory().
    std::span<const uint8_t> bytesAt(uint32_t linear, size_t n) const;
    uint8_t byteAt(uint32_t linear) const;
    uint16_t wordAt(uint32_t linear) const;
    uint32_t dwordAt(uint32_t linear) const;
    // The segment whose offset `offset` holds `signature` (a paragraph-aligned match of the first
    // occurrence in RAM, e.g. a Borland copyright at DS:0004); nullopt if the first match is not
    // paragraph-aligned at that offset, or there is none.
    std::optional<uint16_t> findSegmentBySignature(std::string_view signature, uint16_t offset) const;

    // ---- CPU
    // The CPU's registers where DOSBox-X stopped it: the "CPU" entry's general registers and EIP,
    // and the segment values (8-byte words ES, CS, SS, DS) found by SS == `stackSegment` (the
    // segment the program's stack is known to be in). nullopt without the entry or no match.
    struct Cpu {
        uint16_t cs = 0, ss = 0;  // run-time segments
        uint32_t ip = 0, sp = 0;
    };
    std::optional<Cpu> cpu(uint16_t stackSegment) const;
    // The far return-address candidates on the live stack (SS:SP upwards, `bytes` of it, word by
    // word): every (run-time segment, offset) pair, innermost first. The caller keeps the ones whose
    // segment is one of its code segments.
    std::vector<std::pair<uint16_t, uint16_t>> stackWords(const Cpu& cpu, uint32_t bytes) const;

    // ---- BIOS
    // The BIOS keyboard buffer (0040:001A head .. 001C tail, words from 0040:001E) in reading order,
    // and the shift flags 0040:0017.
    std::vector<uint16_t> biosKeys() const;
    uint8_t biosShiftFlags() const { return byteAt(0x417); }

    // ---- VGA (mode 13h)
    struct Mode13 {
        std::vector<uint8_t> vram;           // 320 x 200 bytes, A000:0000.. as the CPU sees them
        std::array<uint8_t, 768> dac{};      // the DAC's 256 RGB triples, 6-bit values
    };
    // The mode 13h picture and the DAC from the "Vga" entry; nullopt when the entry is missing or
    // its size does not have the layout of the file comment (another video memory size).
    std::optional<Mode13> mode13() const;

private:
    std::filesystem::path path_;
    std::vector<uint8_t> zip_;
    std::vector<uint8_t> mem_;
    std::string machineName_;
};

}  // namespace tmport::dos
