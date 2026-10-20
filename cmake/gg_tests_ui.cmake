# G&G tests for area "ui": the TLC UI library (src/gg/ui) against the specs' rules and, with
# GG_DATA / GG_CAPTURES, the DOSBox-X reference frames (tests/gg/test_gg_ui.cpp).
target_sources(ggtests PRIVATE
    tests/gg/test_gg_ui.cpp
)
