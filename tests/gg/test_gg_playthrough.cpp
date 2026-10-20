// Wave 3 integration: the game played through the app (gameMain on ggport's runtime with the real sound
// facade over GgAudio and PLAYERS.DAT in a scratch preference folder), with scripted keys and mouse in PIT
// ticks, from a fresh boot and from the round 2 states (--gg-load-state): the w0 tour from the attract
// through the sign-in, the hub, the blueprint, the workshop, the tube, the warehouse with its chimps, a
// puzzle and the room behind its door back to the hub; the eight puzzles from their doors and resumed in
// their screens; the races to the post-race menu and its ways on (the hub, the next level, the warehouse);
// the 15th race with the final movie and the exit; Save & Exit and the resume in a new boot; the tube, the
// workshop's belt, a part picked up and a banana thrown. Every picture is compared with the captures as it
// is shown (re/gg/docs/REFERENCE_CAPTURES.md); only differing-pixel counts, ticks, states and hashes here.
#include <doctest/doctest.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gg/app/gg_app.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/game/flow.h"
#include "gg/save/gg_state_file.h"
#include "gg_test_data.h"
#include "mods/png_codec.h"
#include "util/file_util.h"
#include "util/hash.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

constexpr double kTicksPerMs = 3579545.0 / 3.0 / 8192.0 / 1000.0;  // PIT ticks per ms

// An inclusive rectangle of the 320 x 200 screen left out of a comparison.
struct Mask {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// A capture the run's pictures are compared with: the fewest differing pixels of any picture, and when.
struct Shot {
    explicit Shot(std::string file, std::vector<Mask> m = {}) : png(std::move(file)), masks(std::move(m)) {}
    std::string png;  // relative to GG_CAPTURES
    std::vector<Mask> masks;
    int best = INT_MAX;
    uint64_t at = 0;
    std::vector<uint8_t> rgb;  // the capture as 320 x 200 RGB
};

std::vector<Shot> shots(std::initializer_list<const char*> names, std::vector<Mask> masks = {}) {
    std::vector<Shot> out;
    for (const char* n : names) out.emplace_back(n, masks);
    return out;
}

// A capture as 320 x 200 RGB (640 x 400 shots are 2 x 2 blocks).
std::vector<uint8_t> captureRgb(const std::filesystem::path& file) {
    const auto bytes = readWholeFile(file);
    REQUIRE(bytes);
    const auto png = mods::decodePng(*bytes);
    const int step = png.width == 640 ? 2 : 1;
    REQUIRE(png.width == 320 * step);
    REQUIRE(png.height == 200 * step);
    std::vector<uint8_t> rgb(320 * 200 * 3);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x)
            for (int k = 0; k < 3; ++k)
                rgb[(static_cast<size_t>(y) * 320 + static_cast<size_t>(x)) * 3 + static_cast<size_t>(k)] =
                    png.rgba[(static_cast<size_t>(y * step) * static_cast<size_t>(png.width) + static_cast<size_t>(x * step)) * 4 +
                             static_cast<size_t>(k)];
    return rgb;
}

int differing(std::span<const uint32_t> frame, const Shot& s) {
    int n = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            const size_t i = static_cast<size_t>(y) * 320 + static_cast<size_t>(x);
            const uint32_t p = frame[i];
            const uint8_t* c = &s.rgb[i * 3];
            if ((p & 0xFF) == c[0] && ((p >> 8) & 0xFF) == c[1] && ((p >> 16) & 0xFF) == c[2]) continue;
            if (std::any_of(s.masks.begin(), s.masks.end(), [&](const Mask& m) { return x >= m.x0 && x <= m.x1 && y >= m.y0 && y <= m.y1; }))
                continue;
            ++n;
        }
    return n;
}

// One row of the --gg-trace file (game/trace.h).
struct Row {
    uint64_t interrupts = 0;
    int state = 0, displayRoom = 0, x = 0, y = 0, seq = 0;
    uint64_t randCalls = 0;
};

