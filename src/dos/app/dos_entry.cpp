// Entry point of the 1990 DOS edition (dos_entry.h): the DOS folder, the saves, the sound driver and
// the DOS runtime on the host, around the game (dos/game/game.h, main_loop.h). The wave-2 screen
// preview (dos_preview.h) stays reachable with the hidden option --dos-preview.
#include "dos/app/dos_entry.h"

#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <optional>
#include <system_error>

#include "dos/app/dos_preview.h"
#include "dos/app/dos_runtime.h"
#include "dos/app/script_input.h"
#include "dos/data/dgroup_offsets.h"
#include "dos/game/game.h"
#include "dos/game/load_state.h"
#include "dos/game/main_loop.h"
#include "dos/game/sound.h"
#include "dos/game/trace.h"
#include "dos/game/ui.h"
#include "dos/save/dosbox_state.h"
#include "dos/save/users_store.h"
#include "mods/wav_codec.h"
#include "platform/host.h"
#include "rtl/borland_rand.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/log.h"
#include "util/png_writer.h"

namespace tmport::dos {

namespace {

// The roster the preview's screens show: the engine's DOS save, imported from the folder's
// USERS.DAT on the first run (dos/save/users_store.h). Empty (the composer then reads the folder's
// USERS.DAT, if any) without a preference folder or when the engine's file cannot be used.
std::vector<uint8_t> loadRoster(const DosOptions& o, const std::filesystem::path& folder) {
    if (o.prefDir.empty()) return {};
    DosUsersStore store(o.prefDir / std::filesystem::path(kDosUsersFileName));
    const DosUsersStatus status = store.loadWithImport(folder);
    if (status == DosUsersStatus::Ok || store.fromBackup()) return store.users().bytes();
    if (status != DosUsersStatus::Missing)
        log::warn("{}: {}; showing the DOS folder's roster", pathToUtf8(store.path()), describe(status));
    return {};
}

// The headless run's sound as a 16-bit mono WAV (the device's float output, full scale = 1.0).
bool writeSoundWav(const std::filesystem::path& path, const std::vector<float>& samples, int rate) {
    std::vector<uint8_t> bytes(samples.size() * 2);
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto v = static_cast<int16_t>(std::clamp(std::nearbyint(static_cast<double>(samples[i]) * 32767.0), -32768.0, 32767.0));
        bytes[2 * i] = static_cast<uint8_t>(v & 0xFF);
        bytes[2 * i + 1] = static_cast<uint8_t>((static_cast<uint16_t>(v) >> 8) & 0xFF);
    }
    return writeWholeFile(path, mods::makeWav(static_cast<uint32_t>(rate), 1, 16, bytes));
}

std::optional<ScriptedInput> parseScript(const std::string& keys) {
    std::string error;
    auto script = ScriptedInput::parse(keys, error);
    if (!script) throw Error("--keys: " + error);
    return script;
}

// The host's SDL events for the PC keyboard and the mouse driver (both editions of the DOS runtime).
platform::HostHandlers dosInputHandlers(platform::Host& host, DosRuntime& runtime, DosAudioDevice* audio, bool& resync) {
    auto mouseAt = [&host, &runtime](float wx, float wy) {
        float cx = 0.0f, cy = 0.0f;
        if (host.windowToCanvas(wx, wy, cx, cy))
            runtime.mouse().setLogicalPosition(static_cast<int>(std::floor(cx)), static_cast<int>(std::floor(cy)));
    };
    platform::HostHandlers h;
    h.input = [&runtime, mouseAt](const SDL_Event& e) {
        switch (e.type) {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                runtime.hostKey(static_cast<uint32_t>(e.key.scancode), e.type == SDL_EVENT_KEY_DOWN, e.key.repeat);
                break;
            case SDL_EVENT_MOUSE_MOTION: mouseAt(e.motion.x, e.motion.y); break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                mouseAt(e.button.x, e.button.y);
                const uint16_t b = e.button.button == SDL_BUTTON_LEFT     ? MouseDriver::kLeft
                                   : e.button.button == SDL_BUTTON_RIGHT  ? MouseDriver::kRight
                                   : e.button.button == SDL_BUTTON_MIDDLE ? MouseDriver::kMiddle
                                                                          : uint16_t{0};
                if (b) runtime.mouse().setButton(b, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                break;
            }
            default: break;
        }
    };
    h.window = [&runtime](const SDL_Event& e) {
        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) runtime.releaseAllKeys();
    };
    h.activation = [&runtime, audio, &resync](bool active) {
        // Frozen (focus loss, minimised): keys up, sound muted; the time spent frozen is skipped.
        if (!active) runtime.releaseAllKeys();
        if (audio) audio->setOutputEnabled(active);
        resync = true;
    };
    return h;
}

