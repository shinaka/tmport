#include "dos/gfx/back_buffer.h"

#include <algorithm>
#include <cstring>

#include "dos/gfx/pan.h"

namespace tmport::dos {

// ------------------------------------------------------------------------------- dirty spans

void DirtyRegions::addRect(int x, int y, int height, int width) {
    // 1000:1181: BP = (height | y) & 1; y &= ~1; pairs = height/2 + BP; if x is odd the width grows
    // by one, then up to even; the span is [x & 0xFE, (x & 0xFE) + width) in bytes.
    const int pairs = (height >> 1) + ((height | y) & 1);
    if (x & 1) ++width;
    if (width & 1) ++width;
    x &= 0xFE;
    const int end = x + width;
    const int firstUnit = std::max(0, x / 2), endUnit = std::min(kUnits, end / 2);
    const int firstPair = y >> 1;
    for (int p = std::max(0, firstPair); p < std::min(kRowPairs, firstPair + pairs); ++p)
        for (int u = firstUnit; u < endUnit; ++u) units_[static_cast<size_t>(p)].set(static_cast<size_t>(u));
}

bool DirtyRegions::empty() const {
    return std::none_of(units_.begin(), units_.end(), [](const auto& b) { return b.any(); });
}

bool DirtyRegions::isDirty(int xByte, int y) const {
    if (xByte < 0 || xByte >= 2 * kUnits || y < 0 || y >= 2 * kRowPairs) return false;
    return units_[static_cast<size_t>(y >> 1)].test(static_cast<size_t>(xByte >> 1));
}

std::vector<std::pair<int, int>> DirtyRegions::spans(int rowPair) const {
    std::vector<std::pair<int, int>> out;
    const auto& b = units_[static_cast<size_t>(rowPair)];
    for (int u = 0; u < kUnits;) {
        if (!b.test(static_cast<size_t>(u))) {
            ++u;
            continue;
        }
        const int start = u;
        while (u < kUnits && b.test(static_cast<size_t>(u))) ++u;
        out.emplace_back(start * 2, u * 2);
    }
    return out;
}

// ----------------------------------------------------------------------------------- buffer

void BackBuffer::setPixel(int x, int y, uint8_t c) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
    uint8_t& b = row(y)[x >> 1];
    b = (x & 1) ? static_cast<uint8_t>((b & 0xF0) | (c & 15)) : static_cast<uint8_t>((b & 0x0F) | (c << 4));
    if (cgaEnabled()) setCgaEntry(x, y, 0);
}

void BackBuffer::clear(uint8_t fill) {
    bytes_.fill(fill);
    std::fill(cga_.begin(), cga_.end(), uint8_t{0});
}

// ------------------------------------------------------------------------------- CGA plane

void BackBuffer::enableCga(const std::array<uint8_t, 16>& dither) {
    dither_ = dither;
    cga_.assign(static_cast<size_t>(kWidth) * kHeight, 0);
}

uint8_t BackBuffer::cgaPixel(int x, int y) const {
    const uint8_t c = pixel(x, y);
    const uint8_t e = cgaEntry(x, y);
    if ((e & 0x80) && ((e >> 2) & 15) == c) return e & 3;
    return cgaPattern(c, (x & 3) ^ (y & 1));  // the tile rule
}

