#include "gg/game/pz_electric.h"

#include <algorithm>
#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/gfx/video.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kTerminals = 0xA3EC;  // DS of piece 0's terminal table (the stores' displacements)
constexpr uint16_t kGeometryTable = 0x2166, kGeometryEnd = 0x2162;
constexpr size_t kTermSize = 0x1A;
constexpr int kBoardX = 0x5B, kBoardY = 0x11, kBoardW = 0xDE, kBoardH = 0x87;

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

}  // namespace

PzElectric::PzElectric(const GgExe& exe, const Archive& archive) : exe_(exe), archive_(archive) {
    for (size_t i = 0; i < tools_.size(); ++i) {
        const auto o = static_cast<uint16_t>(dg::kElecTools + 16 * i);
        tools_[i] = {exe.dsS16(o), exe.dsS16(o + 2), exe.dsS16(o + 8), exe.dsS16(o + 0x0A), exe.dsS16(o + 0x0C),
                     exe.dsS16(o + 0x0E), {}};
    }
    // 3083:1f35: the stores of each type, from the jump table CS:2166 to 3083:2162.
    for (int type = 0; type < 4; ++type)
        geometry_[static_cast<size_t>(type)] =
            runStoreCode(exe, kCodeSeg, codeWord(exe, kCodeSeg, static_cast<uint16_t>(kGeometryTable + 2 * type)), kGeometryEnd, 0x84);
}

// 3083:256b PzElec_LoadResources
void PzElectric::loadResources(int variant) {
    toolPics_ = Bundle::load(archive_, kToolPics);  // Res_LoadCopy; pictures into the tool table
    for (size_t i = 0; i < tools_.size(); ++i) tools_[i].picture = memberOf(toolPics_, static_cast<int>(i));
    cursor_ = loadCursor(archive_, kCursor);
    cursor_->hotX = 1;
    unused_ = loadCursor(archive_, 4);  // loaded and freed, never shown
    digits_ = Bundle::load(archive_, kDigits);
    labels_ = Bundle::load(archive_, kLabels);
    plug_ = archive_.read(kPlug);
    switchClosed_ = archive_.read(kSwitchClosed);
    bulbLitPic_ = archive_.read(kBulbLit);
    dotUpper_ = archive_.read(kDotUpper);
    dotLower_ = archive_.read(kDotLower);
    panelText_ = puzzleText(archive_, static_cast<uint16_t>(kPanelTextFirst + variant));
}

// 3083:1f35 PzElec_SetTerminalGeometry
void PzElectric::setTerminalGeometry(int piece, int type) {
    Piece& p = pieces_[static_cast<size_t>(piece)];
    for (Terminal& t : p.t) t.piece = kNone;
    if (type < 0 || type > 3) {
        log::debug("PzElec_SetTerminalGeometry: type {} (the original fails an assert)", type);
        return;
    }
    for (const auto& [disp, value] : geometry_[static_cast<size_t>(type)]) {
        const int off = disp - kTerminals;
        if (off < 0 || static_cast<size_t>(off) >= 3 * kTermSize) {
            log::debug("PzElec_SetTerminalGeometry: a store outside the terminals (DS:{:04x})", disp);
            continue;
        }
        Terminal& t = p.t[static_cast<size_t>(off) / kTermSize];
        const auto v = static_cast<int16_t>(value);
        switch (static_cast<size_t>(off) % kTermSize) {
        case 0x00: t.hitX = v; break;
        case 0x02: t.hitY = v; break;
        case 0x04: t.hitW = v; break;
        case 0x06: t.hitH = v; break;
        case 0x08: t.anchorX = v; break;
        case 0x0A: t.anchorY = v; break;
        case 0x0C: t.dotX = v; break;
        case 0x0E: t.dotY = v; break;
        case 0x10: t.overlapH = v; break;
        case 0x12: t.piece = v; break;
        case 0x14: t.term = v; break;
        case 0x16: t.flag = v; break;
        default: log::debug("PzElec_SetTerminalGeometry: a store into the middle of a field"); break;
        }
    }
}

