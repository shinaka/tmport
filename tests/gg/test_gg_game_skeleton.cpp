// Wave 3 skeleton (re/gg/docs/ENGINE_MAP.md, "Wave 3 map"): the game links (every module's functions,
// Seq_Run's table), GgState starts as ssg.exe does (spot checks against the exe's DGROUP), the DGROUP
// codec round-trips the initial image and real DOSBox-X states, the state reader finds ssg.exe in a
// state and puts it into a game, the quirk helpers alias what the original aliases, and the fixture runs
// a game function frame by frame with scripted input. Only counts, offsets and per-tick numbers here.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <set>
#include <string>
#include <vector>

#include "gg/app/seq_run.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/actors.h"
#include "gg/game/chimps.h"
#include "gg/game/collision.h"
#include "gg/game/dam.h"
#include "gg/game/flow.h"
#include "gg/game/game.h"
#include "gg/game/game_state.h"
#include "gg/game/load_state.h"
#include "gg/game/parts.h"
#include "gg/game/player.h"
#include "gg/game/puzzles.h"
#include "gg/game/pz_balance.h"
#include "gg/game/pz_electric.h"
#include "gg/game/pz_energy.h"
#include "gg/game/pz_gears.h"
#include "gg/game/pz_launch.h"
#include "gg/game/pz_magnets.h"
#include "gg/game/pz_pieces.h"
#include "gg/game/pz_simple.h"
#include "gg/game/race.h"
#include "gg/game/results.h"
#include "gg/game/rooms.h"
#include "gg/game/sound.h"
#include "gg/game/trace.h"
#include "gg/game/trace_log.h"
#include "gg/game/workshop.h"
#include "gg/game/world.h"
#include "gg/save/gg_state_file.h"
#include "gg/ui/dialog.h"
#include "gg/ui/hints.h"
#include "gg/ui/menu_bar.h"
#include "gg/ui/menu_commands.h"
#include "gg/ui/players.h"
#include "gg/ui/signin.h"
#include "gg/ui/text_viewer.h"
#include "gg_game_fixture.h"
#include "gg_test_data.h"
#include "rtl/borland_rand.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

// The DOSBox-X states of the captures (savestates/<set>/<group>/*.sav), sorted.
std::vector<std::filesystem::path> stateFiles(const std::filesystem::path& captures, const char* set) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    const auto root = captures / "savestates" / set;
    if (!std::filesystem::is_directory(root, ec)) return out;
    for (const auto& e : std::filesystem::recursive_directory_iterator(root, ec))
        if (e.is_regular_file() && e.path().extension() == ".sav") out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

// The modelled bytes of every field that differ between two images, as "name+offset" (messages).
std::vector<std::string> modelledDifferences(std::span<const uint8_t> a, std::span<const uint8_t> b,
                                             const std::vector<DgroupField>& fields) {
    std::vector<std::string> out;
    for (const DgroupField& f : fields)
        for (size_t i = 0; i < f.size; ++i)
            if (f.modelled(i) && a[f.ds + i] != b[f.ds + i]) {
                out.push_back(std::format("{}+{:#x}", f.name, i));
                break;
            }
    return out;
}

}  // namespace

