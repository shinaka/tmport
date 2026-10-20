// Wave 3, the actors (re/gg/docs/code_world.md §9-§13): the step interpreter op by op on synthetic
// sequences, Actors_Tick's order and countdowns, walls, contacts and probes on synthetic rooms, the
// player's brain and the chimps' brain on the shipped tables of ssg.exe, the chimps' set-up and spawn
// (their rand() calls against the w0 capture), carried parts, room changes, the building block, and
// replays from the DOSBox-X states (the chimps' positions and rand() calls). Only counts, coordinates and
// sequence numbers here.
#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/actors.h"
#include "gg/game/chimps.h"
#include "gg/game/collision.h"
#include "gg/game/game.h"
#include "gg/game/parts.h"
#include "gg/game/player.h"
#include "gg/game/rooms.h"
#include "gg/game/world.h"
#include "gg/save/gg_state_file.h"
#include "gg/save/players_dat.h"
#include "gg_game_fixture.h"
#include "gg_test_data.h"
#include "rtl/borland_rand.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

// ---- synthetic sequences and sets -------------------------------------------------------------------

AnimStep frameStep(int16_t op, int16_t dx = 0, int16_t dy = 0, uint16_t flags = 0) { return AnimStep{op, dx, dy, flags, 0}; }
AnimStep command(int16_t op, int16_t a = 0, int16_t b = 0) { return AnimStep{op, a, b, 0, 0}; }

AnimSequence sequence(std::vector<AnimStep> steps, int16_t delay = 2, int16_t dir = 0) {
    AnimSequence s;
    s.delay = delay;
    s.dir = dir;
    s.steps = std::move(steps);
    return s;
}

// A set of 8 frames (frame k is 10 + k wide and 20 + k high) with the sequences given; `fill` more
// sequences of one frame each (frame k % 8) so that any sequence number of the shipped tables exists.
ActorSet syntheticSet(std::vector<AnimSequence> seqs, int fill = 68) {
    ActorSet s;
    for (int k = 0; k < 8; ++k) {
        s.widths.push_back(static_cast<int16_t>(10 + k));
        s.heights.push_back(static_cast<int16_t>(20 + k));
    }
    s.sequences = std::move(seqs);
    while (static_cast<int>(s.sequences.size()) < fill) {
        const auto k = static_cast<int16_t>(s.sequences.size() % 8);
        s.sequences.push_back(sequence({frameStep(k), command(-1)}));
    }
    return s;
}

// The player's and the projectile's sets (both scales) replaced by `set`.
void usePlayerSet(GgGame& g, const ActorSet& set) {
    ActorsObjects& o = g.actorsObjects();
    for (const uint16_t id : {kPlayerKind.hallSet, kPlayerKind.roomSet, kProjectileKind.hallSet, kProjectileKind.roomSet})
        o.sets[id] = set;
    o.playerSetsLoaded = {true, true, true, true};
}
// The chimps' sets (both scales) replaced by `set`.
void useChimpSet(GgGame& g, const ActorSet& set) {
    ActorsObjects& o = g.actorsObjects();
    o.sets[kChimpKind.hallSet] = set;
    o.sets[kChimpKind.roomSet] = set;
    o.chimpSetHall = o.chimpSeqHall = kChimpKind.hallSet;
    o.chimpSetRoom = o.chimpSeqRoom = kChimpKind.roomSet;
}

// ---- synthetic rooms ---------------------------------------------------------------------------------

// Room 0 the workshop, 1 a hall, 2 a small room, 3 a small room under hall floors 3..5; all empty.
RoomSet emptyRooms() {
    RoomSet s;
    s.rooms.resize(4);
    s.rooms[1].flags = 1;
    s.rooms[3].flags = 2;
    return s;
}

void useRooms(GgGame& g, RoomSet set, int displayRoom = 1) {
    BorlandRand scratch;  // no rand() call: no items are dealt
    g.world.rooms.load(std::move(set), {0}, g.world.parts, g.world.deck, scratch);
    g.world.roomCount = static_cast<int16_t>(g.world.rooms.count());
    g.world.rooms.displayRoom = displayRoom;
}

// A static object of layer 0 with a hit record; returns its index.
int addObject(Room& room, int16_t type, HitRecord hit, uint16_t link = 0, int x = -1, int y = -1) {
    RoomObject o;
    o.type = type;
    o.hit = hit;
    o.link = link;
    o.sprite.x = x < 0 ? hit.x : x;
    o.sprite.y = y < 0 ? hit.y : y;
    room.layers[0].push_back(o);
    return static_cast<int>(room.layers[0].size()) - 1;
}
// An animated object of list 0 (a pad or a flap); returns its index.
int addAnim(Room& room, HitRecord hit, int x, int y) {
    RoomAnim a;
    a.hit = hit;
    a.sprite.x = x;
    a.sprite.y = y;
    room.anims[0].push_back(a);
    return static_cast<int>(room.anims[0].size()) - 1;
}
// The floor references of a floor: (animated, layer or list, index) in order.
struct Ref {
    Ref(bool a, int i, bool s = false) : anim(a), index(i), skip(s) {}
    bool anim = false;
    int index = 0;
    bool skip = false;
};
void setFloorRefs(Room& room, int floor, std::initializer_list<Ref> refs, int first = 0) {
    room.floorRefFirst[static_cast<size_t>(floor)] = static_cast<uint16_t>(first);
    room.floorRefCount[static_cast<size_t>(floor)] = static_cast<uint16_t>(refs.size());
    int k = first;
    for (const Ref& r : refs)
        room.floorRefs[static_cast<size_t>(k++)] =
            static_cast<uint16_t>((r.skip ? 0x8000 : 0) | (r.anim ? 0x4000 : 0) | (r.index << 2));
}
// An item of room r at (x, y, w, h); returns its pool index.
int addItem(GgGame& g, int r, int kind, int x, int y, int w = 10, int h = 7) {
    Rooms& rooms = g.world.rooms;
    const int node = rooms.allocItem();
    REQUIRE(node >= 0);
    ItemNode& n = rooms.pool()[static_cast<size_t>(node)];
    n.free = false;
    n.kind = kind;
    n.pictureId = ItemNode::kSmallPicture;
    n.sprite = Sprite{0, x, y, w, h, {}};
    n.hit = HitRecord{0x800, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)};
    Room& room = rooms.room(r);
    room.items.push_back(node);
    ++room.itemCount;
    return node;
}

// Actor i in room r at anchor (x, y) on a floor, in sequence `seq` at step 0, with walls and contacts.
Actor& place(GgGame& g, int i, int room, int x, int y, int floor, int seq = 0) {
    Actor& a = g.actors.actors[static_cast<size_t>(i)];
    a.disabled = 0;
    a.room = room;
    a.anchorX = a.sprite.x = x;
    a.anchorY = a.sprite.y = y;
    a.floor = floor;
    a.wallsOn = 1;
    a.contactsOn = 1;
    a.blocked = 0;
    a.id = i;
    actorSelectScale(g, i, roomIsHall(g, room));
    actorSetSequence(g, i, seq);
    return a;
}

// The calls of a recording facade by name.
int countCalls(const RecordingSound& s, std::string_view op) {
    return static_cast<int>(std::count_if(s.calls().begin(), s.calls().end(), [&](const auto& c) { return c.op == op; }));
}

// ---- states -------------------------------------------------------------------------------------------

// A DOSBox-X state into the fixture's game with the modules' restores of the building (the world's: the
// catalogue, the rooms, the items, the HUD; the actors': the sets, the scales, the bundles).
bool loadBuildingState(gggame::GameFixture& f, const std::filesystem::path& sav) {
    if (!f.loadState(sav)) return false;
    const GgStateFile s = GgStateFile::load(sav);
    std::string why;
    if (!worldRestoreFromState(f.g(), s, why)) {
        MESSAGE("world restore: " << why);
        return false;
    }
    if (!actorsRestoreFromState(f.g(), s, why)) {
        MESSAGE("actors restore: " << why);
        return false;
    }
    return true;
}

// The fields of an actor a state and the engine must agree on (the DGROUP record's).
std::string actorLine(const Actor& a) {
    return std::format("room {} floor {} seq {} step {} anchor ({}, {}) xy ({}, {}) wh ({}, {}) flags {:#x} cd {} delay {} dis {} "
                       "pend {} exit {} prev {} cool {} walls {} cont {} blk {} door {} pad {} edge {} proom {} item {} "
                       "c48 {:#x} dir {} wx {} wb {} car {} f94 {:#x}",
                       a.room, a.floor, a.seq, a.step, a.anchorX, a.anchorY, a.sprite.x, a.sprite.y, a.sprite.w,
                       a.sprite.h, a.sprite.flags, a.countdown, a.delay, a.disabled, a.pendingSeq, a.exitSeq, a.prevSeq,
                       a.doorCooldown, a.wallsOn, a.contactsOn, a.blocked, a.doorObj, a.pad, a.edgeObj, a.prevRoom,
                       a.item, a.contacts, a.dir, a.wallX, a.wallBits, a.carried, a.field94);
}

// rand() calls from LCG state a to state b (at most `limit`), or -1.
int randCallsBetween(uint32_t a, uint32_t b, int limit = 100000) {
    BorlandRand r;
    r.setRawSeed(a);
    for (int n = 0; n <= limit; ++n) {
        if (r.seed() == b) return n;
        r.rand();
    }
    return -1;
}

}  // namespace

// =============================================================================== the step interpreter

TEST_CASE("actors: a frame step moves the anchor, sizes the sprite and advances the step (1834:05ae)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    useRooms(g, emptyRooms(), 2);
    usePlayerSet(g, syntheticSet({sequence({frameStep(2, 5, -3, 2), frameStep(3, 4), command(-1)})}));
    Actor& a = place(g, 0, 2, 100, 50, 0);
    CHECK(actorRunStep(g, 0) == 0);
    CHECK(a.anchorX == 105);
    CHECK(a.sprite.x == 105);
    CHECK(a.sprite.y == 47);
    CHECK(a.anchorY == 50);
    CHECK(a.sprite.w == 12);
    CHECK(a.sprite.h == 22);
    CHECK(a.sprite.flags == 2);
    CHECK(a.step == 1);
    CHECK(a.frame == 2);
    CHECK(a.blocked == 0);
    CHECK(actorRunStep(g, 0) == 0);
    CHECK(a.anchorX == 109);
    CHECK(a.sprite.y == 50);
    CHECK(a.step == 2);
    // -1: back to step 0, which is the next frame.
    CHECK(actorRunStep(g, 0) == 0);
    CHECK(a.anchorX == 114);
    CHECK(a.step == 1);
    // The screen clamp: x >= 1 and x + w <= 319.
    usePlayerSet(g, syntheticSet({sequence({frameStep(7, -9), frameStep(7, 40), command(-1)})}));
    place(g, 0, 2, 5, 50, 0);
    actorRunStep(g, 0);
    CHECK(a.anchorX == 1);
    CHECK(a.sprite.x == 1);
    a.anchorX = 290;
    actorRunStep(g, 0);
    CHECK(a.sprite.w == 17);
    CHECK(a.anchorX == 319 - 17);
    CHECK(a.sprite.x == 319 - 17);
}

