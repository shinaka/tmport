#include <doctest/doctest.h>

#include <map>
#include <string>

#include "res/resource_types.h"
#include "test_data.h"

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
