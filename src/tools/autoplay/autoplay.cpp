#include "tools/autoplay/autoplay.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <mutex>
#include <thread>

#include "anim/anim_engine.h"
#include "app/app_context.h"
#include "app/game_main.h"
#include "audio/audio_system.h"
#include "game/game_main.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/room/room_run.h"
#include "game/room_table.h"
#include "game/rules.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"
#include "mischief/util/log.h"
#include "mischief/vfs/data_source.h"
#include "platform/engine.h"
#include "res/game_files.h"
#include "save/user_record.h"
#include "save/users_file.h"
#include "tools/autoplay/autoplay_checks.h"
#include "tools/autoplay/autoplay_rig.h"
#include "tools/autoplay/autoplay_vision.h"
#include "ui/game_dialog.h"

namespace tmport::autoplay {

namespace {

// --- process-wide diagnostics for the wall-clock watchdog and the crash handler ---------------
std::atomic<uint64_t> g_heartbeat{0};
std::mutex g_statusMutex;
char g_status[512] = "autoplay: starting";

void setStatus(const std::string& s) {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    std::snprintf(g_status, sizeof(g_status), "%s", s.c_str());
}

extern "C" void onCrashSignal(int sig) {
    // Best effort: the status buffer was written by the main thread (not async-signal-safe).
    std::fprintf(stderr, "\nautoplay: CRASH (signal %d) during: %s\n", sig, g_status);
    std::fflush(stderr);
    std::_Exit(5);
}

class Watchdog {
public:
    explicit Watchdog(int seconds) : seconds_(seconds) {
        if (seconds_ > 0) thread_ = std::thread([this] { loop(); });
    }
    ~Watchdog() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
    }

private:
    void loop() {
        uint64_t last = g_heartbeat.load();
        auto since = std::chrono::steady_clock::now();
        while (!stop_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const uint64_t now = g_heartbeat.load();
            if (now != last) {
                last = now;
                since = std::chrono::steady_clock::now();
                continue;
            }
            if (std::chrono::steady_clock::now() - since > std::chrono::seconds(seconds_)) {
                std::lock_guard<std::mutex> lock(g_statusMutex);
                std::fprintf(stderr, "\nautoplay: HANG: no engine pump for %d s (wall clock) during: %s\n", seconds_, g_status);
                std::fflush(stderr);
                std::_Exit(4);
            }
        }
    }
    int seconds_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

// Log warnings / errors of the engine during a run become notes of the run.
class LogTap {
public:
    explicit LogTap(std::vector<std::string>& out) : out_(out) {
        SDL_GetLogOutputFunction(&prevFn_, &prevData_);
        SDL_SetLogOutputFunction(&LogTap::output, this);
    }
    ~LogTap() { SDL_SetLogOutputFunction(prevFn_, prevData_); }
    LogTap(const LogTap&) = delete;
    LogTap& operator=(const LogTap&) = delete;

private:
    static void SDLCALL output(void* self, int category, SDL_LogPriority priority, const char* message) {
        auto* t = static_cast<LogTap*>(self);
        if (priority >= SDL_LOG_PRIORITY_WARN && t->out_.size() < 100)
            t->out_.push_back(std::format("log: {}", message ? message : ""));
        if (t->prevFn_ && (priority >= SDL_LOG_PRIORITY_WARN || mischief::log::verbose())) t->prevFn_(t->prevData_, category, priority, message);
    }
    std::vector<std::string>& out_;
    SDL_LogOutputFunction prevFn_ = nullptr;
    void* prevData_ = nullptr;
};

std::string defaultName(uint32_t seed) {
    // "Bot" + three letters from the seed (unique enough within one pref directory).
    std::string n = "Bot";
    uint32_t v = seed;
    for (int i = 0; i < 3; ++i) {
        n += static_cast<char>('a' + v % 26);
        v /= 26;
    }
    return n;
}

void writeCraftedPlayer(const std::filesystem::path& usersDat, const game::GameTables& t, const std::string& name,
                        uint32_t total, uint16_t climbs) {
    game::GameState x = game::GameState::initial(t);
    x.player.setName(name);
    game::initNewPlayer(x);
    x.rank.totalTreasures = total;
    x.rank.climbsDone = climbs;
    x.rank.rankTier = static_cast<uint8_t>(game::rankFromTotal(total, t) / 2);
    x.player.room = game::kRoomNewClimb;  // the next set-up generates a climb for this tier
    save::UsersTable table;
    if (auto old = mischief::readWholeFile(usersDat)) save::parseUsersFile(*old, table);
    table.recount();
    const int at = std::min(table.count, save::kSlotCount - 1);
    game::toPlayerRecord(x).writeTo(table.slots[static_cast<size_t>(at)]);
    table.recount();
    table.sortByTotal();
    if (!save::writeUsersFile(usersDat, table)) throw mischief::Error("cannot write " + mischief::pathToUtf8(usersDat));
}

ResourceSample sample(Rig& rig, int climb) {
    ResourceSample r;
    r.climb = climb;
    r.hotspots = rig.engine().hotspots().count();
    for (const auto& e : rig.engine().hotspots().slots())
        if (e.id != 0) r.hotspotIds += std::format("{}{:#x}", r.hotspotIds.empty() ? "" : " ", e.id);
    r.animFrameSlots = rig.anim().frameSlots();
    r.dialogPauseDepth = rig.dialogs().musicPauseDepth();
    r.audioPauseDepth = rig.audio() ? rig.audio()->pauseDepth() : 0;
    r.pendingInput = rig.engine().input().pendingMessages();
    r.postedEvents = rig.engine().pendingPostedEvents();
    return r;
}

}  // namespace

RunResult runAutoplay(GameFiles& files, const RunOptions& o) {
    RunResult res;
    res.seed = o.seed;
    const auto wall0 = std::chrono::steady_clock::now();
    std::error_code ec;
    std::filesystem::remove_all(o.prefDir, ec);
    std::filesystem::create_directories(o.prefDir, ec);

    const game::GameTables tables = game::GameTables::load(*files.exe, *files.res);
    Checker checker(tables);
    std::string name = o.name.empty() ? defaultName(o.seed) : o.name;
    bool existing = false;
    if (o.craftedTotal) {
        writeCraftedPlayer(o.prefDir / "users.dat", tables, name, *o.craftedTotal, o.craftedClimbs);
        existing = true;
    }
    std::mt19937 rng(o.seed ^ 0x5eedu);
    int remaining = o.climbs;
    std::vector<uint8_t> resume;
    LogTap tap(res.notes);
    auto lastSampleWall = std::chrono::steady_clock::now();
    for (int phase = 0;; ++phase) {
        ++res.phases;
        RigOptions ro;
        ro.prefDir = o.prefDir;
        ro.manualClock = o.manualClock;
        ro.clockStart = 1000 + (static_cast<uint64_t>(o.seed) * 7919u + static_cast<uint64_t>(phase) * 104729u) % 65536u;
        ro.audio = o.audio;
        Rig rig(files, ro);
        BotOptions bo;
        bo.seed = o.seed * 31u + static_cast<uint32_t>(phase);
        bo.climbs = remaining;
        bo.name = name;
        bo.existingPlayer = existing;
        bo.wrongAnswerRate = o.wrongAnswerRate;
        bo.escQuestionRate = o.escQuestionRate;
        bo.skipAnimRate = o.skipAnimRate;
        bo.menuExcursions = o.menuExcursions;
        bo.exhaustLevel = o.exhaustLevel;
        bo.tickLog = o.tickLog;
        bo.autorepeat = o.autorepeat;
        bo.maxClimbGameMs = o.maxClimbGameMinutes * 60 * 1000;
        bo.log = o.log;
        if (phase == 0 && o.restartTest) bo.restartAtTick = 150 + static_cast<int>(rng() % 900);
        if (phase == 0 && o.castleExitTest) bo.exitInCastleAtTick = 5 + static_cast<int>(rng() % 60);
        if (phase == 0 && o.exitTest) bo.exitAtTick = 200 + static_cast<int>(rng() % 1500) + (o.restartTest ? 1200 : 0);
        Bot bot(rig, checker, bo);
        if (!resume.empty()) bot.expectResume(resume, "Exit and a new run");
        const int hook = rig.engine().addPumpHook([&](PumpPhase) {
            const uint64_t hb = g_heartbeat.fetch_add(1) + 1;
            if ((hb & 0x3FFF) == 0) setStatus(std::format("seed {} phase {}: {}", o.seed, phase, bot.describe()));
        });
        game::GameMainOptions go;
        go.seed = o.gameSeed;
        int climbAtSetup = 0;
        go.trace = [&](std::string_view l) {
            if (l.starts_with("S8 climb set-up")) {
                ResourceSample s = sample(rig, res.stats.climbsDone + bot.stats().climbsDone + climbAtSetup);
                const auto now = std::chrono::steady_clock::now();
                s.wallSeconds = std::chrono::duration<double>(now - lastSampleWall).count();
                s.resumed = l.find("resumed climb") != std::string_view::npos;
                lastSampleWall = now;
                res.resources.push_back(s);
                setStatus(std::format("seed {} phase {}: {}", o.seed, phase, l));
            }
            bot.onTrace(l);
        };
        std::string abortWhy;
        try {
            app::runGameMain(rig.ctx(), [&go](AppContext& c) { game::gameMain(c, go); });
        } catch (const AutoplayAbort& a) {
            abortWhy = a.why;
        } catch (const std::exception& e) {
            abortWhy = std::format("exception out of the game: {} [{}]", e.what(), bot.describe());
        }
        rig.engine().removePumpHook(hook);
        // Accumulate.
        const BotStats& b = bot.stats();
        BotStats& t = res.stats;
        t.climbsDone += b.climbsDone;
        t.roomTicks += b.roomTicks;
        t.castleTicks += b.castleTicks;
        t.tosses += b.tosses;
        t.tossMisses += b.tossMisses;
        t.treasures += b.treasures;
        t.keys += b.keys;
        t.shopCoins += b.shopCoins;
        t.shopBuys += b.shopBuys;
        t.swings += b.swings;
        t.catches += b.catches;
        t.questions += b.questions;
        t.correct += b.correct;
        t.wrong += b.wrong;
        t.escapes += b.escapes;
        t.hints += b.hints;
        t.cutscenes += b.cutscenes;
        t.gates += b.gates;
        t.piles += b.piles;
        t.pushes += b.pushes;
        t.falls += b.falls;
        t.jumps += b.jumps;
        t.bumps += b.bumps;
        t.restarts += b.restarts;
        t.exits += b.exits;
        t.resumes += b.resumes;
        t.exhausted += b.exhausted;
        t.keysWaited += b.keysWaited;
        t.sparkleHits += b.sparkleHits;
        t.menuTrips += b.menuTrips;
        t.dialogs += b.dialogs;
        t.unexpectedDialogs += b.unexpectedDialogs;
        t.maxCoins = std::max(t.maxCoins, b.maxCoins);
        t.maxNets = std::max(t.maxNets, b.maxNets);
        t.minCoins = std::min(t.minCoins, b.minCoins);
        t.champion += b.champion;
        t.promotions += b.promotions;
        if (b.climbsDone > 0) {
            t.finalTotal = b.finalTotal;
            t.finalRank = b.finalRank;
        }
        res.gameMs += rig.clock().nowMs() - ro.clockStart;
        res.playerName = rig.state().player.nameString();
        if (!abortWhy.empty()) {
            res.abortReason = abortWhy;
            res.recent = bot.recent();
            break;
        }
        if (bot.end() == BotEnd::ExitedMidClimb && !bot.leftRecord().empty()) {
            resume = bot.leftRecord();
            remaining -= b.climbsDone;
            name = rig.state().player.nameString();
            existing = true;
            if (phase > 3) {
                res.abortReason = "too many program runs";
                break;
            }
            continue;
        }
        if (bot.end() != BotEnd::Finished && b.climbsDone < remaining) {
            res.abortReason = std::format("the game ended early ({} of {} climbs) [{}]", b.climbsDone, remaining, bot.describe());
            res.recent = bot.recent();
        }
        break;
    }
    res.violations = checker.violations();
    for (const auto& n : checker.notes()) res.notes.push_back(n);
    res.checks = checker.checks();
    res.saveChecks = checker.saveChecks();
    // Resource growth: the counters at the set-up of every new climb after the first must stay
    // flat (the first climb registers the clue box's hot spots for good; a sign-in clears them).
    std::vector<const ResourceSample*> flat;
    for (const ResourceSample& s : res.resources)
        if (s.climb >= 1 && !s.resumed) flat.push_back(&s);
    if (flat.size() >= 2) {
        const ResourceSample& a = *flat.front();
        const ResourceSample& z = *flat.back();
        if (z.hotspots > a.hotspots)
            res.violations.push_back(std::format("resources: hot spots at climb set-up grew {} -> {} ({} -> {})", a.hotspots,
                                                 z.hotspots, a.hotspotIds, z.hotspotIds));
        if (z.animFrameSlots > a.animFrameSlots + 64)
            res.violations.push_back(std::format("resources: DAM frame slots grew {} -> {}", a.animFrameSlots, z.animFrameSlots));
        if (z.dialogPauseDepth != 0 || z.audioPauseDepth != 0)
            res.violations.push_back(std::format("resources: pause depth at climb set-up dialog {} audio {}", z.dialogPauseDepth, z.audioPauseDepth));
    }
    for (const ResourceSample& s : res.resources)
        if (s.pendingInput > 64 || s.postedEvents > 64)
            res.violations.push_back(std::format("resources: {} queued messages / {} posted events at climb set-up {}", s.pendingInput,
                                                 s.postedEvents, s.climb));
    res.wallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
    res.ok = res.abortReason.empty() && res.violations.empty() && res.stats.climbsDone >= o.climbs;
    if (res.ok) std::filesystem::remove_all(o.prefDir, ec);
    return res;
}

std::string summarize(const RunResult& r) {
    const BotStats& s = r.stats;
    return std::format(
        "seed {:>4} {} climbs {} total {} rank {}{} | ticks {} castle {} game {:.0f} min wall {:.1f} s | toss {} miss {} treasure {} "
        "key {} shop {}/{} | swing {} catch {} quiz {} ok {} wrong {} esc {} | hints {} gates {} piles {} pushes {} falls {} "
        "jumps {} bumps {} sparkle {} | restart {} exit {} resume {} spent {} menu {} dialogs {} (unexpected {}) waited {} | coins {}..{} "
        "nets ..{} | "
        "checks {} saves {} "
        "runs {} | violations {} notes {}",
        r.seed, r.ok ? "OK  " : "FAIL", s.climbsDone, s.finalTotal, s.finalRank, s.champion ? " CHAMPION" : "", s.roomTicks,
        s.castleTicks, static_cast<double>(r.gameMs) / 60000.0, r.wallSeconds, s.tosses, s.tossMisses, s.treasures, s.keys,
        s.shopBuys, s.shopCoins, s.swings, s.catches, s.questions, s.correct, s.wrong, s.escapes, s.hints, s.gates, s.piles,
        s.pushes, s.falls, s.jumps, s.bumps, s.sparkleHits, s.restarts, s.exits, s.resumes, s.exhausted, s.menuTrips, s.dialogs, s.unexpectedDialogs, s.keysWaited,
        s.minCoins == (1 << 30) ? 0 : s.minCoins, s.maxCoins, s.maxNets, r.checks, r.saveChecks, r.phases, r.violations.size(),
        r.notes.size());
}

// ------------------------------------------------------------------------------ vision check

namespace {

struct StopVision {};

int runVisionCheck(GameFiles& files, int seeds, int ticks, bool verbose) {
    struct Acc {
        int samples = 0, detected = 0, falseBlobs = 0, blobs = 0;
        std::vector<int> err;
    } acc;
    const auto prefDir = std::filesystem::temp_directory_path() / "tmport_autoplay" / "vision";
    for (int seed = 1; seed <= seeds; ++seed) {
        RigOptions ro;
        ro.prefDir = prefDir;
        ro.audio = false;
        Rig rig(files, ro);
        game::GameState& s = rig.state();
        const game::GameTables& t = rig.tables();
        s.player.setName("Vision");
        game::initNewPlayer(s);
        game::room::RoomLoop loop(rig.ctx(), s, t, rig.engine().rng());
        rig.engine().rng().srand(static_cast<uint16_t>(seed));
        loop.loadCommon();
        game::setupClimb(s, t, rig.engine().rng(), loop.gameHooks());
        // The room where most elves are, the player at its start pose on the left.
        std::array<int, game::kRoomCount> per{};
        for (int i = 0; i < loop.elves().count(); ++i) {
            const auto& e = loop.elves().elves()[static_cast<size_t>(i)];
            if (game::RoomTable::valid(e.room)) ++per[static_cast<size_t>(e.room)];
        }
        const int room = static_cast<int>(std::max_element(per.begin(), per.end()) - per.begin());
        s.player.room = static_cast<uint8_t>(room);
        s.player.x = 20;
        s.hints.unseen = 0;
        loop.startClimb();
        loop.hooks().afterTick = [&](uint64_t tick) {
            const auto& p = s.player;
            std::vector<VisRect> masks = {{p.x - 10, 0, std::max<int>(p.w, 40) + 20, 400}};
            if (s.coinPile.room == p.room) masks.push_back({s.coinPile.x - 4, 0, s.coinPile.w + 8, 400});
            if (p.room == game::room::kFountainRoom)
                masks.push_back({game::room::kFountainX - 4, game::room::kFountainY - 4, game::room::kFountainW + 8, game::room::kFountainH + 8});
            for (const auto& g : game::room::kGateLocks)
                if (g.room == p.room) masks.push_back({g.x - 4, g.y - 4, game::room::kGateLockW + 8, game::room::kGateLockH + 8});
            const auto blobs = findSprites(rig.engine().frame(), masks);
            acc.blobs += static_cast<int>(blobs.size());
            std::vector<bool> used(blobs.size(), false);
            for (int i = 0; i < loop.elves().count(); ++i) {
                const auto& e = loop.elves().elves()[static_cast<size_t>(i)];
                if (e.room != p.room || e.inNet || e.x < -20 || e.x > 480) continue;
                if (e.x + 60 > p.x - 10 && e.x + 20 < p.x + p.w + 10) continue;  // behind the player mask
                ++acc.samples;
                int bestD = 1 << 20, bi = -1;
                for (size_t b = 0; b < blobs.size(); ++b) {
                    const int d = estimateElfX(blobs[b]) - e.x;
                    if (std::abs(d) < std::abs(bestD)) {
                        bestD = d;
                        bi = static_cast<int>(b);
                    }
                }
                if (bi >= 0 && std::abs(bestD) <= 30) {
                    ++acc.detected;
                    acc.err.push_back(bestD);
                    used[static_cast<size_t>(bi)] = true;
                }
                if (verbose && bi >= 0)
                    std::printf("seed %d tick %llu elf %d set %d x %d facing %d -> blob %d..%d est %d (err %d)\n", seed,
                                static_cast<unsigned long long>(tick), i, e.set, e.x, e.facing, blobs[static_cast<size_t>(bi)].x0,
                                blobs[static_cast<size_t>(bi)].x1, estimateElfX(blobs[static_cast<size_t>(bi)]), bestD);
            }
            for (size_t b = 0; b < blobs.size(); ++b)
                if (!used[b]) ++acc.falseBlobs;
            if (static_cast<int>(tick) >= ticks) throw StopVision{};
        };
        try {
            loop.run();
        } catch (const StopVision&) {
        }
        loop.resetForRestart();
    }
    std::sort(acc.err.begin(), acc.err.end());
    double mean = 0;
    for (int e : acc.err) mean += e;
    if (!acc.err.empty()) mean /= static_cast<double>(acc.err.size());
    std::printf("vision check: %d elf samples, %d detected within 30 px (%.1f%%), error mean %.1f median %d min %d max %d; "
                "%d blobs, %d not matched to a visible elf\n",
                acc.samples, acc.detected, acc.samples ? 100.0 * acc.detected / acc.samples : 0.0, mean,
                acc.err.empty() ? 0 : acc.err[acc.err.size() / 2], acc.err.empty() ? 0 : acc.err.front(),
                acc.err.empty() ? 0 : acc.err.back(), acc.blobs, acc.falseBlobs);
    std::error_code ec;
    std::filesystem::remove_all(prefDir, ec);
    return acc.samples > 0 && acc.detected * 10 >= acc.samples * 8 ? 0 : 1;
}

bool parseRange(std::string_view v, uint32_t& a, uint32_t& b) {
    const size_t dash = v.find('-');
    try {
        if (dash == std::string_view::npos) {
            a = b = static_cast<uint32_t>(std::stoul(std::string(v)));
        } else {
            a = static_cast<uint32_t>(std::stoul(std::string(v.substr(0, dash))));
            b = static_cast<uint32_t>(std::stoul(std::string(v.substr(dash + 1))));
        }
    } catch (...) {
        return false;
    }
    return a <= b;
}

}  // namespace

std::string autoplayUsage() {
    return "Autoplay soak test (area autoplay): plays the real game headless, as a bot, and checks it\n"
           "  tmport --autoplay [--seed N | --seeds A-B] [--climbs N] [--soak] [--fast | --real-clock]\n"
           "         [--restart-test] [--exit-test] [--castle-exit-test] [--menus] [--exhaust LEVEL] [--champion]\n"
           "         [--crafted-total T] [--crafted-climbs C]\n"
           "         [--game-seed S] [--name NAME] [--wrong-rate R] [--no-audio] [--pref-dir DIR] [--trace]\n"
           "         [--watchdog SECONDS] [--max-climb-minutes M] [--data PATH] [-v]\n"
           "    --soak          per seed: Start Another Game (seed%4==1), Exit + resume (seed%4==2), both (3),\n"
           "                    a near-champion crafted player (seed%5==0), menu trips (odd seeds), out of coins\n"
           "                    and nets on purpose on level (seed/3)%3 (seed%3==0: the rescue coin pile), Exit\n"
           "                    on the castle wall + resume (seed%7==3)\n"
           "    --autorepeat N  N extra arrow key-down repeats per tick while an arrow is held (a keyboard's\n"
           "                    autorepeat; the bot then waits for queued input codes before Enter / Space)\n"
           "    --trace-ticks   --trace plus the pose and the action of every room tick\n"
           "    --champion      crafted player with total 290 and 40 climbs (tier 3; Champion after one climb)\n"
           "    --fast          manual clock (default): the game runs as fast as the CPU allows\n"
           "    --trace         print the game's state-machine trace and the bot's decisions\n"
           "    exit code 0: all runs clean; 1: a violation, stall or failed run; 2: usage; 3: game files;\n"
           "              4: wall-clock hang (watchdog); 5: crash signal\n"
           "  tmport --autoplay-vision-check [--seeds N] [--ticks N] [-v]   elf detection vs the room loop's elf table\n";
}

std::optional<int> runAutoplayCommand(int argc, char** argv) {
    bool autoplay = false, vision = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--autoplay") autoplay = true;
        if (a == "--autoplay-vision-check") vision = true;
    }
    if (!autoplay && !vision) return std::nullopt;

