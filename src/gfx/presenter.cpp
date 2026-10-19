#include "gfx/presenter.h"

#include <SDL3/SDL_render.h>

#include <algorithm>
#include <bit>
#include <cstring>

#include "util/log.h"
#include "util/png_writer.h"

namespace tmport::gfx {

namespace {
constexpr uint32_t kOpaqueBlack = 0xFF000000u;
static_assert(std::endian::native == std::endian::little, "canvas packing assumes a little-endian host");

// a * b / c rounded to nearest, in 64 bits (sizes are small; the products are not).
int mulDivRound(int64_t a, int64_t b, int64_t c) { return static_cast<int>((a * b + c / 2) / c); }
}  // namespace

// --------------------------------------------------------------------------------- layouts

PresenterLayout PresenterLayout::windowsCd() { return PresenterLayout{}; }

PresenterLayout PresenterLayout::dos1990(bool squarePixels) {
    PresenterLayout l;
    l.canvasWidth = 320;
    l.canvasHeight = 200;
    l.gameRect = Rect{0, 0, 320, 200};
    // A 320x200 mode filled a 4:3 monitor: each pixel 1.2 times as tall as wide (SCOPING §1).
    l.aspectNum = squarePixels ? 1 : 6;
    l.aspectDen = squarePixels ? 1 : 5;
    l.overlay = false;
    l.defaultWindowScale = 3;  // 960x720 (960x600 with square pixels)
    return l;
}

IntScale integerScaleFor(const PresenterLayout& l, int outW, int outH) {
    const int cw = l.canvasWidth, ch = l.canvasHeight;
    if (cw <= 0 || ch <= 0 || outW < cw || outH < ch) return {};
    // Exact pairs keep the pixel aspect: (den*k, num*k).
    const int k = std::min(outW / (cw * l.aspectDen), outH / (ch * l.aspectNum));
    if (k >= 1) return {l.aspectDen * k, l.aspectNum * k};
    // None fits: the largest horizontal factor whose rounded vertical factor fits too.
    for (int sx = outW / cw; sx >= 1; --sx) {
        const int sy = std::max(1, mulDivRound(sx, l.aspectNum, l.aspectDen));
        if (ch * sy <= outH) return {sx, sy};
    }
    return {};
}

Rect presentRect(const PresenterLayout& l, int outW, int outH, ScaleMode mode) {
    if (outW <= 0 || outH <= 0 || l.canvasWidth <= 0 || l.canvasHeight <= 0) return {};
    if (mode == ScaleMode::Integer) {
        if (const IntScale s = integerScaleFor(l, outW, outH); s.sx > 0) {
            const int w = l.canvasWidth * s.sx, h = l.canvasHeight * s.sy;
            return {(outW - w) / 2, (outH - h) / 2, w, h};
        }
    }
    // Display aspect as exact integers: DOS 320*5 : 200*6 = 1600 : 1200.
    const int64_t dw = static_cast<int64_t>(l.canvasWidth) * l.aspectDen;
    const int64_t dh = static_cast<int64_t>(l.canvasHeight) * l.aspectNum;
    int w = outW, h = outH;
    if (static_cast<int64_t>(outW) * dh >= static_cast<int64_t>(outH) * dw)
        w = mulDivRound(outH, dw, dh);  // the output is wider: bars left and right
    else
        h = mulDivRound(outW, dh, dw);  // taller: bars above and below
    return {(outW - w) / 2, (outH - h) / 2, w, h};
}

WindowSize initialWindowSize(const PresenterLayout& l, int windowScale, bool integerScale, int desktopW, int desktopH) {
    int scale = windowScale <= 0 ? std::max(1, l.defaultWindowScale) : windowScale;
    const bool limit = desktopW > 0 && desktopH > 0;
    const int maxW = desktopW * 9 / 10, maxH = desktopH * 9 / 10;
    if (integerScale && !l.squarePixels()) {
        const int k = (scale + l.aspectDen - 1) / l.aspectDen;
        WindowSize want{l.canvasWidth * l.aspectDen * k, l.canvasHeight * l.aspectNum * k};
        if (!limit || (want.w <= maxW && want.h <= maxH)) return want;
        const IntScale s = integerScaleFor(l, maxW, maxH);
        if (s.sx > 0) return {l.canvasWidth * s.sx, l.canvasHeight * s.sy};
        return {l.canvasWidth, l.canvasHeight};
    }
    const int dw = l.displayWidth(), dh = l.displayHeight();
    if (limit)
        while (scale > 1 && (dw * scale > maxW || dh * scale > maxH)) --scale;
    return {dw * scale, dh * scale};
}

// ------------------------------------------------------------------------------- Presenter

Presenter::Presenter(const PresenterLayout& layout)
    : layout_(layout),
      cw_(layout.canvasWidth),
      ch_(layout.canvasHeight),
      gx_(layout.gameRect.x),
      gy_(layout.gameRect.y),
      gw_(layout.gameRect.w),
      gh_(layout.gameRect.h),
      window_(static_cast<size_t>(gw_) * gh_, 0),
      canvas_(static_cast<size_t>(cw_) * ch_, kOpaqueBlack) {
    renderRect(0, 0, gw_, gh_);
}

Presenter::~Presenter() { detach(); }

void Presenter::setPalette(const Palette& p) {
    palette_ = p;
    renderRect(0, 0, gw_, gh_);
}

void Presenter::renderRect(int x, int y, int w, int h) {
    if (!overlay_.empty()) {
        composeCanvasRect(gx_ + x, gy_ + y, w, h);
        return;
    }
    const auto& lut = palette_.rgba();
    for (int r = 0; r < h; ++r) {
        uint32_t* dst = canvas_.data() + static_cast<size_t>(gy_ + y + r) * cw_ + gx_ + x;
        if (frame_.empty()) {
            const uint8_t* src = window_.data() + static_cast<size_t>(y + r) * gw_ + x;
            for (int c = 0; c < w; ++c) dst[c] = lut[src[c]];
        } else {
            std::memcpy(dst, frame_.data() + static_cast<size_t>(y + r) * gw_ + x, static_cast<size_t>(w) * 4);
        }
    }
    if (!marks_.empty()) paintDebugMarks(gx_ + x, gy_ + y, w, h);
    changed_ = true;
}

void Presenter::paintDebugMarks(int x, int y, int w, int h) {
    const int x1 = x + w, y1 = y + h;
    constexpr int t = kDebugMarkThickness;
    for (const DebugMark& m : marks_) {
        const int mx = gx_ + m.x, my = gy_ + m.y;
        const int r0 = std::max({y, my, gy_}), r1 = std::min({y1, my + m.h, gy_ + gh_});
        for (int r = r0; r < r1; ++r) {
            const bool band = r < my + t || r >= my + m.h - t;  // top or bottom edge: the whole row
            uint32_t* dst = canvas_.data() + static_cast<size_t>(r) * cw_;
            const uint32_t* ov = overlay_.empty() ? nullptr : overlay_.data() + static_cast<size_t>(r) * cw_;
            auto fill = [&](int c0, int c1) {
                c0 = std::max({c0, x, gx_});
                c1 = std::min({c1, x1, gx_ + gw_});
                for (int c = c0; c < c1; ++c)
                    if (!ov || (ov[c] & 0xFF000000u) == 0) dst[c] = m.rgba;
            };
            if (band) {
                fill(mx, mx + m.w);
            } else {
                fill(mx, mx + t);
                fill(mx + m.w - t, mx + m.w);
            }
        }
    }
}

void Presenter::setDebugMarks(std::vector<DebugMark> marks) {
    if (marks == marks_) return;
    std::vector<DebugMark> old = std::move(marks_);
    marks_ = std::move(marks);
    // Recompose where the old marks were (now without them), then paint the new ones.
    for (const DebugMark& m : old) composeCanvasRect(gx_ + m.x, gy_ + m.y, m.w, m.h);
    for (const DebugMark& m : marks_) composeCanvasRect(gx_ + m.x, gy_ + m.y, m.w, m.h);
}

void Presenter::composeCanvasRect(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > cw_) w = cw_ - x;
    if (y + h > ch_) h = ch_ - y;
    if (w <= 0 || h <= 0) return;
    const uint32_t* lut = palette_.rgba().data();
    const bool overlay = !overlay_.empty();
    for (int r = y; r < y + h; ++r) {
        uint32_t* dst = canvas_.data() + static_cast<size_t>(r) * cw_;
        const uint32_t* ov = overlay ? overlay_.data() + static_cast<size_t>(r) * cw_ : nullptr;
        const bool gameRow = r >= gy_ && r < gy_ + gh_;
        const size_t rowOff = gameRow ? static_cast<size_t>(r - gy_) * gw_ : 0;
        const uint8_t* src = gameRow && frame_.empty() ? window_.data() + rowOff : nullptr;
        const uint32_t* rgba = gameRow && !frame_.empty() ? frame_.data() + rowOff : nullptr;
        for (int c = x; c < x + w; ++c) {
            if (ov && (ov[c] & 0xFF000000u) != 0) {
                dst[c] = ov[c];
            } else if (gameRow && c >= gx_ && c < gx_ + gw_) {
                dst[c] = src ? lut[src[c - gx_]] : rgba[c - gx_];
            } else {
                dst[c] = kOpaqueBlack;
            }
        }
    }
    if (!marks_.empty()) paintDebugMarks(x, y, w, h);
    changed_ = true;
}

