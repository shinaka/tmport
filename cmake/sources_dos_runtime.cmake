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
    # Wave 2 preview (dos::runDosEdition): replaced by the game modules in wave 3.
    src/dos/app/dos_preview.cpp
    src/dos/app/dos_entry.cpp
)
