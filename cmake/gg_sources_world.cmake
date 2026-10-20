# G&G sources for area "world": the data and drawing halves of the hub (12e0), the rooms (1622), the
# actors (1834), the screen animations (174b), the part catalogue (153f), the HUD (1a04), the
# picture cache as the world uses it (15df) and the puzzles' screens (ovl13..ovl22), and the world
# tools (re/gg/docs/code_world.md, code_vehicles_races.md §4, code_puzzles_a.md, code_puzzles_b.md).
target_sources(ggcore PRIVATE
    src/gg/game/pic_cache.h
    src/gg/game/pic_cache.cpp
    src/gg/game/screen_anim.h
    src/gg/game/screen_anim.cpp
    src/gg/game/actors.h
    src/gg/game/actors.cpp
    src/gg/game/parts.h
    src/gg/game/parts.cpp
    src/gg/game/rooms.h
    src/gg/game/rooms.cpp
    src/gg/game/hud.h
    src/gg/game/hud.cpp
    src/gg/game/building.h
    src/gg/game/building.cpp
    src/gg/game/blueprint.h
    src/gg/game/blueprint.cpp
    src/gg/game/hub.h
    src/gg/game/hub.cpp
    src/gg/game/puzzles.h
    src/gg/game/puzzles.cpp
    src/gg/game/pz_electric.h
    src/gg/game/pz_electric.cpp
    src/gg/game/pz_simple.h
    src/gg/game/pz_simple.cpp
    src/gg/game/pz_pieces.h
    src/gg/game/pz_pieces.cpp
    src/gg/game/pz_gears.h
    src/gg/game/pz_gears.cpp
    src/gg/game/pz_balance.h
    src/gg/game/pz_balance.cpp
    src/gg/game/pz_launch.h
    src/gg/game/pz_launch.cpp
    src/gg/game/pz_energy.h
    src/gg/game/pz_energy.cpp
    src/gg/game/pz_magnets.h
    src/gg/game/pz_magnets.cpp
    src/gg/game/world_cli.h
    src/gg/game/world_cli.cpp
)
