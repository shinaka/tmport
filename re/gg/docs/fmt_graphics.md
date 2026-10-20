# Gizmos & Gadgets (DOS v1.0): pictures, palettes, fonts, animation scripts and the movie

Scope: everything G&G draws from data. LPX pictures and their blitters, palette resources and
the shadow palette, how pictures and palettes are paired, the two `fnt` fonts, the DAM
animation scripts (compared with TM Windows' DAM), the final movie, and the screen geometry.
The archive that holds all of it is in `fmt_data.md`; the frame buffer, dirty tiles, fades and
wipes are engine behaviour and are summarised here only as far as the formats need them (the
platform spec of wave 1 owns them).

Evidence: the G&G Ghidra export (addresses `SSSS:OOOO`, Ghidra segment = image segment +
0x1000; overlays at 2a80..4140), `DS:XXXX` = DGROUP 1ea7 = file offset `0xFC70 + XXXX` of
`ssg.exe`. Code that the scoping export had not disassembled (the blitters reached through the
tables at DS:01fc / DS:0204) was read from raw bytes; the wave 0 project has them as functions
(`Lpx_BlitRaw`, `Lpx_BlitRawKeyed`, `Lpx_BlitRle`, `Lpx_BlitRleMirror`). Decoders: `tools/gg/lpx.py`, `tools/gg/fnt.py`,
`tools/gg/dam.py`, `tools/gg/movie.py`; previews under `extracted/previews/gg/` (`png/`,
`pal/`, `fonts/`, `dam/`, `movie/`).

Labels: **[O]** observed in code, bytes or decoder output; **[I]** inferred.

---

## 1. Screen and frame buffer (summary) [O]

| Item | Value | Where |
|---|---|---|
| Video mode | VGA/MCGA mode 13h, 320x200, 256 colours, linear; set by INT 10h AX=0013h | 12e0:0b92 |
| Frame buffer | one 320x400 byte buffer (`Mem_Alloc(0x1F400)`): rows 0..199 = the screen being composed, rows 200..399 = a back page holding the clean background | 1473:0074 |
| Row table | 400 far pointers at DS:5f22, stride 0x140; every primitive draws through it | 1473:0074 |
| Sprite erase | copy rows y + 200 to y (`Gfx_RestoreRect` 1399:0233); the reverse copy, screen rows y to y + 200, saves a drawn background into the back page (`Gfx_SaveToBack` 1399:02a2, wave 0 name `Gfx_CopyFromBack`) | `code_runtime.md` §10.3 |
| Present | 40x25 map of dirty 8x8 tiles (in the code segment at 1581:0006); `Dirty_Flush` 1581:052a draws the software mouse cursor into the buffer and copies each dirty tile to A000; no retrace wait | 1581:03ee / 052a |
| Play area | 320x165 (rows 0..164): rooms, puzzles, races; `1473:0337` = copy back page rows 0..164 | |
| HUD | rows 165..199 (35 pixels): score, counters, parts strip | 1a04:00eb, 1a04:0b69 |
| Palette | 768-byte shadow at DS:024e (6-bit values), loaded into the DAC by `Pal_SetRange` 1399:05d6 | §3 |
| Bypasses | the random wipe (rows 9..164, column spans, straight to A000) and the movie (straight to A000) | 1473:0479, 1473:07d9 |

The screen is shown at 4:3 like TM DOS's 320x200 (tmport's `PresenterLayout::dos1990()`).

---

## 2. LPX pictures (types G and P) [O]

### 2.1 Header

All fields little-endian.

| Offset | Size | Field |
|---|---|---|
| +0 | 4 | `'L' 'P' 'X'` 0x1A |
| +4 | 2 | format: 0 = raw, 1 = RLE |
| +6 | 2 | width |
| +8 | 2 | height |
| +10 | 2 | key; the keyed raw blit compares pixels with its **low byte** (1399:03aa `MOV DH,ES:[SI]`) |
| +12 | 2 | data offset, relative to +12 |
| +12 + data offset | | pixel data |

