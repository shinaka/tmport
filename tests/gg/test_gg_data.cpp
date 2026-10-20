// Tests of the G&G data layer (src/gg/data): ssg.exe, the four archives, the method-3 decompressor,
// bundles and the resource containers. The expected numbers come from re/gg/docs/fmt_data.md,
// fmt_graphics.md, fmt_sound.md, re/gg/docs/overlay_map.tsv and the oracle tools/gg/ggarc.py
// (per-volume CRC-32 digests of the decoded entries); no game data is stored here.

#include <doctest/doctest.h>

#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>

#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/data/gg_files.h"
#include "gg/data/lzh.h"
#include "gg/data/resources.h"
#include "gg_test_data.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/png_writer.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

// CRC-32 of the little-endian CRC-32s of a list of payloads (the digests of ggarc.py's output).
struct CrcDigest {
    std::vector<uint8_t> acc;
    void add(std::span<const uint8_t> payload) {
        const uint32_t c = png::crc32(payload);
        for (int i = 0; i < 4; ++i) acc.push_back(static_cast<uint8_t>(c >> (8 * i)));
    }
    uint32_t value() const { return png::crc32(acc); }
};

// Sets an environment variable for the lifetime of the object (restores it after).
class EnvOverride {
public:
    EnvOverride(const char* name, const char* value) : name_(name) {
        if (const char* old = std::getenv(name)) old_ = old;
        set(value);
    }
    ~EnvOverride() { set(old_ ? old_->c_str() : nullptr); }

private:
    void set(const char* value) {
#ifdef _WIN32
        _putenv_s(name_, value ? value : "");
#else
        if (value) setenv(name_, value, 1);
        else unsetenv(name_);
#endif
    }
    const char* name_;
    std::optional<std::string> old_;
};

void touch(const std::filesystem::path& p) { std::ofstream(p, std::ios::binary).put('x'); }

}  // namespace

// --- ssg.exe ------------------------------------------------------------------------------------------

TEST_CASE("gg exe: load module, DGROUP and the Ghidra address mapping") {
    GG_REQUIRE_DATA();
    const GgExe& exe = ggFiles->exe();
    CHECK(exe.file().size() == 213200);
    CHECK(exe.image().size() == 108544);
    CHECK(exe.dgroup().size() == 0xBD04);
    // ARCHITECTURE.md conventions: 1000:0000 is C0's BA A7 0E at 0x1200; 1454:0046 the PIT set-up at
    // 0x5786; 3d39:083a (the DAM opcode table) at 0x2FB1A; DS:0004 at 0xFC74.
    CHECK(exe.fileOffset(0x1000, 0x0000) == 0x1200);
    CHECK(exe.at(0x1000, 0)[0] == 0xBA);
    CHECK(exe.fileOffset(0x1454, 0x0046) == 0x5786);
    CHECK(exe.at(0x1454, 0x0046)[0] == 0xB0);
    CHECK(exe.fileOffset(0x3d39, 0x083a) == 0x2FB1A);
    CHECK(exe.fileOffset(0x2b89, 0x03ba) == 0x1CB40 + 0x3BA);
    CHECK(exe.fileOffset(GgExe::kDgroupSeg, 4) == 0xFC74);
    CHECK(exe.dsString(4).size() == 42);
    CHECK(exe.at(0x2a78, 0).size() == 0x80);  // the stack segment ends the load module (SS:SP 1a78:0080)
    CHECK(exe.at(0x5000, 0).empty());
    CHECK(exe.fileOffset(0x5000, 0) == SIZE_MAX);

    // Far pointers: g_resFileNames DS:0550 far[4] point into DGROUP at the archive names.
    for (int v = 0; v < 4; ++v) {
        const auto p = exe.dsFarPtr(static_cast<uint16_t>(dg::kResFileNames + 4 * v));
        CHECK(p.seg == GgExe::kDgroupSeg);
        CHECK(iequalsAscii(exe.dsString(exe.dsOffsetOf(p)), Archive::volumeName(v)));
    }
    // g_stateTable DS:00c2: state 1 through a stub to State_01_Attract 2c11:0913, state 2 to
    // State_02_SignIn 2caa:1240, state 3 resident State_03_Hub 12e0:0148, entry 18 NULL.
    const auto state = [&](int k) { return exe.resolveCode(exe.dsFarPtr(static_cast<uint16_t>(dg::kStateTable + 4 * k))); };
    CHECK(state(1) == GgExe::Address{0x2c11, 0x0913});
    CHECK(state(2) == GgExe::Address{0x2caa, 0x1240});
    CHECK(state(3) == GgExe::Address{0x12e0, 0x0148});
    CHECK(state(18).isNull());
}

