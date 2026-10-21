// GameMain (1118:0000, game/game_main.h) end to end: the state machine S4-S13 of
// re/docs/ARCHITECTURE.md §2 on a headless engine with a manual clock, driven by the --keys
// engine (app::ScriptInput) exactly like `tmport --headless --keys`. The transitions are checked
// through GameMainOptions::trace, the saves through users.dat. Needs TM_DATA (skips without it).
// No window, no audio.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "anim/anim_engine.h"
#include "anim/anim_player.h"
#include "app/app_context.h"
#include "app/engine_glue.h"
#include "app/game_main.h"
#include "app/menu_commands.h"
#include "app/script_input.h"
#include "game/game_main.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/room_table.h"
#include "game/rules.h"
#include "input/win_msg.h"
#include "mischief/timing/clock.h"
#include "platform/engine.h"
#include "save/settings.h"
#include "save/user_record.h"
#include "save/users_file.h"
#include "test_data.h"
#include "text/font.h"
#include "text/text_renderer.h"
#include "ui/game_dialog.h"
#include "ui/menu_bar.h"

using namespace tmport;
using namespace tmport::game;

namespace {

std::filesystem::path tempDir(const std::string& name) {
    auto d = std::filesystem::temp_directory_path() / "tmport_game_main_tests" / name;
    std::error_code ec;
    std::filesystem::remove_all(d, ec);
    std::filesystem::create_directories(d, ec);
    return d;
}

std::string repeat(std::string_view step, int n) {
    std::string s;
    for (int i = 0; i < n; ++i) {
        if (i) s += ',';
        s += step;
    }
    return s;
}

// Title (Space ends the pages, Space ends the closing animation), then the Club Sign-In.
const std::string kToSignIn = "space,wait:800,space,wait:2000";
// A new player "Ann" from an empty roster, then hints 0 and 2 dismissed with Enter in room 9.
const std::string kNewPlayer = kToSignIn + ",a,n,n,enter,wait:1500,enter,wait:800,enter,wait:800";
// Esc in a room -> the Exit box -> Yes.
const std::string kExitYes = "esc,wait:300,y,wait:500";

// The parts the bootstrap builds (app/bootstrap.cpp), headless, on a manual clock, without audio.
struct MainRig {
    mischief::ManualClock clock{1000};
    std::unique_ptr<Engine> engine;
    std::unique_ptr<text::Font> font;
    std::unique_ptr<text::TextRenderer> text;
    std::unique_ptr<ui::DialogSystem> dialogs;
    std::unique_ptr<anim::AnimEngine> anim;
    std::unique_ptr<ui::MenuBar> menu;
    std::unique_ptr<GameTables> tables;
    std::unique_ptr<GameState> state;
    save::Paths paths;
    std::unique_ptr<AppContext> ctx;
    std::unique_ptr<app::EngineGlue> glue;
    std::vector<std::string> trace;
    std::function<void(std::string_view)> onTrace;
    std::vector<int> animSfx;  // DAM FC opcodes through AnimSound
    std::vector<std::pair<uint64_t, WinMsg>> timed;  // messages posted when the clock reaches the time