std::vector<Row> readTrace(const std::filesystem::path& file) {
    std::vector<Row> rows;
    std::ifstream in(file);
    std::string line;
    bool header = false;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!header) {
            header = true;
            continue;
        }
        std::vector<std::string> c;
        std::string cell;
        std::istringstream ls(line);
        while (std::getline(ls, cell, ',')) c.push_back(cell);
        if (c.size() < 14) continue;
        Row r;
        r.interrupts = std::stoull(c[2]);
        r.state = std::stoi(c[4]);
        r.displayRoom = std::stoi(c[5]);
        r.x = std::stoi(c[7]);
        r.y = std::stoi(c[8]);
        r.seq = std::stoi(c[9]);
        r.randCalls = std::stoull(c[10]);
        rows.push_back(r);
    }
    return rows;
}

// The g_state of every frame boundary, consecutive repeats dropped (with the displayed room for 7).
std::string stateSequence(const std::vector<Row>& rows) {
    std::string out;
    std::string last;
    for (const Row& r : rows) {
        const std::string s = r.state == 7 ? std::format("7.{}", r.displayRoom) : std::to_string(r.state);
        if (s == last) continue;
        out += (out.empty() ? "" : " ") + s;
        last = s;
    }
    return out;
}

struct Played {
    int code = -1;
    bool returned = false;  // gameMain returned (exit), not the run limit
    uint64_t end = 0;
    std::vector<Row> rows;
    std::filesystem::path pref;
};

struct RunSpec {
    RunSpec(std::string n, uint64_t t, std::vector<std::string> s, std::string l = {})
        : name(std::move(n)), ticks(t), script(std::move(s)), load(std::move(l)) {}
    std::string name;
    uint64_t ticks = 0;
    std::vector<std::string> script;      // dos::ScriptedInput items, absolute PIT ticks of the run
    std::string load;                     // a state under savestates/ (without .sav), or empty
    std::optional<std::filesystem::path> players;  // a PLAYERS.DAT for the preference folder
    std::vector<std::string> extra;
};

// Runs the game through the app and compares every picture shown with `want`.
Played play(const GgFiles& files, const RunSpec& spec, std::vector<Shot>& want) {
    Played p;
    p.pref = testdata::scratchDir(("gg_play_" + spec.name).c_str());
    if (spec.players) {
        std::error_code ec;
        std::filesystem::copy_file(*spec.players, p.pref / "players.dat", std::filesystem::copy_options::overwrite_existing, ec);
        REQUIRE_FALSE(ec);
    }
    const auto trace = p.pref / "trace.csv";
    std::vector<std::string> args = {"--data", pathToUtf8(files.folder()), "--pref-dir", pathToUtf8(p.pref), "--headless", "--seed", "10703",
                                     "--ticks", std::to_string(spec.ticks), "--gg-trace", pathToUtf8(trace)};
    std::string script;
    for (const std::string& s : spec.script) script += (script.empty() ? "" : ", ") + s;
    if (!script.empty()) {
        args.push_back("--keys");
        args.push_back(script);
    }
    if (!spec.load.empty()) {
        const auto sav = testdata::ggCapture("savestates/" + spec.load + ".sav");
        REQUIRE(sav);
        args.push_back("--gg-load-state");
        args.push_back(pathToUtf8(*sav));
    }
    for (const std::string& a : spec.extra) args.push_back(a);
    for (Shot& s : want) {
        const auto png = testdata::ggCapture(s.png);
        REQUIRE_MESSAGE(png, s.png);
        s.rgb = captureRgb(*png);
    }
    uint64_t previous = 0;
    const GgEntry entry = [&](GgContext& ctx) {
        ctx.runtime.setFrameSink([&](uint64_t tick, std::span<const uint32_t> rgba) {
            const uint64_t h = fnv1a64(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(rgba.data()), rgba.size() * 4));
            if (h == previous) return;
            previous = h;
            for (Shot& s : want) {
                if (s.best == 0) continue;
                const int d = differing(rgba, s);
                if (d < s.best) {
                    s.best = d;
                    s.at = tick;
                }
            }
        });
        const int code = gameMain(ctx);
        p.returned = true;
        p.end = ctx.runtime.clock().interrupts();
        return code;
    };
    p.code = runGgApp(args, entry);
    p.rows = readTrace(trace);
    return p;
}

void checkShots(const std::vector<Shot>& want) {
    for (const Shot& s : want) {
        INFO(s.png << " (closest picture at PIT tick " << s.at << ")");
        CHECK(s.best == 0);
    }
}