TEST_CASE("gg exe: the 35 overlays match re/gg/docs/overlay_map.tsv") {
    GG_REQUIRE_DATA();
    const GgExe& exe = ggFiles->exe();
    std::ifstream map(testdata::repoRoot() / "re" / "gg" / "docs" / "overlay_map.tsv");
    REQUIRE(map.good());
    std::string line;
    int rows = 0;
    while (std::getline(map, line)) {
        if (line.empty() || !std::isdigit(static_cast<unsigned char>(line[0]))) continue;
        std::istringstream in(line);
        std::string n, stub, ghidraStub, ghidraSeg, fileOffset, codeSize, fixups, entries;
        std::getline(in, n, '\t');
        std::getline(in, stub, '\t');
        std::getline(in, ghidraStub, '\t');
        std::getline(in, ghidraSeg, '\t');
        std::getline(in, fileOffset, '\t');
        std::getline(in, codeSize, '\t');
        std::getline(in, fixups, '\t');
        std::getline(in, entries, '\t');
        const auto& ov = exe.overlays().at(static_cast<size_t>(std::stoi(n)));
        CAPTURE(n);
        CHECK(ov.stubSeg == std::stoul(ghidraStub, nullptr, 16));
        CHECK(ov.seg == (ghidraSeg == "-" ? 0u : std::stoul(ghidraSeg, nullptr, 16)));
        CHECK(ov.fileOffset == std::stoul(fileOffset, nullptr, 16));
        CHECK(ov.code.size() == std::stoul(codeSize));
        CHECK(ov.fixups.size() == std::stoul(fixups));
        CHECK(ov.entries.size() == std::stoul(entries));
        if (ov.seg) {
            CHECK(exe.overlayBySeg(ov.seg) == &ov);
            CHECK(exe.fileOffset(ov.seg, 0) == ov.fileOffset);
        }
        ++rows;
    }
    CHECK(rows == 35);
    CHECK(exe.overlays().size() == 35);
}

TEST_CASE("gg exe: rejects other files") {
    GG_REQUIRE_DATA();
    const auto good = std::vector<uint8_t>(ggFiles->exe().file().begin(), ggFiles->exe().file().end());
    CHECK_THROWS_AS(GgExe::load(std::vector<uint8_t>(good.begin(), good.end() - 1)), std::runtime_error);
    auto sig = good;
    sig[0xFC74] ^= 1;  // the Borland signature at DS:0004
    CHECK_THROWS_AS(GgExe::load(sig), std::runtime_error);
    auto fbov = good;
    fbov[0x1BA00] = 'X';
    CHECK_THROWS_AS(GgExe::load(fbov), std::runtime_error);
    CHECK_THROWS_AS(GgExe::load(std::vector<uint8_t>(16, 0)), std::runtime_error);
}

// --- Archives -----------------------------------------------------------------------------------------

