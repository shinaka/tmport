#pragma once
// Text layout of the 1990 DOS edition: Text_Draw 43cb:0b63 and Text_Width 43cb:0cfc of ui.c
// (re/dos/docs/code_ui.md §3), and Hud_DrawNumber 130f:0d00. Strings are the game's bytes (from
// SST.EXE, HELP.DAT, ALL_PUZS, USERS.DAT); processing stops at a NUL or at the end of the view.
//
// Control codes of Text_Draw (x in bytes, y in rows):
//   CR, LF     x = margin; y += line height (DS:5B3C: 12, or 10 on the title pages and help pages)
//   ~XXXYYY    x = margin = XXX / 2 (signed division), y = YYY; digits are byte - '0', unchecked
//   `          toggles red (0x4444) and back to the ink (DS:9215)
//   |NN        colour = n*0x11 in both bytes, n = 10*d1 + d2, 8-bit: byte = (n << 4) | n
//   TAB NN     x += 10*d1 + d2
//   other      if x < 0x9A (signed): glyph (c & 0x7F) in the current colour, x += its advance;
//              else skipped without advancing (the rest of the line is clipped)

#include <cstdint>
#include <string_view>

namespace tmport::dos {

class BackBuffer;
class DosFont;

struct TextStyle {
    uint16_t ink = 0xFFFF;  // DS:9215 g_textInk (a colour word: four equal nibbles, e.g. 0xCCCC white)
    int lineHeight = 12;    // DS:5B3C g_lineHeight
};

// 43cb:0b63 Text_Draw(x, y, s): returns the number of lines (1 + line breaks).
int textDraw(BackBuffer& buf, const DosFont& font, const TextStyle& style, int x, int y, std::string_view s);
// 43cb:0cfc Text_Width(s): the advances of every byte & 0x7F up to NUL or CR (not LF), control bytes
// included (a back-quote counts as its glyph).
int textWidth(const DosFont& font, std::string_view s);
// 130f:0d00 Hud_DrawNumber(x, y, colour, value): hundreds digit if non-zero, tens if non-zero or
// hundreds drawn, units always; a suppressed digit still advances x by 2 bytes. Digits come from
// repeated signed subtraction (values >= 1000 or < 0 give other glyphs, as in the original).
void drawNumber(BackBuffer& buf, const DosFont& font, int x, int y, uint16_t colour, int value);

}  // namespace tmport::dos
