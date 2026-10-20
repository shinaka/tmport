#pragma once
// DOSBox-X save states of Gizmos & Gadgets (ssg.exe running in the gg profile; the reference captures'
// state library extracted/captures/gg/savestates, re/gg/docs/REFERENCE_CAPTURES.md §3, §9), read so
// that the engine can be compared with them field by field and started from them (the flow agent's
// --gg-load-state, game/load_state.h). Engine tooling only: the reader takes the user's own files.
//
// On the shared core's reader (dos/save/dosbox_state_file.h: the archive, the RAM, the CPU, the BIOS
// keys, mode 13h's VRAM and the DAC). This game's part:
//   * DGROUP is found by the Borland C++ copyright at DS:0004 ("Borland C++ - Copyright 1991 Borland
//     Intl.", tools/gg/capture_mem.py), never at a fixed segment: the MOUSEKEY TSR moves the game 86
//     paragraphs up. Every Ghidra segment S runs at S + relocation() (-0x786 with MOUSEKEY, -0x7dc
//     without); far pointers in RAM hold run-time segments.
//   * the screen buffer g_screenBuffer DS:0248 (Gfx_Init's 128 000 bytes: rows 0..199 the composed
//     screen, 200..399 the back page) and the visible page, VRAM (mode 13h, A000:0000), with the DAC;
//   * the mouse and cursor fields of DGROUP (the pointer, the hot spot, shown / hidden, the bounds);
//   * the PLAYERS.DAT copy tmctl.py keeps next to each state: <name>.players.
// apply() puts all of it into a GgGame: GgState and the service-held globals (loadFromDgroup of
// game/game.h), the screen buffer, VRAM, the DAC, the cursor's position and save-under into Video. Heap structures
// (the room set copy, the part catalogue, the carried lists, the roster copy, the puzzle copies) are
// the modules' to read through the far pointers (farPointerAt, bytesAt): game/load_state.h.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dos/save/dosbox_state_file.h"

namespace tmport::gg {

class GgGame;

// The mouse and cursor globals of a state's DGROUP (code_runtime.md §7, §12).
struct GgCursorFields {
    int16_t mouseX = 0, mouseY = 0;  // g_mouseX DS:7a88 / g_mouseY DS:7a86: the cursor's top-left
    uint16_t buttons = 0;            // g_mouseButtons DS:0a0c
    int16_t hotX = 0, hotY = 0;      // g_cursorHotX DS:7a74 / g_cursorHotY DS:7a76
    int16_t width = 0, height = 0;   // g_cursorW DS:7a70 / g_cursorH DS:7a72
    int16_t shown = 0;               // g_cursorShown DS:7a7c: drawn in the RAM screen
    int16_t hidden = 0;              // g_cursorHidden DS:0a30 (Mouse_Hide)
    int16_t forceDefault = 0;        // g_cursorForceDefault DS:0a52
    int16_t maxX = 0, maxY = 0;      // g_mouseMaxX DS:7a64 / g_mouseMaxY DS:7a62 (Mouse_SetBounds)
    int16_t savedX = 0, savedY = 0;  // DS:7a6c / 7a6e: where the background under the cursor was saved
};

class GgStateFile {
public:
    static constexpr std::string_view kSignature = "Borland C++ - Copyright 1991 Borland Intl.";
    static constexpr uint16_t kSignatureOffset = 4;     // DS:0004
    static constexpr uint16_t kDgroupImageSegment = 0x1EA7;  // Ghidra's DGROUP segment
    static constexpr size_t kScreenBytes = 320 * 400;   // the screen buffer (screen + back page)

    // Reads a state file; throws tmport::Error (unreadable, not a DOSBox-X state) or tmport::FormatError
    // (ssg.exe's DGROUP is not in its RAM).
    static GgStateFile load(const std::filesystem::path& path);

    const std::filesystem::path& path() const { return file_.path(); }
    const dos::DosboxStateFile& file() const { return file_; }

