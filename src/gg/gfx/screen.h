#pragma once
// The screen buffer of Gizmos & Gadgets and the drawing primitives of segments 1399 and 1473
// (re/gg/docs/code_runtime.md §10, §11.1; fmt_graphics.md §1, §2.4).
//
// Gfx_Init (1473:0074) allocates one 320x400 byte buffer and a table of 400 row pointers (DS:5f22,
// stride 320): rows 0..199 are the screen being composed, rows 200..399 the back page that holds the
// clean background (sprites are erased by copying from y + 200, Gfx_RestoreRect; a drawn background
// is saved with Gfx_SaveToBack). Screen keeps exactly that layout, so the original's coordinates work
// unchanged, and the state the primitives share: the clip rectangle DS:01e4..01ea, g_drawColor
// DS:01f4, the rectangle of the last LPX blit DS:01ec..01f2, the 40x25 dirty-tile map (1581) and the
// cursor hide state DS:01e2 with its call into the software cursor (Gfx_HideMouseOnce 1399:0b22).
//
// Every primitive keeps the original's exact clipping and dirty marks, including its quirks: the
// exclusive right/bottom of Gfx_ClipX/Gfx_ClipY and their wrong sign for a negative y, the
// unclipped Gfx_HLine draw, the keyed raw blit's inclusive clip, unsigned y test and 8-bit height,
// the unclipped RLE blits (8-bit row counters, no dirty mark at a negative x), the RLE literal-run
// carry that copies 256 extra bytes. The buffer is addressed linearly as in the original (row y + 1
// starts 320 bytes after row y), so a picture drawn past the right edge wraps into the next rows.
// Where the original would write outside the 128 000-byte buffer, run a zero count as 65 536, or
// read a row pointer outside its table, the engine writes nothing there and logs it in debug builds
// (PORTING_NOTES.md §3.1); no shipped data is known to do it.
//
// Pictures are LPX payloads (gg/gfx/lpx.h), drawn straight from their bytes; the caller keeps them
// alive while a sprite list or a cursor record refers to them.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tmport::gg {

// DS:01e4 left, DS:01e6 right, DS:01e8 top, DS:01ea bottom: inclusive values in the file.
struct ClipRect {
    int left = 0;
    int right = 319;
    int top = 0;
    int bottom = 199;
    friend bool operator==(const ClipRect&, const ClipRect&) = default;
};
inline constexpr ClipRect kFullScreenClip{};

// DS:01ec..01f2: the rectangle the last LPX blitter drew (g_blitX .. g_blitH); x = -1 after a fully
// clipped keyed raw blit (no dirty mark then).
struct BlitRect {
    int x = 0, y = 0, w = 0, h = 0;
    friend bool operator==(const BlitRect&, const BlitRect&) = default;
};

// VRAM A000:0000 (g_vram DS:01f8): the 320x200 picture the player sees. Only Dirty_Flush, the wipe
// and the movie write it (gg/gfx/video.h, transitions.h, movie.h).
using Vram = std::array<uint8_t, 64000>;

// A Gfx_GetImage buffer {u16 w, u16 h, w x h pixels} (Gfx_ImageSize = w * h + 4 bytes).
struct SavedImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

// The 40x25 map of dirty 8x8 tiles (in the original at 1581:0006, inside the code segment) and the
// count of marks since the last flush g_dirtyPending DS:08b6. Dirty_Flush is gg/gfx/sprites.h.
class DirtyMap {
public:
    static constexpr int kCols = 40;
    static constexpr int kRows = 25;
    static constexpr int kTiles = kCols * kRows;

    // 1581:03ee Dirty_Mark(x, y, w, h): nothing for y >= 200 (unsigned, so a negative y too);
    // first tile = (y >> 3) * 40 + (x >> 3) with x shifted unsigned; cols = ceil(((x & 7) + min(w,
    // 320)) / 8), rows = ceil(((y & 7) + h) / 8); a rectangle past column 39 wraps into the next tile
    // row. pending += 1. Tiles outside the map (negative x, past row 24) are not written (the
    // original writes into its own code there).
    void mark(int x, int y, int w, int h);
    // 1581:0470 Dirty_Clear: the map and the pending count.
    void clear();

    bool tile(int col, int row) const { return map_[static_cast<size_t>(row * kCols + col)] != 0; }
    void setTile(int col, int row, bool dirty) { map_[static_cast<size_t>(row * kCols + col)] = dirty ? 1 : 0; }
    int dirtyTiles() const;
    // g_dirtyPending DS:08b6 (Dirty_Flush does nothing while it is 0).
    int pending() const { return pending_; }
    void setPending(int n) { pending_ = n; }
    // Statistics of Dirty_Flush (no reader in the original): DS:08b8 tiles copied, DS:08bc flushes.
    uint32_t tilesCopied = 0;
    uint16_t flushes = 0;

private:
    std::array<uint8_t, kTiles> map_{};
    int pending_ = 0;
};

