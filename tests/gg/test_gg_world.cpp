// Tests of the G&G world area, the data and drawing halves (src/gg/game): the screen animations, the
// actor sets, the part catalogue and the deck, the room sets and the room loader with its item deal,
// the HUD, the hub, the warehouse frame and the eight puzzle screens. Synthetic data test the exact
// semantics of code_world.md and the puzzle specs; with GG_DATA the loaders are checked against
// counts, sizes, coordinates and digests of the user's files and the rand() counts of the specs; with
// GG_CAPTURES the drawn screens are compared pixel by pixel with the DOSBox-X captures (the only mask
// is the 9 x 9 arrow a hint box leaves on some puzzle shots). No game data is stored here: counts,
// sizes, coordinates and CRC-32 digests only.

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "gg/data/dgroup_offsets.h"
#include "gg/data/resources.h"
#include "gg/game/actors.h"
#include "gg/game/blueprint.h"
#include "gg/game/building.h"
#include "gg/game/hub.h"
#include "gg/game/hud.h"
#include "gg/game/parts.h"
#include "gg/game/pic_cache.h"
#include "gg/game/puzzles.h"
#include "gg/game/pz_balance.h"
#include "gg/game/pz_electric.h"
#include "gg/game/pz_energy.h"
#include "gg/game/pz_gears.h"
#include "gg/game/pz_magnets.h"
#include "gg/game/pz_pieces.h"
#include "gg/game/pz_simple.h"
#include "gg/game/rooms.h"
#include "gg/game/screen_anim.h"
#include "gg/game/world_cli.h"
#include "gg/gfx/video.h"
#include "gg_test_data.h"
#include "mods/png_codec.h"
#include "rtl/borland_rand.h"
#include "util/file_util.h"
#include "util/png_writer.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

void put16(std::vector<uint8_t>& v, int x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}

void putSteps(std::vector<uint8_t>& v, std::initializer_list<std::array<int, 5>> steps) {
    for (const auto& s : steps)
        for (const int w : s) put16(v, w);
}

// A synthetic screen-animation set with one sequence (delay d) of the given steps.
std::vector<uint8_t> screenAnimSet(int delay, std::initializer_list<std::array<int, 5>> steps) {
    std::vector<uint8_t> p(0x32, 0);
    p[0x2C] = 1;           // one sequence
    p.resize(0x32 + 4, 0); // its far pointer
    put16(p, delay);
    put16(p, 0);
    put16(p, 0);
    put16(p, 0);
    put16(p, static_cast<int>(steps.size()));
    putSteps(p, steps);
    return p;
}

// A minimal LPX picture (raw, w x h of colour c, key 0).
std::vector<uint8_t> lpx(int w, int h, uint8_t c) {
    std::vector<uint8_t> p = {'L', 'P', 'X', 0x1A};
    put16(p, 0);
    put16(p, w);
    put16(p, h);
    put16(p, 0);
    put16(p, 2);
    p.insert(p.end(), static_cast<size_t>(w * h), c);
    return p;
}

// LCG steps of the Borland rand().
uint32_t advance(uint32_t seed, int n) {
    BorlandRand r;
    r.setRawSeed(seed);
    for (int i = 0; i < n; ++i) r.rand();
    return r.seed();
}

// Pixels of a 320x200 PNG (a 640x400 capture is halved), packed as R | G << 8 | B << 16.
std::vector<uint32_t> loadFrame(const std::filesystem::path& png) {
    const auto bytes = readWholeFile(png);
    REQUIRE(bytes);
    const auto img = mods::decodePng(*bytes);
    const int step = img.width == 640 ? 2 : 1;
    std::vector<uint32_t> px;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            const size_t o = (static_cast<size_t>(y * step) * static_cast<size_t>(img.width) + static_cast<size_t>(x * step)) * 4;
            px.push_back(uint32_t{img.rgba[o]} | uint32_t{img.rgba[o + 1]} << 8 | uint32_t{img.rgba[o + 2]} << 16);
        }
    return px;
}

struct Rect {
    int x0, y0, x1, y1;  // inclusive
};

int differingPixels(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, const std::vector<Rect>& masks) {
    int n = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            bool masked = false;
            for (const Rect& m : masks) masked = masked || (x >= m.x0 && x <= m.x1 && y >= m.y0 && y <= m.y1);
            const size_t i = static_cast<size_t>(y * 320 + x);
            if (!masked && i < a.size() && i < b.size() && a[i] != b[i]) ++n;
        }
    return n;
}

uint32_t frameCrc(const std::vector<uint32_t>& px) {
    std::vector<uint8_t> b;
    for (const uint32_t p : px)
        for (int k = 0; k < 3; ++k) b.push_back(static_cast<uint8_t>(p >> (8 * k)));
    return png::crc32(b);
}

