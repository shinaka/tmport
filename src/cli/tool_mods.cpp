#include "cli/tool_mods.h"

#include <filesystem>

#include "mods/mods.h"
#include "util/file_util.h"
#include "util/log.h"

namespace tmport::cli {

size_t applyToolMods(GameFiles& files, const std::vector<std::string>& dirs) {
    if (dirs.empty()) return 0;
    std::vector<std::filesystem::path> paths;
    for (const auto& d : dirs) {
        const auto p = pathFromUtf8(d);
        std::error_code ec;
        if (!std::filesystem::is_directory(p, ec)) log::warn("--mods: {} is not a folder (ignored)", d);
        paths.push_back(p);
    }
    // applyMods logs its summary, the warnings and the errors itself.
    return mods::applyMods(files, paths).folders.size();
}

}  // namespace tmport::cli
