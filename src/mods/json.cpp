#include "mods/json.h"

#include <cmath>
#include <cstdio>
#include <format>

namespace tmport::mods::json {

Value::Value(double v) : type_(Type::Number), num_(v) {
    if (std::isfinite(v) && std::floor(v) == v && std::fabs(v) < 9.0e15) {
        int_ = static_cast<int64_t>(v);
        isInt_ = true;
    }
}

const Value* Value::find(std::string_view key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& [k, v] : obj_)
        if (k == key) return &v;
    return nullptr;
}

Value& Value::set(std::string key, Value v) {
    for (auto& [k, old] : obj_)
        if (k == key) {
            old = std::move(v);
            return old;
        }
    obj_.emplace_back(std::move(key), std::move(v));
    return obj_.back().second;
}

const char* Value::typeName(Type t) {
    switch (t) {
        case Type::Null: return "null";
        case Type::Bool: return "boolean";
        case Type::Number: return "number";
        case Type::String: return "string";
        case Type::Array: return "array";
        case Type::Object: return "object";
    }
    return "?";
}

bool Value::operator==(const Value& o) const {
    if (type_ != o.type_) return false;
    switch (type_) {
        case Type::Null: return true;
        case Type::Bool: return bool_ == o.bool_;
        case Type::Number: return isInt_ && o.isInt_ ? int_ == o.int_ : num_ == o.num_;
        case Type::String: return str_ == o.str_;
        case Type::Array: return arr_ == o.arr_;
        case Type::Object: return obj_ == o.obj_;
    }
    return false;
}

ParseError::ParseError(const std::string& msg, int line, int column)
    : FormatError(std::format("JSON line {}, column {}: {}", line, column, msg)), line_(line), column_(column) {}

// ------------------------------------------------------------------------------------- parser

namespace {

class Parser {
public:
    explicit Parser(std::string_view t) : t_(t) {
        if (t_.starts_with("\xEF\xBB\xBF")) p_ = 3;
    }

    Value document() {
        skipWs();
        Value v = value(0);
        skipWs();
        if (p_ != t_.size()) fail("unexpected text after the JSON value");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& msg) const {
        int line = 1, col = 1;
        for (size_t i = 0; i < p_ && i < t_.size(); ++i) {
            if (t_[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        throw ParseError(msg, line, col);
    }

    void skipWs() {
        while (p_ < t_.size()) {
            const char c = t_[p_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++p_;
            } else {
                break;
            }
        }
    }

    bool literal(std::string_view w) {
        if (t_.substr(p_, w.size()) != w) return false;
        p_ += w.size();
        return true;
    }

    Value value(int depth) {
        if (depth > 64) fail("nesting too deep");
        if (p_ >= t_.size()) fail("unexpected end of the text");
        const char c = t_[p_];
        if (c == '{') return object(depth);
        if (c == '[') return array(depth);
        if (c == '"') return Value(string());
        if (c == '-' || (c >= '0' && c <= '9')) return number();
        if (literal("true")) return Value(true);
        if (literal("false")) return Value(false);
        if (literal("null")) return Value();
        fail(std::format("unexpected character '{}'", c));
    }

    Value object(int depth) {
        ++p_;  // {
        Value v = Value::object();
        skipWs();
        if (p_ < t_.size() && t_[p_] == '}') {
            ++p_;
            return v;
        }
        for (;;) {
            skipWs();
            if (p_ >= t_.size() || t_[p_] != '"') fail("expected a quoted key");
            std::string key = string();
            skipWs();
            if (p_ >= t_.size() || t_[p_] != ':') fail("expected ':' after the key");
            ++p_;
            skipWs();
            if (v.find(key)) fail(std::format("duplicate key \"{}\"", key));
            v.asObject().emplace_back(std::move(key), value(depth + 1));
            skipWs();
            if (p_ >= t_.size()) fail("unexpected end of the text in an object");
            if (t_[p_] == ',') {
                ++p_;
                continue;
            }
            if (t_[p_] == '}') {
                ++p_;
                return v;
            }
            fail("expected ',' or '}' in an object");
        }
    }

    Value array(int depth) {
        ++p_;  // [
        Value v = Value::array();
        skipWs();
        if (p_ < t_.size() && t_[p_] == ']') {
            ++p_;
            return v;
        }
        for (;;) {
            skipWs();
            v.push(value(depth + 1));
            skipWs();
            if (p_ >= t_.size()) fail("unexpected end of the text in an array");
            if (t_[p_] == ',') {
                ++p_;
                continue;
            }
            if (t_[p_] == ']') {
                ++p_;
                return v;
            }
            fail("expected ',' or ']' in an array");
        }
    }

    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    uint32_t hex4() {
        if (p_ + 4 > t_.size()) fail("truncated \\u escape");
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = t_[p_++];
            v <<= 4;
            if (c >= '0' && c <= '9') {
                v |= static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                v |= static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                v |= static_cast<uint32_t>(c - 'A' + 10);
            } else {
                fail("bad hex digit in a \\u escape");
            }
        }
        return v;
    }

    std::string string() {
        ++p_;  // "
        std::string out;
        for (;;) {
            if (p_ >= t_.size()) fail("unterminated string");
            const char c = t_[p_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in a string (use \\n, \\t, ...)");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p_ >= t_.size()) fail("unterminated escape");
            const char e = t_[p_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00) {
                        if (!literal("\\u")) fail("unpaired surrogate in a \\u escape");
                        const uint32_t lo = hex4();
                        if (lo < 0xDC00 || lo >= 0xE000) fail("bad low surrogate in a \\u escape");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp < 0xE000) {
                        fail("unpaired surrogate in a \\u escape");
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail(std::format("unknown escape \\{}", e));
            }
        }
    }

    Value number() {
        const size_t start = p_;
        if (t_[p_] == '-') ++p_;
        auto digits = [&] {
            const size_t s = p_;
            while (p_ < t_.size() && t_[p_] >= '0' && t_[p_] <= '9') ++p_;
            return p_ - s;
        };
        if (digits() == 0) fail("bad number");
        bool integral = true;
        if (p_ < t_.size() && t_[p_] == '.') {
            ++p_;
            integral = false;
            if (digits() == 0) fail("bad number: digits expected after '.'");
        }
        if (p_ < t_.size() && (t_[p_] == 'e' || t_[p_] == 'E')) {
            ++p_;
            integral = false;
            if (p_ < t_.size() && (t_[p_] == '+' || t_[p_] == '-')) ++p_;
            if (digits() == 0) fail("bad number: digits expected in the exponent");
        }
        const std::string s(t_.substr(start, p_ - start));
        if (integral && s.size() < 18) return Value(static_cast<int64_t>(std::stoll(s)));
        return Value(std::stod(s));
    }

    std::string_view t_;
    size_t p_ = 0;
};

// ------------------------------------------------------------------------------------- writer

void writeString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20)
                    out += std::format("\\u{:04x}", c);
                else
                    out += static_cast<char>(c);
        }
    }
    out += '"';
}

