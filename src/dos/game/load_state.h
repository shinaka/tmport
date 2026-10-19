#pragma once
// Starting the DOS game from a DOSBox-X save state (--dos-load-state, wave 4 fidelity): the state's
// DGROUP goes into the GameState (loadFromDgroup) and every engine object the original keeps outside
// DGROUP, or that the engine keeps outside the GameState, is rebuilt for that moment, so the next
// frames and the per-tick trace continue as DOSBox-X continued from the same state.
//
// Rebuilt from the state (dos/save/dosbox_state.h):
//   * the globals held by wave-2 objects (ENGINE_MAP.md §4.2): g_keys and the Kbd_ISR flags, the step
//     gate, the tick counters, the Turbo C rand seed, the input poll, the mouse pointer and driver
//     position, the draw records with the save heap, the clip window and DS:37AE, the input zone and
//     key tables, the USERS.DAT image DS:BA14, the BIOS keyboard buffer;
//   * the loaded PAN and the one before it (the stale-buffer quirk) from the map buffer in RAM, the
//     sprite bank from the bank buffer;
//   * the back buffer (segment 2005) and the dirty spans not presented yet (DS:154C); a state saved
//     in the middle of a frame (sprites drawn, draw records marked) gets their backgrounds back from
//     the save heap, as Sprites_EraseAll would;
//   * the screen from the video memory, and the sound driver's effect and tune where they were
//     (soundRestoreDriver);
//   * the time base: g_ticks goes on from the state's value and the next game tick comes on the
//     interrupt DS:3D34 says (DosRuntime::resumeAt).
// Not rebuilt: the frame clock's fractions of per-frame features, an open Button_Flash wait or
// dissolve (engine-side waits a state saved inside one cannot resume), the slow-CPU box save
// (DS:37C8 handle).

#include <cstdint>
#include <string>
#include <vector>

#include "dos/gfx/back_buffer.h"
#include "dos/save/dosbox_state.h"
#include "edition/dos_options.h"

namespace tmport::dos {

class DosGame;

// What loadDosboxState found and did (for the log, the trace and the tests).
struct LoadedState {
    uint32_t ticks = 0;          // g_ticks DS:3D2B when DOSBox-X saved
    uint8_t countdown = 0;       // DS:3D34: the next game tick comes countdown + 1 PIT ticks later
    // The original's g_ticks minus the runtime's ticks() at the same moment: 0 when the runtime was
    // fresh (DosRuntime::resumeAt carries g_ticks on); its first game tick after the load is game
    // tick 1 (firstTickTicks in the original).
    uint32_t ticksOffset = 0;
    uint32_t firstTickTicks = 0; // g_ticks of the first game tick after the load in the original
    bool phaseExact = true;      // the runtime was between game ticks, so the phase could be set
    // Where DOSBox-X stopped the CPU (Ghidra segment:offset), and two cases the phase depends on: a
    // timer interrupt that had not counted DS:3D34 yet (the next game tick comes one PIT tick
    // earlier), and Game_Tick itself (g_inGameTick DS:1309: the rest of that tick is not run; the
    // engine's first tick is the original's next one).
    std::string cpu;
    bool insideTimerBeforeCountdown = false;
    bool insideGameTick = false;
    bool midFrame = false;       // saved between Sprites_DrawSorted and Sprites_EraseAll
    int spritesRestored = 0;     // backgrounds put back from the save heap
    int dirtySpans = 0;          // spans of the dirty lists DS:154C not presented yet
    bool screenFromVideo = false;  // g.screen holds the state's video memory
    std::string pan, prevPan, bank;  // the files found in the map and bank buffers
    int biosKeys = 0;            // words restored into the BIOS keyboard buffer
    int restartedSfx = -1, restartedTune = -1;  // the effect and the tune the restored driver goes on with
    std::vector<std::string> warnings;
};

// The look and the sound device of the machine the state was saved on (g_videoType DS:912C,
// g_soundDevice DS:63BC). Unknown values give EGA and the Tandy device.
Look stateLook(const DosboxState& s);
SoundDevice stateSoundDevice(const DosboxState& s);

// Puts the game into the state's moment (see the header comment). Call it instead of mainStart:
// `g` is freshly constructed (gameInit may have run), its runtime not stepped yet (otherwise between
// game ticks: the phase is set, but g_ticks keeps the runtime's count; see ticksOffset). The users store gets the state's in-memory USERS.DAT image; the caller decides
// what the store's file holds (the state's sibling .users copy, dosbox_state.h). The look of the
// game's options is used for drawing; when it is not the state's look, g_videoType follows the option.
LoadedState loadDosboxState(DosGame& g, const DosboxState& s);

// The frame the original showed when it last presented, as the engine draws it from the loaded state:
// the back buffer with the sprites of the draw records (Sprites_DrawSorted over g_spriteSlotsDrawn).
// Equals the state's screen (DosboxState::screen) unless the state was saved between a Frame_Begin
// that moved a sprite and the next present. Changes nothing in `g`.
BackBuffer composeShownFrame(const DosGame& g);

}  // namespace tmport::dos
