#pragma once
// The VGA palette of Gizmos & Gadgets: the 6-bit DAC, the shadow palette DS:024e and its saved copy
// DS:5c22, palette loads and fades (segments 1399 and 1473; re/gg/docs/code_runtime.md §13,
// fmt_graphics.md §3).
//
// The game never writes a colour to the DAC directly: it changes the shadow palette (palette
// resources are byte ranges of it) and loads the whole shadow with Pal_SetRange (1399:05d6), which
// writes colours 0..127 at the start of one vertical retrace and 128..255 at the start of the next:
// one load = 2 retraces of the 70.086 Hz mode 13h refresh. Fades are a series of such loads (Pal_Fade
// 1473:015c): 12 out, 9 in, back to back, the last of each repeating the target, so 22 and 16 video
// frames change, as the DOSBox-X captures show (REFERENCE_CAPTURES.md §10.2).
//
// Waiting for a retrace is the runtime's job (its retrace clock runs on the PIT time base), so loads
// and fades are steppers: PaletteLoad::onRetrace writes one 128-colour group per retrace, PalFade
// hands out the palette of each load. The DAC is shown in 8 bits per channel as (v << 2) | (v >> 4),
// the expansion every G&G capture of DOSBox-X shows (it differs from (v * 255 + 31) / 63 for v = 11..15
// and 49..52; fmt_graphics.md §3.2).

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace tmport::gg {

class GgExe;
struct PaletteRange;

inline constexpr int kPaletteBytes = 768;
using Rgb6 = std::array<uint8_t, kPaletteBytes>;  // 256 x 6-bit R, G, B

// One DAC channel in 8 bits as DOSBox-X shows it.
constexpr uint8_t dacTo8(uint8_t v) {
    v &= 0x3F;
    return static_cast<uint8_t>((v << 2) | (v >> 4));
}

// 256 colours as packed RGBA (R | G << 8 | B << 16 | 0xFF << 24, the convention of
// gfx::Presenter::presentFrame).
std::array<uint32_t, 256> rgba32(const Rgb6& rgb);

// The VGA DAC: write index (port 3C8h) and 768 data registers (3C9h, three per colour, the index
// advancing and wrapping at 256).
class Dac {
public:
    void writeIndex(uint8_t first) { index_ = first * 3; }
    void writeData(uint8_t v);
    void set(const Rgb6& rgb) { regs_ = rgb; }
    const Rgb6& registers() const { return regs_; }
    std::array<uint32_t, 256> rgba() const { return rgba32(regs_); }

private:
    Rgb6 regs_{};
    int index_ = 0;  // byte index 0..767
};

// 1399:05d6 Pal_SetRange(rgb, first, groups): OUT 3C8h = first; then per group: wait for the start
// of the next vertical retrace, write 128 colours (384 bytes) with interrupts off. Every caller
// passes first 0 and 2 groups. The stepper copies the bytes it will write.
class PaletteLoad {
public:
    PaletteLoad() = default;  // nothing to load
    PaletteLoad(std::span<const uint8_t> rgb, int first, int groups);

    bool pending() const { return next_ < groups_; }
    // Retraces still to wait for (one per group).
    int remainingRetraces() const { return groups_ - next_; }
    // At the start of a vertical retrace: the next group into the DAC.
    void onRetrace(Dac& dac);

private:
    std::vector<uint8_t> bytes_;
    int first_ = 0;
    int groups_ = 0;
    int next_ = 0;
};

// 1473:015c Pal_Fade(n, from, to): steps = 63 / n + 1; delta[i] = |to[i] - from[i]| / steps + 1
// (signed bytes, byte result); steps + 1 loads, each moving every byte by its delta towards `to`
// (the direction from `from` vs `to`, never overshooting) and loading the result with
// Pal_SetRange(cur, 0, 2). next() gives the palette of each load in turn.
class PalFade {
public:
    PalFade() = default;  // no loads (the fade functions when there is nothing to do)
    PalFade(int n, const Rgb6& from, const Rgb6& to);

    int loads() const { return loads_; }
    int loadsDone() const { return done_; }
    bool finished() const { return done_ >= loads_; }
    // The palette of the next load; false when every load has been handed out.
    bool next(Rgb6& out);

private:
    Rgb6 from_{}, to_{}, cur_{}, delta_{};
    int loads_ = 0;
    int done_ = 0;
};

// The shadow palette g_palShadow DS:024e (6-bit RGB; its initial contents are ssg.exe's), the saved
// copy g_palSaved DS:5c22 and g_palFaded DS:024c (1 while the DAC is black).
class Palette {
public:
    explicit Palette(std::span<const uint8_t> base = {});  // up to 768 bytes; black after them
    // The base palette DS:024e of the user's ssg.exe.
    static Palette fromExe(const GgExe& exe);

    const Rgb6& shadow() const { return shadow_; }
    Rgb6& shadow() { return shadow_; }
    const Rgb6& saved() const { return saved_; }
    bool faded() const { return faded_; }
    void setFaded(bool f) { faded_ = f; }

    // 1473:02f0 Pal_SetShadowRange(rgb, byteOffset, count): count bytes into the shadow at byteOffset;
    // returns the load the original then makes when not faded (Pal_SetRange(shadow, 0, 2)), else an
    // empty one. A range past the 768 bytes is cut (the original writes on into DGROUP).
    PaletteLoad setShadowRange(std::span<const uint8_t> rgb, int byteOffset, int count);
    // The same for a palette resource (gg/data/resources.h parsePaletteRange): the bundle and
    // Screen_LoadPicture path.
    PaletteLoad apply(const PaletteRange& range);
    // 1473:041c Pal_LoadRes: the range into the shadow only (no DAC load).
    void store(const PaletteRange& range);
    // 1473:074e Pal_Save / 1473:0766 Pal_Restore (no DAC load).
    void save() { saved_ = shadow_; }
    void restore() { shadow_ = saved_; }
    // The load of the whole shadow (Pal_SetRange(DS:024e, 0, 2)).
    PaletteLoad loadShadow() const { return PaletteLoad(shadow_, 0, 2); }

    // 1473:000c Pal_Blackout: Pal_SetRange of 768 zero bytes; faded = 1.
    PaletteLoad blackout();
    // 1473:0265 Pal_FadeOut: if not faded: Pal_Fade(6, shadow, black) (12 loads) and faded = 1;
    // else nothing. The flag is set at once (nothing reads it during the fade).
    PalFade fadeOut();
    // 1473:02a0 Pal_FadeIn: if faded: Pal_Fade(8, black, shadow) (9 loads) and faded = 0.
    PalFade fadeIn();
    // 1473:02db Pal_FadeFromSaved: Pal_Fade(8, saved, shadow); the flag unchanged.
    PalFade fadeFromSaved() const { return PalFade(8, saved_, shadow_); }

private:
    Rgb6 shadow_{};
    Rgb6 saved_{};
    bool faded_ = false;
};

// For tools and tests: runs `fade`'s loads back to back on `dac` (each load starts at the next
// retrace, as Pal_SetRange's wait does) and returns the DAC after every retrace, i.e. what each video
// frame of the fade shows (2 per load).
std::vector<Rgb6> fadeRetraceFrames(Dac dac, PalFade fade);

}  // namespace tmport::gg