TEST_CASE("actors: walls stop the sweep of a frame step and set +0x32 (1834:0615, 1adb:0093)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    RoomSet set = emptyRooms();
    set.rooms[2].walls[0].push_back(WallRecord{0, 50, 0, 10, 200});  // right of the actor
    set.rooms[2].walls[0].push_back(WallRecord{0, 20, 0, 10, 200});  // left of it
    set.rooms[3].walls[1].push_back(WallRecord{0, 50, 0, 10, 200});  // floor 4 of a room under hall floors 3..5
    useRooms(g, std::move(set), 2);
    usePlayerSet(g, syntheticSet({sequence({frameStep(0, 8), command(-1)}, 2, 0), sequence({frameStep(0, -6), command(-1)}, 2, 0),
                                  sequence({frameStep(0, -6), command(-1)}, 2, 2), sequence({frameStep(0, 2), command(-1)}, 2, 0)}));
    // Moving right into the wall at 50: the sweep (35, w 18) sticks out at the left (1): stand left of it.
    Actor& a = place(g, 0, 2, 35, 50, 0, 0);
    actorRunStep(g, 0);
    CHECK(a.wallBits == 1);
    CHECK(a.wallX == 50);
    CHECK(a.anchorX == 50 - 10 + 1);
    CHECK(a.sprite.x == 41);
    CHECK(a.blocked == 1);
    // Moving left into the wall at 20..29: the sweep (27, w 7) sticks out at the right (2): its right
    // edge - 1 (the argument -1), the actor right of it.
    place(g, 0, 2, 33, 50, 0, 1);
    actorRunStep(g, 0);
    CHECK(a.wallBits == 2);
    CHECK(a.wallX == 29);
    CHECK(a.anchorX == 30);
    CHECK(a.blocked == -1);
    // The same with a sequence whose direction is 2: the left stop does not apply, +0x32 stays.
    place(g, 0, 2, 33, 50, 0, 2);
    a.blocked = 1;
    actorRunStep(g, 0);
    CHECK(a.wallBits == 2);
    CHECK(a.anchorX == 27);
    CHECK(a.blocked == 1);
    // Free: +0x32 = 0. Walls off: no test at all.
    place(g, 0, 2, 100, 50, 0, 3);
    a.blocked = -1;
    actorRunStep(g, 0);
    CHECK(a.wallBits == 0);
    CHECK(a.blocked == 0);
    place(g, 0, 2, 35, 50, 0, 0);
    a.wallsOn = 0;
    a.blocked = 1;
    actorRunStep(g, 0);
    CHECK(a.anchorX == 43);
    CHECK(a.blocked == 1);
    // A small room counts floors from hall floor 3 on: floor 4 is its list 1.
    place(g, 0, 3, 35, 50, 4, 0);
    actorRunStep(g, 0);
    CHECK(a.anchorX == 41);
    CHECK(a.blocked == 1);
    // A sweep that sticks out on both sides takes the side of +0x32, else the sequence's direction.
    HitRecord wide{0, 45, 10, 30, 10};
    a.blocked = -1;
    CHECK(actorCollideWalls(g, 0, wide, -1) == 2);
    CHECK(a.wallX == 59);
    a.blocked = 0;
    CHECK(actorCollideWalls(g, 0, wide, -1) == 1);
    CHECK(a.wallX == 50);
}

TEST_CASE("actors: the step interpreter's commands (1834:01b6, table CS:06d7)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    RoomSet set = emptyRooms();
    const int door = addObject(set.rooms[2], 9, HitRecord{4, 60, 40, 10, 30});
    const int edge = addObject(set.rooms[2], 5, HitRecord{1, 150, 70, 20, 4});
    const int pad = addAnim(set.rooms[2], HitRecord{0x80, 200, 60, 20, 6}, 200, 60);
    set.rooms[2].anims[0][static_cast<size_t>(pad)].sprite.flags = 0x14;
    set.rooms[2].anims[0][static_cast<size_t>(pad)].countdown = 7;
    useRooms(g, std::move(set), 2);
    Actor& a = g.actors.actors[0];

    SUBCASE("moves: -12 -13 -14 -15, the delay -24, walls -16 -17, contacts -28") {
        usePlayerSet(g, syntheticSet({sequence({command(-12, 7), command(-13, -4, 1), command(-24, 5), command(-17),
                                                command(-28, 0), frameStep(1), command(-14, 33), command(-15, 44),
                                                command(-16), frameStep(1), command(-1)})}));
        place(g, 0, 2, 100, 50, 2);
        a.blocked = 1;
        actorRunStep(g, 0);
        CHECK(a.anchorX == 107);
        CHECK(a.anchorY == 46);
        CHECK(a.floor == 3);
        CHECK(a.delay == 5);
        CHECK(a.wallsOn == 0);
        CHECK(a.contactsOn == 0);
        CHECK(a.step == 6);
        actorRunStep(g, 0);
        CHECK(a.anchorX == 33);
        CHECK(a.anchorY == 44);
        CHECK(a.wallsOn == 1);
        CHECK(a.blocked == 0);
        CHECK(a.step == 10);
    }
    SUBCASE("jumps: -4 step a, -1 step 0, -6 a new sequence, -7 the sequence before") {
        usePlayerSet(g, syntheticSet({sequence({command(-4, 2), frameStep(0), frameStep(1), command(-6, 1)}),
                                      sequence({frameStep(5), command(-7)}, 4, -2)}));
        place(g, 0, 2, 100, 50, 2, 0);
        actorRunStep(g, 0);
        CHECK(a.frame == 1);
        CHECK(a.step == 3);
        actorRunStep(g, 0);  // -6 1: sequence 1 (its delay and direction), its first frame
        CHECK(a.seq == 1);
        CHECK(a.prevSeq == 0);
        CHECK(a.delay == 4);
        CHECK(a.dir == -2);
        CHECK(a.frame == 5);
        CHECK(a.step == 1);
        actorRunStep(g, 0);  // -7: back to sequence 0 (which jumps to its step 2)
        CHECK(a.seq == 0);
        CHECK(a.prevSeq == 1);
        CHECK(a.frame == 1);
        CHECK(a.step == 3);
    }
    SUBCASE("-8 hides until the next frame step; -25 disables without advancing") {
        usePlayerSet(g, syntheticSet({sequence({command(-8), frameStep(1, 0, 0, 2), command(-25)})}));
        place(g, 0, 2, 100, 50, 2);
        g.player.live.projectiles = 3;
        CHECK(actorRunStep(g, 0) == 0);
        CHECK(a.hidden());
        CHECK(a.step == 1);
        actorRunStep(g, 0);
        CHECK_FALSE(a.hidden());
        CHECK(a.sprite.flags == 2);
        actorRunStep(g, 0);
        CHECK(a.disabled == 1);
        CHECK(a.step == 2);
        CHECK(g.ui.hints.pending() == 0);
        // With no banana left the hint 0x1051 is queued.
        a.disabled = 0;
        g.player.live.projectiles = 0;
        actorRunStep(g, 0);
        CHECK(g.ui.hints.pending() == 1);
    }
    SUBCASE("objects: -26 the door, -41 / -40 the edge, -21 the pad, -9 the pad's restart") {
        usePlayerSet(g, syntheticSet({sequence({command(-26), frameStep(0), command(-41), frameStep(0), command(-40, 3),
                                                frameStep(0), command(-21, 5, -7), frameStep(0),
                                                command(-21, static_cast<int16_t>(0x8000), 2), frameStep(0), command(-9),
                                                frameStep(0), command(-46, 4), frameStep(0)})}));
        place(g, 0, 2, 100, 50, 2);
        a.doorObj = door;
        a.edgeObj = edge;
        a.pad = pad;
        actorRunStep(g, 0);
        CHECK(a.anchorX == 60);
        actorRunStep(g, 0);
        CHECK(a.anchorX == 154);
        actorRunStep(g, 0);
        CHECK(a.anchorX == 150 + 13 - 7);
        actorRunStep(g, 0);
        CHECK(a.anchorX == 205);
        CHECK(a.anchorY == 53);
        actorRunStep(g, 0);
        CHECK(a.anchorX == 205);
        CHECK(a.anchorY == 62);
        const RoomAnim& p = g.world.rooms.room(2).anims[0][static_cast<size_t>(pad)];
        CHECK(p.countdown == 7);
        actorRunStep(g, 0);
        CHECK(p.countdown == 0);
        CHECK(p.sprite.flags == 0);
        CHECK(p.pos == 1);
        actorRunStep(g, 0);  // -46: the right edge of a small room minus W[4]
        CHECK(a.anchorX == 0x13C - 14 + 0);
    }
    SUBCASE("-42 jumps on a floor edge contact; -43 queues hints") {
        usePlayerSet(g, syntheticSet({sequence({command(-42, 3), frameStep(1), command(-1), frameStep(2), command(-43, -1),
                                                command(-43, 0x104D), frameStep(3)})}));
        place(g, 0, 2, 100, 50, 2);
        a.contacts = 0;
        actorRunStep(g, 0);
        CHECK(a.frame == 1);
        place(g, 0, 2, 100, 50, 2);
        a.contacts = 1;
        actorRunStep(g, 0);
        CHECK(a.frame == 2);
        g.player.withChimps = 1;
        actorRunStep(g, 0);
        CHECK(a.frame == 3);
        CHECK(g.ui.hints.pending() == 2);
    }
    SUBCASE("the stun loop: -31 counts g_chimpStunLoops[g_level], -32 loops, -30 the banana goes") {
        usePlayerSet(g, syntheticSet({sequence({command(-30), command(-31), frameStep(1), command(-32), frameStep(2)})}));
        place(g, 0, 2, 100, 50, 2);
        g.player.level = 3;
        g.player.live.projectiles = 4;
        g.actors.actors[kProjectileActor].disabled = 0;
        const int loops = g.tables.chimpStunLoops[3];
        REQUIRE(loops > 1);
        int frames = 0;
        do {
            actorRunStep(g, 0);
            ++frames;
        } while (a.frame == 1 && frames < 1000);
        CHECK(frames == loops + 1);
        CHECK(g.actors.actors[kProjectileActor].disabled == 1);
        CHECK(a.loopCount == 0);
        CHECK(a.loopStart == 2);
    }
    SUBCASE("sounds: -36 plays effect a unless it plays, -37 stops it, in the displayed room only") {
        usePlayerSet(g, syntheticSet({sequence({command(-36, 3, 0x0105), command(-37, 3), frameStep(1)})}));
        g.GgState::sound.effectBank = 40;
        place(g, 0, 2, 100, 50, 2);
        f.sound().clear();
        actorRunStep(g, 0);
        REQUIRE(f.sound().calls().size() == 2);
        CHECK(f.sound().calls()[0] == RecordingSound::Call{"playEffect", {43, 1, 5}});
        CHECK(f.sound().calls()[1] == RecordingSound::Call{"stopEffect", {43}});
        f.sound().clear();
        f.sound().setPlayingSource([](int h) { return h == 43 ? 1 : 0; });
        place(g, 0, 2, 100, 50, 2);
        actorRunStep(g, 0);
        CHECK(countCalls(f.sound(), "playEffect") == 0);
        CHECK(countCalls(f.sound(), "stopEffect") == 1);
        f.sound().clear();
        g.world.rooms.displayRoom = 1;
        place(g, 0, 2, 100, 50, 2);
        actorRunStep(g, 0);
        CHECK(f.sound().calls().empty());
    }
    SUBCASE("ops that hang the original stop the step") {
        for (const int16_t op : {int16_t{-45}, int16_t{-44}, int16_t{-39}, int16_t{-38}, int16_t{-35}, int16_t{-34}, int16_t{-20},
                                 int16_t{-19}, int16_t{-18}, int16_t{-5}, int16_t{-3}, int16_t{-2}, int16_t{-47}, int16_t{-1000}}) {
            usePlayerSet(g, syntheticSet({sequence({command(-12, 1), command(op), frameStep(1)})}));
            place(g, 0, 2, 100, 50, 2);
            CHECK(actorRunStep(g, 0) == 0);
            CHECK(a.step == 1);
            CHECK(a.anchorX == 101);
        }
        // A sequence of commands without a frame: the engine gives up (the original loops for ever).
        usePlayerSet(g, syntheticSet({sequence({command(-12, 1), command(-1)})}));
        place(g, 0, 2, 100, 50, 2);
        CHECK(actorRunStep(g, 0) == 0);
    }
}

