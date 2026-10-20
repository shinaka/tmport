#include "gg/app/gg_app.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_messagebox.h>

#include <cstdio>
#include <ctime>
#include <format>
#include <memory>
#include <optional>
#include <system_error>
#include <vector>

#include "gg/app/icon.h"
#include "gg/app/test_pattern.h"
#include "gg/data/cli_args.h"
#include "mods/png_codec.h"
#include "platform/host.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/hash.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

// With a script the run lasts this many PIT ticks past its last event, so the game polls what the
// last event did (a release, a key) before the run ends.
constexpr uint64_t kScriptGraceTicks = 32;

// --frame-dump: an existing folder or a name without an extension is a folder.
bool isFrameDumpFolder(const std::string& frameDump) {
    if (frameDump.empty()) return false;
    const std::filesystem::path p = pathFromUtf8(frameDump);
    std::error_code ec;
    return std::filesystem::is_directory(p, ec) || !p.has_extension();
}

bool writeRgbaPng(const std::filesystem::path& path, std::span<const uint32_t> rgba) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(rgba.data());
    const auto png = mods::encodePngRgba(GgRuntime::kWidth, GgRuntime::kHeight, std::span<const uint8_t>(bytes, rgba.size() * 4));
    return writeWholeFile(path, png);
}

// --frame-dump DIR: every picture that differs from the one before, NNNNN_tTICKS.png (TICKS = the
// PIT interrupts when it was shown), then end.png.
class FrameDumper {
public:
    explicit FrameDumper(std::filesystem::path dir) : dir_(std::move(dir)) {
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
    }
    void frame(uint64_t interrupt, std::span<const uint32_t> rgba) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(rgba.data());
        const uint64_t h = fnv1a64(std::span<const uint8_t>(bytes, rgba.size() * 4));
        if (have_ && h == last_) return;
        have_ = true;
        last_ = h;
        const auto path = dir_ / std::format("{:05}_t{:07}.png", n_++, interrupt);
        if (!writeRgbaPng(path, rgba)) log::error("cannot write {}", pathToUtf8(path));
    }
    size_t written() const { return n_; }
    const std::filesystem::path& dir() const { return dir_; }

private:
    std::filesystem::path dir_;
    bool have_ = false;
    uint64_t last_ = 0;
    size_t n_ = 0;
};

// A message box for the player, not in automated runs (headless, or SDL's offscreen / dummy video
// drivers, where nobody could close it).
void messageBox(bool headless, const std::string& message) {
    if (headless) return;
    if (const char* d = SDL_GetHint(SDL_HINT_VIDEO_DRIVER); d && (iequalsAscii(d, "offscreen") || iequalsAscii(d, "dummy")))
        return;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kGgWindowTitle, message.c_str(), nullptr);
}

// A start-up error: stderr, and a message box when there is a desktop to show it on.
int startupError(bool headless, const std::string& message) {
    std::fprintf(stderr, "ggport: %s\n", message.c_str());
    std::fflush(stderr);
    log::error("{}", message);
    messageBox(headless, message);
    return exit_code::kStartup;
}

std::optional<dos::ScriptedInput> parseScripts(const GgOptions& o, std::string& error) {
    dos::ScriptedInput all;
    for (const auto& [name, text] : {std::pair<std::string_view, const std::string&>{"--keys", o.keys}, {"--mouse", o.mouse}}) {
        if (text.empty()) continue;
        std::string e;
        const auto s = dos::ScriptedInput::parse(text, e);
        if (!s) {
            error = std::format("{}: {}", name, e);
            return std::nullopt;
        }
        for (const dos::ScriptEvent& ev : s->events()) all.add(ev);
    }
    return all;
}

}  // namespace

