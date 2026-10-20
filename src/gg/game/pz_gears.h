#pragma once
// The gears and buttons puzzle, kind 3 (ovl22 3b2f; re/gg/docs/code_puzzles_b.md §3): the variant data
// F 0x2002 + v (peg grid and objects), the board with its pegs, the objects drawn back to front with the
// buttons on top (PzGears_Draw) and the inventory (the tools' picture); in wave 3 the run on the game
// (game/pz_gears_run.cpp): carrying and placing gears, the crank turned by the mouse with the game
// moving the pointer, the propagation through the drive table DS:3902 and the reset.
//
// F 0x2002 + v: u8 grid[12][23] (non-zero = a peg), u16 n at +0x114, n objects of 20 bytes at +0x116
// {type, state, column, row, length, then run-time fields}. Types: 0 small gear, 1 large gear, 2 / 3
// horizontal / vertical plain rack segment, 4 / 5 toothed, 6 the crank, 7..10 buttons.
//
// The puzzle's globals are GgState::puzzles.gears (GearsState: the variant copy's grid and objects, the
// stock, the crank rectangles and centre, the gear in the hand, the jam flag). A tool works on the
// object's own copy; the game binds the object to its state.

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/game_state.h"
#include "gg/game/puzzles.h"
#include "gg/gfx/cursor.h"

namespace tmport::gg {

class GgGame;

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
    static constexpr uint16_t kEffectBank = 0x11C8;
    static constexpr int kCols = 23, kRows = 12;
    static constexpr int kNotPlaced = -99;
    static constexpr int kMaxGearZones = 100;  // the zone array of PzGears_AddZones (Mem_Alloc(0x898))

    using Object = GearObjectRec;  // 20 bytes: type, state, column, row, length, run-time fields

    PzGears(const GgExe& exe, const Archive& archive);

    // 3b2f:1b83 PzGears_LoadVariant (the copy, its grid and objects; on the game the crank rectangles
    // and the crank zone too) and 3b2f:1bd5 PzGears_LoadResources.
    void loadVariant(int variant);
    void loadResources(int variant);
    // 3b2f:1ed0 PzGears_Run up to the loop, as a tool shows it: variant, resources, the hand, the panel
    // (before the screen: the screen's 157 rows cover its top two), the screen with all 276 pegs, the
    // back page, PzGears_Draw(1), the stock, the inventory, the fade-in, PzGears_SaveHome.
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 3b2f:01e4 PzGears_DrawPeg(c, r, colour).
    void drawPeg(Screen& screen, int c, int r, uint8_t colour) const;
    // 3b2f:024f PzGears_Draw(sounds): restore (84, 1, 226, 160), clip x 86..308, y 23..152, objects
    // n-1 down to 0 (propagation fields cleared; a gear off the board is not drawn), then the buttons
    // 1..n-1 in index order; with `sounds` on the game effect 4 (all down) or 3 (a new one), prio 2; the
    // clip restored; flush. Returns (all buttons down, a button went down).
    std::pair<bool, bool> draw(Video& video, bool sounds);
    // 3b2f:0033 PzGears_CountStock / 3b2f:00a9 PzGears_DrawInventory (flushes).
    void countStock();
    void drawInventory(Video& video);
    // 3b2f:1da8 PzGears_SaveHome: gears +8 = frame, racks +2 = column or row (the reset's home).
    void saveHome();
    std::string describe() const override;

    int objectCount() const { return static_cast<int>(st_->objects.size()); }
    std::vector<Object>& objects() { return st_->objects; }
    const std::vector<Object>& objects() const { return st_->objects; }
    bool peg(int c, int r) const { return st_->grid[static_cast<size_t>(r * kCols + c)] != 0; }
    GearsState& st() { return *st_; }
    const GearsState& st() const { return *st_; }


