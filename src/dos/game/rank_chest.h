#pragma once
// The rank chest screen of main.c (130f): the chest fill one row per 4 game ticks, the coin rows,
// and the CHAMPION flash. Spec: re/dos/docs/code_player.md §7.4, code_ui.md §11.4.
// Owner: player agent.

namespace tmport::dos {

class DosGame;

// 130f:0edf RankChest_Update (per frame): latched rank state 1: name and total, rows wanted from
// DS:0110, phase DS:9100 & 0xFC; later: one row (colour 7 -> 6) when (DS:9100 & 0xFC) changes,
// coin rows at 8..48 rows, sfx 3, at the target g_rankState = 0 and (>= 56 rows) the CHAMPION timer
// 800; the flash runs per frame while DS:0100 != 0 (g.frameClock() ChampionFlash steps; -4 per step
// on a slow CPU), ending visible.
void rankChestUpdate(DosGame& g);
// 130f:1261 RankChest_DrawStar(x, y, frame2): Sprite_Draw(frame2 / 2, x, y, flags 0x40).
void rankChestDrawStar(DosGame& g, int x, int y, int frame2);

}  // namespace tmport::dos
