#include "gg/game/pz_simple.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/gfx/video.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

constexpr size_t kDataSize = 0x570;

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

}  // namespace

PzSimple::PzSimple(const GgExe& exe, const Archive& archive) : exe_(exe), archive_(archive) {
    for (size_t t = 0; t < layout_.size(); ++t) {
        const auto o = static_cast<uint16_t>(dg::kSimplePartLayout + dg::kSimplePartLayoutStride * t);
        layout_[t] = {exe.dsS16(o), exe.dsS16(o + 2), exe.dsS16(o + 5), exe.dsS16(o + 7),
                      exe.dsS16(o + 9), exe.dsS16(o + 0x0B), exe.dsS16(o + 0x0D)};
    }
}

int PzSimple::count(int type) const {
    return type >= 0 && type < kTypes && static_cast<size_t>(type) < data_.size() ? static_cast<int8_t>(data_[static_cast<size_t>(type)]) : 0;
}

// 3938:0ed8 PzSimple_LoadResources
void PzSimple::loadResources(int variant) {
    labels_ = Bundle::load(archive_, kLabelCursors);
    digits_ = Bundle::load(archive_, kPuzzleDigits);
    machine_ = Bundle::load(archive_, static_cast<uint16_t>(kMachineFirst + variant));
    parts_ = Bundle::load(archive_, kPartPictures);
    for (int t = 0; t < kTypes; ++t) partBundles_[static_cast<size_t>(t)] = Bundle::load(archive_, static_cast<uint16_t>(kPartBundleFirst + t));
    panelText_ = puzzleText(archive_, kPanelText);
}

// 3938:0dfe PzSimple_LoadVariant: Res_LoadTo(0x206d + v) into a 0x570-byte block; 10 empty slots.
void PzSimple::loadVariant(int variant) {
    data_ = archive_.read(static_cast<uint16_t>(kVariantFirst + variant));
    if (data_.size() < 0x0C) throw FormatError(std::format("simple machines: F {:04x} is {} bytes", kVariantFirst + variant, data_.size()));
    data_.resize(std::max(data_.size(), kDataSize));
    for (Placed& p : placed_) p = Placed{};
    variant_ = variant;
}

// 3938:0000 PzSimple_Run, up to the loop
void PzSimple::setup(Video& video, Fonts& fonts, int variant) {
    // TODO(wave 3): PzSnd_LoadBank(0x11ca) is the audio area's.
    held = kNone;
    loadResources(variant);
    loadVariant(variant);
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    screenRegion_ = cursorRegionAt(exe_, dg::kSimpleScreenRegion, cursor_.get());
    video.cursor().addRegion(&screenRegion_);  // MouseRegion_Add(DS:338e)
    // 3938:0e76 PzSimple_DrawScreen
    drawPuzzlePicture(video, archive_, puzzleKindInfo(1).picture, puzzleKindInfo(1).palette);
    video.screen().lpxDrawKeyed(s16(data_, 8), s16(data_, 10), memberOf(machine_, 0), 0);
    PuzzlePanel panel;
    panel.init(video.screen(), fonts, archive_, panelText_);
    video.screen().saveToBack(0, 0, 320, 200);
    puzzleFadeIn(video);
    // 3938:0b59 PzSimple_BuildHintText (the slot zones; the text itself is the hint viewer's) and
    // 3938:04a1 PzSimple_InitCursorRegions: a region per slot over its icon with label cursor t.
    // TODO(wave 3): the slot, work, Hint and check zones are the runtime's zone list.
    for (int t = 0; t < kTypes; ++t) {
        const auto icon = memberOf(partBundles_[static_cast<size_t>(t)], 0);
        labelCursors_[static_cast<size_t>(t)] = CursorRecord::fromPicture(memberOf(labels_, t));
        slotRegions_[static_cast<size_t>(t)] = {0, layout_[static_cast<size_t>(t)].iconX, layout_[static_cast<size_t>(t)].iconY,
                                                s16(icon, 6), s16(icon, 8), 0, labelCursors_[static_cast<size_t>(t)].get()};
    }
    // 3938:0524 PzSimple_AddCursorRegions
    for (int t = 0; t < kTypes; ++t)
        if (count(t) > 0) video.cursor().addRegion(&slotRegions_[static_cast<size_t>(t)]);
    drawInventory(video);
}

// 3938:0d63 PzSimple_DrawInventory
void PzSimple::drawInventory(Video& video) const {
    Screen& screen = video.screen();
    screen.restoreRect(4, 0x31, 0x30, 0x62);
    for (int t = 0; t < kTypes; ++t) {
        const int n = count(t);
        if (n <= 0) continue;
        const Layout& l = layout_[static_cast<size_t>(t)];
        screen.lpxDrawKeyed(l.iconX, l.iconY, memberOf(partBundles_[static_cast<size_t>(t)], 0), 0);
        screen.lpxDrawKeyed(l.digitX, l.digitY, memberOf(digits_, n), 0);
    }
    video.flush();
}

// 3938:040f PzSimple_DrawPlaced
void PzSimple::drawPlaced(Video& video, bool flush) const {
    Screen& screen = video.screen();
    screen.restoreRect(66, 14, 245, 142);
    for (const Placed& p : placed_)
        if (p.type != kNone) screen.lpxDrawKeyed(p.x, p.y, memberOf(parts_, p.type), 0);
    if (flush) video.flush();
}

std::string PzSimple::describe() const {
    int parts = 0;
    for (int t = 0; t < kTypes; ++t) parts += count(t);
    return std::format("simple machines variant {}: {} parts in the inventory", variant_, parts);
}

}  // namespace tmport::gg
