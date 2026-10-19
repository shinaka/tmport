// The elves of the DOS edition (src/dos/game/elves.cpp, code_elves_layout_hints.md §4): spawn order
// and limits, Elves_Update gating and slot positions, sprite allocation at the view edges (at most
// 3), the net zones, dodges with the 1/64 escape, the elf dust (hit, miss, the right-throw quirk),
// the coin creature, catches and the shared net cost, and the rand() calls of each handler.
// Handlers are called directly on a prepared record (g_curObj = the elf); checks that need the
// animation module (anim.c, another agent's) run only once it starts scripts.
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <set>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "dos/game/anim.h"
#include "dos/game/elves.h"
#include "dos/data/dgroup_offsets.h"
#include "dos/game/game.h"
#include "dos/game/load_state.h"
#include "dos/save/dosbox_state.h"
#include "dos/save/users_dat.h"
#include "dos_game_fixture.h"
#include "dos_test_data.h"

using namespace tmport;
using namespace tmport::dos;

namespace {

int randCallsBetween(uint32_t from, uint32_t to, int limit = 200) {
    BorlandRand r;
    r.setRawSeed(from);
    for (int n = 0; n <= limit; ++n) {
        if (r.seed() == to) return n;
        r.rand();
    }
    return -1;
}

// True once Anim_SetIfIdle sets a script id (the anim module is implemented); probed on slot 12.
bool animReady(DosGame& g) {
    SpriteSlot saved = g.player.slots[12];
    g.player.slots[12].anim = 0;
    animSetIfIdle(g, 12, 0x21);
    const bool ready = g.player.slots[12].anim == 0x21;
    g.player.slots[12] = saved;
    return ready;
}

// A record prepared for a direct handler call: g_curObj = elf i, the slot and its child cleared.
Elf& prepareElf(DosGame& g, int i, uint16_t base, uint16_t state, uint16_t slot, uint16_t question = 0) {
    Elf& e = g.elves.elves[static_cast<size_t>(i)];
    e = Elf{};
    e.obj = Obj{0x400, 0x770, 0, 0};
    e.base = base;
    e.state = state;
    e.spriteSlot = slot;
    e.question = question;
    g.world.curObj = DsPtr{elfObjOff(i)};
    if (slot != 0) {
        g.player.slots[slot] = SpriteSlot{};
        g.player.slots[slot].z = 0x28;
        g.player.slots[slot + 1u] = SpriteSlot{};
    }
    return e;
}

// A rand() seed whose next value has (rand() & mask) == want (or != want when `equal` is false).
uint32_t seedWithNext(int mask, int want, bool equal = true) {
    for (uint32_t s = 1; s < 100000; ++s) {
        BorlandRand r;
        r.setRawSeed(s);
        if (((r.rand() & mask) == want) == equal) return s;
    }
    return 0;
}

struct ElfFixture {
    dosgame::GameFixture f;
    DosGame& g;
    explicit ElfFixture(const DosGfxFiles& files) : f(files), g(f.g()) {
        f.placePlayer(0x400, 0x770, 0x380, 0x700, 0, 10);
        g.core.tier = 0;
        g.core.slowCpu = 0;
        g.world.slowCpuSaved = 0;
        g.core.uiMode = 0;
        g.core.coins = 5;
        g.core.nets = 3;
        g.player.searchTimer = 0;
        g.world.transition = 0;
        g.elves.playerRelX = 40;
        g.elves.elfRelX = 60;
        g.elves.elfRelY = 0;
        g.rand.setRawSeed(12345);
    }
};

}  // namespace

TEST_CASE("dos elves: Elves_Spawn order A, D, B, C with one rand() each; x, y, dx and slot kept") {
    TM_REQUIRE_DOS_GAME_FILES();
    ElfFixture fx(*gameFiles);
    DosGame& g = fx.g;
    for (auto& e : g.elves.elves) {
        e.obj.dx = 7;
        e.spriteSlot = 8;
        e.base = 13;
        e.state = 50;
        e.question = 3;
    }
    const auto& cnt = g.tables.elves.elfCounts[0];
    REQUIRE(cnt[1] == 0);  // tier 0 has no dust throwers
    g.rand.setRawSeed(4242);
    elvesSpawn(g, 2);
    CHECK(g.elves.elfActiveCount == 15);
    const int n = cnt[0] + cnt[1] + cnt[2] + cnt[3];
    CHECK(randCallsBetween(4242, g.rand.seed()) == n);
    BorlandRand r;
    r.setRawSeed(4242);
    const auto& range = g.tables.elves.levelXRange[0];
    int i = 0;
    auto expectElf = [&](uint16_t base, uint16_t question) {
        CAPTURE(i);
        const Elf& e = g.elves.elves[static_cast<size_t>(i)];
        int x = (r.rand() & range[2]) + range[0];
        int y = range[1];
        if (x > 0x7FF) {
            x -= 0x800;
            y += 0x80;
        }
        CHECK(e.obj.x == x);
        CHECK(e.obj.y == y);
        CHECK(e.base == base);
        CHECK(e.state == base);
        CHECK(e.question == question);
        CHECK(e.obj.dx == 7);         // kept
        CHECK(e.spriteSlot == 8);     // kept
        ++i;
    };
    for (unsigned k = 0; k < cnt[0]; ++k) expectElf((k & 1) ? 5 : 1, k < 2 ? static_cast<uint16_t>(k + 1) : 0);
    for (unsigned k = 0; k < cnt[3]; ++k) expectElf(25, 0);
    for (unsigned k = 0; k < cnt[1]; ++k) expectElf((k & 1) ? 13 : 9, 0);
    for (unsigned k = 0; k < cnt[2]; ++k) expectElf((k & 1) ? 21 : 17, 0);
    for (; i < 15; ++i) {
        CHECK(g.elves.elves[static_cast<size_t>(i)].base == 0);
        CHECK(g.elves.elves[static_cast<size_t>(i)].question == 0);
    }
    CHECK(g.world.curObj.off == elfObjOff(n));

    // Tier 3: 15 elves (the throwers fill the middle); slow CPU: still all spawned, 7 processed.
    g.core.tier = 3;
    g.core.slowCpu = 1;
    g.core.level = 2;
    const auto& cnt3 = g.tables.elves.elfCounts[3];
    const uint32_t before = g.rand.seed();
    elvesSpawn(g, 0);
    CHECK(g.elves.elfActiveCount == 7);
    CHECK(randCallsBetween(before, g.rand.seed()) == cnt3[0] + cnt3[1] + cnt3[2] + cnt3[3]);
    CHECK(g.elves.elves[cnt3[0]].base == 25);
    CHECK(g.elves.elves[cnt3[0] + cnt3[3]].base == 9);
    CHECK(g.elves.elves[cnt3[0] + cnt3[3] + 1].base == 13);
    for (const Elf& e : g.elves.elves) {
        if (e.base == 0) continue;
        CHECK(e.obj.y == g.tables.elves.levelXRange[2][1]);
        CHECK(e.obj.x <= 0x3FF);
    }
}

