#pragma once
// The magnets puzzle, kind 7 (ovl16 35bb; re/gg/docs/code_puzzles_b.md §5): the level map DS:2ae4 (the
// variant is not the level), the inventory per level DS:2a8a, the inventory positions DS:29e2 / 29ee,
// the pole table DS:29fa, the targets of D 0x11a1, the screen with the target outlines baked into the
// back page, the inventory and the table's painter's-order drawing; in wave 3 the run on the game
// (game/pz_magnets_run.cpp): carrying, rotating and placing magnets, the pole-force simulation and the
// success check.
//
// D 0x11a1: i16 count[30] (targets per level), then rec target[30][40] at +0x3c {i16 x, y, a, b}
// (b 0 bar, 1 horseshoe; a the orientation); the outline is member 2b + a of bundle 0x11a6.
//
// The puzzle's globals are GgState::puzzles.magnets (MagnetsState: the magnet in the hand, the
// inventory, the 64 slots of the heap block DS:af52, the level). A tool works on the object's own copy;
// the game binds the object to its state.

#include <array>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/game_state.h"
#include "gg/game/puzzles.h"
#include "gg/gfx/cursor.h"

namespace tmport::gg {

class GgGame;

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
    static constexpr uint16_t kEffectBank = 0x11D0;
    static constexpr int kSlots = 64;
    // The table: x 70..308, y 23..150 (a magnet with its shadow must lie on it).
    static constexpr int kTableX = 70, kTableY = 23, kTableRight = 309, kTableBottom = 151;

    struct Target {
        int x = 0, y = 0, a = 0, b = 0;
    };

    PzMagnets(const GgExe& exe, const Archive& archive);

    // 35bb:1c03 PzMagnets_Run up to the loop, as a tool shows it: level = DS:2ae4[v] - 1, the targets,
    // PzMagnets_Init, the texts and pictures, the hand, the screen at (0, 10), the panel, the target
    // outlines, the back page, the inventory, the fade-in.
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 35bb:1b59 PzMagnets_DrawTargets(level): the inventory counts from DS:2a8a, every outline keyed.
    void drawTargets(Screen& screen);
    // 35bb:01ac PzMagnets_DrawInventory (flushes).
    void drawInventory(Video& video);
    // 35bb:0127 PzMagnets_DrawNumber(x, y, n): the tens digit when n >= 10, the units after it.
    void drawNumber(Screen& screen, int x, int y, int n) const;
    // 35bb:0456 PzMagnets_DrawTable: restore (70, 23, 239, 128), then the magnets in painter's order
    // (a magnet waits while another undrawn one with its shadow overlaps it), shadow at (+2, +2) then
    // the magnet, flush. A pair covering each other would loop for ever in the original; the engine
    // stops after a pass that draws nothing.
    void drawTable(Video& video);
    std::string describe() const override;

    int level() const { return st_->level; }
    const std::vector<Target>& targets() const { return targets_; }
    MagnetsState& st() { return *st_; }
    const MagnetsState& st() const { return *st_; }
    // The width and height of the picture of slot i's kind and orientation.
    std::pair<int, int> magnetSize(int i) const;
    // A pole of picture kind * 4 + orientation (DS:29fa: s8 dx, dy; -1, -1 = no pole).
    std::pair<int, int> pole(int picture, int p) const;


