#pragma once
// The gears and buttons puzzle, kind 3 (ovl22 3b2f; re/gg/docs/code_puzzles_b.md §3), the data and
// drawing half: the variant data F 0x2002 + v (peg grid and objects), the board with its pegs, the
// objects drawn back to front with the buttons on top (PzGears_Draw), and the inventory. Carrying and
// placing gears, the crank drag, the propagation and the reset are wave 3's.
//
// F 0x2002 + v: u8 grid[12][23] (non-zero = a peg), u16 n at +0x114, n objects of 20 bytes at +0x116
// {type, state, column, row, length, then run-time fields}. Types: 0 small gear, 1 large gear, 2 / 3
// horizontal / vertical plain rack segment, 4 / 5 toothed, 6 the crank, 7..10 buttons.

#include <array>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/puzzles.h"

namespace tmport::gg {

class PzGears final : public Puzzle {
public:
    static constexpr uint16_t kVariantFirst = 0x2002;
    static constexpr uint16_t kSmallGear = 0x2023;   // 6 rotation frames
    static constexpr uint16_t kLargeGear = 0x2024;   // 8 rotation frames
    static constexpr uint16_t kCrankGrip = 0x2025;   // the crank with the hand, 24 frames
    static constexpr uint16_t kCrank = 0x2026;       // 24 frames
    static constexpr uint16_t kButtons = 0x2027;     // (up, down) x 4 placements
    static constexpr uint16_t kDigits = 0x2028;
    static constexpr uint16_t kRacks = 0x2029;       // toothed / plain, horizontal / vertical
    static constexpr uint16_t kPanelText = 0x202A;
    static constexpr uint16_t kHintTextFirst = 0x202B;
    static constexpr int kCols = 23, kRows = 12;
    static constexpr int kNotPlaced = -99;

    struct Object {  // 20 bytes
        int type = 0, state = 0, col = 0, row = 0, length = 0;
        int dir = 0, visited = 0, savedState = 0, savedCol = 0, savedRow = 0;  // +0x0a..+0x12 (run time)
    };

    PzGears(const GgExe& exe, const Archive& archive);

    // 3b2f:1b83 PzGears_LoadVariant (the copy, its grid and objects; the crank zone is the runtime's)
    // and 3b2f:1bd5 PzGears_LoadResources.
    void loadVariant(int variant);
    void loadResources(int variant);
    // 3b2f:1ed0 PzGears_Run up to the loop: variant, resources, the hand, the panel (before the
    // screen: the screen's 157 rows cover its top two), PzGears_DrawScreen (3b2f:071c: the screen at
    // (0, 10), all 276 pegs, the back page, PzGears_Draw(1), the stock, the inventory, the fade-in),
    // PzGears_SaveHome.
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 3b2f:01e4 PzGears_DrawPeg(c, r, colour).
    void drawPeg(Screen& screen, int c, int r, uint8_t colour) const;
    // 3b2f:024f PzGears_Draw(sounds): restore (84, 1, 226, 160), clip x 86..308, y 23..152, objects
    // n-1 down to 0 (propagation fields cleared; a gear off the board is not drawn), then the buttons
    // 1..n-1 in index order; the clip restored; flush. Returns (all buttons down, a button went down)
    // for the caller's effects 4 / 3 (prio 2) when `sounds`.
    std::pair<bool, bool> draw(Video& video, bool sounds);
    // 3b2f:0033 PzGears_CountStock / 3b2f:00a9 PzGears_DrawInventory (flushes).
    void countStock();
    void drawInventory(Video& video) const;
    // 3b2f:1da8 PzGears_SaveHome: gears +8 = frame, racks +2 = column or row (the reset's home).
    void saveHome();
    std::string describe() const override;

    int objectCount() const { return static_cast<int>(objects_.size()); }
    std::vector<Object>& objects() { return objects_; }
    bool peg(int c, int r) const { return grid_[static_cast<size_t>(r * kCols + c)] != 0; }
    std::array<int, 2> stock{};  // g_gearsStock DS:b702
    bool gripping = false;       // g_gearsCrankCur = 0x2025 while turning

private:
    int tableS16(uint16_t base, int i) const;

    const GgExe& exe_;
    const Archive& archive_;
    std::vector<uint8_t> grid_;
    std::vector<Object> objects_;
    Bundle gears_[2], grip_, crank_, buttons_, digits_, racks_;
    std::vector<uint8_t> panelText_;
    std::unique_ptr<CursorRecord> cursor_, blank_;
    int variant_ = 0;
};

}  // namespace tmport::gg
