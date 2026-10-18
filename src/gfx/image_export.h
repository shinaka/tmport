#pragma once
// PixMap -> PNG export (indexed PNG with the palette; key colours transparent via tRNS).

#include <cstdint>
#include <filesystem>
#include <vector>

#include "gfx/palette.h"
#include "gfx/pixmap.h"

namespace tmport::gfx {

// transparentKey: make the palette's key colours transparent.
std::vector<uint8_t> pixmapToPng(const PixMap& pm, const Palette& palette, bool transparentKey = true);
bool writePixmapPng(const std::filesystem::path& path, const PixMap& pm, const Palette& palette, bool transparentKey = true);

// Straight RGBA conversion (alpha 0 for key colours when transparentKey).
std::vector<uint8_t> pixmapToRgba(const PixMap& pm, const Palette& palette, bool transparentKey = true);

}  // namespace tmport::gfx
