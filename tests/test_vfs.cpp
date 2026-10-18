#include <doctest/doctest.h>

#include <filesystem>
#include <set>

#include "test_data.h"
#include "util/file_util.h"
#include "util/hash.h"
#include "vfs/cd_image_source.h"
#include "vfs/data_source.h"
#include "vfs/directory_source.h"

using namespace tmport;

TEST_CASE("Data source: case-insensitive lookup") {
    TM_REQUIRE_GAME_DATA();
    const vfs::DataSource& src = *g->source;
    REQUIRE(src.open("TMTWINCD.EXE") != nullptr);
    CHECK(src.open("tmtwincd.exe") != nullptr);
    CHECK(src.open("Tmt.Dll") != nullptr);
    CHECK(src.open("sounds/eopen1.mid") != nullptr);
    CHECK(src.open("SOUNDS\\BOPEN1.MID") != nullptr);
    CHECK(src.open("NOSUCH.DLL") == nullptr);
    CHECK(src.open("SOUNDS") == nullptr);  // a directory is not a file
    const auto list = src.list("sounds");
    size_t mids = 0;
    for (const auto& e : list)
        if (!e.isDirectory && toUpperAscii(e.name).ends_with(".MID")) ++mids;
    CHECK(mids == 20);
    // Random access reads.
    auto f = src.open("TMT.DLL");
    const auto all = f->readAll();
    REQUIRE(all.size() == f->size());
    CHECK(all[0] == 'M');
    CHECK(all[1] == 'Z');
    const auto mid = f->readRange(1000, 4000);
    CHECK(std::equal(mid.begin(), mid.end(), all.begin() + 1000));
    CHECK(f->readRange(all.size() - 3, 100).size() == 3);
    CHECK(f->readRange(all.size() + 5, 10).empty());
}

TEST_CASE("CD image source returns the same bytes as the extracted directory") {
    const auto dir = testdata::dataPath();
    const auto img = testdata::imagePath();
    if (!dir || !img) {
        MESSAGE("TM_DATA (directory) and TM_IMAGE (CD image) are both needed: test skipped");
        return;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(pathFromUtf8(*dir), ec)) {
        MESSAGE("TM_DATA is not a directory: test skipped");
        return;
    }
    auto dsrc = vfs::openDataSource(pathFromUtf8(*dir));
    auto isrc = vfs::openDataSource(pathFromUtf8(*img));
    auto* cd = dynamic_cast<vfs::CdImageSource*>(isrc.get());
    REQUIRE(cd != nullptr);
    CHECK(cd->gameFolder() == "/TMTWINCD");
    for (const char* name : {"TMTWINCD.EXE", "TMT.DLL", "TMT256.DLL", "SOUNDS/EOPEN1.MID"}) {
        INFO(name);
        auto a = dsrc->open(name);
        auto b = isrc->open(name);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK(a->size() == b->size());
        const auto da = a->readAll();
        const auto db = b->readAll();
        CHECK(da.size() == db.size());
        CHECK(fnv1a64(da) == fnv1a64(db));
        CHECK(da == db);
        // Unaligned random access across sector boundaries.
        const auto ra = a->readRange(2047, 5000);
        const auto rb = b->readRange(2047, 5000);
        CHECK(ra == rb);
    }
    std::set<std::string> namesDir, namesImg;
    for (const auto& e : dsrc->list("SOUNDS")) namesDir.insert(toUpperAscii(e.name));
    for (const auto& e : isrc->list("SOUNDS")) namesImg.insert(toUpperAscii(e.name));
    CHECK(namesDir == namesImg);
}
