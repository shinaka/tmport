// DOSBox-X save states of the DOS edition (dosbox_state.h), on the engine's reader
// (mischief/dos/save/dosbox_state_file.h).
#include "dos/save/dosbox_state.h"

#include <algorithm>
#include <array>
#include <format>
#include <string_view>

#include "mischief/util/error.h"
#include "mischief/util/file_util.h"

namespace tmport::dos {

namespace {

constexpr std::string_view kTurboCSignature = "Turbo-C - Copyright (c) 1988 Borland Intl.";
constexpr uint16_t kSignatureOffset = 4;     // DS:0004
constexpr uint16_t kBackBufferSegment = 0x2005;
constexpr uint16_t kEgaRowTable = 0x2EEE;    // DS:2EEE: A000 offset of each screen row (0x4000 + y * 0x50)
constexpr uint32_t kTandyPage = 0x98000;     // the Tandy video page (top 32 KB of the 640 KB)

}  // namespace

DosboxState DosboxState::load(const std::filesystem::path& path) {
    DosboxState s;
    s.file_ = mischief::dos::DosboxStateFile::load(path);
    s.machine_ = s.file_.machineName() == "MCH_EGA" ? Machine::Ega : s.file_.machineName() == "MCH_TANDY" ? Machine::Tandy : Machine::Other;

    const auto seg = s.file_.findSegmentBySignature(kTurboCSignature, kSignatureOffset);
    if (!seg) throw mischief::FormatError(std::format("{}: SST.EXE is not running in this state (DGROUP not found)", mischief::pathToUtf8(path)));
    s.dsSeg_ = *seg;
    if (s.dgroup().size() != kDgroupBytes) throw mischief::FormatError(std::format("{}: DGROUP lies past the saved memory", mischief::pathToUtf8(path)));

    if (s.machine_ == Machine::Tandy) {
        s.readScreen({});
    } else if (const auto vga = s.file_.entry("Vga", 16u << 20)) {
        s.readScreen(*vga);
    }
    // Turbo C's stack segment follows DGROUP (DS:0000..CC1F, 0xCC2 paragraphs).
    s.cpu_ = s.file_.cpu(static_cast<uint16_t>(s.dsSeg_ + kDgroupBytes / 16));
    return s;
}

std::vector<std::pair<uint16_t, uint16_t>> DosboxState::stackReturns(uint32_t bytes) const {
    std::vector<std::pair<uint16_t, uint16_t>> out;
    if (!cpu_) return out;
    for (const auto& [runtimeSeg, ip] : file_.stackWords(*cpu_, bytes)) {
        const uint16_t seg = toImageSegment(runtimeSeg);
        if (seg >= 0x1000 && seg < kDgroupImageSegment) out.emplace_back(seg, ip);  // a code segment of SST.EXE
    }
    return out;
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
