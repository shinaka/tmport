# G&G sources for area "world3" (wave 3, the world agent): states 4, 5, 7 and 8, the new visit, the door
# and score helpers, the rooms, the parts and the HUD on the game. The wave 2 files of the world stay in
# cmake/gg_sources_world.cmake (their wave 3 owner: re/gg/docs/ENGINE_MAP.md).
target_sources(ggcore PRIVATE
    src/gg/game/world.h
    src/gg/game/world.cpp
    src/gg/game/world_rooms.cpp
    src/gg/game/world_parts.cpp
    src/gg/game/hud_run.cpp
)
