#include "gfx/frame_buffer.h"

#include <algorithm>
#include <cstring>

namespace tmport::gfx {

FrameBuffer::FrameBuffer(ColorMode mode) : mode_(mode), px_(static_cast<size_t>(kWidth) * kHeight, 0) {
    for (size_t i = 0; i < 16; ++i) pens_[i] = static_cast<uint8_t>(i);
}

void FrameBuffer::setKeys(const KeyTable& key, const KeyTable& whiteKey) {
    key_ = key;
    whiteKey_ = whiteKey;
}

void FrameBuffer::setPalette(const Palette& p) {
    setKeys(p.keyTable(), p.whiteKeyTable());
    setPens(p.pens());
}

void FrameBuffer::clear(uint8_t index) { std::fill(px_.begin(), px_.end(), index); }

// 1058:068e Gfx_AddDirtyRect
void FrameBuffer::addDirtyRect(int x, int y, int w, int h) {
    if (!dirtyEnabled_ || dirty_.size() >= kMaxDirtyRects) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > kWidth) w = kWidth - x;
    if (y + h > kScreenHeight) h = kScreenHeight - y;
    if (w <= 0 || h <= 0) return;
    dirty_.push_back({x, y, w, h});
}

// 1058:0c78 PixMap_DrawOpaque: no clip rectangle, no dirty rect.
void FrameBuffer::drawOpaque(int x, int y, const PixMap& pm) {
    const int x0 = std::max(x, 0), x1 = std::min(x + pm.width, kWidth);
    if (x0 >= x1) return;
    for (int r = 0; r < pm.height; ++r) {
        const int dy = y + r;
        if (dy < 0 || dy >= kHeight) continue;
        std::memcpy(row(dy) + x0, pm.row(r) + (x0 - x), static_cast<size_t>(x1 - x0));
    }
}

// Shared body of 1058:0d43 (keyed) and 1058:114c (keyed, mirrored): clip to the inclusive clip
// rectangle, copy every source pixel that is not a key colour. Mirrored: destination column X
// takes source column (w-1) - (X - x).
void FrameBuffer::blitClipped(int x, int y, const PixMap& pm, bool mirror, const KeyTable& key) {
    int x0 = std::max(x, clip_.left), x1 = std::min(x + pm.width, clip_.right + 1);
    int y0 = std::max(y, clip_.top), y1 = std::min(y + pm.height, clip_.bottom + 1);
    // Memory safety only (the clip rectangle normally lies inside the buffer).
    x0 = std::max(x0, 0);
    x1 = std::min(x1, kWidth);
    y0 = std::max(y0, 0);
    y1 = std::min(y1, kHeight);
    if (x1 - x0 < 1 || y1 - y0 < 1) return;
    for (int Y = y0; Y < y1; ++Y) {
        const uint8_t* src = pm.row(Y - y);
        uint8_t* dst = row(Y);
        if (!mirror) {
            for (int X = x0; X < x1; ++X) {
                const uint8_t v = src[X - x];
                if (!key[v]) dst[X] = v;
            }
        } else {
            for (int X = x0; X < x1; ++X) {
                const uint8_t v = src[(pm.width - 1) - (X - x)];
                if (!key[v]) dst[X] = v;
            }
        }
    }
}

void FrameBuffer::drawKeyed(int x, int y, const PixMap& pm) { blitClipped(x, y, pm, false, key_); }

void FrameBuffer::drawKeyedMirrored(int x, int y, const PixMap& pm) { blitClipped(x, y, pm, true, key_); }

// 1058:1320 PixMap_DrawSprite
void FrameBuffer::drawSprite(int x, int y, const PixMap& pm, bool mirror) {
    addDirtyRect(x, y, pm.width, pm.height);
    blitClipped(x, y, pm, mirror, key_);
}

// 1058:0cf3 PixMap_DrawOpaqueOrFlippedWhiteKey: flag != 0 draws a mirrored sprite keyed on white.
void FrameBuffer::drawOpaqueOrFlippedWhiteKey(int x, int y, const PixMap& pm, bool flag) {
    if (!flag) {
        drawOpaque(x, y, pm);
        return;
    }
    addDirtyRect(x, y, pm.width, pm.height);
    blitClipped(x, y, pm, true, whiteKey_);
}

// 1058:0b6a Gfx_CopyOffscreenRect: dirty rect for the destination, no clipping, rows top to
// bottom and bytes left to right (overlap is not special-cased, as in the original).
void FrameBuffer::copyRect(int sx, int sy, int w, int h, int dx, int dy) {
    addDirtyRect(dx, dy, w, h);
    for (int r = 0; r < h; ++r) {
        const int ys = sy + r, yd = dy + r;
        if (ys < 0 || ys >= kHeight || yd < 0 || yd >= kHeight) continue;
        const uint8_t* src = row(ys);
        uint8_t* dst = row(yd);
        for (int c = 0; c < w; ++c) {
            const int xs = sx + c, xd = dx + c;
            if (xs < 0 || xs >= kWidth || xd < 0 || xd >= kWidth) continue;
            dst[xd] = src[xs];
        }
    }
}

