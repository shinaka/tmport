// Wave 3, puzzles part B (re/gg/docs/code_puzzles_b.md): machine pieces, gears, balance and magnets on
// the game, checked against the DOSBox-X states and shots of the reference set (REFERENCE_CAPTURES §9.1,
// the w0 tour's machine pieces w0/17..20): each puzzle from its door state to its first screen, the
// recorded solutions (tools/gg/capture_r2.py, tools/gg/capture_tour.py) replayed as scripted mouse input
// in PIT ticks to the solved screens and the solved states' DGROUP and heap data, the captured
// intermediate and failure moments (the unbalanced beam, the repelled magnets, the crank turned the wrong
// way), the Hint buttons, and unit tests of the simulations with the spec's values (gear drive, balance
// arithmetic, magnet forces). Only counts, ticks, coordinates, flags and differing-pixel counts here.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/game.h"
#include "gg/game/puzzles.h"
#include "gg/game/pz_balance.h"
#include "gg/game/pz_gears.h"
#include "gg/game/pz_magnets.h"
#include "gg/game/pz_pieces.h"
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

std::optional<std::filesystem::path> statePath(const std::string& name) { return testdata::ggCapture("savestates/" + name + ".sav"); }
std::optional<std::filesystem::path> shotPath(const std::string& name) { return testdata::ggCapture(name + ".png"); }

// Scripted mouse input in PIT ticks after a start time: capture_r2.py's MOUSEKEY tokens ("click X Y",
// "move X Y", "wait MS", "down [X Y]", "up [X Y]", "drag X1 Y1 X2 Y2 MS") and the same steps by call,
// timed as tools/tmctl.py posts them: a MOUSEKEY command is 10 posted key edges 5 ms apart (about 62 ms
// in all) and acts at its fifth key (about 50 ms in); a command reports the position it carries, so a
// click's release puts back the pointer the game may have warped meanwhile (REFERENCE_CAPTURES §2.2,
// §2.3). "down" / "up" without coordinates report the TSR's current position (which follows the game's
// own INT 33h fn 4 moves): only the button changes.
class Script {
public:
    static constexpr double kAct = 50 * kTicksPerMs;   // the command acts ~50 ms after its start
    static constexpr double kPost = 62 * kTicksPerMs;  // and takes ~62 ms to post

    explicit Script(double t0 = 20) : t_(t0) {}
    double time() const { return t_; }
    const std::string& text() const { return out_; }

    Script& move(int x, int y) { return command({std::format("move:{}:{}", x, y)}); }
    Script& down() { return command({"buttons:1"}); }
    Script& up() { return command({"buttons:0"}); }
    Script& downAt(int x, int y) { return command({std::format("move:{}:{}", x, y), "buttons:1"}); }
    Script& upAt(int x, int y) { return command({std::format("move:{}:{}", x, y), "buttons:0"}); }
    Script& waitMs(double ms) {
        t_ += ms * kTicksPerMs;
        return *this;
    }
    Script& click(int x, int y) {
        downAt(x, y);
        waitMs(120);  // tmctl.py's click holds the button 120 ms
        return upAt(x, y);
    }
    Script& drag(int x1, int y1, int x2, int y2, double ms) {
        move(x1, y1).downAt(x1, y1);
        const double start = t_, dur = std::max(ms, 1.0) * kTicksPerMs;
        for (;;) {
            const double f = std::min(1.0, (t_ - start) / dur);
            move(static_cast<int>(std::lround(x1 + (x2 - x1) * f)), static_cast<int>(std::lround(y1 + (y2 - y1) * f)));
            if (f >= 1.0) break;
        }
        return upAt(x2, y2);
    }
    Script& tokens(std::string_view text) {
        std::istringstream in{std::string(text)};
        std::string op;
        while (in >> op) {
            if (op == "wait") {
                double ms = 0;
                in >> ms;
                waitMs(ms);
            } else if (op == "move" || op == "click") {
                int x = 0, y = 0;
                in >> x >> y;
                if (op == "move") move(x, y);
                else click(x, y);
            } else if (op == "down" || op == "up") {
                const auto pos = in.tellg();
                int x = 0, y = 0;
                if (in >> x >> y) {
                    if (op == "down") downAt(x, y);
                    else upAt(x, y);
                } else {
                    in.clear();
                    in.seekg(pos);
                    if (op == "down") down();
                    else up();
                }
            } else if (op == "drag") {
                int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                double ms = 0;
                in >> x1 >> y1 >> x2 >> y2 >> ms;
                drag(x1, y1, x2, y2, ms);
            }
        }
        return *this;
    }

private:
    Script& command(std::initializer_list<std::string> events) {
        const double at = t_ + kAct;
        for (const std::string& e : events) {
            if (!out_.empty()) out_ += ", ";
            out_ += std::format("{}:{}", static_cast<long long>(std::floor(at)), e);
        }
        t_ += kPost;
        return *this;
    }
    double t_;
    std::string out_;
};

// Steps the running function until `ticks` PIT interrupts have run since `base` (or it returned).
void runTo(gggame::GameFixture& f, uint64_t base, double ticks) {
    const auto target = base + static_cast<uint64_t>(std::ceil(ticks));
    if (f.done()) return;
    f.runUntil([&](const GgTraceRecord& r) { return r.interrupts >= target; }, 20'000'000);
}

// A DOSBox-X state into the fixture's game; the state file stays open in `s`.
void loadState(gggame::GameFixture& f, const std::string& name, std::optional<GgStateFile>& s) {
    const auto path = statePath(name);
    REQUIRE(path);
    REQUIRE(f.loadState(*path));
    s.emplace(GgStateFile::load(*path));
}

// The state's moment inside the running puzzle (puzzlesRestoreFromState and the kind's restore).
void loadPuzzleState(gggame::GameFixture& f, const std::string& name, std::optional<GgStateFile>& s) {
    loadState(f, name, s);
    std::string why;
    const bool ok = puzzlesRestoreFromState(f.g(), *s, why);
    REQUIRE_MESSAGE(ok, why);
}

