// The HUD of main.c (segment 130f) of the 1990 DOS edition: the screen painter dispatch, the status
// bar with the coin count-up, the key icon and clue words, the prompt icon and the numbers.
// Spec: re/dos/docs/code_ui.md §2.4, §11.1-11.3; code_runtime.md §3.8-3.11.
#include "dos/game/hud.h"

#include <algorithm>
#include <cstdlib>
#include <optional>

#include "dos/game/anim.h"
#include "dos/game/camera.h"
#include "dos/game/game.h"
#include "dos/game/quiz.h"
#include "dos/game/search.h"
#include "dos/game/signin.h"
#include "dos/game/sound.h"
#include "dos/game/ui.h"
#include "dos/gfx/blitter.h"
#include "dos/text/text_draw.h"

namespace tmport::dos {

namespace {

constexpr uint16_t kAreaSignIn = 4;
constexpr uint16_t kNoHudScene = 3;  // scenes 3 and 4 (castle top, treasure room): no painter

// SPRITES frame codes of the status bar (frame * 2).
constexpr uint16_t kFrameCountersBox = 0x78;  // 60: Coins / Net / Treasure box
constexpr uint16_t kFrameBagFull = 0x80, kFrameBagHalf = 0x82, kFrameBagLow = 0x84;  // 64, 65, 66
constexpr uint16_t kFrameNetFull = 0x86, kFrameNetSome = 0x88, kFrameNetNone = 0x8A;  // 67, 68, 69
constexpr uint16_t kFrameTreasureIcon = 0x8C;                                          // 70
constexpr uint16_t kFrameMarkerEmpty = 0x180;                                          // 192
constexpr uint16_t kFrameMarkerFound[3] = {0x182, 0x184, 0x186};                       // 193..195 by level
constexpr uint16_t kFrameClueBox = 0x74;     // 58: "Clue Words" box
constexpr uint16_t kFrameKeyBase = 0x64;     // + key state: 51..56 (found / hidden key of a level)
constexpr uint16_t kFramePromptBox = 0x76;   // 59: the message box
constexpr uint16_t kFramePromptBase = 0x78;  // + prompt: 61..63

// Hud_Redraw 130f:07bc..08a8: the status-bar and play-field frame fills of a full redraw.
struct Fill {
    int x1, y1, x2, y2;
    uint8_t fill;
};
constexpr Fill kFrameFills[] = {
    {0, 0x00, 0x9F, 0x0A, 0xFF}, {0, 0x0B, 0x9F, 0x0E, 0x00}, {3, 0x0E, 0x9C, 0x0E, 0xFF},
    {2, 0x0F, 0x9D, 0x0F, 0xFF}, {0, 0x0E, 0x00, 0x91, 0x00}, {1, 0x0F, 0x01, 0x90, 0x0F},
    {1, 0x12, 0x01, 0x8D, 0xFF}, {0x9F, 0x0E, 0x9F, 0x91, 0x00}, {0x9E, 0x0F, 0x9E, 0x90, 0xF0},
    {0x9E, 0x12, 0x9E, 0x8D, 0xFF}, {0, 0x91, 0x9F, 0xC7, 0x00}, {3, 0x91, 0x9C, 0x91, 0xFF},
    {2, 0x90, 0x9D, 0x90, 0xFF}};

// The found-treasure bits of a level and the clue word bytes, through g_userRec (0 without one).
uint8_t foundMask(const DosGame& g, unsigned level) {
    return g.hasRec() && level < 3 ? g.rec().foundMasks[level] : 0;
}
uint8_t clueWord(const DosGame& g, unsigned i) { return g.hasRec() && i < 9 ? g.rec().clueWords[i] : 0x80; }

// A count-up frame on the reference machine (DOSBox-X, cycles=fixed 6000): the counters box, the
// three numbers, Sfx_Play(4): 3.32 PIT ticks (r2/fl_countup_after: 20 coins in the loop frames that
// begin at g_ticks 44674 .. 44738, one coin each, every 3 or 4 PIT ticks), in thousandths.
constexpr uint64_t kCountUpFrame = 3320;

// How many count-up steps this frame runs. At the reference speed the count-up's frames follow each
// other from the frame after the game tick that changed g_coins: coin k at k * kCountUpFrame after it,
// and the lockstep frame of tick n runs those that begin before tick n + 1; `firstAt` is where the
// first of them begins (thousandths of a PIT tick of g_ticks). At the period speed one per frame (the
// frame clock).
int countUpSteps(DosGame& g, std::optional<uint64_t>& firstAt) {
    UiState& ui = g.ui;
    firstAt.reset();
    if (!g.rt.longFrames()) return g.frameClock().steps(FrameFeature::CoinCountUp);
    const uint64_t tick = uint64_t{g.ticks()} * FrameTiming::kMilli;
    const uint64_t next = tick + 5 * FrameTiming::kMilli;
    auto dueBy = [&ui, next] { return next > ui.countUpStart ? (next - 1 - ui.countUpStart) / kCountUpFrame + 1 : 0; };
    // A new count-up, or one that stopped for a while (the HUD painter did not run): from this tick.
    if (ui.countUpDone == 0 || dueBy() > ui.countUpDone + 2u) {
        ui.countUpStart = tick;
        ui.countUpDone = 0;
    }
    const uint64_t due = dueBy();
    firstAt = ui.countUpStart + uint64_t{ui.countUpDone} * kCountUpFrame;
    const int steps = due > ui.countUpDone ? static_cast<int>(due - ui.countUpDone) : 0;
    const int left = std::abs(int{g.core.coins} - int{ui.coinsShown});
    const int run = std::min(steps, left);
    // Running on while coins are left to count; the next count-up starts its own clock.
    ui.countUpDone = run < left ? static_cast<uint16_t>(ui.countUpDone + run) : 0;
    return run;
}

}  // namespace

// 130f:0c73 Screen_DrawOverlay
void screenDrawOverlay(DosGame& g) {
    if (uiFlashPending(g)) return;  // the original cannot get here while Button_Flash waits
    UiState& ui = g.ui;
    if (ui.hudRedraw != 0 && ui.boxSaved == 1) {
        // Slow CPU: the background MsgBox_Draw saved goes back instead of a full redraw.
        if (g.boxSave) drawSprite(g.back, g.boxSave->image(), 0, 0, sprite_flags::kCgaNormal);
        g.boxSave.reset();
        ui.boxSaved = 0;
        g.sprites.heapOffset = g.sprites.heapSegment = g.sprites.heapBase = 0;  // DS:37C8 / 37CA / 37CC
        if (g.core.area != kAreaSignIn)
            for (int i = 0; i < 3; ++i) menuBarDrawTitle(g, i, false);
        ++g.core.dirty;
        ui.hudRedraw = 0;
    } else if (g.world.sceneWanted < kNoHudScene) {
        callScreenPainter(g, ui.screen);
    }
}

// g_screenPainters DS:0104[screen] (far pointers; a switch here).
void callScreenPainter(DosGame& g, int screen) {
    switch (screen) {
        case 0: hudRedraw(g); break;
        case 1: quizDrawScreen(g); break;
        case 2: signInDraw(g); break;
        default: break;
    }
}

// 130f:07a5 Hud_Redraw
void hudRedraw(DosGame& g) {
    UiState& ui = g.ui;
    CoreState& core = g.core;
    if (ui.hudRedraw != 0) {
        for (const Fill& f : kFrameFills) g.back.fillRect(f.x1, f.y1, f.x2, f.y2, f.fill);
        for (int i = 0; i < 3; ++i) menuBarDrawTitle(g, i, false);
        g.world.fullRedraw = 1;
        playfieldRedraw(g);
        const auto& in = g.world.frameInShelfRoom;
        if (in[0] != 0 || in[1] != 0 || in[2] != 0 || in[3] != 0) {
            for (size_t k = 0; k < 4; ++k) g.world.shelfRoomRedraw[2 * k] = in[k];  // DS:7D7C, 7D7E, 7D80, 7D82
            shelfRoomDrawShelf(g);
        }
        if (ui.hudRedraw != 1) spritesDrawSorted(g, 1, 13);  // 2: the frozen actors under a menu
        ui.hudRedraw = 0;
        ui.hudRedrawReq2 = 0;
        ++core.dirty;
        // Every cache below mismatches, so each part redraws.
        ++ui.hudCoins;
        ++ui.hudNets;
        ++ui.hudFound;
        ++ui.hudKeyState;
        ++ui.hudPrompt;
    }
    if (core.coins != ui.hudCoins || core.nets != ui.hudNets || core.treasuresFound != ui.hudFound ||
        core.coins != ui.coinsShown) {
        drawBankFrame(g, kFrameCountersBox, 0, 0, sprite_flags::kOpaque);
        drawBankFrame(g, core.coins > 10 ? kFrameBagFull : core.coins > 4 ? kFrameBagHalf : kFrameBagLow, 0, 0,
                      sprite_flags::kOpaque);
        drawBankFrame(g, core.nets > 3 ? kFrameNetFull : core.nets > 0 ? kFrameNetSome : kFrameNetNone, 0, 0,
                      sprite_flags::kOpaque);
        drawBankFrame(g, kFrameTreasureIcon, 0, 0, sprite_flags::kOpaque);
        // The treasure markers of the current level (levels 0..2, tiers 0..3): tier + 2 of them.
        if (core.level < 3) {
            if (core.tier < 4) {
                unsigned bit = 1;
                int y = 0xA4;
                for (unsigned k = 0; k < core.tier + 2u; ++k, bit <<= 1, y += 6) {
                    const bool found = (bit & foundMask(g, core.level)) != 0;
                    drawBankFrame(g, found ? kFrameMarkerFound[core.level] : kFrameMarkerEmpty, k % 2 == 0 ? 0x8D : 0x90, y,
                                  sprite_flags::kOpaque);
                }
            }
            ++core.dirty;
        }
        // The count-up: one coin (and effect 4, restarted each time) per original frame; a loss
        // counts down silently. At the reference speed each coin's effect sounds where its frame
        // begins on the count-up's clock (wave 5, FrameTiming::setCallTime).
        std::optional<uint64_t> firstAt;
        const int steps = countUpSteps(g, firstAt);
        FrameTiming& timing = g.rt.frameTiming();
        for (int s = 0; s < steps; ++s) {
            if (ui.coinsShown < core.coins) {
                if (firstAt) {
                    // g_ticks units to the runtime's (they differ by a loaded state's g_ticks).
                    const int64_t rel = static_cast<int64_t>(*firstAt + static_cast<uint64_t>(s) * kCountUpFrame) -
                                        static_cast<int64_t>(uint64_t{g.ticks()} * FrameTiming::kMilli);
                    timing.setCallTime(static_cast<uint64_t>(
                        std::max<int64_t>(0, static_cast<int64_t>(g.rt.pitTicks() * FrameTiming::kMilli) + rel)));
                }
                sfxPlay(g, 4);
                ++ui.coinsShown;
            } else if (core.coins < ui.coinsShown) {
                --ui.coinsShown;
            } else {
                break;
            }
        }
        timing.setCallTime(0);
        hudDrawNumber(g, 0x6E, 0xBA, 0xCCCC, ui.coinsShown);
        hudDrawNumber(g, 0x7D, 0xBA, 0xCCCC, core.nets);
        hudDrawNumber(g, 0x8F, 0xBA, 0xCCCC, core.treasuresFound);
        ui.hudCoins = core.coins;
        ui.hudNets = core.nets;
        ui.hudFound = core.treasuresFound;
    }
    if (core.keyState != ui.hudKeyState) {
        drawBankFrame(g, kFrameClueBox, 0, 0, sprite_flags::kOpaque);
        if (core.keyState != 0)
            drawBankFrame(g, static_cast<uint16_t>(kFrameKeyBase + core.keyState), 6, 0xBE, sprite_flags::kOpaque);
        if (g.world.sceneWanted < kNoHudScene) {
            // The revealed clue words of the scene's level, rows 2, 1, 0 at y 0xBA, 0xAD, 0xA0.
            ui.textInk = 0xCCCC;
            const unsigned base = g.world.sceneWanted * 3u;
            int y = 0xBA;
            for (int k = 2; k >= 0; --k, y -= 13) {
                const uint8_t w = clueWord(g, base + static_cast<unsigned>(k));
                const auto& words = g.tables.ui.clueWordText;
                if (w < 0x80 && w < words.size()) textDraw(g, 0x0C, y, g.dsString(words[w]));
            }
        }
        ui.hudKeyState = core.keyState;
        ++core.dirty;
    }
    if (ui.promptIcon != ui.hudPrompt) hudDrawPromptIcon(g);
}

// 130f:0c2e Hud_DrawPromptIcon
void hudDrawPromptIcon(DosGame& g) {
    drawBankFrame(g, kFramePromptBox, 0, 0, sprite_flags::kOpaque);
    if (g.ui.promptIcon != 0)
        drawBankFrame(g, static_cast<uint16_t>(kFramePromptBase + g.ui.promptIcon), 0, 0, sprite_flags::kOpaque);
    g.ui.hudPrompt = g.ui.promptIcon;
    ++g.core.dirty;
}

// 130f:0d00 Hud_DrawNumber
void hudDrawNumber(DosGame& g, int x, int y, int colour, int value) {
    drawNumber(g.back, g.font(), x, y, static_cast<uint16_t>(colour), value);
}

}  // namespace tmport::dos
