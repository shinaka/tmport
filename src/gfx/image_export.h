#pragma once
// Indexed picture -> PNG export (indexed PNG with the palette; key colours transparent via tRNS).
// The picture is width x height bytes of palette indices, rows top to bottom (gfx::PixMap, or any
// type with `int width, height` and a `pixels` byte container: the templates below).

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "gfx/palette.h"

namespace tmport::gfx {

// transparentKey: make the palette's key colours transparent.
std::vector<uint8_t> indexedToPng(int width, int height, std::span<const uint8_t> pixels, const Palette& palette,
                                  bool transparentKey = true);
bool writeIndexedPng(const std::filesystem::path& path, int width, int height, std::span<const uint8_t> pixels,
                     const Palette& palette, bool transparentKey = true);
// Straight RGBA conversion (alpha 0 for key colours when transparentKey).
std::vector<uint8_t> indexedToRgba(std::span<const uint8_t> pixels, const Palette& palette, bool transparentKey = true);

template <class Pic>
std::vector<uint8_t> pixmapToPng(const Pic& pm, const Palette& palette, bool transparentKey = true) {
    return indexedToPng(pm.width, pm.height, pm.pixels, palette, transparentKey);
}
template <class Pic>
bool writePixmapPng(const std::filesystem::path& path, const Pic& pm, const Palette& palette, bool transparentKey = true) {
    return writeIndexedPng(path, pm.width, pm.height, pm.pixels, palette, transparentKey);
}
template <class Pic>
std::vector<uint8_t> pixmapToRgba(const Pic& pm, const Palette& palette, bool transparentKey = true) {
    return indexedToRgba(pm.pixels, palette, transparentKey);
}

}  // namespace tmport::gfx