TEST_CASE("dos elves: Elves_Update runs only in play with an unchanged CPU flag; resets the catch count") {
    TM_REQUIRE_DOS_GAME_FILES();
    ElfFixture fx(*gameFiles);
    DosGame& g = fx.g;
    elvesSpawn(g, 0);
    g.elves.netCatchesThisTick = 2;
    const uint32_t seed = g.rand.seed();
    g.core.uiMode = 0x10;
    elvesUpdate(g);
    CHECK(g.elves.netCatchesThisTick == 2);
    CHECK(g.rand.seed() == seed);
    g.core.uiMode = 0;
    g.core.slowCpu = 1;  // the slow-CPU ladder code clears / sets the flag: elves freeze
    elvesUpdate(g);
    CHECK(g.elves.netCatchesThisTick == 2);
    g.core.slowCpu = 0;
    elvesUpdate(g);
    CHECK(g.elves.netCatchesThisTick == 0);
    // Every active record is visited: the last one stays in g_curObj (the stale visible-call object).
    CHECK(g.world.curObj.off == elfObjOff(14));
}

TEST_CASE("dos elves: Elves_Update positions and the sprite count; at most 3 sprites") {
    TM_REQUIRE_DOS_GAME_FILES();
    ElfFixture fx(*gameFiles);
    DosGame& g = fx.g;
    // Five hidden walkers just left of the view, standing: only three get sprites.
    for (auto& rec : g.elves.elves) rec = Elf{};
    for (int i = 0; i < 5; ++i) {
        Elf& e = prepareElf(g, i, 17, 17, 0);
        e.obj.x = static_cast<uint16_t>(0x380 - 10 - 2 * i);  // 5..9 bytes left of the camera
        e.obj.y = 0x770;
    }
    for (int s : {6, 7, 8, 9, 10, 11}) g.player.slots[static_cast<size_t>(s)] = SpriteSlot{};
    g.elves.elfActiveCount = 15;
    g.player.slots[1].x = 50;
    g.rand.setRawSeed(777);
    elvesUpdate(g);
    int visible = 0;
    std::set<int> slots;
    for (int i = 0; i < 5; ++i) {
        const Elf& e = g.elves.elves[static_cast<size_t>(i)];
        if (e.state >= 0x2D) {
            ++visible;
            slots.insert(e.spriteSlot);
            CHECK(e.state == 60);  // type C walking left
            const SpriteSlot& s = g.player.slots[e.spriteSlot];
            const int rel = static_cast<int16_t>(((e.obj.y & 0xFF80) * 16 + e.obj.x) - ((0x700 & 0xFF80) * 16 + 0x380));
            CHECK(s.x == (rel >> 1) + 2);
            CHECK(s.y == (e.obj.y & 0x7F) - (0x700 & 0x7F) + 0x10);
            CHECK(s.owner == e.spriteSlot);
            CHECK(s.obj.off == elfObjOff(i));
            CHECK(e.obj.dx == 0);
            CHECK((s.z == 0x28 || s.z == 0x3C));
        } else {
            CHECK(e.state == 17);  // in range without a sprite: stays hidden (dx 0, or -1 from ElfSt_17)
        }
    }
    CHECK(visible == 3);
    CHECK(slots == std::set<int>{6, 8, 10});
    CHECK(g.elves.elfSpriteCount == 3);
    CHECK(randCallsBetween(777, g.rand.seed()) == 3);  // one depth rand() per allocated slot
    CHECK(g.elves.playerRelX == 48);
    // Next tick: 3 counted at the start; no fourth sprite.
    elvesUpdate(g);
    CHECK(g.elves.elfSpriteCount == 3);
}

TEST_CASE("dos elves: slow CPU: 7 records processed, one sprite counted in advance (at most 2 elves)") {
    TM_REQUIRE_DOS_GAME_FILES();
    ElfFixture fx(*gameFiles);
    DosGame& g = fx.g;
    g.core.slowCpu = 1;
    g.world.slowCpuSaved = 1;
    for (auto& rec : g.elves.elves) rec = Elf{};
    for (int i = 0; i < 9; ++i) {
        Elf& e = prepareElf(g, i, 17, 17, 0);
        e.obj.x = static_cast<uint16_t>(0x380 - 10 - 2 * (i % 5));
    }
    for (int s : {6, 7, 8, 9, 10, 11}) g.player.slots[static_cast<size_t>(s)] = SpriteSlot{};
    g.elves.elfActiveCount = 7;
    elvesUpdate(g);
    int visible = 0;
    for (const Elf& e : g.elves.elves) visible += e.state >= 0x2D;
    CHECK(visible == 2);
    CHECK(g.elves.elfSpriteCount == 3);  // 1 + the two sprites
    // Records 7.. are not processed: no relative position, no handler.
    CHECK(g.elves.elves[7].state == 17);
    CHECK(g.elves.elves[8].obj.dx == 0);
    CHECK(g.world.curObj.off == elfObjOff(6));
}

