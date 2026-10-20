// Wave 3 vehicles (re/gg/docs/code_vehicles_races.md): Math_DivRound, the race models against the test
// vectors of §18, the workshop resumed from the round 2 DOSBox-X states and played with the recorded
// clicks against their screenshots, the races from the built vehicles to the post-race menu, the final
// movie. Only counts, ticks, coordinates and CRCs here.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gg/app/gg_runtime.h"
#include "gg/audio/dro_compare.h"
#include "gg/audio/gg_audio.h"
#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/actors.h"
#include "gg/game/game.h"
#include "gg/game/parts.h"
#include "gg/game/race.h"
#include "gg/game/results.h"
#include "gg/game/sound.h"
#include "gg/game/workshop.h"
#include "gg/game/world.h"
#include "gg/save/gg_state_file.h"
#include "gg/save/players_store.h"
#include "gg/ui/dialog.h"
#include "gg/ui/ui_canvas.h"
#include "gg_game_fixture.h"
#include "gg_test_data.h"
#include "gg_vehicles_scenario.h"
#include "mods/png_codec.h"
#include "util/file_util.h"

using namespace tmport;
using namespace tmport::gg;

TEST_CASE("gg vehicles: Math_DivRound divides by the integer part, rounded half up") {
    CHECK(mathDivRound(10, 4 << 16) == 3);    // 2.5 -> 3
    CHECK(mathDivRound(9, 4 << 16) == 2);     // 2.25
    CHECK(mathDivRound(7, 3 << 16) == 2);     // 2.33
    CHECK(mathDivRound(8, 3 << 16) == 3);     // 2.67
    CHECK(mathDivRound(-10, 4 << 16) == -3);  // signs removed first
    CHECK(mathDivRound(10, -(4 << 16)) == -3);
    CHECK(mathDivRound(-10, -(4 << 16)) == 3);
    CHECK(mathDivRound(10, 0x48000) == 3);    // 4.5: only the high word 4 divides
    CHECK(mathDivRound(0x10000, 10 << 16) == 0x199A);  // the aircraft camera / 10
    CHECK(mathDivRound(5, 0x8000) == 0);      // a zero high word: the original faults; logged, 0
}

TEST_CASE("gg vehicles: Morty's races match the test vectors of code_vehicles_races.md 18") {
    GG_REQUIRE_DATA();
    const GgExe& exe = ggFiles->exe();
    const Archive& arc = ggFiles->archive();
    // Race index -> the frame Morty finishes at (a player that never moves).
    const std::vector<std::pair<int, int>> land = {{0, 62},   {1, 235},  {2, 290},  {3, 266},  {4, 338},
                                                   {10, 329}, {11, 251}, {12, 215}, {13, 317}, {14, 295}};
    for (const auto& [race, frames] : land) {
        const PartCatalogue cat = PartCatalogue::parse(arc.read(static_cast<uint16_t>(PartCatalogue::kFirstId + race)));
        const auto morty = landPerformance(exe, cat, race, cat.morty, 24);
        const LandPerformance still{};
        const RaceSimResult r = simulateLandRace(exe, arc, race, still, morty);
        INFO("race " << race << ": Morty's top speed " << (morty.max / 65536.0) << ", acceleration " << (morty.acc / 65536.0));
        CHECK(r.result == 1);
        CHECK(r.frames == frames);
    }
    // Morty's numbers of the table (px per frame): race 0 4.8 / 0.3, race 2 6.76 / 1.025.
    {
        const PartCatalogue cat = PartCatalogue::parse(arc.read(PartCatalogue::kFirstId));
        const auto m = landPerformance(exe, cat, 0, cat.morty, 24);
        MESSAGE("race 0: Morty max " << m.max / 65536.0 << " acc " << m.acc / 65536.0);
        CHECK(std::abs(m.max / 65536.0 - 4.8) < 0.001);
        CHECK(m.acc == 0x4CCD);
    }
    const std::vector<int> air = {401, 255, 322, 323, 279};
    for (int level = 0; level < 5; ++level) {
        const PartCatalogue cat = PartCatalogue::parse(arc.read(static_cast<uint16_t>(PartCatalogue::kFirstId + 5 + level)));
        const auto morty = airPerformance(exe, cat, level, cat.morty);
        const AirPerformance still{};
        const RaceSimResult r = simulateAirRace(exe, arc, level, still, morty);
        INFO("aircraft level " << level);
        CHECK(r.result == 1);
        CHECK(r.frames == air[static_cast<size_t>(level)]);
    }
}

// ---- scenarios from the round 2 states -------------------------------------------------------------------

namespace {

// A frame of the fixture (320 x 200, R | G << 8 | B << 16 | A << 24) as a 64-bit FNV-1a hash of its RGB.
uint64_t frameHash(std::span<const uint32_t> f) {
    uint64_t h = 1469598103934665603ull;
    for (const uint32_t p : f) {
        h ^= p & 0xFFFFFF;
        h *= 1099511628211ull;
    }
    return h;
}

// A capture PNG in the fixture's frame format (640 x 400 shots: one pixel of each 2 x 2 block).
std::optional<std::vector<uint32_t>> captureFrame(const std::filesystem::path& png) {
    const auto bytes = readWholeFile(png);
    if (!bytes) return std::nullopt;
    const mods::DecodedPng d = mods::decodePng(*bytes);
    const int step = d.width == 640 ? 2 : 1;
    if (d.width != 320 * step || d.height != 200 * step) return std::nullopt;
    std::vector<uint32_t> out(320 * 200);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            const uint8_t* c = &d.rgba[(static_cast<size_t>(y * step) * static_cast<size_t>(d.width) + static_cast<size_t>(x * step)) * 4];
            out[static_cast<size_t>(y) * 320 + static_cast<size_t>(x)] = c[0] | (c[1] << 8) | (c[2] << 16) | 0xFF000000u;
        }
    return out;
}

