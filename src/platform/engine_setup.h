#pragma once
// Glue between tmport.ini (save/settings.h) and the Engine configuration.
//
// Game start-up (done by app/bootstrap.cpp): Paths::detect(), loadSettingsAndImport(paths)
// (one-time import of C:\TLCWIN\TMTWINCD\users.dat), engineConfigFromSettings(), then use
// paths.usersDat for every users.dat access.

#include <filesystem>
#include <string_view>

#include "platform/engine.h"
#include "save/settings.h"

namespace tmport {

// Reads tmport.ini from iniOverride if given, else from the default location (next to the
// executable, else the preference directory). Missing file = defaults. No import is done.
save::EngineSettings loadEngineSettings(const std::filesystem::path& iniOverride = {});

// EngineConfig fields that come from the settings (scaling, fullscreen, pause, input mode, cursors).
EngineConfig engineConfigFromSettings(const save::EngineSettings& s);

// "lossless" / "first-event" -> InputMode; anything else returns fallback.
InputMode parseInputMode(std::string_view text, InputMode fallback);

}  // namespace tmport
