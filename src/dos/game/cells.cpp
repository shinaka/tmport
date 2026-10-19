// cells.c (segment 1568): the cell dispatchers and the terrain handlers of the 16x16 cell world
// (re/dos/docs/code_world.md §4-§6, §12). Every handler is ported from the disassembly; the
// comparisons keep the original's signedness (dx, dy and the screen offsets signed, `x & 0x78` and
// `y & 0x7F` unsigned) and the mask operations keep their 16-bit immediates (`& 0x00FC` also clears
// the high byte).
#include "dos/game/cells.h"

#include "dos/game/anim.h"
#include "dos/game/cells2.h"
#include "dos/game/elves.h"
#include "dos/game/endgame.h"
#include "dos/game/game.h"
#include "dos/game/player.h"
#include "dos/game/search.h"
#include "dos/game/sound.h"
#include "dos/save/users_dat.h"

namespace tmport::dos {

namespace {

constexpr int kDecorSlot = 12;  // the cell decoration slot DS:90DE

// *g_curObj. The original dereferences whatever DS:8FAB holds; before the first Cell_GetMask of
// a session that is a null far pointer (the interrupt vector table). The engine reads a zeroed
// scratch object then, so a visible call that uses the stale object (§4.3) cannot fault.
Obj& cur(DosGame& g) {
    const uint16_t off = g.world.curObj.off;
    const bool elf = off >= dg::kElves && off < elfObjOff(kElfCount) && (off - dg::kElves) % 0x20 == 0;
    if (off == kPlayerObjOff || elf) return objAt(g, g.world.curObj);
    static Obj scratch;
    scratch = Obj{};
    return scratch;
}

// ISPLAYER: the far pointer compare with DS:37E2.
bool isPlayer(const DosGame& g) { return g.world.curObj.off == kPlayerObjOff; }
bool objectCall(const DosGame& g) { return g.world.cellCallKind == 2; }

uint16_t yIn(const Obj& o) { return o.y & 0x7F; }   // row inside the cell (0x70 = path line)
uint16_t yCol(const Obj& o) { return o.y & 0x78; }  // 8-row band inside the cell
uint16_t xCol(const Obj& o) { return o.x & 0x78; }  // 8-pixel column inside the cell

// picture[g_curCell] as the handlers read it (an index past the grid reads g_cellType, §3.2); a
// write past the grid would land in g_cellType and is dropped (only reachable from a corrupt state).
uint16_t curPicture(const DosGame& g) { return cellPictureRaw(g, g.tables, static_cast<int>(g.world.curCell)); }
void setCurPicture(DosGame& g, uint16_t v) {
    if (g.world.curCell < 256) g.world.cellPicture[g.world.curCell] = v;
}

// SNAP(X): centre on a ladder or door; a full redraw only when the position changed.
void snapX(DosGame& g, Obj& o, uint16_t x) {
    if (o.x != x) ++g.world.fullRedrawReq;
    o.x = x;
}

// The camera x a teleport sets: always from the player's (moved) position DS:37E2.
void setCamera(DosGame& g, int offset) { g.world.cameraX = static_cast<uint16_t>(g.player.obj.x + offset); }

// COLUMN(lo, hi): Up/Down only inside the ladder or door column.
void column(DosGame& g, const Obj& o, uint16_t lo, uint16_t hi) {
    if (xCol(o) < lo || xCol(o) > hi) g.world.cellMask &= 0xFFFC;
}

// PATH on the path line: no downward movement, no Down.
void onPath(DosGame& g, Obj& o) {
    if (o.dy > 0) o.dy = 0;
    g.world.cellMask &= 0xFFFD;
}

// The hint-8 arm of the locked gates (P31, P39): the raw Up key with hint 8 not yet shown.
void armKeyHint(DosGame& g) {
    if ((g.elves.hintFlags[8] & 0x80) == 0 && (g.player.keysUnmasked & 1) != 0) g.elves.hintFlags[8] = 0;
}

// The "walk out of a cave" exits P6, P10, P37, P38, P61 (no ISPLAYER test): Music_Stop, the target,
// camera x - 0xA0, transition 2, redraw, Anim_Set(1, 6), state 9, area 0, tune 0.
void exitToPath(DosGame& g, Obj& o, uint16_t x, uint16_t y) {
    musicStop(g);
    o.x = x;
    o.y = y;
    setCamera(g, -0xA0);
    g.world.transition = 2;
    g.world.fullRedrawReq = 1;
    animSet(g, 1, 6);
    g.player.playerState = 9;
    g.core.area = 0;
    musicPlay(g, 0);
}

// The doorways P9, P11, P17, P18, P34, P60 after Cell_GroundCheckPlayer fired: target and camera.
void enterDoor(DosGame& g, Obj& o, uint16_t x, uint16_t y, int cam) {
    o.x = x;
    o.y = y;
    setCamera(g, cam);
}

// The shelf-room moves (§6.3): target, transition 2, camera, redraw, Anim_Set(1, anim), state 10.
void shelfMove(DosGame& g, Obj& o, uint16_t x, uint16_t y, int cam, int anim) {
    o.x = x;
    o.y = y;
    g.world.transition = 2;
    setCamera(g, cam);
    g.world.fullRedrawReq = 1;
    animSet(g, 1, anim);
    g.player.playerState = 0x0A;
}

// Picture masks shared by the castle wall and the castle top (P57, P63 step 1): returns true when
// the picture was one of 0x31, 0x2D, 0x25 (the mask is then set).
bool castleLadderMask(DosGame& g, const Obj& o) {
    uint16_t& mask = g.world.cellMask;
    const uint16_t pic = curPicture(g);  // a whole-word compare in the original
    if (pic == 0x31) {
        mask = 0x25FF;  // full ladder: climb, ladder jumps
    } else if (pic == 0x2D) {
        mask = 0x25FF;  // short ladder in the lower part
        if (yCol(o) < 0x60) mask = 0x11F0;        // above it: fall onto the ladder
        else if (yCol(o) < 0x78) mask = 0x25FE;   // its top: no Up, jump sideways
    } else if (pic == 0x25) {
        mask = 0x25F3;  // used by no layout
        if (yCol(o) > 0x28) mask = 0x25FD;
    } else {
        return false;
    }
    return true;
}

}  // namespace

// ---- dispatch ----------------------------------------------------------------------------

// 1568:1fe4 Cell_GetMask
uint16_t cellGetMask(DosGame& g) {
    g.world.cellCallKind = 2;
    const Obj& o = cur(g);
    g.world.curCell = static_cast<uint16_t>(cellIndexOf(o.x, o.y));  // unmasked (§3.2)
    return callCellObjectHandler(g, cellTypeRaw(g, g.world.curCell));
}

// 1568:1f0b Cells_UpdateVisible
void cellsUpdateVisible(DosGame& g) {
    if (g.core.uiMode != 0) return;
    WorldState& w = g.world;
    w.castleCellIdx = 0;
    const unsigned c = cellIndexOf(w.cameraX, w.cameraY);
    w.cellScreenX = static_cast<int16_t>(2 - ((w.cameraX & 0x78) >> 1));
    w.cellScreenY = static_cast<int16_t>(0x10 - (w.cameraY & 0x7F));
    w.decorCount = 0;
    w.cellCallKind = 1;
    // g_curObj is not set here: the handlers see the object of the last Cell_GetMask (§4.3). The
    // index is not wrapped: past cell 0xFF the types are read from g_cellContent (cellTypeRaw).
    for (unsigned k = 0; k < 4; ++k) {
        callCellVisibleHandler(g, cellTypeRaw(g, c + k));
        if (k < 3) w.cellScreenX = static_cast<int16_t>(w.cellScreenX + 0x40);
    }
    if (w.decorCount == 0) {  // no decoration placed this tick
        SpriteSlot& s = g.player.slots[kDecorSlot];
        s.frame = 0xFFFF;
        s.anim = 0;
        s.prev = 0;
    }
}

// g_cellPlayerHandlers DS:08A6 (75 entries; entry 74 and anything outside the table: Null_Handler)
uint16_t callCellObjectHandler(DosGame& g, int type) {
    switch (type) {
        case 0: return cellP0Void(g);
        case 1: return cellP1(g);
        case 2: return cellP2(g);
        case 3: return cellP3(g);
        case 4: return cellP4(g);
        case 5: return cellP5CaveDoorW(g);
        case 6: return cellP6CaveExitW(g);
        case 7: return cellP7WallLeft(g);
        case 8: return cellP8WallRight(g);
        case 9: return cellP9CaveDoorE(g);
        case 10: return cellP10CaveExitE(g);
        case 11: return cellP11ClubDoor(g);
        case 12: return cellP12StartRoom(g);
        case 13: return cellP13(g);
        case 14: return cellP14(g);
        case 15: return cellP15(g);
        case 16: return cellP16(g);
        case 17: return cellP17MineDoorW(g);
        case 18: return cellP18MineDoorE(g);
        case 19: return cellP19Boundary12(g);
        case 20: return cellP20CastleGate(g);
        case 21: return cellP21(g);
        case 22: return cellP22(g);
        case 23: return cellP23FallShaft(g);
        case 24: return cellP24Wall(g);
        case 25: return cellP25TrunkShaft(g);
        case 26: return cellP26Unused(g);
        case 27: return cellP27Unused(g);
        case 28: case 32: case 33: case 49: case 58: case 59: case 67: case 68: case 69: return cellP28Path(g);
        case 29: return cellP29Unused(g);
        case 30: return cellP30V30TrunkTop(g);
        case 31: return cellP31GateL0(g);
        case 34: return cellP34CaveUp(g);
        case 35: return cellP35UpperRoomDown(g);
        case 36: return cellP36V36NetShop(g);
        case 37: return cellP37MineExitW(g);
        case 38: return cellP38MineExitE(g);
        case 39: return cellP39V39GateL1(g);
        case 40: return cellP40V40Geyser(g);
        case 41: return cellP41V41GeyserTop(g);
        case 42: return cellP42GateL2(g);
        case 43: return cellP43(g);
        case 44: return cellP44CastleDoor(g);
        case 45: return cellP45V45Bridge(g);
        case 46: return cellP46V46Boundary01(g);
        case 47: return cellP47Boundary01(g);
        case 48: return cellP48Landing(g);
        case 50: return cellP50ShelfEnter(g);
        case 51: return cellP51ShelfExit(g);
        case 52: return cellP52Shelf0to1(g);
        case 53: return cellP53Shelf1to0(g);
        case 54: return cellP54Shelf1to2(g);
        case 55: return cellP55Shelf2to1(g);
        case 56: return cellP56V56CastleHall(g);
        case 57: return cellP57V57CastleWall(g);
        case 60: return cellP60IceDoor(g);
        case 61: return cellP61IceCaveExit(g);
        case 62: return cellP62IceSlide(g);
        case 63: return cellP63V63CastleTop(g);
        case 64: return cellP64EndScreen(g);
        case 65: return cellP65Shelf2to3(g);
        case 66: return cellP66Shelf3to2(g);
        case 70: return cellP70TreasureRoom(g);
        case 71: return cellP71V71ToRankScreen(g);
        case 72: return cellP72RankScreen(g);
        case 73: return cellBonusCoins(g);
        default: return 0;  // 1568:0007 Null_Handler (no cell has type 74)
    }
}

// g_cellVisibleHandlers DS:09D2 (74 entries; the others are Null_Handler, 1568:1d92..1d95 the
// empty Cell_NopV58/67/68/69)
void callCellVisibleHandler(DosGame& g, int type) {
    switch (type) {
        case 7: cellV7(g); break;
        case 8: cellV8(g); break;
        case 10: case 33: cellV10(g); break;
        case 30: cellP30V30TrunkTop(g); break;
        case 32: cellV32AnimDecor(g); break;
        case 36: cellP36V36NetShop(g); break;
        case 39: cellP39V39GateL1(g); break;
        case 40: cellP40V40Geyser(g); break;
        case 41: cellP41V41GeyserTop(g); break;
        case 45: cellP45V45Bridge(g); break;
        case 46: cellP46V46Boundary01(g); break;
        case 49: cellV49Showcase(g); break;
        case 56: cellP56V56CastleHall(g); break;
        case 57: cellP57V57CastleWall(g); break;
        case 59: cellV59(g); break;
        case 63: cellP63V63CastleTop(g); break;
        case 71: cellP71V71ToRankScreen(g); break;
        default: break;  // Null_Handler, Cell_NopV58/67/68/69
    }
}

// ---- shared helpers (§6.1) ----------------------------------------------------------------

// 1568:0134 Cell_GroundCheckObj
int cellGroundCheckObj(DosGame& g) {
    Obj& o = cur(g);
    if (yIn(o) == 0x70) {
        onPath(g, o);
    } else if (yIn(o) < 0x6C) {
        musicStop(g);
        g.world.transition = 1;
        g.world.fullRedrawReq = 1;
        g.player.playerState = 9;
        return 1;
    }
    column(g, o, 0x26, 0x5A);
    return 0;
}

// 1568:0203 Cell_GroundCheckPlayer
int cellGroundCheckPlayer(DosGame& g) {
    if (!isPlayer(g)) return 0;
    Obj& o = cur(g);
    if (yIn(o) == 0x70) {
        onPath(g, o);
    } else if (yIn(o) < 0x68) {  // risen more than 8 rows by the look-up animation
        musicStop(g);
        g.world.transition = 2;
        g.world.fullRedrawReq = 1;
        g.player.playerState = 9;
        return 1;
    }
    column(g, o, 0x26, 0x5A);
    return 0;
}

// 1568:0e6d Cell_LevelBoundary01
void cellLevelBoundary01(DosGame& g) {
    const Obj& o = cur(g);
    if (g.world.loadedScene == 1 && o.dy > 0) {
        g.core.level = 0;
        g.core.keyState = 2;  // going down: the level-0 key was found
    } else if (g.world.loadedScene == 0 && o.dy < 0) {
        g.core.level = 1;
        // Slots 13..29 for the unfound level-1 key (a searched key has bit 7 set and does not match).
        int i = 13;
        while (g.world.cellContent[g.tables.world.hidingCells[i] & 0xFF] != 0x68 && i < 29) ++i;
        g.core.keyState = g.world.cellContent[g.tables.world.hidingCells[i] & 0xFF] == 0x68 ? 0x0A : 4;
    }
}

// 1568:12b4 Cell_LevelBoundary12
void cellLevelBoundary12(DosGame& g) {
    const Obj& o = cur(g);
    if (g.world.loadedScene == 2 && o.dy > 0) {
        g.core.level = 1;
        g.core.keyState = 4;
    } else if (g.world.loadedScene == 1 && o.dy < 0) {
        g.core.level = 2;
        int i = 0;  // slots 0..13 for the unfound level-2 key
        while (g.world.cellContent[g.tables.world.hidingCells[i] & 0xFF] != 0x6A && i < 13) ++i;
        g.core.keyState = g.world.cellContent[g.tables.world.hidingCells[i] & 0xFF] == 0x6A ? 0x0C : 6;
    }
}

// 1568:0ee3 Cell_DrawTreetop
void cellDrawTreetop(DosGame& g, int wy) {
    if (g.core.slowCpu != 0 || g.world.decorCount != 0) return;
    cellDrawObject(g, g.world.cellScreenX + 0x20, wy - g.world.cameraY + 0x80, 0x17C);
}

// 1568:1556 Cell_DrawGeyser
void cellDrawGeyser(DosGame& g, int wy, int depth) {
    if (g.world.cellCallKind != 1 || g.world.cellPicture[0xAE] != 0x35) return;  // gate AE open
    animSetIfIdle(g, kDecorSlot, 0x54);
    int s = static_cast<int16_t>(wy - g.world.cameraY);
    if (s >= 0x3B) s = 0x3B;
    SpriteSlot& slot = g.player.slots[kDecorSlot];
    slot.x = static_cast<int16_t>(g.world.cellScreenX + 0x20);
    slot.y = static_cast<int16_t>(s + 0x54);
    slot.z = static_cast<int16_t>(depth);
    ++g.world.decorCount;
}

// 1568:1e50 Cell_DrawObject. Neither the slot's animation nor g_decorCount is tested: of several
// static decorations in view the last (rightmost) one wins.
void cellDrawObject(DosGame& g, int x, int y, int frame) {
    SpriteSlot& s = g.player.slots[kDecorSlot];
    s.x = static_cast<int16_t>(x);
    s.y = static_cast<int16_t>(y);
    s.z = 0x1E;
    s.frame = static_cast<uint16_t>(frame);
    s.owner = kDecorSlot;
    s.prev = 0;
    ++g.world.decorCount;
}

// 1568:1e9e Castle_PlaceCreature
void castlePlaceCreature(DosGame& g) {
    SpriteSlot& s6 = g.player.slots[6];
    SpriteSlot& s7 = g.player.slots[7];
    s6.x = g.world.cellScreenX;
    s6.y = static_cast<int16_t>(g.world.cellScreenY + 0x70);
    s6.z = 0x50;
    s7.x = 0;
    s7.y = 0;
    s7.z = 0x4F;
    ++g.world.decorCount;
}

// 1568:18f1 Castle_CreatureHitCheck
void castleCreatureHitCheck(DosGame& g) {
    if (g.world.creatureStrike == 0) return;
    g.world.creatureStrike = 0;
    if (g.player.playerState == 0x21) return;  // ladder jump
    const auto py = static_cast<uint16_t>(g.player.playerScrY);
    const auto cy = static_cast<uint16_t>(g.player.slots[6].y);
    // Unsigned compares, as the original (a creature above the screen top wraps).
    if (static_cast<uint16_t>(py - 0x1D) > cy || static_cast<uint16_t>(py + 0x10) < cy) return;
    if (g.core.coins != 0) {
        --g.core.coins;
        sfxPlay(g, 5);
    }
    // The forged Left/Right bit makes the climber ladder-jump away from the creature next tick.
    const auto px = static_cast<uint16_t>(g.player.playerScrX + 2);
    const auto cx = static_cast<uint16_t>(g.player.slots[6].x);
    if (px > cx) g.keys() |= 8;
    if (px < cx) g.keys() |= 4;
}

// ---- object handlers by cell type ---------------------------------------------------------

// 1568:1eca Cell_P0_Void: border colour counter + INT 10h AX=1001h (EGA overscan, not shown by
// the engine); AX is not reloaded, the BIOS leaves 0x1001 (fall + Up).
uint16_t cellP0Void(DosGame& g) {
    ++g.world.borderColor;
    return 0x1001;
}

// 1568:0008 Cell_P5_CaveDoorW
uint16_t cellP5CaveDoorW(DosGame& g) {
    if (!isPlayer(g)) return g.world.cellMask;  // the original returns AX unset (unused for elves)
    Obj& o = cur(g);
    g.world.cellMask = 0xFF;
    if (yIn(o) == 0x70) {
        onPath(g, o);
    } else if (yIn(o) < 0x68) {
        musicStop(g);
        o.x = 0x460;
        o.y = 0x70;
        setCamera(g, -0x20);
        g.core.area = 7;
        musicPlay(g, 2);
        g.world.transition = 2;
        g.world.fullRedrawReq = 1;
        animSet(g, 1, 3);
        g.player.playerState = 9;
    }
    if (xCol(o) > 0x40) g.world.cellMask &= 0xFFFC;  // the door is the left half of the cell
    return g.world.cellMask;
}

// 1568:00ce Cell_P6_CaveExitW
uint16_t cellP6CaveExitW(DosGame& g) {
    Obj& o = cur(g);
    g.world.cellMask = 0x7C;
    if (o.dx < 0) exitToPath(g, o, 0x5B0, 0x770);
    return g.world.cellMask;
}

// 1568:01b4 Cell_P9_CaveDoorE. Quirk kept (PORTING_NOTES §6): camera = player x - 0x124 = 0x48C,
// not a multiple of 8: the scenery is drawn for 0x488 while sprites use the exact camera, so the
// player stands 4 px off the tiles inside this cave until another teleport.
uint16_t cellP9CaveDoorE(DosGame& g) {
    g.world.cellMask = 0xFF;
    if (cellGroundCheckPlayer(g) == 1) {
        Obj& o = cur(g);
        enterDoor(g, o, 0x5B0, 0x70, -0x124);
        animSet(g, 1, 2);
        g.core.area = 0x0B;
        musicPlay(g, 2);
    }
    return g.world.cellMask;
}

// 1568:029d Cell_P10_CaveExitE
uint16_t cellP10CaveExitE(DosGame& g) {
    Obj& o = cur(g);
    g.world.cellMask = 0x7C;
    if (o.dx > 0) exitToPath(g, o, 0x3B0, 0x7F0);
    return g.world.cellMask;
}

// 1568:0303 Cell_P34_CaveUp: the music Cell_GroundCheckPlayer stopped is not restarted.
uint16_t cellP34CaveUp(DosGame& g) {
    g.world.cellMask = 0xFF;
    if (cellGroundCheckPlayer(g) == 1) {
        Obj& o = cur(g);
        enterDoor(g, o, 0xC0, 0x170, -0x20);
        animSet(g, 1, 3);
    }
    return g.world.cellMask;
}

// 1568:0341 Cell_P35_UpperRoomDown (transition 1: the camera keeps the player's screen x)
uint16_t cellP35UpperRoomDown(DosGame& g) {
    g.world.cellMask = 0x0F;
    if (cellGroundCheckObj(g) == 1) {
        Obj& o = cur(g);
        o.x = 0x4C0;
        o.y = 0x70;
        animSet(g, 1, 6);
        g.core.area = 9;
        musicPlay(g, 2);
    }
    return g.world.cellMask;
}

// 1568:0386 Cell_P11_ClubDoor
uint16_t cellP11ClubDoor(DosGame& g) {
    g.world.cellMask = 0xFF;
    if (cellGroundCheckPlayer(g) == 1) {
        Obj& o = cur(g);
        enterDoor(g, o, 0x140, 0x70, -0x80);
        animSet(g, 1, 6);
        g.core.area = 5;
        musicPlay(g, 1);
    }
    // Every call: the showcase put away at the door comes back (DS:9254 keeps its value).
    if (isPlayer(g) && g.player.storedShowcase > 0) g.player.shownTreasure = g.player.storedShowcase;
    return g.world.cellMask;
}

// 1568:03f6 Cell_P12_StartRoom (no kind or ISPLAYER test)
uint16_t cellP12StartRoom(DosGame& g) {
    Obj& o = cur(g);
    uint16_t& mask = g.world.cellMask;
    mask = 0x10FD;
    if (yIn(o) == 0x70) {
        if (o.dy > 0) o.dy = 0;
        mask = 0xFD;
    } else if ((g.keys() & 1) != 0) {  // above the floor with Up held (else he falls back)
        mask = 0xF1;
        if (yIn(o) < 0x6C && xCol(o) < 0x5A) {  // the door
            musicStop(g);
            g.core.area = 0;
            musicPlay(g, 0);
            g.world.transition = 2;
            g.world.fullRedrawReq = 1;
            o.x = 0x1C0;
            o.y = 0x7F0;
            setCamera(g, -0xA0);
            animSet(g, 1, 6);
            g.player.playerState = 9;
            if (g.player.shownTreasure > 0) {  // the showcase treasure is put away while outside
                g.player.storedShowcase = static_cast<int8_t>(g.player.shownTreasure & 0xFF);
                g.player.slots[3].frame = 0xFFFF;
                g.player.shownTreasure = -1;
            }
        }
    }
    if (yIn(o) != 0x70 && xCol(o) >= 0x5A) {  // the prize picture (right part)
        mask |= 3;
        if (yIn(o) < 0x6C) {
            mask &= 0x0002;  // only Down
            if (yIn(o) == 0x60) {
                // Into the treasure room without a transition: the camera keeps the screen offset.
                o.x = 0x4F0;
                o.y = 0x4E0;
                g.core.level = 4;
                g.core.menuLock = 1;
                g.player.playerState = 1;
                g.world.fullRedrawReq = 1;
                animSet(g, 1, 0x51);
                ++g.player.shackStay;
            }
        }
    }
    if (g.player.slots[1].prev == 0xA5) {  // the crown hand-over
        g.core.level = 4;
        g.core.menuLock = 1;
        o.x = 0x4F0;
        o.y = 0x4E0;
        g.world.fullRedrawReq = 1;
        g.world.transition = 1;
        animClear(g, 6);
        animClear(g, 8);
        cellP71V71ToRankScreen(g);  // moves on to the rank room (0x680, 0x80)
        mask = 0;
    }
    if (xCol(o) < 0x26 || xCol(o) > 0x78) mask &= 0x00FC;  // imm16: also drops 0x1000
    return mask;
}

// 1568:05cb Cell_P7_WallLeft
uint16_t cellP7WallLeft(DosGame& g) {
    if (!objectCall(g)) {
        cellV7(g);
        return g.world.cellMask;
    }
    Obj& o = cur(g);
    uint16_t& mask = g.world.cellMask;
    mask = 0x10DC;
    if (xCol(o) <= 0x43) {
        mask = 0x10D8;  // no Left
        if (o.dx < 0) o.dx = 0;
    }
    if (yCol(o) >= 0x70) {  // the lowest 16 rows: the floor step (higher up 0x1000 makes him fall)
        if (o.dy > 0) o.dy = 0;
        mask &= 0x00FC;
    }
    return mask;
}

// 1568:0638 Cell_P8_WallRight
uint16_t cellP8WallRight(DosGame& g) {
    if (!objectCall(g)) {
        cellV8(g);
        return g.world.cellMask;
    }
    Obj& o = cur(g);
    uint16_t& mask = g.world.cellMask;
    mask = 0x10DC;
    if (xCol(o) >= 0x38) {
        mask = 0x10D4;  // no Right
        if (o.dx > 0) o.dx = 0;
    }
    if (yCol(o) >= 0x70) {
        if (o.dy > 0) o.dy = 0;
        mask &= 0x00FC;
    }
    return mask;
}

// The eight shelf-room handlers act on object calls only and set mask 0xDC. "Always" (P50, P52,
// P54, P65): their guard `TEST x,0x78 / JC` never jumps; "walking left" (P51, P53, P55, P66): dx < 0,
// with a second guard `x & 0x78 <= 0x78` that always holds.

// 1568:06a5 Cell_P50_ShelfEnter
uint16_t cellP50ShelfEnter(DosGame& g) {
    if (!objectCall(g)) return g.world.cellMask;
    Obj& o = cur(g);
    g.world.cellMask = 0xDC;
    shelfMove(g, o, 0x460, 0x1F0, -0x20, 3);
    g.world.shelfRoomRedraw[0] = 1;
    g.world.inShelfRoom[0] = 1;
    g.world.playfieldRedrawn = 1;
    g.world.shelfCamRoom = 0;
    g.player.slots[3].frame = 0xFFFF;
    return g.world.cellMask;
}

// 1568:0718 Cell_P51_ShelfExit
uint16_t cellP51ShelfExit(DosGame& g) {
    if (!objectCall(g)) return g.world.cellMask;
    Obj& o = cur(g);
    g.world.cellMask = 0xDC;
    if (o.dx < 0 && xCol(o) <= 0x78) {
        shelfMove(g, o, 0x200, 0x70, -0x100, 2);
        g.world.shelfRoomRedraw[0] = 0;
        g.world.inShelfRoom[0] = 0;
    }
    return g.world.cellMask;
}

// 1568:078a Cell_P52_Shelf0to1
uint16_t cellP52Shelf0to1(DosGame& g) {
    if (!objectCall(g)) return g.world.cellMask;
    Obj& o = cur(g);
    g.world.cellMask = 0xDC;
    shelfMove(g, o, 0x460, 0x270, -0x20, 3);
    g.world.shelfRoomRedraw[2] = 1;
    g.world.inShelfRoom[1] = 1;
    g.world.inShelfRoom[0] = 0;
    g.world.playfieldRedrawn = 1;
    g.world.shelfCamRoom = 1;
    return g.world.cellMask;
}

// 1568:07fc Cell_P53_Shelf1to0
uint16_t cellP53Shelf1to0(DosGame& g) {
    if (!objectCall(g)) return g.world.cellMask;
    Obj& o = cur(g);
    g.world.cellMask = 0xDC;
    if (o.dx < 0 && xCol(o) <= 0x78) {
        shelfMove(g, o, 0x580, 0x1F0, -0x100, 2);
        g.world.shelfRoomRedraw[0] = 1;
        g.world.inShelfRoom[0] = 1;
        g.world.inShelfRoom[1] = 0;
        g.world.playfieldRedrawn = 1;
        g.world.shelfCamRoom = 2;
    }
    return g.world.cellMask;
}

// 1568:0880 Cell_P54_Shelf1to2
uint16_t cellP54Shelf1to2(DosGame& g) {
    if (!objectCall(g)) return g.world.cellMask;
    Obj& o = cur(g);
    g.world.cellMask = 0xDC;
    shelfMove(g, o, 0x460, 0x2F0, -0x20, 3);
    g.world.shelfRoomRedraw[4] = 1;
    g.world.inShelfRoom[2] = 1;
    g.world.inShelfRoom[1] = 0;
    g.world.playfieldRedrawn = 1;
    g.world.shelfCamRoom = 2;
    return g.world.cellMask;
}

// 1568:08f2 Cell_P55_Shelf2to1
uint16_t cellP55Shelf2to1(DosGame& g) {
    if (!objectCall(g)) return g.world.cellMask;
    Obj& o = cur(g);
    g.world.cellMask = 0xDC;
    if (o.dx < 0 && xCol(o) <= 0x78) {
        shelfMove(g, o, 0x580, 0x270, -0x100, 2);
        g.world.shelfRoomRedraw[2] = 1;
        g.world.inShelfRoom[1] = 1;
        g.world.inShelfRoom[2] = 0;
        g.world.playfieldRedrawn = 1;
        g.world.shelfCamRoom = 3;
    }
    return g.world.cellMask;
}

// 1568:0976 Cell_P65_Shelf2to3
uint16_t cellP65Shelf2to3(DosGame& g) {
    if (!objectCall(g)) return g.world.cellMask;
    Obj& o = cur(g);
    g.world.cellMask = 0xDC;
    shelfMove(g, o, 0x460, 0x370, -0x20, 3);
    g.world.shelfRoomRedraw[6] = 1;
    g.world.inShelfRoom[3] = 1;
    g.world.inShelfRoom[2] = 0;
    g.world.playfieldRedrawn = 1;
    g.world.shelfCamRoom = 3;
    return g.world.cellMask;
}

// 1568:09e8 Cell_P66_Shelf3to2
uint16_t cellP66Shelf3to2(DosGame& g) {
    if (!objectCall(g)) return g.world.cellMask;
    Obj& o = cur(g);
    g.world.cellMask = 0xDC;
    if (o.dx < 0 && xCol(o) <= 0x78) {
        shelfMove(g, o, 0x580, 0x2F0, -0x100, 2);
        g.world.shelfRoomRedraw[4] = 1;
        g.world.inShelfRoom[2] = 1;
        g.world.inShelfRoom[3] = 0;
        g.world.playfieldRedrawn = 1;
        g.world.shelfCamRoom = 4;
    }
    return g.world.cellMask;
}

// 1568:0a6c Cell_P30_V30_TrunkTop
uint16_t cellP30V30TrunkTop(DosGame& g) {
    uint16_t& mask = g.world.cellMask;
    mask = 0x4FF;
    if (!objectCall(g)) {
        cellDrawTreetop(g, 0x600);
        return mask;
    }
    if (!isPlayer(g)) return mask;
    Obj& o = cur(g);
    if (yIn(o) == 0x70) {
        if (o.dy > 0) {
            snapX(g, o, 0x7CC);
            mask = static_cast<uint16_t>((mask & 0xFFF3) | 0x100);  // 0x5F3: onto the ladder
        } else {
            mask &= 0xFFFE;  // no Up
        }
        if (o.dy < 0) o.dy = 0;
    } else {
        g.core.area = 2;
        mask = static_cast<uint16_t>((mask & 0xFFF3) | 0x100);
    }
    column(g, o, 0x30, 0x50);
    return mask;
}

// 1568:0b38 Cell_P17_MineDoorW
uint16_t cellP17MineDoorW(DosGame& g) {
    g.world.cellMask = 0xFF;
    if (cellGroundCheckPlayer(g) == 1) {
        Obj& o = cur(g);
        enterDoor(g, o, 0x440, 0x170, -0x40);
        animSet(g, 1, 3);
        g.core.area = 0x0A;
        musicPlay(g, 2);
    }
    return g.world.cellMask;
}

// 1568:0b86 Cell_P37_MineExitW
uint16_t cellP37MineExitW(DosGame& g) {
    Obj& o = cur(g);
    g.world.cellMask = 0x7C;
    if (o.dx < 0 && xCol(o) <= 0x48) exitToPath(g, o, 0x148, 0x5F0);
    return g.world.cellMask;
}

// 1568:0bfb Cell_P18_MineDoorE
uint16_t cellP18MineDoorE(DosGame& g) {
    g.world.cellMask = 0xFF;
    if (cellGroundCheckPlayer(g) == 1) {
        Obj& o = cur(g);
        enterDoor(g, o, 0x540, 0x170, -0x100);
        animSet(g, 1, 2);
        g.core.area = 0x0B;
        musicPlay(g, 2);
    }
    return g.world.cellMask;
}

// 1568:0c49 Cell_P38_MineExitE
uint16_t cellP38MineExitE(DosGame& g) {
    Obj& o = cur(g);
    g.world.cellMask = 0x7C;
    if (o.dx > 0 && xCol(o) >= 0x38) exitToPath(g, o, 0x5B8, 0x5F0);
    return g.world.cellMask;
}

// 1568:0cbe Cell_P44_CastleDoor (no ISPLAYER test)
uint16_t cellP44CastleDoor(DosGame& g) {
    Obj& o = cur(g);
    uint16_t& mask = g.world.cellMask;
    mask = 0x14FF;
    if (yIn(o) == 0x70) {
        g.core.slowCpu = 0;  // the castle always runs in fast mode
        mask &= 0x0FFD;
        if (o.dy > 0) o.dy = 0;
    } else if ((g.keys() & 1) != 0 && g.player.playerState < 0x1F && yIn(o) < 0x6C) {
        o.x = 0x4C0;  // back out to the arch (79)
        o.y = 0x3F0;
        g.core.area = 0;
        musicStop(g);
        musicPlay(g, 0);
        g.core.slowCpu = g.world.slowCpuSaved;
        g.world.slowScroll = g.core.slowCpu;
        g.world.transition = 1;
        g.world.fullRedrawReq = 1;
        animSet(g, 1, 6);
        g.player.playerState = 9;
    }
    column(g, o, 0x26, 0x5A);
    return mask;
}

// 1568:0d94 Cell_P23_FallShaft. The ice-cave drop relies on the fall steps hitting y 0x270 exactly
// in cell 4D (a step that skipped it would carry the player down to the castle gate 6D).
uint16_t cellP23FallShaft(DosGame& g) {
    uint16_t& mask = g.world.cellMask;
    mask = 0x1400;
    if (objectCall(g) && g.core.level == 2) {
        Obj& o = cur(g);
        if (o.y == 0x270) {
            o.x = 0x4C0;  // the mine tunnel (29), level 1
            o.y = 0x170;
            g.world.transition = 1;
            g.world.fullRedrawReq = 1;
            g.core.area = 0x0E;
            g.core.level = 1;
            o.dy = 0;
            mask = 0;
        }
    }
    return mask;
}

// 1568:0df3 Cell_P47_Boundary01
uint16_t cellP47Boundary01(DosGame& g) {
    if (objectCall(g) && yIn(cur(g)) > 0x68) cellLevelBoundary01(g);
    return 0x1400;
}

// 1568:0e13 Cell_P19_Boundary12
uint16_t cellP19Boundary12(DosGame& g) {
    if (objectCall(g) && yIn(cur(g)) > 0x68) cellLevelBoundary12(g);
    return 0x1400;
}

// 1568:0e33 Cell_P24_Wall (no cell uses type 24)
uint16_t cellP24Wall(DosGame& g) {
    cur(g).dx = 0;
    return 0xFF;
}

// 1568:0e41 Cell_P46_V46_Boundary01
uint16_t cellP46V46Boundary01(DosGame& g) {
    if (objectCall(g)) {
        if (yIn(cur(g)) > 0x68) cellLevelBoundary01(g);
    } else {
        cellDrawTreetop(g, 0x600);
    }
    return 0x5F3;
}

// 1568:0f13 Cell_P25_TrunkShaft
uint16_t cellP25TrunkShaft(DosGame&) { return 0x5F3; }

// 1568:0f17 Cell_P31_GateL0
uint16_t cellP31GateL0(DosGame& g) {
    uint16_t& mask = g.world.cellMask;
    mask = 0x4FF;
    if (!isPlayer(g)) return mask;  // the original returns AX unset
    Obj& o = cur(g);
    if (g.core.keyState == 2) {
        if (yIn(o) != 0x70) {
            g.core.area = 2;  // no music call
            if ((o.x & 0x7F) != 0x48) {
                o.x = static_cast<uint16_t>((o.x & 0xFF80) + 0x48);
                ++g.world.fullRedrawReq;
            }
            if (yIn(o) < 0x68) {
                mask |= 0x100;
                if (g.world.cellPicture[0xEF] == 0x40) {  // the trunk turns into a ladder
                    g.world.cellPicture[0xEF] = 0x4C;
                    sfxPlay(g, 8);
                    ++g.world.fullRedrawReq;
                }
            }
        } else {
            onPath(g, o);
        }
        column(g, o, 0x18, 0x50);
    } else {
        armKeyHint(g);
        mask &= 0xFFFC;
    }
    return mask;
}

// 1568:1010 Cell_P26_Unused / 1568:1037 Cell_P27_Unused / 1568:1077 Cell_P29_Unused (no cell uses
// them): camera clamps on DS:37EE/37F0 that Player_Move overwrites right after Cell_GetMask.
uint16_t cellP26Unused(DosGame& g) {
    WorldState& w = g.world;
    if (w.camStepY > 0 && w.cameraY == 0x500) w.camStepY = 0;
    if (w.cameraY != 0x500) w.camStepX = 0;
    return 0xFF;
}

uint16_t cellP27Unused(DosGame& g) {
    WorldState& w = g.world;
    if (w.camStepY > 0 && w.cameraY == 0x380) w.camStepY = 0;
    if (w.cameraY != 0x380) w.camStepX = 0;
    return 0xFF;
}

uint16_t cellP29Unused(DosGame& g) {
    WorldState& w = g.world;
    if (w.camStepY < 0 && w.cameraY == 0x380) w.camStepY = 0;
    return 0xFF;
}

// 1568:105e Cell_P28_Path (types 28 32 33 49 58 59 67 68 69)
uint16_t cellP28Path(DosGame& g) {
    Obj& o = cur(g);
    if (o.dy > 0) o.dy = 0;
    return 0xFC;
}

// 1568:1090 Cell_P36_V36_NetShop. The object part acts for the player only (elves get AX unset).
uint16_t cellP36V36NetShop(DosGame& g) {
    uint16_t& mask = g.world.cellMask;
    WorldState& w = g.world;
    if (objectCall(g) && isPlayer(g)) {
        Obj& o = cur(g);
        mask = 0x2FC;
        if (yIn(o) != 0x70) {  // climbing into the cave
            snapX(g, o, g.player.obj.y < 0x600 ? 0x640 : 0x540);  // BC (level 1) / EA (level 0)
            mask |= 3;
            if (yIn(o) < 0x6C) {
                mask &= 0x00FE;  // no Up; the high byte goes too
                if (g.core.shopCoinsOwed == 0 && o.dy <= 0) {
                    --g.core.shopCoinsOwed;  // -1: the nets were handed out
                    g.core.nets = static_cast<uint16_t>(g.core.nets + 5);  // no cap
                } else if (o.dy > 0) {  // climbing back down closes both shops again
                    g.core.shopCoinsOwed = 4;
                    w.cellPicture[0xBC] = 0x47;
                    w.cellPicture[0xEA] = 0x47;
                    ++w.fullRedrawReq;
                }
            }
        } else {  // on the path in front
            mask &= 0xFFFC;
            if (o.dy > 0) o.dy = 0;
            const uint16_t pic = curPicture(g);
            if (pic == 0x46) {  // open: a coin is a normal search here, Up enters
                mask &= 0xFDFF;
                mask |= 1;
            } else if (pic == 0x47 || pic == 0x48) {
                if (g.core.shopCoinsOwed == 0 && w.fullRedrawReq == 0) {
                    w.cellPicture[0xBC] = 0x46;
                    w.cellPicture[0xEA] = 0x46;
                    ++w.fullRedrawReq;
                } else if (g.core.shopCoinsOwed == 1) {
                    // Quirk kept: one coin owed requests a full redraw on every tick while the
                    // player stands here (main then skips the hint/menu updaters).
                    w.cellPicture[0xBC] = 0x48;
                    w.cellPicture[0xEA] = 0x48;
                    ++w.fullRedrawReq;
                }
            }
        }
        if (g.core.shopCoinsOwed == 0) mask &= 0xFFBF;  // no coin drop once paid
    }
    if (w.cellCallKind == 1) {  // the sign on the rock
        SpriteSlot& s = g.player.slots[kDecorSlot];
        s.z = 0x50;
        s.owner = kDecorSlot;
        s.prev = 0;
        s.x = static_cast<int16_t>(w.cellScreenX + 0x1E);
        s.y = static_cast<int16_t>(w.cellScreenY + 0x3A);
        ++w.decorCount;
        if (g.core.shopCoinsOwed == 0 && w.fullRedrawReq == 0) s.frame = 0x194;  // a net
        else if (g.core.shopCoinsOwed == -1) s.frame = 0xFFFF;
        // Digits 1..4; with nothing owed and a redraw pending this gives 0x186 for that tick.
        else s.frame = static_cast<uint16_t>(0x188 + 2 * (g.core.shopCoinsOwed - 1));
    }
    return mask;
}

// 1568:1286 Cell_P40_V40_Geyser (no kind or ISPLAYER test: a visible call acts on the stale object)
uint16_t cellP40V40Geyser(DosGame& g) {
    g.world.cellMask = 0xCF1;
    if (yIn(cur(g)) > 0x68) cellLevelBoundary12(g);
    cellDrawGeyser(g, 0x500, 0x50);
    return g.world.cellMask;
}

// 1568:1329 Cell_P39_V39_GateL1 (no column test: Up works anywhere in the cell)
uint16_t cellP39V39GateL1(DosGame& g) {
    uint16_t& mask = g.world.cellMask;
    mask = 0x4FF;
    if (objectCall(g) && isPlayer(g)) {
        Obj& o = cur(g);
        if (g.core.keyState == 4) {
            if (yIn(o) != 0x70) {
                snapX(g, o, 0x740);
                if (yIn(o) < 0x68) {
                    if (yIn(o) < 0x60) {
                        g.core.area = 3;
                        mask = static_cast<uint16_t>((mask & 0xFFF1) | 0x800);  // 0xCF1: the geyser lifts him
                    }
                    if (g.world.cellPicture[0xAE] == 0x38) {  // gate open, the geyser starts
                        g.world.cellPicture[0xAE] = 0x35;
                        sfxPlay(g, 8);
                        ++g.world.fullRedrawReq;
                    }
                }
            } else {
                onPath(g, o);
            }
        } else {
            armKeyHint(g);
            mask &= 0xFFFC;
        }
    }
    cellDrawGeyser(g, 0x500, 0x50);
    return mask;
}

// 1568:141c Cell_P41_V41_GeyserTop. Quirk kept (§4.3): no kind test, so a visible call while 7E is
// on screen runs the elf branch for the stale g_curObj (normally the last elf slot, elf 14).
uint16_t cellP41V41GeyserTop(DosGame& g) {
    uint16_t& mask = g.world.cellMask;
    mask = 0x4FF;
    Obj& o = cur(g);
    if (isPlayer(g)) {
        const uint16_t st = g.player.playerState;
        if (yIn(o) != 0x70) {  // inside the hole or the column
            g.core.area = 3;
            mask = static_cast<uint16_t>((mask & 0xFFF4) | 0x800);  // 0xCF4
        } else {
            if (o.dy > 0 && st != 0x1D) {
                snapX(g, o, 0x740);
                mask = static_cast<uint16_t>((mask & 0xFFF4) | 0x800);
            }
            if ((st >= 0x11 && st <= 0x14) || st == 0x1D || xCol(o) <= 0x14 || xCol(o) >= 0x58) {
                mask &= 0xF7FC;  // jumping, sliding, or past the hole's middle
                if (o.dy < 0) o.dy = 0;
            } else {  // walking through the middle: slide down
                mask = static_cast<uint16_t>((mask & 0xFFF4) | 0x800);
                animSet(g, 1, 0x1D);
                g.player.playerState = 0x1E;
            }
        }
    } else if ((o.dx > 0 && xCol(o) <= 0x40) || (o.dx < 0 && xCol(o) >= 0x40)) {
        elfTurn(g);  // elves turn back at the hole
    }
    cellDrawGeyser(g, 0x500, 0x1E);
    return mask;
}

// 1568:15bc Cell_P45_V45_Bridge: the tier-dependent gap (it breaks at 4 stars and up).
uint16_t cellP45V45Bridge(DosGame& g) {
    uint16_t& mask = g.world.cellMask;
    mask = 0xFC;
    if (g.core.slowCpu != 0) return mask;  // no bridge logic and no rail on a slow CPU
    if (!objectCall(g)) {  // the rail
        SpriteSlot& s = g.player.slots[kDecorSlot];
        s.x = static_cast<int16_t>(g.world.cellScreenX + 0x1C);
        s.y = static_cast<int16_t>(g.world.cellScreenY + 0x73);
        s.z = 0x1E;
        s.frame = 0x178;
        s.owner = kDecorSlot;
        s.prev = 0;
        ++g.world.decorCount;
        return mask;
    }
    if (g.core.tier < 2) return mask;  // the bridge holds below 4 stars
    Obj& o = cur(g);
    if (isPlayer(g)) {
        const uint16_t st = g.player.playerState;
        if ((st >= 0x11 && st <= 0x14) || xCol(o) <= 0x28 || xCol(o) >= 0x50) return mask;
        mask = 0x1400;  // fall
        if (curPicture(g) == 0x2F) {
            setCurPicture(g, 0x30);  // broken
            ++g.world.fullRedrawReq;
        }
        sfxPlay(g, 9);  // on every such tick, also when already broken
        return mask;
    }
    if (curPicture(g) == 0x30 && ((o.dx > 0 && xCol(o) <= 0x3C) || (o.dx < 0 && xCol(o) >= 0x3C)))
        elfTurn(g);  // elves turn at a broken bridge
    return mask;
}

// 1568:16ed Cell_P48_Landing
uint16_t cellP48Landing(DosGame& g) {
    if (!objectCall(g) || !isPlayer(g)) return g.world.cellMask;  // AX unset in the original
    Obj& o = cur(g);
    if (yIn(o) == 0x70) {
        o.dy = 0;
        g.world.cellMask = 0xFC;
    } else {
        g.world.cellMask = 0x14F0;  // keep falling
    }
    return g.world.cellMask;
}

// 1568:1734 Cell_P56_V56_CastleHall
uint16_t cellP56V56CastleHall(DosGame& g) {
    if (!objectCall(g)) {  // the hall removes any creature still showing
        SpriteSlot& s6 = g.player.slots[6];
        if (s6.anim != 0) {
            s6.frame = 0xFFFF;
            g.player.slots[7].frame = 0xFFFF;
            s6.prev = 0;
            s6.anim = 0;
            g.world.creatureCell = 0;
        }
        return g.world.cellMask;
    }
    Obj& o = cur(g);
    uint16_t& mask = g.world.cellMask;
    mask = 0x14FF;
    if (yIn(o) == 0x70) {
        mask &= 0x00FF;
        if (o.dy > 0) {
            o.dy = 0;
            mask &= 0x00FD;
        }
    } else if (yIn(o) < 0x68) {
        mask |= 0x2100;  // ladder + ladder jump
    } else if ((o.x & 0x7F) != 0x40) {
        o.x = static_cast<uint16_t>((o.x & 0xFF80) + 0x40);
        ++g.world.fullRedrawReq;
    }
    if (xCol(o) < 0x30 || xCol(o) > 0x60) mask &= 0xFFFC;
    else if (curPicture(g) == 0x27) mask = static_cast<uint16_t>((mask & 0x0FFF) | 0x200C);
    else mask &= 0xFFFC;
    castleCreatureHitCheck(g);
    return mask;
}

// 1568:182b Cell_P57_V57_CastleWall
uint16_t cellP57V57CastleWall(DosGame& g) {
    if (!objectCall(g)) {
        castleCreatureSpawn(g);
        return g.world.cellMask;
    }
    Obj& o = cur(g);
    g.world.cellMask = 0x15F0;
    castleLadderMask(g, o);  // other pictures (0x2A wall) keep 0x15F0: fall
    if (xCol(o) < 0x30 || xCol(o) > 0x60) g.world.cellMask = 0x1400;
    castleCreatureHitCheck(g);
    return g.world.cellMask;
}

// 1568:1953 Cell_P60_IceDoor
uint16_t cellP60IceDoor(DosGame& g) {
    g.world.cellMask = 0xFF;
    if (cellGroundCheckPlayer(g) == 1) {
        Obj& o = cur(g);
        enterDoor(g, o, 0x6C0, 0x170, -0x40);
        animSet(g, 1, 3);
        g.core.area = 0x0C;
        musicStop(g);
        sfxStop(g);
        musicPlay(g, 2);
    }
    return g.world.cellMask;
}

// 1568:19ab Cell_P61_IceCaveExit
uint16_t cellP61IceCaveExit(DosGame& g) {
    Obj& o = cur(g);
    g.world.cellMask = 0x7C;
    if (o.dx < 0 && xCol(o) <= 0x48) exitToPath(g, o, 0x648, 0x3F0);
    return g.world.cellMask;
}

// 1568:1a20 Cell_P62_IceSlide
uint16_t cellP62IceSlide(DosGame& g) {
    Obj& o = cur(g);
    uint16_t& mask = g.world.cellMask;
    mask = 0x7C;
    if (o.dx > 0 && xCol(o) >= 0x68) {
        o.dy = 0;
        mask = 0x1400;
        o.x = 0x6A8;  // the rock shaft (3D)
        o.y = 0x1F0;
        g.world.transition = 2;
        g.world.cameraX = 0x600;
        g.world.fullRedrawReq = 1;
        animSet(g, 1, 0x3F);  // fall
        g.player.playerState = 0x1F;
        g.core.area = 0x0E;
        g.core.keyState = 4;  // the level-1 key state, set before landing on level 1
        sfxPlay(g, 9);
    }
    return mask;
}

// 1568:1aa4 Cell_P63_V63_CastleTop
uint16_t cellP63V63CastleTop(DosGame& g) {
    if (!objectCall(g)) {
        castleCreatureSpawn(g);
        return g.world.cellMask;
    }
    Obj& o = cur(g);
    uint16_t& mask = g.world.cellMask;
    mask = 0x15F0;
    if (!castleLadderMask(g, o) && curPicture(g) == 0x32) {  // the exit
        mask = 0x25F3;
        if (o.dy < 0 && yCol(o) <= 0x70) {  // climbing up out of the top: the end of the climb
            o.dy = 0;
            g.core.keyState = 0;
            mask = 0;
            g.core.level = 3;
            g.core.area = 0;
            g.core.slowCpu = g.world.slowCpuSaved;
            g.world.slowScroll = g.core.slowCpu;
            g.world.transition = 2;
            o.x = 0x400;  // the end screen (A8)
            o.y = 0x500;
            g.world.cameraX = 0x400;
            g.world.fullRedrawReq = 1;
            g.core.menuLock = 1;
            g.player.playerState = 0x29;
            animClear(g, 1);
            endSeqSetup(g);
            animSet(g, 6, 0x78);
            animSet(g, 7, 0x7E);
            animSet(g, 8, 0x7C);
            animSet(g, 10, 0x8E);
            animSet(g, 11, 0x8F);
            animSet(g, 4, 0x90);
            // *g_totalTreasures += g_treasuresFound (16-bit, no saturation).
            if (g.hasRec())
                g.rec().totalTreasures = static_cast<int16_t>(g.rec().totalTreasures + g.core.treasuresFound);
            treasurePickShowcase(g);
        }
        // Only the exit picture reaches the hit check: the ladder pictures (0x31, 0x2D, 0x25) and
        // 0x33 / 0x34 / 0x2A (fall) jump past it to the column test (1568:1c4c).
        castleCreatureHitCheck(g);
    }
    if (xCol(o) < 0x30 || xCol(o) > 0x60) mask = 0x1400;  // after the end step x 0x400 gives 0x1400
    return mask;
}

// 1568:1ed8 Cell_BonusCoins: once per game (the picture is saved and reset by Cells_ResetVariable).
uint16_t cellBonusCoins(DosGame& g) {
    const Obj& o = cur(g);
    if ((o.x & 0x7F) == 0x48 && g.world.cellPicture[0x26] == 0x4B) {
        g.world.cellPicture[0x26] = 0x49;
        sfxPlay(g, 4);
        ++g.world.fullRedrawReq;
        g.core.coins = static_cast<uint16_t>(g.core.coins + 5);  // no cap
    }
    return 0xFC;
}

// ---- visible-only handlers -----------------------------------------------------------------

// 1568:1c7b Cell_V49_Showcase (not counted in g_decorCount)
void cellV49Showcase(DosGame& g) {
    PlayerState& p = g.player;
    const WorldState& w = g.world;
    SpriteSlot& s3 = p.slots[3];
    s3.x = static_cast<int16_t>(w.cellScreenX + p.shackX);
    s3.y = static_cast<int16_t>(w.cellScreenY + p.shackY);
    s3.z = 0x50;
    s3.owner = 3;
    g.world.shelfCamRoom = 0;
    if (static_cast<int16_t>(s3.anim) < 0x5E && (p.shackFlags & 0x20) == 0) {
        s3.frame = static_cast<uint16_t>(p.shownTreasure);
        s3.prev = 0;
    }
    if ((p.shackFlags & 0x40) != 0 && p.shackPartner1 > 0) {
        SpriteSlot& s8 = p.slots[8];
        s8.x = static_cast<int16_t>(w.cellScreenX + p.shackPartner1X);
        s8.y = static_cast<int16_t>(w.cellScreenY + 0x44);
        s8.z = 0x46;
        s8.owner = 8;
        s8.frame = static_cast<uint16_t>(p.shackPartner1);
        s8.prev = 0;
    }
    if ((p.shackFlags & 0x40) != 0 && p.shackPartner2 > 0 && p.shackPartner1X < 0x36) {
        SpriteSlot& s10 = p.slots[10];
        s10.x = static_cast<int16_t>(w.cellScreenX + p.shackPartner2X);
        s10.y = static_cast<int16_t>(w.cellScreenY + 0x44);
        s10.z = 0x3C;
        s10.owner = 0x0A;
        s10.frame = static_cast<uint16_t>(static_cast<int16_t>(p.shackPartner2));
        s10.prev = 0;
    }
}

// 1568:1d96 Cell_V32_AnimDecor: depth and frame are left as they are.
void cellV32AnimDecor(DosGame& g) {
    if (g.core.slowCpu != 0) return;
    animSetIfIdle(g, kDecorSlot, 0x53);
    SpriteSlot& s = g.player.slots[kDecorSlot];
    s.x = static_cast<int16_t>(g.world.cellScreenX + 0x20);
    s.y = static_cast<int16_t>(g.world.cellScreenY + 0x80);
    ++g.world.decorCount;
}

// 1568:1ddb Cell_V10 (types 10, 33: the rock column)
void cellV10(DosGame& g) {
    if (g.core.slowCpu == 0) cellDrawObject(g, g.world.cellScreenX, g.world.cellScreenY + 0x80, 0x176);
}

// 1568:1dfa Cell_V59 (type 59: the wooden post)
void cellV59(DosGame& g) {
    if (g.core.slowCpu == 0) cellDrawObject(g, g.world.cellScreenX + 0x18, g.world.cellScreenY + 0x80, 0x17E);
}

// 1568:1e1c Cell_V7 (rock edge; no slow-CPU test)
void cellV7(DosGame& g) { cellDrawObject(g, g.world.cellScreenX - 1, g.world.cellScreenY + 0x78, 0x17A); }

// 1568:1e35 Cell_V8 (the mirrored rock edge)
void cellV8(DosGame& g) { cellDrawObject(g, g.world.cellScreenX + 0x41, g.world.cellScreenY + 0x78, 0x17B); }

}  // namespace tmport::dos