TEST_CASE("gg archive: volumes, counts and sizes (fmt_data.md §2.1)") {
    GG_REQUIRE_DATA();
    const Archive& arc = ggFiles->archive();
    struct Row {
        int entries, lzh, stored;
        size_t storedBytes, unpacked;
    };
    const Row rows[4] = {{878, 600, 278, 332605, 1907471},
                         {479, 354, 125, 346489, 1830050},
                         {335, 332, 3, 278341, 1537118},
                         {205, 204, 1, 257911, 479048}};
    size_t total = 0;
    for (int v = 0; v < 4; ++v) {
        CAPTURE(v);
        CHECK(arc.entryCount(v) == rows[v].entries);
        int lzh = 0, stored = 0;
        size_t storedBytes = 0, unpacked = 0;
        for (int k = 0; k < arc.entryCount(v); ++k) {
            const auto e = arc.info(static_cast<uint16_t>((v << 12) | k));
            (e.method == kMethodStored ? stored : lzh)++;
            storedBytes += e.storedSize;
            unpacked += arc.unpackedSize(e.id);
        }
        CHECK(lzh == rows[v].lzh);
        CHECK(stored == rows[v].stored);
        CHECK(storedBytes == rows[v].storedBytes);
        CHECK(unpacked == rows[v].unpacked);
        total += static_cast<size_t>(arc.entryCount(v));
        // The integrity entry: I, stored, 0xA55A.
        const auto last = arc.load(static_cast<uint16_t>((v << 12) | (arc.entryCount(v) - 1)));
        CHECK(last.type == 'I');
        CHECK(last.method == kMethodStored);
        REQUIRE(last.bytes.size() == 2);
        CHECK((last.bytes[0] | (last.bytes[1] << 8)) == Archive::kIntegrityWord);
        CHECK(arc.originalReadSize(last.id) == 4);
    }
    CHECK(total == 1897);
    CHECK(arc.totalEntries() == 1897);
    CHECK(arc.ids().size() == 1897);
    CHECK(arc.exists(0x036d));
    CHECK_FALSE(arc.exists(0x036e));
    CHECK_FALSE(arc.exists(0x4000));
    CHECK_THROWS_AS((void)arc.load(0x036e), std::out_of_range);
}

TEST_CASE("gg archive: every entry decodes byte for byte as tools/gg/ggarc.py") {
    GG_REQUIRE_DATA();
    const Archive& arc = ggFiles->archive();
    // CRC-32 over the little-endian CRC-32s of the decoded payloads, per volume, from ggarc.py.
    const uint32_t expected[4] = {0xc455828c, 0x0262884b, 0x8092339f, 0xd1892fe7};
    CrcDigest all;
    size_t lzh = 0, window13 = 0, defaults = 0, tokenWords = 0, unpacked = 0, largest = 0;
    for (int v = 0; v < 4; ++v) {
        CrcDigest digest;
        for (int k = 0; k < arc.entryCount(v); ++k) {
            const auto id = static_cast<uint16_t>((v << 12) | k);
            LzhInfo info;
            const auto r = arc.load(id, &info);
            digest.add(r.bytes);
            all.add(r.bytes);
            unpacked += r.bytes.size();
            largest = std::max(largest, r.bytes.size());
            CHECK(r.bytes.size() == arc.unpackedSize(id));
            if (r.method != kMethodStored) {
                ++lzh;
                CHECK(info.size == r.bytes.size());
                CHECK(info.sizeHighBits == 3);
                CHECK(info.usedBytes == arc.info(id).storedSize);  // 0 bytes of slack in every entry
                CHECK(info.maxDistance <= (1u << info.windowBits));
                window13 += info.windowBits == 13;
                defaults += !info.explicitDistances;
                tokenWords += info.ignoredWord == ((info.literals + info.matches) & 0xFFFF);
            }
        }
        CAPTURE(v);
        CHECK(digest.value() == expected[v]);
    }
    CHECK(all.value() == 0xa51351fe);
    CHECK(lzh == 1490);
    CHECK(window13 == 1490);
    CHECK(defaults == 1313);
    CHECK(tokenWords == 1490);
    CHECK(unpacked == 5753687);
    CHECK(largest == 64014);
}

TEST_CASE("gg archive: method 3 details of resource 0x0001 (fmt_data.md §3.5)") {
    GG_REQUIRE_DATA();
    LzhInfo info;
    const auto r = ggFiles->archive().load(0x0001, &info);
    CHECK(r.type == 0x01);
    CHECK(info.size == 3026);
    CHECK(info.windowBits == 13);
    CHECK(info.ignoredWord == 0x00CF);
    CHECK(info.literals == 82);
    CHECK(info.matches == 125);
    CHECK_FALSE(info.explicitDistances);
    CHECK(ggFiles->archive().info(0x0001).storedSize == 372);
}

TEST_CASE("gg lzh: the default distance lengths of ssg.exe form complete codes") {
    GG_REQUIRE_DATA();
    const auto rows = LzhDistanceRows::fromExe(ggFiles->exe());
    for (const int w : {12, 13}) {
        const auto lengths = rows.lengths(w);
        CHECK(lengths.size() == (size_t{1} << (w - 6)));
        uint64_t kraft = 0;
        for (const auto l : lengths) kraft += uint64_t{1} << (32 - l);
        CHECK(kraft == (uint64_t{1} << 32));
    }
}

