#include "dos/text/text_draw.h"

#include "dos/gfx/back_buffer.h"
#include "dos/text/dos_font.h"

namespace tmport::dos {

namespace {

// Byte i of s as the original reads it: a signed char (CBW), 0 past the end.
int sbyte(std::string_view s, size_t i) { return i < s.size() ? static_cast<int8_t>(s[i]) : 0; }
int digit(std::string_view s, size_t i) { return sbyte(s, i) - '0'; }

}  // namespace

int textDraw(BackBuffer& buf, const DosFont& font, const TextStyle& style, int x, int y, std::string_view s) {
    int margin = x, lines = 1;
    uint16_t colour = style.ink;
    bool highlight = false;
    for (size_t i = 0; i < s.size() && s[i] != '\0'; ++i) {
        const auto c = static_cast<uint8_t>(s[i]);
        if (c == '\r' || c == '\n') {
            x = margin;
            y += style.lineHeight;
            ++lines;
        } else if (c == '~') {  // 43cb:0bae
            x = margin = (digit(s, i + 1) * 100 + digit(s, i + 2) * 10 + digit(s, i + 3)) / 2;
            y = digit(s, i + 4) * 100 + digit(s, i + 5) * 10 + digit(s, i + 6);
            i += 6;
        } else if (c == '`') {  // 43cb:0c38
            highlight = !highlight;
            colour = highlight ? 0x4444 : style.ink;
        } else if (c == '|') {  // 43cb:0c52
            const auto n = static_cast<uint8_t>(digit(s, i + 1) * 10 + digit(s, i + 2));
            const auto b = static_cast<uint8_t>((n << 4) | n);
            colour = static_cast<uint16_t>(b | (b << 8));
            i += 2;
        } else if (c == '\t') {  // 43cb:0c96
            x += digit(s, i + 1) * 10 + digit(s, i + 2);
            i += 2;
        } else if (x < 0x9A) {  // 43cb:0cbf
            x += font.drawChar(buf, colour, x, y, c & 0x7F);
        }
    }
    return lines;
}

int textWidth(const DosFont& font, std::string_view s) {
    int w = 0;
    for (char ch : s) {
        if (ch == '\0' || ch == '\r') break;
        w += font.advance(static_cast<uint8_t>(ch) & 0x7F);
    }
    return w;
}

void drawNumber(BackBuffer& buf, const DosFont& font, int x, int y, uint16_t colour, int value) {
    auto v = static_cast<int16_t>(value);
    int hundreds = 0, tens = 0;
    while (static_cast<int16_t>(v - 100) >= 0) {
        v = static_cast<int16_t>(v - 100);
        ++hundreds;
    }
    while (static_cast<int16_t>(v - 10) >= 0) {
        v = static_cast<int16_t>(v - 10);
        ++tens;
    }
    if (hundreds != 0) x += font.drawChar(buf, colour, x, y, '0' + hundreds);
    else x += 2;
    if (tens != 0 || hundreds != 0) x += font.drawChar(buf, colour, x, y, '0' + tens);
    else x += 2;
    font.drawChar(buf, colour, x, y, '0' + v);
}

}  // namespace tmport::dos
