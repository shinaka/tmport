#include "dos/gfx/pak.h"

#include <format>

#include "util/byte_reader.h"
#include "util/error.h"

namespace tmport::dos {

namespace {

constexpr size_t kFrameHeader = 16;

[[noreturn]] void fail(const std::string& name, const std::string& why) {
    throw FormatError(std::format("{}: {}", name, why));
}

// One frame, the way the blitter reads it (1000:1cbb raw / run rows); `next` = start of the
// following frame (or the file end) for the slot size.
PakFrame decodeFrame(const ByteReader& r, size_t off, size_t next, int index, const std::string& name) {
    PakFrame f;
    f.index = index;
    f.fileOffset = off;
    if (!r.has(off, kFrameHeader)) fail(name, std::format("frame {}: header past the end of the file", index));
    f.word0 = r.u16(off);
    f.widthBytes = r.u16(off + 2);
    f.height = r.u16(off + 4);
    f.xOffset = r.i16(off + 6);
    f.yOffset = r.i16(off + 8);
    f.fieldA = r.u16(off + 0x0A);
    f.fieldC = r.u16(off + 0x0C);
    f.fieldE = r.u16(off + 0x0E);
    f.rows.reserve(static_cast<size_t>(f.widthBytes) * f.height);
    size_t si = off + kFrameHeader;
    for (int y = 0; y < f.height; ++y) {
        const uint8_t len = r.u8(si++);
        const size_t before = f.rows.size();
        if (len & 0x80) {  // raw row (1000:1d1e)
            const auto bytes = r.bytes(si, len & 0x7F);
            f.rows.insert(f.rows.end(), bytes.begin(), bytes.end());
            si += len & 0x7F;
            ++f.rawRows;
        } else {  // (count, byte) runs (1000:1cda)
            if (len % 2) fail(name, std::format("frame {} row {}: odd run list", index, y));
            for (size_t i = 0; i < len; i += 2) f.rows.insert(f.rows.end(), r.u8(si + i), r.u8(si + i + 1));
            si += len;
            ++f.runRows;
        }
        if (f.rows.size() - before != static_cast<size_t>(f.widthBytes))
            fail(name, std::format("frame {} row {}: {} bytes, the header says {}", index, y, f.rows.size() - before,
                                   f.widthBytes));
    }
    f.usedBytes = si - off;
    f.slotBytes = next > off ? next - off : 0;
    return f;
}

}  // namespace

PakFile PakFile::load(std::span<const uint8_t> file, std::string name) {
    PakFile p;
    p.name_ = std::move(name);
    const ByteReader r(file, p.name_);
    if (file.size() < 12) fail(p.name_, "file too short");
    const int count = r.u16(0);
    for (int i = 0; i < 3; ++i) p.header_[i] = r.u16(2 + 2 * static_cast<size_t>(i));
    const size_t table = 8, trailer = table + 2 * static_cast<size_t>(count);
    if (!r.has(trailer, 4) || r.u16(trailer) != 0xFFFF || r.u16(trailer + 2) != 0xFFFF)
        fail(p.name_, "offset table is not followed by FFFF FFFF");
    p.frames_.reserve(static_cast<size_t>(count));
    for (int k = 0; k < count; ++k) {
        const size_t off = static_cast<size_t>(r.u16(table + 2 * static_cast<size_t>(k))) * 16;
        const size_t next = k + 1 < count ? static_cast<size_t>(r.u16(table + 2 * static_cast<size_t>(k) + 2)) * 16 : file.size();
        p.frames_.push_back(decodeFrame(r, off, next, k, p.name_));
    }
    return p;
}

}  // namespace tmport::dos
