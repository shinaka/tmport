#pragma once
// The results, the post-race menu and the final movie (ssg.exe ovl10 2fcb: states 12..15;
// re/gg/docs/code_vehicles_races.md §11-§14): the result screen, the points counted up over 50 frames
// (one click per frame), the rank message after every won race below the top rank, the post-race
// buttons (rematch, next race, warehouse, hub, Save & Exit) with the saves at the original's moments,
// building completion and the trophies' counters, and the movie (MoviePlayer, its Delay cues, the
// music cue Music_MovieCue through the facade) with the save after it.
//
// Owner: vehicles agent.

#include <cstdint>
#include <string>
#include <vector>

namespace tmport::gg {

class GgGame;

// 2fcb:0000 Result_DrawScreen: picture 0x1076 / palette 0x1077, the covers of the unavailable
// buttons (g_resultNextRace, g_resultLost, the final race: then Mouse_Hide), "1st -" / "2nd -" and the
// names, Screen_SavePlayArea.
void resultDrawScreen(GgGame& g);
// The text of Result_RankUpMessage for a score: bundle 0x1070 member k, the rank name, member k + 1,
// the next threshold with thousands commas (not before the top rank), member k + 2 (k = 3 one step below
// the top rank, else 0); empty at the top rank (no message).
std::vector<uint8_t> resultRankUpText(GgGame& g, uint32_t score);
// 2fcb:01dd Result_RankUpMessage: the rank message (Dlg_MessageBox, no title, the "Enter to Go On"
// footer) when below the top rank.
void resultRankUpMessage(GgGame& g);
// 2fcb:0315 State_14_RedrawResult: Result_DrawScreen, Screen_FadeOutIn; 13 (after a rematch).
int16_t state14RedrawResult(GgGame& g);
// 2fcb:0325 State_12_RaceResult: effect bank 0x108F, the screen, the animations, the music (not
// looping), the 4-tick loop with the count-up (I(0x1071)[g_racesWonTotal - 1] * 1000 / 50 per frame,
// Hud_Update(0xFFFB)), the busy-wait on the music, the rank message or hint 0x104F; 15 when all 15 races
// are won, else 13.
int16_t state12RaceResult(GgGame& g);
// 2fcb:0573 State_13_PostRaceMenu: the zones DS:22fe, Input_WaitCode; rematch -> 11, next race -> 4,
// warehouse -> 5, hub -> 3, Save & Exit; the in-progress flag and Players_SaveCurrent after a win,
// Building_Free when leaving the building; Zones_ClearLocal, Pal_FadeOut, Pal_Restore.
int16_t state13PostRaceMenu(GgGame& g);
// 2fcb:0640 State_15_FinalMovie: the movie music 0x30CA and its cue, effects 0x108D, Movie_Play(0x3000,
// 0x30CA, 18, DS:236c), Input_WaitCode, the music freed, the in-progress flag 0, Players_SaveCurrent
// (GgSaveMoment::FinalMovie); 17.
int16_t state15FinalMovie(GgGame& g);

}  // namespace tmport::gg