TEST_CASE("dos elves: Elf_Think sprite allocation at the view edges, release out of range") {
    TM_REQUIRE_DOS_GAME_FILES();
    ElfFixture fx(*gameFiles);
    DosGame& g = fx.g;
    auto& es = g.elves;
    for (int s : {6, 7, 8, 9, 10, 11}) g.player.slots[static_cast<size_t>(s)] = SpriteSlot{};

    // In the view (no edge, no teleport): no sprite, dx cleared.
    Elf& e = prepareElf(g, 0, 1, 1, 0);
    e.obj.dx = -1;
    es.elfSpriteCount = 0;
    es.elfCallCounter = 1;  // 2 at the handler: bit 1 set, so ElfSt_01 leaves dx alone
    es.elfRelX = 50;
    es.elfRelY = 0;
    elfThink(g);
    CHECK(es.elfNetClass == 0);
    CHECK(e.obj.dx == 0);
    CHECK(e.state == 1);

    // Left edge zone, moving away (dx < 0): no sprite.
    e.obj.dx = -1;
    es.elfRelX = -5;
    elfThink(g);
    CHECK(es.elfNetClass == 0);
    // Left edge zone moving in: slot 6 (slot 6 busy by prev: slot 8), the depth rand().
    g.player.slots[6].prev = 0x21;
    e.obj.dx = 1;
    g.rand.setRawSeed(seedWithNext(1, 0));
    elfThink(g);
    CHECK(es.elfNetClass == 2);
    CHECK(e.spriteSlot == 8);
    CHECK(g.player.slots[8].z == 0x3C);
    CHECK(e.state == 45);  // ElfSt_01 with a new sprite: walking left
    CHECK(es.elfSpriteCount == 1);
    // Right edge zone, standing: slot 10 (8 now runs the walk script), odd rand -> depth 0x28.
    Elf& e2 = prepareElf(g, 1, 21, 21, 0);
    g.player.slots[8].anim = 0x1F;
    g.player.slots[10] = SpriteSlot{};
    es.elfRelX = 0x9D;
    g.rand.setRawSeed(seedWithNext(1, 1));
    elfThink(g);
    CHECK(e2.spriteSlot == 10);
    CHECK(g.player.slots[10].z == 0x28);
    CHECK(e2.state == 61);
    // Limit: with 3 sprites counted nothing is allocated.
    Elf& e3 = prepareElf(g, 2, 17, 17, 0);
    es.elfSpriteCount = 3;
    es.elfRelX = -1;
    const uint32_t seed = g.rand.seed();
    elfThink(g);
    CHECK(es.elfNetClass == 0);
    CHECK(e3.state == 17);
    CHECK(g.rand.seed() == seed);
    // No free slot: class 2 anyway, no rand().
    es.elfSpriteCount = 0;
    g.player.slots[6].anim = 0x23;
    g.player.slots[8].anim = 0x23;
    g.player.slots[10].anim = 0x23;
    elfThink(g);
    CHECK(es.elfNetClass == 2);
    CHECK(g.rand.seed() == seed);
    CHECK(es.elfSpriteCount == 1);

    // Out of range with a sprite: Elf_Release (count - 1, state = base, slot freed, child reset).
    Elf& v = prepareElf(g, 3, 9, 49, 6);
    g.player.slots[7].x = 5;
    g.player.slots[7].y = 6;
    g.player.slots[7].frame = 0x100;
    es.elfSpriteCount = 2;
    es.elfRelX = 0xAE;
    elfThink(g);
    CHECK(es.elfNetClass == 1);
    CHECK(v.state == 9);
    CHECK(v.spriteSlot == 0);
    CHECK(es.elfSpriteCount == 1);
    CHECK(g.player.slots[7].x == 0);
    CHECK(g.player.slots[7].y == 0);
    CHECK(g.player.slots[7].frame == 0xFFFF);
    for (const int16_t y : {int16_t{-1}, int16_t{0x81}}) {
        prepareElf(g, 3, 9, 49, 6);
        es.elfRelX = 50;
        es.elfRelY = y;
        elfThink(g);
        CHECK(es.elfNetClass == 1);
    }
    es.elfRelY = 0;
}

TEST_CASE("dos elves: net zones of a net-down swing (classes 4, 5, 6) and their results") {
    TM_REQUIRE_DOS_GAME_FILES();
    ElfFixture fx(*gameFiles);
    DosGame& g = fx.g;
    auto& es = g.elves;
    const int16_t p = 40;
    struct Z {
        uint16_t plr;
        int dx;
        uint16_t cls;
    };
    // Inclusive bounds (bytes from P): right 9..42 edge, 15..36 net, 23..28 centre; left mirrored.
    const Z zones[] = {{24, 8, 3},   {24, 9, 4},   {24, 14, 4},  {24, 15, 5},  {24, 22, 5},  {24, 23, 6},
                       {24, 28, 6},  {24, 29, 5},  {24, 36, 5},  {24, 37, 4},  {24, 42, 4},  {24, 43, 3},
                       {21, -10, 3}, {21, -11, 4}, {21, -16, 4}, {21, -17, 5}, {21, -24, 5}, {21, -25, 6},
                       {21, -30, 6}, {21, -31, 5}, {21, -38, 5}, {21, -39, 4}, {21, -44, 4}, {21, -45, 3},
                       {10, 25, 3},  {22, 25, 3}};
    for (const Z& z : zones) {
        CAPTURE(z.plr);
        CAPTURE(z.dx);
        prepareElf(g, 0, 17, 0x3C, 6);  // C walking left: ElfSt_60 -> Elf_CheckNet
        g.player.playerState = z.plr;
        es.playerRelX = p;
        es.elfRelX = static_cast<int16_t>(p + z.dx);
        es.elfRelY = 0;
        g.rand.setRawSeed(seedWithNext(0x3F, 0, false));
        elfThink(g);
        CHECK(es.elfNetClass == z.cls);
        const Elf& e = es.elves[0];
        const int16_t centre = static_cast<int16_t>(z.plr == 24 ? p + 0x1A : p - 0x1C);
        if (z.cls == 3) {
            CHECK(e.state == 0x3C);
        } else if (z.cls == 4) {
            CHECK(e.state == 64);  // dodge hop
            CHECK(g.player.slots[6].z == 0x28 + 10);
            CHECK(g.player.slots[7].frame == 0xFFFF);
        } else {
            CHECK(e.state == (centre < es.elfRelX ? 65 : 62));  // pulled toward the centre
            CHECK(g.player.slots[6].z == 0x28 + 10);
        }
    }
    g.player.playerState = 10;
}

TEST_CASE("dos elves: dodge, 1/64 escape, escape fall") {
    TM_REQUIRE_DOS_GAME_FILES();
    ElfFixture fx(*gameFiles);
    DosGame& g = fx.g;
    auto& es = g.elves;
    // Class 4: one rand(); (r & 0x3F) == 0 escapes (state 73), else dodge (64).
    Elf& e = prepareElf(g, 0, 1, 45, 6);
    es.elfNetClass = 4;
    g.player.playerState = 24;
    const uint32_t esc = seedWithNext(0x3F, 0);
    g.rand.setRawSeed(esc);
    elfCheckNet(g);
    CHECK(e.state == 73);
    CHECK(randCallsBetween(esc, g.rand.seed()) == 1);
    prepareElf(g, 0, 1, 45, 6);
    es.elfNetClass = 4;
    const uint32_t dodge = seedWithNext(0x3F, 0, false);
    g.rand.setRawSeed(dodge);
    elfCheckNet(g);
    CHECK(e.state == 64);
    CHECK(randCallsBetween(dodge, g.rand.seed()) == 1);
    // Classes 5 / 6 draw nothing.
    prepareElf(g, 0, 1, 45, 6);
    es.elfNetClass = 5;
    g.rand.setRawSeed(9);
    elfCheckNet(g);
    CHECK(g.rand.seed() == 9u);
    CHECK((e.state == 62 || e.state == 65));
    // A carrier's net scripts are offset by 0x16.
    prepareElf(g, 0, 1, 45, 6, 2);
    es.elfNetClass = 3;
    elfCheckNet(g);
    CHECK(es.elfAnimSel == 0x16);
    // Search freeze: slot idle and a search running -> 68, child hidden.
    prepareElf(g, 0, 1, 45, 6);
    g.player.slots[7].frame = 0x40;
    g.player.searchTimer = 5;
    es.elfNetClass = 3;
    elfCheckNet(g);
    CHECK(e.state == 68);
    CHECK(g.player.slots[7].frame == 0xFFFF);
    // ElfSt_68 waits for the search, then the call counter restarts and handler[base] runs.
    es.elfCallCounter = 9;
    elfSt68(g);
    CHECK(e.state == 68);
    g.player.searchTimer = 0;
    elfSt68(g);
    CHECK(es.elfCallCounter == 0);
    CHECK(e.state == 45);  // ElfSt_01 with class 3 (>= 2): walking again

    // ElfSt_73: when idle, depth 0x1E and state 74; ElfSt_74: child y += 2, released past 0xA2.
    prepareElf(g, 0, 1, 73, 8);
    elfSt73(g);
    CHECK(e.state == 74);
    CHECK(g.player.slots[8].z == 0x1E);
    es.elfSpriteCount = 1;
    g.player.slots[9].y = 0xA0;
    elfSt74(g);
    CHECK(g.player.slots[9].y == 0xA2);
    CHECK(e.state == 74);
    elfSt74(g);
    CHECK(e.state == 1);
    CHECK(e.spriteSlot == 0);
    CHECK(es.elfSpriteCount == 0);
}