// Runs a world tool with the data folder; returns its exit code.
int runTool(std::vector<std::string> args) {
    args.push_back("--data");
    args.push_back(pathToUtf8(*testdata::ggDataPath()));
    int code = -1;
    CHECK(worldCli(args, code));
    return code;
}

void writeText(const std::filesystem::path& p, std::string_view s) {
    REQUIRE(writeWholeFile(p, std::vector<uint8_t>(s.begin(), s.end())));
}

}  // namespace

// --- synthetic -------------------------------------------------------------------------------------------

TEST_CASE("world: Rect_Overlap bits and the puzzles' span overlap") {
    CHECK(rectOverlap(0, 0, 10, 10, 10, 0, 5, 5) == 0);   // touching edges do not overlap
    CHECK(rectOverlap(2, 2, 4, 4, 0, 0, 10, 10) == 0xF);  // a inside b
    CHECK(rectOverlap(0, 0, 10, 10, 2, 2, 4, 4) == (1 | 2 | 4 | 8));
    CHECK(rectOverlap(0, 5, 4, 2, 2, 0, 10, 10) == 1);
    CHECK(spansOverlap(0, 0, 10, 10, 5, 5, 3, 3));
    CHECK(spansOverlap(5, 5, 3, 3, 0, 0, 10, 10));
    CHECK_FALSE(spansOverlap(0, 0, 10, 10, 10, 0, 3, 3));
    CHECK(spansOverlap(8, 0, 4, 4, 0, 0, 10, 10));  // straddles the right edge
}

TEST_CASE("world: ScreenAnim steps, countdown and commands (174b:001a)") {
    // -14 x, -15 y, frame 0 (b = 2), frame 1 (a = 3), -8 hidden, -4 back to the first frame.
    const auto set = screenAnimSet(1, {{-14, 100, 0, 0, 0},
                                       {-15, 50, 0, 0, 0},
                                       {0, 0, 2, 0, 0},
                                       {1, 3, 0, 2, 0},
                                       {-8, 0, 0, 0, 0},
                                       {-4, 2, 0, 0, 0},
                                       {-1000, 0, 0, 0, 0}});
    const auto p0 = lpx(4, 3, 7), p1 = lpx(5, 2, 9);
    ScreenAnim a = ScreenAnim::parse(set, {p0, p1});
    BorlandRand rng;
    CHECK(a.delay() == 1);
    a.step(rng);  // countdown 0 -> -1: steps now
    CHECK(a.sprite().x == 100);
    CHECK(a.sprite().y == 52);
    CHECK(a.sprite().w == 4);
    CHECK(a.stepIndex() == 3);
    a.step(rng);  // countdown 1 -> 0: nothing
    CHECK(a.stepIndex() == 3);
    a.step(rng);  // frame 1 at (103, 50), mirrored flags 2
    CHECK(a.sprite().x == 103);
    CHECK(a.sprite().flags == 2);
    CHECK(a.sprite().h == 2);
    a.step(rng);
    a.step(rng);  // -8: hidden, stops
    CHECK(a.hidden());
    CHECK(a.stepIndex() == 5);
    a.step(rng);
    a.step(rng);  // -4 2: frame 0 again (flags = c clears the hidden bit)
    CHECK_FALSE(a.hidden());
    CHECK(a.sprite().y == 52);
    CHECK(rng.seed() == 1);  // no rand() without ops -34/-35

    // -34 base x = a + R(b - a): one rand() call; -1000 finishes and stays.
    const auto set2 = screenAnimSet(0, {{-34, 10, 20, 0, 0}, {0, 0, 0, 0, 0}, {-1000, 0, 0, 0, 0}});
    ScreenAnim b = ScreenAnim::parse(set2, {p0});
    BorlandRand r2;
    b.step(r2);
    CHECK(r2.seed() == advance(1, 1));
    BorlandRand ref;
    CHECK(b.sprite().x == 10 + ref.random(10));
    b.step(r2);
    CHECK(b.state() == ScreenAnim::kFinished);
    CHECK(b.stepIndex() == 2);  // the step is not advanced
    // The running list removes finished ones after queueing them.
    ScreenAnims list;
    ScreenAnim c = ScreenAnim::parse(set2, {p0});
    list.start(c);
    SpriteList sprites;
    list.update(sprites, r2);
    CHECK(list.size() == 1);
    list.update(sprites, r2);
    CHECK(list.size() == 0);
    CHECK(sprites.count() == 2);
}