TEST_CASE("gg skeleton: every module's wave 3 functions link") {
    // Taking the addresses is enough: the linker must find a definition for each.
    const std::vector<const void*> fns = {
        // flow
        reinterpret_cast<const void*>(&gameMain), reinterpret_cast<const void*>(&gameInit),
        reinterpret_cast<const void*>(&gameShutdown), reinterpret_cast<const void*>(&runMainSequence),
        reinterpret_cast<const void*>(&state01Attract), reinterpret_cast<const void*>(&state03Hub),
        reinterpret_cast<const void*>(&state16SaveAndLeave), reinterpret_cast<const void*>(&state17End),
        reinterpret_cast<const void*>(&buildingFree), reinterpret_cast<const void*>(&cmdSaveAndExit),
        reinterpret_cast<const void*>(&installLzhKeyDrain), reinterpret_cast<const void*>(&loadGgState),
        reinterpret_cast<const void*>(&runFromLoadedState),
        // ui
        reinterpret_cast<const void*>(&state02SignIn), reinterpret_cast<const void*>(&dlgRunModal),
        reinterpret_cast<const void*>(&menuSetMenuBar), reinterpret_cast<const void*>(&menuRunDropDown),
        reinterpret_cast<const void*>(&runMenuCommand), reinterpret_cast<const void*>(&textViewRun),
        reinterpret_cast<const void*>(&hintQueue), reinterpret_cast<const void*>(&hintUpdate),
        reinterpret_cast<const void*>(&playersSaveCurrent),
        // world
        reinterpret_cast<const void*>(&state04BuildingSetup), reinterpret_cast<const void*>(&state05EnterRoom),
        reinterpret_cast<const void*>(&state08LeaveRoom), reinterpret_cast<const void*>(&roomsLoad),
        reinterpret_cast<const void*>(&roomEnter), reinterpret_cast<const void*>(&partsLoadCatalogue),
        reinterpret_cast<const void*>(&hudUpdate), reinterpret_cast<const void*>(&hudEscHook),
        reinterpret_cast<const void*>(&scoreAdd), reinterpret_cast<const void*>(&worldRestoreFromState),
        // actors
        reinterpret_cast<const void*>(&actorsTick), reinterpret_cast<const void*>(&actorRunStep),
        reinterpret_cast<const void*>(&playerBrain), reinterpret_cast<const void*>(&projectileLaunch),
        reinterpret_cast<const void*>(&actorCollideWalls), reinterpret_cast<const void*>(&chimpBrain),
        reinterpret_cast<const void*>(&chimpsSpawn), reinterpret_cast<const void*>(&actorsRestoreFromState),
        // puzzles
        reinterpret_cast<const void*>(&puzzleDispatch), reinterpret_cast<const void*>(&puzzlePickUnsolved),
        reinterpret_cast<const void*>(&puzzleAwardPoints), reinterpret_cast<const void*>(&damPlayModal),
        reinterpret_cast<const void*>(&puzzlesRestoreFromState),
        // vehicles
        reinterpret_cast<const void*>(&workshopRun), reinterpret_cast<const void*>(&blueprintRun),
        reinterpret_cast<const void*>(&state10ListVehicleParts), reinterpret_cast<const void*>(&raceLandEnergy),
        reinterpret_cast<const void*>(&raceAircraft), reinterpret_cast<const void*>(&mathDivRound),
        reinterpret_cast<const void*>(&state12RaceResult), reinterpret_cast<const void*>(&state13PostRaceMenu),
        reinterpret_cast<const void*>(&state14RedrawResult), reinterpret_cast<const void*>(&state15FinalMovie),
        reinterpret_cast<const void*>(&vehiclesRestoreFromState),
        // skeleton
        reinterpret_cast<const void*>(&traceSnapshot), reinterpret_cast<const void*>(&writeGgDgroupDump),
    };
    CHECK(std::none_of(fns.begin(), fns.end(), [](const void* p) { return p == nullptr; }));
    // The puzzle classes' runs (member functions).
    int (PzElectric::*e)(GgGame&, int) = &PzElectric::run;
    int (PzSimple::*s)(GgGame&, int) = &PzSimple::run;
    int (PzPieces::*pc)(GgGame&, int) = &PzPieces::run;
    int (PzGears::*gr)(GgGame&, int) = &PzGears::run;
    int (PzBalance::*b)(GgGame&, int) = &PzBalance::run;
    int (PzLaunch::*l)(GgGame&, int) = &PzLaunch::run;
    int (PzEnergy::*en)(GgGame&, int) = &PzEnergy::run;
    int (PzMagnets::*m)(GgGame&, int) = &PzMagnets::run;
    CHECK((e && s && pc && gr && b && l && en && m));
}