TEST_CASE("dos elves: elf dust hit, miss, vanish, the coin flies back; the right-throw quirk") {
    TM_REQUIRE_DOS_GAME_FILES();
    ElfFixture fx(*gameFiles);
    DosGame& g = fx.g;
    auto& es = g.elves;
    auto setup = [&](uint16_t state, int16_t childX) -> Elf& {
        Elf& e = prepareElf(g, 0, state < 56 ? 9 : 13, state, 6);
        g.player.slots[7].x = childX;
        g.player.slots[7].frame = 0x100;
        es.elfNetClass = 3;
        es.playerRelX = 40;
        es.elfRelX = 60;
        g.player.playerState = 10;
        g.player.obj.y = 0x770;
        g.core.coins = 5;
        return e;
    };
    // Dust at child.x + X = 40 + 9 (inside P +- 10, strict): hit, one coin, state 54.
    Elf& e = setup(53, -11);
    elfSt53(g);
    CHECK(g.core.coins == 4);
    CHECK(e.state == 54);
    // Strict bounds: d = P + 10 misses; the dust moves 1 byte.
    setup(53, -10);
    elfSt53(g);
    CHECK(g.core.coins == 5);
    CHECK(e.state == 53);
    CHECK(g.player.slots[7].x == -11);
    // No coins, off the path line, or jumping (17..20): miss.
    setup(53, -20);
    g.core.coins = 0;
    elfSt53(g);
    CHECK(e.state == 53);
    for (const uint16_t ps : {uint16_t{17}, uint16_t{18}, uint16_t{19}, uint16_t{20}}) {
        setup(53, -20);
        g.player.playerState = ps;
        elfSt53(g);
        CHECK(e.state == 53);
        CHECK(g.core.coins == 5);
    }
    setup(53, -20);
    g.player.obj.y = 0x768;
    elfSt53(g);
    CHECK(e.state == 53);
    // Netting does not protect.
    setup(53, -20);
    g.player.playerState = 22;
    elfSt53(g);
    CHECK(e.state == 54);
    // At -0x50 the dust vanishes: walks on left (base 9, state 49), child hidden.
    setup(53, -0x50);
    es.elfRelX = 200;
    elfSt53(g);
    CHECK(e.state == 49);
    CHECK(e.base == 9);
    CHECK(g.player.slots[7].frame == 0xFFFF);
    // ElfSt_54: the dust carries the coin back 1 byte per tick; at 0: state 55.
    setup(54, -2);
    elfSt54(g);
    CHECK(g.player.slots[7].x == -1);
    elfSt54(g);
    CHECK(g.player.slots[7].x == 0);
    CHECK(e.state == 54);
    elfSt54(g);
    CHECK(e.state == 55);
    // ElfSt_55 when idle: walks off right with the coin (base 13, state 50).
    g.player.slots[6].anim = 0;
    elfSt55(g);
    CHECK(e.state == 50);
    CHECK(e.base == 13);
    // Right throw: player states 19 / 20 (jumping right) are still hit (4521:0b06 quirk).
    for (const uint16_t ps : {uint16_t{19}, uint16_t{20}}) {
        setup(57, -20);
        g.player.playerState = ps;
        elfSt57(g);
        CHECK(e.state == 58);
        CHECK(g.core.coins == 4);
    }
    setup(57, -20);
    g.player.playerState = 18;
    elfSt57(g);
    CHECK(e.state == 57);
    CHECK(g.player.slots[7].x == -19);  // moves right
    setup(57, 0x50);
    es.elfRelX = 0;
    elfSt57(g);
    CHECK(e.state == 50);
    CHECK(e.base == 13);
    setup(58, 1);
    elfSt58(g);
    CHECK(g.player.slots[7].x == 0);
    elfSt58(g);
    CHECK(e.state == 59);
    g.player.slots[6].anim = 0;
    elfSt59(g);
    CHECK(e.state == 49);
    CHECK(e.base == 9);
    // ElfSt_52 / 56 when the wind-up ends: dust at the thrower, state + 1.
    setup(52, 33);
    g.player.slots[7].y = 4;
    elfSt52(g);
    CHECK(e.state == 53);
    CHECK(g.player.slots[7].x == 0);
    CHECK(g.player.slots[7].y == 0);
    setup(56, 33);
    elfSt56(g);
    CHECK(e.state == 57);
}

