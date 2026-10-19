// main.c (segment 130f), the main-loop parts: start-up, the frame body, Game_Tick, Frame_Present,
// Mouse_Poll, Game_Shutdown; Sound_SyncState (43cb:046e); the game-side wrappers of input.c (4a85).
// Specs: re/dos/docs/code_runtime.md §3.1-3.4, §3.12, §6.6-6.7, wave 4 corrections; code_ui.md §6.3,
// §8.1; PORTING_NOTES.md §2.3, §2.5; ENGINE_MAP.md §3.
#include "dos/game/main_loop.h"

#include <algorithm>
#include <vector>

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

// ---- The reference frame timing (PORTING_NOTES.md §2.5, code_runtime.md §1.4) ------------------
// What a frame costs on the DOSBox-X reference machine (cycles=fixed 6000), in thousandths of a PIT
// tick (FrameTiming), from what it loaded and drew, counted from where the frame began: right after
// the tick whose work it does, or when a longer frame before it ended (wave 5: the frame after a tick
// runs after it; DosRuntime). The runtime runs the game ticks that fall inside a frame without a
// frame of their own, and skips Game_Tick inside its I/O (g_quit = 1).
//
// I/O with g_quit = 1. The lost game ticks of each kind of I/O come from the tick counters of every
// DOSBox-X state (g_ticks / 5 game ticks fired, DS:90FE = 12 - executed Game_Ticks mod 13) and from
// the STEP gaps of the frame logs: the OPENING load of Title_Run 1 (ega/signin, tandy/signin), a
// MOUNT2N3 or MOUNT3 load 4 (r2/fl_climb, fl_bridge_l3, fl_bridge_l3_jump, fl_geyser_slide; the
// level changes of the EGA climb), MOUNTAIN 5 (r2/fl_bridge_l2), CLOSE 1 and SHACK 1 (ega/castle_top,
// r2/fl_end_t2_wait), a new player's game start (Users_WriteFile, Quiz_LoadQuestions, MOUNTAIN) 7
// (ega/shack_hint1), a resumed game on level 2 (Quiz_LoadQuestions, MOUNT2N3) 5
// (tandy/level2_resumed), the return from the treasure room (Game_End's write, Game_Start, MOUNTAIN)
// 7 (r2/end_t6_clubhouse, ega/after_castle), the treasure room from the clubhouse and back (SHACK,
// MOUNTAIN) 6 (r2/tierb_after_troom), the champion's arrival in the clubhouse (MOUNTAIN, SPRITES.PAK,
// after the chute's last scroll frame) 6 (r2/fl_crown_wait). DOSBox-X serves DOS file reads at once:
// the cost of a PAN is its unpacking, which grows with the packed size; the sprite bank a scene change
// reads with it (CLOSE.PAK, SHACK.PAK, SPRITES.PAK after them) costs 19 units per 1000 bytes.
constexpr uint32_t kPanLoadBase = 3400;      // Scene_LoadPan 1460:05e8: 3.4 PIT ticks
constexpr uint32_t kPanLoadPerKByte = 350;   // + 0.35 per 1000 bytes of the file
constexpr uint32_t kUsersWrite = 4500;       // Users_WriteFile 47c7:055a
constexpr uint32_t kQuizLoad = 7000;         // Quiz_LoadQuestions (Game_Start 47c7:08b5)
// Drawing, from the frame logs' blocked ticks (a tick inside a long frame after one that advanced a
// script or moved the camera is gated) and their frame marks:
//   * a full HUD redraw (Hud_Redraw 130f:07A5 with DS:922E set: the frame fills, the menu bar, the
//     whole play field, the actors, the status bar, then the whole screen presented) lasts 16 PIT
//     ticks: closing the tierc start message with Enter, r2/fl_tierc_outside g_ticks 13805..13836
//     (the tick inside ran Player_Update, the next two were blocked);
//   * a full play-field redraw that a tick asked for (g_fullRedraw latched, no transition: a bridge
//     breaking) lasts 12.5 PIT ticks from that tick: r2/fl_bridge_l3_jump log tick 264 (one tick
//     blocked after the frame's own);
//   * opening a message or hint bubble (the HUD redraw with the frozen actors, then the cloud, its
//     text and Flutter) lasts 27.5 PIT ticks: r2/fl_tierc_outside log ticks 79..82 blocked after the
//     start message opened when the door's dissolve ended, r2/fl_search_t g_ticks 331535..331550
//     behind hint 4, which began when the search's result frame ended;
//   * a scroll frame lasts 6.5 PIT ticks (horizontal), 7 (vertical), 7.5 (both) from the tick that
//     moved the camera (camera.cpp scrollUpdate, kScrollFrameX/Y/XY: the logs' FRAME rings).
// Frames that run past a tick follow each other: r2/fl_bridge_l3 gates log tick 1111 after a scroll
// frame, the bridge's redraw and the fall's first scroll frame ran back to back.
// The dissolve (130f:01C2) and a Button_Flash wait (72 PIT ticks) are long frames the game spreads
// over its frames itself (DosGame::dissolveRunning, uiFlashPending: FrameTiming::continueFrame); the
// frame after one begins when it ends. The dissolve's 350 cells take 30 PIT ticks (127 ms) on the
// reference machine; they start after the rest of the frame that latched the transition, which began
// right after the teleporting tick (or when the frame before it ended): its scene load (if any) with the sprite
// bank, 1 PIT tick of redraw (or a full HUD redraw, 6 PIT ticks in a dissolving frame: the dissolve
// presents), and 5 more when that tick started a tune (the AdLib driver's instrument set-up). The
// doorways with their tune end the dissolve after the 7th tick (r2/fl_l2door_after,
// fl_tierc_outside), the chute without one after the 6th (r2/fl_end_t2_wait); with their loads the
// castle top (CLOSE) after the 7th, the castle gate (SHACK) after the 8th and the champion's arrival
// (MOUNTAIN after the chute's scroll frame) 65..70 PIT ticks after the gate tick (r2/fl_crown_wait).
constexpr uint32_t kHudRedraw = 16000;
constexpr uint32_t kFullRedraw = 12500;
constexpr uint32_t kBubbleCloud = 11500;  // after the HUD redraw that openBubble does first
constexpr uint32_t kDissolveHead = 1000;
// A full HUD redraw in a frame that dissolves: the dissolve presents the screen, so the frame has
// only the drawing part of kHudRedraw (the champion's arrival in the clubhouse, r2/fl_crown_wait).
constexpr uint32_t kHudRedrawDissolving = 6000;
// Reading a sprite bank with a scene (Scene_Update 1437:0034): 19 units per 1000 bytes, SPRITES.PAK
// 2.2 PIT ticks (the champion's arrival loses 6 ticks, the game start 7, the castle top and gate 1).
constexpr uint32_t kPakPerKByte = 19;
constexpr uint32_t kDissolveTuneStart = 5000;
constexpr uint32_t kDissolveLength = 30060;  // 127 ms
constexpr uint32_t kTickUnits = kPitTicksPerGameTick * FrameTiming::kMilli;  // one game tick
constexpr uint32_t kButtonFlashWait = 72;  // Button_Flash 43cb:0eb6: 4 x Delay_Ticks(18) PIT ticks (ui.h)

