// ggport: engine re-implementation of Super Solvers: Gizmos & Gadgets! (DOS v1.0, 1993). The engine
// contains no game data; it reads the user's original files at run time (re/gg/docs/SCOPING.md §9).
//
// `ggport` (no arguments, --play, or only game options) starts the game (gg/app/gg_app.h; until wave 3
// the preview, gg/app/preview.h); a tool command (--gg-info, --gg-screen, ...; gg/app/cli_dispatch.h)
// runs that tool instead.
//
// Windows: ggport.exe is a GUI-subsystem program (no console window on a double-click; SDL_main.h
// supplies WinMain, which calls main() with UTF-8 arguments). Started from a console it attaches to
// that console for its output (platform/console.h). ggport-cli.exe is the same program as a console
// application, for the tools and scripts that need the shell to wait and see the exit code.

#include <SDL3/SDL_main.h>

#include <string>
#include <vector>

#include "gg/app/cli_dispatch.h"
#include "gg/app/gg_app.h"
#include "gg/app/preview.h"
#include "platform/console.h"

int main(int argc, char** argv) {
    tmport::platform::attachParentConsole();
    const std::vector<std::string> args(argv + (argc > 0 ? 1 : 0), argv + argc);
    int exitCode = 0;
    if (tmport::gg::runToolCli(args, exitCode)) return exitCode;
    return tmport::gg::runGgApp(args, &tmport::gg::previewMain);
}
