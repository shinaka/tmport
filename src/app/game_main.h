#pragma once
// The game entry point the bootstrap runs, and the restart point of GameMain.
//
// The original: WinMain -> GameMain (1118:0000) -> title, then `r = setjmp(DS:5eda)` (1118:004c);
// "Start Another Game" longjmps back with 1 (re-run the sign-in), "Exit" with 2 (shut down)
// (re/docs/ARCHITECTURE.md §2.1/§2.5, code_10f8-1148.md §8, PORTING_NOTES.md §3). In the port the
// longjmps are the exceptions RestartToSignIn / QuitGame (app/app_context.h).

#include <functional>

#include "app/app_context.h"

namespace tmport::app {

// The game's main program (GameMain 1118:0000). The bootstrap calls it once with a fully set-up
// context; returning or throwing QuitGame ends the program. TODO(integration): the game area
// provides the real one (title, restart point, sign-in, climb loop); until then the bootstrap runs
// the temporary demo flow (app/demo_flow.h).
using GameMainFn = std::function<void(AppContext&)>;

// The setjmp at 1118:004c as a loop: calls body(0); each RestartToSignIn thrown out of body calls
// body(1) again; QuitGame (longjmp value 2) returns normally. Any other exception propagates.
// Returning from body also returns (the original's climb loop never returns).
void runRestartPoint(const std::function<void(int restartValue)>& body);

// Runs entry with the top-level policy: QuitGame = normal end; a RestartToSignIn that escapes the
// entry (thrown before it set up its restart point) restarts the entry from the beginning.
// Returns the process exit code (0). Other exceptions propagate to the bootstrap.
int runGameMain(AppContext& ctx, const GameMainFn& entry);

}  // namespace tmport::app
