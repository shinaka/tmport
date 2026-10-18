#include "game/screens_cli.h"

#include <SDL3/SDL_filesystem.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

#include "audio/audio_system.h"
#include "audio/engine_input_port.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/rules.h"
#include "game/signin.h"
#include "game/title.h"
#include "platform/engine.h"
#include "res/game_files.h"
#include "save/settings.h"
#include "save/users_file.h"
#include "text/font.h"
#include "text/text_renderer.h"
#include "ui/game_dialog.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/log.h"
#include "vfs/data_source.h"

namespace tmport::game {

namespace {

template <class... Args>
void out(std::format_string<Args...> fmt, Args&&... args) {
    const std::string s = std::format(fmt, std::forward<Args>(args)...);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

struct Args {
    std::string screen;  // title | signin | attract
    std::string keys;
    std::string outDir = ".";
    std::string users;
    std::string player;
    std::string dataPath;
    bool restart = false;
    int voice = 1, sound = 1;
    bool noAudio = false;
    bool colour16 = false;
    bool trace = false;
    bool verbose = false;
    uint64_t maxMs = 900000;
};

// ------------------------------------------------------------------------- scripted input

struct ScriptEnd {
    bool timeout = false;
};

struct Action {
    enum Kind { Msg, Shot, End } kind = Msg;
    uint64_t at = 0;  // ms after the start of the run
    WinMsg msg;
    std::string name;
};

std::optional<std::pair<int, int>> parseXY(const std::string& s) {
    const size_t c = s.find(',');
    if (c == std::string::npos) return std::nullopt;
    try {
        return std::make_pair(std::stoi(s.substr(0, c)), std::stoi(s.substr(c + 1)));
    } catch (...) {
        return std::nullopt;
    }
}

bool parseScript(const std::string& text, std::vector<Action>& actions, std::string& error) {
    std::istringstream in(text);
    std::string tok;
    uint64_t t = 0;
    auto msg = [&](WinMsg m) { actions.push_back(Action{Action::Msg, t, m, {}}); };
    auto keyTap = [&](uint16_t vk) {
        msg(WinMsg::keyDown(vk));
        msg(WinMsg::keyUp(vk));
    };
    while (in >> tok) {
        const size_t colon = tok.find(':');
        const std::string head = colon == std::string::npos ? tok : tok.substr(0, colon);
        const std::string arg = colon == std::string::npos ? std::string() : tok.substr(colon + 1);
        try {
            if (head == "wait") t += std::stoull(arg);
            else if (head == "shot") actions.push_back(Action{Action::Shot, t, {}, arg});
            else if (head == "end") actions.push_back(Action{Action::End, t, {}, {}});
            else if (head == "enter") msg(WinMsg::chr(0x0D));
            else if (head == "esc") msg(WinMsg::chr(0x1B));
            else if (head == "space") msg(WinMsg::chr(0x20));
            else if (head == "tab") msg(WinMsg::chr(0x09));
            else if (head == "bs") msg(WinMsg::chr(0x08));
            else if (head == "ctrl+r") msg(WinMsg::chr(0x12));
            else if (head == "ctrl+s") msg(WinMsg::chr(0x13));
            else if (head == "up") keyTap(0x26);
            else if (head == "down") keyTap(0x28);
            else if (head == "left") keyTap(0x25);
            else if (head == "right") keyTap(0x27);
            else if (head == "pgup") keyTap(0x21);
            else if (head == "pgdn") keyTap(0x22);
            else if (head == "home") keyTap(0x24);
            else if (head == "end-key") keyTap(0x23);
            else if (head == "char") {
                if (arg.size() > 2 && arg.rfind("0x", 0) == 0)
                    msg(WinMsg::chr(static_cast<uint16_t>(std::stoul(arg, nullptr, 16))));
                else if (arg.size() == 1)
                    msg(WinMsg::chr(static_cast<uint8_t>(arg[0])));
                else
                    throw std::invalid_argument("char");
            } else if (head == "text") {
                for (size_t i = 0; i < arg.size(); ++i) {
                    if (i) t += 60;
                    msg(WinMsg::chr(static_cast<uint8_t>(arg[i] == '~' ? ' ' : arg[i])));
                }
            } else if (head == "code") {
                msg(WinMsg::postKey(static_cast<uint16_t>(std::stoul(arg, nullptr, 0))));
            } else if (head == "click" || head == "rclick" || head == "press" || head == "release" || head == "move") {
                const auto xy = parseXY(arg);
                if (!xy) throw std::invalid_argument("xy");
                const auto [x, y] = *xy;
                msg(WinMsg::mouse(WinMsg::Type::MouseMove, x, y));
                if (head == "click" || head == "press") msg(WinMsg::mouse(WinMsg::Type::LButtonDown, x, y));
                if (head == "click" || head == "release") msg(WinMsg::mouse(WinMsg::Type::LButtonUp, x, y));
                if (head == "rclick") {
                    msg(WinMsg::mouse(WinMsg::Type::RButtonDown, x, y));
                    msg(WinMsg::mouse(WinMsg::Type::RButtonUp, x, y));
                }
            } else {
                error = std::format("unknown script token '{}'", tok);
                return false;
            }
        } catch (const std::exception&) {
            error = std::format("bad script token '{}'", tok);
            return false;
        }
    }
    return true;
}

// Plays the script on the manual clock: called from every ManualClock::sleepMs.
class ScriptDriver {
public:
    ScriptDriver(Engine& engine, ManualClock& clock, std::vector<Action> actions, std::filesystem::path outDir,
                 uint64_t maxMs)
        : engine_(engine), clock_(clock), actions_(std::move(actions)), outDir_(std::move(outDir)), maxMs_(maxMs),
          start_(clock.nowMs()) {}

    uint64_t elapsed() const { return clock_.nowMs() - start_; }
    bool done() const { return next_ >= actions_.size(); }

    void poll() {
        const uint64_t t = elapsed();
        while (next_ < actions_.size() && actions_[next_].at <= t) {
            const Action& a = actions_[next_++];
            switch (a.kind) {
                case Action::Msg: engine_.input().post(a.msg); break;
                case Action::Shot: {
                    const auto path = outDir_ / (a.name + ".png");
                    if (engine_.presenter().writeGameAreaPng(path))
                        out("[{:>7} ms] shot {}\n", t, pathToUtf8(path));
                    else
                        out("[{:>7} ms] shot {} FAILED\n", t, pathToUtf8(path));
                    break;
                }
                case Action::End: throw ScriptEnd{false};
            }
        }
        if (t > maxMs_) throw ScriptEnd{true};
    }

private:
    Engine& engine_;
    ManualClock& clock_;
    std::vector<Action> actions_;
    size_t next_ = 0;
    std::filesystem::path outDir_;
    uint64_t maxMs_;
    uint64_t start_;
};

// ------------------------------------------------------------------------- silent audio

// The wave voice timed on the manual clock: a sound "plays" for its length; nothing is heard.
class ClockWaveOutput final : public audio::WaveOutput {
public:
    explicit ClockWaveOutput(Clock& clock) : clock_(clock) {}
    bool available() const override { return true; }
    void start(const audio::Sound& s) override { end_ = clock_.nowMs() + s.durationUs() / 1000; }
    void stop() override { end_ = 0; }
    bool done() override { return clock_.nowMs() >= end_; }

private:
    Clock& clock_;
    uint64_t end_ = 0;
};

std::unique_ptr<audio::AudioSystem> makeSilentAudio(GameFiles& files, ManualClock& clock) {
    audio::AudioParts parts;
    parts.wave = std::make_unique<ClockWaveOutput>(clock);
    parts.sounds = std::make_unique<audio::ResourceSoundLoader>(*files.res);
    parts.midi = std::make_unique<audio::MidiPlayer>(std::make_unique<audio::NullMidiBackend>(),
                                                     audio::MicroClock([&clock] { return clock.nowMs() * 1000; }), false);
    parts.tracks = std::make_unique<audio::DataSourceTrackLoader>(*files.source);
    return std::make_unique<audio::AudioSystem>(audio::AudioTables::fromExe(*files.exe), std::move(parts));
}

// ------------------------------------------------------------------------- argument parsing

std::optional<Args> parseArgs(int argc, char** argv, std::string& error) {
    Args a;
    bool found = false;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                error = std::format("{} needs a value", what);
                return {};
            }
            return argv[++i];
        };
        if (s == "--screen") {
            a.screen = next("--screen");
            found = true;
        } else if (s == "--keys") {
            a.keys += " " + next("--keys");
        } else if (s == "--keys-file") {
            const std::string f = next("--keys-file");
            std::ifstream in(pathFromUtf8(f));
            if (!in) error = std::format("cannot read {}", f);
            std::stringstream ss;
            ss << in.rdbuf();
            a.keys += " " + ss.str();
        } else if (s == "--out") {
            a.outDir = next("--out");
        } else if (s == "--users") {
            a.users = next("--users");
        } else if (s == "--player") {
            a.player = next("--player");
        } else if (s == "--restart") {
            a.restart = true;
        } else if (s == "--voice") {
            a.voice = std::atoi(next("--voice").c_str());
        } else if (s == "--sound") {
            a.sound = std::atoi(next("--sound").c_str());
        } else if (s == "--no-audio") {
            a.noAudio = true;
        } else if (s == "--max-ms") {
            a.maxMs = std::strtoull(next("--max-ms").c_str(), nullptr, 10);
        } else if (s == "--trace") {
            a.trace = true;
        } else if (s == "--data") {
            a.dataPath = next("--data");
        } else if (s == "--16") {
            a.colour16 = true;
        } else if (s == "-v" || s == "--verbose") {
            a.verbose = true;
        } else {
            error = std::format("unknown option {}", s);
        }
        if (!error.empty()) break;
    }
    if (!found) return std::nullopt;
    if (error.empty() && a.screen != "title" && a.screen != "signin" && a.screen != "attract")
        error = std::format("unknown screen '{}' (title, signin, attract)", a.screen);
    return a;
}

