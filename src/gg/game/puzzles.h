#pragma once
// The puzzle framework of ovl18 (38e2) (re/gg/docs/code_puzzles_a.md §1, §2): the eight kinds and their
// screens, Puzzle_Dispatch's line under the menu bar, the common panel (Puzzle_InitPanel 38e2:0357) and
// the dispatcher the tools use to draw a kind and variant as the puzzle shows it at its first input
// poll; in wave 3 (game/puzzle_run.cpp, "wave 3: the run" below) state 9 on the game: the choice of
// kind and variant (rand), the history, the dispatch to the eight Pz*::run, the success, the reward and
// the door, and the restore of a running puzzle from a DOSBox-X state.
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

#include "gg/app/seq_run.h"
#include "gg/game/dam.h"
#include "gg/input/zones.h"
#include "gg/save/players_dat.h"  // kPuzzleKinds (the record's solved-variant table is per kind)

namespace tmport::gg {

class Archive;
class GgGame;
class GgStateFile;
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
    // The pictures loaded without drawing (a puzzle resumed from a save state).
    void load(const Archive& archive);
    // 38e2:03ed's Mem_Free of the two pictures.
    void free();

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

    // ---- wave 3: starting from a DOSBox-X state (puzzlesRestoreFromState, game/load_state.h)
    // Rebuilds what the puzzle keeps outside DGROUP at a moment of its main loop (resources, pictures,
    // the click zones and cursor regions in the state's order, the cursor), from GgState (already
    // loaded) and the state's RAM. False with a reason when the moment is not supported. The puzzle is
    // bound to the game's state then. Default: not supported (each kind overrides it).
    virtual bool restoreFromState(GgGame& g, const GgStateFile& s, std::string& why);
    // The main loop from that moment on, then the puzzle's exit (what Pz*_Run returns: 1 solved).
    virtual int resume(GgGame& g);
};

// The puzzle object of a kind. Throws std::out_of_range for a kind outside 0..7.
std::unique_ptr<Puzzle> makePuzzle(int kind, const GgExe& exe, const Archive& archive);

// Draws kind `kind`, variant `variant` as the puzzle shows it when its input loop starts (after the
// fade-in and the variant's own drawing), with the black line of Puzzle_Dispatch. Returns the puzzle,
// which owns the cursor records the screen shows (keep it while the screen is used). Throws
// std::out_of_range for a kind or variant outside the user's tables.
std::unique_ptr<Puzzle> drawPuzzleScreen(Video& video, const GgExe& exe, const Archive& archive, Fonts& fonts,
                                         int kind, int variant, const PuzzleSceneValues& scene = {});


// ---- wave 3: the run (owner: puzzles_a agent; game/puzzle_run.cpp) ----------------------------------
// State 9 and the framework on the game (code_puzzles_a.md §1, §2): the choice (rand), the history in
// the player record (puzzleSolved), the dispatch to the eight Pz*::run, the common panel, the hint
// viewer (TextView_Run of the ui module), the success and the reward, the door (world's
// roomOpenPuzzleDoor). Each puzzle's globals are GgState::puzzles.<kind>; the running puzzle object is
// PuzzlesObjects'.

// The puzzles' engine-side objects (GgGame::puzzlesObjects): the running puzzle, the panel's pictures
// and zones, the DAM engine of the launch puzzle.
struct PuzzlesObjects {
    std::unique_ptr<Puzzle> running;  // the kind's Pz* object while state 9 runs
    PuzzlePanel panel;                // DS:b134 / DS:b130 (Puzzle_InitPanel)
    Zone goBackZone;                  // DS:3306 g_pzGoBackZone: (249,168) 65x19, press 0x5dd
    Zone panelZone;                   // DS:331c g_pzPanelZone: (2,168) 212x32, priority 100, release 0x5de
    DamPlayer dam;                    // ovl37 3d39 (the cels, the frame table DS:ba26, the channels' scripts)
    // Set by puzzlesRestoreFromState: state09Puzzle(Resume) goes on with the running puzzle's loop.
    bool resumePending = false;
};

