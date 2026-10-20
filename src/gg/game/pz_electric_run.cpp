// PzElectric's run on the game (pz_electric.h, "wave 3: the run"; ovl13 3083,
// re/gg/docs/code_puzzles_a.md §3.5-§3.11). Owner: puzzles_a agent.
#include "gg/game/pz_electric.h"

#include <algorithm>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/game.h"
#include "gg/save/gg_state_file.h"
#include "gg/ui/hints.h"
#include "gg/game/flow.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

constexpr uint16_t kBoardCode = 0x454, kHintCode = 0x458, kToolboxCode = 0x459, kToolFirst = 0x45B, kToolLast = 0x45F;
constexpr uint16_t kPieceFirst = 0x4B0, kPieceLast = 0x4BF, kGoBack = 0x5DD, kPanel = 0x5DE;

}  // namespace

// 3083:08a1 PzElectric_Run
int PzElectric::run(GgGame& g, int variant) {
    PuzzleUnwindGuard guard(g);  // the set-up yields too (the fades, the hint box)
    setupGame(g, variant);
    return loop(g, false);
}

// 3083:08a1..08ff: the set-up before the loop.
void PzElectric::setupGame(GgGame& g, int variant) {
    game_ = &g;
    st_->plugShown = 0;
    loadResources(variant);
    loadVariant(variant);
    screenRegion_ = cursorRegionAt(exe_, dg::kElecScreenRegion, cursor_.get());
    g.video.cursor().addRegion(&screenRegion_);  // MouseRegion_Add(DS:2488)
    initToolZones(g);
    // 3083:24aa PzElec_InitToolCursorRegions: the zones' rectangles, cursor = label picture i.
    for (size_t i = 0; i < tools_.size(); ++i) {
        labelCursors_[i] = CursorRecord::fromPicture(memberOf(labels_, static_cast<int>(i)));
        toolRegions_[i] = {toolZones_[i].shape, toolZones_[i].x, toolZones_[i].y, toolZones_[i].w, toolZones_[i].h,
                           toolZones_[i].priority, labelCursors_[i].get()};
    }
    // 3083:250e PzElec_DrawScreen
    puzzleLoadScreen(g, puzzleKindInfo(0).picture, puzzleKindInfo(0).palette);
    puzzleInitPanel(g, panelText_);
    g.video.screen().saveToBack(0, 0, 320, 200);
    drawBoard(g.video, false);
    g.display.fadeOutIn();  // Screen_FadeOutIn: the room was faded by state 8
    g.video.cursor().setCursorPicture(cursor_.get());
    drawToolbox(g.video);
    addBoardZones(g);
    addToolZones(g);
    addPieceZones(g);
    addToolCursorRegions(g);
    st_->mode = kNone;
    g.sound.pzLoadBank(kEffectBank);  // after the screen is shown
    hintShow(g, 0x1052);              // the puzzle New Player Hint, once per player
}

// 3083:0902..0adc: the main loop and the exit.
int PzElectric::loop(GgGame& g, bool solved) {
    game_ = &g;
    PuzzleUnwindGuard guard(g);
    auto& p = g.puzzles;
    auto success = [&] {
        solved = true;
        pzSndWaitEnd(g, 1);      // the bulb's sound ends first
        puzzleShowSolved(g, 7);  // jingle 7..10
        puzzleAwardPoints(g, 0xD);
    };
    for (bool leave = false; !leave;) {
        uint16_t code = 0;
        for (;;) {
            code = puzzleGetCode(g);
            if (code != 0) break;
            if (st_->mode == Wire) updatePlugCursor(g);
        }
        if (code >= kPieceFirst && code <= kPieceLast) {
            const int pc = code - kPieceFirst;
            if (piece(pc).type == Switch && toggleSwitch(g, pc, g.mouse().x(), g.mouse().y())) {  // the lever
                if (isSolved() && !solved) success();
                pzPlay(piece(pc).closed != 0 ? 2 : 3, 3);  // the click comes after the reward
            } else {
                pickUpPiece(g, pc);
            }
            continue;
        }
        switch (code) {
        case kBoardCode:
            if (st_->mode == Wire) {
                layWire(g);
                if (isSolved() && !solved) success();
            } else if (st_->mode == kNone) {
                cutWire(g);
                if (isSolved() && !solved) success();
            } else {
                placePiece(g);  // no success test after placing (keep)
            }
            break;
        case kToolboxCode: returnToToolbox(g); break;
        case kHintCode:
            if (st_->mode != kNone) break;
            g.video.screen().lpxDrawKeyed(26, 13, hintPressed_, 0);
            g.display.flush();
            g.sound.pzPlay(0x0B, 5);
            puzzleWaitRelease(g);
            g.video.screen().restoreRect(26, 13, 35, 15);  // 35 of the 36 columns (keep)
            g.display.flush();
            puzzleShowHintText(g, hintText_);
            break;
        case kGoBack:
            if (st_->mode != kNone) break;
            g.sound.pzPlay(0x0C, 5);
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
        case code::kMenuRequest1:
            p.leave = 2;
            leave = true;
            break;
        case code::kMenuRequest2:
            p.leave = 1;
            leave = true;
            break;
        default:
            if (code >= kToolFirst && code <= kToolLast) takeFromToolbox(g, code);
            break;
        }
    }
    g.sound.pzFreeBank();
    cleanup(g);
    g.display.fadeOut();
    game_ = nullptr;
    return solved ? 1 : 0;
}

