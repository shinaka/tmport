#include "gg/app/cli_dispatch.h"

#include <cstdio>
#include <exception>

#include "gg/app/runtime_cli.h"
#include "gg/audio/audio_cli.h"
#include "gg/data/data_cli.h"
#include "gg/game/world_cli.h"
#include "gg/gfx/gfx_cli.h"
#include "gg/ui/ui_cli.h"

namespace tmport::gg {

bool runToolCli(const std::vector<std::string>& args, int& exitCode) {
    using AreaCli = bool (*)(const std::vector<std::string>&, int&);
    // worldCli before uiCli: both answer --gg-screen, the world tool only for its own screens (hub,
    // warehouse, blueprint, puzzle), the UI tool for every other name.
    static constexpr AreaCli kAreas[] = {
        &dataCli, &gfxCli, &audioCli, &runtimeCli, &worldCli, &uiCli,
    };
    for (const AreaCli cli : kAreas) {
        try {
            if (cli(args, exitCode)) return true;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "ggport: %s\n", e.what());
            exitCode = 1;
            return true;
        }
    }
    return false;
}

}  // namespace tmport::gg
