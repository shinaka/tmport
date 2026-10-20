#include "gg/app/icon.h"

#include <format>

#include "util/error.h"
#include "util/file_util.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

uint16_t u16(std::span<const uint8_t> d, size_t o) { return static_cast<uint16_t>(d[o] | (d[o + 1] << 8)); }
uint32_t u32(std::span<const uint8_t> d, size_t o) { return u16(d, o) | (static_cast<uint32_t>(u16(d, o + 2)) << 16); }

}  // namespace

std::vector<IcoEntry> parseIcoDirectory(std::span<const uint8_t> file) {
    if (file.size() < 6 || u16(file, 0) != 0 || u16(file, 2) != 1) throw FormatError("not an .ico file");
    const uint16_t count = u16(file, 4);
    if (count == 0 || file.size() < 6 + 16u * count) throw FormatError("truncated .ico directory");
    std::vector<IcoEntry> out;
    for (uint16_t i = 0; i < count; ++i) {
        const size_t o = 6 + 16u * i;
        IcoEntry e;
        e.dir.width = file[o] == 0 ? 256 : file[o];
        e.dir.height = file[o + 1] == 0 ? 256 : file[o + 1];
        e.dir.colourCount = file[o + 2];
        e.dir.planes = u16(file, o + 4);
        e.dir.bitCount = u16(file, o + 6);
        e.dir.bytesInRes = u32(file, o + 8);
        e.dir.iconId = i;
        e.offset = u32(file, o + 12);
        if (e.offset > file.size() || e.dir.bytesInRes > file.size() - e.offset)
            throw FormatError(std::format(".ico image {} lies outside the file", i));
        out.push_back(e);
    }
    return out;
}

IconImage decodeIcoFile(std::span<const uint8_t> file) {
    const auto entries = parseIcoDirectory(file);
    std::vector<IconDirEntry> dirs;
    for (const IcoEntry& e : entries) dirs.push_back(e.dir);
    const IconDirEntry* best = bestIconEntry(dirs);
    if (!best) throw FormatError("empty .ico file");
    const IcoEntry& e = entries[static_cast<size_t>(best - dirs.data())];
    return decodeIconResource(file.subspan(e.offset, e.dir.bytesInRes));
}

std::optional<IconImage> loadIcoFile(const std::filesystem::path& path) {
    const auto bytes = readWholeFile(path);
    if (!bytes) {
        log::debug("no window icon: {} cannot be read", pathToUtf8(path));
        return std::nullopt;
    }
    try {
        return decodeIcoFile(*bytes);
    } catch (const std::exception& e) {
        log::debug("no window icon: {}: {}", pathToUtf8(path), e.what());
        return std::nullopt;
    }
}

}  // namespace tmport::gg