TEST_CASE("dos elves: thrower walk, pauses and their rand() calls") {
    TM_REQUIRE_DOS_GAME_FILES();
    ElfFixture fx(*gameFiles);
    DosGame& g = fx.g;
    auto& es = g.elves;
    // Slot busy: no rand().
    Elf& e = prepareElf(g, 0, 9, 49, 6);
    g.player.slots[6].anim = 0x23;
    es.elfNetClass = 3;
    g.rand.setRawSeed(55);
    elfSt49(g);
    CHECK(g.rand.seed() == 55u);
    CHECK(e.state == 49);
    // Idle, even rand(), player far ahead to the left (P < X - 0x26): throw (52).
    prepareElf(g, 0, 9, 49, 6);
    es.playerRelX = 10;
    es.elfRelX = 60;
    uint32_t s = seedWithNext(1, 0);
    g.rand.setRawSeed(s);
    elfSt49(g);
    CHECK(e.state == 52);
    CHECK(randCallsBetween(s, g.rand.seed()) == 1);
    // Even rand(), player close: pause (51).
    prepareElf(g, 0, 9, 49, 6);
    es.playerRelX = 30;
    g.rand.setRawSeed(s);
    elfSt49(g);
    CHECK(e.state == 51);
    // Odd rand(): a second rand() decides a 1/16 pause.
    prepareElf(g, 0, 9, 49, 6);
    s = seedWithNext(1, 1);
    g.rand.setRawSeed(s);
    elfSt49(g);
    CHECK(randCallsBetween(s, g.rand.seed()) == 2);
    // Right: X + 0x2A < P throws.
    prepareElf(g, 0, 13, 50, 6);
    es.playerRelX = 120;
    es.elfRelX = 60;
    s = seedWithNext(1, 0);
    g.rand.setRawSeed(s);
    elfSt50(g);
    CHECK(e.state == 56);
    es.playerRelX = 102;  // 60 + 42 = 102: not more than 42 ahead
    prepareElf(g, 0, 13, 50, 6);
    g.rand.setRawSeed(s);
    elfSt50(g);
    CHECK(e.state == 51);
    // ElfSt_51: when idle one rand() picks the direction.
    prepareElf(g, 0, 9, 51, 6);
    g.rand.setRawSeed(seedWithNext(1, 1));
    elfSt51(g);
    CHECK(e.state == 50);
    CHECK(e.base == 13);
    CHECK(e.obj.dx == 0);

    // Type A: Elf_WalkCommon draws one rand() every call; ElfSt_47 one, or two when idle and even.
    prepareElf(g, 0, 1, 45, 6);
    g.player.slots[6].anim = 0x1F;
    g.rand.setRawSeed(8);
    elfSt45(g);
    CHECK(randCallsBetween(8, g.rand.seed()) == 1);
    CHECK(e.state == 45);
    prepareElf(g, 0, 1, 45, 6);
    s = seedWithNext(0x0F, 0);
    g.rand.setRawSeed(s);
    elfSt45(g);
    CHECK(e.state == 47);  // idle and (r & 15) == 0: pause
    CHECK(es.elfAnimSel == 0);  // Elf_CheckNet's value (the walk id was passed before it)
    prepareElf(g, 0, 1, 47, 6);
    g.player.slots[6].anim = 0x21;
    g.rand.setRawSeed(seedWithNext(1, 0));
    const uint32_t s47 = g.rand.seed();
    elfSt47(g);
    CHECK(randCallsBetween(s47, g.rand.seed()) == 1);
    CHECK(es.elfAnimSel == 0x21);
    prepareElf(g, 0, 1, 47, 6, 1);
    g.rand.setRawSeed(s47);
    elfSt47(g);
    CHECK(randCallsBetween(s47, g.rand.seed()) == 2);
    CHECK((e.state == 45 || e.state == 46));
    CHECK(e.base == (e.state == 45 ? 1 : 5));
    CHECK(es.elfAnimSel == 0x43);  // carrier pause script
}

TEST_CASE("dos elves: coin creature appears, is collected (+2, respawn rand) or runs from the net") {
    TM_REQUIRE_DOS_GAME_FILES();
    ElfFixture fx(*gameFiles);
    DosGame& g = fx.g;
    auto& es = g.elves;
    // ElfSt_25: needs a new sprite this tick, coins < 4 and nets < 5.
    Elf& e = prepareElf(g, 0, 25, 25, 6);
    es.elfNetClass = 2;
    g.core.coins = 3;
    g.core.nets = 4;
    elfSt25(g);
    CHECK(e.state == 70);
    for (const auto& [coins, nets] : {std::pair{4, 0}, std::pair{0, 5}}) {
        prepareElf(g, 0, 25, 25, 6);
        es.elfNetClass = 2;
        g.core.coins = static_cast<uint16_t>(coins);
        g.core.nets = static_cast<uint16_t>(nets);
        elfSt25(g);
        CHECK(e.state == 25);
    }
    prepareElf(g, 0, 25, 25, 6);
    es.elfNetClass = 0;
    g.core.coins = 0;
    g.core.nets = 0;
    elfSt25(g);
    CHECK(e.state == 25);
    // CoinElf_Check: within +-5 bytes and not jumping -> collected (72).
    for (const int d : {-5, 0, 5}) {
        prepareElf(g, 0, 25, 70, 6);
        es.elfNetClass = 3;
        es.playerRelX = 40;
        es.elfRelX = static_cast<int16_t>(40 + d);
        g.player.playerState = 10;
        elfSt70(g);
        CHECK(e.state == 72);
    }
    prepareElf(g, 0, 25, 70, 6);
    es.elfRelX = 46;
    elfSt70(g);
    CHECK(e.state == 70);
    prepareElf(g, 0, 25, 70, 6);
    es.elfRelX = 40;
    g.player.playerState = 19;
    elfSt70(g);
    CHECK(e.state == 70);
    // In a net zone it runs away for good (71).
    prepareElf(g, 0, 25, 70, 6);
    es.elfNetClass = 4;
    elfSt70(g);
    CHECK(e.state == 71);
    // ElfSt_72 when the sparkle ends: released, state 25, one respawn rand(), coins + 2.
    prepareElf(g, 0, 25, 72, 6);
    es.elfSpriteCount = 1;
    g.core.coins = 1;
    g.core.level = 1;
    const uint32_t s = g.rand.seed();
    elfSt72(g);
    CHECK(g.core.coins == 3);
    CHECK(e.state == 25);
    CHECK(e.spriteSlot == 0);
    CHECK(randCallsBetween(s, g.rand.seed()) == 1);
    BorlandRand r;
    r.setRawSeed(s);
    const auto& range = g.tables.elves.levelXRange[1];
    CHECK(e.obj.x == ((r.rand() & range[2]) + range[0]));
    CHECK(e.obj.y == range[1]);
    // Busy slot: nothing yet.
    prepareElf(g, 0, 25, 72, 6);
    g.player.slots[6].anim = 0x59;
    elfSt72(g);
    CHECK(e.state == 72);
    // Turn = release.
    prepareElf(g, 0, 25, 71, 6);
    elfTurn(g);
    CHECK(e.state == 25);
    CHECK(e.spriteSlot == 0);
}

