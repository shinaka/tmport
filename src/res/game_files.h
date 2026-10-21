#pragma once
// GameFiles: everything loaded from the user's game folder (data source, EXE tables, DLLs).

#include <filesystem>
#include <memory>

#include "data/exe_tables.h"
#include "mischief/gfx/palette.h"
#include "mischief/vfs/data_source.h"
#include "res/resource_manager.h"

namespace tmport {

struct GameFiles {
    std::unique_ptr<mischief::vfs::DataSource> source;
    std::unique_ptr<ExeTables> exe;
    std::unique_ptr<ResourceManager> res;

    // Opens a directory or CD image. Throws mischief::Error (MissingFileError names the file).
    static GameFiles open(const std::filesystem::path& dataPath,
                          ResourceManager::Options options = ResourceManager::Options{});
    // Palette for a colour mode (RRGB 900 or the VGA 16-colour palette) with the EXE's pen
    // colours (DS:07d8 / DS:0838) applied.
    mischief::gfx::Palette makePalette(mischief::gfx::ColorMode mode) const;
};

}  // namespace tmport
