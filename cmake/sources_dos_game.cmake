# sources for area "dos_game" (wave 3: the DOS edition's game logic, src/dos/game/). One line per
# file; add your files, never reorder or remove the lines of others. Add files with:
# target_sources(tmcore PRIVATE path/to/file.cpp ...)
target_sources(tmcore PRIVATE
    src/dos/game/game_state.cpp
    src/dos/game/game.cpp
    src/dos/game/sound.cpp
    src/dos/game/main_loop.cpp
    src/dos/game/init.cpp
    src/dos/game/scene.cpp
    src/dos/game/camera.cpp
    src/dos/game/cells.cpp
    src/dos/game/cells2.cpp
    src/dos/game/player.cpp
    src/dos/game/anim.cpp
    src/dos/game/search.cpp
    src/dos/game/endgame.cpp
    src/dos/game/rank_chest.cpp
    src/dos/game/elves.cpp
    src/dos/game/layout.cpp
    src/dos/game/hints.cpp
    src/dos/game/ui.cpp
    src/dos/game/title.cpp
    src/dos/game/signin.cpp
    src/dos/game/quiz.cpp
    src/dos/game/hud.cpp
    src/dos/game/trace.cpp
    src/dos/save/dosbox_state.cpp
    src/dos/game/load_state.cpp
)
