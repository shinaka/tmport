#pragma once
// Contact sheet of PixMaps (debug tool): a grid of cells on a checkerboard (so transparent
// pixels are visible), each labelled with "<id> <w>x<h>" in a tiny built-in digit font.

#include <cstdint>
#include <vector>

#include "gfx/palette.h"
#include "gfx/pixmap.h"

namespace tmport::cli {

struct SheetItem {
    int id = 0;
    const gfx::PixMap* pixmap = nullptr;
};

struct SheetImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

SheetImage renderContactSheet(const std::vector<SheetItem>& items, const gfx::Palette& palette, bool transparentKey,
                              int cell = 128, int columns = 8);

// Draws text made of digits, 'x', '-' and spaces (3x5 glyphs) into an RGBA image.
void drawTinyText(SheetImage& img, int x, int y, const char* text, uint32_t rgba);

}  // namespace tmport::cli
