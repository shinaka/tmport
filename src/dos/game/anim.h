#pragma once
// anim.c (segment 4306): the 13 sprite slots (PlayerState::slots, DS:8FD6), the animation script
// interpreter and the z-sorted actor drawing over the save-under protocol (wave 2's SpriteRenderer
// holds the draw records, DosGame::sprites). Scripts are read from the writable copy
// PlayerState::animScriptData (animScriptWord), so the patched frame of script 0x88 shows.
// Spec: re/dos/docs/code_player.md §3. Owner: player agent.
//
// Timing rules that are easy to break (§3.2): Anim_Set shows the first entry at once and the first
// tick re-processes entry 0; Anim_SetIfIdle / Anim_Queue show nothing until the next tick; the
// snapshot skips slots whose script ended (anim == 0 && prev != 0).

namespace tmport::dos {

class DosGame;

// 4306:0022 Anim_Tick (per tick, both Game_Tick paths): nothing while g_transition != 0; advances the
// scripts of 13 slots (1 when (g_uiMode & 0x7F) != 0); `held` = g_stepDone at entry holds every due
// advance at delay 0; ops -2 (g_playerDX), -3 (g_playerDY), -4 (slot obj dx), -5 (Sfx_Play), -6
// (g_creatureStrike++), -7 (move the slot), -10 (end); a new frame sets g_stepDone.
void animTick(DosGame& g);
// 4306:02c0 Anim_InitSlots: clears the 13 slots (frame 0xFFFF, owner = own index; +0x12 untouched),
// resets the draw records and order (g.sprites.reset()), DS:9236 = 0.
void animInitSlots(DosGame& g);
// 4306:0393 Sprites_Snapshot(from, to): when g_sceneWanted == g_level: copies x, y, z, frame of each
// slot to its draw record (skipping ended scripts), flags a change; always copies the owner.
void spritesSnapshot(DosGame& g, int from, int to);
// 4306:0497 Sprites_DrawSorted(from, to): draws the records by descending z (ties to the higher
// index), children offset by their owner, saving the background; g_dirty += sprites drawn
// (g.sprites.drawSorted on g.back with g.bank).
void spritesDrawSorted(DosGame& g, int from, int to);
// 4306:06dd Sprites_EraseAll(from, to): restores the saved backgrounds in reverse draw order.
void spritesEraseAll(DosGame& g, int from, int to);
// 4306:0784 Anim_SetIfIdle(slot, anim): only if the slot has no script: anim id = anim.
void animSetIfIdle(DosGame& g, int slot, int anim);
// 4306:07b9 Anim_Queue(slot, anim): if prev != anim: Anim_Stop, anim id = anim (no frame change).
void animQueue(DosGame& g, int slot, int anim);
// 4306:07fa Anim_Set(slot, anim): if the slot runs another script: Anim_Stop; then Anim_Start.
void animSet(DosGame& g, int slot, int anim);
// 4306:0844 Anim_Stop(slot): if anim != 0: prev = anim, delay = pos = 0 (anim id kept).
void animStop(DosGame& g, int slot);
// 4306:088e Anim_Start(slot, anim): anim id = anim; the first entry's frames (skipping a leading
// two-word op) into the slot group, z and owner from the slot.
void animStart(DosGame& g, int slot, int anim);
// 4306:092f Anim_Clear(slot): anim = prev = delay = pos = 0, frame = 0xFFFF.
void animClear(DosGame& g, int slot);

}  // namespace tmport::dos
