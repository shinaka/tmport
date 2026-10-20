# G&G sources for area "ui3" (wave 3, the ui agent): the TLC UI library's runs on the game (Dlg_RunModal
# and the boxes, Menu_SetMenuBar / Menu_RunDropDown, the menu commands and the help topics, TextView_Run,
# the sign-in, the hints), PLAYERS.DAT on the game, the UI's engine-side objects. The wave 2 files of
# src/gg/ui stay in cmake/gg_sources_ui.cmake (their wave 3 owner: re/gg/docs/ENGINE_MAP.md).
target_sources(ggcore PRIVATE
    src/gg/ui/ui_objects.h
    src/gg/ui/ui_run.h
    src/gg/ui/players.h
    src/gg/ui/players.cpp
    src/gg/ui/dialog_run.cpp
    src/gg/ui/menu_run.cpp
    src/gg/ui/commands.cpp
    src/gg/ui/text_view_run.cpp
    src/gg/ui/signin_run.cpp
    src/gg/ui/hints_run.cpp
)
