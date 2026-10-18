#pragma once
// PNG reading for mod overrides and compressed PNG writing for the mod-kit exporter.
//
// Our own small zlib inflate/deflate (RFC 1950/1951) and PNG decoder (RFC 2083): all colour
// types (grey, RGB, indexed, grey+alpha, RGBA), bit depths 1..16, Adam7 interlacing, tRNS.
// Indexed images keep their palette indices, which an RGB-expanding decoder would lose: the game
// palette has duplicate colours, and only the index says which entry a pixel uses.

#include <cstdint>
#include <span>
#include <vector>

#include "util/rgb.h"

namespace tmport::mods {

// zlib stream -> bytes. Throws FormatError on corrupt data or when the output would exceed
// maxOutput bytes.
std::vector<uint8_t> inflateZlib(std::span<const uint8_t> data, size_t maxOutput = 256u << 20);
// bytes -> zlib stream (LZ77 with hash chains + fixed Huffman codes; small and fast enough for
// the exporter).
std::vector<uint8_t> deflateZlib(std::span<const uint8_t> data);

struct DecodedPng {
    int width = 0;
    int height = 0;
    int colorType = 0;   // 0 grey, 2 RGB, 3 indexed, 4 grey+alpha, 6 RGBA
    int bitDepth = 8;
    bool interlaced = false;
    // Indexed images: the palette (PLTE), per-entry alpha (tRNS, 255 where absent) and one index
    // per pixel.
    std::vector<Rgb8> palette;
    std::vector<uint8_t> paletteAlpha;
    std::vector<uint8_t> indices;
    // Every image: 8-bit RGBA per pixel (16-bit samples are reduced to their high byte; a tRNS
    // colour key of a grey/RGB image becomes alpha 0).
    std::vector<uint8_t> rgba;
    bool indexed() const { return colorType == 3; }
};

// Decodes a PNG file. Throws FormatError (bad signature, CRC, unsupported features, truncation).
// Images larger than maxPixels are refused.
DecodedPng decodePng(std::span<const uint8_t> file, size_t maxPixels = 16u << 20);

// Indexed 8-bit PNG with a compressed IDAT. alpha: empty or one entry per palette colour (tRNS).
std::vector<uint8_t> encodePngIndexed(int width, int height, std::span<const uint8_t> indices,
                                      std::span<const Rgb8> palette, std::span<const uint8_t> alpha = {});
// RGBA 8-bit PNG with a compressed IDAT (tests, generated sample art).
std::vector<uint8_t> encodePngRgba(int width, int height, std::span<const uint8_t> rgba);

}  // namespace tmport::mods