TEST_CASE("dos elves: catches, the carrier's question and the net cost per Elves_Update call") {
    TM_REQUIRE_DOS_GAME_FILES();
    ElfFixture fx(*gameFiles);
    DosGame& g = fx.g;
    auto& es = g.elves;
    g.player.playerState = 24;
    g.core.nets = 3;
    g.core.coins = 0;
    es.netCatchesThisTick = 0;
    // Two plain elves held in the same tick share one net; each gives a coin and zips away.
    Elf& a = prepareElf(g, 0, 17, 63, 6);
    elfSt63(g);
    Elf& b = prepareElf(g, 1, 9, 66, 8);
    elfSt66(g);
    CHECK(a.state == 69);
    CHECK(b.state == 69);
    CHECK(g.core.nets == 2);
    CHECK(g.core.coins == 2);
    CHECK(es.netCatchesThisTick == 2);
    // A catch in a later tick (Elves_Update reset the count) costs another net.
    es.netCatchesThisTick = 0;
    Elf& c = prepareElf(g, 2, 21, 63, 10);
    elfSt63(g);
    CHECK(c.state == 69);
    CHECK(g.core.nets == 1);
    CHECK(g.core.coins == 3);
    // ElfSt_69: when the zip ends, released where it is.
    es.elfSpriteCount = 3;
    elfSt69(g);
    CHECK(c.state == (g.player.slots[10].anim == 0 ? 21 : 69));  // waits while the zip runs
    g.player.slots[10].anim = 0;
    elfSt69(g);
    CHECK(c.state == 21);
    CHECK(c.spriteSlot == 0);

    // A carrier (question 2) in states 21..25 opens the question: g_caughtSlot, g_uiMode | 2, 67.
    es.netCatchesThisTick = 0;
    g.player.playerState = 21;
    Elf& q = prepareElf(g, 3, 1, 66, 6, 2);
    elfSt66(g);
    CHECK(q.state == 67);
    CHECK(g.core.caughtSlot == 2);
    CHECK((g.core.uiMode & 2) != 0);
    CHECK(g.core.nets == 0);
    CHECK(g.core.coins == 4);
    CHECK(es.elfAnimSel == 0x17);
    // While the question is open nothing else is caught.
    g.core.nets = 2;
    es.netCatchesThisTick = 0;
    Elf& d = prepareElf(g, 4, 17, 63, 8);
    elfSt63(g);
    CHECK(d.state == 63);
    CHECK(g.core.nets == 2);
    g.core.uiMode = 0;
    // Player state 26: a plain elf is caught, a carrier not.
    g.player.playerState = 26;
    Elf& q2 = prepareElf(g, 5, 1, 63, 10, 1);
    elfSt63(g);
    CHECK(q2.state == 63);
    Elf& z = prepareElf(g, 9, 17, 63, 6);
    elfSt63(g);
    CHECK(z.state == 69);
    CHECK(g.core.nets == 1);
    // No nets: the elf gets up and walks off when idle (64), child hidden.
    g.core.nets = 0;
    Elf& t = prepareElf(g, 6, 17, 63, 10);
    g.player.slots[11].frame = 0x55;
    elfSt63(g);
    CHECK(t.state == 64);
    CHECK(g.player.slots[11].frame == 0xFFFF);
    // ElfSt_64 when idle: handler[base + 1] restores the depth and walks away from the player.
    g.player.slots[10].anim = 0;
    g.player.slots[10].z = 0x32;
    es.playerRelX = 40;
    es.elfRelX = 60;
    elfSt64(g);
    CHECK(t.state == 61);
    CHECK(t.base == 21);
    CHECK(g.player.slots[10].z == 0x28);

    // ElfSt_03 after the quiz: answered right (g_caughtSlot != 0) loses the question.
    Elf& r = prepareElf(g, 7, 1, 67, 6, 3);
    g.core.caughtSlot = 3;
    elfSt03(g);
    CHECK(r.question == 0);
    CHECK(r.state == 64);
    prepareElf(g, 7, 1, 67, 6, 3);
    g.core.caughtSlot = 0;
    elfSt03(g);
    CHECK(r.question == 3);
    CHECK(r.state == 64);

    // ElfSt_62 / 65: the held pose when the pull ends (state + 1); the centre zone stops the pull.
    Elf& p = prepareElf(g, 8, 1, 62, 6, 1);
    p.obj.dx = 4;
    es.elfNetClass = 6;
    elfSt62(g);
    CHECK(p.state == 63);
    CHECK(p.obj.dx == 0);
    CHECK(es.elfAnimSel == 0x47);
    prepareElf(g, 8, 1, 65, 6);
    p.obj.dx = -4;
    es.elfNetClass = 5;
    elfSt65(g);
    CHECK(p.state == 66);
    CHECK(p.obj.dx == -4);
    CHECK(es.elfAnimSel == 0x32);
}

TEST_CASE("dos elves: hidden drift, resume away, turns, Elves_ShiftNearby") {
    TM_REQUIRE_DOS_GAME_FILES();
    ElfFixture fx(*gameFiles);
    DosGame& g = fx.g;
    auto& es = g.elves;
    // Hidden: dx set on calls with bit 1 of the call counter clear, otherwise unchanged.
    Elf& e = prepareElf(g, 0, 9, 9, 0);
    es.elfNetClass = 0;
    es.elfCallCounter = 4;
    elfSt09(g);
    CHECK(e.obj.dx == -1);
    e.obj.dx = 0;
    es.elfCallCounter = 6;
    elfSt09(g);
    CHECK(e.obj.dx == 0);
    prepareElf(g, 0, 21, 21, 0);
    es.elfCallCounter = 1;
    elfSt21(g);
    CHECK(e.obj.dx == 1);
    prepareElf(g, 0, 5, 5, 0);
    elfSt05(g);
    CHECK(e.obj.dx == 1);

    // Resume (handler[base + 1]): depth - 10, away from the player.
    for (const auto& [base, right, left] : {std::tuple{1, 46, 45}, std::tuple{9, 50, 49}, std::tuple{17, 61, 60}}) {
        prepareElf(g, 0, static_cast<uint16_t>(base), 64, 6);
        es.playerRelX = 40;
        es.elfRelX = 60;
        callElfStateHandler(g, static_cast<uint16_t>(base + 1));
        CHECK(e.state == right);
        CHECK(g.player.slots[6].z == 0x28 - 10);
        prepareElf(g, 0, static_cast<uint16_t>(base), 64, 6);
        es.elfRelX = 40;
        callElfStateHandler(g, static_cast<uint16_t>(base + 1));
        CHECK(e.state == left);
    }
    // Carrier resume uses the carrier walk (g_elfAnimSel 0x22).
    prepareElf(g, 0, 1, 64, 6, 1);
    elfSt02(g);
    CHECK(es.elfAnimSel == 0x22);

    // Turn: visible elves flip; hidden ones ignore it.
    prepareElf(g, 0, 5, 46, 6);
    elfTurn(g);
    CHECK(e.base == 1);
    CHECK(e.state == 45);
    elfTurn(g);
    CHECK(e.base == 5);
    CHECK(e.state == 46);
    prepareElf(g, 0, 13, 50, 6);
    elfTurn(g);
    CHECK(e.state == 49);
    prepareElf(g, 0, 17, 60, 6);
    elfTurn(g);
    CHECK(e.state == 61);
    prepareElf(g, 0, 1, 1, 0);
    elfTurn(g);
    CHECK(e.state == 1);
    CHECK(e.base == 1);

    // Elves_ShiftNearby: visible elves with player.x - 0x100 < x < player.x + 0x100 (unsigned).
    for (int i = 0; i < 15; ++i) prepareElf(g, i, 17, 60, 6).obj.x = 0;
    es.elfActiveCount = 15;
    es.elves[0].obj.x = 0x350;  // inside
    es.elves[1].obj.x = 0x500;  // outside (0x400 + 0x100 is exclusive)
    es.elves[2].obj.x = 0x380;
    es.elves[2].state = 17;     // hidden
    es.elves[3].obj.x = 0x301;
    g.player.obj.x = 0x400;
    g.world.curObj = DsPtr{kPlayerObjOff};
    elvesShiftNearby(g, -2, 0x80);
    CHECK(es.elves[0].obj.x == 0x34E);
    CHECK(es.elves[0].obj.y == 0x7F0);
    CHECK(es.elves[1].obj.x == 0x500);
    CHECK(es.elves[2].obj.x == 0x380);
    CHECK(es.elves[3].obj.x == 0x2FF);
    // Player x < 0x100: the lower bound wraps, nothing moves.
    g.player.obj.x = 0x80;
    es.elves[0].obj.x = 0x90;
    elvesShiftNearby(g, 4, 0);
    CHECK(es.elves[0].obj.x == 0x90);
}

