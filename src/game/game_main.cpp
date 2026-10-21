#include "game/game_main.h"

#include <format>
#include <memory>
#include <string>
#include <utility>

#include "anim/anim_engine.h"
#include "app/game_main.h"
#include "app/menu_commands.h"
#include "audio/audio_system.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/room/room_run.h"
#include "game/room_table.h"
#include "game/rules.h"
#include "game/scenes/scenes.h"
#include "game/signin.h"
#include "game/title.h"
#include "mischief/util/log.h"
#include "platform/engine.h"
#include "res/game_files.h"
#include "ui/game_dialog.h"
#include "ui/menu_bar.h"

namespace tmport::game {

namespace {

// AppContext::state for the run: the bootstrap's, or an own one when there is none.
GameState& stateFor(AppContext& ctx, const GameTables& tables, std::unique_ptr<GameState>& own) {
    if (ctx.state) return *ctx.state;
    own = std::make_unique<GameState>(GameState::initial(tables));
    return *own;
}

class GameMain {
public:
    GameMain(AppContext& ctx, const GameMainOptions& options)
        : ctx_(ctx),
          opts_(options),
          tables_(GameTables::load(*ctx.files.exe, *ctx.files.res)),
          prevState_(ctx.state),
          s_(stateFor(ctx, tables_, ownState_)),
          prevServices_(ctx.services),
          signIn_(ctx),
          loop_(ctx, s_, tables_, ctx.engine.rng()) {
        ctx.state = &s_;
        bindHooks();
    }

    ~GameMain() {
        ctx_.services = prevServices_;
        ctx_.state = prevState_;
    }

    GameMain(const GameMain&) = delete;
    GameMain& operator=(const GameMain&) = delete;

    void run();

private:
    template <class... Args>
    void trace(std::format_string<Args...> fmt, Args&&... args) {
        if (!opts_.trace && !mischief::log::verbose()) return;
        const std::string line = std::format(fmt, std::forward<Args>(args)...);
        mischief::log::debug("game: {}", line);
        if (opts_.trace) opts_.trace(line);
    }

    void bindHooks();
    void start();                    // S4
    void resetBeforeSignIn(int r);   // S6 reset block (1118:0062..00dd)
    [[noreturn]] void climbLoop();   // S8..S13, left only through RestartToSignIn / QuitGame
    void setUpClimb();               // S8 (1118:00e5..0257)
    void playClimb();                // S9..S11 (1118:025a..0375)
    void endClimb();                 // S13 (1118:0378..03e8)
    void shutdown();                 // S12 (1118:03eb..0423)
    void saveCurrent(std::string_view why);  // 1160:0f28 Users_SaveCurrent
    void stopMusic();                        // 1128:00ef Music_StopScene
    void playMusic(int zone);                // 1128:0000 Music_PlayZone
    void blackScreen();  // colour 0, Gfx_FillRect(0,0,512,384), Gfx_BlitToWindow(0,0,512,384)
    mischief::BorlandRand& rng() { return ctx_.engine.rng(); }

