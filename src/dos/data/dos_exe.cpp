#include "dos/data/dos_exe.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include "dos/data/dgroup_offsets.h"
#include "util/file_util.h"

namespace tmport::dos {

namespace {

constexpr std::string_view kTurboC = "Turbo-C - Copyright (c) 1988 Borland Intl.";
constexpr uint16_t kDsEgaPalette = dg::kPalEga;  // 16 attribute registers + overscan (fmt_graphics.md)

uint16_t rd16(std::span<const uint8_t> b, size_t at) {
    if (at + 2 > b.size()) throw std::runtime_error("SST.EXE is truncated");
    return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

[[noreturn]] void notV10(const std::string& why) {
    throw std::runtime_error("not the 1990 DOS SST.EXE V 1.0 (" + why + ")");
}

// Microsoft EXEPACK, undone the way its own loader stub does: the packed stream is read backwards
// from just below the stub and expanded backwards from the end of the destination.
std::vector<uint8_t> unpackExepack(std::span<const uint8_t> file) {
    if (file.size() < 0x20 || file[0] != 'M' || file[1] != 'Z') notV10("no MZ header");
    const uint16_t lastPage = rd16(file, 2), pages = rd16(file, 4), hdrParas = rd16(file, 8);
    const uint16_t cs = rd16(file, 0x16);
    const size_t fileLen = (pages ? pages - 1u : 0u) * 512u + (lastPage ? lastPage : 512u);
    const size_t hdrLen = hdrParas * 16u;
    if (fileLen > file.size()) throw std::runtime_error("SST.EXE is truncated");
    if (hdrLen >= fileLen) notV10("bad MZ header");
    const std::span<const uint8_t> image = file.subspan(hdrLen, fileLen - hdrLen);

    const size_t xp = cs * 16u;  // EXEPACK variables + stub
    if (xp + 18 > image.size() || image[xp + 16] != 'R' || image[xp + 17] != 'B') notV10("not EXEPACK");
    const uint16_t destParas = rd16(image, xp + 12), skipParas = rd16(image, xp + 14);
    const std::span<const uint8_t> src = image.first(xp);

    std::vector<uint8_t> dst(destParas * 16u);
    if (dst.size() < src.size() || skipParas == 0 || (skipParas - 1u) * 16u > src.size()) notV10("bad EXEPACK header");
    std::copy(src.begin(), src.end(), dst.begin());

    size_t s = src.size() - (skipParas - 1u) * 16u;
    while (s > 0 && src[s - 1] == 0xFF) --s;  // padding
    size_t d = dst.size();
    for (;;) {
        if (s < 3) notV10("EXEPACK stream ends early");
        const uint8_t cmd = src[--s];
        const size_t len = static_cast<size_t>(src[s - 2] | (src[s - 1] << 8));
        s -= 2;
        if ((cmd & 0xFE) == 0xB0) {  // fill
            if (s < 1 || len > d) notV10("bad EXEPACK fill");
            const uint8_t v = src[--s];
            d -= len;
            std::fill_n(dst.begin() + static_cast<std::ptrdiff_t>(d), len, v);
        } else if ((cmd & 0xFE) == 0xB2) {  // copy
            if (len > s || len > d) notV10("bad EXEPACK copy");
            s -= len;
            d -= len;
            std::copy_n(src.begin() + static_cast<std::ptrdiff_t>(s), len, dst.begin() + static_cast<std::ptrdiff_t>(d));
        } else {
            notV10("bad EXEPACK command");
        }
        if (cmd & 1) break;
    }
    return dst;
}

}  // namespace

DosExe DosExe::load(std::span<const uint8_t> exeFile) {
    DosExe exe;
    exe.image_ = unpackExepack(exeFile);
    if (exe.image_.size() != kImageSize) notV10("unpacked image is " + std::to_string(exe.image_.size()) + " bytes");
    const auto ds = exe.dgroup();
    if (ds.size() < 4 + kTurboC.size() || std::memcmp(ds.data() + 4, kTurboC.data(), kTurboC.size()) != 0)
        notV10("no Turbo C signature at DS:0004");
    for (int i = 0; i < 17; ++i)
        if (exe.ds8(static_cast<uint16_t>(kDsEgaPalette + i)) >= 0x40) notV10("EGA palette check failed");
    return exe;
}

DosExe DosExe::loadFile(const std::filesystem::path& sstExe) {
    const auto bytes = readWholeFile(sstExe);
    if (!bytes) throw std::runtime_error("cannot read " + sstExe.string());
    return load(*bytes);
}

std::span<const uint8_t> DosExe::at(uint16_t seg, uint16_t off) const {
    if (seg < kGhidraBase) return {};
    const size_t pos = (static_cast<size_t>(seg) - kGhidraBase) * 16u + off;
    if (pos >= image_.size()) return {};
    return std::span<const uint8_t>(image_).subspan(pos);
}

uint8_t DosExe::ds8(uint16_t off) const {
    const auto b = at(kDgroupSeg, off);
    if (b.empty()) throw std::out_of_range("DS offset out of range");
    return b[0];
}

uint16_t DosExe::ds16(uint16_t off) const {
    const auto b = at(kDgroupSeg, off);
    if (b.size() < 2) throw std::out_of_range("DS offset out of range");
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}

std::string_view DosExe::dsString(uint16_t off) const {
    const auto b = at(kDgroupSeg, off);
    const auto nul = std::find(b.begin(), b.end(), uint8_t{0});
    return {reinterpret_cast<const char*>(b.data()), static_cast<size_t>(nul - b.begin())};
}

std::span<const uint8_t> DosExe::dsBytes(uint16_t off, size_t n) const {
    const auto b = at(kDgroupSeg, off);
    if (b.size() < n) throw std::out_of_range("DS offset out of range");
    return b.first(n);
}

DosExe::FarPtr DosExe::readFarPtr(uint16_t dsOff) const {
    return {static_cast<uint16_t>(ds16(static_cast<uint16_t>(dsOff + 2)) + kGhidraBase), ds16(dsOff)};
}

std::span<const uint8_t> DosExe::farPtrAtDs(uint16_t off) const {
    const FarPtr p = readFarPtr(off);
    return at(p.seg, p.off);
}

}  // namespace tmport::dos
