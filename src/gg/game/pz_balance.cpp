#include "gg/game/pz_balance.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/gfx/cursor.h"
#include "gg/gfx/video.h"
#include "gg/text/font.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

}  // namespace

PzBalance::PzBalance(const GgExe& exe, const Archive& archive) : exe_(exe), archive_(archive) {
    for (size_t t = 0; t < types_.size(); ++t) {
        const auto o = static_cast<uint16_t>(dg::kBalanceWeights + dg::kBalanceWeightsStride * t);
        types_[t] = {exe.dsS16(o), exe.dsS16(o + 2), exe.dsS16(o + 4), exe.dsS16(o + 6), exe.dsS16(o + 8),
                     exe.dsS16(o + 0x0A), exe.dsS16(o + 0x0C), exe.dsS16(o + 0x0E), exe.dsS16(o + 0x10)};
        dx_[t] = exe.dsS16(static_cast<uint16_t>(dg::kBalanceWeightDx + 2 * t));
    }
}

std::pair<int, int> PzBalance::hookPos(int t, int h) const {
    const auto o = static_cast<uint16_t>(dg::kBalanceHooks + dg::kBalanceHooksStride * static_cast<size_t>(t) + 4 * static_cast<size_t>(h));
    return {exe_.dsS16(o), exe_.dsS16(o + 2)};
}

// 32fe:0000 PzBalance_LoadVariant
void PzBalance::loadVariant(int variant) {
    const size_t o = 2 + 42 * static_cast<size_t>(variant);
    if (o + 42 > table_.size()) throw FormatError(std::format("balance table: no variant {}", variant));
    size_t k = o;
    for (int& s : stock_) s = s16(table_, k), k += 2;
    for (size_t h = 0; h < hooks_.size(); ++h)
        for (int& w : hooks_[h]) {
            if (h < 5) {
                w = -1;
            } else {
                w = s16(table_, k);
                k += 2;
            }
        }
    variant_ = variant;
}

// 32fe:0aa0 PzBalance_Run, up to PzBalance_Loop
void PzBalance::setup(Video& video, Fonts& fonts, int variant) {
    held = -1;
    tilt = -5;
    zoneSet = -1;
    // TODO(wave 3): PzSnd_LoadBank(0x11c6) is the audio area's.
    panelText_ = puzzleText(archive_, kPanelText);
    counts_ = Bundle::load(archive_, kCountPics);
    right_ = Bundle::load(archive_, kRightPics);
    left_ = Bundle::load(archive_, kLeftPics);
    carry_ = Bundle::load(archive_, kCarryPics);
    ring_ = archive_.read(kRingPic);
    stack_ = archive_.read(kStackPic);
    table_ = archive_.read(kVariantTable);
    loadVariant(variant);
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    video.cursor().setCursorPicture(cursor_.get());
    // TODO(wave 3): the zones DS:27fc, DS:2812, DS:2828 and PzBalance_InitZones (the runtime's
    // zone list) and Hint_Show(0x1052) (the UI's).
    drawPuzzlePicture(video, archive_, puzzleKindInfo(4).picture, puzzleKindInfo(4).palette);
    PuzzlePanel panel;
    panel.init(video.screen(), fonts, archive_, panelText_);
    video.screen().saveToBack(0, 0, 320, 200);
    drawInventory(video);
    drawBeam(video);
    puzzleFadeIn(video);
}

// 32fe:0235 PzBalance_DrawInventory
void PzBalance::drawInventory(Video& video) const {
    Screen& screen = video.screen();
    screen.restoreRect(8, 67, 38, 93);
    for (size_t t = 0; t < stock_.size(); ++t) {
        if (stock_[t] <= 0) continue;
        screen.lpxDrawKeyed(types_[t].picX, types_[t].picY, memberOf(carry_, static_cast<int>(t)), 0);
        screen.lpxDrawKeyed(types_[t].countX, types_[t].countY, memberOf(counts_, stock_[t]), 0);
    }
    video.flush();
}

// 32fe:02c8 PzBalance_DrawHook
void PzBalance::drawHook(Screen& screen, int x, int y, const std::array<int, 3>& weights, bool right) const {
    const Bundle& pics = right ? right_ : left_;
    int n = 0;
    for (; n < 3; ++n) {
        const int w = weights[static_cast<size_t>(n)];
        if (w == -1) break;
        screen.lpxDrawKeyed(x, y, ring_, 0);
        const int dx = w >= 0 && w < 6 ? dx_[static_cast<size_t>(w)] : 0;
        screen.lpxDrawKeyed(x + dx, y + 2, memberOf(pics, w), 0);
        y += 15;
    }
    if (n > 0 && n < 3 && !right) screen.lpxDrawKeyed(x - 1, y, stack_, 0);
}

// 32fe:0375 PzBalance_DrawBeam
void PzBalance::drawBeam(Video& video) {
    // PzBalance_SelectHookZones (32fe:0079): the zones of set 0 (tilt -5), 1 (+5), 2 (level) are the
    // runtime's; the set is remembered.
    if (tilt >= 5) {
        zoneSet = 1;
        tilt = 5;
    } else if (tilt < -4) {
        zoneSet = 0;
        tilt = -5;
    } else if (tilt == 0) {
        zoneSet = 2;
    }
    const int t = tilt + 5;
    const auto beam = archive_.read(static_cast<uint16_t>(kBeamFirst + t));  // Res_Read(tilt + 0x2123)
    video.screen().lpxDrawKeyed(53, 14, beam, 0);
    for (int h = 0; h < 10; ++h) {
        const auto [x, y] = hookPos(t, h);
        drawHook(video.screen(), x, y, hooks_[static_cast<size_t>(h)], h > 4);
    }
    video.flush();
}

std::string PzBalance::describe() const {
    int weights = 0;
    for (const int s : stock_) weights += s;
    return std::format("balance variant {}: {} weights in the inventory", variant_, weights);
}

}  // namespace tmport::gg
