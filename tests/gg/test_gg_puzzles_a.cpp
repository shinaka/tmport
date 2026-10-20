// Wave 3, puzzles part A (re/gg/docs/code_puzzles_a.md): state 9's choice of kind and variant with its
// rand() calls, the electric circuits, simple machines, launch and energy forms puzzles on the game, the
// DAM engine and the framework (panel, hint, success, reward, door), checked against the DOSBox-X states,
// shots and the launch's OPL capture of the reference set (REFERENCE_CAPTURES §9.1): each puzzle from its
// door state to its first screen, and the recorded solutions of tools/gg/capture_r2.py replayed as
// scripted mouse input in PIT ticks to the solved screen and the solved state's DGROUP. The UI, world and
// flow functions the puzzles call are other areas'; where a check needs them (the hint box, the text
// viewer, the score drawn by the HUD) it adapts to what they do. Only counts, ticks, coordinates, flags
// and differing-pixel counts here.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "gg/audio/dro_compare.h"
#include "gg/audio/soundtest.h"
#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/data/resources.h"
#include "gg/game/dam.h"
#include "gg/game/game.h"
#include "gg/game/puzzles.h"
#include "gg/game/pz_electric.h"
#include "gg/game/pz_energy.h"
#include "gg/game/pz_launch.h"
#include "gg/game/pz_simple.h"
#include "gg/game/rooms.h"
#include "gg/game/world.h"
#include "gg/save/gg_state_file.h"
#include "gg_game_fixture.h"
#include "gg_test_data.h"
#include "mods/png_codec.h"
#include "rtl/borland_rand.h"
#include "util/file_util.h"
#include "util/png_writer.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

constexpr double kTicksPerMs = 3579545.0 / 3.0 / 8192.0 / 1000.0;  // 0.145652 PIT ticks per ms

std::optional<std::filesystem::path> statePath(const std::string& name) {
    return testdata::ggCapture("savestates/r2/puzzle/" + name + ".sav");
}
std::optional<std::filesystem::path> shotPath(const std::string& name) { return testdata::ggCapture("r2/puzzle/" + name + ".png"); }

uint32_t advance(uint32_t seed, uint64_t n) {
    BorlandRand r;
    r.setRawSeed(seed);
    for (uint64_t i = 0; i < n; ++i) r.rand();
    return r.seed();
}

// A DOSBox-X state into the fixture's game; the state file stays open in `s`.
void loadState(gggame::GameFixture& f, const std::string& name, std::optional<GgStateFile>& s) {
    const auto path = statePath(name);
    REQUIRE(path);
    REQUIRE(f.loadState(*path));
    s.emplace(GgStateFile::load(*path));
}

// The state's moment inside the running puzzle (puzzlesRestoreFromState), with the HUD's digit copy in
// the colours the state's DS:1318 names (Hud_SetDigitColors recolours it in place: the world's restore
// does the same) for the score Hud_AddScoreNow draws into the panel.
void loadPuzzleState(gggame::GameFixture& f, const std::string& name, std::optional<GgStateFile>& s) {
    loadState(f, name, s);
    f.g().hud.syncDigitColors();
    std::string why;
    const bool ok = puzzlesRestoreFromState(f.g(), *s, why);
    REQUIRE_MESSAGE(ok, why);
}

// capture_r2.py's MOUSEKEY tokens ("click X Y", "move X Y", "wait MS", "down [X Y]", "up [X Y]",
// "drag X1 Y1 X2 Y2 MS") as scripted input in PIT ticks after `t` (updated to the end).
std::string r2Mouse(std::string_view tokens, double& t) {
    std::istringstream in{std::string(tokens)};
    std::string op, out;
    auto emit = [&](const std::string& e) {
        if (!out.empty()) out += ", ";
        out += std::format("{}:{}", static_cast<long long>(std::floor(t)), e);
    };
    while (in >> op) {
        if (op == "wait") {
            double ms = 0;
            in >> ms;
            t += ms * kTicksPerMs;
        } else if (op == "move" || op == "click") {
            int x = 0, y = 0;
            in >> x >> y;
            emit(std::format("{}:{}:{}", op, x, y));
            if (op == "click") t += 1;
        } else if (op == "down" || op == "up") {
            // optional coordinates
            const auto pos = in.tellg();
            int x = 0, y = 0;
            if (in >> x >> y) emit(std::format("move:{}:{}", x, y));
            else {
                in.clear();
                in.seekg(pos);
            }
            emit(op == "down" ? "buttons:1" : "buttons:0");
        } else if (op == "drag") {
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            double ms = 0;
            in >> x1 >> y1 >> x2 >> y2 >> ms;
            emit(std::format("move:{}:{}", x1, y1));
            emit("buttons:1");
            const double end = t + ms * kTicksPerMs;
            const int steps = std::max(1, static_cast<int>((end - t) / 2));
            const double t0 = t;
            for (int i = 1; i <= steps; ++i) {
                t = t0 + (end - t0) * i / steps;
                emit(std::format("move:{}:{}", x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps));
            }
            t += 1;
            emit("buttons:0");
        }
    }
    return out;
}

// Steps the running function until `interrupts` PIT interrupts have run since `base` (or it returned).
void runTo(gggame::GameFixture& f, uint64_t base, double ticks) {
    const auto target = base + static_cast<uint64_t>(std::ceil(ticks));
    if (f.done()) return;
    f.runUntil([&](const GgTraceRecord& r) { return r.interrupts >= target; }, 20'000'000);
}

// Modelled bytes of the named fields that differ between the game's DGROUP and a state's.
std::vector<std::string> fieldDifferences(const GgGame& g, const GgStateFile& s, std::initializer_list<std::string_view> names) {
    const auto mine = dumpToDgroup(g, s.dgroup());
    std::vector<std::string> out;
    for (const DgroupField& f : gameStateFields()) {
        if (std::find(names.begin(), names.end(), f.name) == names.end()) continue;
        for (size_t i = 0; i < f.size; ++i)
            if (f.modelled(i) && mine[f.ds + i] != s.dgroup()[f.ds + i]) {
                out.push_back(std::format("{}+{:#x}", f.name, i));
                break;
            }
    }
    return out;
}

