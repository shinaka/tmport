// elves.c (segment 4521): the elves of the three mountain levels: spawn, the per-tick update, the
// sprite-slot allocation at the view edges, the net classification and the 76-entry state machine
// (code_elves_layout_hints.md §3-§4). Every handler works on the record g_curObj points at; the
// module globals (class, call counter, g_elfAnimSel, ...) persist in ElfState because handlers read
// values that other handlers left (ElfSt_03 reads a stale g_elfAnimSel, ElfSt_25 relies on class 2
// meaning "got a slot this tick").
#include "dos/game/elves.h"

#include <stdexcept>

#include "dos/game/anim.h"
#include "dos/game/cells.h"
#include "dos/game/game.h"
#include "dos/game/player.h"

namespace tmport::dos {

namespace {

// Base states (type and facing, record +0x08).
constexpr uint16_t kBaseALeft = 1, kBaseARight = 5;    // walker / clue carrier
constexpr uint16_t kBaseBLeft = 9, kBaseBRight = 13;   // dust thrower
constexpr uint16_t kBaseCLeft = 17, kBaseCRight = 21;  // plain walker
constexpr uint16_t kBaseCoin = 25;                     // coin creature

// States with a sprite (>= kStVisible), indices of g_elfStateHandlers DS:5D6C.
constexpr uint16_t kStVisible = 0x2D;
constexpr uint16_t kStAWalkLeft = 45, kStAWalkRight = 46, kStAPause = 47;
constexpr uint16_t kStBWalkLeft = 49, kStBWalkRight = 50, kStBPause = 51;
constexpr uint16_t kStThrowLeft = 52, kStThrowRight = 56;  // wind-up; +1 dust out, +2 dust back, +3 end
constexpr uint16_t kStCWalkLeft = 60, kStCWalkRight = 61;
constexpr uint16_t kStPulledRight = 62, kStDodge = 64, kStPulledLeft = 65;  // +1: held in the net
constexpr uint16_t kStAfterQuiz = 67, kStFrozen = 68, kStZip = 69;
constexpr uint16_t kStCoinLying = 70, kStCoinRunning = 71, kStCoinCollected = 72;
constexpr uint16_t kStEscapeJump = 73;

// Animation scripts (DS:4F5A ids; §3.5). Carrier variants: walk + kSelCarrierWalk, net scripts +
// kSelCarrierNet (the g_elfAnimSel values).
constexpr int kAnimAWalkLeft = 0x1F, kAnimAWalkRight = 0x20, kAnimPause = 0x21, kAnimStand = 0x22;
constexpr int kAnimBWalkLeft = 0x23, kAnimBWalkRight = 0x24;
constexpr int kAnimWindUpLeft = 0x25, kAnimWindUpRight = 0x26, kAnimDustLeft = 0x27, kAnimDustRight = 0x28;
constexpr int kAnimDustHitLeft = 0x29, kAnimDustHitRight = 0x2A, kAnimCoinBackLeft = 0x2B, kAnimCoinBackRight = 0x2C;
constexpr int kAnimCWalkLeft = 0x2D, kAnimCWalkRight = 0x2E;
constexpr int kAnimPulledRight = 0x2F, kAnimPulledLeft = 0x30, kAnimHeldLeft = 0x31, kAnimHeldRight = 0x32;
constexpr int kAnimHopLeft = 0x33, kAnimHopRight = 0x34, kAnimGetUp = 0x35;
constexpr int kAnimCarrierWalkLeft = 0x41, kAnimCarrierWalkRight = 0x42, kAnimCarrierPause = 0x43;
constexpr int kAnimCarrierHeldLeft = 0x47, kAnimCarrierHeldRight = 0x48;
constexpr int kAnimZipLeft = 0x4D, kAnimZipRight = 0x4E, kAnimEscapeJump = 0x4F, kAnimEscapeSprite = 0x50;
constexpr int kAnimCoinLying = 0x56, kAnimCoinRunRight = 0x57, kAnimCoinRunLeft = 0x58, kAnimCoinCollected = 0x59;
constexpr uint16_t kSelCarrierWalk = 0x22;  // 0x1F + 0x22 = 0x41 (ElfSt_02 / 04)
constexpr uint16_t kSelCarrierNet = 0x16;   // 0x33 + 0x16 = 0x49 (Elf_CheckNet, ElfSt_63)

// g_playerState values the elves test (code_player.md §4.1).
constexpr uint16_t kPlrNetLeft = 21, kPlrNetRight = 24;  // the net-down states (catch window)

// Net classes (g_elfNetClass, Elf_Think).
constexpr uint16_t kClassReleased = 1, kClassNewSprite = 2, kClassVisible = 3, kClassNetEdge = 4, kClassNet = 5,
                   kClassNetCentre = 6;

constexpr int kFirstElfSlot = 6, kElfSlotEnd = 12;  // slots 6, 8, 10 (children 7, 9, 11)
constexpr uint16_t kNoFrame = 0xFFFF;

int16_t s16(int v) { return static_cast<int16_t>(v); }

// The record g_curObj points at (handlers run only on elves).
Elf& curElf(DosGame& g) {
    const int i = elfIndexOf(g.world.curObj);
    if (i < 0) throw std::logic_error("elf handler called with g_curObj = the player");
    return g.elves.elves[static_cast<size_t>(i)];
}

bool isCarrier(const Elf& e) { return e.question != 0; }
int slotIndex(const Elf& e) { return e.spriteSlot; }
SpriteSlot& slotOf(DosGame& g, const Elf& e) { return g.player.slots.at(e.spriteSlot); }
SpriteSlot& childOf(DosGame& g, const Elf& e) { return g.player.slots.at(static_cast<size_t>(e.spriteSlot) + 1); }
bool slotIdle(DosGame& g, const Elf& e) { return slotOf(g, e).anim == 0; }

// The random spawn position of Elves_Spawn and ElfSt_72: one rand() in the level's x range
// (g_levelXRange DS:5D54 {x base, y, x mask}); past the row end it continues on the next row.
void placeInLevelRange(DosGame& g, Elf& e) {
    const auto& r = g.tables.elves.levelXRange.at(g.core.level);
    int x = (g.rand.rand() & r[2]) + r[0];
    int y = r[1];
    if (x > 0x7FF) {
        x -= 0x800;
        y += 0x80;
    }
    e.obj.x = static_cast<uint16_t>(x);
    e.obj.y = static_cast<uint16_t>(y);
}

// The "hidden" handlers of types A, B and C: drift one pixel per tick away from the start facing
// (only on calls where bit 1 of the call counter is clear); a new sprite starts the walk script.
void hiddenWalk(DosGame& g, int16_t drift, int anim, uint16_t walkState) {
    Elf& e = curElf(g);
    if (g.elves.elfNetClass < kClassNewSprite) {
        if ((g.elves.elfCallCounter & 2) == 0) e.obj.dx = drift;
    } else {
        animSetIfIdle(g, slotIndex(e), anim);
        e.state = walkState;
    }
}

// The "resume" handlers (called as handler[base + 1] when a dodge or get-up script ends): depth back
// by 10, walk away from the player.
void resumeAway(DosGame& g, uint16_t baseRight, int animRight, uint16_t stateRight, uint16_t baseLeft, int animLeft,
                uint16_t stateLeft) {
    Elf& e = curElf(g);
    slotOf(g, e).z = s16(slotOf(g, e).z - 10);
    if (g.elves.playerRelX < g.elves.elfRelX) {
        e.base = baseRight;
        animSet(g, slotIndex(e), animRight);
        e.state = stateRight;
    } else {
        e.base = baseLeft;
        animSet(g, slotIndex(e), animLeft);
        e.state = stateLeft;
    }
}

// The "turn" handlers (handler[base + 3], Elf_Turn): visible elves flip their facing; hidden elves
// ignore turns.
void turnAround(DosGame& g, uint16_t baseRight, uint16_t baseLeft, int animLeft, uint16_t stateLeft, int animRight,
                uint16_t stateRight) {
    Elf& e = curElf(g);
    if (e.state < kStVisible) return;
    if (e.base == baseRight) {
        e.base = baseLeft;
        animSet(g, slotIndex(e), animLeft);
        e.state = stateLeft;
    } else {
        e.base = baseRight;
        animSet(g, slotIndex(e), animRight);
        e.state = stateRight;
    }
}

// ElfSt_49 / ElfSt_50: a thrower walking; once per finished walk cycle: throw at a player far enough
// ahead (`throwAt`), or pause.
void throwerWalk(DosGame& g, bool throwAt, int windUp, uint16_t throwState, int walk) {
    elfCheckNet(g);
    Elf& e = curElf(g);
    if (slotIdle(g, e)) {
        if ((g.rand.rand() & 1) == 0) {
            if (throwAt) {
                animSetIfIdle(g, slotIndex(e), windUp);
                e.state = throwState;
            } else {
                animSetIfIdle(g, slotIndex(e), kAnimPause);
                e.state = kStBPause;
            }
        } else if ((g.rand.rand() & 0x0F) == 0) {
            animSetIfIdle(g, slotIndex(e), kAnimPause);
            e.state = kStBPause;
        }
    }
    animSetIfIdle(g, slotIndex(e), walk);
}

// ElfSt_52 / ElfSt_56: when the wind-up ends the dust script starts with the dust (child slot) at
// the thrower; state + 1.
void throwStart(DosGame& g, int dust) {
    elfCheckNet(g);
    Elf& e = curElf(g);
    e.obj.dx = 0;
    if (slotIdle(g, e)) {
        animSetIfIdle(g, slotIndex(e), dust);
        childOf(g, e).x = 0;
        childOf(g, e).y = 0;
        e.state = static_cast<uint16_t>(e.state + 1);
    }
}

// ElfSt_53 / ElfSt_57: the dust flies one byte per tick (`step`); a hit within 10 bytes of the
// player takes a coin. `avoidLast` is the last jump state that avoids it: 20 for dust thrown to the
// left (all jumps), 18 for dust thrown to the right (only jumping left avoids it: 4521:0b06 quirk).
// The hit test runs even when Elf_CheckNet has just changed the state, so a netted thrower whose
// dust hits in the same tick moves one state further (state + 1, kept as in the original).
void dustFlight(DosGame& g, int step, uint16_t avoidLast, int hitAnim, int16_t vanishX, int walkOn, uint16_t walkBase,
                uint16_t walkState, int dust) {
    elfCheckNet(g);
    Elf& e = curElf(g);
    SpriteSlot& child = childOf(g, e);
    const int16_t d = s16(child.x + g.elves.elfRelX);
    e.obj.dx = 0;
    const int16_t p = g.elves.playerRelX;
    const uint16_t ps = g.player.playerState;
    if (s16(p - 10) < d && d < s16(p + 10) && g.core.coins != 0 && (g.player.obj.y & 0x78) == 0x70 &&
        (ps < 17 || ps > avoidLast)) {
        g.core.coins = static_cast<uint16_t>(g.core.coins - 1);
        e.state = static_cast<uint16_t>(e.state + 1);
        animSet(g, slotIndex(e), hitAnim);
    } else if (child.x == vanishX) {
        animSet(g, slotIndex(e), walkOn);
        childOf(g, e).frame = kNoFrame;
        e.base = walkBase;
        e.state = walkState;
    } else {
        animSetIfIdle(g, slotIndex(e), dust);
        childOf(g, e).x = s16(childOf(g, e).x + step);
    }
}

// ElfSt_54 / ElfSt_58: the dust carries the coin back one byte per tick; at the thrower: state + 1
// and the coin pose.
void dustBack(DosGame& g, int step, int coinAnim) {
    Elf& e = curElf(g);
    e.obj.dx = 0;
    SpriteSlot& child = childOf(g, e);
    if (child.x == 0) {
        e.state = static_cast<uint16_t>(e.state + 1);
        animSet(g, slotIndex(e), coinAnim);
    } else {
        child.x = s16(child.x + step);
    }
}

// ElfSt_55 / ElfSt_59: when the coin pose ends the thrower walks off (child hidden).
void throwEnd(DosGame& g, int walk, uint16_t base, uint16_t state, bool hideFirst) {
    Elf& e = curElf(g);
    e.obj.dx = 0;
    if (!slotIdle(g, e)) return;
    if (hideFirst) childOf(g, e).frame = kNoFrame;
    animSet(g, slotIndex(e), walk);
    if (!hideFirst) childOf(g, e).frame = kNoFrame;
    e.base = base;
    e.state = state;
}

// ElfSt_62 / ElfSt_65: being pulled to the net centre; the held pose when the pull script ends
// (state + 1); in the centre zone the pull stops.
void pulled(DosGame& g, int heldCarrier, int held) {
    Elf& e = curElf(g);
    if (slotIdle(g, e)) {
        g.elves.elfAnimSel = isCarrier(e) ? static_cast<uint16_t>(heldCarrier) : static_cast<uint16_t>(held);
        animSet(g, slotIndex(e), g.elves.elfAnimSel);
        e.state = static_cast<uint16_t>(e.state + 1);
    }
    if (g.elves.elfNetClass == kClassNetCentre) curElf(g).obj.dx = 0;
}

}  // namespace

// 4521:182e Elves_Spawn
void elvesSpawn(DosGame& g, int carriers) {
    ElfState& es = g.elves;
    es.elfActiveCount = static_cast<uint16_t>(g.core.slowCpu == 1 ? 7 : kElfCount);
    // Base, state and question go; x, y, dx and the sprite slot stay (§4.1).
    for (int i = 0; i < kElfCount; ++i) {
        g.world.curObj = DsPtr{elfObjOff(i)};
        Elf& e = es.elves[static_cast<size_t>(i)];
        e.base = 0;
        e.state = 0;
        e.question = 0;
    }
    const auto& counts = g.tables.elves.elfCounts.at(g.core.tier);  // {A, B, C, D}
    int n = 0;
    auto spawn = [&](uint16_t base, uint16_t question) {
        if (n >= kElfCount) return;  // the table never asks for more than 15
        g.world.curObj = DsPtr{elfObjOff(n)};
        Elf& e = es.elves[static_cast<size_t>(n)];
        placeInLevelRange(g, e);
        e.base = base;
        e.state = base;
        e.question = question;
        ++n;
    };
    // rand() order: one per elf, types A, D, B, C (§4.1 step 4).
    for (unsigned i = 0; i < counts[0]; ++i)
        spawn((i & 1) ? kBaseARight : kBaseALeft, static_cast<uint16_t>(static_cast<int>(i) < carriers ? i + 1 : 0));
    for (unsigned i = 0; i < counts[3]; ++i) spawn(kBaseCoin, 0);
    for (unsigned i = 0; i < counts[1]; ++i) spawn((i & 1) ? kBaseBRight : kBaseBLeft, 0);
    for (unsigned i = 0; i < counts[2]; ++i) spawn((i & 1) ? kBaseCRight : kBaseCLeft, 0);
    // The original leaves g_curObj one record past the last spawned elf; with all 15 spawned that is
    // DS:39DA, past the table, which the engine cannot point at: it keeps record 14 then (the next
    // tick's Player_Update / Elves_Update overwrite the pointer before anything reads it).
    g.world.curObj = DsPtr{elfObjOff(n < kElfCount ? n : kElfCount - 1)};
}

// 4521:0ead Elves_Update
void elvesUpdate(DosGame& g) {
    ElfState& es = g.elves;
    if (g.core.uiMode != 0 || g.core.slowCpu != g.world.slowCpuSaved) return;
    const uint16_t cam = static_cast<uint16_t>((g.world.cameraY & 0xFF80) * 16 + g.world.cameraX);
    es.elfSpriteCount = static_cast<uint16_t>(g.core.slowCpu != 0 ? 1 : 0);
    for (unsigned i = 0; i < es.elfActiveCount && i < kElfCount; ++i) {
        g.world.curObj = DsPtr{elfObjOff(static_cast<int>(i))};
        if (es.elves[i].state > 0x2C) ++es.elfSpriteCount;
    }
    // Every active record is visited (base 0 ones only set g_curObj), so a visible cell call later in
    // the tick sees the last record as its stale object (code_world.md §4.3).
    for (unsigned i = 0; i < es.elfActiveCount && i < kElfCount; ++i) {
        g.world.curObj = DsPtr{elfObjOff(static_cast<int>(i))};
        Elf& e = es.elves[i];
        if (e.base == 0) continue;
        es.playerRelX = s16(g.player.slots[1].x - 2);
        es.elfRelX = static_cast<int16_t>(static_cast<int16_t>((e.obj.y & 0xFF80) * 16 + e.obj.x - cam) >> 1);
        es.elfRelY = s16((e.obj.y & 0x7F) - (g.world.cameraY & 0x7F));
        elfThink(g);
        cellGetMask(g);
        e.obj.x = static_cast<uint16_t>(e.obj.x + e.obj.dx);
        objWrapWorldX(g);
        if (e.state > 0x2C) {
            // A visible elf's dx comes from the script op -4 of this tick and is consumed here.
            e.obj.dx = 0;
            const int s = e.spriteSlot;
            SpriteSlot& slot = g.player.slots.at(static_cast<size_t>(s));
            es.elfRelX = static_cast<int16_t>((e.obj.y & 0xFF80) * 16 + e.obj.x - cam);  // pixels this time
            es.elfRelY = s16((e.obj.y & 0x7F) - (g.world.cameraY & 0x7F));
            slot.x = s16((es.elfRelX >> 1) + 2);
            slot.y = s16(es.elfRelY + 0x10);
            slot.owner = static_cast<uint16_t>(s);
            slot.obj = g.world.curObj;
        }
    }
    es.netCatchesThisTick = 0;
}

// 4521:1062 Elf_Think
void elfThink(DosGame& g) {
    ElfState& es = g.elves;
    Elf& e = curElf(g);
    es.elfNetClass = 0;
    const int16_t x = es.elfRelX;
    const int16_t y = es.elfRelY;
    if (y < 0 || y > 0x80 || x < -0x11 || x > 0xAD) {
        if (e.state > 0x2C) elfRelease(g);
    } else if (e.state < kStVisible) {
        // A hidden elf gets a sprite only in the 17-byte zones just outside the view (moving in or
        // standing), or during a teleport; at most 3 sprites (+1 counted on a slow CPU). The code's
        // further clauses "Y < 0" and "Y > 0x98" cannot be true here.
        if (es.elfSpriteCount < 3 &&
            (g.world.transition != 0 || (x < 0 && e.obj.dx >= 0) || (x > 0x9C && e.obj.dx <= 0))) {
            ++es.elfSpriteCount;
            es.elfNetClass = kClassNewSprite;
            for (int s = kFirstElfSlot; s < kElfSlotEnd; s += 2) {
                SpriteSlot& slot = g.player.slots[static_cast<size_t>(s)];
                if (slot.anim == 0 && slot.prev == 0) {
                    e.spriteSlot = static_cast<uint16_t>(s);
                    slot.z = static_cast<int16_t>((g.rand.rand() & 1) == 0 ? 0x3C : 0x28);
                    break;
                }
            }
        }
        if (es.elfNetClass == 0) e.obj.dx = 0;
    } else {
        es.elfNetClass = kClassVisible;
        const int16_t p = es.playerRelX;
        const uint16_t ps = g.player.playerState;
        // Net zones in front of the player (signed, inclusive; the inner zones override).
        if (ps == kPlrNetRight && x <= s16(p + 0x2A) && s16(p + 9) <= x)
            es.elfNetClass = kClassNetEdge;
        else if (ps == kPlrNetLeft && s16(p - 0x2C) <= x && x <= s16(p - 0x0B))
            es.elfNetClass = kClassNetEdge;
        if (ps == kPlrNetRight && x <= s16(p + 0x24) && s16(p + 0x0F) <= x) {
            es.elfNetClass = kClassNet;
            if (x <= s16(p + 0x1C) && s16(p + 0x17) <= x) es.elfNetClass = kClassNetCentre;
        } else if (ps == kPlrNetLeft && s16(p - 0x26) <= x && x <= s16(p - 0x11)) {
            es.elfNetClass = kClassNet;
            if (s16(p - 0x1E) <= x && x <= s16(p - 0x19)) es.elfNetClass = kClassNetCentre;
        }
    }
    ++es.elfCallCounter;
    callElfStateHandler(g, curElf(g).state);
}

// g_elfStateHandlers DS:5D6C[state]; Null_Handler2 entries (0, 11, 15, 19, 23, 27, 29-44, 48, 75)
void callElfStateHandler(DosGame& g, uint16_t state) {
    switch (state) {
    case 1: elfSt01(g); break;
    case 2: case 6: elfSt02(g); break;
    case 3: case 7: case 67: elfSt03(g); break;
    case 4: case 8: elfSt04(g); break;
    case 5: elfSt05(g); break;
    case 9: elfSt09(g); break;
    case 10: case 14: elfSt10(g); break;
    case 12: case 16: elfSt12(g); break;
    case 13: elfSt13(g); break;
    case 17: elfSt17(g); break;
    case 18: case 22: elfSt18(g); break;
    case 20: case 24: elfSt20(g); break;
    case 21: elfSt21(g); break;
    case 25: case 26: elfSt25(g); break;
    case 28: elfSt28(g); break;
    case 45: elfSt45(g); break;
    case 46: elfSt46(g); break;
    case 47: elfSt47(g); break;
    case 49: elfSt49(g); break;
    case 50: elfSt50(g); break;
    case 51: elfSt51(g); break;
    case 52: elfSt52(g); break;
    case 53: elfSt53(g); break;
    case 54: elfSt54(g); break;
    case 55: elfSt55(g); break;
    case 56: elfSt56(g); break;
    case 57: elfSt57(g); break;
    case 58: elfSt58(g); break;
    case 59: elfSt59(g); break;
    case 60: elfSt60(g); break;
    case 61: elfSt61(g); break;
    case 62: elfSt62(g); break;
    case 63: elfSt63(g); break;
    case 64: elfSt64(g); break;
    case 65: elfSt65(g); break;
    case 66: elfSt66(g); break;
    case 68: elfSt68(g); break;
    case 69: elfSt69(g); break;
    case 70: elfSt70(g); break;
    case 71: elfSt71(g); break;
    case 72: elfSt72(g); break;
    case 73: elfSt73(g); break;
    case 74: elfSt74(g); break;
    default: break;  // Null_Handler2 (43cb:156b, a lone RETF); states past 75 do not occur
    }
}

// 4521:1268 Elf_Release
void elfRelease(DosGame& g) {
    ElfState& es = g.elves;
    Elf& e = curElf(g);
    es.elfSpriteCount = static_cast<uint16_t>(es.elfSpriteCount - 1);
    es.elfNetClass = kClassReleased;
    e.state = e.base;
    const int s = e.spriteSlot;
    e.spriteSlot = 0;
    animClear(g, s);
    SpriteSlot& child = g.player.slots.at(static_cast<size_t>(s) + 1);
    child.frame = kNoFrame;
    child.y = 0;
    child.x = 0;
}

// 4521:181a Elf_Turn
void elfTurn(DosGame& g) { callElfStateHandler(g, static_cast<uint16_t>(curElf(g).base + 3)); }

// 4521:12df Elves_ShiftNearby
void elvesShiftNearby(DosGame& g, int dx, int dy) {
    // g_curObj is the conveyed object (the player); unsigned compares, so with x < 0x100 the lower
    // bound wraps and no elf moves; no row test.
    const uint16_t px = objAt(g, g.world.curObj).x;
    for (int i = 0; i < static_cast<int>(g.elves.elfActiveCount) && i < kElfCount; ++i) {
        Elf& e = g.elves.elves[static_cast<size_t>(i)];
        if (static_cast<uint16_t>(px - 0x100) < e.obj.x && e.obj.x < static_cast<uint16_t>(px + 0x100) &&
            e.state > 0x2C) {
            e.obj.x = static_cast<uint16_t>(e.obj.x + dx);
            e.obj.y = static_cast<uint16_t>(e.obj.y + dy);
        }
    }
}

// 4521:1648 Elf_CheckNet
void elfCheckNet(DosGame& g) {
    ElfState& es = g.elves;
    Elf& e = curElf(g);
    es.elfAnimSel = isCarrier(e) ? kSelCarrierNet : 0;
    if (g.player.searchTimer != 0 && slotIdle(g, e)) {
        // A search is running: freeze (ElfSt_68 waits for it to end).
        e.state = kStFrozen;
        childOf(g, e).frame = kNoFrame;
        animSetIfIdle(g, slotIndex(e), kAnimStand);
        return;
    }
    // The net centre (classes 4-6 only occur in the two net-down states, so it is always set when
    // used; the original leaves the register uninitialised otherwise).
    int16_t centre = 0;
    if (g.player.playerState == kPlrNetRight)
        centre = s16(es.playerRelX + 0x1A);
    else if (g.player.playerState == kPlrNetLeft)
        centre = s16(es.playerRelX - 0x1C);
    if (es.elfNetClass == kClassNetEdge) {
        slotOf(g, e).z = s16(slotOf(g, e).z + 10);
        childOf(g, e).frame = kNoFrame;
        if ((g.rand.rand() & 0x3F) == 0) {
            // 1 in 64: the elf jumps out of the net and is gone (73, 74).
            e.state = kStEscapeJump;
            animSet(g, slotIndex(e), kAnimEscapeJump);
        } else {
            e.state = kStDodge;
            animSet(g, slotIndex(e), es.elfAnimSel + (centre < es.elfRelX ? kAnimHopRight : kAnimHopLeft));
        }
    } else if (es.elfNetClass == kClassNet || es.elfNetClass == kClassNetCentre) {
        slotOf(g, e).z = s16(slotOf(g, e).z + 10);
        childOf(g, e).frame = kNoFrame;
        if (centre < es.elfRelX) {
            animSet(g, slotIndex(e), es.elfAnimSel + kAnimPulledLeft);
            e.state = kStPulledLeft;
        } else {
            animSet(g, slotIndex(e), es.elfAnimSel + kAnimPulledRight);
            e.state = kStPulledRight;
        }
    }
}

// 4521:147c Elf_NetCatch
void elfNetCatch(DosGame& g) {
    ElfState& es = g.elves;
    Elf& e = curElf(g);
    const uint16_t ps = g.player.playerState;
    if (g.core.nets == 0) {
        // A torn net catches nothing: the elf gets up and walks off.
        if (slotIdle(g, e)) {
            childOf(g, e).frame = kNoFrame;
            e.state = kStDodge;
            animSetIfIdle(g, slotIndex(e), es.elfAnimSel + kAnimGetUp);
        }
    } else if (ps > 20 && ps < 27 && (g.core.uiMode & 2) == 0) {
        // Catches resolved in one Elves_Update call share one net (g_netCatchesThisTick).
        if (!isCarrier(e)) {
            if (es.netCatchesThisTick == 0) g.core.nets = static_cast<uint16_t>(g.core.nets - 1);
            ++es.netCatchesThisTick;
            g.core.coins = static_cast<uint16_t>(g.core.coins + 1);
            e.state = kStZip;
            animSet(g, slotIndex(e), es.playerRelX < es.elfRelX ? kAnimZipRight : kAnimZipLeft);
        } else if (ps > 20 && ps < 26) {
            if (es.netCatchesThisTick == 0) g.core.nets = static_cast<uint16_t>(g.core.nets - 1);
            g.core.coins = static_cast<uint16_t>(g.core.coins + 1);
            ++es.netCatchesThisTick;
            g.core.caughtSlot = e.question;
            g.core.uiMode |= 2;  // opens the question (quiz.c)
            e.state = kStAfterQuiz;
        }
    }
    animSetIfIdle(g, slotIndex(e), es.elfAnimSel + kAnimHeldLeft);
}

// 4521:04e6 Elf_WalkCommon
void elfWalkCommon(DosGame& g, int anim) {
    elfCheckNet(g);
    Elf& e = curElf(g);
    // A rand() every call; the slot is idle only on the tick the walk script ends, so the pause
    // chance is 1/16 per walk cycle.
    if ((g.rand.rand() & 0x0F) == 0 && slotIdle(g, e)) {
        animSetIfIdle(g, slotIndex(e), kAnimPause);
        e.state = kStAPause;
    }
    animSetIfIdle(g, slotIndex(e), anim);
}

// 4521:0d93 CoinElf_Check
void coinElfCheck(DosGame& g) {
    ElfState& es = g.elves;
    Elf& e = curElf(g);
    const int16_t p = es.playerRelX;
    const int16_t x = es.elfRelX;
    if (es.elfNetClass < kClassNetEdge) {
        const uint16_t ps = g.player.playerState;
        if (s16(p - 5) <= x && x <= s16(p + 5) && (ps < 17 || ps > 20)) {
            e.state = kStCoinCollected;
            animSet(g, slotIndex(e), kAnimCoinCollected);
        }
    } else {
        // A net swing makes it run away for good.
        e.state = kStCoinRunning;
        animSet(g, slotIndex(e), p < x ? kAnimCoinRunRight : kAnimCoinRunLeft);
    }
}

// 4521:000c ElfSt_01
void elfSt01(DosGame& g) {
    Elf& e = curElf(g);
    if (g.elves.elfNetClass < kClassNewSprite) {
        if ((g.elves.elfCallCounter & 2) == 0) e.obj.dx = -1;
    } else {
        g.elves.elfAnimSel = static_cast<uint16_t>(isCarrier(e) ? kAnimCarrierWalkLeft : kAnimAWalkLeft);
        animSetIfIdle(g, slotIndex(e), g.elves.elfAnimSel);
        e.state = kStAWalkLeft;
    }
}

// 4521:005c ElfSt_05
void elfSt05(DosGame& g) {
    Elf& e = curElf(g);
    if (g.elves.elfNetClass < kClassNewSprite) {
        if ((g.elves.elfCallCounter & 2) == 0) e.obj.dx = 1;
    } else {
        g.elves.elfAnimSel = static_cast<uint16_t>(isCarrier(e) ? kAnimCarrierWalkRight : kAnimAWalkRight);
        animSetIfIdle(g, slotIndex(e), g.elves.elfAnimSel);
        e.state = kStAWalkRight;
    }
}

// 4521:00ac ElfSt_02
void elfSt02(DosGame& g) {
    const uint16_t sel = isCarrier(curElf(g)) ? kSelCarrierWalk : 0;
    g.elves.elfAnimSel = sel;
    resumeAway(g, kBaseARight, kAnimAWalkRight + sel, kStAWalkRight, kBaseALeft, kAnimAWalkLeft + sel, kStAWalkLeft);
}

// 4521:013d ElfSt_04
void elfSt04(DosGame& g) {
    Elf& e = curElf(g);
    if (e.state < kStVisible) return;
    const uint16_t sel = isCarrier(e) ? kSelCarrierWalk : 0;
    g.elves.elfAnimSel = sel;
    turnAround(g, kBaseARight, kBaseALeft, kAnimAWalkLeft + sel, kStAWalkLeft, kAnimAWalkRight + sel, kStAWalkRight);
}

// 4521:01c4 ElfSt_09
void elfSt09(DosGame& g) { hiddenWalk(g, -1, kAnimBWalkLeft, kStBWalkLeft); }

// 4521:01fd ElfSt_13
void elfSt13(DosGame& g) { hiddenWalk(g, 1, kAnimBWalkRight, kStBWalkRight); }

// 4521:0236 ElfSt_10
void elfSt10(DosGame& g) {
    resumeAway(g, kBaseBRight, kAnimBWalkRight, kStBWalkRight, kBaseBLeft, kAnimBWalkLeft, kStBWalkLeft);
}

// 4521:02aa ElfSt_12
void elfSt12(DosGame& g) {
    turnAround(g, kBaseBRight, kBaseBLeft, kAnimBWalkLeft, kStBWalkLeft, kAnimBWalkRight, kStBWalkRight);
}

// 4521:0311 ElfSt_17
void elfSt17(DosGame& g) { hiddenWalk(g, -1, kAnimCWalkLeft, kStCWalkLeft); }

// 4521:034a ElfSt_21
void elfSt21(DosGame& g) { hiddenWalk(g, 1, kAnimCWalkRight, kStCWalkRight); }

// 4521:0383 ElfSt_18
void elfSt18(DosGame& g) {
    resumeAway(g, kBaseCRight, kAnimCWalkRight, kStCWalkRight, kBaseCLeft, kAnimCWalkLeft, kStCWalkLeft);
}

// 4521:03f7 ElfSt_20
void elfSt20(DosGame& g) {
    turnAround(g, kBaseCRight, kBaseCLeft, kAnimCWalkLeft, kStCWalkLeft, kAnimCWalkRight, kStCWalkRight);
}

// 4521:045e ElfSt_25
void elfSt25(DosGame& g) {
    // Only with a sprite obtained this tick and coins < 4 and nets < 5; otherwise the slot stays free
    // and it never moves while hidden.
    if (g.elves.elfNetClass > 1 && g.core.coins < 4 && g.core.nets < 5) {
        Elf& e = curElf(g);
        animSetIfIdle(g, slotIndex(e), kAnimCoinLying);
        e.state = kStCoinLying;
    }
}

// 4521:0491 ElfSt_28
void elfSt28(DosGame& g) {
    if (curElf(g).state > 0x2C) elfRelease(g);
}

// 4521:04a2 ElfSt_45
void elfSt45(DosGame& g) {
    g.elves.elfAnimSel = static_cast<uint16_t>(isCarrier(curElf(g)) ? kAnimCarrierWalkLeft : kAnimAWalkLeft);
    elfWalkCommon(g, g.elves.elfAnimSel);
}

// 4521:04c4 ElfSt_46
void elfSt46(DosGame& g) {
    g.elves.elfAnimSel = static_cast<uint16_t>(isCarrier(curElf(g)) ? kAnimCarrierWalkRight : kAnimAWalkRight);
    elfWalkCommon(g, g.elves.elfAnimSel);
}

// 4521:0545 ElfSt_47
void elfSt47(DosGame& g) {
    elfCheckNet(g);
    Elf& e = curElf(g);
    e.obj.dx = 0;
    if ((g.rand.rand() & 1) == 0 && slotIdle(g, e)) {
        if ((g.rand.rand() & 1) == 0) {
            e.base = kBaseALeft;
            e.state = kStAWalkLeft;
        } else {
            e.base = kBaseARight;
            e.state = kStAWalkRight;
        }
    }
    // On the tick the walk state is chosen the slot is still idle, so one more pause script runs
    // before the walk script starts.
    g.elves.elfAnimSel = static_cast<uint16_t>(isCarrier(e) ? kAnimCarrierPause : kAnimPause);
    animSetIfIdle(g, slotIndex(e), g.elves.elfAnimSel);
}

// 4521:05d8 ElfSt_49
void elfSt49(DosGame& g) {
    // Throw when the player is more than 38 bytes ahead to the left (no upper limit).
    const bool far = g.elves.playerRelX < s16(g.elves.elfRelX - 0x26);
    throwerWalk(g, far, kAnimWindUpLeft, kStThrowLeft, kAnimBWalkLeft);
}

// 4521:068b ElfSt_50
void elfSt50(DosGame& g) {
    // Throw when the player is more than 42 bytes ahead to the right.
    const bool far = s16(g.elves.elfRelX + 0x2A) < g.elves.playerRelX;
    throwerWalk(g, far, kAnimWindUpRight, kStThrowRight, kAnimBWalkRight);
}

// 4521:073e ElfSt_51
void elfSt51(DosGame& g) {
    elfCheckNet(g);
    Elf& e = curElf(g);
    e.obj.dx = 0;
    if (slotIdle(g, e)) {
        if ((g.rand.rand() & 1) == 0) {
            animSetIfIdle(g, slotIndex(e), kAnimBWalkLeft);
            e.base = kBaseBLeft;
            e.state = kStBWalkLeft;
        } else {
            animSetIfIdle(g, slotIndex(e), kAnimBWalkRight);
            e.base = kBaseBRight;
            e.state = kStBWalkRight;
        }
    }
    animSetIfIdle(g, slotIndex(e), kAnimPause);
}

// 4521:07d7 ElfSt_52
void elfSt52(DosGame& g) { throwStart(g, kAnimDustLeft); }

// 4521:0855 ElfSt_53
void elfSt53(DosGame& g) {
    // Dust thrown to the left: every jump (17..20) avoids it; misses vanish at -0x50 bytes.
    dustFlight(g, -1, 20, kAnimDustHitLeft, -0x50, kAnimBWalkLeft, kBaseBLeft, kStBWalkLeft, kAnimDustLeft);
}

// 4521:0967 ElfSt_54
void elfSt54(DosGame& g) { dustBack(g, 1, kAnimCoinBackLeft); }

// 4521:09c4 ElfSt_55
void elfSt55(DosGame& g) {
    // Walks away to the right with the coin.
    throwEnd(g, kAnimBWalkRight, kBaseBRight, kStBWalkRight, false);
}

// 4521:0a2e ElfSt_56
void elfSt56(DosGame& g) { throwStart(g, kAnimDustRight); }

// 4521:0aac ElfSt_57
void elfSt57(DosGame& g) {
    // Quirk kept (4521:0b06): only jumping left (17, 18) avoids dust thrown to the right.
    dustFlight(g, 1, 18, kAnimDustHitRight, 0x50, kAnimBWalkRight, kBaseBRight, kStBWalkRight, kAnimDustRight);
}

// 4521:0bbe ElfSt_58
void elfSt58(DosGame& g) { dustBack(g, -1, kAnimCoinBackRight); }

// 4521:0c1b ElfSt_59
void elfSt59(DosGame& g) {
    // Walks away to the left with the coin (the child is hidden before the walk starts).
    throwEnd(g, kAnimBWalkLeft, kBaseBLeft, kStBWalkLeft, true);
}

// 4521:0c85 ElfSt_60
void elfSt60(DosGame& g) {
    elfCheckNet(g);
    animSetIfIdle(g, slotIndex(curElf(g)), kAnimCWalkLeft);
}

// 4521:0c9e ElfSt_61
void elfSt61(DosGame& g) {
    elfCheckNet(g);
    animSetIfIdle(g, slotIndex(curElf(g)), kAnimCWalkRight);
}

// 4521:0cb7 ElfSt_70
void elfSt70(DosGame& g) {
    animSetIfIdle(g, slotIndex(curElf(g)), kAnimCoinLying);
    coinElfCheck(g);
}

// 4521:0cd0 ElfSt_71
void elfSt71(DosGame& g) {
    // Restarts the last run script: it keeps running the same way.
    Elf& e = curElf(g);
    animSetIfIdle(g, slotIndex(e), slotOf(g, e).prev);
    coinElfCheck(g);
}

// 4521:0cfe ElfSt_72
void elfSt72(DosGame& g) {
    Elf& e = curElf(g);
    if (!slotIdle(g, e)) return;
    elfRelease(g);
    e.state = e.base;
    placeInLevelRange(g, e);  // the respawn rand()
    g.core.coins = static_cast<uint16_t>(g.core.coins + 2);
}

// 4521:0e1b ElfSt_73
void elfSt73(DosGame& g) {
    Elf& e = curElf(g);
    if (!slotIdle(g, e)) return;
    slotOf(g, e).z = 0x1E;
    animSetIfIdle(g, slotIndex(e), kAnimEscapeSprite);
    e.state = static_cast<uint16_t>(e.state + 1);
}

// 4521:0e6e ElfSt_74
void elfSt74(DosGame& g) {
    // The escape sprite (child) moves down 2 rows per tick out of the play field.
    Elf& e = curElf(g);
    SpriteSlot& child = childOf(g, e);
    child.y = s16(child.y + 2);
    if (child.y > 0xA2) elfRelease(g);
}

// 4521:1354 ElfSt_64
void elfSt64(DosGame& g) {
    Elf& e = curElf(g);
    if (slotIdle(g, e)) callElfStateHandler(g, static_cast<uint16_t>(e.base + 1));
}

// 4521:1382 ElfSt_62
void elfSt62(DosGame& g) { pulled(g, kAnimCarrierHeldLeft, kAnimHeldLeft); }

// 4521:13e2 ElfSt_63
void elfSt63(DosGame& g) {
    g.elves.elfAnimSel = isCarrier(curElf(g)) ? kSelCarrierNet : 0;
    elfNetCatch(g);
}

// 4521:13ff ElfSt_65
void elfSt65(DosGame& g) { pulled(g, kAnimCarrierHeldRight, kAnimHeldRight); }

// 4521:145f ElfSt_66
void elfSt66(DosGame& g) {
    g.elves.elfAnimSel = static_cast<uint16_t>(isCarrier(curElf(g)) ? kSelCarrierNet + 1 : 1);
    elfNetCatch(g);
}

// 4521:15ab ElfSt_03
void elfSt03(DosGame& g) {
    Elf& e = curElf(g);
    // g_caughtSlot != 0: the question was answered right and the elf loses it.
    if (g.core.caughtSlot != 0) e.question = 0;
    childOf(g, e).frame = kNoFrame;
    e.state = kStDodge;
    // Quirk kept: the stale g_elfAnimSel (normally left by this elf's ElfSt_63/66 in the catch tick).
    animSetIfIdle(g, slotIndex(e), g.elves.elfAnimSel + kAnimGetUp);
}

// 4521:15f9 ElfSt_68
void elfSt68(DosGame& g) {
    if (g.player.searchTimer != 0) return;
    g.elves.elfCallCounter = 0;
    callElfStateHandler(g, curElf(g).base);
}

// 4521:1617 ElfSt_69
void elfSt69(DosGame& g) {
    Elf& e = curElf(g);
    if (!slotIdle(g, e)) return;
    e.state = e.base;
    elfRelease(g);
}

}  // namespace tmport::dos
