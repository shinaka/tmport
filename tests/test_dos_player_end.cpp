// The end of a game in the DOS edition (src/dos/game/player.cpp states 41-71, endgame.cpp,
// search.cpp, rank_chest.cpp): the end sequence and the champion branch compared tick for tick with
// the frame logger of the reference recordings (r2/fl_end_t2_wait, r2/fl_crown_wait), the chute,
// the treasure room, the rank chest fill and the CHAMPION flash, the search schedule, the showcase
// choice and hand-over, the carousel and the pair displays. Only numbers are compared; the last test
// compares the engine's pictures with the reference captures, which it reads from TM_DOS_CAPTURES.
#include <doctest/doctest.h>

#include <array>
#include <initializer_list>
#include <iterator>

#include "dos/game/cells.h"
#include "dos/game/load_state.h"
#include "dos/gfx/blitter.h"
#include "dos/gfx/pak.h"
#include "dos/gfx/palettes.h"
#include "dos/save/dosbox_state.h"
#include "dos_test_data.h"
#include "mods/png_codec.h"
#include "test_dos_player_harness.h"
#include "util/file_util.h"

using namespace tmport;
using namespace tmport::dos;
using namespace dosplayer;

namespace {

// ---- reference logs (extracted/scratch/dos_w3_player/gen_expect.py; keys not compared) --------

// kEndT2: fl_end_t2_wait.sav records 9..1047 (state 45 to the last step down the chute); the log
// continues with the castle gate at 1049 after a gated tick (1048), state 1 at 1057, the rank
// screen at 1143.
constexpr LogPoint kEndT2[] = {
    {9, 0x400, 0x500, 0x400, 0x500, 45, 0xffff, 31}, {130, 0x400, 0x500, 0x400, 0x500, 46, 0xffff, 31},
    {159, 0x400, 0x500, 0x400, 0x500, 47, 0xffff, 31}, {167, 0x400, 0x500, 0x400, 0x500, 48, 0xffff, 31},
    {175, 0x400, 0x500, 0x400, 0x500, 49, 0xffff, 31}, {216, 0x400, 0x500, 0x400, 0x500, 50, 0xffff, 31},
    {242, 0x400, 0x500, 0x400, 0x500, 51, 0xffff, 31}, {323, 0x400, 0x500, 0x400, 0x500, 49, 0xffff, 31},
    {337, 0x400, 0x500, 0x400, 0x500, 50, 0xffff, 31}, {363, 0x400, 0x500, 0x400, 0x500, 51, 0xffff, 31},
    {444, 0x400, 0x500, 0x400, 0x500, 49, 0xffff, 31}, {445, 0x400, 0x500, 0x400, 0x500, 52, 0xffff, 31},
    {474, 0x400, 0x500, 0x400, 0x500, 53, 0xffff, 31}, {475, 0x400, 0x500, 0x400, 0x500, 55, 0xffff, 31},
    {476, 0x400, 0x500, 0x400, 0x500, 56, 0xffff, 31}, {491, 0x400, 0x500, 0x400, 0x500, 57, 0xffff, 31},
    {555, 0x400, 0x500, 0x400, 0x500, 58, 0xffff, 31}, {605, 0x400, 0x500, 0x400, 0x500, 59, 0xffff, 31},
    {606, 0x400, 0x500, 0x400, 0x500, 60, 0xffff, 31}, {657, 0x400, 0x500, 0x400, 0x500, 61, 0xffff, 31},
    {700, 0x6a4, 0x1f0, 0x600, 0x180, 34, 0xffff, 31}, {707, 0x6a4, 0x1f8, 0x600, 0x188, 34, 0xffff, 31},
    {714, 0x6a4, 0x200, 0x600, 0x190, 34, 0xffff, 31}, {721, 0x6a4, 0x208, 0x600, 0x198, 34, 0xffff, 31},
    {728, 0x6a4, 0x210, 0x600, 0x1a0, 34, 0xffff, 31}, {736, 0x6a4, 0x218, 0x600, 0x1a8, 34, 0xffff, 31},
    {743, 0x6a4, 0x220, 0x600, 0x1b0, 34, 0xffff, 31}, {750, 0x6a4, 0x228, 0x600, 0x1b8, 34, 0xffff, 31},
    {757, 0x6a4, 0x230, 0x600, 0x1c0, 34, 0xffff, 31}, {765, 0x6a4, 0x238, 0x600, 0x1c8, 34, 0xffff, 31},
    {772, 0x6a4, 0x240, 0x600, 0x1d0, 34, 0xffff, 31}, {779, 0x6a4, 0x248, 0x600, 0x1d8, 34, 0xffff, 31},
    {786, 0x6a4, 0x250, 0x600, 0x1e0, 34, 0xffff, 31}, {794, 0x6a4, 0x258, 0x600, 0x1e8, 34, 0xffff, 31},
    {801, 0x6a4, 0x260, 0x600, 0x1f0, 34, 0xffff, 31}, {808, 0x6a4, 0x268, 0x600, 0x1f8, 34, 0xffff, 31},
    {815, 0x6a4, 0x270, 0x600, 0x200, 34, 0xffff, 31}, {823, 0x6a4, 0x278, 0x600, 0x208, 34, 0xffff, 31},
    {830, 0x6a4, 0x280, 0x600, 0x210, 34, 0xffff, 31}, {837, 0x6a4, 0x288, 0x600, 0x218, 34, 0xffff, 31},
    {844, 0x6a4, 0x290, 0x600, 0x220, 34, 0xffff, 31}, {852, 0x6a4, 0x298, 0x600, 0x228, 34, 0xffff, 31},
    {859, 0x6a4, 0x2a0, 0x600, 0x230, 34, 0xffff, 31}, {866, 0x6a4, 0x2a8, 0x600, 0x238, 34, 0xffff, 31},
    {873, 0x6a4, 0x2b0, 0x600, 0x240, 34, 0xffff, 31}, {881, 0x6a4, 0x2b8, 0x600, 0x248, 34, 0xffff, 31},
    {888, 0x6a4, 0x2c0, 0x600, 0x250, 34, 0xffff, 31}, {895, 0x6a4, 0x2c8, 0x600, 0x258, 34, 0xffff, 31},
    {902, 0x6a4, 0x2d0, 0x600, 0x260, 34, 0xffff, 31}, {910, 0x6a4, 0x2d8, 0x600, 0x268, 34, 0xffff, 31},
    {917, 0x6a4, 0x2e0, 0x600, 0x270, 34, 0xffff, 31}, {924, 0x6a4, 0x2e8, 0x600, 0x278, 34, 0xffff, 31},
    {931, 0x6a4, 0x2f0, 0x600, 0x280, 34, 0xffff, 31}, {939, 0x6a4, 0x2f8, 0x600, 0x288, 34, 0xffff, 31},
    {946, 0x6a4, 0x300, 0x600, 0x290, 34, 0xffff, 31}, {953, 0x6a4, 0x308, 0x600, 0x298, 34, 0xffff, 31},
    {960, 0x6a4, 0x310, 0x600, 0x2a0, 34, 0xffff, 31}, {968, 0x6a4, 0x318, 0x600, 0x2a8, 34, 0xffff, 31},
    {975, 0x6a4, 0x320, 0x600, 0x2b0, 34, 0xffff, 31}, {982, 0x6a4, 0x328, 0x600, 0x2b8, 34, 0xffff, 31},
    {989, 0x6a4, 0x330, 0x600, 0x2c0, 34, 0xffff, 31}, {997, 0x6a4, 0x338, 0x600, 0x2c8, 34, 0xffff, 31},
    {1004, 0x6a4, 0x340, 0x600, 0x2d0, 34, 0xffff, 31}, {1011, 0x6a4, 0x348, 0x600, 0x2d8, 34, 0xffff, 31},
    {1018, 0x6a4, 0x350, 0x600, 0x2e0, 34, 0xffff, 31}, {1026, 0x6a4, 0x358, 0x600, 0x2e8, 34, 0xffff, 31},
    {1033, 0x6a4, 0x360, 0x600, 0x2f0, 34, 0xffff, 31}, {1040, 0x6a4, 0x368, 0x600, 0x2f8, 34, 0xffff, 31},
    {1047, 0x6a4, 0x370, 0x600, 0x300, 34, 0xffff, 31},
};
// kCrown: fl_crown_wait.sav records 99..1216 (after the 4th of 6 treasure flights, total 294 + 6:
// the champion branch, to the last step down the chute); the log continues with the clubhouse
// (0x170, 0x70), camera (0xCC, 0), state 9 at 1223 after six ticks skipped by the scene load.
constexpr LogPoint kCrown[] = {
    {99, 0x400, 0x500, 0x400, 0x500, 49, 0xffff, 31}, {110, 0x400, 0x500, 0x400, 0x500, 50, 0xffff, 31},
    {136, 0x400, 0x500, 0x400, 0x500, 51, 0xffff, 31}, {217, 0x400, 0x500, 0x400, 0x500, 49, 0xffff, 31},
    {231, 0x400, 0x500, 0x400, 0x500, 50, 0xffff, 31}, {257, 0x400, 0x500, 0x400, 0x500, 51, 0xffff, 31},
    {338, 0x400, 0x500, 0x400, 0x500, 49, 0xffff, 31}, {339, 0x400, 0x500, 0x400, 0x500, 52, 0xffff, 31},
    {368, 0x400, 0x500, 0x400, 0x500, 63, 0xffff, 31}, {369, 0x400, 0x500, 0x400, 0x500, 64, 0xffff, 31},
    {426, 0x400, 0x500, 0x400, 0x500, 65, 0xffff, 31}, {448, 0x400, 0x500, 0x400, 0x500, 66, 0xffff, 31},
    {566, 0x400, 0x500, 0x400, 0x500, 67, 0xffff, 31}, {576, 0x400, 0x500, 0x400, 0x500, 68, 0xffff, 31},
    {665, 0x400, 0x500, 0x400, 0x500, 69, 0xffff, 31}, {701, 0x400, 0x500, 0x400, 0x500, 70, 0xffff, 31},
    {716, 0x400, 0x500, 0x400, 0x500, 71, 0xffff, 31}, {737, 0x400, 0x500, 0x400, 0x500, 70, 0xffff, 31},
    {738, 0x400, 0x500, 0x400, 0x500, 71, 0xffff, 31}, {759, 0x400, 0x500, 0x400, 0x500, 70, 0xffff, 31},
    {760, 0x400, 0x500, 0x400, 0x500, 71, 0xffff, 31}, {781, 0x400, 0x500, 0x400, 0x500, 70, 0xffff, 31},
    {782, 0x400, 0x500, 0x400, 0x500, 71, 0xffff, 31}, {803, 0x400, 0x500, 0x400, 0x500, 70, 0xffff, 31},
    {804, 0x400, 0x500, 0x400, 0x500, 71, 0xffff, 31}, {825, 0x400, 0x500, 0x400, 0x500, 70, 0xffff, 31},
    {826, 0x400, 0x500, 0x400, 0x500, 61, 0xffff, 31}, {869, 0x6a4, 0x1f0, 0x600, 0x180, 34, 0xffff, 31},
    {876, 0x6a4, 0x1f8, 0x600, 0x188, 34, 0xffff, 31}, {883, 0x6a4, 0x200, 0x600, 0x190, 34, 0xffff, 31},
    {890, 0x6a4, 0x208, 0x600, 0x198, 34, 0xffff, 31}, {897, 0x6a4, 0x210, 0x600, 0x1a0, 34, 0xffff, 31},
    {905, 0x6a4, 0x218, 0x600, 0x1a8, 34, 0xffff, 31}, {912, 0x6a4, 0x220, 0x600, 0x1b0, 34, 0xffff, 31},
    {919, 0x6a4, 0x228, 0x600, 0x1b8, 34, 0xffff, 31}, {926, 0x6a4, 0x230, 0x600, 0x1c0, 34, 0xffff, 31},
    {934, 0x6a4, 0x238, 0x600, 0x1c8, 34, 0xffff, 31}, {941, 0x6a4, 0x240, 0x600, 0x1d0, 34, 0xffff, 31},
    {948, 0x6a4, 0x248, 0x600, 0x1d8, 34, 0xffff, 31}, {955, 0x6a4, 0x250, 0x600, 0x1e0, 34, 0xffff, 31},
    {963, 0x6a4, 0x258, 0x600, 0x1e8, 34, 0xffff, 31}, {970, 0x6a4, 0x260, 0x600, 0x1f0, 34, 0xffff, 31},
    {977, 0x6a4, 0x268, 0x600, 0x1f8, 34, 0xffff, 31}, {984, 0x6a4, 0x270, 0x600, 0x200, 34, 0xffff, 31},
    {992, 0x6a4, 0x278, 0x600, 0x208, 34, 0xffff, 31}, {999, 0x6a4, 0x280, 0x600, 0x210, 34, 0xffff, 31},
    {1006, 0x6a4, 0x288, 0x600, 0x218, 34, 0xffff, 31}, {1013, 0x6a4, 0x290, 0x600, 0x220, 34, 0xffff, 31},
    {1021, 0x6a4, 0x298, 0x600, 0x228, 34, 0xffff, 31}, {1028, 0x6a4, 0x2a0, 0x600, 0x230, 34, 0xffff, 31},
    {1035, 0x6a4, 0x2a8, 0x600, 0x238, 34, 0xffff, 31}, {1042, 0x6a4, 0x2b0, 0x600, 0x240, 34, 0xffff, 31},
    {1050, 0x6a4, 0x2b8, 0x600, 0x248, 34, 0xffff, 31}, {1057, 0x6a4, 0x2c0, 0x600, 0x250, 34, 0xffff, 31},
    {1064, 0x6a4, 0x2c8, 0x600, 0x258, 34, 0xffff, 31}, {1071, 0x6a4, 0x2d0, 0x600, 0x260, 34, 0xffff, 31},
    {1079, 0x6a4, 0x2d8, 0x600, 0x268, 34, 0xffff, 31}, {1086, 0x6a4, 0x2e0, 0x600, 0x270, 34, 0xffff, 31},
    {1093, 0x6a4, 0x2e8, 0x600, 0x278, 34, 0xffff, 31}, {1100, 0x6a4, 0x2f0, 0x600, 0x280, 34, 0xffff, 31},
    {1108, 0x6a4, 0x2f8, 0x600, 0x288, 34, 0xffff, 31}, {1115, 0x6a4, 0x300, 0x600, 0x290, 34, 0xffff, 31},
    {1122, 0x6a4, 0x308, 0x600, 0x298, 34, 0xffff, 31}, {1129, 0x6a4, 0x310, 0x600, 0x2a0, 34, 0xffff, 31},
    {1137, 0x6a4, 0x318, 0x600, 0x2a8, 34, 0xffff, 31}, {1144, 0x6a4, 0x320, 0x600, 0x2b0, 34, 0xffff, 31},
    {1151, 0x6a4, 0x328, 0x600, 0x2b8, 34, 0xffff, 31}, {1158, 0x6a4, 0x330, 0x600, 0x2c0, 34, 0xffff, 31},
    {1166, 0x6a4, 0x338, 0x600, 0x2c8, 34, 0xffff, 31}, {1173, 0x6a4, 0x340, 0x600, 0x2d0, 34, 0xffff, 31},
    {1180, 0x6a4, 0x348, 0x600, 0x2d8, 34, 0xffff, 31}, {1187, 0x6a4, 0x350, 0x600, 0x2e0, 34, 0xffff, 31},
    {1195, 0x6a4, 0x358, 0x600, 0x2e8, 34, 0xffff, 31}, {1202, 0x6a4, 0x360, 0x600, 0x2f0, 34, 0xffff, 31},
    {1209, 0x6a4, 0x368, 0x600, 0x2f8, 34, 0xffff, 31}, {1216, 0x6a4, 0x370, 0x600, 0x300, 34, 0xffff, 31},
};

struct Hiding {
    int slot;
    uint16_t content;
};

// The 48 hiding places: empty (0x80) except the listed slots.
void setHiding(DosGame& g, std::initializer_list<Hiding> contents) {
    for (int i = 0; i < 48; ++i) g.world.cellContent[hidingCellAt(g, g.tables, i)] = 0x80;
    for (const Hiding& c : contents) g.world.cellContent[hidingCellAt(g, g.tables, c.slot)] = c.content;
}

// The castle-top trigger of Cell_P63_V63_CastleTop 1568:1aa4 (world, code_player.md §6.2) that
// starts the end sequence: the game state after that tick's Player_Move (level 3 loaded).
void castleTopState(DosGame& g, int16_t totalBefore, uint16_t found) {
    g.rec().totalTreasures = totalBefore;
    g.core.treasuresFound = found;
    g.core.tier = 0;
    g.core.coins = 31;
    g.player.shownTreasure = -1;  // parked by the clubhouse door
    Obj& o = g.player.obj;
    o.dy = 0;
    g.core.keyState = 0;
    g.core.level = 3;
    g.world.loadedScene = 3;
    g.world.sceneWanted = 3;
    g.core.area = 0;
    g.core.slowCpu = g.world.slowCpuSaved;
    g.world.slowScroll = g.world.slowCpuSaved;
    g.world.transition = 2;
    o.x = 0x400;
    o.y = 0x500;
    g.world.cameraX = 0x400;
    g.world.cameraY = 0x500;
    g.world.fullRedrawReq = 1;
    g.core.menuLock = 1;
    g.player.playerState = 41;
    g.player.playerMask = 0;
    animClear(g, 1);
    endSeqSetup(g);
    animSet(g, 6, 0x78);
    animSet(g, 7, 0x7E);
    animSet(g, 8, 0x7C);
    animSet(g, 10, 0x8E);
    animSet(g, 11, 0x8F);
    animSet(g, 4, 0x90);
    g.rec().totalTreasures = static_cast<int16_t>(g.rec().totalTreasures + g.core.treasuresFound);
    treasurePickShowcase(g);
}
void castleTop(Harness& h, int16_t totalBefore, uint16_t found) {
    h.setScene(3);
    castleTopState(h.g(), totalBefore, found);
}

// A script's length in ticks when started with Anim_Set: 1 + entries * (delay + 1) (§3.2).
int scriptTicks(const DosGame& g, int anim) {
    const int delay = animScriptWord(g, g.tables, anim, 0), n = animScriptWord(g, g.tables, anim, 1);
    int k = 2, entries = 0;
    for (;;) {
        const int16_t w = animScriptWord(g, g.tables, anim, k);
        if (w == -10) break;
        if (w == -2 || w == -3 || w == -4 || w == -5) k += 2;
        else if (w == -6) k += 1;
        else if (w == -7) k += 3;
        k += n;
        ++entries;
    }
    return 1 + entries * (delay + 1);
}

// The first engine tick from `from` whose record has `state`.
uint64_t firstState(const Harness& h, uint16_t state, uint64_t from = 1) {
    for (uint64_t e = from; e <= h.recs().size(); ++e)
        if (h.at(e).state == state) return e;
    return 0;
}

int countCalls(const RecordingSound& s, RecordingSound::Op op, int arg) {
    int n = 0;
    for (const auto& c : s.calls()) n += (c.op == op && c.arg == arg) ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("DOS player: end sequence states 41-61 with two treasures, then the chute, tick for tick (r2/fl_end_t2_wait)") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    // The recorded game: treasures found in hiding slots 33 (0x10) and 47 (0x4A), the three keys
    // found, four treasures not found; total 0 before.
    setHiding(g, {{2, 0x58}, {8, 0xEA}, {10, 0x4E}, {19, 0x48}, {20, 0xE8}, {22, 0x64}, {32, 0xE6}, {33, 0x90}, {47, 0xCA}});
    castleTop(h, 0, 2);
    h.sound().clear();
    CHECK(g.rec().totalTreasures == 2);
    CHECK(g.player.showcasePick == 0x90);  // the first treasure of this game, bit 7: new
    CHECK(g.player.slots[6].z == 10);
    CHECK(g.player.slots[11].x == 0x18);

    // The script lengths the states wait for (code_player.md §6.3).
    CHECK(scriptTicks(g, 0x78) == 62);
    CHECK(scriptTicks(g, 0x80) == 23);
    CHECK(scriptTicks(g, 0x7D) == 121);
    CHECK(scriptTicks(g, 0x88) == 3);
    CHECK(scriptTicks(g, 0x85) == 43);

    h.dissolveFrames = 6;  // the castle-top transition and the chute's: 6 frames (log 700..706)
    const uint64_t e45 = h.stepUntil([](const Rec& r) { return r.state == 45; }, 600);
    REQUIRE(e45 != 0);
    // Before the anchor: states 42, 43, 44 after the scripts of slot 6 (0x78, 0x80, 0x79) and slot 9.
    const uint64_t e42 = firstState(h, 42), e43 = firstState(h, 43), e44 = firstState(h, 44);
    CHECK(e42 == 6 + 62 + 1);  // the transition blocks Anim_Tick for 6 ticks
    CHECK(e43 - e42 == 23);
    CHECK(e44 - e43 == 22);
    CHECK(e45 - e44 == 22);

    // From state 45 to the last step down the chute: every record as the log (relative to 45).
    h.step(1000 - 9);
    h.dissolveFrames = 7;  // the castle gate's transition: 7 frames (log 1049..1055)
    h.step(1250 - 9 - h.ticks() + e45);
    const auto r = compareWithLog(h, kEndT2, std::size(kEndT2), 9, 1047, e45);
    MESSAGE("fl_end_t2_wait: " << r.records << " records, " << r.mismatches << " differ" << (r.first.empty() ? "" : ": " + r.first));
    CHECK(r.records == 1039);
    CHECK(r.mismatches == 0);
    const auto rec = [&](uint64_t logT) { return h.at(e45 + (logT - 9)); };
    // The castle gate one tick after the last step (lockstep; the log's tick 1048 was gated and
    // shows the gate at 1049): the treasure room at (0x4F0, 0x4E0), camera (0x44C, 0x480), state 35.
    const Rec gate = rec(1048);
    CHECK(gate.x == 0x4F0);
    CHECK(gate.y == 0x4E0);
    CHECK(gate.camX == 0x44C);
    CHECK(gate.camY == 0x480);
    CHECK(gate.state == 35);
    // Landed 9 records after the gate (the 7-frame dissolve), as the log (1049 -> 1057).
    CHECK(rec(1047 + 8).state == 35);
    CHECK(rec(1047 + 9).state == 1);
    CHECK(rec(1047 + 9).keys == 0);
    // The showcase treasure 0x10 is displayed (script 0xAF) after the landing script, then the
    // automatic move to the rank screen 95 records after the gate (log 1048 -> 1143).
    CHECK(rec(1047 + 94).state == 1);
    CHECK(rec(1047 + 94).x == 0x4F0);
    const Rec rank = rec(1047 + 95);
    CHECK(rank.x == 0x680);
    CHECK(rank.y == 0x80);
    CHECK(rank.camX == 0x680);
    CHECK(rank.camY == 0x80);
    // The state the log's save state holds at its end.
    CHECK(g.player.shownTreasure == 0x10);  // handed over on the chute (Treasure_AddCollected)
    CHECK(g.player.storedShowcase == 0x10);
    CHECK(g.player.showcasePick == 0xFF);
    CHECK(g.player.endTreasureCount == 2);
    CHECK(g.player.endSeqDone == 1);
    CHECK(animScriptWordAt(g, dg::kAnimScript88Frame) == 0x10);
    CHECK(g.core.endState == 1);
    CHECK(g.rec().savedGame == 0);
    CHECK(g.player.shackStay == 0);
    CHECK(g.player.shackNoCase == 0);
    // Sounds: horn (8), the looping sparkle (2), the chest (8), the flight music (1).
    const RecordingSound& s = h.sound();
    CHECK(countCalls(s, RecordingSound::Op::SfxPlay, 8) == 2);  // no script of the sequence plays an effect
    CHECK(countCalls(s, RecordingSound::Op::SfxPlay, 2) >= 1);
    CHECK(countCalls(s, RecordingSound::Op::SfxLoop, 1) == 1);
    CHECK(countCalls(s, RecordingSound::Op::SfxLoop, 0) == 2);  // state 45 and Cell_P64
    CHECK(countCalls(s, RecordingSound::Op::MusicPlay, 1) == 1);
    // The rank chest: total 2 fills 8 rows, one step whenever DS:9100 enters a new group of 4.
    CHECK(g.player.rankRowsWanted == 8);
    CHECK(g.player.rankRowsDone == 8);
    CHECK(g.player.rankState == 0);
    CHECK(countCalls(s, RecordingSound::Op::SfxPlay, 3) == 8);
    CHECK(g.player.champFlashTimer == 0);
}

TEST_CASE("DOS player: the champion branch 63-71 and the crown hand-over, tick for tick (r2/fl_crown_wait)") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    // All six treasures found (tier 0), total 294 + 6 = 300.
    setHiding(g, {{2, 0xD8}, {8, 0xEA}, {10, 0xCE}, {19, 0xC8}, {20, 0xE8}, {22, 0xE4}, {32, 0xE6}, {33, 0x90}, {47, 0xCA}});
    castleTop(h, 294, 6);
    CHECK(g.player.showcasePick == 0xD8);
    h.dissolveFrames = 6;
    // The anchor: the 4th return from a treasure flight to the scan (51 -> 49).
    int returns = 0;
    const uint64_t e99 = h.stepUntil(
        [&returns, &h](const Rec& r) {
            const size_t n = h.recs().size();
            if (n >= 2 && r.state == 49 && h.recs()[n - 2].state == 51) ++returns;
            return returns == 4;
        },
        1200);
    REQUIRE(e99 != 0);
    h.step(1240 - 99);
    const auto r = compareWithLog(h, kCrown, std::size(kCrown), 99, 1216, e99);
    MESSAGE("fl_crown_wait: " << r.records << " records, " << r.mismatches << " differ" << (r.first.empty() ? "" : ": " + r.first));
    CHECK(r.records == 1118);
    CHECK(r.mismatches == 0);
    // The clubhouse one tick after the last step (the log skipped six ticks for the scene load).
    const Rec club = h.at(e99 + (1217 - 99));
    CHECK(club.x == 0x170);
    CHECK(club.y == 0x70);
    CHECK(club.camX == 0xCC);
    CHECK(club.camY == 0);
    CHECK(club.state == 9);
    CHECK(g.core.level == 0);
    CHECK(g.ui.hudRedrawReq2 == 1);
    CHECK(g.player.shownTreasure == 0x58);
    CHECK(g.player.endAllFound == 0);  // the champion branch skips state 55
    CHECK(countCalls(h.sound(), RecordingSound::Op::SfxPlay, 9) == 1);  // the creature screams
    CHECK(countCalls(h.sound(), RecordingSound::Op::SfxPlay, 6) == 1);  // crowned
    // After the dissolve the player is free again (state 10); the celebration scripts run.
    CHECK(g.player.playerState == 10);
    CHECK(g.player.slots[1].anim == 0xA5);
    CHECK(g.player.slots[6].x == 0x52);
    CHECK(g.player.slots[8].x == 0x62);
}