int diffPixels(std::span<const uint32_t> a, std::span<const uint32_t> b, std::span<const gggame::Mask> masks = {}) {
    if (a.size() != b.size()) return -1;
    int n = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            if (std::any_of(masks.begin(), masks.end(), [&](const gggame::Mask& m) { return x >= m.x0 && x <= m.x1 && y >= m.y0 && y <= m.y1; }))
                continue;
            const size_t i = static_cast<size_t>(y) * 320 + static_cast<size_t>(x);
            n += (a[i] & 0xFFFFFF) != (b[i] & 0xFFFFFF);
        }
    return n;
}

// Every picture the runtime shows: the interrupt count and the frame's hash (and, while `keep` is on,
// the frame itself).
struct FrameLog {
    std::vector<std::pair<uint64_t, uint64_t>> hashes;
    std::vector<std::pair<uint64_t, std::vector<uint32_t>>> kept;
    bool keep = false;
    std::filesystem::path dumpDir;  // GG_VEHICLES_DUMP: every frame as NNNNN_tTICKS.png (scratch comparisons)
    void attach(gggame::GameFixture& f) { attach(f.runtime()); }
    void attach(GgRuntime& rt) {
        rt.setFrameSink([this](uint64_t interrupt, std::span<const uint32_t> rgba) {
            hashes.emplace_back(interrupt, frameHash(rgba));
            if (keep) kept.emplace_back(interrupt, std::vector<uint32_t>(rgba.begin(), rgba.end()));
            if (!dumpDir.empty()) {
                std::vector<uint8_t> bytes(rgba.size() * 4);
                for (size_t i = 0; i < rgba.size(); ++i)
                    for (size_t k = 0; k < 4; ++k) bytes[4 * i + k] = static_cast<uint8_t>(rgba[i] >> (8 * k));
                writeWholeFile(dumpDir / std::format("{:05}_t{}.png", hashes.size() - 1, interrupt),
                               mods::encodePngRgba(320, 200, bytes));
            }
        });
    }
    // The index of the first picture equal to `frame`, or -1.
    int find(std::span<const uint32_t> frame) const {
        const uint64_t h = frameHash(frame);
        for (size_t i = 0; i < hashes.size(); ++i)
            if (hashes[i].second == h) return static_cast<int>(i);
        return -1;
    }
};

std::filesystem::path dumpDirFor(const char* name) {
    const char* root = std::getenv("GG_VEHICLES_DUMP");
    if (!root || !*root) return {};
    const auto dir = std::filesystem::path(root) / name;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// Steps until the function returns or waits for input it cannot get (a stall), at most `limit` x 500
// boundaries (20 000 frames by default).
void runToStall(gggame::GameFixture& f, int limit = 40) {
    for (int i = 0; i < limit && !f.done(); ++i) {
        (void)f.step(500);
        if (f.stalled()) return;
    }
}

}  // namespace

TEST_CASE("gg vehicles: the workshop resumed from the round 2 states draws their screenshots") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    struct Case {
        const char* state;
        const char* shot;
        // A state saved inside a hint's box (Hint_Show, the ui area's Dlg_RunModal). The box's own
        // save-under is in the state's heap, which nothing restores, so the area under the box (masked)
        // keeps the box's pixels; outside it the run reaches this shot.
        const char* afterBox = nullptr;
        // The box comes again (the hint is not marked seen yet: Hint_Show called from Workshop_Run) and
        // Enter closes it; otherwise the hint came from the delayed queue, already taken off it.
        bool boxAgain = false;
    };
    const Case cases[] = {
        // 30: the parts hint 0x1046 (queued by the workshop's entry, shown by the hint queue's timer).
        {"r2/aircraft/30_workshop_with_parts", "r2/aircraft/30_workshop_with_parts.png", "r2/aircraft/30_workshop_with_parts.png"},
        {"r2/aircraft/31_workshop_conveyor", "r2/aircraft/31_workshop_conveyor.png"},
        {"r2/aircraft/33_envelope_placed", "r2/aircraft/33_envelope_placed.png"},
        // 35: the "finished" hint 0x1053 over the completed vehicle; after it the race-track door opens (36).
        {"r2/aircraft/35_vehicle_assembled", "r2/aircraft/35_vehicle_assembled.png", "r2/aircraft/36_vehicle_built.png", true},
        {"r2/aircraft/36_vehicle_built", "r2/aircraft/36_vehicle_built.png"},
        {"r2/aircraft/55_vehicle_narrow", "r2/aircraft/55_vehicle_narrow_envelope.png"},
        {"r2/automotive/11_vehicle_built", "r2/automotive/11_vehicle_built.png"},
        {"r2/altenergy/11_vehicle_built", "r2/altenergy/11_vehicle_built.png"},
    };
    for (const Case& c : cases) {
        const auto sav = testdata::ggCapture(std::format("savestates/{}.sav", c.state));
        const auto png = testdata::ggCapture(c.shot);
        if (!sav || !png) continue;
        const auto shot = captureFrame(*png);
        REQUIRE(shot);
        gggame::GameFixture f(*ggFiles);
        std::string why;
        const auto s = vehscen::loadVehiclesState(f, *sav, why);
        INFO(std::string(c.state) << ": " << why);
        REQUIRE(s);
        GgGame& g = f.g();
        const int atLoad = diffPixels(f.frame(), *shot);
        f.start([](GgGame& gg) { state06Workshop(gg, StateEntry::Resume); });
        (void)f.step(12);
        if (!c.afterBox) {
            const int after = diffPixels(f.frame(), *shot);
            MESSAGE(std::string(c.state) << ": " << atLoad << " pixels differ from the shot at the load, " << after << " after 12 frames");
            CHECK(atLoad == 0);
            CHECK(after == 0);
            CHECK(!f.stalled());
        } else {
            const auto target = captureFrame(*testdata::ggCapture(c.afterBox));
            REQUIRE(target);
            const bool stalled = f.stalled();
            const int inBox = diffPixels(f.frame(), *shot);
            const gggame::Mask box{g.ui.dlgX, g.ui.dlgY, g.ui.dlgX + g.ui.dlgW - 1, g.ui.dlgY + g.ui.dlgH - 1};
            if (c.boxAgain) {
                REQUIRE(f.keys("2:down:enter, 3:up:enter"));
                (void)f.step(40);  // the race-track door's opening
            }
            const gggame::Mask masks[] = {box, {g.mouse().x(), g.mouse().y(), g.mouse().x() + 9, g.mouse().y() + 9}};
            const int after = diffPixels(f.frame(), *target, masks);
            if (const auto dir = dumpDirFor("resume"); !dir.empty()) {
                const auto px = f.frame();
                std::vector<uint8_t> bytes(px.size() * 4);
                for (size_t i = 0; i < px.size(); ++i)
                    for (size_t k = 0; k < 4; ++k) bytes[4 * i + k] = static_cast<uint8_t>(px[i] >> (8 * k));
                writeWholeFile(dir / (std::filesystem::path(c.state).filename().string() + ".png"), mods::encodePngRgba(320, 200, bytes));
            }
            MESSAGE(std::string(c.state) << ": " << atLoad << " pixels differ from the shot at the load, " << inBox
                                         << " after 12 frames (the box " << std::string(c.boxAgain ? "drawn again" : "not drawn") << "), " << after
                                         << " from " << std::string(c.afterBox) << " outside the box at the end");
            CHECK(atLoad == 0);
            CHECK(stalled == c.boxAgain);
            if (c.boxAgain) CHECK(inBox == 0);
            CHECK(after == 0);
            CHECK(!f.stalled());
        }
        f.finish();
    }
}