TEST_CASE("dos elves: state table dispatch (no-op entries) and a spawned level running 200 ticks") {
    TM_REQUIRE_DOS_GAME_FILES();
    ElfFixture fx(*gameFiles);
    DosGame& g = fx.g;
    // Null_Handler2 entries change nothing.
    prepareElf(g, 0, 1, 0, 6);
    const std::vector<uint8_t> before = dumpToDgroup(static_cast<const GameState&>(g), initialDgroupImage(g.exe()));
    const uint32_t seed = g.rand.seed();
    for (const int st : {0, 11, 15, 19, 23, 27, 29, 35, 44, 48, 75, 76, 200}) callElfStateHandler(g, static_cast<uint16_t>(st));
    CHECK(dumpToDgroup(static_cast<const GameState&>(g), initialDgroupImage(g.exe())) == before);
    CHECK(g.rand.seed() == seed);

    // A spawned level-0 scene with the camera sweeping right: invariants every tick.
    g.core.tier = 3;
    elvesSpawn(g, 3);
    const bool anim = animReady(g);
    for (int t = 0; t < 200; ++t) {
        g.world.cameraX = static_cast<uint16_t>(0x300 + 4 * t);
        if (g.world.cameraX > 0x7FF - 0x9C * 2) g.world.cameraX = 0x300;
        if (anim) animTick(g);
        elvesUpdate(g);
        int visible = 0;
        std::set<int> slots;
        for (int i = 0; i < g.elves.elfActiveCount; ++i) {
            const Elf& e = g.elves.elves[static_cast<size_t>(i)];
            if (e.state >= 0x2D) {
                ++visible;
                slots.insert(e.spriteSlot);
                CHECK((e.spriteSlot == 6 || e.spriteSlot == 8 || e.spriteSlot == 10));
            }
        }
        CHECK(visible <= 3);
        // Distinct slots need the anim module: a slot is free while its script ids are 0.
        if (anim) CHECK(static_cast<int>(slots.size()) == visible);
        CHECK(g.elves.netCatchesThisTick == 0);
    }
}

TEST_CASE("dos elves: replay of the DOSBox-X tierc session (elf dust takes the coins on the logged ticks)") {
    // The logger states savestates/r2/fl_tierc_outside and fl_tierc_dust1 (TM_DOS_CAPTURES), loaded
    // with the engine's state loader (dos/game/load_state.h). Between the two the crafted tier-3
    // player stood still outside the clubhouse for 2617 game ticks (g_seconds 5 -> 49, DS:9100
    // 10 -> 33) while the throwers took his coins; the dust1 state's per-tick log shows the losses
    // 4->3, 3->2, 2->1, 1->0 in the STEP records of PIT ticks 22550, 24010, 24405, 25625, i.e. during
    // game ticks 1591, 1883, 1962 and 2206 after the outside state (the 5->4 loss lies before the
    // log's ring).
    TM_REQUIRE_DOS_GAME_FILES();
    const auto startPath = testdata::dosCapture("savestates/r2/fl_tierc_outside.sav");
    const auto endPath = testdata::dosCapture("savestates/r2/fl_tierc_dust1.sav");
    if (!startPath || !endPath) {
        MESSAGE("TM_DOS_CAPTURES (r2/fl_tierc_outside, fl_tierc_dust1) not found: replay skipped");
        return;
    }
    const DosboxState start = DosboxState::load(*startPath);
    const DosboxState end = DosboxState::load(*endPath);
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    const LoadedState loaded = loadDosboxState(g, start);
    CHECK(loaded.warnings.empty());
    REQUIRE(g.core.uiMode == 0);
    REQUIRE(g.core.coins == 5);
    REQUIRE(g.keys() == 0);

    std::vector<uint64_t> losses;
    uint16_t coins = g.core.coins;
    for (uint64_t tick = 1; tick <= 2617; ++tick) {
        f.step(1);
        if (g.core.coins != coins) {
            if (g.core.coins < coins) losses.push_back(tick);
            coins = g.core.coins;
        }
    }
    CAPTURE(losses.size());
    REQUIRE(losses.size() == 5);
    CHECK(losses[0] < 570);
    CHECK(losses[1] == 1591);
    CHECK(losses[2] == 1883);
    CHECK(losses[3] == 1962);
    CHECK(losses[4] == 2206);
    CHECK(g.rand.seed() == end.dsDword(dg::kRandSeed));
    const std::vector<uint8_t> now = dumpToDgroup(static_cast<const GameState&>(g), initialDgroupImage(g.exe()));
    std::vector<DgroupField> elfFields;
    for (const DgroupField& d : gameStateFields())
        if (d.name == "g_elves" || d.name == "g_elfActiveCount" || d.name == "g_netCatchesThisTick" || d.name == "g_coins" ||
            d.name == "g_nets")
            elfFields.push_back(d);
    const auto diff = diffDgroup(now, end.dgroup(), &elfFields);
    for (const auto& n : diff) MESSAGE("differs from the DOSBox-X state: " << n);
    CHECK(diff.empty());
}

TEST_CASE("dos elves: replay of the DOSBox-X coin creature run (walk right and back, +2 coins, then dust)") {
    // The logger states savestates/r2/fl_tierc_coincreature (the dust1 state with g_nets poked to 3)
    // and fl_coin_after (TM_DOS_CAPTURES). The player walked right and back left (g_keys of each
    // executed tick from the fl_coin_after log: below), the gold coin scrolled into view and was
    // touched: coins 0 -> 2 during game tick 1421, then elf dust took one during 1504; the after
    // state was saved at the start of tick 1747's record.
    TM_REQUIRE_DOS_GAME_FILES();
    const auto startPath = testdata::dosCapture("savestates/r2/fl_tierc_coincreature.sav");
    const auto endPath = testdata::dosCapture("savestates/r2/fl_coin_after.sav");
    if (!startPath || !endPath) {
        MESSAGE("TM_DOS_CAPTURES (r2/fl_tierc_coincreature, fl_coin_after) not found: replay skipped");
        return;
    }
    const DosboxState start = DosboxState::load(*startPath);
    const DosboxState end = DosboxState::load(*endPath);
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    const LoadedState loaded = loadDosboxState(g, start);
    CHECK(loaded.warnings.empty());
    CHECK(loaded.midFrame);  // saved between Sprites_DrawSorted and Sprites_EraseAll
    REQUIRE(g.core.nets == 3);
    REQUIRE(g.core.coins == 0);
    // g_keys at the start of tick k (from k on): right 56..222, left 238..451 and 1374..1445.
    struct KeyRun {
        uint64_t from;
        uint16_t keys;
    };
    static constexpr KeyRun kKeys[] = {{1, 0}, {56, 8}, {223, 0}, {238, 4}, {452, 0}, {1374, 4}, {1446, 0}};
    std::vector<std::pair<uint64_t, uint16_t>> coinChanges;
    uint16_t coins = 0;
    size_t run = 0;
    for (uint64_t tick = 1; tick <= 1747; ++tick) {
        while (run + 1 < std::size(kKeys) && kKeys[run + 1].from <= tick) ++run;
        g.keys() = kKeys[run].keys;
        f.step(1);
        if (g.core.coins != coins) {
            coins = g.core.coins;
            coinChanges.emplace_back(tick, coins);
        }
    }
    CAPTURE(coinChanges.size());
    REQUIRE(coinChanges.size() == 2);
    CHECK(coinChanges[0].first == 1421);
    CHECK(coinChanges[0].second == 2);
    CHECK(coinChanges[1].first == 1504);
    CHECK(coinChanges[1].second == 1);
    CHECK(g.player.obj.x == end.dsWord(dg::kPlayer));
    CHECK(g.rand.seed() == end.dsDword(dg::kRandSeed));
    const std::vector<uint8_t> now = dumpToDgroup(static_cast<const GameState&>(g), initialDgroupImage(g.exe()));
    std::vector<DgroupField> elfFields;
    for (const DgroupField& d : gameStateFields())
        if (d.name == "g_elves" || d.name == "g_coins" || d.name == "g_nets") elfFields.push_back(d);
    const auto diff = diffDgroup(now, end.dgroup(), &elfFields);
    for (const auto& n : diff) MESSAGE("differs from the DOSBox-X state: " << n);
    CHECK(diff.empty());
}

