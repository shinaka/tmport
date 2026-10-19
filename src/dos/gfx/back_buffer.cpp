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
    noteDrawn(x1, y1, x2 - x1 + 1, y2 - y1 + 1);  // the tail at 1000:16e4 uses the unclipped values
    const int xa = std::max(x1, 0), xb = std::min(x2, kRowBytes - 1);
    for (int y = std::max(y1, 0); y <= std::min(y2, kHeight - 1); ++y)
        if (xa <= xb) std::memset(row(y) + xa, fill, static_cast<size_t>(xb - xa + 1));
}

void BackBuffer::replaceColor(int x1, int y1, int x2, int y2, uint16_t from, uint16_t to) {
    noteDrawn(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
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
}

void BackBuffer::scrollPlayfieldLeft(int top, int bottom, bool slowScroll) {
    // 1000:1934 Scroll_Setup records x 2, width 0x9C, rows top..bottom for the tail.
    noteDrawn(2, top, 0x9C, bottom - top + 1);
    const int src = slowScroll ? 0x0A : 6, count = slowScroll ? 0x94 : 0x98;
    for (int y = std::max(top, 0); y <= std::min(bottom, kHeight - 1); ++y) std::memmove(row(y) + 2, row(y) + src, static_cast<size_t>(count));
}

void BackBuffer::scrollPlayfieldRight(int top, int bottom, bool slowScroll) {
    noteDrawn(2, top, 0x9C, bottom - top + 1);
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
    }
}

void BackBuffer::scrollPlayfieldUp(int top, int bottom, bool slowScroll) {
    noteDrawn(2, top, 0x9C, bottom - top + 1);
    const int shift = slowScroll ? 16 : 8;
    const int count = bottom - top + 1 - shift;  // the original loops forever for count <= 0; never called so
    for (int i = 0; i < count; ++i) {
        const int dy = top + i, sy = dy + shift;
        if (dy < 0 || sy >= kHeight) continue;
        std::memmove(row(dy) + 2, row(sy) + 2, 0x9C);
    }
}

void BackBuffer::scrollPlayfieldDown(int top, int bottom, bool slowScroll) {
    noteDrawn(0, top, 0xA0, bottom - top + 1);  // this one records x 0, width 0xA0 (1000:1af6)
    const int shift = slowScroll ? 16 : 8;
    const int count = bottom - top + 1 - shift;
    for (int i = 0; i < count; ++i) {
        const int dy = bottom - i, sy = dy - shift;
        if (sy < 0 || dy >= kHeight) continue;
        std::memmove(row(dy) + 2, row(sy) + 2, 0x9C);
    }
}

void BackBuffer::drawTileColumn(int x, int top, int bottom, const PanFile& pan, size_t mapWordIndex) {
    const int rows = bottom - top + 1;
    noteDrawn(x, top, 4, rows);
    int y = top, left = rows;
    while (left > 0) {
        const auto tile = pan.tile(pan.mapWord(mapWordIndex++));
        for (int ty = 0; ty < 8 && left > 0; ++ty, ++y, --left) {
            if (y < 0 || y >= kHeight) continue;
            for (int k = 0; k < 4; ++k)
                if (x + k >= 0 && x + k < kRowBytes) row(y)[x + k] = tile[static_cast<size_t>(ty * 4 + k)];
        }
    }
}

// ----------------------------------------------------------------------------------- present

void presentDirty(BackBuffer& back, BackBuffer& screen) {
    for (int p = 0; p < DirtyRegions::kRowPairs; ++p)
        for (const auto& [first, end] : back.dirty().spans(p))
            for (int y = 2 * p; y <= 2 * p + 1; ++y)
                std::memcpy(screen.row(y) + first, back.row(y) + first, static_cast<size_t>(end - first));
    back.dirty().clear();
}

void presentFull(const BackBuffer& back, BackBuffer& screen) {
    std::copy(back.bytes().begin(), back.bytes().end(), screen.bytes().begin());
}

}  // namespace tmport::dos
