// The DOS edition's autoplayer and soak test (dos_autoplay.h).
#include "dos/autoplay/dos_autoplay.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <mutex>
#include <thread>

#include "dos/app/dos_runtime.h"
#include "dos/autoplay/dos_autoplay_checks.h"
#include "dos/autoplay/dos_autoplay_rig.h"
#include "dos/autoplay/dos_autoplay_vision.h"
#include "dos/game/game.h"
#include "dos/gfx/dos_screens.h"
#include "dos/save/users_store.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"
#include "mischief/util/log.h"

namespace tmport::dos::autoplay {

namespace {

// --- process-wide diagnostics for the wall-clock watchdog and the crash handler ---------------
std::atomic<uint64_t> g_heartbeat{0};
std::mutex g_statusMutex;
char g_status[512] = "dos autoplay: starting";

void setStatus(const std::string& s) {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    std::snprintf(g_status, sizeof(g_status), "%s", s.c_str());
}

extern "C" void dosAutoplayOnCrashSignal(int sig) {
    // Best effort: the status buffer was written by the main thread (not async-signal-safe).
    std::fprintf(stderr, "\ndos autoplay: CRASH (signal %d) during: %s\n", sig, g_status);
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
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

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
                std::fprintf(stderr, "\ndos autoplay: HANG: no game tick for %d s (wall clock) during: %s\n", seconds_, g_status);
                std::fflush(stderr);
                std::_Exit(4);
            }
        }
    }
    int seconds_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

// Log warnings and errors of the engine during a run become notes of the run.
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
        if (priority >= SDL_LOG_PRIORITY_WARN && t->out_.size() < 100) t->out_.push_back(std::format("log: {}", message ? message : ""));
        if (t->prevFn_ && (priority >= SDL_LOG_PRIORITY_WARN || mischief::log::verbose())) t->prevFn_(t->prevData_, category, priority, message);
    }
    std::vector<std::string>& out_;
    SDL_LogOutputFunction prevFn_ = nullptr;
    void* prevData_ = nullptr;
};

std::string defaultName(uint32_t seed) {
    // "Bot" + three letters of the seed (unique enough within one preference folder).
    std::string n = "Bot";
    uint32_t v = seed;
    for (int i = 0; i < 3; ++i) {
        n += static_cast<char>('a' + v % 26);
        v /= 26;
    }
    return n;
}

void accumulate(BotStats& t, const BotStats& b) {
    t.climbsDone += b.climbsDone;
    t.ticks += b.ticks;
    t.playTicks += b.playTicks;
    t.castleTicks += b.castleTicks;
    t.drops += b.drops;
    t.treasures += b.treasures;
    t.keys += b.keys;
    t.emptySearches += b.emptySearches;
    t.shopCoins += b.shopCoins;
    t.shopVisits += b.shopVisits;
    t.swings += b.swings;
    t.catches += b.catches;
    t.carriers += b.carriers;
    t.questions += b.questions;
    t.correct += b.correct;
    t.wrong += b.wrong;
    t.escapes += b.escapes;
    t.hints += b.hints;
    t.messages += b.messages;
    t.gates += b.gates;
    t.hazardJumps += b.hazardJumps;
    t.bridgeBreaks += b.bridgeBreaks;
    t.holeSlides += b.holeSlides;
    t.falls += b.falls;
    t.ladderJumps += b.ladderJumps;
    t.dustHits += b.dustHits;
    t.creatureHits += b.creatureHits;
    t.coinCreatures += b.coinCreatures;
    t.bonus += b.bonus;
    t.tunnels += b.tunnels;
    t.newPlayers += b.newPlayers;
    t.resumes += b.resumes;
    t.exits += b.exits;
    t.startOvers += b.startOvers;
    t.menuTrips += b.menuTrips;
    t.dialogs += b.dialogs;
    t.unexpectedDialogs += b.unexpectedDialogs;
    t.rankVisits += b.rankVisits;
    t.roomVisits += b.roomVisits;
    t.shelfVisits += b.shelfVisits;
    t.endSkips += b.endSkips;
    t.champion += b.champion;
    t.promotions += b.promotions;
    t.maxCoins = std::max(t.maxCoins, b.maxCoins);
    t.maxNets = std::max(t.maxNets, b.maxNets);
    t.maxTier = std::max(t.maxTier, b.maxTier);
    if (b.climbsDone > 0) {
        t.finalTotal = b.finalTotal;
        t.finalTier = b.finalTier;
    }
}

}  // namespace

