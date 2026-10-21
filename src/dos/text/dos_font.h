#pragma once
// The only font of the 1990 DOS edition (re/dos/docs/fmt_graphics.md §6): 128 glyphs, 10 rows,
// 1 bpp, proportional, stored in the first code segment of SST.EXE at 1000:2447 (MOV SI,2447h at
// 1000:219a) and drawn by Font_DrawChar 1000:2180.
//
// Block: u8 unknown (7, never read); u8 last code (127); u8 height (10); u8 width[n] (advance and
// drawn width in pixels); u8 off_lo[n]; u8 off_hi[n]; glyph data from +3+3n: height rows of
// bytes-per-row bytes, MSB = leftmost pixel. Bytes per row come from the word table DS:3C5B indexed
// by the width (1 for 0..8, 2 for 9..16, ...). Codes 32..126 are ASCII; 1..30 are pictograms (key
// caps, crown, pre-drawn menu-bar labels).

#include <cstdint>
#include <span>
#include <vector>

#include "dos/data/dgroup_offsets.h"

namespace tmport::dos {

class BackBuffer;
class DosExe;

class DosFont {
public:
    static constexpr uint16_t kCodeSegment = 0x1000;
    static constexpr uint16_t kFontOffset = 0x2447;   // 1000:2447
    static constexpr uint16_t kDsBytesPerRow = dg::kFontRowBytes;  // word table indexed by glyph width
    static constexpr uint16_t kMeasureOnly = 1;        // Font_DrawChar colour word that only measures

    // Reads the font from SST.EXE (checks the MOV SI,2447h of the glyph blitter); throws
    // mischief::FormatError if the image does not hold it there.
    static DosFont fromExe(const DosExe& exe);
    // A font block in the layout above with a bytes-per-row table indexed by width (tests).
    static DosFont fromBlock(std::span<const uint8_t> block, std::span<const uint16_t> bytesPerRowByWidth);

    int count() const { return static_cast<int>(widths_.size()); }  // 128
    int height() const { return height_; }                            // 10
    uint8_t unknown0() const { return unknown0_; }                    // 7
    // Width in pixels (0 for a code past the table).
    int width(int code) const { return code >= 0 && code < count() ? widths_[static_cast<size_t>(code)] : 0; }
    // Font_DrawChar's return value: the advance in bytes, width >> 1 (text moves in 2-pixel steps).
    int advance(int code) const { return width(code) >> 1; }
    int bytesPerRow(int code) const { return code >= 0 && code < count() ? bpr_[static_cast<size_t>(code)] : 0; }
    // height * bytesPerRow bytes of glyph data.
    std::span<const uint8_t> glyph(int code) const;

    // 1000:2180 Font_DrawChar (AX colour word, BX x bytes, CX y, DX code): every glyph byte expands to
    // two words of four pixel nibbles (table 2005:8900, F per set bit); each word becomes
    // (dst & ~mask) | (colour & mask), so set bits take the colour word's nibble for that position
    // (byte 0: high, low nibble of the low byte; byte 1: high, low nibble of the high byte) and clear
    // bits keep the background. Whole bytes of 8 pixels are written, even past the glyph's width, and
    // linearly: a glyph running past byte 159 continues at the start of the next row, as in the
    // original. Records the dirty rectangle (x, y, width >> 1, height) when DS:37AE bit 0 is set.
    // Colour word 1 only measures. Returns advance(code). On a CGA buffer the glyph's CGA pixels
    // follow 1000:2295 (BackBuffer::enableCga; the colour pattern turns by one pixel per glyph row).
    int drawChar(BackBuffer& buf, uint16_t colour, int x, int y, int code) const;

    // The 16-word expansion table 2005:8900 as Font_DrawChar uses it (index = 4 font bits).
    static uint16_t expandNibble(int bits);

private:
    uint8_t unknown0_ = 0;
    int height_ = 0;
    std::vector<uint8_t> widths_;
    std::vector<int> bpr_;
    std::vector<size_t> offsets_;  // glyph start in data_
    std::vector<uint8_t> data_;
};

}  // namespace tmport::dos
