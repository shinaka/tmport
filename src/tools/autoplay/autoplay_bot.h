#pragma once
// The autoplayer: plays the real game (game::gameMain in an autoplay::Rig) like a player would,
// with the keyboard and the mouse, by reading the game state at every input fetch.
//
// How it works
//   * GameMain's state-machine trace (GameMainOptions::trace, including the scenes' "scene: ..."
//     lines) tells the bot which screen is up: title, sign-in, room, hint, elf question, level
//     cut-scene, castle wall, celebration / rank screen, shutdown.
//   * An Engine pump hook runs at every input fetch. Input is injected into the engine's input
//     queue right before the fetch drains it (SDL key / text / mouse events through the event
//     translator), so the game sees it exactly like a key pressed just before the fetch. Menu
//     keys (Alt+F, ...) are posted as SDL events so that the menu bar's event filter sees them.
//   * Arrows are held like keys on a keyboard (key-down when the bot starts to hold one, key-up
//     when it lets go; --autorepeat adds keyboard repeats). Enter / Space are only pressed while
//     no input code is queued, so that they act in this tick (see AUTOPLAY_FINDINGS.md A-1).
//   * Room ticks are recognised by the frame timer (slot 7 armed with 100 ms right before
//     Room_Run's Input_GetCode). At each tick the bot decides what to hold or press: walk toward
//     the next spot that hides a treasure or the key (it reads the hidden contents from the game
//     state: it is a test bot), toss a coin at the right third, swing the net at elves it sees on
//     the screen (autoplay_vision.h), buy nets, pick up the rescue coin pile, unlock the gate.
//   * Elf questions: the correct answer is choice 0 of the file; its display position comes from
//     the quiz slot's order; the bot clicks the answer's hot spot. Some answers are wrong or left
//     with Esc on purpose (seeded) to cover those paths.
//   * The castle wall: ladder layout from the trace, climber pose from the state, the wall
//     creature from the trace; the bot climbs through full ladders and keeps away from the
//     creature's reach (on some visits it lets the creature push it once, on purpose).
//   * Exercises (seeded): File > Start Another Game, Exit (also on the castle wall), menu trips,
//     spending every coin and net (rescue pile), wrong answers / Esc on questions.
//   * Dialogs (Exit / Start Another Game Yes/No, help pager, ...) are recognised through the
//     rig's dialog input hook.
//
// Checks (autoplay_checks.h) run at climb set-up, every room tick, every save and every climb end.
// A stall (no progress for a while), an exception or an invariant violation ends the run.

#include <array>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "game/room/room_tables.h"
#include "game/scenes/castle_climb.h"
#include "tools/autoplay/autoplay_vision.h"

namespace tmport {
enum class PumpPhase : uint8_t;
namespace game { struct GameState; }
}  // namespace tmport

