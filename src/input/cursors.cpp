#include "input/cursors.h"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_surface.h>

#include <format>

#include "mischief/util/byte_reader.h"
#include "mischief/util/error.h"
#include "mischief/util/log.h"
#include "ne/ne_image.h"

namespace tmport {

namespace {

constexpr uint16_t kRtCursor = 1;
constexpr uint16_t kRtGroupCursor = 12;

bool bitAt(std::span<const uint8_t> rows, int stride, int row, int x) {
    return (rows[static_cast<size_t>(row * stride + x / 8)] >> (7 - (x & 7))) & 1;
}

}  // namespace

std::vector<uint8_t> CursorImage::rgba(int scale) const {
    if (scale < 1) scale = 1;
    const int w = width * scale, h = height * scale;
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const Pixel p = at(x / scale, y / scale);
            uint8_t* d = out.data() + (static_cast<size_t>(y) * w + x) * 4;
            if (p == kTransparent) continue;
            const uint8_t v = p == kWhite ? 255 : 0;
            d[0] = d[1] = d[2] = v;
            d[3] = 255;
        }
    return out;
}

CursorImage decodeCursorResource(std::span<const uint8_t> data) {
    mischief::ByteReader r(data, "RT_CURSOR");
    CursorImage img;
    img.hotX = r.u16(0);
    img.hotY = r.u16(2);
    const uint32_t hdrSize = r.u32(4);
    int width = 0, height2 = 0, bpp = 0, colours = 0, rgbSize = 0;
    if (hdrSize == 40) {  // BITMAPINFOHEADER
        width = r.i32(8);
        height2 = r.i32(12);
        if (r.u16(16) != 1) throw mischief::FormatError("RT_CURSOR: planes != 1");
        bpp = r.u16(18);
        if (r.u32(20) != 0) throw mischief::FormatError("RT_CURSOR: compressed bitmap");
        colours = static_cast<int>(r.u32(36));
        rgbSize = 4;
    } else if (hdrSize == 12) {  // BITMAPCOREHEADER
        width = r.u16(8);
        height2 = r.u16(10);
        bpp = r.u16(14);
        rgbSize = 3;
    } else {
        throw mischief::FormatError(std::format("RT_CURSOR: unsupported header size {}", hdrSize));
    }
    if (bpp != 1) throw mischief::FormatError(std::format("RT_CURSOR: {} bpp (only monochrome cursors are supported)", bpp));
    if (width <= 0 || width > 256 || height2 <= 0 || height2 > 512 || (height2 & 1))
        throw mischief::FormatError(std::format("RT_CURSOR: bad size {}x{}", width, height2));
    if (colours == 0) colours = 2;
    const int height = height2 / 2;
    const size_t tableOff = 4 + hdrSize;
    // Colour table: index -> white? (a monochrome cursor uses black and white).
    bool white[2] = {false, true};
    for (int i = 0; i < 2 && i < colours; ++i) {
        const auto c = r.bytes(tableOff + static_cast<size_t>(i * rgbSize), 3);
        white[i] = (c[0] + c[1] + c[2]) > 3 * 127;
    }
    const size_t bitsOff = tableOff + static_cast<size_t>(colours * rgbSize);
    const int stride = ((width + 31) / 32) * 4;
    const auto xorBits = r.bytes(bitsOff, static_cast<size_t>(stride) * height);
    const auto andBits = r.bytes(bitsOff + static_cast<size_t>(stride) * height, static_cast<size_t>(stride) * height);

    img.width = width;
    img.height = height;
    img.pixels.resize(static_cast<size_t>(width) * height);
    const int outStride = (width + 7) / 8;
    img.andMask.assign(static_cast<size_t>(outStride) * height, 0);
    img.xorMask.assign(static_cast<size_t>(outStride) * height, 0);
    for (int y = 0; y < height; ++y) {
        const int row = height - 1 - y;  // DIB rows are bottom-up
        for (int x = 0; x < width; ++x) {
            const bool a = bitAt(andBits, stride, row, x);
            const bool xb = bitAt(xorBits, stride, row, x);
            const bool wh = white[xb ? 1 : 0];
            CursorImage::Pixel p;
            if (!a)
                p = wh ? CursorImage::kWhite : CursorImage::kBlack;
            else
                p = wh ? CursorImage::kInvert : CursorImage::kTransparent;
            img.pixels[static_cast<size_t>(y * width + x)] = p;
            const auto bit = static_cast<uint8_t>(0x80 >> (x & 7));
            if (a) img.andMask[static_cast<size_t>(y * outStride + x / 8)] |= bit;
            if (xb) img.xorMask[static_cast<size_t>(y * outStride + x / 8)] |= bit;
        }
    }
    if (img.hotX >= width || img.hotY >= height)
        throw mischief::FormatError(std::format("RT_CURSOR: hot spot ({},{}) outside {}x{}", img.hotX, img.hotY, width, height));
    return img;
}