// Host time drives the virtual PIT (DosRuntime::advance, at most 4 game ticks per host frame after
// a stall) until the game quits or the window closes. `afterAdvance` runs after each advance.
template <class F>
void interactiveLoop(platform::Host& host, DosRuntime& runtime, bool& resync, F&& afterAdvance) {
    uint64_t last = host.nowNs();
    while (runtime.running() && !host.quitRequested()) {
        host.pump();
        if (host.quitRequested()) break;
        const uint64_t now = host.nowNs();
        if (resync) {
            last = now;
            resync = false;
        }
        runtime.advance(now - last);
        last = now;
        afterAdvance();
        host.renderIfDue();
        host.clock().sleepMs(1);
    }
}

// ---- the wave-2 screen preview (--dos-preview) ---------------------------------------------------

int runPreview(platform::Host& host, const DosOptions& options, const std::filesystem::path& folder) {
    log::info("DOS edition in {} (look {}, sound {}{}, frame speed {}): preview of the screens and sound",
              pathToUtf8(folder), lookName(options.look), iniValue(options.sound), options.slowCpu ? ", slow CPU" : "",
              iniValue(options.frameSpeed));
    const DosGfxFiles files = DosGfxFiles::open(folder);

    RuntimeOptions ro;
    ro.frameSpeed = options.frameSpeed;
    // The host keyboard is a PC keyboard, also with the Tandy look: KeyboardOptions::tandy stays off.
    DosRuntime runtime(ro);

    // One Turbo C rand() for the whole game, unseeded (PORTING_NOTES §2.4); the AdLib driver draws
    // from it in Music_Play.
    BorlandRand rand;
    const bool offline = options.headless || !host.hasWindow();
    std::unique_ptr<DosAudioDevice> audio = makeDosAudioDevice(files.files(), options.sound, [&rand] { return rand.rand(); });
    if (audio && !offline && !audio->open()) audio.reset();  // no device: no sound (open() logged why)

    DosPreview preview(files, options, runtime, loadRoster(options, folder), audio.get(), offline);
    runtime.setClient(&preview);

    const std::filesystem::path& dumpDir = options.frameDumpDir;
    if (!dumpDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dumpDir, ec);
        preview.keepOfflineSamples(offline && audio);
    }
    size_t dumped = 0;
    preview.setFrameSink([&](DosScreen s, std::span<const uint32_t> rgba) {
        host.presenter().presentFrame(rgba);
        if (dumpDir.empty()) return;
        const auto path = dumpDir / std::format("{:02}_{}_{}.png", dumped++, dosScreenName(s), lookName(options.look));
        if (!host.writeFrameDump(path)) log::error("cannot write {}", pathToUtf8(path));
    });
    preview.start();

    if (offline) {
        // Headless: the --keys script (game ticks) drives the run, which ends when it is used up and
        // the keys it left in the BIOS buffer are read (one per game tick).
        runtime.setScript(std::move(*parseScript(options.keys)));
        while (runtime.running() && !runtime.script().finished()) runtime.stepGameTicks(1);
        for (int i = 0; i < BiosKeyboard::kCapacity && runtime.running() && runtime.keyboard().bios().size() > 0; ++i)
            runtime.stepGameTicks(1);
        log::info("DOS preview: {} game ticks, {} screens shown", runtime.gameTicks(), preview.screensShown());
        if (!dumpDir.empty() && audio) {
            const auto wav = dumpDir / "sound.wav";
            if (!writeSoundWav(wav, preview.offlineSamples(), audio->sampleRate())) log::error("cannot write {}", pathToUtf8(wav));
        }
        return 0;
    }

    bool resync = false;
    host.setHandlers(dosInputHandlers(host, runtime, audio.get(), resync));
    // A --keys script also drives a windowed run (on top of the player's keys), so the interactive
    // loop can be smoke-tested, e.g. on SDL's offscreen video driver.
    if (!options.keys.empty()) runtime.setScript(std::move(*parseScript(options.keys)));
    interactiveLoop(host, runtime, resync, [] {});
    host.setHandlers({});
    if (audio) audio->close();
    return 0;
}

