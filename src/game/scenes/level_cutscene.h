#pragma once
// Level-transition cut-scene (segment 1188: LevelCutscene_Play 1188:0000; re/docs/
// code_1168-1198.md §6.1, ARCHITECTURE.md §2.8, REFERENCE_CAPTURES.md §4.3 / E4 13-19).
//
// Played by the climb loop when the room's level changes through a gate, with the old level:
//   oldLevel >= 2 (into the castle): only gate_unlocking := 0;
//   otherwise: arrow cursor, menus greyed, play-field clip; Anim_LoadBundle(BNDL 410 / 412,
//   frame base 0) = part 1 script (DAM 20200 / 20204), part 2 script OTHR 20201 / 20205 (it uses
//   the same frames); part 1 plays over the current room at 7 fps (Enter / Space skippable,
//   Esc ignored); gate_unlocking := 0; Space (2) skips the rest; otherwise black play-field,
//   background PMAP 2200 / 2201 at (0,20) + line at y 287, copied to the back page, presented,
//   part 2 at 7 fps, black play-field again; then Sfx_Stop, cursor and menus restored.
// The tables (BNDL ids DS:2d6d, part 2 ids DS:2d71, backgrounds DS:2d75) are read from the EXE.

#include "app/app_context.h"
#include "game/scenes/scene_support.h"

namespace tmport::game::scenes {

namespace cutscene {
inline constexpr int kFps = 7;
inline constexpr int kLevels = 2;  // cut-scenes exist for old levels 0 and 1
}  // namespace cutscene

// The ids the cut-scene of oldLevel (0 or 1) uses, read from the EXE.
struct CutsceneIds {
    int bundle = 0;   // DS:2d6d[level]: 410 / 412
    int script2 = 0;  // DS:2d71[level]: 20201 / 20205
    int background = 0;  // DS:2d75[level]: 2200 / 2201
};
CutsceneIds cutsceneIds(const ExeTables& exe, int oldLevel);

// 1188:0000 LevelCutscene_Play(oldLevel). Returns the result of part 1 (anim::ModalResult: 0
// played out, 1 Enter, 2 Space = part 2 skipped), or -1 when no cut-scene exists for the level.
int playLevelCutscene(AppContext& ctx, int oldLevel, const SceneHooks& hooks = {});

}  // namespace tmport::game::scenes
