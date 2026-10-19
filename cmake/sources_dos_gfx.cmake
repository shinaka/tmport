# sources for area "dos_gfx". Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
target_sources(tmcore PRIVATE
    src/dos/gfx/pan.cpp
    src/dos/gfx/pak.cpp
    src/dos/gfx/palettes.cpp
    src/dos/gfx/back_buffer.cpp
    src/dos/gfx/blitter.cpp
    src/dos/gfx/scenery.cpp
    src/dos/gfx/dos_screens.cpp
    src/dos/gfx/dos_gfx_cli.cpp
    src/dos/text/dos_font.cpp
    src/dos/text/text_draw.cpp
    src/dos/text/ui_draw.cpp
)
