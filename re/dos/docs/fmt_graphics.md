# DOS edition (1990, SST.EXE): picture formats, palettes, video modes and font

Status: scoping pass (graphics agent), corrected and completed by the wave-2 engine pass (dos_gfx
agent, §11: the engine code in `src/dos/gfx/` and `src/dos/text/` reproduces 13 reference screens
in all four looks and 14 UI captures with 0 differing pixels). Covers `*.PAN`, `*.PAK`, the text
font, the adapter palettes, the back-buffer primitives and how the screens are put together.
Decoders: `tools/dos/pan.py`,
`tools/dos/pak.py`, `tools/dos/dos_font.py`, `tools/dos/dos_gfx_common.py` (they read the user's
files at run time and contain no game data). Previews: `extracted/previews/dos/gfx/` (every
picture, block and frame) and `extracted/previews/dos/screens/` (2x composed screens).

Conventions:
* Addresses are Ghidra addresses in `re/dos/ghidra_export` (load image starts at segment `1000`).
  DGROUP is segment `4d94`, so `DS:xxxx` = `4d94:xxxx` = image offset `0x3d940 + xxxx`
  (file offset `0x3fb30 + xxxx` in `extracted/SST_unpacked.EXE`, whose MZ header is 0x21f0 bytes).
* **[obs]** = observed in bytes, disassembly or decoded output. **[inf]** = inference.
* x coordinates in the drawing code are in **bytes** of the 4bpp back buffer (1 byte = 2 pixels)
  unless stated as pixels.
* Ghidra tip: in `disassembly.asm` the operands of `MOV Sreg,r16` (opcode `8E`) are printed
  swapped. `8ec0  MOV AX,ES` is really `MOV ES,AX`. Many routines below only read correctly
  with that in mind.

## 1. Summary

| File | Kind | Content | Used for (file index in the table at DS:0476) |
|---|---|---|---|
| `OPENING.PAN` | tile picture | one 320x200 picture (40x25 tiles) | title screen (index 14), drawn full screen by `47c7:042d` |
| `MOUNTAIN.PAN` | tile blocks | 81 blocks of 128x128 | mountain level 1 scenery (index 0, scene 0) |
| `MOUNT2N3.PAN` | tile blocks | 76 blocks | level 2 scenery (index 1, scene 1) [inf: name says levels 2 and 3] |
| `MOUNT3.PAN` | tile blocks | 68 blocks | level 3 scenery (index 2, scene 2) |
| `CLOSE.PAN` | tile blocks | 5 blocks | castle room at the top: window, pedestal, slide, walls (index 3, scene 3) |
| `SHACK.PAN` | tile blocks | 7 blocks | shack rooms and treasure-chest view (index 4, scene 4) |
| `SPRITES.PAK` | sprite pack | 223 frames | everything on the mountain: treasures, keys, player, elves, net, status bar, cloud, Flutter (index 6) |
| `BEGIN.PAK` | sprite pack | 42 frames | title and sign-in: player walking up the title path, "Club Sign-In", series ads (index 15) |
| `CLOSE.PAK` | sprite pack | 133 frames | castle ending: treasures, chest, Master of Mischief, crown, magic effects (index 16) |
| `SHACK.PAK` | sprite pack | 126 frames | shack: treasures and prize animations, "CHAMPION" (index 17) |
| `SST.EXE` 1000:2447 | font | 128 glyphs, 10 rows, 1 bpp, proportional | all text (glyph blitter `1000:2180`) |

All pictures are stored in **4 bpp packed logical colours** (two pixels per byte, high nibble =
left pixel). The game composes a 320x200 back buffer in the same format and converts it to the
adapter when it presents a frame. The adapter-specific part is only the palette and the copy
to video memory (§2).

**Exactness [obs]:** every PAN decodes to the expected size, consuming exactly its packed bytes;
every PAK frame decodes to exactly `height` rows of `width` bytes and fills its 16-byte-aligned
slot; the font's glyphs are packed back to back. A full composed game screen (mountain
start, §8) matches a DOSBox EGA capture (`extracted/captures/dos/ega/08_shack.png`) with
**0 differing pixels out of 64000**, and the title picture matches `01_title.png` everywhere
except the pixels covered by sprites and text the game draws on top.

## 2. Video modes and palettes

### 2.1 Adapter detection and mode set [obs]

`1000:05bf` sets `DS:912C` (display type):

| `DS:912C` | Adapter | How detected | Mode set (`1000:0738` dispatch) |
|---|---|---|---|
| 1 | EGA, VGA | INT 10h AX=1A00h display code 4/5, or INT 10h AH=12h BL=10h answers | `1000:0836`: INT 10h mode **0Eh** (640x200x16), display page 1 |
| 2 | CGA (also EGA with 64 KB) | display code 2, or EGA with BL=0 from AH=12h | `1000:0921`: mode **04h** (320x200x4) |
| 3 | Tandy 1000 / PCjr | byte `DS:1300` == 21h (the Tandy ID byte) | `1000:0957`: mode **09h** (320x200x16) |
| 4 | Hercules | 3BAh status bit 7 toggles | falls into the CGA branch of `1000:0738` |
| 5 | MCGA | display code >= 0Bh | `1000:07bf`: mode **13h** (320x200x256) |
| -1 | none | | `1460:0003` prints the no-graphics-adapter message (DS:0693) |

VGA reports display code 8, falls through to the AH=12h test and runs as **EGA**.

The back buffer is segment `2005`, 160 bytes x 200 rows starting at `2005:0010`. The row table
at `CS:01C9` (200 words, `0x10 + y*0xA0`) is built by `1000:06e9`. Words `2005:0000..0009`
hold `{0, 0xA0, 0xC8, 0, 0}`, the same layout as a sprite header (§5.2).