TEST_CASE("gg skeleton: the game context and Seq_Run's table") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    GgGame& g = f.g();
    // Seq_Run's table DS:00c2: 19 entries, states 1..17 bound, 0 and 18 NULL.
    const auto table = mainStateTable(g);
    REQUIRE(table.size() == dg::kStateTableCount);
    CHECK_FALSE(table[0]);
    CHECK_FALSE(table[18]);
    int bound = 0;
    for (int i = 1; i <= 17; ++i) bound += table[static_cast<size_t>(i)] ? 1 : 0;
    CHECK(bound == 17);
    // The modules' object holders exist.
    CHECK(static_cast<void*>(&g.flowObjects()) != static_cast<void*>(&g.vehiclesObjects()));
    CHECK(static_cast<void*>(&g.uiObjects()) != static_cast<void*>(&g.puzzlesObjects()));
    // The HUD's pictures are bound to the state's HUD globals.
    CHECK(&g.hud.state() == &g.world.hud);
}

TEST_CASE("gg skeleton: the initial state is ssg.exe's start-up") {
    GG_REQUIRE_DATA();
    const GgExe& exe = ggFiles->exe();
    const std::vector<uint8_t> image = initialDgroupImage(exe);
    REQUIRE(image.size() == dg::kDgroupSize);
    // BSS is zero.
    CHECK(std::all_of(image.begin() + dg::kInitEnd, image.end(), [](uint8_t b) { return b == 0; }));

    const GgState s = initialState(exe);
    // Values main and the init functions set.
    CHECK(s.flow.state == 1);
    CHECK(s.world.hud.flashCount == -1);
    CHECK(s.world.hud.flashPeriod == 3);
    CHECK(s.ui.hints.pending() == 0);
    CHECK(s.actors.actors[kPlayerActor].delay == 2);
    CHECK(s.actors.actors[kProjectileActor].id == 1);
    CHECK(s.actors.actors[kProjectileActor].disabled == 1);
    CHECK(s.actors.actors[kPlayerActor].transTable == dg::kPlayerTransitions);
    CHECK(s.actors.actors[kProjectileActor].transTable == dg::kProjectileTransitions);
    // Initialised data read from the exe.
    CHECK(static_cast<uint16_t>(s.sound.titleMusicBank) == exe.ds16(0x0098));
    CHECK(static_cast<uint16_t>(s.runtime.displayCode) == exe.ds16(dg::kDisplayCode));
    CHECK(static_cast<uint16_t>(s.runtime.soundDevice) == exe.ds16(dg::kSoundDevice));
    CHECK(static_cast<uint16_t>(s.world.hud.colorScheme) == exe.ds16(0x1318));
    for (size_t w = 0; w < kChimpListWords; ++w)
        CHECK(s.actors.chimpListData[w] == exe.ds16(static_cast<uint16_t>(dg::kChimpListData + 2 * w)));

    // The fields the start-up functions do not set are the exe's bytes: the dump of the state on the
    // initial image differs only in the fields gameInitState writes.
    const std::vector<uint8_t> dump = dumpToDgroup(s, image);
    const auto changed = diffDgroup(image, dump);
    std::string names;
    for (const auto n : changed) names += std::string(n) + " ";
    MESSAGE("fields the start-up sets: " << names);
    CHECK(changed.size() <= 6);
    CHECK(std::find(changed.begin(), changed.end(), "g_state") != changed.end());
    CHECK(std::find(changed.begin(), changed.end(), "g_actors") != changed.end());
    CHECK(std::find(changed.begin(), changed.end(), "g_hintSlots") != changed.end());
    // Without the init functions the codec is the identity on the exe's image.
    GgState raw;
    loadFromDgroup(raw, image);
    CHECK(dumpToDgroup(raw, image) == image);
    CHECK(diffDgroup(image, dumpToDgroup(raw, std::vector<uint8_t>(image.size(), 0))).empty());

    // The field registry stays inside DGROUP and its records are whole.
    const auto& fields = gameStateFields();
    MESSAGE("GgState DGROUP fields: " << fields.size());
    CHECK(fields.size() >= 170);
    for (const DgroupField& fd : fields) {
        INFO(fd.name);
        CHECK(static_cast<size_t>(fd.ds) + fd.size <= dg::kDgroupSize);
        if (fd.stride) CHECK(fd.size % fd.stride == 0);
    }
}

