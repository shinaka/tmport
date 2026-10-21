#pragma once
// .PAK sprite packs of the 1990 DOS edition (re/dos/docs/fmt_graphics.md §5): SPRITES (223 frames,
// scenes 0..2), BEGIN (42, title and sign-in), CLOSE (133, castle top) and SHACK (126, treasure
// room). The game loads a whole file into one paragraph-aligned buffer (DS:37BC) and finds frame k
// with Sprite_GetHeader 1000:2159 (ES = pak segment + para[k]).
//
// File: u16 count; u16 0, 62, 58 (never read); u16 para[count] (frame k at file offset para*16);
// u16 FFFFh, FFFFh. Frame: 16-byte header {0, width in bytes, height, x offset (bytes, signed),
// y offset (signed), +0A, +0C, +0E}, then one row per line: a length byte L; L & 0x80 = raw row of
// L & 0x7F packed bytes, else L bytes of (count, pixel-pair byte) runs. Every row decodes to
// exactly `width` bytes of packed 4-bpp logical colours (high nibble = left pixel).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tmport::dos {

struct PakFrame {
    int index = 0;
    uint16_t word0 = 0;    // +0, 0 in every frame
    int widthBytes = 0;    // +2 (pixels = 2 * widthBytes)
    int height = 0;        // +4
    int xOffset = 0;       // +6, signed, bytes: hotspot added to the draw x (absolute x for HUD frames)
    int yOffset = 0;       // +8, signed, rows
    uint16_t fieldA = 0;   // +0A, returned in AX by 1000:2159, overwritten by every caller
    uint16_t fieldC = 0;   // +0C, not read by the blitter
    uint16_t fieldE = 0;   // +0E, not read by the blitter
    std::vector<uint8_t> rows;  // decoded rows: height * widthBytes packed bytes
    int rawRows = 0, runRows = 0;
    size_t fileOffset = 0, usedBytes = 0, slotBytes = 0;

    int widthPixels() const { return widthBytes * 2; }
    const uint8_t* row(int y) const { return rows.data() + static_cast<size_t>(y) * widthBytes; }
    uint8_t pixel(int x, int y) const {  // logical colour
        const uint8_t b = row(y)[x >> 1];
        return (x & 1) ? (b & 15) : (b >> 4);
    }
};

class PakFile {
public:
    // Decodes a .PAK file; throws mischief::FormatError on a malformed file.
    static PakFile load(std::span<const uint8_t> file, std::string name);

    const std::string& name() const { return name_; }
    int count() const { return static_cast<int>(frames_.size()); }
    // Frame k (0 <= k < count()).
    const PakFrame& frame(int k) const { return frames_.at(static_cast<size_t>(k)); }
    const std::vector<PakFrame>& frames() const { return frames_; }
    // The three constant header words after the count (0, 62, 58 in all four files).
    const uint16_t* headerWords() const { return header_; }

private:
    std::string name_;
    uint16_t header_[3] = {};
    std::vector<PakFrame> frames_;
};

}  // namespace tmport::dos
