#pragma once
// The sprite blitter of the 1990 DOS edition (Sprite_Draw 1000:1b8d, Sprite_SaveUnder 1000:2056;
// re/dos/docs/fmt_graphics.md §5.3) and the z-sorted actor renderer of anim.c
// (Sprites_DrawSorted 4306:0497, Sprites_EraseAll 4306:06dd; code_player.md §3.5).
//
// Coordinates: x in bytes of the back buffer (2 pixels), y in rows. A frame's header offsets are a
// hotspot added to the draw position (for HUD, panel and title frames they are absolute screen
// positions, drawn at (0, 0)). Clipping uses the back buffer's clip window (DS:37DA..37E0).

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "dos/gfx/back_buffer.h"

namespace tmport::dos {

struct PakFrame;
class PakFile;

// Draw flags (AL of Sprite_Draw).
namespace sprite_flags {
inline constexpr uint8_t kOpaque = 0x00;      // rows copied as they are, colour 0 included
inline constexpr uint8_t kCgaNormal = 0x01;   // forces the 4-bpp path in CGA mode (save-under restores); no effect here
inline constexpr uint8_t kReverse = 0x10;     // each row reversed with its nibbles swapped (no placement change)
inline constexpr uint8_t kMirror = 0x20;      // reversed rows, placed at x - (width + x offset)
inline constexpr uint8_t kMasked = 0x40;      // colour 0 transparent (mask table 2005:8600)
inline constexpr uint8_t kNoHotspot = 0x80;   // draw at (x, y) exactly
}  // namespace sprite_flags

// A rectangle of packed rows with a hotspot: a PAK frame or a saved background.
struct SpriteImage {
    int widthBytes = 0, height = 0, xOffset = 0, yOffset = 0;
    const uint8_t* rows = nullptr;  // height * widthBytes bytes
    static SpriteImage of(const PakFrame& f);
};

// The part of the buffer a draw touched (after clipping), in bytes and rows.
struct DrawnArea {
    int x = 0, y = 0, widthBytes = 0, height = 0;
};

// 1000:1b8d Sprite_Draw: draws `img` at (x, y) with `flags` through buf.clip(); returns the drawn
// area (also recorded as dirty when DS:37AE bit 0 is set), nothing when the image is clipped away.
std::optional<DrawnArea> drawSprite(BackBuffer& buf, const SpriteImage& img, int x, int y, uint8_t flags);
std::optional<DrawnArea> drawSprite(BackBuffer& buf, const PakFrame& frame, int x, int y, uint8_t flags);

// A background saved by Sprite_SaveUnder: an opaque frame whose hotspot is its own position.
struct SavedRect {
    int x = 0, y = 0, widthBytes = 0, height = 0;
    std::vector<uint8_t> bytes;
    SpriteImage image() const { return {widthBytes, height, x, y, bytes.data()}; }
    // Bytes the original's save heap uses for it: a 16-byte frame header and a length byte per row.
    int heapBytes() const { return 16 + height * (widthBytes + 1); }
};
// 1000:2056 Sprite_SaveUnder (SI x, DI y, CX width, DX height): the clipped rectangle, nothing if it
// is clipped away. Restored with drawSprite(save.image(), 0, 0, kCgaNormal).
std::optional<SavedRect> saveUnder(const BackBuffer& buf, int x, int y, int widthBytes, int height);

// One entry of the draw-record table DS:9145 (14 bytes in the original).
struct DrawRecord {
    int16_t x = 0, y = 0;     // screen position (bytes, rows); for a child an offset from its owner
    int16_t z = 0;            // depth: drawn in descending order, ties to the higher index
    uint16_t frame = 0xFFFF;  // sprite index * 2 + mirror bit; 0xFFFF = nothing
    uint16_t owner = 0;       // record whose position this one is relative to (itself = absolute); bit 15 = drawn this pass
    std::optional<SavedRect> save;  // background under the sprite (+0A/+0C far pointer)
};

// The actor renderer: the draw records, their order and the save heap bookkeeping.
class SpriteRenderer {
public:
    static constexpr int kRecords = 13;

    SpriteRenderer() { reset(); }
    // The record part of Anim_InitSlots (4306:02c0): x = y = z = 0, frame 0xFFFF, owner = own index,
    // no save; draw order i; sprites unchanged; save heap reset pending.
    void reset();

    // 4306:0497 Sprites_DrawSorted(from, to): runs only if spritesChanged or `dirty` (g_dirty != 0).
    // Clip: full screen in title mode (g_uiMode == 0x80), else the play field; a frame code < 6 (the
    // pointer frames) opens it to the full screen for the rest of the pass. For each record picked
    // (largest z >= 0 not yet drawn, ties to the higher index; children offset by their owner's
    // record) the background is saved, then the frame of `pak` is drawn masked (mirrored when bit 0
    // of the frame code is set). Returns how many sprites were drawn (the original adds 1 to g_dirty
    // for each). Leaves the buffer's clip window at the full screen.
    int drawSorted(BackBuffer& buf, const PakFile& pak, int from, int to, bool titleMode, bool dirty);
    // 4306:06dd Sprites_EraseAll(from, to): only if spritesChanged; restores the saved backgrounds of
    // draw order entries (to - from - 1) down to 0 (indexed from 0, not from `from`), then clears the
    // drawn marks.
    void eraseAll(BackBuffer& buf, int from, int to);

    std::array<DrawRecord, kRecords> records{};  // DS:9145
    std::array<int, kRecords> drawOrder{};       // DS:92DC
    bool spritesChanged = false;                 // DS:92F6
    bool saveHeapReset = true;                   // DS:92F8
    // Save heap: DS:37CC (start after a reset), DS:37C8 / DS:37CA (current far pointer) and
    // g_saveHeapHigh DS:929F (the low byte is USERS.DAT header byte 1). The original sets the
    // segment to 1AE7 at each reset; with a non-zero segment DS:929F gets the heap end after each
    // save (only with segment 0 would it be a maximum).
    uint16_t heapBase = 0, heapOffset = 0, heapSegment = 0, saveHeapHigh = 0;
};

}  // namespace tmport::dos
