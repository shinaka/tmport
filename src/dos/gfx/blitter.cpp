#include "dos/gfx/blitter.h"

#include "dos/gfx/pak.h"

namespace tmport::dos {

namespace {

// Mask table 2005:8600: nibble F where the pixel of `b` is colour 0, else 0.
constexpr uint8_t maskOf(uint8_t b) {
    return static_cast<uint8_t>(((b & 0xF0) == 0 ? 0xF0 : 0) | ((b & 0x0F) == 0 ? 0x0F : 0));
}

constexpr uint8_t swapNibbles(uint8_t b) { return static_cast<uint8_t>((b << 4) | (b >> 4)); }

// The clipping of 1000:1be7..1c59 (also 1000:2081..20e9): left/top move the start and shrink the
// size; right/bottom shrink it; nothing remains -> nullopt. Returns {x, y, w, h, skipLeft, skipTop}.
struct Clipped {
    int x, y, w, h, skipLeft, skipTop;
};
std::optional<Clipped> clipRect(const ClipWindow& c, int x, int y, int w, int h) {
    Clipped r{x, y, w, h, 0, 0};
    if (x < c.xMin) {
        r.skipLeft = c.xMin - x;
        r.x = c.xMin;
        r.w -= r.skipLeft;
        if (r.w <= 0) return std::nullopt;
    }
    if (x + w > c.xMax) {  // the original compares x + width (one past the end) with the inclusive max
        r.w -= x + w - c.xMax - 1;
        if (r.w <= 0) return std::nullopt;
    }
    if (y < c.yMin) {
        r.skipTop = c.yMin - y;
        r.y = c.yMin;
        r.h -= r.skipTop;
        if (r.h <= 0) return std::nullopt;
    }
    if (y + h > c.yMax) {
        r.h -= y + h - c.yMax - 1;
        if (r.h <= 0) return std::nullopt;
    }
    return r;
}

}  // namespace

SpriteImage SpriteImage::of(const PakFrame& f) { return {f.widthBytes, f.height, f.xOffset, f.yOffset, f.rows.data()}; }

std::optional<DrawnArea> drawSprite(BackBuffer& buf, const SpriteImage& img, int x, int y, uint8_t flags) {
    using namespace sprite_flags;
    if (!(flags & kNoHotspot)) {  // 1000:1bae
        y += img.yOffset;
        if (flags & kMirror) x -= img.widthBytes + img.xOffset;
        else x += img.xOffset;
    }
    const auto c = clipRect(buf.clip(), x, y, img.widthBytes, img.height);
    if (!c) return std::nullopt;
    const bool reversed = (flags & (kReverse | kMirror)) != 0;  // 1000:1ca8
    const bool masked = (flags & kMasked) != 0;
    for (int r = 0; r < c->h; ++r) {
        const int dy = c->y + r;
        if (dy < 0 || dy >= BackBuffer::kHeight) continue;
        const uint8_t* src = img.rows + static_cast<size_t>(c->skipTop + r) * img.widthBytes;
        uint8_t* dst = buf.row(dy);
        for (int j = 0; j < c->w; ++j) {
            const int dx = c->x + j;
            // Reversed rows read from the row's end: destination byte j <- source byte
            // (width - 1 - skipLeft - j), nibbles swapped (ROR AL,4).
            const uint8_t s = reversed ? swapNibbles(src[img.widthBytes - 1 - c->skipLeft - j]) : src[c->skipLeft + j];
            if (dx < 0 || dx >= BackBuffer::kRowBytes) continue;
            dst[dx] = masked ? static_cast<uint8_t>((dst[dx] & maskOf(s)) | s) : s;
        }
    }
    buf.noteDrawn(c->x, c->y, c->w, c->h);  // the tail 1000:1911 with the clipped rectangle
    return DrawnArea{c->x, c->y, c->w, c->h};
}

std::optional<DrawnArea> drawSprite(BackBuffer& buf, const PakFrame& frame, int x, int y, uint8_t flags) {
    return drawSprite(buf, SpriteImage::of(frame), x, y, flags);
}

std::optional<SavedRect> saveUnder(const BackBuffer& buf, int x, int y, int widthBytes, int height) {
    const auto c = clipRect(buf.clip(), x, y, widthBytes, height);
    if (!c) return std::nullopt;
    SavedRect s;
    s.x = c->x;
    s.y = c->y;
    s.widthBytes = c->w;
    s.height = c->h;
    s.bytes.resize(static_cast<size_t>(c->w) * c->h);
    for (int r = 0; r < c->h; ++r)
        for (int j = 0; j < c->w; ++j) {
            const int dx = c->x + j, dy = c->y + r;
            const bool in = dx >= 0 && dx < BackBuffer::kRowBytes && dy >= 0 && dy < BackBuffer::kHeight;
            s.bytes[static_cast<size_t>(r) * c->w + j] = in ? buf.byteAt(dx, dy) : 0;
        }
    return s;
}

// --------------------------------------------------------------------------------- renderer

void SpriteRenderer::reset() {
    for (int i = 0; i < kRecords; ++i) {
        records[static_cast<size_t>(i)] = DrawRecord{};
        records[static_cast<size_t>(i)].owner = static_cast<uint16_t>(i);
        drawOrder[static_cast<size_t>(i)] = i;
    }
    spritesChanged = false;
    saveHeapReset = true;
}

int SpriteRenderer::drawSorted(BackBuffer& buf, const PakFile& pak, int from, int to, bool titleMode, bool dirty) {
    if (!spritesChanged && !dirty) return 0;
    buf.setClip(titleMode ? kFullScreenClip : kPlayfieldClip);
    int drawn = 0;
    int pick = from;  // the original's local is not reset between picks (and starts as stack garbage)
    for (int out = from; out < to; ++out) {
        int best = 0;
        for (int k = from; k < to; ++k) {
            const DrawRecord& r = records[static_cast<size_t>(k)];
            if (r.z >= best && !(r.owner & 0x8000)) {
                best = r.z;
                pick = k;
            }
        }
        drawOrder[static_cast<size_t>(out)] = pick;
        DrawRecord& r = records[static_cast<size_t>(pick)];
        const int o = r.owner;  // bit 15 is clear: drawn records are never picked
        int16_t x = r.x, y = r.y;
        if (o != pick && o < kRecords) {  // a child: offset from its owner's record
            x = static_cast<int16_t>(x + records[static_cast<size_t>(o)].x);
            y = static_cast<int16_t>(y + records[static_cast<size_t>(o)].y);
        }
        r.owner = static_cast<uint16_t>(o | 0x8000);
        if (r.frame == 0xFFFF) continue;
        const uint8_t flags = (r.frame & 1) ? 0x60 : 0x40;
        if (r.frame < 6) buf.setClip(kFullScreenClip);  // pointer frames
        const int index = r.frame >> 1;
        if (index >= pak.count()) {  // the original would read past the pack; draw nothing
            r.save.reset();
            continue;
        }
        const PakFrame& f = pak.frame(index);
        const int sx = (flags & sprite_flags::kMirror) ? x - (f.widthBytes + f.xOffset) : x + f.xOffset;
        if (saveHeapReset) {  // 1000:205c: AX = 1 restarts the heap (before the clipping)
            heapOffset = heapBase;
            heapSegment = 0x1AE7;
        }
        auto save = saveUnder(buf, sx, y + f.yOffset, f.widthBytes, f.height);
        if (!save) {
            r.save.reset();
            continue;
        }
        const uint16_t end = static_cast<uint16_t>(heapOffset + save->heapBytes());
        r.save = std::move(save);
        heapOffset = end;
        if (heapSegment != 0 || saveHeapHigh < end) saveHeapHigh = end;  // 4306:0664
        drawSprite(buf, f, x, y, flags);
        ++drawn;
        spritesChanged = true;
        saveHeapReset = false;
    }
    buf.setClip(kFullScreenClip);
    return drawn;
}

void SpriteRenderer::eraseAll(BackBuffer& buf, int from, int to) {
    if (!spritesChanged) return;
    for (int j = to - from - 1; j >= 0; --j) {
        const DrawRecord& r = records[static_cast<size_t>(drawOrder[static_cast<size_t>(j)])];
        if (r.frame != 0xFFFF && r.save) drawSprite(buf, r.save->image(), 0, 0, sprite_flags::kCgaNormal);
    }
    saveHeapReset = true;
    spritesChanged = false;
    for (auto& r : records) r.owner &= 0x7FFF;
}

}  // namespace tmport::dos