TEST_CASE("actors: -11 ends the player's sequence (leave the room) and restarts another actor (1834:03b2)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    RoomSet set = emptyRooms();
    addObject(set.rooms[1], 9, HitRecord{4, 60, 40, 10, 30}, static_cast<uint16_t>((5 << 6) | 2));   // an open door to room 2
    addObject(set.rooms[2], 9, HitRecord{4, 120, 60, 20, 40}, static_cast<uint16_t>((5 << 6) | 1));  // its twin
    useRooms(g, std::move(set), 1);
    const ActorSet s = syntheticSet({sequence({frameStep(1), command(-11)})});
    usePlayerSet(g, s);
    useChimpSet(g, s);
    Actor& p = place(g, 0, 1, 60, 40, 5);
    p.doorObj = 0;
    actorRunStep(g, 0);
    CHECK(actorRunStep(g, 0) == 1);  // Player_SequenceEnd: through the open door into room 2
    CHECK(p.step == 1);
    CHECK(p.room == 2);
    CHECK(p.prevRoom == 1);
    CHECK(p.pendingSeq == 33);
    CHECK(p.exitSeq == 33);
    CHECK(g.player.live.hintCancel1048 == 1);
    // A chimp: Chimp_SequenceEnd, Chimp_EnterRoom (sequence 21 at the twin), then its first frame.
    g.player.level = 0;
    Actor& c = place(g, 2, 1, 60, 40, 5);
    c.doorObj = 0;
    c.pendingSeq = -1;
    g.actors.roomChimpCount.fill(0);
    g.actors.roomChimpCount[1] = 1;
    actorRunStep(g, 2);
    CHECK(actorRunStep(g, 2) == 0);
    CHECK(c.room == 2);
    CHECK(c.prevRoom == 1);
    CHECK(c.seq == 21);
    CHECK(c.doorCooldown == 0x2DA);
    CHECK(c.step == 1);  // restarted in sequence 21 and showed its first frame
    CHECK(g.actors.roomChimpCount[1] == 0);
    CHECK(g.actors.roomChimpCount[2] == 1);
    CHECK(c.anchorX == 120 + 10);  // the twin in a small room: + 10, small chimps + 18
    CHECK(c.anchorY == 60 + 18);
}

// =============================================================================== Actors_Tick

TEST_CASE("actors: Actors_Tick runs contacts, brain, the new sequence and the countdown in slot order (1834:0ae9)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    useRooms(g, emptyRooms(), 2);
    usePlayerSet(g, syntheticSet({}));
    Actor& p = place(g, 0, 2, 100, 50, 2, 0);
    p.countdown = 0;
    // Disabled actors are not ticked.
    for (int i = 1; i < kActorCount; ++i) g.actors.actors[static_cast<size_t>(i)].disabled = 1;
    // The shipped transitions of sequence 0: Right held turns the player (sequence 9), started now.
    g.keyboard().isr().heldKeys = 2;
    CHECK(actorsTick(g) == 0);
    CHECK(p.seq == 9);
    CHECK(p.prevSeq == 0);
    CHECK(p.step == 1);
    CHECK(p.countdown == p.delay);
    // A delay of 2 steps every third tick (the countdown reloads with the delay at each step).
    g.keyboard().isr().heldKeys = 0;
    std::vector<int> countdowns;
    for (int t = 0; t < 7; ++t) {
        actorsTick(g);
        countdowns.push_back(p.countdown);
    }
    CHECK(countdowns == std::vector<int>{1, 0, 2, 1, 0, 2, 1});
    CHECK(p.seq == 9);  // sequence 9 has no transitions: the brain answers -1
}

TEST_CASE("actors: the player's brain: held keys, contacts, immunity, jump spots (18f5:029b)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    RoomSet set = emptyRooms();
    set.rooms[1].jumpsLeft[5].push_back(JumpRecord{12, 90, 14, 30, 3});
    useRooms(g, std::move(set), 1);
    usePlayerSet(g, syntheticSet({}));
    Actor& p = place(g, 0, 1, 100, 130, 5, 0);
    p.sprite.w = 10;
    const auto brain = [&](uint16_t keys, uint16_t contacts) {
        g.keyboard().isr().heldKeys = keys;
        p.contacts = contacts;
        return playerBrain(g, 0);
    };
    // Sequence 0 (stand): Left turns (8), Right turns (9), Up at a locked door walks into it (34), Up at
    // an open door goes through (32), nothing: -1.
    CHECK(brain(1, 0) == 8);
    CHECK(brain(2, 0) == 9);
    CHECK(brain(4, 8) == 34);
    CHECK(brain(4, 4) == 32);
    CHECK(brain(0, 0) == -1);
    // A chimp's touch knocks him down unless he is immune; immunity counts down in the brain.
    CHECK(brain(0, 0x2000) == 49);
    p.immune = 2;
    CHECK(brain(0, 0x2000) == -1);
    CHECK(p.immune == 1);
    // Up + Left in sequence 6 (facing left) jumps (12); over a jump spot of group 1 it jumps onto the
    // spot's pad (14) and +0x38 = the pad.
    actorSetSequence(g, 0, 6);
    p.anchorX = p.sprite.x = 200;
    CHECK(brain(5, 0) == 12);
    p.sprite.x = 100;
    CHECK(brain(5, 0) == 14);
    CHECK(p.pad == 3);
    // Space throws a banana (22 facing left; the step -27 of the sequence launches it).
    CHECK(brain(0x20, 0) == 22);
    actorSetSequence(g, 0, 7);
    CHECK(brain(0x20, 0) == 23);
    // The idle bit: after 0x111c ticks without a key, once.
    actorSetSequence(g, 0, 0);
    g.input().resetIdleTick();
    g.rt.advanceTicks(0x111D);
    CHECK(brain(0, 0) == 1);
    CHECK(brain(0, 0) == -1);
    CHECK(projectileBrain(g, 1) == -1);
}

// =============================================================================== collision