namespace {

// The states from the workshop on as Seq_Run runs them (12e0:000f), the workshop resumed: 6 -> 8 (the
// world's State_08_LeaveRoom) -> 10 -> 11 -> 12 / 14 -> 13 / 15; `visited` gets every state entered.
void raceSequence(GgGame& g, std::vector<int16_t>& visited, const std::function<void(int16_t)>& onState = {}) {
    int16_t st = 6;
    for (int guard = 0; guard < 16; ++guard) {
        g.flow.state = st;
        visited.push_back(st);
        if (onState) onState(st);
        switch (st) {
        case 6: st = state06Workshop(g, StateEntry::Resume); break;
        case 8: st = state08LeaveRoom(g); break;
        case 10: st = state10ListVehicleParts(g); break;
        case 11: st = state11Race(g); break;
        case 12: st = state12RaceResult(g); break;
        case 13: st = state13PostRaceMenu(g); break;
        case 14: st = state14RedrawResult(g); break;
        case 15: st = state15FinalMovie(g); break;
        default: return;
        }
    }
}

struct RaceCase {
    const char* name;
    const char* state;      // the built vehicle in the workshop
    int16_t result;         // 0 won
    int32_t points;         // the score after the count-up
    const char* menuShot;   // the post-race menu (after the rank box or the lost hint)
    const char* raceShots[3];
    const char* rankShot;   // the rank-up box over the results
};

}  // namespace