// The bounding box of the pixels that differ from a capture (a message), and the picture written to
// $GG_PUZZLES_B_DUMP/<tag>.png when that variable is set (debugging only).
void explainDiff(gggame::GameFixture& f, const std::filesystem::path& shot, const std::string& tag) {
    const auto frame = f.frame();
    if (const char* dir = std::getenv("GG_PUZZLES_B_DUMP")) {
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

// Differing pixels against a shot (with the message when there are any).
int shotDiff(gggame::GameFixture& f, const std::string& shot, std::vector<gggame::Mask> masks = {}) {
    const auto path = shotPath(shot);
    REQUIRE(path);
    const int px = f.differingPixels(*path, masks);
    if (px != 0) explainDiff(f, *path, std::filesystem::path(shot).filename().string());
    return px;
}

// Modelled bytes of the named fields that differ between the game's DGROUP and a state's.
std::vector<std::string> fieldDifferences(const GgGame& g, const GgStateFile& s, const std::vector<std::string_view>& names) {
    const auto mine = dumpToDgroup(g, s.dgroup());
    std::vector<std::string> out;
    for (const DgroupField& fd : gameStateFields()) {
        if (std::find(names.begin(), names.end(), fd.name) == names.end()) continue;
        for (size_t i = 0; i < fd.size; ++i)
            if (fd.modelled(i) && mine[fd.ds + i] != s.dgroup()[fd.ds + i]) {
                out.push_back(std::format("{}+{:#x} ({:02x} vs {:02x})", fd.name, i, mine[fd.ds + i], s.dgroup()[fd.ds + i]));
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

// The gears' variant copy (grid and objects) against the state's heap copy DS:b6d2.
std::vector<std::string> gearsHeapDifferences(const GgGame& g, const GgStateFile& s) {
    std::vector<std::string> out;
    const auto& st = g.puzzles.gears;
    const size_t n = st.objects.size();
    const auto copy = s.bytesAt(s.farPointerAt(dg::kGearsData), 0x116 + 20 * n);
    if (copy.size() != 0x116 + 20 * n) return {"no copy in the state"};
    if (!std::equal(st.grid.begin(), st.grid.end(), copy.begin())) out.push_back("grid");
    for (size_t i = 0; i < n; ++i) {
        uint8_t mine[20] = {};
        st.objects[i].store(mine);
        for (size_t k = 0; k < 20; k += 2)
            if (mine[k] != copy[0x116 + 20 * i + k] || mine[k + 1] != copy[0x116 + 20 * i + k + 1]) {
                out.push_back(std::format("object {} word +{:#x}: {} vs {}", i, k, static_cast<int16_t>(mine[k] | (mine[k + 1] << 8)),
                                          static_cast<int16_t>(copy[0x116 + 20 * i + k] | (copy[0x116 + 20 * i + k + 1] << 8))));
                break;
            }
    }
    return out;
}

// The magnet slots against the state's heap block DS:af52.
std::vector<std::string> magnetsHeapDifferences(const GgGame& g, const GgStateFile& s) {
    std::vector<std::string> out;
    const auto block = s.bytesAt(s.farPointerAt(dg::kMagnetsSlots), 64 * MagnetRec::kDgroupSize);
    if (block.size() != 64 * MagnetRec::kDgroupSize) return {"no slots in the state"};
    for (size_t i = 0; i < 64; ++i) {
        MagnetRec ref;
        ref.load(block.data() + MagnetRec::kDgroupSize * i);
        const MagnetRec& m = g.puzzles.magnets.slots[i];
        if (ref.kind == -1 && m.kind == -1) continue;  // the other words of a free slot are the heap's
        if (!(ref == m))
            out.push_back(std::format("slot {}: kind {} o {} ({}, {}) att {} match {} vs kind {} o {} ({}, {}) att {} match {}", i, m.kind,
                                      m.orientation, m.x, m.y, m.attached, m.matched, ref.kind, ref.orientation, ref.x, ref.y,
                                      ref.attached, ref.matched));
    }
    return out;
}

int countSound(const std::vector<GgTraceRecord>& rows, const std::string& call) {
    int n = 0;
    for (const auto& r : rows)
        for (size_t at = r.sound.find(call); at != std::string::npos; at = r.sound.find(call, at + 1)) ++n;
    return n;
}

const std::vector<std::string_view> kFramework = {"g_puzzleKind", "g_puzzleVariant", "g_puzzleResult", "g_puzzleGoThrough", "g_puzzleLeave"};

std::vector<std::string_view> with(std::vector<std::string_view> a, const std::vector<std::string_view>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// A replay from a state inside a puzzle's loop: the script, then the run to its end plus `afterMs`.
struct Replay {
    gggame::GameFixture& f;
    uint64_t base = 0;
    uint64_t calls = 0;
    double t = 20;

    explicit Replay(gggame::GameFixture& fx) : f(fx) {}
    void start(const std::string& state, std::optional<GgStateFile>& s) {
        loadPuzzleState(f, state, s);
        f.present();
        calls = f.rand().calls();
        f.sound().clear();
        f.polls(true);
        base = f.runtime().clock().interrupts();
        f.start([](GgGame& g) { state09Puzzle(g, StateEntry::Resume); });
    }
    // Adds the script (times after the previous one's end) and runs to its end + afterMs.
    void play(Script script, double afterMs = 0) {
        const uint64_t now = f.runtime().clock().interrupts();
        REQUIRE(f.mouse(script.text()));
        t = static_cast<double>(now - base) + script.time() + afterMs * kTicksPerMs;
        runTo(f, base, t);
    }
};

}  // namespace

// ------------------------------------------------------------------------------ the screens from the doors

TEST_CASE("puzzles_b: each part-B puzzle from its door state to its first screen") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    for (const int kind : {2, 3, 4, 7}) {
        CAPTURE(kind);
        gggame::GameFixture f(*ggFiles);
        std::optional<GgStateFile> door, screen;
        loadState(f, std::format("r2/puzzle/k{}_door", kind), door);
        screen.emplace(GgStateFile::load(*statePath(std::format("r2/puzzle/k{}_screen", kind))));
        // 11 calls of the two chimps come before the choice (REFERENCE_CAPTURES §10.7): the actors'.
        for (int i = 0; i < 11; ++i) f.rand().rand();
        const uint64_t calls = f.rand().calls();
        f.g().display.fadeOut();  // State_08's Pal_FadeOut
        f.sound().clear();
        f.polls(true);
        f.start([](GgGame& g) { state09Puzzle(g); });
        auto rows = f.runUntil([](const GgTraceRecord& r) { return r.yield == YieldPoint::GetInputCode; }, 200000);
        REQUIRE(!rows.empty());
        CHECK(f.g().puzzles.kind == kind);
        CHECK(f.g().puzzles.variant == 0);
        // The set-ups call rand() never: the kind and the candidate index only (2 calls).
        CHECK(f.rand().calls() - calls == 2);
        CHECK(f.rand().seed() == screen->dsDword(dg::kRandState));
        // The New Player Hint 0x1052's box (Hint_Show, the UI's) over the puzzle's screen, then Enter: the
        // first screen. The box forced the default cursor, and the active copy stays the arrow until the
        // next draw (none of the four loops flushes before input): the shots show the arrow at (269, 150).
        const int hint = f.differingPixels(*shotPath(std::format("r2/puzzle/k{}_00_hint", kind)));
        CHECK(hint == 0);
        REQUIRE(f.keys("2:tap:enter"));
        runTo(f, f.runtime().clock().interrupts(), 2 * 145.652);
        const int px = shotDiff(f, std::format("r2/puzzle/k{}_01_screen", kind));
        MESSAGE(std::format("kind {}: the hint box {} px, the first screen {} px", kind, hint, px));
        CHECK(px == 0);
        CHECK(hintSeen(f.g().player, 0x1052) == 1);
        const uint16_t banks[8] = {0, 0, 0x11CA, 0x11C8, 0x11C6, 0, 0, 0x11D0};
        CHECK(countSound(f.trace(), std::format("pzLoadBank({})", banks[kind])) == 1);
        // The puzzle's DGROUP at its first poll equals the screen state's.
        std::vector<std::string_view> fields;
        if (kind == 2) fields = {"g_pieces", "g_piecesInBox", "g_piecesCount", "g_piecesGroup", "g_piecesHeld"};
        if (kind == 3) fields = {"g_gearsStock", "g_gearsCrankRect", "g_gearsHeld", "g_gearsObjectCount", "g_gearsJam", "g_gearsCrankCY", "g_gearsCrankCX"};
        if (kind == 4) fields = {"g_balanceRightHeavy", "g_balanceZoneSet", "g_balanceTilt", "g_balanceHeld", "g_balanceSolved", "g_balanceHookWeights", "g_balanceStock"};
        if (kind == 7) fields = {"g_magnetsHeldOrient", "g_magnetsHeld", "g_magnetsCounts", "DS_af56_magnetsUnused"};
        const auto diff = fieldDifferences(f.g(), *screen, fields);
        CHECK_MESSAGE(diff.empty(), joined(diff));
        if (kind == 3) {
            const auto heap = gearsHeapDifferences(f.g(), *screen);
            CHECK_MESSAGE(heap.empty(), joined(heap));
        }
        f.finish();
    }
}

// ---------------------------------------------------------------------------------------- machine pieces

TEST_CASE("puzzles_b: machine pieces variant 0 solved by the recorded input") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> s;
    Replay r(f);
    r.start("r2/puzzle/k2_screen", s);
    CHECK(shotDiff(f, "r2/puzzle/k2_01_screen") == 0);
    const uint32_t points = f.g().player.points;
    // capture_r2.py puzzle_k2: the handle out of the box onto the work area, then the head over its end.
    r.play(Script().tokens("click 47 122 wait 600 move 150 80 wait 400 click 150 80 wait 600"));
    CHECK(f.g().puzzles.pieces.inBox == 1);
    CHECK(f.g().puzzles.pieces.pieces[1].placed == 1);
    r.play(Script().tokens("click 47 122 wait 600 move 146 59 wait 400 click 146 59 wait 600 move 200 140"), 3000);
    REQUIRE_FALSE(f.done());
    std::optional<GgStateFile> solved;
    solved.emplace(GgStateFile::load(*statePath("r2/puzzle/k2_solved")));
    CHECK(f.g().player.points == points + 400);
    CHECK(shotDiff(f, "r2/puzzle/k2_03_solved") == 0);
    // Two takes (one try each) and the jingle: 3 rand() calls, the solved state's rand state.
    CHECK(f.rand().calls() - r.calls == 3);
    CHECK(f.rand().seed() == solved->dsDword(dg::kRandState));
    CHECK(countSound(f.trace(), "pzPlay(19,5)") == 1);  // the reveal
    CHECK(countSound(f.trace(), "pzStop(19)") == 1);
    f.finish();
    const auto diff = fieldDifferences(
        f.g(), *solved, with({"g_pieces", "g_piecesInBox", "g_piecesCount", "g_piecesGroup", "g_piecesHeld"}, kFramework));
    CHECK_MESSAGE(diff.empty(), joined(diff));
}

TEST_CASE("puzzles_b: machine pieces, the w0 tour's T-handle (variant 2) from w0/17 to the solved machine") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> s;
    Replay r(f);
    r.start("w0/17_puzzle", s);
    CHECK(f.g().puzzles.kind == 2);
    CHECK(f.g().puzzles.variant == 2);
    CHECK(shotDiff(f, "w0/17_puzzle", {gggame::Mask{0, 0, 319, 9}}) == 0);  // the menu bar's hint glow aside
    // capture_tour.py PUZZLE: the shaft out of the box (dragged into the work area), dropped at (200, 30).
    r.play(Script().tokens("drag 47 122 200 80 600").waitMs(1000).tokens("move 200 30").waitMs(500).tokens("click 200 30"), 1000);
    std::optional<GgStateFile> s18;
    s18.emplace(GgStateFile::load(*statePath("w0/18_puzzle_piece1")));
    CHECK(f.rand().calls() - r.calls == 1);  // random(3) = 1, the shaft, at the first try
    CHECK(f.rand().seed() == s18->dsDword(dg::kRandState));
    CHECK(shotDiff(f, "w0/18_puzzle_piece1_placed") == 0);
    {
        const auto diff = fieldDifferences(f.g(), *s18, {"g_pieces", "g_piecesInBox", "g_piecesCount", "g_piecesGroup", "g_piecesHeld"});
        CHECK_MESSAGE(diff.empty(), joined(diff));
    }
    const uint64_t calls18 = f.rand().calls();
    // The wedge and the bar out of the box; the bar onto the shaft (the T).
    r.play(Script()
               .tokens("click 47 122")
               .waitMs(800)
               .tokens("move 150 60")
               .waitMs(800)
               .tokens("click 120 40")
               .waitMs(800)
               .tokens("click 47 122")
               .waitMs(800)
               .tokens("move 260 40")
               .waitMs(800)
               .tokens("click 190 24"),
           1000);
    CHECK(shotDiff(f, "w0/19_puzzle_t_handle") == 0);
    // The wedge picked up and dropped under the shaft: it snaps by 1 px; the machine is revealed.
    r.play(Script().tokens("click 125 48").waitMs(600).tokens("move 150 90").waitMs(600).tokens("click 200 126"), 3500);
    std::optional<GgStateFile> s20;
    s20.emplace(GgStateFile::load(*statePath("w0/20_puzzle_solved")));
    CHECK(f.rand().calls() - calls18 == 5);  // 2 + 2 tries for the two takes, 1 jingle
    CHECK(f.rand().seed() == s20->dsDword(dg::kRandState));
    CHECK(shotDiff(f, "w0/20_puzzle_solved") == 0);
    f.finish();
    const auto diff = fieldDifferences(f.g(), *s20,
                                       with({"g_pieces", "g_piecesInBox", "g_piecesCount", "g_piecesGroup", "g_piecesHeld"}, kFramework));
    CHECK_MESSAGE(diff.empty(), joined(diff));
}

// ----------------------------------------------------------------------------------------------- balance

TEST_CASE("puzzles_b: balance variant 0 solved by the recorded input") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> s;
    Replay r(f);
    r.start("r2/puzzle/k4_screen", s);
    CHECK(shotDiff(f, "r2/puzzle/k4_01_screen") == 0);
    // capture_r2.py puzzle_k4: the 3 weight from the inventory onto the left hook 3 (arm 3).
    // The session's shot of the carried weight was taken with the pointer at (100, 50) (the weight is
    // drawn with its top-left at the pointer); the press of the solved state is at 117 + 5 (the hot spot).
    r.play(Script().tokens("click 16 113 wait 600 move 100 50 wait 400"));
    CHECK(f.g().puzzles.balance.held == 2);
    CHECK(shotDiff(f, "r2/puzzle/k4_02_weight_carried") == 0);
    r.play(Script().tokens("move 117 50 wait 400"));
    r.play(Script().tokens("click 117 50 wait 1500 move 60 150"), 3000);
    REQUIRE_FALSE(f.done());
    std::optional<GgStateFile> solved;
    solved.emplace(GgStateFile::load(*statePath("r2/puzzle/k4_solved")));
    CHECK(f.g().puzzles.balance.tilt == 0);
    CHECK(f.g().puzzles.balance.solved == 1);
    CHECK(shotDiff(f, "r2/puzzle/k4_03_solved") == 0);
    CHECK(f.rand().calls() - r.calls == 1);  // the jingle
    CHECK(f.rand().seed() == solved->dsDword(dg::kRandState));
    // From -5 to level: 5 steps of 50 ticks, each with the rising effect 8 transposed 0, 1, 2 ..
    CHECK(countSound(f.trace(), "pzSetTranspose(8,") == 5);
    f.finish();
    const auto diff = fieldDifferences(f.g(), *solved,
                                       with({"g_balanceRightHeavy", "g_balanceZoneSet", "g_balanceTilt", "g_balanceHeld", "g_balanceSolved",
                                             "g_balanceHookWeights", "g_balanceStock"},
                                            kFramework));
    CHECK_MESSAGE(diff.empty(), joined(diff));
}

TEST_CASE("puzzles_b: balance, the weight on the far left hook swings the beam to the other end") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> s;
    Replay r(f);
    r.start("r2/puzzle/k4_screen", s);
    // The state's last press (81, 30) = pointer (76, 30) + the weight's hot spot: left hook 0 (arm 5).
    r.play(Script().tokens("click 16 113 wait 600 move 76 30 wait 400 click 76 30 wait 1500 move 60 150"), 4000);
    std::optional<GgStateFile> unbalanced;
    unbalanced.emplace(GgStateFile::load(*statePath("r2/puzzle/k4_unbalanced")));
    // L = 15 > R = 9: from -5 to +5 at (100 * 9 / 15 + 1) / 2 = 30 ticks per step; set 1; ab20 unchanged.
    CHECK(f.g().puzzles.balance.tilt == 5);
    CHECK(f.g().puzzles.balance.zoneSet == 1);
    CHECK(f.g().puzzles.balance.solved == 0);
    CHECK(shotDiff(f, "r2/puzzle/k4_04_unbalanced") == 0);
    CHECK(f.rand().calls() == r.calls);
    CHECK(countSound(f.trace(), "pzSetTranspose(8,") == 10);
    CHECK(countSound(f.trace(), "pzPlay(10,4)") == 1);
    f.finish();
    const auto diff = fieldDifferences(f.g(), *unbalanced,
                                       {"g_balanceRightHeavy", "g_balanceZoneSet", "g_balanceTilt", "g_balanceHeld", "g_balanceSolved",
                                        "g_balanceHookWeights", "g_balanceStock"});
    CHECK_MESSAGE(diff.empty(), joined(diff));
}

