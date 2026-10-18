#include "game/scenes/celebration.h"

#include "anim/anim_bundle.h"
#include "anim/anim_engine.h"
#include "anim/anim_player.h"
#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "input/input_codes.h"
#include "platform/engine.h"
#include "res/resource_manager.h"
#include "text/text_renderer.h"

namespace tmport::game::scenes {

RankBand rankBand(int rank, int step) {
    RankBand b;
    b.clipBottom = rankscreen::kBandBottom0 - rankscreen::kBandStep * rank;
    b.clipTop = b.clipBottom - 1 - step;
    b.presentY = rankscreen::kRankNameY0 - rankscreen::kBandStep * rank;
    return b;
}

namespace {

// 10f0:06af RankScreen_DrawLabels
void drawLabels(SceneIo& io) {
    io.fb().setForeColor(0);
    int y = rankscreen::kLabelY0;
    for (int i = 0; i < rankscreen::kLabelCount; ++i) {
        io.text().draw(rankscreen::kLabelX, y, io.exe().farCstr(dg::kRankThresholdLabels + 4 * static_cast<uint32_t>(i)));
        y -= rankscreen::kBandStep;
    }
}

// 10f0:0702 RankScreen_AnimateStep(rank)
void animateStep(SceneIo& io, const gfx::PixMap& fill, int rank) {
    gfx::FrameBuffer& fb = io.fb();
    gfx::ClipRect clip = fb.clip();
    const RankBand b0 = rankBand(rank, 0);
    clip.bottom = b0.clipBottom;
    clip.top = b0.clipTop;
    for (int k = 0; k < rankscreen::kStepsPerRank; ++k) {
        io.timerSet(4, rankscreen::kStepMs);
        fb.setClip(clip);
        fb.drawSprite(rankscreen::kFillX, rankscreen::kFillY, fill, false);
        drawLabels(io);
        io.present(rankscreen::kPresentX, b0.presentY, rankscreen::kPresentW, rankscreen::kPresentH);
        --clip.top;
        fb.setClip(clip);
        while (!io.timerExpired(4))
            if (io.getCode() == input::kEsc) io.exitGame();
    }
    fb.setForeColor(0);
    io.text().drawBox(rankscreen::kRankNameX, b0.presentY, rankscreen::kRankNameW,
                      io.exe().farCstr(dg::kRankNames + 4 * static_cast<uint32_t>(rank)));
    io.present(rankscreen::kPresentX, b0.presentY, rankscreen::kPresentW, rankscreen::kPresentH);
}

void rankScreen(SceneIo& io, GameState& s, const GameTables& t) {
    gfx::FrameBuffer& fb = io.fb();
    const auto fill = io.pixmap(rankscreen::kFill);
    HotspotTable& hs = io.engine().hotspots();
    hs.set(rankscreen::kClickHotspot, 0, gfx::FrameBuffer::kPlayTop, 512, gfx::FrameBuffer::kPlayHeight);
    fb.drawOpaqueOrFlippedWhiteKey(0, gfx::FrameBuffer::kPlayTop, *io.pixmap(rankscreen::kBackground), false);
    fb.setForeColor(0);
    fb.hline(0, gfx::FrameBuffer::kPlayTop + gfx::FrameBuffer::kPlayHeight, 512);
    // Text_DrawBox keeps the pen colour of the line (EGA 0).
    const std::string name = s.player.nameString();
    const std::span<const uint8_t> nameBytes(reinterpret_cast<const uint8_t*>(name.data()), name.size());
    io.text().drawBox(rankscreen::kNameX, rankscreen::kNameY, rankscreen::kNameW,
                      formatBytes(io.exe().cstr(dg::kFmtName), nameBytes, 0));
    io.text().drawBox(rankscreen::kTotalX, rankscreen::kTotalY, rankscreen::kTotalW,
                      formatBytes(io.exe().cstr(dg::kFmtTotalTreasures), {}, s.rank.totalTreasures));
    drawLabels(io);
    io.present(0, gfx::FrameBuffer::kPlayTop, 512, gfx::FrameBuffer::kPlayHeight + 1);
    io.sfxPlay(rankscreen::kSfx);
    s.rank.rank = static_cast<int16_t>(rankFromTotal(s.rank.totalTreasures, t));
    io.tracef("rank screen: total {} rank {}", s.rank.totalTreasures, s.rank.rank);
    for (int r = 0; r <= s.rank.rank; ++r) animateStep(io, *fill, r);
    io.sfxStop();
    for (bool done = false; !done;) {
        const uint16_t c = io.waitCode();
        if (c == input::kEnter || c == input::kSpace)
            done = true;
        else if (c == input::kEsc)
            io.exitGame();
    }
    hs.remove(rankscreen::kClickHotspot);
    io.flushInput();
}

// One Anim_PlayModal of the celebration: layer 0 at (0,20), 4 fps, skippable, Esc = Exit dialog.
int playCelebrationAnim(SceneIo& io, anim::AnimEngine& anim, const anim::ScriptPtr& script) {
    anim::ModalParams p;
    p.script = script;
    p.frameBase = 0;
    p.skippable = true;
    p.onEsc = [&io] {
        io.exitGame();  // throws QuitGame on Yes
        return false;
    };
    p.x = celebration::kAnimX;
    p.y = celebration::kAnimY;
    p.fps = celebration::kFps;
    const int r = anim::playModal(anim, io.engine(), p);
    io.tracef("celebration DAM {} result {}", script ? script->id : -1, r);
    return r;
}

// Background picture + line + copy to the back page (10f0:0038.. / 024b..).
void drawBackground(SceneIo& io, int pmap) {
    gfx::FrameBuffer& fb = io.fb();
    fb.drawOpaqueOrFlippedWhiteKey(0, gfx::FrameBuffer::kPlayTop, *io.pixmap(pmap), false);
    fb.setForeColor(0);
    fb.hline(0, gfx::FrameBuffer::kPlayTop + gfx::FrameBuffer::kPlayHeight, 512);
    fb.copyRect(0, gfx::FrameBuffer::kPlayTop, 512, gfx::FrameBuffer::kPlayHeight, 0,
                gfx::FrameBuffer::kPlayTop + gfx::FrameBuffer::kLayer1Y);
}

}  // namespace

void showRankScreen(AppContext& ctx, GameState& s, const GameTables& t, const SceneHooks& hooks) {
    SceneIo io(ctx, hooks);
    rankScreen(io, s, t);
}

void seeRank(AppContext& ctx, GameState& s, const GameTables& t, const SceneHooks& hooks) {
    SceneIo io(ctx, hooks);
    gfx::FrameBuffer& fb = io.fb();
    const bool savedDirty = fb.dirtyEnabled();
    fb.setDirtyEnabled(false);
    const gfx::PixMap saved = fb.saveRect(0, gfx::FrameBuffer::kPlayTop, 512, gfx::FrameBuffer::kPlayHeight + 1);
    io.setCursorForced(true);
    MenuDisable menu(ctx);
    rankScreen(io, s, t);
    io.setCursorForced(false);
    fb.restoreRect(0, gfx::FrameBuffer::kPlayTop, saved);
    io.present(0, gfx::FrameBuffer::kPlayTop, 512, gfx::FrameBuffer::kPlayHeight + 1);
    menu.restore();
    fb.setDirtyEnabled(savedDirty);
}

CelebrationInfo celebrate(AppContext& ctx, GameState& s, const GameTables& t, const SceneHooks& hooks) {
    SceneIo io(ctx, hooks);
    io.setCursorForced(true);
    const CelebrationInfo info = celebrateClimb(s, t);
    io.tracef("celebration: climbs {} total {} rank {} promoted {} champion {} treasure {}", s.rank.climbsDone,
              s.rank.totalTreasures, info.rank, info.promoted ? 1 : 0, info.champion ? 1 : 0, info.deliveredTreasure);
    io.musicPlayZone(info.musicZone);
    gfx::FrameBuffer& fb = io.fb();
    fb.setPlayClip(0);
    // Fx_FadeOut_Stub(-1): a stub returning 1.
    drawBackground(io, celebration::kBackground1);
    anim::AnimEngine& anim = io.anim();
    const ResourceManager& res = io.res();
    const anim::LoadedBundle start = anim::loadBundle(res, anim, celebration::kBndlStart, 0);
    anim.setFrame(anim::kDeliveredTreasureSlot,
                  anim::bundleMember(res, kTreasureBndlFirstId + info.deliveredTreasure, 0));
    const anim::ScriptPtr treasure = anim::loadScript(res, celebration::kScriptTreasure);
    io.present(0, gfx::FrameBuffer::kPlayTop, 512, gfx::FrameBuffer::kPlayHeight + 1);
    // Fx_FadeIn_Stub(-1): a stub.
    if (playCelebrationAnim(io, anim, start.script) != anim::kModalFinished) return info;
    if (playCelebrationAnim(io, anim, treasure) != anim::kModalFinished) return info;
    int r = 0;
    if (info.champion) {
        // BNDL 323 is freed first; 324 loads its frames from slot 0 over 323's.
        const anim::LoadedBundle champ = anim::loadBundle(res, anim, celebration::kBndlChampion, 0);
        r = playCelebrationAnim(io, anim, champ.script);
    } else {
        r = playCelebrationAnim(io, anim, anim::loadScript(res, celebration::kScriptNormal));
    }
    if (r != anim::kModalFinished) return info;  // (the original leaks 0f78 / 0f7c here)
    drawBackground(io, celebration::kBackground2);
    const anim::LoadedBundle fin = anim::loadBundle(res, anim, celebration::kBndlFinal, 0);
    io.present(0, gfx::FrameBuffer::kPlayTop, 512, gfx::FrameBuffer::kPlayHeight + 1);
    playCelebrationAnim(io, anim, fin.script);  // result ignored
    rankScreen(io, s, t);
    fb.setFullClip();  // 10e0:0391 Gfx_ResetClip (rows 0..767)
    return info;
}

}  // namespace tmport::game::scenes