TEST_CASE("gg vehicles: the races from the built vehicles to the post-race menu") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const RaceCase cases[] = {
        {"automotive", "r2/automotive/11_vehicle_built", 0, 4160, "r2/automotive/14_won_postrace.png",
         {"r2/automotive/12_race_start.png", nullptr, nullptr}, "r2/automotive/13_race_end.png"},
        {"altenergy", "r2/altenergy/11_vehicle_built", 0, 4180, "r2/altenergy/14_won_postrace.png",
         {"r2/altenergy/12_race_start.png", nullptr, nullptr}, "r2/altenergy/13_race_end.png"},
        {"aircraft_won", "r2/aircraft/55_vehicle_narrow", 0, 7040, "r2/aircraft/57_won_postrace.png", {nullptr, nullptr, nullptr}, "r2/aircraft/56_won_rankup.png"},
        {"aircraft_lost", "r2/aircraft/36_vehicle_built", 1, 2220, "r2/aircraft/43_result_lost.png",
         {"r2/aircraft/40_race_start.png", "r2/aircraft/41_race_mid.png", nullptr}, nullptr},
    };
    for (const RaceCase& c : cases) {
        const auto sav = testdata::ggCapture(std::format("savestates/{}.sav", c.state));
        if (!sav) continue;
        INFO(std::string(c.name));
        gggame::GameFixture f(*ggFiles);
        std::string why;
        const auto s = vehscen::loadVehiclesState(f, *sav, why);
        INFO(why);
        REQUIRE(s);
        GgGame& g = f.g();
        const auto startTicks = g.ticks();
        const int wonBefore = g.player.racesWonTotal;
        FrameLog frames;
        // The pictures' hashes and the count-up: the score at every picture of the result screen (state 12).
        std::vector<std::pair<uint64_t, uint32_t>> score;
        const auto sink = [&](uint64_t interrupt) {
            if (g.flow.state == 12 && (score.empty() || score.back().second != g.player.points)) score.emplace_back(interrupt, g.player.points);
        };
        f.runtime().setFrameSink([&frames, &sink](uint64_t interrupt, std::span<const uint32_t> rgba) {
            frames.hashes.emplace_back(interrupt, frameHash(rgba));
            sink(interrupt);
        });
        REQUIRE(f.mouse("10:click:305:135"));  // the race-track door
        std::vector<int16_t> visited;
        f.start([&visited](GgGame& gg) { raceSequence(gg, visited); });
        runToStall(f);  // the rank box (won) or the lost hint: a dialog of the ui area waits for Enter
        const bool inDialog = !f.done() && g.flow.state == 12;
        if (inDialog) {
            REQUIRE(f.keys("2:down:enter, 3:up:enter"));
            runToStall(f);
        }
        // The post-race menu waits (Input_WaitCode).
        CHECK(g.flow.state == 13);
        CHECK(g.vehicles.raceResult == c.result);
        CHECK(g.player.racesWonTotal == wonBefore + (c.result == 0 ? 1 : 0));
        if (c.points >= 0) CHECK(g.player.points == static_cast<uint32_t>(c.points));
        if (c.result == 0) {  // 50 updates, one per 4-tick frame (REFERENCE_CAPTURES 10.1: 3.94 ticks measured)
            // (frame 1 counts before its fade-in shows the first picture: every entry is an update)
            REQUIRE(score.size() >= 2);
            const size_t updates = score.size();
            CHECK(updates == 50);
            for (size_t i = 2; i < score.size(); ++i) CHECK(score[i].first - score[i - 1].first == 4);
            MESSAGE(std::string(c.name) << ": " << updates << " score updates, " << (score.back().first - score[1].first)
                                        << " ticks from the first to the last");
        }
        MESSAGE(std::string(c.name) << ": states " << visited.size() << ", " << frames.hashes.size() << " pictures over "
                       << (g.ticks() - startTicks) << " ticks; score " << g.player.points << std::string(inDialog ? "; a box closed" : ""));
        if (const auto png = testdata::ggCapture(c.menuShot)) {
            const auto shot = captureFrame(*png);
            REQUIRE(shot);
            const gggame::Mask cursor{290, 125, 319, 155};  // the arrow at the race-track door's click
            const int n = diffPixels(f.frame(), *shot, std::span(&cursor, 1));
            MESSAGE(std::string(c.name) << ": the post-race menu differs from " << std::string(c.menuShot) << " in " << n << " pixels");
            CHECK(n == 0);
        }
        // The rank-up text (Result_RankUpMessage) in the ui library's box (wave 2's Dlg_Draw) over the
        // result screen, against the shot of the original's box.
        if (const auto png = c.rankShot ? testdata::ggCapture(c.rankShot) : std::nullopt) {
            const auto text = resultRankUpText(g, g.player.points);
            REQUIRE(!text.empty());
            (void)dlgDraw(UiCanvas::of(g.video, g.fonts), messageBoxDesc(std::nullopt, text, g.strings.footerGoOn));
            f.present();
            const auto shot = captureFrame(*png);
            REQUIRE(shot);
            const gggame::Mask cursor{290, 125, 319, 155};
            const int n = diffPixels(f.frame(), *shot, std::span(&cursor, 1));
            MESSAGE(std::string(c.name) << ": the rank-up box drawn with the text differs from " << std::string(c.rankShot) << " in " << n
                                        << " pixels");
            CHECK(n == 0);
        }
        for (const char* r : c.raceShots) {
            if (!r) continue;
            const auto png = testdata::ggCapture(r);
            if (!png) continue;
            const auto shot = captureFrame(*png);
            REQUIRE(shot);
            const int at = frames.find(*shot);
            MESSAGE(std::string(c.name) << ": " << std::string(r) << (at >= 0 ? std::format(" is picture {}", at) : std::string(" is no picture of the run")));
        }
        f.finish();
    }
}

