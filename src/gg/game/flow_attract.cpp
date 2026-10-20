// State 1, the attract (flow.h; ssg.exe 2c11, re/gg/docs/code_ui.md §10), on GgGame: the steps draw
// through gg/ui/attract.h, the animations run on the game's picture cache and running list
// (AttractAnimHost over GgGame::screenAnims), the sounds go through the game's facade. The wave 2
// preview's attract (gg/app/preview.cpp), whose frames equal the DOSBox-X captures, is the same code
// on its own objects. Owner: flow agent.
#include <format>
#include <map>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/data/resources.h"
#include "gg/game/flow.h"
#include "gg/game/game.h"
#include "gg/ui/attract.h"
#include "gg/ui/attract_anims.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kKeyEnter = 0x000D;
constexpr uint16_t kKeyEsc = 0x001B;
constexpr uint16_t kKeySpace = 0x0020;
constexpr int kTicksPerSecond = 146;  // Attract_PlayAnim's Timer_Set(1, n * 146)

const AttractAnims& attractAnims(GgGame& g) {
    std::optional<AttractAnims>& a = g.flowObjects().attract;
    if (!a) a = AttractAnims::fromExe(g.exe());  // DS:2090, 2096, 209c, 20a2
    return *a;
}

// The steps' answer to a key (code_ui.md §10.3): Enter -> 0 (the next step); Space -> -1 (the attract
// stops: the sign-in); Esc -> -1 with g_leaveRequest = -1 (the main sequence stops: exit(0)); anything
// else (a time-out, a watched channel's end) -> 0.
int16_t stepResult(GgGame& g, int code) {
    if (code == kKeyEsc) {
        g.flow.leaveRequest = -1;
        return -1;
    }
    return code == kKeySpace ? int16_t{-1} : int16_t{0};
}

// The effect ops of the attract's screen animations (-36 play, -37 stop, -45 wait) on g_effectBank.
ScreenAnimSound attractSound(GgGame& g) {
    ScreenAnimSound fx;
    fx.play = [&g](int member, int loop, int priority) {
        if (g.GgState::sound.effectBank >= 0) g.sound.playEffect(g.GgState::sound.effectBank + member, loop, priority);
    };
    fx.stop = [&g](int member) {
        if (g.GgState::sound.effectBank >= 0) g.sound.stopEffect(g.GgState::sound.effectBank + member);
    };
    fx.isPlaying = [&g](int member) {
        g.rt.idle();  // op -45's busy-wait lets the sound run
        return g.GgState::sound.effectBank >= 0 && g.sound.isPlaying(g.GgState::sound.effectBank + member) == 1;
    };
    return fx;
}

}  // namespace

// DS:20a8, the attract's sub-sequence
std::array<StateFn, 13> attractStateTable(GgGame& g) {
    using Step = int16_t (*)(GgGame&);
    static const std::map<uint32_t, Step> kSteps = {
        {0x2c11043f, &attractTitleAnim},    {0x2c1105cf, &attractAfterTitle},    {0x2c110630, &attractStoryPage},
        {0x2c11077f, &attractAfterStory},   {0x2c1107e5, &attractBeforePrompt},  {0x2c11084c, &attractSignInPrompt},
    };
    std::array<StateFn, 13> t{};
    for (size_t i = 0; i < t.size(); ++i) {
        const GgExe::Address p = g.exe().dsFarPtr(static_cast<uint16_t>(dg::kAttractSequence + 4 * i));
        if (p.isNull()) continue;
        const GgExe::Address f = g.exe().resolveCode(p);
        const auto it = kSteps.find((static_cast<uint32_t>(f.seg) << 16) | f.off);
        if (it == kSteps.end())
            throw FormatError(std::format("the attract's sequence DS:20a8: entry {} points at {:04x}:{:04x}, no attract step of v1.0",
                                          i, f.seg, f.off));
        const Step step = it->second;
        t[i] = [&g, step] { return step(g); };
    }
    return t;
}

