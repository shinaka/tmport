#pragma once
// Headless tools of the DOS graphics area (no window): PNG renders of the 1990 DOS edition's
// pictures, sprite packs and composed screens, for inspection and for pixel comparison with the
// DOSBox-X captures (re/dos/docs/REFERENCE_CAPTURES.md).
//
//   --dos-pan <file|name> <out.png>     a PAN: OPENING as its 320x200 picture, a scenery PAN as the
//                                       whole 16x16-block world (2048x2048) on the block grid
//                                       (--blocks: a sheet of its blocks instead)
//   --dos-pak <file|name> <out.png>     contact sheet of a PAK's frames with their numbers
//   --dos-screen <name> <out.png>       title, signin, clubhouse, quiz, path, castle, menu, msgbox,
//                                       newname (dos_screens.h)
// Options: --data <DOS folder> (default: TM_DATA_DOS, else the folder of a given file),
// --look ega|tandy|mcga|cga (default ega), --scale <n>; for --dos-pan: --users <USERS.DAT>
// [--slot <n>] (block grid with that saved game), --blocks; for --dos-pak: --opaque; for
// --dos-screen: --users, --slot, --camera <x> <y>, --coins/--nets/--found/--key/--prompt/--tier/
// --level/--found-mask <n>, --clue <a> <b> <c>, --puzzle <n>, --order <n>, --mask <n>,
// --sparkle <spot> <frame>, --no-sparkle, --pointer <x> <y>, --no-pointer,
// --actor <slot> <frame code> <x> <y> <z> (repeatable), --no-actors, --cell <cell> <picture>
// (repeatable block-grid change), --no-clue, --logical <out.bin> (320x200 logical colours, one byte
// per pixel); menu: --menu <0..2> --item <n>; msgbox: --box <n> [--button <0|1>]; both over
// --background <320x200 logical .bin>; newname: --name <text> [--cursor-off].

#include <optional>
#include <string>

namespace tmport::dos {

// If argv contains --dos-pan, --dos-pak or --dos-screen, runs it and returns the exit code
// (0 ok, 2 bad command line, 3 data error); otherwise nullopt.
std::optional<int> runDosGfxCommand(int argc, char** argv);
std::string dosGfxUsage();

}  // namespace tmport::dos
