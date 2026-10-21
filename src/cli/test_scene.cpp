#include "cli/test_scene.h"

#include <algorithm>
#include <format>

#include "data/dgroup_offsets.h"

namespace tmport::cli {

TestSceneResult composeTestScene(gfx::FrameBuffer& fb, const ResourceManager& res, const ExeTables& exe,
                                 const mischief::gfx::Palette& palette, mischief::gfx::ColorMode mode) {
    using gfx::FrameBuffer;
    TestSceneResult out;
    auto pm = [&](int id) {
        auto p = res.pixmap(id, mode);
        if (!p) {
            out.ok = false;
            out.notes.push_back(std::format("FAIL: PMAP {} missing", id));
        }
        return p;
    };

    fb.setPalette(palette);
    fb.clear(palette.pen(0));
    fb.setDirtyEnabled(false);
    fb.resetClip();

    // Room 9 (the first mountain room) composed as Room_DrawBackground(0) does: background into
    // the back page at (0,404), pristine copy at (0,788), slot pictures at x {26,197,368},
    // layer-1 y 474, then the play-field copied to the screen at (0,20).
    const auto rooms = exe.rooms();
    const int bgId = rooms[9].bgPmap;
    if (auto bg = pm(bgId)) {
        fb.drawOpaque(0, FrameBuffer::kLayer1Y + FrameBuffer::kPlayTop, *bg);
        out.notes.push_back(std::format("room 9 background PMAP {} ({}x{}) at (0,404)", bgId, bg->width, bg->height));
    }
    fb.copyRect(0, 404, 512, FrameBuffer::kPlayHeight, 0, FrameBuffer::kLayer2Y);
    const std::array<int, 3> slotX = {exe.i16(dg::kSceneSlotX), exe.i16(dg::kSceneSlotX + 2), exe.i16(dg::kSceneSlotX + 4)};
    const std::array<int, 3> slotPmap = {1400, 1437, 1474};
    fb.setFullClip();
    for (size_t i = 0; i < 3; ++i)
        if (auto s = pm(slotPmap[i])) fb.drawSprite(slotX[i], 474, *s, false);
    fb.copyRect(0, 404, 512, FrameBuffer::kPlayHeight, 0, FrameBuffer::kPlayTop);
    out.notes.push_back(std::format("slot pictures {} {} {} at x {} {} {}", slotPmap[0], slotPmap[1], slotPmap[2], slotX[0],
                                    slotX[1], slotX[2]));

    // Status bar: line at y 287, panel (HUD art PMAP 1306) at (5, 0x123).
    fb.setForeColor(0);
    fb.fillRect(0, 287, 512, 97);
    if (auto hud = pm(1306)) fb.drawOpaque(5, 0x123, *hud);

    // Sprites inside the play-field clip, with dirty tracking as in the room loop.
    fb.setPlayClip(0);
    fb.setDirtyEnabled(true);
    fb.clearDirtyRects();
    if (auto player = pm(1100)) {
        fb.drawSprite(233, 142, *player, false);  // facing right
        fb.drawSprite(90, 142, *player, true);    // mirrored (facing left)
    }
    if (auto elf = pm(1612)) {
        fb.drawSprite(400, 190, *elf, false);
        fb.drawSprite(450, 190, *elf, true);
    }
    if (auto fountain = pm(1380)) fb.drawSprite(150, 60, *fountain, false);  // raw 8 bpp in TMT256
    if (auto coins = pm(1699)) fb.drawSprite(330, 230, *coins, false);
    if (auto treasure = pm(1700)) {
        fb.drawSprite(-30, 100, *treasure, false);  // clipped at the left edge
        fb.drawSprite(470, 250, *treasure, false);  // clipped at the right edge and the play-field bottom (286)
        fb.drawSprite(250, 0, *treasure, false);    // clipped at the play-field top (20)
    }
    out.notes.push_back(std::format("{} dirty rectangles recorded", fb.dirtyRects().size()));
    fb.setDirtyEnabled(false);
    fb.resetClip();

    // A message box: white fill, black frame, highlighted blocks, pen lines. Save-under first
    // (x 301 is rounded down to 300 and the width 181 up to 182, as 1058:1643 does). The area is
    // one pixel larger than the box because the 16-colour frame's top and left strokes overhang.
    const gfx::FrameBuffer before = fb;
    const gfx::PixMap saved = fb.saveRect(301, 30, 181, 45);
    fb.fillRectColor(300, 30, 180, 44, 15);
    fb.frameRectColor(300, 30, 180, 44, 0);
    fb.fillRectColor(310, 40, 60, 17, 4);  // red block -> brown when highlighted
    fb.fillRectColor(380, 40, 80, 17, 0);  // black block -> blue when highlighted
    fb.highlightRect(310, 40, 150, 17, palette.highlightLut());  // white background -> yellow
    fb.setForeColor(14);
    fb.hline(310, 62, 150);
    fb.vline(470, 34, 36);

    // Self-check: restoring the save-under on a copy must give back the frame without the box.
    gfx::FrameBuffer restored = fb;
    restored.restoreRect(301, 30, saved);
    const bool same = std::equal(before.pixels(), before.pixels() + static_cast<size_t>(FrameBuffer::kWidth) * FrameBuffer::kHeight,
                                 restored.pixels());
    if (!same) out.ok = false;
    out.notes.push_back(std::format("{}: save-under {}x{} restored at x {}", same ? "ok" : "FAIL", saved.width,
                                    saved.height, 301 & ~1));
    return out;
}

}  // namespace tmport::cli
