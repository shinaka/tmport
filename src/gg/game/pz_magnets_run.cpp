// PzMagnets's run on the game (pz_magnets.h, "wave 3: the run"; ovl16 35bb,
// re/gg/docs/code_puzzles_b.md §5). Owner: puzzles_b agent.
#include "gg/game/pz_magnets.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/flow.h"
#include "gg/game/game.h"
#include "gg/game/pointer_warp.h"
#include "gg/game/pz_restore.h"
#include "gg/save/gg_state_file.h"
#include "gg/ui/hints.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kTableCode = 0x456, kHintCode = 0x458, kInvCode = 0x459, kRotateCode = 0x45A, kGoBack = 0x5DD,
                   kPanel = 0x5DE;
constexpr int kHintX = 13, kHintY = 13;      // the pressed Hint picture, restored with all 36 columns
constexpr int kRotateX = 3, kRotateY = 128;  // the pressed ROTATE picture (61 x 25)

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

// The CWD / XOR / SUB absolute value of a 16-bit word (-32768 stays -32768).
int16_t abs16(int v) {
    const auto x = static_cast<int16_t>(v);
    return static_cast<int16_t>(x < 0 ? -x : x);
}

}  // namespace

// 35bb:1c03 PzMagnets_Run
int PzMagnets::run(GgGame& g, int variant) {
    game_ = &g;
    PuzzleUnwindGuard guard(g);
    PointerWarpHook warp(g);  // the carried magnet is moved with the pointer (35bb:0103)
    loadTargets(variant);  // level = DS:2ae4[v] - 1 selects the inventory, the targets and the hint text
    g.sound.pzLoadBank(kEffectBank);
    init();
    hintText_ = puzzleText(archive_, static_cast<uint16_t>(kHintTextFirst + st_->level));
    panelText_ = puzzleText(archive_, kPanelText);
    loadPictures();
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    g.video.cursor().setCursorPicture(cursor_.get());
    invZone_ = zoneFromDs(exe_, dg::kMagnetsInvZone);
    tableZone_ = zoneFromDs(exe_, dg::kMagnetsTableZone);
    hintZone_ = zoneFromDs(exe_, dg::kMagnetsHintZone);
    rotateZone_ = zoneFromDs(exe_, dg::kMagnetsRotateZone);
    g.zones().addLocal(invZone_);
    g.zones().addLocal(tableZone_);
    g.zones().addLocal(hintZone_);
    g.zones().addLocal(rotateZone_);
    puzzleLoadScreen(g, puzzleKindInfo(7).picture, puzzleKindInfo(7).palette);
    puzzleInitPanel(g, panelText_);
    drawTargets(g.video.screen());
    g.video.screen().saveToBack(0, 0, 320, 200);  // the outlines become part of the background
    drawInventory(g.video);
    g.display.fadeOutIn();
    hintShow(g, 0x1052);
    return finish(g, loop(g, st_->level));
}

int PzMagnets::finish(GgGame& g, bool solved) {
    g.display.fadeOut();
    g.zones().clearLocal();
    puzzleFreePanel(g);
    g.video.cursor().setCursorPicture(g.video.cursor().arrow());
    cursor_.reset();
    freeResources(g);
    g.sound.pzFreeBank();
    game_ = nullptr;
    return solved ? 1 : 0;
}

// 35bb:0103 PzMagnets_WarpMouse
void PzMagnets::warpMouse(GgGame& g, int x, int y) {
    // g_mouseX / g_mouseY and INT 33h AX=04h (CX = 2x); the host pointer follows through onWarp.
    g.mouse().warp(static_cast<int16_t>(x), static_cast<int16_t>(y));
}

// 35bb:0236 PzMagnets_AllDrawn
bool PzMagnets::allDrawn() {
    for (const MagnetRec& m : st_->slots)
        if (m.kind != -1 && m.drawn == 0) return false;
    return true;
}