std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

// The bounding box of the pixels that differ between the fixture's last picture and a capture (a
// message), and the picture written to $GG_PUZZLES_A_DUMP/<tag>.png when that variable is set
// (debugging only).
void explainDiff(gggame::GameFixture& f, const std::filesystem::path& shot, const std::string& tag) {
    const auto frame = f.frame();
    if (const char* dir = std::getenv("GG_PUZZLES_A_DUMP")) {
        std::vector<uint8_t> rgba;
        for (const uint32_t p : frame)
            for (int k = 0; k < 4; ++k) rgba.push_back(static_cast<uint8_t>(p >> (8 * k)));
        png::writeRgba(std::filesystem::path(dir) / (tag + ".png"), 320, 200, rgba);
    }
    const auto bytes = readWholeFile(shot);
    if (!bytes) return;
    const auto ref = mods::decodePng(*bytes);
    const int step = ref.width == 640 ? 2 : 1;
    int x0 = 320, y0 = 200, x1 = -1, y1 = -1, n = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            const uint32_t p = frame[static_cast<size_t>(y) * 320 + static_cast<size_t>(x)];
            const uint8_t* c = &ref.rgba[(static_cast<size_t>(y * step) * static_cast<size_t>(ref.width) + static_cast<size_t>(x * step)) * 4];
            if ((p & 0xFF) == c[0] && ((p >> 8) & 0xFF) == c[1] && ((p >> 16) & 0xFF) == c[2]) continue;
            ++n;
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
        }
    if (n) MESSAGE(std::format("{}: {} px differ in ({}, {}) .. ({}, {})", tag, n, x0, y0, x1, y1));
}

// The software cursor's box at the pointer (the puzzles' cursors are at most 40 x 30).
gggame::Mask cursorBox(int x, int y, int w = 40, int h = 32) { return gggame::Mask{x - 1, y - 1, x + w, y + h}; }

// The score digits of the panel (6 digits at (253, 188)): drawn by the HUD's Hud_AddScoreNow.
constexpr gggame::Mask kScoreDigits{253, 186, 319, 199};

// The number of RecordingSound calls named `op` with these arguments in the trace's sound strings.
int countSound(const std::vector<GgTraceRecord>& rows, const std::string& call) {
    int n = 0;
    for (const auto& r : rows)
        for (size_t at = r.sound.find(call); at != std::string::npos; at = r.sound.find(call, at + 1)) ++n;
    return n;
}

// The number of calls of the recording facade whose description starts with `call` ("pzFreeBank(").
int countCalls(const RecordingSound& sound, const std::string& call) {
    int n = 0;
    for (const auto& c : sound.calls())
        if (RecordingSound::describe({c}).starts_with(call)) ++n;
    return n;
}

// The tick (g_ticks) of the first record whose sound calls include `call` at or after `from`.
std::optional<uint32_t> soundTick(const std::vector<GgTraceRecord>& rows, const std::string& call, size_t from = 0) {
    for (size_t i = from; i < rows.size(); ++i)
        if (rows[i].sound.find(call) != std::string::npos) return rows[i].ticks;
    return std::nullopt;
}

// The target hole's glow (frames 1..7 are the same picture, frame 0 another) of launch variant 0.
gggame::Mask launchGlow(const GgExe& exe) {
    const int x = exe.dsS16(static_cast<uint16_t>(dg::kLaunchHoleX + 2 * 3)), y = exe.dsS16(static_cast<uint16_t>(dg::kLaunchHoleY + 2 * 3));
    return gggame::Mask{x - 1, y - 1, x + 40, y + 16};
}

// The recorded solutions of capture_r2.py (variant 0 of each kind, from the kK_screen states).
constexpr std::string_view kSolveK0a =
    "click 32 145 wait 500 move 136 125 wait 400 click 136 125 wait 400 move 160 100 wait 200 move 192 58 wait 500";
constexpr std::string_view kSolveK0b = "click 192 58";
constexpr std::string_view kSolveK1a =
    "click 24 88 wait 600 move 150 110 wait 300 move 178 120 wait 500 click 178 120 wait 700 move 120 60 wait 300";
constexpr std::string_view kSolveK1b = "click 28 156";
constexpr std::string_view kSolveK6a =
    "click 24 72 wait 600 move 181 36 wait 300 click 181 36 wait 500 move 219 103 wait 400 "
    "click 219 103 wait 600 move 181 70 wait 300 click 181 70 wait 500 move 219 36 wait 400 "
    "click 219 36 wait 600 move 181 104 wait 300 click 181 104 wait 500 move 219 70 wait 400 "
    "click 219 70 wait 600 move 120 140 wait 300";
constexpr std::string_view kSolveK6b = "click 26 137";

std::string k5Pull(int dx) {
    // capture_r2.py: the plunger pulled in 1-pixel steps 100 ms apart (dx 14 = energy 4)
    std::string s = "move 283 141 wait 300 down 283 141 wait 300";
    for (int i = 1; i <= dx; ++i) s += std::format(" move {} {} wait 100", 283 + i, 141 + static_cast<int>(std::lround(i * 8.0 / 14)));
    return s + " wait 500";
}

}  // namespace

// ---------------------------------------------------------------------------------------- the choice

