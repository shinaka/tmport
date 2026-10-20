#include "gg/game/pz_launch.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
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

// 3797:1138 PzLaunch_Run, up to the loop
void PzLaunch::setup(Video& video, Fonts& fonts, int variant) {
    objectShown = false;
    // TODO(wave 3): PzSnd_LoadBank(0x11ce) is the audio area's.
    // 3797:0464 PzLaunch_LoadResources
    plunger_ = Bundle::load(archive_, kPlunger);
    gravityPics_ = Bundle::load(archive_, kGravityPics);
    ramps_[0] = Bundle::load(archive_, kRampsSurface0);
    ramps_[1] = Bundle::load(archive_, kRampsSurface1);
    ramps_[2] = Bundle::load(archive_, kRampsSurface2);
    for (size_t o = 0; o < kObjects.size(); ++o) objects_[o] = Bundle::load(archive_, kObjects[o]);
    digits_ = Bundle::load(archive_, kPuzzleDigits);
    for (size_t t = 0; t < kTools.size(); ++t) tools_[t] = archive_.read(kTools[t]);
    panelText_ = puzzleText(archive_, static_cast<uint16_t>(kPanelTextFirst + variant));
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    glowFrame = exe_.dsS16(dg::kLaunchGlowFrame);
    // 3797:040d PzLaunch_DrawScreen
    drawPuzzlePicture(video, archive_, puzzleKindInfo(5).picture, puzzleKindInfo(5).palette);
    PuzzlePanel panel;
    panel.init(video.screen(), fonts, archive_, panelText_);
    video.screen().saveToBack(0, 0, 320, 200);
    video.cursor().setCursorPicture(cursor_.get());
    puzzleFadeIn(video);
    // TODO(wave 3): PzLaunch_AddZones (the runtime's zones).
    // 3797:030c PzLaunch_LoadVariant
    if (variant < 0 || static_cast<size_t>(variant) >= dg::kLaunchVariantsCount)
        throw std::out_of_range(std::format("launch variant {}", variant));
    for (int i = 0; i < kWords; ++i)
        words_[static_cast<size_t>(i)] = exe_.ds16(static_cast<uint16_t>(
            dg::kLaunchVariants + dg::kLaunchVariantsStride * static_cast<size_t>(variant) + 2 * static_cast<size_t>(i)));
    variant_ = variant;
    gravity = 0;
    setGravity(word(9));
    drawGravity(video);
    energy = 0;
    target = word(10);
    glowOn = true;
    glow_ = Bundle::load(archive_, static_cast<uint16_t>(kGlowFirst + target));
    ramp = word(0);
    offerTool = {word(1), word(2), word(3), word(4)};
    surface = 1;
    selectObject(-1);
    offerObject[0] = word(7);
    offerObject[3] = word(6);
    offerObject[2] = word(5);
    offerObject[1] = word(8);
    drawBox(video.screen());
    redraw(video);
    // TODO(wave 3): Hint_Show(0x1052) (UI) and Timer_Set(6, 5) (the runtime) start the loop.
}

// 3797:087d PzLaunch_SetGravity
void PzLaunch::setGravity(int g) {
    if (gravity == -1) return;
    gravity = g;
    if (g == 2) gravityPic = 1;
    else if (g == 1) gravityPic = 2;
    else if (g == 0) gravityPic = 3;
}

// 3797:08b6 PzLaunch_DrawGravity
void PzLaunch::drawGravity(Video& video) const {
    video.screen().lpxDrawKeyed(4, 0x87, memberOf(gravityPics_, gravity == -1 ? 0 : gravityPic), 0);
    video.flush();
}

