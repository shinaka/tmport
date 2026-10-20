// PzLaunch's run on the game (pz_launch.h, "wave 3: the run"; ovl17 3797,
// re/gg/docs/code_puzzles_a.md §5.2-§5.6). Owner: puzzles_a agent.
#include "gg/game/pz_launch.h"

#include <algorithm>
#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/dam.h"
#include "gg/game/flow.h"
#include "gg/game/game.h"
#include "gg/save/gg_state_file.h"
#include "gg/ui/hints.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

constexpr uint16_t kHintCode = 0x44D, kObject0 = 0x44E, kObject3 = 0x44F, kObject2 = 0x450, kObject1 = 0x451,
                   kTool452 = 0x452, kTool453 = 0x453, kRampUp = 0x454, kRampDown = 0x455, kTool456 = 0x456,
                   kTool457 = 0x457, kGravitySlider = 0x458, kPlungerCode = 0x459, kGravityLow = 0x45A,
                   kGravityNormal = 0x45B, kGravityHigh = 0x45C, kGoBack = 0x5DD, kPanel = 0x5DE;

}  // namespace

// 3797:1138 PzLaunch_Run
int PzLaunch::run(GgGame& g, int variant) {
    game_ = &g;
    PuzzleUnwindGuard guard(g);  // the set-up yields too (the fades, the hint box)
    LaunchState& l = *st_;
    l.objectShown = 0;
    g.sound.pzLoadBank(kEffectBank);
    loadResources(variant);
    // 3797:040d PzLaunch_DrawScreen
    puzzleLoadScreen(g, puzzleKindInfo(5).picture, puzzleKindInfo(5).palette);
    puzzleInitPanel(g, panelText_);
    g.video.screen().saveToBack(0, 0, 320, 200);
    g.video.cursor().setCursorPicture(cursor_.get());
    g.display.fadeOutIn();
    addZones(g);
    // 3797:030c PzLaunch_LoadVariant: drawn after the fade-in
    loadVariant(variant);
    l.gravity = 0;
    setGravity(word(9));
    drawGravity(g.video);
    setEnergy(0);
    setTarget(word(10));
    l.glowOn = 1;
    glow_ = Bundle::load(archive_, static_cast<uint16_t>(kGlowFirst + word(10)));
    l.ramp = static_cast<int16_t>(word(0));
    setOffer(0, word(1));
    setOffer(1, word(2));
    setOffer(2, word(3));
    setOffer(3, word(4));
    setSurface(1);
    selectObject(-1);
    setOffer(4, word(7));
    setOffer(7, word(6));
    setOffer(6, word(5));
    setOffer(5, word(8));
    drawBox(g.video.screen());
    redraw(g);
    hintShow(g, 0x1052);
    g.timers().set(6, 5);
    return loop(g, false);
}

void PzLaunch::surfaceChange(GgGame& g, int effect, uint16_t script, uint16_t frames, int newSurface) {
    g.sound.pzPlay(effect, 3);
    damPlayModal(g, *this, script, frames, newSurface);
}

