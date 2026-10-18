#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ne/ne_image.h"
#include "res/resource_types.h"
#include "test_data.h"
#include "util/error.h"

using namespace tmport;

TEST_CASE("NE: TMTWINCD.EXE headers, segments and DGROUP relocations") {
    TM_REQUIRE_GAME_DATA();
    const ne::NeImage& exe = g->exe->exe();
    CHECK(exe.segmentCount() == 56);
    CHECK(exe.autoDataSegment() == 56);
    CHECK(exe.alignShift() == 9);
    const ne::Segment& dg = exe.segment(56);
    CHECK(dg.fileOffset == 0x23C00u);
    CHECK(dg.length == 0x66A2u);
    CHECK(dg.minAlloc == 0x66A2u);
    CHECK(dg.isData());
    CHECK(dg.hasRelocations());
    const auto relocs = exe.relocations(56);
    CHECK(relocs.size() == 198);
    for (const auto& r : relocs) {
        CHECK(r.addressType == static_cast<uint8_t>(ne::RelocAddr::FarPointer));
        CHECK(r.target.kind == ne::RelocTarget::Kind::Internal);
    }
    // 8 dialog templates (RT_DIALOG = 5) and the three cursors' group entries (RT_GROUP_CURSOR = 12).
    size_t dialogs = 0, groupCursors = 0;
    for (const auto& r : exe.resources()) {
        if (r.integerType() && r.typeId == 5) ++dialogs;
        if (r.integerType() && r.typeId == 12) ++groupCursors;
    }
    CHECK(dialogs == 8);
    CHECK(groupCursors == 3);
    // The EXE's own type-15 table names its 8 dialog templates (integer type 5, named ids).
    REQUIRE(exe.nameTable().size() == 8);
    for (const auto& e : exe.nameTable()) {
        CHECK(e.typeOrdinal == 5);
        CHECK(e.typeName.empty());
        CHECK_FALSE(e.idName.empty());
        CHECK(exe.findResource(5, e.idOrdinal) != nullptr);
    }
}

TEST_CASE("NE: resource tables and type-15 name tables of every DLL") {
    TM_REQUIRE_GAME_DATA();
    struct Expect {
        DllId dll;
        size_t entries;
        std::map<std::string, size_t> perType;
    };
    const Expect expect[] = {
        {DllId::TMT, 557, {{"BNDL", 79}, {"FFNT", 1}, {"OTHR", 476}, {"RRGB", 1}}},
        {DllId::TMT256, 764, {{"PMAP", 764}}},
        {DllId::TMT16, 764, {{"PMAP", 764}}},
        {DllId::TM0SOUND, 42, {{"SSND", 42}}},
        {DllId::TM1SOUND, 203, {{"SSND", 203}}},
        {DllId::TM2SOUND, 400, {{"SSND", 400}}},
        {DllId::TM3SOUND, 400, {{"SSND", 400}}},
        {DllId::TM4SOUND, 400, {{"SSND", 400}}},
        {DllId::TM5SOUND, 400, {{"SSND", 400}}},
        {DllId::TM6SOUND, 400, {{"SSND", 400}}},
        {DllId::TM7SOUND, 400, {{"SSND", 400}}},
        {DllId::TM8SOUND, 216, {{"SSND", 216}}},
    };
    size_t ssnd = 0;
    for (const auto& e : expect) {
        const ne::NeImage* img = g->res->dll(e.dll);
        REQUIRE(img != nullptr);
        INFO(img->name());
        CHECK(img->resourceAlignShift() == 9);
        CHECK(img->nameTable().size() == e.entries);
        std::map<std::string, size_t> counts;
        for (const auto& n : img->nameTable()) {
            counts[n.typeName]++;
            CHECK(n.idName.empty());
            const ne::Resource* r = img->findByTypeName(n.typeName, n.idOrdinal);
            REQUIRE(r != nullptr);  // every name-table entry has its NE resource
            CHECK(r->typeId == n.typeOrdinal);
            CHECK(r->length % 512 == 0);
            CHECK(r->fileOffset + r->length <= img->fileSize());
        }
        CHECK(counts == e.perType);
        // ... and every payload resource is listed (the table itself is type 15).
        size_t payload = 0;
        for (const auto& r : img->resources())
            if (!(r.integerType() && r.typeId == 15)) ++payload;
        CHECK(payload == e.entries);
        ssnd += counts["SSND"];
    }
    CHECK(ssnd == 2861);
    // Type ordinals: BNDL/PMAP/SSND = 32513, FFNT = 32514, OTHR = 32515, RRGB = 32516.
    const ne::NeImage* tmt = g->res->dll(DllId::TMT);
    CHECK(tmt->findByTypeName("BNDL", 300)->typeId == 32513);
    CHECK(tmt->findByTypeName("ffnt", 999)->typeId == 32514);  // case-insensitive
    CHECK(tmt->findByTypeName("OTHR", 10550)->typeId == 32515);
    CHECK(tmt->findByTypeName("RRGB", 900)->typeId == 32516);
    CHECK(tmt->findByTypeName("PMAP", 1000) == nullptr);
}