    // DGROUP: its run-time segment, the relocation of every Ghidra segment, DS:0000..BD03.
    uint16_t dgroupSegment() const { return dsSeg_; }
    int relocation() const { return static_cast<int>(dsSeg_) - kDgroupImageSegment; }
    std::span<const uint8_t> dgroup() const;
    uint8_t dsByte(uint16_t off) const { return file_.byteAt(dsLinear() + off); }
    uint16_t dsWord(uint16_t off) const { return file_.wordAt(dsLinear() + off); }
    uint32_t dsDword(uint16_t off) const { return file_.dwordAt(dsLinear() + off); }
    // The far pointer stored at DS:off as a physical address (offset + run-time segment * 16); 0 for NULL.
    uint32_t farPointerAt(uint16_t off) const;
    // `n` bytes at a physical address (empty if not all inside the RAM).
    std::span<const uint8_t> bytesAt(uint32_t linear, size_t n) const { return file_.bytesAt(linear, n); }
    // A physical address of a Ghidra segment:offset (relocated), and a run-time segment as a Ghidra
    // segment (0 stays 0).
    uint32_t imageLinear(uint16_t ghidraSegment, uint16_t off) const;
    uint16_t toImageSegment(uint16_t runtimeSegment) const;

    // The screen buffer g_screenBuffer DS:0248: 320 x 400 bytes (rows 0..199 the screen, 200..399 the
    // back page); empty when the pointer does not lead into the RAM (a state saved before Gfx_Init).
    std::span<const uint8_t> screenBuffer() const;
    // VRAM (320 x 200, A000:0000) and the DAC (6-bit) of the "Vga" entry; nullopt when the entry has
    // another layout (dos::DosboxStateFile::mode13).
    const std::optional<dos::DosboxStateFile::Mode13>& vga() const { return vga_; }
    GgCursorFields cursor() const;

    // The CPU where DOSBox-X stopped it (the stack segment follows DGROUP: DS:0000..BD03 then 1 paragraph
    // of alignment), and the far return addresses on the live stack whose segment is a resident code
    // segment of ssg.exe (Ghidra 1000..1e9f), as Ghidra segment:offset pairs, innermost first.
    const std::optional<dos::DosboxStateFile::Cpu>& cpu() const { return cpu_; }
    std::vector<std::pair<uint16_t, uint16_t>> stackReturns(uint32_t bytes = 0x200) const;

    // tmctl.py's library keeps the matching PLAYERS.DAT next to each state: <name>.players.
    static std::filesystem::path playersFileOf(const std::filesystem::path& sav);

    // Puts the state's moment into `g`: loadFromDgroup(g, dgroup(), withTime) (GgState and the
    // service-held globals: the rand() state, the held keys and the play mode, the palette's shadow,
    // saved copy and faded flag, ...), the screen buffer into video.screen() (both pages), VRAM and the
    // DAC into video, the cursor's position (driver and Video) and the cursor's save-under (below).
    // Heap structures are not touched. Returns what it could not apply (empty: all of it).
    std::vector<std::string> apply(GgGame& g, bool withTime = true) const;
    // The palette as the state has it, again: the shadow g_palShadow DS:024e, the saved copy g_palSaved
    // DS:5c22, g_palFaded DS:024c and the DAC. The modules' restores load pictures through the picture
    // cache, which applies the palette members it meets (the actors' bundles, the rooms' layers); the
    // original had applied them long before the moment and drawn its screen's palette over them, so a
    // loader calls this after every restore (game/load_state.cpp).
    void applyPalette(GgGame& g) const;

private:
    uint32_t dsLinear() const { return static_cast<uint32_t>(dsSeg_) * 16; }
    // The software cursor drawn in the state's RAM screen: the mouse hide state when it is 0, and the
    // state's save-under (DS:7a7e at DS:7a6c / 7a6e) in the active copy when g_cursorShown DS:7a7c is
    // set, so the next Mouse_RestoreUnder erases the cursor as the original's would.
    void applyCursorUnder(GgGame& g) const;

    dos::DosboxStateFile file_;
    uint16_t dsSeg_ = 0;
    std::optional<dos::DosboxStateFile::Mode13> vga_;
    std::optional<dos::DosboxStateFile::Cpu> cpu_;
};

}  // namespace tmport::gg
