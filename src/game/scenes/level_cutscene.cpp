#include "game/scenes/level_cutscene.h"

#include "anim/anim_bundle.h"
#include "anim/anim_engine.h"
#include "anim/anim_player.h"
#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "platform/engine.h"
#include "res/resource_manager.h"

namespace tmport::game::scenes {

CutsceneIds cutsceneIds(const ExeTables& exe, int oldLevel) {
    CutsceneIds ids;
    const uint32_t i = static_cast<uint32_t>(oldLevel) * 2;
    ids.bundle = exe.u16(dg::kCutBndlIds + i);
    ids.script2 = exe.u16(dg::kCutScript2Ids + i);
    ids.background = exe.u16(dg::kCutBgIds + i);
    return ids;
}

int playLevelCutscene(AppContext& ctx, int oldLevel, const SceneHooks& hooks) {
    SceneIo io(ctx, hooks);
    if (oldLevel >= cutscene::kLevels || oldLevel < 0) {
        // Signed compare in the original: only levels 0 and 1 have a cut-scene.
        io.clearGateUnlocking();
        return -1;
    }
    io.tracef("level cut-scene {}", oldLevel);
    io.setCursorForced(true);
    MenuDisable menu(ctx);
    gfx::FrameBuffer& fb = io.fb();
    fb.setPlayClip(0);
    const CutsceneIds ids = cutsceneIds(io.exe(), oldLevel);
    anim::AnimEngine& anim = io.anim();
    const anim::LoadedBundle part1 = anim::loadBundle(io.res(), anim, ids.bundle, 0);
    const anim::ScriptPtr part2 = anim::loadScript(io.res(), ids.script2);

    anim::ModalParams p;
    p.script = part1.script;
    p.frameBase = 0;
    p.skippable = true;
    p.onEsc = nullptr;  // Esc is ignored during the cut-scene
    p.x = 0;
    p.y = 0;
    p.fps = cutscene::kFps;
    const int r = anim::playModal(anim, io.engine(), p);
    io.tracef("cut-scene part 1 result {}", r);
    io.clearGateUnlocking();
    if (r != anim::kModalSpace) {
        constexpr int top = gfx::FrameBuffer::kPlayTop, h = gfx::FrameBuffer::kPlayHeight;
        fb.setForeColor(0);
        fb.fillRect(0, top, 512, h);
        io.present(0, top, 512, h);
        fb.drawOpaqueOrFlippedWhiteKey(0, top, *io.pixmap(ids.background), false);
        fb.setForeColor(0);
        fb.hline(0, top + h, 512);
        fb.copyRect(0, top, 512, h, 0, top + gfx::FrameBuffer::kLayer1Y);
        io.present(0, top, 512, h + 1);
        p.script = part2;
        const int r2 = anim::playModal(anim, io.engine(), p);
        io.tracef("cut-scene part 2 result {}", r2);
        fb.setForeColor(0);
        fb.fillRect(0, top, 512, h);
        io.present(0, top, 512, h);
    }
    // LevelCutscene_Free (1188:01aa): the bundle and script are released with their owners.
    io.sfxStop();
    io.setCursorForced(false);
    menu.restore();
    return r;
}

}  // namespace tmport::game::scenes
