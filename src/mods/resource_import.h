#pragma once
// Converting one mod file into the original binary resource format, with the checks of
// PORTING_NOTES.md §7: the id range decides the type; replaced art keeps its logical size;
// new ids must be declared in mod.json; BNDL member counts the code indexes; the question-text
// grammar of OTHR 11000-11358 and the narration that depends on its line count; DAM scripts that
// the interpreter can run. The same code serves the runtime override hook, `tmport --check-mods`
// and the round-trip check of the mod-kit exporter.

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "mischief/gfx/palette.h"
#include "mods/mod_folder.h"
#include "res/resource_types.h"

namespace tmport {
class ResourceManager;
}

namespace tmport::mods {

// What an import may ask about the game data (decoupled from ResourceManager for tests).
struct ImportContext {
    // The original resource body (NE-rounded) from the DLLs, ignoring mods; nullopt if absent.
    std::function<std::optional<std::vector<uint8_t>>(ResType, int, mischief::gfx::ColorMode)> original;
    // The 256-colour logical palette (RRGB 900, a mod's RRGB override included).
    std::function<mischief::gfx::Palette()> palette256;
    // True if a resource id exists in the game or in a mod.
    std::function<bool(int)> exists;
    const ModManifest* manifest = nullptr;
};

// An ImportContext over a ResourceManager (the palette comes through its mod hook, originals and
// existence from the DLLs plus the installed mods).
ImportContext importContextFor(const ResourceManager& res, const ModManifest* manifest);

struct ImportResult {
    std::vector<uint8_t> body;        // original format, NE-padded
    std::vector<std::string> notes;   // informational (conversions done)
    std::vector<std::string> warnings;
};

// Converts a resource override (not MIDI). Throws FormatError with a message that names the
// problem (the caller adds the file path).
ImportResult importResourceFile(const ModFile& file, const ImportContext& ctx);
// Same for file contents already in memory.
ImportResult importResourceBytes(ModFileKind kind, int id, std::span<const uint8_t> bytes, const ImportContext& ctx);

// Checks a MIDI override (id 9000..9009, a Standard MIDI File the sequencer accepts).
void validateMidiFile(int id, std::span<const uint8_t> bytes);

// BNDL JSON: [ids...] or {"members": [ids...]} -> body; and the exporter's form.
std::vector<uint8_t> bndlBodyFromJson(std::string_view json, std::vector<uint32_t>* members = nullptr);
std::string bndlJson(std::span<const uint32_t> members);

// Member-count rule of a bundle the game code indexes (PORTING_NOTES §7 end); empty text if none.
struct BundleRule {
    int minCount = 0;
    int maxCount = 0x7FFF;
    bool firstIsScript = false;  // member 0 is a DAM script (OTHR), the rest are frames
    const char* what = "";
};
std::optional<BundleRule> bundleRule(int bndlId);

}  // namespace tmport::mods
