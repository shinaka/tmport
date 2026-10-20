#pragma once
// The world's states (ssg.exe 12e0: states 4, 5, 7, 8; 18f5:094d Building_NewVisit; the door and score
// helpers; re/gg/docs/code_world.md §4, §6.6, §14, §15): building set-up and resume, entering and
// leaving a room, and the warehouse loop with its exact per-frame order and rand() order. The rooms
// (1622), the parts (153f) and the HUD (1a04) on the game are in rooms.h, parts.h and hud.h ("wave 3:
// the run"); the actors (1834, 18f5, 1adb, 2a80) are the actors module's (actors.h, player.h,
// collision.h, chimps.h).
//
// Cross-module calls (re/gg/docs/ENGINE_MAP.md, "Wave 3 map"):
//   warehouse loop -> actorsTick, actorsDraw (actors), hudUpdate (here), hintUpdate / hintQueue (ui),
//                     menuSetMenuBar (ui), cmdSaveAndExit (flow)
//   state 4        -> chimpsSetupActors, chimpsSpawn, actorsLoadPlayerSets, actorsRestoreState (actors)
//   state 5        -> actorsEnterRoom (actors)
//   state 8        -> chimpsFreeSets, actorsFreePlayerSets, actorsLeaveRoom (actors),
//                     playersSaveCurrent (ui)
//   state 9        -> roomOpenPuzzleDoor (here, called by puzzles_a)
//
// Owner: world agent.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gg/app/seq_run.h"
#include "gg/gfx/cursor.h"
#include "gg/input/zones.h"
#include "gg/save/players_dat.h"

namespace tmport::gg {

class GgExe;
class GgGame;
class GgStateFile;

// The world module's engine-side objects (GgGame::worldObjects): the widgets the original keeps inside
// heap records or DGROUP tables, which the engine's zone and cursor lists link by address.
struct WorldObjects {
    // The HUD strip zone of every part record (+0x48: Zone_SetFromObject of its strip sprite, priority
    // 10, release 5000 + part, group 4), indexed by part; never moved while a catalogue is loaded.
    std::array<Zone, kPartStates> partZones{};
    // g_hudStripZones DS:1330 (the arrows: 6002 / 6003, group 4) and g_hudCarryZones DS:135c (6001 the
    // whole HUD, 6000 the recycle can) as ssg.exe holds them (loaded on first use).
    std::array<Zone, 2> stripZones{};
    std::array<Zone, 2> carryZones{};
    bool hudZonesLoaded = false;
    // The part on the cursor (Hud_SelectPart: Mouse_LoadCursors of its icon), freed when the arrow comes
    // back (Mouse_RestoreDefaultCursor).
    std::vector<std::unique_ptr<CursorRecord>> partCursors;
    // The name box's text (DS:879a, a far pointer to a part's name copy in the original).
    std::vector<uint8_t> infoText;

    WorldObjects();
    ~WorldObjects();
    WorldObjects(const WorldObjects&) = delete;
    WorldObjects& operator=(const WorldObjects&) = delete;

