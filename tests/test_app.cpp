// App area: engine pump hooks and modal hold, the audio/dialog/menu glue, the menu command
// handlers (1140/1180), the restart point, the game command line, scripted input and the
// bootstrap (error paths without a window, and one scripted headless run of the whole game:
// app::runGame with its default entry, game::gameMain).
// Headless only: no window, no audible audio (fake / null devices).
#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "app/app_context.h"
#include "app/bootstrap.h"
#include "app/engine_glue.h"
#include "app/game_main.h"
#include "app/launch_options.h"
#include "app/menu_commands.h"
#include "app/script_input.h"
#include "app_test_util.h"
#include "audio_test_util.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "platform/engine.h"
#include "save/user_record.h"
#include "save/users_file.h"
#include "test_data.h"
#include "text/font.h"
#include "text/text_renderer.h"
#include "timing/clock.h"
#include "ui/game_dialog.h"
#include "ui/menu_bar.h"
#include "util/file_util.h"

using namespace tmport;
using namespace apptest;

namespace {

std::filesystem::path scratchDir(const char* name) { return outDir(name); }

// A preference directory whose tmport.ini says the original users.dat was already imported, so
// the bootstrap never copies anything from C:\TLCWIN.
std::filesystem::path prefDirFor(const char* name) {
    auto d = scratchDir(name);
    std::ofstream(d / "tmport.ini") << "[saves]\nimported_original = true\n";
    return d;
}

// Everything a menu command needs, on a headless engine with a manual clock and fake audio.
struct AppRig {
    ManualClock clock;
    audiotest::Rig audioRig;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<text::Font> font;
    std::unique_ptr<text::TextRenderer> text;
    std::unique_ptr<ui::DialogSystem> dialogs;
    std::unique_ptr<ui::MenuBar> menu;
    std::unique_ptr<game::GameTables> tables;
    std::unique_ptr<game::GameState> state;
    std::unique_ptr<AppContext> ctx;
    std::unique_ptr<app::EngineGlue> glue;
    std::deque<uint16_t> answers;  // scripted dialog input

    explicit AppRig(GameFiles& g) {
        EngineConfig cfg;
        cfg.clock = &clock;
        engine = std::make_unique<Engine>(g, gfx::ColorMode::Colour256, cfg);
        font = std::make_unique<text::Font>(text::Font::load(*g.res));
        text = std::make_unique<text::TextRenderer>(*font, &engine->frame(), &engine->presenter());
        dialogs = std::make_unique<ui::DialogSystem>(*engine, *font);
        dialogs->hooks().getInputCode = [this] {
            if (answers.empty()) return static_cast<uint16_t>(0x1B);
            const uint16_t c = answers.front();
            answers.pop_front();
            return c;
        };
        dialogs->hooks().delayMs = [](uint32_t) {};
        menu = std::make_unique<ui::MenuBar>(*engine, ui::MenuBarModel::fromExe(*g.exe), *font);
        tables = std::make_unique<game::GameTables>(game::GameTables::load(*g.exe, *g.res));
        state = std::make_unique<game::GameState>(game::GameState::initial(*tables));
        ctx = std::make_unique<AppContext>(AppContext{*engine, g, *font, *text, *dialogs});
        ctx->audio = audioRig.audio.get();
        ctx->menu = menu.get();
        ctx->state = state.get();
        app::registerMenuCommands(*ctx);
        glue = std::make_unique<app::EngineGlue>(*ctx);
    }
    ~AppRig() { glue.reset(); }
};

}  // namespace

// ------------------------------------------------------------------------------- pump hooks

