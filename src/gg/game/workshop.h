#pragma once
// The workshop (ssg.exe ovl08 2dee: state 6, the shelf conveyor, the vehicle's assembly, the paint, the
// recycler, the race-track door, the blueprint viewer's run; re/gg/docs/code_vehicles_races.md §5, §6).
// Its globals are GgState::vehicles (carried row and part, the shelf and conveyor words, the race door,
// the vehicle sprite pool and order, the part records' shelf fields) and the player record
// (g_blueprintSeen, g_partsInstalled, g_vehicleBonusGiven, g_paintColor, g_tubeRoom, g_vehicleColor);
// the drawing half of the blueprint is the wave 2 BlueprintView (blueprint.h).
//
// The workshop writes the fields of the part records that the original's workshop writes (+0x36 bit 0
// the upper shelf row, 0x8000 installed, 0x4000 recycled) in GgState::world.parts; holders and lists
// change only through Actor_RemovePart (actors). Painting remaps the cached pictures in place, as the
// original does (shared pictures: the undo loses shades of 14-colour parts, keep).
//
// Cross-module calls: hintQueue / hintShow / hintUpdate (ui), hudUpdate / hudSetInfoText /
// hudRefreshStrip / hudStripAnchorFor (world), roomDrawAnimLayer (world), actorRemovePart / actorOnEnd
// (actors: the tube's Player_SequenceEnd), itemPlaceInRandomRoom and scoreAdd (world: the recycler's
// rand, the completion bonus), cmdSaveAndExit (flow), the sound facade's workshop music.
//
// Owner: vehicles agent.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gg/app/seq_run.h"
#include "gg/game/race.h"
#include "gg/game/screen_anim.h"
#include "gg/gfx/cursor.h"
#include "gg/gfx/screen.h"
#include "gg/input/zones.h"
#include "gg/ui/button.h"

namespace tmport::gg {

class BlueprintView;
class GgGame;
class GgStateFile;

// The locals of Workshop_Run (2dee:0c89) that a frame carries to the next: kept with the module's
// objects so that a run resumed from a DOSBox-X state can rebuild them.
struct WorkshopRunLocals {
    int index = 0;          // SI: the current lower part's position in the shelf list
    int last = 0;           // [BP-0xe]: g_shelfCount - 1
    int current = -1;       // [BP-4]: the current (lower) part, -1 none
    int partner = -1;       // [BP-8]: its partner, -1 none
    bool mayInstall = false;    // [BP-0x18]: the carried part may be installed
    bool vehicleChanged = false;// [BP-0x12]: redraw the vehicle at the next frame
    bool leave = false;     // [BP-0xc]
    std::vector<int> shelf; // [BP-0x72]: Workshop_ListShelfParts' lower parts in list order
};

// The vehicles module's engine-side objects (GgGame::vehiclesObjects): the workshop's widgets (the
// click zones of DGROUP's tables and of the vehicle sprites), pictures and screen animations, the
// carried-part cursor, the blueprint viewer while it is open, the races' paths, sprite and scenery
// pictures, the results' animations.
struct VehiclesObjects {
    // ---- the workshop (§5)
    std::array<Zone, 7> staticZones{};  // DS:21ce (1009 the shelf area: its priority is DS:21d8)
    std::array<Zone, 2> shelfZones{};   // DS:21a2 (1004) and DS:21b8 (1005)
    Zone workbench;                     // DS:2268 (1006)
    std::array<Zone, 20> vehicleZones{};                  // the pool records' +0x1a zones
    std::array<std::span<const uint8_t>, 20> vehiclePics{};  // the pool records' +0x16 pictures
    std::vector<std::span<const uint8_t>> shelfPics;      // per part: +0x68 (PicCache_Load(+0x12))
    // The conveyor animations: DS:a0b8 set 0x0d, DS:a0bc 0x0c (the gear sets), DS:a0c0 0x11, DS:a0c4 0x0f.
    std::array<std::optional<ScreenAnim>, 4> conveyor;
    std::unique_ptr<CursorRecord> carriedCursor;  // Mouse_LoadCursors of the carried part's picture
    WorkshopRunLocals run;
    bool zonesBuilt = false;
    // ---- the blueprint viewer (§6)
    std::unique_ptr<BlueprintView> blueprint;
    PicButton done;                // DS:2280 (code 1007)
    std::vector<Zone> hotZones;    // the 'M' hot spots (release 2000 + page)
    SavedImage blueprintUnder;     // Screen_SaveToSwap's area (gizmo004.dat in the original: in memory)
    int blueprintShown = 0;        // the page shown (1 overview, 2 a part page), Blueprint_Run's [BP-2]
    // ---- the races (§9, §10)
    std::vector<RacePathPoint> playerPath, mortyPath;  // copies of the 'R' paths (+0x18 far)
    std::vector<RacePathPoint> launchPath1, launchPath2;  // aircraft level 1: 0x252 / 0x253
    const std::vector<std::span<const uint8_t>>* playerFrames = nullptr;  // the sprite bundles' pictures
    const std::vector<std::span<const uint8_t>>* mortyFrames = nullptr;
    const std::vector<std::span<const uint8_t>>* sceneryFrames = nullptr;  // bundle 0x248
    const std::vector<std::span<const uint8_t>>* launchFrames = nullptr;   // bundle 0x24f
    struct SceneryRec {
        int16_t x = 0, y = 0, frame = 0;
    };
    std::vector<SceneryRec> scenery1, scenery2;  // W 0x264 + level: the far and the near layer
    std::span<const uint8_t> playerPic, mortyPic;  // the racer sprites' pictures (+0x0a / +0x0e far)
    // ---- the results (§11)
    std::array<std::optional<ScreenAnim>, 4> resultAnims;  // W1, W2, A, B

