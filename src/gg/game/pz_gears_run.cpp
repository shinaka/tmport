// PzGears's run on the game (pz_gears.h, "wave 3: the run"; ovl22 3b2f, re/gg/docs/code_puzzles_b.md
// §3). Owner: puzzles_b agent.
//
// The game moves the pointer itself (PzGears_WarpMouse: INT 33h fn 4 and g_mouseX/Y) when a gear is
// taken, picked up, placed or put back and at every crank step while the handle is dragged. The engine
// moves its driver model and g_mouseX/Y together (GgMouse::warp); a windowed host follows through
// GgMouse::onWarp (game/pointer_warp.h installs the hook for the puzzle's run).
#include "gg/game/pz_gears.h"

#include <algorithm>
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
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kHintCode = 0x3E8, kCrankCode = 0x44C, kLargeCode = 0x44D, kSmallCode = 0x44E, kBoardCode = 0x44F,
                   kGearFirst = 0x4B0, kGearEnd = 0x514, kGoBack = 0x5DD, kPanel = 0x5DE;
constexpr int kHintX = 23, kHintY = 24;  // the pressed Hint picture, restored with all 36 columns

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

int16_t abs16(int v) {
    const auto x = static_cast<int16_t>(v);
    return static_cast<int16_t>(x < 0 ? -x : x);
}

}  // namespace

// 3b2f:1ed0 PzGears_Run
int PzGears::run(GgGame& g, int variant) {
    game_ = &g;
    PuzzleUnwindGuard guard(g);
    PointerWarpHook warp(g);
    loadVariant(variant);  // with the crank rectangles and the crank zone
    loadResources(variant);
    g.sound.pzLoadBank(kEffectBank);
    g.video.cursor().setCursorPicture(cursor_.get());
    puzzleInitPanel(g, panelText_);  // before the screen: its 157 rows cover the panel's top two (keep)
    // 3b2f:071c PzGears_DrawScreen
    puzzleLoadScreen(g, puzzleKindInfo(3).picture, puzzleKindInfo(3).palette);
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c) drawPeg(g.video.screen(), c, r, peg(c, r) ? 7 : 1);
    g.video.screen().saveToBack(0, 0, 320, 200);
    draw(g.video, true);
    countStock();
    drawInventory(g.video);
    g.display.fadeOutIn();
    addZones(g);
    saveHome();
    hintShow(g, 0x1052);
    return loop(g, false);
}

// 3b2f:1f25..206d: the main loop and the exit.
int PzGears::loop(GgGame& g, bool solved) {
    game_ = &g;
    PuzzleUnwindGuard guard(g);
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
        case kLargeCode: takeFromStock(g, 1); break;
        case kSmallCode: takeFromStock(g, 0); break;
        case kCrankCode:
            if (carrying(g)) {  // a press on the handle while carrying is a placement attempt
                place(g, g.mouse().x(), g.mouse().y());
                break;
            }
            saveFrames();
            dragCrank(g);
            if (solved) break;  // after a success, drags neither check nor reset
            solved = allButtonsPressed();  // only when the drag ends (the button released)
            if (solved) {
                puzzleShowSolved(g, 5);  // jingle 5..8 (rand)
                puzzleAwardPoints(g, 0x0B);
            } else {
                reset(g);
            }
            break;
        case kBoardCode: place(g, g.mouse().x(), g.mouse().y()); break;
        case kHintCode:
            g.video.screen().lpxDrawKeyed(kHintX, kHintY, hintPressed_, 0);
            g.display.flush();
            g.sound.pzPlay(9, 5);
            puzzleWaitRelease(g);
            g.video.screen().restoreRect(kHintX, kHintY, 36, 15);
            g.display.flush();
            puzzleShowHintText(g, hintText_);
            break;
        case kGoBack:  // a carried gear is dropped silently with the screen; its record is not freed (keep)
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
        default:
            // A press on a placed gear's circle; ignored while a gear is carried (PzGears_PickUp).
            if (c >= kGearFirst && c < kGearEnd) pickUp(g, c - kGearFirst);
            break;
        }
    }
    puzzleFreePanel(g);
    g.display.fadeOut();
    free(g);
    game_ = nullptr;
    return solved ? 1 : 0;
}

