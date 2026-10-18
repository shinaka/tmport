#pragma once
// New Player Messages popup (segment 1130: Hint_Show 1130:0068, Hint_WaitDismiss 1130:0166;
// re/docs/code_10f8-1148.md §11.3-11.4, fmt_othr_puzzles.md §9, ARCHITECTURE.md §5.15,
// REFERENCE_CAPTURES.md §4.7).
//
//   speech bubble PMAP 2011 at (33,60), butterfly PMAP 2010 at (430,230) (keyed, onto layer 0
//   only; the room loop restores the play-field from layer 1 afterwards), text = BNDL 327 entry e
//   (OTHR 11500+e) in EGA 0 at (83,92) width 321; full-screen hot spot 0x4B0; music halted;
//   voice SSND 7100+e (Voice on) interruptible by the Hint stop list {Enter, Esc, 0x4B0};
//   then the clue box shows CONTINUE (mode 2) and Enter / 0x409 / Esc / 0x4B0 / 0x24B0 dismiss;
//   music restarted from the beginning, clue box mode 0.

#include <cstdint>

#include "app/app_context.h"
#include "game/scenes/scene_support.h"

namespace tmport::game::scenes {

namespace hint {
inline constexpr int kBubblePmap = 2011;   // 0x7db
inline constexpr int kFlutterPmap = 2010;  // 0x7da, 59x44
inline constexpr int kTextBndl = 327;      // 0x147: OTHR 11500..11532
inline constexpr int kVoiceBase = 7100;    // 0x1bbc
inline constexpr int kBubbleX = 33, kBubbleY = 60;
inline constexpr int kFlutterX = 430, kFlutterY = 230;
inline constexpr int kTextX = 83, kTextY = 92, kTextW = 321;
inline constexpr uint16_t kHotspot = 0x4B0;  // (0,0,512,384)
}  // namespace hint

// 1130:0068 Hint_Show(event). The caller (Room_Run) clears g_hintPending and restores the
// play-field from layer 1 on its next tick (redraw request 2).
void showHint(AppContext& ctx, int event, const SceneHooks& hooks = {});

}  // namespace tmport::game::scenes