// capture_r2.py's MOUSEKEY tokens ("click X Y", "move X Y", "wait MS", "down [X Y]", "up [X Y]", "drag X1 Y1
// X2 Y2 MS") from PIT tick `t` (updated to the end) as tests/gg/test_gg_puzzles_a.cpp converts them: a click
// is pressed at its tick and released one tick later.
std::vector<std::string> r2Mouse(std::string_view tokens, double& t) {
    std::istringstream in{std::string(tokens)};
    std::string op;
    std::vector<std::string> out;
    const auto emit = [&](const std::string& e) { out.push_back(std::format("{}:{}", static_cast<long long>(std::floor(t)), e)); };
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
            const double end = t + ms * kTicksPerMs, t0 = t;
            const int steps = std::max(1, static_cast<int>((end - t) / 2));
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

// The same tokens timed as tools/tmctl.py posts them (tests/gg/test_gg_puzzles_b.cpp Script): a command
// acts 50 ms after its start and takes 62 ms to post; a click holds the button 120 ms.
class Mousekey {
public:
    explicit Mousekey(double t0) : t_(t0) {}
    double time() const { return t_; }
    const std::vector<std::string>& items() const { return out_; }
    Mousekey& move(int x, int y) { return command({std::format("move:{}:{}", x, y)}); }
    Mousekey& down() { return command({"buttons:1"}); }
    Mousekey& up() { return command({"buttons:0"}); }
    Mousekey& downAt(int x, int y) { return command({std::format("move:{}:{}", x, y), "buttons:1"}); }
    Mousekey& upAt(int x, int y) { return command({std::format("move:{}:{}", x, y), "buttons:0"}); }
    Mousekey& waitMs(double ms) {
        t_ += ms * kTicksPerMs;
        return *this;
    }
    Mousekey& click(int x, int y) { return downAt(x, y).waitMs(120).upAt(x, y); }
    Mousekey& tokens(std::string_view text) {
        std::istringstream in{std::string(text)};
        std::string op;
        while (in >> op) {
            int x = 0, y = 0;
            if (op == "wait") {
                double ms = 0;
                in >> ms;
                waitMs(ms);
            } else if (op == "move" || op == "click") {
                in >> x >> y;
                if (op == "move") move(x, y);
                else click(x, y);
            }
        }
        return *this;
    }

private:
    Mousekey& command(std::initializer_list<std::string> events) {
        const auto at = static_cast<long long>(std::floor(t_ + 50 * kTicksPerMs));
        for (const std::string& e : events) out_.push_back(std::format("{}:{}", at, e));
        t_ += 62 * kTicksPerMs;
        return *this;
    }
    double t_;
    std::vector<std::string> out_;
};

void append(std::vector<std::string>& to, const std::vector<std::string>& items) { to.insert(to.end(), items.begin(), items.end()); }

std::optional<std::vector<uint8_t>> readCapture(const char* relative) {
    const auto p = testdata::ggCapture(relative);
    return p ? readWholeFile(*p) : std::nullopt;
}

int differingBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) return -1;
    int n = 0;
    for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
    return n;
}

// The puzzles' recorded solutions of variant 0 (tools/gg/capture_r2.py; gears as the solved state holds
// it, tests/gg/test_gg_puzzles_b.cpp).
std::string k5Pull(int dx) {
    std::string s = "move 283 141 wait 300 down 283 141 wait 300";
    for (int i = 1; i <= dx; ++i) s += std::format(" move {} {} wait 100", 283 + i, 141 + static_cast<int>(std::lround(i * 8.0 / 14)));
    return s + " wait 500";
}

