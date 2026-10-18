#include "game/scenes/scene_hud.h"

#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "game/rules.h"
#include "platform/engine.h"
#include "res/resource_manager.h"
#include "text/text_renderer.h"

namespace tmport::game::scenes {

namespace {
constexpr int kHudArtBndl = 301;
constexpr uint16_t kShowCoins = 0x0EC1;  // DS:0ec1 g_showCoins (u8)
constexpr uint16_t kShowNets = 0x0EC2;   // DS:0ec2 g_showNets (u8)
constexpr uint16_t kClueHotspot = 99, kFullScreenHotspot = 0x409;
}  // namespace

HudStandIn::HudStandIn(AppContext& ctx, GameState& s, const GameTables& t) : ctx_(ctx), s_(s), t_(t) {
    const ResourceManager& res = *ctx_.files.res;
    if (const auto b = res.bundle(kHudArtBndl))
        for (int id : b->ids) art_.push_back(res.pixmap(id));
}

void HudStandIn::bind(SceneHooks& hooks) {
    hooks.setClueBoxMode = [this](int m) { setClueBoxMode(m); };
    hooks.hudRedraw = [this] { redraw(); };
}

void HudStandIn::setClueBoxMode(int mode) {
    HotspotTable& hs = ctx_.engine.hotspots();
    if (mode_ == 0) {
        hs.remove(kClueHotspot);
        hs.remove(kFullScreenHotspot);
    } else if (mode_ == 1 || mode_ == 2) {
        hs.remove(kFullScreenHotspot);
    }
    mode_ = mode;
    if (mode == 0) {
        hs.set(kClueHotspot, 10, 0x128, 0xBE, 0x4C);
        hs.set(kFullScreenHotspot, 0, 0, 512, 384);
    } else if (mode == 1 || mode == 2) {
        hs.set(kFullScreenHotspot, 0, 0, 512, 384);
    }
    redraw();
}

void HudStandIn::drawNumber(uint16_t tmplOff, size_t prefix, const std::string& digits, int x, int w) {
    const ExeTables& exe = *ctx_.files.exe;
    std::vector<uint8_t> b;
    for (size_t i = 0; i < prefix; ++i) b.push_back(exe.u8(tmplOff + static_cast<uint32_t>(i)));  // FF 05 FF
    for (char c : digits) b.push_back(static_cast<uint8_t>(c));
    ctx_.engine.frame().setForeColor(15);
    ctx_.text.drawBox(x, 0x153, w, b);
}

void HudStandIn::redraw() {
    gfx::FrameBuffer& fb = ctx_.engine.frame();
    const ExeTables& exe = *ctx_.files.exe;
    auto art = [&](size_t i) -> const gfx::PixMap* { return i < art_.size() && art_[i] ? art_[i].get() : nullptr; };
    fb.setFullClip();
    fb.setForeColor(0);
    fb.hline(0, 0x11F, 512);
    fb.setForeColor(8);
    fb.fillRect(0, 0x120, 512, 0x60);
    if (auto* p = art(6)) fb.drawOpaqueOrFlippedWhiteKey(5, 0x123, *p, false);
    const int level = s_.currentLevel();
    if (mode_ == 0) {
        fb.setForeColor(15);  // 10e8:019f Hud_DrawClueTexts
        if (level >= 0 && level < kLevelCount)
            for (int i = 0; i < s_.inv.numClues && i < 3; ++i) {
                const std::string_view w = s_.clues.text[static_cast<size_t>(level)][static_cast<size_t>(i)];
                ctx_.text.draw(0x11, 0x13B + 0x13 * i, {reinterpret_cast<const uint8_t*>(w.data()), w.size()});
            }
        if (level >= 0 && level < 3)  // 10e8:026e Hud_DrawKeyIcon
            if (auto* p = art(static_cast<size_t>(2 * level + (1 - (s_.inv.haveKey ? 1 : 0)))))
                fb.drawSprite(0x83, 0x128, *p, false);
    } else {
        fb.setForeColor(9);  // 10e8:00dd Hud_DrawClueBoxMode(mode - 1)
        fb.fillRect(10, 0x128, 0xBE, 0x4C);
        fb.setForeColor(15);
        if (mode_ == 1) {
            ctx_.text.drawBox(10, 0x136, 0xBE, exe.cstr(dg::kStrAnswerPrompt));
        } else if (mode_ == 2) {
            if (auto* p = art(9)) fb.drawSprite((0xBE - p->width) / 2 + 10, (0x4C - p->height) / 2 + 0x128, *p, false);
        }
    }
    if (exe.u8(kShowCoins)) drawNumber(dg::kTmplCoins, 3, hudTwoDigits(s_.inv.coins), 0x10F, 0x20);
    if (exe.u8(kShowNets)) drawNumber(dg::kTmplNets, 3, hudTwoDigits(s_.inv.nets), 0x169, 0x29);
    if (level >= 0 && level < 3) {  // 10e8:03cb Hud_DrawTreasureGems
        const int tier = s_.rank.rankTier & 3;
        const int needed = t_.gemsNeeded[static_cast<size_t>(tier)];
        for (int i = 0; i < needed && i < 5; ++i) {
            const auto* p = art(i < s_.inv.gemsFound ? 7 : 8);
            if (!p) continue;
            const uint32_t o = static_cast<uint32_t>(tier * 10 + i * 2);
            fb.drawSprite(exe.i16(dg::kGemX + o), exe.i16(dg::kGemY + o), *p, false);
        }
    }
    drawNumber(dg::kTmplPoints, 3, hudPointsDigits(s_.inv.climbPoints), 0x1CF, 0x29);
    ctx_.engine.presenter().blitToWindow(fb, 0, 0x11F, 512, 0x61);
    fb.setPlayClip(0);
}

}  // namespace tmport::game::scenes