// 2c11:0913 State_01_Attract
int16_t state01Attract(GgGame& g) {
    FlowObjects& fo = g.flowObjects();
    g.video.cursor().hide();  // Mouse_Hide: no cursor on these pages
    g.GgState::sound.effectBank = static_cast<int16_t>(g.sound.loadEffectBank(kAttractEffects));  // 14 effects
    g.sound.setEffectBank(g.GgState::sound.effectBank);
    g.GgState::sound.musicBank = static_cast<int16_t>(g.sound.loadMusicBank(kAttractMusic));
    g.sound.playMusic(g.GgState::sound.musicBank, 0, 1);
    // Zones_AddLocal(DS:0a3a): the whole screen, priority 5, release 0x0D (a click = Enter).
    fo.clickEnter = Zone::fromRecord(g.exe().dsBytes(dg::kZoneClickEnter, Zone::kRecordSize));
    g.zones().addLocal(fo.clickEnter);
    g.flow.leaveRequest = 2;
    {
        // Leaving by an exception (the end of a run): the zone goes.
        struct ZoneGuard {
            GgGame& g;
            bool armed = true;
            ~ZoneGuard() {
                if (armed) g.zones().clearLocal();
            }
        } guard{g};
        // Seq_Run(DS:20a8, &local = 1)
        const auto table = attractStateTable(g);
        int16_t step = 1;
        seqRun(table, step);
        guard.armed = false;
    }
    g.display.fadeOut();
    g.zones().clearLocal();
    // The banks are freed; DS:9dc4 and DS:7a44 keep their (stale) handles, as in the original.
    g.sound.freeMusicBank(g.GgState::sound.musicBank);
    g.sound.freeEffectBank(g.GgState::sound.effectBank);
    g.video.cursor().show();  // Mouse_Show
    return g.flow.leaveRequest;  // 2 -> the sign-in; -1 -> the main Seq_Run stops (exit(0))
}

// 2c11:0000 Attract_PlayAnim(anim, fade, flags, vararg)
int attractPlayAnim(GgGame& g, const AttractAnim& anim, int fade, int flags, int arg) {
    AttractAnimHost host(g.video, g.cache, g.rand, attractSound(g), &g.screenAnims);
    AttractAnimPlayer player(g.exe(), anim, host);  // ScreenAnim_Load of every id, Sprites_Reset
    if (flags & kAnimTimed) g.timers().set(1, static_cast<uint16_t>(arg * kTicksPerSecond));
    const int watch = (flags & kAnimWatch) ? arg : -1;
    for (bool first = true;; first = false) {
        g.timers().set(0, 4);
        player.startChannels();
        host.frame();  // ScreenAnim_Frame
        g.display.flush();
        g.rt.timerWait(0);
        if (first) {
            if (fade == kAnimFadeIn) g.display.fadeIn();
            else if (fade == kAnimFadeFromSaved) g.display.fadeFromSaved();
        }
        player.advance();
        // 2c11:02fb: the time-out leaves with 0, but the input is still read (its code is returned).
        bool leave = false;
        int result = 0;
        if ((flags & kAnimTimed) && g.rt.timerExpired(1)) leave = true;
        if (flags & kAnimInput) {
            result = g.rt.getInputCode();  // other codes are lost
            if (result == kKeyEnter || result == kKeyEsc || result == kKeySpace) leave = true;
        }
        if (watch >= 0 && player.channelAtEnd(watch)) leave = true;
        if (leave) return result;  // the player's destructor: ScreenAnim_Free of every instance
    }
}

// 2c11:043f Attract_TitleAnim
int16_t attractTitleAnim(GgGame& g) {
    const AttractAnims& a = attractAnims(g);
    g.display.runLoad(attractTitleBackground(g.video, g.archive()));
    const Bundle art = Bundle::load(g.archive(), kTitleArt);  // Res_LoadCopy (freed at the end)
    // Phase 1 (1 s, the fade-in); Enter -> the next step, Space / Esc -> the steps' answer.
    int r = attractPlayAnim(g, a.title, kAnimFadeIn, kAnimTimed | kAnimInput, kTitlePhaseSeconds[0]);
    if (r == kKeyEnter) return 0;
    if (r == kKeyEsc || r == kKeySpace) return stepResult(g, r);
    // Phase 2 (3 s): effect 12, title picture 0.
    g.sound.playEffect(g.GgState::sound.effectBank + kTitleEffect[0], 0, 4);
    attractDrawTitleArt(g.video.screen(), art, 2);
    g.display.flush();
    r = attractPlayAnim(g, a.title, 0, kAnimTimed | kAnimInput, kTitlePhaseSeconds[1]);
    if (r == kKeyEnter) return 0;
    if (r == kKeyEsc || r == kKeySpace) return stepResult(g, r);
    // Phase 3 (5 s): pictures 1..3, effect 13.
    attractDrawTitleArt(g.video.screen(), art, 3);
    g.sound.playEffect(g.GgState::sound.effectBank + kTitleEffect[1], 0, 4);
    g.display.flush();
    r = attractPlayAnim(g, a.title, 0, kAnimTimed | kAnimInput, kTitlePhaseSeconds[2]);
    return stepResult(g, r);
}

