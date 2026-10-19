// cells2.c (segment 176a), end part: leaving the castle top, the castle gate, the treasure room
// with its showcase displays and carousel, the rank screen cells and the end-of-game state
// (code_player.md §6.4-§7.3, with the wave-3 correction of the 0x08/0x36 cases).
#include "dos/game/endgame.h"

#include "dos/game/anim.h"
#include "dos/game/cells.h"
#include "dos/game/game.h"
#include "dos/game/sound.h"

namespace tmport::dos {

namespace {

constexpr int kCollectedCount = 48;
constexpr int16_t kDisplayScriptsFirst = 0x5E;  // slot 3 scripts >= 0x5E are the showcase displays

SpriteSlot& slot(DosGame& g, int s) { return g.player.slots[static_cast<size_t>(s)]; }
Obj& curObj(DosGame& g) { return objAt(g, g.world.curObj); }

// g_collected[i] (signed chars, 0x80 = empty); index 48 is record +0xBA, one past the list, which
// the unbounded scans of the original reach only with a full list.
uint8_t& collectedAt(DosGame& g, int i) {
    UserRecord& r = g.rec();
    return i >= 0 && i < kCollectedCount ? r.collected[static_cast<size_t>(i)] : r.unusedBA;
}

// The simple cases of the showcase switch: the treasure's own display script on slot 3 (SHACK.PAK).
struct DisplayCase {
    int16_t treasure;
    uint8_t script;
};
constexpr DisplayCase kDisplayCases[] = {
    {0x0A, 0xAC}, {0x0C, 0xA9}, {0x10, 0xAF}, {0x12, 0xB2}, {0x14, 0xB0}, {0x16, 0xB1}, {0x1A, 0x6A},
    {0x20, 0xA8}, {0x24, 0xA3}, {0x26, 0xAB}, {0x28, 0xAA}, {0x2A, 0xAE}, {0x2E, 0x76}, {0x30, 0xAD},
    {0x32, 0x5E}, {0x34, 0x5F}, {0x38, 0x61}, {0x3A, 0x62}, {0x3C, 0x63}, {0x3E, 0x64}, {0x40, 0x65},
    {0x42, 0x66}, {0x44, 0x67}, {0x46, 0x68}, {0x48, 0x69}, {0x52, 0x6B}, {0x54, 0x6C}, {0x56, 0x6D},
    {0x5C, 0x70}, {0x5E, 0x71}, {0x60, 0x72}, {0x62, 0x73}, {0x64, 0x74},
};

// The showcase switch of Cell_P70_TreasureRoom (53 values in the code at CS 176a:0654, targets at
// 176a:06BE): the 48 treasures plus the display codes 0x6A, 0x84, 0x86, 0x8A, 0xCC. Pairs and the
// trio move the partner treasures one byte per tick (Treasure_IsCollected sets g_shackFlags = 0x40,
// which re-triggers the case every tick) until the positions meet, then the combined display
// starts (Shack_ResetDisplay). Position compares are unsigned. Returns false for a value with no
// case.
bool runShowcaseCase(DosGame& g) {
    PlayerState& p = g.player;
    const auto display = [&g](int script) { animSetIfIdle(g, 3, script); };
    const int16_t shown = p.shownTreasure;
    for (const DisplayCase& c : kDisplayCases) {
        if (c.treasure != shown) continue;
        display(c.script);
        return true;
    }
    switch (shown) {
        case 0x06:
            if (!treasureIsCollected(g, 0x5A)) display(0xB4);
            else if (p.shackPartner1X < 0x1B) shackResetDisplay(g, 0x6F);
            else shackPair065a(g);
            return true;
        case 0x5A:
            if (!treasureIsCollected(g, 0x06)) display(0xBE);
            else if (p.shackPartner1X < 0x1B) shackResetDisplay(g, 0x6F);
            else shackPair065a(g);
            return true;
        case 0x08:
        case 0x8A:  // 0x8A: treasure 8 while the pair moves apart
            if (!treasureIsCollected(g, 0x36)) {
                display(0xB5);
            } else if ((p.shackX < 0x22 || p.shackPartner1X > 0x12) && p.shownTreasure == 8) {
                shackPair0836Approach(g, 1);
            } else if (p.shackX < 0x50) {
                shackPair0836Swap(g, 1);
            } else {
                shackResetDisplay(g, 0);
                p.shownTreasure = 8;
                slot(g, 3).prev = kDisplayScriptsFirst;  // display done: the automatic exit follows
            }
            return true;
        case 0x36:
        case 0x6A:  // 0x6A: treasure 0x36's alternate frame while the pair moves apart
            if (!treasureIsCollected(g, 0x08)) {
                display(0x60);
            } else if ((p.shackX < 0x13 && p.shackPartner1X < 0x23) || p.shackPartner1 == 0x8A) {
                if (p.shackPartner1X < 0x50) {
                    shackPair0836Swap(g, 0);
                } else {
                    shackResetDisplay(g, 0);
                    p.shownTreasure = 0x36;
                    slot(g, 3).prev = kDisplayScriptsFirst;
                }
            } else {
                shackPair0836Approach(g, 0);
            }
            return true;
        case 0x0E:
            if (!treasureIsCollected(g, 0x2C)) display(0xB6);
            else if (p.shackX < 0x20 || p.shackPartner1X > 0x12) shackPair0e2c(g, 1);
            else shackResetDisplay(g, 0xB3);
            return true;
        case 0x2C:
            if (!treasureIsCollected(g, 0x0E)) display(0xBA);
            else if (p.shackX < 0x13 && p.shackPartner1X < 0x21) shackResetDisplay(g, 0xB3);
            else shackPair0e2c(g, 0);
            return true;
        case 0x18:
            if (!treasureIsCollected(g, 0x58)) display(0xB7);
            else if (p.shackX < 0x13 && p.shackPartner1X < 0x21) shackResetDisplay(g, 0x75);
            else shackPair1858(g, 0);
            return true;
        case 0x58:
            if (!treasureIsCollected(g, 0x18)) display(0x6E);
            else if (p.shackX < 0x20 || p.shackPartner1X > 0x12) shackPair1858(g, 1);
            else shackResetDisplay(g, 0x75);
            return true;
        case 0x1C:
            if (!treasureIsCollected(g, 0x22)) display(0xB8);
            else if (p.shackX < 0x14 && p.shackPartner1X < 0x23) shackResetDisplay(g, 0xA4);
            else shackPair1c22(g, 0);
            return true;
        case 0x22:
            if (!treasureIsCollected(g, 0x1C)) display(0xB9);
            else if (p.shackX < 0x22 || p.shackPartner1X > 0x13) shackPair1c22(g, 1);
            else shackResetDisplay(g, 0xA4);
            return true;
        // The trio: both partners must be collected (short-circuit: the second test runs only if
        // the first was true; a failed second test clears the 0x40 the first one set).
        case 0x4A:
            if (!treasureIsCollected(g, 0x4C) || !treasureIsCollected(g, 0x4E)) {
                display(0xBB);
                p.shackFlags &= 0xBF;
            } else if (p.shackX < 0x0D && p.shackPartner1X < 0x1B && p.shackPartner2X < 0x27) {
                shackResetDisplay(g, 0x77);
            } else {
                shackTrio4a4c4e(g, 0);
            }
            return true;
        case 0x4C:
            if (!treasureIsCollected(g, 0x4A) || !treasureIsCollected(g, 0x4E)) {
                display(0xBC);
                p.shackFlags &= 0xBF;
            } else if (p.shackX < 0x1A || p.shackPartner1X > 0x0C || p.shackPartner2X > 0x26) {
                shackTrio4a4c4e(g, 1);
            } else {
                shackResetDisplay(g, 0x77);
            }
            return true;
        case 0x4E:
            if (!treasureIsCollected(g, 0x4A) || !treasureIsCollected(g, 0x4C)) {
                display(0xBD);
                p.shackFlags &= 0xBF;
            } else if (p.shackX < 0x26 || p.shackPartner1X > 0x0C || p.shackPartner2X > 0x1A) {
                shackTrio4a4c4e(g, 2);
            } else {
                shackResetDisplay(g, 0x77);
            }
            return true;
        case 0x1E:
        case 0xCC:
            shackFly1e(g);
            return true;
        case 0x50:
        case 0x84:
        case 0x86:
            shackFly50(g);
            return true;
        default:
            return false;
    }
}

// The carousel swap of Cell_P70 (176a:0d..): the collected list works as a ring with one gap (the
// first 0x80 entry); Right brings the entry after the gap onto the pedestal, Left the one before
// it, and the old pedestal treasure goes into the gap.
void carouselSwap(DosGame& g) {
    PlayerState& p = g.player;
    int gap = 0;
    while (gap < kCollectedCount && collectedAt(g, gap) != 0x80) ++gap;
    int next = 0;
    if (p.shackFlags & 0x80) {
        next = (collectedAt(g, gap + 1) == 0x80 || gap == 0x2F) ? 0 : gap + 1;
    } else if (gap == 0) {
        next = 0x2F;
        while (next > 0 && collectedAt(g, next) == 0x80) --next;
    } else {
        next = gap - 1;
    }
    p.shackPartner1 = -1;
    p.shackPartner2 = -1;  // 0xFF
    p.shackPartner1X = 0x50;
    p.shackPartner2X = 0x50;
    const int16_t old = p.shownTreasure;
    p.shownTreasure = static_cast<int8_t>(collectedAt(g, next));
    collectedAt(g, next) = 0x80;
    collectedAt(g, gap) = static_cast<uint8_t>(old);
    p.shackFlags = 2;
    sfxPlay(g, 7);
}

// The partner of a pair stepper, chosen once (partner 1 < 0) by the shown treasure.
void pickPartner(DosGame& g, int16_t ifShown, int16_t thenPartner, int16_t elsePartner) {
    PlayerState& p = g.player;
    if (p.shackPartner1 < 0) p.shackPartner1 = p.shownTreasure == ifShown ? thenPartner : elsePartner;
}

// One approach step of a pair: dir 0 moves the partner to `p0` and the pedestal treasure left to
// `x0`; dir 1 moves the partner to `p1` and the pedestal treasure right to `x1` (unsigned).
void pairStep(DosGame& g, int dir, uint16_t p0, uint16_t x0, uint16_t p1, uint16_t x1) {
    PlayerState& p = g.player;
    if (dir == 0) {
        if (p.shackPartner1X > p0) --p.shackPartner1X;
        if (p.shackX > x0) --p.shackX;
    } else {
        if (p.shackPartner1X > p1) --p.shackPartner1X;
        if (p.shackX < x1) ++p.shackX;
    }
}

// ShackFly_1e / ShackFly_50: the treasure rises to the top (shown as `riseCode`), comes back as
// `topCode` and lands as `landCode`; the shown treasure is saved meanwhile.
void shackFly(DosGame& g, int16_t riseCode, int16_t topCode, int16_t landCode) {
    PlayerState& p = g.player;
    if ((p.shackFlags & 0x80) == 0) {  // start
        p.shackSavedShown = p.shownTreasure;
        p.shackFlags |= 0xC0;
        if (riseCode >= 0) p.shownTreasure = riseCode;
        p.shackY = 0x43;
    } else if ((p.shackFlags & 0x10) == 0 && p.shackY > 0) {  // rise
        p.shackY = static_cast<int16_t>(p.shackY - 2);
    } else if (p.shackY <= 0) {
        p.shownTreasure = topCode;
        p.shackFlags |= 0x10;
        p.shackY = 2;
    } else if (p.shackY == 0x44) {  // back down
        p.shackFlags &= 0x3F;
        p.shownTreasure = landCode;
        p.shackSavedShown = -1;
        slot(g, 3).prev = kDisplayScriptsFirst;
    } else {  // descend
        ++p.shackY;
    }
}

}  // namespace

// 176a:0404 Cell_P64_EndScreen
uint16_t cellP64EndScreen(DosGame& g) {
    WorldState& w = g.world;
    w.cellMask = 0;
    // The end sequence is done, or any command key or click skips the rest of it: the total was
    // added at the castle top and the showcase is handed over on the chute, so only the pictures
    // are skipped.
    if (g.player.endSeqDone != 0 || g.input.cmd != -1) {
        for (int s = 3; s < 12; ++s) animClear(g, s);
        setSfxLoop(g, false);
        sfxStop(g);
        w.cellMask = 0x14F0;
        Obj& o = curObj(g);
        o.dy = 0;
        o.x = 0x6A4;  // the top of the chute, row 3 column 13
        o.y = 0x1F0;
        w.transition = 2;
        w.cameraX = 0x600;
        w.fullRedrawReq = 1;
        animSetIfIdle(g, 1, 0x5D);
        g.player.playerState = 34;
    }
    return w.cellMask;
}

// 176a:0485 Cell_P20_CastleGate
uint16_t cellP20CastleGate(DosGame& g) {
    WorldState& w = g.world;
    Obj& o = curObj(g);
    if ((o.y & 0x7F) != 0x70) {
        w.cellMask = 0x14F0;  // keep falling
        return w.cellMask;
    }
    o.dy = 0;
    g.rec().savedGame = 0;  // record +9: no saved game any more
    g.core.endState = 1;
    g.core.menuLock = 1;
    if (static_cast<uint16_t>(g.rec().totalTreasures) < 300) {  // the treasure room
        o.x = 0x4F0;
        o.y = 0x4E0;
        g.core.level = 4;
        g.player.playerState = 35;
        w.fullRedrawReq = 1;
        w.transition = 1;
        animInitSlots(g);
    } else {  // champion: the celebration in the clubhouse start cell
        o.x = 0x170;
        o.y = 0x70;
        g.core.level = 0;
        w.fullRedrawReq = 1;
        w.transition = 1;
        animInitSlots(g);
        g.player.playerState = 9;
        g.ui.hudRedrawReq2 = 1;
        animSetIfIdle(g, 1, 0xA5);
        slot(g, 6).x = 0x52;
        slot(g, 6).y = 0x84;
        slot(g, 6).z = 10;
        animSetIfIdle(g, 6, 0xA6);
        slot(g, 8).x = 0x62;
        slot(g, 8).y = 0x86;
        slot(g, 8).z = 10;
        animSetIfIdle(g, 8, 0xA7);
    }
    w.cellMask = 0;
    return w.cellMask;
}

// 176a:0596 Cell_P70_TreasureRoom
uint16_t cellP70TreasureRoom(DosGame& g) {
    WorldState& w = g.world;
    PlayerState& p = g.player;
    Obj& o = curObj(g);  // the player (the elves never reach scene 4)
    uint16_t& keys = g.keys();
    w.cellMask = 0x104F;
    if ((o.y & 0x7F) == 0x70) {
        w.cellMask = 0;
        if (o.dy > 0) o.dy = 0;
    }
    if (g.core.endState == 1) keys = 0;  // no keys during the end-of-game visit
    if (keys & 1) p.shackStay = 1;       // Up: stay (no automatic move to the rank screen)
    SpriteSlot& s1 = slot(g, 1);
    SpriteSlot& s3 = slot(g, 3);
    // The display: after the landing script, on Up, or while a pair moves.
    if (((s1.prev == 0x52 && static_cast<int16_t>(s3.prev) < kDisplayScriptsFirst) || (keys & 1) || (p.shackFlags & 0x40)) &&
        (p.shackState & 0x7F) == 0) {
        if (!sfxBlockActive(g) && (p.shackState & 0x80) == 0) sfxPlay(g, 2);
        p.shackState |= 0x80;
        s1.prev = 0;
        keys &= 0xFFFE;
        // Quirk kept: g_shackNoCase is never cleared, so after one shown value without a case
        // (e.g. 0xFFFF of a new player) every later end-of-game visit leaves at once.
        if (!runShowcaseCase(g)) p.shackNoCase = 1;
    }
    // The automatic exit to the rank screen once the display script ended.
    if (p.shackStay == 0 && (static_cast<int16_t>(s3.prev) >= kDisplayScriptsFirst || p.shackNoCase == 1)) {
        cellP71V71ToRankScreen(g);
        p.shackState &= 0x7F;
    }
    // Leaving (Down, the coin key or any command) outside the end-of-game visit. The original pushes
    // no argument for Shack_ResetDisplay here; whatever it reads only starts a script on slot 3,
    // which TreasureRoom_Leave clears at once.
    if (((keys & 0x42) || g.input.cmd != -1) && g.core.endState == 0) {
        w.cellMask = 0;
        shackResetDisplay(g, 0);
        treasureRoomLeave(g);
    }
    // The carousel through the collected list (Left/Right on the floor, a treasure on the shelf).
    if (((keys & 8) || (keys & 4)) && p.obj.y == 0x4E0 && (p.shackFlags & 0xBF) == 0 &&
        (static_cast<int8_t>(collectedAt(g, 0)) > 0 || static_cast<int8_t>(collectedAt(g, 1)) > 0) && p.shackState == 0 &&
        static_cast<int16_t>(s3.anim) < kDisplayScriptsFirst) {
        p.shackState = 1;
        p.shackFlags |= (keys & 8) ? 0x81 : 0x01;
    }
    if (p.shackX < 0x50 && (p.shackFlags & 0x0F) == 1) {  // slide out to the right
        p.shackX = static_cast<uint16_t>(p.shackX + 2);
        p.shackPartner1X = static_cast<uint16_t>(p.shackPartner1X + 2);
        p.shackPartner2X = static_cast<uint16_t>(p.shackPartner2X + 2);
    } else if (p.shackX == 0x50 && (p.shackFlags & 0x0F) == 1) {
        carouselSwap(g);
    } else if (p.shackX > 0x1A && p.shackFlags == 2) {  // slide back in
        p.shackX = static_cast<uint16_t>(p.shackX - 2);
    } else if (p.shackX == 0x1A && p.shownTreasure < 0x80 && p.shackY > 0x43) {  // carousel finished
        p.shackState = 0;
        p.shackFlags &= 0x40;
    }
    return w.cellMask;
}

// 176a:0f47 TreasureRoom_Leave
void treasureRoomLeave(DosGame& g) {
    Obj& o = curObj(g);
    o.x = 0x170;
    o.y = 0x70;
    o.dy = 0;
    g.core.level = 0;
    g.core.menuLock = 0;
    g.player.playerState = 9;
    animInitSlots(g);
    animSet(g, 1, 6);
    g.world.fullRedrawReq = 1;
    g.world.transition = 1;
    g.player.shackStay = 0;
    g.player.shackFlags = 0;
    gameAdvanceEnd(g);
}

// 176a:0fa6 Cell_P71_V71_ToRankScreen
uint16_t cellP71V71ToRankScreen(DosGame& g) {
    WorldState& w = g.world;
    w.cellMask = 0xDF;
    if (w.cellCallKind != 2) {  // a visible call: the rock edge decoration
        cellV7(g);
        return w.cellMask;
    }
    Obj& o = curObj(g);
    if ((o.x & 0x78) < 0x44) {  // the left wall
        if (o.dx < 0) o.dx = 0;
        w.cellMask = 0xDB;
    }
    if ((o.y & 0x7F) < 0x6C) {
        w.cellMask &= 0xF2;
        if ((o.y & 0x7F) == 0x60) {  // enter the rank screen (Cell_P70 and Cell_P12 arrive here at once)
            o.x = 0x680;
            o.y = 0x80;
            w.cameraX = 0x680;
            g.core.level = 4;
            g.player.rankState = 1;
            g.core.menuLock = 1;
            g.player.playerState = 1;
            animClear(g, 1);
            animClear(g, 3);
            w.fullRedrawReq = 1;
            w.transition = 2;
        }
    }
    if ((o.y & 0x78) > 0x6F) {
        if (o.dy > 0) o.dy = 0;
        w.cellMask &= 0xFD;
    }
    return w.cellMask;
}

// 176a:1094 Cell_P72_RankScreen
uint16_t cellP72RankScreen(DosGame& g) {
    WorldState& w = g.world;
    w.cellMask = 0x104F;
    // Back to the clubhouse on a mouse button change, Enter (8) or Esc (0), or Down / the coin key.
    if (g.mouse.buttons != g.mouse.prevButtons || g.input.cmd == 8 || g.input.cmd == 0 || (g.keys() & 0x42)) {
        g.player.rankState = 0;
        Obj& o = curObj(g);
        o.x = 0xF0;
        o.y = 0x70;
        w.cameraX = 0xC0;
        g.core.level = 0;
        g.core.menuLock = 0;
        g.player.playerState = 9;
        w.fullRedrawReq = 1;
        w.transition = 2;
        w.cellMask = 3;
        animSet(g, 1, 6);
        gameAdvanceEnd(g);
        g.player.champFlashTimer = 0;
    }
    return w.cellMask;
}

// 176a:1119 Game_AdvanceEnd
void gameAdvanceEnd(DosGame& g) {
    if (g.core.endState == 1) g.core.endState = 2;  // Scene_Update then ends the game (Game_End)
    else animSet(g, 1, 6);
}

// 176a:1136 Treasure_IsCollected
int treasureIsCollected(DosGame& g, int id) {
    for (int i = 0; i < kCollectedCount; ++i) {
        if (static_cast<int8_t>(collectedAt(g, i)) != id) continue;
        g.player.shackFlags = 0x40;  // an assignment: the pair display runs every tick
        return 1;
    }
    return 0;
}

// 176a:1162 ShackPair_08_36_Approach
void shackPair0836Approach(DosGame& g, int dir) {
    pickPartner(g, 0x08, 0x36, 0x08);
    pairStep(g, dir, 0x22, 0x12, 0x12, 0x22);
}

// 176a:11b7 ShackPair_08_36_Swap
void shackPair0836Swap(DosGame& g, int dir) {
    PlayerState& p = g.player;
    if (dir == 0) {
        if (p.shackPartner1 == 0x08) p.shackPartner1 = 0x8A;
        if (p.shackPartner1X < 0x50) {
            ++p.shackPartner1X;
            p.shownTreasure = (p.shackPartner1X & 1) == 0 ? 0x36 : 0x6A;  // alternating frames
        }
    } else {
        if (p.shownTreasure == 0x08) p.shownTreasure = 0x8A;
        if (p.shackX < 0x50) {
            ++p.shackX;
            p.shackPartner1 = (p.shackX & 1) == 0 ? 0x36 : 0x6A;
        }
    }
}

// 176a:1220 ShackPair_0e_2c
void shackPair0e2c(DosGame& g, int dir) {
    pickPartner(g, 0x0E, 0x2C, 0x0E);
    pairStep(g, dir, 0x20, 0x12, 0x12, 0x20);
}

// 176a:1275 ShackPair_06_5a
void shackPair065a(DosGame& g) {
    pickPartner(g, 0x06, 0x5A, 0x06);
    if (g.player.shackPartner1X > 0x1A) --g.player.shackPartner1X;  // the direction argument is ignored
}

// 176a:129d ShackPair_1c_22
void shackPair1c22(DosGame& g, int dir) {
    pickPartner(g, 0x1C, 0x22, 0x1C);
    pairStep(g, dir, 0x22, 0x13, 0x13, 0x22);
}

// 176a:12f2 ShackPair_18_58
void shackPair1858(DosGame& g, int dir) {
    pickPartner(g, 0x58, 0x18, 0x58);
    pairStep(g, dir, 0x20, 0x12, 0x12, 0x20);
}

// 176a:1347 ShackFly_1e
void shackFly1e(DosGame& g) {
    const uint8_t f = g.player.shackFlags;
    if (((f & 0x80) == 0 || (f & 0x40) != 0) && (f & 1) == 0) shackFly(g, -1, 0xCC, 0x1E);
}

// 176a:13d7 ShackFly_50
void shackFly50(DosGame& g) {
    const uint8_t f = g.player.shackFlags;
    if ((f & 0x80) == 0 || (f & 0x40) != 0) shackFly(g, 0x84, 0x86, 0x50);
}

// 176a:1463 ShackTrio_4a_4c_4e
void shackTrio4a4c4e(DosGame& g, int which) {
    PlayerState& p = g.player;
    if (p.shackPartner1 < 0) {
        if (p.shownTreasure == 0x4A) {
            p.shackPartner1 = 0x4C;
            p.shackPartner2 = 0x4E;
        } else if (p.shownTreasure == 0x4C) {
            p.shackPartner1 = 0x4A;
            p.shackPartner2 = 0x4E;
        } else {
            p.shackPartner1 = 0x4A;
            p.shackPartner2 = 0x4C;
        }
    }
    // which: the trio member on the pedestal moves to x 0x0C, 0x1A or 0x26; the partners close up.
    switch (which) {
        case 0:
            if (p.shackX > 0x0C) --p.shackX;
            break;
        case 1:
            if (p.shackX < 0x1A) ++p.shackX;
            break;
        case 2:
            if (p.shackX < 0x26) ++p.shackX;
            break;
        default:
            return;
    }
    const uint16_t p1Limit = which == 0 ? 0x1A : 0x0C;
    const uint16_t p2Limit = which == 2 ? 0x1A : 0x26;
    if (p.shackPartner1X > p1Limit) --p.shackPartner1X;
    if (p.shackPartner2X > p2Limit && p.shackPartner1X < 0x36) --p.shackPartner2X;
}

// 176a:1531 Shack_ResetDisplay
void shackResetDisplay(DosGame& g, int anim) {
    PlayerState& p = g.player;
    if (p.shackSavedShown > 0) {
        p.shownTreasure = p.shackSavedShown;
        p.shackSavedShown = -1;
    }
    p.shackPartner1 = -1;
    p.shackPartner2 = -1;  // 0xFF
    p.shackPartner1X = 0x50;
    p.shackPartner2X = 0x50;
    p.shackX = 0x1A;
    p.shackY = 0x44;
    p.shackFlags = static_cast<uint8_t>((p.shackFlags & 0xBF) | 0x20);  // the display script owns slot 3
    animClear(g, 8);
    animClear(g, 10);
    if (anim != 0) animSetIfIdle(g, 3, anim);
}

}  // namespace tmport::dos