TEST_CASE("puzzles_b: the balance's sound calls in the order and spacing of r2_puzzle_k4_success.dro") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    // The capture's schedule (src/gg/audio/soundtest.cpp "puzzle_k4", every OPL write matched in wave 2),
    // in sound ticks after the bank's load: the weight taken at 100 and hung at 213, five swing steps of
    // effect 8 transposed 0..4 at 215, 240, 265, 290, 315, the jingle at 340 (then the points). Here the
    // same input at the same sound ticks: the engine's calls must follow in that order and spacing. The
    // 2 sound ticks between the hang and the first swing in the capture are the original's drawing time
    // (the beam drawn twice), which the engine's frames do not cost (PORTING_NOTES §2.4).
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> door;
    loadState(f, "r2/puzzle/k4_door", door);
    for (int i = 0; i < 11; ++i) f.rand().rand();
    hintSeen(f.g().player, 0x1052) = 1;  // the capture has no hint chime: the player had seen the hint
    f.g().display.fadeOut();
    f.sound().clear();
    f.polls(true);
    f.start([](GgGame& g) { state09Puzzle(g); });
    auto rows = f.runUntil([](const GgTraceRecord& r) { return r.sound.find("pzLoadBank(4550)") != std::string::npos; }, 400000);
    REQUIRE(!rows.empty());
    const uint32_t t0 = rows.back().ticks;
    const uint64_t i0 = rows.back().interrupts;
    const auto now = f.runtime().clock().interrupts();
    auto at = [&](int soundTick) { return static_cast<long long>(i0 + 2 * static_cast<uint64_t>(soundTick)) + 1 - static_cast<long long>(now); };
    // MOUSEKEY clicks: the press with its position, the release 120 ms (17 PIT ticks) later.
    const std::string script = std::format("{}:move:16:113, {}:buttons:1, {}:buttons:0, {}:move:117:50, {}:move:117:50, {}:buttons:1, {}:buttons:0",
                                           at(100) - 1, at(100) - 1, at(100) + 16, at(150), at(213) - 1, at(213) - 1, at(213) + 16);
    REQUIRE(f.mouse(script, true));
    const size_t from = f.trace().size();
    runTo(f, i0, 2.0 * 450);
    std::vector<GgTraceRecord> after(f.trace().begin() + static_cast<std::ptrdiff_t>(from), f.trace().end());
    f.finish();
    // Every call with its sound tick, in order. A record holds the calls made since the previous one,
    // and the work between two yield points costs no time: a call was made at the previous record's tick
    // (a record's own tick is the end of its yield, e.g. a 50-tick Timer_Wait).
    std::vector<std::pair<int, std::string>> calls;
    uint32_t prevTicks = f.trace()[from - 1].ticks;
    for (const auto& r : after) {
        std::istringstream in(r.sound);
        std::string c;
        while (std::getline(in, c, ';')) {
            while (!c.empty() && c.front() == ' ') c.erase(c.begin());
            if (!c.empty()) calls.emplace_back(static_cast<int>((prevTicks - t0) / 2), c);
        }
        prevTicks = r.ticks;
    }
    auto first = [&](const std::string& call, size_t start = 0) -> std::optional<size_t> {
        for (size_t i = start; i < calls.size(); ++i)
            if (calls[i].second == call) return i;
        return std::nullopt;
    };
    const auto take = first("pzPlay(0,3)"), hang = first("pzPlay(1,3)");
    if (!take) {
        std::string all;
        for (const auto& [t, c] : calls) all += std::format("{}:{} ", t, c);
        MESSAGE(all);
    }
    REQUIRE(take);
    REQUIRE(hang);
    CHECK(calls[*take].first == 100);
    CHECK(calls[*hang].first == 213);
    size_t k = *hang;
    for (int step = 0; step < 5; ++step) {
        CAPTURE(step);
        const auto t = first(std::format("pzSetTranspose(8,{})", step), k);
        REQUIRE(t);
        REQUIRE(*t + 1 < calls.size());
        CHECK(calls[*t + 1].second == "pzPlay(8,3)");
        CHECK(calls[*t].first == 213 + 25 * step);  // 50 PIT ticks per step from the hang (215 + 25 k there)
        k = *t + 1;
    }
    // The jingle effect 2 + random(4) 25 sound ticks after the last step: effect 5 as in the capture
    // (at 340 there: + 2), then the four points effects 13 (6 sound ticks apart there: PzSnd_WaitEnd on
    // the driver; the recording sound has no effect playing, so here at once).
    const auto jingle = first("pzPlay(5,4)", k);
    REQUIRE(jingle);
    CHECK(calls[*jingle].first == 213 + 125);
    CHECK(countSound(after, "pzPlay(13,5)") == 4);
    CHECK(countSound(after, "pzPlay(9,3)") == 0);  // rising only: from -5 toward level
}

