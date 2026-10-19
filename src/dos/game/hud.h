#pragma once
// The HUD of main.c (130f): the status bar with the coin count-up, the key icon and clue words, the
// prompt icon, numbers, and the screen painter dispatch. The rank chest (also 130f) is in
// rank_chest.h. Spec: re/dos/docs/code_ui.md §11, code_runtime.md §3.8-3.11. Owner: ui agent.

namespace tmport::dos {

class DosGame;

// 130f:0c73 Screen_DrawOverlay (per frame and on demand): a saved box background (slow CPU) is
// restored; else for scenes < 3 the painter DS:0104[g_screen] (Hud_Redraw, Quiz_DrawScreen,
// SignIn_Draw). No painter in scenes 3 and 4.
void screenDrawOverlay(DosGame& g);
// The painter g_screenPainters DS:0104[screen]: 0 Hud_Redraw, 1 Quiz_DrawScreen, 2 SignIn_Draw.
// Engine helper.
void callScreenPainter(DosGame& g, int screen);
// 130f:07a5 Hud_Redraw (painter 0): DS:922E: full redraw (frames, menu titles, Playfield_Redraw,
// shelves, actors when 2); counters changed: status bar, bag and net pictures, the level's treasure
// slots, the coin count-up (one coin and sfx 4 per g.frameClock() CoinCountUp step), numbers; key
// state changed: clue box, key icon, the level's revealed clue words; prompt changed:
// Hud_DrawPromptIcon.
void hudRedraw(DosGame& g);
// 130f:0c2e Hud_DrawPromptIcon: SPRITES 59 and, if g_promptIcon != 0, frame 60 + g_promptIcon / 2;
// g_hudPrompt = g_promptIcon; g_dirty++. Also called by Scene_Update.
void hudDrawPromptIcon(DosGame& g);
// 130f:0d00 Hud_DrawNumber(x, y, colour, value): 1-3 digits right-aligned in 2-byte steps (wave 2's
// drawNumber on g.back).
void hudDrawNumber(DosGame& g, int x, int y, int colour, int value);

}  // namespace tmport::dos