// 3083:1850 PzElec_AutoConnect
void PzElectric::autoConnect(int p) {
    for (int t = 0; t < 3; ++t) {
        if (term(p, t).piece != kNone) continue;
        for (int q = 0; q < kSlots; ++q) {
            if (!pieces_[static_cast<size_t>(q)].used || q == p) continue;
            for (int u = 0; u < 3; ++u) {
                const Terminal& tq = term(q, u);
                if (tq.piece != kNone) continue;
                const Terminal& tp = term(p, t);
                const Piece& pq = pieces_[static_cast<size_t>(q)];
                const Piece& pp = pieces_[static_cast<size_t>(p)];
                if (!spansOverlap(pq.x + tq.anchorX - 2, pq.y + tq.anchorY, 3, tq.overlapH, pp.x + tp.anchorX - 2,
                                  pp.y + tp.anchorY, 3, tp.overlapH))
                    continue;
                term(p, t).piece = q;
                term(p, t).term = u;
                term(p, t).flag = 3;
                term(q, u).piece = p;
                term(q, u).term = t;
                term(q, u).flag = 0;
            }
        }
    }
}

// 3083:1e20 PzElec_CreatePiece
void PzElectric::createPiece(int x, int y) {
    int slot = 0;
    while (slot < kSlots && pieces_[static_cast<size_t>(slot)].used) ++slot;
    if (slot == kSlots) {
        log::debug("PzElec_CreatePiece: no free slot (the original fails an assert)");
        return;
    }
    Piece& p = pieces_[static_cast<size_t>(slot)];
    p.type = mode;
    p.used = true;
    p.x = x;
    p.y = y;
    p.picture = mode >= 0 && mode < 5 ? tools_[static_cast<size_t>(mode)].picture : std::span<const uint8_t>{};
    // TODO(wave 3): the piece zone (x, y, picture size, press 0x4b0 + slot) is the runtime's.
    setTerminalGeometry(slot, mode);
    autoConnect(slot);
}

// 3083:22bf PzElec_LoadVariant
void PzElectric::loadVariant(int variant) {
    for (Piece& p : pieces_) p = Piece{};  // the terminal and piece tables cleared, used = 0
    const auto data = archive_.read(static_cast<uint16_t>(kVariantFirst + variant));  // Res_Read
    if (data.size() < 280) throw FormatError(std::format("electric: F {:04x} is {} bytes", kVariantFirst + variant, data.size()));
    auto w = [&](int i) { return static_cast<int>(s16(data, 2 * static_cast<size_t>(i))); };
    for (int i = 0; i < 5; ++i) toolCounts[static_cast<size_t>(i)] = w(i);
    for (int i = 0; i < kSlots; ++i) {
        mode = w(5 + i);
        if (mode == 5) break;
        createPiece(w(20 + i), w(35 + i));
        pieces_[static_cast<size_t>(i)].fixed = true;
    }
    for (int t = 0; t < 3; ++t)
        for (int i = 0; i < kSlots; ++i) {
            Terminal& tt = term(i, t);
            tt.piece = w(50 + 30 * t + i);
            if (tt.piece != kNone) tt.flag = 1;
        }
    for (int t = 0; t < 3; ++t)
        for (int i = 0; i < kSlots; ++i) term(i, t).term = w(65 + 30 * t + i);
    for (int i = 0; i < kSlots; ++i)
        for (int t = 0; t < 3; ++t) {
            const Terminal& tt = term(i, t);
            if (tt.piece == kNone || tt.piece < 0 || tt.piece >= kSlots || tt.term < 0 || tt.term > 2) continue;
            Terminal& other = term(tt.piece, tt.term);
            other.piece = i;
            other.term = t;
        }
    variant_ = variant;
}