// 3797:117f..144e: the main loop and the exit.
int PzLaunch::loop(GgGame& g, bool solved) {
    game_ = &g;
    PuzzleUnwindGuard guard(g);
    auto& p = g.puzzles;
    LaunchState& l = *st_;
    auto chooseObject = [&](int o) {
        g.sound.pzPlay(0, 3);
        selectObject(o);
        redraw(g);
    };
    for (bool leave = false; !leave;) {
        const uint16_t code = getCode(g);
        switch (code) {
        case code::kMenuRequest1:
            p.leave = 2;
            leave = true;
            break;
        case code::kMenuRequest2:
            p.leave = 1;
            leave = true;
            break;
        case kHintCode:
            g.video.screen().lpxDrawKeyed(12, 13, hintPressed_, 0);
            g.display.flush();
            g.sound.pzPlay(0x0A, 5);
            puzzleWaitRelease(g);
            g.video.screen().restoreRect(12, 13, 36, 15);
            g.display.flush();
            puzzleShowHintText(g, hintText_);
            break;
        case kPlungerCode: {
            const int r = pullPlunger(g);
            if (!solved) solved = r != 0;
            if (solved && l.glowOn != 0) {
                l.glowOn = 0;
                redraw(g);
                puzzleShowSolved(g, 0x0B);  // jingle 0xb..0xe
                puzzleAwardPoints(g, 0x10);
            }
            break;
        }
        case kGravityLow:
            setGravity(2);
            drawGravity(g.video);
            break;
        case kGravityNormal:
            setGravity(1);
            drawGravity(g.video);
            break;
        case kGravityHigh:
            setGravity(0);
            drawGravity(g.video);
            break;
        case kGravitySlider: dragGravity(g); break;
        case kRampDown:
            stepRamp(-1);
            redraw(g);
            break;
        case kRampUp:
            stepRamp(1);
            redraw(g);
            break;
        // The surface tools: a transition plays only from the listed states (DAM 0x114b..0x1156).
        case kTool456:
            if (l.surface == 0) surfaceChange(g, 0x13, 0x1154, 0x1160, 1);
            break;
        case kTool457:
            if (l.surface == 2) surfaceChange(g, 0x14, 0x114E, 0x115A, 1);
            break;
        case kTool453:
            if (l.surface == 1) {
                surfaceChange(g, 0x16, 0x114B, 0x1157, 2);
            } else if (l.surface == 0) {
                surfaceChange(g, 0x13, 0x1154, 0x1160, 1);
                surfaceChange(g, 0x16, 0x114B, 0x1157, 2);
            }
            break;
        case kTool452:
            if (l.surface == 1) {
                surfaceChange(g, 0x15, 0x1151, 0x115D, 0);
            } else if (l.surface == 2) {
                surfaceChange(g, 0x14, 0x114E, 0x115A, 1);
                surfaceChange(g, 0x15, 0x1151, 0x115D, 0);
            }
            break;
        case kObject0: chooseObject(0); break;
        case kObject3: chooseObject(3); break;
        case kObject1: chooseObject(1); break;
        case kObject2: chooseObject(2); break;
        case kGoBack:
            g.sound.pzPlay(0x0F, 5);
            puzzlePressGoBack(g);
            p.goThrough = 0;
            leave = true;
            break;
        case code::kEnter:
        case kPanel:
            if (solved) {
                p.goThrough = 1;
                leave = true;
            }
            break;
        case code::kEsc: cmdSaveAndExit(g); break;
        default: break;
        }
    }
    g.display.fadeOut();
    puzzleFreePanel(g);
    cleanup(g);
    g.sound.pzFreeBank();
    game_ = nullptr;
    return solved ? 1 : 0;
}

// 3797:0000 PzLaunch_DrawObject
void PzLaunch::drawObject(GgGame& g) {
    const LaunchState& l = *st_;
    if (l.objectShown == 0) return;
    Screen& screen = g.video.screen();
    const ClipRect saved = screen.clip();
    screen.setClip({l.clipLeft, l.clipRight, l.clipTop, l.clipBottom});
    screen.lpxDrawKeyed(l.objectX, l.objectY, objectPic_, 0);
    screen.setClip(saved);
}

// 3797:0070 PzLaunch_DrawRamp
void PzLaunch::drawRamp(GgGame& g) {
    const LaunchState& l = *st_;
    g.video.screen().lpxDrawKeyed(dsS16(dg::kLaunchRampX, l.ramp), dsS16(dg::kLaunchRampY, l.ramp), memberOf(rampSet(), l.ramp), 0);
}

// 3797:009f PzLaunch_DrawPlunger
void PzLaunch::drawPlunger(GgGame& g) {
    const LaunchState& l = *st_;
    Screen& screen = g.video.screen();
    screen.lpxDrawKeyed(0xFA, 0x7A, memberOf(plunger_, l.energy), 0);
    const auto digit = memberOf(digits_, l.energy);
    screen.lpxDrawKeyed((7 - s16(digit, 6)) / 2 + 0x11B, 0x82, digit, 0);
}

