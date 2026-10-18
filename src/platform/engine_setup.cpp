#include "platform/engine_setup.h"

namespace tmport {

save::EngineSettings loadEngineSettings(const std::filesystem::path& iniOverride) {
    std::filesystem::path ini = iniOverride;
    if (ini.empty()) ini = save::Paths::detect().iniPath;
    if (ini.empty()) return {};
    auto file = save::IniFile::load(ini);
    return file ? save::EngineSettings::fromIni(*file) : save::EngineSettings{};
}

EngineConfig engineConfigFromSettings(const save::EngineSettings& s) {
    EngineConfig c;
    c.windowScale = s.windowScale;
    c.integerScale = s.integerScale;
    c.fullscreen = s.fullscreen;
    c.pauseOnFocusLoss = s.pauseOnFocusLoss;
    c.cursorScale = s.cursorScale;
    c.inputMode = s.inputMode == save::InputModeSetting::FirstEvent ? InputMode::FirstEventPerDrain : InputMode::Lossless;
    c.tapLatch = s.tapLatch;
    return c;
}

InputMode parseInputMode(std::string_view text, InputMode fallback) {
    if (text == "lossless") return InputMode::Lossless;
    if (text == "first-event") return InputMode::FirstEventPerDrain;
    return fallback;
}

}  // namespace tmport