namespace {

// Loads `startRel`, runs the --keys script `keys` up to the game tick of `endRel`'s g_ticks and
// compares g_randSeed, the elf records and the named fields with that state (wave 4, level1 group).
void replayToState(const DosGfxFiles& files, const char* startRel, const char* endRel, const char* keys,
                   std::initializer_list<const char*> more) {
    const auto startPath = testdata::dosCapture(startRel);
    const auto endPath = testdata::dosCapture(endRel);
    if (!startPath || !endPath) {
        MESSAGE("TM_DOS_CAPTURES (" << startRel << ", " << endRel << ") not found: replay skipped");
        return;
    }
    const DosboxState start = DosboxState::load(*startPath);
    const DosboxState end = DosboxState::load(*endPath);
    dosgame::GameFixture f(files);
    DosGame& g = f.g();
    const LoadedState loaded = loadDosboxState(g, start);
    CHECK(loaded.warnings.empty());
    REQUIRE(f.setScript(keys));
    // The game ticks from the first after the start state up to the end state's g_ticks (the
    // fidelity runner's at_log_end tick).
    const uint32_t first = loaded.firstTickTicks;
    REQUIRE(end.dsDword(dg::kTicks) >= first);
    const int ticks = static_cast<int>((end.dsDword(dg::kTicks) - first) / 5 + 1);
    f.step(ticks);
    CHECK(g.rand.seed() == end.dsDword(dg::kRandSeed));
    CHECK(g.player.obj.x == end.dsWord(dg::kPlayer));
    const std::vector<uint8_t> now = dumpToDgroup(g);  // with the service-held globals (g_seconds)
    std::vector<DgroupField> fields;
    auto pick = [&](const std::vector<DgroupField>& from) {
        for (const DgroupField& d : from) {
            bool want = d.name == "g_elves" || d.name == "g_elfActiveCount" || d.name == "g_coins" || d.name == "g_nets" ||
                        d.name == "g_cameraX" || d.name == "g_cameraY";
            for (const char* m : more) want = want || d.name == m;
            if (want) fields.push_back(d);
        }
    };
    pick(gameStateFields());
    pick(serviceFields());
    const auto diff = diffDgroup(now, end.dgroup(), &fields);
    for (const auto& n : diff) MESSAGE("differs from the DOSBox-X state: " << n);
    CHECK(diff.empty());
}

}  // namespace

TEST_CASE("dos elves: replay of the Round-1 walk to the flags (ega/outside_clue1 -> ega/path_flags, 2267 ticks)") {
    // Ada's Round-1 session: Right held from 83 to 273 (the press pinned by avi/ega_walk_and_scroll.avi,
    // where an elf enters 8 AVI frames before the walk starts), then 2000 ticks standing at the flags
    // while the elves walk, pause and leave the view. Every elf record and g_randSeed equal the
    // DOSBox-X state saved there (level1/walk_to_flags).
    TM_REQUIRE_DOS_GAME_FILES();
    replayToState(*gameFiles, "savestates/ega/outside_clue1.sav", "savestates/ega/path_flags.sav", "83:down:right, 273:up:right",
                  {"g_seconds"});
}

TEST_CASE("dos elves: replay of r2/fl_treasure5_found (an elf walks on screen through a treasure search)") {
    // From r2/at_treasure5: Down at 434 (the only press tick that gives the end state's seed with the
    // wave-5 frame order, the frame after a tick running after it; 433 before), the coin
    // throw, the search (the visible elf keeps walking, its walk script does not end during the
    // search), hint 4; the elves stop with the hint (level1/treasure5_found).
    TM_REQUIRE_DOS_GAME_FILES();
    replayToState(*gameFiles, "savestates/r2/at_treasure5.sav", "savestates/r2/fl_treasure5_found.sav", "434:down:down, 436:up:down",
                  {"g_seconds", "g_treasuresFound"});
}

TEST_CASE("dos elves: replay of the Round-1 first question (ega/quiz1 -> ega/outside_clue1, 1624 ticks)") {
    // The first answer with Enter at tick 10, the praise bubble closed with Enter at 1007: the elves
    // stand still while the question and the bubble are open, then the caught carrier gets up
    // (ElfSt_03 with the stale g_elfAnimSel) and walks off. g_seconds (paused, with the byte-wrap
    // quirk), every elf record and g_randSeed equal the DOSBox-X state (level1/quiz1_to_clue1).
    TM_REQUIRE_DOS_GAME_FILES();
    replayToState(*gameFiles, "savestates/ega/quiz1.sav", "savestates/ega/outside_clue1.sav", "10:tap:enter, 1007:tap:enter",
                  {"g_seconds", "g_coinsShown"});
}

TEST_CASE("dos elves: replay of the Round-1 catch of a clue carrier (ega/outside_caught1 -> ega/quiz1)") {
    // One net swing, Space at 865: the carrier of question slot 2 is pulled into the net (ElfSt_62/65,
    // ElfSt_63/66, Elf_NetCatch), nets 9 -> 8, coins 6 -> 7, g_caughtSlot = 2 and the question opens
    // (level1/catch_to_quiz1). The start state was saved inside Game_Tick, so the tick counters end
    // one tick behind; they are not compared.
    TM_REQUIRE_DOS_GAME_FILES();
    replayToState(*gameFiles, "savestates/ega/outside_caught1.sav", "savestates/ega/quiz1.sav",
                  "865:down:space, 869:up:space", {"g_caughtSlot", "g_uiMode", "g_elfAnimSel"});
}
