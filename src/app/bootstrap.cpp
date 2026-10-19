#include "app/bootstrap.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_messagebox.h>

#include <cstdio>
#include <format>

#include "anim/anim_engine.h"
#include "app/engine_glue.h"
#include "app/menu_commands.h"
#include "app/script_input.h"
#include "audio/audio_system.h"
#include "dos/app/dos_entry.h"
#include "dos/app/script_input.h"
#include "dos/data/dos_files.h"
#include "game/game_main.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "platform/engine.h"
#include "platform/engine_setup.h"
#include "mods/mods.h"
#include "res/game_files.h"
#include "save/settings.h"
#include "text/font.h"
#include "text/text_renderer.h"
#include "ui/game_dialog.h"
#include "ui/menu_bar.h"
#include "util/file_util.h"
#include "util/log.h"
#include "vfs/data_source.h"

namespace tmport::app {

namespace {

constexpr const char* kAppTitle = "Treasure Mountain! (TM Port)";
constexpr const char* kDosTitle = "Treasure Mountain! 1990 DOS edition (TM Port)";

// --frame-dump for the DOS edition: an existing folder or a name without an extension is a folder
// for one PNG per screen (DosOptions::frameDumpDir); anything else is the canvas file at the end.
bool isFrameDumpFolder(const std::string& frameDump) {
    if (frameDump.empty()) return false;
    const std::filesystem::path p = pathFromUtf8(frameDump);
    std::error_code ec;
    return std::filesystem::is_directory(p, ec) || !p.has_extension();
}

std::filesystem::path exeDirectory() {
    const char* base = SDL_GetBasePath();  // owned by SDL
    return base ? pathFromUtf8(base) : std::filesystem::path();
}

audio::MusicSet musicSetFrom(const LaunchOptions& o, const save::EngineSettings& s) {
    if (!o.midiSet.empty()) return (o.midiSet == "B" || o.midiSet == "b") ? audio::MusicSet::Base : audio::MusicSet::Extended;
    return s.midiSet == save::MidiSet::Base ? audio::MusicSet::Base : audio::MusicSet::Extended;
}

audio::MidiBackendKind midiBackendFrom(const LaunchOptions& o, const save::EngineSettings& s) {
    if (o.headless || o.noAudio) return audio::MidiBackendKind::Null;
    if (o.midiBackend == "null") return audio::MidiBackendKind::Null;
    if (o.midiBackend == "winmm") return audio::MidiBackendKind::WinMM;
    if (o.midiBackend == "auto") return audio::MidiBackendKind::Auto;
    if (o.midiBackend == "soundfont" || (o.midiBackend.empty() && !o.soundFont.empty())) return audio::MidiBackendKind::SoundFont;
    switch (s.midiBackend) {
        case save::MidiBackend::Null: return audio::MidiBackendKind::Null;
        case save::MidiBackend::WinMM: return audio::MidiBackendKind::WinMM;
        case save::MidiBackend::SoundFont: return audio::MidiBackendKind::SoundFont;
        case save::MidiBackend::Auto: break;
    }
    return audio::MidiBackendKind::Auto;
}

std::string howToPointAtData(const save::Paths& paths) {
    return std::format(
        "Point the game at the TMTWINCD folder of the Treasure Mountain! Windows CD (or at the CD image, "
        ".cue/.bin/.iso):\n"
        "  - start it with  --data <folder or image>,\n"
        "  - or set the environment variable TM_DATA,\n"
        "  - or write  data = <folder or image>  in the [engine] section of\n    {}\n"
        "(The 1990 DOS edition plays from its folder with SST.EXE: --data, TM_DATA_DOS, or  data =  in "
        "the [dos] section.)",
        paths.iniPath.empty() ? std::string("tmport.ini") : pathToUtf8(paths.iniPath));
}

std::string howToPointAtDos() {
    return "Point the game at the folder of the 1990 DOS edition (the one with SST.EXE, SPRITES.PAK and "
           "ALL_PUZS):\n"
           "  - start it with  --data <folder>,\n"
           "  - or set the environment variable TM_DATA_DOS,\n"
           "  - or write  data = <folder>  in the [dos] section of tmport.ini.";
}

// The DOS edition's folder when no --data was given: TM_DATA_DOS, [dos] data, auto-detection.
std::string resolveDosDataPath(const std::string& iniDosData, const std::filesystem::path& exeDir) {
    if (auto env = getEnv("TM_DATA_DOS"); env && !env->empty()) return *env;
    if (!iniDosData.empty()) return iniDosData;
    if (auto p = autoDetectDosPath(exeDir)) return pathToUtf8(*p);
    return {};
}

}  // namespace

ErrorSink defaultErrorSink(bool headless) {
    return [headless](const StartupError& e) {
        std::fprintf(stderr, "tmport: %s\n", e.what());
        std::fflush(stderr);
        if (!headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kAppTitle, e.what(), nullptr);
    };
}

std::string resolveDataPath(const LaunchOptions& o, const std::string& iniDataPath) {
    if (!o.dataPath.empty()) return o.dataPath;
    if (auto env = getEnv("TM_DATA"); env && !env->empty()) return *env;
    if (!iniDataPath.empty()) return iniDataPath;
    if (auto p = vfs::autoDetectDataPath(exeDirectory())) return pathToUtf8(*p);
    return {};
}

EditionChoice selectEdition(const LaunchOptions& o, const save::EngineSettings& settings, const std::filesystem::path& exeDir) {
    std::optional<Edition> preferred = settings.edition;
    if (!o.edition.empty()) preferred = o.edition == "auto" ? std::nullopt : parseEditionKey(o.edition);

    std::string path;
    if (!o.dataPath.empty()) {
        path = o.dataPath;
    } else if (preferred == Edition::Dos1990) {
        path = resolveDosDataPath(settings.dosDataPath, exeDir);
    } else {
        path = resolveDataPath(o, settings.dataPath);
        if (path.empty() && !preferred) path = resolveDosDataPath(settings.dosDataPath, exeDir);
    }
    if (path.empty()) {
        if (preferred == Edition::Dos1990)
            throw StartupError("The 1990 DOS edition of Treasure Mountain! was not found.\n\n" + howToPointAtDos(),
                               std::string(kDosExeName));
        return {Edition::WinCd, {}};  // the Windows start-up reports it as before
    }
    try {
        const EditionCandidate c = detectEdition(pathFromUtf8(path), preferred);
        if (c.edition == Edition::Dos1990) return {Edition::Dos1990, pathToUtf8(c.path)};
        // Auto: exactly the path as given (the Windows start-up of before); --edition win: the
        // Windows part of a folder holding both.
        return {Edition::WinCd, preferred == Edition::WinCd ? pathToUtf8(c.path) : path};
    } catch (const EditionError& e) {
        // A DOS folder that cannot be played names its file; the two-editions message stands alone.
        if (e.file().empty()) throw StartupError(e.what());
        throw StartupError(std::format("{}\n\n{}", e.what(), howToPointAtDos()), e.file());
    }
}

dos::DosOptions resolveDosOptions(const LaunchOptions& o, const save::EngineSettings& settings,
                                  const std::filesystem::path& prefDir) {
    dos::DosOptions d = dos::dosOptionsFrom(settings.dos);
    if (!o.dosArgs.empty())
        for (const std::string& a : dos::applyDosSwitches(d, o.dosArgs)) log::warn("--dos-args: '{}' has no effect", a);
    if (auto v = dos::parseLookOption(o.dosLook)) d.look = *v;
    if (auto v = dos::parseSoundDevice(o.dosSound)) d.sound = *v;
    if (o.dosSlowCpu) d.slowCpu = true;
    if (auto v = dos::parseFrameSpeed(o.frameSpeed)) d.frameSpeed = *v;
    if (o.squarePixels) d.squarePixels = true;
    if (o.noAudio) d.sound = dos::SoundDevice::Off;
    d.headless = o.headless;
    d.keys = o.keys;
    d.prefDir = prefDir;
    d.verbose = o.verbose;
    if (isFrameDumpFolder(o.frameDump)) d.frameDumpDir = pathFromUtf8(o.frameDump);
    // DOS game diagnostics (checked by the parser).
    if (!o.dosTrace.empty()) d.tracePath = pathFromUtf8(o.dosTrace);
    for (const std::string& s : o.dosDumpStates) {
        const size_t colon = s.find(':');
        d.stateDumps.push_back({std::stoull(s.substr(0, colon)), pathFromUtf8(s.substr(colon + 1))});
    }
    if (!o.dosTicks.empty()) d.runTicks = std::stoull(o.dosTicks);
    d.preview = o.dosPreview;
    if (!o.dosLayoutSeed.empty()) {
        const std::string& s = o.dosLayoutSeed;  // checked by the parser
        const bool hex = s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
        d.layoutSeed = static_cast<uint16_t>(std::stoul(hex ? s.substr(2) : s, nullptr, hex ? 16 : 10));
    }
    return d;
}

platform::HostConfig dosHostConfig(const LaunchOptions& o, const save::EngineSettings& settings,
                                   const std::filesystem::path& screenshotDir) {
    platform::HostConfig h;
    h.layout = gfx::PresenterLayout::dos1990(settings.dos.squarePixels || o.squarePixels);
    h.title = kDosTitle;
    h.windowScale = settings.dos.windowScale;
    h.integerScale = settings.integerScale || o.integerScale;
    h.fullscreen = (settings.fullscreen || o.fullscreen) && !o.windowed;
    h.pauseOnFocusLoss = settings.pauseOnFocusLoss && !o.noPause;
    h.screenshotDir = !o.screenshotDir.empty() ? pathFromUtf8(o.screenshotDir) : screenshotDir;
    h.screenshotPrefix = "tmport_dos";
    return h;
}

// ------------------------------------------------------------------------------------------- App

struct App::Parts {
    save::Paths paths;
    save::EngineSettings settings;
    Edition edition = Edition::WinCd;
    // The 1990 DOS edition:
    std::filesystem::path dosFolder;
    dos::DosOptions dosOptions;
    std::unique_ptr<platform::Host> host;
    // The Windows CD edition:
    GameFiles files;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<audio::AudioSystem> audio;
    std::unique_ptr<text::Font> font;
    std::unique_ptr<text::TextRenderer> text;
    std::unique_ptr<ui::DialogSystem> dialogs;
    std::unique_ptr<anim::AnimEngine> anim;
    std::unique_ptr<ui::MenuBar> menu;
    std::unique_ptr<game::GameTables> tables;
    std::unique_ptr<game::GameState> state;
    std::unique_ptr<EngineGlue> glue;
};

App::App(LaunchOptions options) : options_(std::move(options)), p_(std::make_unique<Parts>()) {}

bool App::initialised() const { return ctx_ != nullptr || (p_ && p_->host != nullptr); }

Edition App::edition() const { return p_->edition; }

App::~App() {
    if (p_) {
        p_->glue.reset();  // uses ctx_
        p_->menu.reset();  // removes its engine hooks and the overlay
        p_->anim.reset();
        p_->dialogs.reset();
        p_->text.reset();
        p_->audio.reset();  // stops the voice and the music before the files go
    }
    ctx_.reset();
    p_.reset();
}

void App::init() {
    Parts& p = *p_;
    const LaunchOptions& o = options_;

    // 1. Paths and settings (tmport.ini, one-time import of the original users.dat).
    p.paths = o.prefDir.empty() ? save::Paths::detect() : save::Paths::under(pathFromUtf8(o.prefDir), exeDirectory());
    if (!o.configPath.empty()) p.paths.iniPath = pathFromUtf8(o.configPath);
    if (p.paths.prefDir.empty()) log::warn("no preference directory: saves and settings are not available");
    save::ImportResult import = save::ImportResult::AlreadyDone;
    p.settings = save::loadSettingsAndImport(p.paths, &import);
    if (import == save::ImportResult::Imported) log::info("imported the original users.dat into {}", pathToUtf8(p.paths.usersDat));

    // 2. Edition and data path (edition/edition.h). The DOS edition has its own runtime.
    const EditionChoice choice = selectEdition(o, p.settings, exeDirectory());
    if (choice.edition == Edition::Dos1990) {
        initDos(pathFromUtf8(choice.dataPath));
        return;
    }

    // 2a. Game files. The original swallowed DLL load failures; the port refuses to start.
    const std::string& dataPath = choice.dataPath;
    if (dataPath.empty())
        throw StartupError("The Treasure Mountain! game files were not found.\n\n" + howToPointAtData(p.paths));
    ResourceManager::Options ro;
    ro.colorMode = o.colour16 ? gfx::ColorMode::Colour16 : gfx::ColorMode::Colour256;
    ro.requireSounds = true;
    try {
        p.files = GameFiles::open(pathFromUtf8(dataPath), ro);
        if (o.colour16 && !p.files.res->hasDll(DllId::TMT16))
            throw MissingFileError("TMT16.DLL", "TMT16.DLL is needed for the 16-colour mode (--16)");
    } catch (const MissingFileError& e) {
        throw StartupError(std::format("A game file is missing: {}\n\n({})\n\nGame data: {}\n\n{}", e.file(), e.what(), dataPath,
                                       howToPointAtData(p.paths)),
                           e.file());
    } catch (const std::exception& e) {
        throw StartupError(std::format("The game data could not be read.\n\n{}\n\nGame data: {}\n\n{}", e.what(), dataPath,
                                       howToPointAtData(p.paths)));
    }
    log::info("game data: {}", p.files.source->describe());

    // 2b. Mods (docs/MODDING.md): tmport.ini [mods] folders, then --mods. Broken overrides are
    //     logged and fall back to the original data; they never stop the game.
    if (!o.noMods) {
        std::vector<std::filesystem::path> modDirs = mods::configuredModDirs(p.paths);
        for (const auto& d : o.modDirs) modDirs.push_back(pathFromUtf8(d));
        mods::applyMods(p.files, modDirs);
    }

    // 3. Font (FFNT 999) before any window: a missing font is a data error, too.
    try {
        p.font = std::make_unique<text::Font>(text::Font::load(*p.files.res));
    } catch (const MissingFileError& e) {
        throw StartupError(std::format("A game file is damaged or incomplete: {}\n\n({})", e.file(), e.what()), e.file());
    } catch (const std::exception& e) {
        throw StartupError(std::format("The game font (FFNT 999) could not be read: {}", e.what()));
    }

    // 4. Engine (palette RRGB 900 / VGA 16, cursors) and, in GUI mode, the window.
    EngineConfig cfg = engineConfigFromSettings(p.settings);
    if (o.integerScale) cfg.integerScale = true;
    if (o.fullscreen) cfg.fullscreen = true;
    if (o.windowed) cfg.fullscreen = false;
    if (o.noPause) cfg.pauseOnFocusLoss = false;
    if (o.tapLatch) cfg.tapLatch = true;
    if (o.debugKeys || o.showTreasure) cfg.debugKeys = true;
    if (o.showTreasure) cfg.showTreasure = true;
    if (!o.inputMode.empty()) cfg.inputMode = parseInputMode(o.inputMode, cfg.inputMode);
    cfg.screenshotDir = !o.screenshotDir.empty() ? pathFromUtf8(o.screenshotDir) : p.paths.screenshotDir;
    cfg.title = kAppTitle;
    p.engine = std::make_unique<Engine>(p.files, ro.colorMode, cfg);
    if (!o.headless && !p.engine->openWindow())
        throw StartupError("The game window could not be opened (see the log for the SDL error).");

    // 5. Audio: failures leave the game silent, they do not stop it.
    try {
        audio::AudioConfig ac;
        ac.waveDevice = !(o.headless || o.noAudio);
        ac.midiBackend = midiBackendFrom(o, p.settings);
        ac.musicSet = musicSetFrom(o, p.settings);
        // SoundFont: --soundfont, else [audio] soundfont (a relative path is relative to tmport.ini),
        // else a single .sf2 next to the executable (a ready-to-play copy, scripts/package.cmake).
        if (!o.soundFont.empty()) {
            ac.soundFontPath = o.soundFont;
        } else if (!p.settings.soundFont.empty()) {
            std::filesystem::path sf = pathFromUtf8(p.settings.soundFont);
            if (sf.is_relative() && !p.paths.iniPath.empty()) sf = p.paths.iniPath.parent_path() / sf;
            ac.soundFontPath = pathToUtf8(sf);
        } else if (auto sf = findSingleFileWithExtension(exeDirectory(), ".sf2")) {
            ac.soundFontPath = pathToUtf8(*sf);
            log::info("audio: SoundFont next to the executable: {}", ac.soundFontPath);
        }
        ac.musicVolume = static_cast<float>(p.settings.musicVolume) / 100.0f;
        p.audio = audio::AudioSystem::create(p.files, ac);
    } catch (const std::exception& e) {
        log::warn("audio: {} (the game runs without sound)", e.what());
        p.audio.reset();
    }

    // 6. Text, dialogs, animation, menu bar, game state.
    p.text = std::make_unique<text::TextRenderer>(*p.font, &p.engine->frame(), &p.engine->presenter());
    p.dialogs = std::make_unique<ui::DialogSystem>(*p.engine, *p.font);
    {
        Engine* eng = p.engine.get();
        audio::AudioSystem* a = p.audio.get();
        anim::AnimSound sound;
        if (a) {
            sound.play = [a](int n) { a->sfxPlay(n); };
            sound.stop = [a] { a->sfxStop(); };
        }
        p.anim = std::make_unique<anim::AnimEngine>(
            p.engine->frame(), [eng](int x, int y, int w, int h) { eng->presenter().blitToWindow(eng->frame(), x, y, w, h); },
            std::move(sound));
    }
    try {
        p.menu = std::make_unique<ui::MenuBar>(*p.engine, ui::MenuBarModel::fromExe(*p.files.exe), *p.font);
    } catch (const std::exception& e) {
        log::warn("menu bar: {} (no menu)", e.what());
        p.menu.reset();
    }
    try {
        // TODO(integration): the game area's GameMain may own its GameState; it can replace ctx.state.
        p.tables = std::make_unique<game::GameTables>(game::GameTables::load(*p.files.exe, *p.files.res));
        p.state = std::make_unique<game::GameState>(game::GameState::initial(*p.tables));
    } catch (const std::exception& e) {
        log::warn("game state: {}", e.what());
        p.state.reset();
    }

    // 7. The shared context and the hooks between the parts.
    ctx_ = std::make_unique<AppContext>(AppContext{*p.engine, p.files, *p.font, *p.text, *p.dialogs});
    ctx_->audio = p.audio.get();
    ctx_->anim = p.anim.get();
    ctx_->menu = p.menu.get();
    ctx_->state = p.state.get();
    ctx_->paths = &p.paths;
    ctx_->settings = &p.settings;
    registerMenuCommands(*ctx_);
    p.glue = std::make_unique<EngineGlue>(*ctx_);
    mirrorAudioOptions(*ctx_);
}

void App::initDos(const std::filesystem::path& folder) {
    Parts& p = *p_;
    const LaunchOptions& o = options_;
    p.edition = Edition::Dos1990;
    p.dosFolder = folder;
    p.dosOptions = resolveDosOptions(o, p.settings, p.paths.prefDir);
    // The DOS --keys script (game ticks, dos/app/script_input.h) and every file of the folder
    // (dos/data/dos_files.h) are checked before a window opens; the runtime opens the files again.
    if (!p.dosOptions.keys.empty()) {
        std::string error;
        if (!dos::ScriptedInput::parse(p.dosOptions.keys, error)) throw StartupError("--keys: " + error);
    }
    try {
        (void)dos::DosFiles::open(folder);
    } catch (const MissingFileError& e) {
        throw StartupError(std::format("A file of the 1990 DOS edition is missing: {}\n\n({})\n\nDOS game folder: {}\n\n{}",
                                       e.file(), e.what(), pathToUtf8(folder), howToPointAtDos()),
                           e.file());
    } catch (const std::exception& e) {
        throw StartupError(std::format("The files of the 1990 DOS edition could not be used.\n\n{}\n\nDOS game folder: {}",
                                       e.what(), pathToUtf8(folder)));
    }
    p.host = std::make_unique<platform::Host>(dosHostConfig(o, p.settings, p.paths.screenshotDir));
    log::info("game data: {} ({})", pathToUtf8(folder), editionName(Edition::Dos1990));
    if (!o.headless && !p.host->openWindow())
        throw StartupError("The game window could not be opened (see the log for the SDL error).");
}

int App::run(const GameMainFn& entry) {
    Parts& p = *p_;
    if (p.edition == Edition::Dos1990) {
        if (!p.host) throw Error("App::run before init");
        struct DosDump {
            App& app;
            ~DosDump() {
                // A frame-dump folder is filled by the DOS runtime itself.
                if (app.options_.frameDump.empty() || !app.p_->dosOptions.frameDumpDir.empty()) return;
                if (app.p_->host->writeFrameDump(pathFromUtf8(app.options_.frameDump)))
                    log::info("canvas -> {}", app.options_.frameDump);
                else
                    log::error("cannot write {}", app.options_.frameDump);
            }
        } dump{*this};
        return dos::runDosEdition(*p.host, p.dosOptions, p.dosFolder);
    }
    if (!ctx_) throw Error("App::run before init");
    std::unique_ptr<ScriptInput> script;
    if (!options_.keys.empty() || options_.headless) {
        std::string error;
        auto steps = parseScript(options_.keys, error);
        if (!steps) throw StartupError(error);
        script = std::make_unique<ScriptInput>(*p.engine, std::move(*steps), options_.headless);
        if (!p.engine->hasWindow()) {
            // Headless, the dialogs would answer Esc to an empty poll (no input can arrive without a
            // window); scripted input arrives one fetch later, so they poll the engine instead.
            Engine* eng = p.engine.get();
            p.dialogs->hooks().getInputCode = [eng] {
                const uint16_t c = eng->getInputCode();
                if (c == 0) eng->clock().sleepMs(1);
                return c;
            };
        }
    }
    int rc = 0;
    struct Dump {
        App& app;
        ~Dump() {
            if (app.options_.frameDump.empty()) return;
            const auto path = pathFromUtf8(app.options_.frameDump);
            if (app.p_->engine->presenter().writeCanvasPng(path))
                log::info("canvas -> {}", app.options_.frameDump);
            else
                log::error("cannot write {}", app.options_.frameDump);
        }
    } dump{*this};
    rc = runGameMain(*ctx_, entry ? entry : GameMainFn([](AppContext& c) { game::gameMain(c); }));
    // Shutdown (1118:03eb): menu off, music stopped; the rest is RAII.
    if (p.menu) p.menu->remove();
    if (p.audio) p.audio->music().stopScene();
    return rc;
}

// ------------------------------------------------------------------------------------- runGame

int runGame(const LaunchOptions& options, const GameMainFn& entry, const ErrorSink& sinkIn) {
    log::setVerbose(options.verbose);
    const ErrorSink sink = sinkIn ? sinkIn : defaultErrorSink(options.headless);
    App app(options);
    try {
        app.init();
    } catch (const StartupError& e) {
        sink(e);
        return 3;
    } catch (const std::exception& e) {
        sink(StartupError(e.what()));
        return 3;
    }
    try {
        return app.run(entry);
    } catch (const StartupError& e) {
        sink(e);
        return 3;
    } catch (const std::exception& e) {
        sink(StartupError(std::format("The game stopped because of an error:\n\n{}", e.what())));
        return 3;
    }
}

int runGame(int argc, char** argv, const GameMainFn& entry, const ErrorSink& sink) {
    std::string error;
    auto o = parseLaunchArgs(argc, argv, error);
    if (!o) {
        std::fprintf(stderr, "tmport: %s\n\n%s", error.c_str(), launchUsage().c_str());
        return 2;
    }
    if (!o->keys.empty()) {
        // The editions script in their own units: Windows steps per input fetch (app/script_input.h),
        // DOS key events in game ticks (dos/app/script_input.h). Before the edition is known (auto),
        // a script either one accepts passes; the start-up checks it again for the chosen edition.
        std::string dosError;
        const bool winOk = o->edition != "dos" && parseScript(o->keys, error).has_value();
        const bool dosOk = o->edition != "win" && !winOk && dos::ScriptedInput::parse(o->keys, dosError).has_value();
        if (!winOk && !dosOk) {
            std::fprintf(stderr, "tmport: %s\n", (o->edition == "dos" ? dosError : error).c_str());
            return 2;
        }
    }
    return runGame(*o, entry, sink);
}

}  // namespace tmport::app
