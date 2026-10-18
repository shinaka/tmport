#include "game/room/room_run.h"

#include <vector>

#include "anim/anim_engine.h"
#include "anim/anim_player.h"
#include "audio/audio_system.h"
#include "audio/engine_input_port.h"
#include "data/exe_tables.h"
#include "game/rules.h"
#include "platform/engine.h"
#include "res/game_files.h"
#include "text/text_renderer.h"
#include "util/log.h"

namespace tmport::game::room {

// The GameHooks of a climb: everything the rules trigger that the room modules own.
class RoomLoop::EngineHooks final : public GameHooks {
public:
    explicit EngineHooks(RoomLoop& loop) : loop_(loop) {}
    void playSfx(int n) override {
        if (loop_.hooks_.sfxObserver) loop_.hooks_.sfxObserver(n);
        if (loop_.ctx_.audio) loop_.ctx_.audio->sfxPlay(n);  // 1170:0000 (gated by g_soundOn)
    }
    void hudChanged() override { loop_.hud_->redraw(loop_.s_); }  // 10e8:0040
    void soundToggled(bool on) override {
        if (fallback) fallback->soundToggled(on);
    }
    void spawnElves(GameState& state, int count) override {  // 1178:0251
        if (!loop_.spawnElves_) {
            loop_.elves_.unloadAll();
            return;
        }
        loop_.elves_.spawnForLevel(state, count, loop_.rng_, loop_.ctx_.files.res.get());
    }
    void unloadElves(GameState&) override { loop_.elves_.unloadAll(); }  // 1178:0364
    GameHooks* fallback = nullptr;

private:
    RoomLoop& loop_;
};

// Held arrows and mouse as dispatched by the last input fetch (DS:06ee..06f8, 06e3).
class RoomLoop::EnginePlayerInput final : public PlayerInput {
public:
    explicit EnginePlayerInput(InputQueue& in) : in_(in) {}
    bool upHeld() override { return in_.upHeld; }
    bool downHeld() override { return in_.downHeld; }
    bool leftHeld() override { return in_.leftHeld; }
    bool rightHeld() override { return in_.rightHeld; }
    int mouseX() override { return in_.mouseX(); }
    int mouseY() override { return in_.mouseY(); }
    bool mouseButtonDown() override { return in_.mouseButtonDown(); }
    uint16_t pollMouseDown() override { return in_.pollMouseDown(); }

private:
    InputQueue& in_;
};

RoomLoop::RoomLoop(AppContext& ctx, GameState& s, const GameTables& t, BorlandRand& rng, RoomHooks hooks,
                   anim::AnimEngine* anim)
    : ctx_(ctx), s_(s), t_(t), rng_(rng), hooks_(std::move(hooks)), rt_(RoomTables::fromExe(*ctx.files.exe)),
      elves_(rt_) {
    Engine& e = ctx_.engine;
    gameHooks_ = std::make_unique<EngineHooks>(*this);
    anim_ = anim ? anim : ctx_.anim;
    if (!anim_) {
        anim::AnimSound sound;
        sound.play = [this](int n) { gameHooks_->playSfx(n); };
        sound.stop = [this] {
            if (ctx_.audio) ctx_.audio->sfxStop();
        };
        ownAnim_ = std::make_unique<anim::AnimEngine>(e.frame(), anim::enginePresent(e), std::move(sound));
        anim_ = ownAnim_.get();
    }
    Engine* ep = &e;  // stored callbacks capture the engine by pointer
    auto present = [ep](int x, int y, int w, int h) { ep->presenter().blitToWindow(ep->frame(), x, y, w, h); };
    hud_ = std::make_unique<Hud>(e.frame(), ctx_.text, e.hotspots(), rt_, present);
    SceneDeps deps{e.frame(),
                   ctx_.files.res.get(),
                   *anim_,
                   rt_,
                   s_,
                   p_,
                   sc_,
                   elves_,
                   *hud_,
                   *gameHooks_,
                   present,
                   [this, ep](int ms) {
                       if (hooks_.beforePause) hooks_.beforePause(ms);
                       ep->delayMs(static_cast<uint32_t>(ms));
                   },
                   [ep] { ep->flushInput(); }};
    scene_ = std::make_unique<RoomScene>(std::move(deps));
    player_ = std::make_unique<PlayerController>(s_, t_, rt_, scene_->frameSizes(), p_, sc_, *gameHooks_);
    player_->setStampGateLock([this] { scene_->gateLockDraw(kLayer1); });
}

RoomLoop::~RoomLoop() = default;

GameHooks& RoomLoop::gameHooks() { return *gameHooks_; }

void RoomLoop::setFallbackHooks(GameHooks* fallback) { gameHooks_->fallback = fallback; }

void RoomLoop::loadCommon() { scene_->loadCommon(); }

void RoomLoop::freeCommon() { scene_->freeCommon(); }

uint16_t RoomLoop::getCode() {
    const uint16_t c = ctx_.engine.getInputCode();
    if (ctx_.audio) ctx_.audio->service();
    return c;
}

void RoomLoop::flushInput() { ctx_.engine.flushInput(); }

// GameMain 1118:0239..0257 (menu and music are the caller's).
void RoomLoop::startClimb() {
    Engine& e = ctx_.engine;
    scene_->loadRoomSlots();
    scene_->hallLoadRoom();
    hud_->setClueBoxMode(0, s_);
    hud_->redraw(s_);
    e.presenter().blitAll(e.frame());
    scene_->present(1);
    e.cursor().setForced(false);
}

// 10e8:0228 Hud_ReplayClues
void RoomLoop::replayClues() {
    Engine& e = ctx_.engine;
    e.presenter().blitAll(e.frame());
    const uint8_t level = s_.currentLevel();
    if (level >= kLevelCount) return;
    for (int i = 0; i < s_.inv.numClues && i < 3; ++i) {
        // 10e8:01ef Hud_SpeakClue: SSND 7300 + id when voice is on, Enter / Esc cut it short (the
        // result is ignored and the next word still plays).
        if (!ctx_.audio) continue;
        audio::EngineInputPort port(e, ctx_.audio);
        ctx_.audio->speak(kClueVoiceBank1 + s_.clues.voice[level][static_cast<size_t>(i)], audio::StopList::ClueReplay,
                          port);
    }
}

void RoomLoop::updateTreasureMarks(bool show) {
    constexpr uint32_t kGold = 0xFF00D7FFu, kCyan = 0xFFFFFF00u;  // packed R,G,B,A: (255,215,0), (0,255,255)
    constexpr int kPad = 3;                                         // outside the 118x133 picture
    std::vector<gfx::Presenter::DebugMark> marks;
    const int room = s_.player.room;
    if (show && ctx_.engine.showTreasure() && RoomTable::valid(room)) {
        for (int i = 0; i < 3; ++i) {
            if (s_.rooms.spotKind(room, i) >= kSpotShop) continue;  // no picture in this slot
            const uint8_t c = s_.rooms.spotContent(room, i);
            if (c != kContentTreasure && c != kContentKey) continue;
            marks.push_back({rt_.slotX[static_cast<size_t>(i)] - kPad, kSlotY - kPad, kSlotW + 2 * kPad,
                             kSlotH + 2 * kPad, c == kContentKey ? kCyan : kGold});
        }
    }
    ctx_.engine.presenter().setDebugMarks(std::move(marks));
}

// One tick of the Room_Run loop body.
void RoomLoop::tick(PlayerInput& in) {
    Engine& e = ctx_.engine;
    gfx::FrameBuffer& fb = e.frame();
    e.timerSet(DeadlineTimers::kFrameSlot, 100);
    // g_unused0b9b is cleared (never set); g_rankPromoted is cleared.
    s_.rank.rankPromoted = 0;
    roomTickHints(s_, t_);  // QueueHint(0), QueueHint(2), room 9: Room9_QueueRankHint
    if (p_.inputLocked) {
        p_.inputEvent = 0;
        flushInput();
    } else {
        p_.inputEvent = getCode();
    }
    if (p_.inputEvent == 0x1B && hooks_.exitPrompt) {
        updateTreasureMarks(false);
        hooks_.exitPrompt();  // 1140:0098 MenuCmd_Exit
    }
    if (p_.inputEvent == 'c' || p_.inputEvent == 'C' || p_.inputEvent == 0x2063) replayClues();
    if (elves_.caughtElf != 0) {
        const int slot = elves_.caughtElf + 3 * s_.currentLevel() - 1;
        updateTreasureMarks(false);
        sc_.elfQuizResult = static_cast<uint8_t>(hooks_.askQuestion ? hooks_.askQuestion(slot) : 0);
        elves_.caughtElf = 0;
        sc_.redrawRequest = 1;
    } else if (s_.hints.pending != -1) {
        const int event = s_.hints.pending;
        updateTreasureMarks(false);
        if (hooks_.showHint) hooks_.showHint(event);  // 1130:0068 Hint_Show
        s_.hints.pending = -1;
        if (sc_.redrawRequest == 0) sc_.redrawRequest = 2;
    }
    p_.prevShopCoins = static_cast<uint8_t>(s_.shop.coinsLeft);
    // RoomExtra_Draw (1108:0001) is empty.
    scene_->hallUpdate();
    fb.setPlayClip(0);
    fb.setDirtyEnabled(true);
    fb.clearDirtyRects();
    // ERASE: restore last tick's sprites from the back page.
    scene_->gateLockErase();
    scene_->fountainErase();
    elves_.eraseAll(fb, s_.player.room);
    scene_->coinPileErase();
    scene_->playerFxErase();
    scene_->playerErase();
    scene_->eraseSlotReveal();
    anim_->eraseSprites(kLayer1);
    // UPDATE
    player_->update(in);
    ElfTick et{s_, p_, sc_, *gameHooks_, rng_};
    elves_.updateAll(et);
    updateCoinPile(s_, rng_, *gameHooks_);  // 1110:00aa
    anim_->step();
    p_.gateWalkStarted = 0;
    // REBUILD
    if (sc_.redrawRequest == 1)
        scene_->drawBackground(0);
    else if (sc_.redrawRequest == 2)
        fb.copyRect(0, kPlayTop + kLayer1, kPlayW, kPlayH, 0, kPlayTop);
    if (static_cast<int>(p_.prevShopCoins) != s_.shop.coinsLeft || sc_.redrawRequest != 0) scene_->netShopDrawSign(0);
    // DRAW
    scene_->drawForeground(0);
    if (sc_.redrawRequest != 0) {
        e.presenter().blitToWindow(fb, 0, kPlayTop, kPlayW, kPlayH);
        hud_->redraw(s_);
        sc_.redrawRequest = 0;
    }
    anim_->flushUpdateRects();
    updateTreasureMarks(true);
    for (;;) {
        e.flushDirtyRects();
        if (e.timerExpired(DeadlineTimers::kFrameSlot)) break;
        fb.clearDirtyRects();
    }
    ++ticks_;
    if (hooks_.afterTick) hooks_.afterTick(ticks_);
}

// 10e0:007d Room_Run
void RoomLoop::run() {
    Engine& e = ctx_.engine;
    HotspotTable& hot = e.hotspots();
    gfx::FrameBuffer& fb = e.frame();
    PlayerState& pl = s_.player;
    p_.nextRoom = pl.room;
    hot.set(kHotPlayField, 0, kPlayTop, kPlayW, kPlayH);
    hot.set(kHotLeftEdge, 0, kPlayTop, 0, kPlayH);
    hot.set(kHotRightEdge, 512, kPlayTop, 0, kPlayH);
    pl.w = scene_->frameSizes().width(pl.frame);
    pl.h = scene_->frameSizes().height(pl.frame);
    p_.bumpStep = 0;
    p_.fxFrame = 0;
    p_.inputLocked = 0;
    elves_.caughtElf = 0;
    p_.playerHidden = 0;
    p_.doorStep = 0;
    p_.coinStep = 0;
    p_.emptySwingStep = 0;
    p_.netStep = 0;
    p_.coinTargetSlot = 0xFF;
    // lastMouse = (-1,-1); Player_UpdateCursor(); Mouse_SetMoveHook(10e0:0000).
    e.cursor().setForced(e.cursor().forced());
    e.cursor().installAsMoveHook();
    fb.setPlayClip(0);
    EnginePlayerInput in(e.input());
    do {
        tick(in);
        if (p_.nextRoom != pl.room && !RoomTable::valid(p_.nextRoom)) {
            // Never commit a room that does not exist (damaged room data, REVIEW_1 M-2): stay.
            static int lastWarned = -1;  // once per (room, exit) pair: the exit is retried every tick
            if (const int key = pl.room << 8 | p_.nextRoom; key != lastWarned) {
                lastWarned = key;
                log::warn("room {}: the exit leads to room {}, which does not exist; staying", int(pl.room),
                          int(p_.nextRoom));
            }
            p_.nextRoom = pl.room;
        }
    } while (p_.nextRoom == pl.room);
    updateTreasureMarks(false);
    hot.remove(kHotPlayField);
    hot.remove(kHotEnterClick);
    hot.remove(kHotLeftEdge);
    hot.remove(kHotRightEdge);
    elves_.eraseAll(fb, pl.room);
    scene_->playerErase();
    // Commit the entry pose.
    pl.room = p_.nextRoom;
    pl.x = p_.entryX;
    pl.y = p_.entryY;
    pl.frame = p_.entryFrame;
    pl.facingLeft = p_.entryFacing;
    p_.inputLocked = 0;
    fb.setDirtyEnabled(false);
    fb.setFullClip();
}

// 1118:0062..00e0 (the room modules' part)
void RoomLoop::resetForRestart() {
    scene_->freeRoomSlots();
    scene_->freeCommon();
    elves_.unloadAll();
    if (ownAnim_) ownAnim_->resetAll();
    scene_->hallUnloadRoom();
    elves_.caughtElf = 0;
    p_.playerHidden = 0;
    p_.doorStep = 0;
    p_.coinStep = 0;
    p_.netStep = 0;
    p_.coinTargetSlot = 0xFF;
    ctx_.engine.frame().setDirtyEnabled(false);
    ctx_.engine.frame().setFullClip();
}

void RoomLoop::leaveRoom() {
    scene_->freeRoomSlots();   // 1198:022e RoomSlots_Free
    scene_->hallUnloadRoom();  // 1190:01c5
}

void RoomLoop::enterRoom(uint8_t transition) {
    scene_->loadRoomSlots();  // 1198:0228 RoomSlots_Load
    scene_->hallLoadRoom();   // 1190:009b
    scene_->present(static_cast<uint8_t>(transition | 0x80));
    if (transition == 1 || transition == 2) p_.jumping = 0;
}

// 1118:0265..02f0
RoomChange RoomLoop::visitRoom(const ClimbHooks& ch) {
    RoomChange rc;
    rc.oldRoom = s_.player.room;
    const int oldZone = s_.rooms.musicZone(rc.oldRoom);
    const int oldLevel = s_.rooms.level(rc.oldRoom);
    run();
    leaveRoom();
    const int room = s_.player.room;
    rc.newRoom = room;
    bool deferMusic = false;
    if (s_.rooms.musicZone(room) != oldZone) {
        rc.zoneChanged = true;
        if (ch.stopMusic) ch.stopMusic();
        // Treasures_Free: the level's treasure pictures are looked up on demand.
        // onRoomChanged: on a level change unload the elves, clear the clues, carry missed
        // questions, clear key and gems, respawn the elves; re-pick the treasures. (The
        // original respawns after the cut-scene; the cut-scene draws no random numbers.)
        if (onRoomChanged(s_, oldZone, oldLevel, t_, rng_, *gameHooks_)) {
            rc.levelChanged = true;
            if (ch.levelCutscene) ch.levelCutscene(oldLevel);
            sc_.gateUnlocking = 0;  // LevelCutscene_Play (also for oldLevel >= 2)
            p_.roomTransition = 1;
        }
        if (room == 0 || room == kMountainStartRoom)
            deferMusic = true;
        else if (ch.playMusic)
            ch.playMusic(s_.rooms.musicZone(room));
    }
    rc.transition = p_.roomTransition;
    enterRoom(p_.roomTransition);
    if (deferMusic && ch.playMusic) ch.playMusic(s_.rooms.musicZone(room));
    return rc;
}

}  // namespace tmport::game::room