namespace tmport::autoplay {

class Rig;
class Checker;

// Thrown out of the pump hook to end a run (not a std::exception, so that no handler in the game
// swallows it).
struct AutoplayAbort {
    std::string why;
};

struct BotOptions {
    uint32_t seed = 1;                 // the bot's own decisions (never the game's generator)
    int climbs = 1;                    // climbs to finish in this run, then Exit
    std::string name = "Bot";          // player to create (or to select with existingPlayer)
    bool existingPlayer = false;       // select `name` from the roster instead of creating it
    double wrongAnswerRate = 0.2;      // first answers given wrong on purpose
    double escQuestionRate = 0.05;     // questions left with Esc on purpose
    double skipAnimRate = 0.5;         // cut-scenes / celebrations skipped with Space
    double huntRate = 1.0;             // 0: never hunt elves unless out of coins
    int exhaustLevel = -1;             // on this level, spend every coin and net first (rescue pile)
    double creatureSeekRate = 0.35;    // castle visits where the bot lets the wall creature push it once
    bool tickLog = false;              // log the pose and the action of every room / castle tick
    // Keyboard autorepeat while an arrow is held: this many extra key-down repeats per tick (a
    // real keyboard sends ~3 per 100 ms tick). 0: like a player holding a key on a keyboard
    // without repeat (one key-down when it goes down, one key-up when it is released).
    int autorepeat = 0;
    // Mid-climb exercises (-1 = off): after this many room ticks of the run.
    int restartAtTick = -1;            // File > Start Another Game -> sign-in -> resume
    int exitAtTick = -1;               // Esc + Yes: the run ends mid-climb (the caller resumes)
    int exitInCastleAtTick = -1;       // Esc + Yes on the castle wall after this many castle ticks
    bool menuExcursions = false;       // See Rank, help pages, options, Ctrl+S, clue replay
    uint64_t maxClimbGameMs = 60ull * 60 * 1000;  // game time per climb before it counts as stuck
    uint64_t stallGameMs = 180000;                // no progress for this long -> hang
    // Every bot decision and game trace line (verbose diagnostics); empty = only the ring buffer.
    std::function<void(std::string_view)> log;
};

struct BotStats {
    int climbsDone = 0;
    uint64_t roomTicks = 0, castleTicks = 0;
    int tosses = 0, tossMisses = 0, treasures = 0, keys = 0, shopCoins = 0, shopBuys = 0;
    int swings = 0, catches = 0, questions = 0, correct = 0, wrong = 0, escapes = 0;
    int hints = 0, cutscenes = 0, gates = 0, piles = 0, pushes = 0, falls = 0, jumps = 0, bumps = 0;
    int restarts = 0, exits = 0, resumes = 0, menuTrips = 0, dialogs = 0, unexpectedDialogs = 0;
    int keysWaited = 0;  // Enter / Space held back because input codes were still queued
    int sparkleHits = 0; // coins lost in a room without a toss (an elf's sparkle hit the player)
    int exhausted = 0;
    int maxCoins = 0, maxNets = 0, minCoins = 1 << 30;
    int champion = 0, promotions = 0;
    uint32_t finalTotal = 0;
    int finalRank = 0;
};

enum class BotEnd { Running, Finished, ExitedMidClimb, Aborted };

class Bot {
public:
    Bot(Rig& rig, Checker& checker, BotOptions options);
    ~Bot();
    Bot(const Bot&) = delete;
    Bot& operator=(const Bot&) = delete;

    // GameMainOptions::trace.
    void onTrace(std::string_view line);

    // The record (game::toRecord) the player had when the run left mid-climb (Exit), for the
    // resume check of the next run; and the record to compare the next sign-in with.
    const std::vector<uint8_t>& leftRecord() const { return leftRecord_; }
    void expectResume(std::vector<uint8_t> record, std::string how) {
        resumeExpected_ = std::move(record);
        resumeHow_ = std::move(how);
    }

    BotEnd end() const { return end_; }
    const BotStats& stats() const { return stats_; }
    // The last trace lines and bot decisions (for failure reports).
    std::vector<std::string> recent() const { return {recent_.begin(), recent_.end()}; }
    std::string describe() const;  // one line: mode, room, pose, inventory
    uint64_t heartbeat() const { return heartbeat_; }

private:
    enum class Mode { None, Title, SignIn, SignedIn, Room, Hint, Quiz, Cutscene, Castle, Celebrate, SeeRank, Shutdown };
    enum class Busy { None, Toss, Net, Door, Gate, Shop, Jump, Pile };
    enum class DialogIntent { Enter, Yes, No, Esc };
    struct Act {
        bool left = false, right = false, up = false, down = false;
        bool enter = false, space = false, esc = false;
    };
    struct KeyDef {
        uint32_t key = 0, scan = 0;
        uint16_t mod = 0;
        std::string text;
    };

    void onPump(PumpPhase phase);
    void progress();
    void logLine(std::string line);
    template <class... Args>
    void say(std::format_string<Args...> fmt, Args&&... args);
    [[noreturn]] void abort(std::string why);

    // Input.
    KeyDef keyDef(std::string_view name) const;
    void inject(std::string_view key);  // into the input queue, delivered by this fetch
    void injectClick(int gx, int gy);   // game coordinates
    void post(std::string_view key);    // SDL event for the next pump (menu bar)
    void apply(const Act& a);
    void setHeld(int arrow, bool down);  // 0 left, 1 right, 2 up, 3 down
    void releaseAll();
    void injectKey(std::string_view key, bool down, bool repeat = false);
    bool rateOk(uint64_t ms);           // at most one modal key per `ms` of game time

    // Modes.
    void titleFetch();
    void signInFetch();
    void dialogFetch();
    void hintFetch();
    void quizFetch();
    void animFetch(bool skip);
    void seeRankFetch();
    void roomTick();
    void castleTick();
    bool menuStep();  // advances a posted menu sequence; true while one is running