TEST_CASE("world: actor set layout and the truncated last sequence (1834:0088)") {
    std::vector<uint8_t> p;
    put16(p, 2);  // 2 frames
    put16(p, 10);
    put16(p, 12);  // W
    put16(p, 20);
    put16(p, 22);  // H
    put16(p, 2);   // 2 sequences
    for (const int w : {2, -1, 0, 0, 2}) put16(p, w);  // delay 2, dir -1, steps far, count 2
    putSteps(p, {{1, -4, 1, 2, 0}, {-1000, 0, 0, 0, 0}});
    for (const int w : {0, 1, 0, 0, 2}) put16(p, w);
    putSteps(p, {{0, 4, 0, 0, 0}});
    put16(p, -1000);  // the very last step stores only its op
    const ActorSet s = ActorSet::parse(p);
    REQUIRE(s.sequences.size() == 2);
    CHECK(s.widths[1] == 12);
    CHECK(s.heights[0] == 20);
    CHECK(s.sequences[0].dir == -1);
    CHECK(s.sequences[1].steps.back().op == -1000);

    // The frame Actor_RunStep showed last and its size.
    Actor a;
    a.seq = 0;
    a.step = 1;
    a.anchorX = 30;
    a.anchorY = 40;
    const AnimStep* st = lastFrameStep(a, s);
    REQUIRE(st);
    const auto pic = lpx(12, 22, 3);
    std::vector<std::span<const uint8_t>> pics = {pic, pic};
    actorShowFrame(a, {&s, &pics}, *st);
    CHECK(a.sprite.w == 12);
    CHECK(a.sprite.h == 22);
    CHECK(a.sprite.y == 41);
    CHECK(a.sprite.flags == 2);
}

TEST_CASE("world: Actors_Draw order and the player's blink (1834:0733)") {
    std::array<Actor, 12> actors{};
    for (Actor& a : actors) a.disabled = 1;
    actors[0].disabled = 0;
    actors[0].room = 3;
    actors[0].sprite.x = 1;
    actors[5].disabled = 0;
    actors[5].room = 3;
    actors[5].sprite.x = 5;
    actors[6].disabled = 0;
    actors[6].room = 2;  // another room
    int16_t blink = 3;
    SpriteList sprites;
    actorsDraw(actors, 3, blink, sprites);
    REQUIRE(sprites.count() == 2);
    CHECK(sprites[0].x == 5);  // chimps first, the player last
    CHECK(sprites[1].x == 1);
    CHECK(blink == 0);  // not immune: reset
    actors[0].immune = 10;
    int shown = 0;
    for (int f = 0; f < 8; ++f) {
        SpriteList s;
        actorsDraw(actors, 3, blink, s);
        shown += s.count() == 2;
    }
    CHECK(shown == 4);  // 2 frames on, 2 off
}

TEST_CASE("world: room set parse order and the animated objects' script (1622:0085, 0706)") {
    // Room 0: one static object in layer 0. Room 1 (a hall): one object in layer 3 and one in
    // layer 0 (layer 3 comes first in the file), one animated object, one wall on floor 2.
    std::vector<uint8_t> p;
    put16(p, 2);
    auto header = [&](uint16_t flags, std::array<int, 4> objects, int anims, int walls2) {
        std::vector<uint8_t> h(0x120, 0);
        h[0] = static_cast<uint8_t>(flags);
        for (size_t l = 0; l < 4; ++l) h[2 + 2 * l] = static_cast<uint8_t>(objects[l]);
        h[0x0A] = static_cast<uint8_t>(anims);
        h[0x12 + 4] = static_cast<uint8_t>(walls2);
        p.insert(p.end(), h.begin(), h.end());
    };
    auto object = [&](int x, int type) {
        std::vector<uint8_t> o(0x20, 0);
        o[2] = static_cast<uint8_t>(x);
        o[0x10] = static_cast<uint8_t>(type);
        p.insert(p.end(), o.begin(), o.end());
    };
    header(0, {1, 0, 0, 0}, 0, 0);
    object(1, 3);
    header(1, {1, 0, 0, 1}, 1, 1);
    object(33, 12);  // layer 3
    object(10, 4);   // layer 0
    std::vector<uint8_t> anim(0x48, 0);
    anim[0x14] = 0;  // reload 0: steps every frame
    const int script[] = {0, 1, -2, -3};
    for (size_t k = 0; k < 4; ++k) {
        anim[0x26 + 2 * k] = static_cast<uint8_t>(script[k] & 0xFF);
        anim[0x27 + 2 * k] = static_cast<uint8_t>((script[k] >> 8) & 0xFF);
    }
    p.insert(p.end(), anim.begin(), anim.end());
    p.insert(p.end(), 10, 0);  // the wall
    RoomSet set = RoomSet::parse(p);
    CHECK(set.used == p.size());
    REQUIRE(set.rooms.size() == 2);
    CHECK(set.rooms[1].isHall());
    CHECK(set.rooms[1].layers[3][0].sprite.x == 33);
    CHECK(set.rooms[1].layers[0][0].type == 4);
    CHECK(set.rooms[1].walls[2].size() == 1);

    // The script: frame 0, frame 1, then -2 skipped and -3 pauses on frame 0.
    Rooms rooms;
    PartCatalogue parts;
    PartDeck deck;
    BorlandRand rng;
    rooms.load(std::move(set), {0, 0}, parts, deck, rng);
    CHECK(rng.seed() == 1);  // no items: no rand() (the deck of 0 entries makes no swap)
    const auto p0 = lpx(3, 3, 1), p1 = lpx(5, 4, 2);
    std::vector<std::span<const uint8_t>> pics = {p0, p1};
    RoomAnim& a = rooms.room(1).anims[0][0];
    a.pictures = &pics;
    a.sprite.picture = p0;
    rooms.displayRoom = 1;
    std::vector<int> widths;
    for (int f = 0; f < 4; ++f) {
        SpriteList s;
        rooms.drawAnimLayer(0, s);
        widths.push_back(s.count() ? s[0].w : -1);
    }
    CHECK(widths == std::vector<int>{3, 5, 3, 3});
    CHECK((a.sprite.flags & RoomAnim::kPaused) != 0);
}

