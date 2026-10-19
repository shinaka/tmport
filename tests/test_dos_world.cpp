// Tests of the DOS game's world modules (dos/game/cells.h, cells2.h, camera.h, scene.h): every cell
// type with synthetic player and elf positions (masks, teleports, cameras, transitions, sounds,
// rand() draws), the level loops, gates, bridges, geyser, net shop, castle, the visible-cell
// decorations, Frame_Begin, incremental scrolling against full redraws, the dissolve and scene
// loading. Expected numbers come from re/dos/docs/code_world.md and the DOSBox-X observations of
// REFERENCE_CAPTURES.md (bonus spot, bridges, geyser hole, cave door camera). Functions of other
// modules that are still stubs (anim, elves, end screens) are not asserted on. Data-dependent:
// skips without TM_DATA_DOS. Stored facts are numbers only.
#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

#include "dos/app/dos_runtime.h"
#include "dos/data/dgroup_offsets.h"
#include "dos/game/camera.h"
#include "dos/game/cells.h"
#include "dos/game/cells2.h"
#include "dos/game/game.h"
#include "dos/game/scene.h"
#include "dos/game/sound.h"
#include "dos/save/users_dat.h"
#include "dos_game_fixture.h"
#include "rtl/borland_rand.h"

using namespace tmport;
using namespace tmport::dos;

namespace {

using Op = RecordingSound::Op;
using Call = RecordingSound::Call;

dosgame::FixtureOptions noInit() {
    dosgame::FixtureOptions o;
    o.runGameInit = false;  // the world handlers are tested on the EXE's initial state
    return o;
}

// A game in play on the EXE's block grid: no UI mode, fast CPU, the player as g_curObj.
struct World {
    dosgame::GameFixture f;
    DosGame& g;

    explicit World(const DosGfxFiles& files, uint16_t level = 0) : f(files, noInit()), g(f.g()) {
        g.core.uiMode = 0;
        g.core.slowCpu = 0;
        g.world.slowCpuSaved = 0;
        g.core.level = level;
        g.world.loadedScene = level;
        g.world.sceneWanted = level;
        g.world.curObj = DsPtr{kPlayerObjOff};
        f.sound().clear();
    }
    // Player_Move's call: the player at (x, y) with this tick's movement request, Cell_GetMask.
    uint16_t player(uint16_t x, uint16_t y, int16_t dx = 0, int16_t dy = 0) {
        g.player.obj = Obj{x, y, dx, dy};
        g.world.curObj = DsPtr{kPlayerObjOff};
        return cellGetMask(g);
    }
    // Elves_Update's call for elf i.
    uint16_t elf(int i, uint16_t x, uint16_t y, int16_t dx, int16_t dy = 0) {
        g.elves.elves[static_cast<size_t>(i)].obj = Obj{x, y, dx, dy};
        g.world.curObj = DsPtr{elfObjOff(i)};
        return cellGetMask(g);
    }
    Obj& p() { return g.player.obj; }
    Obj& e(int i) { return g.elves.elves[static_cast<size_t>(i)].obj; }
    const std::vector<Call>& sounds() { return f.sound().calls(); }
    int sfxCount(int id) {
        return static_cast<int>(std::count(sounds().begin(), sounds().end(), Call{Op::SfxPlay, id}));
    }
};

std::vector<Call> calls(std::initializer_list<Call> c) { return std::vector<Call>(c); }

// The seed after `n` more rand() calls.
uint32_t seedAfter(uint32_t seed, int n) {
    BorlandRand r;
    r.setRawSeed(seed);
    for (int i = 0; i < n; ++i) r.rand();
    return r.seed();
}

// The play field (x bytes 2..0x9D, rows 0x10..0x8F) of a back buffer.
std::vector<uint8_t> playfield(const BackBuffer& b, int lastRow = 0x8F) {
    std::vector<uint8_t> out;
    for (int y = 0x10; y <= lastRow; ++y)
        for (int x = 2; x <= 0x9D; ++x) out.push_back(b.byteAt(x, y));
    return out;
}

// A full redraw at the camera (as after a teleport).
void redrawAt(DosGame& g, uint16_t camX, uint16_t camY) {
    g.world.cameraX = camX;
    g.world.cameraY = camY;
    g.world.fullRedrawReq = 1;
    frameBegin(g);
    playfieldRedraw(g);
}

// A frame after Player_Move moved the camera by (sx, sy) pixels: latch and scroll.
void scrollBy(DosGame& g, int sx, int sy) {
    g.world.cameraX = static_cast<uint16_t>(g.world.cameraX + sx);
    g.world.cameraY = static_cast<uint16_t>(g.world.cameraY + sy);
    g.world.camStepX = static_cast<int16_t>(sx / 2);  // DS:37EE holds the byte step (only its sign is read)
    g.world.camStepY = static_cast<int16_t>(sy);
    frameBegin(g);
    scrollUpdate(g);
    g.world.camStepX = 0;
    g.world.camStepY = 0;
}

}  // namespace

// ------------------------------------------------------------------------------ dispatch

TEST_CASE("dos world: Cell_GetMask reads the grid and dispatches by cell type") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    // The block grid of SST.EXE (code_world.md §3.3).
    CHECK(cellTypeRaw(g, 0x02) == 12);
    CHECK(cellTypeRaw(g, 0x26) == 73);
    CHECK(cellTypeRaw(g, 0x7E) == 41);
    CHECK(cellTypeRaw(g, 0xEF) == 31);
    CHECK(cellTypeRaw(g, 0xFF) == 1);

    w.player(0x5B0, 0x770);
    CHECK(g.world.cellCallKind == 2);
    CHECK(g.world.curCell == 0xEB);
    // An x of -1 is column 0x1FF (unmasked): the content table is read as types, here type 0, the
    // void, which flashes the border and returns INT 10h's AX.
    const uint8_t border = g.world.borderColor;
    CHECK(w.player(0xFFFF, 0x70) == 0x1001);
    CHECK(g.world.curCell == 0x1FF);
    CHECK(g.world.borderColor == static_cast<uint8_t>(border + 1));

    g.world.cellCallKind = 2;
    w.p() = Obj{0x40, 0x70, 3, 5};
    CHECK(callCellObjectHandler(g, 24) == 0xFF);  // P24: wall (no cell uses it)
    CHECK(w.p().dx == 0);
    CHECK(callCellObjectHandler(g, 25) == 0x5F3);
    CHECK(callCellObjectHandler(g, 28) == 0xFC);  // P28: no downward movement on a path
    CHECK(w.p().dy == 0);
    for (int t : {32, 33, 49, 58, 59, 67, 68, 69}) CHECK(callCellObjectHandler(g, t) == 0xFC);
    CHECK(callCellObjectHandler(g, 74) == 0);  // Null_Handler
    CHECK(callCellObjectHandler(g, 0) == 0x1001);
}

TEST_CASE("dos world: walls, floor steps and the unused camera clamps") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    // Left wall, cell 14 (x 0x200.., y 0x80..): no Left in the left part, floor step at the bottom.
    CHECK(w.player(0x210, 0xF0, -2, 3) == 0xD8);
    CHECK(w.p().dx == 0);
    CHECK(w.p().dy == 0);
    CHECK(w.player(0x250, 0xC0, -2, 3) == 0x10DC);  // higher up: falls (0x1000)
    CHECK(w.p().dx == -2);
    // Right wall, cell 1A.
    CHECK(w.player(0x540, 0xF0, 2) == 0xD4);
    CHECK(w.p().dx == 0);
    CHECK(w.player(0x520, 0xC0, 2) == 0x10DC);
    // The unused clamps touch only DS:37EE / 37F0.
    g.world.cameraY = 0x500;
    g.world.camStepY = 3;
    g.world.camStepX = 4;
    g.world.cellCallKind = 2;
    CHECK(cellP26Unused(g) == 0xFF);
    CHECK(g.world.camStepY == 0);
    CHECK(g.world.camStepX == 4);
    g.world.cameraY = 0x380;
    g.world.camStepY = -2;
    CHECK(cellP29Unused(g) == 0xFF);
    CHECK(g.world.camStepY == 0);
    CHECK(cellP27Unused(g) == 0xFF);
}

// ------------------------------------------------------------------------------ level loops