std::optional<std::filesystem::path> resolveDataPath(const Args& a) {
    if (!a.dataPath.empty()) return pathFromUtf8(a.dataPath);
    if (auto env = getEnv("TM_DATA")) return pathFromUtf8(*env);
    const char* base = SDL_GetBasePath();
    return vfs::autoDetectDataPath(base ? pathFromUtf8(base) : std::filesystem::path());
}

int runScreens(const Args& a, GameFiles& files, gfx::ColorMode mode) {
    std::vector<Action> actions;
    std::string error;
    if (!parseScript(a.keys, actions, error)) {
        std::fprintf(stderr, "tmport: %s\n", error.c_str());
        return 2;
    }
    const std::filesystem::path outDir = pathFromUtf8(a.outDir);
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    ManualClock clock(1000);
    EngineConfig cfg;
    cfg.clock = &clock;
    Engine engine(files, mode, cfg);  // headless: no window
    const text::Font font = text::Font::load(*files.res);
    text::TextRenderer text(font, &engine.frame(), &engine.presenter());
    ui::DialogSystem dialogs(engine, font);

    std::unique_ptr<audio::AudioSystem> audio;
    if (!a.noAudio) {
        audio = makeSilentAudio(files, clock);
        audio->settings().soundOn = a.sound ? 1 : 0;
        audio->settings().voiceOn = a.voice ? 1 : 0;
        audio::connectAudioToEngine(engine, *audio);
        dialogs.hooks().soundPauseAll = [p = audio.get()] { p->pauseAll(); };
        dialogs.hooks().soundResumeAll = [p = audio.get()] { p->resumeAll(); };
    }
    // Modal dialogs poll through the engine and let the manual clock (and the script) run.
    dialogs.hooks().getInputCode = [&engine, &clock, p = audio.get()]() -> uint16_t {
        const uint16_t c = engine.getInputCode();
        if (p) p->service();
        if (!c) clock.sleepMs(1);
        return c;
    };

    const GameTables tables = GameTables::load(*files.exe, *files.res);
    GameState state = GameState::initial(tables);
    if (audio) {
        state.options.soundOn = audio->settings().soundOn;
        state.options.voiceOn = audio->settings().voiceOn;
    }
    save::Paths paths;
    paths.usersDat = a.users.empty() ? outDir / "users.dat" : pathFromUtf8(a.users);

    AppContext ctx{engine, files, font, text, dialogs};
    ctx.audio = audio.get();
    ctx.state = &state;
    ctx.paths = &paths;

    ScriptDriver driver(engine, clock, std::move(actions), outDir, a.maxMs);
    clock.onSleep = [&driver](uint64_t) { driver.poll(); };
    auto trace = [&driver](std::string_view ev) { out("[{:>7} ms] {}\n", driver.elapsed(), ev); };

    SignInScreen signin(ctx);
    if (!a.player.empty()) {
        save::UsersTable t;
        save::readUsersFile(paths.usersDat, t);
        const int idx = t.findByName(a.player);
        if (idx < t.count) {
            fromRecord(state, t.slots[static_cast<size_t>(idx)].bytes);
            signin.setUserSel(idx);
            out("player {} loaded from slot {}\n", a.player, idx);
        } else {
            state.player.setName(a.player);
            initNewPlayer(state);
            signin.setUserSel(t.count);
            out("player {} is a new player (slot {})\n", a.player, t.count);
        }
        if (audio) {
            audio->settings().soundOn = state.options.soundOn;
            audio->settings().voiceOn = state.options.voiceOn;
        }
    }

    int rc = 0;
    try {
        bool titleContinue = true;
        if (a.screen == "title" || a.screen == "attract") {
            TitleScreen title(ctx);
            if (a.trace) title.setTrace(trace);
            const TitleResult r = title.run();
            titleContinue = r == TitleResult::Continue;
            out("[{:>7} ms] title: {}\n", driver.elapsed(), r == TitleResult::Continue ? "continue" : "quit");
            if (r != TitleResult::Continue || a.screen == "title") driver.poll();
            if (r != TitleResult::Continue) return 0;
        }
        if (a.screen == "signin" || (a.screen == "attract" && titleContinue)) {
            if (a.trace) signin.setTrace(trace);
            const SignInResult r = signin.run(a.restart && a.screen == "signin");
            out("[{:>7} ms] sign-in: {} ({} player), slot {}, room {}, sound {}, voice {}\n", driver.elapsed(), r.name,
                r.newPlayer ? "new" : "existing", r.userSel, static_cast<int>(state.player.room),
                static_cast<int>(state.options.soundOn), static_cast<int>(state.options.voiceOn));
        }
        // Let the rest of the script (shots after the screen returned) run.
        while (!driver.done()) clock.sleepMs(10);
    } catch (const ScriptEnd& e) {
        out("[{:>7} ms] {}\n", driver.elapsed(), e.timeout ? "time limit reached" : "script end");
        rc = e.timeout ? 1 : 0;
    } catch (const QuitGame&) {
        out("[{:>7} ms] quit (exit confirmed)\n", driver.elapsed());
    }
    return rc;
}

}  // namespace

