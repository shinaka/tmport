#pragma once
// The world area's tools for ggport-cli (re/gg/docs/code_world.md, code_puzzles_a.md,
// code_puzzles_b.md): screens drawn by the engine's own draw paths, written as 320x200 PNGs of what
// VRAM shows through the DAC (the palette the screen loaded, as after its fade-in), and listings of
// the world's data.
//
//   --gg-screen hub OUT.png [--scene FILE]
//   --gg-screen warehouse OUT.png --scene FILE | --race R --room N [--rand-state HEX]
//   --gg-screen puzzle KIND VARIANT OUT.png [--scene FILE]
//   --gg-screen blueprint OUT.png --scene FILE | --race R [--page P]   the viewer over the workshop (box only)
//   --gg-rooms RACE [--rand-state HEX]      the rooms of a race's set, their objects and dealt items (TSV)
//
// Common options: --data DIR (the game folder; else GG_DATA, else next to the program),
// --mouse X,Y (draw the cursor there; default: the scene's, else no cursor), --no-cursor, --no-menu-bar.
// A scene file is the JSON that extracted/scratch/gg/wave2/world/scene.py writes from a DOSBox-X
// save state (positions, frames and counters as the game held them). Exit codes: 0 ok, 1 data error,
// 2 usage.

#include <string>
#include <vector>

namespace tmport::gg {

// `args` without argv[0]. Returns false when none of its commands is present, and for a --gg-screen
// whose name is not one of the four above (the UI tool's screens, gg/ui/ui_cli.h).
bool worldCli(const std::vector<std::string>& args, int& exitCode);

}  // namespace tmport::gg
