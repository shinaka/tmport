#pragma once
// .PAN tile pictures of the 1990 DOS edition (re/dos/docs/fmt_graphics.md §3): OPENING.PAN (one
// 320x200 picture of 40x25 tiles) and the five scenery files MOUNTAIN, MOUNT2N3, MOUNT3, CLOSE and
// SHACK (blocks of 16x16 tiles = 128x128 pixels).
//
// File: four u16 (map packed size, width in tiles, height in tiles, tiles packed size), then the
// word-RLE tile map (1000:2368 / 1000:23b9) and the byte-RLE tiles with escape A5h (1000:240e).
// Scene_LoadPan (1460:05e8) reads each packed block to the END of a fixed buffer and decodes it in
// place towards offset 0. PanFile keeps both buffers exactly as the game then holds them: decoded
// data from offset 0, the not-overwritten tail of the packed block after it. Map words are
// paragraph offsets into the tile buffer (a tile starts at byte word*16); tiles are 8x8 pixels,
// 4 bytes per row, packed 4 bpp with the high nibble = left pixel, in logical colours 0..15.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tmport::dos {

class PanFile {
public:
    static constexpr size_t kMapBufferSize = 0xA400;   // segment 2897 (far pointer DS:37B8), 1460:0654
    static constexpr size_t kTileBufferSize = 0xE880;  // segment 32d7 (far pointer DS:37B4), 1460:0678
    static constexpr int kTileBytes = 32;              // 8x8 pixels, 4 bytes per row
    static constexpr int kBlockWords = 256;            // one 16x16-tile block = 512 bytes = 32 paragraphs

    // Decodes a .PAN file; throws mischief::FormatError on a malformed file.
    static PanFile load(std::span<const uint8_t> file, std::string name);

    const std::string& name() const { return name_; }
    int widthTiles() const { return width_; }    // per picture: OPENING 40, the others 16
    int heightTiles() const { return height_; }  // OPENING 25, the others 16
    int pictureCount() const { return pictures_; }
    size_t mapWordCount() const { return mapWords_; }    // decoded map entries
    size_t tileByteCount() const { return tileBytes_; }  // decoded tile bytes
    int tileCount() const { return static_cast<int>(tileBytes_ / kTileBytes); }
    int rowBackRefs() const { return backRefs_; }        // 0xFFF row copies in the map stream
    size_t mapPackedSize() const { return mapPacked_; }
    size_t tilesPackedSize() const { return tilesPacked_; }

    // Map buffer word at a word index (little endian), as the game reads it; 0 past the buffer
    // (the original would read whatever memory follows it).
    uint16_t mapWord(size_t wordIndex) const;
    // The 32 bytes of the tile a map entry addresses (entry = paragraph offset into the tile
    // buffer); a zero tile when it lies past the buffer.
    std::span<const uint8_t, kTileBytes> tile(uint16_t entry) const;
    // Word index of the first map entry of picture/block k (k * width * height).
    size_t pictureWordIndex(int k) const { return static_cast<size_t>(k) * width_ * height_; }
    // Picture k as logical colours, one byte per pixel, (widthTiles*8) x (heightTiles*8). The map
    // is column-major: entry c*height + r is the tile at column c, row r.
    std::vector<uint8_t> picturePixels(int k) const;

    const std::vector<uint8_t>& mapBuffer() const { return mapBuf_; }
    const std::vector<uint8_t>& tileBuffer() const { return tileBuf_; }

private:
    std::string name_;
    int width_ = 0, height_ = 0, pictures_ = 0, backRefs_ = 0;
    size_t mapWords_ = 0, tileBytes_ = 0, mapPacked_ = 0, tilesPacked_ = 0;
    std::vector<uint8_t> mapBuf_;   // kMapBufferSize bytes
    std::vector<uint8_t> tileBuf_;  // kTileBufferSize bytes
};

}  // namespace tmport::dos