// 3083:0844 PzElec_SetBulbPicture
void PzElectric::setBulbPicture(GgGame& g, int bulb, bool lit) {
    auto& pic = picture_[static_cast<size_t>(bulb)];
    if (!lit) {
        pic = tools_[Bulb].picture;
    } else if (pic.data() != bulbLitPic_.data()) {
        pic = bulbLitPic_;
        g.sound.pzPlay(1, 4);  // a bulb lights
    }
}

// 3083:0b15 PzElec_LayWire
void PzElectric::layWire(GgGame& g) {
    st_->foundPiece = kNone;
    if (!findFreeTerminal(g) || term(st_->foundPiece, st_->foundTerm).piece != kNone) return;  // the wire stays in hand
    g.sound.pzPlay(4, 3);
    setMouseRange(g, true);
    const int startPiece = st_->foundPiece, startTerm = st_->foundTerm;
    auto hot = [&] { return std::pair<int, int>{g.mouse().x() + g.video.cursor().hotX(), g.mouse().y() + g.video.cursor().hotY()}; };
    bool done = false;
    while (!done) {
        const auto p = hot();
        drawBoard(g.video, false);
        drawRubberBand(g, p.first, p.second);
        g.display.flush();
        // Only the board code is looked at while the hot point stays; other codes are swallowed. After
        // a miss the loop goes on until the pointer moves (a further click may still string a wire).
        while (hot() == p) {
            const uint16_t code = puzzleGetCode(g);
            updatePlugCursor(g);
            if (code != kBoardCode) continue;
            if (!findFreeTerminal(g)) {
                // A miss: the wire goes back into the toolbox (DS:a8ce + 1: the wire count, see wireBack).
                term(st_->foundPiece, st_->foundTerm).piece = kNone;
                wireBack();
                drawBoard(g.video, false);
                drawToolbox(g.video);
                g.video.cursor().setCursorPicture(cursor_.get());
                freeHeldCursor();
                st_->mode = kNone;
                addPieceZones(g);
                addToolCursorRegions(g);
                g.zones().removeLocal(&toolboxZone_);
                done = true;
                continue;
            }
            if (startPiece == st_->foundPiece && startTerm == st_->foundTerm) {
                // Unreachable: the search skips the start piece (DS:a8c2).
                term(st_->foundPiece, st_->foundTerm).piece = kNone;
                drawBoard(g.video, true);
                done = true;
                break;
            }
            if (term(st_->foundPiece, st_->foundTerm).piece != kNone) continue;
            ElecTerminalRec& target = term(st_->foundPiece, st_->foundTerm);
            target.piece = static_cast<int16_t>(startPiece);
            target.term = static_cast<int16_t>(startTerm);
            target.flag = 0;
            ElecTerminalRec& start = term(startPiece, startTerm);
            start.piece = st_->foundPiece;
            start.term = st_->foundTerm;
            start.flag = 1;
            drawBoard(g.video, false);
            g.video.cursor().setCursorPicture(cursor_.get());
            freeHeldCursor();
            st_->mode = kNone;
            addPieceZones(g);
            addToolCursorRegions(g);
            g.zones().removeLocal(&toolboxZone_);
            done = true;
            g.sound.pzPlay(4, 3);
            break;
        }
    }
    setMouseRange(g, false);
}

