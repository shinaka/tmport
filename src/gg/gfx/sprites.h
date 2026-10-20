#pragma once
// The sprite list of segment 1581 (re/gg/docs/code_runtime.md §11.3): up to 50 records of 14 bytes
// at DS:05f6 {u16 flags, x, y, w, h, far picture}, count DS:05f4. A frame erases the previous
// frame's sprites from the back page (Sprites_EraseAll), the game adds this frame's (Sprites_Add)
// and draws them in insertion order (Sprites_DrawAll); Dirty_Flush (gg/gfx/video.h) presents.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace tmport::gg {

class Screen;

struct Sprite {
    uint16_t flags = 0;  // bits 0-1 blit mode (Lpx_DrawKeyed), bit 4 = do not draw, bit 7 = do not erase
    int x = 0, y = 0;
    int w = 0, h = 0;    // the erase rectangle
    std::span<const uint8_t> picture;  // LPX payload (kept alive by the caller)
};

class SpriteList {
public:
    static constexpr size_t kCapacity = 50;
    static constexpr uint16_t kNoDraw = 0x10;
    static constexpr uint16_t kNoErase = 0x80;

    // 1581:0484 Sprites_Reset
    void reset() { count_ = 0; }
    // 1581:0496 Sprites_Add: no overflow check in the original (the 51st record would overwrite the
    // write pointer DS:08b2); the engine drops it and logs in debug builds.
    void add(const Sprite& s);
    // 1581:04b6 Sprites_EraseAll: Gfx_RestoreRect(x, y, w, h) of every record, then Sprites_Reset.
    // Quirk kept: a record with bit 7 (do not erase) skips the erase without moving to the next
    // record (04c8 jumps past ADD SI,0Eh), so it and every record after it stay unerased.
    void eraseAll(Screen& screen);
    // 1581:04fc Sprites_DrawAll: Lpx_DrawKeyed(x, y, picture, flags) of every record without bit 4,
    // in insertion order.
    void drawAll(Screen& screen) const;

    size_t count() const { return count_; }
    const Sprite& operator[](size_t i) const { return list_[i]; }

private:
    std::array<Sprite, kCapacity> list_{};
    size_t count_ = 0;
};

}  // namespace tmport::gg
