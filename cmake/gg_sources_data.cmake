# G&G sources for area "data". Add files with:
# target_sources(ggcore PRIVATE src/gg/<module>/file.cpp ...)
target_sources(ggcore PRIVATE
    src/gg/data/gg_exe.cpp
    src/gg/data/gg_files.cpp
    src/gg/data/lzh.cpp
    src/gg/data/archive.cpp
    src/gg/data/resources.cpp
    src/gg/data/data_cli.cpp
    src/gg/data/cli_args.cpp
    src/gg/save/players_dat.cpp
    src/gg/save/players_store.cpp
)