TEST_CASE("actors: contacts: items, actors of the contact list, floor objects in order (1adb:03b0)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    RoomSet set = emptyRooms();
    Room& r = set.rooms[2];
    const int door = addObject(r, 7, HitRecord{8, 95, 40, 10, 30});
    const int edge = addObject(r, 5, HitRecord{1, 108, 60, 10, 4});
    const int hole = addObject(r, 6, HitRecord{2, 95, 40, 10, 30});
    const int pad = addAnim(r, HitRecord{0x100, 95, 40, 10, 30}, 95, 40);
    useRooms(g, std::move(set), 2);
    usePlayerSet(g, syntheticSet({}));
    useChimpSet(g, syntheticSet({}));
    Room& room = g.world.rooms.room(2);
    for (int i = 1; i < kActorCount; ++i) g.actors.actors[static_cast<size_t>(i)].disabled = 1;
    Actor& p = place(g, 0, 2, 100, 40, 0);
    p.sprite.w = 10;
    p.sprite.h = 25;
    // An item: 0x800 and its index.
    addItem(g, 2, 3, 300, 40);
    addItem(g, 2, 4, 104, 50);
    setFloorRefs(room, 0, {});
    actorUpdateContacts(g, 0);
    CHECK(p.contacts == 0x800);
    CHECK(p.item == 1);
    // A chimp on the same floor of the room, moving left: 0x1000 and +0x6e its id; another floor: none.
    Actor& c = place(g, 4, 2, 103, 45, 0);
    c.sprite.w = 10;
    c.sprite.h = 10;
    c.dir = -2;
    actorUpdateContacts(g, 0);
    CHECK(p.contacts == 0x1800);
    CHECK(p.toucher == 4);
    c.dir = 0;
    actorUpdateContacts(g, 0);
    CHECK(p.contacts == 0x2800);
    c.floor = 1;
    actorUpdateContacts(g, 0);
    CHECK(p.contacts == 0x800);
    c.floor = 0;
    c.contactsOn = 0;
    actorUpdateContacts(g, 0);
    CHECK(p.contacts == 0x800);
    // The floor objects in order: the locked door stops the scan (+0x36), the edge after it is not seen.
    setFloorRefs(room, 0, {{false, door}, {false, edge}});
    actorUpdateContacts(g, 0);
    CHECK(p.contacts == (0x800 | 8));
    CHECK(p.doorObj == door);
    p.edgeObj = -5;
    setFloorRefs(room, 0, {{false, door, true}, {false, edge}});
    actorUpdateContacts(g, 0);
    CHECK(p.contacts == (0x800 | 1));
    CHECK(p.edgeObj == edge);
    // The edge sticks out at the right of the actor (r = 2) and he walks left: only the edge bit stays.
    p.dir = -1;
    actorUpdateContacts(g, 0);
    CHECK(p.contacts == 1);
    p.dir = 1;
    actorUpdateContacts(g, 0);
    CHECK(p.contacts == (0x800 | 1));
    // A hole: +0x36 and +0x3a; a launch pad: +0x38.
    setFloorRefs(room, 0, {{false, hole}});
    actorUpdateContacts(g, 0);
    CHECK(p.contacts == (0x800 | 2));
    CHECK(p.doorObj == hole);
    CHECK(p.edgeObj == hole);
    setFloorRefs(room, 0, {{true, pad}});
    actorUpdateContacts(g, 0);
    CHECK(p.contacts == (0x800 | 0x100));
    CHECK(p.pad == pad);
    // A small room uses floor - 3: floor 3 is its list 0.
    p.floor = 3;
    c.floor = 3;
    c.contactsOn = 1;
    actorUpdateContacts(g, 0);
    CHECK(p.contacts == (0x800 | 0x100 | 0x2000));
}

TEST_CASE("actors: chimp probes: items assign 0x800, objects add their kinds, pads set +0x38 (1adb:01ef)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    RoomSet set = emptyRooms();
    Room& r = set.rooms[1];
    const int edge = addObject(r, 4, HitRecord{1, 130, 60, 10, 4});
    const int flap = addAnim(r, HitRecord{0x200, 140, 50, 10, 10}, 140, 50);
    useRooms(g, std::move(set), 1);
    useChimpSet(g, syntheticSet({}));
    Room& room = g.world.rooms.room(1);
    setFloorRefs(room, 2, {{false, edge}, {true, flap}}, 7);
    Actor& c = place(g, 2, 1, 100, 50, 2);
    c.contacts = 0x30;
    const HitRecord probe{0, 100, 50, 60, 20};
    CHECK(chimpProbeContacts(g, 2, probe) == (1 | 0x200));
    CHECK(c.pad == flap);
    CHECK(c.contacts == 0x30);  // not touched without an item
    addItem(g, 1, 3, 150, 60);
    CHECK(chimpProbeContacts(g, 2, probe) == (0x800 | 1 | 0x200));
    CHECK(c.contacts == 0x800);  // assigned: the other bits are gone
    CHECK(c.item == 0);
    CHECK(chimpProbeContacts(g, 2, HitRecord{0, 0, 0, 5, 5}) == 0);
}

// =============================================================================== the chimps

TEST_CASE("actors: Chimp_Brain on the shipped tables: the start, walking probes, doors, items, stays (2a80:0112)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    RoomSet set = emptyRooms();
    const int door = addObject(set.rooms[1], 9, HitRecord{4, 140, 40, 10, 30});
    useRooms(g, std::move(set), 1);
    useChimpSet(g, syntheticSet({}));
    Room& room = g.world.rooms.room(1);
    Actor& c = place(g, 2, 1, 100, 40, 1, 0);
    c.sprite.w = 20;
    c.sprite.h = 20;
    c.doorCooldown = -1;

    // Sequence 0: one unconditional entry, 500 per mille -> 2, else the default 3: one rand() call.
    BorlandRand expect = g.rand;
    const int v = expect.random(1000);
    auto calls = g.rand.calls();
    CHECK(chimpBrain(g, 2) == (v < 500 ? 2 : 3));
    CHECK(g.rand.calls() == calls + 1);

    // Walking left (sequence 2) in an empty room: no probe touches, no rand(), the default 2; blocked at
    // the left it turns (10).
    actorSetSequence(g, 2, 2);
    c.contacts = 0;
    calls = g.rand.calls();
    CHECK(chimpBrain(g, 2) == 2);
    c.blocked = -1;
    CHECK(chimpBrain(g, 2) == 10);
    c.blocked = 0;
    CHECK(g.rand.calls() == calls);

    // An item within the probe: every earlier probe sees it too (and assigns 0x800), only the item entry
    // fires (1000 per mille: always) -> 28; carrying a part it walks on (2).
    addItem(g, 1, 3, 95, 50);
    calls = g.rand.calls();
    CHECK(chimpBrain(g, 2) == 28);
    CHECK(g.rand.calls() == calls + 1);
    CHECK(c.contacts == 0x800);
    CHECK(c.field94 == 0x8000000u);
    c.carried = 1;
    CHECK(chimpBrain(g, 2) == 2);
    c.carried = 0;
    g.world.rooms.room(1).items.clear();
    g.world.rooms.room(1).itemCount = 0;

    // A door at the centre probe: 300 per mille, not while the cooldown runs (which counts down).
    setFloorRefs(room, 1, {{false, door}});
    c.anchorX = c.sprite.x = 132;  // the 4-px probe at 132 + 10 - 2 = 140
    c.doorCooldown = 5;
    calls = g.rand.calls();
    CHECK(chimpBrain(g, 2) == 2);
    CHECK(g.rand.calls() == calls);
    CHECK(c.doorCooldown == 4);
    c.doorCooldown = -1;
    int doorDraws = 0;
    for (int k = 0; k < 40; ++k) {
        BorlandRand e = g.rand;
        const int d = e.random(1000);
        calls = g.rand.calls();
        const int next = chimpBrain(g, 2);
        CHECK(g.rand.calls() == calls + 1);
        CHECK(next == (d < 300 ? 20 : 2));
        doorDraws += next == 20;
    }
    CHECK(doorDraws > 0);

    // The banana's touch (0x1000 / 0x2000, the contacts before the probes) stuns: 30.
    setFloorRefs(room, 1, {});
    c.contacts = 0x1000;
    CHECK(chimpBrain(g, 2) == 30);
    c.contacts = 0;
    // A stay list (26: falling) keeps the sequence; a launch pad's contact turns it into 38.
    actorSetSequence(g, 2, 26);
    CHECK(chimpBrain(g, 2) == 26);
    c.contacts = 0x100;
    CHECK(chimpBrain(g, 2) == 38);
    // No move list (sequence 30): -1.
    actorSetSequence(g, 2, 30);
    c.contacts = 0;
    CHECK(chimpBrain(g, 2) == -1);
}

TEST_CASE("actors: Chimps_SetupActors and Chimps_Spawn deal the w0 chimps: 3925 rand() calls from the seed (2a80:0635, 0784)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto sav = testdata::ggCapture("savestates/w0/09_blueprint.sav");
    if (!sav) {
        MESSAGE("w0/09_blueprint.sav missing: test skipped");
        return;
    }
    gggame::GameFixture ref(*ggFiles);
    REQUIRE(ref.loadState(*sav));
    const GgGame& want = ref.g();

    for (const bool cpu286 : {false, true}) {
        gggame::FixtureOptions o;
        if (cpu286) o.game.cpu = GgCpu::Cpu286;
        gggame::GameFixture f(*ggFiles, o);
        GgGame& g = f.g();
        // State 4 of the w0 tour: Aircraft level 0 (race 5), with chimps, from the seed 0x29CF.
        g.player.building = 1;
        g.player.level = 0;
        g.player.raceIndex = 5;
        g.player.withChimps = 1;
        g.rand.setRawSeed(0x29CF);
        g.world.parts = PartCatalogue::load(g.archive(), g.cache, 5);
        g.world.rooms.load(g.archive(), g.exe(), 5, g.world.parts, g.world.deck, g.rand);  // 3920 calls
        g.world.roomCount = static_cast<int16_t>(g.world.rooms.count());
        g.rand.random(100);  // g_setupCoin (12e0:0910)
        chimpsSetupActors(g);
        const auto before = g.rand.calls();
        chimpsSpawn(g);
        CHECK(g.rand.calls() - before == 4);
        CHECK(g.rand.seed() == want.rand.seed());
        const uint16_t firstRoom = chimpList(g, g.tables, 5).rooms[0];
        for (int i = kFirstChimpActor; i < kActorCount; ++i) {
            const Actor& a = g.actors.actors[static_cast<size_t>(i)];
            const Actor& w = want.actors.actors[static_cast<size_t>(i)];
            CAPTURE(i);
            CAPTURE(cpu286);
            CHECK(a.disabled == w.disabled);
            if (a.disabled) continue;
            CHECK(a.anchorX == w.anchorX);
            if (cpu286 && i == kFirstChimpActor) {
                // In room 0 (a small room's scale): the same rand() values give R(3) and the small rows.
                CHECK(a.room == 0);
                CHECK(a.anchorY == a.floor * 52 + 28);
            } else {
                CHECK(a.room == w.room);
                CHECK(a.floor == w.floor);
                CHECK(a.anchorY == w.anchorY);
            }
            CHECK(a.seq == w.seq);
            CHECK(a.prevSeq == w.prevSeq);
            CHECK(a.exitSeq == w.exitSeq);
            CHECK(a.pendingSeq == w.pendingSeq);
            CHECK(a.doorCooldown == w.doorCooldown);
            CHECK(a.delay == w.delay);
            CHECK(a.id == w.id);
            CHECK(a.toucher == w.toucher);
            CHECK(a.prevRoom == w.prevRoom);
            CHECK(a.field94 == w.field94);
            CHECK(a.spriteBase == w.spriteBase);
            CHECK(a.bundle == w.bundle);
        }
        if (cpu286) {
            // The 286 path halves the first room word of the list in place: 1 -> 0, and 0 at the next set-up.
            CHECK(firstRoom == 0);
            chimpsSpawn(g);
            CHECK(chimpList(g, g.tables, 5).rooms[0] == 0);
            CHECK(g.actors.roomChimpCount[0] == 1);
        } else {
            CHECK(g.actors.roomChimpCount == want.actors.roomChimpCount);
        }
    }
    // Without chimps: placed with the same rand() calls, disabled.
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    g.player.level = 0;
    g.player.raceIndex = 5;
    g.player.withChimps = 0;
    g.world.parts = PartCatalogue::load(g.archive(), g.cache, 5);
    BorlandRand scratch;
    g.world.rooms.load(g.archive(), g.exe(), 5, g.world.parts, g.world.deck, scratch);
    chimpsSetupActors(g);
    const auto before = g.rand.calls();
    chimpsSpawn(g);
    CHECK(g.rand.calls() - before == 4);
    CHECK(g.actors.actors[2].disabled == 1);
    CHECK(g.actors.actors[3].disabled == 1);
    CHECK(g.actors.actors[4].disabled == 1);
    CHECK(g.actors.actors[2].room == 1);
}