void writeCraftedPlayer(const std::filesystem::path& usersDat, std::string_view name, int total, uint16_t gamesPlayed) {
    UsersDat users{};
    if (auto old = mischief::readWholeFile(usersDat))
        if (auto parsed = UsersDat::parse(*old)) users = *parsed;
    const int at = users.firstFreeRecord();
    if (at < 0) throw mischief::Error("the roster is full: no room for a crafted player");
    UserRecord r{};
    r.clear();  // Users_ClearRecord: zeros, answer masks 7
    r.setName(name);
    r.totalTreasures = static_cast<int16_t>(total);
    r.quizSlots.fill(0xFFFF);
    // One collected treasure per game played (treasure codes 6 + 2k), the first one on the pedestal.
    r.collected.fill(0x80);
    const int n = std::min<int>(gamesPlayed, kHidingCells);
    for (int k = 1; k < n; ++k) r.collected[static_cast<size_t>(k - 1)] = static_cast<uint8_t>(6 + 2 * k);
    r.shownTreasure = n > 0 ? 6 : -1;
    r.tier = static_cast<uint16_t>(total >= 230 ? 3 : total >= 115 ? 2 : total >= 25 ? 1 : 0);
    r.coins = r.bankedCoins = 8;
    r.nets = r.bankedNets = 10;
    r.playerX = 0x140;
    r.playerY = 0x70;
    r.cameraX = 0xC0;
    r.promptIcon = 4;
    r.shopCoinsOwed = 4;
    r.musicOn = r.soundOn = 1;
    r.gamesPlayed = gamesPlayed;
    users.records[static_cast<size_t>(at)] = r;
    if (!mischief::writeWholeFile(usersDat, users.bytes())) throw mischief::Error("cannot write " + mischief::pathToUtf8(usersDat));
}

