#include <doctest/doctest.h>

#include <cstring>

#include "res/resource_manager.h"
#include "test_data.h"
#include "util/hash.h"

using namespace tmport;

namespace {

// Expected values computed with tools/assets/bndl.py (count and FNV-1a 64 of the member ids as
// u16 little-endian), see extracted/scratch/engine/gen_expect.py. Hashes only, no game data.
struct BndlExpect {
    int id;
    int count;
    uint64_t hash;
};
const BndlExpect kBundles[] = {
    {300, 69, 0x88c326206903a219ull}, {301, 10, 0xaa3a96dc86fcd478ull}, {302, 6, 0xc41e08c6134cd180ull},
    {310, 9, 0xeadccb3d03b9d75bull},  {311, 6, 0x186f38e42576b5faull},  {312, 5, 0xb171e8b599640bf8ull},
    {313, 2, 0xdbc29af93e9c9588ull},  {314, 5, 0x5aca3aa091a94631ull},  {315, 8, 0x29e7f3bed76d1435ull},
    {316, 8, 0xd2d6fc54cd0acd65ull},  {317, 8, 0xee2ba2529a0157b5ull},  {318, 5, 0x54443d31623209a4ull},
    {319, 4, 0x5eee69e118953375ull},  {320, 9, 0x0e6ce857c0159217ull},  {321, 24, 0xe62f25f7da431246ull},
    {322, 4, 0xcf785f65edce4e15ull},  {323, 67, 0xd71ccc7f9f0a18ddull}, {324, 53, 0xf5e62ca0e5f5f49aull},
    {325, 8, 0xc45892efb6366c66ull},  {326, 4, 0xa9793291aa8af2a9ull},  {327, 33, 0xe5daac60c7ee42c6ull},
    {328, 4, 0x0cd145642f23f5e9ull},  {330, 2, 0xde9cfb7ea4ad5508ull},  {331, 2, 0x40658b8f6405c4e8ull},
    {332, 2, 0x18607be2875c1228ull},  {333, 2, 0x78760bf34542ed88ull},  {334, 2, 0xde8d8bc1567252d8ull},
    {335, 2, 0x40561bd215cac2b8ull},  {336, 2, 0x14eb0c25363de6f8ull},  {337, 2, 0x78669c35f707eb58ull},
    {338, 2, 0xdaab5c0404f7c288ull},  {339, 2, 0x3c73ec14c4503268ull},  {340, 2, 0x146edc67e7a67fa8ull},
    {341, 2, 0x7b506c78ab53ad08ull},  {342, 2, 0xda2f2c46b6605b38ull},  {343, 2, 0x4110bc577a0d8898ull},
    {344, 2, 0x1603aec029616598ull},  {345, 2, 0x76f2bed0e8010b38ull},  {346, 2, 0xdb573e9ef7bedc08ull},
    {347, 4, 0x741ea57ad83afbc5ull},  {348, 4, 0x053496a22e14deddull},  {349, 4, 0x07f8f175d98ee935ull},
    {350, 4, 0xc4c76879120e417dull},  {351, 2, 0x3f20af351c6f4128ull},  {352, 2, 0x15689f883e53f9e8ull},
    {353, 2, 0x780aaf98fe653408ull},  {354, 2, 0xdb28ef670d0dd578ull},  {355, 2, 0x3dcaff77cd1f0f98ull},
    {356, 2, 0x171aedb563063898ull},  {357, 2, 0x78e37dc6225ea878ull},  {358, 2, 0xdc6e7d943163af08ull},
    {359, 2, 0x3e370da4f0bc1ee8ull},  {360, 2, 0x147efdf812a0d7a8ull},  {361, 2, 0x76478e08d1f94788ull},
    {362, 2, 0xdaac0dd6e1b71858ull},  {363, 2, 0x3c749de7a10f8838ull},  {364, 2, 0x12bc8e3ac2f440f8ull},
    {365, 2, 0x7b511e4b881302d8ull},  {366, 3, 0xd5f797a1f1224d94ull},  {367, 3, 0x711af68bee909eaeull},
    {368, 3, 0x900390b2e9bcf81full},  {369, 3, 0x27c7bcd917950f2full},  {370, 3, 0x85fdbc8b4f3ce866ull},
    {371, 3, 0x018b8281a0a75cedull},  {372, 3, 0x3ea3a1024116059cull},  {373, 3, 0x7f90a71a12b9ec7bull},
    {374, 3, 0x1348f39f12268aeaull},  {375, 3, 0x58678c8190ff6349ull},  {376, 4, 0xc32f08311190b9adull},
    {377, 4, 0xf1d98683f030a135ull},  {378, 4, 0xd704ae9f484272cdull},  {379, 4, 0xb350055c73606a85ull},
    {380, 4, 0x2bbbd2cac2c53e8dull},  {400, 11, 0x4ebeaaa1336be5ccull}, {401, 20, 0xa106edbe96eee8bcull},
    {402, 41, 0xda2e34174572f995ull}, {410, 6, 0x872f8cb2f857f5ccull},  {412, 11, 0xdb96e28aeee7086cull},
    {415, 3, 0xf7405e54eab1058dull},
};

struct SsndExpect {
    int id;
    uint32_t rounded;
    uint32_t samples;
    uint64_t pcmHash;
};
const SsndExpect kSounds[] = {
    {7000, 34816, 34772, 0x12159feb14528b8cull},  {7041, 40448, 40340, 0x84fb8710d79eb564ull},
    {7100, 208384, 208038, 0xcd24e93419c4e4b0ull}, {4000, 71680, 71446, 0xf90a0c2c224b2640ull},
    {6615, 14336, 14002, 0x26664431f566da13ull},
};

}  // namespace

