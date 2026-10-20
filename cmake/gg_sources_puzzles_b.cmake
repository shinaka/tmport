# G&G sources for area "puzzles_b" (wave 3): the runs of machine pieces, gears, balance and magnets, and
# the puzzles' pointer warp on a windowed host.
target_sources(ggcore PRIVATE
    src/gg/game/pz_pieces_run.cpp
    src/gg/game/pz_gears_run.cpp
    src/gg/game/pz_balance_run.cpp
    src/gg/game/pz_magnets_run.cpp
    src/gg/game/pointer_warp.h
    src/gg/game/pointer_warp.cpp
    src/gg/game/pz_restore.h
)