// 3797:0103 PzLaunch_Animate
void PzLaunch::animate(GgGame& g, bool force, bool sprites) {
    const bool expired = g.rt.timerExpired(6);
    if (!expired && !force) return;
    LaunchState& l = *st_;
    Screen& screen = g.video.screen();
    if (l.glowOn != 0)
        screen.lpxDrawKeyed(dsS16(dg::kLaunchHoleX, l.target), dsS16(dg::kLaunchHoleY, l.target), memberOf(glow_, l.glowFrame), 0);
    drawRamp(g);
    if (sprites) g.puzzlesObjects().dam.drawSprites(screen, 0);  // Anim_DrawSprites(0)
    drawObject(g);
    drawPlunger(g);
    if (!force) {
        l.glowFrame = static_cast<int16_t>(l.glowFrame + 1 > 7 ? 0 : l.glowFrame + 1);
        g.display.flush();
        g.timers().set(6, 5);  // a 6-tick period: Timer_Expired is strict
    }
}

// 3797:0ae9 PzLaunch_Redraw
void PzLaunch::redraw(GgGame& g) {
    g.video.screen().restoreRect(0x40, 0x11, 0xFB, 0x8B);  // the ramp set follows the surface (rampSet)
    drawRamp(g);
    drawObject(g);
    drawPlunger(g);
    animate(g, true, false);
    g.display.flush();
}

// 3797:018e PzLaunch_GetCode
uint16_t PzLaunch::getCode(GgGame& g) {
    animate(g, false, false);
    return puzzleGetCode(g);
}

// 3797:01a5 PzLaunch_WaitTimer
void PzLaunch::waitTimer(GgGame& g, int slot) {
    do {
        animate(g, false, false);
    } while (!g.rt.timerExpired(slot));
}

// 3797:01c3 PzLaunch_WaitTimerAnim
void PzLaunch::waitTimerAnim(GgGame& g, int slot) {
    do {
        animate(g, false, true);
    } while (!g.rt.timerExpired(slot));
}

// 3797:01e1 PzLaunch_SetObjectSprite
void PzLaunch::setObjectSprite(GgGame& g, int x, int y, int frame) {
    LaunchState& l = *st_;
    l.objectX = static_cast<int16_t>(x);
    l.objectY = static_cast<int16_t>(y);
    objectPic_ = memberOf(objectFrames(), frame);
    const ClipRect& clip = g.video.screen().clip();  // the clip at the time of the call
    l.clipLeft = static_cast<int16_t>(clip.left);
    l.clipRight = static_cast<int16_t>(clip.right);
    l.clipTop = static_cast<int16_t>(clip.top);
    l.clipBottom = static_cast<int16_t>(clip.bottom);
}

// 3797:023c PzLaunch_IsSolved
bool PzLaunch::isSolved() {
    const LaunchState& l = *st_;
    if (word(12) != 4 && word(12) != l.surface) return false;
    if (word(13) != 5 && word(13) != l.object) return false;
    if (word(14) != 4) {
        if (l.gravity == -1) {
            if (word(14) != 1) return false;  // with gravity locked only "normal" passes
        } else if (word(14) != l.gravity) {
            return false;
        }
    }
    return (word(10) == -1 || word(10) == l.landing - 4) && (word(11) == 6 || word(11) == l.ramp);
}

// 3797:02c3 PzLaunch_AddZones
void PzLaunch::addZones(GgGame& g) {
    // DS:2b02 Hint, 2bde ramp - 1, 2bc8 ramp + 1, 2bf4 the slider, 2c0a / 2c20 / 2c36 the labels, 2c4c the plunger.
    for (const int i : {0, 10, 9, 11, 12, 13, 14, 15}) {
        Zone& z = zones_[static_cast<size_t>(i)];
        z = zoneFromDs(exe_, static_cast<uint16_t>(dg::kLaunchZones + 0x16 * i));
        g.zones().addLocal(z);
    }
}

// 3797:0725 PzLaunch_Cleanup
void PzLaunch::cleanup(GgGame& g) {
    g.zones().clearLocal();
    g.video.cursor().clearRegions();
    g.video.cursor().setCursorPicture(g.video.cursor().arrow());
}

