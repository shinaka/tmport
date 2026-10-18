#pragma once
// The autoplayer as a soak test of the whole game (area autoplay).
//
//   tmport --autoplay [options]          (dispatched from tmport's CLI, cli/commands.cpp)
//
// Each run plays the real game (game::gameMain) headless in a fresh autoplay::Rig: title, Club
// Sign-In (a new player, or a crafted one), climbs through the three mountain levels, the castle
// wall, the celebration and the rank screen, the next climb ... then File > Exit. Optional
// mid-climb exercises: File > Start Another Game + resume, Exit + a new program run that resumes
// the saved climb (the loaded record must equal the one left, byte for byte).
//
// Checks while it plays (autoplay_checks.h): game-rule invariants, users.dat round trips after
// every save, climb-end arithmetic; plus stalls (no progress for minutes of game time), pumps
// without time passing, exceptions, a wall-clock watchdog (no pump at all) and crash signals.
// Resource counters (hot spots, DAM frame slots, pause depths, queued input) are recorded at
// every climb set-up and must not grow.
//
// The bot reads hidden spot contents from the game state (it is a test bot); it sees elves the
// way a player does, on the screen (autoplay_vision.h). Nothing is written outside the pref
// directory (default: <temp>/tmport_autoplay/seed_<n>, removed after a clean run).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tools/autoplay/autoplay_bot.h"

namespace tmport {
struct GameFiles;
}

namespace tmport::autoplay {

struct RunOptions {
    std::filesystem::path prefDir;     // the run's own directory (users.dat); cleared first
    uint32_t seed = 1;                 // bot decisions; also offsets the manual clock (game seeds)
    int climbs = 1;
    std::optional<uint16_t> gameSeed;  // fixed srand for every climb set-up (default: the clock)
    bool manualClock = true;
    bool audio = true;
    std::string name;                  // player name (default "Bot" + letters of the seed)
    std::optional<uint32_t> craftedTotal;  // start with a crafted player with this total
    uint16_t craftedClimbs = 0;            // ... and this many climbs done
    bool restartTest = false;          // File > Start Another Game mid-climb, then resume
    bool exitTest = false;             // Exit mid-climb, then a new run that resumes
    bool castleExitTest = false;       // Exit on the castle wall, then a new run that resumes there
    bool menuExcursions = false;
    bool tickLog = false;
    int autorepeat = 0;                // extra arrow key-down repeats per tick (keyboard autorepeat)
    int exhaustLevel = -1;             // spend every coin and net on this level (rescue pile)
    double wrongAnswerRate = 0.2;
    double escQuestionRate = 0.05;
    double skipAnimRate = 0.5;
    uint64_t maxClimbGameMinutes = 60;
    std::function<void(std::string_view)> log;  // trace + bot lines (empty: none)
};

struct ResourceSample {
    int climb = 0;
    bool resumed = false;    // a resumed climb (after a sign-in, which clears the hot spots)
    int hotspots = 0;
    std::string hotspotIds;  // the registered ids (hex)
    size_t animFrameSlots = 0;
    int dialogPauseDepth = 0;
    int audioPauseDepth = 0;
    size_t pendingInput = 0;
    size_t postedEvents = 0;
    double wallSeconds = 0;
};

struct RunResult {
    bool ok = false;
    uint32_t seed = 0;
    int phases = 0;                  // game runs (2+ with the Exit test)
    BotStats stats;
    std::string abortReason;
    std::vector<std::string> violations;
    std::vector<std::string> notes;
    std::vector<std::string> recent;  // last lines before a failure
    std::vector<ResourceSample> resources;
    uint64_t checks = 0, saveChecks = 0;
    uint64_t gameMs = 0;
    double wallSeconds = 0;
    std::string playerName;
};

// One soak run. Throws only on start-up errors (the game files).
RunResult runAutoplay(GameFiles& files, const RunOptions& options);

// One-line summary of a run.
std::string summarize(const RunResult& r);

// The CLI. nullopt when argv is not an --autoplay / --autoplay-vision-check command.
std::optional<int> runAutoplayCommand(int argc, char** argv);
std::string autoplayUsage();

}  // namespace tmport::autoplay