    MainRig(GameFiles& g, const std::filesystem::path& dir) {
        EngineConfig cfg;
        cfg.clock = &clock;
        cfg.tapLatch = true;  // one scripted arrow press = one tick of a held arrow
        engine = std::make_unique<Engine>(g, mischief::gfx::ColorMode::Colour256, cfg);
        clock.onSleep = [this](uint64_t now) {
            for (auto it = timed.begin(); it != timed.end();) {
                if (it->first <= now) {
                    engine->input().post(it->second);
                    it = timed.erase(it);
                } else {
                    ++it;
                }
            }
        };
        font = std::make_unique<text::Font>(text::Font::load(*g.res));
        text = std::make_unique<text::TextRenderer>(*font, &engine->frame(), &engine->presenter());
        dialogs = std::make_unique<ui::DialogSystem>(*engine, *font);
        // As App::run does headless: the dialogs poll the engine (scripted input arrives one fetch later).
        dialogs->hooks().getInputCode = [this] {
            const uint16_t c = engine->getInputCode();
            if (c == 0) clock.sleepMs(1);
            return c;
        };
        anim::AnimSound sound;
        sound.play = [this](int n) { animSfx.push_back(n); };
        sound.stop = [] {};
        anim = std::make_unique<anim::AnimEngine>(engine->frame(), anim::enginePresent(*engine), std::move(sound));
        menu = std::make_unique<ui::MenuBar>(*engine, ui::MenuBarModel::fromExe(*g.exe), *font);
        tables = std::make_unique<GameTables>(GameTables::load(*g.exe, *g.res));
        state = std::make_unique<GameState>(GameState::initial(*tables));
        paths.prefDir = dir;
        paths.usersDat = dir / "users.dat";
        ctx = std::make_unique<AppContext>(AppContext{*engine, g, *font, *text, *dialogs});
        ctx->anim = anim.get();
        ctx->menu = menu.get();
        ctx->state = state.get();
        ctx->paths = &paths;
        app::registerMenuCommands(*ctx);
        glue = std::make_unique<app::EngineGlue>(*ctx);
    }
    ~MainRig() {
        clock.onSleep = {};
        glue.reset();
        menu.reset();
    }

    // Runs GameMain with the script; the run ends when the game quits (or the script is used up).
    void run(const std::string& keys, uint16_t seed = 0x1234) {
        std::string error;
        auto steps = app::parseScript(keys, error);
        REQUIRE_MESSAGE(steps, error);
        app::ScriptInput script(*engine, std::move(*steps), true);
        GameMainOptions o;
        o.seed = seed;
        o.trace = [this](std::string_view line) {
            trace.emplace_back(line);
            if (onTrace) onTrace(line);
        };
        app::runGameMain(*ctx, [&o](AppContext& c) { gameMain(c, o); });
        CHECK(script.finished());
    }

    // Posts a key (WM_CHAR code, or 0x100 + VK for an arrow: down + up) `ms` from now.
    void postIn(uint64_t ms, uint16_t code) {
        const uint64_t at = clock.nowMs() + ms;
        if (code >= 0x100) {
            timed.emplace_back(at, WinMsg::keyDown(static_cast<uint16_t>(code - 0x100)));
            timed.emplace_back(at, WinMsg::keyUp(static_cast<uint16_t>(code - 0x100)));
        } else {
            timed.emplace_back(at, WinMsg::chr(static_cast<uint8_t>(code)));
        }
    }

    size_t find(std::string_view prefix, size_t from = 0) const {
        for (size_t i = from; i < trace.size(); ++i)
            if (trace[i].starts_with(prefix)) return i;
        return std::string::npos;
    }
    bool has(std::string_view prefix) const { return find(prefix) != std::string::npos; }
    size_t count(std::string_view prefix) const {
        return static_cast<size_t>(
            std::count_if(trace.begin(), trace.end(), [&](const std::string& l) { return l.starts_with(prefix); }));
    }
    // The prefixes occur in this order (each after the previous one).
    bool inOrder(std::initializer_list<std::string_view> prefixes) const {
        size_t at = 0;
        for (std::string_view p : prefixes) {
            const size_t i = find(p, at);
            if (i == std::string::npos) {
                MESSAGE("trace line not found (in order): " << std::string(p));
                return false;
            }
            at = i + 1;
        }
        return true;
    }
    void dumpTrace() const {
        for (const auto& l : trace) MESSAGE(l);
    }