// 3083:0d1a PzElec_SetMouseRange
void PzElectric::setMouseRange(GgGame& g, bool board) {
    const CursorRecord& c = g.video.cursor().active();  // DS:7a70 / 7a72: the current cursor's size
    if (board) puzzleSetMouseRange(g, 0x5B - c.width + 3, 0x0B, 0x139 - c.width, 0x90);
    else puzzleSetMouseRange(g, 0, 0, 0x13F - c.width, 0xC7 - c.height);
}

// 3083:0d97 PzElec_CutWire
void PzElectric::cutWire(GgGame& g) {
    for (int i = 0; i < kSlots; ++i) {
        if (piece(i).used == 0) continue;
        for (int t = 0; t < 3; ++t) {
            const ElecTerminalRec& own = term(i, t);
            if (own.piece == kNone || own.flag == 0) continue;
            const ElecPieceRec& q = piece(own.piece);
            const ElecTerminalRec& other = term(own.piece, own.term);
            // The 1 is the puzzle cursor's click offset (its record's +0x0a).
            if (!onWire(g.mouse().x() + cursor_->hotX, g.mouse().y(), piece(i).x + own.anchorX, piece(i).y + own.anchorY,
                        q.x + other.anchorX, q.y + other.anchorY))
                continue;
            ElecTerminalRec& partner = term(own.piece, own.term);
            partner.piece = kNone;
            partner.flag = 0;
            term(i, t).piece = kNone;  // the own flag stays
            removePieceZones(g);
            g.zones().addLocal(toolboxZone_);
            g.video.cursor().clearRegions();
            // The wire spool with its offset (DS:252a, DS:2532 / 2534: tool 4).
            held_ = newHeldCursor(tools_[Wire].picture, tools_[Wire].hotX, tools_[Wire].hotY);
            g.video.cursor().setCursorPicture(held_);
            g.sound.pzPlay(5, 3);
            st_->mode = Wire;
            drawBoard(g.video, true);
            return;
        }
    }
}

// 3083:0f18 PzElec_OnWire
bool PzElectric::onWire(int px, int py, int x1, int y1, int x2, int y2) {
    if (x1 < x2 && pointInRect(px, py, x1, y1 - 1, x2 - x1 - 1, 3)) return true;
    if (x1 > x2 && pointInRect(px, py, x2, y1 - 1, x1 - x2, 3)) return true;
    if (y1 < y2 && pointInRect(px, py, x2 - 1, y1, 3, y2 - y1)) return true;
    if (y1 > y2 && pointInRect(px, py, x2 - 1, y2, 3, y1 - y2)) return true;
    return false;
}

// 3083:11a8 PzElec_DrawRubberBand
void PzElectric::drawRubberBand(GgGame& g, int x, int y) {
    Screen& screen = g.video.screen();
    const int ax = st_->wireAnchorX, ay = st_->wireAnchorY;
    if (ax < x) {
        for (int d = -1; d <= 1; ++d) screen.hLine(ax, ay + d, x - ax + 1);
    } else if (x < ax) {
        for (int d = -1; d <= 1; ++d) screen.hLine(x, ay + d, ax - x);
    }
    if (ay < y) {
        for (int d = -1; d <= 1; ++d) screen.vLine(x + d, ay, y - ay);
    } else if (y < ay) {
        for (int d = -1; d <= 1; ++d) screen.vLine(x + d, y, ay - y);
    }
    drawTerminalDot(screen, st_->foundPiece, st_->foundTerm);
}

// 3083:1327 PzElec_ToggleSwitch
bool PzElectric::toggleSwitch(GgGame& g, int pc, int mx, int my) {
    ElecPieceRec& p = piece(pc);
    if (!pointInRect(mx, my, p.x + 7, p.y + 0x14, 9, 0x10)) return false;  // the raw pointer, no hot spot
    if (p.closed == 0) {
        picture_[static_cast<size_t>(pc)] = switchClosed_;
        p.closed = 1;
    } else {
        picture_[static_cast<size_t>(pc)] = tools_[Switch].picture;
        p.closed = 0;
    }
    drawBoard(g.video, true);
    return true;
}

