#pragma once
// The machine pieces puzzle, kind 2 (ovl21 3a4a; re/gg/docs/code_puzzles_b.md §2): the variant
// records F 0x209c + v, the pieces and the assembled machine of bundle 0x2088 + v, the screen with the
// box count and the work area's drawing of placed pieces (the tools' picture); in wave 3 the run on
// the game (game/pz_pieces_run.cpp): taking pieces out of the box (rand), the snap, the groups and
// their slides, the reveal.
//
// F 0x209c + v: 10 records of 18 bytes (the layout of g_pieces DS:b51e); only +0x0c u8 dx and +0x0d
// u8 dy (the piece's place inside the machine picture) are data. Bundle 0x2088 + v: members 0..n-1
// the pieces, member n the machine.
//
// The puzzle's globals are GgState::puzzles.pieces (PiecesState: the records, the box count, the
// count, the moving group, the piece in the hand). A tool works on the object's own copy; the game
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

class PzPieces final : public Puzzle {
public:
    static constexpr uint16_t kVariantFirst = 0x209C;
    static constexpr uint16_t kBundleFirst = 0x2088;
    static constexpr uint16_t kBoxPics = 0x2087;   // lid closed, half open, open
    static constexpr uint16_t kHintFormat = 0x20B1;
    static constexpr uint16_t kPanelText = 0x20B0;
    static constexpr uint16_t kEffectBank = 0x11CA;
    static constexpr int kMaxPieces = 10;
    static constexpr int kNoPiece = 0x0B;  // g_piecesHeld DS:b5d8 when nothing is held
    // The work area x 93..311, y 14..155 (0x5d, 0x0e, 0x138, 0x9c in the code) and its zone codes.
    static constexpr int kWorkX = 93, kWorkY = 14, kWorkW = 219, kWorkH = 142;
    static constexpr int kWorkRight = kWorkX + kWorkW, kWorkBottom = kWorkY + kWorkH;

    PzPieces(const GgExe& exe, const Archive& archive);

    // 3a4a:0bcb PzPieces_LoadVariant (Res_LoadTo straight into the records) and 3a4a:0ca8
    // PzPieces_LoadResources (n, the box count, every piece's picture, size and group).
    void loadVariant(int variant);
    void loadResources(int variant);
    // 3a4a:0000 PzPieces_Run up to the loop, as a tool shows it: held = none, variant, resources,
    // PzPieces_DrawScreen (the hand, the screen at (0, 10), the panel, the back page, the box count
    // digit at (37, 130), the hand, the fade-in).
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 3a4a:053f PzPieces_RedrawWork: restore (93, 14, 219, 142), every placed piece wholly inside the
    // work area keyed in index order, flush.
    void redrawWork(Video& video);
    std::string describe() const override;

    int pieceCount() const { return st_->count; }
    PiecesState& st() { return *st_; }
    const PiecesState& st() const { return *st_; }
    std::span<const uint8_t> machine() const { return machine_; }
    std::span<const uint8_t> picture(int i) const;


    // ---- wave 3: the run (owner: puzzles_b agent; game/pz_pieces_run.cpp) ----
    // The puzzle's globals are GgState::puzzles.pieces: bind() points the class at them.
    void bind(PiecesState& state) { st_ = &state; }
    bool bound() const { return st_ != &own_; }
    // 3a4a:0000 PzPieces_Run: the puzzle (code_puzzles_b.md §2): set-up, hint 0x1052, the loop (codes 0x45b..0x465 first, then switch CS:01cb), cleanup; 1 when solved.
    int run(GgGame& g, int variant);
    // 3a4a:01ef PzPieces_SetMouseBounds: INT 33h 7/8: the work area less the cursor size, or the whole screen less it.
    void setMouseBounds(GgGame& g, bool work);
    // 3a4a:0265 PzPieces_PickUp: piece code - 0x45b into the hand, effect 0.
    void pickUp(GgGame& g, int code);
    // 3a4a:037d PzPieces_Drop: the carried piece to the mouse, the snap within 10 px, the group move, effect 1.
    void drop(GgGame& g);
    // 3a4a:05b8 PzPieces_MoveGroup: the group to the anchor (ax, ay), kept inside the work area, 5-step slides.
    void moveGroup(GgGame& g, int ax, int ay);
    // 3a4a:071d PzPieces_MergeGroups: the carried piece's group and piece j's become the smaller group number.
    void mergeGroups(int j);
    // 3a4a:0780 PzPieces_TakeFromBox: the box animation, a random piece still in the box (R(n) per try: rand), the wait for the mouse in the work area.
    void takeFromBox(GgGame& g);
    // 3a4a:089c PzPieces_AddZones: the zones of the placed pieces.
    void addZones(GgGame& g);
    // 3a4a:08c9 PzPieces_RemoveZones: removes them.
    void removeZones(GgGame& g);
    // 3a4a:08f6 PzPieces_CheckSolved: all pieces placed on one anchor: the machine picture revealed; true then.
    bool checkSolved(GgGame& g);
    // 3a4a:0a88 PzPieces_OpenBox: effect 12, lid frames 1, 2 at (5, 82), 10 ticks each.
    void openBox(GgGame& g);
    // 3a4a:0ade PzPieces_CloseBox: effect 13, lid frames 1, 0, 10 ticks each.
    void closeBox(GgGame& g);
    // 3a4a:0b33 PzPieces_InitZones: the piece zone records: size, priority 32000 - w*h/100, press 0x45b + i.
    void initZones(GgGame& g);
    // 3a4a:0baf PzPieces_AddStaticZones: the work area, box and Hint zones.
    void addStaticZones(GgGame& g);
    // 3a4a:0be5 PzPieces_DrawScreen: the copy of g_cursor, the hand and the blank cursors, the screen, the panel, the back page, the box count, the hand, the fade-in.
    void drawScreen(GgGame& g);
    // 3a4a:0d95 PzPieces_Free: the zones, the cursor restored, everything freed.
    void free(GgGame& g);
    // The hint text of 3a4a:00ba..0178: sprintf(T 0x20b1, DS:357c[variant]).
    std::vector<uint8_t> hintText(int variant) const;

    // A moment of the main loop from a DOSBox-X state (puzzles.h).
    bool restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) override;
    int resume(GgGame& g) override;

private:
    int loop(GgGame& g, bool solved);
    void loadPictures(int variant);  // the resource half of PzPieces_LoadResources (no state written)
    void flush(Video& video);
    std::unique_ptr<CursorRecord> pieceCursor(int i) const;  // Mouse_LoadCursors + the hot spot (w/2, h/2)

    const GgExe& exe_;
    const Archive& archive_;
    PiecesState own_;
    PiecesState* st_ = &own_;
    GgGame* game_ = nullptr;
    std::array<std::span<const uint8_t>, kMaxPieces> pictures_{};  // +8 of each record
    Bundle bundle_, box_, digits_;
    std::span<const uint8_t> machine_;
    std::vector<uint8_t> panelText_, hintPressed_;
    std::unique_ptr<CursorRecord> cursor_, blank_, held_;  // DS:b6c2, DS:b6b6, DS:b6ba
    const CursorRecord* savedCursor_ = nullptr;             // DS:b6be: the record g_cursor showed at start
    // The zones: the work area DS:353a, the box DS:3550, Hint DS:3566, the pieces DS:b5da + 22 i.
    Zone workZone_, boxZone_, hintZone_;
    std::array<Zone, kMaxPieces> zones_{};
    int variant_ = 0;
};

}  // namespace tmport::gg
