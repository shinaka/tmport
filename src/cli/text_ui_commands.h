#pragma once
// Headless render commands of the text/UI area (milestone 2): FFNT glyph sheet, OTHR text
// layout, game dialogs (DlgDesc) and help pager pages, written as PNGs of the 512x384 game area
// for comparison with reference captures of the original (re/docs/REFERENCE_CAPTURES.md).

#include <optional>
#include <string>

namespace tmport::cli {

// If argv contains one of the commands below, runs it and returns the process exit code;
// otherwise returns nullopt and the regular command parser handles argv.
//   --render-dialog <name|hexoffset> <out.png>
//   --render-help <topic 1..4> <page 1..n> <out.png>
//   --render-text <othrId> <x> <y> <w> <out.png>
//   --render-font <out.png>
//   --text-metrics <othrId> [<w>]
// Options: --data <path>, --16, --bg <file.ppm> (512x384 binary PPM background, e.g. a cropped
// capture), --bg-pmap <id> [--bg-at <x> <y>] (PMAP drawn opaque, default at (0,20)),
// --fill <x> <y> <w> <h> <ega> (solid rectangle after the background, repeatable),
// --sel <n> (dialog default button; -1 = none highlighted), --arg <text> (fills the %s of a
// sign-in dialog body), --colour <n> (text pen, EGA number), --fb-dump <png> (whole buffer),
// --mods <dir> (repeatable; cli/tool_mods.h).
std::optional<int> runTextUiCommand(int argc, char** argv);
std::string textUiUsage();

}  // namespace tmport::cli
