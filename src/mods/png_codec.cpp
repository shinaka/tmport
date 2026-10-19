#include "mods/png_codec.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>

#include "util/error.h"
#include "util/inflate.h"
#include "util/png_writer.h"

namespace tmport::mods {

// ===================================================================================== inflate

namespace {

// Length and distance codes of RFC 1951 (the deflate encoder below uses them).
constexpr std::array<uint16_t, 29> kLenBase = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<uint8_t, 29> kLenExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<uint16_t, 30> kDistBase = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<uint8_t, 30> kDistExtra = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

}  // namespace

// The engine-wide inflate (util/inflate.h); kept here for the mod layer's callers.
std::vector<uint8_t> inflateZlib(std::span<const uint8_t> data, size_t maxOutput) {
    return tmport::inflateZlib(data, maxOutput);
}

// ===================================================================================== deflate

namespace {

class BitWriter {
public:
    void put(uint32_t v, int n) {
        buf_ |= static_cast<uint64_t>(v) << cnt_;
        cnt_ += n;
        while (cnt_ >= 8) {
            out.push_back(static_cast<uint8_t>(buf_));
            buf_ >>= 8;
            cnt_ -= 8;
        }
    }
    // Huffman codes are sent most significant bit first.
    void putCode(uint32_t code, int len) {
        uint32_t r = 0;
        for (int b = 0; b < len; ++b) r |= ((code >> b) & 1u) << (len - 1 - b);
        put(r, len);
    }
    void flush() {
        if (cnt_ > 0) out.push_back(static_cast<uint8_t>(buf_));
        buf_ = 0;
        cnt_ = 0;
    }
    std::vector<uint8_t> out;

private:
    uint64_t buf_ = 0;
    int cnt_ = 0;
};

void putLiteral(BitWriter& w, int sym) {
    if (sym < 144)
        w.putCode(static_cast<uint32_t>(0x30 + sym), 8);
    else if (sym < 256)
        w.putCode(static_cast<uint32_t>(0x190 + sym - 144), 9);
    else if (sym < 280)
        w.putCode(static_cast<uint32_t>(sym - 256), 7);
    else
        w.putCode(static_cast<uint32_t>(0xC0 + sym - 280), 8);
}

void putMatch(BitWriter& w, int len, int dist) {
    int li = 28;
    while (kLenBase[static_cast<size_t>(li)] > len) --li;
    putLiteral(w, 257 + li);
    w.put(static_cast<uint32_t>(len - kLenBase[static_cast<size_t>(li)]), kLenExtra[static_cast<size_t>(li)]);
    int di = 29;
    while (kDistBase[static_cast<size_t>(di)] > dist) --di;
    w.putCode(static_cast<uint32_t>(di), 5);
    w.put(static_cast<uint32_t>(dist - kDistBase[static_cast<size_t>(di)]), kDistExtra[static_cast<size_t>(di)]);
}

}  // namespace

std::vector<uint8_t> deflateZlib(std::span<const uint8_t> data) {
    BitWriter w;
    w.out = {0x78, 0x01};
    w.put(1, 1);  // BFINAL
    w.put(1, 2);  // fixed Huffman
    constexpr int kHashBits = 15, kWindow = 32768, kMaxChain = 48;
    std::vector<int> head(1u << kHashBits, -1), prev(data.size(), -1);
    auto hash = [&](size_t i) {
        return ((static_cast<uint32_t>(data[i]) << 10) ^ (static_cast<uint32_t>(data[i + 1]) << 5) ^ data[i + 2]) &
               ((1u << kHashBits) - 1);
    };
    auto insert = [&](size_t i) {
        if (i + 2 >= data.size()) return;
        const uint32_t h = hash(i);
        prev[i] = head[h];
        head[h] = static_cast<int>(i);
    };
    size_t i = 0;
    const size_t n = data.size();
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + 2 < n) {
            int cand = head[hash(i)];
            int chain = kMaxChain;
            const size_t maxLen = std::min<size_t>(258, n - i);
            while (cand >= 0 && chain-- > 0 && static_cast<int>(i) - cand <= kWindow) {
                const uint8_t* a = data.data() + i;
                const uint8_t* b = data.data() + cand;
                if (b[bestLen] == a[bestLen]) {
                    size_t l = 0;
                    while (l < maxLen && a[l] == b[l]) ++l;
                    if (static_cast<int>(l) > bestLen) {
                        bestLen = static_cast<int>(l);
                        bestDist = static_cast<int>(i) - cand;
                        if (l == maxLen) break;
                    }
                }
                cand = prev[static_cast<size_t>(cand)];
            }
        }
        if (bestLen >= 3) {
            putMatch(w, bestLen, bestDist);
            for (int k = 0; k < bestLen; ++k) insert(i + static_cast<size_t>(k));
            i += static_cast<size_t>(bestLen);
        } else {
            putLiteral(w, data[i]);
            insert(i);
            ++i;
        }
    }
    putLiteral(w, 256);
    w.flush();
    const uint32_t a = png::adler32(data);
    w.out.push_back(static_cast<uint8_t>(a >> 24));
    w.out.push_back(static_cast<uint8_t>(a >> 16));
    w.out.push_back(static_cast<uint8_t>(a >> 8));
    w.out.push_back(static_cast<uint8_t>(a));
    return std::move(w.out);
}

