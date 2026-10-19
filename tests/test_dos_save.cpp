// DOS edition USERS.DAT (dos/save/users_dat.h, users_store.h). Synthetic files always; byte-exact
// round trips of the user's TM-DOS\USERS.DAT (TM_DATA_DOS, read only) and of every reference file
// written by the original in DOSBox-X (TM_DOS_CAPTURES = extracted\captures\dos: USERS_*.DAT,
// original\USERS.DAT, savestates\*\*.users; when unset, <TM_DATA_DOS>\..\extracted\captures\dos is
// tried). Writes happen only in a temporary directory.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <vector>

#include "dos/save/users_dat.h"
#include "dos/save/users_store.h"
#include "dos_test_data.h"
#include "save/settings.h"
#include "util/file_util.h"

using namespace tmport;
using namespace tmport::dos;

namespace {

namespace fs = std::filesystem;

fs::path tempDir(const char* name) {
    const fs::path d = fs::temp_directory_path() / "tmport_tests" / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

std::vector<uint8_t> pseudoRandomFile(uint32_t seed) {
    std::vector<uint8_t> v(kUsersFileSize);
    for (auto& b : v) {
        seed = seed * 1103515245u + 12345u;
        b = static_cast<uint8_t>(seed >> 16);
    }
    return v;
}

std::optional<fs::path> capturesDir() {
    if (auto p = getEnv("TM_DOS_CAPTURES")) return pathFromUtf8(*p);
    if (auto d = testdata::dosDataPath()) {
        const fs::path guess = *d / ".." / "extracted" / "captures" / "dos";
        std::error_code ec;
        if (fs::is_directory(guess, ec)) return guess;
    }
    return std::nullopt;
}

// Every USERS.DAT the original wrote in the reference runs.
std::vector<fs::path> referenceUsersFiles(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string name = toUpperAscii(pathToUtf8(e.path().filename()));
        if ((name.starts_with("USERS") && name.ends_with(".DAT")) || name.ends_with(".USERS")) out.push_back(e.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

TEST_CASE("DOS USERS.DAT: layout") {
    CHECK(kUsersFileSize == 3243);
    CHECK(kUserRecordSize == 360);
    CHECK(UsersDat::recordOffset(0) == 3);
    CHECK(UsersDat::recordOffset(8) == 3 + 8 * 0x168);
    // The typed fields tile the record without gaps (fmt_data.md §4, code_ui.md §9.8).
    size_t at = 0;
    for (const auto& f : urec::kFields) {
        INFO(f.name);
        CHECK(f.offset == at);
        at += f.size;
    }
    CHECK(at == kUserRecordSize);
    CHECK(urec::kVariablePictures == urec::kHidingPictures + 48);
    CHECK(urec::kHidingContents == urec::kVariablePictures + 9);
}

TEST_CASE("DOS USERS.DAT: every byte is a typed field (round trip of arbitrary bytes)") {
    for (uint32_t seed : {1u, 2u, 0xC0FFEEu}) {
        const auto file = pseudoRandomFile(seed);
        const auto d = UsersDat::parse(file);
        REQUIRE(d);
        CHECK(d->bytes() == file);
    }
    CHECK_FALSE(UsersDat::parse(std::vector<uint8_t>(kUsersFileSize - 1, 0)));
    // Extra bytes after the 3243 are ignored (the original reads exactly 0xCAB bytes).
    auto longer = pseudoRandomFile(7);
    const auto exact = longer;
    longer.push_back(0x55);
    CHECK(UsersDat::parse(longer)->bytes() == exact);
}

TEST_CASE("DOS USERS.DAT: typed fields land at their offsets") {
    UsersDat d;
    d.spriteHighWater = 0x05A1;
    UserRecord& r = d.records[2];
    r.setName("Abcdefghij");  // at most 8 characters, the rest NUL
    r.savedGame = 1;
    r.totalTreasures = 300;
    r.quizSlots[8] = 0x8123;
    r.levelPicWords[5] = 0xBEEF;
    r.foundMasks[2] = 0x0F;
    r.quizTier = 5;
    r.cameraX = -128;
    r.shopCoinsOwed = -1;
    r.castleLayout = -2;
    r.bankedCoins = 0x1234;
    const auto b = d.bytes();
    const size_t o = UsersDat::recordOffset(2);
    CHECK(b[0] == 0);
    CHECK(b[1] == 0xA1);
    CHECK(b[2] == 0x05);
    CHECK(b[o + 7] == 'h');
    CHECK(b[o + 8] == 0);
    CHECK(b[o + 0x009] == 1);
    CHECK(b[o + 0x00A] == 0x2C);
    CHECK(b[o + 0x00B] == 0x01);
    CHECK(b[o + 0x066 + 16] == 0x23);
    CHECK(b[o + 0x067 + 16] == 0x81);
    CHECK(b[o + 0x124 + 10] == 0xEF);
    CHECK(b[o + 0x124 + 11] == 0xBE);
    CHECK(b[o + 0x132] == 0x0F);
    CHECK(b[o + 0x13F] == 5);
    CHECK(b[o + 0x152] == 0x80);
    CHECK(b[o + 0x153] == 0xFF);
    CHECK(b[o + 0x158] == 0xFF);
    CHECK(b[o + 0x159] == 0xFF);
    CHECK(b[o + 0x15C] == 0x34);
    CHECK(b[o + 0x166] == 0xFE);
    CHECK(b[o + 0x167] == 0xFF);
    CHECK(d.records[2].nameString().size() == 8);
    CHECK(UsersDat::parse(b)->records[2] == r);
}

TEST_CASE("DOS USERS.DAT: question history, clear, names") {
    UserRecord r;
    r.setHistory(0, 3);
    r.setHistory(1, 1);
    r.setHistory(359, 2);
    CHECK(r.quizHistory[0] == (3 | (1 << 2)));
    CHECK(r.quizHistory[89] == (2 << 6));
    CHECK(r.history(0) == 3);
    CHECK(r.history(1) == 1);
    CHECK(r.history(2) == 0);
    CHECK(r.history(359) == 2);
    r.setHistory(0, 0);
    CHECK(r.history(0) == 0);
    CHECK(r.history(1) == 1);

    // Users_ClearRecord (47c7:075d): zeros, then answer masks 7.
    r.setName("Zed");
    r.coins = 9;
    r.clear();
    CHECK(r.empty());
    CHECK(r.coins == 0);
    for (uint8_t m : r.answerMasks) CHECK(m == 7);
    CHECK(r.quizHistory[0] == 0);

    // Users_NameExists (47c7:04a4): trailing blanks dropped, case ignored, empty records skipped.
    UsersDat d;
    CHECK_FALSE(d.anyPlayer());
    CHECK(d.firstFreeRecord() == 0);
    d.records[0].setName("Ada");
    d.records[3].setName("Ben");
    CHECK(d.anyPlayer());
    CHECK(d.playerCount() == 2);
    CHECK(d.firstFreeRecord() == 1);
    CHECK(d.findName("ada") == 0);
    CHECK(d.findName("BEN  ") == 3);
    CHECK(d.findName(std::string_view("Ben\0\0", 5)) == 3);
    CHECK(d.findName("Be") == -1);
    CHECK(d.findName("Bens") == -1);
    for (auto& rec : d.records) rec.setName("X");
    CHECK(d.firstFreeRecord() == -1);
}

TEST_CASE("DOS USERS.DAT: Users_ReadFile semantics for a short file") {
    auto file = pseudoRandomFile(3);
    file.resize(100);
    bool complete = true;
    const UsersDat d = UsersDat::fromImage(file, &complete);
    CHECK_FALSE(complete);
    const auto b = d.bytes();
    CHECK(std::equal(file.begin(), file.end(), b.begin()));
    CHECK(std::all_of(b.begin() + 100, b.end(), [](uint8_t v) { return v == 0; }));
}

TEST_CASE("DOS USERS.DAT: the user's TM-DOS\\USERS.DAT round trips byte for byte") {
    const auto dir = testdata::dosDataPath();
    if (!dir) {
        MESSAGE("TM_DATA_DOS is not set: test skipped");
        return;
    }
    const auto path = findDosUsersDat(*dir);
    REQUIRE(path);
    const auto data = readWholeFile(*path);
    REQUIRE(data);
    REQUIRE(data->size() == kUsersFileSize);
    const auto d = UsersDat::parse(*data);
    REQUIRE(d);
    CHECK(d->bytes() == *data);
    CHECK(d->playerCount() >= 0);
}

TEST_CASE("DOS USERS.DAT: every reference file of the original round trips byte for byte") {
    const auto dir = capturesDir();
    if (!dir) {
        MESSAGE("TM_DOS_CAPTURES is not set: test skipped");
        return;
    }
    const auto files = referenceUsersFiles(*dir);
    CHECK(files.size() >= 8);
    int checked = 0;
    for (const auto& f : files) {
        INFO(pathToUtf8(f));
        const auto data = readWholeFile(f);
        REQUIRE(data);
        CHECK(data->size() == kUsersFileSize);
        const auto d = UsersDat::parse(*data);
        REQUIRE(d);
        CHECK(d->bytes() == *data);
        CHECK(d->header0 == 0);
        for (const auto& r : d->records) CHECK(r.nameString().size() <= kUserMaxNameLength);
        ++checked;
    }
    MESSAGE("reference USERS.DAT files checked: " << checked);

    // Field values against tools/dos/users_dos.py for one file the original wrote after a game.
    const fs::path one = *dir / "USERS_after_castle_climb_Ada.DAT";
    if (const auto data = readWholeFile(one)) {
        const auto d = UsersDat::parse(*data);
        REQUIRE(d);
        CHECK(d->spriteHighWater == 0x04F6);
        CHECK(d->playerCount() == 2);
        const UserRecord& r = d->records[1];
        CHECK(r.nameString().size() == 3);
        CHECK(r.savedGame == 0);
        CHECK(r.gamesPlayed == 1);
        CHECK(r.coins == 31);
        CHECK(r.bankedCoins == 31);
        CHECK(r.bankedNets == 0);
        CHECK(r.playerX == 240);
        CHECK(r.playerY == 112);
        CHECK(r.cameraX == 192);
        CHECK(r.shopCoinsOwed == 4);
        CHECK(r.shownTreasure == -1);
        CHECK(r.musicOn == 1);
        CHECK(r.soundOn == 1);
        CHECK(r.quizSlots[0] == 32798);
        CHECK(r.quizSlots[8] == 32796);
        CHECK(r.levelPicWords == std::array<uint16_t, 6>{40, 0, 10, 0, 5, 0});
        int counts[4] = {};
        for (int q = 0; q < kQuizQuestions; ++q) ++counts[r.history(q)];
        CHECK(counts[0] == 351);
        CHECK(counts[1] == 9);
        const UserRecord& r0 = d->records[0];
        CHECK(r0.savedGame == 1);
        CHECK(r0.area == 5);
        CHECK(r0.keyState == 8);
        CHECK(r0.levelPicWords == std::array<uint16_t, 6>{1, 0, 22, 0, 28, 0});
    }
}

TEST_CASE("DOS users store: path, write moments, damaged files") {
    const fs::path dir = tempDir("dos_users_store");
    const auto paths = save::Paths::under(dir, dir / "exe");
    CHECK(dosUsersPath(paths) == dir / "dos_users.dat");

    DosUsersStore store(dosUsersPath(paths));
    CHECK(store.load() == DosUsersStatus::Missing);
    CHECK_FALSE(store.users().anyPlayer());
    CHECK(store.writable());

    // New player (47c7:07b9): the caller packs the record, then the name is copied and the file written.
    const int rec = store.users().firstFreeRecord();
    REQUIRE(rec == 0);
    store.record(rec).bankedCoins = 5;
    store.record(rec).bankedNets = 10;
    store.users().spriteHighWater = 0x0480;
    REQUIRE(store.saveNewPlayer(rec, "Ada"));
    CHECK(store.lastMoment() == DosSaveMoment::NewPlayer);
    auto onDisk = readWholeFile(store.path());
    REQUIRE(onDisk);
    CHECK(onDisk->size() == kUsersFileSize);
    CHECK(*onDisk == store.users().bytes());

    // Exit and Save with a game in progress keeps the saved game; Game_End banks coins and nets.
    REQUIRE(store.saveExitAndSave(rec, true));
    CHECK(store.record(rec).savedGame == 1);
    REQUIRE(store.saveGameEnd(rec, 7, 12));
    CHECK(store.record(rec).savedGame == 0);
    CHECK(store.record(rec).bankedNets == 7);
    CHECK(store.record(rec).bankedCoins == 12);
    REQUIRE(store.saveStartNewPlayer(rec, false));
    REQUIRE(store.saveStartOver(rec));
    CHECK(store.writes() == 5);

    DosUsersStore again(store.path());
    CHECK(again.load() == DosUsersStatus::Ok);
    CHECK(again.users() == store.users());
    CHECK(again.users().records[0].nameString() == "Ada");

    // Remove player: Users_ClearRecord + write.
    REQUIRE(again.removePlayer(0));
    CHECK(again.users().records[0].empty());
    CHECK(again.users().records[0].answerMasks[8] == 7);
    CHECK(readWholeFile(again.path()) == again.users().bytes());
    // The replaced (whole) file was kept as .bak.
    CHECK(readWholeFile(backupPathFor(again.path()))->size() == kUsersFileSize);

    // A short file is not overwritten from scratch; with a whole .bak the roster comes from it.
    REQUIRE(writeWholeFile(again.path(), std::vector<uint8_t>(10, 1)));
    DosUsersStore damaged(again.path());
    CHECK(damaged.load() == DosUsersStatus::Short);
    CHECK(damaged.fromBackup());
    CHECK(damaged.writable());
    std::error_code ec;
    fs::remove(backupPathFor(again.path()), ec);
    DosUsersStore noBackup(again.path());
    CHECK(noBackup.load() == DosUsersStatus::Short);
    CHECK_FALSE(noBackup.writable());
    CHECK_FALSE(noBackup.saveStartOver(0));
    CHECK(readWholeFile(again.path())->size() == 10);
}

TEST_CASE("DOS users store: import from the DOS folder and export back") {
    const fs::path dir = tempDir("dos_users_import");
    const fs::path dosFolder = dir / "TM-DOS-copy";
    fs::create_directories(dosFolder);
    const fs::path engineFile = dir / "pref" / "dos_users.dat";

    // Case-insensitive lookup; a short file is refused.
    REQUIRE(writeWholeFile(dosFolder / "users.dat", std::vector<uint8_t>(100, 0)));
    REQUIRE(findDosUsersDat(dosFolder));
    CHECK(importDosUsers(*findDosUsersDat(dosFolder), engineFile, false) == DosImportResult::SourceInvalid);

    const auto original = pseudoRandomFile(11);
    REQUIRE(writeWholeFile(dosFolder / "users.dat", original));
    DosUsersStore store(engineFile);
    DosImportOutcome outcome;
    CHECK(store.loadWithImport(dosFolder, &outcome) == DosUsersStatus::Ok);
    CHECK(outcome.result == DosImportResult::Imported);
    CHECK(readWholeFile(engineFile) == original);
    CHECK(store.users().bytes() == original);
    // Only once: the engine's file is kept afterwards.
    CHECK(store.loadWithImport(dosFolder, &outcome) == DosUsersStatus::Ok);
    CHECK(outcome.result == DosImportResult::DestExists);
    CHECK(importDosUsers(dosFolder / "users.dat", engineFile, false) == DosImportResult::DestExists);
    CHECK(importDosUsers(dir / "nothing.dat", engineFile, true) == DosImportResult::SourceMissing);

    // Export back: byte for byte.
    const fs::path exported = dir / "out" / "USERS.DAT";
    REQUIRE(exportDosUsers(engineFile, exported));
    CHECK(readWholeFile(exported) == original);

    // The user's own USERS.DAT, copied (never written in place).
    if (const auto d = testdata::dosDataPath()) {
        if (const auto src = findDosUsersDat(*d)) {
            const fs::path engine2 = dir / "pref2" / "dos_users.dat";
            CHECK(importDosUsers(*src, engine2, false) == DosImportResult::Imported);
            CHECK(readWholeFile(engine2) == readWholeFile(*src));
        }
    }
}