    std::optional<save::PlayerRecord> record(std::string_view name) const {
        save::UsersTable t;
        if (!save::readUsersFile(paths.usersDat, t)) return std::nullopt;
        const int i = t.findByName(name);
        if (i < 0 || i >= t.count) return std::nullopt;
        return save::PlayerRecord::fromSlot(t.slots[static_cast<size_t>(i)]);
    }
};

int treasuresOnLevel(const save::PlayerRecord& r, int level) {
    static constexpr int kFirst[] = {9, 23, 36}, kLast[] = {22, 35, 48};
    int n = 0;
    for (int room = kFirst[level]; room <= kLast[level]; ++room)
        for (int k = 0; k < 3; ++k) n += r.spotContent[static_cast<size_t>(room * 3 + k)] == kContentTreasure;
    return n;
}

}  // namespace

TEST_CASE("game main: title Esc + Yes ends the game without a sign-in (S5 -> S12)") {
    TM_REQUIRE_GAME_DATA();
    MainRig r(*g, tempDir("title_quit"));
    r.run("esc,wait:300,y,wait:300");
    CHECK(r.inOrder({"S4 game start", "S5 title", "S12 shutdown"}));
    CHECK_FALSE(r.has("S6 "));
    CHECK_FALSE(std::filesystem::exists(r.paths.usersDat));  // nobody signed in: nothing saved
    CHECK(r.ctx->state == r.state.get());
    CHECK_FALSE(r.ctx->services.saveCurrentPlayer);
    CHECK_FALSE(r.ctx->services.seeRank);
}

TEST_CASE("game main: the sign-in's exit prompt ends the game (S7 -> S12), nothing saved") {
    TM_REQUIRE_GAME_DATA();
    MainRig r(*g, tempDir("signin_quit"));
    r.run(kToSignIn + ",esc,wait:300,y,wait:300");
    CHECK(r.inOrder({"S5 title", "S6 restart point: r = 0", "S7 sign-in", "S12 shutdown"}));
    CHECK_FALSE(r.has("S8 "));
    CHECK_FALSE(std::filesystem::exists(r.paths.usersDat));
}

TEST_CASE("game main: new player -> climb set-up in room 9 -> hints 0 and 2 -> Esc + Yes saves twice") {
    TM_REQUIRE_GAME_DATA();
    const auto dir = tempDir("new_player");
    MainRig r(*g, dir);
    // The presented game area at the black play-field of Room_Present(1) and when the first hint
    // starts (the room-9 start pose), for comparisons with the original's captures (E3 03 / 04).
    int pauses = 0;
    r.onTrace = [&](std::string_view l) {
        if (l.starts_with("room: presentation pause") && pauses++ == 0)
            r.engine->presenter().writeGameAreaPng(dir / "e3_03_playfield_black.png");
        if (l.starts_with("S9 hint 0")) r.engine->presenter().writeGameAreaPng(dir / "e3_04_room9_start_pose.png");
    };
    r.run(kNewPlayer + "," + kExitYes);
    CHECK(r.inOrder({"S4 game start", "S5 title", "S6 restart point: r = 0", "S7 sign-in", "S7 signed in: Ann (new player)",
                     "S8 climb set-up: seed 0x1234, new climb, room 9, climbs 0, tier 0", "music: zone 1",
                     "room: presentation pause", "S9 hint 0", "S9 hint 2", "save: exit (player Ann, room 9",
                     "S12 shutdown", "save: shutdown (player Ann, room 9"}));
    CHECK(r.count("save: ") == 2);  // MenuCmd_Exit and the shutdown tail (1118:03eb)
    CHECK(r.menu->installed());  // the shutdown tail does not remove it (the app does, after GameMain)
    // The record on disk (ARCHITECTURE.md §4.3).
    const auto rec = r.record("Ann");
    REQUIRE(rec);
    CHECK(rec->location == 9);
    CHECK(rec->hintUnseenMask == 0xFFFFFFFAu);  // hints 0 and 2 consumed
    CHECK(rec->totalTreasures == 0);
    CHECK(rec->climbsDone == 0);
    CHECK(rec->coins == 10);
    CHECK(rec->nets == 10);
    // srand(seed) immediately before the climb set-up on the shared generator: the deal and the
    // layout are the ones setupClimb makes from that seed.
    GameState x = GameState::initial(*r.tables);
    initNewPlayer(x);
    mischief::BorlandRand rng;
    rng.srand(0x1234);
    setupClimb(x, *r.tables, rng);
    const save::PlayerRecord expected = toPlayerRecord(x);
    CHECK(rec->spotKind == expected.spotKind);
    CHECK(rec->spotContent == expected.spotContent);
    CHECK(rec->clueFlags == expected.clueFlags);
    CHECK(rec->clueAttr == expected.clueAttr);
    CHECK(rec->quizSlots == expected.quizSlots);
    // GameMain gave the context back.
    CHECK(r.ctx->state == r.state.get());
    CHECK_FALSE(r.ctx->services.saveCurrentPlayer);
    if (!r.has("S12 shutdown")) r.dumpTrace();
}

