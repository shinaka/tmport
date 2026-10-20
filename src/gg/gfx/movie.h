#pragma once
// The final movie of Gizmos & Gadgets (re/gg/docs/code_runtime.md §10.5, §15; fmt_graphics.md §7):
// Movie_DecodeFrame (1399:0a85), the Deluxe Paint Animation "RunSkipDump" delta coding with colour 1
// as skip, written straight into VRAM, and Movie_Play (1473:07d9), the frame loop with its cue table.
//
// State 15 (2fcb:0640) plays frames 0x3000..0x30C9 at 18 PIT ticks each with the cue table DS:236c
// and the effect bank 0x108D; the movie loads no palette (it shows the results screen's) and never
// touches the RAM screen, the dirty map or the cursor.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "gg/gfx/screen.h"

namespace tmport::gg {

class Archive;

inline constexpr uint16_t kMovieFirst = 0x3000;
inline constexpr uint16_t kMovieEnd = 0x30CA;  // exclusive: 202 frames
inline constexpr int kMoviePeriod = 18;        // PIT ticks per frame (2fcb:069d)
inline constexpr uint16_t kMovieEffectBank = 0x108D;

struct MovieDecodeResult {
    size_t used = 0;       // bytes of the frame consumed, up to and including the 80 00 00 end code
    uint16_t end = 0;      // the destination offset at the end (Movie_DecodeFrame's return value)
    bool complete = false; // false when the data ran out before the end code (not in shipped data)
};

// 1399:0a85 Movie_DecodeFrame(src, dst): 01..7F copy that many bytes; 00 n v: n bytes of v (v = 1
// skips); 80 w: w = 0 end, w < 0x8000 skip w, 0x8000 + n copy n, 0xC000 + n then v: n bytes of v (v =
// 1 skips); 81..FF skip code - 0x80. The destination offset is 16-bit (A000:0000 + offset); bytes
// past the visible 64 000 are not stored.
MovieDecodeResult movieDecodeFrame(std::span<const uint8_t> src, Vram& dst);

// One record of the cue table: words {type, frame, a, b}.
struct MovieCue {
    int16_t type = 0;   // 0 Delay(a); 1 SndRes_PlayEffect(bank + a, 0, b); 2 SndRes_StopEffect(bank + a);
                        // 5 SndRes_PlayEffect(bank + a, 1, b); 3, 4, 6 nothing; others skipped
    int16_t frame = 0;  // the frame counter (0-based) it belongs to
    int16_t a = 0;
    int16_t b = 0;
    friend bool operator==(const MovieCue&, const MovieCue&) = default;
};

// 1473:07d9 Movie_Play(first, end, period, cues, bank) as a stepper:
//
//     Res_Read(first)                                   (the constructor)
//     for frame = first .. end - 1, counter = 0, 1, ...:
//         Timer_Set(0, period)                          (caller, before step())
//         Movie_DecodeFrame(buffer, VRAM)               \  step()
//         if frame + 1 != end: Res_Read(frame + 1)       |
//         if cue.frame == counter: run it, cue += 1     /   (the cue is returned for the caller to run)
//         Timer_Wait(0)                                 (caller)
//
// A Delay cue therefore runs after the frame's decode and the next frame's read, inside the frame.
// The cue table is read record by record from `cueBytes` (DS:236c to the end of DGROUP) with no
// count, as the original does: the shipped table holds 25 cues and an end record {6, 0, 0, 0}.
class MoviePlayer {
public:
    MoviePlayer(const Archive& archive, uint16_t first, uint16_t end, int period, std::span<const uint8_t> cueBytes,
                int bank);

    int period() const { return period_; }
    int bank() const { return bank_; }
    bool finished() const { return frame_ >= end_; }
    int counter() const { return counter_; }      // frames shown so far
    uint16_t frameId() const { return frame_; }   // the frame the next step decodes
    // The current cue record (the one the player compares with the counter).
    std::optional<MovieCue> pendingCue() const;
    // One frame (see above). Returns the cue of this frame, if any.
    std::optional<MovieCue> step(Vram& vram);
    // The decode result of the last step.
    const MovieDecodeResult& lastDecode() const { return last_; }

private:
    const Archive& archive_;
    uint16_t frame_;
    uint16_t end_;
    int period_;
    std::span<const uint8_t> cues_;
    size_t cueIndex_ = 0;
    int bank_;
    int counter_ = 0;
    std::vector<uint8_t> buffer_;
    MovieDecodeResult last_;
};

}  // namespace tmport::gg
