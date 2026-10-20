#pragma once
// The balance puzzle, kind 4 (ovl14 32fe; re/gg/docs/code_puzzles_b.md §4): the variant table
// D 0x211b, the weight types DS:25cc, the hook positions DS:2638 and the weight offsets DS:27f0 of the
// user's ssg.exe, the screen and its drawing (inventory, beam and hooks); in wave 3 the run on the game
// (game/pz_balance_run.cpp): carrying and hanging weights, the torques, the swings and the hook zones.
//
// D 0x211b: u16 count (32), then 21 i16 per variant (42 bytes at 2 + 42 v): words 0..5 the inventory
// per weight type, words 6..20 the right hooks 5..9 (arms 1..5), 3 slots each (type or -1, top first).
//
// The puzzle's globals are GgState::puzzles.balance (BalanceState: the tilt, the zone set, the weight in
// the hand, the solved flag, the hooks and the stock). A tool works on the object's own copy; the game
// binds the object to its state.

#include <array>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/game_state.h"
#include "gg/game/puzzles.h"
#include "gg/gfx/cursor.h"

namespace tmport::gg {

class GgGame;

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
    static constexpr uint16_t kEffectBank = 0x11C6;
    static constexpr int kHooks = 10;  // 0..4 left (arm 5 - h), 5..9 right (arm h - 4)
    static constexpr int kSlots = 3;   // weights per hook, top first
    static constexpr int kTypes = 6;

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
    // 32fe:0aa0 PzBalance_Run up to the loop, as a tool shows it: tilt -5, nothing held, resources,
    // variant, the hand, the screen at (0, 10), the panel, Gfx_SaveToBack, the inventory, the beam,
    // the fade-in.
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 32fe:0235 PzBalance_DrawInventory (flushes).
    void drawInventory(Video& video);
    // 32fe:0375 PzBalance_DrawBeam: tilt clamped to +-5 with the hook zone set of a resting tilt
    // (PzBalance_SelectHookZones on the game), the beam picture of the tilt keyed at (53, 14), the hooks
    // of the tilt, flush.
    void drawBeam(Video& video);
    // 32fe:02c8 PzBalance_DrawHook(x, y, hook, right).
    void drawHook(Screen& screen, int x, int y, const std::array<int16_t, 3>& weights, bool right) const;
    std::string describe() const override;

    BalanceState& st() { return *st_; }
    const BalanceState& st() const { return *st_; }
    const std::array<WeightType, kTypes>& weightTypes() const { return types_; }
    // The hook x, y of tilt index t (tilt + 5) and hook h (DS:2638).
    std::pair<int, int> hookPos(int t, int h) const;


    // ---- wave 3: the run (owner: puzzles_b agent; game/pz_balance_run.cpp) ----
    // The puzzle's globals are GgState::puzzles.balance: bind() points the class at them.
    void bind(BalanceState& state) { st_ = &state; }
    bool bound() const { return st_ != &own_; }
    // 32fe:0aa0 PzBalance_Run: the puzzle (code_puzzles_b.md §4): set-up, hint 0x1052, PzBalance_Loop, cleanup; 1 when solved.
    int run(GgGame& g, int variant);
    // 32fe:0079 PzBalance_SelectHookZones: swaps the 5 left hook zones to the given resting tilt's set.
    void selectHookZones(GgGame& g, int set, bool removeOld);
    // 32fe:00d4 PzBalance_InitZones: the inventory zones (added) and the three hook zone sets; set 0 added.
    void initZones(GgGame& g);
    // 32fe:043b PzBalance_LeftTorque: the sum of mass * (5 - hook) over hooks 0..4.
    int leftTorque();
    // 32fe:0494 PzBalance_RightTorque: the sum of mass * (hook - 4) over hooks 5..9.
    int rightTorque();
    // 32fe:04f0 PzBalance_Compare: 0 no motion, -1 come to level, else 100 * lighter / heavier + 1 (the swing speed).
    int compare();
    // 32fe:0566 PzBalance_SwingLevel: the tilt one step toward 0 every 50 ticks, the rising or falling effect.
    void swingLevel(GgGame& g);
    // 32fe:05eb PzBalance_Swing: the tilt to the other end (or from level) at ratio / 2 ticks per step, then effect 10.
    void swing(GgGame& g, int ratio);
    // 32fe:06b9 PzBalance_Settle: PzBalance_Compare and the motion; true when the beam came to level.
    bool settle(GgGame& g);
    // 32fe:06de PzBalance_HoldWeight: the carried-weight cursor (hot spot w / 2, 0), effect 0.
    void holdWeight(GgGame& g, int type);
    // 32fe:0750 PzBalance_DropCursor: the puzzle cursor back, the carried cursor freed.
    void dropCursor(GgGame& g);
    // 32fe:0775 PzBalance_Hang: the carried weight to the first free slot of the hook (3 at most), effect 1.
    void hang(GgGame& g, int hook);
    // 32fe:07c9 PzBalance_Unhang: the last weight of the hook into the hand.
    void unhang(GgGame& g, int hook);
    // 32fe:081a PzBalance_Loop: the input loop (switch CS:0a2c, 29 codes); true when solved.
    bool loop(GgGame& g);

    // A moment of the main loop from a DOSBox-X state (puzzles.h).
    bool restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) override;
    int resume(GgGame& g) override;

private:
    void loadResources(int variant);  // 32fe:0aa0's Res_LoadCopy calls
    void flush(Video& video);
    void swingEffect(GgGame& g, int step, int& n);  // the transposed swing effect of a beam step
    int finish(GgGame& g);                          // 32fe:0c0b..0ce4: the exit of PzBalance_Run

    const GgExe& exe_;
    const Archive& archive_;
    BalanceState own_;
    BalanceState* st_ = &own_;
    GgGame* game_ = nullptr;
    std::vector<uint8_t> table_;
    std::array<WeightType, kTypes> types_{};
    std::array<int, kTypes> dx_{};
    Bundle carry_, left_, right_, counts_;
    std::vector<uint8_t> ring_, stack_;
    std::vector<uint8_t> panelText_, hintText_, hintPressed_;
    std::unique_ptr<CursorRecord> cursor_, held_;  // DS:ab56, DS:ab52
    // The zones: inventory DS:27fc, beam DS:2812, Hint DS:2828, the weight types DS:aca4 + 22 t, the
    // hooks DS:ab5a + 22 (5 s + i).
    Zone invZone_, beamZone_, hintZone_;
    std::array<Zone, kTypes> typeZones_{};
    std::array<std::array<Zone, 5>, 3> hookZones_{};
    int variant_ = 0;
};

}  // namespace tmport::gg
