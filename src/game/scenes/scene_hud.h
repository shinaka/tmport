#pragma once
// A status bar for the scene tools (segment 10e8: Hud_RedrawNear 10e8:0040, Hud_DrawClueBoxMode
// 10e8:00dd, Hud_SetClueBoxMode 10e8:0658 and the element routines; re/docs/code_10e0-10f0.md
// §3.5, §5.1-5.2, REFERENCE_CAPTURES.md §4.2), so headless scene renders show the status bar the
// way the original does (CONTINUE in the clue box while a hint or result screen waits, the answer
// prompt during a question, the counters).
//
// The game's own HUD belongs to the room area; GameMain binds that one into SceneHooks.
// TODO(integration): drop this stand-in from the scene tools once the room area's HUD exists.

#include <memory>
#include <vector>

#include "app/app_context.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/scenes/scene_support.h"
#include "gfx/pixmap.h"

namespace tmport::game::scenes {

class HudStandIn {
public:
    HudStandIn(AppContext& ctx, GameState& s, const GameTables& t);

    int clueBoxMode() const { return mode_; }  // DS:5e53 g_clueBoxMode
    // 10e8:0658 Hud_SetClueBoxMode: hot spots 99 / 0x409, then redraw.
    void setClueBoxMode(int mode);
    // 10e8:0040 Hud_RedrawNear: line, panel, clue box or clue texts + key, counters, gems,
    // present (0,287,512,97), play-field clip.
    void redraw();
    // SceneHooks with setClueBoxMode / hudRedraw bound to this object.
    void bind(SceneHooks& hooks);

private:
    void drawNumber(uint16_t tmplOff, size_t prefix, const std::string& digits, int x, int w);

    AppContext& ctx_;
    GameState& s_;
    const GameTables& t_;
    std::vector<std::shared_ptr<const gfx::PixMap>> art_;  // BNDL 301 (PMAP 1300..1309)
    int mode_ = 0;
};

}  // namespace tmport::game::scenes