TEST_CASE("engine: pump hooks see Busy / Fetch phases and can be removed") {
    TM_REQUIRE_GAME_DATA();
    ManualClock clock;
    EngineConfig cfg;
    cfg.clock = &clock;
    Engine e(*g, gfx::ColorMode::Colour256, cfg);
    std::vector<PumpPhase> seen;
    const int id = e.addPumpHook([&](PumpPhase p) { seen.push_back(p); });
    e.pump();
    e.getInputCode();
    e.peekInputCode();
    e.flushInput();
    REQUIRE(seen.size() == 4);
    CHECK(seen[0] == PumpPhase::Busy);
    CHECK(seen[1] == PumpPhase::Fetch);
    CHECK(seen[2] == PumpPhase::Fetch);
    CHECK(seen[3] == PumpPhase::Fetch);
    seen.clear();
    e.timerSet(1, 5);
    e.timerWait(1);
    CHECK(!seen.empty());
    CHECK(std::all_of(seen.begin(), seen.end(), [](PumpPhase p) { return p == PumpPhase::Busy; }));
    e.removePumpHook(id);
    seen.clear();
    e.pump();
    CHECK(seen.empty());

    // A hook that removes itself and adds another one: the new one runs from the next pump on.
    int a = 0, b = 0;
    int idB = 0;
    int idA = 0;
    idA = e.addPumpHook([&](PumpPhase) {
        ++a;
        e.removePumpHook(idA);
        idB = e.addPumpHook([&](PumpPhase) { ++b; });
    });
    e.pump();
    CHECK(a == 1);
    CHECK(b == 0);
    e.pump();
    CHECK(a == 1);
    CHECK(b == 1);
    e.removePumpHook(idB);

    // Exceptions from a hook leave pump() (the menu's QuitGame / RestartToSignIn path).
    const int idT = e.addPumpHook([](PumpPhase p) {
        if (p == PumpPhase::Fetch) throw QuitGame{};
    });
    e.pump();
    CHECK_THROWS_AS(e.getInputCode(), QuitGame);
    e.removePumpHook(idT);
    CHECK_NOTHROW(e.getInputCode());
}

TEST_CASE("engine: holdWhile deactivates, handles events and reactivates") {
    TM_REQUIRE_GAME_DATA();
    ManualClock clock;
    EngineConfig cfg;
    cfg.clock = &clock;
    Engine e(*g, gfx::ColorMode::Colour256, cfg);
    std::vector<int> act;
    e.setActivationHook([&](bool on) { act.push_back(on); });
    int keys = 0;
    e.setEventFilter([&](const SDL_Event& ev) {
        if (ev.type == SDL_EVENT_KEY_DOWN) ++keys;
        return true;
    });
    bool hold = true;
    e.holdWhile([&] { return false; });  // not active: nothing happens
    CHECK(act.empty());
    press(e, SDLK_A);
    press(e, SDLK_B);
    int nested = 0;
    e.holdWhile([&] {
        e.holdWhile([&] { return ++nested < 0; });  // re-entrant: returns at once
        return hold && keys < 2;
    });
    CHECK(keys == 2);
    REQUIRE(act.size() == 2);
    CHECK(act[0] == 0);
    CHECK(act[1] == 1);
    CHECK_FALSE(e.holding());
    // Headless with nothing queued: returns although still active.
    act.clear();
    e.holdWhile([&] { return hold; });
    CHECK(act.size() == 2);
    e.setEventFilter({});
}

// ------------------------------------------------------------------------------------ glue

