#include "gg/gfx/screen.h"

#include <algorithm>

#include "util/log.h"

namespace tmport::gg {

namespace {

// The LPX header fields the blitters read (fmt_graphics.md §2.1); nullopt-like `ok` false when the
// payload is shorter than the header.
struct LpxFields {
    bool ok = false;
    uint16_t format = 0, width = 0, height = 0, key = 0;
    size_t data = 0;  // index of the pixel data in the payload: 12 + data offset
};

LpxFields lpxFields(std::span<const uint8_t> p) {
    LpxFields f;
    if (p.size() < 14) return f;
    auto u16 = [&](size_t o) { return static_cast<uint16_t>(p[o] | (p[o + 1] << 8)); };
    f.ok = true;
    f.format = u16(4);
    f.width = u16(6);
    f.height = u16(8);
    f.key = u16(10);
    f.data = 12 + size_t{u16(12)};
    return f;
}

// 1399:0021 Gfx_ClipX (AX = x, CX = length): false (CF = 1) when x > right. For x < left the
// length grows by x (right only for left = 0); the right bound is read as exclusive.
bool clipX(const ClipRect& c, int& x, int& len) {
    if (x > c.right) return false;
    if (x < c.left) {
        len += x;
        x = c.left;
    }
    const int end = x + len;
    if (end > c.right) len -= end - (c.right - c.left);
    return true;
}

// 1399:004c Gfx_ClipY (BX = y, DX = height): never rejects; for y < top the height loses y (a
// negative y makes it longer); the bottom bound is read as exclusive.
void clipY(const ClipRect& c, int& y, int& h) {
    if (y < c.top) {
        h -= y;
        y = c.top;
    }
    const int end = y + h;
    if (end > c.bottom) h -= end - (c.bottom - c.top);
}

}  // namespace

// ------------------------------------------------------------------------------------- dirty map

// 1581:03ee Dirty_Mark
void DirtyMap::mark(int x, int y, int w, int h) {
    const auto uy = static_cast<uint16_t>(y);
    if (uy >= 200) return;
    const auto ux = static_cast<uint16_t>(x);
    const long first = static_cast<long>(uy >> 3) * kCols + (ux >> 3);
    const int width = static_cast<int16_t>(w) > 320 ? 320 : static_cast<int16_t>(w);
    const auto cx = static_cast<uint16_t>((ux & 7) + width);
    const auto dx = static_cast<uint16_t>((uy & 7) + h);
    const long cols = (cx >> 3) + ((cx & 7) ? 1 : 0);
    const long rows = (dx >> 3) + ((dx & 7) ? 1 : 0);
    // A negative width or height reaches the original's REP STOSB / DEC DX loop as a count near
    // 65 536 (a height of 0 too): it would overwrite the code segment. Nothing is marked then.
    if (static_cast<int16_t>(cx) < 0 || static_cast<int16_t>(dx) <= 0) {
        log::debug("Dirty_Mark({}, {}, {}, {}): count out of range, not marked", x, y, w, h);
    } else {
        bool outside = false;
        for (long r = 0; r < rows; ++r)
            for (long c = 0; c < cols; ++c) {
                const long i = first + r * kCols + c;
                if (i >= 0 && i < kTiles)
                    map_[static_cast<size_t>(i)] = 1;
                else
                    outside = true;
            }
        if (outside) log::debug("Dirty_Mark({}, {}, {}, {}) reaches past the tile map", x, y, w, h);
    }
    ++pending_;
}

// 1581:0470 Dirty_Clear
void DirtyMap::clear() {
    map_.fill(0);
    pending_ = 0;
}

int DirtyMap::dirtyTiles() const { return static_cast<int>(std::count(map_.begin(), map_.end(), uint8_t{1})); }

// ---------------------------------------------------------------------------------------- screen

Screen::Screen() : bytes_(kBytes, 0) {}

// 1399:0b22 Gfx_HideMouseOnce
void Screen::hideMouseOnce() {
    if (hideState_ == 0) ++hideState_;
    if (hideState_ == 1) {
        if (mouseUnder_) mouseUnder_->restoreUnder();
        ++hideState_;
    }
}

// 1473:0074 Gfx_Init (the buffer part)
void Screen::init() {
    drawColor_ = 0;
    fillRect(0, 0, kWidth, kHeight);
    saveToBack(0, 0, kWidth, kHeight);
}

// 1399:0072 Gfx_HLine
void Screen::hLine(int x, int y, int len) {
    int cx = x, cl = len;
    if (!clipX(clip_, cx, cl)) return;  // rejection only: the unclipped span is drawn
    hideMouseOnce();
    dirty_.mark(x, y, len, 1);
    const long base = rowOffset(y);
    if (base < 0 || len < 0) {
        log::debug("Gfx_HLine({}, {}, {}) outside the buffer", x, y, len);
        return;
    }
    for (int i = 0; i < len; ++i)
        if (uint8_t* d = at(base + x + i)) *d = drawColor_;
}

// 1399:00b3 Gfx_VLine
void Screen::vLine(int x, int y, int len) {
    hideMouseOnce();
    dirty_.mark(x, y, 1, len);
    int cy = y, cl = len;
    clipY(clip_, cy, cl);
    const long base = rowOffset(cy);
    if (base < 0 || cl <= 0) {  // a count of 0 or less would run the original's LOOP 65 536 times
        log::debug("Gfx_VLine({}, {}, {}) clipped to nothing or outside the buffer", x, y, len);
        return;
    }
    for (int i = 0; i < cl; ++i)
        if (uint8_t* d = at(base + x + static_cast<long>(i) * kWidth)) *d = drawColor_;
}

// 1399:0100 Gfx_FillRect
void Screen::fillRect(int x, int y, int w, int h) {
    hideMouseOnce();
    dirty_.mark(x, y, w, h);
    const long base = rowOffset(y);
    if (base < 0 || w < 0 || h <= 0) {
        log::debug("Gfx_FillRect({}, {}, {}, {}) outside the buffer or with a bad size", x, y, w, h);
        return;
    }
    for (int r = 0; r < h; ++r)
        for (int i = 0; i < w; ++i)
            if (uint8_t* d = at(base + x + static_cast<long>(r) * kWidth + i)) *d = drawColor_;
}

// 1399:0146 Gfx_XorRect
void Screen::xorRect(int x, int y, int w, int h) {
    hideMouseOnce();
    dirty_.mark(x, y, w, h);
    const long base = rowOffset(y);
    if (base < 0 || w <= 0 || h <= 0) {
        log::debug("Gfx_XorRect({}, {}, {}, {}) outside the buffer or with a bad size", x, y, w, h);
        return;
    }
    for (int r = 0; r < h; ++r)
        for (int i = 0; i < w; ++i)
            if (uint8_t* d = at(base + x + static_cast<long>(r) * kWidth + i)) *d ^= 0x0F;
}

// 1399:0194 Gfx_GetImage
void Screen::getImage(int x, int y, int w, int h, SavedImage& buf) {
    if (!clipX(clip_, x, w)) return;
    clipY(clip_, y, h);
    hideMouseOnce();
    buf.width = w;
    buf.height = h;
    const long base = rowOffset(y);
    if (base < 0 || w < 0 || h <= 0) {
        log::debug("Gfx_GetImage({}, {}, {}, {}) outside the buffer or with a bad size", x, y, w, h);
        buf.pixels.clear();
        return;
    }
    buf.pixels.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    for (int r = 0; r < h; ++r)
        for (int i = 0; i < w; ++i)
            if (const uint8_t* s = at(base + x + static_cast<long>(r) * kWidth + i))
                buf.pixels[static_cast<size_t>(r) * static_cast<size_t>(w) + static_cast<size_t>(i)] = *s;
}

// 1399:01f1 Gfx_PutImage
void Screen::putImage(int x, int y, const SavedImage& buf) {
    hideMouseOnce();
    const int w = buf.width, h = buf.height;
    dirty_.mark(x, y, w, h);
    const long base = rowOffset(y);
    if (base < 0 || w < 0 || h <= 0 || buf.pixels.size() < static_cast<size_t>(w) * static_cast<size_t>(h)) {
        log::debug("Gfx_PutImage({}, {}, {}x{}) outside the buffer or with a bad image", x, y, w, h);
        return;
    }
    for (int r = 0; r < h; ++r)
        for (int i = 0; i < w; ++i)
            if (uint8_t* d = at(base + x + static_cast<long>(r) * kWidth + i))
                *d = buf.pixels[static_cast<size_t>(r) * static_cast<size_t>(w) + static_cast<size_t>(i)];
}

// 1399:0233 Gfx_RestoreRect
void Screen::restoreRect(int x, int y, int w, int h) {
    hideMouseOnce();
    if (x < 0) {
        w += x;
        if (w < 0) return;
        x = 0;
    }
    dirty_.mark(x, y, w, h);
    const long dst = rowOffset(y), src = rowOffset(y + kBackPage);
    if (dst < 0 || src < 0 || h <= 0) {
        log::debug("Gfx_RestoreRect({}, {}, {}, {}) outside the row table or with a bad size", x, y, w, h);
        return;
    }
    for (int r = 0; r < h; ++r)
        for (int i = 0; i < w; ++i) {
            const long o = static_cast<long>(r) * kWidth + x + i;
            uint8_t* d = at(dst + o);
            const uint8_t* s = at(src + o);
            if (d && s) *d = *s;
        }
}

// 1399:02a2 Gfx_SaveToBack
void Screen::saveToBack(int x, int y, int w, int h) {
    hideMouseOnce();
    const long dst = rowOffset(y + kBackPage), src = rowOffset(y);
    if (dst < 0 || src < 0 || w < 0 || h <= 0) {
        log::debug("Gfx_SaveToBack({}, {}, {}, {}) outside the row table or with a bad size", x, y, w, h);
        return;
    }
    for (int r = 0; r < h; ++r)
        for (int i = 0; i < w; ++i) {
            const long o = static_cast<long>(r) * kWidth + x + i;
            uint8_t* d = at(dst + o);
            const uint8_t* s = at(src + o);
            if (d && s) *d = *s;
        }
}

// 1399:0b45 Gfx_PutPixel
void Screen::putPixel(int x, int y, uint8_t c) {
    hideMouseOnce();
    const long base = rowOffset(y);
    if (uint8_t* d = base < 0 ? nullptr : at(base + x))
        *d = c;
    else
        log::debug("Gfx_PutPixel({}, {}) outside the buffer", x, y);
    dirty_.mark(x, y, 1, 1);
}

// 1399:0541 Gfx_DrawGlyph
void Screen::drawGlyph(int x, int y, std::span<const uint8_t> g, uint8_t colour) {
    hideMouseOnce();
    if (g.size() < 4) return;
    const int full = g[0], bits = g[1], rows = g[2] | (g[3] << 8);
    const long base = rowOffset(y);
    const size_t stride = static_cast<size_t>(full) + (bits ? 1 : 0);
    if (base < 0 || rows == 0 || g.size() < 4 + stride * static_cast<size_t>(rows)) {
        log::debug("Gfx_DrawGlyph({}, {}) outside the buffer or with a short glyph", x, y);
        return;
    }
    size_t s = 4;
    for (int r = 0; r < rows; ++r) {
        long d = base + x + static_cast<long>(r) * kWidth;
        for (int b = 0; b < full; ++b) {
            const uint8_t v = g[s++];
            for (int k = 0; k < 8; ++k, ++d)
                if (v & (1 << k))
                    if (uint8_t* p = at(d)) *p = colour;
        }
        if (bits) {
            const uint8_t v = g[s++];
            for (int k = 0; k < bits; ++k, ++d)
                if (v & (1 << k))
                    if (uint8_t* p = at(d)) *p = colour;
        }
    }
}

// ---------------------------------------------------------------------------------- LPX blitters

// 1399:0335 Lpx_Draw
void Screen::lpxDraw(int x, int y, std::span<const uint8_t> pic) {
    hideMouseOnce();
    const LpxFields f = lpxFields(pic);
    if (!f.ok) {
        log::debug("Lpx_Draw: not a picture ({} bytes)", pic.size());
        return;
    }
    dirty_.mark(x, y, f.width, f.height);
    switch (f.format) {  // DS:0204[format]
        case 0: blitRaw(x, y, pic); break;
        case 1: blitRle(x, y, pic); break;
        default: log::debug("Lpx_Draw: format {} has no blitter", f.format); break;
    }
}

// 1399:02ee Lpx_DrawKeyed
void Screen::lpxDrawKeyed(int x, int y, std::span<const uint8_t> pic, int mode) {
    hideMouseOnce();
    const LpxFields f = lpxFields(pic);
    if (!f.ok) {
        log::debug("Lpx_DrawKeyed: not a picture ({} bytes)", pic.size());
        return;
    }
    switch ((f.format + mode) & 3) {  // DS:01fc
        case 0:
        case 2: blitRawKeyed(x, y, pic); break;
        case 1: blitRle(x, y, pic); break;
        default: blitRleMirror(x, y, pic); break;
    }
    if (blit_.x >= 0) dirty_.mark(blit_.x, blit_.y, blit_.w, blit_.h);
}

// 1399:036d Lpx_BlitRaw: width x height bytes, row stride 320, no clipping, 16-bit height.
void Screen::blitRaw(int x, int y, std::span<const uint8_t> pic) {
    const LpxFields f = lpxFields(pic);
    const long base = rowOffset(y);
    if (base < 0 || f.height == 0) {
        log::debug("Lpx_BlitRaw at ({}, {}): outside the row table or no rows", x, y);
        return;
    }
    size_t s = f.data;
    for (int r = 0; r < f.height; ++r) {
        const long d = base + x + static_cast<long>(r) * kWidth;
        for (int i = 0; i < f.width; ++i) {
            if (s >= pic.size()) {
                log::debug("Lpx_BlitRaw: picture data ends early");
                return;
            }
            const uint8_t c = pic[s++];
            if (uint8_t* p = at(d + i)) *p = c;
        }
    }
}

// 1399:0399 Lpx_BlitRawKeyed: clipped to the inclusive clip rectangle; y compared unsigned (a
// negative y is rejected); the height is the 8-bit DL (the key overwrites DH); pixels equal to the
// key's low byte are skipped.
void Screen::blitRawKeyed(int x, int y, std::span<const uint8_t> pic) {
    const LpxFields f = lpxFields(pic);
    int w = f.width;
    int rows = f.height & 0xFF;
    const uint8_t key = static_cast<uint8_t>(f.key & 0xFF);
    size_t s = f.data;
    auto reject = [&] { blit_.x = -1; };
    if (static_cast<uint16_t>(y) > static_cast<uint16_t>(clip_.bottom)) return reject();
    const int last = y + rows - 1;
    if (static_cast<uint16_t>(y) < static_cast<uint16_t>(clip_.top)) {  // 03cc: skip the rows above
        const int n = clip_.top - y;
        s += static_cast<size_t>(n) * static_cast<size_t>(w);
        y += n;
        rows -= n;
    }
    if (static_cast<uint16_t>(last) < static_cast<uint16_t>(clip_.top)) return reject();
    const int excess = static_cast<int16_t>(last - clip_.bottom);
    if (excess >= 0) rows -= excess;
    blit_.h = rows & 0xFF;
    blit_.y = y;
    int skip = 0;
    int avail = clip_.right - x;
    if (static_cast<int16_t>(avail) < 0) return reject();
    ++avail;
    if (static_cast<uint16_t>(w) > static_cast<uint16_t>(avail)) {
        skip = w - avail;
        w = avail;
    }
    const int left = clip_.left - x;
    if (left > 0) {
        if (static_cast<uint16_t>(w) <= static_cast<uint16_t>(left)) return reject();
        w -= left;
        x += left;
        s += static_cast<size_t>(left);
        skip += left;
    }
    blit_.x = x;
    blit_.w = w;
    const long base = rowOffset(y);
    if (base < 0 || (rows & 0xFF) == 0) {  // DL = 0 would draw 256 rows
        log::debug("Lpx_BlitRawKeyed at ({}, {}): no rows", x, y);
        return;
    }
    for (int r = 0; r < (rows & 0xFF); ++r) {
        const long d = base + x + static_cast<long>(r) * kWidth;
        for (int i = 0; i < w; ++i) {
            if (s >= pic.size()) {
                log::debug("Lpx_BlitRawKeyed: picture data ends early");
                return;
            }
            const uint8_t c = pic[s++];
            if (c != key)
                if (uint8_t* p = at(d + i)) *p = c;
        }
        s += static_cast<size_t>(skip);
    }
}

// 1399:0465 Lpx_BlitRle: per row, codes until an 8-bit remainder of the width reaches 0 (00 n
// skips n pixels, c copies c bytes); no clipping; 8-bit height. A literal longer than the remainder
// sets the carry that RCR CL,1 shifts into the word count: 256 more bytes are copied.
void Screen::blitRle(int x, int y, std::span<const uint8_t> pic) {
    const LpxFields f = lpxFields(pic);
    blit_ = {x, y, f.width, f.height};
    long d = rowOffset(y);
    const int rows = f.height & 0xFF;
    if (d < 0 || rows == 0) {
        log::debug("Lpx_BlitRle at ({}, {}): outside the row table or no rows", x, y);
        return;
    }
    d += x;
    const long rowStep = kWidth - f.width;  // BP = 0x140 - width
    const auto width8 = static_cast<uint8_t>(f.width);
    size_t s = f.data;
    for (int r = 0; r < rows; ++r) {
        uint8_t remain = width8;
        do {
            if (s >= pic.size()) {
                log::debug("Lpx_BlitRle: picture data ends early");
                return;
            }
            const uint8_t c = pic[s++];
            if (c == 0) {
                if (s >= pic.size()) return;
                const uint8_t n = pic[s++];
                d += n;
                remain = static_cast<uint8_t>(remain - n);
            } else {
                const bool borrow = remain < c;
                remain = static_cast<uint8_t>(remain - c);
                const int n = c + (borrow ? 256 : 0);
                for (int i = 0; i < n; ++i, ++d) {
                    if (s >= pic.size()) {
                        log::debug("Lpx_BlitRle: literal run past the picture data");
                        return;
                    }
                    const uint8_t v = pic[s++];
                    if (uint8_t* p = at(d)) *p = v;
                }
            }
        } while (remain != 0);
        d += rowStep;
    }
}

// 1399:04d1 Lpx_BlitRleMirror: as Lpx_BlitRle, starting at x + w - 1 and storing right to left;
// the next row at +320 + w.
void Screen::blitRleMirror(int x, int y, std::span<const uint8_t> pic) {
    const LpxFields f = lpxFields(pic);
    blit_ = {x, y, f.width, f.height};
    long d = rowOffset(y);
    const int rows = f.height & 0xFF;
    if (d < 0 || rows == 0) {
        log::debug("Lpx_BlitRleMirror at ({}, {}): outside the row table or no rows", x, y);
        return;
    }
    d += x + f.width - 1;
    const long rowStep = kWidth + f.width;  // BP = 0x140 + width
    const auto width8 = static_cast<uint8_t>(f.width);
    size_t s = f.data;
    for (int r = 0; r < rows; ++r) {
        uint8_t remain = width8;
        do {
            if (s >= pic.size()) {
                log::debug("Lpx_BlitRleMirror: picture data ends early");
                return;
            }
            const uint8_t c = pic[s++];
            if (c == 0) {
                if (s >= pic.size()) return;
                const uint8_t n = pic[s++];
                d -= n;
                remain = static_cast<uint8_t>(remain - n);
            } else {
                remain = static_cast<uint8_t>(remain - c);
                for (int i = 0; i < c; ++i, --d) {
                    if (s >= pic.size()) {
                        log::debug("Lpx_BlitRleMirror: literal run past the picture data");
                        return;
                    }
                    const uint8_t v = pic[s++];
                    if (uint8_t* p = at(d)) *p = v;
                }
            }
        } while (remain != 0);
        d += rowStep;
    }
}

// ------------------------------------------------------------------------------ 1473 helpers

// 1473:010d Gfx_FrameRect
void Screen::frameRect(int x, int y, int w, int h) {
    hLine(x, y, w);
    vLine(x, y, h);
    hLine(x, y + h - 1, w);
    vLine(x + w - 1, y, h);
}

// 1473:037c Screen_ClearPlayArea
void Screen::clearPlayArea() {
    drawColor_ = 2;
    fillRect(0, 9, 320, 155);
}

// 1473:03df Obj_DrawKeyed
void Screen::objDrawKeyed(uint16_t flags, int x, int y, int lift, std::span<const uint8_t> pic) {
    lpxDrawKeyed(x, (flags & 0x1000) ? y - lift : y, pic, flags);
}

}  // namespace tmport::gg
