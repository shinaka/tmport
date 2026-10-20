#pragma once
// The launch puzzle, kind 5 (ovl17 3797; re/gg/docs/code_puzzles_a.md §5), the data and drawing half:
// the variant records DS:2c62 (30 of 15 words) and the hole, ramp and plunger positions of the user's
// ssg.exe, the pictures, the screen, the variant's drawing after the fade-in (gravity slider, the box
// of offered objects and tools, the ramp, the plunger and the glowing target) and the animation tick's
// drawing. The plunger, the landing tables, the flight and the DAM surface changes are wave 3's.
//
// Variant words: 0 ramp (0..2 choosable, 3..5 fixed), 1..4 offer the tools 0x452 / 0x453 / 0x456 /
// 0x457, 5..8 offer the objects 2 / 3 / 0 / 1, 9 gravity (-1 locked), 10 the target hole, 11..14 the
// required ramp, surface, object and gravity.

#include <array>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/puzzles.h"
#include "gg/gfx/screen.h"

namespace tmport::gg {

class PzLaunch final : public Puzzle {
public:
    static constexpr uint16_t kPlunger = 0x1148;       // 8 pictures by energy
    static constexpr uint16_t kGravityPics = 0x114A;   // 0 locked, 1 low, 2 normal, 3 high
    static constexpr uint16_t kRampsSurface0 = 0x1143; // 6 ramps each
    static constexpr uint16_t kRampsSurface1 = 0x1141;
    static constexpr uint16_t kRampsSurface2 = 0x1142;
    static constexpr std::array<uint16_t, 4> kObjects = {0x1144, 0x1147, 0x1146, 0x1145};  // objects 0..3
    static constexpr std::array<uint16_t, 4> kTools = {0x1135, 0x1136, 0x1137, 0x1138};    // 0x452, 0x453, 0x456, 0x457
    static constexpr uint16_t kGlowFirst = 0x1139;     // + target
    static constexpr uint16_t kHintTextFirst = 0x1163;
    static constexpr uint16_t kPanelTextFirst = 0x1181;
    static constexpr int kWords = 15;

    PzLaunch(const GgExe& exe, const Archive& archive);

    // 3797:1138 PzLaunch_Run up to the loop: PzLaunch_LoadResources, PzLaunch_DrawScreen (3797:040d:
    // the screen at (0, 10), the panel, the back page, the hand, the fade-in), then
    // PzLaunch_LoadVariant (3797:030c), which draws the gravity, the box and the ramp after the
    // fade-in. Timer 6 starts the glow at frame DS:3304 (0 in the file).
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 3797:087d PzLaunch_SetGravity / 3797:08b6 PzLaunch_DrawGravity (flushes).
    void setGravity(int g);
    void drawGravity(Video& video) const;
    // 3797:09b7 PzLaunch_DrawBox (the zones of the offers are the runtime's).
    void drawBox(Screen& screen) const;
    // 3797:0bd9 PzLaunch_SelectObject (-1 none) and 3797:0217 PzLaunch_PutObjectOnLauncher.
    void selectObject(int o);
    // 3797:0ae9 PzLaunch_Redraw: the ramp set of the surface, restore (64, 17, 251, 139), ramp,
    // object, plunger, PzLaunch_Animate(1, 0), flush.
    void redraw(Video& video);
    // 3797:0103 PzLaunch_Animate(force, sprites) for force = 1 (no DAM sprites): the glow frame of the
    // target, the ramp, the object, the plunger; the timer's tick (force 0) also advances the frame.
    void animate(Screen& screen, bool force);
    // A save state's glow frame DS:3304 (the next frame to draw): the screen shows the one before.
    void applyScene(Video& video, const PuzzleSceneValues& scene) override;
    std::string describe() const override;

    int word(int i) const { return static_cast<int16_t>(words_[static_cast<size_t>(i)]); }
    int glowFrame = 0;     // DS:3304
    int gravity = 0;       // DS:b11e
    int gravityPic = 0;    // DS:b128
    int energy = 0;        // DS:b12a
    int target = 0;        // DS:b126
    int ramp = 0;          // DS:b124
    int surface = 1;       // DS:b11a
    int object = -1;       // DS:b118
    bool glowOn = true;    // DS:af90
    bool objectShown = false;  // DS:af9a
    std::array<int, 4> offerTool{};    // 0x452, 0x453, 0x456, 0x457 (DS:b116, b114, b10a, b108)
    std::array<int, 4> offerObject{};  // objects 0..3 (DS:b112, b10c, b10e, b110)

private:
    int dsS16(uint16_t off, int i) const;
    const Bundle& rampSet() const;

    const GgExe& exe_;
    const Archive& archive_;
    std::array<uint16_t, kWords> words_{};
    Bundle plunger_, gravityPics_, ramps_[3], objects_[4], glow_, digits_;
    std::array<std::vector<uint8_t>, 4> tools_;
    std::vector<uint8_t> panelText_;
    std::unique_ptr<CursorRecord> cursor_;
    // The object sprite (DS:af8c..af9e): picture, position and the clip copied when it was set.
    std::span<const uint8_t> objectPic_;
    int objectX_ = 0, objectY_ = 0;
    ClipRect objectClip_;
    int variant_ = 0;
};

}  // namespace tmport::gg
