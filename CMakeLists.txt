cmake_minimum_required(VERSION 3.24)

project(tmport
    VERSION 0.1.0
    DESCRIPTION "Engine re-implementation of Super Solvers Treasure Mountain! (Windows CD edition)"
    LANGUAGES C CXX)

# The engine contains no game data. It reads the user's original files at run time.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TM_BUILD_TESTS "Build the tmtests unit tests (fetches doctest)" ON)

# --- The engine: MischiefEngine -----------------------------------------------------------------

# The engine core (file and CD-image access, clocks, the SDL host and presenter, the DOS sound chips
# and input models, the codecs, the TLC text engine) is the library `mischief` of the MischiefEngine
# repository. cmake/engine.cmake fetches the commit pinned in cmake/engine_pin.cmake, or uses a local
# checkout (MISCHIEF_ENGINE_DIR, else ../MischiefEngine); docs/BUILDING.md "The engine". The engine
# brings SDL 3, ymfm, doctest and mischief_configure_target().
include(cmake/engine.cmake)

# --- Treasure Mountain engine library -----------------------------------------------------------

add_library(tmcore STATIC
    src/ne/ne_image.cpp
    src/data/exe_tables.cpp
    src/res/resource_types.cpp
    src/res/mod_overrides.cpp
    src/res/ssnd.cpp
    src/res/resource_manager.cpp
    src/res/game_files.cpp
    src/gfx/pixmap.cpp
    src/gfx/frame_buffer.cpp
    src/input/input_queue.cpp
    src/platform/engine.cpp
    src/cli/options.cpp
    src/cli/contact_sheet.cpp
    src/cli/test_scene.cpp
    src/cli/commands.cpp
    src/cli/tool_mods.cpp
)
# Per-area source lists (each owns one file: target_sources(tmcore PRIVATE ...)).
include(cmake/sources_text_ui.cmake)
include(cmake/sources_input_save.cmake)
include(cmake/sources_audio.cmake)
include(cmake/sources_game.cmake)
include(cmake/sources_anim.cmake)
include(cmake/sources_screens.cmake)
include(cmake/sources_app.cmake)
include(cmake/sources_scenes.cmake)
include(cmake/sources_mods.cmake)
include(cmake/sources_room.cmake)
include(cmake/sources_autoplay.cmake)
include(cmake/sources_platform.cmake)
include(cmake/sources_dos_data.cmake)
include(cmake/sources_dos_gfx.cmake)
include(cmake/sources_dos_audio.cmake)
include(cmake/sources_dos_runtime.cmake)
include(cmake/sources_dos_game.cmake)
include(cmake/sources_dos_autoplay.cmake)
target_include_directories(tmcore PUBLIC src)
target_link_libraries(tmcore PUBLIC mischief)
mischief_configure_target(tmcore)

# --- The game executable ------------------------------------------------------------------------

# WIN32: a GUI-subsystem program on Windows (no console window on a double-click; SDL_main.h
# provides WinMain). From a console it attaches to it for its output (the engine's
# mischief/platform/console.h).
add_executable(tmport WIN32 src/main.cpp)
target_link_libraries(tmport PRIVATE tmcore)
mischief_configure_target(tmport)
if(WIN32)
    # The same program as a console application: the shell waits for it and sees its exit code
    # (scripts, CI). Elsewhere tmport itself is a console program.
    add_executable(tmport-cli src/main.cpp)
    target_link_libraries(tmport-cli PRIVATE tmcore)
    mischief_configure_target(tmport-cli)
endif()

# --- Unit tests ---------------------------------------------------------------------------------

if(TM_BUILD_TESTS)
    # doctest at the engine's version.
    mischief_use_doctest()
    add_executable(tmtests
        tests/test_main.cpp
        tests/test_rand.cpp
        tests/test_gfx.cpp
        tests/test_vfs.cpp
        tests/test_ne.cpp
        tests/test_resources.cpp
        tests/test_pmap.cpp
        tests/test_exe_tables.cpp
        tests/test_cli.cpp
    )
    # Per-area test lists (each owns one file: target_sources(tmtests PRIVATE ...)).
    include(cmake/tests_text_ui.cmake)
    include(cmake/tests_input_save.cmake)
    include(cmake/tests_audio.cmake)
    include(cmake/tests_game.cmake)
    include(cmake/tests_anim.cmake)
    include(cmake/tests_screens.cmake)
    include(cmake/tests_app.cmake)
    include(cmake/tests_scenes.cmake)
    include(cmake/tests_mods.cmake)
    include(cmake/tests_room.cmake)
    include(cmake/tests_autoplay.cmake)
    include(cmake/tests_platform.cmake)
    include(cmake/tests_dos_data.cmake)
    include(cmake/tests_dos_gfx.cmake)
    include(cmake/tests_dos_audio.cmake)
    include(cmake/tests_dos_runtime.cmake)
    include(cmake/tests_dos_game.cmake)
    include(cmake/tests_dos_autoplay.cmake)
    target_include_directories(tmtests PRIVATE tests)
    target_link_libraries(tmtests PRIVATE tmcore mischief_test_support doctest::doctest)
    # Include the standard headers instead of forward-declaring std types (MSVC C5285).
    target_compile_definitions(tmtests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
    mischief_configure_target(tmtests)
    enable_testing()
    add_test(NAME tmtests COMMAND tmtests)
endif()