bool PzGears::carrying(const GgGame& g) const {
    return held_ && const_cast<GgGame&>(g).video.cursor().defaultRegion().record == held_.get();
}

std::unique_ptr<CursorRecord> PzGears::gearCursor(int i) const {
    const Object& o = st_->objects[static_cast<size_t>(i)];
    auto rec = CursorRecord::fromPicture(memberOf(gears_[o.type == 1 ? 1 : 0], o.state));
    const int r = tableS16(dg::kGearsRadius, o.type);
    rec->hotX = r;  // stored into the record directly (+0x0a / +0x0c)
    rec->hotY = r;
    return rec;
}

// 3b2f:0000 PzGears_AllButtonsPressed
bool PzGears::allButtonsPressed() {
    for (const Object& o : st_->objects)
        if (o.type >= 7 && o.state == 0) return false;
    return true;
}

// 3b2f:0070 PzGears_AddZones
void PzGears::addZones(GgGame& g) {
    for (Zone& z : gearZones_) z = Zone{};  // Mem_Alloc(0x898): one circle zone per placed gear
    boardZone_ = zoneFromDs(exe_, dg::kGearsBoardZone);
    largeZone_ = zoneFromDs(exe_, dg::kGearsLargeZone);
    smallZone_ = zoneFromDs(exe_, dg::kGearsSmallZone);
    hintZone_ = zoneFromDs(exe_, dg::kGearsHintZone);
    g.zones().addLocal(boardZone_);
    g.zones().addLocal(largeZone_);
    g.zones().addLocal(smallZone_);
    g.zones().addLocal(hintZone_);
}

// 3b2f:079b PzGears_InitCrankRects
void PzGears::initCrankRects() {
    GearsState& s = *st_;
    const Object& crank = s.objects[0];  // object 0 is the crank in every shipped variant
    const int cx = crank.col * 9, cy = crank.row * 9;
    s.crankCX = static_cast<int16_t>(cx + 0x61);
    s.crankCY = static_cast<int16_t>(cy + 0x28);
    for (size_t k = 0; k < s.crankRect.size(); ++k) {
        const auto o = static_cast<uint16_t>(dg::kGearsCrankRects + dg::kGearsCrankRectsStride * k);
        RectRec& r = s.crankRect[k];
        r.x0 = static_cast<int16_t>(exe_.dsS16(o) + cx + 0x43);
        r.y0 = static_cast<int16_t>(exe_.dsS16(static_cast<uint16_t>(o + 2)) + cy + 0x0A);
        r.x1 = static_cast<int16_t>(exe_.dsS16(static_cast<uint16_t>(o + 4)) + cx + 0x43);
        r.y1 = static_cast<int16_t>(exe_.dsS16(static_cast<uint16_t>(o + 6)) + cy + 0x0A);
    }
}

// 3b2f:081e PzGears_SetCrankZone
void PzGears::setCrankZone(GgGame& g) {
    const GearsState& s = *st_;
    const RectRec& r = s.crankRect[static_cast<size_t>(std::clamp(s.objects[0].state + 1, 0, 25))];
    crankZone_ = Zone{};  // setmem(0x16): the zone is not unlinked first (the callers remove it)
    crankZone_.x = r.x0;
    crankZone_.y = r.y0;
    crankZone_.w = static_cast<int16_t>(r.x1 - r.x0);
    crankZone_.h = static_cast<int16_t>(r.y1 - r.y0);
    crankZone_.priority = 0x63;
    crankZone_.pressCode = kCrankCode;
    g.zones().addLocal(crankZone_);
}

// 3b2f:0892 PzGears_AddGearZone
void PzGears::addGearZone(GgGame& g, int i) {
    const Object& o = st_->objects[static_cast<size_t>(i)];
    const int t = o.type, r = tableS16(dg::kGearsRadius, t);
    Zone& z = gearZones_[static_cast<size_t>(i)];
    z = Zone{};  // setmem(0x16)
    z.shape = 1;  // a circle at the gear's centre
    z.x = static_cast<int16_t>(o.col * 9 + tableS16(dg::kGearsPicX, t) + r);
    z.y = static_cast<int16_t>(o.row * 9 + tableS16(dg::kGearsPicY, t) + r);
    z.w = static_cast<int16_t>(r);
    z.priority = 10;
    z.pressCode = static_cast<uint16_t>(kGearFirst + i);
    g.zones().addLocal(z);
}