TEST_CASE("puzzles_a: the choice of kind and variant from the door states (rand() calls)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    for (int kind = 0; kind < kPuzzleKinds; ++kind) {
        CAPTURE(kind);
        gggame::GameFixture f(*ggFiles);
        std::optional<GgStateFile> door, screen;
        loadState(f, std::format("k{}_door", kind), door);
        screen.emplace(GgStateFile::load(*statePath(std::format("k{}_screen", kind))));
        // 11 calls of the two chimps come before the choice (REFERENCE_CAPTURES §10.7): the actors'.
        for (int i = 0; i < 11; ++i) f.rand().rand();
        const uint64_t calls = f.rand().calls();
        f.polls(true);
        f.start([](GgGame& g) { state09Puzzle(g); });
        f.step(1);  // the first poll: the choice is made, the puzzle's set-up has run
        f.finish();
        CHECK(f.g().puzzles.kind == kind);
        CHECK(f.g().puzzles.variant == 0);
        CHECK(f.g().puzzles.candidates[0] == 0);
        CHECK(f.g().puzzles.candidates[1] == 1);
        CHECK(f.g().puzzles.candidates[2] == 2);
        CHECK(f.rand().calls() - calls >= 2);
        // The kind and the candidate index: 2 calls, the rand state of the puzzle's screen state.
        CHECK(advance(door->dsDword(dg::kRandState), 13) == screen->dsDword(dg::kRandState));
        if (kind == 0 || kind == 1 || kind == 5 || kind == 6) {
            CHECK(f.rand().calls() - calls == 2);  // part A's set-ups call rand() never
            CHECK(f.rand().seed() == screen->dsDword(dg::kRandState));
        }
    }
}

TEST_CASE("puzzles_a: the choice rules (first three unsolved, few left, a kind with none left)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    // First three unsolved in index order.
    puzzleSolved(g.player, 3, 0) = 1;
    puzzleSolved(g.player, 3, 2) = 1;
    const uint64_t c0 = f.rand().calls();
    puzzlePickUnsolved(g, 3);
    CHECK(g.puzzles.candidates[0] == 1);
    CHECK(g.puzzles.candidates[1] == 3);
    CHECK(g.puzzles.candidates[2] == 4);
    CHECK(f.rand().calls() == c0);
    CHECK_FALSE(puzzleFewUnsolvedLeft(g));
    // At most 4 left over the whole game: any variant of the kind (one rand() call).
    for (int k = 0; k < kPuzzleKinds; ++k)
        for (int v = 0; v < g.tables.puzzleVariants[static_cast<size_t>(k)]; ++v) puzzleSolved(g.player, k, v) = 1;
    puzzleSolved(g.player, 6, 27) = 0;
    puzzleSolved(g.player, 0, 5) = 0;
    puzzleSolved(g.player, 1, 7) = 0;
    puzzleSolved(g.player, 2, 9) = 0;
    CHECK(puzzleFewUnsolvedLeft(g));
    puzzleSolved(g.player, 2, 10) = 0;
    CHECK_FALSE(puzzleFewUnsolvedLeft(g));  // 5 left
    puzzleSolved(g.player, 2, 10) = 1;
    BorlandRand expect;
    expect.setRawSeed(f.rand().seed());
    puzzlePickUnsolved(g, 6);
    CHECK(g.puzzles.candidates[0] == expect.random(28));
    CHECK(g.puzzles.candidates[1] == -1);
    CHECK(g.puzzles.candidates[2] == -1);
    CHECK(f.rand().calls() == c0 + 1);
    // A kind without an unsolved variant gives no candidate (state 9 then tries another kind).
    for (int k = 0; k < kPuzzleKinds; ++k)
        for (int v = 0; v < g.tables.puzzleVariants[static_cast<size_t>(k)]; ++v) puzzleSolved(g.player, k, v) = 0;
    for (int v = 0; v < 20; ++v) puzzleSolved(g.player, 1, v) = 1;
    puzzlePickUnsolved(g, 1);
    CHECK(g.puzzles.candidates[0] == -1);
}

// ------------------------------------------------------------------------- the screens from the doors

TEST_CASE("puzzles_a: each part-A puzzle from its door state to its first screen") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    for (const int kind : {0, 1, 5, 6}) {
        CAPTURE(kind);
        gggame::GameFixture f(*ggFiles);
        std::optional<GgStateFile> door;
        loadState(f, std::format("k{}_door", kind), door);
        for (int i = 0; i < 11; ++i) f.rand().rand();
        f.g().display.fadeOut();  // State_08's Pal_FadeOut
        f.sound().clear();
        f.polls(true);
        f.start([](GgGame& g) { state09Puzzle(g); });
        // The first input poll: the New Player Hint's box (when the UI shows it) or the puzzle's loop.
        auto rows = f.runUntil([](const GgTraceRecord& r) { return r.yield == YieldPoint::GetInputCode; }, 200000);
        REQUIRE(!rows.empty());
        const bool box = countSound(f.trace(), "loadSong(4182,") != 0;  // the hint chime 0x1056
        if (box) {
            // The New Player Hint 0x1052 (Ada's first puzzle) over the puzzle's screen, then Enter: the
            // first screen. In the electric and simple machines loops nothing flushes before input, so
            // the arrow the box forced stays on the screen (the shots show it); launch and energy redraw
            // on their timers and show their own cursors.
            const int hint = f.differingPixels(*shotPath(std::format("k{}_00_hint", kind)));
            if (hint != 0) explainDiff(f, *shotPath(std::format("k{}_00_hint", kind)), std::format("door_hint_k{}", kind));
            CHECK(hint == 0);
            CHECK(hintSeen(f.g().player, 0x1052) == 0);  // set when the box is closed
            REQUIRE(f.keys("2:tap:enter"));
            const uint64_t base = f.runtime().clock().interrupts();
            runTo(f, base, 2 * 145.652);
            // The launch's glow and the energy devices' idle frames are in another phase than in the shot.
            std::vector<gggame::Mask> masks;
            if (kind == 5) masks.push_back(launchGlow(ggFiles->exe()));
            if (kind == 6)
                for (int d = 0; d < 4; ++d) {
                    const int x = ggFiles->exe().dsS16(dg::kEnergyLayout), y = ggFiles->exe().dsS16(static_cast<uint16_t>(dg::kEnergyLayout + 2 + 2 * d));
                    masks.push_back(gggame::Mask{x, y, x + 87, y + 33});
                }
            const int screen = f.differingPixels(*shotPath(std::format("k{}_01_screen", kind)), masks);
            if (screen != 0) explainDiff(f, *shotPath(std::format("k{}_01_screen", kind)), std::format("door_k{}", kind));
            MESSAGE(std::format("kind {}: the hint box {} px, the first screen {} px", kind, hint, screen));
            CHECK(screen == 0);
        } else {
            // Without the UI's box the cursor is the puzzle's, not the arrow the shots show: left out.
            const std::vector<gggame::Mask> masks{cursorBox(door->cursor().mouseX, door->cursor().mouseY)};
            const int screen = f.differingPixels(*shotPath(std::format("k{}_01_screen", kind)), masks);
            MESSAGE(std::format("kind {}: no New Player Hint box (the UI's Hint_Show); {} px from the screen shot", kind, screen));
            CHECK(screen == 0);
        }
        // The puzzle's effect bank is loaded once.
        const uint16_t banks[8] = {0x11CC, 0x11CA, 0, 0, 0, 0x11CE, 0x11D2, 0};
        CHECK(countSound(f.trace(), std::format("pzLoadBank({})", banks[kind])) == 1);
        f.finish();
    }
}

