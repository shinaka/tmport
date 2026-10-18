#include "ui/dialog_desc.h"

#include <algorithm>
#include <charconv>
#include <format>

#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport::ui {

namespace {

std::vector<uint8_t> toVec(std::span<const uint8_t> s) { return {s.begin(), s.end()}; }

// String behind the far pointer at `off`: nullopt for NULL; a non-DGROUP target is a data error.
std::optional<std::vector<uint8_t>> farString(const ExeTables& exe, uint32_t off) {
    const FarPtr p = exe.farPtr(off);
    if (p.isNull()) return std::nullopt;
    if (p.kind != FarPtr::Kind::DGroup)
        throw FormatError(std::format("DGROUP {:#06x}: far pointer does not point into DGROUP", off));
    return toVec(exe.cstr(p.offset));
}

constexpr NamedDialog kDialogs[] = {
    {"start-another-game", dg::kDlgStartAnotherGame},
    {"exit", dg::kDlgExit},
    {"new-player-messages", dg::kDlgNewPlayerMessages},
    {"music-and-sound", dg::kDlgMusicAndSound},
    {"voice", dg::kDlgVoice},
    {"about", dg::kDlgAbout},
    {"write-error", dg::kDlgWriteError},
    {"already-on-list", dg::kDlgAlreadyOnList},
    {"remove-player", dg::kDlgRemovePlayer},
    {"list-full", dg::kDlgListFull},
    {"enter-name", dg::kDlgEnterName},
    {"title-exit", dg::kDlgTitleExit},
};

}  // namespace

DlgDesc DlgDesc::fromExe(const ExeTables& exe, uint16_t off) {
    DlgDesc d;
    d.type = exe.u16(off + 0x00u);
    d.title = farString(exe, off + 0x02u);
    d.footer = exe.u8(off + 0x06u);
    if (auto body = farString(exe, off + 0x07u)) d.body = std::move(*body);
    d.defaultButton = exe.u8(off + 0x0Bu);
    d.x = exe.i16(off + 0x0Cu);
    d.y = exe.i16(off + 0x0Eu);
    d.textWidth = exe.i16(off + 0x10u);
    d.lineCount = exe.i16(off + 0x12u);
    if (!exe.farPtr(off + 0x14u).isNull())
        log::warn("DlgDesc {:#06x}: picture pointer cannot be resolved statically; ignored", off);
    d.picDx = exe.u8(off + 0x18u);
    d.picDy = exe.u8(off + 0x19u);
    return d;
}

PagerDesc PagerDesc::fromExe(const ExeTables& exe, uint16_t off) {
    PagerDesc p;
    p.title = farString(exe, off + 0x00u);
    p.footer = exe.u8(off + 0x04u);
    p.pageCount = exe.u8(off + 0x09u);
    p.x = exe.i16(off + 0x0Au);
    p.y = exe.i16(off + 0x0Cu);
    p.textWidth = exe.i16(off + 0x0Eu);
    p.lineCount = exe.i16(off + 0x10u);
    return p;
}

std::span<const NamedDialog> staticDialogs() { return kDialogs; }

std::optional<uint16_t> findDialog(std::string_view s) {
    for (const auto& d : kDialogs)
        if (d.name == s) return d.offset;
    std::string_view hex = s;
    if (hex.size() > 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex.remove_prefix(2);
    unsigned v = 0;
    auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), v, 16);
    if (ec != std::errc() || ptr != hex.data() + hex.size() || v > 0xFFFF) return std::nullopt;
    return static_cast<uint16_t>(v);
}

std::vector<uint8_t> formatPercentS(std::span<const uint8_t> fmt, std::span<const uint8_t> arg) {
    std::vector<uint8_t> out;
    out.reserve(fmt.size() + arg.size());
    bool done = false;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (!done && fmt[i] == '%' && i + 1 < fmt.size() && fmt[i + 1] == 's') {
            out.insert(out.end(), arg.begin(), arg.end());
            ++i;
            done = true;
            continue;
        }
        out.push_back(fmt[i]);
    }
    return out;
}

}  // namespace tmport::ui
