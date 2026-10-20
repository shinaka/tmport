#pragma once
// Headless command-line tools of the G&G data layer (the ggport-cli dispatcher calls dataCli with
// the program's arguments, without argv[0]; it returns false when none of them is its command).
//
//   --gg-info       [--data DIR]              the folder, ssg.exe and per-volume archive statistics
//   --gg-list       [--data DIR] [--leaves]   one TSV row per entry: id, volume, index, file offset,
//                                             type, method, stored and unpacked size, kind, CRC-32 of
//                                             the unpacked payload (--leaves: bundle members too)
//   --gg-selftest   [--data DIR]              decodes every entry and bundle and checks them (sizes,
//                                             over-reads, match bounds, codes, the 0xA55A ends)
//   --gg-extract ID OUT [--data DIR]          writes the decompressed payload of resource ID (hex,
//                                             "0x1002" or "1002"; "0001/3" for a bundle member) to OUT,
//                                             for checks only (under extracted/; never into the game
//                                             folder)
//
// The folder is --data DIR, else GG_DATA, else found next to the program (detectGgFolder).

#include <string>
#include <vector>

namespace tmport::gg {

bool dataCli(const std::vector<std::string>& args, int& exitCode);

}  // namespace tmport::gg
