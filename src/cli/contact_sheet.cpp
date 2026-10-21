#include "cli/contact_sheet.h"

#include <algorithm>
#include <format>
#include <string>

namespace tmport::cli {

namespace {

constexpr int kLabelHeight = 9;

// 3x5 glyphs, 3 bits per row (bit 2 = left column).
const uint8_t* glyph(char c) {
    static const uint8_t digits[10][5] = {
        {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
        {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
    };
    static const uint8_t x[5] = {0, 5, 2, 5, 0};
    static const uint8_t minus[5] = {0, 0, 7, 0, 0};
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c == 'x') return x;
    if (c == '-') return minus;
    return nullptr;
}

void putPixel(SheetImage& img, int x, int y, uint32_t rgba) {
    if (x < 0 || y < 0 || x >= img.width || y >= img.height) return;
    uint8_t* p = img.rgba.data() + (static_cast<size_t>(y) * static_cast<size_t>(img.width) + static_cast<size_t>(x)) * 4;
    p[0] = static_cast<uint8_t>(rgba);
    p[1] = static_cast<uint8_t>(rgba >> 8);
    p[2] = static_cast<uint8_t>(rgba >> 16);
    p[3] = static_cast<uint8_t>(rgba >> 24);
}

constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) | 0xFF000000u;
}

}  // namespace

void drawTinyText(SheetImage& img, int x, int y, const char* text, uint32_t color) {
    for (; *text; ++text, x += 4) {
        const uint8_t* g = glyph(*text);
        if (!g) continue;
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 3; ++c)
                if (g[r] & (4 >> c)) putPixel(img, x + c, y + r, color);
    }
}

SheetImage renderContactSheet(const std::vector<SheetItem>& items, const mischief::gfx::Palette& palette, bool transparentKey,
                              int cell, int columns) {
    SheetImage img;
    const int n = static_cast<int>(items.size());
    const int cols = std::max(1, std::min(columns, std::max(n, 1)));
    const int rows = std::max(1, (n + cols - 1) / cols);
    img.width = cols * cell;
    img.height = rows * (cell + kLabelHeight);
    img.rgba.assign(static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 4, 0);
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) putPixel(img, x, y, rgb(20, 20, 24));

    for (int k = 0; k < n; ++k) {
        const int cx = (k % cols) * cell;
        const int cy = (k / cols) * (cell + kLabelHeight);
        // Checkerboard background.
        for (int y = 0; y < cell; ++y)
            for (int x = 0; x < cell; ++x)
                putPixel(img, cx + x, cy + y, ((x / 8 + y / 8) & 1) ? rgb(64, 64, 74) : rgb(48, 48, 56));
        const gfx::PixMap* pm = items[static_cast<size_t>(k)].pixmap;
        std::string label = std::to_string(items[static_cast<size_t>(k)].id);
        if (pm && !pm->empty()) {
            label += std::format(" {}x{}", pm->width, pm->height);
            // Fit into cell-4 pixels (nearest-neighbour scaling, never enlarged beyond 2x).
            const double scale = std::min({(cell - 4.0) / pm->width, (cell - 4.0) / pm->height, 2.0});
            const int dw = std::max(1, static_cast<int>(pm->width * scale));
            const int dh = std::max(1, static_cast<int>(pm->height * scale));
            const int ox = cx + (cell - dw) / 2, oy = cy + (cell - dh) / 2;
            for (int y = 0; y < dh; ++y) {
                const int sy = std::min(pm->height - 1, static_cast<int>(y / scale));
                for (int x = 0; x < dw; ++x) {
                    const int sx = std::min(pm->width - 1, static_cast<int>(x / scale));
                    const uint8_t v = pm->at(sx, sy);
                    if (transparentKey && palette.keyTable()[v]) continue;
                    putPixel(img, ox + x, oy + y, palette.rgba()[v]);
                }
            }
        }
        drawTinyText(img, cx + 2, cy + cell + 2, label.c_str(), rgb(230, 230, 230));
    }
    return img;
}

}  // namespace tmport::cli
