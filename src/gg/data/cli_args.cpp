#include "gg/data/cli_args.h"

#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <stdexcept>
#include <system_error>

#include "util/file_util.h"

namespace tmport::gg::cli {

std::optional<std::string> optionValue(const std::vector<std::string>& args, std::string_view name) {
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == name) return args[i + 1];
    return std::nullopt;
}

std::vector<std::string> optionValues(const std::vector<std::string>& args, std::string_view name) {
    std::vector<std::string> v;
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == name) v.push_back(args[i + 1]);
    return v;
}

bool hasFlag(const std::vector<std::string>& args, std::string_view name) {
    return std::find(args.begin(), args.end(), name) != args.end();
}

std::filesystem::path programDir() {
    const char* base = SDL_GetBasePath();  // owned by SDL
    return base ? pathFromUtf8(base) : std::filesystem::path{};
}

GgFiles openFolder(const std::vector<std::string>& args) {
    std::optional<std::filesystem::path> explicitPath;
    if (const auto d = optionValue(args, "--data")) explicitPath = pathFromUtf8(*d);
    const auto folder = detectGgFolder(explicitPath, programDir());
    if (!folder) throw std::runtime_error("no Gizmos & Gadgets folder found (give --data DIR or set GG_DATA)");
    return GgFiles::open(folder->path);
}

bool insideFolder(const std::filesystem::path& file, const std::filesystem::path& folder) {
    std::error_code ec;
    const auto f = std::filesystem::weakly_canonical(std::filesystem::absolute(file, ec), ec);
    const auto d = std::filesystem::weakly_canonical(std::filesystem::absolute(folder, ec), ec);
    const auto fs = toLowerAscii(pathToUtf8(f)), ds = toLowerAscii(pathToUtf8(d));
    return fs == ds || (fs.size() > ds.size() && fs.starts_with(ds) && (fs[ds.size()] == '/' || fs[ds.size()] == '\\'));
}

}  // namespace tmport::gg::cli
