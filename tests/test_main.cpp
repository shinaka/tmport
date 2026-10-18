#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <memory>

#include "test_data.h"
#include "util/file_util.h"

namespace testdata {

std::optional<std::string> dataPath() { return tmport::getEnv("TM_DATA"); }
std::optional<std::string> imagePath() { return tmport::getEnv("TM_IMAGE"); }

tmport::GameFiles* game() {
    static std::unique_ptr<tmport::GameFiles> files;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (auto p = dataPath()) files = std::make_unique<tmport::GameFiles>(tmport::GameFiles::open(tmport::pathFromUtf8(*p)));
    }
    return files.get();
}

}  // namespace testdata