// ========================================================================================= PNG

namespace {

uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

void putBe32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

void putChunk(std::vector<uint8_t>& out, const char* type, std::span<const uint8_t> data) {
    putBe32(out, static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    putBe32(out, png::crc32(std::span<const uint8_t>(out).subspan(start)));
}

constexpr uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

// Reverses the filters of one (sub-)image in place; returns the offset after it.
size_t unfilter(std::vector<uint8_t>& raw, size_t off, size_t rowBytes, int rows, int bpp) {
    std::vector<uint8_t> prior(rowBytes, 0);
    for (int y = 0; y < rows; ++y) {
        if (off + 1 + rowBytes > raw.size()) throw FormatError("PNG: image data too short");
        const uint8_t f = raw[off];
        uint8_t* row = raw.data() + off + 1;
        for (size_t x = 0; x < rowBytes; ++x) {
            const int a = x >= static_cast<size_t>(bpp) ? row[x - static_cast<size_t>(bpp)] : 0;
            const int b = prior[x];
            const int c = x >= static_cast<size_t>(bpp) ? prior[x - static_cast<size_t>(bpp)] : 0;
            int v = row[x];
            switch (f) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += paeth(a, b, c); break;
                default: throw FormatError(std::format("PNG: unknown filter type {}", f));
            }
            row[x] = static_cast<uint8_t>(v);
        }
        std::memcpy(prior.data(), row, rowBytes);
        off += 1 + rowBytes;
    }
    return off;
}

}  // namespace

