#pragma once
// The magnets puzzle, kind 7 (ovl16 35bb; re/gg/docs/code_puzzles_b.md §5), the data and drawing half:
// the level map DS:2ae4 (the variant is not the level), the inventory per level DS:2a8a, the inventory
// positions DS:29e2 / 29ee, the targets of D 0x11a1, the screen with the target outlines baked into
// the back page, the inventory, and the table's painter's-order drawing of placed magnets. The pole
// forces, the simulation, the placement rules and the success check are wave 3's.
//
// D 0x11a1: i16 count[30] (targets per level), then rec target[30][40] at +0x3c {i16 x, y, a, b}
// (b 0 bar, 1 horseshoe; a the orientation); the outline is member 2b + a of bundle 0x11a6.

#include <array>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/puzzles.h"

namespace tmport::gg {

class PzMagnets final : public Puzzle {
public:
    static constexpr uint16_t kTargets = 0x11A1;
    static constexpr uint16_t kRotatePics = 0x11A2;
    static constexpr uint16_t kDigits = 0x11A3;
    static constexpr uint16_t kMagnetPics = 0x11A4;  // member kind * 4 + orientation
    static constexpr uint16_t kShadowPics = 0x11A5;
    static constexpr uint16_t kOutlinePics = 0x11A6;
    static constexpr uint16_t kHintTextFirst = 0x11A7;  // + level
    static constexpr uint16_t kPanelText = 0x11C5;
    static constexpr int kSlots = 64;

    struct Target {
        int x = 0, y = 0, a = 0, b = 0;
    };
    // A magnet slot (0x12 bytes of the Mem_Alloc(0x480) block): kind -1 = free.
    struct Magnet {
        int kind = -1, orientation = 0, x = 0, y = 0;
        bool drawn = false;
    };

    PzMagnets(const GgExe& exe, const Archive& archive);

    // 35bb:1c03 PzMagnets_Run up to the loop: level = DS:2ae4[v] - 1, the targets, PzMagnets_Init,
    // the texts and pictures, the hand, the screen at (0, 10), the panel, the target outlines, the
    // back page, the inventory, the fade-in.
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 35bb:1b59 PzMagnets_DrawTargets(level): the inventory counts from DS:2a8a, every outline keyed.
    void drawTargets(Screen& screen);
    // 35bb:01ac PzMagnets_DrawInventory (flushes).
    void drawInventory(Video& video) const;
    // 35bb:0127 PzMagnets_DrawNumber(x, y, n): the tens digit when n >= 10, the units after it.
    void drawNumber(Screen& screen, int x, int y, int n) const;
    // 35bb:0456 PzMagnets_DrawTable: restore (70, 23, 239, 128), then the magnets in painter's order
    // (a magnet waits while another undrawn one with its shadow overlaps it), shadow at (+2, +2) then
    // the magnet, flush. A pair covering each other would loop for ever in the original; the engine
    // stops after a pass that draws nothing.
    void drawTable(Video& video);
    std::string describe() const override;

    int level() const { return level_; }
    const std::vector<Target>& targets() const { return targets_; }
    std::array<int, 3>& counts() { return counts_; }
    std::array<Magnet, kSlots>& slots() { return slots_; }
    int held = -1;             // g_magnetsHeld DS:af4a
    int heldOrientation = 0;   // DS:af48

private:
    // 35bb:0271 PzMagnets_Overlap(j, i, 0): j with its shadow over i's shrunk rectangle.
    bool covers(int j, int i) const;
    std::pair<int, int> magnetSize(const Magnet& m) const;

    const GgExe& exe_;
    const Archive& archive_;
    int level_ = 0;
    std::vector<Target> targets_;
    std::array<int, 3> counts_{};
    std::array<Magnet, kSlots> slots_{};
    Bundle rotate_, digits_, magnets_, shadows_, outlines_;
    std::vector<uint8_t> panelText_;
    std::unique_ptr<CursorRecord> cursor_;
};

}  // namespace tmport::gg
