# DOS edition: music and sound effects (TUNES.IBM/.TAN, SFX.IBM/.TAN)

Scope: the four sound files of the 1990 DOS edition (`TM-DOS\`) and the sound driver in
`SST.EXE` that plays them. The driver supports PC speaker, Tandy 1000 / PCjr PSG,
IBM PS/1 Audio Card PSG and AdLib (OPL2). This document gives the file format, the event
language, the exact per-tick driver algorithm and timing, and how the four output devices
are driven. It is a scoping spec for a DOS "edition backend" in the engine.

Evidence: Ghidra export `re\dos\ghidra_export\` (addresses `SSSS:OOOO` as in that export).
Code that Ghidra had not disassembled (timer ISR `1000:0428`, sound tick `491b:0004`, the
command-line switch table `1460:04c2`, the stubbed network check `1460:05a3`) was listed
with a private headless project (`extracted\scratch\dos_data\ghidra\`, script
`DisRange.java`). DGROUP bytes were read from `extracted\SST_unpacked.EXE`
(DGROUP = Ghidra segment `4d94` = load-image offset `0x3d940`, file offset `0x3fb30`).
The decoder/renderer is `tools\dos\dos_sound.py`; its output is in
`extracted\previews\dos\audio\`.

Labels: **[obs]** = read from bytes, disassembly or decoder output. **[inf]** = inference.

---

## 1. Summary

| File | Size | Header | Entries | Voices/entry | Played on |
|---|---|---|---|---|---|
| `TUNES.IBM` | 3152 | 0x96 bytes = 75 u16 slots | 3 tunes | 1 | PC speaker |
| `TUNES.TAN` | 5000 | 0x230 bytes = 70 x 4 u16 slots | 3 tunes | 4 | Tandy / PS/1 PSG, AdLib |
| `SFX.IBM` | 1680 | 0x96 bytes = 75 u16 slots | 12 effects | 1 | PC speaker (also on AdLib machines) |
| `SFX.TAN` | 1622 | 0x230 bytes = 70 x 4 u16 slots | 12 effects | 4 | Tandy / PS/1 PSG |

* Each entry is a set of u16 offsets (relative to the file start) to **event streams** of
  2-byte events: `[octave<<4 | note] [duration or parameter]` [obs].
* One driver (segment `491b`, ~2.2 KB of hand-written assembly) runs a fixed-tick sequencer
  at **1193182 / 5041 = 236.70 Hz** from the reprogrammed timer interrupt [obs].
* Notes are equal-tempered semitones from period tables in DGROUP; durations are
  `tempo >> n` ticks (n = 0..7, optional dotted), so `tempo` = ticks per whole note [obs].
* Tandy voices have **volume envelopes** (36 attenuation sequences in DGROUP), a release
  time and sustain level; the speaker has only on/off with a release gap [obs].
* All 3 tunes and 12 effects exist in both forms. The .TAN tunes are separately arranged
  (e.g. tune 0 is a whole tone higher and starts with a one-bar rest) [obs].
* The AdLib path plays **TUNES.TAN** through an OPL2 instrument driver (instrument data in
  DGROUP, 4 random orchestrations per tune) and plays effects from **SFX.IBM on the PC
  speaker** [obs]. The driver (segment `4ac9`) is specified register by register in §5.5
  (wave 5).
* Verified (§11): the engine's driver (`src/dos/audio/`) renders all 30 entries sample for
  sample like `tools/dos/dos_sound.py`, and its timing, pitches, levels and the Tandy noise
  register agree with the DOSBox-X recordings of all 12 effects and 3 tunes [obs]. Its AdLib
  driver writes the original's OPL register stream write for write, at the original's PIT
  ticks, for every tune in every orchestration (§11.7).

---

## 2. Device selection

### 2.1 Detection (`1000:04a1`, called from start-up `1460:0003`) [obs]

`DS:63bc` (the sound device) starts at 0 and is set by the first test that succeeds:

| Order | Test | Device `DS:63bc` |
|---|---|---|
| 1 | BIOS machine id `F000:FFFE` = 0xFF and byte `F000:C000` = 0x21 (Tandy 1000 signature; same test sets `DS:1300` = 0x21) | 1 Tandy PSG |
| 2 | `4ac9:0d0f` AdLib timer test on port `DS:656c` (= 0x388): timer status & 0xE0 is 0 before and 0xC0 after starting timer 1 | 2 AdLib |
| 3 | `INT 15h AH=C0h` model bytes = FC 0B (IBM PS/1 model 2011) and port 0x203 echoes 0xA5 ten times | 3 PS/1 Audio Card |
| – | otherwise | 0 PC speaker |

### 2.2 Command-line override (`1460:0463`, table `1460:04c2`) [obs]

Arguments starting with `-` or `/` are matched by their second character against a
14-entry word table in the code segment (`C E H M S T X c e h m s t x`), with handlers at
`1460:04fa..0558`:

| Switch | Effect |
|---|---|
| `-C` / `-E` / `-T` / `-H` / `-M` | video adapter `DS:912c` = 2 (CGA) / 1 (EGA) / 3 (Tandy) / 4 (Hercules) / 5 (MCGA) |
| `-X` | toggles `DS:923c` (set at start-up to 1 on an 8086/8088 by the `SHR BX,32` test in `1000:04a1`, else 0) [inf: slow-CPU flag] |
| `-S<c>` | sound device: `<c>` is looked up in `"iItTaAsS"` (DS:0620); the device is byte `DS:0629 + index` = `0 0 1 1 2 2 2 2`, i.e. `i` speaker, `t` Tandy, `a` AdLib, `s` AdLib [inf: `s` = Sound Blaster FM]. PS/1 (3) cannot be chosen by switch. |

The switches are parsed after detection, so they override it.

### 2.3 Which files are loaded (`1460:022a`) [obs]

| Device | Effects file → buffer `415f:0000` (`DS:63a4/63a6`) | Music file → buffer `41ca:0000` (`DS:63a8/63aa`) |
|---|---|---|
| 0 speaker | `SFX.IBM` | `TUNES.IBM` |
| 1 Tandy | `SFX.TAN` | `TUNES.TAN` |
| 2 AdLib | `SFX.IBM` | `TUNES.TAN` |
| 3 PS/1 | `SFX.TAN` | `TUNES.TAN` |

Files are opened through the game file table (`DS:0476`, indices 10..13, see
`fmt_data.md` §1) and read whole with a 9999-byte limit (`1460:07b4`). The buffers are
static: segment `415f` holds 1712 bytes (CODE_14 in the Ghidra segment list), `41ca` holds
5056 bytes (CODE_15). The largest files (1680 and 5000 bytes) fit [obs].

### 2.4 Driver initialisation (`491b:067b`, after the files are loaded) [obs]

* Block size `DS:63c4` = 0x78.
* Speaker: voices `DS:63be` = 1.
* Tandy: port `DS:656c` = 0xC0, voices = 4, and `491b:0575` sets port 0x61 bits 5-6 (route the
  SN76496 to the speaker; PCjr/Tandy sound multiplexer).
* PS/1: port = 0x205, voices = 4, **device rewritten to 1**: from here on the PS/1 is treated
  exactly as a Tandy PSG on another port.
* AdLib: voices = 4, `4ac9:00b7` initialises the OPL2 (§5.5: 40 register writes, no instrument yet).

---

## 3. Timing

* `1000:03c5` hooks INT 8 with `1000:0428` and programs PIT channel 0 with mode 3, divisor
  **0x13B1 = 5041**: 1193182 / 5041 = **236.696 Hz**, one tick = 4.225 ms. It also writes
  0xB6 to port 0x43 (channel 2, mode 3, lo/hi) once, for the speaker [obs].
* The ISR increments a 32-bit counter (`DS:3d2b`), chains to the BIOS INT 8 handler every
  13th tick (`DS:3d33` counts 12..0; 236.7/13 = 18.2 Hz), and calls the sound tick
  `491b:0004` on **every** tick when `(music status & DS:37ce) | sfx status` is non-zero. Every
  5th tick it also runs `1000:31ee` (game timer work, not sound) [obs].
* `1000:0407` restores the BIOS vector and divisor 0 on exit [obs].
* Tempo: command `tempo v` sets `tempo = 4*v` ticks per whole note. The tunes use v = 112..125,
  i.e. a quarter note of 112..125 ticks = 0.47..0.53 s (113..127 BPM) [obs from data].

---

## 4. File format

### 4.1 Header [obs]

* u16 array at offset 0. Its byte length is the first element (all real offsets point past
  the header): 0x96 for `.IBM`, 0x230 for `.TAN`.
* `.IBM`: entry *i* = `u16[i]` → one stream. `.TAN`: entry *i* = `u16[4i .. 4i+3]` → streams for
  voices 0..3 (voice 3 = the PSG noise channel on Tandy). The driver indexes with
  `id << 1` or `id << 3` depending on the device (`491b:0791`, `491b:06d7`).
* The entry count is **not stored**. Slots after the real entries hold stale data: in both
  TUNES files, slots 3..11 are a copy of the SFX header, and the `.TAN` header area between
  0x60 and 0x230 contains leftover event bytes from an older file. Valid entries are the
  leading run of ascending, in-range offsets (3 tunes, 12 effects); the game only ever asks
  for tunes 0..2 and effects 0..11 (§7).

### 4.2 Events [obs, `491b:01ee`]

Every event is two bytes `b0 b1`. `oct = b0 >> 4`, `note = b0 & 0x0F`.

| `note` | Meaning | `b1` |
|---|---|---|
| 1..12 | play semitone `note` (1 = C .. 12 = B) in octave `oct` | duration code |
| 0 | rest (note off) | duration code |
| 0x0D | command number `oct` (takes no time) | parameter |
| 0x0E | raw-pitch event, lasts `rawdur` ticks | pitch parameter (0 = silence) |
| 0x0F | end of this voice (`0xFF 0xFF` in the files); the pointer stays on it | – |

A voice whose first byte is 0xFF is inactive from the start (`491b:0136`). Events are
fetched until one sets a non-zero duration, so any number of commands can precede a note.

**Duration code** `b1` (`491b:02d8`), with `T = tempo + tempo_adj` (16-bit):

* bit 7 set: duration = `T` (the low 7 bits are ignored). Not used in the shipped files.
* else: duration = `T >> (b1 & 7)`; if bit 3: `+= duration >> 1` (dotted); bit 4 = tie (see §5.3).
  So 0 = whole, 1 = half, 2 = quarter, 3 = eighth, 4 = sixteenth, 5 = 1/32.
* A duration of 0 makes the note take no time (the next event is fetched at once).

**Commands** (`note` = 0x0D, `491b:03fe`):

| `oct` | Name | Effect |
|---|---|---|
| 0 | release | `b1` = 0: legato on (no release gap). Else legato off, `release = b1` ticks. Default 6. |
| 1 | tempo +% | `tempo_adj = round(tempo * b1 / 100)` (rounds up when the remainder > 49); 0 clears |
| 2 | tempo -% | same, negated |
| 3 | rawdur | duration in ticks of each raw-pitch event (default 1) |
| 4 | tempo | `tempo = 4 * b1` |
| 5 | envelope | Tandy: envelope = table entry `b1` (DS:656e, §6.2) |
| 6 | sustain | Tandy: attenuation (0..15) held after the envelope ends |
| 7..15 | – | ignored |

`tempo` and `tempo_adj` are shared by all voices of the entry; the other settings are per
voice. Tunes use commands 0, 1, 3, 4, 5, 6; the end of tune 1 slows down with tempo +20%,
+30%, +40% [obs from data].

**Pitch** (per device, `491b:025d`):

* Speaker: PIT channel 2 divisor = `SPK[note-1] >> oct`, `SPK` = u16[12] at DS:6538 =
  36488 .. 19329 (36488 = 1193182 / 32.70 Hz = C1). Octave nibble *o* therefore sounds as
  scientific octave *o + 1*; `0x31` = C4 = 261.6 Hz.
* Tandy/PS/1 tone voices 0..2: period `N = PSG[note-1] >> max(oct-1, 0)`, then halved once if
  `N > 0x3FF`. `PSG` = u16[12] at DS:6550 = 1720 .. 911, so `0x31` gives 3579545 / (32·430) =
  260 Hz. Octaves 0 and 1 give the same pitch, and periods above 1023 (the chip's 10-bit
  limit) jump up an octave.
* Tandy voice 3 (noise): writes `0xE0 + note - 1` to the PSG noise register: notes 1..4 =
  periodic noise, 5..8 = white noise, with shift rate N/512, N/1024, N/2048 or "tone 3"
  (= follow voice 2's frequency). SFX.TAN entry 1 uses white noise clocked by an inaudible
  voice 2 (envelope 35 = silent).
* AdLib: semitone number `note - 1 + 12*oct` (8-bit) goes to the OPL driver (§6.4).

**Raw-pitch event** (`note` = 0x0E, `491b:04ce`), `v = b1`:

* `v` = 0: note off, rest.
* Speaker: divisor = `(36488 - 128*v) mod 65536 >> oct` (base = first word of the table
  that DS:6568[device] points to).
* Tandy: `k = (3*v >> 2) + 1`; `c = oct or 1`; `N = (0xD70 >> c) - (k << (4-c))` for c < 4,
  `- k` for c = 4, `- (k >> (c-4))` for c > 4 (16-bit); `N >>= 1` if 0x400 < N < 0x8000.
  The second PSG byte written is `(N >> 4) & 0xFF`, not masked to 6 bits.
* AdLib: only marks a new note (no sound).
* Then the voice waits `rawdur` ticks. Effects build sweeps and noise bursts from runs of
  raw events (SFX.IBM entry 1 has ~200).

---

## 5. Driver algorithm

### 5.1 State blocks [obs]

Two 0x78-byte blocks: **sfx** at DS:6448, **music** at DS:64c0. Each tick the active block
is copied to the work area DS:63ce, processed, and copied back (`491b:00be`). Work-area
layout (4 voices, u16 each unless noted):

| DS | Field | DS | Field |
|---|---|---|---|
| 63ce | status: 0 idle, 1 start requested, 2 playing | 63fe | legato[4] |
| 63d0 | id (effect/tune number, 0xFFFF idle) | 6406 | release[4] |
| 63d2 | tempo (ticks per whole note) | 640e | envelope start[4] (near ptr) |
| 63d4 | tempo_adj (signed) | 6416 | envelope position[4] |
| 63d6 | stream pointer[4] (offset in the file buffer) | 641e | envelope done[4] (0 running, 1 sustain) |
| 63de | stream start[4] (for looping) | 6426 | sustain attenuation[4] |
| 63e6 | ticks left[4] | 642e | new-note flag[4] |
| 63ee | rawdur[4] | 6436 | rest flag[4] |
| 63f6 | active[4] (0 or 2) | 643e | tie state[4] |

Other driver variables: 63a4/63a6 sfx buffer, 63a8/63aa music buffer, 63b4 sound on, 63b6
music on, 63b8 sfx loop flag (set only by the end sequence: player states 44/45/64/65, `code_player.md` §6.3), 63ba music loop flag (set to 1 at `1437:0204`,
`4ba4:0469`), 63bc device, 63be voice count, 63c0 1 = processing music, 63c2 block pointer,
63c6[4] voice-owned-by-effect mask (cleared every tick), 63ac[4] AdLib pitch, 6538/6550
period tables, 6568 table pointers, 656c port, 656e envelope pointers. DS:6446..6447 lie between
the 0x78-byte work copy (DS:63ce..6445) and the effect block DS:6448; no use of them was found
(unexplained, noted in wave 2).

### 5.2 One tick (`491b:0004`) [obs]

1. Clear the voice-owned mask.
2. If an effect is active (sfx status ≠ 0):
   * AdLib: temporarily switch to device 0 with 1 voice, run the sfx block on the **speaker**,
     switch back; then run the music block only if `DS:37ce` ≠ 0.
   * Other devices: run the sfx block and **stop** — music is frozen while an effect plays.
3. Else, if music status ≠ 0: (AdLib: 4 voices) run the music block.

Running a block (`491b:00be`): if status ≠ 2, **init** (`491b:0136`): status = 2,
tempo_adj = 0, and for each voice whose first byte ≠ 0xFF: active = 2, ticks left 0,
rawdur 1, legato 0, rest 0, tie 0, release 6. Then the **voice pass** (`491b:0181`):

```
for each voice v < voice count:
    if not active[v]: continue
    if ticks_left[v] == 0:
        fetch events (section 4.2) until ticks_left[v] != 0 or the voice ends
        if not active[v]: continue
    ticks_left[v] -= 1
    if device != Tandy and not legato[v] and ticks_left[v] == release[v] - 1:
        note off (speaker: port 0x61 &= 0xFC; AdLib: key off)
if all four active flags are 0: stop (status 0, id 0xFFFF, note off on every voice)
elif device == Tandy: attenuation pass (5.3)
```

If the block stopped and its loop flag is set (music: 63ba; effects: 63b8), the stream
pointers are reset to their starts and init + voice pass run again in the same tick.

Consequences: a note of D ticks holds the speaker on for `D - release` ticks and off for
`release` ticks; if `D < release` it sounds for the full D (no gap). A pitch is written to
the hardware at the tick the note is fetched.

The voice-owned mask (DS:63c6) is cleared at the start of every tick and, for the music run on
the speaker and the Tandy, loaded from the effect block's active flags (491b:008a). The music
only runs when no effect is active, and both `Sound_AllVoicesDone` and `Sfx_Stop` clear those
flags, so the mask is never non-zero in practice [obs].

### 5.2a Entry points called by the game (491b:06d7..0854) [obs, disassembly]

| Routine | What it does (beyond §7) |
|---|---|
| `Sfx_Play` 06d7 | returns if `g_soundOn` (DS:63b4) is 0; AdLib: voice count = 1; ignores the call if `id > current id` (unsigned; idle = 0xFFFF); stores the id; **Tandy: calls `Sfx_Stop`, which sets the current id back to 0xFFFF, so on the Tandy (and PS/1) there is no priority: every effect replaces the running one**; sets the stream pointers and starts, status 1. **The block is not cleared**: tempo, envelope pointers and sustain of the previous effect remain until the new streams set them (every shipped effect sets its tempo and envelopes first, so it is not audible) |
| `Sfx_Stop` 074b | status 0, id 0xFFFF; speaker and AdLib: `port 0x61 &= 0xFC` directly and active[0] = 0; Tandy: for each voice active = 0, owned = 0, `Sound_VoiceOff` (attenuation 15 + period 0) |
| `Music_Play` 0791 | **zeroes the whole music block**, stores the tune id, then returns if `g_musicOn` (DS:63b6) is 0 (the block stays cleared: the music is stopped); AdLib: `AdLib_PlayTune(t)` (**one `rand()` per call**, which shifts the game's random sequence) and voice count 4; pointers; status 1. A call while the same tune plays restarts it. On AdLib status 1 comes only after `AdLib_PlayTune`'s register writes, so the timer interrupts during them play no music (§5.5: 1 or 2 interrupts on the reference machine) |
| `Music_Stop` 0811 | status 0, id 0xFFFF; voice count 4 on AdLib (temporarily); for each voice active = 0, owned = 0, `Sound_VoiceOff`. On the speaker this also cuts a playing effect's current note (the gate is shared) until its next note |
| `Sound_SilenceAll` 0854 | `Sound_VoiceOff` on every voice (4 on AdLib, where it keys off the OPL voices, not the speaker) |

Side effect of `Sound_VoiceOff` on the PSG: it writes period 0 (491b:0600). When an effect ends
on the Tandy (`Sound_AllVoicesDone` turns all four voices off) the frozen music resumes with its
attenuation restored by the next volume pass, but each voice keeps period 0 (= 1024, about
109 Hz) until its next note [obs code; not captured in a recording].

### 5.3 Tandy attenuation pass (`491b:033d`) [obs]

For voices 0..3, attenuation register latch `0x90 + 0x20*v`, skipping voices owned by an
effect or inactive:

1. If new-note: clear it. If tie < 2, restart the envelope (position = start, done = 0);
   else if tie ≠ 3, set tie = 0 (a tied-to note keeps the running envelope). Go to step 5.
2. Else if rest: write attenuation 15 (silent). Next voice.
3. Else if tie = 1 or 3, or legato: go to step 5.
4. Else if ticks left ≤ release (signed): write 15. Else go to step 5.
5. Envelope: if done = 0, read the next byte; 0xFF sets done = 1 and writes nothing this
   tick; otherwise write it. If done = 1 write the sustain value. The value is added to the
   latch byte without masking.

Tie state (`643e`), updated by each duration code: bit 4 set: 0→1, 1→3, 2→3, 3 stays 3;
bit 4 clear: 0 stays, otherwise +1. So a tied note does not release, and the note after it
continues the envelope instead of restarting it.

Note on/off on the PSG: rest or end writes attenuation 15 and period 0 (`491b:05b8`).
Unlike the speaker, Tandy releases one tick earlier (step 4 does not run on the new-note
tick), and a note of D ticks sounds for `D - 1 - release` ticks (at least 1).

### 5.4 Envelopes (DS:656e) [obs]

36 near pointers to 0xFF-terminated byte strings of attenuation values (0 = loudest, 15 =
off, 2 dB steps), stored at DS:65c3..6bbd (constant levels first, the long decays in the middle):

| Index | Length | Shape |
|---|---|---|
| 0 | 29 | attack 14, 9, 5, 0 then decay to 4 |
| 1, 2 | 585 (same data) | long decay 10, 0, 1 ... |
| 3 | 29 | decay 3 → 15 |
| 4, 8 | 47 / 43 | stepped decays (echo-like) |
| 5 | 22 | 0, 4, 6, 8, 10 x16, 12, 14 |
| 6 | 542 | long decay from 12 |
| 7 | 68 | 6, 4, 0 ... slow decay |
| 9 | 1 | 0 |
| 10..17 | 4-5 | short attack shapes |
| 18 | 16 | swell 14 → 11 |
| 19 | 2 | 15, 15 |
| 20..35 | 1 | constant level 0..15 |

Tunes mainly use 6, 35 (silent: rests between phrases) and 2; effects use 20 (full
volume), 35 and 2. A voice with no envelope command would read from DS:0000 (RTL data);
no shipped stream does this (checked by `dos_sound.py`).

### 5.5 AdLib path (segment `4ac9`, Turbo C) [obs, wave 5]

The driver is a cut-down AdLib sample driver (the "timbre" records, `SetVoiceTimbre`,
`SndSetAllPrm` and friends of the AdLib programmer's toolkit), melodic mode only (the percussion
code is gone), with the game's own play/note entry points. The table gives the role found in the
disassembly; the canonical names follow it since the wave-5 integration (18 functions renamed: the
older names, e.g. AdLib_SetTempo for 054a, are listed in `re/names/dos/README.md`, and the driver's
variables DS:C6E4..CB7C, 8148, 815A are named in `re/names/dos/dgroup.tsv`, g_opl*):

| Address | Canonical name | Role (sample-driver equivalent) | Called |
|---|---|---|---|
| 0002 | AdLib_PlayTune | orchestration k = 4 * tune + (rand() & 3): instruments and transpositions | Music_Play 491b:07c9 |
| 00b7 | AdLib_Init | the initialisation below | Sound_Init 491b:06a9 |
| 0103 | AdLib_SetMode | SetMode(0): only writes 0xBD | AdLib_Init |
| 010d | AdLib_SetWaveSel | SetWaveSel(on): every slot's 0xE0+ = 0, then 0x01 = 0x20 | AdLib_Init |
| 0150 | AdLib_SetPitchRange | SetPitchRange (1..12; range * 25 kept for the pitch bend) | AdLib_Init |
| 0179 | AdLib_SetGlobalParams | SetGParam(AM depth, vibrato depth, note select): 0xBD, 0x08 | AdLib_Init |
| 019a | AdLib_SetVoiceTimbre | SetVoiceTimbre(channel, record) | AdLib_PlayTune |
| 029e | AdLib_NoteOn | note on, voice v -> channels 3v..3v+2 | Speaker_On 491b:05a3 |
| 0432 | AdLib_NoteOff | note off, same groups | Sound_VoiceOff 491b:05e2 |
| 04ef | AdLib_NoteOffVoice | one channel's note off (the game's own, below) | AdLib_NoteOff |
| 054a | AdLib_InitSlotVolume | InitSlotVolume: the 18 slot volumes = 0x7F | AdLib_Init |
| 055c / 0601 / 067e | AdLib_CalcFNum / SetFNumRow / InitFNums | CalcPremFNum / SetFNum / InitFNums | AdLib_Init |
| 081a | AdLib_SetSlotParam | SetASlotParam(slot, parameter, value) | AdLib_NoteOffVoice |
| 0847 | AdLib_SetSlotParams | SetSlotParam(slot, 13 parameters, wave) | AdLib_SetVoiceTimbre |
| 08db | AdLib_WriteParam | SndSetPrm(slot, parameter): jump table 4ac9:08f3 | AdLib_SetSlotParam |
| 095e | AdLib_WriteAllParams | SndSetAllPrm(slot) | AdLib_SetSlotParams |
| 099c | AdLib_WriteKslLevel | SndSKslLevel: 0x40 + slot offset | |
| 0a0e | AdLib_WriteNoteSel | SndSNoteSel: 0x08 | |
| 0a29 | AdLib_WriteFeedbackFm | SndSFeedFm: 0xC0 + channel (modulator slots only) | |
| 0a86 / 0ad6 | AdLib_SetAD / AdLib_SetSR | 0x60 / 0x80 + slot offset | |
| 0b26 | AdLib_WriteAmVibEgKsrMulti | SndSAVEK: 0x20 + slot offset | |
| 0bd6 | AdLib_WriteAmVibRhythm | SndSAmVibRhythm: 0xBD (AM and vibrato depth bits only) | |
| 0c03 | AdLib_SetWave | 0xE0 + slot offset | |
| 0c44 | AdLib_SetFreq | SetFreq(channel, pitch, key) | |
| 0ce8 | AdLib_KeyOff | SoundChut: 0xB0 + channel = 0, then 0xA0 + channel = 0 | |
| 0d0f | AdLib_Detect | the timer test of §2.1 (registers 2 and 4 only) | Hw_Detect 1000:04a1 |
| 020f, 0251, 0271, 071d, 089d | AdLib_SetVoiceA/B, AdLib_SetReg, AdLib_PitchBend, AdLib_SetPercInstr | SetVoiceVolume, ..., SetVoicePitch, the percussion timbre | **never called** |

Every register write goes through `AdLib_Write` 491b:0875: OUT register to port DS:656C (0x388),
6 status reads, OUT value to the next port, 35 status reads.

**Variables** (BSS of DGROUP): C6E4 u8[96] block of a pitch, C744 u8[96] semitone of a pitch, C7A4
AM depth, C7A5 u8[9] channel used by the orchestration, C7AE note select, C7AF vibrato depth, C7B0
u8[11] last pitch per channel (stored as a byte, read back signed), C7BB pitch range, C7BD
u16[25][12] F-numbers, CA15 u8[18][14] parameters per operator slot, CB11 u8[11] key bit per
channel, CB1C wave-select enable (0x20), CB1E far[11] F-number row per channel, CB4A u8[18] slot
volume, CB5C i8[9] transposition per channel, CB65 pitch-range step, CB67 i16[11] half-tone offset.
**Tables** (initialised data): 8124 u8[9][2] modulator and carrier slot of each channel, 8136
u8[18] register offset of each slot, 8148 u8[18] 1 = carrier slot, 815A u8[18] channel of each slot
(the standard OPL2 layout), 816C 24 instrument records of 0x4E bytes (13 i16 modulator parameters,
13 i16 carrier parameters, the two wave selects at +0x34 / +0x36; the 0x16 bytes after +0x38 are
never read), 88BC far[24] to the records, 891C and 8988 the instrument (0xFF = channel unused) and
the transposition of each channel for k = 0..11. The third row DS:89F4 + 9k is addressed but never
read (for k >= 9 it would run into text).

**Parameters** of a slot, in the records' order (= DS:CA15 rows, the jump table's cases): 0 KSL, 1
multiplier, 2 feedback, 3 attack, 4 sustain level, 5 sustaining (EG type), 6 decay, 7 release, 8
output level, 9 AM, 10 vibrato, 11 KSR, 12 connection (non-zero = FM), 13 wave select. Only the low
byte of each i16 is used. The register values (each written alone, low byte, CBW sign extension
on the parameters):

| Register | Value | Writer |
|---|---|---|
| 0x20 + offset | (AM ? 0x80) + (vibrato ? 0x40) + (sustaining ? 0x20) + (KSR ? 0x10) + (multiplier & 15) | 0b26 |
| 0x40 + offset | (KSL << 6) \| (63 - t / 254), t = 2 * volume * (63 - (level & 63)) + 127 (volume 0x7F: the level itself) | 099c |
| 0x60 + offset | attack << 4 \| (decay & 15) | 0a86 |
| 0x80 + offset | sustain << 4 \| (release & 15) | 0ad6 |
| 0xE0 + offset | wave & 3 while CB1C != 0, else 0 | 0c03 |
| 0xC0 + channel | feedback << 1 \| (connection ? 0 : 1); skipped for carrier slots | 0a29 |
| 0xBD | (AM depth ? 0x80) \| (vibrato depth ? 0x40) | 0bd6 |
| 0x08 | note select ? 0x40 : 0 | 0a0e |

**AdLib_Init** (40 writes): slot volumes 0x7F; the F-number rows; 0xBD = 0 (SetMode); 0xBD = 0,
0x08 = 0 (SetGParam 0, 0, 0); for channels 0..8: 0xB0+ch = 0, 0xA0+ch = 0; pitch range 1; for the 18
slots 0xE0+offset = 0, then 0x01 = 0x20 (wave selects enabled). No instrument is loaded.

**F-numbers** (055c, 0601, 067e, Turbo C long arithmetic): row r detunes by 4r/100 semitone:
v = (((10000 + 24r) * 52088) / 250000 << 14) * 9 / 111875, entry 0 = (v + 4) >> 3, then 11 times
v = v * 106 / 100, entry = (v + 4) >> 3 (16-bit). Row 0 is C..B = 343, 364, 385, 408, 433, 459, 486,
515, 546, 579, 614, 650. Every channel uses row 0 with half-tone offset 0 (only the never-called pitch
bend would change them). C6E4 / C744 give block i and semitone j of pitch 12i + j, i < 8.

**AdLib_PlayTune(t)**: k = 4t + (rand() & 3) (16-bit); C7A5[0..8] = 0; for each channel whose row-1
entry is not 0xFF: CB5C[ch] = the row-2 entry, SetVoiceTimbre(ch, record of that instrument), C7A5[ch] =
1. SetVoiceTimbre loads the record's first 13 parameters and wave into the channel's modulator slot,
the next 13 and the second wave into its carrier slot, each slot written by SndSetAllPrm: 0xBD,
0x08, 0x40+off, [0xC0+ch for the modulator], 0x60+off, 0x80+off, 0x20+off, 0xE0+off: **15 writes per
used channel** (the orchestrations use 6 to 9 channels: 90 to 135 writes). Nothing is keyed off: a
channel the new orchestration leaves unused keeps whatever it held (Music_Stop's note off only
reaches used channels) [obs code]. Tune 1's orchestrations 0 and 3 are the same rows.

**AdLib_NoteOn(v, pitch)** (pitch = DS:63AC[v] = note - 1 + 12 * octave, written by Sound_NoteOn):
voices above 2 do nothing (voice 3, the noise voice, has no channel); for each used channel ch of
3v..3v+2: AdLib_KeyOff(ch) (0xB0 = 0, 0xA0 = 0), then AdLib_SetFreq(ch, CB5C[ch] + pitch, key on).
**AdLib_SetFreq(ch, p, key)**: CB11[ch] = key, C7B0[ch] = p (low byte); p += CB67[ch]; clamped to
0..95; F = row CB1E[ch] entry C744[p]; 0xA0+ch = F & 0xFF; 0xB0+ch = (key ? 0x20) + (C6E4[p] << 2) + (F >> 8
& 3). So the OPL pitch is semitone + transposition: block = p / 12 (frequency F * 49716 / 2^(20 - block),
260.2 Hz for p = 48, 9 cents below C4); a transposition of +12 plays the PSG and speaker pitch
(octave nibble 3, note 1 = C4).

**AdLib_NoteOff(v)**: for each used channel of the group, **AdLib_NoteOffVoice(ch)**: release rate 10
on both slots (SetASlotParam(slot, 7, 10): 0x80+off rewritten; it stays until the next PlayTune
reloads the instrument), then AdLib_SetFreq(ch, (signed) C7B0[ch], key off). The key-off pitch is
the stored byte read signed: a pitch above 127 would key off at pitch 0 (no tune reaches it).

**Timing on the reference machine** (DOSBox-X, cycles=fixed 6000; the .dro captures, §11.7): each
`AdLib_Write` of the timer interrupt takes about 70 us, so the writes of one interrupt follow each
other at that pace (a heavy one, e.g. a loop restart with all voices off and on, reaches its last
note about 5 ms after the interrupt). AdLib_PlayTune costs about 76 us per write (6.9 ms for 6
channels, 10.3 ms for 9): Music_Play sets status 1 after it, so the timer interrupts of the set-up
play no music; with the call about 0.2 PIT tick after its interrupt that is 1 interrupt for up to 6
used channels and 2 for 7 to 9, which every tune start of the captures shows.

**Engine** (`src/dos/audio/`): `adlib_driver.h` (AdLibDriver: the functions above writing to an
OplWriter, the variables at their DS offsets, `restore` / `saveTo` for DOSBox-X states),
`opl_chip.h` (OplChip: ymfm's YM3812 at 3579545 / 72 Hz, writes 70 us apart, cubic resampling),
`opl_backend.h` (AdLibOpl: the two together for the sequencer; the set-up hold).

---

## 6. Hardware output models

### 6.1 PC speaker [obs driver, inf model]

PIT channel 2, mode 3 square wave, frequency `1193182 / divisor` (divisor 0 = 65536), audible
while port 0x61 bits 0 and 1 are both set. `dos_sound.py` box-filters the ideal square wave
per output sample (44.1 kHz) and holds the parameters for each tick.

Details that the DOSBox-X recordings show [obs, §11]: the speaker output is unipolar (the
silent level when the gate is off, a square between that level and the high level when on),
low-passed at roughly 6 kHz; a rising gate restarts the counter (each note starts with a
full high half period). In mode 3 a new divisor written while counting takes effect at the
end of the current half period, so a pitch change inside a note (glides, legato) lands up to
half an old period late.

### 6.2 SN76496 (Tandy 1000 / PCjr; PS/1 at port 0x205) [obs driver, inf model]

Clock 3579545 Hz. Latch byte `1 rrr dddd` (r: 0/2/4 tone period of voice 0/1/2, 1/3/5/7
attenuation of voice 0..3, 6 noise control), data byte `0 x dddddd` = upper 6 period bits.
Tone frequency = clock / (32 N) (N = 0 treated as 1024); attenuation 2 dB per step, 15 =
off; noise shift rate clock/512, /1024, /2048 or tone-3 rate (= the voice 2 tone frequency);
periodic noise = 15-step pulse. `dos_sound.py` models the white noise as the TI SN76489
register (15 bits, feedback bit 0 XOR bit 1, restarted by every noise-control write).

**Tandy 1000 (NCR 8496), from the DOSBox-X recording** [obs, §11]: the white noise of effect 1
decodes (one bit per shift period) to a sequence that satisfies `o[n+16] = NOT(o[n+1] XOR
o[n+5])` for all 3 368 decoded bits, i.e. a **16-bit register with taps 0x0002 and 0x0020 and
XNOR feedback** (output bit 0, inverted at the output), as MAME models the NCR 8496; the 506
bits after the first noise write equal the engine's sequence started from 0x8000. The later
tied noise notes of the same effect rewrite the same noise control (0xE7) **without restarting
the register**: the NCR 8496 restarts it only on a latch write that flips the white/periodic
bit (MAME's rule, matched bit for bit by the engine over all five noise notes). The shift rate
is the voice 2 tone frequency (5 327 Hz for period 21, 9.01 samples per bit at 48 kHz).

The tone counters behave like the PIT: a new period starts when the current half period ends.
After `Sound_VoiceOff`'s period 0 (= 1024, a 4.58 ms half period) the first note of the next
effect therefore starts up to 1.08 ticks late; the recording shows onset delays of 0..0.96
tick, consistent with period 0 = 1024 (a period 0 taken as 1 would leave no delay) [obs, §11].

---

## 7. How the game uses sound [obs]

* **Music**: `491b:0791(t)` clears the music block, and (if music is on, `DS:63b6`) sets the
  4 (or 1) stream pointers and status 1. Tune choice follows the room number `DS:910b`:
  < 4 tune 0, < 7 tune 1, < 15 tune 2 (`43cb:03cb`, `43cb:046e`, `47c7:08b5`). Music loops
  (63ba = 1). `DS:37ce` is 0xFFFF while the game runs and 0 while a menu/dialog is open
  (`43cb:046e`), which pauses the music through the ISR test.
* **Effects**: `491b:06d7(id)` (if sound is on, `DS:63b4`) starts effect *id* only if
  `id <= current id` (current = 0xFFFF when idle): **lower ids have priority** and an
  effect is never interrupted by a higher-numbered one. On Tandy the running effect is
  stopped first (`491b:074b`), which also resets the current id: on the Tandy any effect
  replaces any other (§5.2a). Effects do not loop.
* Stop functions: `491b:074b` stop effect, `491b:0811` stop music, `491b:0854` silence all
  voices.
* Effect ids used by code: 3 and 4 (`130f:0edf`, `130f:07a5`), 6 and 7 (`4a03:0003`),
  10 = right answer (`46ce:05bb`), 11 = wrong answer (`46ce:069e`); any id from animation
  scripts (opcode -5 in the sprite scripts, `4306:0022`).
* Music on/off and sound on/off are saved per player (USERS.DAT record +0x15e/+0x160,
  `fmt_data.md` §4).

---

## 8. Decoded content [obs, from `dos_sound.py list`]

| Entry | .IBM length | .TAN length | Notes |
|---|---|---|---|
| tune 0 | 53.00 s | 54.89 s | .TAN is a tone higher, 1-bar intro rest, 3 voices |
| tune 1 | 71.81 s | 71.96 s | ends with a ritardando; .TAN tempo 124 vs .IBM 125 |
| tune 2 | 24.34 s | 24.34 s | .TAN uses 2 voices |
| sfx 0..11 | 0.09–2.12 s | 0.09–2.02 s | sfx 1, 2, 6, 8, 10 differ in length between the two forms |

No tune uses the noise voice; only effect 1 does (voice 3, white noise clocked by voice 2). Durations are counted to the tick the
last voice ends, without looping.

Outputs (`python -P tools/dos/dos_sound.py all`): `tunes_ibm_00..02.wav`,
`tunes_tan_00..02.wav`, `sfx_ibm_00..11.wav`, `sfx_tan_00..11.wav` and event listings
`tunes_ibm.txt`, `tunes_tan.txt`, `sfx_ibm.txt`, `sfx_tan.txt` in
`extracted\previews\dos\audio\`.

---

## 9. Porting notes [inf]

* The format is small and fully specified above; an engine backend can run this sequencer
  at 236.696 Hz (e.g. advance it in the audio callback by sample count) and feed either a
  speaker model or an SN76496 model. The two period tables and the envelope table must come
  from SST.EXE's DGROUP (offsets in §4.2/§5.4), so the engine needs the unpacked EXE image
  (EXEPACK, `tools\unexepack.py` algorithm) or must locate DGROUP the same way the tools do
  (the entry point starts `MOV DX,DGROUP`; DS:0004 holds the Turbo C copyright).
* Reproduce the quirks that change the sound: music frozen during effects (non-AdLib),
  effect priority by id, Tandy octave folding (periods > 1023), the release gap rules, the
  tie/envelope continuation, envelope 0xFF tick with no write.
* The user may prefer Tandy sound over the speaker on a modern PC: device choice is a
  backend option (the data for both exists for every entry).
* AdLib needs an OPL2 core and the instrument tables: done in wave 5 (§5.5, §11.7).
* The engine implementation (§11): `src/dos/audio/` — `sound_data` (files, DGROUP tables),
  `sequencer` (491b, device independent, `timerTick()` = one PIT interrupt), `speaker_synth`,
  `psg_synth`, `adlib_driver` (4ac9), `opl_chip` (ymfm's YM3812), `opl_backend` (the two for the
  sequencer), `dos_sound_renderer` (sequencer + models advanced by sample count), `dos_audio_device`
  (SDL3 pull stream, thread-safe command queue, offline mode) and the `--dos-sound` tool.

## 10. Open questions

* ~~The exact OPL parameter order in the instrument records~~: answered in wave 5 (§5.5): the AdLib
  timbre order (KSL, multiplier, feedback, attack, sustain, sustaining, decay, release, level, AM,
  vibrato, KSR, connection), as the jump table at 4ac9:08f3 dispatches them; the 0x16 bytes after
  each 0x38-byte record are never read. Every register write of every tune and orchestration is
  checked against .dro captures (§11.7).
* ~~Tandy 1000 noise LFSR taps~~: answered for the DOSBox-X reference (§6.2: NCR 8496, 16 bits,
  taps 0x0002/0x0020, XNOR, restart only when the mode bit flips). A real NCR 8496 was not
  measured.
* Whether a real Tandy's PSG period-0 behaviour is audible: during rests it is silent
  (attenuation 15); after an effect it is audible as a ~109 Hz tone on each resumed music voice
  until its next note (§5.2a), at least in the emulation; no recording covers that case yet.

## 11. Engine implementation and verification [obs]

Scripts and outputs: `extracted/scratch/dos_w2_dos_audio/` (`py/cmp_raw.py`,
`py/cmp_speaker.py`, `py/cmp_tandy.py`, `py/cmp_opl.py`, `py/noise_taps2.py`,
`py/noise_seq.py`; reports `cmp_*.txt`). Engine renders: `tmport --dos-sound <device>
<sfx N|tune N|soundtest|all> OUT [--raw] [--state-log TXT]`.

### 11.1 Against `dos_sound.py`

`--raw` (44 100 Hz, TI noise, dos_sound.py's normalisation) renders of all 12 effects and 3
tunes on the speaker and on the Tandy: **30 of 30 WAV files identical, 0 differing samples**
(14 076 858 samples in all). Tick lengths as in §8.

### 11.2 Against the DOSBox-X recordings (`extracted/captures/dos/audio/*_soundtest_*.wav`)

The engine runs the recording schedule (Music_Stop +100 ticks, effects 0..11 every 600 ticks,
tunes 0, 1, 2 at +700, +12 900, +17 300, Music_Stop +6 000, music loop flag DS:63ba = 1 as in
the recorded states) and logs the speaker/PSG state per tick; the recording is aligned by one
offset (recording sample = A + tick × 48000 / 236.69543).

| Check | Speaker | Tandy |
|---|---|---|
| note onsets after silence | 896 notes, all within **0.115 tick** (0.49 ms) of the engine tick, median 0.000 | 427 onsets, delay 0..0.96 tick (median 0.006): the tone counter finishing its old half period (§6.2) |
| note ends (followed by silence) | 895 notes, median −0.035 tick; the outliers are low notes whose last edge precedes the gate-off by up to half a period | – |
| pitch | 1 111 segments with ≥ 2 periods: mean −0.014, median −0.007, max 3.7 cents | 633 one-voice runs (DFT peak): median 0.0, max 4.0 cents; 1 435 of 1 435 polyphonic tune notes ≥ 12 ticks strongest at the expected pitch (against ±1 semitone) |
| levels | – | one-voice peak-to-peak by attenuation 2..10: −4.01, −6.01, −8.01, −10.00, −12.00, −13.99, −15.99, −17.98, −19.98 dB (2 dB steps) |
| extra or missing sound | 0 of 61 023 recorded edges outside an engine note | 4 of 9 818 silent ticks / 13 of 18 383 sounding ticks differ, all next to a change or inside a flat half period of a ~110 Hz tone |
| music loops | tunes 0, 1 and 2 each restart once before the next command, as recorded | same |

The AdLib recording's effects (on the speaker): 121 onsets within 0.364 tick, 102 pitch
segments within 8 cents (1-tick segments), 0 stray edges.

### 11.3 Tandy noise

See §6.2: the engine's `NoiseType::Ncr8496` reproduces the recorded noise bits exactly (5 notes,
506 + 683 + 687 + 682 + 687 bits, 100 %); the TI and 17-bit registers match 56 %.

### 11.4 AdLib music timing (`adlib_soundtest_sfx0-11_tunes0-2.dro`)

The engine's AdLib-device sequencer calls `AdLib_NoteOn` 1 465 times for voices 0-2 in the
sound test; every call has a key-on (register 0xB0+ch bit 5) in its channel group (voice 0 →
channels 0-2, 1 → 3-5, 2 → 6-8) within 20 ms: 1 462 within 0.5 tick, max 1.0 tick (the .dro has
1 ms resolution). The OPL pitch (F-number, block) equals the engine's semitone `note - 1 + 12 ×
octave` plus 0 or ±12 semitones per channel (the orchestration transposition) and a tuning
offset of −5.6 cents on average (max 9 cents) from the F-number table. (Wave 2; the register-level
comparison of wave 5 is §11.7: every write of this capture is reproduced.)

### 11.5 Deviations of the engine (by design)

* Smooth mode (the default for listening): bipolar outputs instead of DOSBox-X's unipolar
  ones (no DC steps at note on/off), a 7 kHz 2-pole low-pass and a 12 Hz DC blocker; counter
  reloads at half periods and PIT gate restarts as in §6.1/§6.2; NCR 8496 noise.
* A stream whose events never take time (it would hang the original's interrupt) is cut after
  4 096 events; a block whose streams end at once restarts at most once per tick when looping.
* AdLib (wave 5): the original's driver on ymfm's YM3812 (§5.5, §11.7). Modelled on top of the
  original: each register write reaches the chip 70 us after the previous one (the reference
  machine's pace; the chip must see AdLib_NoteOn's key off for a re-keyed note to start again), and
  the tune's first notes wait the interrupts of AdLib_PlayTune's set-up (76.3 us per write, the call
  0.2 tick after its interrupt). The AdLib mix: the OPL against the speaker as DOSBox-X mixes them,
  both at 0.75 (the speaker and Tandy devices are unchanged). Not modelled: AdLib_Detect's timer
  writes, the speaker work of Sound_Tick before the OPL writes, AdLib_PlayTune's own write pace (70
  instead of 76 us). A DOSBox-X state's driver is restored from its variables (the registers are
  written again, the envelopes start anew at the load).

### 11.6 Wave 4: the driver of a saved state, and the sound of the whole game (looks agent)

* **Restoring a DOSBox-X state's driver.** `Sequencer::blockFromDgroup` / `blockToDgroup` read and
  write the 0x78-byte blocks of §5.1 (DS:6448, DS:64c0; stream pointers are offsets into the effect /
  music file, e.g. 0x230.. in TUNES.TAN, envelope pointers DS offsets, as observed in every reference
  state). `Sequencer::restore(sfx, music, voices)` continues them: the device gets what the current
  notes had written (the pitch of the time-taking event right before each sounding voice's stream
  pointer, `Sound_NextEvent` having advanced past it; a rest or raw-pitch 0 as voice off; the speaker
  gate off past the release point; the Tandy volumes come with the next tick's volume pass).
  `DosAudioDevice::restoreBlocks` queues it like the other commands; the game side is
  `soundRestoreDriver(g, dgroup)` (sound.h, DS:63be = voices). Checked in `tests/test_dos_audio.cpp`:
  a driver restored from the blocks of tick 1000 of tune 1 (inside notes) and one restored inside
  effect 6 write exactly the bytes of the uninterrupted driver in every following tick (speaker and
  Tandy). The AdLib orchestration of the tune is not in the blocks (the OPL driver's tables, wave 5).
* **The game's sound against DOSBox-X AVIs** (`tools/dos/audiocmp.py`: `align` / `track` = Goertzel
  magnitudes of the 61 semitones C2..C7 per 4 PIT ticks, cosine similarity; `edges` = sound on/off
  edges per PIT tick):
  * `avi/tandy_attract_full_cycle.avi` (150 s) against 300 s of the engine's attract (Tandy look and
    sound): tune 1 at one constant alignment over all 150 s (best lag per 6-s segment within ±1
    window = 4 PIT ticks of the global one, similarity 0.93-0.96; the AVI began 148.66 s after the
    tune's start, in the second attract cycle, so the tune loops seamlessly every 17032 PIT ticks as
    the engine does); the five page changes lie 0.01-0.03 s after the engine's at the same tune
    position (the original's title page wait starts after the title has been drawn).
  * `avi/tandy_r3_session.avi` (Round 3, 45 s; REFERENCE_CAPTURES.md R3) against the engine run with
    the same keys: the attract music similarity 0.94-0.99 at one alignment; the sign-in pauses the
    music 17 PIT ticks later in the engine (the walk-off runs 3 game ticks later: its title redraw is
    a long frame in the original, not in the engine); the coin toss's effects 5 and 0 start 3 PIT
    ticks later than in the recording (within the ±4 of the alignment) and their pulses (16 PIT ticks
    apart) and the frozen tune match; the resumed game's tune 0 starts 23 PIT ticks earlier in the
    engine after Yes on the saved-game prompt and resumes 10 PIT ticks earlier when Flutter's hint is
    closed (sound calls made inside a long frame took effect at the frame's start in the engine; wave
    5 places them in the frame, §11.8).

### 11.7 Wave 5: the AdLib driver against DOSBox-X (register streams and audio)

**Captures** (`extracted/captures/dos/`, DOSBox-X 2026.10.01, SB16 OPL3 in OPL2 use, oplemu default):
`audio/adlib_w5_variants.dro` + `.wav` from the harness state `savestates/w5/snd_adlib_variants`
(the R2 sound-test stub extended: before a Music_Play it writes the rand seed DS:8F48 so that
AdLib_PlayTune's rand() & 3 is the wanted orchestration; scratch `dos_w5_adlib/py/soundtest_v.py`):
every tune in every orchestration (k = 0..11) for its whole length plus 200 ticks, effect 6 under the
first, and a tune change without Music_Stop at the end; recorded in turbo mode (655 s emulated).
Also the R2 capture `audio/adlib_soundtest_sfx0-11_tunes0-2.dro` + `.wav` (real time; its
orchestrations, drawn by rand(), were 1, 0, 1: k = 1, 4, 9).

**Register streams** (`tools/dos/oplcmp.py`; engine: `tmport --dos-sound adlib soundtest OUT
--from-state STATE --script ... --opl-log LOG`, the driver restored from the capture's start state;
DOSBox-X logs only the writes that change a register, which the comparison applies to the engine's
stream too):

| Capture | Engine writes | Logged writes compared | Identical in order | Registers where the capture continues | Timer-interrupt writes against their engine tick |
|---|---|---|---|---|---|
| w5 variants (12 orchestrations, 14 tune starts) | 138 956 | 85 767 | 85 767 | 120 of 120 equal | 66 230 before the capture's clock slip: −0.14 .. +0.24 tick; per segment 81 753 of 84 757 within a quarter tick, all within 0.56 |
| R2 sound test (k = 1, 4, 9) | 34 785 | 21 324 | 21 324 | 120 of 120 equal | 21 162: −0.13 .. +0.19 tick |

A write is expected at its PIT tick plus 70 us per write before it in the tick (the capture's ms are
whole: up to 1 ms early). Every tune start's first note falls on the engine's tick (the set-up hold of
§5.5: 2 interrupts for 7-9 channels, 1 for the 6-channel orchestration). The turbo-mode capture's ms
clock slips by 2.2 ms against the PIT around tick 122 000 (its WAV by the same); the R2 capture (real
time) has no slip. The commands' own writes (Music_Play's set-up, Music_Stop's note off) come between
the tick before and their tick (−1.35 .. −0.6). `tests/test_dos_audio.cpp` repeats the w5 comparison
(write for write, registers, timing) and pins every orchestration's stream by CRC-32.

**Audio** (scratch `dos_w5_adlib/py/audio_detail.py`, `avi_batch.py`): the engine's render against
DOSBox-X's, aligned by the speaker effects (WAV) or by content (AVI audio; `audiocmp.py align`). Onsets:
the lag aligning the two energy envelopes around each engine key-on, measured around the common lag;
pitch: the strongest partial of sustained notes (8192 samples, zero-padded); sample level: the
normalised correlation of the waveforms (2048 samples, best lag within ±2.5 ms) and the cosine of
their magnitude spectra.

| Recording | Onsets within 1 ms | Pitch (cents) | Waveform correlation (median) | Spectrum cosine (median) |
|---|---|---|---|---|
| R2 sound test WAV (real time) | 542 of 548 | 26 notes, median −0.01, max 1.07 | 0.977 | 0.995 |
| w5 variants WAV (turbo) | 2 644 of 3 385 (the turbo recording jitters; the same tune and orchestration in the R2 WAV: 99 %) | 60 notes, median +0.04 | 0.971 | 0.993 |
| `avi/ega_opening_title.avi` (boot: tune 1, orchestration 2 from rand() = 346) | 142 of 194 | 17, max 0.78 | 0.983 | 0.986 |
| `avi/ega_r2_end_t2_nokey.avi` (log_end_t2) | 271 of 283 | 29, max 1.32 | 0.982 | 0.997 |
| `avi/ega_r2_crown_nokey.avi` (log_crown) | 334 of 353 | 30, max 2.47 | 0.979 | 0.996 |
| `avi/ega_r2_end_t6_allfound_nokey.avi` | 224 of 237 | 23, median +0.02 | 0.982 | 0.991 |
| `avi/ega_r2_castle_tier3_climb.avi` | 43 of 52 | 11, median +1.15 | 0.946 | 0.973 |
| `avi/ega_castle_ladder_jump_top.avi` | 38 of 47 | 2, max 0.27 | 0.909 | 0.959 |

Onsets outside 1 ms are mostly the detector's (effects and noise among the candidates of the
AVI runs, which have no OPL log). `avi/ega_r2_treasure_room_from_clubhouse.avi` does not match at any
alignment (its effects line up with the engine's at offset 0, its music is not the state's tune):
that AVI was not recorded from the sound driver of its scenario's state. DOSBox-X's AVI audio is not
in step with its frames: against the engine runs from the scenarios' states the audio tracks come
0.68-1.07 s early, 0.55-1.1 s ahead of their own pictures as the scenarios' AVI fits place them; the
comparisons therefore align the AVI audio by content.

**DOSBox-X's OPL core against ymfm**: the same frequencies (pitch within 1-2.5 cents), the same
onsets; at sample level the best-scaled engine render leaves −13 to −15 dB of the recording
(correlation 0.977-0.983 in the clean recordings), from the envelope generators' rounding, ymfm's
YM3014 floating-point DAC and DOSBox-X's own resampling. **Level**: in the R2 recording the tunes' RMS
is 0.391 / 0.437 / 0.501 of the effects' peak; the engine's AdLib mix gives 0.395 / 0.435 / 0.500
(OplChip::kDefaultGain), the whole mix at kAdLibMixLevel 0.75: the loudest tune passage peaks at
0.82 of full scale at the device's default volume.

**The game**: `--sound adlib` (or an EGA state, whose device is AdLib) plays the real music in the
headless frame-dump `sound.wav` and in the windowed device; `--dos-load-state` continues the state's
orchestration and notes (`soundRestoreDriver` → `SoundFacade::restoreOplDriver`). Headless runs that
keep no sound do not synthesise the OPL (`DosAudioSound::keepSamples(false)`).

### 11.8 Wave 5 integration: sound calls inside long frames

The driver's entry points (`Sfx_Play`, `Music_Play`, the flags) write its blocks at once; the next
timer interrupt plays them. A call made partway through a long frame of the reference machine
therefore sounds that far into the frame, not at its start. The engine reports a frame's costs where
the work happens and passes each call the PIT ticks between the next interrupt and the one after its
place (`DosRuntime::soundDelay`, `DosAudioDevice`'s per-command delay; `ENGINE_MAP.md` §3.8): Game_Start's
Music_Play after Quiz_LoadQuestions (7 PIT ticks), Sound_SyncState's gate and Music_Play after the
level's PAN and sprite bank, the music's pause after a message bubble's drawing (27.5), a search
frame's effect at that frame, each coin of the count-up on its own 3.32-tick clock; Title_Run's tune
after the OPENING load (9.85: Title_Run 4ba4:0434 loads before Music_Play). A game tick's calls and
the period speed have no delay. The game sees nothing of it: the 164 fidelity scenarios' traces are
byte-identical with and without the delays.

Checked with `avi/tandy_r3_session.avi` (the R3 Tandy session; sound edges per PIT tick at the
scenario's time base, engine PIT = AVI seconds x PIT + 10336; scripts in
`extracted/scratch/dos_w5_integration/`, `sess.py`, `edges.py`, `sessfit.py`):

| Event | Recording | Engine before | Engine now |
|---|---|---|---|
| the resumed game's tune 0 starts (after Yes) | 15013 | 14990 (23 early) | 15016 (3 late) |
| the tune resumes after Flutter's first hint | 16201 | 16191 (10 early) | 16192 (9 early) |
| the attract tune (AVI 0.5-3.5 s), at the title pages' time base | edges | 11-12 early | 2-3 early |

The pictures give the time base of each part (`avifit.py`): the title pages' walk-off (AVI frames
385-462) 10348.0, the sign-in and the resumed game (frames 892-1000, 1139-1222) 10331-10336.6, the net
swing 10326. At the title pages' base the attract tune now runs 2-3 PIT ticks ahead of the recording
(Title_Run's tune after the OPENING load; 11-12 ahead before). At the resumed game's base the tune's
start is within 1-5 PIT ticks: the
model's 29.5 PIT ticks of I/O before the second Music_Play (7 + MOUNT2N3 20.3 + SPRITES.PAK 2.2)
against about 23-26 in the recording. The resume after the hint keeps its offset for another reason:
the start-of-game message pauses the music after its bubble's drawing, which the engine now does at
the right place in the frame, but the bubble's frame begins when the resume frame's HUD redraw ends,
and the Tandy draws both HUD redraws in about 10 PIT ticks each where the engine takes the EGA's 16
(the bubble is complete at AVI frame 1192, 13 PIT ticks before the engine's): the tune runs 31 PIT
ticks before the pause in the recording, 42 in the engine, and resumes 11 earlier in its note. Per-look
drawing costs are not modelled (`ENGINE_MAP.md` §8).
