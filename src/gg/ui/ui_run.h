#pragma once
// Helpers the UI library's run files share (ui/dialog_run.cpp, menu_run.cpp, commands.cpp,
// text_view_run.cpp, signin_run.cpp, hints_run.cpp): the clean-up of a modal loop that an exception
// leaves (the test fixture's stop, GgRunEnd, exit's GgExit thrown from a yield point inside it), the
// dialog layout globals, Dlg_RestoreUnder at g_dlgX / g_dlgY, the menu hook. Internal to src/gg/ui.
//
// Owner: ui agent.

#include <exception>
#include <utility>

#include "gg/ui/dialog.h"
#include "gg/ui/ui_canvas.h"

namespace tmport::gg {

class GgGame;
struct UiState;

// Runs `fn` when the scope is left by an exception only (the normal exit is the loop's own code, in the
// original's order). Used to unlink click zones that live in a loop's stack frame and to put the hooks,
// the cursor and the play mode back, so that nothing points into a frame that is gone.
template <class F>
class OnUnwind {
public:
    explicit OnUnwind(F fn) : fn_(std::move(fn)), exceptions_(std::uncaught_exceptions()) {}
    ~OnUnwind() {
        if (std::uncaught_exceptions() > exceptions_) fn_();
    }
    OnUnwind(const OnUnwind&) = delete;
    OnUnwind& operator=(const OnUnwind&) = delete;

private:
    F fn_;
    int exceptions_;
};

// The layout globals Dlg_Layout writes (g_dlgX DS:bc26 .. g_dlgPicY DS:bbb8): the title's
// only with a title, the footer's x only with a footer, the pictures' only with pictures (the others
// keep their last values, as in the original).
void storeDlgLayout(UiState& ui, const DialogLayout& layout, const DialogDesc& desc);

// 3f56:0367 Dlg_RestoreUnder: Gfx_PutImage(g_dlgX, g_dlgY, saved), Dirty_Flush. The original restores
// the save-under of the shared buffer at the position of the last layout.
void dlgRestoreUnderAt(GgGame& g, const SavedImage& saved);

// g_menuHandler's value while a bar is installed: the stub entry 1e94:0025 of Menu_RunDropDown.
void installMenuHook(GgGame& g);

}  // namespace tmport::gg
