#pragma once
// Scenery drawing of the 1990 DOS edition: the loaded PAN's blocks laid out on the 16x16 block grid
// (g_cellPicture DS:0CAA) and shown for a camera (re/dos/docs/code_world.md §8, code_runtime.md
// §3.5-3.7). These are the drawing parts of main.c's Frame_Begin (130f:025a, the latched tile
// position), Playfield_Redraw (130f:0326) and Scroll_Update (130f:04e5), plus the full-screen picture
// of the title and sign-in (Screen_DrawBackdrop 47c7:042d). The gating, bookkeeping and redraw
// requests stay with the main loop (wave 3).
//
// World coordinates are pixels; a block is 128x128. Only the camera's tile position is used: the
// scenery is drawn for camX & ~7 and camY & ~7 (sprites use the exact camera).

#include <cstddef>
#include <cstdint>
#include <span>

namespace tmport::dos {

class BackBuffer;
class PanFile;

// The block grid: 256 words, index row*16 + column; the block number is in bits 0..6.
using CellPictures = std::span<const uint16_t>;

// The tile position latched by Frame_Begin 130f:025a.
struct SceneryCamera {
    uint16_t camX = 0, camY = 0;  // DS:37F2 / DS:37F4
    int blockColumn = 0;          // DS:9124
    int blockRowBase = 0;         // DS:9126, block row * 16
    int columnOffset = 0;         // DS:920D, (camX & 0x78) << 2: byte offset of the tile column in a block's map
    int rowOffset = 0;            // DS:920F, (camY >> 2) & 0x1E: byte offset of the tile row in a map column

    // colpart(v) = (v >> 7) as 9 bits; a negative camera x (block column 0x1FF) means column 15 of
    // the row above.
    static SceneryCamera at(uint16_t camX, uint16_t camY);
    int cell() const { return blockColumn + blockRowBase; }
};

// Map word index of the tile at byte offset `mapOffset` (column*32 + row*2) of `cell`'s block:
// map paragraph mapSeg + (picture[cell] & 0x7F) * 32. Cells outside the 256-entry grid read block 0
// (the original would read the words after DS:0CAA+0x200; cameras inside the world never do).
size_t blockMapWord(CellPictures pictures, int cell, int mapOffset);

// Playfield_Redraw 130f:0326 step 3: the 16 tile rows (y 0x10..0x88) of the tile columns
// x = x0, x0+4, ... < x1 (bytes; the whole play field is 2..0x9E, 39 columns), one Tile_DrawColumn
// per tile. The original clears DS:37AE bit 0 around it (no dirty rectangles per tile) and then
// adds one rectangle (x0, 0x10, x1-x0+1 wide, 0x80 rows); this function does the same with the
// buffer's flag.
void drawPlayfield(BackBuffer& buf, const PanFile& pan, CellPictures pictures, const SceneryCamera& cam, int x0 = 2,
                   int x1 = 0x9E);

// The horizontal part of Scroll_Update 130f:04e5 (steps 1-7) for camera delta `delta` (DS:37F6,
// sign only): scrolls the play field by one tile column (two with slowScroll, g_slowScroll DS:3D31)
// and draws the uncovered column(s). slowCpu (g_slowCpu): rows 0x10..0x6F only.
void scrollColumns(BackBuffer& buf, const PanFile& pan, CellPictures pictures, const SceneryCamera& cam, int delta,
                   bool slowCpu, bool slowScroll);
// The vertical part (DS:37F8): scrolls by one tile row (two with slowScroll) and draws the new row(s).
void scrollRows(BackBuffer& buf, const PanFile& pan, CellPictures pictures, const SceneryCamera& cam, int delta,
                bool slowScroll);

// Screen_DrawBackdrop 47c7:042d (and TitleStep_DrawTitle 4ba4:0045): 40 Tile_DrawColumn calls,
// x = 0..0x9C step 4, rows 0..0xC7, map pointer advancing by 0x32 bytes (25 words) per column: the
// whole 320x200 picture of OPENING.PAN.
void drawFullScreenPicture(BackBuffer& buf, const PanFile& pan);

}  // namespace tmport::dos
