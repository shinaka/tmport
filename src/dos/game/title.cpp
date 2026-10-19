// title.c (segment 4ba4) of the 1990 DOS edition: the title page, the two story pages, the Super
// Solvers page, the attract timing and the sparkles. Title_Run (4ba4:0434) is a blocking loop in the
// original; the engine runs it as titleStart, one titleFrame per frame, and titleFinish (ENGINE_MAP
// §3.3). Spec: re/dos/docs/code_ui.md §8.
#include "dos/game/title.h"

#include "dos/data/dgroup_offsets.h"
#include "dos/game/anim.h"
#include "dos/game/game.h"
#include "dos/game/main_loop.h"
#include "dos/game/scene.h"
#include "dos/game/signin.h"
#include "dos/game/sound.h"
#include "dos/game/ui.h"
#include "dos/gfx/blitter.h"

namespace tmport::dos {

namespace {

constexpr uint8_t kModeTitle = 0x80;
constexpr uint16_t kAreaTitle = 6;
constexpr int kPanOpening = 14;      // Scene_LoadPan(14): OPENING.PAN
constexpr int kCtxTitle = 5;         // input context of the title pages
constexpr int kTitleBottomZone = 1;  // zone 1 of context 5: its release command is DS:80DA
constexpr uint32_t kPageTicks = 0x1C20;  // 7200 PIT ticks = 30.42 s per page
constexpr int kCmdEsc = 0, kCmdEnter = 8, kCmdSeries = 13;
constexpr int kPhaseStory2Wait = 5, kPhaseSeriesPage = 10, kPhaseDone = 9;
constexpr int kTitlePhaseCount = static_cast<int>(dg::kTitlePhasesCount);
constexpr int kSlotPlayer = 1, kSlotSparkle = 2;
constexpr int kScriptTitleFigure = 0x5B, kScriptWalkoff = 0x5A, kScriptSparkle = 0x5C;
constexpr int kSparkleSpots = 12;
constexpr int kVideoCga = 2, kVideoTandy = 3;

// The phase table DS:8D83 (near pointers into segment 4ba4): 0 ends the title loop.
bool phaseHasFunction(const DosGame& g, int phase) {
    return phase >= 0 && phase < kTitlePhaseCount && g.exe().ds16(static_cast<uint16_t>(dg::kTitlePhases + 2 * phase)) != 0;
}

// DS:80DA: the release command of the bottom zone of context 5 (8 = Enter, 13 = the S key).
void setBottomZoneCommand(DosGame& g, int8_t cmd) {
    auto& recs = g.inputTables.zones(kCtxTitle).records;
    if (recs.size() > kTitleBottomZone) recs[kTitleBottomZone].release = cmd;
}

}  // namespace

// 4ba4:0434 Title_Run, before its loop
void titleStart(DosGame& g) {
    UiState& ui = g.ui;
    ui.titleSavedLineHeight = ui.lineHeight;
    ui.lineHeight = 10;
    g.core.uiMode = kModeTitle;
    g.core.area = kAreaTitle;
    g.core.quit = 1;  // around the load in the original (ticks paused); the engine's load is instant
    sceneLoadPan(g, kPanOpening);
    g.core.quit = 0;
    ui.titlePhase = 0;
    setMusicLoop(g, true);
    musicPlay(g, 1);
    g.frameClock().reset(FrameFeature::TitleSparkle);
}

// 4ba4:0434 Title_Run, one iteration of its loop
bool titleFrame(DosGame& g) {
    UiState& ui = g.ui;
    if (!phaseHasFunction(g, ui.titlePhase)) return false;
    g.core.dirty = 0;
    gameHandleHotkeys(g);
    callTitlePhase(g, ui.titlePhase);
    // Sparkle attempts while slot 2 is idle and the series page is not up: rand() & 0x7F == 0 starts
    // one at a random spot of DS:8A18 / 8A30. The original tries once per loop frame (~1690/s in
    // DOSBox-X); the engine takes the attempts of this frame from the original frame clock.
    SpriteSlot& sparkle = g.player.slots[kSlotSparkle];
    const int attempts = g.frameClock().steps(FrameFeature::TitleSparkle);
    for (int a = 0; a < attempts; ++a) {
        if (sparkle.anim != 0 || ui.ssPage != 0) break;
        sparkle.frame = 0xFFFF;
        sparkle.prev = 0;
        animClear(g, kSlotSparkle);
        if ((g.rand.rand() & 0x7F) == 0) {
            const int k = g.rand.rand() % kSparkleSpots;
            sparkle.x = g.tables.ui.titleSparkleX[static_cast<size_t>(k)];
            sparkle.y = g.tables.ui.titleSparkleY[static_cast<size_t>(k)];
            animSet(g, kSlotSparkle, kScriptSparkle);
        }
    }
    spritesSnapshot(g, 0, 3);
    g.stepGate().clear();
    spritesDrawSorted(g, 0, 3);
    framePresent(g);
    spritesEraseAll(g, 0, 3);
    return phaseHasFunction(g, ui.titlePhase);
}

// 4ba4:0434 Title_Run, after its loop
void titleFinish(DosGame& g) {
    if (g.core.slowCpu == 1 && (g.core.videoType == kVideoTandy || g.core.videoType == kVideoCga)) {
        setMusicOn(g, false);  // a slow Tandy or CGA machine plays no music in the game
        musicStop(g);
        sfxStop(g);
    }
    g.core.quit = 0;
    g.core.uiMode = 0;
    setMusicLoop(g, false);
    g.ui.lineHeight = g.ui.titleSavedLineHeight;
    animInitSlots(g);
}

// g_titlePhases DS:8D83[phase] (a switch on the phase; phases 9 and 13 have no function).
void callTitlePhase(DosGame& g, int phase) {
    switch (phase) {
        case 0: titleStepDrawTitle(g); break;
        case 1: case 3: case 5: case 11: titleStepWait(g); break;
        case 2: titleStepStory1(g); break;
        case 4: titleStepStory2(g); break;
        case 6: titleStepStartWalkoff(g); break;
        case 7: titleStepWaitWalkoff(g); break;
        case 8: titleStepWaitActorGone(g); break;
        case 10: titleSuperSolversPage(g); break;
        case 12: titleResetPhase(g); break;
        default: break;
    }
}

// 4ba4:000f Title_ClearBox
void titleClearBox(DosGame& g) {
    const bool slow = g.core.slowCpu == 1;
    if (slow) g.back.dirty().clear();
    g.back.fillRect(5, 0x1E, 0x6F, 0xAA, 0xAA);  // light cyan
    if (slow) {
        presentDirty(g.back, g.screen);
        ++g.screenVersion;
    } else {
        ++g.core.dirty;
    }
}

// 4ba4:0045 TitleStep_DrawTitle
void titleStepDrawTitle(DosGame& g) {
    screenDrawBackdrop(g);  // the same 40 tile columns of OPENING.PAN
    SpriteSlot& figure = g.player.slots[kSlotPlayer];
    figure.x = figure.y = figure.z = 0;  // DS:8FEC / 8FEE / 8FF0
    animSet(g, kSlotPlayer, kScriptTitleFigure);
    g.ui.textInk = 0x0000;  // blue, also for the story pages
    textDraw(g, 0x11, 0xA0, g.dsString(dg::kStrTitlePrompt));
    g.input.context = kCtxTitle;
    setBottomZoneCommand(g, kCmdEnter);
    ++g.ui.titlePhase;
    ++g.core.dirty;
}

// 4ba4:00b2 TitleStep_Wait
void titleStepWait(DosGame& g) {
    UiState& ui = g.ui;
    const uint32_t now = g.ticks();
    if (ui.titleTimer == 0) ui.titleTimer = now + kPageTicks;
    if (ui.titleTimer < now) g.input.cmd = static_cast<int16_t>(ui.titlePhase == kPhaseStory2Wait ? kCmdSeries : kCmdEnter);
    const int cmd = g.input.cmd;
    if (cmd < 0) return;
    ui.titleTimer = 0;  // any command restarts the wait (also F1..F3 and the arrows)
    if (cmd == kCmdEsc) {
        if (ui.ssPage == 0) {
            gameShutdown(g);  // Esc on a title page quits to DOS
        } else {
            ui.ssPage = 0;  // on the series page: back to the title
            ui.titlePhase = 0;
        }
    } else if (cmd == kCmdEnter) {
        ++ui.titlePhase;
    } else if (cmd == kCmdSeries && ui.titlePhase == kPhaseStory2Wait) {
        ui.titlePhase = kPhaseSeriesPage;
    }
    g.input.cmd = -1;
    g.input.deliverNext = true;
}

// 4ba4:0156 Title_DrawSprite
void titleDrawSprite(DosGame& g, int x, int y, int frame2, uint8_t flags) {
    drawBankFrame(g, static_cast<uint16_t>(frame2), x, y, flags);
    ++g.core.dirty;
}

// 4ba4:017f TitleStep_Story1
void titleStepStory1(DosGame& g) {
    titleClearBox(g);
    textDraw(g, 6, 0x20, g.dsString(dg::kStrStory1));
    titleDrawSprite(g, 0x0C, 0x48, 0x40, sprite_flags::kMasked);
    textDraw(g, 0x11, 0xA0, g.dsString(dg::kStrTitlePrompt));
    ++g.ui.titlePhase;
    ++g.core.dirty;
}

// 4ba4:01cc TitleStep_Story2
void titleStepStory2(DosGame& g) {
    titleClearBox(g);
    textDraw(g, 6, 0x20, g.dsString(dg::kStrStory2));
    textDraw(g, 10, 0xBD, g.dsString(dg::kStrStory2Keys));
    titleDrawSprite(g, 8, 0x55, 0x42, sprite_flags::kMasked);
    textDraw(g, 0x11, 0xA0, g.dsString(dg::kStrTitlePrompt));
    ++g.ui.titlePhase;
    ++g.core.dirty;
    setBottomZoneCommand(g, kCmdSeries);  // a click on the bottom line = S (the series page)
}

// 4ba4:0233 TitleStep_StartWalkoff
void titleStepStartWalkoff(DosGame& g) {
    screenDrawBackdrop(g);  // removes the box and the texts
    spritesEraseAll(g, 1, 1);
    animSet(g, kSlotPlayer, kScriptWalkoff);
    ++g.ui.titlePhase;
}

// 4ba4:027a TitleStep_WaitWalkoff
void titleStepWaitWalkoff(DosGame& g) {
    SpriteSlot& figure = g.player.slots[kSlotPlayer];
    if (figure.anim == 0 || g.input.cmd == kCmdEnter) {  // Enter cuts the walk short (not consumed)
        ++g.ui.titlePhase;
        figure.frame = 0xFFFF;
        figure.prev = 0;
        animClear(g, kSlotPlayer);
    }
}

// 4ba4:02a3 TitleStep_WaitActorGone
void titleStepWaitActorGone(DosGame& g) {
    if (g.sprites.records[kSlotPlayer].frame == 0xFFFF) g.ui.titlePhase = kPhaseDone;  // draw record 1 (DS:9159)
}

// 4ba4:02b1 Title_SuperSolversPage
void titleSuperSolversPage(DosGame& g) {
    UiState& ui = g.ui;
    const uint16_t savedHeight = ui.lineHeight;
    SpriteSlot& sparkle = g.player.slots[kSlotSparkle];
    sparkle.frame = 0xFFFF;
    sparkle.prev = 0;
    animClear(g, kSlotSparkle);
    ui.lineHeight = 12;
    ui.ssPage = 1;
    spritesEraseAll(g, 1, 1);
    SpriteSlot& figure = g.player.slots[kSlotPlayer];
    figure.frame = 0xFFFF;
    figure.prev = 0;
    g.back.fillRect(0, 0, 0x9F, 0xC7, 0x44);
    g.back.fillRect(1, 0x2E, 0x9E, 0xC5, 0xCC);
    g.back.fillRect(1, 2, 0x9E, 0x2D, 0x00);
    titleDrawSprite(g, 0, 0, 0x48, sprite_flags::kOpaque);
    gfxFillRectColor(g, 0x4C, 0x1D, 0x9B, 0x29, 0x3333);
    gfxFrameRect(g, 1, 2, 0x9E, 0xC5, 0xBBBB);
    // In the ink left by the title page (blue); the texts colour themselves with |NN.
    textDraw(g, 3, 6, g.dsString(dg::kStrSsPage1));
    textDraw(g, 3, 0x31, g.dsString(dg::kStrSsPage2));
    titleDrawSprite(g, 5, 0x3E, 0x46, sprite_flags::kOpaque);
    textDraw(g, 0, 0x3E, g.dsString(dg::kStrSsPage4));
    titleDrawSprite(g, 0x54, 0x78, 0x44, sprite_flags::kOpaque);
    textDraw(g, 3, 0x82, g.dsString(dg::kStrSsPage3));
    ui.textInk = 0xFFFF;
    textDraw(g, 0x28, 0xB9, g.dsString(dg::kStrTitlePrompt));
    ++ui.titlePhase;
    ui.lineHeight = savedHeight;
    setBottomZoneCommand(g, kCmdEnter);
}

// 4ba4:042b Title_ResetPhase
void titleResetPhase(DosGame& g) {
    g.ui.titlePhase = 0;
    g.ui.ssPage = 0;
}

}  // namespace tmport::dos
