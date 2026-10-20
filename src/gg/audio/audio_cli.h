#pragma once
// Headless command-line tools of the G&G sound area (the ggport-cli dispatcher calls audioCli with
// the program's arguments, without argv[0]; it returns false when none of them is its command).
// Nothing is played: the driver runs offline, on the sound-tick clock (72.826 Hz).
//
//   --gg-sound fm|speaker|off WHAT OUT.wav     renders to a 16-bit mono WAV
//   --gg-oplog [fm|off] WHAT OUT.txt           every OPL register write: 'tick reg value[ c]' (hex reg
//                                              and value; tick = the sound tick; 'c' = made by a game
//                                              call before that tick), as tools/dos/oplcmp.py reads it
//   --gg-dro-compare LOG.txt REF.dro           an OPL log against a DOSBox-X capture (dro_compare.h)
//   --gg-songs                                 TSV: every song of the archive, as tools/gg/bmf.py --all
//   --gg-soundtest-check NAME|all [--captures DIR]  a named schedule (soundtest.h) against its DOSBox-X
//                                              capture (DIR/audio/*.dro; default GG_CAPTURES)
//
// WHAT (ids in hex, "0x1021" or "1021"):
//   song ID[/M]     the BMF entry ID, or member M of bundle ID, exactly that resource; played once
//                   (--loop: looping) at priority 0
//   music ID        music bank ID as the game loads it (+1 for fm and off: g_fmPresent), arrangement
//                   --arrangement N (none: all channels), looping (--once: not)
//   effect ID K     effect K of effect bank ID (+ g_fmPresent), SndRes_PlayEffect(loop 0, --priority P;
//                   default 4 = driver priority 43h)
//   soundtest NAME  a schedule (soundtest list: the names), or a script "TICK:CMD[:ARG..],...": the
//                   command runs before sound tick TICK (absolute); CMD m:ID load music bank, p[:LOOP]
//                   play it, a:N arrangement, x stop it, M free it, e:ID load an effect bank, f:K[:LOOP[:P]]
//                   play effect K of it, s:K stop it, E free it, g:K[:LOOP[:P]] a global effect (0x105B),
//                   t:T transpose of the music's section 0, q Ctrl+S, z pause, r resume, R:B:N room music
//                   of building B entering room arrangement N (Rooms_Load, RoomMusic_SelectLayers,
//                   RoomMusic_Start), W workshop music start, V movie music start, end
// Options: --data DIR (the game folder; else GG_DATA or next to the program), --ticks N (length;
// default: until the song ends, one pass of a looping one, the script's end), --rate HZ (48000),
// --raw (no low-pass and DC blocker), --speaker-log OUT.txt ('tick div|p61 value[ c]'),
// --opl-log OUT.txt (--gg-sound); --gg-dro-compare: --from N (search the capture's start from log
// entry N), --base engine|dump (the write filter starts from the engine's registers, or from the
// capture's dump for a capture made after screens the run does not reproduce), --pairs OUT.tsv (the
// matched writes: .dro ms, engine tick, writes before it in the tick, game call); --seed-dro REF.dro
// (--gg-oplog: the driver's B0h shadows of voices 0..5 from that capture's dump, for the same).

#include <string>
#include <vector>

namespace tmport::gg {

bool audioCli(const std::vector<std::string>& args, int& exitCode);

}  // namespace tmport::gg
