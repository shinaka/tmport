#include "game/game_tables.h"

#include <algorithm>
#include <format>

#include "data/dgroup_offsets.h"
#include "mischief/util/error.h"
#include "res/resource_manager.h"

namespace tmport::game {

namespace {

std::string_view asView(std::span<const uint8_t> s) {
    return std::string_view(reinterpret_cast<const char*>(s.data()), s.size());
}

template <size_t N>
std::array<std::string_view, N> stringTable(const ExeTables& exe, uint16_t off) {
    std::array<std::string_view, N> out{};
    for (size_t i = 0; i < N; ++i) out[i] = asView(exe.farCstr(off + static_cast<uint32_t>(4 * i)));
    return out;
}

}  // namespace

std::vector<LayoutRecord> parseLayoutTable(std::span<const uint8_t> data) {
    std::vector<LayoutRecord> out;
    if (data.size() < 2) return out;
    // 1100:04b5 reads the count as a signed word.
    const int declared = static_cast<int16_t>(data[0] | (data[1] << 8));
    const size_t available = (data.size() - 2) / kLayoutRecordSize;
    const size_t n = declared > 0 ? std::min(static_cast<size_t>(declared), available) : 0;
    out.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* r = data.data() + 2 + i * kLayoutRecordSize;
        LayoutRecord& rec = out[i];
        rec.mask = r[0];
        rec.keyKind = r[1];
        for (size_t k = 0; k < kLayoutSpots; ++k) {
            rec.kind[k] = r[2 + k];
            rec.content[k] = r[0x14 + k];
        }
    }
    return out;
}

std::vector<LayoutRecord> loadLayoutTable(int id, std::span<const uint8_t> data) {
    auto records = parseLayoutTable(data);
    if (records.size() < static_cast<size_t>(kMinLayoutRecords))
        throw mischief::FormatError(std::format("OTHR {} (hiding-place layouts) holds {} records; a climb needs at least {}", id,
                                      records.size(), kMinLayoutRecords));
    return records;
}

std::vector<ObjectAttributes> parseObjectTable(std::span<const uint8_t> data) {
    const size_t n = std::min(data.size() / 4, static_cast<size_t>(kObjectTableEntries));
    std::vector<ObjectAttributes> out(n);
    for (size_t v = 0; v < n; ++v)
        out[v] = ObjectAttributes{data[4 * v], data[4 * v + 1], data[4 * v + 2], data[4 * v + 3]};
    return out;
}

ObjectAttributes GameTables::object(uint8_t kind) const {
    return kind < objects.size() ? objects[kind] : ObjectAttributes{};
}

GameTables GameTables::fromExe(const ExeTables& exe) {
    GameTables t;
    t.rooms = exe.rooms();
    t.rankThresholds = exe.array<uint32_t, kRankCount>(dg::kRankThresholds);
    t.gemsNeeded = exe.array<uint8_t, kTierCount>(dg::kGemsNeeded);
    t.elfCountByTier = exe.array<uint8_t, kTierCount>(dg::kElfCountByTier);
    t.creatureSpawnPermille = exe.array<int16_t, kTierCount>(dg::kClimbCreatSpawnPermille);
    t.hintBit = exe.array<int16_t, kHintEventCount>(dg::kHintBit);
    t.room9RankHints = exe.array<int16_t, kRankCount>(dg::kRoom9RankHints);
    t.rankPromoHints = exe.array<int16_t, kRankCount>(dg::kRankPromoHints);
    t.rankStayHints = exe.array<int16_t, kRankCount>(dg::kRankStayHints);
    t.netSignX = exe.array<int16_t, kLevelCount>(dg::kNetSignX);
    t.shopWalkX = exe.array<int16_t, kLevelCount>(dg::kShopWalkX);

    t.words.count = stringTable<8>(exe, dg::kStrCount);
    t.words.size = stringTable<5>(exe, dg::kStrSize);
    t.words.shape = stringTable<5>(exe, dg::kStrShape);
    t.words.noun = stringTable<16>(exe, dg::kStrNoun);
    t.words.nounPlural = stringTable<16>(exe, dg::kStrNounPlural);

    // DS:1990 has 360 words in the image: the 360th is read by nothing but bounds the last
    // question's ids for the narration cross-check.
    t.quiz.voiceBase = exe.array<uint16_t, kQuestionCount + 1>(dg::kQuizVoiceBase);
    t.quiz.voiceBank = exe.array<uint8_t, kQuestionCount>(dg::kQuizVoiceBank);
    t.quiz.silentAnswers = exe.array<int16_t, 16>(dg::kQuizSilentAnswers);
    t.quiz.tierStart = exe.array<int16_t, kQuizTierCount>(dg::kQuizTierStart);
    return t;
}

GameTables GameTables::load(const ExeTables& exe, const ResourceManager& res) {
    GameTables t = fromExe(exe);
    for (int tier = 0; tier < kTierCount; ++tier) {
        const int id = kLayoutFirstId + tier;
        auto raw = res.loadRaw(id);
        if (!raw) throw mischief::FormatError(std::format("OTHR {} (hiding-place layouts) not found in TMT.DLL", id));
        t.layouts[static_cast<size_t>(tier)] = loadLayoutTable(id, *raw);
    }
    auto obj = res.loadRaw(kObjectTableId);
    if (!obj) throw mischief::FormatError(std::format("OTHR {} (hiding-place pictures) not found in TMT.DLL", kObjectTableId));
    t.objects = parseObjectTable(*obj);
    return t;
}

}  // namespace tmport::game
