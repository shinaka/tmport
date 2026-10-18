#pragma once
// Presenter: models the game window. The original copies rectangles of the offscreen buffer to
// the window (Gfx_BlitToWindow 1058:05bc, Gfx_FlushDirtyRects 1058:0731); whatever was not
// presented is not visible. The presenter keeps that "window picture" (512x384 indices) and a
// CPU-composed 640x480 RGBA canvas: black, game area at (64,48). With a renderer attached it
// uploads the canvas to an SDL streaming texture shown with logical presentation (letterbox,
// optional integer scaling). Without a renderer (headless) only the canvas exists.

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "gfx/frame_buffer.h"
#include "gfx/palette.h"

struct SDL_Renderer;
struct SDL_Texture;

namespace tmport::gfx {

class Presenter {
public:
    static constexpr int kCanvasWidth = 640;
    static constexpr int kCanvasHeight = 480;
    static constexpr int kGameX = 64;
    static constexpr int kGameY = 48;
    static constexpr int kGameWidth = 512;
    static constexpr int kGameHeight = 384;

    Presenter();
    ~Presenter();
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    // Palette used to convert indices to RGB; re-renders the canvas.
    void setPalette(const Palette& p);
    const Palette& palette() const { return palette_; }

    // 1058:05bc Gfx_BlitToWindow: copies a rectangle of screen rows 0..383 into the window
    // picture (clipped to 512x384).
    void blitToWindow(const FrameBuffer& fb, int x, int y, int w, int h);
    void blitAll(const FrameBuffer& fb) { blitToWindow(fb, 0, 0, kGameWidth, kGameHeight); }
    // 1058:0731 Gfx_FlushDirtyRects: presents every dirty rectangle, then clears the list.
    void flushDirtyRects(FrameBuffer& fb);

    // 640x480 RGBA bytes (R,G,B,A per pixel).
    std::span<const uint8_t> canvas() const;
    // 512x384 RGBA copy of the game area (for F12 screenshots).
    std::vector<uint8_t> gameAreaRgba() const;
    bool writeCanvasPng(const std::filesystem::path& path) const;
    bool writeGameAreaPng(const std::filesystem::path& path) const;
    bool canvasChanged() const { return changed_; }

    // --- Overlay (the in-engine menu bar, ui/menu_bar.h) -----------------------------------------
    // A 640x480 packed-RGBA layer composited over the canvas: pixels with alpha 0 show the canvas
    // (game area or black border) below, others replace it. The owner draws into overlayPixels()
    // (allocated, fully transparent, on first use) and calls overlayChanged() for each region it
    // changed. The window picture (game area) is not affected; F12 screenshots of the game area
    // show what is on the canvas, including an open menu.
    std::span<uint32_t> overlayPixels();
    void overlayChanged(int x, int y, int w, int h);
    // Makes the whole overlay transparent (and recomposes the regions it covered).
    void clearOverlay();
    bool hasOverlay() const { return !overlay_.empty(); }

    // --- Debug marks (tmport --show-treasure) ----------------------------------------------------
    // Rectangle outlines (kDebugMarkThickness px, inside x/y/w/h) in game-area coordinates, drawn
    // over the game area and under the overlay. Like the overlay they leave the window picture
    // alone, so nothing the game draws or reads back changes. Setting different marks recomposes
    // the areas of the old and the new ones.
    struct DebugMark {
        int x = 0, y = 0, w = 0, h = 0;
        uint32_t rgba = 0;  // packed like the canvas: bytes R,G,B,A
        bool operator==(const DebugMark&) const = default;
    };
    static constexpr int kDebugMarkThickness = 2;
    void setDebugMarks(std::vector<DebugMark> marks);
    const std::vector<DebugMark>& debugMarks() const { return marks_; }

    // --- SDL output (interactive mode only) ----------------------------------------------------
    // Creates the streaming texture and sets logical presentation. Returns false on failure.
    bool attach(SDL_Renderer* renderer, bool integerScale);
    void detach();
    void setIntegerScale(bool on);
    bool integerScale() const { return integerScale_; }
    // Uploads the canvas (if changed or force) and presents it.
    void render(bool force);

private:
    void renderRect(int x, int y, int w, int h);
    void composeCanvasRect(int x, int y, int w, int h);  // canvas coordinates, with the overlay
    void paintDebugMarks(int x, int y, int w, int h);    // canvas coordinates, under the overlay

    Palette palette_;
    std::vector<uint8_t> window_;   // 512x384 indices as last presented
    std::vector<uint32_t> canvas_;  // 640x480 packed RGBA (little-endian bytes R,G,B,A)
    std::vector<uint32_t> overlay_;  // 640x480 packed RGBA, alpha 0 = transparent (empty: none)
    std::vector<DebugMark> marks_;
    bool changed_ = true;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    bool integerScale_ = false;
};

}  // namespace tmport::gfx
