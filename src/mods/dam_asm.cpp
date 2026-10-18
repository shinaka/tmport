#include "mods/dam_asm.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <format>
#include <map>
#include <optional>
#include <set>

#include "anim/dam_script.h"
#include "mods/mod_common.h"

namespace tmport::mods {

namespace {

using anim::kOpEnd;
using anim::kOpEndFrame;
using anim::kOpLoop;
using anim::kOpSfx;
using anim::kOpSfxStop;
using anim::kOpSprite;
using anim::kOpUpdateRect;

size_t opSize(uint8_t op) {
    switch (op) {
        case kOpUpdateRect: return 7;
        case kOpLoop: return 4;
        case kOpSfxStop: return 2;
        case kOpSfx: return 3;
        case kOpSprite: return 7;
        default: return 1;
    }
}

int16_t s16(const uint8_t* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); }

// ------------------------------------------------------------------------------------ assembler

struct Statement {
    int line = 0;
    std::string label;                 // label defined on this line
    std::string op;                    // mnemonic (lower case), empty for label-only lines
    std::vector<std::string> args;
    size_t offset = 0;                 // assigned in pass 1
    size_t size = 0;
};

std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}

std::optional<long> parseNumber(std::string_view s) {
    bool neg = false;
    if (!s.empty() && (s[0] == '-' || s[0] == '+')) {
        neg = s[0] == '-';
        s.remove_prefix(1);
    }
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s.remove_prefix(2);
    }
    if (s.empty()) return std::nullopt;
    long v = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v, base);
    if (ec != std::errc() || p != s.data() + s.size()) return std::nullopt;
    return neg ? -v : v;
}

bool validLabel(std::string_view s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')) return false;
    return true;
}

class Assembler {
public:
    explicit Assembler(std::string_view text) : text_(text) {}

    DamAssembly run() {
        parseLines();
        layout();
        emit();
        check();
        return std::move(out_);
    }

private:
    [[noreturn]] static void fail(int line, const std::string& msg) { throw ModSyntaxError(msg, line); }

    void parseLines() {
        std::string_view t = text_;
        if (t.starts_with("\xEF\xBB\xBF")) t.remove_prefix(3);
        int line = 0;
        size_t p = 0;
        while (p <= t.size()) {
            const size_t e = std::min(t.find('\n', p), t.size());
            std::string_view l = t.substr(p, e - p);
            p = e + 1;
            ++line;
            if (const size_t c = l.find(';'); c != std::string::npos) l = l.substr(0, c);
            l = trimView(l);
            if (l.empty()) {
                if (e == t.size()) break;
                continue;
            }
            Statement s;
            s.line = line;
            // Label?
            const size_t colon = l.find(':');
            if (colon != std::string::npos) {
                const std::string_view name = trimView(l.substr(0, colon));
                if (!validLabel(name)) fail(line, std::format("bad label name '{}'", name));
                s.label = std::string(name);
                l = trimView(l.substr(colon + 1));
            }
            // Words.
            std::vector<std::string> w;
            size_t i = 0;
            while (i < l.size()) {
                while (i < l.size() && (l[i] == ' ' || l[i] == '\t' || l[i] == ',')) ++i;
                const size_t st = i;
                while (i < l.size() && l[i] != ' ' && l[i] != '\t' && l[i] != ',') ++i;
                if (i > st) w.emplace_back(l.substr(st, i - st));
            }
            if (!w.empty()) {
                s.op = lower(w[0]);
                s.args.assign(w.begin() + 1, w.end());
            }
            stmts_.push_back(std::move(s));
            if (e == t.size()) break;
        }
    }

    void argc(const Statement& s, size_t lo, size_t hi) {
        if (s.args.size() < lo || s.args.size() > hi) {
            if (lo == hi) fail(s.line, std::format("'{}' takes {} operand(s), {} given", s.op, lo, s.args.size()));
            fail(s.line, std::format("'{}' takes {} to {} operands, {} given", s.op, lo, hi, s.args.size()));
        }
    }

