#include "text/text_engine.h"

#include <algorithm>

namespace tmport::text {

namespace {

// Byte i of s; 0 past the end (the end of a span stands for the terminating NUL).
inline uint8_t at(Bytes s, size_t i) { return i < s.size() ? s[i] : 0; }

// Consumes bytes up to and including the next FF (10b0:0277 and the other skip loops). The
// original has no bound; a span stops at its end.
inline void skipToFF(Bytes s, size_t& p) {
    while (p < s.size())
        if (s[p++] == 0xFF) break;
}

// subst[n - 1]; an absent table or index gives an empty string (the original would read
// through a NULL or out-of-range far pointer).
inline Bytes substEntry(SubstTable subst, int index) {
    if (index < 0 || static_cast<size_t>(index) >= subst.size()) return {};
    return untilNul(subst[static_cast<size_t>(index)]);
}

// C '/' 2 on 16-bit ints: CWD; SUB AX,DX; SAR AX,1 (truncates toward zero).
inline int half(int v) { return v / 2; }

}  // namespace

Bytes untilNul(Bytes b) {
    const auto it = std::find(b.begin(), b.end(), uint8_t{0});
    return b.first(static_cast<size_t>(it - b.begin()));
}

// TM 10b0:0662 / G&G 2b89:0466 Text_DrawChar
int TextEngine::drawChar(int x, int y, int lineH, uint8_t c, bool underline, bool flush) {
    int w;
    if (dialect_.underscoreBlank && underline && c == '_')
        w = font_.charWidth(c);
    else
        w = canvas_->drawGlyph(x, y, c);
    if (underline) canvas_->underline(x, y + lineH - 1, w);  // TM 1058:090f Gfx_DrawHLine, current colour
    if (flush) canvas_->flushGlyph(x, y, w, lineH);          // TM 1058:05bc
    return w;
}

// TM 10b0:053c / G&G 2b89:03ca Text_MeasureRun
int TextEngine::measureRun(uint8_t stop, Bytes s, size_t pos, SubstTable subst, bool inSubst, Bytes substStr,
                           size_t substPos) const {
    int total = 0;
    bool done = false;
    bool inSub = inSubst;
    bool useGiven = inSubst;  // the first substitution scanned is the one handed in
    int index = 0;
    while (!done) {
        while (!done && !inSub) {
            const uint8_t c = at(s, pos++);
            if (c == stop || c == 0 || c == 0x0A || c == 0x0D) {
                done = true;
            } else if (c == 0xFF) {
                const uint8_t k = at(s, pos++);
                if (k == 1) {
                    inSub = true;
                    index = at(s, pos++) - 1;
                }
                skipToFF(s, pos);  // every FF k skips to the next FF here, whatever k is
            } else {
                total += font_.runWidth(c);
            }
        }
        if (inSub) {
            Bytes cur;
            size_t q = 0;
            bool given = false;
            if (useGiven) {
                cur = substStr;
                q = substPos;
                useGiven = false;
                given = true;
            } else {
                cur = substEntry(subst, index);
            }
            const bool stopAtDelimiter = dialect_.substRunStopsAtDelimiter || given;
            while (!done && inSub) {
                const uint8_t c = at(cur, q++);
                if ((c == stop && stopAtDelimiter) || c == 0x0A || c == 0x0D)
                    done = true;
                else if (c == 0)
                    inSub = false;  // resume in the main string
                else
                    total += font_.runWidth(c);
            }
        }
    }
    return total;
}

// TM 10b0:00b5 / G&G 2b89:007a Text_Render
TextExtent TextEngine::render(int x0, int y0, int wrapW, Bytes s, SubstTable subst, bool flush, bool draw) {
    if (!canvas_) draw = false;
    const int right = x0 + wrapW - 1;
    const int spaceW = font_.charWidth(' ');
    const int lineH = font_.lineHeight();
    int x = x0, y = y0;
    int minX = right, maxX = x0;
    bool underline = false, centre = false;
    std::array<int, 8> tabs{};
    for (size_t k = 0; k < tabs.size(); ++k) tabs[k] = 40 * static_cast<int>(k + 1);
    size_t p = 0;

    // 10b0:0498 (and the space / substitution branches): place one glyph.
    auto put = [&](uint8_t c) {
        if (x < minX) minX = x;
        const int adv = draw ? drawChar(x, y, lineH, c, underline, flush) : font_.charWidth(c);
        if (observer_) observer_(c, x, y, adv);
        x += adv;
        if (maxX < x) maxX = x;
    };
    // Start of a centred line: x0 + (w - width of the rest of the line) / 2.
    auto centreStart = [&] {
        const int cx = x0 + half(wrapW - measureRun(0x0A, s, p, subst));
        if (dialect_.centreSetsExtent && maxX < cx) maxX = cx;  // G&G 2b89:029f
        return cx;
    };

    for (;;) {
        const uint8_t c = at(s, p++);
        if (c == 0) break;
        if (c == 0x09 && dialect_.tabStops) {  // 10b0:0393: tab stops do not touch minX/maxX
            for (int t : tabs) {
                if (t + x0 > x) {
                    x = t + x0;
                    break;
                }
            }
            continue;
        }
        if (c == 0xFE && dialect_.marginReturn) {  // 10b0:0412
            x = x0;
            continue;
        }
        switch (c) {
            case 0x0A:
            case 0x0D:  // 10b0:03cf, 2b89:028f
                x = centre ? centreStart() : x0;
                y += lineH;
                break;
            case 0x20: {  // 10b0:0418, 2b89:02d1: wrap test; the space is not drawn when wrapping
                const int need = measureRun(' ', s, p, subst) + spaceW;
                if (x + need > right) {
                    while (at(s, p) == 0x20 || at(s, p) == 0x09) ++p;
                    x = x0;  // wrapped lines are never re-centred
                    y += lineH;
                } else {
                    put(0x20);
                }
                break;
            }
            case 0xFF: {  // 10b0:014e: sub-codes 1..6 through the table at 10b0:0518 (G&G CS:03ae)
                const uint8_t k = at(s, p++);
                switch (k) {
                    case 1: {  // 10b0:016d substitution
                        const Bytes sub = substEntry(subst, at(s, p) - 1);
                        ++p;  // p now rests on the closing FF: the measure below starts there (quirk)
                        size_t q = 0;
                        for (;;) {
                            const uint8_t ch = at(sub, q++);
                            if (ch == 0) break;
                            if (ch == 0x20) {
                                const int need = measureRun(' ', s, p, subst, true, sub, q) + spaceW;
                                if (x + need > right) {
                                    while (at(sub, q) == 0x20 || at(sub, q) == 0x09) ++q;
                                    x = x0;
                                    y += lineH;
                                    continue;
                                }
                            }
                            put(ch);  // no control codes inside a substitution
                        }
                        skipToFF(s, p);
                        break;
                    }
                    case 2:  // 10b0:028d underline toggle
                        underline = !underline;
                        skipToFF(s, p);
                        break;
                    case 3:  // 10b0:02ae Pal_SetForeColor(n - 1), also when only measuring
                        if (canvas_) canvas_->setColour(at(s, p) - 1);
                        ++p;
                        skipToFF(s, p);
                        break;
                    case 4: {  // 10b0:02d7 tab stops: a u16 at every byte position (quirk)
                        if (!dialect_.tabStops) break;  // G&G: FF 04 only (what follows is text)
                        size_t n = 0;
                        while (at(s, p) != 0xFF && n < tabs.size()) {
                            tabs[n++] = static_cast<int16_t>(at(s, p) | (at(s, p + 1) << 8));
                            ++p;
                        }
                        skipToFF(s, p);
                        break;
                    }
                    case 5:  // 10b0:0326 centring toggle
                        skipToFF(s, p);
                        centre = !centre;
                        if (centre) x = centreStart();
                        break;
                    case 6:  // 10b0:037d comment (e.g. puzzle level tags, G&G hint notes)
                        skipToFF(s, p);
                        break;
                    default:  // k = 0 or k > 6: only k is consumed
                        break;
                }
                break;
            }
            default: put(c); break;
        }
        // 10b0:04d1 Timer_Wait(6): slot 6 is never armed, so this wait is omitted.
    }
    return {dialect_.extentFromX0 ? maxX - x0 : maxX - minX, y - y0 + lineH};
}

}  // namespace tmport::text