// 3b2f:0972 PzGears_RemoveGearZone
void PzGears::removeGearZone(GgGame& g, int i) { g.zones().removeLocal(&gearZones_[static_cast<size_t>(i)]); }

// 3b2f:098f PzGears_WarpMouse
void PzGears::warpMouse(GgGame& g, int x, int y) {
    g.mouse().warp(static_cast<int16_t>(x), static_cast<int16_t>(y));  // g_mouseX/Y, INT 33h AX=04h (CX = 2x)
}

// 3b2f:09b3 PzGears_PutBack
void PzGears::putBack(GgGame& g) {
    GearsState& s = *st_;
    const int t = s.objects[static_cast<size_t>(s.held)].type;
    ++s.stock[static_cast<size_t>(t)];  // the object keeps column -99
    const int r = tableS16(dg::kGearsRadius, t);
    warpMouse(g, tableS16(dg::kGearsInvX, t) + r, tableS16(dg::kGearsInvY, t) + r);  // the inventory picture's centre
    g.sound.pzPlay(1, 1);
    g.video.cursor().setCursorPicture(cursor_.get());
    held_.reset();  // Mouse_FreeCursor
    s.carrying = false;
    drawInventory(g.video);
}

// 3b2f:0a22 PzGears_TakeFromStock
void PzGears::takeFromStock(GgGame& g, int type) {
    GearsState& s = *st_;
    if (carrying(g)) {
        putBack(g);  // whatever circle was clicked
        return;
    }
    if (s.stock[static_cast<size_t>(type)] == 0) return;
    for (int j = 0; j < objectCount(); ++j) {
        const Object& o = s.objects[static_cast<size_t>(j)];
        if (o.type == type && o.col == kNotPlaced) {
            s.held = static_cast<int16_t>(j);
            break;
        }
    }
    held_ = gearCursor(s.held);
    --s.stock[static_cast<size_t>(type)];
    warpMouse(g, tableS16(dg::kGearsInvX, type), tableS16(dg::kGearsInvY, type));  // covers the inventory picture
    g.sound.pzPlay(0, 1);
    g.video.cursor().setCursorPicture(held_.get());
    s.carrying = true;
    drawInventory(g.video);
}

// 3b2f:0b18 PzGears_PickUp
void PzGears::pickUp(GgGame& g, int i) {
    GearsState& s = *st_;
    if (carrying(g)) return;  // gears cannot be dropped onto another gear's circle
    Object& o = s.objects[static_cast<size_t>(i)];
    held_ = gearCursor(i);
    warpMouse(g, tableS16(dg::kGearsPicX, o.type) + o.col * 9, tableS16(dg::kGearsPicY, o.type) + o.row * 9);  // the gear's top-left
    o.row = kNotPlaced;  // in neither place: the stock is not changed
    o.col = kNotPlaced;
    s.held = static_cast<int16_t>(i);
    g.sound.pzPlay(0, 1);
    removeGearZone(g, i);
    g.video.cursor().setCursorPicture(held_.get());
    s.carrying = true;
    draw(g.video, true);
}

// 3b2f:0c2b PzGears_Place
void PzGears::place(GgGame& g, int mx, int my) {
    GearsState& s = *st_;
    if (!carrying(g)) return;
    Object& h = s.objects[static_cast<size_t>(s.held)];
    const int t = h.type, radius = tableS16(dg::kGearsRadius, t);
    const int px = mx + radius, py = my + radius;  // the carried gear's centre
    const int c = (px - 0x5B) / 9, r = (py - 0x22) / 9;  // signed, toward 0
    // The grid byte is read before the range test (an out-of-range cell reads past the grid): a
    // refusal either way.
    if (c < 0 || r < 0 || c >= kCols || r >= kRows || s.grid[static_cast<size_t>(r * kCols + c)] == 0) return;
    const Object& crank = s.objects[0];
    {
        const int dx = abs16(c - crank.col), dy = abs16(r - crank.row);
        if (static_cast<int16_t>(dx * dx + dy * dy) < minDist2(t, 0)) return;  // the crank counts as a small gear
    }
    int partner = -1;
    for (int j = 0; j < objectCount(); ++j) {
        const Object& o = s.objects[static_cast<size_t>(j)];
        const int dx = abs16(c - o.col), dy = abs16(r - o.row);
        if (j == s.held || o.col == kNotPlaced || o.type > 1) continue;  // racks are not checked (empty branches)
        if (static_cast<int16_t>(dx * dx + dy * dy) < minDist2(t, o.type)) return;
        if (dx <= 4 && dy <= 4 && meshes(t, o.type, dy, dx)) partner = j;  // the last wins
    }
    if (partner != -1 && (h.state & 1) == (s.objects[static_cast<size_t>(partner)].state & 1)) {
        if (++h.state >= frames(t)) h.state = 0;  // the teeth interleave visually
    }
    h.col = static_cast<int16_t>(c);
    h.row = static_cast<int16_t>(r);
    addGearZone(g, s.held);
    warpMouse(g, px, py);
    g.sound.pzPlay(1, 1);
    g.video.cursor().setCursorPicture(cursor_.get());
    held_.reset();
    s.carrying = false;
    draw(g.video, true);
}