// The script of puzzle kind k from PIT tick t0 (updated to its end).
std::vector<std::string> puzzleSolution(int k, double& t) {
    switch (k) {
        case 0:
            return r2Mouse("click 32 145 wait 500 move 136 125 wait 400 click 136 125 wait 400 move 160 100 wait 200 move 192 58 wait 500 "
                           "click 192 58 wait 3000",
                           t);
        case 1:
            return r2Mouse("click 24 88 wait 600 move 150 110 wait 300 move 178 120 wait 500 click 178 120 wait 700 move 120 60 wait 300 "
                           "click 28 156 wait 3000",
                           t);
        case 5: return r2Mouse("drag 37 97 228 102 600 wait 1000 " + k5Pull(14) + " up wait 5000", t);
        case 6:
            return r2Mouse("click 24 72 wait 600 move 181 36 wait 300 click 181 36 wait 500 move 219 103 wait 400 "
                           "click 219 103 wait 600 move 181 70 wait 300 click 181 70 wait 500 move 219 36 wait 400 "
                           "click 219 36 wait 600 move 181 104 wait 300 click 181 104 wait 500 move 219 70 wait 400 "
                           "click 219 70 wait 600 move 120 140 wait 300 click 26 137 wait 10000",
                           t);
        default: break;
    }
    Mousekey m(t);
    if (k == 2)
        m.tokens("click 47 122 wait 600 move 150 80 wait 400 click 150 80 wait 600 click 47 122 wait 600 move 146 59 wait 400 "
                 "click 146 59 wait 600 move 200 140 wait 3000");
    if (k == 4) m.tokens("click 16 113 wait 600 move 117 50 wait 400 click 117 50 wait 1500 move 60 150 wait 3000");
    if (k == 7)
        m.tokens("click 28 56 wait 600 move 160 83 wait 400 click 160 83 wait 700 click 28 56 wait 600 move 184 83 wait 400 "
                 "click 184 83 wait 700 click 28 56 wait 600 move 208 83 wait 400 click 208 83 wait 700 move 150 140 wait 3000");
    if (k == 3) {
        // The gear onto the peg, then the crank: one pointer event per step onto the handle of the frame
        // wanted (the game warps the pointer there after every step), 19 counter-clockwise, one back, one on.
        m.tokens("click 56 104 wait 600 move 174 67 wait 400 click 174 67 wait 800 move 250 140 wait 1000");
        const auto sav = testdata::ggCapture("savestates/r2/puzzle/k3_gear_placed.sav");
        REQUIRE(sav);
        const GgStateFile s = GgStateFile::load(*sav);
        const auto centre = [&](int frame) {
            const auto r = [&](int k2) {
                return static_cast<int16_t>(s.dsWord(static_cast<uint16_t>(dg::kGearsCrankRect + 8 * (frame + 1) + 2 * k2)));
            };
            return std::pair<int, int>{r(0) + (r(2) - r(0)) / 2, r(1) + (r(3) - r(1)) / 2};
        };
        m.move(196, 128).waitMs(300).downAt(196, 128).waitMs(200);
        std::vector<int> wanted(19, +1);
        wanted.push_back(-1);
        wanted.push_back(+1);
        int frame = 0;
        for (size_t i = 0; i + 1 < wanted.size(); ++i) {
            frame = wanted[i] == +1 ? (frame + 23) % 24 : (frame + 1) % 24;
            const auto [x, y] = centre(frame);
            m.move(x, y).waitMs(120);
        }
        m.waitMs(500);
        frame = (frame + 23) % 24;
        const auto [x, y] = centre(frame);
        m.move(x, y).waitMs(120).waitMs(500).up().waitMs(3000);
    }
    t = m.time();
    return m.items();
}

constexpr const char* kSolvedShot[8] = {"r2/puzzle/k0_04_solved.png", "r2/puzzle/k1_03_solved.png", "r2/puzzle/k2_03_solved.png",
                                        "r2/puzzle/k3_05_solved.png", "r2/puzzle/k4_03_solved.png", "r2/puzzle/k5_03_solved.png",
                                        "r2/puzzle/k6_04_solved.png", "r2/puzzle/k7_04_solved.png"};

