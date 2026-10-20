#pragma once
// The game's flow (ssg.exe 12e0 main and Seq_Run's table, 2c11 the attract, states 3, 16, 17, the exit
// paths; re/gg/docs/code_runtime.md §2-§4, code_world.md §2, §3, §15.4, code_ui.md §10, §14): what
// ggport runs instead of the wave 2 preview (which stays behind --gg-preview).
//
//   gameMain        runGgApp's entry: the game context (GgGame on the launch's runtime, the sound
//                   facade over GgAudio, PlayersStore in the preference folder), main's start-up, the
//                   main sequence, Game_Shutdown; the tools --gg-trace, --gg-dump-state and
//                   --gg-load-state (game/trace_log.h, game/load_state.h)
//   mainStateTable  Seq_Run's table DS:00c2: state n -> the engine function that ports it (the table's
//                   far pointers are read from the exe and resolved through the overlay stubs, so a
//                   modded table still maps; an unknown pointer is an error)
//
// States and their owners (re/gg/docs/ENGINE_MAP.md, "Wave 3 map"): 1 flow (state01Attract), 2 ui
// (state02SignIn), 3 flow, 4 5 7 8 world, 6 vehicles (state06Workshop), 9 puzzles_a, 10..15 vehicles,
// 16 17 flow.
//
// Owner: flow agent.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "gg/app/seq_run.h"
#include "gg/game/hub.h"
#include "gg/input/zones.h"
#include "gg/ui/attract.h"

