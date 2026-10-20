#include "gg/data/gg_files.h"

#include <array>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include "util/error.h"
#include "util/file_util.h"
#include "vfs/directory_source.h"

namespace tmport::gg {

std::string_view describe(GgFolderSource s) {
    switch (s) {
    case GgFolderSource::Explicit: return "given on the command line";
    case GgFolderSource::Environment: return "GG_DATA";
    case GgFolderSource::NearProgram: return "found next to the program";
    }
    return "?";
}

GgFiles::GgFiles(std::filesystem::path folder, GgExe exe, Archive archive)
    : folder_(std::move(folder)), exe_(std::move(exe)), archive_(std::move(archive)) {}

bool GgFiles::looksLikeGgFolder(const std::filesystem::path& folder) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return false;
    const vfs::DirectorySource dir(folder);
    return dir.resolve(kExeName, false) && dir.resolve(Archive::volumeName(0), false);
}

GgFiles GgFiles::open(const std::filesystem::path& folder) {
    const vfs::DirectorySource dir(folder);
    const auto missing = [&](std::string_view name) {
        return MissingFileError(toUpperAscii(name), "The Gizmos & Gadgets folder " + pathToUtf8(folder) + " has no " +
                                                        toUpperAscii(name) +
                                                        " (copy all files of Super Solvers: Gizmos & Gadgets! DOS)");
    };
    const auto read = [&](std::string_view name) {
        const auto p = dir.resolve(name, false);
        if (!p) throw missing(name);
        auto bytes = readWholeFile(*p);
        if (!bytes) throw FormatError(toUpperAscii(name) + ": cannot be read");
        return std::move(*bytes);
    };

    auto exeBytes = read(kExeName);
    std::optional<GgExe> exe;
    try {
        exe.emplace(GgExe::load(std::move(exeBytes)));
    } catch (const std::runtime_error& e) {
        throw FormatError(toUpperAscii(kExeName) + ": " + e.what());
    }

    std::array<std::vector<uint8_t>, Archive::kVolumes> volumes;
    for (int v = 0; v < Archive::kVolumes; ++v) volumes[static_cast<size_t>(v)] = read(Archive::volumeName(v));
    Archive archive(std::move(volumes), LzhDistanceRows::fromExe(*exe));  // checks every volume
    return GgFiles(folder, std::move(*exe), std::move(archive));
}

std::optional<std::filesystem::path> GgFiles::pathOf(std::string_view name) const {
    return vfs::DirectorySource(folder_).resolve(name, false);
}

std::optional<GgFolder> detectGgFolder(const std::optional<std::filesystem::path>& explicitPath,
                                       const std::filesystem::path& programDir) {
    if (explicitPath && !explicitPath->empty()) return GgFolder{*explicitPath, GgFolderSource::Explicit};
    if (const auto env = getEnv("GG_DATA")) {
        const auto p = pathFromUtf8(*env);
        if (GgFiles::looksLikeGgFolder(p)) return GgFolder{p, GgFolderSource::Environment};
    }
    if (programDir.empty()) return std::nullopt;
    std::error_code ec;
    auto dir = std::filesystem::absolute(programDir, ec);
    if (ec) dir = programDir;
    dir = dir.lexically_normal();
    if (!dir.has_filename()) dir = dir.parent_path();  // SDL_GetBasePath ends in a separator
    for (int up = 0; up <= 3; ++up) {
        if (GgFiles::looksLikeGgFolder(dir)) return GgFolder{dir, GgFolderSource::NearProgram};
        if (const auto child = vfs::DirectorySource(dir).resolve(GgFiles::kDefaultFolderName, true))
            if (GgFiles::looksLikeGgFolder(*child)) return GgFolder{*child, GgFolderSource::NearProgram};
        const auto parent = dir.parent_path();
        if (parent.empty() || parent == dir) break;
        dir = parent;
    }
    return std::nullopt;
}

}  // namespace tmport::gg
