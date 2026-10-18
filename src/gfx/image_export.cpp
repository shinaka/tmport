#include "gfx/image_export.h"

#include "util/file_util.h"
#include "util/png_writer.h"

namespace tmport::gfx {

std::vector<uint8_t> pixmapToPng(const PixMap& pm, const Palette& palette, bool transparentKey) {
    const int n = palette.size();
    std::vector<Rgb8> pal(palette.colours().begin(), palette.colours().begin() + n);
    std::vector<uint8_t> alpha;
    if (transparentKey) {
        alpha.assign(static_cast<size_t>(n), 255);
        for (int i = 0; i < n; ++i)
            if (palette.keyTable()[static_cast<size_t>(i)]) alpha[static_cast<size_t>(i)] = 0;
    }
    return png::encodeIndexed(pm.width, pm.height, pm.pixels, pal, alpha);
}

bool writePixmapPng(const std::filesystem::path& path, const PixMap& pm, const Palette& palette, bool transparentKey) {
    return writeWholeFile(path, pixmapToPng(pm, palette, transparentKey));
}

std::vector<uint8_t> pixmapToRgba(const PixMap& pm, const Palette& palette, bool transparentKey) {
    std::vector<uint8_t> out(pm.pixels.size() * 4);
    for (size_t i = 0; i < pm.pixels.size(); ++i) {
        const uint8_t v = pm.pixels[i];
        const Rgb8& c = palette.colour(v);
        out[i * 4 + 0] = c.r;
        out[i * 4 + 1] = c.g;
        out[i * 4 + 2] = c.b;
        out[i * 4 + 3] = (transparentKey && palette.keyTable()[v]) ? 0 : 255;
    }
    return out;
}

}  // namespace tmport::gfx