TEST_CASE("DOS player: the search schedule, 37 frames (ega_r2_treasure_found)") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    // Treasure 5 (frame code 0x10) in hiding cell 0xE9, level 0, picture 5 (opened: 0x33 by the
    // attribute bits of level 0's table).
    g.world.cellPicture[0xE9] = 5;
    g.world.cellContent[0xE9] = 0x10;
    g.ui.hudFound = 0;
    g.elves.hintFlags[4] = 1;  // waiting
    h.place(LogPoint{0, 0x4A0, 0x770, 0x400, 0x700, 28, 0, 4}, 3);
    g.player.slots[1].anim = 3;  // the stand script states 27/28 start before Search_Start
    g.player.slots[2].x = 25;
    searchStart(g);
    REQUIRE(g.player.playerState == 5);
    CHECK(g.player.searchTimer == 0x39);
    CHECK(g.player.searchCell == 0xE9);
    CHECK(g.player.searchPicture == 5);
    CHECK(g.core.menuLock == 1);
    // One Search_Update per frame (one frame per tick in lockstep).
    std::vector<uint16_t> picture, content;
    for (int k = 1; k <= 40; ++k) {
        h.step(1);
        picture.push_back(g.world.cellPicture[0xE9]);
        content.push_back(g.player.slots[2].frame);
    }
    int changes = 0;
    for (int k = 1; k <= 16; ++k) {
        INFO("frame " << k);
        CHECK(picture[static_cast<size_t>(k - 1)] == (k % 2 ? 5 : 0x33));         // closed on odd timers
        CHECK(content[static_cast<size_t>(k - 1)] == (k % 2 ? 0x10 : 0xFFFF));    // shown with the next (opened) frame
        if (k > 1 && picture[static_cast<size_t>(k - 1)] != picture[static_cast<size_t>(k - 2)]) ++changes;
    }
    CHECK(changes == 15);
    for (int k = 17; k <= 36; ++k) {
        INFO("frame " << k);
        CHECK(picture[static_cast<size_t>(k - 1)] == 0x33);  // held open, two timer steps per frame
        CHECK(content[static_cast<size_t>(k - 1)] == 0x10);
    }
    CHECK(picture[36] == 5);  // frame 37: closed, the result
    CHECK(content[36] == 0xFFFF);
    CHECK(g.player.searchTimer == 0);
    CHECK(g.core.treasuresFound == 1);
    CHECK(g.rec().foundMasks[0] == 1);
    CHECK(g.world.cellContent[0xE9] == 0x90);
    CHECK(g.core.menuLock == 0);
    CHECK(g.player.slots[2].y == 0);
    CHECK(g.elves.hintFlags[4] == 0);
    CHECK(countCalls(h.sound(), RecordingSound::Op::SfxPlay, 7) == 1);
    CHECK(g.player.playerState == 10);  // state 5 ended at timer 1
    // A second search of the same place finds nothing (searched bit) and keeps the picture.
    g.player.playerState = 28;
    g.player.slots[1].anim = 3;
    searchStart(g);
    h.step(40);
    CHECK(g.core.treasuresFound == 1);
    CHECK(g.world.cellPicture[0xE9] == 5);
}

