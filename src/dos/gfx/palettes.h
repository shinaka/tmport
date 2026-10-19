#pragma once
// The four looks of the 1990 DOS edition and the conversion of the back buffer to RGB
// (re/dos/docs/fmt_graphics.md §2.2). The back buffer holds logical colours 0..15; each adapter
// shows them through a table in SST.EXE's DGROUP:
//   EGA    DS:3C4A  16 attribute registers; mode 0Eh is a 200-line mode, so the monitor reads them as
//                   RGBI (bit 0 B, 1 G, 2 R, bit 4 = intensity), standard RGBI colours (brown for 6)
//   Tandy  DS:3D1B  16 palette registers, IRGB nibbles: the same colours as EGA for every entry
//   MCGA   DS:0144  16 x (r,g,b) 6-bit DAC values, expanded to 8 bits as (v*255+31)/63, which is what
//                   DOSBox-X shows (v*255/63 is one lower in some channels)
//   CGA    DS:0174  16 bytes of four 2-bit pixels each, shown in mode 4 palette 1 high intensity
//                   (black, light cyan, light magenta, white)
//
// Pixel convention of the RGBA output (the same as gfx::Presenter's canvas): 320x200 pixels, rows
// top to bottom, 4 bytes per pixel in the order R, G, B, A (A = 255); as a packed uint32 that is
// R | G << 8 | B << 16 | A << 24 (little endian, R at the lowest address), like gfx::Palette::rgba().
// No aspect correction: the original's pixels are 1.2 times taller than wide (4:3 at 320x240).

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "util/rgb.h"

namespace tmport::dos {

class DosExe;
class BackBuffer;

enum class Look { Ega, Tandy, Mcga, Cga };

std::optional<Look> parseLook(std::string_view name);  // "ega", "tandy", "mcga", "cga" (any case)
std::string_view lookName(Look look);

// The 16 colours of an RGBI display (index = I<<3 | R<<2 | G<<1 | B; colour 6 shown as brown).
// Hardware fact, not game data.
const std::array<Rgb8, 16>& rgbiColours();
// CGA mode 4, palette 1, high intensity.
const std::array<Rgb8, 4>& cgaColours();

struct LookPalette {
    Look look = Look::Ega;
    std::array<Rgb8, 16> colours{};      // logical colour -> displayed colour (CGA: the dither's average is not used)
    std::array<uint8_t, 16> cgaDither{};  // DS:0174 (filled for every look, used by Look::Cga)

    // Displayed colour of logical colour c at screen pixel (x, y).
    Rgb8 at(uint8_t c, int x, int y) const;
};

// Reads the tables from SST.EXE. Never fails for a validated DosExe.
LookPalette lookPalette(const DosExe& exe, Look look);

// CGA: the 2-bit pixel that logical colour c becomes at pixel (x, y) by the tile rule.
// Gfx_BuildCgaColorMap (1000:1087) turns each entry (pixels a b c d from the most significant bits)
// into XLAT tables used by Tile_DrawColumn (1000:1a33): rows alternate between a b c d (the first
// row of a column, even y on the play field and the title) and b a d c, by pixel x mod 4. The other
// routines have their own phases (back_buffer.h, the CGA plane).
int cgaPixel(const std::array<uint8_t, 16>& dither, uint8_t c, int x, int y);

// Turns on the CGA plane of a game's two buffers when the look is CGA (BackBuffer::enableCga): a
// DosGame member declared after its palette, so the buffers keep the original's 2-bpp picture from
// the first drawing on.
struct CgaBinding {
    CgaBinding(BackBuffer& back, BackBuffer& screen, const LookPalette& pal);
};

// The whole buffer as RGBA bytes (see the pixel convention above). CGA: the buffer's CGA plane when
// it has one (BackBuffer::enableCga), else the tile rule (cgaPixel) for every pixel.
std::vector<uint8_t> toRgba(const BackBuffer& buf, const LookPalette& pal);
void toRgba(const BackBuffer& buf, const LookPalette& pal, std::span<uint8_t> out);  // out: 320*200*4 bytes
// The same as packed pixels (R | G << 8 | B << 16 | A << 24, SDL_PIXELFORMAT_RGBA32), the form
// gfx::Presenter::presentFrame takes.
std::vector<uint32_t> toRgba32(const BackBuffer& buf, const LookPalette& pal);
void toRgba32(const BackBuffer& buf, const LookPalette& pal, std::span<uint32_t> out);  // out: 320*200

}  // namespace tmport::dos