RunResult runDosAutoplay(const DosGfxFiles& files, const RunOptions& o) {
    RunResult res;
    res.seed = o.seed;
    res.options = o;
    const auto wall0 = std::chrono::steady_clock::now();
    std::error_code ec;
    std::filesystem::remove_all(o.prefDir, ec);
    std::filesystem::create_directories(o.prefDir, ec);

    Checker checker;
    std::string name = o.name.empty() ? defaultName(o.seed) : o.name;
    bool existing = false;
    if (o.craftedTotal) {
        writeCraftedPlayer(o.prefDir / std::filesystem::path(kDosUsersFileName), name, *o.craftedTotal, o.craftedClimbs);
        existing = true;
    }
    std::mt19937 rng(o.seed ^ 0x5eedu);
    int remaining = o.climbs;
    std::optional<UserRecord> resume;
    LogTap tap(res.notes);
    auto lastSampleWall = std::chrono::steady_clock::now();
    for (int phase = 0;; ++phase) {
        ++res.phases;
        RigOptions ro;
        ro.prefDir = o.prefDir;
        ro.look = o.look;
        ro.sound = o.sound;
        ro.slowCpu = o.slowCpu;
        ro.frameSpeed = o.frameSpeed;
        ro.clockSeed = o.seed * 7919u + static_cast<uint32_t>(phase) * 104729u;
        Rig rig(files, ro);
        Bot* botPtr = nullptr;
        BotOptions bo;
        bo.seed = o.seed * 31u + static_cast<uint32_t>(phase);
        bo.climbs = remaining;
        bo.name = name;
        bo.existingPlayer = existing;
        bo.wrongAnswerRate = o.wrongAnswerRate;
        bo.escQuestionRate = o.escQuestionRate;
        bo.skipEndRate = o.skipEndRate;
        bo.huntRate = o.huntRate;
        bo.hazardWalkRate = o.hazardWalkRate;
        bo.menuTrips = o.menuTrips;
        bo.clubhouseTrips = o.clubhouseTrips;
        bo.tunnelTrips = o.tunnelTrips;
        bo.maxClimbTicks = o.maxClimbGameMinutes * 60 * 47;
        bo.log = o.log;
        bo.tickLog = o.tickLog;
        bo.tick = [&] {
            const uint64_t hb = g_heartbeat.fetch_add(1) + 1;
            if ((hb & 0x3FF) == 0 && botPtr) setStatus(std::format("seed {} phase {}: {}", o.seed, phase, botPtr->describe()));
        };
        if (phase == 0 && o.newPlayerTest) bo.newPlayerAtTick = 300 + static_cast<int>(rng() % 3000);
        if (phase == 0 && o.startOverTest) bo.startOverAtTick = 200 + static_cast<int>(rng() % 4000);
        if (phase == 0 && (o.exitTest || o.castleExitTest)) {
            bo.exitAtTick = o.castleExitTest ? 0 : 400 + static_cast<int>(rng() % 6000) + (o.newPlayerTest ? 3000 : 0);
            bo.exitInCastle = o.castleExitTest;
        }
        bo.climbSetup = [&] {
            ResourceSample s;
            s.climb = res.stats.climbsDone + (botPtr ? botPtr->stats().climbsDone : 0);
            s.tick = rig.ticks();
            s.saveHeapHigh = rig.game().sprites.saveHeapHigh;
            s.heapOffset = rig.game().sprites.heapOffset;
            s.biosKeys = rig.game().bios().size();
            s.quitSections = rig.runtime().frameTiming().quitSections().size();
            const auto now = std::chrono::steady_clock::now();
            s.wallSeconds = std::chrono::duration<double>(now - lastSampleWall).count();
            lastSampleWall = now;
            res.resources.push_back(s);
        };
        Bot bot(rig, checker, bo);
        botPtr = &bot;
        if (resume) bot.expectResume(*resume, "Exit and a new program run");
        std::string abortWhy;
        setStatus(std::format("seed {} phase {}: playing", o.seed, phase));
        try {
            bot.run();
        } catch (const BotAbort& a) {
            abortWhy = a.why;
        } catch (const std::exception& e) {
            abortWhy = std::format("exception: {} [{}]", e.what(), bot.describe());
        }
        g_heartbeat.fetch_add(1);
        accumulate(res.stats, bot.stats());
        res.gameTicks += rig.ticks();
        if (!abortWhy.empty()) {
            res.abortReason = abortWhy;
            res.recent = bot.recent();
            break;
        }
        if (bot.end() == BotEnd::ExitedMidClimb && bot.leftRecord()) {
            resume = bot.leftRecord();
            remaining -= bot.stats().climbsDone;
            existing = true;
            if (phase > 3) {
                res.abortReason = "too many program runs";
                break;
            }
            continue;
        }
        if (bot.stats().climbsDone < remaining) {
            res.abortReason = std::format("the game ended early ({} of {} climbs) [{}]", bot.stats().climbsDone, remaining, bot.describe());
            res.recent = bot.recent();
        }
        break;
    }
    res.violations = checker.violations();
    for (const auto& n : checker.notes()) res.notes.push_back(n);
    res.checks = checker.checks();
    res.saveChecks = checker.saveChecks();
    // Resources at the climb set-ups must stay bounded: the save heap is reset every frame, the BIOS
    // buffer holds at most 15 keys, a frame has a few I/O sections.
    for (const ResourceSample& s : res.resources) {
        if (s.biosKeys > 4) res.violations.push_back(std::format("resources: {} BIOS keys waiting at climb set-up {}", s.biosKeys, s.climb));
        if (s.quitSections > 8) res.violations.push_back(std::format("resources: {} I/O sections in a frame at climb set-up {}", s.quitSections, s.climb));
        if (s.saveHeapHigh > 0x8000) res.violations.push_back(std::format("resources: save heap high-water {:#x} at climb set-up {}", s.saveHeapHigh, s.climb));
    }
    res.wallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
    res.ok = res.abortReason.empty() && res.violations.empty() && res.stats.climbsDone >= o.climbs;
    if (res.ok) std::filesystem::remove_all(o.prefDir, ec);
    return res;
}

