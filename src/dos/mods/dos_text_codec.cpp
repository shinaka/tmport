#include "dos/mods/dos_text_codec.h"

#include <algorithm>
#include <format>

#include "dos/text/dos_font.h"
#include "mods/mod_common.h"

namespace tmport::dos {

namespace {

constexpr DosGlyphName kGlyphNames[] = {
    {"spacebar", 0x07, 0x06},  // two glyphs: listed first so the exporter prefers it over {x07}{x06}
    {"up", 0x1B, -1},   {"down", 0x1C, -1}, {"right", 0x1D, -1}, {"left", 0x1E, -1},
    {"enter", 0x05, -1}, {"menu", 0x02, -1}, {"crown", 0x1A, -1},
};

constexpr uint8_t kCR = 0x0D, kLF = 0x0A, kTab = 0x09;
constexpr int kClipX = 0x9A;  // Text_Draw skips glyphs starting at x >= 0x9A

bool isDigit(uint8_t c) { return c >= '0' && c <= '9'; }
int digit(uint8_t c) { return static_cast<int>(c) - 0x30; }

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

std::span<const DosGlyphName> dosGlyphNames() { return kGlyphNames; }

std::string escapeDosText(std::span<const uint8_t> bytes, DosTextForm form) {
    std::string out;
    out.reserve(bytes.size() + 16);
    for (size_t i = 0; i < bytes.size(); ++i) {
        const uint8_t c = bytes[i];
        if (c == '{') {
            out += "{{";
            continue;
        }
        if (c >= 0x20 && c < 0x7F) {
            out.push_back(static_cast<char>(c));
            continue;
        }
        if (c == kCR) {
            out.push_back(form == DosTextForm::Lines ? '\n' : '\r');
            continue;
        }
        if (c == kLF) {
            out += form == DosTextForm::Lines ? "{lf}" : "\n";
            continue;
        }
        if (c == kTab) {
            out += form == DosTextForm::Lines ? "{tab}" : "\t";
            continue;
        }
        bool named = false;
        for (const DosGlyphName& g : kGlyphNames) {
            if (g.first != c) continue;
            if (g.second >= 0) {
                if (i + 1 >= bytes.size() || bytes[i + 1] != g.second) continue;
                ++i;
            }
            out += std::format("{{{}}}", g.name);
            named = true;
            break;
        }
        if (!named) out += std::format("{{x{:02X}}}", c);
    }
    return out;
}

std::vector<uint8_t> unescapeDosText(std::string_view text, DosTextForm form, int firstLine) {
    std::vector<uint8_t> out;
    out.reserve(text.size());
    int line = firstLine, column = 1;
    auto fail = [&](const std::string& msg) -> void { throw mods::ModSyntaxError(msg, line, column); };
    for (size_t i = 0; i < text.size(); ++i, ++column) {
        const char ch = text[i];
        const auto u = static_cast<unsigned char>(ch);
        if (ch == '\r' || ch == '\n') {
            if (form == DosTextForm::Lines) {
                if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;  // CR LF = one break
                out.push_back(kCR);
            } else {
                out.push_back(ch == '\r' ? kCR : kLF);
            }
            ++line;
            column = 0;
            continue;
        }
        if (ch == '\t') {
            out.push_back(kTab);
            continue;
        }
        if (u == 0) fail("NUL is not allowed in game text (it ends the string)");
        if (u >= 0x80) fail("the DOS font has no characters outside ASCII (use {xHH} for a raw byte)");
        if (u < 0x20) fail(std::format("control character {:#04x} (write it as {{x{:02X}}})", u, u));
        if (ch != '{') {
            out.push_back(u);
            continue;
        }
        if (i + 1 < text.size() && text[i + 1] == '{') {
            out.push_back('{');
            ++i;
            ++column;
            continue;
        }
        const size_t close = text.find('}', i + 1);
        if (close == std::string_view::npos) fail("'{' without '}' (write a literal brace as {{)");
        const std::string_view name = text.substr(i + 1, close - i - 1);
        if (name.size() == 3 && (name[0] == 'x' || name[0] == 'X') && hexValue(name[1]) >= 0 && hexValue(name[2]) >= 0) {
            const int v = hexValue(name[1]) * 16 + hexValue(name[2]);
            if (v == 0) fail("{x00} is not allowed (NUL ends the string)");
            out.push_back(static_cast<uint8_t>(v));
        } else if (name == "lf") {
            out.push_back(kLF);
        } else if (name == "tab") {
            out.push_back(kTab);
        } else if (name == "cr") {
            out.push_back(kCR);
        } else {
            bool found = false;
            for (const DosGlyphName& g : kGlyphNames)
                if (name == g.name) {
                    out.push_back(g.first);
                    if (g.second >= 0) out.push_back(static_cast<uint8_t>(g.second));
                    found = true;
                    break;
                }
            if (!found) fail(std::format("unknown code {{{}}} (known: {{xHH}}, {{lf}}, {{tab}}, {{up}}, {{down}}, {{left}}, "
                                         "{{right}}, {{enter}}, {{spacebar}}, {{menu}}, {{crown}}; a literal brace is {{{{)",
                                         name));
        }
        column += static_cast<int>(close - i);
        i = close;
    }
    return out;
}

std::string checkDosMarkup(std::span<const uint8_t> text) {
    for (size_t i = 0; i < text.size(); ++i) {
        const uint8_t c = text[i];
        size_t need = 0;
        const char* what = nullptr;
        if (c == '~') {
            need = 6;
            what = "'~' (a move) is not followed by six digits XXXYYY";
        } else if (c == '|') {
            need = 2;
            what = "'|' (a colour) is not followed by two digits";
        } else if (c == kTab) {
            need = 2;
            what = "TAB (an indent) is not followed by two digits";
        } else if (c == 0) {
            return std::format("NUL at byte {}", i);
        }
        if (!what) continue;
        for (size_t k = 1; k <= need; ++k)
            if (i + k >= text.size() || !isDigit(text[i + k])) return std::format("{} at byte {}", what, i);
        i += need;
    }
    return {};
}

DosTextExtent measureDosText(const DosFont& font, std::span<const uint8_t> text, int x, int y, int lineHeight) {
    DosTextExtent e;
    int margin = x;
    auto glyphBox = [&](uint8_t code) {
        const int w = font.width(code & 0x7F);
        if (code == ' ' || w == 0) return;
        const int r = x + (w + 1) / 2, b = y + font.height();
        if (!e.any) {
            e.left = x;
            e.top = y;
            e.right = r;
            e.bottom = b;
            e.any = true;
            return;
        }
        e.left = std::min(e.left, x);
        e.top = std::min(e.top, y);
        e.right = std::max(e.right, r);
        e.bottom = std::max(e.bottom, b);
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const uint8_t c = text[i];
        if (c == 0) break;
        if (c == kCR || c == kLF) {
            x = margin;
            y += lineHeight;
            ++e.lines;
        } else if (c == '~') {
            if (i + 6 >= text.size()) break;  // malformed (checkDosMarkup reports it)
            x =(100 * digit(text[i + 1]) + 10 * digit(text[i + 2]) + digit(text[i + 3])) / 2;
            margin = x;
            y = 100 * digit(text[i + 4]) + 10 * digit(text[i + 5]) + digit(text[i + 6]);
            i += 6;
        } else if (c == '`') {
            // highlight toggle: no movement
        } else if (c == '|') {
            i += 2;
        } else if (c == kTab) {
            if (i + 2 < text.size()) x += 10 * digit(text[i + 1]) + digit(text[i + 2]);
            i += 2;
        } else if (x < kClipX) {
            glyphBox(c);
            x += font.advance(c & 0x7F);
        } else {
            ++e.clipped;
        }
    }
    return e;
}

int dosTextWidth(const DosFont& font, std::span<const uint8_t> text) {
    int w = 0;
    for (const uint8_t c : text) {
        if (c == 0 || c == kCR) break;
        w += font.advance(c & 0x7F);
    }
    return w;
}

std::vector<std::span<const uint8_t>> dosTextLines(std::span<const uint8_t> text) {
    std::vector<std::span<const uint8_t>> lines;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] == kCR || text[i] == kLF) {
            lines.push_back(text.subspan(start, i - start));
            start = i + 1;
        }
    lines.push_back(text.subspan(start));
    return lines;
}

}  // namespace tmport::dos
