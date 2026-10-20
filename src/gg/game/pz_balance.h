#pragma once
// The balance puzzle, kind 4 (ovl14 32fe; re/gg/docs/code_puzzles_b.md §4), the data and drawing
// half: the variant table D 0x211b, the weight types DS:25cc, the hook positions DS:2638 and the
// weight offsets DS:27f0 of the user's ssg.exe, the screen and its first drawing (inventory and beam
// at tilt -5). Hanging, the torques, the swing and the zones are wave 3's.
//
// D 0x211b: u16 count (32), then 21 i16 per variant (42 bytes at 2 + 42 v): words 0..5 the inventory
// per weight type, words 6..20 the right hooks 5..9 (arms 1..5), 3 slots each (type or -1, top first).

#include <array>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/puzzles.h"

namespace tmport::gg {

class PzBalance final : public Puzzle {
public:
    static constexpr uint16_t kVariantTable = 0x211B;
    static constexpr uint16_t kBeamFirst = 0x211E;  // + tilt + 5 (tilts -5..5), 265 x 146 keyed
    static constexpr uint16_t kCarryPics = 0x2129;  // weights with their hook (inventory, cursor)
    static constexpr uint16_t kLeftPics = 0x212A;   // hanging weights, left side
    static constexpr uint16_t kRightPics = 0x212B;  // hanging weights, right side
    static constexpr uint16_t kCountPics = 0x212C;  // inventory counts 0..8
    static constexpr uint16_t kRingPic = 0x211C;
    static constexpr uint16_t kStackPic = 0x211D;
    static constexpr uint16_t kHintTextFirst = 0x212D;
    static constexpr uint16_t kPanelText = 0x214D;

    // A weight type (DS:25cc, 18 bytes each).
    struct WeightType {
        int mass = 0;
        int picX = 0, picY = 0;      // inventory picture
        int countX = 0, countY = 0;  // inventory count
        int zoneX = 0, zoneY = 0, zoneW = 0, zoneH = 0;
    };

    PzBalance(const GgExe& exe, const Archive& archive);

    // 32fe:0000 PzBalance_LoadVariant: the stock and the hooks (left empty, right from the table).
    void loadVariant(int variant);
    // 32fe:0aa0 PzBalance_Run up to the loop: tilt -5, nothing held, resources, variant, the hand,
    // the screen at (0, 10), the panel, Gfx_SaveToBack, the inventory, the beam, the fade-in.
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 32fe:0235 PzBalance_DrawInventory (flushes).
    void drawInventory(Video& video) const;
    // 32fe:0375 PzBalance_DrawBeam: tilt clamped to +-5 (the hook zone set follows: wave 3), the beam
    // picture of the tilt keyed at (53, 14), the hooks of the tilt, flush.
    void drawBeam(Video& video);
    // 32fe:02c8 PzBalance_DrawHook(x, y, hook, right).
    void drawHook(Screen& screen, int x, int y, const std::array<int, 3>& weights, bool right) const;
    std::string describe() const override;

    const std::array<int, 6>& stock() const { return stock_; }
    const std::array<std::array<int, 3>, 10>& hooks() const { return hooks_; }
    const std::array<WeightType, 6>& weightTypes() const { return types_; }
    int tilt = -5;       // g_balanceTilt DS:ab24
    int zoneSet = -1;    // g_balanceZoneSet DS:ab22
    int held = -1;       // g_balanceHeld DS:ab26
    // The hook x, y of tilt index t (tilt + 5) and hook h (DS:2638).
    std::pair<int, int> hookPos(int t, int h) const;

private:
    const GgExe& exe_;
    const Archive& archive_;
    std::vector<uint8_t> table_;
    std::array<int, 6> stock_{};
    std::array<std::array<int, 3>, 10> hooks_{};
    std::array<WeightType, 6> types_{};
    std::array<int, 6> dx_{};
    Bundle carry_, left_, right_, counts_;
    std::vector<uint8_t> ring_, stack_;
    std::vector<uint8_t> panelText_;
    std::unique_ptr<CursorRecord> cursor_;
    int variant_ = 0;
};

}  // namespace tmport::gg
