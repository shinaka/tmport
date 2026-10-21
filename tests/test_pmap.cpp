#include <doctest/doctest.h>

#include <algorithm>

#include "gfx/pixmap.h"
#include "mischief/util/hash.h"
#include "res/resource_manager.h"
#include "test_data.h"

using namespace tmport;

namespace {

// Expected values computed with tools/assets/pmap.py (FNV-1a 64 of the decoded w*h index bytes),
// see extracted/scratch/engine/gen_expect.py. Hashes only, no pixels.
struct PixExpect {
    int id, w, h;
    uint64_t hash;
};
const PixExpect k256[] = {
    {1000, 512, 364, 0x9a540184d1b0f031ull}, {1003, 512, 268, 0xb3276159b54676f7ull},
    {1090, 68, 267, 0x4471b1c2e4d3dc75ull},  {1095, 83, 89, 0x33d259c93bbb287dull},
    {1100, 60, 122, 0x96f954cf030f78fdull},  {1306, 502, 89, 0x755ccfbc325e902full},
    {1380, 107, 108, 0x5064bfd31c2c1970ull}, {1383, 106, 108, 0x22b26a2b2874cd91ull},
    {1400, 118, 133, 0x14dffd68b07b27c2ull}, {1511, 118, 133, 0x8ffb26beba87bd82ull},
    {1600, 10, 2, 0xe57cce5abd644c75ull},    {1609, 48, 58, 0xc70974eefe750148ull},
    {1699, 52, 25, 0xa1b59bc21ebd1415ull},   {1700, 71, 51, 0x053222df26681eaeull},
    {2000, 512, 267, 0x961b9f6a78fc3d08ull}, {2100, 512, 268, 0x9b0bc9f0f1fb362full},
    {2183, 512, 96, 0x590f72549923a527ull},  {2448, 133, 82, 0xfb5e6a6ecb90be7full},
};
const PixExpect k16[] = {
    {1000, 512, 364, 0xbbb1cc64c70686d5ull}, {1003, 512, 268, 0x9ae76922cd713cf4ull},
    {1090, 68, 267, 0x04ca6094283cf809ull},  {1095, 83, 89, 0x6f75959f8b093d4cull},
    {1100, 60, 122, 0x5aba56ce4677d4e8ull},  {1306, 502, 89, 0x75a2d7739ad21b11ull},
    {1380, 107, 108, 0x0d66e623b7a1eea0ull}, {1383, 106, 108, 0x9f4de9f7fc6aae3bull},
    {1400, 118, 133, 0x060c4afb00394f48ull}, {1511, 118, 133, 0xd47352c134cf7cf5ull},
    {1600, 10, 2, 0x635df7caf4b26258ull},    {1609, 48, 58, 0x092099dcdc8602bbull},
    {1699, 52, 25, 0xf1c01620f6e04197ull},   {1700, 71, 51, 0xc0d71ea58eed775aull},
    {2000, 512, 267, 0x9623b9744126cc10ull}, {2100, 512, 268, 0x2a476550953e059bull},
    {2183, 512, 96, 0xba65d1520a80c6e7ull},  {2448, 133, 82, 0x784be990783db886ull},
};
constexpr uint64_t kAll256 = 0xea2670dd62c34093ull;  // all 764, id order: (id, w, h, pixels)
constexpr uint64_t kAll16 = 0x2ba65138a68b7f2bull;

void checkSet(const ResourceManager& res, mischief::gfx::ColorMode mode, std::span<const PixExpect> set) {
    for (const auto& e : set) {
        INFO("PMAP " << e.id);
        auto pm = res.pixmap(e.id, mode);
        REQUIRE(pm != nullptr);
        CHECK(pm->width == e.w);
        CHECK(pm->height == e.h);
        CHECK(mischief::fnv1a64(pm->pixels) == e.hash);
    }
}

uint64_t combined(const ResourceManager& res, mischief::gfx::ColorMode mode, DllId dll, size_t& count) {
    auto ids = res.ids(ResType::PMAP, dll);
    std::sort(ids.begin(), ids.end());
    uint64_t h = mischief::kFnv64Offset;
    count = 0;
    for (int id : ids) {
        auto pm = res.pixmap(id, mode);
        h = mischief::fnv1a64U16(static_cast<uint16_t>(id), h);
        h = mischief::fnv1a64U16(static_cast<uint16_t>(pm->width), h);
        h = mischief::fnv1a64U16(static_cast<uint16_t>(pm->height), h);
        h = mischief::fnv1a64(pm->pixels, h);
        ++count;
    }
    return h;
}

}  // namespace

TEST_CASE("PMAP: pixel hashes match tools/assets/pmap.py (TMT256 and TMT16)") {
    TM_REQUIRE_GAME_DATA();
    checkSet(*g->res, mischief::gfx::ColorMode::Colour256, k256);
    checkSet(*g->res, mischief::gfx::ColorMode::Colour16, k16);
    size_t n = 0;
    CHECK(combined(*g->res, mischief::gfx::ColorMode::Colour256, DllId::TMT256, n) == kAll256);
    CHECK(n == 764);
    CHECK(combined(*g->res, mischief::gfx::ColorMode::Colour16, DllId::TMT16, n) == kAll16);
    CHECK(n == 764);
    g->res->clearCache();
}

TEST_CASE("PMAP: RUND decode -> re-encode is byte-identical for every RUND image") {
    TM_REQUIRE_GAME_DATA();
    size_t rund = 0, raw8 = 0;
    for (int id : g->res->ids(ResType::PMAP, DllId::TMT256)) {
        INFO("PMAP " << id);
        auto body = g->res->loadRaw(ResType::PMAP, id, mischief::gfx::ColorMode::Colour256);
        REQUIRE(body.has_value());
        const gfx::PixMap pm = gfx::decodePmap(*body, 8);
        if (pm.encoding == gfx::PixEncoding::Raw8) {
            ++raw8;
            CHECK(id >= 1380);
            CHECK(id <= 1383);
            continue;
        }
        REQUIRE(pm.encoding == gfx::PixEncoding::Rund);
        ++rund;
        const auto enc = gfx::rundCompress(pm.pixels, pm.width, pm.height);
        CHECK(enc.size() == pm.consumed);
        CHECK(std::equal(enc.begin(), enc.end(), body->begin()));
        // The NE padding after the stream is zero in every shipped file.
        CHECK(body->size() - pm.consumed < 512);
    }
    CHECK(rund == 760);
    CHECK(raw8 == 4);
}

TEST_CASE("PMAP: TMT16 and TMT256 have the same ids and sizes") {
    TM_REQUIRE_GAME_DATA();
    auto a = g->res->ids(ResType::PMAP, DllId::TMT256);
    auto b = g->res->ids(ResType::PMAP, DllId::TMT16);
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    CHECK(a == b);
    for (int id : a) {
        auto p8 = g->res->pixmap(id, mischief::gfx::ColorMode::Colour256);
        auto p4 = g->res->pixmap(id, mischief::gfx::ColorMode::Colour16);
        CHECK(p8->width == p4->width);
        CHECK(p8->height == p4->height);
        CHECK(*std::max_element(p4->pixels.begin(), p4->pixels.end()) <= 15);
    }
    g->res->clearCache();
}