TEST_CASE("glue: audio service per pump, activation, Ctrl+S, dialog and menu hooks") {
    TM_REQUIRE_GAME_DATA();
    AppRig r(*g);
    Engine& e = *r.engine;
    audio::AudioSystem& a = *r.audioRig.audio;

    // audio.service() on every pump.
    const long s0 = r.glue->serviceCalls();
    e.pump();
    e.getInputCode();
    e.delayMs(3);
    CHECK(r.glue->serviceCalls() >= s0 + 3);

    // Ctrl+S inside the fetch: Audio_ToggleAll; the 0x13 code still reaches the game (speech waits
    // stop on it); the player's option bytes follow.
    REQUIRE(a.soundOn());
    REQUIRE(a.voiceOn());
    press(e, SDLK_S, SDL_KMOD_LCTRL);
    CHECK(e.getInputCode() == 0x13);
    CHECK_FALSE(a.soundOn());
    CHECK_FALSE(a.voiceOn());
    CHECK(r.state->options.soundOn == 0);
    CHECK(r.state->options.voiceOn == 0);
    press(e, SDLK_S, SDL_KMOD_LCTRL);
    CHECK(e.getInputCode() == 0x13);
    CHECK(a.soundOn());
    CHECK(a.voiceOn());
    CHECK(r.state->options.soundOn == 1);

    // Activation hook (here through the menu loop's hold): App_Deactivate cuts the wave voice.
    r.audioRig.sounds->lengthsMs[7003] = 2000;
    a.sfxPlay(3);
    REQUIRE(a.sfxIsPlaying());
    const size_t log0 = r.audioRig.wave->log.size();
    r.menu->install();
    press(e, SDLK_F, SDL_KMOD_LALT);
    e.getInputCode();
    CHECK(r.menu->tracking());
    CHECK(r.audioRig.wave->log.size() > log0);
    CHECK(r.audioRig.wave->log.back().what == 'X');
    CHECK(a.appActive());  // reactivated when the (headless) hold returned
    press(e, SDLK_ESCAPE);
    press(e, SDLK_ESCAPE);
    e.getInputCode();
    CHECK_FALSE(r.menu->tracking());

    // Dialog hooks: Sound_PauseAll on a message box (the voice is cut), menus greyed meanwhile.
    a.sfxPlay(3);
    REQUIRE(a.sfxIsPlaying());
    bool menuOffInside = false;
    r.dialogs->hooks().getInputCode = [&] {
        menuOffInside = !r.menu->isEnabled();
        return static_cast<uint16_t>(0x0D);
    };
    r.dialogs->runModal(ui::DlgDesc::fromExe(*g->exe, 0x1919));
    CHECK(menuOffInside);
    CHECK(r.menu->isEnabled());
    CHECK_FALSE(a.sfxIsPlaying());
    CHECK(r.dialogs->musicPauseDepth() == 0);
}

// -------------------------------------------------------------------------------- commands

TEST_CASE("menu commands: Options handlers (1140:0015 / 00cc / 00f7)") {
    TM_REQUIRE_GAME_DATA();
    AppRig r(*g);
    audio::AudioSystem& a = *r.audioRig.audio;
    AppContext& ctx = *r.ctx;

    r.answers = {'n'};
    app::menuCmdVoice(ctx);
    CHECK_FALSE(a.voiceOn());
    CHECK(r.state->options.voiceOn == 0);
    r.answers = {0x1B};  // Esc: unchanged
    app::menuCmdVoice(ctx);
    CHECK_FALSE(a.voiceOn());
    r.answers = {'y'};
    app::menuCmdVoice(ctx);
    CHECK(a.voiceOn());

    r.answers = {'n'};
    app::menuCmdMusicAndSound(ctx);
    CHECK_FALSE(a.soundOn());
    CHECK(r.state->options.soundOn == 0);
    r.answers = {'n'};  // same answer: no toggle
    app::menuCmdMusicAndSound(ctx);
    CHECK_FALSE(a.soundOn());
    r.answers = {0x0D};  // Enter = the preset (current value: No)
    app::menuCmdMusicAndSound(ctx);
    CHECK_FALSE(a.soundOn());
    r.answers = {'y'};
    app::menuCmdMusicAndSound(ctx);
    CHECK(a.soundOn());

    r.state->hints.pending = 5;
    r.state->hints.unseen = 0x1234;
    r.answers = {'n'};
    app::menuCmdNewPlayerMessages(ctx);
    CHECK(r.state->options.hintsOn == 0);
    CHECK(r.state->hints.pending == -1);
    CHECK(r.state->hints.unseen == 0);
    r.answers = {'y'};
    app::menuCmdNewPlayerMessages(ctx);
    CHECK(r.state->options.hintsOn == 1);
    CHECK(r.state->hints.unseen == 0xFFFFFFFFu);
}

