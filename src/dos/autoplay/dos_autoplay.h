#pragma once
// The autoplayer of the 1990 DOS edition as a soak test of the whole game (area autoplay, wave 5).
//
//   tmport --edition dos --autoplay [options]      (dispatched from tmport's CLI, cli/commands.cpp)
//
// Each run plays the real game (DosGame through mainStart and the lockstep runtime, headless, no
// window, no audible sound) in a fresh Rig (dos_autoplay_rig.h): the title, the Club Sign-In (a new
// player, or a crafted one written into the run's dos_users.dat), climbs through the three levels, the
// castle, the end sequence, the treasure room and the rank screen, the next climbs (the tiers rise:
// bridges break, the castle creature, elf dust; a crafted record near 300 takes the champion path),
// then File > Exit and Save. Exercises (seeded): the clubhouse's rank screen, treasure room and prize
// shelf rooms; File > Start New Player and back; File > Start Over; Exit and Save mid-climb (also on the castle wall)
// with a new program run that resumes the saved climb (the loaded record must equal the saved one
// byte for byte); menu trips; the caves and tunnels.
//
// Checks while it plays (dos_autoplay_checks.h): game-rule invariants every tick, USERS.DAT round
// trips after every save, the climb's end arithmetic; plus stalls (no progress for minutes of game
// time), exceptions, a wall-clock watchdog (no game tick at all) and crash signals. Resource counters
// (the sprite save heap, the BIOS key buffer, the frame timing) are sampled at every climb set-up and
// must stay bounded.
//
// The bot reads the hiding places' contents from the game state (it is a test bot); it sees elves on
// the screen (dos_autoplay_vision.h). Nothing is written outside the run's preference folder
// (default: <temp>/tmport_dos_autoplay/seed_<n>, removed after a clean run).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dos/autoplay/dos_autoplay_bot.h"
#include "edition/dos_options.h"

namespace tmport::dos {
class DosGfxFiles;
}

namespace tmport::dos::autoplay {

struct RunOptions {
    std::filesystem::path prefDir;     // the run's own folder (dos_users.dat); cleared first
    uint32_t seed = 1;                 // the bot's decisions and the layout clock
    int climbs = 1;
    Look look = Look::Ega;
    SoundDevice sound = SoundDevice::Off;
    bool slowCpu = false;
    FrameSpeed frameSpeed = FrameSpeed::Reference;
    std::string name;                  // player name (default "Bot" + letters of the seed)
    std::optional<int> craftedTotal;   // start with a crafted player with this total ...
    uint16_t craftedClimbs = 0;        // ... and this many games played (collected treasures)
    bool exitTest = false;             // Exit and Save mid-climb, then a new program run resumes
    bool castleExitTest = false;       // ... on the castle wall
    bool newPlayerTest = false;        // File > Start New Player mid-climb, then back to the same player
    bool startOverTest = false;        // File > Start Over mid-climb (the climb is dropped, a new one starts)
    bool menuTrips = false;
    bool clubhouseTrips = false;
    bool tunnelTrips = false;
    double wrongAnswerRate = 0.2;
    double escQuestionRate = 0.05;
    double skipEndRate = 0.5;
    double huntRate = 0.15;
    double hazardWalkRate = 0.3;       // climbs that walk into the geyser hole / a breaking bridge once
    uint64_t maxClimbGameMinutes = 60;
    bool tickLog = false;
    std::function<void(std::string_view)> log;
};

struct ResourceSample {
    int climb = 0;
    uint64_t tick = 0;
    uint16_t saveHeapHigh = 0, heapOffset = 0;
    int biosKeys = 0;
    size_t quitSections = 0;
    double wallSeconds = 0;
};

struct RunResult {
    bool ok = false;
    uint32_t seed = 0;
    RunOptions options;
    int phases = 0;                  // program runs (2+ with the Exit test)
    BotStats stats;
    std::string abortReason;
    std::vector<std::string> violations;
    std::vector<std::string> notes;
    std::vector<std::string> recent;
    std::vector<ResourceSample> resources;
    uint64_t checks = 0, saveChecks = 0;
    uint64_t gameTicks = 0;
    double wallSeconds = 0;
};

// One soak run. Throws only on start-up errors.
RunResult runDosAutoplay(const DosGfxFiles& files, const RunOptions& options);

// One-line summary of a run.
std::string summarize(const RunResult& r);

// What the bot sees against the elf table (--autoplay-vision-check): the bot plays `seeds` new games
// for `ticks` mountain ticks each; every walking elf holding a sprite is a sample, `seen` when the
// vision reports its draw record with enough visible pixels, `rightKind` when it also names its kind
// (clue carrier, walker, thrower, plain walker, coin creature); `strays` are elf sprites the vision
// reports without a walking elf behind them.
struct VisionCheck {
    int samples = 0, seen = 0, rightKind = 0, strays = 0;
    bool ok() const { return samples > 0 && seen * 100 >= samples * 95 && rightKind * 100 >= seen * 95; }
};
VisionCheck checkVision(const DosGfxFiles& files, int seeds, int ticks, bool verbose = false);

// A record for a crafted player (the fields Users_InitNewPlayer 47c7:0a8b sets, with this total,
// games played and as many collected treasures), added to the dos_users.dat at `usersDat`.
void writeCraftedPlayer(const std::filesystem::path& usersDat, std::string_view name, int total, uint16_t gamesPlayed);

// The CLI. nullopt when argv is not a DOS --autoplay / --autoplay-vision-check command (both need
// --edition dos).
std::optional<int> runDosAutoplayCommand(int argc, char** argv);
std::string dosAutoplayUsage();

}  // namespace tmport::dos::autoplay
