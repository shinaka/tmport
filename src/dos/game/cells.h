#pragma once
// cells.c (segment 1568): the terrain handlers of the 16x16 cell world. Every cell type has an
// object handler (g_cellPlayerHandlers DS:08A6, called by Cell_GetMask for g_curObj: the player in
// Player_Move, each elf in Elves_Update; returns the terrain mask) and a visible handler
// (g_cellVisibleHandlers DS:09D2, called per tick for the 4 cells of the camera row). Handlers act on
// objAt(g, g.world.curObj) and set g.world.cellMask; visible calls see the STALE g_curObj of the last
// Cell_GetMask (code_world.md §4.3). Spec: re/dos/docs/code_world.md §4-§6, §12.
// Owner: world agent. The 176a handlers are in cells2.h (world) and endgame.h (player).
//
// Conventions: object handlers return the mask (the engine returns it on every path where the
// original left another value in AX, code_world.md §4.5); "o" below is the current object;
// ISPLAYER = g_curObj == kPlayerObjOff.

#include <cstdint>

namespace tmport::dos {

class DosGame;

// ---- dispatch ----------------------------------------------------------------------------
// 1568:1fe4 Cell_GetMask: g_cellCallKind = 2, g_curCell = cellIndexOf(o.x, o.y) (unmasked), calls
// the object handler of (signed) g_cellType[g_curCell] (cellTypeRaw) and returns its mask.
uint16_t cellGetMask(DosGame& g);
// 1568:1f0b Cells_UpdateVisible (per tick, g_uiMode == 0): g_castleCellIdx = 0; for the 4 cells from
// cellIndexOf(camX, camY): g_cellScreenX = 2 - ((camX & 0x78) >> 1) + 0x40k, g_cellScreenY = 0x10 -
// (camY & 0x7F), g_cellCallKind = 1, the visible handler of the cell's type; slot 12 cleared when
// no decoration was placed (g_decorCount == 0).
void cellsUpdateVisible(DosGame& g);
// g_cellPlayerHandlers DS:08A6[type] (types 0..74; 74 and unused entries: Null_Handler -> 0).
uint16_t callCellObjectHandler(DosGame& g, int type);
// g_cellVisibleHandlers DS:09D2[type] (Null_Handler entries do nothing).
void callCellVisibleHandler(DosGame& g, int type);

// ---- shared helpers (§6.1) ----------------------------------------------------------------
// 1568:0134 Cell_GroundCheckObj: any object on the path line: PATH; above 0x6C: Music_Stop,
// transition 1, redraw, player state 9, return 1; door column 0x26..0x5A; else 0.
int cellGroundCheckObj(DosGame& g);
// 1568:0203 Cell_GroundCheckPlayer: as Cell_GroundCheckObj for the player only, threshold 0x68,
// transition 2; 0 for an elf.
int cellGroundCheckPlayer(DosGame& g);
// 1568:0e6d Cell_LevelBoundary01: loaded scene 1 and dy > 0: level 0, key state 2; loaded scene 0
// and dy < 0: level 1, key state 0x0A if the level-1 key (0x68) is still in slots 13..29, else 4.
void cellLevelBoundary01(DosGame& g);
// 1568:12b4 Cell_LevelBoundary12: scene 2, dy > 0: level 1, key state 4; scene 1, dy < 0: level 2,
// key state 0x0C if the level-2 key (0x6A) is in slots 0..13, else 6.
void cellLevelBoundary12(DosGame& g);
// 1568:0ee3 Cell_DrawTreetop(wy): fast CPU and no decoration yet: Cell_DrawObject(cellScreenX+0x20,
// wy - camY + 0x80, 0x17C).
void cellDrawTreetop(DosGame& g, int wy);
// 1568:1556 Cell_DrawGeyser(wy, depth): visible calls only, gate AE open (picture 0x35): slot 12
// anim 0x54 at cellScreenX+0x20, min(wy-camY, 0x3B) + 0x54, depth; g_decorCount++.
void cellDrawGeyser(DosGame& g, int wy, int depth);
// 1568:1e50 Cell_DrawObject(x, y, frame): slot 12 = (x, y), depth 0x1E, frame, owner 12, prev 0;
// g_decorCount++.
void cellDrawObject(DosGame& g, int x, int y, int frame);
// 1568:1e9e Castle_PlaceCreature: slot 6 at (cellScreenX, cellScreenY+0x70) depth 0x50, slot 7 at
// (0, 0) depth 0x4F; g_decorCount++.
void castlePlaceCreature(DosGame& g);
// 1568:18f1 Castle_CreatureHitCheck: on a pending strike (g_creatureStrike) and the player not
// ladder-jumping and at the creature's height: coins -1 (sfx 5), a forced Left/Right key bit.
void castleCreatureHitCheck(DosGame& g);

// ---- object handlers by cell type (§6.2-§6.8) ---------------------------------------------
// 1568:1eca Cell_P0_Void (type 0): border colour counter++ (DS:92C8); returns 0x1001 (fall + Up).
uint16_t cellP0Void(DosGame& g);
// 1568:0008 Cell_P5_CaveDoorW (type 5, EB): player rising above 0x68 -> (0x460,0x70) cell 08,
// camX = x-0x20, transition 2, area 7, tune 2; door column x&0x78 <= 0x40.
uint16_t cellP5CaveDoorW(DosGame& g);
// 1568:00ce Cell_P6_CaveExitW (type 6, 08): walking left -> (0x5B0,0x770) cell EB, area 0, tune 0.
uint16_t cellP6CaveExitW(DosGame& g);
// 1568:01b4 Cell_P9_CaveDoorE (type 9, F7): up -> (0x5B0,0x70) cell 0B, camX = x-0x124 (keep the
// exact camera: sprites 4 px off the tiles), area 0x0B, tune 2.
uint16_t cellP9CaveDoorE(DosGame& g);
// 1568:029d Cell_P10_CaveExitE (type 10, 0B): walking right -> (0x3B0,0x7F0) cell F7, area 0, tune 0.
uint16_t cellP10CaveExitE(DosGame& g);
// 1568:0303 Cell_P34_CaveUp (type 34, 0A): up -> (0xC0,0x170) cell 21; the music stays off.
uint16_t cellP34CaveUp(DosGame& g);
// 1568:0341 Cell_P35_UpperRoomDown (type 35, 21): down passage (any object) -> (0x4C0,0x70) cell 09,
// area 9, tune 2; mask 0x0F.
uint16_t cellP35UpperRoomDown(DosGame& g);
// 1568:0386 Cell_P11_ClubDoor (type 11, F3): up -> start room (0x140,0x70), camX = x-0x80, area 5,
// tune 1; restores the put-away showcase (DS:9254 -> g_shownTreasure).
uint16_t cellP11ClubDoor(DosGame& g);
// 1568:03f6 Cell_P12_StartRoom (type 12, 02): door down to F3 (puts the showcase away), the prize
// picture into the treasure room, the crown hand-over (slot 1 prev 0xA5) via
// Cell_P71_V71_ToRankScreen.
uint16_t cellP12StartRoom(DosGame& g);
// 1568:05cb Cell_P7_WallLeft (type 7): left wall (any object), mask 0x10DC / 0x10D8, floor step;
// a visible call runs Cell_V7.
uint16_t cellP7WallLeft(DosGame& g);
// 1568:0638 Cell_P8_WallRight (type 8): the mirror image; visible call -> Cell_V8.
uint16_t cellP8WallRight(DosGame& g);
// 1568:06a5 Cell_P50_ShelfEnter (type 50, 04): always -> shelf room 0 (0x460,0x1F0); flags.
uint16_t cellP50ShelfEnter(DosGame& g);
// 1568:0718 Cell_P51_ShelfExit (type 51, 38): walking left -> clubhouse (0x200,0x70).
uint16_t cellP51ShelfExit(DosGame& g);
// 1568:078a Cell_P52_Shelf0to1 (type 52, 3B): always -> shelf room 1 (0x460,0x270).
uint16_t cellP52Shelf0to1(DosGame& g);
// 1568:07fc Cell_P53_Shelf1to0 (type 53, 48): walking left -> (0x580,0x1F0).
uint16_t cellP53Shelf1to0(DosGame& g);
// 1568:0880 Cell_P54_Shelf1to2 (type 54, 4B): always -> shelf room 2 (0x460,0x2F0).
uint16_t cellP54Shelf1to2(DosGame& g);
// 1568:08f2 Cell_P55_Shelf2to1 (type 55, 58): walking left -> (0x580,0x270).
uint16_t cellP55Shelf2to1(DosGame& g);
// 1568:0976 Cell_P65_Shelf2to3 (type 65, 5B): always -> shelf room 3 (0x460,0x370).
uint16_t cellP65Shelf2to3(DosGame& g);
// 1568:09e8 Cell_P66_Shelf3to2 (type 66, 68): walking left -> (0x580,0x2F0).
uint16_t cellP66Shelf3to2(DosGame& g);
// 1568:0a6c Cell_P30_V30_TrunkTop (type 30, BF): top of the trunk ladder (snap 0x7CC, mask 0x5F3,
// area 2); visible: Cell_DrawTreetop(0x600).
uint16_t cellP30V30TrunkTop(DosGame& g);
// 1568:0b38 Cell_P17_MineDoorW (type 17, B2): up -> (0x440,0x170) cell 28, area 0x0A, tune 2.
uint16_t cellP17MineDoorW(DosGame& g);
// 1568:0b86 Cell_P37_MineExitW (type 37, 28): walking left, x&0x78 <= 0x48 -> (0x148,0x5F0).
uint16_t cellP37MineExitW(DosGame& g);
// 1568:0bfb Cell_P18_MineDoorE (type 18, BB): up -> (0x540,0x170) cell 2A, area 0x0B, tune 2.
uint16_t cellP18MineDoorE(DosGame& g);
// 1568:0c49 Cell_P38_MineExitE (type 38, 2A): walking right, x&0x78 >= 0x38 -> (0x5B8,0x5F0).
uint16_t cellP38MineExitE(DosGame& g);
// 1568:0cbe Cell_P44_CastleDoor (type 44, 73): hall door; on the floor g_slowCpu = 0, mask 0x4FD;
// Up -> (0x4C0,0x3F0) cell 79, restores the slow-CPU flags, tune 0.
uint16_t cellP44CastleDoor(DosGame& g);
// 1568:0d94 Cell_P23_FallShaft (type 23): mask 0x1400; level 2 at y == 0x270 (cell 4D): -> mine
// tunnel (0x4C0,0x170), level 1, transition 1, area 0x0E.
uint16_t cellP23FallShaft(DosGame& g);
// 1568:0df3 Cell_P47_Boundary01 (type 47, C7): mask 0x1400; boundary 0/1 when y&0x7F > 0x68.
uint16_t cellP47Boundary01(DosGame& g);
// 1568:0e13 Cell_P19_Boundary12 (type 19, 94): mask 0x1400; boundary 1/2 when y&0x7F > 0x68.
uint16_t cellP19Boundary12(DosGame& g);
// 1568:0e33 Cell_P24_Wall (type 24, unused): dx = 0, returns 0xFF.
uint16_t cellP24Wall(DosGame& g);
// 1568:0e41 Cell_P46_V46_Boundary01 (type 46, CF): returns 0x5F3; object: boundary 0/1 above 0x68;
// visible: Cell_DrawTreetop(0x600).
uint16_t cellP46V46Boundary01(DosGame& g);
// 1568:0f13 Cell_P25_TrunkShaft (type 25, DF): returns 0x5F3.
uint16_t cellP25TrunkShaft(DosGame& g);
// 1568:0f17 Cell_P31_GateL0 (type 31, EF): level-0 gate; key state 2: snap, ladder above 0x68,
// picture 0x40 -> 0x4C (sfx 8); no key: Up arms hint 8, no Up/Down.
uint16_t cellP31GateL0(DosGame& g);
// 1568:1010 Cell_P26_Unused (type 26, unused): camera clamp at 0x500 on DS:37EE/37F0; returns 0xFF.
uint16_t cellP26Unused(DosGame& g);
// 1568:1037 Cell_P27_Unused (type 27, unused): as P26 with 0x380.
uint16_t cellP27Unused(DosGame& g);
// 1568:105e Cell_P28_Path (types 28 32 33 49 58 59 67 68 69): dy > 0 -> 0; returns 0xFC.
uint16_t cellP28Path(DosGame& g);
// 1568:1077 Cell_P29_Unused (type 29, unused): upward clamp at 0x380; returns 0xFF.
uint16_t cellP29Unused(DosGame& g);
// 1568:1090 Cell_P36_V36_NetShop (type 36, BC EA): coins pay g_shopCoinsOwed (mask 0x200), the rock
// pictures 0x47/0x48/0x46, +5 nets inside; visible: the sign (digits / net) in slot 12.
uint16_t cellP36V36NetShop(DosGame& g);
// 1568:1286 Cell_P40_V40_Geyser (type 40, 8E 9E): mask 0xCF1; boundary 1/2 above 0x68 (no kind test);
// Cell_DrawGeyser(0x500, 0x50).
uint16_t cellP40V40Geyser(DosGame& g);
// 1568:1329 Cell_P39_V39_GateL1 (type 39, AE): level-1 gate; key state 4: snap 0x740, open 0x38 ->
// 0x35 (sfx 8), lift 0xCF1; no key: hint 8; always Cell_DrawGeyser(0x500, 0x50).
uint16_t cellP39V39GateL1(DosGame& g);
// 1568:141c Cell_P41_V41_GeyserTop (type 41, 7E): the geyser hole; a walking player in the middle
// slides down (anim 0x1D, state 0x1E, 0xCF4); elves turn (also the stale elf of visible calls).
uint16_t cellP41V41GeyserTop(DosGame& g);
// 1568:15bc Cell_P45_V45_Bridge (type 45, 84 B7): tier >= 2, a walking player in the middle: fall,
// picture 0x2F -> 0x30, sfx 9; elves turn at a broken bridge; visible: the rail in slot 12; nothing
// on a slow CPU.
uint16_t cellP45V45Bridge(DosGame& g);
// 1568:16ed Cell_P48_Landing (type 48, B4 E7): player on the path line: dy = 0, 0xFC; else 0x14F0.
uint16_t cellP48Landing(DosGame& g);
// 1568:1734 Cell_P56_V56_CastleHall (type 56, 71 72 74 75): hall floor and ladders (0x20FF / 0x24FF
// / 0x25FF), creature hit check; visible: removes a showing creature.
uint16_t cellP56V56CastleHall(DosGame& g);
// 1568:182b Cell_P57_V57_CastleWall (type 57): mask by picture (0x31, 0x2D, 0x25, else fall),
// 0x1400 outside x&0x78 0x30..0x60, creature hit check; visible: Castle_CreatureSpawn.
uint16_t cellP57V57CastleWall(DosGame& g);
// 1568:1953 Cell_P60_IceDoor (type 60, 7C): up -> (0x6C0,0x170) cell 2D, area 0x0C, Music_Stop,
// Sfx_Stop, tune 2.
uint16_t cellP60IceDoor(DosGame& g);
// 1568:19ab Cell_P61_IceCaveExit (type 61, 2D): walking left, x&0x78 <= 0x48 -> (0x648,0x3F0).
uint16_t cellP61IceCaveExit(DosGame& g);
// 1568:1a20 Cell_P62_IceSlide (type 62, 2E): walking right, x&0x78 >= 0x68: fall into the rock shaft
// (0x6A8,0x1F0), camX 0x600, anim 0x3F, state 0x1F, area 0x0E, key state 4, sfx 9.
uint16_t cellP62IceSlide(DosGame& g);
// 1568:1aa4 Cell_P63_V63_CastleTop (type 63): masks as P57; climbing up out of the exit picture
// 0x32 ends the climb (scene 3 at (0x400,0x500), EndSeq_Setup, end anims, total += found,
// Treasure_PickShowcase); visible: Castle_CreatureSpawn.
uint16_t cellP63V63CastleTop(DosGame& g);
// 1568:1ed8 Cell_BonusCoins (type 73, 26): any object at x&0x7F == 0x48 while picture[26] == 0x4B:
// picture 0x49, sfx 4, redraw, coins +5; returns 0xFC.
uint16_t cellBonusCoins(DosGame& g);

// ---- visible-only handlers -----------------------------------------------------------------
// 1568:1c7b Cell_V49_Showcase (type 49, 03 9A): slot 3 showcase treasure, slots 8/10 treasure-room
// partners (DS:00F8..00FE, g_shackFlags); g_shelfCamRoom = 0.
void cellV49Showcase(DosGame& g);
// 1568:1d92 Cell_NopV58 / 1568:1d93 Cell_NopV67 / 1568:1d94 Cell_NopV68 / 1568:1d95 Cell_NopV69:
// empty visible handlers (RETF); no engine function.
// 1568:1d96 Cell_V32_AnimDecor (type 32, E6): fast CPU: slot 12 anim 0x53 at cellScreenX+0x20,
// cellScreenY+0x80; g_decorCount++.
void cellV32AnimDecor(DosGame& g);
// 1568:1ddb Cell_V10 (types 10, 33): fast CPU: frame 0x176 at cellScreenX, cellScreenY+0x80.
void cellV10(DosGame& g);
// 1568:1dfa Cell_V59 (type 59): fast CPU: frame 0x17E at cellScreenX+0x18, cellScreenY+0x80.
void cellV59(DosGame& g);
// 1568:1e1c Cell_V7: frame 0x17A at cellScreenX-1, cellScreenY+0x78.
void cellV7(DosGame& g);
// 1568:1e35 Cell_V8: frame 0x17B at cellScreenX+0x41, cellScreenY+0x78.
void cellV8(DosGame& g);

}  // namespace tmport::dos