// 3b2f:0ecc PzGears_GearToGear
int PzGears::gearToGear(int i, int j) {
    GearsState& s = *st_;
    const Object& a = s.objects[static_cast<size_t>(i)];
    Object& b = s.objects[static_cast<size_t>(j)];
    const int dx = abs16(a.col - b.col), dy = abs16(a.row - b.row);
    if (dx > 4 || dy > 4 || !meshes(a.type, b.type, dy, dx)) return 0;
    if (b.dir != 0) {
        if (b.dir == a.dir) s.jam = 1;  // two meshed gears driven the same way
        return 1;
    }
    // One frame per step whatever the size: no gear ratios (keep).
    if (a.dir == -1) {
        b.dir = 1;
        if (--b.state < 0) b.state = static_cast<int16_t>(frames(b.type) - 1);
    } else {
        b.dir = -1;
        if (++b.state >= frames(b.type)) b.state = 0;
    }
    return 1;
}

// 3b2f:0ff9 PzGears_HRackToHRack
int PzGears::hRackToHRack(int i, int j) {
    GearsState& s = *st_;
    const Object& a = s.objects[static_cast<size_t>(i)];
    Object& b = s.objects[static_cast<size_t>(j)];
    if (a.row != b.row) return 0;
    const int from = a.col - a.dir, to = from + 2 * a.length;  // i before its move
    const int e = b.col;
    if (e < from) {
        if (a.type == b.type && a.dir > 0) return 0;  // the same type only pushes in the direction of motion
        if (e + 2 * b.length < from) return 0;
    } else {
        if (a.type == b.type && a.dir < 0) return 0;
        if (e > to) return 0;
    }
    if (b.dir == 0) {
        b.dir = a.dir;
        b.col = static_cast<int16_t>(b.col + a.dir);
    }
    if (a.dir != b.dir) s.jam = 1;
    return 1;
}

// 3b2f:111d PzGears_HRackToButton
int PzGears::hRackToButton(int i, int j) {
    const Object& a = st_->objects[static_cast<size_t>(i)];
    Object& b = st_->objects[static_cast<size_t>(j)];
    if (a.row != b.row) return 0;
    const int from = a.col - a.dir;
    if (b.type == 7) {  // the left button: pressed when the rack moves left onto it
        if (b.col + 7 == from) {
            if (a.dir > 0) b.state = 0;
        } else if (b.col + 8 == from) {
            if (a.dir < 0) b.state = 1;
        }
    } else {  // the right button
        const int end = from + 2 * a.length;
        if (b.col + 1 == end) {
            if (a.dir < 0) b.state = 0;
        } else if (b.col == end) {
            if (a.dir > 0) b.state = 1;
        }
    }
    return 0;  // buttons do not propagate
}

// 3b2f:11dc PzGears_VRackToButton
int PzGears::vRackToButton(int i, int j) {
    const Object& a = st_->objects[static_cast<size_t>(i)];
    Object& b = st_->objects[static_cast<size_t>(j)];
    if (a.col != b.col) return 0;
    const int from = a.row - a.dir;
    if (b.type == 10) {  // the button above
        if (b.row + 8 == from) {
            if (a.dir > 0) b.state = 0;
        } else if (b.row + 9 == from) {
            if (a.dir < 0) b.state = 1;
        }
    } else {  // the button below
        const int end = from + 2 * a.length;
        if (b.row + 1 == end) {
            if (a.dir < 0) b.state = 0;
        } else if (b.row == end) {
            if (a.dir > 0) b.state = 1;
        }
    }
    return 0;
}

