// DOS edition data layer: the file table, ALL_PUZS + PRG_DICT, HELP.DAT, DosFiles and the DGROUP
// offsets, checked against the user's files (TM_DATA_DOS). Only sizes, counts and CRCs are stored.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "dos/data/dgroup_offsets.h"
#include "dos/data/dos_files.h"
#include "dos/data/file_table.h"
#include "dos/data/help_file.h"
#include "dos/data/puzzles.h"
#include "dos_test_data.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/png_writer.h"

using namespace tmport;
using namespace tmport::dos;
using tmport::png::crc32;

namespace {

std::span<const uint8_t> bytesOf(const std::string& s) {
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// A scratch copy of the DOS folder with lower-case names (the originals are only read).
struct ScratchFolder {
    std::filesystem::path dir;
    explicit ScratchFolder(const std::filesystem::path& from) {
        dir = std::filesystem::temp_directory_path() / ("tmtests_dos_data_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        for (const auto& e : std::filesystem::directory_iterator(from))
            if (e.is_regular_file())
                std::filesystem::copy_file(e.path(), dir / toLowerAscii(pathToUtf8(e.path().filename())));
    }
    ~ScratchFolder() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

}  // namespace

// --- Without game data ---------------------------------------------------------------------------

TEST_CASE("DOS puzzles: dictionary expansion and the question split (synthetic)") {
    std::vector<uint8_t> dict(128 * 16, 0);
    const std::string w0 = "ab", w5 = "xyz";
    std::copy(w0.begin(), w0.end(), dict.begin());
    std::copy(w5.begin(), w5.end(), dict.begin() + 5 * 16);
    const auto d = Dictionary::parse(dict);
    CHECK(d.word(0) == "ab");
    CHECK(d.word(0x85) == "xyz");  // b & 0x7F
    const std::vector<uint8_t> src = {' ', ' ', 'Q', 0x80, '~', 'A', 0x85, '\r', 'B', '\r', 'C', '\r', 0x1A, 0, 'Z'};
    const std::string exp = d.expand(src);
    CHECK(exp == std::string("  Qab~Axyz\rB\rC\r\x1a"));  // stops at the NUL
    const Puzzle p = splitPuzzle(exp);
    CHECK(p.question == "  Qab");
    CHECK(p.answers[0] == "Axyz");
    CHECK(p.answers[1] == "B");
    CHECK(p.answers[2] == "C");
    CHECK_THROWS_AS(splitPuzzle("no tilde"), FormatError);
    CHECK_THROWS_AS(Dictionary::parse(std::span<const uint8_t>(dict).first(100)), FormatError);
    const uint8_t history[2] = {0xE4, 0x01};  // statuses 0, 1, 2, 3, then 1
    CHECK(puzzleStatus(history, 0) == 0);
    CHECK(puzzleStatus(history, 1) == 1);
    CHECK(puzzleStatus(history, 2) == 2);
    CHECK(puzzleStatus(history, 3) == 3);
    CHECK(puzzleStatus(history, 4) == 1);
}

TEST_CASE("DOS data files: header checks reject broken files") {
    std::vector<uint8_t> pan = {4, 0, 16, 0, 16, 0, 2, 0, 1, 2, 3, 4, 5, 6};
    CHECK_NOTHROW(checkDosDataFile(FileIndex::MountainPan, "mountain.pan", pan));
    pan.push_back(0);
    CHECK_THROWS_AS(checkDosDataFile(FileIndex::MountainPan, "mountain.pan", pan), FormatError);
    std::vector<uint8_t> pak(48, 0);
    pak[0] = 1;                    // one frame
    pak[8] = 1;                    // at paragraph 1 (offset 16)
    pak[10] = pak[11] = pak[12] = pak[13] = 0xFF;
    CHECK_NOTHROW(checkDosDataFile(FileIndex::BeginPak, "begin.pak", pak));
    pak[12] = 0;
    CHECK_THROWS_AS(checkDosDataFile(FileIndex::BeginPak, "begin.pak", pak), FormatError);
    std::vector<uint8_t> sfx(0x96, 0);
    sfx[0] = 0x96;
    CHECK_NOTHROW(checkDosDataFile(FileIndex::SfxIbm, "sfx.ibm", sfx));
    CHECK_THROWS_AS(checkDosDataFile(FileIndex::SfxTan, "sfx.tan", sfx), FormatError);  // .TAN header is 0x230
    try {
        checkDosDataFile(FileIndex::HelpDat, "help.dat", std::vector<uint8_t>(2, 0));
        FAIL("no exception");
    } catch (const FormatError& e) {
        CHECK(std::string(e.what()).starts_with("HELP.DAT"));
    }
}

TEST_CASE("DOS DGROUP offsets: tables are laid out back to back") {
    using namespace tmport::dos::dg;
    CHECK(kCellPlayerHandlers + 4 * kCellPlayerHandlersCount == kCellVisibleHandlers);
    CHECK(kCellVisibleHandlers + 4 * kCellVisibleHandlersCount == kCreatureMask);
    CHECK(kPlayerStateHandlers + 4 * kPlayerStateHandlersCount == kCellPicture);
    CHECK(kCellPicture + 2 * kCellPictureCount == kCellType);
    CHECK(kCellType + kCellPictureCount == kCellContent);
    CHECK(kCellContent + 2 * kCellPictureCount == kHidingCells);
    CHECK(kHidingCells + 2 * kHidingCellsCount == kVariableCells);
    CHECK(kVariableCells + 2 * kVariableCellsCount == kVariableCellDefaults);
    CHECK(kVariableCellDefaults + 2 * kVariableCellsCount == kLevelPicData);
    CHECK(kLevelPicData + 3 * kLevelPicDataStride == kPictureAttrTables);
    CHECK(kPictureAttrTables + 12 == kCastleLayouts);
    CHECK(kPictureAttrL0 + 4 * kPictureAttrL0Count == kPictureAttrL1);
    CHECK(kPictureAttrL1 + 4 * kPictureAttrL1Count == kPictureAttrL2);
    CHECK(kAnimScripts + 4 * kAnimScriptsCount == kStrMenuHelpItems);
    CHECK(kMsgBoxes + kMsgBoxesCount * kMsgBoxesStride == kLineHeight);
    CHECK(kHelpPicFrame1 + 2 * kHelpPicFrame1Count == kHelpPicFrame2);
    CHECK(kHelpPicY + 2 * kHelpPicFrame1Count == kMenuZoneCounts);
    CHECK(kElfStateHandlers + 4 * kElfStateHandlersCount == kAnswerPerms);
    CHECK(kClueWordText + 4 * kClueWordTextCount == kQuizFocusStep);
    CHECK(kFlutterText + 4 * kFlutterTextCount == kHintRankThresholds);
    CHECK(kDialogYesHandlers + 4 * kDialogYesHandlersCount == kDialogNoHandlers);
    CHECK(kOplInstruments + kOplInstrumentsCount * kOplInstrumentsStride == kOplInstrumentPtrs);
    CHECK(kOplInstrumentPtrs + 4 * kOplInstrumentsCount == kOplOrchInstrument);
    CHECK(kFileTable + (FileTable::kCount + 1) * filerec::kSize == kSavedVideoMode);
    CHECK(kSfxBlock + sndblock::kSize == kMusicBlock);
    CHECK(kSndWork + sndblock::kSize + 2 == kSfxBlock);  // two bytes between the work copy and the blocks
    CHECK(kZoneLists + 4 * kZoneListsCount == kKeyLists);
    CHECK(kKeyLists + 4 * kZoneListsCount == kCmdLists);
    CHECK(kZonesQuiz + zonerec::kFirst + 9 * zonerec::kSize == kQuizAnswerZones);
    CHECK(kZonesTitle + zonerec::kFirst + 2 * zonerec::kSize - 1 == kTitleBottomZoneCmd);
}

// --- With the user's DOS files (TM_DATA_DOS) ------------------------------------------------------

TEST_CASE("DOS file table: 18 files and their disks") {
    TM_REQUIRE_DOS_DATA();
    const auto t = FileTable::read(*dosExe);
    const int disk2[] = {0, 1, 2, 6, 7};
    for (int i = 0; i < FileTable::kCount; ++i) {
        const bool two = std::find(std::begin(disk2), std::end(disk2), i) != std::end(disk2);
        CHECK(t.entry(i).disk == (two ? 2 : 1));
        CHECK(t.indexOf(toUpperAscii(t.entry(i).name)) == i);
    }
    CHECK(t.indexOf("sprites.pak") == static_cast<int>(FileIndex::SpritesPak));
    CHECK(t.indexOf("SHACK.PAK") == static_cast<int>(FileIndex::ShackPak));
    CHECK(!t.indexOf("SST.EXE"));
}

TEST_CASE("DOS puzzles: 360 questions, dictionary, tiers and the 0x7B00 windows") {
    TM_REQUIRE_DOS_DATA();
    TM_REQUIRE_DOS_FILES();
    const auto* files = dosFiles;
    const auto allPuzs = files->bytes(FileIndex::AllPuzs);
    const auto prgDict = files->bytes(FileIndex::PrgDict);
    CHECK(allPuzs.size() == 43887);
    CHECK(prgDict.size() == 3328);
    CHECK(crc32(allPuzs) == 0x9DA2663Du);
    CHECK(crc32(prgDict) == 0xD74CB158u);

    const auto dict = Dictionary::parse(prgDict);
    CHECK(dict.entryCount() == 208);
    uint32_t dcrc = 0;
    for (int i = 0; i < Dictionary::kReachable; ++i) {
        CHECK(!dict.word(i).empty());
        CHECK(dict.word(i).size() < Dictionary::kEntrySize);  // every reachable entry has its NUL
        dcrc = crc32(bytesOf(std::string(dict.word(i)) + '\0'), dcrc);
    }
    CHECK(dcrc == 0xC9E70B80u);

    const auto puz = PuzzleFile::parse(allPuzs);
    CHECK(puz.offset(0) == 0);
    uint32_t crc = 0, expCrc = 0;
    for (int i = 0; i < PuzzleFile::kCount; ++i) {
        const std::string exp = dict.expand(puz.raw(i));
        CHECK(std::count(exp.begin(), exp.end(), '~') == 1);
        const auto after = exp.substr(exp.find('~') + 1);
        CHECK(std::count(after.begin(), after.end(), '\r') >= 3);  // answers never run into the next text
        const Puzzle p = puz.puzzle(i, dict);
        CHECK(p.question.starts_with("  "));
        for (const auto* s : {&p.question, &p.answers[0], &p.answers[1], &p.answers[2]})
            crc = crc32(bytesOf(*s + '\0'), crc);
        expCrc = crc32(bytesOf(exp + '\0'), expCrc);
    }
    CHECK(crc == 0x8B96A851u);
    CHECK(expCrc == 0xA995C962u);

    CHECK(puz.tier5Start() == 123);
    const auto tiers = QuizTiers::read(*dosExe, puz);
    const int expected[] = {0, 63, 180, 303, 360, 123, 360};
    for (int k = 0; k < QuizTiers::kEntries; ++k) CHECK(tiers.start(k) == expected[k]);
    const size_t spans[] = {5878, 12734, 15921, 8634, 0, 30660};
    for (const int tier : {0, 1, 2, 3, 5}) {
        const auto [first, end] = tiers.range(tier);
        for (int q = first; q < end; ++q) CHECK(puz.fitsWindow(first, q));
        const size_t last = puz.textPos(end - 1) + puz.raw(end - 1).size() + 1;
        CHECK(last - puz.textPos(first) == spans[tier]);
        CHECK(last - puz.textPos(first) <= PuzzleFile::kWindowSize);
    }
    CHECK(!puz.fitsWindow(0, 359));  // the whole file does not fit one read
}

TEST_CASE("DOS HELP.DAT: 15 pages in 4 topics and the page pictures") {
    TM_REQUIRE_DOS_DATA();
    TM_REQUIRE_DOS_FILES();
    const auto* files = dosFiles;
    const auto raw = files->bytes(FileIndex::HelpDat);
    CHECK(raw.size() == 3726);
    CHECK(crc32(raw) == 0xC4E53250u);
    const auto h = HelpFile::parse(raw);
    CHECK(h.totalPages() == 15);
    const int counts[] = {6, 4, 4, 1};
    for (int t = 0; t < HelpFile::kTopics; ++t) CHECK(h.pageCount(t) == counts[t]);
    CHECK(h.pageIndex(2, 1) == 11);
    CHECK(h.pageOffset(0) == 0x22);
    const size_t lengths[] = {219, 261, 268, 216, 270, 297, 229, 201, 207, 235, 364, 271, 201, 159, 279};
    uint32_t crc = 0;
    for (int i = 0; i < h.totalPages(); ++i) {
        CHECK(h.pageText(i).size() == lengths[i]);
        crc = crc32(bytesOf(std::string(h.pageText(i)) + '\0'), crc);
    }
    CHECK(crc == 0xC9D95CBBu);
    // Pages 0, 1, 3 and 10 have pictures (code_ui.md §6.1): frame codes of SPRITES 218, 94, 123, 72.
    CHECK(helpPagePictures(*dosExe, 0).frame1 == 436);
    CHECK(helpPagePictures(*dosExe, 1).frame1 == 188);
    CHECK(helpPagePictures(*dosExe, 3).frame1 == 246);
    CHECK(helpPagePictures(*dosExe, 10).frame1 == 144);
    CHECK(helpPagePictures(*dosExe, 2).frame1 == -1);
    CHECK(helpPagePictures(*dosExe, 10).frame2 == -1);
}

TEST_CASE("DosFiles: opens the DOS folder and checks every file") {
    TM_REQUIRE_DOS_DATA();
    TM_REQUIRE_DOS_FILES();
    const auto* files = dosFiles;
    CHECK(DosFiles::looksLikeDosFolder(files->folder()));
    CHECK(files->exe().image().size() == DosExe::kImageSize);
    CHECK(files->bytes("sst.exe").size() == 115011);
    const size_t sizes[FileTable::kCount] = {64599, 48302, 51554, 9623, 10159, 3726, 116240, 43887, 3328,
                                             3243,  1680,  3152,  1622, 5000,  18421, 28432, 87936, 50368};
    for (int i = 0; i < FileTable::kCount; ++i) {
        if (static_cast<FileIndex>(i) == FileIndex::UsersDat && !files->hasUsersDat()) continue;
        CHECK(files->bytes(static_cast<FileIndex>(i)).size() == sizes[i]);
    }
    // PAK frame counts (fmt_graphics.md §5.4): 223 + 42 + 133 + 126 = 524.
    const auto frames = [&](FileIndex f) { return files->bytes(f)[0] | (files->bytes(f)[1] << 8); };
    CHECK(frames(FileIndex::SpritesPak) == 223);
    CHECK(frames(FileIndex::BeginPak) == 42);
    CHECK(frames(FileIndex::ClosePak) == 133);
    CHECK(frames(FileIndex::ShackPak) == 126);
    CHECK(files->bytes("Sprites.Pak").data() == files->bytes(FileIndex::SpritesPak).data());
    CHECK(files->pathOf("help.dat").has_value());
    CHECK_THROWS_AS(files->bytes("TLCINFO.DAT"), std::out_of_range);
}

TEST_CASE("DosFiles: names the missing or broken file") {
    TM_REQUIRE_DOS_DATA();
    const ScratchFolder copy(*testdata::dosDataPath());
    CHECK_NOTHROW(DosFiles::open(copy.dir));  // lower-case names are found

    std::filesystem::remove(copy.dir / "users.dat");
    {
        const auto f = DosFiles::open(copy.dir);  // optional
        CHECK(!f.hasUsersDat());
        CHECK_THROWS_AS(f.bytes(FileIndex::UsersDat), MissingFileError);
    }
    std::filesystem::remove(copy.dir / "all_puzs");
    CHECK(!DosFiles::looksLikeDosFolder(copy.dir));
    try {
        (void)DosFiles::open(copy.dir);
        FAIL("no exception");
    } catch (const MissingFileError& e) {
        CHECK(e.file() == "ALL_PUZS");
    }
    std::filesystem::copy_file(*testdata::dosDataPath() / "ALL_PUZS", copy.dir / "ALL_PUZS");
    auto help = *readWholeFile(copy.dir / "help.dat");
    help.resize(40);
    REQUIRE(writeWholeFile(copy.dir / "help.dat", help));
    try {
        (void)DosFiles::open(copy.dir);
        FAIL("no exception");
    } catch (const FormatError& e) {
        CHECK(std::string(e.what()).starts_with("HELP.DAT"));
    }
}

TEST_CASE("DOS DGROUP offsets: tables match the user's SST.EXE") {
    TM_REQUIRE_DOS_DATA();
    using namespace tmport::dos::dg;
    const auto& x = *dosExe;
    CHECK(x.ds16(kRandSeed) == 1);
    CHECK(x.ds16(kRandSeed + 2) == 0);
    // Code pointer tables point into their modules.
    for (size_t t = 0; t < kCellPlayerHandlersCount; ++t) {
        const auto p = x.readFarPtr(static_cast<uint16_t>(kCellPlayerHandlers + 4 * t));
        CHECK((p.seg == 0x1568 || p.seg == 0x176a));
    }
    for (size_t t = 0; t < kElfStateHandlersCount; ++t) {
        const auto p = x.readFarPtr(static_cast<uint16_t>(kElfStateHandlers + 4 * t));
        CHECK(p.seg == 0x4521);  // Null_Handler2 43cb:156b is 4521:000b
    }
    for (size_t t = 0; t < kPlayerStateHandlersCount; ++t) {
        const auto p = x.readFarPtr(static_cast<uint16_t>(kPlayerStateHandlers + 4 * t));
        CHECK((p.seg == 0x18cf || p.seg == 0x1568));
    }
    CHECK(x.readFarPtr(kScreenPainters).seg == 0x130f);
    // DGROUP pointer tables point at the tables named here.
    const auto ds = [&](uint16_t at) { const auto p = x.readFarPtr(at); return p.seg == DosExe::kDgroupSeg ? p.off : 0xFFFF; };
    CHECK(ds(kAnimScripts) == kAnimScriptData);
    CHECK(ds(kPictureAttrTables) == kPictureAttrL0);
    CHECK(ds(kPictureAttrTables + 4) == kPictureAttrL1);
    CHECK(ds(kPictureAttrTables + 8) == kPictureAttrL2);
    CHECK(ds(kPlayerSlotPtr) == kSpriteSlots + slotrec::kSize);
    CHECK(ds(kUserNamePtrs) == kUsersFile + 3);
    CHECK(ds(kClueWordText) == kStrClueWords);
    CHECK(ds(kOplInstrumentPtrs) == kOplInstruments);
    CHECK(ds(kMsgBoxes + boxrec::kText) == kStrMenuHelpItems);
    CHECK(ds(kDiskPathPtrs + 4) == kPathDisk1);
    CHECK(ds(kDiskPathPtrs + 8) == kPathDisk2);
    const uint16_t zones[] = {kZonesPlay, kZonesMenu, kZonesDialog, kZonesQuiz, kSigninZones, kZonesTitle};
    const uint16_t keys[] = {kKeysPlay, kKeysMenu, kKeysDialog, kKeysPlay, kKeysName, kKeysPlay};
    const uint16_t cmds[] = {kCmdsPlay, kCmdsMenu, kCmdsDialog, kCmdsPlay, kCmdsName, kCmdsPlay};
    for (int c = 0; c < 6; ++c) {
        CHECK(ds(static_cast<uint16_t>(kZoneLists + 4 * c)) == zones[c]);
        CHECK(ds(static_cast<uint16_t>(kKeyLists + 4 * c)) == keys[c]);
        CHECK(ds(static_cast<uint16_t>(kCmdLists + 4 * c)) == cmds[c]);
    }
    CHECK(x.ds16(kKeysPlay) == 13);  // 13 keys of contexts 0, 3, 5 (code_runtime.md §6.1)
    CHECK(x.ds16(kKeysName) == 69);
    // Values of small tables (the docs quote them).
    const int rank[] = {5, 1, 25, 8, 70, 16, 115, 24, 170, 32, 230, 40, 300, 48, 400, 56, 9999, 66, 9999, 86, -1};
    for (int i = 0; i < 21; ++i) CHECK(x.dsS16(static_cast<uint16_t>(kRankTable + 2 * i)) == rank[i]);
    const int elves[] = {3, 0, 3, 3, 3, 2, 3, 3, 3, 4, 3, 3, 3, 6, 3, 3};
    for (int i = 0; i < 16; ++i) CHECK(x.ds16(static_cast<uint16_t>(kElfCounts + 2 * i)) == elves[i]);
    const int thresholds[] = {5, 25, 70, 115, 170, 230, 300};
    for (int i = 0; i < 7; ++i) {
        CHECK(x.ds16(static_cast<uint16_t>(kSigninRankThresholds + 2 * i)) == thresholds[i]);
        CHECK(x.ds16(static_cast<uint16_t>(kHintRankThresholds + 2 * i)) == thresholds[i]);
    }
    CHECK(x.ds16(kLevelPicData + levelpic::kPictureCount) == 50);
    CHECK(x.ds16(kLevelPicData + kLevelPicDataStride + levelpic::kPlaces) == 13);
    CHECK(x.ds16(kLevelXRange + 2 * kLevelXRangeStride + spawnrange::kXMask) == 0x3FF);
    CHECK(x.ds16(kHidingCells) == 0x77);
    CHECK(x.ds16(kVariableCells) == 0x26);
    CHECK(x.ds16(kVariableCellDefaults) == 0x4B);
    CHECK(x.ds8(kCellType + 2) == 12);
    CHECK(x.ds16(kCellPicture + 4) == 0x45);
    CHECK(x.ds16(kCreatureMask + 2) == 0x1FF);
    CHECK(x.ds16(kUserRecOffsets + 2 * 8) == 3 + 8 * 360);
    CHECK(x.ds16(kSoundPort) == 0x388);
    CHECK(x.ds8(kDealTemplate + 1) == 3);
    CHECK(x.ds8(kQuizReward + 6) == 2);
    CHECK(x.ds16(kTitlePhases + 2 * 9) == 0);  // phase 9 ends the title
    CHECK(x.ds8(kTitleBottomZoneCmd) == 13);  // the title page sets 8, story page 2 sets 13 again
    CHECK(x.ds16(kMsgBoxes + boxrec::kY1) == 13);
    for (size_t k = 0; k < kRankNamesCount; ++k) CHECK(x.ds8(static_cast<uint16_t>(kRankNames + k * kRankNamesStride)) != 0);
    CHECK(x.ds8(kPalEga + 16) == 0);  // overscan
}
