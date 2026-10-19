#pragma once
// The DOS autoplayer's bot (area autoplay, wave 5): plays the 1990 DOS edition (DosGame in a Rig) like
// a player, with the keyboard only, deciding before every game tick which keys to hold or press.
//
// How it plays
//   * The screen it is on comes from the game's mode bits (g_uiMode: title, sign-in, menu, dialog,
//     hint, message, question) and, in play, from where the player is (clubhouse, a mountain level,
//     a cave or tunnel, the castle, the castle top, the treasure room, the rank screen).
//   * Mountain levels (dos_autoplay_world.h): the bot walks along the level's loop to the hiding places
//     of the level's key and treasures (it reads the hidden contents from the game state: it is a test
//     bot), stands in the middle of the place and drops a coin (Down); with the key found it walks to
//     the level's gate and holds Up (trunk, geyser, stone arch). It jumps over the geyser hole and,
//     from 4 stars, over the rope bridges (PgUp / Home held for two flips); a fall it did not avoid
//     is played on from where it lands.
//   * Elves: it sees them on the screen (dos_autoplay_vision.h) and swings the net (Space with the
//     direction) when an elf it wants is in front of it: clue carriers while the level has hidden
//     clue words, any elf when it is short of coins; it walks into the coin creature when broke.
//     Questions: the correct answer is choice 0 of the file; its row comes from the screen's answer
//     order; the bot moves the focus there (Up/Down) and presses Enter; some answers are wrong or
//     left with Esc on purpose (seeded).
//   * Coins and nets: the net shop (4 coins in front of it, Up into the cave, Down out) when it has no
//     net left; the bonus coins of the level-0 cave's upper room once per climb when short of coins.
//   * The castle: the hall ladder, then the route up the wall (castleRoute: climbing, ladder jumps with
//     Left/Right at the lower part of a cell) to the exit; after a fall it plans again.
//   * The end: waits through (or skips with Enter) the end sequence, the chute, the treasure room;
//     Enter on the rank screen and on the end-of-game message.
//   * The clubhouse: exercises (seeded): the rank screen (Up at the chest), the treasure room (Up on
//     the right of the start cell, Down to leave), the prize shelf rooms (walking right through all
//     four and back); then Up at the door.
//   * Exercises mid-climb: File > Start New Player and back (the resumed game must equal the saved
//     record), File > Start Over, Exit and Save (the caller resumes in a new program run), menu trips
//     (help pages, the options' Yes/No boxes), the caves and tunnels, and once in a while a walk into
//     the geyser hole or onto a 4-star bridge (the player falls a level and climbs back).
//
// Checks (dos_autoplay_checks.h) run after every tick, at climb set-up, at every save and at the
// climb's end. A stall (no progress for minutes of game time) or an exception ends the run.

#include <array>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "dos/autoplay/dos_autoplay_checks.h"
#include "dos/autoplay/dos_autoplay_vision.h"
#include "dos/autoplay/dos_autoplay_world.h"
#include "dos/input/keyboard.h"
#include "dos/save/users_dat.h"