std::span<uint32_t> Presenter::overlayPixels() {
    if (!layout_.overlay) return {};
    if (overlay_.empty()) overlay_.assign(static_cast<size_t>(cw_) * ch_, 0u);
    return overlay_;
}

void Presenter::overlayChanged(int x, int y, int w, int h) {
    if (!layout_.overlay) return;
    composeCanvasRect(x, y, w, h);
}

void Presenter::clearOverlay() {
    if (overlay_.empty()) return;
    overlay_.clear();
    composeCanvasRect(0, 0, cw_, ch_);
}

void Presenter::blitToWindow(const FrameBuffer& fb, int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > gw_) w = gw_ - x;
    if (y + h > gh_) h = gh_ - y;
    if (w <= 0 || h <= 0) return;
    for (int r = 0; r < h; ++r)
        std::memcpy(window_.data() + static_cast<size_t>(y + r) * gw_ + x, fb.row(y + r) + x, static_cast<size_t>(w));
    if (!frame_.empty()) {
        // Back from an RGBA frame to the window picture: the whole game area shows indices again.
        frame_.clear();
        renderRect(0, 0, gw_, gh_);
        return;
    }
    renderRect(x, y, w, h);
}

void Presenter::flushDirtyRects(FrameBuffer& fb) {
    for (const Rect& r : fb.dirtyRects()) blitToWindow(fb, r.x, r.y, r.w, r.h);
    fb.clearDirtyRects();
}