TEST_CASE("DOS player: a key found, the mirror cell, and a direction key ending the wait") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    // The level-0 key (0x66) in cell 0xE4, which is mirrored as 0xFE.
    g.world.cellPicture[0xE4] = 3;
    g.world.cellContent[0xE4] = 0x66;
    g.world.cellContent[0xFE] = 0x66;
    h.place(LogPoint{0, 0x220, 0x770, 0x180, 0x700, 28, 0, 4}, 3);
    g.player.slots[1].anim = 3;
    searchStart(g);
    REQUIRE(g.player.searchCell == 0xE4);
    h.step(20);
    CHECK(g.player.searchTimer > 1);
    // Left: the place is still in front (side 0), so the search jumps to its result.
    h.script("21:down:left 22:up:left");
    h.step(3);
    CHECK(g.player.searchTimer == 0);
    CHECK(g.core.keyState == 2);
    CHECK(g.ui.hudRedraw == 1);
    CHECK(g.world.cellContent[0xE4] == 0xE6);
    CHECK(g.world.cellContent[0xFE] == 0xE6);
    CHECK(countCalls(h.sound(), RecordingSound::Op::SfxPlay, 6) == 1);
}

TEST_CASE("DOS player: Treasure_PickShowcase compares only g_collected[0], and its g_hidingCells[-1] read") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    auto& col = g.rec().collected;
    // Nothing found this game: the pick is unchanged.
    setHiding(g, {{5, 0x20}, {6, 0x80}});
    g.player.showcasePick = 0xFF;
    treasurePickShowcase(g);
    CHECK(g.player.showcasePick == 0xFF);
    // Found 0x20 (= collected[0]) and 0x22 (= collected[1], which is never compared): 0x22 is new.
    col[0] = 0x20;
    col[1] = 0x22;
    setHiding(g, {{5, 0xA0}, {9, 0xA2}});
    treasurePickShowcase(g);
    CHECK(g.player.showcasePick == 0xA2);
    // Only "known" treasures found: the last of them, bit 7 clear.
    setHiding(g, {{5, 0xA0}, {9, 0xA0}});
    treasurePickShowcase(g);
    CHECK(g.player.showcasePick == 0x20);
    // The first found equals the shown treasure: DI stays -1 and the cell index is read from
    // g_hidingCells[-1] = g_cellContent[255].
    g.player.shownTreasure = 0x20;
    setHiding(g, {{5, 0xA0}});
    g.world.cellContent[255] = 0x30;
    g.world.cellContent[0x30] = 0x2C;
    treasurePickShowcase(g);
    CHECK(g.player.showcasePick == 0x2C);
}

