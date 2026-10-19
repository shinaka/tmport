#include "dos_test_data.h"

#include <memory>

#include "util/file_util.h"

namespace testdata {

std::optional<std::filesystem::path> dosDataPath() {
    if (auto p = tmport::getEnv("TM_DATA_DOS")) return tmport::pathFromUtf8(*p);
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

}  // namespace testdata
