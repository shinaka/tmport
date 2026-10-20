#pragma once
// LPX pictures of Gizmos & Gadgets (types G and P; re/gg/docs/fmt_graphics.md §2): decoding to a
// plain picture for the tools, the tests and the mods, and the colour remaps that change a picture
// in place (Gfx_RemapPicture 1399:0b71, Gfx_RecolorPicture 1473:077e).
//
//   +0   'LPX' 0x1A
//   +4   u16 format       0 raw, 1 skip/literal RLE
//   +6   u16 width
//   +8   u16 height
//   +10  u16 key          the keyed raw blit compares pixels with its low byte (1399:03aa)
//   +12  u16 data offset  relative to +12 (2; 0 in 55 RLE frames, whose stream then starts with the
//                         offset word 00 00 = skip 0)
//   raw: width x height bytes; RLE, per row: 00 n = skip n pixels, c (1..255) = c literal bytes, the
//   row ends when an 8-bit counter that starts at (width & 0xFF) reaches 0 (1399:0465).
//
// The engine draws pictures straight from their payload (gg/gfx/screen.h: Lpx_Draw and
// Lpx_DrawKeyed), as the original does; decodeLpx is the reference decoder of tools/gg/lpx.py.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tmport::gg {

struct DecodedLpx {
    int format = 0;  // 0 raw, 1 RLE
    int width = 0;
    int height = 0;
    uint16_t key = 0;
    uint16_t dataOffset = 0;
    std::vector<uint8_t> pixels;  // width x height colour indices (0 where an RLE row skipped)
    std::vector<uint8_t> opaque;  // width x height: 1 = written by the picture, 0 = skipped (RLE)
    size_t used = 0;              // payload bytes consumed
    int counterMismatches = 0;    // RLE rows whose pixel count differs from the width (none shipped)
};

// Decodes an LPX payload exactly as tools/gg/lpx.py decode_lpx does. Throws tmport::FormatError
// for a payload that is not LPX, an unknown format, raw data shorter than width x height, an RLE row
// that runs past the data or wider than the picture, or one that never ends.
DecodedLpx decodeLpx(std::span<const uint8_t> payload);

// The transparency of a decoded picture as the engine's keyed blits see it: raw pictures are keyed
// on the key's low byte, RLE pictures on the skipped pixels. 1 = drawn.
std::vector<uint8_t> keyedMask(const DecodedLpx& pic);

// 1399:0b71 Gfx_RemapPicture: every pixel of a raw picture = lut[pixel], in place in the payload;
// RLE pictures (format bit 0 set) are left alone. A payload too short for its header is ignored.
void remapPicture(std::span<uint8_t> payload, const std::array<uint8_t, 256>& lut);

// 1473:077e Gfx_RecolorPicture(pic, from, to): identity table, then lut[from[i]] = to[i] for i = 0..
// until to[i] == 0xFF (a later pair wins), then Gfx_RemapPicture. The lists end at the first 0xFF of
// `to` (or at the end of the shorter span).
void recolorPicture(std::span<uint8_t> payload, std::span<const uint8_t> from, std::span<const uint8_t> to);

}  // namespace tmport::gg
