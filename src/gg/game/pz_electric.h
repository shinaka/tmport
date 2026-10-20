#pragma once
// The electric circuits puzzle, kind 0 (ovl13 3083, electric.c; re/gg/docs/code_puzzles_a.md §3): the
// variant data F 0x20bf + v (fixed pieces and their links), the terminal geometry (in the overlay's code
// as straight-line stores, read here by the interpreter of puzzles.h), the tool table DS:24e6, the board
// with its pieces and wires, the toolbox and its cursor regions, the circuit evaluation (the path search
// of PzElec_BulbLit and PzElec_BatteryShorted, reproduced literally) and, in wave 3, the run on the game
// (game/pz_electric_run.cpp): taking, placing and wiring pieces, the switches and the success test.
//
// F 0x20bf + v (140 words): 0..4 toolbox counts (battery, bulb, switch, Y-wire, wire), 5..19 piece type
// per slot (the list ends at the first 5), 20..34 x, 35..49 y, then per terminal t 0..2 two arrays of
// 15: linked piece (99 none) and linked terminal.
//
// The puzzle's globals are GgState::puzzles.electric (ElectricState: the pieces, their terminals, the
// path table, the mode, the toolbox counts, ...). A tool (no game) works on the object's own copy; the
// game binds the object to its state (bind). The pieces' current pictures (+6, far) are the engine's.

#include <array>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/game_state.h"
#include "gg/game/puzzles.h"
#include "gg/gfx/cursor.h"

namespace tmport::gg {

class GgGame;

class PzElectric final : public Puzzle {
public:
    static constexpr uint16_t kToolPics = 0x20B7;   // battery, bulb, switch, Y-wire, wire spool
    static constexpr uint16_t kCursor = 0x20B4;     // the puzzle cursor, click offset x 1
    static constexpr uint16_t kDigits = 0x20B5;
    static constexpr uint16_t kLabels = 0x20B6;     // the toolbox labels (cursors)
    static constexpr uint16_t kPlug = 0x20BA;
    static constexpr uint16_t kSwitchClosed = 0x20BC;
    static constexpr uint16_t kBulbLit = 0x20BB;
    static constexpr uint16_t kDotUpper = 0x20B8;
    static constexpr uint16_t kDotLower = 0x20B9;
    static constexpr uint16_t kHintTextFirst = 0x20DD;
    static constexpr uint16_t kPanelTextFirst = 0x20FB;
    static constexpr uint16_t kVariantFirst = 0x20BF;
    static constexpr uint16_t kEffectBank = 0x11CC;
    static constexpr uint16_t kCodeSeg = 0x3083;
    static constexpr int kSlots = 15;
    static constexpr int kNone = 99;
    static constexpr int kMaxPaths = 10;  // DS:a138 holds 10 rows of 20 words

    enum Type { Battery = 0, Bulb = 1, Switch = 2, YWire = 3, Wire = 4 };

    struct Tool {  // DS:24e6, 16 bytes
        int x = 0, y = 0, digitX = 0, digitY = 0, hotX = 0, hotY = 0;
        std::span<const uint8_t> picture;
    };
    // A piece of the bound state with its three terminals (DS:a2fc + 16 i, DS:a3ec + 0x4e i).
    struct PieceView {
        ElecPieceRec& rec;
        std::array<ElecTerminalRec, 3>& t;
    };
    struct PiecesView {
        ElectricState* s;
        PieceView operator[](size_t i) const { return {s->pieces[i], s->terminals[i]}; }
        static constexpr size_t size() { return kSlots; }
    };
    // The toolbox counts DS:a8c6 of the bound state as ints (read access for tools and tests).
    struct ToolCountsView {
        const PzElectric* pz;
        int operator[](size_t i) const { return pz->st().toolCounts[i]; }
    };

    PzElectric(const GgExe& exe, const Archive& archive);

    // 3083:256b PzElec_LoadResources and 3083:22bf PzElec_LoadVariant (pieces created and
    // auto-connected, then every link of the data written, made symmetric).
    void loadResources(int variant);
    void loadVariant(int variant);
    // 3083:08a1 PzElectric_Run up to the loop, as a tool shows it: resources, variant, the whole-screen
    // cursor region, the tool label regions, PzElec_DrawScreen (3083:250e: the screen, the panel, the
    // back page, the board, the fade-in, the puzzle cursor), the toolbox, the label regions of the tools
    // in stock.
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 3083:1f35 PzElec_SetTerminalGeometry(piece, type): links 99, then the code's constants.
    void setTerminalGeometry(int piece, int type);
    // 3083:1e20 PzElec_CreatePiece(x, y) of type `mode`, 3083:1850 PzElec_AutoConnect(p).
    void createPiece(int x, int y);
    void autoConnect(int p);
    // 3083:1c99 PzElec_DrawBoard(flush): restore (91, 17, 222, 135), the used pieces (a bulb first
    // takes its lit or unlit picture: PzElec_SetBulbPicture, with its sound in the game), the wires;
    // flush when asked.
    void drawBoard(Video& video, bool flush);
    // 3083:0fcf PzElec_DrawWires, 3083:10ac PzElec_DrawWireL, 3083:12cc PzElec_DrawTerminalDot.
    void drawWires(Screen& screen) const;
    void drawWireL(Screen& screen, int x1, int y1, int x2, int y2) const;
    void drawTerminalDot(Screen& screen, int p, int t) const;
    // 3083:163c PzElec_DrawToolbox (flushes).
    void drawToolbox(Video& video);

