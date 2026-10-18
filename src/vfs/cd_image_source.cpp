#include "vfs/cd_image_source.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <format>
#include <sstream>
#include <system_error>

#include "util/byte_reader.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/log.h"
#include "vfs/directory_source.h"

namespace tmport::vfs {

namespace {

constexpr uint32_t kUserSize = 2048;
constexpr uint8_t kSync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

// True if the image has an ISO 9660 volume descriptor ("CD001") at LBA 16 with this layout.
bool probeIso(File& f, const SectorImage::Layout& l) {
    uint8_t buf[6] = {};
    const uint64_t off = l.baseOffset + 16ull * l.sectorSize + l.dataOffset;
    if (f.read(off, buf) != sizeof(buf)) return false;
    return std::memcmp(buf + 1, "CD001", 5) == 0;
}

std::optional<std::filesystem::path> findCaseInsensitive(const std::filesystem::path& dir, const std::string& name) {
    std::error_code ec;
    const std::filesystem::path direct = dir / pathFromUtf8(name);
    if (std::filesystem::is_regular_file(direct, ec)) return direct;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (iequalsAscii(pathToUtf8(it->path().filename()), name)) return it->path();
    return std::nullopt;
}

// Reads a cue-sheet token: a quoted string or a run of non-blank characters.
std::string nextToken(std::istringstream& in) {
    in >> std::ws;
    if (in.peek() == '"') {
        in.get();
        std::string s;
        std::getline(in, s, '"');
        return s;
    }
    std::string s;
    in >> s;
    return s;
}

}  // namespace

// ------------------------------------------------------------------------------- SectorImage

SectorImage::SectorImage(const std::filesystem::path& file, Layout layout)
    : file_(file), layout_(std::move(layout)), cache_(kUserSize) {
    host_ = HostFile::open(file);
    if (!host_) throw MissingFileError(pathToUtf8(file), std::format("cannot open CD image '{}'", pathToUtf8(file)));
}

std::shared_ptr<SectorImage> SectorImage::openDetect(const std::filesystem::path& file) {
    auto host = HostFile::open(file);
    if (!host) throw MissingFileError(pathToUtf8(file), std::format("cannot open CD image '{}'", pathToUtf8(file)));

    std::vector<Layout> candidates;
    uint8_t head[16] = {};
    if (host->read(0, head) == sizeof(head) && std::memcmp(head, kSync, sizeof(kSync)) == 0) {
        // Raw 2352-byte sectors: the mode byte follows the 3-byte address.
        if (head[15] == 2)
            candidates.push_back({2352, 24, 0, "MODE2/2352"});
        else
            candidates.push_back({2352, 16, 0, "MODE1/2352"});
    }
    candidates.push_back({2048, 0, 0, "MODE1/2048"});
    candidates.push_back({2352, 16, 0, "MODE1/2352"});
    candidates.push_back({2352, 24, 0, "MODE2/2352"});
    for (const Layout& l : candidates)
        if (probeIso(*host, l)) return std::make_shared<SectorImage>(file, l);
    throw FormatError(std::format("'{}' is not an ISO 9660 CD image (no CD001 descriptor found)", pathToUtf8(file)));
}

uint64_t SectorImage::sectorCount() const {
    const uint64_t sz = host_->size();
    if (sz <= layout_.baseOffset) return 0;
    return (sz - layout_.baseOffset) / layout_.sectorSize;
}

bool SectorImage::readSector(uint32_t lba, uint8_t* out) {
    if (cachedLba_ == static_cast<int64_t>(lba)) {
        std::memcpy(out, cache_.data(), kUserSize);
        return true;
    }
    const uint64_t off = layout_.baseOffset + static_cast<uint64_t>(lba) * layout_.sectorSize + layout_.dataOffset;
    if (host_->read(off, std::span<uint8_t>(cache_.data(), kUserSize)) != kUserSize) {
        cachedLba_ = -1;
        return false;
    }
    cachedLba_ = lba;
    std::memcpy(out, cache_.data(), kUserSize);
    return true;
}

// --------------------------------------------------------------------------------- Cue sheet

CueInfo parseCueSheet(const std::filesystem::path& cuePath) {
    auto text = readWholeFile(cuePath);
    if (!text) throw MissingFileError(pathToUtf8(cuePath), std::format("cannot read cue sheet '{}'", pathToUtf8(cuePath)));
    std::istringstream lines(std::string(text->begin(), text->end()));
    std::string line, currentFile;
    bool inDataTrack = false, found = false;
    CueInfo info;
    while (std::getline(lines, line)) {
        std::istringstream in(line);
        const std::string kw = toUpperAscii(nextToken(in));
        if (kw == "FILE") {
            currentFile = nextToken(in);
            inDataTrack = false;
        } else if (kw == "TRACK" && !found) {
            nextToken(in);  // track number
            const std::string mode = toUpperAscii(nextToken(in));
            inDataTrack = true;
            if (mode == "MODE1/2352")
                info.layout = {2352, 16, 0, mode};
            else if (mode == "MODE2/2352")
                info.layout = {2352, 24, 0, mode};
            else if (mode == "MODE1/2048")
                info.layout = {2048, 0, 0, mode};
            else
                inDataTrack = false;  // audio or unsupported
            if (inDataTrack) {
                if (currentFile.empty()) throw FormatError("cue sheet: TRACK before FILE");
                auto bin = findCaseInsensitive(cuePath.parent_path(), currentFile);
                if (!bin)
                    throw MissingFileError(currentFile, std::format("cue sheet '{}' refers to missing file '{}'",
                                                                    pathToUtf8(cuePath), currentFile));
                info.binFile = *bin;
                found = true;
            }
        } else if (kw == "INDEX" && inDataTrack && found) {
            const std::string num = nextToken(in);
            const std::string msf = nextToken(in);
            if (num == "01" || num == "1") {
                int m = 0, s = 0, fr = 0;
                char c1 = 0, c2 = 0;
                std::istringstream t(msf);
                if (t >> m >> c1 >> s >> c2 >> fr) {
                    const uint64_t frames = static_cast<uint64_t>((m * 60 + s) * 75 + fr);
                    info.layout.baseOffset = frames * info.layout.sectorSize;
                }
                inDataTrack = false;  // only the first data track matters
            }
        }
    }
    if (!found) throw FormatError(std::format("cue sheet '{}' has no data track", pathToUtf8(cuePath)));
    return info;
}

// ------------------------------------------------------------------------------- ISO 9660 file

namespace {

class IsoFile final : public File {
public:
    IsoFile(std::shared_ptr<SectorImage> img, uint32_t lba, uint32_t size) : img_(std::move(img)), lba_(lba), size_(size) {}
    uint64_t size() const override { return size_; }
    size_t read(uint64_t offset, std::span<uint8_t> out) override {
        if (offset >= size_ || out.empty()) return 0;
        size_t want = out.size();
        if (want > size_ - offset) want = static_cast<size_t>(size_ - offset);
        size_t done = 0;
        uint8_t sector[kUserSize];
        while (done < want) {
            const uint64_t pos = offset + done;
            const uint32_t lba = lba_ + static_cast<uint32_t>(pos / kUserSize);
            const size_t within = static_cast<size_t>(pos % kUserSize);
            const size_t n = std::min(want - done, static_cast<size_t>(kUserSize) - within);
            if (!img_->readSector(lba, sector)) break;
            std::memcpy(out.data() + done, sector + within, n);
            done += n;
        }
        return done;
    }

private:
    std::shared_ptr<SectorImage> img_;
    uint32_t lba_;
    uint32_t size_;
};

}  // namespace

// ------------------------------------------------------------------------------ CdImageSource

CdImageSource::CdImageSource(const std::filesystem::path& path) : path_(path) {
    const std::string ext = toLowerAscii(pathToUtf8(path.extension()));
    if (ext == ".cue") {
        const CueInfo cue = parseCueSheet(path);
        image_ = std::make_shared<SectorImage>(cue.binFile, cue.layout);
    } else {
        image_ = SectorImage::openDetect(path);
    }

    // Find the primary volume descriptor (type 1) from LBA 16 on.
    std::vector<uint8_t> pvd(kUserSize);
    bool havePvd = false;
    for (uint32_t lba = 16; lba < 16 + 32; ++lba) {
        if (!image_->readSector(lba, pvd.data())) break;
        if (std::memcmp(pvd.data() + 1, "CD001", 5) != 0) break;
        if (pvd[0] == 1) {
            havePvd = true;
            break;
        }
        if (pvd[0] == 255) break;
    }
    if (!havePvd) throw FormatError(std::format("'{}': no ISO 9660 primary volume descriptor", pathToUtf8(path)));

    ByteReader r(pvd, "ISO 9660 primary volume descriptor");
    {
        auto id = r.bytes(40, 32);
        volumeId_.assign(id.begin(), id.end());
        while (!volumeId_.empty() && (volumeId_.back() == ' ' || volumeId_.back() == '\0')) volumeId_.pop_back();
    }
    root_.name = "";
    root_.isDirectory = true;
    root_.lba = r.u32(156 + 2) + r.u8(156 + 1);
    root_.size = r.u32(156 + 10);

    auto game = findGameFolder(root_);
    if (!game)
        throw MissingFileError(std::string(kGameExeName),
                               std::format("CD image '{}' does not contain {}", pathToUtf8(path), kGameExeName));
    gameFolder_ = *game;
}

CdImageSource::~CdImageSource() = default;

std::vector<CdImageSource::Node> CdImageSource::readDirectory(const Node& dir) const {
    std::vector<Node> out;
    IsoFile f(image_, dir.lba, dir.size);
    const std::vector<uint8_t> data = f.readAll();
    size_t p = 0;
    while (p < data.size()) {
        const uint8_t len = data[p];
        if (len == 0) {  // records never cross a sector boundary; skip the rest of the sector
            p = (p / kUserSize + 1) * kUserSize;
            continue;
        }
        if (len < 34 || p + len > data.size()) break;
        ByteReader rec(std::span<const uint8_t>(data).subspan(p, len), "ISO 9660 directory record");
        const uint8_t xar = rec.u8(1);
        Node n;
        n.lba = rec.u32(2) + xar;
        n.size = rec.u32(10);
        n.isDirectory = (rec.u8(25) & 0x02) != 0;
        const uint8_t nameLen = rec.u8(32);
        auto nameBytes = rec.bytes(33, nameLen);
        p += len;
        if (nameLen == 1 && (nameBytes[0] == 0 || nameBytes[0] == 1)) continue;  // "." and ".."
        std::string name(nameBytes.begin(), nameBytes.end());
        if (auto semi = name.find(';'); semi != std::string::npos) name.resize(semi);
        if (!name.empty() && name.back() == '.') name.pop_back();
        n.name = std::move(name);
        out.push_back(std::move(n));
    }
    return out;
}

std::optional<CdImageSource::Node> CdImageSource::lookup(const Node& start, std::string_view relPath, bool wantDir) const {
    const auto parts = splitPath(relPath);
    Node cur = start;
    for (size_t i = 0; i < parts.size(); ++i) {
        const bool needDir = i + 1 < parts.size() || wantDir;
        bool found = false;
        for (const Node& n : readDirectory(cur)) {
            if (n.isDirectory == needDir && iequalsAscii(n.name, parts[i])) {
                cur = n;
                found = true;
                break;
            }
        }
        if (!found) return std::nullopt;
    }
    if (parts.empty() && !wantDir) return std::nullopt;
    return cur;
}

std::optional<CdImageSource::Node> CdImageSource::findGameFolder(const Node& root) {
    // Preferred: /TMTWINCD containing TMTWINCD.EXE; else the root itself; else a breadth-first
    // search (depth <= 3) for a directory containing it.
    if (auto d = lookup(root, kGameFolderName, true); d && lookup(*d, kGameExeName, false)) {
        gameFolderPath_ = "/" + d->name;
        return d;
    }
    if (lookup(root, kGameExeName, false)) {
        gameFolderPath_ = "/";
        return root;
    }
    std::deque<std::pair<Node, std::string>> queue{{root, ""}};
    while (!queue.empty()) {
        auto [dir, path] = queue.front();
        queue.pop_front();
        const int depth = static_cast<int>(std::count(path.begin(), path.end(), '/'));
        for (const Node& n : readDirectory(dir)) {
            if (!n.isDirectory) continue;
            const std::string p = path + "/" + n.name;
            if (lookup(n, kGameExeName, false)) {
                gameFolderPath_ = p;
                return n;
            }
            if (depth < 3) queue.emplace_back(n, p);
        }
    }
    return std::nullopt;
}

std::string CdImageSource::describe() const {
    return std::format("CD image {} ({} in {}, volume '{}'), game folder {}", pathToUtf8(path_), image_->layout().mode,
                       pathToUtf8(image_->file().filename()), volumeId_, gameFolderPath_);
}

std::shared_ptr<File> CdImageSource::open(std::string_view relPath) const {
    auto n = lookup(gameFolder_, relPath, false);
    if (!n) return nullptr;
    return std::make_shared<IsoFile>(image_, n->lba, n->size);
}

std::vector<DirEntry> CdImageSource::list(std::string_view relDir) const {
    std::vector<DirEntry> out;
    auto d = lookup(gameFolder_, relDir, true);
    if (!d) return out;
    for (const Node& n : readDirectory(*d)) out.push_back({n.name, n.isDirectory, n.size});
    return out;
}

}  // namespace tmport::vfs
