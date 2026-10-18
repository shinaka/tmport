// tmport: engine re-implementation of Super Solvers Treasure Mountain! (Windows CD edition).
// The engine contains no game data; it reads the user's original files at run time.
//
// `tmport` (no arguments, --play, or only game options) starts the game; any tool command
// (--info, --dump-*, --render-*, --view, ...) runs the command-line tools instead.
//
// Windows: tmport.exe is a GUI-subsystem program (no console window on a double-click); SDL_main.h
// supplies WinMain, which calls main() with UTF-8 arguments. Started from a console it attaches
// to that console for its output (platform/console.h). tmport-cli.exe is the same program as a
// console application, for scripts that need the shell to wait and see the exit code.

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <string_view>

#include "app/bootstrap.h"
#include "app/launch_options.h"
#include "cli/commands.h"
#include "game/game_main.h"
#include "platform/console.h"

int main(int argc, char** argv) {
    tmport::platform::attachParentConsole();
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--help" || a == "-h" || a == "/?") {
            std::fputs(tmport::app::launchUsage().c_str(), stdout);
            std::fputs("\nTools:\n", stdout);
            break;
        }
    }
    if (tmport::app::isGameInvocation(argc, argv)) {
        // The game: GameMain (1118:0000, game/game_main.h).
        return tmport::app::runGame(argc, argv, [](tmport::AppContext& ctx) { tmport::game::gameMain(ctx); });
    }
    return tmport::cli::run(argc, argv);
}