TEST_CASE("gg skeleton: the read-only tables") {
    GG_REQUIRE_DATA();
    const GgTables t = GgTables::load(ggFiles->exe());
    CHECK(t.dgroup.size() == dg::kInitEnd);
    const int withTransitions = static_cast<int>(std::count(t.playerHasTransitions.begin(), t.playerHasTransitions.end(), true));
    CHECK(withTransitions == 18);  // 68 pointers, 50 of them NULL
    int chimpSeqs = 0;
    for (const auto& m : t.chimpMoves) chimpSeqs += m ? 1 : 0;
    CHECK(chimpSeqs > 0);
    CHECK(chimpSeqs <= GgTables::kChimpSequences);
    for (const uint16_t off : t.chimpListOffsets) {
        CHECK(off >= dg::kChimpListData);
        CHECK(off < dg::kChimpListDataEnd);
    }
    CHECK_FALSE(t.contactsPlayer.empty());
    CHECK_FALSE(t.contactsChimp.empty());
    // Ascending rank thresholds.
    CHECK(std::is_sorted(t.rankThresholds.begin(), t.rankThresholds.end()));
}

TEST_CASE("gg skeleton: the quirk helpers alias what the original aliases") {
    GgState s;
    // A 16th typed-name byte lands in DS:a064.
    typedNameByte(s.ui, 16) = 0x1F;
    CHECK(s.ui.typedName16 == 0x1F);
    CHECK_THROWS_AS(typedNameByte(s.ui, 17), std::out_of_range);
    // Row 10 of the electric path table is g_elecPathCount.
    CHECK(elecPathWord(s.puzzles.electric, 10, 0) == &s.puzzles.electric.pathCount);
    CHECK(elecPathWord(s.puzzles.electric, 10, 1) == nullptr);
    // The energy form rows: sent[] is indexed the other way round.
    CHECK(&energySent(s.puzzles.energy, 1, 2) == &s.puzzles.energy.forms[1].words[6]);
    CHECK(&energySatisfied(s.puzzles.energy, 1, 2) == &s.puzzles.energy.forms[1].words[2]);
    CHECK(&energyLine(s.puzzles.energy, 3, 0) == &s.puzzles.energy.forms[3].words[8]);
    // The player record's tables.
    puzzleSolved(s.player, 7, 32) = 1;
    CHECK_THROWS_AS(puzzleSolved(s.player, 8, 0), std::out_of_range);
    CHECK_THROWS_AS(hintSeen(s.player, 0x1042), std::out_of_range);
    hintSeen(s.player, 0x1055) = 1;
    CHECK(&buildingBlock(s.player, 0) != &buildingBlock(s.player, 1));
}

TEST_CASE("gg skeleton: the hint queue and the part deck through the DGROUP codec") {
    GgState s;
    s.ui.hints.queue(0x1043, 10, 100, false, false, {}, 0x458A);
    s.ui.hints.queue(0x1050, 5, 100, false, false);
    s.world.deck.restore([] {
        std::array<int16_t, PartDeck::kImageWords> img{};
        for (size_t i = 0; i < img.size(); ++i) img[i] = static_cast<int16_t>(i * 3 % 61);
        return img;
    }(), 7);
    const std::vector<uint8_t> zero(dg::kDgroupSize, 0);
    const std::vector<uint8_t> dump = dumpToDgroup(s, zero);
    GgState back;
    loadFromDgroup(back, dump);
    auto ids = [](const HintQueue& q) {
        std::string out;
        for (const uint16_t id : q.pendingIds()) out += std::format("{:x} ", id);
        for (const int k : q.order()) out += std::format("[{}]", k);
        return out;
    };
    INFO("queued: " << ids(s.ui.hints) << " loaded: " << ids(back.ui.hints));
    CHECK(back.ui.hints.pendingIds() == s.ui.hints.pendingIds());
    CHECK(back.ui.hints.slot(0).cancelDs == 0x458A);
    CHECK(back.world.deck.image() == s.world.deck.image());
    CHECK(dumpToDgroup(back, zero) == dump);
}

