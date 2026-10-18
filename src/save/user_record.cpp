#include "save/user_record.h"

#include <algorithm>

namespace tmport::save {

std::string UserSlot::name() const {
    std::string s;
    for (size_t i = 0; i < kNameSize && bytes[i] != 0; ++i) s.push_back(static_cast<char>(bytes[i]));
    return s;
}

void UserSlot::setName(std::string_view name) {
    const size_t n = std::min(name.size(), kMaxNameLength);
    for (size_t i = 0; i < n; ++i) bytes[off::kName + i] = static_cast<uint8_t>(name[i]);
    bytes[off::kName + n] = 0;
}

PlayerRecord PlayerRecord::fromSlot(const UserSlot& s) {
    PlayerRecord r;
    r.name = s.name();
    r.tier = s.u8(off::kTier);
    r.totalTreasures = s.u32(off::kTotalTreasures);
    r.soundOn = s.u8(off::kSoundOn);
    r.voiceOn = s.u8(off::kVoiceOn);
    r.hintsOn = s.u8(off::kHintsOn);
    r.hintUnseenMask = s.u32(off::kHintMask);
    r.haveKey = s.u8(off::kHaveKey);
    r.coins = s.u16(off::kCoins);
    r.nets = s.u16(off::kNets);
    r.gemsFound = s.u8(off::kGemsFound);
    r.climbTreasures = s.u16(off::kClimbTreasures);
    r.shopCoinsLeft = static_cast<int16_t>(s.u16(off::kShopCoinsLeft));
    r.clueWords = s.u8(off::kClueWords);
    for (size_t i = 0; i < 3; ++i) {
        r.clueFlags[i] = s.u8(off::kClueFlags + i);
        r.cluePlural[i] = s.u8(off::kCluePlural + i);
    }
    for (size_t i = 0; i < 9; ++i) r.clueAttr[i] = s.u8(off::kClueAttr + i);
    r.quizTier = s.u8(off::kQuizTier);
    for (size_t i = 0; i < kQuizHistorySize; ++i) r.quizHistory[i] = s.u8(off::kQuizHistory + i);
    for (size_t q = 0; q < static_cast<size_t>(kQuizSlotCount); ++q) {
        const size_t o = off::kQuizSlots + q * 8;
        QuizSlot& qs = r.quizSlots[q];
        qs.id = s.u16(o);
        for (size_t k = 0; k < 3; ++k) {
            qs.order[k] = s.u8(o + 2 + k);
            qs.done[k] = s.u8(o + 5 + k);
        }
    }
    r.location = s.u8(off::kLocation);
    for (size_t i = 0; i < static_cast<size_t>(kRooms) * 3; ++i) {
        r.spotKind[i] = s.u8(off::kSpotKind + i);
        r.spotContent[i] = s.u8(off::kSpotContent + i);
    }
    r.climbsDone = s.u16(off::kClimbsDone);
    return r;
}

void PlayerRecord::writeTo(UserSlot& s) const {
    s.setName(name);
    s.setU8(off::kTier, tier);
    s.setU32(off::kTotalTreasures, totalTreasures);
    s.setU8(off::kSoundOn, soundOn);
    s.setU8(off::kVoiceOn, voiceOn);
    s.setU8(off::kHintsOn, hintsOn);
    s.setU32(off::kHintMask, hintUnseenMask);
    s.setU8(off::kHaveKey, haveKey);
    s.setU16(off::kCoins, coins);
    s.setU16(off::kNets, nets);
    s.setU8(off::kGemsFound, gemsFound);
    s.setU16(off::kClimbTreasures, climbTreasures);
    s.setU16(off::kShopCoinsLeft, static_cast<uint16_t>(shopCoinsLeft));
    s.setU8(off::kClueWords, clueWords);
    for (size_t i = 0; i < 3; ++i) {
        s.setU8(off::kClueFlags + i, clueFlags[i]);
        s.setU8(off::kCluePlural + i, cluePlural[i]);
    }
    for (size_t i = 0; i < 9; ++i) s.setU8(off::kClueAttr + i, clueAttr[i]);
    s.setU8(off::kQuizTier, quizTier);
    for (size_t i = 0; i < kQuizHistorySize; ++i) s.setU8(off::kQuizHistory + i, quizHistory[i]);
    for (size_t q = 0; q < static_cast<size_t>(kQuizSlotCount); ++q) {
        const size_t o = off::kQuizSlots + q * 8;
        const QuizSlot& qs = quizSlots[q];
        s.setU16(o, qs.id);
        for (size_t k = 0; k < 3; ++k) {
            s.setU8(o + 2 + k, qs.order[k]);
            s.setU8(o + 5 + k, qs.done[k]);
        }
    }
    s.setU8(off::kLocation, location);
    for (size_t i = 0; i < static_cast<size_t>(kRooms) * 3; ++i) {
        s.setU8(off::kSpotKind + i, spotKind[i]);
        s.setU8(off::kSpotContent + i, spotContent[i]);
    }
    s.setU16(off::kClimbsDone, climbsDone);
}

}  // namespace tmport::save