TEST_CASE("actors: the 286 path's chimp in room 0 draws one R(1000), then walks against the screen edge (PORTING_NOTES 5)") {
    GG_REQUIRE_DATA();
    gggame::FixtureOptions o;
    o.game.cpu = GgCpu::Cpu286;
    gggame::GameFixture f(*ggFiles, o);
    GgGame& g = f.g();
    g.player.building = 1;
    g.player.level = 0;
    g.player.raceIndex = 5;
    g.player.withChimps = 1;
    g.world.parts = PartCatalogue::load(g.archive(), g.cache, 5);
    BorlandRand scratch;
    g.world.rooms.load(g.archive(), g.exe(), 5, g.world.parts, g.world.deck, scratch);
    g.world.roomCount = static_cast<int16_t>(g.world.rooms.count());
    g.world.rooms.displayRoom = 1;
    actorsInit(g);
    chimpsSetupActors(g);
    chimpsSpawn(g);
    Actor& c = g.actors.actors[kFirstChimpActor];
    REQUIRE(c.room == 0);
    REQUIRE(c.disabled == 0);
    for (int i = 0; i < kActorCount; ++i)
        if (i != kFirstChimpActor) g.actors.actors[static_cast<size_t>(i)].disabled = 1;
    const auto calls = g.rand.calls();
    for (int t = 0; t < 600; ++t) {
        actorsTick(g);
        CHECK(c.room == 0);
        CHECK(c.sprite.x >= 1);
        CHECK(c.sprite.x + c.sprite.w <= 319);
    }
    CHECK(g.rand.calls() == calls + 1);
    CHECK((c.sprite.x == 1 || c.sprite.x + c.sprite.w == 319));
}

// =============================================================================== parts, rooms, the block

TEST_CASE("actors: carried parts: pairs, holders and the chimps' shared list (1834:08d7, 0990)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    g.world.parts = PartCatalogue::load(g.archive(), g.cache, 5);
    // Three parts of one category (each needed category has four).
    std::vector<int> same;
    for (const PartRecord& t : g.world.parts.parts) {
        same.clear();
        for (const PartRecord& r : g.world.parts.parts)
            if (r.category == t.category) same.push_back(r.index);
        if (same.size() >= 3) break;
    }
    REQUIRE(same.size() >= 3);
    const int a = same[0], b = same[1], c = same[2];
    g.world.parts.parts[static_cast<size_t>(a)].flags = 0x4000 | 0x0100;
    CHECK(actorAddPart(g, 0, a) == 0);
    CHECK(actorAddPart(g, 0, b) == 1);  // the second of its type
    CHECK(actorAddPart(g, 0, c) == 0);  // a third is refused
    CHECK(g.actors.playerParts == std::vector<int16_t>{static_cast<int16_t>(a), static_cast<int16_t>(b)});
    CHECK(g.actors.actors[0].carried == 2);
    CHECK(partRecord(g, a).flags == 0x4000);
    CHECK(partRecord(g, a).holder == 0);
    CHECK(partRecord(g, a).pair == static_cast<int16_t>(b | 0x8000));
    CHECK(partRecord(g, b).pair == a);
    // Removing the first of the pair: the second takes its place, the links go.
    const int other = std::find_if(g.world.parts.parts.begin(), g.world.parts.parts.end(),
                                   [&](const PartRecord& r) { return r.category != partRecord(g, a).category; })->index;
    g.actors.playerParts.clear();
    g.actors.actors[0].carried = 0;
    for (PartRecord& r : g.world.parts.parts) {
        r.holder = -1;
        r.pair = -1;
    }
    actorAddPart(g, 0, a);
    actorAddPart(g, 0, other);
    actorAddPart(g, 0, b);
    CHECK(actorRemovePart(g, 0, a, true) == 1);
    CHECK(g.actors.playerParts == std::vector<int16_t>{static_cast<int16_t>(b), static_cast<int16_t>(other)});
    CHECK(partRecord(g, a).holder == -1);
    CHECK(partRecord(g, b).pair == -1);
    CHECK(g.actors.actors[0].carried == 2);
    // The holder is checked unless the third argument is 0: the chimps' shared list.
    CHECK(actorAddPart(g, 3, a) == 0);
    CHECK(actorAddPart(g, 5, c) == 1);  // the second of its type in the chimps' list
    CHECK(g.actors.chimpParts.size() == 2);
    CHECK(g.actors.actors[3].carried == 1);
    actorRemovePart(g, 4, c, true);  // chimp 4 holds nothing
    CHECK(g.actors.chimpParts.size() == 2);
    CHECK(actorRemovePart(g, 4, c, false) == 1);  // whoever holds it: chimp 5's count goes down
    CHECK(g.actors.actors[5].carried == 0);
    CHECK(g.actors.chimpParts == std::vector<int16_t>{static_cast<int16_t>(a)});
}

TEST_CASE("actors: the building block's actor fields round trip; the restore sets the scale and the header (1834:0b5e, 0b95)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    useRooms(g, emptyRooms(), 1);
    usePlayerSet(g, syntheticSet({sequence({frameStep(0)}, 2, 0), sequence({frameStep(1)}, 6, -2)}));
    useChimpSet(g, syntheticSet({sequence({frameStep(0)}, 2, 0), sequence({frameStep(1)}, 7, 2)}));
    for (int i = 0; i < kActorCount; ++i) {
        Actor& a = g.actors.actors[static_cast<size_t>(i)];
        a.anchorX = 10 * i;
        a.anchorY = 5 + i;
        a.seq = 1;
        a.room = i % 3;
        a.floor = i % 6;
        a.prevSeq = -1;
        a.exitSeq = 33;
        a.step = i;
        a.doorCooldown = 700 - i;
        a.loopCount = 2;
        a.loopStart = 3;
        a.disabled = i & 1;
        a.pendingSeq = -1;
        a.immune = 150 - i;
        a.item = i;
        a.carried = 9;
    }
    BuildingBlock block;
    actorsSaveState(g, block);
    const auto saved = g.actors.actors;
    for (Actor& a : g.actors.actors) a = Actor{};
    actorsRestoreState(g, block);
    for (int i = 0; i < kActorCount; ++i) {
        const Actor& a = g.actors.actors[static_cast<size_t>(i)];
        const Actor& s = saved[static_cast<size_t>(i)];
        CAPTURE(i);
        CHECK(a.anchorX == s.anchorX);
        CHECK(a.step == s.step);
        CHECK(a.doorCooldown == s.doorCooldown);
        CHECK(a.disabled == s.disabled);
        CHECK(a.immune == s.immune);
        CHECK(a.item == s.item);
        CHECK(a.carried == 0);  // outside +0x14..+0x41: not in the block
        CHECK(a.hallSet == (a.room == 1));
        CHECK(a.delay == (i < 2 ? 6 : 7));
        CHECK(a.dir == (i < 2 ? -2 : 2));
    }
}

TEST_CASE("actors: Projectile_Launch and the crates dealt anew with the last banana (18f5:0895)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    useRooms(g, emptyRooms(), 1);
    usePlayerSet(g, syntheticSet({}));
    Actor& p = place(g, 0, 1, 100, 120, 4);
    Actor& b = g.actors.actors[kProjectileActor];
    b.disabled = 1;
    g.player.live.projectiles = 0;
    projectileLaunch(g, 1, 6);
    CHECK(b.disabled == 1);
    g.player.live.projectiles = 2;
    g.world.rooms.cratesLeft = 0;
    g.world.rooms.cratesTotal = 0;
    projectileLaunch(g, 3, 6);
    CHECK(b.disabled == 0);
    CHECK(b.anchorX == 106);
    CHECK(b.sprite.x == 106);
    CHECK(b.anchorY == p.anchorY);
    CHECK(b.room == 1);
    CHECK(b.floor == 4);
    CHECK(b.seq == 3);
    CHECK(b.step == 0);
    CHECK(b.countdown == 0);
    CHECK(b.contactsOn == 1);
    CHECK(b.pendingSeq == -1);
    CHECK(b.unused34 == -1);
    CHECK(b.hallSet);
    CHECK(g.player.live.projectiles == 1);
    CHECK((g.world.hud.dirty & 8) != 0);
    // The last banana with no crate left: g_cratesTotal crates dealt to random rooms (rand), the count
    // restored. On the rooms of race 5.
    g.player.raceIndex = 5;
    g.world.parts = PartCatalogue::load(g.archive(), g.cache, 5);
    BorlandRand scratch;
    g.world.rooms.load(g.archive(), g.exe(), 5, g.world.parts, g.world.deck, scratch);
    g.world.roomCount = static_cast<int16_t>(g.world.rooms.count());
    g.world.rooms.displayRoom = 1;
    const int total = g.world.rooms.cratesTotal;
    REQUIRE(total > 0);
    const auto crates = [&] {
        int n = 0;
        for (int r = 0; r < g.world.rooms.count(); ++r)
            for (const ItemNode* it : g.world.rooms.itemsOf(r)) n += it->kind == ItemNode::kCrate;
        return n;
    };
    const int before = crates();
    g.world.rooms.cratesLeft = 0;
    g.player.live.projectiles = 1;
    const auto calls = g.rand.calls();
    projectileLaunch(g, 3, 6);
    CHECK(g.player.live.projectiles == 0);
    CHECK(g.world.rooms.cratesLeft == total);
    CHECK(crates() == before + total);
    CHECK(g.rand.calls() > calls + static_cast<uint64_t>(20 * 21 * total));
}

