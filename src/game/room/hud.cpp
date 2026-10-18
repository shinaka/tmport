#include "game/room/hud.h"

#include "input/hotspot_table.h"
#include "res/resource_manager.h"
#include "text/text_renderer.h"
#include "util/log.h"

namespace tmport::game::room {

namespace {

constexpr size_t kDigitsAt = 3;  // after FF 05 FF

std::vector<uint8_t> withDigits(std::span<const uint8_t> tmpl, const std::string& digits) {
    std::vector<uint8_t> b(tmpl.begin(), tmpl.end());
    if (b.size() < kDigitsAt + digits.size() + 1) b.resize(kDigitsAt + digits.size() + 1, 0);
    for (size_t i = 0; i < digits.size(); ++i) b[kDigitsAt + i] = static_cast<uint8_t>(digits[i]);
    b[kDigitsAt + digits.size()] = 0;
    return b;
}

}  // namespace

std::vector<uint8_t> hudCounterText(std::span<const uint8_t> tmpl, uint16_t v) {
    std::string d;
    if (v < 10)
        d = std::string(1, static_cast<char>('0' + v));
    else
        d = {static_cast<char>('0' + (v / 10) % 10), static_cast<char>('0' + v % 10)};
    return withDigits(tmpl, d);
}

std::vector<uint8_t> hudPointsText(std::span<const uint8_t> tmpl, uint16_t v) {
    // < 10, < 100, < 1000, < 10000, else five digits with the top one (v / 10000) % 10.
    std::string d;
    int n = v < 10 ? 1 : v < 100 ? 2 : v < 1000 ? 3 : v < 10000 ? 4 : 5;
    uint32_t div = 1;
    for (int i = 1; i < n; ++i) div *= 10;
    for (int i = 0; i < n; ++i, div /= 10) d.push_back(static_cast<char>('0' + (v / div) % 10));
    return withDigits(tmpl, d);
}

// 10e8:0000 Hud_LoadArt
void Hud::loadArt(const ResourceManager& res) {
    const auto b = res.bundle(kHudBndl);
    if (!b) {
        log::warn("HUD: BNDL {} not found", kHudBndl);
        return;
    }
    for (size_t i = 0; i < art_.size(); ++i) art_[i] = i < b->ids.size() ? res.pixmap(b->ids[i]) : nullptr;
}

// 10e8:0019 Hud_FreeArt
void Hud::freeArt() {
    for (auto& a : art_) a.reset();
}

void Hud::setArt(int i, std::shared_ptr<const gfx::PixMap> pm) {
    if (i >= 0 && i < hud::kArtCount) art_[static_cast<size_t>(i)] = std::move(pm);
}

// 10e8:00dd Hud_DrawClueBoxMode(m): m = clueBoxMode - 1.
void Hud::drawClueBoxMode(int m) {
    fb_.setForeColor(hud::kClueBoxColour);
    fb_.fillRect(hud::kClueBoxX, hud::kClueBoxY, hud::kClueBoxW, hud::kClueBoxH);
    fb_.setForeColor(hud::kTextColour);
    if (m == 0) {
        text_.drawBox(hud::kClueBoxX, hud::kPromptY, hud::kClueBoxW, rt_.answerPrompt);
    } else if (m == 1) {
        if (const gfx::PixMap* c = art(hud::kArtContinue))
            fb_.drawSprite((hud::kClueBoxW - c->width) / 2 + hud::kClueBoxX,
                           (hud::kClueBoxH - c->height) / 2 + hud::kClueBoxY, *c, false);
    }
}

// 10e8:019f Hud_DrawClueTexts
void Hud::drawClueTexts(const GameState& s) {
    fb_.setForeColor(hud::kTextColour);
    const uint8_t level = s.currentLevel();
    if (level >= kLevelCount) return;  // the original would read past the per-level table
    for (int i = 0; i < s.inv.numClues && i < 3; ++i) {
        const std::string_view w = s.clues.text[level][static_cast<size_t>(i)];
        text_.draw(hud::kClueX, i * hud::kClueDy + hud::kClueY, text::asBytes(w));
    }
}

// 10e8:026e Hud_DrawKeyIcon
void Hud::drawKeyIcon(const GameState& s) {
    const uint8_t level = s.currentLevel();
    if (level >= 3) return;
    if (const gfx::PixMap* k = art((1 - s.inv.haveKey) + level * 2)) fb_.drawSprite(hud::kKeyX, hud::kKeyY, *k, false);
}

// 10e8:02cf Hud_DrawCoins (g_showCoins DS:0ec1 is always 1)
void Hud::drawCoins(const GameState& s) {
    const auto t = hudCounterText(rt_.tmplCoins, s.inv.coins);
    fb_.setForeColor(hud::kTextColour);
    text_.drawBox(hud::kCoinsX, hud::kCounterY, hud::kCoinsW, untilNul(t));
}

// 10e8:034d Hud_DrawNets (g_showNets DS:0ec2 is always 1)
void Hud::drawNets(const GameState& s) {
    const auto t = hudCounterText(rt_.tmplNets, s.inv.nets);
    fb_.setForeColor(hud::kTextColour);
    text_.drawBox(hud::kNetsX, hud::kCounterY, hud::kNetsW, untilNul(t));
}

// 10e8:03cb Hud_DrawTreasureGems
void Hud::drawTreasureGems(const GameState& s) {
    const uint8_t level = s.currentLevel();
    if (level >= 3) return;
    const size_t tier = s.rank.rankTier < 4 ? s.rank.rankTier : 3;
    const gfx::PixMap* filled = art(hud::kArtGemFilled);
    const gfx::PixMap* hollow = art(hud::kArtGemHollow);
    const int need = rt_.gemsNeeded[tier];
    int i = 0;
    // If gemsFound exceeded gemsNeeded the original would read past the row; never in play.
    for (; i < s.inv.gemsFound && i < 5; ++i)
        if (filled) fb_.drawSprite(rt_.gemX[tier][static_cast<size_t>(i)], rt_.gemY[tier][static_cast<size_t>(i)], *filled, false);
    for (; i < need && i < 5; ++i)
        if (hollow) fb_.drawSprite(rt_.gemX[tier][static_cast<size_t>(i)], rt_.gemY[tier][static_cast<size_t>(i)], *hollow, false);
}

// 10e8:04b2 Hud_DrawTreasurePoints
void Hud::drawTreasurePoints(const GameState& s) {
    const auto t = hudPointsText(rt_.tmplPoints, s.inv.climbPoints);
    fb_.setForeColor(hud::kTextColour);
    text_.drawBox(hud::kPointsX, hud::kCounterY, hud::kPointsW, untilNul(t));
}

// 10e8:0040 Hud_RedrawNear
void Hud::redraw(const GameState& s) {
    if (!hasArt()) return;
    fb_.setFullClip();
    fb_.setForeColor(hud::kLineColour);
    fb_.hline(0, hud::kLineY, 512);
    fb_.setForeColor(hud::kFillColour);
    fb_.fillRect(0, hud::kFillY, 512, hud::kFillH);
    fb_.drawOpaqueOrFlippedWhiteKey(hud::kPanelX, hud::kPanelY, *art(hud::kArtPanel), false);
    if (clueBoxMode_ == 0) {
        drawClueTexts(s);
        drawKeyIcon(s);
    } else {
        drawClueBoxMode(clueBoxMode_ - 1);
    }
    drawCoins(s);
    drawNets(s);
    drawTreasureGems(s);
    drawTreasurePoints(s);
    if (present_) present_(0, hud::kPresentY, 512, hud::kPresentH);
    fb_.setPlayClip(0);
}

// 10e8:0658 Hud_SetClueBoxMode
void Hud::setClueBoxMode(uint8_t mode, const GameState& s) {
    if (clueBoxMode_ == 0) {
        hot_.remove(kHotClueBox);
        hot_.remove(kHotFullScreen);
    } else if (clueBoxMode_ == 1 || clueBoxMode_ == 2) {
        hot_.remove(kHotFullScreen);
    }
    clueBoxMode_ = mode;
    if (mode == 0) {
        hot_.set(kHotClueBox, hud::kClueBoxX, hud::kClueBoxY, hud::kClueBoxW, hud::kClueBoxH);
        hot_.set(kHotFullScreen, 0, 0, 512, 384);
    } else if (mode == 1 || mode == 2) {
        hot_.set(kHotFullScreen, 0, 0, 512, 384);
    }
    redraw(s);
}

}  // namespace tmport::game::room
