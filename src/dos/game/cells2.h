#pragma once
// cells2.c (segment 176a), world part: the path links (conveyors) that close the level loops, the
// level-2 gate and the castle creature. The end screens, treasure room and rank screen of the same
// segment are in endgame.h (player). Spec: re/dos/docs/code_world.md §6.1, §6.2, §6.8.
// Owner: world agent.

#include <cstdint>

namespace tmport::dos {

class DosGame;

// 176a:0019 Cell_ConveyLeft(dx, dy): a right-moving object is moved by (dx, dy): the player anywhere
// in the cell (Elves_ShiftNearby first), an elf only at x&0x78 == 0x78.
void cellConveyLeft(DosGame& g, int dx, int dy);
// 176a:00a6 Cell_ConveyLeftIf(dx, dy, xmax, row): elves only: dx > 0, player.x <= xmax (unsigned),
// player row == row -> move by (dx, dy).
void cellConveyLeftIf(DosGame& g, int dx, int dy, uint16_t xmax, uint16_t row);
// 176a:0110 Cell_ConveyRight(dx, dy): a left-moving object: the player anywhere (Elves_ShiftNearby),
// an elf only at x&0x78 == 0.
void cellConveyRight(DosGame& g, int dx, int dy);
// 176a:019d Cell_ConveyRightIf(dx, dy, xmin, row): elves only: dx < 0, player.x >= xmin, player row
// == row -> move by (dx, dy).
void cellConveyRightIf(DosGame& g, int dx, int dy, uint16_t xmin, uint16_t row);

// The seam handlers; all return 0xFC.
// 176a:0006 Cell_P1 (type 1, FF): Cell_ConveyLeft(-0x500, -0x80) into E5.
uint16_t cellP1(DosGame& g);
// 176a:008a Cell_P2 (type 2, FD): Cell_ConveyLeftIf(-0x500, -0x80, 0x300, 0x700).
uint16_t cellP2(DosGame& g);
// 176a:00fd Cell_P3 (type 3, E3): Cell_ConveyRight(+0x500, +0x80) into FD.
uint16_t cellP3(DosGame& g);
// 176a:0181 Cell_P4 (type 4, E5): Cell_ConveyRightIf(+0x500, +0x80, 0x680, 0x780).
uint16_t cellP4(DosGame& g);
// 176a:01f8 Cell_P13 (type 13, C2): Cell_ConveyLeft(+0x580, -0x100) into AD.
uint16_t cellP13(DosGame& g);
// 176a:020a Cell_P14 (type 14, C0): Cell_ConveyLeftIf(+0x580, -0x100, 0x700, 0x500).
uint16_t cellP14(DosGame& g);
// 176a:0225 Cell_P15 (type 15, AB): Cell_ConveyRight(-0x580, +0x100) into C0.
uint16_t cellP15(DosGame& g);
// 176a:0237 Cell_P16 (type 16, AD): elves moving left at x&0x78 == 0x78 -> C2 (x-0x580, y+0x100)
// while the player is in row 0x600, or in row 0x580 with (x & 0xFF80) > 0x600.
uint16_t cellP16(DosGame& g);
// 176a:029d Cell_P22 (type 22, 88): Cell_ConveyLeft(+0x80, -0x80) into 79.
uint16_t cellP22(DosGame& g);
// 176a:02af Cell_P21 (type 21, 77): Cell_ConveyRight(-0x80, +0x80) into 86.
uint16_t cellP21(DosGame& g);
// 176a:02c1 Cell_P43 (type 43, 86): Cell_ConveyLeftIf(+0x80, -0x80, 0x500, 0x380).
uint16_t cellP43(DosGame& g);
// 176a:02dc Cell_P42_GateL2 (type 42, 79): elf conveyor to 88; key state 6: snap 0x4C0, open
// 0x26 -> 0x3C in 79 and 88 (sfx 8); a later tick above 0x60 -> castle hall door (0x1C0,0x3F0),
// transition 1, area 0x0D, tune 2, g_slowScroll = 0; no hint without the key.
uint16_t cellP42GateL2(DosGame& g);

// 176a:159e Castle_CreatureSpawn (visible part of types 57 and 63): g_castleCellIdx++; with no
// creature and tier >= 1: a player at this cell's centre rolls (rand() & mask[tier]) == 1 (anim
// 0x3C), at the left neighbour's centre == 0 (anim 0x3B); keeps or removes a showing creature.
void castleCreatureSpawn(DosGame& g);

}  // namespace tmport::dos