// 35bb:0401 PzMagnets_CanDraw
bool PzMagnets::canDraw(int i) {
    for (int j = 0; j < kSlots; ++j) {
        const MagnetRec& o = st_->slots[static_cast<size_t>(j)];
        if (o.kind == -1 || o.drawn != 0 || j == i) continue;
        if (covers(j, i)) return false;
    }
    return true;
}

// 35bb:05be PzMagnets_ISqrt
int PzMagnets::isqrt(uint32_t n) {
    // 1 for n < 4 (a distance of 0 gives 1: no division by zero); otherwise a binary search between 2
    // and n / 2 from n / 4 + 1, stopping when the LOW WORDS of the bounds differ by at most 1 (exact
    // floor(sqrt(n)) for 4 <= n < 131074, which covers every distance on the table).
    if (n < 4) return 1;
    uint32_t guess = (n >> 2) + 1, high = n >> 1, low = 2;
    for (;;) {
        const auto diff = static_cast<int16_t>(static_cast<uint16_t>(high) - static_cast<uint16_t>(low));
        if (abs16(diff) <= 1) break;
        const uint32_t sq = guess * guess;  // F_LXMUL, 32 bits
        if (sq > n) {
            high = guess;
        } else if (sq < n) {
            low = guess;
        } else {
            low = guess;
            break;
        }
        guess = low + ((high - low) >> 1);
    }
    return static_cast<int16_t>(low);
}

// 35bb:06aa PzMagnets_Cube
int16_t PzMagnets::cube(int16_t x) {
    return static_cast<int16_t>(static_cast<int16_t>(x * x) * x);  // two 16-bit IMULs
}

// 35bb:06ba PzMagnets_PoleForce
void PzMagnets::poleForce(bool north, int px, int py, int i, int j) {
    MagnetRec& mi = st_->slots[static_cast<size_t>(i)];
    const MagnetRec& mj = st_->slots[static_cast<size_t>(j)];
    const int pic = mj.kind * 4 + mj.orientation;
    int16_t sx = 0, sy = 0;
    // Every one of j's 6 poles, the absent ones included at (x_j - 1, y_j - 1) (the phantom poles: keep).
    for (int q = 0; q < 6; ++q) {
        const bool qNorth = q < 3;
        const auto [qdx, qdy] = pole(pic, q);
        int16_t dx = 0, dy = 0;
        if (qNorth != north) {  // attract toward Q
            dx = static_cast<int16_t>(mj.x + qdx - px);
            dy = static_cast<int16_t>(mj.y + qdy - py);
        } else {  // repel away from Q
            dx = static_cast<int16_t>(px - mj.x - qdx);
            dy = static_cast<int16_t>(py - mj.y - qdy);
        }
        const auto n2 = static_cast<uint32_t>(static_cast<int32_t>(dx) * dx + static_cast<int32_t>(dy) * dy);
        const auto dist = static_cast<int16_t>(isqrt(n2));
        if ((abs16(dx) == 1 && dy == 0) || (dx == 0 && abs16(dy) == 1)) {
            if (qNorth != north) {
                // Unlike poles 1 px apart: i sticks; its accumulators are zeroed and sx / sy dropped.
                mi.attached = 1;
                mi.fx = 0;
                mi.fy = 0;
                return;
            }
            // Like poles 1 px apart fall through to the force.
        }
        if (dist < 18) {
            const int16_t k = cube(static_cast<int16_t>(18 - dist));
            // 16-bit products (IMUL, then CWD drops the high word), signed division truncated.
            sx = static_cast<int16_t>(sx + static_cast<int16_t>(k * dx) / dist);
            sy = static_cast<int16_t>(sy + static_cast<int16_t>(k * dy) / dist);
        }
    }
    mi.fx = static_cast<int16_t>(mi.fx + sx);  // i16 wrapping accumulators (keep)
    mi.fy = static_cast<int16_t>(mi.fy + sy);
}

