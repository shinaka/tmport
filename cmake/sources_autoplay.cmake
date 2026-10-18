# sources for area "autoplay". Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
# The autoplayer / soak test (tmport --autoplay, src/tools/autoplay/autoplay.h).
target_sources(tmcore PRIVATE
    src/tools/autoplay/autoplay_rig.cpp
    src/tools/autoplay/autoplay_vision.cpp
    src/tools/autoplay/autoplay_checks.cpp
    src/tools/autoplay/autoplay_bot.cpp
    src/tools/autoplay/autoplay.cpp
)
