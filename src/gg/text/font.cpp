#include "gg/text/font.h"

#include <algorithm>
#include <format>

#include "gg/data/archive.h"
#include "util/error.h"

namespace tmport::gg {

// 2b16:0000 Font_Load
GgFont GgFont::parse(std::span<const uint8_t> p) {
    static constexpr uint8_t kMagic[4] = {'f', 'n', 't', 0x1A};
    if (p.size() < 12 || !std::equal(std::begin(kMagic), std::end(kMagic), p.begin()))
        throw FormatError("not a fnt font");
    auto u16 = [&](size_t o) { return static_cast<uint16_t>(p[o] | (p[o + 1] << 8)); };
    GgFont f;
    f.bytes_.assign(p.begin(), p.end());
    f.flags_ = u16(4);
    f.records_ = u16(6);
    f.height_ = u16(8);
    f.spacing_ = u16(10);
    const auto n = static_cast<size_t>(f.records_);
    f.width_.assign(n, 0);
    f.yOffset_.assign(n, 0);
    f.glyphs_.assign(n, {0, 0});
    size_t pos = 12;
    for (size_t c = 0; c < n; ++c) {
        if (pos + 4 > p.size()) throw FormatError(std::format("fnt: glyph record {} past the end", c));
        f.yOffset_[c] = static_cast<int16_t>(u16(pos));  // first word: y offset (+0x14 table)
        f.width_[c] = u16(pos + 2);                       // second word: width (+0x10 table)
        pos += 4;
        if (f.width_[c] == 0) continue;
        if (pos + 2 > p.size()) throw FormatError(std::format("fnt: glyph {:02x} size past the end", c));
        const size_t size = u16(pos);
        pos += 2;
        if (pos + size > p.size()) throw FormatError(std::format("fnt: glyph {:02x} data past the end", c));
        f.glyphs_[c] = {pos, size};
        pos += size;
    }
    return f;
}

std::span<const uint8_t> GgFont::glyph(uint8_t c) const {
    if (c >= glyphs_.size() || glyphs_[c].second == 0) return {};
    return std::span<const uint8_t>(bytes_).subspan(glyphs_[c].first, glyphs_[c].second);
}

// 2b16:021c Font_Init
Fonts Fonts::load(const Archive& archive) {
    return Fonts(GgFont::parse(archive.read(kUiFontId)), GgFont::parse(archive.read(kSmallFontId)));
}

Fonts::Fonts(GgFont ui, GgFont small) : fonts_{std::move(ui), std::move(small)} { select(0); }

// 2b16:0256 Font_Select
int Fonts::select(int n) {
    const int old = index_;
    index_ = n & 1;
    glyphColor_ = current().colour();
    return old;
}

// 2b16:0243 Font_SetColor
void Fonts::setColor(int c) {
    current().setColour(c);
    glyphColor_ = c;
}

// 2b16:0368 Font_DrawChar
int Fonts::drawChar(Screen& screen, int x, int y, uint8_t c) const {
    if (c == 0x20) return 3;
    const GgFont& f = current();
    if (f.width(c) == 0) return 0;
    if (f.oneBit())
        screen.drawGlyph(x, y + f.yOffset(c), f.glyph(c), glyphColor());
    else
        screen.lpxDrawKeyed(x, y + f.yOffset(c), f.glyph(c), 0);
    return f.width(c) + f.spacing();
}

// 2b16:029c Font_DrawString
void Fonts::drawString(Screen& screen, int x, int y, std::span<const uint8_t> s) const {
    const GgFont& f = current();
    const int startX = x;
    for (const uint8_t c : s) {
        if (c == 0) break;
        if (c == 0x0A || c == 0x0D) {
            y += f.height() + 2;
            x = startX;
        } else if (c == 0x20) {
            x += 3;
        } else if (c < 0x80 && c < f.records() && !f.glyph(c).empty()) {
            if (f.oneBit())
                screen.drawGlyph(x, y + f.yOffset(c), f.glyph(c), glyphColor());
            else
                screen.lpxDrawKeyed(x, y + f.yOffset(c), f.glyph(c), 0);
            x += f.width(c) + f.spacing();
        }
    }
}

}  // namespace tmport::gg