TEST_CASE("menu commands: Exit / Start Another Game leave with the exceptions") {
    TM_REQUIRE_GAME_DATA();
    AppRig r(*g);
    AppContext& ctx = *r.ctx;
    int saves = 0;
    ctx.services.saveCurrentPlayer = [&] { ++saves; };

    r.answers = {'n'};
    CHECK_NOTHROW(app::menuCmdExit(ctx));
    CHECK(saves == 0);
    r.answers = {'y'};
    CHECK_THROWS_AS(app::menuCmdExit(ctx), QuitGame);
    CHECK(saves == 1);

    r.state->hints.pending = 3;
    r.answers = {0x1B};
    CHECK_NOTHROW(app::menuCmdStartAnotherGame(ctx));
    CHECK(r.state->hints.pending == 3);
    r.answers = {'y'};
    CHECK_THROWS_AS(app::menuCmdStartAnotherGame(ctx), RestartToSignIn);
    CHECK(r.state->hints.pending == -1);

    // Through the menu bar, from inside an input fetch: File > Exit, Yes.
    Engine& e = *r.engine;
    r.menu->install();
    r.answers = {'y'};
    press(e, SDLK_F, SDL_KMOD_LALT);
    press(e, SDLK_UP);      // the last item: Exit
    press(e, SDLK_RETURN);
    CHECK_THROWS_AS(e.getInputCode(), QuitGame);
    CHECK(saves == 2);
    CHECK(r.menu->isEnabled());
    CHECK_FALSE(r.menu->inHandler());

    // Help > About, and a help topic (the pager is cancelled by the scripted Esc).
    press(e, SDLK_H, SDL_KMOD_LALT);
    press(e, SDLK_UP);      // About Treasure Mountain!
    press(e, SDLK_RETURN);
    r.answers = {0x0D};
    CHECK_NOTHROW(e.getInputCode());
    CHECK(r.answers.empty());
    CHECK(r.menu->stats().delivered == 2);
    r.answers = {0x1B};
    CHECK_NOTHROW(app::menuCmdHelp(ctx, 1));

    // See Rank: the game area's service.
    int ranks = 0;
    ctx.services.seeRank = [&] { ++ranks; };
    CHECK(r.menu->dispatch(0x0000));
    CHECK(ranks == 1);
}

TEST_CASE("restart point and game entry policy") {
    std::vector<int> calls;
    app::runRestartPoint([&](int r) {
        calls.push_back(r);
        if (calls.size() < 3) throw RestartToSignIn{};
        throw QuitGame{};
    });
    CHECK(calls == std::vector<int>{0, 1, 1});
    calls.clear();
    app::runRestartPoint([&](int r) { calls.push_back(r); });
    CHECK(calls == std::vector<int>{0});
    CHECK_THROWS_AS(app::runRestartPoint([](int) { throw std::runtime_error("x"); }), std::runtime_error);
}

// ------------------------------------------------------------------------- command line

