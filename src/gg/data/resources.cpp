#include "gg/data/resources.h"

#include <algorithm>
#include <cstring>
#include <format>

#include "util/error.h"

namespace tmport::gg {

namespace {

uint16_t le16(std::span<const uint8_t> b, size_t at) { return static_cast<uint16_t>(b[at] | (b[at + 1] << 8)); }

bool hasMagic(std::span<const uint8_t> p, const char (&magic)[5]) {
    return p.size() >= 4 && std::memcmp(p.data(), magic, 4) == 0;
}

[[noreturn]] void bad(std::string_view what, const std::string& why) { throw FormatError(std::string(what) + ": " + why); }

constexpr uint16_t kMovieFirst = 0x3000, kMovieEnd = 0x30CA;  // Movie_Play(0x3000, 0x30ca, ...) 2fcb:06a5
constexpr uint16_t kIntegrityIds[] = {0x036D, 0x11DE, 0x214E, 0x30CC};

// The offset table of a bundle, or an empty vector when it does not parse.
std::vector<uint16_t> bundleOffsets(std::span<const uint8_t> p, std::string* why) {
    const auto fail = [&](const char* msg) {
        if (why) *why = msg;
        return std::vector<uint16_t>{};
    };
    if (p.size() < 4) return fail("shorter than its table");
    const uint16_t first = le16(p, 0);
    if (first < 4 || first % 2 || first > p.size()) return fail("bad table size");
    std::vector<uint16_t> offs(first / 2);
    for (size_t i = 0; i < offs.size(); ++i) offs[i] = le16(p, 2 * i);
    if (offs.back() != p.size()) return fail("the table does not end at the payload length");
    for (size_t i = 0; i + 1 < offs.size(); ++i)
        if (offs[i + 1] < offs[i] + 2) return fail("a member is shorter than its header");
    return offs;
}

BundleMember memberAt(std::span<const uint8_t> p, const std::vector<uint16_t>& offs, size_t i) {
    BundleMember m;
    m.index = static_cast<int>(i);
    m.offset = offs[i];
    m.type = p[offs[i]];
    m.method = p[offs[i] + 1u];
    m.payload = p.subspan(offs[i] + 2u, offs[i + 1] - offs[i] - 2u);
    return m;
}

}  // namespace

// --- Bundles -------------------------------------------------------------------------------------------

std::vector<BundleMember> parseBundle(std::span<const uint8_t> payload) {
    std::string why;
    const auto offs = bundleOffsets(payload, &why);
    if (offs.empty()) bad("bundle", why);
    std::vector<BundleMember> out;
    out.reserve(offs.size() - 1);
    for (size_t i = 0; i + 1 < offs.size(); ++i) out.push_back(memberAt(payload, offs, i));
    return out;
}

bool looksLikeBundle(std::span<const uint8_t> payload) { return !bundleOffsets(payload, nullptr).empty(); }

Bundle::Bundle(Resource res) : res_(std::move(res)) {
    if (res_.type != static_cast<uint8_t>(ResType::Bundle))
        bad(std::format("resource {:04x}", res_.id), "not a bundle");
    std::string why;
    offsets_ = bundleOffsets(res_.bytes, &why);
    if (offsets_.empty()) bad(std::format("bundle {:04x}", res_.id), why);
}

Bundle Bundle::load(const Archive& archive, uint16_t id) { return Bundle(archive.load(id)); }

BundleMember Bundle::member(size_t i) const {
    if (i >= size()) throw std::out_of_range(std::format("bundle {:04x} has {} members", res_.id, size()));
    return memberAt(res_.bytes, offsets_, i);
}

std::vector<BundleMember> Bundle::members() const {
    std::vector<BundleMember> out;
    out.reserve(size());
    for (size_t i = 0; i < size(); ++i) out.push_back(memberAt(res_.bytes, offsets_, i));
    return out;
}

// --- LPX ----------------------------------------------------------------------------------------------

bool isLpx(std::span<const uint8_t> p) { return hasMagic(p, "LPX\x1a"); }

LpxHeader parseLpx(std::span<const uint8_t> p) {
    if (!isLpx(p) || p.size() < LpxHeader::kSize) bad("LPX", "no LPX header");
    LpxHeader h;
    h.format = le16(p, 4);
    h.width = le16(p, 6);
    h.height = le16(p, 8);
    h.key = le16(p, 10);
    h.dataOffset = le16(p, 12);
    if (h.format > 1) bad("LPX", std::format("format {}", h.format));
    if (12u + h.dataOffset > p.size()) bad("LPX", "data offset outside the picture");
    h.data = p.subspan(12u + h.dataOffset);
    if (!h.rle() && h.data.size() < static_cast<size_t>(h.width) * h.height) bad("LPX", "raw picture is truncated");
    return h;
}

// --- Palettes ---------------------------------------------------------------------------------------------

bool isPaletteRange(std::span<const uint8_t> p) {
    if (p.size() < 4 || isLpx(p)) return false;
    const uint16_t off = le16(p, 0), count = le16(p, 2);
    return off + count <= 768 && p.size() >= 4u + count;
}

PaletteRange parsePaletteRange(std::span<const uint8_t> p) {
    if (!isPaletteRange(p)) bad("palette", "not a palette range (offset + count > 768 or truncated)");
    PaletteRange r;
    r.byteOffset = le16(p, 0);
    r.byteCount = le16(p, 2);
    r.rgb = p.subspan(4, r.byteCount);
    return r;
}

// --- Fonts ------------------------------------------------------------------------------------------------

bool isFont(std::span<const uint8_t> p) { return hasMagic(p, "fnt\x1a"); }

FontHeader parseFont(std::span<const uint8_t> p) {
    if (!isFont(p) || p.size() < FontHeader::kSize) bad("font", "no fnt header");
    FontHeader f;
    f.flags = le16(p, 4);
    f.records = le16(p, 6);
    f.height = le16(p, 8);
    f.spacing = le16(p, 10);
    f.glyphData = p.subspan(FontHeader::kSize);
    return f;
}

// --- DAM ----------------------------------------------------------------------------------------------------

bool isDam(std::span<const uint8_t> p) { return hasMagic(p, "DAM\x1a"); }

DamScript parseDam(std::span<const uint8_t> p) {
    if (!isDam(p)) bad("DAM", "no DAM header");
    DamScript d;
    d.words = p.subspan(4, (p.size() - 4) & ~size_t{1});
    return d;
}

// --- BMF ----------------------------------------------------------------------------------------------------

bool isBmf(std::span<const uint8_t> p) { return hasMagic(p, "BMF\x1a"); }

BmfSong parseBmf(std::span<const uint8_t> p) {
    if (!isBmf(p) || p.size() <= BmfSong::kBlockOffset) bad("BMF", "no BMF header");
    BmfSong s;
    s.block = p.subspan(BmfSong::kBlockOffset);
    s.songClass = s.block[0];
    return s;
}

// --- Texts ---------------------------------------------------------------------------------------------------

std::span<const uint8_t> textBytes(std::span<const uint8_t> p) {
    const auto nul = std::find(p.begin(), p.end(), uint8_t{0});
    if (nul == p.end()) bad("text", "no terminating NUL");
    return p.first(static_cast<size_t>(nul - p.begin()));
}

std::string textString(std::span<const uint8_t> p) {
    const auto t = textBytes(p);
    return {reinterpret_cast<const char*>(t.data()), t.size()};
}

bool isPrintableString(std::span<const uint8_t> p) {
    if (p.size() < 2 || p.back() != 0) return false;
    return std::all_of(p.begin(), p.end() - 1, [](uint8_t c) { return (c >= 0x20 && c < 0x7F) || c == 0x0D || c == 0x0A; });
}

HintText parseHint(std::span<const uint8_t> p) {
    const auto t = textBytes(p);
    HintText h;
    if (t.size() >= 2 && t[0] == 0xFF && t[1] == 0x06) {
        const auto end = std::find(t.begin() + 2, t.end(), uint8_t{0xFF});
        if (end == t.end()) bad("hint", "the FF 06 note is not closed");
        h.note = t.subspan(2, static_cast<size_t>(end - t.begin()) - 2);
        h.text = t.subspan(static_cast<size_t>(end - t.begin()) + 1);
    } else {
        h.text = t;
    }
    return h;
}

// --- Integer arrays and records ------------------------------------------------------------------------------

std::vector<uint16_t> u16Array(std::span<const uint8_t> p) {
    std::vector<uint16_t> out(p.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) out[i] = le16(p, 2 * i);
    return out;
}

std::vector<int16_t> i16Array(std::span<const uint8_t> p) {
    std::vector<int16_t> out(p.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<int16_t>(le16(p, 2 * i));
    return out;
}

std::vector<PathRecord> parsePath(std::span<const uint8_t> p) {
    if (p.empty() || p.size() % 6) bad("path", "size is not a multiple of 6");
    std::vector<PathRecord> out(p.size() / 6);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = {static_cast<int16_t>(le16(p, 6 * i)), static_cast<int16_t>(le16(p, 6 * i + 2)),
                  static_cast<int16_t>(le16(p, 6 * i + 4))};
    if (out.back().code != -1) bad("path", "the last record is not the end (-1)");
    return out;
}

MuteList parseMuteList(std::span<const uint8_t> p) {
    MuteList m;
    if (p.empty() || p[0] != 1) return m;
    for (size_t i = 1; i < p.size(); ++i) {
        if (p[i] == 0xFF) {
            m.terminated = true;
            break;
        }
        m.channels.push_back(p[i]);
    }
    return m;
}

// --- Classification --------------------------------------------------------------------------------------------

std::string_view classify(uint8_t type, std::span<const uint8_t> p, uint16_t id, int member) {
    if (type == static_cast<uint8_t>(ResType::Bundle)) return "bundle";
    if (isLpx(p)) return "picture";
    if (isFont(p)) return "font";
    if (isDam(p)) return "dam";
    if (isBmf(p)) return "song";
    switch (type) {
    case 'P': return isPaletteRange(p) ? "palette" : "unknown";
    case 'D':
        if (member < 0 && id >= kMovieFirst && id < kMovieEnd) return "movie";
        if (member < 0 && (id >> 12) == 0) return "level";
        return "table";
    case 'T': return "text";
    case 'H': return "hint";
    case 'S': return isPrintableString(p) ? "string" : "struct";
    case 'F': return "grid";
    case 'I':
        if (member < 0 && std::find(std::begin(kIntegrityIds), std::end(kIntegrityIds), id) != std::end(kIntegrityIds))
            return "integrity";
        return "integer";
    case 'A':
    case 'C':
    case 'L':
    case 'M':
    case 'R':
    case 'W': return "table";
    default: return "unknown";
    }
}

std::string typeName(uint8_t type) {
    if (type >= 0x21 && type < 0x7F) return std::string(1, static_cast<char>(type));
    return std::format("{:02x}", type);
}

}  // namespace tmport::gg
