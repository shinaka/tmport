#pragma once
// Presenter: models the game window. The original copies rectangles of the offscreen buffer to
// the window (Gfx_BlitToWindow 1058:05bc, Gfx_FlushDirtyRects 1058:0731); whatever was not
// presented is not visible. The presenter keeps that "window picture" and a CPU-composed RGBA
// canvas laid out by a PresenterLayout. With a renderer attached it uploads the canvas to an SDL
// streaming texture shown letterboxed (optionally with integer scaling). Without a renderer
// (headless) only the canvas exists.
//
// Layouts (PresenterLayout):
//   Windows CD edition: 640x480 canvas, black, game area 512x384 at (64,48), square pixels, with
//     the overlay layer of the in-engine menu bar. The game area is a 512x384 picture of palette
//     indices (blitToWindow from the FrameBuffer).
//   1990 DOS edition: 320x200 canvas = game area, pixels 1.2 times as tall as wide (the picture is
//     4:3, as on a period monitor) or square on request, no overlay (the DOS game draws its own
//     menu bar). The game area is a ready RGBA frame (presentFrame) from the DOS palette code.
//
// Pixel convention (canvas, overlay, presentFrame, Palette::rgba()): one uint32_t per pixel whose
// bytes in memory are R, G, B, A (little-endian value 0xAABBGGRR, SDL_PIXELFORMAT_RGBA32); A = 255
// for opaque pixels. Rows top to bottom, no padding.

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "gfx/frame_buffer.h"
#include "gfx/palette.h"

struct SDL_Renderer;
struct SDL_Texture;

namespace tmport::gfx {

// Geometry of a presented picture.
struct PresenterLayout {
    int canvasWidth = 640;
    int canvasHeight = 480;
    Rect gameRect{64, 48, 512, 384};  // the game area inside the canvas
    // Shape of one canvas pixel on screen as height : width = aspectNum : aspectDen (1:1 square;
    // DOS 6:5 = 1.2, which shows 320x200 at 4:3).
    int aspectNum = 1;
    int aspectDen = 1;
    bool overlay = true;          // the overlay layer exists (Windows: the in-engine menu bar)
    int defaultWindowScale = 2;   // initial window = display size x this (reduced to fit the desktop)

    static PresenterLayout windowsCd();
    static PresenterLayout dos1990(bool squarePixels = false);

    double pixelAspect() const { return static_cast<double>(aspectNum) / aspectDen; }
    bool squarePixels() const { return aspectNum == aspectDen; }
    // The canvas at its pixel aspect, in canvas-width units: Windows 640x480, DOS 320x240.
    int displayWidth() const { return canvasWidth; }
    int displayHeight() const { return (canvasHeight * aspectNum + aspectDen / 2) / aspectDen; }

    bool operator==(const PresenterLayout&) const = default;
};

enum class ScaleMode : uint8_t {
    Letterbox,  // as large as fits, keeping the display aspect
    Integer,    // whole-number scale factors per axis (see integerScaleFor)
};

struct IntScale {
    int sx = 0, sy = 0;  // 0: no integer scale fits
    bool operator==(const IntScale&) const = default;
};

// Integer scale factors for an output of outW x outH pixels. Square pixels: (k, k), the largest k
// that fits. Other aspects: the largest exact pair (aspectDen*k, aspectNum*k) that fits (DOS 5x6 =
// 1600x1200 for 320x200, exact 4:3); when none fits, the largest sx with sy = round(sx * aspect)
// that fits (DOS on a 1080-line screen: 4x5 = 1280x1000). {0, 0} when not even 1x1 fits.
IntScale integerScaleFor(const PresenterLayout& layout, int outW, int outH);

// Where the canvas is drawn in an output of outW x outH pixels (centred, black around it). Integer
// mode falls back to letterboxing when the output is smaller than the canvas.
Rect presentRect(const PresenterLayout& layout, int outW, int outH, ScaleMode mode);

// Initial window size: display size x windowScale (<= 0: the layout's default), reduced by whole
// steps until it fits 90% of the desktop (desktopW/H 0 = unknown, no reduction). With integerScale
// and non-square pixels the window is the smallest exact integer picture at least windowScale
// times the canvas width (DOS: 5x6 = 1600x1200), or the largest integer picture that fits.
struct WindowSize {
    int w = 0, h = 0;
    bool operator==(const WindowSize&) const = default;
};
WindowSize initialWindowSize(const PresenterLayout& layout, int windowScale, bool integerScale, int desktopW = 0,
                             int desktopH = 0);

class Presenter {
public:
    // The Windows CD layout (PresenterLayout::windowsCd()).
    static constexpr int kCanvasWidth = 640;
    static constexpr int kCanvasHeight = 480;
    static constexpr int kGameX = 64;
    static constexpr int kGameY = 48;
    static constexpr int kGameWidth = 512;
    static constexpr int kGameHeight = 384;