TEST_CASE("game main: File > Start Another Game -> restart point r = 1 -> sign-in with the player saved -> resume") {
    TM_REQUIRE_GAME_DATA();
    MainRig r(*g, tempDir("restart"));
    std::optional<save::PlayerRecord> atSignIn;
    r.onTrace = [&](std::string_view l) {
        if (l.starts_with("S7 signed in: Ann (existing player)")) atSignIn = r.record("Ann");
    };
    // File menu: See Rank, Start Another Game, Exit.
    r.run(kNewPlayer + ",alt+f,down,enter,wait:300,y,wait:2500,down,enter,wait:1500," + kExitYes);
    CHECK(r.inOrder({"S6 restart point: r = 0", "S8 climb set-up: seed 0x1234, new climb, room 9", "S9 hint 2",
                     "S6 restart point: r = 1", "S7 sign-in (restart)", "S7 signed in: Ann (existing player), slot 0, room 9",
                     "S8 climb set-up: seed 0x1234, resumed climb, room 9", "save: exit", "S12 shutdown"}));
    CHECK(r.count("S9 hint ") == 2);  // the once-only hints do not come back for the resumed climb
    REQUIRE(atSignIn);                // the sign-in saved the player before showing the roster
    CHECK(atSignIn->location == 9);
    CHECK(atSignIn->hintUnseenMask == 0xFFFFFFFAu);
    if (!r.has("S7 sign-in (restart)")) r.dumpTrace();
}

TEST_CASE("game main: File > See Rank shows the rank screen inside the climb and returns to the room") {
    TM_REQUIRE_GAME_DATA();
    MainRig r(*g, tempDir("see_rank"));
    r.run(kNewPlayer + ",alt+f,enter,wait:1500,enter,wait:500," + kExitYes);
    CHECK(r.inOrder({"S9 hint 2", "See Rank", "scene: rank screen: total 0 rank 0", "save: exit", "S12 shutdown"}));
}

TEST_CASE("game main: a door into the hall: music stops, room 0 is presented, then its music starts") {
    TM_REQUIRE_GAME_DATA();
    const auto dir = tempDir("door");
    MainRig r(*g, dir);
    // The presented game area at the black play-field of the transition and after it (E4 06 / 07).
    int pauses = 0;
    r.onTrace = [&](std::string_view l) {
        if (l.starts_with("room: presentation pause") && pauses++ == 1)
            r.engine->presenter().writeGameAreaPng(dir / "e4_06_black_to_room0.png");
        if (l.starts_with("music: zone 0")) r.engine->presenter().writeGameAreaPng(dir / "e4_07_room0_start_pose.png");
    };
    // Up at the room-9 start pose enters the door to the hall (transition 1, zone 1 -> 0).
    r.run(kNewPlayer + "," + repeat("up", 5) + ",wait:1000," + kExitYes);
    CHECK(r.inOrder({"S8 climb set-up", "music: zone 1", "S9 hint 2", "music: stop", "room: presentation pause",
                     "music: zone 0", "S9 room 9 -> 0 (transition 1, zone change)", "save: exit (player Ann, room 0"}));
    CHECK_FALSE(r.has("S10 "));  // a zone change, not a level change
}

