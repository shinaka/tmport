#pragma once
// The machine pieces puzzle, kind 2 (ovl21 3a4a; re/gg/docs/code_puzzles_b.md §2), the data and
// drawing half: the variant records F 0x209c + v, the pieces and the assembled machine of bundle
// 0x2088 + v, the screen with the box count, and the work area's drawing of placed pieces. Taking
// pieces out of the box (rand), the snap, the groups and the reveal are wave 3's.
//
// F 0x209c + v: 10 records of 18 bytes (the layout of g_pieces DS:b51e); only +0x0c u8 dx and +0x0d
// u8 dy (the piece's place inside the machine picture) are data. Bundle 0x2088 + v: members 0..n-1
// the pieces, member n the machine.

#include <array>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/puzzles.h"

namespace tmport::gg {

class PzPieces final : public Puzzle {
public:
    static constexpr uint16_t kVariantFirst = 0x209C;
    static constexpr uint16_t kBundleFirst = 0x2088;
    static constexpr uint16_t kBoxPics = 0x2087;   // lid closed, half open, open
    static constexpr uint16_t kHintFormat = 0x20B1;
    static constexpr uint16_t kPanelText = 0x20B0;
    static constexpr int kMaxPieces = 10;
    static constexpr int kNoPiece = 0x0B;  // g_piecesHeld DS:b5d8 when nothing is held

    // A piece record (DS:b51e + 18 i).
    struct Piece {
        int x = 0, y = 0, w = 0, h = 0;
        std::span<const uint8_t> picture;  // +8
        int dx = 0, dy = 0;                // +0x0c, +0x0d (u8)
        int group = 0;                     // +0x0e
        bool placed = false;               // +0x10
    };

    PzPieces(const GgExe& exe, const Archive& archive);

    // 3a4a:0bcb PzPieces_LoadVariant (Res_LoadTo straight into the records) and 3a4a:0ca8
    // PzPieces_LoadResources (n, the box count, every piece's picture, size and group).
    void loadVariant(int variant);
    void loadResources(int variant);
    // 3a4a:0000 PzPieces_Run up to the loop: held = none, variant, resources, PzPieces_DrawScreen
    // (3a4a:0be5: the hand and the blank cursor, the screen at (0, 10), the panel, the back page, the
    // box count digit at (37, 130), the hand, the fade-in).
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 3a4a:053f PzPieces_RedrawWork: restore (93, 14, 219, 142), every placed piece wholly inside the
    // work area keyed in index order, flush.
    void redrawWork(Video& video) const;
    std::string describe() const override;

    int pieceCount() const { return count_; }
    int inBox = 0;           // g_piecesInBox DS:b5d2
    int held = kNoPiece;     // g_piecesHeld DS:b5d8
    std::array<Piece, kMaxPieces>& pieces() { return pieces_; }
    std::span<const uint8_t> machine() const { return machine_; }

private:
    const Archive& archive_;
    int count_ = 0;
    std::array<Piece, kMaxPieces> pieces_{};
    Bundle bundle_, box_, digits_;
    std::span<const uint8_t> machine_;
    std::vector<uint8_t> panelText_;
    std::unique_ptr<CursorRecord> cursor_, blank_;
    int variant_ = 0;
};

}  // namespace tmport::gg
