// Wave 3, the world (re/gg/docs/code_world.md §4-§8, §14-§17, code_vehicles_races.md §3-§4): the part
// lists, the HUD strip and its commands, the building set-up and resume with their rand() calls, the
// room changes with the random wipe, the warehouse loop, the door a solved puzzle opens and the room half
// of the building block, checked against the DOSBox-X states and shots of the reference captures and
// the rand() counts tools/gg/capture_mem.py gives between two states. The actors, the UI and the flow
// are other areas': where a check needs what they do (a carried list, the player's room after a resume)
// the test sets it as the original would have, so these tests hold with their stubs and with their code.
// Only counts, ticks, coordinates, flags and differing-pixel counts here.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "gg/data/dgroup_offsets.h"
#include "gg/game/actors.h"
#include "gg/game/game.h"
#include "gg/game/hud.h"
#include "gg/game/load_state.h"
#include "gg/game/parts.h"
#include "gg/game/pic_cache.h"
#include "gg/game/rooms.h"
#include "gg/game/world.h"
#include "gg/gfx/screen.h"
#include "gg/gfx/transitions.h"
#include "gg/save/gg_state_file.h"
#include "gg/save/players_dat.h"
#include "gg_game_fixture.h"
#include "gg_test_data.h"
#include "rtl/borland_rand.h"
#include "util/file_util.h"
#include "util/png_writer.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

std::optional<std::filesystem::path> statePath(const std::string& name) { return testdata::ggCapture("savestates/" + name + ".sav"); }

// A DOSBox-X state into the fixture's game (GgStateFile::apply with its .players copy) and the world's
// heap structures from its RAM (worldRestoreFromState). The state file stays open in `s`.
bool loadWorld(gggame::GameFixture& f, const std::string& name, std::optional<GgStateFile>& s) {
    const auto path = statePath(name);
    REQUIRE(path);
    const auto missing = f.loadState(*path);
    REQUIRE(missing);
    s.emplace(GgStateFile::load(*path));
    std::string why;
    const bool ok = worldRestoreFromState(f.g(), *s, why);
    CHECK_MESSAGE(ok, why);
    return ok;
}

// The rand() state after n calls from `seed`.
uint32_t advance(uint32_t seed, uint64_t n) {
    BorlandRand r;
    r.setRawSeed(seed);
    for (uint64_t i = 0; i < n; ++i) r.rand();
    return r.seed();
}

// The palette faded, as the hub leaves it (12e0:0148 ends with Pal_FadeOut) before state 4.
void fadeOut(GgGame& g) { g.display.fadeOut(); }

// Modelled bytes of the named fields that differ between the game's DGROUP and a state's.
std::vector<std::string> fieldDifferences(const GgGame& g, const GgStateFile& s, std::initializer_list<std::string_view> names) {
    const auto mine = dumpToDgroup(g, s.dgroup());
    std::vector<std::string> out;
    for (const DgroupField& f : gameStateFields()) {
        if (std::find(names.begin(), names.end(), f.name) == names.end()) continue;
        for (size_t i = 0; i < f.size; ++i)
            if (f.modelled(i) && mine[f.ds + i] != s.dgroup()[f.ds + i]) {
                out.push_back(std::format("{}+{:#x}", f.name, i));
                break;
            }
    }
    return out;
}

// The rectangle (inclusive) a sprite record covers, one pixel larger on each side.
gggame::Mask around(int x, int y, int w, int h) { return gggame::Mask{x - 1, y - 1, x + w, y + h}; }

// Actor `slot` of a state's DGROUP as the rectangle it was drawn at (+0x02 x, +0x04 y, +0x06 w, +0x08 h).
gggame::Mask actorRect(const GgStateFile& s, int slot) {
    const auto base = static_cast<uint16_t>(dg::kActors + 0xA0 * slot);
    auto w = [&](int o) { return static_cast<int16_t>(s.dsWord(static_cast<uint16_t>(base + o))); };
    return around(w(2), w(4), w(6), w(8));
}

// The software cursor (its top-left at g_mouseX / Y, a 16 x 16 box covers every shape the game uses
// in the rooms).
gggame::Mask cursorRect(const GgStateFile& s) {
    const GgCursorFields c = s.cursor();
    return around(c.mouseX, c.mouseY, 16, 16);
}

// Differing bytes of two 320-wide buffers in rows y0..y1 outside the masks (rows taken modulo 200 for
// the masks); the bounding box of the differences goes to the test's log.
// The game's actor `slot` and pointer as the engine last drew them.
gggame::Mask actorRect(const GgGame& g, int slot) {
    const Actor& a = g.actors.actors[static_cast<size_t>(slot)];
    return around(a.sprite.x, a.sprite.y, a.sprite.w, a.sprite.h);
}
gggame::Mask cursorRect(GgGame& g) { return around(g.video.cursor().x(), g.video.cursor().y(), 16, 16); }

int differences(std::span<const uint8_t> a, std::span<const uint8_t> b, int y0, int y1, std::span<const gggame::Mask> masks = {}) {
    int n = 0, bx0 = 320, by0 = 400, bx1 = -1, by1 = -1;
    for (int y = y0; y <= y1; ++y)
        for (int x = 0; x < 320; ++x) {
            const int fy = y % 200;
            if (std::any_of(masks.begin(), masks.end(), [&](const gggame::Mask& m) { return x >= m.x0 && x <= m.x1 && fy >= m.y0 && fy <= m.y1; }))
                continue;
            const size_t i = static_cast<size_t>(y) * 320 + static_cast<size_t>(x);
            if (a[i] == b[i]) continue;
            ++n;
            bx0 = std::min(bx0, x);
            by0 = std::min(by0, y);
            bx1 = std::max(bx1, x);
            by1 = std::max(by1, y);
        }
    if (n) MESSAGE(std::format("{} bytes differ in ({}, {}) .. ({}, {})", n, bx0, by0, bx1, by1));
    return n;
}

// A yield hook that counts the random wipe's steps (Timer_Wait(2)) with the tick each ends at, and stops
// the game at the warehouse loop's first frame boundary (Timer_Wait(0)).
struct WipeProbe {
    struct Stop {};
    std::vector<uint32_t> stepEnds;
    GgGame* g = nullptr;
    void operator()(YieldPoint p, int slot) {
        if (p == YieldPoint::TimerWait && slot == 2) stepEnds.push_back(g->ticks());
        if (p == YieldPoint::TimerWait && slot == 0) throw Stop{};
    }
};

