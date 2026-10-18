#include "tools/autoplay/autoplay_checks.h"

#include <algorithm>
#include <array>
#include <format>
#include <span>

#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/room_table.h"
#include "game/rules.h"
#include "save/user_record.h"
#include "save/users_file.h"
#include "util/file_util.h"

namespace tmport::autoplay {

namespace {

constexpr int kChampionRank = 7;                 // last rank (thresholds DS:0ea0)
constexpr int kHudDigits = 99;                   // the HUD shows coins / nets modulo 100
constexpr int kHintMax = game::kHintEventCount - 1;
constexpr std::array<int, game::kLevelCount> kLevelFirst = {9, 23, 36}, kLevelLast = {22, 35, 48};

int countContent(const game::GameState& s, int level, uint8_t content) {
    int n = 0;
    for (int r = kLevelFirst[static_cast<size_t>(level)]; r <= kLevelLast[static_cast<size_t>(level)]; ++r)
        for (int k = 0; k < 3; ++k) n += s.rooms.spotContent(r, k) == content;
    return n;
}

std::string firstDiffs(std::span<const uint8_t> a, std::span<const uint8_t> b, int max = 6) {
    std::string out;
    int n = 0;
    const size_t len = std::min(a.size(), b.size());
    for (size_t i = 0; i < len && n < max; ++i)
        if (a[i] != b[i]) {
            out += std::format("{}+{:#05x}: {:02x} != {:02x}", n ? ", " : "", i, a[i], b[i]);
            ++n;
        }
    if (a.size() != b.size()) out += std::format("{}size {} != {}", n ? ", " : "", a.size(), b.size());
    return out;
}

}  // namespace

void Checker::fail(std::string what) {
    if (violations_.size() < 200) violations_.push_back(std::move(what));
}

void Checker::note(std::string what) {
    if (notes_.size() < 200) notes_.push_back(std::move(what));
}

void Checker::checkRanges(const game::GameState& s, std::string_view where) {
    ++checks_;
    const auto& inv = s.inv;
    if (inv.coins >= 0x8000) fail(std::format("{}: coins negative ({:#06x})", where, inv.coins));
    if (inv.nets >= 0x8000) fail(std::format("{}: nets negative ({:#06x})", where, inv.nets));
    if (inv.coins > kHudDigits && inv.coins < 0x8000 && !coinsNoted_) {
        coinsNoted_ = true;
        note(std::format("{}: coins {} exceed the HUD's two digits (shown modulo 100, as in the original)", where, inv.coins));
    }
    if (inv.nets > kHudDigits && inv.nets < 0x8000 && !netsNoted_) {
        netsNoted_ = true;
        note(std::format("{}: nets {} exceed the HUD's two digits (shown modulo 100, as in the original)", where, inv.nets));
    }
    if (inv.haveKey > 1) fail(std::format("{}: haveKey {}", where, inv.haveKey));
    if (inv.numClues > 3) fail(std::format("{}: clue words {}", where, inv.numClues));
    const uint8_t tier = s.rank.rankTier;
    if (tier > 3) fail(std::format("{}: rank tier {}", where, tier));
    const int need = t_.gemsNeeded[std::min<size_t>(tier, 3)];
    if (inv.gemsFound > need) fail(std::format("{}: gems found {} > {} per level", where, inv.gemsFound, need));
    if (s.shop.coinsLeft < 0 || s.shop.coinsLeft > 4) fail(std::format("{}: shop coins left {}", where, s.shop.coinsLeft));
    if (s.hints.pending < -1 || s.hints.pending > kHintMax) fail(std::format("{}: hint pending {}", where, s.hints.pending));
    if (s.quiz.tier > 4) fail(std::format("{}: quiz tier {}", where, s.quiz.tier));
    if (s.coinPile.room != 0xFF && s.coinPile.room != 0xFE && !game::RoomTable::valid(s.coinPile.room))
        fail(std::format("{}: coin pile room {}", where, s.coinPile.room));
    const int rank = game::rankFromTotal(s.rank.totalTreasures, t_);
    if (s.rank.rankTier != rank / 2)
        fail(std::format("{}: tier {} but total {} gives rank {} (tier {})", where, s.rank.rankTier, s.rank.totalTreasures,
                         rank, rank / 2));
}

void Checker::checkSpots(const game::GameState& s, std::string_view where) {
    const int level = s.currentLevel();
    if (level > 2 && level != 3) return;  // only rooms with a level
    const int need = t_.gemsNeeded[std::min<size_t>(climbTier_, 3)];
    int pointsExpected = 0;
    for (int l = 0; l < game::kLevelCount; ++l) {
        const int keys = countContent(s, l, game::kContentKey);
        const int treasures = countContent(s, l, game::kContentTreasure);
        if (l > level) {
            if (keys != 1) fail(std::format("{}: level {} holds {} keys (expected 1)", where, l, keys));
            if (treasures != need) fail(std::format("{}: level {} holds {} treasures (tier {}: {})", where, l, treasures, climbTier_, need));
        } else if (l == level) {
            if (keys + s.inv.haveKey != 1)
                fail(std::format("{}: level {}: {} keys hidden + haveKey {} != 1", where, l, keys, s.inv.haveKey));
            if (treasures + s.inv.gemsFound != need)
                fail(std::format("{}: level {}: {} treasures hidden + {} found != {}", where, l, treasures, s.inv.gemsFound, need));
            pointsExpected += s.inv.gemsFound;
        } else {
            if (keys != 0) fail(std::format("{}: level {} (passed) still hides {} keys", where, l, keys));
            pointsExpected += need - treasures;
        }
        // Spot contents only under pictures; gates and shops keep their markers.
        for (int r = kLevelFirst[static_cast<size_t>(l)]; r <= kLevelLast[static_cast<size_t>(l)]; ++r)
            for (int k = 0; k < 3; ++k) {
                const uint8_t kind = s.rooms.spotKind(r, k), content = s.rooms.spotContent(r, k);
                const bool marker = kind >= game::kSpotShop;
                if (marker && kind != content && kind != game::kSpotNone)
                    fail(std::format("{}: room {} slot {}: marker kind {:#x} with content {:#x}", where, r, k, kind, content));
                if (!marker && content > game::kContentKey)
                    fail(std::format("{}: room {} slot {}: picture {} with content {:#x}", where, r, k, kind, content));
                if (kind == game::kSpotNone && content != game::kSpotNone)
                    fail(std::format("{}: room {} slot {}: no spot but content {:#x}", where, r, k, content));
            }
    }
    if (level <= 2 && s.inv.climbPoints != pointsExpected)
        fail(std::format("{}: climb points {} but {} treasures were found this climb", where, s.inv.climbPoints, pointsExpected));
    for (size_t l = 0; l < game::kGateRooms.size(); ++l) {
        int gates = 0;
        for (int k = 0; k < 3; ++k) gates += s.rooms.spotContent(game::kGateRooms[l], k) == game::kSpotGate;
        if (gates != 2) fail(std::format("{}: gate room {} has {} gate thirds", where, game::kGateRooms[l], gates));
        int shops = 0;
        for (int k = 0; k < 3; ++k) shops += s.rooms.spotContent(game::kShopRooms[l], k) == game::kSpotShop;
        if (shops != 1) fail(std::format("{}: shop room {} has {} shop slots", where, game::kShopRooms[l], shops));
    }
}

void Checker::checkQuiz(const game::GameState& s, std::string_view where) {
    int dealt = 0;
    for (size_t q = 0; q < s.quiz.status.size(); ++q) {
        const uint8_t v = s.quiz.status[q];
        if ((v & 0x7F) > game::kQuizRightAfterMiss) {
            fail(std::format("{}: question {} history {:#04x}", where, q, v));
            return;
        }
        dealt += (v & game::kQuizDealtBit) != 0;
    }
    if (dealt > game::kQuizSlotCount) fail(std::format("{}: {} questions marked dealt (> 9)", where, dealt));
    for (int i = 0; i < game::kQuizSlotCount; ++i) {
        const game::QuizSlot& sl = s.quiz.slots[static_cast<size_t>(i)];
        if (sl.q == game::kQuizNoQuestion) continue;
        if (sl.q < 0 || sl.q >= game::kQuestionCount) {
            fail(std::format("{}: quiz slot {} question {}", where, i, sl.q));
            continue;
        }
        std::array<int, 3> seen{};
        bool ok = true;
        for (int k = 0; k < 3; ++k) {
            const uint8_t a = sl.perm[static_cast<size_t>(k)];
            if (a > 2) ok = false;
            else ++seen[a];
            if (sl.tried[static_cast<size_t>(k)] > 1) ok = false;
            if (a == 0 && sl.tried[static_cast<size_t>(k)] != 0) ok = false;
        }
        if (!ok || seen != std::array<int, 3>{1, 1, 1})
            fail(std::format("{}: quiz slot {} (question {}): order {} {} {} tried {} {} {}", where, i, sl.q, sl.perm[0], sl.perm[1],
                             sl.perm[2], sl.tried[0], sl.tried[1], sl.tried[2]));
        // Dealt this climb (bit 7), or missed this climb: Quiz_JudgeAnswer stores 2 over the whole
        // history byte after a wrong answer and the slot keeps the question (also its copy carried
        // to the next level, 1150:0375). Slots of passed levels are dead: the carried copy may have
        // been answered on the next level while the old slot keeps the question ("broken swap").
        const uint8_t st = s.quiz.status[static_cast<size_t>(sl.q)];
        if (i / 3 < static_cast<int>(s.currentLevel())) continue;  // (the castle: every level passed)
        if ((st & game::kQuizDealtBit) == 0 && st != game::kQuizMissed)
            fail(std::format("{}: quiz slot {} holds question {} with history {:#04x} (neither dealt nor missed)", where, i, sl.q, st));
    }
}

void Checker::checkRoomTable(const game::GameState& s) {
    for (int r = 0; r < game::kRoomCount; ++r) {
        const auto& d = s.rooms[r];
        if (d.level > 2) continue;
        for (uint8_t to : {d.exitLeft, d.exitRight, d.exitDoor}) {
            if (to == 0xFF) continue;
            if (!game::RoomTable::valid(to)) {
                fail(std::format("room table: room {} exit to invalid room {}", r, to));
                continue;
            }
            if (s.rooms[to].level != d.level)
                fail(std::format("room table: room {} (level {}) leads to room {} (level {})", r, d.level, to, s.rooms[to].level));
        }
    }
}

void Checker::onClimbSetup(const game::GameState& s, bool newClimb) {
    inClimb_ = true;
    climbTier_ = s.rank.rankTier;
    maxLevel_ = std::max(0, static_cast<int>(s.currentLevel() == 0xFF ? 0 : s.currentLevel()));
    if (newClimb) {
        // Fresh layout: every level holds its key and all of its treasures.
        for (int l = 0; l < game::kLevelCount; ++l) {
            const int keys = countContent(s, l, game::kContentKey);
            const int treasures = countContent(s, l, game::kContentTreasure);
            const int need = t_.gemsNeeded[std::min<size_t>(climbTier_, 3)];
            if (keys != 1) fail(std::format("climb set-up: level {} holds {} keys", l, keys));
            if (treasures != need) fail(std::format("climb set-up: level {} holds {} treasures (tier {}: {})", l, treasures, climbTier_, need));
        }
        if (s.inv.climbPoints != 0 || s.inv.gemsFound != 0 || s.inv.haveKey != 0)
            fail(std::format("climb set-up: new climb with points {} gems {} key {}", s.inv.climbPoints, s.inv.gemsFound, s.inv.haveKey));
        const int start = s.rank.climbsDone == 0 ? game::kMountainStartRoom : 0;
        if (s.player.room != start) fail(std::format("climb set-up: new climb starts in room {} (expected {})", s.player.room, start));
    }
    checkRoomTable(s);
    checkRanges(s, "climb set-up");
    checkQuiz(s, "climb set-up");
    checkSpots(s, "climb set-up");
}

void Checker::onRoomTick(const game::GameState& s) {
    if (!game::RoomTable::valid(s.player.room)) {
        fail(std::format("room tick: player in room {}", s.player.room));
        return;
    }
    const int level = s.currentLevel();
    if (inClimb_ && level <= 3) {
        if (level < maxLevel_)
            fail(std::format("room tick: level went down from {} to {} (room {})", maxLevel_, level, s.player.room));
        maxLevel_ = std::max(maxLevel_, level);
    }
    checkRanges(s, "room tick");
    checkQuiz(s, "room tick");
    if (inClimb_) checkSpots(s, std::format("room {}", s.player.room));
}

void Checker::onSave(const game::GameState& s, const std::filesystem::path& usersDat, std::string_view why) {
    ++saveChecks_;
    const std::string where = std::format("save ({})", why);
    auto bytes = readWholeFile(usersDat);
    if (!bytes) {
        if (s.player.signedIn()) fail(std::format("{}: users.dat missing after the save", where));
        return;
    }
    save::UsersTable table;
    if (!save::parseUsersFile(*bytes, table)) {
        fail(std::format("{}: users.dat is short ({} bytes)", where, bytes->size()));
        return;
    }
    const std::vector<uint8_t> again = table.fileBytes();
    if (again != *bytes) fail(std::format("{}: users.dat round trip differs: {}", where, firstDiffs(*bytes, again)));
    table.recount();
    for (int i = 0; i < table.count; ++i) {
        const save::UserSlot& slot = table.slots[static_cast<size_t>(i)];
        save::UserSlot copy = slot;
        save::PlayerRecord::fromSlot(slot).writeTo(copy);
        if (copy != slot) fail(std::format("{}: slot {} PlayerRecord round trip differs: {}", where, i, firstDiffs(slot.bytes, copy.bytes)));
        game::GameState g = game::GameState::initial(t_);
        game::fromRecord(g, std::span<const uint8_t, game::kRecordSize>(slot.bytes.data(), game::kRecordSize));
        const auto rec = game::toRecord(g);
        if (!std::equal(rec.begin(), rec.end(), slot.bytes.begin()))
            fail(std::format("{}: slot {} GameState round trip differs: {}", where, i, firstDiffs(slot.bytes, rec)));
        if (i > 0 && table.slots[static_cast<size_t>(i - 1)].totalTreasures() < slot.totalTreasures())
            fail(std::format("{}: roster not sorted by total at slot {}", where, i));
    }
    if (!s.player.signedIn()) return;
    const int idx = table.findByName(s.player.nameString());
    if (idx >= table.count) {
        fail(std::format("{}: player {} not in users.dat", where, s.player.nameString()));
        return;
    }
    const auto mine = game::toRecord(s);
    const auto& slot = table.slots[static_cast<size_t>(idx)].bytes;
    if (!std::equal(mine.begin(), mine.end(), slot.begin()))
        fail(std::format("{}: saved slot of {} differs from the game state: {}", where, s.player.nameString(), firstDiffs(slot, mine)));
}

void Checker::onClimbEnd(const game::GameState& s) {
    haveEnd_ = true;
    endClimbs_ = s.rank.climbsDone;
    endTotal_ = s.rank.totalTreasures;
    endPoints_ = s.inv.climbPoints;
    inClimb_ = false;
    checkRanges(s, "climb end");
}

void Checker::onCelebrated(const game::GameState& s, bool tracePromoted, bool traceChampion) {
    checkRanges(s, "celebrated");
    if (!haveEnd_) return;
    haveEnd_ = false;
    if (s.rank.climbsDone != static_cast<uint16_t>(endClimbs_ + 1))
        fail(std::format("celebration: climbs {} -> {}", endClimbs_, s.rank.climbsDone));
    uint64_t total = static_cast<uint64_t>(endTotal_) + endPoints_;
    if (total > 0xFFFFFFF0u) total = 0xFFFFFFF0u;
    if (s.rank.totalTreasures != total)
        fail(std::format("celebration: total {} + {} points = {} (expected {})", endTotal_, endPoints_, s.rank.totalTreasures, total));
    const int before = game::rankFromTotal(endTotal_, t_);
    const int after = game::rankFromTotal(s.rank.totalTreasures, t_);
    if (s.rank.rank != after) fail(std::format("celebration: rank {} (thresholds give {})", s.rank.rank, after));
    if (s.rank.rankTier != after / 2) fail(std::format("celebration: tier {} for rank {}", s.rank.rankTier, after));
    const bool promoted = s.rank.totalTreasures != 0 && after != before;
    if (promoted != tracePromoted) fail(std::format("celebration: promoted flag {} (rank {} -> {})", tracePromoted, before, after));
    const bool champion = s.rank.totalTreasures != 0 && after == kChampionRank;
    if (champion != traceChampion) fail(std::format("celebration: champion flag {} (rank {})", traceChampion, after));
}

void Checker::compareResume(const std::vector<uint8_t>& saved, const std::vector<uint8_t>& loaded, std::string_view how) {
    ++checks_;
    if (saved != loaded) fail(std::format("resume after {}: loaded record differs from the one left: {}", how, firstDiffs(saved, loaded)));
}

}  // namespace tmport::autoplay
