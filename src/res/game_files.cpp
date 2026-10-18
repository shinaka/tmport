#include "res/game_files.h"

namespace tmport {

GameFiles GameFiles::open(const std::filesystem::path& dataPath, ResourceManager::Options options) {
    GameFiles g;
    g.source = vfs::openDataSource(dataPath);
    g.exe = ExeTables::load(*g.source);
    g.res = std::make_unique<ResourceManager>(*g.source, options);
    return g;
}

gfx::Palette GameFiles::makePalette(gfx::ColorMode mode) const {
    gfx::Palette p = mode == gfx::ColorMode::Colour16 ? gfx::Palette::vga16() : res->palette(900);
    p.setPens(exe->egaColours(), exe->vga16Index());
    return p;
}

}  // namespace tmport
