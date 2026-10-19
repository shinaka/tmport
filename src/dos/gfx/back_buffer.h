#pragma once
// The 320x200 back buffer of the 1990 DOS edition and the drawing primitives of code segment 1000
// that work on it (re/dos/docs/fmt_graphics.md §2, code_world.md §8.7, code_ui.md §2.5).
//
// The game composes every frame in segment 2005: 160 bytes x 200 rows of packed 4-bpp logical
// colours (two pixels per byte, high nibble = left pixel), row y at 2005:0010 + y*0xA0. BackBuffer
// keeps exactly that layout, so every routine works on the same bytes as the original; x
// coordinates are BYTES (0..159, 2 pixels each) and rectangles are inclusive, as in the original.
// It also holds the state those routines share: the clip window of the sprite blitter
// (DS:37DA..37E0), DS:37AE (bit 0: record dirty rectangles, Gfx_SetColor 1000:18af) and the
// dirty-span list that Present_Dirty copies to the screen.
//
// Routines that the original leaves unclipped are clipped here only to the buffer, so a bad call
// cannot corrupt memory; for valid calls the result is identical.
//
// The CGA look (fmt_graphics.md §2.4). In CGA mode the original's buffer is 2 bpp: every routine
// converts the 4-bpp colours it draws through the XLAT tables of Gfx_BuildCgaColorMap (1000:1087),
// each with its own row phase (the tiles alternate from their first row, the fills always use the
// even tables, the sprites alternate from their first drawn row, the font turns the colour by one
// pixel per glyph row), and Gfx_ReplaceColorRect compares and replaces 2-bit pixels. After
// enableCga() the buffer keeps that 2-bpp picture next to the 4-bpp one (the "CGA plane"): each
// primitive writes the value the original's CGA path writes, the scrolls, the presents and the
// save-unders move it with the bytes. A plane entry belongs to the 4-bpp colour it was written for;
// a pixel that no CGA-aware routine set, or whose colour was changed by a raw byte writer, shows
// the tile rule (Tile_DrawColumn from an even row: pixels a b c d of the dither on even rows, b a d c
// on odd ones). Raw writers report their rectangle with noteDrawn().

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace tmport::dos {

class PanFile;

// Clip window of Sprite_Draw / Sprite_SaveUnder: x in bytes, y in rows, inclusive.
struct ClipWindow {
    int xMin = 0, xMax = 0x9F, yMin = 0, yMax = 0xC7;
    friend bool operator==(const ClipWindow&, const ClipWindow&) = default;
};
inline constexpr ClipWindow kFullScreenClip{0, 0x9F, 0, 0xC7};  // title mode, pointer frames, after a sorted draw
inline constexpr ClipWindow kPlayfieldClip{2, 0x9D, 0x10, 0x8F};  // actors in play (4306:04e7)

// The dirty-span lists at DS:154C: one sorted span list per pair of rows (100 pairs), spans with
// even byte bounds. Present copies each span for both rows of its pair. Kept as a bitmap of
// 2-byte units per row pair: the original's merging (1000:122f) keeps the union of the spans,
// which is all that the presentation reads.
class DirtyRegions {
public:
    static constexpr int kRowPairs = 100;
    static constexpr int kUnits = 80;  // 2-byte units per row

    // 1000:1181 Dirty_AddRect, register convention: AX = x (bytes), BX = y, CX = height (rows),
    // DX = width (bytes). y is rounded down to even, the height up to whole row pairs; x is rounded
    // down to even and the end (x + width) up to even.
    void addRect(int x, int y, int height, int width);
    // 1000:138d Dirty_Clear.
    void clear() { units_ = {}; }
    bool empty() const;
    bool isDirty(int xByte, int y) const;
    // The spans of row pair p (rows 2p and 2p+1) as [first byte, end byte) ranges, ascending.
    std::vector<std::pair<int, int>> spans(int rowPair) const;

private:
    std::array<std::bitset<kUnits>, kRowPairs> units_{};
};

class BackBuffer {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 200;
    static constexpr int kRowBytes = 160;

    BackBuffer() { bytes_.fill(0); }

    // Raw access. A writer that changes pixels through it reports the rectangle with noteDrawn() (the
    // CGA plane then shows the tile rule there).
    std::span<uint8_t> bytes() { return bytes_; }
    std::span<const uint8_t> bytes() const { return bytes_; }
    uint8_t* row(int y) { return bytes_.data() + static_cast<size_t>(y) * kRowBytes; }
    const uint8_t* row(int y) const { return bytes_.data() + static_cast<size_t>(y) * kRowBytes; }
    uint8_t byteAt(int xb, int y) const { return row(y)[xb]; }
    // Logical colour of pixel (x, y), x in pixels.
    uint8_t pixel(int x, int y) const {
        const uint8_t b = row(y)[x >> 1];
        return (x & 1) ? (b & 15) : (b >> 4);
    }
    void setPixel(int x, int y, uint8_t c);  // the CGA plane shows the tile rule there
    // 320x200 logical colours, one byte per pixel, rows top to bottom.
    std::vector<uint8_t> logicalPixels() const;
    void clear(uint8_t fill = 0);

    // --- the CGA picture (see the header comment) ----------------------------------------------
    // Turns the CGA plane on with the look's dither table (DS:0174, LookPalette::cgaDither): from
    // now on every primitive keeps the 2-bpp picture the original's CGA path would hold.
    void enableCga(const std::array<uint8_t, 16>& dither);
    bool cgaEnabled() const { return !cga_.empty(); }
    const std::array<uint8_t, 16>& cgaDither() const { return dither_; }
    // Pixel p (0..3, from the left) of logical colour c's dither entry: the 2-bit CGA colour.
    uint8_t cgaPattern(uint8_t c, int p) const { return static_cast<uint8_t>((dither_[c & 15] >> (6 - 2 * (p & 3))) & 3); }
    // The 2-bit CGA pixel (x, y) (pixels) of the original's buffer; the tile rule where no routine
    // set it. Requires cgaEnabled().
    uint8_t cgaPixel(int x, int y) const;
    // Sets the CGA value of pixel (x, y) for the colour the 4-bpp pixel has now (the drawing routines
    // outside this class: the blitter, the font). Ignored without CGA or off the buffer.
    void cgaSet(int x, int y, uint8_t value);
    // The plane entry of a pixel (0 = tile rule; else 0x80 | colour << 2 | value), for copies.
    uint8_t cgaEntry(int x, int y) const { return cga_[static_cast<size_t>(y) * kWidth + x]; }
    void setCgaEntry(int x, int y, uint8_t e) { cga_[static_cast<size_t>(y) * kWidth + x] = e; }

    // --- state shared by the segment-1000 routines -----------------------------------------------
    // Default: the full screen (the original's DGROUP words start at 0 and are first set by
    // Sprites_DrawSorted; nothing draws a sprite before that).
    const ClipWindow& clip() const { return clip_; }
    void setClip(const ClipWindow& c) { clip_ = c; }
    // 1000:18af Gfx_SetColor: DS:37AE; while bit 0 is set every routine adds the rectangle it drew
    // to the dirty list (the common tail 1000:1911 / 1000:16e4).
    void setColor(uint8_t flags) { colorFlags_ = flags; }
    uint8_t color() const { return colorFlags_; }
    DirtyRegions& dirty() { return dirty_; }
    const DirtyRegions& dirty() const { return dirty_; }
    // The common tail: Dirty_AddRect(x, y, height, width) when DS:37AE bit 0 is set.
    void recordDirty(int x, int y, int widthBytes, int height) {
        if (colorFlags_ & 1) dirty_.addRect(x, y, height, widthBytes);
    }
    // For a routine outside this class that wrote the rectangle's bytes directly (camera.cpp's
    // Tile_DrawColumn on the two-PAN buffers): the common tail, and the CGA plane of the rectangle
    // falls back to the tile rule (Tile_DrawColumn's own CGA conversion from an even first row).
    void noteDrawn(int x, int y, int widthBytes, int height);

    // --- primitives --------------------------------------------------------------------------------
    // 1000:169b Gfx_FillRect (SI x1, DI y1, CX x2, DX y2, AL fill): every byte of the inclusive
    // rectangle = fill (two pixels: high nibble left). CGA (1000:1707): the even tables on every row,
    // so a pixel shows dither pixel x mod 4 of its colour whatever its row.
    void fillRect(int x1, int y1, int x2, int y2, uint8_t fill);
    // 1000:176c Gfx_XorRect [Gfx_SwapColor]: a colour REPLACE, not an XOR. Per byte, the left pixel
    // becomes the "to" word's high-byte high nibble where it equals the "from" word's high-byte high
    // nibble; the right pixel likewise with the low-byte low nibbles. Callers pass colour words of
    // four equal nibbles (0xFFFF, 0xAAAA, 0x3333, ...). CGA (1000:17d9): every 2-bit pixel equal to
    // dither pixel a of the "from" colour (its low byte through the even tables) becomes dither pixel
    // b of the "to" colour (its low byte through the tables 7F00 and 8000), whatever its position; a
    // rectangle starting at an odd byte enters the loop with the compare one pixel out of step (see
    // cgaReplace in the .cpp), as the original does.
    void replaceColor(int x1, int y1, int x2, int y2, uint16_t from, uint16_t to);
    // 1000:18b3 Scroll_PlayfieldLeft (DI top, DX bottom): content of rows top..bottom moves left by 4
    // bytes (bytes 6..0x9D -> 2..0x99) or, with g_slowScroll (DS:3D31), by 8 (0x0A..0x9D -> 2..0x95).
    void scrollPlayfieldLeft(int top, int bottom, bool slowScroll);
    // 1000:196d Scroll_PlayfieldRight: moves right by 4 bytes (2..0x99 -> 6..0x9D), or by 8 with slow
    // scroll (bytes -2..0x95 -> 6..0x9D: the first word comes from the end of the row above and is
    // overwritten by the second new column).
    void scrollPlayfieldRight(int top, int bottom, bool slowScroll);
    // 1000:1a86 Scroll_PlayfieldUp: rows top+8..bottom (slow: +16) -> top.., bytes 2..0x9D.
    void scrollPlayfieldUp(int top, int bottom, bool slowScroll);
    // 1000:1aef Scroll_PlayfieldDown: rows top..bottom-8 (slow: -16) -> top+8.., copied from the
    // bottom up; its dirty rectangle is the full width (x 0, 0xA0 bytes).
    void scrollPlayfieldDown(int top, int bottom, bool slowScroll);
    // 1000:19cf Tile_DrawColumn (SI x, DI top, DX bottom, CX:BX map pointer): from the map word at
    // `mapWordIndex` of the loaded PAN downwards, copies each addressed 8x8 tile (4 bytes per row) to
    // x, rows top..bottom; stops after bottom-top+1 rows, even inside a tile. CGA (1000:1a33): the
    // even tables for row `top`, then alternating.
    void drawTileColumn(int x, int top, int bottom, const PanFile& pan, size_t mapWordIndex);

private:
    // Moves `count` plane entries from linear pixel `from` to `to` (memmove; the scrolls).
    void cgaMove(ptrdiff_t to, ptrdiff_t from, size_t count);
    void cgaReplace(int x1, int y1, int x2, int y2, uint16_t from, uint16_t to, const std::vector<uint8_t>& before);

    std::array<uint8_t, kRowBytes * kHeight> bytes_{};
    ClipWindow clip_ = kFullScreenClip;
    uint8_t colorFlags_ = 0;
    DirtyRegions dirty_;
    std::vector<uint8_t> cga_;  // per pixel, empty without CGA (see cgaEntry)
    std::array<uint8_t, 16> dither_{};
};

// Present_Dirty (1000:13ae, per adapter 1000:144a / 1000:1526, CGA 1000:13b1): copies every dirty
// span of `back` (both rows of its row pair) into `screen`, the picture the player sees, then clears
// the list. The CGA plane goes with the bytes.
void presentDirty(BackBuffer& back, BackBuffer& screen);
// Present_Full (1000:158c): copies the whole buffer.
void presentFull(const BackBuffer& back, BackBuffer& screen);

}  // namespace tmport::dos
