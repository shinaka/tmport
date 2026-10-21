#include "game/title.h"

#include <format>

#include "anim/anim_bundle.h"
#include "anim/anim_engine.h"
#include "anim/anim_player.h"
#include "audio/audio_system.h"
#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "mischief/util/error.h"
#include "platform/engine.h"
#include "res/game_files.h"
#include "text/text_renderer.h"
#include "ui/game_dialog.h"

namespace tmport::game {

using namespace title;

TitleScreen::TitleScreen(AppContext& ctx) : io_(ctx) {
    if (ctx.anim) {
        anim_ = ctx.anim;
    } else {
        // TODO(integration): the app area normally provides AppContext::anim; a private engine
        // is equivalent here because nothing else animates during the title.
        anim::AnimSound snd;
        if (ctx.audio) {
            snd.play = [a = ctx.audio](int n) { a->sfxPlay(n); };
            snd.stop = [a = ctx.audio] { a->sfxStop(); };
        }
        ownAnim_ = std::make_unique<anim::AnimEngine>(ctx.engine.frame(), anim::enginePresent(ctx.engine), snd);
        anim_ = ownAnim_.get();
    }
    const ExeTables& exe = io_.exe();
    voiceFirst_ = exe.array<int16_t, 4>(dg::kTitleVoiceFirst);
    voiceCount_ = exe.array<int16_t, 4>(dg::kTitleVoiceCount);
}

TitleScreen::~TitleScreen() { freeAnims(); }

bool TitleScreen::voiceOn() const {
    auto* a = const_cast<ScreenIo&>(io_).audio();
    return a && a->voiceOn();
}

// 1138:0000 Title_LoadAnims
void TitleScreen::loadAnims() {
    const ResourceManager& res = *io_.ctx().files.res;
    const anim::LoadedBundle b1 = anim::loadBundle(res, *anim_, kAnim1Bndl, 0);
    script1_ = b1.script;
    frames1_ = b1.frameCount;
    const anim::LoadedBundle b2 = anim::loadBundle(res, *anim_, kAnim2Bndl, frames1_);
    script2_ = b2.script;
    frames2_ = b2.frameCount;
}

// 1138:0037 Title_FreeAnims. The original only frees the bundles (the channels keep running
// until GameMain's reset block calls Anim_ResetAll, with no tick in between); here the channels
// that still run the title scripts are released and the frame slots cleared, which looks the same.
void TitleScreen::freeAnims() {
    if (!anim_) return;
    if (script1_ && anim_->isRunning(anim1_, script1_)) anim_->release(anim1_);
    if (script2_ && anim_->isRunning(anim2_, script2_)) anim_->release(anim2_);
    if (script1_ || script2_)
        for (int i = 0; i < frames1_ + frames2_; ++i) anim_->setFrame(i, nullptr);
    script1_.reset();
    script2_.reset();
    frames1_ = frames2_ = 0;
}

// 1138:007a Title_Run
TitleResult TitleScreen::run() {
    Engine& eng = io_.engine();
    gfx::FrameBuffer& fb = eng.frame();
    audio::AudioSystem* audio = io_.audio();

    int action = 0;       // local_4: Title_WaitPage result - 1 of the last key
    bool anyKey = false;  // bVar1
    loadAnims();
    io_.setCursorForced(true);
    eng.hotspots().set(kHotspot, 0, 20, 512, 267);
    if (audio) audio->music().playZone(kMusicOpen1);
    musicPending_ = true;
    fb.setForeColor(0);
    fb.fillRect(0, 0, 512, 384);  // not presented: page 0 presents the whole screen
    anim1_ = anim_->start(anim::layer::kDefault, kAnim1X, kAnim1Y, 0, script1_, 0, 0, anim::kChannelFlagErase);
    io_.trace("title start");

    int page = 0;
    while (!((page >= kPages && anyKey) || action != 0)) {
        if (page >= kPages) page = 0;
        anim2_ = -1;  // g_titleAnim2 = g_titleAnim3 = -1
        drawPage(page);
        const int r = waitPage(page);
        io_.trace(std::format("page {} wait -> {}", page, r));
        if (r > 0) {
            anim_->stop(anim2_, script2_);  // Anim_Stop(-1, ...) does nothing during the loop
            anyKey = true;
            action = r - 1;
        }
        ++page;
    }
    if (action == 2) {
        // Esc + Yes: Title_Run returns 0 at once (no hot-spot removal; the program ends).
        io_.trace("title quit");
        return TitleResult::Quit;
    }

    // Remove the text box: restore the title picture from the back page.
    fb.copyRect(0, gfx::FrameBuffer::kLayer1Y, 512, 384, 0, 0);
    io_.present(kBoxX, kBoxY, kBoxW, kBoxH);
    anim2_ = anim_->start(anim::layer::kDefault, kAnim2X, kAnim2Y, frames1_, script2_, 0, 0, anim::kChannelFlagErase);
    io_.trace("end animation");
    const int r = waitPage(kEndPage);
    io_.trace(std::format("end wait -> {}", r));
    eng.hotspots().remove(kHotspot);
    freeAnims();
    return r == 3 ? TitleResult::Quit : TitleResult::Continue;
}

// The page switch of Title_Run (1138:0105..01b8).
void TitleScreen::drawPage(int page) {
    gfx::FrameBuffer& fb = io_.engine().frame();
    const ResourceManager& res = *io_.ctx().files.res;
    if (page == 0) {
        auto pm = res.pixmap(kTitlePmap);
        if (!pm) throw mischief::Error(std::format("title picture PMAP {} not found", kTitlePmap));
        fb.drawOpaqueOrFlippedWhiteKey(0, 20, *pm, false);
        fb.copyRect(0, 0, 512, 384, 0, gfx::FrameBuffer::kLayer1Y);  // save the title screen in layer 1
        io_.present(0, 0, 512, 384);
    } else if (page <= 4) {
        fb.setForeColor(kBoxColour);
        fb.fillRect(kBoxX, kBoxY, kBoxW, kBoxH);
        const auto text = res.loadRaw(kPageTextFirst + page);
        if (!text) throw mischief::Error(std::format("story text OTHR {} not found", kPageTextFirst + page));
        fb.setForeColor(0);
        io_.ctx().text.drawBox(kTextX, kTextY, kTextW, mischief::text::untilNul(*text));
        io_.present(kBoxX, kBoxY, kBoxW, kBoxH);
    }
    io_.trace(std::format("page {} drawn", page));
}

// 1138:043d Title_WaitPage: the 60 s page timer is armed once for all clips of the page.
int TitleScreen::waitPage(int page) {
    gfx::FrameBuffer& fb = io_.engine().frame();
    int r = 0;
    io_.timerSet(4, kPageTimeoutMs);
    io_.timerSet(7, kTickMs);
    fb.setDirtyEnabled(true);
    if (!voiceOn() || page == 0 || page == kEndPage) {
        r = waitStep(0, true);
    } else {
        int v = voiceFirst_[static_cast<size_t>(page - 1)];
        const int count = voiceCount_[static_cast<size_t>(page - 1)];
        for (int i = 0; i < count - 1; ++i) {
            r = waitStep(v, false);
            if (r > 0) break;
            ++v;
        }
        if (r == 0) r = waitStep(v, true);  // last clip: keep waiting for a key after it
    }
    fb.setDirtyEnabled(false);
    return r;
}

// 1138:0283 Title_WaitStep: 1 = next page, 2 = end now, 3 = quit confirmed, 0 = timeout or the
// clip / ending animation finished.
int TitleScreen::waitStep(int voiceId, bool isLast) {
    Engine& eng = io_.engine();
    audio::AudioSystem* audio = io_.audio();
    const bool voiceAllowed = voiceOn();
    bool playing = false;
    if (voiceId != 0 && voiceAllowed) {
        audio->startSpeech(voiceId);  // Res_LockInPlace + Wave_Play (not gated by Music and Sound)
        playing = true;
        io_.trace(std::format("voice {}", voiceId));
    }
    io_.flushInput();
    int res = 0;
    while (res == 0) {
        if (io_.timerExpired(7)) {  // 10 animation ticks per second
            io_.timerSet(7, kTickMs);
            anim_->tick(anim::layer::kDefault);
            eng.flushDirtyRects();
        }
        if (musicPending_ && !(audio && audio->music().isZonePlaying(kMusicOpen1))) {
            if (audio) audio->music().playZone(kMusicOpen2);
            musicPending_ = false;
            io_.trace("music open2");
        }
        const uint16_t c = io_.getCode();
        if (c == 0x0D || c == kHotspot || c == (0x2000 | kHotspot)) {
            res = 1;
        } else if (c == 0x20) {
            res = 2;
        } else if (c == 0x1B) {
            if (voiceOn()) audio->voice().stop();
            if (io_.ctx().dialogs.runModal(io_.dialog(dg::kDlgTitleExit)) == 1) res = 3;
            io_.setCursorForced(true);
        } else if (c == 0x13) {
            // Ctrl+S: the event fetch already ran Audio_ToggleAll; with voice now off the clip stops.
            if (!voiceOn() && audio) audio->voice().stop();
        } else if (io_.timerExpired(4)) {
            res = -1;
        } else if (!isLast && playing && !(audio && audio->voice().isPlaying())) {
            res = -2;
        } else if (anim2_ >= 0 && !anim_->isRunning(anim2_, script2_)) {
            res = -2;
        }
        // g_titleAnim3 is never started (always -1): its test is omitted.
    }
    if (audio) audio->voice().stop();  // Wave_Stop
    return res < 0 ? 0 : res;
}

TitleResult runTitle(AppContext& ctx) {
    TitleScreen t(ctx);
    return t.run();
}

}  // namespace tmport::game
