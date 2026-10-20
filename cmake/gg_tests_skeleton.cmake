# G&G tests for area "skeleton" (wave 3): the game fixture every wave 3 area's tests use
# (tests/gg/gg_game_fixture.h) and the skeleton's own tests.
target_sources(ggtests PRIVATE
    tests/gg/gg_game_fixture.h
    tests/gg/gg_game_fixture.cpp
    tests/gg/test_gg_game_skeleton.cpp
)