TEST_CASE("dos world: the path links convey objects through the level seams") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    SUBCASE("level 0: FF and E3 move the player anywhere in the cell") {
        CHECK(w.player(0x790, 0x7F0, 2) == 0xFC);
        CHECK(w.p().x == 0x290);
        CHECK(w.p().y == 0x770);
        w.player(0x790, 0x7F0, -2);  // a left-mover is not moved by FF
        CHECK(w.p().x == 0x790);
        w.player(0x1A0, 0x770, -2);
        CHECK(w.p().x == 0x6A0);
        CHECK(w.p().y == 0x7F0);
    }
    SUBCASE("level 0: elves cross only at the cell edge, the If seams only near the player") {
        w.elf(3, 0x7F8, 0x7F0, 1);  // FF, x & 0x78 == 0x78
        CHECK(w.e(3).x == 0x2F8);
        w.elf(3, 0x790, 0x7F0, 1);  // inside FF: stays
        CHECK(w.e(3).x == 0x790);
        w.elf(4, 0x182, 0x770, -1);  // E3, x & 0x78 == 0
        CHECK(w.e(4).x == 0x682);
        CHECK(w.e(4).y == 0x7F0);
        w.p() = Obj{0x300, 0x770, 0, 0};  // FD (P2): player.x <= 0x300 in row 0x700
        w.elf(5, 0x6C0, 0x7F0, 1);
        CHECK(w.e(5).x == 0x1C0);
        CHECK(w.e(5).y == 0x770);
        w.p() = Obj{0x308, 0x770, 0, 0};
        w.elf(5, 0x6C0, 0x7F0, 1);
        CHECK(w.e(5).x == 0x6C0);
        w.p() = Obj{0x680, 0x7F0, 0, 0};  // E5 (P4): player.x >= 0x680 in row 0x780
        w.elf(6, 0x2C0, 0x770, -1);
        CHECK(w.e(6).x == 0x7C0);
        CHECK(w.e(6).y == 0x7F0);
    }
    SUBCASE("level 1: C2, AB and the elf seams C0, AD") {
        w.player(0x140, 0x670, 2);  // C2
        CHECK(w.p().x == 0x6C0);
        CHECK(w.p().y == 0x570);
        w.player(0x5C0, 0x570, -2);  // AB
        CHECK(w.p().x == 0x40);
        CHECK(w.p().y == 0x670);
        w.p() = Obj{0x700, 0x570, 0, 0};  // C0 (P14): player.x <= 0x700 in row 0x500
        w.elf(2, 0x40, 0x670, 1);
        CHECK(w.e(2).x == 0x5C0);
        CHECK(w.e(2).y == 0x570);
        w.p() = Obj{0x100, 0x670, 0, 0};  // AD (P16): player in row 0x600
        w.elf(2, 0x6F8, 0x570, -1);
        CHECK(w.e(2).x == 0x178);
        CHECK(w.e(2).y == 0x670);
        w.p() = Obj{0x680, 0x5F0, 0, 0};  // row 0x580 right of 0x600
        w.elf(2, 0x6F8, 0x570, -1);
        CHECK(w.e(2).x == 0x178);
        w.p() = Obj{0x600, 0x5F0, 0, 0};  // row 0x580 at 0x600: no
        w.elf(2, 0x6F8, 0x570, -1);
        CHECK(w.e(2).x == 0x6F8);
    }
    SUBCASE("level 2: 88, 77 and the elf seam 86") {
        w.player(0x440, 0x470, 2);  // 88
        CHECK(w.p().x == 0x4C0);
        CHECK(w.p().y == 0x3F0);
        w.player(0x3C0, 0x3F0, -2);  // 77
        CHECK(w.p().x == 0x340);
        CHECK(w.p().y == 0x470);
        w.p() = Obj{0x500, 0x3F0, 0, 0};  // 86 (P43): player.x <= 0x500 in row 0x380
        w.elf(1, 0x340, 0x470, 1);
        CHECK(w.e(1).x == 0x3C0);
        CHECK(w.e(1).y == 0x3F0);
    }
}

// ------------------------------------------------------------------------------ doorways

TEST_CASE("dos world: the doorways and exits teleport as in code_world.md §9") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    struct Expect {
        uint16_t x, y, camX, transition, area, state;
    };
    auto check = [&](const Expect& e) {
        CHECK(w.p().x == e.x);
        CHECK(w.p().y == e.y);
        CHECK(g.world.cameraX == e.camX);
        CHECK(g.world.transition == e.transition);
        CHECK(g.core.area == e.area);
        CHECK(g.player.playerState == e.state);
        CHECK(g.world.fullRedrawReq == 1);
    };
    SUBCASE("EB cave door (P5)") {
        CHECK(w.player(0x5A0, 0x760) == 0xFC);  // the new x 0x460 is past the door column
        check({0x460, 0x70, 0x440, 2, 7, 9});
        CHECK(w.sounds() == calls({{Op::MusicStop, 0}, {Op::MusicPlay, 2}}));
        CHECK(w.player(0x5A0, 0x770, 0, 3) == 0xFD);  // on the path: no Down
        CHECK(w.p().dy == 0);
        const uint16_t before = g.world.cellMask;
        w.elf(0, 0x5A0, 0x760, 0);  // no elf goes through
        CHECK(w.e(0).y == 0x760);
        CHECK(g.world.cellMask == before);
    }
    SUBCASE("F7 cave door (P9): the camera 0x48C of capture ega/w_cave_f7") {
        CHECK(w.player(0x3B0, 0x7E0) == 0xFF);
        check({0x5B0, 0x70, 0x48C, 2, 0x0B, 9});
        CHECK(w.sounds() == calls({{Op::MusicStop, 0}, {Op::MusicPlay, 2}}));
        // The scenery uses camX & ~7 (0x488), the sprites the exact camera: the player is drawn at
        // byte ((0x5B0 - 0x48C) >> 1) + 2 = 0x94 against the tile grid's 0x96 (the capture's anchor).
        frameBegin(g);
        CHECK(g.world.camTileColOff == ((0x488 << 2) & 0x1E0));
        CHECK(g.world.camCellCol == 9);
        CHECK(((0x5B0 - g.world.cameraX) >> 1) + 2 == 0x94);
        CHECK(w.player(0x3B0, 0x7F0) == 0xFD);  // standing below the door: nothing
    }
    SUBCASE("08 / 0B cave exits (P6, P10)") {
        CHECK(w.player(0x420, 0x70, -1) == 0x7C);
        check({0x5B0, 0x770, 0x510, 2, 0, 9});
        CHECK(w.sounds() == calls({{Op::MusicStop, 0}, {Op::MusicPlay, 0}}));
        w.player(0x5A0, 0x70, 1);
        check({0x3B0, 0x7F0, 0x310, 2, 0, 9});
    }
    SUBCASE("0A passage up (P34) leaves the music off; 21 down (P35) keeps the screen offset") {
        w.player(0x520, 0x60);
        check({0xC0, 0x170, 0xA0, 2, g.core.area, 9});
        CHECK(w.sounds() == calls({{Op::MusicStop, 0}}));
        // On the path the door column 0x26..0x5A decides whether Up may start the look-up rise:
        // at x & 0x78 = 0x60 Up is masked (the r1 test where Up at x 0x560 did nothing).
        CHECK(w.player(0x560, 0x70) == 0xFC);
        CHECK(w.player(0x530, 0x70) == 0xFD);
        w.f.sound().clear();
        g.world.cameraX = 0x123;
        CHECK(w.player(0xC0, 0x160) == 0x0F);
        check({0x4C0, 0x70, 0x123, 1, 9, 9});
        CHECK(w.sounds() == calls({{Op::MusicStop, 0}, {Op::MusicPlay, 2}}));
    }
    SUBCASE("F3 clubhouse door (P11) brings the showcase back") {
        g.player.storedShowcase = 5;
        g.player.shownTreasure = -1;
        w.player(0x1C0, 0x7E0);
        check({0x140, 0x70, 0xC0, 2, 5, 9});
        CHECK(w.sounds() == calls({{Op::MusicStop, 0}, {Op::MusicPlay, 1}}));
        CHECK(g.player.shownTreasure == 5);
        CHECK(g.player.storedShowcase == 5);
    }
    SUBCASE("mine tunnel doors and exits (P17, P18, P37, P38)") {
        w.player(0x140, 0x5E0);
        check({0x440, 0x170, 0x400, 2, 0x0A, 9});
        w.player(0x5C0, 0x5E0);
        check({0x540, 0x170, 0x440, 2, 0x0B, 9});
        w.player(0x460, 0x170, -1);  // x & 0x78 = 0x60 > 0x48: not yet
        CHECK(w.p().x == 0x460);
        w.player(0x440, 0x170, -1);
        check({0x148, 0x5F0, 0xA8, 2, 0, 9});
        w.player(0x530, 0x170, 1);  // x & 0x78 = 0x30 < 0x38: not yet
        CHECK(w.p().x == 0x530);
        w.player(0x540, 0x170, 1);
        check({0x5B8, 0x5F0, 0x518, 2, 0, 9});
    }
    SUBCASE("ice cave door, exit and slide (P60, P61, P62)") {
        w.player(0x640, 0x3E0);
        check({0x6C0, 0x170, 0x680, 2, 0x0C, 9});
        CHECK(w.sounds() ==
              calls({{Op::MusicStop, 0}, {Op::MusicStop, 0}, {Op::SfxStop, 0}, {Op::MusicPlay, 2}}));
        w.player(0x6C0, 0x170, -1);
        check({0x648, 0x3F0, 0x5A8, 2, 0, 9});
        w.f.sound().clear();
        g.core.keyState = 6;
        CHECK(w.player(0x770, 0x170, 1, 3) == 0x1400);
        check({0x6A8, 0x1F0, 0x600, 2, 0x0E, 0x1F});
        CHECK(w.p().dy == 0);
        CHECK(g.core.keyState == 4);
        CHECK(w.sounds() == calls({{Op::SfxPlay, 9}}));
    }
}