TEST_CASE("game main: the castle climb ends the climb: save before the score, celebration, tier, next climb in room 0") {
    TM_REQUIRE_GAME_DATA();
    const auto dir = tempDir("climb_end");
    MainRig r(*g, dir);
    // A player at the castle wall (room 49) on tier 0 (castle layout 0, no wall creature), total 24
    // and 1 treasure this climb: 24 + 1 = 25 reaches rank 2 (tier 1). No once-only hints left.
    GameState x = GameState::initial(*r.tables);
    x.player.setName("Gate1");
    initNewPlayer(x);
    mischief::BorlandRand rng;
    rng.srand(7);
    setupClimb(x, *r.tables, rng);
    x.player.room = kCastleRoom;
    x.rank.totalTreasures = 24;
    x.inv.climbPoints = 1;
    x.inv.coins = 9;
    x.hints.unseen = 0;
    const save::PlayerRecord crafted = toPlayerRecord(x);
    {
        save::UsersTable t;
        crafted.writeTo(t.slots[0]);
        t.recount();
        REQUIRE(save::writeUsersFile(r.paths.usersDat, t));
    }
    std::optional<save::PlayerRecord> atClimbEnd;
    std::optional<RankState> afterCelebration;
    r.onTrace = [&](std::string_view l) {
        if (l.starts_with("save: climb end")) atClimbEnd = r.record("Gate1");
        if (l.starts_with("S13 celebrated")) afterCelebration = r.state->rank;
    };
    // Sign-in: Down selects Gate1. Castle screen 0: walk right under the ladder (column 2), climb;
    // on screen 1 jump right to column 3, climb off the top of screen 3. Then the celebration and
    // the rank screen (Enter), the next climb, Exit.
    r.run(kToSignIn + ",down,enter,wait:1500," + repeat("right", 9) + ",wait:300," + repeat("up", 14) + ",right," +
          repeat("up", 75) + ",wait:30000,enter,wait:1500," + kExitYes);
    CHECK(r.inOrder({"S7 signed in: Gate1 (existing player)", "S8 climb set-up: seed 0x1234, resumed climb, room 49",
                     "music: zone 7", "S11 castle climb", "scene: castle: layout 0", "scene: castle: done", "S13 climb end",
                     "music: stop", "save: climb end (player Gate1, room 254, total 24, climbs 0, tier 0)",
                     "scene: celebration: climbs 1 total 25 rank 2 promoted 1 champion 0",
                     "scene: rank screen: total 25 rank 2", "S13 celebrated: climbs 1, total 25, rank 2, tier 1, promoted",
                     "S8 climb set-up: seed 0x1234, new climb, room 0, climbs 1, tier 1", "music: zone 0",
                     "save: exit (player Gate1, room 0, total 25, climbs 1, tier 1)", "S12 shutdown"}));
    // The climb-end save (1118:03b9) happens before Climb_Celebrate adds the score.
    REQUIRE(atClimbEnd);
    CHECK(atClimbEnd->location == kRoomNewClimb);
    CHECK(atClimbEnd->totalTreasures == 24);
    CHECK(atClimbEnd->climbsDone == 0);
    CHECK(atClimbEnd->tier == 0);
    CHECK(atClimbEnd->climbTreasures == 1);
    CHECK(atClimbEnd->haveKey == 0);
    CHECK(atClimbEnd->gemsFound == 0);
    CHECK(atClimbEnd->clueWords == 0);
    CHECK(atClimbEnd->coins == 9);
    CHECK(atClimbEnd->spotContent == crafted.spotContent);
    REQUIRE(afterCelebration);
    CHECK(afterCelebration->climbsDone == 1);
    CHECK(afterCelebration->totalTreasures == 25);
    CHECK(afterCelebration->rankTier == 1);  // Rank_UpdateTier after the celebration
    // The Exit in the next climb: room 0, the new total, tier 1, a new tier-1 layout.
    const auto fin = r.record("Gate1");
    REQUIRE(fin);
    CHECK(fin->location == 0);
    CHECK(fin->totalTreasures == 25);
    CHECK(fin->climbsDone == 1);
    CHECK(fin->tier == 1);
    CHECK(fin->climbTreasures == 0);
    CHECK(fin->spotContent != crafted.spotContent);
    for (int level = 0; level < kLevelCount; ++level) CHECK(treasuresOnLevel(*fin, level) == r.tables->gemsNeeded[1]);
    CHECK_FALSE(r.animSfx.empty());  // the celebration's DAM scripts play their FC effects
    if (!r.has("S13 climb end")) r.dumpTrace();
}