TEST_CASE("world: the deck and Room_PlaceItems' rand() calls (153f:01b6, 1622:0a76)") {
    PartDeck deck;
    BorlandRand rng;
    deck.shuffle(22, 20, rng);
    CHECK(rng.seed() == advance(1, 440));  // 20 calls per entry
    CHECK(std::count(deck.entries().begin(), deck.entries().end(), PartDeck::kCrate) == 2);

    // An empty hall: 90 cells, 20 * 90 calls per Room_PlaceItems; items on the grid.
    std::vector<uint8_t> p;
    put16(p, 2);
    for (int r = 0; r < 2; ++r) {
        std::vector<uint8_t> h(0x120, 0);
        h[0] = 1;
        p.insert(p.end(), h.begin(), h.end());
    }
    Rooms rooms;
    PartCatalogue parts;
    BorlandRand r2;
    rooms.load(RoomSet::parse(p), {3, 3}, parts, deck, r2);
    CHECK(r2.seed() == advance(1, 60 + 1800));
    const auto items = rooms.itemsOf(1);
    REQUIRE(items.size() == 3);
    for (const ItemNode* it : items) {
        CHECK((it->sprite.x - 20) % 20 == 0);
        CHECK(it->sprite.y % 26 == 0);
        CHECK(it->sprite.w == 10);
        CHECK(it->pictureId == ItemNode::kHallPicture);
        CHECK(it->kind == PartDeck::kCrate);
    }
    // 90 items do not fit: the last shuffled cell is never tried.
    BorlandRand r3;
    CHECK(rooms.placeItems(1, 90, PartDeck::kCrate, parts, deck, r3) == 0);
}

TEST_CASE("world: the room save block's doors and items round trip (1622:0fb3, 10e7)") {
    std::vector<uint8_t> p;
    put16(p, 2);
    for (int r = 0; r < 2; ++r) {
        std::vector<uint8_t> h(0x120, 0);
        h[0] = static_cast<uint8_t>(r == 1 ? 0 : 1);  // room 1 small
        h[2] = 1;
        p.insert(p.end(), h.begin(), h.end());
        std::vector<uint8_t> o(0x20, 0);
        o[0x10] = 7;  // a locked door
        p.insert(p.end(), o.begin(), o.end());
    }
    Rooms rooms;
    PartCatalogue parts;
    PartDeck deck;
    BorlandRand rng;
    rooms.load(RoomSet::parse(p), {0, 0}, parts, deck, rng);
    rooms.dropItemAt(1, 75, 1, PartDeck::kCrate, false, parts);
    rooms.dropItemAt(1, 115, 5, 3, false, parts);  // floor 5 of a small room = its floor 2
    std::vector<uint8_t> doors;
    std::vector<int16_t> items;
    rooms.saveState(doors, items);
    CHECK(doors == std::vector<uint8_t>{7, 7});
    CHECK(items == std::vector<int16_t>{0, 2, 75, 1, 1000, 115, 2, 3});
    Rooms back;
    back.load(RoomSet::parse(p), {0, 0}, parts, deck, rng);
    back.restoreState(std::vector<uint8_t>{9, 7}, items, parts);
    CHECK(back.room(0).layers[0][0].type == 9);
    CHECK(back.room(0).layers[0][0].pictureId == 0x33C);  // a hall's open door
    CHECK(back.room(0).layers[0][0].hit.kind == 4);
    CHECK(back.itemsOf(1).size() == 2);
    CHECK(back.itemsOf(1)[1]->sprite.y == 2 * 52 + 46);
    CHECK(back.cratesLeft == 1);
}

