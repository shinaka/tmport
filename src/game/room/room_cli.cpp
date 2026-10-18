#include "game/room/room_cli.h"

#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <vector>

#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/quiz.h"
#include "game/room/room_run.h"
#include "game/rules.h"
#include "platform/engine.h"
#include "res/game_files.h"
#include "save/users_file.h"
#include "text/font.h"
#include "text/text_renderer.h"
#include "ui/game_dialog.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/log.h"
#include "vfs/data_source.h"

namespace tmport::game::room {

namespace {

template <class... A>
void out(std::format_string<A...> fmt, A&&... args) {
    const std::string s = std::format(fmt, std::forward<A>(args)...);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

struct Args {
    std::string usersFile, player;
    std::string newPlayer;
    uint16_t seed = 1;
    bool start = false;
    int startRoom = 9, startX = 0, startFrame = 0, startFacing = 0;
    int coins = -1, nets = -1, key = -1, shopCoins = -1, gems = -1;
    bool noElves = false;
    std::vector<std::array<int, 6>> elves;  // --elf set:room:x:facing:step[:item]
    std::string quiz = "esc";
    int clueBoxMode = -1;
    int clues = -1;
    int climbs = -1;
    int pileRoom = -1, pileX = 0;
    std::string keys;
    int dumpEvery = 0;
    std::string dumpDir;
    std::string outDir = ".";
    bool log = false;
    uint64_t maxTicks = 600;
    std::string dataPath;
    bool colour16 = false;
    bool verbose = false;
};

struct ScriptEnd {
    bool exitPrompt = false;
};

struct Action {
    enum Kind { Msg, Shot, End } kind = Msg;
    uint64_t at = 0;
    WinMsg msg;
    std::string name;
};

std::optional<uint16_t> vkOf(const std::string& k) {
    if (k == "up") return uint16_t{0x26};
    if (k == "down") return uint16_t{0x28};
    if (k == "left") return uint16_t{0x25};
    if (k == "right") return uint16_t{0x27};
    return std::nullopt;
}

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
    auto msgAt = [&](uint64_t at, WinMsg m) { actions.push_back(Action{Action::Msg, at, m, {}}); };
    auto msg = [&](WinMsg m) { msgAt(t, m); };
    while (in >> tok) {
        const size_t colon = tok.find(':');
        const std::string head = colon == std::string::npos ? tok : tok.substr(0, colon);
        const std::string arg = colon == std::string::npos ? std::string() : tok.substr(colon + 1);
        try {
            if (head == "wait") {
                t += std::stoull(arg);
            } else if (head == "tick") {
                t = std::stoull(arg) * 100;
            } else if (head == "shot") {
                actions.push_back(Action{Action::Shot, t, {}, arg});
            } else if (head == "end") {
                actions.push_back(Action{Action::End, t, {}, {}});
            } else if (head == "enter") {
                msg(WinMsg::chr(0x0D));
            } else if (head == "esc") {
                msg(WinMsg::chr(0x1B));
            } else if (head == "space") {
                msg(WinMsg::chr(0x20));
            } else if (head == "tab") {
                msg(WinMsg::chr(0x09));
            } else if (head == "c") {
                msg(WinMsg::chr('c'));
            } else if (auto vk = vkOf(head)) {
                msg(WinMsg::keyDown(*vk));
                msg(WinMsg::keyUp(*vk));
            } else if (head == "hold") {
                const size_t c2 = arg.find(':');
                const auto v = vkOf(arg.substr(0, c2));
                if (!v || c2 == std::string::npos) throw std::invalid_argument("hold");
                msg(WinMsg::keyDown(*v));
                msgAt(t + std::stoull(arg.substr(c2 + 1)), WinMsg::keyUp(*v));
            } else if (head == "kd" || head == "ku") {
                const auto v = vkOf(arg);
                if (!v) throw std::invalid_argument("key");
                msg(head == "kd" ? WinMsg::keyDown(*v) : WinMsg::keyUp(*v));
            } else if (head == "click" || head == "press" || head == "release" || head == "move") {
                const auto xy = parseXY(arg);
                if (!xy) throw std::invalid_argument("xy");
                msg(WinMsg::mouse(WinMsg::Type::MouseMove, xy->first, xy->second));
                if (head == "click" || head == "press") msg(WinMsg::mouse(WinMsg::Type::LButtonDown, xy->first, xy->second));
                if (head == "click" || head == "release") msg(WinMsg::mouse(WinMsg::Type::LButtonUp, xy->first, xy->second));
            } else {
                error = std::format("unknown script token '{}'", tok);
                return false;
            }
        } catch (const std::exception&) {
            error = std::format("bad script token '{}'", tok);
            return false;
        }
    }
    std::stable_sort(actions.begin(), actions.end(), [](const Action& a, const Action& b) { return a.at < b.at; });
    return true;
}

class ScriptDriver {
public:
    ScriptDriver(Engine& engine, ManualClock& clock, std::vector<Action> actions, std::filesystem::path outDir)
        : engine_(engine), clock_(clock), actions_(std::move(actions)), outDir_(std::move(outDir)) {}
    void begin() { start_ = clock_.nowMs(); started_ = true; }
    uint64_t elapsed() const { return started_ ? clock_.nowMs() - start_ : 0; }
    bool done() const { return next_ >= actions_.size(); }
    void poll() {
        if (!started_) return;
        const uint64_t t = elapsed();
        while (next_ < actions_.size() && actions_[next_].at <= t) {
            const Action& a = actions_[next_++];
            switch (a.kind) {
                case Action::Msg: engine_.input().post(a.msg); break;
                case Action::Shot: {
                    const auto path = outDir_ / (a.name + ".png");
                    engine_.presenter().writeGameAreaPng(path);
                    out("[{:>7} ms] shot {}\n", t, pathToUtf8(path));
                    break;
                }
                case Action::End: throw ScriptEnd{};
            }
        }
    }

private:
    Engine& engine_;
    ManualClock& clock_;
    std::vector<Action> actions_;
    size_t next_ = 0;
    std::filesystem::path outDir_;
    uint64_t start_ = 0;
    bool started_ = false;
};

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
        auto nextInt = [&](const char* what) { return std::atoi(next(what).c_str()); };
        if (s == "--room-run") {
            found = true;
        } else if (s == "--record") {
            a.usersFile = next("--record");
            a.player = next("--record");
        } else if (s == "--new-player") {
            a.newPlayer = next("--new-player");
        } else if (s == "--seed") {
            a.seed = static_cast<uint16_t>(std::stoul(next("--seed"), nullptr, 0));
        } else if (s == "--start") {
            a.start = true;
            a.startRoom = nextInt("--start");
            a.startX = nextInt("--start");
            a.startFrame = nextInt("--start");
            a.startFacing = nextInt("--start");
        } else if (s == "--coins") {
            a.coins = nextInt("--coins");
        } else if (s == "--nets") {
            a.nets = nextInt("--nets");
        } else if (s == "--key") {
            a.key = nextInt("--key");
        } else if (s == "--shop-coins") {
            a.shopCoins = nextInt("--shop-coins");
        } else if (s == "--gems") {
            a.gems = nextInt("--gems");
        } else if (s == "--no-elves") {
            a.noElves = true;
        } else if (s == "--elf") {
            std::array<int, 6> e{0, 0, 0, 0, 1, 0};
            std::istringstream es(next("--elf"));
            std::string part;
            for (size_t k = 0; k < e.size() && std::getline(es, part, ':'); ++k) e[k] = std::atoi(part.c_str());
            a.elves.push_back(e);
            a.noElves = true;
        } else if (s == "--pile") {
            a.pileRoom = nextInt("--pile");
            a.pileX = nextInt("--pile");
        } else if (s == "--climbs") {
            a.climbs = nextInt("--climbs");
        } else if (s == "--clues") {
            a.clues = nextInt("--clues");
        } else if (s == "--clue-box-mode") {
            a.clueBoxMode = nextInt("--clue-box-mode");
        } else if (s == "--quiz") {
            a.quiz = next("--quiz");
        } else if (s == "--keys") {
            a.keys += " " + next("--keys");
        } else if (s == "--keys-file") {
            const std::string f = next("--keys-file");
            std::ifstream in(pathFromUtf8(f));
            if (!in) error = std::format("cannot read {}", f);
            std::stringstream ss;
            ss << in.rdbuf();
            a.keys += " " + ss.str();
        } else if (s == "--dump-every") {
            a.dumpEvery = nextInt("--dump-every");
            a.dumpDir = next("--dump-every");
        } else if (s == "--out") {
            a.outDir = next("--out");
        } else if (s == "--log") {
            a.log = true;
        } else if (s == "--ticks") {
            a.maxTicks = std::strtoull(next("--ticks").c_str(), nullptr, 10);
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
    if (error.empty() && a.usersFile.empty() && a.newPlayer.empty()) error = "--record or --new-player is required";
    return a;
}

std::optional<std::filesystem::path> resolveDataPath(const Args& a) {
    if (!a.dataPath.empty()) return pathFromUtf8(a.dataPath);
    if (auto env = getEnv("TM_DATA")) return pathFromUtf8(*env);
    const char* base = SDL_GetBasePath();
    return vfs::autoDetectDataPath(base ? pathFromUtf8(base) : std::filesystem::path());
}

int runRoom(const Args& a, GameFiles& files, gfx::ColorMode mode) {
    std::vector<Action> actions;
    std::string error;
    if (!parseScript(a.keys, actions, error)) {
        std::fprintf(stderr, "tmport: %s\n", error.c_str());
        return 2;
    }
    const std::filesystem::path outDir = pathFromUtf8(a.outDir);
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    std::filesystem::path dumpDir;
    if (a.dumpEvery > 0) {
        dumpDir = pathFromUtf8(a.dumpDir);
        std::filesystem::create_directories(dumpDir, ec);
    }

    ManualClock clock(1000);
    EngineConfig cfg;
    cfg.clock = &clock;
    Engine engine(files, mode, cfg);  // headless: no window, no audio
    const text::Font font = text::Font::load(*files.res);
    text::TextRenderer text(font, &engine.frame(), &engine.presenter());
    ui::DialogSystem dialogs(engine, font);

    const GameTables tables = GameTables::load(*files.exe, *files.res);
    GameState state = GameState::initial(tables);
    if (!a.newPlayer.empty()) {
        state.player.setName(a.newPlayer);
        initNewPlayer(state);
        out("new player {}\n", a.newPlayer);
    } else {
        save::UsersTable t;
        if (!save::readUsersFile(pathFromUtf8(a.usersFile), t)) {
            std::fprintf(stderr, "tmport: cannot read %s\n", a.usersFile.c_str());
            return 3;
        }
        int idx = t.findByName(a.player);
        if (idx >= t.count) {
            char* end = nullptr;
            const long v = std::strtol(a.player.c_str(), &end, 10);
            if (end && *end == 0 && v >= 0 && v < t.count) idx = static_cast<int>(v);
        }
        if (idx >= t.count) {
            std::fprintf(stderr, "tmport: player %s not in %s\n", a.player.c_str(), a.usersFile.c_str());
            return 3;
        }
        loadPlayerRecord(state, t.slots[static_cast<size_t>(idx)].bytes);
        out("player {} loaded from slot {} (room {})\n", state.player.nameString(), idx, int(state.player.room));
    }

    AppContext ctx{engine, files, font, text, dialogs};
    ctx.state = &state;

    ScriptDriver driver(engine, clock, std::move(actions), outDir);
    clock.onSleep = [&driver](uint64_t) { driver.poll(); };

    std::vector<int> tickSfx;
    RoomHooks hooks;
    RoomLoop* loopPtr = nullptr;
    hooks.sfxObserver = [&tickSfx](int n) { tickSfx.push_back(n); };
    hooks.showHint = [&](int e) { out("[{:>7} ms] hint {} (popup not shown in this tool)\n", driver.elapsed(), e); };
    hooks.exitPrompt = [&] {
        out("[{:>7} ms] Esc: exit prompt\n", driver.elapsed());
        throw ScriptEnd{true};
    };
    hooks.askQuestion = [&](int slot) -> int {
        out("[{:>7} ms] elf question, slot {} (q {}), answer {}\n", driver.elapsed(), slot,
            int(state.quiz.slots[static_cast<size_t>(slot)].q), a.quiz);
        if (a.quiz == "esc" || slot < 0 || slot >= kQuizSlotCount) return 0;
        const QuizSlot& qs = state.quiz.slots[static_cast<size_t>(slot)];
        int pos = -1;
        for (int k = 0; k < 3; ++k) {
            const bool right = qs.perm[static_cast<size_t>(k)] == 0;
            if (qs.tried[static_cast<size_t>(k)] == 0 && (a.quiz == "correct" ? right : !right)) {
                pos = k;
                break;
            }
        }
        if (pos < 0) return 0;
        const AnswerOutcome o = answerQuestion(state, slot, pos, tables, engine.rng(), loopPtr->gameHooks());
        out("    answered position {}: {} (coins {})\n", pos, o.correct ? "correct" : "wrong", o.coins);
        return o.correct ? 1 : 0;
    };
    uint64_t lastTick = 0;
    hooks.afterTick = [&](uint64_t tick) {
        lastTick = tick;
        const PlayerState& pl = state.player;
        if (a.log) {
            std::string sfx;
            for (int n : tickSfx) sfx += std::format(" {}", n);
            const PlayerRuntime& p = loopPtr->player();
            out("tick {:>4} t={:>6} room {:>2} frame {:>2} x {:>4} y {:>3} {} coins {:>2} nets {:>2} coin {:>2} net {:>2} "
                "walk {} shop {}/{}{}{}\n",
                tick, driver.elapsed(), int(pl.room), int(pl.frame), pl.x, pl.y, pl.facingLeft ? 'L' : 'R', state.inv.coins,
                state.inv.nets, int(p.coinStep), int(p.netStep), int(p.walkActive), state.shop.coinsLeft, state.shop.state,
                sfx.empty() ? "" : " sfx", sfx);
        }
        tickSfx.clear();
        if (a.dumpEvery > 0 && tick % static_cast<uint64_t>(a.dumpEvery) == 0)
            engine.presenter().writeGameAreaPng(dumpDir / std::format("tick_{:05}.png", tick));
        if (tick >= a.maxTicks) throw ScriptEnd{};
    };

    int pauses = 0;
    hooks.beforePause = [&](int ms) {
        ++pauses;
        if (a.dumpEvery > 0)
            engine.presenter().writeGameAreaPng(dumpDir / std::format("tick_{:05}_pause{}.png", lastTick, pauses));
        if (a.log) out("[{:>7} ms] presentation pause {} ms\n", driver.elapsed(), ms);
    };
    RoomLoop loop(ctx, state, tables, engine.rng(), hooks);
    loopPtr = &loop;
    loop.setElfSpawning(!a.noElves);
    GameHooks& setupHooks = loop.gameHooks();

    int rc = 0;
    try {
        // GameMain 1118:00e5..0257.
        if (a.climbs >= 0) state.rank.climbsDone = static_cast<uint16_t>(a.climbs);
        engine.rng().srand(a.seed);
        gfx::FrameBuffer& fb = engine.frame();
        fb.setForeColor(0);
        fb.fillRect(0, 0, 512, 384);
        fb.fillRect(0, 384, 512, 267);
        loop.loadCommon();
        const ClimbSetup cs = setupClimb(state, tables, engine.rng(), setupHooks);
        out("climb set-up: seed {}, {} climb, room {}, layouts {} {} {}, elves {}\n", a.seed, cs.newClimb ? "new" : "resumed",
            int(state.player.room), cs.layoutRecord[0], cs.layoutRecord[1], cs.layoutRecord[2], loop.elves().count());
        if (a.start) {
            state.player.room = static_cast<uint8_t>(a.startRoom);
            state.player.x = static_cast<int16_t>(a.startX);
            state.player.frame = static_cast<uint8_t>(a.startFrame);
            state.player.y = loop.tables().frameY[static_cast<size_t>(a.startFrame) % kPlayerFrameSlots];
            state.player.facingLeft = static_cast<uint8_t>(a.startFacing);
        }
        if (a.coins >= 0) state.inv.coins = static_cast<uint16_t>(a.coins);
        if (a.nets >= 0) state.inv.nets = static_cast<uint16_t>(a.nets);
        if (a.key >= 0) state.inv.haveKey = static_cast<uint8_t>(a.key);
        if (a.shopCoins >= 0) state.shop.coinsLeft = static_cast<int16_t>(a.shopCoins);
        if (a.gems >= 0) state.inv.gemsFound = static_cast<uint8_t>(a.gems);
        if (a.clues >= 0) state.inv.numClues = static_cast<uint8_t>(a.clues);
        if (a.pileRoom >= 0) {
            state.coinPile.room = static_cast<uint8_t>(a.pileRoom);
            state.coinPile.x = static_cast<int16_t>(a.pileX);
        }
        if (!a.elves.empty()) {
            // Elves placed by hand (to reproduce a save state whose elves are unknown).
            ElfSystem& es = loop.elves();
            es.loadSets(files.res.get());
            for (size_t i = 0; i < a.elves.size() && i < static_cast<size_t>(kMaxElves); ++i) {
                Elf& e = es.elves()[i];
                const auto& v = a.elves[i];
                e = Elf{};
                e.set = e.behaviour = static_cast<int16_t>(v[0]);
                e.room = static_cast<int16_t>(v[1]);
                e.x = static_cast<int16_t>(v[2]);
                e.facing = static_cast<int16_t>(v[3]);
                e.step = static_cast<int16_t>(v[4]);
                e.hasItem = static_cast<int16_t>(v[5]);
                const auto& sc = loop.tables().elfScripts[static_cast<size_t>(std::clamp(v[0], 0, 4))];
                const size_t k = static_cast<size_t>(std::max(0, v[4] - 1));
                e.y = k < sc.y.size() ? sc.y[k] : int16_t{175};
            }
            es.setCount(static_cast<int>(a.elves.size()));
        }
        for (int i = 0; i < loop.elves().count(); ++i) {
            const Elf& e = loop.elves().elves()[static_cast<size_t>(i)];
            if (a.verbose) out("  elf {} set {} room {} x {} facing {} item {}\n", i, e.set, e.room, e.x, e.facing, e.hasItem);
        }
        loop.startClimb();
        if (a.clueBoxMode >= 0) loop.hud().setClueBoxMode(static_cast<uint8_t>(a.clueBoxMode), state);
        if (a.dumpEvery > 0) engine.presenter().writeGameAreaPng(dumpDir / "tick_00000.png");
        driver.begin();
        driver.poll();  // actions at time 0 reach the first tick's input fetch
        ClimbHooks ch;
        ch.stopMusic = [&] { out("[{:>7} ms] music stop\n", driver.elapsed()); };
        ch.playMusic = [&](int z) { out("[{:>7} ms] music zone {}\n", driver.elapsed(), z); };
        ch.levelCutscene = [&](int lvl) {
            out("[{:>7} ms] level cut-scene {} (not played in this tool)\n", driver.elapsed(), lvl);
            if (a.dumpEvery > 0)
                engine.presenter().writeGameAreaPng(dumpDir / std::format("tick_{:05}_cutscene.png", lastTick));
        };
        while (state.player.room != kRoomNewClimb && state.player.room != kCastleRoom) {
            const RoomChange r = loop.visitRoom(ch);
            out("[{:>7} ms] tick {} room {} -> {} (transition {}{}{})\n", driver.elapsed(), loop.ticks(), r.oldRoom,
                r.newRoom, int(r.transition), r.zoneChanged ? ", zone change" : "", r.levelChanged ? ", level change" : "");
        }
        out("castle wall reached: the castle climb is another module\n");
        if (a.dumpEvery > 0) engine.presenter().writeGameAreaPng(dumpDir / "final.png");
    } catch (const ScriptEnd& e) {
        out("[{:>7} ms] {} after {} ticks\n", driver.elapsed(), e.exitPrompt ? "stopped at the exit prompt" : "end", lastTick);
        rc = 0;
    }
    out("final: room {} x {} y {} frame {} coins {} nets {} key {} gems {} points {} shop {}\n", int(state.player.room),
        state.player.x, state.player.y, int(state.player.frame), state.inv.coins, state.inv.nets, int(state.inv.haveKey),
        int(state.inv.gemsFound), state.inv.climbPoints, state.shop.coinsLeft);
    return rc;
}

}  // namespace

std::string roomUsage() {
    return "tmport --room-run (--record <users.dat> <slot|name> | --new-player <name>) [--seed n]\n"
           "       [--start room x frame facing] [--coins n] [--nets n] [--key 0|1] [--shop-coins n] [--gems n]\n"
           "       [--no-elves] [--quiz correct|wrong|esc] [--keys \"<script>\"] [--keys-file f]\n"
           "       [--dump-every N dir] [--out dir] [--log] [--ticks N] [--data path] [--16] [-v]\n"
           "  script tokens: wait:MS tick:N shot:NAME end enter esc space c tab up down left right\n"
           "                 hold:KEY:MS kd:KEY ku:KEY click:X,Y press:X,Y release:X,Y move:X,Y\n";
}

std::optional<int> runRoomCommand(int argc, char** argv) {
    std::string error;
    auto a = parseArgs(argc, argv, error);
    if (!a) return std::nullopt;
    if (!error.empty()) {
        std::fprintf(stderr, "tmport: %s\n\n%s", error.c_str(), roomUsage().c_str());
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
        return runRoom(*a, files, ro.colorMode);
    } catch (const MissingFileError& e) {
        std::fprintf(stderr, "tmport: missing game file %s: %s\n", e.file().c_str(), e.what());
        return 3;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tmport: %s\n", e.what());
        return 3;
    }
}

}  // namespace tmport::game::room
