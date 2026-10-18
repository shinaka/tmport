#pragma once
// Access to the user's game data for data-dependent tests.
//   TM_DATA  = game folder (TMTWINCD) or CD image; tests that need game data skip without it.
//   TM_IMAGE = CD image (.cue/.bin/.iso) for the image-vs-directory comparison test.

#include <optional>
#include <string>

#include "res/game_files.h"

namespace testdata {

std::optional<std::string> dataPath();
std::optional<std::string> imagePath();
// Shared GameFiles opened from TM_DATA (nullptr when TM_DATA is unset).
tmport::GameFiles* game();

}  // namespace testdata

// Skips the rest of a test case (with a message) when TM_DATA is not set.
#define TM_REQUIRE_GAME_DATA()                                         \
    auto* g = testdata::game();                                        \
    if (!g) {                                                          \
        MESSAGE("TM_DATA is not set: test skipped");                   \
        return;                                                        \
    }                                                                  \
    (void)g
