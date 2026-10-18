#pragma once
// Start-up of the game (the port's WinMain, re/docs/ARCHITECTURE.md §2.3, S1-S4):
//   paths + settings (save::Paths, tmport.ini, one-time users.dat import)
//   -> data path (--data, TM_DATA, tmport.ini [engine] data, auto-detection)
//   -> GameFiles (refuse to start with a clear message naming a missing file; the original
//      silently swallowed DLL load failures)
//   -> Engine (+ window in GUI mode), palette (RRGB 900 / VGA 16), cursors
//   -> AudioSystem (music set and MIDI back end from the settings; failures are not fatal)
//   -> Font (FFNT 999), TextRenderer, DialogSystem, AnimEngine, MenuBar (+ command handlers),
//      GameState -> AppContext, EngineGlue (pump/activation/Ctrl+S/dialog hooks)
//   -> the game entry (GameMainFn), with QuitGame / RestartToSignIn handling.

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "app/app_context.h"
#include "app/game_main.h"
#include "app/launch_options.h"
#include "util/error.h"

namespace tmport::app {

// A start-up failure with the message for the player. file(): the missing game file, if any.
class StartupError : public Error {
public:
    explicit StartupError(const std::string& message, std::string file = {}) : Error(message), file_(std::move(file)) {}
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
    bool initialised() const { return ctx_ != nullptr; }
    AppContext& context() { return *ctx_; }
    const LaunchOptions& options() const { return options_; }
    // Runs the entry (default: game::gameMain, game/game_main.h) with the scripted input of --keys and
    // writes --frame-dump at the end. Returns the exit code.
    int run(const GameMainFn& entry);

private:
    struct Parts;
    LaunchOptions options_;
    std::unique_ptr<Parts> p_;
    std::unique_ptr<AppContext> ctx_;
};

// The data path chosen from --data, TM_DATA, tmport.ini [engine] data and auto-detection (empty
// when nothing was found).
std::string resolveDataPath(const LaunchOptions& o, const std::string& iniDataPath);

// The whole game: parse argv, init, run entry (empty = game::gameMain), report errors through sink
// (empty = defaultErrorSink). Returns the process exit code (0 ok, 2 usage, 3 start-up error).
int runGame(int argc, char** argv, const GameMainFn& entry = {}, const ErrorSink& sink = {});
int runGame(const LaunchOptions& options, const GameMainFn& entry = {}, const ErrorSink& sink = {});

}  // namespace tmport::app