// 2c11:05cf Attract_AfterTitle
int16_t attractAfterTitle(GgGame& g) {
    attractAfterTitle(g.video, g.archive());  // Screen_RestoreAll, Pal_Save, Pal_LoadRes(0x1004)
    const int16_t r = stepResult(g, attractPlayAnim(g, attractAnims(g).afterTitle, kAnimFadeFromSaved, kAnimWatch | kAnimInput, 0));
    g.display.fadeOut();
    g.sound.stopAllEffects();
    g.flow.storyTextId = static_cast<int16_t>(kStoryFirstText);  // DS:a04e
    return r;
}

// 2c11:0630 Attract_StoryPage
int16_t attractStoryPage(GgGame& g) {
    const bool first = g.flow.storyTextId == static_cast<int16_t>(kStoryFirstText);
    g.display.runLoad(attractStoryPage(g.video, g.fonts, g.archive(), static_cast<uint16_t>(g.flow.storyTextId)));
    if (first) g.display.fadeOutIn();
    else g.display.flush();
    g.timers().set(1, static_cast<uint16_t>(kAttractPageTicks));
    int16_t result = 0;
    for (bool done = false; !done;) {
        const uint16_t c = g.rt.getInputCode();
        if (c == kKeyEnter || c == kKeySpace || c == kKeyEsc) {
            result = stepResult(g, c);
            done = true;
        }
        if (g.rt.timerExpired(1)) {
            result = 0;  // the time-out also overrides a key of the same poll (an Esc's g_leaveRequest stays)
            done = true;
        }
    }
    ++g.flow.storyTextId;
    return result;
}

// 2c11:077f Attract_AfterStory
int16_t attractAfterStory(GgGame& g) {
    attractAfterStory(g.video.screen());  // the story box and line restored from the back page
    // Fade 1 has no effect here (the screen is not faded).
    const int16_t r = stepResult(g, attractPlayAnim(g, attractAnims(g).afterStory, kAnimFadeIn, kAnimWatch | kAnimInput, 2));
    g.display.fadeOut();
    return r;
}

// 2c11:07e5 Attract_BeforePrompt
int16_t attractBeforePrompt(GgGame& g) {
    g.display.runLoad(attractBeforePrompt(g.video, g.archive()));
    const int16_t r = stepResult(g, attractPlayAnim(g, attractAnims(g).beforePrompt, kAnimFadeIn, kAnimWatch | kAnimInput, 0));
    g.display.fadeOut();
    return r;
}

// 2c11:084c Attract_SignInPrompt
int16_t attractSignInPrompt(GgGame& g) {
    g.display.runLoad(attractSignInPrompt(g.video, g.fonts, g.archive()));
    g.display.fadeOutIn();
    g.timers().set(1, static_cast<uint16_t>(kAttractPageTicks));
    int16_t result = 0;
    for (bool done = false; !done;) {
        const uint16_t c = g.rt.getInputCode();
        if (c == kKeyEnter || c == kKeySpace) {
            result = 0;  // -> step 12 (NULL): the sign-in
            done = true;
        } else if (c == kKeyEsc) {
            g.flow.leaveRequest = -1;
            result = -1;
            done = true;
        }
        if (g.rt.timerExpired(1)) {
            result = 1;  // restart at the title (also over a key of the same poll)
            done = true;
        }
    }
    g.display.fadeOut();
    return result;
}

}  // namespace tmport::gg
