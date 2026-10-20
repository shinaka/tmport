// The results, the post-race menu and the final movie (results.h; ovl10 2fcb). Owner: vehicles agent.
#include "gg/game/results.h"

#include <algorithm>
#include <array>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/data/resources.h"
#include "gg/game/flow.h"
#include "gg/game/game.h"
#include "gg/game/hud.h"
#include "gg/game/vehicles_common.h"
#include "gg/game/workshop.h"
#include "gg/gfx/movie.h"
#include "gg/ui/dialog.h"
#include "gg/ui/hints.h"
#include "gg/ui/players.h"
#include "gg/ui/rank.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

using vehicles::animFrame;
using vehicles::freeAnim;
using vehicles::snd;

constexpr uint16_t kResultPicture = 0x1076;
constexpr uint16_t kResultPalette = 0x1077;
constexpr uint16_t kCoverBundle = 0x1078;     // m0 the button cover, m1 rematch, m2..m4 the labels
constexpr uint16_t kResultEffects = 0x108F;
constexpr uint16_t kMusicWon = 0x1089;
constexpr uint16_t kMusicLost = 0x108B;
constexpr uint16_t kAnimA = 0x107D;           // the podium: started when W1 holds
constexpr uint16_t kAnimBFirst = 0x107F;      // + 2 level
constexpr uint16_t kAnimWonW1 = 0x1074, kAnimWonW2 = 0x1079;
constexpr uint16_t kAnimLostW1 = 0x107B, kAnimLostW2 = 0x1072;
constexpr uint16_t kRewards = 0x1071;         // I: 15 words, thousands of points by the races won in all
constexpr int kCountUpFrames = 50;
constexpr uint16_t kRankMessage = 0x1070;
constexpr uint16_t kLostHint = 0x104F;
constexpr uint16_t kMovieMusic = 0x30CA;
constexpr int kAllRaces = 15;
constexpr int kTopRank = 7;
constexpr size_t kMovieCueBytes = 64 * 8;     // room for the cue table DS:236c (26 records of 8 bytes)

// An 'S' member up to its NUL.
std::span<const uint8_t> stringOf(std::span<const uint8_t> p) {
    const auto end = std::find(p.begin(), p.end(), uint8_t{0});
    return p.first(static_cast<size_t>(end - p.begin()));
}

std::span<const uint8_t> nameBytes(const PlayerRecord& p) {
    return stringOf(std::span<const uint8_t>(p.name));
}

std::span<const uint8_t> asBytes(std::string_view s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

// The far string DS:22fa ("Morty") and the labels DS:2442 / DS:2448.
std::span<const uint8_t> dsString(const GgExe& exe, uint16_t off) { return asBytes(exe.dsString(off)); }

}  // namespace

// 2fcb:0000 Result_DrawScreen
void resultDrawScreen(GgGame& g) {
    const GgExe& exe = g.exe();
    VehiclesState& v = g.vehicles;
    Screen& screen = g.video.screen();
    bool final = false;
    v.resultLost = 0;
    v.resultNextRace = 0;
    std::vector<uint8_t> picture;
    g.display.runLoad(g.video.loadPicture(g.archive(), kResultPicture, kResultPalette, picture));  // Screen_LoadPicture
    screen.lpxDraw(0, 0, picture);
    const auto& covers = g.cache.loadBundle(kCoverBundle);
    auto cover = [&](size_t m) { return m < covers.size() ? covers[m] : std::span<const uint8_t>{}; };
    const auto morty = dsString(exe, exe.dsOffsetOf(exe.dsFarPtr(dg::kMortyNamePtr)));
    std::span<const uint8_t> first, second;
    if (v.raceResult == 0) {
        first = nameBytes(g.player);
        second = morty;
        const int b = g.building();
        if (b >= 0 && b < 3 && g.player.racesWon[static_cast<size_t>(b)] < 4) v.resultNextRace = 1;
        if (g.player.racesWonTotal == kAllRaces) {
            v.resultNextRace = 0;
            final = true;
            g.video.cursor().hide();  // Mouse_Hide: the movie follows
        }
    } else {
        first = morty;
        second = nameBytes(g.player);
        v.resultLost = 1;
    }
    if (v.resultNextRace == 0) {  // next race
        screen.lpxDrawKeyed(75, 73, cover(0), 0);
        screen.lpxDrawKeyed(79, 58, cover(2), 0);
    }
    if (v.resultLost == 0) {  // warehouse
        screen.lpxDrawKeyed(139, 73, cover(0), 0);
        screen.lpxDrawKeyed(143, 58, cover(3), 0);
    }
    if (final) {  // hub and rematch
        screen.lpxDrawKeyed(203, 73, cover(0), 0);
        screen.lpxDrawKeyed(207, 58, cover(4), 0);
        screen.lpxDrawKeyed(5, 82, cover(1), 0);
    }
    g.fonts.select(0);
    g.fonts.setColor(15);
    g.fonts.drawString(screen, 5, 32, dsString(exe, dg::kStrFirst));
    g.fonts.drawString(screen, 5, 41, dsString(exe, dg::kStrSecond));
    g.fonts.setColor(2);
    g.fonts.drawString(screen, 40, 32, first);
    g.fonts.setColor(4);
    g.fonts.drawString(screen, 40, 41, second);
    screen.savePlayArea();
    g.cache.releaseBundle(kCoverBundle);
}

