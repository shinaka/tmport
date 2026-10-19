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
// cannot corrupt memory; for valid calls the result is identical. The CGA path (a 2-bpp buffer
// converted at draw time) is not modelled: CGA is produced from the 4-bpp buffer at presentation
// (palettes.h).

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
    void setPixel(int x, int y, uint8_t c);
    // 320x200 logical colours, one byte per pixel, rows top to bottom.
    std::vector<uint8_t> logicalPixels() const;
    void clear(uint8_t fill = 0) { bytes_.fill(fill); }

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
    void noteDrawn(int x, int y, int widthBytes, int height) {
        if (colorFlags_ & 1) dirty_.addRect(x, y, height, widthBytes);
    }

    // --- primitives --------------------------------------------------------------------------------
    // 1000:169b Gfx_FillRect (SI x1, DI y1, CX x2, DX y2, AL fill): every byte of the inclusive
    // rectangle = fill (two pixels: high nibble left).
    void fillRect(int x1, int y1, int x2, int y2, uint8_t fill);
    // 1000:176c Gfx_XorRect [Gfx_SwapColor]: a colour REPLACE, not an XOR. Per byte, the left pixel
    // becomes the "to" word's high-byte high nibble where it equals the "from" word's high-byte high
    // nibble; the right pixel likewise with the low-byte low nibbles. Callers pass colour words of
    // four equal nibbles (0xFFFF, 0xAAAA, 0x3333, ...).
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
    // x, rows top..bottom; stops after bottom-top+1 rows, even inside a tile.
    void drawTileColumn(int x, int top, int bottom, const PanFile& pan, size_t mapWordIndex);

private:
    std::array<uint8_t, kRowBytes * kHeight> bytes_{};
    ClipWindow clip_ = kFullScreenClip;
    uint8_t colorFlags_ = 0;
    DirtyRegions dirty_;
};

// Present_Dirty (1000:13ae, per adapter 1000:144a / 1000:1526): copies every dirty span of `back`
// (both rows of its row pair) into `screen`, the picture the player sees, then clears the list.
void presentDirty(BackBuffer& back, BackBuffer& screen);
// Present_Full (1000:158c): copies the whole buffer.
void presentFull(const BackBuffer& back, BackBuffer& screen);

}  // namespace tmport::dos