// ------------------------------------------------------------------------------- the solutions replayed

namespace {

// A replay of a part-A puzzle from its kK_screen state: the first script, a shot, the second script
// (the solving click), the solved shot `sleep` seconds later, then the solved state's DGROUP.
struct SolveCase {
    int kind;
    std::string firstShot, solvedShot, solvedState;
    std::string first, second;
    double sleepAfter;
    std::vector<std::string_view> fields;  // the puzzle's GgState fields to compare with the solved state
    std::vector<gggame::Mask> screenMasks = {};
    std::vector<gggame::Mask> firstMasks = {};
};


void replaySolve(const SolveCase& c) {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> screen;
    loadPuzzleState(f, std::format("k{}_screen", c.kind), screen);
    f.present();
    explainDiff(f, *shotPath(std::format("k{}_01_screen", c.kind)), std::format("resume_k{}", c.kind));
    // The shot was taken a second after the state: the launch's glowing hole may show another frame.
    CHECK(f.differingPixels(*shotPath(std::format("k{}_01_screen", c.kind)), c.screenMasks) == 0);
    const uint32_t points = f.g().player.points;
    const uint64_t calls = f.rand().calls();
    f.sound().clear();
    f.polls(true);
    const uint64_t base = f.runtime().clock().interrupts();
    double t = 20;
    const std::string s1 = r2Mouse(c.first, t);
    REQUIRE(f.mouse(s1));
    f.start([](GgGame& g) { state09Puzzle(g, StateEntry::Resume); });
    runTo(f, base, t);
    const int first = f.differingPixels(*shotPath(c.firstShot), c.firstMasks);
    MESSAGE(std::format("kind {}: {} px from {}", c.kind, first, c.firstShot));
    explainDiff(f, *shotPath(c.firstShot), c.firstShot);
    CHECK(first == 0);
    // The solving input, then the solved screen after the capture's sleep.
    const double t2 = t + 5;
    double tt = 5;
    const std::string s2 = r2Mouse(c.second, tt);
    REQUIRE(f.mouse(s2));
    runTo(f, base, t2 + c.sleepAfter * 1000 * kTicksPerMs);
    REQUIRE_FALSE(f.done());
    std::optional<GgStateFile> solved;
    solved.emplace(GgStateFile::load(*statePath(c.solvedState)));
    // The reward: 4 x 100 points through Hud_AddScoreNow (the world's), drawn in the panel's score box.
    const bool scored = f.g().player.points == points + 400;
    std::vector<gggame::Mask> masks;
    if (!scored) masks.push_back(kScoreDigits);
    const int px = f.differingPixels(*shotPath(c.solvedShot), masks);
    MESSAGE(std::format("kind {}: {} px from {} (score {})", c.kind, px, c.solvedShot, f.g().player.points));
    explainDiff(f, *shotPath(c.solvedShot), c.solvedShot);
    CHECK(px == 0);
    if (scored) CHECK(f.g().player.points == solved->dsDword(dg::kPlayer + 0x10));
    else MESSAGE("Hud_AddScoreNow did not add the points (the world's stub): the score is not checked");
    // The jingle is the only rand() call of the puzzle: the solved state's rand state.
    CHECK(f.rand().calls() - calls == 1);
    CHECK(f.rand().seed() == solved->dsDword(dg::kRandState));
    CHECK(countSound(f.trace(), "pzPlay(") >= 5);
    f.finish();
    const auto mine = dumpToDgroup(f.g(), solved->dgroup());
    std::vector<std::string> diff;
    for (const DgroupField& fd : gameStateFields()) {
        if (std::find(c.fields.begin(), c.fields.end(), fd.name) == c.fields.end()) continue;
        for (size_t i = 0; i < fd.size; ++i)
            if (fd.modelled(i) && mine[fd.ds + i] != solved->dgroup()[fd.ds + i]) {
                diff.push_back(std::format("{}+{:#x}", fd.name, i));
                break;
            }
    }
    CHECK_MESSAGE(diff.empty(), joined(diff));
}

}  // namespace

TEST_CASE("puzzles_a: electric circuits variant 0 solved by the recorded input") {
    replaySolve({0,
                 "k0_03_wire_strung",
                 "k0_04_solved",
                 "k0_solved",
                 std::string(kSolveK0a),
                 std::string(kSolveK0b),
                 3.0,
                 {"g_elecPieces", "g_elecTerminals", "g_elecToolCounts", "g_elecMode", "g_elecOnPath",
                  "g_elecBatteryShorted", "g_elecPaths", "g_elecPathCount", "g_elecFoundPiece", "g_elecFoundTerm",
                  "g_elecPlugShown", "DS_a8ba_elecWireAnchorY", "DS_a8bc_elecWireAnchorX", "g_puzzleKind",
                  "g_puzzleVariant", "g_puzzleResult", "g_puzzleGoThrough", "g_puzzleLeave"},
                 {},
                 // The plug's picture replaces the spool's in the active copy (DS:7a78) and shows at the
                 // next flush; in the capture the pointer's move came after PzElec_UpdatePlugCursor had
                 // read the old position, so the redraw's flush still drew the spool. The engine delivers
                 // the move in the loop's poll, before that read: the cursor box is left out.
                 {cursorBox(192, 58)}});
}

