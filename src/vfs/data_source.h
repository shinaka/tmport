#pragma once
// Data-source abstraction: the user's game folder (TMTWINCD) either as a plain directory or
// inside a CD image (.cue/.bin/.iso, ISO 9660). All lookups are case-insensitive and use paths
// relative to the game folder, with '/' or '\' separators (e.g. "TMT.DLL", "SOUNDS/EOPEN1.MID").

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tmport::vfs {

// A read-only file with random access.
class File {
public:
    virtual ~File() = default;
    virtual uint64_t size() const = 0;
    // Reads up to out.size() bytes at offset; returns the number of bytes read (short at EOF).
    virtual size_t read(uint64_t offset, std::span<uint8_t> out) = 0;

    // Reads [offset, offset+count) clipped to the file size.
    std::vector<uint8_t> readRange(uint64_t offset, size_t count);
    std::vector<uint8_t> readAll();
};

struct DirEntry {
    std::string name;  // as stored (ISO names without ";1")
    bool isDirectory = false;
    uint64_t size = 0;
};

class DataSource {
public:
    virtual ~DataSource() = default;
    // Human-readable description, e.g. "directory C:\Games\TMTWINCD".
    virtual std::string describe() const = 0;
    // nullptr if the file does not exist.
    virtual std::shared_ptr<File> open(std::string_view relPath) const = 0;
    virtual std::vector<DirEntry> list(std::string_view relDir) const = 0;
    virtual bool exists(std::string_view relPath) const { return open(relPath) != nullptr; }
};

// Splits a relative path into components ('/' and '\' separators, empty parts dropped).
std::vector<std::string> splitPath(std::string_view relPath);

// The name of the game folder on the CD and the file that identifies it.
inline constexpr std::string_view kGameFolderName = "TMTWINCD";
inline constexpr std::string_view kGameExeName = "TMTWINCD.EXE";

// Opens a directory (the game folder itself or a folder containing TMTWINCD\) or a CD image
// (.cue, .bin, .iso; the image format is detected from the contents). Throws tmport::Error.
std::unique_ptr<DataSource> openDataSource(const std::filesystem::path& path);

// Searches the usual places for the game data (not the TM_DATA variable; the caller handles
// it): the current directory and the executable's directory and their parents, each as
// "<dir>" or "<dir>/TMTWINCD", plus a single .cue/.iso found in one of those directories.
std::optional<std::filesystem::path> autoDetectDataPath(const std::filesystem::path& exeDir);

}  // namespace tmport::vfs