// 3b2f:129b PzGears_VRackToVRack
int PzGears::vRackToVRack(int i, int j) {
    GearsState& s = *st_;
    const Object& a = s.objects[static_cast<size_t>(i)];
    Object& b = s.objects[static_cast<size_t>(j)];
    if (a.col != b.col) return 0;
    const int from = a.row - a.dir, to = from + 2 * a.length;
    const int e = b.row;
    if (e < from) {
        if (a.type == b.type && a.dir > 0) return 0;
        if (e + 2 * b.length < from) return 0;
    } else {
        if (a.type == b.type && a.dir < 0) return 0;
        if (e > to) return 0;
    }
    if (b.dir == 0) {
        b.dir = a.dir;
        b.row = static_cast<int16_t>(b.row + a.dir);
    }
    if (a.dir != b.dir) s.jam = 1;
    return 1;
}

// 3b2f:13bf PzGears_GearToHRack
int PzGears::gearToHRack(int i, int j) {
    GearsState& s = *st_;
    const Object& a = s.objects[static_cast<size_t>(i)];
    Object& b = s.objects[static_cast<size_t>(j)];
    const int t = a.type;
    const int delta = b.row - a.row;
    int step = 0;
    if (delta == t + 1) step = a.dir;          // the rack below the gear
    else if (delta == -(t + 2)) step = -a.dir;  // above
    else return 0;
    const int u = b.col - b.dir + step;  // the rack as if moved by step from where it was before this step
    if (3 * (a.col + t) + (t ^ 1) < u) return 0;
    if (3 * (a.col - t) > u + 2 * b.length) return 0;
    if (b.dir == 0) {
        b.dir = static_cast<int16_t>(step);
        b.col = static_cast<int16_t>(b.col + step);
    }
    if (b.dir != step) s.jam = 1;
    if (b.col < 0) s.jam = 1;  // a toothed rack driven by a gear jams below position 0
    return 1;
}

// 3b2f:14ae PzGears_HRackToGear
int PzGears::hRackToGear(int i, int j) {
    GearsState& s = *st_;
    const Object& a = s.objects[static_cast<size_t>(i)];
    Object& b = s.objects[static_cast<size_t>(j)];
    const int t = b.type;
    const int delta = a.row - b.row;
    int step = 0;
    if (delta == t + 1) step = a.dir;
    else if (delta == -(t + 2)) step = -a.dir;
    else return 0;
    if (3 * (b.col + t) + (t ^ 1) < a.col) return 0;
    if (a.col + 2 * a.length < 3 * (b.col - t)) return 0;
    if (b.dir == 0) {
        b.dir = static_cast<int16_t>(step);
        if (step == 1) {
            if (--b.state < 0) b.state = static_cast<int16_t>(frames(t) - 1);
        } else if (++b.state >= frames(t)) {
            b.state = 0;
        }
    }
    if (b.dir != step) s.jam = 1;
    return 1;
}

// 3b2f:15c3 PzGears_GearToVRack
int PzGears::gearToVRack(int i, int j) {
    GearsState& s = *st_;
    const Object& a = s.objects[static_cast<size_t>(i)];
    Object& b = s.objects[static_cast<size_t>(j)];
    const int t = a.type;
    const int delta = b.col - a.col;
    int step = 0;
    if (delta == t + 1) step = -a.dir;  // the rack right of the gear
    else if (delta == -(t + 2)) step = a.dir;
    else return 0;
    const int u = b.row - b.dir + step;
    if (3 * (a.row + t) + (t ^ 1) < u) return 0;
    if (3 * (a.row - t) > u + 2 * b.length) return 0;
    if (b.dir == 0) {
        b.dir = static_cast<int16_t>(step);
        b.row = static_cast<int16_t>(b.row + step);
    }
    if (b.dir != step) s.jam = 1;
    if (b.row < 0) s.jam = 1;
    return 1;
}

