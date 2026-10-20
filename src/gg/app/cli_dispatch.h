#pragma once
// The tool commands of ggport-cli (and ggport): each area exposes
// `bool tmport::gg::<area>Cli(const std::vector<std::string>& args, int& exitCode)` in
// src/gg/<area>/<area>_cli.h, which returns false when none of `args` is its command. The
// dispatcher asks them in turn; the first that takes the command decides the exit code. Without a
// tool command the program runs the game (gg_app.h).
//
//   data     gg/data/data_cli.h      --gg-info, --gg-list, --gg-selftest, --gg-extract
//   gfx      gg/gfx/gfx_cli.h        (the graphics tools)
//   audio    gg/audio/audio_cli.h    (the sound tools)
//   runtime  gg/app/runtime_cli.h    --gg-clock, --gg-input-trace
//   world    gg/game/world_cli.h     --gg-screen hub|warehouse|blueprint|puzzle, --gg-rooms
//   ui       gg/ui/ui_cli.h          --gg-screen (the attract, sign-in and UI screens), --gg-ui-info

#include <string>
#include <vector>

namespace tmport::gg {

// `args` without argv[0]. True when a tool ran (exitCode set).
bool runToolCli(const std::vector<std::string>& args, int& exitCode);

}  // namespace tmport::gg