// 35bb:08ae PzMagnets_Forces
void PzMagnets::forces(int i) {
    MagnetRec& mi = st_->slots[static_cast<size_t>(i)];
    const int pic = mi.kind * 4 + mi.orientation;
    for (int p = 0; p < 6; ++p) {
        const auto [pdx, pdy] = pole(pic, p);
        if (pdx == -1) continue;  // an absent pole of i exerts no force on it
        for (int j = 0; j < kSlots; ++j) {
            if (mi.attached != 0) break;  // nothing more is added for an attached magnet
            const MagnetRec& mj = st_->slots[static_cast<size_t>(j)];
            if (mj.kind == -1 || j == i) continue;
            poleForce(p < 3, mi.x + pdx, mi.y + pdy, i, j);
        }
    }
}

// 35bb:09a5 PzMagnets_Blocked
bool PzMagnets::blocked(int i) {
    const MagnetRec& m = st_->slots[static_cast<size_t>(i)];
    const auto [w, h] = magnetSize(i);
    if (m.x < kTableX || m.y < kTableY || m.x + w + 2 > kTableRight || m.y + h + 2 > kTableBottom) return true;
    for (int j = 0; j < kSlots; ++j) {
        const MagnetRec& o = st_->slots[static_cast<size_t>(j)];
        if (o.kind == -1 || j == i) continue;
        const auto [ow, oh] = magnetSize(j);
        // 35bb:0271 PzMagnets_Overlap(j, i, 1): the two shrunk rectangles.
        if (spansOverlap(o.x, o.y, ow - 2, oh - 1, m.x, m.y, w - 2, h - 1)) return true;
    }
    return false;
}

// 35bb:0aa4 PzMagnets_TouchesOpposite
bool PzMagnets::touchesOpposite(int i) {
    const MagnetRec& mi = st_->slots[static_cast<size_t>(i)];
    const int pic = mi.kind * 4 + mi.orientation;
    for (int p = 0; p < 6; ++p) {
        const auto [pdx, pdy] = pole(pic, p);  // absent poles of i included
        const int px = mi.x + pdx, py = mi.y + pdy;
        for (int c = 0; c < kSlots; ++c) {
            const MagnetRec& mc = st_->slots[static_cast<size_t>(c)];
            if (mc.kind == -1 || c == i) continue;
            const int qpic = mc.kind * 4 + mc.orientation;
            for (int q = p < 3 ? 3 : 0, end = p < 3 ? 6 : 3; q < end; ++q) {
                const auto [qdx, qdy] = pole(qpic, q);
                if (qdx == -1) continue;  // an existing pole of the other class
                const int qx = mc.x + qdx, qy = mc.y + qdy;
                if ((abs16(qx - px) == 1 && qy == py) || (abs16(qy - py) == 1 && qx == px)) return true;
            }
        }
    }
    return false;
}

// 35bb:0c78 PzMagnets_TouchesSame
bool PzMagnets::touchesSame(int i, bool vertical) {
    MagnetRec& mi = st_->slots[static_cast<size_t>(i)];
    const int pic = mi.kind * 4 + mi.orientation;
    for (int p = 0; p < 6; ++p) {
        const auto [pdx, pdy] = pole(pic, p);  // absent poles of i included
        const int px = mi.x + pdx, py = mi.y + pdy;
        for (int c = 0; c < kSlots; ++c) {
            const MagnetRec& mc = st_->slots[static_cast<size_t>(c)];
            if (mc.kind == -1 || c == i) continue;
            const int qpic = mc.kind * 4 + mc.orientation;
            for (int q = p < 3 ? 0 : 3, end = p < 3 ? 3 : 6; q < end; ++q) {
                const auto [qdx, qdy] = pole(qpic, q);
                if (qdx == -1) continue;  // an existing pole of the same class
                const int qx = mc.x + qdx, qy = mc.y + qdy;
                if (!((abs16(qx - px) == 1 && qy == py) || (abs16(qy - py) == 1 && qx == px))) continue;
                // Like poles in contact push the magnet along the other axis in the next pass, toward a
                // free side: one pixel tried, the sign of the nudge set, the pixel undone.
                if (vertical) {
                    ++mi.y;
                    mi.fy = static_cast<int16_t>(blocked(i) ? -1 : 1);
                    --mi.y;
                } else {
                    ++mi.x;
                    mi.fx = static_cast<int16_t>(blocked(i) ? -1 : 1);
                    --mi.x;
                }
                return true;
            }
        }
    }
    return false;
}