// ---- the game ------------------------------------------------------------------------------------

// The state's USERS.DAT copy (<name>.users next to the .sav) becomes the preference folder's DOS
// save, so the next sign-in reads what DOSBox-X had on disk. A replaced file is kept as .bak.
void useStateUsersFile(const DosboxState& state, const std::filesystem::path& usersPath) {
    const auto copy = DosboxState::usersFileOf(state.path());
    std::error_code ec;
    if (!std::filesystem::is_regular_file(copy, ec)) {
        log::warn("{} not found: the DOS save of the preference folder stays as it is", pathToUtf8(copy));
        return;
    }
    const DosImportResult r = importDosUsers(copy, usersPath, true);
    if (r != DosImportResult::Imported) log::warn("{} -> {}: {}", pathToUtf8(copy), pathToUtf8(usersPath), describe(r));
}

void logLoadedState(const LoadedState& s) {
    log::info("state loaded: CPU at {}{}{}, g_ticks {} (the next game tick at {}), PAN {} (before it {}), bank {}, {} dirty "
              "spans{}{}{}",
              s.cpu.empty() ? "?" : s.cpu, s.insideTimerBeforeCountdown ? " (timer interrupt, before its countdown)" : "",
              s.insideGameTick ? " (inside Game_Tick: the engine starts with the next tick)" : "", s.ticks, s.firstTickTicks,
              s.pan.empty() ? "?" : s.pan, s.prevPan.empty() ? "-" : s.prevPan,
              s.bank.empty() ? "?" : s.bank, s.dirtySpans,
              s.midFrame ? std::format(", saved inside a frame ({} sprite backgrounds put back)", s.spritesRestored) : "",
              s.restartedSfx >= 0 ? std::format(", effect {} goes on", s.restartedSfx) : "",
              s.restartedTune >= 0 ? std::format(", tune {} goes on", s.restartedTune) : "");
    for (const std::string& w : s.warnings) log::warn("state: {}", w);
}

// --frame-dump as a folder: the last frame of each screen the game showed (the title pages, the
// sign-in, play in each scene, each UI mode), so the settled pictures can be compared with the
// reference captures, and the final frame. Files: NNN_tTICK_<what>_<look>.png.
class ScreenDumper {
public:
    ScreenDumper(std::filesystem::path dir, Look look) : dir_(std::move(dir)), look_(look) {
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
    }

