#pragma once
// Small helpers shared by the mod-layer codecs.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "util/error.h"

namespace tmport::mods {

// NE resources are stored in 512-byte units (alignment shift 9 in every game DLL), so the
// original's GetResource copies carry zero padding up to the next multiple of 512. Converted mod
// resources get the same padding, so code that relies on the rounded size (Res_GetSize, the
// object table that reads 128 entries from 512 bytes) sees what it would see for an original.
inline constexpr size_t kNeAlignment = 512;
inline size_t roundUpNe(size_t n) { return (n + kNeAlignment - 1) / kNeAlignment * kNeAlignment; }
inline void padToNe(std::vector<uint8_t>& v) { v.resize(roundUpNe(v.size()), 0); }

// Bytes up to (excluding) the last non-zero byte + 1.
inline size_t significantLength(std::span<const uint8_t> v) {
    size_t n = v.size();
    while (n > 0 && v[n - 1] == 0) --n;
    return n;
}

// True when `body` equals `significant` followed by zero padding to the NE size.
inline bool isNePadded(std::span<const uint8_t> body, size_t significant) {
    if (body.size() != roundUpNe(significant)) return false;
    for (size_t i = significant; i < body.size(); ++i)
        if (body[i] != 0) return false;
    return true;
}

// A syntax error in a mod text file, with the 1-based line (and column, 0 if unknown).
class ModSyntaxError : public FormatError {
public:
    ModSyntaxError(const std::string& msg, int line, int column = 0)
        : FormatError(column > 0 ? "line " + std::to_string(line) + ", column " + std::to_string(column) + ": " + msg
                                 : "line " + std::to_string(line) + ": " + msg),
          line_(line),
          column_(column) {}
    int line() const { return line_; }
    int column() const { return column_; }

private:
    int line_, column_;
};

inline std::string_view trimView(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

}  // namespace tmport::mods