// With WORLD3_DUMP set (a folder), the picture is written there as a PNG for a look by hand.
void dumpFrame(const std::vector<uint32_t>& frame, const std::string& name) {
    const char* dir = std::getenv("WORLD3_DUMP");
    if (!dir || frame.size() != 320u * 200u) return;
    std::vector<uint8_t> rgba;
    for (const uint32_t p : frame)
        for (int k = 0; k < 4; ++k) rgba.push_back(k == 3 ? uint8_t{255} : static_cast<uint8_t>(p >> (8 * k)));
    png::writeRgba(std::filesystem::path(dir) / (name + ".png"), 320, 200, rgba);
}

}  // namespace

// ------------------------------------------------------------------------------------- the part lists

TEST_CASE("world3: the carried lists' walks (153f:0216..0354, 2bd4:0101)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    PartCatalogue& c = g.world.parts;
    c.parts.resize(8);
    for (int i = 0; i < 8; ++i) {
        PartRecord& r = c.parts[static_cast<size_t>(i)];
        r.index = i;
        r.category = static_cast<int16_t>(i / 2);
    }
    // The player picked up 0 (category 0), 2 (1), 1 (0: the second of its category, the upper part) and
    // 4 (2, built in); a chimp (actor 3) holds 6.
    c.parts[0].pair = static_cast<int16_t>(1 | 0x8000);
    c.parts[1].pair = 0;
    for (const int p : {0, 1, 2, 4}) c.parts[static_cast<size_t>(p)].holder = 0;
    c.parts[4].flags = PartRecord::kInstalled;
    c.parts[6].holder = 3;
    g.actors.playerParts = {0, 2, 1, 4};
    g.actors.chimpParts = {6};
    const auto& list = g.actors.playerParts;

    int last = 99;
    CHECK(partListCountType(g, list, 0, &last) == 2);
    CHECK(last == 1);
    CHECK(partListCountType(g, list, 3, &last) == 0);
    CHECK(last == -1);
    CHECK(partListFindType(g, list, 1) == 2);
    CHECK(partListFindType(g, list, 3) == -1);
    CHECK(partListCountHeldBy(g, 0) == 4);
    CHECK(partListCountHeldBy(g, 3) == 1);
    CHECK(partListCountHeldBy(g, 2) == 0);
    CHECK(partListCountHeldBy(g, 1) == 0);
    // The columns: the lower parts and the unpaired ones (an upper part's +0x1c is >= 0).
    CHECK(partListNextColumn(g, list, 0) == 2);
    CHECK(partListNextColumn(g, list, 2) == 4);
    CHECK(partListNextColumn(g, list, 1) == 4);
    CHECK(partListNextColumn(g, list, 4) == -1);
    CHECK(partListPrevColumn(g, list, 4) == 2);
    CHECK(partListPrevColumn(g, list, 2) == 0);
    CHECK(partListPrevColumn(g, list, 0) == -1);
    // The cyclic search skips the built-in part; a start past the list tries the tail first.
    CHECK(partListFindHeld(g, list, 0, 0) == 0);
    CHECK(partListFindHeld(g, list, 0, 3) == 0);
    CHECK(partListFindHeld(g, list, 0, 1) == 2);
    CHECK(partListFindHeld(g, list, 5, 0) == -1);
    CHECK(partListFindHeld(g, list, 0, 7) == 0);
    const std::vector<int16_t> two = {0, 2};
    CHECK(partListFindHeld(g, two, 0, 9) == 2);
    CHECK(partListFindHeld(g, std::vector<int16_t>{}, 0, 0) == -1);
}

// ------------------------------------------------------------------------------ building set-up, resume

TEST_CASE("world3: the w0 new visit makes the original's rand() calls and deal (code_world.md §4.4)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    const auto hub = statePath("w0/06_hub");
    const auto after = statePath("w0/09_blueprint");
    REQUIRE((hub && after));
    REQUIRE(f.loadState(*hub));
    const GgStateFile s9 = GgStateFile::load(*after);
    const uint32_t seed = g.rand.seed();
    CHECK(seed == 0x29CFu);
    fadeOut(g);
    g.player.building = 1;  // the click on the Aircraft building (12e0:03bf..0479)
    const uint64_t calls0 = g.rand.calls();
    CHECK(state04BuildingSetup(g) == 5);
    const uint64_t calls = g.rand.calls() - calls0;
    // 440 (the deck) + 1800 (the hall) + 4 x 420 (rooms 4..7) + 1 (the coin) = 3921 of the world's, then
    // Chimps_Spawn's R(6) / R(3) and R(100) per chimp (2 chimps): 3925 in the capture.
    CHECK((calls == 3921 || calls == 3925));
    CHECK(advance(seed, 3925) == s9.dsDword(dg::kRandState));
    if (calls == 3925) CHECK(g.rand.seed() == s9.dsDword(dg::kRandState));
    CHECK(advance(seed, calls) == g.rand.seed());
    // The deal and the counters the workshop state still holds.
    const auto diff = fieldDifferences(g, s9, {"g_itemPool", "g_partDeck", "g_cratesLeft", "g_cratesTotal", "g_roomCount"});
    const std::string firstDiff = diff.empty() ? std::string() : diff.front();
    CHECK_MESSAGE(diff.empty(), firstDiff);
    CHECK(g.player.level == 0);
    CHECK(g.player.raceIndex == 5);
    CHECK(g.player.live.partsNeeded == static_cast<int16_t>(s9.dsWord(dg::kPartsNeeded)));
    CHECK(g.player.live.projectiles == 10);
    CHECK(g.player.live.tubeRoom == static_cast<int16_t>(s9.dsWord(dg::kTubeRoom)));
    CHECK(g.player.live.setupCoin == static_cast<int16_t>(s9.dsWord(dg::kSetupCoin)));
    CHECK(g.player.live.paintColor == -1);
    CHECK(g.playerRoom() == 0);  // Building_NewVisit: every visit starts in the workshop
    CHECK(g.actors.actors[kProjectileActor].disabled == 1);
    CHECK(g.GgState::sound.titleMusicBank == -1);
    CHECK(g.GgState::sound.roomMusicBank != -1);
    CHECK(g.world.roomCount == 8);
}

