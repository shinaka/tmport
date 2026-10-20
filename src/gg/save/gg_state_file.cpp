// DOSBox-X save states of Gizmos & Gadgets (gg_state_file.h).
#include "gg/save/gg_state_file.h"

#include <algorithm>
#include <format>

#include "gg/data/dgroup_offsets.h"
#include "gg/game/game.h"
#include "util/error.h"
#include "util/file_util.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kStackParagraphs = (dg::kDgroupSize + 15) / 16;  // the stack segment follows DGROUP (2a78)
constexpr uint16_t kFirstCodeSegment = 0x1000;
constexpr uint16_t kLastResidentSegment = 0x1E9F;  // the overlay stubs end here; overlays move in memory

}  // namespace

GgStateFile GgStateFile::load(const std::filesystem::path& path) {
    GgStateFile s;
    s.file_ = dos::DosboxStateFile::load(path);
    const auto seg = s.file_.findSegmentBySignature(kSignature, kSignatureOffset);
    if (!seg) throw FormatError(std::format("{}: ssg.exe is not running in this state (DGROUP not found)", pathToUtf8(path)));
    s.dsSeg_ = *seg;
    if (s.dgroup().size() != dg::kDgroupSize) throw FormatError(std::format("{}: DGROUP lies past the saved memory", pathToUtf8(path)));
    s.vga_ = s.file_.mode13();
    s.cpu_ = s.file_.cpu(static_cast<uint16_t>(s.dsSeg_ + kStackParagraphs));
    return s;
}

std::span<const uint8_t> GgStateFile::dgroup() const { return file_.bytesAt(dsLinear(), dg::kDgroupSize); }

uint32_t GgStateFile::farPointerAt(uint16_t off) const {
    const uint16_t o = dsWord(off), seg = dsWord(static_cast<uint16_t>(off + 2));
    if (o == 0 && seg == 0) return 0;
    return o + static_cast<uint32_t>(seg) * 16;
}

uint32_t GgStateFile::imageLinear(uint16_t ghidraSegment, uint16_t off) const {
    return static_cast<uint32_t>((ghidraSegment + relocation()) & 0xFFFF) * 16 + off;
}

uint16_t GgStateFile::toImageSegment(uint16_t runtimeSegment) const {
    return runtimeSegment == 0 ? 0 : static_cast<uint16_t>(runtimeSegment - relocation());
}

std::span<const uint8_t> GgStateFile::screenBuffer() const {
    const uint32_t at = farPointerAt(dg::kScreenBuffer);
    if (at == 0) return {};
    return file_.bytesAt(at, kScreenBytes);
}

GgCursorFields GgStateFile::cursor() const {
    auto s16 = [this](uint16_t off) { return static_cast<int16_t>(dsWord(off)); };
    GgCursorFields c;
    c.mouseX = s16(dg::kMouseX);
    c.mouseY = s16(dg::kMouseY);
    c.buttons = dsWord(dg::kMouseButtons);
    c.hotX = s16(dg::kCursorHotX);
    c.hotY = s16(dg::kCursorHotY);
    c.width = s16(dg::kCursorW);
    c.height = s16(dg::kCursorH);
    c.shown = s16(dg::kCursorShown);
    c.hidden = s16(dg::kCursorHidden);
    c.forceDefault = s16(dg::kCursorForceDefault);
    c.maxX = s16(dg::kMouseMaxX);
    c.maxY = s16(dg::kMouseMaxY);
    c.savedX = s16(dg::kCursorSavedX);
    c.savedY = s16(static_cast<uint16_t>(dg::kCursorSavedX + 2));
    return c;
}

std::vector<std::pair<uint16_t, uint16_t>> GgStateFile::stackReturns(uint32_t bytes) const {
    std::vector<std::pair<uint16_t, uint16_t>> out;
    if (!cpu_) return out;
    for (const auto& [runtimeSeg, ip] : file_.stackWords(*cpu_, bytes)) {
        const uint16_t seg = toImageSegment(runtimeSeg);
        if (seg >= kFirstCodeSegment && seg <= kLastResidentSegment) out.emplace_back(seg, ip);
    }
    return out;
}

