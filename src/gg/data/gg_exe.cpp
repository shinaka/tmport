#include "gg/data/gg_exe.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include "util/file_util.h"

namespace tmport::gg {

namespace {

constexpr std::string_view kBorland = "Borland C++ - Copyright 1991 Borland Intl.";
constexpr uint8_t kTlink[6] = {0x01, 0x00, 0xFB, 0x50, 0x6A, 0x72};  // file 0x1C: TLINK 5.0
constexpr size_t kFbovSize = 16;                                     // 'FBOV', u32 size, u32 table, i32 count
constexpr uint16_t kSegFlagStub = 3;                                 // segment table: code with overlay

uint16_t le16(std::span<const uint8_t> b, size_t at) { return static_cast<uint16_t>(b[at] | (b[at + 1] << 8)); }
uint32_t le32(std::span<const uint8_t> b, size_t at) {
    return static_cast<uint32_t>(le16(b, at)) | (static_cast<uint32_t>(le16(b, at + 2)) << 16);
}

[[noreturn]] void notV10(const std::string& why) {
    throw std::runtime_error("not ssg.exe of Gizmos & Gadgets! DOS v1.0 (" + why + ")");
}

}  // namespace

GgExe GgExe::load(std::vector<uint8_t> bytes) {
    GgExe exe;
    exe.file_ = std::move(bytes);
    const std::span<const uint8_t> f = exe.file_;
    if (f.size() < 0x40 || f[0] != 'M' || f[1] != 'Z') notV10("no MZ header");
    if (f.size() != kFileSize) notV10(std::to_string(f.size()) + " bytes instead of " + std::to_string(kFileSize));

    // MZ header: e_cblp 0 means a full last page.
    const uint16_t lastPage = le16(f, 2), pages = le16(f, 4), hdrParas = le16(f, 8);
    const size_t loadEnd = static_cast<size_t>(pages) * 512u - (lastPage ? 512u - lastPage : 0u);
    if (hdrParas * 16u != kHeaderSize || loadEnd != kLoadModuleEnd) notV10("unexpected MZ layout");
    if (!std::equal(std::begin(kTlink), std::end(kTlink), f.begin() + 0x1C)) notV10("no TLINK signature");
    if (std::memcmp(f.data() + kDsFileOffset + 4, kBorland.data(), kBorland.size()) != 0)
        notV10("no Borland C++ signature at DS:0004");

    // C0 start-up (CS:IP 0000:0000): MOV DX,DGROUP.
    const auto img = exe.image();
    const size_t entry = static_cast<size_t>(le16(f, 0x16)) * 16u + le16(f, 0x14);
    if (entry + 3 > img.size() || img[entry] != 0xBA || le16(img, entry + 1) != kDgroupImageSeg)
        notV10("the entry point does not load DGROUP 0ea7");

    // VROOMM: 'FBOV' after the load module, then the segment table inside the load module.
    if (std::memcmp(f.data() + kLoadModuleEnd, "FBOV", 4) != 0) notV10("no FBOV overlay header");
    const uint32_t segTable = le32(f, kLoadModuleEnd + 8);
    const int32_t segCount = static_cast<int32_t>(le32(f, kLoadModuleEnd + 12));
    const size_t ovrBase = kLoadModuleEnd + kFbovSize;
    if (segTable < kHeaderSize || segCount <= 0 || segTable + 8u * static_cast<uint32_t>(segCount) > kLoadModuleEnd)
        notV10("bad overlay segment table");

    // Overlays in stub order; their code gets consecutive paragraph-aligned Ghidra segments after
    // the load module, as in the flattened image of tools/gg/vroomm.py.
    uint16_t nextSeg = static_cast<uint16_t>(img.size() / 16u + kGhidraBase);
    for (int32_t i = 0; i < segCount; ++i) {
        const size_t e = segTable + 8u * static_cast<size_t>(i);
        if (le16(f, e + 4) != kSegFlagStub) continue;
        const size_t stub = static_cast<size_t>(le16(f, e)) * 16u;
        if (stub + kStubHeaderSize > img.size() || img[stub] != 0xCD || img[stub + 1] != 0x3F)
            notV10("bad overlay stub");
        Overlay ov;
        ov.number = static_cast<int>(exe.overlays_.size());
        ov.stubSeg = static_cast<uint16_t>(le16(f, e) + kGhidraBase);
        const uint32_t rec = le32(img, stub + 4);
        const uint16_t codeSize = le16(img, stub + 8), fixBytes = le16(img, stub + 10), count = le16(img, stub + 12);
        ov.fileOffset = ovrBase + rec;
        if (ov.fileOffset + codeSize + fixBytes > f.size()) notV10("overlay past the end of the file");
        ov.code = f.subspan(ov.fileOffset, codeSize);
        for (size_t k = 0; k < fixBytes / 2u; ++k) {
            const uint16_t at = le16(f, ov.fileOffset + codeSize + 2 * k);
            if (at + 2u > codeSize) notV10("overlay fix-up outside its code");
            ov.fixups.push_back(at);
        }
        for (size_t k = 0; k < count; ++k) {
            const size_t eo = stub + kStubHeaderSize + kStubEntrySize * k;
            if (eo + kStubEntrySize > img.size() || img[eo] != 0xCD || img[eo + 1] != 0x3F) notV10("bad stub entry");
            ov.entries.push_back(le16(img, eo + 2));
        }
        if (codeSize > 0) {
            ov.seg = nextSeg;
            nextSeg = static_cast<uint16_t>(nextSeg + (codeSize + 15u) / 16u);
        }
        exe.overlays_.push_back(std::move(ov));
    }
    if (exe.overlays_.size() != kOverlayCount) notV10(std::to_string(exe.overlays_.size()) + " overlays");
    if (exe.overlays_.front().seg != kFirstOverlaySeg) notV10("unexpected overlay layout");
    return exe;
}

GgExe GgExe::loadFile(const std::filesystem::path& ssgExe) {
    auto bytes = readWholeFile(ssgExe);
    if (!bytes) throw std::runtime_error("cannot read " + pathToUtf8(ssgExe));
    return load(std::move(*bytes));
}

std::span<const uint8_t> GgExe::image() const {
    return std::span<const uint8_t>(file_).subspan(kHeaderSize, kLoadModuleEnd - kHeaderSize);
}

const GgExe::Overlay* GgExe::overlayBySeg(uint16_t ghidraSeg) const {
    for (const auto& ov : overlays_) {
        if (ov.stubSeg == ghidraSeg) return &ov;
        if (ov.seg != 0 && ghidraSeg >= ov.seg && ghidraSeg < ov.seg + (ov.code.size() + 15u) / 16u) return &ov;
    }
    return nullptr;
}

const GgExe::Overlay* GgExe::overlayByStub(uint16_t ghidraStubSeg) const {
    for (const auto& ov : overlays_)
        if (ov.stubSeg == ghidraStubSeg) return &ov;
    return nullptr;
}

std::span<const uint8_t> GgExe::at(uint16_t seg, uint16_t off) const {
    if (seg < kGhidraBase) return {};
    if (seg >= kFirstOverlaySeg) {
        const Overlay* ov = overlayBySeg(seg);
        if (!ov || ov->seg == 0 || seg < ov->seg) return {};
        const size_t pos = static_cast<size_t>(seg - ov->seg) * 16u + off;
        return pos < ov->code.size() ? ov->code.subspan(pos) : std::span<const uint8_t>{};
    }
    const auto img = image();
    const size_t pos = static_cast<size_t>(seg - kGhidraBase) * 16u + off;
    return pos < img.size() ? img.subspan(pos) : std::span<const uint8_t>{};
}

size_t GgExe::fileOffset(uint16_t seg, uint16_t off) const {
    const auto b = at(seg, off);
    if (b.empty()) return SIZE_MAX;
    return static_cast<size_t>(b.data() - file_.data());
}

std::span<const uint8_t> GgExe::dgroup() const {
    return std::span<const uint8_t>(file_).subspan(kDsFileOffset, kDgroupSize);
}

uint8_t GgExe::ds8(uint16_t off) const { return dsBytes(off, 1)[0]; }

uint16_t GgExe::ds16(uint16_t off) const {
    const auto b = dsBytes(off, 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}

uint32_t GgExe::ds32(uint16_t off) const {
    const auto b = dsBytes(off, 4);
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
           (static_cast<uint32_t>(b[3]) << 24);
}

std::string_view GgExe::dsString(uint16_t off) const {
    const auto ds = dgroup();
    if (off >= ds.size()) throw std::out_of_range("DS offset out of range");
    const auto b = ds.subspan(off);
    const auto nul = std::find(b.begin(), b.end(), uint8_t{0});
    return {reinterpret_cast<const char*>(b.data()), static_cast<size_t>(nul - b.begin())};
}

std::span<const uint8_t> GgExe::dsBytes(uint16_t off, size_t n) const {
    const auto ds = dgroup();
    if (off > ds.size() || n > ds.size() - off) throw std::out_of_range("DS offset out of range");
    return ds.subspan(off, n);
}

void GgExe::writeDs(uint16_t off, std::span<const uint8_t> bytes) {
    if (static_cast<size_t>(off) + bytes.size() > kDsInitEnd)
        throw std::out_of_range("DS write past the initialised DGROUP");
    std::copy(bytes.begin(), bytes.end(), file_.begin() + static_cast<std::ptrdiff_t>(kDsFileOffset + off));
}

GgExe::Address GgExe::dsFarPtr(uint16_t dsOff) const {
    const uint16_t off = ds16(dsOff), seg = ds16(static_cast<uint16_t>(dsOff + 2));
    if (seg == 0 && off == 0) return {};
    return {static_cast<uint16_t>(seg + kGhidraBase), off};
}

uint16_t GgExe::dsOffsetOf(Address p) const {
    const long linear = (static_cast<long>(p.seg) - kDgroupSeg) * 16L + p.off;
    if (p.isNull() || linear < 0 || linear >= kDgroupSize) throw std::out_of_range("far pointer outside DGROUP");
    return static_cast<uint16_t>(linear);
}

GgExe::Address GgExe::resolveCode(Address a) const {
    const Overlay* ov = overlayByStub(a.seg);
    if (!ov || a.off < kStubHeaderSize || (a.off - kStubHeaderSize) % kStubEntrySize != 0) return a;
    const size_t k = (a.off - kStubHeaderSize) / kStubEntrySize;
    if (k >= ov->entries.size() || ov->seg == 0) return a;
    return {ov->seg, ov->entries[k]};
}

}  // namespace tmport::gg
