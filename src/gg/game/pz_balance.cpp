#include "gg/game/pz_balance.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/game.h"
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
    for (int16_t& s : st_->stock) s = s16(table_, k), k += 2;
    for (size_t h = 0; h < st_->hookWeights.size(); ++h)
        for (int16_t& w : st_->hookWeights[h]) {
            if (h < 5) {
                w = -1;
            } else {
                w = s16(table_, k);
                k += 2;
            }
        }
    variant_ = variant;
}

void PzBalance::loadResources(int variant) {
    hintPressed_ = archive_.read(kHintPressed);
    hintText_ = puzzleText(archive_, static_cast<uint16_t>(kHintTextFirst + variant));
    panelText_ = puzzleText(archive_, kPanelText);
    counts_ = Bundle::load(archive_, kCountPics);
    right_ = Bundle::load(archive_, kRightPics);
    left_ = Bundle::load(archive_, kLeftPics);
    carry_ = Bundle::load(archive_, kCarryPics);
    ring_ = archive_.read(kRingPic);
    stack_ = archive_.read(kStackPic);
    table_ = archive_.read(kVariantTable);
}

void PzBalance::flush(Video& video) {
    if (game_) game_->display.flush();
    else video.flush();
}

// 32fe:0aa0 PzBalance_Run, up to PzBalance_Loop (a tool's picture of it)
void PzBalance::setup(Video& video, Fonts& fonts, int variant) {
    game_ = nullptr;
    BalanceState& s = *st_;
    s.held = -1;
    s.tilt = -5;
    s.zoneSet = -1;
    s.solved = 0;
    loadResources(variant);
    loadVariant(variant);
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    video.cursor().setCursorPicture(cursor_.get());
    drawPuzzlePicture(video, archive_, puzzleKindInfo(4).picture, puzzleKindInfo(4).palette);
    PuzzlePanel panel;
    panel.init(video.screen(), fonts, archive_, panelText_);
    video.screen().saveToBack(0, 0, 320, 200);
    drawInventory(video);
    drawBeam(video);
    puzzleFadeIn(video);
}

// 32fe:0235 PzBalance_DrawInventory
void PzBalance::drawInventory(Video& video) {
    Screen& screen = video.screen();
    screen.restoreRect(8, 67, 38, 93);
    for (size_t t = 0; t < st_->stock.size(); ++t) {
        const int n = st_->stock[t];
        if (n <= 0) continue;
        screen.lpxDrawKeyed(types_[t].picX, types_[t].picY, memberOf(carry_, static_cast<int>(t)), 0);
        screen.lpxDrawKeyed(types_[t].countX, types_[t].countY, memberOf(counts_, n), 0);
    }
    flush(video);
}

// 32fe:02c8 PzBalance_DrawHook
void PzBalance::drawHook(Screen& screen, int x, int y, const std::array<int16_t, 3>& weights, bool right) const {
    const Bundle& pics = right ? right_ : left_;
    int n = 0;
    for (; n < kSlots; ++n) {
        const int w = weights[static_cast<size_t>(n)];
        if (w == -1) break;
        screen.lpxDrawKeyed(x, y, ring_, 0);
        const int dx = w >= 0 && w < kTypes ? dx_[static_cast<size_t>(w)] : 0;
        screen.lpxDrawKeyed(x + dx, y + 2, memberOf(pics, w), 0);
        y += 15;
    }
    // On the left side, below the last of 1 or 2 weights, the hook where another one can go.
    if (n > 0 && n < kSlots && !right) screen.lpxDrawKeyed(x - 1, y, stack_, 0);
}

// 32fe:0375 PzBalance_DrawBeam
void PzBalance::drawBeam(Video& video) {
    BalanceState& s = *st_;
    // The hook zones follow the beam only at the resting tilts -5, 0 and +5 (32fe:0079).
    auto select = [&](int set) {
        if (game_) selectHookZones(*game_, set, true);
        else s.zoneSet = static_cast<int16_t>(set);
    };
    if (s.tilt > 4) {
        select(1);
        s.tilt = 5;
    } else if (s.tilt < -4) {
        select(0);
        s.tilt = -5;
    } else if (s.tilt == 0) {
        select(2);
    }
    const int t = s.tilt + 5;
    const auto beam = archive_.read(static_cast<uint16_t>(kBeamFirst + t));  // Res_Read(tilt + 0x2123)
    video.screen().lpxDrawKeyed(53, 14, beam, 0);
    for (int h = 0; h < kHooks; ++h) {
        const auto [x, y] = hookPos(t, h);
        drawHook(video.screen(), x, y, s.hookWeights[static_cast<size_t>(h)], h > 4);
    }
    flush(video);
}

std::string PzBalance::describe() const {
    int weights = 0;
    for (const int s : st_->stock) weights += s;
    return std::format("balance variant {}: {} weights in the inventory", variant_, weights);
}

}  // namespace tmport::gg
