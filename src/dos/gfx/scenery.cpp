#include "dos/gfx/scenery.h"

#include "dos/gfx/back_buffer.h"
#include "dos/gfx/pan.h"

namespace tmport::dos {

namespace {

// ROL16(v & 0xFF80, 1) with the bytes swapped: (v >> 7) as a 9-bit value (code_world.md §3.2).
int colpart(uint16_t v) { return ((v >> 7) & 0xFF) | ((v >> 15) << 8); }

}  // namespace

// 130f:025a Frame_Begin, after the STI.
SceneryCamera SceneryCamera::at(uint16_t camX, uint16_t camY) {
    SceneryCamera c;
    c.camX = camX;
    c.camY = camY;
    c.rowOffset = (camY >> 2) & 0x1E;
    c.blockRowBase = colpart(camY) << 4;
    c.columnOffset = (camX << 2) & 0x1E0;
    c.blockColumn = colpart(camX);
    if (c.blockColumn >= 0x10) {  // a negative camera x: column 15 of the row above
        c.blockColumn -= 0x1F0;
        c.blockRowBase -= 0x10;
    }
    return c;
}

size_t blockMapWord(CellPictures pictures, int cell, int mapOffset) {
    const int block = (cell >= 0 && static_cast<size_t>(cell) < pictures.size()) ? (pictures[static_cast<size_t>(cell)] & 0x7F) : 0;
    return static_cast<size_t>(block) * PanFile::kBlockWords + static_cast<size_t>(mapOffset / 2);
}

void drawPlayfield(BackBuffer& buf, const PanFile& pan, CellPictures pictures, const SceneryCamera& cam, int x0, int x1) {
    buf.setColor(0);  // Gfx_SetColor(0): no dirty rectangle per tile
    for (int y = 0x10; y < 0x8F; y += 8) {
        int rowCell = cam.blockColumn + cam.blockRowBase;
        int rowOff = ((y - 0x10) >> 2) + cam.rowOffset;
        if (rowOff >= 0x20) {  // the next block row
            rowCell += 0x10;
            rowOff -= 0x20;
        }
        for (int x = x0; x < x1; x += 4) {
            int colOff = (x - 2) * 8 + cam.columnOffset;
            const int cell = rowCell + (colOff >> 9);
            colOff &= 0x1FF;
            buf.drawTileColumn(x, y, y + 7, pan, blockMapWord(pictures, cell, colOff + rowOff));
        }
    }
    buf.setColor(1);
    buf.dirty().addRect(x0, 0x10, 0x80, x1 - x0 + 1);
}

void scrollColumns(BackBuffer& buf, const PanFile& pan, CellPictures pictures, const SceneryCamera& cam, int delta,
                   bool slowCpu, bool slowScroll) {
    if (delta == 0) return;  // the caller only scrolls on a camera step (the original then uses stale registers)
    int cell = cam.cell();
    int off = cam.columnOffset;
    const int bottom = slowCpu ? 0x6F : 0x8F;
    int x;
    if (delta < 0) {  // camera moved left: content moves right, the new column at x 2
        buf.scrollPlayfieldRight(0x10, bottom, slowScroll);
        x = 2;
    } else {  // content moves left; the new column 38 tiles right of the camera (37 + 1 with slow scroll)
        buf.scrollPlayfieldLeft(0x10, bottom, slowScroll);
        cell += 2;
        off += slowScroll ? 0xA0 : 0xC0;
        x = slowScroll ? 0x96 : 0x9A;
        if (off >= 0x200) {
            off -= 0x200;
            ++cell;
        }
    }
    off += cam.rowOffset;
    int rowInBlock = 0, b = 0x6F;
    if (!slowCpu) {
        rowInBlock = off & 0x1F;
        b = rowInBlock ? 0x8F - 4 * rowInBlock : 0x8F;
    }
    buf.drawTileColumn(x, 0x10, b, pan, blockMapWord(pictures, cell, off));
    if (slowScroll) {  // the second column, rows 0x10..b as well
        int off2 = off + 0x20;
        int cell2 = cell;
        if (off2 >= 0x200) {
            cell2 = cell + 1;
            off2 = 0;
        }
        x += 4;
        buf.drawTileColumn(x, 0x10, b, pan, blockMapWord(pictures, cell2, off2));
    }
    if (rowInBlock != 0)  // the rest of the column from the block row below (at x, which slow scroll moved on)
        buf.drawTileColumn(x, b + 1, 0x8F, pan, blockMapWord(pictures, cell + 0x10, off - cam.rowOffset));
}

void scrollRows(BackBuffer& buf, const PanFile& pan, CellPictures pictures, const SceneryCamera& cam, int delta, bool slowScroll) {
    if (delta == 0) return;
    int cell = cam.cell();
    int rowOff = cam.rowOffset;
    int colOff = cam.columnOffset;
    int y;
    if (delta < 0) {  // camera moved up: content moves down, the new row at the top
        buf.scrollPlayfieldDown(0x10, 0x8F, slowScroll);
        y = 0x10;
    } else {
        buf.scrollPlayfieldUp(0x10, 0x8F, slowScroll);
        y = slowScroll ? 0x80 : 0x88;
        rowOff += slowScroll ? 0x1C : 0x1E;
        if (rowOff >= 0x20) {
            rowOff -= 0x20;
            cell += 0x10;
        }
    }
    for (int x = 2; x < 0x9E; x += 4) {
        buf.drawTileColumn(x, y, y + (slowScroll ? 0x0F : 7), pan, blockMapWord(pictures, cell, colOff + rowOff));
        colOff += 0x20;
        if (colOff >= 0x200) {
            colOff -= 0x200;
            ++cell;
        }
    }
}

void drawFullScreenPicture(BackBuffer& buf, const PanFile& pan) {
    size_t word = 0;  // map pointer DS:37B8 (the picture starts at the buffer's offset 0)
    for (int x = 0; x < 0xA0; x += 4, word += 0x32 / 2) buf.drawTileColumn(x, 0, 0xC7, pan, word);
}

}  // namespace tmport::dos
