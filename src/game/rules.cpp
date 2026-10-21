#include "game/rules.h"

#include <algorithm>

#include "game/layout.h"
#include "game/quiz.h"

namespace tmport::game {

namespace {

// Hint events with fixed meanings (ARCHITECTURE.md §5.15).
constexpr int kHintFirstRoom = 0;
constexpr int kHintFirstRoom2 = 2;
constexpr int kHintSecondClue = 3;
constexpr int kHintTreasure = 4;
constexpr int kHintNoNets = 5;
constexpr int kHintNoCoins = 6;
constexpr int kHintNothingLeft = 7;
constexpr int kHintGateNoKey = 8;
constexpr int kHintKeyFound = 9;

// Sound effects (SSND 7000 + n, fmt_sound.md §7.2).
constexpr int kSfxCoinPile = 7;
constexpr int kSfxKey = 12;
constexpr int kSfxTreasure = 41;

constexpr int kCoinPileMinX = 50;   // 0x32
constexpr int kCoinPileRange = 412; // 0x19c
constexpr int kCastleChampionRank = 7;
constexpr int kMusicFinale = -98;
constexpr int kMusicThrone = -96;

bool addClamped(uint16_t& v, int delta) {
    // Inv_AddCoins / Inv_AddNets: (delta >= 0) || (uint)-delta < v || -v == delta
    const int16_t d = static_cast<int16_t>(delta);
    if (d >= 0 || static_cast<uint16_t>(-d) <= v) {
        v = static_cast<uint16_t>(v + d);
        return true;
    }
    return false;
}

int16_t tableAt(const std::array<int16_t, kRankCount>& table, int index) {
    return index >= 0 && index < kRankCount ? table[static_cast<size_t>(index)] : int16_t{-1};
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Inventory

// 10e8:06ff Inv_AddCoins
bool addCoins(GameState& s, int delta, GameHooks& hooks) {
    if (!addClamped(s.inv.coins, delta)) return false;
    hooks.hudChanged();
    return true;
}

// 10e8:0720 Inv_AddNets
bool addNets(GameState& s, int delta, GameHooks& hooks) {
    if (!addClamped(s.inv.nets, delta)) return false;
    hooks.hudChanged();
    return true;
}

// 10e8:06c9 Inv_SetHaveKey
void setHaveKey(GameState& s, bool have, GameHooks& hooks) {
    s.inv.haveKey = have ? 1 : 0;
    if (have) hooks.playSfx(kSfxKey);
    hooks.hudChanged();
}

// 10e8:06e7 Inv_AddClue
void addClue(GameState& s, const GameTables& t) {
    ++s.inv.numClues;
    if (s.inv.numClues == 2) queueHint(s, kHintSecondClue, t);
}

// 10e8:0741 Inv_ResetGems
void resetGems(GameState& s, GameHooks& hooks) {
    s.inv.gemsFound = 0;
    hooks.hudChanged();
}

// 10e8:074a Inv_CollectTreasure
void collectTreasure(GameState& s, const GameTables& t, GameHooks& hooks) {
    queueHint(s, kHintTreasure, t);
    ++s.inv.gemsFound;
    ++s.inv.treasureCount2;
    const uint8_t level = s.rooms.level(s.player.room);
    if (level == 0 || level == 1 || level == 2) ++s.inv.climbPoints;
    hooks.playSfx(kSfxTreasure);
    hooks.hudChanged();
}

std::string hudTwoDigits(uint16_t v) {
    if (v < 10) return std::string(1, static_cast<char>('0' + v));
    return {static_cast<char>('0' + (v / 10) % 10), static_cast<char>('0' + v % 10)};
}

std::string hudPointsDigits(uint16_t v) {
    std::string s = std::to_string(v);  // 1..5 digits, exactly the original's cases
    return s;
}

// ---------------------------------------------------------------------------------------------
// Ranks

// 10e8:07a6 Rank_FromTotal
int rankFromTotal(uint32_t total, const GameTables& t) {
    int i = 0;
    while (i < kRankCount && t.rankThresholds[static_cast<size_t>(i)] <= total) ++i;
    return i - 1;
}

// 10e8:0790 Rank_UpdateTier
void updateRankTier(GameState& s, const GameTables& t) {
    s.rank.rankTier = static_cast<uint8_t>(rankFromTotal(s.rank.totalTreasures, t) / 2);
}

// ---------------------------------------------------------------------------------------------
// Hints

// 1130:0000 QueueHint
void queueHint(GameState& s, int event, const GameTables& t) {
    if (s.hints.pending >= 0) return;
    if (event < 0 || event >= kHintEventCount) return;
    const int bit = t.hintBit[static_cast<size_t>(event)];
    if (bit < 0) {
        s.hints.pending = static_cast<int16_t>(event);
    } else {
        const uint32_t mask = 1u << (bit & 31);
        if (s.hints.unseen & mask) {
            s.hints.unseen ^= mask;
            s.hints.pending = static_cast<int16_t>(event);
        }
    }
}

// 10e0:007d (per tick) + 10e0:10b2 Room9_QueueRankHint
void roomTickHints(GameState& s, const GameTables& t) {
    queueHint(s, kHintFirstRoom, t);
    queueHint(s, kHintFirstRoom2, t);
    if (s.player.room == kMountainStartRoom && s.rank.climbsDone != 0)
        queueHint(s, tableAt(t.room9RankHints, rankFromTotal(s.rank.totalTreasures, t)), t);
}

int takePendingHint(GameState& s) {
    const int e = s.hints.pending;
    s.hints.pending = -1;
    return e;
}

// 1140:0015 MenuCmd_NewPlayerMessages (after the dialog answered)
void setHintsOption(GameState& s, bool on) {
    s.options.hintsOn = on ? 1 : 0;
    if (on) {
        s.hints.unseen = 0xFFFFFFFFu;
    } else {
        s.hints.pending = -1;
        s.hints.unseen = 0;
    }
}

// ---------------------------------------------------------------------------------------------
// Space / Enter, coin toss, net shop (10e0:0447, 10f8:0241)

bool pressNetKey(GameState& s, const GameTables& t, GameHooks& hooks) {
    bool swing = true;
    if (s.inv.nets == 0) {
        swing = false;  // empty-handed swing (g_emptySwingStep = 1)
        queueHint(s, s.inv.coins == 0 ? kHintNothingLeft : kHintNoNets, t);
    }
    addNets(s, -1, hooks);
    return swing;
}

bool pressCoinKey(GameState& s, const GameTables& t, GameHooks& hooks) {
    bool toss = true;
    if (s.inv.coins == 0) {
        toss = false;
        queueHint(s, s.inv.nets == 0 ? kHintNothingLeft : kHintNoCoins, t);
    }
    addCoins(s, -1, hooks);
    return toss;
}

TossOutcome resolveCoinToss(GameState& s, int slot, const GameTables& t, GameHooks& hooks) {
    TossOutcome out;
    const int room = s.player.room;
    if (!RoomTable::valid(room) || slot < 0 || slot > 2) return out;
    uint8_t& content = s.rooms.spotContent(room, slot);
    if (content == kContentTreasure) {
        collectTreasure(s, t, hooks);
        content = kContentEmpty;
        out.result = TossResult::Treasure;
    } else if (content == kContentKey) {
        setHaveKey(s, true, hooks);
        queueHint(s, kHintKeyFound, t);
        content = kContentEmpty;
        out.result = TossResult::Key;
    } else if (content == kSpotShop) {
        const int16_t before = s.shop.coinsLeft;
        s.shop.coinsLeft = static_cast<int16_t>(before - 1);
        if (s.shop.coinsLeft == 0 || before < 1) {
            const uint8_t level = s.rooms.level(room);
            out.walkToX = level < kLevelCount ? t.shopWalkX[level] : int16_t{0};
            s.shop.state = 1;
            out.result = TossResult::ShopOpens;
        } else {
            out.result = TossResult::ShopCoin;
        }
    }
    return out;
}

// 10e0:0d18 (Player_Update, Up at a gate third)
GateOutcome tryGate(GameState& s, const GameTables& t) {
    GateOutcome out;
    if (s.inv.haveKey == 1) {
        out.opens = true;
        switch (s.player.room) {
            case 11: out.walkToX = 0x14A; break;
            case 25: out.walkToX = 0x119; break;
            case 38: out.walkToX = 0xAA; break;
            default: break;  // no gate elsewhere (the original then skips the walk)
        }
    } else {
        queueHint(s, kHintGateNoKey, t);
    }
    return out;
}

// 10f8:0241 NetShop_OnNetTaken
void netShopTakeNet(GameState& s, GameHooks& hooks) {
    s.shop.state = 3;
    s.shop.coinsLeft = 4;
    addNets(s, 10, hooks);
}

// ---------------------------------------------------------------------------------------------
// Treasures

// 1198:0103 Treasures_PickForLevel
void pickTreasuresForLevel(GameState& s, const GameTables& t, mischief::BorlandRand& rng) {
    if (s.rooms.level(s.player.room) >= 3) return;
    const int base = s.rank.climbsDone % kTreasureCycle;
    const int n = std::min<int>(t.gemsNeeded[std::min<size_t>(s.rank.rankTier, kTierCount - 1)], kMaxTreasuresPerLevel);
    for (int i = 0; i < n; ++i) {
        int v = base;
        if (i != 0 || s.inv.treasureCount2 != 0) {
            for (;;) {
                v = rng.random(kTreasureIndexLimit - base) + base;
                int j = 0;
                while (j < i && s.treasures.index[static_cast<size_t>(j)] != v) ++j;
                if (j == i) break;
            }
        }
        s.treasures.index[static_cast<size_t>(i)] = static_cast<int16_t>(v);
    }
    s.treasures.count = n;
}

int treasureForNextFind(const GameState& s) {
    return s.inv.gemsFound < s.treasures.count ? s.treasures.index[s.inv.gemsFound] : -1;
}

int deliveredTreasureIndex(uint16_t climbsDoneAfterIncrement) {
    return static_cast<uint16_t>(climbsDoneAfterIncrement - 1u) % kTreasureCycle;
}

// ---------------------------------------------------------------------------------------------
// Rescue coin pile

// 1110:00aa CoinPile_Update
bool updateCoinPile(GameState& s, mischief::BorlandRand& rng, GameHooks& hooks) {
    CoinPileState& p = s.coinPile;
    const uint8_t room = s.player.room;
    if (room == p.room) {
        // Horizontal overlap only, signed compares.
        if (p.x <= s.player.x + s.player.w && s.player.x <= p.x + p.w) {
            p.room = 0xFF;
            hooks.playSfx(kSfxCoinPile);
            addCoins(s, 10, hooks);
            return true;
        }
    } else if (s.inv.coins == 0 && s.inv.nets == 0) {
        if (p.room == 0xFF) {
            p.room = 0xFE;  // armed: not in the room where the player ran out
            p.noSpawnRoom = room;
        } else if (s.rooms.musicZone(room) != 0 && (p.room != 0xFE || room != p.noSpawnRoom)) {
            p.room = room;  // spawn (or move) here
            p.noSpawnRoom = 0xFF;
            p.x = static_cast<int16_t>(rng.random(kCoinPileRange) + kCoinPileMinX);
        }
    } else {
        p.room = 0xFF;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Quiz answer (inventory side)

int finishCorrectAnswer(GameState& s, int slot, const GameTables& t, GameHooks& hooks) {
    addClue(s, t);  // inside Quiz_JudgeAnswer, after the praise screen
    const int coins = quizCoinsForCorrect(s.quiz.slots[static_cast<size_t>(slot)]);
    if (coins > 0) addCoins(s, coins, hooks);
    return coins;
}

AnswerOutcome answerQuestion(GameState& s, int slot, int pos, const GameTables& t, mischief::BorlandRand& rng,
                             GameHooks& hooks) {
    AnswerOutcome out;
    const QuizJudgement j = quizJudgeAnswer(s.quiz, slot, pos);
    out.correct = j.correct;
    out.messageIndex = rng.random(4);  // Quiz_ShowCorrectScreen / Quiz_ShowWrongScreen
    if (j.correct) {
        out.clueIndex = s.inv.numClues;
        out.coins = finishCorrectAnswer(s, slot, t, hooks);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Castle wall

int pickCastleLayout(uint8_t rankTier, mischief::BorlandRand& rng) { return rng.random(rankTier + 1); }

bool creatureSpawnRoll(uint8_t rankTier, const GameTables& t, mischief::BorlandRand& rng) {
    const int permille = t.creatureSpawnPermille[std::min<size_t>(rankTier, kTierCount - 1)];
    return rng.random(1000) < permille;
}

// ---------------------------------------------------------------------------------------------
// Climb flow

// 1160:1632 Users_InitNewPlayer
void initNewPlayer(GameState& s, GameHooks& hooks) {
    s.rank.rankTier = 0;
    s.rank.totalTreasures = 0;
    if (s.options.soundOn == 0) {  // Audio_ToggleSound: a new player always starts with sound
        s.options.soundOn = 1;
        hooks.soundToggled(true);
    }
    s.options.hintsOn = 1;
    s.hints.unseen = 0xFFFFFFFFu;
    s.inv.coins = 10;
    s.inv.nets = 10;
    s.shop.coinsLeft = 4;
    s.quiz.tier = 0;
    s.rank.climbsDone = 0;
    quizResetHistory(s.quiz);
    s.player.room = kRoomNewClimb;
}

// 1160:137d Users_LoadSelected
void loadPlayerRecord(GameState& s, std::span<const uint8_t, kRecordSize> record, GameHooks& hooks) {
    const uint8_t before = s.options.soundOn;
    fromRecord(s, record);
    const uint8_t stored = s.options.soundOn;
    s.options.soundOn = before;
    if (stored != before && before != 0) {
        // Audio_ToggleSound (sound off), then g_soundOn = the record's value.
        s.options.soundOn = 0;
        hooks.soundToggled(false);
        s.options.soundOn = stored;
    }
}

// 1118:00e5..0257
ClimbSetup setupClimb(GameState& s, const GameTables& t, mischief::BorlandRand& rng, GameHooks& hooks) {
    ClimbSetup out;
    // A record only ever stores a room 0..49 or 0xFE; anything else (a damaged file) would make
    // the original read outside the room table, so it starts a new climb instead.
    if (!RoomTable::valid(s.player.room)) s.player.room = kRoomNewClimb;
    s.rooms.buildTreasureHall(s.rank.climbsDone);
    if (s.player.room == kRoomNewClimb) {
        s.inv.haveKey = 0;
        s.inv.treasureCount2 = 0;
        s.inv.gemsFound = 0;
        s.inv.climbPoints = 0;
        s.inv.numClues = 0;
        out.quizPromoted = quizNewRound(s.quiz, t.quiz, rng);
        const LayoutChoice c = generateClimbLayout(s.rooms, s.clues, s.rank.rankTier, t, rng);
        out.layoutRecord = c.record;
        s.player.room = static_cast<uint8_t>(s.rank.climbsDone == 0 ? kMountainStartRoom : kHallFirstRoom);
        s.player.newClimb = 1;
        out.newClimb = true;
    } else {
        s.player.newClimb = 0;
    }
    buildClueTexts(s.clues, t.words);
    buildClueVoiceIds(s.clues);
    hooks.spawnElves(s, t.elfCountByTier[std::min<size_t>(s.rank.rankTier, kTierCount - 1)]);
    pickTreasuresForLevel(s, t, rng);
    // Music_PlayZone(room.music) is the caller's.
    const RoomDef& r = s.rooms[s.player.room];
    s.player.x = r.startX;
    s.player.y = r.startY;
    s.player.frame = r.startFrame;
    s.player.facingLeft = r.startFacingLeft;
    if (s.player.newClimb) resetGems(s, hooks);
    return out;
}

// 1118 climb loop, after Room_Run
bool onRoomChanged(GameState& s, int oldZone, int oldLevel, const GameTables& t, mischief::BorlandRand& rng,
                   GameHooks& hooks) {
    if (s.rooms.musicZone(s.player.room) == oldZone) return false;
    bool levelChanged = false;
    if (s.rooms.level(s.player.room) != oldLevel) {
        hooks.unloadElves(s);
        s.inv.numClues = 0;
        quizCarryMissedToNextLevel(s.quiz, oldLevel);
        setHaveKey(s, false, hooks);
        resetGems(s, hooks);
        // LevelCutscene_Play(oldLevel) and g_roomTransition = 1 are the caller's; the cut-scene
        // draws no random numbers, so respawning here keeps the original call order.
        hooks.spawnElves(s, t.elfCountByTier[std::min<size_t>(s.rank.rankTier, kTierCount - 1)]);
        levelChanged = true;
    }
    pickTreasuresForLevel(s, t, rng);
    return levelChanged;
}

// 1118:0378..03b4
void endClimbBeforeSave(GameState& s, GameHooks& hooks) {
    hooks.unloadElves(s);
    s.inv.numClues = 0;
    setHaveKey(s, false, hooks);
    resetGems(s, hooks);
}

// 10f0:0000 Climb_Celebrate (rules) = climbsDone++, 10f0:030d, 10f0:07f5
CelebrationInfo celebrateClimb(GameState& s, const GameTables& t) {
    ++s.rank.climbsDone;
    // 10f0:030d Rank_AddClimbPoints
    s.rank.rankPromoted = 0;
    s.rank.isChampion = 0;
    const int before = rankFromTotal(s.rank.totalTreasures, t);
    s.rank.totalTreasures += s.inv.climbPoints;  // 32-bit add, then saturate (as compiled)
    if (s.rank.totalTreasures > 0xFFFFFFF0u) s.rank.totalTreasures = 0xFFFFFFF0u;
    s.rank.rank = static_cast<int16_t>(before);
    if (s.rank.totalTreasures != 0) {
        s.rank.rank = static_cast<int16_t>(rankFromTotal(s.rank.totalTreasures, t));
        if (s.rank.rank != before) s.rank.rankPromoted = 1;
        if (s.rank.rank == kCastleChampionRank) s.rank.isChampion = 1;
    }
    // 10f0:07f5 Rank_QueueResultHint
    s.hints.pending = -1;
    queueHint(s, tableAt(s.rank.rankPromoted ? t.rankPromoHints : t.rankStayHints, s.rank.rank), t);

    CelebrationInfo c;
    c.promoted = s.rank.rankPromoted != 0;
    c.champion = s.rank.isChampion != 0;
    c.rank = s.rank.rank;
    c.deliveredTreasure = deliveredTreasureIndex(s.rank.climbsDone);
    c.musicZone = c.champion ? kMusicFinale : kMusicThrone;
    return c;
}

}  // namespace tmport::game
