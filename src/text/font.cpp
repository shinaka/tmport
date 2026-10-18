#include "text/font.h"

#include <format>

#include "gfx/frame_buffer.h"
#include "res/resource_manager.h"
#include "util/error.h"

namespace tmport::text {

// 1068:0073 Font_Select caches exactly these table positions.
Font Font::parse(std::span<const uint8_t> data) {
    if (data.size() < 3) throw FormatError("FFNT: shorter than its 3-byte header");
    const size_t n = static_cast<size_t>(data[1]) + 1;
    const size_t base = 3 + 3 * n;
    if (data.size() < base) throw FormatError(std::format("FFNT: tables need {} bytes, have {}", base, data.size()));
    Font f;
    f.height_ = data[2];
    f.widths_.assign(data.begin() + 3, data.begin() + 3 + static_cast<std::ptrdiff_t>(n));
    f.offsets_.resize(n);
    for (size_t c = 0; c < n; ++c)
        f.offsets_[c] = static_cast<uint16_t>(data[3 + n + c] | (data[3 + 2 * n + c] << 8));
    f.bits_.assign(data.begin() + static_cast<std::ptrdiff_t>(base), data.end());
    for (size_t c = 0; c < n; ++c) {
        const size_t need = static_cast<size_t>(f.offsets_[c]) +
                            static_cast<size_t>((f.widths_[c] + 7) / 8) * static_cast<size_t>(f.height_);
        if (need > f.bits_.size()) throw FormatError(std::format("FFNT: glyph {:#04x} overruns the bitmap pool", c));
    }
    return f;
}

Font Font::load(const ResourceManager& res, int id) {
    auto body = res.loadRaw(ResType::FFNT, id);
    if (!body) throw MissingFileError("TMT.DLL", std::format("FFNT {} not found in TMT.DLL", id));
    return parse(*body);
}

bool Font::pixel(uint8_t c, int x, int y) const {
    if (c >= widths_.size() || x < 0 || y < 0 || x >= widths_[c] || y >= height_) return false;
    const size_t rowBytes = static_cast<size_t>((widths_[c] + 7) / 8);
    const uint8_t b = bits_[offsets_[c] + static_cast<size_t>(y) * rowBytes + static_cast<size_t>(x >> 3)];
    return ((b << (x & 7)) & 0x80) != 0;
}

bool Font::hasInk(uint8_t c) const {
    if (c >= widths_.size()) return false;
    for (int y = 0; y < height_; ++y)
        for (int x = 0; x < widths_[c]; ++x)
            if (pixel(c, x, y)) return true;
    return false;
}

// 1060:01ef Font_BlitGlyph8bpp: per row, one source byte per 8 pixels (MSB first); when the width
// runs out mid-byte the rest of the byte is ignored (row stride ceil(w/8)). Set bits store the
// pen pixel; clear bits leave the destination unchanged. The 4-bpp blitter (1060:0000) does the
// same on nibbles; the engine's buffer holds one index per pixel in both modes.
int Font::drawChar(gfx::FrameBuffer& fb, int x, int y, uint8_t c) const {
    const int w = charWidth(c);
    if (w <= 0 || height_ <= 0) return w;
    const uint8_t pen = fb.forePixel();
    const size_t rowBytes = static_cast<size_t>((w + 7) / 8);
    const uint8_t* src = bits_.data() + offsets_[c];
    for (int r = 0; r < height_; ++r, src += rowBytes) {
        const int Y = y + r;
        if (Y < 0 || Y >= gfx::FrameBuffer::kHeight) continue;
        uint8_t* dst = fb.row(Y);
        for (int i = 0; i < w; ++i) {
            if (!((src[i >> 3] << (i & 7)) & 0x80)) continue;
            const int X = x + i;
            if (X >= 0 && X < gfx::FrameBuffer::kWidth) dst[X] = pen;
        }
    }
    return w;
}

}  // namespace tmport::text
