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
and 0x4905 (about 4.3 minutes) [O]. So the workshop's end-of-song cue probably fires much later
than intended, or never, since a section restart (1bb4:0045) clears the tick counter [I].

What the `M` channel lists select is to be traced in wave 1, with the calls that take a bank
and small numbers (2fcb:0640 calls `1cdd:031a(bank, 0, 1)` and `1cdd:0423(bank, 1)`; 1cdd:031a
walks the bank's members like 1cdd:0024 and reads a parameter from a member starting with 3).
The workshop's two arrangements differ in length (653 and 1307 ticks), so the two devices do
not play the same music note for note [O].

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
| `Snd_Init(device)` 1bb4:1234: clears the 64 slots; detects the AdLib (timer test 1bb4:0142); returns caps: bit 0 speaker, bit 1 PSG (DS:1683), bit 2 OPL (DS:1684), bit 3 MPU (DS:1685) | DS:1681..1685 = class available (1, 1, 0, 0, 0 in the file; the OPL entry is set by the detection) |
| `-S` switch: N, '-' or none = off, I = speaker, A/B/M = FM; default -1 = detect | 303a:011c |
| Register a song: 1bb4:0ebc checks the magic and calls 1bb4:0e71, which takes the first free slot (far pointers DS:1458[64]) if the song's class is available (DS:1681[class] == 1); results: slot, -1 no slot, -4 class not available, -6 not BMF | |
| `Snd_Tick` 1bb4:0db3: for each active slot (DS:1558[64]): +3 tick counter += 1, call the cue callback when it equals +7; +1 delay -= 1; at 0 run the class interpreter (DS:1aaa[class]) | every 2nd PIT tick from the timer ISR 1454:0096 |
| Section start 1bb4:0045: delay = 1, tick = 0, clear the channel bends, transpose from the section entry, skip the initial delay byte | |
| Channels per class DS:1658 = 1, 16, 4, 11, 16 | |
| Speaker: note + transpose → PIT divisor table DS:1816, pitch bend 1bb4:0541, port 61h gate | 1bb4:0966 |
| OPL: note on 1bb4:0343 (transpose, bend, 96-entry note tables DS:1712 / DS:1792), note off 1bb4:0302, volume 1bb4:0218 (scales the TL by the master DS:1686 and channel DS:1688 volumes and the velocity), `AdLib_Write` 1bb4:00a9 (6 and 35 status reads, the same delays as TM DOS) | 1bb4:0ad6 |
| Lock DS:165d (set while the game changes slots; `Snd_Tick` does nothing while it is 1) | |

Ctrl+S toggles sound (1cdd:05c8); dialogs pause it (1cdd:05db / 05e5). Treasure Mountain DOS's
driver (TUNES/SFX, 236.7 Hz, per-device files) is a similar idea with a different format
(`re/dos/docs/fmt_sound.md`); TM Windows uses MIDI files and PCM, so nothing there applies.

---

## 5. Porting notes [I]

* Parse BMF exactly as in §3; keep the song block as mutable state per slot, because the driver
  keeps its state there (a port can copy it on registration).
* Tick the driver from the virtual PIT: one `Snd_Tick` per two PIT ticks at divisor 0x2000.
  Sound events that happen inside long frames need the timing model TM DOS needed
  (`re/dos/docs/fmt_sound.md` §11.8).
* The OPL path reuses tmport's ymfm OPL2 (rhythm mode included); the speaker path the speaker
  synth. Reference: DOSBox-X raw OPL captures (`.dro`) compared write for write with
  `tools/dos/oplcmp.py`, as for TM DOS.
* Choose the arrangement exactly as the game does: id + 1 when an OPL is present, else id; an
  effect slot whose speaker member is `C FF` stays silent on the speaker.

## 6. Open questions (wave 1)

* The class interpreters in full: speaker monophony when several channels sound (which note
  wins), the note and divisor tables, the bend formula, OPL voice allocation, the volume
  formula, priorities between slots (1bb4:102c) and what the game's API calls (1cdd:031a,
  0423, 03c9, 0409, 055f) do with banks, lists and slots.
* The `M` channel lists of music banks, member kind 3 (1cdd:031a reads a parameter from a
  member starting with 3; none in the data), and what the callbacks 12e0:00bd / 12e0:00ea do.
* Whether the workshop music loops (1cdd:0423) and so whether its misread cue (§2.1) ever
  fires; check in DOSBox-X.
* Why 0x105b/0 has 12 bytes after its end event.
* Classes 0 (divisor lists), 2 (PSG) and 4 (MPU) are not used by the data; the PSG and MPU flags
  are never set (scoping §1). Confirm that no code path can select them.
* Tempo: the format has no tempo field; check that no song or API call changes the tick
  rate.