// 3b2f:16b2 PzGears_VRackToGear
int PzGears::vRackToGear(int i, int j) {
    GearsState& s = *st_;
    const Object& a = s.objects[static_cast<size_t>(i)];
    Object& b = s.objects[static_cast<size_t>(j)];
    const int t = b.type;
    const int delta = a.col - b.col;
    int step = 0;
    if (delta == t + 1) step = -a.dir;
    else if (delta == -(t + 2)) step = a.dir;
    else return 0;
    if (3 * (b.row + t) + (t ^ 1) < a.row) return 0;
    if (a.row + 2 * a.length < 3 * (b.row - t)) return 0;
    if (b.dir == 0) {
        b.dir = static_cast<int16_t>(step);
        if (step == 1) {
            if (--b.state < 0) b.state = static_cast<int16_t>(frames(t) - 1);
        } else if (++b.state >= frames(t)) {
            b.state = 0;
        }
    }
    if (b.dir != step) s.jam = 1;
    return 1;
}

// 3b2f:17c7 PzGears_Propagate
void PzGears::propagate(int i) {
    GearsState& s = *st_;
    s.objects[static_cast<size_t>(i)].visited = 1;
    for (int j = 0; j < objectCount(); ++j) {
        if (j == i) continue;
        if (s.objects[static_cast<size_t>(j)].col == kNotPlaced) continue;
        // Only rows 0..5 are ever used: the source is a gear (the crank as type 0) or a rack.
        const Drive f = driveHandler(s.objects[static_cast<size_t>(i)].type, s.objects[static_cast<size_t>(j)].type);
        if (!f || (this->*f)(i, j) == 0) continue;
        if (s.objects[static_cast<size_t>(j)].visited == 0) propagate(j);  // depth first
    }
}

// 3b2f:185c PzGears_TurnCrank
void PzGears::turnCrank(GgGame& g, int dir) {
    GearsState& s = *st_;
    for (Object& o : s.objects) {
        if (o.type < 2 || o.type > 5) o.savedState = o.state;  // gears, the crank and the buttons
        o.savedCol = o.col;
        o.savedRow = o.row;
    }
    Object& crank = s.objects[0];
    if (dir == -1) {
        if (++crank.state >= 24) crank.state = 0;
    } else {
        if (--crank.state < 0) crank.state = 23;
    }
    s.jam = 0;
    crank.dir = static_cast<int16_t>(dir);
    crank.type = 0;  // the crank acts as a small gear
    propagate(0);
    crank.type = 6;
    g.sound.pzPlay(0, 1);
    draw(g.video, true);
    if (s.jam) {
        // A jammed step is drawn moved and then restored (a visible flick: keep); the crank does not turn.
        for (Object& o : s.objects) {
            if (o.type < 2 || o.type > 5) o.state = o.savedState;
            o.col = o.savedCol;
            o.row = o.savedRow;
        }
        g.sound.pzPlay(0, 1);
        draw(g.video, true);
    }
}

// 3b2f:1991 PzGears_RectContains
bool PzGears::rectContains(const RectRec& r, int x, int y) {
    return r.x0 <= x && r.x1 > x && r.y0 <= y && r.y1 > y;  // no caller (keep as dead code)
}

int PzGears::quadrant(int x, int y) const {
    const int cx = st_->crankCX, cy = st_->crankCY;
    if (x > cx) return y < cy ? 0 : 1;
    return y > cy ? 2 : 3;
}

int PzGears::slope(int x, int y) const {
    const int dx = static_cast<int16_t>(x - st_->crankCX);
    if (dx == 0) return 0x7FFF;
    const auto dy = static_cast<int16_t>(y - st_->crankCY);
    return static_cast<int16_t>(static_cast<int16_t>(dy * 100) / dx);  // 16-bit product, truncated
}

