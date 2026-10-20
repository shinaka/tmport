# Gizmos & Gadgets (DOS v1.0): BMF songs, sound banks and the sound driver (format level)

Scope: the music and sound effects of G&G: the `BMF` 0x1A song format, the bundles that carry
songs (sound banks) and how a speaker and an FM arrangement pair up, and what the resident sound
driver (segment 1bb4) does with the format: tick rate, slots, sections, the two classes in use.
The driver's internals (voice allocation, note and volume tables, the OPL register writes,
pitch bend, priorities) are a wave-1 topic and are only pointed to here.

There is no digitised sound anywhere: the `-sb` and `-sm` switches only select the OPL2 at
388h. Devices in use: PC speaker and OPL2 (AdLib) in rhythm mode; the driver also contains a
Tandy/PS1-style PSG path and a Roland MT-32 (MPU-401) path that nothing enables (scoping §1,
platform pass).

Evidence: the G&G Ghidra export (addresses `SSSS:OOOO`, Ghidra segment = image segment +
0x1000; overlays at 2a80..4140), `DS:XXXX` = DGROUP 1ea7 = file offset `0xFC70 + XXXX` of
`ssg.exe`. The class interpreters 1bb4:0966 and 1bb4:0ad6 had no function in the scoping export
and were read from raw bytes; the wave 0 project has them (`Snd_StepSpeaker`, `Snd_StepOpl`). Decoder: `tools/gg/bmf.py` (event listings, `--banks`, `--check`);
listings for every song in `extracted/previews/gg/bmf/`.

Labels: **[O]** observed in code, bytes or decoder output; **[I]** inferred.

---

## 1. Summary

| Item | Value |
|---|---|
| Songs | 495 `BMF` resources: 2 entries (0x1056 / 0x1057) and 493 bundle members; 237 of class 1 (speaker), 258 of class 3 (OPL2) [O] |
| Containers | 27 speaker/FM bank pairs: music banks (channel lists + one song) and effect banks (songs only) (§2) |
| Driver | resident segment 1bb4, hand-written assembly; 64 song slots |
| Tick | `Snd_Tick` 1bb4:0db3 runs on every second timer interrupt: PIT divisor 0x2000 → 145.652 Hz / 2 = **72.826 Hz** (13.73 ms) [O, platform pass] |
| Event language | MIDI-like: note off, note on, program, pitch bend, a meta end event; a delay byte after every event; running status |
| Speaker | class 1: one voice on PIT channel 2, notes through a divisor table (DS:1816) |
| OPL2 | class 3: 11 voices (6 melodic + 5 rhythm), rhythm mode on, 11-byte instrument patches inside each song |

---

## 2. Sound banks and the speaker/FM pairing [O]

Songs come in pairs of resources: **id holds the speaker arrangement (class 1), id + 1 the FM
arrangement (class 3)**. Every loader adds DS:a0d8 to the id it is given; `Init_CommandLine` sets
DS:a0d8 to 1 when the driver reports an OPL (caps bit 2 of `Snd_Init` 1bb4:1234), else 0.

| Loader | Kind | Container | Use |
|---|---|---|---|
| `1cdd:0024(id)` | music bank | bundle: `M` channel lists (`01 ... FF`), optionally an `L` record, a member whose payload starts with 0 (`I 0`), then **one** BMF song | walks members until a payload starts with 0 and registers the next member; a member whose payload starts with 4 stores the word at payload + 2 in DS:9dc2 (§2.1) |
| `1cdd:012d(id)` | effect bank | bundle of BMF songs; in a speaker bank a member can be `C FF` instead (no speaker version of that effect) | registers every member; the `C` members are rejected (not BMF) and leave an empty handle |
| `1cdd:01e0(id)` | single song | a BMF entry | 0x1056 / 0x1057, the New Player Hint chime (3060:013b) |

Handles: DS:9d48[60] (loaders 012d and 01e0 search from entry 5), memory far pointers DS:9c58.

