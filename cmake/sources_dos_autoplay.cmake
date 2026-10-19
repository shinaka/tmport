# sources for area "dos_autoplay" (wave 5). Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
# The DOS edition's autoplayer / soak test (tmport --edition dos --autoplay, src/dos/autoplay/dos_autoplay.h).
target_sources(tmcore PRIVATE
    src/dos/autoplay/dos_autoplay_rig.cpp
    src/dos/autoplay/dos_autoplay_world.cpp
    src/dos/autoplay/dos_autoplay_vision.cpp
    src/dos/autoplay/dos_autoplay_checks.cpp
    src/dos/autoplay/dos_autoplay_bot.cpp
    src/dos/autoplay/dos_autoplay.cpp
)
