#include "tools/autoplay/autoplay_vision.h"

#include <algorithm>
#include <array>

#include "gfx/frame_buffer.h"

namespace tmport::autoplay {

namespace {
constexpr int kW = gfx::FrameBuffer::kWidth;
constexpr int kBack = gfx::FrameBuffer::kLayer1Y;
}  // namespace

std::vector<SpriteBlob> findSprites(const gfx::FrameBuffer& fb, const std::vector<VisRect>& masks, const VisionParams& p) {
    std::array<int, kW> colCount{};
    std::array<int, kW> colTop{}, colBottom{};
    colTop.fill(1 << 20);
    colBottom.fill(-1);
    const int top = std::max(p.bandTop, gfx::FrameBuffer::kPlayTop);
    const int bottom = std::min(p.bandBottom, gfx::FrameBuffer::kPlayTop + gfx::FrameBuffer::kPlayHeight);
    for (int y = top; y < bottom; ++y) {
        const uint8_t* scr = fb.row(y);
        const uint8_t* back = fb.row(y + kBack);
        for (int x = 0; x < kW; ++x) {
            if (scr[x] == back[x]) continue;
            bool masked = false;
            for (const VisRect& m : masks)
                if (x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h) {
                    masked = true;
                    break;
                }
            if (masked) continue;
            ++colCount[static_cast<size_t>(x)];
            colTop[static_cast<size_t>(x)] = std::min(colTop[static_cast<size_t>(x)], y);
            colBottom[static_cast<size_t>(x)] = std::max(colBottom[static_cast<size_t>(x)], y);
        }
    }
    std::vector<SpriteBlob> out;
    SpriteBlob cur;
    bool open = false;
    int gap = 0;
    auto close = [&] {
        if (open && cur.width() >= p.minWidth && cur.pixels >= p.minPixels) out.push_back(cur);
        open = false;
    };
    for (int x = 0; x < kW; ++x) {
        const size_t k = static_cast<size_t>(x);
        if (colCount[k] >= p.minColumnPixels) {
            if (!open) {
                cur = SpriteBlob{x, x, colTop[k], colBottom[k], 0};
                open = true;
            }
            cur.x1 = x;
            cur.y0 = std::min(cur.y0, colTop[k]);
            cur.y1 = std::max(cur.y1, colBottom[k]);
            cur.pixels += colCount[k];
            gap = 0;
        } else if (open && ++gap > p.maxGap) {
            close();
        }
    }
    close();
    return out;
}

int estimateElfX(const SpriteBlob& b) {
    // Elf frames are drawn at Elf.x (mirrored within the frame when facing left) and their first
    // visible column is at or just after it in every frame (--autoplay-vision-check: error -1..+3
    // for a lone elf); two elves close together merge into one blob (the left one is found).
    return b.x0 - 1;
}

}  // namespace tmport::autoplay
