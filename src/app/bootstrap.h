#pragma once
// Start-up of the game (the port's WinMain, re/docs/ARCHITECTURE.md §2.3, S1-S4):
//   paths + settings (save::Paths, tmport.ini, one-time users.dat import)
//   -> data path and edition (selectEdition: --data, TM_DATA, tmport.ini [engine] data,
//      auto-detection; edition/edition.h decides between the Windows CD and the 1990 DOS edition)
//   -> 1990 DOS edition: a platform::Host in the DOS layout, then dos::runDosEdition (dos/app/dos_entry.h)
//   -> Windows CD edition: GameFiles (refuse to start with a clear message naming a missing file; the original
//      silently swallowed DLL load failures)
//   -> Engine (+ window in GUI mode), palette (RRGB 900 / VGA 16), cursors
//   -> AudioSystem (music set and MIDI back end from the settings; failures are not fatal)
//   -> Font (FFNT 999), TextRenderer, DialogSystem, AnimEngine, MenuBar (+ command handlers),
//      GameState -> AppContext, EngineGlue (pump/activation/Ctrl+S/dialog hooks)
//   -> the game entry (GameMainFn), with QuitGame / RestartToSignIn handling.

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "app/app_context.h"
#include "app/game_main.h"
#include "app/launch_options.h"
#include "edition/dos_options.h"
#include "edition/edition.h"
#include "mischief/platform/host.h"
#include "mischief/util/error.h"

namespace tmport::app {

// A start-up failure with the message for the player. file(): the missing game file, if any.
class StartupError : public mischief::Error {
public:
    explicit StartupError(const std::string& message, std::string file = {}) : mischief::Error(message), file_(std::move(file)) {}
    const std::string& file() const { return file_; }

private:
    std::string file_;
};

// Shows a start-up error: stderr always; an SDL message box unless headless.
using ErrorSink = std::function<void(const StartupError& e)>;
ErrorSink defaultErrorSink(bool headless);

class App {
public:
    explicit App(LaunchOptions options);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Builds everything (see the file comment). Throws StartupError. No window is opened when
    // options.headless is set or when the data files fail.
    void init();
    bool initialised() const;
    Edition edition() const;
    // Windows CD edition only (the DOS edition has no AppContext).
    AppContext& context() { return *ctx_; }
    const LaunchOptions& options() const { return options_; }
    // Runs the entry (default: game::gameMain, game/game_main.h) with the scripted input of --keys and
    // writes --frame-dump at the end. Returns the exit code. The DOS edition runs
    // dos::runDosEdition instead (entry is not used).
    int run(const GameMainFn& entry);

private:
    void initDos(const std::filesystem::path& folder);

    struct Parts;
    LaunchOptions options_;
    std::unique_ptr<Parts> p_;
    std::unique_ptr<AppContext> ctx_;
};

// The data path chosen from --data, TM_DATA, tmport.ini [engine] data and auto-detection (empty
// when nothing was found). The Windows CD edition's chain.
std::string resolveDataPath(const LaunchOptions& o, const std::string& iniDataPath);

// The edition to play and its data path (edition/edition.h, SCOPING §5.1). The preference is
// --edition, else [engine] edition. The path:
//   auto:     --data, TM_DATA, [engine] data, Windows auto-detection, then TM_DATA_DOS, [dos] data,
//             DOS auto-detection (a folder with SST.EXE next to the executable or the current
//             directory). The Windows edition stays the default while the DOS one is being built.
//   win:      --data, TM_DATA, [engine] data, Windows auto-detection.
//   dos:      --data, TM_DATA_DOS, [dos] data, DOS auto-detection.
// Then detectEdition on that path. Throws StartupError (both editions found, DOS folder unusable or
// not found). An empty dataPath with WinCd means nothing was found (the Windows start-up reports it).
struct EditionChoice {
    Edition edition = Edition::WinCd;
    std::string dataPath;  // DOS: the folder holding SST.EXE
};
EditionChoice selectEdition(const LaunchOptions& o, const save::EngineSettings& settings, const std::filesystem::path& exeDir);

// The DOS edition's options: tmport.ini [dos], then --dos-args (the original's switches, in order),
// then --look / --sound / --slow-cpu / --frame-speed / --square-pixels; --no-audio = sound off; plus
// the run context (headless, keys, preference folder).
dos::DosOptions resolveDosOptions(const LaunchOptions& o, const save::EngineSettings& settings,
                                  const std::filesystem::path& prefDir);
// The host of the DOS edition: PresenterLayout::dos1990 (square pixels on request), [dos]
// window_scale, and the shared [engine] integer_scale / fullscreen / pause_on_focus_loss with their
// command-line overrides.
mischief::platform::HostConfig dosHostConfig(const LaunchOptions& o, const save::EngineSettings& settings,
                                   const std::filesystem::path& screenshotDir);

// The whole game: parse argv, init, run entry (empty = game::gameMain), report errors through sink
// (empty = defaultErrorSink). Returns the process exit code (0 ok, 2 usage, 3 start-up error).
int runGame(int argc, char** argv, const GameMainFn& entry = {}, const ErrorSink& sink = {});
int runGame(const LaunchOptions& options, const GameMainFn& entry = {}, const ErrorSink& sink = {});

}  // namespace tmport::app
