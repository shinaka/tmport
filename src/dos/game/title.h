#pragma once
// title.c (segment 4ba4): the title page, the two story pages, the Super Solvers page and the
// attract timing. The original's Title_Run is a blocking loop that main calls before its frame loop;
// the engine splits it into titleStart / titleFrame / titleFinish, run by DosGame's frame body while
// the title is active (ENGINE_MAP §3.3). Spec: re/dos/docs/code_ui.md §8. Owner: ui agent.

#include <cstdint>

namespace tmport::dos {

class DosGame;

// 4ba4:0434 Title_Run, before its loop: line height 10, g_uiMode = 0x80, area 6, OPENING.PAN, phase 0,
// music loop on, Music_Play(1).
void titleStart(DosGame& g);
// 4ba4:0434 Title_Run, one loop iteration: g_dirty = 0, Game_HandleHotkeys, the phase function
// (DS:8D83), the sparkle attempt (slot 2 idle, not on the SS page: rand() & 0x7F == 0 -> rand() % 12
// spot, Anim_Set(2, 0x5C); g.frameClock() TitleSparkle steps), Sprites_Snapshot(0, 3), g_stepDone =
// 0, Sprites_DrawSorted(0, 3), Frame_Present, Sprites_EraseAll(0, 3). Returns false once the phase
// entry is 0 (phase 9: the sign-in follows).
bool titleFrame(DosGame& g);
// 4ba4:0434 Title_Run, after its loop: slow Tandy/CGA: music off; line height restored; g_uiMode = 0;
// music loop off; Anim_InitSlots. (main then sets g_uiMode = 8.)
void titleFinish(DosGame& g);
// The phase function g_titlePhases DS:8D83[g_titlePhase]: 0 draw title, 1/3/5/11 wait, 2 story 1,
// 4 story 2, 6 start walk-off, 7 wait walk-off, 8 wait actor gone, 10 SS page, 12 reset. Engine helper.
void callTitlePhase(DosGame& g, int phase);
// 4ba4:000f Title_ClearBox: slow CPU Dirty_Clear; fill (5, 0x1E, 0x6F, 0xAA) 0xAA; g_dirty++ (slow:
// Present_Dirty).
void titleClearBox(DosGame& g);
// 4ba4:0045 TitleStep_DrawTitle (phase 0): OPENING.PAN full screen, slot 1 title figure (0x5B), the
// prompt line in blue, input context 5, DS:80DA = 8; phase++.
void titleStepDrawTitle(DosGame& g);
// 4ba4:00b2 TitleStep_Wait (phases 1/3/5/11): 7200-tick time-out (g_titleTimer) -> command 8 (13 on
// phase 5); Esc quits (SS page: back to the title), Enter next phase, S on story 2 the SS page.
void titleStepWait(DosGame& g);
// 4ba4:0156 Title_DrawSprite(x, y, frame2, flags): BEGIN frame frame2 / 2; g_dirty++.
void titleDrawSprite(DosGame& g, int x, int y, int frame2, uint8_t flags);
// 4ba4:017f TitleStep_Story1 (phase 2): box, story 1 text (DS:8A5A), BEGIN frame 32, prompt; phase++.
void titleStepStory1(DosGame& g);
// 4ba4:01cc TitleStep_Story2 (phase 4): box, story 2 text (DS:8B54), key line (DS:8C49), frame 33,
// prompt; phase++; DS:80DA = 13.
void titleStepStory2(DosGame& g);
// 4ba4:0233 TitleStep_StartWalkoff (phase 6): OPENING.PAN redrawn, Sprites_EraseAll(1, 1),
// Anim_Set(1, 0x5A); phase++.
void titleStepStartWalkoff(DosGame& g);
// 4ba4:027a TitleStep_WaitWalkoff (phase 7): slot 1 script ended or command 8: phase++, slot 1
// frame -1, prev 0, Anim_Clear(1).
void titleStepWaitWalkoff(DosGame& g);
// 4ba4:02a3 TitleStep_WaitActorGone (phase 8): draw record 1 frame == -1 -> phase 9.
void titleStepWaitActorGone(DosGame& g);
// 4ba4:02b1 Title_SuperSolversPage (phase 10): the series page (fills, BEGIN frames 34/35/36, texts
// DS:8C6F/8CCA/8CF0/8D2B, prompt); DS:8A14 = 1; phase++; DS:80DA = 8.
void titleSuperSolversPage(DosGame& g);
// 4ba4:042b Title_ResetPhase (phase 12): phase 0, DS:8A14 = 0.
void titleResetPhase(DosGame& g);

}  // namespace tmport::dos