    explicit Presenter(const PresenterLayout& layout = PresenterLayout::windowsCd());
    ~Presenter();
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    const PresenterLayout& layout() const { return layout_; }

    // Palette used to convert indices to RGB; re-renders the canvas.
    void setPalette(const Palette& p);
    const Palette& palette() const { return palette_; }

    // 1058:05bc Gfx_BlitToWindow: copies a rectangle of screen rows of the frame buffer into the
    // window picture (clipped to the game area, 512x384 for Windows).
    void blitToWindow(const FrameBuffer& fb, int x, int y, int w, int h);
    void blitAll(const FrameBuffer& fb) { blitToWindow(fb, 0, 0, layout_.gameRect.w, layout_.gameRect.h); }
    // 1058:0731 Gfx_FlushDirtyRects: presents every dirty rectangle, then clears the list.
    void flushDirtyRects(FrameBuffer& fb);

    // The game area as a ready RGBA frame (gameRect.w x gameRect.h pixels, the convention above;
    // the DOS edition's 320x200 frame after its palette conversion). From then on the game area
    // shows this frame (until blitToWindow is used again). A frame of another size is ignored.
    void presentFrame(std::span<const uint32_t> rgba);

    // Canvas RGBA bytes (R,G,B,A per pixel), canvasWidth x canvasHeight.
    std::span<const uint8_t> canvas() const;
    // RGBA copy of the game area (for F12 screenshots).
    std::vector<uint8_t> gameAreaRgba() const;
    bool writeCanvasPng(const std::filesystem::path& path) const;
    bool writeGameAreaPng(const std::filesystem::path& path) const;
    bool canvasChanged() const { return changed_; }

    // --- Overlay (the in-engine menu bar, ui/menu_bar.h) -----------------------------------------
    // A canvas-sized packed-RGBA layer composited over the canvas: pixels with alpha 0 show the
    // canvas (game area or black border) below, others replace it. The owner draws into
    // overlayPixels() (allocated, fully transparent, on first use) and calls overlayChanged() for
    // each region it changed. The window picture (game area) is not affected; F12 screenshots of the
    // game area show what is on the canvas, including an open menu. A layout without an overlay
    // returns an empty span and ignores overlayChanged().
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
    // Creates the streaming texture. Square-pixel layouts use the renderer's logical presentation
    // (letterbox or integer scale; SDL then converts mouse coordinates to canvas coordinates);
    // other layouts are drawn into presentRect() of the output. Returns false on failure.
    bool attach(SDL_Renderer* renderer, bool integerScale);
    void detach();
    void setIntegerScale(bool on);
    bool integerScale() const { return integerScale_; }
    // Uploads the canvas (if changed or force) and presents it.
    void render(bool force);
    // Window coordinates (SDL events, pointer) -> canvas coordinates (may lie outside the canvas
    // in the black border). False without a renderer.
    bool windowToCanvas(float wx, float wy, float& cx, float& cy) const;

private:
    void renderRect(int x, int y, int w, int h);         // game-area coordinates
    void composeCanvasRect(int x, int y, int w, int h);  // canvas coordinates, with the overlay
    void paintDebugMarks(int x, int y, int w, int h);    // canvas coordinates, under the overlay

    PresenterLayout layout_;
    int cw_, ch_;              // canvas size
    int gx_, gy_, gw_, gh_;    // game rect
    Palette palette_;
    std::vector<uint8_t> window_;    // game-area indices as last presented
    std::vector<uint32_t> frame_;    // game-area RGBA of presentFrame (empty: indices are shown)
    std::vector<uint32_t> canvas_;   // canvas packed RGBA (little-endian bytes R,G,B,A)
    std::vector<uint32_t> overlay_;  // canvas-sized packed RGBA, alpha 0 = transparent (empty: none)
    std::vector<DebugMark> marks_;
    bool changed_ = true;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    bool integerScale_ = false;
};

}  // namespace tmport::gfx