// ------------------------------------------------------------------------------------------------- gears

namespace {

// The centre of the crank handle's rectangle of frame f (what PzGears_DragCrank warps the pointer to).
std::pair<int, int> handleCentre(const GgGame& g, int frame) {
    const RectRec& r = g.puzzles.gears.crankRect[static_cast<size_t>(frame + 1)];
    return {r.x0 + (r.x1 - r.x0) / 2, r.y0 + (r.y1 - r.y0) / 2};
}

// Places the large gear as capture_r2.py puzzle_k3 does (from k3_screen).
const char* kPlaceGear = "click 56 104 wait 600 move 174 67 wait 400 click 174 67 wait 800 move 250 140";

}  // namespace

TEST_CASE("puzzles_b: gears variant 0: the gear placed and the crank turned until both buttons are down") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> s;
    Replay r(f);
    r.start("r2/puzzle/k3_screen", s);
    CHECK(shotDiff(f, "r2/puzzle/k3_01_screen") == 0);
    r.play(Script().tokens(kPlaceGear), 1000);
    std::optional<GgStateFile> placed;
    placed.emplace(GgStateFile::load(*statePath("r2/puzzle/k3_gear_placed")));
    CHECK(shotDiff(f, "r2/puzzle/k3_02_gear_placed") == 0);
    {
        const auto diff = fieldDifferences(f.g(), *placed, {"g_gearsStock", "g_gearsHeld", "g_gearsObjectCount", "g_gearsJam"});
        CHECK_MESSAGE(diff.empty(), joined(diff));
        const auto heap = gearsHeapDifferences(f.g(), *placed);
        CHECK_MESSAGE(heap.empty(), joined(heap));
    }
    // The drag: a press on the handle, then one pointer event per crank step, each onto the handle of
    // the frame wanted (the game warps the pointer there after every step): 19 counter-clockwise steps,
    // one clockwise, one counter-clockwise. The racks stop at 36 (upper) and 34 (lower); the last step
    // moves both back onto their limits and presses the second button: the solved state's objects,
    // including the values saved before that step.
    std::vector<int> wanted;
    for (int i = 0; i < 19; ++i) wanted.push_back(+1);
    wanted.push_back(-1);
    wanted.push_back(+1);
    Script drag;
    drag.move(196, 128).waitMs(300).downAt(196, 128).waitMs(200);
    int frame = 0;
    for (size_t k = 0; k + 1 < wanted.size(); ++k) {
        frame = wanted[k] == +1 ? (frame + 23) % 24 : (frame + 1) % 24;  // counter-clockwise lowers the frame
        const auto [x, y] = handleCentre(f.g(), frame);
        drag.move(x, y).waitMs(120);
    }
    r.play(drag, 500);
    // After 19 steps counter-clockwise and one back: the racks one unit short of their limits, the
    // lower button released again: the session's shot "one button" (k3_04) in the middle of this drag.
    CHECK(f.g().puzzles.gears.objects[0].state == 6);
    CHECK(f.g().puzzles.gears.objects[8].state == 1);
    CHECK(f.g().puzzles.gears.objects[9].state == 0);
    CHECK(shotDiff(f, "r2/puzzle/k3_04_one_button") == 0);
    frame = (frame + 23) % 24;
    const auto [lx, ly] = handleCentre(f.g(), frame);
    r.play(Script().move(lx, ly).waitMs(120), 500);
    CHECK(f.g().puzzles.gears.objects[0].state == 5);
    r.play(Script().up(), 3000);
    REQUIRE_FALSE(f.done());
    std::optional<GgStateFile> solved;
    solved.emplace(GgStateFile::load(*statePath("r2/puzzle/k3_solved")));
    CHECK(shotDiff(f, "r2/puzzle/k3_05_solved") == 0);  // the hand at the last handle the game warped to
    CHECK(f.rand().calls() - r.calls == 1);
    CHECK(f.rand().seed() == solved->dsDword(dg::kRandState));
    f.finish();
    const auto diff = fieldDifferences(f.g(), *solved,
                                       with({"g_gearsStock", "g_gearsCrankRect", "g_gearsHeld", "g_gearsObjectCount", "g_gearsJam",
                                             "g_gearsCrankCY", "g_gearsCrankCX"},
                                            {"g_puzzleKind", "g_puzzleVariant", "g_puzzleResult"}));
    CHECK_MESSAGE(diff.empty(), joined(diff));
    const auto heap = gearsHeapDifferences(f.g(), *solved);
    CHECK_MESSAGE(heap.empty(), joined(heap));
}