// The w0 tour (REFERENCE_CAPTURES §3.1) in this engine's PIT ticks (seed 0x29CF): Space on story page 3,
// "ada", Start Game, With Chimps, the Aircraft building, its hint, the Decal page, Done twice, the workshop
// hint, Up at the tube, the warehouse hint, right for 2 s, Up at the X door 280 ticks later than w0 (the
// chimps' rand() calls then deal w0's machine pieces variant 2), the T-handle solved as capture_tour.py
// solves it, Enter through the door, the pointer on a part, File > Return to Technology Center > Yes.
std::vector<std::string> tourScript(bool full) {
    std::vector<std::string> s = {"12000:tap:space", "12300:tap:a", "12310:tap:d", "12320:tap:a", "12400:click:164:180", "12500:click:93:137"};
    if (!full) return s;
    append(s, {"12900:move:100:50", "13100:click:62:85", "13450:tap:enter", "13600:click:53:51", "13750:click:269:150", "13900:click:269:150",
               "14700:tap:enter", "14850:down:up", "14894:up:up", "15150:tap:enter", "15200:down:right", "15491:up:right", "15880:down:up",
               "15924:up:up", "16100:tap:enter"});
    double t = 16200;
    append(s, r2Mouse("drag 47 122 200 80 600 wait 1000 move 200 30 wait 500 click 200 30 wait 1000", t));
    append(s, r2Mouse("click 47 122 wait 800 move 150 60 wait 800 click 120 40 wait 800 click 47 122 wait 800 move 260 40 wait 800 "
                      "click 190 24 wait 1000",
                      t));
    append(s, r2Mouse("click 125 48 wait 600 move 150 90 wait 600 click 200 126 wait 3500", t));
    append(s, {"18100:tap:enter", "18500:move:290:128", "18800:click:14:4", "18950:click:107:27", "19100:click:221:133"});
    return s;
}

}  // namespace

TEST_CASE("playthrough: the w0 tour from the boot to the hub, with the new player's PLAYERS.DAT (app)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    auto want = shots({"w0/01_title.png", "w0/02_story1.png", "w0/03_signin.png", "w0/04_signin_name.png", "w0/05_chimps_dialog.png",
                       "w0/06_hub.png"});
    RunSpec spec{"tour_hub", 12900, tourScript(false)};
    const Played p = play(*ggFiles, spec, want);
    CHECK(p.code == 0);
    checkShots(want);
    CHECK(stateSequence(p.rows) == "1 3");  // the sign-in (2) is unpaced: no frame boundary
    // Ada With Chimps written as the original wrote her: slot 98 of the shipped index (8 bytes).
    const auto mine = readWholeFile(p.pref / "players.dat");
    const auto ref = readCapture("savestates/w0/06_hub.players");
    REQUIRE(mine);
    REQUIRE(ref);
    CHECK(differingBytes(*mine, *ref) == 0);
}

TEST_CASE("playthrough: the w0 tour through the building, a puzzle and the room behind its door back to the hub (app)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    // The chimp walks by the real pace in the shots (21..23: the bottom-left strip where it is).
    const std::vector<Mask> chimp = {{0, 120, 120, 170}};
    auto want = shots({"w0/01_title.png",
                       "w0/02_story1.png",
                       "w0/03_signin.png",
                       "w0/04_signin_name.png",
                       "w0/05_chimps_dialog.png",
                       "w0/06_hub.png",
                       "w0/07_hub_hover_aircraft.png",
                       "w0/08_building_hint.png",
                       "w0/09_blueprint.png",
                       "w0/10_blueprint_part_info.png",
                       "w0/11_workshop_hint.png",
                       "w0/12_workshop.png",
                       "w0/16_puzzle_hint.png",
                       "w0/17_puzzle.png",
                       "w0/18_puzzle_piece1_placed.png",
                       "w0/19_puzzle_t_handle.png",
                       "w0/20_puzzle_solved.png",
                       "w0/24_return_dialog.png",
                       "w0/25_hub_again.png"});
    for (Shot& s : shots({"w0/21_room_behind_door.png", "w0/22_room_hover_part.png", "w0/23_file_menu.png"}, chimp)) want.push_back(s);
    const auto dump = testdata::scratchDir("gg_play_tour_puzzle") / "puzzle.dgroup";
    RunSpec spec{"tour", 19800, tourScript(true)};
    spec.extra = {"--gg-dump-state", "16390", pathToUtf8(dump)};
    const Played p = play(*ggFiles, spec, want);
    CHECK(p.code == 0);
    checkShots(want);
    // The hub, the workshop (6; 4 and 5 have no frame), room 1, the puzzle (9, unpaced), room 6 behind the
    // door, the hub again.
    CHECK(stateSequence(p.rows) == "1 3 6 7.1 7.6 3");
    // The building's set-up: 3925 rand() calls from the seed (code_world.md §4.4), as w0's.
    const auto at6 = std::find_if(p.rows.begin(), p.rows.end(), [](const Row& r) { return r.state == 6; });
    REQUIRE(at6 != p.rows.end());
    CHECK(at6->randCalls == 3925);
    // The puzzle dealt behind the door: machine pieces, variant 2 (w0/17's).
    const auto image = readWholeFile(dump);
    REQUIRE(image);
    const auto word = [&](uint16_t off) { return static_cast<int16_t>((*image)[off] | ((*image)[off + 1] << 8)); };
    CHECK(word(dg::kState) == 9);
    CHECK(word(0xB13A) == 2);  // g_puzzleKind
    CHECK(word(0xB138) == 2);  // g_puzzleVariant
    // Return to Technology Center saved the building (Players_SaveCurrent): w0/25's file but for the two
    // chimps' fields in the Aircraft block (the actors' part, block +0x492: they walked at another pace).
    const auto mine = readWholeFile(p.pref / "players.dat");
    const auto ref = readCapture("savestates/w0/25_hub_again.players");
    REQUIRE(mine);
    REQUIRE(ref);
    REQUIRE(mine->size() == ref->size());
    const size_t block = 0x8A0 + 98 * 0x16BD + 0x27E + 1 * 0x6BA;
    size_t outside = 0, inActors = 0;
    for (size_t i = 0; i < mine->size(); ++i) {
        if ((*mine)[i] == (*ref)[i]) continue;
        ((i >= block + 0x492 && i < block + 0x6BA) ? inActors : outside) += 1;
    }
    CHECK(outside == 0);
    CHECK(inActors <= 46 * 2);  // at most the two chimps' records
    MESSAGE("PLAYERS.DAT after the tour: " << inActors << " bytes differ from w0/25's, all in the chimps' records");
}