Present path per adapter [obs, only partly decoded]: rows go to video memory through
unrolled code templates that are copied into place at start-up (`1000:0800`/`1000:08fb` copy
from `CS:09E0` or `CS:09D1` to `CS:09F5`). EGA: page at `A000:4000`, 80-byte rows (row table
`DS:2EEE` = `0x4000 + y*0x50`). Each game pixel becomes two screen pixels. `1000:0836` first
fills `A000:0000..0FFF` with every combination of 3 colours x 16 (write mode 2, bit masks
`0C`,`03`,`C0`), so that latch copies can produce four doubled pixels per byte. Tandy: four
interleaved banks of `0x2000`, 160-byte rows. MCGA: `y*0x140`. CGA: two banks of `0x2000`,
80-byte rows.

### 2.2 Logical colour -> displayed colour [obs]

The nibble written to the back buffer is a **logical colour 0..15**. Each adapter maps it
through a table in DGROUP:

| Adapter | Table | Applied by | Meaning of each entry |
|---|---|---|---|
| EGA/VGA | `DS:3C4A`, 16 bytes + overscan byte `00` | INT 10h AX=1002h at `1000:0852` | attribute palette register. Mode 0Eh is a 200-line mode, so the monitor (or the VGA's default DAC) reads it as **RGBI**: bit0 B, bit1 G, bit2 R, **bit4 = intensity**; bits 3 and 5 are ignored |
| Tandy | `DS:3D1B`, 16 bytes | `OUT 3DAh/3DEh` loop at `1000:097d` (palette regs 10h..1Fh) | IRGB nibble |
| MCGA | `DS:0144`, 16 x (r,g,b) 6-bit | `1460:0003` -> `1437:0001` copies it to `DS:38EA`, `1000:1144` loads it | DAC values. Each colour fills a block of 16 DAC entries (`16*i .. 16*i+15`), so a screen byte's **high nibble** selects the colour. DOSBox-X shows a 6-bit value v as **(v*255+31)/63** (rounded; `v*255/63` is one lower in some channels, e.g. 0x9D for 0x9E) [obs: 0 differing pixels on `mcga/01`, `mcga/04`] |
| MCGA (first) | `DS:3C1A`, 16 x (r,g,b) | `1000:07bf` at mode set | replaced by `DS:0144` immediately after |
| CGA | `DS:0174`, 16 bytes | `1437:0027` -> `1000:1087` builds XLAT tables `2005:7E00..81FF` | four 2-bit CGA pixels `a b c d` (from the most significant bits) per logical colour, shown on 4 consecutive pixels; the next row uses `b a d c` (§2.4). Mode 4, palette 1, high intensity: black, light cyan, light magenta, white |

**[obs]** The EGA and Tandy tables give the **same RGBI colour for every logical colour**, so
the 16-colour look is identical on both. Resulting colours (standard RGBI values, brown for 6):

| logical | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| RGBI | 1 | 9 | 6 | 12 | 4 | 5 | 13 | 3 | 2 | 10 | 11 | 14 | 15 | 7 | 8 | 0 |
| colour | blue | light blue | brown | light red | red | magenta | light magenta | cyan | green | light green | light cyan | yellow | white | light grey | dark grey | black |

The MCGA table is the same scheme with softer values (for example orange for 3 and a darker teal for 7).
`python -P tools/dos/dos_gfx_common.py SST.EXE` prints all five tables side by side.
`--palette ega|tandy|mcga|mcga_modeset|identity` selects the mapping in every decoder (default `ega`).

### 2.3 Fixed tables in the back-buffer segment [obs]

Initialised data in `SST.EXE` (segment `2005`):
* `2005:8600` (256 B): **sprite mask**. `mask[b]` has nibble `F` where the corresponding pixel of `b`
  is 0, else `0`. Masked drawing is `dst = (dst & mask[src]) | src`. **Logical colour 0 is the
  transparent colour** (blue, if drawn opaque).
* `2005:8900` (16 words): expands 4 font bits to 4 pixel nibbles (`F` per set bit, MSB = left),
  used by `1000:2180`.
* `2005:8700` (256 words): 2-bit expansion for the CGA glyph path. `2005:7E00..81FF`: CGA
  XLAT tables, built at run time (§2.4).
* The engine computes the mask and expansion tables; `tests/test_dos_text.cpp` checks them against
  the bytes at 2005:8600 and 2005:8900 of the user's SST.EXE.

### 2.4 CGA conversion (`Gfx_BuildCgaColorMap` 1000:1087) [obs]

In CGA mode the back buffer itself is 2 bpp (80 bytes per row, 4 pixels per byte) and every
drawing routine converts 4-bpp data when it writes. For an input byte `b` (left pixel `h = b >> 4`,
right pixel `l = b & 15`) and the table `T` at DS:0174, 1000:1087 builds:

| Table | Value | Used for |
|---|---|---|
| `2005:7E00[b]` | `(T[h] & 0xC0) \| (T[l] & 0x30)` | pixels 0, 1 of a CGA byte, even table |
| `2005:7F00[b]` | `(T[h] & 0x0C) \| (T[l] & 0x03)` | pixels 2, 3, even table |
| `2005:8000[b]` | `(T[h] & 0x30) << 2 \| (T[l] & 0xC0) >> 2` | pixels 0, 1, odd table |
| `2005:8100[b]` | `(T[h] & 0x03) << 2 \| (T[l] & 0x0C) >> 2` | pixels 2, 3, odd table |

So with `T[c]` = pixels `a b c d`, a logical colour at pixel x shows `a b c d`[x mod 4] with the
even tables and `b a d c`[x mod 4] with the odd ones. The CGA buffer keeps the 4-bpp row table
(`CS:01C9`, 0xA0 bytes per row) and uses the first 80 bytes of each row (CGA byte = 4-bpp byte x >> 1).
Per routine (wave 4, looks agent; the disassembly of each CGA branch):
* `Tile_DrawColumn` (1000:1a33): even tables for the first row of the column, then alternating
  (`XOR BH,0FEh`) — the play field and the title start on even rows, so even rows = `a b c d`. The
  alternation runs on across the tiles of the column.
* `Gfx_FillRect` (1000:1707): the fill byte through 7E00/7F00 on **every** row, so a dithered fill
  shows `a b c d`[x mod 4] on odd rows too. An odd first byte keeps the high half of its CGA byte
  (`AND 0F0h`), an odd last byte the low half: pixel for pixel the same rule.
* `Gfx_ReplaceColorRect` (1000:17d9): the compare value is pixel `a` of the "from" word's low byte
  through 7E00/7F00, the new value pixel `b` of the "to" word's low byte through **7F00 and 8000**
  (BH is still 7F when the second conversion starts); both are cut to their top two bits (`AND
  DX,0C0C0h`) and rotated by one pixel per pixel step, so every 2-bit pixel equal to `a(from)`
  becomes `b(to)`, whatever its position. A rectangle starting at an odd byte rotates them by two
  pixels (1000:181b) and enters the loop at its pixel-2 step, which rotates once more: the compare
  then looks one pixel ahead (a match needs a zero pixel and a zero `a`, and writes `b` to the next
  position), and the offset carries into the following rows (DX is not reloaded per row). The menu
  and sign-in highlights (`exchangeColors`: white → 3 → black ...) only meet solid colours, where
  the rule is the 4-bpp one; colours 3, 4 and 6 are all magenta in CGA (0xAA), so a magenta pixel
  inside a highlighted rectangle turns black there.
* `Font_DrawChar` (1000:2295): the colour word's low byte through 7E00/7F00 gives one CGA byte
  `a b c d` (pixels 0 and 2 from its high nibble's entry, 1 and 3 from its low nibble's), copied to
  both bytes of a word and rotated right by one pixel (`ROR` by 2 bits) after every glyph row: row r
  shows `a b c d`[(x − r) mod 4] at its set bits (expanded through the 2-bit table 2005:8700, 8
  pixels per glyph byte; an odd x shifts the expansion by two pixels with `ROR BL,2` and masks the
  halves). The rotation restarts with each glyph. A glyph past x byte 159 lands in the unseen half
  of the CGA row instead of on the next row.
* `Sprite_Draw` (1000:1e30, `Sprite_DrawRows` 1000:1f46): each clipped row is decoded to 4 bpp at
  2005:7D20 (mirrored rows with `ROR AL,4`), then converted by the pixel's position in its CGA byte,
  the even tables for the **first drawn row** (after clipping) and alternating per row (`XOR
  CS:[235E],0FE00h`) whatever the row's y parity: a sprite starting on an odd row (the status-bar
  panels at y 147, many actors) shows its dithered colours in the opposite phase from the tiles.
  Masked draws AND with the tables 4 pages higher (2005:8200..85FF, initialised data in SST.EXE:
  11 where the 4-bpp pixel is colour 0, for both phases). Flag 0x01 forces the 4-bpp byte path,
  which on a CGA buffer copies a save-under's CGA bytes back as they are.
