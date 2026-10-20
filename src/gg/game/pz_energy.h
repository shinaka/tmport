#pragma once
// The energy forms puzzle, kind 6 (ovl15 33cd; re/gg/docs/code_puzzles_a.md §4): the 28 variants, which
// live in the overlay's code as straight-line stores (read here by a small interpreter of exactly the
// three opcodes the code uses), the device pictures, the layout DS:28c8..28e8 and the form names DS:28ea
// of the user's ssg.exe, the screen with the right-aligned names, the hidden connectors of empty rows,
// the devices' idle frames and the pencil count; in wave 3 the run on the game
// (game/pz_energy_run.cpp): drawing lines, the eraser, sending energy and the device animations.
//
// PzEnergy_LoadVariant 33cd:1274 clears DS:adb8 (0x1a0 bytes) and DS:ad70 (0x120), jumps through CS:1b69
// (28 entries) into code made only of `MOV word [imm16], imm16` (C7 06 lo hi vlo vhi) and JMP rel16 /
// rel8 (E9, EB) ending at 33cd:1b23, which derives satisfied[k][d] (0 when device d uses form[k]).
//
// The puzzle's globals are GgState::puzzles.energy (EnergyState: the devices with their form flags, the
// form rows with satisfied / sent / line words, the idle frames, the mode, the lines left). A tool works
// on the object's own copy; the game binds the object to its state.

#include <array>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/game_state.h"
#include "gg/game/puzzles.h"

namespace tmport::gg {

class GgGame;

class PzEnergy final : public Puzzle {
public:
    static constexpr uint16_t kPencil = 0x1097;
    static constexpr uint16_t kFormTexts = 0x1130;
    static constexpr uint16_t kHintHeader = 0x1131;
    static constexpr uint16_t kPanelFormat = 0x1132;  // one %d: the number of lines
    static constexpr uint16_t kEffectBank = 0x11D2;
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

    // 33cd:0000 PzEnergy_Run up to the loop, as a tool shows it: mode 0 (pencil), the variant,
    // PzEnergy_LoadResources (33cd:1cc7: digits, pencil, the hand, pencil and eraser cursors, every used
    // device's idle bundle at frame 0), PzEnergy_DrawScreen (33cd:1ba1: the screen at (0, 10), the names,
    // the panel with the line count, empty rows' connectors hidden, idle frame 0 of each device, the back
    // page, the pencil count, the pencil cursor, the fade-in).
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 33cd:0735 PzEnergy_BuildHintAndLabels, the drawing: font 0, colour 0, spacing 1, each used
    // row's name right-aligned to end at DS:28de - 4 at y DS:28e2[k] - 4 (Font_DrawString), spacing 2.
    void drawLabels(Screen& screen, Fonts& fonts) const;
    // 33cd:1135 PzEnergy_DrawPencilCount (flushes).
    void drawPencilCount(Video& video);
    // Device d's idle frame n in its area (the fill and the keyed frame of PzEnergy_IdleAnimate
    // 33cd:064d, then Gfx_SaveToBack of the area).
    void drawIdleFrame(Screen& screen, int d, int n) const;
    // A save state's idle frames: the frame each device shows (DS:ae20[d] counts one ahead).
    void applyScene(Video& video, const PuzzleSceneValues& scene) override;
    std::string describe() const override;

    const Variant& variant() const { return v_; }
    int linesLeft() const { return st_->linesLeft; }
    EnergyState& st() { return *st_; }
    const EnergyState& st() const { return *st_; }