namespace {

// A race run on the real sound driver (GgAudio offline, FM, every OPL write logged) instead of the
// fixture's recording facade: the music intro waits last as long as the original's, the OPL writes can
// be compared with the .dro captures. Clicks the race-track door `clickTick` ticks after the state's
// moment, taps Enter every 300 ticks from `enterFrom` on (the keys before the box opens are flushed by
// it, the post-race menu ignores Enter), and ends at `limit` ticks (GgRunEnd).
struct AudioRun {
    std::vector<dos::OplWriteLog::Entry> opl;
    std::map<int16_t, std::pair<uint64_t, size_t>> stateAt;  // state -> its first sound tick and OPL log index
    FrameLog frames;
    std::vector<int16_t> visited;
    int16_t result = -1;
    uint32_t points = 0;
    std::string why;
};

// A capture from `fromMs` on: the registers' values there as its dump (every non-zero register, the key
// bits of B0h..B8h and the drum bits of BDh masked, 02h..04h never: DOSBox-X's rules, dro_compare.h),
// then the writes after it.
DroFile droTailAt(const DroFile& d, size_t from) {
    std::array<int, 0x200> regs;
    regs.fill(-1);
    size_t i = 0;
    for (; i < d.writes.size() && i < from; ++i) regs[d.writes[i].reg & 0x1FF] = d.writes[i].value;
    DroFile t;
    t.hardware = d.hardware;
    t.lengthMs = d.lengthMs;
    for (int r = 0; r < 0x200; ++r) {
        if (regs[static_cast<size_t>(r)] <= 0 || (r >= 2 && r <= 4)) continue;
        int v = regs[static_cast<size_t>(r)];
        if (r >= 0xB0 && r <= 0xB8) v &= ~0x20;
        if (r == 0xBD) v &= ~0x1F;
        if (v == 0) continue;
        t.writes.push_back({0, static_cast<uint16_t>(r), static_cast<uint8_t>(v)});
    }
    t.dumpSize = t.writes.size();
    t.writes.insert(t.writes.end(), d.writes.begin() + static_cast<std::ptrdiff_t>(i), d.writes.end());
    return t;
}

DroFile droTail(const DroFile& d, uint32_t fromMs) {
    size_t i = 0;
    while (i < d.writes.size() && d.writes[i].ms < fromMs) ++i;
    return droTailAt(d, i);
}

AudioRun runRaceWithAudio(const GgFiles& files, const std::filesystem::path& sav, const DroFile* seed, int clickTick,
                          int enterFrom, uint64_t limit, const std::filesystem::path& dumpDir, bool entryEffect = true,
                          uint64_t retracePhaseNs = 0) {
    AudioRun out;
    GgAudioConfig cfg;
    cfg.mode = SoundMode::Fm;
    cfg.logOpl = true;
    GgAudio audio(files.exe(), files.archive(), cfg);
    if (seed) audio.call([&](SndRes& r) { seedB0Shadows(r.driver(), *seed); });
    RuntimeConfig rc;
    rc.deterministic = true;
    rc.retracePhaseNs = retracePhaseNs;
    GgRuntime rt(rc);
    rt.installHandlers();
    rt.clock().setSoundTick([&audio](uint32_t) { audio.step(nullptr); });
    GgAudioSound sound(audio);
    const auto pref = testdata::scratchDir("gg_vehicles_audio");
    {
        PlayersStore store(playersPathIn(pref));
        store.load();
        BorlandRand rand;
        rand.srand(0x29CF);
        GgGameOptions o;
        o.seed = 0x29CF;
        GgGame g(files, rt, sound, store, rand, o);
        const GgStateFile s = GgStateFile::load(sav);
        (void)s.apply(g, true);
        if (importPlayers(GgStateFile::playersFileOf(sav), store.path(), true) == PlayersImport::Imported) {
            store.load();
            g.ui.roster = store.roster();
        }
        std::string w1, w2;
        const bool owners = worldRestoreFromState(g, s, w1) && actorsRestoreFromState(g, s, w2) && g.world.parts.count() > 0;
        if ((!owners && !vehscen::restoreParts(g, s, out.why)) || !vehiclesRestoreFromState(g, s, out.why)) {
            rt.clock().setSoundTick({});
            rt.removeHandlers();
            return out;
        }
        // The workshop's entry effect 10 (0, 5) as the captures' schedules start it with the music
        // (fmt_sound.md 5: "0:W,0:f:10:0:5"): its channel use shapes the music's voices.
        if (entryEffect) g.sound.playEffect(static_cast<GgState&>(g).sound.effectBank + 10, 0, 5);
        out.frames.dumpDir = dumpDir;
        out.frames.attach(rt);
        std::string script = std::format("{}:click:305:135", clickTick);
        for (int t = enterFrom; t < static_cast<int>(limit); t += 300) script += std::format(", {}:down:enter, {}:up:enter", t, t + 1);
        std::string err;
        const auto parsed = dos::ScriptedInput::parse(script, err);
        REQUIRE_MESSAGE(parsed, err);
        rt.addScript(*parsed, rt.clock().interrupts());
        rt.setRunLimit(rt.clock().interrupts() + limit);
        const auto mark = [&](int16_t st) {
            if (!out.stateAt.count(st)) out.stateAt[st] = {audio.tickIndex(), audio.oplLog().entries().size()};
        };
        try {
            raceSequence(g, out.visited, mark);
        } catch (const GgRunEnd&) {
        } catch (const GgExit&) {
        }
        out.result = g.vehicles.raceResult;
        out.points = g.player.points;
        rt.setFrameSink({});
    }
    out.opl.assign(audio.oplLog().entries().begin(), audio.oplLog().entries().end());
    if (!dumpDir.empty()) {  // scratch comparisons: "tick reg value [c]" per write
        std::string text;
        for (const auto& e : out.opl) text += std::format("{} {:02x} {:02x}{}{}", e.tick, e.reg, e.value, e.command ? " c" : "", '\n');
        writeWholeFile(dumpDir / "opl.txt", std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
    }
    rt.clock().setSoundTick({});
    rt.removeHandlers();
    std::error_code ec;
    std::filesystem::remove_all(pref, ec);
    return out;
}

}  // namespace

TEST_CASE("gg vehicles: the races on the sound driver against their OPL captures") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    // The captures' schedules (gg/audio/soundtest.cpp) free the workshop's banks M sound ticks after its
    // music started: the click comes 25 sound ticks (the fade-out) before, 2 (M - 25) PIT ticks. The
    // aircraft course's first effect comes one sound tick earlier against the music than in the
    // original (its set-up's decompression and first frame take CPU time the engine does not model:
    // PORTING_NOTES 2.4): the race part is reported, not checked, there.
    struct Case {
        const char* name;
        const char* state;
        const char* dro;
        int16_t result;
        int workshopTicks;  // M
        bool landRace;      // the race's writes identical up to its end
        bool resultsExact;  // the results' writes identical once aligned
        uint64_t limit = 9000;  // PIT ticks to run
    };
    const Case cases[] = {
        {"automotive", "r2/automotive/11_vehicle_built", "audio/r2_automotive_race1_won.dro", 0, 514, true, false},
        {"altenergy", "r2/altenergy/11_vehicle_built", "audio/r2_altenergy_race1_won.dro", 0, 367, true, true},
        {"aircraft_won", "r2/aircraft/55_vehicle_narrow", "audio/r2_aircraft_race1_won.dro", 0, 821, false, true},
        {"aircraft_lost", "r2/aircraft/36_vehicle_built", "audio/r2_aircraft_race1_lost.dro", 1, 542, false, false},
        {"movie", "r2/movie/00_vehicle_ready_total14", "audio/r2_movie_race15.dro", 0, 359, false, false, 20000},
    };
    for (const Case& c : cases) {
        const auto sav = testdata::ggCapture(std::format("savestates/{}.sav", c.state));
        const auto droPath = testdata::ggCapture(c.dro);
        if (!sav || !droPath) continue;
        const auto bytes = readWholeFile(*droPath);
        REQUIRE(bytes);
        const DroFile dro = parseDro(*bytes);
        // The click after 489 sound ticks of the workshop's music, as the captures' schedules found
        // (fmt_sound.md 5: the race music 514 sound ticks after the workshop's, its fade-out between).
        const AudioRun run = runRaceWithAudio(*ggFiles, *sav, &dro, 2 * (c.workshopTicks - 25), 3000 + 2 * c.workshopTicks, c.limit,
                                              dumpDirFor((std::string(c.name) + "_audio").c_str()));
        INFO(std::string(c.name) << ": " << run.why);
        REQUIRE(run.why.empty());
        CHECK(run.result == c.result);
        const DroComparison cmp = compareWithDro(dro, run.opl, 0, GgAudio::kOplWriteSeconds * 1000.0, CompareBase::CaptureDump);
        MESSAGE(std::string(c.name) << ": the whole capture, " << cmp.matched << " of " << cmp.refWrites << " writes in order; "
                                    << cmp.summary());
        if (std::getenv("GG_VEHICLES_DUMP")) {  // the drift of the matched writes (scratch diagnostics)
            std::string drift;
            double last = 0;
            for (size_t i = 0; i < cmp.pairs.size(); ++i) {
                const auto& p = cmp.pairs[i];
                const double dev = (p.ms - cmp.offsetMs) / 13.7314 - static_cast<double>(p.tick);
                if (i == 0 || std::abs(dev - last) > 0.75) drift += std::format("[{} t{} {:+.1f}{}] ", i, p.tick, dev, p.command ? "c" : "");
                last = dev;
            }
            MESSAGE(drift);
        }
        CHECK(cmp.found);
        // The race's music and effects are written as the original writes them up to the race's end on the
        // land courses; the aircraft course diverges at the first effect (the reason is in the report).
        const auto raceEnd = run.stateAt.count(12) ? run.stateAt.at(12).first : uint64_t{0};
        const uint64_t lastMatched = cmp.pairs.empty() ? 0 : cmp.pairs.back().tick;
        MESSAGE(std::string(c.name) << ": the results start at sound tick " << raceEnd << ", identical up to tick " << lastMatched);
        if (c.landRace) CHECK(lastMatched + 1 >= raceEnd);
        // The results from their start: the capture cut where the engine's results begin (the original takes
        // about 14 PIT ticks more to load and draw the result screen: PORTING_NOTES 2.4), the register state
        // there as its dump.
        if (run.stateAt.count(15) && cmp.found) {  // the final movie (State_15_FinalMovie: music 0x30CA, the cues' effects)
            // The original's rank-up box held the sound (Snd_Pause) until the player's Enter: the capture's
            // movie begins after its longest silence.
            // The cut is the first key-on after it (a capture opens on a key-on).
            uint32_t longest = 0;
            size_t cut = 0;
            for (size_t i = 0; i + 1 < dro.writes.size(); ++i)
                if (dro.writes[i + 1].ms - dro.writes[i].ms > longest) {
                    longest = dro.writes[i + 1].ms - dro.writes[i].ms;
                    cut = i + 1;
                }
            while (cut < dro.writes.size() && !isCaptureTrigger(static_cast<uint8_t>(dro.writes[cut].reg), dro.writes[cut].value)) ++cut;
            REQUIRE(cut < dro.writes.size());
            const DroFile tail = droTailAt(dro, cut);
            const DroComparison movie =
                compareWithDro(tail, run.opl, run.stateAt.at(15).second, GgAudio::kOplWriteSeconds * 1000.0, CompareBase::CaptureDump);
            MESSAGE(std::string(c.name) << ": the movie, the capture from " << dro.writes[cut].ms << " ms (after " << longest << " ms of silence): "
                                        << movie.matched << " of " << movie.refWrites << " writes in order ("
                                        << (movie.pairs.empty() ? 0u : movie.pairs.back().ms - movie.pairs.front().ms) << " ms; "
                                        << movie.firstMismatch << "); " << movie.summary());
            CHECK(movie.found);
        }
        if (run.stateAt.count(12) && cmp.found) {
            const auto [tick12, index12] = run.stateAt.at(12);
            // The cut after the original's race music stopped: searched over the load time (0..40 sound ticks).
            DroComparison best;
            int bestShift = -1;
            for (int shift = 0; shift <= 40; ++shift) {
                const double ms = cmp.offsetMs + 13.7314 * static_cast<double>(tick12 + static_cast<uint64_t>(shift));
                const DroFile tail = droTail(dro, static_cast<uint32_t>(std::max(0.0, ms)));
                const DroComparison res = compareWithDro(tail, run.opl, index12, GgAudio::kOplWriteSeconds * 1000.0, CompareBase::CaptureDump);
                if (res.found && (bestShift < 0 || res.matched > best.matched)) {
                    best = res;
                    bestShift = shift;
                }
            }
            MESSAGE(std::string(c.name) << ": the results, the capture cut " << bestShift << " sound ticks after the engine's start: "
                                        << best.matched << " of " << best.refWrites << " writes in order; " << best.summary());
            CHECK(bestShift >= 0);
            // Where the frames fall on the other parity of the sound ticks than in the original (whose
            // drawing takes CPU time the engine does not model), a frame's effect or count-up click is
            // written one tick before or after a music write instead: the in-order count stops there.
            if (c.resultsExact) CHECK(best.matched == best.refWrites);
        }
    }
}

