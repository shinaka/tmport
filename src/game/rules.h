#pragma once
// Game rules that are not tied to one screen: inventory (10e8 Inv_*), ranks and tiers
// (10e8:0790/07a6, 10f0:030d/07f5), the hint queue (1130:0000 and its triggers), treasure picks
// (1198:0103), the rescue coin pile (1110:00aa), the net shop counter (10e0:0766, 10f8:0241),
// the coin toss result, and the climb flow of GameMain (1118:0000: new player, climb set-up,
// zone/level change, climb end). All randomness goes through the Borland RNG in the original's
// call order (ARCHITECTURE.md §5.16). Specs: ARCHITECTURE.md §2.6-2.10, §5.

#include <array>
#include <cstdint>
#include <string>

#include "game/game_hooks.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "mischief/rtl/borland_rand.h"

namespace tmport::game {

// --- Inventory (10e8 status.cpp) -----------------------------------------------------------------
// 10e8:06ff Inv_AddCoins / 10e8:0720 Inv_AddNets: a decrement below 0 is refused silently
// (returns false); otherwise the value changes (16-bit) and the HUD is redrawn.
bool addCoins(GameState& s, int delta, GameHooks& hooks = nullHooks());
bool addNets(GameState& s, int delta, GameHooks& hooks = nullHooks());
// 10e8:06c9 Inv_SetHaveKey (sfx 12 when set).
void setHaveKey(GameState& s, bool have, GameHooks& hooks = nullHooks());
// 10e8:06e7 Inv_AddClue: numClues++, hint 3 when it reaches 2.
void addClue(GameState& s, const GameTables& t);
// 10e8:0741 Inv_ResetGems.
void resetGems(GameState& s, GameHooks& hooks = nullHooks());
// 10e8:074a Inv_CollectTreasure: hint 4, gemsFound++, treasureCount2++, climbPoints++ on levels
// 0..2, sfx 41.
void collectTreasure(GameState& s, const GameTables& t, GameHooks& hooks = nullHooks());
// HUD number strings (ASCII digits only; 10e8:02cf/034d/04b2): coins/nets show one digit below
// 10, else the last two digits; climb points show 1..5 digits.
std::string hudTwoDigits(uint16_t v);
std::string hudPointsDigits(uint16_t v);

// --- Ranks -----------------------------------------------------------------------------------------
// 10e8:07a6 Rank_FromTotal: (number of thresholds <= total) - 1.
int rankFromTotal(uint32_t total, const GameTables& t);
// 10e8:0790 Rank_UpdateTier: tier = rank / 2.
void updateRankTier(GameState& s, const GameTables& t);

// --- Hints (1130) ----------------------------------------------------------------------------------
// 1130:0000 QueueHint: one pending hint; once-only bits are consumed when queued.
void queueHint(GameState& s, int event, const GameTables& t);
// The per-tick triggers of Room_Run (10e0:007d): 0, 2, and in room 9 after the first climb the
// rank hint DS:0e8f[rank] (10e0:10b2).
void roomTickHints(GameState& s, const GameTables& t);
// Room_Run shows a pending hint (when no elf was caught this tick) and clears it: returns the
// event or -1.
int takePendingHint(GameState& s);
// 1140:0015 MenuCmd_NewPlayerMessages: off clears the mask and the pending hint, on sets all bits.
void setHintsOption(GameState& s, bool on);

// --- Space / Enter economy (10e0:0447) -------------------------------------------------------------
// Space: with nets a net swing starts, without nets an empty swing and hint 5 (coins left) or 7;
// then Inv_AddNets(-1). Returns true for a real net swing.
bool pressNetKey(GameState& s, const GameTables& t, GameHooks& hooks = nullHooks());
// Enter: with coins a coin toss starts, else hint 6 (nets left) or 7; then Inv_AddCoins(-1).
// Returns true when a toss starts.
bool pressCoinKey(GameState& s, const GameTables& t, GameHooks& hooks = nullHooks());

// Result of a coin toss at step 13 (10e0:0766): what the targeted spot held.
enum class TossResult { Nothing, Treasure, Key, ShopCoin, ShopOpens };
struct TossOutcome {
    TossResult result = TossResult::Nothing;
    int16_t walkToX = 0;  // ShopOpens: auto-walk target DS:0e89[level] (y 142)
};
// Applies the step-13 rules to (current room, slot): treasure -> collectTreasure, content := 0;
// key -> setHaveKey(1), hint 9, content := 0; 0xFD -> shopCoinsLeft--, and at <= 0 the shop opens
// (shop.state := 1; the player controller locks input, sets frame 0 and walks to walkToX);
// anything else -> the coin is lost.
TossOutcome resolveCoinToss(GameState& s, int slot, const GameTables& t, GameHooks& hooks = nullHooks());
// Up in a third whose content is 0xFE (10e0:0d18): with the key (haveKey == 1) the gate opens and
// the player auto-walks to x 330 / 281 / 170 (rooms 11 / 25 / 38, constants in the code, y 142;
// the controller locks input and sets frame 0); without it hint 8 is queued.
struct GateOutcome {
    bool opens = false;
    int16_t walkToX = -1;
};
GateOutcome tryGate(GameState& s, const GameTables& t);
// 10f8:0241 NetShop_OnNetTaken, economy part: shop.state := 3, coinsLeft := 4, +10 nets (the
// player controller restores its own pose and input lock).
void netShopTakeNet(GameState& s, GameHooks& hooks = nullHooks());

// --- Treasures (1198:0103) -------------------------------------------------------------------------
// Treasures_PickForLevel: only on levels 0..2. gemsNeeded[tier] treasure indices: base =
// climbsDone mod 43; element 0 = base if no treasure was found this climb (treasureCount2 == 0),
// otherwise random; the others base + R(51 - base), distinct (re-rolled).
void pickTreasuresForLevel(GameState& s, const GameTables& t, mischief::BorlandRand& rng);
// The treasure shown for the gemsFound-th find on this level (BNDL 330 + index).
int treasureForNextFind(const GameState& s);
// Treasure delivered at the end of a climb, after climbsDone++ (10f0:0000): (climbsDone-1) mod 43.
int deliveredTreasureIndex(uint16_t climbsDoneAfterIncrement);

// --- Rescue coin pile (1110:00aa) ------------------------------------------------------------------
// CoinPile_Update, once per room tick. Returns true when the pile was picked up (+10 coins, sfx 7).
bool updateCoinPile(GameState& s, mischief::BorlandRand& rng, GameHooks& hooks = nullHooks());

// --- Quiz answer, inventory side (1150:0403 + 1150:100a) -------------------------------------------
// After a correct quizJudgeAnswer and the praise screen: Inv_AddClue, then +2/+1/0 coins.
// Returns the coins awarded.
int finishCorrectAnswer(GameState& s, int slot, const GameTables& t, GameHooks& hooks = nullHooks());
struct AnswerOutcome {
    bool correct = false;
    int messageIndex = 0;  // R(4): praise text / voice (correct) or sorry text / voice (wrong)
    int clueIndex = -1;    // correct: clue word shown and spoken (numClues before the increment)
    int coins = 0;         // correct: coins awarded
};
// The whole answer path of Quiz_AskQuestion in the original order: judge (state), the result
// screen's R(4), then for a correct answer the clue word and the coins.
AnswerOutcome answerQuestion(GameState& s, int slot, int pos, const GameTables& t, mischief::BorlandRand& rng,
                             GameHooks& hooks = nullHooks());

// --- Castle wall per tier (1168:0047, 1168:1143) ---------------------------------------------------
// ClimbScene_PickLayout: R(tier + 1).
int pickCastleLayout(uint8_t rankTier, mischief::BorlandRand& rng);
// Creature spawn test of ClimbScene_UpdateCreature: R(1000) < permille[tier].
bool creatureSpawnRoll(uint8_t rankTier, const GameTables& t, mischief::BorlandRand& rng);

// --- Climb flow (1118:0000 GameMain, 1160:1632, 1160:137d, 10f0:0000) ------------------------------
// 1160:1632 Users_InitNewPlayer (sound is switched on for a new player; voice and the per-climb
// counters keep their values, the climb set-up resets them).
void initNewPlayer(GameState& s, GameHooks& hooks = nullHooks());
// 1160:137d Users_LoadSelected: fromRecord plus the sound quirk -- loading a player can switch the
// sound off but never on (g_soundOn before the load = s.options.soundOn).
void loadPlayerRecord(GameState& s, std::span<const uint8_t, kRecordSize> record, GameHooks& hooks = nullHooks());

struct ClimbSetup {
    bool newClimb = false;
    bool quizPromoted = false;
    std::array<int, kLevelCount> layoutRecord{-1, -1, -1};
};
// Climb-loop steps 3..9 of GameMain (1118:00e5..0257) after srand: treasure hall rooms; on a new
// climb (room 0xFE) reset the per-climb counters, Quiz_NewRound, Layout_GenerateClimb and the start
// room (9 before the first finished climb, else 0); clue texts and voice ids; spawn elves
// (hooks.spawnElves); pick treasures; the player pose from the room; reset gems on a new climb.
// The caller seeds the RNG first (srand(low 16 bits of timeGetTime()), 1118:00e5).
ClimbSetup setupClimb(GameState& s, const GameTables& t, mischief::BorlandRand& rng, GameHooks& hooks = nullHooks());

// After Room_Run changed the room (1118 climb loop): when the music zone changed, re-pick the
// treasures, and when the level also changed (through a gate) first unload elves, clear the
// clues, carry missed questions, clear key and gems, then (after the cut-scene, which is the
// caller's) respawn elves. Returns true when the level changed. Music and transitions are the
// caller's.
bool onRoomChanged(GameState& s, int oldZone, int oldLevel, const GameTables& t, mischief::BorlandRand& rng,
                   GameHooks& hooks = nullHooks());

// End of a climb, before the save (1118:0378..03b4): room is already 0xFE; elves unloaded, clues
// cleared, key and gems reset.
void endClimbBeforeSave(GameState& s, GameHooks& hooks = nullHooks());
struct CelebrationInfo {
    bool promoted = false;
    bool champion = false;
    int rank = 0;
    int deliveredTreasure = 0;  // BNDL 330 + index (DAM 20302 frame slot 80)
    int musicZone = 0;          // -98 FINALE (champion) or -96 THRONE
};
// 10f0:0000 Climb_Celebrate, rule part: climbsDone++, Rank_AddClimbPoints (saturating total,
// promoted/champion flags), Rank_QueueResultHint (pending cleared first).
CelebrationInfo celebrateClimb(GameState& s, const GameTables& t);

}  // namespace tmport::game