void FrameBuffer::fillRaw(int x, int y, int w, int h, uint8_t value) {
    const int x0 = std::max(x, 0), x1 = std::min(x + w, kWidth);
    const int y0 = std::max(y, 0), y1 = std::min(y + h, kHeight);
    if (x0 >= x1 || y0 >= y1) return;
    for (int Y = y0; Y < y1; ++Y) std::memset(row(Y) + x0, value, static_cast<size_t>(x1 - x0));
}

// 1058:07d0 Gfx_FillRectColor. 256 colours: the WinG branch (1008:0418, the default
// configuration) fills [left,right) x [top,bottom). 16 colours: an empty width or height is
// widened to 1 before the nibble fill (1050:097c).
void FrameBuffer::fillRectColor(int x, int y, int w, int h, int ega) {
    const uint8_t v = pens_[static_cast<size_t>(ega & 15)];
    if (mode_ == ColorMode::Colour16) {
        if (w == 0) w = 1;
        if (h == 0) h = 1;
    }
    if (w <= 0 || h <= 0) return;
    fillRaw(x, y, w, h, v);
}

void FrameBuffer::fillRect(int x, int y, int w, int h) { fillRectColor(x, y, w, h, foreColor_); }
void FrameBuffer::hline(int x, int y, int w) { fillRectColor(x, y, w, 1, foreColor_); }
void FrameBuffer::vline(int x, int y, int h) { fillRectColor(x, y, 1, h, foreColor_); }
void FrameBuffer::pixel(int x, int y) { fillRectColor(x, y, 1, 1, foreColor_); }

// 1058:09b8 Gfx_FrameRectColor with RECT {x, y, x+w, y+h}.
void FrameBuffer::frameRectColor(int x, int y, int w, int h, int ega) {
    const uint8_t v = pens_[static_cast<size_t>(ega & 15)];
    if (mode_ == ColorMode::Colour16) {
        // Four nibble fills; top and left are one pixel longer than GDI's FrameRect.
        const int L = x, T = y, R = x + w, B = y + h;
        auto f = [&](int fx, int fy, int fw, int fh) {
            if (fw > 0 && fh > 0) fillRaw(fx, fy, fw, fh, v);
        };
        f(L, T, R - L + 1, 1);
        f(L, T, 1, B - T + 1);
        f(R - 1, T, 1, B - T);
        f(L, B - 1, R - L, 1);
        return;
    }
    // WinG_FrameRect (1008:050b): the bottom edge is drawn at top + max(1, h-2).
    auto put = [&](int px, int py) {
        if (px >= 0 && px < kWidth && py >= 0 && py < kHeight) row(py)[px] = v;
    };
    if (w > 0) fillRaw(x, y, w, 1, v);
    for (int i = 1; i <= h - 3; ++i) {
        put(x, y + i);
        put(x + w - 1, y + i);
    }
    if (w > 0) fillRaw(x, y + std::max(1, h - 2), w, 1, v);
}

void FrameBuffer::frameRect(int x, int y, int w, int h) { frameRectColor(x, y, w, h, foreColor_); }

// 1058:1643 Gfx_SaveRect
PixMap FrameBuffer::saveRect(int x, int y, int w, int h) const {
    if (x & 1) {
        --x;
        ++w;
    }
    if (w & 1) ++w;
    PixMap pm;
    pm.width = std::max(w, 0);
    pm.height = std::max(h, 0);
    pm.depth = mode_ == ColorMode::Colour16 ? 4 : 8;
    pm.encoding = PixEncoding::Memory;
    pm.pixels.assign(static_cast<size_t>(pm.width) * static_cast<size_t>(pm.height), 0);
    for (int r = 0; r < pm.height; ++r) {
        const int Y = y + r;
        if (Y < 0 || Y >= kHeight) continue;
        for (int c = 0; c < pm.width; ++c) {
            const int X = x + c;
            if (X < 0 || X >= kWidth) continue;
            pm.pixels[static_cast<size_t>(r) * static_cast<size_t>(pm.width) + static_cast<size_t>(c)] = at(X, Y);
        }
    }
    pm.consumed = pm.pixels.size() + kPmapHeaderSize;
    return pm;
}

// 1058:1695 Gfx_RestoreRect
void FrameBuffer::restoreRect(int x, int y, const PixMap& saved) { drawOpaque(x & ~1, y, saved); }

// 1058:19b2 Gfx_HighlightRect: unclipped LUT remap of a rectangle.
void FrameBuffer::highlightRect(int x, int y, int w, int h, const std::array<uint8_t, 256>& lut) {
    const int x0 = std::max(x, 0), x1 = std::min(x + w, kWidth);
    const int y0 = std::max(y, 0), y1 = std::min(y + h, kHeight);
    for (int Y = y0; Y < y1; ++Y) {
        uint8_t* d = row(Y);
        for (int X = x0; X < x1; ++X) d[X] = lut[d[X]];
    }
}

}  // namespace tmport::gfx
