#pragma once
// The DAM animation engine (segment 10b8; re/docs/code_1098-10d8.md §2.3-2.7, §8;
// fmt_othr_tables.md §4.2-4.3; ARCHITECTURE.md §3.2; PORTING_NOTES.md §4.6).
//
// One global pool of 16 channels runs DAM scripts. Every tick a player loop does
//     eraseSprites(0x180)        restore last tick's sprite rectangles from the back page
//     step()                     advance every channel by one tick (fills the sprite queue)
//     drawSprites(layer, 0, 0x180)  per layer; callers interleave layers with other drawing
//     flushUpdateRects()         present the F9 rectangles
//     Gfx_FlushDirtyRects        (the caller: Engine::flushDirtyRects)
//
// Frame table: 100 slots of PixMaps (growable here). Animation bundles fill from their base
// (Anim_LoadBundle, anim_bundle.h); the treasure hall uses bases 20/30/../70 and slot 80 holds
// the delivered treasure during DAM 20302.
//
// The original never bounds-checks its lists (100 sprites, 100 erase rects, 16 update rects);
// they are growable vectors here with the same order and contents. Deviations, only for data
// the shipped game never has: an opcode fetched past the end of a script acts as FF, operands
// past the end read 0, a channel that runs more than kMaxOpsPerTick opcodes in one tick (a loop
// without FE, which hangs the original) is ended, and a sprite whose frame slot is empty is
// queued but not drawn (the original would draw through a null pointer).

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "anim/dam_script.h"
#include "gfx/frame_buffer.h"
#include "gfx/pixmap.h"

namespace tmport::anim {

inline constexpr int kChannelCount = 16;            // DS:4f82, 29 bytes each
inline constexpr int kFrameSlots = 100;             // DS:5152..52e1
inline constexpr int kBgPageOffset = 0x180;         // bgYOff of every caller: the back page
inline constexpr int kHallFrameBase = 20;           // treasure-hall pedestal i: 20 + 10*i (DS:2da8)
inline constexpr int kHallFrameStride = 10;
inline constexpr int kDeliveredTreasureSlot = 80;   // DS:5292, set before DAM 20302 (10f0:00d8)
inline constexpr uint8_t kChannelFlagErase = 1;     // queue an erase rect for each sprite
inline constexpr uint8_t kChannelFlagOpaque = 2;    // opaque / white-key draw, presented at once
inline constexpr uint8_t kModeMirror = 1;           // FD mode bit 0
inline constexpr uint8_t kModeStamp = 2;            // FD mode bit 1: copy into the back page

// Draw groups selected by drawSprites (channel field +0x00).
namespace layer {
inline constexpr uint8_t kDefault = 0x00;  // title, cut-scenes, celebration (Anim_PlayModal)
inline constexpr uint8_t kHall = 0x4D;     // treasure-hall pedestals (1190:02a0 / 03a3)
inline constexpr uint8_t kFore14 = 0x14;   // drawn by Room_DrawForeground, never started
inline constexpr uint8_t kFore57 = 0x57;   // drawn by Room_DrawForeground, never started
}  // namespace layer

// Sound effects requested by FC / FB.
// TODO(integration): bind to audio::AudioSystem::sfxPlay(n) / sfxStop() (1170:0000 / 005b);
// sfxPlay already applies the effects-on flag (DS:0b96) and the 7000+n id.
struct AnimSound {
    std::function<void(int n)> play;  // Sfx_Play(n)
    std::function<void()> stop;       // Sfx_Stop()
};

// Gfx_BlitToWindow (1058:05bc): present a rectangle of screen rows 0..383.
using PresentFn = std::function<void(int x, int y, int w, int h)>;

// AnimChannel (29 bytes at DS:4f82 + 0x1d*i).
struct AnimChannel {
    uint8_t layer = 0;                     // +0x00
    bool stop = false;                     // +0x01: the next opcode is executed as FF
    uint8_t flags = 0;                     // +0x02: kChannelFlag*, copied into every sprite
    int16_t x = 0, y = 0;                  // +0x03 / +0x05: origin
    int16_t frameBase = 0;                 // +0x07
    std::array<int16_t, 2> loopCount{-1, -1};  // +0x09: FA counters, -1 = idle
    int16_t user0 = 0, user1 = 0;          // +0x0d / +0x0f: written, never read
    std::function<void()> onEnd;           // +0x11: called by FF
    ScriptPtr script;                      // +0x19 base (also keeps the script alive)
    size_t cursor = 0;                     // +0x15: offset of the next byte in script->bytes
    bool running = false;                  // +0x15 != NULL