* 2959 pictures (502 entries, 2457 bundle members), every one decodes to its exact byte count
  [O, `lpx.py --check`]: 904 raw, 2055 RLE.
* Data offset 2 in 2904 pictures (data at +14). The other 55 (the RLE frames of the DAM frame
  bundles 0x1157..0x1161 except 0x115f) have offset 0, so the stream starts with the offset
  word itself, `00 00`, which the RLE decoder reads as "skip 0 pixels"; their key is 0x0101.
  The result is the same.
* Key: 1 in 2903 pictures, 0x0101 in 55, 15 in one 1x1 picture (0x0004).
* Largest picture 320x200; no RLE picture is wider than 255 pixels (§2.3).
* Type `'P'` is used for 70 pictures (bundles 0x1141..0x114a) as well as for palettes; the magic
  tells them apart.

### 2.2 Raw (format 0)

`width * height` bytes, row-major, one byte per pixel (a colour index). 14 + w * h = payload size
for all 904.

### 2.3 RLE (format 1)

Per row, codes until the row is complete:

| Code | Meaning |
|---|---|
| `00 n` | skip n pixels (they stay as they are: transparent) |
| `c` (1..255) | copy the next c bytes as pixels |

A row ends when a counter reaches zero: the blitter (1399:0465) keeps the remaining width in an
**8-bit** register (`MOV BH,BL` of the width, `SUB BL,CL` per code), so a row of width w ends
when the sum of its counts reaches w modulo 256. Widths of 256 or more would end rows early; no
picture has one, so `x == width` gives the same rows for every shipped picture [O]. The row
loop counts the height in DL, also 8 bits (heights up to 200 fit). The RLE path never looks at
the key: skipped pixels are the transparent ones.

### 2.4 Blitters [O]

| Routine | Use | Dispatch |
|---|---|---|
| 1399:0335 (x, y, picture) | opaque draw; marks (x, y, w, h) dirty first | DS:0204[format]: 0 → 1399:036d raw copy (no clipping), 1 → 1399:0465 RLE |
| 1399:02ee (x, y, picture, mode) | keyed draw; marks the clipped rectangle dirty after | DS:01fc[(format + mode) & 3]: 0 → 1399:0399 raw with colour key and clipping, 1 → 1399:0465 RLE, 2 → 1399:0399, 3 → 1399:04d1 RLE mirrored left-right |

* Mode 0 draws normally; **mode 2 mirrors RLE pictures** (1399:04d1 starts at x + w - 1 and
  stores right to left) and leaves raw pictures unmirrored. Modes 1 and 3 would send raw data
  to the RLE routine and RLE data to the raw one; callers pass actor flag words (1473:03df) or
  `flag & 1` (the DAM draw, §6.3), so whether 1 or 3 ever occurs is open [I].
* The keyed raw blit clips to the inclusive rectangle DS:01e4..01ea (x 0..319, y 0..199 at
  start) and skips pixels equal to the key's low byte; a fully clipped picture sets DS:01ec =
  -1 and is not marked dirty. The RLE blits do not clip.
* All blits call `Gfx_HideMouseOnce` 1399:0b22 first.

Compared with Treasure Mountain: TM Windows' PMAP is `u16 w, u16 h` + RUND RLE on a 512x384
canvas; TM DOS uses 4-bpp PAN/PAK. G&G's LPX is the same idea (8-bit chunky, simple RLE,
colour key) in a new encoding.

---

## 3. Palettes [O]

### 3.1 Palette resources (type P, not LPX)

```
u16 byte offset into the 768-byte DAC table (3 per colour)
u16 byte count
count bytes: 6-bit R, G, B
```

50 resources (26 entries, 24 bundle members). The ranges in the data:

| Colours | Offset / count | Resources | Used with |
|---|---|---|---|
| 0..255 | 0 / 768 | 0x1094, 0x1134, 0x11a0, 0x2022, 0x204b, 0x2085, 0x20b3, 0x211a | the eight puzzle screens (§4) |
| 64..255 | 192 / 576 | 0x1000, 0x1004, 0x100a, 0x102a, 0x1077 | title, story pages, sign-in, race results |
| 128..255 | 384 / 384 | 0x0245; members 0 of 0x0304..0x0306, 0x0336..0x0338 | aircraft races; `[P, G]` picture bundles |
| 128..191 | 384 / 192 | 0x0034, 0x0105..0x0109, 0x0281 | level palettes (level descriptions +8, `fmt_data.md` §7.1) |
| 192..255 | 576 / 192 | 0x000b/0, 0x00e0, 0x00e2, 0x00e4 | race backgrounds 0xdf/0xe1/0xe3 |
| 64..127 | 192 / 192 | 0x1031/94, 0x1032/116 | members of two large bundles |
| 32..63 | 96 / 96 | 0x002b, 0x00f6; the palette members of the level sprite bundles (0x00ed/5 ...) | workshop, races, the parts of a level |
| 16..63 | 48 / 144 | 0x1058/5 | |

Three of the full palettes (0x11a0, 0x20b3, 0x211a) are 782 bytes: 10 bytes follow the 768
colour bytes. No loader reads them (1473:02f0 and 1473:041c copy exactly `count` bytes), and
two of the three sets contain values above 63, so they are not colour data [O]; they look like
leftovers of the tool that wrote the files [I].

### 3.2 The shadow palette and the DAC

* DS:024e holds 768 bytes of 6-bit RGB. Its initial contents are **initialised data in
  `ssg.exe`** (the "base palette"): colours 0..15 are the 16 EGA colours, 16..31 a grey ramp
  from white down to nearly black, then colour ramps and, from 192, browns [O bytes].
  Resources only ever overwrite ranges of it: colours 0..15 change only with the eight full
  palettes of the puzzle screens, 16..31 also with 0x1058/5 (§3.1). The tools read the base
  from the user's `ssg.exe` (`lpx.base_palette`).
* 1473:000c (start-up) loads an all-black DAC and sets DS:024c = 1 ("faded out").
* `Pal_SetShadowRange(rgb, byteOffset, count)` 1473:02f0 copies a range into the shadow and,
  unless faded out (DS:024c), loads the whole shadow into the DAC. `1473:041c(id)` copies a palette resource
  into the shadow only. `1473:0399(picture, palette)` applies the palette (if > 0) with 02f0,
  then reads the picture.
* `Pal_SetRange` 1399:05d6 writes 128 colours per group, each group after waiting for the start
  of a vertical retrace (port 3DAh bit 3 off, then on), interrupts off around each group. Every
  caller passes 2 groups, so one palette load takes 2 retraces (about 28.5 ms at 70.09 Hz)
  [O, platform pass].
* Fades (1473:015c(n, from, to)): step = 63 / n + 1, each colour moves by |d| / step + 1 per
  load, step + 1 loads: fade out 1473:0265 (n 6, 12 loads, about 0.34 s), fade in 1473:02a0 (n 8,
  9 loads, about 0.26 s); 1473:0325 = fade out, present, fade in; 1473:074e / 0766 save and
  restore the shadow [O, platform pass]. The last load of each fade repeats the target, so 11 and
  8 loads change colours: **22 and 16 visible frames** at 2 frames per load, as measured
  (`code_runtime.md` §13.4, REFERENCE_CAPTURES §10.2).
* The DAC holds 6 bits per channel. The tools show a value v as `(v * 255 + 31) / 63`, the
  rounding DOSBox-X uses (`re/dos/docs/fmt_graphics.md` §2.2) [I for G&G until checked against a
  capture].

---

## 4. Which palette goes with which picture

The game never stores a palette in a picture; the code pairs them [O for each source]:

1. **Code pairs** `1473:0399(picture, palette)`: 0x1002 + 0x1000 (title, 2c11:043f; drawn again
   with 0x1004 by 2c11:07e5), 0x1009 + 0x100a (story, 2c11:0630), 0x1029 + 0x102a (sign-in,
   2c11:084c, 2caa:016e), 0x1076 + 0x1077 (results, 2fcb:0000), and one per puzzle screen:
   0x1093 + 0x1094 (33cd:1ba1), 0x1133 + 0x1134 (3797:040d), 0x119f + 0x11a0 (35bb:1c03),
   0x2021 + 0x2022 (3b2f:071c), 0x204a + 0x204b (3938:0e76), 0x2084 + 0x2085 (3a4a:0be5),
   0x20b2 + 0x20b3 (3083:250e), 0x2119 + 0x211a (32fe:0aa0). In every pair the palette is the
   picture's id + 1 (except the title's second palette 0x1004).
