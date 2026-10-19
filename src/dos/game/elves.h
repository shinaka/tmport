#pragma once
// elves.c (segment 4521): spawn of up to 15 elves per scene, the per-tick update, sprite-slot
// allocation (slots 6/8/10 with children 7/9/11), the net classification, and the 76-entry elf
// state machine (g_elfStateHandlers DS:5D6C): walkers and clue carriers (A), dust throwers (B),
// plain walkers (C), coin creatures (D). Handlers act on the elf g_curObj points at
// (elfIndexOf(g.world.curObj)); "class" is g_elfNetClass of this tick, P = g_playerRelX, X =
// g_elfRelX. Spec: re/dos/docs/code_elves_layout_hints.md §3-§4. Owner: elves agent.
//
// rand() order matters (§4.11): one per spawned elf (A, D, B, C), the sprite depth in Elf_Think, then
// the handler's own calls, elf by elf in record order.

#include <cstdint>

namespace tmport::dos {

class DosGame;

// ---- update, think, dispatch ---------------------------------------------------------------
// 4521:182e Elves_Spawn(carriers) (Scene_Update, scenes < 3): g_elfActiveCount = 15 (7 slow CPU);
// clears base, state, question of all 15 (x, y, dx and sprite slot kept); spawns by
// g_elfCounts[tier] in the order A (first `carriers` get question slots 1..n), D, B, C with one
// rand() each for x in the level's range.
void elvesSpawn(DosGame& g, int carriers);
// 4521:0ead Elves_Update (per tick, g_uiMode == 0 and g_slowCpu == g_slowCpuSaved): sprite count,
// then per active record: relative position, Elf_Think, Cell_GetMask (g_curObj = the elf), x += dx,
// Obj_WrapWorldX, the sprite slot position; g_netCatchesThisTick = 0 at the end.
void elvesUpdate(DosGame& g);
// 4521:1062 Elf_Think: out of range -> Elf_Release (class 1); a hidden elf entering at a view edge
// (or during a teleport) gets a free slot 6/8/10 (class 2, rand() for depth 0x28/0x3C); a visible elf
// class 3, or 4/5/6 in the net zones of a net-down swing; then g_elfCallCounter++ and the handler.
void elfThink(DosGame& g);
// g_elfStateHandlers DS:5D6C[state]; Null_Handler2 entries (0, 11, 15, 19, 23, 27, 29-44, 48, 75) do nothing.
void callElfStateHandler(DosGame& g, uint16_t state);
// 4521:1268 Elf_Release: g_elfSpriteCount--, class 1, state = base, Anim_Clear(slot), child slot
// frame none, x = y = 0, record slot 0.
void elfRelease(DosGame& g);
// 4521:181a Elf_Turn (from the cell handlers): calls handler[base + 3].
void elfTurn(DosGame& g);
// 4521:12df Elves_ShiftNearby(dx, dy) (conveyors, before the player moves): visible elves with
// player.x - 0x100 < x < player.x + 0x100 (unsigned, no row test) move by (dx, dy).
void elvesShiftNearby(DosGame& g, int dx, int dy);
// 4521:1648 Elf_CheckNet: freeze during a search (68, anim 0x22); class 4: dodge hop (64) or 1/64
// escape (73, anim 0x4F); class 5/6: pulled to the net centre (62/65).
void elfCheckNet(DosGame& g);
// 4521:147c Elf_NetCatch (states 63/66): no nets: get up (base+0x35, 64); else while the player
// swings: one net per tick's catches, coins +1; carrier: g_caughtSlot, g_uiMode |= 2, state 67;
// other: zip away (0x4D/0x4E, 69); then SetIfIdle(sel + 0x31).
void elfNetCatch(DosGame& g);
// 4521:04e6 Elf_WalkCommon(anim): Elf_CheckNet; rand() every call; pause (0x21, 47) when rand()&15
// == 0 and the slot is idle; SetIfIdle(anim).
void elfWalkCommon(DosGame& g, int anim);
// 4521:0d93 CoinElf_Check: class < 4 and the player within +-5 bytes, not jumping: state 72, anim
// 0x59 (sfx 4); class >= 4: state 71, runs away (0x57 / 0x58).
void coinElfCheck(DosGame& g);

// ---- state handlers (§3.4) -------------------------------------------------------------------
// 4521:000c ElfSt_01 (A hidden left): drift dx -1 on even call-counter phases; a new sprite starts
// the walk (0x1F / carrier 0x41), state 0x2D.
void elfSt01(DosGame& g);
// 4521:005c ElfSt_05 (A hidden right): dx +1; walk 0x20 / 0x42, state 0x2E.
void elfSt05(DosGame& g);
// 4521:00ac ElfSt_02 (A resume, handler[base+1]): depth -10, walks away from the player.
void elfSt02(DosGame& g);
// 4521:013d ElfSt_04 (A turn): visible only: flip base 1 <-> 5 with the walk script.
void elfSt04(DosGame& g);
// 4521:01c4 ElfSt_09 (B hidden left): as ElfSt_01 with walk 0x23, state 49.
void elfSt09(DosGame& g);
// 4521:01fd ElfSt_13 (B hidden right): walk 0x24, state 50.
void elfSt13(DosGame& g);
// 4521:0236 ElfSt_10 (B resume): depth -10, walks away (13/0x24/50 or 9/0x23/49).
void elfSt10(DosGame& g);
// 4521:02aa ElfSt_12 (B turn): visible only: flip 9 <-> 13.
void elfSt12(DosGame& g);
// 4521:0311 ElfSt_17 (C hidden left): walk 0x2D, state 60.
void elfSt17(DosGame& g);
// 4521:034a ElfSt_21 (C hidden right): walk 0x2E, state 61.
void elfSt21(DosGame& g);
// 4521:0383 ElfSt_18 (C resume): depth -10, walks away.
void elfSt18(DosGame& g);
// 4521:03f7 ElfSt_20 (C turn): visible only: flip 17 <-> 21.
void elfSt20(DosGame& g);
// 4521:045e ElfSt_25 (D hidden, also 26): with a new sprite and coins < 4 and nets < 5: anim 0x56,
// state 70; never moves while hidden.
void elfSt25(DosGame& g);
// 4521:0491 ElfSt_28 (D turn): visible: Elf_Release.
void elfSt28(DosGame& g);
// 4521:04a2 ElfSt_45 (A walking left): Elf_WalkCommon(0x1F + sel).
void elfSt45(DosGame& g);
// 4521:04c4 ElfSt_46 (A walking right): Elf_WalkCommon(0x20 + sel).
void elfSt46(DosGame& g);
// 4521:0545 ElfSt_47 (A pause): Elf_CheckNet, dx = 0, rand() every call; idle and even: rand() for
// the direction; SetIfIdle(pause 0x21 / 0x43).
void elfSt47(DosGame& g);
// 4521:05d8 ElfSt_49 (B walking left): when idle: rand() even: throw (player > 38 bytes ahead: 0x25,
// 52) or pause (0x21, 51); odd: rand() & 15 == 0: pause; SetIfIdle(0x23).
void elfSt49(DosGame& g);
// 4521:068b ElfSt_50 (B walking right): the mirror (test X + 0x2A < P, 0x26, 56, walk 0x24).
void elfSt50(DosGame& g);
// 4521:073e ElfSt_51 (B pause): when idle: rand() picks the walk direction (49 / 50).
void elfSt51(DosGame& g);
// 4521:07d7 ElfSt_52 (throw left start): wind-up ended: dust script 0x27, child at (0, 0), 53.
void elfSt52(DosGame& g);
// 4521:0855 ElfSt_53 (dust flying left): hit within +-10 bytes (coins > 0, player on the path line,
// not jumping 17..20): coins -1, 54, anim 0x29; at -0x50 the dust vanishes (49); else child x -1.
void elfSt53(DosGame& g);
// 4521:0967 ElfSt_54 (dust back with the coin): child x +1 until 0, then 0x2B, 55.
void elfSt54(DosGame& g);
// 4521:09c4 ElfSt_55 (throw left end): when idle: walk right 0x24, child hidden, base 13, 50.
void elfSt55(DosGame& g);
// 4521:0a2e ElfSt_56 (throw right start): 0x28, 57.
void elfSt56(DosGame& g);
// 4521:0aac ElfSt_57 (dust flying right): as ElfSt_53 mirrored; only player states 17..18 avoid a hit.
void elfSt57(DosGame& g);
// 4521:0bbe ElfSt_58 (dust back, right throw): child x -1 until 0, then 0x2C, 59.
void elfSt58(DosGame& g);
// 4521:0c1b ElfSt_59 (throw right end): when idle: walk left 0x23, base 9, 49.
void elfSt59(DosGame& g);
// 4521:0c85 ElfSt_60 (C walking left): Elf_CheckNet, SetIfIdle(0x2D).
void elfSt60(DosGame& g);
// 4521:0c9e ElfSt_61 (C walking right): Elf_CheckNet, SetIfIdle(0x2E).
void elfSt61(DosGame& g);
// 4521:0cb7 ElfSt_70 (D lying): SetIfIdle(0x56), CoinElf_Check.
void elfSt70(DosGame& g);
// 4521:0cd0 ElfSt_71 (D running away): SetIfIdle(slot prev) (keeps running), CoinElf_Check.
void elfSt71(DosGame& g);
// 4521:0cfe ElfSt_72 (D collected): when idle: Elf_Release, state = base, rand() respawn x, coins += 2.
void elfSt72(DosGame& g);
// 4521:0e1b ElfSt_73 (escape jump): when idle: depth 0x1E, SetIfIdle(0x50), 74.
void elfSt73(DosGame& g);
// 4521:0e6e ElfSt_74 (escaped): child y += 2 per tick; > 0xA2: Elf_Release.
void elfSt74(DosGame& g);
// 4521:1354 ElfSt_64 (dodge / get up): when idle: handler[base + 1].
void elfSt64(DosGame& g);
// 4521:1382 ElfSt_62 (pulled, moving right): when idle: held pose (0x31 / carrier 0x47), 63;
// class 6: dx = 0.
void elfSt62(DosGame& g);
// 4521:13e2 ElfSt_63 (held, left): g_elfAnimSel = carrier ? 0x16 : 0; Elf_NetCatch.
void elfSt63(DosGame& g);
// 4521:13ff ElfSt_65 (pulled, moving left): held pose 0x32 / 0x48, 66.
void elfSt65(DosGame& g);
// 4521:145f ElfSt_66 (held, right): g_elfAnimSel = carrier ? 0x17 : 1; Elf_NetCatch.
void elfSt66(DosGame& g);
// 4521:15ab ElfSt_03 (after a quiz, also states 3, 7, 67): answered right (g_caughtSlot != 0): the
// question is gone; child hidden, 64, SetIfIdle(stale g_elfAnimSel + 0x35).
void elfSt03(DosGame& g);
// 4521:15f9 ElfSt_68 (search freeze): when g_searchTimer == 0: g_elfCallCounter = 0, handler[base].
void elfSt68(DosGame& g);
// 4521:1617 ElfSt_69 (zipped away): when idle: state = base, Elf_Release.
void elfSt69(DosGame& g);

}  // namespace tmport::dos
