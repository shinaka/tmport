// tmport: engine re-implementation of Super Solvers Treasure Mountain! (Windows CD edition).
// The engine contains no game data; it reads the user's original files at run time.
//
// `tmport` (no arguments, --play, or only game options) starts the game; anything else (a tool
// command such as --info, --render-*, --scene, ..., or --help) runs the command-line tools
// (cli/commands.h), which also print the help of the game options.
//
// Windows: tmport.exe is a GUI-subsystem program (no console window on a double-click); SDL_main.h
// supplies WinMain, which calls main() with UTF-8 arguments. Started from a console it attaches
// to that console for its output (mischief/platform/console.h). tmport-cli.exe is the same program as a
// console application, for scripts that need the shell to wait and see the exit code.

#include <SDL3/SDL_main.h>

#include "app/bootstrap.h"
#include "app/launch_options.h"
#include "cli/commands.h"
#include "game/game_main.h"
#include "mischief/platform/console.h"

int main(int argc, char** argv) {
    mischief::platform::attachParentConsole();
    if (tmport::app::isGameInvocation(argc, argv)) {
        // The game: GameMain (1118:0000, game/game_main.h).
        return tmport::app::runGame(argc, argv, [](tmport::AppContext& ctx) { tmport::game::gameMain(ctx); });
    }
    return tmport::cli::run(argc, argv);
}
