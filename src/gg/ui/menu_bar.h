#pragma once
// The menu bar (ovl31 40a4; re/gg/docs/code_ui.md §6): its data model read from DGROUP, the bar of
// picture titles, the drop-downs (dialog boxes without buttons), the item highlight and the click
// zones. The drop-down loop (Menu_RunDropDown 40a4:020b: keys, sound pause, handler calls) is the
// game's (wave 3); MenuBarDef keeps what it needs.
//
// Data (code_ui.md §6.1; the record layout of TM Windows' menus, re/docs/dgroup_map.md §4.12):
//   MenuBarDef {u16 count, far MenuDef[count]}: g_menuBar DS:4053 = {3, DS:4017}
//   MenuDef (0x14 bytes): +0 far title (a padded string, not drawn: the bar shows pictures), +4 title
//   width (the title zone is 2 wider), +6 item count, +8 far items (LF-separated: the drop-down body),
//   +0xc far handler table (far code pointers to overlay stub entries; NULL allowed), +0x10 drop-down
//   width (the body's wrap width), +0x12 title x.
//   Record i is menu i: 0 Help (x 108), 1 File (x 0), 2 Options (x 54); F1/F2/F3 and Alt+H/F/O.
// Bar pictures: bundle 1, 6 pictures 53 x 9: members 0..2 the titles in record order, 3..5 the same
// highlighted.

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "gg/data/resources.h"
#include "gg/input/zones.h"
#include "gg/ui/dialog.h"
#include "gg/ui/menu_commands.h"
#include "gg/ui/ui_canvas.h"
#include "text/text_engine.h"

namespace tmport::gg {

class GgGame;

class Archive;
class GgExe;

struct MenuDef {
    text::Bytes title;               // +0x00 (not drawn)
    int titleWidth = 0;              // +0x04: title zone width - 2
    int itemCount = 0;               // +0x06 (at most 8: 10 zones per drop-down)
    text::Bytes items;               // +0x08: LF-separated
    std::vector<MenuCommand> handlers;  // +0x0c: one per item (MenuCommand::None for a NULL pointer)
    int dropWidth = 0;               // +0x10
    int titleX = 0;                  // +0x12
};

struct MenuBarDef {
    std::vector<MenuDef> menus;

    // The MenuBarDef at DS:off of ssg.exe (default g_menuBar DS:4053) and its records, strings and
    // handler tables. Throws std::out_of_range for pointers outside DGROUP.
    static MenuBarDef fromExe(const GgExe& exe, uint16_t dsOff);
    static MenuBarDef fromExe(const GgExe& exe);
    int count() const { return static_cast<int>(menus.size()); }
};

// Menu indices (the F1..F3 / Alt+H, F, O order, code_runtime.md §9.3).
inline constexpr int kMenuHelp = 0;
inline constexpr int kMenuFile = 1;
inline constexpr int kMenuOptions = 2;
inline constexpr uint16_t kMenuBarPictures = 0x0001;  // bundle 1

// The six bar pictures (Res_Read(1) at every Menu_DrawBar; the engine keeps one copy).
struct MenuBarPictures {
    Bundle bundle;
    std::vector<std::span<const uint8_t>> pictures;  // members 0..5, into `bundle` (move-only)

    static MenuBarPictures load(const Archive& archive);
    MenuBarPictures() = default;
    MenuBarPictures(MenuBarPictures&&) noexcept = default;
    MenuBarPictures& operator=(MenuBarPictures&&) noexcept = default;
    MenuBarPictures(const MenuBarPictures&) = delete;
    MenuBarPictures& operator=(const MenuBarPictures&) = delete;
    std::span<const uint8_t> title(int i, bool highlighted) const;
};

// 40a4:04db Menu_DrawBar(sel): colour 0 FillRect(0, 0, 320, 9); for i = count - 1 down to 0: the title
// picture i (i + 3 when i == sel) keyed at (record i's x, 0); Dirty_Flush. g_drawColor is kept.
void menuDrawBar(const UiCanvas& ui, const MenuBarDef& bar, const MenuBarPictures& pics, int sel);
// 40a4:0000 Menu_SetMenuBar(NULL), the drawing part: colour 0 FillRect(0, 0, 320, 9) (the strip goes
// black; no flush). g_drawColor is left at 0.
void menuClearBar(const UiCanvas& ui);

// 40a4:056c Menu_DrawDropDown(rec): Dlg_Draw with the save-under of a descriptor {title NULL, body =
// items, footer "Esc to Go Back", no buttons, centred, x = title x, y 9, body width = drop-down width,
// body height = count * 10 - 10, flags compact}: the box is (title x, 9), H = count * 10 + 24, item r on
// the line at y 13 + 10 r.
DialogDesc menuDropDownDesc(const DialogStrings& strings, const MenuDef& menu);
DialogLayout menuDrawDropDown(const UiCanvas& ui, const DialogStrings& strings, const MenuDef& menu,
                              SavedImage* saveUnder);
// The drop-down's item highlight (40a4:020b): Gfx_XorRect(x + 4, 12 + 10 row, g_dlgInnerW, 10) (^0x0F:
// black text on white becomes white on black), then Dirty_Flush. Toggling the same row again undoes
// it.
void menuToggleRow(const UiCanvas& ui, const DialogLayout& dropDown, int row);

// 40a4:0000 Menu_SetMenuBar(bar), the zone part: per record i the global zone {x = title x, y 0, w =
// title width + 2, h 9, priority 8, press 0, release 0x13B + i} (a release over a title is its F key).
inline constexpr uint16_t kMenuTitleCodeBase = 0x013B;
std::vector<Zone> menuTitleZones(const MenuBarDef& bar);

// 40a4:00ee Menu_AddItemZones(rec) (10 zones of 22 bytes, Mem_Alloc(220)): zone 0 = (0, 12, 320, 188)
// priority 20, press 0, release 0x44F (a click outside: ignored); item r = (x + 4, 13 + 10 r, innerW, 10)
// priority 21, press = release = 0xFD0 + r; last = (x + 4, count * 10 + 15, innerW, 11) priority 21,
// press 0, release 0x1B (the footer: Esc). Added to the local list in this order.
inline constexpr uint16_t kMenuOutsideCode = 0x044F;
inline constexpr uint16_t kMenuItemCodeBase = 0x0FD0;
std::vector<Zone> menuItemZones(const MenuDef& menu, const DialogLayout& dropDown);


// ---- wave 3: the run (owner: ui agent; ui/menu_run.cpp) ---------------------------------------------
// 40a4:0000 Menu_SetMenuBar(bar): install (true: g_menuBar DS:4053) draws the bar, sets the menu hook
// (Input_GetCode's F1..F3 / Alt+H, F, O: menuRunDropDown) and adds the title zones (global); remove
// (false) clears the hook, removes the zones, g_menuBarCur = DS:405a and blacks the strip. The
// MenuBarDef, its pictures and the zones are UiObjects'.
void menuSetMenuBar(GgGame& g, bool install);
// 40a4:020b Menu_RunDropDown(i): the drop-down loop of code_ui.md §6.4 (Snd_Pause, the hook cleared,
// play mode 0, Mouse_ForceDefault(1), Zones_Push at the first nesting level g_menuNesting, Up / Down /
// Left / Right / F keys / clicks, a handler's non-zero result reopens the menu; time runs).
void menuRunDropDown(GgGame& g, int index);

}  // namespace tmport::gg
