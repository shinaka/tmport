#pragma once
// ssg.exe of Gizmos & Gadgets (DOS v1.0, 1993): the MZ load module, its DGROUP and the Borland
// VROOMM overlays, with the addressing of the Ghidra project (re/CONTEXT.md, G&G section;
// re/gg/docs/ARCHITECTURE.md §1).
//
//  * Resident code and data (Ghidra 1000..2a7f): image offset = (SSSS - 0x1000) * 16 + OOOO, file
//    offset = image offset + 0x1200 (the MZ header). DGROUP is Ghidra segment 1ea7 (DS:XXXX = file
//    offset 0xFC70 + XXXX); far pointers stored in the image hold image segments (0x0ea7 = DGROUP).
//  * Overlays (Ghidra 2a80..4140): the code of overlay n as stored after the 'FBOV' header (file
//    bytes, fix-up words unresolved), at the Ghidra segment tools/gg/vroomm.py gives it in the
//    flattened image: the overlays follow the load module paragraph-aligned in overlay order.
//  * Stub segments (Ghidra 1e0d..1e9f): entry k at stub:0020 + 5k is `CD 3F <u16 offset> 00`;
//    resolveCode() maps a far pointer to a stub entry onto the overlay function it calls.
//
// The engine runs all code resident (PORTING_NOTES.md §6): the overlays matter only for the few
// tables that live in overlay code (energy-forms variants, electric terminal geometry, switch
// tables) and for mapping DGROUP code pointers to engine functions. The bytes are always read from
// the user's file; nothing of it is in the repository.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace tmport::gg {

class GgExe {
public:
    static constexpr size_t kFileSize = 213200;         // fmt_data.md §1
    static constexpr size_t kHeaderSize = 0x1200;        // 0x120 MZ header paragraphs
    static constexpr size_t kLoadModuleEnd = 0x1BA00;    // 'FBOV' follows the load module here
    static constexpr uint16_t kGhidraBase = 0x1000;      // Ghidra segment = image segment + 0x1000
    static constexpr uint16_t kDgroupSeg = 0x1ea7;       // Ghidra segment of DGROUP
    static constexpr uint16_t kDgroupImageSeg = 0x0ea7;  // the same as stored in far pointers
    static constexpr size_t kDsFileOffset = 0xFC70;      // file offset of DS:0000
    static constexpr uint16_t kDgroupSize = 0xBD04;      // DGROUP 0xBD04 bytes (segment table)
    static constexpr uint16_t kDsInitEnd = 0x4552;       // initialised data DS:0000..4551, BSS after
    static constexpr uint16_t kFirstStubSeg = 0x1e0d;    // stub segments 1e0d..1e9f
    static constexpr uint16_t kFirstOverlaySeg = 0x2a80; // overlay code 2a80..4140
    static constexpr int kOverlayCount = 35;             // 2 of them empty (ovl09, ovl29)
    static constexpr uint16_t kStubHeaderSize = 0x20;    // stub entry k at stub:0020 + 5k
    static constexpr uint16_t kStubEntrySize = 5;

    struct Overlay {
        int number = 0;                // ovlNN: stub order (re/gg/docs/overlay_map.tsv)
        uint16_t stubSeg = 0;          // Ghidra segment of the resident stub (1e0d..1e9f)
        uint16_t seg = 0;              // Ghidra segment of the code (2a80..), 0 for an empty overlay
        size_t fileOffset = 0;         // file offset of the code in ssg.exe
        std::span<const uint8_t> code; // the code as stored (fix-up words hold segment-table offsets)
        std::vector<uint16_t> fixups;  // offsets of the fix-up words in `code`
        std::vector<uint16_t> entries; // stub entry k -> offset of the function in `code`
    };

    // A Ghidra-style segment:offset (seg 0 with off 0 is a null pointer).
    struct Address {
        uint16_t seg = 0;
        uint16_t off = 0;
        bool isNull() const { return seg == 0 && off == 0; }
        bool operator==(const Address&) const = default;
    };

    // Parses and validates the file: its size, the MZ header, TLINK's signature at 0x1C, the
    // Borland C++ copyright at DS:0004, the C0 entry (MOV DX,DGROUP), the 'FBOV' overlay header and
    // segment table, and the 35 overlay stubs. Throws std::runtime_error with a message a player
    // can act on ("not ssg.exe of Gizmos & Gadgets! DOS v1.0 (...)").
    static GgExe load(std::vector<uint8_t> file);
    static GgExe loadFile(const std::filesystem::path& ssgExe);

    // Move-only: the overlay spans point into the file buffer, which a move keeps.
    GgExe(GgExe&&) noexcept = default;
    GgExe& operator=(GgExe&&) noexcept = default;
    GgExe(const GgExe&) = delete;
    GgExe& operator=(const GgExe&) = delete;

    std::span<const uint8_t> file() const { return file_; }
    // The MZ load module (resident code, stubs, DGROUP, stack): file 0x1200..0x1BA00.
    std::span<const uint8_t> image() const;

    // Bytes at Ghidra address seg:off up to the end of the load module (resident segments) or of
    // the overlay's code (overlay segments); empty when the address is outside both.
    std::span<const uint8_t> at(uint16_t seg, uint16_t off) const;
    std::span<const uint8_t> at(Address a) const { return at(a.seg, a.off); }
    // File offset of seg:off in ssg.exe, or SIZE_MAX when outside the load module and the overlays.
    size_t fileOffset(uint16_t seg, uint16_t off) const;

    // DGROUP: DS:0000..BD03 (the BSS part is zero in the file, as at start-up).
    std::span<const uint8_t> dgroup() const;
    uint8_t ds8(uint16_t off) const;
    uint16_t ds16(uint16_t off) const;
    int16_t dsS16(uint16_t off) const { return static_cast<int16_t>(ds16(off)); }
    uint32_t ds32(uint16_t off) const;
    // NUL-terminated string at DS:off (without the NUL).
    std::string_view dsString(uint16_t off) const;
    // The n bytes at DS:off (a table of dgroup_offsets.h). Throws std::out_of_range past DGROUP.
    std::span<const uint8_t> dsBytes(uint16_t off, size_t n) const;
    // Overwrites initialised DGROUP bytes (for a later mod layer, as dos::DosExe::writeDs). Throws
    // std::out_of_range past DS:4551.
    void writeDs(uint16_t off, std::span<const uint8_t> bytes);

    // The far pointer stored at DS:off as a Ghidra address (stored segment + 0x1000; a stored
    // 0000:0000 stays null).
    Address dsFarPtr(uint16_t dsOff) const;
    // The DGROUP offset a far data pointer points at (it must point into DGROUP; std::nullopt-like
    // failure is reported as std::out_of_range).
    uint16_t dsOffsetOf(Address farPtr) const;
    // Follows a code pointer through an overlay stub: stub:0020 + 5k becomes the overlay function
    // (overlay seg : entry k); any other address is returned unchanged.
    Address resolveCode(Address a) const;

    const std::vector<Overlay>& overlays() const { return overlays_; }
    // The overlay whose code (or stub) is at this Ghidra segment; nullptr if none.
    const Overlay* overlayBySeg(uint16_t ghidraSeg) const;
    const Overlay* overlayByStub(uint16_t ghidraStubSeg) const;

private:
    GgExe() = default;

    std::vector<uint8_t> file_;
    std::vector<Overlay> overlays_;
};

}  // namespace tmport::gg
