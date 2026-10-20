# G&G tests for area "runtime". Add files with:
# target_sources(ggtests PRIVATE tests/gg/test_file.cpp ...)
target_sources(ggtests PRIVATE
    tests/gg/test_gg_input.cpp
    tests/gg/test_gg_runtime.cpp
    tests/gg/test_gg_app.cpp
)