// 3797:09b7 PzLaunch_DrawBox
void PzLaunch::drawBox(Screen& screen) const {
    screen.restoreRect(5, 0x33, 0x36, 0x4B);
    // TODO(wave 3): Zones_RemoveLocalGroup(1000) and each offer's zone (tag 1000).
    if (offerObject[0]) screen.lpxDrawKeyed(7, 0x5B, memberOf(objects_[0], 0), 0);
    if (offerObject[3]) screen.lpxDrawKeyed(8, 0x6C, memberOf(objects_[3], 0), 0);
    if (offerObject[2]) screen.lpxDrawKeyed(0x1E, 0x5B, memberOf(objects_[2], 0), 0);
    if (offerObject[1]) screen.lpxDrawKeyed(0x1D, 0x68, memberOf(objects_[1], 0), 0);
    if (offerTool[0]) screen.lpxDrawKeyed(10, 0x35, tools_[0], 0);
    if (offerTool[2]) screen.lpxDrawKeyed(0x0D, 0x43, tools_[2], 0);
    if (offerTool[3]) screen.lpxDrawKeyed(0x21, 0x4B, tools_[3], 0);
    if (offerTool[1]) screen.lpxDrawKeyed(0x25, 0x36, tools_[1], 0);
}

// 3797:0bd9 PzLaunch_SelectObject, 3797:0217 PzLaunch_PutObjectOnLauncher, 3797:01e1 SetObjectSprite
void PzLaunch::selectObject(int o) {
    object = o;  // the frame set is kept for -1
    if (object == -1) return;
    objectPic_ = memberOf(objects_[static_cast<size_t>(object & 3)], 0);
    objectX_ = 0xEF;
    objectY_ = 0x6F;
    objectShown = true;
}

const Bundle& PzLaunch::rampSet() const { return surface == 0 ? ramps_[0] : surface == 2 ? ramps_[2] : ramps_[1]; }

// 3797:0103 PzLaunch_Animate
void PzLaunch::animate(Screen& screen, bool force) {
    if (glowOn) screen.lpxDrawKeyed(dsS16(dg::kLaunchHoleX, target), dsS16(dg::kLaunchHoleY, target), memberOf(glow_, glowFrame), 0);
    // 3797:0070 PzLaunch_DrawRamp
    screen.lpxDrawKeyed(dsS16(dg::kLaunchRampX, ramp), dsS16(dg::kLaunchRampY, ramp), memberOf(rampSet(), ramp), 0);
    // 3797:0000 PzLaunch_DrawObject: with the clip copied when the sprite was set
    if (objectShown) {
        const ClipRect saved = screen.clip();
        screen.setClip(objectClip_);
        screen.lpxDrawKeyed(objectX_, objectY_, objectPic_, 0);
        screen.setClip(saved);
    }
    // 3797:009f PzLaunch_DrawPlunger
    screen.lpxDrawKeyed(0xFA, 0x7A, memberOf(plunger_, energy), 0);
    const auto digit = memberOf(digits_, energy);
    screen.lpxDrawKeyed((7 - s16(digit, 6)) / 2 + 0x11B, 0x82, digit, 0);
    if (!force) glowFrame = glowFrame + 1 > 7 ? 0 : glowFrame + 1;
}

// 3797:0ae9 PzLaunch_Redraw
void PzLaunch::redraw(Video& video) {
    Screen& screen = video.screen();
    screen.restoreRect(0x40, 0x11, 0xFB, 0x8B);
    screen.lpxDrawKeyed(dsS16(dg::kLaunchRampX, ramp), dsS16(dg::kLaunchRampY, ramp), memberOf(rampSet(), ramp), 0);
    if (objectShown) {
        const ClipRect saved = screen.clip();
        screen.setClip(objectClip_);
        screen.lpxDrawKeyed(objectX_, objectY_, objectPic_, 0);
        screen.setClip(saved);
    }
    screen.lpxDrawKeyed(0xFA, 0x7A, memberOf(plunger_, energy), 0);
    const auto digit = memberOf(digits_, energy);
    screen.lpxDrawKeyed((7 - s16(digit, 6)) / 2 + 0x11B, 0x82, digit, 0);
    animate(screen, true);
    video.flush();
}

void PzLaunch::applyScene(Video& video, const PuzzleSceneValues& scene) {
    const auto it = scene.find("glow");
    if (it == scene.end()) return;
    // The tick drew frame (DS:3304 - 1) last, then advanced the counter.
    glowFrame = (it->second + 7) % 8;
    animate(video.screen(), true);
    glowFrame = it->second;
    video.flush();
}

std::string PzLaunch::describe() const {
    return std::format("launch variant {}: ramp {}, target hole {}, gravity {}, glow frame {}", variant_, ramp, target,
                       gravity, glowFrame);
}

}  // namespace tmport::gg