// 35bb:0efc PzMagnets_Step
bool PzMagnets::step(int substep) {
    bool moved = false;
    // Horizontal: thresholds 0x200 every substep, 0x40 on odd substeps, any force on substep 1.
    for (int i = 0; i < kSlots; ++i) {
        MagnetRec& m = st_->slots[static_cast<size_t>(i)];
        const int16_t a = abs16(m.fx);
        const bool go = a > 0x200 || (a > 0x40 && substep % 2 != 0) || (a > 0 && substep == 1);
        if (m.kind == -1) continue;
        if (!(go && m.attached == 0 && !touchesOpposite(i)) && !touchesSame(i, true)) continue;  // fx kept
        const int d = m.fx < 0 ? -1 : 1;
        m.x = static_cast<int16_t>(m.x + d);
        if (blocked(i)) m.x = static_cast<int16_t>(m.x - d);
        else moved = true;
        m.fx = 0;
    }
    // Vertical: thresholds 8 times lower than horizontal, 0x40 and 0x10 (the asymmetry: keep).
    for (int i = 0; i < kSlots; ++i) {
        MagnetRec& m = st_->slots[static_cast<size_t>(i)];
        const int16_t a = abs16(m.fy);
        const bool go = a > 0x40 || (a > 0x10 && substep % 2 != 0) || (a > 0 && substep == 1);
        if (m.kind == -1) continue;
        if (!(go && m.attached == 0 && !touchesOpposite(i)) && !touchesSame(i, false)) continue;
        const int d = m.fy < 0 ? -1 : 1;
        m.y = static_cast<int16_t>(m.y + d);
        if (blocked(i)) m.y = static_cast<int16_t>(m.y - d);
        else moved = true;
        m.fy = 0;
    }
    return moved;
}

// 35bb:113c PzMagnets_Simulate
void PzMagnets::simulate(GgGame& g) {
    drawTable(g.video);
    for (MagnetRec& m : st_->slots) {
        m.attached = 0;  // attached is cleared only when a simulation starts
        m.fx = 0;
        m.fy = 0;
    }
    g.sound.pzPlayLoop(1, 5);  // the hum
    int rounds = 0;
    bool moved = false;
    do {
        moved = false;
        for (int s = 0; s < 4; ++s) {
            g.timers().set(0, 4);
            for (int i = 0; i < kSlots; ++i)
                if (st_->slots[static_cast<size_t>(i)].kind != -1) forces(i);  // accumulated, not cleared
            if (step(s)) moved = true;
            drawTable(g.video);
            g.rt.timerWait(0);
        }
        ++rounds;
    } while (moved && rounds < 16);
    g.sound.pzStop(1);
}

// 35bb:1208 PzMagnets_HoldMagnet
void PzMagnets::holdMagnet(GgGame& g, int kind, int orient, int x, int y) {
    held_ = CursorRecord::fromPicture(memberOf(magnets_, kind * 4 + orient));  // Mouse_LoadCursors
    g.video.cursor().setHotSpot(held_.get(), 0, 0);
    warpMouse(g, x, y);  // the carried magnet appears exactly where it was
    g.video.cursor().setCursorPicture(held_.get());
}

// 35bb:126f PzMagnets_DropCursor
void PzMagnets::dropCursor(GgGame& g, bool offset) {
    const int x = g.mouse().x(), y = g.mouse().y();
    if (offset) warpMouse(g, x + 5, y + 5);
    else warpMouse(g, x, y);
    g.video.cursor().setCursorPicture(cursor_.get());
    held_.reset();  // Mouse_FreeCursor
    g.sound.pzPlay(3, 3);
}