* `Sprite_SaveUnder` (1000:20f8): whole CGA bytes, from byte x >> 1, ceil((width + (x & 1)) / 2)
  bytes per row (header x and width in CGA bytes; the restore's dirty rectangle doubles them back,
  1000:1e16).
* The scrolls (1000:18b3 ff.) move the same pixels (CGA bytes 1/3/5 for the 4-bpp bytes 2/6/10),
  the presents copy the CGA bytes of the dirty spans (1000:13b1) or the whole buffer (1000:158f).

Only colours with a dithered entry differ between the phases (V 1.0: 2 = 0x99, 5 = 0x88, 8 = 0x11,
9 = 0x77, 14 = 0x82; the others are solid). The engine (§11, wave 4) keeps the CGA picture next to
the 4-bpp buffer: `BackBuffer::enableCga` turns on a per-pixel plane that every primitive writes
with its routine's rule above (and the scrolls, presents and save-unders move); a pixel that no
CGA-aware routine set, or that a raw byte writer changed, shows the tile rule. The game's buffers get
it through `CgaBinding` (a `DosGame` member, look CGA only).

## 3. `.PAN` tile pictures

### 3.1 File layout [obs]

```
+0  u16 map_packed_size      bytes of block 1
+2  u16 width_tiles          tiles per picture row    (OPENING 40, others 16)
+4  u16 height_tiles         tiles per picture column (OPENING 25, others 16)
+6  u16 tiles_packed_size    bytes of block 2
+8  block 1 (map), then block 2 (tiles); 8 + size1 + size2 == file size for all six files
```

Loader `1460:05e8(file_index)`: reads the 4 header words, reads block 1 to the **end** of a
`0xA400`-byte buffer at segment `2897` (`DS:37B8`), reads block 2 to the end of a `0xE880`-byte
buffer at segment `32d7` (`DS:37B4`), then decompresses both in place toward offset 0
(`1000:2368` with BX=width, CX=height, and `1000:240e`). After loading, each buffer holds the
decoded data from offset 0 and, after it, the part of the packed block that was not overwritten
(the engine keeps both buffers exactly like that, so any read past the decoded map or tiles sees
the same bytes, except where the original would see leftovers of an earlier file).

### 3.2 Block 1: tile map, word RLE (`1000:2368`, inner routine `1000:23b9`) [obs]

The output is an array of 16-bit map entries. Entry `k` of a picture is the tile at
column `k / height`, row `k % height`, so the array is **column-major** (consecutive words go
down the screen). The encoder, however, works **along rows**: for each row `r` it writes
`width` entries with a stride of `height` words. Words are read from the input in order:

```
for picture p = 0, 1, ... until the input is used up:          (1000:23ab: SI == 0xA400)
    for row r in 0 .. height-1:
        di = p*width*height + r          (word index);  left = width
        while left > 0:
            w = next u16
            n = w >> 12 ; v = w & 0x0FFF
            if n != 0:   write v (n+1) times, di += height each, left -= n+1
            elif v == 0xFFF:
                 ref = next u16          (word index of an earlier row start)
                 copy 16 entries ref, ref+height, ... to di, di+height, ...
                 left = 0                (ends the row; the count 16 is hard-coded at 1000:23e9)
            else:        write v once, di += height, left -= 1
```

The back-reference therefore only works for 16-wide pictures. OPENING (40 wide) uses none [obs].
The next picture starts at the word after the last row's end minus one row stride (`1000:239d`:
`AX = DI_end - 2*height + 2`), i.e. at `p*width*height` when every row wrote `width` entries
(a row copy always writes 16). The outer loop tests for the end of the input only after a whole
picture; the input pointer must end exactly at 0xA400.

A map entry `v` is a **paragraph offset into the tile buffer**: the tile starts at byte `v*16`
(`1000:1a15`: `DS = tileseg + [BX]`). Tiles are 32 bytes, so all entries are even and tile number =
`v/2` [obs: true for every entry of every file].

### 3.3 Block 2: tiles, byte RLE with escape A5h (`1000:240e`) [obs]

```
b = next byte
b != A5h          -> output b
A5h A5h           -> output A5h
A5h n v (n != A5h)-> output v, n times
until the input pointer reaches 0xE880 (the end of the buffer)
```

A tile is 8x8 pixels, 4 bytes per row, packed 4bpp, high nibble left.

### 3.4 Results (`python -P tools/dos/pan.py --all TM-DOS OUT --tiles --layout`) [obs]

| File | map packed -> words | pictures | row back-refs | tiles packed -> bytes | tiles |
|---|---|---|---|---|---|
| OPENING | 1568 -> 1000 | 1 x 320x200 | 0 | 16845 -> 21536 | 673 |
| MOUNTAIN | 15216 -> 20736 | 81 x 128x128 | 404 | 49375 -> 59168 | 1849 |
| MOUNT2N3 | 12162 -> 19456 | 76 | 301 | 36132 -> 43776 | 1368 |
| MOUNT3 | 12844 -> 17408 | 68 | 284 | 38702 -> 44832 | 1401 |
| CLOSE | 1656 -> 1280 | 5 | 15 | 7959 -> 9344 | 292 |
| SHACK | 1648 -> 1792 | 7 | 9 | 8503 -> 10560 | 330 |

Checks done by the decoder: every packed byte is consumed exactly (no overrun or leftover).
The map has no holes. Every entry addresses a tile, and every tile is used. Decoded sizes fit the
buffers (largest: MOUNTAIN map 41472 of 41984 B, tiles 59168 of 59520 B). Simulating the
in-place decompression, no write overtakes the not-yet-read input.

The PAN tile pictures have no transparency. Block 0 of each mountain PAN, and MOUNTAIN block 54,
is a placeholder (plain background with a small "MT" mark) [obs]. Grid cells that hold 0 show it.

## 4. Mountain/scene layout: blocks, camera, viewport

### 4.1 Block grid `DS:0CAA` [obs]

The scenery of scenes 0..4 is a **16 x 16 grid of words** at `DS:0CAA` (index `row*16 + col`). The
low 7 bits of each word are the PAN block number (`AND AX,7Fh` at `130f:0422`), so a block is
`map segment + n*32 paragraphs` (512 bytes = 256 entries). The grid in `SST.EXE` is the initial
level-1 layout. At run time hiding places are written into it at the slots listed in
`DS:11AA` (48 words) and `DS:120A` (`47c7:0c62`, `1a54:...`), and `1a54:08dc` copies a 4x5
patch from `DS:1252`. Row 0 has no slots. With MOUNTAIN.PAN, the static grid shows the
clubhouse rooms in row 0 and the paths, ladders and tunnels below
(`extracted/previews/dos/screens/03_mountain_ds0caa_grid_overview.png`).
[inf] Levels 2 and 3 and scenes 3/4 use other parts of this grid or rewrite it. That was not traced;
see §9.

### 4.2 World and camera [obs]

World coordinates are pixels, and a block is 128x128. New game (`47c7:0b19`): player
`(DS:37E2, DS:37E4) = (0x140, 0x70)`, camera `(DS:37EA, DS:37EC) = (0xC0, 0)`. `130f:025a`
derives the block column `DS:9124 = camx >> 7`, the block row `DS:9126 = (camy >> 7)*16`, the
byte offset of the column inside the block `DS:920D = ((camx & 0x78) << 2)` (32 bytes per tile
column), and the row `DS:920F = (camy >> 2) & 0x1E` (2 bytes per tile row).

### 4.3 Viewport [obs]

`130f:0326` (full redraw) draws **39 tile columns x 16 tile rows**: x bytes `2..0x9A` step 4
(pixels 4..315) and rows `0x10..0x88` step 8 (pixels 16..143). It uses `1000:19cf`, which copies
one 8x8 tile (two `MOVSW` per row, stride 0xA0) per map word and walks down a map column.
Clip window for actors: x bytes `2..0x9D`, y `0x10..0x8F` (`4306:04e7`). Screen position of a
world point: `x_bytes = ((wx - camx) >> 1) + 2`, `y = wy - camy + 0x10`. Verified: the new-game
player frame lands exactly where the capture shows it (§8).

Scrolling [obs, not needed for the formats]: `1000:18b3`/`1000:196d` shift the viewport left/right
by 4 or 2 bytes (selected by `DS:3D31`). `1000:1a86`/`1000:1aef` shift it vertically.
`130f:04e5` then draws the uncovered column(s) or row with `1000:19cf`.

## 5. `.PAK` sprite packs