TEST_CASE("DOS player: Treasure_AddCollected shelves the old showcase and removes duplicates once") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    auto& col = g.rec().collected;
    col.fill(0x80);
    col[0] = 0x10;
    col[1] = 0x12;
    col[3] = 0x10;
    g.player.shownTreasure = 0x24;
    g.player.showcasePick = 0x90;  // new treasure 0x10
    g.rec().shelfBits.fill(0);
    treasureAddCollected(g);
    CHECK(col[2] == 0x24);  // the old showcase in the first free slot
    CHECK(col[0] == 0x80);  // both copies of the new showcase left the shelves, with their bits
    CHECK(col[3] == 0x80);
    CHECK(g.rec().shelfBits[0] == 0x09);
    CHECK(g.player.shownTreasure == 0x10);
    CHECK(g.player.storedShowcase == 0x10);
    CHECK(g.player.showcasePick == 0xFF);
    treasureAddCollected(g);  // acts once
    CHECK(col[0] == 0x80);
    CHECK(g.player.shownTreasure == 0x10);
    // Shown -1 (parked outside the clubhouse): the old showcase is replaced, not shelved.
    g.player.shownTreasure = -1;
    g.player.showcasePick = 0x14;  // a known treasure (bit 7 clear): no duplicate removal
    treasureAddCollected(g);
    CHECK(col[0] == 0x80);
    CHECK(g.player.shownTreasure == 0x14);
}

