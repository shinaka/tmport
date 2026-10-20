// PzBalance's run on the game (pz_balance.h, "wave 3: the run"; ovl14 32fe,
// re/gg/docs/code_puzzles_b.md §4). Owner: puzzles_b agent.
#include "gg/game/pz_balance.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/flow.h"
#include "gg/game/game.h"
#include "gg/game/pz_restore.h"
#include "gg/save/gg_state_file.h"
#include "gg/ui/hints.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kHintCode = 0x458, kInvCode = 0x459, kTypeFirst = 0x45A, kTypeLast = 0x45F, kHookFirst = 0x461,
                   kHookLast = 0x46F, kGoBack = 0x5DD, kPanel = 0x5DE;
constexpr int kHintX = 6, kHintY = 13;  // the pressed Hint picture; 35 of its 36 columns restored (keep)

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

}  // namespace

// 32fe:0aa0 PzBalance_Run
int PzBalance::run(GgGame& g, int variant) {
    game_ = &g;
    PuzzleUnwindGuard guard(g);
    BalanceState& s = *st_;
    s.held = -1;
    s.tilt = -5;
    s.zoneSet = -1;
    s.solved = 0;
    g.sound.pzLoadBank(kEffectBank);
    loadResources(variant);
    loadVariant(variant);
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    g.video.cursor().setCursorPicture(cursor_.get());
    invZone_ = zoneFromDs(exe_, dg::kBalanceInvZone);
    beamZone_ = zoneFromDs(exe_, dg::kBalanceBeamZone);  // press 0x454: no handler
    hintZone_ = zoneFromDs(exe_, dg::kBalanceHintZone);
    g.zones().addLocal(invZone_);
    g.zones().addLocal(beamZone_);
    g.zones().addLocal(hintZone_);
    initZones(g);
    puzzleLoadScreen(g, puzzleKindInfo(4).picture, puzzleKindInfo(4).palette);
    puzzleInitPanel(g, panelText_);
    g.video.screen().saveToBack(0, 0, 320, 200);
    drawInventory(g.video);
    drawBeam(g.video);
    g.display.fadeOutIn();
    hintShow(g, 0x1052);
    return finish(g);
}

int PzBalance::finish(GgGame& g) {
    PuzzleUnwindGuard guard(g);
    st_->solved = static_cast<int16_t>(loop(g) ? 1 : 0);
    g.display.fadeOut();
    puzzleFreePanel(g);
    g.zones().clearLocal();
    g.video.cursor().setCursorPicture(g.video.cursor().arrow());
    cursor_.reset();  // a weight still in the hand keeps its record (the original does not free it)
    g.sound.pzFreeBank();
    game_ = nullptr;
    return st_->solved;
}

// 32fe:0079 PzBalance_SelectHookZones
void PzBalance::selectHookZones(GgGame& g, int set, bool removeOld) {
    BalanceState& s = *st_;
    if (s.zoneSet == set) return;
    if (removeOld && s.zoneSet >= 0 && s.zoneSet < 3)
        for (Zone& z : hookZones_[static_cast<size_t>(s.zoneSet)]) g.zones().removeLocal(&z);
    if (set >= 0 && set < 3)
        for (Zone& z : hookZones_[static_cast<size_t>(set)]) g.zones().addLocal(z);
    s.zoneSet = static_cast<int16_t>(set);
}

// 32fe:00d4 PzBalance_InitZones
void PzBalance::initZones(GgGame& g) {
    for (int t = 0; t < kTypes; ++t) {
        const WeightType& w = types_[static_cast<size_t>(t)];
        Zone& z = typeZones_[static_cast<size_t>(t)];
        z.shape = 0;
        z.x = static_cast<int16_t>(w.zoneX);
        z.y = static_cast<int16_t>(w.zoneY);
        z.w = static_cast<int16_t>(w.zoneW);
        z.h = static_cast<int16_t>(w.zoneH);
        z.priority = 0x4B;
        z.pressCode = static_cast<uint16_t>(kTypeFirst + t);  // the release code (+0x12) is left as it was
        g.zones().addLocal(z);
    }
    // The left hooks' zones for the three resting tilts: set 0 tilt -5, 1 tilt +5, 2 level.
    constexpr int kTiltIndex[3] = {0, 10, 5};
    for (int set = 0; set < 3; ++set)
        for (int i = 0; i < 5; ++i) {
            const auto [hx, hy] = hookPos(kTiltIndex[set], i);
            Zone& z = hookZones_[static_cast<size_t>(set)][static_cast<size_t>(i)];
            z.shape = 0;
            z.x = static_cast<int16_t>(hx - 10);
            z.y = static_cast<int16_t>(hy - 10);
            z.w = 20;
            z.h = 0x43;
            z.priority = 0x4B;
            z.pressCode = static_cast<uint16_t>(kHookFirst + 5 * set + i);
        }
    selectHookZones(g, 0, false);
}

