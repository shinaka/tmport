// DOSBox-X save states, the game-neutral reader (dosbox_state_file.h).
#include "dos/save/dosbox_state_file.h"

#include <algorithm>
#include <format>

#include "util/error.h"
#include "util/file_util.h"
#include "util/inflate.h"

namespace tmport::dos {

namespace {

uint16_t rd16(std::span<const uint8_t> b, size_t p) { return static_cast<uint16_t>(b[p] | (b[p + 1] << 8)); }
uint32_t rd32(std::span<const uint8_t> b, size_t p) { return rd16(b, p) | (static_cast<uint32_t>(rd16(b, p + 2)) << 16); }

}  // namespace

std::optional<ZipEntry> findZipEntry(std::span<const uint8_t> zip, std::string_view name) {
    if (zip.size() < 22) return std::nullopt;
    size_t eocd = zip.size() - 22;
    const size_t lowest = zip.size() > 22 + 0xFFFF ? zip.size() - 22 - 0xFFFF : 0;
    while (eocd > lowest && rd32(zip, eocd) != 0x06054b50) --eocd;
    if (rd32(zip, eocd) != 0x06054b50) return std::nullopt;
    const uint16_t entries = rd16(zip, eocd + 10);
    size_t p = rd32(zip, eocd + 16);
    for (uint16_t e = 0; e < entries && p + 46 <= zip.size(); ++e) {
        if (rd32(zip, p) != 0x02014b50) break;
        const uint16_t method = rd16(zip, p + 10);
        const uint32_t csize = rd32(zip, p + 20), usize = rd32(zip, p + 24);
        const uint16_t nlen = rd16(zip, p + 28), xlen = rd16(zip, p + 30), clen = rd16(zip, p + 32);
        const uint32_t local = rd32(zip, p + 42);
        if (p + 46 + nlen > zip.size()) break;
        const std::string_view entryName(reinterpret_cast<const char*>(zip.data() + p + 46), nlen);
        if (entryName == name) {
            if (local + 30 > zip.size()) return std::nullopt;
            const size_t data = local + 30 + rd16(zip, local + 26) + rd16(zip, local + 28);
            if (data > zip.size() || csize > zip.size() - data) return std::nullopt;
            return ZipEntry{method, zip.subspan(data, csize), usize};
        }
        p += 46 + nlen + xlen + clen;
    }
    return std::nullopt;
}

std::vector<uint8_t> readZipEntry(const ZipEntry& e, size_t limit) {
    if (e.method == 0) return std::vector<uint8_t>(e.packed.begin(), e.packed.begin() + static_cast<std::ptrdiff_t>(std::min(e.packed.size(), limit)));
    if (e.method == 8) return inflateRaw(e.packed, std::min<size_t>(limit, e.size), InflateLimit::Stop);
    throw FormatError(std::format("unsupported zip compression method {}", e.method));
}

DosboxStateFile DosboxStateFile::load(const std::filesystem::path& path) {
    auto zip = readWholeFile(path);
    if (!zip) throw Error(std::format("{}: cannot read the file", pathToUtf8(path)));
    DosboxStateFile s;
    s.path_ = path;
    s.zip_ = std::move(*zip);
    const auto memory = findZipEntry(s.zip_, "Memory");
    if (!memory) throw Error(std::format("{}: not a DOSBox-X save state (no Memory entry)", pathToUtf8(path)));
    std::vector<uint8_t> mem = readZipEntry(*memory, kMemoryHeader + kRamBytes);
    if (mem.size() < kMemoryHeader + 0x10000) throw FormatError(std::format("{}: the Memory entry is too short", pathToUtf8(path)));
    s.mem_.assign(mem.begin() + kMemoryHeader, mem.end());
    if (const auto m = findZipEntry(s.zip_, "Machine_Type")) {
        const std::vector<uint8_t> name = readZipEntry(*m, 64);
        s.machineName_.assign(name.begin(), name.end());
    }
    return s;
}

std::optional<std::vector<uint8_t>> DosboxStateFile::entry(std::string_view name, size_t limit) const {
    const auto e = findZipEntry(zip_, name);
    if (!e) return std::nullopt;
    return readZipEntry(*e, limit);
}

bool DosboxStateFile::hasEntry(std::string_view name) const { return findZipEntry(zip_, name).has_value(); }

std::span<const uint8_t> DosboxStateFile::bytesAt(uint32_t linear, size_t n) const {
    if (linear > mem_.size() || n > mem_.size() - linear) return {};
    return std::span<const uint8_t>(mem_).subspan(linear, n);
}

uint8_t DosboxStateFile::byteAt(uint32_t linear) const { return linear < mem_.size() ? mem_[linear] : 0; }

uint16_t DosboxStateFile::wordAt(uint32_t linear) const {
    return static_cast<uint16_t>(byteAt(linear) | (byteAt(linear + 1) << 8));
}

uint32_t DosboxStateFile::dwordAt(uint32_t linear) const {
    return wordAt(linear) | (static_cast<uint32_t>(wordAt(linear + 2)) << 16);
}

std::optional<uint16_t> DosboxStateFile::findSegmentBySignature(std::string_view signature, uint16_t offset) const {
    const auto it = std::search(mem_.begin(), mem_.end(), signature.begin(), signature.end());
    const auto at = static_cast<size_t>(it - mem_.begin());
    if (it == mem_.end() || at < offset || (at - offset) % 16 != 0) return std::nullopt;
    return static_cast<uint16_t>((at - offset) / 16);
}

std::optional<DosboxStateFile::Cpu> DosboxStateFile::cpu(uint16_t stackSegment) const {
    // The registers come first: EAX..EDI (ESP at +0x10), EIP at +0x20. The segment values follow
    // later as 8-byte words ES, CS, SS, DS (DOSBox-X 2026.10.01: from +0xEB); SS identifies them.
    const auto head = entry("CPU", 1024);
    if (!head || head->size() < 0x40) return std::nullopt;
    const std::vector<uint8_t>& h = *head;
    auto q = [&h](size_t p) {
        uint64_t v = 0;
        for (int i = 7; i >= 0; --i) v = (v << 8) | h[p + static_cast<size_t>(i)];
        return v;
    };
    for (size_t p = 0x40 + 16; p + 16 <= h.size(); ++p) {
        if (q(p) != stackSegment || q(p - 8) == 0 || q(p - 8) > 0xFFFF || q(p - 16) > 0xFFFF || q(p + 8) > 0xFFFF) continue;
        Cpu c;
        c.cs = static_cast<uint16_t>(q(p - 8));
        c.ss = static_cast<uint16_t>(q(p));
        c.sp = rd32(h, 0x10) & 0xFFFF;
        c.ip = rd32(h, 0x20) & 0xFFFF;
        return c;
    }
    return std::nullopt;
}

std::vector<std::pair<uint16_t, uint16_t>> DosboxStateFile::stackWords(const Cpu& c, uint32_t bytes) const {
    std::vector<std::pair<uint16_t, uint16_t>> out;
    const uint32_t base = static_cast<uint32_t>(c.ss) * 16;
    for (uint32_t off = c.sp; off + 3 < c.sp + bytes && off + 3 <= 0xFFFF; off += 2)
        out.emplace_back(wordAt(base + off + 2), wordAt(base + off));
    return out;
}

std::vector<uint16_t> DosboxStateFile::biosKeys() const {
    std::vector<uint16_t> keys;
    uint16_t head = wordAt(0x41A), tail = wordAt(0x41C);
    for (int n = 0; head != tail && n < 16; ++n) {
        if (head < 0x1E || head > 0x3C) break;  // not the standard buffer: leave it empty
        keys.push_back(wordAt(0x400u + head));
        head = static_cast<uint16_t>(head + 2 == 0x3E ? 0x1E : head + 2);
    }
    return keys;
}

std::optional<DosboxStateFile::Mode13> DosboxStateFile::mode13() const {
    const auto vga = entry("Vga");
    if (!vga || vga->size() < kVgaMemoryBytes + kVgaTailBytes + kVgaDacBeforeMemory) return std::nullopt;
    const size_t base = vga->size() - kVgaMemoryBytes - kVgaTailBytes;
    Mode13 m;
    m.vram.resize(static_cast<size_t>(kMode13Width) * kMode13Height);
    for (size_t a = 0; a < m.vram.size(); ++a) m.vram[a] = (*vga)[base + 16 * (a >> 2) + (a & 3)];
    std::copy_n(vga->begin() + static_cast<std::ptrdiff_t>(base - kVgaDacBeforeMemory), m.dac.size(), m.dac.begin());
    // A DAC entry holds 6 bits: anything larger means the entry has another layout.
    if (std::any_of(m.dac.begin(), m.dac.end(), [](uint8_t v) { return v > 63; })) return std::nullopt;
    return m;
}

}  // namespace tmport::dos