TEST_CASE("dos world: the start room, the prize picture and the shelf rooms") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    SUBCASE("P12: floor, door with Up held, falling back without it") {
        CHECK(w.player(0x140, 0x70, 0, 2) == 0xFD);
        CHECK(w.p().dy == 0);
        CHECK(w.player(0x140, 0x60) == 0x10FD);  // no Up: falls back
        g.keys() = 1;
        g.player.shownTreasure = 7;
        CHECK(w.player(0x140, 0x60) == 0xF1);
        CHECK(w.p().x == 0x1C0);
        CHECK(w.p().y == 0x7F0);
        CHECK(g.world.cameraX == 0x120);
        CHECK(g.world.transition == 2);
        CHECK(g.player.playerState == 9);
        CHECK(g.core.area == 0);
        CHECK(w.sounds() == calls({{Op::MusicStop, 0}, {Op::MusicPlay, 0}}));
        CHECK(g.player.storedShowcase == 7);  // put away while outside
        CHECK(g.player.slots[3].frame == 0xFFFF);
        CHECK(g.player.shownTreasure == -1);
    }
    SUBCASE("P12: rising to 0x60 at the prize picture enters the treasure room (x 0x168 in r2)") {
        const uint8_t stay = g.player.shackStay;
        g.world.transition = 0;
        CHECK(w.player(0x168, 0x60) == 0x0002);
        CHECK(w.p().x == 0x4F0);
        CHECK(w.p().y == 0x4E0);
        CHECK(g.core.level == 4);
        CHECK(g.core.menuLock == 1);
        CHECK(g.player.playerState == 1);
        CHECK(g.world.transition == 0);  // no dissolve: the camera keeps the screen offset
        CHECK(g.player.shackStay == static_cast<uint8_t>(stay + 1));
        CHECK(w.player(0x168, 0x64) == 0x0002);  // above it: only Down
        CHECK(w.p().x == 0x168);
    }
    SUBCASE("P12: the crown hand-over") {
        g.player.slots[1].prev = 0xA5;
        CHECK(w.player(0x170, 0x70) == 0);
        CHECK(g.core.level == 4);
        CHECK(g.core.menuLock == 1);
        CHECK(g.world.fullRedrawReq == 1);
    }
    SUBCASE("the shelf rooms chain rooms 0..3 (P50..P66)") {
        struct Move {
            uint16_t fromX, fromY;
            int16_t dx;
            uint16_t toX, toY, camX;
            uint8_t camRoom;
            int roomOn, roomOff, redraw;
        };
        const Move moves[] = {
            {0x230, 0x70, 0, 0x460, 0x1F0, 0x440, 0, 0, -1, 0},    // P50 04 -> room 0
            {0x5C0, 0x1F0, 0, 0x460, 0x270, 0x440, 1, 1, 0, 2},    // P52 3B -> room 1
            {0x5C0, 0x270, 0, 0x460, 0x2F0, 0x440, 2, 2, 1, 4},    // P54 4B -> room 2
            {0x5C0, 0x2F0, 0, 0x460, 0x370, 0x440, 3, 3, 2, 6},    // P65 5B -> room 3
            {0x410, 0x370, -1, 0x580, 0x2F0, 0x480, 4, 2, 3, 4},   // P66 68 -> room 2
            {0x410, 0x2F0, -1, 0x580, 0x270, 0x480, 3, 1, 2, 2},   // P55 58 -> room 1
            {0x410, 0x270, -1, 0x580, 0x1F0, 0x480, 2, 0, 1, 0},   // P53 48 -> room 0
        };
        for (const Move& m : moves) {
            CAPTURE(m.fromX);
            CAPTURE(m.fromY);
            g.world.playfieldRedrawn = 0;
            CHECK(w.player(m.fromX, m.fromY, m.dx) == 0xDC);
            CHECK(w.p().x == m.toX);
            CHECK(w.p().y == m.toY);
            CHECK(g.world.cameraX == m.camX);
            CHECK(g.world.transition == 2);
            CHECK(g.player.playerState == 0x0A);
            CHECK(g.world.shelfCamRoom == m.camRoom);
            CHECK(g.world.inShelfRoom[static_cast<size_t>(m.roomOn)] == 1);
            if (m.roomOff >= 0) CHECK(g.world.inShelfRoom[static_cast<size_t>(m.roomOff)] == 0);
            CHECK(g.world.shelfRoomRedraw[static_cast<size_t>(m.redraw)] == 1);
            CHECK(g.world.playfieldRedrawn == 1);
        }
        CHECK(g.player.slots[3].frame == 0xFFFF);  // P50 hides the showcase
        w.player(0x410, 0x1F0, 1);  // P51 only for a left-mover
        CHECK(w.p().x == 0x410);
        w.player(0x410, 0x1F0, -1);
        CHECK(w.p().x == 0x200);
        CHECK(w.p().y == 0x70);
        CHECK(g.world.cameraX == 0x100);
        CHECK(g.world.inShelfRoom[0] == 0);
        CHECK(g.world.shelfRoomRedraw[0] == 0);
        g.world.cellCallKind = 1;  // visible calls do nothing
        w.p() = Obj{0x230, 0x70, 0, 0};
        cellP50ShelfEnter(g);
        CHECK(w.p().x == 0x230);
    }
}

// ------------------------------------------------------------------------------ gates, boundaries

TEST_CASE("dos world: the three gates open with their keys") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    SUBCASE("EF tree trunk (P31), key state 2") {
        g.core.keyState = 2;
        CHECK(g.world.cellPicture[0xEF] == 0x40);
        CHECK(w.player(0x7C0, 0x760) == 0x5FF);
        CHECK(w.p().x == 0x7C8);  // snapped to the trunk
        CHECK(g.world.cellPicture[0xEF] == 0x4C);  // turned into a ladder
        CHECK(g.world.fullRedrawReq == 2);
        CHECK(g.core.area == 2);
        CHECK(w.sounds() == calls({{Op::SfxPlay, 8}}));
        w.player(0x7C8, 0x758);
        CHECK(w.sfxCount(8) == 1);  // once
        CHECK(w.player(0x7C8, 0x770, 0, 2) == 0x4FD);
    }
    SUBCASE("EF without the key: Up arms hint 8") {
        g.core.keyState = 0;
        g.player.keysUnmasked = 1;
        g.elves.hintFlags[8] = 1;
        CHECK(w.player(0x7C8, 0x770) == 0x4FC);
        CHECK(g.elves.hintFlags[8] == 0);
        g.elves.hintFlags[8] = 0x80;  // shown already
        w.player(0x7C8, 0x770);
        CHECK(g.elves.hintFlags[8] == 0x80);
    }
    SUBCASE("AE geyser cave (P39), key state 4") {
        g.core.keyState = 4;
        CHECK(w.player(0x730, 0x550) == 0xCF1);  // the geyser lifts him
        CHECK(w.p().x == 0x740);
        CHECK(g.world.cellPicture[0xAE] == 0x35);
        CHECK(g.core.area == 3);
        CHECK(w.sfxCount(8) == 1);
        CHECK(w.player(0x740, 0x564) == 0x4FF);  // 0x60..0x67: no lift
        g.core.keyState = 0;
        g.player.keysUnmasked = 1;
        g.elves.hintFlags[8] = 1;
        CHECK(w.player(0x740, 0x570) == 0x4FC);
        CHECK(g.elves.hintFlags[8] == 0);
    }
    SUBCASE("79 stone arch (P42), key state 6: open, then enter on a later tick") {
        g.core.keyState = 6;
        g.core.slowCpu = 1;
        g.world.slowScroll = 1;
        w.player(0x4D0, 0x3D0);  // snaps: the redraw request blocks the rest this tick
        CHECK(w.p().x == 0x4C0);
        CHECK(g.world.cellPicture[0x79] == 0x26);
        g.world.fullRedrawReq = 0;
        w.player(0x4C0, 0x3D0);
        CHECK(g.world.cellPicture[0x79] == 0x3C);
        CHECK(g.world.cellPicture[0x88] == 0x3C);
        CHECK(w.sfxCount(8) == 1);
        g.world.fullRedrawReq = 0;
        w.f.sound().clear();
        CHECK(w.player(0x4C0, 0x3D0, 0, -2) == 0xFF);
        CHECK(w.p().x == 0x1C0);
        CHECK(w.p().y == 0x3F0);
        CHECK(w.p().dy == 0);
        CHECK(g.world.transition == 1);
        CHECK(g.core.area == 0x0D);
        CHECK(g.player.playerState == 9);
        CHECK(g.world.slowCpuSaved == 1);
        CHECK(g.world.slowScroll == 0);
        CHECK(w.sounds() == calls({{Op::MusicStop, 0}, {Op::MusicPlay, 2}}));
        g.core.keyState = 4;  // without the key: no Up/Down and no hint
        g.elves.hintFlags[8] = 1;
        g.player.keysUnmasked = 1;
        CHECK(w.player(0x4C0, 0x3F0) == 0xFC);
        CHECK(g.elves.hintFlags[8] == 1);
        g.core.keyState = 6;
        w.p() = Obj{0x300, 0x470, 0, 0};  // the elf conveyor to 88
        w.elf(7, 0x4C0, 0x3F0, -1);
        CHECK(w.e(7).x == 0x440);
        CHECK(w.e(7).y == 0x470);
    }
    SUBCASE("73 castle door (P44)") {
        g.core.slowCpu = 1;
        g.world.slowCpuSaved = 1;
        CHECK(w.player(0x1C0, 0x3F0, 0, 1) == 0x4FD);  // the hall floor forces the fast CPU mode
        CHECK(g.core.slowCpu == 0);
        CHECK(w.p().dy == 0);
        CHECK(w.player(0x1C0, 0x3E0) == 0x14FF);  // no Up: falls back
        CHECK(w.p().x == 0x1C0);
        g.keys() = 1;
        g.player.playerState = 10;
        w.player(0x1C0, 0x3E0);
        CHECK(w.p().x == 0x4C0);
        CHECK(w.p().y == 0x3F0);
        CHECK(g.core.slowCpu == 1);
        CHECK(g.world.slowScroll == 1);
        CHECK(g.world.transition == 1);
        CHECK(g.core.area == 0);
        CHECK(w.sounds() == calls({{Op::MusicStop, 0}, {Op::MusicPlay, 0}}));
    }
}