// 32fe:043b PzBalance_LeftTorque
int PzBalance::leftTorque() {
    int16_t sum = 0;  // 16-bit sums and products, as the original
    for (int h = 0; h < 5; ++h)
        for (int k = 0; k < kSlots; ++k) {
            const int w = st_->hookWeights[static_cast<size_t>(h)][static_cast<size_t>(k)];
            if (w == -1) break;
            const int mass = w >= 0 && w < kTypes ? types_[static_cast<size_t>(w)].mass : 0;
            sum = static_cast<int16_t>(sum + static_cast<int16_t>(mass * (5 - h)));
        }
    return sum;
}

// 32fe:0494 PzBalance_RightTorque
int PzBalance::rightTorque() {
    int16_t sum = 0;
    for (int h = 5; h < kHooks; ++h)
        for (int k = 0; k < kSlots; ++k) {
            const int w = st_->hookWeights[static_cast<size_t>(h)][static_cast<size_t>(k)];
            if (w == -1) break;
            const int mass = w >= 0 && w < kTypes ? types_[static_cast<size_t>(w)].mass : 0;
            sum = static_cast<int16_t>(sum + static_cast<int16_t>(mass * (h - 4)));
        }
    return sum;
}

// 32fe:04f0 PzBalance_Compare
int PzBalance::compare() {
    BalanceState& s = *st_;
    const int l = leftTorque(), r = rightTorque();
    if (s.tilt != 0) {
        if ((s.tilt < 0 && l < r) || (s.tilt > 0 && l > r)) return 0;  // already on the heavier side
        if (l == r) return -1;                                          // come to level
    } else {
        if (l == r) return 0;
        s.rightHeavy = l < r ? 1 : 0;  // set only as the beam leaves the level
    }
    // 100 * lighter / heavier + 1 with the 16-bit product (IMUL AX, 100; CWD; IDIV): 1..100.
    const int16_t lighter = static_cast<int16_t>(l < r ? l : r), heavier = static_cast<int16_t>(l < r ? r : l);
    const int16_t product = static_cast<int16_t>(lighter * 100);
    return static_cast<int16_t>(product / heavier + 1);
}

void PzBalance::swingEffect(GgGame& g, int step, int& n) {
    // A beam step's effect transposed by the step number: 8 rising (0, 1, 2 ..) when the step is
    // positive, 9 falling (0, -1, -2 ..) otherwise (the transposed swing effects: keep).
    if (step > 0) {
        g.sound.pzSetTranspose(8, n++);
        g.sound.pzPlay(8, 3);
    } else {
        g.sound.pzSetTranspose(9, n--);
        g.sound.pzPlay(9, 3);
    }
}

// 32fe:0566 PzBalance_SwingLevel
void PzBalance::swingLevel(GgGame& g) {
    BalanceState& s = *st_;
    const int step = s.tilt < 0 ? 1 : -1;
    int n = 0;
    for (int t = s.tilt; t != 0; t += step) {  // the first step redraws the starting tilt
        g.timers().set(0, 50);
        s.tilt = static_cast<int16_t>(t);
        drawBeam(g.video);
        swingEffect(g, step, n);
        g.rt.timerWait(0);
    }
    s.tilt = 0;
    drawBeam(g.video);
}

// 32fe:05eb PzBalance_Swing
void PzBalance::swing(GgGame& g, int ratio) {
    BalanceState& s = *st_;
    int step = 0, target = 0;
    if (s.tilt == 0) {
        step = s.rightHeavy ? -1 : 1;
        target = s.rightHeavy ? -5 : 5;
    } else {
        step = s.tilt < 0 ? 1 : -1;
        target = -s.tilt;  // the other end
    }
    // The nearer the sides are to balance, the slower the swing: ratio / 2 ticks per step (keep).
    const auto ticks = static_cast<uint16_t>(static_cast<int16_t>(ratio) / 2);
    int n = 0;
    int t = s.tilt;
    for (; t != target; t += step) {
        g.timers().set(0, ticks);
        s.tilt = static_cast<int16_t>(t);
        drawBeam(g.video);
        swingEffect(g, step, n);
        g.rt.timerWait(0);
    }
    s.tilt = static_cast<int16_t>(t);
    drawBeam(g.video);
    g.sound.pzPlay(0x0A, 4);
}

