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
  speaker** [obs]. This document specifies the AdLib note mapping and table locations, but
  not a full OPL2 rendering.

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
* AdLib: voices = 4, `4ac9:00b7` initialises the OPL2.

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
music on, 63b8 sfx loop flag (never set), 63ba music loop flag (set to 1 at `1437:0204`,
`4ba4:0469`), 63bc device, 63be voice count, 63c0 1 = processing music, 63c2 block pointer,
63c6[4] voice-owned-by-effect mask (cleared every tick), 63ac[4] AdLib pitch, 6538/6550
period tables, 6568 table pointers, 656c port, 656e envelope pointers.

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

### 5.5 AdLib path (segment `4ac9`) [obs, partly inf]

* Register writes go through `491b:0875` (port DS:656c = 0x388, data at +1, 6 and 35 status
  reads as delays).
* Play tune *t* (`4ac9:0002`): `k = 4t + (rand() & 3)` picks one of **4 orchestrations**. Three
  9-byte rows at DS:891c, 8988, 89f4 (+9k) are addressed per OPL channel 0..8. The first
  gives the instrument index (0xFF = channel unused), the second the transposition in
  semitones (signed, copied to DS:cb5c+ch). The pointer into the third row is computed but
  never read in `4ac9:0002`; for k ≥ 9 it would run into the text at DS:8a48, so it is not a
  real 12-row table. Each used channel gets the instrument whose far pointer is entry
  *index* of DS:88bc (24 instruments, records at DS:816c.., stride 0x4E). Listing:
  `dos_sound.py adlib` → `adlib_tables.txt`.
* Instrument record [obs layout, inf meaning]: 13 int16 operator parameters for the
  modulator at +0x00, 13 for the carrier at +0x1A, wave-select words at +0x34/+0x36
  (`4ac9:019a`, `4ac9:0847`); this is the AdLib sample-driver "timbre" layout. Operator
  slots map through DS:8124 and DS:8136 (the standard OPL2 operator offsets).
* Note on (`4ac9:029e`): sequencer voice 0 drives OPL channels 0-2, voice 1 channels 3-5,
  voice 2 channels 6-8 (each channel only if its instrument slot is used), each at
  `semitone + transposition`; voice 3 (the noise voice) is ignored. Pitch uses a 25-steps-
  per-semitone F-number table built at start-up (`4ac9:067e`, DS:c7bd).
* Note off (`4ac9:0432`) keys off the same channel group.
* Rendering this needs an OPL2 emulator plus the 24 instrument records; not done in this
  pass.

---

## 6. Hardware output models (as used by `dos_sound.py`)

### 6.1 PC speaker [obs driver, inf model]

PIT channel 2, mode 3 square wave, frequency `1193182 / divisor` (divisor 0 = 65536), audible
while port 0x61 bits 0 and 1 are both set. The renderer box-filters the ideal square wave
per output sample (44.1 kHz) and holds the parameters for each tick.

### 6.2 SN76496 (Tandy 1000 / PCjr; PS/1 at port 0x205) [obs driver, inf model]

Clock 3579545 Hz. Latch byte `1 rrr dddd` (r: 0/2/4 tone period of voice 0/1/2, 1/3/5/7
attenuation of voice 0..3, 6 noise control), data byte `0 x dddddd` = upper 6 period bits.
Tone frequency = clock / (32 N) (N = 0 treated as 1024); attenuation 2 dB per step, 15 =
off; noise shift rate clock/512, /1024, /2048 or tone-3 rate; white noise = 15-bit LFSR with
taps 0 and 1 (TI SN76489 behaviour [inf]; the Tandy's NCR 8496 may differ), periodic =
15-step pulse.

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
  stopped first (`491b:074b`). Effects do not loop.
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
* AdLib needs an OPL2 core and the instrument tables; it is optional for a first version.

## 10. Open questions

* The exact OPL parameter order in the instrument records (needs `4ac9:08db`, which Ghidra
  decompiled badly), and what the 0x16 bytes after each 0x38-byte record hold.
* Tandy 1000 noise LFSR taps (NCR 8496 vs SN76496); the renderer uses the TI pattern.
* Whether a real Tandy's PSG period-0 behaviour (written on every rest) is audible.
