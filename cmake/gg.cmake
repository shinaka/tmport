# Gizmos & Gadgets (DOS v1.0, 1993): the second game of this repository (re/gg/docs/SCOPING.md §9).
# Included by CMakeLists.txt when TM_BUILD_GG is ON, after GG_CORE_LIB names the shared engine core
# (portcore; tmcore until the core library exists). G&G code includes only core headers (src/util,
# src/vfs, src/rtl, src/timing, ...) and never Treasure Mountain's game code.
#
#   ggcore      static library of src/gg/<module>/ (namespace tmport::gg); the areas add their
#               sources in cmake/gg_sources_<area>.cmake
#   ggport      the game (WIN32: a GUI-subsystem program on Windows, like tmport)
#   ggport-cli  the same program as a console application (Windows only), for the tools and scripts
#   ggtests     unit tests (doctest); the areas add their tests in cmake/gg_tests_<area>.cmake. Tests
#               that need the user's files read GG_DATA / GG_CAPTURES and skip without them.

if(NOT GG_CORE_LIB)
    set(GG_CORE_LIB tmcore)
endif()

# --- G&G engine library -----------------------------------------------------------------------------

add_library(ggcore STATIC)
# Per-area source lists (each owns one file: target_sources(ggcore PRIVATE ...)).
include(cmake/gg_sources_data.cmake)
include(cmake/gg_sources_gfx.cmake)
include(cmake/gg_sources_audio.cmake)
include(cmake/gg_sources_runtime.cmake)
include(cmake/gg_sources_ui.cmake)
include(cmake/gg_sources_world.cmake)
include(cmake/gg_sources_integration.cmake)
# Wave 3 (re/gg/docs/ENGINE_MAP.md, "Wave 3 map"): the skeleton and one list per module area.
include(cmake/gg_sources_skeleton.cmake)
include(cmake/gg_sources_flow.cmake)
include(cmake/gg_sources_ui3.cmake)
include(cmake/gg_sources_world3.cmake)
include(cmake/gg_sources_actors.cmake)
include(cmake/gg_sources_puzzles_a.cmake)
include(cmake/gg_sources_puzzles_b.cmake)
include(cmake/gg_sources_vehicles.cmake)
target_include_directories(ggcore PUBLIC src)
target_link_libraries(ggcore PUBLIC ${GG_CORE_LIB})
tm_configure_target(ggcore)

# --- The game executable ----------------------------------------------------------------------------

add_executable(ggport WIN32 src/gg/main.cpp)
target_link_libraries(ggport PRIVATE ggcore)
tm_configure_target(ggport)
if(WIN32)
    # The same program as a console application: the shell waits for it and sees its exit code.
    add_executable(ggport-cli src/gg/main.cpp)
    target_link_libraries(ggport-cli PRIVATE ggcore)
    tm_configure_target(ggport-cli)
endif()

# --- Unit tests ---------------------------------------------------------------------------------------

if(TM_BUILD_TESTS)
    add_executable(ggtests tests/gg/test_main.cpp)
    # Per-area test lists (each owns one file: target_sources(ggtests PRIVATE ...)).
    include(cmake/gg_tests_data.cmake)
    include(cmake/gg_tests_gfx.cmake)
    include(cmake/gg_tests_audio.cmake)
    include(cmake/gg_tests_runtime.cmake)
    include(cmake/gg_tests_ui.cmake)
    include(cmake/gg_tests_world.cmake)
    include(cmake/gg_tests_integration.cmake)
    include(cmake/gg_tests_skeleton.cmake)
    include(cmake/gg_tests_flow.cmake)
    include(cmake/gg_tests_ui3.cmake)
    include(cmake/gg_tests_world3.cmake)
    include(cmake/gg_tests_actors.cmake)
    include(cmake/gg_tests_puzzles_a.cmake)
    include(cmake/gg_tests_puzzles_b.cmake)
    include(cmake/gg_tests_vehicles.cmake)
    target_include_directories(ggtests PRIVATE tests tests/gg)
    target_link_libraries(ggtests PRIVATE ggcore doctest::doctest)
    # Include the standard headers instead of forward-declaring std types (MSVC C5285).
    # The repository root, for the checked-in docs the tests compare with (re/gg/docs/*.tsv).
    target_compile_definitions(ggtests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS
        GG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    tm_configure_target(ggtests)
    enable_testing()
    add_test(NAME ggtests COMMAND ggtests)
endif()
