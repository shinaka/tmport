#include "gg/game/pz_launch.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/game.h"
#include "gg/gfx/cursor.h"
#include "gg/gfx/video.h"
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

PzLaunch::PzLaunch(const GgExe& exe, const Archive& archive) : exe_(exe), archive_(archive) {}

int PzLaunch::dsS16(uint16_t off, int i) const { return exe_.dsS16(static_cast<uint16_t>(off + 2 * i)); }

// 3797:0464 PzLaunch_LoadResources
void PzLaunch::loadResources(int variant) {
    plunger_ = Bundle::load(archive_, kPlunger);
    gravityPics_ = Bundle::load(archive_, kGravityPics);
    ramps_[0] = Bundle::load(archive_, kRampsSurface0);
    ramps_[1] = Bundle::load(archive_, kRampsSurface1);
    ramps_[2] = Bundle::load(archive_, kRampsSurface2);
    for (size_t o = 0; o < kObjects.size(); ++o) objects_[o] = Bundle::load(archive_, kObjects[o]);
    digits_ = Bundle::load(archive_, kPuzzleDigits);
    for (size_t t = 0; t < kTools.size(); ++t) tools_[t] = archive_.read(kTools[t]);
    hintPressed_ = archive_.read(kHintPressed);
    hintText_ = puzzleText(archive_, static_cast<uint16_t>(kHintTextFirst + variant));
    panelText_ = puzzleText(archive_, static_cast<uint16_t>(kPanelTextFirst + variant));
    cursor_ = loadCursor(archive_, kPuzzleCursor);
}

void PzLaunch::drawFlush(Video& video) {
    if (game_) game_->display.flush();
    else video.flush();
}

// 3797:030c PzLaunch_LoadVariant
void PzLaunch::loadVariant(int variant) {
    if (variant < 0 || static_cast<size_t>(variant) >= dg::kLaunchVariantsCount)
        throw std::out_of_range(std::format("launch variant {}", variant));
    for (int i = 0; i < kWords; ++i)
        words_[static_cast<size_t>(i)] = exe_.ds16(static_cast<uint16_t>(
            dg::kLaunchVariants + dg::kLaunchVariantsStride * static_cast<size_t>(variant) + 2 * static_cast<size_t>(i)));
    variant_ = variant;
}

// 3797:1138 PzLaunch_Run, up to the loop (a tool's picture of it)
void PzLaunch::setup(Video& video, Fonts& fonts, int variant) {
    game_ = nullptr;
    LaunchState& l = *st_;
    l.objectShown = 0;
    loadResources(variant);
    l.glowFrame = exe_.dsS16(dg::kLaunchGlowFrame);  // DS:3304 as the file has it
    // 3797:040d PzLaunch_DrawScreen
    drawPuzzlePicture(video, archive_, puzzleKindInfo(5).picture, puzzleKindInfo(5).palette);
    PuzzlePanel panel;
    panel.init(video.screen(), fonts, archive_, panelText_);
    video.screen().saveToBack(0, 0, 320, 200);
    video.cursor().setCursorPicture(cursor_.get());
    puzzleFadeIn(video);
    // 3797:030c PzLaunch_LoadVariant
    loadVariant(variant);
    l.gravity = 0;
    setGravity(word(9));
    drawGravity(video);
    setEnergy(0);
    setTarget(word(10));
    l.glowOn = 1;
    glow_ = Bundle::load(archive_, static_cast<uint16_t>(kGlowFirst + l.target));
    l.ramp = static_cast<int16_t>(word(0));
    setOffer(0, word(1));
    setOffer(1, word(2));
    setOffer(2, word(3));
    setOffer(3, word(4));
    setSurface(1);
    selectObject(-1);
    setOffer(4, word(7));
    setOffer(7, word(6));
    setOffer(6, word(5));
    setOffer(5, word(8));
    drawBox(video.screen());
    redraw(video);
}

// 3797:087d PzLaunch_SetGravity
void PzLaunch::setGravity(int g) {
    LaunchState& l = *st_;
    if (l.gravity == -1) return;
    l.gravity = static_cast<int16_t>(g);
    if (g == 2) l.gravityPic = 1;
    else if (g == 1) l.gravityPic = 2;
    else if (g == 0) l.gravityPic = 3;
}

// 3797:08b6 PzLaunch_DrawGravity
void PzLaunch::drawGravity(Video& video) {
    video.screen().lpxDrawKeyed(4, 0x87, memberOf(gravityPics_, st_->gravity == -1 ? 0 : st_->gravityPic), 0);
    drawFlush(video);
}

// 3797:09b7 PzLaunch_DrawBox
void PzLaunch::drawBox(Screen& screen) {
    const LaunchState& l = *st_;
    screen.restoreRect(5, 0x33, 0x36, 0x4B);
    if (game_) game_->zones().removeLocalGroup(1000);
    // The icon and its zone of each offer, in the code's order.
    struct Offer {
        int16_t on;
        int x, y;
        std::span<const uint8_t> icon;
        int zone;  // index into zones_ (DS:2b02 + 0x16 i)
    };
    const Offer offers[] = {
        {l.offerObject0, 7, 0x5B, memberOf(objects_[0], 0), 1},  {l.offerObject3, 8, 0x6C, memberOf(objects_[3], 0), 2},
        {l.offerObject2, 0x1E, 0x5B, memberOf(objects_[2], 0), 3}, {l.offerObject1, 0x1D, 0x68, memberOf(objects_[1], 0), 4},
        {l.offerTool452, 10, 0x35, tools_[0], 5},                  {l.offerTool456, 0x0D, 0x43, tools_[2], 7},
        {l.offerTool457, 0x21, 0x4B, tools_[3], 8},                {l.offerTool453, 0x25, 0x36, tools_[1], 6},
    };
    for (const Offer& o : offers) {
        if (o.on == 0) continue;
        screen.lpxDrawKeyed(o.x, o.y, o.icon, 0);
        if (game_) {
            Zone& z = zones_[static_cast<size_t>(o.zone)];
            z = zoneFromDs(exe_, static_cast<uint16_t>(dg::kLaunchZones + 0x16 * o.zone));
            game_->zones().addLocal(z);
        }
    }
}