TEST_CASE("game main: a caught carrier elf asks its question (Room_Run -> Quiz_AskQuestion), a right answer pays") {
    TM_REQUIRE_GAME_DATA();
    // Net swings in room 11, where the level-0 elves start: with the elves' random walk some seed
    // of a few brings a question carrier into the net (any one is enough).
    bool asked = false;
    for (uint16_t seed = 1; seed <= 24 && !asked; ++seed) {
        MainRig r(*g, tempDir("elf_question"));
        GameState x = GameState::initial(*r.tables);
        x.player.setName("Elfy");
        initNewPlayer(x);
        mischief::BorlandRand rng;
        rng.srand(7);
        setupClimb(x, *r.tables, rng);
        x.player.room = 11;
        x.hints.unseen = 0;
        {
            save::UsersTable t;
            toPlayerRecord(x).writeTo(t.slots[0]);
            t.recount();
            REQUIRE(save::writeUsersFile(r.paths.usersDat, t));
        }
        int slot = -1;
        uint16_t coinsBefore = 0;
        r.onTrace = [&](std::string_view l) {
            if (!l.starts_with("S9 elf question: slot ") || slot >= 0) return;
            slot = std::stoi(std::string(l.substr(22)));
            coinsBefore = r.state->inv.coins;
            // The answer the file lists first (the right one) is at display position k; the focus
            // starts on position 0 and Down moves it. After the screen's flush: Down x k, Enter;
            // then Enter on the praise screen's CONTINUE.
            const auto& perm = r.state->quiz.slots[static_cast<size_t>(slot)].perm;
            const int k = static_cast<int>(std::find(perm.begin(), perm.end(), uint8_t{0}) - perm.begin());
            for (int i = 0; i < k; ++i) r.postIn(400 + 50 * static_cast<uint64_t>(i), 0x100 + 0x28);
            r.postIn(600, 0x0D);
            r.postIn(3000, 0x0D);
        };
        r.run(kToSignIn + ",down,enter,wait:1500," + repeat("space,wait:1200", 10) + ",wait:3000," + kExitYes, seed);
        if (slot < 0) continue;
        asked = true;
        CHECK(r.inOrder({"S9 elf question: slot", "scene: question", "scene: clue box mode 1", "scene: answer position",
                         "scene: clue box mode 0", "save: exit (player Elfy, room 11"}));
        const size_t a = r.find("scene: answer position");
        REQUIRE(a != std::string::npos);
        CHECK(r.trace[a].ends_with("correct"));
        CHECK_FALSE(r.trace[a].ends_with("wrong"));
        const auto rec = r.record("Elfy");
        REQUIRE(rec);
        CHECK(rec->clueWords == 1);                       // the clue word earned
        CHECK(rec->coins == coinsBefore + 2);             // +2 for a right first try
        CHECK(rec->quizSlots[static_cast<size_t>(slot)].id == 0xFFFF);  // the slot is cleared
    }
    CHECK(asked);
}
