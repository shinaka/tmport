#include "res/resource_manager.h"

#include <cstring>
#include <format>

#include "util/byte_reader.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport {

std::optional<BundleInfo> parseBundle(std::span<const uint8_t> data) {
    if (data.size() < 8 || std::memcmp(data.data(), "BNDL", 4) != 0) return std::nullopt;
    ByteReader r(data, "BNDL");
    const uint16_t count = r.u16(4);  // Bndl_GetCount 1038:0088 reads the low word
    if (!r.has(8, static_cast<size_t>(count) * 4)) throw FormatError(std::format("BNDL count {} overruns the resource", count));
    BundleInfo b;
    for (uint16_t i = 0; i < count; ++i) {
        const uint32_t raw = r.u32(8 + static_cast<size_t>(i) * 4);
        b.rawIds.push_back(raw);
        b.ids.push_back(static_cast<int>(raw & 0xFFFF));
    }
    return b;
}

ResourceManager::ResourceManager(const vfs::DataSource& source) : ResourceManager(source, Options{}) {}

ResourceManager::ResourceManager(const vfs::DataSource& source, Options options) : source_(source), options_(options) {
    auto openDll = [&](DllId d, bool required) {
        const std::string name(dllFileName(d));
        auto file = source_.open(name);
        if (!file) {
            if (required)
                throw MissingFileError(name, std::format("the game file {} was not found in {}", name, source_.describe()));
            return;
        }
        dlls_[static_cast<size_t>(d)] = std::make_unique<ne::NeImage>(ne::NeImage::open(std::move(file), name));
    };
    const bool colour16 = options_.colorMode == gfx::ColorMode::Colour16;
    openDll(DllId::TMT, true);
    openDll(DllId::TMT256, !colour16);
    openDll(DllId::TMT16, colour16);
    for (int n = 0; n < 9; ++n) openDll(soundDll(n), options_.requireSounds);
}

std::optional<ResourceManager::Location> ResourceManager::locate(ResType type, int id,
                                                                 std::optional<gfx::ColorMode> mode) const {
    const auto d = dllFor(type, id, mode.value_or(options_.colorMode));
    if (!d) return std::nullopt;
    const ne::NeImage* img = dll(*d);
    if (!img || id < 0 || id > 0x7FFF) return std::nullopt;
    const ne::Resource* r = img->findByTypeName(resTypeName(type), static_cast<uint16_t>(id));
    if (!r) return std::nullopt;
    return Location{*d, r};
}

uint32_t ResourceManager::roundedSize(ResType type, int id) const {
    if (mods_.provides(type, id, options_.colorMode))
        if (auto mod = mods_.find(type, id, options_.colorMode, *this)) return static_cast<uint32_t>(mod->size());
    auto loc = locate(type, id);
    return loc ? loc->resource->length : 0;
}

uint32_t ResourceManager::gameSize(int id) const {
    const ResType t = typeForId(id);
    const uint32_t s = roundedSize(t, id);
    return t == ResType::BNDL ? s * 2 : s;
}

std::optional<std::vector<uint8_t>> ResourceManager::loadRaw(ResType type, int id, std::optional<gfx::ColorMode> mode) const {
    // MOD OVERRIDE HOOK: <mod folder>/<TYPE>/<id>.<ext> wins (res/mod_overrides.h, docs/MODDING.md).
    if (mods_.enabled())
        if (auto mod = mods_.find(type, id, mode.value_or(options_.colorMode), *this)) return mod;
    return loadOriginal(type, id, mode);
}

std::optional<std::vector<uint8_t>> ResourceManager::loadOriginal(ResType type, int id, std::optional<gfx::ColorMode> mode) const {
    auto loc = locate(type, id, mode);
    if (!loc) return std::nullopt;
    return dll(loc->dll)->readResource(*loc->resource);
}

std::optional<ResourceTree> ResourceManager::getResource(int id) const { return getResourceImpl(id, 0); }

std::optional<ResourceTree> ResourceManager::getResourceImpl(int id, int depth) const {
    auto bytes = loadRaw(id);
    if (!bytes) return std::nullopt;
    ResourceTree t;
    t.type = typeForId(id);
    t.id = id;
    t.bytes = std::move(*bytes);
    // The original tests the loaded bytes for "BNDL", whatever the type (1038:076f).
    if (auto b = parseBundle(t.bytes)) {
        if (depth >= 8) throw FormatError(std::format("BNDL {}: nesting too deep", id));
        t.isBundle = true;
        for (int sub : b->ids) {
            t.memberIds.push_back(sub);
            t.memberSizes.push_back(gameSize(sub));
            auto m = getResourceImpl(sub, depth + 1);
            if (!m) {
                log::warn("BNDL {}: member {} does not exist", id, sub);
                ResourceTree empty;
                empty.type = typeForId(sub);
                empty.id = sub;
                t.members.push_back(std::move(empty));
            } else {
                t.members.push_back(std::move(*m));
            }
        }
    }
    return t;
}

std::optional<BundleInfo> ResourceManager::bundle(int id) const {
    auto bytes = loadRaw(ResType::BNDL, id);
    if (!bytes) return std::nullopt;
    return parseBundle(*bytes);
}

gfx::Palette ResourceManager::palette(int id) const {
    auto bytes = loadRaw(ResType::RRGB, id);
    if (!bytes) throw MissingFileError("TMT.DLL", std::format("RRGB {} not found in TMT.DLL", id));
    return gfx::Palette::fromRrgb(*bytes);
}

std::shared_ptr<const gfx::PixMap> ResourceManager::pixmap(int id, gfx::ColorMode mode) const {
    auto& cache = pixCache_[mode == gfx::ColorMode::Colour16 ? 1 : 0];
    if (options_.cachePixmaps)
        if (auto it = cache.find(id); it != cache.end()) return it->second;
    if (typeForId(id) != ResType::PMAP) return nullptr;
    auto bytes = loadRaw(ResType::PMAP, id, mode);
    if (!bytes) return nullptr;
    std::shared_ptr<const gfx::PixMap> pm;
    try {
        pm = std::make_shared<const gfx::PixMap>(gfx::decodePmap(*bytes, mode == gfx::ColorMode::Colour16 ? 4 : 8));
    } catch (const FormatError& e) {
        throw FormatError(std::format("PMAP {}: {}", id, e.what()));
    }
    if (options_.cachePixmaps) cache.emplace(id, pm);
    return pm;
}

std::optional<SsndInfo> ResourceManager::soundInfo(int id) const {
    auto bytes = loadRaw(ResType::SSND, id);
    if (!bytes) return std::nullopt;
    return parseSsnd(*bytes);
}

std::vector<int> ResourceManager::ids(ResType type, DllId d) const {
    std::vector<int> out;
    const ne::NeImage* img = dll(d);
    if (!img) return out;
    for (uint16_t id : img->idsForTypeName(resTypeName(type))) out.push_back(id);
    return out;
}

void ResourceManager::clearCache() {
    for (auto& c : pixCache_) c.clear();
}

}  // namespace tmport
