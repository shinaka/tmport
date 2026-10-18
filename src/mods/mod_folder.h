#pragma once
// A mod folder on disk: its manifest (mod.json) and the override files it contains.
//
//   <dir>/mod.json             optional: {"name": "...", "newIds": [2500, "2600-2610"],
//                              "resizable": [2100]}
//   <dir>/PMAP/<id>.png        256-colour art (TMT256.DLL)
//   <dir>/PMAP16/<id>.png      16-colour art (TMT16.DLL; --16). Without it, the 16-colour mode
//                              uses PMAP/<id>.png mapped to the 16 colours.
//   <dir>/SSND/<id>.wav        sounds
//   <dir>/OTHR/<id>.txt|.bin   text (escape syntax, mods/text_codec.h) or raw bytes
//   <dir>/DAM/<id>.dam.txt     DAM animation scripts (OTHR ids; mods/dam_asm.h)
//   <dir>/BNDL/<id>.json       bundle member lists
//   <dir>/FFNT/<id>.ffnt|.bin  font (binary FFNT)
//   <dir>/RRGB/<id>.pal        palette (768 bytes: 256 x R,G,B)
//   <dir>/MIDI/<id>.mid        music 9000..9009 (both sets); <id>.e.mid / <id>.b.mid for one set
//   <dir>/TABLES/*.json        EXE table overrides (mods/table_overrides.h)
// Folder and file names are matched case-insensitively.

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gfx/palette.h"
#include "res/resource_types.h"

namespace tmport::mods {

enum class ModFileKind { Pmap, Pmap16, Ssnd, OthrBin, OthrText, Dam, Bndl, Ffnt, Rrgb, Midi, MidiE, MidiB };

// Resource type served by a kind (nullopt for MIDI files).
std::optional<ResType> resTypeOf(ModFileKind k);
// "PMAP/1000.png"-style description of the expected file name, for messages and docs.
std::string describeKind(ModFileKind k);

// Inclusive id ranges ("2500" or "2600-2610" in JSON).
class IdSet {
public:
    void add(int lo, int hi) { ranges_.emplace_back(lo, hi); }
    bool contains(int id) const;
    bool empty() const { return ranges_.empty(); }

private:
    std::vector<std::pair<int, int>> ranges_;
};

struct ModManifest {
    std::string name;
    IdSet newIds;     // ids that do not exist in the game and may be added
    IdSet resizable;  // existing PMAPs whose size, BNDLs whose member count may change
};

struct ModFile {
    ModFileKind kind = ModFileKind::OthrBin;
    int id = 0;
    std::filesystem::path path;
};

struct ModFolder {
    std::filesystem::path dir;
    ModManifest manifest;
    std::vector<ModFile> files;                       // in directory order
    std::vector<std::filesystem::path> tableFiles;    // TABLES/*.json, sorted by name
    std::vector<std::string> warnings;                // unrecognised names, manifest problems
    std::vector<std::string> errors;                  // a broken mod.json
};

// Scans a folder. A missing folder gives an empty ModFolder (no error).
ModFolder scanModFolder(const std::filesystem::path& dir);

// Parses mod.json text. Throws FormatError.
ModManifest parseManifest(std::string_view json);

// The id range of a type, e.g. "1000-3999" for PMAP.
std::string idRangeText(ResType t);

}  // namespace tmport::mods