// 3083:08a1 PzElectric_Run, up to the loop
void PzElectric::setup(Video& video, Fonts& fonts, int variant) {
    loadResources(variant);
    loadVariant(variant);
    screenRegion_ = cursorRegionAt(exe_, dg::kElecScreenRegion, cursor_.get());
    video.cursor().addRegion(&screenRegion_);  // MouseRegion_Add(DS:2488)
    // TODO(wave 3): PzElec_InitToolZones (the runtime's zones).
    // 3083:24aa PzElec_InitToolCursorRegions: a region per tool over its picture, cursor = label i.
    for (size_t i = 0; i < tools_.size(); ++i) {
        labelCursors_[i] = CursorRecord::fromPicture(memberOf(labels_, static_cast<int>(i)));
        toolRegions_[i] = {0, tools_[i].x, tools_[i].y, s16(tools_[i].picture, 6), s16(tools_[i].picture, 8), 0, labelCursors_[i].get()};
    }
    // 3083:250e PzElec_DrawScreen
    drawPuzzlePicture(video, archive_, puzzleKindInfo(0).picture, puzzleKindInfo(0).palette);
    PuzzlePanel panel;
    panel.init(video.screen(), fonts, archive_, panelText_);
    video.screen().saveToBack(0, 0, 320, 200);
    drawBoard(video, false);
    puzzleFadeIn(video);
    video.cursor().setCursorPicture(cursor_.get());
    drawToolbox(video);
    // 3083:1d83 PzElec_AddToolCursorRegions
    for (size_t i = 0; i < toolRegions_.size(); ++i)
        if (toolCounts[i] != 0) video.cursor().addRegion(&toolRegions_[i]);
    mode = kNone;
    // TODO(wave 3): PzSnd_LoadBank(0x11cc) (audio), Hint_Show(0x1052) (UI).
}

// 3083:1c99 PzElec_DrawBoard
void PzElectric::drawBoard(Video& video, bool flush) {
    Screen& screen = video.screen();
    screen.restoreRect(kBoardX, kBoardY, kBoardW, kBoardH);
    for (int i = 0; i < kSlots; ++i) {
        Piece& p = pieces_[static_cast<size_t>(i)];
        if (!p.used) continue;
        if (p.type == Bulb) {
            // 3083:0844 PzElec_SetBulbPicture (the lit sound is the audio area's)
            p.picture = bulbLit(i) ? std::span<const uint8_t>(bulbLitPic_) : tools_[Bulb].picture;
        }
        screen.lpxDrawKeyed(p.x, p.y, p.picture, 0);
    }
    drawWires(screen);
    if (flush) video.flush();
}

// 3083:0fcf PzElec_DrawWires
void PzElectric::drawWires(Screen& screen) const {
    screen.setDrawColor(1);
    for (int i = 0; i < kSlots; ++i) {
        const Piece& p = pieces_[static_cast<size_t>(i)];
        if (!p.used) continue;
        for (int t = 0; t < 3; ++t) {
            const Terminal& tt = p.t[static_cast<size_t>(t)];
            if (tt.piece == kNone || tt.flag == 0 || tt.piece < 0 || tt.piece >= kSlots || tt.term < 0 || tt.term > 2) continue;
            const Piece& q = pieces_[static_cast<size_t>(tt.piece)];
            const Terminal& tq = q.t[static_cast<size_t>(tt.term)];
            if (tt.flag == 1) drawWireL(screen, p.x + tt.anchorX, p.y + tt.anchorY, q.x + tq.anchorX, q.y + tq.anchorY);
            drawTerminalDot(screen, tt.piece, tt.term);
            drawTerminalDot(screen, i, t);
        }
    }
}

// 3083:10ac PzElec_DrawWireL
void PzElectric::drawWireL(Screen& screen, int x1, int y1, int x2, int y2) const {
    if (x1 < x2) {
        for (int d = -1; d <= 1; ++d) screen.hLine(x1, y1 + d, x2 - x1 + 1);
    } else if (x1 > x2) {
        for (int d = -1; d <= 1; ++d) screen.hLine(x2, y1 + d, x1 - x2);
    }
    if (y1 < y2) {
        for (int d = -1; d <= 1; ++d) screen.vLine(x2 + d, y1, y2 - y1);
    } else if (y1 > y2) {
        for (int d = -1; d <= 1; ++d) screen.vLine(x2 + d, y2, y1 - y2);
    }
}

