#include <doctest/doctest.h>

#include <set>
#include <string>

#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "test_data.h"

using namespace tmport;

TEST_CASE("ExeTables: validation markers and relocations") {
    TM_REQUIRE_GAME_DATA();
    const ExeTables& t = *g->exe;
    const auto v = t.validate();
    for (const auto& m : v.messages) MESSAGE(m);
    CHECK(v.ok);
    CHECK(t.size() == dg::kLength);
    CHECK(t.cstr(dg::kUsersMagic).size() == 4);
    CHECK(t.farPointers().size() == 198);
    size_t intoDg = 0;
    for (const auto& [site, p] : t.farPointers()) {
        // In the file every relocated far pointer holds the chain placeholder FF FF 00 00 or the
        // next link of its chain; the raw selector word is always 0.
        CHECK(t.u16(site + 2u) == 0);
        if (p.kind == FarPtr::Kind::DGroup) ++intoDg;
    }
    CHECK(intoDg == 171);
    // BSS is zero in the file.
    for (uint32_t o = dg::kBssStart; o < dg::kLength; ++o)
        if (t.u8(o) != 0) FAIL("non-zero BSS byte at " << o);
}

TEST_CASE("ExeTables: far pointers resolve to DGROUP strings and code") {
    TM_REQUIRE_GAME_DATA();
    const ExeTables& t = *g->exe;
    // Clue-word table element 0 points to the empty string at 1141 (dgroup_map.md §4.10).
    CHECK(t.dgPtr(dg::kStrNoun) == uint16_t{0x1141});
    CHECK(t.farCstr(dg::kStrNoun).empty());
    CHECK(t.dgPtr(dg::kStrNoun + 4).has_value());
    CHECK_FALSE(t.farCstr(dg::kStrNoun + 4).empty());
    // MIDI file names (§4.6) and rank names (§4.9).
    CHECK(t.dgPtr(dg::kMusicFiles) == uint16_t{0x0A56});
    CHECK(t.dgPtr(dg::kRankNames) == uint16_t{0x101D});
    // File menu handlers point into code: 10f0:0487 = NE segment 31.
    const FarPtr h = t.farPtr(0x1338);
    CHECK(h.kind == FarPtr::Kind::Segment);
    CHECK(h.segment == 31);
    CHECK(h.selector() == 0x10F0);
    CHECK(h.offset == 0x0487);
    CHECK_FALSE(t.dgPtr(0x1338).has_value());
    // g_menuDef (09ce) is NULL in the image.
    CHECK(t.farPtr(0x09CE).isNull());
    // The relocation-applied image holds offset:selector at every relocated site.
    const auto pat = t.patched();
    CHECK(pat[dg::kStrNoun] == 0x41);
    CHECK(pat[dg::kStrNoun + 1] == 0x11);
    CHECK(pat[dg::kStrNoun + 2] == 0xB8);
    CHECK(pat[dg::kStrNoun + 3] == 0x11);
    CHECK(pat[0x1338 + 2] == 0xF0);
    CHECK(pat[0x1338 + 3] == 0x10);
    CHECK(pat[0x09CE] == 0);
    CHECK(pat[dg::kUsersMagic] == 'U');
    // Elf behaviour handlers 1178:0a6d.. (segment 48).
    const FarPtr e = t.farPtr(dg::kElfProjY + 0x12);  // 2c8f
    CHECK(e.kind == FarPtr::Kind::Segment);
    CHECK(e.selector() == 0x1178);
    CHECK(e.offset == 0x0A6D);
    // Plain typed reads.
    CHECK(t.u16(dg::kMusicIds) == 9000);
    CHECK(t.u16(dg::kMusicIds + 18) == 9009);
    CHECK(t.u32(dg::kRankThresholds + 28) == 300u);
    CHECK(t.u32(dg::kRandSeed) == 1u);
    CHECK(t.array<uint8_t, 4>(dg::kGemsNeeded) == std::array<uint8_t, 4>{2, 3, 4, 5});
    CHECK_THROWS(t.u16(dg::kLength - 1));
}

