#pragma once
// CD image data source: ISO 9660 inside a .cue (BINARY, MODE1/2352, MODE2/2352 or MODE1/2048
// track), a raw .bin (2352-byte sectors, detected by the sync pattern) or a cooked .iso
// (2048-byte sectors). The game folder TMTWINCD is located inside the file system.

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "vfs/data_source.h"

namespace tmport::vfs {

// Reads 2048-byte user-data sectors from an image file.
class SectorImage {
public:
    struct Layout {
        uint32_t sectorSize = 2048;  // bytes per sector in the file: 2048 or 2352
        uint32_t dataOffset = 0;     // user data offset inside a sector: 0, 16 (MODE1) or 24 (MODE2 form 1)
        uint64_t baseOffset = 0;     // file offset of LBA 0
        std::string mode = "MODE1/2048";
    };

    SectorImage(const std::filesystem::path& file, Layout layout);
    // Detects the layout of a .bin/.iso from its contents (ISO 9660 signature at sector 16).
    static std::shared_ptr<SectorImage> openDetect(const std::filesystem::path& file);

    const Layout& layout() const { return layout_; }
    const std::filesystem::path& file() const { return file_; }
    uint64_t sectorCount() const;
    // Fills out (2048 bytes) with sector lba. Returns false past the end of the image.
    bool readSector(uint32_t lba, uint8_t* out);

private:
    std::filesystem::path file_;
    Layout layout_;
    std::shared_ptr<File> host_;
    // One-sector cache.
    int64_t cachedLba_ = -1;
    std::vector<uint8_t> cache_;
};

class CdImageSource final : public DataSource {
public:
    // path: .cue, .bin or .iso. Throws tmport::Error if the image or the game folder is not found.
    explicit CdImageSource(const std::filesystem::path& path);
    ~CdImageSource() override;

    std::string describe() const override;
    std::shared_ptr<File> open(std::string_view relPath) const override;
    std::vector<DirEntry> list(std::string_view relDir) const override;

    std::string volumeId() const { return volumeId_; }
    // Path of the game folder inside the image, e.g. "/TMTWINCD".
    std::string gameFolder() const { return gameFolderPath_; }

    struct Node {
        std::string name;
        bool isDirectory = false;
        uint32_t lba = 0;
        uint32_t size = 0;
    };

private:
    std::vector<Node> readDirectory(const Node& dir) const;
    std::optional<Node> lookup(const Node& start, std::string_view relPath, bool wantDir) const;
    std::optional<Node> findGameFolder(const Node& root);

    std::filesystem::path path_;
    std::shared_ptr<SectorImage> image_;
    std::string volumeId_;
    Node root_;
    Node gameFolder_;
    std::string gameFolderPath_;
};

// Parses a cue sheet; returns the image file (resolved next to the cue) and its layout.
struct CueInfo {
    std::filesystem::path binFile;
    SectorImage::Layout layout;
};
CueInfo parseCueSheet(const std::filesystem::path& cuePath);

}  // namespace tmport::vfs
