#pragma once
// GgGame: Gizmos & Gadgets' game as one context object handed to every module function
// (`int16_t state07Warehouse(GgGame& g)`): the state (GgState, inherited, so `g.world.rooms`,
// `g.player.points`), the read-only tables, the user's files, the display (Video, Display, the fonts,
// the picture cache, the HUD's pictures), the sound facade, the one Borland rand(), the runtime (the
// clock and the PIT, the deadline timers, the yield points, the keyboard and its ring, the mouse, the
// click zones, Input_GetCode) and the players' store.
//
// Runtime model (re/gg/docs/PORTING_NOTES.md §2.2, ENGINE_MAP.md §3): the original's nested blocking
// loops, as nested functions on one thread; every wait or poll is a GgRuntime yield point (rt), which
// lets the virtual PIT run and pumps the host. Module functions therefore never return to a frame loop
// of the engine: they run until the original's function would return.
//
// Engine-side objects of a module that live longer than one call (pictures, the building's actor sets,
// the running puzzle, the sign-in screen, the workshop's sprites) are in the module's *Objects struct
// (flowObjects() .. vehiclesObjects()); each owner defines its struct in its own header (game/flow.h,
// ui/ui_objects.h, game/world.h, game/actors.h, game/puzzles.h, game/workshop.h) and adds members
// there. GgGame creates one of each.
//
// Owner: skeleton (the constructor and the accessors); the module agents add nothing here.

#include <cstdint>
#include <memory>

#include "gg/app/display.h"
#include "gg/app/gg_runtime.h"
#include "gg/app/options.h"
#include "gg/data/gg_files.h"
#include "gg/game/game_state.h"
#include "gg/game/hud.h"
#include "gg/game/pic_cache.h"
#include "gg/game/screen_anim.h"
#include "gg/game/sound.h"
#include "gg/gfx/video.h"
#include "gg/save/players_store.h"
#include "gg/text/font.h"
#include "gg/ui/dialog.h"
#include "gg/ui/ui_canvas.h"
#include "rtl/borland_rand.h"

namespace tmport::gg {

struct FlowObjects;
struct UiObjects;
struct WorldObjects;
struct ActorsObjects;
struct PuzzlesObjects;
struct VehiclesObjects;

struct GgGameOptions {
    // g_cpuType DS:2450: 2 for Cpu286 (the chimp set-up's path), else 3 here; the game (flow.cpp
    // gameMain) then stores 4, what Cpu_Detect reports on the reference machine (a 486, DOSBox-X).
    GgCpu cpu = GgCpu::Cpu386;
    int16_t soundDevice = -1;    // g_soundDevice DS:2452 (the -S switch; -1 detect)
    // g_displayCode DS:244e (8 after -GV; the check itself is dropped); -1 here, and the game stores
    // 0x1A, Video_DccSupported's answer on the reference machine (VGA), when no -GV was given.
    int16_t displayCode = -1;
    uint16_t seed = 0;           // g_randSeed DS:2454: what the caller passed to rand.srand()
    // Gfx_Init (1473:0074) at construction: the screen cleared, the cursor set up, Pal_Blackout's load
    // run on the retraces (Display::init). Off: the caller does it.
    bool initDisplay = true;
    // Game_Init's Hud_Init (1a04:025b) at construction: the HUD bundle (its palette member into the
    // shadow) and the digit copy.
    bool initHud = true;
};

class GgGame : public GgState {
public:
    // `files`, `runtime`, `sound`, `players` and `rand` must outlive the game. Loads the tables, sets
    // every GgState field to its start-up value (initialState: the exe's initialised data, then the
    // init functions' values; g_cpuType, g_soundDevice, g_displayCode and g_randSeed from `options`),
    // builds the display on the runtime (Display: the mouse handler's screen) and the picture cache,
    // runs Gfx_Init and Hud_Init as `options` say, and installs Ctrl+S (Snd_ToggleOption 1cdd:05c8:
    // DS:1ad4 ^= 1 and g_soundOff ^= 1) as Input_GetCode's sound key. Nothing of Seq_Run runs.
    GgGame(const GgFiles& files, GgRuntime& runtime, SoundFacade& sound, PlayersStore& players, BorlandRand& rand,
           GgGameOptions options = {});
    ~GgGame();
    GgGame(const GgGame&) = delete;
    GgGame& operator=(const GgGame&) = delete;

