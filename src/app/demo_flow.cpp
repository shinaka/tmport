#include "app/demo_flow.h"

#include <string_view>

#include "app/game_main.h"
#include "app/menu_commands.h"
#include "audio/audio_system.h"
#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "game/game_state.h"
#include "game/signin.h"
#include "game/title.h"
#include "input/input_codes.h"
#include "platform/engine.h"
#include "res/game_files.h"
#include "text/text_renderer.h"
#include "ui/dialog_desc.h"
#include "ui/game_dialog.h"
#include "ui/menu_bar.h"
#include "util/log.h"

namespace tmport::app {

namespace {

constexpr int kTickTimer = 7;          // the room loop's timer slot (10e0:007d)

void clearScreen(Engine& e) {
    auto& fb = e.frame();
    fb.resetClip();
    fb.fillRectColor(0, 0, 512, 384, 0);
}

bool drawPmap(AppContext& ctx, int id, int x, int y) {
    try {
        auto pm = ctx.files.res->pixmap(id, ctx.engine.colorMode());
        if (!pm) return false;
        ctx.engine.frame().drawOpaque(x, y, *pm);
        return true;
    } catch (const std::exception& e) {
        log::warn("demo: PMAP {}: {}", id, e.what());
        return false;
    }
}

// The port's own notice (engine text, not game text) in the HUD area of the demo screen.
void drawNotice(AppContext& ctx) {
    auto& fb = ctx.engine.frame();
    fb.fillRectColor(0, 288, 512, 96, 0);
    fb.setForeColor(15);
    auto line = [&](int y, std::string_view s) {
        ctx.text.draw(16, y, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
    };
    line(296, "TM Port preview: the climb is not playable yet.");
    line(316, "Try the menu bar: mouse, Alt+F, Alt+O, Alt+H or F10.");
    line(336, "Esc = File > Exit.  Start Another Game = sign-in.");
}

// The climb stand-in: the player's room (the foot of the mountain for a new climb) with the menu bar.
void runClimbStandIn(AppContext& ctx) {
    Engine& e = ctx.engine;
    const auto rooms = ctx.files.exe->rooms();
    int roomNo = 9;  // climb set-up: room 9 for a first climb, 0 (the clubhouse) afterwards
    if (ctx.state) {
        const auto& st = *ctx.state;
        if (st.player.room < rooms.size()) roomNo = st.player.room;
        else if (st.rank.climbsDone > 0) roomNo = 0;
    }
    const RoomDef& room = rooms[static_cast<size_t>(roomNo)];
    clearScreen(e);
    drawPmap(ctx, room.bgPmap, 0, 20);
    drawNotice(ctx);
    e.presentAll();
    if (ctx.audio) ctx.audio->music().playZone(room.music);
    if (ctx.menu) ctx.menu->install();  // GameMenu_Install (1118: climb set-up step 10)
    e.flushInput();
    for (;;) {
        // One 100 ms tick as in Room_Run: fetch, react, spin until the timer expires.
        e.timerSet(kTickTimer, 100);
        const uint16_t c = e.getInputCode();
        if (c == input::kEsc) menuCmdExit(ctx);  // 10e0:0191: Esc / window close -> MenuCmd_Exit
        while (!e.timerExpired(kTickTimer)) {
        }
    }
}

}  // namespace

void demoGameMain(AppContext& ctx) {
    ctx.engine.setTitle("Treasure Mountain! (TM Port preview)");
    if (ctx.menu) ctx.menu->remove();
    // S5: Title_Run (screens area).
    if (game::runTitle(ctx) == game::TitleResult::Quit) return;
    // S6/S7: the restart point and the Club Sign-In (screens area). The sign-in object keeps the
    // selected users.dat slot for the later saves (Exit, Start Another Game).
    game::SignInScreen signIn(ctx);
    ctx.services.saveCurrentPlayer = [&signIn] { signIn.saveCurrent(); };
    struct ClearServices {
        AppContext& c;
        ~ClearServices() { c.services.saveCurrentPlayer = {}; }
    } clearServices{ctx};
    runRestartPoint([&](int r) {
        // GameMain reset block (1118:004c..): music nesting reset, black screen on restart, menu off.
        ctx.dialogs.musicResumeForce();
        if (r != 0) {
            clearScreen(ctx.engine);
            ctx.engine.presentAll();
        }
        if (ctx.menu) ctx.menu->remove();
        signIn.run(r != 0);
        // S8/S9 stand-in until the game area's climb loop exists.
        runClimbStandIn(ctx);
    });
    // S12 shutdown (1118:03eb): Users_SaveCurrent (a no-op when nobody is signed in).
    signIn.saveCurrent();
}

}  // namespace tmport::app