// 3797:0870 PzLaunch_SetEnergy
void PzLaunch::setEnergy(int e) { st_->energy = static_cast<int16_t>(e); }

// 3797:08e2 PzLaunch_StepRamp
void PzLaunch::stepRamp(int delta) {
    LaunchState& l = *st_;
    const int fixed = word(0);
    if (fixed >= 3 && fixed <= 5) {
        l.ramp = static_cast<int16_t>(fixed);
        return;
    }
    l.ramp = static_cast<int16_t>(l.ramp + delta);
    if (l.ramp < 0) l.ramp = 2;
    else if (l.ramp > 2) l.ramp = 0;
}

// 3797:0942 PzLaunch_SetOfferObject0 (and the seven other setters, see the header)
void PzLaunch::setOffer(int which, int on) {
    LaunchState& l = *st_;
    const auto v = static_cast<int16_t>(on);
    switch (which) {
    case 0: l.offerTool452 = v; break;   // 3797:099d DS:b116
    case 1: l.offerTool453 = v; break;   // 3797:09aa DS:b114
    case 2: l.offerTool456 = v; break;   // 3797:094f DS:b10a
    case 3: l.offerTool457 = v; break;   // 3797:095c DS:b108
    case 4: l.offerObject0 = v; break;   // 3797:0942 DS:b112
    case 5: l.offerObject1 = v; break;   // 3797:0990 DS:b10c
    case 6: l.offerObject2 = v; break;   // 3797:0983 DS:b10e
    case 7: l.offerObject3 = v; break;   // 3797:0976 DS:b110
    default: break;
    }
}

// 3797:0969 PzLaunch_SetSurface
void PzLaunch::setSurface(int s) { st_->surface = static_cast<int16_t>(s); }

// 3797:0b4b PzLaunch_SetTarget
void PzLaunch::setTarget(int hole) { st_->target = static_cast<int16_t>(hole); }

// 3797:0b58 PzLaunch_ComputeLanding
void PzLaunch::computeLanding() {
    LaunchState& l = *st_;
    // Object -1 reads the word before the table (DS:2fe4); object 3 adds 2 instead of the surface's term.
    int i = exe_.dsS16(static_cast<uint16_t>(dg::kLaunchObjectTerm + 2 * l.object));
    i += l.object == 3 ? 2 : exe_.dsS16(static_cast<uint16_t>(dg::kLaunchSurfaceTerm + 2 * l.surface));
    i += l.gravity == -1 ? exe_.dsS16(0x2FF6) : exe_.dsS16(static_cast<uint16_t>(dg::kLaunchGravityTerm + 2 * l.gravity));
    if (l.ramp == 3) i += exe_.dsS16(0x2FFE);
    else if (l.ramp == 4) i += exe_.dsS16(0x2FFC);
    else if (l.ramp == 5) i += exe_.dsS16(dg::kLaunchRampTerm);
    else i += exe_.dsS16(static_cast<uint16_t>(dg::kLaunchRampTerm + 2 * l.ramp));
    l.landing = exe_.dsS16(static_cast<uint16_t>(dg::kLaunchLandingTable + 2 * (i + l.energy - 1)));
}

