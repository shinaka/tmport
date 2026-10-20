# G&G sources for area "integration": the display on the runtime (src/gg/app/display: Video driven by
# GgRuntime's yield points and clocks, the mouse handler's MouseScreen) and the wave 2 preview
# (src/gg/app/preview: replaced by the game modules in wave 3).
target_sources(ggcore PRIVATE
    src/gg/app/display.h
    src/gg/app/display.cpp
    src/gg/app/preview.h
    src/gg/app/preview.cpp
)