std::vector<uint8_t> resultRankUpText(GgGame& g, uint32_t score) {
    const RankTable ranks = RankTable::fromExe(g.exe());
    const int r = ranks.rankOf(score);
    std::vector<uint8_t> text;
    if (r >= kTopRank) return text;
    const Bundle bundle = Bundle::load(g.archive(), kRankMessage);  // Res_Read(0x1070)
    const size_t k = r == kTopRank - 1 ? 3 : 0;
    auto add = [&](std::span<const uint8_t> s) { text.insert(text.end(), s.begin(), s.end()); };
    auto member = [&](size_t i) { return i < bundle.size() ? stringOf(bundle.member(i).payload) : std::span<const uint8_t>{}; };
    add(member(k));
    add(asBytes(ranks.names[static_cast<size_t>(r)]));
    add(member(k + 1));
    if (r != kTopRank - 1) add(asBytes(formatThousands(ranks.thresholds[static_cast<size_t>(r)])));
    add(member(k + 2));
    return text;
}

// 2fcb:01dd Result_RankUpMessage
void resultRankUpMessage(GgGame& g) {
    const std::vector<uint8_t> text = resultRankUpText(g, g.player.points);
    if (text.empty()) return;  // the top rank: no message
    dlgMessageBox(g, std::nullopt, text, g.strings.footerGoOn);
}

// 2fcb:0315 State_14_RedrawResult
int16_t state14RedrawResult(GgGame& g) {
    resultDrawScreen(g);
    g.display.fadeOutIn();
    return 13;
}

// 2fcb:0325 State_12_RaceResult
int16_t state12RaceResult(GgGame& g) {
    VehiclesObjects& o = g.vehiclesObjects();
    VehiclesState& v = g.vehicles;
    SoundState& s = snd(g);
    s.effectBank = static_cast<int16_t>(g.sound.loadEffectBank(kResultEffects));
    g.sound.setEffectBank(s.effectBank);
    resultDrawScreen(g);
    auto& [w1, w2, a, b] = o.resultAnims;
    a = ScreenAnim::load(g.cache, kAnimA);
    b = ScreenAnim::load(g.cache, static_cast<uint16_t>(kAnimBFirst + 2 * g.level()));
    const bool won = v.raceResult == 0;
    uint32_t step = 0;
    uint32_t framesLeft = 0;
    if (won) {
        w1 = ScreenAnim::load(g.cache, kAnimWonW1);
        w2 = ScreenAnim::load(g.cache, kAnimWonW2);
        // The reward of the n-th win of the game, in thousands, counted up over 50 frames (u32).
        const auto rewards = g.archive().read(kRewards);  // Res_Read(0x1071)
        const size_t i = static_cast<size_t>(std::clamp(g.player.racesWonTotal - 1, 0, kAllRaces - 1));
        const uint32_t thousands = 2 * i + 1 < rewards.size() ? static_cast<uint32_t>(rewards[2 * i] | (rewards[2 * i + 1] << 8)) : 0;
        step = thousands * 1000u / kCountUpFrames;
        framesLeft = kCountUpFrames;
    } else {
        w1 = ScreenAnim::load(g.cache, kAnimLostW1);
        w2 = ScreenAnim::load(g.cache, kAnimLostW2);
    }
    const int music = g.sound.loadMusicBank(won ? kMusicWon : kMusicLost);
    g.sound.playMusic(music, 0, 0);
    g.screenAnims.start(*w1);
    g.screenAnims.start(*w2);
    bool first = true, started = false, done = false;
    while (!done) {
        g.timers().set(0, 4);
        animFrame(g);
        if (framesLeft != 0) {  // one step and one click per frame
            --framesLeft;
            g.player.points += step;
            g.world.hud.dirty |= Hud::kScore;
            hudUpdate(g, 0xFFFB);
        }
        if (first) {
            g.display.fadeOutIn();
            first = false;
        } else {
            g.display.flush();
        }
        if (w1->state() != 0 && !started) {  // W1 holds: the podium
            started = true;
            g.screenAnims.startAt(*a, 0);
            g.screenAnims.startAt(*b, 1);
        }
        done = a->state() != 0;
        g.rt.timerWait(0);
    }
    while (g.sound.isPlaying(music)) g.rt.idle();  // the results music plays to its end
    for (auto& anim : o.resultAnims) freeAnim(g, anim);
    g.screenAnims.stopAll();
    g.sound.freeMusicBank(music);
    g.sound.freeEffectBank(s.effectBank);
    if (won) resultRankUpMessage(g);
    else hintShow(g, kLostHint);
    return g.player.racesWonTotal == kAllRaces ? int16_t{15} : int16_t{13};
}