TEST_CASE("gg vehicles: the 15th race, the rank-up and the final movie") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto sav = testdata::ggCapture("savestates/r2/movie/00_vehicle_ready_total14.sav");
    if (!sav) {
        MESSAGE("savestates/r2/movie/00_vehicle_ready_total14.sav missing: test skipped");
        return;
    }
    gggame::GameFixture f(*ggFiles);
    std::string why;
    const auto s = vehscen::loadVehiclesState(f, *sav, why);
    INFO(why);
    REQUIRE(s);
    GgGame& g = f.g();
    FrameLog frames;
    frames.dumpDir = dumpDirFor("movie");
    frames.attach(f);
    REQUIRE(f.mouse("10:click:305:135"));
    std::vector<int16_t> visited;
    f.start([&visited](GgGame& gg) { raceSequence(gg, visited); });
    runToStall(f);  // the rank-up box (a dialog of the ui area)
    CHECK(g.player.racesWonTotal == 15);
    CHECK(g.player.points == 303040u);  // the 15th win's 300 000
    const size_t beforeBox = frames.hashes.size();
    if (g.flow.state == 12 && f.stalled()) {
        REQUIRE(f.keys("2:down:enter, 3:up:enter"));
        runToStall(f);  // the movie, then Input_WaitCode
    }
    CHECK(g.flow.state == 15);
    MESSAGE("movie: " << frames.hashes.size() - beforeBox << " pictures after the rank-up box");
    int found = 0, shots = 0;
    for (const char* shot : {"r2/movie/01_rankup_scientist.png", "r2/movie/02_movie_start.png", "r2/movie/03_movie_10s.png",
                             "r2/movie/04_movie_20s.png", "r2/movie/05_movie_35s.png", "r2/movie/06_movie_50s.png"}) {
        const auto png = testdata::ggCapture(shot);
        if (!png) continue;
        ++shots;
        const auto frame = captureFrame(*png);
        REQUIRE(frame);
        const int at = frames.find(*frame);
        found += at >= 0;
        MESSAGE(std::string(shot) << (at >= 0 ? std::format(": picture {}", at) : std::string(": no picture of the run")));
    }
    CHECK(found >= shots - 1);  // the rank-up box is the ui area's drawing
    // The key after the movie: saved, state 17.
    REQUIRE(f.keys("2:down:space, 3:up:space"));
    runToStall(f);
    CHECK(f.done());
    CHECK(visited.back() == 17);
    CHECK(g.player.buildingInProgress[1] == 0);
    f.finish();
}