const Bundle& PzLaunch::objectFrames() const { return objects_[static_cast<size_t>(objectSet_ & 3)]; }

// 3797:0bd9 PzLaunch_SelectObject
void PzLaunch::selectObject(int o) {
    st_->object = static_cast<int16_t>(o);
    if (o >= 0 && o <= 3) objectSet_ = o;  // the frame set is kept for -1
    putObjectOnLauncher();
}

// 3797:0217 PzLaunch_PutObjectOnLauncher
void PzLaunch::putObjectOnLauncher() {
    LaunchState& l = *st_;
    if (l.object == -1) return;
    // 3797:01e1 PzLaunch_SetObjectSprite(239, 111, frame 0) with the clip as it is now.
    l.objectX = 0xEF;
    l.objectY = 0x6F;
    objectPic_ = memberOf(objectFrames(), 0);
    const ClipRect clip = game_ ? game_->video.screen().clip() : kFullScreenClip;
    l.clipLeft = static_cast<int16_t>(clip.left);
    l.clipRight = static_cast<int16_t>(clip.right);
    l.clipTop = static_cast<int16_t>(clip.top);
    l.clipBottom = static_cast<int16_t>(clip.bottom);
    l.objectShown = 1;
}

const Bundle& PzLaunch::rampSet() const { return st_->surface == 0 ? ramps_[0] : st_->surface == 2 ? ramps_[2] : ramps_[1]; }

// 3797:0103 PzLaunch_Animate, the drawing of a tool (force: no frame advance)
void PzLaunch::animate(Screen& screen, bool force) {
    LaunchState& l = *st_;
    if (l.glowOn) screen.lpxDrawKeyed(dsS16(dg::kLaunchHoleX, l.target), dsS16(dg::kLaunchHoleY, l.target), memberOf(glow_, l.glowFrame), 0);
    // 3797:0070 PzLaunch_DrawRamp
    screen.lpxDrawKeyed(dsS16(dg::kLaunchRampX, l.ramp), dsS16(dg::kLaunchRampY, l.ramp), memberOf(rampSet(), l.ramp), 0);
    // 3797:0000 PzLaunch_DrawObject: with the clip copied when the sprite was set
    if (l.objectShown) {
        const ClipRect saved = screen.clip();
        screen.setClip({l.clipLeft, l.clipRight, l.clipTop, l.clipBottom});
        screen.lpxDrawKeyed(l.objectX, l.objectY, objectPic_, 0);
        screen.setClip(saved);
    }
    // 3797:009f PzLaunch_DrawPlunger
    screen.lpxDrawKeyed(0xFA, 0x7A, memberOf(plunger_, l.energy), 0);
    const auto digit = memberOf(digits_, l.energy);
    screen.lpxDrawKeyed((7 - s16(digit, 6)) / 2 + 0x11B, 0x82, digit, 0);
    if (!force) l.glowFrame = static_cast<int16_t>(l.glowFrame + 1 > 7 ? 0 : l.glowFrame + 1);
}

// 3797:0ae9 PzLaunch_Redraw, a tool's
void PzLaunch::redraw(Video& video) {
    Screen& screen = video.screen();
    const LaunchState& l = *st_;
    screen.restoreRect(0x40, 0x11, 0xFB, 0x8B);
    screen.lpxDrawKeyed(dsS16(dg::kLaunchRampX, l.ramp), dsS16(dg::kLaunchRampY, l.ramp), memberOf(rampSet(), l.ramp), 0);
    if (l.objectShown) {
        const ClipRect saved = screen.clip();
        screen.setClip({l.clipLeft, l.clipRight, l.clipTop, l.clipBottom});
        screen.lpxDrawKeyed(l.objectX, l.objectY, objectPic_, 0);
        screen.setClip(saved);
    }
    screen.lpxDrawKeyed(0xFA, 0x7A, memberOf(plunger_, l.energy), 0);
    const auto digit = memberOf(digits_, l.energy);
    screen.lpxDrawKeyed((7 - s16(digit, 6)) / 2 + 0x11B, 0x82, digit, 0);
    animate(screen, true);
    drawFlush(video);
}

void PzLaunch::applyScene(Video& video, const PuzzleSceneValues& scene) {
    const auto it = scene.find("glow");
    if (it == scene.end()) return;
    // The tick drew frame (DS:3304 - 1) last, then advanced the counter.
    st_->glowFrame = static_cast<int16_t>((it->second + 7) % 8);
    animate(video.screen(), true);
    st_->glowFrame = static_cast<int16_t>(it->second);
    video.flush();
}

std::string PzLaunch::describe() const {
    const LaunchState& l = *st_;
    return std::format("launch variant {}: ramp {}, target hole {}, gravity {}, glow frame {}", variant_, l.ramp, l.target,
                       l.gravity, l.glowFrame);
}

}  // namespace tmport::gg
