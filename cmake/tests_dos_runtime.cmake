# tests for area "dos_runtime". Add files with:
# target_sources(tmtests PRIVATE path/to/file.cpp ...)
target_sources(tmtests PRIVATE
    tests/test_dos_save.cpp
    tests/test_dos_input.cpp
    tests/test_dos_runtime.cpp
    tests/test_dos_preview.cpp
)
