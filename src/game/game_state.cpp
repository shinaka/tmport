#include "game/game_state.h"

#include <algorithm>

#include "game/game_hooks.h"
#include "save/user_record.h"

namespace tmport::game {

namespace {

uint16_t rd16(std::span<const uint8_t, kRecordSize> r, size_t o) {
    return static_cast<uint16_t>(r[o] | (r[o + 1] << 8));
}
uint32_t rd32(std::span<const uint8_t, kRecordSize> r, size_t o) {
    return static_cast<uint32_t>(r[o]) | (static_cast<uint32_t>(r[o + 1]) << 8) |
           (static_cast<uint32_t>(r[o + 2]) << 16) | (static_cast<uint32_t>(r[o + 3]) << 24);
}
void wr16(std::span<uint8_t, kRecordSize> r, size_t o, uint16_t v) {
    r[o] = static_cast<uint8_t>(v);
    r[o + 1] = static_cast<uint8_t>(v >> 8);
}
void wr32(std::span<uint8_t, kRecordSize> r, size_t o, uint32_t v) {
    for (size_t i = 0; i < 4; ++i) r[o + i] = static_cast<uint8_t>(v >> (8 * i));
}

}  // namespace

GameHooks& nullHooks() {
    static GameHooks hooks;
    return hooks;
}

std::string PlayerState::nameString() const {
    std::string s;
    for (char c : name) {
        if (c == 0) break;
        s.push_back(c);
    }
    return s;
}

void PlayerState::setName(std::string_view n) {
    const size_t len = std::min(n.size(), kNameSize - 1);
    for (size_t i = 0; i < len; ++i) name[i] = n[i];
    name[len] = 0;
}

GameState GameState::initial(const GameTables& tables) {
    GameState s;
    s.rooms = RoomTable::fromDefs(tables.rooms);
    return s;
}

// 1160:0f28 Users_SaveCurrent (field copies)
void writeRecord(const GameState& s, std::span<uint8_t, kRecordSize> r) {
    // strcpy(rec.name, g_playerName): up to and including the NUL (bounded by the 9-byte field).
    for (size_t i = 0; i < kNameSize; ++i) {
        r[rec::kName + i] = static_cast<uint8_t>(s.player.name[i]);
        if (s.player.name[i] == 0) break;
    }
    r[rec::kTier] = s.rank.rankTier;
    wr32(r, rec::kTotal, s.rank.totalTreasures);
    r[rec::kSoundOn] = s.options.soundOn;
    r[rec::kVoiceOn] = s.options.voiceOn;
    r[rec::kHintsOn] = s.options.hintsOn;
    wr32(r, rec::kHintUnseen, s.hints.unseen);
    r[rec::kHaveKey] = s.inv.haveKey;
    wr16(r, rec::kCoins, s.inv.coins);
    wr16(r, rec::kNets, s.inv.nets);
    r[rec::kGemsFound] = s.inv.gemsFound;
    wr16(r, rec::kClimbPoints, s.inv.climbPoints);
    wr16(r, rec::kShopCoinsLeft, static_cast<uint16_t>(s.shop.coinsLeft));
    r[rec::kNumClues] = s.inv.numClues;
    for (size_t l = 0; l < kLevelCount; ++l) {
        r[rec::kClueFlags + l] = s.clues.flags[l];
        for (size_t k = 0; k < 3; ++k) r[rec::kClueAttr + 3 * l + k] = s.clues.attr[l][k];
        r[rec::kCluePlural + l] = s.clues.plural[l];
    }
    r[rec::kQuizTier] = s.quiz.tier;
    for (size_t q = 0; q < kQuestionCount; ++q) r[rec::kQuizStatus + q] = s.quiz.status[q];
    for (size_t i = 0; i < kQuizSlotCount; ++i) {
        const size_t o = rec::kQuizSlots + 8 * i;
        const QuizSlot& slot = s.quiz.slots[i];
        wr16(r, o, static_cast<uint16_t>(slot.q));
        for (size_t k = 0; k < 3; ++k) {
            r[o + 2 + k] = slot.perm[k];
            r[o + 5 + k] = slot.tried[k];
        }
    }
    r[rec::kRoom] = s.player.room;
    for (int room = 0; room < kRoomCount; ++room) {
        for (int slot = 0; slot < 3; ++slot) {
            const size_t i = static_cast<size_t>(room * 3 + slot);
            r[rec::kSpotKind + i] = s.rooms.spotKind(room, slot);
            r[rec::kSpotContent + i] = s.rooms.spotContent(room, slot);
        }
    }
    wr16(r, rec::kClimbsDone, s.rank.climbsDone);
}

std::array<uint8_t, kRecordSize> toRecord(const GameState& s) {
    std::array<uint8_t, kRecordSize> r = s.recordImage;
    writeRecord(s, r);
    return r;
}

// 1160:137d Users_LoadSelected (field copies)
void fromRecord(GameState& s, std::span<const uint8_t, kRecordSize> r) {
    std::copy(r.begin(), r.end(), s.recordImage.begin());
    // strcpy(g_playerName, rec.name): bytes after the NUL keep their previous values.
    for (size_t i = 0; i < kNameSize; ++i) {
        s.player.name[i] = static_cast<char>(r[rec::kName + i]);
        if (r[rec::kName + i] == 0) break;
    }
    s.rank.rankTier = r[rec::kTier];
    s.rank.totalTreasures = rd32(r, rec::kTotal);
    s.options.soundOn = r[rec::kSoundOn];
    s.options.voiceOn = r[rec::kVoiceOn];
    s.options.hintsOn = r[rec::kHintsOn];
    s.hints.unseen = rd32(r, rec::kHintUnseen);
    s.inv.haveKey = r[rec::kHaveKey];
    s.inv.coins = rd16(r, rec::kCoins);
    s.inv.nets = rd16(r, rec::kNets);
    s.inv.gemsFound = r[rec::kGemsFound];
    s.inv.climbPoints = rd16(r, rec::kClimbPoints);
    s.shop.coinsLeft = static_cast<int16_t>(rd16(r, rec::kShopCoinsLeft));
    s.inv.numClues = r[rec::kNumClues];
    for (size_t l = 0; l < kLevelCount; ++l) {
        s.clues.flags[l] = r[rec::kClueFlags + l];
        for (size_t k = 0; k < 3; ++k) s.clues.attr[l][k] = r[rec::kClueAttr + 3 * l + k];
        s.clues.plural[l] = r[rec::kCluePlural + l];
    }
    s.quiz.tier = r[rec::kQuizTier];
    for (size_t q = 0; q < kQuestionCount; ++q) s.quiz.status[q] = r[rec::kQuizStatus + q];
    for (size_t i = 0; i < kQuizSlotCount; ++i) {
        const size_t o = rec::kQuizSlots + 8 * i;
        QuizSlot& slot = s.quiz.slots[i];
        slot.q = static_cast<int16_t>(rd16(r, o));
        for (size_t k = 0; k < 3; ++k) {
            slot.perm[k] = r[o + 2 + k];
            slot.tried[k] = r[o + 5 + k];
        }
    }
    s.player.room = r[rec::kRoom];
    for (int room = 0; room < kRoomCount; ++room) {
        for (int slot = 0; slot < 3; ++slot) {
            const size_t i = static_cast<size_t>(room * 3 + slot);
            s.rooms.spotKind(room, slot) = r[rec::kSpotKind + i];
            s.rooms.spotContent(room, slot) = r[rec::kSpotContent + i];
        }
    }
    s.rank.climbsDone = rd16(r, rec::kClimbsDone);
}

save::PlayerRecord toPlayerRecord(const GameState& s) {
    save::UserSlot slot;
    slot.bytes = toRecord(s);
    return save::PlayerRecord::fromSlot(slot);
}

void fromPlayerRecord(GameState& s, const save::PlayerRecord& r) {
    save::UserSlot slot;
    slot.bytes = s.recordImage;
    r.writeTo(slot);
    fromRecord(s, slot.bytes);
}

}  // namespace tmport::game
