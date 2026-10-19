#pragma once
// Pictures of the 1990 DOS edition for mods: PAK sprite frames and PAN scenery pictures <-> PNG in
// logical colours, and the original packers' encodings (docs/MODDING.md "The 1990 DOS edition",
// re/dos/docs/fmt_graphics.md §3, §5).
//
// Every picture of the DOS edition is stored in logical colours 0..15; each look shows them through
// its own table in SST.EXE (dos/gfx/palettes.h): EGA and Tandy as the same RGBI colours, MCGA as
// softer DAC colours, CGA as a 2-bit dither pattern per colour. The PNGs hold the logical colour as
// the palette index, with the EGA colours as the palette, so one edited picture changes all four
// looks the way the original's would.
//
// Encoders (checked against every file of V 1.0, see tests/test_dos_mods.cpp):
//   PAK rows   (count, byte) runs (greedy, at most 255 per run) when that is at least two bytes
//              shorter than the raw row, else raw (fmt_graphics.md §5.2).
//   PAN map    per picture row: a 0xFFF back-reference to the first earlier identical row when the
//              row needs more than two words (16-wide pictures only: the decoder copies 16 entries),
//              else runs of up to 16 equal entries ((n-1) << 12 | v) and single entries.
//   PAN tiles  byte RLE with escape A5h: runs of 4..255 equal bytes as A5 n v, a literal A5 as A5 A5.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "util/rgb.h"

namespace tmport::dos {

class DosExe;
class PakFile;
struct PakFrame;
class PanFile;

// A picture in logical colours, one byte (0..15) per pixel.
struct LogicalImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    size_t remapped = 0;        // PNG pixels that were not one of the 16 colours (nearest used)
    size_t distinctColours = 0; // distinct colours of the PNG
    bool operator==(const LogicalImage& o) const { return width == o.width && height == o.height && pixels == o.pixels; }
};

// The colours of the editable PNGs: the EGA look's (logical colour i -> RGB).
using DosColours = std::array<Rgb8, 16>;
DosColours dosPngColours(const DosExe& exe);

// Indexed PNG (16-entry palette = colours). Sprites mark colour 0 with alpha 0: it is the colour the
// masked blit leaves out (an opaque draw shows it as its colour).
std::vector<uint8_t> logicalToPng(const LogicalImage& img, const DosColours& colours, bool sprite);
// PNG -> logical colours. An indexed PNG whose first 16 palette entries are `colours` (what the
// exporter writes) gives its indices (an index above 15 is an error). Any other PNG: a pixel with
// alpha < 128 becomes colour 0 for a sprite (an error for scenery, which has no transparency);
// every other pixel takes the nearest of the 16 colours (squared RGB distance, lowest index on
// ties). Throws tmport::FormatError.
LogicalImage pngToLogical(std::span<const uint8_t> png, const DosColours& colours, bool sprite);

// ---- PAK sprite packs ---------------------------------------------------------------------------

LogicalImage pakFramePixels(const PakFrame& f);

struct PakFrameHeader {
    uint16_t word0 = 0;
    int xOffset = 0;  // bytes (2-pixel units), signed
    int yOffset = 0;  // rows, signed
    uint16_t fieldA = 0, fieldC = 0, fieldE = 0;
};
PakFrameHeader pakFrameHeader(const PakFrame& f);

// A frame as the original packer stored it: the 16-byte header and the rows, without the slot's
// padding. Throws tmport::FormatError for an odd pixel width or a row that fits neither form
// (raw rows hold at most 127 bytes, run rows at most 63 runs).
std::vector<uint8_t> encodePakFrame(const LogicalImage& img, const PakFrameHeader& h);

// Changes to one frame; unset parts keep the frame's own.
struct PakFrameEdit {
    std::optional<LogicalImage> image;
    std::optional<int> xOffset, yOffset;
    std::optional<uint16_t> fieldA, fieldC, fieldE;
};

struct PakRebuild {
    std::vector<uint8_t> file;
    int framesChanged = 0;
};
// The file with the edits applied. Frames whose pixels and header stay as they are keep their bytes
// (including the slot's padding, which holds leftovers of the original's packing tool); changed
// frames are encoded and zero-padded to their 16-byte slot. The frame count never changes. Throws
// tmport::FormatError (an edit of a frame that does not exist, a frame that cannot be encoded).
PakRebuild rebuildPak(std::span<const uint8_t> base, const std::map<int, PakFrameEdit>& edits, const std::string& name);

// How many frames of a pack the encoder reproduces byte for byte (header and rows, not the padding).
int pakFramesReproduced(std::span<const uint8_t> file, const std::string& name);

// ---- PAN tile pictures ----------------------------------------------------------------------------

// The decoded content of a PAN: map words (column-major per picture) and tiles (32 bytes each).
struct PanContent {
    int width = 0, height = 0;  // tiles per picture row / column
    int pictures = 0;
    std::vector<uint16_t> map;  // pictures * width * height entries (tile number * 2)
    std::vector<uint8_t> tiles;
};
PanContent panContent(const PanFile& pan);
LogicalImage panPicture(const PanContent& c, int k);
// The file of a content, packed like the original. Throws tmport::FormatError for a map entry the
// format cannot hold.
std::vector<uint8_t> encodePan(const PanContent& c);

struct PanRebuild {
    std::vector<uint8_t> file;
    int picturesChanged = 0;
    int tiles = 0;  // tiles of the new file
};
// The file with pictures replaced: unchanged pictures keep their map entries, a changed picture
// reuses identical tiles and appends new ones, tiles no picture uses any more are dropped. The
// result is decoded in place the way Scene_LoadPan does and compared with the intended pictures,
// so a file the game would unpack wrongly (a packed block overwritten before it is read, a buffer
// overflow) is refused. Throws tmport::FormatError.
PanRebuild rebuildPan(std::span<const uint8_t> base, const std::map<int, LogicalImage>& pictures, const std::string& name);

}  // namespace tmport::dos