// 3083:12cc PzElec_DrawTerminalDot
void PzElectric::drawTerminalDot(Screen& screen, int p, int t) const {
    const Piece& pc = pieces_[static_cast<size_t>(p)];
    const Terminal& tt = pc.t[static_cast<size_t>(t)];
    screen.lpxDrawKeyed(pc.x + tt.dotX, pc.y + tt.dotY, tt.dotY > 5 ? dotLower_ : dotUpper_, 0);
}

// 3083:163c PzElec_DrawToolbox
void PzElectric::drawToolbox(Video& video) const {
    Screen& screen = video.screen();
    screen.restoreRect(7, 0x43, 0x4E, 0x5E);
    for (size_t i = 0; i < tools_.size(); ++i) {
        const int n = toolCounts[i];
        if (n <= 0) continue;
        screen.lpxDrawKeyed(tools_[i].x, tools_[i].y, tools_[i].picture, 0);
        screen.lpxDrawKeyed(tools_[i].digitX, tools_[i].digitY, memberOf(digits_, n % 10), 0);
        if (n / 10 != 0) screen.lpxDrawKeyed(tools_[i].digitX - 2, tools_[i].digitY, memberOf(digits_, n / 10), 0);
    }
    video.flush();
}

// The path search of 3083:0129 / 3083:04c7 (code_puzzles_a.md §3.8)
std::vector<std::vector<int>> PzElectric::findPaths(int s) {
    std::vector<std::vector<int>> rows;
    auto link = [&](int c, int t) -> const Terminal& { return term(c, t); };
    auto onStack = [](const std::vector<int>& comps, int depth, int c) {
        for (int k = 0; k < depth; ++k)
            if (comps[static_cast<size_t>(k)] == c) return true;
        return false;
    };
    bool done = false;
    for (int restart = 0; !done; ++restart) {
        if (restart > 1000) {
            log::debug("PzElec path search: no end (piece {})", s);
            break;
        }
        std::vector<int> comps(21, -1), terms(21, -1);
        int cur = link(s, 0).piece, t = link(s, 0).term;
        comps[0] = cur;
        terms[0] = t;
        int depth = 1;
        bool found = false;
        for (int steps = 0; !found; ++steps) {
            if (steps > 100000) {
                log::debug("PzElec path search: a walk without end (piece {})", s);
                return rows;
            }
            bool again;
            do {
                again = false;
                // PzElec_SkipExit 3083:002b
                for (int tries = 0;; ++tries) {
                    t = (t + 1) % 3;
                    const int c = link(cur, t).piece;
                    bool skip = c == kNone;
                    if (!skip && onStack(comps, depth, c)) skip = !(depth > 0 && terms[static_cast<size_t>(depth - 1)] == t);
                    if (!skip) break;
                    if (tries > 3) {
                        log::debug("PzElec path search: a piece without exits (piece {})", cur);
                        return rows;
                    }
                }
                if (link(cur, t).piece == s) {
                    if (link(cur, t).term == 0) {
                        found = done = true;
                    } else {
                        bool known = false;  // PzElec_PathKnown 3083:007c: a prefix compare
                        for (const auto& row : rows) {
                            int k = 0;
                            while (k < depth && k < static_cast<int>(row.size()) &&
                                   row[static_cast<size_t>(k)] == comps[static_cast<size_t>(k)])
                                ++k;
                            if (k >= depth) known = true;
                        }
                        if (!known) {
                            if (static_cast<int>(rows.size()) >= kMaxPaths) {
                                pathOverflow = true;  // an 11th row would overwrite DS:a2c8
                                log::debug("PzElec path search: more than {} paths (piece {})", kMaxPaths, s);
                            } else {
                                std::vector<int> row(comps.begin(), comps.begin() + depth);  // PzElec_PathAdd
                                row.push_back(s);
                                rows.push_back(std::move(row));
                            }
                            found = true;
                        } else {
                            again = true;
                        }
                    }
                }
            } while (again);
            // The move is made also when a path was found (no effect: the stacks are rebuilt).
            const Terminal& next = link(cur, t);
            if (depth > 0 && comps[static_cast<size_t>(depth - 1)] == cur && terms[static_cast<size_t>(depth - 1)] == t) {
                cur = next.piece;  // backtrack
                t = next.term;
                --depth;
                comps[static_cast<size_t>(depth)] = 0;
                terms[static_cast<size_t>(depth)] = 0;
            } else {
                cur = next.piece;  // advance
                t = next.term;
                if (!onStack(comps, depth, cur) && depth < 20) {
                    comps[static_cast<size_t>(depth)] = cur;
                    terms[static_cast<size_t>(depth)] = t;
                    ++depth;
                }
            }
            if (cur < 0 || cur >= kSlots) {
                log::debug("PzElec path search: a link to piece {} (piece {})", cur, s);
                return rows;
            }
        }
    }
    return rows;
}

