#pragma once
// player.c (segment 18cf): the player state machine (g_playerStateHandlers DS:0B8A, 72 entries),
// movement and camera follow (Player_Move), the strip wrap, the search start, and the castle-top
// end sequence (states 41-71). Spec: re/dos/docs/code_player.md §4, §6. Owner: player agent.
//
// "keys" below is g.keys() after Player_Update masked it with the previous tick's cell mask (the
// high byte then holds the mask's high byte: 0x100 ladder, 0x200 shop, 0x400 camera mode, 0x800
// slide, 0x1000 fall, 0x2000 ladder jump). "idle" = slot 1 has no script.

#include <cstdint>
#include <functional>

namespace tmport::dos {

class DosGame;

// ---- test seam -------------------------------------------------------------------------------
// The terrain query of Player_Move is Cell_GetMask (cells.h). A module test of the player may
// install a stand-in for the world's cell handlers: it must act as Cell_GetMask does (handler of
// the cell at g_curObj's position: g_cellCallKind = 2, g_curCell, teleports and side effects
// included) and return the mask. An empty function restores Cell_GetMask. The game never sets it.
using TerrainMaskHook = std::function<uint16_t(DosGame&)>;
void setTerrainMaskHookForTests(TerrainMaskHook hook);

// ---- tick entry, movement, dispatch --------------------------------------------------------
// 18cf:181a Player_Update (per tick when g_uiMode == 0 and g_transition == 0): g_curObj = player;
// g_keysUnmasked = keys; keys = (keys | 0xFF00) & g_playerMask; the state handler; Player_Move.
void playerUpdate(DosGame& g);
// g_playerStateHandlers DS:0B8A[state]; states 1, 2, 6-8, 36-40, 62 (and out of range) do nothing.
void callPlayerStateHandler(DosGame& g, uint16_t state);
// 18cf:15f0 Player_Move: g_playerMask = Cell_GetMask() (may teleport and set g_transition); camera
// re-anchor by transition mode; scroll-or-move decision (left 0x20 / right 0x7C bytes, up 0x40 or
// 0x70 with mask 0x400 / down 0x70 rows; doubled with g_slowScroll); position += (2*dx, dy); strip
// wrap; g_stepDone |= camera steps; slot 1 position and depth.
void playerMove(DosGame& g);
// 18cf:17c9 Obj_WrapWorldX (on g_curObj; also Elves_Update): x >= 0x8000: y -= 0x80, x += 0x800;
// then x > 0x7FF: y += 0x80, x -= 0x800; each clears +6.
void objWrapWorldX(DosGame& g);
// 18cf:0b36 Player_NetLeft: Anim_Set(1, 0x15 if no nets else 0x14), slot 2 x = 0, state 21.
void playerNetLeft(DosGame& g);
// 18cf:0b6a Player_NetRight: scripts 0x18 / 0x17, state 24.
void playerNetRight(DosGame& g);
// 18cf:14e0 Search_Start (states 27/28): if no search runs: state 10, and with a place in front
// (Search_CanSearch): state 5, g_searchTimer = 0x39, g_searchCell, g_searchPicture, g_menuLock = 1.
void searchStart(DosGame& g);
// 18cf:1532 Search_CanSearch: picks g_searchSide by the walk script and x within the cell, returns
// whether the picture of the cell in front is <= the level's picture count + 3 (cellPictureRaw for
// the strip ends).
int searchCanSearch(DosGame& g);
// 18cf:0038 EndSeq_Setup: g_endSeqDone = 0; slots 4, 6-10 at (2, 0x10), slot 11 at (0x18, 0x2A);
// their depths; Sfx_Stop, Music_Stop.
void endSeqSetup(DosGame& g);

// ---- states 0-35 (§4.5) ----------------------------------------------------------------------
// 18cf:000d PlrSt_00_Idle: (BSS start value) ladder -> 15; Anim_Set(1, 6); prev 6 -> state 10.
void plrSt00Idle(DosGame& g);
// 18cf:082a PlrSt_03_Spawn: state 4.
void plrSt03Spawn(DosGame& g);
// 18cf:0831 PlrSt_04_Arrive: stand (6, 10), ladder (snap to the cell middle, 0x1E, 15), passage
// (0x1C, 29) or fall (0x3F, 31) by the mask.
void plrSt04Arrive(DosGame& g);
// 18cf:07f6 PlrSt_05_Searching: frozen; a direction or timer 1 ends it (timer 1 when the place is
// still in front), state 10.
void plrSt05Searching(DosGame& g);
// 18cf:081b PlrSt_09_AfterTeleport: frozen until no direction key is held, then state 10.
void plrSt09AfterTeleport(DosGame& g);
// 18cf:08cc PlrSt_10_OnPath: standing: Up (ladder / passage / look up), Down, jumps, nets, walks,
// fall, coin drop (coins -1, scripts 0x1A/0x1B, slot 2 x = -25/+25; no coins: hints 7/6) or net by
// facing.
void plrSt10OnPath(DosGame& g);
// 18cf:0b9e PlrSt_11_WalkLeft: jumps, nets, keep walking (SetIfIdle 4), stop (2), fall (0x3F, 31).
void plrSt11WalkLeft(DosGame& g);
// 18cf:0ca0 PlrSt_12_WalkRight: the mirror image (5, 3, 0x40, 32).
void plrSt12WalkRight(DosGame& g);
// 18cf:0da2 PlrSt_13_LookUp: back pose 0xB; held Up walks up (9); ladder (0xC, 15) / passage (0x1C, 29).
void plrSt13LookUp(DosGame& g);
// 18cf:0e39 PlrSt_14_Front: front pose; Down climbs down (0xD, 16) or walks down (10) to the path line.
void plrSt14Front(DosGame& g);
// 18cf:0ed8 PlrSt_15_ClimbUp: ladder up (0xC while Up held), down (16), ladder jump (0x2000: clears
// g_slowCpu, 0x3D/0x3E, 33), fall (0x1000: 0x3F, 31); no ladder: back pose, 10.
void plrSt15ClimbUp(DosGame& g);
// 18cf:0fa9 PlrSt_16_ClimbDown: ladder down (0xD), up (15), the same jump and fall parts.
void plrSt16ClimbDown(DosGame& g);
// 18cf:107a PlrSt_17_JumpLeft: flip left (0xE / 0x10 while jump-left held), then landing (0x12, 18).
void plrSt17JumpLeft(DosGame& g);
// 18cf:10df PlrSt_18_JumpLeftLand: landing left or another flip.
void plrSt18JumpLeftLand(DosGame& g);
// 18cf:1120 PlrSt_19_JumpRight: flip right (0xF / 0x11), then landing (0x13, 20).
void plrSt19JumpRight(DosGame& g);
// 18cf:1185 PlrSt_20_JumpRightLand: landing right or another flip.
void plrSt20JumpRightLand(DosGame& g);
// 18cf:11c6 PlrSt_21_NetLeftSwing: when idle: state 22 (elves are caught only in states 21/24).
void plrSt21NetLeftSwing(DosGame& g);
// 18cf:11d6 PlrSt_22_NetLeftHold: Space released and idle: Anim_Queue(1, 0x16), state 23.
void plrSt22NetLeftHold(DosGame& g);
// 18cf:11fd PlrSt_23_NetLeftDone: when idle: Anim_Set(1, 2), state 10, slot 2 frame 0xFFFF.
void plrSt23NetLeftDone(DosGame& g);
// 18cf:1224 PlrSt_24_NetRightSwing: when idle: state 25.
void plrSt24NetRightSwing(DosGame& g);
// 18cf:1234 PlrSt_25_NetRightHold: Space released and idle: Anim_Queue(1, 0x19), state 26.
void plrSt25NetRightHold(DosGame& g);
// 18cf:125b PlrSt_26_NetRightDone: when idle: Anim_Set(1, 3), state 10, slot 2 frame 0xFFFF.
void plrSt26NetRightDone(DosGame& g);
// 18cf:1282 PlrSt_27_CoinLeftDone: when idle: SetIfIdle(2), slot 2 frame 0xFFFF; mask 0x200: state
// 10 and g_shopCoinsOwed -= 1, else Search_Start.
void plrSt27CoinLeftDone(DosGame& g);
// 18cf:12bc PlrSt_28_CoinRightDone: the same with script 3.
void plrSt28CoinRightDone(DosGame& g);
// 18cf:12f6 PlrSt_29_PassageUp: carried up (0x1C, sfx 1 every tick while slot 12 is idle, fast CPU)
// while 0x800; then area 0, Sfx_Stop, flip out (17 / 19).
void plrSt29PassageUp(DosGame& g);
// 18cf:1367 PlrSt_30_SlideDown: sliding down (0x1D, sfx 1) while 0x800; then area 0, Sfx_Stop,
// Anim_Set(1, 6), state 10.
void plrSt30SlideDown(DosGame& g);
// 18cf:13b9 PlrSt_31_FallLeft: falling (0x3F) while 0x1000; then flip-land left (17) or catch the
// ladder (0x1E, 16); keys &= 0xFFF3.
void plrSt31FallLeft(DosGame& g);
// 18cf:140c PlrSt_32_FallRight: falling (0x40), then flip-land right (19) or the ladder.
void plrSt32FallRight(DosGame& g);
// 18cf:145f PlrSt_33_LadderJump: when idle: no ladder -> fall right (32, 0x40), ladder -> 15.
void plrSt33LadderJump(DosGame& g);
// 18cf:1495 PlrSt_34_Chute: SetIfIdle(1, 0x5D); slot 1 prev != 0x5D: Treasure_AddCollected.
void plrSt34Chute(DosGame& g);
// 18cf:14b1 PlrSt_35_TreasureRoomLand: falling (0x5D) until 0x1000 clears, then state 1, Anim_Set(1, 0x52).
void plrSt35TreasureRoomLand(DosGame& g);

// ---- end sequence, states 41-61 and the champion branch 63-71 (§6.3) -----------------------
// Each waits for one slot's script to end, then starts the next scripts (CLOSE.PAK) and advances.
// 18cf:00a9 PlrSt_41_EndIntro: slot 6 done -> anim 0x80 on slot 6; 42.
void plrSt41EndIntro(DosGame& g);
// 18cf:00c4 PlrSt_42_EndHorn: sfx 8; slot 7 <- 0x7F, slot 6 <- 0x79; 43.
void plrSt42EndHorn(DosGame& g);
// 18cf:00f8 PlrSt_43_EndBeam: slot 9 <- 0x7A, slot 6 <- 0x7B; 44.
void plrSt43EndBeam(DosGame& g);
// 18cf:0122 PlrSt_44_EndSparkle: effect loop on (DS:63B8), sfx 2, slot 9 <- 0x7D; 45.
void plrSt44EndSparkle(DosGame& g);
// 18cf:014d PlrSt_45_EndChestOpen: effect loop off, sfx 8, Anim_Clear(9), slot 8 <- 0x83; 46.
void plrSt45EndChestOpen(DosGame& g);
// 18cf:0182 PlrSt_46_EndStep: slot 6 x = 10, slot 6 <- 0x82; 47.
void plrSt46EndStep(DosGame& g);
// 18cf:01a3 PlrSt_47_EndStep: slot 7 <- 0x7E, slot 6 <- 0x8B; 48.
void plrSt47EndStep(DosGame& g);
// 18cf:01cd PlrSt_48_EndFlightInit: slot 6 x = 2, <- 0x86; scan counters 0; Layout_UnmarkSharedCells;
// Music_Play(1); 49.
void plrSt48EndFlightInit(DosGame& g);
// 18cf:0205 PlrSt_49_EndFlightNext: one hiding slot per tick; a found treasure starts its flight
// (frame patched into script 0x88, DS:48DE; slot 9 <- 0x87, 50); after 48: chest closes (0x8C), 52.
void plrSt49EndFlightNext(DosGame& g);
// 18cf:0280 PlrSt_50_EndFlightStart: slot 9 at (0x88, 0x40) <- 0x88; 51.
void plrSt50EndFlightStart(DosGame& g);
// 18cf:02a7 PlrSt_51_EndFlightStep: x -= 4, y = DS:0B06[i] per 3 ticks; at the -1: 49.
void plrSt51EndFlightStep(DosGame& g);
// 18cf:02f2 PlrSt_52_EndRank: stars of the rank from the new total (DS:0110); total >= 300 -> 63.
void plrSt52EndRank(DosGame& g);
// 18cf:0343 PlrSt_53_EndRankSparkle: one sparkle (0x98) per star at DS:0B5E/0B6A; done -> 55.
void plrSt53EndRankSparkle(DosGame& g);
// 18cf:0398 PlrSt_54_EndRankWait: slot 9 done -> 53.
void plrSt54EndRankWait(DosGame& g);
// 18cf:03a4 PlrSt_55_EndCrown: no treasure: eyes 0x91 (57); all found (tier*3+6): crown 0x97 and
// creature 0x92, else 0x94 / 0x8A; 56.
void plrSt55EndCrown(DosGame& g);
// 18cf:0441 PlrSt_56_EndCreature: eyes 0x93, or puffs 0x95/0x96 when all found; 57.
void plrSt56EndCreature(DosGame& g);
// 18cf:049b PlrSt_57_EndPlayer: Anim_Clear(4), Anim_Clear(3), slot 6 <- 0x84; 58.
void plrSt57EndPlayer(DosGame& g);
// 18cf:04ca PlrSt_58_EndShowcaseCheck: no treasure: celebration 0x85, 61; else chest 0x8D, 59.
void plrSt58EndShowcaseCheck(DosGame& g);
// 18cf:050e PlrSt_59_EndShowcaseStart: DS:48DE = g_showcasePick & 0x7F; slot 9 at (0x20, 0x6E); 60.
void plrSt59EndShowcaseStart(DosGame& g);
// 18cf:0546 PlrSt_60_EndShowcaseStep: flight steps along DS:0B3C (x += 4 on the first 8), then the
// celebration 0x85, 61.
void plrSt60EndShowcaseStep(DosGame& g);
// 18cf:05b2 PlrSt_61_EndDone: slot 6 done -> g_endSeqDone = 1, state 62 (no-op; Cell_P64 takes over).
void plrSt61EndDone(DosGame& g);
// 18cf:05c4 PlrSt_63_ChampSparkle: slot 9 at (2, 0x10) <- 0x99; 64.
void plrSt63ChampSparkle(DosGame& g);
// 18cf:05eb PlrSt_64_ChampChest: effect loop on, sfx 7, chest 0x9A, cloud 0x9C; 65.
void plrSt64ChampChest(DosGame& g);
// 18cf:0625 PlrSt_65_ChampArcs: effect loop off, Sfx_Stop, arcs 0x9B, crown 0x97, creature 0x92; 66.
void plrSt65ChampArcs(DosGame& g);
// 18cf:0685 PlrSt_66_ChampScream: creature 0x9D, sfx 9; 67.
void plrSt66ChampScream(DosGame& g);
// 18cf:06be PlrSt_67_ChampCrownFly: crown 0xA0 flies, creature 0x9E; 68.
void plrSt67ChampCrownFly(DosGame& g);
// 18cf:06f4 PlrSt_68_ChampCatch: player catches the crown (0x9F, crown 0xA1); 69.
void plrSt68ChampCatch(DosGame& g);
// 18cf:0738 PlrSt_69_ChampCrowned: sfx 6, player 0xA2, 5 sparkles to come; 70.
void plrSt69ChampCrowned(DosGame& g);
// 18cf:0773 PlrSt_70_ChampSparkle: sparkle at DS:0B76/0B80; after 5: celebration 0x85, 61.
void plrSt70ChampSparkle(DosGame& g);
// 18cf:07ea PlrSt_71_ChampSparkleWait: slot 10 done -> 70.
void plrSt71ChampSparkleWait(DosGame& g);

}  // namespace tmport::dos
