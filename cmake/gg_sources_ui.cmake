# G&G sources for area "ui": the TLC UI library of Gizmos & Gadgets (src/gg/ui): dialogs (ovl27
# 3f56), buttons (ovl28 4034), the scroll bar (ovl30 406a), the menu bar and its commands (ovl31
# 40a4, ovl32 4106), help topics (ovl33 411d), the text viewer (ovl34 4140), the attract (ovl06
# 2c11), the sign-in (ovl07 2caa), hints (ovl12 3060), the rank helpers (ovl04 2bd4) and the UI
# tools (ui_cli; re/gg/docs/code_ui.md).
target_sources(ggcore PRIVATE
    src/gg/ui/ui_canvas.h
    src/gg/ui/rank.h
    src/gg/ui/rank.cpp
    src/gg/ui/button.h
    src/gg/ui/button.cpp
    src/gg/ui/dialog.h
    src/gg/ui/dialog.cpp
    src/gg/ui/scroll_bar.h
    src/gg/ui/scroll_bar.cpp
    src/gg/ui/menu_commands.h
    src/gg/ui/menu_commands.cpp
    src/gg/ui/menu_bar.h
    src/gg/ui/menu_bar.cpp
    src/gg/ui/help.h
    src/gg/ui/help.cpp
    src/gg/ui/hints.h
    src/gg/ui/hints.cpp
    src/gg/ui/text_viewer.h
    src/gg/ui/text_viewer.cpp
    src/gg/ui/attract.h
    src/gg/ui/attract.cpp
    src/gg/ui/attract_anims.h
    src/gg/ui/attract_anims.cpp
    src/gg/ui/signin.h
    src/gg/ui/signin.cpp
    src/gg/ui/ui_cli.h
    src/gg/ui/ui_cli.cpp
)
