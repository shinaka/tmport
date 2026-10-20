#include "gg/gfx/lpx.h"

#include <algorithm>
#include <format>

#include "util/error.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr size_t kHeaderSize = 14;
constexpr uint8_t kMagic[4] = {'L', 'P', 'X', 0x1A};

uint16_t u16At(std::span<const uint8_t> p, size_t off) { return static_cast<uint16_t>(p[off] | (p[off + 1] << 8)); }

}  // namespace

DecodedLpx decodeLpx(std::span<const uint8_t> p) {
    if (p.size() < kHeaderSize || !std::equal(std::begin(kMagic), std::end(kMagic), p.begin()))
        throw FormatError("not an LPX picture");
    DecodedLpx pic;
    pic.format = u16At(p, 4);
    pic.width = u16At(p, 6);
    pic.height = u16At(p, 8);
    pic.key = u16At(p, 10);
    pic.dataOffset = u16At(p, 12);
    const size_t start = 12 + size_t{pic.dataOffset};
    const size_t n = static_cast<size_t>(pic.width) * static_cast<size_t>(pic.height);
    if (pic.format == 0) {
        if (start + n > p.size())
            throw FormatError(std::format("raw picture {}x{} needs {} bytes, has {}", pic.width, pic.height, n,
                                          p.size() > start ? p.size() - start : 0));
        pic.pixels.assign(p.begin() + static_cast<ptrdiff_t>(start), p.begin() + static_cast<ptrdiff_t>(start + n));
        pic.opaque.assign(n, 1);
        pic.used = start + n;
        return pic;
    }
    if (pic.format != 1) throw FormatError(std::format("unknown LPX format {}", pic.format));
    pic.pixels.assign(n, 0);
    pic.opaque.assign(n, 0);
    size_t i = start;
    const int w = pic.width;
    for (int y = 0; y < pic.height; ++y) {
        int x = 0;
        uint8_t count = static_cast<uint8_t>(w & 0xFF);  // 1399:0465 MOV BH,BL: an 8-bit width counter
        const size_t row = static_cast<size_t>(y) * static_cast<size_t>(w);
        for (;;) {
            if (i >= p.size()) throw FormatError(std::format("RLE data ends in row {}", y));
            const uint8_t c = p[i++];
            int k;
            if (c == 0) {
                if (i >= p.size()) throw FormatError(std::format("RLE data ends in a skip in row {}", y));
                k = p[i++];
                x += k;
            } else {
                k = c;
                if (x + k > w || i + static_cast<size_t>(k) > p.size())
                    throw FormatError(std::format("RLE literal run overflows row {}", y));
                std::copy_n(p.begin() + static_cast<ptrdiff_t>(i), k, pic.pixels.begin() + static_cast<ptrdiff_t>(row + static_cast<size_t>(x)));
                std::fill_n(pic.opaque.begin() + static_cast<ptrdiff_t>(row + static_cast<size_t>(x)), k, uint8_t{1});
                i += static_cast<size_t>(k);
                x += k;
            }
            count = static_cast<uint8_t>(count - k);
            if (count == 0) break;
            if (x > w + 255) throw FormatError(std::format("RLE row {} never ends", y));
        }
        if (x != w) {
            ++pic.counterMismatches;
            if (x > w) throw FormatError(std::format("RLE row {} is {} pixels wide, picture {}", y, x, w));
        }
    }
    pic.used = i;
    return pic;
}

std::vector<uint8_t> keyedMask(const DecodedLpx& pic) {
    if (pic.format != 0) return pic.opaque;
    std::vector<uint8_t> mask(pic.pixels.size());
    const uint8_t key = static_cast<uint8_t>(pic.key & 0xFF);
    std::transform(pic.pixels.begin(), pic.pixels.end(), mask.begin(), [key](uint8_t c) { return uint8_t(c != key); });
    return mask;
}

// 1399:0b71 Gfx_RemapPicture
void remapPicture(std::span<uint8_t> p, const std::array<uint8_t, 256>& lut) {
    if (p.size() < kHeaderSize) return;
    if (u16At(p, 4) & 1) return;  // RLE: untouched
    const size_t w = u16At(p, 6), h = u16At(p, 8);
    const size_t start = 12 + size_t{u16At(p, 12)};
    // The original loops CX = width bytes per row, DX = height rows (a zero count would run 65536
    // times); the engine stops at the end of the payload.
    const size_t end = std::min(p.size(), start + w * h);
    if (start + w * h > p.size()) log::debug("Gfx_RemapPicture: picture {}x{} runs past its payload", w, h);
    for (size_t i = start; i < end; ++i) p[i] = lut[p[i]];
}

// 1473:077e Gfx_RecolorPicture
void recolorPicture(std::span<uint8_t> payload, std::span<const uint8_t> from, std::span<const uint8_t> to) {
    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) lut[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    for (size_t i = 0; i < to.size() && i < from.size() && to[i] != 0xFF; ++i) lut[from[i]] = to[i];
    remapPicture(payload, lut);
}

}  // namespace tmport::gg
