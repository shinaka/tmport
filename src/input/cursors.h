#pragma once
// Mouse cursors (segment 1020 "cursor.cpp"; re/docs/code_1008-1030.md §6.4).
//
// The EXE carries three Win16 cursors: RT_GROUP_CURSOR 128, 129, 130 (the game loads the id list
// at DS:1292 = {128, 129, 130, 0xFFFF}); 129/130 are the walk-left/walk-right cursors chosen by
// the mouse-move hook (input/cursor_controller.h).
//
// Win16 formats (GAPS.md P1-5) [O: TMTWINCD.EXE resources]:
//  * RT_GROUP_CURSOR (type 12): u16 reserved = 0, u16 type = 2, u16 count, then count entries of
//    14 bytes {u16 width, u16 height (AND+XOR, i.e. doubled), u16 planes, u16 bitCount,
//    u32 bytesInRes, u16 cursorId}.
//  * RT_CURSOR (type 1): u16 hotX, u16 hotY, then a DIB: BITMAPINFOHEADER (40 bytes; biHeight
//    doubled) or BITMAPCOREHEADER (12 bytes), a 2-entry colour table, the XOR bitmap and then
//    the AND bitmap, both 1 bpp, rows bottom-up, padded to 32 bits.
//  * Pixel = AND 0/XOR 0 black, AND 0/XOR 1 white, AND 1/XOR 0 transparent, AND 1/XOR 1 inverted.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

struct SDL_Cursor;

namespace tmport {

namespace ne {
class NeImage;
}

struct CursorImage {
    enum Pixel : uint8_t { kBlack = 0, kWhite = 1, kTransparent = 2, kInvert = 3 };

    int width = 0, height = 0;
    int hotX = 0, hotY = 0;
    std::vector<uint8_t> pixels;  // width*height Pixel values, rows top to bottom
    std::vector<uint8_t> andMask;  // 1 bpp, MSB first, (width+7)/8 bytes per row, top to bottom
    std::vector<uint8_t> xorMask;

    Pixel at(int x, int y) const { return static_cast<Pixel>(pixels[static_cast<size_t>(y * width + x)]); }
    // RGBA (R,G,B,A per pixel) at an integer scale; inverted pixels are drawn black (SDL colour
    // cursors cannot invert; none of the shipped cursors uses inversion).
    std::vector<uint8_t> rgba(int scale = 1) const;
};

// Decodes one RT_CURSOR resource. Throws tmport::FormatError.
CursorImage decodeCursorResource(std::span<const uint8_t> data);
// Loads RT_GROUP_CURSOR groupId from an NE image and decodes the cursor it names (the first 1-bpp
// entry, else the first entry). Throws tmport::FormatError / tmport::Error when missing.
CursorImage loadGroupCursor(const ne::NeImage& image, uint16_t groupId);

// The cursor table and the selection calls of segment 1020. Decoding is headless; SDL cursors
// exist only while attached (window open).
class CursorSet {
public:
    static constexpr int kMaxCursors = 15;  // g_cursors[15] (DS:3d64)
    enum class Shown { Default, Arrow, Wait, Custom };

    CursorSet() = default;
    ~CursorSet();
    CursorSet(const CursorSet&) = delete;
    CursorSet& operator=(const CursorSet&) = delete;

    // 1020:0000 Cursor_LoadSet: decodes up to 15 group cursors; the list stops at 0xFFFF. An id
    // that is missing or cannot be decoded leaves its slot empty (like a NULL LoadCursor).
    void load(const ne::NeImage& image, std::span<const uint16_t> ids);
    // 1020:005c Cursor_FreeSet.
    void freeSet();
    const CursorImage* image(int idx) const;
    int loadedCount() const;

    // 1020:008b Cursor_Select: shows cursor idx and returns the previous index (-1 initially).
    // An empty slot shows the system arrow (the original's SetCursor(NULL) would hide it).
    int select(int idx);
    // 1020:00de Cursor_SetArrow / 1020:00f2 Cursor_SetWait (system cursors; the current index is
    // not changed, as in the original).
    void setArrow();
    void setWait();

    int currentIndex() const { return curIndex_; }  // DS:06e9
    Shown shown() const { return shown_; }
    int shownIndex() const { return shownIndex_; }  // valid when shown() == Custom

    // --- SDL side ----------------------------------------------------------------------------------
    // Creates the SDL cursors (scale = integer magnification of the 32x32 images) and shows the
    // current shape. Needs the SDL video subsystem.
    void attach(int scale);
    void detach();
    void setScale(int scale);
    int scale() const { return scale_; }
    bool attached() const { return attached_; }

private:
    void createSdlCursors();
    void destroySdlCursors();
    void apply();

    std::array<std::optional<CursorImage>, kMaxCursors> images_{};
    std::array<SDL_Cursor*, kMaxCursors> sdl_{};
    SDL_Cursor* arrow_ = nullptr;
    SDL_Cursor* wait_ = nullptr;
    int curIndex_ = -1;
    Shown shown_ = Shown::Default;
    int shownIndex_ = -1;
    bool attached_ = false;
    int scale_ = 1;
};

}  // namespace tmport
