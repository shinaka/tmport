#include "gg/game/pointer_warp.h"

#include <SDL3/SDL.h>

#include "gg/game/game.h"
#include "platform/host.h"

namespace tmport::gg {

PointerWarpHook::PointerWarpHook(GgGame& g) : g_(g) {
    platform::Host* host = g.rt.host();
    if (g.rt.deterministic() || !host || !host->hasWindow()) return;
    previous_ = g.mouse().onWarp;
    g.mouse().onWarp = [host](int16_t x, int16_t y) {
        // windowToCanvas is affine (the presenter's logical size and letterbox): invert it from two
        // probes and aim at the centre of the canvas pixel, so the motion event maps back to (x, y).
        float c0x = 0, c0y = 0, c1x = 0, c1y = 0;
        if (!host->windowToCanvas(0.0f, 0.0f, c0x, c0y) || !host->windowToCanvas(1000.0f, 1000.0f, c1x, c1y)) return;
        const float sx = (c1x - c0x) / 1000.0f, sy = (c1y - c0y) / 1000.0f;
        if (sx <= 0.0f || sy <= 0.0f) return;
        const float wx = (static_cast<float>(x) + 0.5f - c0x) / sx;
        const float wy = (static_cast<float>(y) + 0.5f - c0y) / sy;
        SDL_WarpMouseInWindow(host->window(), wx, wy);
    };
    installed_ = true;
}

PointerWarpHook::~PointerWarpHook() {
    if (installed_) g_.mouse().onWarp = std::move(previous_);
}

}  // namespace tmport::gg
