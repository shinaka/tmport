#pragma once
// MOD OVERRIDE HOOK (docs/DESIGN.md "Mod layer", re/docs/PORTING_NOTES.md §7, docs/MODDING.md).
//
// ResourceManager asks this object first for every resource. It holds the installed mod folders
// (lowest precedence first: a later folder overrides an earlier one) and an index of their files:
// PMAP/<id>.png, PMAP16/<id>.png, SSND/<id>.wav, OTHR/<id>.txt|.bin, DAM/<id>.dam.txt,
// BNDL/<id>.json, FFNT/<id>.ffnt, RRGB/<id>.pal. A file is converted to the original binary
// format on first use (src/mods/resource_import.h) and the result is cached. A file that fails
// validation is reported once in the log and the game's own data is used instead.
//
// Folders are installed by mods::applyMods (src/mods/mods.h); without folders the hook is off.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gfx/palette.h"
#include "res/resource_types.h"

namespace tmport {

class ResourceManager;

namespace mods {
struct ModFolder;
struct ModFile;
}  // namespace mods

class ModOverrides {
public:
    ModOverrides();
    ~ModOverrides();
    ModOverrides(ModOverrides&&) noexcept;
    ModOverrides& operator=(ModOverrides&&) noexcept;

    // Installs mod folders, lowest precedence first (replaces earlier ones, clears the cache).
    void setFolders(std::vector<mods::ModFolder> folders);
    bool enabled() const;
    const std::vector<mods::ModFolder>& folders() const;

    struct Entry {
        const mods::ModFile* file = nullptr;
        size_t folder = 0;  // index into folders()
    };
    // The winning override file for (type, id) in a colour mode. In 16-colour mode a PMAP16 file
    // wins over a PMAP file (which is then mapped to the 16 colours).
    std::optional<Entry> lookup(ResType type, int id, gfx::ColorMode mode) const;
    bool provides(ResType type, int id, gfx::ColorMode mode) const { return lookup(type, id, mode).has_value(); }
    // The override file, or an empty path.
    std::filesystem::path overridePath(ResType type, int id, gfx::ColorMode mode = gfx::ColorMode::Colour256) const;

    // Resource body in the original binary format (NE-padded), or nullopt to use the game's own
    // data (no override, or the override failed validation).
    std::optional<std::vector<uint8_t>> find(ResType type, int id, gfx::ColorMode mode, const ResourceManager& res) const;

    // Every (type, id) with an override (PMAP16-only overrides are listed under PMAP).
    std::vector<std::pair<ResType, int>> overriddenIds() const;
    // Files shadowed by a later folder or by a preferred file of the same folder ("...: ignored").
    const std::vector<std::string>& shadowed() const;
    // Conversion failures so far ("path: message").
    std::vector<std::string> failures() const;
    void clearCache();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tmport
