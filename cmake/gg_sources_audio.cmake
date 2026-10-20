# G&G sources for area "audio": the BMF songs, the sound driver (ssg.exe 1bb4), the bank layer
# (1cdd), the music cues (12e0), the puzzles' sound helpers (3924), the device glue on the core's
# DOS sound hardware, the .dro comparison and the sound tools (re/gg/docs/code_sound.md).
target_sources(ggcore PRIVATE
    src/gg/audio/bmf.cpp
    src/gg/audio/snd_driver.cpp
    src/gg/audio/snd_res.cpp
    src/gg/audio/music_cues.cpp
    src/gg/audio/pz_snd.cpp
    src/gg/audio/sound_mode.cpp
    src/gg/audio/gg_audio.cpp
    src/gg/audio/soundtest.cpp
    src/gg/audio/dro_compare.cpp
    src/gg/audio/audio_cli.cpp
)