uint32_t panLoadCost(const DosGame& g, FileIndex pan) {
    return kPanLoadBase + static_cast<uint32_t>(g.files.files().bytes(pan).size() * kPanLoadPerKByte / 1000);
}

// A g_quit section of the frame being run, where the work happens (the I/O routines below report
// themselves): nothing outside a frame (tests and the state loader call those routines directly).
void reportIo(DosGame& g, uint32_t units) {
    FrameTiming& t = g.rt.frameTiming();
    if (t.isOpen()) t.quitSection(units);
}

// What the frame body drew that the reference machine took time for, noted around its calls and
// reported as soon as it is known, in the original's order (wave 5): a sound call later in the frame
// then reaches the driver after it (DosRuntime::soundDelay). The I/O (USERS.DAT writes,
// Quiz_LoadQuestions, PAN and sprite-bank loads) is reported by the routines that do it.
struct FrameCosts {
    explicit FrameCosts(const DosGame& g) : uiMode(g.core.uiMode) {}

    // After the UI updaters (or a Button_Flash's confirm handler): Gameover_UpdateMessages /
    // Hints_Update opened a bubble (g_uiMode 4 / 0x10): the HUD redraw with the frozen actors, the
    // cloud, the text and Flutter, before the rest of the frame (Sound_SyncState pauses the music
    // after it).
    void afterUpdaters(DosGame& g) {
        if (bubble || (uiMode & 0x14) != 0 || (g.core.uiMode & 0x14) == 0) return;
        bubble = true;
        FrameTiming& t = g.rt.frameTiming();
        t.fromPreviousTick();  // asked for before this tick (the end of a transition, the hint clock)
        t.spend((g.core.slowCpu == 1 ? 0 : kHudRedraw - redrawn(g)) + kBubbleCloud);
    }
    // Around Quiz_Update and Screen_DrawOverlay: a full HUD redraw (DS:922E consumed by Hud_Redraw:
    // the quiz's own call, or painter 0; the sign-in and quiz painters are not modelled).
    void beforeHud(const DosGame& g) {
        hudRequest = g.ui.hudRedraw;
        hudScreen = g.ui.screen;
    }
    void afterHud(DosGame& g, bool anyScreen) {
        if (hudRedrawn || hudRequest == 0 || g.ui.hudRedraw != 0 || !(anyScreen || hudScreen == 0)) return;
        hudRedrawn = true;
        g.rt.frameTiming().spend(g.world.frameTransition != 0 && !bubble ? kHudRedrawDissolving
                                                                         : kHudRedraw - (bubble ? 0 : redrawn(g)));
    }
    // Before Playfield_Redraw: the full play-field redraw Frame_Begin latched (frameBeginTiming),
    // kFullRedraw (a HUD redraw in the same frame, which draws the play field itself, counted only the
    // rest of its own cost).
    void beforePlayfield(DosGame& g) {
        if (!g.redrawCostDue) return;
        g.redrawCostDue = false;
        g.rt.frameTiming().spend(kFullRedraw);
    }