TEST_CASE("puzzles_a: simple machines variant 0 solved by the recorded input") {
    replaySolve({1,
                 "k1_02_wedge_placed",
                 "k1_03_solved",
                 "k1_solved",
                 std::string(kSolveK1a),
                 std::string(kSolveK1b),
                 3.0,
                 {"g_simpleHeld", "g_simplePlaced", "g_puzzleKind", "g_puzzleVariant", "g_puzzleResult", "g_puzzleGoThrough"}});
}

TEST_CASE("puzzles_a: energy forms variant 0 solved by the recorded input") {
    replaySolve({6,
                 "k6_03_matched",
                 "k6_04_solved",
                 "k6_solved",
                 std::string(kSolveK6a),
                 std::string(kSolveK6b),
                 10.0,
                 {"g_energyDevices", "g_energyForms", "g_energyIdleFrame", "g_energyMode", "g_energyLinesLeft",
                  "g_puzzleKind", "g_puzzleVariant", "g_puzzleResult"}});
}

TEST_CASE("puzzles_a: launch variant 0 solved by the recorded input") {
    GG_REQUIRE_DATA();
    replaySolve({5,
                 "k5_02_energy4_held",
                 "k5_03_solved",
                 "k5_solved",
                 "drag 37 97 228 102 600 wait 1000 " + k5Pull(14),
                 "up",
                 5.0,
                 {"g_launchGlowOn", "g_launchObjectShown", "DS_af92_launchClipBottom", "DS_af94_launchClipTop",
                  "DS_af96_launchClipRight", "DS_af98_launchClipLeft", "DS_af9c_launchObjectY", "DS_af9e_launchObjectX",
                  "g_launchObject", "g_launchSurface", "g_launchLanding", "g_launchGravity", "g_launchRamp", "g_launchTarget",
                  "g_launchGravityPic", "g_launchEnergy", "g_puzzleKind", "g_puzzleVariant", "g_puzzleResult"},
                 {launchGlow(ggFiles->exe())}});
}

// --------------------------------------------------------------------------------- the other moments

TEST_CASE("puzzles_a: the electric Hint button and the hint viewer") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> screen;
    loadPuzzleState(f, "k0_screen", screen);
    f.sound().clear();
    f.polls(true);
    const uint64_t base = f.runtime().clock().interrupts();
    REQUIRE(f.mouse("20:click:44:20"));
    f.start([](GgGame& g) { state09Puzzle(g, StateEntry::Resume); });
    runTo(f, base, 20 + 3 * 145.652);
    // The pressed Hint picture, effect 0xb at priority 5, then TextView_Run (the UI's) on the text
    // 0x20dd + 0.
    CHECK(countSound(f.trace(), "pzPlay(11,5)") == 1);
    const int px = f.differingPixels(*shotPath("k0_02_hint_text"));
    explainDiff(f, *shotPath("k0_02_hint_text"), "k0_02_hint_text");
    // Outside the viewer's area (0, 28) 129 x 138 and the HINT button (its 36th column stays: keep) the
    // screen is the puzzle's.
    const std::vector<gggame::Mask> viewer{{0, 28, 128, 165}, {20, 10, 70, 32}};
    CHECK(f.differingPixels(*shotPath("k0_02_hint_text"), viewer) == 0);
    MESSAGE(std::format("the hint viewer's screen: {} px from k0_02_hint_text", px));
    CHECK(px == 0);  // the UI's TextView_Run over the puzzle (its arrow cursor at the button)
    f.finish();
}

TEST_CASE("puzzles_a: Go Back leaves the electric puzzle unsolved") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> screen;
    loadPuzzleState(f, "k0_screen", screen);
    GgGame& g = f.g();
    g.puzzles.goThrough = 1;  // stale from an earlier puzzle: Go Back writes 0
    f.sound().clear();
    f.polls(true);
    REQUIRE(f.mouse("20:click:280:176"));
    int16_t next = -1;
    f.start([&next](GgGame& gg) { next = state09Puzzle(gg, StateEntry::Resume); });
    f.runUntil([](const GgTraceRecord&) { return false; }, 2'000'000);
    REQUIRE(f.done());
    f.finish();
    CHECK(next == 5);
    CHECK(g.puzzles.result == 0);
    CHECK(g.puzzles.goThrough == 0);
    CHECK(puzzleSolved(g.player, 0, 0) == 0);
    CHECK(countCalls(f.sound(), "pzPlay(12,5)") == 1);
    CHECK(countCalls(f.sound(), "pzFreeBank()") == 1);
    CHECK(g.zones().local() == nullptr);  // Zones_ClearLocal, then state 9's Zones_Pop
    CHECK(g.video.palette().faded());
}

