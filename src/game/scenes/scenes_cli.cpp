#include "game/scenes/scenes_cli.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "anim/anim_engine.h"
#include "anim/anim_player.h"
#include "audio/audio_system.h"
#include "audio/audio_tables.h"
#include "audio/engine_input_port.h"
#include "audio/midi_backend.h"
#include "audio/midi_driver.h"
#include "audio/midi_player.h"
#include "audio/sound.h"
#include "audio/wave_output.h"
#include "cli/tool_mods.h"
#include "data/exe_tables.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/layout.h"
#include "game/rules.h"
#include "game/scenes/castle_climb.h"
#include "game/scenes/celebration.h"
#include "game/scenes/hint_popup.h"
#include "game/scenes/level_cutscene.h"
#include "game/scenes/quiz_screen.h"
#include "game/scenes/scene_hud.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"
#include "mischief/util/log.h"
#include "mischief/vfs/data_source.h"
#include "platform/engine.h"
#include "res/game_files.h"
#include "res/resource_manager.h"
#include "text/font.h"
#include "text/text_renderer.h"
#include "ui/game_dialog.h"

namespace tmport::game::scenes {

namespace {

struct Args {
    std::string kind;
    int arg = 0;
    bool hasArg = false;
    std::string keys;
    std::string out = "extracted/scratch/scenes/out";
    std::string dataPath;
    int shotEvery = 0;
    int seed = 1;
    int tier = 0;
    int voice = 1, sound = 1;
    bool noAudio = false;
    uint64_t maxMs = 600000;
    bool trace = false;
    int slot = 0;
    std::string perm = "012", tried = "000";
    int clues = 0;
    int room = -1;
    long total = -1;
    int points = 0;
    int coins = -1;
    int nets = -1;
    std::string name = "Tester";
    bool verbose = false;
    std::vector<std::string> modDirs;  // --mods <dir> (repeatable, cli/tool_mods.h)
};

bool parseInt(const std::string& s, long& v) {
    try {
        size_t n = 0;
        v = std::stol(s, &n, 0);
        return n == s.size();
    } catch (...) {
        return false;
    }
}

std::optional<Args> parseArgs(int argc, char** argv, std::string& error) {
    int i = 1;
    while (i < argc && std::string(argv[i]) != "--scene") ++i;
    if (i >= argc) return std::nullopt;
    Args a;
    if (i + 1 >= argc) {
        error = "--scene needs a kind";
        return a;
    }
    a.kind = argv[i + 1];
    std::vector<std::string> rest;
    for (int k = 1; k < argc; ++k)
        if (k != i && k != i + 1) rest.emplace_back(argv[k]);
    auto need = [&](size_t& k) -> std::string {
        if (k + 1 >= rest.size()) {
            error = rest[k] + " needs a value";
            return {};
        }
        return rest[++k];
    };
    for (size_t k = 0; k < rest.size() && error.empty(); ++k) {
        const std::string& o = rest[k];
        long v = 0;
        auto num = [&](auto& dst) {
            const std::string s = need(k);
            if (error.empty() && !parseInt(s, v)) error = "bad number for " + o + ": " + s;
            dst = static_cast<std::remove_reference_t<decltype(dst)>>(v);
        };
        if (o == "--keys") a.keys = need(k);
        else if (o == "--keys-file") {
            std::ifstream f(mischief::pathFromUtf8(need(k)));
            std::stringstream ss;
            ss << f.rdbuf();
            a.keys = ss.str();
        } else if (o == "--out") a.out = need(k);
        else if (o == "--data") a.dataPath = need(k);
        else if (o == "--shot-every") num(a.shotEvery);
        else if (o == "--seed") num(a.seed);
        else if (o == "--tier") num(a.tier);
        else if (o == "--voice") num(a.voice);
        else if (o == "--sound") num(a.sound);
        else if (o == "--no-audio") a.noAudio = true;
        else if (o == "--max-ms") num(a.maxMs);
        else if (o == "--trace") a.trace = true;
        else if (o == "--slot") num(a.slot);
        else if (o == "--perm") a.perm = need(k);
        else if (o == "--tried") a.tried = need(k);
        else if (o == "--clues") num(a.clues);
        else if (o == "--room") num(a.room);
        else if (o == "--total") num(a.total);
        else if (o == "--points") num(a.points);
        else if (o == "--coins") num(a.coins);
        else if (o == "--nets") num(a.nets);
        else if (o == "--name") a.name = need(k);
        else if (o == "--mods") a.modDirs.push_back(need(k));
        else if (o == "-v" || o == "--verbose") a.verbose = true;
        else if (!a.hasArg && parseInt(o, v)) {
            a.arg = static_cast<int>(v);
            a.hasArg = true;
        } else {
            error = "unknown option " + o;
        }
    }
    return a;
}

// ------------------------------------------------------------------------- silent timed wave out

class ClockWaveOutput final : public audio::WaveOutput {
public:
    explicit ClockWaveOutput(mischief::Clock& c) : clock_(c) {}
    bool available() const override { return true; }
    void start(const audio::Sound& s) override {
        startMs_ = clock_.nowMs();
        durMs_ = s.durationUs() / 1000;
    }
    void stop() override { durMs_ = 0; }
    bool done() override { return clock_.nowMs() >= startMs_ + durMs_; }

private:
    mischief::Clock& clock_;
    uint64_t startMs_ = 0, durMs_ = 0;
};

// ---------------------------------------------------------------------------------------- script

struct ScriptEnd {};

struct Step {
    enum class Kind { Key, KeyDown, KeyUp, Code, Mouse, Shot, End };
    Kind kind = Kind::Key;
    uint64_t at = 0;
    SDL_Keycode key = 0;
    SDL_Scancode scan = SDL_SCANCODE_UNKNOWN;
    SDL_Keymod mod = SDL_KMOD_NONE;
    std::string text;
    uint16_t code = 0;
    int x = 0, y = 0;
    int button = 0;       // 0 move only, SDL_BUTTON_LEFT / RIGHT
    int phase = 3;        // 1 press, 2 release, 3 both
    std::string name;
};

bool keyFor(const std::string& n, Step& s) {
    struct K {
        const char* name;
        SDL_Keycode key;
        SDL_Scancode scan;
        const char* text;
    };
    static const K keys[] = {
        {"enter", SDLK_RETURN, SDL_SCANCODE_RETURN, ""},  {"esc", SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, ""},
        {"space", SDLK_SPACE, SDL_SCANCODE_SPACE, " "},   {"tab", SDLK_TAB, SDL_SCANCODE_TAB, ""},
        {"up", SDLK_UP, SDL_SCANCODE_UP, ""},             {"down", SDLK_DOWN, SDL_SCANCODE_DOWN, ""},
        {"left", SDLK_LEFT, SDL_SCANCODE_LEFT, ""},       {"right", SDLK_RIGHT, SDL_SCANCODE_RIGHT, ""},
        {"c", SDLK_C, SDL_SCANCODE_C, "c"},           {"y", SDLK_Y, SDL_SCANCODE_Y, "y"},
        {"n", SDLK_N, SDL_SCANCODE_N, "n"},
    };
    if (n == "ctrl+s") {
        s.key = SDLK_S;
        s.scan = SDL_SCANCODE_S;
        s.mod = SDL_KMOD_LCTRL;
        return true;
    }
    for (const K& k : keys)
        if (n == k.name) {
            s.key = k.key;
            s.scan = k.scan;
            s.text = k.text;
            return true;
        }
    return false;
}

bool isMouseHead(const std::string& head) {
    return head == "click" || head == "rclick" || head == "press" || head == "release" || head == "move";
}

// The script's tokens. They are separated by white space or by commas (the game's --keys syntax);
// the comma between X and Y of a mouse token belongs to the token (click:X,Y; click:X:Y also works).
std::vector<std::string> scriptTokens(const std::string& text) {
    auto isNumber = [](const std::string& s) {
        long v = 0;
        return parseInt(s, v);
    };
    // A mouse token that still lacks its Y ("click:10" of "click:10,20").
    auto wantsY = [&](const std::string& t) {
        const size_t c = t.find(':');
        if (c == std::string::npos || !isMouseHead(t.substr(0, c))) return false;
        const std::string v = t.substr(c + 1);
        return v.find_first_of(",:") == std::string::npos && isNumber(v);
    };
    std::vector<std::string> toks;
    std::istringstream in(text);
    std::string word;
    while (in >> word) {
        size_t p = 0;
        for (;;) {
            const size_t c = word.find(',', p);
            const std::string piece = word.substr(p, c == std::string::npos ? std::string::npos : c - p);
            if (!piece.empty()) {
                if (!toks.empty() && wantsY(toks.back()) && isNumber(piece))
                    toks.back() += "," + piece;
                else
                    toks.push_back(piece);
            }
            if (c == std::string::npos) break;
            p = c + 1;
        }
    }
    return toks;
}

std::optional<std::vector<Step>> parseScript(const std::string& text, std::string& error) {
    std::vector<Step> steps;
    uint64_t t = 0;
    auto xy = [&](const std::string& v, Step& s) {
        size_t c = v.find(',');
        if (c == std::string::npos) c = v.find(':');
        long x = 0, y = 0;
        if (c == std::string::npos || !parseInt(v.substr(0, c), x) || !parseInt(v.substr(c + 1), y)) return false;
        s.x = static_cast<int>(x);
        s.y = static_cast<int>(y);
        return true;
    };
    for (const std::string& tok : scriptTokens(text)) {
        Step s;
        s.at = t;
        const size_t colon = tok.find(':');
        const std::string head = tok.substr(0, colon);
        const std::string val = colon == std::string::npos ? "" : tok.substr(colon + 1);
        long v = 0;
        if (head == "wait" && parseInt(val, v)) {
            t += static_cast<uint64_t>(v);
            continue;
        }
        if (head == "shot" && !val.empty()) {
            s.kind = Step::Kind::Shot;
            s.name = val;
        } else if (head == "end") {
            s.kind = Step::Kind::End;
        } else if (head == "code" && parseInt(val, v)) {
            s.kind = Step::Kind::Code;
            s.code = static_cast<uint16_t>(v);
        } else if (isMouseHead(head) && xy(val, s)) {
            s.kind = Step::Kind::Mouse;
            s.button = head == "move" ? 0 : head == "rclick" ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
            s.phase = head == "press" ? 1 : head == "release" ? 2 : 3;
        } else if (head == "hold") {
            const size_t c2 = val.find(':');
            Step d;
            if (c2 == std::string::npos || !keyFor(val.substr(0, c2), d) || !parseInt(val.substr(c2 + 1), v)) {
                error = "bad token " + tok;
                return std::nullopt;
            }
            d.kind = Step::Kind::KeyDown;
            d.at = t;
            steps.push_back(d);
            t += static_cast<uint64_t>(v);
            d.kind = Step::Kind::KeyUp;
            d.at = t;
            steps.push_back(d);
            continue;
        } else if ((head == "down" || head == "up") && colon != std::string::npos && keyFor(val, s)) {
            s.kind = head == "down" ? Step::Kind::KeyDown : Step::Kind::KeyUp;
        } else if (colon == std::string::npos && keyFor(tok, s)) {
            s.kind = Step::Kind::Key;
        } else {
            error = "bad token " + tok;
            return std::nullopt;
        }
        steps.push_back(s);
    }
    return steps;
}

// ------------------------------------------------------------------------------------------- rig

struct Rig {
    Args args;
    GameFiles files;
    mischief::ManualClock clock{1000};
    std::unique_ptr<Engine> engine;
    std::unique_ptr<text::Font> font;
    std::unique_ptr<text::TextRenderer> text;
    std::unique_ptr<ui::DialogSystem> dialogs;
    std::unique_ptr<anim::AnimEngine> anim;
    std::unique_ptr<audio::AudioSystem> audio;
    GameTables tables;
    GameState state;
    std::unique_ptr<AppContext> ctx;
    std::unique_ptr<HudStandIn> hud;
    SceneHooks hooks;
    std::vector<Step> steps;
    size_t next = 0;
    uint64_t startMs = 0;
    uint64_t nextShot = 0;
    int shotIndex = 0;
    std::filesystem::path outDir;
    int hookId = 0;