TEST_CASE("puzzles_b: gears, the crank turned the wrong way (the racks driven left to their limits)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    // The session's shot k3_03 in the middle of a clockwise drag, the button still held: 22 pointer
    // events onto the next handle clockwise give 20 steps (two land on the slope of the handle the game
    // warped to: no step) and the racks stop after 7, at 20 and 23, the left ends of their ranges.
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> s;
    Replay r(f);
    r.start("r2/puzzle/k3_screen", s);
    r.play(Script().tokens(kPlaceGear), 1000);
    Script drag;
    drag.move(196, 128).waitMs(300).downAt(196, 128).waitMs(200);
    int frame = 0;
    for (int k = 0; k < 22; ++k) {
        frame = (frame + 1) % 24;
        const auto [x, y] = handleCentre(f.g(), frame);
        drag.move(x, y).waitMs(120);
    }
    r.play(drag, 300);
    const auto& o = f.g().puzzles.gears.objects;
    CHECK(o[0].state == 20);
    CHECK(o[3].col == 20);
    CHECK(o[2].col == 23);
    CHECK(f.g().puzzles.gears.turning);
    CHECK(shotDiff(f, "r2/puzzle/k3_03_crank_wrong_way") == 0);
    // Released with a button up: everything springs back (PzGears_Reset): the frames of the drag's start,
    // the racks home, the buttons up; the board as before the drag.
    r.play(Script().up(), 1000);
    CHECK(o[0].state == 0);
    CHECK(o[3].col == 27);
    CHECK(o[2].col == 30);
    CHECK_FALSE(f.g().puzzles.gears.turning);
    // The hand is at the last handle here (220, 119) and at (250, 140) in the shot: both boxes masked.
    CHECK(shotDiff(f, "r2/puzzle/k3_02_gear_placed", {gggame::Mask{218, 117, 238, 138}, gggame::Mask{248, 138, 268, 160}}) == 0);
    f.finish();
}

TEST_CASE("puzzles_b: gears, the capture's two-turn drag as posted (not replayable in DOSBox-X)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    // capture_r2.py's crank(196, 112, 16): 49 moves 15 degrees apart counter-clockwise from the handle at
    // the bottom, 60 ms apart. The reference replay solved 2 of 4 tries: the game warps the pointer to
    // the handle after every step (35bb/3b2f INT 33h fn 4), so each posted absolute move is judged against
    // the warped point, and moves that arrive while a step is being drawn are dropped (DS:01e2). The
    // engine's frames cost no time, so no move is dropped and the outcome is fixed: recorded here.
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> s;
    Replay r(f);
    r.start("r2/puzzle/k3_screen", s);
    r.play(Script().tokens(kPlaceGear), 1000);
    Script drag;
    drag.tokens("move 196 128 wait 300 down 196 128 wait 200");
    for (int k = 0; k <= 48; ++k) {
        const double a = 3.14159265358979 / 2 - k * 2 * 3.14159265358979 / 24;
        drag.move(static_cast<int>(std::lround(196 + 16 * std::cos(a))), static_cast<int>(std::lround(112 + 16 * std::sin(a)))).waitMs(60 + 62);
    }
    r.play(drag, 1000);
    const auto& objs = f.g().puzzles.gears.objects;
    MESSAGE(std::format("after the posted drag: crank frame {}, upper rack {}, lower rack {}, buttons {} {}", objs[0].state, objs[3].col,
                        objs[2].col, objs[8].state, objs[9].state));
    // The engine ends one step short of the solution, at the session's "one button" moment (k3_04):
    // 19 steps counter-clockwise and one back; released there, the board springs back.
    CHECK(objs[0].state == 6);
    CHECK(objs[3].col == 35);
    CHECK(objs[2].col == 33);
    CHECK(objs[8].state == 1);
    CHECK(objs[9].state == 0);
    CHECK(shotDiff(f, "r2/puzzle/k3_04_one_button") == 0);
    r.play(Script().up(), 3000);
    const bool solved = f.g().puzzles.gears.objects[8].state == 1 && f.g().puzzles.gears.objects[9].state == 1;
    MESSAGE(std::format("released: {}", solved ? "solved" : "reset"));
    CHECK_FALSE(solved);
    CHECK(f.g().puzzles.gears.objects[3].col == 27);
    f.finish();
}

// ----------------------------------------------------------------------------------------------- magnets