2. **Races**: background DS:399a[race] with palette id + 1, then 0xf6 stored with 1473:041c
   (3dd6:0762); aircraft races store 0x245 and 0xf6 (3e77:0a23).
3. **Workshop**: 0x2b stored (2dee:1a90) and the level description's palette (+8) stored
   (2dee:028f); the parts' sprites come from the level's bundle (+0xa).
4. **The launch puzzle**: its screen palette 0x1134 (3797:040d) is in effect while its DAM
   frames (bundles 0x1157..0x1162) play [I: same overlay, no other palette call seen].
5. **Bundles**: a bundle's own palette members are applied when it is loaded, in member order
   (15df:023b applies every `'P'` member it meets; 15df:00a1 / 01a5 check only member 0).

The tools (`lpx.palettes_for`) start from the base palette and apply: the palettes given with
`--pal`, otherwise the context palette (rules 1, 2, the level palette of rule 3, rule 4, or
else the palette at id + 1 if there is one [I as a general rule]), and then the palette members
of the picture's bundle (rule 5).

Not covered yet: room objects and actors outside the level bundles, and the colours 64..127 in
rooms. `extract_all.py` writes the palette it used for every picture into `index.tsv`.

---

## 5. Fonts: `fnt` 0x1A (resources 0x0009 and 0x000a) [O]

Loader 2b16:0000 (both loaded by 2b16:021c at start-up, far pointers DS:a040 / DS:a044); select
2b16:0256 (returns the previous font); draw char 2b16:0368; char width 2b16:03ea; line height
2b16:0418; 1-bpp glyph blit 1399:0541 in the text colour DS:1f6c.

| Offset | Size | Field |
|---|---|---|
| +0 | 4 | `'f' 'n' 't'` 0x1A |
| +4 | 2 | flags: bit 0 set = 1-bpp glyphs drawn by 1399:0541; clear = glyphs are LPX pictures drawn by 1399:02ee (not used) |
| +6 | 2 | number of glyph records, indexed by character code from 0 |
| +8 | 2 | height; the line height is height + 1 |
| +10 | 2 | spacing added to every glyph's width for the advance |
| +12 | | glyph records |

Glyph record: `u16 y offset, u16 width`; if width is not 0: `u16 size`, then `size` bytes:
`u8 full bytes per row, u8 bits in a last partial byte, u16 rows`, then rows x (full + (bits > 0))
bytes. **Bit 0 of each byte is the leftmost pixel.** Set bits are drawn in the text colour;
clear bits are left alone. A glyph is drawn at (x, y + y offset).

| Font | Records | Glyphs | Height | Spacing | Use |
|---|---|---|---|---|---|
| 0x0009 | 128 | 103 | 9 | 2 | the UI font: printable ASCII plus key-cap pictograms 0x11 Spacebar, 0x17 Ctrl, 0x18 Enter, 0x19 Left, 0x1A Right, 0x1B Down, 0x1C Up, 0x1D Ctrl+R, 0x1F Esc (all 10 rows high, 16..62 pixels wide) |
| 0x000a | 127 | 96 | 9 | 1 | the small font |

* The space (0x20) is never drawn and advances 3 pixels in both fonts (2b16:0368 / 03ea).
* A code without a glyph draws nothing and advances 0 in 2b16:0368, but 2b16:03ea still returns
  the spacing as its width, so measuring and drawing disagree by the spacing for such codes
  (0x0C and 0x12 occur 5 times in the texts) [O code; effect on centring I].
* Characters are indexed as signed bytes; codes of 0x80 and more do not occur.

TM Windows' FFNT and TM DOS's font are MSB-first bitmaps with width tables; the G&G format is
new, the API shape (load, select, width, height, draw) is TM Windows' (scoping §4).

