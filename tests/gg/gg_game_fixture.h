#pragma once
// Scenario helper for the tests of the G&G game modules (wave 3; re/gg/docs/ENGINE_MAP.md, "Wave 3 map",
// "Tests: the fixture"): a GgGame on the user's GG_DATA folder, headless and deterministic, with a
// recording sound facade, a PLAYERS.DAT store in a scratch preference folder and the one Borland rand()
// seeded as the test asks. A test sets a scenario up (a fresh game, a PLAYERS.DAT record, a DOSBox-X
// state), gives scripted keys and mouse in PIT ticks, runs a game function and steps it frame by frame,
// recording per frame what the --gg-trace logger writes (game/trace.h), and compares the picture with
// a capture.
//
//   GG_REQUIRE_DATA();                                       // skips without GG_DATA
//   gggame::GameFixture f(*ggFiles);
//   f.loadPlayers(*testdata::ggCapture("savestates/r2/aircraft/06_room1.players"));
//   f.loadState(*testdata::ggCapture("savestates/r2/aircraft/06_room1.sav"));
//   f.keys("10:down:right, 60:up:right");                    // PIT ticks after now
//   f.start([](GgGame& g) { state07Warehouse(g, StateEntry::Resume); });
//   auto rows = f.step(20);                                  // 20 frames (Timer_Wait(0) boundaries)
//   CHECK(rows.back().x == ...);
//   f.finish();                                              // stops the function at its next yield
//
// The game function runs on a worker thread that hands over to the test at every frame boundary (one
// thread runs at a time): checks happen on the test thread between steps, never inside the function.
// A boundary is a Timer_Wait(0) (the paced loops' frames) and, with polls(true), every input poll and
// busy-wait turn too (the unpaced loops: dialogs, menus, puzzles). A function that yields kStallYields
// times without a boundary is paused as stalled (a test of an unpaced loop forgot polls(true)).
//
// With stub modules the functions run but change little; the set-up helpers work anyway.

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "gg/app/gg_runtime.h"
#include "gg/game/game.h"
#include "gg/game/sound.h"
#include "gg/game/trace.h"
#include "gg/save/players_store.h"
#include "rtl/borland_rand.h"

namespace gggame {

struct FixtureOptions {
    tmport::gg::GgGameOptions game;   // cpu, switches, Gfx_Init and Hud_Init at construction
    uint16_t seed = 0x29CF;           // srand's argument (the w0 tour's seed; DS:2454)
    // A preference folder for players.dat; empty: a new scratch folder under the system temp folder,
    // removed with the fixture.
    std::filesystem::path prefDir;
    FixtureOptions() = default;
};

// A rectangle of the 320x200 screen, inclusive, excluded from a picture comparison.
struct Mask {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

class GameFixture {
public:
    static constexpr uint64_t kStallYields = 200'000;

    explicit GameFixture(const tmport::gg::GgFiles& files, FixtureOptions o = {});
    ~GameFixture();
    GameFixture(const GameFixture&) = delete;
    GameFixture& operator=(const GameFixture&) = delete;

    tmport::gg::GgGame& g() { return *game_; }
    tmport::gg::GgRuntime& runtime() { return runtime_; }
    tmport::gg::RecordingSound& sound() { return sound_; }
    tmport::gg::PlayersStore& players() { return *players_; }
    tmport::BorlandRand& rand() { return rand_; }
    const std::filesystem::path& prefDir() const { return prefDir_; }