    // Room helpers.
    Act decideRoom();
    void startBusy(Busy b, int limit);
    bool busyStep();  // true while the bot waits for an action to finish
    struct Range {
        int lo = 0, hi = 0;
        bool facingLeft = false;
    };
    bool standRanges(int room, int slot, std::vector<Range>& out) const;
    int bfsFirstMove(int from, int to, bool allowGate, int* dist) const;  // 0 none, 1 L, 2 R, 3 door, 4 gate
    Act walkToward(int x0, int target);
    Act moveTowardRange(const std::vector<Range>& ranges, bool& inside);
    Act goToRoom(int room);
    Act goToSpot(int room, int slot, bool& ready);
    Act hunt(bool& active);
    Act jumpFountain(int targetX);
    Act wander();
    bool simulateJump(int x0, bool facingLeft) const;
    std::vector<VisRect> spriteMasks() const;
    int questionsLeftOnLevel() const;

    // Castle helpers.
    void planCastle();
    int castleCell(int screen, int col) const;

    Rig& rig_;
    Checker& checker_;
    BotOptions o_;
    game::GameState& s_;
    game::room::RoomTables rt_;
    game::scenes::CastleTables ct_;
    std::mt19937 rng_;
    int hookId_ = 0;

    Mode mode_ = Mode::None;
    BotEnd end_ = BotEnd::Running;
    BotStats stats_;
    std::deque<std::string> recent_;
    uint64_t heartbeat_ = 0;

    // Timing / progress.
    uint64_t lastKeyMs_ = 0;
    uint64_t lastProgressMs_ = 0;
    uint64_t lastClockMs_ = 0;
    uint64_t pumpsSameClock_ = 0;
    uint64_t climbStartMs_ = 0;
    uint64_t lastTickDeadline_ = 0;
    std::array<int, 11> progressKey_{};

    // Sign-in.
    int signInStep_ = 0;
    int signInCycles_ = 0;

    // Dialog intent for the next game dialog.
    DialogIntent dialogIntent_ = DialogIntent::Enter;
    bool dialogExpected_ = false;
    bool dialogAnswered_ = false;
    bool snapshotOnYes_ = false;
    std::string snapshotHow_;

    // Modal sub-states.
    bool modalReady_ = false;   // "clue box mode 2" seen (CONTINUE / dismiss)
    int quizSlot_ = -1;
    int quizStage_ = 0;         // 0 wait screen, 1 answer, 2 result
    int quizSkip_ = 0;
    int quizPos_ = -1;
    bool quizAnswered_ = false;
    bool animSkip_ = false;
    bool rankShown_ = false;

    // Menu sequence (posted keys).
    std::vector<std::string> menuKeys_;
    size_t menuNext_ = 0;
    Mode menuReturn_ = Mode::Room;

    // Climb bookkeeping.
    bool exitRequested_ = false, restartRequested_ = false;
    bool exitDone_ = false, restartDone_ = false;
    int exitPendingTicks_ = 0, restartPendingTicks_ = 0;  // room ticks since the request, until its dialog
    std::vector<uint8_t> leftRecord_;
    std::optional<std::vector<uint8_t>> resumeExpected_;
    std::string resumeHow_;
    int excursionsThisClimb_ = 0;
    uint64_t nextExcursionTick_ = 0;

    // Room tick state.
    Busy busy_ = Busy::None;
    int busyTicks_ = 0, busyLimit_ = 0;
    uint16_t busyCoins_ = 0, busyNets_ = 0;
    int busyRoom_ = -1, tossRoom_ = -1, tossSlot_ = -1;
    uint8_t tossContent_ = 0;
    int lastRoom_ = -1;
    int roomTicksHere_ = 0;
    int huntTicksLevel_ = 0;
    int huntLevel_ = -1;
    int wanderDir_ = 0;
    int bumpRetreat_ = 0;
    int lastFrame_ = 0;
    uint16_t prevCoins_ = 0, prevNets_ = 0;
    bool shopping_ = false;
    bool exhausted_ = false;
    std::array<bool, 4> held_{};

    // Castle state.
    int castleLayout_ = 0;
    int castleScreen_ = 0;
    bool castleWalking_ = true;
    struct Creature {
        int screen = -1, column = -1, y = 0, flip = 0;
        uint64_t seenTick = 0;
        bool active = false;
    } creature_;
    uint64_t castleTickNo_ = 0;
    std::array<int, game::scenes::castle::kScreens> castleTarget_{};
    bool seekCreature_ = false;
};

}  // namespace tmport::autoplay