TEST_CASE("dos world: the top of the trunk ladder (P30) and the geyser decoration") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles, 1);
    DosGame& g = w.g;
    SUBCASE("BF: onto the ladder going down, no Up on the path, a ladder above it") {
        CHECK(w.player(0x7C0, 0x5F0, 0, 2) == 0x5F3);
        CHECK(w.p().x == 0x7CC);  // snapped onto the ladder
        CHECK(g.world.fullRedrawReq == 1);
        CHECK(w.player(0x7C0, 0x5F0) == 0x4FE);
        w.player(0x7C0, 0x5F0, 0, -2);
        CHECK(w.p().dy == 0);
        g.core.area = 0;
        CHECK(w.player(0x7CC, 0x5A0) == 0x5F3);
        CHECK(g.core.area == 2);
        CHECK(w.player(0x790, 0x5A0) == 0x5F0);  // outside the ladder column 0x30..0x50
        CHECK(w.elf(0, 0x7C0, 0x5F0, 1, 2) == 0x4FF);
        CHECK(w.e(0).x == 0x7C0);
    }
    SUBCASE("the geyser column shows in the visible cells once the gate is open") {
        g.world.cellCallKind = 1;
        g.world.cellScreenX = 0x42;
        g.world.decorCount = 0;
        g.world.cameraY = 0x480;
        cellP40V40Geyser(g);  // gate AE still closed (0x38): no geyser
        CHECK(g.world.decorCount == 0);
        g.world.cellPicture[0xAE] = 0x35;
        cellP40V40Geyser(g);
        const SpriteSlot& s = g.player.slots[12];
        CHECK(g.world.decorCount == 1);
        CHECK(s.x == 0x62);
        CHECK(s.y == 0x3B + 0x54);  // wy - camY clamped at 0x3B
        CHECK(s.z == 0x50);
        g.world.cameraY = 0x4E0;
        cellP41V41GeyserTop(g);
        CHECK(s.y == 0x20 + 0x54);
        CHECK(s.z == 0x1E);
        g.world.cellCallKind = 2;  // object calls never draw it
        g.world.decorCount = 0;
        cellDrawGeyser(g, 0x500, 0x50);
        CHECK(g.world.decorCount == 0);
    }
}

TEST_CASE("dos world: the showcase and the treasure-room partners (V49)") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    PlayerState& p = g.player;
    g.world.cellScreenX = 0x30;
    g.world.cellScreenY = 0x10;
    g.world.shelfCamRoom = 2;
    p.shackX = 0x1A;
    p.shackY = 0x44;
    p.shownTreasure = 0x0C;
    p.shackFlags = 0;
    p.slots[3].anim = 0;
    p.slots[3].prev = 9;
    cellV49Showcase(g);
    CHECK(p.slots[3].x == 0x30 + 0x1A);
    CHECK(p.slots[3].y == 0x10 + 0x44);
    CHECK(p.slots[3].z == 0x50);
    CHECK(p.slots[3].owner == 3);
    CHECK(p.slots[3].frame == 0x0C);
    CHECK(p.slots[3].prev == 0);
    CHECK(g.world.shelfCamRoom == 0);
    CHECK(g.world.decorCount == 0);  // not a decoration
    p.slots[3].anim = 0x5E;  // a showcase script runs: the frame is left to it
    p.shownTreasure = 0x0E;
    cellV49Showcase(g);
    CHECK(p.slots[3].frame == 0x0C);
    p.slots[3].anim = 0;
    p.shackFlags = 0x40;
    p.shackPartner1 = 0x10;
    p.shackPartner1X = 0x20;
    p.shackPartner2 = 0x12;
    p.shackPartner2X = 0x40;
    cellV49Showcase(g);
    CHECK(p.slots[8].frame == 0x10);
    CHECK(p.slots[8].x == 0x30 + 0x20);
    CHECK(p.slots[8].y == 0x10 + 0x44);
    CHECK(p.slots[8].z == 0x46);
    CHECK(p.slots[10].frame == 0x12);
    CHECK(p.slots[10].x == 0x30 + 0x40);
    CHECK(p.slots[10].z == 0x3C);
    p.shackPartner1X = 0x36;  // the second partner is tested against the FIRST partner's x
    p.slots[10].frame = 0xFFFF;
    cellV49Showcase(g);
    CHECK(p.slots[10].frame == 0xFFFF);
}

TEST_CASE("dos world: level boundaries switch g_level and the key state") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    // Hiding slot 15 holds the unfound level-1 key, slot 3 the level-2 key.
    const auto slot = [&](int i) { return g.tables.world.hidingCells[static_cast<size_t>(i)]; };
    SUBCASE("CF trunk (P46): up from scene 0, down from scene 1") {
        CHECK(w.player(0x7C8, 0x678, 0, -8) == 0x5F3);
        CHECK(g.core.level == 1);
        CHECK(g.core.keyState == 4);  // no key 0x68 in slots 13..29
        g.core.level = 0;
        g.world.cellContent[slot(15)] = 0x68;
        w.player(0x7C8, 0x678, 0, -8);
        CHECK(g.core.keyState == 0x0A);
        g.world.cellContent[slot(15)] = 0xE8;  // searched
        w.player(0x7C8, 0x678, 0, -8);
        CHECK(g.core.keyState == 4);
        g.world.loadedScene = 1;
        w.player(0x7C8, 0x678, 0, 8);
        CHECK(g.core.level == 0);
        CHECK(g.core.keyState == 2);
        g.core.level = 1;
        w.player(0x7C8, 0x660, 0, 8);  // not below 0x68: nothing
        CHECK(g.core.level == 1);
    }
    SUBCASE("C7 shaft (P47) and 94 shaft (P19)") {
        g.world.loadedScene = 1;
        CHECK(w.player(0x3C0, 0x678, 0, 8) == 0x1400);
        CHECK(g.core.level == 0);
        g.world.loadedScene = 1;
        CHECK(w.player(0x240, 0x4F8, 0, -8) == 0x1400);
        CHECK(g.core.level == 2);
        CHECK(g.core.keyState == 6);
        g.world.cellContent[slot(3)] = 0x6A;
        w.player(0x240, 0x4F8, 0, -8);
        CHECK(g.core.keyState == 0x0C);
        g.world.loadedScene = 2;
        w.player(0x240, 0x4F8, 0, 8);
        CHECK(g.core.level == 1);
        CHECK(g.core.keyState == 4);
    }
    SUBCASE("4D rock shaft (P23): the ice-cave drop lands in the mine tunnel at y 0x270 exactly") {
        g.core.level = 2;
        CHECK(w.player(0x6A8, 0x268, 0, 8) == 0x1400);
        CHECK(w.p().y == 0x268);
        CHECK(w.player(0x6A8, 0x270, 0, 8) == 0);
        CHECK(w.p().x == 0x4C0);
        CHECK(w.p().y == 0x170);
        CHECK(w.p().dy == 0);
        CHECK(g.core.level == 1);
        CHECK(g.core.area == 0x0E);
        CHECK(g.world.transition == 1);
        CHECK(w.player(0x6A8, 0x270, 0, 8) == 0x1400);  // only on level 2
    }
}

