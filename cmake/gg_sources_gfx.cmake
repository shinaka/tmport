# G&G sources for area "gfx": the screen buffer and primitives, dirty tiles, sprites, the software
# cursor, the palette and fades, transitions, the movie (src/gg/gfx), the fonts and the text renderer
# (src/gg/text) and the graphics tools (gfx_cli).
target_sources(ggcore PRIVATE
    src/gg/gfx/lpx.h
    src/gg/gfx/lpx.cpp
    src/gg/gfx/screen.h
    src/gg/gfx/screen.cpp
    src/gg/gfx/sprites.h
    src/gg/gfx/sprites.cpp
    src/gg/gfx/cursor.h
    src/gg/gfx/cursor.cpp
    src/gg/gfx/palette.h
    src/gg/gfx/palette.cpp
    src/gg/gfx/video.h
    src/gg/gfx/video.cpp
    src/gg/gfx/transitions.h
    src/gg/gfx/transitions.cpp
    src/gg/gfx/movie.h
    src/gg/gfx/movie.cpp
    src/gg/gfx/gfx_cli.h
    src/gg/gfx/gfx_cli.cpp
    src/gg/text/font.h
    src/gg/text/font.cpp
    src/gg/text/text.h
    src/gg/text/text.cpp
)
