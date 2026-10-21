#include "mods/mod_data_source.h"

#include <algorithm>
#include <cstring>

#include "mischief/util/file_util.h"
#include "mischief/util/log.h"

namespace tmport::mods {

namespace {

class MemoryFile final : public mischief::vfs::File {
public:
    explicit MemoryFile(std::vector<uint8_t> d) : d_(std::move(d)) {}
    uint64_t size() const override { return d_.size(); }
    size_t read(uint64_t offset, std::span<uint8_t> out) override {
        if (offset >= d_.size()) return 0;
        const size_t n = std::min<size_t>(out.size(), d_.size() - static_cast<size_t>(offset));
        std::memcpy(out.data(), d_.data() + offset, n);
        return n;
    }

private:
    std::vector<uint8_t> d_;
};

}  // namespace

ModDataSource::ModDataSource(std::unique_ptr<mischief::vfs::DataSource> base, const std::map<std::string, std::filesystem::path>& files)
    : base_(std::move(base)) {
    for (const auto& [k, p] : files) files_[key(k)] = p;
}

std::string ModDataSource::key(std::string_view relPath) {
    std::string k;
    for (const auto& part : mischief::vfs::splitPath(relPath)) {
        if (!k.empty()) k += '/';
        k += mischief::toUpperAscii(part);
    }
    return k;
}

std::string ModDataSource::describe() const { return base_->describe() + " (+ mod files)"; }

std::shared_ptr<mischief::vfs::File> ModDataSource::open(std::string_view relPath) const {
    if (auto it = files_.find(key(relPath)); it != files_.end()) {
        if (auto data = mischief::readWholeFile(it->second)) {
            mischief::log::debug("mod: {} <- {}", relPath, mischief::pathToUtf8(it->second));
            return std::make_shared<MemoryFile>(std::move(*data));
        }
        mischief::log::error("mod: cannot read {} (the game's own {} is used)", mischief::pathToUtf8(it->second), relPath);
    }
    return base_->open(relPath);
}

}  // namespace tmport::mods
