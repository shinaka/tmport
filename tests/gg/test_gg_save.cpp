// Tests of PLAYERS.DAT (src/gg/save): the typed layout, byte-exact round trips of every reference
// copy (the shipped file, the w0 and r2 .players copies of REFERENCE_CAPTURES.md §3.4, §9.5), the
// creation loop, and the original's write moments replayed on the captured before/after pairs.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "gg/save/players_dat.h"
#include "gg/save/players_store.h"
#include "gg_test_data.h"
#include "util/file_util.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    auto b = readWholeFile(p);
    REQUIRE(b);
    return std::move(*b);
}

// Number of differing bytes outside [from, from + n).
size_t diffOutside(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, size_t from, size_t n) {
    size_t d = 0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        if ((i < from || i >= from + n) && a[i] != b[i]) ++d;
    return d + (a.size() > b.size() ? a.size() - b.size() : b.size() - a.size());
}

}  // namespace

TEST_CASE("gg players: sizes and field offsets (fmt_data.md §8)") {
    CHECK(kRosterSize == 0x8A0);
    CHECK(kPlayersFileSize == 578487);
    CHECK(PlayersDat::recordOffset(98) == 572666);  // REFERENCE_CAPTURES.md §3.4
    CHECK(PlayersDat::recordOffset(1) == 0x1F5D);
    CHECK(PlayersDat::entryOffset(1) == 0x1E);

    PlayerRecord r;
    r.setName("Zed");
    r.points = 0x01020304;
    r.slot = 7;
    r.withChimps = 1;
    r.live.projectiles = 10;
    r.live.setupCoin = 0x1234;
    r.raceIndex = 14;
    r.racesWon = {1, 2, 3};
    r.hintSeen[18] = 0xBEEF;
    r.soundOff = 1;
    r.puzzleSolved[7][32] = 0xCAFE;
    r.blocks[2].live.paintColor = -1;
    r.blocks[1].parts[149] = {11, 0x8000};
    r.blocks[0].doorTypes[0] = 7;
    r.blocks[2].actors[11].item = 0x7777;
    r.racesWonTotal = 15;
    r.vehicleColor[14] = 5;
    const auto b = r.bytes();
    REQUIRE(b.size() == kPlayerRecordSize);
    const auto w = [&](size_t off) { return static_cast<uint16_t>(b[off] | (b[off + 1] << 8)); };
    CHECK(b[0] == 'Z');
    CHECK(b[0x10] == 4);
    CHECK(b[0x13] == 1);
    CHECK(w(0x14) == 7);
    CHECK(w(0x16) == 1);
    CHECK(w(0x1A + 2) == 10);          // DS:457e
    CHECK(w(0x1A + 0x12) == 0x1234);   // DS:458e
    CHECK(w(0x2E) == 14);              // DS:4590
    CHECK(w(0x34 + 4) == 3);           // DS:459a
    CHECK(w(0x42 + 36) == 0xBEEF);     // hint 0x1055
    CHECK(w(0x6A) == 1);               // DS:45cc
    CHECK(w(0x6E + 7 * 0x42 + 32 * 2) == 0xCAFE);
    const size_t block2 = 0x27E + 2 * kBuildingBlockSize, block1 = 0x27E + kBuildingBlockSize;
    CHECK(w(block2 + 0x0C) == 0xFFFF);                  // paint colour in the block's live copy
    CHECK(w(block1 + 0x14 + 149 * 4) == 11);            // part 149 holder
    CHECK(w(block1 + 0x14 + 149 * 4 + 2) == 0x8000);    // and flags
    CHECK(b[0x27E + 0x26C] == 7);
    CHECK(w(block2 + 0x492 + 11 * 0x2E + 0x2C) == 0x7777);  // actor 11 +0x40
    CHECK(w(0x16AC) == 15);
    CHECK(b[0x16AE + 14] == 5);
    CHECK(PlayerRecord::fromBytes(std::span<const uint8_t, kPlayerRecordSize>(b.data(), kPlayerRecordSize)) == r);

    const auto blank = PlayerRecord::blank().bytes();
    CHECK(std::count(blank.begin(), blank.end(), uint8_t{0xFF}) == 8);  // +0x30 and +0x34..+0x39
}

TEST_CASE("gg players: building block room item lists") {
    BuildingBlock blk;
    const std::vector<std::vector<SavedItem>> rooms = {{}, {{235, 1, 6}, {115, 2, 1000}}, {}, {{35, 0, 16}}};
    blk.writeRoomItems(rooms);
    CHECK(blk.readRoomItems(4) == rooms);
    CHECK(blk.roomItems[2] == 2);
    std::vector<std::vector<SavedItem>> tooMany(1, std::vector<SavedItem>(67));
    CHECK_THROWS(blk.writeRoomItems(tooMany));
}

