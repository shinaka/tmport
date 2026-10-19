# sources for area "dos_audio". Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
# The DOS edition's sound driver (SST.EXE segment 491b), its device models and output.
target_sources(tmcore PRIVATE
    src/dos/audio/sound_data.cpp
    src/dos/audio/sequencer.cpp
    src/dos/audio/speaker_synth.cpp
    src/dos/audio/psg_synth.cpp
    src/dos/audio/opl_backend.cpp
    src/dos/audio/dos_sound_renderer.cpp
    src/dos/audio/dos_audio_device.cpp
    src/dos/audio/dos_sound_cli.cpp
    src/dos/audio/adlib_driver.cpp
    src/dos/audio/opl_chip.cpp
)

# ymfm (BSD-3-Clause, Aaron Giles): the YM3812 (OPL2) emulator core behind the AdLib music
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
target_link_libraries(tmcore PRIVATE tm_ymfm)
