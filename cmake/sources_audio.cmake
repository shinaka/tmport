# sources for area "audio". Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
target_sources(tmcore PRIVATE
    src/audio/sound.cpp
    src/audio/wave_voice.cpp
    src/audio/sdl_wave_output.cpp
    src/audio/smf.cpp
    src/audio/midi_backend.cpp
    src/audio/midi_backend_winmm.cpp
    src/audio/midi_backend_soundfont.cpp
    src/audio/sf2_synth.cpp
    src/audio/sf2_writer.cpp
    src/audio/midi_render.cpp
    src/audio/midi_sequencer.cpp
    src/audio/midi_player.cpp
    src/audio/audio_tables.cpp
    src/audio/midi_driver.cpp
    src/audio/music_manager.cpp
    src/audio/audio_system.cpp
    src/audio/engine_input_port.cpp
    src/audio/audio_cli.cpp
)
# The WinMM MIDI back end (midiOut*, timeBeginPeriod).
if(WIN32)
    target_link_libraries(tmcore PUBLIC winmm)
endif()

# TinySoundFont (MIT, Bernhard Schelling): the SoundFont 2 synthesizer of the cross-platform MIDI
# back end. Header-only (tsf.h); compiled once in src/audio/sf2_synth.cpp. The repository has no
# release tags, so it is pinned to a commit, by URL and SHA-256 like SDL and doctest. It has no
# CMakeLists.txt: FetchContent only downloads it.
FetchContent_Declare(tinysoundfont
    URL https://github.com/schellingb/TinySoundFont/archive/853a0a171759f1ddba0de1442133a75912bbeffa.tar.gz
    URL_HASH SHA256=76c356df524f71f2a34f67fa0cb2cb946036fe7a65d2930a849569c5e742fa57)
FetchContent_MakeAvailable(tinysoundfont)
target_include_directories(tmcore SYSTEM PRIVATE ${tinysoundfont_SOURCE_DIR})