TEST_CASE("playthrough: the eight puzzles from their door states: hint, screen, solved, through the door (app)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    for (int k = 0; k < 8; ++k) {
        CAPTURE(k);
        RunSpec spec{std::format("door_k{}", k), 0, {"200:tap:enter"}, std::format("r2/puzzle/k{}_door", k)};
        double t = 300;
        append(spec.script, puzzleSolution(k, t));
        const auto leave = static_cast<uint64_t>(t) + 50;
        spec.script.push_back(std::format("{}:tap:enter", leave));
        spec.ticks = leave + 900;
        std::vector<Shot> want = shots({});
        want.emplace_back(std::format("r2/puzzle/k{}_00_hint.png", k));
        want.emplace_back(std::format("r2/puzzle/k{}_01_screen.png", k));
        want.emplace_back(kSolvedShot[k]);
        const Played p = play(*ggFiles, spec, want);
        CHECK(p.code == 0);
        checkShots(want);
        // Room 1 (the door, Up held in the state), the puzzle (unpaced), room 6 behind the opened door
        // (the state's held Up then walks the player on through the doors).
        const std::string seq = stateSequence(p.rows);
        CHECK(seq.starts_with("7.1 "));
        CHECK(seq.find("7.6") != std::string::npos);
    }
}

TEST_CASE("playthrough: the eight puzzles resumed in their screen states: solved, through the door (app)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    // The resume loads the puzzle's effect bank (else PzSnd_WaitEnd after the solve waits on the room's
    // music) and puts the state's palette back after the modules' picture loads.
    for (int k = 0; k < 8; ++k) {
        CAPTURE(k);
        RunSpec spec{std::format("screen_k{}", k), 0, {}, std::format("r2/puzzle/k{}_screen", k)};
        double t = 28;  // the load ends at PIT tick 8; the module tests' scripts start 20 after it
        append(spec.script, puzzleSolution(k, t));
        const auto leave = static_cast<uint64_t>(t) + 50;
        spec.script.push_back(std::format("{}:tap:enter", leave));
        spec.ticks = leave + 400;
        std::vector<Shot> want = shots({});
        want.emplace_back(kSolvedShot[k]);
        const Played p = play(*ggFiles, spec, want);
        CHECK(p.code == 0);
        checkShots(want);
        CHECK(stateSequence(p.rows).find("7.6") != std::string::npos);
    }
}

