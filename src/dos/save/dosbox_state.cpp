// DOSBox-X save states of the DOS edition (dosbox_state.h).
#include "dos/save/dosbox_state.h"

#include <algorithm>
#include <array>
#include <format>
#include <string_view>

#include "util/error.h"
#include "util/file_util.h"
#include "util/inflate.h"

namespace tmport::dos {

namespace {

constexpr size_t kMemoryHeader = 0x88;       // "Memory" entry: header before physical address 0
constexpr uint32_t kRamBytes = 0xA0000;      // conventional memory: everything the game uses
constexpr std::string_view kTurboCSignature = "Turbo-C - Copyright (c) 1988 Borland Intl.";
constexpr uint16_t kSignatureOffset = 4;     // DS:0004
constexpr uint16_t kBackBufferSegment = 0x2005;
constexpr uint16_t kEgaRowTable = 0x2EEE;    // DS:2EEE: A000 offset of each screen row (0x4000 + y * 0x50)
constexpr uint32_t kTandyPage = 0x98000;     // the Tandy video page (top 32 KB of the 640 KB)

uint16_t rd16(std::span<const uint8_t> b, size_t p) { return static_cast<uint16_t>(b[p] | (b[p + 1] << 8)); }
uint32_t rd32(std::span<const uint8_t> b, size_t p) { return rd16(b, p) | (static_cast<uint32_t>(rd16(b, p + 2)) << 16); }

// A zip entry as stored (method 0 or 8) with its sizes.
struct ZipEntry {
    uint16_t method = 0;
    std::span<const uint8_t> packed;
    uint32_t size = 0;
};

// The entry `name` of a zip archive (central directory lookup); nullopt if absent.
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

// The bytes of an entry, at most `limit` of them (the start of a large entry).
std::vector<uint8_t> readZipEntry(const ZipEntry& e, size_t limit) {
    if (e.method == 0) return std::vector<uint8_t>(e.packed.begin(), e.packed.begin() + static_cast<std::ptrdiff_t>(std::min(e.packed.size(), limit)));
    if (e.method == 8) return inflateRaw(e.packed, std::min<size_t>(limit, e.size), InflateLimit::Stop);
    throw FormatError(std::format("unsupported zip compression method {}", e.method));
}

}  // namespace

DosboxState DosboxState::load(const std::filesystem::path& path) {
    const auto zip = readWholeFile(path);
    if (!zip) throw Error(std::format("{}: cannot read the file", pathToUtf8(path)));
    DosboxState s;
    s.path_ = path;
    const auto memory = findZipEntry(*zip, "Memory");
    if (!memory) throw Error(std::format("{}: not a DOSBox-X save state (no Memory entry)", pathToUtf8(path)));
    std::vector<uint8_t> mem = readZipEntry(*memory, kMemoryHeader + kRamBytes);
    if (mem.size() < kMemoryHeader + 0x10000) throw FormatError(std::format("{}: the Memory entry is too short", pathToUtf8(path)));
    s.mem_.assign(mem.begin() + kMemoryHeader, mem.end());
    if (const auto m = findZipEntry(*zip, "Machine_Type")) {
        const std::vector<uint8_t> name = readZipEntry(*m, 64);
        s.machineName_.assign(name.begin(), name.end());
    }
    s.machine_ = s.machineName_ == "MCH_EGA" ? Machine::Ega : s.machineName_ == "MCH_TANDY" ? Machine::Tandy : Machine::Other;

    const auto it = std::search(s.mem_.begin(), s.mem_.end(), kTurboCSignature.begin(), kTurboCSignature.end());
    const auto at = static_cast<size_t>(it - s.mem_.begin());
    if (it == s.mem_.end() || at < kSignatureOffset || (at - kSignatureOffset) % 16 != 0)
        throw FormatError(std::format("{}: SST.EXE is not running in this state (DGROUP not found)", pathToUtf8(path)));
    s.dsSeg_ = static_cast<uint16_t>((at - kSignatureOffset) / 16);
    if (s.dgroup().size() != kDgroupBytes) throw FormatError(std::format("{}: DGROUP lies past the saved memory", pathToUtf8(path)));

    if (s.machine_ == Machine::Tandy) {
        s.readScreen({});
    } else if (const auto vga = findZipEntry(*zip, "Vga")) {
        s.readScreen(readZipEntry(*vga, 16u << 20));
    }
    if (const auto cpu = findZipEntry(*zip, "CPU")) {
        // The registers come first: EAX..EDI (ESP at +0x10), EIP at +0x20. The segment values follow
        // later as 8-byte words ES, CS, SS, DS (DOSBox-X 2026.10.01: from +0xEB). SS identifies them:
        // Turbo C's stack segment follows DGROUP (DS:0000..CC1F, 0xCC2 paragraphs).
        const std::vector<uint8_t> head = readZipEntry(*cpu, 1024);
        if (head.size() >= 0x40) {
            auto q = [&head](size_t p) {
                uint64_t v = 0;
                for (int i = 7; i >= 0; --i) v = (v << 8) | head[p + static_cast<size_t>(i)];
                return v;
            };
            const uint64_t stackSeg = static_cast<uint64_t>(s.dsSeg_) + kDgroupBytes / 16;
            for (size_t p = 0x40 + 16; p + 16 <= head.size(); ++p) {
                if (q(p) != stackSeg || q(p - 8) == 0 || q(p - 8) > 0xFFFF || q(p - 16) > 0xFFFF || q(p + 8) > 0xFFFF) continue;
                Cpu c;
                c.cs = static_cast<uint16_t>(q(p - 8));
                c.ss = static_cast<uint16_t>(q(p));
                c.sp = rd32(head, 0x10) & 0xFFFF;
                c.ip = rd32(head, 0x20) & 0xFFFF;
                s.cpu_ = c;
                break;
            }
        }
    }
    return s;
}

std::vector<std::pair<uint16_t, uint16_t>> DosboxState::stackReturns(uint32_t bytes) const {
    std::vector<std::pair<uint16_t, uint16_t>> out;
    if (!cpu_) return out;
    const uint32_t base = static_cast<uint32_t>(cpu_->ss) * 16;
    for (uint32_t off = cpu_->sp; off + 3 < cpu_->sp + bytes && off + 3 <= 0xFFFF; off += 2) {
        const uint16_t ip = wordAt(base + off), seg = toImageSegment(wordAt(base + off + 2));
        if (seg >= 0x1000 && seg < kDgroupImageSegment) out.emplace_back(seg, ip);  // a code segment of SST.EXE
    }
    return out;
}

std::span<const uint8_t> DosboxState::bytesAt(uint32_t linear, size_t n) const {
    if (linear > mem_.size() || n > mem_.size() - linear) return {};
    return std::span<const uint8_t>(mem_).subspan(linear, n);
}

uint8_t DosboxState::byteAt(uint32_t linear) const { return linear < mem_.size() ? mem_[linear] : 0; }

uint16_t DosboxState::wordAt(uint32_t linear) const {
    return static_cast<uint16_t>(byteAt(linear) | (byteAt(linear + 1) << 8));
}

uint32_t DosboxState::dsDword(uint16_t off) const {
    return dsWord(off) | (static_cast<uint32_t>(dsWord(static_cast<uint16_t>(off + 2))) << 16);
}

uint32_t DosboxState::farPointerAt(uint16_t off) const {
    return dsWord(off) + static_cast<uint32_t>(dsWord(static_cast<uint16_t>(off + 2))) * 16;
}

uint32_t DosboxState::imageLinear(uint16_t ghidraSegment, uint16_t off) const {
    return static_cast<uint32_t>((ghidraSegment + relocation()) & 0xFFFF) * 16 + off;
}

uint16_t DosboxState::toImageSegment(uint16_t runtimeSegment) const {
    return runtimeSegment == 0 ? 0 : static_cast<uint16_t>(runtimeSegment - relocation());
}

std::span<const uint8_t> DosboxState::backBuffer() const {
    return bytesAt(imageLinear(kBackBufferSegment, 0x10), static_cast<size_t>(kScreenRowBytes) * kScreenRows);
}

std::vector<uint16_t> DosboxState::biosKeys() const {
    std::vector<uint16_t> keys;
    uint16_t head = wordAt(0x41A), tail = wordAt(0x41C);
    for (int n = 0; head != tail && n < 16; ++n) {
        if (head < 0x1E || head > 0x3C) break;  // not the standard buffer: leave it empty
        keys.push_back(wordAt(0x400u + head));
        head = static_cast<uint16_t>(head + 2 == 0x3E ? 0x1E : head + 2);
    }
    return keys;
}

std::filesystem::path DosboxState::usersFileOf(const std::filesystem::path& sav) {
    auto p = sav;
    p.replace_extension(".users");
    return p;
}

void DosboxState::readScreen(std::span<const uint8_t> vga) {
    std::vector<uint8_t> px(static_cast<size_t>(kScreenRowBytes) * kScreenRows, 0);
    if (machine_ == Machine::Tandy) {
        if (bytesAt(kTandyPage, 0x8000).empty()) return;
        for (int y = 0; y < kScreenRows; ++y) {
            const uint32_t row = kTandyPage + static_cast<uint32_t>((y & 3) * 0x2000 + (y >> 2) * kScreenRowBytes);
            const auto src = bytesAt(row, kScreenRowBytes);
            std::copy(src.begin(), src.end(), px.begin() + static_cast<std::ptrdiff_t>(y * kScreenRowBytes));
        }
        screen_ = std::move(px);
        return;
    }
    // EGA (and VGA, which runs the EGA mode): the planes start at the latch table.
    std::array<uint8_t, 64> latch{};
    for (int k = 0; k < 16; ++k)
        for (int p = 0; p < 4; ++p) latch[static_cast<size_t>(4 * k + p)] = (k >> p) & 1 ? 0xC0 : 0x00;
    const auto it = std::search(vga.begin(), vga.end(), latch.begin(), latch.end());
    if (it == vga.end()) return;
    const auto planes = vga.subspan(static_cast<size_t>(it - vga.begin()));
    for (int y = 0; y < kScreenRows; ++y) {
        uint16_t row = dsWord(static_cast<uint16_t>(kEgaRowTable + 2 * y));
        if (y > 0 && row == 0) row = static_cast<uint16_t>(0x4000 + y * 0x50);  // table not built yet
        for (int x = 0; x < 2 * kScreenRowBytes; ++x) {
            const size_t a = 4 * (static_cast<size_t>(row) + static_cast<size_t>(x >> 2));
            if (a + 3 >= planes.size()) return;
            const int bit = 7 - 2 * (x & 3);  // each game pixel is two screen pixels: take the left one
            uint8_t c = 0;
            for (int p = 0; p < 4; ++p) c = static_cast<uint8_t>(c | (((planes[a + static_cast<size_t>(p)] >> bit) & 1) << p));
            uint8_t& b = px[static_cast<size_t>(y * kScreenRowBytes + (x >> 1))];
            b = (x & 1) ? static_cast<uint8_t>((b & 0xF0) | c) : static_cast<uint8_t>((b & 0x0F) | (c << 4));
        }
    }
    screen_ = std::move(px);
}

}  // namespace tmport::dos
