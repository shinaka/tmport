// cells2.c (segment 176a), world part: the path links that close the level loops, the level-2 gate
// and the castle creature (re/dos/docs/code_world.md §3.4, §6.1, §6.2, §6.8). The guards that can
// never decide (`TEST x,0x78 / JC`, `(x & 0x78) > 0x80`) are left out with a note where they stand.
#include "dos/game/cells2.h"

#include "dos/game/anim.h"
#include "dos/game/cells.h"
#include "dos/game/elves.h"
#include "dos/game/game.h"
#include "dos/game/sound.h"

namespace tmport::dos {

namespace {

bool isPlayer(const DosGame& g) { return g.world.curObj.off == kPlayerObjOff; }

// *g_curObj (as cells.cpp: a stray pointer reads a zeroed scratch object).
Obj& cur(DosGame& g) {
    const uint16_t off = g.world.curObj.off;
    const bool elf = off >= dg::kElves && off < elfObjOff(kElfCount) && (off - dg::kElves) % 0x20 == 0;
    if (off == kPlayerObjOff || elf) return objAt(g, g.world.curObj);
    static Obj scratch;
    scratch = Obj{};
    return scratch;
}

void move(Obj& o, int dx, int dy) {
    o.x = static_cast<uint16_t>(o.x + dx);
    o.y = static_cast<uint16_t>(o.y + dy);
}

}  // namespace

// 176a:0019 Cell_ConveyLeft
void cellConveyLeft(DosGame& g, int dx, int dy) {
    Obj& o = cur(g);
    if (o.dx <= 0) return;
    if (isPlayer(g)) {
        // Anywhere in the cell (the guard TEST x,0x78 / JC never jumps); the elves near the player
        // move along so the seam stays invisible.
        elvesShiftNearby(g, dx, dy);
        move(o, dx, dy);
    } else if ((o.x & 0x78) == 0x78) {
        move(o, dx, dy);
    }
}

// 176a:00a6 Cell_ConveyLeftIf: elves only, while the player is near the far side.
void cellConveyLeftIf(DosGame& g, int dx, int dy, uint16_t xmax, uint16_t row) {
    Obj& o = cur(g);
    if (o.dx <= 0 || isPlayer(g)) return;
    if (g.player.obj.x <= xmax && (g.player.obj.y & 0xFF80) == row) move(o, dx, dy);
}

// 176a:0110 Cell_ConveyRight
void cellConveyRight(DosGame& g, int dx, int dy) {
    Obj& o = cur(g);
    if (o.dx >= 0) return;
    if (isPlayer(g)) {  // the guard (x & 0x78) > 0x80 never holds
        elvesShiftNearby(g, dx, dy);
        move(o, dx, dy);
    } else if ((o.x & 0x78) == 0) {
        move(o, dx, dy);
    }
}

// 176a:019d Cell_ConveyRightIf
void cellConveyRightIf(DosGame& g, int dx, int dy, uint16_t xmin, uint16_t row) {
    Obj& o = cur(g);
    if (o.dx >= 0 || isPlayer(g)) return;
    if (g.player.obj.x >= xmin && (g.player.obj.y & 0xFF80) == row) move(o, dx, dy);
}

// 176a:0006 Cell_P1 (FF, level 0 seam): right-movers into E5.
uint16_t cellP1(DosGame& g) {
    cellConveyLeft(g, -0x500, -0x80);
    return 0xFC;
}

// 176a:008a Cell_P2 (FD)
uint16_t cellP2(DosGame& g) {
    cellConveyLeftIf(g, -0x500, -0x80, 0x300, 0x700);
    return 0xFC;
}

// 176a:00fd Cell_P3 (E3): left-movers into FD.
uint16_t cellP3(DosGame& g) {
    cellConveyRight(g, 0x500, 0x80);
    return 0xFC;
}

// 176a:0181 Cell_P4 (E5)
uint16_t cellP4(DosGame& g) {
    cellConveyRightIf(g, 0x500, 0x80, 0x680, 0x780);
    return 0xFC;
}

// 176a:01f8 Cell_P13 (C2, level 1 seam): right-movers into AD.
uint16_t cellP13(DosGame& g) {
    cellConveyLeft(g, 0x580, -0x100);
    return 0xFC;
}

// 176a:020a Cell_P14 (C0)
uint16_t cellP14(DosGame& g) {
    cellConveyLeftIf(g, 0x580, -0x100, 0x700, 0x500);
    return 0xFC;
}

// 176a:0225 Cell_P15 (AB): left-movers into C0.
uint16_t cellP15(DosGame& g) {
    cellConveyRight(g, -0x580, 0x100);
    return 0xFC;
}

// 176a:0237 Cell_P16 (AD): elves walking left at the cell's right edge go to C2 while the player is
// in row 0x600, or in row 0x580 right of x 0x600.
uint16_t cellP16(DosGame& g) {
    Obj& o = cur(g);
    if (o.dx < 0 && !isPlayer(g) && (o.x & 0x78) == 0x78) {
        const uint16_t row = g.player.obj.y & 0xFF80;
        if (row == 0x600 || (row == 0x580 && (g.player.obj.x & 0xFF80) > 0x600)) move(o, -0x580, 0x100);
    }
    return 0xFC;
}

// 176a:029d Cell_P22 (88, level 2 seam): right-movers into 79.
uint16_t cellP22(DosGame& g) {
    cellConveyLeft(g, 0x80, -0x80);
    return 0xFC;
}

// 176a:02af Cell_P21 (77): left-movers into 86.
uint16_t cellP21(DosGame& g) {
    cellConveyRight(g, -0x80, 0x80);
    return 0xFC;
}

// 176a:02c1 Cell_P43 (86)
uint16_t cellP43(DosGame& g) {
    cellConveyLeftIf(g, 0x80, -0x80, 0x500, 0x380);
    return 0xFC;
}

// 176a:02dc Cell_P42_GateL2 (79, the stone arch; no ISPLAYER test, no hint without the key).
// Opening and entering take two ticks with the redraw done in between.
uint16_t cellP42GateL2(DosGame& g) {
    uint16_t& mask = g.world.cellMask;
    mask = 0xFF;
    cellConveyRightIf(g, -0x80, 0x80, 0x300, 0x400);  // elves leaving 79 to the left go to 88
    if (g.core.keyState != 6) {
        mask &= 0xFFFC;
        return mask;
    }
    Obj& o = cur(g);
    if ((o.y & 0x7F) == 0x70) {
        if (o.dy > 0) o.dy = 0;
        mask &= 0xFFFD;
        return mask;
    }
    if (o.x != 0x4C0) ++g.world.fullRedrawReq;  // SNAP(0x4C0)
    o.x = 0x4C0;
    if ((o.y & 0x7F) < 0x68 && g.world.fullRedrawReq == 0) {
        if (g.world.cellPicture[0x79] == 0x26) {  // the arch opens (both twins)
            g.world.cellPicture[0x79] = 0x3C;
            g.world.cellPicture[0x88] = 0x3C;
            sfxPlay(g, 8);
            ++g.world.fullRedrawReq;
        } else if ((o.y & 0x7F) < 0x60) {  // through the arch: the castle hall door (73)
            o.dy = 0;
            o.x = 0x1C0;
            o.y = 0x3F0;
            g.world.slowCpuSaved = g.core.slowCpu;
            musicStop(g);
            musicPlay(g, 2);
            g.core.area = 0x0D;
            g.world.slowScroll = 0;
            g.world.transition = 1;
            g.world.fullRedrawReq = 1;
            animSet(g, 1, 6);
            g.player.playerState = 9;
        }
    }
    return mask;
}

// 176a:159e Castle_CreatureSpawn (visible part of types 57, 63). The creature is anchored to the
// visible-cell index, not to a world cell: it moves with the index while the camera scrolls.
void castleCreatureSpawn(DosGame& g) {
    WorldState& w = g.world;
    ++w.castleCellIdx;
    if (w.creatureCell == 0) {
        if (g.core.tier == 0) return;
        const int16_t playerX = g.player.slots[1].x;
        const uint16_t chance = g.tables.world.creatureMask[g.core.tier & 3];
        if (static_cast<int16_t>(playerX - 0x20) == w.cellScreenX) {  // the player at this cell's centre
            if ((g.rand.rand() & chance) == 1) {
                w.creatureCell = w.castleCellIdx;
                animSetIfIdle(g, 6, 0x3C);
                castlePlaceCreature(g);
                return;
            }
        }
        if (static_cast<int16_t>(playerX + 0x20) == w.cellScreenX) {  // at the left neighbour's centre
            if ((g.rand.rand() & chance) == 0) {
                w.creatureCell = w.castleCellIdx;
                animSetIfIdle(g, 6, 0x3B);
                castlePlaceCreature(g);
            }
        }
    } else if (w.creatureCell == w.castleCellIdx) {
        SpriteSlot& s6 = g.player.slots[6];
        if (s6.anim == 0) {  // the strike finished
            s6.frame = 0xFFFF;
            g.player.slots[7].frame = 0xFFFF;
            s6.prev = 0;
            w.creatureCell = 0;
        }
        castlePlaceCreature(g);  // re-anchor at this cell's current screen position
    }
}

}  // namespace tmport::dos
