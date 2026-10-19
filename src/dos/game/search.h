#pragma once
// search.c (segment 4a03): the per-frame hiding-place search, the showcase treasure choice and
// hand-over, and the treasure shelf rooms. Spec: re/dos/docs/code_player.md §5. Owner: player agent.

namespace tmport::dos {

class DosGame;

// 4a03:0003 Search_Update (per frame, kept per frame in both frame speeds): 37 frames from timer
// 0x39: blink closed/opened picture (opened = count + bits 4-5 of attribute byte 0, base DS:8FB4 /
// 0x26 / 0x1F), content sprite in slot 2; at timer 1 the result (treasure: found +1, found-mask
// bit, sfx 7, hint 4; key: key state 2/4/6, HUD redraw, sfx 6, hint 9), content |= 0x80 (also the
// mirror cell), g_menuLock = 0; each frame g_fullRedraw++ and DS:8FC8++ (partial redraw). Slow CPU:
// 21 frames. No search: g_searchSide = 0.
void searchUpdate(DosGame& g);
// 4a03:038c Treasure_PickShowcase (castle top): g_showcasePick among the treasures found this game,
// preferring one not equal to g_collected[0] or g_shownTreasure (only collected[0] is compared;
// the second loop may read g_hidingCells[-1]: hidingCellAt).
void treasurePickShowcase(DosGame& g);
// 4a03:04a8 ShelfRoom_DrawShelf (per frame and from Hud_Redraw): draws the 12 collected treasures of
// the first flagged shelf room (latched DS:8F9F..8FA2) when DS:8FBA is set, raised copies by the
// shelf bits; clears the room's redraw byte and DS:8FBA.
void shelfRoomDrawShelf(DosGame& g);
// 4a03:078c Treasure_AddCollected (player state 34, acts once): old showcase to the first free
// collected slot, duplicates of a new pick removed (shelf bits), new showcase to g_shownTreasure
// and DS:9254; g_showcasePick = 0xFF.
void treasureAddCollected(DosGame& g);

}  // namespace tmport::dos
