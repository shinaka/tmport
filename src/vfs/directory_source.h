#pragma once
// Plain directory data source with case-insensitive lookup.

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>

#include "vfs/data_source.h"

namespace tmport::vfs {

class DirectorySource final : public DataSource {
public:
    explicit DirectorySource(std::filesystem::path root);

    std::string describe() const override;
    std::shared_ptr<File> open(std::string_view relPath) const override;
    std::vector<DirEntry> list(std::string_view relDir) const override;

    const std::filesystem::path& root() const { return root_; }
    // Resolves a relative path case-insensitively; nullopt if any component is missing.
    std::optional<std::filesystem::path> resolve(std::string_view relPath, bool wantDir) const;

private:
    struct Listing {
        std::vector<std::pair<std::string, bool>> entries;  // name, isDirectory
    };
    const Listing& listing(const std::filesystem::path& dir) const;

    std::filesystem::path root_;
    mutable std::mutex mutex_;
    mutable std::map<std::filesystem::path, Listing> cache_;
};

// A file on the host file system.
class HostFile final : public File {
public:
    static std::shared_ptr<HostFile> open(const std::filesystem::path& p);
    uint64_t size() const override { return size_; }
    size_t read(uint64_t offset, std::span<uint8_t> out) override;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    uint64_t size_ = 0;
};

}  // namespace tmport::vfs