    // ---- scenario set-up (on the test thread, while no function runs)
    // A PLAYERS.DAT file (e.g. a savestates/*.players copy) as the store's file (copied into the
    // scratch preference folder, then loaded). False if it is missing or not a valid PLAYERS.DAT.
    bool loadPlayers(const std::filesystem::path& playersDat);
    // Players_LoadRecord: GgState::player = the record of `slot`, g_rosterIndex = its index entry.
    // False when the slot is not in the roster.
    bool restorePlayer(int slot);
    // The same for the roster entry whose name is `name`. False when there is none.
    bool restorePlayerByName(std::string_view name);
    // A DOSBox-X state (GgStateFile::apply: GgState, the service-held globals, the screen, VRAM, the
    // DAC, the cursor; with its .players copy into the store when there is one). Returns what could not
    // be applied, or nullopt when the file is not a G&G state.
    std::optional<std::vector<std::string>> loadState(const std::filesystem::path& sav, bool withTime = true);
    // Scripted input in PIT ticks (dos::ScriptedInput's syntax: "10:down:right, +40:up:right",
    // "5:click:120:80"), relative to the current interrupt count when `relative`. False (and `error`)
    // when it does not parse.
    bool keys(std::string_view script, bool relative = true, std::string* error = nullptr);
    bool mouse(std::string_view script, bool relative = true, std::string* error = nullptr) {
        return keys(script, relative, error);
    }

    // ---- running a game function
    // Starts `fn(g())` on the worker; it runs at the first step. Only one function at a time.
    void start(std::function<void(tmport::gg::GgGame&)> fn);
    // Polls as boundaries (see the file comment).
    void polls(bool on) { polls_ = on; }
    // Runs up to `n` more boundaries (fewer when the function returns) and returns their records.
    std::vector<tmport::gg::GgTraceRecord> step(int n);
    // Steps until `pred` is true for a record (or `maxBoundaries` were run); returns the records.
    std::vector<tmport::gg::GgTraceRecord> runUntil(const std::function<bool(const tmport::gg::GgTraceRecord&)>& pred,
                                                    int maxBoundaries);
    // The function has returned (or thrown).
    bool done() const;
    // The last pause was a stall (kStallYields yields without a boundary).
    bool stalled() const { return stalled_; }
    // Stops the function at its next yield point (or lets a returned one go) and joins the worker. An
    // exception the function threw (other than the stop, GgRunEnd and GgExit) is rethrown here.
    void finish();
    // GgExit's code when the function ended through exit(), else nullopt.
    std::optional<int> exitCode() const { return exitCode_; }
    // Every record so far.
    const std::vector<tmport::gg::GgTraceRecord>& trace() const { return trace_; }
    // The records as text, one line per change (test messages).
    static std::string changes(std::span<const tmport::gg::GgTraceRecord> rows);

    // ---- pictures
    // Presents VRAM through the DAC now (Display::present), e.g. right after loadState.
    void present() { game_->display.present(); }
    // The last picture presented (320 x 200 RGBA, packed R | G << 8 | B << 16 | A << 24).
    std::vector<uint32_t> frame() const { return runtime_.frame(); }
    // Differing pixels between the last picture and a capture PNG (640 x 400 DOSBox-X shots are 2 x 2
    // blocks, or 320 x 200), outside the masks; -1 when the capture cannot be read.
    int differingPixels(const std::filesystem::path& capturePng, std::span<const Mask> masks = {}) const;

private:
    struct Stop {};
    void onYield(tmport::gg::YieldPoint p, int slot);
    void record(tmport::gg::YieldPoint p, int slot);
    void workerMain(std::function<void(tmport::gg::GgGame&)> fn);

    tmport::gg::GgRuntime runtime_;
    tmport::gg::RecordingSound sound_;
    tmport::BorlandRand rand_;
    std::filesystem::path prefDir_;
    bool ownPrefDir_ = false;
    std::unique_ptr<tmport::gg::PlayersStore> players_;
    std::unique_ptr<tmport::gg::GgGame> game_;

    enum class Phase : uint8_t { Idle, Waiting, Running, Paused, Done };
    mutable std::mutex m_;
    std::condition_variable cv_;
    Phase phase_ = Phase::Idle;
    int budget_ = 0;
    bool stopRequested_ = false;
    bool polls_ = false;
    bool stalled_ = false;
    uint64_t yieldsSinceBoundary_ = 0;
    std::thread worker_;
    std::thread::id workerId_;
    std::exception_ptr error_;
    std::optional<int> exitCode_;
    std::vector<tmport::gg::GgTraceRecord> trace_;
};

}  // namespace gggame
