#include <doctest/doctest.h>

#include "mischief/rtl/borland_rand.h"
#include "res/resource_types.h"

using namespace tmport;

TEST_CASE("resource id -> type routing (1038:038a)") {
    CHECK(typeForId(300) == ResType::BNDL);
    CHECK(typeForId(899) == ResType::BNDL);
    CHECK(typeForId(900) == ResType::RRGB);
    CHECK(typeForId(949) == ResType::RRGB);
    CHECK(typeForId(950) == ResType::FFNT);
    CHECK(typeForId(999) == ResType::FFNT);
    CHECK(typeForId(1000) == ResType::PMAP);
    CHECK(typeForId(3999) == ResType::PMAP);
    CHECK(typeForId(4000) == ResType::SSND);
    CHECK(typeForId(9999) == ResType::SSND);
    CHECK(typeForId(10000) == ResType::OTHR);
    CHECK(typeForId(32767) == ResType::OTHR);
    CHECK(typeForId(40000) == ResType::BNDL);  // signed 16-bit compare: negative
}

TEST_CASE("(type, id) -> DLL routing (sys_resources_saves.md §5.3)") {
    using mischief::gfx::ColorMode;
    CHECK(dllFor(ResType::PMAP, 1000, ColorMode::Colour256) == DllId::TMT256);
    CHECK(dllFor(ResType::PMAP, 1000, ColorMode::Colour16) == DllId::TMT16);
    CHECK(dllFor(ResType::BNDL, 300, ColorMode::Colour256) == DllId::TMT);
    CHECK(dllFor(ResType::OTHR, 11000, ColorMode::Colour256) == DllId::TMT);
    CHECK(dllFor(ResType::RRGB, 900, ColorMode::Colour256) == DllId::TMT);
    CHECK(dllFor(ResType::FFNT, 999, ColorMode::Colour256) == DllId::TMT);
    CHECK(dllFor(ResType::SSND, 7000, ColorMode::Colour256) == DllId::TM0SOUND);
    CHECK(dllFor(ResType::SSND, 7099, ColorMode::Colour256) == DllId::TM0SOUND);
    CHECK(dllFor(ResType::SSND, 7100, ColorMode::Colour256) == DllId::TM1SOUND);
    CHECK(dllFor(ResType::SSND, 7999, ColorMode::Colour256) == DllId::TM1SOUND);
    CHECK(dllFor(ResType::SSND, 4000, ColorMode::Colour256) == DllId::TM2SOUND);
    CHECK(dllFor(ResType::SSND, 4399, ColorMode::Colour256) == DllId::TM2SOUND);
    CHECK(dllFor(ResType::SSND, 4400, ColorMode::Colour256) == DllId::TM3SOUND);
    CHECK(dllFor(ResType::SSND, 5999, ColorMode::Colour256) == DllId::TM6SOUND);
    CHECK(dllFor(ResType::SSND, 6000, ColorMode::Colour256) == DllId::TM7SOUND);
    CHECK(dllFor(ResType::SSND, 6799, ColorMode::Colour256) == DllId::TM8SOUND);
    CHECK_FALSE(dllFor(ResType::SSND, 6800, ColorMode::Colour256).has_value());
    CHECK_FALSE(dllFor(ResType::SSND, 6999, ColorMode::Colour256).has_value());
    CHECK_FALSE(dllFor(ResType::SSND, 8000, ColorMode::Colour256).has_value());
    CHECK_FALSE(dllFor(ResType::SSND, 9999, ColorMode::Colour256).has_value());
}