    void layout() {
        size_t off = 0;
        bool haveTag = false;
        for (Statement& s : stmts_) {
            if (!s.label.empty()) {
                if (labels_.contains(s.label)) fail(s.line, std::format("label '{}' defined twice", s.label));
                labels_[s.label] = off;
            }
            if (s.op.empty()) continue;
            if (s.op == "tag") {
                if (haveTag || off != 0) fail(s.line, "'tag' must be the first statement, once");
                haveTag = true;
                s.size = 4;
            } else {
                if (!haveTag) fail(s.line, "the script must start with 'tag DAM'");
                if (s.op == "sprite") {
                    argc(s, 3, 4);
                    s.size = 7;
                } else if (s.op == "tick" || s.op == "end") {
                    argc(s, 0, 0);
                    s.size = 1;
                } else if (s.op == "loop") {
                    argc(s, 2, 3);
                    s.size = 4;
                } else if (s.op == "sound") {
                    argc(s, 1, 2);
                    s.size = 3;
                } else if (s.op == "stopsound") {
                    argc(s, 0, 1);
                    s.size = 2;
                } else if (s.op == "update") {
                    argc(s, 4, 4);
                    s.size = 7;
                } else if (s.op == "byte") {
                    if (s.args.empty()) fail(s.line, "'byte' needs at least one value");
                    s.size = s.args.size();
                } else if (s.op == "size") {
                    argc(s, 1, 1);
                    s.size = 0;
                } else {
                    fail(s.line, std::format("unknown statement '{}'", s.op));
                }
            }
            s.offset = off;
            off += s.size;
        }
        if (!haveTag) fail(1, "the script must start with 'tag DAM'");
    }

    long num(const Statement& s, const std::string& a, long lo, long hi, std::string_view what) {
        const auto v = parseNumber(a);
        if (!v) fail(s.line, std::format("{}: '{}' is not a number", what, a));
        if (*v < lo || *v > hi) fail(s.line, std::format("{}: {} is outside {}..{}", what, *v, lo, hi));
        return *v;
    }

    void put8(long v) { out_.bytes.push_back(static_cast<uint8_t>(v & 0xFF)); }
    void put16(long v) {
        out_.bytes.push_back(static_cast<uint8_t>(v & 0xFF));
        out_.bytes.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }

    void emit() {
        for (const Statement& s : stmts_) {
            if (s.op.empty()) continue;
            if (s.op == "tag") {
                if (s.args.size() == 1 && lower(s.args[0]) == "dam") {
                    out_.bytes.insert(out_.bytes.end(), {'D', 'A', 'M', 0x1A});
                } else if (s.args.size() == 1 && lower(s.args[0]) == "dop") {
                    out_.bytes.insert(out_.bytes.end(), {'D', 'O', 'P', 0x1A});
                } else if (s.args.size() == 4) {
                    for (const auto& a : s.args) put8(num(s, a, 0, 255, "tag byte"));
                } else {
                    fail(s.line, "'tag' takes DAM, DOP or four byte values");
                }
                const bool dam = std::memcmp(out_.bytes.data(), "DAM\x1A", 4) == 0;
                const bool dop = std::memcmp(out_.bytes.data(), "DOP", 3) == 0;
                if (!dam && !dop) out_.warnings.push_back(std::format("line {}: the tag is neither DAM nor DOP", s.line));
            } else if (s.op == "sprite") {
                out_.bytes.push_back(kOpSprite);
                put16(num(s, s.args[0], -32768, 65535, "sprite dx"));
                put16(num(s, s.args[1], -32768, 65535, "sprite dy"));
                const long frame = num(s, s.args[2], 0, 255, "sprite frame");
                put8(frame);
                put8(s.args.size() > 3 ? num(s, s.args[3], 0, 255, "sprite mode") : 0);
                out_.maxFrame = std::max(out_.maxFrame, static_cast<int>(frame));
            } else if (s.op == "tick") {
                out_.bytes.push_back(kOpEndFrame);
            } else if (s.op == "end") {
                out_.bytes.push_back(kOpEnd);
                ++out_.endCount;
            } else if (s.op == "loop") {
                out_.bytes.push_back(kOpLoop);
                long target = 0;
                if (auto it = labels_.find(s.args[0]); it != labels_.end())
                    target = static_cast<long>(it->second);
                else if (auto v = parseNumber(s.args[0]))
                    target = *v;
                else
                    fail(s.line, std::format("loop: unknown label '{}'", s.args[0]));
                if (target < 0 || target > 65535) fail(s.line, std::format("loop target {} is outside 0..65535", target));
                put16(target);
                long count = 0;
                if (lower(s.args[1]) == "forever")
                    count = 0;
                else
                    count = num(s, s.args[1], 0, 127, "loop count");
                const long counter = s.args.size() > 2 ? num(s, s.args[2], 0, 1, "loop counter") : 0;
                put8((counter << 7) | count);
                loops_.push_back({s.line, s.offset, static_cast<size_t>(target), count == 0});
            } else if (s.op == "sound") {
                out_.bytes.push_back(kOpSfx);
                const long n = num(s, s.args[0], 0, 255, "sound number");
                put8(s.args.size() > 1 ? num(s, s.args[1], 0, 255, "sound pad") : 0);
                put8(n);
                out_.sounds.push_back(static_cast<int>(n));
            } else if (s.op == "stopsound") {
                out_.bytes.push_back(kOpSfxStop);
                put8(!s.args.empty() ? num(s, s.args[0], 0, 255, "stopsound pad") : 0);
            } else if (s.op == "update") {
                out_.bytes.push_back(kOpUpdateRect);
                put16(num(s, s.args[0], -32768, 65535, "update dx"));
                put8(num(s, s.args[1], -128, 255, "update dy"));
                put16(num(s, s.args[2], -32768, 65535, "update w"));
                put8(num(s, s.args[3], -128, 255, "update h"));
            } else if (s.op == "byte") {
                for (const auto& a : s.args) put8(num(s, a, -128, 255, "byte"));
            } else if (s.op == "size") {
                sizeLine_ = s.line;
                sizeValue_ = static_cast<size_t>(num(s, s.args[0], 1, 1 << 20, "size"));
            }
        }
        if (sizeLine_) {
            if (sizeValue_ < out_.bytes.size())
                fail(sizeLine_, std::format("size {} is smaller than the script ({} bytes)", sizeValue_, out_.bytes.size()));
            out_.paddedSize = sizeValue_;
        } else {
            out_.paddedSize = roundUpNe(out_.bytes.size());
        }
    }

