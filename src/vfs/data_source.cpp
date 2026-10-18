#include "vfs/data_source.h"

#include <format>
#include <system_error>

#include "util/error.h"
#include "util/file_util.h"
#include "vfs/cd_image_source.h"
#include "vfs/directory_source.h"

namespace tmport::vfs {

std::vector<uint8_t> File::readRange(uint64_t offset, size_t count) {
    const uint64_t sz = size();
    if (offset >= sz) return {};
    const uint64_t avail = sz - offset;
    if (count > avail) count = static_cast<size_t>(avail);
    std::vector<uint8_t> out(count);
    const size_t got = read(offset, out);
    out.resize(got);
    return out;
}

std::vector<uint8_t> File::readAll() { return readRange(0, static_cast<size_t>(size())); }

std::vector<std::string> splitPath(std::string_view relPath) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : relPath) {
        if (c == '/' || c == '\\') {
            if (!cur.empty() && cur != ".") parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty() && cur != ".") parts.push_back(cur);
    return parts;
}

namespace {

bool isImageExtension(const std::filesystem::path& p) {
    const std::string ext = toLowerAscii(pathToUtf8(p.extension()));
    return ext == ".cue" || ext == ".bin" || ext == ".iso" || ext == ".img";
}

// Case-insensitive check for <dir>/<name> being a regular file.
std::optional<std::filesystem::path> findChild(const std::filesystem::path& dir, std::string_view name, bool wantDir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return std::nullopt;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!iequalsAscii(pathToUtf8(it->path().filename()), name)) continue;
        std::error_code ec2;
        const bool isDir = it->is_directory(ec2);
        if (isDir == wantDir) return it->path();
    }
    return std::nullopt;
}

bool isGameFolder(const std::filesystem::path& dir) { return findChild(dir, kGameExeName, false).has_value(); }

}  // namespace

std::unique_ptr<DataSource> openDataSource(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        if (isGameFolder(path)) return std::make_unique<DirectorySource>(path);
        if (auto sub = findChild(path, kGameFolderName, true); sub && isGameFolder(*sub))
            return std::make_unique<DirectorySource>(*sub);
        throw MissingFileError(std::string(kGameExeName),
                               std::format("'{}' does not contain {} (nor a {} folder with it)", pathToUtf8(path),
                                           kGameExeName, kGameFolderName));
    }
    if (!std::filesystem::is_regular_file(path, ec))
        throw Error(std::format("data path '{}' does not exist", pathToUtf8(path)));
    return std::make_unique<CdImageSource>(path);
}

std::optional<std::filesystem::path> autoDetectDataPath(const std::filesystem::path& exeDir) {
    std::vector<std::filesystem::path> bases;
    std::error_code ec;
    auto addWithParents = [&](std::filesystem::path p, int levels) {
        for (int i = 0; i <= levels && !p.empty(); ++i) {
            bases.push_back(p);
            const auto parent = p.parent_path();
            if (parent == p) break;
            p = parent;
        }
    };
    addWithParents(std::filesystem::current_path(ec), 2);
    if (!exeDir.empty()) addWithParents(exeDir, 3);

    for (const auto& b : bases) {
        if (isGameFolder(b)) return b;
        if (auto sub = findChild(b, kGameFolderName, true); sub && isGameFolder(*sub)) return *sub;
    }
    // A single CD image next to the executable or in the current directory.
    for (const auto& b : bases) {
        std::vector<std::filesystem::path> images;
        for (std::filesystem::directory_iterator it(b, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code ec2;
            if (!it->is_regular_file(ec2)) continue;
            const std::string ext = toLowerAscii(pathToUtf8(it->path().extension()));
            if (ext == ".cue" || ext == ".iso") images.push_back(it->path());
        }
        if (images.size() == 1 && isImageExtension(images[0])) return images[0];
    }
    return std::nullopt;
}

}  // namespace tmport::vfs