TEST_CASE("world: the energy panel's sprintf of one %d") {
    const std::string f = "Draw %d lines (100%%).";
    const auto out = formatOneInt(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(f.data()), f.size()), 3);
    CHECK(std::string(out.begin(), out.end()) == "Draw 3 lines (100%).");
}

// --- the user's files ------------------------------------------------------------------------------------

TEST_CASE("world data: the nine room sets (A 0x347..0x34f)") {
    GG_REQUIRE_DATA();
    const Archive& arc = ggFiles->archive();
    int rooms = 0;
    std::map<int, int> types, animKinds;
    for (uint16_t id = 0x347; id <= 0x34F; ++id) {
        const auto bytes = arc.read(id);
        const RoomSet set = RoomSet::parse(bytes);
        CHECK(set.used == bytes.size());  // every set is consumed exactly
        rooms += static_cast<int>(set.rooms.size());
        CHECK(set.rooms[0].layers[0].size() == 1);  // room 0 is the workshop: one background
        for (const Room& r : set.rooms) {
            for (int l = 1; l < 4; ++l) {
                CHECK(r.layers[static_cast<size_t>(l)].empty());
                CHECK(r.anims[static_cast<size_t>(l)].empty());
            }
            for (const RoomObject& o : r.layers[0]) ++types[o.type];
            for (const RoomAnim& a : r.anims[0]) ++animKinds[a.hit.kind];
            if (!r.anims[0].empty()) CHECK(r.isHall());
        }
    }
    CHECK(rooms == 108);
    // code_world.md §5.3 / §5.4
    CHECK(types == std::map<int, int>{{3, 1255}, {4, 182}, {5, 74}, {6, 14}, {7, 710}, {8, 18}, {9, 18}, {10, 77},
                                      {11, 77}, {12, 247}, {13, 247}, {16, 180}, {18, 90}});
    CHECK(animKinds == std::map<int, int>{{0x80, 45}, {0x100, 25}, {0x200, 12}, {0x400, 14}});
    // g_roomSetIds DS:09e8: 8, 12, 12, 16, 16 rooms per building
    for (int race = 0; race < 15; ++race) {
        const int expected[5] = {8, 12, 12, 16, 16};
        CHECK(RoomSet::parse(arc.read(Rooms::roomSetId(ggFiles->exe(), race))).rooms.size() ==
              static_cast<size_t>(expected[race % 5]));
    }
}

TEST_CASE("world data: the actor sets and the part catalogues") {
    GG_REQUIRE_DATA();
    const Archive& arc = ggFiles->archive();
    for (const ActorKind& k : {kPlayerKind, kProjectileKind, kChimpKind, kBigChimpKind})
        for (const uint16_t id : {k.hallSet, k.roomSet}) {
            const ActorSet s = ActorSet::parse(arc.read(id));
            CHECK(s.widths.size() == s.heights.size());
            for (const AnimSequence& q : s.sequences) {
                REQUIRE_FALSE(q.steps.empty());
                CHECK(q.steps.back().op == -1000);
            }
        }
    CHECK(ActorSet::parse(arc.read(kPlayerKind.hallSet)).sequences.size() == 68);
    CHECK(ActorSet::parse(arc.read(kPlayerKind.roomSet)).sequences.size() == 53);
    const size_t partCounts[15] = {20, 36, 40, 40, 40, 20, 32, 40, 40, 44, 24, 36, 36, 40, 44};
    for (int race = 0; race < 15; ++race) {
        const PartCatalogue c = PartCatalogue::parse(arc.read(static_cast<uint16_t>(PartCatalogue::kFirstId + race)));
        CHECK(c.count() == partCounts[race]);
        CHECK(c.building == race / 5);
        CHECK_FALSE(c.morty.empty());
        std::map<int, int> perCategory;
        for (const PartRecord& p : c.parts)
            if (p.required) ++perCategory[p.category];
        CHECK(static_cast<int>(perCategory.size()) == c.neededCount);
        for (const auto& [cat, n] : perCategory) CHECK(n == 4);  // every needed type has 4 parts
    }
}

TEST_CASE("world data: Rooms_Load of the w0 set-up deals 3920 rand() calls (code_world.md §4.4)") {
    GG_REQUIRE_DATA();
    const Archive& arc = ggFiles->archive();
    PartCatalogue parts = PartCatalogue::parse(arc.read(PartCatalogue::kFirstId + 5));
    PartDeck deck;
    Rooms rooms;
    BorlandRand rng;
    rng.setRawSeed(0x29CF);  // the seed of the w0 captures
    rooms.load(arc, ggFiles->exe(), 5, parts, deck, rng);
    // deck 20 * 22, room 1 (a hall) 20 * 90, rooms 4..7 20 * 21; the coin and the 2 chimps' spawn
    // (R(100) and 2 x {R(6), R(100)}) follow and give the state of w0/09_blueprint.
    CHECK(rng.seed() == advance(0x29CF, 3920));
    CHECK(advance(rng.seed(), 5) == 0xAD7A0B08u);
    CHECK(rooms.count() == 8);
    CHECK(rooms.cratesTotal == 2);
    std::vector<uint8_t> deal;
    int items = 0;
    for (int r = 0; r < rooms.count(); ++r)
        for (const ItemNode* it : rooms.itemsOf(r)) {
            ++items;
            for (const int v : {r, it->sprite.x, it->sprite.y, it->kind}) {
                deal.push_back(static_cast<uint8_t>(v));
                deal.push_back(static_cast<uint8_t>(v >> 8));
            }
        }
    CHECK(items == 22);
    // The rooms, positions and parts of every item, as the save state w0/09_blueprint holds them.
    CHECK(png::crc32(deal) == 0x88C730E3u);
}