std::filesystem::path GgStateFile::playersFileOf(const std::filesystem::path& sav) {
    auto p = sav;
    p.replace_extension(".players");
    return p;
}

std::vector<std::string> GgStateFile::apply(GgGame& g, bool withTime) const {
    std::vector<std::string> missing;
    loadFromDgroup(g, dgroup(), withTime);
    // The screen buffer: both pages.
    if (const auto buf = screenBuffer(); buf.size() == kScreenBytes) {
        for (int y = 0; y < Screen::kRows; ++y)
            std::copy_n(buf.begin() + static_cast<std::ptrdiff_t>(y) * Screen::kWidth, Screen::kWidth, g.video.screen().row(y));
    } else {
        missing.emplace_back("the screen buffer (g_screenBuffer DS:0248 does not lead into the RAM)");
    }
    // What the player saw: VRAM and the DAC.
    if (vga_) {
        std::copy(vga_->vram.begin(), vga_->vram.end(), g.video.vram().begin());
        Rgb6 dac{};
        std::copy(vga_->dac.begin(), vga_->dac.end(), dac.begin());
        g.video.dac().set(dac);
    } else {
        missing.emplace_back("VRAM and the DAC (the Vga entry has another layout)");
    }
    // The pointer: the driver's position (its logical x is g_mouseX) and the cursor's.
    const GgCursorFields c = cursor();
    g.mouse().driver().setLogicalPosition(c.mouseX, c.mouseY);
    g.video.cursor().setPosition(c.mouseX, c.mouseY);
    if (c.hidden) g.video.cursor().hide();
    else g.video.cursor().show();
    applyCursorUnder(g);
    return missing;
}

void GgStateFile::applyPalette(GgGame& g) const {
    const auto image = dgroup();
    Palette& pal = g.video.palette();
    // Pal_Save copies the shadow: the saved colours go through the shadow, then the shadow's own.
    std::copy_n(image.begin() + dg::kPalSaved, kPaletteBytes, pal.shadow().begin());
    pal.save();
    std::copy_n(image.begin() + dg::kPalShadow, kPaletteBytes, pal.shadow().begin());
    pal.setFaded(dsWord(dg::kPalFaded) != 0);
    if (vga_) {
        Rgb6 dac{};
        std::copy(vga_->dac.begin(), vga_->dac.end(), dac.begin());
        g.video.dac().set(dac);
    }
}

void GgStateFile::applyCursorUnder(GgGame& g) const {
    // g_mouseHideState DS:01e2 (store-only in the codec): 0 between a flush and the next drawing call,
    // where the mouse handler draws at once.
    if (dsWord(dg::kMouseHideState) == 0) g.video.screen().resetMouseHide();
    // The RAM screen copied above holds the cursor the original drew at its last flush. When
    // g_cursorShown DS:7a7c is set, the active copy gets the state's save-under (DS:7a7e: w, h, pixels;
    // at DS:7a6c / 7a6e), so the next Mouse_RestoreUnder puts the background back as the original's
    // would, instead of leaving the old arrow on the screen.
    const GgCursorFields c = cursor();
    if (c.shown == 0) return;
    const uint32_t buf = farPointerAt(dg::kCursorSaveBuf);
    const auto head = file_.bytesAt(buf, 4);
    if (buf == 0 || head.size() < 4) return;
    const int w = head[0] | (head[1] << 8), h = head[2] | (head[3] << 8);
    if (w <= 0 || h <= 0) return;
    const auto pixels = file_.bytesAt(buf + 4, static_cast<size_t>(w) * static_cast<size_t>(h));
    if (pixels.size() != static_cast<size_t>(w) * static_cast<size_t>(h)) return;
    CursorRecord& active = g.video.cursor().activeCopy();
    auto under = std::make_shared<SavedImage>();
    under->width = w;
    under->height = h;
    under->pixels.assign(pixels.begin(), pixels.end());
    active.saveUnder = std::move(under);
    active.saveX = c.savedX;
    active.saveY = c.savedY;
    active.width = w;
    active.height = h;
    active.shown = 1;
}

}  // namespace tmport::gg