// 3b2f:19bf PzGears_DragCrank
void PzGears::dragCrank(GgGame& g) {
    GearsState& s = *st_;
    g.zones().removeLocal(&crankZone_);
    g.video.cursor().setCursorPicture(blank_.get());
    s.turning = true;  // the crank with the hand on the handle (0x2025)
    draw(g.video, false);
    int prevX = g.mouse().x(), prevY = g.mouse().y();
    // A busy loop on the interrupt-driven pointer and buttons: no input code is read during the drag
    // (keys wait in the queue, global keys do not act: keep); the host is pumped.
    while (g.mouse().buttons() != 0) {
        const int curX = g.mouse().x(), curY = g.mouse().y();
        if (curX == prevX && curY == prevY) {
            g.rt.idle();
            continue;
        }
        const int frame0 = s.objects[0].state;
        const int d = (quadrant(curX, curY) - quadrant(prevX, prevY)) & 3;
        if (d == 1) {
            turnCrank(g, -1);  // clockwise across a quadrant border
        } else if (d == 3) {
            turnCrank(g, 1);  // counter-clockwise
        } else if (d == 0) {
            // Within a quadrant the slope decides: clockwise on screen increases it.
            const int sPrev = slope(prevX, prevY), sCur = slope(curX, curY);
            if (sPrev != sCur) turnCrank(g, sCur < sPrev ? 1 : -1);
        }
        if (s.objects[0].state != frame0) {
            // One step per mouse event; the pointer is moved back onto the handle of the new frame.
            const RectRec& r = s.crankRect[static_cast<size_t>(std::clamp(s.objects[0].state + 1, 0, 25))];
            prevX = r.x0 + (r.x1 - r.x0) / 2;
            prevY = r.y0 + (r.y1 - r.y0) / 2;
            warpMouse(g, prevX, prevY);
        } else {
            prevX = curX;
            prevY = curY;
        }
        g.rt.idle();
    }
    g.video.cursor().setCursorPicture(cursor_.get());
    setCrankZone(g);
    s.turning = false;
    draw(g.video, false);
}

// 3b2f:1cad PzGears_Free
void PzGears::free(GgGame& g) {
    hintPressed_.clear();
    g.video.cursor().setCursorPicture(g.video.cursor().arrow());
    blank_.reset();
    cursor_.reset();
    held_.reset();
    st_->carrying = false;
    g.zones().clearLocal();
    g.sound.pzFreeBank();
}

// 3b2f:1e0a PzGears_SaveFrames
void PzGears::saveFrames() {
    for (Object& o : st_->objects)
        if (o.type == 0 || o.type == 1 || o.type == 6) o.length = o.state;  // +8 = frame
}

// 3b2f:1e46 PzGears_Reset
void PzGears::reset(GgGame& g) {
    for (Object& o : st_->objects) {
        switch (o.type) {
        case 0:
        case 1:
        case 6: o.state = o.length; break;  // the frame at the start of the drag
        case 2:
        case 4: o.col = o.state; break;  // home
        case 3:
        case 5: o.row = o.state; break;
        case 7:
        case 8:
        case 9:
        case 10: o.state = 0; break;
        default: break;
        }
    }
    g.zones().removeLocal(&crankZone_);
    setCrankZone(g);
    draw(g.video, true);
}

// ---- starting from a DOSBox-X state (puzzles.h)