// ------------------------------------------------------------------------------ captures

TEST_CASE("dos world: the bonus spot pays five coins once at x 0x348 (ega_r2_bonus_coins)") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    g.core.coins = 5;
    std::vector<uint16_t> paidAt;
    for (int pass = 0; pass < 2; ++pass)
        for (uint16_t x = 0x300; x < 0x380; x = static_cast<uint16_t>(x + 8)) {
            const uint16_t before = g.core.coins;
            CHECK(w.player(x, 0x170, 4) == 0xFC);
            if (g.core.coins != before) paidAt.push_back(x);
        }
    CHECK(paidAt == std::vector<uint16_t>{0x348});
    CHECK(g.core.coins == 10);
    CHECK(g.world.cellPicture[0x26] == 0x49);
    CHECK(w.sounds() == calls({{Op::SfxPlay, 4}}));
}

TEST_CASE("dos world: the bridges break at 4 stars (ega_r2_bridge_break_l3 / _l2)") {
    TM_REQUIRE_DOS_GAME_FILES();
    // Walks right over the bridge, then falls straight down 8 rows per step with the scene loaded
    // after each boundary, as Scene_Update would; returns the x where it broke.
    auto run = [](World& w, uint16_t y0, uint16_t level, uint16_t cell, uint16_t landY, uint16_t landLevel) {
        DosGame& g = w.g;
        g.core.tier = 2;
        g.core.level = level;
        g.world.loadedScene = level;
        g.player.playerState = 12;
        uint16_t breakX = 0;
        uint16_t x = static_cast<uint16_t>((cell & 15) * 0x80);
        for (; breakX == 0 && x < (cell & 15) * 0x80 + 0x80; x = static_cast<uint16_t>(x + 8))
            if (w.player(x, y0, 4) == 0x1400) breakX = x;
        CHECK(g.world.cellPicture[cell] == 0x30);
        CHECK(w.sfxCount(9) == 1);
        uint16_t y = y0, mask = 0x1400;
        while ((mask & 0x1000) != 0 && y < 0x800) {
            mask = w.player(breakX, y, 0, 8);
            g.world.loadedScene = g.core.level;
            if ((mask & 0x1000) != 0) y = static_cast<uint16_t>(y + 8);
        }
        CHECK(y == landY);
        CHECK(mask == 0xFC);
        CHECK(g.core.level == landLevel);
        return breakX;
    };
    SUBCASE("level 3 (84): breaks at x 0x230 and drops through 94/A4 into B4, level 2") {
        World w(*gameFiles);
        CHECK(run(w, 0x470, 2, 0x84, 0x5F0, 1) == 0x230);
        CHECK(w.g.core.keyState == 4);
    }
    SUBCASE("level 2 (B7): breaks at x 0x3B0 and drops through C7/D7 into E7, level 1") {
        World w(*gameFiles);
        CHECK(run(w, 0x5F0, 1, 0xB7, 0x770, 0) == 0x3B0);
        CHECK(w.g.core.keyState == 2);
    }
    SUBCASE("jumps over the middle, a lower tier and the slow CPU keep it whole") {
        World w(*gameFiles);
        DosGame& g = w.g;
        g.core.tier = 2;
        g.player.playerState = 0x13;  // jumping right
        CHECK(w.player(0x240, 0x470, 4) == 0xFC);
        g.player.playerState = 12;
        g.core.tier = 1;
        CHECK(w.player(0x240, 0x470, 4) == 0xFC);
        g.core.tier = 2;
        g.core.slowCpu = 1;
        CHECK(w.player(0x240, 0x470, 4) == 0xFC);
        CHECK(g.world.cellPicture[0x84] == 0x2F);
        g.core.slowCpu = 0;
        CHECK(w.player(0x240, 0x470, 4) == 0x1400);  // standing on the middle breaks it
        w.player(0x240, 0x470, 0);
        CHECK(w.sfxCount(9) == 2);  // on every such tick
    }
    SUBCASE("the rail decoration (visible call)") {
        World w(*gameFiles);
        DosGame& g = w.g;
        g.world.cellCallKind = 1;
        g.world.cellScreenX = 0x22;
        g.world.cellScreenY = 0x10;
        g.world.decorCount = 0;
        cellP45V45Bridge(g);
        const SpriteSlot& s = g.player.slots[12];
        CHECK(s.x == 0x22 + 0x1C);
        CHECK(s.y == 0x10 + 0x73);
        CHECK(s.z == 0x1E);
        CHECK(s.frame == 0x178);
        CHECK(g.world.decorCount == 1);
    }
}

TEST_CASE("dos world: the geyser hole slides the player down to (0x740, 0x560) (ega_r2_geyser_hole_slide)") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles, 2);
    DosGame& g = w.g;
    g.core.keyState = 0x0C;
    g.world.cellPicture[0xAE] = 0x35;
    // Walking left from x 0x770 at y 0x3F0 (8 px per step) until a cell handler starts the slide.
    g.player.playerState = 0x0B;
    uint16_t x = 0x770, slideX = 0;
    uint16_t mask = 0;
    for (; slideX == 0 && x > 0x700; x = static_cast<uint16_t>(x - 8)) {
        mask = w.player(x, 0x3F0, -4);
        if (g.player.playerState == 0x1E) slideX = x;
    }
    CHECK(slideX == 0x750);
    CHECK(mask == 0xCF4);
    // The slide (PlrSt_30 stand-in): 8 rows on the first tick, then one step per 7 ticks while the
    // previous tick's mask has 0x800; Player_Move calls Cell_GetMask before applying dy.
    uint16_t y = 0x3F0;
    x = w.p().x;
    int tick = 0, snapTick = -1;
    while ((mask & 0x800) != 0 && tick < 1000) {
        const int16_t dy = (tick % 7 == 0) ? 8 : 0;
        mask = w.player(x, y, 0, dy);
        if (snapTick < 0 && w.p().x == 0x740) snapTick = tick;
        x = w.p().x;
        y = static_cast<uint16_t>(w.p().y + w.p().dy);
        g.world.loadedScene = g.core.level;  // the frame loads the scene a boundary asked for
        ++tick;
    }
    CHECK(snapTick == 0);  // the next tick snaps to 0x740 and moves 8 rows (mask 0xCF4)
    CHECK(x == 0x740);
    CHECK(y == 0x560);
    CHECK(g.core.level == 1);
    CHECK(g.core.keyState == 4);
    // Jumping over the middle is safe; elves turn (not asserted: Elf_Turn is the elves module's).
    g.core.level = 2;
    g.world.loadedScene = 2;
    g.player.playerState = 0x11;
    CHECK(w.player(0x740, 0x3F0, -4) == 0x4FC);
    CHECK(g.player.playerState == 0x11);
}

// ------------------------------------------------------------------------------ net shop

TEST_CASE("dos world: the net shop takes four coins and hands out five nets") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    g.core.shopCoinsOwed = 4;
    g.core.nets = 0;
    CHECK(w.player(0x540, 0x770) == 0x2FC);  // in front of EA: a coin pays (0x200)
    CHECK(g.world.cellPicture[0xEA] == 0x47);
    g.core.shopCoinsOwed = 1;
    w.player(0x540, 0x770);
    w.player(0x540, 0x770);
    CHECK(g.world.cellPicture[0xEA] == 0x48);
    CHECK(g.world.cellPicture[0xBC] == 0x48);
    CHECK(g.world.fullRedrawReq == 2);  // a redraw on every tick with one coin owed
    g.core.shopCoinsOwed = 0;
    w.player(0x540, 0x770);  // a redraw is still pending: not yet open
    CHECK(g.world.cellPicture[0xEA] == 0x48);
    g.world.fullRedrawReq = 0;
    CHECK(w.player(0x540, 0x770) == 0x2BC);  // opens; no coin drop once paid
    CHECK(g.world.cellPicture[0xEA] == 0x46);
    CHECK(g.world.cellPicture[0xBC] == 0x46);
    CHECK(w.player(0x540, 0x770) == 0xBD);  // open: Up enters, a coin is a normal search
    CHECK(w.player(0x548, 0x760, 0, -1) == 0xFE);  // climbing in
    CHECK(w.p().x == 0x540);
    CHECK(g.core.nets == 5);
    CHECK(g.core.shopCoinsOwed == -1);
    w.player(0x540, 0x760, 0, -1);
    CHECK(g.core.nets == 5);  // once
    w.player(0x540, 0x760, 0, 1);  // climbing back down closes both shops
    CHECK(g.core.shopCoinsOwed == 4);
    CHECK(g.world.cellPicture[0xEA] == 0x47);
    CHECK(g.world.cellPicture[0xBC] == 0x47);
    w.player(0x650, 0x5E0, 0, 0);  // BC on level 1 snaps to 0x640
    CHECK(w.p().x == 0x640);
    const uint16_t before = g.world.cellMask;
    w.elf(0, 0x540, 0x770, 1);  // the shop ignores elves
    CHECK(g.world.cellMask == before);

    // The sign (visible call): digits 4..1, the net when paid, nothing after the nets were taken.
    g.world.cellCallKind = 1;
    g.world.cellScreenX = 0x40;
    g.world.cellScreenY = 0x10;
    const SpriteSlot& s = g.player.slots[12];
    struct Sign {
        int16_t owed;
        uint16_t redraw, frame;
    };
    for (const Sign& t : {Sign{4, 0, 0x18E}, Sign{3, 0, 0x18C}, Sign{1, 0, 0x188}, Sign{0, 0, 0x194}, Sign{0, 1, 0x186},
                          Sign{-1, 0, 0xFFFF}}) {
        g.core.shopCoinsOwed = t.owed;
        g.world.fullRedrawReq = t.redraw;
        cellP36V36NetShop(g);
        CHECK(s.frame == t.frame);
    }
    CHECK(s.x == 0x40 + 0x1E);
    CHECK(s.y == 0x10 + 0x3A);
    CHECK(s.z == 0x50);
}

