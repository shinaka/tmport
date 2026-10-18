#pragma once
// ResourceManager: the original's Mac Resource Manager emulation (segment 1038) over the user's
// DLLs. Ids are routed to a type by range and to a DLL by type and id
// (re/docs/sys_resources_saves.md §5.2-5.3), then looked up through each DLL's type-15 name
// table. Raw bodies keep the NE-rounded size, BNDLs expand recursively like GetResource
// (1038:076f), and PMAP/RRGB/SSND have typed accessors. Decoded PixMaps may be cached (the
// original has no cache; decoded data is immutable, so caching is invisible).

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "gfx/palette.h"
#include "gfx/pixmap.h"
#include "ne/ne_image.h"
#include "res/mod_overrides.h"
#include "res/resource_types.h"
#include "res/ssnd.h"
#include "vfs/data_source.h"

namespace tmport {

// Parsed on-disk BNDL: "BNDL", u16 count (+4), u16 0, count x u32 member ids (+8).
struct BundleInfo {
    std::vector<int> ids;          // low 16 bits (what the original uses)
    std::vector<uint32_t> rawIds;  // full u32 values
};
std::optional<BundleInfo> parseBundle(std::span<const uint8_t> data);

// Result of GetResource (1038:076f): the resource body and, for a BNDL, its loaded members.
struct ResourceTree {
    ResType type = ResType::OTHR;
    int id = 0;
    std::vector<uint8_t> bytes;          // rounded body as read from the DLL
    bool isBundle = false;
    std::vector<int> memberIds;          // BNDL member ids, in order
    std::vector<uint32_t> memberSizes;   // Res_GetSize of each member (rounded; x2 for a BNDL)
    std::vector<ResourceTree> members;   // loaded members (empty bytes if a member is missing)
};

class ResourceManager {
public:
    struct Options {
        gfx::ColorMode colorMode = gfx::ColorMode::Colour256;
        bool requireSounds = true;   // the sound DLLs must exist
        bool cachePixmaps = true;
    };

    // Opens the DLLs from the data source. Throws MissingFileError naming a missing file.
    explicit ResourceManager(const vfs::DataSource& source);
    ResourceManager(const vfs::DataSource& source, Options options);

    gfx::ColorMode colorMode() const { return options_.colorMode; }
    const ne::NeImage* dll(DllId d) const { return dlls_[static_cast<size_t>(d)].get(); }
    bool hasDll(DllId d) const { return dll(d) != nullptr; }
    ModOverrides& mods() { return mods_; }
    const ModOverrides& mods() const { return mods_; }

    struct Location {
        DllId dll;
        const ne::Resource* resource;
    };
    // mode: pixmap DLL to use (default: the active colour mode). The DLLs only (no mods).
    std::optional<Location> locate(ResType type, int id, std::optional<gfx::ColorMode> mode = {}) const;
    // In the DLLs or provided by an installed mod (docs/MODDING.md).
    bool exists(int id) const { return locate(typeForId(id), id).has_value() || mods_.provides(typeForId(id), id, options_.colorMode); }
    // NE-aligned length (SizeofResource); 0 if missing. A mod override reports its own (padded) size.
    uint32_t roundedSize(ResType type, int id) const;
    // 1038:00c9 Res_GetSize: the rounded size, doubled for BNDL.
    uint32_t gameSize(int id) const;

    // Raw body with the rounded size. The mod hook is consulted first.
    std::optional<std::vector<uint8_t>> loadRaw(int id) const { return loadRaw(typeForId(id), id); }
    std::optional<std::vector<uint8_t>> loadRaw(ResType type, int id, std::optional<gfx::ColorMode> mode = {}) const;
    // The body from the user's DLLs, ignoring mods (mod validation, the mod-kit exporter).
    std::optional<std::vector<uint8_t>> loadOriginal(ResType type, int id, std::optional<gfx::ColorMode> mode = {}) const;
    // GetResource: loads id and, if it is a BNDL, all members recursively and in order.
    std::optional<ResourceTree> getResource(int id) const;
    std::optional<BundleInfo> bundle(int id) const;

    // RRGB palette (900 is the only one). Throws if missing.
    gfx::Palette palette(int id = 900) const;
    // Decoded PMAP from the active (or given) pixmap DLL; nullptr if it does not exist. Throws
    // FormatError (naming the id) for corrupt data.
    std::shared_ptr<const gfx::PixMap> pixmap(int id) const { return pixmap(id, options_.colorMode); }
    std::shared_ptr<const gfx::PixMap> pixmap(int id, gfx::ColorMode mode) const;
    std::optional<SsndInfo> soundInfo(int id) const;

    // Ids of a type listed in a DLL's name table (table order); empty if the DLL is not loaded.
    std::vector<int> ids(ResType type, DllId d) const;
    void clearCache();

private:
    std::optional<ResourceTree> getResourceImpl(int id, int depth) const;

    const vfs::DataSource& source_;
    Options options_;
    std::array<std::unique_ptr<ne::NeImage>, kDllCount> dlls_;
    ModOverrides mods_;
    mutable std::array<std::unordered_map<int, std::shared_ptr<const gfx::PixMap>>, 2> pixCache_;
};

}  // namespace tmport
