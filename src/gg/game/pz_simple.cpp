#include "gg/game/pz_simple.h"

#include <algorithm>
#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/game.h"
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
        fitDx_[t] = exe.dsS16(static_cast<uint16_t>(dg::kSimpleFitDx + 2 * t));
        fitDy_[t] = exe.dsS16(static_cast<uint16_t>(dg::kSimpleFitDy + 2 * t));
    }
}

int PzSimple::count(int type) const {
    const auto& d = st_->data;
    return type >= 0 && type < kTypes && static_cast<size_t>(type) < d.size() ? static_cast<int8_t>(d[static_cast<size_t>(type)]) : 0;
}

int16_t PzSimple::recordWord(int r, size_t off) const { return s16(st_->data, 0x0C + 0x8A * static_cast<size_t>(r) + off); }

void PzSimple::setRecordWord(int r, size_t off, int16_t v) {
    const size_t o = 0x0C + 0x8A * static_cast<size_t>(r) + off;
    if (o + 2 > st_->data.size()) return;
    st_->data[o] = static_cast<uint8_t>(v);
    st_->data[o + 1] = static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8);
}

// 3938:0ed8 PzSimple_LoadResources
void PzSimple::loadResources(int variant) {
    labels_ = Bundle::load(archive_, kLabelCursors);
    digits_ = Bundle::load(archive_, kPuzzleDigits);
    machine_ = Bundle::load(archive_, static_cast<uint16_t>(kMachineFirst + variant));
    parts_ = Bundle::load(archive_, kPartPictures);
    for (int t = 0; t < kTypes; ++t) partBundles_[static_cast<size_t>(t)] = Bundle::load(archive_, static_cast<uint16_t>(kPartBundleFirst + t));
    checkPressed_ = archive_.read(kCheckPressed);
    hintPressed_ = archive_.read(kHintPressed);
    hintTexts_ = Bundle::load(archive_, kHintTexts);
    panelText_ = puzzleText(archive_, kPanelText);
    hintHeader_ = puzzleText(archive_, kHintHeader);
}

// 3938:0dfe PzSimple_LoadVariant: Res_LoadTo(0x206d + v) into a 0x570-byte block; 10 empty slots.
void PzSimple::loadVariant(int variant) {
    auto& data = st_->data;
    data = archive_.read(static_cast<uint16_t>(kVariantFirst + variant));
    if (data.size() < 0x0C) throw FormatError(std::format("simple machines: F {:04x} is {} bytes", kVariantFirst + variant, data.size()));
    data.resize(std::max(data.size(), kDataSize));
    for (SimplePlacedRec& p : st_->placed) p = SimplePlacedRec{};
    variant_ = variant;
}

// 3938:0000 PzSimple_Run, up to the loop (a tool's picture of it)
void PzSimple::setup(Video& video, Fonts& fonts, int variant) {
    game_ = nullptr;
    st_->held = kNone;
    loadResources(variant);
    loadVariant(variant);
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    screenRegion_ = cursorRegionAt(exe_, dg::kSimpleScreenRegion, cursor_.get());
    video.cursor().addRegion(&screenRegion_);  // MouseRegion_Add(DS:338e)
    // 3938:0e76 PzSimple_DrawScreen
    drawPuzzlePicture(video, archive_, puzzleKindInfo(1).picture, puzzleKindInfo(1).palette);
    video.screen().lpxDrawKeyed(s16(st_->data, 8), s16(st_->data, 10), memberOf(machine_, 0), 0);
    PuzzlePanel panel;
    panel.init(video.screen(), fonts, archive_, panelText_);
    video.screen().saveToBack(0, 0, 320, 200);
    puzzleFadeIn(video);
    // 3938:04a1 PzSimple_InitCursorRegions: a region per slot over its icon with label cursor t.
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

void PzSimple::flush(Video& video) {
    if (game_) game_->display.flush();
    else video.flush();
}

// 3938:0d63 PzSimple_DrawInventory
void PzSimple::drawInventory(Video& video) {
    Screen& screen = video.screen();
    screen.restoreRect(4, 0x31, 0x30, 0x62);
    for (int t = 0; t < kTypes; ++t) {
        const int n = count(t);
        if (n <= 0) continue;
        const Layout& l = layout_[static_cast<size_t>(t)];
        screen.lpxDrawKeyed(l.iconX, l.iconY, memberOf(partBundles_[static_cast<size_t>(t)], 0), 0);
        screen.lpxDrawKeyed(l.digitX, l.digitY, memberOf(digits_, n), 0);
    }
    flush(video);
}

// 3938:040f PzSimple_DrawPlaced
void PzSimple::drawPlaced(Video& video, bool doFlush) {
    Screen& screen = video.screen();
    screen.restoreRect(66, 14, 245, 142);
    for (const SimplePlacedRec& p : st_->placed)
        if (p.type != kNone) screen.lpxDrawKeyed(p.x, p.y, memberOf(parts_, p.type), 0);
    if (doFlush) flush(video);
}

std::string PzSimple::describe() const {
    int parts = 0;
    for (int t = 0; t < kTypes; ++t) parts += count(t);
    return std::format("simple machines variant {}: {} parts in the inventory", variant_, parts);
}

}  // namespace tmport::gg