`bmf.py --banks` lists 54 banks in 27 pairs (id, id + 1), every one paired: music banks 0x0027,
0x00f7, 0x00f9, 0x0262, 0x035f, 0x0361, 0x0363, 0x1020, 0x1089, 0x108b, 0x30ca (each with its
id + 1); effect banks 0x0029, 0x00f4, 0x0260, 0x1022, 0x102f, 0x1039, 0x105b, 0x108d, 0x108f,
0x11c6, 0x11c8, 0x11ca, 0x11cc, 0x11ce, 0x11d0, 0x11d2 (with id + 1). The two arrangements of an
effect bank have the same number of members, so effect n means the same sound in both. The FM
music bank may have one more channel list than the speaker one (0x0360 vs 0x035f) [O].

The puzzle effect banks, one per kind, are loaded by `PzSnd_LoadBank` 3924:0000 into their own
handle table DS:b146 (`code_puzzles_a.md` §2.4): 0x11cc electric, 0x11ca simple machines and
machine pieces, 0x11c8 gears, 0x11c6 balance, 0x11ce launch, 0x11d2 energy forms, 0x11d0 magnets.
The other banks by screen are listed in `code_sound.md` §15.

### 2.1 Cue ticks and the `L` record [O bytes and code; I intent]

A song slot can call a far function when its tick counter reaches a cue tick (s+0x07/+0x09, set
by 1bb4:0be5(slot, tick, callback)). The ending sets the cue of music bank 0x30ca (+ DS:a0d8)
to 0x250 = 592 ticks with callback 12e0:00ea (2fcb:0640); both arrangements (0x30ca/8 and
0x30cb/8) are exactly 592 ticks long, so the cue marks the end of the music.

The workshop (2dee:1a90) loads music bank 0x27 (speaker) / 0x28 (FM) and sets the cue to DS:9dc2
with callback 12e0:00bd. Those banks are the only ones with an `L` record starting with 4:
`04 8d 02` and `04 1b 05`, i.e. 4 followed by the song's length as a word at payload + 1 (653 and
1307 ticks, equal to the lengths of the two songs). 1cdd:0024 reads the word at payload + 2
instead, which takes the length's high byte and the type byte `'I'` of the next member: 0x4902
and 0x4905 (about 4.3 minutes) [O]. The looping song restarts its section (1bb4:0045 clears the tick counter) long before, so
**the workshop's cue never fires** and the workshop always plays arrangement 1 (`code_sound.md`
§14.2) [O].

The `M` members of a music bank are **lists of channels to mute** for an arrangement:
`SndRes_SelectArrangement(handle, n)` (1cdd:0423) unmutes all 16 channels, then, for n ≥ 1, mutes
the channels of the n-th member whose payload starts with 1 (`01 c c … FF`); n = 0 leaves every
channel playing (`code_sound.md` §13.3). A member whose payload starts with 3 would give
`SndRes_PlayMusic(handle, section, loop)` (1cdd:031a) its priority (none in the data, so 0). The
callbacks 12e0:00bd (`Music_WorkshopCue`) and 12e0:00ea (`Music_MovieCue`) switch the workshop's
and the movie's arrangements (`code_sound.md` §14). Hazard: list 4 of the speaker movie bank
0x30ca ends in FDh instead of FFh, so the mute loop runs on into the next member: the 4th music
pass is silent and 256 ticks longer (`code_sound.md` §13.4) [O]. The workshop's two arrangements
differ in length (653 and 1307 ticks), so the two devices do not play the same music note for
note [O].

---

## 3. The BMF format [O]

### 3.1 Layout

All words little-endian. The driver keeps its run-time state inside the loaded song, so several
header fields are written while it plays; the file holds their initial values.

```
+0x00  'B' 'M' 'F' 0x1A        checked by 1bb4:0ebc
+0x04  12 bytes, zero in all 495 songs
s = +0x10: the song block registered with the driver (offsets below are relative to s)
```