TEST_CASE("launch options: game vs tool invocation, parsing") {
    auto inv = [](std::vector<const char*> a) {
        a.insert(a.begin(), "tmport");
        return app::isGameInvocation(static_cast<int>(a.size()), const_cast<char**>(a.data()));
    };
    CHECK(inv({}));
    CHECK(inv({"--play"}));
    CHECK(inv({"--data", "X:/TMTWINCD", "--no-pause", "--midi", "null"}));
    CHECK_FALSE(inv({"--info"}));
    CHECK_FALSE(inv({"--data", "X", "--info"}));
    CHECK_FALSE(inv({"--render-dialog", "exit", "o.png"}));
    CHECK_FALSE(inv({"--help"}));
    CHECK_FALSE(inv({"--dat", "X"}));

    std::string err;
    std::vector<const char*> a = {"tmport", "--headless", "--keys", "enter,esc", "--midi-set", "B", "--16"};
    auto o = app::parseLaunchArgs(static_cast<int>(a.size()), const_cast<char**>(a.data()), err);
    REQUIRE(o);
    CHECK(o->headless);
    CHECK(o->keys == "enter,esc");
    CHECK(o->midiSet == "B");
    CHECK(o->colour16);
    std::vector<const char*> bad = {"tmport", "--midi", "fm"};
    CHECK_FALSE(app::parseLaunchArgs(static_cast<int>(bad.size()), const_cast<char**>(bad.data()), err));
    CHECK(err.find("--midi") != std::string::npos);
    std::vector<const char*> missing = {"tmport", "--data"};
    CHECK_FALSE(app::parseLaunchArgs(static_cast<int>(missing.size()), const_cast<char**>(missing.data()), err));
}

TEST_CASE("launch options: editions and the DOS edition's options") {
    auto parse = [](std::vector<const char*> a, std::string& err) {
        a.insert(a.begin(), "tmport");
        return app::parseLaunchArgs(static_cast<int>(a.size()), const_cast<char**>(a.data()), err);
    };
    auto inv = [](std::vector<const char*> a) {
        a.insert(a.begin(), "tmport");
        return app::isGameInvocation(static_cast<int>(a.size()), const_cast<char**>(a.data()));
    };
    CHECK(inv({"--edition", "dos", "--headless", "--frame-dump", "x.png"}));
    CHECK(inv({"--look", "tandy", "--sound", "adlib", "--slow-cpu", "--frame-speed", "period", "--dos-args", "-T -St",
               "--square-pixels"}));
    CHECK_FALSE(inv({"--edition", "dos", "--info"}));

    std::string err;
    auto o = parse({"--edition", "dos", "--look", "mcga", "--sound", "speaker", "--slow-cpu", "--frame-speed", "period",
                    "--dos-args", "-T -St", "--square-pixels"},
                   err);
    REQUIRE(o);
    CHECK(o->edition == "dos");
    CHECK(o->dosLook == "mcga");
    CHECK(o->dosSound == "speaker");
    CHECK(o->dosSlowCpu);
    CHECK(o->frameSpeed == "period");
    CHECK(o->dosArgs == "-T -St");
    CHECK(o->squarePixels);
    o = parse({"--edition", "auto"}, err);
    REQUIRE(o);
    CHECK(o->edition == "auto");
    o = parse({}, err);
    REQUIRE(o);
    CHECK(o->edition.empty());
    CHECK_FALSE(o->dosSlowCpu);
    CHECK_FALSE(o->squarePixels);

    CHECK_FALSE(parse({"--edition", "mac"}, err));
    CHECK(err.find("--edition") != std::string::npos);
    CHECK_FALSE(parse({"--look", "svga"}, err));
    CHECK(err.find("--look") != std::string::npos);
    CHECK_FALSE(parse({"--sound", "gus"}, err));
    CHECK(err.find("--sound") != std::string::npos);
    CHECK_FALSE(parse({"--frame-speed", "fast"}, err));
    CHECK(err.find("--frame-speed") != std::string::npos);
    CHECK_FALSE(parse({"--dos-args"}, err));
    CHECK(app::launchUsage().find("--edition") != std::string::npos);
    CHECK(app::launchUsage().find("--dos-args") != std::string::npos);
}

