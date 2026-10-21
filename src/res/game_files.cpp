#include "res/game_files.h"

namespace tmport {

GameFiles GameFiles::open(const std::filesystem::path& dataPath, ResourceManager::Options options) {
    GameFiles g;
    g.source = mischief::vfs::openDataSource(dataPath);
    g.exe = ExeTables::load(*g.source);
    g.res = std::make_unique<ResourceManager>(*g.source, options);
    return g;
}

mischief::gfx::Palette GameFiles::makePalette(mischief::gfx::ColorMode mode) const {
    mischief::gfx::Palette p = mode == mischief::gfx::ColorMode::Colour16 ? mischief::gfx::Palette::vga16() : res->palette(900);
    p.setPens(exe->egaColours(), exe->vga16Index());
    return p;
}

}  // namespace tmport