TEST_CASE("world data: the puzzles' variant data") {
    GG_REQUIRE_DATA();
    const GgExe& exe = ggFiles->exe();
    const Archive& arc = ggFiles->archive();
    const int variants[8] = {30, 20, 20, 31, 32, 30, 28, 30};
    for (int k = 0; k < kPuzzleKinds; ++k) CHECK(puzzleVariantCount(exe, k) == variants[k]);

    // Energy forms: 28 variants run as code; the budget is the number of needed lines.
    for (int v = 0; v < 28; ++v) {
        const auto e = PzEnergy::readVariant(exe, v);
        CHECK(e.stores > 0);
        int needed = 0;
        for (int k = 0; k < 4; ++k)
            for (int d = 0; d < 4; ++d)
                needed += e.form[static_cast<size_t>(k)] >= 0 && e.device[static_cast<size_t>(d)] != 0xFFFF &&
                          e.uses[static_cast<size_t>(d)][static_cast<size_t>(e.form[static_cast<size_t>(k)])] != 0;
        CHECK(e.budget == needed);
    }
    // Electric: the terminal geometry read from the code (code_puzzles_a.md §3.4) and the variants.
    PzElectric el(exe, arc);
    el.setTerminalGeometry(0, PzElectric::Battery);
    const auto& bat = el.pieces()[0].t;
    CHECK((bat[0].hitX == 9 && bat[0].hitY == 2 && bat[0].hitW == 8 && bat[0].hitH == 10));
    CHECK((bat[1].anchorX == 14 && bat[1].anchorY == 60 && bat[1].dotY == 54 && bat[1].overlapH == -6));
    CHECK(bat[0].piece == PzElectric::kNone);
    el.setTerminalGeometry(1, PzElectric::YWire);
    const auto& y = el.pieces()[1].t;
    CHECK((y[2].hitX == 1 && y[2].hitY == 26 && y[2].anchorX == 6 && y[2].anchorY == 31 && y[2].overlapH == -6));
    el.setTerminalGeometry(2, PzElectric::Bulb);
    CHECK((el.pieces()[2].t[1].anchorX == 12 && el.pieces()[2].t[1].anchorY == 39));
    int maxWires = 0;
    for (int v = 0; v < 30; ++v) {
        PzElectric e(exe, arc);
        e.loadVariant(v);
        CHECK(e.usedPieces() <= 7);
        maxWires = std::max(maxWires, e.toolCounts[4]);
    }
    CHECK(maxWires == 14);
    // Gears: object 0 is the crank, every gear starts in the inventory, 2..5 buttons.
    for (int v = 0; v < 31; ++v) {
        PzGears g(exe, arc);
        g.loadVariant(v);
        REQUIRE(g.objectCount() > 0);
        CHECK(g.objects()[0].type == 6);
        int buttons = 0;
        for (int i = 1; i < g.objectCount(); ++i) {
            const auto& o = g.objects()[static_cast<size_t>(i)];
            CHECK(o.type != 6);
            if (o.type <= 1) CHECK(o.col == PzGears::kNotPlaced);
            if (o.type >= 7) {
                ++buttons;
                CHECK(o.state == 0);
            }
        }
        CHECK((buttons >= 2 && buttons <= 5));
    }
    // Machine pieces: 2..5 pieces; simple machines: the records match the counts.
    for (int v = 0; v < 20; ++v) {
        PzPieces p(exe, arc);
        p.loadResources(v);
        CHECK((p.pieceCount() >= 2 && p.pieceCount() <= 5));
    }
    // Magnets: the level map is a permutation of 1..30.
    std::vector<int> levels;
    for (int v = 0; v < 30; ++v) levels.push_back(exe.ds8(static_cast<uint16_t>(dg::kMagnetsLevelOfVariant + v)));
    std::sort(levels.begin(), levels.end());
    for (int i = 0; i < 30; ++i) CHECK(levels[static_cast<size_t>(i)] == i + 1);
}