// ------------------------------------------------------------------------------ castle

TEST_CASE("dos world: the castle hall, wall and top") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles, 2);
    DosGame& g = w.g;
    SUBCASE("hall ladders (P56)") {
        g.world.cellPicture[0x74] = 0x27;
        CHECK(w.player(0x240, 0x3F0) == 0x20FF);
        CHECK(w.player(0x248, 0x3E8) == 0x24FF);
        CHECK(w.p().x == 0x240);  // centred on the ladder
        CHECK(w.player(0x240, 0x3A0) == 0x25FF);
        CHECK(w.player(0x210, 0x3A0) == 0x35FC);  // outside the ladder column
        g.world.cellPicture[0x74] = 0x2C;
        CHECK(w.player(0x240, 0x3F0, 0, 2) == 0xFC);
        // A visible call removes a creature still showing.
        g.player.slots[6].anim = 0x3C;
        g.world.creatureCell = 2;
        g.world.cellCallKind = 1;
        cellP56V56CastleHall(g);
        CHECK(g.player.slots[6].anim == 0);
        CHECK(g.player.slots[6].frame == 0xFFFF);
        CHECK(g.player.slots[7].frame == 0xFFFF);
        CHECK(g.world.creatureCell == 0);
    }
    SUBCASE("wall masks by picture (P57)") {
        struct M {
            uint16_t pic, y, mask;
        };
        for (const M& m : {M{0x31, 0x2A0, 0x25FF}, M{0x2D, 0x2A0, 0x11F0}, M{0x2D, 0x2E0, 0x25FE}, M{0x2D, 0x2F8, 0x25FF},
                           M{0x25, 0x2A0, 0x25F3}, M{0x25, 0x2C0, 0x25FD}, M{0x2A, 0x2A0, 0x15F0}}) {
            CAPTURE(m.pic);
            CAPTURE(m.y);
            g.world.cellPicture[0x52] = m.pic;
            CHECK(w.player(0x140, m.y) == m.mask);
        }
        CHECK(w.player(0x110, 0x2A0) == 0x1400);  // outside the ladder column
    }
    SUBCASE("the creature's strike costs a coin and forges a ladder jump") {
        g.world.cellPicture[0x52] = 0x31;
        g.core.coins = 3;
        g.player.playerScrY = 0x60;
        g.player.playerScrX = 0x40;
        g.player.slots[6].x = 0x30;
        g.player.slots[6].y = 0x50;
        g.world.creatureStrike = 1;
        g.keys() = 0;
        w.player(0x140, 0x2A0);
        CHECK(g.world.creatureStrike == 0);
        CHECK(g.core.coins == 2);
        CHECK(g.keys() == 8);
        CHECK(w.sounds() == calls({{Op::SfxPlay, 5}}));
        g.player.slots[6].x = 0x50;
        g.world.creatureStrike = 1;
        g.keys() = 0;
        g.core.coins = 0;  // broke: no coin, no sound, still knocked away
        w.player(0x140, 0x2A0);
        CHECK(g.keys() == 4);
        CHECK(w.sfxCount(5) == 1);
        g.player.slots[6].y = 0x80;  // out of reach
        g.world.creatureStrike = 1;
        g.keys() = 0;
        w.player(0x140, 0x2A0);
        CHECK(g.world.creatureStrike == 0);
        CHECK(g.keys() == 0);
        g.player.slots[6].y = 0x50;
        g.player.playerState = 0x21;  // ladder-jumping
        g.world.creatureStrike = 1;
        w.player(0x140, 0x2A0);
        CHECK(g.keys() == 0);
    }
    SUBCASE("the top row: only the exit picture runs the hit check; climbing out ends the climb") {
        g.world.cellPicture[0x33] = 0x31;
        g.world.creatureStrike = 1;
        CHECK(w.player(0x1C0, 0x1A0) == 0x25FF);
        CHECK(g.world.creatureStrike == 1);  // the ladder pictures skip Castle_CreatureHitCheck
        g.world.cellPicture[0x33] = 0x32;
        CHECK(w.player(0x1C0, 0x1F0) == 0x25F3);
        CHECK(g.world.creatureStrike == 0);
        g.ui.userRec = 0;
        g.rec().totalTreasures = 10;
        g.core.treasuresFound = 3;
        g.core.keyState = 6;
        g.world.slowCpuSaved = 1;
        CHECK(w.player(0x1C0, 0x1F0, 0, -8) == 0x1400);  // x 0x400 is outside the column
        CHECK(w.p().x == 0x400);
        CHECK(w.p().y == 0x500);
        CHECK(w.p().dy == 0);
        CHECK(g.world.cameraX == 0x400);
        CHECK(g.core.level == 3);
        CHECK(g.core.keyState == 0);
        CHECK(g.core.area == 0);
        CHECK(g.world.transition == 2);
        CHECK(g.core.menuLock == 1);
        CHECK(g.player.playerState == 0x29);
        CHECK(g.core.slowCpu == 1);
        CHECK(g.world.slowScroll == 1);
        CHECK(g.rec().totalTreasures == 13);
    }
    SUBCASE("the creature appears at the player's cell with chance (rand() & mask[tier]) == 1") {
        g.core.tier = 0;
        g.world.cellScreenX = 0x42;
        g.world.cellScreenY = 0x10 - 0x40;
        g.player.slots[1].x = 0x62;  // the player at this cell's centre
        g.world.creatureCell = 0;
        g.world.castleCellIdx = 0;
        const uint32_t s0 = g.rand.seed();
        castleCreatureSpawn(g);
        CHECK(g.rand.seed() == s0);  // tier 0: no draw
        CHECK(g.world.castleCellIdx == 1);
        g.core.tier = 3;
        g.world.castleCellIdx = 0;
        castleCreatureSpawn(g);
        CHECK(g.rand.seed() == seedAfter(s0, 1));
        // A seed whose next rand() & 0x3F is 1.
        BorlandRand r;
        uint32_t seed = 1;
        for (;; ++seed) {
            r.setRawSeed(seed);
            if ((r.rand() & 0x3F) == 1) break;
        }
        g.rand.setRawSeed(seed);
        g.world.castleCellIdx = 1;
        g.world.decorCount = 0;
        castleCreatureSpawn(g);
        CHECK(g.world.creatureCell == 2);
        CHECK(g.player.slots[6].x == 0x42);
        CHECK(g.player.slots[6].y == 0x10 - 0x40 + 0x70);
        CHECK(g.player.slots[6].z == 0x50);
        CHECK(g.player.slots[7].z == 0x4F);
        CHECK(g.world.decorCount == 1);
        // At its cell index again with the strike script over: removed, then re-anchored.
        const uint32_t s1 = g.rand.seed();
        g.world.castleCellIdx = 1;
        g.player.slots[6].anim = 0;
        g.world.cellScreenX = 0x30;
        castleCreatureSpawn(g);
        CHECK(g.world.creatureCell == 0);
        CHECK(g.player.slots[6].frame == 0xFFFF);
        CHECK(g.player.slots[6].x == 0x30);
        CHECK(g.rand.seed() == s1);
        // At the left neighbour's centre the test is (rand() & mask) == 0.
        g.player.slots[1].x = 0x10;  // slot1.x + 0x20 == cellScreenX
        g.world.castleCellIdx = 0;
        castleCreatureSpawn(g);
        CHECK(g.rand.seed() == seedAfter(s1, 1));
    }
}

// ------------------------------------------------------------------------------ visible cells

