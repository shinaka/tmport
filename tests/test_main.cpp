#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <memory>

#include "mischief/util/file_util.h"
#include "test_data.h"

namespace testdata {

std::optional<std::string> dataPath() { return mischief::getEnv("TM_DATA"); }
std::optional<std::string> imagePath() { return mischief::getEnv("TM_IMAGE"); }

tmport::GameFiles* game() {
    static std::unique_ptr<tmport::GameFiles> files;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (auto p = dataPath()) files = std::make_unique<tmport::GameFiles>(tmport::GameFiles::open(mischief::pathFromUtf8(*p)));
    }
    return files.get();
}

}  // namespace testdata
