#pragma once
// The simple machines puzzle, kind 1 (ovl20 3938, simple.c; re/gg/docs/code_puzzles_a.md §6): the variant
// data F 0x206d + v, the machine bundle 0x2059 + v, the part pictures and part bundles, the part layout
// DS:3402, the screen with the machine, the inventory, the cursor regions over the inventory slots and
// the placed parts; in wave 3 the run on the game (game/pz_simple_run.cpp): taking, dropping, checking,
// the fit and bounce animations and the reveal.
//
// F 0x206d + v (0x570 bytes): s8 count[8]; +0x08, +0x0a machine x, y; +0x0c + 0x8a r (r 0..9) record
// r: +0 x, +2 y of its filled picture, +4 six 22-byte acceptance rectangles (shape 2 = unused), +0x88
// the needed part type (8 = none or filled).
//
// The puzzle's globals are GgState::puzzles.simple (SimpleState: the variant copy, the part in the hand,
// the placed parts). A tool works on the object's own copy; the game binds the object to its state.

#include <array>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/game_state.h"
#include "gg/game/puzzles.h"
#include "gg/gfx/cursor.h"

namespace tmport::gg {

class GgGame;

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
    static constexpr uint16_t kEffectBank = 0x11CA;
    static constexpr int kTypes = 8;
    static constexpr int kRecords = 10;
    static constexpr int kNone = 9;  // g_simpleHeld DS:b1b8 when nothing is held

    struct Layout {  // DS:3402, 15 bytes per type
        int iconX = 0, iconY = 0, digitX = 0, digitY = 0, testDx = 0, testDy = 0, fitEffect = 0;
    };

    PzSimple(const GgExe& exe, const Archive& archive);

    // 3938:0ed8 PzSimple_LoadResources and 3938:0dfe PzSimple_LoadVariant.
    void loadResources(int variant);
    void loadVariant(int variant);
    // 3938:0000 PzSimple_Run up to the loop, as a tool shows it: held = none, resources, variant, the
    // hand as the whole-screen cursor region DS:338e, PzSimple_DrawScreen (3938:0e76: the screen,
    // member 0 of the machine bundle at the variant's x, y, the panel, the back page, the fade-in), the
    // label cursor regions of the types with a count, the inventory.
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 3938:0d63 PzSimple_DrawInventory (flushes).
    void drawInventory(Video& video);
    // 3938:040f PzSimple_DrawPlaced(flush).
    void drawPlaced(Video& video, bool flush);
    std::string describe() const override;

    int count(int type) const;
    const std::array<Layout, kTypes>& layout() const { return layout_; }
    SimpleState& st() { return *st_; }
    const SimpleState& st() const { return *st_; }


    // ---- wave 3: the run (owner: puzzles_a agent; game/pz_simple_run.cpp) ----
    // The puzzle's globals are GgState::puzzles.simple: bind() points the class at them.
    void bind(SimpleState& state) { st_ = &state; }
    bool bound() const { return st_ != &own_; }
    // 3938:0000 PzSimple_Run: the puzzle (code_puzzles_a.md §6): set-up, hint 0x1052, the loop (codes 0x44d..0x454 first, then switch CS:020b), cleanup; 1 when solved.
    int run(GgGame& g, int variant);
    // 3938:0233 PzSimple_DropPart: the held part placed at the mouse when it fits inside the work area.
    void dropPart(GgGame& g, int x, int y);
    // 3938:0345 PzSimple_PickUpPlaced: the topmost placed part under the mouse back into the hand.
    void pickUpPlaced(GgGame& g, int x, int y);
    // 3938:046b PzSimple_PointInRect: x <= px < x + w and y <= py < y + h.
    bool pointInRect(int px, int py, int x, int y, int w, int h);
    // 3938:04a1 PzSimple_InitCursorRegions: the label cursors (0x204e) over the 8 inventory slots.
    void initCursorRegions(GgGame& g);
    // 3938:0524 PzSimple_AddCursorRegions: the label cursor regions of the slots with a count.
    void addCursorRegions(GgGame& g);
    // 3938:054d PzSimple_CheckParts: the check button: each placed part fits a record of its type (the fit animation) or bounces back.
    void checkParts(GgGame& g);
    // 3938:0671 PzSimple_BounceBack: the part flies toward its inventory slot in 5 steps of 5 ticks.
    void bounceBack(GgGame& g, int slot);
    // 3938:0750 PzSimple_JudgeAndReveal: all 10 records filled: a 100-tick pause, the expanding reveal of the finished machine; true when solved.
    bool judgeAndReveal(GgGame& g);
    // 3938:0886 PzSimple_AnimateFit: the part's animation frames 1..n-1 at 40 ticks each with its looped sound.
    void animateFit(GgGame& g, int type, int x, int y);
    // 3938:099f PzSimple_ReturnHeld: the held part back into the inventory.
    void returnHeld(GgGame& g);
    // 3938:09df PzSimple_TakeFromInventory: a part of the type (count > 0) into the hand.
    void takeFromInventory(GgGame& g, int type);
    // 3938:0a64 PzSimple_FindRecord: the first record of the part's type whose acceptance rectangle holds its test point, -1 none.
    int findRecord(int slot);
    // 3938:0af7 PzSimple_RemoveRecordZones: the acceptance rectangles of the held type off the zone list (never added: keep).
    void removeRecordZones(GgGame& g);
    // 3938:0b59 PzSimple_BuildHintText: the hint text (header 0x2083 for variants 0..2, then the text of each part type present) and the slot zones.
    void buildHintText(GgGame& g, int variant);
    // 3938:1004 PzSimple_Cleanup: frees everything, clears the zones and cursor regions, the default cursor.
    void cleanup(GgGame& g);

    // A moment of the main loop from a DOSBox-X state (puzzles.h).
    bool restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) override;
    int resume(GgGame& g) override;

private:
    int16_t recordWord(int r, size_t off) const;  // record r of the variant copy, its word at +off
    void setRecordWord(int r, size_t off, int16_t v);
    int loop(GgGame& g, bool solved);
    void flush(Video& video);
    void buildHint(int variant);  // the text of 3938:0b59 (the zones are the game's)

    const GgExe& exe_;
    const Archive& archive_;
    SimpleState own_;
    SimpleState* st_ = &own_;
    GgGame* game_ = nullptr;
    std::array<Layout, kTypes> layout_{};
    std::array<int, kTypes> fitDx_{}, fitDy_{};  // DS:347a / DS:348a
    Bundle labels_, digits_, machine_, parts_, hintTexts_;
    std::array<Bundle, kTypes> partBundles_;
    std::vector<uint8_t> panelText_, hintHeader_, hintText_, checkPressed_, hintPressed_;
    std::unique_ptr<CursorRecord> cursor_;
    CursorRecord* held_ = nullptr;  // DS:b416
    std::vector<std::unique_ptr<CursorRecord>> heldRecords_;
    std::array<std::unique_ptr<CursorRecord>, kTypes> labelCursors_;
    CursorRegion screenRegion_;
    std::array<CursorRegion, kTypes> slotRegions_{};
    // The click zones: the work area DS:33aa, Hint DS:33ec, check DS:33c0, the inventory column
    // DS:33d6 (only while a part is held), the inventory slots DS:b21a + 0x16 t.
    Zone workZone_, hintZone_, checkZone_, inventoryZone_;
    std::array<Zone, kTypes> slotZones_{};
    int variant_ = 0;
};

}  // namespace tmport::gg