TEST_CASE("dos world: Cells_UpdateVisible places the decorations of the camera's cell row") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    SpriteSlot& s = g.player.slots[12];
    SUBCASE("the animated decoration of E6 and the cell positions") {
        g.world.cameraX = 0x300;
        g.world.cameraY = 0x700;
        cellsUpdateVisible(g);
        CHECK(g.world.cellCallKind == 1);
        CHECK(g.world.decorCount == 1);
        CHECK(s.x == 0x22);
        CHECK(s.y == 0x90);
        CHECK(g.world.cellScreenX == 2 + 3 * 0x40);
        CHECK(g.world.cellScreenY == 0x10);
        g.world.cameraX = 0x308;
        g.world.cameraY = 0x710;
        cellsUpdateVisible(g);
        CHECK(g.world.cellScreenY == 0);
        CHECK(s.x == -2 + 0x20);
    }
    SUBCASE("rock columns of the level-0 cave: the rightmost static decoration wins") {
        g.world.cameraX = 0x480;
        g.world.cameraY = 0;
        cellsUpdateVisible(g);
        CHECK(g.world.decorCount == 2);
        CHECK(s.x == 2 + 2 * 0x40);
        CHECK(s.y == 0x90);
        CHECK(s.frame == 0x176);
        CHECK(s.z == 0x1E);
        CHECK(s.owner == 12);
    }
    SUBCASE("walls, the treetop and the post") {
        g.world.cameraX = 0x200;
        g.world.cameraY = 0x80;  // 14 (left wall) .. 17
        cellsUpdateVisible(g);
        CHECK(s.frame == 0x17A);
        CHECK(s.x == 1);
        CHECK(s.y == 0x88);
        g.world.cameraX = 0x780;
        g.world.cameraY = 0x600;  // CF treetop
        cellsUpdateVisible(g);
        CHECK(s.frame == 0x17C);
        CHECK(s.x == 0x22);
        CHECK(s.y == 0x80);
        g.world.cameraX = 0x480;
        g.world.cameraY = 0x100;  // 29 post
        cellsUpdateVisible(g);
        CHECK(s.frame == 0x17E);
        CHECK(s.x == 2 + 0x18);
    }
    SUBCASE("no decoration clears slot 12; a slow CPU draws none of the optional ones; UI modes stop it") {
        s.frame = 0x17E;
        s.anim = 0x53;
        s.prev = 7;
        g.world.cameraX = 0x400;
        g.world.cameraY = 0x780;
        cellsUpdateVisible(g);
        CHECK(s.frame == 0xFFFF);
        CHECK(s.anim == 0);
        CHECK(s.prev == 0);
        g.core.slowCpu = 1;
        g.world.cameraX = 0x480;
        g.world.cameraY = 0;
        cellsUpdateVisible(g);
        CHECK(g.world.decorCount == 0);
        g.core.uiMode = 0x40;
        g.world.castleCellIdx = 3;
        cellsUpdateVisible(g);
        CHECK(g.world.castleCellIdx == 3);
    }
    SUBCASE("visible calls see the stale g_curObj (the geyser top 7E)") {
        g.core.level = 2;
        g.world.loadedScene = 2;
        g.player.obj = Obj{0x740, 0x3F0, -4, 0};
        g.player.playerState = 0x0B;
        g.world.curObj = DsPtr{kPlayerObjOff};  // Elves_Update returned early: the player is left
        g.world.cameraX = 0x680;
        g.world.cameraY = 0x380;  // 7E is the second visible cell
        cellsUpdateVisible(g);
        CHECK(g.player.playerState == 0x1E);
        g.player.playerState = 0x0B;
        g.world.curObj = DsPtr{elfObjOff(14)};  // normally the last elf slot
        cellsUpdateVisible(g);
        CHECK(g.player.playerState == 0x0B);
    }
}

// ------------------------------------------------------------------------------ camera

TEST_CASE("dos world: Frame_Begin latches the frame and the camera's tile position") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    WorldState& s = g.world;
    s.cameraX = 0xFFF8;  // -8: column 15 of the row above
    s.cameraY = 0x784;
    s.camStepX = -4;
    s.camStepY = 2;
    g.core.level = 1;
    s.transition = 2;
    s.partialRedrawReq = 1;
    s.inShelfRoom = {0, 1, 0, 0};
    g.player.rankState = 1;
    g.ui.hudRedraw = 0;
    g.ui.hudRedrawReq2 = 1;
    g.stepGate().mark();
    frameBegin(g);
    CHECK(s.frameCamX == 0xFFF8);
    CHECK(s.frameCamY == 0x784);
    CHECK(s.frameCamStepX == -4);
    CHECK(s.frameCamStepY == 2);
    CHECK(s.sceneWanted == 1);
    CHECK(s.frameTransition == 2);
    CHECK(s.framePartialRedraw == 1);
    CHECK(s.frameInShelfRoom[1] == 1);
    CHECK(s.frameRankState == 1);
    CHECK(g.ui.hudRedraw == 1);
    CHECK_FALSE(g.stepDone());
    CHECK(s.camCellCol == 15);
    CHECK(s.camCellRow16 == 0xE0);
    CHECK(s.camTileColOff == 0x1E0);
    CHECK(s.camTileRowOff == ((0x784 >> 2) & 0x1E));
    s.fullRedrawReq = 1;  // a redraw frame does not scroll
    frameBegin(g);
    CHECK(s.fullRedraw == 1);
    CHECK(s.frameCamStepX == 0);
    CHECK(s.frameCamStepY == 0);
}

TEST_CASE("dos world: incremental scrolling equals a full redraw at the new camera") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    g.pan = &gameFiles->pan("MOUNTAIN");
    g.ui.hudRedraw = 0;
    struct Path {
        uint16_t camX, camY;
        int sx, sy, steps;
        bool slowScroll, slowCpu;
    };
    for (const Path& p : {Path{0x300, 0x700, 8, 0, 20, false, false},   // right across a cell edge
                          Path{0x3C0, 0x700, -8, 0, 20, false, false},  // left
                          Path{0x780, 0x680, 0, 8, 20, false, false},   // down the trunk
                          Path{0x780, 0x6F0, 0, -8, 20, false, false},  // up
                          Path{0x300, 0x710, 8, 0, 3, false, false},    // a camera between two cell rows
                          Path{0x300, 0x700, 16, 0, 10, true, false},   // g_slowScroll (after a ladder jump)
                          Path{0x3C0, 0x700, -16, 0, 10, true, false},
                          Path{0x780, 0x680, 0, 16, 10, true, false},
                          Path{0x300, 0x700, 16, 0, 10, true, true}}) {  // slow CPU: rows 112..143 stay
        CAPTURE(p.camX);
        CAPTURE(p.camY);
        CAPTURE(p.sx);
        CAPTURE(p.sy);
        g.world.slowScroll = p.slowScroll ? 1 : 0;
        g.core.slowCpu = p.slowCpu ? 1 : 0;
        redrawAt(g, p.camX, p.camY);
        for (int i = 0; i < p.steps; ++i) {
            const int dirty = g.core.dirty;
            scrollBy(g, p.sx, p.sy);
            CHECK(g.core.dirty == dirty + 1);
            CHECK(g.world.drawnCamX == g.world.cameraX);
            CHECK(g.world.drawnCamY == g.world.cameraY);
            const int lastRow = p.slowCpu ? 0x6F : 0x8F;
            const auto scrolled = playfield(g.back, lastRow);
            BackBuffer saved = g.back;
            redrawAt(g, g.world.cameraX, g.world.cameraY);
            CHECK(scrolled == playfield(g.back, lastRow));
            if (p.slowCpu) {  // the bottom 32 rows keep the picture of before the scroll
                bool same = true;
                for (int y = 0x70; y <= 0x8F && same; ++y)
                    same = std::equal(saved.row(y) + 2, saved.row(y) + 0x9E, g.back.row(y) + 2);
                CHECK_FALSE(same);
            }
            g.back = saved;
        }
    }
    // Nothing while a UI mode or a HUD redraw is pending.
    g.core.slowCpu = 0;
    g.world.slowScroll = 0;
    redrawAt(g, 0x300, 0x700);
    const auto before = playfield(g.back);
    g.ui.hudRedraw = 1;
    scrollBy(g, 8, 0);
    CHECK(playfield(g.back) == before);
}

TEST_CASE("dos world: the partial redraw of a searched cell") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    g.pan = &gameFiles->pan("MOUNTAIN");
    redrawAt(g, 0x300, 0x700);
    const auto full = playfield(g.back);
    g.back.clear(0x77);
    g.player.obj = Obj{0x3A0, 0x770, 0, 0};  // cell E7, its left edge at screen byte 0x42
    g.player.searchSide = 0;
    g.world.fullRedrawReq = 1;
    g.world.partialRedrawReq = 1;
    frameBegin(g);
    playfieldRedraw(g);
    CHECK(g.world.partialRedrawReq == 0);
    CHECK(g.world.framePartialRedraw == 0);
    CHECK(g.world.fullRedrawReq == 0);
    // Columns 0x42..0x85 (the cell plus one tile column) are redrawn, the rest kept.
    for (int y : {0x10, 0x40, 0x8F})
        for (int x : {0x2, 0x41, 0x42, 0x60, 0x85, 0x86, 0x9D}) {
            CAPTURE(x);
            CAPTURE(y);
            const bool inside = x >= 0x42 && x < 0x86;
            const uint8_t want = inside ? full[static_cast<size_t>((y - 0x10) * 0x9C + (x - 2))] : uint8_t{0x77};
            CHECK(g.back.byteAt(x, y) == want);
        }
}