    // ---- wave 3: the run (owner: puzzles_b agent; game/pz_gears_run.cpp) ----
    // The puzzle's globals are GgState::puzzles.gears: bind() points the class at them.
    void bind(GearsState& state) { st_ = &state; }
    bool bound() const { return st_ != &own_; }
    // 3b2f:1ed0 PzGears_Run: the puzzle (code_puzzles_b.md §3): set-up, hint 0x1052, the loop (0x4b0..0x513 tested after the switch CS:206e, 11 codes), cleanup; 1 when solved. The game moves the pointer (PzGears_WarpMouse: GgMouse::warp and its onWarp hook).
    int run(GgGame& g, int variant);
    // 3b2f:0000 PzGears_AllButtonsPressed: every button object (type >= 7) has state != 0.
    bool allButtonsPressed();
    // 3b2f:0070 PzGears_AddZones: the gear zone array; the board, inventory circles and Hint zones.
    void addZones(GgGame& g);
    // 3b2f:079b PzGears_InitCrankRects: the crank centre and the 26 handle rectangles on screen (GgState gearsCrankRect).
    void initCrankRects();
    // 3b2f:081e PzGears_SetCrankZone: the crank handle zone for the current crank frame.
    void setCrankZone(GgGame& g);
    // 3b2f:0892 PzGears_AddGearZone: the circle zone of placed gear i, press 0x4b0 + i.
    void addGearZone(GgGame& g, int i);
    // 3b2f:0972 PzGears_RemoveGearZone: removes the zone of gear i.
    void removeGearZone(GgGame& g, int i);
    // 3b2f:098f PzGears_WarpMouse: g_mouseX/Y and INT 33h function 4 (GgMouse::warp; the host pointer follows through onWarp).
    void warpMouse(GgGame& g, int x, int y);
    // 3b2f:09b3 PzGears_PutBack: the carried gear back to the inventory, effect 1.
    void putBack(GgGame& g);
    // 3b2f:0a22 PzGears_TakeFromStock: a gear of that type into the hand (or the carried one put back).
    void takeFromStock(GgGame& g, int type);
    // 3b2f:0b18 PzGears_PickUp: placed gear i back into the hand.
    void pickUp(GgGame& g, int i);
    // 3b2f:0c2b PzGears_Place: the carried gear onto the peg under its centre when allowed, effect 1.
    void place(GgGame& g, int mx, int my);
    // 3b2f:0ecc PzGears_GearToGear: the mesh test and the opposite turn of gear j.
    int gearToGear(int i, int j);
    // 3b2f:0ff9 PzGears_HRackToHRack: a horizontal rack segment pushes or pulls segment j.
    int hRackToHRack(int i, int j);
    // 3b2f:111d PzGears_HRackToButton: presses or releases button j (types 7, 8); 0.
    int hRackToButton(int i, int j);
    // 3b2f:11dc PzGears_VRackToButton: presses or releases button j (types 9, 10); 0.
    int vRackToButton(int i, int j);
    // 3b2f:129b PzGears_VRackToVRack: a vertical rack segment pushes or pulls segment j.
    int vRackToVRack(int i, int j);
    // 3b2f:13bf PzGears_GearToHRack: gear i moves toothed horizontal rack j by one unit.
    int gearToHRack(int i, int j);
    // 3b2f:14ae PzGears_HRackToGear: toothed horizontal rack i turns gear j.
    int hRackToGear(int i, int j);
    // 3b2f:15c3 PzGears_GearToVRack: gear i moves toothed vertical rack j by one unit.
    int gearToVRack(int i, int j);
    // 3b2f:16b2 PzGears_VRackToGear: toothed vertical rack i turns gear j.
    int vRackToGear(int i, int j);
    // 3b2f:17c7 PzGears_Propagate: the depth-first drive from object i through g_gearsDriveTable DS:3902.
    void propagate(int i);
    // 3b2f:185c PzGears_TurnCrank: one crank step (-1 clockwise, +1 counter-clockwise), the propagation, undone on a jam.
    void turnCrank(GgGame& g, int dir);
    // 3b2f:1991 PzGears_RectContains: a point in a rectangle (no caller).
    bool rectContains(const RectRec& r, int x, int y);
    // 3b2f:19bf PzGears_DragCrank: turns the crank by the mouse's motion while the button is held (no input during the drag: keep).
    void dragCrank(GgGame& g);
    // 3b2f:1cad PzGears_Free: frees everything, the arrow cursor, the zones cleared, the bank freed.
    void free(GgGame& g);
    // 3b2f:1e0a PzGears_SaveFrames: the gear and crank frames at the start of a crank drag.
    void saveFrames();
    // 3b2f:1e46 PzGears_Reset: after an unsuccessful drag: the frames, the racks home, the buttons up, the crank zone, a redraw.
    void reset(GgGame& g);

    // The drive table DS:3902 (near code pointers, 6 rows x 11 columns) mapped to the handlers by the
    // pointers' targets in ovl22: the handler of (source type, target type), nullptr for none.
    using Drive = int (PzGears::*)(int, int);
    Drive driveHandler(int sourceType, int targetType) const;
    // The tables of DS:36e2 (squared cell distances) and DS:36ea (mesh) and DS:38e8 (frames).
    int minDist2(int t, int u) const;
    bool meshes(int t, int u, int dy, int dx) const;
    int frames(int t) const;
    // The quadrant and slope of PzGears_DragCrank for a point (x, y) around the crank centre.
    int quadrant(int x, int y) const;
    int slope(int x, int y) const;

    // A moment of the main loop from a DOSBox-X state (puzzles.h).
    bool restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) override;
    int resume(GgGame& g) override;

private:
    int tableS16(uint16_t base, int i) const;
    void flush(Video& video);
    bool carrying(const GgGame& g) const;  // the cursor is the carried gear's record (DS:0a6c == DS:b6c6)
    int loop(GgGame& g, bool solved);
    std::unique_ptr<CursorRecord> gearCursor(int i) const;  // Mouse_LoadCursors of i's frame, hot spot (r, r)

    const GgExe& exe_;
    const Archive& archive_;
    GearsState own_;
    GearsState* st_ = &own_;
    GgGame* game_ = nullptr;
    std::array<std::array<Drive, 11>, 6> drive_{};
    Bundle gears_[2], grip_, crank_, buttons_, digits_, racks_;
    std::vector<uint8_t> panelText_, hintText_, hintPressed_;
    std::unique_ptr<CursorRecord> cursor_, blank_, held_;  // DS:b6ca, DS:b6ce, DS:b6c6
    // The zones: the board DS:37b2, the inventory circles DS:37c8 / DS:37de, Hint DS:38ec, the crank
    // handle DS:b818 and the gear zone array of the heap (DS:b7e8).
    Zone boardZone_, largeZone_, smallZone_, hintZone_, crankZone_;
    std::array<Zone, kMaxGearZones> gearZones_{};
    int variant_ = 0;
};

}  // namespace tmport::gg