// 35bb:12c4 PzMagnets_TakeFromInventory
void PzMagnets::takeFromInventory(GgGame& g) {
    MagnetsState& s = *st_;
    const int mx = g.mouse().x(), my = g.mouse().y();
    for (int k = 0; k < 3; ++k) {
        const auto pos = static_cast<uint16_t>(dg::kMagnetsInvPos + dg::kMagnetsInvPosStride * static_cast<size_t>(k));
        const int px = exe_.dsS16(pos), py = exe_.dsS16(static_cast<uint16_t>(pos + 2));
        const auto pic = memberOf(magnets_, k * 4);  // the kind's orientation-0 picture
        if (px > mx || py > my || px + s16(pic, 6) <= mx || py + s16(pic, 8) <= my) continue;
        if (s.counts[static_cast<size_t>(k)] <= 0) continue;
        s.heldOrient = 0;
        s.held = static_cast<int16_t>(k);
        holdMagnet(g, k, 0, px, py);
        g.sound.pzPlay(2, 3);
        --s.counts[static_cast<size_t>(k)];
        drawInventory(g.video);
        return;
    }
}

// 35bb:1387 PzMagnets_PutBack
void PzMagnets::putBack(GgGame& g) {
    MagnetsState& s = *st_;
    ++s.counts[static_cast<size_t>(s.held)];
    s.held = -1;
    dropCursor(g, true);
    drawInventory(g.video);
}

// 35bb:13a7 PzMagnets_PickUp
void PzMagnets::pickUp(GgGame& g) {
    MagnetsState& s = *st_;
    const int mx = g.mouse().x(), my = g.mouse().y();
    for (int i = 0; i < kSlots; ++i) {
        MagnetRec& m = s.slots[static_cast<size_t>(i)];
        if (m.kind == -1) continue;
        const auto [w, h] = magnetSize(i);
        if (m.x > mx || m.y > my || m.x + w <= mx || m.y + h <= my) continue;
        s.heldOrient = m.orientation;
        s.held = m.kind;
        holdMagnet(g, m.kind, m.orientation, m.x, m.y);
        g.sound.pzPlay(2, 3);
        m.kind = -1;
        simulate(g);  // the others react to the removal
        return;
    }
}

// 35bb:14f5 PzMagnets_Drop
void PzMagnets::drop(GgGame& g) {
    MagnetsState& s = *st_;
    const int x = g.mouse().x(), y = g.mouse().y();
    const auto pic = memberOf(magnets_, s.held * 4 + s.heldOrient);
    const int w = s16(pic, 6), h = s16(pic, 8);
    // The picture with its shadow must lie on the table.
    if (x < kTableX || y < kTableY || x + w + 2 > kTableRight || y + h + 2 > kTableBottom) return;
    for (int j = 0; j < kSlots; ++j) {
        const MagnetRec& o = s.slots[static_cast<size_t>(j)];
        if (o.kind == -1) continue;
        const auto [ow, oh] = magnetSize(j);
        if (spansOverlap(x, y, w - 2, h - 1, o.x, o.y, ow - 2, oh - 1)) return;
    }
    int j = 0;
    while (j < kSlots && s.slots[static_cast<size_t>(j)].kind != -1) ++j;
    if (j == kSlots) return;  // all 64 slots used
    MagnetRec& m = s.slots[static_cast<size_t>(j)];
    m.kind = s.held;
    m.orientation = s.heldOrient;
    m.x = static_cast<int16_t>(x);
    m.y = static_cast<int16_t>(y);
    s.held = -1;
    dropCursor(g, true);
    simulate(g);
}