    VehiclesObjects();
    ~VehiclesObjects();
};

// 2dee:1db5 Workshop_Init (Game_Init): the vehicle sprite list (empty).
void workshopInit(GgGame& g);
// 2dee:1a90 State_06_Workshop: the entry of §5.2 (Pal_Save, Pal_LoadRes(0x2B), play mode 0,
// g_inBuilding = 1, the Esc hook Workshop_DropCarried, the trophies, the tube sign, the music and cue,
// the shelf, the vehicle, the conveyor sets, effect 10, Delay(15), the race door, the fade, the tube
// animation), Workshop_Run, the exit; returns g_workshopNextState (8 or 16). Resume: the state's
// workshop is rebuilt by vehiclesRestoreFromState and Workshop_Run's frames go on (inside the blueprint
// viewer when it was open).
int16_t state06Workshop(GgGame& g, StateEntry entry = StateEntry::Normal);
// 2dee:0c89 Workshop_Run: the 4-tick frames of §5.4 and the dispatch of §5.5; -1. Resume: no set-up;
// the next step is the input of the frame the state was saved in.
int workshopRun(GgGame& g, StateEntry entry = StateEntry::Normal);
// 2dee:00a6 Workshop_UpdateRaceDoor(sound): the door opened or closed when the completion changed
// (set 0x1d modal, pictures 0x26 / 0x25, effects 0xD / 0xE), the door area saved to the back page.
void workshopUpdateRaceDoor(GgGame& g, bool sound);
// 2dee:01a0 Workshop_PaintSprite(sprite, colour, undo): a paintable part's picture recoloured in place
// (shared pictures: the undo loses shades of 14-colour parts, keep).
void workshopPaintSprite(GgGame& g, int poolIndex, int colour, bool undo);
// 2dee:028f Workshop_LayoutShelf(load, x0): the held parts' shelf places (100 px apart from x0, the
// upper row above the partner); with load their pictures and the installed parts' vehicle sprites.
void workshopLayoutShelf(GgGame& g, bool load, int x0);
void workshopReleaseShelfPics(GgGame& g);       // 2dee:03b2 Workshop_ReleaseShelfPics
// 2dee:0401 Workshop_CompareLayer(a, b): b.layer - a.layer (the vehicle list's order).
int workshopCompareLayer(int layerA, int layerB);
void workshopEnableVehicleZones(GgGame& g, bool on);  // 2dee:0414 Workshop_EnableVehicleZones (no caller)
// 2dee:046d Workshop_InstallPart(part): its vehicle sprites from the pool (+0x18 instances), paint,
// g_vehicleColor[race], the zones 4000 + part, inserted by layer.
void workshopInstallPart(GgGame& g, int part);
// 2dee:060f Workshop_UninstallPart(part): its sprites and zones removed (the paint undone on shared
// pictures), g_partsInstalled - 1 when required; returns the part.
int workshopUninstallPart(GgGame& g, int part);
void workshopDrawVehicle(GgGame& g);            // 2dee:070a Workshop_DrawVehicle: back to front
void workshopReleaseVehiclePics(GgGame& g);     // 2dee:0797 Workshop_ReleaseVehiclePics
void workshopDrawShelf(GgGame& g);              // 2dee:0804 Workshop_DrawShelf: clipped to x 93..226
// 2dee:0880 Workshop_ScrollStepLeft / 2dee:0986 Workshop_ScrollStepRight: a conveyor step of 4 px with
// its sounds; -1 / 1 while moving, 0 at the end (26 frames for 100 px, the stop effect twice: keep).
int workshopScrollStepLeft(GgGame& g);
int workshopScrollStepRight(GgGame& g);
void workshopRestoreShelfBack(GgGame& g);       // 2dee:0a8c Workshop_RestoreShelfBack: the two rows from the back page
void workshopSetShelfZones(GgGame& g, int part);  // 2dee:0ab3 Workshop_SetShelfZones: 1004 / 1005 at the shelf rectangles
// 2dee:0b3d Workshop_CarryPart(part): the name to the HUD, the cursor picture, the bounds, the
// workbench zone 1006.
void workshopCarryPart(GgGame& g, int part);
// 2dee:0bb2 Workshop_ListShelfParts: the lower-row held parts in list order.
std::vector<int> workshopListShelfParts(GgGame& g);
int partGetPartner(GgGame& g, int part);        // 2dee:0c25 Part_GetPartner: -1 when the pair link is -1
void workshopDropCarried(GgGame& g);            // 2dee:0c4d Workshop_DropCarried (also the Esc hook)

// The blueprint viewer's run (§6; the drawing is BlueprintView's).
// 2dee:17eb Blueprint_Open: Zones_Push, g_blueprintOpen (the facade's workshopBlueprint), the save-under
// (in memory: no gizmo004.dat), g_blueprintSeen = 1, the opening, the title, the Done button,
// arrangement 3.
void blueprintOpen(GgGame& g);
// 2dee:185d Blueprint_Close: the save-under back, the button removed, Blueprint_Free (2dee:174f), the
// arrangement back, Zones_Pop.
void blueprintClose(GgGame& g);
// 2dee:1894 Blueprint_Run: the unpaced viewer loop (pages, hot spots, hint 0x1043 the first time,
// Done); 0, or the menu's leave request 2 / 3. Resume: the viewer is open already (vehiclesRestoreFromState).
int blueprintRun(GgGame& g, StateEntry entry = StateEntry::Normal);

// The vehicles' part of starting from a DOSBox-X state (game/load_state.h): in the workshop (g_state 6)
// the part records' shelf fields and pictures, the vehicle sprite pool and its order, the carried part
// and its cursor, the conveyor animations, the click zones in the original's list order and, when the
// blueprint is open, the viewer; Workshop_Run's locals. Needs the part catalogue and the carried list
// (worldRestoreFromState, actorsRestoreFromState). False with a reason when the moment cannot be resumed
// (races and results: their states start again at their set-up).
bool vehiclesRestoreFromState(GgGame& g, const GgStateFile& s, std::string& why);

}  // namespace tmport::gg
