# sources for area "text_ui". Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
target_sources(tmcore PRIVATE
    src/text/font.cpp
    src/text/text_renderer.cpp
    src/ui/dialog_desc.cpp
    src/ui/game_dialog.cpp
    src/ui/help_topics.cpp
)
target_sources(tmcore PRIVATE src/cli/text_ui_commands.cpp)