TEST_CASE("playthrough: the races, the results and the post-race menu's ways on (app)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    struct Case {
        const char* name;
        const char* load;
        uint64_t enter, choice;
        const char* click;  // the post-race menu's button
        uint64_t ticks;
        std::vector<const char*> shots;
        const char* after;  // the states after the result screen (the post-race menu, 13, is unpaced: no frame)
    };
    // capture_r2.py race(): the race-track door (305, 135), Enter at the result's box. The menu: the next
    // race (93, 89) -> state 4 and the next level's blueprint; the hub (221, 89); the warehouse (157, 89)
    // after a loss -> the workshop again.
    const Case cases[] = {
        {"altenergy", "r2/altenergy/11_vehicle_built", 16100, 16600, "93:89", 19000,
         {"r2/altenergy/12_race_start.png", "r2/altenergy/13_race_end.png", "r2/altenergy/14_won_postrace.png"}, "6"},
        {"automotive", "r2/automotive/11_vehicle_built", 14400, 14900, "221:89", 17500,
         {"r2/automotive/12_race_start.png", "r2/automotive/13_race_end.png", "r2/automotive/14_won_postrace.png"}, "3"},
        {"aircraft_won", "r2/aircraft/55_vehicle_narrow", 16800, 17300, "221:89", 18500,
         {"r2/aircraft/56_won_rankup.png", "r2/aircraft/57_won_postrace.png"}, "3"},
        {"aircraft_lost", "r2/aircraft/36_vehicle_built", 17100, 17600, "157:89", 19000,
         {"r2/aircraft/40_race_start.png", "r2/aircraft/41_race_mid.png", "r2/aircraft/43_result_lost.png"}, "6"},
    };
    for (const Case& c : cases) {
        const std::string name = c.name;
        INFO(name);
        RunSpec spec{std::string("race_") + c.name, c.ticks,
                     {"10:click:305:135", std::format("{}:tap:enter", c.enter), std::format("{}:click:{}", c.choice, c.click)}, c.load};
        std::vector<Shot> want;
        for (const char* s : c.shots) want.emplace_back(s);
        // The next level's blueprint (r2/altenergy/15): the pointer left where Next Race was clicked.
        if (std::string_view(c.name) == "altenergy") want.emplace_back("r2/altenergy/15_next_level.png", std::vector<Mask>{{88, 72, 120, 100}});
        const Played p = play(*ggFiles, spec, want);
        CHECK(p.code == 0);
        checkShots(want);
        CHECK(stateSequence(p.rows) == std::format("6 11 12 {}", c.after));
    }
}

TEST_CASE("playthrough: the 15th race, the rank-up, the final movie, the key and the exit (app)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    auto want = shots({"r2/movie/01_rankup_scientist.png", "r2/movie/02_movie_start.png", "r2/movie/03_movie_10s.png",
                       "r2/movie/04_movie_20s.png", "r2/movie/05_movie_35s.png", "r2/movie/06_movie_50s.png"});
    RunSpec spec{"movie", 30000, {"10:click:305:135", "16000:tap:enter", "27000:tap:space"}, "r2/movie/00_vehicle_ready_total14"};
    const Played p = play(*ggFiles, spec, want);
    checkShots(want);
    // The key after the movie: the player saved and exit(0) to DOS (State_15_FinalMovie), not the run limit.
    CHECK(p.code == 0);
    CHECK(p.returned);
    CHECK(p.end >= 27000);
    CHECK(p.end < 27100);
    CHECK(stateSequence(p.rows) == "6 11 12 15");
    const auto mine = readWholeFile(p.pref / "players.dat");
    const auto ref = readCapture("savestates/r2/movie/07_after_key.players");
    REQUIRE(mine);
    REQUIRE(ref);
    CHECK(differingBytes(*mine, *ref) == 0);
}

