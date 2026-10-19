#pragma once
// layout.c (segment 1a54) and Clue_BuildWords (46ce:079c): the new-game layout of all 44 hiding
// places (key, treasures, decoys, empty places), the mirror cells, the variable-picture cells, the
// castle ladder layout and the slow-CPU cell patches. Every rejection loop draws from g.rand in the
// original order (§5.2, §5.7). Spec: re/dos/docs/code_elves_layout_hints.md §5, code_ui.md §10.7.
// Owner: elves agent.

#include <cstdint>

namespace tmport::dos {

class DosGame;

// 1a54:0003 Layout_Generate (Game_NewSetup): srand(layoutTimeSeed(g)) (the only srand), clue scheme
// R(3), per level Layout_PlaceKey then Layout_PlaceTreasures, Layout_MirrorSharedCells,
// Castle_PickLayout, Castle_ApplyLayout, Cells_AdjustForCpu.
void layoutGenerate(DosGame& g);
// The srand value of Layout_Generate: g.layoutSeedOverride, else the low 16 bits of Turbo C's
// time() for the host's local wall clock (EST5EDT rule, §5.5). Engine helper.
uint16_t layoutTimeSeed(const DosGame& g);
// 4d1a:0050 time() of Turbo C 2.0 for a local wall-clock date and time (month 1..12): dostounix
// 4d2d:0009 with the default TZ "EST5EDT" (local seconds since 1970 + 18000, - 3600 in US daylight
// saving time). Used by layoutTimeSeed; exposed for tests.
uint32_t turboCTime(int year, int month, int day, int hour, int minute, int second);
// 4d63:01e5 __isDST(hour, yday, month, year - 1970) of Turbo C: month 0 = take the month from the
// 0-based day of the year; DST from the first Sunday of April (the last before 1987) 2:00 to the
// last Sunday of October 2:00.
bool turboCIsDst(int year70, unsigned month, unsigned yday, unsigned hour);
// 1a54:0059 Layout_PutInSlot(picture, content, level): rand() & 0x3F until a free slot of the level
// (0: 29..47, 1: 13..26, 2: 0..10; picture 0 = free); stores picture and content (levels 1/2: the
// key's cell to DS:927A / 927C).
void layoutPutInSlot(DosGame& g, int picture, int content, int level);
// 1a54:0179 Layout_PlaceKey(level): key picture k (rand() & 0x3F, 1..count, with the attributes the
// scheme needs), g_levelPicData[level][1] = g_keyPicture = k, Layout_PutInSlot(k, 0x66 + 2*level),
// Clue_BuildWords(level * 3).
void layoutPlaceKey(DosGame& g, int level);
// 1a54:029a Layout_PlaceTreasures(level): tier from the total (>= 230: 3, >= 115: 2, >= 25: 1; below
// unchanged), t = tier + 2 treasures (match 2), decoys (match 1), the rest (match 0), cycling the
// candidates from rand() % count; treasure ids rand() % 48 + 1 cyclic, skipping collected / shown /
// placed ids (accepted after 48 tries).
void layoutPlaceTreasures(DosGame& g, int level);
// 1a54:06dc Layout_MirrorSharedCells: picture and content of 0xE3+i -> 0xFD+i, 0xAB+i -> 0xC0+i,
// 0x77+i -> 0x86+i (i = 0..2).
void layoutMirrorSharedCells(DosGame& g);
// 1a54:078b Layout_UnmarkSharedCells: content &= 0x7F for 0xFD-0xFF, 0xC0-0xC2, 0x86-0x88 (empty
// 0x80 becomes 0).
void layoutUnmarkSharedCells(DosGame& g);
// 1a54:07d6 Layout_ClearSlots: the 48 hiding cells: picture 0, content 0x80.
void layoutClearSlots(DosGame& g);
// 1a54:0803 Cells_AdjustForCpu: fast CPU: picture[BF] = 0x41, [7E] = 0x37, bridges 84/B7 at 0x2E ->
// 0x2F and type 45; slow CPU: 0x40, 0x2E, both bridges 0x2E and type 28.
void cellsAdjustForCpu(DosGame& g);
// 1a54:085f Cells_ResetVariable: the 9 g_variableCells get their default picture, content 0x80.
void cellsResetVariable(DosGame& g);
// 1a54:0892 Castle_PickLayout: g_castleLayout = rand() % (tier + 1) for tier <= 3 (one rand() even at
// tier 0). Returns the layout.
int castlePickLayout(DosGame& g);
// 1a54:08dc Castle_ApplyLayout: the 20 words of g_castleLayouts[g_castleLayout] into cells 0x32 +
// 16r + c (r 0..4, c 0..3).
void castleApplyLayout(DosGame& g);
// 46ce:079c Clue_BuildWords(base) (quiz.c, called by Layout_PlaceKey with the same rand stream): the 3
// clue words of level base/3 from the key picture's attributes by the scheme (DS:5FB3); size
// attributes draw rand() % 4 until non-zero; stored with bit 7 = not yet revealed.
void clueBuildWords(DosGame& g, int base);

}  // namespace tmport::dos
