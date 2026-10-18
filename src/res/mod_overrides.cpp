#include "res/mod_overrides.h"

#include <format>
#include <map>
#include <set>
#include <tuple>

#include "mods/mod_folder.h"
#include "mods/resource_import.h"
#include "res/resource_manager.h"
#include "util/file_util.h"
#include "util/log.h"

namespace tmport {

namespace {

// Index key: (type, id, variant); variant 1 = PMAP16 (16-colour art), 0 = everything else.
using Key = std::tuple<int, int, int>;

int otherPriority(mods::ModFileKind k) {
    // Within one folder: a DAM script beats a .txt, which beats a .bin.
    switch (k) {
        case mods::ModFileKind::Dam: return 3;
        case mods::ModFileKind::OthrText: return 2;
        case mods::ModFileKind::OthrBin: return 1;
        default: return 0;
    }
}

}  // namespace

struct ModOverrides::Impl {
    std::vector<mods::ModFolder> folders;
    std::map<Key, Entry> index;
    std::vector<std::string> shadowed;
    // Conversion cache: (type, id, mode16) -> body, or nullopt after a failure.
    mutable std::map<Key, std::optional<std::vector<uint8_t>>> cache;
    mutable std::vector<std::string> failures;
};

ModOverrides::ModOverrides() : impl_(std::make_unique<Impl>()) {}
ModOverrides::~ModOverrides() = default;
ModOverrides::ModOverrides(ModOverrides&&) noexcept = default;
ModOverrides& ModOverrides::operator=(ModOverrides&&) noexcept = default;

void ModOverrides::setFolders(std::vector<mods::ModFolder> folders) {
    impl_ = std::make_unique<Impl>();
    impl_->folders = std::move(folders);
    for (size_t fi = 0; fi < impl_->folders.size(); ++fi) {
        for (const mods::ModFile& f : impl_->folders[fi].files) {
            const auto t = mods::resTypeOf(f.kind);
            if (!t) continue;  // MIDI: served through the data source (mods/mods.h)
            const Key k{static_cast<int>(*t), f.id, f.kind == mods::ModFileKind::Pmap16 ? 1 : 0};
            auto it = impl_->index.find(k);
            if (it == impl_->index.end()) {
                impl_->index.emplace(k, Entry{&f, fi});
                continue;
            }
            Entry& old = it->second;
            if (old.folder == fi && otherPriority(old.file->kind) >= otherPriority(f.kind)) {
                impl_->shadowed.push_back(std::format("{}: ignored, {} of the same mod wins", pathToUtf8(f.path), pathToUtf8(old.file->path)));
                continue;
            }
            impl_->shadowed.push_back(old.folder == fi
                                          ? std::format("{}: ignored, {} of the same mod wins", pathToUtf8(old.file->path), pathToUtf8(f.path))
                                          : std::format("{}: overridden by {}", pathToUtf8(old.file->path), pathToUtf8(f.path)));
            old = Entry{&f, fi};
        }
    }
}

bool ModOverrides::enabled() const { return impl_ && !impl_->index.empty(); }

const std::vector<mods::ModFolder>& ModOverrides::folders() const {
    static const std::vector<mods::ModFolder> none;
    return impl_ ? impl_->folders : none;
}

std::optional<ModOverrides::Entry> ModOverrides::lookup(ResType type, int id, gfx::ColorMode mode) const {
    if (!enabled()) return std::nullopt;
    if (type == ResType::PMAP && mode == gfx::ColorMode::Colour16)
        if (auto it = impl_->index.find(Key{static_cast<int>(type), id, 1}); it != impl_->index.end()) return it->second;
    if (auto it = impl_->index.find(Key{static_cast<int>(type), id, 0}); it != impl_->index.end()) return it->second;
    return std::nullopt;
}

std::filesystem::path ModOverrides::overridePath(ResType type, int id, gfx::ColorMode mode) const {
    const auto e = lookup(type, id, mode);
    return e ? e->file->path : std::filesystem::path();
}

std::optional<std::vector<uint8_t>> ModOverrides::find(ResType type, int id, gfx::ColorMode mode, const ResourceManager& res) const {
    const auto e = lookup(type, id, mode);
    if (!e) return std::nullopt;
    const bool c16 = type == ResType::PMAP && mode == gfx::ColorMode::Colour16;
    const Key ck{static_cast<int>(type), id, c16 ? 1 : 0};
    if (auto it = impl_->cache.find(ck); it != impl_->cache.end()) return it->second;

    mods::ModFile file = *e->file;
    if (c16 && file.kind == mods::ModFileKind::Pmap) file.kind = mods::ModFileKind::Pmap16;  // 256-colour art mapped to 16 colours
    const mods::ModFolder& folder = impl_->folders[e->folder];
    const std::string where = pathToUtf8(e->file->path);
    std::optional<std::vector<uint8_t>> body;
    try {
        // May re-enter find() (the palette for a PMAP comes through RRGB): no iterators are held.
        mods::ImportResult r = mods::importResourceFile(file, mods::importContextFor(res, &folder.manifest));
        for (const auto& n : r.notes) log::debug("mod: {}: {}", where, n);
        for (const auto& w : r.warnings) log::warn("mod: {}: {}", where, w);
        log::debug("mod: {} {} <- {}", resTypeName(type), id, where);
        body = std::move(r.body);
    } catch (const std::exception& ex) {
        log::error("mod: {}: {} (the game's own {} {} is used instead)", where, ex.what(), resTypeName(type), id);
        impl_->failures.push_back(std::format("{}: {}", where, ex.what()));
    }
    impl_->cache[ck] = body;
    return body;
}

std::vector<std::pair<ResType, int>> ModOverrides::overriddenIds() const {
    std::set<std::pair<int, int>> s;
    if (!impl_) return {};
    for (const auto& [k, e] : impl_->index) s.emplace(std::get<0>(k), std::get<1>(k));
    std::vector<std::pair<ResType, int>> out;
    for (const auto& [t, id] : s) out.emplace_back(static_cast<ResType>(t), id);
    return out;
}

const std::vector<std::string>& ModOverrides::shadowed() const {
    static const std::vector<std::string> none;
    return impl_ ? impl_->shadowed : none;
}

std::vector<std::string> ModOverrides::failures() const { return impl_ ? impl_->failures : std::vector<std::string>{}; }

void ModOverrides::clearCache() {
    if (!impl_) return;
    impl_->cache.clear();
    impl_->failures.clear();
}

}  // namespace tmport
