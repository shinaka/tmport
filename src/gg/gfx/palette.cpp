#include "gg/gfx/palette.h"

#include <algorithm>
#include <cstdlib>

#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/data/resources.h"
#include "util/log.h"

namespace tmport::gg {

std::array<uint32_t, 256> rgba32(const Rgb6& rgb) {
    std::array<uint32_t, 256> out{};
    for (size_t i = 0; i < 256; ++i)
        out[i] = uint32_t{dacTo8(rgb[3 * i])} | uint32_t{dacTo8(rgb[3 * i + 1])} << 8 |
                 uint32_t{dacTo8(rgb[3 * i + 2])} << 16 | 0xFF000000u;
    return out;
}

void Dac::writeData(uint8_t v) {
    regs_[static_cast<size_t>(index_)] = static_cast<uint8_t>(v & 0x3F);
    index_ = (index_ + 1) % kPaletteBytes;
}

// ------------------------------------------------------------------------------- Pal_SetRange

PaletteLoad::PaletteLoad(std::span<const uint8_t> rgb, int first, int groups)
    : first_(first & 0xFF), groups_(std::max(groups, 0)) {
    const size_t n = static_cast<size_t>(groups_) * 384;
    bytes_.assign(n, 0);
    std::copy_n(rgb.begin(), std::min(n, rgb.size()), bytes_.begin());
}

// 1399:05d6 Pal_SetRange: one group per retrace
void PaletteLoad::onRetrace(Dac& dac) {
    if (!pending()) return;
    if (next_ == 0) dac.writeIndex(static_cast<uint8_t>(first_));  // OUT 3C8h, before the first wait
    const size_t base = static_cast<size_t>(next_) * 384;
    for (size_t i = 0; i < 384; ++i) dac.writeData(bytes_[base + i]);
    ++next_;
}

// ------------------------------------------------------------------------------------ Pal_Fade

// 1473:015c Pal_Fade
PalFade::PalFade(int n, const Rgb6& from, const Rgb6& to) : from_(from), to_(to), cur_(from) {
    const int steps = 63 / n + 1;  // MOV AX,3Fh; CWD; IDIV n; INC AX
    for (size_t i = 0; i < kPaletteBytes; ++i) {
        const int d = std::abs(static_cast<int8_t>(to[i]) - static_cast<int8_t>(from[i]));  // CBW both
        delta_[i] = static_cast<uint8_t>(d / steps + 1);  // IDIV, INC AL
    }
    loads_ = steps + 1;
}

bool PalFade::next(Rgb6& out) {
    if (finished()) return false;
    for (size_t i = 0; i < kPaletteBytes; ++i) {
        if (static_cast<int8_t>(from_[i]) < static_cast<int8_t>(to_[i])) {
            cur_[i] = static_cast<uint8_t>(cur_[i] + delta_[i]);
            if (static_cast<int8_t>(to_[i]) < static_cast<int8_t>(cur_[i])) cur_[i] = to_[i];
        } else {
            cur_[i] = static_cast<uint8_t>(cur_[i] - delta_[i]);
            if (static_cast<int8_t>(to_[i]) > static_cast<int8_t>(cur_[i])) cur_[i] = to_[i];
        }
    }
    ++done_;
    out = cur_;
    return true;
}

// -------------------------------------------------------------------------------- the shadow

Palette::Palette(std::span<const uint8_t> base) {
    std::copy_n(base.begin(), std::min(base.size(), shadow_.size()), shadow_.begin());
}

Palette Palette::fromExe(const GgExe& exe) { return Palette(exe.dsBytes(dg::kPalShadow, dg::kPalShadowCount)); }

// 1473:02f0 Pal_SetShadowRange
PaletteLoad Palette::setShadowRange(std::span<const uint8_t> rgb, int byteOffset, int count) {
    if (byteOffset < 0 || count < 0 || byteOffset + count > kPaletteBytes) {
        log::debug("Pal_SetShadowRange({}, {}) past the shadow palette: cut", byteOffset, count);
        byteOffset = std::clamp(byteOffset, 0, kPaletteBytes);
        count = std::clamp(count, 0, kPaletteBytes - byteOffset);
    }
    std::copy_n(rgb.begin(), std::min(rgb.size(), static_cast<size_t>(count)),
                shadow_.begin() + byteOffset);
    return faded_ ? PaletteLoad() : loadShadow();
}

PaletteLoad Palette::apply(const PaletteRange& range) { return setShadowRange(range.rgb, range.byteOffset, range.byteCount); }

// 1473:041c Pal_LoadRes
void Palette::store(const PaletteRange& range) {
    const int off = std::min<int>(range.byteOffset, kPaletteBytes);
    const size_t n = std::min({range.rgb.size(), size_t{range.byteCount}, static_cast<size_t>(kPaletteBytes - off)});
    std::copy_n(range.rgb.begin(), n, shadow_.begin() + off);
}

// 1473:000c Pal_Blackout
PaletteLoad Palette::blackout() {
    faded_ = true;
    const Rgb6 black{};
    return PaletteLoad(black, 0, 2);
}

// 1473:0265 Pal_FadeOut
PalFade Palette::fadeOut() {
    if (faded_) return {};
    faded_ = true;
    return PalFade(6, shadow_, Rgb6{});
}

// 1473:02a0 Pal_FadeIn
PalFade Palette::fadeIn() {
    if (!faded_) return {};
    faded_ = false;
    return PalFade(8, Rgb6{}, shadow_);
}

std::vector<Rgb6> fadeRetraceFrames(Dac dac, PalFade fade) {
    std::vector<Rgb6> frames;
    Rgb6 cur;
    while (fade.next(cur)) {
        PaletteLoad load(cur, 0, 2);
        while (load.pending()) {
            load.onRetrace(dac);
            frames.push_back(dac.registers());
        }
    }
    return frames;
}

}  // namespace tmport::gg
