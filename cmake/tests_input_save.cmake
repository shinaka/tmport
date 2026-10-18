# tests for area "input_save". Add files with:
# target_sources(tmtests PRIVATE path/to/file.cpp ...)
target_sources(tmtests PRIVATE
    tests/test_input.cpp
    tests/test_cursors.cpp
    tests/test_timing.cpp
    tests/test_save.cpp
    tests/test_settings.cpp
)
