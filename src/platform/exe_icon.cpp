#include "platform/exe_icon.h"

#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <format>

#include "util/byte_reader.h"
#include "util/error.h"

namespace tmport {

namespace {

int rowStride(int width, int bpp) { return ((width * bpp + 31) / 32) * 4; }

bool bitAt(std::span<const uint8_t> rows, int stride, int row, int x) {
    return (rows[static_cast<size_t>(row) * stride + x / 8] >> (7 - (x & 7))) & 1;
}

}  // namespace

std::vector<IconDirEntry> parseIconDirectory(std::span<const uint8_t> data) {
    ByteReader r(data, "RT_GROUP_ICON");
    if (r.u16(0) != 0 || r.u16(2) != 1) throw FormatError("RT_GROUP_ICON: not an icon directory");
    const int count = r.u16(4);
    std::vector<IconDirEntry> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const size_t e = 6 + static_cast<size_t>(i) * 14;
        IconDirEntry d;
        d.width = r.u8(e) ? r.u8(e) : 256;
        d.height = r.u8(e + 1) ? r.u8(e + 1) : 256;
        d.colourCount = r.u8(e + 2) ? r.u8(e + 2) : 256;
        d.planes = r.u16(e + 4);
        d.bitCount = r.u16(e + 6);
        d.bytesInRes = r.u32(e + 8);
        d.iconId = r.u16(e + 12);
        out.push_back(d);
    }
    return out;
}

IconImage decodeIconResource(std::span<const uint8_t> data) {
    ByteReader r(data, "RT_ICON");
    const uint32_t hdrSize = r.u32(0);
    int width = 0, height2 = 0, bpp = 0, colours = 0, rgbSize = 0;
    if (hdrSize >= 40) {  // BITMAPINFOHEADER (or a later, longer header)
        width = r.i32(4);
        height2 = r.i32(8);
        if (r.u16(12) != 1) throw FormatError("RT_ICON: planes != 1");
        bpp = r.u16(14);
        if (r.u32(16) != 0) throw FormatError("RT_ICON: compressed bitmap");
        colours = static_cast<int>(r.u32(32));
        rgbSize = 4;
    } else if (hdrSize == 12) {  // BITMAPCOREHEADER
        width = r.u16(4);
        height2 = r.u16(6);
        if (r.u16(8) != 1) throw FormatError("RT_ICON: planes != 1");
        bpp = r.u16(10);
        rgbSize = 3;
    } else {
        throw FormatError(std::format("RT_ICON: unsupported header size {}", hdrSize));
    }
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)
        throw FormatError(std::format("RT_ICON: {} bpp is not supported", bpp));
    // biHeight counts the XOR and the AND bitmap; a negative (top-down) height is not valid here.
    if (width <= 0 || width > 256 || height2 <= 0 || height2 > 512 || (height2 & 1))
        throw FormatError(std::format("RT_ICON: bad size {}x{}", width, height2));
    const int height = height2 / 2;
    if (bpp <= 8) {
        if (colours == 0 || colours > (1 << bpp)) colours = 1 << bpp;
    } else {
        colours = 0;  // no colour table for direct colour (BI_RGB)
    }

    const size_t tableOff = hdrSize;
    std::vector<uint8_t> table(static_cast<size_t>(colours) * 3);  // R,G,B per entry
    for (int i = 0; i < colours; ++i) {
        const auto c = r.bytes(tableOff + static_cast<size_t>(i) * rgbSize, 3);  // B, G, R
        table[static_cast<size_t>(i) * 3 + 0] = c[2];
        table[static_cast<size_t>(i) * 3 + 1] = c[1];
        table[static_cast<size_t>(i) * 3 + 2] = c[0];
    }
    const size_t xorOff = tableOff + static_cast<size_t>(colours) * rgbSize;
    const int xorStride = rowStride(width, bpp);
    const int andStride = rowStride(width, 1);
    const auto xorBits = r.bytes(xorOff, static_cast<size_t>(xorStride) * height);
    const auto andBits = r.bytes(xorOff + static_cast<size_t>(xorStride) * height, static_cast<size_t>(andStride) * height);

    IconImage img;
    img.width = width;
    img.height = height;
    img.bitCount = bpp;
    img.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    // A 32-bpp icon may carry its own alpha channel; it then replaces the AND mask.
    bool ownAlpha = false;
    if (bpp == 32)
        for (int i = 0; i < width * height && !ownAlpha; ++i) ownAlpha = xorBits[static_cast<size_t>(i) * 4 + 3] != 0;

    for (int y = 0; y < height; ++y) {
        const int row = height - 1 - y;  // DIB rows are bottom-up
        const uint8_t* src = xorBits.data() + static_cast<size_t>(row) * xorStride;
        for (int x = 0; x < width; ++x) {
            uint8_t rgb[3] = {0, 0, 0};
            uint8_t alpha = 255;
            if (bpp <= 8) {
                int idx = 0;
                switch (bpp) {
                    case 1: idx = (src[x / 8] >> (7 - (x & 7))) & 1; break;
                    case 4: idx = (x & 1) ? (src[x / 2] & 0x0F) : (src[x / 2] >> 4); break;
                    default: idx = src[x]; break;
                }
                if (idx >= colours) throw FormatError(std::format("RT_ICON: colour index {} outside the table", idx));
                for (int k = 0; k < 3; ++k) rgb[k] = table[static_cast<size_t>(idx) * 3 + k];
            } else {
                const uint8_t* p = src + static_cast<size_t>(x) * (bpp / 8);  // B, G, R(, A)
                rgb[0] = p[2];
                rgb[1] = p[1];
                rgb[2] = p[0];
                if (ownAlpha) alpha = p[3];
            }
            if (!ownAlpha && bitAt(andBits, andStride, row, x)) {
                if (rgb[0] | rgb[1] | rgb[2]) ++img.invertedPixels;
                alpha = 0;
                rgb[0] = rgb[1] = rgb[2] = 0;
            }
            uint8_t* d = img.rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
            d[0] = rgb[0];
            d[1] = rgb[1];
            d[2] = rgb[2];
            d[3] = alpha;
        }
    }
    return img;
}

const IconDirEntry* bestIconEntry(std::span<const IconDirEntry> entries) {
    if (entries.empty()) return nullptr;
    // Best entry: the largest, then the deepest colour (bitCount may be 0 in Win16 directories:
    // fall back to the colour count).
    auto depth = [](const IconDirEntry& e) {
        if (e.bitCount) return e.bitCount * std::max(1, e.planes);
        return e.colourCount >= 256 ? 8 : e.colourCount > 2 ? 4 : 1;
    };
    const IconDirEntry* best = &entries[0];
    for (const IconDirEntry& e : entries) {
        const long a = static_cast<long>(e.width) * e.height, b = static_cast<long>(best->width) * best->height;
        if (a > b || (a == b && depth(e) > depth(*best))) best = &e;
    }
    return best;
}

bool setWindowIcon(SDL_Window* window, const IconImage& icon) {
    if (!window || icon.width <= 0 || icon.height <= 0) return false;
    SDL_Surface* s = SDL_CreateSurfaceFrom(icon.width, icon.height, SDL_PIXELFORMAT_RGBA32,
                                           const_cast<uint8_t*>(icon.rgba.data()), icon.width * 4);
    if (!s) return false;
    const bool ok = SDL_SetWindowIcon(window, s);  // SDL keeps its own converted copy
    SDL_DestroySurface(s);
    return ok;
}

}  // namespace tmport