TEST_CASE("puzzles_b: magnets level 0 solved by the recorded input") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    std::optional<GgStateFile> s;
    Replay r(f);
    r.start("r2/puzzle/k7_screen", s);
    CHECK(shotDiff(f, "r2/puzzle/k7_01_screen") == 0);
    // capture_r2.py puzzle_k7: three bars N-S in a row, unrotated.
    r.play(Script().tokens("click 28 56 wait 600 move 160 83 wait 400"));
    CHECK(f.g().puzzles.magnets.held == 0);
    const int carried = shotDiff(f, "r2/puzzle/k7_02_magnet_carried");
    MESSAGE(std::format("k7_02_magnet_carried: {} px", carried));
    r.play(Script().tokens("click 160 83 wait 700 click 28 56 wait 600 move 184 83 wait 400 click 184 83 wait 700"));
    CHECK(shotDiff(f, "r2/puzzle/k7_03_two_placed") == 0);
    r.play(Script().tokens("click 28 56 wait 600 move 208 83 wait 400 click 208 83 wait 700 move 150 140"), 3000);
    REQUIRE_FALSE(f.done());
    std::optional<GgStateFile> solved;
    solved.emplace(GgStateFile::load(*statePath("r2/puzzle/k7_solved")));
    CHECK(shotDiff(f, "r2/puzzle/k7_04_solved") == 0);
    CHECK(f.rand().calls() - r.calls == 1);
    CHECK(f.rand().seed() == solved->dsDword(dg::kRandState));
    CHECK(countSound(f.trace(), "pzPlayLoop(1,5)") == 3);  // a simulation per drop
    f.finish();
    const auto diff = fieldDifferences(f.g(), *solved,
                                       with({"g_magnetsHeldOrient", "g_magnetsHeld", "g_magnetsCounts", "DS_af56_magnetsUnused"}, kFramework));
    CHECK_MESSAGE(diff.empty(), joined(diff));
    const auto heap = magnetsHeapDifferences(f.g(), *solved);
    CHECK_MESSAGE(heap.empty(), joined(heap));
}

TEST_CASE("puzzles_b: magnets, a rotated bar attaches and like poles repel") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    // The reference session's input is not recorded: a bar at (160, 83), then a second one rotated once
    // (vertical) or twice (poles swapped) dropped at (184, 83), ROTATE pressed while carrying.
    for (const int turns : {1, 2}) {
        CAPTURE(turns);
        gggame::GameFixture f(*ggFiles);
        std::optional<GgStateFile> s;
        Replay r(f);
        r.start("r2/puzzle/k7_screen", s);
        Script script;
        script.tokens("click 28 56 wait 600 move 160 83 wait 400 click 160 83 wait 700 click 28 56 wait 600");
        for (int k = 0; k < turns; ++k) script.tokens("move 30 136 wait 300 click 30 136 wait 500");
        script.tokens("move 184 83 wait 400 click 184 83 wait 700 move 150 140");
        r.play(script, 3000);
        const std::string name = turns == 1 ? "rotated_placed" : "repelled";
        std::optional<GgStateFile> ref;
        ref.emplace(GgStateFile::load(*statePath("r2/puzzle/k7_" + name)));
        CHECK(shotDiff(f, turns == 1 ? "r2/puzzle/k7_05_rotated_placed" : "r2/puzzle/k7_06_repelled") == 0);
        CHECK(f.rand().calls() == r.calls);
        f.finish();
        const auto diff = fieldDifferences(f.g(), *ref, {"g_magnetsHeldOrient", "g_magnetsHeld", "g_magnetsCounts"});
        CHECK_MESSAGE(diff.empty(), joined(diff));
        const auto heap = magnetsHeapDifferences(f.g(), *ref);
        CHECK_MESSAGE(heap.empty(), joined(heap));
    }
}

TEST_CASE("puzzles_b: magnets, the repelled bar's slide against r2_puzzle_k7_repel.avi") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    // The distinct pictures of the table (70, 23) 169 x 105 in the AVI (DOSBox-X's 640 x 400 frames at
    // 70 fps, every second pixel) as CRC-32s of their RGB rows, and when each first shows, in PIT ticks
    // after the first change (the frame times: one AVI frame is 2.08 ticks). The first is the table with
    // the dropped bar before the simulation (PzMagnets_Simulate: substeps of Timer_Set(0, 4)); the last
    // has no hand on it: the session's pointer moved away (its time is the session's, not compared).
    struct Picture {
        uint32_t crc;
        double ticks;  // < 0: not compared
    };
    static constexpr Picture kAvi[] = {{0xABB940D4, -1},    {0x2D7ACC4A, 0.0},   {0x3DE481EA, 2.08},  {0x85522EFD, 4.16},
                                       {0x40D0D6A4, 8.31},  {0x74F8EB4E, 12.47}, {0xF309988A, 16.63}, {0xE8BAE545, 20.78},
                                       {0x8B25CA0F, 24.94}, {0xAAFCFD14, 29.09}, {0x2B366391, 37.41}, {0xE1B2BC43, 51.95},
                                       {0xBE4F236A, -1}};
    std::vector<std::pair<uint64_t, uint32_t>> seen;  // (interrupt, crc) of each new table picture
    gggame::GameFixture f(*ggFiles);
    f.runtime().setFrameSink([&seen](uint64_t interrupt, std::span<const uint32_t> rgba) {
        std::vector<uint8_t> rgb;
        rgb.reserve(169 * 105 * 3);
        for (int y = 23; y < 23 + 105; ++y)
            for (int x = 70; x < 70 + 169; ++x) {
                const uint32_t p = rgba[static_cast<size_t>(y) * 320 + static_cast<size_t>(x)];
                rgb.push_back(static_cast<uint8_t>(p));
                rgb.push_back(static_cast<uint8_t>(p >> 8));
                rgb.push_back(static_cast<uint8_t>(p >> 16));
            }
        const uint32_t crc = tmport::png::crc32(rgb);
        if (seen.empty() || seen.back().second != crc) seen.emplace_back(interrupt, crc);
    });
    std::optional<GgStateFile> s;
    Replay r(f);
    r.start("r2/puzzle/k7_screen", s);
    // The input of the rotated/repelled test: a bar at (160, 83), a second one rotated twice dropped at
    // (184, 83), then the pointer away to (150, 140).
    Script script;
    script.tokens("click 28 56 wait 600 move 160 83 wait 400 click 160 83 wait 700 click 28 56 wait 600");
    script.tokens("move 30 136 wait 300 click 30 136 wait 500 move 30 136 wait 300 click 30 136 wait 500 move 184 83 wait 400");
    script.tokens("click 184 83 wait 700 move 150 140");
    r.play(script, 1000);
    f.runtime().setFrameSink({});
    f.finish();
    // Each AVI picture in the engine's sequence, in order, at its time (within one AVI frame).
    size_t k = 0;
    std::optional<uint64_t> t0;
    int found = 0;
    for (const Picture& p : kAvi) {
        CAPTURE(found);
        while (k < seen.size() && seen[k].second != p.crc) ++k;
        REQUIRE_MESSAGE(k < seen.size(), std::format("the AVI's picture {:#010x} is not shown (or out of order)", p.crc));
        if (p.ticks >= 0) {
            if (!t0) t0 = seen[k].first;
            const double at = static_cast<double>(seen[k].first - *t0);
            CAPTURE(at);
            CHECK(std::abs(at - p.ticks) <= 2.5);
        }
        ++found;
    }
    CHECK(found == 13);
    MESSAGE(std::format("{} table pictures in the run, the AVI's 13 among them in order", seen.size()));
}

// ------------------------------------------------------------------------------------------- Hint buttons

