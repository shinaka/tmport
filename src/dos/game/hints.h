#pragma once
// hints.c (segment 49a6): the New Player Hints (15 one-shot flags, the Flutter bubble) and the
// start-of-game and end-of-game messages; both per frame. Message texts are read from and patched
// in the writable copy ElfState::messageText (g.dsString(t.elves.messageOffset[id])).
// Spec: re/dos/docs/code_elves_layout_hints.md §6. Owner: elves agent.

namespace tmport::dos {

class DosGame;

// 49a6:0001 SoundPort_Out: part of the sound driver (out(DS:656C, AL)); no engine function.

// 49a6:000f Hints_Update (per frame): with hints on and no other UI mode, HUD redraw, menu lock or
// transition: the timer reset (DS:913F); no bubble: Hints_CheckTriggers, the highest due hint opens
// (g_uiMode |= 0x10, overlay redraw, Flutter_Show, prompt 6); bubble open: Enter / Esc / a button
// change closes it (full redraw, prompt restored). Reads g_cmd with the DS:9238 handshake.
void hintsUpdate(DosGame& g);
// 49a6:0121 Hints_CheckTriggers: hint 1 (3 "seconds" in the start area without walking), 2 (31
// seconds without the net, area 0), 3 (2 clue words known, no coin for 5 more seconds), 5 (net
// retracted with 0 nets).
void hintsCheckTriggers(DosGame& g);
// 49a6:022b Hints_PickNext: the highest id 14..0 whose flag is 0 (flag -> 0x80), else -1 (and, never
// reached, hints off when all flags have bit 7).
int hintsPickNext(DosGame& g);
// 49a6:026f Gameover_UpdateMessages (per frame): an open message closes on Enter / Esc / a button
// change (pending -= 1); else shows the pending one: 1 = the rank start message 23..30 (digits and
// plural patched; Flutter intro 0 for a first game), g_gameInProgress = 1; 2 = the end message 31..34
// (digits patched), or the crown messages 35 (name patched, pending 3) / 36 for totals 300..314;
// arms hint 13; g_uiMode = 4.
void gameoverUpdateMessages(DosGame& g);
// 49a6:0565 Flutter_Show(msg): the cloud (SPRITES 71) and, with g_uiMode & 0x14, Flutter (72); ink
// 0xFFFF; Text_Draw(0x14, 0x22, text[msg]); g_dirty = 1.
void flutterShow(DosGame& g, int msg);

}  // namespace tmport::dos
