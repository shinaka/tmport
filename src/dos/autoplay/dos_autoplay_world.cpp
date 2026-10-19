// What the DOS autoplayer knows about the mountain (dos_autoplay_world.h).
#include "dos/autoplay/dos_autoplay_world.h"

#include <algorithm>
#include <deque>
#include <map>

#include "dos/game/game_state.h"

namespace tmport::dos::autoplay {

namespace {

LevelLoop makeLoop(int level, unsigned first, unsigned last) {
    LevelLoop l;
    l.level = level;
    for (unsigned c = first; c <= last; ++c) l.cells.push_back(static_cast<uint8_t>(c));
    return l;
}

// code_world.md §3.4: level 0 E3..FF (twins FD-FF = E3-E5), level 1 AB..C2 (C0-C2 = AB-AD), level 2
// 77..88 (86-88 = 77-79). The canonical loops start at the cell the right-mover's seam conveys to.
const std::array<LevelLoop, 3>& loops() {
    static const std::array<LevelLoop, 3> l = {makeLoop(0, 0xE5, 0xFE), makeLoop(1, 0xAD, 0xC1), makeLoop(2, 0x79, 0x87)};
    return l;
}

struct Twin {
    unsigned cell, canonical;
};
constexpr Twin kTwins[] = {{0xE3, 0xFD}, {0xE4, 0xFE}, {0xFF, 0xE5}, {0xAB, 0xC0}, {0xAC, 0xC1},
                           {0xC2, 0xAD}, {0x77, 0x86}, {0x78, 0x87}, {0x88, 0x79}};

}  // namespace

const LevelLoop& levelLoop(int level) { return loops().at(static_cast<size_t>(std::clamp(level, 0, 2))); }

std::optional<unsigned> canonicalCell(int level, unsigned cell) {
    if (level < 0 || level > 2) return std::nullopt;
    for (const Twin& t : kTwins)
        if (t.cell == cell) cell = t.canonical;
    const auto& l = levelLoop(level);
    if (std::find(l.cells.begin(), l.cells.end(), static_cast<uint8_t>(cell)) == l.cells.end()) return std::nullopt;
    return cell;
}

int loopIndex(int level, unsigned cell) {
    const auto c = canonicalCell(level, cell);
    if (!c) return -1;
    const auto& l = levelLoop(level);
    return static_cast<int>(std::find(l.cells.begin(), l.cells.end(), static_cast<uint8_t>(*c)) - l.cells.begin());
}

std::optional<int> loopPos(int level, uint16_t x, uint16_t y) {
    const int i = loopIndex(level, cellOf(x, y));
    if (i < 0) return std::nullopt;
    return i * kCellPx + (x & 0x7F);
}

int loopPosOf(int level, unsigned cell, int offset) {
    const int i = loopIndex(level, cell);
    return i < 0 ? -1 : i * kCellPx + offset;
}

int loopDelta(int level, int a, int b) {
    const int len = levelLoop(level).lengthPx();
    int d = ((b - a) % len + len) % len;
    if (d > len / 2) d -= len;
    return d;
}

int levelOfPathCell(unsigned cell) {
    for (int level = 0; level < 3; ++level)
        if (loopIndex(level, cell) >= 0) return level;
    return -1;
}

std::vector<Hazard> hazards(const GameState& s, int level) {
    std::vector<Hazard> out;
    if (level == 2) {
        // Cell_P41_V41_GeyserTop 1568:141c: walking through x & 0x78 within 0x18..0x50 slides down.
        const int p = loopPosOf(2, cells::kGeyserTop, 0);
        out.push_back({cells::kGeyserTop, p + 0x18, p + 0x57});
    }
    // Cell_P45_V45_Bridge 1568:15bc: from tier 2 a walking player in x & 0x78 within 0x30..0x48 falls
    // (only where the cell still has the bridge type: a slow CPU makes it plain path).
    const unsigned bridge = level == 1 ? cells::kBridgeL1 : level == 2 ? cells::kBridgeL2 : 0;
    if (bridge && s.core.tier >= 2 && s.world.cellType[bridge] == 45 && s.core.slowCpu == 0) {
        const int p = loopPosOf(level, bridge, 0);
        out.push_back({bridge, p + 0x30, p + 0x4F});
    }
    return out;
}

// ------------------------------------------------------------------------------- the castle

CastleGrid castleGrid(const GameState& s) {
    CastleGrid g;
    for (int r = CastleGrid::kTopRow; r <= CastleGrid::kHallRow; ++r)
        for (int c = 0; c < CastleGrid::kCols; ++c)
            g.pic[static_cast<size_t>(r - CastleGrid::kTopRow)][static_cast<size_t>(c)] =
                static_cast<uint8_t>(s.world.cellPicture[static_cast<size_t>(r * 16 + CastleGrid::kFirstCol + c)] & 0x7F);
    return g;
}

namespace {
bool fullLadder(uint8_t p) { return p == 0x31 || p == 0x27; }
bool anyLadder(uint8_t p) { return p == 0x31 || p == 0x2D || p == 0x27; }
bool landing(uint8_t p) { return p == 0x31 || p == 0x2D; }
}  // namespace

std::vector<CastleStep> castleRoute(const CastleGrid& g, int row, int col) {
    if (!CastleGrid::valid(row, col)) return {};
    struct Node {
        int row, col;
        bool operator<(const Node& o) const { return row != o.row ? row < o.row : col < o.col; }
    };
    // Dijkstra over at most 20 cells (jumps cost 2, climbing 1).
    std::map<Node, int> dist;
    std::map<Node, std::pair<Node, CastleMove>> prev;
    std::vector<std::pair<int, Node>> open = {{0, {row, col}}};
    dist[{row, col}] = 0;
    std::optional<Node> goal;
    while (!open.empty()) {
        auto best = std::min_element(open.begin(), open.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        const auto [d, n] = *best;
        open.erase(best);
        if (d != dist[n]) continue;
        const uint8_t p = g.at(n.row, n.col);
        if (p == 0x32) {
            goal = n;
            break;
        }
        auto relax = [&](Node m, int cost, CastleMove move) {
            if (!CastleGrid::valid(m.row, m.col)) return;
            auto it = dist.find(m);
            if (it != dist.end() && it->second <= d + cost) return;
            dist[m] = d + cost;
            prev[m] = {n, move};
            open.push_back({d + cost, m});
        };
        if (fullLadder(p)) {
            const uint8_t up = n.row > CastleGrid::kTopRow ? g.at(n.row - 1, n.col) : 0;
            if (up == 0x31 || up == 0x2D || up == 0x32) relax({n.row - 1, n.col}, 1, CastleMove::Up);
        }
        if (anyLadder(p) && n.row < CastleGrid::kHallRow) {
            if (n.col > CastleGrid::kFirstCol && landing(g.at(n.row, n.col - 1))) relax({n.row, n.col - 1}, 2, CastleMove::JumpLeft);
            if (n.col + 1 < CastleGrid::kFirstCol + CastleGrid::kCols && landing(g.at(n.row, n.col + 1)))
                relax({n.row, n.col + 1}, 2, CastleMove::JumpRight);
        }
    }
    if (!goal) return {};
    std::vector<CastleStep> steps = {{goal->row, goal->col, CastleMove::Exit}};
    Node n = *goal;
    while (!(n.row == row && n.col == col)) {
        const auto& [from, move] = prev.at(n);
        steps.push_back({from.row, from.col, move});
        n = from;
    }
    std::reverse(steps.begin(), steps.end());
    return steps;
}

std::vector<int> hallLadders(const CastleGrid& g) {
    std::vector<int> out;
    for (int c = CastleGrid::kFirstCol; c < CastleGrid::kFirstCol + CastleGrid::kCols; ++c)
        if (g.at(CastleGrid::kHallRow, c) == 0x27) out.push_back(c);
    return out;
}

bool inCastle(unsigned cell) {
    const unsigned row = cell >> 4, col = cell & 0xF;
    if (row >= 3 && row <= 6) return col <= 7;
    return row == 7 && col <= 6;
}

}  // namespace tmport::dos::autoplay