TEST_CASE("world data: Hud_SetDigitColors recolours the digits and back") {
    GG_REQUIRE_DATA();
    Video video;
    video.init(ggFiles->exe(), ggFiles->archive());
    PicCache cache(ggFiles->archive(), &video.palette());
    Hud hud;
    hud.init(ggFiles->exe(), ggFiles->archive(), cache);
    CHECK(hud.scheme() == 0);
    auto digits = [&] {
        Screen s;
        hud.drawNumber(s, 0, 0, 1234567890u % 1000000u, 6, hud.scheme());
        return png::crc32(s.screenPixels());
    };
    const uint32_t s0 = digits();
    hud.setDigitColors(2);
    CHECK(digits() != s0);
    hud.setDigitColors(0);
    CHECK(digits() == s0);
}

// --- the screens ---------------------------------------------------------------------------------------
//
// Each screen is drawn by the world tool from a scene (what scene.py reads from a save state: positions,
// frames, counters). With GG_DATA alone the frames' CRC-32 (RGB, 320 x 200) guard the drawing; with
// GG_CAPTURES they are compared with the DOSBox-X shots pixel by pixel.

namespace {

// w0/06_hub: the animations and the pointer as the save state holds them (steps 3 and 4, the sign-in label).
constexpr const char* kHubScene = R"({"racesWon": [-1, -1, -1], "mouse": {"x": 93, "y": 137},
    "anims": [{"id": 4101, "seq": 0, "step": 3, "countdown": 2, "baseX": 265, "baseY": 10},
              {"id": 4103, "seq": 0, "step": 4, "countdown": 2, "baseX": 283, "baseY": 45}]})";

// r2/aircraft/06_room1: race 5, room 1 (a hall) without chimps; the player standing at (17, 144), two
// boxes, the pads one frame before the shot (--frames 1), the HUD counters.
constexpr const char* kRoomScene = R"({"race": 5, "room": 1, "withChimps": 0, "playerBlink": 0,
    "mouse": {"x": 269, "y": 150}, "hud": {"partsNeeded": 3, "projectiles": 10, "score": 0},
    "actors": [{"slot": 0, "disabled": 0, "room": 1, "floor": 5, "seq": 0, "step": 1, "anchorX": 17,
                "anchorY": 144, "x": 17, "y": 144, "flags": 0}],
    "anims": [{"index": 0, "flags": 0, "countdown": 0, "pos": 1}, {"index": 1, "flags": 0, "countdown": 0, "pos": 1},
              {"index": 2, "flags": 0, "countdown": 0, "pos": 1}, {"index": 3, "flags": 0, "countdown": 0, "pos": 1},
              {"index": 4, "flags": 0, "countdown": 0, "pos": 1}],
    "items": [{"x": 300, "y": 130, "w": 10, "h": 7, "flags": 0, "picture": 835, "kind": 19},
              {"x": 220, "y": 52, "w": 10, "h": 7, "flags": 0, "picture": 835, "kind": 12}]})";

// r2/puzzle/kK_01_screen: the HUD the warehouse left under the panel (3 parts needed, 10 bananas,
// score 0), the pointer at (269, 150); the launch glow one frame on from the state (frame 0 shows).
constexpr const char* kPuzzleScene = R"({"mouse": {"x": 269, "y": 150}, "hud": {"partsNeeded": 3,
    "projectiles": 10, "score": 0}, "puzzle": {"glow": 1, "device0": 0, "device1": 0, "device2": 0, "device3": 0}})";

// w0/09_blueprint: race 5 (Level-1 BLIMP), the pointer at (62, 85) (outside the hot spots).
constexpr const char* kBlueprintScene = R"({"race": 5, "mouse": {"x": 62, "y": 85},
    "hud": {"partsNeeded": 3, "projectiles": 10, "score": 0}})";

// Only the blueprint's box (18, 9) 280 x 157 is the viewer's: around it is the workshop (wave 3).
const std::vector<Rect> kOutsideBlueprintBox = {{0, 0, 319, 8}, {0, 9, 17, 165}, {298, 9, 319, 165}, {0, 166, 319, 199}};

struct ScreenCase {
    std::vector<std::string> args;  // after "--gg-screen"; OUT, --scene and --no-cursor are added
    const char* scene;
    std::string capture;
    bool engineCursor;  // false: --no-cursor, the shot's leftover arrow masked (10 x 10 at the pointer)
    bool blueprintBox;  // compare the blueprint's box only
    uint32_t crc;       // of the drawn frame (RGB)
};

