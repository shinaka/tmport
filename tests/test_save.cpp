// users.dat (save/users_file.h, save/user_record.h). Synthetic records always; the round trip
// on a real file written by the original when TM_USERS_DAT points at one (never written to:
// the tests work on copies in a temporary directory).
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <string>

#include "save/user_record.h"
#include "save/users_file.h"
#include "util/file_util.h"

using namespace tmport;
using namespace tmport::save;

namespace {

namespace fs = std::filesystem;

fs::path tempDir(const char* name) {
    const fs::path d = fs::temp_directory_path() / "tmport_tests" / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

UserSlot makeSlot(const std::string& name, uint32_t total, uint8_t fill = 0) {
    UserSlot s;
    s.bytes.fill(fill);
    PlayerRecord r;
    r.name = name;
    r.totalTreasures = total;
    r.writeTo(s);
    return s;
}

PlayerRecord player(const std::string& name, uint32_t total) {
    PlayerRecord r;
    r.name = name;
    r.totalTreasures = total;
    return r;
}

std::vector<std::string> names(const UsersTable& t) {
    std::vector<std::string> v;
    for (int i = 0; i < t.count; ++i) v.push_back(t.slots[static_cast<size_t>(i)].name());
    return v;
}

}  // namespace

TEST_CASE("users.dat: layout constants") {
    CHECK(kSlotSize == 1583);
    CHECK(kTableSize == 0x2B49);
    CHECK(kFileSize == 11085);
    CHECK(off::kClimbsDone + 2 == kSlotSize);
    CHECK(off::kSpotContent + 150 == off::kUnused2);
    CHECK(off::kQuizSlots + 72 == off::kLocation);
    CHECK(off::kQuizHistory + kQuizHistorySize == off::kQuizSlots);
}

TEST_CASE("users.dat: record field copy is exact and keeps untouched bytes") {
    UserSlot s;
    for (size_t i = 0; i < kSlotSize; ++i) s.bytes[i] = static_cast<uint8_t>(i * 7 + 3);
    s.bytes[5] = 0;  // name "\x03\x0a\x11\x18\x1f" then NUL, bytes 6..8 are leftovers
    const UserSlot before = s;
    PlayerRecord r = PlayerRecord::fromSlot(s);
    CHECK(r.name.size() == 5);
    UserSlot copy = s;
    r.writeTo(copy);
    CHECK(copy == before);  // load -> save of the same values is byte-identical

    // Changing fields changes only those bytes; the unused areas and the name tail stay.
    r.name = "Al";
    r.coins = 0x1234;
    r.totalTreasures = 0xA1B2C3D4;
    r.spotContent[149] = 1;  // value 1 is kept as is
    r.writeTo(copy);
    CHECK(copy.name() == "Al");
    CHECK(copy.bytes[2] == 0);
    CHECK(copy.bytes[3] == before.bytes[3]);  // strcpy leaves the bytes after the NUL
    CHECK(copy.u16(off::kCoins) == 0x1234);
    CHECK(copy.totalTreasures() == 0xA1B2C3D4);
    CHECK(copy.bytes[off::kSpotContent + 149] == 1);
    for (size_t i = off::kUnused1; i < off::kUnused1 + 800; ++i) CHECK(copy.bytes[i] == before.bytes[i]);
    CHECK(copy.bytes[off::kUnused2] == before.bytes[off::kUnused2]);
    // Names are at most 8 bytes.
    r.name = "ABCDEFGHIJ";
    r.writeTo(copy);
    CHECK(copy.name() == "ABCDEFGH");
    CHECK(copy.bytes[8] == 0);
}

TEST_CASE("users.dat: load rules (header ignored, short file empty, extra bytes ignored)") {
    const fs::path dir = tempDir("load");
    UsersTable t;
    t.slots[0] = makeSlot("Ann", 3);
    t.slots[1] = makeSlot("Bob", 1);
    t.recount();
    CHECK(t.count == 2);
    auto bytes = t.fileBytes();
    CHECK(bytes.size() == kFileSize);
    CHECK(bytes[0] == 'U');
    CHECK(bytes[3] == 0x1A);
    bytes[0] = 'X';  // never checked
    bytes.push_back(0x55);  // ignored
    UsersTable r;
    CHECK(parseUsersFile(bytes, r));
    CHECK(names(r) == std::vector<std::string>{"Ann", "Bob"});
    bytes.resize(kFileSize - 1);
    CHECK_FALSE(parseUsersFile(bytes, r));
    CHECK(r.count == 0);
    CHECK(r.slots[0].bytes[0] == 0);
    CHECK_FALSE(readUsersFile(dir / "missing.dat", r));
    CHECK(r.count == 0);
    // The count stops at the first empty name.
    t.slots[3] = makeSlot("Dan", 9);
    t.recount();
    CHECK(t.count == 2);
}

TEST_CASE("users.dat: save merges into the re-read file, stable sort, re-find by name") {
    const fs::path dir = tempDir("merge");
    const fs::path file = dir / "TLCWIN" / "users.dat";  // directories are created on save
    UsersTable disk;
    disk.slots[0] = makeSlot("Ann", 25);
    disk.slots[1] = makeSlot("Bob", 5);
    disk.slots[2] = makeSlot("Cid", 5);
    disk.slots[2].bytes[off::kUnused1 + 10] = 0xEE;  // unused bytes from the file survive
    disk.slots[1].bytes[7] = 0x77;                   // name tail
    disk.recount();
    REQUIRE(writeUsersFile(file, disk));

    // Bob finishes a climb with 30: moves to the top.
    int sel = 1;
    UsersTable out;
    auto res = saveCurrentPlayer(file, player("Bob", 30), sel, &out);
    CHECK(res.attempted);
    CHECK(res.written);
    CHECK(names(out) == std::vector<std::string>{"Bob", "Ann", "Cid"});
    CHECK(sel == 0);
    CHECK(out.slots[0].bytes[7] == 0x77);
    UsersTable reread;
    REQUIRE(readUsersFile(file, reread));
    CHECK(reread == out);
    CHECK(reread.slots[2].bytes[off::kUnused1 + 10] == 0xEE);

    // Equal totals keep their order (stable).
    sel = 2;
    saveCurrentPlayer(file, player("Cid", 25), sel, &out);
    CHECK(names(out) == std::vector<std::string>{"Bob", "Ann", "Cid"});
    CHECK(sel == 2);

    // A new player is appended at index == count, then sorted in.
    sel = 3;
    saveCurrentPlayer(file, player("Dee", 26), sel, &out);
    CHECK(names(out) == std::vector<std::string>{"Bob", "Dee", "Ann", "Cid"});
    CHECK(out.count == 4);
    CHECK(sel == 1);

    // Merge: other slots come from disk, not from memory. Someone else changed the file.
    UsersTable other;
    readUsersFile(file, other);
    other.slots[3] = makeSlot("Zed", 100);
    writeUsersFile(file, other);
    sel = 2;  // Ann, as the game remembers her
    saveCurrentPlayer(file, player("Ann", 27), sel, &out);
    CHECK(names(out) == std::vector<std::string>{"Zed", "Bob", "Ann", "Dee"});
    CHECK(sel == 2);

    // Sort keys are unsigned 32-bit.
    sel = 3;
    saveCurrentPlayer(file, player("Dee", 0xFFFFFFF0u), sel, &out);
    CHECK(out.slots[0].name() == "Dee");
    CHECK(sel == 0);

    // Nobody signed in: nothing is written.
    const auto beforeBytes = readWholeFile(file);
    sel = 0;
    res = saveCurrentPlayer(file, player("________", 1), sel);
    CHECK_FALSE(res.attempted);
    CHECK(readWholeFile(file) == beforeBytes);
}

TEST_CASE("users.dat: save with a single player and with a missing file") {
    const fs::path dir = tempDir("single");
    const fs::path file = dir / "users.dat";
    int sel = 0;
    UsersTable out;
    auto res = saveCurrentPlayer(file, player("Solo", 4), sel, &out);
    CHECK(res.written);
    CHECK(out.count == 1);
    CHECK(sel == 0);
    auto bytes = readWholeFile(file);
    REQUIRE(bytes);
    CHECK(bytes->size() == kFileSize);
    // The file was deleted while playing (sys_resources_saves.md §6.9). The original writes the
    // player at the old index behind an empty slot 0, where the next load does not see them; the
    // port appends at count instead (REVIEW_1 H-1 / M-1), so the player stays visible.
    fs::remove(file);
    sel = 3;
    res = saveCurrentPlayer(file, player("Late", 4), sel, &out);
    CHECK(res.written);
    CHECK(out.count == 1);
    CHECK(out.slots[0].name() == "Late");
    CHECK(out.slots[3].empty());
    CHECK(sel == 0);
    UsersTable reread;
    REQUIRE(readUsersFile(file, reread));
    CHECK(names(reread) == std::vector<std::string>{"Late"});
}

// ------------------------------------------------------------------ REVIEW_1 H-1 / M-1 regressions

namespace {

// [0]Ann(50) [1]Bob(30) [2]Cy(10), written by three saves as in the reviewer's harness.
void threePlayers(const fs::path& file) {
    int sel = 0;
    REQUIRE(saveCurrentPlayer(file, player("Ann", 50), sel).written);
    sel = 1;
    REQUIRE(saveCurrentPlayer(file, player("Bob", 30), sel).written);
    sel = 2;
    REQUIRE(saveCurrentPlayer(file, player("Cy", 10), sel).written);
}

std::vector<std::pair<std::string, uint32_t>> roster(const fs::path& file) {
    UsersTable t;
    readUsersFile(file, t);
    std::vector<std::pair<std::string, uint32_t>> v;
    for (int i = 0; i < t.count; ++i)
        v.emplace_back(t.slots[static_cast<size_t>(i)].name(), t.slots[static_cast<size_t>(i)].totalTreasures());
    return v;
}

using Roster = std::vector<std::pair<std::string, uint32_t>>;

}  // namespace

TEST_CASE("file_util: checked and atomic writes") {
    const fs::path dir = tempDir("atomic");
    const fs::path f = dir / "sub" / "a.bin";
    const std::vector<uint8_t> one(11085, 0x11), two(300, 0x22);
    CHECK(writeWholeFile(f, one));  // creates the directory; size checked after the close
    CHECK(fs::file_size(f) == one.size());
    CHECK(writeFileAtomically(f, two));  // replaces an existing file
    CHECK(readWholeFile(f) == two);
    CHECK_FALSE(fs::exists(dir / "sub" / "a.bin.tmp"));
    CHECK(writeFileAtomically(dir / "new.bin", one));
    CHECK(readWholeFile(dir / "new.bin") == one);
    // The temporary file cannot be created (a directory has its name): false, target unchanged.
    fs::create_directories(dir / "sub" / "a.bin.tmp");
    CHECK_FALSE(writeFileAtomically(f, one));
    CHECK(readWholeFile(f) == two);
    // Backups: the replaced contents are kept when they differ and `keep` accepts them.
    const fs::path g = dir / "g.txt";
    const std::vector<uint8_t> v1{'1'}, v2{'2'}, v3{'3'};
    CHECK(replaceFileKeepingBackup(g, v1));
    CHECK_FALSE(fs::exists(backupPathFor(g)));  // nothing to keep yet
    CHECK(replaceFileKeepingBackup(g, v2));
    CHECK(readWholeFile(backupPathFor(g)) == v1);
    CHECK(replaceFileKeepingBackup(g, v2));  // identical: the backup keeps the older version
    CHECK(readWholeFile(backupPathFor(g)) == v1);
    CHECK(replaceFileKeepingBackup(g, v3, [](std::span<const uint8_t>) { return false; }));
    CHECK(readWholeFile(g) == v3);
    CHECK(readWholeFile(backupPathFor(g)) == v1);  // `keep` refused v2
    CHECK(backupPathFor(g).filename() == "g.txt.bak");
}

TEST_CASE("users.dat (REVIEW_1 H-1): an interrupted write never costs the other players") {
    const fs::path dir = tempDir("h1");
    const fs::path file = dir / "users.dat";
    threePlayers(file);
    CHECK(roster(file) == Roster{{"Ann", 50}, {"Bob", 30}, {"Cy", 10}});
    CHECK(fs::file_size(file) == kFileSize);  // the format is unchanged
    UsersTable disk;
    REQUIRE(readUsersFile(file, disk));
    CHECK(readWholeFile(file) == disk.fileBytes());
    // users.dat.bak holds the file before the last change: Ann and Bob.
    CHECK(roster(usersBackupPath(file)) == Roster{{"Ann", 50}, {"Bob", 30}});
    const auto good = *readWholeFile(file);

    SUBCASE("with a valid users.dat.bak the roster is rebuilt from it") {
        // The reviewer's scenario: a write interrupted after 5 000 bytes, then Bob (slot 1) saves.
        auto bytes = good;
        bytes.resize(5000);
        REQUIRE(writeWholeFile(file, bytes));
        UsersTable t;
        CHECK(readUsersFileStatus(file, t) == UsersFileStatus::Short);
        const RosterLoad load = loadRoster(file, t);  // what the sign-in shows
        CHECK(load.fromBackup);
        CHECK(load.usable());
        CHECK(names(t) == std::vector<std::string>{"Ann", "Bob"});
        int sel = 1;
        const auto r = saveCurrentPlayer(file, player("Bob", 31), sel);
        CHECK(r.attempted);
        CHECK(r.written);
        CHECK(r.fromBackup);
        CHECK(roster(file) == Roster{{"Ann", 50}, {"Bob", 31}});  // was count=0 before the fix
        CHECK(fs::file_size(file) == kFileSize);
        // The damaged file did not replace the backup.
        CHECK(roster(usersBackupPath(file)) == Roster{{"Ann", 50}, {"Bob", 30}});
    }
    SUBCASE("without a valid backup the damaged file is not overwritten") {
        fs::remove(usersBackupPath(file));
        auto bytes = good;
        bytes.resize(5000);
        REQUIRE(writeWholeFile(file, bytes));
        int sel = 1;
        UsersTable merged;
        const auto r = saveCurrentPlayer(file, player("Bob", 31), sel, &merged);
        CHECK(r.attempted);
        CHECK_FALSE(r.written);  // the caller shows the Write Error box (DS:2084)
        CHECK(r.refused);
        CHECK(readWholeFile(file) == bytes);  // untouched: nothing of Ann and Cy is destroyed
        CHECK(names(merged) == std::vector<std::string>{"Bob"});  // the in-memory roster
        // A backup that is itself short does not help either.
        REQUIRE(writeWholeFile(usersBackupPath(file), std::vector<uint8_t>(100, 0)));
        CHECK_FALSE(saveCurrentPlayer(file, player("Bob", 32), sel).written);
        CHECK(readWholeFile(file) == bytes);
    }
    SUBCASE("an unreadable users.dat is treated like a damaged one") {
        fs::remove(file);
        fs::create_directories(file);  // exists, but no file can be read there
        UsersTable t;
        const UsersFileStatus st = readUsersFileStatus(file, t);
        CHECK(st != UsersFileStatus::Ok);
        CHECK(st != UsersFileStatus::Missing);
        const RosterLoad load = loadRoster(file, t);
        CHECK(load.fromBackup);
        CHECK(names(t) == std::vector<std::string>{"Ann", "Bob"});
        fs::remove(usersBackupPath(file));
        int sel = 0;
        CHECK(saveCurrentPlayer(file, player("Ann", 51), sel).refused);
        CHECK(fs::is_directory(file));
    }
    SUBCASE("a failed write leaves users.dat as it was") {
        fs::create_directories(dir / "users.dat.tmp");  // the temporary file cannot be created
        int sel = 1;
        const auto r = saveCurrentPlayer(file, player("Bob", 99), sel);
        CHECK(r.attempted);
        CHECK_FALSE(r.written);
        CHECK(readWholeFile(file) == good);
        CHECK_FALSE(writeUsersFile(file, UsersTable{}));
        CHECK(readWholeFile(file) == good);
    }
    SUBCASE("a missing users.dat is still an empty roster (the original's rule)") {
        fs::remove(file);
        UsersTable t;
        const RosterLoad load = loadRoster(file, t);
        CHECK(load.file == UsersFileStatus::Missing);
        CHECK_FALSE(load.fromBackup);
        CHECK(load.usable());
        CHECK(t.count == 0);
        int sel = 0;
        CHECK(saveCurrentPlayer(file, player("Dee", 1), sel).written);
        CHECK(roster(file) == Roster{{"Dee", 1}});
    }
}

TEST_CASE("users.dat (REVIEW_1 M-1): a stale slot index never overwrites another player") {
    const fs::path dir = tempDir("m1");
    const fs::path file = dir / "users.dat";
    SUBCASE("two sessions (the reviewer's harness H2)") {
        threePlayers(file);
        int selBob = 1;  // session 1 signed in Bob
        int selCy = 2;   // session 2 signed in Cy
        REQUIRE(saveCurrentPlayer(file, player("Cy", 40), selCy).written);  // Cy passes Bob
        CHECK(roster(file) == Roster{{"Ann", 50}, {"Cy", 40}, {"Bob", 30}});
        CHECK(selCy == 1);
        REQUIRE(saveCurrentPlayer(file, player("Bob", 32), selBob).written);  // stale index 1
        // Before the fix: [Ann(50), Bob(32), Bob(30)], Cy's progress gone and Bob duplicated.
        CHECK(roster(file) == Roster{{"Ann", 50}, {"Cy", 40}, {"Bob", 32}});
        CHECK(selBob == 2);
    }
    SUBCASE("a save after a failed write") {
        int sel = 0;
        REQUIRE(saveCurrentPlayer(file, player("Ann", 50), sel).written);
        sel = 1;
        REQUIRE(saveCurrentPlayer(file, player("Bob", 30), sel).written);
        fs::create_directories(dir / "users.dat.tmp");
        CHECK_FALSE(saveCurrentPlayer(file, player("Bob", 60), sel).written);
        CHECK(sel == 0);  // the merged order that never reached the disk
        fs::remove(dir / "users.dat.tmp");
        REQUIRE(saveCurrentPlayer(file, player("Bob", 61), sel).written);
        CHECK(roster(file) == Roster{{"Bob", 61}, {"Ann", 50}});  // not [Bob(61), Bob(30)]
    }
    SUBCASE("the player was removed by another session: appended, not written behind empty slots") {
        threePlayers(file);
        UsersTable t;
        REQUIRE(readUsersFile(file, t));
        removeSlot(t, 0);
        removeSlot(t, 0);  // only Cy is left
        REQUIRE(writeUsersFile(file, t));
        int sel = 1;  // Bob, as this session remembers him
        REQUIRE(saveCurrentPlayer(file, player("Bob", 33), sel).written);
        CHECK(roster(file) == Roster{{"Bob", 33}, {"Cy", 10}});
        CHECK(sel == 0);
    }
    SUBCASE("mergePlayer: identical to the original while its assumptions hold; a full roster refuses") {
        UsersTable a;
        for (int i = 0; i < 7; ++i)
            a.slots[static_cast<size_t>(i)] = makeSlot("P" + std::to_string(i), 70u - static_cast<uint32_t>(i));
        a.recount();
        UsersTable b = a;
        int sel = 3;
        CHECK(mergePlayer(b, player("P3", 100), sel));
        CHECK(sel == 0);
        CHECK(b.slots[0].name() == "P3");
        CHECK(b.count == 7);
        int selNew = 7;  // a new eighth player cannot exist in the original (the list is full)
        UsersTable c = b;
        CHECK_FALSE(mergePlayer(c, player("Zed", 1), selNew));
        CHECK(c == b);
        // Out-of-range indices re-find the name.
        int neg = -5;
        CHECK(mergePlayer(c, player("P6", 64), neg));
        CHECK(c.slots[static_cast<size_t>(neg)].name() == "P6");
        CHECK(c.count == 7);
    }
}

TEST_CASE("users.dat: Remove Player") {
    const fs::path dir = tempDir("remove");
    const fs::path file = dir / "users.dat";
    UsersTable t;
    const char* n[] = {"P0", "P1", "P2", "P3", "P4", "P5", "P6"};
    for (int i = 0; i < 7; ++i) t.slots[static_cast<size_t>(i)] = makeSlot(n[i], 70u - static_cast<uint32_t>(i));
    t.recount();
    REQUIRE(t.count == 7);
    const UserSlot last = t.slots[6];
    // "No": nothing changes, but the file is written anyway.
    CHECK(removePlayer(file, t, 2, false));
    UsersTable reread;
    readUsersFile(file, reread);
    CHECK(reread == t);
    // "Yes": slots shift down, slot 6 keeps a stale copy except its first byte.
    CHECK(removePlayer(file, t, 2, true));
    CHECK(names(t) == std::vector<std::string>{"P0", "P1", "P3", "P4", "P5", "P6"});
    CHECK(t.slots[6].bytes[0] == 0);
    CHECK(t.slots[6].bytes[1] == last.bytes[1]);
    CHECK(t.slots[5] == last);
    readUsersFile(file, reread);
    CHECK(names(reread) == names(t));
    // Removing the last row and down to zero players.
    removeSlot(t, 5);
    CHECK(names(t) == std::vector<std::string>{"P0", "P1", "P3", "P4", "P5"});
    for (int i = 0; i < 6; ++i) removeSlot(t, 0);
    CHECK(t.count == 0);
}

TEST_CASE("users.dat: findByName is a byte-exact compare") {
    UsersTable t;
    t.slots[0] = makeSlot("Tester", 0);
    t.slots[1] = makeSlot("tester", 0);
    t.slots[2] = makeSlot("Test", 0);
    t.recount();
    CHECK(t.findByName("tester") == 1);
    CHECK(t.findByName("Test") == 2);
    CHECK(t.findByName("Tes") == 3);  // not found = count
}

TEST_CASE("users.dat: byte-identical round trip of a real file written by the original") {
    const auto src = getEnv("TM_USERS_DAT");
    if (!src) {
        MESSAGE("TM_USERS_DAT is not set: test skipped");
        return;
    }
    const auto original = readWholeFile(pathFromUtf8(*src));
    REQUIRE(original);
    REQUIRE(original->size() == kFileSize);

    UsersTable t;
    REQUIRE(parseUsersFile(*original, t));
    MESSAGE(std::format("{} players", t.count));
    CHECK(t.count >= 1);
    CHECK(t.fileBytes() == *original);

    const fs::path dir = tempDir("real");
    const fs::path copy = dir / "users.dat";
    REQUIRE(writeUsersFile(copy, t));
    CHECK(readWholeFile(copy) == original);

    // Saving every player unchanged (fields read back from the file) rewrites the identical file
    // and finds the player at the same index.
    for (int i = 0; i < t.count; ++i) {
        REQUIRE(writeWholeFile(copy, *original));
        const PlayerRecord rec = PlayerRecord::fromSlot(t.slots[static_cast<size_t>(i)]);
        int sel = i;
        const auto res = saveCurrentPlayer(copy, rec, sel);
        CHECK(res.written);
        CHECK(sel == t.findByName(rec.name));
        CHECK(readWholeFile(copy) == original);
    }
}