// 3083:13c5 PzElec_PickUpPiece
void PzElectric::pickUpPiece(GgGame& g, int pc) {
    ElecPieceRec& p = piece(pc);
    if (p.fixed != 0) return;
    for (int t = 0; t < 3; ++t) {
        ElecTerminalRec& own = term(pc, t);
        if (own.piece == kNone) continue;
        ElecTerminalRec& other = term(own.piece, own.term);
        if (own.flag != 3 && other.flag != 3) wireBack();  // a wire (not a contact) goes back into the toolbox
        other.flag = 0;
        other.piece = kNone;
        own.flag = 0;
        own.piece = kNone;
    }
    removePieceZones(g);
    g.zones().addLocal(toolboxZone_);
    g.video.cursor().clearRegions();
    p.used = 0;
    if (p.type == Switch) {
        p.closed = 0;
        picture_[static_cast<size_t>(pc)] = tools_[Switch].picture;
    }
    if (p.type == Bulb) {
        p.closed = 0;
        picture_[static_cast<size_t>(pc)] = tools_[Bulb].picture;
    }
    const Tool& tool = tools_[static_cast<size_t>(std::clamp<int>(p.type, 0, 4))];
    held_ = newHeldCursor(picture_[static_cast<size_t>(pc)], tool.hotX, tool.hotY);
    g.video.cursor().setCursorPicture(held_);
    g.sound.pzPlay(0, 3);
    st_->mode = p.type;
    drawBoard(g.video, false);
    drawToolbox(g.video);
}

// 3083:156a PzElec_ReturnToToolbox
void PzElectric::returnToToolbox(GgGame& g) {
    if (st_->mode >= 0 && st_->mode < 5) ++st_->toolCounts[static_cast<size_t>(st_->mode)];
    else log::debug("PzElec_ReturnToToolbox: mode {} (the original writes past the counts)", st_->mode);
    g.video.cursor().setCursorPicture(cursor_.get());
    freeHeldCursor();
    addPieceZones(g);
    st_->mode = kNone;
    g.zones().removeLocal(&toolboxZone_);
    drawToolbox(g.video);
    addToolCursorRegions(g);
}

// 3083:15ad PzElec_TakeFromToolbox
void PzElectric::takeFromToolbox(GgGame& g, int code) {
    const int i = code - kToolFirst;
    if (st_->toolCounts[static_cast<size_t>(i)] <= 0) return;
    --st_->toolCounts[static_cast<size_t>(i)];
    held_ = newHeldCursor(tools_[static_cast<size_t>(i)].picture, tools_[static_cast<size_t>(i)].hotX, tools_[static_cast<size_t>(i)].hotY);
    g.video.cursor().setCursorPicture(held_);
    g.sound.pzPlay(0, 3);
    st_->mode = static_cast<int16_t>(i);
    removePieceZones(g);
    g.video.cursor().clearRegions();
    drawToolbox(g.video);
    g.zones().addLocal(toolboxZone_);
}

// 3083:172a PzElec_PlacePiece
void PzElectric::placePiece(GgGame& g) {
    const int mx = g.mouse().x(), my = g.mouse().y();  // the piece's top-left = the pointer
    const auto pic = tools_[static_cast<size_t>(std::clamp<int>(st_->mode, 0, 4))].picture;
    const int w = s16(pic, 6), h = s16(pic, 8);
    if (!(mx > 0x5A && mx + w < 0x138 && my > 0x12 && my + h < 0x97)) return;
    for (int i = 0; i < kSlots; ++i) {
        if (piece(i).used == 0) continue;
        const auto other = picture_[static_cast<size_t>(i)];
        // The bodies (y + 7, h - 14) of the full widths must not overlap.
        if (spansOverlap(mx, my + 7, w, h - 14, piece(i).x, piece(i).y + 7, s16(other, 6), s16(other, 8) - 14)) return;
    }
    g.video.cursor().setCursorPicture(cursor_.get());
    freeHeldCursor();
    createPiece(mx, my);
    drawBoard(g.video, true);
    g.sound.pzPlay(4, 3);
    st_->mode = kNone;
    addPieceZones(g);
    addToolCursorRegions(g);
    g.zones().removeLocal(&toolboxZone_);
}

