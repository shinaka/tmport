#pragma once
// What the autoplayer "sees": sprites on the play-field, found by comparing the screen layer of
// the frame buffer with the back page. Room_Run (10e0:007d) keeps the room's clean background
// (picture, spot pictures, sign) in the back page (rows 384 + y) and restores every sprite from
// it in its ERASE phase, so at the start of a tick (the input fetch) every screen pixel that
// differs from the back page belongs to a sprite drawn by the previous tick: the player, the
// elves, the coin pile, the coin sparkle, the sparkle thrown by an elf, the fountain frogs.
//
// The game state does not include the elves (they live in the room loop, which GameMain does not
// expose), so this is how the bot finds them -- the same information a player has.

#include <vector>

namespace tmport::gfx {
class FrameBuffer;
}

namespace tmport::autoplay {

struct VisRect {
    int x = 0, y = 0, w = 0, h = 0;
};

struct SpriteBlob {
    int x0 = 0, x1 = 0;  // first / last column with differing pixels (inclusive)
    int y0 = 0, y1 = 0;  // first / last row
    int pixels = 0;
    int width() const { return x1 - x0 + 1; }
    int centre() const { return (x0 + x1) / 2; }
};

struct VisionParams {
    int bandTop = 150;     // rows of the play-field searched (screen coordinates)
    int bandBottom = 287;  // exclusive
    int minColumnPixels = 2;  // a column belongs to a blob when this many of its pixels differ
    int maxGap = 6;           // columns: blobs closer than this merge
    int minWidth = 14;        // narrower blobs are ignored (sparkles, noise)
    int minPixels = 120;
};

// Blobs of differing pixels in the band, outside the masked rectangles, left to right.
std::vector<SpriteBlob> findSprites(const gfx::FrameBuffer& fb, const std::vector<VisRect>& masks,
                                    const VisionParams& p = {});

// The elf position (Elf.x, the left edge of its 80-px erase rectangle) estimated from a blob.
// Calibrated with --autoplay-vision-check against the room loop's own elf table.
int estimateElfX(const SpriteBlob& b);

}  // namespace tmport::autoplay