TEST_CASE("puzzles_a: leaving a solved puzzle opens the door and keeps the solved flag") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> solved;
    loadPuzzleState(f, "k0_solved", solved);
    GgGame& g = f.g();
    std::string why;
    const bool rooms = worldRestoreFromState(g, *solved, why);
    if (!rooms) MESSAGE("the world's restore failed (" << why << "): the door is not checked");
    const int room = g.playerRoom();
    const int doorObj = g.actors.actors[kPlayerActor].doorObj;
    const RoomObject* before = rooms ? roomGetObject(g, room, doorObj) : nullptr;
    if (before) CHECK(before->type == 7);
    CHECK(puzzleSolved(g.player, 0, 0) == 0);  // written when the puzzle is left (REFERENCE_CAPTURES §9.1)
    f.polls(true);
    REQUIRE(f.keys("20:tap:enter"));
    int16_t next = -1;
    f.start([&next](GgGame& gg) { next = state09Puzzle(gg, StateEntry::Resume); });
    f.runUntil([](const GgTraceRecord&) { return false; }, 2'000'000);
    REQUIRE(f.done());
    f.finish();
    CHECK(next == 5);
    CHECK(g.puzzles.result == 1);
    CHECK(g.puzzles.goThrough == 1);
    CHECK(puzzleSolved(g.player, 0, 0) == 1);
    if (const RoomObject* door = rooms ? roomGetObject(g, room, doorObj) : nullptr) {
        CHECK(door->type == 9);  // Room_OpenPuzzleDoor (the world's): open, the player through it
        CHECK(g.playerRoom() == door->targetRoom());
    }
}

TEST_CASE("puzzles_a: a launch that misses lands where the original's did") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> screen;
    loadPuzzleState(f, "k5_screen", screen);
    f.polls(true);
    const uint64_t base = f.runtime().clock().interrupts();
    double t = 20;
    // The ball onto the launcher, the plunger pulled 21 pixels (energy 7) and released.
    const std::string s = r2Mouse("drag 37 97 228 102 600 wait 1000 " + k5Pull(21) + " up", t);
    REQUIRE(f.mouse(s));
    f.start([](GgGame& g) { state09Puzzle(g, StateEntry::Resume); });
    runTo(f, base, t + 5 * 145.652);
    REQUIRE_FALSE(f.done());
    std::optional<GgStateFile> missed;
    missed.emplace(GgStateFile::load(*statePath("k5_missed")));
    // The ball in the 7th hole (landing 10) with the clip of its last flight step; not solved.
    const std::vector<gggame::Mask> masks{launchGlow(ggFiles->exe()), cursorBox(f.g().mouse().x(), f.g().mouse().y()),
                                          cursorBox(missed->cursor().mouseX, missed->cursor().mouseY)};
    const int px = f.differingPixels(*shotPath("k5_05_missed"), masks);
    explainDiff(f, *shotPath("k5_05_missed"), "k5_05_missed");
    CHECK(px == 0);
    f.finish();
    CHECK(f.g().puzzles.launch.landing == 10);
    const auto diff = fieldDifferences(f.g(), *missed,
                                       {"g_launchGlowOn", "g_launchObjectShown", "DS_af92_launchClipBottom", "DS_af94_launchClipTop",
                                        "DS_af96_launchClipRight", "DS_af98_launchClipLeft", "DS_af9c_launchObjectY",
                                        "DS_af9e_launchObjectX", "g_launchObject", "g_launchSurface", "g_launchLanding",
                                        "g_launchGravity", "g_launchRamp", "g_launchTarget", "g_launchEnergy"});
    CHECK_MESSAGE(diff.empty(), joined(diff));
    CHECK(countSound(f.trace(), "pzPlay(16,5)") == 0);  // no reward
}

TEST_CASE("puzzles_a: the launch's sounds against r2_puzzle_k5_miss.dro") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto dro = testdata::ggCapture("audio/r2_puzzle_k5_miss.dro");
    REQUIRE(dro);
    // The puzzle from its door state (the bank is loaded at its start), then the ball at sound tick 100
    // and the plunger from 260 to 371 with energy 3 (landing 6: three flight steps), as the capture.
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> door;
    loadState(f, "k5_door", door);
    for (int i = 0; i < 11; ++i) f.rand().rand();
    hintSeen(f.g().player, 0x1052) = 1;  // the capture has no hint chime: the player had seen the hint
    f.g().display.fadeOut();
    f.sound().clear();
    f.polls(true);
    f.start([](GgGame& g) { state09Puzzle(g); });
    auto rows = f.runUntil([](const GgTraceRecord& r) { return r.sound.find("pzLoadBank(4558)") != std::string::npos; }, 400000);
    REQUIRE(!rows.empty());
    const uint32_t t0 = rows.back().ticks;  // the bank's load (sound tick 0)
    const uint64_t i0 = rows.back().interrupts;
    const auto now = f.runtime().clock().interrupts();
    // An event of PIT tick n comes right before interrupt n: the game's call follows at sound tick
    // (n - i0) / 2 when n = i0 + 2 k + 1.
    auto at = [&](int soundTick) { return static_cast<long long>(i0 + 2 * static_cast<uint64_t>(soundTick)) + 1 - static_cast<long long>(now); };
    std::string script = std::format("{}:click:37:97", at(100) - 1);
    script += std::format(", {}:move:283:141, {}:buttons:1", at(260) - 3, at(260) - 1);
    for (int i = 1; i <= 9; ++i) script += std::format(", {}:move:{}:141", at(260) + 10 * i, 283 + i);
    script += std::format(", {}:buttons:0", at(371) - 1);
    REQUIRE(f.mouse(script, true));
    const size_t from = f.trace().size();
    runTo(f, i0, 2.0 * 700);
    std::vector<GgTraceRecord> after(f.trace().begin() + static_cast<std::ptrdiff_t>(from), f.trace().end());
    f.finish();
    // The calls' sound ticks (PIT ticks after the bank / 2).
    auto tickOf = [&](const std::string& call) -> int {
        const auto t = soundTick(after, call);
        REQUIRE_MESSAGE(t.has_value(), call);
        return static_cast<int>((*t - t0) / 2);
    };
    const int pick = tickOf("pzPlay(0,3)"), pull = tickOf("pzPlay(1,3)"), climb = tickOf("pzPlay(4,3)"),
              flight = tickOf("pzPlay(7,3)"), land = tickOf("pzPlay(2,4)");
    MESSAGE(std::format("sound ticks: ball {}, plunger {}, climb {}, flight {}, landing {}", pick, pull, climb, flight, land));
    CHECK(flight - climb == 11);  // two climb steps of 11 PIT ticks
    CHECK(land - flight == 24);   // three flight steps of 16 PIT ticks
    // The calls as a sound schedule (the room music the door state played, stopped by state 8, first)
    // against the capture: every OPL write.
    auto sch = *SoundTestSchedule::named("puzzle_k5");  // the capture and how to compare with it
    sch.steps = SoundTestSchedule::parse(std::format(
                    "0:R:1:1,0:x,0:P:11ce,{}:y:0:3,{}:y:1:3,{}:u:1,{}:y:4:3,{}:y:7:3,{}:u:7,{}:y:2:4,700:end", pick, pull,
                    climb, climb, flight, land, land))
                    .steps;
    const DroComparison c = checkSchedule(ggFiles->exe(), ggFiles->archive(), sch, *dro);
    MESSAGE(c.summary());
    CHECK(c.refWrites == 162);
    CHECK(c.matched == c.refWrites);
}

