#pragma once
// Starting Gizmos & Gadgets from a DOSBox-X save state (--gg-load-state FILE; the wave 3 fidelity
// work): the state's DGROUP, screen and VRAM go into GgGame (GgStateFile::apply), its PLAYERS.DAT copy
// (<name>.players) into the store, the frame's sprite list (its rectangles: the next Sprites_EraseAll
// erases them), each module rebuilds what it keeps outside DGROUP from the state's RAM (the
// *RestoreFromState functions of the modules' headers: rooms, items, the catalogue and the carried
// lists, the actors' sets and bundles, the roster copy, a running puzzle, the workshop's sprites, a
// race, the hub's screen), and the game goes on from g_state at the module's resume point
// (StateEntry::Resume of the state functions that have one).
//
// The moments the modules resume (re/gg/docs/ENGINE_MAP.md, "Wave 3 map", "Starting from a DOSBox-X
// state"): the hub's frame loop (state 3, flow), the workshop (6), a warehouse room (7), a puzzle (9),
// a race (11). A state saved anywhere else, or one a module cannot rebuild, is refused with the reasons.
//
// Owner: flow agent.

#include <cstdint>
#include <string>
#include <vector>

#include "gg/app/seq_run.h"  // StateEntry

namespace tmport::gg {

class GgGame;
class GgStateFile;

struct GgLoadedState {
    uint32_t ticks = 0;          // g_ticks DS:021a when DOSBox-X saved
    int16_t state = 0;           // g_state DS:455c
    std::string cpu;             // where the CPU was (Ghidra segment:offset of the innermost resident return)
    bool supported = false;      // a module resumes this moment
    std::string why;             // why not (the modules' reasons), when !supported
    std::vector<std::string> warnings;  // what GgStateFile::apply and the optional restores could not do
};

// Loads `s` into `g` (see the file comment) and says where the game goes on. `g` is freshly
// constructed (nothing of Seq_Run has run).
GgLoadedState loadGgState(GgGame& g, const GgStateFile& s);
// Runs the main sequence from the loaded moment: the state function of g_state with
// StateEntry::Resume, then Seq_Run on from its result (with that state as Seq_Run's previous one).
// Returns Seq_Run's result. Throws tmport::Error when the moment is not supported.
int16_t runFromLoadedState(GgGame& g, const GgLoadedState& loaded);
// The state functions that can be resumed (3, 6, 7, 9, 11).
bool isResumableState(int16_t state);

}  // namespace tmport::gg
