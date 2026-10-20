# G&G sources for area "skeleton" (wave 3): the game state and its DGROUP codec (src/gg/game/game_state),
# the game context (src/gg/game/game), the sound facade with its recording double (src/gg/game/sound), the
# per-frame trace record (src/gg/game/trace) and the DOSBox-X state reader of ssg.exe
# (src/gg/save/gg_state_file). The module areas of wave 3 list their files in cmake/gg_sources_<flow|ui3|
# world3|actors|puzzles_a|puzzles_b|vehicles>.cmake (re/gg/docs/ENGINE_MAP.md, "Wave 3 map").
target_sources(ggcore PRIVATE
    src/gg/game/game_state.h
    src/gg/game/game_state.cpp
    src/gg/game/game.h
    src/gg/game/game.cpp
    src/gg/game/sound.h
    src/gg/game/sound.cpp
    src/gg/game/trace.h
    src/gg/game/trace.cpp
    src/gg/save/gg_state_file.h
    src/gg/save/gg_state_file.cpp
)