// The common calls of the puzzles on the game (each is a step the original repeats in every overlay).
// Input_StripHeld(Input_GetCode()) 198d:0328 / 198d:0106: the puzzles' only way of reading input.
uint16_t puzzleGetCode(GgGame& g);
// The busy-wait of PzSnd_WaitEnd (the facade's pzIsActive is its Snd_IsActive test): while effect i
// plays, one idle turn.
void pzSndWaitEnd(GgGame& g, int i);
// `while (g_mouseButtons DS:0a0c != 0)`: the release waits of the pressed buttons (one idle turn each).
void puzzleWaitRelease(GgGame& g);
// Screen_LoadPicture(picture, palette) 1473:0399 (the palette's DAC load runs unless faded) and
// Lpx_Draw(0, 10, picture) 1399:0335: a puzzle's screen picture on the game.
void puzzleLoadScreen(GgGame& g, uint16_t picture, uint16_t palette);
// A 22-byte zone record of ssg.exe's DGROUP (its initial value).
Zone zoneFromDs(const GgExe& exe, uint16_t off);
// The active cursor copy DS:7a6a. Some puzzles write its fields directly instead of selecting a
// record: the electric plug picture (DS:7a78, 3083:1a94) and the save-under position after clamping
// the pointer (DS:7a6c / 7a6e, 3083:0d1a, 33cd:022a) (Cursor::activeCopy).
CursorRecord& activeCursorCopy(Video& video);
// The INT 33h ranges a puzzle sets itself (fn 7: x min / max in driver units, i.e. doubled; fn 8),
// after clamping g_mouseX / g_mouseY (upper bounds only), then the save-under position = the pointer:
// PzElec_SetMouseRange 3083:0d1a and PzEnergy_SetMouseRange 33cd:022a share this tail.
void puzzleSetMouseRange(GgGame& g, int minX, int minY, int maxX, int maxY);

// Unwinding guard of a puzzle run: when an exception leaves the puzzle (the run limit, exit(0) through
// Save & Exit, a test's stop), the local zones and the cursor regions are cleared and the arrow is
// selected, so nothing keeps a pointer into the puzzle's objects (they are freed with it).
class PuzzleUnwindGuard {
public:
    explicit PuzzleUnwindGuard(GgGame& g);
    ~PuzzleUnwindGuard();
    PuzzleUnwindGuard(const PuzzleUnwindGuard&) = delete;
    PuzzleUnwindGuard& operator=(const PuzzleUnwindGuard&) = delete;

private:
    GgGame& g_;
    int exceptions_;
};

// Reading a DOSBox-X state's lists (for the puzzles' restore): the DS offsets of the zones of the local
// list g_zonesLocal DS:0a32 (head first, +0x0c far next) and of the cursor regions of the list DS:0a8c
// (front first; a list head {far first, far last}, items linked at +0), each ending at the first item
// outside DGROUP. A walk that does not end (60 items) returns what it has.
std::vector<uint16_t> stateLocalZones(const GgStateFile& s);
std::vector<uint16_t> stateCursorRegions(const GgStateFile& s);
// The far pointer stored at DS:off of the state equals the one at DS:other (both non-NULL).
bool stateSamePointer(const GgStateFile& s, uint16_t off, uint16_t other);
// The default cursor region's record of a restored puzzle (Mouse_SetCursorPicture's store). When the
// state shows the arrow (DS:7a4a) with Mouse_ForceDefault off, the arrow a dialog forced stays on the
// screen until the next Dirty_Flush draws the cursor the regions give (the New Player Hint's box before
// the electric and simple machines loops, which flush only on input): nothing is drawn then; otherwise
// Mouse_SetCursorPicture, whose flush draws the record the regions give.
void restoreCursorPicture(GgGame& g, const GgStateFile& s, CursorRecord* rec);
// Where a puzzle's DOSBox-X state stopped: its main loop (the poll, the idle animation, the dispatch of
// a code) or a loop nested in it (the rubber band of a wire, the plunger held, a DAM transition, the hint
// viewer, the success sequence, ...), told by the offsets in the overlay that the live stack holds (the
// return addresses of its calls; the innermost known one decides) or, without one, by the CPU's IP in
// the overlay's main loop. Only the main loop's moments can be resumed (Puzzle::resume).
struct PuzzleLoopMarkers {
    std::span<const uint16_t> mainReturns;  // the returns of the main loop's polls
    std::span<const uint16_t> subReturns;   // the returns of the calls that loop or wait in the dispatch
    uint16_t loopFirst = 0, loopLast = 0;   // the main loop's code (IP range in the overlay)
};
bool stateAtMainLoop(const GgStateFile& s, const PuzzleLoopMarkers& m, std::string& why);
// Links the zones of a restored puzzle as the state's local list has them: every DS offset of
// `order` (stateLocalZones) must have a Zone in `zones`; each is loaded from the state's record and
// the list is rebuilt in the same order. The panel zones (DS:3306, DS:331c) are the framework's and
// always known. False (with `why`) for a zone the puzzle does not know.
bool relinkStateZones(GgGame& g, const GgStateFile& s, const std::vector<std::pair<uint16_t, Zone*>>& zones,
                      std::string& why);