TEST_CASE("script input: parsing") {
    std::string err;
    auto s = app::parseScript("enter, alt+o ,wait:250,shot:C:/x/y.png,click:100:7,ctrl+s,q,shift+a,quit", err);
    REQUIRE(s);
    REQUIRE(s->size() == 9);
    using K = app::ScriptStep::Kind;
    CHECK((*s)[0].kind == K::Key);
    CHECK((*s)[0].keycode == SDLK_RETURN);
    CHECK((*s)[0].text.empty());
    CHECK(app::parseScript("space", err)->at(0).text == " ");
    CHECK((*s)[1].mod == SDL_KMOD_LALT);
    CHECK((*s)[1].keycode == SDLK_O);
    CHECK((*s)[1].text.empty());
    CHECK((*s)[2].kind == K::Wait);
    CHECK((*s)[2].ms == 250);
    CHECK((*s)[3].kind == K::Shot);
    CHECK(pathToUtf8((*s)[3].path) == "C:/x/y.png");
    CHECK((*s)[4].kind == K::Click);
    CHECK((*s)[4].x == 100);
    CHECK((*s)[4].y == 7);
    CHECK((*s)[5].mod == SDL_KMOD_LCTRL);
    CHECK((*s)[6].text == "q");
    CHECK((*s)[7].text == "A");
    CHECK((*s)[8].kind == K::Quit);
    CHECK_FALSE(app::parseScript("enter,bogus", err));
    CHECK(err.find("bogus") != std::string::npos);
    CHECK_FALSE(app::parseScript("wait:x", err));
    CHECK(app::parseScript("", err)->empty());
}

// ------------------------------------------------------------------------------ bootstrap

TEST_CASE("bootstrap: missing data is reported, naming the file, without a window") {
    const bool videoBefore = SDL_WasInit(SDL_INIT_VIDEO) != 0;
    std::vector<app::StartupError> errors;
    auto sink = [&](const app::StartupError& e) { errors.push_back(e); };

    app::LaunchOptions o;
    o.headless = true;
    o.prefDir = pathToUtf8(prefDirFor("boot_pref"));

    SUBCASE("folder without the game") {
        o.dataPath = pathToUtf8(scratchDir("boot_empty"));
        CHECK(app::runGame(o, {}, sink) == 3);
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].file() == "TMTWINCD.EXE");
        CHECK(std::string(errors[0].what()).find("TMTWINCD.EXE") != std::string::npos);
        CHECK(std::string(errors[0].what()).find("--data") != std::string::npos);
    }
    SUBCASE("path that does not exist") {
        o.dataPath = pathToUtf8(scratchDir("boot_missing") / "no_such_folder");
        CHECK(app::runGame(o, {}, sink) == 3);
        REQUIRE(errors.size() == 1);
        CHECK(std::string(errors[0].what()).find("does not exist") != std::string::npos);
    }
    SUBCASE("EXE present, DLLs missing") {
        auto* gf = testdata::game();
        auto dp = testdata::dataPath();
        if (!gf || !dp || !std::filesystem::is_directory(pathFromUtf8(*dp))) {
            MESSAGE("TM_DATA (a folder) is not set: subcase skipped");
            return;
        }
        auto dir = scratchDir("boot_exe_only");
        std::error_code ec;
        std::filesystem::copy_file(pathFromUtf8(*dp) / "TMTWINCD.EXE", dir / "TMTWINCD.EXE", ec);
        REQUIRE(!ec);
        o.dataPath = pathToUtf8(dir);
        CHECK(app::runGame(o, {}, sink) == 3);
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].file().ends_with(".DLL"));
        CHECK(std::string(errors[0].what()).find(errors[0].file()) != std::string::npos);
        std::filesystem::remove_all(dir, ec);
    }
    if (!videoBefore) CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);  // no window was ever opened
}

TEST_CASE("bootstrap: data path precedence") {
    app::LaunchOptions o;
    o.dataPath = "A:/from-command-line";
    CHECK(app::resolveDataPath(o, "B:/from-ini") == "A:/from-command-line");
    o.dataPath.clear();
    if (auto env = testdata::dataPath()) {
        CHECK(app::resolveDataPath(o, "B:/from-ini") == *env);  // TM_DATA beats tmport.ini
    }
}

