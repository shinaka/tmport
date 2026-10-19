// player.c (segment 18cf): the player state machine, movement with the camera follow, the strip
// wrap, the search start and the castle-top end sequence (code_player.md §4, §6.2-§6.3).
//
// Every handler tests the keys Player_Update masked with the previous tick's cell mask, so a key
// the terrain forbids is lost until the next make code (a typematic repeat) brings it back; the
// high byte of g_keys is the mask's high byte (0x100 ladder, 0x200 shop, 0x400 camera mode,
// 0x800 slide, 0x1000 fall, 0x2000 ladder jump).
#include "dos/game/player.h"

#include <initializer_list>
#include <utility>

#include "dos/game/anim.h"
#include "dos/game/cells.h"
#include "dos/game/game.h"
#include "dos/game/layout.h"
#include "dos/game/search.h"
#include "dos/game/sound.h"

namespace tmport::dos {

namespace {

TerrainMaskHook g_terrainHook;  // tests only (player.h); the game uses Cell_GetMask

// Slot 1 is the player (g_playerSlotPtr DS:0B02 = DS:8FEC); slot 2 his net, coin or found content.
SpriteSlot& slot(DosGame& g, int s) { return g.player.slots[static_cast<size_t>(s)]; }
SpriteSlot& slot1(DosGame& g) { return slot(g, 1); }
bool idle(DosGame& g) { return slot1(g).anim == 0; }  // slot 1 runs no script
// "Facing left": the last script was the left stand (2) or the left walk (4).
bool facingLeft(DosGame& g) { return slot1(g).prev == 2 || slot1(g).prev == 4; }
uint16_t& keys(DosGame& g) { return g.keys(); }
bool onPathLine(const DosGame& g) { return (g.player.obj.y & 0x7F) == 0x70; }
void setState(DosGame& g, uint16_t s) { g.player.playerState = s; }
void nextState(DosGame& g, int delta = 1) { g.player.playerState = static_cast<uint16_t>(g.player.playerState + delta); }
void setSlotPos(DosGame& g, int s, int x, int y) {
    slot(g, s).x = static_cast<int16_t>(x);
    slot(g, s).y = static_cast<int16_t>(y);
}
// A hint flag of g_hintFlags DS:923E armed (0 = due) unless it was shown already (bit 7).
void armHint(DosGame& g, int n) {
    uint8_t& h = g.elves.hintFlags[static_cast<size_t>(n)];
    if ((h & 0x80) == 0) h = 0;
}

// The coin drop of PlrSt_10_OnPath 18cf:08cc (key 0x40, slot 1 idle): one coin out and the toss
// script, or, without coins, the hints "no coins" (7) and, with no nets either, "no nets" (6).
void dropCoin(DosGame& g, bool left) {
    if (g.core.coins == 0) {
        armHint(g, 7);
        if ((g.elves.hintFlags[6] & 0x80) == 0 && g.core.nets == 0) g.elves.hintFlags[6] = 0;
        return;
    }
    --g.core.coins;  // at once, before the throw
    animSet(g, 1, left ? 0x1A : 0x1B);
    setState(g, left ? 27 : 28);
    slot(g, 2).x = static_cast<int16_t>(left ? -25 : 25);  // g_slot2X DS:9002
}

// The ladder-jump and fall tests shared by states 15 and 16 (18cf:0f24.., 18cf:0ff5..).
void ladderJumpOrFall(DosGame& g) {
    const uint16_t k = keys(g);
    if (k & 0x2000) {
        // Quirk kept (PORTING_NOTES §6): a ladder jump clears the slow-CPU flag; the castle top
        // restores it from DS:8FA5.
        if (k & 4) {
            g.core.slowCpu = 0;
            animSet(g, 1, 0x3D);
            setState(g, 33);
        } else if (k & 8) {
            g.core.slowCpu = 0;
            animSet(g, 1, 0x3E);
            setState(g, 33);
        }
    }
    if (k & 0x1000) {
        animSet(g, 1, 0x3F);
        setState(g, 31);
    }
}

// The terrain query of Player_Move: Cell_GetMask (world), or the tests' stand-in.
uint16_t terrainMask(DosGame& g) { return g_terrainHook ? g_terrainHook(g) : cellGetMask(g); }

// The picture count of a level: the word at DS:122E + 8 * level (g_levelPicData[level][0] for the
// mountain levels; levels 3 and 4 read the EXE words that follow, as the original does).
uint16_t levelPictureCount(const DosGame& g) {
    if (g.core.level < g.world.levelPicData.size()) return g.world.levelPicData[g.core.level][0];
    return g.exe().ds16(static_cast<uint16_t>(dg::kLevelPicData + dg::kLevelPicDataStride * g.core.level));
}

// The cell in front of the player for a search: (y >> 7) * 16 + (x >> 7) + g_searchSide
// (unsigned shifts; -1 or 256 at the strip ends).
int searchedCellIndex(const DosGame& g) {
    return static_cast<int>(g.player.obj.y >> 7) * 16 + static_cast<int>(g.player.obj.x >> 7) + g.player.searchSide;
}

}  // namespace

void setTerrainMaskHookForTests(TerrainMaskHook hook) { g_terrainHook = std::move(hook); }

// ---- tick entry, movement, dispatch --------------------------------------------------------

// 18cf:181a Player_Update
void playerUpdate(DosGame& g) {
    if (g.core.uiMode != 0 || g.world.transition != 0) return;
    g.world.curObj = DsPtr{kPlayerObjOff};
    g.player.keysUnmasked = keys(g);
    keys(g) = static_cast<uint16_t>((keys(g) | 0xFF00) & g.player.playerMask);
    callPlayerStateHandler(g, g.player.playerState);
    playerMove(g);
}

// g_playerStateHandlers DS:0B8A: states 1, 2, 6-8, 36-40 and 62 point at a lone RETF (1568:0007).
void callPlayerStateHandler(DosGame& g, uint16_t state) {
    switch (state) {
        case 0: plrSt00Idle(g); break;
        case 3: plrSt03Spawn(g); break;
        case 4: plrSt04Arrive(g); break;
        case 5: plrSt05Searching(g); break;
        case 9: plrSt09AfterTeleport(g); break;
        case 10: plrSt10OnPath(g); break;
        case 11: plrSt11WalkLeft(g); break;
        case 12: plrSt12WalkRight(g); break;
        case 13: plrSt13LookUp(g); break;
        case 14: plrSt14Front(g); break;
        case 15: plrSt15ClimbUp(g); break;
        case 16: plrSt16ClimbDown(g); break;
        case 17: plrSt17JumpLeft(g); break;
        case 18: plrSt18JumpLeftLand(g); break;
        case 19: plrSt19JumpRight(g); break;
        case 20: plrSt20JumpRightLand(g); break;
        case 21: plrSt21NetLeftSwing(g); break;
        case 22: plrSt22NetLeftHold(g); break;
        case 23: plrSt23NetLeftDone(g); break;
        case 24: plrSt24NetRightSwing(g); break;
        case 25: plrSt25NetRightHold(g); break;
        case 26: plrSt26NetRightDone(g); break;
        case 27: plrSt27CoinLeftDone(g); break;
        case 28: plrSt28CoinRightDone(g); break;
        case 29: plrSt29PassageUp(g); break;
        case 30: plrSt30SlideDown(g); break;
        case 31: plrSt31FallLeft(g); break;
        case 32: plrSt32FallRight(g); break;
        case 33: plrSt33LadderJump(g); break;
        case 34: plrSt34Chute(g); break;
        case 35: plrSt35TreasureRoomLand(g); break;
        case 41: plrSt41EndIntro(g); break;
        case 42: plrSt42EndHorn(g); break;
        case 43: plrSt43EndBeam(g); break;
        case 44: plrSt44EndSparkle(g); break;
        case 45: plrSt45EndChestOpen(g); break;
        case 46: plrSt46EndStep(g); break;
        case 47: plrSt47EndStep(g); break;
        case 48: plrSt48EndFlightInit(g); break;
        case 49: plrSt49EndFlightNext(g); break;
        case 50: plrSt50EndFlightStart(g); break;
        case 51: plrSt51EndFlightStep(g); break;
        case 52: plrSt52EndRank(g); break;
        case 53: plrSt53EndRankSparkle(g); break;
        case 54: plrSt54EndRankWait(g); break;
        case 55: plrSt55EndCrown(g); break;
        case 56: plrSt56EndCreature(g); break;
        case 57: plrSt57EndPlayer(g); break;
        case 58: plrSt58EndShowcaseCheck(g); break;
        case 59: plrSt59EndShowcaseStart(g); break;
        case 60: plrSt60EndShowcaseStep(g); break;
        case 61: plrSt61EndDone(g); break;
        case 63: plrSt63ChampSparkle(g); break;
        case 64: plrSt64ChampChest(g); break;
        case 65: plrSt65ChampArcs(g); break;
        case 66: plrSt66ChampScream(g); break;
        case 67: plrSt67ChampCrownFly(g); break;
        case 68: plrSt68ChampCatch(g); break;
        case 69: plrSt69ChampCrowned(g); break;
        case 70: plrSt70ChampSparkle(g); break;
        case 71: plrSt71ChampSparkleWait(g); break;
        default: break;  // the frozen states: a cell handler or another module drives the player
    }
}

// 18cf:15f0 Player_Move
void playerMove(DosGame& g) {
    WorldState& w = g.world;
    Obj& p = g.player.obj;
    int16_t si = g.player.playerScrX;  // screen offsets of this tick (Game_Tick), bytes / rows
    int16_t di = g.player.playerScrY;
    // The handler of the cell at the position before this move: it may teleport the player and
    // set g_transition; the camera is re-anchored on the (new) position by the transition mode.
    g.player.playerMask = terrainMask(g);
    if (w.transition & 1) {
        w.cameraY = p.y & 0xFF80;
        w.cameraX = static_cast<uint16_t>(p.x - 2 * si);
    } else if (w.transition & 2) {
        w.cameraY = p.y & 0xFF80;
        di = static_cast<int16_t>(p.y - w.cameraY);
        si = static_cast<int16_t>(static_cast<int16_t>(p.x - w.cameraX) >> 1);  // the camera x is kept
        g.player.playerScrX = si;
    } else {
        w.cameraY = static_cast<uint16_t>(p.y - di);
        w.cameraX = static_cast<uint16_t>(p.x - 2 * si);
    }
    if (w.cameraX > 0x800) {  // the camera wrapped to the previous row of the strip (unsigned)
        w.cameraY = static_cast<uint16_t>(w.cameraY - 0x80);
        w.cameraX = static_cast<uint16_t>(w.cameraX + 0x800);
    }
    w.camStepX = p.dx;
    w.camStepY = p.dy;
    if (p.dx != 0 || p.dy != 0) {
        // Horizontal: the camera scrolls once the player is within 0x20 bytes of the left edge or
        // at 0x7C bytes and beyond (signed); otherwise the player moves on the screen.
        if ((p.dx < 0 && si <= 0x20) || (p.dx > 0 && si >= 0x7C)) {
            if (w.slowScroll == 1) {  // slow CPU: the player moves and the screen scrolls twice as far
                p.dx = static_cast<int16_t>(p.dx * 2);
                w.camStepX = p.dx;
            }
        } else {
            si = static_cast<int16_t>(si + p.dx);
            w.camStepX = 0;
        }
        // Vertical: up to row 0x40 (0x70 in camera mode 0x400), down from row 0x70.
        const int lim = (g.player.playerMask & 0x400) ? 0x70 : 0x40;
        if ((p.dy < 0 && di <= lim) || (p.dy > 0 && di >= 0x70)) {
            if (w.slowScroll == 1) {
                p.dy = static_cast<int16_t>(p.dy * 2);
                w.camStepY = p.dy;
            }
        } else {
            di = static_cast<int16_t>(di + p.dy);
            w.camStepY = 0;
        }
        p.x = static_cast<uint16_t>(p.x + 2 * p.dx);
        p.y = static_cast<uint16_t>(p.y + p.dy);
        if ((w.transition & 2) == 0) {
            objWrapWorldX(g);  // on g_curObj = the player
            w.cameraY = static_cast<uint16_t>(p.y - di);
            w.cameraX = static_cast<uint16_t>(p.x - 2 * si);
        }
        if (w.cameraX > 0x800) {
            w.cameraY = static_cast<uint16_t>(w.cameraY - 0x80);
            w.cameraX = static_cast<uint16_t>(w.cameraX + 0x800);
        }
    }
    if ((w.camStepX | w.camStepY) != 0) g.stepGate().mark();  // a scroll counts as a step
    p.dx = 0;
    p.dy = 0;
    SpriteSlot& s1 = slot1(g);
    s1.x = static_cast<int16_t>(si + 2);
    s1.y = static_cast<int16_t>(di + 0x10);
    const unsigned r = p.y & 0x7F;
    const int depth = r == 0x70 ? 0x32 : (r > 0x18 && r < 0x70 ? 0x3D : 0x27);
    s1.z = static_cast<int16_t>(depth + 1);
    if (g.player.searchTimer == 0) slot(g, 2).z = static_cast<int16_t>(depth);  // net/coin in front of the player
}

// 18cf:17c9 Obj_WrapWorldX
void objWrapWorldX(DosGame& g) {
    Obj& o = objAt(g, g.world.curObj);
    // Two sequential tests (unsigned): left of the strip start, then right of the row end.
    if (o.x > 0x7FFF) {
        o.y = static_cast<uint16_t>(o.y - 0x80);
        o.x = static_cast<uint16_t>(o.x + 0x800);
        o.dy = 0;
    }
    if (o.x > 0x7FF) {
        o.y = static_cast<uint16_t>(o.y + 0x80);
        o.x = static_cast<uint16_t>(o.x - 0x800);
        o.dy = 0;
    }
}

// 18cf:0b36 Player_NetLeft
void playerNetLeft(DosGame& g) {
    animSet(g, 1, g.core.nets == 0 ? 0x15 : 0x14);  // 0x15: the torn net
    slot(g, 2).x = 0;
    setState(g, 21);
}

// 18cf:0b6a Player_NetRight
void playerNetRight(DosGame& g) {
    animSet(g, 1, g.core.nets == 0 ? 0x18 : 0x17);
    slot(g, 2).x = 0;
    setState(g, 24);
}

// 18cf:14e0 Search_Start
void searchStart(DosGame& g) {
    if (g.player.searchTimer >= 1) return;  // a search still runs: retried every tick until it ends
    setState(g, 10);
    if (searchCanSearch(g)) {
        setState(g, 5);
        g.player.searchTimer = 0x39;
        g.player.searchCell = static_cast<uint16_t>(searchedCellIndex(g));
        g.player.searchPicture = static_cast<uint8_t>(cellPictureRaw(g, g.tables, static_cast<int16_t>(g.player.searchCell)) & 0xFF);
        g.core.menuLock = 1;
    }
    // Without a hiding place in front the coin is simply lost.
}

// 18cf:1532 Search_CanSearch
int searchCanSearch(DosGame& g) {
    // g_searchSide is not reset here (Search_Update zeroes it on every frame without a search).
    // "Facing right" means script 3 (or prev 3): after a walk to the right (prev 5) the side comes
    // only from the stand script 3 that states 27/28 start before calling Search_Start.
    const SpriteSlot& s1 = slot1(g);
    const unsigned xr = g.player.obj.x & 0x7F;
    int16_t& side = g.player.searchSide;
    if (s1.anim == 3 && xr > 0x60) side = 1;
    if (s1.anim == 2 && xr < 0x20) side = -1;
    if (side == 0 && s1.prev == 3 && xr > 0x60) side = 1;
    if (side == 0 && s1.prev == 2 && xr < 0x20) side = -1;
    // Closed pictures 1..count, their opened variants up to count + 3 and the unused 0 are all
    // accepted (the whole word is compared, unsigned).
    const uint16_t picture = cellPictureRaw(g, g.tables, searchedCellIndex(g));
    return picture <= static_cast<uint16_t>(levelPictureCount(g) + 3) ? 1 : 0;
}

// 18cf:0038 EndSeq_Setup
void endSeqSetup(DosGame& g) {
    g.player.endSeqDone = 0;
    for (int s : {4, 6, 7, 8, 9, 10}) setSlotPos(g, s, 2, 0x10);  // the play-field origin: CLOSE.PAK
    setSlotPos(g, 11, 0x18, 0x2A);                                 // frames carry their own offsets
    slot(g, 6).z = 10;
    for (int s : {7, 8, 10, 11}) slot(g, s).z = 0x0F;
    slot(g, 3).z = 0x0D;  // slot 3's position is not set here
    slot(g, 4).z = 0x0D;
    slot(g, 9).z = 8;
    sfxStop(g);
    musicStop(g);
}

// ---- states 0-35 ---------------------------------------------------------------------------

// 18cf:000d PlrSt_00_Idle
void plrSt00Idle(DosGame& g) {
    if (keys(g) & 0x100) setState(g, 15);
    animSet(g, 1, 6);
    if (slot1(g).prev == 6) setState(g, 10);
}

// 18cf:082a PlrSt_03_Spawn
void plrSt03Spawn(DosGame& g) { setState(g, 4); }

// 18cf:0831 PlrSt_04_Arrive
void plrSt04Arrive(DosGame& g) {
    const uint16_t k = keys(g);
    if ((k & 0x3900) == 0) {
        animSet(g, 1, 6);  // front pose on a path
        setState(g, 10);
    } else if (k & 0x100) {
        Obj& o = objAt(g, g.world.curObj);
        if ((o.x & 0x7F) != 0x40) {  // snap to the ladder in the cell's middle
            o.x = static_cast<uint16_t>((o.x & 0xFF80) + 0x40);
            ++g.world.fullRedrawReq;
        }
        animSet(g, 1, 0x1E);
        setState(g, 15);
    } else if (k & 0x800) {
        animSet(g, 1, 0x1C);
        setState(g, 29);
    } else {  // 0x1000 or 0x2000
        animSet(g, 1, 0x3F);
        setState(g, 31);
    }
}

// 18cf:07f6 PlrSt_05_Searching
void plrSt05Searching(DosGame& g) {
    // A direction key ends the wait at once and makes the search jump to its last step when the
    // place is still in front; otherwise the search keeps running while the player walks away.
    if ((keys(g) & 0xF) != 0 || g.player.searchTimer == 1) {
        if (searchCanSearch(g)) g.player.searchTimer = 1;
        setState(g, 10);
    }
}

// 18cf:081b PlrSt_09_AfterTeleport
void plrSt09AfterTeleport(DosGame& g) {
    if ((keys(g) & 0xF) == 0) setState(g, 10);  // frozen until every direction key is released
}

// 18cf:08cc PlrSt_10_OnPath
void plrSt10OnPath(DosGame& g) {
    const uint16_t k = keys(g);
    if ((k & 1) && idle(g)) {  // Up acts only when no script runs
        if (k & 0x100) {
            animSet(g, 1, 0xC);
            setState(g, 15);
        } else if (k & 0x800) {
            animSet(g, 1, 0x1C);
            setState(g, 29);
        } else {
            animSet(g, 1, 0xB);
            setState(g, 13);
        }
    } else if (k & 2) {
        animSet(g, 1, 6);
        setState(g, 14);
    } else if ((objAt(g, g.world.curObj).y & 0x7F) == 0x70) {  // on the path line
        if ((k & 0x24) == 0x24) {
            animSet(g, 1, 0xE);
            setState(g, 17);
        } else if ((k & 0x28) == 0x28) {
            animSet(g, 1, 0xF);
            setState(g, 19);
        } else if ((k & 0x14) == 0x14) {
            playerNetLeft(g);
        } else if ((k & 0x18) == 0x18) {
            playerNetRight(g);
        } else if (k & 4) {
            animSet(g, 1, 2);  // stand left first; the walk starts once the stand script ends
            setState(g, 11);
        } else if (k & 8) {
            animSet(g, 1, 3);
            setState(g, 12);
        } else if (k & 0x1000) {
            animSet(g, 1, 0x3F);
            setState(g, 31);
        } else if (idle(g)) {  // Space alone and the coin key wait for the stand/front script
            if (facingLeft(g)) {
                if (k & 0x40) dropCoin(g, true);
                else if (k & 0x10) playerNetLeft(g);
            } else {
                if (k & 0x40) dropCoin(g, false);
                else if (k & 0x10) playerNetRight(g);
            }
        }
    } else if (k & 0xC) {  // above the line (a doorway): walk down to the line first
        animSet(g, 1, 6);
        setState(g, 14);
    }
}

// 18cf:0b9e PlrSt_11_WalkLeft
void plrSt11WalkLeft(DosGame& g) {
    const uint16_t k = keys(g);
    if ((k & 0x2C) == 0x24) {
        animSet(g, 1, 0xE);
        setState(g, 17);
    } else if ((k & 0x2C) == 0x28) {
        animSet(g, 1, 0xF);
        setState(g, 19);
    } else if ((k & 0x14) == 0x14) {
        playerNetLeft(g);
    } else if ((k & 0x18) == 0x18) {
        playerNetRight(g);
    } else if (k & 4) {
        animSetIfIdle(g, 1, 4);  // keep walking: restart the 8-step cycle when it ends
    } else if (k & 9) {
        animSet(g, 1, 0xB);
        setState(g, 10);
    } else if (k & 2) {
        animSet(g, 1, 6);
        setState(g, 10);
    } else if (k & 0x1000) {
        animSet(g, 1, 0x3F);
        setState(g, 31);
    } else if (slot1(g).delay >= 0) {  // always true: releasing the key stops at once
        animSet(g, 1, 2);
        setState(g, 10);
    }
}

// 18cf:0ca0 PlrSt_12_WalkRight
void plrSt12WalkRight(DosGame& g) {
    const uint16_t k = keys(g);
    if ((k & 0x2C) == 0x24) {
        animSet(g, 1, 0xE);
        setState(g, 17);
    } else if ((k & 0x2C) == 0x28) {
        animSet(g, 1, 0xF);
        setState(g, 19);
    } else if ((k & 0x14) == 0x14) {
        playerNetLeft(g);
    } else if ((k & 0x18) == 0x18) {
        playerNetRight(g);
    } else if (k & 8) {
        animSetIfIdle(g, 1, 5);
    } else if (k & 1) {
        animSet(g, 1, 0xB);
        setState(g, 10);
    } else if (k & 6) {
        animSet(g, 1, 6);
        setState(g, 10);
    } else if (k & 0x1000) {
        animSet(g, 1, 0x40);
        setState(g, 32);
    } else if (slot1(g).delay >= 0) {
        animSet(g, 1, 3);
        setState(g, 10);
    }
}

// 18cf:0da2 PlrSt_13_LookUp
void plrSt13LookUp(DosGame& g) {
    const uint16_t k = keys(g);
    if (idle(g)) {
        if (k & 0xE) {
            animSet(g, 1, 6);
            setState(g, 14);
        } else if ((k & 1) == 0) {
            animSetIfIdle(g, 1, 0xB);
            setState(g, 10);
        } else {
            animSetIfIdle(g, 1, 9);  // two 8-px steps into the background per cycle
        }
    } else if (k & 0x100) {
        animSet(g, 1, 0xC);
        setState(g, 15);
    } else if (k & 0x800) {
        animSet(g, 1, 0x1C);
        setState(g, 29);
    }
}

// 18cf:0e39 PlrSt_14_Front
void plrSt14Front(DosGame& g) {
    const uint16_t k = keys(g);
    if (k & 1) {
        animSet(g, 1, 0xB);
        setState(g, 13);
    } else if (k & 2) {
        if (k & 0x100) {
            animSet(g, 1, 0xD);
            setState(g, 16);
        } else {
            animSetIfIdle(g, 1, 10);  // two 8-px steps towards the front
        }
    } else if ((k & 0xC) && (objAt(g, g.world.curObj).y & 0x7F) < 0x70) {
        animSetIfIdle(g, 1, 10);
    }
    if (idle(g)) {  // independent of the tests above
        animSet(g, 1, 6);
        setState(g, 10);
    }
}

// 18cf:0ed8 PlrSt_15_ClimbUp
void plrSt15ClimbUp(DosGame& g) {
    const uint16_t k = keys(g);
    if ((k & 0x100) == 0) {
        animSet(g, 1, 0xB);
        setState(g, 10);
        return;
    }
    if (idle(g)) {
        if (k & 2) {
            animSetIfIdle(g, 1, 0xD);
            setState(g, 16);
        } else if (k & 1) {
            animSetIfIdle(g, 1, 0xC);  // 4 steps of 8 rows per cycle
        }
    }
    ladderJumpOrFall(g);
}

// 18cf:0fa9 PlrSt_16_ClimbDown
void plrSt16ClimbDown(DosGame& g) {
    const uint16_t k = keys(g);
    if ((k & 0x100) == 0) {
        animSet(g, 1, 6);
        setState(g, 10);
        return;
    }
    if (idle(g)) {
        if (k & 1) {
            animSetIfIdle(g, 1, 0xC);
            setState(g, 15);
        } else if (k & 2) {
            animSetIfIdle(g, 1, 0xD);
        }
    }
    ladderJumpOrFall(g);
}

// 18cf:107a PlrSt_17_JumpLeft
void plrSt17JumpLeft(DosGame& g) {
    if (!idle(g)) return;
    if (slot1(g).prev == 0x10 || (keys(g) & 0x24) != 0x24) {
        if (keys(g) & 8) {
            animSetIfIdle(g, 1, 0x11);
            setState(g, 19);
        } else {
            animSetIfIdle(g, 1, 0x12);  // the landing flip
            nextState(g);
        }
    } else {
        animSetIfIdle(g, 1, 0x10);  // jump-left still held after the first flip: one more flip
    }
}

// 18cf:10df PlrSt_18_JumpLeftLand
void plrSt18JumpLeftLand(DosGame& g) {
    if (!idle(g)) return;
    if ((keys(g) & 0x24) == 0x24) {
        animSetIfIdle(g, 1, 0xE);
        nextState(g, -1);
    } else {
        animSetIfIdle(g, 1, 2);
        setState(g, 10);
    }
}

// 18cf:1120 PlrSt_19_JumpRight
void plrSt19JumpRight(DosGame& g) {
    if (!idle(g)) return;
    if (slot1(g).prev == 0x11 || (keys(g) & 0x28) != 0x28) {
        if (keys(g) & 4) {
            animSetIfIdle(g, 1, 0x10);
            setState(g, 17);
        } else {
            animSetIfIdle(g, 1, 0x13);
            nextState(g);
        }
    } else {
        animSetIfIdle(g, 1, 0x11);
    }
}

// 18cf:1185 PlrSt_20_JumpRightLand
void plrSt20JumpRightLand(DosGame& g) {
    if (!idle(g)) return;
    if ((keys(g) & 0x28) == 0x28) {
        animSetIfIdle(g, 1, 0xF);
        nextState(g, -1);
    } else {
        animSetIfIdle(g, 1, 3);
        setState(g, 10);
    }
}

// 18cf:11c6 PlrSt_21_NetLeftSwing
void plrSt21NetLeftSwing(DosGame& g) {
    if (idle(g)) nextState(g);  // elves are caught only while the net goes out (21/24)
}

// 18cf:11d6 PlrSt_22_NetLeftHold
void plrSt22NetLeftHold(DosGame& g) {
    if ((keys(g) & 0x10) == 0 && idle(g)) {  // while Space is held the net stays out
        animQueue(g, 1, 0x16);
        nextState(g);
    }
}

// 18cf:11fd PlrSt_23_NetLeftDone
void plrSt23NetLeftDone(DosGame& g) {
    if (!idle(g)) return;
    animSet(g, 1, 2);
    setState(g, 10);
    slot(g, 2).frame = 0xFFFF;
}

// 18cf:1224 PlrSt_24_NetRightSwing
void plrSt24NetRightSwing(DosGame& g) {
    if (idle(g)) nextState(g);
}

// 18cf:1234 PlrSt_25_NetRightHold
void plrSt25NetRightHold(DosGame& g) {
    if ((keys(g) & 0x10) == 0 && idle(g)) {
        animQueue(g, 1, 0x19);
        nextState(g);
    }
}

// 18cf:125b PlrSt_26_NetRightDone
void plrSt26NetRightDone(DosGame& g) {
    if (!idle(g)) return;
    animSet(g, 1, 3);
    setState(g, 10);
    slot(g, 2).frame = 0xFFFF;
}

// 18cf:1282 PlrSt_27_CoinLeftDone
void plrSt27CoinLeftDone(DosGame& g) {
    if (!idle(g)) return;
    animSetIfIdle(g, 1, 2);
    slot(g, 2).frame = 0xFFFF;
    if (keys(g) & 0x200) {  // the coin pays the net shop
        setState(g, 10);
        --g.core.shopCoinsOwed;
    } else {
        searchStart(g);
    }
}

// 18cf:12bc PlrSt_28_CoinRightDone
void plrSt28CoinRightDone(DosGame& g) {
    if (!idle(g)) return;
    animSetIfIdle(g, 1, 3);
    slot(g, 2).frame = 0xFFFF;
    if (keys(g) & 0x200) {
        setState(g, 10);
        --g.core.shopCoinsOwed;
    } else {
        searchStart(g);
    }
}

// 18cf:12f6 PlrSt_29_PassageUp
void plrSt29PassageUp(DosGame& g) {
    // Quirk kept: the effect is restarted every tick (not on a slow CPU, not while the cell
    // decoration slot 12 runs a script).
    if (g.core.slowCpu == 0 && slot(g, 12).anim == 0) sfxPlay(g, 1);
    if ((keys(g) & 0x800) == 0) {  // out of the passage: come out flipping
        g.core.area = 0;
        sfxStop(g);
        if (keys(g) & 4) {
            setState(g, 17);
            animSet(g, 1, 0x10);
        } else {
            setState(g, 19);
            animSet(g, 1, 0x11);
        }
    } else {
        animSetIfIdle(g, 1, 0x1C);
    }
}

// 18cf:1367 PlrSt_30_SlideDown
void plrSt30SlideDown(DosGame& g) {
    if (g.core.slowCpu == 0 && slot(g, 12).anim == 0) sfxPlay(g, 1);
    if ((keys(g) & 0x800) == 0) {
        g.core.area = 0;
        sfxStop(g);
        animSet(g, 1, 6);
        setState(g, 10);
    } else {
        animSetIfIdle(g, 1, 0x1D);
    }
}

// 18cf:13b9 PlrSt_31_FallLeft
void plrSt31FallLeft(DosGame& g) {
    if (keys(g) & 0x1000) {
        animSetIfIdle(g, 1, 0x3F);  // 8 rows per 5 ticks
        return;
    }
    if ((keys(g) & 0x100) == 0) {  // land with a flip
        setState(g, 17);
        keys(g) &= 0xFFF3;
        animSet(g, 1, 0x10);
    } else {  // catch the ladder
        keys(g) &= 0xFFF3;
        animSet(g, 1, 0x1E);
        setState(g, 16);
    }
}

// 18cf:140c PlrSt_32_FallRight
void plrSt32FallRight(DosGame& g) {
    if (keys(g) & 0x1000) {
        animSetIfIdle(g, 1, 0x40);
        return;
    }
    if ((keys(g) & 0x100) == 0) {
        setState(g, 19);
        keys(g) &= 0xFFF3;
        animSet(g, 1, 0x11);
    } else {
        keys(g) &= 0xFFF3;
        animSet(g, 1, 0x1E);
        setState(g, 16);
    }
}

// 18cf:145f PlrSt_33_LadderJump
void plrSt33LadderJump(DosGame& g) {
    if (!idle(g)) return;
    if ((keys(g) & 0x100) == 0) {  // no ladder at the landing point: fall
        setState(g, 32);
        keys(g) &= 0xFFF3;
        animSetIfIdle(g, 1, 0x40);
    } else {
        setState(g, 15);
        keys(g) &= 0xFFF3;
    }
}

// 18cf:1495 PlrSt_34_Chute
void plrSt34Chute(DosGame& g) {
    animSetIfIdle(g, 1, 0x5D);  // 4 steps of 8 rows per 29 ticks
    if (slot1(g).prev != 0x5D) treasureAddCollected(g);  // acts once (it sets g_showcasePick = 0xFF)
}

// 18cf:14b1 PlrSt_35_TreasureRoomLand
void plrSt35TreasureRoomLand(DosGame& g) {
    if ((keys(g) & 0x1000) == 0) {
        setState(g, 1);
        animSet(g, 1, 0x52);  // landing
    } else {
        animSetIfIdle(g, 1, 0x5D);  // keep falling
    }
}

// ---- the end sequence, states 41-61, and the champion branch 63-71 -------------------------
// Each state waits for one slot's script to end, then starts the next scripts (CLOSE.PAK, the
// scene 3 bank) and advances. Slots: 6 the player figure, 7 the horn, 8 the chest, 9 sparkles and
// flying treasures, 10 the creature, 11 the crown, 3/4 eyes and puffs.

// 18cf:00a9 PlrSt_41_EndIntro
void plrSt41EndIntro(DosGame& g) {
    if (slot(g, 6).anim != 0) return;
    animSet(g, 6, 0x80);
    nextState(g);
}

// 18cf:00c4 PlrSt_42_EndHorn
void plrSt42EndHorn(DosGame& g) {
    if (slot(g, 6).anim != 0) return;
    sfxPlay(g, 8);
    animSet(g, 7, 0x7F);
    animSet(g, 6, 0x79);
    nextState(g);
}

// 18cf:00f8 PlrSt_43_EndBeam
void plrSt43EndBeam(DosGame& g) {
    if (slot(g, 6).anim != 0) return;
    animSet(g, 9, 0x7A);
    animSet(g, 6, 0x7B);
    nextState(g);
}

// 18cf:0122 PlrSt_44_EndSparkle
void plrSt44EndSparkle(DosGame& g) {
    if (slot(g, 9).anim != 0) return;
    setSfxLoop(g, true);  // effect 2 loops while the sparkle runs
    sfxPlay(g, 2);
    animSet(g, 9, 0x7D);
    nextState(g);
}

// 18cf:014d PlrSt_45_EndChestOpen
void plrSt45EndChestOpen(DosGame& g) {
    if (slot(g, 9).anim != 0) return;
    setSfxLoop(g, false);
    sfxPlay(g, 8);  // refused while effect 2 plays its last pass (lower ids have priority)
    animClear(g, 9);
    animSet(g, 8, 0x83);
    nextState(g);
}

// 18cf:0182 PlrSt_46_EndStep
void plrSt46EndStep(DosGame& g) {
    if (slot(g, 8).anim != 0) return;
    slot(g, 6).x = 10;
    animSet(g, 6, 0x82);
    nextState(g);
}

// 18cf:01a3 PlrSt_47_EndStep
void plrSt47EndStep(DosGame& g) {
    if (slot(g, 6).anim != 0) return;
    animSet(g, 7, 0x7E);
    animSet(g, 6, 0x8B);
    nextState(g);
}

// 18cf:01cd PlrSt_48_EndFlightInit
void plrSt48EndFlightInit(DosGame& g) {
    if (slot(g, 6).anim != 0) return;
    slot(g, 6).x = 2;
    animSet(g, 6, 0x86);
    nextState(g);
    g.player.endScanIdx = 0;
    g.player.endTreasureCount = 0;
    layoutUnmarkSharedCells(g);  // each treasure of a mirrored cell is counted once
    musicPlay(g, 1);
}

// 18cf:0205 PlrSt_49_EndFlightNext
void plrSt49EndFlightNext(DosGame& g) {
    if (slot(g, 6).anim != 0) return;
    if (g.player.endScanIdx < 0x30) {  // one hiding place per tick
        const int16_t c = static_cast<int16_t>(
            g.world.cellContent[hidingCellAt(g, g.tables, g.player.endScanIdx) & 0xFF]);
        ++g.player.endScanIdx;
        if (c > 0x80 && c < 0xE6) {  // a treasure found this game
            g.player.endPathIdx = 0;
            setSlotPos(g, 9, 0x9A, 0x5A);
            // The treasure sprite of the flight is script 0x88: its frame word is patched with
            // the treasure's frame code (6 + 2k = sprite 3 + k, the same in every PAK).
            animScriptWordAt(g, dg::kAnimScript88Frame) = static_cast<int16_t>(c & 0x7F);
            animSet(g, 9, 0x87);
            nextState(g);
            ++g.player.endTreasureCount;
        }
    } else {  // all 48 scanned: the chest closes
        animSet(g, 8, 0x8C);
        nextState(g, 3);
    }
}

// 18cf:0280 PlrSt_50_EndFlightStart
void plrSt50EndFlightStart(DosGame& g) {
    if (slot(g, 9).anim != 0) return;
    setSlotPos(g, 9, 0x88, 0x40);
    animSet(g, 9, 0x88);
    nextState(g);
}

// 18cf:02a7 PlrSt_51_EndFlightStep
void plrSt51EndFlightStep(DosGame& g) {
    if (slot(g, 9).anim != 0) return;
    const auto& path = g.tables.player.endFlightPathY;  // -1 terminated
    const size_t i = g.player.endPathIdx;
    const int16_t y = i < path.size() ? path[i] : int16_t{-1};
    if (y < 0) {
        animClear(g, 9);
        nextState(g, -2);  // back to the scan
    } else {
        slot(g, 9).x = static_cast<int16_t>(slot(g, 9).x - 4);
        slot(g, 9).y = y;
        ++g.player.endPathIdx;
        animSetIfIdle(g, 9, 0x88);  // one step per 3 ticks
    }
}

// 18cf:02f2 PlrSt_52_EndRank
void plrSt52EndRank(DosGame& g) {
    if (slot(g, 8).anim != 0) return;
    nextState(g);
    // The stars of the rank of the new total: the first pair of g_rankTable DS:0110 whose
    // threshold is above the total (unsigned); 7 pairs or more is the champion branch.
    const auto& t = g.tables.player.rankTable;
    const uint16_t limit = static_cast<uint16_t>(g.rec().totalTreasures + 1);
    size_t i = 0;
    while (i + 1 < t.size() && t[i] != -1 && static_cast<uint16_t>(t[i]) < limit) i += 2;
    if (i < 14) {
        g.player.endCount = static_cast<int16_t>(i / 2);
        g.player.endPathIdx = 0;
    } else {
        setState(g, 63);
    }
}

// 18cf:0343 PlrSt_53_EndRankSparkle
void plrSt53EndRankSparkle(DosGame& g) {
    if (slot(g, 8).anim != 0) return;
    if (g.player.endCount == 0) {
        --g.player.endCount;
        animClear(g, 9);
        nextState(g, 2);
    } else {
        const size_t i = g.player.endPathIdx;
        const auto& xs = g.tables.player.endRankSparkleX;
        const auto& ys = g.tables.player.endRankSparkleY;
        setSlotPos(g, 9, i < xs.size() ? xs[i] : 0, i < ys.size() ? ys[i] : 0);
        ++g.player.endPathIdx;
        --g.player.endCount;
        animSet(g, 9, 0x98);
        nextState(g);
    }
}

// 18cf:0398 PlrSt_54_EndRankWait
void plrSt54EndRankWait(DosGame& g) {
    if (slot(g, 9).anim == 0) nextState(g, -1);
}

// 18cf:03a4 PlrSt_55_EndCrown
void plrSt55EndCrown(DosGame& g) {
    if (slot(g, 8).anim != 0) return;
    if (g.player.endTreasureCount == 0) {
        animSet(g, 4, 0x91);  // the creature's eyes only
        nextState(g, 2);
        return;
    }
    animClear(g, 4);
    slot(g, 11).y = 0x1C;
    if (g.core.tier * 3 + 6 == g.player.endTreasureCount) {  // all treasures found
        g.player.endAllFound = 1;
        animSet(g, 11, 0x97);
        animSet(g, 10, 0x92);
    } else {
        animSet(g, 11, 0x94);
        g.player.endAllFound = 0;
        animSet(g, 10, 0x8A);
    }
    nextState(g);
}

// 18cf:0441 PlrSt_56_EndCreature
void plrSt56EndCreature(DosGame& g) {
    if (slot(g, 10).anim != 0) return;
    if (g.player.endAllFound == 0) {
        animSet(g, 4, 0x93);
    } else {
        setSlotPos(g, 4, 0x11, 0x34);
        animSet(g, 4, 0x95);
        setSlotPos(g, 3, 0x1F, 0x34);
        animSet(g, 3, 0x96);
    }
    nextState(g);
}

// 18cf:049b PlrSt_57_EndPlayer
void plrSt57EndPlayer(DosGame& g) {
    if (slot(g, 4).anim != 0) return;
    animClear(g, 4);
    animClear(g, 3);
    animSet(g, 6, 0x84);
    nextState(g);
}

// 18cf:04ca PlrSt_58_EndShowcaseCheck
void plrSt58EndShowcaseCheck(DosGame& g) {
    if (slot(g, 6).anim != 0) return;
    if (g.player.endTreasureCount == 0) {
        setSlotPos(g, 6, 2, 0x10);
        animSet(g, 6, 0x85);  // celebration
        nextState(g, 3);
    } else {
        animSet(g, 8, 0x8D);  // the open chest
        nextState(g);
    }
}

// 18cf:050e PlrSt_59_EndShowcaseStart
void plrSt59EndShowcaseStart(DosGame& g) {
    if (slot(g, 7).anim != 0) return;  // the horn's long idle: passes at once
    animScriptWordAt(g, dg::kAnimScript88Frame) = static_cast<int16_t>(g.player.showcasePick & 0x7F);
    setSlotPos(g, 9, 0x20, 0x6E);
    g.player.endPathIdx = 0;
    animSetIfIdle(g, 9, 0x88);
    nextState(g);
}

// 18cf:0546 PlrSt_60_EndShowcaseStep
void plrSt60EndShowcaseStep(DosGame& g) {
    if (slot(g, 9).anim != 0) return;
    const auto& path = g.tables.player.endShowcasePathY;  // -1 terminated
    const size_t i = g.player.endPathIdx;
    const int16_t y = i < path.size() ? path[i] : int16_t{-1};
    if (y < 0) {
        setSlotPos(g, 6, 2, 0x10);
        animClear(g, 9);
        animSet(g, 6, 0x85);
        nextState(g);
    } else {
        if (g.player.endPathIdx < 8) slot(g, 9).x = static_cast<int16_t>(slot(g, 9).x + 4);  // x moves on the first 8 only
        slot(g, 9).y = y;
        ++g.player.endPathIdx;
        animSetIfIdle(g, 9, 0x88);
    }
}

// 18cf:05b2 PlrSt_61_EndDone
void plrSt61EndDone(DosGame& g) {
    if (slot(g, 6).anim != 0) return;
    g.player.endSeqDone = 1;
    nextState(g);  // 62, a no-op: Cell_P64_EndScreen takes over
}

// 18cf:05c4 PlrSt_63_ChampSparkle
void plrSt63ChampSparkle(DosGame& g) {
    if (slot(g, 8).anim != 0) return;
    setSlotPos(g, 9, 2, 0x10);
    animSet(g, 9, 0x99);
    nextState(g);
}

// 18cf:05eb PlrSt_64_ChampChest
void plrSt64ChampChest(DosGame& g) {
    if (slot(g, 9).anim != 0) return;
    setSfxLoop(g, true);
    sfxPlay(g, 7);
    animSet(g, 8, 0x9A);
    animSet(g, 9, 0x9C);
    nextState(g);
}

// 18cf:0625 PlrSt_65_ChampArcs
void plrSt65ChampArcs(DosGame& g) {
    if (slot(g, 8).anim != 0) return;
    setSfxLoop(g, false);
    sfxStop(g);
    animClear(g, 4);
    setSlotPos(g, 3, 2, 0x10);
    slot(g, 11).y = 0x1C;
    animSet(g, 3, 0x9B);
    animSet(g, 11, 0x97);
    animSet(g, 10, 0x92);
    nextState(g);
}

// 18cf:0685 PlrSt_66_ChampScream
void plrSt66ChampScream(DosGame& g) {
    if (slot(g, 3).anim != 0) return;
    animClear(g, 3);
    animClear(g, 11);
    animSet(g, 10, 0x9D);
    sfxPlay(g, 9);
    nextState(g);
}

// 18cf:06be PlrSt_67_ChampCrownFly
void plrSt67ChampCrownFly(DosGame& g) {
    if (slot(g, 10).anim != 0) return;
    setSlotPos(g, 11, 0x18, 0x1B);
    animSet(g, 11, 0xA0);
    animSet(g, 10, 0x9E);
    nextState(g);
}

// 18cf:06f4 PlrSt_68_ChampCatch
void plrSt68ChampCatch(DosGame& g) {
    if (slot(g, 10).anim == 0) animClear(g, 10);
    if (slot(g, 11).anim != 0) return;
    animSet(g, 6, 0x9F);
    slot(g, 11).x = static_cast<int16_t>(slot(g, 11).x + 4);
    slot(g, 11).y = static_cast<int16_t>(slot(g, 11).y + 1);
    animSet(g, 11, 0xA1);
    nextState(g);
}

// 18cf:0738 PlrSt_69_ChampCrowned
void plrSt69ChampCrowned(DosGame& g) {
    if (slot(g, 6).anim != 0) return;
    sfxPlay(g, 6);
    slot(g, 6).x = static_cast<int16_t>(slot(g, 6).x - 0x37);
    slot(g, 6).y = static_cast<int16_t>(slot(g, 6).y + 2);
    animSet(g, 6, 0xA2);
    nextState(g);
    g.player.endCount = 5;
    g.player.endPathIdx = 0;
}

// 18cf:0773 PlrSt_70_ChampSparkle
void plrSt70ChampSparkle(DosGame& g) {
    if (slot(g, 6).anim != 0) return;
    if (g.player.endCount == 0) {
        --g.player.endCount;
        animClear(g, 10);
        setSlotPos(g, 6, 2, 0x10);
        animSet(g, 6, 0x85);
        setState(g, 61);
    } else {
        const size_t i = g.player.endPathIdx;
        const auto& xs = g.tables.player.endChampSparkleX;
        const auto& ys = g.tables.player.endChampSparkleY;
        setSlotPos(g, 10, i < xs.size() ? xs[i] : 0, i < ys.size() ? ys[i] : 0);
        ++g.player.endPathIdx;
        slot(g, 10).z = 8;
        --g.player.endCount;
        animSet(g, 10, 0x98);
        nextState(g);
    }
}

// 18cf:07ea PlrSt_71_ChampSparkleWait
void plrSt71ChampSparkleWait(DosGame& g) {
    if (slot(g, 10).anim == 0) nextState(g, -1);
}

}  // namespace tmport::dos
