#include "dos/text/dos_font.h"

#include <algorithm>
#include <array>
#include <format>

#include "dos/data/dos_exe.h"
#include "dos/gfx/back_buffer.h"
#include "util/error.h"

namespace tmport::dos {

namespace {

// 1000:219a  BE 47 24  MOV SI,2447h / 33 C0  XOR AX,AX / 26 8A 44 02  MOV AL,ES:[SI+2]
constexpr std::array<uint8_t, 9> kBlitterSignature = {0xBE, 0x47, 0x24, 0x33, 0xC0, 0x26, 0x8A, 0x44, 0x02};

}  // namespace

DosFont DosFont::fromExe(const DosExe& exe) {
    const auto sig = exe.at(kCodeSegment, 0x219A);
    if (sig.size() < kBlitterSignature.size() || !std::equal(kBlitterSignature.begin(), kBlitterSignature.end(), sig.begin()))
        throw FormatError("SST.EXE: the glyph blitter 1000:2180 does not load the font from 1000:2447");
    std::vector<uint16_t> bpr(256);
    for (size_t w = 0; w < bpr.size(); ++w) bpr[w] = exe.ds16(static_cast<uint16_t>(kDsBytesPerRow + 2 * w));
    return fromBlock(exe.at(kCodeSegment, kFontOffset), bpr);
}

DosFont DosFont::fromBlock(std::span<const uint8_t> block, std::span<const uint16_t> bytesPerRowByWidth) {
    if (block.size() < 3) throw FormatError("DOS font: block too short");
    DosFont f;
    f.unknown0_ = block[0];
    const size_t n = static_cast<size_t>(block[1]) + 1;
    f.height_ = block[2];
    const size_t dataStart = 3 + 3 * n;
    if (block.size() < dataStart) throw FormatError("DOS font: tables run past the block");
    f.widths_.assign(block.begin() + 3, block.begin() + 3 + static_cast<std::ptrdiff_t>(n));
    size_t end = 0;
    for (size_t c = 0; c < n; ++c) {
        const size_t off = block[3 + n + c] | (static_cast<size_t>(block[3 + 2 * n + c]) << 8);
        const int w = f.widths_[c];
        const int bpr = static_cast<size_t>(w) < bytesPerRowByWidth.size() ? bytesPerRowByWidth[static_cast<size_t>(w)] : (w + 7) / 8;
        f.offsets_.push_back(off);
        f.bpr_.push_back(bpr);
        end = std::max(end, off + static_cast<size_t>(bpr) * f.height_);
    }
    if (block.size() < dataStart + end) throw FormatError("DOS font: glyph data runs past the block");
    f.data_.assign(block.begin() + static_cast<std::ptrdiff_t>(dataStart), block.begin() + static_cast<std::ptrdiff_t>(dataStart + end));
    return f;
}

std::span<const uint8_t> DosFont::glyph(int code) const {
    if (code < 0 || code >= count()) return {};
    return std::span<const uint8_t>(data_).subspan(offsets_[static_cast<size_t>(code)],
                                                   static_cast<size_t>(bpr_[static_cast<size_t>(code)]) * height_);
}

uint16_t DosFont::expandNibble(int bits) {
    // Pixel order of a little-endian word on screen: low byte high nibble, low byte low nibble, high
    // byte high nibble, high byte low nibble; font bit 3 (MSB) is the leftmost pixel.
    uint16_t m = 0;
    if (bits & 8) m |= 0x00F0;
    if (bits & 4) m |= 0x000F;
    if (bits & 2) m |= 0xF000;
    if (bits & 1) m |= 0x0F00;
    return m;
}

int DosFont::drawChar(BackBuffer& buf, uint16_t colour, int x, int y, int code) const {
    if (code < 0 || code >= count()) return 0;
    const int adv = advance(code);
    if (colour == kMeasureOnly) return adv;
    const auto g = glyph(code);
    const int bpr = bytesPerRow(code);
    auto bytes = buf.bytes();
    for (int row = 0; row < height_; ++row) {
        ptrdiff_t at = static_cast<ptrdiff_t>(y + row) * BackBuffer::kRowBytes + x;
        for (int b = 0; b < bpr; ++b) {
            const uint8_t bits = g[static_cast<size_t>(row * bpr + b)];
            for (int half = 0; half < 2; ++half, at += 2) {
                const uint16_t m = expandNibble(half == 0 ? bits >> 4 : bits & 15);
                const uint16_t ink = colour & m;
                for (int k = 0; k < 2; ++k) {
                    const ptrdiff_t p = at + k;
                    if (p < 0 || p >= static_cast<ptrdiff_t>(bytes.size())) continue;
                    const uint8_t mb = static_cast<uint8_t>(m >> (8 * k)), ib = static_cast<uint8_t>(ink >> (8 * k));
                    bytes[static_cast<size_t>(p)] = static_cast<uint8_t>((bytes[static_cast<size_t>(p)] & ~mb) | ib);
                }
            }
        }
    }
    buf.noteDrawn(x, y, width(code) >> 1, height_);
    return adv;
}

}  // namespace tmport::dos
