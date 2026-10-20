#pragma once
// The energy forms puzzle, kind 6 (ovl15 33cd; re/gg/docs/code_puzzles_a.md §4), the data and drawing
// half: the 28 variants, which live in the overlay's code as straight-line stores (read here by a small
// interpreter of exactly the three opcodes the code uses), the device pictures, the layout DS:28c8..
// 28e8 and the form names DS:28ea of the user's ssg.exe, the screen with the right-aligned names, the
// hidden connectors of empty rows, the devices' idle frames and the pencil count. Drawing lines, the
// eraser, sending energy and the device animations are wave 3's.
//
// PzEnergy_LoadVariant 33cd:1274 clears DS:adb8 (0x1a0 bytes) and DS:ad70 (0x120), jumps through CS:1b69
// (28 entries) into code made only of `MOV word [imm16], imm16` (C7 06 lo hi vlo vhi) and JMP rel16 /
// rel8 (E9, EB) ending at 33cd:1b23, which derives satisfied[k][d] (0 when device d uses form[k]).

#include <array>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/puzzles.h"

namespace tmport::gg {

class PzEnergy final : public Puzzle {
public:
    static constexpr uint16_t kPencil = 0x1097;
    static constexpr uint16_t kFormTexts = 0x1130;
    static constexpr uint16_t kHintHeader = 0x1131;
    static constexpr uint16_t kPanelFormat = 0x1132;  // one %d: the number of lines
    static constexpr uint16_t kCodeSeg = 0x33CD;
    static constexpr uint16_t kVariantTable = 0x1B69; // CS:1b69, 28 entries
    static constexpr uint16_t kVariantEnd = 0x1B23;
    static constexpr int kVariants = 28;

    // The variant as PzEnergy_LoadVariant leaves DGROUP (code_puzzles_a.md §4.2, §4.3).
    struct Variant {
        int budget = 0;                     // DS:ae8a
        std::array<int, 4> form{};          // DS:adb8 + 0x1a k (-1 empty)
        std::array<uint16_t, 4> device{};   // DS:ad70 + 0x12 d (0xffff empty)
        std::array<std::array<int, 8>, 4> uses{};       // DS:ad72 + 0x12 d + 2 f
        std::array<std::array<int, 4>, 4> satisfied{};  // DS:adba + 0x1a k + 2 d
        int stores = 0;                     // MOV instructions run
    };
    // Runs the variant code of the user's ssg.exe. Throws tmport::FormatError on any other opcode, a
    // jump outside the overlay or a run without end.
    static Variant readVariant(const GgExe& exe, int variant);

    PzEnergy(const GgExe& exe, const Archive& archive);

    // 33cd:0000 PzEnergy_Run up to the loop: mode 0 (pencil), the variant, PzEnergy_LoadResources
    // (33cd:1cc7: digits, pencil, the hand, pencil and eraser cursors, every used device's idle
    // bundle at frame 0), PzEnergy_DrawScreen (33cd:1ba1: the screen at (0, 10), the names, the panel
    // with the line count, empty rows' connectors hidden, idle frame 0 of each device, the back page,
    // the pencil count, the pencil cursor, the fade-in).
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 33cd:0735 PzEnergy_BuildHintAndLabels, the drawing: font 0, colour 0, spacing 1, each used
    // row's name right-aligned to end at DS:28de - 4 at y DS:28e2[k] - 4 (Font_DrawString), spacing 2.
    void drawLabels(Screen& screen, Fonts& fonts) const;
    // 33cd:1135 PzEnergy_DrawPencilCount (flushes).
    void drawPencilCount(Video& video) const;
    // Device d's idle frame n in its area (the fill and the keyed frame of PzEnergy_IdleAnimate
    // 33cd:064d, then Gfx_SaveToBack of the area).
    void drawIdleFrame(Screen& screen, int d, int n) const;
    // A save state's idle frames: the frame each device shows (DS:ae20[d] counts one ahead).
    void applyScene(Video& video, const PuzzleSceneValues& scene) override;
    std::string describe() const override;

    const Variant& variant() const { return v_; }
    int linesLeft() const { return v_.budget; }
    int mode = 0;                     // g_energyMode DS:ae88
    std::array<int, 4> idleFrame{};   // g_energyIdleFrame DS:ae20

private:
    int layout(int i) const;  // DS:28c8 + 2 i
    std::vector<uint8_t> formName(int f) const;

    const GgExe& exe_;
    const Archive& archive_;
    Variant v_;
    std::array<Bundle, 4> devices_;
    Bundle digits_;
    std::vector<uint8_t> pencil_;
    std::vector<uint8_t> panelText_;
    std::unique_ptr<CursorRecord> arrow_, pencilCursor_, eraserCursor_;
    int variant_ = 0;
};

// Borland sprintf of a format with one %d (and %% escapes), as the panel line count uses it.
std::vector<uint8_t> formatOneInt(std::span<const uint8_t> format, int value);

}  // namespace tmport::gg
