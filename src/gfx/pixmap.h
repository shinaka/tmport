#pragma once
// PixMap (decoded PMAP) and the PMAP codecs (re/docs/fmt_pmap.md, code_1058-1090.md §4.5).
// In memory a PixMap is always one byte per pixel: a palette index (0..255 for TMT256 art,
// 0..15 for TMT16 art), rows top to bottom, no padding.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tmport::gfx {

enum class PixEncoding : uint8_t {
    Raw4,  // TMT16: packed 4 bpp, row stride (w+1)/2, high nibble = left pixel
    Raw8,  // TMT256 uncompressed: w*h bytes
    Rund,  // TMT256 "RUND" RLE (packets cross rows)
    Memory,  // created at run time (save-under, tests)
};

struct PixMap {
    int width = 0;
    int height = 0;
    uint8_t depth = 8;  // bits per pixel of the source art (4 or 8)
    PixEncoding encoding = PixEncoding::Memory;
    std::vector<uint8_t> pixels;  // width*height indices
    size_t consumed = 0;          // bytes of the resource used by header + data

    uint8_t at(int x, int y) const { return pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)]; }
    const uint8_t* row(int y) const { return pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width); }
    bool empty() const { return width <= 0 || height <= 0; }
};

inline constexpr size_t kPmapHeaderSize = 4;  // u16 width, u16 height

// True if the 4 bytes at +4 are "RUND" (1058:0566).
bool isRund(std::span<const uint8_t> data);

// Decodes a PMAP resource body. depth: 4 (TMT16.DLL) or 8 (TMT256.DLL). Throws FormatError.
PixMap decodePmap(std::span<const uint8_t> data, int depth);

// RUND decoder (1058:0400): decodes exactly w*h pixels starting at data[start]; *end receives
// the offset after the last packet. Throws FormatError on truncation or overshoot.
std::vector<uint8_t> rundDecompress(std::span<const uint8_t> data, int width, int height, size_t start = 8,
                                    size_t* end = nullptr);

// RUND encoder (1058:0000, exact, including its "no run in the last 5 pixels" quirk).
// Returns the full resource body: header + "RUND" + packets.
std::vector<uint8_t> rundCompress(std::span<const uint8_t> pixels, int width, int height);

}  // namespace tmport::gfx
