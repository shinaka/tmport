# sources for area "game". Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
target_sources(tmcore PRIVATE
    src/game/game_tables.cpp
    src/game/room_table.cpp
    src/game/quiz.cpp
    src/game/layout.cpp
    src/game/game_state.cpp
    src/game/rules.cpp
    src/game/game_main.cpp
)
