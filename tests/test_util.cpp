#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "util/byte_reader.h"
#include "util/error.h"
#include "util/png_writer.h"
#include "vfs/data_source.h"

using namespace tmport;

namespace {

std::span<const uint8_t> bytesOf(const char* s) { return {reinterpret_cast<const uint8_t*>(s), std::strlen(s)}; }

uint32_t be32(const std::vector<uint8_t>& d, size_t o) {
    return (uint32_t(d[o]) << 24) | (uint32_t(d[o + 1]) << 16) | (uint32_t(d[o + 2]) << 8) | d[o + 3];
}

struct Chunk {
    std::string type;
    std::vector<uint8_t> body;
};

// Splits a PNG into chunks, checking every CRC.
std::vector<Chunk> chunks(const std::vector<uint8_t>& png) {
    std::vector<Chunk> out;
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    REQUIRE(png.size() > 8);
    REQUIRE(std::memcmp(png.data(), sig, 8) == 0);
    size_t p = 8;
    while (p + 12 <= png.size()) {
        const uint32_t len = be32(png, p);
        Chunk c;
        c.type.assign(png.begin() + static_cast<long>(p + 4), png.begin() + static_cast<long>(p + 8));
        c.body.assign(png.begin() + static_cast<long>(p + 8), png.begin() + static_cast<long>(p + 8 + len));
        const uint32_t crc = be32(png, p + 8 + len);
        CHECK(crc == png::crc32(std::span<const uint8_t>(png.data() + p + 4, len + 4)));
        out.push_back(std::move(c));
        p += 12 + len;
    }
    CHECK(p == png.size());
    return out;
}

// Inflates a zlib stream made of stored blocks (what the writer produces).
std::vector<uint8_t> inflateStored(const std::vector<uint8_t>& z) {
    REQUIRE(z.size() >= 6);
    CHECK(((z[0] << 8) | z[1]) % 31 == 0);
    CHECK((z[0] & 0x0F) == 8);
    std::vector<uint8_t> out;
    size_t p = 2;
    for (;;) {
        const uint8_t hdr = z[p++];
        REQUIRE((hdr & 0x06) == 0);  // BTYPE = stored
        const uint16_t len = static_cast<uint16_t>(z[p] | (z[p + 1] << 8));
        const uint16_t nlen = static_cast<uint16_t>(z[p + 2] | (z[p + 3] << 8));
        CHECK(static_cast<uint16_t>(~len) == nlen);
        p += 4;
        out.insert(out.end(), z.begin() + static_cast<long>(p), z.begin() + static_cast<long>(p + len));
        p += len;
        if (hdr & 1) break;
    }
    CHECK(be32(z, p) == png::adler32(out));
    CHECK(p + 4 == z.size());
    return out;
}

}  // namespace

TEST_CASE("crc32 and adler32 known vectors") {
    CHECK(png::crc32(bytesOf("123456789")) == 0xCBF43926u);
    CHECK(png::adler32(bytesOf("Wikipedia")) == 0x11E60398u);
    CHECK(png::adler32({}) == 1u);
    // Long input exercises the deferred modulo.
    std::vector<uint8_t> big(100000, 0xFF);
    uint32_t a = 1, b = 0;
    for (uint8_t v : big) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    CHECK(png::adler32(big) == ((b << 16) | a));
}

TEST_CASE("PNG writer: RGBA image structure and stored deflate") {
    const int w = 300, h = 120;  // > 65535 bytes of scanlines: several stored blocks
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = static_cast<uint8_t>(i * 7);
    const auto file = png::encodeRgba(w, h, rgba);
    const auto cs = chunks(file);
    REQUIRE(cs.size() == 3);
    CHECK(cs[0].type == "IHDR");
    CHECK(be32(cs[0].body, 0) == uint32_t(w));
    CHECK(be32(cs[0].body, 4) == uint32_t(h));
    CHECK(cs[0].body[8] == 8);
    CHECK(cs[0].body[9] == 6);
    CHECK(cs[1].type == "IDAT");
    CHECK(cs[2].type == "IEND");
    const auto raw = inflateStored(cs[1].body);
    REQUIRE(raw.size() == static_cast<size_t>(h) * (w * 4 + 1));
    for (int y = 0; y < h; ++y) {
        CHECK(raw[static_cast<size_t>(y) * (w * 4 + 1)] == 0);
        CHECK(std::memcmp(raw.data() + static_cast<size_t>(y) * (w * 4 + 1) + 1, rgba.data() + static_cast<size_t>(y) * w * 4,
                          static_cast<size_t>(w) * 4) == 0);
    }
}

TEST_CASE("PNG writer: indexed image with palette and tRNS") {
    const std::vector<Rgb8> pal = {{0, 0, 0}, {255, 0, 0}, {0, 0, 170}, {255, 255, 255}};
    const std::vector<uint8_t> idx = {0, 1, 2, 3, 3, 2, 1, 0};
    const std::vector<uint8_t> alpha = {255, 255, 0, 255};
    const auto cs = chunks(png::encodeIndexed(4, 2, idx, pal, alpha));
    REQUIRE(cs.size() == 5);
    CHECK(cs[0].body[9] == 3);
    CHECK(cs[1].type == "PLTE");
    CHECK(cs[1].body.size() == 12);
    CHECK(cs[2].type == "tRNS");
    CHECK(cs[2].body == std::vector<uint8_t>{255, 255, 0});  // trailing 255 trimmed
    const auto raw = inflateStored(cs[3].body);
    CHECK(raw == std::vector<uint8_t>{0, 0, 1, 2, 3, 0, 3, 2, 1, 0});
    CHECK_THROWS(png::encodeIndexed(1, 1, std::vector<uint8_t>{9}, pal));
}

TEST_CASE("ByteReader little-endian access and bounds") {
    const std::vector<uint8_t> d = {0x34, 0x12, 0x78, 0x56, 0xFE, 0xFF};
    ByteReader r(d, "test");
    CHECK(r.u16(0) == 0x1234);
    CHECK(r.u32(0) == 0x56781234u);
    CHECK(r.i16(4) == -2);
    CHECK(r.readU16() == 0x1234);
    CHECK(r.readU16() == 0x5678);
    CHECK(r.pos() == 4);
    CHECK_THROWS_AS(r.u32(4), FormatError);
    CHECK_THROWS_AS(r.bytes(5, 2), FormatError);
    CHECK_THROWS_AS(r.u8(6), FormatError);
    CHECK(r.has(6, 0));
    CHECK_FALSE(r.has(7, 0));
}

TEST_CASE("splitPath") {
    CHECK(vfs::splitPath("SOUNDS\\eopen1.mid") == std::vector<std::string>{"SOUNDS", "eopen1.mid"});
    CHECK(vfs::splitPath("/a//b/./c/") == std::vector<std::string>{"a", "b", "c"});
    CHECK(vfs::splitPath("").empty());
}