// 38e2:0000 State_09_Puzzle: Pal_Save, Zones_Push, Menu_SetMenuBar(DS:4053), g_puzzleLeave = 0, the
// choice of kind and variant (rand: §1.3), Puzzle_Dispatch, Room_OpenPuzzleDoor(g_puzzleResult),
// Pal_FadeOut, Zones_Pop, Menu_SetMenuBar(NULL), Pal_Restore; 5, or 16 with g_leaveRequest 3 / 2 for a
// menu exit (g_puzzleLeave). Resume: the running puzzle's loop goes on.
int16_t state09Puzzle(GgGame& g, StateEntry entry = StateEntry::Normal);
// 38e2:00d4 Puzzle_Dispatch: the black line under the bar, g_puzzleResult = Pz*::run(variant) of the
// kind, the solved flag written when it was 0.
void puzzleDispatch(GgGame& g);
// 38e2:019e Puzzle_FewUnsolvedLeft: at most 4 variants unsolved over all 8 kinds.
bool puzzleFewUnsolvedLeft(GgGame& g);
// 38e2:01e8 Puzzle_RandomType: R(8) (rand).
int puzzleRandomType(GgGame& g);
// 38e2:0202 Puzzle_PickUnsolved(kind): g_puzzleCandidates = the first 3 unsolved variants, or one
// random variant (R(n): rand) when few are left.
void puzzlePickUnsolved(GgGame& g, int kind);
// 38e2:0286 Puzzle_ShowHintText(text): TextView_Run(text) (the ui module's textViewRun).
void puzzleShowHintText(GgGame& g, std::span<const uint8_t> text);
// 38e2:0299 Puzzle_ShowSolved(jingle): PzSnd_Play(jingle + R(4), 4) (rand), the panel zone DS:331c,
// the solved message 0x2000 in the panel, flush.
void puzzleShowSolved(GgGame& g, int jingle);
// 38e2:031a Puzzle_PressGoBack: the pressed Go Back picture until the button is released.
void puzzlePressGoBack(GgGame& g);
// 38e2:0357 Puzzle_InitPanel(text): PuzzlePanel::init with the Go Back zone DS:3306 pushed.
void puzzleInitPanel(GgGame& g, std::span<const uint8_t> text);
// 38e2:03bf Puzzle_AwardPoints(sound): 4 times PzSnd_Play(sound, 5), Hud_AddScoreNow(100), PzSnd_WaitEnd.
void puzzleAwardPoints(GgGame& g, int sound);
// 38e2:03ed Puzzle_FreePanel: font 0, the panel pictures freed.
void puzzleFreePanel(GgGame& g);
// The puzzles' part of starting from a DOSBox-X state (game/load_state.h): the running puzzle's object
// rebuilt from GgState::puzzles and the state's heap copies. False with a reason when unsupported.
bool puzzlesRestoreFromState(GgGame& g, const GgStateFile& s, std::string& why);

}  // namespace tmport::gg
