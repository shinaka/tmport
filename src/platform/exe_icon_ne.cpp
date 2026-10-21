#include "platform/exe_icon_ne.h"

#include <format>

#include "mischief/util/error.h"
#include "ne/ne_image.h"

namespace tmport {

namespace {

constexpr uint16_t kRtIcon = 3;
constexpr uint16_t kRtGroupIcon = 14;

}  // namespace

mischief::IconImage loadGroupIcon(const ne::NeImage& image, uint16_t groupId) {
    const ne::Resource* g = image.findResource(kRtGroupIcon, groupId);
    if (!g) throw mischief::Error(std::format("{}: RT_GROUP_ICON {} not found", image.name(), groupId));
    const auto entries = mischief::parseIconDirectory(image.readResource(*g));
    if (entries.empty()) throw mischief::FormatError(std::format("RT_GROUP_ICON {}: empty", groupId));
    const mischief::IconDirEntry* best = mischief::bestIconEntry(entries);
    const ne::Resource* res = image.findResource(kRtIcon, best->iconId);
    if (!res) throw mischief::Error(std::format("{}: RT_ICON {} (group {}) not found", image.name(), best->iconId, groupId));
    auto data = image.readResource(*res);
    if (best->bytesInRes != 0 && best->bytesInRes <= data.size()) data.resize(best->bytesInRes);  // NE padding
    return mischief::decodeIconResource(data);
}

}  // namespace tmport