TEST_CASE("DOS player: the treasure room carousel through the collected ring") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    auto& col = g.rec().collected;
    col.fill(0x80);
    col[0] = 0x20;
    col[1] = 0x30;
    h.setScene(4);
    h.place(LogPoint{0, 0x4F0, 0x4E0, 0x44C, 0x480, 1, 0, 4}, 0x51);
    g.player.shownTreasure = 0x10;
    g.player.shackStay = 1;  // visited from the clubhouse with Up: no automatic exit
    h.script("2:down:right 3:up:right");
    h.step(3);  // started at tick 2 (first step there), one more step at tick 3
    CHECK(g.player.shackState == 1);
    CHECK((g.player.shackFlags & 0x81) == 0x81);
    CHECK(g.player.shackX == 0x1E);
    // Out to 0x50 two bytes per tick, the swap, back to 0x1A, finished.
    h.step(25);
    CHECK(g.player.shackX == 0x50);
    h.step(1);
    CHECK(g.player.shackFlags == 2);
    CHECK(g.player.shownTreasure == 0x20);  // the entry after the gap (2) wraps to 0
    CHECK(col[0] == 0x80);
    CHECK(col[2] == 0x10);
    CHECK(countCalls(h.sound(), RecordingSound::Op::SfxPlay, 7) == 1);
    h.step(27);
    CHECK(g.player.shackX == 0x1A);
    CHECK(g.player.shackState == 1);
    h.step(1);
    CHECK(g.player.shackState == 0);
    CHECK(g.player.shackFlags == 0);
    // Left: the gap is at 0 now; the last entry before it (from 47 down) comes back.
    h.script("62:down:left 63:up:left");
    h.step(62 - h.ticks() + 60);
    CHECK(g.player.shownTreasure == 0x10);
    CHECK(col[0] == 0x20);
    CHECK(col[1] == 0x30);
    CHECK(col[2] == 0x80);
    // Down leaves outside the end-of-game visit: the clubhouse (0x170, 0x70), state 9.
    h.script("130:down:down 131:up:down");
    h.step(130 - h.ticks() + 2);
    CHECK(g.player.obj.x == 0x170);
    CHECK(g.player.obj.y == 0x70);
    CHECK(g.core.level == 0);
    CHECK(g.player.playerState == 9);
    CHECK(g.player.shackStay == 0);
    CHECK(g.player.slots[1].anim == 6);
}

TEST_CASE("DOS player: treasure room pair, trio and fly displays") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    auto& col = g.rec().collected;
    h.setScene(4);
    g.player.obj = Obj{0x4F0, 0x4E0, 0, 0};
    g.world.curObj = DsPtr{kPlayerObjOff};
    g.world.cellCallKind = 2;
    g.player.shackStay = 1;
    g.core.endState = 0;
    auto callRoom = [&g](uint16_t keys) {
        g.keys() = keys;
        g.input.cmd = -1;
        return cellP70TreasureRoom(g);
    };
    // Pair 0x0E / 0x2C: the partner approaches one byte per tick, the pedestal treasure moves right
    // to 0x20; when both are in place the combined display 0xB3 starts.
    col.fill(0x80);
    col[4] = 0x2C;
    g.player.shownTreasure = 0x0E;
    callRoom(1);  // Up starts the display
    CHECK(g.player.shackPartner1 == 0x2C);
    CHECK(g.player.shackFlags == 0x40);
    CHECK(g.player.shackPartner1X == 0x4F);
    int calls = 1;
    while (g.player.slots[3].anim == 0 && calls < 200) {
        callRoom(0);
        ++calls;
    }
    CHECK(calls == 63);
    CHECK(g.player.slots[3].anim == 0xB3);
    CHECK(g.player.shackPartner1 == -1);
    CHECK(g.player.shackX == 0x1A);
    // Pair 0x08 / 0x36 (the bodies of the two cases, wave-3 correction): shown 8 with 0x36 collected
    // approaches with direction 1 (pedestal right to 0x22, partner to 0x12), then moves apart
    // alternating the frames 0x36 / 0x6A while the shown treasure is 0x8A, then lands as 8.
    animClear(g, 3);
    shackResetDisplay(g, 0);
    g.player.shackFlags = 0;
    g.player.shackState = 0;
    col.fill(0x80);
    col[7] = 0x36;
    g.player.shownTreasure = 0x08;
    callRoom(1);
    CHECK(g.player.shackPartner1 == 0x36);
    CHECK(g.player.shackX == 0x1B);
    for (calls = 1; g.player.shownTreasure == 0x08 && calls < 200; ++calls) callRoom(0);
    CHECK(g.player.shownTreasure == 0x8A);
    CHECK(g.player.shackX == 0x23);
    CHECK(g.player.shackPartner1 == 0x6A);  // odd x: the alternate frame
    for (calls = 0; g.player.shackX < 0x50 && calls < 200; ++calls) callRoom(0);
    callRoom(0);
    CHECK(g.player.shownTreasure == 0x08);
    CHECK(g.player.slots[3].prev == 0x5E);  // display done
    // Without the partner collected: the treasure's own script (0x08 -> 0xB5).
    animClear(g, 3);
    shackResetDisplay(g, 0);
    g.player.shackFlags = 0;
    g.player.shackState = 0;
    col.fill(0x80);
    g.player.shownTreasure = 0x08;
    callRoom(1);
    CHECK(g.player.slots[3].anim == 0xB5);
    // Trio 0x4A needs both 0x4C and 0x4E; one missing: its own script and the 0x40 flag cleared.
    animClear(g, 3);
    g.player.shackState = 0;
    g.player.shackFlags = 0;
    col[0] = 0x4C;
    g.player.shownTreasure = 0x4A;
    callRoom(1);
    CHECK(g.player.slots[3].anim == 0xBB);
    CHECK((g.player.shackFlags & 0x40) == 0);
    // A plain case: 0x10 -> script 0xAF; a value with no case sets g_shackNoCase for good.
    animClear(g, 3);
    g.player.shackState = 0;
    g.player.shownTreasure = 0x10;
    callRoom(1);
    CHECK(g.player.slots[3].anim == 0xAF);
    animClear(g, 3);
    g.player.shackState = 0;
    g.player.shownTreasure = -1;
    callRoom(1);
    CHECK(g.player.shackNoCase == 1);
    // The fly display of 0x1E: rises 2 rows per tick from 0x43 to below 0, shown as 0xCC at the top,
    // comes back one row per tick and lands as 0x1E (display done).
    g.player.shackNoCase = 0;
    animClear(g, 3);
    shackResetDisplay(g, 0);
    g.player.shackFlags = 0;
    g.player.shackState = 0;
    g.player.shownTreasure = 0x1E;
    callRoom(1);
    CHECK(g.player.shackSavedShown == 0x1E);
    CHECK(g.player.shackY == 0x43);
    int top = 0, land = 0;
    for (calls = 2; calls < 300 && !land; ++calls) {
        callRoom(0);
        if (!top && g.player.shownTreasure == 0xCC) top = calls;
        if (top && g.player.shownTreasure == 0x1E) land = calls;
    }
    CHECK(top == 36);
    CHECK(land == 103);
    CHECK(g.player.shackSavedShown == -1);
    CHECK(g.player.slots[3].prev == 0x5E);
}

