# sources for area "room". Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
target_sources(tmcore PRIVATE
    src/game/room/room_tables.cpp
    src/game/room/player.cpp
    src/game/room/hud.cpp
    src/game/room/elves.cpp
    src/game/room/room_scene.cpp
    src/game/room/room_run.cpp
    src/game/room/room_cli.cpp
)
# The headless room-loop driver (--room-run, room_cli.h) is dispatched from tmport's CLI.