void BackBuffer::cgaSet(int x, int y, uint8_t value) {
    if (!cgaEnabled() || x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
    setCgaEntry(x, y, static_cast<uint8_t>(0x80 | (pixel(x, y) << 2) | (value & 3)));
}

void BackBuffer::noteDrawn(int x, int y, int widthBytes, int height) {
    recordDirty(x, y, widthBytes, height);
    if (!cgaEnabled()) return;
    for (int yy = std::max(y, 0); yy < std::min(y + height, kHeight); ++yy)
        for (int xx = std::max(2 * x, 0); xx < std::min(2 * (x + widthBytes), kWidth); ++xx) setCgaEntry(xx, yy, 0);
}

void BackBuffer::cgaMove(ptrdiff_t to, ptrdiff_t from, size_t count) {
    if (!cgaEnabled()) return;
    const ptrdiff_t n = static_cast<ptrdiff_t>(cga_.size());
    // Entries before the buffer (the slow right scroll's first word of row 0) are taken as unset.
    std::vector<uint8_t> tmp(count, 0);
    for (size_t i = 0; i < count; ++i) {
        const ptrdiff_t s = from + static_cast<ptrdiff_t>(i);
        if (s >= 0 && s < n) tmp[i] = cga_[static_cast<size_t>(s)];
    }
    for (size_t i = 0; i < count; ++i) {
        const ptrdiff_t d = to + static_cast<ptrdiff_t>(i);
        if (d >= 0 && d < n) cga_[static_cast<size_t>(d)] = tmp[i];
    }
}

std::vector<uint8_t> BackBuffer::logicalPixels() const {
    std::vector<uint8_t> px(static_cast<size_t>(kWidth) * kHeight);
    for (size_t i = 0; i < bytes_.size(); ++i) {
        px[2 * i] = bytes_[i] >> 4;
        px[2 * i + 1] = bytes_[i] & 15;
    }
    return px;
}

void BackBuffer::fillRect(int x1, int y1, int x2, int y2, uint8_t fill) {
    recordDirty(x1, y1, x2 - x1 + 1, y2 - y1 + 1);  // the tail at 1000:16e4 uses the unclipped values
    const int xa = std::max(x1, 0), xb = std::min(x2, kRowBytes - 1);
    for (int y = std::max(y1, 0); y <= std::min(y2, kHeight - 1); ++y) {
        if (xa > xb) continue;
        std::memset(row(y) + xa, fill, static_cast<size_t>(xb - xa + 1));
        // 1000:1707: the fill byte through 7E00 and 7F00 on every row (an odd first byte keeps the
        // high half of its CGA byte, an odd end the low half: pixel for pixel the same rule).
        if (cgaEnabled())
            for (int x = 2 * xa; x <= 2 * xb + 1; ++x)
                cgaSet(x, y, cgaPattern(static_cast<uint8_t>((x & 1) ? (fill & 15) : (fill >> 4)), x & 3));
    }
}

void BackBuffer::replaceColor(int x1, int y1, int x2, int y2, uint16_t from, uint16_t to) {
    recordDirty(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
    // CGA: the 2-bit pixels as they are before the 4-bpp replace (the original has only those).
    std::vector<uint8_t> before;
    if (cgaEnabled()) {
        before.assign(static_cast<size_t>(kWidth) * kHeight, 0);
        for (int y = std::max(y1, 0); y <= std::min(y2, kHeight - 1); ++y)
            for (int x = 0; x < kWidth; ++x) before[static_cast<size_t>(y) * kWidth + x] = cgaPixel(x, y);
    }
    // 1000:179d: AX &= 0xF00F, BX &= 0xF00F; the high byte's high nibble is compared with the left
    // pixel, the low byte's low nibble with the right pixel.
    const uint8_t fromHi = static_cast<uint8_t>((from >> 8) & 0xF0), fromLo = static_cast<uint8_t>(from & 0x0F);
    const uint8_t toHi = static_cast<uint8_t>((to >> 8) & 0xF0), toLo = static_cast<uint8_t>(to & 0x0F);
    for (int y = std::max(y1, 0); y <= std::min(y2, kHeight - 1); ++y)
        for (int x = std::max(x1, 0); x <= std::min(x2, kRowBytes - 1); ++x) {
            uint8_t& b = row(y)[x];
            uint8_t hi = b & 0xF0, lo = b & 0x0F;
            if (hi == fromHi) hi = toHi;
            if (lo == fromLo) lo = toLo;
            b = static_cast<uint8_t>(hi | lo);
        }
    if (cgaEnabled()) cgaReplace(x1, y1, x2, y2, from, to, before);
}

// 1000:17d9, the CGA branch of Gfx_ReplaceColorRect, on the 2-bpp picture `before` (whole CGA bytes
// of four pixels, row stride kWidth pixels). The compare value DH is pixel a of the "from" colour's
// low byte through the even tables 7E00/7F00, the new value DL pixel b of the "to" colour's low byte
// through 7F00 and 8000 (BH is still 7F when the second conversion starts); both are cut to their
// top two bits (AND DX,C0C0) and rotated by one pixel (2 bits) per pixel step, so every pixel is
// compared with the same a and replaced by the same b. An odd first byte rotates them by two pixels
// and then enters the loop at its pixel-2 step, which rotates once more: from there on the compare
// looks one pixel ahead (a match then needs a zero pixel and a zero `a`), and the registers carry
// that offset into the following rows, which start without reloading them.
void BackBuffer::cgaReplace(int x1, int y1, int x2, int y2, uint16_t from, uint16_t to, const std::vector<uint8_t>& before) {
    auto table = [&](int page, uint8_t b) -> uint8_t {
        const uint8_t th = dither_[b >> 4], tl = dither_[b & 15];
        switch (page) {
            case 0x7E: return static_cast<uint8_t>((th & 0xC0) | (tl & 0x30));
            case 0x7F: return static_cast<uint8_t>((th & 0x0C) | (tl & 0x03));
            default: return static_cast<uint8_t>(((th & 0x30) << 2) | ((tl & 0xC0) >> 2));  // 0x80
        }
    };
    auto ror = [](uint8_t v, int n) { return static_cast<uint8_t>((v >> n) | (v << (8 - n))); };
    const uint8_t fb = static_cast<uint8_t>(from & 0xFF), tb = static_cast<uint8_t>(to & 0xFF);
    uint8_t dh = static_cast<uint8_t>((table(0x7E, fb) | table(0x7F, fb)) & 0xC0);
    uint8_t dl = static_cast<uint8_t>((table(0x7F, tb) | table(0x80, tb)) & 0xC0);
    const int width = x2 - x1 + 1;
    for (int y = y1; y <= y2; ++y) {
        if (y < 0 || y >= kHeight) continue;  // the original would write outside the buffer
        auto readByte = [&](int k) -> uint8_t {
            uint8_t v = 0;
            for (int p = 0; p < 4; ++p) {
                const int x = 4 * k + p;
                v = static_cast<uint8_t>((v << 2) | (x >= 0 && x < kWidth ? before[static_cast<size_t>(y) * kWidth + x] : 0));
            }
            return v;
        };
        auto writeByte = [&](int k, uint8_t v) {
            for (int p = 0; p < 4; ++p) cgaSet(4 * k + p, y, static_cast<uint8_t>((v >> (6 - 2 * p)) & 3));
        };
        int di = x1 >> 1;
        int cx = width;
        uint8_t ah = 0, al = 0;
        bool enterMid = (x1 & 1) != 0;
        if (enterMid) {  // 1000:181b
            dl = ror(dl, 4);
            dh = ror(dh, 4);
            ah = static_cast<uint8_t>(readByte(di) & 0xF0);
        }
        for (;;) {
            if (!enterMid) {  // 1000:1834: pixels 0 and 1
                const uint8_t cur = readByte(di);
                al = cur & 0xC0;
                if (al == dh) al = dl;
                ah = al;
                dl = ror(dl, 2);
                dh = ror(dh, 2);
                al = cur & 0x30;
                if (al == dh) al = dl;
                ah |= al;
                if (--cx == 0) {  // 1000:1891: the row ends inside the byte (ROL by one pixel)
                    dl = ror(dl, 6);
                    dh = ror(dh, 6);
                    writeByte(di, static_cast<uint8_t>((cur & 0x0F) | ah));
                    break;
                }
            }
            enterMid = false;
            const uint8_t cur = readByte(di);  // 1000:1859: pixels 2 and 3
            dl = ror(dl, 2);
            dh = ror(dh, 2);
            al = cur & 0x0C;
            if (al == dh) al = dl;
            ah |= al;
            dl = ror(dl, 2);
            dh = ror(dh, 2);
            al = cur & 0x03;
            if (al == dh) al = dl;
            al |= ah;
            dl = ror(dl, 2);
            dh = ror(dh, 2);
            writeByte(di++, al);
            if (--cx == 0) break;  // LOOP
        }
    }
}

void BackBuffer::scrollPlayfieldLeft(int top, int bottom, bool slowScroll) {
    // 1000:1934 Scroll_Setup records x 2, width 0x9C, rows top..bottom for the tail.
    recordDirty(2, top, 0x9C, bottom - top + 1);
    const int src = slowScroll ? 0x0A : 6, count = slowScroll ? 0x94 : 0x98;
    for (int y = std::max(top, 0); y <= std::min(bottom, kHeight - 1); ++y) {
        std::memmove(row(y) + 2, row(y) + src, static_cast<size_t>(count));
        cgaMove(static_cast<ptrdiff_t>(y) * kWidth + 4, static_cast<ptrdiff_t>(y) * kWidth + 2 * src, static_cast<size_t>(2 * count));
    }
}

void BackBuffer::scrollPlayfieldRight(int top, int bottom, bool slowScroll) {
    recordDirty(2, top, 0x9C, bottom - top + 1);
    // Copied backwards from the end of each row (STD): 0x4C words from byte 0x98 (slow: 0x94) to
    // byte 0x9C. With slow scroll the last word read is bytes -2..-1, the end of the row above.
    const int srcOff = slowScroll ? -2 : 2;
    for (int y = std::max(top, 0); y <= std::min(bottom, kHeight - 1); ++y) {
        const ptrdiff_t s = static_cast<ptrdiff_t>(y) * kRowBytes + srcOff;
        uint8_t* d = row(y) + 6;
        if (s >= 0) {
            std::memmove(d, bytes_.data() + s, 0x98);
        } else {  // row 0 with slow scroll: nothing above the buffer, keep the two bytes
            std::memmove(d + 2, bytes_.data(), 0x96);
        }
        // CGA (1000:1982): the same pixels (slow: the first CGA byte from the row before, which the
        // second new column overwrites).
        cgaMove(static_cast<ptrdiff_t>(y) * kWidth + 12, 2 * s, 2 * 0x98);
    }
}

void BackBuffer::scrollPlayfieldUp(int top, int bottom, bool slowScroll) {
    recordDirty(2, top, 0x9C, bottom - top + 1);
    const int shift = slowScroll ? 16 : 8;
    const int count = bottom - top + 1 - shift;  // the original loops forever for count <= 0; never called so
    for (int i = 0; i < count; ++i) {
        const int dy = top + i, sy = dy + shift;
        if (dy < 0 || sy >= kHeight) continue;
        std::memmove(row(dy) + 2, row(sy) + 2, 0x9C);
        cgaMove(static_cast<ptrdiff_t>(dy) * kWidth + 4, static_cast<ptrdiff_t>(sy) * kWidth + 4, 2 * 0x9C);
    }
}

void BackBuffer::scrollPlayfieldDown(int top, int bottom, bool slowScroll) {
    recordDirty(0, top, 0xA0, bottom - top + 1);  // this one records x 0, width 0xA0 (1000:1af6)
    const int shift = slowScroll ? 16 : 8;
    const int count = bottom - top + 1 - shift;
    for (int i = 0; i < count; ++i) {
        const int dy = bottom - i, sy = dy - shift;
        if (sy < 0 || dy >= kHeight) continue;
        std::memmove(row(dy) + 2, row(sy) + 2, 0x9C);
        cgaMove(static_cast<ptrdiff_t>(dy) * kWidth + 4, static_cast<ptrdiff_t>(sy) * kWidth + 4, 2 * 0x9C);
    }
}

void BackBuffer::drawTileColumn(int x, int top, int bottom, const PanFile& pan, size_t mapWordIndex) {
    const int rows = bottom - top + 1;
    recordDirty(x, top, 4, rows);
    int y = top, left = rows;
    while (left > 0) {
        const auto tile = pan.tile(pan.mapWord(mapWordIndex++));
        for (int ty = 0; ty < 8 && left > 0; ++ty, ++y, --left) {
            if (y < 0 || y >= kHeight) continue;
            for (int k = 0; k < 4; ++k)
                if (x + k >= 0 && x + k < kRowBytes) row(y)[x + k] = tile[static_cast<size_t>(ty * 4 + k)];
            // 1000:1a33: 7E00/7F00 for the column's first row, then XOR BH,0FEh per row (the game
            // draws columns at even x only).
            if (cgaEnabled())
                for (int px = 2 * x; px < 2 * x + 8; ++px) cgaSet(px, y, cgaPattern(pixel(px, y), (px & 3) ^ ((y - top) & 1)));
        }
    }
}

// ----------------------------------------------------------------------------------- present

void presentDirty(BackBuffer& back, BackBuffer& screen) {
    const bool cga = back.cgaEnabled() && screen.cgaEnabled();
    for (int p = 0; p < DirtyRegions::kRowPairs; ++p)
        for (const auto& [first, end] : back.dirty().spans(p))
            for (int y = 2 * p; y <= 2 * p + 1; ++y) {
                std::memcpy(screen.row(y) + first, back.row(y) + first, static_cast<size_t>(end - first));
                if (cga)
                    for (int x = 2 * first; x < 2 * end; ++x) screen.setCgaEntry(x, y, back.cgaEntry(x, y));
            }
    back.dirty().clear();
}

void presentFull(const BackBuffer& back, BackBuffer& screen) {
    std::copy(back.bytes().begin(), back.bytes().end(), screen.bytes().begin());
    if (back.cgaEnabled() && screen.cgaEnabled())
        for (int y = 0; y < BackBuffer::kHeight; ++y)
            for (int x = 0; x < BackBuffer::kWidth; ++x) screen.setCgaEntry(x, y, back.cgaEntry(x, y));
}

}  // namespace tmport::dos
