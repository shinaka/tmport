# sources for area "scenes". Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
target_sources(tmcore PRIVATE
    src/game/scenes/scene_support.cpp
    src/game/scenes/quiz_screen.cpp
    src/game/scenes/hint_popup.cpp
    src/game/scenes/level_cutscene.cpp
    src/game/scenes/celebration.cpp
    src/game/scenes/castle_climb.cpp
    src/game/scenes/scene_hud.cpp
    src/game/scenes/scenes_cli.cpp
)