// 32fe:06b9 PzBalance_Settle
bool PzBalance::settle(GgGame& g) {
    const int r = compare();
    if (r == 0) return false;
    if (r == -1) {
        swingLevel(g);
        return true;
    }
    swing(g, r);
    return false;
}

// 32fe:06de PzBalance_HoldWeight
void PzBalance::holdWeight(GgGame& g, int type) {
    const auto pic = memberOf(carry_, type);
    held_ = CursorRecord::fromPicture(pic);  // Mouse_LoadCursors
    g.video.cursor().setHotSpot(held_.get(), s16(pic, 6) / 2, 0);  // the ring at the weight's top centre
    g.video.cursor().setCursorPicture(held_.get());
    g.sound.pzPlay(0, 3);
}

// 32fe:0750 PzBalance_DropCursor
void PzBalance::dropCursor(GgGame& g) {
    g.video.cursor().setCursorPicture(cursor_.get());
    held_.reset();  // Mouse_FreeCursor
}

// 32fe:0775 PzBalance_Hang
void PzBalance::hang(GgGame& g, int hook) {
    BalanceState& s = *st_;
    auto& slots = s.hookWeights[static_cast<size_t>(hook)];
    int k = 0;
    while (k < kSlots && slots[static_cast<size_t>(k)] != -1) ++k;
    if (k >= kSlots) return;  // a full hook: the weight stays in the hand
    slots[static_cast<size_t>(k)] = s.held;
    dropCursor(g);
    drawBeam(g.video);
    s.held = -1;
    g.sound.pzPlay(1, 3);
}

// 32fe:07c9 PzBalance_Unhang
void PzBalance::unhang(GgGame& g, int hook) {
    BalanceState& s = *st_;
    auto& slots = s.hookWeights[static_cast<size_t>(hook)];
    int k = 0;
    while (k < kSlots && slots[static_cast<size_t>(k)] != -1) ++k;
    if (k == 0) return;
    int16_t& last = slots[static_cast<size_t>(k - 1)];
    holdWeight(g, last);
    s.held = last;
    last = -1;
    drawBeam(g.video);
}

// 32fe:081a PzBalance_Loop
bool PzBalance::loop(GgGame& g) {
    game_ = &g;
    BalanceState& s = *st_;
    auto& p = g.puzzles;
    for (bool leave = false; !leave;) {
        const uint16_t c = puzzleGetCode(g);
        if (c >= kTypeFirst && c <= kTypeLast) {
            const int type = c - kTypeFirst;
            if (s.held == -1) {
                if (s.stock[static_cast<size_t>(type)] > 0) {
                    holdWeight(g, type);
                    --s.stock[static_cast<size_t>(type)];
                    drawInventory(g.video);
                    s.held = static_cast<int16_t>(type);
                }
                continue;
            }
            // Something carried: as the inventory (put back).
        }
        if ((c >= kTypeFirst && c <= kTypeLast) || c == kInvCode) {
            if (s.held != -1) {
                dropCursor(g);
                ++s.stock[static_cast<size_t>(s.held)];
                drawInventory(g.video);
                s.held = -1;
            }
            continue;
        }
        if (c >= kHookFirst && c <= kHookLast) {
            const int hook = (c - kHookFirst) % 5;  // three zone sets of five: 0x461.., 0x466.., 0x46b..
            if (s.held != -1) hang(g, hook);
            else unhang(g, hook);
            if (!s.solved) {
                s.solved = static_cast<int16_t>(settle(g) ? 1 : 0);
                if (s.solved == 1) {
                    puzzleShowSolved(g, 2);  // jingle 2..5 (rand)
                    puzzleAwardPoints(g, 0x0D);
                }
            } else {
                settle(g);  // the beam still moves; the puzzle stays solved
            }
            g.rt.flushInput();  // Input_Flush
            continue;
        }
        switch (c) {
        case code::kMenuRequest1:
            p.leave = 2;
            leave = true;
            break;
        case code::kMenuRequest2:
            p.leave = 1;
            leave = true;
            break;
        case kHintCode:
            g.video.screen().lpxDrawKeyed(kHintX, kHintY, hintPressed_, 0);
            g.display.flush();
            g.sound.pzPlay(0x0B, 5);
            puzzleWaitRelease(g);
            g.video.screen().restoreRect(kHintX, kHintY, 35, 15);  // 35 of 36 columns: the last stays (keep)
            g.display.flush();
            puzzleShowHintText(g, hintText_);
            break;
        case kGoBack:  // a carried weight is simply dropped with the screen
            g.sound.pzPlay(0x0C, 5);
            puzzlePressGoBack(g);
            p.goThrough = 0;
            leave = true;
            break;
        case code::kEnter:
        case kPanel:
            if (s.solved) {
                p.goThrough = 1;
                leave = true;
            }
            break;
        case code::kEsc: cmdSaveAndExit(g); break;
        default: break;
        }
    }
    return s.solved != 0;
}

