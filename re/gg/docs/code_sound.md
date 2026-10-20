# ssg.exe sound: the BMF driver (1bb4), the bank layer (1cdd) and who plays what

Scope: the complete sound system of *Gizmos & Gadgets!* (DOS v1.0): the resident driver in
segment 1bb4 (64 song slots, the sound tick, the class interpreters for the PC speaker and the
OPL2 in rhythm mode, the dead PSG and MT-32 paths, channel ownership and priorities, pause and
silence), the bank layer in segment 1cdd (loading sound banks, handles, music arrangements,
effects, the Ctrl+S sound switch), the music cue callbacks in 12e0, and every place in the game
that starts, stops or changes a sound. It ends with what tmport's `src/dos/audio` can reuse.

The song format (BMF) and the bank formats are in [fmt_sound.md](fmt_sound.md); the PIT and the
timer interrupt that drive the sound tick are in [code_runtime.md](code_runtime.md) §1.

Evidence: the disassembly of segments 1bb4 and 1cdd (hand-written assembly and small C
functions; every routine below was read instruction by instruction), the bytes of `ssg.exe`
(DGROUP tables), the songs decoded by `tools/gg/bmf.py`, and the wave 0 OPL captures
(`REFERENCE_CAPTURES.md` §3.3). Scratch scripts of this pass: `extracted/scratch/gg/wave1/platform/`
(`bends.py`: bend and velocity ranges over all 495 songs; `callargs.py`: the call sites and their
arguments).