TEST_CASE("world3: a resumed building deals 3920 rand() calls and restores the block (r2/resume 07 -> 08)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    const auto hub = statePath("r2/resume/07_hub_bob_resumed");
    const auto room = statePath("r2/resume/08_aircraft_resumed");
    REQUIRE((hub && room));
    REQUIRE(f.loadState(*hub));
    const GgStateFile s8 = GgStateFile::load(*room);
    REQUIRE(g.player.buildingInProgress[1] == 1);
    fadeOut(g);
    g.player.building = 1;
    const uint64_t calls0 = g.rand.calls();
    CHECK(state04BuildingSetup(g) == 5);
    // The deck and the item grids are dealt and thrown away: no coin, no spawn (code_world.md §4.2).
    CHECK(g.rand.calls() - calls0 == 3920);
    CHECK(g.rand.seed() == s8.dsDword(dg::kRandState));
    CHECK(g.world.inBuilding == 1);
    // The doors and the items of the block (Rooms_RestoreState: the pool allocated in the same order).
    const auto diff = fieldDifferences(g, s8, {"g_itemPool", "g_partDeck", "g_cratesLeft", "g_cratesTotal", "g_roomCount"});
    const std::string firstDiff = diff.empty() ? std::string() : diff.front();
    CHECK_MESSAGE(diff.empty(), firstDiff);
    std::optional<GgStateFile> s;
    gggame::GameFixture ref(*ggFiles);
    REQUIRE(loadWorld(ref, "r2/resume/08_aircraft_resumed", s));
    for (int r = 0; r < g.world.rooms.count(); ++r) {
        CAPTURE(r);
        const Room& a = g.world.rooms.room(r);
        const Room& b = ref.g().world.rooms.room(r);
        CHECK(a.items == b.items);
        CHECK(a.itemCount == b.itemCount);
        for (size_t i = 0; i < a.layers[0].size(); ++i) {
            CHECK(a.layers[0][i].type == b.layers[0][i].type);
            CHECK(a.layers[0][i].pictureId == b.layers[0][i].pictureId);
            CHECK(a.layers[0][i].hit.kind == b.layers[0][i].hit.kind);
        }
    }
    // Every part's flags come back; the holders are Actor_AddPart's (the actors area).
    for (size_t i = 0; i < g.world.parts.parts.size(); ++i) CHECK(g.world.parts.parts[i].flags == ref.g().world.parts.parts[i].flags);

    // State 5 into the block's room (Actors_RestoreState puts the player there) and the warehouse's
    // entry: faded, so a flush and the fade-in (no wipe, no rand()), then the first frame.
    g.actors.actors[kPlayerActor].room = g.player.blocks[1].actors[0].room;
    REQUIRE(g.playerRoom() == 4);
    f.start([](GgGame& game) {
        if (state05EnterRoom(game) == 7) state07Warehouse(game);
    });
    const auto rows = f.step(1);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].randCalls - calls0 == 3920);
    CHECK(g.world.rooms.displayRoom == 4);
    // The room as the capture shows it: the player (drawn by the actors area), the pointers (the shot's
    // and the hub state's), the menu bar (the UI area's) and the HUD's strip (laid out by Hud_AddPart
    // from the carried list Actor_AddPart rebuilds) aside.
    const gggame::Mask masks[] = {actorRect(s8, kPlayerActor), actorRect(g, kPlayerActor), cursorRect(s8), cursorRect(g),
                                  {0, 0, 319, 8}, {0, 165, 216, 199}};
    f.finish();
    dumpFrame(f.frame(), "resume08");
    CHECK(f.differingPixels(*testdata::ggCapture("r2/resume/08_aircraft_resumed.png"), masks) == 0);
}

// --------------------------------------------------------------------------------------------- the HUD

TEST_CASE("world3: the strip's layout and drawing equal the resumed building's (1a04:0348, 0588)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    std::optional<GgStateFile> s;
    REQUIRE(loadWorld(f, "r2/resume/08_aircraft_resumed", s));
    auto& parts = g.world.parts.parts;
    REQUIRE(!g.actors.playerParts.empty());
    // Parts_RestoreState gave the parts back in index order, each through Hud_AddPart: the strip is the
    // layout from the last of them.
    int anchor = -1;
    for (size_t i = 0; i < parts.size(); ++i)
        if (parts[i].holder == 0) anchor = static_cast<int>(i);
    REQUIRE(anchor >= 0);
    std::vector<std::pair<int, int>> shown;
    for (const PartRecord& r : parts) shown.emplace_back(r.hudSprite.x, r.hudSprite.y);
    const int16_t first = g.world.hud.stripFirst, last = g.world.hud.stripLast;
    CHECK(first != -1);
    for (const int16_t p : g.actors.playerParts) parts[static_cast<size_t>(p)].hudSprite.x = 77;
    hudLayoutStrip(g, anchor, 0);
    for (const int16_t p : g.actors.playerParts) {
        CAPTURE(p);
        CHECK(parts[static_cast<size_t>(p)].hudSprite.x == shown[static_cast<size_t>(p)].first);
        if (parts[static_cast<size_t>(p)].hudSprite.x != -1) CHECK(parts[static_cast<size_t>(p)].hudSprite.y == shown[static_cast<size_t>(p)].second);
    }
    CHECK(g.world.hud.stripFirst == first);
    CHECK(g.world.hud.stripLast == last);

    // A full redraw of the HUD (Hud_Update with 0x8000) over the state's screen: the capture's HUD.
    const size_t zonesBefore = ClickZones::listOf(g.zones().local()).size();
    g.world.hud.dirty |= Hud::kFull;
    hudUpdate(g, 0);
    g.display.flush();
    f.present();
    const gggame::Mask playArea[] = {{0, 0, 319, 164}};
    CHECK(f.differingPixels(*testdata::ggCapture("r2/resume/08_aircraft_resumed.png"), playArea) == 0);
    // The strip's zones: the shown parts' and the two arrows', as before the redraw.
    size_t visible = 0;
    for (const int16_t p : g.actors.playerParts) visible += parts[static_cast<size_t>(p)].hudSprite.x != -1;
    CHECK(ClickZones::listOf(g.zones().local()).size() == visible + 2);
    CHECK(zonesBefore == visible + 2);
}