TEST_CASE("puzzles_a: the DAM engine over the launch's twelve scripts") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    DamPlayer dam;
    int ticks = 0, cels = 0;
    for (uint16_t id = 0x114B; id <= 0x1156; ++id) {
        CAPTURE(id);
        const auto script = g.archive().read(id);
        const Bundle frames = Bundle::load(g.archive(), static_cast<uint16_t>(id + 0x0C));
        std::vector<std::span<const uint8_t>> table;
        for (size_t i = 0; i < frames.size(); ++i) table.push_back(frames.member(i).payload);
        dam.setFrames(table);
        const int ch = dam.start(g, 0, 0, 0, 0, script);
        CHECK(ch == 0);
        CHECK(g.puzzles.dam.channels[0][0x1A] != 0);  // a cursor: running
        int n = 0;
        while (dam.isRunning(ch, script) && n < 100) {
            dam.step(g);
            ++n;
            cels += static_cast<int>(dam.cels().size());
            for (const auto& cel : dam.cels()) CHECK(!cel.picture.empty());
        }
        CHECK((n >= 3 && n <= 9));
        ticks += n;
        CHECK(g.puzzles.dam.celCount == static_cast<int16_t>(dam.cels().size()));
        CHECK(g.puzzles.dam.channels[0][0x1A] == 0);  // freed by 0x7FFF
    }
    // 62 cel opcodes; 50 ends of tick and 12 ends: 50 ticks end at a 0x7FFE, 12 at the 0x7FFF.
    CHECK(cels == 62);
    CHECK(ticks == 62);
    CHECK(dam.rects().empty());
    CHECK(g.puzzles.dam.rectCount == 0);
}

TEST_CASE("puzzles_a: a launch surface change plays its DAM script at 19 ticks a step") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    // Variant 18 offers the four surface tools (its ramp is the fixed ramp 4: the scripts + 1).
    g.puzzles.kind = 5;
    g.puzzles.variant = 18;
    hintSeen(g.player, 0x1052) = 1;  // no New Player Hint box before the loop
    f.polls(true);
    const uint64_t base = f.runtime().clock().interrupts();
    // Tool 0x452 (surface 1 -> 0, DAM 0x1151 + 1), then tool 0x456 (0 -> 1, DAM 0x1154 + 1 with
    // effect 0x13 restarted at every DAM tick).
    REQUIRE(f.mouse("40:click:15:58, 700:click:18:72"));
    f.start([](GgGame& gg) { puzzleDispatch(gg); });
    runTo(f, base, 1500);
    REQUIRE_FALSE(f.done());
    std::vector<uint32_t> plays;
    for (const auto& r : f.trace())
        for (size_t at = r.sound.find("pzPlay(19,3)"); at != std::string::npos; at = r.sound.find("pzPlay(19,3)", at + 1))
            plays.push_back(r.ticks);
    CHECK(countSound(f.trace(), "pzPlay(21,3)") == 1);
    f.finish();
    CHECK(g.puzzles.launch.surface == 1);
    // The DAM steps of script 0x1155.
    DamPlayer dam;
    const auto script = g.archive().read(0x1155);
    dam.start(g, 0, 0, 0, 0, script);
    int steps = 0;
    while (dam.isRunning(0, script) && steps < 50) {
        dam.step(g);
        ++steps;
    }
    REQUIRE(plays.size() == static_cast<size_t>(steps) + 1);  // the tool's own, then one per DAM tick
    CHECK(plays[1] == plays[0]);  // the first DAM tick right away
    for (size_t i = 2; i < plays.size(); ++i) CHECK(plays[i] - plays[i - 1] == 19);  // Timer_Set(7, 18), strict
}

TEST_CASE("puzzles_a: an energy line to the wrong device retracts when checked") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> screen;
    loadPuzzleState(f, "k6_screen", screen);
    f.sound().clear();
    f.polls(true);
    const uint64_t base = f.runtime().clock().interrupts();
    double t = 20;
    // Mechanical (row 0) to device 0, which needs the chemical form; then the check button.
    const std::string s = r2Mouse("move 181 36 wait 300 click 181 36 wait 500 move 219 36 wait 400 click 219 36 wait 600 "
                                  "move 120 140 wait 300 click 26 137 wait 3000",
                                  t);
    REQUIRE(f.mouse(s));
    f.start([](GgGame& g) { state09Puzzle(g, StateEntry::Resume); });
    runTo(f, base, t);
    REQUIRE_FALSE(f.done());
    const auto rows = f.trace();
    f.finish();
    EnergyState& e = f.g().puzzles.energy;
    CHECK(energyLine(e, 0, 0) == 0);
    CHECK(energySent(e, 0, 0) == 0);
    CHECK(e.linesLeft == 3);
    CHECK(e.mode == 0);
    // A pulse to the device (effect 2), then the line retracting (effect 3), each 9 frames of 5 ticks
    // (Timer_Wait(7) is no boundary of the fixture: both come in one record, 90 ticks after the one
    // before it, the release of the check button).
    size_t i = 0;
    while (i < rows.size() && rows[i].sound.find("pzPlay(2,3)") == std::string::npos) ++i;
    REQUIRE((i > 0 && i < rows.size()));
    CHECK(rows[i].sound.find("pzPlay(3,3)") != std::string::npos);
    MESSAGE(std::format("the check's pulses: {} ticks", rows[i].ticks - rows[i - 1].ticks));
    CHECK((rows[i].ticks - rows[i - 1].ticks >= 90 && rows[i].ticks - rows[i - 1].ticks <= 91));
    CHECK(countSound(rows, "pzPlay(39,") + countSound(rows, "pzPlay(40,") + countSound(rows, "pzPlay(41,") +
              countSound(rows, "pzPlay(42,") ==
          0);  // no jingle
}