TEST_CASE("ExeTables: RoomDef table sanity (DS:23d8, 50 x 0x1c)") {
    TM_REQUIRE_GAME_DATA();
    const auto rooms = g->exe->rooms();
    for (size_t i = 0; i < rooms.size(); ++i) {
        const RoomDef& r = rooms[i];
        INFO("room " << i);
        CHECK(r.startY == 142);
        CHECK(r.unused09 == 0xFF);
        CHECK(r.unused0D == 1);
        CHECK(r.unused10 == -100);
        CHECK(r.typeDoor == 1);
        CHECK(r.slotObject == std::array<uint8_t, 3>{0, 0, 0});
        CHECK(r.slotContent == std::array<uint8_t, 3>{0, 0, 0});
        CHECK(g->res->exists(r.bgPmap));
        const int expectLevel = i < 23 ? 0 : i < 36 ? 1 : i < 49 ? 2 : 3;
        CHECK(r.level == expectLevel);
        const int expectMusic = i < 9 ? 0 : i < 23 ? 1 : i < 36 ? 3 : i < 49 ? 5 : 7;
        CHECK(r.music == expectMusic);
        CHECK((r.exitLeft == 0xFF || r.exitLeft < 50));
        CHECK((r.exitRight == 0xFF || r.exitRight < 50));
    }
    // Room 9 (dgroup_map.md §4.16).
    CHECK(rooms[9].startX == 233);
    CHECK(rooms[9].startFrame == 60);
    CHECK(rooms[9].exitLeft == 16);
    CHECK(rooms[9].exitRight == 10);
    CHECK(rooms[9].exitDoor == 0);
    CHECK(rooms[9].doorX == 270);
    CHECK(rooms[9].bgPmap == 1003);
    CHECK(rooms[49].bgPmap == 1040);
    CHECK(rooms[0].bgPmap == 1036);
    // The level rings: 9 -> 10 -> ... -> 16 -> 9, 23 -> ... -> 29 -> 23, 36 -> ... -> 42 -> 36.
    for (auto [first, last] : {std::pair{9, 16}, std::pair{23, 29}, std::pair{36, 42}}) {
        int r = first;
        std::set<int> seen;
        for (int k = 0; k <= last - first; ++k) {
            seen.insert(r);
            r = rooms[static_cast<size_t>(r)].exitRight;
        }
        CHECK(r == first);
        CHECK(static_cast<int>(seen.size()) == last - first + 1);
    }
    // Gates are not exits: no exit leads from a higher level to a lower one.
    for (size_t i = 9; i < 50; ++i)
        for (uint8_t x : {rooms[i].exitLeft, rooms[i].exitRight, rooms[i].exitDoor})
            if (x != 0xFF && x >= 9) CHECK(rooms[x].level >= rooms[i].level);
}

TEST_CASE("ExeTables: EGA pen colours and 16-colour index table") {
    TM_REQUIRE_GAME_DATA();
    const auto ega = g->exe->egaColours();
    CHECK(ega[0] == Rgb8{0, 0, 0});
    CHECK(ega[1] == Rgb8{0, 0, 0xAA});
    CHECK(ega[6] == Rgb8{0xAA, 0x55, 0});
    CHECK(ega[7] == Rgb8{0xC0, 0xC0, 0xC0});
    CHECK(ega[8] == Rgb8{0x55, 0x55, 0x55});
    CHECK(ega[14] == Rgb8{0xFF, 0xFF, 0x55});
    CHECK(ega[15] == Rgb8{0xFF, 0xFF, 0xFF});
    CHECK(g->exe->vga16Index() == std::array<uint8_t, 16>{0, 4, 2, 6, 1, 5, 1, 8, 7, 12, 10, 14, 9, 13, 11, 15});
    // Pens in 256 colours: nearest RRGB index; black is logical 1, white logical 0.
    const gfx::Palette p = g->makePalette(gfx::ColorMode::Colour256);
    CHECK(p.pen(0) == 1);
    CHECK(p.pen(15) == 0);
    CHECK(p.colour(p.pen(14)) == p.colour(p.nearestIndex({0xFF, 0xFF, 0x55})));
}