TEST_CASE("puzzles_b: the Hint button of each puzzle and its text viewer") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    struct Case {
        int kind;
        int x, y;  // the pointer of the state's last click
        int hintX, hintY;
        int effect;
    };
    for (const Case c : {Case{2, 44, 21, 26, 13, 0x12}, Case{3, 41, 31, 23, 24, 9}, Case{4, 24, 21, 6, 13, 0x0B}, Case{7, 31, 20, 13, 13, 4}}) {
        CAPTURE(c.kind);
        gggame::GameFixture f(*ggFiles);
        std::optional<GgStateFile> s;
        Replay r(f);
        r.start(std::format("r2/puzzle/k{}_screen", c.kind), s);
        r.play(Script().move(c.x, c.y).waitMs(300).down().waitMs(120), 0);
        // The pressed Hint picture while the button is held.
        CHECK(countSound(f.trace(), std::format("pzPlay({},5)", c.effect)) == 1);
        r.play(Script().up(), 1500);
        // The viewer (TextView_Run, the UI library's) covers (0, 28) 129 x 138: all of it matches, and
        // outside it the puzzle's screen (the Hint button restored, 35 columns for machine pieces and
        // balance) on its own.
        const std::string shot = std::format("r2/puzzle/k{}_09_hint_text", c.kind);
        const int full = shotDiff(f, shot);
        const int outside = shotDiff(f, shot, {gggame::Mask{0, 28, 128, 165}, gggame::Mask{c.x - 1, c.y - 1, c.x + 16, c.y + 18}});
        MESSAGE(std::format("kind {}: {} px in all, {} outside the viewer", c.kind, full, outside));
        CHECK(full == 0);
        CHECK(outside == 0);
        f.finish();
    }
}

// ---------------------------------------------------------------------------------- the simulations (units)

TEST_CASE("puzzles_b: gears drive rules: one frame per step whatever the size, rack ranges, buttons") {
    GG_REQUIRE_DATA();
    const GgExe& exe = ggFiles->exe();
    PzGears gz(exe, ggFiles->archive());
    // The tables of DS:36e2 / DS:36ea / DS:38e8 (code_puzzles_b.md §3.6): mesh at 2, 3, 4 cells.
    CHECK(gz.frames(0) == 6);
    CHECK(gz.frames(1) == 8);
    CHECK(gz.meshes(0, 0, 0, 2));
    CHECK(gz.meshes(0, 1, 0, 3));
    CHECK(gz.meshes(1, 1, 0, 4));
    CHECK_FALSE(gz.meshes(0, 0, 0, 3));
    // The drive table DS:3902 mapped by its code addresses (§3.8).
    CHECK(gz.driveHandler(0, 0) == &PzGears::gearToGear);
    CHECK(gz.driveHandler(1, 1) == &PzGears::gearToGear);
    CHECK(gz.driveHandler(0, 4) == &PzGears::gearToHRack);
    CHECK(gz.driveHandler(0, 2) == nullptr);
    CHECK(gz.driveHandler(1, 5) == &PzGears::gearToVRack);
    CHECK(gz.driveHandler(4, 0) == &PzGears::hRackToGear);
    CHECK(gz.driveHandler(2, 4) == &PzGears::hRackToHRack);
    CHECK(gz.driveHandler(4, 2) == &PzGears::hRackToHRack);
    CHECK(gz.driveHandler(2, 7) == &PzGears::hRackToButton);
    CHECK(gz.driveHandler(4, 8) == &PzGears::hRackToButton);
    CHECK(gz.driveHandler(3, 9) == &PzGears::vRackToButton);
    CHECK(gz.driveHandler(5, 1) == &PzGears::vRackToGear);
    CHECK(gz.driveHandler(3, 5) == &PzGears::vRackToVRack);
    CHECK(gz.driveHandler(2, 0) == nullptr);
    CHECK(gz.driveHandler(2, 9) == nullptr);
    // Variant 0 with the large gear on (11, 5), as the reference: crank steps by propagation alone.
    gz.loadVariant(0);
    gz.saveHome();
    auto& o = gz.objects();
    REQUIRE(o.size() == 10);
    o[1].col = 11;
    o[1].row = 5;
    auto step = [&](int dir) {
        for (auto& x : o) x.dir = x.visited = 0;  // PzGears_Draw clears them at every redraw
        for (auto& x : o) {
            if (x.type < 2 || x.type > 5) x.savedState = x.state;
            x.savedCol = x.col;
            x.savedRow = x.row;
        }
        if (dir == -1) o[0].state = static_cast<int16_t>((o[0].state + 1) % 24);
        else o[0].state = static_cast<int16_t>((o[0].state + 23) % 24);
        gz.st().jam = 0;
        o[0].dir = static_cast<int16_t>(dir);
        o[0].type = 0;
        gz.propagate(0);
        o[0].type = 6;
    };
    const int gear0 = o[1].state;
    step(+1);  // counter-clockwise
    CHECK(o[0].state == 23);
    CHECK(o[1].dir == -1);
    CHECK(o[1].state == (gear0 + 1) % 8);  // one frame per step: no gear ratio (the large gear too)
    CHECK(o[3].col == 28);                 // the upper toothed rack, above the large gear
    CHECK(o[2].col == 31);                 // the lower toothed rack, below the crank
    CHECK(o[4].col == 24);                 // plain segments joined end to end with a toothed one
    CHECK(o[6].col == 38);
    CHECK(gz.st().jam == 0);
    for (int k = 0; k < 30; ++k) step(+1);
    // The racks disengage at 36 = 3 (11 + 1) and 34 = 3 * 11 + 1 (§8) and both buttons are down.
    CHECK(o[3].col == 36);
    CHECK(o[2].col == 34);
    CHECK(o[8].state == 1);
    CHECK(o[9].state == 1);
    CHECK(gz.allButtonsPressed());
    // Back one step: both racks move left and the lower chain lets its button go.
    step(-1);
    CHECK(o[3].col == 35);
    CHECK(o[2].col == 33);
    CHECK(o[9].state == 0);
    CHECK(o[8].state == 1);
    CHECK_FALSE(gz.allButtonsPressed());
    // The quadrants and slopes of PzGears_DragCrank around the crank centre (196, 112).
    gz.initCrankRects();
    CHECK(gz.st().crankCX == 196);
    CHECK(gz.st().crankCY == 112);
    CHECK(gz.quadrant(200, 100) == 0);
    CHECK(gz.quadrant(200, 112) == 1);
    CHECK(gz.quadrant(196, 120) == 2);
    CHECK(gz.quadrant(196, 112) == 3);
    CHECK(gz.slope(196, 50) == 0x7FFF);
    CHECK(gz.slope(206, 102) == -100);
    CHECK(gz.slope(203, 112) == 0);
}

TEST_CASE("puzzles_b: balance torques, the swing ratio and its 16-bit product") {
    GG_REQUIRE_DATA();
    PzBalance b(ggFiles->exe(), ggFiles->archive());
    // The masses of DS:25cc (code_puzzles_b.md §4.2): 1, 2, 3, 4, 5, 10.
    const int masses[6] = {1, 2, 3, 4, 5, 10};
    for (int t = 0; t < 6; ++t) CHECK(b.weightTypes()[static_cast<size_t>(t)].mass == masses[t]);
    auto& s = b.st();
    for (auto& h : s.hookWeights) h = {-1, -1, -1};
    s.hookWeights[7] = {2, -1, -1};  // variant 0: a 3 on right hook 7 (arm 3)
    CHECK(b.rightTorque() == 9);
    CHECK(b.leftTorque() == 0);
    s.tilt = -5;
    CHECK(b.compare() == 0);  // already down on the heavier side
    s.hookWeights[0] = {2, -1, -1};  // arm 5: L = 15 > R = 9
    CHECK(b.leftTorque() == 15);
    CHECK(b.compare() == 100 * 9 / 15 + 1);  // 61: (61 / 2) = 30 ticks per swing step
    s.rightHeavy = 0;
    s.hookWeights[0] = {-1, -1, -1};
    s.hookWeights[2] = {2, -1, -1};  // arm 3: L = R = 9
    CHECK(b.compare() == -1);        // come to level
    s.tilt = 0;
    CHECK(b.compare() == 0);
    s.hookWeights[2] = {1, -1, -1};  // L = 6 < R = 9 at level: the right side heavier
    CHECK(b.compare() == 100 * 6 / 9 + 1);
    CHECK(s.rightHeavy == 1);
    s.tilt = 5;
    CHECK(b.compare() == 100 * 6 / 9 + 1);  // left down but lighter: swing over
    // Three weights per hook at most; the stock and hooks of variant 0 from D 0x211b.
    s.hookWeights[0] = {5, 5, 5};  // 3 x 10 x 5 = 150
    s.hookWeights[1] = {5, 5, 5};  // + 120
    s.hookWeights[2] = {5, 5, 5};  // + 90
    s.hookWeights[3] = {5, 5, 5};  // + 60
    for (int h = 5; h < 10; ++h) s.hookWeights[static_cast<size_t>(h)] = {5, 5, 5};  // R = 30 * 15 = 450
    CHECK(b.leftTorque() == 420);
    CHECK(b.rightTorque() == 450);
    // 100 * 420 = 42000 wraps to -23536 in the 16-bit IMUL: -23536 / 450 + 1 = -51 (left down, lighter).
    s.tilt = 5;
    CHECK(b.compare() == static_cast<int16_t>(42000) / 450 + 1);
}

