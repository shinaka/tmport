#include "anim/anim_bundle.h"

#include <format>

#include "mischief/util/error.h"
#include "mischief/util/log.h"

namespace tmport::anim {

namespace {

BundleInfo requireBundle(const ResourceManager& res, int bndlId) {
    auto b = res.bundle(bndlId);
    if (!b) throw mischief::Error(std::format("animation bundle BNDL {} not found", bndlId));
    return std::move(*b);
}

std::shared_ptr<const gfx::PixMap> loadPixmap(const ResourceManager& res, int bndlId, int id) {
    auto pm = res.pixmap(id);
    if (!pm) mischief::log::warn("BNDL {}: member PMAP {} not found; its frame slot stays empty", bndlId, id);
    return pm;
}

}  // namespace

ScriptPtr loadScript(const ResourceManager& res, int id) {
    auto bytes = res.loadRaw(ResType::OTHR, id);
    if (!bytes) throw mischief::Error(std::format("DAM script OTHR {} not found", id));
    ScriptPtr s = makeScript(id, std::move(*bytes));
    if (!s->hasDamTag() && !s->isDop()) mischief::log::warn("OTHR {} does not start with the DAM tag", id);
    return s;
}

// 10d8:0000 Anim_LoadBundle
LoadedBundle loadBundle(const ResourceManager& res, AnimEngine& anim, int bndlId, int frameBase) {
    const BundleInfo b = requireBundle(res, bndlId);
    LoadedBundle out;
    out.bndlId = bndlId;
    out.memberIds = b.ids;
    if (b.ids.empty()) {
        // Bndl_GetEntry(bndl, 0) of an empty bundle: no script; the count is -1.
        out.frameCount = -1;
        return out;
    }
    out.script = loadScript(res, b.ids[0]);
    out.frameCount = static_cast<int>(b.ids.size()) - 1;
    for (int i = 0; i < out.frameCount; ++i)
        anim.setFrame(frameBase + i, loadPixmap(res, bndlId, b.ids[static_cast<size_t>(i) + 1]));
    return out;
}

// 1190:009b (frame loop)
int loadFrames(const ResourceManager& res, AnimEngine& anim, int bndlId, int frameBase) {
    const BundleInfo b = requireBundle(res, bndlId);
    for (size_t i = 0; i < b.ids.size(); ++i)
        anim.setFrame(frameBase + static_cast<int>(i), loadPixmap(res, bndlId, b.ids[i]));
    return static_cast<int>(b.ids.size());
}

std::shared_ptr<const gfx::PixMap> bundleMember(const ResourceManager& res, int bndlId, int index) {
    auto b = res.bundle(bndlId);
    if (!b || index < 0 || static_cast<size_t>(index) >= b->ids.size()) return nullptr;
    return res.pixmap(b->ids[static_cast<size_t>(index)]);
}

}  // namespace tmport::anim