    // The DS:1330 and DS:135c records from the exe (once).
    void loadHudZones(const GgExe& exe);
    // Every part zone out of the local zone list (a catalogue is about to go), then all of them as a
    // fresh catalogue's records hold them (zeros, ready for Hud_AddPart).
    void unlinkPartZones(GgGame& g);
    void resetPartZones();
    // Mouse_RestoreDefaultCursor for the HUD: the arrow back, the part cursors freed.
    void restoreCursor(GgGame& g);
};

// 12e0:0820 State_04_BuildingSetup: the title music freed (DS:0098), g_level = g_racesWon[b] + 1,
// g_raceIndex, Parts_LoadCatalogue, Rooms_Load (rand: the deck and the item grids), Hud_DrawAll(7);
// resume (g_buildingInProgress[b]): Actors_LoadPlayerSets, Chimps_SetupActors, the 20 live bytes,
// Parts_RestoreState, Rooms_RestoreState, Actors_RestoreState, g_inBuilding = 1; new visit:
// Building_NewVisit, g_partsNeeded, level 0's bananas and hint 0x1046 flag, R(100) g_setupCoin,
// Chimps_SetupActors, the workshop's live words, g_tubeRoom, Chimps_Spawn (rand). Returns 5.
int16_t state04BuildingSetup(GgGame& g);
// 12e0:096a State_05_EnterRoom: Input_Flush, play mode 1, Menu_SetMenuBar(DS:4053), Room_Enter(the
// player's room), Actors_EnterRoom, g_hudDirty |= 0x8000; 6 (room 0, the workshop) or 7.
int16_t state05EnterRoom(GgGame& g);
// 12e0:0619 State_07_Warehouse: the entry of code_world.md §14.1 (the Esc hook Hud_EscHook, effect
// bank 0x1039, the entry tick, Screen_Show's wipe with its rand(), hint 0x1048), the frames of §14.2
// (Hint_Update, Timer_Set(0, 4), Actors_Tick, the layers, Hud_Update, Dirty_Flush, Timer_Wait(0),
// Input_GetCode; Esc, Alt+S / Alt+W, the menu requests), the exit of §14.3; returns 8 or 16. Resume:
// the frames go on (a DOSBox-X state in a room): the effect bank is loaded again and the hooks set,
// without the entry's drawing, wipe and hint.
int16_t state07Warehouse(GgGame& g, StateEntry entry = StateEntry::Normal);
// 12e0:09aa State_08_LeaveRoom: Menu_SetMenuBar(NULL), Room_Leave, Actors_LeaveRoom; a puzzle door:
// font colour 0, the room music stopped, Pal_FadeOut, Chimps_FreeSets, Actors_FreePlayerSets -> 9; the
// vehicle done: g_buildingInProgress[b] = 1, g_inBuilding = 1, Players_SaveCurrent -> 10; else 5.
int16_t state08LeaveRoom(GgGame& g);
// 18f5:094d Building_NewVisit: the player of a new visit: floor 0, immunity 0, the previous room -1,
// room 0 (the workshop), contacts 0, carried count 0 and the list emptied; the projectile disabled.
void buildingNewVisit(GgGame& g);
// 12e0:0a24 Room_OpenPuzzleDoor(solved): when solved and the player's door object (actor 0 +0x36 in
// g_playerRoom) is still type 7: type 9, hit kind 4, the open-door picture; g_puzzleGoThrough set:
// g_playerRoom = the target; the twin in the target room opened too.
void roomOpenPuzzleDoor(GgGame& g, int solved);
// 12e0:0ace Score_Add(points, hud): g_score += points (u32, points sign-extended); the HUD's score bit
// (4) only when `hud` is non-zero. The pick-up (18f5:0151) and the workshop's vehicle bonus (2dee:12c7)
// pass 0 (the score shows the new value at the next full HUD redraw), Hud_AddScoreNow 1.
void scoreAdd(GgGame& g, int points, int hud = 0);
// 12e0:0aea Projectiles_Add(n): g_projectiles += n, the HUD's banana bit (8).
void projectilesAdd(GgGame& g, int n);
// The emergency reset of the warehouse loop (Alt+S then Alt+W, 12e0:0759..07c5): the player at x 10 on
// the bottom floor of the room, sequence 0, step 0, contacts cleared, contacts and walls on.
void warehouseResetPlayer(GgGame& g);
// The world's part of starting from a DOSBox-X state (game/load_state.h), for the states 5..15 (a
// building loaded): the part catalogue as the state's RAM holds it (holders, pairs, flags, strip
// sprites) with its icons and names, the carried lists (the player's and the chimps'), the room set copy
// with the door types, the animated objects and the item lists, g_roomItemCounts; the room music bank
// loaded again (and started in a room); when g_roomLoaded, the displayed room's pictures, the HUD's
// zones of the local list, the part on the cursor and the name box; in a room (state 7) the frame's
// sprite list unless a loader restored it. In every state the HUD's digit pictures follow DS:1318 and the
// picture cache's records DS:6640 are the state's (picCacheRestoreFromState). Meant for a freshly
// constructed game. False with a reason in `why` when the RAM does not hold what the DGROUP pointers
// promise.
bool worldRestoreFromState(GgGame& g, const GgStateFile& s, std::string& why);
// The picture cache's record table g_picCache DS:6640 (15df: the ids and use counts) from a state, so the
// slot numbers go on as the original's (PicCache::slots). worldRestoreFromState ends with it; a loader
// that runs other modules' restores after the world's (they load their pictures again) calls it last.
void picCacheRestoreFromState(GgGame& g, const GgStateFile& s);

}  // namespace tmport::gg
