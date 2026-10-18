#pragma once
// The room loop of a climb (segment 10e0:007d Room_Run, re/docs/code_10e0-10f0.md §4.1,
// ARCHITECTURE.md §2.7) and the room part of GameMain's climb loop (1118:00e5..02f0,
// code_10f8-1148.md §8.2).
//
// One 100 ms tick (timer slot 7), in the original's phase order:
//   hints (QueueHint 0 / 2 / room-9 rank hint) -> input (Esc: exit prompt; 'c'/'C'/0x2063: clue
//   replay) -> caught elf: question (RoomHooks::askQuestion), else pending hint
//   (RoomHooks::showHint) -> treasure-hall update -> ERASE (gate lock, fountain, elves, coin pile,
//   player fx, player, slot reveal, DAM sprites) -> UPDATE (Player_Update, elves incl. net catch,
//   coin pile, DAM step) -> REBUILD (redraw 1: background; 2: play-field from the back page) ->
//   net-shop sign -> DRAW (Room_DrawForeground) -> present (HUD on redraw) -> dirty rects until
//   the timer expires;
// until the room changes, then the entry pose is committed.
//
// Modal scenes are not called directly: the hint popup (1130), the elf question (1150) and the
// quit prompt (1140) belong to other modules and are bound through RoomHooks by GameMain. So are
// music and the level cut-scene (ClimbHooks). Everything random uses the BorlandRand passed in,
// which must be the instance GameMain seeds and passes to setupClimb / onRoomChanged.
//
// Typical use by GameMain (one climb; the real binding is game/game_main.cpp):
//     RoomLoop loop(ctx, state, tables, engine.rng(), roomHooks);
//     ... srand; black screen; wait cursor ...
//     loop.loadCommon();                                    // Scene_LoadCommon
//     setupClimb(state, tables, rng, loop.gameHooks());     // elves spawn through the hooks
//     ... Music_PlayZone(zone); GameMenu_Install() ...
//     loop.startClimb();                                    // slots, hall, HUD, Room_Present(1)
//     while (state.player.room != 0xFE) {
//         if (state.player.room == 49) { ClimbScene_Run(); state.player.room = 0xFE; }
//         else loop.visitRoom(climbHooks);                  // Room_Run + the room change
//     }
//     endClimbBeforeSave(state, loop.gameHooks()); ... save ...; loop.freeCommon(); ... celebration
// The hint popup and the elf question switch the status bar's clue box with
// loop.hud().setClueBoxMode(1 | 2 | 0, state) (10e8:0658). After RestartToSignIn / QuitGame
// unwound out of run(), GameMain's reset block calls loop.resetForRestart().

#include <cstdint>
#include <functional>
#include <memory>

#include "app/app_context.h"
#include "game/game_hooks.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/room/elves.h"
#include "game/room/hud.h"
#include "game/room/player.h"
#include "game/room/room_scene.h"
#include "game/room/room_state.h"
#include "game/room/room_tables.h"
#include "rtl/borland_rand.h"

namespace tmport::anim {
class AnimEngine;
}

namespace tmport::game::room {

// Modal scenes and game-flow actions Room_Run triggers. Empty members do nothing (the original
// behaviour minus that scene), so the loop runs headless.
struct RoomHooks {
    // Esc / window close (input 0x1B): 1140:0098 MenuCmd_Exit (Yes/No; Yes saves and throws
    // QuitGame). TODO(integration): menu command handlers (app area).
    std::function<void()> exitPrompt;
    // 1150:100a Quiz_AskQuestion(slot): the elf question for quiz slot 3*level + elf; returns
    // g_elfQuizResult (non-zero = answered correctly, 0 = wrong or Esc).
    // TODO(integration): game/scenes (quiz screen).
    std::function<int(int slot)> askQuestion;
    // 1130:0068 Hint_Show(event): the hint popup. TODO(integration): game/scenes (hints).
    std::function<void(int event)> showHint;
    // Sound effect observer (tools / tests); called for every Sfx_Play besides the audio system.
    std::function<void(int n)> sfxObserver;
    // Called at the end of every tick (after the tick's presentation), with the tick count since
    // the RoomLoop was created (tools / tests).
    std::function<void(uint64_t tick)> afterTick;
    // Called when a presentation pause starts (the black play-field of transition 1, a dissolve
    // pass), with the pause length; the window shows what was just presented (tools / tests).
    std::function<void(int ms)> beforePause;
};

// Music and cut-scene steps of the climb loop's room change (1118:0265..02f0).
struct ClimbHooks {
    std::function<void()> stopMusic;                // 1128:00ef Music_StopScene
    std::function<void(int zone)> playMusic;        // 1128:0000 Music_PlayZone(room.music)
    std::function<void(int oldLevel)> levelCutscene;  // 1188:0000 LevelCutscene_Play (levels 0 and 1)
};

// What visitRoom did.
struct RoomChange {
    int oldRoom = 0, newRoom = 0;
    uint8_t transition = 0;
    bool zoneChanged = false, levelChanged = false;
};

class RoomLoop {
public:
    // anim: the DAM engine to use (null: AppContext::anim, else an own one).
    RoomLoop(AppContext& ctx, GameState& s, const GameTables& t, BorlandRand& rng, RoomHooks hooks = {},
             anim::AnimEngine* anim = nullptr);
    ~RoomLoop();
    RoomLoop(const RoomLoop&) = delete;
    RoomLoop& operator=(const RoomLoop&) = delete;

