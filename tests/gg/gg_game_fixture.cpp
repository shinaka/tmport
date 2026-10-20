#include "gg_game_fixture.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <system_error>

#include "dos/app/script_input.h"
#include "gg/app/seq_run.h"
#include "gg/save/gg_state_file.h"
#include "gg/save/players_dat.h"
#include "mods/png_codec.h"
#include "util/file_util.h"

using namespace tmport;
using namespace tmport::gg;

namespace gggame {

namespace {

std::filesystem::path uniquePrefDir() {
    static std::atomic<uint32_t> counter{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::error_code ec;
    auto dir = std::filesystem::temp_directory_path(ec);
    if (ec) dir = std::filesystem::current_path();
    dir /= std::format("ggtests_game_{:x}_{}", static_cast<uint64_t>(now), counter.fetch_add(1));
    std::filesystem::create_directories(dir, ec);
    return dir;
}

RuntimeConfig headlessConfig() {
    RuntimeConfig rc;
    rc.deterministic = true;
    return rc;
}

}  // namespace

GameFixture::GameFixture(const GgFiles& files, FixtureOptions o) : runtime_(headlessConfig(), nullptr) {
    prefDir_ = o.prefDir;
    if (prefDir_.empty()) {
        prefDir_ = uniquePrefDir();
        ownPrefDir_ = true;
    }
    players_ = std::make_unique<PlayersStore>(playersPathIn(prefDir_));
    players_->load();  // Roster_LoadOrCreate's file (created in the scratch folder)
    runtime_.installHandlers();
    rand_.srand(o.seed);
    o.game.seed = o.seed;
    game_ = std::make_unique<GgGame>(files, runtime_, sound_, *players_, rand_, o.game);
    runtime_.setYieldHook([this](YieldPoint p, int slot) { onYield(p, slot); });
}

GameFixture::~GameFixture() {
    try {
        finish();
    } catch (...) {
        // a test that wanted the error called finish() itself
    }
    runtime_.setYieldHook({});
    game_.reset();
    runtime_.removeHandlers();
    if (ownPrefDir_) {
        std::error_code ec;
        std::filesystem::remove_all(prefDir_, ec);
    }
}

// ---------------------------------------------------------------------------------- set-up

bool GameFixture::loadPlayers(const std::filesystem::path& playersDat) {
    const auto bytes = readWholeFile(playersDat);
    if (!bytes || !isValidPlayersDat(*bytes)) return false;
    if (importPlayers(playersDat, players_->path(), true) != PlayersImport::Imported) return false;
    players_->load();
    game_->ui.roster = players_->roster();
    return true;
}

bool GameFixture::restorePlayer(int slot) {
    const Roster& r = game_->ui.roster;
    for (int i = 0; i < r.count; ++i) {
        if (r.entries[static_cast<size_t>(i)].slot != slot) continue;
        game_->player = players_->loadRecord(slot);
        game_->ui.rosterIndex = static_cast<int16_t>(i);
        return true;
    }
    return false;
}

bool GameFixture::restorePlayerByName(std::string_view name) {
    const Roster& r = game_->ui.roster;
    for (int i = 0; i < r.count; ++i)
        if (r.entries[static_cast<size_t>(i)].nameString() == name) return restorePlayer(r.entries[static_cast<size_t>(i)].slot);
    return false;
}

std::optional<std::vector<std::string>> GameFixture::loadState(const std::filesystem::path& sav, bool withTime) {
    std::optional<GgStateFile> s;
    try {
        s.emplace(GgStateFile::load(sav));
    } catch (const std::exception&) {
        return std::nullopt;
    }
    std::error_code ec;
    if (const auto players = GgStateFile::playersFileOf(sav); std::filesystem::is_regular_file(players, ec)) loadPlayers(players);
    return s->apply(*game_, withTime);
}

bool GameFixture::keys(std::string_view script, bool relative, std::string* error) {
    std::string err;
    const auto parsed = dos::ScriptedInput::parse(script, err);
    if (!parsed) {
        if (error) *error = err;
        return false;
    }
    runtime_.addScript(*parsed, relative ? runtime_.clock().interrupts() : 0);
    return true;
}

// ---------------------------------------------------------------------------------- running

void GameFixture::start(std::function<void(GgGame&)> fn) {
    finish();
    {
        std::lock_guard lk(m_);
        phase_ = Phase::Waiting;
        budget_ = 0;
        stopRequested_ = false;
        stalled_ = false;
        error_ = nullptr;
        exitCode_.reset();
        yieldsSinceBoundary_ = 0;
    }
    worker_ = std::thread([this, fn = std::move(fn)]() mutable { workerMain(std::move(fn)); });
}

void GameFixture::workerMain(std::function<void(GgGame&)> fn) {
    {
        std::unique_lock lk(m_);
        workerId_ = std::this_thread::get_id();
        cv_.wait(lk, [this] { return phase_ == Phase::Running || stopRequested_; });
        if (stopRequested_) {
            phase_ = Phase::Done;
            cv_.notify_all();
            return;
        }
    }
    try {
        fn(*game_);
    } catch (const Stop&) {
    } catch (const GgRunEnd&) {
    } catch (const GgExit& e) {
        exitCode_ = e.code;
    } catch (...) {
        error_ = std::current_exception();
    }
    std::lock_guard lk(m_);
    phase_ = Phase::Done;
    cv_.notify_all();
}

void GameFixture::onYield(YieldPoint p, int slot) {
    if (std::this_thread::get_id() != workerId_) return;  // the test thread calling the game directly
    ++yieldsSinceBoundary_;
    const bool boundary = (p == YieldPoint::TimerWait && slot == 0) ||
                          (polls_ && (p == YieldPoint::GetInputCode || p == YieldPoint::Idle || p == YieldPoint::TimerExpired));
    const bool stall = !boundary && yieldsSinceBoundary_ >= kStallYields;
    if (!boundary && !stall) return;
    yieldsSinceBoundary_ = 0;
    record(p, slot);
    std::unique_lock lk(m_);
    stalled_ = stall;
    if (--budget_ > 0 && !stall) return;
    phase_ = Phase::Paused;
    cv_.notify_all();
    cv_.wait(lk, [this] { return phase_ == Phase::Running || stopRequested_; });
    if (stopRequested_) throw Stop{};
}

void GameFixture::record(YieldPoint p, int slot) {
    GgTraceRecord r = traceSnapshot(*game_);
    r.frame = trace_.size() + 1;
    r.yield = p;
    r.slot = (p == YieldPoint::TimerWait || p == YieldPoint::TimerExpired) ? slot : -1;
    r.sound = RecordingSound::describe(sound_.takeCalls());
    trace_.push_back(std::move(r));
}

std::vector<GgTraceRecord> GameFixture::step(int n) {
    std::unique_lock lk(m_);
    if (n <= 0 || (phase_ != Phase::Waiting && phase_ != Phase::Paused)) return {};
    const size_t from = trace_.size();
    budget_ = n;
    stalled_ = false;
    phase_ = Phase::Running;
    cv_.notify_all();
    cv_.wait(lk, [this] { return phase_ == Phase::Paused || phase_ == Phase::Done; });
    return std::vector<GgTraceRecord>(trace_.begin() + static_cast<std::ptrdiff_t>(from), trace_.end());
}

std::vector<GgTraceRecord> GameFixture::runUntil(const std::function<bool(const GgTraceRecord&)>& pred, int maxBoundaries) {
    std::vector<GgTraceRecord> out;
    for (int i = 0; i < maxBoundaries && !done(); ++i) {
        const auto rows = step(1);
        out.insert(out.end(), rows.begin(), rows.end());
        if (!rows.empty() && pred(rows.back())) break;
        if (rows.empty()) break;
    }
    return out;
}

bool GameFixture::done() const {
    std::lock_guard lk(m_);
    return phase_ == Phase::Done || phase_ == Phase::Idle;
}

void GameFixture::finish() {
    if (worker_.joinable()) {
        {
            std::lock_guard lk(m_);
            stopRequested_ = true;
            cv_.notify_all();
        }
        worker_.join();
    }
    std::exception_ptr err;
    {
        std::lock_guard lk(m_);
        phase_ = Phase::Idle;
        workerId_ = {};
        err = error_;
        error_ = nullptr;
    }
    if (err) std::rethrow_exception(err);
}

std::string GameFixture::changes(std::span<const GgTraceRecord> rows) {
    std::string out;
    const GgTraceRecord* prev = nullptr;
    for (const GgTraceRecord& r : rows) {
        out += traceChanges(prev, r);
        prev = &r;
    }
    return out;
}

// ---------------------------------------------------------------------------------- pictures

int GameFixture::differingPixels(const std::filesystem::path& capturePng, std::span<const Mask> masks) const {
    const auto bytes = readWholeFile(capturePng);
    if (!bytes) return -1;
    mods::DecodedPng png;
    try {
        png = mods::decodePng(*bytes);
    } catch (const std::exception&) {
        return -1;
    }
    const int step = png.width == 640 ? 2 : 1;
    if (png.width != 320 * step || png.height != 200 * step) return -1;
    const std::vector<uint32_t> f = frame();
    if (f.size() != 320u * 200u) return -1;
    int n = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            if (std::any_of(masks.begin(), masks.end(), [&](const Mask& m) { return x >= m.x0 && x <= m.x1 && y >= m.y0 && y <= m.y1; }))
                continue;
            const uint32_t p = f[static_cast<size_t>(y) * 320 + static_cast<size_t>(x)];
            const uint8_t* c = &png.rgba[(static_cast<size_t>(y * step) * static_cast<size_t>(png.width) + static_cast<size_t>(x * step)) * 4];
            n += (p & 0xFF) != c[0] || ((p >> 8) & 0xFF) != c[1] || ((p >> 16) & 0xFF) != c[2];
        }
    return n;
}

}  // namespace gggame
