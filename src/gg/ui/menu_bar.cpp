#include "gg/ui/menu_bar.h"

#include <algorithm>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

constexpr size_t kMenuDefSize = 0x14;

text::Bytes farText(const GgExe& exe, uint16_t ptrOff) {
    const GgExe::Address p = exe.dsFarPtr(ptrOff);
    if (p.isNull()) return {};
    return text::asBytes(exe.dsString(exe.dsOffsetOf(p)));
}

}  // namespace

MenuBarDef MenuBarDef::fromExe(const GgExe& exe, uint16_t dsOff) {
    MenuBarDef bar;
    const int count = exe.dsS16(dsOff);
    const GgExe::Address table = exe.dsFarPtr(static_cast<uint16_t>(dsOff + 2));
    if (count <= 0 || table.isNull()) return bar;
    const uint16_t first = exe.dsOffsetOf(table);
    for (int i = 0; i < count; ++i) {
        const auto rec = static_cast<uint16_t>(first + kMenuDefSize * static_cast<size_t>(i));
        MenuDef m;
        m.title = farText(exe, rec);
        m.titleWidth = exe.dsS16(static_cast<uint16_t>(rec + 0x04));
        m.itemCount = exe.dsS16(static_cast<uint16_t>(rec + 0x06));
        m.items = farText(exe, static_cast<uint16_t>(rec + 0x08));
        const GgExe::Address handlers = exe.dsFarPtr(static_cast<uint16_t>(rec + 0x0C));
        if (!handlers.isNull()) {
            const uint16_t h = exe.dsOffsetOf(handlers);
            for (int k = 0; k < m.itemCount; ++k)
                m.handlers.push_back(menuCommandOf(exe, exe.dsFarPtr(static_cast<uint16_t>(h + 4 * k))));
        } else {
            m.handlers.assign(static_cast<size_t>(std::max(m.itemCount, 0)), MenuCommand::None);
        }
        m.dropWidth = exe.dsS16(static_cast<uint16_t>(rec + 0x10));
        m.titleX = exe.dsS16(static_cast<uint16_t>(rec + 0x12));
        bar.menus.push_back(std::move(m));
    }
    return bar;
}

MenuBarDef MenuBarDef::fromExe(const GgExe& exe) { return fromExe(exe, dg::kMenuBar); }

MenuBarPictures MenuBarPictures::load(const Archive& archive) {
    MenuBarPictures p;
    p.bundle = Bundle::load(archive, kMenuBarPictures);
    for (const BundleMember& m : p.bundle.members()) p.pictures.push_back(m.payload);
    if (p.pictures.size() < 6) throw FormatError("menu bar bundle 1 has fewer than 6 pictures");
    return p;
}

std::span<const uint8_t> MenuBarPictures::title(int i, bool highlighted) const {
    const size_t k = static_cast<size_t>(highlighted ? i + 3 : i);
    return k < pictures.size() ? pictures[k] : std::span<const uint8_t>{};
}

// 40a4:04db Menu_DrawBar
void menuDrawBar(const UiCanvas& ui, const MenuBarDef& bar, const MenuBarPictures& pics, int sel) {
    Screen& s = ui.screen;
    const uint8_t saved = s.drawColor();
    s.setDrawColor(0);
    s.fillRect(0, 0, 320, 9);
    for (int i = bar.count() - 1; i >= 0; --i)
        s.lpxDrawKeyed(bar.menus[static_cast<size_t>(i)].titleX, 0, pics.title(i, i == sel), 0);
    ui.dirtyFlush();
    s.setDrawColor(saved);
}

// 40a4:0000 Menu_SetMenuBar(NULL): the black strip
void menuClearBar(const UiCanvas& ui) {
    ui.screen.setDrawColor(0);
    ui.screen.fillRect(0, 0, 320, 9);
}

// 40a4:056c Menu_DrawDropDown: the descriptor
DialogDesc menuDropDownDesc(const DialogStrings& strings, const MenuDef& menu) {
    DialogDesc d;
    d.body = menu.items;
    d.footer = strings.footerGoBack;  // DS:3e38
    d.align = DlgAlign::Centre;
    d.x = menu.titleX;
    d.y = 9;
    d.bodyW = menu.dropWidth;
    d.bodyH = menu.itemCount * 10 - 10;
    d.flags = kDlgCompact;
    return d;
}

// 40a4:056c Menu_DrawDropDown
DialogLayout menuDrawDropDown(const UiCanvas& ui, const DialogStrings& strings, const MenuDef& menu,
                              SavedImage* saveUnder) {
    return dlgDraw(ui, menuDropDownDesc(strings, menu), saveUnder);
}

// 40a4:020b Menu_RunDropDown's highlight
void menuToggleRow(const UiCanvas& ui, const DialogLayout& dropDown, int row) {
    ui.screen.xorRect(dropDown.x + 4, 12 + 10 * row, dropDown.innerW, 10);
    ui.dirtyFlush();
}

// 40a4:0000 Menu_SetMenuBar: the title zones
std::vector<Zone> menuTitleZones(const MenuBarDef& bar) {
    std::vector<Zone> zones;
    for (int i = 0; i < bar.count(); ++i) {
        const MenuDef& m = bar.menus[static_cast<size_t>(i)];
        zones.push_back(Zone::rect(static_cast<int16_t>(m.titleX), 0, static_cast<int16_t>(m.titleWidth + 2), 9, 8, 0,
                                   static_cast<uint16_t>(kMenuTitleCodeBase + i)));
    }
    return zones;
}

// 40a4:00ee Menu_AddItemZones
std::vector<Zone> menuItemZones(const MenuDef& menu, const DialogLayout& dropDown) {
    std::vector<Zone> zones;
    zones.push_back(Zone::rect(0, 12, 320, 188, 20, 0, kMenuOutsideCode));
    const auto x = static_cast<int16_t>(menu.titleX + 4);
    const auto w = static_cast<int16_t>(dropDown.innerW);
    for (int r = 0; r < menu.itemCount; ++r) {
        const auto code = static_cast<uint16_t>(kMenuItemCodeBase + r);
        zones.push_back(Zone::rect(x, static_cast<int16_t>(13 + 10 * r), w, 10, 21, code, code));
    }
    zones.push_back(Zone::rect(x, static_cast<int16_t>(menu.itemCount * 10 + 15), w, 11, 21, 0, 0x1B));
    return zones;
}

}  // namespace tmport::gg
