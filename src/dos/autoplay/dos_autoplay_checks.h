#pragma once
// Invariants of the DOS edition's game rules that the autoplayer checks while it plays (area
// autoplay, wave 5; rules: re/dos/docs/ARCHITECTURE.md §5, code_world.md, code_player.md,
// code_elves_layout_hints.md, code_ui.md §9-§10). Each violation is a short text; a soak run fails on
// any.
//
//   ranges     coins and nets never wrap below zero (u16 above 0x7FFF), key state one of 0/2/4/6/8/10/12,
//              tier <= 3, level <= 4, player state <= 71, clue words valid word indices
//   coins      every change of g_coins in a tick is explained by what happened in it: a coin dropped
//              (player state 27/28 entered) -1, elf dust (an elf entering 54/58) -1, the castle creature
//              -1, a net catch (an elf entering 67/69) +1, the coin creature (72 -> 25) +2, the bonus
//              spot (picture 0x4B -> 0x49) +5, closing the question screen = the screen's coins (reward
//              0/1/2 after the catch); nets: -1 for a tick with catches, +5 entering the paid net shop
//   spots      at every climb set-up: per level exactly one key (0x66 + 2 level), tier + 2 treasures,
//              every place filled, the mirror cells equal their sources; always: treasures found =
//              the bits of the found masks = the searched treasures of the levels; a key state of
//              2/4/6 means that level's key was searched, 8/10/12 that it was not
//   tier       at a new climb: tier from the total (>= 230: 3, >= 115: 2, >= 25: 1, else the previous
//              one), castle layout <= tier, the spawned elves of each kind by tier, bridges break only
//              from tier 2 (fast CPU), the castle creature only from tier 1, dust only from throwers
//   world      on a mountain level the player never stands in an empty cell (type 0: the void, a bug
//              path of the original), positions stay inside the 16 x 16 strip
//   clue words revealed words only grow during a game, one per correct answer at most; the collected
//              treasures are treasure codes, each at most once
//   climb end  the castle top adds the treasures found to the total (16 bits); Game_End: games played
//              + 1, the coins and nets banked, no saved game
//   saves      after every USERS.DAT write: the file equals the store's image byte for byte, parses and
//              writes back unchanged, and the selected record equals what Users_Pack makes of the game
//              state (packed into a copy)
//   resume     a resumed game packs to the record it was saved as, byte for byte

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dos/save/users_dat.h"

namespace tmport::dos {
class DosGame;
class DosUsersStore;
}  // namespace tmport::dos

namespace tmport::dos::autoplay {

// The few values of the previous tick the per-tick checks compare with.
struct TickSnapshot {
    bool valid = false;
    uint16_t coins = 0, nets = 0, playerState = 0, level = 0, scene = 0, keyState = 0, tier = 0;
    uint16_t treasuresFound = 0, uiMode = 0, quizCoins = 0, quizClosed = 1, bonusPicture = 0;
    int16_t shopOwed = 0;
    uint16_t creatureCell = 0, creatureStrike = 0, area = 0;
    uint16_t total = 0;
    int16_t userRec = -1;
    int saves = 0;
    int revealed = 0;          // clue words revealed (all levels)
    std::array<uint16_t, 15> elfState{}, elfBase{};
};
TickSnapshot snapshotOf(const DosGame& g, const DosUsersStore& users);

class Checker {
public:
    Checker() = default;

    // After every game tick: `before` is the snapshot taken before it.
    void onTick(const DosGame& g, const TickSnapshot& before, const TickSnapshot& after);
    // A climb starts (a new layout, or a restored game).
    void onClimbSetup(const DosGame& g, bool newClimb, uint16_t previousTier);
    // The castle top added the treasures (player state 41 entered from the castle).
    void onCastleTop(const DosGame& g, uint32_t totalBefore, uint16_t found);
    // After a USERS.DAT write of `moment` (DosUsersStore::lastMoment).
    void onSave(DosGame& g, const DosUsersStore& store, std::string_view moment, uint16_t gamesBefore);
    // The record the sign-in read from dos_users.dat must be the record that was saved, byte for byte.
    void compareLoaded(const UserRecord& loaded, const UserRecord& saved, std::string_view how);
    // A resumed game: the record the game packs to now must equal the record it was saved as.
    void compareResume(DosGame& g, const UserRecord& saved, std::string_view how);

    const std::vector<std::string>& violations() const { return violations_; }
    const std::vector<std::string>& notes() const { return notes_; }
    uint64_t checks() const { return checks_; }
    uint64_t saveChecks() const { return saveChecks_; }

    void fail(std::string what);
    void note(std::string what);

    // What Users_Pack makes of the game state now, without changing the store's image.
    static UserRecord packedRecord(DosGame& g);

private:
    void checkRanges(const DosGame& g);
    void checkSpots(const DosGame& g, bool setup);
    bool once(const std::string& key);

    std::vector<std::string> violations_;
    std::vector<std::string> notes_;
    std::vector<std::string> seen_;
    uint16_t quizOpenCoins_ = 0;  // g_coins when the question screen opened (the reward's base)
    uint64_t checks_ = 0;
    uint64_t saveChecks_ = 0;
};

}  // namespace tmport::dos::autoplay
