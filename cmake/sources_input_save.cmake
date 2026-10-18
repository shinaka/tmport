# sources for area "input_save". Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
target_sources(tmcore PRIVATE
    src/input/event_translator.cpp
    src/input/cursors.cpp
    src/platform/engine_setup.cpp
    src/save/user_record.cpp
    src/save/users_file.cpp
    src/save/settings.cpp
)