namespace tmport::dos::autoplay {

class Rig;

struct BotOptions {
    uint32_t seed = 1;                // the bot's own decisions (never the game's generator)
    int climbs = 1;                   // climbs to finish in this program run, then Exit and Save
    std::string name = "Bot";         // player to create, or to choose with existingPlayer
    bool existingPlayer = false;      // choose `name` on the sign-in list instead of typing it
    double wrongAnswerRate = 0.2;     // first answers given wrong on purpose
    double escQuestionRate = 0.05;    // questions left with Esc on purpose
    double skipEndRate = 0.5;         // end sequences skipped with Enter
    double huntRate = 0.15;           // non-carrier elves netted on purpose (while nets last)
    double hazardWalkRate = 0.3;      // climbs where the bot walks into the geyser hole or onto a 4-star
                                      // bridge once on purpose (and climbs back from below)
    bool clubhouseTrips = false;      // rank screen, treasure room and shelf rooms before a climb
    bool menuTrips = false;           // help pages and the options' boxes during a climb
    bool tunnelTrips = false;         // the cave's upper room, the mine tunnel, the ice cave
    int newPlayerAtTick = -1;         // File > Start New Player after this many play ticks, then back
    int startOverAtTick = -1;         // File > Start Over after this many play ticks (the game is dropped)
    int exitAtTick = -1;              // Exit and Save after this many play ticks (the caller resumes)
    bool exitInCastle = false;        // ... but only once the player is on the castle wall
    uint64_t stallTicks = 3 * 60 * 47;     // no progress for this long: stuck
    uint64_t maxClimbTicks = 60 * 60 * 47; // one climb longer than this: stuck
    bool tickLog = false;             // log the pose and the keys of every play tick
    std::function<void(std::string_view)> log;  // decisions and events (empty: only the ring buffer)
    std::function<void()> climbSetup;           // called at every climb set-up (resource samples)
    std::function<void()> tick;                 // called after every game tick (the watchdog's heartbeat)
};

struct BotStats {
    int climbsDone = 0;
    uint64_t ticks = 0, playTicks = 0, castleTicks = 0;
    int drops = 0, treasures = 0, keys = 0, emptySearches = 0, shopCoins = 0, shopVisits = 0;
    int swings = 0, catches = 0, carriers = 0, questions = 0, correct = 0, wrong = 0, escapes = 0;
    int hints = 0, messages = 0, gates = 0, hazardJumps = 0, falls = 0, ladderJumps = 0;
    int bridgeBreaks = 0, holeSlides = 0;
    int dustHits = 0, creatureHits = 0, coinCreatures = 0, bonus = 0, tunnels = 0;
    int newPlayers = 0, resumes = 0, exits = 0, startOvers = 0, menuTrips = 0, dialogs = 0, unexpectedDialogs = 0;
    int rankVisits = 0, roomVisits = 0, shelfVisits = 0, endSkips = 0, champion = 0, promotions = 0;
    int maxCoins = 0, maxNets = 0, maxTier = 0;
    uint32_t finalTotal = 0;
    int finalTier = 0;
};

enum class BotEnd { Running, Finished, ExitedMidClimb, Aborted };

class Bot {
public:
    Bot(Rig& rig, Checker& checker, BotOptions options);
    ~Bot();
    Bot(const Bot&) = delete;
    Bot& operator=(const Bot&) = delete;

    // Plays until the game quits; throws BotAbort when it is stuck.
    void run();
    // One game tick (the keys before it, the checks after it); false once the game has quit.
    bool step();

    // The record the player left with (Exit and Save mid-climb) for the next program run.
    const std::optional<UserRecord>& leftRecord() const { return leftRecord_; }
    void expectResume(UserRecord record, std::string how) {
        resumeExpected_ = std::move(record);
        resumeHow_ = std::move(how);
    }

    BotEnd end() const { return end_; }
    const BotStats& stats() const { return stats_; }
    std::vector<std::string> recent() const { return {recent_.begin(), recent_.end()}; }
    std::string describe() const;

private:
    enum class Mode { None, Title, SignIn, Menu, Dialog, Hint, Message, Quiz, Flash, Play };
    enum class Key : uint8_t { Left, Right, Up, Down, Space, PgUp, Home, Count };
    enum class Plan { None, Exit, NewPlayer, MenuTrip, StartOver };

    void beforeTick();
    void afterTick();
    Mode modeOf() const;
    void logLine(std::string line);
    template <class... Args>
    void say(std::format_string<Args...> fmt, Args&&... args) {
        logLine(std::format(fmt, std::forward<Args>(args)...));
    }
    [[noreturn]] void abort(std::string why);
    bool chance(double p);

    // Keys: held movement keys and one-tick taps of UI keys.
    void hold(Key k, bool down);
    void holdOnly(std::initializer_list<Key> keys);
    void releaseMovement();
    void tap(std::string_view keyName);
    bool uiReady() const;  // no command or BIOS key waiting, the last tap released, pacing elapsed

    // Screens.
    void title();
    void signIn();
    void menu();
    void dialog();
    void hint();
    void message();
    void quiz();
    void play();

