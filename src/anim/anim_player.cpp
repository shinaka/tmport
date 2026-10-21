#include "anim/anim_player.h"

#include "input/input_codes.h"
#include "mischief/timing/deadline_timers.h"
#include "mischief/util/log.h"
#include "platform/engine.h"

namespace tmport::anim {

void EngineModalHost::timerSet(int slot, int32_t ms) { engine_.timerSet(slot, ms); }
bool EngineModalHost::timerExpired(int slot) { return engine_.timerExpired(slot); }
void EngineModalHost::flushInput() { engine_.flushInput(); }
uint16_t EngineModalHost::getInputCode() { return engine_.getInputCode(); }
void EngineModalHost::flushDirtyRects() { engine_.flushDirtyRects(); }

PresentFn enginePresent(Engine& engine) {
    return [&engine](int x, int y, int w, int h) { engine.presenter().blitToWindow(engine.frame(), x, y, w, h); };
}

int32_t modalTickMs(int fps) {
    if (fps == 0) {
        mischief::log::warn("Anim_PlayModal: fps 0 (the original would fault); using 1");
        fps = 1;
    }
    // 10d8:00e4: (long)(int)(1000L / fps), truncated toward zero.
    return static_cast<int32_t>(static_cast<int16_t>(1000L / fps));
}

// 10d8:0082 Anim_PlayModal
int playModal(AnimEngine& anim, ModalHost& host, const ModalParams& p) {
    gfx::FrameBuffer& fb = anim.frameBuffer();
    const bool savedDirty = fb.dirtyEnabled();
    fb.setDirtyEnabled(true);
    int result = kModalFinished;
    const int32_t period = modalTickMs(p.fps);
    const int ch = anim.start(layer::kDefault, p.x, p.y, p.frameBase, p.script, p.user0, p.user1, kChannelFlagErase, {});
    constexpr int kSlot = mischief::DeadlineTimers::kFrameSlot;
    host.timerSet(kSlot, 0);
    host.flushInput();
    do {
        if (host.timerExpired(kSlot)) {
            host.timerSet(kSlot, period);
            anim.eraseSprites(kBgPageOffset);
            anim.step();
            anim.drawSprites(layer::kDefault, 0, kBgPageOffset);
            anim.flushUpdateRects();
            host.flushDirtyRects();
        }
        const uint16_t k = host.getInputCode();
        if ((k == input::kEnter || k == input::kSpace) && p.skippable) {
            anim.stop(ch, p.script);
            result = k == input::kEnter ? kModalEnter : kModalSpace;
        } else if (k == input::kEsc && p.onEsc) {
            if (p.onEsc()) {
                if (anim.isRunning(ch, p.script)) anim.release(ch);
                result = kModalQuit;
                break;
            }
            // No: the animation simply continues.
        }
    } while (anim.isRunning(ch, p.script));
    fb.setDirtyEnabled(savedDirty);
    return result;
}

int playModal(AnimEngine& anim, Engine& engine, const ModalParams& params) {
    EngineModalHost host(engine);
    return playModal(anim, host, params);
}

}  // namespace tmport::anim