// ------------------------------------------------------------------ REVIEW_1 L-2 regressions

namespace {

class MemFile final : public vfs::File {
public:
    explicit MemFile(std::vector<uint8_t> d) : d_(std::move(d)) {}
    uint64_t size() const override { return d_.size(); }
    size_t read(uint64_t off, std::span<uint8_t> out) override {
        if (off >= d_.size()) return 0;
        const size_t n = std::min(out.size(), static_cast<size_t>(d_.size() - off));
        std::memcpy(out.data(), d_.data() + off, n);
        return n;
    }

private:
    std::vector<uint8_t> d_;
};

void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = uint8_t(v);
    b[at + 1] = uint8_t(v >> 8);
}

// A minimal synthetic NE file (no game data): MZ stub, NE header at 0x40, no segments, a resource
// table with one resource of integer type `type` at sector `off` (resource shift units) of `len`
// units, everything else zero. `fileSize` bytes in all.
std::shared_ptr<vfs::File> syntheticNe(uint16_t alignShift, uint16_t rsrcShift, uint16_t type, uint16_t off,
                                       uint16_t len, size_t fileSize) {
    std::vector<uint8_t> b(std::max<size_t>(fileSize, 0x100), 0);
    b[0] = 'M';
    b[1] = 'Z';
    put16(b, 0x3C, 0x40);
    const size_t ne = 0x40;
    b[ne] = 'N';
    b[ne + 1] = 'E';
    const uint16_t rsrcTab = 0x40, rsrcLen = 2 + 8 + 12 + 2;
    put16(b, ne + 0x22, rsrcTab);                     // segment table (empty)
    put16(b, ne + 0x24, rsrcTab);                     // resource table
    put16(b, ne + 0x26, uint16_t(rsrcTab + rsrcLen));  // resident names (empty)
    put16(b, ne + 0x28, uint16_t(rsrcTab + rsrcLen));
    put16(b, ne + 0x2A, uint16_t(rsrcTab + rsrcLen));
    put16(b, ne + 0x32, alignShift);
    const size_t r = ne + rsrcTab;
    put16(b, r, rsrcShift);
    put16(b, r + 2, uint16_t(0x8000 | type));
    put16(b, r + 4, 1);
    put16(b, r + 10, off);
    put16(b, r + 12, len);
    put16(b, r + 16, 0x8001);  // id 1
    b.resize(fileSize);
    return std::make_shared<MemFile>(std::move(b));
}

}  // namespace

TEST_CASE("NE (REVIEW_1 L-2): shift counts and resource extents from the file are validated") {
    // A sane file: one type-15 resource in the last 512-byte unit, which lacks its padding.
    {
        const auto img = ne::NeImage::open(syntheticNe(0, 9, 15, 1, 1, 700), "ok.dll");
        CHECK(img.alignShift() == 9);  // 0 means 9
        REQUIRE(img.resources().size() == 1);
        CHECK(img.resources()[0].length == 512);
        CHECK(img.readResource(img.resources()[0]).size() == 512);  // zero-padded
    }
    // Shifts above 15 (UB for >= 32, garbage for 16..31) are refused with the file's name.
    CHECK_THROWS_WITH_AS(ne::NeImage::open(syntheticNe(40, 9, 15, 1, 1, 1024), "bad.dll"),
                         doctest::Contains("bad.dll"), FormatError);
    CHECK_THROWS_AS(ne::NeImage::open(syntheticNe(16, 9, 15, 1, 1, 1024), "bad.dll"), FormatError);
    CHECK_THROWS_AS(ne::NeImage::open(syntheticNe(9, 31, 15, 1, 1, 1024), "bad.dll"), FormatError);
    CHECK_THROWS_AS(ne::NeImage::open(syntheticNe(9, 16, 15, 1, 1, 1024), "bad.dll"), FormatError);
    // A declared length far past the end of the file: refused at open, before the type-15 table
    // is read (0xFFFF << 9 = 32 MB here; up to 0xFFFF << 15 = 2 GB was zero-filled before).
    CHECK_THROWS_WITH_AS(ne::NeImage::open(syntheticNe(9, 9, 15, 1, 0xFFFF, 1024), "big.dll"),
                         doctest::Contains("past the end"), FormatError);
    // Other resource types too, even though they are only read on demand.
    CHECK_THROWS_AS(ne::NeImage::open(syntheticNe(9, 9, 32513, 1, 0x100, 1024), "big.dll"), FormatError);
    CHECK_THROWS_AS(ne::NeImage::open(syntheticNe(9, 15, 32513, 0xFFFF, 1, 1024), "far.dll"), FormatError);
}
