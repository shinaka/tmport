// The menu bar on the game (menu_bar.h, "wave 3: the run"; code_ui.md §6.2, §6.4). Owner: ui agent.
//
// The bar exists in the warehouse rooms, the workshop and the puzzles (Menu_SetMenuBar's callers). Its
// drop-down is entered from Input_GetCode through the menu hook (F1..F3, Alt+H/F/O, a release over a
// title) and runs a busy poll; time runs (no freeze), the sound is paused (not nestable).
#include "gg/ui/menu_bar.h"

#include "gg/data/dgroup_offsets.h"
#include "gg/game/game.h"
#include "gg/ui/menu_commands.h"
#include "gg/ui/ui_objects.h"
#include "gg/ui/ui_run.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

// The records and pictures of g_menuBar DS:4053 (read once).
void loadBar(GgGame& g) {
    UiObjects& o = g.uiObjects();
    if (!o.menuBar) o.menuBar = MenuBarDef::fromExe(g.exe(), dg::kMenuBar);
    if (!o.menuPictures) o.menuPictures = MenuBarPictures::load(g.archive());
}

// g_menuBarCur DS:bc70: the installed bar, or none (DS:405a {0, NULL}, or a NULL pointer before the
// first Menu_SetMenuBar: both have no menus).
const MenuBarDef* currentBar(GgGame& g) {
    UiObjects& o = g.uiObjects();
    if (g.ui.menuBarCur.off != dg::kMenuBar) return nullptr;
    loadBar(g);
    return &*o.menuBar;
}

void unlinkTitleZones(GgGame& g) {
    UiObjects& o = g.uiObjects();
    if (!o.titleZonesLinked) return;
    for (Zone& z : o.titleZones) g.zones().removeGlobal(&z);  // Zones_RemoveGlobal
    o.titleZonesLinked = false;
}

void linkTitleZones(GgGame& g) {
    UiObjects& o = g.uiObjects();
    if (o.titleZonesLinked) {
        // The original would link the records a second time (a cycle in the global list); no caller
        // installs the bar twice without removing it.
        log::warn("Menu_SetMenuBar: the bar is already installed; its title zones are relinked");
        unlinkTitleZones(g);
    }
    o.titleZones = menuTitleZones(*o.menuBar);  // Zone_Init per record
    for (Zone& z : o.titleZones) g.zones().addGlobal(z);  // Zones_AddGlobal
    o.titleZonesLinked = true;
}

// 40a4:00ee Menu_AddItemZones
void addItemZones(GgGame& g, const MenuDef& menu) {
    UiObjects& o = g.uiObjects();
    o.itemZones = menuItemZones(menu, o.dropDown);
    for (Zone& z : o.itemZones) g.zones().addLocal(z);
    o.itemZonesLinked = true;
}

// 40a4:01c7 Menu_RemoveItemZones (nothing when they are gone already: g_menuItemZones is NULL then)
void removeItemZones(GgGame& g) {
    UiObjects& o = g.uiObjects();
    if (!o.itemZonesLinked) return;
    for (Zone& z : o.itemZones) g.zones().removeLocal(&z);
    o.itemZonesLinked = false;
}

}  // namespace

void installMenuHook(GgGame& g) {
    g.input().setMenuHandler([&g](int index) { menuRunDropDown(g, index); });
}

// 40a4:0000 Menu_SetMenuBar
void menuSetMenuBar(GgGame& g, bool install) {
    const UiCanvas ui = g.canvas();
    if (install) {
        loadBar(g);
        UiObjects& o = g.uiObjects();
        g.ui.menuBarCur.off = dg::kMenuBar;
        menuDrawBar(ui, *o.menuBar, *o.menuPictures, -1);  // 40a4:04db Menu_DrawBar(-1)
        installMenuHook(g);                                // g_menuHandler = 1e94:0025
        linkTitleZones(g);
        return;
    }
    g.input().setMenuHandler({});
    unlinkTitleZones(g);
    g.ui.menuBarCur.off = dg::kMenuBarNone;
    menuClearBar(ui);  // colour 0 FillRect(0, 0, 320, 9)
}