TEST_CASE("gg players: the creation loop reproduces the shipped file (fmt_data.md §8.2)") {
    const auto created = PlayersDat::create();
    CHECK(created.roster.count == 0);
    CHECK(created.roster.entries[0].slot == 98);
    for (int e = 1; e < 99; ++e) CHECK(created.roster.entries[static_cast<size_t>(e)].slot == e);
    CHECK(created.records[0].slot == 0);
    CHECK(created.records[98].slot == 98);
    const auto bytes = created.bytes();
    CHECK(bytes.size() == kPlayersFileSize);

    std::optional<std::filesystem::path> shipped = testdata::ggCapture("original/PLAYERS.DAT");
    if (!shipped)
        if (const auto* files = testdata::ggFiles()) shipped = files->playersDatPath();
    if (!shipped) {
        MESSAGE("no shipped PLAYERS.DAT: comparison skipped");
        return;
    }
    const auto original = readFile(*shipped);
    // Equal except index entry 99 (uninitialised heap bytes in the original, zeros here).
    CHECK(diffOutside(bytes, original, PlayersDat::entryOffset(99), kRosterEntrySize) == 0);
}

TEST_CASE("gg players: every reference copy round-trips byte for byte") {
    std::vector<std::filesystem::path> files;
    if (const auto* gg = testdata::ggFiles())
        if (const auto p = gg->playersDatPath()) files.push_back(*p);
    if (const auto captures = testdata::ggCapturesPath()) {
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(*captures, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            const auto ext = toLowerAscii(pathToUtf8(it->path().extension()));
            const auto name = toLowerAscii(pathToUtf8(it->path().filename()));
            if (it->is_regular_file() && (ext == ".players" || name == "players.dat")) files.push_back(it->path());
        }
    }
    if (files.empty()) {
        MESSAGE("GG_DATA / GG_CAPTURES not set: test skipped");
        return;
    }
    int players = 0;
    for (const auto& f : files) {
        CAPTURE(pathToUtf8(f));
        const auto bytes = readFile(f);
        const auto parsed = PlayersDat::parse(bytes);
        REQUIRE(parsed);
        CHECK(parsed->versionOk());
        CHECK(parsed->bytes() == bytes);
        CHECK(isValidPlayersDat(bytes));
        players = std::max<int>(players, parsed->roster.count);
    }
    if (testdata::ggCapturesPath()) {
        CHECK(files.size() >= 148);  // 147 .players copies and original/PLAYERS.DAT (REFERENCE_CAPTURES.md)
        CHECK(players == 2);         // Ada and Bob
    }
}

TEST_CASE("gg players: captured players (REFERENCE_CAPTURES.md §3.4, §9.5)") {
    GG_REQUIRE_CAPTURES();
    const auto ada = testdata::ggCapture("savestates/w0/06_hub.players");
    const auto bob = testdata::ggCapture("savestates/r2/player2/06_hub_bob.players");
    REQUIRE(ada);
    REQUIRE(bob);
    const auto a = *PlayersDat::parse(readFile(*ada));
    CHECK(a.roster.count == 1);
    CHECK(a.roster.entries[0].slot == 98);  // the first new player gets slot 98
    CHECK(a.records[98].slot == 98);
    CHECK(a.records[98].withChimps == 1);
    CHECK(a.records[98].nameString() == a.roster.entries[0].nameString());
    CHECK(a.records[0] == PlayersDat::create().records[0]);  // record 0 stays blank
    const auto b = *PlayersDat::parse(readFile(*bob));
    CHECK(b.roster.count == 2);
    CHECK(b.roster.entries[1].slot == 1);  // Bob took slot 1 = the entry count
    CHECK(b.records[1].withChimps == 0);
    CHECK(b.records[1].building == -1);

    // The Aircraft block of the Save & Exit (r2/resume/04): 8 rooms, the items of rooms 4..7.
    const auto saved = *PlayersDat::parse(readFile(*testdata::ggCapture("savestates/r2/resume/04_after_save_exit.players")));
    const auto& rec = saved.records[1];
    CHECK(rec.buildingInProgress[1] == 1);
    const auto& blk = rec.blocks[1];
    const auto rooms = blk.readRoomItems(8);
    CHECK(rooms[1].empty());
    CHECK(rooms[4].size() == 4);
    CHECK(rooms[5].size() == 3);
    CHECK(rooms[6].size() == 5);
    CHECK(rooms[7].size() == 4);
    BuildingBlock copy = blk;
    copy.roomItems.fill(0);
    copy.writeRoomItems(rooms);
    CHECK(copy == blk);
}

