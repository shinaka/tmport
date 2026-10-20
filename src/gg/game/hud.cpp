#include "gg/game/hud.h"

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/data/resources.h"
#include "gg/game/pic_cache.h"
#include "gg/gfx/lpx.h"
#include "gg/gfx/screen.h"
#include "gg/text/font.h"
#include "gg/text/text.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr int kTop = 165;           // 0xa5
constexpr int kHeight = 35;         // 0x23
constexpr uint8_t kBarColour = 0x16;
constexpr uint8_t kRimColour = 0x17;
constexpr int kLabelX = 218;        // 0xda
constexpr int kInfoWidth = 102;     // 0x66
constexpr int kDigitStep = 11;

std::span<const uint8_t> pictureAt(const std::vector<std::span<const uint8_t>>& v, size_t i) {
    return i < v.size() ? v[i] : std::span<const uint8_t>{};
}

}  // namespace

// 1a04:025b Hud_Init
void Hud::init(const GgExe& exe, const Archive& archive, PicCache& cache) {
    sprites_ = cache.loadBundle(kSpriteBundle);
    digitCopy_ = archive.read(kDigitBundle);  // Res_LoadCopy
    const auto members = parseBundle(digitCopy_);
    for (size_t i = 0; i < digits_.size() && i < members.size(); ++i)
        digits_[i] = std::span<uint8_t>(digitCopy_.data() + members[i].offset + 2, members[i].payload.size());
    for (size_t s = 0; s < triples_.size(); ++s)
        for (size_t k = 0; k < 4; ++k) triples_[s][k] = exe.ds8(static_cast<uint16_t>(dg::kHudDigitColors + 4 * s + k));
    for (size_t k = 0; k < pow10_.size(); ++k) pow10_[k] = exe.ds32(static_cast<uint16_t>(dg::kPow10 + 4 * k));
    st_->colorScheme = exe.dsS16(dg::kHudColorScheme);
    pictureScheme_ = st_->colorScheme;  // the copy's digits are in the scheme DS:1318 names
    st_->dirty = 0;
    st_->labelsShown = 1;
}

HudCounters Hud::shown() const {
    return HudCounters{st_->shownPartsNeeded, st_->shownProjectiles, st_->shownScore};
}

// 1a04:00eb Hud_DrawBar
void Hud::drawBar(Screen& screen) const {
    screen.setDrawColor(kBarColour);
    screen.fillRect(0, kTop, 320, kHeight);
    screen.setDrawColor(kRimColour);
    screen.fillRect(0, kTop, 320, 2);
}

// 1a04:011c Hud_ClearInfo
void Hud::clearInfo(Screen& screen) const {
    screen.setDrawColor(kBarColour);
    screen.fillRect(kLabelX, kTop, kInfoWidth, kHeight);
    screen.setDrawColor(kRimColour);
    screen.fillRect(kLabelX, kTop, kInfoWidth, 2);
}

// 1a04:018e Hud_DrawLabels
void Hud::drawLabels(Screen& screen, unsigned bits) const {
    if (bits & 4) screen.lpxDraw(kLabelX, 166, pictureAt(sprites_, 0));
    if (bits & 2) screen.lpxDraw(kLabelX, 177, pictureAt(sprites_, 1));
    if (bits & 1) screen.lpxDraw(kLabelX, 188, pictureAt(sprites_, 2));
}

// 1a04:000e Hud_SetDigitColors: nothing when the scheme is in effect, else the ten pictures recoloured
// from the scheme in effect and DS:1318 = scheme. "In effect" is the scheme the engine's pictures are in,
// which is DS:1318 as in the original unless DS:1318 was loaded from a state without syncDigitColors.
void Hud::setDigitColors(int scheme) {
    const int current = pictureScheme_;
    if (scheme == current) {
        st_->colorScheme = static_cast<int16_t>(scheme);
        return;
    }
    if (scheme < 0 || static_cast<size_t>(scheme) >= triples_.size() || current < 0 ||
        static_cast<size_t>(current) >= triples_.size()) {
        log::debug("Hud_SetDigitColors: scheme {} -> {} outside the table", current, scheme);
        return;
    }
    for (auto& d : digits_) recolorPicture(d, triples_[static_cast<size_t>(current)], triples_[static_cast<size_t>(scheme)]);
    pictureScheme_ = scheme;
    st_->colorScheme = static_cast<int16_t>(scheme);
}

void Hud::syncDigitColors() {
    const int want = st_->colorScheme;
    if (want == pictureScheme_) return;
    if (want < 0 || static_cast<size_t>(want) >= triples_.size() || pictureScheme_ < 0 ||
        static_cast<size_t>(pictureScheme_) >= triples_.size()) {
        log::debug("Hud: digit scheme {} -> {} outside the table", pictureScheme_, want);
        return;
    }
    for (auto& d : digits_)
        recolorPicture(d, triples_[static_cast<size_t>(pictureScheme_)], triples_[static_cast<size_t>(want)]);
    pictureScheme_ = want;
}

