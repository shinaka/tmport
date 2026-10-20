#pragma once
// The launch puzzle, kind 5 (ovl17 3797; re/gg/docs/code_puzzles_a.md §5): the variant records DS:2c62
// (30 of 15 words) and the hole, ramp and plunger positions of the user's ssg.exe, the pictures, the
// screen, the variant's drawing after the fade-in (gravity slider, the box of offered objects and tools,
// the ramp, the plunger and the glowing target) and the animation tick's drawing; in wave 3 the run on
// the game (game/pz_launch_run.cpp): the plunger, the landing tables, the flight and the DAM surface
// changes (game/dam.h).
//
// Variant words: 0 ramp (0..2 choosable, 3..5 fixed), 1..4 offer the tools 0x452 / 0x453 / 0x456 /
// 0x457, 5..8 offer the objects 2 / 3 / 0 / 1, 9 gravity (-1 locked), 10 the target hole, 11..14 the
// required ramp, surface, object and gravity.
//
// The puzzle's globals are GgState::puzzles.launch (LaunchState: the selections, the glow, the object
// sprite's position and clip, the offers). A tool works on the object's own copy; the game binds the
// object to its state. The object sprite's picture is the engine's.

#include <array>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/game/game_state.h"
#include "gg/game/puzzles.h"
#include "gg/gfx/screen.h"

namespace tmport::gg {

class GgGame;

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
    static constexpr uint16_t kEffectBank = 0x11CE;
    static constexpr int kWords = 15;

    PzLaunch(const GgExe& exe, const Archive& archive);

    // 3797:1138 PzLaunch_Run up to the loop, as a tool shows it: PzLaunch_LoadResources,
    // PzLaunch_DrawScreen (3797:040d: the screen at (0, 10), the panel, the back page, the hand, the
    // fade-in), then PzLaunch_LoadVariant (3797:030c), which draws the gravity, the box and the ramp after
    // the fade-in. Timer 6 starts the glow at frame DS:3304 (0 in the file).
    void setup(Video& video, Fonts& fonts, int variant) override;
    // 3797:087d PzLaunch_SetGravity / 3797:08b6 PzLaunch_DrawGravity (flushes).
    void setGravity(int g);
    void drawGravity(Video& video);
    // 3797:09b7 PzLaunch_DrawBox: the offers' icons (and, on the game, their zones, tag 1000).
    void drawBox(Screen& screen);
    // 3797:0bd9 PzLaunch_SelectObject (-1 none: the frame set is kept).
    void selectObject(int o);
    // 3797:0217 PzLaunch_PutObjectOnLauncher: when an object is selected, the sprite = its frame 0 at
    // (239, 111) with the current clip, shown.
    void putObjectOnLauncher();
    // 3797:0ae9 PzLaunch_Redraw: the ramp set of the surface, restore (64, 17, 251, 139), ramp, object,
    // plunger, PzLaunch_Animate(1, 0), flush.
    void redraw(Video& video);
    // 3797:0103 PzLaunch_Animate(force, sprites): when timer slot 6 expired or forced: the glow frame of
    // the target, the ramp, the DAM cels (sprites), the object, the plunger; the timer's tick (force 0)
    // also advances the glow frame, flushes and sets timer 6 to 5 ticks (a 6-tick period).
    void animate(Screen& screen, bool force);
    // A save state's glow frame DS:3304 (the next frame to draw): the screen shows the one before.
    void applyScene(Video& video, const PuzzleSceneValues& scene) override;
    std::string describe() const override;

    int word(int i) const { return static_cast<int16_t>(words_[static_cast<size_t>(i)]); }
    LaunchState& st() { return *st_; }
    const LaunchState& st() const { return *st_; }