### 5.1 File layout [obs]

```
+0      u16 count
+2      u16 0, u16 62, u16 58      identical in all four files; not read by any code seen [obs]
+8      u16 para[count]            frame k begins at file offset para[k]*16
+8+2n   u16 FFFFh, u16 FFFFh       trailer; frame 0 starts at the next 16-byte boundary
```

The whole file is loaded at a paragraph-aligned buffer (`DS:37BC`, 0x1C908 bytes allocated by
`4c1f:020c` at `1460:0003` [inf: Turbo C `farmalloc`]). SPRITES.PAK is loaded for scenes 0..2. For scenes 3/4 the buffer is overwritten by
CLOSE.PAK/SHACK.PAK (file index `scene + 13`, `1437:0034`). BEGIN.PAK is loaded at start-up (`1460:02c6`).
`1000:2159(AX = 2*k)` sets `ES = pakseg + para[k]`, then returns `AX=[+0A] CX=[+2] DX=[+4] SI=[+6] DI=[+8]`.

### 5.2 Frame [obs]

```
+0   u16 0                 (always 0)
+2   u16 width in bytes    (pixels = 2*width)
+4   u16 height in rows
+6   i16 x offset, bytes   hotspot: added to the draw x (1000:1bc7)
+8   i16 y offset, rows    added to the draw y (1000:1bb2)
+A   u16                   returned in AX by 1000:2159; every call site seen overwrites AL with
                           the draw flags right away. Values 0,1,4,5,8,12 [meaning unknown]
+C   u16, +E u16           not read by the blitter [unknown; usually equal, e.g. 150,150]
+10  rows
```

Row encoding (read by `1000:1cbb`):
* length byte `L`. If `L & 0x80`: **raw row**, `L & 0x7F` bytes of packed pixels follow (always equal to the width;
  so a raw row is at most 127 bytes: frames wider than that, e.g. the 153-byte SPRITES 57, use run rows only).
* else `L` bytes of **(count, pixel-pair byte)** pairs follow, and the counts add up to the width.
* Rows clipped off the top are skipped with `SI += (L & 0x7F) + 1` (`1000:1c6d`).

### 5.3 Blitter `1000:1b8d` [obs]

Inputs: `ES` = frame segment (callers do `MOV DX,BX` after `2159`), `BX` = x bytes, `CX` = y,
`AL` = flags:

| flag | effect |
|---|---|
| `80h` | ignore the hotspot (draw at BX,CX); also skips the mirror placement |
| `20h` | mirror horizontally, and place with `x -= width + x_off` |
| `10h`/`20h` | draw each row reversed with nibbles swapped (`ROR AL,4`); `10h` alone keeps the normal placement `x + x_off` |
| `40h` | masked through `2005:8600`: colour 0 is transparent (`dst = (dst & mask[s]) \| s`, s after the nibble swap). Without it rows are copied opaque |
| `01h` | forces the normal path in CGA mode |

Clipping uses the window `DS:37DA..37E0` (x bytes min/max, y min/max, inclusive) [obs 1000:1be7]:
left: if `x < xmin` the first `xmin - x` bytes are skipped (for a reversed row: the last bytes of the
source); right: `x + width` (one past the end) is compared with `xmax` and `x + width - xmax - 1`
bytes are dropped; top and bottom likewise in rows; nothing left = no draw. The common tail
(1000:1911) adds the **clipped** rectangle as a dirty rectangle when DS:37AE bit 0 is set. The
window's DGROUP words are 0 in the file: it is first set by `Sprites_DrawSorted` and left at the
full screen (0..0x9F, 0..0xC7) after it, which is what every HUD/panel draw sees.

`Sprite_SaveUnder` 1000:2056 (SI x, DI y, CX width, DX height, AX = 1: restart the heap at DS:37CC,
segment 1AE7): clips like the blitter, stores an opaque frame (header `{0, w, h, x, y}`, rows with
`L = w | 0x80`) at DS:37CA:37C8 and returns the new heap end, 0 when clipped away. It is restored
with `Sprite_Draw(AL = 1, 0, 0)`: the frame's hotspot is its own position. The actor renderer
`4306:0497` draws actors from the table at `DS:9145` (14-byte entries: x bytes, y, depth,
frame code with bit 0 = mirror, parent link). It uses flags `40h` or `60h`, saves the
background under each sprite first (`1000:2056`), and opens the clip window to the full screen
for frame codes < 6 (the pointer frames 0..2).

**Two kinds of position [obs]:** status-bar, cloud and title frames are drawn with `BX=CX=0`,
so their header offsets are **absolute screen positions** (e.g. SPRITES 58 at (2,147), BEGIN
3..30 at x 248..280 px). Actor frames have small negative offsets: a hotspot at the feet.

### 5.4 Results (`python -P tools/dos/pak.py --all TM-DOS OUT`) [obs]

| File | frames | raw rows / run rows | notes |
|---|---|---|---|
| SPRITES | 223 | 4436 / 2464 | |
| BEGIN | 42 | 1058 / 360 | frames 0..2 = SPRITES 0..2 |
| CLOSE | 133 | 2485 / 1653 | frames 0..50 identical to SPRITES 0..50 |
| SHACK | 126 | 2246 / 846 | frames 0..50 identical to SPRITES 0..50 |

Every frame: all rows exactly `width` bytes, and the used size rounded up to 16 equals the slot.