TEST_CASE("puzzles_b: magnets square root, cube, pole forces, attachment and the step thresholds") {
    GG_REQUIRE_DATA();
    PzMagnets m(ggFiles->exe(), ggFiles->archive());
    // PzMagnets_ISqrt: 1 below 4, floor(sqrt(n)) up to 131 073 except n = 65 540 and 65 541, where the
    // first bounds' low words differ by 0x8000 and the 16-bit absolute value (CWD, XOR, SUB) is -32768:
    // the search ends at once with 2 (a correction of §5.7, whose emulation took a Python abs). Those
    // distances occur on the table (e.g. dx 254, dy 32). Misses again from 131 074.
    for (uint32_t n = 0; n < 4; ++n) CHECK(m.isqrt(n) == 1);
    std::vector<uint32_t> misses;
    for (uint32_t n = 4; n < 131074; ++n)
        if (m.isqrt(n) != static_cast<int>(std::floor(std::sqrt(static_cast<double>(n))))) misses.push_back(n);
    CHECK(misses == std::vector<uint32_t>{65540, 65541});
    CHECK(m.isqrt(65540) == 2);
    CHECK(m.isqrt(65542) == 256);
    CHECK(m.isqrt(131074) != 362);
    CHECK(m.cube(17) == 4913);
    CHECK(m.cube(18) == 5832);
    CHECK(m.cube(40) == static_cast<int16_t>(64000));
    m.loadPictures();
    auto& s = m.st();
    for (auto& x : s.slots) x = MagnetRec{};
    // Bars: orientation 0 has its N poles (0..2) on the left (§5.2); poles exist at both ends.
    int nPoles = 0;
    for (int p = 0; p < 6; ++p)
        if (m.pole(0, p).first != -1) ++nPoles;
    CHECK(nPoles == 6);
    int horseshoe = 0;
    for (int p = 0; p < 6; ++p)
        if (m.pole(4, p).first != -1) ++horseshoe;
    CHECK(horseshoe == 4);  // poles 2 and 5 absent
    // Two bars end to end N-S | S-N with a 6 px gap: like poles repel, the left bar is pushed left.
    s.slots[0] = MagnetRec{0, 0, 120, 80};
    const int w = m.magnetSize(0).first;
    s.slots[1] = MagnetRec{0, 2, static_cast<int16_t>(120 + w + 6), 80};
    m.forces(0);
    m.forces(1);
    CHECK(s.slots[0].fx < 0);
    CHECK(s.slots[1].fx > 0);
    CHECK(s.slots[0].fx == -s.slots[1].fx);  // symmetric
    CHECK(s.slots[0].attached == 0);
    // N-S | N-S: unlike poles 1 px apart attach; the accumulators are zeroed.
    for (int gap = 0; gap < 40; ++gap) {
        s.slots[0] = MagnetRec{0, 0, 120, 80};
        s.slots[1] = MagnetRec{0, 0, static_cast<int16_t>(120 + gap), 80};
        m.forces(0);
        if (s.slots[0].attached) {
            CHECK(s.slots[0].fx == 0);
            CHECK(s.slots[0].fy == 0);
            MESSAGE(std::format("bars attach at a gap of {} px between their left edges", gap));
            break;
        }
    }
    // The step thresholds: horizontal 0x200 / 0x40 (odd substeps) / any (substep 1); vertical 8 times lower.
    for (auto& x : s.slots) x = MagnetRec{};
    s.slots[0] = MagnetRec{0, 1, 150, 60};  // a vertical bar alone in the middle of the table
    auto moves = [&](int16_t fx, int16_t fy, int substep) {
        s.slots[0].x = 150;
        s.slots[0].y = 60;
        s.slots[0].fx = fx;
        s.slots[0].fy = fy;
        s.slots[0].attached = 0;
        m.step(substep);
        return std::pair<int, int>{s.slots[0].x - 150, s.slots[0].y - 60};
    };
    CHECK(moves(0x201, 0, 0).first == 1);
    CHECK(moves(-0x201, 0, 2).first == -1);
    CHECK(moves(0x41, 0, 0).first == 0);
    CHECK(moves(0x41, 0, 3).first == 1);
    CHECK(moves(1, 0, 1).first == 1);
    CHECK(moves(1, 0, 3).first == 0);
    CHECK(moves(0, 0x41, 0).second == 1);
    CHECK(moves(0, 0x11, 3).second == 1);
    CHECK(moves(0, 0x11, 2).second == 0);
    CHECK(moves(0, -1, 1).second == -1);
}

TEST_CASE("puzzles_b: machine pieces zone priorities and groups") {
    GG_REQUIRE_DATA();
    PzPieces p(ggFiles->exe(), ggFiles->archive());
    // Every variant: 2..5 pieces (members - 1), each smaller than the 219 x 142 work area.
    for (int v = 0; v < 20; ++v) {
        p.loadVariant(v);
        p.loadResources(v);
        CHECK((p.pieceCount() >= 2 && p.pieceCount() <= 5));
        CHECK(p.st().inBox == p.pieceCount());
        for (int i = 0; i < p.pieceCount(); ++i) {
            CHECK(p.st().pieces[static_cast<size_t>(i)].group == i);
            CHECK(p.st().pieces[static_cast<size_t>(i)].w * p.st().pieces[static_cast<size_t>(i)].h < 32768);
        }
    }
    // Groups take the smaller number; equal groups rename nothing (the stale CX, §2.7).
    p.loadVariant(2);
    p.loadResources(2);
    auto& s = p.st();
    s.held = 1;
    s.group = s.pieces[1].group;
    p.mergeGroups(2);
    CHECK(s.pieces[2].group == 1);
    CHECK(s.group == 1);
    s.held = 0;
    s.group = 0;
    p.mergeGroups(2);
    CHECK(s.pieces[1].group == 0);
    CHECK(s.pieces[2].group == 0);
    s.held = 2;
    s.group = 0;
    p.mergeGroups(1);  // equal: nothing changes
    CHECK(s.pieces[0].group == 0);
    CHECK(s.pieces[1].group == 0);
    CHECK(s.pieces[2].group == 0);
    // The hint text: the format's %s replaced by the machine's name of the variant.
    const auto text = p.hintText(2);
    CHECK(text.size() > 10);
    CHECK(std::find(text.begin(), text.end(), static_cast<uint8_t>('%')) == text.end());
}
