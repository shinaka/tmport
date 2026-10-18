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
    // The file was deleted while playing: the player is written at the old index; count 1 (edge
    // case of sys_resources_saves.md §6.9: slot 0 stays empty, so the next load sees nobody).
    fs::remove(file);
    sel = 3;
    saveCurrentPlayer(file, player("Late", 4), sel, &out);
    CHECK(out.count == 1);
    CHECK(out.slots[3].name() == "Late");
    UsersTable reread;
    readUsersFile(file, reread);
    CHECK(reread.count == 0);
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
