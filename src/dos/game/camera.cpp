// The world's part of main.c (130f): Frame_Begin, Playfield_Redraw, Scroll_Update and the dissolve
// of Frame_Present (re/dos/docs/code_world.md §8, code_runtime.md §3.4-3.7), on the map and tile
// buffers as the PAN loader leaves them (§6.10, §7.2).
#include "dos/game/camera.h"

#include "dos/app/dos_runtime.h"
#include "dos/game/anim.h"
#include "dos/game/game.h"
#include "dos/gfx/back_buffer.h"
#include "dos/gfx/pan.h"

namespace tmport::dos {

namespace {

// colpart(v) = (v >> 7) as 9 bits (ROL16(v & 0xFF80, 1) with the bytes swapped, §3.2).
uint16_t colpart(uint16_t v) { return static_cast<uint16_t>(((v >> 7) & 0xFF) | ((v >> 15) << 8)); }

// The reference timing of a scroll frame (FrameTiming units, thousandths of a PIT tick: the shift,
// the uncovered tiles, the sprites and the present of the whole play field the shift marked dirty).
// The frame began right after the game tick that moved the camera; the FRAME marks of the r2 logs
// (130f:0097, after the frame's drawing) put the next frame 6-7 PIT ticks after that tick for a
// horizontal step (r2/fl_walk2, fl_countup, fl_bridge_l3_jump: 14 steps) and 6-8 for a vertical one
// (r2/fl_climb, fl_castle_t3_a, fl_geyser_slide, fl_bridge_l3_jump: 76 steps). A step in both
// directions in one frame (one present, two shifts) and the slow CPU's 16-pixel steps (rows
// 0x10..0x6F) are not measured and take the same values.
constexpr uint32_t kScrollFrameX = 6500;
constexpr uint32_t kScrollFrameY = 7000;
constexpr uint32_t kScrollFrameXY = 7500;

// The map buffer (segment 2897) and the tile buffer (segment 32D7) as Scene_LoadPan leaves them:
// each PAN writes its decoded data from offset 0 and its packed block at the buffer's end and
// clears nothing else, so the bytes in between still belong to the PAN loaded before it.
class PanBuffers {
public:
    PanBuffers(const PanFile* pan, const PanFile* prev) : pan_(pan), prev_(prev == pan ? nullptr : prev) {}

    uint16_t mapWord(size_t wordIndex) const {
        return static_cast<uint16_t>(mapByte(2 * wordIndex) | (mapByte(2 * wordIndex + 1) << 8));
    }
    uint8_t tileByte(size_t i) const {
        if (i >= PanFile::kTileBufferSize) return 0;
        const PanFile* p = pan_ && wroteTiles(*pan_, i) ? pan_ : prev_ && wroteTiles(*prev_, i) ? prev_ : pan_;
        return p ? p->tileBuffer()[i] : 0;
    }

private:
    uint8_t mapByte(size_t i) const {
        if (i >= PanFile::kMapBufferSize) return 0;
        const PanFile* p = pan_ && wroteMap(*pan_, i) ? pan_ : prev_ && wroteMap(*prev_, i) ? prev_ : pan_;
        return p ? p->mapBuffer()[i] : 0;
    }
    static bool wroteMap(const PanFile& p, size_t i) {
        return i < 2 * p.mapWordCount() || i >= PanFile::kMapBufferSize - p.mapPackedSize();
    }
    static bool wroteTiles(const PanFile& p, size_t i) {
        return i < p.tileByteCount() || i >= PanFile::kTileBufferSize - p.tilesPackedSize();
    }