| s + | Size | Field | In the files |
|---|---|---|---|
| 0x00 | 1 | class: 0 PIT divisor list, 1 speaker, 2 PSG, 3 OPL2, 4 MPU-401 (interpreters DS:1aaa: 1bb4:087e, 0966, 0a1f, 0ad6, 0966) | 1 or 3 |
| 0x01 | 2 | delay countdown (run time) | 0 |
| 0x03 | 2 | tick counter (run time) | 0 |
| 0x05 | 2 | stream position, relative to s (run time) | 0 |
| 0x07 | 2 | cue tick: when the tick counter reaches it, the far function at +0x09 is called | 0 |
| 0x09 | 4 | far cue callback | 0 |
| 0x0d | 1 | loop flag | 0 |
| 0x0e | 16 x 4 | channel records: `u8 flags` (bit 7 = channel used by the song; bits 0 and 1 are set at run time: muted by the game, stopped), `u8 program` (0xFF = none yet; class 3), `u16 pitch bend` (cleared at a section start) | flags 0x80 or 0, program 0xFF, bend 0 |
| 0x4e | 1 | loop section | 0 |
| 0x4f | 1 | current section | 0 |
| 0x50 | 1 | transpose (copied from the section entry) | 0 |
| 0x51 | 1 | running status | 0 |
| 0x52 | 1 | n = number of sections | 1 in all songs |
| 0x53 | n x 4 | section entries: `u16 start` (relative to s), `u8 next section` (0xFF = none), `u8 transpose` | next 0xFF, transpose 0 |
| 0x53 + 4n | 1 | class 3 only: m = number of instrument patches | 1..11 |
| 0x54 + 4n | m x 2 | class 3 only: u16 offsets (relative to s) of 11-byte patches | |

### 3.2 Section stream

A section starts (1bb4:0045) with one delay byte that the driver skips (followed by a u16 if it
is 0xFF; it is 0 in all songs); the first event runs on the next tick. Then events, each
followed by a delay:

| Bytes | Class 1 (speaker, 1bb4:0966) | Class 3 (OPL2, 1bb4:0ad6) |
|---|---|---|
| `8c` | note off on channel c (no data byte) | note off |
| `9c k` / `9c k v` | note on, key k | note on, key k, velocity v |
| `Cc p` | (would be taken as a note on; does not occur) | program change: load patch p (if not already loaded) |
| `Ec lo hi` | pitch bend (u16); re-pitches a sounding note | pitch bend |
| `FF len16 t v ...` | meta event (1bb4:08ef): skip `len` payload bytes; type t = 1 with value v = the current section ends the section | same |
| a byte < 0x80 where a status is expected | running status: the previous status (s+0x51) with this byte as its first data byte | same |

* Delay after each event: one byte d; d = 0: the next event runs in the same tick; d = 0xFF: a
  u16 delay follows; else wait d ticks. Delays are in driver ticks (72.826 Hz).
* Note and bend events on a channel whose flags have bit 0 or 1 set are read but not played;
  a program change is still recorded (and its patch loaded unless bit 1 is set).
* At the end of a section the driver goes to the section's `next` entry; if there is none it
  loops to section +0x4e when the loop flag is set, else it stops the song (1bb4:084a).
* In class 1, status 0x90..0xDF all reach the note-on code with one data byte, so a program
  change would play a note; class-1 songs contain none. In class 3, 0x90..0xBF reach note on,
  0xC0..0xDF program change [O, interpreter bytes].

Every one of the 495 songs parses exactly to its end meta event `FF 02 00 01 00`; all but one
(0x105b/0) end exactly there, that one has 12 bytes after it [O, `bmf.py --check`]. These are
the "4 bytes after FF" the scoping report left open: a length word of 2 and the
end-of-section {1, 0}.

### 3.3 Observed ranges [O, `bmf.py`]

