#include "gg/gfx/sprites.h"

#include "gg/gfx/screen.h"
#include "util/log.h"

namespace tmport::gg {

// 1581:0496 Sprites_Add
void SpriteList::add(const Sprite& s) {
    if (count_ >= kCapacity) {
        log::debug("Sprites_Add: the list is full ({} records); sprite dropped", kCapacity);
        return;
    }
    list_[count_++] = s;
}

// 1581:04b6 Sprites_EraseAll
void SpriteList::eraseAll(Screen& screen) {
    for (size_t i = 0; i < count_; ++i) {
        if (list_[i].flags & kNoErase) break;  // SI is not advanced: the rest is skipped too
        screen.restoreRect(list_[i].x, list_[i].y, list_[i].w, list_[i].h);
    }
    reset();
}

// 1581:04fc Sprites_DrawAll
void SpriteList::drawAll(Screen& screen) const {
    for (size_t i = 0; i < count_; ++i)
        if (!(list_[i].flags & kNoDraw)) screen.lpxDrawKeyed(list_[i].x, list_[i].y, list_[i].picture, list_[i].flags);
}

}  // namespace tmport::gg