void writeScalar(std::string& out, const Value& v) {
    switch (v.type()) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.asBool() ? "true" : "false"; break;
        case Value::Type::Number:
            if (v.isInteger())
                out += std::to_string(v.asInt());
            else
                out += std::format("{}", v.asDouble());
            break;
        case Value::Type::String: writeString(out, v.asString()); break;
        default: break;
    }
}

bool isScalar(const Value& v) { return !v.isArray() && !v.isObject(); }

void writeValue(std::string& out, const Value& v, const WriteOptions& o, int level) {
    const std::string pad(static_cast<size_t>(o.indent * (level + 1)), ' ');
    const std::string padEnd(static_cast<size_t>(o.indent * level), ' ');
    if (v.isArray()) {
        const auto& a = v.asArray();
        if (a.empty()) {
            out += "[]";
            return;
        }
        bool allScalar = true;
        for (const auto& e : a) allScalar = allScalar && isScalar(e);
        if (o.indent == 0) {
            out += '[';
            for (size_t i = 0; i < a.size(); ++i) {
                if (i) out += ',';
                writeValue(out, a[i], o, level + 1);
            }
            out += ']';
            return;
        }
        if (allScalar) {
            // Pack scalars onto lines.
            std::string line;
            std::vector<std::string> lines;
            for (size_t i = 0; i < a.size(); ++i) {
                std::string item;
                writeScalar(item, a[i]);
                if (i + 1 < a.size()) item += ',';
                if (!line.empty() && static_cast<int>(pad.size() + line.size() + 1 + item.size()) > o.wrapColumn) {
                    lines.push_back(line);
                    line.clear();
                }
                if (!line.empty()) line += ' ';
                line += item;
            }
            if (!line.empty()) lines.push_back(line);
            if (lines.size() == 1 && static_cast<int>(padEnd.size() + lines[0].size() + 2) <= o.wrapColumn) {
                out += '[' + lines[0] + ']';
                return;
            }
            out += "[\n";
            for (const auto& l : lines) out += pad + l + '\n';
            out += padEnd + ']';
            return;
        }
        out += "[\n";
        for (size_t i = 0; i < a.size(); ++i) {
            out += pad;
            writeValue(out, a[i], o, level + 1);
            if (i + 1 < a.size()) out += ',';
            out += '\n';
        }
        out += padEnd + ']';
        return;
    }
    if (v.isObject()) {
        const auto& obj = v.asObject();
        if (obj.empty()) {
            out += "{}";
            return;
        }
        if (o.indent == 0) {
            out += '{';
            for (size_t i = 0; i < obj.size(); ++i) {
                if (i) out += ',';
                writeString(out, obj[i].first);
                out += ':';
                writeValue(out, obj[i].second, o, level + 1);
            }
            out += '}';
            return;
        }
        // Small objects of scalars on one line (e.g. one room record).
        bool allScalar = true;
        for (const auto& [k, e] : obj) allScalar = allScalar && isScalar(e);
        if (allScalar) {
            std::string one = "{";
            for (size_t i = 0; i < obj.size(); ++i) {
                if (i) one += ", ";
                writeString(one, obj[i].first);
                one += ": ";
                writeScalar(one, obj[i].second);
            }
            one += '}';
            if (static_cast<int>(pad.size() + one.size()) <= o.wrapColumn + 60) {
                out += one;
                return;
            }
        }
        out += "{\n";
        for (size_t i = 0; i < obj.size(); ++i) {
            out += pad;
            writeString(out, obj[i].first);
            out += ": ";
            writeValue(out, obj[i].second, o, level + 1);
            if (i + 1 < obj.size()) out += ',';
            out += '\n';
        }
        out += padEnd + '}';
        return;
    }
    writeScalar(out, v);
}

}  // namespace

Value parse(std::string_view text) { return Parser(text).document(); }

std::string write(const Value& v, const WriteOptions& options) {
    std::string out;
    writeValue(out, v, options, 0);
    if (options.indent > 0) out += '\n';
    return out;
}

}  // namespace tmport::mods::json