---

## 6. DAM animation scripts (D 0x114b..0x1156) [O]

### 6.1 Format

```
+0  'D' 'A' 'M' 0x1A
+4  little-endian 16-bit words: opcode, operands, opcode, ...
```

Interpreter 3d39:058f (one tick of both channels): it reads an opcode word; 0x7FFE ends the
channel's tick; otherwise it subtracts 0x7FF8 and dispatches through CS:083a for 0..7, and any
other word is skipped as a one-word no-op.

| Opcode | Operands (words) | Effect |
|---|---|---|
| 7FF8 | index, arg (u32) | if the far function DS:ba1e[index] is set, call it (channel, arg) |
| 7FF9 | x, y, w, h | queue an update rectangle (x + channel x, y + channel y, w, h) in the list DS:b86e (8 bytes each, count DS:398a) |
| 7FFA | target, n | loop: counter = bit 15 of n; if it is -1, set it to n & 0x7FFF; decrement; at 0 set -1 and go on, else jump to **word** `target` of the script (counted from the start, header included) |
| 7FFB | a | operand skipped |
| 7FFC | a, b | operands skipped |
| 7FFD | dx, dy, frame, flags | queue a cel: picture = frame table DS:ba26[frame + channel frame base], at (channel x + dx, channel y + dy); flags bit 0 is passed to the blit as its mode (§2.4) |
| 7FFE | – | end of this channel's tick |
| 7FFF | – | end: call the channel's callback (if set), free the channel |

A pending stop request turns the next opcode into 7FFF; a paused channel (+4) is skipped.

### 6.2 The scripts in the data

12 scripts, 40..112 bytes, all linear: only 7FFD (62), 7FFE (50) and 7FFF (12) occur; every 7FFD
has flags 0; every script ends with 7FFF, 3 to 8 ticks long [O, `dam.py --check`]. The only user
is the launch puzzle (overlay 3797): `3797:100e(script, frames, ...)` loads script id + k and
frame bundle id + k with k = DS:b124 mod 3, for the pairs (0x114b, 0x1157), (0x114e, 0x115a),
(0x1151, 0x115d), (0x1154, 0x1160) (3797:129d..139e), turns the bundle's offset table into the
frame table DS:ba26, starts a channel with all other arguments 0 (3d39:03cb, so position and
frame base 0 [I from the zero arguments]) and steps it every **19** PIT ticks (`Timer_Set(7,
0x12)` followed by a strict `Timer_Expired` loop, 3797:10ba / 01c3) while 3d39:0468 says it is
still running; the queued cels are drawn by the puzzle's 6-tick redraw (3797:0103), not at each
DAM step. The 0x7FF8 callback table DS:ba1e is never written (`code_puzzles_a.md` §5.5). So script 0x114b + j animates the members of
bundle 0x1157 + j, drawn over the launch puzzle's palette 0x1134.

### 6.3 Channels and drawing

Two channels of 0x22 bytes at DS:ba66 [O offsets from 3d39:058f / 084a; names I]:

| Offset | Field |
|---|---|
| +0x00 | layer (compared by the draw call) |
| +0x02 | stop request |
| +0x04 | pause (channel skipped while non-zero) |
| +0x06 | flags: bit 1 = draw opaque (1399:0335) instead of keyed |
| +0x08, +0x0a | x, y |
| +0x0c | frame base |
| +0x0e, +0x10 | loop counters (initially -1) |
| +0x12 | user value |
| +0x16 | far callback (7FFF) |
| +0x1a | far cursor (0 = channel free) |
| +0x1e | far script base |

Queued cels are 16-byte records at DS:b87e (count DS:3988): layer, x, y, flags, far picture,
channel flags. `3d39:084a(layer)` draws the cels of a layer: opaque with 1399:0335 when the
channel flag bit 1 is set, else keyed with 1399:02ee and mode = cel flags & 1.

### 6.4 Compared with TM Windows' DAM

