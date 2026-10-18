#include "vfs/directory_source.h"

#include <fstream>
#include <system_error>

#include "util/file_util.h"

namespace tmport::vfs {

struct HostFile::Impl {
    std::ifstream stream;
};

std::shared_ptr<HostFile> HostFile::open(const std::filesystem::path& p) {
    auto impl = std::make_shared<Impl>();
    impl->stream.open(p, std::ios::binary);
    if (!impl->stream) return nullptr;
    impl->stream.seekg(0, std::ios::end);
    const std::streamoff n = impl->stream.tellg();
    if (n < 0) return nullptr;
    auto f = std::make_shared<HostFile>();
    f->impl_ = std::move(impl);
    f->size_ = static_cast<uint64_t>(n);
    return f;
}

size_t HostFile::read(uint64_t offset, std::span<uint8_t> out) {
    if (offset >= size_ || out.empty()) return 0;
    size_t n = out.size();
    if (n > size_ - offset) n = static_cast<size_t>(size_ - offset);
    auto& s = impl_->stream;
    s.clear();
    s.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    s.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n));
    return static_cast<size_t>(s.gcount());
}

DirectorySource::DirectorySource(std::filesystem::path root) : root_(std::move(root)) {}

std::string DirectorySource::describe() const { return "directory " + pathToUtf8(root_); }

const DirectorySource::Listing& DirectorySource::listing(const std::filesystem::path& dir) const {
    auto it = cache_.find(dir);
    if (it != cache_.end()) return it->second;
    Listing l;
    std::error_code ec;
    for (std::filesystem::directory_iterator di(dir, ec), end; !ec && di != end; di.increment(ec)) {
        std::error_code ec2;
        l.entries.emplace_back(pathToUtf8(di->path().filename()), di->is_directory(ec2));
    }
    return cache_.emplace(dir, std::move(l)).first->second;
}

std::optional<std::filesystem::path> DirectorySource::resolve(std::string_view relPath, bool wantDir) const {
    std::lock_guard lock(mutex_);
    const auto parts = splitPath(relPath);
    std::filesystem::path cur = root_;
    for (size_t i = 0; i < parts.size(); ++i) {
        const bool last = i + 1 == parts.size();
        const bool needDir = !last || wantDir;
        bool found = false;
        for (const auto& [name, isDir] : listing(cur).entries) {
            if (isDir == needDir && iequalsAscii(name, parts[i])) {
                cur /= pathFromUtf8(name);
                found = true;
                break;
            }
        }
        if (!found) return std::nullopt;
    }
    if (parts.empty() && !wantDir) return std::nullopt;
    return cur;
}

std::shared_ptr<File> DirectorySource::open(std::string_view relPath) const {
    auto p = resolve(relPath, false);
    if (!p) return nullptr;
    return HostFile::open(*p);
}

std::vector<DirEntry> DirectorySource::list(std::string_view relDir) const {
    std::vector<DirEntry> out;
    auto p = resolve(relDir, true);
    if (!p) return out;
    std::error_code ec;
    for (std::filesystem::directory_iterator di(*p, ec), end; !ec && di != end; di.increment(ec)) {
        DirEntry e;
        e.name = pathToUtf8(di->path().filename());
        std::error_code ec2;
        e.isDirectory = di->is_directory(ec2);
        if (!e.isDirectory) {
            const auto sz = di->file_size(ec2);
            e.size = ec2 ? 0 : static_cast<uint64_t>(sz);
        }
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace tmport::vfs
