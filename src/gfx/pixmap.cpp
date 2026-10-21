#include "gfx/pixmap.h"

#include <cstring>
#include <format>

#include "mischief/util/byte_reader.h"
#include "mischief/util/error.h"

namespace tmport::gfx {

bool isRund(std::span<const uint8_t> data) { return data.size() >= 8 && std::memcmp(data.data() + 4, "RUND", 4) == 0; }

std::vector<uint8_t> rundDecompress(std::span<const uint8_t> data, int width, int height, size_t start, size_t* end) {
    const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> out;
    out.reserve(total);
    size_t p = start;
    while (out.size() < total) {
        if (p >= data.size()) throw mischief::FormatError(std::format("RUND stream truncated at {} ({}/{} pixels)", p, out.size(), total));
        const uint8_t c = data[p];
        const size_t n = c & 0x7F;
        if (c & 0x80) {  // run: one byte repeated n times
            if (p + 1 >= data.size()) throw mischief::FormatError("RUND run truncated");
            out.insert(out.end(), n, data[p + 1]);
            p += 2;
        } else {  // literal: n bytes follow (n = 0 is a no-op packet)
            if (p + 1 + n > data.size()) throw mischief::FormatError("RUND literal truncated");
            out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(p + 1),
                       data.begin() + static_cast<std::ptrdiff_t>(p + 1 + n));
            p += 1 + n;
        }
    }
    if (out.size() != total) throw mischief::FormatError(std::format("RUND stream overshoots: {} > {} pixels", out.size(), total));
    if (end) *end = p;
    return out;
}

std::vector<uint8_t> rundCompress(std::span<const uint8_t> pixels, int width, int height) {
    // Header-inclusive coordinates as in 1058:0000: src = header + pixels, N2 = w*h, N3 = N2 + 4.
    const size_t n2 = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t n3 = n2 + 4;
    std::vector<uint8_t> src(n3);
    src[0] = static_cast<uint8_t>(width & 0xFF);
    src[1] = static_cast<uint8_t>((width >> 8) & 0xFF);
    src[2] = static_cast<uint8_t>(height & 0xFF);
    src[3] = static_cast<uint8_t>((height >> 8) & 0xFF);
    std::memcpy(src.data() + 4, pixels.data(), n2);

    // A run may start only at i < N2 - 1 (not N3 - 1): the encoder quirk.
    auto run4 = [&](size_t j) {
        return n2 >= 1 && j < n2 - 1 && src[j] == src[j + 1] && src[j] == src[j + 2] && src[j] == src[j + 3];
    };

    std::vector<uint8_t> out(src.begin(), src.begin() + 4);
    out.insert(out.end(), {'R', 'U', 'N', 'D'});
    size_t i = 4;
    while (i < n3) {
        const size_t ctl = out.size();
        out.push_back(0);
        if (run4(i)) {
            const uint8_t v = src[i];
            out.push_back(v);
            ++i;
            unsigned n = 1;
            while (n < 127 && i < n3 && src[i] == v) {
                ++n;
                ++i;
            }
            out[ctl] = static_cast<uint8_t>(0x80 | n);
        } else {
            unsigned n = 0;
            while (n < 127 && i < n3 && !run4(i)) {
                out.push_back(src[i++]);
                ++n;
            }
            out[ctl] = static_cast<uint8_t>(n);
        }
    }
    return out;
}

PixMap decodePmap(std::span<const uint8_t> data, int depth) {
    mischief::ByteReader r(data, "PMAP");
    PixMap pm;
    // The EXE reads the header words as signed 16-bit (1058:0000); the largest value is 512.
    pm.width = r.i16(0);
    pm.height = r.i16(2);
    if (pm.width <= 0 || pm.height <= 0) throw mischief::FormatError(std::format("PMAP has an empty size {}x{}", pm.width, pm.height));
    const size_t w = static_cast<size_t>(pm.width), h = static_cast<size_t>(pm.height);

    if (depth == 4) {
        pm.depth = 4;
        pm.encoding = PixEncoding::Raw4;
        const size_t stride = (w + 1) / 2;
        auto src = r.bytes(kPmapHeaderSize, stride * h);
        pm.pixels.resize(w * h);
        for (size_t y = 0; y < h; ++y) {
            const uint8_t* row = src.data() + y * stride;
            uint8_t* dst = pm.pixels.data() + y * w;
            for (size_t x = 0; x < w; ++x) dst[x] = (x & 1) ? (row[x >> 1] & 0x0F) : (row[x >> 1] >> 4);
        }
        pm.consumed = kPmapHeaderSize + stride * h;
        return pm;
    }
    if (depth != 8) throw mischief::FormatError(std::format("unsupported PMAP depth {}", depth));
    pm.depth = 8;
    if (isRund(data)) {
        pm.encoding = PixEncoding::Rund;
        size_t end = 0;
        pm.pixels = rundDecompress(data, pm.width, pm.height, 8, &end);
        pm.consumed = end;
        return pm;
    }
    pm.encoding = PixEncoding::Raw8;
    auto src = r.bytes(kPmapHeaderSize, w * h);
    pm.pixels.assign(src.begin(), src.end());
    pm.consumed = kPmapHeaderSize + w * h;
    return pm;
}

}  // namespace tmport::gfx
