# sources for area "mods". Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
# Mod layer (docs/MODDING.md): override formats, table overrides, mod-kit exporter.
target_sources(tmcore PRIVATE
    src/mods/json.cpp
    src/mods/png_codec.cpp
    src/mods/text_codec.cpp
    src/mods/dam_asm.cpp
    src/mods/wav_codec.cpp
    src/mods/pmap_codec.cpp
    src/mods/mod_folder.cpp
    src/mods/resource_import.cpp
    src/mods/table_overrides.cpp
    src/mods/mod_data_source.cpp
    src/mods/mods.cpp
    src/mods/mod_kit.cpp
    src/mods/mods_cli.cpp
)
# The 1990 DOS edition's mod layer (docs/MODDING.md "The 1990 DOS edition").
target_sources(tmcore PRIVATE
    src/dos/mods/dos_text_codec.cpp
    src/dos/mods/dos_pictures.cpp
    src/dos/mods/dos_sound_text.cpp
    src/dos/mods/dos_text_files.cpp
    src/dos/mods/dos_exe_strings.cpp
    src/dos/mods/dos_mod_folder.cpp
    src/dos/mods/dos_mods.cpp
    src/dos/mods/dos_mod_kit.cpp
    src/dos/mods/dos_mods_cli.cpp
)