    AppContext& ctx_;
    GameMainOptions opts_;
    GameTables tables_;
    GameState* prevState_;
    std::unique_ptr<GameState> ownState_;
    GameState& s_;
    GameServices prevServices_;
    SignInScreen signIn_;  // keeps g_userSel (DS:6171) for the later saves
    room::RoomLoop loop_;  // the room modules; its ElfSystem is Elf_InitSystem's state
    scenes::SceneHooks sceneHooks_;
    room::ClimbHooks climbHooks_;
};

void GameMain::bindHooks() {
    AppContext& ctx = ctx_;
    GameState& s = s_;
    // The scenes' links to the HUD, the Exit command and the gate lock (game/scenes/scenes.h).
    sceneHooks_.setClueBoxMode = [this, &s](int m) { loop_.hud().setClueBoxMode(static_cast<uint8_t>(m), s); };
    sceneHooks_.hudRedraw = [this, &s] { loop_.hud().redraw(s); };
    sceneHooks_.exitGame = [&ctx] { app::menuCmdExit(ctx); };  // 1140:0098
    sceneHooks_.clearGateUnlocking = [this] { loop_.clearGateUnlocking(); };
    sceneHooks_.gameHooks = &loop_.gameHooks();
    sceneHooks_.rng = &ctx.engine.rng();  // the generator RoomLoop and setupClimb use
    if (opts_.trace || mischief::log::verbose())
        sceneHooks_.trace = [this](std::string_view ev) { trace("scene: {}", ev); };

    // Room_Run's modal sub-states (S9).
    room::RoomHooks& rh = loop_.hooks();
    rh.exitPrompt = [&ctx] { app::menuCmdExit(ctx); };  // Esc / window close (10e0:0191)
    rh.askQuestion = [this, &ctx, &s](int slot) {      // 1150:100a Quiz_AskQuestion
        trace("S9 elf question: slot {}", slot);
        return scenes::askQuestion(ctx, s, tables_, slot, sceneHooks_).result;
    };
    rh.showHint = [this, &ctx](int event) {  // 1130:0068 Hint_Show
        trace("S9 hint {}", event);
        scenes::showHint(ctx, event, sceneHooks_);
    };
    if (opts_.trace || mischief::log::verbose())
        rh.beforePause = [this](int ms) { trace("room: presentation pause {} ms", ms); };

    // The room change's music and cut-scene (1118:0265..02f0).
    climbHooks_.stopMusic = [this] { stopMusic(); };                   // 1128:00ef Music_StopScene
    climbHooks_.playMusic = [this](int zone) { playMusic(zone); };     // 1128:0000 Music_PlayZone
    climbHooks_.levelCutscene = [this, &ctx](int oldLevel) {  // 1188:0000 LevelCutscene_Play
        trace("S10 level cut-scene: old level {}", oldLevel);
        scenes::playLevelCutscene(ctx, oldLevel, sceneHooks_);
    };

    // The services the app's menu commands call (File > See Rank, Exit).
    ctx.services.seeRank = [this, &ctx, &s] {  // 10f0:0487 Menu_SeeRank
        trace("See Rank");
        scenes::seeRank(ctx, s, tables_, sceneHooks_);
    };
    ctx.services.saveCurrentPlayer = [this] { saveCurrent("exit"); };
}

void GameMain::saveCurrent(std::string_view why) {
    signIn_.saveCurrent();  // a no-op while nobody is signed in
    trace("save: {} (player {}, room {}, total {}, climbs {}, tier {})", why, s_.player.nameString(),
          int(s_.player.room), s_.rank.totalTreasures, s_.rank.climbsDone, int(s_.rank.rankTier));
}

void GameMain::stopMusic() {
    trace("music: stop");
    if (ctx_.audio) ctx_.audio->music().stopScene();
}

void GameMain::playMusic(int zone) {
    trace("music: zone {}", zone);
    if (ctx_.audio) ctx_.audio->music().playZone(zone);
}

void GameMain::blackScreen() {
    gfx::FrameBuffer& fb = ctx_.engine.frame();
    fb.setForeColor(0);
    fb.fillRect(0, 0, 512, 384);
    ctx_.engine.presentAll();
}

// 1118:0000..0049
void GameMain::start() {
    trace("S4 game start");
    // GetResourceInto(900) + Pal_SetFromRGB: the Engine set RRGB 900 when it was created (the
    // original's palette set is one-shot as well).
    if (ctx_.anim) ctx_.anim->resetAll();  // 10b8:00fd Anim_ResetAll
    loop_.elves().init();                  // 1178:01ac Elf_InitSystem
    // Cursor_LoadSet(DS:1292 = {128, 129, 130}): loaded by the Engine; Cursor_SetArrow.
    ctx_.engine.cursors().setArrow();
}

// 1118:0062..00e0
void GameMain::resetBeforeSignIn(int r) {
    s_.rank.rankPromoted = 0;
    ctx_.engine.cursor().setForced(true);  // SetCursorForced(1)
    ctx_.dialogs.musicResumeForce();       // 10d0:0025 Music_ResumeForce
    // RoomSlots_Free, Scene_FreeCommon, Elf_UnloadAll, TreasureHall_UnloadRoom and the player
    // runtime resets (g_caughtElf .. g_coinTargetSlot); Treasures_Free has nothing to free (the
    // treasure pictures are looked up when drawn).
    loop_.resetForRestart();
    if (ctx_.anim) ctx_.anim->resetAll();  // Anim_ResetAll
    if (r != 0) blackScreen();
    if (ctx_.menu) ctx_.menu->remove();    // 1120:000d GameMenu_Remove
}

// 1118:00e5..0257
void GameMain::setUpClimb() {
    Engine& e = ctx_.engine;
    const uint16_t seed = opts_.seed ? *opts_.seed : static_cast<uint16_t>(e.clock().nowMs());
    rng().srand(seed);  // srand(timeGetTime())
    gfx::FrameBuffer& fb = e.frame();
    fb.setForeColor(0);
    fb.fillRect(0, 0, 512, 384);
    fb.fillRect(0, gfx::FrameBuffer::kLayer1Y, 512, 267);
    e.cursors().setWait();  // Cursor_SetWait
    loop_.loadCommon();     // 1198:0000 Scene_LoadCommon
    // TreasureHall_BuildRooms, the new-climb branch (room 0xFE), Clue_BuildTexts/VoiceIds,
    // Elf_SpawnForLevel, Treasures_PickForLevel, the pose from the room, Inv_ResetGems.
    const ClimbSetup cs = setupClimb(s_, tables_, rng(), loop_.gameHooks());
    trace("S8 climb set-up: seed {:#06x}, {} climb, room {}, climbs {}, tier {}, layouts {} {} {}, elves {}", seed,
          cs.newClimb ? "new" : "resumed", int(s_.player.room), s_.rank.climbsDone, int(s_.rank.rankTier),
          cs.layoutRecord[0], cs.layoutRecord[1], cs.layoutRecord[2], loop_.elves().count());
    playMusic(s_.rooms.musicZone(s_.player.room));  // Music_PlayZone(rooms[g_room].music)
    if (ctx_.menu) ctx_.menu->install();  // 1120:0000 GameMenu_Install
    // RoomSlots_Load, TreasureHall_LoadRoom, Hud_SetClueBoxMode(0), Hud_Redraw,
    // Gfx_BlitToWindow(0,0,512,384), Room_Present(1), SetCursorForced(0).
    loop_.startClimb();
}

// 1118:025a..0375
void GameMain::playClimb() {
    while (s_.player.room != kRoomNewClimb) {
        if (s_.player.room == kCastleRoom) {
            trace("S11 castle climb");
            scenes::runCastleClimb(ctx_, s_, tables_, sceneHooks_);  // 1168:0000 ClimbScene_Run
            s_.player.room = kRoomNewClimb;
        } else {
            const room::RoomChange rc = loop_.visitRoom(climbHooks_);
            trace("S9 room {} -> {} (transition {}{}{})", rc.oldRoom, rc.newRoom, int(rc.transition),
                  rc.zoneChanged ? ", zone change" : "", rc.levelChanged ? ", level change" : "");
        }
    }
}

// 1118:0378..03e8
void GameMain::endClimb() {
    trace("S13 climb end");
    stopMusic();  // Music_StopScene
    // Treasures_Free: nothing to free. Elf_UnloadAll, g_numClues = 0, Inv_SetHaveKey(0),
    // Inv_ResetGems.
    endClimbBeforeSave(s_, loop_.gameHooks());
    if (ctx_.menu) ctx_.menu->remove();     // GameMenu_Remove
    ctx_.engine.cursor().setForced(true);   // SetCursorForced(1)
    saveCurrent("climb end");               // location 0xFE, the total before this climb's points
    loop_.freeCommon();                     // Scene_FreeCommon
    blackScreen();
    const CelebrationInfo info = scenes::celebrate(ctx_, s_, tables_, sceneHooks_);  // 10f0:0000
    updateRankTier(s_, tables_);                                                     // 10e8:0790
    trace("S13 celebrated: climbs {}, total {}, rank {}, tier {}{}{}", s_.rank.climbsDone, s_.rank.totalTreasures,
          info.rank, int(s_.rank.rankTier), info.promoted ? ", promoted" : "", info.champion ? ", champion" : "");
}

void GameMain::climbLoop() {
    for (;;) {
        setUpClimb();
        playClimb();
        endClimb();
    }
}

// 1118:03eb..0423
void GameMain::shutdown() {
    trace("S12 shutdown");
    saveCurrent("shutdown");
    ctx_.engine.cursors().setArrow();  // Cursor_SetArrow (Cursor_FreeSet: the Engine owns the set)
    // Scene_FreeCommon, Elf_UnloadAll, RoomSlots_Free, TreasureHall_UnloadRoom; Treasures_Free and
    // Quiz_FreeResources have nothing to free.
    loop_.resetForRestart();
    stopMusic();  // Music_StopScene
}

void GameMain::run() {
    start();
    // S5: Title_Run returns 0 for Esc + Yes. (A QuitGame can only come from the port's own
    // automation here, e.g. the end of a headless script; it ends the game the same way.)
    bool play = false;
    try {
        trace("S5 title");
        play = runTitle(ctx_) == TitleResult::Continue;
    } catch (const QuitGame&) {
        play = false;
    }
    if (play) {
        // S6: r = setjmp(DS:5eda); r == 2 (QuitGame) returns from runRestartPoint.
        app::runRestartPoint([this](int r) {
            trace("S6 restart point: r = {}", r);
            resetBeforeSignIn(r);
            trace("S7 sign-in{}", r != 0 ? " (restart)" : "");
            const SignInResult res = signIn_.run(r != 0);
            trace("S7 signed in: {} ({}), slot {}, room {}", res.name, res.newPlayer ? "new player" : "existing player",
                  res.userSel, int(s_.player.room));
            climbLoop();
        });
    }
    shutdown();
}

}  // namespace

void gameMain(AppContext& ctx, const GameMainOptions& options) {
    GameMain main(ctx, options);
    main.run();
}

void gameMain(AppContext& ctx) { gameMain(ctx, GameMainOptions{}); }

}  // namespace tmport::game