// 3083:1a94 PzElec_UpdatePlugCursor
void PzElectric::updatePlugCursor(GgGame& g) {
    const int hx = g.mouse().x() + g.video.cursor().hotX(), hy = g.mouse().y() + g.video.cursor().hotY();
    for (int i = 0; i < kSlots; ++i) {
        if (piece(i).used == 0) continue;
        for (int t = 0; t < 3; ++t) {
            const ElecTerminalRec& tt = term(i, t);
            if (!pointInRect(hx, hy, piece(i).x + tt.hitX, piece(i).y + tt.hitY, tt.hitW, tt.hitH)) continue;
            // The first terminal holding the hot point decides.
            if (st_->plugShown != 0 || tt.piece != kNone) return;
            activeCursorCopy(g.video).picture = plug_;  // DS:7a78 = the plug picture
            st_->plugShown = 1;
            return;
        }
    }
    if (st_->plugShown != 0) {
        activeCursorCopy(g.video).picture = held_ ? held_->picture : std::span<const uint8_t>{};  // the held picture back
        st_->plugShown = 0;
    }
}

// 3083:1b75 PzElec_FindFreeTerminal
bool PzElectric::findFreeTerminal(GgGame& g) {
    const int hx = g.mouse().x() + g.video.cursor().hotX(), hy = g.mouse().y() + g.video.cursor().hotY();
    for (int i = 0; i < kSlots; ++i) {
        if (piece(i).used == 0 || i == st_->foundPiece) continue;
        for (int t = 0; t < 3; ++t) {
            const ElecTerminalRec& tt = term(i, t);
            if (!pointInRect(hx, hy, piece(i).x + tt.hitX, piece(i).y + tt.hitY, tt.hitW, tt.hitH) || tt.piece != kNone) continue;
            st_->wireAnchorY = static_cast<int16_t>(piece(i).y + tt.anchorY);
            st_->wireAnchorX = static_cast<int16_t>(piece(i).x + tt.anchorX);
            st_->foundTerm = static_cast<int16_t>(t);
            st_->foundPiece = static_cast<int16_t>(i);
            return true;
        }
    }
    return false;
}

// 3083:1c63 PzElec_PointInRect
bool PzElectric::pointInRect(int px, int py, int x, int y, int w, int h) {
    return !(px < x || x + w <= px || py < y || y + h <= py);
}

// 3083:1d1d PzElec_AddPieceZones
void PzElectric::addPieceZones(GgGame& g) {
    for (int i = 0; i < kSlots; ++i)
        if (piece(i).used != 0) g.zones().addLocal(pieceZones_[static_cast<size_t>(i)]);
}

// 3083:1d43 PzElec_RemovePieceZones
void PzElectric::removePieceZones(GgGame& g) {
    for (int i = 0; i < kSlots; ++i)
        if (piece(i).used != 0) g.zones().removeLocal(&pieceZones_[static_cast<size_t>(i)]);
}

// 3083:1d69 PzElec_AddToolZones
void PzElectric::addToolZones(GgGame& g) {
    for (Zone& z : toolZones_) g.zones().addLocal(z);
}

// 3083:1d83 PzElec_AddToolCursorRegions
void PzElectric::addToolCursorRegions(GgGame& g) {
    for (size_t i = 0; i < toolRegions_.size(); ++i)
        if (st_->toolCounts[i] != 0) g.video.cursor().addRegion(&toolRegions_[i]);
}

// 3083:1dab PzElec_InitToolZones
void PzElectric::initToolZones(GgGame&) {
    for (size_t i = 0; i < toolZones_.size(); ++i) {
        Zone& z = toolZones_[i];
        z.shape = 0;
        z.init(static_cast<int16_t>(tools_[i].x), static_cast<int16_t>(tools_[i].y), s16(tools_[i].picture, 6),
               s16(tools_[i].picture, 8), 0, static_cast<uint16_t>(kToolFirst + i), 0);
        z.group = 0;
    }
    toolboxZone_ = zoneFromDs(exe_, dg::kElecToolboxZone);
}

// 3083:1f22 PzElec_AddBoardZones
void PzElectric::addBoardZones(GgGame& g) {
    boardZone_ = zoneFromDs(exe_, dg::kElecBoardZone);
    hintZone_ = zoneFromDs(exe_, dg::kElecHintZone);
    g.zones().addLocal(boardZone_);
    g.zones().addLocal(hintZone_);
}

