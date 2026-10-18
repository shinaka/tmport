#pragma once
// Little-endian reader over a byte span with bounds checks (throws tmport::FormatError).

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

#include "util/error.h"

namespace tmport {

class ByteReader {
public:
    ByteReader() = default;
    explicit ByteReader(std::span<const uint8_t> data, std::string what = "data")
        : data_(data), what_(std::move(what)) {}

    std::span<const uint8_t> data() const { return data_; }
    size_t size() const { return data_.size(); }
    bool has(size_t offset, size_t count) const {
        return offset <= data_.size() && count <= data_.size() - offset;
    }

    // Random access.
    uint8_t u8(size_t off) const { need(off, 1); return data_[off]; }
    int8_t i8(size_t off) const { return static_cast<int8_t>(u8(off)); }
    uint16_t u16(size_t off) const {
        need(off, 2);
        return static_cast<uint16_t>(data_[off] | (data_[off + 1] << 8));
    }
    int16_t i16(size_t off) const { return static_cast<int16_t>(u16(off)); }
    uint32_t u32(size_t off) const {
        need(off, 4);
        return static_cast<uint32_t>(data_[off]) | (static_cast<uint32_t>(data_[off + 1]) << 8) |
               (static_cast<uint32_t>(data_[off + 2]) << 16) | (static_cast<uint32_t>(data_[off + 3]) << 24);
    }
    int32_t i32(size_t off) const { return static_cast<int32_t>(u32(off)); }
    std::span<const uint8_t> bytes(size_t off, size_t count) const {
        need(off, count);
        return data_.subspan(off, count);
    }
    ByteReader sub(size_t off, size_t count) const { return ByteReader(bytes(off, count), what_); }

    // Sequential access.
    size_t pos() const { return pos_; }
    void seek(size_t off) {
        if (off > data_.size()) fail(off, 0);
        pos_ = off;
    }
    void skip(size_t count) { seek(pos_ + count); }
    uint8_t readU8() { auto v = u8(pos_); pos_ += 1; return v; }
    uint16_t readU16() { auto v = u16(pos_); pos_ += 2; return v; }
    int16_t readI16() { auto v = i16(pos_); pos_ += 2; return v; }
    uint32_t readU32() { auto v = u32(pos_); pos_ += 4; return v; }
    int32_t readI32() { auto v = i32(pos_); pos_ += 4; return v; }
    std::span<const uint8_t> readBytes(size_t count) { auto v = bytes(pos_, count); pos_ += count; return v; }

private:
    void need(size_t off, size_t count) const {
        if (!has(off, count)) fail(off, count);
    }
    [[noreturn]] void fail(size_t off, size_t count) const {
        throw FormatError(std::format("{}: read of {} byte(s) at offset {:#x} is outside {} byte(s)", what_, count,
                                      off, data_.size()));
    }

    std::span<const uint8_t> data_;
    std::string what_ = "data";
    size_t pos_ = 0;
};

}  // namespace tmport