    // ---- wave 3: the run (owner: puzzles_b agent; game/pz_magnets_run.cpp) ----
    // The puzzle's globals are GgState::puzzles.magnets: bind() points the class at them.
    void bind(MagnetsState& state) { st_ = &state; }
    bool bound() const { return st_ != &own_; }
    // 35bb:1c03 PzMagnets_Run: the puzzle (code_puzzles_b.md §5): set-up, hint 0x1052, PzMagnets_Loop, cleanup; 1 when solved.
    int run(GgGame& g, int variant);
    // 35bb:0103 PzMagnets_WarpMouse: g_mouseX/Y and INT 33h function 4 (GgMouse::warp).
    void warpMouse(GgGame& g, int x, int y);
    // 35bb:0236 PzMagnets_AllDrawn: every used slot has its drawn flag.
    bool allDrawn();
    // 35bb:0401 PzMagnets_CanDraw: no undrawn magnet's shadow covers magnet i.
    bool canDraw(int i);
    // 35bb:05be PzMagnets_ISqrt: the binary-search square root; 1 for n < 4.
    int isqrt(uint32_t n);
    // 35bb:06aa PzMagnets_Cube: x * x * x (16 bits).
    int16_t cube(int16_t x);
    // 35bb:06ba PzMagnets_PoleForce: the force of magnet j's poles on a pole of magnet i, or attach.
    void poleForce(bool north, int px, int py, int i, int j);
    // 35bb:08ae PzMagnets_Forces: accumulates the forces of all other magnets on magnet i.
    void forces(int i);
    // 35bb:09a5 PzMagnets_Blocked: off the table or overlapping another magnet.
    bool blocked(int i);
    // 35bb:0aa4 PzMagnets_TouchesOpposite: a pole of i 1 px from an opposite pole.
    bool touchesOpposite(int i);
    // 35bb:0c78 PzMagnets_TouchesSame: a pole of i 1 px from a like pole: sets the cross-axis nudge.
    bool touchesSame(int i, bool vertical);
    // 35bb:0efc PzMagnets_Step: moves the magnets one pixel by their forces; true when one moved.
    bool step(int substep);
    // 35bb:113c PzMagnets_Simulate: up to 16 rounds of 4 substeps of 4 ticks, the looping effect 1.
    void simulate(GgGame& g);
    // 35bb:1208 PzMagnets_HoldMagnet: the carried-magnet cursor (hot spot 0, 0), the mouse moved to (x, y).
    void holdMagnet(GgGame& g, int kind, int orient, int x, int y);
    // 35bb:126f PzMagnets_DropCursor: the mouse moved (+5, +5 when offset), the puzzle cursor back, effect 3.
    void dropCursor(GgGame& g, bool offset);
    // 35bb:12c4 PzMagnets_TakeFromInventory: a magnet of the kind under the mouse into the hand, effect 2.
    void takeFromInventory(GgGame& g);
    // 35bb:1387 PzMagnets_PutBack: the carried magnet back to the inventory.
    void putBack(GgGame& g);
    // 35bb:13a7 PzMagnets_PickUp: the first magnet under the mouse into the hand, then simulate.
    void pickUp(GgGame& g);
    // 35bb:14f5 PzMagnets_Drop: the carried magnet to the table at the mouse when it fits, then simulate.
    void drop(GgGame& g);
    // 35bb:16b6 PzMagnets_Rotate: the next orientation of the carried magnet, effect 5.
    void rotate(GgGame& g);
    // 35bb:16f7 PzMagnets_CheckSolved: every target has its own matching magnet within 4 px.
    bool checkSolved(int level);
    // 35bb:1898 PzMagnets_Loop: the input loop (switch CS:1a1f, 10 codes); true when solved.
    bool loop(GgGame& g, int level);
    // 35bb:1a47 PzMagnets_Init: nothing carried, all 64 slots free.
    void init();
    // 35bb:1af4 PzMagnets_FreeResources: frees what PzMagnets_LoadResources loaded.
    void freeResources(GgGame& g);

    // A moment of the main loop from a DOSBox-X state (puzzles.h).
    bool restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) override;
    int resume(GgGame& g) override;

    // Simulation helpers without a game (unit tests): the slots and the level's targets as given.
    void setTargets(std::vector<Target> targets) { targets_ = std::move(targets); }
    void loadPictures();  // 35bb:1a8f PzMagnets_LoadResources' pictures (the sizes the rules use)

private:
    // 35bb:0271 PzMagnets_Overlap(j, i, 0): j with its shadow over i's shrunk rectangle.
    bool covers(int j, int i) const;
    void loadTargets(int variant);  // the level and its targets from D 0x11a1
    void flush(Video& video);
    bool loopFrom(GgGame& g, int level, bool solved);  // PzMagnets_Loop with its solved local DI
    int finish(GgGame& g, bool solved);  // 35bb:1d34..1db3: the exit of PzMagnets_Run

    const GgExe& exe_;
    const Archive& archive_;
    MagnetsState own_;
    MagnetsState* st_ = &own_;
    GgGame* game_ = nullptr;
    std::vector<Target> targets_;
    Bundle rotate_, digits_, magnets_, shadows_, outlines_;
    std::vector<uint8_t> panelText_, hintText_, hintPressed_;
    std::unique_ptr<CursorRecord> cursor_, held_;  // DS:af88, DS:af84
    // The zones: inventory DS:298a, table DS:29a0, Hint DS:29b6, ROTATE DS:29cc.
    Zone invZone_, tableZone_, hintZone_, rotateZone_;
};

}  // namespace tmport::gg
