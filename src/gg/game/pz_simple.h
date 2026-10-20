#pragma once
// The simple machines puzzle, kind 1 (ovl20 3938, simple.c; re/gg/docs/code_puzzles_a.md §6), the data
// and drawing half: the variant data F 0x206d + v, the machine bundle 0x2059 + v, the part pictures and
// part bundles, the part layout DS:3402, the screen with the machine, the inventory, the cursor
// regions over the inventory slots, and the placed parts. Taking, dropping, checking, the fit and
// bounce animations and the reveal are wave 3's.
//
// F 0x206d + v (0x570 bytes): s8 count[8]; +0x08, +0x0a machine x, y; +0x0c + 0x8a r (r 0..9) record
// r: +0 x, +2 y of its filled picture, +4 six 22-byte acceptance rectangles (shape 2 = unused), +0x88
// the needed part type (8 = none or filled).

#include <array>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/puzzles.h"
#include "gg/gfx/cursor.h"

namespace tmport::gg {

class PzSimple final : public Puzzle {
public:
    static constexpr uint16_t kLabelCursors = 0x204E;  // 8 label pictures
    static constexpr uint16_t kPartPictures = 0x2050;  // 8 part pictures (held and placed)
    static constexpr uint16_t kPartBundleFirst = 0x2051;  // + type: member 0 icon, 1.. the fit animation
    static constexpr uint16_t kMachineFirst = 0x2059;  // + v: 0 machine, 1 finished, 2 + r filled record r
    static constexpr uint16_t kVariantFirst = 0x206D;
    static constexpr uint16_t kHintTexts = 0x2081;
    static constexpr uint16_t kPanelText = 0x2082;
    static constexpr uint16_t kHintHeader = 0x2083;
    static constexpr int kTypes = 8;
    static constexpr int kNone = 9;  // g_simpleHeld DS:b1b8 when nothing is held

    struct Layout {  // DS:3402, 15 bytes per type
        int iconX = 0, iconY = 0, digitX = 0, digitY = 0, testDx = 0, testDy = 0, fitEffect = 0;
    };
    struct Placed {  // DS:b38a, 14 bytes: type (9 free), x, y, w, h, test x, test y
        int type = kNone, x = 0, y = 0, w = 0, h = 0, testX = 0, testY = 0;
    };

    PzSimple(const GgExe& exe, const Archive& archive);

    // 3938:0ed8 PzSimple_LoadResources and 3938:0dfe PzSimple_LoadVariant.
    void loadResources(int variant);
    void loadVariant(int variant);
    // 3938:0000 PzSimple_Run up to the loop: held = none, resources, variant, the hand as the
    // whole-screen cursor region DS:338e, PzSimple_DrawScreen (3938:0e76: the screen, member 0 of the
    // machine bundle at the variant's x, y, the panel, the back page, the fade-in), the slot zones of
    // PzSimple_BuildHintText, the label cursor regions of the types with a count, the inventory.
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 3938:0d63 PzSimple_DrawInventory (flushes).
    void drawInventory(Video& video) const;
    // 3938:040f PzSimple_DrawPlaced(flush).
    void drawPlaced(Video& video, bool flush) const;
    std::string describe() const override;

    int count(int type) const;
    int held = kNone;
    std::array<Placed, 10>& placed() { return placed_; }
    const std::array<Layout, kTypes>& layout() const { return layout_; }

private:
    const GgExe& exe_;
    const Archive& archive_;
    std::vector<uint8_t> data_;  // the F copy (counts change in play)
    std::array<Layout, kTypes> layout_{};
    std::array<Placed, 10> placed_{};
    Bundle labels_, digits_, machine_, parts_;
    std::array<Bundle, kTypes> partBundles_;
    std::vector<uint8_t> panelText_;
    std::unique_ptr<CursorRecord> cursor_;
    std::array<std::unique_ptr<CursorRecord>, kTypes> labelCursors_;
    CursorRegion screenRegion_;
    std::array<CursorRegion, kTypes> slotRegions_{};
    int variant_ = 0;
};

}  // namespace tmport::gg
