#pragma once
// scene.c (segment 1437) and Scene_LoadPan (1460:05e8): loading the scene of g_level (PAN, sprite
// bank), the menu-bar blank and restore of scenes 3/4, the end-of-game hook. Spec:
// re/dos/docs/code_world.md §7. Owner: world agent.

#include <cstdint>

#include "dos/data/file_table.h"

namespace tmport::dos {

class DosGame;

// 1437:0001 Pal_Apply: not ported as a function. MCGA's DAC copy and CGA's dither map are the
// look's palette, applied when a frame is presented (DosGame::palette, dos/gfx/palettes.h).

// 1437:0034 Scene_Update (per frame): blank the menu bar when entering scene 3/4; when
// g_sceneWanted != g_loadedScene and g_uiMode == 0: the end-of-game hook (g_endState 2: Game_End,
// Cells_ResetVariable, Layout_ClearSlots, Game_Start, message 2), Quiz_ReorderSlots + Elves_Spawn
// for scenes < 3, Scene_LoadPan, the sprite bank (SPRITES / CLOSE / SHACK), g_loadedScene, the
// music loop flag; restore the menu bar for scenes < 3. Returns AX as the original (unused).
uint16_t sceneUpdate(DosGame& g);

// 1460:05e8 Scene_LoadPan(index): loads PAN file `index` (scene number, or 14 OPENING.PAN) into
// the map/tile buffers: g.prevPan = g.pan, g.pan = the file. The buffers are not cleared, so a
// smaller PAN leaves the previous one's blocks readable past its own (code_world.md §6.10, §7.2).
// Returns non-zero on success (always, the engine's files are checked at start-up).
int sceneLoadPan(DosGame& g, int fileIndex);

// The sprite-bank part of Scene_Update / Sprites_LoadBegin: g.bank = the PAK of `file`
// (SPRITES.PAK, BEGIN.PAK, CLOSE.PAK or SHACK.PAK). Engine helper, no address of its own.
void loadSpriteBank(DosGame& g, FileIndex file);

}  // namespace tmport::dos
