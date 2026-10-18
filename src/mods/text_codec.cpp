#include "mods/text_codec.h"

#include <array>
#include <charconv>
#include <format>

#include "mods/mod_common.h"

namespace tmport::mods {

namespace {

// FFNT key-cap glyphs (fmt_bndl_ffnt.md §3.2). Two-glyph icons have ".1"/".2"-style halves and
// a pair name that the exporter uses when both halves follow each other.
constexpr std::array<KeyCapName, 27> kKeyCaps = {{
    {0x06, "r-key"},          {0x17, "s-key"},          {0x80, "left"},         {0x81, "right"},
    {0x82, "up"},             {0x83, "down"},           {0x84, "spacebar-1"},   {0x85, "spacebar-2"},
    {0x86, "esc"},            {0x87, "return-1"},       {0x88, "return-2"},     {0x89, "backspace-1"},
    {0x8A, "backspace-2"},    {0x8B, "enter"},          {0x8C, "ctrl"},         {0x8D, "tab-key"},
    {0x8E, "spacebar-alt-1"}, {0x8F, "spacebar-alt-2"}, {0x90, "tab-key-alt"},  {0x91, "enter-alt"},
    {0x92, "esc-alt"},        {0x93, "delete"},         {0x94, "return-alt-1"}, {0x95, "return-alt-2"},
    {0x99, "cmd"},            {0xB6, "cmd-alt"},        {0xB7, "r-key-alt"},
}};

struct PairName {
    uint8_t first, second;
    const char* name;
};
constexpr std::array<PairName, 5> kPairs = {{
    {0x84, 0x85, "spacebar"},
    {0x87, 0x88, "return"},
    {0x89, 0x8A, "backspace"},
    {0x8E, 0x8F, "spacebar-alt"},
    {0x94, 0x95, "return-alt"},
}};

const char* keyCapName(uint8_t b) {
    for (const auto& k : kKeyCaps)
        if (k.byte == b) return k.name;
    return nullptr;
}

// Mac Roman 0x96..0xFD (the FFNT glyphs follow Mac Roman there, fmt_bndl_ffnt.md §3.2). 0x99,
// 0xB6 and 0xB7 hold key icons instead and are left out (0).
constexpr uint16_t kMacRoman96[0xFE - 0x96] = {
    0x00F1, 0x00F3, 0x00F2, 0x0000, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,                  // 96..9F
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4,  // A0..AB
    0x00A8, 0x2260, 0x00C6, 0x00D8,                                                                  // AC..AF
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x0000, 0x0000, 0x220F, 0x03C0, 0x222B, 0x00AA,  // B0..BB
    0x00BA, 0x03A9, 0x00E6, 0x00F8,                                                                  // BC..BF
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0,  // C0..CB
    0x00C3, 0x00D5, 0x0152, 0x0153,                                                                  // CC..CF
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC,  // D0..DB
    0x2039, 0x203A, 0xFB01, 0xFB02,                                                                  // DC..DF
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE,  // E0..EB
    0x00CF, 0x00CC, 0x00D3, 0x00D4,                                                                  // EC..EF
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA,  // F0..FB
    0x00B8, 0x02DD,                                                                                  // FC..FD
};

std::optional<uint8_t> macRomanByte(uint32_t cp) {
    if (cp == 0) return std::nullopt;
    for (size_t i = 0; i < std::size(kMacRoman96); ++i)
        if (kMacRoman96[i] == cp) return static_cast<uint8_t>(0x96 + i);
    if (cp == 0x00A4) return uint8_t{0xDB};  // the pre-1998 Mac Roman currency sign
    return std::nullopt;
}

bool printable(uint8_t c) { return c >= 0x20 && c <= 0x7E; }

// End of the FF code starting at i (index of the closing FF), or npos.
size_t codeEnd(std::span<const uint8_t> b, size_t i) {
    for (size_t j = i + 1; j < b.size(); ++j)
        if (b[j] == 0xFF) return j;
    return std::string::npos;
}

bool plainNote(std::span<const uint8_t> payload) {
    for (uint8_t c : payload)
        if (!printable(c) || c == '{' || c == '}') return false;
    return true;
}

// The escape for one FF code b[i..j] (j = closing FF).
std::string codeEscape(std::span<const uint8_t> b, size_t i, size_t j) {
    if (j == i + 1) return "{ff}";
    const uint8_t op = b[i + 1];
    const auto payload = b.subspan(i + 2, j - i - 2);
    switch (op) {
        case 1:
            if (payload.size() == 1 && payload[0] >= 1 && payload[0] <= 254) return std::format("{{arg {}}}", payload[0]);
            break;
        case 2:
            if (payload.empty()) return "{ul}";
            break;
        case 3:
            if (payload.size() == 1 && payload[0] >= 1 && payload[0] <= 254) return std::format("{{color {}}}", payload[0] - 1);
            break;
        case 4: {
            bool ok = true;
            for (uint8_t c : payload) ok = ok && c >= 1 && c <= 254;
            if (!ok) break;
            std::string s = "{tabs";
            for (uint8_t c : payload) s += std::format(" {}", c);
            return s + "}";
        }
        case 5:
            if (payload.empty()) return "{center}";
            break;
        case 6:
            if (plainNote(payload)) return payload.empty() ? "{note}" : "{note " + std::string(payload.begin(), payload.end()) + "}";
            break;
        default: break;
    }
    std::string s = std::format("{{ff {:02X}", op);
    for (uint8_t c : payload) s += std::format(" {:02X}", c);
    return s + "}";
}

}  // namespace

std::span<const KeyCapName> keyCapNames() { return kKeyCaps; }

std::string escapeGameText(std::span<const uint8_t> b) {
    std::string out;
    out.reserve(b.size() + b.size() / 8);
    for (size_t i = 0; i < b.size(); ++i) {
        const uint8_t c = b[i];
        if (c == '{') {
            out += "{{";
        } else if (printable(c)) {
            out += static_cast<char>(c);
        } else if (c == 0x0D) {
            out += '\n';
        } else if (c == 0x0A) {
            out += "{lf}";
        } else if (c == 0x09) {
            out += "{tab}";
        } else if (c == 0xFE) {
            out += "{margin}";
        } else if (c == 0xFF) {
            const size_t j = codeEnd(b, i);
            if (j == std::string::npos) {
                out += "{xFF}";  // unterminated code: a raw byte
            } else {
                out += codeEscape(b, i, j);
                i = j;
            }
        } else {
            bool pair = false;
            if (i + 1 < b.size())
                for (const auto& p : kPairs)
                    if (c == p.first && b[i + 1] == p.second) {
                        out += std::format("{{{}}}", p.name);
                        ++i;
                        pair = true;
                        break;
                    }
            if (pair) continue;
            if (const char* n = keyCapName(c))
                out += std::format("{{{}}}", n);
            else
                out += std::format("{{x{:02X}}}", c);
        }
    }
    return out;
}

bool isPlainGameText(std::span<const uint8_t> b) {
    for (size_t i = 0; i < b.size(); ++i) {
        const uint8_t c = b[i];
        if (c == 0) return false;
        if (printable(c) || c == 0x0D || c == 0x0A || c == 0x09 || c == 0xFE || keyCapName(c)) continue;
        if (c == 0xFF) {
            const size_t j = codeEnd(b, i);
            if (j == std::string::npos) return false;
            i = j;
            continue;
        }
        return false;
    }
    return true;
}

// ------------------------------------------------------------------------------------- parsing

namespace {

class TextParser {
public:
    TextParser(std::string_view t, int firstLine) : t_(t), line_(firstLine) {
        if (t_.starts_with("\xEF\xBB\xBF")) t_.remove_prefix(3);
    }