TEST_CASE("gg lzh: broken streams are rejected") {
    LzhDistanceRows rows;  // all zero: no default code
    CHECK_THROWS_AS(lzhDecompress(std::vector<uint8_t>{1, 0}, rows), FormatError);
    // Size 1, window bit 0, no literal codes at all: an empty (incomplete) code.
    std::vector<uint8_t> s = {1, 0, 0, 0xC0};
    s.resize(64, 0);
    CHECK_THROWS_AS(lzhDecompress(s, rows), FormatError);
    // Size 0 decodes to nothing once both codes are complete: two literals of length 1 (symbols 0
    // and 1) and explicit distances with codes 0 and 1 of length 1 (window 12: 64 codes).
    std::vector<uint8_t> bits;
    const auto put = [&](uint32_t v, int n) {
        for (int i = n - 1; i >= 0; --i) bits.push_back(static_cast<uint8_t>((v >> i) & 1));
    };
    put(0, 1);   // window 12
    put(2, 16);  // ignored
    for (int sym = 0; sym < 286; ++sym) {
        if (sym < 2) {
            put(1, 1);
            put(0, 4);  // length 1
        } else {
            put(0, 1);
        }
    }
    put(1, 1);  // explicit distances
    for (int c = 0; c < 64; ++c) put(c < 2 ? 1 : 0, 4);
    put(1, 1);  // literal 1
    put(0, 1);  // literal 0
    std::vector<uint8_t> stream = {2, 0, 0, 0xC0};
    for (size_t i = 0; i < bits.size(); i += 8) {
        uint8_t b = 0;
        for (size_t j = 0; j < 8; ++j) b = static_cast<uint8_t>((b << 1) | (i + j < bits.size() ? bits[i + j] : 0));
        stream.push_back(b);
    }
    LzhInfo info;
    const auto out = lzhDecompress(stream, rows, &info);
    CHECK(out == std::vector<uint8_t>{1, 0});
    CHECK(info.literals == 2);
    CHECK(info.usedBytes == stream.size());
    // The same stream cut short reads past its end.
    stream.pop_back();
    CHECK_THROWS_AS(lzhDecompress(stream, rows), FormatError);
}

TEST_CASE("gg archive: checkVolume names the broken file") {
    GG_REQUIRE_DATA();
    const auto& arc = ggFiles->archive();
    const auto v2 = arc.volumeBytes(2);
    std::vector<uint8_t> bad(v2.begin(), v2.end());
    bad[bad.size() - 1] ^= 0xFF;  // the 0xA55A word
    try {
        Archive::checkVolume(2, bad);
        FAIL("no error");
    } catch (const FormatError& e) {
        CHECK(std::string(e.what()).starts_with("GIZMO002.DAT"));
    }
    CHECK_THROWS_AS(Archive::checkVolume(1, v2), FormatError);  // the digit says volume 2
    bad.assign(v2.begin(), v2.end() - 3);
    CHECK_THROWS_AS(Archive::checkVolume(2, bad), FormatError);
}

// --- Bundles and resource types ------------------------------------------------------------------------