    // ---- files and read-only data
    const GgFiles& files;
    const GgGameOptions options;
    const GgExe& exe() const { return files.exe(); }
    const Archive& archive() const { return files.archive(); }
    const GgTables tables;
    const DialogStrings strings;  // the boxes' DGROUP strings and keys (code_ui.md §18)

    // ---- services
    GgRuntime& rt;
    SoundFacade& sound;
    BorlandRand& rand;        // the one rand() (seed DS:442a; srand(g_randSeed) at start-up, 303a:0229)
    PlayersStore& players;    // PLAYERS.DAT in the preference folder
    Video video;              // the RAM screen and back page, VRAM, the sprite list, the cursor, the palette
    Display display;          // Video on the runtime's clocks
    Fonts fonts;              // fonts 9 and 10 (Font_Init)
    PicCache cache;           // the picture cache (15df), palettes into video.palette()
    Hud hud;                  // the HUD's pictures (Hud_Init), bound to world.hud
    ScreenAnims screenAnims;  // g_screenAnims DS:7a46 (ScreenAnim_Init 174b:0009): the one running list

    // ---- the runtime's parts (the original's globals they hold: ENGINE_MAP.md "Wave 3 map")
    GgClock& clock() { return rt.clock(); }          // g_ticks DS:021a
    GgTimers& timers() { return rt.timers(); }       // g_timers DS:6578
    InputCodes& input() { return rt.input(); }       // Input_GetCode, g_menuRequest, g_menuDisabled, hooks
    ClickZones& zones() { return rt.zones(); }       // g_zonesLocal / g_zonesGlobal, the zone stack
    GgKeyboard& keyboard() { return rt.keyboard(); } // g_heldKeys, g_kbdPlayMode, the key ring
    GgMouse& mouse() { return rt.mouse(); }          // g_mouseButtons, the latches, g_mouseX / Y
    uint32_t ticks() const { return rt.ticks(); }    // g_ticks DS:021a
    // The UI library's canvas over the game's screen and fonts (Dirty_Flush = display.flush()).
    UiCanvas canvas() { return UiCanvas{video.screen(), fonts, [this] { display.flush(); }}; }

    // ---- the modules' engine-side objects (see the file comment)
    FlowObjects& flowObjects() { return *flow_; }
    UiObjects& uiObjects() { return *ui_; }
    WorldObjects& worldObjects() { return *world_; }
    ActorsObjects& actorsObjects() { return *actors_; }
    PuzzlesObjects& puzzlesObjects() { return *puzzles_; }
    VehiclesObjects& vehiclesObjects() { return *vehicles_; }

private:
    std::unique_ptr<FlowObjects> flow_;
    std::unique_ptr<UiObjects> ui_;
    std::unique_ptr<WorldObjects> world_;
    std::unique_ptr<ActorsObjects> actors_;
    std::unique_ptr<PuzzlesObjects> puzzles_;
    std::unique_ptr<VehiclesObjects> vehicles_;
};

// The globals held by the services (DgroupField::service = true): g_ticks, the timers, the held keys
// and the play mode, the key ring's indexes, the idle tick, g_menuRequest, g_menuDisabled, the mouse's
// buttons and position, the palette's faded flag, shadow and saved copy, the rand() state.
const std::vector<DgroupField>& serviceFields();
// The state and the service-held globals in the DGROUP layout, on the exe's initialised image (or on
// `base` when given): what is compared with a DOSBox-X state's DGROUP (gg/save/gg_state_file.h).
std::vector<uint8_t> dumpToDgroup(const GgGame& g, std::span<const uint8_t> base = {});
// The inverse: GgState from the image (loadFromDgroup) and the service-held globals it can set (the
// rand() state, the held keys and the play mode, g_menuRequest, the idle tick, the palette's faded
// flag, shadow and saved copy into Video; g_ticks and the timers through the clock when `withTime`).
void loadFromDgroup(GgGame& g, std::span<const uint8_t> dgroup, bool withTime = false);

}  // namespace tmport::gg