TEST_CASE("actors: Player_SequenceEnd and Player_EnterRoom through doors, passages, holes and the tube (18f5:04cf, 0409)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    RoomSet set = emptyRooms();
    const int locked = addObject(set.rooms[1], 7, HitRecord{8, 40, 100, 10, 30}, static_cast<uint16_t>((3 << 6) | 2));
    const int open = addObject(set.rooms[1], 9, HitRecord{4, 80, 100, 10, 30}, static_cast<uint16_t>((4 << 6) | 2));
    const int tube = addObject(set.rooms[1], 8, HitRecord{0x40, 120, 100, 10, 30}, static_cast<uint16_t>((100 << 6) | 0));
    const int passage = addObject(set.rooms[1], 10, HitRecord{0x10, 0, 100, 5, 30}, static_cast<uint16_t>((6 << 6) | 2));
    const int hole = addObject(set.rooms[1], 6, HitRecord{2, 150, 140, 20, 5}, 3);
    addObject(set.rooms[2], 9, HitRecord{4, 200, 60, 20, 40}, static_cast<uint16_t>((4 << 6) | 1), 200, 60);
    addObject(set.rooms[2], 11, HitRecord{0x20, 300, 60, 20, 40}, static_cast<uint16_t>((6 << 6) | 1), 300, 60);
    useRooms(g, std::move(set), 1);
    usePlayerSet(g, syntheticSet({}));
    Actor& p = place(g, 0, 1, 50, 130, 5, 7);
    const auto end = [&](int object) {
        p.room = 1;
        p.doorObj = object;
        p.pendingSeq = -1;
        playerSequenceEnd(g, 0);
    };
    end(locked);
    CHECK(g.world.puzzleDoorHit == 1);
    CHECK(p.room == 1);
    CHECK(p.pendingSeq == 33);
    CHECK(g.player.live.hintCancel1048 == 1);
    end(tube);
    CHECK(g.world.puzzleDoorHit == 0);
    CHECK(p.room == 0);
    CHECK(p.pendingSeq == 0);
    CHECK(g.player.live.tubeRoom == 1);
    end(passage);
    CHECK(p.room == 2);
    CHECK(p.pendingSeq == 45);
    end(hole);
    CHECK(p.room == 3);
    p.dir = 2;
    end(hole);
    CHECK(p.pendingSeq == 41);
    p.dir = 0;
    end(hole);
    CHECK(p.pendingSeq == 40);
    // No object (the workshop's exit): the tube room, sequence 53; g_puzzleDoorHit stays.
    g.world.puzzleDoorHit = 1;
    end(-1);
    CHECK(p.room == 1);
    CHECK(p.pendingSeq == 53);
    CHECK(g.world.puzzleDoorHit == 1);
    // Entering: the pending sequence started (+0x1e too), at the twin of the door used, + (10, 14) in a
    // small room.
    end(open);
    CHECK(p.room == 2);
    p.countdown = 3;
    playerEnterRoom(g, 0);
    CHECK(p.seq == 33);
    CHECK(p.prevSeq == 33);
    CHECK(p.pendingSeq == -1);
    CHECK(p.countdown == 0);
    CHECK(p.step == 0);
    CHECK_FALSE(p.hallSet);
    CHECK(p.anchorX == 210);
    CHECK(p.anchorY == 74);
    end(passage);
    playerEnterRoom(g, 0);
    CHECK(p.seq == 45);
    CHECK(p.anchorX == 310);
    // The tube's sequence 53 puts him on floor 5.
    end(-1);
    p.floor = 2;
    playerEnterRoom(g, 0);
    CHECK(p.seq == 53);
    CHECK(p.floor == 5);
    CHECK(p.hallSet);
}

TEST_CASE("actors: Actors_EnterRoom / Actors_LeaveRoom load and release the sets and bundles (1834:07ad, 0870)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    g.player.building = 1;
    g.player.level = 0;
    g.player.raceIndex = 5;
    g.player.withChimps = 1;
    g.world.parts = PartCatalogue::load(g.archive(), g.cache, 5);
    BorlandRand scratch;
    g.world.rooms.load(g.archive(), g.exe(), 5, g.world.parts, g.world.deck, scratch);
    g.world.roomCount = static_cast<int16_t>(g.world.rooms.count());
    actorsInit(g);
    chimpsSetupActors(g);
    chimpsSpawn(g);
    // The workshop: nothing.
    g.world.rooms.displayRoom = 0;
    actorsEnterRoom(g);
    CHECK(g.actors.actorSpritesLoaded == 0);
    // Room 1 (a hall): the hall bundles; Chimp_EnterRoom for the chimps in it.
    g.world.rooms.displayRoom = 1;
    g.actors.actors[0].room = 1;
    g.actors.actors[0].pendingSeq = -1;
    const auto counts = g.actors.roomChimpCount;
    actorsEnterRoom(g);
    CHECK(g.actors.actorSpritesLoaded == 1);
    CHECK(g.actors.actors[0].bundle == 0x1032);
    CHECK(g.actors.actors[1].bundle == 0x1038);
    CHECK(g.cache.useCount(0x1032) == 1);
    CHECK(g.cache.useCount(0x1038) == 1);
    CHECK(g.cache.useCount(0x1040) == 10);
    CHECK(g.actors.actors[0].hallSet);
    int inRoom = 0;
    for (int i = kFirstChimpActor; i < kActorCount; ++i) {
        const Actor& c = g.actors.actors[static_cast<size_t>(i)];
        if (!c.disabled && c.room == 1) ++inRoom;
    }
    CHECK(inRoom > 0);
    CHECK(g.actors.roomChimpCount[1] == counts[1] + inRoom);  // counted again (kept)
    actorsLeaveRoom(g);
    CHECK(g.actors.actorSpritesLoaded == 0);
    CHECK_FALSE(g.cache.cached(0x1032));
    CHECK_FALSE(g.cache.cached(0x1040));
    CHECK(g.actors.actors[0].bundle == -1);
    CHECK(g.actors.actors[5].bundle == -1);
    // The sets: loaded once, freed by the free functions.
    CHECK(g.actorsObjects().playerSetsLoaded == std::array<bool, 4>{true, true, true, true});
    actorsFreePlayerSets(g);
    chimpsFreeSets(g);
    CHECK(g.actorsObjects().sets.empty());
    CHECK(g.actorsObjects().chimpSetHall == 0);
}

// =============================================================================== DOSBox-X states

TEST_CASE("actors data: Up at the locked door of w0/15 (r2/puzzle/k*_door): 11 chimp rand() calls before the player leaves") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    for (int kind = 0; kind < 8; ++kind) {
        const auto door = testdata::ggCapture(std::format("savestates/r2/puzzle/k{}_door.sav", kind));
        const auto screen = testdata::ggCapture(std::format("savestates/r2/puzzle/k{}_screen.sav", kind));
        if (!door || !screen) continue;
        CAPTURE(kind);
        gggame::GameFixture f(*ggFiles);
        REQUIRE(loadBuildingState(f, *door));
        GgGame& g = f.g();
        const uint32_t start = g.rand.seed();
        int frames = 0, leave = 0;
        while (!leave && frames < 200) {
            leave = actorsTick(g);
            g.rt.advanceTicks(4);
            ++frames;
        }
        CHECK(leave == 1);
        CHECK(randCallsBetween(start, g.rand.seed()) == 11);
        CHECK(g.world.puzzleDoorHit == 1);
        CHECK(g.actors.actors[0].pendingSeq == 33);
        // The puzzle choice then makes the 12th and 13th call: the state of the puzzle screen.
        const GgStateFile s = GgStateFile::load(*screen);
        CHECK(randCallsBetween(g.rand.seed(), s.dsDword(dg::kRandState), 100) == 2);
        MESSAGE(std::format("k{}: the player left after {} frames", kind, frames));
    }
}

TEST_CASE("actors data: the chimps of w0/14_warehouse walk into w0/15_at_door (190 rand() calls)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto from = testdata::ggCapture("savestates/w0/14_warehouse.sav");
    const auto to = testdata::ggCapture("savestates/w0/15_at_door.sav");
    if (!from || !to) {
        MESSAGE("w0/14_warehouse or w0/15_at_door missing: test skipped");
        return;
    }
    gggame::GameFixture ref(*ggFiles);
    REQUIRE(ref.loadState(*to));
    const GgGame& want = ref.g();
    gggame::GameFixture f(*ggFiles);
    REQUIRE(loadBuildingState(f, *from));
    GgGame& g = f.g();
    const uint32_t start = g.rand.seed();
    const int wantCalls = randCallsBetween(start, want.rand.seed());
    REQUIRE(wantCalls == 190);
    // The player stands (his walk to the door does not touch the chimps: their contact list holds the
    // projectile only); frame after frame until the rand() state and both chimps equal the state's.
    const auto chimpsEqual = [&] {
        for (int i : {2, 3})
            if (actorLine(g.actors.actors[static_cast<size_t>(i)]) != actorLine(want.actors.actors[static_cast<size_t>(i)])) return false;
        return true;
    };
    int frames = 0;
    int matched = -1;
    while (frames < 700) {
        actorsTick(g);
        g.rt.advanceTicks(4);
        ++frames;
        if (g.rand.seed() == want.rand.seed() && chimpsEqual()) {
            matched = frames;
            break;
        }
        if (randCallsBetween(start, g.rand.seed(), 400) < 0) break;  // past the state
    }
    for (int i : {2, 3}) {
        CAPTURE(i);
        CHECK(actorLine(g.actors.actors[static_cast<size_t>(i)]) == actorLine(want.actors.actors[static_cast<size_t>(i)]));
    }
    CHECK(matched > 0);
    CHECK(g.actors.roomChimpCount == want.actors.roomChimpCount);
    MESSAGE(std::format("w0/14 -> w0/15: {} frames ({} PIT ticks between the states)", matched,
                        want.ticks() - GgStateFile::load(*from).dsDword(dg::kTicks)));
}