std::optional<int> runScreensCommand(int argc, char** argv) {
    std::string error;
    auto a = parseArgs(argc, argv, error);
    if (!a) return std::nullopt;
    if (!error.empty()) {
        std::fprintf(stderr, "tmport: %s\n\n%s", error.c_str(), screensUsage().c_str());
        return 2;
    }
    log::setVerbose(a->verbose);
    const auto dataPath = resolveDataPath(*a);
    if (!dataPath) {
        std::fprintf(stderr, "tmport: the game data was not found. Pass --data <folder or CD image> or set TM_DATA.\n");
        return 2;
    }
    try {
        ResourceManager::Options ro;
        ro.colorMode = a->colour16 ? gfx::ColorMode::Colour16 : gfx::ColorMode::Colour256;
        ro.requireSounds = false;
        GameFiles files = GameFiles::open(*dataPath, ro);
        return runScreens(*a, files, ro.colorMode);
    } catch (const MissingFileError& e) {
        std::fprintf(stderr, "tmport: missing game file %s: %s\n", e.file().c_str(), e.what());
        return 3;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tmport: %s\n", e.what());
        return 3;
    }
}

std::string screensUsage() {
    return "Title / Club Sign-In screens (headless, scripted input on a manual clock):\n"
           "  --screen <title|signin|attract> [--keys \"<script>\"] [--keys-file <f>] [--out <dir>]\n"
           "        [--users <users.dat>] [--restart] [--player <name>] [--voice 0|1] [--sound 0|1]\n"
           "        [--no-audio] [--max-ms <n>] [--trace]\n"
           "        script: wait:MS shot:NAME enter esc space tab bs ctrl+r ctrl+s up down left right\n"
           "                char:C text:STR code:0xNNNN click:X,Y rclick:X,Y press:X,Y release:X,Y move:X,Y end\n";
}

}  // namespace tmport::game
