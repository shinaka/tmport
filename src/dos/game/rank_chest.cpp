// The rank chest screen of main.c (130f): the chest fills one row per 4 game ticks (polled per
// frame), coin rows mark the ranks, and the CHAMPION banner flashes, counted in frames
// (code_player.md §7.4, code_runtime.md §3.13-3.14).
#include "dos/game/rank_chest.h"

#include "dos/game/game.h"
#include "dos/game/hud.h"
#include "dos/game/sound.h"
#include "dos/game/ui.h"
#include "dos/gfx/blitter.h"
#include "dos/gfx/pak.h"

namespace tmport::dos {

namespace {

constexpr int kStarFrame = 0xFA;  // SHACK sprite 125, a small coin star
constexpr uint16_t kChampionFrames = 800;
constexpr uint16_t kChampionPhase = 160;  // invisible at 0, visible at 80 (mod 160)

// The coin row drawn when the fill reaches a rank's row count: row r has r + 1 stars 6 bytes apart,
// starting at (x, y).
struct StarRow {
    int x, y;
};
constexpr StarRow kStarRows[6] = {{0x51, 0x67}, {0x4E, 0x5F}, {0x4B, 0x57}, {0x48, 0x4F}, {0x45, 0x47}, {0x42, 0x3F}};

// Word i of g_rankTable DS:0110 (10 (threshold, rows) pairs and a -1). Index 21 lies past the -1
// (the first bytes of the CHAMPION text), which a total of 9999 or more would read.
uint16_t rankWord(const DosGame& g, size_t i) {
    const auto& t = g.tables.player.rankTable;
    return i < t.size() ? static_cast<uint16_t>(t[i]) : g.exe().ds16(static_cast<uint16_t>(dg::kRankTable + 2 * i));
}

}  // namespace

// 130f:0edf RankChest_Update
void rankChestUpdate(DosGame& g) {
    WorldState& w = g.world;
    PlayerState& p = g.player;
    // DS:9100, the 60-tick "second" byte of the tick counters: one fill step whenever it enters a
    // new group of 4 (16 steps per 60 ticks), at most one per frame.
    const uint16_t phase = static_cast<uint16_t>(g.counters().secondCountdown & 0xFC);
    if (w.frameRankState == 1) {  // the first frame (g_rankState latched by Frame_Begin)
        g.ui.textInk = 0xFFFF;
        if (g.hasRec()) textDraw(g, 0x44, 0x1D, g.dsString(static_cast<uint16_t>(kUsersRecordBase + kUserRecordSize * g.ui.userRec)));
        const uint16_t total = g.hasRec() ? static_cast<uint16_t>(g.rec().totalTreasures) : 0;
        hudDrawNumber(g, 0x6C, 0x77, 0xFFFF, total);
        // The rows of the NEXT pair after the first threshold above the total (unsigned).
        size_t i = 0;
        while (rankWord(g, i) != 0xFFFF && rankWord(g, i) < static_cast<uint16_t>(total + 1)) i += 2;
        p.rankRowsWanted = rankWord(g, i + 3);
        p.rankTickPhase = phase;
        p.rankRowsDone = 0;
        ++p.rankState;  // 2
    } else if (w.frameRankState != 0 && phase != p.rankTickPhase) {  // one step
        p.rankTickPhase = phase;
        const int n = p.rankRowsDone;
        const int y = 0x6F - n;
        g.back.replaceColor(0x1A, y, 0x98, y + 1, 0x7777, 0x6666);  // colour 7 -> 6: the band fills
        // The coin rows at the row counts of the ranks: words 3, 5, ..., 13 of the table (8 .. 48).
        for (int r = 0; r < 6; ++r) {
            if (n != rankWord(g, static_cast<size_t>(3 + 2 * r))) continue;
            for (int k = 0; k <= r; ++k) rankChestDrawStar(g, kStarRows[r].x + 6 * k, kStarRows[r].y, kStarFrame);
            break;
        }
        sfxPlay(g, 3);
        ++g.core.dirty;
        p.rankRowsDone = static_cast<uint16_t>(n + 1);
        if (p.rankRowsDone == p.rankRowsWanted) {
            p.rankState = 0;
            w.frameRankState = 0;
            // 66 or 86 rows (a total of 300 or more): the champion banner (word 15 = 56, unsigned).
            if (static_cast<uint16_t>(n) >= rankWord(g, 15)) p.champFlashTimer = kChampionFrames;
        }
    }
    // The CHAMPION banner, every frame on any screen while the timer runs: drawn in the band's
    // colour 6 (invisible) at 800, 640, ... and in black at 720, 560, ..., 80, so it appears five
    // times and ends visible (kept, PORTING_NOTES §6). It counts frames: g.frameClock() gives the
    // DOSBox-X rate (~914 steps/s) or one per original frame.
    const int steps = g.frameClock().steps(FrameFeature::ChampionFlash);
    for (int s = 0; s < steps && p.champFlashTimer != 0; ++s) {
        if (p.champFlashTimer % kChampionPhase == 0) {
            g.ui.textInk = 0x6666;
            textDraw(g, 0x40, 0x2E, g.dsString(dg::kStrChampion));
        } else if (p.champFlashTimer % kChampionPhase == kChampionPhase / 2) {
            g.ui.textInk = 0xFFFF;
            textDraw(g, 0x40, 0x2E, g.dsString(dg::kStrChampion));
        }
        p.champFlashTimer = static_cast<uint16_t>(p.champFlashTimer - (g.core.slowCpu == 0 ? 1 : 4));
        ++g.core.dirty;
    }
}

// 130f:1261 RankChest_DrawStar
void rankChestDrawStar(DosGame& g, int x, int y, int frame2) {
    if (!g.bank || (frame2 >> 1) >= g.bank->count()) return;
    drawSprite(g.back, g.bank->frame(frame2 >> 1), x, y, sprite_flags::kMasked);
}

}  // namespace tmport::dos
