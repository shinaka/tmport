#pragma once
// The puzzle framework of ovl18 (38e2), the drawing half (re/gg/docs/code_puzzles_a.md §1.4, §2):
// the eight kinds and their screens, Puzzle_Dispatch's line under the menu bar, the common panel
// (Puzzle_InitPanel 38e2:0357) and the dispatcher the tools use to draw a kind and variant as the
// puzzle shows it at its first input poll. The choice of kind and variant (rand), the history, the
// door, the reward and the input loops are wave 3's.
//
// Every puzzle draws its picture at (0, 10) after Screen_LoadPicture(picture, palette) (each has a
// full 0..255 palette), the panel 0x1091 at (0, 165) with its text, saves the screen into the back
// page and fades in (code_puzzles_a.md §2.1, §2.3, code_puzzles_b.md §1.2).

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "gg/save/players_dat.h"  // kPuzzleKinds (the record's solved-variant table is per kind)

namespace tmport::gg {

class Archive;
struct CursorRecord;
struct CursorRegion;
class Fonts;
class GgExe;
class Screen;
class Video;

enum class PuzzleKind : int {
    Electric = 0,  // ovl13 3083
    Simple = 1,    // ovl20 3938
    Pieces = 2,    // ovl21 3a4a
    Gears = 3,     // ovl22 3b2f
    Balance = 4,   // ovl14 32fe
    Launch = 5,    // ovl17 3797
    Energy = 6,    // ovl15 33cd
    Magnets = 7,   // ovl16 35bb
};
static_assert(kPuzzleKinds == 8);

// Puzzle_Dispatch's table (38e2:00d4, CS:018e) as the overlays use it: the screen picture and its
// palette, and the effect bank (code_puzzles_a.md §1.4).
struct PuzzleKindInfo {
    uint16_t picture;
    uint16_t palette;
    uint16_t effectBank;
    const char* name;
};
const PuzzleKindInfo& puzzleKindInfo(int kind);
// g_puzzleVariants DS:3334 of the user's ssg.exe (30, 20, 20, 31, 32, 30, 28, 30 in v1.0).
int puzzleVariantCount(const GgExe& exe, int kind);

// The common resources (code_puzzles_a.md §2.5).
inline constexpr uint16_t kPanelPicture = 0x1091;     // 320 x 35
inline constexpr uint16_t kGoBackPressed = 0x1092;    // 65 x 19
inline constexpr uint16_t kHintPressed = 0x1095;      // 36 x 15
inline constexpr uint16_t kCheckPressed = 0x1096;     // 36 x 13
inline constexpr uint16_t kSolvedMessage = 0x2000;
inline constexpr uint16_t kPuzzleDigits = 0x204F;     // bundle of 10 digit pictures
inline constexpr uint16_t kPuzzleCursor = 0x0003;     // the 15 x 17 hand

// 38e2:00d4 Puzzle_Dispatch, before the jump: g_drawColor = 0, Gfx_HLine(0, 9, 320).
void puzzleDispatchLine(Screen& screen);

// 38e2:0357 Puzzle_InitPanel(text): the panel (DS:b134) and the pressed Go Back (DS:b130) loaded,
// the Go Back zone pushed (the runtime's), font 1, colour 0, the panel keyed at (0, 165) and
// Text_DrawBox(4, 170, 208, text). No flush.
class PuzzlePanel {
public:
    void init(Screen& screen, Fonts& fonts, const Archive& archive, std::span<const uint8_t> text);
    std::span<const uint8_t> panel() const { return panel_; }
    std::span<const uint8_t> goBackPressed() const { return goBack_; }

private:
    std::vector<uint8_t> panel_;
    std::vector<uint8_t> goBack_;
};

// The text of a 'T' resource (bytes before the NUL).
std::vector<uint8_t> puzzleText(const Archive& archive, uint16_t id);

// Screen_LoadPicture(picture, palette) (1473:0399) and Lpx_Draw(0, 10, picture): a puzzle's screen.
void drawPuzzlePicture(Video& video, const Archive& archive, uint16_t picture, uint16_t palette);
// 1473:0325 Screen_FadeOutIn as a tool sees it: Dirty_Flush, then the fade-in that ends on the shadow
// palette (the runtime runs the fade's loads on the retraces).
void puzzleFadeIn(Video& video);
// 17ab:0083 Mouse_LoadCursors(id, 0): a cursor record of a picture resource.
std::unique_ptr<CursorRecord> loadCursor(const Archive& archive, uint16_t id);
// Tables that live in an overlay's code as straight-line stores (code_puzzles_a.md §4.2, §9): runs the
// code of overlay segment `seg` from `start` until `end`, accepting only `MOV word [imm16], imm16` (C7 06,
// modrm 0x06) or `MOV word [SI + disp16], imm16` (C7 84, modrm 0x84), as `modrm` says, and JMP rel16 /
// rel8 (E9, EB). Returns the (address or displacement, value) pairs in execution order. Throws
// tmport::FormatError on any other byte, a jump out of the overlay or a run without end.
std::vector<std::pair<uint16_t, uint16_t>> runStoreCode(const GgExe& exe, uint16_t seg, uint16_t start, uint16_t end,
                                                        uint8_t modrm);
// The 16-bit word of a jump table in overlay code (CS:off + 2 i).
uint16_t codeWord(const GgExe& exe, uint16_t seg, uint16_t off);
// 35bb:0000 PzMagnets_RectsOverlap / 3083:19a9 PzElec_StripsOverlap (the same routine in two overlays):
// per axis, the narrower span's start inside [a, a + wa) or its end inside (a, a + wa] of the wider
// one; both axes must hit. Signed, so negative sizes take part as they are.
bool spansOverlap(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2);

// A 0x1c-byte cursor region of ssg.exe's DGROUP (its zone at +8) with the given cursor.
CursorRegion cursorRegionAt(const GgExe& exe, uint16_t regionOff, CursorRecord* record);
// A 22-byte click zone of DGROUP as a cursor region's shape (shape, x, y, w, h, priority).
CursorRegion zoneAt(const GgExe& exe, uint16_t zoneOff, CursorRecord* record);

// Values a tool takes from a save state for the screen's animations (e.g. "glow" for the launch
// target, "device0".."device3" for the energy forms' idle frames).
using PuzzleSceneValues = std::map<std::string, int>;

// One puzzle's screen: the eight kinds implement it (pz_*.h).
class Puzzle {
public:
    virtual ~Puzzle() = default;
    // The puzzle's start up to its input loop (Pz..._Run): its resources and variant loaded, the
    // screen drawn, the cursor set, the screen faded in (in a tool: the shadow palette is final).
    virtual void setup(Video& video, Fonts& fonts, int variant) = 0;
    // Shows the animation phases a save state holds (tools); the default has none.
    virtual void applyScene(Video& video, const PuzzleSceneValues& scene) {
        (void)video;
        (void)scene;
    }
    // A short description for the tools' output.
    virtual std::string describe() const = 0;
};

// The puzzle object of a kind. Throws std::out_of_range for a kind outside 0..7.
std::unique_ptr<Puzzle> makePuzzle(int kind, const GgExe& exe, const Archive& archive);

// Draws kind `kind`, variant `variant` as the puzzle shows it when its input loop starts (after the
// fade-in and the variant's own drawing), with the black line of Puzzle_Dispatch. Returns the puzzle,
// which owns the cursor records the screen shows (keep it while the screen is used). Throws
// std::out_of_range for a kind or variant outside the user's tables.
std::unique_ptr<Puzzle> drawPuzzleScreen(Video& video, const GgExe& exe, const Archive& archive, Fonts& fonts,
                                         int kind, int variant, const PuzzleSceneValues& scene = {});

}  // namespace tmport::gg
