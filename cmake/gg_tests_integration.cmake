# G&G tests for area "integration": the tool dispatcher, the display on the runtime and the wave 2
# preview against the DOSBox-X captures (tests/gg/test_gg_preview.cpp); wave 3: the game played
# through the app from a fresh boot and from the round 2 states (tests/gg/test_gg_playthrough.cpp).
target_sources(ggtests PRIVATE
    tests/gg/test_gg_preview.cpp
    tests/gg/test_gg_playthrough.cpp
)
