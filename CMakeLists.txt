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

include(FetchContent)

# --- Third-party code: pinned by URL + hash, fetched at configure time -------------------------

# SDL 3.4.18 (zlib license), static.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
FetchContent_MakeAvailable(SDL3)

if(TM_BUILD_TESTS)
    # doctest 2.4.12 (MIT), header-only.
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(doctest
        URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
        URL_HASH SHA256=73381c7aa4dee704bd935609668cf41880ea7f19fa0504a200e13b74999c2d70)
    FetchContent_MakeAvailable(doctest)
endif()

# --- Common compile settings --------------------------------------------------------------------

function(tm_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive- /Zc:__cplusplus /EHsc)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# --- Engine core library ------------------------------------------------------------------------

add_library(tmcore STATIC
    src/util/log.cpp
    src/util/file_util.cpp
    src/util/png_writer.cpp
    src/vfs/data_source.cpp
    src/vfs/directory_source.cpp
    src/vfs/cd_image_source.cpp
    src/ne/ne_image.cpp
    src/data/exe_tables.cpp
    src/res/resource_types.cpp
    src/res/mod_overrides.cpp
    src/res/ssnd.cpp
    src/res/resource_manager.cpp
    src/res/game_files.cpp
    src/gfx/palette.cpp
    src/gfx/pixmap.cpp
    src/gfx/frame_buffer.cpp
    src/gfx/presenter.cpp
    src/gfx/image_export.cpp
    src/input/input_queue.cpp
    src/timing/deadline_timers.cpp
    src/rtl/borland_rand.cpp
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
target_include_directories(tmcore PUBLIC src)
target_link_libraries(tmcore PUBLIC SDL3::SDL3-static)
tm_configure_target(tmcore)

# --- The game executable ------------------------------------------------------------------------

# WIN32: a GUI-subsystem program on Windows (no console window on a double-click; SDL_main.h
# provides WinMain). From a console it attaches to it for its output (src/platform/console.h).
add_executable(tmport WIN32 src/main.cpp)
target_link_libraries(tmport PRIVATE tmcore)
tm_configure_target(tmport)
if(WIN32)
    # The same program as a console application: the shell waits for it and sees its exit code
    # (scripts, CI). Elsewhere tmport itself is a console program.
    add_executable(tmport-cli src/main.cpp)
    target_link_libraries(tmport-cli PRIVATE tmcore)
    tm_configure_target(tmport-cli)
endif()

# --- Unit tests ---------------------------------------------------------------------------------

if(TM_BUILD_TESTS)
    add_executable(tmtests
        tests/test_main.cpp
        tests/test_util.cpp
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
    target_include_directories(tmtests PRIVATE tests)
    target_link_libraries(tmtests PRIVATE tmcore doctest::doctest)
    # Include the standard headers instead of forward-declaring std types (MSVC C5285).
    target_compile_definitions(tmtests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
    tm_configure_target(tmtests)
    enable_testing()
    add_test(NAME tmtests COMMAND tmtests)
endif()
