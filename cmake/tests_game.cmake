# tests for area "game". Add files with:
# target_sources(tmtests PRIVATE path/to/file.cpp ...)
target_sources(tmtests PRIVATE
    tests/test_game_rules.cpp
    tests/test_game_quiz.cpp
    tests/test_game_layout.cpp
    tests/test_game_record.cpp
    tests/test_game_main.cpp
)