    // ---- wave 3: the run (owner: puzzles_a agent; game/pz_launch_run.cpp) ----
    // The puzzle's globals are GgState::puzzles.launch: bind() points the class at them.
    void bind(LaunchState& state) { st_ = &state; }
    bool bound() const { return st_ != &own_; }
    // 3797:1138 PzLaunch_Run: the puzzle (code_puzzles_a.md §5): set-up, hint 0x1052, the loop (switch CS:144f, 22 codes) with the animation tick on timer slot 6, the DAM cut-scenes (Anim_PlayModal), cleanup; 1 when solved.
    int run(GgGame& g, int variant);
    // 3797:0000 PzLaunch_DrawObject: the object sprite at its place with its saved clip rectangle, when shown.
    void drawObject(GgGame& g);
    // 3797:0070 PzLaunch_DrawRamp: member DS:b124 of the current ramp set at (DS:32ec, DS:32f8)[DS:b124].
    void drawRamp(GgGame& g);
    // 3797:009f PzLaunch_DrawPlunger: the plunger picture of the energy level and its digit.
    void drawPlunger(GgGame& g);
    // 3797:0103 PzLaunch_Animate on the game (timer slot 6 polled through the runtime).
    void animate(GgGame& g, bool force, bool sprites);
    // 3797:0ae9 PzLaunch_Redraw on the game (the presenting flush).
    void redraw(GgGame& g);
    // 3797:018e PzLaunch_GetCode: PzLaunch_Animate(0, 0), then Input_StripHeld(Input_GetCode()).
    uint16_t getCode(GgGame& g);
    // 3797:01a5 PzLaunch_WaitTimer: animates until Timer_Expired(slot).
    void waitTimer(GgGame& g, int slot);
    // 3797:01c3 PzLaunch_WaitTimerAnim: the same with the DAM cels drawn.
    void waitTimerAnim(GgGame& g, int slot);
    // 3797:01e1 PzLaunch_SetObjectSprite: the object sprite's position and picture; the current clip rectangle copied.
    void setObjectSprite(GgGame& g, int x, int y, int frame);
    // 3797:023c PzLaunch_IsSolved: surface, object, gravity, landing hole and ramp against the variant's requirements.
    bool isSolved();
    // 3797:02c3 PzLaunch_AddZones: the hint, ramp, gravity and plunger zones.
    void addZones(GgGame& g);
    // 3797:0725 PzLaunch_Cleanup: frees everything, clears the zones and cursor regions, the default cursor.
    void cleanup(GgGame& g);
    // 3797:0870 PzLaunch_SetEnergy: DS:b12a.
    void setEnergy(int e);
    // 3797:08e2 PzLaunch_StepRamp: the ramp choice + delta wrapping 0..2, forced to the variant's fixed ramp 3..5.
    void stepRamp(int delta);
    // 3797:0942 PzLaunch_SetOfferObject0, 3797:094f PzLaunch_SetOfferTool456, 3797:095c
    // PzLaunch_SetOfferTool457, 3797:0976 PzLaunch_SetOfferObject3, 3797:0983 PzLaunch_SetOfferObject2,
    // 3797:0990 PzLaunch_SetOfferObject1, 3797:099d PzLaunch_SetOfferTool452, 3797:09aa
    // PzLaunch_SetOfferTool453: the eight offer setters (DS:b108..b116) as one; `which` names the
    // setter: 0..3 the tools 0x452, 0x453, 0x456, 0x457, 4..7 the objects 0..3.
    void setOffer(int which, int on);
    // 3797:0969 PzLaunch_SetSurface: DS:b11a.
    void setSurface(int s);
    // 3797:0b4b PzLaunch_SetTarget: DS:b126.
    void setTarget(int hole);
    // 3797:0b58 PzLaunch_ComputeLanding: DS:b11c = DS:3000[object + surface + gravity + ramp terms + energy - 1].
    void computeLanding();
    // 3797:0c33 PzLaunch_Fly: the launch's animation: the roll back below 4, else the climb and the flight along the path tables into the hole.
    void fly(GgGame& g);
    // 3797:0f33 PzLaunch_PullPlunger: the drag sets the energy (dx / 3, 0..7); the release launches; returns the judgement.
    int pullPlunger(GgGame& g);
    // 3797:0fca PzLaunch_DragGravity: the gravity slider drag: y < 0x92 low, < 0x9d normal, else high.
    void dragGravity(GgGame& g);

    // A moment of the main loop from a DOSBox-X state (puzzles.h).
    bool restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) override;
    int resume(GgGame& g) override;

private:
    int dsS16(uint16_t off, int i) const;
    const Bundle& rampSet() const;
    const Bundle& objectFrames() const;  // DS:b120 g_launchObjectFrames (the selected object's set)
    void loadResources(int variant);
    void loadVariant(int variant);       // 3797:030c PzLaunch_LoadVariant (the drawing through drawFlush)
    void drawFlush(Video& video);
    int loop(GgGame& g, bool solved);
    void surfaceChange(GgGame& g, int effect, uint16_t script, uint16_t frames, int newSurface);

    const GgExe& exe_;
    const Archive& archive_;
    LaunchState own_;
    LaunchState* st_ = &own_;
    GgGame* game_ = nullptr;
    std::array<uint16_t, kWords> words_{};
    Bundle plunger_, gravityPics_, ramps_[3], objects_[4], glow_, digits_;
    std::array<std::vector<uint8_t>, 4> tools_;
    std::vector<uint8_t> panelText_, hintText_, hintPressed_;
    std::unique_ptr<CursorRecord> cursor_;
    std::span<const uint8_t> objectPic_;  // DS:af8c g_launchObjectPic
    int objectSet_ = 0;                   // DS:b120: the frame set kept for "no object"
    // The click zones: DS:2b02 .. DS:2c4c (the hint, the box items of tag 1000, the ramp areas, the
    // gravity slider and labels, the plunger).
    std::array<Zone, 16> zones_{};
    int variant_ = 0;
};

}  // namespace tmport::gg