// 3083:216e PzElec_AllBulbsLit
bool PzElectric::allBulbsLit() {
    for (int i = 0; i < kSlots; ++i)
        if (piece(i).type == Bulb && !bulbLit(i)) return false;  // the used flag is not checked (keep)
    return true;
}

// 3083:2196 PzElec_IsSolved
bool PzElectric::isSolved() {
    const auto& c = st_->toolCounts;
    if (c[1] != 0 || c[2] != 0 || c[0] != 0 || !allBulbsLit()) return false;  // Y-wires and wires may stay
    bool anySwitch = false, matters = false;
    std::vector<int> listed;
    for (int i = 0; i < kSlots; ++i) {
        ElecPieceRec& p = piece(i);
        if (p.type == Battery && p.used != 0) {
            if (st_->onPath[static_cast<size_t>(i)] == 0) return false;
        } else if (p.type == Switch) {  // the used flag is not checked (keep)
            if (st_->onPath[static_cast<size_t>(i)] == 0 || p.closed == 0) return false;
            anySwitch = true;
            p.closed = 0;
            if (!allBulbsLit()) matters = true;
            p.closed = 1;
            if (listed.size() >= 5) log::debug("PzElec_IsSolved: a sixth switch (the original overwrites a local)");
            listed.push_back(i);
        }
    }
    if (listed.size() > 1 && !matters) {
        for (const int i : listed) piece(i).closed = 0;
        if (!allBulbsLit()) matters = true;
        for (const int i : listed) piece(i).closed = 1;
    }
    return anySwitch ? matters : true;
}

// 3083:2695 PzElec_Cleanup
void PzElectric::cleanup(GgGame& g) {
    puzzleFreePanel(g);
    g.zones().clearLocal();
    g.video.cursor().clearRegions();
    g.video.cursor().setCursorPicture(g.video.cursor().arrow());
    for (auto& c : labelCursors_) c.reset();
    // The cursor records (the puzzle cursor, the unused one, the held ones) and the pictures are freed
    // with the object; the active copy keeps what it shows alive.
}

CursorRecord* PzElectric::newHeldCursor(std::span<const uint8_t> picture, int hotX, int hotY) {
    auto rec = CursorRecord::fromPicture(picture);
    rec->hotX = hotX;
    rec->hotY = hotY;
    heldRecords_.push_back(std::move(rec));
    return heldRecords_.back().get();
}

void PzElectric::freeHeldCursor() { held_ = nullptr; }

// DS:a8ce is the wire count of the toolbox (g_elecToolCounts[4]); the code that increments it after a
// missed second click (3083:0ca8) and for the wires of a picked-up piece (3083:13c5) gives the wire back.
void PzElectric::wireBack() { ++st_->toolCounts[Wire]; }

// ---- starting from a DOSBox-X state (puzzles.h)

