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
}  // namespace

Presenter::Presenter()
    : window_(static_cast<size_t>(kGameWidth) * kGameHeight, 0),
      canvas_(static_cast<size_t>(kCanvasWidth) * kCanvasHeight, kOpaqueBlack) {
    renderRect(0, 0, kGameWidth, kGameHeight);
}

Presenter::~Presenter() { detach(); }

void Presenter::setPalette(const Palette& p) {
    palette_ = p;
    renderRect(0, 0, kGameWidth, kGameHeight);
}

void Presenter::renderRect(int x, int y, int w, int h) {
    if (!overlay_.empty()) {
        composeCanvasRect(kGameX + x, kGameY + y, w, h);
        return;
    }
    const auto& lut = palette_.rgba();
    for (int r = 0; r < h; ++r) {
        const uint8_t* src = window_.data() + static_cast<size_t>(y + r) * kGameWidth + x;
        uint32_t* dst = canvas_.data() + static_cast<size_t>(kGameY + y + r) * kCanvasWidth + kGameX + x;
        for (int c = 0; c < w; ++c) dst[c] = lut[src[c]];
    }
    changed_ = true;
}

void Presenter::composeCanvasRect(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > kCanvasWidth) w = kCanvasWidth - x;
    if (y + h > kCanvasHeight) h = kCanvasHeight - y;
    if (w <= 0 || h <= 0) return;
    const auto& lut = palette_.rgba();
    const bool overlay = !overlay_.empty();
    for (int r = y; r < y + h; ++r) {
        uint32_t* dst = canvas_.data() + static_cast<size_t>(r) * kCanvasWidth;
        const uint32_t* ov = overlay ? overlay_.data() + static_cast<size_t>(r) * kCanvasWidth : nullptr;
        const bool gameRow = r >= kGameY && r < kGameY + kGameHeight;
        const uint8_t* src = gameRow ? window_.data() + static_cast<size_t>(r - kGameY) * kGameWidth : nullptr;
        for (int c = x; c < x + w; ++c) {
            if (ov && (ov[c] & 0xFF000000u) != 0) {
                dst[c] = ov[c];
            } else if (src && c >= kGameX && c < kGameX + kGameWidth) {
                dst[c] = lut[src[c - kGameX]];
            } else {
                dst[c] = kOpaqueBlack;
            }
        }
    }
    changed_ = true;
}

std::span<uint32_t> Presenter::overlayPixels() {
    if (overlay_.empty()) overlay_.assign(static_cast<size_t>(kCanvasWidth) * kCanvasHeight, 0u);
    return overlay_;
}

void Presenter::overlayChanged(int x, int y, int w, int h) { composeCanvasRect(x, y, w, h); }

void Presenter::clearOverlay() {
    if (overlay_.empty()) return;
    overlay_.clear();
    composeCanvasRect(0, 0, kCanvasWidth, kCanvasHeight);
}

void Presenter::blitToWindow(const FrameBuffer& fb, int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > kGameWidth) w = kGameWidth - x;
    if (y + h > kGameHeight) h = kGameHeight - y;
    if (w <= 0 || h <= 0) return;
    for (int r = 0; r < h; ++r)
        std::memcpy(window_.data() + static_cast<size_t>(y + r) * kGameWidth + x, fb.row(y + r) + x, static_cast<size_t>(w));
    renderRect(x, y, w, h);
}

void Presenter::flushDirtyRects(FrameBuffer& fb) {
    for (const Rect& r : fb.dirtyRects()) blitToWindow(fb, r.x, r.y, r.w, r.h);
    fb.clearDirtyRects();
}

std::span<const uint8_t> Presenter::canvas() const {
    return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(canvas_.data()), canvas_.size() * 4);
}

std::vector<uint8_t> Presenter::gameAreaRgba() const {
    std::vector<uint8_t> out(static_cast<size_t>(kGameWidth) * kGameHeight * 4);
    for (int y = 0; y < kGameHeight; ++y)
        std::memcpy(out.data() + static_cast<size_t>(y) * kGameWidth * 4,
                    canvas_.data() + static_cast<size_t>(kGameY + y) * kCanvasWidth + kGameX, static_cast<size_t>(kGameWidth) * 4);
    return out;
}

bool Presenter::writeCanvasPng(const std::filesystem::path& path) const {
    return png::writeRgba(path, kCanvasWidth, kCanvasHeight, canvas());
}

bool Presenter::writeGameAreaPng(const std::filesystem::path& path) const {
    const auto rgba = gameAreaRgba();
    return png::writeRgba(path, kGameWidth, kGameHeight, rgba);
}

bool Presenter::attach(SDL_Renderer* renderer, bool integerScale) {
    detach();
    renderer_ = renderer;
    texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kCanvasWidth, kCanvasHeight);
    if (!texture_) {
        log::error("SDL_CreateTexture failed: {}", SDL_GetError());
        renderer_ = nullptr;
        return false;
    }
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
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
    if (renderer_)
        SDL_SetRenderLogicalPresentation(renderer_, kCanvasWidth, kCanvasHeight,
                                         on ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE : SDL_LOGICAL_PRESENTATION_LETTERBOX);
    changed_ = true;
}

void Presenter::render(bool force) {
    if (!renderer_ || !texture_) return;
    if (!changed_ && !force) return;
    SDL_UpdateTexture(texture_, nullptr, canvas_.data(), kCanvasWidth * 4);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
    changed_ = false;
}

}  // namespace tmport::gfx