TEST_CASE("gg skeleton: Borland rand counts its calls and the recording facade numbers handles") {
    BorlandRand r;
    r.srand(0x29CF);
    for (int i = 0; i < 5; ++i) (void)r.rand();
    CHECK(r.calls() == 5);
    RecordingSound snd;
    const int music = snd.loadMusicBank(0x30CA);
    const int fx = snd.loadEffectBank(0x108F);
    CHECK(fx == music + 1);
    CHECK(snd.loadSong(1) == fx + RecordingSound::kEffectSpan);
    snd.playEffect(fx + 3, 0, 4);
    CHECK(snd.isPlaying(fx + 3) == 0);
    const auto calls = snd.takeCalls();
    REQUIRE(calls.size() == 4);
    CHECK(calls.back().op == "playEffect");
    CHECK(snd.takeCalls().empty());
}

TEST_CASE("gg skeleton: real DOSBox-X states round-trip through GgState") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const std::vector<uint8_t> initial = initialDgroupImage(ggFiles->exe());
    int states = 0, fieldsChecked = 0;
    for (const char* set : {"r2", "w0"}) {
        for (const auto& path : stateFiles(*captures, set)) {
            INFO(path.filename().string());
            std::optional<GgStateFile> sf;
            try {
                sf.emplace(GgStateFile::load(path));
            } catch (const std::exception& e) {
                FAIL_CHECK("not readable: " << e.what());
                continue;
            }
            const auto ds = sf->dgroup();
            REQUIRE(ds.size() == dg::kDgroupSize);
            CHECK((sf->relocation() == -0x786 || sf->relocation() == -0x7dc));
            // DGROUP -> GgState -> DGROUP: every modelled byte comes back (on the exe's image and on
            // the state's own image).
            GgState s;
            loadFromDgroup(s, ds);
            const auto onInitial = dumpToDgroup(s, initial);
            const auto diff = modelledDifferences(onInitial, ds, gameStateFields());
            CHECK_MESSAGE(diff.empty(), (diff.empty() ? std::string{} : diff.front()));
            CHECK(std::equal(ds.begin(), ds.end(), dumpToDgroup(s, ds).begin()));
            fieldsChecked += static_cast<int>(gameStateFields().size());
            ++states;
        }
    }
    MESSAGE("states round-tripped: " << states << " (" << fieldsChecked << " field checks)");
    CHECK(states > 0);
}

