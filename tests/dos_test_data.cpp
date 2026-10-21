#include "dos_test_data.h"

#include <memory>
#include <system_error>

#include "mischief/util/file_util.h"

namespace testdata {

std::optional<std::filesystem::path> dosDataPath() {
    if (auto p = mischief::getEnv("TM_DATA_DOS")) return mischief::pathFromUtf8(*p);
    return std::nullopt;
}

const tmport::dos::DosExe* dosExe() {
    static std::unique_ptr<tmport::dos::DosExe> exe;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (auto p = dosDataPath()) exe = std::make_unique<tmport::dos::DosExe>(tmport::dos::DosExe::loadFile(*p / "SST.EXE"));
    }
    return exe.get();
}

const tmport::dos::DosFiles* dosFiles() {
    static std::unique_ptr<tmport::dos::DosFiles> files;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (auto p = dosDataPath()) files = std::make_unique<tmport::dos::DosFiles>(tmport::dos::DosFiles::open(*p));
    }
    return files.get();
}

std::optional<std::filesystem::path> dosCapturesPath() {
    const auto p = mischief::getEnv("TM_DOS_CAPTURES");
    if (!p || p->empty()) return std::nullopt;
    const auto dir = mischief::pathFromUtf8(*p);
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return std::nullopt;
    return dir;
}

std::optional<std::filesystem::path> dosCapture(const std::filesystem::path& relative) {
    const auto dir = dosCapturesPath();
    if (!dir) return std::nullopt;
    const auto file = *dir / relative;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return std::nullopt;
    return file;
}

}  // namespace testdata
