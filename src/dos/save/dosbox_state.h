#pragma once
// DOSBox-X save states (.sav) of the 1990 DOS edition: the reference captures' state library
// (extracted/captures/dos/savestates, re/dos/docs/REFERENCE_CAPTURES.md §4.6, R2.1), read so the
// engine can start from the exact moment DOSBox-X saved (dos/game/load_state.h) and tests can compare
// with it. Engine tooling only: the reader takes the user's own state files at run time.
//
// Format (DOSBox-X 2026.10.01, observed): a zip archive of deflated entries. "Machine_Type" names the
// machine (MCH_EGA, MCH_TANDY); "Memory" is a 0x88-byte header followed by the guest's physical RAM
// from address 0; "Vga" is the VGA/EGA state, whose video memory (4 planes interleaved, one 4-byte
// group per address) starts where the game's latch table A000:0000..003F lies (1000:0836 fills it
// with colour k at bits C0 of address k, k = 0..15, fmt_graphics.md §2.1).
//
// SST.EXE in RAM: DGROUP is found by the Turbo C copyright at DS:0004. DOS loads the program at
// another segment than Ghidra's image base, so every Ghidra segment S runs at S + relocation()
// (DGROUP 4D94 at 45B8 in the reference sessions: relocation -0x7DC). Far pointers stored in RAM
// hold run-time segments.
//
// The picture on the screen comes from the video memory, as the game wrote it:
//   EGA    mode 0Eh (640x200), each game pixel doubled; row y at A000:[DS:2EEE + 2y] (page 0x4000)
//   Tandy  mode 09h, the 32 KB page at the top of the 640 KB (physical 0x98000 with DOSBox-X's
//          Tandy BIOS), four banks of 0x2000, 160-byte rows, the back buffer's own byte layout
// and is returned in the back buffer's layout (160 bytes x 200 rows, high nibble = left pixel).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tmport::dos {

class DosboxState {
public:
    enum class Machine : uint8_t { Ega, Tandy, Other };

    static constexpr uint16_t kDgroupImageSegment = 0x4D94;  // Ghidra's DGROUP segment
    static constexpr size_t kDgroupBytes = 0xCC20;           // DS:0000..CC1F (initialised data + BSS)
    static constexpr int kScreenRowBytes = 160;
    static constexpr int kScreenRows = 200;

    // Reads a state file; throws tmport::Error (unreadable, not a zip, no "Memory" entry) or
    // tmport::FormatError (SST.EXE's DGROUP is not in its RAM).
    static DosboxState load(const std::filesystem::path& path);

    const std::filesystem::path& path() const { return path_; }
    Machine machine() const { return machine_; }
    const std::string& machineName() const { return machineName_; }  // "MCH_EGA", "MCH_TANDY", ...

    // Guest RAM from physical address 0 (the conventional 640 KB and the upper memory up to A0000).
    std::span<const uint8_t> memory() const { return mem_; }
    // `n` bytes at a physical address; empty if they are not all inside memory().
    std::span<const uint8_t> bytesAt(uint32_t linear, size_t n) const;
    uint8_t byteAt(uint32_t linear) const;
    uint16_t wordAt(uint32_t linear) const;

    // DGROUP: its run-time segment, the relocation of every Ghidra segment, DS:0000..CC1F.
    uint16_t dgroupSegment() const { return dsSeg_; }
    int relocation() const { return static_cast<int>(dsSeg_) - kDgroupImageSegment; }
    std::span<const uint8_t> dgroup() const { return bytesAt(dsLinear(), kDgroupBytes); }
    uint8_t dsByte(uint16_t off) const { return byteAt(dsLinear() + off); }
    uint16_t dsWord(uint16_t off) const { return wordAt(dsLinear() + off); }
    uint32_t dsDword(uint16_t off) const;
    // The far pointer stored at DS:off as a physical address (offset + run-time segment * 16).
    uint32_t farPointerAt(uint16_t off) const;
    // A physical address of a Ghidra segment:offset (relocated).
    uint32_t imageLinear(uint16_t ghidraSegment, uint16_t off) const;
    // A run-time segment as a Ghidra segment (0 stays 0, the null pointer).
    uint16_t toImageSegment(uint16_t runtimeSegment) const;

    // The back buffer (segment 2005: 2005:0010 + y * 0xA0), 160 x 200 bytes.
    std::span<const uint8_t> backBuffer() const;
    // The picture shown (160 x 200 bytes, the back buffer's layout); nullopt when the video memory
    // of this machine type was not found.
    const std::optional<std::vector<uint8_t>>& screen() const { return screen_; }

    // The BIOS keyboard buffer (0040:001A head .. 001C tail, words from 0040:001E) in reading order,
    // and the shift flags 0040:0017.
    std::vector<uint16_t> biosKeys() const;
    uint8_t biosShiftFlags() const { return byteAt(0x417); }

    // The CPU's registers where DOSBox-X stopped it (the "CPU" entry: the general registers and IP
    // first, the segment values as 8-byte words ES, CS, SS, DS found by DS = dgroupSegment()).
    struct Cpu {
        uint16_t cs = 0, ss = 0;  // run-time segments
        uint32_t ip = 0, sp = 0;
    };
    const std::optional<Cpu>& cpu() const { return cpu_; }
    // The far return addresses on the live stack (SS:SP upwards, `bytes` of it) whose segment is a
    // code segment of the program, as Ghidra segment:offset pairs, innermost first.
    std::vector<std::pair<uint16_t, uint16_t>> stackReturns(uint32_t bytes = 0x200) const;

    // tmctl.py's library keeps the matching USERS.DAT copy next to each state: <name>.users.
    static std::filesystem::path usersFileOf(const std::filesystem::path& sav);

private:
    uint32_t dsLinear() const { return static_cast<uint32_t>(dsSeg_) * 16; }
    void readScreen(std::span<const uint8_t> vga);

    std::filesystem::path path_;
    Machine machine_ = Machine::Other;
    std::string machineName_;
    std::vector<uint8_t> mem_;
    uint16_t dsSeg_ = 0;
    std::optional<std::vector<uint8_t>> screen_;
    std::optional<Cpu> cpu_;
};

}  // namespace tmport::dos