TEST_CASE("bootstrap: scripted headless run of the game with the menu open") {
    TM_REQUIRE_GAME_DATA();
    const bool videoBefore = SDL_WasInit(SDL_INIT_VIDEO) != 0;
    const bool audioBefore = SDL_WasInit(SDL_INIT_AUDIO) != 0;
    auto dir = scratchDir("boot_run");
    const auto shot = dir / "game_menu_options.png";
    const auto shot2 = dir / "game_after_menu.png";
    const auto dump = dir / "game_end.png";
    std::error_code ec;
    for (const auto& f : {shot, shot2, dump}) std::filesystem::remove(f, ec);  // no stale files
    const auto pref = prefDirFor("boot_run_pref");
    std::filesystem::remove(pref / "users.dat", ec);  // an empty roster (TM_APP_SHOTS keeps the folder)
    app::LaunchOptions o;
    o.headless = true;
    o.dataPath = *testdata::dataPath();
    o.prefDir = pathToUtf8(pref);
    // The settings of this run only: a tmport.ini next to the test executable would otherwise win
    // over the preference directory's (save::Paths::under) and could change the flow.
    o.configPath = pathToUtf8(pref / "tmport.ini");
    // Title: Space ends the pages, Space ends the closing animation; Club Sign-In (empty roster):
    // typing starts a new player "Test"; climb set-up in room 9, where the first two New Player
    // Messages (hints 0 and 2) wait for Enter while the menu bar is greyed; then open Options,
    // select Voice, shot, close (Esc closes the drop-down, Esc leaves the bar); Help > About via
    // the keyboard, close it, shot. The end of the script ends the headless run (QuitGame), and
    // GameMain's shutdown saves the player. The waits are generous so that a loaded machine
    // still reaches each screen before its keys.
    o.keys = "space,wait:800,space,wait:2000,t,e,s,t,enter,wait:1500,enter,wait:800,enter,wait:800,alt+o,down,down,shot:" +
             pathToUtf8(shot) + ",esc,esc,alt+h,up,enter,wait:300,enter,wait:300,shot:" + pathToUtf8(shot2);
    o.frameDump = pathToUtf8(dump);
    std::vector<std::string> errors;
    apptest::LogCapture logs;
    const int rc = app::runGame(o, {}, [&](const app::StartupError& e) { errors.push_back(e.what()); });
    CHECK(rc == 0);
    CHECK(errors.empty());
    for (const auto& e : errors) MESSAGE(e);
    // Process-local evidence first (another test process sharing TM_APP_SHOTS could remove files).
    const bool wrote1 = logs.has("script: canvas -> " + pathToUtf8(shot));
    const bool wrote2 = logs.has("script: canvas -> " + pathToUtf8(shot2));
    CHECK(wrote1);
    CHECK(wrote2);
    CHECK(logs.has("script: finished"));      // every step was used; the run did not end early
    CHECK(std::filesystem::exists(shot));
    CHECK(std::filesystem::exists(shot2));
    CHECK(std::filesystem::exists(dump));
    if (!wrote1 || !wrote2 || !std::filesystem::exists(shot2))
        for (const std::string& l : logs.lines()) MESSAGE("log: " << l);
    // The real game ran: the sign-in created "Test", the climb set-up put the player in room 9
    // (a player who never reached a climb keeps location 0xFE) and the shutdown saved it.
    save::UsersTable users;
    REQUIRE(save::readUsersFile(pref / "users.dat", users));
    const int slot = users.findByName("Test");
    REQUIRE(slot >= 0);
    REQUIRE(slot < users.count);
    const save::PlayerRecord rec = save::PlayerRecord::fromSlot(users.slots[static_cast<size_t>(slot)]);
    CHECK(rec.location == 9);
    CHECK(rec.coins == 10);
    if (!videoBefore) CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    if (!audioBefore) CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);
}