    // Play.
    void playMountain(int level);
    void playClubhouse();
    void playCastle();
    void playEnd();
    void playTransit();
    bool busyPlayerState() const;
    bool walkTo(int level, int target, int tolerance);  // true when there
    bool searchHere();                                   // drops a coin once the player is idle
    bool elfAction(int level);                           // a net swing or a chase this tick
    int remainingSearches(int level) const;
    std::optional<int> nextSearchTarget(int level) const;
    void climbGate(int level);
    bool netShop(int level);
    void enterDoor(int level, unsigned cell, int offset);
    bool startPlan(Plan p);

    // Events.
    void onClimbSetup(bool fresh);
    void progress();

    Rig& rig_;
    Checker& checker_;
    BotOptions o_;
    DosGame& g_;
    Vision vision_;
    std::mt19937 rng_;

    BotEnd end_ = BotEnd::Running;
    BotStats stats_;
    std::deque<std::string> recent_;
    std::optional<UserRecord> leftRecord_;
    std::optional<UserRecord> resumeExpected_;
    std::string resumeHow_;
    bool expectFresh_ = false;     // the player left before the game started: the next one is new

    // Keys.
    std::array<bool, static_cast<size_t>(Key::Count)> held_{};
    std::optional<PcKey> tapped_;   // released before the next tick
    uint64_t lastTapTick_ = 0;
    uint64_t tick_ = 0;

    // Progress and snapshots.
    TickSnapshot before_;
    uint64_t lastProgressTick_ = 0, climbStartTick_ = 0;
    std::string progressKey_;
    uint64_t layoutHash_ = 0;
    bool haveLayout_ = false;
    uint16_t tierBeforeSetup_ = 0;
    uint16_t gamesBefore_ = 0;

    // Plans and exercises.
    Plan plan_ = Plan::None;
    int planStep_ = 0;
    uint64_t planTick_ = 0;
    bool exitDone_ = false, newPlayerDone_ = false, startOverDone_ = false;
    int menuTripsThisClimb_ = 0;
    uint64_t nextMenuTripTick_ = 0;

    // Quiz.
    int quizTarget_ = -1;
    bool quizEsc_ = false;
    bool quizSeen_ = false;

    // Play state.
    int searchCell_ = -1;          // the canonical cell of the place being searched
    uint64_t searchTick_ = 0;      // tick of the last coin drop attempt
    int searchAttempts_ = 0;
    int jumpTicks_ = 0;            // ticks left holding the jump key
    bool hazardWalk_ = false;      // this climb: walk into the next hazard on purpose
    int walkInto_ = -1;            // the hazard cell being walked into (no jump there until a level change)
    std::array<uint16_t, 2> bridgePictures_{};  // pictures of 84 and B7 after the last tick
    int swingTicks_ = 0;           // ticks left holding Space
    int chaseTicks_ = 0;           // ticks spent chasing elves on this level
    int chaseLevel_ = -1;
    int wanderDir_ = 1;
    uint64_t wanderUntil_ = 0;
    int shopStep_ = 0;
    int shopLevel_ = 0;
    int doorCell_ = -1;            // holding Up into a doorway (the cave, the mine, the ice cave)
    uint64_t doorSince_ = 0;
    uint64_t levelTicks_ = 0;      // play ticks on the current level (to give up on treasures)
    bool bonusTaken_ = false;      // this climb's bonus coins
    bool bonusTrip_ = false;
    bool tunnelsDone_ = false;
    int clubStep_ = 0;             // clubhouse exercise progress
    int shelfStep_ = 0;            // the shelf rooms: 0 walk in, 1 through, 2 back, 3 done
    bool endSkip_ = false;         // skip this climb's end sequence with Enter
    int quizTierSeen_ = -1;        // the record's question tier (promotions)
    uint64_t clubTick_ = 0;
    int endWait_ = 0;              // ticks on the current screen of the end (castle top, rooms)
    int endCell_ = -1;             // the cell of that screen
    int lastLevel_ = -1;
    uint64_t gateTick_ = 0;        // when the bot began holding Up at the gate
    bool climbing_ = false;        // holding Up at a gate
    int climbLevel_ = 0;           // the level whose gate it is
};

// Thrown by the bot when a run cannot go on (stuck, an unexpected screen, the step limit).
struct BotAbort {
    std::string why;
};

}  // namespace tmport::dos::autoplay