Content, as seen in the contact sheets (`*_PAK_sheet.png`). Frame numbers marked * are confirmed
by code, the others by eye:
* SPRITES: 0..2 pointer frames; 3..50 treasures; 51..53 keys, 54..56 key outlines (*54 = key slot
  for `[9120]=8`); *57 message scroll (306x125 at (8,19)); *58 "Clue Words" box, *59 middle panel,
  *60 Coins/Net/Treasure box; 61 "move box / select" prompt; *62 "Press ↓ to drop coin", *63 "Press
  Enter to go on"; *64..66 coin bag full/half/low; *67..69 net states; *70 treasure icon; *71 hint cloud; *72
  Flutter the butterfly; 73..102, 134..140, 167..173, 211..216 player; 94..124, 148..166, 221..222
  elves; 125..133, 203..206 coins and sparkles; 141..147 net; 174..179 POOF/ZIP effects; 182..189
  dust and slide; *192..195 treasure markers (not found / found on level 1/2/3); 196..201 small
  digits; 207..210 splash; 217..218 doors; 219..220 grabber arm.
* BEGIN: *3..30 player walking up the title path (absolute positions); 31 "Club Sign-In";
  32..35 Super Solvers series pictures (opaque); 36..41 sparkles.
* CLOSE: 51..61 player with magic horn; 67..71 treasure chest opening; 72..91 magic sparkle;
  98..101, 118..120 Master of Mischief; 102 crown; 112..132 player celebrating.
* SHACK: 51..116 animated treasure/prize variants; 117..123 player; 124 "CHAMPION".

## 6. Font

### 6.1 Layout [obs]

The only font. It sits in the first code segment at `1000:2447`, found by the decoder from the
`MOV SI,2447h` at `1000:219a`. Data runs from `1000:25CA` to `1000:2E3A`.

```
+0      u8  7            never read by 1000:2180 [unknown]
+1      u8  last code    127 -> 128 glyphs
+2      u8  height       10
+3      u8  width[128]   advance = drawn width, pixels
+3+n    u8  off_lo[128]
+3+2n   u8  off_hi[128]  glyph offset from the data start (+3+3n)
data    height rows of bpr bytes, 1 bpp, MSB = leftmost; bpr = max(1, ceil(width/8))
        (word table DS:3C5B indexed by width: 1 for 0..8, 2 for 9..16, ...)
```

Codes 32..126 are ASCII (mostly 8 px wide; space 7). Codes 1..30 are wide pictogram glyphs:
key caps (F1, F2, F3, Esc, Enter, Space bar, arrow keys), the crown, and the pre-drawn menu-bar
labels in normal and inverted form (14/15 "Help"/"F1", 18/19 "File"/"F2", 22/23
"Options"/"F3"; 16/17, 20/21, 24/25 the highlighted versions).

### 6.2 Drawing [obs]

`1000:2180(AX=colour word, BX=x bytes, CX=y, DX=code)` expands each glyph byte through
`2005:8900` and merges `(dst & ~mask) | (colour & mask)`. Set bits take the colour and clear bits
keep the background. The colour word is four equal nibbles (`0xCCCC` = white,
`0xFFFF` = black). It returns `width/2`, the advance in bytes, so text is placed on 2-pixel steps.
`AX=1` only measures. Details [obs 1000:2203, verified]: each glyph byte becomes two words through
`2005:8900` (index = 4 font bits, `F` nibble per set bit, MSB = leftmost); each word is written as
`(dst & ~mask) | (colour & mask)`, so pixel 0/1 of a pair take the low byte's high/low nibble of the
colour word and pixels 2/3 the high byte's (only visible with `|NN` colours whose nibbles differ).
**Whole bytes** are written (`bytes per row` from DS:3C5B), so set bits past the glyph's width would
be drawn, and the addressing is linear: a glyph that runs past byte 159 continues at the start of
the next row. The dirty rectangle is `(x, y, width >> 1, height)`.

