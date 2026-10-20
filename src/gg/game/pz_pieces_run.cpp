// PzPieces's run on the game (pz_pieces.h, "wave 3: the run"; ovl21 3a4a,
// re/gg/docs/code_puzzles_b.md §2). Owner: puzzles_b agent.
#include "gg/game/pz_pieces.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/flow.h"
#include "gg/game/game.h"
#include "gg/game/pz_restore.h"
#include "gg/save/gg_state_file.h"
#include "gg/ui/hints.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kWorkCode = 0x454, kHintCode = 0x458, kBoxCode = 0x459, kPieceFirst = 0x45B, kPieceLast = 0x465,
                   kGoBack = 0x5DD, kPanel = 0x5DE;
constexpr int kBoxX = 5, kBoxY = 82;        // the lid pictures
constexpr int kCountX = 37, kCountY = 130;  // the box count digit
constexpr int kHintX = 26, kHintY = 13;     // the pressed Hint picture; 35 of its 36 columns restored (keep)

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

// A run of rand() retries that cannot end (no piece left out of the box, only from an inconsistent
// state): the original loops for ever; the engine stops after this many tries and logs.
constexpr int kMaxTries = 1'000'000;

}  // namespace

// 3a4a:0000 PzPieces_Run
int PzPieces::run(GgGame& g, int variant) {
    game_ = &g;
    PuzzleUnwindGuard guard(g);
    g.sound.pzLoadBank(kEffectBank);
    st_->held = kNoPiece;
    loadVariant(variant);
    loadResources(variant);
    initZones(g);
    drawScreen(g);
    addStaticZones(g);
    hintShow(g, 0x1052);
    return loop(g, false);
}