    // --- climb-level steps (GameMain order) -----------------------------------------------------
    // 1198:0000 Scene_LoadCommon / 1198:00b1 Scene_FreeCommon.
    void loadCommon();
    void freeCommon();
    // The GameHooks the rules need during a climb: Sfx_Play, Hud_RedrawNear, Elf_SpawnForLevel,
    // Elf_UnloadAll (soundToggled goes to `fallback`, the caller's hooks, if set).
    GameHooks& gameHooks();
    void setFallbackHooks(GameHooks* fallback);
    // GameMain step 10 without the menu: RoomSlots_Load, TreasureHall_LoadRoom,
    // Hud_SetClueBoxMode(0), Hud_Redraw, Gfx_BlitToWindow(0,0,512,384), Room_Present(1),
    // SetCursorForced(0).
    void startClimb();
    // 10e0:007d Room_Run: returns when the room has changed (state.player.room = the new room,
    // pose = the entry pose).
    void run();
    // The climb loop's room branch for a room other than 49: Room_Run, RoomSlots_Free,
    // TreasureHall_UnloadRoom, the zone / level change (music stop, game::onRoomChanged, the
    // cut-scene, transition 1), RoomSlots_Load, TreasureHall_LoadRoom, Room_Present(t | 0x80),
    // g_jumping = 0 for transitions 1/2, music (rooms 0 and 9 after the transition).
    RoomChange visitRoom(const ClimbHooks& ch = {});
    // The two halves of visitRoom around the zone/level handling (for a GameMain that wants to
    // do that part itself): leaveRoom = RoomSlots_Free + TreasureHall_UnloadRoom; enterRoom =
    // RoomSlots_Load + TreasureHall_LoadRoom + Room_Present(t | 0x80) + the g_jumping rule.
    void leaveRoom();
    void enterRoom(uint8_t transition);
    // LevelCutscene_Play's gate_unlocking = 0 (visitRoom does it on a level change).
    void clearGateUnlocking() { sc_.gateUnlocking = 0; }
    // The room modules' part of GameMain's reset block before the sign-in (1118:0062..00e0),
    // also needed after RestartToSignIn / QuitGame unwound out of run(): RoomSlots_Free,
    // Scene_FreeCommon, Elf_UnloadAll, TreasureHall_UnloadRoom, g_caughtElf = g_playerHidden =
    // g_doorStep = g_coinStep = g_netStep = 0, g_coinTargetSlot = 0xFF; the dirty list off and the
    // full clip. (Anim_ResetAll of a shared DAM engine and the treasure pictures are the caller's;
    // an own DAM engine is reset here.)
    void resetForRestart();
    // Tools / tests: when false, Elf_SpawnForLevel through gameHooks() places no elves (to
    // reproduce save states whose elves are unknown). Default true.
    void setElfSpawning(bool on) { spawnElves_ = on; }

    // 10e8:0228 Hud_ReplayClues (voice 7300 + id for each earned clue word).
    void replayClues();

    // --- parts and state (other modules: the hint popup and the quiz switch the clue box mode
    // through hud(); GameMain reads the transition) --------------------------------------------------
    Hud& hud() { return *hud_; }
    RoomScene& scene() { return *scene_; }
    ElfSystem& elves() { return elves_; }
    PlayerRuntime& player() { return p_; }
    SceneRuntime& sceneState() { return sc_; }
    const RoomTables& tables() const { return rt_; }
    anim::AnimEngine& anim() { return *anim_; }
    uint64_t ticks() const { return ticks_; }
    RoomHooks& hooks() { return hooks_; }

private:
    class EngineHooks;
    class EnginePlayerInput;
    uint16_t getCode();
    void flushInput();
    void tick(PlayerInput& in);
    // Debug (--show-treasure, F9): presenter boxes around this room's hiding places that hold a
    // treasure or the key; show = false removes them before anything else draws over the room.
    void updateTreasureMarks(bool show);

    AppContext& ctx_;
    GameState& s_;
    const GameTables& t_;
    BorlandRand& rng_;
    RoomHooks hooks_;
    RoomTables rt_;
    std::unique_ptr<anim::AnimEngine> ownAnim_;
    anim::AnimEngine* anim_ = nullptr;
    PlayerRuntime p_;
    SceneRuntime sc_;
    ElfSystem elves_;
    std::unique_ptr<EngineHooks> gameHooks_;
    std::unique_ptr<Hud> hud_;
    std::unique_ptr<RoomScene> scene_;
    std::unique_ptr<PlayerController> player_;
    uint64_t ticks_ = 0;
    bool spawnElves_ = true;
};

}  // namespace tmport::game::room
