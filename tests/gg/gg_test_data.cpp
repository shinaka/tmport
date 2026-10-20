#include "gg_test_data.h"

#include <memory>
#include <system_error>

#include "util/file_util.h"

#ifndef GG_SOURCE_DIR
#define GG_SOURCE_DIR "."
#endif

namespace testdata {

std::filesystem::path repoRoot() { return tmport::pathFromUtf8(GG_SOURCE_DIR); }

std::optional<std::filesystem::path> ggDataPath() {
    if (auto p = tmport::getEnv("GG_DATA")) return tmport::pathFromUtf8(*p);
    const auto local = repoRoot() / "GadgetsAndGizmos";
    if (tmport::gg::GgFiles::looksLikeGgFolder(local)) return local;
    return std::nullopt;
}

const tmport::gg::GgFiles* ggFiles() {
    static std::unique_ptr<tmport::gg::GgFiles> files;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (auto p = ggDataPath()) files = std::make_unique<tmport::gg::GgFiles>(tmport::gg::GgFiles::open(*p));
    }
    return files.get();
}

std::optional<std::filesystem::path> ggCapturesPath() {
    const auto p = tmport::getEnv("GG_CAPTURES");
    if (!p || p->empty()) return std::nullopt;
    const auto dir = tmport::pathFromUtf8(*p);
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return std::nullopt;
    return dir;
}

std::optional<std::filesystem::path> ggCapture(const std::filesystem::path& relative) {
    const auto dir = ggCapturesPath();
    if (!dir) return std::nullopt;
    const auto file = *dir / relative;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return std::nullopt;
    return file;
}

std::filesystem::path scratchDir(const char* name) {
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) / "ggtests" / name;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

}  // namespace testdata
