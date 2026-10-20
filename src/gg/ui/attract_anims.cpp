#include "gg/ui/attract_anims.h"

#include <stdexcept>

#include "gg/game/pic_cache.h"
#include "gg/gfx/video.h"

namespace tmport::gg {

AttractAnimHost::AttractAnimHost(Video& video, PicCache& cache, BorlandRand& rng, ScreenAnimSound sound)
    : video_(video), cache_(cache), rng_(rng), sound_(std::move(sound)) {}

AttractAnimHost::~AttractAnimHost() {
    for (int h = 0; h < static_cast<int>(slots_.size()); ++h)
        if (slots_[static_cast<size_t>(h)].loaded) release(h);
}

AttractAnimHost::Slot& AttractAnimHost::slot(int handle) {
    if (handle < 0 || static_cast<size_t>(handle) >= slots_.size()) throw std::out_of_range("screen animation handle");
    return slots_[static_cast<size_t>(handle)];
}

const AttractAnimHost::Slot& AttractAnimHost::slot(int handle) const {
    if (handle < 0 || static_cast<size_t>(handle) >= slots_.size()) throw std::out_of_range("screen animation handle");
    return slots_[static_cast<size_t>(handle)];
}

// 174b:0324 ScreenAnim_Load
int AttractAnimHost::load(uint16_t id) {
    slots_.push_back(Slot{ScreenAnim::load(cache_, id), true});
    return static_cast<int>(slots_.size()) - 1;
}

// 174b:02ab ScreenAnim_StartAt
void AttractAnimHost::startAt(int handle, int pos) { list_.startAt(slot(handle).anim, pos); }

bool AttractAnimHost::inList(int handle) const { return slot(handle).anim.active; }

bool AttractAnimHost::stopped(int handle) const { return slot(handle).anim.state() != 0; }

// 174b:0510 ScreenAnim_Frame
void AttractAnimHost::frame() { list_.frame(video_.screen(), video_.sprites(), rng_, sound_); }

// 174b:0444 ScreenAnim_Free: out of the running list, the frame bundle released (PicCache_ReleaseBundle)
void AttractAnimHost::release(int handle) {
    Slot& s = slot(handle);
    if (!s.loaded) return;
    list_.stop(s.anim);
    cache_.releaseBundle(s.anim.frameBundle());
    s.loaded = false;
}

// 1581:0484 Sprites_Reset
void AttractAnimHost::resetSprites() { video_.sprites().reset(); }

}  // namespace tmport::gg