    RunOptions base;
    uint32_t seedA = 1, seedB = 1;
    bool soak = false, champion = false, trace = false, verbose = false;
    int watchdogSeconds = 120, visionSeeds = 6, visionTicks = 400;
    std::string dataPath, prefBase;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto need = [&](const char* what) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(std::format("{} needs {}", a, what));
            return argv[++i];
        };
        try {
            if (a == "--autoplay" || a == "--autoplay-vision-check") continue;
            if (a == "--seed") {
                seedA = seedB = static_cast<uint32_t>(std::stoul(need("a number")));
            } else if (a == "--seeds") {
                const std::string v = need("A-B");
                if (vision) {
                    visionSeeds = std::stoi(v);
                } else if (!parseRange(v, seedA, seedB)) {
                    throw std::runtime_error("--seeds needs A-B");
                }
            } else if (a == "--ticks") visionTicks = std::stoi(need("a number"));
            else if (a == "--climbs") base.climbs = std::stoi(need("a number"));
            else if (a == "--soak") soak = true;
            else if (a == "--fast") base.manualClock = true;
            else if (a == "--real-clock") base.manualClock = false;
            else if (a == "--restart-test") base.restartTest = true;
            else if (a == "--exit-test") base.exitTest = true;
            else if (a == "--castle-exit-test") base.castleExitTest = true;
            else if (a == "--menus") base.menuExcursions = true;
            else if (a == "--exhaust") base.exhaustLevel = std::stoi(need("a level 0..2"));
            else if (a == "--champion") champion = true;
            else if (a == "--crafted-total") base.craftedTotal = static_cast<uint32_t>(std::stoul(need("a number")));
            else if (a == "--crafted-climbs") base.craftedClimbs = static_cast<uint16_t>(std::stoul(need("a number")));
            else if (a == "--game-seed") base.gameSeed = static_cast<uint16_t>(std::stoul(need("a number")));
            else if (a == "--name") base.name = need("a name");
            else if (a == "--wrong-rate") base.wrongAnswerRate = std::stod(need("a rate"));
            else if (a == "--no-audio") base.audio = false;
            else if (a == "--pref-dir") prefBase = need("a directory");
            else if (a == "--trace") trace = true;
            else if (a == "--trace-ticks") trace = base.tickLog = true;
            else if (a == "--autorepeat") base.autorepeat = std::stoi(need("repeats per tick"));
            else if (a == "--watchdog") watchdogSeconds = std::stoi(need("seconds"));
            else if (a == "--max-climb-minutes") base.maxClimbGameMinutes = static_cast<uint64_t>(std::stoul(need("minutes")));
            else if (a == "--data") dataPath = need("a path");
            else if (a == "-v" || a == "--verbose") verbose = true;
            else throw std::runtime_error(std::format("unknown option '{}'", a));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "tmport: %s\n\n%s", e.what(), autoplayUsage().c_str());
            return 2;
        }
    }
    mischief::log::setVerbose(verbose);
    if (dataPath.empty())
        if (auto env = mischief::getEnv("TM_DATA")) dataPath = *env;
    if (dataPath.empty())
        if (auto p = mischief::vfs::autoDetectDataPath(std::filesystem::current_path())) dataPath = mischief::pathToUtf8(*p);
    if (dataPath.empty()) {
        std::fprintf(stderr, "tmport: the game data was not found. Pass --data <TMTWINCD folder or CD image> or set TM_DATA.\n");
        return 3;
    }
    GameFiles files;
    try {
        ResourceManager::Options ro;
        ro.requireSounds = true;
        files = GameFiles::open(mischief::pathFromUtf8(dataPath), ro);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tmport: cannot open the game data: %s\n", e.what());
        return 3;
    }
    if (vision) return runVisionCheck(files, visionSeeds, visionTicks, verbose);

    std::signal(SIGSEGV, onCrashSignal);
    std::signal(SIGABRT, onCrashSignal);
    std::signal(SIGFPE, onCrashSignal);
    std::signal(SIGILL, onCrashSignal);
    Watchdog watchdog(watchdogSeconds);
    const std::filesystem::path base0 =
        prefBase.empty() ? std::filesystem::temp_directory_path() / "tmport_autoplay" : mischief::pathFromUtf8(prefBase);
    int failures = 0, runs = 0;
    for (uint32_t seed = seedA; seed <= seedB; ++seed) {
        RunOptions o = base;
        o.seed = seed;
        o.prefDir = base0 / std::format("seed_{}", seed);
        if (soak) {
            o.restartTest = o.restartTest || seed % 4 == 1 || seed % 4 == 3;
            o.exitTest = o.exitTest || seed % 4 == 2 || seed % 4 == 3;
            o.menuExcursions = o.menuExcursions || seed % 2 == 1;
            o.castleExitTest = o.castleExitTest || seed % 7 == 3;
            if (seed % 3 == 0 && o.exhaustLevel < 0) o.exhaustLevel = static_cast<int>(seed / 3 % 3);
            if (seed % 5 == 0 && !o.craftedTotal) {
                o.craftedTotal = 290;
                o.craftedClimbs = 40;
            }
        }
        if (champion && !o.craftedTotal) {
            o.craftedTotal = 290;
            o.craftedClimbs = 40;
        }
        if (trace) o.log = [](std::string_view l) { std::printf("%.*s\n", static_cast<int>(l.size()), l.data()); };
        setStatus(std::format("seed {}: starting", seed));
        RunResult r;
        try {
            r = runAutoplay(files, o);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "tmport: seed %u: %s\n", seed, e.what());
            ++failures;
            continue;
        }
        ++runs;
        std::printf("%s\n", summarize(r).c_str());
        if (!r.ok) {
            ++failures;
            if (!r.abortReason.empty()) std::printf("  abort: %s\n", r.abortReason.c_str());
            for (const auto& v : r.violations) std::printf("  violation: %s\n", v.c_str());
            for (const auto& l : r.recent) std::printf("  | %s\n", l.c_str());
            std::printf("  (pref dir kept: %s)\n", mischief::pathToUtf8(o.prefDir).c_str());
        }
        for (const auto& n : r.notes) std::printf("  note: %s\n", n.c_str());
        if (verbose)
            for (const auto& s : r.resources)
                std::printf("  resources at climb %d: hot spots %d, DAM frame slots %zu, pause depth %d/%d, queued %zu/%zu, %.2f s\n",
                            s.climb, s.hotspots, s.animFrameSlots, s.dialogPauseDepth, s.audioPauseDepth, s.pendingInput,
                            s.postedEvents, s.wallSeconds);
        std::fflush(stdout);
    }
    std::printf("autoplay: %d run(s), %d failed\n", runs, failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace tmport::autoplay
