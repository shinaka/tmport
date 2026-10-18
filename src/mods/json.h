#pragma once
// Minimal JSON (RFC 8259) value, parser and writer for the mod layer (BNDL member lists, table
// overrides, mod manifests). Objects keep the order of their keys. Numbers are kept as double and,
// when they are integral, also as int64 so that ids and table values round-trip exactly.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/error.h"

namespace tmport::mods::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Value>;
    using Member = std::pair<std::string, Value>;
    using Object = std::vector<Member>;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(int v) : Value(static_cast<int64_t>(v)) {}
    Value(int64_t v) : type_(Type::Number), num_(static_cast<double>(v)), int_(v), isInt_(true) {}
    Value(uint32_t v) : Value(static_cast<int64_t>(v)) {}
    Value(double v);
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
    Value(const char* s) : Value(std::string(s)) {}
    Value(Array a) : type_(Type::Array), arr_(std::move(a)) {}
    Value(Object o) : type_(Type::Object), obj_(std::move(o)) {}

    static Value array() { return Value(Array{}); }
    static Value object() { return Value(Object{}); }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isInteger() const { return type_ == Type::Number && isInt_; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool() const { return bool_; }
    double asDouble() const { return num_; }
    int64_t asInt() const { return int_; }
    const std::string& asString() const { return str_; }
    const Array& asArray() const { return arr_; }
    Array& asArray() { return arr_; }
    const Object& asObject() const { return obj_; }
    Object& asObject() { return obj_; }

    // Object access. find() returns nullptr when the key is missing (or this is not an object).
    const Value* find(std::string_view key) const;
    // Adds or replaces a key (this must be an object).
    Value& set(std::string key, Value v);
    // Appends to an array.
    void push(Value v) { arr_.push_back(std::move(v)); }
    size_t size() const { return type_ == Type::Array ? arr_.size() : type_ == Type::Object ? obj_.size() : 0; }

    static const char* typeName(Type t);

    bool operator==(const Value& o) const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0;
    int64_t int_ = 0;
    bool isInt_ = false;
    std::string str_;
    Array arr_;
    Object obj_;
};

// Parse error with the position (1-based line and column) in what().
class ParseError : public FormatError {
public:
    ParseError(const std::string& msg, int line, int column);
    int line() const { return line_; }
    int column() const { return column_; }

private:
    int line_ = 0, column_ = 0;
};

// Parses one JSON document (a UTF-8 BOM is skipped). Throws ParseError.
Value parse(std::string_view text);

struct WriteOptions {
    int indent = 2;            // 0 = compact, one line
    int wrapColumn = 100;      // arrays of scalars are packed onto lines up to this width
};
// Serialises a value; the output ends with a newline when indent > 0.
std::string write(const Value& v, const WriteOptions& options = {});

}  // namespace tmport::mods::json