TEST_CASE("playthrough: Save & Exit in a building, then a new boot resumes it in the same room (app)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    // capture_r2.py resume: File (14, 4), Save & Exit (100, 38), 'y'.
    auto first = shots({"r2/resume/02_file_menu.png", "r2/resume/03_after_save_exit.png"});
    RunSpec save{"save_exit", 1500, {"300:click:14:4", "450:click:100:38", "600:tap:y"}, "r2/aircraft/26_all_parts"};
    const Played a = play(*ggFiles, save, first);
    checkShots(first);
    CHECK(a.code == 0);
    CHECK(a.returned);  // exit(0) after the save
    CHECK(a.end < 700);
    const auto written = readWholeFile(a.pref / "players.dat");
    const auto ref = readCapture("savestates/r2/resume/04_after_save_exit.players");
    REQUIRE(written);
    REQUIRE(ref);
    CHECK(differingBytes(*written, *ref) == 0);  // the 295 bytes the original wrote
    // A new boot with that file: Space in the attract, Bob from the list, Start Game, the Aircraft
    // building: room 4, where he was, without a question. The shot 08 shows him facing front: the idle bit
    // (0x111C ticks without a held key after the Space) came before it.
    auto second = shots({"r2/resume/05_signin.png", "r2/resume/06_bob_selected.png", "r2/resume/07_hub.png", "r2/resume/08_aircraft_resumed.png"});
    RunSpec resume{"resume", 6500, {"1000:tap:space", "1300:move:160:100", "1400:click:100:62", "1550:click:164:180", "1900:click:50:50"}};
    resume.players = a.pref / "players.dat";
    const Played b = play(*ggFiles, resume, second);
    CHECK(b.code == 0);
    checkShots(second);
    CHECK(stateSequence(b.rows) == "1 3 7.4");
    const auto in4 = std::find_if(b.rows.begin(), b.rows.end(), [](const Row& r) { return r.state == 7; });
    REQUIRE(in4 != b.rows.end());
    CHECK(in4->randCalls == 3920);  // the resumed set-up's discarded shuffles
    CHECK(in4->x == 185);           // where Save & Exit left him
}

TEST_CASE("playthrough: the tube, the workshop's belt, the engine taken off and fitted again, the race door (app)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    {
        auto want = shots({"r2/aircraft/30_workshop_with_parts.png"});
        RunSpec spec{"tube", 600, {"10:down:up", "54:up:up"}, "r2/aircraft/29_at_tube_all_parts"};
        const Played p = play(*ggFiles, spec, want);
        CHECK(p.code == 0);
        checkShots(want);
        CHECK(stateSequence(p.rows) == "7.1 6");
    }
    // capture_r2.py workshop_build: the engine off the vehicle and onto the belt, the belt moved, the engine
    // carried back and fitted; then the race-track door starts the race (the vehicle is complete again).
    Mousekey m(28);
    m.tokens("click 195 155 wait 900 move 160 100 wait 500").waitMs(300).tokens("click 160 100 wait 1500").waitMs(200);
    m.tokens("click 190 42 wait 1500 click 160 66 wait 900 move 150 135 wait 500 click 150 135 wait 2500");
    std::vector<std::string> script = m.items();
    const auto door = static_cast<uint64_t>(m.time()) + 20;
    script.push_back(std::format("{}:click:305:135", door));
    auto want = shots({"r2/aircraft/37_remove_engine_carried.png", "r2/aircraft/38_engine_back_on_belt.png"});
    RunSpec spec{"workshop", door + 300, script, "r2/aircraft/36_vehicle_built"};
    const Played p = play(*ggFiles, spec, want);
    CHECK(p.code == 0);
    checkShots(want);
    CHECK(stateSequence(p.rows) == "6 11");
}

TEST_CASE("playthrough: a part picked up in room 4 (crouch over its box) and the HUD (app)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    // From the top floor's door of room 4: right through the two floor gaps to the box at (235, 150),
    // Down: the crouch (sequence 47) and Player_PickUpItem; the HUD as r2/aircraft/18 shows it (outside the
    // room: the player stands elsewhere in the shot), after a held key brought the labels back.
    std::vector<Shot> want;
    want.emplace_back("r2/aircraft/18_envelope_collected.png", std::vector<Mask>{{0, 0, 319, 164}});
    RunSpec spec{"pickup", 1000, {"20:down:right", "712:up:right", "730:down:down", "760:up:down", "900:tap:left"}, "r2/aircraft/17_room4_floor1"};
    const Played p = play(*ggFiles, spec, want);
    CHECK(p.code == 0);
    checkShots(want);
    CHECK(std::any_of(p.rows.begin(), p.rows.end(), [](const Row& r) { return r.seq == 47; }));
}