TEST_CASE("Resources: BNDL member lists match tools/assets/bndl.py") {
    TM_REQUIRE_GAME_DATA();
    const auto ids = g->res->ids(ResType::BNDL, DllId::TMT);
    CHECK(ids.size() == std::size(kBundles));
    size_t pmapRefs = 0, othrRefs = 0;
    for (const auto& e : kBundles) {
        INFO("BNDL " << e.id);
        auto b = g->res->bundle(e.id);
        REQUIRE(b.has_value());
        CHECK(static_cast<int>(b->ids.size()) == e.count);
        uint64_t h = kFnv64Offset;
        for (int m : b->ids) {
            h = fnv1a64U16(static_cast<uint16_t>(m), h);
            if (typeForId(m) == ResType::PMAP) ++pmapRefs;
            if (typeForId(m) == ResType::OTHR) ++othrRefs;
        }
        CHECK(h == e.hash);
        for (uint32_t raw : b->rawIds) CHECK((raw >> 16) == 0u);
    }
    CHECK(pmapRefs == 532);
    CHECK(othrRefs == 41);
}

TEST_CASE("Resources: GetResource expands BNDLs recursively with member sizes") {
    TM_REQUIRE_GAME_DATA();
    auto t = g->res->getResource(328);  // fountain frames 1380..1383 (ARCHITECTURE.md §7.1 C1)
    REQUIRE(t.has_value());
    CHECK(t->isBundle);
    CHECK(t->type == ResType::BNDL);
    CHECK(t->memberIds == std::vector<int>{1380, 1381, 1382, 1383});
    REQUIRE(t->members.size() == 4);
    for (size_t i = 0; i < 4; ++i) {
        CHECK(t->members[i].type == ResType::PMAP);
        CHECK(t->members[i].bytes.size() == t->memberSizes[i]);
        CHECK(t->memberSizes[i] == g->res->roundedSize(ResType::PMAP, t->memberIds[i]));
        CHECK(t->memberSizes[i] % 512 == 0);
    }
    // Res_GetSize doubles BNDL sizes.
    CHECK(g->res->gameSize(328) == 2 * g->res->roundedSize(ResType::BNDL, 328));
    // A bundle with an animation script: BNDL 323 = OTHR 20300 + frames.
    auto c = g->res->getResource(323);
    REQUIRE(c.has_value());
    CHECK(c->memberIds.front() == 20300);
    CHECK(c->members.front().type == ResType::OTHR);
    CHECK(std::memcmp(c->members.front().bytes.data(), "DAM\x1A", 4) == 0);
    // A non-bundle resource is returned as is.
    auto p = g->res->getResource(1000);
    REQUIRE(p.has_value());
    CHECK_FALSE(p->isBundle);
    CHECK(p->members.empty());
}

TEST_CASE("Resources: RRGB, FFNT, OTHR raw bodies keep the rounded size") {
    TM_REQUIRE_GAME_DATA();
    auto rrgb = g->res->loadRaw(900);
    REQUIRE(rrgb.has_value());
    CHECK(rrgb->size() == 1024);  // GetResourceInto copies 1024 bytes into DS:3d90
    CHECK(g->res->gameSize(900) == 1024);
    CHECK(fnv1a64(std::span<const uint8_t>(rrgb->data(), 768)) == 0xc31b8c96caee1176ull);
    const gfx::Palette pal = g->res->palette();
    CHECK(pal.keyTable().count() == 2);
    CHECK(pal.keyTable()[15]);
    CHECK(pal.keyTable()[241]);
    CHECK(pal.whiteKeyTable()[0]);
    CHECK(pal.whiteKeyTable()[77]);

    auto font = g->res->loadRaw(999);
    REQUIRE(font.has_value());
    CHECK((*font)[0] == 0);
    CHECK((*font)[2] == 17);  // glyph height
    CHECK(font->size() % 512 == 0);

    auto othr = g->res->loadRaw(20300);
    REQUIRE(othr.has_value());
    CHECK(std::memcmp(othr->data(), "DAM\x1A", 4) == 0);
    CHECK_FALSE(g->res->loadRaw(9999).has_value());   // SSND id no DLL serves
    CHECK_FALSE(g->res->loadRaw(6800).has_value());
    CHECK_FALSE(g->res->loadRaw(2999).has_value());   // PMAP id that does not exist
}

TEST_CASE("Resources: SSND headers") {
    TM_REQUIRE_GAME_DATA();
    for (const auto& e : kSounds) {
        INFO("SSND " << e.id);
        auto raw = g->res->loadRaw(e.id);
        REQUIRE(raw.has_value());
        CHECK(raw->size() == e.rounded);
        auto s = parseSsnd(*raw);
        REQUIRE(s.has_value());
        CHECK(s->reserved == 0u);
        CHECK(s->sampleRate == 22050u);
        CHECK(s->sampleCount == e.samples);
        CHECK_FALSE(s->truncated);
        CHECK(fnv1a64(s->pcm(*raw)) == e.pcmHash);
    }
    size_t n = 0;
    for (int k = 0; k < 9; ++k)
        for (int id : g->res->ids(ResType::SSND, soundDll(k))) {
            auto s = g->res->soundInfo(id);
            REQUIRE(s.has_value());
            CHECK(s->sampleRate == 22050u);
            CHECK_FALSE(s->truncated);
            ++n;
        }
    CHECK(n == 2861);
}