// 1a04:0058 Hud_DrawNumber
void Hud::drawNumber(Screen& screen, int x, int y, uint32_t value, int digits, int scheme) {
    setDigitColors(scheme);
    if (digits <= 0 || static_cast<size_t>(digits) >= pow10_.size()) return;
    uint32_t div = pow10_[static_cast<size_t>(digits)];
    for (int i = 0; i < digits; ++i, x += kDigitStep) {
        const uint32_t d = div ? value / div : 0;
        value = div ? value % div : value;
        if (d < digits_.size()) screen.lpxDraw(x, y, digits_[d]);
        else log::debug("Hud_DrawNumber: digit {} past the table", d);
        div /= 10;
    }
}

// 1a04:02e9 Hud_DrawCounters
void Hud::drawCounters(Screen& screen, const HudCounters& c) {
    drawNumber(screen, 0x129, 0xA6, static_cast<uint32_t>(static_cast<uint16_t>(c.partsNeeded)), 2, 1);
    drawNumber(screen, 0x11E, 0xB1, static_cast<uint32_t>(static_cast<uint16_t>(c.projectiles)), 3, 0);
    drawNumber(screen, 0xFD, 0xBC, c.score, 6, 2);
    st_->shownPartsNeeded = static_cast<int16_t>(c.partsNeeded);
    st_->shownProjectiles = static_cast<int16_t>(c.projectiles);
    st_->shownScore = c.score;
}

// 1a04:02c2 Hud_DrawAll
void Hud::drawAll(Screen& screen, unsigned bits) {
    drawBar(screen);
    drawLabels(screen, bits);
    screen.saveToBack(0, kTop, 320, kHeight);
}

// 1a04:0588 Hud_DrawStrip
void Hud::drawStrip(Screen& screen, std::span<const HudStripEntry> strip) const {
    screen.setDrawColor(kBarColour);
    screen.fillRect(20, 167, 157, 33);
    for (const HudStripEntry& e : strip)
        if (e.sprite.x != -1) screen.lpxDrawKeyed(e.sprite.x, e.sprite.y, e.sprite.picture, 0);
    screen.lpxDrawKeyed(191, 172, pictureAt(sprites_, 3), 0);
    screen.lpxDrawKeyed(2, 173, pictureAt(sprites_, 4), 0);
    screen.lpxDrawKeyed(178, 173, pictureAt(sprites_, 4), 2);
}

// 1a04:0a11 Hud_DrawInfoText
void Hud::drawInfoText(Screen& screen, Fonts& fonts, std::span<const uint8_t> text) {
    const int old = fonts.select(1);
    st_->labelsShown = 0;
    clearInfo(screen);
    fonts.setColor(0);
    textDrawBox(screen, fonts, kLabelX, 168, kInfoWidth, text);
    fonts.select(old);
}

// 1a04:0b69 Hud_Update (without the flash and the label return)
uint16_t Hud::update(Screen& screen, uint16_t skip, const HudCounters& c, int displayRoom,
                     std::span<const HudStripEntry> strip, Fonts* fonts, std::span<const uint8_t> infoText) {
    uint16_t& dirty = st_->dirty;
    dirty &= static_cast<uint16_t>(~skip);
    if (dirty == 0) return 0;
    const uint16_t was = dirty;
    if (dirty & kFull) {
        drawBar(screen);
        drawLabels(screen, 7);
        drawCounters(screen, c);
        if (displayRoom != 0) drawStrip(screen, strip);
        dirty = 0;
    }
    if (dirty & kBar) {
        drawBar(screen);
        drawLabels(screen, 1);
        dirty = kScore;
    }
    if (dirty & kStrip) {
        drawStrip(screen, strip);
        dirty &= static_cast<uint16_t>(~kStrip);
    }
    if (dirty & kBananas) {
        drawNumber(screen, 0x11E, 0xB1, static_cast<uint32_t>(static_cast<uint16_t>(c.projectiles)), 3, 0);
        dirty &= static_cast<uint16_t>(~kBananas);
    }
    if (dirty & kInfoText) {
        if (fonts) drawInfoText(screen, *fonts, infoText);
        dirty &= static_cast<uint16_t>(~kInfoText);
    }
    if (dirty & kPartsNeeded) {
        drawNumber(screen, 0x129, 0xA6, static_cast<uint32_t>(static_cast<uint16_t>(c.partsNeeded)), 2, 1);
        dirty &= static_cast<uint16_t>(~kPartsNeeded);
    }
    if (dirty & kScore) {
        drawNumber(screen, 0xFD, 0xBC, c.score, 6, 2);
        dirty &= static_cast<uint16_t>(~kScore);
    }
    screen.saveToBack(0, kTop, 320, 16);
    return was;
}

}  // namespace tmport::gg
