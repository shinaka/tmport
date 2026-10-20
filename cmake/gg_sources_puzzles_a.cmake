# G&G sources for area "puzzles_a" (wave 3): state 9 and the puzzle framework (38e2), the DAM engine
# (3d39, 3797:100e), and the runs of electric circuits, simple machines, launch and energy forms.
target_sources(ggcore PRIVATE
    src/gg/game/puzzle_run.cpp
    src/gg/game/dam.h
    src/gg/game/dam.cpp
    src/gg/game/pz_electric_run.cpp
    src/gg/game/pz_simple_run.cpp
    src/gg/game/pz_launch_run.cpp
    src/gg/game/pz_energy_run.cpp
)
