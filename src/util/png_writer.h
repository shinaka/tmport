#pragma once
// Minimal PNG encoder: 8-bit RGBA or 8-bit indexed (+palette, optional tRNS), filter 0,
// zlib stream made of stored (uncompressed) deflate blocks. For screenshots and debug dumps.

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "util/rgb.h"

namespace tmport::png {

uint32_t crc32(std::span<const uint8_t> data, uint32_t crc = 0);
uint32_t adler32(std::span<const uint8_t> data, uint32_t adler = 1);

// rgba: width*height*4 bytes, rows top to bottom.
std::vector<uint8_t> encodeRgba(int width, int height, std::span<const uint8_t> rgba);

// indices: width*height bytes. palette: 1..256 entries. alpha: empty (all opaque) or one
// alpha byte per palette entry (written as tRNS, trailing 255s trimmed).
std::vector<uint8_t> encodeIndexed(int width, int height, std::span<const uint8_t> indices,
                                   std::span<const Rgb8> palette, std::span<const uint8_t> alpha = {});

bool writeRgba(const std::filesystem::path& path, int width, int height, std::span<const uint8_t> rgba);
bool writeIndexed(const std::filesystem::path& path, int width, int height, std::span<const uint8_t> indices,
                  std::span<const Rgb8> palette, std::span<const uint8_t> alpha = {});

}  // namespace tmport::png