// 35bb:16b6 PzMagnets_Rotate
void PzMagnets::rotate(GgGame& g) {
    MagnetsState& s = *st_;
    dropCursor(g, false);  // the cursor freed, effect 3
    if (++s.heldOrient >= 4) s.heldOrient = 0;
    holdMagnet(g, s.held, s.heldOrient, g.mouse().x(), g.mouse().y());
    g.sound.pzPlay(5, 3);
}

// 35bb:16f7 PzMagnets_CheckSolved
bool PzMagnets::checkSolved(int /*level: the targets of the run's level*/) {
    MagnetsState& s = *st_;
    for (MagnetRec& m : s.slots) m.matched = 0;
    for (const Target& t : targets_) {
        int best = -1, bestX = 0x3FFF, bestY = 0x3FFF;
        for (int c = 0; c < kSlots; ++c) {
            const MagnetRec& m = s.slots[static_cast<size_t>(c)];
            if (m.kind == -1) continue;
            const int notBar = m.kind > 0 ? 1 : 0;
            const int o = m.orientation % (notBar ? 4 : 2);  // bars modulo 2: the poles' order does not matter
            const int ddx = abs16(m.x - t.x), ddy = abs16(m.y - t.y);
            if (m.matched == 0 && t.b == notBar && t.a == o && ddx < 5 && ddy < 5 && ddx + ddy < bestX + bestY) {
                bestX = ddx;
                bestY = ddy;
                best = c;
            }
        }
        if (best == -1) return false;
        s.slots[static_cast<size_t>(best)].matched = 1;
    }
    return true;
}

// 35bb:1898 PzMagnets_Loop
bool PzMagnets::loop(GgGame& g, int level) { return loopFrom(g, level, false); }

