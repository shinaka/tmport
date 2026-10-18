#pragma once
// PNG <-> PMAP for mods (re/docs/fmt_pmap.md).
//
// Import rules:
//   * Indexed PNG whose palette matches the game palette entry for entry (what the exporter
//     writes): indices are taken as they are (the RRGB 900 palette has duplicate colours, so only
//     the index is exact). A palette entry with alpha < 128 that is not a key index becomes the key.
//   * Any other PNG (indexed with another palette, RGB, RGBA, grey): a pixel with alpha < 128 or
//     colour (0,0,170) becomes the colour key (the first key index of the palette: logical 15 in
//     256 colours, nibble 3 in 16 colours); every other pixel takes the nearest palette colour
//     (squared RGB distance, lowest index on ties), never a key index.
//   * 256-colour art is encoded like the original resource (RUND, or raw 8 bpp for the few raw
//     originals); new ids use RUND. 16-colour art is packed 4 bpp.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gfx/palette.h"
#include "gfx/pixmap.h"

namespace tmport::mods {

struct PmapImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> indices;  // width*height palette indices
    bool direct = false;           // indices taken from an indexed PNG with the game palette
    size_t keyPixels = 0;          // transparent pixels
    size_t remapped = 0;           // pixels whose colour was not exactly in the palette
};

// Decodes a PNG into palette indices for `palette` (256-colour logical palette, or the VGA
// 16-colour palette for depth-4 art). Throws FormatError.
PmapImage pngToIndices(std::span<const uint8_t> png, const gfx::Palette& palette);

// Encodes indices as a PMAP body (header + pixels, zero-padded to the NE size).
// depth 8: encoding Raw8 or Rund; depth 4: Raw4.
std::vector<uint8_t> encodePmapBody(const PmapImage& img, int depth, gfx::PixEncoding encoding);

// Editable PNG of a decoded PMAP: indexed, the full palette (256 or 16 entries), key colours
// with alpha 0. Compressed.
std::vector<uint8_t> pixmapToEditablePng(const gfx::PixMap& pm, const gfx::Palette& palette);

}  // namespace tmport::mods