TEST_CASE("dos world: the chute shows the stale MOUNT3 shaft under CLOSE.PAN (code_world.md §6.10)") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles, 3);
    DosGame& g = w.g;
    // The first frame of the fall: camera (0x600, 0x180), cells 3C..3E; 3D (MOUNT3 block 0x43) is
    // drawn at screen bytes 0x42..0x81.
    REQUIRE((g.world.cellPicture[0x3D] & 0x7F) >= gameFiles->pan("CLOSE").pictureCount());
    auto cellBytes = [&](const PanFile* pan, const PanFile* prev) {
        g.pan = pan;
        g.prevPan = prev;
        redrawAt(g, 0x600, 0x180);
        std::vector<uint8_t> out;
        for (int y = 0x10; y <= 0x8F; ++y) out.insert(out.end(), g.back.row(y) + 0x42, g.back.row(y) + 0x82);
        return out;
    };
    const auto stale = cellBytes(&gameFiles->pan("CLOSE"), &gameFiles->pan("MOUNT3"));
    const auto mount3 = cellBytes(&gameFiles->pan("MOUNT3"), nullptr);
    const auto none = cellBytes(&gameFiles->pan("CLOSE"), nullptr);
    CHECK(stale == mount3);  // CLOSE.PAN overwrites none of the bytes the block uses
    CHECK(stale != none);
    // Further down the fall, 4D (MOUNT3 block too) and 5D (a CLOSE block) are both readable.
    g.pan = &gameFiles->pan("CLOSE");
    g.prevPan = &gameFiles->pan("MOUNT3");
    redrawAt(g, 0x600, 0x240);
    const auto bothRows = playfield(g.back);
    g.pan = &gameFiles->pan("MOUNT3");
    g.prevPan = nullptr;
    redrawAt(g, 0x600, 0x240);
    CHECK(playfield(g.back) != bothRows);  // 5D is CLOSE's own block
}

// ------------------------------------------------------------------------------ dissolve

TEST_CASE("dos world: the dissolve draws 700 random numbers and ends with the whole picture") {
    TM_REQUIRE_DOS_GAME_FILES();
    SUBCASE("reference speed: spread over 7 lockstep frames") {
        World w(*gameFiles);
        DosGame& g = w.g;
        g.back.clear(0x5A);
        g.screen.clear(0);
        g.world.transition = 1;
        const uint32_t s0 = g.rand.seed();
        int frames = 0;
        bool running = true;
        while (running && frames < 50) {
            g.frameClock().beginFrame(5);  // one game tick of PIT ticks
            running = transitionDissolve(g);
            ++frames;
            if (running) CHECK(transitionDissolveRunning(g));
        }
        CHECK(frames == 7);
        CHECK(g.rand.seed() == seedAfter(s0, 700));
        CHECK(g.world.transition == 0);
        CHECK_FALSE(transitionDissolveRunning(g));
        CHECK(std::equal(g.back.bytes().begin(), g.back.bytes().end(), g.screen.bytes().begin()));
    }
    SUBCASE("cells land on the 16 x 16 grid of the play field") {
        World w(*gameFiles);
        DosGame& g = w.g;
        g.back.clear(0x5A);
        g.screen.clear(0);
        g.world.transition = 1;
        g.frameClock().beginFrame(5);
        CHECK(transitionDissolve(g));
        int copied = 0;
        for (int y = 0; y < BackBuffer::kHeight; ++y)
            for (int x = 0; x < BackBuffer::kRowBytes; ++x)
                if (g.screen.byteAt(x, y) != 0) {
                    ++copied;
                    CHECK(y >= 0x10);
                    CHECK(y < 0x90);
                    CHECK(x >= 2);
                    CHECK(x < 0x9E);
                }
        CHECK(copied > 0);
    }
    SUBCASE("period speed: all 350 cells in one frame") {
        dosgame::FixtureOptions o = noInit();
        o.options.frameSpeed = FrameSpeed::Period;
        dosgame::GameFixture f(*gameFiles, o);
        DosGame& g = f.g();
        g.world.transition = 1;
        const uint32_t s0 = g.rand.seed();
        g.frameClock().beginFrame(5);
        CHECK_FALSE(transitionDissolve(g));
        CHECK(g.rand.seed() == seedAfter(s0, 700));
    }
    SUBCASE("slow CPU: no cells and no rand()") {
        World w(*gameFiles);
        DosGame& g = w.g;
        g.core.slowCpu = 1;
        g.world.transition = 2;
        g.back.clear(0x33);
        const uint32_t s0 = g.rand.seed();
        g.frameClock().beginFrame(5);
        CHECK_FALSE(transitionDissolve(g));
        CHECK(g.rand.seed() == s0);
        CHECK(g.world.transition == 0);
        CHECK(g.screen.byteAt(50, 50) == 0x33);
    }
}

// ------------------------------------------------------------------------------ scenes

TEST_CASE("dos world: Scene_Update loads the PAN and sprite bank of the wanted scene") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    g.core.startDrive = 2;
    g.pan = &gameFiles->pan("MOUNT3");
    g.bank = &gameFiles->pak("SPRITES");
    g.world.loadedScene = 2;
    g.world.sceneWanted = 3;
    const uint16_t found = g.ui.hudFound;
    sceneUpdate(g);
    CHECK(g.world.loadedScene == 3);
    CHECK(g.pan == &gameFiles->pan("CLOSE"));
    CHECK(g.prevPan == &gameFiles->pan("MOUNT3"));
    CHECK(g.bank == &gameFiles->pak("CLOSE"));
    CHECK(g.core.quit == 0);
    CHECK(g.core.musicLoop == 1);
    CHECK(g.ui.hudFound == static_cast<uint16_t>(found - 1));
    g.world.sceneWanted = 4;
    sceneUpdate(g);
    CHECK(g.pan == &gameFiles->pan("SHACK"));
    CHECK(g.prevPan == &gameFiles->pan("CLOSE"));
    CHECK(g.bank == &gameFiles->pak("SHACK"));
    g.world.sceneWanted = 0;
    sceneUpdate(g);
    CHECK(g.pan == &gameFiles->pan("MOUNTAIN"));
    CHECK(g.bank == &gameFiles->pak("SPRITES"));
    const PakFile* bank = g.bank;
    g.world.sceneWanted = 1;  // between scenes 0..2 the bank stays
    sceneUpdate(g);
    CHECK(g.pan == &gameFiles->pan("MOUNT2N3"));
    CHECK(g.bank == bank);
    g.core.uiMode = 0x40;  // no load while a UI mode is open
    g.world.sceneWanted = 2;
    sceneUpdate(g);
    CHECK(g.world.loadedScene == 1);
}

TEST_CASE("dos world: Scene_Update blanks the menu bar for scenes 3/4 and restores it") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    g.core.startDrive = 2;
    g.pan = &gameFiles->pan("MOUNT3");
    g.back.clear(0x12);
    g.world.fullRedraw = 1;
    g.core.menuLock = 1;
    g.world.loadedScene = 2;
    g.world.sceneWanted = 3;
    g.core.dirty = 0;
    sceneUpdate(g);
    for (int y = 0; y <= 0x0A; ++y) CHECK(g.back.byteAt(80, y) == 0xFF);
    CHECK(g.back.byteAt(80, 0x0B) == 0x12);
    CHECK(g.ui.promptIcon == 6);
    CHECK(g.world.menuBarHidden == 1);
    CHECK(g.core.dirty >= 1);
    const uint16_t prompt = g.ui.hudPrompt;
    g.world.sceneWanted = 0;
    sceneUpdate(g);
    CHECK(g.world.menuBarHidden == 0);
    CHECK(g.ui.hudPrompt == static_cast<uint16_t>(prompt + 1));
}

TEST_CASE("dos world: Scene_Update starts the next game after the treasure room") {
    TM_REQUIRE_DOS_GAME_FILES();
    World w(*gameFiles);
    DosGame& g = w.g;
    g.core.startDrive = 2;
    g.pan = &gameFiles->pan("SHACK");
    g.world.loadedScene = 4;
    g.world.sceneWanted = 0;
    g.core.endState = 2;
    g.ui.hudRedraw = 0;
    sceneUpdate(g);
    CHECK(g.core.endState == 0);
    CHECK(g.elves.messagePending == 2);
    CHECK(g.ui.hudRedraw == 1);
    REQUIRE(w.sounds().size() >= 2);
    CHECK(w.sounds()[0] == Call{Op::MusicStop, 0});
    CHECK(w.sounds()[1] == Call{Op::SfxStop, 0});
    CHECK(g.world.loadedScene == 0);
}