// ---- starting from a DOSBox-X state (puzzles.h)

bool PzBalance::restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) {
    game_ = &g;
    bind(g.puzzles.balance);
    const BalanceState& b = *st_;
    if (b.held < -1 || b.held >= kTypes) {
        why = std::format("g_balanceHeld is {}", b.held);
        return false;
    }
    if (b.tilt < -5 || b.tilt > 5) {
        why = std::format("g_balanceTilt is {}", b.tilt);
        return false;
    }
    variant_ = g.puzzles.variant;
    loadResources(variant_);
    g.puzzlesObjects().panel.load(g.archive());
    g.sound.pzLoadBank(kEffectBank);  // the bank the state's RAM holds (DS:b1b0, the slots DS:b146)
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    invZone_ = zoneFromDs(exe_, dg::kBalanceInvZone);
    beamZone_ = zoneFromDs(exe_, dg::kBalanceBeamZone);
    hintZone_ = zoneFromDs(exe_, dg::kBalanceHintZone);
    std::vector<std::pair<uint16_t, Zone*>> zones = {
        {dg::kBalanceInvZone, &invZone_}, {dg::kBalanceBeamZone, &beamZone_}, {dg::kBalanceHintZone, &hintZone_}};
    for (int t = 0; t < kTypes; ++t) {
        const auto off = static_cast<uint16_t>(dg::kBalanceInvZones + Zone::kRecordSize * static_cast<size_t>(t));
        typeZones_[static_cast<size_t>(t)] = Zone::fromRecord(s.dgroup().subspan(off, Zone::kRecordSize));
        zones.emplace_back(off, &typeZones_[static_cast<size_t>(t)]);
    }
    for (int set = 0; set < 3; ++set)
        for (int i = 0; i < 5; ++i) {
            const auto off = static_cast<uint16_t>(dg::kBalanceHookZones + dg::kBalanceHookZonesStride * static_cast<size_t>(set) +
                                                   Zone::kRecordSize * static_cast<size_t>(i));
            Zone& z = hookZones_[static_cast<size_t>(set)][static_cast<size_t>(i)];
            z = Zone::fromRecord(s.dgroup().subspan(off, Zone::kRecordSize));
            zones.emplace_back(off, &z);
        }
    if (!relinkStateZones(g, s, zones, why)) return false;
    // The cursor: the hand DS:ab56 or the weight in the hand DS:ab52.
    Cursor& c = g.video.cursor();
    if (stateSamePointer(s, 0x0A6C, dg::kBalanceHeldCursor) && b.held != -1) {
        const auto pic = memberOf(carry_, b.held);
        held_ = CursorRecord::fromPicture(pic);
        held_->hotX = s16(pic, 6) / 2;
        held_->hotY = 0;
        restoreStateCursorRecord(g, s, held_.get());
    } else if (stateSamePointer(s, 0x0A6C, dg::kBalanceCursor)) {
        restoreStateCursorRecord(g, s, cursor_.get());
    } else if (stateSamePointer(s, 0x0A6C, dg::kCursorArrow)) {
        c.setCursorPicture(c.arrow());
    } else {
        why = "the cursor is neither the hand, the weight in the hand nor the arrow";
        return false;
    }
    return true;
}

int PzBalance::resume(GgGame& g) { return finish(g); }

}  // namespace tmport::gg