TEST_CASE("world3: the strip commands: select, put back, scroll, trash (1a04:084c..0b5b)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    std::optional<GgStateFile> s;
    REQUIRE(loadWorld(f, "r2/aircraft/26_all_parts", s));
    REQUIRE(g.world.rooms.displayRoom == 4);
    auto& parts = g.world.parts.parts;
    HudState& h = g.world.hud;
    REQUIRE(h.selectedPart == -1);
    // A shown part that is the only one of a needed category.
    int pick = -1;
    for (const int16_t p : g.actors.playerParts) {
        const PartRecord& r = parts[static_cast<size_t>(p)];
        if (r.hudSprite.x != -1 && r.required && r.pair == -1 && !(r.flags & PartRecord::kInstalled)) pick = p;
    }
    REQUIRE(pick >= 0);
    const size_t zones = ClickZones::listOf(g.zones().local()).size();
    f.sound().clear();

    // 5000 + part through Input_GetCode's hook: the part on the cursor, the carry zones instead of the
    // strip's, effect 4 of the global bank, its name in the box.
    REQUIRE(g.input().hooks().hudSelectPart);
    g.input().hooks().hudSelectPart(static_cast<uint16_t>(pick));
    CHECK(h.selectedPart == pick);
    CHECK(ClickZones::listOf(g.zones().local()).size() == 2);
    CHECK(g.video.cursor().activeRecord() != g.video.cursor().arrow());
    CHECK(g.video.cursor().hotX() == parts[static_cast<size_t>(pick)].hudSprite.w / 2);
    CHECK((h.dirty & Hud::kInfoText) != 0);
    CHECK(g.worldObjects().infoText == parts[static_cast<size_t>(pick)].name);
    CHECK(RecordingSound::describe(f.sound().calls()).find(std::format("playEffect({},0,5)", g.sound.globalEffects() + 4)) != std::string::npos);
    // 6001: put back (the arrow, the carry zones gone, a full redraw brings the strip's zones back).
    g.input().hooks().hudPutBack();
    CHECK(h.selectedPart == -1);
    CHECK(g.video.cursor().activeRecord() == g.video.cursor().arrow());
    CHECK((h.dirty & Hud::kFull) != 0);
    hudUpdate(g, 0);
    CHECK(ClickZones::listOf(g.zones().local()).size() == zones);
    // Esc in the warehouse: Hud_EscHook puts a selected part back.
    hudSelectPart(g, pick);
    hudEscHook(g);
    CHECK(h.selectedPart == -1);
    CHECK(ClickZones::listOf(g.zones().local()).size() == zones);
    // Hud_FlashPart: 6 toggles, one every 4 Hud_Update calls; meanwhile the strip ignores the commands.
    hudFlashPart(g, pick, false);
    CHECK(h.flashCount == 5);
    CHECK(h.flashPart == pick);
    CHECK((h.dirty & Hud::kFlash) != 0);
    hudSelectPart(g, pick);
    CHECK(h.selectedPart == -1);
    int updates = 0;
    while ((h.dirty & Hud::kFlash) && updates < 100) {
        hudUpdate(g, 0);
        ++updates;
    }
    CHECK(updates == 6 * (h.flashPeriod + 1));
    CHECK(h.flashCount == -1);

    // 6000: into the recycle can: flagged 0x4000, placed in a random other room (one R(roomCount - 1)
    // per draw, then a grid shuffle of 20 calls per cell), the category counts as needed again, effect
    // 3, a full redraw.
    hudSelectPart(g, pick);
    std::vector<int> before(static_cast<size_t>(g.world.rooms.count()));
    for (int r = 0; r < g.world.rooms.count(); ++r) before[static_cast<size_t>(r)] = g.world.rooms.room(r).itemCount;
    const int needed = g.player.live.partsNeeded;
    const uint64_t calls0 = g.rand.calls();
    f.sound().clear();
    g.input().hooks().hudTrash();
    const uint64_t calls = g.rand.calls() - calls0;
    CHECK(calls >= 1 + 20 * 21);
    CHECK((parts[static_cast<size_t>(pick)].flags & PartRecord::kNoPoints) != 0);
    CHECK(h.selectedPart == -1);
    CHECK(g.player.live.partsNeeded == needed + 1);
    CHECK((h.dirty & Hud::kFull) != 0);
    CHECK(g.input().keyHeldSeen == 1);
    CHECK(RecordingSound::describe(f.sound().calls()).find(std::format("playEffect({},0,5)", g.sound.globalEffects() + 3)) != std::string::npos);
    int placed = -1;
    for (int r = 0; r < g.world.rooms.count(); ++r)
        if (g.world.rooms.room(r).itemCount == before[static_cast<size_t>(r)] + 1) placed = r;
    REQUIRE(placed > 0);
    CHECK(placed != g.world.rooms.displayRoom);
    CHECK(g.world.rooms.itemCounts()[static_cast<size_t>(placed)] != 0);
    const ItemNode& node = g.world.rooms.pool()[static_cast<size_t>(g.world.rooms.room(placed).items.back())];
    CHECK(node.kind == pick);
    CHECK(node.pictureId == (g.world.rooms.room(placed).isHall() ? 0x344 : 0x333));  // + 1: scored before
}

// -------------------------------------------------------------------------------- rooms, doors, wipes

TEST_CASE("world3: through an open door: the wipe, its rand() calls and room 5 drawn (r2/aircraft 12 -> 13)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    std::optional<GgStateFile> s;
    REQUIRE(loadWorld(f, "r2/aircraft/12_room1_open_door", s));
    const GgStateFile s13 = GgStateFile::load(*statePath("r2/aircraft/13_room5_arrived"));
    REQUIRE(g.player.withChimps == 0);
    // Without chimps the only rand() calls are the wipe's: R(8) for the pattern, R(2) for its sound.
    const uint32_t seed = g.rand.seed();
    BorlandRand pick;
    pick.setRawSeed(seed);
    CHECK(randomWipePattern(pick) == 0x11DB);  // the pattern the AVI shows (code_runtime.md §14.2)
    // Player_SequenceEnd at the door (the actors area) sent the player to its target room.
    g.actors.actors[kPlayerActor].room = 5;
    WipeProbe probe;
    probe.g = &g;
    f.runtime().setYieldHook(std::ref(probe));
    const uint64_t calls0 = g.rand.calls();
    try {
        CHECK(state08LeaveRoom(g) == 5);
        CHECK(state05EnterRoom(g) == 7);
        state07Warehouse(g);
        FAIL("the warehouse loop returned");
    } catch (const WipeProbe::Stop&) {
    }
    f.runtime().setYieldHook({});
    CHECK(g.rand.calls() - calls0 == 2);
    CHECK(g.rand.seed() == s13.dsDword(dg::kRandState));
    // 0x11DB: 48 steps on timer slot 2, 2 ticks each (96 ticks: the 45 changing video frames of
    // r2_aircraft_door_to_room5.avi; wave 2 matched every one of them to a step).
    REQUIRE(probe.stepEnds.size() == 48);
    for (size_t i = 1; i < probe.stepEnds.size(); ++i) CHECK(probe.stepEnds[i] - probe.stepEnds[i - 1] == 2);
    const std::string calls = RecordingSound::describe(f.sound().calls());
    CHECK(calls.find(std::format("playEffect({},1,10)", g.GgState::sound.effectBank + 0x19 + (pick.random(2)))) != std::string::npos);
    // Room 5 as the original drew it: the back page (the room's background, the HUD's saved rows) to the
    // pixel, the screen with the player and the pointer aside.
    REQUIRE(s13.screenBuffer().size() == GgStateFile::kScreenBytes);
    CHECK(g.world.rooms.displayRoom == 5);
    CHECK(differences(g.video.screen().bytes(), s13.screenBuffer(), 200, 399) == 0);
    const gggame::Mask masks[] = {actorRect(s13, kPlayerActor), actorRect(g, kPlayerActor), cursorRect(s13), cursorRect(g)};
    CHECK(differences(g.video.screen().bytes(), s13.screenBuffer(), 9, 164, masks) == 0);
}

