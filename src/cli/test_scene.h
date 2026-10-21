#pragma once
// A fixed test scene for --headless-frame: a room composed like Room_DrawBackground (1198:0234)
// plus keyed, mirrored and clipped sprites and the rectangle primitives.

#include <string>
#include <vector>

#include "data/exe_tables.h"
#include "gfx/frame_buffer.h"
#include "mischief/gfx/palette.h"
#include "res/resource_manager.h"

namespace tmport::cli {

struct TestSceneResult {
    bool ok = true;
    std::vector<std::string> notes;  // what was drawn, plus any self-check failures
};

TestSceneResult composeTestScene(gfx::FrameBuffer& fb, const ResourceManager& res, const ExeTables& exe,
                                 const mischief::gfx::Palette& palette, mischief::gfx::ColorMode mode);

}  // namespace tmport::cli