TEST_CASE("DOS player: the rank screen cells, the end screen skip and the end-state advance") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    g.world.curObj = DsPtr{kPlayerObjOff};
    g.world.cellCallKind = 2;
    // Cell_P71: the left wall stops Left; rising to row 0x60 enters the rank screen.
    g.player.obj = Obj{0x0A0, 0x070, -4, 0};
    CHECK(cellP71V71ToRankScreen(g) == 0xD9);  // 0xDB & 0xFD (the floor)
    CHECK(g.player.obj.dx == 0);
    g.player.obj = Obj{0x0D0, 0x060, 0, -8};
    cellP71V71ToRankScreen(g);
    CHECK(g.player.obj.x == 0x680);
    CHECK(g.player.obj.y == 0x80);
    CHECK(g.world.cameraX == 0x680);
    CHECK(g.core.level == 4);
    CHECK(g.player.rankState == 1);
    CHECK(g.player.playerState == 1);
    CHECK(g.world.transition == 2);
    // Cell_P72: Enter (command 8) back to the clubhouse; Game_AdvanceEnd ends the game's end state.
    g.core.endState = 1;
    g.player.champFlashTimer = 300;
    g.input.cmd = -1;
    g.keys() = 0;
    CHECK(cellP72RankScreen(g) == 0x104F);  // nothing pressed
    g.input.cmd = 8;
    CHECK(cellP72RankScreen(g) == 3);
    CHECK(g.player.obj.x == 0xF0);
    CHECK(g.player.obj.y == 0x70);
    CHECK(g.world.cameraX == 0xC0);
    CHECK(g.core.level == 0);
    CHECK(g.player.playerState == 9);
    CHECK(g.core.endState == 2);
    CHECK(g.player.champFlashTimer == 0);
    gameAdvanceEnd(g);  // outside the end visit: the front pose
    CHECK(g.player.slots[1].anim == 6);
    // Cell_P64: any command skips the end sequence to the chute.
    g.input.cmd = -1;
    g.player.endSeqDone = 0;
    animClear(g, 1);  // cleared at the castle top
    CHECK(cellP64EndScreen(g) == 0);
    g.input.cmd = 0;
    animSet(g, 9, 0x7D);
    CHECK(cellP64EndScreen(g) == 0x14F0);
    CHECK(g.player.obj.x == 0x6A4);
    CHECK(g.player.obj.y == 0x1F0);
    CHECK(g.player.playerState == 34);
    CHECK(g.player.slots[9].anim == 0);
    CHECK(g.player.slots[1].anim == 0x5D);
    CHECK(g.core.sfxLoop == 0);
    // Cell_P20 above the floor: keep falling.
    g.player.obj = Obj{0x6A4, 0x368, 0, 0};
    CHECK(cellP20CastleGate(g) == 0x14F0);
    // Treasure_IsCollected sets g_shackFlags = 0x40 (an assignment).
    g.rec().collected.fill(0x80);
    g.rec().collected[47] = 0x5A;
    g.player.shackFlags = 0x21;
    CHECK(treasureIsCollected(g, 0x5A) == 1);
    CHECK(g.player.shackFlags == 0x40);
    CHECK(treasureIsCollected(g, 0x06) == 0);
}

TEST_CASE("DOS player: the rank chest fill, the coin rows and the CHAMPION flash") {
    TM_REQUIRE_DOS_GAME_FILES();
    struct Case {
        int16_t total;
        uint16_t rows;
    };
    for (const Case c : {Case{0, 8}, Case{4, 8}, Case{5, 16}, Case{69, 24}, Case{229, 48}, Case{299, 56}, Case{300, 66},
                         Case{450, 86}}) {
        INFO("total " << c.total);
        Harness h(*gameFiles);
        DosGame& g = h.g();
        g.bank = &gameFiles->pak("SHACK");
        h.setScene(4);
        setTerrainMaskHookForTests([](DosGame& gg) { return gg.world.cellMask = 0x104F; });
        h.place(LogPoint{0, 0x680, 0x80, 0x680, 0x80, 1, 0, 4}, 0);
        g.rec().totalTreasures = c.total;
        // The band of the chest in colour 7.
        g.back.fillRect(0x1A, 0x10, 0x98, 0x70, 0x77);
        g.player.rankState = 1;
        std::vector<uint64_t> rowTicks;
        uint16_t last = 0;
        for (int t = 1; t <= 400; ++t) {
            h.step(1);
            if (g.player.rankRowsDone != last) {
                rowTicks.push_back(h.ticks());
                last = g.player.rankRowsDone;
            }
        }
        CHECK(g.player.rankRowsWanted == c.rows);
        CHECK(g.player.rankRowsDone == c.rows);
        CHECK(countCalls(h.sound(), RecordingSound::Op::SfxPlay, 3) == c.rows);
        REQUIRE(rowTicks.size() == c.rows);
        // At most one row per frame, one whenever DS:9100 enters a new group of 4: intervals of 4
        // ticks, 1 or 3 around the 60-tick wrap; 16 rows per 60 ticks.
        int fours = 0;
        for (size_t i = 1; i < rowTicks.size(); ++i) {
            const uint64_t d = rowTicks[i] - rowTicks[i - 1];
            CHECK((d == 1 || d == 3 || d == 4));
            fours += d == 4 ? 1 : 0;
        }
        CHECK(fours >= static_cast<int>(c.rows) * 3 / 4 - 1);
        // The filled rows: colour 7 -> 6 from row 0x6F up, two rows per step (the second overlaps).
        int filledRows = 0;
        for (int y = 0x10; y <= 0x70; ++y) filledRows += g.back.pixel(0x1A * 2, y) == 6 ? 1 : 0;
        CHECK(filledRows == c.rows + 1);
        if (c.total >= 300) {
            // 800 flash frames at ~914/s (reference frame clock): over well within the 400 ticks,
            // ending visible (the last draw in black, ink 0xFFFF).
            CHECK(g.player.champFlashTimer == 0);
            CHECK(g.ui.textInk == 0xFFFF);
        } else {
            CHECK(g.player.champFlashTimer == 0);
        }
    }
}