    // 3083:04c7 PzElec_BulbLit(s), 3083:0129 PzElec_BatteryShorted(s), 3083:0492
    // PzElec_CheckBatteries, as code_puzzles_a.md §3.8.
    bool bulbLit(int s);
    bool batteryShorted(int s);
    void checkBatteries();
    std::string describe() const override;

    int usedPieces() const;
    PiecesView pieces() { return {st_}; }
    ToolCountsView toolCounts{this};
    bool pathOverflow = false;  // more than 10 rows were found (the original corrupts DS:a2c8)
    ElectricState& st() { return *st_; }
    const ElectricState& st() const { return *st_; }


    // ---- wave 3: the run (owner: puzzles_a agent; game/pz_electric_run.cpp) ----
    // The puzzle's globals are GgState::puzzles.electric: bind() points the class at them.
    void bind(ElectricState& state) { st_ = &state; }
    bool bound() const { return st_ != &own_; }
    // 3083:08a1 PzElectric_Run: the puzzle (code_puzzles_a.md §3): set-up, PzSnd bank 0x11cc, hint 0x1052, the loop on Input_StripHeld(Input_GetCode()) (switch CS:0add, 14 codes), the success order of §2.6, cleanup; 1 when solved during this visit.
    int run(GgGame& g, int variant);
    // 3083:0844 PzElec_SetBulbPicture: lit: picture 0x20bb (effect 1 when it changes); unlit: the bulb's tool picture.
    void setBulbPicture(GgGame& g, int bulb, bool lit);
    // 3083:0b15 PzElec_LayWire: wire mode click: a rubber band from a free terminal to a free terminal of another piece; a miss loses the wire.
    void layWire(GgGame& g);
    // 3083:0d1a PzElec_SetMouseRange: INT 33h fn 7/8: the board while a wire is strung, else the screen less the cursor size.
    void setMouseRange(GgGame& g, bool board);
    // 3083:0d97 PzElec_CutWire: an idle click on a drawn connection unlinks it and puts a wire in the hand (mode 4).
    void cutWire(GgGame& g);
    // 3083:0f18 PzElec_OnWire: the point on the L-shaped wire (horizontal at y1, then vertical at x2), 3 pixels wide.
    bool onWire(int px, int py, int x1, int y1, int x2, int y2);
    // 3083:11a8 PzElec_DrawRubberBand: the L from the wire's start anchor (DS:a8bc / a8ba) to (x, y), then the start dot.
    void drawRubberBand(GgGame& g, int x, int y);
    // 3083:1327 PzElec_ToggleSwitch: a click inside the lever (x+7, y+20, 9, 16) flips the switch picture and state; true when toggled.
    bool toggleSwitch(GgGame& g, int piece, int mx, int my);
    // 3083:13c5 PzElec_PickUpPiece: a non-fixed piece off the board: unlinked (its wires are lost), the slot freed, the piece cursor.
    void pickUpPiece(GgGame& g, int piece);
    // 3083:156a PzElec_ReturnToToolbox: the held piece or wire back into the toolbox (count + 1), idle mode.
    void returnToToolbox(GgGame& g);
    // 3083:15ad PzElec_TakeFromToolbox: toolbox item code - 0x45b (count > 0): count - 1, its picture as the cursor, mode = its type.
    void takeFromToolbox(GgGame& g, int code);
    // 3083:172a PzElec_PlacePiece: a board click with a piece in hand: placed at the mouse when inside the board and not overlapping (no success test: keep).
    void placePiece(GgGame& g);
    // 3083:1a94 PzElec_UpdatePlugCursor: wire mode: the plug picture 0x20ba as the cursor over a free terminal, the held picture elsewhere.
    void updatePlugCursor(GgGame& g);
    // 3083:1b75 PzElec_FindFreeTerminal: the first free terminal (of a piece other than DS:a8c2) whose hit rectangle holds the hot point: DS:a8c0 / a8c2.
    bool findFreeTerminal(GgGame& g);
    // 3083:1c63 PzElec_PointInRect: half-open rectangle test.
    bool pointInRect(int px, int py, int x, int y, int w, int h);
    // 3083:1d1d PzElec_AddPieceZones: the zone of every placed piece (press 0x4b0 + slot).
    void addPieceZones(GgGame& g);
    // 3083:1d43 PzElec_RemovePieceZones: removes them.
    void removePieceZones(GgGame& g);
    // 3083:1d69 PzElec_AddToolZones: the 5 toolbox zones (0x45b..0x45f).
    void addToolZones(GgGame& g);
    // 3083:1d83 PzElec_AddToolCursorRegions: the label cursors over the toolbox items with a count.
    void addToolCursorRegions(GgGame& g);
    // 3083:1dab PzElec_InitToolZones: the toolbox zone records DS:a8dc from the tool table DS:24e6.
    void initToolZones(GgGame& g);
    // 3083:1f22 PzElec_AddBoardZones: the board zone DS:24ba (0x454) and the Hint zone DS:24d0 (0x458).
    void addBoardZones(GgGame& g);
    // 3083:216e PzElec_AllBulbsLit: every slot of type 1 (placed or not) passes PzElec_BulbLit.
    bool allBulbsLit();
    // 3083:2196 PzElec_IsSolved: the success test of code_puzzles_a.md §3.9.
    bool isSolved();
    // 3083:2695 PzElec_Cleanup: frees everything, clears the zones and cursor regions, the default cursor.
    void cleanup(GgGame& g);