    // After the frame of game tick `tick`; `rgba` is the screen now shown, the copy `shownVersion` of
    // DosGame::screenVersion.
    void afterFrame(const DosGame& g, uint64_t tick, uint64_t shownVersion, std::span<const uint32_t> rgba) {
        const Phase now = phaseOf(g);
        if (have_ && now != phase_) write(tick_, phase_, rgba_);
        if (!have_ || now != phase_ || shownVersion != version_) {
            rgba_.assign(rgba.begin(), rgba.end());
            version_ = shownVersion;
        }
        phase_ = now;
        tick_ = tick;
        have_ = true;
    }
    void finish(const DosGame& g, uint64_t tick, std::span<const uint32_t> rgba) {
        write(tick, phaseOf(g), std::vector<uint32_t>(rgba.begin(), rgba.end()), "end");
    }
    size_t written() const { return n_; }

private:
    struct Phase {
        bool title = false;
        uint16_t titlePhase = 0;
        uint8_t uiMode = 0;
        uint16_t scene = 0;
        uint8_t screen = 0;
        bool operator==(const Phase&) const = default;
    };
    static Phase phaseOf(const DosGame& g) {
        Phase p;
        p.title = g.titleRunning;
        p.titlePhase = g.titleRunning ? g.ui.titlePhase : 0;
        p.uiMode = g.core.uiMode;
        p.scene = g.world.loadedScene;
        p.screen = g.ui.screen;
        return p;
    }
    static std::string label(const Phase& p) {
        if (p.title) return std::format("title{}", p.titlePhase);
        if (p.uiMode & 0x08) return "signin";
        if (p.uiMode == 0) return std::format("play_s{}", p.scene);
        return std::format("ui{:02x}_s{}", p.uiMode, p.scene);
    }
    void write(uint64_t tick, const Phase& p, const std::vector<uint32_t>& rgba, std::string_view suffix = {}) {
        if (rgba.size() != static_cast<size_t>(BackBuffer::kWidth * BackBuffer::kHeight)) return;
        std::string name = std::format("{:03}_t{:06}_{}", n_++, tick, label(p));
        if (!suffix.empty()) name += std::format("_{}", suffix);
        const auto path = dir_ / std::format("{}_{}.png", name, lookName(look_));
        const auto* bytes = reinterpret_cast<const uint8_t*>(rgba.data());
        if (!png::writeRgba(path, BackBuffer::kWidth, BackBuffer::kHeight, std::span<const uint8_t>(bytes, rgba.size() * 4)))
            log::error("cannot write {}", pathToUtf8(path));
    }

    std::filesystem::path dir_;
    Look look_;
    bool have_ = false;
    Phase phase_;
    uint64_t tick_ = 0;
    uint64_t version_ = ~0ull;
    std::vector<uint32_t> rgba_;
    size_t n_ = 0;
};