TEST_CASE("actors data: inside the warehouse loop (state 7 resumed) the chimps of w0/14 reach w0/15 frame by frame") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto from = testdata::ggCapture("savestates/w0/14_warehouse.sav");
    const auto to = testdata::ggCapture("savestates/w0/15_at_door.sav");
    if (!from || !to) {
        MESSAGE("w0/14_warehouse or w0/15_at_door missing: test skipped");
        return;
    }
    gggame::GameFixture ref(*ggFiles);
    REQUIRE(ref.loadState(*to));
    const uint32_t wantRand = ref.g().rand.seed();
    gggame::GameFixture f(*ggFiles);
    REQUIRE(loadBuildingState(f, *from));
    f.start([](GgGame& g) { state07Warehouse(g, StateEntry::Resume); });
    // 4 PIT ticks a frame: the 1986 ticks between the states are 496 frames and a part of one.
    const auto rows = f.step(496);
    REQUIRE(rows.size() == 496);
    CHECK(rows.back().randState == wantRand);
    f.finish();
    for (int i : {2, 3}) {
        CAPTURE(i);
        CHECK(actorLine(f.g().actors.actors[static_cast<size_t>(i)]) == actorLine(ref.g().actors.actors[static_cast<size_t>(i)]));
    }
}

TEST_CASE("actors data: walking and running from r2/aircraft/06_room1 step as r2_warehouse_walk.avi shows") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto sav = testdata::ggCapture("savestates/r2/aircraft/06_room1.sav");
    if (!sav) {
        MESSAGE("r2/aircraft/06_room1 missing: test skipped");
        return;
    }
    gggame::GameFixture f(*ggFiles);
    REQUIRE(loadBuildingState(f, *sav));
    GgGame& g = f.g();
    REQUIRE(g.player.withChimps == 0);  // Bob: the chimps never call rand()
    const auto calls = g.rand.calls();
    Actor& p = g.actors.actors[0];
    // The frames (of 4 PIT ticks) at which the player's step ran, with his sequence and anchor.
    struct Step {
        int frame = 0, seq = 0, x = 0;
    };
    std::vector<Step> steps;
    int frame = 0;
    const auto run = [&](int frames) {
        for (int k = 0; k < frames; ++k, ++frame) {
            CHECK(actorsTick(g) == 0);
            if (p.countdown == p.delay) steps.push_back({frame, p.seq, p.anchorX});
            g.rt.advanceTicks(4);
        }
    };
    // The anchor's moves between the steps of one sequence (from its second step on), and whether they
    // came every third frame (12 ticks: the AVI's 5.8 video frames).
    const auto deltas = [&](int seq, bool& everyThirdFrame) {
        std::vector<int> d;
        everyThirdFrame = true;
        for (size_t k = 1; k < steps.size(); ++k) {
            if (steps[k].seq != seq || steps[k - 1].seq != seq) continue;
            d.push_back(steps[k].x - steps[k - 1].x);
            everyThirdFrame = everyThirdFrame && steps[k].frame - steps[k - 1].frame == 3;
        }
        return d;
    };
    // A cyclic rotation of `cycle` repeated: what the AVI's left edge did step by step.
    const auto followsCycle = [](const std::vector<int>& d, const std::vector<int>& cycle) {
        if (d.size() < cycle.size() * 2) return false;
        for (size_t r = 0; r < cycle.size(); ++r) {
            bool ok = true;
            for (size_t k = 0; k < d.size() && ok; ++k) ok = d[k] == cycle[(r + k) % cycle.size()];
            if (ok) return true;
        }
        return false;
    };
    // Right: he turns, then walks right (sequence 3): +12 px per 8 steps. The AVI's box edge moved
    // 3, 0, 1, 4, 2, 0, 0, 2 per step (frames 182..275), one step every 5.8 video frames.
    REQUIRE(f.keys("1:down:right"));
    run(150);
    bool third = false;
    const std::vector<int> walk = deltas(3, third);
    CHECK(followsCycle(walk, {3, 0, 1, 4, 2, 0, 0, 2}));
    CHECK(third);
    // Ctrl as well: he runs (sequence 5): +35 px per 8 steps; the AVI: 4, 2, 4, 4, 6, 3, 1, 11 (frames
    // 327..420).
    REQUIRE(f.keys("1:down:lctrl"));
    run(60);
    const std::vector<int> runs = deltas(5, third);
    CHECK(followsCycle(runs, {4, 2, 4, 4, 6, 3, 1, 11}));
    CHECK(third);
    CHECK(g.rand.calls() == calls);
    MESSAGE(std::format("walk: {} steps, run: {} steps, to x {}", walk.size(), runs.size(), p.anchorX));
}

// =============================================================================== parts in play

namespace {

// The rooms of race 5 (Aircraft level 0) dealt from a scratch rand(), the catalogue, the displayed room 1.
void useRace5(GgGame& g) {
    g.player.building = 1;
    g.player.level = 0;
    g.player.raceIndex = 5;
    g.world.parts = PartCatalogue::load(g.archive(), g.cache, 5);
    BorlandRand scratch;
    g.world.rooms.load(g.archive(), g.exe(), 5, g.world.parts, g.world.deck, scratch);
    g.world.roomCount = static_cast<int16_t>(g.world.rooms.count());
    g.world.rooms.displayRoom = 1;
    g.player.live.partsNeeded = static_cast<int16_t>(g.world.parts.neededCount);
}

// Parts of one category of the catalogue (at least n of them; needed or not; not of `notCategory`).
std::vector<int> partsOf(GgGame& g, bool needed, size_t n, int notCategory = -1) {
    for (const PartRecord& t : g.world.parts.parts) {
        if ((t.required != 0) != needed || t.category == notCategory) continue;
        std::vector<int> out;
        for (const PartRecord& r : g.world.parts.parts)
            if (r.category == t.category) out.push_back(r.index);
        if (out.size() >= n) return out;
    }
    return {};
}

int itemsIn(GgGame& g, int room) { return g.world.rooms.room(room).itemCount; }

int itemsInRooms(GgGame& g) {
    int n = 0;
    for (int r = 1; r < g.world.rooms.count(); ++r) n += itemsIn(g, r);
    return n;
}

}  // namespace

TEST_CASE("actors: picking up (18f5:0002): crates, parts, the third of a type, points and hints") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    useRace5(g);
    g.GgState::sound.effectBank = 40;
    Room& room = g.world.rooms.room(1);
    room.items.clear();
    room.itemCount = 0;
    g.world.rooms.resetItemPool();
    Actor& p = g.actors.actors[0];
    p.room = 1;
    const auto pickUp = [&](int kind) {
        addItem(g, 1, kind, 50, 140);
        p.item = itemsIn(g, 1) - 1;
        f.sound().clear();
        playerPickUpItem(g, 0);
    };
    // A crate: 5 bananas, gone, one crate fewer.
    g.player.live.projectiles = 3;
    g.world.rooms.cratesLeft = 2;
    pickUp(ItemNode::kCrate);
    CHECK(g.player.live.projectiles == 8);
    CHECK(itemsIn(g, 1) == 0);
    CHECK(g.world.rooms.cratesLeft == 1);
    // A needed part: effect 1 of the bank (0, 4), carried, hint 0x104a, g_partsNeeded - 1, 20 points.
    const std::vector<int> needed = partsOf(g, true, 3);
    REQUIRE(needed.size() >= 3);
    const int partsNeeded = g.player.live.partsNeeded;
    const uint32_t score = g.player.points;
    pickUp(needed[0]);
    CHECK(f.sound().calls() == std::vector<RecordingSound::Call>{{"playEffect", {41, 0, 4}}});
    CHECK(g.actors.playerParts == std::vector<int16_t>{static_cast<int16_t>(needed[0])});
    CHECK(g.player.live.partsNeeded == partsNeeded - 1);
    CHECK(g.player.points == score + 20);
    CHECK(itemsIn(g, 1) == 0);
    CHECK((g.world.hud.dirty & 1) != 0);
    CHECK(g.ui.hints.pending() == 1);
    // A second part of the type, recycled once (0x4000): carried, the count of needed types stays, no
    // points.
    partRecord(g, needed[1]).flags = PartRecord::kNoPoints;
    pickUp(needed[1]);
    CHECK(g.actors.playerParts.size() == 2);
    CHECK(g.player.live.partsNeeded == partsNeeded - 1);
    CHECK(g.player.points == score + 20);
    // A third: refused, the item stays, no sound.
    pickUp(needed[2]);
    CHECK(g.actors.playerParts.size() == 2);
    CHECK(itemsIn(g, 1) == 1);
    CHECK(f.sound().calls().empty());
    // An item freed this frame already: nothing.
    room.items.clear();
    room.itemCount = 0;
    g.world.rooms.resetItemPool();
    const int node = addItem(g, 1, needed[2], 50, 140);
    g.world.rooms.pool()[static_cast<size_t>(node)].free = true;
    p.item = 0;
    const int carried = p.carried;
    playerPickUpItem(g, 0);
    CHECK(p.carried == carried);
    // The last needed type: hint 0x1050 as well.
    room.items.clear();
    room.itemCount = 0;
    g.player.live.partsNeeded = 1;
    const int hints = g.ui.hints.pending();
    const std::vector<int> other = partsOf(g, true, 1, partRecord(g, needed[0]).category);
    REQUIRE_FALSE(other.empty());
    pickUp(other[0]);
    CHECK(g.player.live.partsNeeded == 0);
    CHECK(g.ui.hints.pending() == hints + 2);
}