TM Windows (`re/docs/fmt_othr_tables.md` §4, `re/docs/code_1098-10d8.md` §8): byte opcodes
F9..FF in OTHR resources, 16 channels of 0x1d bytes, interpreter 10b8:0312.

| | TM Windows | G&G |
|---|---|---|
| Opcode | byte F9..FF; bytes below F9 are 1-byte no-ops | word 7FF8..7FFF; other words are 1-word no-ops |
| Cel (FD / 7FFD) | i16 dx, i16 dy, u8 frame, u8 mode (7 bytes) | 4 words; flags bit 0 = blit mode |
| Loop (FA / 7FFA) | u16 byte offset from the base, u8 c: bit 7 = counter, 7-bit count | word index from the base, u16: bit 15 = counter, 15-bit count; same -1 idle value and "count 0 loops forever" rule |
| Rect (F9 / 7FF9) | i16 dx, u8 dy, i16 w, u8 h; queued as w + 2, h + 2, y in 8 bits | 4 words, queued as given |
| FB / 7FFB | stop sound | operand skipped, no sound |
| FC / 7FFC | play sound 7000 + n | operands skipped, no sound |
| 7FF8 | – | new: indexed callback with a 32-bit argument |
| End of tick / end | FE / FF | 7FFE / 7FFF |
| Channels | 16 | 2 |
| Frames | BNDL frame table | frame table built from a G&G bundle |

The numbering, the channel record's shape and quirks (start returns 0 both for "no free
channel" and for channel 0; stop re-checks the owner) are the same code family; the verify pass
found 11 of the 15 G&G DAM functions without callers (scoping §4).

---

## 7. The final movie (D 0x3000..0x30c9) [O]

### 7.1 Playback

State 15 (2fcb:0640, after the 15th race and its results screen) loads music bank 0x30ca with
`1cdd:0024` and sets a music cue (tick 0x250 → 12e0:00ea), loads effect bank 0x108d with
`1cdd:012d`, then calls `Movie_Play(0x3000, 0x30ca, 18, DS:236c, effects)` = 1473:07d9:

```
read frame 'first'
for frame = first .. end - 1, counter = 0, 1, ...:
    Timer_Set(0, 18)                       PIT ticks per frame
    decode the loaded frame into A000:0000 (1399:0a85)
    if frame + 1 != end: Res_Read(frame + 1)
    if cue.frame == counter: run the cue, advance to the next cue
    Timer_Wait(0)
```

18 PIT ticks at 145.65 Hz = 0.1236 s = 8.09 frames per second when the decode and the disk read
fit in the frame; a slower machine runs the movie slower. There are 202 frames (the end id
0x30ca is exclusive; 0x30ca/0x30cb are the music bank pair, 0x30cc the integrity entry).

### 7.2 Cue table DS:236c

18 records of four words {type, frame, a, b}, at most one per frame (the player checks one cue
per frame):

| Type | Effect (1473:084c switch, table CS:08bf) |
|---|---|
| 0 | `Delay(a)` PIT ticks (152c:0072: busy wait that keeps polling input) |
| 1 | start effect a of the effect bank with parameter b (1cdd:03c9(bank + a, 0, b)) |
| 2 | stop effect a (1cdd:0409) |
| 5 | start effect a with the second argument 1 (1cdd:03c9(bank + a, 1, b)) |
| 3, 4, 6 | nothing |

The shipped table holds 4 pauses of 1460 ticks (10.0 s) at frames 15, 17, 19 and 116, and 14
sound starts at frames 25..134 [O, `movie.py --cues`].

### 7.3 Frame coding (1399:0a85)

Deluxe Paint Animation's "RunSkipDump" coding, applied to the 64 000-byte screen:

| Code | Meaning |
|---|---|
| `01..7F` | copy that many literal bytes |
| `00 n v` | n bytes of value v; **v = 1 skips n bytes instead** |
| `80 w` (u16 w) | w = 0: end of frame; w < 0x8000: skip w; 0x8000 + n (n < 0x4000): copy n literal bytes; 0xC000 + n: v = next byte, n bytes of v (v = 1 skips) |
| `81..FF` | skip code - 0x80 bytes |

