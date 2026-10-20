# G&G sources for area "flow" (wave 3): main and Seq_Run's table, Game_Init / Game_Shutdown, the attract
# (state 1), the hub (state 3), states 16 and 17, Building_Free, Save & Exit, the LZH key drain, the sound
# facade over GgAudio, the trace logger and the state dump, starting from a DOSBox-X state.
target_sources(ggcore PRIVATE
    src/gg/game/flow.h
    src/gg/game/flow.cpp
    src/gg/game/flow_attract.cpp
    src/gg/game/sound_audio.cpp
    src/gg/game/trace_log.h
    src/gg/game/trace_log.cpp
    src/gg/game/load_state.h
    src/gg/game/load_state.cpp
)
