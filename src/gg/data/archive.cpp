#include "gg/data/archive.h"

#include <format>
#include <stdexcept>

#include "util/error.h"

namespace tmport::gg {

namespace {

uint32_t le32(std::span<const uint8_t> b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) |
           (static_cast<uint32_t>(b[at + 2]) << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

[[noreturn]] void bad(int volume, const std::string& why) {
    throw FormatError(std::format("GIZMO{:03}.DAT: {}", volume, why));
}

std::string idText(uint16_t id) { return std::format("resource {:04x}", id); }

}  // namespace

std::string Archive::volumeName(int volume) { return std::format("gizmo{:03}.dat", volume); }

void Archive::checkVolume(int volume, std::span<const uint8_t> b) {
    // Res_OpenFiles 14ff:0167 (the original checks only the 0xA55A word of volumes 0..2 and only
    // prints a message); the engine refuses any volume that is not the v1.0 one.
    if (b.size() < 12 || b[0] != 'M' || b[1] != 'R' || b[3] != 0x1A) bad(volume, "not an MRn archive");
    if (b[2] != '0' + volume) bad(volume, std::format("is volume {:c}, not {}", static_cast<char>(b[2]), volume));
    const uint32_t first = le32(b, 4);
    if (first % 4 || first < 8 || 4u + first > b.size()) bad(volume, "bad offset table");
    const size_t n = first / 4 - 1;
    if (static_cast<int>(n) != kEntryCounts[static_cast<size_t>(volume)])
        bad(volume, std::format("{} entries instead of {} (not the DOS v1.0 data)", n,
                                kEntryCounts[static_cast<size_t>(volume)]));
    if (le32(b, 4 + 4 * n) != b.size() - 4) bad(volume, "the offset table does not end at the file length");
    for (size_t k = 0; k < n; ++k) {
        const uint32_t a = le32(b, 4 + 4 * k), e = le32(b, 8 + 4 * k);
        if (a < first || e < a + 2) bad(volume, std::format("entry {} is out of order", k));
    }
    // The last entry: 'I', stored, the word 0xA55A.
    const size_t last = le32(b, 4 + 4 * (n - 1)) + 4u;
    if (b.size() - last != 4 || b[last] != 'I' || b[last + 1] != kMethodStored || b[last + 2] != 0x5A || b[last + 3] != 0xA5)
        bad(volume, "the integrity entry (I 5A A5) is missing");
}

Archive::Archive(std::array<std::vector<uint8_t>, kVolumes> volumes, const LzhDistanceRows& rows)
    : volumes_(std::move(volumes)), rows_(rows) {
    for (int v = 0; v < kVolumes; ++v) {
        const auto& b = volumes_[static_cast<size_t>(v)];
        checkVolume(v, b);
        const size_t n = le32(b, 4) / 4;
        auto& offs = index_[static_cast<size_t>(v)].offsets;
        offs.resize(n);
        for (size_t k = 0; k < n; ++k) offs[k] = le32(b, 4 + 4 * k);
    }
}

int Archive::entryCount(int volume) const {
    return static_cast<int>(index_.at(static_cast<size_t>(volume)).offsets.size()) - 1;
}

size_t Archive::totalEntries() const {
    size_t n = 0;
    for (int v = 0; v < kVolumes; ++v) n += static_cast<size_t>(entryCount(v));
    return n;
}

bool Archive::exists(uint16_t id) const {
    const int v = id >> 12;
    return v < kVolumes && static_cast<int>(id & 0xFFF) < entryCount(v);
}

std::vector<uint16_t> Archive::ids() const {
    std::vector<uint16_t> out;
    out.reserve(totalEntries());
    for (int v = 0; v < kVolumes; ++v)
        for (int k = 0; k < entryCount(v); ++k) out.push_back(static_cast<uint16_t>((v << 12) | k));
    return out;
}

std::vector<uint16_t> Archive::idsOfType(ResType type) const {
    std::vector<uint16_t> out;
    for (const uint16_t id : ids())
        if (stored(id)[0] == static_cast<uint8_t>(type)) out.push_back(id);
    return out;
}

const std::vector<uint8_t>& Archive::volumeOf(uint16_t id, size_t& index) const {
    if (!exists(id)) throw std::out_of_range(idText(id) + " does not exist");
    index = id & 0xFFF;
    return volumes_[id >> 12];
}

std::span<const uint8_t> Archive::stored(uint16_t id) const {
    size_t k = 0;
    const auto& b = volumeOf(id, k);
    const auto& offs = index_[id >> 12].offsets;
    return std::span<const uint8_t>(b).subspan(offs[k] + 4u, offs[k + 1] - offs[k]);
}

EntryInfo Archive::info(uint16_t id) const {
    const auto s = stored(id);
    EntryInfo e;
    e.id = id;
    e.volume = id >> 12;
    e.index = id & 0xFFF;
    e.type = s[0];
    e.method = s[1];
    e.fileOffset = index_[id >> 12].offsets[static_cast<size_t>(e.index)] + 4u;
    e.storedSize = s.size() - 2;
    return e;
}

uint32_t Archive::unpackedSize(uint16_t id) const {
    const auto s = stored(id);
    if (s[1] == kMethodStored) return static_cast<uint32_t>(s.size() - 2);
    if (s.size() < 6) throw FormatError(idText(id) + ": compressed payload shorter than its size word");
    return le32(s, 2) & 0x3FFFFFFFu;
}

uint32_t Archive::originalReadSize(uint16_t id) const {
    const auto s = stored(id);
    return s[1] == kMethodStored ? static_cast<uint32_t>(s.size()) : unpackedSize(id);
}

namespace {
thread_local LzhRefillHook t_refillHook;
}  // namespace

LzhRefillHook setLzhRefillHook(LzhRefillHook hook) {
    LzhRefillHook previous = std::move(t_refillHook);
    t_refillHook = std::move(hook);
    return previous;
}

int lzhReadCount(size_t usedBytes) {
    // 1b48:02aa reads the size word itself, then every byte comes through 1b48:002e, which reads the
    // next 4000 bytes (and drains the keyboard) whenever its count is 0, the first time included.
    if (usedBytes <= 4) return 0;
    return static_cast<int>((usedBytes - 4 + kLzhReadBlock - 1) / kLzhReadBlock);
}

Resource Archive::load(uint16_t id, LzhInfo* lzhInfo) const {
    const auto s = stored(id);
    Resource r;
    r.id = id;
    r.type = s[0];
    r.method = s[1];
    if (r.method == kMethodStored) {
        r.bytes.assign(s.begin() + 2, s.end());
    } else {
        LzhInfo local;
        LzhInfo& info = lzhInfo ? *lzhInfo : local;
        try {
            r.bytes = lzhDecompress(s.subspan(2), rows_, &info);
        } catch (const FormatError& e) {
            throw FormatError(idText(id) + ": " + e.what());
        }
        if (t_refillHook) t_refillHook(lzhReadCount(info.usedBytes));
    }
    return r;
}

Resource Archive::load(uint16_t id, ResType expected) const {
    Resource r = load(id);
    if (r.type != static_cast<uint8_t>(expected))
        throw FormatError(std::format("{}: type {:02x} instead of {:02x}", idText(id), r.type, static_cast<uint8_t>(expected)));
    return r;
}

}  // namespace tmport::gg