bool PzMagnets::loopFrom(GgGame& g, int level, bool solved) {
    game_ = &g;
    MagnetsState& s = *st_;
    auto& p = g.puzzles;
    for (bool leave = false; !leave;) {
        const uint16_t c = puzzleGetCode(g);
        switch (c) {
        case code::kMenuRequest1:
            p.leave = 2;
            leave = true;
            break;
        case code::kMenuRequest2:
            p.leave = 1;
            leave = true;
            break;
        case kInvCode:
            if (s.held == -1) takeFromInventory(g);
            else putBack(g);
            break;
        case kTableCode:
            if (s.held == -1) pickUp(g);
            else drop(g);
            // The check runs after every table action, after the simulation, until solved.
            if (!solved && checkSolved(level)) {
                solved = true;
                puzzleShowSolved(g, 6);  // jingle 6..9 (rand)
                puzzleAwardPoints(g, 0x0B);
            }
            g.rt.flushInput();  // Input_Flush
            break;
        case kHintCode:
            g.video.screen().lpxDrawKeyed(kHintX, kHintY, hintPressed_, 0);
            g.sound.pzPlay(4, 5);  // before the flush
            g.display.flush();
            puzzleWaitRelease(g);
            g.video.screen().restoreRect(kHintX, kHintY, 36, 15);
            g.display.flush();
            puzzleShowHintText(g, hintText_);
            break;
        case kRotateCode:
            if (s.held == -1) break;
            g.video.screen().lpxDrawKeyed(kRotateX, kRotateY, memberOf(rotate_, 1), 0);
            g.display.flush();
            puzzleWaitRelease(g);
            rotate(g);
            g.video.screen().restoreRect(kRotateX, kRotateY, 61, 25);
            g.display.flush();
            break;
        case kGoBack:  // a carried magnet is simply dropped with the screen
            g.sound.pzPlay(0x0A, 5);
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
    return solved;
}

// 35bb:1a47 PzMagnets_Init
void PzMagnets::init() {
    MagnetsState& s = *st_;
    s.held = -1;
    for (int k = 0; k < 3; ++k) s.counts[static_cast<size_t>(k)] = static_cast<int16_t>(k + 9);  // overwritten by DrawTargets
    s.heldOrient = 0;
    for (MagnetRec& m : s.slots) m = MagnetRec{};  // kind -1 (the other words are the heap's: zero here)
    s.field56 = 0;
}

// 35bb:1af4 PzMagnets_FreeResources
void PzMagnets::freeResources(GgGame&) {
    rotate_ = Bundle{};
    digits_ = Bundle{};
    magnets_ = Bundle{};
    shadows_ = Bundle{};
    outlines_ = Bundle{};
    hintPressed_.clear();
    held_.reset();  // a magnet still in the hand: its record goes with the puzzle
}

// ---- starting from a DOSBox-X state (puzzles.h)

bool PzMagnets::restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) {
    game_ = &g;
    bind(g.puzzles.magnets);
    MagnetsState& m = *st_;
    loadTargets(g.puzzles.variant);
    // The 64 slots of the heap block DS:af52 (Mem_Alloc(0x480)).
    const auto block = s.bytesAt(s.farPointerAt(dg::kMagnetsSlots), kSlots * MagnetRec::kDgroupSize);
    if (block.size() != kSlots * MagnetRec::kDgroupSize) {
        why = "the magnet slots DS:af52 are not in the state's RAM";
        return false;
    }
    for (size_t i = 0; i < m.slots.size(); ++i) m.slots[i].load(block.data() + MagnetRec::kDgroupSize * i);
    if (m.held < -1 || m.held > 2 || m.heldOrient < 0 || m.heldOrient > 3) {
        why = std::format("g_magnetsHeld {} / orientation {}", m.held, m.heldOrient);
        return false;
    }
    hintText_ = puzzleText(archive_, static_cast<uint16_t>(kHintTextFirst + m.level));
    panelText_ = puzzleText(archive_, kPanelText);
    loadPictures();
    g.puzzlesObjects().panel.load(g.archive());
    g.sound.pzLoadBank(kEffectBank);  // the bank the state's RAM holds (DS:b1b0, the slots DS:b146)
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    invZone_ = zoneFromDs(exe_, dg::kMagnetsInvZone);
    tableZone_ = zoneFromDs(exe_, dg::kMagnetsTableZone);
    hintZone_ = zoneFromDs(exe_, dg::kMagnetsHintZone);
    rotateZone_ = zoneFromDs(exe_, dg::kMagnetsRotateZone);
    if (!relinkStateZones(g, s,
                          {{dg::kMagnetsInvZone, &invZone_},
                           {dg::kMagnetsTableZone, &tableZone_},
                           {dg::kMagnetsHintZone, &hintZone_},
                           {dg::kMagnetsRotateZone, &rotateZone_}},
                          why))
        return false;
    // The cursor: the hand DS:af88 or the magnet in the hand DS:af84 (hot spot 0, 0).
    Cursor& c = g.video.cursor();
    if (stateSamePointer(s, 0x0A6C, dg::kMagnetsHeldCursor) && m.held != -1) {
        held_ = CursorRecord::fromPicture(memberOf(magnets_, m.held * 4 + m.heldOrient));
        restoreStateCursorRecord(g, s, held_.get());
    } else if (stateSamePointer(s, 0x0A6C, dg::kMagnetsCursor)) {
        restoreStateCursorRecord(g, s, cursor_.get());
    } else if (stateSamePointer(s, 0x0A6C, dg::kCursorArrow)) {
        c.setCursorPicture(c.arrow());
    } else {
        why = "the cursor is neither the hand, the magnet in the hand nor the arrow";
        return false;
    }
    return true;
}

int PzMagnets::resume(GgGame& g) {
    // The solved local DI of PzMagnets_Loop: the panel zone is pushed only by Puzzle_ShowSolved.
    bool solved = false;
    for (const Zone* z = g.zones().local(); z; z = z->next)
        if (z == &g.puzzlesObjects().panelZone) solved = true;
    PuzzleUnwindGuard guard(g);
    PointerWarpHook warp(g);
    return finish(g, loopFrom(g, st_->level, solved));
}

}  // namespace tmport::gg