    // A full play-field redraw that Frame_Begin latched counts in kFullRedraw (beforePlayfield), so a
    // HUD redraw or a bubble in the same frame counts only the rest of its own cost.
    static uint32_t redrawn(const DosGame& g) { return g.latchedRedraw ? kFullRedraw : 0; }

    uint8_t uiMode;
    uint16_t hudRequest = 0;
    uint8_t hudScreen = 0;
    bool hudRedrawn = false;
    bool bubble = false;
};

// The search's frames on the reference machine (frame_speed = reference): each redraws the searched
// cell's column band and presents it, 4.25 PIT ticks, 4.65 while the found treasure or key is drawn
// above the cell (the 36 frame intervals of r2/fl_typematic, nothing found, and r2/fl_search_t, a
// treasure), the first one 0.5 PIT ticks after the game tick that started the search; they run back to
// back, so a game tick falls inside one of them and the next one begins when it ends. An engine frame
// runs the search frames that begin before the next game tick (FrameTiming::nextTickUnits) and lasts
// until the last of them ends: the 37 frames take 31 game ticks as in the logs (37 in period mode: one
// Search_Update 4a03:0003 per frame), and the frame after the result frame (the hint of a find) begins
// when it ends, before the next tick (r2/fl_search_t: the result frame's FRAME mark at g_ticks 331520,
// the hint frame 331520 -> 331543).
constexpr uint32_t kSearchFirstFrame = 500;  // FrameTiming units after the starting tick
constexpr uint32_t kSearchFrame = 4250;
constexpr uint32_t kSearchContentFrame = 4650;

