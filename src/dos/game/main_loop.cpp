// main.c (segment 130f), the main-loop parts: start-up, the frame body, Game_Tick, Frame_Present,
// Mouse_Poll, Game_Shutdown; Sound_SyncState (43cb:046e); the game-side wrappers of input.c (4a85).
// Specs: re/dos/docs/code_runtime.md §3.1-3.4, §3.12, §6.6-6.7; code_ui.md §6.3, §8.1;
// PORTING_NOTES.md §2.3; ENGINE_MAP.md §3.
#include "dos/game/main_loop.h"

#include "dos/game/anim.h"
#include "dos/game/camera.h"
#include "dos/game/cells.h"
#include "dos/game/elves.h"
#include "dos/game/game.h"
#include "dos/game/hints.h"
#include "dos/game/hud.h"
#include "dos/game/init.h"
#include "dos/game/player.h"
#include "dos/game/quiz.h"
#include "dos/game/rank_chest.h"
#include "dos/game/scene.h"
#include "dos/game/search.h"
#include "dos/game/signin.h"
#include "dos/game/sound.h"
#include "dos/game/title.h"
#include "dos/game/ui.h"

namespace tmport::dos {

namespace {

constexpr uint8_t kUiModeTitle = 0x80;   // g_uiMode while Title_Run runs
constexpr uint8_t kUiModeSignIn = 0x08;  // main 130f:0032 after Title_Run

// The tail of a frame after its last dissolve cell (or after a present without one): main
// 130f:00CA Sprites_EraseAll(0, g_spriteSlotsDrawn).
void endFrame(DosGame& g) { spritesEraseAll(g, 0, g.core.spriteSlotsDrawn); }

// A frame of a dissolve that spans frames: the original is still inside the one Frame_Present call
// (no Frame_Begin, no updaters, the sprites of that frame still drawn in the back buffer).
void continueDissolve(DosGame& g) {
    g.dissolveRunning = transitionDissolve(g);
    ++g.screenVersion;
    if (g.dissolveRunning) {
        g.dissolveGate = g.stepGate().done();
        return;
    }
    g.dissolveGate = false;
    endFrame(g);
}

// Ends a frame inside a Button_Flash wait (43cb:0eb6): the original stays in the updater that
// called it, so nothing after it runs and g_stepDone keeps its value into the next frame.
bool stopForFlash(DosGame& g) {
    if (!uiFlashPending(g)) return false;
    g.flashGate = g.stepGate().done();
    return true;
}

// The UI updaters of main 130f:0041..0055. A Button_Flash started by SignIn_Update (sign-in boxes,
// dialogs 14..18) or Menu_Update (dialogs 0..13) blocks the rest of the frame until it ends.
// Returns false when the frame stops there.
bool runUiUpdaters(DosGame& g) {
    signInUpdate(g);
    if (stopForFlash(g)) return false;
    gameoverUpdateMessages(g);
    hintsUpdate(g);
    menuUpdate(g);
    return !stopForFlash(g);
}

// A frame while a Button_Flash wait runs: only the wait advances (uiFlashContinue). When it ends,
// the confirm handler and the interrupted updater's tail have run, and the frame continues after
// that updater: after SignIn_Update with the other three updaters, after Menu_Update with
// Scene_Update. Returns false while the wait goes on.
bool continueFlash(DosGame& g) {
    const bool fromSignIn = g.ui.dialogId >= 14;  // code_ui.md §9.6: dialogs 14..18 belong to the sign-in
    if (!uiFlashContinue(g)) {
        g.flashGate = g.stepGate().done();
        return false;
    }
    if (!fromSignIn) return true;
    gameoverUpdateMessages(g);
    hintsUpdate(g);
    menuUpdate(g);
    return !stopForFlash(g);
}

// One iteration of Title_Run's loop (4ba4:0434); when it ends, the rest of Title_Run and main's
// g_uiMode = 8 (130f:0032), so the next frame is the first of main's loop.
void titleLoopFrame(DosGame& g) {
    if (titleFrame(g)) return;
    titleFinish(g);
    g.titleRunning = false;
    g.core.uiMode = kUiModeSignIn;
}

}  // namespace

// 130f:0007 main, before its loop.
void mainStart(DosGame& g) {
    g.core.quit = 0;
    gameInit(g);
    // Kbd_Install 43a0:0002, Timer_Install 1000:03C5: the runtime's keyboard and virtual PIT.
    titleStart(g);
    g.titleRunning = true;
}

// 130f:0007 main, one iteration of the frame loop (130f:003D..00D1) after g_dirty = 0 and
// Frame_Begin (DosGame::frameBegin).
void mainFrameBody(DosGame& g) {
    if (g.dissolveRunning) {
        continueDissolve(g);
        return;
    }
    if (g.titleRunning) {
        titleLoopFrame(g);
        return;
    }
    if (uiFlashPending(g)) {
        // Integration (wave 3): the Button_Flash contract of ui.h; the frame resumes after the
        // updater that started the wait.
        if (!continueFlash(g)) return;
    } else if (g.world.fullRedraw == 0 && g.world.frameTransition == 0 && g.world.framePartialRedraw == 0) {
        // The UI updaters wait while a full redraw, a transition or a partial (search) redraw is latched.
        if (!runUiUpdaters(g)) return;
    }
    sceneUpdate(g);
    scrollUpdate(g);
    searchUpdate(g);
    quizUpdate(g);
    soundSyncState(g);
    gameHandleHotkeys(g);
    screenDrawOverlay(g);
    playfieldRedraw(g);
    shelfRoomDrawShelf(g);
    rankChestUpdate(g);
    // With any UI mode open only the mouse pointer (slot 0) is drawn.
    g.core.spriteSlotsDrawn = g.core.uiMode == 0 ? 13 : 1;
    spritesDrawSorted(g, 0, g.core.spriteSlotsDrawn);
    framePresent(g);
    if (!g.dissolveRunning) endFrame(g);
    // until g_quit != 0: the runtime ends the run after this frame (DosRuntime::gameStep).
}

// 130f:00fe Game_Tick. The runtime skips it while g_quit != 0 (130f:0103) and runs the 13-tick
// counter and the hint "second" after it (130f:019a..01bd, TickCounters::advance).
void gameTick(DosGame& g, const TickInfo& info) {
    if (info.gated) {
        // A script advanced or the camera moved since the frame began: the scripts wait (Anim_Tick
        // holds them at delay 0) and only the input runs (130f:0190).
        animTick(g);
        inputTick(g);
        return;
    }
    // The player's screen position (x in bytes, SAR: signed), corrected when his world x wrapped
    // into the next row of the strip while the camera has not (unsigned compare with 0x400).
    PlayerState& p = g.player;
    const uint16_t px = p.obj.x, py = p.obj.y, cx = g.world.cameraX, cy = g.world.cameraY;
    p.playerScrX = static_cast<int16_t>(static_cast<int16_t>(px - cx) >> 1);
    p.playerScrY = static_cast<int16_t>(py - cy);
    if (static_cast<uint16_t>(p.playerScrX) > 0x400) {
        p.playerScrX = static_cast<int16_t>(p.playerScrX + (((py & 0xFF80) - (cy & 0xFF80)) << 3));
        p.playerScrY = static_cast<int16_t>((py & 0x7F) - (cy & 0x7F));
    }
    mousePoll(g);
    inputTick(g);
    animTick(g);
    // The world is frozen while the scene of g_level is not loaded yet, except during the title
    // (the walk-off animation runs on these updates; byte compare with 0x80).
    if (g.world.loadedScene == g.core.level || g.core.uiMode == kUiModeTitle) {
        inputMouseWalk(g);
        playerUpdate(g);
        elvesUpdate(g);
        cellsUpdateVisible(g);
    }
}

// 130f:01c2 Frame_Present.
void framePresent(DosGame& g) {
    if (g.core.dirty == 0) return;  // nothing drawn: the 1000-iteration busy loop is dropped
    if (g.world.frameTransition == 0) {
        presentDirty(g.back, g.screen);
        ++g.screenVersion;
        return;
    }
    // A latched transition: the dissolve (700 rand() calls on a fast CPU), Present_Full and
    // g_transition = 0, spread over frames by the original frame clock (camera.h). Note that a
    // transition latched in a frame that drew nothing is not presented (g_dirty is tested first).
    g.dissolveRunning = transitionDissolve(g);
    g.dissolveGate = g.dissolveRunning && g.stepGate().done();
    ++g.screenVersion;
}

// 130f:00db Game_Shutdown.
void gameShutdown(DosGame& g) {
    if (g.shutdownDone) return;
    g.shutdownDone = true;
    musicStop(g);
    sfxStop(g);
    // Timer_Remove, Kbd_Remove, INT 10h AX=0003h and exit(0): the engine ends the run instead.
    g.core.quit = 1;
}

// 130f:0d94 Mouse_Poll (per executed game tick).
void mousePoll(DosGame& g) {
    SpriteSlot& ptr = g.player.slots[0];
    const MousePointer::Result r = g.mouse.poll(g.rt.mouse(), g.core.uiMode, static_cast<int16_t>(ptr.prev));
    if (!r.polled) return;  // no mouse driver (g_mouse != -1)
    // Slot 0 (the pointer) at the mouse, depth 0 (DS:8FD6/8FD8/8FDA), before the script calls.
    ptr.x = static_cast<int16_t>(g.mouse.x);
    ptr.y = static_cast<int16_t>(g.mouse.y);
    ptr.z = 0;
    for (int i = 0; i < r.callCount; ++i) {
        const PointerAnimCall& c = r.calls[static_cast<size_t>(i)];
        switch (c.op) {
            case PointerAnimCall::Op::Set: animSet(g, 0, c.script); break;
            // DS:8FE2 (slot 0's last script) is read when the call is made, after the calls before it.
            case PointerAnimCall::Op::SetIfIdleCurrent: animSetIfIdle(g, 0, g.player.slots[0].prev); break;
            case PointerAnimCall::Op::Clear: animClear(g, 0); break;
        }
    }
}

// 43cb:046e Sound_SyncState (per frame).
void soundSyncState(DosGame& g) {
    CoreState& c = g.core;
    if (c.lastUiMode != c.uiMode) {
        c.lastUiMode = c.uiMode;
        if (c.uiMode == 0) {
            setMusicGate(g, 0xFFFF);  // the music runs again (the timer ANDs the music status with it)
        } else {
            // Any UI mode pauses the music and cuts the effect. On the first frame after the title
            // (g_lastUiMode is BSS 0, g_uiMode 8) this pauses the title tune: the sign-in is silent.
            setMusicGate(g, 0);
            sfxStop(g);
            soundSilenceAll(g);
        }
    }
    if (g.ui.soundChanged == 1) {  // a Music / Sound option changed (MenuCmd_Music, MenuCmd_Sound)
        if (c.musicOn == 0) {
            musicStop(g);
            sfxStop(g);
            soundSilenceAll(g);
        } else {
            musicPlay(g, tuneForArea(c.area));  // nothing from area 15 on
        }
        g.ui.soundChanged = 0;
    }
}

// 4a85:03ac Input_Tick (both Game_Tick paths).
void inputTick(DosGame& g) {
    CoreState& c = g.core;
    switch (g.input.tick(g.bios(), g.mouse, c.uiMode, c.menuLock != 0, c.slowCpu == 1)) {
        case InputTickAction::None: break;
        case InputTickAction::ExitDialog:  // Esc on a slow CPU: the Exit-and-Save box (dialog 10) at once
            c.uiMode = 0x20;
            g.ui.dialogId = 10;
            break;
        case InputTickAction::MenuEscape:  // Esc: the File menu with a queued Enter (g_cmd = 8) on item 2
            c.uiMode = 0x40;
            g.ui.menuOpen = 1;
            g.ui.menuItem = 2;
            break;
        case InputTickAction::MenuOpen:  // F1-F3 or a click on a menu title
            c.uiMode = 0x40;
            g.ui.menuOpen = -1;
            g.ui.menuItem = 0;
            break;
    }
}

// 4a85:0208 Input_MouseWalk (per executed game tick with the scene loaded, before Player_Update).
void inputMouseWalk(DosGame& g) {
    MouseWalkParams p;
    p.playerScreenX = g.player.playerScrX;
    p.playerScreenY = g.player.playerScrY;
    p.uiMode = g.core.uiMode;
    p.scriptedMove = g.core.menuLock != 0;
    p.terrainMask = g.player.playerMask;  // the previous tick's mask (Player_Update writes it later)
    p.mousePresent = g.core.mouse == -1;
    if (const auto prompt = g.input.mouseWalk(g.rt.keyboard().isr(), g.mouse, p)) g.ui.promptIcon = static_cast<uint16_t>(*prompt);
}

}  // namespace tmport::dos