bool PzElectric::restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) {
    // 3083:090c..0917 the main loop's polls (Input_GetCode, Input_StripHeld, PzElec_UpdatePlugCursor); the rubber band of PzElec_LayWire (0b9e, 0bad, 09d5), the
    // hint text, Go Back, the success sequence and Save & Exit (0a81, 0a9e, 095d..096d, 0a0b..0a1b, 0abc).
    static constexpr uint16_t kMain[] = {0x0911, 0x0917, 0x090C};
    static constexpr uint16_t kSub[] = {0x0BA3, 0x0BB0, 0x09D8, 0x0A86, 0x0AA3, 0x0962, 0x0A10,
                                        0x096A, 0x0A18, 0x0972, 0x0A20, 0x0AC1};
    if (!stateAtMainLoop(s, {kMain, kSub, 0x0902, 0x0AC9}, why)) return false;
    game_ = &g;
    bind(g.puzzles.electric);
    const int variant = g.puzzles.variant;
    variant_ = variant;
    loadResources(variant);
    g.puzzlesObjects().panel.load(g.archive());
    // The puzzle's effect bank, as the state's RAM holds it (DS:b1b0, the slots DS:b146): without it the
    // slots keep their start-up handles and PzSnd_WaitEnd after a solve waits on the room's music.
    g.sound.pzLoadBank(kEffectBank);
    // The pieces' current pictures: a lit bulb and a closed switch point at their own copies.
    for (int i = 0; i < kSlots; ++i) {
        const ElecPieceRec& p = piece(i);
        const uint16_t picOff = static_cast<uint16_t>(dg::kElecPieces + 16 * i + 6);
        const int type = std::clamp<int>(p.type, 0, 4);
        picture_[static_cast<size_t>(i)] = tools_[static_cast<size_t>(type)].picture;
        if (stateSamePointer(s, picOff, 0xA2EC)) picture_[static_cast<size_t>(i)] = bulbLitPic_;      // DS:a2ec the lit bulb
        else if (stateSamePointer(s, picOff, 0xA2CC)) picture_[static_cast<size_t>(i)] = switchClosed_;  // DS:a2cc the closed switch
        const auto pic = picture_[static_cast<size_t>(i)];
        pieceZones_[static_cast<size_t>(i)].init(p.x, p.y, s16(pic, 6), s16(pic, 8), 0, static_cast<uint16_t>(0x4B0 + i), 0);
    }
    initToolZones(g);
    boardZone_ = zoneFromDs(exe_, dg::kElecBoardZone);
    hintZone_ = zoneFromDs(exe_, dg::kElecHintZone);
    screenRegion_ = cursorRegionAt(exe_, dg::kElecScreenRegion, cursor_.get());
    for (size_t i = 0; i < tools_.size(); ++i) {
        labelCursors_[i] = CursorRecord::fromPicture(memberOf(labels_, static_cast<int>(i)));
        toolRegions_[i] = {toolZones_[i].shape, toolZones_[i].x, toolZones_[i].y, toolZones_[i].w, toolZones_[i].h,
                           toolZones_[i].priority, labelCursors_[i].get()};
    }
    // The click zones in the state's order.
    std::vector<std::pair<uint16_t, Zone*>> zones = {{dg::kElecBoardZone, &boardZone_},
                                                     {dg::kElecHintZone, &hintZone_},
                                                     {dg::kElecToolboxZone, &toolboxZone_}};
    for (int i = 0; i < 5; ++i) zones.emplace_back(static_cast<uint16_t>(dg::kElecToolZones + 0x16 * i), &toolZones_[static_cast<size_t>(i)]);
    for (int i = 0; i < kSlots; ++i)
        zones.emplace_back(static_cast<uint16_t>(dg::kElecPieceZones + 0x16 * i), &pieceZones_[static_cast<size_t>(i)]);
    if (!relinkStateZones(g, s, zones, why)) return false;
    // The cursor regions in the state's order (the default region DS:0a54 last).
    std::vector<CursorRegion*> regions;
    for (const uint16_t off : stateCursorRegions(s)) {
        if (off == 0x0A54) break;
        if (off == dg::kElecScreenRegion) {
            regions.push_back(&screenRegion_);
            continue;
        }
        const int i = (off - dg::kElecToolRegions) / static_cast<int>(dg::kElecToolRegionsStride);
        if (off < dg::kElecToolRegions || i >= 5 || (off - dg::kElecToolRegions) % dg::kElecToolRegionsStride != 0) {
            why = std::format("a cursor region the puzzle does not know (DS:{:04x})", off);
            return false;
        }
        regions.push_back(&toolRegions_[static_cast<size_t>(i)]);
    }
    g.video.cursor().clearRegions();
    for (auto r = regions.rbegin(); r != regions.rend(); ++r) g.video.cursor().addRegion(*r);
    // The default region's cursor: the puzzle cursor DS:a8d8 or the held one DS:a8d4.
    if (stateSamePointer(s, 0x0A6C, dg::kElecCursor)) {
        restoreCursorPicture(g, s, cursor_.get());
    } else if (stateSamePointer(s, 0x0A6C, dg::kElecHeldCursor) && st_->mode >= 0 && st_->mode < 5) {
        const Tool& tool = tools_[static_cast<size_t>(st_->mode)];
        held_ = newHeldCursor(tool.picture, tool.hotX, tool.hotY);
        restoreCursorPicture(g, s, held_);
        if (st_->plugShown != 0) activeCursorCopy(g.video).picture = plug_;
    } else {
        why = "the cursor is neither the puzzle cursor nor the held one";
        return false;
    }
    return true;
}

int PzElectric::resume(GgGame& g) {
    // Solved during this visit: the panel zone DS:331c is linked only then.
    bool solved = false;
    for (const Zone* z = g.zones().local(); z; z = z->next)
        if (z == &g.puzzlesObjects().panelZone) solved = true;
    return loop(g, solved);
}

}  // namespace tmport::gg
