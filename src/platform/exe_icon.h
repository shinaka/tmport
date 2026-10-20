#pragma once
// Win16 / ICO icon decoding and the SDL window icon (both games: TM reads its icon from
// TMTWINCD.EXE's NE resources, platform/exe_icon_ne.h; an .ico file has the same entries).
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
// The entry to show: the largest, then the most colours (the first of equals); nullptr if empty.
const IconDirEntry* bestIconEntry(std::span<const IconDirEntry> entries);

// Sets the window icon from an IconImage (SDL copies it). Needs SDL video. False on SDL errors.
bool setWindowIcon(SDL_Window* window, const IconImage& icon);

}  // namespace tmport