TEST_CASE("world3: the warehouse frames without chimps call rand() never and keep 4 ticks (r2/aircraft/06)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    std::optional<GgStateFile> s;
    REQUIRE(loadWorld(f, "r2/aircraft/06_room1", s));
    REQUIRE(g.world.rooms.displayRoom == 1);
    const uint64_t calls0 = g.rand.calls();
    f.start([](GgGame& game) { state07Warehouse(game, StateEntry::Resume); });
    const auto rows = f.step(60);
    REQUIRE(rows.size() == 60);
    for (size_t i = 1; i < rows.size(); ++i) CHECK(rows[i].ticks - rows[i - 1].ticks == 4);
    CHECK(rows.back().randCalls == calls0);
    CHECK(rows.back().displayRoom == 1);
    f.finish();
    // The shot was taken a frame before the state: the first frame's pads (stepped once from the state)
    // and the items over the room, the HUD; the player is the actors area's.
    gggame::GameFixture f1(*ggFiles);
    std::optional<GgStateFile> s1;
    REQUIRE(loadWorld(f1, "r2/aircraft/06_room1", s1));
    f1.start([](GgGame& game) { state07Warehouse(game, StateEntry::Resume); });
    REQUIRE(f1.step(1).size() == 1);
    const gggame::Mask masks[] = {actorRect(*s1, kPlayerActor), actorRect(f1.g(), kPlayerActor)};
    f1.finish();
    CHECK(f1.differingPixels(*testdata::ggCapture("r2/aircraft/06_room1.png"), masks) == 0);
}

TEST_CASE("world3: a solved puzzle opens the door and its twin (12e0:0a24; r2/aircraft 10 -> 12)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    std::optional<GgStateFile> s;
    REQUIRE(loadWorld(f, "r2/aircraft/10_room1_door_k4", s));
    const int door = g.actors.actors[kPlayerActor].doorObj;
    RoomObject* obj = roomGetObject(g, g.playerRoom(), door);
    REQUIRE(obj);
    REQUIRE(obj->type == 7);
    const int target = obj->targetRoom();
    // Not solved: nothing changes.
    roomOpenPuzzleDoor(g, 0);
    CHECK(obj->type == 7);
    g.puzzles.goThrough = 0;  // Go Back: the player comes out of the same door
    roomOpenPuzzleDoor(g, 1);
    CHECK(obj->type == 9);
    CHECK(obj->hit.kind == 4);
    CHECK(obj->pictureId == 0x33C);  // room 1 is a hall
    CHECK(g.playerRoom() == 1);
    const RoomObject* twin = roomFindLinkedDoor(g, target, obj->link);
    REQUIRE(twin);
    CHECK(twin->type == 9);
    CHECK(twin->pictureId == (roomIsHall(g, target) ? 0x33C : 0x30E));
    // The door types of every room as the state after the puzzle holds them.
    gggame::GameFixture ref(*ggFiles);
    std::optional<GgStateFile> s12;
    REQUIRE(loadWorld(ref, "r2/aircraft/12_room1_open_door", s12));
    for (int r = 0; r < g.world.rooms.count(); ++r)
        for (size_t i = 0; i < g.world.rooms.room(r).layers[0].size(); ++i) {
            CAPTURE(r);
            CAPTURE(i);
            const RoomObject& a = g.world.rooms.room(r).layers[0][i];
            const RoomObject& b = ref.g().world.rooms.room(r).layers[0][i];
            CHECK(a.type == b.type);
            CHECK(a.hit.kind == b.hit.kind);
            CHECK(a.pictureId == b.pictureId);
        }
    // Going through (DS:b144 set): the player's room becomes the target.
    gggame::GameFixture f2(*ggFiles);
    std::optional<GgStateFile> s2;
    REQUIRE(loadWorld(f2, "r2/aircraft/10_room1_door_k4", s2));
    f2.g().puzzles.goThrough = 1;
    roomOpenPuzzleDoor(f2.g(), 1);
    CHECK(f2.g().playerRoom() == target);
}

TEST_CASE("world3: Rooms_SaveState and Parts_SaveState write the original's block (r2/resume 03 -> 04)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    std::optional<GgStateFile> s;
    REQUIRE(loadWorld(f, "r2/resume/03_save_exit_dialog", s));
    const auto after = testdata::ggCapture("savestates/r2/resume/04_after_save_exit.players");
    REQUIRE(after);
    const auto bytes = readWholeFile(*after);
    REQUIRE(bytes);
    const auto dat = PlayersDat::parse(*bytes);
    REQUIRE(dat);
    const int slot = g.player.slot;
    const BuildingBlock& want = dat->records.at(static_cast<size_t>(slot)).blocks[1];
    BuildingBlock block = g.player.blocks[1];
    partsSaveState(g, block);
    roomsSaveState(g, block);
    const size_t n = g.world.parts.count();
    REQUIRE(n > 0);
    for (size_t i = 0; i < n; ++i) {
        CAPTURE(i);
        CHECK(block.parts[i] == want.parts[i]);
    }
    CHECK(block.doorTypes == want.doorTypes);
    CHECK(block.roomItems == want.roomItems);
    // And back: Rooms_RestoreState of that block gives the rooms the state holds.
    std::vector<std::vector<int>> items;
    for (int r = 0; r < g.world.rooms.count(); ++r) items.push_back(g.world.rooms.room(r).items);
    const int crates = g.world.rooms.cratesLeft;
    roomsRestoreState(g, want);
    for (int r = 0; r < g.world.rooms.count(); ++r) {
        CAPTURE(r);
        CHECK(g.world.rooms.room(r).items.size() == items[static_cast<size_t>(r)].size());
    }
    CHECK(g.world.rooms.cratesLeft <= crates);  // the last room's crates only (§20 bug 6)
}

TEST_CASE("world3: leaving a room: to a puzzle (9), with the vehicle done (10), else 5 (12e0:09aa)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    std::optional<GgStateFile> s;
    REQUIRE(loadWorld(f, "r2/aircraft/06_room1", s));
    REQUIRE(g.world.rooms.roomLoaded);
    const int music = g.GgState::sound.roomMusicBank;
    f.sound().clear();
    g.world.puzzleDoorHit = 1;
    CHECK(state08LeaveRoom(g) == 9);
    CHECK(g.world.puzzleDoorHit == 0);
    CHECK_FALSE(g.world.rooms.roomLoaded);
    CHECK(g.video.palette().faded());
    CHECK(RecordingSound::describe(f.sound().calls()).find(std::format("stopMusic({})", music)) != std::string::npos);
    g.world.vehicleDone = 1;
    g.world.inBuilding = 0;
    g.player.buildingInProgress[1] = 0;
    CHECK(state08LeaveRoom(g) == 10);
    CHECK(g.world.vehicleDone == 0);
    CHECK(g.player.buildingInProgress[1] == 1);
    CHECK(state08LeaveRoom(g) == 5);
    // Into the workshop (the tube: g_playerRoom 0): Room_Enter(0) stops the room music, state 6 next.
    g.actors.actors[kPlayerActor].room = 0;
    f.sound().clear();
    CHECK(state05EnterRoom(g) == 6);
    CHECK(g.world.rooms.displayRoom == 0);
    CHECK(RecordingSound::describe(f.sound().calls()).find(std::format("stopMusic({})", music)) != std::string::npos);
}

