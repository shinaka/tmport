# G&G tests for area "data". Add files with:
# target_sources(ggtests PRIVATE tests/gg/test_file.cpp ...)
target_sources(ggtests PRIVATE
    tests/gg/gg_test_data.cpp
    tests/gg/test_gg_data.cpp
    tests/gg/test_gg_save.cpp
)
