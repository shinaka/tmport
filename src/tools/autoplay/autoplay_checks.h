#pragma once
// Invariants of the game rules (re/docs/ARCHITECTURE.md §2.10-§2.11, §4.3, §5) that the
// autoplayer checks while it plays. Each violation is a short text; the soak test fails on any.
//
//   ranges        coins / nets never negative (16-bit wrap), haveKey 0/1, clue words 0..3, gems
//                 <= treasures per level, shop coins 0..4, hint -1..33, tiers, room numbers;
//                 coins / nets above the HUD's two digits are only counted (notes)
//   spots         exactly one key per level (the current level: key on the mountain + haveKey ==
//                 1; higher levels 1; lower levels 0), treasures per level by tier (current level:
//                 hidden + gemsFound == gemsNeeded[tier]), climb points == treasures found
//   one-way       no exit of a mountain room leads to another level; the player's level never
//                 goes down during a climb
//   quiz          history 0..3 (+ the dealt bit), slots hold a valid question with a permutation
//                 of the three answers, the right answer never marked tried, dealt bits <= 9
//   saves         users.dat after every save: header + table round-trip byte-identical through
//                 the parser, every slot through PlayerRecord and through GameState, and the
//                 signed-in player's slot equals the record built from the game state
//   climb end     climbsDone + 1, total + climb points (saturating), rank from the thresholds,
//                 tier = rank / 2, champion flag = rank 7

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tmport::game {
struct GameState;
struct GameTables;
}  // namespace tmport::game

namespace tmport::autoplay {

class Checker {
public:
    explicit Checker(const game::GameTables& t) : t_(t) {}

    // A climb starts (S8): newClimb = generated now (not resumed).
    void onClimbSetup(const game::GameState& s, bool newClimb);
    // One room tick (the state after the previous tick).
    void onRoomTick(const game::GameState& s);
    // After Users_SaveCurrent ("save: <why>" trace line).
    void onSave(const game::GameState& s, const std::filesystem::path& usersDat, std::string_view why);
    // "S13 climb end" (before the celebration) and "S13 celebrated" (after Rank_UpdateTier).
    void onClimbEnd(const game::GameState& s);
    void onCelebrated(const game::GameState& s, bool tracePromoted, bool traceChampion);
    // A resumed player (sign-in after Exit / Start Another Game): the loaded record must equal the
    // record saved when the player left (both built with game::toRecord).
    void compareResume(const std::vector<uint8_t>& saved, const std::vector<uint8_t>& loaded, std::string_view how);

    const std::vector<std::string>& violations() const { return violations_; }
    const std::vector<std::string>& notes() const { return notes_; }
    uint64_t checks() const { return checks_; }
    uint64_t saveChecks() const { return saveChecks_; }

    // Adds a violation from outside (the bot: hang, exception, crash in a scene, ...).
    void fail(std::string what);
    void note(std::string what);

private:
    void checkRanges(const game::GameState& s, std::string_view where);
    void checkSpots(const game::GameState& s, std::string_view where);
    void checkQuiz(const game::GameState& s, std::string_view where);
    void checkRoomTable(const game::GameState& s);

    const game::GameTables& t_;
    std::vector<std::string> violations_;
    std::vector<std::string> notes_;
    uint64_t checks_ = 0;
    uint64_t saveChecks_ = 0;
    // The climb being played.
    bool inClimb_ = false;
    int maxLevel_ = 0;
    uint8_t climbTier_ = 0;
    // Climb end snapshot.
    bool haveEnd_ = false;
    uint16_t endClimbs_ = 0;
    uint32_t endTotal_ = 0;
    uint16_t endPoints_ = 0;
    bool coinsNoted_ = false, netsNoted_ = false;
};

}  // namespace tmport::autoplay
