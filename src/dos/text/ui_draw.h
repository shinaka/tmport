#pragma once
// The drawing primitives of ui.c (segment 43cb) of the 1990 DOS edition: lines, frames, the colour
// exchange used by highlights, message-box frames and geometry, Yes/No buttons and the menu-bar
// titles (re/dos/docs/code_ui.md §2.5, §4, §5.1). They take explicit parameters; the dialog state
// machine, input zones and box table logic are the UI module's (wave 3). x in bytes, y in rows,
// rectangles inclusive; fill colours are bytes (two pixels, e.g. 0xFF black, 0xCC white).

#include <cstdint>
#include <optional>
#include <string_view>

namespace tmport::dos {

class BackBuffer;
class DosExe;
class DosFont;
struct TextStyle;

// 43cb:0ab9 Gfx_HLine(x, y, w, c) = fill (x, y, x+w-1, y).
void hline(BackBuffer& buf, int x, int y, int w, uint8_t fill);
// 43cb:0ada Gfx_VLine(x, y, h, c) = fill (x, y, x, y+h-1).
void vline(BackBuffer& buf, int x, int y, int h, uint8_t fill);
// 43cb:0b18 Gfx_FrameRect(x1, y1, x2, y2, c): 1-byte (2-pixel) sides and 2-row top and bottom.
void frameRect(BackBuffer& buf, int x1, int y1, int x2, int y2, uint8_t fill);
// Exchanges two colours in a rectangle with three colour replaces through colour 3, as
// SignIn_HighlightRow 47c7:06d4 (a <-> b) and Menu_HighlightItem 43cb:04ff do: a -> 3, b -> a,
// 3 -> b. Pixels already of colour 3 end up as b.
void exchangeColors(BackBuffer& buf, int x1, int y1, int x2, int y2, uint16_t a, uint16_t b);

// The fixed strings the box code draws (far pointers and strings in DGROUP).
struct BoxTexts {
    std::string_view escFooter;    // far pointer DS:5256: the Esc footer (with the Esc key glyph)
    std::string_view enterLeft;    // far pointer DS:525A: the left Enter line of type 3
    std::string_view enterFooter;  // far pointer DS:525E: the Enter footer of type 4
    std::string_view no;           // DS:5D00
    std::string_view yes;          // DS:5D03
    std::string_view type5Left;    // DS:5D07 (glyph string of the unused type 5)
    std::string_view type5Right;   // DS:5D09
    static BoxTexts fromExe(const DosExe& exe);
};

// The geometry MsgBox_Draw 43cb:0592 computes for a box of `type` (0 drop-down, 1 Yes/No, 2 help,
// 3 OK with an Enter line, 4 disk, 6 name entry, 7 plain); x1/y1 < 0 = centred.
struct BoxGeometry {
    int type = 0;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    int width = 0, height = 0;  // DS:931E: x2 - x1; y2 - y1
    int textY = 0;              // DS:A1FF: y1 + 9 (+ 0x0E with a title)
    int yesX = 0, noX = 0, buttonY = 0;  // type 1: button 1 (DS:5D03) at x2 - 0x3A, button 0 (DS:5D00) at x2 - 0x1C, y2 - 0x23
};
BoxGeometry msgBoxGeometry(const DosFont& font, int type, int x1, int y1, std::optional<std::string_view> title,
                           std::string_view text);

// 43cb:0848 Box_DrawFrame(type, x1, y1, x2, y2, title): white box (from y1 - 2), black frame, light
// grey shadow, the title bar and the footer of the type, text in style.ink.
void drawBoxFrame(BackBuffer& buf, const DosFont& font, const TextStyle& style, const BoxTexts& texts, int type, int x1,
                  int y1, int x2, int y2, std::optional<std::string_view> title);

// 43cb:0d50 Button_Draw: a rounded button with `label` at (x, y); selected = black face and white
// text (ink 0xCCCC), else white face and text in style.ink.
void drawButton(BackBuffer& buf, const DosFont& font, const TextStyle& style, std::string_view label, int x, int y, bool selected);

// The drawing part of 43cb:0592 MsgBox_Draw(n) for record n of the box table DS:59CC (23 records
// {u8 type; u8; i16 x1; i16 y1; u8 default button; u8; far title; far text}): frame, buttons (type
// 1, `selectedButton` or the record's default), text. Ink 0xFFFF. Input zones and the slow-CPU
// background save belong to the UI module. Returns the geometry; `lines` gets Text_Draw's count.
BoxGeometry drawMsgBox(BackBuffer& buf, const DosFont& font, const DosExe& exe, const BoxTexts& texts, int n,
                       std::optional<int> selectedButton = std::nullopt, int* lines = nullptr);

// 43cb:0f1d MenuBar_DrawTitle(i, on) for menu i = 0..2 (x ranges DS:5BC7/DS:5BCD, glyphs DS:5BD3 and
// DS:5BDF). Sets style.ink as the original sets DS:9215 (0xFFFF off, 0xCCCC on).
void drawMenuTitle(BackBuffer& buf, const DosFont& font, TextStyle& style, const DosExe& exe, int i, bool on);
// 43cb:04ff Menu_HighlightItem: exchanges white and black over item `item` of the open drop-down
// `menu` (box record x1, and the geometry DrawMsgBox returned).
void highlightMenuItem(BackBuffer& buf, const DosExe& exe, int menu, int item, const BoxGeometry& box);

}  // namespace tmport::dos