TEST_CASE("DOS player: the CHAMPION flash counts original frames (period speed: 800 frames)") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    h.runtime().setFrameSpeed(FrameSpeed::Period);
    setTerrainMaskHookForTests([](DosGame& gg) { return gg.world.cellMask = 0x104F; });
    g.player.champFlashTimer = 800;
    int visibleDraws = 0;
    uint16_t lastTimer = 800;
    int frames = 0;
    while (g.player.champFlashTimer != 0 && frames < 1000) {
        h.step(1);
        ++frames;
        // ink 0xFFFF (black) is set when the timer passes 80 mod 160
        if (lastTimer % 160 == 80 && g.ui.textInk == 0xFFFF) ++visibleDraws;
        lastTimer = g.player.champFlashTimer;
    }
    CHECK(frames == 800);
    CHECK(visibleDraws == 5);
    CHECK(g.ui.textInk == 0xFFFF);  // ends visible
    // Slow CPU: four per frame.
    g.core.slowCpu = 1;
    g.player.champFlashTimer = 800;
    for (frames = 0; g.player.champFlashTimer != 0 && frames < 1000; ++frames) h.step(1);
    CHECK(frames == 200);
}

TEST_CASE("DOS player: the end sequence through the real main loop, cell dispatch and dissolve (r2/fl_end_t2_wait)") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    f.selectRecord(0);
    g.rec().setName("tester");
    g.rec().collected.fill(0x80);
    f.placePlayer(0x400, 0x500, 0x400, 0x500, 3, 41);
    setHiding(g, {{2, 0x58}, {8, 0xEA}, {10, 0x4E}, {19, 0x48}, {20, 0xE8}, {22, 0x64}, {32, 0xE6}, {33, 0x90}, {47, 0xCA}});
    castleTopState(g, 0, 2);
    const auto log = f.step(1400);
    // Fixture record k: after engine tick k + 1. Anchor: the first record in state 45 (log 9).
    size_t k45 = 0;
    while (k45 < log.size() && log[k45].state != 45) ++k45;
    REQUIRE(k45 < log.size());
    int mismatches = 0;
    std::string first;
    // States 45..61 have no transition: tick for tick as the log, up to the end-screen teleport.
    for (uint64_t t = 9; t <= 699; ++t) {
        const auto& r = log[k45 + (t - 9)];
        const LogPoint& l = logAt(kEndT2, std::size(kEndT2), t);
        if (r.x != l.x || r.y != l.y || r.camX != l.camX || r.camY != l.camY || r.state != l.state) {
            if (mismatches++ == 0) first = std::format("t={}: state={} / log state={}", t, r.state, l.state);
        }
    }
    MESSAGE("real loop, states 45-61: " << mismatches << " records differ" << (first.empty() ? "" : ": " + first));
    CHECK(mismatches == 0);
    // The chute: 48 steps of 8 rows (and the camera) at the log's ticks relative to the first step.
    size_t kStep = k45 + (699 - 9);
    while (kStep < log.size() && !(log[kStep].x == 0x6A4 && log[kStep].y == 0x1F8)) ++kStep;
    REQUIRE(kStep < log.size());
    const size_t kTeleport = k45 + (700 - 9);
    // The dissolve's length is the main loop's (ENGINE_MAP §3.4): DOSBox-X blocked 6 ticks here.
    MESSAGE("real loop: first chute step " << kStep - kTeleport << " ticks after the end-screen teleport (log: 7)");
    CHECK(kStep - kTeleport >= 7);
    CHECK(kStep - kTeleport <= 8);
    int chute = 0;
    for (uint64_t t = 707; t <= 1047; ++t) {
        const auto& r = log[kStep + (t - 707)];
        const LogPoint& l = logAt(kEndT2, std::size(kEndT2), t);
        chute += (r.y != l.y || r.camY != l.camY || r.state != l.state) ? 1 : 0;
    }
    CHECK(chute == 0);
    // The castle gate one tick after the last step, the treasure room, the rank screen.
    const auto& gate = log[kStep + (1048 - 707)];
    CHECK(gate.x == 0x4F0);
    CHECK(gate.y == 0x4E0);
    CHECK(gate.state == 35);
    CHECK(gate.level == 4);
    size_t kRank = kStep + (1048 - 707);
    while (kRank < log.size() && log[kRank].x != 0x680) ++kRank;
    REQUIRE(kRank < log.size());
    const size_t afterGate = kRank - (kStep + (1048 - 707));
    MESSAGE("real loop: rank screen " << afterGate << " ticks after the gate (log: 94, with a 7-tick dissolve)");
    CHECK(afterGate >= 93);
    CHECK(afterGate <= 95);
    CHECK(g.player.rankRowsDone == 8);
    CHECK(g.player.shownTreasure == 0x10);
}

TEST_CASE("DOS player: ShelfRoom_DrawShelf draws the first flagged room once, with the raised copies") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    auto& col = g.rec().collected;
    col.fill(0x80);
    for (int i = 0; i < 12; ++i) col[static_cast<size_t>(i)] = static_cast<uint8_t>(6 + 2 * i);  // room 0's 12 slots
    col[13] = 0x30;  // room 1
    g.rec().shelfBits.fill(0);
    g.rec().shelfBits[0] = 0x01;  // slot 0 of room 0 had a duplicate removed: a raised copy
    g.world.shelfRoomRedraw = {1, 0, 1, 12, 0, 24, 0, 36};
    g.world.frameInShelfRoom = {1, 1, 0, 0};
    g.world.shelfCamRoom = 0;
    g.world.cameraX = 0x440;
    g.back.clear(0);
    // Nothing without the redraw request (DS:8FBA): the request is consumed anyway.
    g.world.playfieldRedrawn = 0;
    shelfRoomDrawShelf(g);
    auto inked = [&g](int x0, int x1, int y0, int y1) {
        int n = 0;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) n += g.back.pixel(x, y) != 0 ? 1 : 0;
        return n;
    };
    CHECK(inked(0, 319, 0, 199) == 0);
    g.world.playfieldRedrawn = 1;
    shelfRoomDrawShelf(g);
    CHECK(inked(0, 319, 0, 199) > 0);
    CHECK(g.world.shelfRoomRedraw[0] == 0);  // room 0 drawn
    CHECK(g.world.shelfRoomRedraw[2] == 1);  // room 1 not (only the first flagged room per call)
    CHECK(g.world.playfieldRedrawn == 0);
    CHECK(g.back.color() == 1);
    // The same picture drawn from the spec's positions (code_player.md §5.4): slot (r, c) at x byte
    // 0x2C + 0x14 c + 2 (camera 0x440, the room entered from its left end), row 0x28 r + 0x2D; the
    // raised copy 2 bytes left and 4 rows up, drawn first.
    BackBuffer expected;
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 6; ++c) {
            const int i = 6 * r + c;
            const PakFrame& f = g.bank->frame(col[static_cast<size_t>(i)] >> 1);
            if (i == 0) drawSprite(expected, f, 0x2C + 0x14 * c, 0x28 * r + 0x2D - 4, sprite_flags::kMasked);
            drawSprite(expected, f, 0x2C + 0x14 * c + 2, 0x28 * r + 0x2D, sprite_flags::kMasked);
        }
    int differ = 0;
    for (int y = 0; y < BackBuffer::kHeight; ++y)
        for (int x = 0; x < BackBuffer::kRowBytes; ++x) differ += g.back.byteAt(x, y) != expected.byteAt(x, y) ? 1 : 0;
    CHECK(differ == 0);
}