std::string summarize(const RunResult& r) {
    const BotStats& s = r.stats;
    const RunOptions& o = r.options;
    return std::format(
        "seed {:>4} {} {} {}{}{} | climbs {} total {} tier {}{} | ticks {} castle {} game {:.0f} min wall {:.1f} s | drop {} "
        "treasure {} key {} shop {}/{} | swing {} catch {} (carriers {}) quiz {} ok {} wrong {} esc {} promo {} | hints {} msgs {} "
        "gates {} jumps {} falls {} (bridge {} hole {}) ladder {} dust {} creature {} coin-elf {} bonus {} tunnel {} | new {} resume {} exit {} menu "
        "{} over {} dialogs {} (unexpected {}) rank {} room {} shelf {} skip {} | coins ..{} nets ..{} | checks {} saves {} runs {} | "
        "violations {} notes {}",
        r.seed, r.ok ? "OK  " : "FAIL", lookName(o.look), iniValue(o.sound), o.slowCpu ? " slow" : "",
        o.frameSpeed == FrameSpeed::Period ? " period" : "", s.climbsDone, s.finalTotal, s.finalTier,
        s.champion ? " CHAMPION" : "", r.gameTicks, s.castleTicks, static_cast<double>(r.gameTicks) / 47.339 / 60.0, r.wallSeconds,
        s.drops, s.treasures, s.keys, s.shopVisits, s.shopCoins, s.swings, s.catches, s.carriers, s.questions, s.correct, s.wrong,
        s.escapes, s.promotions, s.hints, s.messages, s.gates, s.hazardJumps, s.falls, s.bridgeBreaks, s.holeSlides, s.ladderJumps, s.dustHits,
        s.creatureHits, s.coinCreatures, s.bonus, s.tunnels, s.newPlayers, s.resumes, s.exits, s.menuTrips, s.startOvers, s.dialogs,
        s.unexpectedDialogs, s.rankVisits, s.roomVisits, s.shelfVisits, s.endSkips, s.maxCoins, s.maxNets, r.checks,
        r.saveChecks, r.phases, r.violations.size(), r.notes.size());
}

// ------------------------------------------------------------------------------ vision check

// The bot plays and every mountain tick compares what it sees with the elf table: every walking elf
// holding a sprite (state >= 0x2D, sprite slot 6/8/10) whose draw record shows a frame must be seen,
// as the right kind (clue carriers walking as carriers, throwers, plain walkers, coin creatures).
VisionCheck checkVision(const DosGfxFiles& files, int seeds, int ticks, bool verbose) {
    int samples = 0, seen = 0, kindOk = 0, falseSeen = 0;
    for (int seed = 1; seed <= seeds; ++seed) {
        RunOptions o;
        o.seed = static_cast<uint32_t>(seed);
        o.prefDir = std::filesystem::temp_directory_path() / "tmport_dos_autoplay" / std::format("vision_{}", seed);
        std::error_code ec;
        std::filesystem::remove_all(o.prefDir, ec);
        RigOptions ro;
        ro.prefDir = o.prefDir;
        ro.clockSeed = static_cast<uint32_t>(seed) * 7919u;
        Rig rig(files, ro);
        Checker checker;
        BotOptions bo;
        bo.seed = static_cast<uint32_t>(seed);
        bo.name = defaultName(static_cast<uint32_t>(seed));
        bo.climbs = 1;
        bo.huntRate = 1.0;
        Bot bot(rig, checker, bo);
        const Vision vision(rig.game());
        int played = 0;
        try {
            while (played < ticks && bot.step()) {
                const DosGame& g = rig.game();
                if (g.core.uiMode != 0 || g.core.level > 2 || g.player.obj.y >= 0x800) continue;
                if (levelOfPathCell(cellOf(g.player.obj.x, g.player.obj.y)) < 0) continue;
                ++played;
                const auto sprites = vision.elves(g);
                for (int i = 0; i < kElfCount; ++i) {
                    const Elf& e = g.elves.elves[static_cast<size_t>(i)];
                    if (e.base == 0 || e.state < 0x2D || e.spriteSlot < 6 || e.spriteSlot > 10) continue;
                    const DrawRecord& r = g.sprites.records[e.spriteSlot];
                    if (r.frame == 0xFFFF) continue;
                    const bool walking = e.state == 45 || e.state == 46 || e.state == 49 || e.state == 50 || e.state == 60 ||
                                         e.state == 61 || e.state == 70;
                    if (!walking) continue;
                    // A sprite only partly inside the play field (an elf coming in at a view edge) is a
                    // sample once 8 pixels of it are in view.
                    bool inView = false;
                    for (const SeenSprite& sp : vision.look(g))
                        if (sp.record == e.spriteSlot) inView = sp.right - sp.left >= 8 && sp.bottom - sp.top >= 8;
                    if (!inView) continue;
                    ++samples;
                    bool found = false;
                    for (const SeenSprite& sp : sprites) found = found || sp.record == e.spriteSlot;
                    if (!found && verbose) {
                        const auto all = vision.look(g);
                        std::string what = "not drawn";
                        for (const SeenSprite& sp : all)
                            if (sp.record == e.spriteSlot)
                                what = std::format("frame {} kind {} at x {} rect {}..{} pixels {}", sp.frame >> 1, kindName(sp.kind),
                                                   sp.x, sp.left, sp.right, sp.differing);
                        std::printf("seed %d: missed elf %d base %d state %d slot %d: %s\n", seed, i, e.base, e.state,
                                    e.spriteSlot, what.c_str());
                    }
                    for (size_t k = 0; k < sprites.size(); ++k) {
                        if (sprites[k].record != e.spriteSlot) continue;
                        ++seen;
                        SpriteKind want = SpriteKind::Walker;
                        if (e.base == 9 || e.base == 13) want = SpriteKind::Thrower;
                        else if (e.base == 17 || e.base == 21) want = SpriteKind::PlainWalker;
                        else if (e.base == 25) want = SpriteKind::CoinCreature;
                        else if (e.question != 0) want = SpriteKind::Carrier;
                        if (sprites[k].kind == want || vision.mayBe(sprites[k].frame, want) || sprites[k].kind == SpriteKind::FrozenElf)
                            ++kindOk;
                        else if (verbose)
                            std::printf("seed %d: elf %d base %d state %d question %d seen as %s\n", seed, i, e.base, e.state,
                                        e.question, std::string(kindName(sprites[k].kind)).c_str());
                    }
                }
                // Elf pictures where no elf holds a sprite.
                for (size_t k = 0; k < sprites.size(); ++k) {
                    bool held = false;
                    for (const Elf& e : g.elves.elves) held = held || (e.base != 0 && e.state >= 0x2D && e.spriteSlot == sprites[k].record);
                    if (!held && isElf(sprites[k].kind)) ++falseSeen;
                }
            }
        } catch (const BotAbort&) {
        }
        std::filesystem::remove_all(o.prefDir, ec);
    }
    return {samples, seen, kindOk, falseSeen};
}

