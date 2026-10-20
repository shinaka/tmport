#include "gg/gfx/video.h"

#include <algorithm>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/data/resources.h"

namespace tmport::gg {

namespace {

// The click zone embedded at +8 of a 28-byte cursor region in DGROUP (code_runtime.md §12.1).
CursorRegion regionFromExe(const GgExe& exe, uint16_t regionOff) {
    const uint16_t z = regionOff + 8;
    CursorRegion r;
    r.shape = exe.dsS16(z);
    r.x = exe.dsS16(z + 2);
    r.y = exe.dsS16(z + 4);
    r.w = exe.dsS16(z + 6);
    r.h = exe.dsS16(z + 8);
    r.priority = exe.dsS16(z + 0x0A);
    return r;
}

}  // namespace

// 1581:052a Dirty_Flush
void dirtyFlush(Screen& screen, Cursor& cursor, Vram& vram) {
    DirtyMap& map = screen.dirty();
    if (map.pending() == 0) return;
    cursor.draw();  // Mouse_DrawCursor (marks the cursor's tiles)
    const auto src = screen.screenPixels();
    uint32_t count = 0;
    for (int ty = 0; ty < DirtyMap::kRows; ++ty)
        for (int tx = 0; tx < DirtyMap::kCols; ++tx) {
            if (!map.tile(tx, ty)) continue;
            map.setTile(tx, ty, false);
            ++count;
            for (int r = 0; r < 8; ++r) {
                const size_t o = static_cast<size_t>(ty * 8 + r) * Screen::kWidth + static_cast<size_t>(tx * 8);
                std::copy_n(src.begin() + static_cast<ptrdiff_t>(o), 8, vram.begin() + static_cast<ptrdiff_t>(o));
            }
        }
    screen.resetMouseHide();
    map.setPending(0);
    ++map.flushes;
    map.tilesCopied += count;
}

void vramToRgba(const Vram& vram, const Dac& dac, std::span<uint32_t> out) {
    const auto lut = dac.rgba();
    const size_t n = std::min(out.size(), vram.size());
    for (size_t i = 0; i < n; ++i) out[i] = lut[vram[i]];
}

std::vector<uint32_t> vramToRgba(const Vram& vram, const Dac& dac) {
    std::vector<uint32_t> out(vram.size());
    vramToRgba(vram, dac, out);
    return out;
}

Video::Video() : cursor_(screen_) {
    cursor_.setFlush([this] { flush(); });
}

// 1473:0074 Gfx_Init (after Video_SetMode13h)
PaletteLoad Video::init(const GgExe& exe, const Archive& archive, int driverX, int driverY) {
    // 17ab:000e Mouse_Init
    cursor_.init(CursorRecord::fromResource(kArrowCursor, archive.read(kArrowCursor)), driverX, driverY,
                 regionFromExe(exe, dg::kCursorRegionDefault), regionFromExe(exe, dg::kCursorRegionForced));
    palette_ = Palette::fromExe(exe);  // DS:024e is initialised data
    screen_.init();
    return palette_.blackout();  // 1473:000c Pal_Blackout
}

// 17a0 Mouse_MoveCursor
void Video::moveCursor(int x, int y) {
    cursor_.restoreUnder();
    cursor_.setPosition(x, y);
    flush();
}

// 1473:0399 Screen_LoadPicture
PaletteLoad Video::loadPicture(const Archive& archive, uint16_t pic, int pal, std::vector<uint8_t>& picture) {
    PaletteLoad load;
    if (pal > 0) {
        const auto bytes = archive.read(static_cast<uint16_t>(pal));
        load = palette_.apply(parsePaletteRange(bytes));
    }
    picture = archive.read(pic);
    return load;
}

}  // namespace tmport::gg