    void check() {
        // Instruction starts and ticks (from the statements, so data bytes are not mistaken).
        std::set<size_t> starts;
        std::vector<size_t> ticks;
        for (const Statement& s : stmts_) {
            if (s.op.empty() || s.op == "size" || s.op == "tag") continue;
            if (s.op == "byte") {
                for (size_t k = 0; k < s.size; ++k) starts.insert(s.offset + k);
            } else {
                starts.insert(s.offset);
            }
            if (s.op == "tick") ticks.push_back(s.offset);
        }
        bool forever = false;
        for (const Loop& l : loops_) {
            if (l.target < 4 || l.target >= out_.bytes.size())
                fail(l.line, std::format("loop target {} is outside the script (4..{})", l.target, out_.bytes.size() - 1));
            if (!starts.contains(l.target))
                out_.warnings.push_back(std::format("line {}: loop target {} is not the start of a statement", l.line, l.target));
            if (l.target <= l.offset) {
                const bool hasTick = std::any_of(ticks.begin(), ticks.end(), [&](size_t t) { return t >= l.target && t < l.offset; });
                if (!hasTick) fail(l.line, "the loop body has no 'tick': the animation engine would hang");
            }
            forever = forever || l.forever;
        }
        if (out_.endCount == 0 && !forever) fail(1, "the script has no 'end' and no endless loop");
    }

    struct Loop {
        int line;
        size_t offset;
        size_t target;
        bool forever;
    };

    std::string_view text_;
    std::vector<Statement> stmts_;
    std::map<std::string, size_t> labels_;
    std::vector<Loop> loops_;
    int sizeLine_ = 0;
    size_t sizeValue_ = 0;
    DamAssembly out_;
};

}  // namespace

DamAssembly assembleDam(std::string_view text) { return Assembler(text).run(); }

std::vector<uint8_t> damBodyFromText(std::string_view text) {
    DamAssembly a = assembleDam(text);
    a.bytes.resize(a.paddedSize, 0);
    return std::move(a.bytes);
}

bool isDamResource(std::span<const uint8_t> r) {
    return r.size() >= 4 && (std::memcmp(r.data(), "DAM\x1A", 4) == 0 || std::memcmp(r.data(), "DOP", 3) == 0);
}

// --------------------------------------------------------------------------------- disassembler