CursorImage loadGroupCursor(const ne::NeImage& image, uint16_t groupId) {
    const ne::Resource* g = image.findResource(kRtGroupCursor, groupId);
    if (!g) throw mischief::Error(std::format("{}: RT_GROUP_CURSOR {} not found", image.name(), groupId));
    const auto gdata = image.readResource(*g);
    mischief::ByteReader r(gdata, std::format("RT_GROUP_CURSOR {}", groupId));
    if (r.u16(0) != 0 || r.u16(2) != 2) throw mischief::FormatError(std::format("RT_GROUP_CURSOR {}: not a cursor directory", groupId));
    const int count = r.u16(4);
    if (count < 1) throw mischief::FormatError(std::format("RT_GROUP_CURSOR {}: empty", groupId));
    int pick = 0;
    for (int i = 0; i < count; ++i)
        if (r.u16(6 + static_cast<size_t>(i) * 14 + 6) == 1) {
            pick = i;
            break;
        }
    const size_t e = 6 + static_cast<size_t>(pick) * 14;
    const uint16_t cursorId = r.u16(e + 12);
    const uint32_t bytes = r.u32(e + 8);
    const ne::Resource* c = image.findResource(kRtCursor, cursorId);
    if (!c) throw mischief::Error(std::format("{}: RT_CURSOR {} (group {}) not found", image.name(), cursorId, groupId));
    auto cdata = image.readResource(*c);
    if (bytes != 0 && bytes <= cdata.size()) cdata.resize(bytes);  // drop the NE alignment padding
    return decodeCursorResource(cdata);
}

// ------------------------------------------------------------------------------------ CursorSet

CursorSet::~CursorSet() { detach(); }

void CursorSet::load(const ne::NeImage& image, std::span<const uint16_t> ids) {
    freeSet();
    for (size_t i = 0; i < ids.size() && i < static_cast<size_t>(kMaxCursors); ++i) {
        if (ids[i] == 0xFFFF) break;
        try {
            images_[i] = loadGroupCursor(image, ids[i]);
        } catch (const std::exception& ex) {
            mischief::log::warn("cursor {}: {}", ids[i], ex.what());
        }
    }
    if (attached_) {
        createSdlCursors();
        apply();
    }
}

void CursorSet::freeSet() {
    destroySdlCursors();
    for (auto& i : images_) i.reset();
}

const CursorImage* CursorSet::image(int idx) const {
    if (idx < 0 || idx >= kMaxCursors || !images_[static_cast<size_t>(idx)]) return nullptr;
    return &*images_[static_cast<size_t>(idx)];
}

int CursorSet::loadedCount() const {
    int n = 0;
    for (const auto& i : images_) n += i.has_value();
    return n;
}

int CursorSet::select(int idx) {
    const int old = curIndex_;
    curIndex_ = idx;
    shown_ = Shown::Custom;
    shownIndex_ = idx;
    apply();
    return old;
}

void CursorSet::setArrow() {
    shown_ = Shown::Arrow;
    apply();
}

void CursorSet::setWait() {
    shown_ = Shown::Wait;
    apply();
}

void CursorSet::attach(int scale) {
    scale_ = scale < 1 ? 1 : scale;
    attached_ = true;
    if (!arrow_) arrow_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    if (!wait_) wait_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
    createSdlCursors();
    apply();
}

void CursorSet::detach() {
    destroySdlCursors();
    if (arrow_) SDL_DestroyCursor(arrow_);
    if (wait_) SDL_DestroyCursor(wait_);
    arrow_ = wait_ = nullptr;
    attached_ = false;
}

void CursorSet::setScale(int scale) {
    if (scale < 1) scale = 1;
    if (scale == scale_) return;
    scale_ = scale;
    if (attached_) {
        createSdlCursors();
        apply();
    }
}

void CursorSet::createSdlCursors() {
    destroySdlCursors();
    for (int i = 0; i < kMaxCursors; ++i) {
        const CursorImage* img = image(i);
        if (!img) continue;
        auto px = img->rgba(scale_);
        SDL_Surface* s = SDL_CreateSurfaceFrom(img->width * scale_, img->height * scale_, SDL_PIXELFORMAT_RGBA32,
                                               px.data(), img->width * scale_ * 4);
        if (!s) {
            mischief::log::warn("cursor {}: SDL_CreateSurfaceFrom failed: {}", i, SDL_GetError());
            continue;
        }
        sdl_[static_cast<size_t>(i)] = SDL_CreateColorCursor(s, img->hotX * scale_, img->hotY * scale_);
        if (!sdl_[static_cast<size_t>(i)]) mischief::log::warn("cursor {}: SDL_CreateColorCursor failed: {}", i, SDL_GetError());
        SDL_DestroySurface(s);
    }
}

void CursorSet::destroySdlCursors() {
    for (auto& c : sdl_) {
        if (c) SDL_DestroyCursor(c);
        c = nullptr;
    }
}

void CursorSet::apply() {
    if (!attached_) return;
    SDL_Cursor* c = nullptr;
    switch (shown_) {
        case Shown::Default:
        case Shown::Arrow: c = arrow_; break;
        case Shown::Wait: c = wait_; break;
        case Shown::Custom:
            if (shownIndex_ >= 0 && shownIndex_ < kMaxCursors) c = sdl_[static_cast<size_t>(shownIndex_)];
            if (!c) c = arrow_;
            break;
    }
    if (c) SDL_SetCursor(c);
}

}  // namespace tmport