    bool isFree() const { return !running; }
};

// AnimSprite (14 bytes, per-tick queue at DS:5666).
struct AnimSprite {
    uint8_t layer = 0;
    bool noDirty = false;     // the channel's script is a "DOP" script
    int16_t x = 0, y = 0;     // channel origin + (dx, dy)
    uint8_t mode = 0;         // kModeMirror / kModeStamp
    std::shared_ptr<const gfx::PixMap> pixmap;  // frame table entry at step time (may be null)
    uint8_t sflags = 0;       // channel flags
    int16_t frame = 0;        // frameBase + frame byte
};

// AnimEraseRect (9 bytes at DS:52e2): {noDirty, x, y, w + 2, h + 2}.
struct AnimEraseRect {
    bool noDirty = false;
    int16_t x = 0, y = 0, w = 0, h = 0;
};

// AnimUpdateRect (6 bytes at DS:5bde): y and h are bytes.
struct AnimUpdateRect {
    int16_t x = 0;
    uint8_t y = 0;
    int16_t w = 0;
    uint8_t h = 0;
};

class AnimEngine {
public:
    static constexpr size_t kMaxOpsPerTick = 1u << 20;

    explicit AnimEngine(gfx::FrameBuffer& fb, PresentFn present = {}, AnimSound sound = {});

    gfx::FrameBuffer& frameBuffer() { return fb_; }
    void setPresent(PresentFn present) { present_ = std::move(present); }
    void setSound(AnimSound sound) { sound_ = std::move(sound); }

    // --- frame table (g_animFrames, DS:5152) ------------------------------------------------------
    // Slots >= the current size grow the table; negative slots are ignored.
    void setFrame(int slot, std::shared_ptr<const gfx::PixMap> pm);
    std::shared_ptr<const gfx::PixMap> frame(int slot) const;
    size_t frameSlots() const { return frames_.size(); }
    void clearFrames();

    // --- channels -----------------------------------------------------------------------------------
    // 10b8:00fd Anim_ResetAll: frees every channel (no callbacks; the lists are kept).
    void resetAll();
    // 10b8:0119 Anim_Start: takes the first free channel and returns its index. Returns 0 when
    // no channel is free, which cannot be told apart from channel 0 (as in the original). A null
    // script leaves the channel free.
    int start(uint8_t layer, int x, int y, int frameBase, ScriptPtr script, int user0 = 0, int user1 = 0,
              uint8_t flags = kChannelFlagErase, std::function<void()> onEnd = {});
    // 10b8:020a Anim_IsRunning: 0 <= ch < 16, the channel's script is `script` and it runs.
    bool isRunning(int ch, const DamScript* script) const;
    bool isRunning(int ch, const ScriptPtr& script) const { return isRunning(ch, script.get()); }
    // 10b8:024e Anim_Stop: sets the stop flag if the channel runs `script`; the next step executes
    // FF for it (erasing its sprites through the normal erase list).
    void stop(int ch, const DamScript* script);
    void stop(int ch, const ScriptPtr& script) { stop(ch, script.get()); }
    // Frees one channel at once without running FF (not in the original; used when a modal
    // player is abandoned).
    void release(int ch);

    // --- per-tick pipeline ----------------------------------------------------------------------------
    // 10b8:029d Anim_EraseSprites: restores every queued erase rect from bgYOff rows below
    // (clipped to the clip rectangle) and clears the list.
    void eraseSprites(int bgYOff = kBgPageOffset);
    // 10b8:0312 Anim_Step: clears the sprite queue and runs every running channel until FE/FF.
    void step();
    // 10b8:06e4 Anim_DrawSprites: draws the queued sprites of one layer at y + yOff, queues their
    // erase rects (or stamps them into the page bgYOff rows below).
    void drawSprites(uint8_t layer, int yOff = 0, int bgYOff = kBgPageOffset);
    // 10b8:090a Anim_FlushUpdateRects: presents the F9 rects (clipped), then clears the list.
    void flushUpdateRects();
    // The whole §8.4 pipeline for one layer (as in Anim_PlayModal and Title_WaitStep), without
    // the final Gfx_FlushDirtyRects, which belongs to the caller.
    void tick(uint8_t layer = layer::kDefault);

    // --- inspection ------------------------------------------------------------------------------------
    const AnimChannel& channel(int ch) const { return channels_[static_cast<size_t>(ch & 15)]; }
    int runningChannels() const;
    const std::vector<AnimSprite>& sprites() const { return sprites_; }
    const std::vector<AnimEraseRect>& eraseRects() const { return erase_; }
    const std::vector<AnimUpdateRect>& updateRects() const { return updates_; }
    // Number of steps executed (diagnostics).
    uint64_t steps() const { return steps_; }

private:
    // 10b8:0000 Anim_RestoreBgRect / 10b8:0082 Anim_PresentRect: clip to the inclusive clip
    // rectangle; false if nothing is left.
    bool clipToClipRect(int& x, int& y, int& w, int& h) const;
    void runChannel(int ch);
    void present(int x, int y, int w, int h) const;

    gfx::FrameBuffer& fb_;
    PresentFn present_;
    AnimSound sound_;
    std::array<AnimChannel, kChannelCount> channels_{};
    std::vector<std::shared_ptr<const gfx::PixMap>> frames_;
    std::vector<AnimSprite> sprites_;
    std::vector<AnimEraseRect> erase_;
    std::vector<AnimUpdateRect> updates_;
    uint64_t steps_ = 0;
};

}  // namespace tmport::anim