TEST_CASE("gg resources: types, bundles and kinds (fmt_data.md §4-§5)") {
    GG_REQUIRE_DATA();
    const Archive& arc = ggFiles->archive();
    std::map<char, int> types, memberTypes;
    std::map<std::string, int> kinds;
    int bundles = 0, members = 0;
    CrcDigest memberDigest;
    for (const uint16_t id : arc.ids()) {
        const auto r = arc.load(id);
        ++types[static_cast<char>(r.type)];
        if (r.type == static_cast<uint8_t>(ResType::Bundle)) {
            ++bundles;
            const Bundle b(r);
            for (const auto& m : b.members()) {
                ++members;
                ++memberTypes[static_cast<char>(m.type)];
                ++kinds[std::string(classify(m.type, m.payload, id, m.index))];
                CHECK(m.method == 0);
                CHECK(m.type != 0x01);  // no nesting
                memberDigest.add(m.payload);
            }
        } else {
            ++kinds[std::string(classify(r.type, r.bytes, id))];
        }
    }
    CHECK(bundles == 364);
    CHECK(members == 3945);
    CHECK(memberDigest.value() == 0x25205e2a);
    const std::map<char, int> expectedTypes = {{'\x01', 364}, {'A', 22}, {'D', 231}, {'F', 103}, {'G', 502}, {'H', 19},
                                               {'I', 99},   {'P', 26}, {'R', 18},  {'S', 278}, {'T', 222}, {'W', 13}};
    const std::map<char, int> expectedMembers = {{'C', 21}, {'G', 2387}, {'H', 13},  {'I', 120}, {'L', 86},
                                                 {'M', 388}, {'P', 94},  {'S', 687}, {'T', 149}};
    CHECK(types == expectedTypes);
    CHECK(memberTypes == expectedMembers);
    const std::map<std::string, int> expectedKinds = {
        {"picture", 2959}, {"font", 2},    {"palette", 50},    {"table", 550}, {"integer", 215},
        {"song", 495},     {"string", 437}, {"text", 371},     {"struct", 33}, {"level", 15},
        {"integrity", 4},  {"hint", 32},   {"dam", 12},        {"grid", 101},  {"movie", 202}};
    CHECK(kinds == expectedKinds);
    CHECK(arc.idsOfType(ResType::R).size() == 18);
}

TEST_CASE("gg resources: container headers (fmt_graphics.md, fmt_sound.md)") {
    GG_REQUIRE_DATA();
    const Archive& arc = ggFiles->archive();
    int lpx = 0, raw = 0, rle = 0, offset0 = 0, palettes = 0, fullPalettes = 0, dam = 0, damWords = 0, paths = 0;
    std::map<int, int> bmfClasses;
    int hints = 0, notes = 0, textsChecked = 0;
    const auto visit = [&](uint8_t type, std::span<const uint8_t> p, uint16_t id) {
        if (isLpx(p)) {
            const auto h = parseLpx(p);
            ++lpx;
            (h.rle() ? rle : raw)++;
            if (h.dataOffset == 0) ++offset0;
            if (!h.rle()) CHECK(14u + static_cast<size_t>(h.width) * h.height == p.size());
            CHECK(h.width <= 320);
            CHECK(h.height <= 200);
            return;
        }
        if (type == 'P' && isPaletteRange(p)) {
            const auto pal = parsePaletteRange(p);
            ++palettes;
            if (pal.byteCount == 768) ++fullPalettes;
            CHECK(pal.byteOffset % 3 == 0);
            CHECK(pal.byteCount % 3 == 0);
        }
        if (isDam(p)) {
            ++dam;
            const auto d = parseDam(p);
            damWords += static_cast<int>(d.wordCount());
            CHECK(d.word(d.wordCount() - 1) == 0x7FFF);
        }
        if (isBmf(p)) ++bmfClasses[parseBmf(p).songClass];
        if (type == 'R') {
            const auto path = parsePath(p);
            CHECK(path.back().code == -1);
            ++paths;
        }
        if (type == 'H') {
            ++hints;
            if (!parseHint(p).note.empty()) ++notes;
        }
        if (type == 'T' || type == 'H') {
            CHECK(textBytes(p).size() + 1 <= p.size());
            ++textsChecked;
        }
        (void)id;
    };
    for (const uint16_t id : arc.ids()) {
        const auto r = arc.load(id);
        if (r.type == 0x01) {
            for (const auto& m : parseBundle(r.bytes)) visit(m.type, m.payload, id);
        } else {
            visit(r.type, r.bytes, id);
        }
    }
    CHECK(lpx == 2959);
    CHECK(raw == 904);
    CHECK(rle == 2055);
    CHECK(offset0 == 55);
    CHECK(palettes == 50);
    CHECK(fullPalettes == 8);
    CHECK(dam == 12);
    CHECK(paths == 18);
    CHECK(bmfClasses == std::map<int, int>{{1, 237}, {3, 258}});
    CHECK(hints == 32);
    CHECK(notes == 19);  // the designer's trigger note of each New Player Hint
    CHECK(textsChecked == 371 + 32);

    // Fonts 9 and 10 (fmt_graphics.md §5).
    const auto f9 = parseFont(arc.load(0x0009, ResType::F).bytes);
    const auto f10 = parseFont(arc.load(0x000a, ResType::F).bytes);
    CHECK(f9.records == 128);
    CHECK(f9.height == 9);
    CHECK(f9.spacing == 2);
    CHECK((f9.flags & 1) == 1);
    CHECK(f10.records == 127);
    CHECK(f10.spacing == 1);
    CHECK_THROWS_AS(arc.load(0x0009, ResType::G), FormatError);

    // The final movie: 202 delta frames 0x3000..0x30c9, then the music bank pair 0x30ca/0x30cb.
    for (uint16_t id = 0x3000; id < 0x30CA; ++id) CHECK(classify(arc.stored(id)[0], arc.load(id).bytes, id) == "movie");
    CHECK(Bundle::load(arc, 0x30CA).size() > 0);
}

