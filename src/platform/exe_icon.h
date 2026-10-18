#pragma once
// The game's window icon, read from the user's TMTWINCD.EXE at run time.
//
// RegisterMainWindowClass (1018:00a8) gives the window class LoadIcon(hInst, MAKEINTRESOURCE(15000))
// (re/docs/code_1008-1030.md §2.2). The EXE holds RT_GROUP_ICON 15000 with a single 32x32
// 16-colour entry (RT_ICON 1).
//
// Win16 formats (same DIB family as the cursors in input/cursors.h) [O: TMTWINCD.EXE resources]:
//  * RT_GROUP_ICON (type 14): u16 reserved = 0, u16 type = 1, u16 count, then count entries of
//    14 bytes {u8 width, u8 height (0 = 256), u8 colourCount (0 = 256 or more), u8 reserved,
//    u16 planes, u16 bitCount (either may be 0 in old files), u32 bytesInRes, u16 iconId}.
//  * RT_ICON (type 3): a DIB without file header: BITMAPINFOHEADER (40 bytes; biHeight doubled)
//    or BITMAPCOREHEADER (12 bytes), the colour table (RGBQUAD / RGBTRIPLE, for 1/4/8 bpp), the
//    XOR (colour) bitmap, then the 1-bpp AND mask; rows bottom-up, padded to 32 bits.
//  * Pixel = AND 0: the XOR colour; AND 1: transparent (a non-black XOR colour would invert the
//    screen; it is shown transparent, as SDL icons cannot invert).

#include <cstdint>
#include <span>
#include <vector>

struct SDL_Window;

namespace tmport {

namespace ne {
class NeImage;
}

// 1018:00a8 RegisterMainWindowClass: the class icon id.
inline constexpr uint16_t kWindowIconGroupId = 15000;

struct IconImage {
    int width = 0, height = 0;
    int bitCount = 0;           // of the source DIB
    std::vector<uint8_t> rgba;  // width*height*4 (R,G,B,A), rows top to bottom; alpha 0 or 255
    int invertedPixels = 0;     // AND 1 with a non-black XOR colour (shown transparent)

    bool opaqueAt(int x, int y) const { return rgba[(static_cast<size_t>(y) * width + x) * 4 + 3] != 0; }
};

struct IconDirEntry {
    int width = 0, height = 0;  // 0 in the file means 256
    int colourCount = 0;        // 0 in the file means 256 or more
    int planes = 0, bitCount = 0;
    uint32_t bytesInRes = 0;
    uint16_t iconId = 0;
};

// Parses an RT_GROUP_ICON directory. Throws FormatError.
std::vector<IconDirEntry> parseIconDirectory(std::span<const uint8_t> data);
// Decodes one RT_ICON resource (1, 4, 8, 24 or 32 bpp, uncompressed). Throws FormatError.
IconImage decodeIconResource(std::span<const uint8_t> data);
// Loads RT_GROUP_ICON groupId from an NE image and decodes its best entry (largest, then the most
// colours). Throws FormatError / Error when missing.
IconImage loadGroupIcon(const ne::NeImage& image, uint16_t groupId);

// Sets the window icon from an IconImage (SDL copies it). Needs SDL video. False on SDL errors.
bool setWindowIcon(SDL_Window* window, const IconImage& icon);

}  // namespace tmport
