#pragma once
// PLAYERS.DAT on the game (ssg.exe ovl02 2b5a; re/gg/docs/code_ui.md §12): the current player's record
// is GgState::player (g_player DS:4562), the roster copy GgState::ui.roster, the file the store
// (gg/save/players_store.h), which writes exactly the original's bytes at the original's moments.
//
// Owner: ui agent.

#include <cstdint>

#include "gg/save/players_store.h"

namespace tmport::gg {

class GgGame;

// 2b5a:0000 Player_InitRecord: the blank record (zeros, building -1, races won -1 x 3) into
// GgState::player (its copy of DS:1ad4 into g_soundOff is overwritten: code_ui.md §7).
void playerInitRecord(GgGame& g);
// 2b5a:0198 Players_LoadRecord(slot): the record of a slot into GgState::player. A missing or broken
// file: the original exits with code 1; the engine reports it (tmport::Error) to the caller.
void playersLoadRecord(GgGame& g, int slot);
// 2b5a:01ea Players_SaveCurrent: nothing while g_saveDisabled; while g_inBuilding: the building's
// block (the 20 live bytes, Parts_SaveState, Rooms_SaveState, Actors_SaveState), its in-progress flag
// set, g_inBuilding = 0; then the record to its slot and its index entry g_rosterIndex
// (PlayersStore::saveCurrent at `moment`). A write failure: playersWriteError.
void playersSaveCurrent(GgGame& g, GgSaveMoment moment);
// 2b5a:010a Players_WriteError: Pal_FadeIn and the disk-full box 0x106F (its footer is member 3: the
// original's footer pointer is uninitialised), then exit(1). Deviation: the original deletes
// players.dat first and prints a text-mode message; the engine keeps the file (the previous one also as
// .bak) and logs the error (PORTING_NOTES.md §9).
[[noreturn]] void playersWriteError(GgGame& g);

}  // namespace tmport::gg