TEST_CASE("gg vehicles: an engine taken off the built vehicle and put back on the belt") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto sav = testdata::ggCapture("savestates/r2/aircraft/36_vehicle_built.sav");
    const auto carried = testdata::ggCapture("r2/aircraft/37_remove_engine_carried.png");
    const auto onBelt = testdata::ggCapture("r2/aircraft/38_engine_back_on_belt.png");
    if (!sav || !carried || !onBelt) return;
    gggame::GameFixture f(*ggFiles);
    std::string why;
    const auto s = vehscen::loadVehiclesState(f, *sav, why);
    INFO(why);
    REQUIRE(s);
    GgGame& g = f.g();
    const int installed = g.player.live.partsInstalled;
    CHECK(g.vehicles.raceDoorOpen == 1);
    // capture_r2.py "workshop_build": click the engine (195, 155), move to (160, 100); click there.
    REQUIRE(f.mouse("10:click:195:155, +131:move:160:100"));
    f.start([](GgGame& gg) { state06Workshop(gg, StateEntry::Resume); });
    (void)f.runUntil([&](const GgTraceRecord&) { return g.ticks() >= s->dsDword(dg::kTicks) + 10 + 131 + 73; }, 400);
    CHECK(g.vehicles.carriedRow != -1);
    CHECK(g.player.live.partsInstalled == installed - 1);
    CHECK(g.vehicles.raceDoorOpen == 0);  // the door closed when the vehicle became incomplete
    const auto shot37 = captureFrame(*carried);
    REQUIRE(shot37);
    const int n37 = diffPixels(f.frame(), *shot37);
    MESSAGE("37_remove_engine_carried: " << n37 << " pixels differ");
    REQUIRE(f.mouse("1:click:160:100"));
    const auto t0 = g.ticks();
    (void)f.runUntil([&](const GgTraceRecord&) { return g.ticks() >= t0 + 218; }, 400);
    CHECK(g.vehicles.carriedRow == -1);
    const auto shot38 = captureFrame(*onBelt);
    REQUIRE(shot38);
    const int n38 = diffPixels(f.frame(), *shot38);
    MESSAGE("38_engine_back_on_belt: " << n38 << " pixels differ");
    CHECK(n37 == 0);
    CHECK(n38 == 0);
    f.finish();
}