TEST_CASE("world3: Item_PlaceInRandomRoom and Score_Add (1622:0f4e, 12e0:0ace)") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    fadeOut(g);
    g.player.building = 1;
    g.player.racesWon = {-1, -1, -1};
    g.player.buildingInProgress = {0, 0, 0};
    state04BuildingSetup(g);
    Rooms& rooms = g.world.rooms;
    rooms.displayRoom = 1;
    std::vector<int> before;
    for (int r = 0; r < rooms.count(); ++r) before.push_back(rooms.room(r).itemCount);
    // One draw per R(roomCount - 1) + 1 until a room other than the displayed one that had items at
    // load comes up, then that room's grid shuffle.
    BorlandRand replay;
    replay.setRawSeed(g.rand.seed());
    int expect = 0;
    uint64_t draws = 0;
    do {
        expect = replay.random(rooms.count() - 1) + 1;
        ++draws;
    } while (expect == 1 || rooms.itemCounts()[static_cast<size_t>(expect)] == 0);
    const uint64_t calls0 = g.rand.calls();
    itemPlaceInRandomRoom(g, ItemNode::kCrate);
    const int cells = rooms.room(expect).isHall() ? 90 : 21;
    CHECK(g.rand.calls() - calls0 == draws + 20 * static_cast<uint64_t>(cells));
    CHECK(rooms.room(expect).itemCount == before[static_cast<size_t>(expect)] + 1);
    CHECK(rooms.pool()[static_cast<size_t>(rooms.room(expect).items.back())].kind == ItemNode::kCrate);
    // Room_DropItemAt (Chimp_DropPart's drop where the chimp stands): in the displayed hall on floor 4
    // (y 26 * 4 + 26), counted and its picture loaded; in a small room floors 3..5 are its 0..2.
    const int count1 = rooms.room(1).itemCount;
    roomDropItemAt(g, 1, 77, 4, 2, false);
    CHECK(rooms.room(1).itemCount == count1 + 1);
    const ItemNode& dropped = rooms.pool()[static_cast<size_t>(rooms.room(1).items.back())];
    CHECK((dropped.kind == 2 && dropped.sprite.x == 77 && dropped.sprite.y == 130 && dropped.sprite.w == 10 && dropped.sprite.h == 7));
    CHECK(dropped.pictureId == 0x343);
    CHECK(!dropped.sprite.picture.empty());
    CHECK(dropped.hit.kind == 0x800);
    int small = -1;
    for (int r = 4; r < rooms.count() && small < 0; ++r)
        if (!rooms.room(r).isHall()) small = r;
    REQUIRE(small > 0);
    const int count2 = rooms.room(small).itemCount;
    roomDropItemAt(g, small, 40, 4, ItemNode::kCrate, false);
    const ItemNode& crate = rooms.pool()[static_cast<size_t>(rooms.room(small).items.back())];
    CHECK((crate.sprite.y == 98 && crate.sprite.w == 20 && crate.sprite.h == 13 && crate.pictureId == 0x332));
    CHECK(crate.sprite.picture.empty());  // not the displayed room: loaded when it is entered
    CHECK(rooms.room(small).itemCount == count2 + 1);
    // Room_RemoveItem: freed, counted down, unlinked.
    const int crateNode = rooms.room(small).items.back();
    roomRemoveItem(g, small, crateNode);
    CHECK(rooms.pool()[static_cast<size_t>(crateNode)].free);
    CHECK(rooms.room(small).itemCount == count2);
    CHECK(std::find(rooms.room(small).items.begin(), rooms.room(small).items.end(), crateNode) == rooms.room(small).items.end());
    // Score_Add: u32, the HUD's score bit only when asked (the pick-up and the workshop do not ask).
    g.player.points = 0xFFFF;
    g.world.hud.dirty = 0;
    scoreAdd(g, 20);
    CHECK(g.player.points == 0x10013u);
    CHECK(g.world.hud.dirty == 0);
    scoreAdd(g, -3, 1);
    CHECK(g.player.points == 0x10010u);
    CHECK(g.world.hud.dirty == Hud::kScore);
    projectilesAdd(g, 5);
    CHECK((g.world.hud.dirty & Hud::kBananas) != 0);
}

// ------------------------------------------------------------------------------- the picture cache

TEST_CASE("world3: the picture cache's records follow the original's slot rules (15df:003e..03a2)") {
    GG_REQUIRE_DATA();
    PicCache cache(ggFiles->archive());
    auto rec = [&](int i) { return cache.slots()[static_cast<size_t>(i)]; };
    auto same = [](PicCache::Slot a, int id, int uses) { return a.id == static_cast<int16_t>(id) && a.uses == uses; };
    CHECK(same(rec(0), -1, 0));
    cache.load(0x343);  // the first free record
    CHECK(same(rec(0), 0x343, 1));
    const size_t pads = cache.loadBundle(0x340).size();  // its own record, then one per picture
    REQUIRE(pads > 1);
    CHECK(same(rec(1), 0x340, 1));
    for (size_t k = 0; k < pads; ++k) CHECK(same(rec(2 + static_cast<int>(k)), 0x340, 0));
    cache.load(0x343);  // cached: the use count
    CHECK(same(rec(0), 0x343, 2));
    cache.loadBundle(0x340);
    CHECK(same(rec(1), 0x340, 2));
    const int next = 2 + static_cast<int>(pads);
    cache.load(0x332);
    CHECK(same(rec(next), 0x332, 1));
    cache.release(0x343);
    CHECK(same(rec(0), 0x343, 1));
    cache.release(0x343);  // freed at 0
    CHECK(same(rec(0), -1, 0));
    cache.load(0x30E);  // takes the first free record again
    CHECK(same(rec(0), 0x30E, 1));
    cache.releaseBundle(0x340);
    CHECK(same(rec(1), 0x340, 1));
    cache.releaseBundle(0x340);  // the bundle's record and its pictures' records
    for (int i = 1; i < next; ++i) CHECK(same(rec(i), -1, 0));
    cache.release(0x1234);  // not cached: nothing
    // A new bundle fills the free records from the first on, around the ones still taken.
    const size_t launch = cache.loadBundle(0x345).size();
    CHECK(same(rec(1), 0x345, 1));
    int members = 0, at = 2;
    for (; members < static_cast<int>(launch); ++at)
        if (at != next) {
            CHECK(same(rec(at), 0x345, 0));
            ++members;
        }
    CHECK(same(rec(at), -1, 0));
    CHECK(same(rec(next), 0x332, 1));
}

