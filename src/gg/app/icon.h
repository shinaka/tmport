#pragma once
// The window icon from the user's ssg.ico (the game folder's icon file; no icon is in the
// repository). An .ico file is a directory of 16-byte entries {u8 width, u8 height, u8 colourCount,
// u8 reserved, u16 planes, u16 bitCount, u32 bytesInRes, u32 imageOffset} after a 6-byte header
// {u16 0, u16 1, u16 count}; each image is the DIB of an RT_ICON resource (platform/exe_icon.h).
// ssg.ico holds one 32x32 16-colour icon.

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "platform/exe_icon.h"

namespace tmport::gg {

struct IcoEntry {
    IconDirEntry dir;
    uint32_t offset = 0;  // file offset of the image
};

// Parses the directory of an .ico file. Throws FormatError.
std::vector<IcoEntry> parseIcoDirectory(std::span<const uint8_t> file);
// Decodes the best image of an .ico file (bestIconEntry). Throws FormatError.
IconImage decodeIcoFile(std::span<const uint8_t> file);
// The icon of `path`; nullopt (logged at debug level) when it is missing or cannot be decoded.
std::optional<IconImage> loadIcoFile(const std::filesystem::path& path);

}  // namespace tmport::gg
