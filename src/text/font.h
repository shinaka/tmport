#pragma once
// FFNT bitmap font (TMT.DLL Mac type 'FFNT', Win type 32514, id 999) and its glyph blitter
// (segments 1060/1068; re/docs/fmt_bndl_ffnt.md §3, code_1058-1090.md §2.5 and §5).
//
// Layout of the resource body:
//   +0        u8  unused by the EXE (probably the Mac "first char")
//   +1        u8  last  -> N = last + 1 entries per table
//   +2        u8  height (rows per glyph = line height)
//   +3        u8  width[N]   advance = bitmap width (spacing built in)
//   +3+N      u8  offLo[N]
//   +3+2N     u8  offHi[N]
//   +3+3N     bitmap pool; glyph c at offHi[c] << 8 | offLo[c], `height` rows of ceil(width/8)
//             bytes, MSB = leftmost pixel, 1 = paint in the pen colour, 0 = transparent.
// Tables are indexed directly by the byte value (byte 0 is not subtracted). The game indexes the
// font by raw byte; text is never transcoded (0x80..0x95 are key-cap glyphs).

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace tmport {
class ResourceManager;
namespace gfx {
class FrameBuffer;
}
}  // namespace tmport

namespace tmport::text {

inline constexpr int kGameFontId = 999;  // 1068:0024 Font_Init loads FFNT 999 into slot 0

class Font {
public:
    // Parses an FFNT body (trailing NE padding allowed). Throws FormatError when the tables or a
    // glyph bitmap do not fit.
    static Font parse(std::span<const uint8_t> data);
    // Loads FFNT `id` from TMT.DLL. Throws MissingFileError/FormatError.
    static Font load(const ResourceManager& res, int id = kGameFontId);

    // 1068:00d9 Font_GetHeight
    int height() const { return height_; }
    // Number of table entries (last + 1).
    int glyphCount() const { return static_cast<int>(widths_.size()); }
    // 1068:00dd Font_CharWidth: the advance of byte c (0 for bytes past the table; the shipped
    // font has all 256 entries).
    int charWidth(uint8_t c) const { return c < widths_.size() ? widths_[c] : 0; }
    // True if pixel (x, y) of glyph c is set.
    bool pixel(uint8_t c, int x, int y) const;
    // True if glyph c has at least one set pixel.
    bool hasInk(uint8_t c) const;

    // 1068:00f6 Font_DrawChar -> 1060:01ef (8 bpp) / 1060:0000 (4 bpp, expanded here): paints the
    // set bits of glyph c with the frame buffer's current pen (Pal_SetForeColor), cell origin at
    // the top-left, and returns the advance. The original does not clip and registers no dirty
    // rectangle; here pixels outside the buffer are skipped (memory safety only). A glyph of
    // width 0 draws nothing.
    int drawChar(gfx::FrameBuffer& fb, int x, int y, uint8_t c) const;

private:
    int height_ = 0;
    std::vector<uint8_t> widths_;
    std::vector<uint16_t> offsets_;
    std::vector<uint8_t> bits_;  // bitmap pool
};

}  // namespace tmport::text
