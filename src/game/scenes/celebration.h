#pragma once
// End of a climb: celebration animations and the rank screen (segment 10f0 rank.cpp:
// Climb_Celebrate 10f0:0000, RankScreen_Show 10f0:054b, RankScreen_DrawLabels 10f0:06af,
// RankScreen_AnimateStep 10f0:0702, Menu_SeeRank 10f0:0487; re/docs/code_10e0-10f0.md §6,
// ARCHITECTURE.md §2.10, §5.12).
//
// celebrate():
//   game::celebrateClimb (climbsDone++, total += climbPoints, promoted / champion, result hint);
//   music -98 FINALE (now champion) or -96 THRONE; PMAP 2299 at (0,20) + line at y 287, copied to
//   the back page; BNDL 323 (DAM 20300 + frames from slot 0); the delivered treasure (BNDL
//   330 + (climbsDone-1) mod 43, entry 0) in frame slot 80; OTHR 20302; present; DAM 20300, then
//   20302, then (champion) BNDL 324 / DAM 20305 or OTHR 20301 -- all at 4 fps at (0,20), Enter /
//   Space skip the rest, Esc = the Exit dialog (QuitGame on Yes, the animation goes on after No);
//   then PMAP 2202 + DAM 20308 (BNDL 325), and the rank screen.
// showRankScreen():
//   PMAP 2380 at (0,20) + line, "FF05FF %s" (DS:0fe8) with the player name at (134,55) w 236,
//   "FF05FF Total Treasures %lu" (DS:0fee) at (128,217) w 245, the threshold labels (DS:0f8c) at
//   (121, 181-16i); present; sfx 22; for every rank 0..current: PMAP 2381 at (84,86) revealed in a
//   16-row band, 16 steps of 50 ms (timer 4; Esc = Exit dialog), then the rank name (DS:0fa8) at
//   (121, 197-16r) w 259; Sfx_Stop; wait for Enter / Space / a click (hot spot 0x0D over the
//   play-field; Esc = Exit dialog). The band clip is left set, as in the original.
// seeRank(): File > See Rank: saves the play-field, shows the rank screen, restores it.

#include <cstdint>

#include "app/app_context.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/rules.h"
#include "game/scenes/scene_support.h"

namespace tmport::game::scenes {

namespace celebration {
inline constexpr int kFps = 4;
inline constexpr int kBackground1 = 2299;    // 0x8fb
inline constexpr int kBackground2 = 2202;    // 0x89a
inline constexpr int kBndlStart = 323;       // DAM 20300 + frames
inline constexpr int kBndlChampion = 324;    // DAM 20305 + frames
inline constexpr int kBndlFinal = 325;       // DAM 20308 + frames
inline constexpr int kScriptTreasure = 20302;  // 0x4f4e
inline constexpr int kScriptNormal = 20301;    // 0x4f4d
inline constexpr int kAnimX = 0, kAnimY = 20;
}  // namespace celebration

namespace rankscreen {
inline constexpr int kBackground = 2380;   // 0x94c
inline constexpr int kFill = 2381;         // 0x94d, 334x126
inline constexpr int kFillX = 84, kFillY = 86;
inline constexpr int kNameX = 134, kNameY = 55, kNameW = 236;
inline constexpr int kTotalX = 128, kTotalY = 217, kTotalW = 245;
inline constexpr int kLabelX = 121, kLabelY0 = 181, kLabelCount = 7;
inline constexpr int kRankNameX = 121, kRankNameY0 = 197, kRankNameW = 259;
inline constexpr int kBandStep = 16;       // rows per rank
inline constexpr int kBandBottom0 = 213;   // clip bottom for rank 0
inline constexpr int kStepsPerRank = 16;
inline constexpr int kStepMs = 50;         // timer 4
inline constexpr int kPresentX = 84, kPresentW = 334, kPresentH = 16;
inline constexpr int kSfx = 22;
inline constexpr uint16_t kClickHotspot = 0x0D;  // (0,20,512,267): a click acts as Enter
}  // namespace rankscreen

// 10f0:0000 Climb_Celebrate (after GameMain's climb-end save). Returns the rule result.
CelebrationInfo celebrate(AppContext& ctx, GameState& s, const GameTables& t, const SceneHooks& hooks = {});
// 10f0:054b RankScreen_Show (stores g_rank = Rank_FromTotal(total)).
void showRankScreen(AppContext& ctx, GameState& s, const GameTables& t, const SceneHooks& hooks = {});
// 10f0:0487 Menu_SeeRank (File > See Rank).
void seeRank(AppContext& ctx, GameState& s, const GameTables& t, const SceneHooks& hooks = {});

// The band geometry of RankScreen_AnimateStep(rank) step k (0..15): inclusive clip rows.
struct RankBand {
    int clipTop = 0, clipBottom = 0;
    int presentY = 0;
};
RankBand rankBand(int rank, int step);

}  // namespace tmport::game::scenes
