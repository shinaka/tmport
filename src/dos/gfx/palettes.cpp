#include "dos/gfx/palettes.h"

#include <stdexcept>

#include "dos/data/dgroup_offsets.h"
#include "dos/data/dos_exe.h"
#include "dos/gfx/back_buffer.h"
#include "mischief/util/file_util.h"

namespace tmport::dos {

namespace {

constexpr uint16_t kDsEga = dg::kPalEga;    // attribute registers, INT 10h AX=1002h at 1000:0852
constexpr uint16_t kDsTandy = dg::kPalTandy;  // palette registers 10h..1Fh, OUT 3DAh/3DEh at 1000:097d
constexpr uint16_t kDsMcga = dg::kPalMcga;   // DAC values, 1460:0003 -> 1437:0001 -> 1000:1144
constexpr uint16_t kDsCga = dg::kPalCga;    // dither bytes, 1437:0027 -> 1000:1087

}  // namespace

std::optional<Look> parseLook(std::string_view name) {
    const std::string n = mischief::toLowerAscii(name);
    if (n == "ega") return Look::Ega;
    if (n == "tandy") return Look::Tandy;
    if (n == "mcga") return Look::Mcga;
    if (n == "cga") return Look::Cga;
    return std::nullopt;
}

std::string_view lookName(Look look) {
    switch (look) {
        case Look::Ega: return "ega";
        case Look::Tandy: return "tandy";
        case Look::Mcga: return "mcga";
        case Look::Cga: return "cga";
    }
    return "ega";
}

const std::array<mischief::Rgb8, 16>& rgbiColours() {
    static const std::array<mischief::Rgb8, 16> k = {{{0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
                                            {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
                                            {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
                                            {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF}}};
    return k;
}

const std::array<mischief::Rgb8, 4>& cgaColours() {
    static const std::array<mischief::Rgb8, 4> k = {{{0x00, 0x00, 0x00}, {0x55, 0xFF, 0xFF}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0xFF}}};
    return k;
}

int cgaPixel(const std::array<uint8_t, 16>& dither, uint8_t c, int x, int y) {
    // Even rows use the tables at 2005:7E00/7F00 (pixels a b c d), odd rows 2005:8000/8100 (b a d c).
    static constexpr int kOddRow[4] = {1, 0, 3, 2};
    int p = x & 3;
    if (y & 1) p = kOddRow[p];
    return (dither[c & 15] >> (6 - 2 * p)) & 3;
}

CgaBinding::CgaBinding(BackBuffer& back, BackBuffer& screen, const LookPalette& pal) {
    if (pal.look != Look::Cga) return;
    back.enableCga(pal.cgaDither);
    screen.enableCga(pal.cgaDither);
}

mischief::Rgb8 LookPalette::at(uint8_t c, int x, int y) const {
    if (look == Look::Cga) return cgaColours()[static_cast<size_t>(cgaPixel(cgaDither, c, x, y))];
    return colours[c & 15];
}

LookPalette lookPalette(const DosExe& exe, Look look) {
    LookPalette p;
    p.look = look;
    for (int i = 0; i < 16; ++i) p.cgaDither[static_cast<size_t>(i)] = exe.ds8(static_cast<uint16_t>(kDsCga + i));
    const auto& rgbi = rgbiColours();
    for (int i = 0; i < 16; ++i) {
        mischief::Rgb8& c = p.colours[static_cast<size_t>(i)];
        switch (look) {
            case Look::Ega:
            case Look::Cga: {  // CGA: the EGA colours describe the logical colours (not shown)
                const uint8_t v = exe.ds8(static_cast<uint16_t>(kDsEga + i));
                c = rgbi[static_cast<size_t>((v & 7) | ((v >> 1) & 8))];
                break;
            }
            case Look::Tandy: c = rgbi[exe.ds8(static_cast<uint16_t>(kDsTandy + i)) & 15]; break;
            case Look::Mcga: {
                auto expand = [](uint8_t v) { return static_cast<uint8_t>(((v & 0x3F) * 255 + 31) / 63); };
                const uint16_t at = static_cast<uint16_t>(kDsMcga + 3 * i);
                c = {expand(exe.ds8(at)), expand(exe.ds8(static_cast<uint16_t>(at + 1))), expand(exe.ds8(static_cast<uint16_t>(at + 2)))};
                break;
            }
        }
    }
    return p;
}

void toRgba(const BackBuffer& buf, const LookPalette& pal, std::span<uint8_t> out) {
    if (out.size() < static_cast<size_t>(BackBuffer::kWidth) * BackBuffer::kHeight * 4)
        throw std::invalid_argument("toRgba: output buffer too small");
    size_t o = 0;
    const bool plane = pal.look == Look::Cga && buf.cgaEnabled();
    for (int y = 0; y < BackBuffer::kHeight; ++y)
        for (int x = 0; x < BackBuffer::kWidth; ++x) {
            const mischief::Rgb8 c = plane ? cgaColours()[buf.cgaPixel(x, y)] : pal.at(buf.pixel(x, y), x, y);
            out[o++] = c.r;
            out[o++] = c.g;
            out[o++] = c.b;
            out[o++] = 0xFF;
        }
}

void toRgba32(const BackBuffer& buf, const LookPalette& pal, std::span<uint32_t> out) {
    if (out.size() < static_cast<size_t>(BackBuffer::kWidth) * BackBuffer::kHeight)
        throw std::invalid_argument("toRgba32: output buffer too small");
    auto pack = [](mischief::Rgb8 c) { return static_cast<uint32_t>(c.r) | (static_cast<uint32_t>(c.g) << 8) | (static_cast<uint32_t>(c.b) << 16) | 0xFF000000u; };
    std::array<uint32_t, 16> packed{};
    for (size_t i = 0; i < 16; ++i) packed[i] = pack(pal.colours[i]);
    const bool plane = pal.look == Look::Cga && buf.cgaEnabled();
    size_t o = 0;
    for (int y = 0; y < BackBuffer::kHeight; ++y)
        for (int x = 0; x < BackBuffer::kWidth; ++x, ++o) {
            const uint8_t c = buf.pixel(x, y);
            out[o] = plane ? pack(cgaColours()[buf.cgaPixel(x, y)]) : pal.look == Look::Cga ? pack(pal.at(c, x, y)) : packed[c];
        }
}

std::vector<uint32_t> toRgba32(const BackBuffer& buf, const LookPalette& pal) {
    std::vector<uint32_t> out(static_cast<size_t>(BackBuffer::kWidth) * BackBuffer::kHeight);
    toRgba32(buf, pal, out);
    return out;
}

std::vector<uint8_t> toRgba(const BackBuffer& buf, const LookPalette& pal) {
    std::vector<uint8_t> out(static_cast<size_t>(BackBuffer::kWidth) * BackBuffer::kHeight * 4);
    toRgba(buf, pal, out);
    return out;
}

}  // namespace tmport::dos