TEST_CASE("world3: the picture cache's records after a door equal the original's (r2/aircraft 12 -> 13)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    std::optional<GgStateFile> s;
    REQUIRE(loadWorld(f, "r2/aircraft/12_room1_open_door", s));
    const GgStateFile s13 = GgStateFile::load(*statePath("r2/aircraft/13_room5_arrived"));
    g.actors.actors[kPlayerActor].room = 5;
    CHECK(state08LeaveRoom(g) == 5);
    // Actors_LeaveRoom (the actors area) releases the player's and the projectile's bundles: done here
    // when its stub has not.
    Actor* movers[] = {&g.actors.actors[kPlayerActor], &g.actors.actors[kProjectileActor]};
    if (movers[0]->bundle != -1)
        for (Actor* a : movers) {
            if (a->bundle != -1) g.cache.releaseBundle(static_cast<uint16_t>(a->bundle));
            a->bundle = -1;
        }
    CHECK(state05EnterRoom(g) == 7);
    // Actors_EnterRoom's loads (the room's scale of each), likewise.
    if (movers[0]->bundle == -1) {
        const int scale = roomIsHall(g, 5) ? 1 : 0;
        movers[0]->bundle = kPlayerKind.spriteBase + scale;
        movers[1]->bundle = kProjectileKind.spriteBase + scale;
        for (Actor* a : movers) g.cache.loadBundle(static_cast<uint16_t>(a->bundle));
    }
    int differ = 0;
    for (int i = 0; i < PicCache::kSlots; ++i) {
        const auto at = static_cast<uint16_t>(dg::kPicCache + 8 * i);
        const PicCache::Slot want{static_cast<int16_t>(s13.dsWord(at)), static_cast<int16_t>(s13.dsWord(static_cast<uint16_t>(at + 2)))};
        const PicCache::Slot have = g.cache.slots()[static_cast<size_t>(i)];
        if (have.id != want.id || (want.id != -1 && have.uses != want.uses)) {
            ++differ;
            MESSAGE(std::format("record {}: {:04x} x{} instead of {:04x} x{}", i, static_cast<uint16_t>(have.id), have.uses,
                                static_cast<uint16_t>(want.id), want.uses));
        }
    }
    CHECK(differ == 0);
}

// ------------------------------------------------------------------------- a pick-up, the other rooms

TEST_CASE("world3: a part picked up: the room's boxes, the counters and the HUD (r2/aircraft 17 -> 18)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    std::optional<GgStateFile> s;
    REQUIRE(loadWorld(f, "r2/aircraft/17_room4_floor1", s));
    gggame::GameFixture ref(*ggFiles);
    std::optional<GgStateFile> s18;
    REQUIRE(loadWorld(ref, "r2/aircraft/18_room4_envelope", s18));
    constexpr int kPart = 8;  // the box at (235, 150) of room 4
    Room& room = g.world.rooms.room(4);
    int node = -1;
    for (const int i : room.items)
        if (g.world.rooms.pool()[static_cast<size_t>(i)].kind == kPart) node = i;
    REQUIRE(node >= 0);
    // Player_PickUpItem's world calls in its order (18f5:0002; the actors area's function): fewer than 2
    // of the category carried, Actor_AddPart (by the test when the actors area's is a stub), Hud_AddPart,
    // Room_RemoveItem, the strip bit, the parts needed, Score_Add(20) without the HUD bit.
    PartRecord& part = partRecord(g, kPart);
    REQUIRE(partListCountType(g, g.actors.playerParts, part.category) < 2);
    const bool needed = partIsNeeded(g, kPart);
    const size_t carried = g.actors.playerParts.size();
    int second = actorAddPart(g, kPlayerActor, kPart);
    if (g.actors.playerParts.size() == carried) {
        int last = -1;
        partListCountType(g, g.actors.playerParts, part.category, &last);
        part.flags &= PartRecord::kNoPoints;
        part.holder = 0;
        part.pair = static_cast<int16_t>(last);
        if (last != -1) partRecord(g, last).pair = static_cast<int16_t>(kPart | 0x8000);
        ++g.actors.actors[kPlayerActor].carried;
        g.actors.playerParts.push_back(kPart);
        second = last != -1;
    }
    hudAddPart(g, kPart);
    roomRemoveItem(g, 4, node);
    g.world.hud.dirty |= Hud::kStrip;
    if (!second && needed) --g.player.live.partsNeeded;
    if (!(part.flags & PartRecord::kNoPoints)) scoreAdd(g, 20);
    // As the next state holds them.
    const GgGame& r = ref.g();
    CHECK(g.player.points == r.player.points);
    CHECK(g.player.live.partsNeeded == r.player.live.partsNeeded);
    CHECK(room.items == r.world.rooms.room(4).items);
    CHECK(room.itemCount == r.world.rooms.room(4).itemCount);
    CHECK(g.world.rooms.pool()[static_cast<size_t>(node)].free);
    CHECK(g.actors.playerParts == r.actors.playerParts);
    for (const int16_t p : g.actors.playerParts) {
        CAPTURE(p);
        CHECK(g.world.parts.parts[static_cast<size_t>(p)].hudSprite.x == r.world.parts.parts[static_cast<size_t>(p)].hudSprite.x);
        CHECK(g.world.parts.parts[static_cast<size_t>(p)].hudSprite.y == r.world.parts.parts[static_cast<size_t>(p)].hudSprite.y);
    }
    CHECK(g.world.hud.stripFirst == r.world.hud.stripFirst);
    CHECK(g.world.hud.stripLast == r.world.hud.stripLast);
    CHECK(g.worldObjects().infoText == part.name);
    // The next Hud_Update draws the strip and the name box; once a key is held the labels and the
    // counters come back (the score is redrawn only then): the shot's HUD.
    hudUpdate(g, 0);
    CHECK(g.world.hud.labelsShown == 0);
    g.input().keyHeldSeen = 1;
    hudUpdate(g, 0);
    CHECK(g.world.hud.labelsShown == 1);
    g.display.flush();
    f.present();
    dumpFrame(f.frame(), "pickup18");
    const gggame::Mask playArea[] = {{0, 0, 319, 164}};
    CHECK(f.differingPixels(*testdata::ggCapture("r2/aircraft/18_envelope_collected.png"), playArea) == 0);
}