Colour 1 as "skip" is G&G's own twist on the DPaint coding. All 202 frames end exactly at their
`80 00 00` terminator; 476 926 bytes of frame data after decompression [O, `movie.py --check`].

### 7.4 What the frames hold, and the palette

* Frame 0 rewrites rows 0..164 in full and equals the results picture 0x1076 in 47 860 of those
  52 800 bytes: the movie starts from the results screen. Frames 1..9 also fill rows 165..199
  (where the HUD was); later frames are deltas anywhere on the screen [O].
* The movie loads **no palette** (2fcb:0640 makes no palette call): it shows the shadow palette
  left by the results screen, which applied 0x1077 (colours 64..255) over colours 0..63 from
  before. The frames use only colours 0..31 and 64..255 (none in 32..63) [O]. So the movie's
  palette is the base colours 0..31 plus 0x1077 [O: round 2 saw no fade or palette load between
  the rank box and the movie, `r2_movie_race15.avi`, REFERENCE_CAPTURES §9.4].
  `movie.py` uses exactly that by default (`--pal` replaces 0x1077).

Nothing comparable exists in Treasure Mountain (TM Windows plays cut-scenes as DAM over PMAPs;
TM DOS draws them in code).

---

## 8. Decoder outputs

| Tool | Output (default under `extracted/previews/gg/`) |
|---|---|
| `lpx.py PATH` / `--palettes` / `--check` | one picture or a bundle sheet (RGBA PNG); palette swatches; a check of all pictures and palettes |
| `fnt.py` | `fonts/fnt_0009.png` (labelled grid), `.tsv` (metrics), `--text` sample |
| `dam.py [ID] [--render]` | listings; one PNG per tick with the paired frames |
| `movie.py` | `movie/movie_NNN.png`, `--sheet`, `--cues` |
| `extract_all.py` | all of the above for every resource, with `index.tsv` |

Checked by eye in this pass: the title town (0x1002), the sign-in screen (0x1029), a puzzle
screen (0x1093), the level-0 parts (bundle 0x00ed with level palette 0x34 and its own palette
member), the launch-puzzle frames (0x1157 with 0x1134), DAM 0x1154 tick by tick, font 9 with
its key caps, and movie frames 0..201.

## 9. Porting notes [I]

* Implement the RLE row counter in 8 bits only if bit-exactness for widths of 256 or more ever
  matters; for the shipped data `x == width` is the same.
* The mirror mode exists for RLE pictures only; reproduce the (fmt + mode) & 3 table rather than
  a "mirror" flag so that the unused combinations behave as in the original if a caller ever
  passes them.
* The base palette must come from the user's `ssg.exe` (DS:024e) or be rebuilt by the engine
  from the same bytes at start-up; it is game data and stays out of the repository.
* The movie writes to the screen directly, bypassing the dirty tiles and the cursor; its pacing
  includes the next frame's disk read and decompression.
* tmport's `anim/` DAM engine generalises to G&G with a word operand reader, 2 channels, the
  pause field and the 7FF8 callback (scoping §5).

## 10. Open questions

* Whether any caller passes blit mode 1 or 3 (§2.4), and which flag words the actor draw passes.
* Palettes for room objects and actors, and where colours 64..127 come from in rooms. (The
  puzzles do restore the room's palette: state 9 saves the shadow before the puzzle and restores
  it after, 38e2:0000, `code_puzzles_a.md` §1.2.)
* *Answered (round 2):* the movie loads no palette (§7.4); its frames come every 18.0 ticks and a
  `Delay(1460)` cue lasts 10.06 s (REFERENCE_CAPTURES §10.5).
* The 10 bytes after three of the full palettes (§3.1).
* *Answered:* the wipe patterns `W` 0x11d6..0x11dd (636 bytes each: period, sweep flags, 156 left
  and right start columns, per-step increments) are decoded in `code_runtime.md` §14.2.
* DOSBox-X's 6-bit to 8-bit rounding for VGA mode 13h (assumed equal to its MCGA rounding).
* Whether the DAM interpreter checks a "DOP" tag as TM Windows' does.
