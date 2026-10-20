#pragma once
// The electric circuits puzzle, kind 0 (ovl13 3083, electric.c; re/gg/docs/code_puzzles_a.md §3), the
// data and drawing half: the variant data F 0x20bf + v (fixed pieces and their links), the terminal
// geometry (in the overlay's code as straight-line stores, read here by the interpreter of
// puzzles.h), the tool table DS:24e6, the board with its pieces and wires, the toolbox and its cursor
// regions. The board drawing needs whether each bulb is lit, so the circuit evaluation (the path search
// of PzElec_BulbLit and PzElec_BatteryShorted, reproduced literally) is here too. Taking, placing and
// wiring pieces, the switches and the success test are wave 3's.
//
// F 0x20bf + v (140 words): 0..4 toolbox counts (battery, bulb, switch, Y-wire, wire), 5..19 piece type
// per slot (the list ends at the first 5), 20..34 x, 35..49 y, then per terminal t 0..2 two arrays of
// 15: linked piece (99 none) and linked terminal.

#include <array>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/puzzles.h"
#include "gg/gfx/cursor.h"

namespace tmport::gg {

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
    static constexpr uint16_t kCodeSeg = 0x3083;
    static constexpr int kSlots = 15;
    static constexpr int kNone = 99;
    static constexpr int kMaxPaths = 10;  // DS:a138 holds 10 rows of 20 words

    enum Type { Battery = 0, Bulb = 1, Switch = 2, YWire = 3, Wire = 4 };

    struct Terminal {  // 0x1a bytes (DS:a3ec + 0x4e i + 0x1a t)
        int hitX = 0, hitY = 0, hitW = 0, hitH = 0;
        int anchorX = 0, anchorY = 0;
        int dotX = 0, dotY = 0;
        int overlapH = 0;
        int piece = kNone, term = 0;  // +0x12, +0x14
        int flag = 0;                 // +0x16: 1 owns a drawn wire, 3 contact by placing, 0 passive
    };
    struct Piece {  // 16 bytes (DS:a2fc + 16 i)
        int type = 0, x = 0, y = 0;
        std::span<const uint8_t> picture;  // +6
        bool used = false, closed = false, fixed = false;
        std::array<Terminal, 3> t{};
    };
    struct Tool {  // DS:24e6, 16 bytes
        int x = 0, y = 0, digitX = 0, digitY = 0, hotX = 0, hotY = 0;
        std::span<const uint8_t> picture;
    };

    PzElectric(const GgExe& exe, const Archive& archive);

    // 3083:256b PzElec_LoadResources and 3083:22bf PzElec_LoadVariant (pieces created and
    // auto-connected, then every link of the data written, made symmetric).
    void loadResources(int variant);
    void loadVariant(int variant);
    // 3083:08a1 PzElectric_Run up to the loop: resources, variant, the whole-screen cursor region,
    // the tool label regions, PzElec_DrawScreen (3083:250e: the screen, the panel, the back page, the
    // board, the fade-in, the puzzle cursor), the toolbox, the label regions of the tools in stock.
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 3083:1f35 PzElec_SetTerminalGeometry(piece, type): links 99, then the code's constants.
    void setTerminalGeometry(int piece, int type);
    // 3083:1e20 PzElec_CreatePiece(x, y) of type `mode`, 3083:1850 PzElec_AutoConnect(p).
    void createPiece(int x, int y);
    void autoConnect(int p);
    // 3083:1c99 PzElec_DrawBoard(flush): restore (91, 17, 222, 135), the used pieces (a bulb first
    // takes its lit or unlit picture), the wires; flush when asked.
    void drawBoard(Video& video, bool flush);
    // 3083:0fcf PzElec_DrawWires, 3083:10ac PzElec_DrawWireL, 3083:12cc PzElec_DrawTerminalDot.
    void drawWires(Screen& screen) const;
    void drawWireL(Screen& screen, int x1, int y1, int x2, int y2) const;
    void drawTerminalDot(Screen& screen, int p, int t) const;
    // 3083:163c PzElec_DrawToolbox (flushes).
    void drawToolbox(Video& video) const;

    // 3083:04c7 PzElec_BulbLit(s), 3083:0129 PzElec_BatteryShorted(s), 3083:0492
    // PzElec_CheckBatteries, as code_puzzles_a.md §3.8.
    bool bulbLit(int s);
    bool batteryShorted(int s);
    void checkBatteries();
    std::string describe() const override;

    int usedPieces() const;
    std::array<Piece, kSlots>& pieces() { return pieces_; }
    std::array<int, 5> toolCounts{};  // DS:a8c6
    int mode = kNone;                 // DS:a8be
    std::array<int, kSlots> shorted{};  // DS:a87e
    std::array<int, kSlots> onPath{};   // DS:a89c
    bool pathOverflow = false;          // more than 10 rows were found (the original corrupts DS:a2c8)

private:
    // The closed paths of piece s (the search of §3.8): rows of piece numbers ending with s.
    std::vector<std::vector<int>> findPaths(int s);
    Terminal& term(int p, int t) { return pieces_[static_cast<size_t>(p)].t[static_cast<size_t>(t)]; }
    const Terminal& term(int p, int t) const { return pieces_[static_cast<size_t>(p)].t[static_cast<size_t>(t)]; }

    const GgExe& exe_;
    const Archive& archive_;
    std::array<Piece, kSlots> pieces_{};
    std::array<Tool, 5> tools_{};
    // The geometry stores of each type (displacement from piece 0's terminal table, value).
    std::array<std::vector<std::pair<uint16_t, uint16_t>>, 4> geometry_;
    Bundle toolPics_, digits_, labels_;
    std::vector<uint8_t> plug_, switchClosed_, bulbLitPic_, dotUpper_, dotLower_, panelText_;
    std::unique_ptr<CursorRecord> cursor_, unused_;
    std::array<std::unique_ptr<CursorRecord>, 5> labelCursors_;
    CursorRegion screenRegion_;
    std::array<CursorRegion, 5> toolRegions_{};
    int variant_ = 0;
};

}  // namespace tmport::gg
