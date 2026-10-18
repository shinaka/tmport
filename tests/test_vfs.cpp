#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <random>
#include <set>
#include <string_view>
#include <vector>

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

// ------------------------------------------------------------------ REVIEW_1 L-2 regression

namespace {

void le32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
void be32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * (3 - i)));
}

// One ISO 9660 directory record; returns its length.
size_t dirRecord(uint8_t* at, uint32_t lba, uint32_t size, bool dir, std::string_view name) {
    const size_t len = 33 + name.size() + ((33 + name.size()) % 2);
    at[0] = uint8_t(len);
    le32(at + 2, lba);
    be32(at + 6, lba);
    le32(at + 10, size);
    be32(at + 14, size);
    at[25] = dir ? 2 : 0;
    at[28] = 1;
    at[32] = uint8_t(name.size());
    std::memcpy(at + 33, name.data(), name.size());
    return len;
}

}  // namespace

TEST_CASE("CD image (REVIEW_1 L-2): sizes in directory records are clamped to the image") {
    // A synthetic 21-sector ISO (no game data): PVD at 16, root at 18, TMTWINCD\ at 19, and a
    // TMTWINCD.EXE at 20 whose record claims 0xFFFFFFF0 bytes.
    std::vector<uint8_t> iso(21 * 2048, 0);
    auto sec = [&](int lba) { return iso.data() + static_cast<size_t>(lba) * 2048; };
    uint8_t* pvd = sec(16);
    pvd[0] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    dirRecord(pvd + 156, 18, 2048, true, std::string_view("\0", 1));
    uint8_t* term = sec(17);
    term[0] = 255;
    std::memcpy(term + 1, "CD001", 5);
    size_t o = 0;
    o += dirRecord(sec(18) + o, 18, 2048, true, std::string_view("\0", 1));
    o += dirRecord(sec(18) + o, 18, 2048, true, std::string_view("\1", 1));
    dirRecord(sec(18) + o, 19, 2048, true, "TMTWINCD");
    o = 0;
    o += dirRecord(sec(19) + o, 19, 2048, true, std::string_view("\0", 1));
    o += dirRecord(sec(19) + o, 18, 2048, true, std::string_view("\1", 1));
    dirRecord(sec(19) + o, 20, 0xFFFFFFF0u, false, "TMTWINCD.EXE;1");
    std::memcpy(sec(20), "MZ synthetic", 12);
    std::random_device rd;
    const auto dir = std::filesystem::temp_directory_path() / ("tmport_iso_" + std::to_string(rd()));
    const auto path = dir / "synthetic.iso";
    REQUIRE(writeWholeFile(path, iso));
    {
        vfs::CdImageSource src(path);
        CHECK(src.gameFolder() == "/TMTWINCD");
        const auto f = src.open("TMTWINCD.EXE");
        REQUIRE(f);
        CHECK(f->size() == 2048);  // what the image holds from sector 20 on, not 4 GB
        const auto all = f->readAll();
        CHECK(all.size() == 2048);
        CHECK(std::memcmp(all.data(), "MZ synthetic", 12) == 0);
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
