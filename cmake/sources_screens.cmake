# sources for area "screens". Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
target_sources(tmcore PRIVATE
    src/game/screens_common.cpp
    src/game/title.cpp
    src/game/signin.cpp
    src/game/screens_cli.cpp
)
