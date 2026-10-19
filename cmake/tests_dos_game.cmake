# tests for area "dos_game" (wave 3). One line per file; add your files, never reorder or remove the
# lines of others. Add files with:
# target_sources(tmtests PRIVATE path/to/file.cpp ...)
target_sources(tmtests PRIVATE
    tests/dos_game_fixture.cpp
    tests/test_dos_game_skeleton.cpp
    tests/test_dos_mainloop.cpp
    tests/test_dos_world_captures.cpp
    tests/test_dos_layout.cpp
    tests/test_dos_elves.cpp
    tests/test_dos_hints.cpp
    tests/test_dos_world.cpp
    tests/test_dos_ui.cpp
    tests/test_dos_player.cpp
    tests/test_dos_player_end.cpp
    tests/test_dos_signin.cpp
    tests/test_dos_quiz.cpp
    tests/test_dos_playthrough.cpp
)
