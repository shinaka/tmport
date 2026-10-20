# G&G sources for area "actors" (wave 3): the actor system on the game (1834), the player and the
# projectile (18f5), collision and contacts (1adb), the Cyber Chimps (2a80).
target_sources(ggcore PRIVATE
    src/gg/game/actors_run.cpp
    src/gg/game/player.h
    src/gg/game/player.cpp
    src/gg/game/collision.h
    src/gg/game/collision.cpp
    src/gg/game/chimps.h
    src/gg/game/chimps.cpp
)
