# portcore: the engine core shared by both games of this repository (Treasure Mountain: tmcore,
# tmport; Gizmos & Gadgets: ggcore, ggport). docs/DESIGN.md "Core and games" describes the split.
#
# The core is a library over the existing directories, not a separate tree: utilities, data
# sources, the Borland RTL pieces, clocks and timers, the SDL host and presenter, the DOS sound
# chips and their output, the DOS keyboard and mouse models, scripted input in ticks, the INI file,
# the mod codecs that do not depend on a game, and the TLC text engine (both games' Text_Render).
#
# Rule: a file listed here includes only files listed here (and the standard library, SDL and
# ymfm). tools/check_core_deps.py follows every #include "..." of these files and fails on any
# header that is not in this list:
#     python -P tools/check_core_deps.py
# Headers are listed with the sources so that the checker (and IDEs) see the whole core.

add_library(portcore STATIC
    # util: logging, files, byte reader, errors, hashes, PNG writer and inflate
    src/util/byte_reader.h
    src/util/error.h
    src/util/file_util.h
    src/util/file_util.cpp
    src/util/hash.h
    src/util/inflate.h
    src/util/inflate.cpp
    src/util/log.h
    src/util/log.cpp
    src/util/png_writer.h
    src/util/png_writer.cpp
    src/util/rgb.h
    # vfs: directory and CD image data sources
    src/vfs/data_source.h
    src/vfs/data_source.cpp
    src/vfs/directory_source.h
    src/vfs/directory_source.cpp
    src/vfs/cd_image_source.h
    src/vfs/cd_image_source.cpp
    # rtl: Borland rand/srand
    src/rtl/borland_rand.h
    src/rtl/borland_rand.cpp
    # timing: clocks, deadline timers, the virtual PIT
    src/timing/clock.h
    src/timing/deadline_timers.h
    src/timing/deadline_timers.cpp
    src/timing/pit_clock.h
    src/timing/pit_clock.cpp
    # platform: SDL host (window, events, focus pause, fullscreen, screenshots), console, icons
    src/platform/host.h
    src/platform/host.cpp
    src/platform/console.h
    src/platform/console.cpp
    src/platform/exe_icon.h
    src/platform/exe_icon.cpp
    # gfx: presenter (layouts, canvas, letterbox), palette, PNG export of indexed pictures
    src/gfx/rect.h
    src/gfx/palette.h
    src/gfx/palette.cpp
    src/gfx/presenter.h
    src/gfx/presenter.cpp
    src/gfx/image_export.h
    src/gfx/image_export.cpp
    # text: the TLC text engine (Text_Render of TM Windows and G&G), font and target behind interfaces
    src/text/text_engine.h
    src/text/text_engine.cpp
    # save: the INI file
    src/save/ini_file.h
    src/save/ini_file.cpp
    # mods: codecs that do not depend on a game (JSON, PNG, WAV)
    src/mods/json.h
    src/mods/json.cpp
    src/mods/png_codec.h
    src/mods/png_codec.cpp
    src/mods/wav_file.h
    src/mods/wav_file.cpp
    # DOS sound chips: PC speaker, SN76496, OPL2 (ymfm)
    src/dos/audio/synth_common.h
    src/dos/audio/speaker_synth.h
    src/dos/audio/speaker_synth.cpp
    src/dos/audio/psg_synth.h
    src/dos/audio/psg_synth.cpp
    src/dos/audio/opl_writer.h
    src/dos/audio/opl_output.h
    src/dos/audio/opl_chip.h
    src/dos/audio/opl_chip.cpp
    src/dos/audio/opl_log.h
    src/dos/audio/opl_log.cpp
    # DOS sound output: the chips ticked by a game's driver, played through SDL
    src/dos/audio/chip_renderer.h
    src/dos/audio/chip_renderer.cpp
    src/dos/audio/chip_audio_device.h
    src/dos/audio/chip_audio_device.cpp
    # DOS input: the PC keyboard with typematic repeats, the BIOS keyboard, the INT 33h mouse, and
    # scripted input in game ticks
    src/dos/input/bios_keyboard.h
    src/dos/input/bios_keyboard.cpp
    src/dos/input/pc_keyboard.h
    src/dos/input/pc_keyboard.cpp
    src/dos/input/int33_mouse.h
    src/dos/input/int33_mouse.cpp
    src/dos/app/script_input.h
    src/dos/app/script_input.cpp
)
# Test helpers that use only the core (header-only, under tests/): the G&G tests may include them too.
# tools/check_core_deps.py checks them like the core's files.
set(PORTCORE_TEST_HELPERS
    tests/app_test_util.h
)

target_include_directories(portcore PUBLIC src)
target_link_libraries(portcore PUBLIC SDL3::SDL3-static)
tm_configure_target(portcore)

# ymfm (BSD-3-Clause, Aaron Giles): the YM3812 (OPL2) emulator core behind the DOS AdLib music
# (src/dos/audio/opl_chip.cpp; only its ym3812 class is used). The repository has no release tags
# and no CMakeLists.txt, so it is pinned to a commit by URL and SHA-256 like TinySoundFont, and its
# OPL-family sources (ymfm_opl.cpp, which also holds the Y8950 and OPL4 classes, needs the ADPCM
# and PCM engines) are compiled into a static library of their own, without the engine's warnings.
FetchContent_Declare(ymfm
    URL https://github.com/aaronsgiles/ymfm/archive/81aec25ccbb98f4873a255f7551ac4dadac59b4a.tar.gz
    URL_HASH SHA256=9bcf001b30f53754c49cf2fb864792a64702b8aeebb105baa908ad5dd33f443f)
FetchContent_MakeAvailable(ymfm)
add_library(tm_ymfm STATIC
    ${ymfm_SOURCE_DIR}/src/ymfm_opl.cpp
    ${ymfm_SOURCE_DIR}/src/ymfm_adpcm.cpp
    ${ymfm_SOURCE_DIR}/src/ymfm_pcm.cpp
)
target_include_directories(tm_ymfm SYSTEM PUBLIC ${ymfm_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(tm_ymfm PRIVATE /W0 /utf-8)
else()
    target_compile_options(tm_ymfm PRIVATE -w)
endif()
target_link_libraries(portcore PRIVATE tm_ymfm)
