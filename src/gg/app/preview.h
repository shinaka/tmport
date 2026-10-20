#pragma once
// Wave 2 preview: replaced by the game modules in wave 3 (ggport runs the game, gg/game/flow.h); kept
// behind --gg-preview and for the wave 2 integration tests.
//
// What ggport ran until the game existed (re/gg/docs/SCOPING.md §9, the wave 2 stop/go point): the
// engine foundation on the user's files, with the original's own code paths where wave 2 has them.
//
//   1. The attract (State_01_Attract 2c11:0913, code_ui.md §10) as the original runs it: the title
//      with its three phases, the after-title animation, the six story pages (30 s each), the
//      animations before the prompt and the sign-in prompt, then again from the title; the title
//      music (bank 0x1020) and the animations' effects (bank 0x1022). Enter goes to the next page,
//      Space (or Enter on the prompt) leaves to the screens below, Esc quits.
//   2. Enter or Space then steps through composed screens, each with the music its state plays
//      (code_sound.md §15) and the original's fades:
//        the sign-in (State_02's screen and roster rows: typing, the list, Up / Down and the
//          buttons work on a read-only copy of the roster; nothing is saved),
//        the hub (State_03's set-up and frame loop: the windmill, the cursor labels; a click on the
//          sign-in booth or a building goes there),
//        a warehouse room of the first race of the building (states 4, 5, 7: the room drawn with its
//          animated objects and items, the HUD and the menu bar; Left / Right enter the next room
//          with the random wipe; the player and the chimps are wave 3's),
//        the eight puzzle screens (variant 0, as state 9 draws them),
//      and back to the attract.
//   Everywhere: number keys play the screen's effects (1 = effect 0, ..., 0 = effect 9; the global
//   bank in the hub, the puzzle's PzSnd bank in the puzzles), Ctrl+S switches the sound off and on
//   (Snd_ToggleOption through Input_GetCode), Esc or closing the window quits.
//
// Input reaches it through gg/input (the key ring, the INT 33h mouse model, click zones), time
// through the virtual PIT (GgRuntime's yield points) and pictures through gg/app/display.h. The sound
// is GgAudio: an SDL stream in a window (ticked by sample count), else ticked by the virtual PIT
// (headless runs, or no audio device). Headless runs can keep it: --wav-dump writes the run's sound
// rendered offline, --opl-log the OPL register writes. The sound calls are logged (--verbose) in
// the sound tool's schedule syntax (gg/audio/soundtest.h), with their sound tick.

#include "gg/app/gg_app.h"

namespace tmport::gg {

// The preview as runGgApp's entry. Returns the exit code (0); GgRunEnd passes through.
int previewMain(GgContext& ctx);

}  // namespace tmport::gg