int runGame(platform::Host& host, const DosOptions& launchOptions, const std::filesystem::path& folder) {
    // --dos-load-state: the DOSBox-X state decides the machine (its look unless one was given, and
    // its sound device, which the game's rand() sequence depends on: AdLib draws one per tune).
    std::optional<DosboxState> state;
    DosOptions options = launchOptions;
    if (!options.loadState.empty()) {
        state = DosboxState::load(options.loadState);
        if (!options.lookFromCommandLine) options.look = stateLook(*state);
        log::info("DOSBox-X state {} ({}, g_videoType {}, g_soundDevice {})", pathToUtf8(options.loadState),
                  state->machineName(), static_cast<int16_t>(state->dsWord(dg::kVideoType)), state->dsWord(dg::kSoundDevice));
    }
    log::info("DOS edition in {} (look {}, sound {}{}, frame speed {})", pathToUtf8(folder), lookName(options.look),
              iniValue(options.sound), options.slowCpu ? ", slow CPU" : "", iniValue(options.frameSpeed));
    const DosGfxFiles files = DosGfxFiles::open(folder);

    RuntimeOptions ro;
    ro.frameSpeed = options.frameSpeed;
    // The host keyboard is a PC keyboard, also with the Tandy look: KeyboardOptions::tandy stays off.
    ro.keyboard.greyKeyE0Artefact = options.greyKeyArtefact;
    DosRuntime runtime(ro);

    // The saves: the engine's DOS file in the preference folder, imported from the DOS folder's
    // USERS.DAT on the first run (PORTING_NOTES §5).
    std::filesystem::path usersPath;
    if (!options.prefDir.empty()) usersPath = options.prefDir / std::filesystem::path(kDosUsersFileName);
    DosUsersStore users(usersPath);
    if (usersPath.empty()) {
        log::warn("no preference folder: the DOS edition's players and saved games are not kept");
    } else {
        if (state) useStateUsersFile(*state, usersPath);
        const DosUsersStatus status = users.loadWithImport(folder);  // logs an import
        if (status != DosUsersStatus::Ok && status != DosUsersStatus::Missing)
            log::warn("{}: {}{}", pathToUtf8(usersPath), describe(status), users.fromBackup() ? " (the backup is used)" : "");
    }

    // The sound driver of the chosen device. Without sound, or without an audio device, it still runs
    // (silently): the game reads the effect status DS:6448 (Cell_P70_TreasureRoom waits for an effect
    // to end), so effect lengths must stay those of the original. The AdLib driver draws one rand()
    // per Music_Play from the game's generator (PORTING_NOTES §2.4).
    DosGame* game = nullptr;
    const bool offline = options.headless || !host.hasWindow();
    const bool silent = options.sound == SoundDevice::Off;
    SoundDevice device = silent ? SoundDevice::Tandy : options.sound;
    if (state) {
        device = stateSoundDevice(*state);  // also when silent: the device is part of the game's state
        if (!silent && device != options.sound)
            log::info("sound device {}: the state's (the sound option does not apply to a loaded state)", iniValue(device));
    }
    std::unique_ptr<DosAudioDevice> audio = makeDosAudioDevice(files.files(), device,
                                                               [&game] { return game ? game->rand.rand() : 0; });
    if (!audio) throw Error("the DOS sound driver could not be created");
    bool audioOffline = offline || silent;
    if (!audioOffline && !audio->open()) {
        audioOffline = true;  // open() logged why
        log::warn("no audio device: the game runs without sound");
    }
    DosAudioSound driver(*audio, audioOffline);
    // A long frame's sound calls reach the driver where the original's frame made them (wave 5).
    driver.setDelaySource([&runtime] { return runtime.soundDelay(); });
    const bool keepWav = offline && !silent && !options.frameDumpDir.empty();
    driver.keepSamples(keepWav);

    // Diagnostics: the per-tick trace wraps the sound facade and the runtime client.
    std::ofstream traceFile;
    std::unique_ptr<TraceSound> traceSound;
    if (!options.tracePath.empty()) {
        traceFile.open(options.tracePath, std::ios::binary | std::ios::trunc);
        if (!traceFile) throw Error(std::format("--dos-trace: cannot write {}", pathToUtf8(options.tracePath)));
        traceSound = std::make_unique<TraceSound>(driver);
    }
    SoundFacade& sound = traceSound ? static_cast<SoundFacade&>(*traceSound) : driver;

    DosGame g(files, runtime, sound, users, options);
    game = &g;
    g.layoutSeedOverride = options.layoutSeed;  // --dos-layout-seed (reproducible new games)
    syncSoundFlags(g);  // the driver starts with the EXE's flags (sound and music on, gate open)
    std::unique_ptr<DosTrace> trace;
    if (state) {
        // The runtime's g_ticks go on from the state's (DosRuntime::resumeAt): the trace's ticks
        // column and the DGROUP dumps carry the original's g_ticks.
        runtime.setClient(&g);
        const LoadedState loaded = loadDosboxState(g, *state);
        logLoadedState(loaded);
        // Created after the load, so the first row's events are what the first tick changed.
        if (traceSound) trace = std::make_unique<DosTrace>(g, traceFile, traceSound.get());
        runtime.setClient(trace ? static_cast<RuntimeClient*>(trace.get()) : &g);
    } else {
        if (traceSound) trace = std::make_unique<DosTrace>(g, traceFile, traceSound.get());
        runtime.setClient(trace ? static_cast<RuntimeClient*>(trace.get()) : &g);
        mainStart(g);
    }

    // Presentation: the screen buffer through the look's palette whenever Frame_Present changed it. At
    // the reference frame speed a long frame's picture appears when the frame ends, as DOSBox-X
    // presents it at the end of its frame (a bubble, a scene load, a title page): until the last game
    // tick before that end the previous picture stays (the frames in between run no frame body, so
    // `screen` keeps the new picture meanwhile).
    std::vector<uint32_t> rgba(static_cast<size_t>(BackBuffer::kWidth * BackBuffer::kHeight));
    uint64_t shownVersion = ~0ull;
    auto present = [&] {
        if (g.screenVersion == shownVersion) return;
        const uint64_t shownAt = std::max(runtime.frameEndUnits(), g.screenShownAt);
        const uint64_t nextTick = (runtime.pitTicks() / kPitTicksPerGameTick + 1) * kPitTicksPerGameTick * FrameTiming::kMilli;
        if (runtime.longFrames() && shownVersion != ~0ull && shownAt > nextTick) return;
        shownVersion = g.screenVersion;
        toRgba32(g.screen, g.palette, rgba);
        host.presenter().presentFrame(rgba);
    };
    present();

    std::unique_ptr<ScreenDumper> dumper;
    if (!options.frameDumpDir.empty()) dumper = std::make_unique<ScreenDumper>(options.frameDumpDir, options.look);
    std::vector<bool> dumped(options.stateDumps.size(), false);
    auto afterTicks = [&] {
        present();
        const uint64_t tick = runtime.gameTicks();
        if (dumper) dumper->afterFrame(g, tick, shownVersion, rgba);
        for (size_t i = 0; i < options.stateDumps.size(); ++i) {
            const auto& d = options.stateDumps[i];
            if (dumped[i] || tick < d.tick) continue;
            dumped[i] = true;
            if (writeDgroupDump(g, d.file)) log::info("DGROUP after game tick {} -> {}", tick, pathToUtf8(d.file));
            else log::error("cannot write {}", pathToUtf8(d.file));
            // ... and the picture shown after that tick (<file>.png), to compare with a capture.
            auto shot = d.file;
            shot += ".png";
            const auto* bytes = reinterpret_cast<const uint8_t*>(rgba.data());
            if (!png::writeRgba(shot, BackBuffer::kWidth, BackBuffer::kHeight, std::span<const uint8_t>(bytes, rgba.size() * 4)))
                log::error("cannot write {}", pathToUtf8(shot));
        }
    };
    // From a state, tick 0 is the loaded moment: --dos-dump-state 0:<file> and the frame dump see the
    // picture DOSBox-X showed (the state's video memory).
    if (state) afterTicks();

    if (offline) {
        // Headless: the --keys script (game ticks) drives the run, which lasts until the script is used
        // up (and at least --dos-ticks, and until the last --dos-dump-state), the keys it left in the
        // BIOS buffer are read (one per game tick), or the game quits.
        runtime.setScript(std::move(*parseScript(options.keys)));
        uint64_t minTicks = options.runTicks;
        for (const auto& d : options.stateDumps) minTicks = std::max(minTicks, d.tick);
        while (runtime.running() && (!runtime.script().finished() || runtime.gameTicks() < minTicks)) {
            runtime.stepGameTicks(1);
            afterTicks();
        }
        for (int i = 0; i < BiosKeyboard::kCapacity && runtime.running() && runtime.keyboard().bios().size() > 0; ++i) {
            runtime.stepGameTicks(1);
            afterTicks();
        }
        // A Button_Flash or a dissolve the last keys started runs to its end, so the confirm handler
        // runs (a script ending with Esc, Y saves and quits). Both last well under 100 game ticks.
        for (int i = 0; i < 100 && runtime.running() && (uiFlashPending(g) || g.dissolveRunning); ++i) {
            runtime.stepGameTicks(1);
            afterTicks();
        }
    } else {
        bool resync = false;
        host.setHandlers(dosInputHandlers(host, runtime, audio.get(), resync));
        // A --keys script also drives a windowed run (on top of the player's keys), so the interactive
        // loop can be smoke-tested, e.g. on SDL's offscreen video driver.
        if (!options.keys.empty()) runtime.setScript(std::move(*parseScript(options.keys)));
        interactiveLoop(host, runtime, resync, afterTicks);
        host.setHandlers({});
    }

    // main 130f:00D1: the loop ended because g_quit was set.
    if (!runtime.running()) gameShutdown(g);
    if (dumper) dumper->finish(g, runtime.gameTicks(), rgba);
    log::info("DOS game: {} game ticks, {} frames; g_uiMode {:#04x}, level {}, scene {}{}", runtime.gameTicks(), runtime.frames(),
              g.core.uiMode, g.core.level, g.world.loadedScene, runtime.running() ? "" : ", quit");
    trace.reset();
    if (keepWav) {
        const auto wav = options.frameDumpDir / "sound.wav";
        if (!writeSoundWav(wav, driver.samples(), audio->sampleRate())) log::error("cannot write {}", pathToUtf8(wav));
    }
    if (!audioOffline) audio->close();
    runtime.setClient(nullptr);
    return 0;
}

}  // namespace

int runDosEdition(platform::Host& host, const DosOptions& options, const std::filesystem::path& folder) {
    return options.preview ? runPreview(host, options, folder) : runGame(host, options, folder);
}

}  // namespace tmport::dos