TEST_CASE("gg skeleton: a DOSBox-X state's screen, DAC and globals go into the game") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto sav = testdata::ggCapture("savestates/r2/aircraft/06_room1.sav");
    if (!sav) {
        MESSAGE("savestates/r2/aircraft/06_room1.sav missing: test skipped");
        return;
    }
    const GgStateFile sf = GgStateFile::load(*sav);
    // The screen buffer's visible page is what VRAM shows; the DAC is 6-bit and is the shadow palette.
    const auto screen = sf.screenBuffer();
    REQUIRE(screen.size() == GgStateFile::kScreenBytes);
    REQUIRE(sf.vga().has_value());
    CHECK(std::equal(sf.vga()->vram.begin(), sf.vga()->vram.end(), screen.begin()));
    CHECK(std::all_of(sf.vga()->dac.begin(), sf.vga()->dac.end(), [](uint8_t c) { return c <= 63; }));
    const auto shadow = sf.dgroup().subspan(dg::kPalShadow, static_cast<size_t>(kPaletteBytes));
    CHECK(std::equal(shadow.begin(), shadow.end(), sf.vga()->dac.begin()));
    // ssg.exe was stopped inside its own code.
    CHECK_FALSE(sf.stackReturns().empty());

    gggame::GameFixture f(*ggFiles);
    const auto missing = f.loadState(*sav);
    REQUIRE(missing.has_value());
    for (const auto& m : *missing) MESSAGE("not applied: " << m);
    GgGame& g = f.g();
    CHECK(static_cast<uint16_t>(g.flow.state) == sf.dsWord(dg::kState));
    CHECK(g.rand.seed() == sf.dsDword(dg::kRandState));
    CHECK(g.ticks() == sf.dsDword(dg::kTicks));
    CHECK(std::ranges::equal(g.video.screen().screenPixels(), screen.first(64000)));
    // The whole game dumped on the state's image: the state's fields and the service-held ones come
    // back, except the store-only service globals.
    const auto dump = dumpToDgroup(g, sf.dgroup());
    CHECK(modelledDifferences(dump, sf.dgroup(), gameStateFields()).empty());
    const std::set<std::string_view> storeOnly = {"g_kbdRingWrite", "g_kbdRingRead", "g_mouseButtons", "g_mouseHideState"};
    for (const auto n : diffDgroup(dump, sf.dgroup(), &serviceFields())) {
        INFO("service global not restored: " << n);
        CHECK(storeOnly.count(n) == 1);
    }
    // The player record of the state's PLAYERS.DAT copy is in the store.
    CHECK(f.players().roster().count > 0);
    f.present();
    // The state's picture against DOSBox-X's screenshot of the room: the screenshot is not of the saved
    // instant (123 pixels differ, all inside x 103..257, y 49..163, where the room moves), but a wrong
    // screen, page or DAC would differ almost everywhere.
    if (const auto png = testdata::ggCapture("r2/aircraft/06_room1.png")) {
        const int n = f.differingPixels(*png);
        MESSAGE("06_room1: " << n << " pixels differ from the screenshot");
        CHECK(n >= 0);
        CHECK(n < 320);
    }
}

TEST_CASE("gg skeleton: the fixture steps a game function frame by frame") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    REQUIRE(f.keys("6:down:right, 30:up:right"));
    std::vector<uint16_t> held;
    f.start([&held](GgGame& g) {
        for (;;) {
            g.timers().set(0, 4);  // a paced loop: Timer_Set(0, 4) ... Timer_Wait(0)
            g.rt.getInputCode();
            held.push_back(g.keyboard().isr().heldKeys);
            g.rt.timerWait(0);
        }
    });
    const auto rows = f.step(10);
    REQUIRE(rows.size() == 10);
    for (size_t i = 1; i < rows.size(); ++i) {
        CHECK(rows[i].yield == YieldPoint::TimerWait);
        CHECK(rows[i].slot == 0);
        CHECK(rows[i].ticks - rows[i - 1].ticks == 4);  // the 4-tick frames
        CHECK(rows[i].frame == rows[i - 1].frame + 1);
    }
    f.finish();
    CHECK(f.done());
    // The key was held between PIT ticks 6 and 30: frames 2..7 saw it (each frame polls at 4 n).
    const auto withRight = std::count_if(held.begin(), held.end(), [](uint16_t h) { return (h & 2) != 0; });
    CHECK(withRight == 6);

    // A returned function is done; a second one can start.
    f.start([](GgGame& g) { g.rt.timerWait(0); });
    (void)f.step(3);
    CHECK(f.done());
    f.finish();
}

TEST_CASE("gg skeleton: the fixture restores a PLAYERS.DAT record") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto players = testdata::ggCapture("savestates/r2/aircraft/06_room1.players");
    if (!players) {
        MESSAGE("savestates/r2/aircraft/06_room1.players missing: test skipped");
        return;
    }
    gggame::GameFixture f(*ggFiles);
    REQUIRE(f.loadPlayers(*players));
    const Roster r = f.g().ui.roster;
    REQUIRE(r.count > 0);
    const auto& entry = r.entries[0];
    REQUIRE(f.restorePlayer(entry.slot));
    CHECK(f.g().player.slot == static_cast<int16_t>(entry.slot));
    CHECK(f.g().player.points == entry.points);
    CHECK(f.g().ui.rosterIndex == 0);
    CHECK(f.restorePlayerByName(entry.nameString()));
    CHECK_FALSE(f.restorePlayer(99));
}
