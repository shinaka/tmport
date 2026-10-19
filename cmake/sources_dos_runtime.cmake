# sources for area "dos_runtime". Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
target_sources(tmcore PRIVATE
    src/dos/save/users_dat.cpp
    src/dos/save/users_store.cpp
    src/dos/input/bios_keyboard.cpp
    src/dos/input/keyboard.cpp
    src/dos/input/mouse.cpp
    src/dos/input/input_poll.cpp
    src/dos/app/script_input.cpp
    src/dos/app/dos_runtime.cpp
    # dos::runDosEdition (the game; the wave-2 screen preview behind --dos-preview).
    src/dos/app/dos_preview.cpp
    src/dos/app/dos_entry.cpp
)