    // A moment of the main loop from a DOSBox-X state (puzzles.h).
    bool restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) override;
    int resume(GgGame& g) override;

private:
    // The closed paths of piece s (the search of §3.8): rows of piece numbers ending with s; also
    // written into the path table DS:a138 / DS:a2c8 of the state.
    std::vector<std::vector<int>> findPaths(int s);
    ElecTerminalRec& term(int p, int t) { return st_->terminals[static_cast<size_t>(p)][static_cast<size_t>(t)]; }
    const ElecTerminalRec& term(int p, int t) const { return st_->terminals[static_cast<size_t>(p)][static_cast<size_t>(t)]; }
    ElecPieceRec& piece(int p) { return st_->pieces[static_cast<size_t>(p)]; }
    const ElecPieceRec& piece(int p) const { return st_->pieces[static_cast<size_t>(p)]; }
    // The game's run: the set-up of 3083:08a1 (Normal entry), the loop (3083:0902..0ac7) and the exit.
    void setupGame(GgGame& g, int variant);
    int loop(GgGame& g, bool solved);
    void flush(Video& video);
    void pzPlay(int i, int priority);
    // Mouse_LoadCursors(far picture) with the click offset of a tool table entry (the held cursor).
    CursorRecord* newHeldCursor(std::span<const uint8_t> picture, int hotX, int hotY);
    // Mouse_FreeCursor of the held cursor (kept alive until the cleanup: the cursor compares records
    // by address).
    void freeHeldCursor();
    // INC word [DS:a8ce]: the toolbox's wire count g_elecToolCounts[4] + 1.
    void wireBack();

    const GgExe& exe_;
    const Archive& archive_;
    ElectricState own_;
    ElectricState* st_ = &own_;
    GgGame* game_ = nullptr;  // the game while run() runs (sounds, the presenting flush)
    std::array<std::span<const uint8_t>, kSlots> picture_{};  // +6 far current picture of each slot
    std::array<Tool, 5> tools_{};
    // The geometry stores of each type (displacement from piece 0's terminal table, value).
    std::array<std::vector<std::pair<uint16_t, uint16_t>>, 4> geometry_;
    Bundle toolPics_, digits_, labels_;
    std::vector<uint8_t> plug_, switchClosed_, bulbLitPic_, dotUpper_, dotLower_, panelText_, hintText_, hintPressed_;
    std::unique_ptr<CursorRecord> cursor_, unused_;
    CursorRecord* held_ = nullptr;                          // DS:a8d4 g_elecHeldCursor
    std::vector<std::unique_ptr<CursorRecord>> heldRecords_;
    std::array<std::unique_ptr<CursorRecord>, 5> labelCursors_;
    CursorRegion screenRegion_;
    std::array<CursorRegion, 5> toolRegions_{};
    // The click zones (DGROUP records): the board DS:24ba, Hint DS:24d0, the toolbox area DS:24a4,
    // the toolbox items DS:a8dc + 0x16 i, the placed pieces DS:a94a + 0x16 i.
    Zone boardZone_, hintZone_, toolboxZone_;
    std::array<Zone, 5> toolZones_{};
    std::array<Zone, kSlots> pieceZones_{};
    int variant_ = 0;
};

}  // namespace tmport::gg
