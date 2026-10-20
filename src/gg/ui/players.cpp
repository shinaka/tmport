// PLAYERS.DAT on the game (players.h; code_ui.md §12). Owner: ui agent.
#include "gg/ui/players.h"

#include "gg/app/seq_run.h"
#include "gg/game/actors.h"
#include "gg/game/game.h"
#include "gg/game/parts.h"
#include "gg/game/rooms.h"
#include "gg/ui/dialog.h"
#include "util/file_util.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kMsgDiskFull = 0x106F;

}  // namespace

// 2b5a:0000 Player_InitRecord
void playerInitRecord(GgGame& g) {
    // The original stores DS:1ad4 into g_soundOff first; the copy of the blank record overwrites it
    // (a new player's sound option is always "on": kept, code_ui.md §7).
    g.player = PlayerRecord::blank();
}

// 2b5a:0198 Players_LoadRecord
void playersLoadRecord(GgGame& g, int slot) { g.player = g.players.loadRecord(slot); }

// 2b5a:01ea Players_SaveCurrent
void playersSaveCurrent(GgGame& g, GgSaveMoment moment) {
    if (g.ui.saveDisabled != 0) return;  // the sign-in's Esc path
    if (g.world.inBuilding != 0) {
        // The building in progress into its block (record +0x27E + b * 0x6BA), its flag set; a later
        // save does not rewrite the block (g_inBuilding = 0).
        const int b = g.player.building;
        if (b >= 0 && b < kBuildingCount) {
            g.player.buildingInProgress[static_cast<size_t>(b)] = 1;
            BuildingBlock& block = buildingBlock(g.player, b);
            block.live = g.player.live;  // memcpy(block, DS:457c, 0x14)
            partsSaveState(g, block);    // 153f:0385 Parts_SaveState(block + 0x14)
            roomsSaveState(g, block);    // 1622:0fb3 Rooms_SaveState(block + 0x26c, block + 0x302)
            actorsSaveState(g, block);   // 1834:0b5e Actors_SaveState(block + 0x492)
        } else {
            // The original writes outside the record (g_building is -1 only outside a building).
            log::error("Players_SaveCurrent: g_inBuilding with building {}: no block written", b);
        }
        g.world.inBuilding = 0;
    }
    // The record to its slot and its first 22 bytes to index entry g_rosterIndex (the file never changes
    // size).
    if (!g.players.saveCurrent(moment, g.player, g.ui.rosterIndex)) playersWriteError(g);
}

// 2b5a:010a Players_WriteError
void playersWriteError(GgGame& g) {
    log::error("{}: the players file could not be written; the game ends (the file is kept)", pathToUtf8(g.players.path()));
    g.display.fadeIn();             // Pal_FadeIn
    dlgMessageRes(g, kMsgDiskFull);  // the footer from member 3 (PORTING_NOTES.md §10)
    gameExit(exit_code::kStartup);   // exit(1)
}

}  // namespace tmport::gg
