#pragma once
// Screen transitions of segment 1473 (re/gg/docs/code_runtime.md §14): Screen_Show (1473:045a) and
// the random wipe (1473:0042 Screen_RandomWipe, 1473:0479 Screen_Wipe). Screen_FadeOutIn (1473:0325)
// is Palette::fadeOut, Video::flush, Palette::fadeIn in that order.
//
// The wipe copies column spans of the screen rows 9..164 straight into VRAM, bypassing the dirty
// map, in timer-paced steps (slot 2, 2 ticks per step in every pattern); after it Screen_Show clears
// the dirty map without flushing, so tiles marked before outside the wiped rows reach the screen only
// when marked again, and pattern 0x11DA never copies column 319 (its left sweep stops at the right
// frontier 319): both kept. Its two rand() calls (the pattern, then the sound) come before the first
// copy; the generator is the caller's (the one Borland LCG of the game), so the order stays exact.
// The door wipe captured in r2_aircraft_door_to_room5.avi is pattern 0x11DB: 48 steps of 2 ticks,
// every video frame showing the state after a step or between two (fmt_graphics.md §11).
//
// The transitions are steppers the runtime drives: it sets timer slot 2 to period() before each
// Wipe::step and waits for it after, and runs each palette load of a fade on its retrace clock.

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "gg/gfx/palette.h"
#include "gg/gfx/screen.h"

namespace tmport {
class BorlandRand;
}

namespace tmport::gg {

class Archive;
class Video;

// A wipe pattern, W 0x11D6..0x11DD (636 bytes; code_runtime.md §14.2).
struct WipePattern {
    static constexpr int kRows = 156;      // screen rows 9..164
    static constexpr size_t kSize = 636;
    uint16_t unused = 0;                   // +000
    uint16_t period = 0;                   // +002: ticks per step (Timer_Set(2, period))
    uint16_t leftEnabled = 0;              // +004
    std::array<int16_t, kRows> left{};     // +006: initial left frontier per row
    uint16_t rightEnabled = 0;             // +13E
    std::array<int16_t, kRows> right{};    // +140: initial right frontier per row
    int16_t leftStep = 0;                  // +278
    int16_t rightStep = 0;                 // +27A

    // Throws tmport::FormatError for a payload shorter than 636 bytes.
    static WipePattern parse(std::span<const uint8_t> payload);
};

inline constexpr uint16_t kWipePatternFirst = 0x11D6;  // 8 patterns, 0x11D6..0x11DD
inline constexpr int kWipeSoundBase = 0x19;            // effects 0x19 / 0x1A of g_effectBank
inline constexpr int kWipeTimerSlot = 2;

// 1473:0042 Screen_RandomWipe's choice: 0x11D6 + rand() * 8 / 0x8000 (CWD, F_LXLSH 3, F_LDIV).
uint16_t randomWipePattern(BorlandRand& rng);

// 1473:0479 Screen_Wipe(pattern) as a stepper.
class Wipe {
public:
    // Gfx_HideMouseOnce; the row state (left done 0, right done 320, frontiers from the pattern);
    // the sound: rand() * 2 / 0x8000. The caller then runs SndRes_PlayEffect(g_effectBank +
    // soundEffect(), loop 1, priority 10).
    Wipe(Screen& screen, const WipePattern& pattern, BorlandRand& rng);

    // A pattern whose rows never all meet would loop forever in the original; the engine stops after
    // this many steps (the shipped patterns take 18..48).
    static constexpr int kMaxSteps = 1000;

    int soundEffect() const { return sound_; }  // 0x19 or 0x1A
    int period() const { return pattern_.period; }
    bool finished() const { return finished_ == WipePattern::kRows || steps_ >= kMaxSteps; }
    int steps() const { return steps_; }
    // One iteration of the loop (between Timer_Set(2, period) and Timer_Wait(2)): every row not yet
    // done moves its frontiers by the steps (meeting frontiers end the row) and copies the newly
    // uncovered spans of the screen rows to VRAM.
    void step(const Screen& screen, Vram& vram);
    // After the loop: the caller stops the effect (SndRes_StopEffect(g_effectBank + soundEffect())),
    // then Gfx_ResetMouseHide.
    void end(Screen& screen) const { screen.resetMouseHide(); }

private:
    WipePattern pattern_;
    std::array<int, WipePattern::kRows> l_{}, r_{}, leftDone_{}, rightDone_{};
    std::array<bool, WipePattern::kRows> done_{};
    int finished_ = 0;
    int sound_ = 0;
    int steps_ = 0;
};

// 1473:045a Screen_Show (the warehouse room entry): faded: Dirty_Flush now, then Pal_FadeIn;
// otherwise Screen_RandomWipe (the pattern read from the archive) and, at the end, Dirty_Clear.
class ScreenShow {
public:
    ScreenShow(Video& video, const Archive& archive, BorlandRand& rng);

    bool fading() const { return fading_; }
    // The fade-in (fading()): run each load on the retrace clock.
    PalFade& fade() { return fade_; }
    // The wipe (!fading()): drive as Wipe says.
    Wipe& wipe() { return *wipe_; }
    uint16_t wipePattern() const { return pattern_; }
    // After the last step: the wipe's Gfx_ResetMouseHide and Dirty_Clear (nothing after a fade).
    void finish(Video& video);

private:
    bool fading_ = false;
    PalFade fade_;
    uint16_t pattern_ = 0;
    std::optional<Wipe> wipe_;
};

}  // namespace tmport::gg