Text layout `43cb:0b63(x bytes, y, far string)` (full rules: `code_ui.md` §3.1, verified):
* `CR`/`LF`: x = margin, `y += [DS:5B3C]` (12, or 10 on the title and help pages)
* `~XXXYYY`: absolute position, x = margin = XXX/2 bytes (signed division), y = YYY pixels
* `` ` ``: toggles red (0x4444) and back to the ink DS:9215
* `|NN`: colour word = byte `(n << 4) | n` (8-bit) in both bytes, n = 10*d1 + d2
* `TAB d d`: advance 10*d1 + d2 bytes
* digits are `(signed char) byte - '0'`, unchecked; glyph code = byte & 0x7F
* clipped at x >= 0x9A (signed): such glyphs are skipped without advancing
(The scoping text said "`|` skips 2 characters, and the back-quote character is ignored": that was
the decompiler's view, which hides the colour register; corrected.)

## 7. Which picture is used where

* **Title** (`47c7:0007` -> `1460:05e8(14)`, `1460:02c6`, `47c7:042d`): OPENING.PAN full screen.
  Then BEGIN.PAK frames, e.g. frame 3 at its absolute position, plus "Press Enter to go on" text and sparkles.
* **Scene loads** (`1437:0034`): scene `DS:8FCA` 0..4 loads PAN index 0..4. Scenes 0..2 (the
  mountain levels) re-load SPRITES.PAK when coming back from scene 3/4. Scenes 3/4 load
  CLOSE.PAK/SHACK.PAK.
* **Disks** [obs, byte +0Dh of each 22-byte entry of the file table at DS:0476]: disk 1 =
  OPENING, BEGIN, CLOSE.*, SHACK.*, HELP.DAT, PRG_DICT, USERS.DAT, SFX/TUNES; disk 2 = the three
  mountain PANs, SPRITES.PAK, ALL_PUZS. The loader `1460:06c8` uses that byte to pick the drive
  path and prompt for the disk.

## 8. Composition of the game screen (verified)

Drawn by `130f:07a5` when `DS:922E` != 0 (all fills via `1000:169b`, x in bytes inclusive;
order and HUD rules in `code_ui.md` §11 and `code_runtime.md` §3.8):
1. Frame fills `130f:07bc..08a8`: rows 0..10 colour 15, rows 11..14 colour 0, a black rounded
   2-pixel border around the viewport (fills with bytes `FF`, `0F`, `F0` at x bytes 1..3 and 0x9C..0x9E,
   rows 0x0E..0x91), the rest colour 0 down to row 199.
2. Menu bar `43cb:0f1d(i,0)` for i = 0..2: x ranges from `DS:5BC7`/`DS:5BCD` (bytes 0..25, 26..51,
   52..84). Black box, light-grey (13) shadow, white (12) face, then glyphs `DS:5BD3[i]` at x0 and
   `DS:5BDF[i]` at x0+18 in black.
3. Viewport (§4.3).
4. Status bar, all opaque, frames at their own positions: 60; coin bag 64/65/66 for coins >10 / >4 /
   else; net 67/68/69 for net >3 / >0 / else; 70; treasure markers `192` (or 193..195 when found)
   at x bytes 0x8D/0x90 alternating, y 0xA4+6i, for `rank+2` slots; numbers in colour 12 at
   x bytes 0x6E/0x7D/0x8F, y 0xBA (`130f:0d00`, leading zeros skipped, 2 bytes each); 58; key
   frame `50 + [9120]/2` at (6,0xBE) if the key state is not 0; the clue words (colour 0xCCCC,
   x 0x0C, y 0xA0/0xAD/0xBA); 59; prompt frame `60 + [90F8]/2` if `[90F8]` != 0 (`130f:0c2e`).
5. Actors (`4306:0497`), e.g. the player, SPRITES 82, at (66, 128) on a new game.

Title page (`4ba4:0045` + the title loop `4ba4:0434`) [obs, 0 differing pixels in all four looks]:
OPENING.PAN by 40 `Tile_DrawColumn` calls, `Text_Draw(0x11, 0xA0, DS:8A48)` in ink 0 with line
height 10, then `Sprites_DrawSorted(0, 3)` in title mode (full-screen clip) over BEGIN.PAK: slot 1
the figure (script 0x5B: frame code 6 = BEGIN 3, at 0,0), slot 2 a sparkle (script 0x5C: BEGIN
36..40 at one of the 12 spots DS:8A18/DS:8A30), slot 0 the mouse pointer (script 1: BEGIN 2 at the
mouse position). All z are 0, so the order is slot 2, 1, 0: the sparkle is drawn **under** the
figure and the pointer last. In the captures the pointer sits where the mouse driver left it
(EGA/Tandy at x byte 159, y 101; MCGA 158, 82; CGA 159, 146) and the sparkles are spot 0 frame 40
(EGA), spot 0 frame 38 (Tandy), spot 10 frame 40 (MCGA), spot 5 frame 37 (CGA).

Hint bubble (`49a6:0565`): SPRITES 71 and 72 at their own positions with flag 40h, the text
in colour 15 with `~XXXYYY` placement, and prompt frame 63.

Verification: `extracted/scratch/dos_graphics/compose_screens.py` rebuilds the new-game
screen from the user's files with these rules. It is **identical to the EGA capture
`captures/dos/ega/08_shack.png` (0 of 64000 pixels differ)**. The status values (coins 5, net 10)
come from that capture. The hint screen matches `09_hint_controls.png` except for the hint
text, which is a different hint in the capture. Placing BEGIN frame 3 at origin (0,0) on
OPENING reproduces the kid in `01_title.png`.

## 9. Open questions

* PAK header words `62, 58` and frame fields `+0A/+0C/+0E`: purpose unknown (not used for drawing).
* Font byte 0 (7): unknown.
* How scenes 3 (CLOSE) and 4 (SHACK) and levels 2/3 lay out their blocks: answered by the wave-1
  world spec (`code_world.md` §3.3: all scenes use the one 16x16 grid DS:0CAA with the scene's PAN).
* The CGA dither tables are decoded (§2.4); the EGA latch present routine is described only in
  outline (not needed: the engine presents complete RGB frames).
* Hercules (`912C`=4) takes the CGA branch of `1000:0738` [inf: probably unusable].

## 10. Outputs

* `extracted/previews/dos/gfx/`: `<NAME>_PAN_<nnn>.png` (each picture/block),
  `<NAME>_PAN_sheet.png`, `<NAME>_PAN_tiles.png`, `MOUNTAIN_PAN_layout_ds0caa.png`,
  `<NAME>_PAK_<nnn>.png` (each frame, colour 0 transparent), `<NAME>_PAK_sheet.png`,
  `<NAME>_PAK_frames.tsv` (header fields), `dos_font_sheet.png`, `dos_font_sample.png`,
  `dos_font_metrics.tsv`.
* `extracted/previews/dos/screens/` (2x): `01_title_opening`, `01b_..._with_begin_frame3`,
  `02_mountain_level1_start` (pixel-exact vs capture), `02b_..._hint`,
  `03_mountain_ds0caa_grid_overview`, `04*_shack_*`, `05*_close_*`.

## 11. Engine implementation and verification (wave 2)

Namespace `tmport::dos`; addresses of the ported routines are in the code comments.

| File | Contents |
|---|---|
| `src/dos/gfx/pan.h` | `PanFile::load` (1460:05e8, 1000:2368/23b9 map, 1000:240e tiles, in place, both buffers kept as the game holds them), `mapWord`, `tile`, `picturePixels` |
| `src/dos/gfx/pak.h` | `PakFile::load` (offset table, raw/run rows, header fields), `PakFrame` (decoded packed rows) |
| `src/dos/gfx/palettes.h` | `Look`, `lookPalette` (EGA DS:3C4A RGBI, Tandy DS:3D1B, MCGA DS:0144 rounded, CGA DS:0174), `cgaPixel`, `toRgba` / `toRgba32` (320x200, bytes R,G,B,A = `SDL_PIXELFORMAT_RGBA32`, the convention of `gfx::Presenter::presentFrame`) |
| `src/dos/gfx/back_buffer.h` | `BackBuffer` (160x200 packed bytes = segment 2005), clip window DS:37DA, DS:37AE flag, `DirtyRegions` (1000:1181), `fillRect` 1000:169b, `replaceColor` 1000:176c, the four scrolls 1000:18b3/196d/1a86/1aef, `drawTileColumn` 1000:19cf, `presentDirty` / `presentFull` |
| `src/dos/gfx/blitter.h` | `drawSprite` 1000:1b8d, `saveUnder` 1000:2056, `SpriteRenderer::drawSorted` 4306:0497 / `eraseAll` 4306:06dd with the save-heap bookkeeping of DS:929F |
| `src/dos/gfx/scenery.h` | `SceneryCamera::at` (Frame_Begin 130f:025a), `drawPlayfield` (130f:0326), `scrollColumns` / `scrollRows` (130f:04e5), `drawFullScreenPicture` (47c7:042d) |
| `src/dos/text/dos_font.h` | `DosFont::fromExe` (1000:2447), `drawChar` 1000:2180 |
| `src/dos/text/text_draw.h` | `textDraw` 43cb:0b63, `textWidth` 43cb:0cfc, `drawNumber` 130f:0d00 |
| `src/dos/text/ui_draw.h` | `hline`/`vline`/`frameRect` (43cb:0ab9/0ada/0b18), `exchangeColors`, `msgBoxGeometry` / `drawMsgBox` (43cb:0592), `drawBoxFrame` 43cb:0848, `drawButton` 43cb:0d50, `drawMenuTitle` 43cb:0f1d, `highlightMenuItem` 43cb:04ff |
| `src/dos/gfx/dos_screens.h`, `dos_gfx_cli.h` | the reference composer and `tmport --dos-pan / --dos-pak / --dos-screen` |

Deviations: the CGA picture is the 4-bpp buffer plus the CGA plane of §2.4 (the original has
only the 2-bpp buffer: what the plane holds equals it as long as every write goes through a
CGA-aware primitive or `BackBuffer::noteDrawn`); the clip window starts at the full screen instead of 0,0,0,0; out-of-buffer
writes are clipped to the buffer (valid calls are unaffected); a frame index past the loaded
pack draws nothing; the dirty-span lists are kept as a per-row-pair bitmap (the union the original
merges); reads past the PAN buffers return 0 / a zero tile.

Verification (`extracted/scratch/dos_w2_dos_gfx/`: `render_all.sh` renders with
`tmport-cli --dos-screen`, `compare_screens.py` and `ui_compare.py` compare by RGB, EGA captures
reduced from 640x400 after checking the 2x2 doubling):

| Screen | Captures | Differing pixels |
|---|---|---|
| title | `ega/001`, `tandy/01`, `mcga/01`, `cga/01` | 0 each (scoping: 479 / 36 on the EGA title: sparkle and pointer) |
| sign-in | `ega/004` (shipped USERS.DAT), `tandy/06`, `mcga/04`, `cga/05` (their USERS.DAT snapshots) | 0 each |
| clubhouse (new game) | `ega/008`, `tandy/21` | 0 each |
| elf question (puzzle 61) | `ega/030` | 0 |
| level-1 path at the trunk gate | `ega/042` | 0 (scoping 2242: player, clue word and the cell picture) |
| castle ladder shaft | `ega/099` | 0 (scoping 1427: player and clue words) |
| drop-downs Help/File/Options, File item 3 | `ega/010`, `021`, `022`, `070a`, `tandy/10`, `tandy/16` | 0 each |
| Yes/No boxes (Music, Exit and Save, Start New Player, saved game) | `ega/023`, `ega/070b`, `tandy/13`, `tandy/17`, `tandy/07` | 0 each |
| name entry (empty, "Ada", "Ben" with the cursor off) | `ega/005`, `ega/006`, `tandy/19` | 0 each |

The level-1 path capture was taken before the trunk was unlocked: the saved game used for the grid
has cell 0xEF = block 0x4C (the ladder), the capture shows block 0x40 (locked trunk). With
`picture[0xEF] = 0x40` the scenery matches; it is not an overlay painted by the cell handler
(as SCOPING.md §3/§8 guessed). The decoded PAN maps and tiles, PAK frames and the font hash equal
the Python decoders' (`tests/test_dos_gfx.cpp`, `tests/test_dos_text.cpp`).

### 11.1 Wave 4: the looks through the real game (looks agent)

Every `tandy/`, `mcga/` and `cga/` capture is reproduced through the game, not the composer
(`tools/dos/scenarios/looks/`, fidelity runner group `looks`): boot runs with the roster of the
capture run (Tandy: `USERS_after_exit_level2_Ada.DAT`; MCGA and CGA: `USERS_after_tandy_newplayer_Ben.DAT`),
the pointer hidden after 600 idle ticks where the capture has none, and the Tandy states
`savestates/tandy/*` with key ticks found by search (Ben's layout from `srand(0x27ED)`, the time()
of the guest clock when USERS.DAT was written). Result: **0 differing pixels in all 22 Tandy, 5
MCGA and 6 CGA captures**; the only masks are the 12 title-sparkle spots on the title pages (the
original tries a sparkle in every loop frame, ~1690/s on the reference machine, so its rand() count
cannot be shared). Round 3 (REFERENCE_CAPTURES.md) adds 24 CGA and 24 MCGA UI frames (sign-in boxes,
name box, saved-game prompt, Flutter's hint, the three menus with moved highlights, help pages,
Yes/No boxes with either button): 0 pixels each (one elf masked in the last frame of each set).
Before the CGA plane the CGA frames differed by 2273 pixels in Round 3 (hint cloud 690, About page 1
997, every frame with the status bar 51: sprites drawn from odd rows) and by 269 / 51 in
`cga/04_enter_walkoff` / `cga/06_level2`. The MCGA palette (`(v*255+31)/63`) needed no change.