    const PanFile* pan_;
    const PanFile* prev_;
};

}  // namespace

// 1000:19cf Tile_DrawColumn on the buffers above (wave 2's BackBuffer::drawTileColumn reads one
// PanFile and clips each row, so it cannot show the stale blocks).
void tileDrawColumn(DosGame& g, int x, int top, int bottom, size_t mapWordIndex) {
    const PanBuffers buffers(g.pan, g.prevPan);
    BackBuffer& buf = g.back;
    const int rows = bottom - top + 1;
    buf.noteDrawn(x, top, 4, rows);
    auto bytes = buf.bytes();
    int y = top, left = rows;
    while (left > 0) {
        const size_t tile = static_cast<size_t>(buffers.mapWord(mapWordIndex++)) * 16;  // a paragraph
        for (int ty = 0; ty < 8 && left > 0; ++ty, ++y, --left) {
            for (int k = 0; k < 4; ++k) {
                const long at = static_cast<long>(y) * BackBuffer::kRowBytes + x + k;
                if (at >= 0 && at < static_cast<long>(bytes.size()))
                    bytes[static_cast<size_t>(at)] = buffers.tileByte(tile + static_cast<size_t>(ty * 4 + k));
            }
        }
    }
}

// Engine helper: the map pointer of Playfield_Redraw 130f:041e and Scroll_Update 130f:05c3 / 0657 /
// 072e (mapSeg + (g_cellPicture[cell] & 0x7F) * 32 paragraphs, plus the byte offset).
size_t cellMapWord(const DosGame& g, int cell, int mapOffset) {
    const int block = cellPictureRaw(g, g.tables, cell) & 0x7F;
    return static_cast<size_t>(block) * PanFile::kBlockWords + static_cast<size_t>(mapOffset / 2);
}

// 130f:025a Frame_Begin
void frameBegin(DosGame& g) {
    WorldState& w = g.world;
    // The latches (taken under CLI in the original).
    w.frameCamX = w.cameraX;
    w.frameCamY = w.cameraY;
    w.frameCamStepX = w.camStepX;
    w.frameCamStepY = w.camStepY;
    w.sceneWanted = g.core.level;
    w.fullRedraw = w.fullRedrawReq;
    w.frameTransition = w.transition;
    w.frameRankState = g.player.rankState;
    w.framePartialRedraw = w.partialRedrawReq;
    w.frameInShelfRoom = w.inShelfRoom;
    g.ui.hudRedraw |= g.ui.hudRedrawReq2;
    spritesSnapshot(g, 0, g.core.spriteSlotsDrawn);
    g.stepGate().clear();
    // The tile position of the latched camera: its low 3 bits are ignored for the scenery.
    w.camTileRowOff = static_cast<uint16_t>((w.frameCamY >> 2) & 0x1E);
    w.camCellRow16 = static_cast<uint16_t>(colpart(w.frameCamY) << 4);
    w.camTileColOff = static_cast<uint16_t>((w.frameCamX << 2) & 0x1E0);
    w.camCellCol = colpart(w.frameCamX);
    if (w.camCellCol >= 0x10) {  // a negative camera x: column 15 of the row above
        w.camCellCol = static_cast<uint16_t>(w.camCellCol - 0x1F0);
        w.camCellRow16 = static_cast<uint16_t>(w.camCellRow16 - 0x10);
    }
    if (w.fullRedraw != 0) {  // no incremental scroll in a redraw frame
        w.frameCamStepX = 0;
        w.frameCamStepY = 0;
    }
    // Engine: while no dissolve runs, its share of the original frame clock starts again from zero,
    // so a dissolve latched by the next frame always spreads over the same frames (7 at the
    // reference speed: 350 cells in 127 ms, the DOSBox-X timing of r2/fl_l2door).
    if (w.dissolveCellsDone == 0) g.frameClock().reset(FrameFeature::Dissolve);
}

// 130f:0326 Playfield_Redraw
void playfieldRedraw(DosGame& g) {
    WorldState& w = g.world;
    if (w.fullRedraw == 0) return;
    uint16_t x0 = 2, x1 = 0x9E;
    if (w.framePartialRedraw != 0) {
        // Only the searched cell (or its neighbour by g_searchSide) plus one tile column, from the
        // player's cell as an unwrapped strip position (row * 0x800 + x), on the live camera.
        const Obj& p = g.player.obj;
        const auto lin = static_cast<uint16_t>(((p.y & 0xFF80) << 4) + (p.x & 0xFF80) - ((w.cameraY & 0xFF80) << 4) -
                                               w.cameraX);
        const auto t = static_cast<uint16_t>((static_cast<int16_t>(lin) >> 1) & 0xFFFC);
        x0 = static_cast<uint16_t>(t + (g.player.searchSide << 6) + 2);
        x1 = static_cast<uint16_t>(x0 + 0x44);
        if (static_cast<int16_t>(x0) < 2) x0 = 2;
        if (x1 > 0x9E) x1 = 0x9E;
    }
    g.back.setColor(0);  // Gfx_SetDirtyTracking(0): no rectangle per tile
    for (int y = 0x10; y < 0x8F; y += 8) {
        int rowCell = w.camCellCol + w.camCellRow16;
        int rowOff = ((y - 0x10) >> 2) + w.camTileRowOff;
        if (rowOff >= 0x20) {  // the next block row
            rowCell += 0x10;
            rowOff -= 0x20;
        }
        uint16_t x = x0;
        do {  // a do-while in the original: the first column of a row is drawn unconditionally
            int colOff = (static_cast<uint16_t>(x - 2) << 3) + w.camTileColOff;
            const int cell = rowCell + (colOff >> 9);
            colOff &= 0x1FF;
            tileDrawColumn(g, x, y, y + 7, cellMapWord(g, cell, colOff + rowOff));
            x = static_cast<uint16_t>(x + 4);
        } while (x < x1);
    }
    g.back.setColor(1);
    if (x1 >= x0) g.back.dirty().addRect(x0, 0x10, 0x80, x1 - x0 + 1);
    w.drawnCamX = w.frameCamX;
    w.drawnTileColOff = w.camTileColOff;
    w.drawnCellCol = w.camCellCol;
    w.drawnCamY = w.frameCamY;
    w.drawnTileRowOff = w.camTileRowOff;
    w.drawnCellRow16 = w.camCellRow16;
    w.frameCamStepX = 0;
    w.frameCamStepY = 0;
    w.fullRedraw = 0;
    // A request a tick made after Frame_Begin latched it is lost here (§8.5); in lockstep the tick
    // runs before the frame body, so none is.
    w.fullRedrawReq = 0;
    w.framePartialRedraw = 0;
    w.partialRedrawReq = 0;
    ++g.core.dirty;
    w.playfieldRedrawn = 1;  // the shelves are drawn again
}

// 130f:04e5 Scroll_Update
void scrollUpdate(DosGame& g) {
    WorldState& w = g.world;
    if (g.core.uiMode != 0 || g.ui.hudRedraw != 0) return;
    const bool slowCpu = g.core.slowCpu == 1;
    const bool slowScroll = w.slowScroll == 1;
    bool scrolledX = false, scrolledY = false;

    // Horizontal: one tile column (two with g_slowScroll) at the side the camera moved to.
    if (w.frameCamX != w.drawnCamX && w.frameCamStepX != 0 &&
        (w.camTileColOff != w.drawnTileColOff || w.camCellCol != w.drawnCellCol)) {
        int off = w.camTileColOff;
        int cell = w.camCellCol + w.camCellRow16;
        const int bottom = slowCpu ? 0x6F : 0x8F;  // slow CPU: rows 112..143 neither shifted nor drawn
        int x = 0;
        if (w.frameCamStepX < 0) {
            g.back.scrollPlayfieldRight(0x10, bottom, slowScroll);
            x = 2;
        } else {
            g.back.scrollPlayfieldLeft(0x10, bottom, slowScroll);
            cell += 2;
            off += slowScroll ? 0xA0 : 0xC0;
            x = slowScroll ? 0x96 : 0x9A;
            if (off >= 0x200) {
                off -= 0x200;
                ++cell;
            }
        }
        off += w.camTileRowOff;
        int b = 0x8F, rowInBlock = 0;
        if (slowCpu) {
            b = 0x6F;
        } else {
            rowInBlock = off & 0x1F;
            if (rowInBlock != 0) b = 0x8F - 4 * rowInBlock;
        }
        tileDrawColumn(g, x, 0x10, b, cellMapWord(g, cell, off));
        if (slowScroll) {  // the second column, with the first piece's bottom b
            int cell2 = cell, off2 = off + 0x20;
            if (off2 >= 0x200) {  // the next block column; the row offset is dropped here
                cell2 = cell + 1;
                off2 = 0;
            }
            x += 4;
            tileDrawColumn(g, x, 0x10, b, cellMapWord(g, cell2, off2));
        }
        if (rowInBlock != 0)  // the rest from the block row below, at the current x
            tileDrawColumn(g, x, b + 1, 0x8F, cellMapWord(g, cell + 0x10, off - w.camTileRowOff));
        w.drawnCamX = w.frameCamX;
        w.drawnTileColOff = w.camTileColOff;
        w.drawnCellCol = w.camCellCol;
        ++g.core.dirty;
        scrolledX = true;
    }

    // Vertical: one tile row (two with g_slowScroll) at the edge the camera moved to.
    if (w.frameCamY != w.drawnCamY && w.frameCamStepY != 0 &&
        (w.camTileRowOff != w.drawnTileRowOff || w.camCellRow16 != w.drawnCellRow16)) {
        int rowOff = w.camTileRowOff;
        int cell = w.camCellCol + w.camCellRow16;
        int colOff = w.camTileColOff;
        int y = 0;
        if (w.frameCamStepY < 0) {
            g.back.scrollPlayfieldDown(0x10, 0x8F, slowScroll);
            y = 0x10;
        } else {
            g.back.scrollPlayfieldUp(0x10, 0x8F, slowScroll);
            y = slowScroll ? 0x80 : 0x88;
            rowOff += slowScroll ? 0x1C : 0x1E;
            if (rowOff >= 0x20) {
                rowOff -= 0x20;
                cell += 0x10;
            }
        }
        for (int x = 2; x < 0x9E; x += 4) {
            tileDrawColumn(g, x, y, y + (slowScroll ? 0x0F : 7), cellMapWord(g, cell, colOff + rowOff));
            colOff += 0x20;
            if (colOff >= 0x200) {
                colOff -= 0x200;
                ++cell;
            }
        }
        w.drawnCamY = w.frameCamY;
        w.drawnTileRowOff = w.camTileRowOff;
        w.drawnCellRow16 = w.camCellRow16;
        ++g.core.dirty;
        scrolledY = true;
    }

    // The reference timing (long frames, dos_runtime.h FrameTiming): the step was asked for by the
    // previous game tick. A frame that begins when this one ends (a full redraw the tick inside it
    // asked for, the next step) starts later: r2/fl_bridge_l3 log tick 1111 is gated because the
    // horizontal step, the redraw of the breaking bridge and the first vertical step of the fall
    // follow each other.
    if (scrolledX || scrolledY) {
        FrameTiming& t = g.rt.frameTiming();
        t.fromPreviousTick();
        t.spend(scrolledX && scrolledY ? kScrollFrameXY : scrolledX ? kScrollFrameX : kScrollFrameY);
    }
}

// 130f:01c2 Frame_Present, the transition branch. Quirk kept (PORTING_NOTES §6): the 700 rand()
// calls, which shift the random sequence of everything after a transition.
bool transitionDissolve(DosGame& g) {
    WorldState& w = g.world;
    if (g.core.slowCpu == 0) {
        g.back.dirty().clear();  // Dirty_Clear (again on each continuing frame: nothing else shows)
        if (w.dissolveCellsDone == 0) g.frameClock().reset(FrameFeature::Dissolve);
        int n = g.frameClock().steps(FrameFeature::Dissolve);
        while (n-- > 0 && w.dissolveCellsDone < kDissolveCells) {
            const int y = (g.rand.rand() & 0x0F) * 8 + 0x10;  // row first
            int xs = (g.rand.rand() & 0x0F) * 10;
            if (xs > 0x91) xs = 0x91;
            g.back.dirty().addRect(xs + 2, y, 8, 0x0A);
            presentDirty(g.back, g.screen);
            ++w.dissolveCellsDone;
        }
        if (w.dissolveCellsDone < kDissolveCells) return true;
    }
    w.dissolveCellsDone = 0;
    presentFull(g.back, g.screen);
    w.transition = 0;
    return false;
}

// Engine helper: the dissolve of 130f:01c2 spans frames.
bool transitionDissolveRunning(const DosGame& g) { return g.world.dissolveCellsDone != 0; }

}  // namespace tmport::dos