bool PzGears::restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) {
    game_ = &g;
    bind(g.puzzles.gears);
    GearsState& st = *st_;
    variant_ = g.puzzles.variant;
    // The F copy DS:b6d2 (heap): the grid at its start, the objects at +0x116.
    const int n = st.objectCount;
    if (n < 1 || n > 200) {
        why = std::format("g_gearsObjectCount is {}", n);
        return false;
    }
    const auto copy = s.bytesAt(s.farPointerAt(dg::kGearsData), 0x116 + 20 * static_cast<size_t>(n));
    if (copy.size() != 0x116 + 20 * static_cast<size_t>(n)) {
        why = "the variant copy DS:b6d2 is not in the state's RAM";
        return false;
    }
    std::copy(copy.begin(), copy.begin() + 0x114, st.grid.begin());
    st.objects.assign(static_cast<size_t>(n), Object{});
    for (int i = 0; i < n; ++i) st.objects[static_cast<size_t>(i)].load(copy.data() + 0x116 + 20 * static_cast<size_t>(i));
    if (st.objects[0].type != 6 || st.objects[0].state < 0 || st.objects[0].state > 23) {
        why = "object 0 of the variant copy is not the crank";
        return false;
    }
    loadResources(variant_);
    st.turning = stateSamePointer(s, dg::kGearsCrankCur, dg::kGearsCrankGripPics);
    g.puzzlesObjects().panel.load(g.archive());
    g.sound.pzLoadBank(kEffectBank);  // the bank the state's RAM holds (DS:b1b0, the slots DS:b146)
    // The zones: the DGROUP ones by their offsets, the gear zones by their place in the heap array.
    boardZone_ = zoneFromDs(exe_, dg::kGearsBoardZone);
    largeZone_ = zoneFromDs(exe_, dg::kGearsLargeZone);
    smallZone_ = zoneFromDs(exe_, dg::kGearsSmallZone);
    hintZone_ = zoneFromDs(exe_, dg::kGearsHintZone);
    for (Zone& z : gearZones_) z = Zone{};
    const uint32_t gearZonesAt = s.farPointerAt(dg::kGearsGearZones);
    std::vector<Zone*> linked;
    const uint32_t ds = static_cast<uint32_t>(s.dgroupSegment()) * 16;
    auto farAt = [&](uint32_t linear) -> uint32_t {
        const auto b = s.bytesAt(linear, 4);
        if (b.size() < 4) return 0;
        const uint32_t off = static_cast<uint32_t>(b[0] | (b[1] << 8)), seg = static_cast<uint32_t>(b[2] | (b[3] << 8));
        return seg == 0 && off == 0 ? 0 : seg * 16 + off;
    };
    uint32_t next = farAt(ds + dg::kZonesLocal);
    for (int guard = 0; next != 0 && guard < 200; ++guard) {
        Zone* z = nullptr;
        const std::pair<uint16_t, Zone*> known[] = {{dg::kGearsBoardZone, &boardZone_},     {dg::kGearsLargeZone, &largeZone_},
                                                    {dg::kGearsSmallZone, &smallZone_},     {dg::kGearsHintZone, &hintZone_},
                                                    {dg::kGearsCrankZone, &crankZone_},     {dg::kPzGoBackZone, &g.puzzlesObjects().goBackZone},
                                                    {dg::kPzPanelZone, &g.puzzlesObjects().panelZone}};
        for (const auto& [off, zone] : known)
            if (next == ds + off) z = zone;
        if (!z && gearZonesAt != 0 && next >= gearZonesAt && next < gearZonesAt + Zone::kRecordSize * kMaxGearZones &&
            (next - gearZonesAt) % Zone::kRecordSize == 0)
            z = &gearZones_[(next - gearZonesAt) / Zone::kRecordSize];
        if (!z) {
            why = std::format("a click zone the puzzle does not know (linear {:x})", next);
            return false;
        }
        *z = Zone::fromRecord(s.bytesAt(next, Zone::kRecordSize));
        linked.push_back(z);
        next = farAt(next + 0x0C);
    }
    g.zones().clearLocal();
    for (auto z = linked.rbegin(); z != linked.rend(); ++z) g.zones().addLocal(**z);  // the head is added last
    // The cursor: the hand DS:b6ca, the blank DS:b6ce (turning) or the carried gear DS:b6c6.
    Cursor& c = g.video.cursor();
    if (stateSamePointer(s, 0x0A6C, dg::kGearsHeldCursor) && st.held >= 0 && st.held < n) {
        held_ = gearCursor(st.held);
        restoreStateCursorRecord(g, s, held_.get());
    } else if (stateSamePointer(s, 0x0A6C, dg::kGearsCursor)) {
        restoreStateCursorRecord(g, s, cursor_.get());
    } else if (stateSamePointer(s, 0x0A6C, dg::kGearsBlankCursor)) {
        restoreStateCursorRecord(g, s, blank_.get());
    } else if (stateSamePointer(s, 0x0A6C, dg::kCursorArrow)) {
        c.setCursorPicture(c.arrow());
    } else {
        why = "the cursor is neither the hand, the blank, the carried gear nor the arrow";
        return false;
    }
    st.carrying = carrying(g);
    return true;
}

int PzGears::resume(GgGame& g) {
    // The solved local DI of PzGears_Run: the panel zone is pushed only by Puzzle_ShowSolved.
    bool solved = false;
    for (const Zone* z = g.zones().local(); z; z = z->next)
        if (z == &g.puzzlesObjects().panelZone) solved = true;
    PointerWarpHook warp(g);
    return loop(g, solved);
}

}  // namespace tmport::gg