TEST_CASE("gg resources: music-bank mute lists (fmt_sound.md §2.1)") {
    GG_REQUIRE_DATA();
    // The speaker movie bank 0x30ca: its list 4 ends in FD instead of FF (the silent pass).
    const auto bank = Bundle::load(ggFiles->archive(), 0x30CA);
    int lists = 0, unterminated = 0;
    for (const auto& m : bank.members()) {
        if (m.type != 'M') continue;
        const auto list = parseMuteList(m.payload);
        if (m.payload.empty() || m.payload[0] != 1) continue;
        ++lists;
        if (!list.terminated) ++unterminated;
    }
    CHECK(lists >= 4);
    CHECK(unterminated == 1);
}

// --- Folder ------------------------------------------------------------------------------------------------

TEST_CASE("gg files: missing files are named") {
    const auto empty = testdata::scratchDir("gg_files_empty");
    try {
        (void)GgFiles::open(empty);
        FAIL("no error");
    } catch (const MissingFileError& e) {
        CHECK(e.file() == "SSG.EXE");
    }
    GG_REQUIRE_DATA();
    const auto partial = testdata::scratchDir("gg_files_partial");
    std::filesystem::copy_file(*ggFiles->pathOf("ssg.exe"), partial / "SSG.EXE");
    try {
        (void)GgFiles::open(partial);
        FAIL("no error");
    } catch (const MissingFileError& e) {
        CHECK(e.file() == "GIZMO000.DAT");
    }
    std::error_code ec;
    std::filesystem::remove_all(partial, ec);
}

TEST_CASE("gg files: folder detection") {
    const auto root = testdata::scratchDir("gg_detect");
    const auto game = root / "gadgetsandgizmos";  // any case
    std::filesystem::create_directories(game);
    touch(game / "SSG.EXE");
    touch(game / "GIZMO000.DAT");
    const auto program = root / "a" / "b" / "c";
    std::filesystem::create_directories(program);
    {
        const EnvOverride noEnv("GG_DATA", nullptr);
        auto found = detectGgFolder(std::nullopt, program);
        REQUIRE(found);
        CHECK(found->source == GgFolderSource::NearProgram);
        CHECK(std::filesystem::equivalent(found->path, game));
        // With a trailing separator, as SDL_GetBasePath gives it.
        found = detectGgFolder(std::nullopt, pathFromUtf8(pathToUtf8(program) + "/"));
        REQUIRE(found);
        CHECK(std::filesystem::equivalent(found->path, game));
        // Too far away: 4 levels up.
        const auto deep = root / "a" / "b" / "c" / "d" / "e";
        std::filesystem::create_directories(deep);
        CHECK_FALSE(detectGgFolder(std::nullopt, deep));
        // The explicit path wins and is returned as given.
        found = detectGgFolder(root / "elsewhere", program);
        REQUIRE(found);
        CHECK(found->source == GgFolderSource::Explicit);
        CHECK(found->path == root / "elsewhere");
    }
    {
        const EnvOverride env("GG_DATA", pathToUtf8(game).c_str());
        const auto found = detectGgFolder(std::nullopt, root / "nowhere");
        REQUIRE(found);
        CHECK(found->source == GgFolderSource::Environment);
    }
    CHECK(GgFiles::looksLikeGgFolder(game));
    CHECK_FALSE(GgFiles::looksLikeGgFolder(root));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
