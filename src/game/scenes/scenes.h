#pragma once
// The modal scenes of the climb (area "scenes"), one header each:
//
//   quiz_screen.h     1150:100a Quiz_AskQuestion  (elf question, praise / sorry screens)
//   hint_popup.h      1130:0068 Hint_Show          (New Player Messages)
//   level_cutscene.h  1188:0000 LevelCutscene_Play (gate cut-scenes between levels)
//   celebration.h     10f0:0000 Climb_Celebrate, 10f0:054b RankScreen_Show, 10f0:0487 Menu_SeeRank
//   castle_climb.h    1168:0000 ClimbScene_Run     (room 49)
//   scene_support.h   SceneHooks / SceneIo (shared plumbing)
//
// How GameMain binds them (done in game/game_main.cpp). With `ctx` the AppContext, `s` the
// GameState, `t` the GameTables, `loop` the room area's game::room::RoomLoop and its Hud / room
// state:
//
//   scenes::SceneHooks sh;
//   sh.setClueBoxMode = [&](int m) { hud.setClueBoxMode(uint8_t(m), s); };   // 10e8:0658
//   sh.hudRedraw = [&] { hud.redraw(s); };                                     // 10e8:003b
//   sh.exitGame = [&] { app::menuCmdExit(ctx); };                              // 1140:0098
//   sh.clearGateUnlocking = [&] { roomState.gateUnlocking = 0; };              // DS:2d60
//   sh.gameHooks = &loop.gameHooks();
//   sh.rng = &rng;                                       // the generator RoomLoop uses
//   roomHooks.askQuestion = [&](int slot) { return scenes::askQuestion(ctx, s, t, slot, sh).result; };
//   roomHooks.showHint = [&](int e) { scenes::showHint(ctx, e, sh); };
//   climbHooks.levelCutscene = [&](int oldLevel) { scenes::playLevelCutscene(ctx, oldLevel, sh); };
//   ctx.services.seeRank = [&] { scenes::seeRank(ctx, s, t, sh); };
//   room 49:     scenes::runCastleClimb(ctx, s, t, sh);          // ends with s.player.room = 0xFE
//   climb end:   ... Users_SaveCurrent ... black screen ... scenes::celebrate(ctx, s, t, sh);
//                game::updateRankTier(s, t);
//
// The scenes draw their R(n) calls (quiz R(4), castle layout / creature) from SceneHooks::rng
// (default ctx.engine.rng()): it must be the generator the rest of the climb uses.
// AppContext::anim's FC / FB sound binding (anim_engine.h AnimSound) is the app's; without
// AppContext::anim a scene uses its own DAM engine.

#include "game/scenes/castle_climb.h"
#include "game/scenes/celebration.h"
#include "game/scenes/hint_popup.h"
#include "game/scenes/level_cutscene.h"
#include "game/scenes/quiz_screen.h"
#include "game/scenes/scene_support.h"