namespace {

int runVisionCheck(const DosGfxFiles& files, int seeds, int ticks, bool verbose) {
    const VisionCheck v = checkVision(files, seeds, ticks, verbose);
    std::printf("vision check: %d walking elves with a sprite, %d seen (%.1f%%), %d of them as the right kind; %d elf "
                "sprites seen without a walking elf\n",
                v.samples, v.seen, v.samples ? 100.0 * v.seen / v.samples : 0.0, v.rightKind, v.strays);
    return v.ok() ? 0 : 1;
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

constexpr Look kLooks[] = {Look::Ega, Look::Tandy, Look::Mcga, Look::Cga};
constexpr SoundDevice kSounds[] = {SoundDevice::Tandy, SoundDevice::Speaker, SoundDevice::AdLib, SoundDevice::Off};

}  // namespace

std::string dosAutoplayUsage() {
    return "DOS autoplay soak test: plays the 1990 DOS edition headless, as a bot, and checks its rules\n"
           "  tmport --edition dos --autoplay [--seed N | --seeds A-B] [--climbs N] [--soak]\n"
           "         [--look ega|tandy|mcga|cga|all] [--sound tandy|speaker|adlib|off|null] [--slow-cpu]\n"
           "         [--frame-speed reference|period] [--exit-test] [--castle-exit-test] [--new-player-test]\n"
           "         [--start-over-test] [--menus] [--club] [--tunnels] [--champion] [--crafted-total T]\n"
           "         [--crafted-climbs C] [--name NAME] [--wrong-rate R] [--pref-dir DIR] [--trace | --trace-ticks]\n"
           "         [--watchdog SECONDS] [--max-climb-minutes M] [--data DOSFOLDER] [-v]\n"
           "    --soak          per seed: the look (seed % 4), the sound driver, slow CPU ((seed / 4) % 2), the period\n"
           "                    frame speed ((seed / 8) % 2), Exit + resume (seed % 3 == 1), Start New Player and back\n"
           "                    (seed % 3 == 2), Exit on the castle wall (seed % 7 == 3), Start Over (seed % 11 == 5),\n"
           "                    menu trips (odd seeds), clubhouse trips (even seeds), tunnels (seed % 3 == 0), crafted\n"
           "                    players: near the champion (seed % 5 == 0, total 290), 4 stars (seed % 5 == 3, total\n"
           "                    120), 6 stars (seed % 5 == 4, total 240)\n"
           "    --champion      crafted player with total 290 and 40 games (tier 3; champion after one climb)\n"
           "    --look all      the four looks in turn (by seed)\n"
           "    --sound null    no sound device: the Tandy driver runs silently (as the game does with sound off)\n"
           "    --trace         print the bot's decisions and the game's events (--trace-ticks: and every play tick)\n"
           "    exit code 0: all runs clean; 1: a violation, stall or failed run; 2: usage; 3: game files;\n"
           "              4: wall-clock hang (watchdog); 5: crash signal\n"
           "  tmport --edition dos --autoplay-vision-check [--seeds N] [--ticks N] [-v]   what the bot sees vs the elf table\n";
}

std::optional<int> runDosAutoplayCommand(int argc, char** argv) {
    bool autoplay = false, vision = false, dos = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--autoplay") autoplay = true;
        if (a == "--autoplay-vision-check") vision = true;
        if (a == "--edition" && i + 1 < argc && std::string_view(argv[i + 1]) == "dos") dos = true;
    }
    if ((!autoplay && !vision) || !dos) return std::nullopt;