TEST_CASE("gg vehicles: the blueprint of a first visit closed with Done") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto sav = testdata::ggCapture("savestates/r2/aircraft/02_blueprint.sav");
    const auto open = testdata::ggCapture("r2/aircraft/02_blueprint.png");
    const auto workshop = testdata::ggCapture("r2/aircraft/04_workshop.png");
    if (!sav || !open || !workshop) return;
    gggame::GameFixture f(*ggFiles);
    std::string why;
    const auto s = vehscen::loadVehiclesState(f, *sav, why);
    INFO(why);
    REQUIRE(s);
    GgGame& g = f.g();
    CHECK(g.vehicles.blueprintOpen == 1);
    CHECK(g.vehiclesObjects().blueprintShown == 1);  // the overview: its hot spots are linked
    const auto shot02 = captureFrame(*open);
    REQUIRE(shot02);
    const gggame::Mask cursor{40, 40, 70, 70};  // the arrow (the state's pointer at (50, 50))
    MESSAGE("02_blueprint at the load: " << diffPixels(f.frame(), *shot02, std::span(&cursor, 1)) << " pixels differ");
    REQUIRE(f.mouse("10:click:262:150"));  // Done (1007 at (248, 142))
    f.polls(true);                          // Blueprint_Run is an unpaced loop
    f.start([](GgGame& gg) { state06Workshop(gg, StateEntry::Resume); });
    (void)f.runUntil([&](const GgTraceRecord&) { return g.vehicles.blueprintOpen == 0; }, 200000);
    CHECK(g.vehicles.blueprintOpen == 0);
    CHECK(g.player.live.blueprintSeen == 1);
    f.polls(false);
    runToStall(f);  // the hint 0x1046 (due while the blueprint was open) waits for Enter
    REQUIRE(f.keys("2:down:enter, 3:up:enter"));
    (void)f.step(12);
    const auto shot04 = captureFrame(*workshop);
    REQUIRE(shot04);
    // The arrow after the Done click, and the state's arrow at (50, 50): the state's screen buffer holds
    // the drawn cursor, and GgStateFile::apply restores the pointer but not the cursor's save-under, so
    // the first move puts that picture back (48 pixels).
    const gggame::Mask arrows[] = {{255, 135, 300, 175}, {48, 48, 60, 60}};
    const int n = diffPixels(f.frame(), *shot04, arrows);
    MESSAGE("04_workshop after Done and the hint: " << n << " pixels differ");
    if (const auto dir = dumpDirFor("blueprint"); !dir.empty()) {
        const auto px = f.frame();
        std::vector<uint8_t> bytes(px.size() * 4);
        for (size_t i = 0; i < px.size(); ++i)
            for (size_t k = 0; k < 4; ++k) bytes[4 * i + k] = static_cast<uint8_t>(px[i] >> (8 * k));
        writeWholeFile(dir / "after_done.png", mods::encodePngRgba(320, 200, bytes));
    }
    CHECK(n == 0);
    f.finish();
}

TEST_CASE("gg vehicles: the blueprint opened from the workshop and its part pages") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    struct Shot {
        const char* png;
        gggame::Mask pointer;  // the original's arrow in the shot (9 x 9)
    };
    // 39 was taken with the engine off the vehicle (state 38: the race-track door closed), 46..48 with the
    // vehicle built again (after state 45: the door open).
    struct Run {
        const char* state;
        std::vector<Shot> shots;
    };
    const Run runs[] = {
        {"r2/aircraft/38_engine_removed", {{"r2/aircraft/39_blueprint_from_workshop.png", {270, 50, 278, 58}}}},
        {"r2/aircraft/45_vehicle_other_engine",
         {{"r2/aircraft/46_blueprint_envelope_page.png", {200, 75, 208, 83}},
          {"r2/aircraft/47_blueprint_engine_page.png", {165, 140, 173, 148}},
          {"r2/aircraft/48_blueprint_gondola_page.png", {220, 125, 228, 133}}}},
    };
    for (const Run& run : runs) {
        const auto sav = testdata::ggCapture(std::format("savestates/{}.sav", run.state));
        if (!sav) continue;
        std::vector<std::pair<const Shot*, std::vector<uint32_t>>> shots;
        for (const Shot& sh : run.shots)
            if (const auto png = testdata::ggCapture(sh.png))
                if (const auto frame = captureFrame(*png)) shots.emplace_back(&sh, *frame);
        if (shots.empty()) continue;
        gggame::GameFixture f(*ggFiles);
        std::string why;
        const auto s = vehscen::loadVehiclesState(f, *sav, why);
        INFO(why);
        REQUIRE(s);
        GgGame& g = f.g();
        // The blueprint screen (1003), then each page's hot spot (Blueprint_Load's 'M' records of race 5,
        // + (18, 9)) and Enter back to the overview.
        const std::pair<int, int> spots[] = {{65, 122}, {168, 141}, {218, 127}, {106, 46}, {55, 50}};
        std::string script = "10:click:260:50";
        for (const auto& [x, y] : spots) script += std::format(", +150:click:{}:{}, +150:down:enter, +1:up:enter", x, y);
        REQUIRE(f.mouse(script));
        f.polls(true);  // Blueprint_Run is an unpaced loop
        f.start([](GgGame& gg) { state06Workshop(gg, StateEntry::Resume); });
        const auto t0 = g.ticks();
        std::vector<int> best(shots.size(), 1 << 30);
        for (uint64_t t = t0 + 140; t < t0 + 10 + 301 * (std::size(spots) + 1); t += 150) {
            (void)f.runUntil([&](const GgTraceRecord&) { return g.ticks() >= t; }, 1000000);
            const int mx = g.mouse().x(), my = g.mouse().y();
            for (size_t k = 0; k < shots.size(); ++k) {
                const gggame::Mask masks[] = {{mx, my, mx + 9, my + 9}, shots[k].first->pointer};
                best[k] = std::min(best[k], diffPixels(f.frame(), shots[k].second, masks));
            }
        }
        for (size_t k = 0; k < shots.size(); ++k) {
            MESSAGE(std::string(shots[k].first->png) << ": " << best[k] << " pixels differ at the closest checkpoint");
            CHECK(best[k] == 0);
        }
        CHECK(g.vehicles.blueprintOpen == 1);
        f.finish();
    }
}