void searchFrames(DosGame& g) {
    PlayerState& p = g.player;
    const bool running = p.searchTimer > 0 && g.core.uiMode == 0;
    if (!running || g.frameClock().speed() != FrameSpeed::Reference) {
        searchUpdate(g);
        g.searchRunning = p.searchTimer > 0;
        return;
    }
    FrameTiming& t = g.rt.frameTiming();
    const uint64_t start = t.startUnits(), next = t.nextTickUnits();
    // The tick that started the search is the last one before this frame.
    if (!g.searchRunning) g.searchAt = std::max<uint64_t>(start, next - kTickUnits + kSearchFirstFrame);
    while (g.searchAt < next) {
        // The frame stands where this search frame begins (its effects sound there).
        if (g.searchAt > start + t.length()) t.spend(static_cast<uint32_t>(g.searchAt - start - t.length()));
        searchUpdate(g);
        g.searchAt += p.slots[2].frame != 0xFFFF ? kSearchContentFrame : kSearchFrame;
        // Player_Update must see g_searchTimer == 1 to leave state 5: the result step waits for the
        // next frame (with these frame lengths it always falls after a game tick anyway).
        if (p.searchTimer <= 1) break;
    }
    if (g.searchAt > start + t.length()) t.spend(static_cast<uint32_t>(g.searchAt - start - t.length()));
    g.searchRunning = p.searchTimer > 0;
}

// A dissolve starts in this frame (Frame_Present with the transition latched): on the reference
// machine the dissolve runs after the frame's other work (its I/O and drawing as reported so far, then
// kDissolveHead of play-field redraw unless a HUD redraw did it, plus the tune the teleporting tick
// started) for kDissolveLength, and the original's frame ends with it. The runtime runs the ticks
// inside the frame's other work without frames; the dissolve then continues one frame per game tick
// (camera.h transitionDissolve, at the frame clock's rate from a fresh remainder) and must end in the
// frame of the last tick before its end, which reports the rest of the frame (DosGame::dissolveTail):
// this sets the cells of the first frame within the counts that end it there, as close as possible to
// the cells of its own time.
void setDissolveStart(DosGame& g) {
    FrameTiming& t = g.rt.frameTiming();
    t.fromPreviousTick();
    // Positions in units after the frame's start: its other work ends at `before`.
    const uint32_t before = t.length();
    uint32_t start = before + (g.dissolveAfterHud ? 0 : kDissolveHead);
    // The teleporting tick started a tune (the doors set the area and play its tune; the fall shaft
    // and the ice slide change the area without one; a scene change instead loads, and its tune
    // follows later).
    if (!g.latchedLoad && g.musicPlaysAtBegin != g.musicPlaysAtPrevBegin) start += kDissolveTuneStart;
    const uint32_t end = start + kDissolveLength;
    // The following frames: the game ticks after this frame's body (the first one untilNextTick after
    // its start) after the frame's other work and before the dissolve's end.
    const uint32_t next = t.untilNextTick();
    int frames = 0;
    uint32_t last = 0;
    for (uint32_t at = next; at < end; at += kTickUnits) {
        if (at < before) continue;
        ++frames;
        last = at;
    }
    // Cells of j following frames, at the frame clock's rate from a zero remainder.
    auto later = [](int j) {
        OriginalFrameClock c(FrameSpeed::Reference);
        int n = 0;
        for (int f = 0; f < j; ++f) {
            c.beginFrame(kPitTicksPerGameTick);
            n += c.steps(FrameFeature::Dissolve);
        }
        return n;
    };
    const int lo = std::max(0, kDissolveCells - later(frames));
    const int hi = frames == 0 ? kDissolveCells : std::max(lo, kDissolveCells - later(frames - 1) - 1);
    const int own = start >= next ? 0 : static_cast<int>(static_cast<uint64_t>(next - start) * kDissolveCells / kDissolveLength);
    g.frameClock().setSteps(FrameFeature::Dissolve, std::clamp(own, lo, hi));
    // Where the original's frame ends: after the dissolve, which the last following frame reports.
    if (frames == 0) t.spend(end - before);
    g.dissolveTail = frames == 0 ? 0 : end - last;
}

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
    // The original's long frame ends with the dissolve: the next frame begins then (DosRuntime).
    g.rt.frameTiming().spend(g.dissolveTail);
    g.dissolveTail = 0;
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
// Returns false when the frame stops there (its I/O so far still counts).
bool runUiUpdaters(DosGame& g, FrameCosts& costs) {
    signInUpdate(g);
    if (stopForFlash(g)) {
        costs.afterUpdaters(g);
        return false;
    }
    gameoverUpdateMessages(g);
    hintsUpdate(g);
    menuUpdate(g);
    costs.afterUpdaters(g);
    return !stopForFlash(g);
}

