# sources for area "dos_audio". Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
# The DOS edition's sound driver (SST.EXE segment 491b), its device models and output.
target_sources(tmcore PRIVATE
    src/dos/audio/sound_data.cpp
    src/dos/audio/sequencer.cpp
    src/dos/audio/opl_backend.cpp
    src/dos/audio/dos_sound_renderer.cpp
    src/dos/audio/dos_audio_device.cpp
    src/dos/audio/dos_sound_cli.cpp
    src/dos/audio/adlib_driver.cpp
)

# The chips (speaker_synth, psg_synth, opl_chip with ymfm) are in the shared core (cmake/portcore.cmake).
