#pragma once
// The screen animations Attract_PlayAnim drives (ScreenAnimHost, attract.h) on the world area's
// ScreenAnim (174b, gg/game/screen_anim.h; re/gg/docs/code_world.md §19.1) and picture cache (15df):
//
//   load(id)       174b:0324 ScreenAnim_Load: the set (a private copy) and its frame bundle through
//                  PicCache_LoadBundle (palette members into the shadow, the cache's DAC load hook)
//   startAt(h, i)  174b:02ab ScreenAnim_StartAt into the running list g_screenAnims DS:7a46 (the game's
//                  GgGame::screenAnims, or a list of the host's own)
//   frame()        174b:0510 ScreenAnim_Frame: Sprites_EraseAll, step and queue, Sprites_DrawAll
//   release(h)     174b:0444 ScreenAnim_Free: out of the list, the frame bundle released
//
// The steps' sounds (ops -36 / -37 / -45 on the current effect bank) go to `sound`; ops -34 / -35
// draw from `rng` (the game's one rand(); the attract's scripts have no such step).

#include <cstdint>
#include <deque>

#include "gg/game/screen_anim.h"
#include "gg/ui/attract.h"

namespace tmport {
class BorlandRand;
}

namespace tmport::gg {

class PicCache;
class Video;

class AttractAnimHost final : public ScreenAnimHost {
public:
    // Everything is borrowed and must outlive the host. `running`: the running list (the game's
    // g_screenAnims); nullptr: a list of the host's own.
    AttractAnimHost(Video& video, PicCache& cache, BorlandRand& rng, ScreenAnimSound sound = {},
                    ScreenAnims* running = nullptr);
    ~AttractAnimHost() override;
    AttractAnimHost(const AttractAnimHost&) = delete;
    AttractAnimHost& operator=(const AttractAnimHost&) = delete;

    int load(uint16_t id) override;
    void startAt(int handle, int pos) override;
    bool inList(int handle) const override;
    bool stopped(int handle) const override;
    void frame() override;
    void release(int handle) override;
    void resetSprites() override;

    ScreenAnims& running() { return *running_; }

private:
    struct Slot {
        ScreenAnim anim;
        bool loaded = false;
    };
    Slot& slot(int handle);
    const Slot& slot(int handle) const;

    Video& video_;
    PicCache& cache_;
    BorlandRand& rng_;
    ScreenAnimSound sound_;
    std::deque<Slot> slots_;  // stable addresses: the running list points into it
    ScreenAnims list_;
    ScreenAnims* running_;
};

}  // namespace tmport::gg