// A frame while a Button_Flash wait runs: only the wait advances (uiFlashContinue). When it ends,
// the confirm handler and the interrupted updater's tail have run, and the frame continues after
// that updater: after SignIn_Update with the other three updaters, after Menu_Update with
// Scene_Update. Returns false while the wait goes on.
bool continueFlash(DosGame& g, FrameCosts& costs) {
    const bool fromSignIn = g.ui.dialogId >= 14;  // code_ui.md §9.6: dialogs 14..18 belong to the sign-in
    // The wait ends kButtonFlashWait PIT ticks after it began (ui.h), between two game ticks: the rest
    // of the frame (the confirm handler, a HUD redraw) runs from then on the reference machine, so
    // what it costs and the sound calls it makes count from there.
    const uint32_t elapsed = g.ticks() - g.ui.flashStart;
    if (elapsed >= kButtonFlashWait && elapsed - kButtonFlashWait < kPitTicksPerGameTick)
        g.rt.frameTiming().backdateStart((elapsed - kButtonFlashWait) * FrameTiming::kMilli);
    if (!uiFlashContinue(g)) {
        g.flashGate = g.stepGate().done();
        return false;
    }
    if (!fromSignIn) {
        costs.afterUpdaters(g);
        return true;
    }
    gameoverUpdateMessages(g);
    hintsUpdate(g);
    menuUpdate(g);
    costs.afterUpdaters(g);
    return !stopForFlash(g);
}

void mainFrameParts(DosGame& g, FrameCosts& costs);

// One iteration of Title_Run's loop (4ba4:0434); when it ends, the rest of Title_Run, main's
// g_uiMode = 8 (130f:0032) and at once the first iteration of main's loop (g_dirty = 0, Frame_Begin,
// SignIn_Update's set-up and draw ...) in the same tick interval.
void titleLoopFrame(DosGame& g) {
    if (titleFrame(g)) return;
    titleFinish(g);
    g.titleRunning = false;
    g.core.uiMode = kUiModeSignIn;
    g.core.dirty = 0;
    frameBegin(g);
    frameBeginTiming(g);
    FrameCosts costs(g);
    mainFrameParts(g, costs);
}

// main's loop body after Frame_Begin (130f:0041..00CF), up to where a Button_Flash stops it.
void mainFrameParts(DosGame& g, FrameCosts& costs) {
    if (uiFlashPending(g)) {
        // Integration (wave 3): the Button_Flash contract of ui.h; the frame resumes after the
        // updater that started the wait.
        if (!continueFlash(g, costs)) return;
    } else if (g.world.fullRedraw == 0 && g.world.frameTransition == 0 && g.world.framePartialRedraw == 0) {
        // The UI updaters wait while a full redraw, a transition or a partial (search) redraw is latched.
        if (!runUiUpdaters(g, costs)) return;
    }
    sceneUpdate(g);  // its loads report themselves (reportPanLoad, reportBankLoad)
    scrollUpdate(g);
    searchFrames(g);
    costs.beforeHud(g);
    quizUpdate(g);  // closing the quiz redraws the HUD itself
    costs.afterHud(g, true);
    soundSyncState(g);
    gameHandleHotkeys(g);
    costs.beforeHud(g);
    screenDrawOverlay(g);
    costs.afterHud(g, false);
    costs.beforePlayfield(g);
    playfieldRedraw(g);
    shelfRoomDrawShelf(g);
    rankChestUpdate(g);
    // With any UI mode open only the mouse pointer (slot 0) is drawn.
    g.core.spriteSlotsDrawn = g.core.uiMode == 0 ? 13 : 1;
    spritesDrawSorted(g, 0, g.core.spriteSlotsDrawn);
    // What the frame cost on the reference machine is reported by now: a dissolve starts after it.
    g.dissolveAfterHud = costs.hudRedrawn || costs.bubble;
    framePresent(g);
    if (!g.dissolveRunning) endFrame(g);
    // until g_quit != 0: the runtime ends the run after this frame (DosRuntime::gameStep).
}

}  // namespace