void Presenter::presentFrame(std::span<const uint32_t> rgba) {
    if (rgba.size() != static_cast<size_t>(gw_) * gh_) {
        log::warn("presenter: frame of {} pixels ignored (the game area is {}x{})", rgba.size(), gw_, gh_);
        return;
    }
    frame_.assign(rgba.begin(), rgba.end());
    renderRect(0, 0, gw_, gh_);
}

std::span<const uint8_t> Presenter::canvas() const {
    return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(canvas_.data()), canvas_.size() * 4);
}

std::vector<uint8_t> Presenter::gameAreaRgba() const {
    std::vector<uint8_t> out(static_cast<size_t>(gw_) * gh_ * 4);
    for (int y = 0; y < gh_; ++y)
        std::memcpy(out.data() + static_cast<size_t>(y) * gw_ * 4, canvas_.data() + static_cast<size_t>(gy_ + y) * cw_ + gx_,
                    static_cast<size_t>(gw_) * 4);
    return out;
}

bool Presenter::writeCanvasPng(const std::filesystem::path& path) const { return png::writeRgba(path, cw_, ch_, canvas()); }

bool Presenter::writeGameAreaPng(const std::filesystem::path& path) const {
    const auto rgba = gameAreaRgba();
    return png::writeRgba(path, gw_, gh_, rgba);
}

bool Presenter::attach(SDL_Renderer* renderer, bool integerScale) {
    detach();
    renderer_ = renderer;
    texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, cw_, ch_);
    if (!texture_) {
        log::error("SDL_CreateTexture failed: {}", SDL_GetError());
        renderer_ = nullptr;
        return false;
    }
    // Non-square pixels are scaled by fractional factors in letterbox mode: pixel-art sampling keeps
    // them sharp without uneven rows.
    SDL_SetTextureScaleMode(texture_, layout_.squarePixels() ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_PIXELART);
    setIntegerScale(integerScale);
    changed_ = true;
    return true;
}

void Presenter::detach() {
    if (texture_) SDL_DestroyTexture(texture_);
    texture_ = nullptr;
    renderer_ = nullptr;
}

void Presenter::setIntegerScale(bool on) {
    integerScale_ = on;
    if (renderer_) {
        if (layout_.squarePixels())
            SDL_SetRenderLogicalPresentation(renderer_, cw_, ch_,
                                             on ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE : SDL_LOGICAL_PRESENTATION_LETTERBOX);
        else
            SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    }
    changed_ = true;
}

void Presenter::render(bool force) {
    if (!renderer_ || !texture_) return;
    if (!changed_ && !force) return;
    SDL_UpdateTexture(texture_, nullptr, canvas_.data(), cw_ * 4);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    if (layout_.squarePixels()) {
        SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    } else {
        int ow = 0, oh = 0;
        SDL_GetCurrentRenderOutputSize(renderer_, &ow, &oh);
        const Rect r = presentRect(layout_, ow, oh, integerScale_ ? ScaleMode::Integer : ScaleMode::Letterbox);
        const SDL_FRect dst{static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.w), static_cast<float>(r.h)};
        SDL_RenderTexture(renderer_, texture_, nullptr, &dst);
    }
    SDL_RenderPresent(renderer_);
    changed_ = false;
}

bool Presenter::windowToCanvas(float wx, float wy, float& cx, float& cy) const {
    if (!renderer_) return false;
    float rx = 0.0f, ry = 0.0f;
    if (!SDL_RenderCoordinatesFromWindow(renderer_, wx, wy, &rx, &ry)) return false;
    if (layout_.squarePixels()) {
        // The logical presentation already mapped the point to the canvas.
        cx = rx;
        cy = ry;
        return true;
    }
    int ow = 0, oh = 0;
    if (!SDL_GetCurrentRenderOutputSize(renderer_, &ow, &oh)) return false;
    const Rect r = presentRect(layout_, ow, oh, integerScale_ ? ScaleMode::Integer : ScaleMode::Letterbox);
    if (r.w <= 0 || r.h <= 0) return false;
    cx = (rx - static_cast<float>(r.x)) * static_cast<float>(cw_) / static_cast<float>(r.w);
    cy = (ry - static_cast<float>(r.y)) * static_cast<float>(ch_) / static_cast<float>(r.h);
    return true;
}

}  // namespace tmport::gfx
