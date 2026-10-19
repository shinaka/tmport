#pragma once
// cells2.c (segment 176a), end part: the castle-top end screen, the castle gate, the treasure room
// with its 53-case display switch and helpers, the rank screen cells, and the end-of-game state.
// The rank chest drawing (130f) is in rank_chest.h. Spec: re/dos/docs/code_player.md §6.4-§7.3.
// Owner: player agent. Cell handlers follow cells.h's conventions (they return the mask).

#include <cstdint>

namespace tmport::dos {

class DosGame;

// 176a:0404 Cell_P64_EndScreen (type 64, A8-AA): when g_endSeqDone or any command: clear slots
// 3..11, effect loop off, Sfx_Stop, mask 0x14F0, player to the chute top (0x6A4,0x1F0), transition
// 2, camX 0x600, redraw, SetIfIdle(1, 0x5D), state 34; else mask 0.
uint16_t cellP64EndScreen(DosGame& g);
// 176a:0485 Cell_P20_CastleGate (type 20, 6D): landing on the path line: record +9 = 0, g_endState
// = 1, g_menuLock = 1; total < 300: treasure room (0x4F0,0x4E0), level 4, state 35; else the
// clubhouse (0x170,0x70) with the crown scripts 0xA5/0xA6/0xA7; transition 1; Anim_InitSlots.
uint16_t cellP20CastleGate(DosGame& g);
// 176a:0596 Cell_P70_TreasureRoom (type 70, 98 99 9B): the showcase display (case table of
// g_shownTreasure), the automatic exit to the rank screen, leaving (Down/coin/command), the
// carousel through the collected list.
uint16_t cellP70TreasureRoom(DosGame& g);
// 176a:0f47 TreasureRoom_Leave: player (0x170,0x70), level 0, g_menuLock = 0, state 9,
// Anim_InitSlots, Anim_Set(1, 6), redraw, transition 1, g_shackStay = g_shackFlags = 0,
// Game_AdvanceEnd.
void treasureRoomLeave(DosGame& g);
// 176a:0fa6 Cell_P71_V71_ToRankScreen (type 71, 01): left wall; rising to y&0x7F == 0x60 -> the rank
// screen (0x680,0x80), camX 0x680, level 4, g_rankState = 1, g_menuLock = 1, state 1, transition 2;
// visible call: Cell_V7. Also called directly by Cell_P70 and Cell_P12.
uint16_t cellP71V71ToRankScreen(DosGame& g);
// 176a:1094 Cell_P72_RankScreen (type 72, 1D): mouse buttons changed, command 8 or 0, or keys & 0x42:
// g_rankState = 0, back to (0xF0,0x70), camX 0xC0, level 0, state 9, transition 2, Anim_Set(1, 6),
// Game_AdvanceEnd, g_champFlashTimer = 0; mask 0x104F (3 when leaving).
uint16_t cellP72RankScreen(DosGame& g);
// 176a:1119 Game_AdvanceEnd: g_endState 1 -> 2, else Anim_Set(1, 6).
void gameAdvanceEnd(DosGame& g);
// 176a:1136 Treasure_IsCollected(id): is id in the collected list (signed chars)? Then
// g_shackFlags = 0x40 (assignment) and 1, else 0.
int treasureIsCollected(DosGame& g, int id);
// 176a:1162 ShackPair_08_36_Approach(dir): pair 0x08/0x36 partner and pedestal steps.
void shackPair0836Approach(DosGame& g, int dir);
// 176a:11b7 ShackPair_08_36_Swap(dir): pair 0x08/0x36 alternating frames 0x36/0x6A while moving apart.
void shackPair0836Swap(DosGame& g, int dir);
// 176a:1220 ShackPair_0e_2c(dir): pair 0x0E/0x2C approach step.
void shackPair0e2c(DosGame& g, int dir);
// 176a:1275 ShackPair_06_5a: pair 0x06/0x5A approach step (no argument used).
void shackPair065a(DosGame& g);
// 176a:129d ShackPair_1c_22(dir): pair 0x1C/0x22 approach step.
void shackPair1c22(DosGame& g, int dir);
// 176a:12f2 ShackPair_18_58(dir): pair 0x18/0x58 approach step.
void shackPair1858(DosGame& g, int dir);
// 176a:1347 ShackFly_1e: treasure 0x1E rises (y 0x43 -> 0), becomes 0xCC, comes back.
void shackFly1e(DosGame& g);
// 176a:13d7 ShackFly_50: treasure 0x50 rises as 0x84, comes back as 0x86.
void shackFly50(DosGame& g);
// 176a:1463 ShackTrio_4a_4c_4e(which): trio 0x4A/0x4C/0x4E approach step.
void shackTrio4a4c4e(DosGame& g, int which);
// 176a:1531 Shack_ResetDisplay(anim): restores the pedestal state (saved shown treasure, partners,
// positions, flags 0x20), Anim_Clear(8), Anim_Clear(10), and with anim != 0 SetIfIdle(3, anim).
// (Cell_P70 calls it without an argument when leaving: pass 0, TreasureRoom_Leave clears slot 3.)
void shackResetDisplay(DosGame& g, int anim);

}  // namespace tmport::dos