// The part of the software cursor that the primitives call (gg/gfx/cursor.h implements it).
class MouseUnder {
public:
    virtual ~MouseUnder() = default;
    // 17ab:072a Mouse_RestoreUnder: puts the saved background back over the cursor if it is shown.
    virtual void restoreUnder() = 0;
};

class Screen {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 200;   // screen rows 0..199
    static constexpr int kRows = 400;     // + the back page, rows 200..399
    static constexpr int kBackPage = 200; // row offset of the back page
    static constexpr size_t kBytes = static_cast<size_t>(kWidth) * kRows;

    Screen();

    // --- the buffer --------------------------------------------------------------------------------
    uint8_t* row(int y) { return bytes_.data() + static_cast<size_t>(y) * kWidth; }
    const uint8_t* row(int y) const { return bytes_.data() + static_cast<size_t>(y) * kWidth; }
    uint8_t pixel(int x, int y) const { return row(y)[x]; }
    std::span<uint8_t> bytes() { return bytes_; }
    std::span<const uint8_t> bytes() const { return bytes_; }
    // Rows 0..199 (64 000 bytes): what Dirty_Flush copies to VRAM.
    std::span<const uint8_t> screenPixels() const { return std::span<const uint8_t>(bytes_).first(64000); }
    std::span<const uint8_t> backPixels() const { return std::span<const uint8_t>(bytes_).subspan(64000); }

    // --- shared state ------------------------------------------------------------------------------
    const ClipRect& clip() const { return clip_; }
    void setClip(const ClipRect& c) { clip_ = c; }
    // g_drawColor DS:01f4: the colour of the lines, rectangles and the text underline.
    uint8_t drawColor() const { return drawColor_; }
    void setDrawColor(uint8_t c) { drawColor_ = c; }
    const BlitRect& lastBlit() const { return blit_; }
    DirtyMap& dirty() { return dirty_; }
    const DirtyMap& dirty() const { return dirty_; }

    // g_mouseHideState DS:01e2: 0 after a flush; the first primitive of a frame removes the cursor.
    int mouseHideState() const { return hideState_; }
    // The software cursor whose background the primitives restore (nullptr: none).
    void setMouseUnder(MouseUnder* m) { mouseUnder_ = m; }
    // 1399:0b22 Gfx_HideMouseOnce: state 0 becomes 1; at 1 Mouse_RestoreUnder runs and the state
    // becomes 2 (a nested call from the restore's Gfx_PutImage leaves 3; only zero / non-zero counts).
    void hideMouseOnce();
    // 1399:0b3e Gfx_ResetMouseHide.
    void resetMouseHide() { hideState_ = 0; }

    // 1473:0074 Gfx_Init, the buffer part: g_drawColor = 0, Gfx_FillRect(0, 0, 320, 200) (every tile
    // dirty) and Gfx_SaveToBack(0, 0, 320, 200).
    void init();