// 3797:0c33 PzLaunch_Fly
void PzLaunch::fly(GgGame& g) {
    LaunchState& l = *st_;
    // Ramp class: 2 / 3 A, 1 / 4 B, 0 / 5 C (the climb y tables and the flight y arrays).
    const int cls = (l.ramp == 2 || l.ramp == 3) ? 0 : (l.ramp == 1 || l.ramp == 4) ? 1 : 2;
    const uint16_t climbY = cls == 0 ? dg::kLaunchClimbYA : cls == 1 ? dg::kLaunchClimbYB : dg::kLaunchClimbYC;
    int landEffect = 0;
    switch (l.object) {
    case 0: landEffect = 0x12; break;
    case 1: landEffect = 0x11; break;
    case 3: landEffect = 3; break;
    case 2: landEffect = 2; break;
    default: break;
    }
    if (l.landing < 4) {  // falls short: up the ramp and back
        g.sound.pzStop(7);
        g.sound.pzPlay(9, 3);
        for (int i = 0; i <= l.landing; ++i) {
            g.timers().set(7, static_cast<uint16_t>(4 * i + 10));
            setObjectSprite(g, dsS16(dg::kLaunchClimbX, i), dsS16(climbY, i), 0);
            redraw(g);
            waitTimer(g, 7);
        }
        for (int i = l.landing; i >= 0; --i) {
            g.timers().set(7, static_cast<uint16_t>(4 * i + 10));
            setObjectSprite(g, dsS16(dg::kLaunchClimbX, i), dsS16(climbY, i), 0);
            redraw(g);
            waitTimer(g, 7);
        }
        putObjectOnLauncher();
        redraw(g);
        return;
    }
    const int n = l.landing - 4;  // the hole (or 8: over)
    const uint16_t pathX = exe_.dsOffsetOf(exe_.dsFarPtr(static_cast<uint16_t>(dg::kLaunchPathX + 4 * n)));
    const uint16_t yTable = cls == 0 ? dg::kLaunchPathYA : cls == 1 ? dg::kLaunchPathYB : dg::kLaunchPathYC;
    const uint16_t pathY = exe_.dsOffsetOf(exe_.dsFarPtr(static_cast<uint16_t>(yTable + 4 * n)));
    g.sound.pzPlay(l.surface == 0 ? 5 : l.surface == 2 ? 6 : 4, 3);  // the climb
    for (int i = 1; i < 4; i += 2) {  // always the class A climb
        g.timers().set(7, 10);
        setObjectSprite(g, dsS16(dg::kLaunchClimbX, i), dsS16(dg::kLaunchClimbYA, i), 0);
        redraw(g);
        waitTimer(g, 7);
    }
    g.sound.pzPlay(7, 3);  // the flight
    Screen& screen = g.video.screen();
    for (int j = 0; j <= n; ++j) {
        g.timers().set(7, 15);
        ClipRect clip = screen.clip();
        const int top = clip.top;
        if (j == n) {  // the last step: the object sinks into the hole
            clip.left = dsS16(dg::kLaunchHoleClipLeft, n);
            clip.bottom = dsS16(dg::kLaunchHoleClipBottom, n);
        }
        clip.top = 0x11;
        screen.setClip(clip);
        setObjectSprite(g, exe_.dsS16(static_cast<uint16_t>(pathX + 2 * j)), exe_.dsS16(static_cast<uint16_t>(pathY + 2 * j)),
                        dsS16(dg::kLaunchFlightFrames, j));
        clip.left = 0;  // back to left 0, bottom 199 (not the old values), top as it was
        clip.bottom = 199;
        clip.top = top;
        screen.setClip(clip);
        redraw(g);
        waitTimer(g, 7);
    }
    g.sound.pzStop(7);
    if (l.landing > 11) landEffect = 9;  // over every hole: a miss
    g.sound.pzPlay(landEffect, 4);
}

// 3797:0f33 PzLaunch_PullPlunger
int PzLaunch::pullPlunger(GgGame& g) {
    LaunchState& l = *st_;
    const int x0 = g.mouse().x();
    putObjectOnLauncher();
    redraw(g);
    g.sound.pzPlay(1, 3);
    int lastX = x0;
    while (g.mouse().buttons() != 0) {
        animate(g, false, false);
        const int x = g.mouse().x();
        if (x != lastX && x >= x0) {  // left of the start keeps the last energy
            l.energy = static_cast<int16_t>(std::clamp((x - x0) / 3, 0, 7));
            redraw(g);
            lastX = x;
        }
    }
    g.sound.pzStop(1);
    if (l.energy == 0) return 0;
    computeLanding();
    l.energy = 0;
    redraw(g);
    if (l.object != -1) fly(g);
    return isSolved() ? 1 : 0;
}