    // ---- wave 3: the run (owner: puzzles_a agent; game/pz_energy_run.cpp) ----
    // The puzzle's globals are GgState::puzzles.energy: bind() points the class at them.
    void bind(EnergyState& state) { st_ = &state; }
    bool bound() const { return st_ != &own_; }
    // 33cd:0000 PzEnergy_Run: the puzzle (code_puzzles_a.md §4): set-up, hint 0x1052, the loop (switch CS:01de, 19 codes) with the idle animation (timer slot 7, 31 ticks), cleanup; 1 when solved.
    int run(GgGame& g, int variant);
    // 33cd:022a PzEnergy_SetMouseRange: INT 33h fn 7/8: x 0x3d..0x13f-w, y 0x12..0x97 while a line is drawn, else the screen.
    void setMouseRange(GgGame& g, bool drawing);
    // 33cd:02a0 PzEnergy_SendEnergy: the check button: a pulse along every unsent line; right ones animate their device, wrong ones retract.
    void sendEnergy(GgGame& g);
    // 33cd:03c8 PzEnergy_AnimateDevice: finishes the idle loop, plays the working animation (resources R+1..R+3), leaves its last frame.
    void animateDevice(GgGame& g, int device);
    // 33cd:064d PzEnergy_IdleAnimate: steps the idle animation of every device when timer slot 7 expired (period 31 ticks).
    void idleAnimate(GgGame& g);
    // 33cd:0735 PzEnergy_BuildHintAndLabels: the hint text (header 0x1131 for variants 0..2, then each form's text) and the form labels.
    void buildHintAndLabels(GgGame& g, int variant);
    // 33cd:08f5 PzEnergy_DrawLines: restores the work area and draws every line in colour 0.
    void drawLines(GgGame& g);
    // 33cd:099c PzEnergy_DrawFromForm: the pencil from a form connector: a rubber line until a device connector or a cancel click.
    void drawFromForm(GgGame& g, int code);
    // 33cd:0ae7 PzEnergy_DrawFromDevice: the pencil from a device connector: a rubber line until a form connector or a cancel click.
    void drawFromDevice(GgGame& g, int code);
    // 33cd:0c3e PzEnergy_IsSolved: every needed form-device line drawn.
    bool isSolved();
    // 33cd:0c71 PzEnergy_SelectEraser: the eraser cursor, mode 1.
    void selectEraser(GgGame& g);
    // 33cd:0c97 PzEnergy_EraseLines: while a button is held: every line whose pixel is under the eraser's hot point erased.
    void eraseLines(GgGame& g);
    // 33cd:0db4 PzEnergy_SelectPencil: the pencil cursor, mode 0, when lines are left.
    void selectPencil(GgGame& g);
    // 33cd:0dde PzEnergy_AddZones: the eraser, pencil, hint, check and work-area zones.
    void addZones(GgGame& g);
    // 33cd:0e0c PzEnergy_DrawLine: a stepped line drawn pixel by pixel (vertical lines as a VLine).
    void drawLine(GgGame& g, int x1, int y1, int x2, int y2, int colour);
    // 33cd:0ee7 PzEnergy_AnimatePulse: moves the diamond along a line, a frame every 4 x steps and 5 ticks; erase retracts the line.
    void animatePulse(GgGame& g, int x1, int y1, int x2, int y2, bool erase);
    // 33cd:0ff7 PzEnergy_DrawPulse: the 5x5 diamond outline in colour 0x83, flushed.
    void drawPulse(GgGame& g, int x, int y);
    // 33cd:10a9 PzEnergy_PointOnLine: (px, py) is a pixel PzEnergy_DrawLine would draw (lines with x1 != x2).
    bool pointOnLine(int x1, int y1, int x2, int y2, int px, int py);
    // 33cd:1189 PzEnergy_AddConnectorZones: the circular zones (radius 7) on the used form and device connectors.
    void addConnectorZones(GgGame& g);
    // 33cd:1dd9 PzEnergy_Cleanup: frees everything, clears the zones and cursor regions, the default cursor.
    void cleanup(GgGame& g);

    // A moment of the main loop from a DOSBox-X state (puzzles.h).
    bool restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) override;
    int resume(GgGame& g) override;

private:
    // The layout DS:28c8 + 2 i: 0 device picture x, 1..4 device picture y, 5 / 6 form / device
    // connector zone x, 7..10 the zones' y, 11 / 12 the line ends' x at the forms / devices, 13..16
    // the line ends' y (DS:28e2[4]).
    int layout(int i) const;
    int lineFormX() const { return layout(11); }    // DS:28de
    int lineDeviceX() const { return layout(12); }  // DS:28e0
    int lineY(int row) const { return layout(13 + row); }  // DS:28e2[row]
    std::vector<uint8_t> formName(int f) const;
    void loadVariantIntoState();  // the variant's DGROUP values into the bound state
    void loadResources();         // 33cd:1cc7 PzEnergy_LoadResources (the device bundles of the state)
    void buildHint(int variant);  // the text of 33cd:0735
    int loop(GgGame& g, bool solved);
    void flush(Video& video);
    bool uses(int d, int f) const;  // device d uses form f (DS:ad72 + 0x12 d + 2 f)
    int deviceFrames(int d) const;  // members of device d's idle bundle

    const GgExe& exe_;
    const Archive& archive_;
    EnergyState own_;
    EnergyState* st_ = &own_;
    GgGame* game_ = nullptr;
    Variant v_;
    std::array<Bundle, 4> devices_;
    Bundle digits_, formTexts_;
    std::vector<uint8_t> pencil_, panelText_, hintHeader_, hintText_, hintPressed_, checkPressed_;
    std::unique_ptr<CursorRecord> arrow_, pencilCursor_, eraserCursor_;
    // The click zones: eraser DS:2870, pencil DS:2886, Hint DS:289c, check DS:28b2, the work area DS:285a,
    // the device and form connectors DS:ae8c + 0x16 d / DS:aee4 + 0x16 k.
    Zone eraserZone_, pencilZone_, hintZone_, checkZone_, workZone_;
    std::array<Zone, 4> deviceZones_{}, formZones_{};
    int variant_ = 0;
};

// Borland sprintf of a format with one %d (and %% escapes), as the panel line count uses it.
std::vector<uint8_t> formatOneInt(std::span<const uint8_t> format, int value);

}  // namespace tmport::gg
