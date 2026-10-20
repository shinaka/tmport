#pragma once
// The DAM animation engine of the launch puzzle (ssg.exe ovl37 3d39 and 3797:100e Anim_PlayModal;
// re/gg/docs/fmt_graphics.md §6, code_puzzles_a.md §5.5): two channels of 16-bit opcodes 0x7FF8..0x7FFF
// (the table CS:083a), the queued cels drawn per layer, the update rectangles. Its globals are
// GgState::puzzles.dam (the cel and rectangle counts, the channels' 0x22-byte records); the cels, the
// rectangles, the callbacks, the frame table and the scripts are the engine's (DamPlayer).
//
// The functions without a caller in the shipped game (Anim_Init 3d39:0000, Anim_RestoreBgRect 0049,
// Anim_ResetAll 011c, Anim_SeekTick 013a, Anim_DrawCel 0323, Anim_EraseList 037e, Anim_Stop 04ae,
// Anim_SetPause 04f9, Anim_EraseSprites 0553, Anim_FlushUpdateRects 0983) are not ported.
//
// Owner: puzzles_a agent.

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace tmport::gg {

class GgGame;
class PzLaunch;
class Screen;
struct DamState;

class DamPlayer {
public:
    static constexpr int kChannels = 2;
    static constexpr int kMaxCels = 16;        // DS:b87e .. b97e (16 records of 16 bytes)
    static constexpr int kCallbacks = 2;       // DS:ba1e .. ba26 (2 far pointers before the frame table)

    // A channel: the 0x22-byte record DS:ba66 + 0x22 ch (fmt_graphics.md §6.3). The far cursor and
    // script base are the engine's script copy and a byte offset into it (0 = the channel is free).
    struct Channel {
        int16_t layer = 0;      // +0x00
        int16_t stop = 0;       // +0x02 stop request (the next opcode becomes 0x7FFF)
        int16_t pause = 0;      // +0x04 (never written by Anim_Start: BSS 0)
        int16_t flags = 0;      // +0x06 bit 1 opaque cels, bit 0 erase rectangles
        int16_t x = 0, y = 0;   // +0x08, +0x0a
        int16_t frameBase = 0;  // +0x0c
        std::array<int16_t, 2> counters{-1, -1};  // +0x0e, +0x10 loop counters
        uint32_t user = 0;      // +0x12
        std::function<void()> callback;           // +0x16 the 0x7FFF callback (never set in the game)
        std::vector<uint8_t> script;              // +0x1e the script copy ('DAM' 0x1A + words)
        size_t cursor = 0;                        // +0x1a byte offset into the script, 0 = free
    };
    // A queued cel (16 bytes at DS:b87e): layer, x, y, flags, far picture, the channel's flags.
    struct Cel {
        int16_t layer = 0, x = 0, y = 0, flags = 0;
        std::span<const uint8_t> picture;
        int16_t channelFlags = 0;
    };
    struct Rect {
        int16_t x = 0, y = 0, w = 0, h = 0;
    };

    // The frame table DS:ba26: member i of the frame bundle (Anim_PlayModal builds it).
    void setFrames(std::vector<std::span<const uint8_t>> frames) { frames_ = std::move(frames); }
    // 3d39:03cb Anim_Start(layer, x, y, frame base, script, user, flags, callback): the first free
    // channel of 2; returns it, or 0 when none is free (so 0 also means "none", as in the original).
    int start(GgGame& g, int layer, int x, int y, int frameBase, std::span<const uint8_t> script, uint32_t user = 0,
              int flags = 0, std::function<void()> callback = {});
    // 3d39:0468 Anim_IsRunning(channel, script): the channel runs that script and is not free.
    bool isRunning(int channel, std::span<const uint8_t> script) const;
    // 3d39:058f Anim_Step: the cel queue emptied (g_animCelCount = 0), then every running channel's
    // opcodes up to its 0x7FFE (end of tick) or 0x7FFF (end: callback, channel freed).
    void step(GgGame& g);
    // 3d39:084a Anim_DrawSprites(layer): the queued cels of a layer: opaque (Lpx_Draw) when the
    // channel's flag bit 1 is set, else keyed with mode = cel flags & 1; then a cel with flag 2 is saved
    // into the back page, and one of a channel with flag bit 0 (and no cel flag 4) queues an erase
    // rectangle.
    void drawSprites(Screen& screen, int layer);
    // 3d39:00ba Anim_PresentRect(x, y, w, h): clipped to the screen's clip; a non-empty rectangle is
    // presented through Dirty_Flush (its only caller, Anim_FlushUpdateRects, has none in the game).
    void presentRect(GgGame& g, int x, int y, int w, int h);

    // The 7FF8 callbacks DS:ba1e (none in the game: the table is BSS and never written).
    std::array<std::function<void(int channel, uint32_t arg)>, kCallbacks> callbacks;

    const Channel& channel(int i) const { return channels_[static_cast<size_t>(i)]; }
    const std::vector<Cel>& cels() const { return cels_; }
    const std::vector<Rect>& rects() const { return rects_; }
    const std::vector<Rect>& eraseRects() const { return erase_; }
    // Writes the counts and the channels' records into GgState::puzzles.dam (the far cursor and
    // script base as {byte offset, 0} for a running channel, 0 when free).
    void syncState(DamState& s) const;

private:
    std::array<Channel, kChannels> channels_{};
    std::vector<std::span<const uint8_t>> frames_;
    std::vector<Cel> cels_;
    std::vector<Rect> rects_;   // DS:b86e, count g_animRectCount DS:398a
    std::vector<Rect> erase_;   // DS:b980, count DS:3986
};

// 3797:100e Anim_PlayModal(script, frames, newSurface): the launch puzzle's surface change: DAM script
// `script` + ramp % 3 with frame bundle `frames` + ramp % 3 (an animation tick after each load), one
// channel started with everything else 0, then per DAM tick: Timer_Set(7, 18), the play area restored,
// Anim_Step, the object back on the launcher and the plunger, effect 0x13 for script 0x1154, and the
// wait on timer 7 that redraws the cels every 6 ticks (19 ticks per DAM tick: Timer_Expired is strict);
// at the end the new surface, PzLaunch_Redraw and the copies freed.
void damPlayModal(GgGame& g, PzLaunch& launch, uint16_t script, uint16_t frames, int newSurface);

}  // namespace tmport::gg
