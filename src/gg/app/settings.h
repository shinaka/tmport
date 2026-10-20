#pragma once
// ggport.ini in the preference folder: the player's lasting choices (the command line overrides
// them for one run). Written with its defaults on the first start so it can be edited; values the
// user wrote are kept as they are (save/ini_file.h keeps comments and order).
//
//   [ggport]
//   data = <folder>             the Gizmos & Gadgets folder (empty: GG_DATA, next to the program)
//   sound = fm                  off | speaker | fm
//   cpu = 386                   286 | 386
//   fullscreen = false
//   square_pixels = false       320x200 with square pixels instead of 4:3
//   integer_scale = false
//   window_scale = 0            initial window = n x 320x240 (0: the presenter's default)
//   pause_on_focus_loss = true
//   typematic_delay_ms = 500    the keyboard's repeat delay and period (DOSBox-X's)
//   typematic_rate_ms = 33

#include <cstdint>
#include <filesystem>
#include <string>

#include "gg/app/options.h"

namespace tmport::gg {

struct GgSettings {
    static constexpr const char* kFileName = "ggport.ini";
    static constexpr const char* kSection = "ggport";

    std::string data;
    GgSound sound = GgSound::Fm;
    GgCpu cpu = GgCpu::Cpu386;
    bool fullscreen = false;
    bool squarePixels = false;
    bool integerScale = false;
    int windowScale = 0;
    bool pauseOnFocusLoss = true;
    uint32_t typematicDelayMs = 500;
    uint32_t typematicRateMs = 33;

    // Reads `path`; missing keys and bad values keep their defaults (a bad value is logged).
    // `existed` tells whether the file was there.
    static GgSettings load(const std::filesystem::path& path, bool* existed = nullptr);
    // Writes every key (keeping the user's other lines); atomic, the old file kept as .bak.
    bool save(const std::filesystem::path& path) const;
};

}  // namespace tmport::gg