    void post(const Step& s);
    void onPump();
    bool shot(const std::string& name);
};

bool Rig::shot(const std::string& name) {
    const auto p = outDir / (name + ".png");
    const bool ok = engine->presenter().writeGameAreaPng(p);
    if (!ok) std::fprintf(stderr, "scene: could not write %s\n", mischief::pathToUtf8(p).c_str());
    return ok;
}

void Rig::post(const Step& s) {
    SDL_Event e{};
    switch (s.kind) {
        case Step::Kind::Code: engine->postKey(s.code); break;
        case Step::Kind::Key:
        case Step::Kind::KeyDown:
        case Step::Kind::KeyUp:
            e.key.key = s.key;
            e.key.scancode = s.scan;
            e.key.mod = s.mod;
            if (s.kind != Step::Kind::KeyUp) {
                e.type = SDL_EVENT_KEY_DOWN;
                e.key.down = true;
                engine->postEvent(e);
                if (!s.text.empty()) {
                    SDL_Event t{};
                    t.type = SDL_EVENT_TEXT_INPUT;
                    t.text.text = s.text.c_str();
                    engine->postEvent(t);
                }
            }
            if (s.kind != Step::Kind::KeyDown) {
                e.type = SDL_EVENT_KEY_UP;
                e.key.down = false;
                engine->postEvent(e);
            }
            break;
        case Step::Kind::Mouse: {
            SDL_Event m{};
            m.type = SDL_EVENT_MOUSE_MOTION;
            m.motion.x = static_cast<float>(s.x + 64) + 0.5f;
            m.motion.y = static_cast<float>(s.y + 48) + 0.5f;
            engine->postEvent(m);
            if (!s.button) break;
            e.button.button = static_cast<uint8_t>(s.button);
            e.button.clicks = 1;
            e.button.x = m.motion.x;
            e.button.y = m.motion.y;
            if (s.phase & 1) {
                e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                e.button.down = true;
                engine->postEvent(e);
            }
            if (s.phase & 2) {
                e.type = SDL_EVENT_MOUSE_BUTTON_UP;
                e.button.down = false;
                engine->postEvent(e);
            }
            break;
        }
        default: break;
    }
}

void Rig::onPump() {
    const uint64_t now = clock.nowMs() - startMs;
    while (args.shotEvery > 0 && now >= nextShot) {
        shot(std::format("{:04d}", shotIndex++));
        nextShot += static_cast<uint64_t>(args.shotEvery);
    }
    while (next < steps.size() && steps[next].at <= now) {
        const Step& s = steps[next++];
        if (s.kind == Step::Kind::Shot) {
            shot(s.name);
            continue;
        }
        if (s.kind == Step::Kind::End) throw ScriptEnd{};
        post(s);
    }
    if (now > args.maxMs) throw ScriptEnd{};
}

std::optional<std::filesystem::path> resolveData(const Args& a) {
    if (!a.dataPath.empty()) return mischief::pathFromUtf8(a.dataPath);
    if (auto env = mischief::getEnv("TM_DATA")) return mischief::pathFromUtf8(*env);
    const char* base = SDL_GetBasePath();
    return mischief::vfs::autoDetectDataPath(base ? mischief::pathFromUtf8(base) : std::filesystem::path());
}

void setupRig(Rig& r) {
    EngineConfig cfg;
    cfg.clock = &r.clock;
    r.engine = std::make_unique<Engine>(r.files, mischief::gfx::ColorMode::Colour256, cfg);
    r.font = std::make_unique<text::Font>(text::Font::load(*r.files.res));
    r.text = std::make_unique<text::TextRenderer>(*r.font, &r.engine->frame(), &r.engine->presenter());
    r.dialogs = std::make_unique<ui::DialogSystem>(*r.engine, *r.font);
    r.dialogs->hooks().getInputCode = [&r] {
        const uint16_t c = r.engine->getInputCode();
        if (!c) r.clock.sleepMs(1);
        return c;
    };
    r.anim = std::make_unique<anim::AnimEngine>(r.engine->frame(), anim::enginePresent(*r.engine));
    if (!r.args.noAudio) {
        audio::AudioParts parts;
        parts.wave = std::make_unique<ClockWaveOutput>(r.clock);
        parts.sounds = std::make_unique<audio::ResourceSoundLoader>(*r.files.res);
        audio::MicroClock us = [&r] { return r.clock.nowMs() * 1000; };
        parts.midi = std::make_unique<audio::MidiPlayer>(std::make_unique<audio::NullMidiBackend>(), us, false);
        parts.tracks = std::make_unique<audio::DataSourceTrackLoader>(*r.files.source);
        r.audio = std::make_unique<audio::AudioSystem>(audio::AudioTables::fromExe(*r.files.exe), std::move(parts));
        r.audio->settings().voiceOn = static_cast<uint8_t>(r.args.voice);
        r.audio->settings().soundOn = static_cast<uint8_t>(r.args.sound);
        audio::connectAudioToEngine(*r.engine, *r.audio);
    }
    r.tables = GameTables::load(*r.files.exe, *r.files.res);
    r.state = GameState::initial(r.tables);
    r.state.options.voiceOn = static_cast<uint8_t>(r.args.voice);
    r.state.options.soundOn = static_cast<uint8_t>(r.args.sound);
    r.state.player.setName(r.args.name);
    r.state.rank.rankTier = static_cast<uint8_t>(r.args.tier & 3);
    r.state.rooms.clearSpots();  // no hiding places until a layout is generated
    r.ctx = std::make_unique<AppContext>(AppContext{*r.engine, r.files, *r.font, *r.text, *r.dialogs});
    r.ctx->audio = r.audio.get();
    r.ctx->anim = r.anim.get();
    r.ctx->state = &r.state;
    r.hud = std::make_unique<HudStandIn>(*r.ctx, r.state, r.tables);
    r.hud->bind(r.hooks);
    if (r.args.trace)
        r.hooks.trace = [&r](std::string_view ev) {
            std::printf("[%7llu ms] %.*s\n", static_cast<unsigned long long>(r.clock.nowMs() - r.startMs),
                        static_cast<int>(ev.size()), ev.data());
        };
    // DAM FC / FB -> Sfx_Play / Sfx_Stop (the app's binding of AppContext::anim).
    anim::AnimSound snd;
    snd.play = [&r](int n) {
        if (r.hooks.trace) r.hooks.trace(std::format("sfx {} (DAM)", 7000 + n));
        if (r.audio) r.audio->sfxPlay(n);
    };
    snd.stop = [&r] {
        if (r.audio) r.audio->sfxStop();
    };
    r.anim->setSound(std::move(snd));
}

// Room_DrawBackground (1198:0234) without the net-shop sign: background into layers 1 and 2, the
// slot pictures into layer 1, layer 1 copied to the screen; plus the gate lock (frame by key, or
// `lockFrame`) and the player at the room's start pose.
void drawRoom(Rig& r, int room, int lockFrame = -1, bool player = true) {
    gfx::FrameBuffer& fb = r.engine->frame();
    const ResourceManager& res = *r.files.res;
    const RoomDef& def = r.state.rooms[room];
    fb.setFullClip();
    if (auto bg = res.pixmap(def.bgPmap)) fb.drawOpaque(0, 404, *bg);
    fb.copyRect(0, 404, 512, 267, 0, 788);
    static constexpr int kSlotX[3] = {26, 197, 368};
    for (int i = 0; i < 3; ++i) {
        const uint8_t kind = r.state.rooms.spotKind(room, i);
        if (kind >= 0xFD) continue;
        if (auto pm = res.pixmap(1400 + kind)) fb.drawSprite(kSlotX[i], 0x1DA, *pm, false);
    }
    // Gate lock (1188:0267) in the gate rooms.
    static constexpr struct {
        int room, x, y;
    } kLocks[] = {{11, 342, 143}, {25, 410, 143}, {38, 155, 156}};
    for (const auto& l : kLocks) {
        if (l.room != room) continue;
        const int frame = lockFrame >= 0 ? lockFrame : (r.state.inv.haveKey ? 1 : 0);
        if (const auto b = res.bundle(415); b && frame < static_cast<int>(b->ids.size()))
            if (auto pm = res.pixmap(b->ids[static_cast<size_t>(frame)])) fb.drawSprite(l.x, l.y + 384, *pm, false);
    }
    fb.copyRect(0, 404, 512, 267, 0, 20);
    fb.setPlayClip(0);
    if (player) {
        PlayerState& p = r.state.player;
        p.x = def.startX;
        p.y = def.startY;
        p.frame = def.startFrame;
        p.facingLeft = def.startFacingLeft;
        if (const auto b = res.bundle(300); b && p.frame < b->ids.size())
            if (auto pm = res.pixmap(b->ids[p.frame])) {
                p.w = static_cast<int16_t>(pm->width);
                p.h = static_cast<int16_t>(pm->height);
                fb.drawSprite(p.x, p.y, *pm, p.facingLeft != 0);
            }
    }
}

void blackScreen(Rig& r) {
    gfx::FrameBuffer& fb = r.engine->frame();
    fb.setForeColor(0);
    fb.fillRect(0, 0, 512, 384);
    fb.fillRect(0, 384, 512, 267);
}

// A climb layout for the clue words (Layout_GenerateClimb + Clue_BuildTexts / VoiceIds).
void makeLayout(Rig& r) {
    generateClimbLayout(r.state.rooms, r.state.clues, r.state.rank.rankTier, r.tables, r.engine->rng());
    buildClueTexts(r.state.clues, r.tables.words);
    buildClueVoiceIds(r.state.clues);
}

int runScene(Rig& r) {
    const Args& a = r.args;
    r.engine->rng().srand(static_cast<uint16_t>(a.seed));
    Engine& engine = *r.engine;
    if (a.coins >= 0) r.state.inv.coins = static_cast<uint16_t>(a.coins);
    if (a.nets >= 0) r.state.inv.nets = static_cast<uint16_t>(a.nets);
    r.state.inv.climbPoints = static_cast<uint16_t>(a.points);  // HUD treasure count / climb points
    auto showHud = [&] {
        r.hud->setClueBoxMode(0);
        engine.presentAll();
    };
    if (a.kind == "quiz") {
        if (!a.hasArg || a.arg < 0 || a.arg >= kQuestionCount) throw mischief::Error("quiz needs a question 0..358");
        r.state.player.room = static_cast<uint8_t>(a.room >= 0 ? a.room : 9);
        makeLayout(r);
        r.state.inv.numClues = static_cast<uint8_t>(a.clues);
        QuizSlot& slot = r.state.quiz.slots[static_cast<size_t>(a.slot)];
        slot.q = static_cast<int16_t>(a.arg);
        for (size_t i = 0; i < 3; ++i) {
            slot.perm[i] = static_cast<uint8_t>(i < a.perm.size() ? a.perm[i] - '0' : i);
            slot.tried[i] = static_cast<uint8_t>(i < a.tried.size() && a.tried[i] == '1');
        }
        drawRoom(r, r.state.player.room);
        showHud();
        const QuizResult q = askQuestion(*r.ctx, r.state, r.tables, a.slot, r.hooks);
        std::printf("quiz: result %d answered %d correct %d position %d message %d clue %d coins %d -> coins %u, "
                    "clues %u, status %u, slot q %d\n",
                    q.result, q.answered, q.correct, q.position, q.messageIndex, q.clueIndex, q.coins, r.state.inv.coins,
                    r.state.inv.numClues, r.state.quiz.status[static_cast<size_t>(a.arg)], slot.q);
        if (q.clueIndex >= 0) {
            const std::string_view w =
                r.state.clues.text[r.state.currentLevel() % kLevelCount][static_cast<size_t>(q.clueIndex)];
            std::printf("quiz: clue word '%.*s'\n", static_cast<int>(w.size()), w.data());
        }
    } else if (a.kind == "hint") {
        r.state.player.room = static_cast<uint8_t>(a.room >= 0 ? a.room : 9);
        drawRoom(r, r.state.player.room);
        showHud();
        showHint(*r.ctx, a.arg, r.hooks);
        std::printf("hint %d dismissed\n", a.arg);
    } else if (a.kind == "cutscene") {
        const int level = a.arg;
        const int room = level == 1 ? 25 : 11;
        r.state.player.room = static_cast<uint8_t>(room);
        drawRoom(r, room, 2, false);
        showHud();
        const int res = playLevelCutscene(*r.ctx, level, r.hooks);
        std::printf("cut-scene %d: part 1 result %d\n", level, res);
    } else if (a.kind == "celebrate") {
        r.state.rank.climbsDone = static_cast<uint16_t>(a.arg > 0 ? a.arg - 1 : 0);
        if (a.total >= 0) r.state.rank.totalTreasures = static_cast<uint32_t>(a.total);
        r.state.player.room = kRoomNewClimb;
        blackScreen(r);
        engine.presentAll();
        const CelebrationInfo c = celebrate(*r.ctx, r.state, r.tables, r.hooks);
        std::printf("celebrate: climbs %u total %u rank %d promoted %d champion %d treasure %d music %d hint %d\n",
                    r.state.rank.climbsDone, r.state.rank.totalTreasures, c.rank, c.promoted, c.champion,
                    c.deliveredTreasure, c.musicZone, r.state.hints.pending);
    } else if (a.kind == "rank" || a.kind == "seerank") {
        r.state.rank.totalTreasures = static_cast<uint32_t>(a.hasArg ? a.arg : 0);
        if (a.kind == "rank") {
            blackScreen(r);
            engine.presentAll();
            engine.frame().setPlayClip(0);
            showRankScreen(*r.ctx, r.state, r.tables, r.hooks);
        } else {
            r.state.player.room = static_cast<uint8_t>(a.room >= 0 ? a.room : 9);
            drawRoom(r, r.state.player.room);
            showHud();
            seeRank(*r.ctx, r.state, r.tables, r.hooks);
        }
        std::printf("%s: total %u rank %d\n", a.kind.c_str(), r.state.rank.totalTreasures, r.state.rank.rank);
    } else if (a.kind == "castle") {
        r.state.player.room = static_cast<uint8_t>(kCastleRoom);
        const RoomDef& def = r.state.rooms[kCastleRoom];
        r.state.player.x = def.startX;
        r.state.player.y = def.startY;
        blackScreen(r);
        showHud();
        runCastleClimb(*r.ctx, r.state, r.tables, r.hooks);
        std::printf("castle: done, room 0x%02X, coins %u nets %u\n", r.state.player.room, r.state.inv.coins,
                    r.state.inv.nets);
    } else {
        throw mischief::Error("unknown scene kind " + a.kind);
    }
    return 0;
}

}  // namespace

std::optional<int> runScenesCommand(int argc, char** argv) {
    std::string error;
    auto a = parseArgs(argc, argv, error);
    if (!a) return std::nullopt;
    if (!error.empty()) {
        std::fprintf(stderr, "tmport: %s\n\n%s", error.c_str(), scenesUsage().c_str());
        return 2;
    }
    mischief::log::setVerbose(a->verbose);
    const auto dataPath = resolveData(*a);
    if (!dataPath) {
        std::fprintf(stderr, "tmport: the game data was not found. Pass --data <folder or CD image> or set TM_DATA.\n");
        return 2;
    }
    Rig r;
    r.args = *a;
    auto steps = parseScript(a->keys, error);
    if (!steps) {
        std::fprintf(stderr, "tmport: %s in --keys\n\n%s", error.c_str(), scenesUsage().c_str());
        return 2;
    }
    r.steps = std::move(*steps);
    try {
        r.files = GameFiles::open(*dataPath);
        cli::applyToolMods(r.files, a->modDirs);
        r.outDir = mischief::pathFromUtf8(a->out);
        std::filesystem::create_directories(r.outDir);
        setupRig(r);
        r.startMs = r.clock.nowMs();
        r.hookId = r.engine->addPumpHook([&r](PumpPhase) {
            if (r.audio) r.audio->service();
            r.onPump();
        });
        int rc = 0;
        try {
            rc = runScene(r);
        } catch (const ScriptEnd&) {
            std::printf("scene: script ended at %llu ms\n", static_cast<unsigned long long>(r.clock.nowMs() - r.startMs));
        } catch (const QuitGame&) {
            std::printf("scene: QuitGame (Exit answered Yes)\n");
        }
        r.engine->removePumpHook(r.hookId);
        // Steps that are due at the very end (a final shot).
        r.shot("final");
        std::printf("scene: %llu ms simulated\n", static_cast<unsigned long long>(r.clock.nowMs() - r.startMs));
        return rc;
    } catch (const mischief::MissingFileError& e) {
        std::fprintf(stderr, "tmport: missing game file %s: %s\n", e.file().c_str(), e.what());
        return 3;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tmport: %s\n", e.what());
        return 3;
    }
}

std::string scenesUsage() {
    return "Scene tools (headless, scripted input on a simulated clock, silent timed audio):\n"
           "  --scene quiz <q> | hint <n> | cutscene <level> | celebrate <climbs> | rank <total> |\n"
           "          seerank <total> | castle   [--keys <script>] [--keys-file <f>] [--out <dir>]\n"
           "          [--shot-every <ms>] [--max-ms <n>] [--seed n] [--tier t] [--voice 0|1] [--sound 0|1]\n"
           "          [--no-audio] [--trace] [--slot n --perm 012 --tried 000 --clues n] [--room n]\n"
           "          [--total n --points n] [--coins n] [--nets n] [--name <player>] [--data <path>]\n"
           "          [--mods <dir>]... [-v]\n"
           "        script tokens, separated by spaces or commas (times from the start of the scene):\n"
           "          wait:MS shot:NAME end; enter esc space tab ctrl+s y n c up down left right (a press\n"
           "          and release in the same instant); hold:KEY:MS (an arrow held for MS ms: walking and\n"
           "          climbing need it, the 100 ms tick does not see a press); down:KEY up:KEY;\n"
           "          code:0xNNNN; click:X,Y rclick:X,Y press:X,Y release:X,Y move:X,Y (game coordinates)\n"
           "        The run ends at `end`, when the scene returns or after --max-ms of simulated time\n"
           "        (default 600000); it writes <out>/final.png (default out: extracted/scratch/scenes/out\n"
           "        under the current directory) and, with --shot-every, <out>/NNNN.png.\n";
}

}  // namespace tmport::game::scenes