// ---- wave 4: the captures of the end of a game through the whole engine ------------------------
// Each run starts from a DOSBox-X reference state (loadDosboxState) with the --keys script of the
// castle_end fidelity scenario of the same name (tools/dos/scenarios/castle_end/) and compares the
// picture shown after each listed game tick with a capture: 0 differing pixels. The pictures are
// read from the user's captures at run time; only file names, ticks and rectangles are stored here.

namespace {

struct CaptureCheck {
    int tick = 0;                  // the picture shown after this game tick (0: the loaded moment)
    const char* capture = nullptr;  // a 320x200 capture of TM_DOS_CAPTURES
    bool keyMask = false;          // leave out the clue box's key icon (the castle-top race below)
};

struct CaptureRun {
    const char* name;
    const char* state;  // the DOSBox-X state the run starts from
    const char* keys;   // the --keys script, game ticks from the load
    std::vector<CaptureCheck> checks;
};

// The castle-top tick clears g_keyState. DOSBox-X redraws the clue box only when that tick lands
// between a frame's Frame_Begin and its Screen_DrawOverlay (1 of the 5 reference runs: the first run
// below, ega_r2_end_t2_nokey); the engine runs the frame after its tick (wave 5), whose Frame_Begin
// latches scene 3 first, so the icon stays as in the other 4 runs (re/dos/docs/ENGINE_MAP.md §8).
constexpr std::array<int, 4> kKeyIcon = {6, 165, 22, 191};

int differingPixels(const DosGame& g, const tmport::mods::DecodedPng& cap, bool keyMask) {
    const std::vector<uint8_t> shown = toRgba(g.screen, g.palette);
    int n = 0;
    for (int y = 0; y < BackBuffer::kHeight; ++y)
        for (int x = 0; x < BackBuffer::kWidth; ++x) {
            if (keyMask && x >= kKeyIcon[0] && x <= kKeyIcon[2] && y >= kKeyIcon[1] && y <= kKeyIcon[3]) continue;
            const size_t i = (static_cast<size_t>(y) * BackBuffer::kWidth + static_cast<size_t>(x)) * 4;
            if (shown[i] != cap.rgba[i] || shown[i + 1] != cap.rgba[i + 1] || shown[i + 2] != cap.rgba[i + 2]) ++n;
        }
    return n;
}

}  // namespace

TEST_CASE("DOS player: the end of a game from the reference states equals the captures (wave 4)") {
    TM_REQUIRE_DOS_GAME_FILES();
    TM_REQUIRE_DOS_CAPTURES();
    const std::vector<CaptureRun> runs = {
        {"two treasures: the end sequence, the chute, the treasure room, the rank chest",
         "savestates/r2/fl_castle_upper_t2.sav", "82:down:up, 342:up:up",
         {{266, "ega/r2_end_01_castle_top_horn.png", true}, {403, "ega/r2_end_02_treasure_into_chest.png", true},
          {517, "ega/r2_end_03_chest_open.png", true}, {647, "ega/r2_end_04_second_treasure.png", true},
          {972, "ega/r2_end_05_at_chute.png", true}, {1036, "ega/r2_end_06_fall_shaft.png", true},
          {1358, "ega/r2_end_07_treasure_room.png", true}, {1457, "ega/r2_end_08_rank_chest_fill.png", true}}},
        {"the crown: the champion branch, the clubhouse with two figures, CHAMPION",
         "savestates/r2/fl_castle_upper_t6_294.sav", "81:down:up, 342:up:up",
         {{1277, "ega/r2_crown_01_chest_glow.png"}, {1446, "ega/r2_crown_02_window_empty.png"},
          {1567, "ega/r2_crown_03_crown_on_pedestal.png"}, {1774, "ega/r2_crown_a.png"},
          {2158, "ega/r2_crown_05_treasure_room_two_figures.png"}, {2419, "ega/r2_crown_07_rank_champion_hidden.png"},
          {2423, "ega/r2_crown_06_rank_champion_visible.png"}, {2600, "ega/r2_crown_b.png"}}},
        {"Round 1: the castle hall, the ladder", "savestates/ega/castle.sav", "10:down:right, 152:up:right",
         {{1, "ega/097_castle_inside.png"}, {156, "ega/098_castle_ladder.png"}}},
        {"Round 1: the shaft, a ladder jump, the castle top", "savestates/ega/castle_shaft.sav",
         "30:down:pgup, 32:up:pgup, 210:down:up",
         {{1, "ega/099_castle_ladder_shaft.png"}, {119, "ega/100_castle_ladder_jump.png"},
          {545, "ega/101_castle_top_treasure_room.png"}}},
        {"Round 1: the rank chest, the clubhouse message, the hint", "savestates/ega/castle_top.sav",
         "10:down:enter, 11:up:enter, 200:down:enter, 201:up:enter, 450:down:enter, 451:up:enter",
         {{160, "ega/102_castle_rank_chest.png"}, {222, "ega/103_clubhouse_top_reached_msg.png"},
          {458, "ega/104_hint_after_climb.png"}}},
        {"the champion's end messages", "savestates/r2/crown_t2_298_wait.sav",
         "10:down:enter, 11:up:enter, 259:down:enter, 260:up:enter, 469:down:enter, 470:up:enter",
         {{30, "ega/r2_crown_08_end_message.png"}, {265, "ega/r2_crown_09_after_message.png"},
          {475, "ega/r2_crown_10_after_message2.png"}}},
        {"the treasure room from the clubhouse", "savestates/r2/tierb_exitbox.sav",
         "5:down:esc, 6:up:esc, 20:down:right, 52:up:right, 98:down:up, 128:up:up, 1490:down:left, 1495:up:left, "
         "1700:down:right, 1705:up:right, 1911:down:down, 1915:up:down",
         {{126, "ega/r2_treasure_room_from_clubhouse_00.png"}, {1547, "ega/r2_treasure_room_from_clubhouse_01_left.png"},
          {1800, "ega/r2_treasure_room_from_clubhouse_02_right.png"}, {1927, "ega/r2_treasure_room_from_clubhouse_03_down.png"}}},
        {"the prize rooms", "savestates/r2/tierb_exitbox.sav",
         "5:down:esc, 6:up:esc, 20:down:right, 296:up:right, 310:down:right, 454:up:right, 470:down:right, 613:up:right, "
         "630:down:right, 773:up:right, 790:down:right, 933:up:right",
         {{156, "ega/r2_prize_room_1.png"}, {300, "ega/r2_prize_room_2.png"}, {458, "ega/r2_prize_room_3.png"},
          {617, "ega/r2_prize_room_4.png"}, {777, "ega/r2_prize_room_5.png"}, {937, "ega/r2_prize_room_6.png"}}},
    };
    int compared = 0;
    for (const CaptureRun& run : runs) {
        CAPTURE(run.name);
        const auto sav = testdata::dosCapture(run.state);
        REQUIRE(sav.has_value());
        const DosboxState st = DosboxState::load(*sav);
        dosgame::FixtureOptions opts;
        opts.options.look = stateLook(st);
        dosgame::GameFixture f(*gameFiles, opts);
        DosGame& g = f.g();
        const LoadedState loaded = loadDosboxState(g, st);
        for (const std::string& w : loaded.warnings) MESSAGE(run.name << ": " << w);
        REQUIRE(f.setScript(run.keys));
        int done = 0;
        for (const CaptureCheck& c : run.checks) {
            CAPTURE(c.capture);
            CAPTURE(c.tick);
            if (c.tick > done) f.step(c.tick - done);
            done = std::max(done, c.tick);
            const auto file = testdata::dosCapture(c.capture);
            REQUIRE(file.has_value());
            const auto bytes = readWholeFile(*file);
            REQUIRE(bytes.has_value());
            const tmport::mods::DecodedPng cap = tmport::mods::decodePng(*bytes);
            REQUIRE(cap.width == BackBuffer::kWidth);
            REQUIRE(cap.height == BackBuffer::kHeight);
            CHECK(differingPixels(g, cap, c.keyMask) == 0);
            ++compared;
        }
    }
    CHECK(compared == 37);
}