namespace tmport::gg {

class GgGame;
class GgStateFile;
struct GgContext;

// The flow module's engine-side objects (GgGame::flowObjects).
struct FlowObjects {
    // State_01_Attract: the full-screen zone DS:0a3a (a click = Enter) while the attract runs, and the
    // four animation descriptors DS:2090 .. DS:20a2 (read at the first attract).
    Zone clickEnter;
    std::optional<AttractAnims> attract;
    // State_03_Hub: the zones DS:0132 (Zones_AddLocalArray links them while the hub runs) and the hub
    // screen (its animations in GgGame::screenAnims, cursor labels, hover regions). A game resumed in
    // the hub from a DOSBox-X state finds the screen set up here by flowRestoreFromState.
    std::array<Zone, 8> hubZones{};
    std::unique_ptr<HubScreen> hub;
    // Where a hub resumed from a state goes on: the next frame, the frame's Timer_Wait(0) (the state
    // was saved in it), or the frame's Input_GetCode.
    enum class HubResume : uint8_t { Frame, Wait, Input };
    HubResume hubResume = HubResume::Frame;
};

// ---- main (12e0)
// 12e0:0b38 main: atexit(Game_Shutdown), the start-up the app has not done (Gfx_Init, Font_Init,
// PicCache_Init are GgGame's), Game_Init, g_state = 2 then 1, Seq_Run(DS:00c2, &g_state), exit(0).
// runGgApp's entry (gg/app/gg_app.h): returns the exit code; GgRunEnd passes through (after
// Game_Shutdown), exit(code) (GgExit) returns its code.
int gameMain(GgContext& ctx);
// 12e0:0afb Game_Init: Actors_Init (18f5:0721), Workshop_Init (2dee:1db5), Hud_Init (1a04:025b),
// SndRes_Init (1cdd:0004), Hint_Init (3060:0013), in this order.
void gameInit(GgGame& g);
// 12e0:0b19 Game_Shutdown (atexit): g_shutdownFlag = -1, Snd_Off, Gfx_Shutdown, Res_CloseFiles,
// Init_Shutdown (the runtime's handlers: the app does that).
void gameShutdown(GgGame& g);
// Seq_Run's main table DS:00c2 (index 0 unused, 1..17 the states, 18 NULL) as engine functions.
// Throws tmport::FormatError for a pointer that is no state function of v1.0.
std::array<StateFn, 19> mainStateTable(GgGame& g);
// The attract's sub-sequence DS:20a8 (index 0 unused, 1..11 the steps of State_01_Attract, 12 NULL) as
// engine functions; throws tmport::FormatError as above.
std::array<StateFn, 13> attractStateTable(GgGame& g);
// 12e0:000f Seq_Run(DS:00c2, &g_state) from the current g_state; returns Seq_Run's result.
int16_t runMainSequence(GgGame& g);

// ---- state 1, the attract (2c11; code_ui.md §10)
// 2c11:0913 State_01_Attract: Mouse_Hide, effect bank 0x1022, music bank 0x1020 playing, the
// full-screen zone DS:0a3a, g_leaveRequest = 2, Seq_Run of the sub-sequence DS:20a8, Pal_FadeOut,
// Zones_ClearLocal, the banks freed, Mouse_Show; returns g_leaveRequest (2 sign-in, -1 quit).
int16_t state01Attract(GgGame& g);
// 2c11:0000 Attract_PlayAnim(anim, fade, flags, arg): the channel scripts at 4 ticks per frame
// (ui/attract.h AttractAnimPlayer), fade 1 Pal_FadeIn / 2 Pal_FadeFromSaved after the first frame;
// flags 2 time-out (arg seconds, timer 1), 4 watch channel arg, 8 Input_GetCode (Enter, Esc, Space
// leave). Returns the last code read (0 on a time-out or a watched end).
int attractPlayAnim(GgGame& g, const AttractAnim& anim, int fade, int flags, int arg);
int16_t attractTitleAnim(GgGame& g);      // 2c11:043f Attract_TitleAnim: the 3 title phases (1, 3, 5 s)
int16_t attractAfterTitle(GgGame& g);     // 2c11:05cf Attract_AfterTitle: DS:2096 until channel 0 ends
int16_t attractStoryPage(GgGame& g);      // 2c11:0630 Attract_StoryPage: text DS:a04e, 0x111c ticks
int16_t attractAfterStory(GgGame& g);     // 2c11:077f Attract_AfterStory: DS:209c until channel 2 ends
int16_t attractBeforePrompt(GgGame& g);   // 2c11:07e5 Attract_BeforePrompt: DS:20a2 until channel 0 ends
int16_t attractSignInPrompt(GgGame& g);   // 2c11:084c Attract_SignInPrompt: time-out 1 = restart the attract

// ---- state 3, the Technology Center (12e0:0148; code_world.md §3)
// 12e0:0148 State_03_Hub: the music (DS:0098, arrangement 2), g_inBuilding = 0, play mode 0, the hub
// screen (game/hub.h HubScreen), the zones DS:0132; 4-tick frames; complete (g_racesWonTotal 15):
// Dlg_MessageRes(0x106d) once; 0x1b / 0xbbc Cmd_SaveAndExit; 0xbb8 -> 2 (sign-in); a building b ->
// Dlg_MessageRes(0x106e) when g_racesWon[b] == 4, else g_building = b and 0 (-> state 4). Resume: the
// frame loop goes on with the screen flowRestoreFromState set up (a DOSBox-X state in the hub).
int16_t state03Hub(GgGame& g, StateEntry entry = StateEntry::Normal);
// The hub codes (DS:0132's release codes; 0xbbc is in the switch, no zone gives it).
inline constexpr uint16_t kHubCodeSignIn = 0x0BB8;
inline constexpr uint16_t kHubCodeBuilding0 = 0x0BB9;  // + b for building b (0 Automotive, 1 Aircraft, 2 Alt. Energy)
inline constexpr uint16_t kHubCodeSaveAndExit = 0x0BBC;

// ---- leaving a building, the end, the exits
// 12e0:012f State_16_SaveAndLeave: Players_SaveCurrent (the building when g_inBuilding),
// Building_Free, returns g_leaveRequest (2 sign-in, 3 hub).
int16_t state16SaveAndLeave(GgGame& g);
// 12e0:0141 State_17_End: 0 (-> entry 18, NULL: Seq_Run ends, exit(0)).
int16_t state17End(GgGame& g);
// 12e0:05d5 Building_Free: Actors_FreePlayerSets, Chimps_Free, Zones_ClearLocal, Menu_SetMenuBar(NULL),
// Room_Leave, Rooms_Free, Actors_LeaveRoom, Parts_FreeCatalogue, List_Clear(the player's list) (the
// carried count +0x70 stays: code_world.md §20 bug 7).
void buildingFree(GgGame& g);
// 4106:00b2 Cmd_SaveAndExit (File > Save & Exit; Esc in the hub, rooms, workshop, puzzles, post-race;
// the sign-in's Esc with g_saveDisabled): Dlg_YesNoRes(0x1064) (default No); Yes: Players_SaveCurrent
// (GgSaveMoment::SaveAndExit) and exit(0) (throws GgExit); returns the box's answer (0 No, -1 Esc:
// the menu then reopens File).
int cmdSaveAndExit(GgGame& g);

// ---- the LZH refill's key drain (1b48:002e; code_runtime.md §6.4, §16.2)
// Kbd_PopQueuedKey until 0 after each 4000-byte read of a compressed resource: keys typed while a
// resource decompresses are lost. Installs the hook that does it for every archive read the calling
// thread makes (gg/data/archive.h setLzhRefillHook: PicCache, Res_LoadCopy, Res_Read, the sound
// banks); `on` false removes it. gameMain installs it on the game's thread; a test that runs a game
// function on the fixture's worker calls it there.
void installLzhKeyDrain(GgGame& g, bool on);

// ---- starting from a DOSBox-X state (game/load_state.h)
// The flow's part: in the hub's frame loop (g_state 3, a return into 12e0:03bf..0479 on the stack), the
// hub screen without its drawing (the state's screen, back page and palette show it), the two
// animations' run-time fields from the running list DS:7a46 (countdown, delay, base, sequence, step,
// state), the zones; the title music handle reloaded into the engine's bank layer (it restarts from its
// beginning: the driver's position is not in the state's DGROUP). False with a reason in `why` for any
// other moment of state 3 (the complete box, a dialog of Save & Exit).
bool flowRestoreFromState(GgGame& g, const GgStateFile& s, std::string& why);

}  // namespace tmport::gg