// 3a4a:003a..01ca: the main loop (codes 0x45b..0x465 first, then the switch CS:01cb) and the exit.
int PzPieces::loop(GgGame& g, bool solved) {
    game_ = &g;
    PuzzleUnwindGuard guard(g);
    auto& p = g.puzzles;
    PiecesState& s = *st_;
    for (bool leave = false; !leave;) {
        const uint16_t c = puzzleGetCode(g);
        if (c >= kPieceFirst && c <= kPieceLast) {
            pickUp(g, c);  // reachable only while nothing is carried (the zones are off meanwhile)
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
        case kBoxCode: takeFromBox(g); break;
        case kWorkCode:
            drop(g);
            // The check runs only until the puzzle is solved; the pieces stay movable afterwards (keep).
            if (!solved && checkSolved(g)) {
                solved = true;
                puzzleShowSolved(g, 3);  // jingle 3..6 (rand)
                puzzleAwardPoints(g, 0x16);
            }
            break;
        case kHintCode:
            if (s.held != kNoPiece) break;
            g.video.screen().lpxDrawKeyed(kHintX, kHintY, hintPressed_, 0);
            g.display.flush();
            g.sound.pzPlay(0x12, 5);
            puzzleWaitRelease(g);
            g.video.screen().restoreRect(kHintX, kHintY, 35, 15);  // 35 of 36 columns: the last stays (keep)
            g.display.flush();
            puzzleShowHintText(g, hintText(variant_));  // the block of strlen(format) + strlen(name), freed after
            break;
        case kGoBack:
            if (s.held != kNoPiece) break;  // only while no piece is carried
            g.sound.pzPlay(0x15, 5);
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
    free(g);
    g.sound.pzFreeBank();
    g.display.fadeOut();
    game_ = nullptr;
    return solved ? 1 : 0;
}

std::unique_ptr<CursorRecord> PzPieces::pieceCursor(int i) const {
    // Mouse_LoadCursors(the piece's picture) with the hot spot (w / 2, h / 2) of the record's size.
    const PieceRec& r = st_->pieces[static_cast<size_t>(i)];
    auto rec = CursorRecord::fromPicture(picture(i));
    rec->hotX = r.w / 2;
    rec->hotY = r.h / 2;
    return rec;
}

// 3a4a:01ef PzPieces_SetMouseBounds
void PzPieces::setMouseBounds(GgGame& g, bool work) {
    const CursorRecord& c = g.video.cursor().active();  // g_cursorW DS:7a70 / g_cursorH DS:7a72
    if (work) puzzleSetMouseRange(g, kWorkX, kWorkY, kWorkRight - c.width, kWorkBottom - c.height);
    else puzzleSetMouseRange(g, 0, 0, 319 - c.width, 199 - c.height);
}

// 3a4a:0265 PzPieces_PickUp
void PzPieces::pickUp(GgGame& g, int code) {
    PiecesState& s = *st_;
    const int i = code - kPieceFirst;
    removeZones(g);
    Screen& screen = g.video.screen();
    screen.restoreRect(kWorkX, kWorkY, kWorkW, kWorkH);
    s.held = static_cast<int16_t>(i);
    for (int j = 0; j < s.count; ++j) {  // every placed piece but i: no inside test, no flush
        const PieceRec& o = s.pieces[static_cast<size_t>(j)];
        if (j != i && o.placed == 1) screen.lpxDrawKeyed(o.x, o.y, picture(j), 0);
    }
    held_ = pieceCursor(i);
    // g_mouseX / g_mouseY clamped by hand (the driver is told by the bounds below).
    const PieceRec& r = s.pieces[static_cast<size_t>(i)];
    int x = g.mouse().x(), y = g.mouse().y();
    if (x < kWorkX) x = kWorkX + 1;
    if (y < kWorkY) y = kWorkY + 1;
    if (x + r.w > kWorkRight) x = kWorkRight - r.w - 1;
    if (y + r.h > kWorkBottom) y = kWorkBottom - r.h - 1;
    g.mouse().setPosition(static_cast<int16_t>(x), static_cast<int16_t>(y));
    g.video.cursor().setCursorPicture(held_.get());
    g.sound.pzPlay(0, 3);
    setMouseBounds(g, true);
    g.display.flush();
}

// 3a4a:037d PzPieces_Drop
void PzPieces::drop(GgGame& g) {
    PiecesState& s = *st_;
    if (s.held == kNoPiece) return;
    PieceRec& h = s.pieces[static_cast<size_t>(s.held)];
    s.group = h.group;
    h.x = g.mouse().x();  // the picture's top-left (no hot spot)
    h.y = g.mouse().y();
    if (h.x < kWorkX || h.y < kWorkY || h.x + h.w > kWorkRight || h.y + h.h > kWorkBottom) return;  // cannot happen with the bounds
    const int ax0 = h.x - h.dx, ay0 = h.y - h.dy;  // the anchor of the held piece
    int ax = ax0, ay = ay0;
    for (int j = 0; j < s.count; ++j) {
        if (j == s.held) continue;
        const PieceRec& o = s.pieces[static_cast<size_t>(j)];
        if (o.placed != 1) continue;
        const int bx = o.x - o.dx, by = o.y - o.dy;
        if (bx + 10 > ax0 && bx - 10 < ax0 && by + 10 > ay0 && by - 10 < ay0) {  // strict on both axes
            h.x = static_cast<int16_t>(bx + h.dx);  // snap to j's anchor; the first such j in index order
            h.y = static_cast<int16_t>(by + h.dy);
            mergeGroups(j);
            ax = bx;
            ay = by;
            break;
        }
    }
    g.video.cursor().setCursorPicture(blank_.get());  // the piece appears at the end of the slides
    moveGroup(g, ax, ay);
    h.placed = 1;
    g.sound.pzPlay(1, 3);
    redrawWork(g.video);
    g.video.cursor().setCursorPicture(cursor_.get());
    setMouseBounds(g, false);
    held_.reset();  // Mouse_FreeCursor
    addZones(g);
    s.held = kNoPiece;
}

// 3a4a:05b8 PzPieces_MoveGroup
void PzPieces::moveGroup(GgGame& g, int ax, int ay) {
    PiecesState& s = *st_;
    // Keep the group inside the work area, member by member with the running anchor.
    for (int i = 0; i < s.count; ++i) {
        const PieceRec& m = s.pieces[static_cast<size_t>(i)];
        if (m.group != s.group) continue;
        const int x = ax + m.dx, y = ay + m.dy;
        if (x < kWorkX) ax += kWorkX - x;
        else if (x + m.w > kWorkRight) ax -= x + m.w - kWorkRight;
        if (y < kWorkY) ay += kWorkY - y;
        else if (y + m.h > kWorkBottom) ay -= y + m.h - kWorkBottom;
    }
    // Each member slides in 5 steps (unpaced: one redraw and flush per step), then lands exactly.
    for (int i = 0; i < s.count; ++i) {
        PieceRec& m = s.pieces[static_cast<size_t>(i)];
        if (m.group != s.group) continue;
        const int stepX = (ax + m.dx - m.x) / 5, stepY = (ay + m.dy - m.y) / 5;  // signed, toward 0
        if (stepX != 0 || stepY != 0) {
            for (int k = 0; k < 5; ++k) {
                m.x = static_cast<int16_t>(m.x + stepX);
                m.y = static_cast<int16_t>(m.y + stepY);
                redrawWork(g.video);
            }
        }
        m.x = static_cast<int16_t>(ax + m.dx);
        m.y = static_cast<int16_t>(ay + m.dy);
        redrawWork(g.video);
        zones_[static_cast<size_t>(i)].x = m.x;
        zones_[static_cast<size_t>(i)].y = m.y;
    }
}

// 3a4a:071d PzPieces_MergeGroups
void PzPieces::mergeGroups(int j) {
    PiecesState& s = *st_;
    const int16_t gh = s.pieces[static_cast<size_t>(s.held)].group;
    const int16_t gj = s.pieces[static_cast<size_t>(j)].group;
    // Equal groups leave CX as the caller had it, the piece index j, as the "old" number: a group number
    // is its smallest member index, so this renames nothing (keep).
    int old = j;
    if (gh < gj) {
        s.group = gh;
        old = gj;
    } else if (gj < gh) {
        old = gh;
        s.group = gj;
    }
    for (int d = 0; d < s.count; ++d)
        if (s.pieces[static_cast<size_t>(d)].group == old) s.pieces[static_cast<size_t>(d)].group = s.group;
}

// 3a4a:0780 PzPieces_TakeFromBox
void PzPieces::takeFromBox(GgGame& g) {
    PiecesState& s = *st_;
    if (s.held != kNoPiece || s.inBox <= 0) return;
    openBox(g);
    --s.inBox;
    removeZones(g);
    // One rand() per try until a piece not yet placed comes up (keep: the count depends on the draws).
    int tries = 0;
    do {
        s.held = static_cast<int16_t>(g.rand.random(s.count));  // 3a4a:07a4: rand() * n / 0x8000
        if (++tries >= kMaxTries) {
            log::warn("PzPieces_TakeFromBox: no piece is left out of the box (the original loops for ever)");
            break;
        }
    } while (s.pieces[static_cast<size_t>(s.held)].placed == 1);
    closeBox(g);
    Screen& screen = g.video.screen();
    screen.restoreRect(kCountX, kCountY, 10, 10);
    screen.lpxDrawKeyed(kCountX, kCountY, memberOf(digits_, s.inBox), 0);
    held_ = pieceCursor(s.held);
    g.video.cursor().setCursorPicture(held_.get());
    g.sound.pzPlay(4, 3);
    // Busy-wait on the interrupt-driven pointer until the piece lies wholly in the work area: no input
    // code is read meanwhile (keep); the host is pumped.
    const PieceRec& r = s.pieces[static_cast<size_t>(s.held)];
    while (!(g.mouse().x() >= kWorkX && g.mouse().y() >= kWorkY && g.mouse().x() + r.w <= kWorkRight &&
             g.mouse().y() + r.h <= kWorkBottom))
        g.rt.idle();
    setMouseBounds(g, true);
}

// 3a4a:089c PzPieces_AddZones
void PzPieces::addZones(GgGame& g) {
    for (int i = 0; i < st_->count; ++i)
        if (st_->pieces[static_cast<size_t>(i)].placed == 1) g.zones().addLocal(zones_[static_cast<size_t>(i)]);
}

// 3a4a:08c9 PzPieces_RemoveZones
void PzPieces::removeZones(GgGame& g) {
    for (int i = 0; i < st_->count; ++i)
        if (st_->pieces[static_cast<size_t>(i)].placed == 1) g.zones().removeLocal(&zones_[static_cast<size_t>(i)]);
}

// 3a4a:08f6 PzPieces_CheckSolved
bool PzPieces::checkSolved(GgGame& g) {
    const PiecesState& s = *st_;
    const PieceRec& p0 = s.pieces[0];
    if (p0.placed != 1) return false;
    const int ax = p0.x - p0.dx, ay = p0.y - p0.dy;
    for (int i = 1; i < s.count; ++i) {
        const PieceRec& p = s.pieces[static_cast<size_t>(i)];
        if (p.placed != 1 || p.x - p.dx != ax || p.y - p.dy != ay) return false;
    }
    g.timers().set(7, 200);  // overwritten by the reveal's own Timer_Set: no effect (keep)
    Screen& screen = g.video.screen();
    screen.lpxDrawKeyed(ax, ay, machine_, 0);
    screen.saveToBack(kWorkX, kWorkY, kWorkW, kWorkH);  // the back page: pieces + machine
    screen.setDrawColor(0x76);
    screen.fillRect(kWorkX, kWorkY, kWorkW, kWorkH);
    for (int i = 0; i < s.count; ++i) {
        const PieceRec& p = s.pieces[static_cast<size_t>(i)];
        if (p.placed == 1) screen.lpxDrawKeyed(p.x, p.y, picture(i), 0);
    }
    g.sound.pzPlay(0x13, 5);
    const int mw = machine_.size() >= 10 ? static_cast<int16_t>(machine_[6] | (machine_[7] << 8)) : 0;
    const int mh = machine_.size() >= 10 ? static_cast<int16_t>(machine_[8] | (machine_[9] << 8)) : 0;
    for (int y = ay; ay + mh > y; y += 5) {  // the machine top to bottom, 5 rows per 10 ticks
        screen.restoreRect(ax, y, mw, 5);
        g.timers().set(7, 10);
        g.rt.timerWait(7);
        g.display.flush();
    }
    // The work area holds only the machine on plain colour 0x76; the back page is plain, so a later
    // pick-up redraws the pieces without the machine (keep). Flushed by the caller's Puzzle_ShowSolved.
    screen.setDrawColor(0x76);
    screen.fillRect(kWorkX, kWorkY, kWorkW, kWorkH);
    screen.saveToBack(kWorkX, kWorkY, kWorkW, kWorkH);
    screen.lpxDrawKeyed(ax, ay, machine_, 0);
    g.sound.pzStop(0x13);
    return true;
}

// 3a4a:0a88 PzPieces_OpenBox
void PzPieces::openBox(GgGame& g) {
    g.sound.pzPlay(0x0C, 3);
    for (int k = 1; k < 3; ++k) {
        g.video.screen().lpxDrawKeyed(kBoxX, kBoxY, memberOf(box_, k), 0);
        g.display.flush();
        g.timers().set(7, 10);
        g.rt.timerWait(7);
    }
}

// 3a4a:0ade PzPieces_CloseBox
void PzPieces::closeBox(GgGame& g) {
    g.sound.pzPlay(0x0D, 3);
    for (int k = 1; k >= 0; --k) {
        g.video.screen().lpxDrawKeyed(kBoxX, kBoxY, memberOf(box_, k), 0);
        g.display.flush();
        g.timers().set(7, 10);
        g.rt.timerWait(7);
    }
}

// 3a4a:0b33 PzPieces_InitZones
void PzPieces::initZones(GgGame&) {
    for (int i = 0; i < st_->count; ++i) {
        const PieceRec& p = st_->pieces[static_cast<size_t>(i)];
        Zone& z = zones_[static_cast<size_t>(i)];
        // x, y (+2, +4) are set when the piece is placed (PzPieces_MoveGroup); the group word stays.
        z.shape = 0;
        z.w = p.w;
        z.h = p.h;
        // 32000 - w * h / 100 with the product truncated to 16 bits before the division (IMUL, CWD).
        z.priority = static_cast<int16_t>(32000 - static_cast<int16_t>(p.w * p.h) / 100);
        z.next = nullptr;
        z.pressCode = static_cast<uint16_t>(kPieceFirst + i);
        z.releaseCode = 0;
    }
}

// 3a4a:0baf PzPieces_AddStaticZones
void PzPieces::addStaticZones(GgGame& g) {
    workZone_ = zoneFromDs(exe_, dg::kPiecesWorkZone);
    boxZone_ = zoneFromDs(exe_, dg::kPiecesBoxZone);
    hintZone_ = zoneFromDs(exe_, dg::kPiecesHintZone);
    g.zones().addLocal(workZone_);
    g.zones().addLocal(boxZone_);
    g.zones().addLocal(hintZone_);
}

// 3a4a:0be5 PzPieces_DrawScreen
void PzPieces::drawScreen(GgGame& g) {
    savedCursor_ = g.video.cursor().activeRecord();  // the 0x18-byte copy of g_cursor DS:7a6a
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    blank_ = loadCursor(archive_, 4);
    puzzleLoadScreen(g, puzzleKindInfo(2).picture, puzzleKindInfo(2).palette);
    puzzleInitPanel(g, panelText_);
    g.video.screen().saveToBack(0, 0, 320, 200);
    g.video.screen().lpxDrawKeyed(kCountX, kCountY, memberOf(digits_, st_->inBox), 0);  // not in the back page
    g.video.cursor().setCursorPicture(cursor_.get());
    g.display.fadeOutIn();
}

// 3a4a:0d95 PzPieces_Free
void PzPieces::free(GgGame& g) {
    g.zones().clearLocal();
    // The saved copy of g_cursor goes back into DS:7a6a and becomes the cursor (the engine selects the
    // record it was taken from; the original also puts back the copy's stale save-under fields).
    Cursor& c = g.video.cursor();
    c.setCursorPicture(savedCursor_ ? const_cast<CursorRecord*>(savedCursor_) : c.arrow());
    held_.reset();
    cursor_.reset();
    blank_.reset();
    puzzleFreePanel(g);
}

std::vector<uint8_t> PzPieces::hintText(int variant) const {
    // The machine's name: the far pointer DS:357c[v] into DGROUP (DS:35cc..36cf), NUL-terminated.
    std::vector<uint8_t> name;
    if (variant >= 0 && static_cast<size_t>(variant) < dg::kPiecesExampleNamesCount) {
        uint16_t off = static_cast<uint16_t>(exe_.dsS16(static_cast<uint16_t>(dg::kPiecesExampleNames + 4 * variant)));
        for (int guard = 0; guard < 256; ++guard, ++off) {
            const uint8_t ch = exe_.ds8(off);
            if (ch == 0) break;
            name.push_back(ch);
        }
    }
    // sprintf(buffer, format, name): the format's one %s replaced ("%%" gives "%").
    const auto format = puzzleText(archive_, kHintFormat);
    std::vector<uint8_t> out;
    for (size_t i = 0; i < format.size(); ++i) {
        if (format[i] == '%' && i + 1 < format.size()) {
            if (format[i + 1] == 's') {
                out.insert(out.end(), name.begin(), name.end());
                ++i;
                continue;
            }
            if (format[i + 1] == '%') {
                out.push_back('%');
                ++i;
                continue;
            }
        }
        out.push_back(format[i]);
    }
    return out;
}

// ---- starting from a DOSBox-X state (puzzles.h)

bool PzPieces::restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) {
    game_ = &g;
    bind(g.puzzles.pieces);
    const int variant = g.puzzles.variant;
    loadPictures(variant);
    if (st_->count != static_cast<int>(bundle_.size()) - 1) {
        why = std::format("g_piecesCount {} does not match bundle {:04x}", st_->count, kBundleFirst + variant);
        return false;
    }
    if (st_->held != kNoPiece && (st_->held < 0 || st_->held >= st_->count)) {
        why = std::format("g_piecesHeld is {}", st_->held);
        return false;
    }
    g.puzzlesObjects().panel.load(g.archive());
    g.sound.pzLoadBank(kEffectBank);  // the bank the state's RAM holds (DS:b1b0, the slots DS:b146)
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    blank_ = loadCursor(archive_, 4);
    savedCursor_ = nullptr;  // the copy DS:b6be: the arrow at the puzzle's start
    // The zone records as the state holds them (the pieces' positions are in theirs).
    workZone_ = zoneFromDs(exe_, dg::kPiecesWorkZone);
    boxZone_ = zoneFromDs(exe_, dg::kPiecesBoxZone);
    hintZone_ = zoneFromDs(exe_, dg::kPiecesHintZone);
    std::vector<std::pair<uint16_t, Zone*>> zones = {
        {dg::kPiecesWorkZone, &workZone_}, {dg::kPiecesBoxZone, &boxZone_}, {dg::kPiecesHintZone, &hintZone_}};
    for (int i = 0; i < kMaxPieces; ++i) {
        const auto off = static_cast<uint16_t>(dg::kPiecesZones + Zone::kRecordSize * static_cast<size_t>(i));
        zones_[static_cast<size_t>(i)] = Zone::fromRecord(s.dgroup().subspan(off, Zone::kRecordSize));
        zones.emplace_back(off, &zones_[static_cast<size_t>(i)]);
    }
    if (!relinkStateZones(g, s, zones, why)) return false;
    // The cursor: the hand DS:b6c2, the blank DS:b6b6 (a group sliding), the piece in the hand DS:b6ba.
    Cursor& c = g.video.cursor();
    if (stateSamePointer(s, 0x0A6C, dg::kPiecesHeldCursor) && st_->held != kNoPiece) {
        held_ = pieceCursor(st_->held);
        restoreStateCursorRecord(g, s, held_.get());
        // The bounds PzPieces_SetMouseBounds(1) gave the driver (the pointer is as the state has it).
        g.mouse().driver().setRangeX(2 * kWorkX, 2 * (kWorkRight - held_->width));
        g.mouse().driver().setRangeY(kWorkY, kWorkBottom - held_->height);
    } else if (stateSamePointer(s, 0x0A6C, dg::kPiecesCursor)) {
        restoreStateCursorRecord(g, s, cursor_.get());
    } else if (stateSamePointer(s, 0x0A6C, dg::kPiecesBlankCursor)) {
        restoreStateCursorRecord(g, s, blank_.get());
    } else if (stateSamePointer(s, 0x0A6C, dg::kCursorArrow)) {
        c.setCursorPicture(c.arrow());
    } else {
        why = "the cursor is neither the hand, the blank, the held piece nor the arrow";
        return false;
    }
    return true;
}

int PzPieces::resume(GgGame& g) {
    // The solved local SI of PzPieces_Run: the panel zone is pushed only by Puzzle_ShowSolved.
    bool solved = false;
    for (const Zone* z = g.zones().local(); z; z = z->next)
        if (z == &g.puzzlesObjects().panelZone) solved = true;
    return loop(g, solved);
}

}  // namespace tmport::gg