TEST_CASE("puzzles_a: a simple machines part in the wrong place bounces back") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> screen;
    loadPuzzleState(f, "k1_screen", screen);
    f.sound().clear();
    f.polls(true);
    const uint64_t base = f.runtime().clock().interrupts();
    double t = 20;
    const std::string s = r2Mouse("click 24 88 wait 600 move 100 40 wait 500 click 100 40 wait 700 move 120 60 wait 300 "
                                  "click 28 156 wait 2000",
                                  t);
    REQUIRE(f.mouse(s));
    f.start([](GgGame& g) { state09Puzzle(g, StateEntry::Resume); });
    runTo(f, base, t);
    REQUIRE_FALSE(f.done());
    // Back in the inventory and the machine as it was: the first screen but for the cursor.
    const std::vector<gggame::Mask> masks{cursorBox(28, 156), cursorBox(screen->cursor().mouseX, screen->cursor().mouseY)};
    const int px = f.differingPixels(*shotPath("k1_01_screen"), masks);
    explainDiff(f, *shotPath("k1_01_screen"), "k1_bounced");
    CHECK(px == 0);
    const auto rows = f.trace();
    f.finish();
    const SimpleState& sm = f.g().puzzles.simple;
    CHECK(static_cast<int8_t>(sm.data[2]) == 1);  // the wedge
    CHECK(sm.held == 9);
    for (const auto& p : sm.placed) CHECK(p.type == 9);
    CHECK(countSound(rows, "pzPlay(17,3)") == 1);  // the bounce
    CHECK(countSound(rows, "pzPlay(14,3)") == 1);  // the check
    CHECK(countSound(rows, "pzPlay(19,5)") == 0);  // no reveal
}

TEST_CASE("puzzles_a: electric circuits with a switch (variant 21's loop)") {
    GG_REQUIRE_DATA();
    PzElectric e(ggFiles->exe(), ggFiles->archive());
    e.loadVariant(21);
    auto& s = e.st();
    // Battery 0, bulb 1, battery 2, bulb 3, switch 4 in one loop; the batteries face each other.
    REQUIRE(s.pieces[4].type == PzElectric::Switch);
    CHECK(e.usedPieces() == 5);
    CHECK_FALSE(e.isSolved());  // the switch is open
    s.pieces[4].closed = 1;
    CHECK_FALSE(e.bulbLit(1));  // batteries of opposite directions
    CHECK_FALSE(e.isSolved());
    // Battery 2 turned round: bulb 1 to its terminal 1, bulb 3 to its terminal 0.
    auto link = [&](int p, int t, int q, int u, int flag) {
        s.terminals[static_cast<size_t>(p)][static_cast<size_t>(t)].piece = static_cast<int16_t>(q);
        s.terminals[static_cast<size_t>(p)][static_cast<size_t>(t)].term = static_cast<int16_t>(u);
        s.terminals[static_cast<size_t>(p)][static_cast<size_t>(t)].flag = static_cast<int16_t>(flag);
    };
    link(1, 1, 2, 1, 1);
    link(2, 1, 1, 1, 0);
    link(3, 0, 2, 0, 0);
    link(2, 0, 3, 0, 1);
    CHECK(e.bulbLit(1));
    CHECK(e.bulbLit(3));
    CHECK(e.allBulbsLit());
    CHECK(e.isSolved());  // and opening the switch darkens the bulbs
    CHECK(s.pieces[4].closed == 1);  // the test restores the switch
    s.pieces[4].closed = 0;
    CHECK_FALSE(e.allBulbsLit());
    CHECK_FALSE(e.isSolved());
    // The path table holds the last bulb's paths (DS:a138, DS:a2c8).
    CHECK(s.pathCount >= 1);
    CHECK(s.paths[0][0] != -1);
}

TEST_CASE("puzzles_a: the moments of the puzzle states that can be resumed") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    // The main loops' moments (the poll, the idle animation, the dispatch: k5_missed stopped between
    // PzLaunch_GetCode and the switch) and the nested ones (the rubber band, the plunger held, the hint
    // viewer).
    const std::pair<const char*, bool> states[] = {
        {"k0_screen", true},       {"k0_solved", true},        {"k1_screen", true},      {"k1_placed", true},
        {"k1_solved", true},       {"k5_screen", true},        {"k5_missed", true},      {"k5_solved", true},
        {"k6_screen", true},       {"k6_matched", true},       {"k6_solved", true},      {"k0_wire_strung", false},
        {"k0_hint_text", false},   {"k5_energy4_held", false}, {"k5_hint_text", false},  {"k1_hint_text", false},
        {"k6_hint_text", false},
    };
    for (const auto& [name, ok] : states) {
        CAPTURE(std::string(name));
        gggame::GameFixture f(*ggFiles);
        std::optional<GgStateFile> s;
        loadState(f, name, s);
        std::string why;
        const bool restored = puzzlesRestoreFromState(f.g(), *s, why);
        CHECK_MESSAGE(restored == ok, why);
        if (!ok) {
            MESSAGE(std::string(name) << ": " << why);
            CHECK(f.g().zones().local() == nullptr);
            CHECK(f.g().zones().depth() == 0);
        }
    }
}