TEST_CASE("gg players store: create, import, export, re-create") {
    const auto dir = testdata::scratchDir("gg_players_store");
    PlayersStore store(playersPathIn(dir));
    CHECK(store.load() == PlayersStatus::Created);
    CHECK(store.writes() == 1);
    CHECK(store.lastMoment() == GgSaveMoment::Create);
    CHECK(readFile(store.path()) == PlayersDat::create().bytes());

    PlayersStore again(playersPathIn(dir));
    CHECK(again.load() == PlayersStatus::Ok);
    CHECK(again.writes() == 0);

    // A file of the wrong version is re-created; the old one is kept as .bak.
    auto wrong = readFile(store.path());
    wrong[4] = 11;
    REQUIRE(writeWholeFile(store.path(), wrong));
    PlayersStore third(playersPathIn(dir));
    CHECK(third.load() == PlayersStatus::Recreated);
    CHECK(readFile(backupPathFor(store.path())) == wrong);
    CHECK(readFile(store.path()) == PlayersDat::create().bytes());

    // Import / export.
    const auto source = dir / "src" / "PLAYERS.DAT";
    REQUIRE(writeWholeFile(source, wrong));
    CHECK(importPlayers(source, dir / "imported.dat", false) == PlayersImport::SourceInvalid);
    CHECK(importPlayers(dir / "none.dat", dir / "imported.dat", false) == PlayersImport::SourceMissing);
    wrong[4] = 12;
    REQUIRE(writeWholeFile(source, wrong));
    CHECK(importPlayers(source, dir / "imported.dat", false) == PlayersImport::Imported);
    CHECK(importPlayers(source, dir / "imported.dat", false) == PlayersImport::DestExists);
    CHECK(exportPlayers(dir / "imported.dat", dir / "export" / "PLAYERS.DAT"));
    CHECK(readFile(dir / "export" / "PLAYERS.DAT") == wrong);

    // loadWithImport takes the game folder's file only when the engine has none.
    const auto fresh = dir / "fresh";
    PlayersStore imported(playersPathIn(fresh));
    PlayersImport how{};
    CHECK(imported.loadWithImport(source, &how) == PlayersStatus::Ok);
    CHECK(how == PlayersImport::Imported);
    CHECK(imported.file().bytes() == wrong);
    PlayersStore notAgain(playersPathIn(fresh));
    CHECK(notAgain.loadWithImport(source, &how) == PlayersStatus::Ok);
    CHECK(how == PlayersImport::DestExists);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("gg players store: the original's write moments on the captured before/after pairs") {
    GG_REQUIRE_CAPTURES();
    struct Pair {
        const char* before;
        const char* after;
        GgSaveMoment moment;
        int slot, rosterIndex;
        bool rosterSave;
    };
    const Pair pairs[] = {
        // New player "Ada" (w0): record to slot 98, index entry 0, then Roster_Save.
        {"savestates/w0/03_signin.players", "savestates/w0/06_hub.players", GgSaveMoment::NewPlayer, 98, 0,
         true},
        // File > Return to Technology Center > Yes (State 16): record and index entry only.
        {"savestates/w0/21_room_behind_door.players", "savestates/w0/25_hub_again.players",
         GgSaveMoment::SaveAndLeave, 98, 0, false},
        // New player "Bob" (r2): slot 1, index entry 1, then Roster_Save.
        {"savestates/r2/player2/04_name_typed.players", "savestates/r2/player2/06_hub_bob.players",
         GgSaveMoment::NewPlayer, 1, 1, true},
        // Save & Exit from the Aircraft building (r2): record with its building block, index entry 1.
        {"savestates/r2/resume/01_before_save_exit.players", "savestates/r2/resume/04_after_save_exit.players",
         GgSaveMoment::SaveAndExit, 1, 1, false},
    };
    const auto dir = testdata::scratchDir("gg_players_moments");
    int n = 0;
    for (const auto& p : pairs) {
        CAPTURE(p.after);
        const auto before = testdata::ggCapture(p.before);
        const auto after = testdata::ggCapture(p.after);
        REQUIRE(before);
        REQUIRE(after);
        const auto file = dir / std::to_string(n++) / std::string(kPlayersFileName);
        REQUIRE(importPlayers(*before, file, true) == PlayersImport::Imported);
        PlayersStore store(file);
        REQUIRE(store.load() == PlayersStatus::Ok);
        const auto target = *PlayersDat::parse(readFile(*after));
        const auto& rec = target.records[static_cast<size_t>(p.slot)];
        CHECK(store.saveCurrent(p.moment, rec, p.rosterIndex));
        if (p.rosterSave) CHECK(store.saveRoster(p.moment, target.roster));
        CHECK(readFile(file) == readFile(*after));
        CHECK(store.writes() == (p.rosterSave ? 2 : 1));
        // The sign-in's Esc path saves nothing.
        CHECK(store.saveCurrent(GgSaveMoment::SaveAndExit, PlayerRecord::blank(), 0, true));
        CHECK(readFile(file) == readFile(*after));
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