TEST_CASE("actors: hit by a chimp (18f5:0180): immunity, R(carried), the part to the chimp, two of a type at most") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    useRace5(g);
    Actor& p = g.actors.actors[0];
    p.room = 1;
    p.toucher = 3;
    // Nothing carried: immune for 150 frames, no rand().
    auto calls = g.rand.calls();
    playerHitByChimp(g, 0);
    CHECK(p.immune == 150);
    CHECK(g.rand.calls() == calls);
    // Two parts of two types: R(2) picks one; it goes to chimp 3; the only one of a needed type: + 1.
    const std::vector<int> a = partsOf(g, true, 4);
    REQUIRE(a.size() >= 4);
    const std::vector<int> b = partsOf(g, true, 1, partRecord(g, a[0]).category);
    REQUIRE_FALSE(b.empty());
    actorAddPart(g, 0, a[0]);
    hudAddPart(g, a[0]);
    actorAddPart(g, 0, b[0]);
    hudAddPart(g, b[0]);
    const int partsNeeded = g.player.live.partsNeeded;
    BorlandRand e = g.rand;
    const int k = e.random(2);
    const int taken = g.actors.playerParts[static_cast<size_t>(k)];
    calls = g.rand.calls();
    playerHitByChimp(g, 0);
    CHECK(g.rand.calls() == calls + 1);
    CHECK(g.actors.chimpParts == std::vector<int16_t>{static_cast<int16_t>(taken)});
    CHECK(partRecord(g, taken).holder == 3);
    CHECK(g.actors.actors[3].carried == 1);
    CHECK(p.carried == 1);
    CHECK(g.player.live.partsNeeded == partsNeeded + 1);
    // The chimps hold two of the type already: the first of theirs goes to a random room (rand) first.
    g.actors.chimpParts.clear();
    g.actors.actors[3].carried = 0;
    g.actors.playerParts.clear();
    p.carried = 0;
    for (PartRecord& r : g.world.parts.parts) {
        r.holder = -1;
        r.pair = -1;
    }
    actorAddPart(g, 4, a[1]);
    actorAddPart(g, 5, a[2]);
    actorAddPart(g, 0, a[3]);
    hudAddPart(g, a[3]);
    const int before = itemsInRooms(g);
    calls = g.rand.calls();
    playerHitByChimp(g, 0);
    CHECK(itemsInRooms(g) == before + 1);
    CHECK(g.rand.calls() > calls + 1 + 20 * 21);
    CHECK(g.actors.chimpParts.size() == 2);
    CHECK(partRecord(g, a[3]).holder == 3);
    CHECK(partRecord(g, a[1]).holder == -1);
}

TEST_CASE("actors: chimps take parts and crates and drop parts (2a80:0000, 00a1)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    useRace5(g);
    g.world.rooms.displayRoom = 2;
    Room& room = g.world.rooms.room(1);
    room.items.clear();
    room.itemCount = 0;
    Actor& c = g.actors.actors[3];
    c.room = 1;
    c.id = 3;
    c.floor = 4;
    c.anchorX = 120;
    const std::vector<int> a = partsOf(g, true, 3);
    REQUIRE(a.size() >= 3);
    const auto take = [&](int kind) {
        addItem(g, 1, kind, 120, 104);
        c.item = itemsIn(g, 1) - 1;
        chimpTakePart(g, 3);
    };
    // A part: carried by the chimp, gone from the room.
    take(a[0]);
    CHECK(g.actors.chimpParts == std::vector<int16_t>{static_cast<int16_t>(a[0])});
    CHECK(c.carried == 1);
    CHECK(itemsIn(g, 1) == 0);
    // One part per chimp: a second stays in the room.
    take(a[1]);
    CHECK(g.actors.chimpParts.size() == 1);
    CHECK(itemsIn(g, 1) == 1);
    room.items.clear();
    room.itemCount = 0;
    // A crate moves to a random room other than the displayed one (rand: a room draw and its grid
    // shuffle); here the chimp's room is the displayed one.
    g.world.rooms.displayRoom = 1;
    addItem(g, 1, ItemNode::kCrate, 120, 104);
    c.item = itemsIn(g, 1) - 1;
    const int before = itemsInRooms(g);
    const auto calls = g.rand.calls();
    chimpTakePart(g, 3);
    CHECK(itemsInRooms(g) == before);
    CHECK(itemsIn(g, 1) == 0);
    CHECK(g.rand.calls() > calls + 20 * 21);
    g.world.rooms.displayRoom = 2;
    // Kept quirk: the chimps hold two of the type, so Actor_AddPart refuses it, and the part is removed
    // from the room all the same (lost for the visit).
    Actor& d = g.actors.actors[4];
    d.room = 1;
    d.id = 4;
    actorAddPart(g, 5, a[1]);
    REQUIRE(g.actors.chimpParts.size() == 2);
    addItem(g, 1, a[2], 120, 104);
    d.item = 0;
    chimpTakePart(g, 4);
    CHECK(itemsIn(g, 1) == 0);
    CHECK(g.actors.chimpParts.size() == 2);
    CHECK(d.carried == 0);
    CHECK(partRecord(g, a[2]).holder == -1);
    // Stunned, it drops its part where it stands: a new item at its x on its floor, in its room.
    chimpDropPart(g, 3);
    CHECK(c.carried == 0);
    CHECK(g.actors.chimpParts == std::vector<int16_t>{static_cast<int16_t>(a[1])});
    REQUIRE(itemsIn(g, 1) == 1);
    const ItemNode& dropped = g.world.rooms.pool()[static_cast<size_t>(room.items.back())];
    CHECK(dropped.kind == a[0]);
    CHECK(dropped.sprite.y == 26 + 26 * 4);  // a hall's floor 4 (Room_DropItemAt: 26 + 26 floor)
    CHECK(dropped.sprite.x == 120);
    // Nothing held: nothing dropped.
    chimpDropPart(g, 3);
    CHECK(itemsIn(g, 1) == 1);
}

TEST_CASE("actors: the step interpreter's callbacks: -10 take, -29 hit, -27 launch, -33 drop, -22 / -23 hide and show") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    useRace5(g);
    const ActorSet set = syntheticSet({sequence({command(-10), command(-29), command(-27, 2, -5), command(-22, 0, 0),
                                                 command(-23, 0, 1), command(-22, 0, -1), frameStep(1)}),
                                       sequence({command(-33), frameStep(1)})});
    usePlayerSet(g, set);
    useChimpSet(g, set);
    Room& room = g.world.rooms.room(1);
    room.items.clear();
    room.itemCount = 0;
    REQUIRE(room.layers[0].size() >= 3);
    room.layers[0][0].sprite.flags = 0;
    room.layers[0][1].sprite.flags = 0x10;
    room.layers[0][2].sprite.flags = 0;
    const std::vector<int> a = partsOf(g, true, 1);
    Actor& p = place(g, 0, 1, 100, 130, 5, 0);
    addItem(g, 1, a[0], 100, 130);
    p.item = 0;
    p.toucher = 2;
    p.pad = 2;
    g.player.live.projectiles = 4;
    actorRunStep(g, 0);
    CHECK(itemsIn(g, 1) == 0);                            // -10: Player_PickUpItem
    CHECK(p.immune == 150);                               // -29: Player_HitByChimp: chimp 2 takes it
    CHECK(g.actors.playerParts.empty());
    CHECK(g.actors.chimpParts == std::vector<int16_t>{static_cast<int16_t>(a[0])});
    CHECK(g.actors.actors[2].carried == 1);
    CHECK(g.actors.actors[1].disabled == 0);              // -27: Projectile_Launch(2, -5)
    CHECK(g.actors.actors[1].anchorX == 95);
    CHECK(g.actors.actors[1].seq == 2);
    CHECK(g.player.live.projectiles == 3);
    CHECK((room.layers[0][0].sprite.flags & 0x40) != 0);  // -22 0 0: Obj_HideAt
    CHECK((room.layers[0][1].sprite.flags & 0x10) == 0);  // -23 0 1: Obj_ShowAt
    CHECK((room.layers[0][2].sprite.flags & 0x40) != 0);  // -22 with b = -1: the object of the pad index
    // -33 for a chimp: Chimp_DropPart, the part where it stands.
    Actor& c = place(g, 2, 1, 150, 104, 4, 1);
    actorRunStep(g, 2);
    CHECK(c.carried == 0);
    CHECK(itemsIn(g, 1) == 1);
}

TEST_CASE("actors data: Save & Exit wrote the actors of r2/resume/02_file_menu; the resume restores them as 08_aircraft_resumed shows") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto menu = testdata::ggCapture("savestates/r2/resume/02_file_menu.sav");
    const auto saved = testdata::ggCapture("savestates/r2/resume/04_after_save_exit.players");
    const auto resumed = testdata::ggCapture("savestates/r2/resume/08_aircraft_resumed.sav");
    if (!menu || !saved || !resumed) {
        MESSAGE("the r2/resume captures are missing: test skipped");
        return;
    }
    // Actors_SaveState at the moment of the save (the drop-down and the box stop the warehouse loop).
    gggame::GameFixture at(*ggFiles);
    REQUIRE(at.loadState(*menu));
    BuildingBlock block;
    actorsSaveState(at.g(), block);
    // The block the game wrote: Bob's record, building 1 (Aircraft).
    gggame::GameFixture file(*ggFiles);
    REQUIRE(file.loadPlayers(*saved));
    REQUIRE(file.restorePlayerByName("Bob"));
    const BuildingBlock& written = file.g().player.blocks[1];
    for (size_t i = 0; i < block.actors.size(); ++i) {
        CAPTURE(i);
        CHECK(block.actors[i] == written.actors[i]);
    }
    // State 4's resume: Chimps_SetupActors, then Actors_RestoreState: the scale of each actor's room and
    // the delay and direction of its sequence's header (the projectile's sequence 0: what 08 shows).
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    g.player = file.g().player;
    g.player.raceIndex = 5;
    g.player.level = 0;
    g.player.building = 1;
    g.world.parts = PartCatalogue::load(g.archive(), g.cache, 5);
    BorlandRand scratch;
    g.world.rooms.load(g.archive(), g.exe(), 5, g.world.parts, g.world.deck, scratch);
    g.world.roomCount = static_cast<int16_t>(g.world.rooms.count());
    actorsInit(g);
    chimpsSetupActors(g);
    actorsRestoreState(g, written);
    gggame::GameFixture after(*ggFiles);
    REQUIRE(after.loadState(*resumed));
    for (int i = 1; i < kActorCount; ++i) {  // the player has played on since (his idle animation)
        const Actor& a = g.actors.actors[static_cast<size_t>(i)];
        const Actor& w = after.g().actors.actors[static_cast<size_t>(i)];
        CAPTURE(i);
        CHECK(a.delay == w.delay);
        CHECK(a.dir == w.dir);
        CHECK(a.disabled == w.disabled);
        CHECK(a.room == w.room);
    }
    const Actor& p = g.actors.actors[0];
    CHECK(p.room == 4);
    CHECK_FALSE(p.hallSet);
    CHECK(p.anchorX == written.actors[0].anchorX);
    CHECK(p.seq == written.actors[0].seq);
    CHECK(g.actors.actors[1].delay == 0);
    CHECK(g.actors.actors[1].dir == -1);
}