// 3797:0fca PzLaunch_DragGravity
void PzLaunch::dragGravity(GgGame& g) {
    int lastY = g.mouse().y();
    while (g.mouse().buttons() != 0) {
        animate(g, false, false);
        const int y = g.mouse().y();
        if (y == lastY) continue;
        setGravity(y < 0x92 ? 2 : y < 0x9D ? 1 : 0);
        drawGravity(g.video);
        lastY = y;
    }
}

// ---- starting from a DOSBox-X state (puzzles.h)

bool PzLaunch::restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) {
    // 3797:117f the main loop's PzLaunch_GetCode; the plunger (120e, 1215), the gravity slider (1269), the
    // DAM transitions (12a6 .. 139e), the success (1234, 123c), the hint text (1202), Go Back (140f) and
    // Save & Exit (142b).
    static constexpr uint16_t kMain[] = {0x1182};
    static constexpr uint16_t kSub[] = {0x1211, 0x1218, 0x126C, 0x12A9, 0x12D1, 0x12F6, 0x131E, 0x1339,
                                        0x135E, 0x1386, 0x13A1, 0x1239, 0x1241, 0x1207, 0x1414, 0x1430};
    if (!stateAtMainLoop(s, {kMain, kSub, 0x117F, 0x1436}, why)) return false;
    game_ = &g;
    bind(g.puzzles.launch);
    const int variant = g.puzzles.variant;
    loadResources(variant);
    loadVariant(variant);
    g.puzzlesObjects().panel.load(g.archive());
    // The puzzle's effect bank, as the state's RAM holds it (DS:b1b0, the slots DS:b146): without it the
    // slots keep their start-up handles and PzSnd_WaitEnd after a solve waits on the room's music.
    g.sound.pzLoadBank(kEffectBank);
    LaunchState& l = *st_;
    glow_ = Bundle::load(archive_, static_cast<uint16_t>(kGlowFirst + l.target));
    // The selected object's frame set DS:b120 (a DGROUP table of far pointers per object) and the
    // sprite's picture DS:af8c (one of its frames).
    static constexpr std::array<uint16_t, 4> kFrameTables = {0xB0A4, 0xB044, 0xB064, 0xB084};  // objects 0..3
    const uint16_t frames = s.dsWord(dg::kLaunchObjectFrames);
    for (int o = 0; o < 4; ++o)
        if (frames == kFrameTables[static_cast<size_t>(o)]) objectSet_ = o;
    objectPic_ = {};
    if (l.objectShown != 0) {
        const uint32_t pic = s.dsDword(0xAF8C);
        for (int f = 0; f < 8; ++f)
            if (pic != 0 && pic == s.dsDword(static_cast<uint16_t>(kFrameTables[static_cast<size_t>(objectSet_)] + 4 * f)))
                objectPic_ = memberOf(objectFrames(), f);
        if (objectPic_.empty()) {
            why = "the object sprite's picture is not a frame of the selected object";
            return false;
        }
    }
    std::vector<std::pair<uint16_t, Zone*>> zones;
    for (int i = 0; i < 16; ++i) {
        const auto off = static_cast<uint16_t>(dg::kLaunchZones + 0x16 * i);
        zones_[static_cast<size_t>(i)] = zoneFromDs(exe_, off);
        zones.emplace_back(off, &zones_[static_cast<size_t>(i)]);
    }
    if (!relinkStateZones(g, s, zones, why)) return false;
    if (!stateCursorRegions(s).empty() && stateCursorRegions(s).front() != 0x0A54) {
        why = "a cursor region the puzzle does not have";
        return false;
    }
    g.video.cursor().clearRegions();
    if (!stateSamePointer(s, 0x0A6C, 0xB104)) {  // the hand DS:b104
        why = "the cursor is not the puzzle's hand";
        return false;
    }
    restoreCursorPicture(g, s, cursor_.get());
    return true;
}

int PzLaunch::resume(GgGame& g) {
    bool solved = false;
    for (const Zone* z = g.zones().local(); z; z = z->next)
        if (z == &g.puzzlesObjects().panelZone) solved = true;
    return loop(g, solved);
}

}  // namespace tmport::gg
