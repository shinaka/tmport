#include "gg/app/test_pattern.h"

#include <algorithm>
#include <vector>

#include "gg/app/gg_app.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/input/input_codes.h"

namespace tmport::gg {

namespace {

constexpr int kW = 320, kH = 200;

constexpr uint32_t rgb(uint32_t r, uint32_t g, uint32_t b) { return r | (g << 8) | (b << 16) | 0xFF000000u; }

void fill(std::span<uint32_t> px, int x, int y, int w, int h, uint32_t c) {
    for (int yy = std::max(y, 0); yy < std::min(y + h, kH); ++yy)
        for (int xx = std::max(x, 0); xx < std::min(x + w, kW); ++xx) px[static_cast<size_t>(yy) * kW + xx] = c;
}

}  // namespace

std::array<uint32_t, 256> rgbaPalette(std::span<const uint8_t> dac768) {
    std::array<uint32_t, 256> p{};
    for (size_t i = 0; i < 256 && 3 * i + 2 < dac768.size(); ++i) {
        auto e = [&](size_t k) { return static_cast<uint32_t>((dac768[3 * i + k] & 0x3F) * 255 + 31) / 63; };
        p[i] = rgb(e(0), e(1), e(2));
    }
    return p;
}

void drawTestPattern(std::span<uint32_t> px, const std::array<uint32_t, 256>& palette, const TestPatternView& v) {
    if (px.size() < static_cast<size_t>(kW) * kH) return;
    // The palette grid.
    for (int i = 0; i < 256; ++i) fill(px, (i % 16) * 20, (i / 16) * 10, 20, 10, palette[static_cast<size_t>(i)]);
    // The status strip.
    const uint32_t black = rgb(0, 0, 0), dark = rgb(48, 48, 48), on = rgb(0, 220, 0), white = rgb(255, 255, 255);
    fill(px, 0, 160, kW, 40, black);
    fill(px, static_cast<int>((v.ticks / 73) % 32) * 10, 162, 10, 8, white);
    for (int b = 0; b < 6; ++b) fill(px, 2 + b * 22, 174, 20, 10, (v.heldKeys >> b) & 1 ? on : dark);
    fill(px, 2 + 6 * 22 + 8, 174, 20, 10, v.soundOn ? rgb(0, 120, 255) : dark);
    for (int b = 0; b < 16; ++b) fill(px, 2 + b * 12, 188, 10, 8, (v.lastCode >> (15 - b)) & 1 ? rgb(255, 160, 0) : dark);
    if (v.flash) {
        const uint32_t y = rgb(255, 255, 0);
        fill(px, 0, 0, kW, 2, y);
        fill(px, 0, kH - 2, kW, 2, y);
        fill(px, 0, 0, 2, kH, y);
        fill(px, kW - 2, 0, 2, kH, y);
    }
    // The pointer: a crosshair at the cursor's top-left (the hot spot of the arrow is (0, 0)).
    fill(px, v.mouseX - 4, v.mouseY, 9, 1, white);
    fill(px, v.mouseX, v.mouseY - 4, 1, 9, white);
    fill(px, v.mouseX, v.mouseY, 1, 1, black);
}

int testPatternMain(GgContext& ctx) {
    GgRuntime& rt = ctx.runtime;
    const auto palette = rgbaPalette(ctx.files.exe().dsBytes(dg::kPalShadow, dg::kPalShadowCount));
    std::vector<uint32_t> rgba(static_cast<size_t>(kW) * kH);

    // Mouse_Init 17ab:000e reads the driver's position.
    rt.mouse().readDriverPosition();
    Zone screen = Zone::rect(0, 0, kW, kH, 5, 0, code::kEnter);
    rt.zones().addLocal(screen);
    // The zone and the hook go when the loop ends, also through GgExit / GgRunEnd.
    struct Guard {
        GgRuntime& rt;
        Zone& z;
        ~Guard() {
            rt.zones().removeLocal(&z);
            rt.input().hooks().toggleSound = {};
        }
    } guard{rt, screen};

    TestPatternView v;
    rt.input().hooks().toggleSound = [&v] { v.soundOn = !v.soundOn; };
    uint32_t flashUntil = 0;
    bool flashing = false;
    while (true) {
        rt.timers().set(GgTimers::kFrameSlot, 4);
        v.ticks = rt.ticks();
        v.mouseX = rt.mouse().x();
        v.mouseY = rt.mouse().y();
        v.heldKeys = rt.keyboard().heldKeys();
        v.flash = flashing && rt.ticks() < flashUntil;
        drawTestPattern(rgba, palette, v);
        rt.showFrame(rgba);
        rt.timerWait(GgTimers::kFrameSlot);
        const uint16_t c = rt.getInputCode();
        if (c != 0) v.lastCode = c;
        if (c == code::kEsc) break;
        if (c == code::kEnter) {
            flashing = true;
            flashUntil = rt.ticks() + 73;
        }
    }
    return 0;
}

}  // namespace tmport::gg
