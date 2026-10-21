#pragma once
// The mod-kit exporter (tmport --export-mod-kit <dir>): writes the user's own game assets in the
// editable formats of docs/MODDING.md, laid out as a mod folder, so that they can be edited and
// copied into a mod. With verification on, every written file is imported again through the
// same code as a real mod override and compared with the original bytes (import(export(x)) == x).
//
// The kit is the user's private copy of game data: it is written only to the directory the user
// names and must never be distributed or committed anywhere.

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "mischief/gfx/palette.h"
#include "mods/mod_folder.h"
#include "res/game_files.h"

namespace tmport::mods {

struct ModKitOptions {
    bool verify = true;   // re-import every file and compare with the original
    bool force = false;   // allow a non-empty output directory (files are overwritten)
};

struct ModKitReport {
    std::map<std::string, size_t> counts;  // files per folder ("PMAP", "SSND", ..., "TABLES")
    size_t files = 0;
    uint64_t bytes = 0;
    size_t verified = 0;                   // files whose round trip was byte-exact
    std::vector<std::string> failures;     // round-trip mismatches and errors
};

// Throws mischief::Error if the directory cannot be used (exists and is not empty without force).
ModKitReport exportModKit(const GameFiles& files, const std::filesystem::path& outDir, const ModKitOptions& options = {});

// One resource in its editable form (from the DLLs, ignoring mods).
struct ExportedFile {
    std::string rel;              // path inside a mod folder, e.g. "PMAP/1000.png"
    ModFileKind kind = ModFileKind::OthrBin;
    int id = 0;
    std::vector<uint8_t> bytes;   // file content
    std::vector<uint8_t> original;  // the resource body it was made from
};
// nullopt if the resource does not exist. mode selects TMT16 art for PMAPs. Throws FormatError
// for data that cannot be converted.
std::optional<ExportedFile> exportResource(const ResourceManager& res, ResType type, int id,
                                           mischief::gfx::ColorMode mode = mischief::gfx::ColorMode::Colour256);

}  // namespace tmport::mods