// 2fcb:0573 State_13_PostRaceMenu
int16_t state13PostRaceMenu(GgGame& g) {
    VehiclesState& v = g.vehicles;
    const GgExe& exe = g.exe();
    std::array<Zone, 5> zones;  // DS:22fe: 2000 rematch .. 2004 Save & Exit (linked while the menu runs)
    for (size_t k = 0; k < zones.size(); ++k)
        zones[k] = Zone::fromRecord(exe.dsBytes(static_cast<uint16_t>(dg::kPostRaceZones + Zone::kRecordSize * k), Zone::kRecordSize));
    g.zones().addLocalArray(zones);
    const int b = g.building();
    auto inProgress = [&]() -> int16_t& { return g.player.buildingInProgress.at(static_cast<size_t>(std::clamp(b, 0, 2))); };
    bool done = false, leave = false;
    int16_t next = 3;
    while (!done) {
        switch (static_cast<uint16_t>(g.rt.waitInputCode() - 2000)) {
        case 0:  // rematch: the same vehicle list
            done = true;
            next = 11;
            v.rematch = 1;
            break;
        case 1:  // next race
            if (v.resultNextRace != 0) {
                next = 4;
                done = leave = true;
            }
            break;
        case 2:  // back to the workshop after a loss
            if (v.resultLost != 0) {
                next = 5;
                done = true;
            }
            break;
        case 3:  // the Technology Center
            next = 3;
            done = leave = true;
            break;
        case 4:  // Save & Exit (No or Esc: the menu goes on)
            inProgress() = v.raceResult != 0 ? 1 : 0;
            cmdSaveAndExit(g);
            break;
        default: break;
        }
    }
    if (leave) {
        if (v.raceResult == 0) {
            inProgress() = 0;
            playersSaveCurrent(g, GgSaveMoment::PostRaceMenu);
        }
        buildingFree(g);
    }
    g.zones().clearLocal();
    g.display.fadeOut();
    g.video.palette().restore();  // Pal_Restore
    return next;
}

// 2fcb:0640 State_15_FinalMovie
int16_t state15FinalMovie(GgGame& g) {
    SoundState& s = snd(g);
    s.movieMusicBank = static_cast<int16_t>(g.sound.loadMusicBank(kMovieMusic));
    g.sound.movieMusicStart(s.movieMusicBank);  // g_movieMusicStep = 1, the cue at 0x250, playing, arrangement 1
    const int effects = g.sound.loadEffectBank(kMovieEffectBank);
    // 1473:07d9 Movie_Play(0x3000, 0x30ca, 18, DS:236c, effects): straight into VRAM, no palette. The cue
    // table is read record by record up to its end record (25 cues and {6, 0, 0, 0} in v1.0).
    MoviePlayer movie(g.archive(), kMovieFirst, kMovieEnd, kMoviePeriod, g.exe().dsBytes(dg::kMovieCues, kMovieCueBytes), effects);
    while (!movie.finished()) {
        g.timers().set(0, static_cast<uint16_t>(movie.period()));
        const auto cue = movie.step(g.video.vram());
        g.display.present();
        if (cue) {
            switch (cue->type) {
            case 0: g.rt.delay(static_cast<uint16_t>(cue->a)); break;
            case 1: g.sound.playEffect(effects + cue->a, 0, cue->b); break;
            case 2: g.sound.stopEffect(effects + cue->a); break;
            case 5: g.sound.playEffect(effects + cue->a, 1, cue->b); break;
            default: break;  // 3, 4, 6: nothing
            }
        }
        g.rt.timerWait(0);
    }
    g.sound.freeEffectBank(effects);
    g.rt.waitInputCode();  // any key, click or menu request
    // farheapcheck() < 0 would exit(2): the engine's heap does not break.
    g.sound.freeMusicBank(s.movieMusicBank);
    const int b = g.building();
    if (b >= 0 && b < 3) g.player.buildingInProgress[static_cast<size_t>(b)] = 0;
    playersSaveCurrent(g, GgSaveMoment::FinalMovie);
    return 17;
}

}  // namespace tmport::gg