// 3083:0129 PzElec_BatteryShorted
bool PzElectric::batteryShorted(int s) {
    if (term(s, 0).piece == kNone || term(s, 1).piece == kNone) return false;
    const auto rows = findPaths(s);
    bool shortedFlag = false;
    for (const auto& row : rows) {
        bool load = false, open = false, other = false;
        int batteries = 0;
        for (size_t k = 0; k + 1 < row.size(); ++k) {
            const Piece& c = pieces_[static_cast<size_t>(row[k])];
            if (c.type == Bulb) load = true;
            else if (c.type == Battery) {
                load = true;
                ++batteries;
            } else if (c.type == Switch && !c.closed) open = true;
            if (c.type != YWire && c.type != Battery) other = true;
        }
        if (!load && !open) shortedFlag = true;
        if (!other && batteries > 0) {
            bool changed = false;
            int prev = 0;
            for (size_t k = 0; k + 1 < row.size(); ++k) {
                const int c = row[k];
                if (pieces_[static_cast<size_t>(c)].type != Battery) continue;
                const int before = k == 0 ? s : row[k - 1];
                const int v = term(c, 0).piece == before ? 1 : 0;
                if (prev != v) changed = true;
                prev = v;
            }
            if (!changed) shortedFlag = true;
        }
    }
    return shortedFlag;
}

// 3083:0492 PzElec_CheckBatteries
void PzElectric::checkBatteries() {
    for (int i = 0; i < kSlots; ++i)
        if (pieces_[static_cast<size_t>(i)].used && pieces_[static_cast<size_t>(i)].type == Battery)
            shorted[static_cast<size_t>(i)] = batteryShorted(i) ? 1 : 0;
}

// 3083:04c7 PzElec_BulbLit
bool PzElectric::bulbLit(int s) {
    if (term(s, 0).piece == kNone || term(s, 1).piece == kNone) return false;
    checkBatteries();
    const auto rows = findPaths(s);
    onPath.fill(0);
    onPath[static_cast<size_t>(s)] = 1;
    bool bad = false, lit = false, first = false;
    int ref = 0;
    for (const auto& row : rows) {
        bool bat = false, bulb = false, open = false;
        for (size_t k = 0; k + 1 < row.size(); ++k) {
            const int c = row[k];
            const Piece& p = pieces_[static_cast<size_t>(c)];
            onPath[static_cast<size_t>(c)] = 1;
            if (p.type == Battery && !shorted[static_cast<size_t>(c)]) {
                bat = true;
                const int before = k == 0 ? s : row[k - 1];
                const int v = term(c, 0).piece != before ? 1 : 0;  // entered through terminal 1 or 2
                if (!first) {
                    first = true;
                    ref = v;
                } else if (v != ref) {
                    bad = true;
                }
            } else if (p.type == Switch && !p.closed) {
                open = true;
            } else if (p.type == Bulb) {
                bulb = true;
            }
        }
        if (open) continue;
        if (bat) lit = true;
        else if (!bulb) bad = true;
    }
    return lit && !bad;
}

int PzElectric::usedPieces() const {
    return static_cast<int>(std::count_if(pieces_.begin(), pieces_.end(), [](const Piece& p) { return p.used; }));
}

std::string PzElectric::describe() const {
    return std::format("electric circuits variant {}: {} pieces on the board, toolbox {}/{}/{}/{}/{}", variant_, usedPieces(),
                       toolCounts[0], toolCounts[1], toolCounts[2], toolCounts[3], toolCounts[4]);
}

}  // namespace tmport::gg
