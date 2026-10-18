#pragma once
// The status bar (segment 10e8 status.cpp; re/docs/code_10e0-10f0.md §3.5, §5.1-5.2, pixel-checked
// in REFERENCE_CAPTURES.md §4.2): line and panel, the clue box (mode 0 clue words, 1 "click on an
// answer" prompt, 2 CONTINUE button) with hot spots 99 / 0x409, the key icon, coins and nets (two
// digits), the treasure gem icons and the climb's treasure counter. Art = BNDL 301 (PMAP
// 1300..1309), read from the user's files.
//
// Hud_RedrawNear is called after every inventory change (GameHooks::hudChanged) and presents the
// status bar (0,287,512,97) at once. The hint popup (1130) and the elf question (1150) switch the
// clue box mode through setClueBoxMode().

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "game/game_state.h"
#include "game/room/room_tables.h"
#include "gfx/frame_buffer.h"
#include "gfx/pixmap.h"

namespace tmport {
class ResourceManager;
class HotspotTable;
}  // namespace tmport

namespace tmport::text {
class TextRenderer;
}

namespace tmport::game::room {

// HUD layout (code constants of 10e8).
namespace hud {
inline constexpr int kArtCount = 10;
inline constexpr int kArtPanel = 6, kArtGemFilled = 7, kArtGemHollow = 8, kArtContinue = 9;
inline constexpr int kPanelX = 5, kPanelY = 291;
inline constexpr int kLineY = 287, kFillY = 288, kFillH = 96;
inline constexpr int kLineColour = 0, kFillColour = 8, kTextColour = 15, kClueBoxColour = 9;
inline constexpr int kClueBoxX = 10, kClueBoxY = 296, kClueBoxW = 190, kClueBoxH = 76;
inline constexpr int kPromptY = 310;
inline constexpr int kClueX = 17, kClueY = 315, kClueDy = 19;
inline constexpr int kKeyX = 131, kKeyY = 296;
inline constexpr int kCoinsX = 271, kCoinsW = 32, kNetsX = 361, kNetsW = 41, kPointsX = 463, kPointsW = 41;
inline constexpr int kCounterY = 339;
inline constexpr int kPresentY = 287, kPresentH = 97;
}  // namespace hud

// Counter strings: the template (FF 05 FF '0' '0' NUL, DS:0f5a / 0f60) with the decimal digits of
// v written at index 3: one digit below 10, else the last two digits (10e8:02cf / 034d).
std::vector<uint8_t> hudCounterText(std::span<const uint8_t> tmpl, uint16_t v);
// The 1..5 digit treasure counter on the 9-byte template DS:0f66 (10e8:04b2).
std::vector<uint8_t> hudPointsText(std::span<const uint8_t> tmpl, uint16_t v);

class Hud {
public:
    using PresentFn = std::function<void(int x, int y, int w, int h)>;

    Hud(gfx::FrameBuffer& fb, text::TextRenderer& text, HotspotTable& hotspots, const RoomTables& rt, PresentFn present)
        : fb_(fb), text_(text), hot_(hotspots), rt_(rt), present_(std::move(present)) {}

    // 10e8:0000 Hud_LoadArt (BNDL 301) / 10e8:0019 Hud_FreeArt.
    void loadArt(const ResourceManager& res);
    void freeArt();
    bool hasArt() const { return art_[hud::kArtPanel] != nullptr; }
    const gfx::PixMap* art(int i) const {
        return i >= 0 && i < hud::kArtCount ? art_[static_cast<size_t>(i)].get() : nullptr;
    }
    // For tests: install art by hand.
    void setArt(int i, std::shared_ptr<const gfx::PixMap> pm);

    // 10e8:0040 Hud_RedrawNear (does nothing before the art is loaded).
    void redraw(const GameState& s);
    // 10e8:0658 Hud_SetClueBoxMode: hot spots 99 / 0x409 by the allocator's rules, then redraw.
    void setClueBoxMode(uint8_t mode, const GameState& s);
    uint8_t clueBoxMode() const { return clueBoxMode_; }  // DS:0ec0 g_clueBoxMode

    // The parts (10e8:00dd .. 04b2), drawing into the frame buffer only.
    void drawClueBoxMode(int m);
    void drawClueTexts(const GameState& s);
    void drawKeyIcon(const GameState& s);
    void drawCoins(const GameState& s);
    void drawNets(const GameState& s);
    void drawTreasureGems(const GameState& s);
    void drawTreasurePoints(const GameState& s);

private:
    gfx::FrameBuffer& fb_;
    text::TextRenderer& text_;
    HotspotTable& hot_;
    const RoomTables& rt_;
    PresentFn present_;
    std::array<std::shared_ptr<const gfx::PixMap>, hud::kArtCount> art_{};
    uint8_t clueBoxMode_ = 0;
};

}  // namespace tmport::game::room