// 40a4:020b Menu_RunDropDown
void menuRunDropDown(GgGame& g, int index) {
    const MenuBarDef* bar = currentBar(g);
    if (index < 0 || !bar || index >= bar->count()) return;
    UiObjects& o = g.uiObjects();
    const UiCanvas ui = g.canvas();

    // The entry: the hook cleared, Snd_Pause, the arrow forced, play mode 0, Input_Flush, the local
    // zones pushed at the first nesting level.
    g.input().setMenuHandler({});
    g.sound.pause();
    const uint16_t prevPlay = g.keyboard().playMode();
    const int prevForce = g.video.cursor().forceDefault(1);
    g.keyboard().setPlayMode(0);
    g.rt.flushInput();
    if (g.ui.menuNesting == 0) g.zones().push();
    ++g.ui.menuNesting;
    OnUnwind unwind([&] {
        removeItemZones(g);
        if (--g.ui.menuNesting <= 0) {
            g.zones().pop();
            g.ui.menuNesting = 0;
        }
        installMenuHook(g);
        g.keyboard().setPlayMode(prevPlay);
        g.video.cursor().forceDefault(prevForce);
    });

    int i = index;  // SI: the open menu, -1 closes
    while (i != -1) {
        const int opened = i;
        const MenuDef& menu = bar->menus[static_cast<size_t>(i)];
        menuDrawBar(ui, *bar, *o.menuPictures, i);
        o.dropDown = menuDrawDropDown(ui, g.strings, menu, &o.dropDownSave);  // 40a4:056c, saving the area
        storeDlgLayout(g.ui, o.dropDown, menuDropDownDesc(g.strings, menu));
        bool restored = false;
        addItemZones(g, menu);
        int row = 0;  // DI: the highlighted item
        menuToggleRow(ui, o.dropDown, row);
        for (bool leaving = false; !leaving;) {
            const int prev = row;
            const uint16_t code = g.rt.getInputCode();  // the busy poll
            const auto choose = [&] {
                // Dlg_RestoreUnder, the bar, the item zones away, then the item's handler: non-zero
                // reopens the same menu (the help pager's Done, an Esc'd Save & Exit), 0 closes it.
                dlgRestoreUnderAt(g, o.dropDownSave);
                restored = true;
                menuDrawBar(ui, *bar, *o.menuPictures, -1);
                removeItemZones(g);
                const MenuCommand c = row < static_cast<int>(menu.handlers.size()) ? menu.handlers[static_cast<size_t>(row)]
                                                                                    : MenuCommand::None;
                if (runMenuCommand(g, c) == 0) i = -1;
                leaving = true;
            };
            switch (code) {  // the table CS:0493
                case 0x0000: break;
                case 0x000D: choose(); break;  // Enter
                case 0x001B:                   // Esc, the footer line
                case 0x0FCF:                   // (no zone or key gives it)
                    i = -1;
                    leaving = true;
                    break;
                case 0x013B: case 0x013C: case 0x013D: case 0x013E: case 0x013F:
                case 0x0140: case 0x0141: case 0x0142: case 0x0143: case 0x0144: {  // F1..F10
                    const int j = code - kMenuTitleCodeBase;
                    if (j >= 0 && j < bar->count() && j != opened) {
                        i = j;
                        leaving = true;
                    }
                    break;
                }
                case 0x0148:  // Up
                    if (--row < 0) row = menu.itemCount - 1;
                    break;
                case 0x0150:  // Down
                    if (++row >= menu.itemCount) row = 0;
                    break;
                case 0x014B:  // Left: the previous menu
                    if (--i < 0) i = bar->count() - 1;
                    leaving = true;
                    break;
                case 0x014D:  // Right: the next menu
                    if (++i >= bar->count()) i = 0;
                    leaving = true;
                    break;
                default: {
                    const uint16_t m = code & 0x0FFF;
                    if (m < kMenuItemCodeBase) break;  // the outside zone's 0x44F and every other code
                    if (code & (code::kFirstPoll | code::kStillHeld)) row = m - kMenuItemCodeBase;  // the highlight follows
                    else choose();  // a release chooses the highlighted row (DI), not the released one
                    break;
                }
            }
            if (!leaving && row != prev) {
                menuToggleRow(ui, o.dropDown, prev);
                menuToggleRow(ui, o.dropDown, row);
            }
        }
        if (!restored) dlgRestoreUnderAt(g, o.dropDownSave);
        removeItemZones(g);
    }
    // The exit: the bar, the hook set to the drop-down, play mode, Input_Flush, the zones popped at the
    // last nesting level, the cursor, Snd_Resume.
    menuDrawBar(ui, *bar, *o.menuPictures, -1);
    installMenuHook(g);
    g.keyboard().setPlayMode(prevPlay);
    g.rt.flushInput();
    if (--g.ui.menuNesting <= 0) {
        g.zones().pop();
        g.ui.menuNesting = 0;
    }
    g.video.cursor().forceDefault(prevForce);
    g.sound.resume();
}

}  // namespace tmport::gg
