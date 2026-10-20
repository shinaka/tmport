# G&G sources for area "runtime". Add files with:
# target_sources(ggcore PRIVATE src/gg/<module>/file.cpp ...)
# The input models (src/gg/input: keyboard 1454/198d/1d3c, mouse 17a0, zones 17ab, input codes 198d)
# and the runtime and app shell (src/gg/app: clock 1454, timers 152c, Seq_Run 12e0, the runtime's
# yield points, options and ggport.ini, the icon, the test pattern, the tool dispatcher).
target_sources(ggcore PRIVATE
    src/gg/input/keyboard.h
    src/gg/input/keyboard.cpp
    src/gg/input/mouse.h
    src/gg/input/mouse.cpp
    src/gg/input/zones.h
    src/gg/input/zones.cpp
    src/gg/input/input_codes.h
    src/gg/input/input_codes.cpp
    src/gg/app/gg_clock.h
    src/gg/app/gg_clock.cpp
    src/gg/app/timers.h
    src/gg/app/gg_runtime.h
    src/gg/app/gg_runtime.cpp
    src/gg/app/seq_run.h
    src/gg/app/seq_run.cpp
    src/gg/app/options.h
    src/gg/app/options.cpp
    src/gg/app/settings.h
    src/gg/app/settings.cpp
    src/gg/app/icon.h
    src/gg/app/icon.cpp
    src/gg/app/test_pattern.h
    src/gg/app/test_pattern.cpp
    src/gg/app/gg_app.h
    src/gg/app/gg_app.cpp
    src/gg/app/runtime_cli.h
    src/gg/app/runtime_cli.cpp
    src/gg/app/cli_dispatch.h
    src/gg/app/cli_dispatch.cpp
)