int runGgApp(std::span<const std::string> args, const GgEntry& entryIn) {
    std::string error;
    const auto parsed = parseGgOptions(args, error);
    if (!parsed) {
        std::fprintf(stderr, "ggport: %s\n\n%s", error.c_str(), ggUsage().c_str());
        return exit_code::kStartup;
    }
    GgLaunch launch;
    launch.options = *parsed;
    const GgOptions& o = launch.options;
    if (o.help) {
        std::fputs(ggUsage().c_str(), stdout);
        return exit_code::kNormal;
    }
    log::setVerbose(o.verbose);
    launch.headless = o.headless;

    // Init_CommandLine 303a:005c step 3, the original's switches.
    launch.switches = parseOriginalSwitches(splitArgs(o.ggArgs));
    if (launch.switches.usage) {
        if (!launch.switches.bad.empty()) {
            std::fprintf(stderr, "ggport: --gg-args: unknown switch '%s'\n\n%s", launch.switches.bad.c_str(), ggUsage().c_str());
            return exit_code::kStartup;
        }
        std::fputs(ggUsage().c_str(), stdout);  // -?
        return exit_code::kNormal;
    }

    // The preference folder and ggport.ini.
    if (!o.prefDir.empty()) {
        launch.prefDir = pathFromUtf8(o.prefDir);
    } else if (char* p = SDL_GetPrefPath("GGPort", "GGPort")) {
        launch.prefDir = pathFromUtf8(p);
        SDL_free(p);
    } else {
        log::warn("no preference folder ({}): players and settings are not kept", SDL_GetError());
    }
    if (!launch.prefDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(launch.prefDir, ec);
        const auto ini = launch.prefDir / GgSettings::kFileName;
        bool existed = false;
        launch.settings = GgSettings::load(ini, &existed);
        if (!existed && !launch.settings.save(ini)) log::warn("cannot write {}", pathToUtf8(ini));
    }
    const GgSettings& settings = launch.settings;

    // The game folder.
    std::optional<std::filesystem::path> explicitPath;
    if (!o.data.empty()) explicitPath = pathFromUtf8(o.data);
    else if (!settings.data.empty()) explicitPath = pathFromUtf8(settings.data);
    const auto found = detectGgFolder(explicitPath, cli::programDir());
    if (!found)
        return startupError(launch.headless,
                            "No Gizmos & Gadgets folder was found. Give the folder with --data DIR (or data = in "
                            "ggport.ini, or the GG_DATA environment variable).");
    launch.folder = found->path;
    std::optional<GgFiles> files;
    try {
        files.emplace(GgFiles::open(launch.folder));
    } catch (const MissingFileError& e) {
        return startupError(launch.headless, std::format("A file of Gizmos & Gadgets is missing: {}\n\n({})\n\nGame folder: {}",
                                                         e.file(), e.what(), pathToUtf8(launch.folder)));
    } catch (const std::exception& e) {
        return startupError(launch.headless, std::format("The files of Gizmos & Gadgets could not be used.\n\n{}\n\nGame folder: {}",
                                                         e.what(), pathToUtf8(launch.folder)));
    }

    // Sound and CPU: ggport.ini, then the original's switches, then the engine options.
    launch.sound = settings.sound;
    if (launch.switches.soundDevice != -1) launch.sound = soundForDevice(launch.switches.soundDevice);
    if (o.sound) launch.sound = *o.sound;
    launch.cpu = o.cpu.value_or(settings.cpu);
    // 303a:0229: DS:2454 = (u16) time(NULL); srand(DS:2454).
    launch.seedFromClock = !o.seed.has_value();
    launch.seed = o.seed ? *o.seed : static_cast<uint16_t>(std::time(nullptr) & 0xFFFF);

    // The host: the DOS screen at 4:3.
    platform::HostConfig hc;
    hc.layout = gfx::PresenterLayout::dos1990(o.squarePixels || settings.squarePixels);
    hc.title = kGgWindowTitle;
    hc.windowScale = o.scale.value_or(settings.windowScale);
    hc.integerScale = o.integerScale || settings.integerScale;
    hc.fullscreen = (o.fullscreen || settings.fullscreen) && !o.windowed;
    hc.pauseOnFocusLoss = settings.pauseOnFocusLoss && !o.noPause;
    if (!o.screenshotDir.empty()) hc.screenshotDir = pathFromUtf8(o.screenshotDir);
    else if (!launch.prefDir.empty()) hc.screenshotDir = launch.prefDir / "screenshots";
    hc.screenshotPrefix = "ggport";
    platform::Host host(hc);
    if (!launch.headless) {
        if (const auto ico = files->pathOf("ssg.ico")) {
            if (auto icon = loadIcoFile(*ico)) {
                host.setWindowIconHook([img = std::move(*icon)](SDL_Window* w) {
                    if (!setWindowIcon(w, img)) log::debug("the window icon could not be set");
                });
            }
        }
        if (!host.openWindow()) return startupError(false, "The game window could not be opened (see the log for the SDL error).");
    }

    // The runtime.
    RuntimeConfig rc;
    rc.deterministic = launch.headless;
    rc.keyboard.typematicDelayMs = settings.typematicDelayMs;
    rc.keyboard.typematicRateMs = settings.typematicRateMs;
    GgRuntime runtime(rc, &host);
    const auto script = parseScripts(o, error);
    if (!script) return startupError(launch.headless, error);
    const bool hasScript = !script->events().empty();
    runtime.setScript(*script);
    if (launch.headless) {
        uint64_t last = o.ticks.value_or(0);
        if (hasScript) last = std::max(last, runtime.scriptEndTick() + kScriptGraceTicks);
        runtime.setRunLimit(last);
    } else if (o.ticks) {
        runtime.setRunLimit(*o.ticks);
    }
    std::unique_ptr<FrameDumper> dumper;
    if (isFrameDumpFolder(o.frameDump)) {
        dumper = std::make_unique<FrameDumper>(pathFromUtf8(o.frameDump));
        runtime.setFrameSink([&dumper](uint64_t i, std::span<const uint32_t> rgba) { dumper->frame(i, rgba); });
    }

    BorlandRand rand;
    rand.srand(launch.seed);
    ExitHandlers atExit;
    // 1454:0000 Input_InstallHandlers (Init_CommandLine step 8).
    runtime.installHandlers();

    log::info("Gizmos & Gadgets in {} ({}); sound {}, cpu {}, seed {}{}{}", pathToUtf8(launch.folder), describe(found->source),
              soundModeName(launch.sound), cpuName(launch.cpu), launch.seed, launch.seedFromClock ? " (clock)" : "",
              launch.headless ? ", headless" : "");

    GgContext ctx{*files, runtime, host, launch, rand, atExit};
    const GgEntry entry = entryIn ? entryIn : GgEntry([](GgContext& c) { return testPatternMain(c); });
    int code = exit_code::kNormal;
    try {
        code = entry(ctx);
    } catch (const GgExit& e) {
        code = e.code;
    } catch (const GgRunEnd& e) {
        log::info("run ended: {} after {} PIT ticks", e.what(), runtime.clock().interrupts());
    } catch (const std::exception& e) {
        log::error("the game stopped because of an error: {}", e.what());
        messageBox(launch.headless, std::format("The game stopped because of an error:\n\n{}", e.what()));
        code = exit_code::kAbort;
    }
    // exit(): the atexit functions (Game_Shutdown), then Input_RemoveHandlers.
    atExit.run();
    runtime.removeHandlers();

    // --frame-dump
    if (!o.frameDump.empty()) {
        const auto& last = runtime.frame();
        if (last.empty()) {
            log::warn("--frame-dump: no picture was shown");
        } else if (dumper) {
            const auto end = dumper->dir() / "end.png";
            if (!writeRgbaPng(end, last)) log::error("cannot write {}", pathToUtf8(end));
            log::info("--frame-dump: {} pictures and end.png in {}", dumper->written(), pathToUtf8(dumper->dir()));
        } else {
            const auto path = pathFromUtf8(o.frameDump);
            if (writeRgbaPng(path, last)) log::info("picture -> {}", o.frameDump);
            else log::error("cannot write {}", o.frameDump);
        }
    }
    log::info("ggport: {} PIT ticks, {} pictures, exit code {}", runtime.clock().interrupts(), runtime.framesShown(), code);
    return code;
}

}  // namespace tmport::gg