std::string disassembleDam(std::span<const uint8_t> r, int id) {
    std::string out = std::format(
        "; DAM animation script {} (OTHR {}), exported by tmport --export-mod-kit.\n"
        "; Statements: tag, sprite dx dy frame [mode], tick, end, loop target count|forever [counter],\n"
        ";             sound n [pad], stopsound [pad], update dx dy w h, byte b.., size n (docs/MODDING.md)\n",
        id, id);
    if (r.size() < 4) {
        out += "; (too short for a tag)\n";
        for (uint8_t b : r) out += std::format("    byte 0x{:02X}\n", b);
        return out;
    }
    const size_t sig = std::max<size_t>(4, significantLength(r));

    // Linear decode: instruction list (offset, size) up to sig; instructions may use padding bytes
    // inside the resource but never run past its end.
    struct Ins {
        size_t off, size;
        bool raw;  // emit as byte
    };
    std::vector<Ins> ins;
    size_t p = 4, firstEnd = std::string::npos;
    while (p < sig) {
        const uint8_t op = r[p];
        size_t n = op >= kOpUpdateRect ? opSize(op) : 1;
        bool raw = op < kOpUpdateRect;
        if (p + n > r.size()) {
            n = 1;
            raw = true;
        }
        ins.push_back({p, n, raw});
        if (op == kOpEnd && firstEnd == std::string::npos) firstEnd = p;
        p += n;
    }
    // Loop targets -> labels.
    std::set<size_t> starts;
    for (const Ins& i : ins) starts.insert(i.off);
    std::map<size_t, std::string> labels;
    for (const Ins& i : ins)
        if (!i.raw && r[i.off] == kOpLoop) {
            const size_t t = static_cast<uint16_t>(s16(&r[i.off + 1]));
            if (starts.contains(t) && !labels.contains(t)) labels[t] = t == 4 ? "start" : std::format("at_{:04x}", t);
        }

    if (std::memcmp(r.data(), "DAM\x1A", 4) == 0)
        out += "tag DAM\n";
    else if (std::memcmp(r.data(), "DOP\x1A", 4) == 0)
        out += "tag DOP\n";
    else
        out += std::format("tag 0x{:02X} 0x{:02X} 0x{:02X} 0x{:02X}\n", r[0], r[1], r[2], r[3]);

    size_t end = 4;
    for (const Ins& i : ins) {
        if (firstEnd != std::string::npos && i.off == firstEnd + 1)
            out += "; --- after END: never executed (left-over bytes, kept for an exact round trip) ---\n";
        if (auto it = labels.find(i.off); it != labels.end()) out += it->second + ":\n";
        const uint8_t* b = &r[i.off];
        std::string s;
        if (i.raw) {
            s = std::format("byte 0x{:02X}", b[0]);
        } else {
            switch (b[0]) {
                case kOpSprite:
                    s = std::format("sprite {} {} {}", s16(b + 1), s16(b + 3), b[5]);
                    if (b[6]) s += std::format(" {}", b[6]);
                    break;
                case kOpEndFrame: s = "tick"; break;
                case kOpEnd: s = "end"; break;
                case kOpLoop: {
                    const size_t t = static_cast<uint16_t>(s16(b + 1));
                    const int count = b[3] & 0x7F, counter = b[3] >> 7;
                    auto it = labels.find(t);
                    s = std::format("loop {} {}", it != labels.end() ? it->second : std::format("0x{:04x}", t),
                                    count == 0 ? std::string("forever") : std::to_string(count));
                    if (counter) s += " 1";
                    break;
                }
                case kOpSfx:
                    s = std::format("sound {}", b[2]);
                    if (b[1]) s += std::format(" {}", b[1]);
                    break;
                case kOpSfxStop:
                    s = "stopsound";
                    if (b[1]) s += std::format(" {}", b[1]);
                    break;
                case kOpUpdateRect: s = std::format("update {} {} {} {}", s16(b + 1), b[3], s16(b + 4), b[6]); break;
                default: s = std::format("byte 0x{:02X}", b[0]); break;
            }
        }
        out += std::format("    {:<32}; {:04x}\n", s, i.off);
        end = i.off + i.size;
    }
    if (r.size() != roundUpNe(end)) out += std::format("size {}\n", r.size());
    return out;
}

}  // namespace tmport::mods
