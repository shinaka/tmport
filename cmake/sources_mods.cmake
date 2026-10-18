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
