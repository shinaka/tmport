#include "util/png_writer.h"

#include <algorithm>
#include <array>
#include <stdexcept>

#include "util/file_util.h"

namespace tmport::png {

namespace {

const std::array<uint32_t, 256>& crcTable() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}

void putU32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void putChunk(std::vector<uint8_t>& out, const char type[4], std::span<const uint8_t> body) {
    putU32BE(out, static_cast<uint32_t>(body.size()));
    const size_t typePos = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    const uint32_t c = crc32(std::span<const uint8_t>(out.data() + typePos, 4 + body.size()));
    putU32BE(out, c);
}

// zlib stream (CMF/FLG, stored deflate blocks of at most 65535 bytes, Adler-32).
std::vector<uint8_t> zlibStored(std::span<const uint8_t> raw) {
    std::vector<uint8_t> z;
    z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    z.push_back(0x78);  // CM = 8 (deflate), CINFO = 7 (32 KB window)
    z.push_back(0x01);  // FCHECK so that (0x78 << 8 | FLG) % 31 == 0, FLEVEL 0
    size_t pos = 0;
    do {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);  // BFINAL, BTYPE = 00 (stored)
        z.push_back(static_cast<uint8_t>(n & 0xFF));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xFF));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (pos < raw.size());
    putU32BE(z, adler32(raw));
    return z;
}

std::vector<uint8_t> encode(int width, int height, uint8_t colorType, size_t bytesPerPixel,
                            std::span<const uint8_t> pixels, std::span<const Rgb8> palette,
                            std::span<const uint8_t> alpha) {
    if (width <= 0 || height <= 0) throw std::invalid_argument("png: empty image");
    const size_t rowBytes = static_cast<size_t>(width) * bytesPerPixel;
    if (pixels.size() < rowBytes * static_cast<size_t>(height)) throw std::invalid_argument("png: pixel buffer too small");

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    std::vector<uint8_t> ihdr;
    putU32BE(ihdr, static_cast<uint32_t>(width));
    putU32BE(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);          // bit depth
    ihdr.push_back(colorType);  // 6 = RGBA, 3 = indexed
    ihdr.push_back(0);          // compression
    ihdr.push_back(0);          // filter method
    ihdr.push_back(0);          // no interlace
    putChunk(out, "IHDR", ihdr);

    if (colorType == 3) {
        if (palette.empty() || palette.size() > 256) throw std::invalid_argument("png: bad palette size");
        std::vector<uint8_t> plte;
        for (const Rgb8& c : palette) {
            plte.push_back(c.r);
            plte.push_back(c.g);
            plte.push_back(c.b);
        }
        putChunk(out, "PLTE", plte);
        if (!alpha.empty()) {
            size_t n = std::min(alpha.size(), palette.size());
            while (n > 0 && alpha[n - 1] == 255) --n;
            if (n > 0) putChunk(out, "tRNS", alpha.first(n));
        }
    }

    std::vector<uint8_t> raw;
    raw.reserve((rowBytes + 1) * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);  // filter type None
        const auto row = pixels.subspan(static_cast<size_t>(y) * rowBytes, rowBytes);
        raw.insert(raw.end(), row.begin(), row.end());
    }
    putChunk(out, "IDAT", zlibStored(raw));
    putChunk(out, "IEND", {});
    return out;
}

}  // namespace

uint32_t crc32(std::span<const uint8_t> data, uint32_t crc) {
    const auto& t = crcTable();
    uint32_t c = crc ^ 0xFFFFFFFFu;
    for (uint8_t b : data) c = t[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t adler32(std::span<const uint8_t> data, uint32_t adler) {
    uint32_t a = adler & 0xFFFF, b = adler >> 16;
    size_t i = 0;
    while (i < data.size()) {
        const size_t n = std::min<size_t>(5552, data.size() - i);  // keeps b below 2^32
        for (size_t k = 0; k < n; ++k) {
            a += data[i + k];
            b += a;
        }
        a %= 65521;
        b %= 65521;
        i += n;
    }
    return (b << 16) | a;
}

std::vector<uint8_t> encodeRgba(int width, int height, std::span<const uint8_t> rgba) {
    return encode(width, height, 6, 4, rgba, {}, {});
}

std::vector<uint8_t> encodeIndexed(int width, int height, std::span<const uint8_t> indices,
                                   std::span<const Rgb8> palette, std::span<const uint8_t> alpha) {
    // Every index must refer to a palette entry.
    const size_t n = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < n && i < indices.size(); ++i)
        if (indices[i] >= palette.size()) throw std::invalid_argument("png: index outside the palette");
    return encode(width, height, 3, 1, indices, palette, alpha);
}

bool writeRgba(const std::filesystem::path& path, int width, int height, std::span<const uint8_t> rgba) {
    return writeWholeFile(path, encodeRgba(width, height, rgba));
}

bool writeIndexed(const std::filesystem::path& path, int width, int height, std::span<const uint8_t> indices,
                  std::span<const Rgb8> palette, std::span<const uint8_t> alpha) {
    return writeWholeFile(path, encodeIndexed(width, height, indices, palette, alpha));
}

}  // namespace tmport::png