    RunOptions base;
    uint32_t seedA = 1, seedB = 1;
    bool soak = false, champion = false, trace = false, verbose = false, allLooks = false;
    bool soundGiven = false, slowGiven = false, speedGiven = false;
    int watchdogSeconds = 120, visionSeeds = 4, visionTicks = 3000;
    std::string dataPath, prefBase;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto need = [&](const char* what) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(std::format("{} needs {}", a, what));
            return argv[++i];
        };
        try {
            if (a == "--autoplay" || a == "--autoplay-vision-check") continue;
            if (a == "--edition") {
                need("dos");
            } else if (a == "--seed") {
                seedA = seedB = static_cast<uint32_t>(std::stoul(need("a number")));
            } else if (a == "--seeds") {
                const std::string v = need("A-B");
                if (vision) visionSeeds = std::stoi(v);
                else if (!parseRange(v, seedA, seedB)) throw std::runtime_error("--seeds needs A-B");
            } else if (a == "--ticks") visionTicks = std::stoi(need("a number"));
            else if (a == "--climbs") base.climbs = std::stoi(need("a number"));
            else if (a == "--soak") soak = true;
            else if (a == "--look") {
                const std::string v = need("a look");
                if (v == "all") allLooks = true;
                else if (auto l = parseLookOption(v)) base.look = *l;
                else throw std::runtime_error("--look: ega, tandy, mcga, cga or all");
            } else if (a == "--sound") {
                const std::string v = need("a device");
                soundGiven = true;
                if (v == "null") base.sound = SoundDevice::Off;
                else if (auto d = parseSoundDevice(v)) base.sound = *d;
                else throw std::runtime_error("--sound: tandy, speaker, adlib, off or null");
            } else if (a == "--slow-cpu") {
                base.slowCpu = true;
                slowGiven = true;
            } else if (a == "--frame-speed") {
                const auto f = parseFrameSpeed(need("reference or period"));
                if (!f) throw std::runtime_error("--frame-speed: reference or period");
                base.frameSpeed = *f;
                speedGiven = true;
            } else if (a == "--exit-test") base.exitTest = true;
            else if (a == "--castle-exit-test") base.castleExitTest = true;
            else if (a == "--new-player-test") base.newPlayerTest = true;
            else if (a == "--start-over-test") base.startOverTest = true;
            else if (a == "--menus") base.menuTrips = true;
            else if (a == "--club") base.clubhouseTrips = true;
            else if (a == "--tunnels") base.tunnelTrips = true;
            else if (a == "--champion") champion = true;
            else if (a == "--crafted-total") base.craftedTotal = std::stoi(need("a number"));
            else if (a == "--crafted-climbs") base.craftedClimbs = static_cast<uint16_t>(std::stoul(need("a number")));
            else if (a == "--name") base.name = need("a name");
            else if (a == "--wrong-rate") base.wrongAnswerRate = std::stod(need("a rate"));
            else if (a == "--pref-dir") prefBase = need("a directory");
            else if (a == "--trace") trace = true;
            else if (a == "--trace-ticks") trace = base.tickLog = true;
            else if (a == "--watchdog") watchdogSeconds = std::stoi(need("seconds"));
            else if (a == "--max-climb-minutes") base.maxClimbGameMinutes = static_cast<uint64_t>(std::stoul(need("minutes")));
            else if (a == "--data") dataPath = need("a path");
            else if (a == "-v" || a == "--verbose") verbose = true;
            else throw std::runtime_error(std::format("unknown option '{}'", a));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "tmport: %s\n\n%s", e.what(), dosAutoplayUsage().c_str());
            return 2;
        }
    }
    mischief::log::setVerbose(verbose);
    if (dataPath.empty())
        if (auto env = mischief::getEnv("TM_DATA_DOS")) dataPath = *env;
    if (dataPath.empty()) {
        std::fprintf(stderr, "tmport: the DOS game folder was not found. Pass --data <folder with SST.EXE> or set TM_DATA_DOS.\n");
        return 3;
    }
    std::optional<DosGfxFiles> files;
    try {
        files.emplace(DosGfxFiles::open(mischief::pathFromUtf8(dataPath)));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tmport: cannot open the DOS game files: %s\n", e.what());
        return 3;
    }
    if (vision) return runVisionCheck(*files, visionSeeds, visionTicks, verbose);

    std::signal(SIGSEGV, dosAutoplayOnCrashSignal);
    std::signal(SIGABRT, dosAutoplayOnCrashSignal);
    std::signal(SIGFPE, dosAutoplayOnCrashSignal);
    std::signal(SIGILL, dosAutoplayOnCrashSignal);
    Watchdog watchdog(watchdogSeconds);
    const std::filesystem::path base0 =
        prefBase.empty() ? std::filesystem::temp_directory_path() / "tmport_dos_autoplay" : mischief::pathFromUtf8(prefBase);
    int failures = 0, runs = 0, climbs = 0;
    for (uint32_t seed = seedA; seed <= seedB; ++seed) {
        RunOptions o = base;
        o.seed = seed;
        o.prefDir = base0 / std::format("seed_{}", seed);
        if (allLooks || soak) o.look = kLooks[seed % 4];
        if (soak) {
            if (!soundGiven) o.sound = kSounds[(seed / 2) % 4];
            if (!slowGiven) o.slowCpu = (seed / 4) % 2 == 1;
            if (!speedGiven) o.frameSpeed = (seed / 8) % 2 == 1 ? FrameSpeed::Period : FrameSpeed::Reference;
            o.exitTest = o.exitTest || seed % 3 == 1;
            o.newPlayerTest = o.newPlayerTest || seed % 3 == 2;
            o.castleExitTest = o.castleExitTest || seed % 7 == 3;
            o.startOverTest = o.startOverTest || seed % 11 == 5;
            o.menuTrips = o.menuTrips || seed % 2 == 1;
            o.clubhouseTrips = o.clubhouseTrips || seed % 2 == 0;
            o.tunnelTrips = o.tunnelTrips || seed % 3 == 0;
            if (!o.craftedTotal) {
                if (seed % 5 == 0) o.craftedTotal = 290, o.craftedClimbs = 40;
                else if (seed % 5 == 3) o.craftedTotal = 120, o.craftedClimbs = 14;
                else if (seed % 5 == 4) o.craftedTotal = 240, o.craftedClimbs = 25;
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
            r = runDosAutoplay(*files, o);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "tmport: seed %u: %s\n", seed, e.what());
            ++failures;
            continue;
        }
        ++runs;
        climbs += r.stats.climbsDone;
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
                std::printf("  resources at climb %d (tick %llu): save heap high %#x offset %#x, BIOS keys %d, I/O sections %zu, %.2f s\n",
                            s.climb, static_cast<unsigned long long>(s.tick), s.saveHeapHigh, s.heapOffset, s.biosKeys,
                            s.quitSections, s.wallSeconds);
        std::fflush(stdout);
    }
    std::printf("dos autoplay: %d run(s), %d failed, %d climbs\n", runs, failures, climbs);
    return failures == 0 ? 0 : 1;
}

}  // namespace tmport::dos::autoplay
