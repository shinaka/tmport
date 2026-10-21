#pragma once
// The original's offscreen buffer (re/docs/code_1058-1090.md §2.1, §4; ARCHITECTURE.md §3.1):
// 512 x 1055 bytes of palette indices. Rows 0..383 = screen (layer 0), 384..767 = back page
// (layer 1), 788..1054 = pristine play-field (layer 2). A global inclusive clip rectangle applies
// to keyed sprite draws only; a dirty-rectangle list (max 256, only while enabled) records the
// screen areas that must be presented.
//
// Every drawing routine mirrors one routine of segment 1058 (address in the comment). Routines
// that the original leaves unclipped are clipped here only to the buffer itself, so a bad call
// cannot corrupt memory; for valid calls the result is identical.

#include <array>
#include <cstdint>
#include <vector>

#include "gfx/pixmap.h"
#include "mischief/gfx/palette.h"
#include "mischief/gfx/rect.h"

namespace tmport::gfx {

// Inclusive clip rectangle (DGROUP 0864 top, 0866 bottom, 0868 left, 086a right).
struct ClipRect {
    int top = 0, bottom = 0x180, left = 0, right = 0x200;  // initial DGROUP values
    friend bool operator==(const ClipRect&, const ClipRect&) = default;
};

class FrameBuffer {
public:
    static constexpr int kWidth = 512;
    static constexpr int kHeight = 1055;
    static constexpr int kScreenHeight = 384;
    static constexpr int kLayer1Y = 384;  // back page
    static constexpr int kLayer2Y = 788;  // pristine play-field (512 x 267)
    static constexpr int kPlayTop = 20;
    static constexpr int kPlayHeight = 267;
    static constexpr size_t kMaxDirtyRects = 256;

    explicit FrameBuffer(mischief::gfx::ColorMode mode = mischief::gfx::ColorMode::Colour256);

    mischief::gfx::ColorMode mode() const { return mode_; }
    uint8_t* pixels() { return px_.data(); }
    const uint8_t* pixels() const { return px_.data(); }
    uint8_t* row(int y) { return px_.data() + static_cast<size_t>(y) * kWidth; }
    const uint8_t* row(int y) const { return px_.data() + static_cast<size_t>(y) * kWidth; }
    uint8_t at(int x, int y) const { return px_[static_cast<size_t>(y) * kWidth + static_cast<size_t>(x)]; }

    // Colour state (set from the palette).
    void setKeys(const mischief::gfx::KeyTable& key, const mischief::gfx::KeyTable& whiteKey);
    void setPens(const std::array<uint8_t, 16>& pens) { pens_ = pens; }
    void setPalette(const mischief::gfx::Palette& p);  // keys + pens
    // Pal_SetForeColor (1048:02fd): EGA colour number used by fill/frame/line routines.
    void setForeColor(int ega) { foreColor_ = ega & 15; }
    int foreColor() const { return foreColor_; }
    uint8_t forePixel() const { return pens_[static_cast<size_t>(foreColor_)]; }

    // Clip rectangle.
    const ClipRect& clip() const { return clip_; }
    void setClip(const ClipRect& c) { clip_ = c; }
    void resetClip() { clip_ = {0, 0x17F, 0, 0x1FF}; }                         // 10c8:000c
    void setPlayClip(int yoff = 0) { clip_ = {yoff + 0x14, yoff + 0x11E, 0, 0x1FF}; }  // 10e0:036b
    void setFullClip() { clip_ = {0, 0x2FF, 0, 0x1FF}; }                        // 10e0:0391

    // Dirty rectangles (1058:068e / 0731 / 076f).
    void setDirtyEnabled(bool on) { dirtyEnabled_ = on; }
    bool dirtyEnabled() const { return dirtyEnabled_; }
    void addDirtyRect(int x, int y, int w, int h);
    void clearDirtyRects() { dirty_.clear(); }
    const std::vector<mischief::gfx::Rect>& dirtyRects() const { return dirty_; }

    // Fills the whole buffer with one index.
    void clear(uint8_t index);

    // --- PixMap drawing ------------------------------------------------------------------------
    void drawOpaque(int x, int y, const PixMap& pm);                    // 1058:0c78, unclipped
    void drawKeyed(int x, int y, const PixMap& pm);                     // 1058:0d43, clipped
    void drawKeyedMirrored(int x, int y, const PixMap& pm);             // 1058:114c, clipped
    void drawSprite(int x, int y, const PixMap& pm, bool mirror);       // 1058:1320, + dirty rect
    void drawOpaqueOrFlippedWhiteKey(int x, int y, const PixMap& pm, bool flag);  // 1058:0cf3

    // --- Rectangles ----------------------------------------------------------------------------
    void copyRect(int sx, int sy, int w, int h, int dx, int dy);        // 1058:0b6a, + dirty rect
    void fillRect(int x, int y, int w, int h);                          // 1058:097f, fore colour
    void fillRectColor(int x, int y, int w, int h, int ega);            // 1058:07d0
    void frameRect(int x, int y, int w, int h);                         // 1058:0b31, fore colour
    void frameRectColor(int x, int y, int w, int h, int ega);           // 1058:09b8
    void hline(int x, int y, int w);                                    // 1058:090f
    void vline(int x, int y, int h);                                    // 1058:0947
    void pixel(int x, int y);                                           // 1058:08d8

    // Save-under (1058:1643 / 1695): x is rounded down to even and w up to even.
    PixMap saveRect(int x, int y, int w, int h) const;
    void restoreRect(int x, int y, const PixMap& saved);

    // Reading highlight (1058:19b2): remaps the rectangle through the LUT (Palette::highlightLut).
    void highlightRect(int x, int y, int w, int h, const std::array<uint8_t, 256>& lut);

private:
    void blitClipped(int x, int y, const PixMap& pm, bool mirror, const mischief::gfx::KeyTable& key);
    void fillRaw(int x, int y, int w, int h, uint8_t value);  // clipped to the buffer

    mischief::gfx::ColorMode mode_;
    std::vector<uint8_t> px_;
    mischief::gfx::KeyTable key_;
    mischief::gfx::KeyTable whiteKey_;
    std::array<uint8_t, 16> pens_{};
    int foreColor_ = 0;
    ClipRect clip_;
    bool dirtyEnabled_ = false;
    std::vector<mischief::gfx::Rect> dirty_;
};

}  // namespace tmport::gfx
