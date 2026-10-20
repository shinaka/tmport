#include "gg/ui/scroll_bar.h"

#include <algorithm>

#include "gg/data/archive.h"
#include "gg/data/gg_exe.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

int16_t s16(int v) { return static_cast<int16_t>(v); }
int half(int v) { return v / 2; }

}  // namespace

ScrollBar ScrollBar::fromRecord(const GgExe& exe, uint16_t dsOff) {
    ScrollBar sb;
    auto w = [&](int o) { return exe.dsS16(static_cast<uint16_t>(dsOff + o)); };
    sb.x = w(0);
    sb.y = w(2);
    sb.w = w(4);
    sb.h = w(6);
    sb.pos = w(8);
    sb.max = w(0xA);
    sb.page = w(0xC);
    sb.nextRepeat = exe.ds32(static_cast<uint16_t>(dsOff + 0xE));
    sb.thumbY = w(0x12);
    return sb;
}

ScrollBarPictures ScrollBarPictures::load(const Archive& archive, uint16_t id) {
    ScrollBarPictures p;
    p.bundle = Bundle::load(archive, id);
    for (const BundleMember& m : p.bundle.members()) p.members.push_back(m.payload);
    if (p.members.size() < 5) throw FormatError("scroll bar bundle has fewer than 5 members");
    return p;
}

// 406a:0000 ScrollBar_DrawThumb
void scrollBarDrawThumb(const UiCanvas& ui, int x, int y) {
    ui.screen.setDrawColor(0xF1);
    ui.screen.frameRect(x, y, 11, 11);
    ui.screen.setDrawColor(0xCB);
    ui.screen.fillRect(x + 1, y + 1, 9, 9);
}

// 406a:003f ScrollBar_Draw
void scrollBarDraw(const UiCanvas& ui, const ScrollBarPictures& pics, ScrollBar& sb) {
    Screen& s = ui.screen;
    s.setDrawColor(0xC8);
    s.fillRect(sb.x, sb.y, sb.w, sb.h);
    s.setDrawColor(0xF1);
    s.frameRect(sb.x, sb.y, sb.w, sb.h);
    s.setDrawColor(15);
    const bool active = sb.max > 0;
    s.lpxDrawKeyed(sb.x, sb.y, pics.members[active ? 1 : 3], 0);
    s.lpxDrawKeyed(sb.x, sb.y + sb.h - 11, pics.members[active ? 2 : 4], 0);
    if (active) {
        // IMUL then CWD: the product's low word, sign-extended, divided by max.
        sb.thumbY = sb.y + s16(s16(sb.h - 31) * sb.pos) / sb.max + 10;
        scrollBarDrawThumb(ui, sb.x, sb.thumbY);
    }
}

// 406a:013e ScrollBar_SetPos
void scrollBarSetPos(const UiCanvas& ui, const ScrollBarPictures& pics, ScrollBar& sb, int p) {
    p = p < 0 ? 0 : std::min(p, sb.max);
    if (sb.pos == p) return;
    sb.pos = p;
    scrollBarDraw(ui, pics, sb);
    ui.dirtyFlush();
}

// 406a:017f ScrollBar_SetRange
void scrollBarSetRange(const UiCanvas& ui, const ScrollBarPictures& pics, ScrollBar& sb, int m) {
    m = std::max(m, 0);
    if (sb.max == m) return;
    sb.max = m;
    if (sb.pos > m) sb.pos = m;
    scrollBarDraw(ui, pics, sb);
    ui.dirtyFlush();
}

// 406a:01b6 ScrollBar_HandleClick
std::optional<int> scrollBarHandleClick(const UiCanvas& ui, const ScrollBarPictures& pics, ScrollBar& sb,
                                        uint16_t code, int mouseY, uint32_t now) {
    if (sb.max <= 0) return std::nullopt;
    bool act = false;
    if (code & 0x4000) {
        sb.nextRepeat = now + 27;
        act = true;
    }
    if (!(sb.nextRepeat < now) && !act) return std::nullopt;
    int target = 0;
    if (sb.y + 10 > mouseY) target = sb.pos - 1;
    else if (sb.y + sb.h - 10 < mouseY) target = sb.pos + 1;
    else if (sb.thumbY > mouseY) target = sb.pos - sb.page;
    else if (sb.thumbY + 11 <= mouseY) target = sb.pos + sb.page;
    else return mouseY - sb.thumbY;  // on the thumb: the drag
    scrollBarSetPos(ui, pics, sb, target);
    sb.nextRepeat = now + 27;
    return std::nullopt;
}

// 406a:02a0 the drag loop's body
void scrollBarDragMove(const UiCanvas& ui, ScrollBar& sb, int grab, int mouseX, int mouseY, int& dragY, int& thumb) {
    if (mouseY == dragY || sb.x > mouseX || sb.x + sb.w <= mouseX) return;
    dragY = mouseY;
    thumb = std::min(std::max(dragY - grab, sb.y + 9), sb.y + sb.h - 19);
    if (thumb == sb.thumbY) return;
    ui.screen.restoreRect(sb.x, sb.y, sb.w, sb.h);
    scrollBarDrawThumb(ui, sb.x, thumb);
    ui.dirtyFlush();
}

// 406a:032f the end of the drag
void scrollBarDragEnd(const UiCanvas& ui, const ScrollBarPictures& pics, ScrollBar& sb, int thumb, uint32_t now) {
    const int range = sb.h - 31;
    const int16_t num = s16(s16(s16(thumb - (sb.y + 10)) * sb.max) + half(range));
    const int p = range != 0 ? num / range : 0;
    sb.pos = -1;  // forces the redraw
    sb.thumbY = thumb;
    scrollBarSetPos(ui, pics, sb, p);
    sb.nextRepeat = now + 27;
}

}  // namespace tmport::gg
