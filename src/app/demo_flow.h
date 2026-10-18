#pragma once
// TEMPORARY game entry until the game area's GameMain (1118:0000) exists. It runs what is built so
// far so that `tmport` shows the real game as far as it goes:
//   Title_Run (game/title.h, screens area; Esc + Yes quits)
//   -> restart point (runRestartPoint: File > Start Another Game comes back here)
//   -> Club Sign-In (game/signin.h, screens area; it loads or creates the player)
//   -> a climb stand-in: the player's room background and its music zone, with the menu bar
//      installed (File / Options / Help all work; Esc = File > Exit, as in the room loop
//      10e0:0191; Exit and Start Another Game save the player through the sign-in's
//      Users_SaveCurrent).
// TODO(integration): replace with the game area's GameMain (climb set-up, room loop, ...).

#include "app/app_context.h"

namespace tmport::app {

void demoGameMain(AppContext& ctx);

}  // namespace tmport::app
