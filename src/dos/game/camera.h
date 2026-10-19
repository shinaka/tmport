#pragma once
// The camera and scroll glue of main.c (130f) that belongs to the world: the frame latch
// (Frame_Begin), the full / partial play-field redraw (Playfield_Redraw), the incremental scroll
// (Scroll_Update), and the transition dissolve of Frame_Present. Specs: re/dos/docs/code_world.md §8,
// code_runtime.md §3.4-3.7. Owner: world agent.
//
// The tiles are read from the map and tile buffers as Scene_LoadPan leaves them (code_world.md §7.2):
// the loaded PAN (g.pan) where its decoding and its packed tail wrote, the PAN before it (g.prevPan)
// elsewhere. So the chute cells 3D/4D, whose MOUNT3 blocks lie past CLOSE.PAN's five, show the stale
// MOUNT3 shaft as in the original (§6.10). The scroll and shift primitives are wave 2's
// (dos/gfx/back_buffer.h); the tile columns address the back buffer linearly as Tile_DrawColumn does.

#include <cstddef>
#include <cstdint>

namespace tmport::dos {

class DosGame;

// 130f:025a Frame_Begin (per frame; also called by Game_Restore, Users_InitNewPlayer and
// Users_LoadPlayer): latch camera, deltas, g_sceneWanted, g_fullRedraw, the transition, the rank
// state, the partial-redraw flag and the shelf-room flags; DS:922E |= DS:925B;
// Sprites_Snapshot(0, g_spriteSlotsDrawn); g_stepDone = 0 (g.stepGate().clear()); then the tile
// position DS:920D/920F/9124/9126 of the latched camera (negative x = column 15 of the row above);
// a redraw frame zeroes the latched deltas. Engine: while no dissolve runs, the dissolve's
// remainder in the original frame clock is dropped, so every dissolve spreads over the same frames.
void frameBegin(DosGame& g);

// 130f:0326 Playfield_Redraw (per frame): when g_fullRedraw is latched, redraws the play-field
// (x bytes 2..0x9E, or only the searched cell's band when DS:8FC8 is set) from the tile map at the
// latched camera, adds one dirty rectangle, updates the "last drawn" camera, clears the redraw
// requests (also g_fullRedrawReq), g_dirty++, DS:8FBA = 1. Also called by Hud_Redraw.
void playfieldRedraw(DosGame& g);

// 130f:04e5 Scroll_Update (per frame): unless a UI mode or a HUD redraw is pending, scrolls the
// play-field by one tile column / row in the latched direction and draws the uncovered tiles
// (two with g_slowScroll; slow CPU: rows 0x10..0x6F only), then updates the drawn camera.
void scrollUpdate(DosGame& g);

// The dissolve of 130f:01c2 Frame_Present when the latched transition DS:8FB2 is set and the CPU
// is fast: 350 cells of 10 bytes x 8 rows at (2 + 10 * (rand() & 15) clamped to 0x93, 0x10 + 8 *
// (rand() & 15)) copied from g.back to g.screen one by one (700 rand() calls, row first), then the
// whole buffer; g_transition = 0. The engine spreads the cells over frames by
// g.frameClock().steps(FrameFeature::Dissolve) (period mode: all 350 at once); returns true while
// cells remain (the caller does not run the rest of the frame body then, and calls it again in the
// next frame with g.back unchanged; dirty spans added in between are dropped, the original's
// dissolve being one uninterrupted present). Slow CPU: no cells, no rand, Present_Full at once.
bool transitionDissolve(DosGame& g);
// A dissolve started and has cells left (WorldState::dissolveCellsDone != 0).
bool transitionDissolveRunning(const DosGame& g);

// Tile_DrawColumn 1000:19cf on the map/tile buffers described above: from map word `mapWordIndex`
// down, the addressed 8x8 tiles into g.back at x (bytes), rows top..bottom (stops after
// bottom - top + 1 rows, even inside a tile). Row y starts at byte y * 0xA0, so an x past 0x9F
// continues in the next row, as in the original. Adds the column's dirty rectangle when DS:37AE
// bit 0 is set.
void tileDrawColumn(DosGame& g, int x, int top, int bottom, size_t mapWordIndex);
// Map word index of the tile at byte offset `mapOffset` of `cell`'s block: block = g_cellPicture
// [cell] & 0x7F (cellPictureRaw, so an index past the grid reads g_cellType as the original does).
size_t cellMapWord(const DosGame& g, int cell, int mapOffset);

}  // namespace tmport::dos
