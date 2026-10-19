# tests for area "dos_data". Add files with:
# target_sources(tmtests PRIVATE path/to/file.cpp ...)
target_sources(tmtests PRIVATE
    tests/dos_test_data.cpp
    tests/test_dos_exe.cpp
    tests/test_dos_data.cpp
)
