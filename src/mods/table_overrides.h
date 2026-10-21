#pragma once
// JSON overrides for EXE tables (re/docs/PORTING_NOTES.md §7.2, dgroup_map.md). A table file is a
// JSON object keyed by table name; every key is optional and keys starting with '_' are ignored:
//
//   "rooms":          50 room records (DS:23d8): objects with any of the fields startX, startY,
//                     startFrame, startFacingLeft, exitLeft, exitRight, exitDoor, typeLeft,
//                     typeRight, typeDoor, doorX, bgPmap, level, music (+ unused09, unused0D,
//                     unused10 for an exact copy)
//   "rankThresholds": 8 numbers (DS:0ea0), [0] must be 0, never decreasing
//   "rankNames":      8 strings (DS:0fa8 far pointers)
//   "gemsNeeded":     4 numbers 1..5, treasures per climb by tier (DS:0ec3)
//   "gemX", "gemY":   4 x 5 numbers, HUD treasure icon positions by tier (DS:0ec7 / DS:0eef)
//   "elfCountByTier": 4 numbers 0..32, elves per level by tier (DS:128e)
//   "hintBit":        34 numbers -1..31 (DS:13d0; -1 = message shown every time)
//   "clueWords":      {"count": 8, "size": 5, "shape": 5, "noun": 16, "nounPlural": 16} strings
//                     (DS:10f9, 1119, 112d, 1079, 10b9; element 0 is unused)
//
// An array must have the table's length (null keeps an element); an object {"index": value}
// changes single elements. Strings use the escape syntax of mods/text_codec.h. A document is
// validated completely before anything is changed.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "data/exe_tables.h"
#include "mischief/mods/json.h"

namespace tmport::mods {

// Table names, in the exporter's order.
const std::vector<std::string>& supportedTables();

// The tables of exe as one JSON object (the exporter's form).
mischief::mods::json::Value exportTables(const ExeTables& exe);
// One file per table: {file name, JSON text}, e.g. {"rooms.json", "{\"rooms\": [...]}"}.
std::vector<std::pair<std::string, std::string>> exportTableFiles(const ExeTables& exe);

struct TableApplyResult {
    std::vector<std::string> tables;    // names of the tables the document touched
    size_t changedBytes = 0;            // bytes that differ from before
    size_t repointedStrings = 0;        // strings moved to new storage
    std::vector<std::string> warnings;
};

// Validates and applies a document. Throws FormatError naming the JSON path, e.g.
// "rooms[12].exitLeft: 60 is not a room (0-49) or 255"; nothing is changed then.
TableApplyResult applyTables(ExeTables& exe, const mischief::mods::json::Value& doc);
TableApplyResult applyTablesText(ExeTables& exe, std::string_view jsonText);

}  // namespace tmport::mods
