# tests for area "app". Add files with:
# target_sources(tmtests PRIVATE path/to/file.cpp ...)
target_sources(tmtests PRIVATE
    tests/test_menu_bar.cpp
    tests/test_app.cpp
    tests/test_platform.cpp
)