    // --- primitives of 1399 (all call Gfx_HideMouseOnce first, except where noted) ----------------
    // 1399:0072 Gfx_HLine: Gfx_ClipX only decides rejection; the unclipped x and len are marked
    // (x, y, len, 1) and drawn in g_drawColor.
    void hLine(int x, int y, int len);
    // 1399:00b3 Gfx_VLine: marks (x, y, 1, len) before clipping; y is clipped by Gfx_ClipY; x never.
    void vLine(int x, int y, int len);
    // 1399:0100 Gfx_FillRect: w x h of g_drawColor, no clipping, marks (x, y, w, h).
    void fillRect(int x, int y, int w, int h);
    // 1399:0146 Gfx_XorRect: every pixel ^= 0x0F, no clipping, marks.
    void xorRect(int x, int y, int w, int h);
    // 1399:0194 Gfx_GetImage: x and y clipped (Gfx_ClipX/ClipY applied; rejected only if x > right,
    // then `buf` is left as it was and the cursor is not touched); buf = {w', h', pixels}; no mark.
    void getImage(int x, int y, int w, int h, SavedImage& buf);
    // 1399:01f1 Gfx_PutImage: copies buf's w x h to (x, y), no clipping, marks (x, y, w, h).
    void putImage(int x, int y, const SavedImage& buf);
    // 1399:0233 Gfx_RestoreRect: back-page rows y + 200.. to rows y..; for x < 0: w += x, x = 0,
    // nothing if w < 0; marks (x, y, w, h) after that clip.
    void restoreRect(int x, int y, int w, int h);
    // 1399:02a2 Gfx_SaveToBack: rows y.. to back-page rows y + 200.., no clipping, no mark.
    void saveToBack(int x, int y, int w, int h);
    // 1399:0b45 Gfx_PutPixel: one pixel, marks (x, y, 1, 1) after drawing.
    void putPixel(int x, int y, uint8_t c);
    // 1399:0541 Gfx_DrawGlyph(x, y, glyph): a 1-bpp glyph {u8 full bytes per row, u8 bits in a last
    // partial byte, u16 rows, rows}, bit 0 = leftmost pixel; set bits in `colour` (g_glyphColor
    // DS:1f6c), clear bits left alone; no clipping, no mark (the font code marks).
    void drawGlyph(int x, int y, std::span<const uint8_t> glyph, uint8_t colour);
    // 1399:0335 Lpx_Draw(x, y, pic): marks (x, y, width, height) unclipped, then the blitter of the
    // format (DS:0204: 0 Lpx_BlitRaw, no clipping; 1 Lpx_BlitRle).
    void lpxDraw(int x, int y, std::span<const uint8_t> pic);
    // 1399:02ee Lpx_DrawKeyed(x, y, pic, mode): blitter DS:01fc[(format + mode) & 3] (0, 2
    // Lpx_BlitRawKeyed; 1 Lpx_BlitRle; 3 Lpx_BlitRleMirror), then Dirty_Mark(lastBlit()) if its x >= 0.
    // Mode 2 mirrors an RLE picture; modes 1 and 3 hand raw data to the RLE blitters and RLE data to
    // the raw one, as the original's table does (no caller is known to pass them; the blitters stop
    // at the end of the payload).
    void lpxDrawKeyed(int x, int y, std::span<const uint8_t> pic, int mode);

    // --- screen helpers of 1473 -----------------------------------------------------------------------
    // 1473:010d Gfx_FrameRect: HLine(x, y, w), VLine(x, y, h), HLine(x, y + h - 1, w), VLine(x + w - 1, y, h).
    void frameRect(int x, int y, int w, int h);
    // 1473:0337 Screen_SavePlayArea: Gfx_SaveToBack(0, 0, 320, 165).
    void savePlayArea() { saveToBack(0, 0, 320, 165); }
    // 1473:034e Screen_SaveAll: Gfx_SaveToBack(0, 0, 320, 200).
    void saveAll() { saveToBack(0, 0, 320, 200); }
    // 1473:0365 Screen_RestoreAll: Gfx_RestoreRect(0, 0, 320, 200).
    void restoreAll() { restoreRect(0, 0, 320, 200); }
    // 1473:037c Screen_ClearPlayArea: g_drawColor = 2, Gfx_FillRect(0, 9, 320, 155).
    void clearPlayArea();
    // 1473:040e Gfx_ImageSize(w, h) = w * h + 4 (16-bit).
    static int imageSize(int w, int h) { return static_cast<int16_t>(w * h + 4); }
    // 1473:03df Obj_DrawKeyed(rec): Lpx_DrawKeyed(x, y - (lift if flags & 0x1000), pic, flags) for the
    // large object records (rec+0 flags, +2 x, +4 y, +0x70 lift, +0xa picture).
    void objDrawKeyed(uint16_t flags, int x, int y, int lift, std::span<const uint8_t> pic);

private:
    // The linear byte at offset `off` of the buffer, or nullptr outside it (logged once per call site
    // in debug builds by the callers).
    uint8_t* at(long off) { return off >= 0 && off < static_cast<long>(kBytes) ? bytes_.data() + off : nullptr; }
    // Row y's linear offset, or -1 when y is outside the row table (the original reads garbage).
    static long rowOffset(int y) { return y >= 0 && y < kRows ? static_cast<long>(y) * kWidth : -1; }

    void blitRaw(int x, int y, std::span<const uint8_t> pic);
    void blitRawKeyed(int x, int y, std::span<const uint8_t> pic);
    void blitRle(int x, int y, std::span<const uint8_t> pic);
    void blitRleMirror(int x, int y, std::span<const uint8_t> pic);

    std::vector<uint8_t> bytes_;
    ClipRect clip_;
    uint8_t drawColor_ = 0;
    BlitRect blit_;
    DirtyMap dirty_;
    int hideState_ = 0;
    MouseUnder* mouseUnder_ = nullptr;
};

}  // namespace tmport::gg