Notation: **[O]** observed, **[I]** inferred, **[R]** recommendation for the engine. Addresses
`SSSS:OOOO` (Ghidra, G&G project) and `DS:XXXX` (DGROUP 1ea7) as in `re/CONTEXT.md`. Names from
`re/names/gg/` (this pass's names were merged there after wave 1, `re/names/gg/README.md`). "Tick" here is a
**sound tick** = one `Snd_Tick` = 2 PIT ticks = 13.731 ms (72.826 Hz). "Song" is a registered
BMF song block (BMF + 0x10, `s` below); "slot" is its index in the driver's table.

---

## 1. Overview

```
game code ──► bank layer 1cdd (handles DS:9d48[60], bank memory DS:9c58[60])
                 SndRes_LoadMusicBank / LoadEffectBank / LoadSong, PlayMusic / PlayEffect,
                 Stop*, SelectArrangement, Mute/Unmute, Free*, ToggleOption, Pause/Resume
              ──► driver 1bb4 (slots DS:1458[64], active DS:1558[64], priority DS:1598[64])
                 Snd_RegisterSong, Snd_Play, Snd_Stop, Snd_Free, Snd_SetCue, Snd_SetTranspose,
                 Snd_MuteChannel, Snd_SetPriority ...
Timer_ISR (every 2nd PIT tick) ──► Snd_Tick ──► class interpreter per active slot
                 class 1: PC speaker (PIT channel 2, port 61h)
                 class 3: OPL2 at 388h/389h, rhythm mode (6 melodic + 5 drum voices)
                 classes 0, 2, 4: dead (no data, never enabled)
```

* Every song keeps its run-time state inside its own loaded copy (fmt_sound.md §3.1).
* Several songs play at once; each **channel** of a class has one owner song at a time, chosen
  by priority (§10). Effects get priority + 0x3F, music priority 0, so an effect takes the
  channels it uses from the music and gives them back when it ends.
* All arrangements of a game are of one class: a bank id holds the speaker version, id + 1 the
  FM version, and every loader adds `g_fmPresent` DS:a0d8 (0 or 1) to the id.

---

## 2. Driver state in DGROUP [O]

| DS | Type | Name | Meaning |
|---|---|---|---|
| 1458 | far[64] | `g_sndSlots` | registered song blocks (BMF + 0x10); NULL = free slot |
| 1558 | u8[64] | `g_sndActive` | 1 = playing |
| 1598 | u8[64] | `g_sndPriority` | priority of the slot's song (set by `Snd_Play`, `Snd_SetPriority`) |
| 15d8 | u16[4][16] | `g_sndChannelOwner` | per class (1, 2, 3, 4; class 0 uses class 1's row) and channel: low byte = owner slot, high byte = its priority; 0x00FF = free; 0x00FE = released (transient, §10.4); bit 7 of the low byte = taker marked (transient) |
| 1658 | u8[5] | `g_sndClassChannels` | channels per class: 1, 16, 4, 11, 16 |
| 165d | u8 | `g_sndBusy` | lock: `Snd_Tick` does nothing while it is 1 |
| 165f | u8[16] | `g_spkChannelNote` | class 1: last key per channel; ≥ 0x80 = none (a note-off stores its status byte) |
| 166f | u8[4] | `g_psgChannelNote` | class 2 (dead) |
| 1673 | u8[11] | `g_oplVoiceKey` | class 3: last key per voice; ≥ 0x80 = none |
| 167e | u16 | `g_sndCurrentSlot` | slot being stepped by `Snd_Tick` (used to stop it at its end) |
| 1680 | u8 | `g_sndEnabled` | 1; `Snd_Silence` sets 0: no key-on, frequency, BD or speaker writes |
| 1681 | u8[5] | `g_sndClassAvailable` | per class: 1, 1, 0, 0, 0 in the file; [2] `g_hasPsg` DS:1683 (never set), [3] `g_hasOpl` DS:1684, [4] `g_hasMpu` DS:1685 (never set) |
| 1686 | u16 | `g_oplMasterVolume` | 256 (only the dead `Snd_SetMasterVolume` writes it) |
| 1688 | u16[16] | `g_oplChannelVolume` | 256 each (only the dead `Snd_SetChannelVolume` writes them) |
| 16a8 | u8[6] | – | MT-32 SysEx body for the master volume (dead) |
| 16ae | u16 | `g_psgPort` | 0, never written (dead PSG) |
| 16b0..16dd | | – | dead: tempo-scaler remainder, PSG envelope state |
| 16de | u8[6] | `g_oplB0Shadow` | last B0h value of melodic channels 0..5 |
| 16e4 | u8 | `g_oplBdShadow` | register BDh (0x20 after the OPL init: rhythm mode, drums off) |
| 16e5 | u8[11][2] | `g_oplVoiceOperators` | per voice the indices of its operators (0xFF = none) |
| 16fb | u8[18] | `g_oplOperatorOffsets` | operator index → register offset 00 01 02 03 04 05 08 09 0A 0B 0C 0D 10 11 12 13 14 15 |
| 170d | u8[5] | `g_oplPatchRegs` | 20h, 40h, 60h, 80h, E0h |
| 1712 | u8[128] | `g_noteOctave` | n / 12 |
| 1792 | u8[128] | `g_noteInOctave` | n % 12 |
| 1812 | u16[16] | – | speaker divisors for semitones −2..13 of the base octave (base entry DS:1816) |
| 1832 | u16[16] | – | PSG periods, same layout (base DS:1836; dead) |
| 1852 | u16[25][12] | `g_oplFNumbers` | OPL F-numbers: row r = 12 semitones detuned by r/25 semitone (§8.4) |
| 1aaa | near[5] | `g_sndClassStep` | interpreters: 087e, 0966, 0a1f, 0ad6, 0966 |
| 1ab4 | u8[4][8] | `g_sndEventSizes` | bytes per event by class 1..4 and status nibble 8..F (only the dead tempo scaler reads it) |
| 1ad4 | u16 | `g_soundMuted` | the sound switch (§12.2) |
| 1ad6, 1ad8 | i16 | – | slot of the last effect / music started by the bank layer (−1 after a stop); no reader |

The voice table DS:16e5 and the voices it defines [O bytes; OPL roles I from the OPL2 rhythm
layout]:

| Voice (= channel of a class-3 song) | Operators (offsets) | OPL use | Frequency written to channel | C0h written |
|---|---|---|---|---|
| 0..5 | 00/03, 01/04, 02/05, 08/0B, 09/0C, 0A/0D | melodic channels 0..5 | 0..5 (with key-on) | C0h + voice |
| 6 | 10/13 | bass drum | 6 | C6h |
| 7 | 14 | snare drum | 7 | – |
| 8 | 12 | tom-tom | 8 | C8h |
| 9 | 15 | cymbal | 8 | – |
| 10 | 11 | hi-hat | 7 | C7h |

---

## 3. Initialisation

### 3.1 `Snd_Init(device)` 1bb4:1234 [O]

1. All 64 slots: active = 0, pointer = NULL; busy = 0.
2. By device (`g_soundDevice` DS:2452, code_runtime.md §2.2):
   * −1 (no switch): `AdLib_Detect`; the OPL is available only if it answers.
   * 1 (`-SI`): no detection; OPL not available.
   * 8: nothing (no switch selects it).
   * any other value (0 for `-SN`/`-S`/`-S-`, 4 for `-SA`/`-SB`/`-SM`): `AdLib_Detect`, then
     **`g_hasOpl` = 1 regardless of the result**.
3. Returns `1 | hasPsg<<1 | hasOpl<<2 | hasMpu<<3`.

`Init_CommandLine` then sets `g_fmPresent` = caps bit 2 and, for device 0, calls `Snd_Silence`
(§12.3). Consequences [O]: with `-SN` the game loads the **FM** banks and plays them silently
(`g_sndEnabled` = 0); with `-SA` and no card, it plays FM songs to an absent chip; with `-SI` it
uses the speaker even if an AdLib is present. Round 2 saw both flags (REFERENCE_CAPTURES §9.7):
`-SN` gives `g_soundDevice` = 0 with `g_fmPresent` = 1, `-SI` gives 1 and 0 [O]. DOSBox-X wrote
no `.dro` for the `-SN` run: it opens a capture only at the first key-on, which `Snd_Silence`
prevents; the patch and volume writes of §12.3 still happen but go unrecorded [I: DOSBox-X's
capture rule].

### 3.2 `AdLib_Write` 1bb4:00a9 [O]

AL = register, AH = value: `OUT 388h, AL`; 6 × `IN 388h`; `OUT 389h, AH`; 35 × `IN 388h`. The
same delays as TM DOS's `AdLib_Write` (491b:0875).

### 3.3 `AdLib_Detect` 1bb4:0142 [O]

Writes 04h = 60h, 04h = 80h; reads 388h: if `status & E0h` ≠ 0 → absent. Writes 02h = FFh,
04h = 21h; 200 × `IN 388h`; if `status & E0h` ≠ C0h → absent. Present: `g_hasOpl` = 1, writes
04h = 60h, 04h = 80h, then `AdLib_InitChip`. Absent: `g_hasOpl` = 0, writes 04h = 60h, 04h = 80h.

### 3.4 `AdLib_InitChip` 1bb4:00de [O]

In this order (111 writes): BDh = 20h (and `g_oplBdShadow` = 20h); 08h = 00h; for channels 0..8:
A0h+c = 0, B0h+c = 0; for the 18 operator offsets in table order: E0h+o, 20h+o, 40h+o, 60h+o,
80h+o = 0; finally 01h = 20h (wave-select enable). It runs only when the detection succeeded.

---

## 4. Registering songs [O]

* `Snd_RegisterSong(p)` 1bb4:0ebc: p must start with `BMF` 1Ah, else −6; returns
  `Snd_AllocSlot(p + 0x10)`.
* `Snd_AllocSlot(s)` 1bb4:0e71: the first slot whose pointer is NULL (−1 if none); if
  `g_sndClassAvailable[s.class]` ≠ 1 → −4 and the slot stays free; else the pointer is stored and
  the slot returned. A registered song is not playing until `Snd_Play`.
* `Snd_Free(slot)` 1bb4:0d8f: `Snd_Stop(slot)`, pointer = NULL.
* `Snd_GetSlotSong` 1bb4:000e (near, AX = slot): ES:BX = song; CF and AX = −2 if slot ≥ 64 or
  the pointer's segment is 0. Every API function starts with it, so calls with a bad slot
  (−1, −4, −6 handles) do nothing and return −2 (the "is active" query then returns −2, which is
  true; §11).

Song block run-time fields (fmt_sound.md §3.1): +01h delay countdown, +03h tick counter, +05h
stream position, +07h cue tick, +09h far cue callback, +0Dh loop flag, +0Eh 16 channel records
{u8 flags, u8 program, u16 bend}, +4Eh loop section, +4Fh current section, +50h live transpose,
+51h running status. Channel flags: bit 7 = used by the song (from the file), **bit 1 =
stopped** (the channel belongs to another song, or the song was stopped), **bit 0 = muted**
(by the game: arrangements, §13.4). Events on a channel with bit 0 or 1 set are read but not
played (a program change is still recorded; its patch is loaded unless bit 1 is set).

---

## 5. `Snd_Tick` 1bb4:0db3 [O]

Called by `Timer_ISR` on even PIT ticks, with interrupts enabled.

```
if g_sndBusy == 1: return                              (the whole tick is lost; no catch-up)
for slot = 0 .. 63:
    if not active: continue
    g_sndCurrentSlot = slot; s = g_sndSlots[slot]
    s.tick += 1
    if s.tick == s.cue: far call s.callback            (ES:BX pushed; the callbacks take no arguments)
    s.delay −= 1
    if s.delay == 0: g_sndClassStep[s.class]()        (interprets events until the next delay)
if g_hasPsg: Psg_StepEnvelopes                         (dead)
```

* Slots are processed in ascending order; within one tick a higher slot's writes come last.
* The cue is compared with equality on a u16 counter that a section start resets to 0 (§6.1);
  a cue beyond the section's length never fires. With cue = 0 and no callback set, a song that
  ran 65 536 ticks (15 minutes) without a section restart would call a NULL pointer; no shipped
  song does (looping songs restart their section, the others stop) [O code; I].
* A delay of 0 read from a `FF 00 00` prefix would wait 65 536 ticks (none in the data).
* The lock is taken by every API function that changes slots or channels (push, set 1, restore),
  so a `Snd_Tick` that falls into such a call is skipped entirely [O]. [R] ignore (API calls are
  instantaneous in the engine).

---

## 6. Sections, events and delays [O]

### 6.1 `Snd_StartSection` 1bb4:0045 (ES:BX = song, AL = section)

* delay = 1, tick = 0.
* Class 0: stream position = 11h (no section table); done.
* Otherwise: current section = AL; the 16 channel bend words = 0 (class 4: 4000h); live transpose
  +50h = the section entry's transpose; position = the entry's start; one delay byte is skipped
  (three if it is FFh); position stored at +05h.
* Flags and programs are **not** reset: a restarted song keeps its loaded patches, and its
  program-change events at the start are skipped when the program is unchanged.

Timing [O]: after `Snd_Play` the first events run at the next sound tick (delay 1); the tick
counter then reads 1 there, so in the first pass an event at stream time t runs when the counter
is t + 1. After a restart by the end-of-section event (same tick, §6.4) the counter is 0 for the
events of the new pass, so in later passes an event at stream time t runs at counter t.

### 6.2 `Snd_ReadEvent` 1bb4:093f (near)

Reads a word at the position: a status byte (≥ 80h) consumes 2 bytes (status, first data byte in
AH); a data byte (< 80h) is running status: status = +51h, the byte is the first data byte, 1
byte consumed. +51h = the status (also after a meta event). Returns the channel number (status
& 0Fh) and the offset of its record (0Eh + 4·channel).

### 6.3 Delays

After each event: d = next byte; d = FFh → a u16 delay follows; d = 0 → the next event runs in
the same tick; else delay = d. The delay and the position are stored and the interpreter returns.

### 6.4 `Snd_MetaEvent` 1bb4:08ef (FF len16 t v)

Skips len payload bytes (the position ends after them). If t = 1 and v = the current section:
end of section:

* the entry's next section ≠ FFh → `Snd_StartSection(next)`, then **the interpreter continues in
  the same tick** with the new section's first event (no delay byte is read);
* else, if the loop flag (+0Dh) is set → the same with the loop section (+4Eh);
* else → `Snd_Stop(g_sndCurrentSlot)` and the interpreter returns.

Any other meta event: the delay byte that follows is read as usual. All 495 songs have one
section whose next is FFh and transpose 0, so in practice the end event either loops (loop flag
from `Snd_Play`) or stops the song [O data].

---

## 7. Class 1: PC speaker (`Snd_StepSpeaker` 1bb4:0966) [O]

Per event (status & F0h):

| Status | Data | Action (when the channel's flags & 3 = 0) |
|---|---|---|
| 8n | none | `g_spkChannelNote[n]` = status & F0h, i.e. always 80h (1bb4:096d + 0985; the OPL note-off stores the full status, 1bb4:0af1); if enabled: port 61h &= ~01h (timer-2 gate off; the speaker data bit 1 stays set) |
| 9n..Dn | key | `g_spkChannelNote[n]` = key; divisor d(key + transpose, bend[n]) below; if enabled: `OUT 42h` low, high; port 61h \|= 03h |
| En | lo, hi | bend word = hi:lo; if it equals the stored bend → nothing; else store it (**also when muted or stopped**); then, if flags & 3 = 0 and the channel has a sounding key (< 80h): re-pitch (divisor with the new bend, `OUT 42h`, port 61h \|= 03h) |
| Fn | meta | §6.4 |

**Divisor** (`Spk_PitchBend` 1bb4:0541 with base DS:1816; n = key + transpose, 8-bit, no clamp):

```
T = word table at DS:1816, valid for indices −2 .. 13 (DS:1812..1831)
o = g_noteOctave[n];  i = g_noteInOctave[n]
d = T[i] >> o
bend b (u16): b = 0 → d
   b bit 15 clear (up):   up   = T[i + 2] >> o;  d = d − (((d − up) · b) >> 13)
   b bit 15 set (down):   down = T[i − 2] >> o;  d = d + (((down − d) · (b & 7FFFh)) >> 13)
```

(The products are 16 × 16 → 32 bits; `>> 13` keeps bits 13..28.) T[0] = 8E88h = 36 488 →
32.70 Hz: n = 0 is C1; the tone is 1 193 182 / d Hz (PIT channel 2, mode 3, set once by
`Input_InstallHandlers`). A bend magnitude of 2000h is two semitones. Class 1 songs use bends
0..1FFFh up and 8000h + 0..1FFFh down [O, `bends.py`: 118 bend events].

**One voice for everything.** All class 1 songs write the same PIT channel and gate: the last
note-on (from any channel of any song) sets the pitch, any note-off silences the speaker, and a
bend on a channel re-pitches the speaker to that channel's last key even if another channel's
note is the one sounding [O]. Channel ownership (§10) only decides which song's channel events
are executed: speaker songs mostly use channel 0 (232 of 237), so an effect on channel 0 takes
it from the music, while music on channels 1..3 keeps playing into the same speaker [O data].

Pitch mapping compared with FM: on the speaker key k sounds at MIDI note k + 24, on the OPL at
MIDI note k (§8.4); the two arrangements are separate songs [O tables; I intent].

---

## 8. Class 3: OPL2 (`Snd_StepOpl` 1bb4:0ad6) [O]

The song's channel number is the OPL **voice** of §2. Events (status & F0h):

| Status | Data | Action (when flags & 3 = 0 unless noted) |
|---|---|---|
| 8n | none | `g_oplVoiceKey[n]` = status (≥ 80h); `Opl_NoteOff(n)` |
| 9n, An, Bn | key, velocity | `Opl_SetVolume(n, velocity)`, then `Opl_NoteOn(song, key, n)` |
| Cn, Dn | program | if program = stored program → nothing; else store it (always); if flags bit 1 is clear: `Opl_SetPatch(song, program, n)` (a muted channel still loads the patch) |
| En | lo, hi | bend = hi:lo; if unchanged → nothing; else store (**also when muted or stopped**); then, if flags & 3 = 0: `Opl_NoteOn(song, g_oplVoiceKey[n], n)` (returns early when the key is ≥ 80h) |
| Fn | meta | §6.4 |

### 8.1 `Opl_SetPatch(song, p, voice)` 1bb4:0270

Patch address = s + word at s + 54h + 4·(number of sections) + 2p (fmt_sound.md §3.4). Writes,
in this order: 20h, 40h, 60h, 80h, E0h + offset(first operator) = bytes 0..4; then C0h + channel =
byte 5 for voices 0..6 (channel = voice), 8 (C8h) and 10 (C7h), nothing for voices 7 and 9; then,
when the voice has a second operator (voices 0..6), 20h..E0h + offset(second operator) = bytes
6..10. These writes do not check `g_sndEnabled`.

### 8.2 `Opl_SetVolume(voice, v)` 1bb4:0218

Register = 40h + offset(carrier), the carrier being the second operator when the voice has one,
else the first. The value is computed from the velocity byte v as if it were a 40h register value
(KSL in bits 6-7, attenuation in bits 0-5):

```
a  = 63 − (v & 3Fh)
a1 = ((a · master) & FFFFh + 40h) >> 8 & FFh         (master = DS:1686 = 256)
a2 = ((a1 · chanVol[voice]) & FFFFh + 40h) >> 8      (chanVol = DS:1688[voice] = 256)
value = (v & C0h) | ((63 − a2) & FFh)
```

With the volumes at 256 (always: their setters have no caller) the written value is **exactly
the velocity byte**. Velocities in the data are 0..171; their bits 6-7 (KSL) are 0, 1 or 2 [O,
`bends.py`]. Not gated by `g_sndEnabled`.

### 8.3 `Opl_NoteOn(song, key, voice)` 1bb4:0343 and `Opl_NoteOff(voice)` 1bb4:0302

```
g_oplVoiceKey[voice] = key;  if key ≥ 80h: return
bend = channel bend word (hi = semitones, lo = 1/25 semitone)
n = key + transpose + bend.hi − 12          (8-bit; if the result has bit 7 set → 0; if > 95 → 95)
block = g_noteOctave[n];  i = g_noteInOctave[n]
f = g_oplFNumbers[bend.lo][i]                (row = bend.lo, not range-checked)
A0 = f & FFh;  B0 = (block << 2) | (f >> 8)
voice 0..5:  write A0h+voice = A0; write B0h+voice = B0 | 20h (key on); g_oplB0Shadow[voice] = B0 | 20h
voice 6..10: ch = voice (6, 7, 8), 9 → 8, 10 → 7; write A0h+ch, B0h+ch (no key-on bit);
             g_oplBdShadow |= 1 << (10 − voice); write BDh
```

`Opl_NoteOff`: voices 0..5: write B0h+voice = `g_oplB0Shadow[voice]` & DFh; voices 6..10:
`g_oplBdShadow` &= ~(1 << (10 − voice)), write BDh. Rhythm bits: voice 6 → 10h (bass drum), 7 →
08h (snare), 8 → 04h (tom-tom), 9 → 02h (cymbal), 10 → 01h (hi-hat).

All A0h/B0h/BDh writes are skipped while `g_sndEnabled` = 0. Notes [O code]:

* There is **no key-off before a key-on**: a note-on on a melodic voice that is still keyed
  rewrites the frequency with the key bit set and does not restart the envelope; a drum whose
  BD bit is already set is not re-triggered. The songs send note-offs (8774 offs for 9041 ons in
  class 3).
* A bend re-keys a melodic voice (B0h with 20h) but has the same "no restart" property.
* Register writes per note-on: 40h (volume), A0h, B0h (melodic) or 40h, A0h, B0h, BDh (drums).

### 8.4 Pitch

The F-number table DS:1852 (25 rows × 12) is **identical** to the table the AdLib sample
driver's `InitFNums` computes (tmport `AdLibDriver::setFNumRow`, TM DOS 4ac9:067e): row 0 holds
the 12 semitones from C (F-number 343), row r is detuned by 4r cents [O: all 300 values
compared]. Frequency = f · 49 716 / 2^(20 − block): n = 48 (block 4, C) gives 260 Hz, i.e. MIDI
note 60; since n = key − 12 without bend or transpose, a song key k sounds at MIDI note k. Bends
in the data: lo 0..24, hi 0, 1, FEh, FFh, i.e. −2..+2 semitones in 1/25 steps [O, `bends.py`:
628 events]. Transposes are 0 in every song; the game sets them at run time (§14.1).

---

## 9. Dead classes [O]

* **Class 0** (`Snd_StepSpeakerSweep` 1bb4:087e): a list of words from s + 11h: a positive word is
  a speaker divisor (`OUT 42h`, gate on) held for the delay at s + 0Fh; 0 = gate off; 8000h..FFFEh:
  delay = word & 0Fh; FFFFh = end (restart if looping, else stop). No song has class 0.
* **Class 2** (`Snd_StepPsg` 1bb4:0a1f, `Psg_*` 1bb4:041d..05f1, `Psg_StepEnvelopes` 1bb4:04a0):
  SN76496-style writes to the port DS:16ae with envelopes from the patches (FEh = loop mark).
  `g_hasPsg` is never set and the port never written.
* **Class 4** (MPU-401 / MT-32): the interpreter slot points to the speaker interpreter (DS:1aaa[4]
  = 0966); `Snd_ApplyChannelProgram` (1bb4:06d9) sends program, bend, reset-all-controllers and RPN
  0 = 2 semitones to MIDI channels 1..8, `Snd_SetMasterVolume` (1bb4:11a2) a Roland SysEx, and the
  note-off path "all notes off" (CC 7Bh). `g_hasMpu` is never set; no song has class 4.
* `Snd_ScaleSectionTempo` (1bb4:0fb2) and `Snd_ScaleDelay` (0f70): rewrite every delay of a
  section in place by a factor/256 with a carried remainder (DS:16b0); no caller.

---

## 10. Channel ownership and priorities [O]

### 10.1 `Snd_Play(slot, section, priority, loop)` 1bb4:10c1

```
s.loop = loop; if class ≠ 0: section must be < count (else −3); s.loopSection = section
if active[slot]:                                   (restart)
    busy; Snd_StartSection(section)
    for each channel used and not stopped: Snd_HwChannelOff(channel, class)
    Snd_SetPriority(slot, priority); restore busy
else:
    Snd_StartSection(section)
    for each used channel c (flags bit 7):
        o = owner[class][c]
        if o == free: take
        elif priority > o.priority (unsigned): Snd_StopOwnerChannel(o.slot, c) (its flags |= 2);
                                               Snd_HwChannelOff(c, class); take
        else: flags |= 2 (this song's channel stays silent)
        take: flags &= ~2; owner = slot | priority << 8; program = FFh; Snd_ApplyChannelProgram
    g_sndPriority[slot] = priority; active[slot] = 1
```

Equal priority does not steal: the earlier song keeps the channel. Confirmed by the
r2_warehouse_walk capture [O wave 2]: an effect started while another effect holds the channel at
the same priority records its program without loading the patch; when the holder ends, the patch
is loaded and the new effect sounds from its next note-on.

### 10.2 `Snd_SetPriority(slot, p)` 1bb4:102c

Stores p; if the song is active and p differs: if p > old: the channel loop of §10.1 again (it
can take channels from others, and re-takes its own, which reloads their patches); if p < old:
`Snd_ReleaseChannels` (§10.4).

### 10.3 `Snd_Stop(slot)` 1bb4:084a

If active: active = 0, busy, `Snd_ReleaseChannels`, restore busy. Returns 0 (also for an
inactive slot).

### 10.4 `Snd_ReleaseChannels` 1bb4:077c (ES:BX = the song giving up its channels)

1. For each channel used and not stopped: owner = 00FEh; flags |= 2; `Snd_HwChannelOff`.
2. For each channel whose owner is 00FEh: search slots 63..0 for an active song of the same
   class (classes 0 and 1 count as one) that uses the channel: a candidate replaces the current
   one if its priority ≥ the current owner priority (signed compare; the first candidate needs
   ≥ 0). So the highest priority wins and, among equals, the lowest slot. The winner is recorded
   as `slot | 80h` with its priority.
3. For each channel: owner 00FEh (nobody) → 00FFh (free); a marked winner → its slot, then
   `Snd_ApplyChannelProgram(winner, program)` (OPL: reload its current patch) and the winner's
   channel flags &= ~2. The winner resumes at its next note-on (nothing is re-keyed).

The song being released is still marked active during step 2 when called from
`Snd_SetPriority`, so it can win its own channels back.

The search compares priorities as signed bytes against a starting owner priority of 0, so a song
playing at priority 80h or higher never gets a channel back through a release (1bb4:07cf JL); the
candidate test checks only flag bit 7, not the stopped bit [O wave 2].

### 10.5 Hardware off and silence [O]

* `Snd_HwChannelOff(channel, class)` 1bb4:0641: classes 0/1: port 61h &= ~01h; class 2: PSG
  channel off; class 3: `Opl_NoteOff(channel)`; class 4: MIDI CC 7Bh on channel 1..9.
* `Snd_HwSilenceAll` 1bb4:0674: port 61h &= ~01h; PSG channels 3..0 (if present); OPL voices
  10..0 note-off (if `g_hasOpl`); MIDI channels 9..1 (if present).
* `Snd_StopOwnerChannel` 1bb4:0eec (near): sets bit 1 of the same channel record in the slot's
  song.

---

## 11. Driver API summary [O]

All are far C functions taking the slot first; they return 0 or a value, −2 for a bad slot, −3
for a bad section.

| Function | Effect |
|---|---|
| `Snd_Init(dev)` 1bb4:1234 | §3.1 |
| `Snd_RegisterSong(p)` 0ebc, `Snd_AllocSlot(s)` 0e71, `Snd_Free(slot)` 0d8f | §4 |
| `Snd_Play(slot, section, priority, loop)` 10c1 | §10.1 |
| `Snd_Stop(slot)` 084a | §10.3 |
| `Snd_SetPriority(slot, p)` 102c, `Snd_GetPriority` 0c34 | §10.2 |
| `Snd_IsActive(slot)` 0c1e | `g_sndActive[slot]` (−2 for a bad slot) |
| `Snd_SetCue(slot, tick, callback)` 0be5 | under CLI: +07h = tick, +09h = far callback |
| `Snd_GetTick(slot)` 0c0c | +03h |
| `Snd_SetLoop(slot, v)` 0bba, `Snd_GetLoop` 0bd1 | +0Dh |
| `Snd_SetSection(slot, n)` 0c72, `Snd_GetSection` 0c4a | +4Fh (no restart) |
| `Snd_SetLoopSection(slot, n)` 0c91, `Snd_GetLoopSection` 0c5e | +4Eh |
| `Snd_GetSectionTranspose(slot, n)` 0cb0, `Snd_GetSectionNext` 0cd2, `Snd_SetSectionNext(slot, next, n)` 0cf4 | the section entry's +3 / +2 |
| `Snd_SetTranspose(slot, n, t)` 0d26 | entry[n].transpose = t; if n is the current section: live transpose +50h = t (class 4: all notes off first). Sounding notes keep their pitch |
| `Snd_MuteChannel(slot, ch)` 0f01 | busy; if the channel is used and flags & 3 = 0: flags \|= 1 and `Snd_HwChannelOff`; else flags \|= 1. **ch is not range-checked** (§13.4) |
| `Snd_UnmuteChannel(slot, ch)` 0f50 | flags &= ~1 (nothing is re-keyed) |
| `Snd_PauseLock` 0b7f / `Snd_ResumeUnlock` 0b90 | §12.1 |
| `Snd_Silence` 0b97, `Snd_Off` 0e15 | §12.3 |
| `Snd_SetMasterVolume` 11a2, `Snd_SetChannelVolume(ch, v)` 11df, `Snd_GetChannelVolume` 1225 | no caller |

---

## 12. Pause, the sound switch, silence

### 12.1 Pause [O]

`Snd_Pause` (1cdd:05db) → `Snd_PauseLock` (1bb4:0b7f): if the busy word DS:165d is 0: busy = 1
and `Snd_HwSilenceAll`. `Snd_Resume` (1cdd:05e5) → `Snd_ResumeUnlock` (1bb4:0b90): busy word = 0.

* Users: `Dlg_RunModal` (3f56:06ac … 088f) and the menu drop-down (40a4:020b).
* While paused no sound tick runs: songs, cues and delays stand still, so music resumes where it
  stopped. Sounding notes were cut by the silence and are **not** re-keyed: each channel stays
  quiet until its next note-on.
* Not nestable: an inner resume unlocks for the outer pauser too.
* The dialog loop pauses **before** it draws its box (3f56:06ac, then `Dlg_Draw`), so a sound
  started just before a dialog (the hint chime, §15) gets at most the time until then.

### 12.2 The sound switch (Ctrl+S, Options > Sound & Music) [O]

* `Snd_ToggleOption` (1cdd:05c8, from `Input_GetCode` on code 0x13): `g_soundMuted` DS:1ad4 ^= 1;
  `g_soundOff` DS:45cc (the player's option) ^= 1; `SndRes_ApplySoundOption`.
* `SndRes_ApplySoundOption` (1cdd:0592): if DS:1ad4 ≠ 0: `Snd_Play(slot of the global effect bank's
  effect 0, section 0, priority 7Fh, loop 1)`; else `Snd_Stop` of that slot.
* Effect 0 of bank 0x105B / 0x105C is a **silence song**: speaker version: note-offs on channels
  0..3; FM version: program 0 on voices 0..10; then 65 278 ticks (14.9 minutes) of nothing,
  looping [O, `bmf.py 105b/0`, `105c/0`]. Started at priority 7Fh it takes every channel it uses
  from every other song (whose channels are stopped and silenced) and holds them; the other songs
  keep running silently (time, cues and section loops go on). Turning the sound on stops it, and
  `Snd_ReleaseChannels` gives each channel back to the highest-priority song that wants it.
* Other writers: the sign-in copies the player's option into DS:1ad4 and applies it
  (2caa:136a..1370); Options > Sound & Music (`Cmd_ToggleSound` 4106:011e) sets DS:45cc from the
  dialog, copies it to DS:1ad4 (4106:0143) and applies it. `Player_InitRecord` stores DS:45cc =
  DS:1ad4 (2b5a:002f), but the memcpy of the zeroed record over DS:4562 (2b5a:0042) overwrites it:
  **a new player's DS:45cc is always 0**, and the sign-in does not apply it for a new player, so
  when the sound was off DS:1ad4 stays 1 with DS:45cc = 0 and the next Ctrl+S inverts the two
  flags (sound on, option "off") (`code_ui.md` §7) [O code; I effect]. At start-up DS:1ad4 = 0
  (sound on).
* Quirks [O code + data; I effect]: the speaker silence song does not use channel 8, so the two
  speaker effects that do (0x11CA members 12 and 13) still sound with the sound switched off; an
  effect started with a priority above 7Fh (the steal compare is unsigned) would also get
  through — none of the code's fixed priorities does (the largest is 32h + 3Fh = 71h; data-driven
  priorities are open, §15).

### 12.3 Silence and shutdown [O]

* `Snd_Silence` (1bb4:0b97, device 0 at start-up): if enabled: lock, `Snd_HwSilenceAll`,
  `g_sndEnabled` = 0, unlock. Afterwards the interpreters keep running, `Opl_SetPatch` and
  `Opl_SetVolume` still write their registers, but no key-on, frequency, BD or speaker gate-on
  write happens: the OPL is configured but never keyed (visible in an OPL capture). The speaker
  gate-off writes still happen: `Snd_HwChannelOff` 1bb4:0648 and `Snd_HwSilenceAll` 1bb4:0674 write
  port 61h &= FEh without checking `g_sndEnabled` [O wave 2].
* `Snd_Off` (1bb4:0e15, `Game_Shutdown`): lock; every active slot: inactive and every channel
  record flags |= 2; `Snd_HwSilenceAll`; all 64 owner words = 00FFh; unlock.

---

## 13. The bank layer (1cdd)

### 13.1 Handles [O]

`g_sndTable` DS:9d48: 60 × i16, −1 = free (`SndRes_Init` 1cdd:0004 sets all to −1, then loads
the global effect bank 0x105B into `g_sndGlobalEffects` DS:9dc0). `g_sndBankMem` DS:9c58: 60 far
pointers to the loaded bank copies.

| Kind | Loader | Handle h | Table use |
|---|---|---|---|
| music bank | `SndRes_LoadMusicBank(id)` 1cdd:0024 | first free index from **0** | table[h] = slot; mem[h] = bank |
| effect bank | `SndRes_LoadEffectBank(id)` 1cdd:012d | (first free index from **5**) + 1 | table[h − 1] = n; table[h + k] = slot of effect k (or an error code); mem[h − 1] = bank |
| single song | `SndRes_LoadSong(id)` 1cdd:01e0 | first free index from 5 | table[h] = slot; mem[h] = the BMF entry |

* Every loader reads `Res_LoadCopy(id + g_fmPresent)` (a private copy).
* Music bank: walks members from 0, following each member's payload (member + 2): a payload
  starting with 4 stores its word at payload + 2 in `g_sndBankCueTick` DS:9dc2; the walk stops at
  the first payload starting with 0 (the `I 0` member); the **next** member is the song and is
  registered. A failed registration frees the copy and sets table[h] = −1, but h is still
  returned. No search limit check: with no free entry h = 60 (out of the table).
* Effect bank: n = (first offset / 2) − 1 members, all registered in order; a `C FF` member is
  not BMF and leaves −6 in its entry (playing it does nothing). The n + 1 entries from the free
  index on are used **without checking that they are free or below 60** [O; I: never happens with
  the banks loaded at once].
* Freeing: `SndRes_FreeMusicBank(h)` 1cdd:0237 (`Snd_Free(table[h])` if ≥ 0, `Mem_Free`, table[h]
  = −1, DS:1ad8 = −1); `SndRes_FreeEffectBank(h)` 1cdd:027d (`Snd_Free` of the n effects, entries
  −1, DS:1ad6 = −1, `Mem_Free(mem[h − 1])`); `SndRes_FreeSong(h)` 1cdd:02e3.

### 13.2 Playing [O]

* `SndRes_PlayMusic(h, section, loop)` 1cdd:031a: priority = the signed byte after a member whose
  payload starts with 3, searched before the `I 0` member (none in the data → **0**); if
  `Snd_IsActive(table[h])` is 0: DS:1ad8 = table[h]; `Snd_Play(table[h], section, priority,
  loop)`. An already playing music is not restarted.
* `SndRes_PlayEffect(h, loop, p)` 1cdd:03c9: DS:1ad6 = table[h]; `Snd_Play(table[h], 0, p + 3Fh,
  loop)` (restarts the effect if it is playing).
* `SndRes_StopMusic(h)` 1cdd:03ef / `SndRes_StopEffect(h)` 1cdd:0409: `Snd_Stop(table[h])`.
* `SndRes_StopAllEffects()` 1cdd:0521: stops the n effects of the screen effect bank
  `g_effectBank` DS:7a44.
* `SndRes_IsPlaying(h)` 1cdd:056d: 0 if table[h] < 0, else `Snd_IsActive`.
* `SndRes_GetSlot(h)` 1cdd:055f and its duplicate `SndRes_GetSlotB` 1cdd:0551: table[h].
* `SndRes_MuteChannel(h, ch)` 1cdd:04ef, `SndRes_UnmuteChannel(h, ch)` 1cdd:0508.
* `SndRes_Nop()` 1cdd:05ef: empty (called by the sign-in, 2caa:0fcf).

### 13.3 Arrangements: `SndRes_SelectArrangement(h, n)` 1cdd:0423 [O]

The `M` members of a music bank are lists of channels to **mute** (payload `01 c c … FF`):

```
unmute channels 0..15 of table[h]
if n == 0: done                                       (all channels play)
walk members from 0: payloads starting with 1 are counted 1, 2, ...; stop at the n-th
    (or at the first payload starting with 0 when there are fewer than n lists)
from the byte after the first: Snd_MuteChannel(table[h], (signed) byte) for each byte until FFh
```

Arrangements in use: title music 0x1020 / 0x1021 (sign-in n = 1, hub n = 2, attract none), room
music 0x35F.. (n = room 1..3, else 4, plus the room code's own mutes, §14.1), workshop 0x27 /
0x28 (n = 1; 3 while the swap-file sub-screen is open; 2 from the cue that never fires), races,
the final movie (n = 1..4, then 0, §14.3) [O call sites, §15].

### 13.4 Two original data/code hazards [O code + data]

* **No end check.** When a bank has fewer than n lists, or a list is not terminated by FFh, the
  mute loop runs on through the following members' headers and data until it meets an FFh byte,
  and `Snd_MuteChannel` accepts any channel number: a "channel" c ≥ 16 or < 0 ORs bit 0 into the
  song block at offset 0Eh + 4c.
* **The movie's speaker list 4 hits it.** List 4 of bank 0x30CA is `01 01 02 03 FD` (FDh instead
  of FFh). Selecting it mutes channels 1, 2, 3, then "channel −3" (ORs 1 into the song's +02h,
  the **high byte of the delay countdown**: +256 ticks), then the next member's header bytes 4Dh
  (channel 77: offset 142h, a delay byte 03h that stays 03h) and 00h, then 01, 02, 03, 04, and
  stops at that member's FFh. Effect in the final movie on the speaker (§14.3): the 4th pass of
  the music is completely muted (channels 0..4) and its end is postponed by 256 ticks (3.5 s).
  The FM bank 0x30CB is correct (list 4 = `01 08 FF`).

---

## 14. Music cue callbacks (12e0) [O]

Cues are set with `Snd_SetCue` and run at interrupt time inside `Snd_Tick` (§5).

### 14.1 Room music: `Music_RoomCue` 12e0:007f

Room music is music bank 0x35F + 2·building (`Rooms_Load` 1622:0096 → `g_roomMusicBank` DS:09e6).
`Room_Enter` (1622:0411) for room 0 (the workshop) stops it (`SndRes_StopMusic`); for room r
≥ 1 it calls 1622:03b6 with n = r (1..3) or 4: `SndRes_SelectArrangement(DS:09e6, n)`, mute
channels 4 and 5, and if the room's flag DS:a01c[r] is set, unmute 5 (level > 2) or 4; then
1622:0346: if the music is not playing: `SndRes_PlayMusic(DS:09e6, 0, loop 1)`, `g_roomMusicStep`
DS:08be = 0, `Snd_SetTranspose(slot, 0, byte DS:010e[6·building])`, `Snd_SetCue(slot,
DS:0a06[building], Music_RoomCue)`.

`Music_RoomCue`: DS:08be = (DS:08be + 1) mod 6; `Snd_SetTranspose(slot, 0, byte DS:010e[12·building
+ 2·DS:08be])`.

| Building | Cue tick DS:0a06 | Song length speaker / FM |
|---|---|---|
| 0 Automotive | 1260 | 1260 / 1260 |
| 1 Aircraft | 3555 | **2370** / 3555 |
| 2 Alternative Energy | 2119 | 2119 / 2119 |

`g_roomMusicTransposes` DS:010e: 3 rows (buildings) of 6 little-endian words (steps 0..5), the low
byte used as a signed semitone transpose; step 0 is 0 in every row and the values stay within a
few semitones [O bytes].

Each time the music reaches its end (cue = length) the next step's transpose applies to the next
pass: the room music changes key every pass. Two original bugs [O code + data]:

* The start transpose is read with a stride of 6 instead of 12: building 1 starts with building
  0's step 3 (a transpose of 2), building 2 with building 1's step 0 (0, the right value by
  coincidence).
* On the speaker the Aircraft music is 2370 ticks long, so the counter never reaches 3555 and the
  key never changes.

### 14.2 Workshop: `Music_WorkshopCue` 12e0:00bd

`State_06_Workshop` loads effects 0x29 (→ DS:7a44) and music 0x27 (→ `g_musicBank` DS:9dc4),
sets `g_workshopArrangement` DS:4558 = 1 and the cue count DS:4556 = 0, selects arrangement 1,
plays the music looping, and sets the cue to DS:9dc2 with this callback. The callback counts;
at the 2nd call it sets DS:4558 = 2 and, unless the swap-file sub-screen is open (DS:2198), selects
arrangement 2. DS:9dc2 holds 4902h / 4905h (the misread `L` member, fmt_sound.md §2.1), far beyond
the 653 / 1307-tick songs whose loop resets the counter: **the callback never runs** and the
workshop always plays arrangement 1 (3 while the sub-screen is open: 2dee:17eb / 185d).

### 14.3 Final movie: `Music_MovieCue` 12e0:00ea

`State_15_FinalMovie` (2fcb:0640): music bank 0x30CA → `g_movieMusicBank` DS:4552; DS:4554 = 1;
cue 250h (592 = the song length) → this callback; `SndRes_PlayMusic(…, 0, loop 1)`; arrangement 1;
effect bank 0x108D for the movie cues; `Movie_Play` (code_runtime.md §15); then the effects are
freed, `Input_WaitCode` (the music goes on), the music bank is freed.

The callback: DS:4554 += 1; if < 5: arrangement DS:4554; if = 5: arrangement 0 (all); if = 7:
DS:4554 = 0 (no change). So pass by pass: 1, 2, 3, 4, all, all, all, 1, 2, … On the speaker the
switch to arrangement 4 triggers §13.4 (a silent pass, 256 ticks longer). The movie lasts about
202 × 0.124 s + 4 × 10 s ≈ 65 s, about 8 passes of 8.13 s [I].

Verified by r2_movie_race15 [O wave 2]: `Music_MovieCue` switches the arrangement at every 592-tick
pass, write for write, over four passes; the cue table's effects play as listed (effect 1 looping
(type 5), effect 9 every 4 frames, 5 and 2). Two calls not in the table follow at the movie's end:
effect 1 stopped and effect 6 played.

---

## 15. Who plays what [O call sites; I for the "when" of game-logic functions]

Banks are loaded per screen into `g_effectBank` DS:7a44 (effects) and a music handle; "fx k" is
effect k of the current effect bank; "glob k" is effect k of 0x105B (DS:9dc0); priorities are
the driver priorities (effects: argument + 3Fh).

| Caller | Sound | Loop | Priority | When |
|---|---|---|---|---|
| `SndRes_Init` 1cdd:0015 | load 0x105B (global effects) | | | start-up |
| `SndRes_ApplySoundOption` 1cdd:0592 | glob 0 (silence song) | 1 | 7Fh | sound switched off (§12.2) |
| `State_01_Attract` 2c11:0923..0940 | load fx 0x1022, music 0x1020 → DS:9dc4; play music | 1 | 0 | attract start; freed at its end (2c11:0972..0980) |
| `Attract_TitleAnim` 2c11:04c7, 2c11:0580 | fx 12, fx 13 | 0 | 43h | title animation steps |
| 2c11:05cf → 2c11:0620 | `SndRes_StopAllEffects` | | | end of the title animation |
| sign-in 2caa:1259, 2caa:13b8..13ed | load 0x1020 → DS:0098 if not loaded; fx 0x102F → DS:7a44; play DS:0098; arrangement 1 | 1 | 0 | sign-in screen |
| sign-in 2caa:0ce3 | fx 0 | 0 | 40h | sign-in action [I which] |
| sign-in 2caa:1370 | apply the player's sound option | | | player chosen |
| `State_03_Hub` 12e0:0167..0186 | load 0x1020 → DS:0098 if needed; play; arrangement 2 | 1 | 0 | hub (coming from the sign-in the music is already playing: `SndRes_PlayMusic` does not restart it, only the arrangement changes) |
| `State_04_BuildingSetup` 12e0:0831 | free DS:0098 | | | entering a building |
| `Rooms_Load` 1622:0096 | load room music 0x35F + 2b → DS:09e6 | | | building set-up |
| `Room_Enter` 1622:0411 | room music arrangement, start, transpose, cue (§14.1); room 0: stop | 1 | 0 | every room entry |
| `State_07_Warehouse` 12e0:0634 | load fx 0x1039 → DS:7a44 (freed 12e0:07e6) | | | warehouse loop |
| `Screen_Wipe` 1473:0535 / 073e | fx 0x19 + rand(2) | 1 | 49h | room entry with a wipe; stopped at its end |
| `State_08_LeaveRoom` 12e0:09e3 | stop room music | | | leaving the rooms |
| 174b:01af / 01bf / 01cd | fx k from animation sequence data: start (loop and priority from the data word: high byte loop, low byte priority), stop, or wait while playing | data | data + 3Fh | `S` animation sequences |
| 1834:04ce / 04ee | fx k from actor data, only when the actor is in the current room and the effect is not already playing; stop | data | data + 3Fh | actors (chimps) |
| 18f5:00a1 | fx 1 | 0 | 43h | player actor |
| `Hud_Update` 1a04:0c96 | glob 5 | 0 | 44h | HUD dirty bit 08h set (the banana counter is redrawn, `code_vehicles_races.md` §4.2) |
| `Hud_Update` 1a04:0d15 | glob 1 | 0 | 44h | HUD dirty bit 04h set (the score `g_score` is redrawn there) |
| `Hud_TrashCursorPart` 1a04:0b40 | glob 3 | 0 | 44h | inventory command 6000 (the trash can) |
| `Hud_ScrollLeftArrow` 1a04:08e9, `Hud_ScrollRightArrow` 1a04:0888 | glob 6 | 0 | 44h | inventory commands 6002 / 6003 |
| `Hud_SelectPart` 1a04:09ed | glob 4 | 0 | 44h | a part taken from the strip |
| `State_06_Workshop` 2dee:1bab..1bfd, 1c6c | load fx 0x29, music 0x27; arrangement 1; play; cue (§14.2); fx 10 | 1 / 0 | 0 / 44h | workshop |
| `Workshop_Run` 2dee:0ec4..1387 | fx 0, 0, 4, 4, 2, 12, 1, 3 | 0 | 40h | workshop actions |
| 2dee:00a6 (0144, 0174) | fx 13, fx 14 | 0 | 42h, 43h | workshop |
| 2dee:0880 / 0986 | fx 5 when neither fx 4 nor fx 5 is playing; stop fx 5; fx 6 | 0 | 41h, 42h | workshop |
| 2dee:17eb / 185d | arrangement 3 / back to DS:4558 | | | swap-file sub-screen open / closed |
| `Race_LandEnergy` 3dd6:0775..0791, 0799 | music DS:3994[b] (0xF7, 0xF9) → DS:baac, arrangement 1, play; fx 0xF4 → DS:baaa | 1 | 0 | land and energy races |
| `Race_Run` 3dd6:0225..02a4 | reads the music tick; fx 0 / fx 1 (looped); arrangement by race state | 0 / 1 | 49h | race frames [I] |
| 3dd6:0186 / 0196 | fx from data, stop | 0 | 49h | race events |
| 3dd6:0409 | `SndRes_StopAllEffects` | | | race end |
| `Race_Aircraft` 3e77:0ad7..0cf3 | music 0x262 → DS:bbb2, fx 0x260 → DS:bbb0; arrangement 1; play; stop and free at the end | 1 | 0 | aircraft races |
| 3e77:04cc, 05e3 | fx from data; fx 2 looped; arrangement by race state | 0 / 1 | 42h | aircraft race frames |
| `State_12_RaceResult` 2fcb:033a, 0419..0426 | fx 0x108F → DS:7a44; music 0x1089 or 0x108B, **not looping**; at the end busy-waits while it plays (2fcb:04f1..04fa), then frees both | 0 | 0 | race result |
| `State_15_FinalMovie` 2fcb:0644..06ce | movie music and cues (§14.3) | 1 | 0 | final movie |
| `Movie_Play` cues 1473:0870 / 0884 / 089b | fx a of 0x108D: start (type 1, loop 0; type 5, loop 1), stop (type 2) | 0 / 1 | b + 3Fh | movie frames (code_runtime.md §15) |
| `Hint_Show` 3060:018c..01c7 | single song 0x1056, then the hint box, then freed | 0 | 71h | a New Player Hint; the dialog pauses the driver before drawing (§12.1), so the chime probably sounds at most for an instant [I] |
| puzzle helpers ovl19 3924 | own bank table DS:b146 (load from an id + DS:a0d8, register every member), `Snd_Play` directly with section 0, loop 0 or 1 and the **raw** priority, stop, stop all, set transpose, busy-wait until a song ends (3924:005b: no input polling) | | raw | puzzles |

Data-driven priorities, an example [O wave 2]: the workshop race-door screen animation 0x1d plays
effects 13 and 14 with b = 3 (loop 0, priority 3 = 42h; words 94 and 109 of the resource);
`Workshop_UpdateRaceDoor` then plays 14 again at priority 4, which reloads its patches.

Race results (State_12), seen in all four race captures [O wave 2]: the results music at tick P
(0x1089 won, 0x108B lost); global effect 1 at P, then every 2 sound ticks from P + 19 for 49 plays
during the count-up (on odd PIT ticks in the Alternative Energy run); effect 0 of 0x108F at P + 121
and effect 1 at P + 381 (the rank-up / post-race box). A loss has no count-up. Race arrangements
seen: Aircraft 1, then 2 at +92, 3 at +109, 2 at +624; Alternative Energy 1, 2 at +207, 3 at +832, 2
at +940; Automotive 1, 2 at +312.

---

Round 2 (REFERENCE_CAPTURES §10.6): the puzzle screens play no music (the room music is stopped by
state 8 and no puzzle starts one; only effects sound), the OPL writes of every `.dro` fall on a
13.731-ms grid, and every DOSBox-X `.dro` starts with about 100 register writes at time 0 (the
emulator's register dump when the capture opens, at the first key-on) [O].

---

## 16. tmport's `src/dos/audio` and G&G

| tmport part | G&G use | Notes |
|---|---|---|
| `opl_chip` (ymfm OPL2, `OplWriter`) | **as is** | rhythm mode is a chip feature (BDh); the per-write time model (70 µs measured for TM DOS at `cycles=fixed 6000`) was re-measured for G&G in wave 2: **65 µs** per `AdLib_Write` at 26800 cycles (port I/O dominates), fitted over 7809 title-capture writes with only 45 whole-ms stamps off (`GgAudio::kOplWriteSeconds`) |
| `speaker_synth` (PIT channel 2 mode 3, gate) | **as is** | G&G writes `OUT 42h` low/high and port 61h \|= 3 / &= ~1, exactly the model's inputs |
| `synth_common`, `opl_backend` | `synth_common` as is; `opl_backend` **no** | `OplBackend` (playTune, noteOn, noteOff, restore) is TM's AdLib-driver API and stays in tmcore; G&G writes registers to `OplChip` (an `OplWriter` and `OplOutput`) |
| `adlib_driver` (TM's 4ac9 melodic driver) | **no**, except the F-number table | G&G has no orchestrations, timbre records or key-off-then-on; but `setFNumRow` produces G&G's DS:1852 table exactly (or read it from `ssg.exe`) |
| `sequencer` (TM's 491b: one music + one effect block, TUNES/SFX formats, effect-freezes-music rule) | **no** | G&G's driver is slot based with per-channel ownership and priorities; write a new `BmfDriver` from §4-§12 |
| `sound_data` | no | BMF and banks: new parser (`tools/gg/bmf.py` is the reference) |
| `dos_sound_renderer` | **generalised in wave 2** | the core's `dos::ChipRenderer` (`dos/audio/chip_renderer.h`): any `SoundTickSource` ticked at `ChipRenderConfig::tickHz` = `PitTimeBase(kPitDivisorGg).hz() / 2` = 72.8260295 Hz (not `synth_common`'s rounded 1 193 182 / 16 384 = 72.8260498 Hz), the same fractional sample accumulator and tick hook; TM's `DosSoundRenderer` is built on it |
| `dos_audio_device` (SDL stream, command queue, offline render) | **generalised in wave 2** | the core's `dos::ChipAudioDevice` (`post(fn, delay)`, offline render) and `dos::OplWriteLog` / `OplLogTap` for `.dro` comparisons; TM's `DosAudioDevice` is built on it. G&G's `GgAudio` (gg/audio/gg_audio.h) runs its calls synchronously under the render lock instead of queueing them |
| `psg_synth` | no | class 2 is dead |
| `dos_sound_cli` | pattern | done as `gg/audio/audio_cli.h` (`--gg-sound`, `--gg-oplog`, `--gg-dro-compare`, `--gg-songs`, `--gg-soundtest-check`); `tools/dos/oplcmp.py` is TM-specific (TICK_MS, the AdLib_PlayTune start detection): for G&G use `--gg-dro-compare` |

Differences that matter for fidelity [O]:

* Sound tick 72.826 Hz from the PIT; delays in sound ticks; no tempo field.
* Cue callbacks run inside the sound tick (interrupt time) and can change arrangements and
  transposes; in the engine they must run on the audio side at the exact tick, or be queued to
  the game thread with the tick number [R: run them in the driver, they only touch driver and
  bank state].
* `Snd_Pause` stops sound time entirely; the dialog tick freeze is separate (code_runtime.md §1.2).
* The OPL writes of `Opl_SetPatch` and `Opl_SetVolume` happen even when the output is disabled
  (`-SN`), which matters only for OPL-log comparisons.

---

## 17. Engine notes [R]

1. Port the driver literally: the slot table, the owner table and the release search order
   (slots 63..0, ≥, lowest slot wins ties), the unsigned compare in `Snd_Play` and the signed one
   in the release, the no-catch-up busy rule (can be dropped), the per-class interpreters.
2. Keep each song's run-time block mutable per loaded copy, as the original (the movie bug writes
   into it).
3. Keep the bank layer's handle numbering exactly (music from 0, effects and songs from 5, effect
   handle = base + 1): game code adds effect numbers to handles.
4. Keep the original's data bugs (movie list 4, the room-music stride, the workshop cue) behind
   no option: they are what the game sounds like. A mod or a fixed-data option can address them
   later.
5. Run `Snd_Tick` from the virtual PIT on even ticks (code_runtime.md §20, item 1); `Timer_ISR`'s
   order (tick increment, then the sound tick) decides whether a sound starts in the same tick as
   a deadline.
6. Speaker: one `SpeakerSynth`; OPL: one `OplChip`; class 2 and class 4 not needed.

---

## Open questions

1. Data-driven effect priorities (animation sequences 174b, actors 1834, race tables): do any
   exceed 40h (driver priority ≥ 80h), which would get past the silence song? (Partly answered: the
   workshop race door's are 3, §15.)
2. Whether the hint chime (0x1056) is audible: compare a DOSBox-X capture of a hint with the
   expected few ticks (§12.1, §15).
3. The movie's speaker music (§13.4): confirm the silent 4th pass and the 3.5-s delay in a
   `-si` capture of the ending.
4. The Aircraft room music on the speaker (§14.1): confirm that it never changes key.
5. *Answered in wave 2:* `AdLib_Write` takes 65 µs in the G&G DOSBox-X profile (§16).
6. The race arrangement logic (3dd6:02a4, 3e77:0680) and what `Snd_GetTick` of the race music is
   used for (3dd6:0225): race spec.