std::vector<ScreenCase> screenCases() {
    std::vector<ScreenCase> v = {
        {{"hub"}, kHubScene, "w0/06_hub.png", true, false, 0},
        {{"warehouse", "--frames", "1"}, kRoomScene, "r2/aircraft/06_room1.png", true, false, 0},
        {{"blueprint"}, kBlueprintScene, "w0/09_blueprint.png", true, true, 0},
        {{"blueprint", "--page", "4", "--mouse", "53,51"}, kBlueprintScene, "w0/10_blueprint_part_info.png", true, true, 0},
    };
    // Energy (6) and launch (5) redraw the cursor on their timers: drawn by the engine. The others
    // still show the arrow the hint box left (no flush since).
    for (int k = 0; k < kPuzzleKinds; ++k)
        v.push_back({{"puzzle", std::to_string(k), "0"}, kPuzzleScene, std::format("r2/puzzle/k{}_01_screen.png", k),
                     k == 5 || k == 6, false, 0});
    const uint32_t crcs[] = {0x6A4D5C6Eu, 0xBAC9CA25u, 0x41CE1B8Eu, 0x98783777u, 0xEBE4BE28u, 0x8BD88152u,
                             0x8345FCA3u, 0x59929AD2u, 0xFE2B011Fu, 0x4F29FAE3u, 0x5C1C86C9u, 0x15C65A05u};
    for (size_t i = 0; i < v.size(); ++i) v[i].crc = crcs[i];
    return v;
}

// Draws a case into `dir`; returns the frame.
std::vector<uint32_t> drawCase(const ScreenCase& c, size_t index, const std::filesystem::path& dir) {
    const auto scene = dir / std::format("scene{}.json", index);
    writeText(scene, c.scene);
    const auto out = dir / std::format("screen{}.png", index);
    std::vector<std::string> args = {"--gg-screen"};
    args.insert(args.end(), c.args.begin(), c.args.end());
    // OUT follows the screen's own operands (the options are found anywhere).
    const size_t outAt = c.args[0] == "puzzle" ? 4 : 2;
    args.insert(args.begin() + static_cast<ptrdiff_t>(outAt), pathToUtf8(out));
    args.push_back("--scene");
    args.push_back(pathToUtf8(scene));
    if (!c.engineCursor) args.push_back("--no-cursor");
    REQUIRE(runTool(args) == 0);
    return loadFrame(out);
}

}  // namespace

TEST_CASE("world screens: the frames drawn from the scenes (CRC-32)") {
    GG_REQUIRE_DATA();
    const auto dir = testdata::scratchDir("world_screens_crc");
    const auto cases = screenCases();
    for (size_t i = 0; i < cases.size(); ++i) {
        CAPTURE(i);
        CHECK(frameCrc(drawCase(cases[i], i, dir)) == cases[i].crc);
    }
}

TEST_CASE("world screens: against the DOSBox-X captures (0 pixels differ)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto dir = testdata::scratchDir("world_screens");
    const auto cases = screenCases();
    for (size_t i = 0; i < cases.size(); ++i) {
        CAPTURE(i);
        const ScreenCase& c = cases[i];
        const auto ref = testdata::ggCapture(c.capture);
        REQUIRE(ref);
        std::vector<Rect> masks;
        if (c.blueprintBox) masks = kOutsideBlueprintBox;
        if (!c.engineCursor) masks.push_back({269, 150, 278, 159});  // the puzzles' pointer
        CHECK(differingPixels(drawCase(c, i, dir), loadFrame(*ref), masks) == 0);
    }
}

TEST_CASE("world data: the fifteen blueprint bundles (DS:22c4)") {
    GG_REQUIRE_DATA();
    const GgExe& exe = ggFiles->exe();
    const Archive& arc = ggFiles->archive();
    // code_vehicles_races.md §6.2: 3 + 3 n + 1 + z members, every hot spot on a page of its blueprint.
    const int pages[15] = {5, 9, 10, 10, 10, 5, 8, 10, 10, 11, 6, 9, 9, 10, 11};
    const int spots[15] = {15, 21, 22, 22, 22, 13, 15, 25, 27, 16, 12, 30, 25, 26, 28};
    for (int race = 0; race < 15; ++race) {
        CAPTURE(race);
        BlueprintView view(exe, arc);
        view.load(race);
        CHECK(view.pageCount() == pages[race]);
        CHECK(static_cast<int>(view.hotSpots().size()) == spots[race]);
        const Bundle b = Bundle::load(arc, exe.ds16(static_cast<uint16_t>(dg::kBlueprintBundles + 2 * race)));
        CHECK(b.members().size() == static_cast<size_t>(4 + 3 * pages[race] + spots[race]));
        for (const BlueprintView::HotSpot& h : view.hotSpots()) {
            CHECK((h.page >= 0 && h.page < pages[race]));
            CHECK((h.x >= 18 && h.y >= 9 && h.x < 298 && h.y < 166));
        }
    }
    const auto box = BlueprintView(exe, arc).box();
    CHECK((box.x == 18 && box.y == 9 && box.w == 280 && box.h == 157));
}
