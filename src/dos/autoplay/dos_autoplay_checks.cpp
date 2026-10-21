// Invariants of the DOS edition's game rules (dos_autoplay_checks.h).
#include "dos/autoplay/dos_autoplay_checks.h"

#include <algorithm>
#include <bit>
#include <format>

#include "dos/game/game.h"
#include "dos/game/signin.h"
#include "dos/save/users_store.h"
#include "mischief/util/file_util.h"

namespace tmport::dos::autoplay {

namespace {

// Hiding slots per level (g_hidingCells DS:11AA: 0..12 level 2, 13..28 level 1, 29..47 level 0) and
// the mirror slots that only receive copies (code_elves_layout_hints.md §5.1).
constexpr int kSlotFirst[3] = {29, 13, 0};
constexpr int kSlotEnd[3] = {48, 29, 13};
constexpr bool mirrorSlot(int i) { return i == 11 || i == 12 || i == 27 || i == 28; }

constexpr bool isTreasure(uint16_t c) {
    const uint16_t v = c & 0x7F;
    return v >= 6 && v <= 0x64 && (v & 1) == 0;
}
constexpr bool isKeyOf(uint16_t c, int level) { return (c & 0x7F) == 0x66 + 2 * level; }

// Source and mirror cells (Layout_MirrorSharedCells 1a54:06dc).
constexpr unsigned kMirrorPairs[3][2] = {{0xE3, 0xFD}, {0xAB, 0xC0}, {0x77, 0x86}};

uint16_t content(const DosGame& g, int slot) { return g.world.cellContent[g.tables.world.hidingCells[static_cast<size_t>(slot)]]; }
uint16_t picture(const DosGame& g, int slot) { return g.world.cellPicture[g.tables.world.hidingCells[static_cast<size_t>(slot)]]; }

std::string recordDiff(const UserRecord& a, const UserRecord& b) {
    std::array<uint8_t, kUserRecordSize> x{}, y{};
    a.toBytes(x);
    b.toBytes(y);
    std::string out;
    for (const auto& f : urec::kFields) {
        bool differ = false;
        for (size_t i = f.offset; i < f.offset + f.size; ++i) differ = differ || x[i] != y[i];
        if (!differ) continue;
        if (!out.empty()) out += ", ";
        if (f.size <= 2) {
            auto v = [&](const std::array<uint8_t, kUserRecordSize>& r) {
                return f.size == 1 ? r[f.offset] : static_cast<unsigned>(r[f.offset] | (r[f.offset + 1] << 8));
            };
            out += std::format("{} {:#x} -> {:#x}", f.name, v(x), v(y));
        } else {
            out += std::string(f.name);
        }
    }
    return out;
}

}  // namespace

TickSnapshot snapshotOf(const DosGame& g, const DosUsersStore& users) {
    TickSnapshot s;
    s.valid = true;
    s.coins = g.core.coins;
    s.nets = g.core.nets;
    s.playerState = g.player.playerState;
    s.level = g.core.level;
    s.scene = g.world.loadedScene;
    s.keyState = g.core.keyState;
    s.tier = g.core.tier;
    s.treasuresFound = g.core.treasuresFound;
    s.uiMode = g.core.uiMode;
    s.quizCoins = g.ui.quizCoins;
    s.quizClosed = g.ui.quizClosed;
    s.bonusPicture = g.world.cellPicture[0x26];
    s.shopOwed = g.core.shopCoinsOwed;
    s.creatureCell = g.world.creatureCell;
    s.creatureStrike = g.world.creatureStrike;
    s.area = g.core.area;
    s.total = g.hasRec() ? static_cast<uint16_t>(g.rec().totalTreasures) : 0;
    s.userRec = g.ui.userRec;
    s.saves = users.writes();
    if (g.hasRec())
        for (uint8_t w : g.rec().clueWords) s.revealed += (w & 0x80) ? 0 : 1;
    for (int i = 0; i < kElfCount; ++i) {
        s.elfState[static_cast<size_t>(i)] = g.elves.elves[static_cast<size_t>(i)].state;
        s.elfBase[static_cast<size_t>(i)] = g.elves.elves[static_cast<size_t>(i)].base;
    }
    return s;
}

void Checker::fail(std::string what) {
    if (violations_.size() < 200) violations_.push_back(std::move(what));
}
void Checker::note(std::string what) {
    if (notes_.size() < 100) notes_.push_back(std::move(what));
}
bool Checker::once(const std::string& key) {
    if (std::find(seen_.begin(), seen_.end(), key) != seen_.end()) return false;
    seen_.push_back(key);
    return true;
}

// ------------------------------------------------------------------------------ per tick

void Checker::checkRanges(const DosGame& g) {
    ++checks_;
    const auto& c = g.core;
    if (c.coins > 0x7FFF) fail(std::format("coins wrapped below zero ({:#x})", c.coins));
    if (c.nets > 0x7FFF) fail(std::format("nets wrapped below zero ({:#x})", c.nets));
    if (c.coins > 99 && once("coins>99")) note(std::format("coins above the HUD's two digits ({})", c.coins));
    if (c.nets > 99 && once("nets>99")) note(std::format("nets above the HUD's two digits ({})", c.nets));
    switch (c.keyState) {
        case 0: case 2: case 4: case 6: case 8: case 10: case 12: break;
        default: fail(std::format("key state {:#x}", c.keyState));
    }
    if (c.tier > 3) fail(std::format("tier {}", c.tier));
    if (c.level > 4) fail(std::format("level {}", c.level));
    if (g.player.playerState > 71) fail(std::format("player state {}", g.player.playerState));
    if (g.hasRec()) {
        for (size_t i = 0; i < 9; ++i) {
            const uint8_t w = g.rec().clueWords[i] & 0x7F;
            if (w >= dg::kClueWordTextCount) fail(std::format("clue word {} = {:#x}", i, g.rec().clueWords[i]));
        }
    }
    // Bridges break only from tier 2 (Cell_P45 1568:15bc), the castle creature only from tier 1
    // (Castle_CreatureSpawn 176a:159e: mask 0 at tier 0).
    if ((g.world.cellPicture[0x84] & 0x7F) == 0x30 || (g.world.cellPicture[0xB7] & 0x7F) == 0x30)
        if (c.tier < 2) fail(std::format("a bridge broke at tier {}", c.tier));
    if (g.world.creatureCell != 0 && c.tier < 1) fail("the castle creature appeared at tier 0");
    // The player stays inside the strip, and never stands in an empty cell on a level (Cell_P0_Void, a
    // bug path of the original: the player would fall through the world).
    const auto& p = g.player.obj;
    if (p.x >= 0x800 || p.y >= 0x800) fail(std::format("player outside the world at ({:#x}, {:#x})", p.x, p.y));
    else if (c.uiMode == 0 && c.level <= 2 && (p.y & 0x7F) == 0x70 && g.world.cellType[cellIndexOf(p.x, p.y) & 0xFF] == 0 &&
             once(std::format("void{:x}", cellIndexOf(p.x, p.y))))
        fail(std::format("the player stands in an empty cell {:02X} at ({:#x}, {:#x})", cellIndexOf(p.x, p.y), p.x, p.y));
    // The collected treasures (record +0x8A): treasure codes 6..0x64, or 0x80 for none, each once.
    if (g.hasRec()) {
        std::array<int, 128> n{};
        for (uint8_t v : g.rec().collected) {
            if (v == 0x80) continue;
            if (v < 6 || v > 0x64 || (v & 1)) {
                if (once(std::format("collected{:x}", v))) fail(std::format("collected treasure {:#x}", v));
            } else if (++n[v] == 2 && once(std::format("dup{:x}", v))) {
                fail(std::format("treasure {:#x} collected twice", v));
            }
        }
    }
}

void Checker::checkSpots(const DosGame& g, bool setup) {
    if (!g.hasRec()) return;
    ++checks_;
    const auto& rec = g.rec();
    const bool inClimb = g.core.level <= 2 && g.player.playerState < 41 && g.core.endState == 0;
    int foundBits = 0;
    for (int level = 0; level < 3; ++level) {
        int keys = 0, keySearched = 0, treasures = 0, treasuresSearched = 0, empty = 0;
        for (int i = kSlotFirst[level]; i < kSlotEnd[level]; ++i) {
            if (mirrorSlot(i)) continue;
            const uint16_t c = content(g, i);
            if (isKeyOf(c, level)) {
                ++keys;
                if (c & 0x80) ++keySearched;
            } else if (isTreasure(c)) {
                ++treasures;
                if (c & 0x80) ++treasuresSearched;
            }
            if (picture(g, i) == 0) ++empty;
        }
        const int bits = std::popcount(static_cast<unsigned>(rec.foundMasks[static_cast<size_t>(level)]));
        foundBits += bits;
        if (keys != 1) fail(std::format("level {}: {} keys in the hiding places", level, keys));
        if (treasures != g.core.tier + 2 && (setup || inClimb))
            fail(std::format("level {}: {} treasures hidden at tier {}", level, treasures, g.core.tier));
        if (empty != 0) fail(std::format("level {}: {} hiding places without a picture", level, empty));
        if (treasuresSearched != bits)
            fail(std::format("level {}: {} treasures searched, found mask {:#x}", level, treasuresSearched,
                             rec.foundMasks[static_cast<size_t>(level)]));
        if (setup && g.core.keyState == 8 && (keySearched || treasuresSearched))
            fail(std::format("level {}: a new climb with searched places", level));
        // g_keyState: 2/4/6 = the key of level 0/1/2 found, 8/10/12 = still hidden (ARCHITECTURE §5.6).
        if (inClimb) {
            if (g.core.keyState == 2 * (level + 1) && !keySearched)
                fail(std::format("key state {} but the level-{} key was not searched", g.core.keyState, level));
            if (g.core.keyState == 8 + 2 * level && keySearched)
                fail(std::format("key state {} but the level-{} key was searched", g.core.keyState, level));
        }
    }
    if (foundBits != g.core.treasuresFound)
        fail(std::format("treasures found {} but {} bits in the found masks", g.core.treasuresFound, foundBits));
    // The mirror cells hold copies of their sources (not while a search toggles one, nor after
    // Layout_UnmarkSharedCells of the end sequence).
    if (inClimb && g.player.searchTimer == 0) {
        for (const auto& p : kMirrorPairs)
            for (unsigned k = 0; k < 3; ++k) {
                const unsigned a = p[0] + k, b = p[1] + k;
                if (g.world.cellPicture[a] != g.world.cellPicture[b] || g.world.cellContent[a] != g.world.cellContent[b])
                    fail(std::format("mirror cells {:02X}/{:02X} differ: picture {:#x}/{:#x} content {:#x}/{:#x}", a, b,
                                     g.world.cellPicture[a], g.world.cellPicture[b], g.world.cellContent[a],
                                     g.world.cellContent[b]));
            }
    }
}

void Checker::onTick(const DosGame& g, const TickSnapshot& b, const TickSnapshot& a) {
    checkRanges(g);
    if (!b.valid) return;
    // A game is being (re)loaded or replaced in this tick: the sign-in, a restore, or a menu command that
    // writes USERS.DAT and reloads the player (Start Over: Users_LoadPlayer's banked coins and nets,
    // at the end of its Button_Flash, when the dialog bit is already clear).
    const bool reload = (b.uiMode & 0x68) != 0 || (a.uiMode & 0x08) != 0 || b.userRec != a.userRec || a.saves != b.saves;
    if (!reload) {
        int drops = 0, dust = 0, catches = 0, creatures = 0;
        if ((a.playerState == 27 || a.playerState == 28) && b.playerState != 27 && b.playerState != 28) ++drops;
        for (size_t i = 0; i < 15; ++i) {
            const uint16_t s0 = b.elfState[i], s1 = a.elfState[i];
            if ((s1 == 54 || s1 == 58) && s0 != 54 && s0 != 58) ++dust;
            if ((s1 == 67 || s1 == 69) && s0 != 67 && s0 != 69) ++catches;
            if (s0 == 72 && s1 == a.elfBase[i] && a.elfBase[i] == 25) ++creatures;
        }
        const int bonus = (b.bonusPicture & 0x7F) == 0x4B && (a.bonusPicture & 0x7F) == 0x49 ? 5 : 0;
        const bool quizClosed = (b.uiMode & 0x02) && !(a.uiMode & 0x02) && b.quizClosed == 0;
        if (b.quizClosed != 0 && a.quizClosed == 0) quizOpenCoins_ = a.coins;
        const int delta = static_cast<int>(a.coins) - static_cast<int>(b.coins);
        if (quizClosed) {
            const int reward = static_cast<int>(a.coins) - static_cast<int>(quizOpenCoins_);
            if (a.coins != b.quizCoins || reward < 0 || reward > 2)
                fail(std::format("question closed: coins {} -> {} (screen {}, reward {})", b.coins, a.coins, b.quizCoins, reward));
        } else {
            const int expected = -drops - dust + catches + 2 * creatures + bonus;
            // The castle creature's strike (anim op -6, g_creatureStrike DS:9264) costs a coin when the next
            // castle cell's object call checks it (Castle_CreatureHitCheck 1568:18f1): in the tick of the
            // strike, or later when the strike waited (the player was in the hall door cell 73 or out of
            // the castle cells), even after the creature has gone.
            const bool creatureHit = delta == expected - 1 && b.coins > 0 && b.area == 0x0D &&
                                     (b.creatureCell != 0 || a.creatureCell != 0 || b.creatureStrike != 0);
            // A drop, dust or creature hit with no coin left takes nothing.
            const bool broke = b.coins == 0 && delta >= expected && delta <= expected + drops + dust;
            if (delta != expected && !creatureHit && !broke) {
                std::string elves;
                for (size_t i = 0; i < 15; ++i)
                    if (b.elfState[i] != a.elfState[i])
                        elves += std::format(" elf {} ({}) {}->{}", i, a.elfBase[i], b.elfState[i], a.elfState[i]);
                fail(std::format("coins {} -> {} unexplained (drops {}, dust {}, catches {}, coin creatures {}, bonus {}, "
                                 "player state {} -> {} at ({:#x}, {:#x}) level {} area {:#x} creature {}/{} strike {};{})",
                                 b.coins, a.coins, drops, dust, catches, creatures, bonus, b.playerState, a.playerState,
                                 g.player.obj.x, g.player.obj.y, g.core.level, g.core.area, b.creatureCell, a.creatureCell,
                                 b.creatureStrike, elves));
            }
        }
        const int netDelta = static_cast<int>(a.nets) - static_cast<int>(b.nets);
        const int netExpected = (catches > 0 && b.nets > 0 ? -1 : 0) + (b.shopOwed == 0 && a.shopOwed == -1 ? 5 : 0);
        if (netDelta != netExpected)
            fail(std::format("nets {} -> {} unexplained (catches {}, shop owed {} -> {})", b.nets, a.nets, catches, b.shopOwed,
                             a.shopOwed));
        if (dust > 0 && a.tier < 1) fail("elf dust at tier 0");
        // Clue words are revealed by a correct answer (Quiz_Correct -> Clue_Reveal 46ce:06f0), one at a
        // time, and only Game_NewSetup hides them again (a new game comes with a USERS.DAT write in the
        // same frame: Game_End, Start Over, a new player).
        if (a.revealed < b.revealed && a.userRec == b.userRec && a.saves == b.saves)
            fail(std::format("clue words hidden again during a game ({} -> {})", b.revealed, a.revealed));
        if (a.revealed > b.revealed + 1) fail(std::format("{} clue words revealed in one tick", a.revealed - b.revealed));
    }
    // A scene of the mountain was loaded: the spawned elves by tier (Elves_Spawn 4521:182e) and the
    // clue carriers by the level's hidden clue words.
    if (a.scene != b.scene && a.scene <= 2 && g.core.uiMode == 0 && g.hasRec()) {
        ++checks_;
        const auto& counts = g.tables.elves.elfCounts[std::min<size_t>(g.core.tier, 3)];
        int n[4] = {0, 0, 0, 0}, carriers = 0;
        for (const Elf& e : g.elves.elves) {
            if (e.base == 1 || e.base == 5) ++n[0];
            else if (e.base == 9 || e.base == 13) ++n[1];
            else if (e.base == 17 || e.base == 21) ++n[2];
            else if (e.base == 25) ++n[3];
            if (e.question != 0) ++carriers;
        }
        int hidden = 0;
        for (int k = 0; k < 3; ++k) hidden += (g.rec().clueWords[static_cast<size_t>(3 * a.scene + k)] & 0x80) ? 1 : 0;
        for (int k = 0; k < 4; ++k)
            if (n[k] != counts[static_cast<size_t>(k)])
                fail(std::format("scene {}: {} elves of kind {} at tier {} (expected {})", a.scene, n[k], "ABCD"[k], g.core.tier,
                                 counts[static_cast<size_t>(k)]));
        if (carriers != std::min(hidden, static_cast<int>(counts[0])))
            fail(std::format("scene {}: {} clue carriers for {} hidden clue words", a.scene, carriers, hidden));
    }
    if (a.treasuresFound != b.treasuresFound || a.keyState != b.keyState || a.scene != b.scene) checkSpots(g, false);
}

// ---------------------------------------------------------------------- climb set-up and end

void Checker::onClimbSetup(const DosGame& g, bool newClimb, uint16_t previousTier) {
    checkSpots(g, newClimb);
    if (!newClimb || !g.hasRec()) return;
    ++checks_;
    // Layout_PlaceTreasures 1a54:029a: the tier follows the total from 25 on.
    const int total = g.rec().totalTreasures;
    const int expected = total >= 230 ? 3 : total >= 115 ? 2 : total >= 25 ? 1 : previousTier;
    if (g.core.tier != expected) fail(std::format("new climb at total {}: tier {} (expected {})", total, g.core.tier, expected));
    if (g.world.castleLayout > g.core.tier) fail(std::format("castle layout {} at tier {}", g.world.castleLayout, g.core.tier));
    if (g.core.treasuresFound != 0 || g.core.keyState != 8) fail("a new climb with treasures found or a key");
}

void Checker::onCastleTop(const DosGame& g, uint32_t totalBefore, uint16_t found) {
    if (!g.hasRec()) return;
    ++checks_;
    const uint16_t total = static_cast<uint16_t>(g.rec().totalTreasures);
    if (total != static_cast<uint16_t>(totalBefore + found))
        fail(std::format("castle top: total {} + {} found gave {}", totalBefore, found, total));
}

// ------------------------------------------------------------------------------------- saves

UserRecord Checker::packedRecord(DosGame& g) {
    if (!g.hasRec()) return {};
    const UserRecord keep = g.rec();
    const uint16_t keepHigh = g.users.users().spriteHighWater;
    usersPack(g);
    UserRecord packed = g.rec();
    g.rec() = keep;
    g.users.users().spriteHighWater = keepHigh;
    return packed;
}

void Checker::onSave(DosGame& g, const DosUsersStore& store, std::string_view moment, uint16_t gamesBefore) {
    ++saveChecks_;
    const auto file = mischief::readWholeFile(store.path());
    const std::vector<uint8_t> image = store.users().bytes();
    if (!file) {
        fail(std::format("save {}: {} cannot be read", moment, mischief::pathToUtf8(store.path())));
        return;
    }
    // After Users_WriteFile the frame may go on with Game_Start (a new player, Start Over, the end of a
    // game), which rewrites fields of the selected record in place (question history, slots, answer
    // masks, clue words, found masks, question tier: Game_NewSetup, Layout_Generate,
    // Quiz_LoadQuestions); everything else must be what the file holds.
    if (file->size() != image.size()) {
        fail(std::format("save {}: the file has {} bytes", moment, file->size()));
    } else {
        const bool restarts = moment == describe(DosSaveMoment::NewPlayer) || moment == describe(DosSaveMoment::StartOver) ||
                              moment == describe(DosSaveMoment::GameEnd);
        const size_t rec = g.hasRec() ? UsersDat::recordOffset(g.ui.userRec) : 0;
        auto inPlace = [&](size_t i) {
            if (!restarts || !g.hasRec() || i < rec || i >= rec + kUserRecordSize) return false;
            const size_t o = i - rec;
            auto in = [&](size_t off, size_t n) { return o >= off && o < off + n; };
            return in(urec::kQuizHistory, 90) || in(urec::kQuizSlots, 18) || in(urec::kAnswerMasks, 9) || in(urec::kClueWords, 9) ||
                   in(urec::kFoundMasks, 3) || in(urec::kQuizTier, 1);
        };
        for (size_t i = 0; i < image.size(); ++i)
            if ((*file)[i] != image[i] && !inPlace(i)) {
                fail(std::format("save {}: the file differs from the store's image at byte {:#x}", moment, i));
                break;
            }
    }
    const auto parsed = UsersDat::parse(*file);
    if (!parsed || parsed->bytes() != *file) fail(std::format("save {}: the file does not round-trip through the parser", moment));
    if (!g.hasRec()) return;
    const UserRecord& rec = g.rec();
    if (moment == describe(DosSaveMoment::ExitAndSave) || moment == describe(DosSaveMoment::StartNewPlayer)) {
        // Nothing changes the game between Users_Pack and the end of the frame: the record is the
        // packed game state.
        const UserRecord packed = packedRecord(g);
        if (!(packed == rec)) fail(std::format("save {}: the record differs from the game state: {}", moment, recordDiff(rec, packed)));
    }
    if (moment == describe(DosSaveMoment::GameEnd)) {
        // Game_End 47c7:09a2: games played + 1, no saved game, the coins and nets banked.
        if (rec.gamesPlayed != static_cast<uint16_t>(gamesBefore + 1))
            fail(std::format("game end: games played {} -> {}", gamesBefore, rec.gamesPlayed));
        if (rec.savedGame != 0) fail("game end: the record keeps a saved game");
        if (rec.bankedCoins != g.core.coins || rec.bankedNets != g.core.nets)
            fail(std::format("game end: banked {} coins / {} nets, the game has {} / {}", rec.bankedCoins, rec.bankedNets,
                             g.core.coins, g.core.nets));
    }
}

void Checker::compareLoaded(const UserRecord& loaded, const UserRecord& saved, std::string_view how) {
    ++saveChecks_;
    if (!(loaded == saved)) fail(std::format("load ({}): the record read at the sign-in differs from the saved one: {}", how, recordDiff(saved, loaded)));
}

void Checker::compareResume(DosGame& g, const UserRecord& saved, std::string_view how) {
    ++saveChecks_;
    if (!g.hasRec()) {
        fail(std::format("resume ({}): no player record selected", how));
        return;
    }
    UserRecord packed = packedRecord(g);
    // The level's scene load reorders the question slots in place (Quiz_ReorderSlots 46ce:0df2: missed,
    // unasked, then the answered ones from the back), which a resumed game runs again: the slots must
    // hold the same (question, answer mask) pairs, in any order.
    std::vector<std::pair<uint16_t, uint8_t>> a, b;
    for (size_t i = 0; i < kQuizSlotCount; ++i) {
        a.emplace_back(saved.quizSlots[i], saved.answerMasks[i]);
        b.emplace_back(packed.quizSlots[i], packed.answerMasks[i]);
    }
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    if (a != b) fail(std::format("resume ({}): the question slots differ from the saved record's", how));
    packed.quizSlots = saved.quizSlots;
    packed.answerMasks = saved.answerMasks;
    // Game_Start runs Cells_AdjustForCpu after Game_Restore (47c7:08b5), with the castle's fast-CPU flag
    // when the game was saved in the castle (Game_Restore: g_area 0x0D -> g_slowCpu = 0): a slow-CPU game
    // saved there comes back with the fast CPU's bridge pictures (84, B7: 0x2E -> 0x2F), as on the
    // original.
    for (size_t j : {size_t{2}, size_t{5}}) {
        const uint8_t want = g.core.slowCpu == 0 && saved.variablePictures[j] == 0x2E ? 0x2F
                             : g.core.slowCpu != 0                                   ? 0x2E
                                                                                      : saved.variablePictures[j];
        if (packed.variablePictures[j] == want && want != saved.variablePictures[j]) {
            note(std::format("resume ({}): bridge picture {:#x} -> {:#x} (Cells_AdjustForCpu with g_slowCpu {})", how,
                             saved.variablePictures[j], want, g.core.slowCpu));
            packed.variablePictures[j] = saved.variablePictures[j];
        }
    }
    if (!(packed == saved)) fail(std::format("resume ({}): the loaded game differs from the saved record: {}", how, recordDiff(saved, packed)));
}

}  // namespace tmport::dos::autoplay
