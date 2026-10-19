# tests for area "mods". Add files with:
# target_sources(tmtests PRIVATE path/to/file.cpp ...)
target_sources(tmtests PRIVATE
    tests/test_mods_formats.cpp
    tests/test_mods_game.cpp
    tests/test_dos_mods.cpp
)
