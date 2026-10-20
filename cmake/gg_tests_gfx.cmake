# G&G tests for area "gfx": the screen, blitters, dirty tiles, sprites, cursor, palette, fades,
# wipes and the movie (test_gg_gfx.cpp), the fonts and the text renderer (test_gg_text.cpp).
target_sources(ggtests PRIVATE
    tests/gg/test_gg_gfx.cpp
    tests/gg/test_gg_text.cpp
)
