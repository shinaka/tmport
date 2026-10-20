#pragma once
// Screen animations of segment 174b (re/gg/docs/code_world.md §19.1): the small looping sprites of
// the static screens (the hub's windmill and flags, the workshop, the attract pages, the results).
//
// A set ('S' or 'A' resource) is loaded as a private copy that is itself the running record:
//
//   +0x00 list node            +0x08 1 finished, 2 holding     +0x0A sprite {flags, x, y, w, h, pic}
//   +0x18 frame bundle id      +0x1A countdown, +0x1C delay    +0x1E, +0x20 base x, y
//   +0x22 sequence             +0x24 in the list               +0x26 step
//   +0x28, +0x2A x wrap range  +0x2C sequences n               +0x32 n far pointers, then the
//   sequences: 10-byte header {i16 delay, i16 dir, far steps, u16 count}, count 10-byte steps
//   {i16 op, a, b, u16 c (sprite flags), u16 d (picture slot, bound at load)}
//
// ScreenAnim_Load gives every frame step the cache slot of its picture (ScreenAnim_BindPictures
// 1e20:0034: d = slots[op]); the engine keeps op and takes picture `op` of the frame bundle's slot
// list (gg/game/pic_cache.h), which is the same picture.

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "gg/gfx/sprites.h"

namespace tmport {
class BorlandRand;
}

namespace tmport::gg {

class PicCache;
class Screen;

// The step format of screen animations and actor sequences (code_world.md §9.2).
struct AnimStep {
    int16_t op = 0;  // >= 0 a frame (picture op of the bundle), < 0 a command
    int16_t a = 0;
    int16_t b = 0;
    uint16_t c = 0;  // sprite flags of a frame
    uint16_t d = 0;  // picture slot (written at bind time in the original)
};

struct AnimSequence {
    int16_t delay = 0;  // header +0
    int16_t dir = 0;    // header +2
    std::vector<AnimStep> steps;
};

// What the sound ops of a screen animation call (DS:7a44 = the effect bank of the screen). The ops
// do nothing while a hook is empty; isPlaying() false ends op -45's wait at once.
struct ScreenAnimSound {
    std::function<void(int member, int loop, int priority)> play;  // op -36: SndRes_PlayEffect(bank + a, b >> 8, b & 0xff)
    std::function<void(int member)> stop;                     // op -37: SndRes_StopEffect(bank + a)
    std::function<bool(int member)> isPlaying;                // op -45: SndRes_IsPlaying(bank + a)
};

class ScreenAnim {
public:
    static constexpr int kFinished = 1;  // +0x08
    static constexpr int kHolding = 2;
    static constexpr uint16_t kHidden = 0x8000;  // sprite flags

    // 174b:0324 ScreenAnim_Load(id): the set from the archive, its frame bundle (+0x18) loaded into
    // the cache (PicCache_LoadBundle); list node, state, countdown, step and sequence 0; delay = the
    // first sequence's word 0. Throws tmport::FormatError for a set that does not parse.
    static ScreenAnim load(PicCache& cache, uint16_t id);
    // The same from a payload and the pictures of its bundle (tests, tools).
    static ScreenAnim parse(std::span<const uint8_t> payload, std::vector<std::span<const uint8_t>> pictures);

    // 174b:030b ScreenAnim_SetSequence: sequence, step 0, state 0.
    void setSequence(int seq);
    // 174b:001a ScreenAnim_Step: when the countdown runs out (--countdown < 0), reload it and run
    // the steps up to the next frame (or a stopping command). Ops -34/-35 call rand() (R(b - a)).
    void step(BorlandRand& rng, const ScreenAnimSound& sound = {});

    // The current frame as a sprite (+0x0A..+0x17).
    const Sprite& sprite() const { return sprite_; }
    bool hidden() const { return (sprite_.flags & kHidden) != 0; }

    uint16_t id() const { return id_; }
    uint16_t frameBundle() const { return frameBundle_; }
    int state() const { return state_; }
    int countdown() const { return countdown_; }
    int delay() const { return delay_; }
    int baseX() const { return baseX_; }
    int baseY() const { return baseY_; }
    int sequence() const { return seq_; }
    int stepIndex() const { return step_; }
    const std::vector<AnimSequence>& sequences() const { return sequences_; }
    const std::vector<std::span<const uint8_t>>& pictures() const { return pictures_; }

    // Restores the run-time fields read from a save state (tools): countdown, base, sequence and
    // step, and shows the frame of the last frame step before `step` as ScreenAnim_Step left it
    // (sprite flags c, x = base x + a, y = base y + b). Returns false when no frame step precedes it.
    bool restore(int seq, int step, int countdown, int baseX, int baseY, int state = 0);
    // Shows frame step k of the current sequence (no commands run).
    void showFrame(const AnimStep& s);

    bool active = false;  // +0x24: in the running list

private:
    uint16_t id_ = 0;
    uint16_t frameBundle_ = 0;
    int state_ = 0;
    Sprite sprite_;
    int countdown_ = 0;
    int delay_ = 0;
    int baseX_ = 0, baseY_ = 0;
    int seq_ = 0;
    int step_ = 0;
    int wrapMin_ = 0, wrapMax_ = 0;
    std::vector<AnimSequence> sequences_;
    std::vector<std::span<const uint8_t>> pictures_;
};

// The running list g_screenAnims DS:7a46 (List_InsertAt order) and the frame of 174b:0510.
class ScreenAnims {
public:
    // 174b:0285 ScreenAnim_Start: appended, active. 174b:02ab ScreenAnim_StartAt: at position i.
    void start(ScreenAnim& a);
    void startAt(ScreenAnim& a, int i);
    // 174b:02d2 ScreenAnim_Stop / 174b:02f6 ScreenAnim_StopAll.
    void stop(ScreenAnim& a);
    void stopAll();
    size_t size() const { return list_.size(); }

    // 174b:047e ScreenAnim_UpdateAll: steps every running animation in list order, queues its sprite
    // unless hidden, and removes the finished ones (state bit 0) after queueing.
    void update(SpriteList& sprites, BorlandRand& rng, const ScreenAnimSound& sound = {});
    // 174b:0510 ScreenAnim_Frame: Sprites_EraseAll, update, Sprites_DrawAll.
    void frame(Screen& screen, SpriteList& sprites, BorlandRand& rng, const ScreenAnimSound& sound = {});

private:
    std::vector<ScreenAnim*> list_;
};

// ---- wave 3: the run (owner: world agent; game/world_rooms.cpp) ------------------------------------
// The one running list of the game is GgGame::screenAnims (g_screenAnims DS:7a46, ScreenAnim_Init
// 174b:0009); the wave 2 screens' own lists (Hub, AttractAnims) move there when their states run on the
// game.
class GgGame;
// 174b:0522 ScreenAnim_PlayModal(done): Sprites_Reset, then 4-tick frames of ScreenAnim_Frame (the
// running list GgGame::screenAnims, its sound ops on g_effectBank) and Dirty_Flush until *done != 0.
void screenAnimPlayModal(GgGame& g, const int16_t& done);
// The same in the form its callers use (2dee:0156, 1cb2, 1d56 pass the running animation + 8): until
// the animation's state word +0x08 is non-zero (1 finished at op -1000, 2 holding at op -2).
void screenAnimPlayModal(GgGame& g, const ScreenAnim& anim);

}  // namespace tmport::gg