DecodedPng decodePng(std::span<const uint8_t> file, size_t maxPixels) {
    if (file.size() < 8 || std::memcmp(file.data(), kSig, 8) != 0) throw FormatError("not a PNG file (bad signature)");
    DecodedPng img;
    std::vector<uint8_t> idat;
    std::vector<uint8_t> trns;
    bool haveHeader = false, haveEnd = false;
    size_t p = 8;
    while (p + 12 <= file.size() && !haveEnd) {
        const uint32_t len = be32(file.data() + p);
        if (len > file.size() - p - 12) throw FormatError("PNG: chunk runs past the end of the file");
        const uint8_t* type = file.data() + p + 4;
        const std::span<const uint8_t> data = file.subspan(p + 8, len);
        const uint32_t crc = be32(file.data() + p + 8 + len);
        if (png::crc32(file.subspan(p + 4, len + 4)) != crc)
            throw FormatError(std::format("PNG: CRC error in chunk {}", std::string(reinterpret_cast<const char*>(type), 4)));
        const std::string t(reinterpret_cast<const char*>(type), 4);
        if (t == "IHDR") {
            if (len != 13) throw FormatError("PNG: bad IHDR");
            img.width = static_cast<int>(be32(data.data()));
            img.height = static_cast<int>(be32(data.data() + 4));
            img.bitDepth = data[8];
            img.colorType = data[9];
            if (data[10] != 0 || data[11] != 0) throw FormatError("PNG: unknown compression or filter method");
            if (data[12] > 1) throw FormatError("PNG: unknown interlace method");
            img.interlaced = data[12] == 1;
            haveHeader = true;
        } else if (t == "PLTE") {
            if (len % 3 != 0 || len / 3 > 256 || len == 0) throw FormatError("PNG: bad PLTE");
            img.palette.clear();
            for (size_t i = 0; i < len; i += 3) img.palette.push_back(Rgb8{data[i], data[i + 1], data[i + 2]});
        } else if (t == "tRNS") {
            trns.assign(data.begin(), data.end());
        } else if (t == "IDAT") {
            idat.insert(idat.end(), data.begin(), data.end());
        } else if (t == "IEND") {
            haveEnd = true;
        } else if (!(type[0] & 0x20)) {
            throw FormatError(std::format("PNG: unsupported critical chunk {}", t));
        }
        p += 12 + len;
    }
    if (!haveHeader) throw FormatError("PNG: no IHDR chunk");
    if (idat.empty()) throw FormatError("PNG: no image data");
    if (img.width <= 0 || img.height <= 0 || static_cast<size_t>(img.width) * static_cast<size_t>(img.height) > maxPixels)
        throw FormatError(std::format("PNG: unsupported image size {}x{}", img.width, img.height));

    int channels = 0;
    bool depthOk = false;
    const int d = img.bitDepth;
    switch (img.colorType) {
        case 0: channels = 1; depthOk = d == 1 || d == 2 || d == 4 || d == 8 || d == 16; break;
        case 2: channels = 3; depthOk = d == 8 || d == 16; break;
        case 3: channels = 1; depthOk = d == 1 || d == 2 || d == 4 || d == 8; break;
        case 4: channels = 2; depthOk = d == 8 || d == 16; break;
        case 6: channels = 4; depthOk = d == 8 || d == 16; break;
        default: throw FormatError(std::format("PNG: unknown colour type {}", img.colorType));
    }
    if (!depthOk) throw FormatError(std::format("PNG: bit depth {} is invalid for colour type {}", d, img.colorType));
    if (img.colorType == 3 && img.palette.empty()) throw FormatError("PNG: indexed image without a palette");

    const int bitsPerPixel = channels * d;
    const int bpp = std::max(1, bitsPerPixel / 8);
    auto rowBytesFor = [&](int w) { return (static_cast<size_t>(w) * static_cast<size_t>(bitsPerPixel) + 7) / 8; };

    struct Pass {
        int x0, y0, dx, dy;
    };
    static constexpr Pass kAdam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                       {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    static constexpr Pass kNoInterlace[1] = {{0, 0, 1, 1}};
    const std::span<const Pass> passes = img.interlaced ? std::span<const Pass>(kAdam7) : std::span<const Pass>(kNoInterlace);

    size_t expected = 0;
    for (const Pass& ps : passes) {
        const int pw = (img.width - ps.x0 + ps.dx - 1) / ps.dx, ph = (img.height - ps.y0 + ps.dy - 1) / ps.dy;
        if (pw > 0 && ph > 0) expected += static_cast<size_t>(ph) * (1 + rowBytesFor(pw));
    }
    std::vector<uint8_t> raw = inflateZlib(idat, expected);
    if (raw.size() < expected) throw FormatError("PNG: image data too short");

    const size_t npix = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    // Samples per pixel at full precision (16-bit kept for the tRNS key).
    std::vector<uint16_t> samples(npix * static_cast<size_t>(channels));
    size_t off = 0;
    for (const Pass& ps : passes) {
        const int pw = (img.width - ps.x0 + ps.dx - 1) / ps.dx, ph = (img.height - ps.y0 + ps.dy - 1) / ps.dy;
        if (pw <= 0 || ph <= 0) continue;
        const size_t rb = rowBytesFor(pw);
        const size_t start = off;
        off = unfilter(raw, off, rb, ph, bpp);
        for (int y = 0; y < ph; ++y) {
            const uint8_t* row = raw.data() + start + static_cast<size_t>(y) * (1 + rb) + 1;
            for (int x = 0; x < pw; ++x) {
                const size_t dst = (static_cast<size_t>(ps.y0 + y * ps.dy) * static_cast<size_t>(img.width) +
                                    static_cast<size_t>(ps.x0 + x * ps.dx)) *
                                   static_cast<size_t>(channels);
                for (int c = 0; c < channels; ++c) {
                    uint16_t v;
                    if (d == 16) {
                        const size_t o = (static_cast<size_t>(x) * static_cast<size_t>(channels) + static_cast<size_t>(c)) * 2;
                        v = static_cast<uint16_t>((row[o] << 8) | row[o + 1]);
                    } else if (d == 8) {
                        v = row[static_cast<size_t>(x) * static_cast<size_t>(channels) + static_cast<size_t>(c)];
                    } else {
                        const size_t bit = static_cast<size_t>(x) * static_cast<size_t>(d);
                        v = static_cast<uint16_t>((row[bit / 8] >> (8 - d - static_cast<int>(bit % 8))) & ((1 << d) - 1));
                    }
                    samples[dst + static_cast<size_t>(c)] = v;
                }
            }
        }
    }

    img.rgba.resize(npix * 4);
    auto to8 = [&](uint16_t v) -> uint8_t {
        if (d == 16) return static_cast<uint8_t>(v >> 8);
        if (d == 8) return static_cast<uint8_t>(v);
        return static_cast<uint8_t>(v * 255 / ((1 << d) - 1));
    };
    if (img.colorType == 3) {
        img.paletteAlpha.assign(img.palette.size(), 255);
        for (size_t i = 0; i < trns.size() && i < img.paletteAlpha.size(); ++i) img.paletteAlpha[i] = trns[i];
        img.indices.resize(npix);
        for (size_t i = 0; i < npix; ++i) {
            const uint16_t idx = samples[i];
            if (idx >= img.palette.size()) throw FormatError(std::format("PNG: pixel index {} outside the {}-colour palette", idx, img.palette.size()));
            img.indices[i] = static_cast<uint8_t>(idx);
            const Rgb8 c = img.palette[idx];
            img.rgba[i * 4] = c.r;
            img.rgba[i * 4 + 1] = c.g;
            img.rgba[i * 4 + 2] = c.b;
            img.rgba[i * 4 + 3] = img.paletteAlpha[idx];
        }
        return img;
    }
    const bool greyKey = img.colorType == 0 && trns.size() >= 2;
    const bool rgbKey = img.colorType == 2 && trns.size() >= 6;
    const uint16_t kg = greyKey ? static_cast<uint16_t>((trns[0] << 8) | trns[1]) : 0;
    const uint16_t kr = rgbKey ? static_cast<uint16_t>((trns[0] << 8) | trns[1]) : 0;
    const uint16_t kgr = rgbKey ? static_cast<uint16_t>((trns[2] << 8) | trns[3]) : 0;
    const uint16_t kb = rgbKey ? static_cast<uint16_t>((trns[4] << 8) | trns[5]) : 0;
    for (size_t i = 0; i < npix; ++i) {
        const uint16_t* s = samples.data() + i * static_cast<size_t>(channels);
        uint8_t* o = img.rgba.data() + i * 4;
        switch (img.colorType) {
            case 0:
                o[0] = o[1] = o[2] = to8(s[0]);
                o[3] = greyKey && s[0] == kg ? 0 : 255;
                break;
            case 2:
                o[0] = to8(s[0]);
                o[1] = to8(s[1]);
                o[2] = to8(s[2]);
                o[3] = rgbKey && s[0] == kr && s[1] == kgr && s[2] == kb ? 0 : 255;
                break;
            case 4:
                o[0] = o[1] = o[2] = to8(s[0]);
                o[3] = to8(s[1]);
                break;
            case 6:
                o[0] = to8(s[0]);
                o[1] = to8(s[1]);
                o[2] = to8(s[2]);
                o[3] = to8(s[3]);
                break;
            default: break;
        }
    }
    return img;
}

std::vector<uint8_t> encodePngIndexed(int width, int height, std::span<const uint8_t> indices, std::span<const Rgb8> palette,
                                      std::span<const uint8_t> alpha) {
    std::vector<uint8_t> out(kSig, kSig + 8);
    std::vector<uint8_t> ihdr;
    putBe32(ihdr, static_cast<uint32_t>(width));
    putBe32(ihdr, static_cast<uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 3, 0, 0, 0});
    putChunk(out, "IHDR", ihdr);
    std::vector<uint8_t> plte;
    for (const Rgb8& c : palette) plte.insert(plte.end(), {c.r, c.g, c.b});
    putChunk(out, "PLTE", plte);
    if (!alpha.empty()) {
        size_t n = std::min(alpha.size(), palette.size());
        while (n > 0 && alpha[n - 1] == 255) --n;
        if (n > 0) putChunk(out, "tRNS", alpha.subspan(0, n));
    }
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (static_cast<size_t>(width) + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const auto row = indices.subspan(static_cast<size_t>(y) * static_cast<size_t>(width), static_cast<size_t>(width));
        raw.insert(raw.end(), row.begin(), row.end());
    }
    putChunk(out, "IDAT", deflateZlib(raw));
    putChunk(out, "IEND", {});
    return out;
}

std::vector<uint8_t> encodePngRgba(int width, int height, std::span<const uint8_t> rgba) {
    std::vector<uint8_t> out(kSig, kSig + 8);
    std::vector<uint8_t> ihdr;
    putBe32(ihdr, static_cast<uint32_t>(width));
    putBe32(ihdr, static_cast<uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    putChunk(out, "IHDR", ihdr);
    std::vector<uint8_t> raw;
    const size_t stride = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const auto row = rgba.subspan(static_cast<size_t>(y) * stride, stride);
        raw.insert(raw.end(), row.begin(), row.end());
    }
    putChunk(out, "IDAT", deflateZlib(raw));
    putChunk(out, "IEND", {});
    return out;
}

}  // namespace tmport::mods
