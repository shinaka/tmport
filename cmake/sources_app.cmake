# sources for area "app". Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
target_sources(tmcore PRIVATE
    src/ui/menu_bar.cpp
    src/app/engine_glue.cpp
    src/app/menu_commands.cpp
    src/app/game_main.cpp
    src/app/launch_options.cpp
    src/app/script_input.cpp
    src/app/bootstrap.cpp
    src/platform/console.cpp
    src/platform/exe_icon.cpp
)