| | Class 1 | Class 3 |
|---|---|---|
| Songs | 237 | 258 |
| Bytes | 115..4347 | 132..9550 |
| Events | 3..1610 | 4..3089 |
| Channels used | 1..4 (channels 0..3 and 8) | 1..11 (channels 0..10) |
| Keys | 2..106 | 4..106 |
| Velocity | – | 0..171 |
| Programs | – | 0..10 (patch numbers) |
| Running-status events | 652 | 222 |
| Event counts | 5711 on, 3438 off, 118 bend | 9041 on, 8774 off, 642 program, 628 bend |

### 3.4 OPL patches (class 3) [O]

11 bytes, loaded by 1bb4:0270 when a channel's program changes:

| Bytes | Written to |
|---|---|
| 0..4 | first operator of the voice: registers 0x20, 0x40, 0x60, 0x80, 0xE0 + operator offset (register list DS:170d) |
| 5 | 0xC0 + OPL channel (feedback / connection): written for voices 0..6 (channel = voice), 8 (channel 8) and 10 (channel 7); not for voices 7 and 9 |
| 6..10 | second operator, same registers, for voices 0..6 only |

Voices (DS:16e5: per voice the indices of its one or two operators in the offset table DS:16fb)
[O tables]:

| Voice = MIDI channel | Operators (offsets) | OPL use [I from the standard OPL2 layout] |
|---|---|---|
| 0..5 | two each: 00/03, 01/04, 02/05, 08/0B, 09/0C, 0A/0D | melodic channels 0..5 |
| 6 | 10/13 | bass drum (channel 6) |
| 7 | 14 | snare drum |
| 8 | 12 | tom-tom (sets channel 8's C0) |
| 9 | 15 | cymbal |
| 10 | 11 | hi-hat (sets channel 7's C0) |

Rhythm mode is switched on by the driver's OPL init (BD register 0x20, register 01 = 0x20;
platform pass). The patch for a voice is loaded only when its program changes and the channel
is not stopped.

---

## 4. The driver at format level [O, platform pass and this pass]

| Item | Where |
|---|---|
| `Snd_Init(device)` 1bb4:1234: clears the 64 slots; detects the AdLib (timer test 1bb4:0142) only for device -1, skips detection for 1 and 8, and for any other device runs the detection and then **forces** the OPL flag to 1 (so `-SN` loads the FM banks and plays them silently); returns caps: bit 0 speaker, bit 1 PSG (DS:1683), bit 2 OPL (DS:1684), bit 3 MPU (DS:1685) | DS:1681..1685 = class available (1, 1, 0, 0, 0 in the file; the OPL entry is set by the detection or forced) |
| `-S` switch: N, '-' or none = off, I = speaker, A/B/M = FM; default -1 = detect | 303a:011c |
| Register a song: 1bb4:0ebc checks the magic and calls 1bb4:0e71, which takes the first free slot (far pointers DS:1458[64]) if the song's class is available (DS:1681[class] == 1); results: slot, -1 no slot, -4 class not available, -6 not BMF | |
| `Snd_Tick` 1bb4:0db3: for each active slot (DS:1558[64]): +3 tick counter += 1, call the cue callback when it equals +7; +1 delay -= 1; at 0 run the class interpreter (DS:1aaa[class]) | every 2nd PIT tick from the timer ISR 1454:0096 |
| Section start 1bb4:0045: delay = 1, tick = 0, clear the channel bends, transpose from the section entry, skip the initial delay byte | |
| Channels per class DS:1658 = 1, 16, 4, 11, 16 | |
| Speaker: note + transpose → PIT divisor table DS:1812..1831 (base entry DS:1816, semitones −2..13 of the base octave; divisor = T[n % 12] >> (n / 12)), pitch bend 1bb4:0541, port 61h gate | 1bb4:0966 |
| OPL: note on 1bb4:0343 (transpose, bend; the 128-entry tables DS:1712 = n / 12 and DS:1792 = n % 12; F-numbers DS:1852, 25 rows × 12, identical to the AdLib driver's `InitFNums` table), note off 1bb4:0302, volume 1bb4:0218 (scales the TL by the master DS:1686 and channel DS:1688 volumes and the velocity), `AdLib_Write` 1bb4:00a9 (6 and 35 status reads, the same delays as TM DOS) | 1bb4:0ad6 |
| Lock DS:165d (set while the game changes slots; `Snd_Tick` does nothing while it is 1) | |

Ctrl+S toggles sound (1cdd:05c8) by playing or stopping a looping priority-7Fh silence song,
effect 0 of 0x105b / 0x105c; dialogs and drop-downs pause the driver (1cdd:05db / 05e5). The full
driver specification is `code_sound.md`. Treasure Mountain DOS's
driver (TUNES/SFX, 236.7 Hz, per-device files) is a similar idea with a different format
(`re/dos/docs/fmt_sound.md`); TM Windows uses MIDI files and PCM, so nothing there applies.

---

## 5. The engine's port and its verification (wave 2)

### 5.1 Modules

| Engine file (`src/gg/audio/`) | Original | What |
|---|---|---|
| `bmf` | §3 | the song block's offsets and a parser that reads a song as the interpreters do (running status, the note-off without a data byte, the class 1 / class 3 status ranges, meta events) |
| `snd_driver` | 1bb4 | the driver routine by routine: 64 slots, `Snd_Tick`, sections, delays, meta events, cues, channel ownership, the speaker and OPL2 interpreters, class 0, pause, silence, `Snd_Off`; tables read from DGROUP (`SndTables`), state at the original offsets of the loaded bank copy |
| `snd_res` | 1cdd | handles, the three loaders, play/stop, arrangements, the sound switch, pause |
| `music_cues` | 12e0:007f / 00bd / 00ea and their callers 1622:0346, 1622:03b6, 2dee:1a90, 2dee:17eb / 185d, 2fcb:0640 | room, workshop and movie music |
| `pz_snd` | 3924 (ovl19) | the puzzles' bank and raw-priority calls |
| `gg_audio` | `Init_CommandLine` step 7, `SndRes_Init` | the start-up per sound mode, the core's `ChipRenderer` / `ChipAudioDevice` / `OplChip`, logs |
| `soundtest`, `dro_compare`, `audio_cli` | – | schedules, the DOSBox-X capture model, the tools |

The driver is a `dos::SoundTickSource`: the core's renderer ticks it at
`PitTimeBase(0x2000).hz() / 2` = 72.826 Hz by sample count (the SDL callback), or the caller ticks
it (`GgAudio::advanceTo`, for deterministic runs and when no audio device opens). Game calls run
with the renderer locked, between two ticks.

### 5.2 Write timing [O]

One `AdLib_Write` (OUT, 6 status reads, OUT, 35 status reads) takes **65 µs** on the reference
machine (DOSBox-X, 486 at 26 800 cycles): with the writes of a tick at `t0 + 0.065 ms · k` (k = the
writes before it in its tick, logged or not), only 22 of the 4224 inside-tick writes of
`w0_title_attract.dro` and 23 of the 3585 of the replay get another whole-ms stamp than DOSBox-X
gave them (a grid search over the offset and 0..120 µs; 64 µs and 66 µs do slightly worse). TM DOS
measured 70 µs at 6000 cycles, so DOSBox-X's port I/O cost dominates, not the CPU speed.
`GgAudio::kOplWriteSeconds` and `OplChip`'s write spacing use it.

### 5.3 What a DOSBox-X `.dro` records [O]

`dro_compare.h` applies these rules to the engine's write log (they agree with all 13 captures):

* The file opens at the first key-on after the capture was requested: a write to B0h..B8h with
  bit 5, or to BDh with `value & 3Fh` > 20h. A capture therefore starts inside the tick of that
  key-on.
* First comes DOSBox-X's register cache: every non-zero register in ascending order, B0h..B8h
  without their key bit, BDh without its five drum bits. Then the key-on, then only the writes that
  change a register, in whole milliseconds. The timer registers 02h..04h are never cached (the
  emulated chip consumes them).
* So a capture made after screens the run does not reproduce shows them only in its dump: the
  comparison can start its "does it change a register" filter from the capture's dump
  (`--base dump`), and the driver's own memory of those screens, the B0h shadows that
  `Opl_NoteOff` writes back, can be seeded from it (`--seed-dro`). The dump differences are
  reported, not counted.

### 5.4 The captures reproduced [O]

Each capture has a named schedule (`soundtest.cpp`; `--gg-soundtest-check NAME|all`): the game's
sound calls at sound ticks of an engine run, found by matching the capture write for write (the
calls follow the code of `code_sound.md` §15; times, and priorities that a channel conflict
reveals, come from the capture). Result: identical writes (register and value, in order) from the
capture's first key-on, and the writes made inside ticks that lie within half a tick of the
13.731-ms grid.

| Schedule | Capture | Calls | Identical | On the grid |
|---|---|---|---|---|
| `title` | `w0_title_attract` | attract: effects 0x1022, music 0x1020; title animation effects 12 (tick 80), 13 (301); screen-animation effect 0 (789), stopped (885) | 4225 / 4225, dump 99 / 99 equal | 4224 / 4224 |
| `title_replay` | `w0_replay_title_attract` | the same, effect 13 at 300 (the second boot) | 3586 / 3586, dump 68 / 68 | 3585 / 3585 |
| `hub` | `r2_hub_idle` | music 0x1020, arrangement 2 | 1521 / 1521 | 1521 / 1521 |
| `postrace` | `r2_postrace_to_hub` | the same | 549 / 549 | 549 / 549 |
| `warehouse` | `r2_warehouse_walk` | room music 0x361 arrangement 1 with the chimp layer, effects 0x1039: 15, 17, 15 | 1186 / 1186 | 1186 / 1186 |
| `workshop` | `r2_workshop_idle` | `State_06_Workshop`'s start, 15 belt and race-door effects | 669 / 669 | 659 / 659 |
| `puzzle_k4` | `r2_puzzle_k4_success` | `PzSnd` bank 0x11c6: 0, 1, the swing (effect 8 transposed 0..4), 5 and 13 ×4 | 356 / 356 | 356 / 356 |
| `puzzle_k5` | `r2_puzzle_k5_miss` | `PzSnd` bank 0x11ce: 0, 1, 4, 7, 2 | 162 / 162 | 162 / 162 |
| `race_automotive` | `r2_automotive_race1_won` | workshop music freed, race music 0xF7 (arrangements 1, 2), results | 1421 / 1421 | 1366 / 1366 |
| `race_altenergy` | `r2_altenergy_race1_won` | the same with 0xF9 (1, 2, 3, 2) | 2899 / 2899 | 2831 / 2841 |
| `race_aircraft_won` | `r2_aircraft_race1_won` | music 0x262 (1, 2, 3, 2), effects 0x260 as the path's code −13 gives them (loop y >> 8, priority y & 0xFF): 8 (0, 1), 7 (1, 2), 9 (0, 3) (wave 2 wrote all three at priority 3; the writes are the same, nothing competes); results | 2685 / 2685 | 2617 / 2617 |
| `race_aircraft_lost` | `r2_aircraft_race1_lost` | engine effect 7 looping, 9; results of a loss (music 0x108B, no count-up) | 2598 / 2598 | 2579 / 2579 |
| `movie` | `r2_movie_race15` | race 15, results, `State_15_FinalMovie` (music 0x30CA with `Music_MovieCue`, the movie cue effects of 0x108D) | 7050 / 8320 (to the movie's end) | – |

The `title` captures are compared against the engine's own registers (the run starts with the
game); the others start from the capture's dump, and the race and movie runs also seed the B0h
shadows (§5.3). The results screens follow one pattern in all four races: results music at P
(0x1089 won, 0x108B lost), global effect 1 at P and, while the score counts up, every 2 ticks from
P + 19 (49 times; on odd PIT ticks in the Alternative Energy run), effect 0 of 0x108F at P + 121 and
effect 1 at P + 381 (the rank-up box). The writes off the grid: six in the first 90 ticks of
`race_altenergy` (the workshop music at the capture start: some of its drum note-offs one tick
late in the capture, not explained), and race-phase writes whose call times the order fixes only
to a tick.

### 5.5 Driver behaviour the captures confirm [O]

* **Equal priority holds the channel; the release hands it over with its patch.** In the
  warehouse effect 15 starts while effect 17 holds channel 0 at the same priority: its program
  change is recorded but no patch is loaded; when 17 ends, `Snd_ReleaseChannels` gives the channel
  to 15, `Snd_ApplyChannelProgram` reloads its patch (the capture shows the patch writes without a
  note), and 15 sounds from its next note-on. The results' effect 0 under the count-up shows the
  same.
* **A higher priority re-takes its own channels and reloads their patches**: the race-door
  animation 0x1d plays effects 13 and 14 with priority byte 3 (`ScreenAnim` op −36), then
  `Workshop_UpdateRaceDoor` plays 14 again at 4: only the carrier volumes changed by the notes are
  rewritten.
* **A restart cuts the sounding channels** (`Snd_Play` on an active slot: B0h without the key bit
  in the call, the notes again at the next tick: the workshop belt's effect 6).
* `Music_MovieCue` switches the movie's arrangement at every 592-tick pass, and the ordinary loop
  of the music goes on through four passes, write for write.
* The `M`-list arrangements, the room music's chimp layer (channel 4 at level 1), the puzzles'
  raw priorities and `PzSnd_SetTranspose` steps behave as specified.

### 5.6 The PC speaker [O]

`r2_si_boot_to_building.wav` (DOSBox-X, `-si`): notes are taken as runs of rising edges at half
the square's 3300 level (at least 2 periods; divisor = 1 193 182 / frequency). The `title` schedule
on the speaker (`--gg-sound speaker soundtest title ... --speaker-log`) against it: 223 of the 233
engine onsets that last at least 2.5 periods are found within 3 ms (plus one period, the detector
needs a rising edge) and 1 % of the divisor, over the first 42 s, once ticks 170..428 are
silenced: there the capture holds only a few notes (this run left the attract with Space; that
path was not reconstructed). The unmatched 10 lie where the screen animation's effect 0 plays
(other timing in this boot). Divisors are the driver's to the unit (1140, 3044, 2280, 380, ...).

### 5.7 Deviations [R]

* Game calls never overlap a sound tick, so no tick is lost to `g_sndBusy` (the tools' `L`
  command reproduces one).
* `Snd_AllocSlot` refuses classes above 4 (the original would index past DS:1685); song-memory
  accesses outside the bank copy are dropped; a handle beyond `g_sndTable[60]` throws; a cue with a
  NULL callback is not called (no shipped song gets there, `code_sound.md` §5).
* The PSG (class 2) and MPU-401 (class 4) paths, the tempo scaler and the volume setters are not
  ported (nothing reaches them).
* `AdLib_Detect`'s status reads are the emulated card's presence flag (the same writes).

### 5.8 Porting notes [R]

* Choose the arrangement exactly as the game does: id + 1 when an OPL is present, else id; an
  effect slot whose speaker member is `C FF` stays silent on the speaker.
* The game's call order decides the slot order (and so the order of writes inside a tick): the
  room music is loaded before the warehouse effects, the race music before the race effects.

## 6. Open questions

Wave 1 (`code_sound.md`) answered the wave 0 questions; wave 2 verified the driver against every
capture (§5.4). Still open:

* Why 0x105b/0 (the silence song) has 12 bytes after its end event.
* Whether data-driven effect priorities (screen animations, actors, races) can exceed 40h, which
  would let an effect past the sound-off silence song (`code_sound.md` open questions). The race
  door's animation uses 3.
* The six one-tick-late drum note-offs at the start of `r2_altenergy_race1_won` and the end of the
  movie capture (the last effect one tick late against the music; §5.4).
* The silent stretch of the `-si` capture at 3.3..6.9 s (§5.6).
