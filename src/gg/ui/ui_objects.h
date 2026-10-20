#pragma once
// The UI module's engine-side objects (GgGame::uiObjects): what the TLC UI library keeps across calls
// in the game: the menu bar's records and pictures with the title zones, the drop-down's zones and
// save-under, the sign-in screen while state 2 runs. The original keeps them in DGROUP or the shared
// resource buffer (code_ui.md §2.3); the engine gives each its own buffer. The globals the UI writes
// (g_menuBarCur, g_menuNesting, the dialog layout, the text viewer's scroll words, the sign-in's
// variables, the hint queue) are GgState::ui's.
//
// Owner: ui agent.

#include <memory>
#include <optional>
#include <vector>

#include "gg/gfx/screen.h"
#include "gg/input/zones.h"
#include "gg/ui/dialog.h"
#include "gg/ui/menu_bar.h"
#include "gg/ui/signin.h"

namespace tmport::gg {

struct UiObjects {
    // ---- the menu bar (40a4, code_ui.md §6)
    // g_menuBar DS:4053's records (read from ssg.exe at the first Menu_SetMenuBar) and the six title
    // pictures of bundle 1 (Res_Read(1) at every Menu_DrawBar in the original).
    std::optional<MenuBarDef> menuBar;
    std::optional<MenuBarPictures> menuPictures;
    // g_menuTitleZones DS:bc2e: one global zone per title while the bar is installed (titleZonesLinked;
    // the vector is not resized while they are linked).
    std::vector<Zone> titleZones;
    bool titleZonesLinked = false;
    // The open drop-down: g_menuItemZones DS:bc2a (Mem_Alloc(220): the outside zone, the item rows, the
    // footer; local while linked), the layout Menu_DrawDropDown's Dlg_Draw wrote and its save-under
    // (g_resBuffer in the original).
    std::vector<Zone> itemZones;
    bool itemZonesLinked = false;
    DialogLayout dropDown;
    SavedImage dropDownSave;

    // ---- the sign-in (2caa, code_ui.md §11): the screen from SignIn_LoadButtons to SignIn_Free, bound
    // to GgState::ui.
    std::unique_ptr<SignIn> signIn;
};

}  // namespace tmport::gg
