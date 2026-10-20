#pragma once
// Helpers the vehicles module's files share (workshop.cpp, race.cpp, results.cpp): the sound handles of
// GgState, the screen animations' sound ops on g_effectBank, a screen animation played modally, the
// size of a picture, and the in-place recolouring of cached pictures. Internal to the vehicles area.
//
// Owner: vehicles agent.

#include <cstdint>
#include <optional>
#include <span>

#include "gg/game/game.h"
#include "gg/game/screen_anim.h"

namespace tmport::gg::vehicles {

// GgState::sound (GgGame's `sound` is the facade).
inline SoundState& snd(GgGame& g) { return static_cast<GgState&>(g).sound; }

// The screen animations' sound ops on g_effectBank DS:7a44 (174b:001a ops -36, -37, -45); the busy-wait
// of op -45 pumps the runtime.
inline ScreenAnimSound animSound(GgGame& g) {
    ScreenAnimSound fx;
    fx.play = [&g](int member, int loop, int priority) { g.sound.playEffect(snd(g).effectBank + member, loop, priority); };
    fx.stop = [&g](int member) { g.sound.stopEffect(snd(g).effectBank + member); };
    fx.isPlaying = [&g](int member) {
        g.rt.idle();
        return g.sound.isPlaying(snd(g).effectBank + member) != 0;
    };
    return fx;
}

// 174b:0510 ScreenAnim_Frame on the game's running list g_screenAnims.
inline void animFrame(GgGame& g) { g.screenAnims.frame(g.video.screen(), g.video.sprites(), g.rand, animSound(g)); }

// 174b:0444 ScreenAnim_Free: stopped when in the list, its frame bundle released.
inline void freeAnim(GgGame& g, std::optional<ScreenAnim>& a) {
    if (!a) return;
    g.screenAnims.stop(*a);
    g.cache.releaseBundle(a->frameBundle());
    a.reset();
}

// Obj_SetSizeFromPicture 15df:0414: w and h from the LPX header (+6, +8).
inline void sizeFromPicture(std::span<const uint8_t> pic, int16_t& w, int16_t& h) {
    if (pic.size() < 10) {
        w = h = 0;
        return;
    }
    w = static_cast<int16_t>(pic[6] | (pic[7] << 8));
    h = static_cast<int16_t>(pic[8] | (pic[9] << 8));
}

// A cached picture as the original's Gfx_RecolorPicture changes it: in place. The picture cache keeps
// each loaded resource in a buffer of its own (gg/game/pic_cache.h), so the bytes are the cache's and
// writable; every user of the same cached picture sees the change, as in the original.
inline std::span<uint8_t> cachedPictureBytes(std::span<const uint8_t> pic) {
    return {const_cast<uint8_t*>(pic.data()), pic.size()};
}

}  // namespace tmport::gg::vehicles
