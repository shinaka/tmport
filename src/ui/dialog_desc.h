#pragma once
// DlgDesc (dialog descriptor, 0x1a bytes) and PagerDesc (multi-page descriptor, 0x1e bytes):
// the static data that drives the game-drawn message boxes of segment 10c0
// (re/docs/code_1098-10d8.md §2.8-2.9, dgroup_map.md §4.7/§4.14/§4.15/§4.19). Both are decoded
// from the user's TMTWINCD.EXE (DGROUP, far pointers resolved through ExeTables); the engine
// keeps the strings as raw bytes.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "gfx/pixmap.h"

namespace tmport {
class ExeTables;
}

namespace tmport::ui {

// DlgDesc.type (code_1098-10d8.md §10.2).
namespace dlgtype {
inline constexpr uint16_t kNonModal = 0;    // message box drawn with Dlg_Draw (name entry)
inline constexpr uint16_t kMessage = 1;     // modal message (Enter / click footer = 0, Esc = -1)
inline constexpr uint16_t kCompact = 2;     // unused by shipped data
inline constexpr uint16_t kCompact3 = 3;    // unused by shipped data
inline constexpr uint16_t kPagerOne = 4;    // pager, single page ("Done")
inline constexpr uint16_t kPagerFirst = 5;  // pager, first page ("See Next Page")
inline constexpr uint16_t kPagerMiddle = 6; // pager, middle page (back / next)
inline constexpr uint16_t kPagerLast = 7;   // pager, last page (back / done)
inline constexpr uint16_t kYesNo = 8;       // Yes/No: 1 Yes, 0 No, -1 Esc / footer
inline constexpr uint16_t kDoneCancel = 9;  // unused by shipped data
}  // namespace dlgtype

struct DlgDesc {
    uint16_t type = dlgtype::kMessage;              // +0x00
    std::optional<std::vector<uint8_t>> title;      // +0x02 far char*; nullopt = NULL (no title bar)
    uint8_t footer = 0;                             // +0x06 0 none, 1 = DS:0b24, 2 = DS:0b31
    std::vector<uint8_t> body;                      // +0x07 far char* (raw bytes, no NUL)
    uint8_t defaultButton = 0;                      // +0x0b (callers patch it, e.g. option values)
    int16_t x = -1;                                 // +0x0c -1 = centre in 512
    int16_t y = -1;                                 // +0x0e -1 = centre in 384
    int16_t textWidth = -1;                         // +0x10 -1 = measured
    int16_t lineCount = -1;                         // +0x12 -1 = 1 + CR/LF count
    std::shared_ptr<const gfx::PixMap> picture;     // +0x14 PixMap far* (NULL in every static one)
    uint8_t picDx = 0;                              // +0x18
    uint8_t picDy = 0;                              // +0x19

    static constexpr uint16_t kSize = 0x1A;
    // Decodes the descriptor at DGROUP offset `off`. Throws FormatError if it cannot be read.
    // A non-NULL picture pointer cannot be resolved statically and is ignored (none exists).
    static DlgDesc fromExe(const ExeTables& exe, uint16_t off);
};

struct PagerDesc {
    std::optional<std::vector<uint8_t>> title;              // +0x00 (filled by the owner)
    uint8_t footer = 0;                                     // +0x04
    std::vector<std::vector<uint8_t>> texts;                // +0x05 texts[pageCount]
    int pageCount = 0;                                      // +0x09 u8
    int16_t x = 0, y = 0, textWidth = 0, lineCount = 0;     // +0x0a, +0x0c, +0x0e, +0x10
    std::vector<std::shared_ptr<const gfx::PixMap>> pics;   // +0x12 pics[pageCount] (may be NULL)
    std::vector<uint16_t> picDx, picDy;                     // +0x16, +0x1a (low bytes used)

    static constexpr uint16_t kSize = 0x1E;
    // Decodes the static part (footer, count, geometry) of the descriptor at `off`; the title,
    // texts and pictures are run-time data the owner fills in (1180:0000 Help_ShowTopic).
    static PagerDesc fromExe(const ExeTables& exe, uint16_t off);
};

// The static DlgDesc instances in DGROUP (code_1098-10d8.md §2.8), with a short handle each.
struct NamedDialog {
    std::string_view name;
    uint16_t offset;
};
std::span<const NamedDialog> staticDialogs();
// Accepts a handle ("exit"), a DGROUP offset in hex ("1533", "0x1533").
std::optional<uint16_t> findDialog(std::string_view nameOrOffset);

// Replaces the first "%s" of a sprintf-style template with `arg` (the two sign-in boxes 2129 and
// 21e6 are filled this way by their callers, e.g. sprintf(buf, DS:2106, name)). Other bytes are
// copied as they are. TODO(integration): replace with the rtl sprintf subset when it exists.
std::vector<uint8_t> formatPercentS(std::span<const uint8_t> fmt, std::span<const uint8_t> arg);

}  // namespace tmport::ui