    std::vector<uint8_t> run() {
        while (p_ < t_.size()) {
            const unsigned char c = static_cast<unsigned char>(t_[p_]);
            if (c == '\r' || c == '\n') {
                if (c == '\r' && p_ + 1 < t_.size() && t_[p_ + 1] == '\n') ++p_;
                ++p_;
                out_.push_back(0x0D);
                newLine();
                continue;
            }
            if (c == '{') {
                if (p_ + 1 < t_.size() && t_[p_ + 1] == '{') {
                    out_.push_back('{');
                    p_ += 2;
                    continue;
                }
                if (escape()) return std::move(out_);  // {end}
                continue;
            }
            if (c == '\t') {
                out_.push_back(0x09);
                ++p_;
                continue;
            }
            if (c < 0x20 || c == 0x7F) fail(std::format("control character 0x{:02X} (use an escape such as {{x{:02X}}})", c, c));
            if (c < 0x80) {
                out_.push_back(c);
                ++p_;
                continue;
            }
            const uint32_t cp = utf8();
            const auto b = macRomanByte(cp);
            if (!b)
                fail(std::format("character U+{:04X} has no glyph in the game font (write a raw byte as {{xHH}})", cp));
            out_.push_back(*b);
        }
        return std::move(out_);
    }

private:
    [[noreturn]] void fail(const std::string& msg) const { throw ModSyntaxError(msg, line_, static_cast<int>(p_ - lineStart_) + 1); }

