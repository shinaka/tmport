# tests for area "audio". Add files with:
# target_sources(tmtests PRIVATE path/to/file.cpp ...)
target_sources(tmtests PRIVATE
    tests/test_audio.cpp
    tests/test_midi.cpp
    tests/test_music.cpp
    tests/test_soundfont.cpp
)
