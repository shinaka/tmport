#pragma once
// The Technology Center hub, state 3 of Seq_Run (12e0:0148; re/gg/docs/code_world.md §3): its screen
// set-up and its frame, the drawing half. The input dispatch (buildings, the sign-in booth, Esc), the
// music and the leaving are wave 3's; they need nothing more than this object.
//
// Set-up order (§3.1): the picture 0x1002 with palette 0x1004 at (0, 0), Pal_LoadRes(0xf6),
// Screen_SaveAll; the screen animations 0x1005 and 0x1007 loaded and started; the trophies (bundle
// 0x1003: member 0 x table, 1 y table, 2 + t picture t, t = building * 5 + k) recoloured from
// "STUVW" to 0x20 + 5 * colour + i and drawn after the back page was saved; the four cursor labels
// of 0x101f and the eight hover regions over the zones DS:0132 (cursor = label code - 0xbb8).

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "gg/game/screen_anim.h"
#include "gg/gfx/cursor.h"

namespace tmport {
class BorlandRand;
}

namespace tmport::gg {

class Archive;
class GgExe;
class PicCache;
class Video;

// What the hub reads of the player's progress.
struct HubProgress {
    std::array<int, 3> racesWon{-1, -1, -1};  // g_racesWon DS:4596 (-1..4)
    std::array<uint8_t, 15> vehicleColor{};   // g_vehicleColor DS:5c10 (record +0x16AE)
    int racesWonTotal = 0;                    // g_racesWonTotal DS:5c0e
};

// A hub click zone (DS:0132, 8 records of 0x16 bytes; ARCHITECTURE.md §4.5).
struct HubZone {
    int shape = 0, x = 0, y = 0, w = 0, h = 0, priority = 0;
    int pressCode = 0, releaseCode = 0, tag = 0;
};

class HubScreen {
public:
    static constexpr uint16_t kPicture = 0x1002;
    static constexpr uint16_t kPalette = 0x1004;
    static constexpr uint16_t kPaletteExtra = 0x00F6;
    static constexpr uint16_t kTrophies = 0x1003;
    static constexpr uint16_t kCursorLabels = 0x101F;
    static constexpr std::array<uint16_t, 2> kAnims = {0x1005, 0x1007};
    static constexpr int kSignIn = 0xBB8;  // release codes 0xbb8..0xbbb

    // The zones DS:0132 of the user's ssg.exe.
    static std::array<HubZone, 8> zones(const GgExe& exe);

    // State_03_Hub up to the frame loop (12e0:0148..03bc), the drawing part: picture, palettes,
    // back page, animations, trophies, cursor labels and hover regions; Sprites_Reset. The palette
    // loads go into video's shadow (a tool shows the shadow afterwards; the runtime fades in at the
    // first frame).
    void setup(Video& video, const GgExe& exe, const Archive& archive, PicCache& cache, const HubProgress& progress);
    // One frame of the loop (12e0:03bf): ScreenAnim_Frame; the caller flushes or fades in.
    void frame(Video& video, BorlandRand& rng);
    // Leaving (12e0:0479..): the hover regions cleared and the default cursor restored; the labels
    // and animations are freed with this object. Pal_FadeOut is the caller's.
    void leave(Video& video);

    ScreenAnims& anims() { return list_; }
    ScreenAnim& anim(size_t i) { return anims_.at(i); }
    const std::array<HubZone, 8>& hubZones() const { return zones_; }
    bool complete() const { return complete_; }

private:
    std::vector<ScreenAnim> anims_;
    ScreenAnims list_;
    std::vector<uint8_t> labels_;                      // the 0x101f copy (Res_LoadCopy)
    std::vector<std::unique_ptr<CursorRecord>> cursors_;  // Mouse_LoadCursors of members 0..3
    std::array<CursorRegion, 8> regions_{};
    std::array<HubZone, 8> zones_{};
    bool complete_ = false;
};

}  // namespace tmport::gg