    void newLine() {
        ++line_;
        lineStart_ = p_;
    }

    uint32_t utf8() {
        const unsigned char c = static_cast<unsigned char>(t_[p_]);
        int n = 0;
        uint32_t cp = 0;
        if ((c & 0xE0) == 0xC0) {
            n = 1;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            n = 2;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            n = 3;
            cp = c & 0x07;
        } else {
            fail("invalid UTF-8 (save the file as UTF-8)");
        }
        if (p_ + static_cast<size_t>(n) >= t_.size()) fail("truncated UTF-8 sequence");
        for (int k = 1; k <= n; ++k) {
            const unsigned char cc = static_cast<unsigned char>(t_[p_ + static_cast<size_t>(k)]);
            if ((cc & 0xC0) != 0x80) fail("invalid UTF-8 (save the file as UTF-8)");
            cp = (cp << 6) | (cc & 0x3F);
        }
        p_ += static_cast<size_t>(n) + 1;
        return cp;
    }

    static std::optional<int> number(std::string_view s, int base) {
        int v = 0;
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v, base);
        if (ec != std::errc() || ptr != s.data() + s.size()) return std::nullopt;
        return v;
    }

    std::vector<std::string_view> words(std::string_view s) {
        std::vector<std::string_view> w;
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && s[i] == ' ') ++i;
            const size_t st = i;
            while (i < s.size() && s[i] != ' ') ++i;
            if (i > st) w.push_back(s.substr(st, i - st));
        }
        return w;
    }

    uint8_t byteArg(std::string_view a, int base, int lo, int hi, std::string_view what) {
        const auto v = number(a, base);
        if (!v || *v < lo || *v > hi) fail(std::format("{}: '{}' is not a number in {}..{}", what, a, lo, hi));
        return static_cast<uint8_t>(*v);
    }

    void code(uint8_t op, std::span<const uint8_t> payload) {
        out_.push_back(0xFF);
        out_.push_back(op);
        out_.insert(out_.end(), payload.begin(), payload.end());
        out_.push_back(0xFF);
    }

    // Parses one {...} escape at p_. Returns true for {end}.
    bool escape() {
        const size_t close = t_.find('}', p_);
        const size_t nl = t_.find_first_of("\r\n", p_);
        if (close == std::string::npos || (nl != std::string::npos && nl < close))
            fail("'{' without a closing '}' on the same line (write a literal brace as \"{{\")");
        const std::string_view body = t_.substr(p_ + 1, close - p_ - 1);
        const size_t sp = body.find(' ');
        const std::string_view name = body.substr(0, sp);
        const std::string_view rest = sp == std::string::npos ? std::string_view() : body.substr(sp + 1);
        const auto args = words(rest);
        auto noArgs = [&] {
            if (!args.empty()) fail(std::format("{{{}}} takes no arguments", name));
        };
        if (name == "end") {
            noArgs();
            const size_t after = close + 1;
            for (size_t k = after; k < t_.size(); ++k)
                if (t_[k] != ' ' && t_[k] != '\t' && t_[k] != '\r' && t_[k] != '\n') {
                    p_ = k;
                    fail("text after {end}");
                }
            p_ = t_.size();
            return true;
        }
        if (name == "lf") {
            noArgs();
            out_.push_back(0x0A);
        } else if (name == "tab") {
            noArgs();
            out_.push_back(0x09);
        } else if (name == "margin") {
            noArgs();
            out_.push_back(0xFE);
        } else if (name == "ul") {
            noArgs();
            code(2, {});
        } else if (name == "center") {
            noArgs();
            code(5, {});
        } else if (name == "color") {
            if (args.size() != 1) fail("{color N} takes one number (EGA colour 0..15)");
            const uint8_t n = byteArg(args[0], 10, 0, 253, "{color}");
            const uint8_t c = static_cast<uint8_t>(n + 1);
            code(3, std::span<const uint8_t>(&c, 1));
        } else if (name == "arg") {
            if (args.size() != 1) fail("{arg n} takes one number");
            const uint8_t n = byteArg(args[0], 10, 1, 254, "{arg}");
            code(1, std::span<const uint8_t>(&n, 1));
        } else if (name == "tabs") {
            std::vector<uint8_t> b;
            for (auto a : args) b.push_back(byteArg(a, 10, 1, 254, "{tabs}"));
            code(4, b);
        } else if (name == "note") {
            std::vector<uint8_t> b(rest.begin(), rest.end());
            for (uint8_t c : b)
                if (!printable(c)) fail("{note} text must be printable ASCII (use {ff 06 ..} for other bytes)");
            code(6, b);
        } else if (name == "ff") {
            if (args.empty()) {
                out_.push_back(0xFF);
                out_.push_back(0xFF);
            } else {
                std::vector<uint8_t> b;
                for (auto a : args) b.push_back(byteArg(a, 16, 1, 254, "{ff}"));
                code(b[0], std::span<const uint8_t>(b).subspan(1));
            }
        } else if (name.size() == 3 && name[0] == 'x') {
            noArgs();
            const uint8_t v = byteArg(name.substr(1), 16, 0, 255, "{xHH}");
            if (v == 0) fail("{x00}: a NUL byte would end the text");
            out_.push_back(v);
        } else {
            bool found = false;
            for (const auto& pr : kPairs)
                if (name == pr.name) {
                    noArgs();
                    out_.push_back(pr.first);
                    out_.push_back(pr.second);
                    found = true;
                    break;
                }
            if (!found)
                for (const auto& k : kKeyCaps)
                    if (name == k.name) {
                        noArgs();
                        out_.push_back(k.byte);
                        found = true;
                        break;
                    }
            if (!found) fail(std::format("unknown escape {{{}}} (write a literal brace as \"{{{{\")", body));
        }
        p_ = close + 1;
        return false;
    }

    std::string_view t_;
    size_t p_ = 0;
    int line_ = 1;
    size_t lineStart_ = 0;
    std::vector<uint8_t> out_;
};

}  // namespace

std::vector<uint8_t> unescapeGameText(std::string_view text, int firstLine) { return TextParser(text, firstLine).run(); }

std::optional<std::vector<uint8_t>> othrTextPayload(std::span<const uint8_t> resource) {
    size_t nul = 0;
    while (nul < resource.size() && resource[nul] != 0) ++nul;
    if (nul == 0 || nul == resource.size()) return std::nullopt;
    const auto text = resource.subspan(0, nul);
    if (!isPlainGameText(text) || !isNePadded(resource, nul + 1)) return std::nullopt;
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::string othrTextFile(std::span<const uint8_t> payload) { return escapeGameText(payload) + "{end}\n"; }

std::vector<uint8_t> othrBodyFromTextFile(std::string_view file) {
    std::vector<uint8_t> b = unescapeGameText(file);
    if (b.empty()) throw FormatError("the text is empty");
    b.push_back(0);
    padToNe(b);
    return b;
}

}  // namespace tmport::mods