TEST_CASE("world3: re-entering room 1 of each building: the wipe and the room as the shots show it") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    for (const std::string name : {"r2/automotive/05_room1", "r2/altenergy/05_room1", "r2/aircraft/06_room1"}) {
        CAPTURE(name);
        gggame::GameFixture f(*ggFiles);
        GgGame& g = f.g();
        std::optional<GgStateFile> s;
        REQUIRE(loadWorld(f, name, s));
        const int room = g.playerRoom();
        REQUIRE(room == g.world.rooms.displayRoom);
        const uint64_t calls0 = g.rand.calls();
        f.start([](GgGame& game) {
            if (state08LeaveRoom(game) == 5 && state05EnterRoom(game) == 7) state07Warehouse(game);
        });
        const auto rows = f.step(1);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].randCalls - calls0 == 2);  // without chimps: the wipe's two
        CHECK(rows[0].displayRoom == room);
        // The back page is the room Room_Enter drew and saved, to the byte.
        CHECK(differences(g.video.screen().bytes(), s->screenBuffer(), 200, 364) == 0);
        // The player, the pointers, and the pads and flaps: the shot was taken a frame before the state,
        // the re-entry steps them twice (Room_QueueEntryLayers and the first frame).
        std::vector<gggame::Mask> masks = {actorRect(*s, kPlayerActor), actorRect(g, kPlayerActor), cursorRect(*s), cursorRect(g)};
        for (const RoomAnim& a : g.world.rooms.room(room).anims[0]) masks.push_back(around(a.sprite.x, a.sprite.y, a.sprite.w, a.sprite.h));
        f.finish();
        dumpFrame(f.frame(), name.substr(3, 6) + "_room");
        CHECK(f.differingPixels(*testdata::ggCapture(name + ".png"), masks) == 0);
    }
}

TEST_CASE("world3: from the workshop's tube into the first hall with the fade (w0/12 -> w0/14)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    std::optional<GgStateFile> s;
    REQUIRE(loadWorld(f, "w0/12_workshop", s));
    const GgStateFile s14 = GgStateFile::load(*statePath("w0/14_warehouse"));
    // The workshop's exit (2dee:0e4a, the vehicles area): Player_SequenceEnd with no object sends the
    // player to g_tubeRoom with the tube's entry sequence 53; the workshop fades out.
    Actor& p = g.actors.actors[kPlayerActor];
    p.doorObj = -1;
    p.prevRoom = 0;
    p.room = g.player.live.tubeRoom;
    p.pendingSeq = 53;
    p.exitSeq = 53;
    g.display.fadeOut();
    REQUIRE(g.playerRoom() == 1);
    const uint64_t calls0 = g.rand.calls();
    f.start([](GgGame& game) {
        if (state08LeaveRoom(game) == 5 && state05EnterRoom(game) == 7) state07Warehouse(game);
    });
    const auto rows = f.step(1);
    REQUIRE(rows.size() == 1);
    // Faded: a flush and the fade-in, no wipe; only the chimps' brains of the entry and first ticks.
    CHECK(!g.video.palette().faded());
    const uint64_t chimpCalls = rows[0].randCalls - calls0;
    MESSAGE(std::format("rand() calls of the entry and the first frame: {}", chimpCalls));
    // The hall's background, as the warehouse state's back page holds it (rows 9..164: the menu bar's
    // rows are the UI area's).
    CHECK(differences(g.video.screen().bytes(), s14.screenBuffer(), 209, 364) == 0);
    f.finish();
}

// ---------------------------------------------------------------------- the warehouse loop with chimps

TEST_CASE("world3: through a locked door with the chimps: 11 rand() calls before the puzzle (w0/15_at_door)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    std::optional<GgStateFile> s;
    REQUIRE(loadWorld(f, "w0/15_at_door", s));
    REQUIRE(g.player.withChimps == 1);
    std::string why;
    REQUIRE_MESSAGE(actorsRestoreFromState(g, *s, why), why);
    // REFERENCE_CAPTURES §9.9 / §10.7: the state with Up held (g_heldKeys = 4) goes through the door in
    // a frame or two; 11 rand() calls (the two chimps' brains) come before the puzzle choice.
    g.keyboard().isr().heldKeys = 4;
    const uint64_t calls0 = g.rand.calls();
    int16_t next = 0;
    f.start([&next](GgGame& game) { next = state07Warehouse(game, StateEntry::Resume); });
    for (int i = 0; i < 200 && !f.done(); ++i) f.step(1);
    REQUIRE(f.done());
    f.finish();
    CHECK(next == 8);
    CHECK(g.world.puzzleDoorHit == 1);
    CHECK(state08LeaveRoom(g) == 9);
    CHECK(g.rand.calls() - calls0 == 11);
}

TEST_CASE("world3: the warehouse's codes: Alt+S then Alt+W puts the player back on the bottom floor") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    std::optional<GgStateFile> s;
    REQUIRE(loadWorld(f, "r2/aircraft/22_room5_floor1", s));
    std::string why;
    actorsRestoreFromState(g, *s, why);
    const int room = g.playerRoom();
    REQUIRE(g.actors.actors[kPlayerActor].floor != 5);
    REQUIRE(f.keys("4:down:lalt, 8:tap:s, 16:tap:w, 24:up:lalt"));
    f.start([](GgGame& game) { state07Warehouse(game, StateEntry::Resume); });
    f.step(12);
    f.finish();
    const Actor& p = g.actors.actors[kPlayerActor];
    CHECK(g.playerRoom() == room);
    CHECK(p.anchorX == 10);
    CHECK(p.anchorY == (roomIsHall(g, room) ? 0x90 : 0x7F));
    CHECK(p.floor == (roomIsHall(g, room) || (roomGetFlags(g, room) & 2) ? 5 : 2));
    CHECK(p.wallsOn == 1);
    CHECK(p.contactsOn == 1);
}

TEST_CASE("world3: a warehouse state through the game's loader (--gg-load-state) draws its shot") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    const GgStateFile s = GgStateFile::load(*statePath("r2/aircraft/06_room1"));
    const GgLoadedState loaded = loadGgState(g, s);
    REQUIRE_MESSAGE(loaded.supported, loaded.why);
    REQUIRE(g.flow.state == 7);
    f.start([](GgGame& game) { state07Warehouse(game, StateEntry::Resume); });
    REQUIRE(f.step(1).size() == 1);
    f.finish();
    dumpFrame(f.frame(), "loader06");
    // The shot was taken a frame before the state: the player (the actors area's) is left out.
    const gggame::Mask masks[] = {actorRect(s, kPlayerActor), actorRect(g, kPlayerActor)};
    CHECK(f.differingPixels(*testdata::ggCapture("r2/aircraft/06_room1.png"), masks) == 0);
}