// 130f:0007 main, before its loop.
void mainStart(DosGame& g) {
    g.core.quit = 0;
    gameInit(g);
    // Kbd_Install 43a0:0002, Timer_Install 1000:03C5: the runtime's keyboard and virtual PIT.
    // Title_Run loads OPENING.PAN with g_quit = 1 right after Timer_Install (its cost counts from PIT
    // tick 0, before the first frame): on the reference machine the first game tick falls into that
    // load (one tick lost before the sign-in in every DOSBox-X state), and the title's first frame
    // follows it; its tune starts after it.
    g.rt.frameTiming().open();
    titleStart(g);
    g.titleRunning = true;
}

// 130f:0007 main, one iteration of the frame loop (130f:003D..00D1) after g_dirty = 0 and
// Frame_Begin (DosGame::frameBegin).
void mainFrameBody(DosGame& g) {
    if (g.dissolveRunning) {
        continueDissolve(g);
    } else if (g.titleRunning) {
        titleLoopFrame(g);
    } else {
        FrameCosts costs(g);  // what the frame cost on the reference machine (DosRuntime: long frames)
        mainFrameParts(g, costs);
    }
    // A dissolve or a Button_Flash wait goes on past the next game tick: the original is still in
    // this frame (no Frame_Begin until it ends), so the engine's next frame continues it after its tick
    // with g_stepDone as the ticks left it, and the frame after the long one begins when it ends
    // (DosRuntime, FrameTiming::continueFrame).
    if (g.dissolveRunning || uiFlashPending(g)) g.rt.frameTiming().continueFrame();
}

// The work Frame_Begin's latches start: the scene the ticks asked for (Scene_Update loads it, which
// reports its cost) and a full play-field redraw they asked for (reported before Playfield_Redraw).
void frameBeginTiming(DosGame& g) {
    const WorldState& w = g.world;
    g.latchedLoad = w.sceneWanted != w.loadedScene && g.core.uiMode == 0 && w.sceneWanted < 5;
    g.latchedRedraw = w.fullRedraw != 0 && w.frameTransition == 0 && w.framePartialRedraw == 0;
    g.redrawCostDue = g.latchedRedraw;
    if (g.latchedLoad || g.latchedRedraw) g.rt.frameTiming().fromPreviousTick();
}

void reportPanLoad(DosGame& g, FileIndex pan) { reportIo(g, panLoadCost(g, pan)); }

// CLOSE.PAK / SHACK.PAK for scenes 3 / 4, SPRITES.PAK when a mountain scene follows one of them (or
// the title, or a game start: g_loadedScene 0xFF).
void reportBankLoad(DosGame& g, FileIndex pak) {
    reportIo(g, static_cast<uint32_t>(g.files.files().bytes(pak).size() * kPakPerKByte / 1000));
}

void reportUsersWrite(DosGame& g) { reportIo(g, kUsersWrite); }
void reportQuizLoad(DosGame& g) { reportIo(g, kQuizLoad); }

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
    if (g.frameClock().speed() == FrameSpeed::Reference && !transitionDissolveRunning(g)) setDissolveStart(g);
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
