# PMAP pixmaps, palettes and sprite transparency (Windows CD edition)

Scope: the `PMAP` image resources in `TMT16.DLL` and `TMT256.DLL` (764 each), the `RRGB`
palette in `TMT.DLL`, and how `TMTWINCD.EXE` loads, recolours and draws them.

Evidence used: Ghidra export `re\win\ghidra_export\` (functions cited as `SSSS:OOOO`), raw
resources `extracted\res\<DLL>\<type>\<id>`, DGROUP (`11b8`) bytes read from
`TMTWINCD.EXE` at file offset `0x23c00 + off`, an NE relocation scan of the EXE (scratch
script `extracted\scratch\pmap\nereloc.py`), and decoding all 1528 images with
`tools\assets\pmap.py`.

Labels used below: **[obs]** means read directly from bytes, decompiled code or decoded
output. **[inf]** means inferred.

---

## 1. Summary

* Header [obs]: `u16 width, u16 height`, little-endian. That is all. The header has no
  hotspot, flags, palette or transparency field.
* `TMT16.DLL` [obs]: uncompressed 4 bpp. Each row takes `(w+1)/2` bytes and the left pixel
  is the **high** nibble. All 764 images use this layout.
* `TMT256.DLL` [obs]: 8 bpp, one byte per pixel, no row padding. 760 images are
  compressed with **`RUND`**, a PackBits-like RLE whose runs cross row boundaries. 4 images
  (ids 1380–1383) are stored raw. `RUND` is the only compression tag the EXE checks for.
* 256-colour palette [obs]: one palette for the whole game, `TMT.DLL` RRGB id **900**: 256 ×
  `R,G,B` bytes (768 bytes, NE-padded to 1024). It is loaded once at startup. The EXE has
  no palette animation, colour cycling or fades.
* 16-colour palette: the PMAP nibble is written to the 4 VGA bit planes unchanged, so the
  colours are the Windows VGA device colours [obs for the code, inf for the exact RGB]:
  `0 000000, 1 800000, 2 008000, 3 808000, 4 000080, 5 800080, 6 008080, 7 808080, 8 C0C0C0,
  9 FF0000, 10 00FF00, 11 FFFF00, 12 0000FF, 13 FF00FF, 14 00FFFF, 15 FFFFFF`.
* Transparency is a colour key; there are no mask images [obs].
  * 16-colour key: nibble value **3**.
  * 256-colour key: whichever palette index is nearest to **RGB(0,0,0xAA)**. That is
    logical index **15** (also 241, which has the same colour but is never used in any
    image).
  * A secondary "white key" exists. It is used only by the mirrored path of `1058:0cf3`:
    nibble 15, or RRGB entries equal to `FFFFFF` (indices 0 and 77). The `FEFEFE` entries
    stay opaque.

---

## 2. Where the data lives and how it is loaded

| Item | Value | Evidence |
|---|---|---|
| Resource type | NE custom type 32513, named `PMAP` through the type-15 name table | CONTEXT.md, `extracted\res\TMT*.DLL\15` |
| Id range | 1000–3999 is treated as PMAP | `1038:038a` maps id ranges to type-name strings: <900 BNDL (DGROUP `0x741`), 900–949 RRGB (`0x746`), 950–999 FFNT (`0x74b`), 1000–3999 PMAP (`0x750`), 4000–9999 SSND (`0x755`), ≥10000 OTHR (`0x75a`) |
| Which DLL | `TMT256.DLL` in WinG mode, or in GDI mode when the display is palette-based; otherwise `TMT16.DLL` | `1018:0563`: in GDI mode (`DAT_0074 == 0`) `DAT_086c = GetDeviceCaps(hdc, RASTERCAPS=0x26) & RC_PALETTE(0x100)` (`1018:05fe`, the only write to `0x86c`). In WinG mode that branch is skipped and `DAT_086c` keeps its DGROUP initial value `1` [obs: bytes `01 00` at `0x86c`], so WinG always means 256-colour. If it is nonzero, `LoadLibrary("TMT256.DLL")` (DGROUP `0x5eb`), else `"TMT16.DLL"` (`0x5f6`). The handle goes to `DAT_0872`. [inf] A non-palette (15/16/24-bit) display in GDI mode therefore gets the 16-colour art. |
| Loaders | `1038:03da` (by type+id) and `1038:0ba8` (by id) | Both call `FindResource/LoadResource/LockResource` on `DAT_0872` when the type name starts with `'P'`. |
| 256-colour fix-up at load | `1058:1d28` | Called only when `DAT_086c != 0`. It translates pixel indices in place, in the locked resource memory (§5.3). `1038:03da` then copies the bytes into a private global block (`1030:0000` alloc + `1050:0e10` memcpy) and frees the resource. `1038:0ba8` makes no copy: it keeps the locked resource, storing the far pointer in DGROUP `0x3d8c` and the resource handle in `0x073a`. |

The same ids exist in both DLLs with identical width and height [obs: all 764 ids match].
The only depth-dependent difference is the pixel encoding.

Runtime-generated pixmaps use the same in-memory format. `1058:1643` grabs a screen
rectangle into a buffer laid out as `{u16 w, u16 h, raw pixels}`. In 8 bpp this is a raw
PMAP. In 4 bpp, x and w are first rounded to even values. Code paths
`1050:0c6e` / `1050:0c0b`. [obs]

---

## 3. PMAP header

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0 | 2 | u16 LE | width in pixels (`1058:0bbc`/`0bed` return `*(u16*)p`) |
| 2 | 2 | u16 LE | height in pixels (`1058:0bd4`/`0c05` return `*(u16*)(p+2)`) |
| 4 | … | | 16-colour: packed nibbles. 256-colour: either `"RUND"` + RLE stream, or raw bytes. |

[obs] The EXE reads these words as signed 16-bit (`MOVSX` in `1058:0000`). The largest
value is 512, so the sign never matters. Observed ranges are 10×2 (id 1600) up to 512×364.
337 images have odd widths.

The header does not carry an origin or hotspot. Callers pass the **top-left** (x, y) in
the 512-wide frame buffer (e.g. `10e8:0040` draws at `(5, 0x123)`; `10f0:0038` and others
draw full-screen backgrounds at `(0, 0x14)`). Any per-frame offsets live in the callers'
tables or in OTHR data, not in the PMAP (§6.1, Open questions).

---

## 4. Pixel layouts

### 4.1 16-colour (`TMT16.DLL`): packed 4 bpp

* `stride = (width + 1) >> 1` bytes per row, rows top to bottom, no further padding
  [obs: `1050:0120` uses `(w+1)>>1` as the source stride; all 764 files have size
  `4 + stride*h` rounded up to 512].
* Within a byte, bits 7..4 hold the even (left) pixel and bits 3..0 hold the odd (right)
  pixel [obs: `1050:0029`, `1050:0120`; in an even/even copy the source high nibble goes
  to the destination high nibble].
* For odd widths the unused low nibble of the last byte is always 0 [obs, 27288 rows].
* Pixel value = VGA colour index 0..15. Value 3 is the transparent key (§6).

### 4.2 256-colour (`TMT256.DLL`)

`1058:0566` checks for compression: `memcmp(p + 4, "RUND", 4) == 0`, using the string at
DGROUP `0x997`. That is the only tag test in the EXE [obs: every PMAP consumer branches on
this one check: `0d43`, `0f7e`, `114c`, `13be`, `174b`, `0c78`, `1d28`].

* **Raw**: `w*h` bytes immediately after the 4-byte header, `stride = w` (no padding)
  [obs: `1050:00c9` copies `w` bytes per row; in ids 1380–1383 the data takes
  `4 + w*h` bytes, followed only by NE padding].
* **RUND**: bytes 4..7 are `52 55 4E 44` and the RLE stream starts at byte 8. It decodes to
  exactly `w*h` bytes, row-major, `stride = w`.

#### 4.2.1 RUND decoder (`1058:0400`; direct-to-screen variant `1050:03e7`; `1058:1c1b` walks the same stream to translate colours)

```
out = []                      # decoded pixel bytes
p   = 8
while len(out) < w*h:
    c = data[p]
    n = c & 0x7F
    if c & 0x80:              # run
        out += [data[p+1]] * n
        p   += 2
    else:                     # literal
        out += data[p+1 : p+1+n]
        p   += 1 + n
```

* A run or literal may span any number of rows. Rows are just consecutive `w`-byte slices
  of `out` [obs: e.g. id 1100 (60 wide) starts with runs of 46+6+53 pixels].
* Count 0: `1058:0400` and `1058:1c1b` treat both forms as no-ops (`0x00` consumes 1 byte,
  `0x80 v` consumes 2). The direct-to-screen decoder `1050:03e7` handles `0x80 v` correctly
  (`REP STOSB` with `CX = 0`, `1050:0475`), but **not `0x00`**. Its literal loop is
  `LODSB; STOSB; LOOP` (`1050:049f`–`04b8`), so with `CX = 0` it copies a byte before `LOOP`
  decrements `CX` to `0xFFFF`. It then runs 65536 times and corrupts the frame buffer. An
  encoder must never emit a zero-length literal. `1058:0000` never does, and the shipped data
  contains no zero-count packet of either kind [obs, all 760 streams].
* `1058:0400` copies the 4-byte header first, then skips the tag, so its output buffer is
  `{w, h, pixels}` (size `w*h+4`, which `1058:0c1e` computes). It stops when the output
  reaches `w*h+4`. It does no input bounds check, and an over-long final packet would
  overrun the buffer. No shipped file does this [obs: all 760 streams end exactly at `w*h`].
* The stream ends well before the NE padding. In every shipped file the trailing bytes up to
  the next 512 boundary are zero [obs]. A loader must not depend on that.

#### 4.2.2 RUND encoder (`1058:0000`) [obs]

The EXE also contains the compressor. It is called (with its arguments lost by the
decompiler) after the colour-replace helpers `1058:13be` and `1058:174b`, both of which
are unused (§6.5). Re-implemented in `pmap.rund_compress`. Re-encoding the decoded pixels
of all 760 RUND images reproduces the shipped streams **byte for byte**, which confirms
both directions.

Algorithm, in "header-inclusive" coordinates where `src` = 4-byte header + pixels,
`N2 = w*h` and `N3 = N2 + 4`:

1. Emit the header (4 bytes) and `"RUND"`. Set `i = 4`.
2. `run4(j)` is true when `j < N2 - 1` and `src[j] == src[j+1] == src[j+2] == src[j+3]`.
   Because the bound is `N2 - 1` (not `N3 - 1`), no run can start in the last 5 pixels.
   This is an encoder quirk; the decoder does not care.
3. While `i < N3`:
   * If `run4(i)`: value `v = src[i]`. Count `1 + (number of following bytes equal to v)`,
     capped at 127 and at `N3`. Emit `0x80|count, v`.
   * Otherwise: take bytes as a literal until 127 bytes, `N3`, or a position where
     `run4` holds. Emit `count` followed by the bytes.
4. Maximum run is 127 (`0xFF v`); maximum literal is 127 (`0x7F` + 127 bytes).

#### 4.2.3 Statistics [obs]

* RUND: 14,958,590 pixels into 6,182,957 stream bytes (41 %).
* Raw 8 bpp: ids 1380, 1381, 1382, 1383 (the fountain frames, about 107×108; BNDL 328).

---

## 5. Palettes

### 5.1 RRGB (`TMT.DLL`, type 32516, id 900)

| Offset | Size | Content |
|---|---|---|
| 0 | 768 | 256 entries × {u8 R, u8 G, u8 B}, full 8-bit values |
| 768 | 256 | NE padding (zeros) |

[obs] `1048:010a` reads exactly 3 bytes per entry, 256 entries, into a LOGPALETTE with
`peFlags = 0`. RRGB 900 is the only RRGB resource.

Contents (identifying facts only):

* Indices 0–15 are special colours. 0 = `FFFFFF`, 1 = `000000`, then grey/primary entries.
  12–14 = `FEFEFE` (an "opaque white" that survives the white key). **15 = `0000AA`**,
  the transparent key colour.
* Indices 77–255 hold hue ramps of about 8–16 steps each (greys, earths, teals, greens,
  yellows, browns, reds, magentas, purples, blues). The ramps are separated by `000000`
  slots.
* 85 entries are `000000`. The palette has 154 distinct colours, 144 of them not among
  the 20 Windows static colours.
* **241 is also `0000AA`** but no image uses it.
* 106 indices are never used by any of the 764 images, including 16–76 (all black) and 241.
  The pixel histogram can be regenerated with `pmap.py info`.

### 5.2 Palette set-up in 256-colour mode [obs]

1. Startup (`1018:0563`). In WinG mode, `1008:00ee` first creates the WinG DC and bitmap
   (§5.4). Then, in 256-colour mode only, `1048:02b2` replaces `DAT_07d4` with a new
   256-entry palette of all-zero (black) entries (`1048:0000`:
   `LocalAlloc(LMEM_ZEROINIT, 0x408)` + `CreatePalette`) and selects it (a second
   `SelectPalette` happens when not in WinG mode).
2. `1118:0000`, the main game loop entry, loads RRGB 900 into DGROUP `0x3d90`
   (`1038:08d4(900, 0x3d90)`) and calls **`1048:010a`** once:
   * It runs only while the one-shot flag `DAT_0858` (initialised to 1 in DGROUP) is set,
     and clears it. It only acts in 256-colour mode.
   * `SetPaletteEntries(hpal = DAT_07d4, 0, 256, rrgb)` with flags 0, then
     `RealizePalette(window DC DAT_4296)`.
   * In WinG mode it then calls `1008:02ec` (§5.4).
   * It builds the **logical→physical translation table** `xlat[256]` at DGROUP
     **`0x4193`**: `xlat[i] = GetNearestPaletteIndex(P, PALETTERGB(rrgb[i]))`, where `P` is
     a fresh palette made from the current *system* palette entries (`1048:0034`:
     `GetSystemPaletteEntries(0, 256)` + `CreatePalette`).
   * It recomputes the colour key (`1048:008b`, §6.2).
   * It converts the 256-byte shade table at DGROUP `0x876` (§6.5) from logical to
     physical indices (`1048:03bf`): `new[xlat[i]] = xlat[old[i]]`.
3. No other code changes the palette. The only `SetPaletteEntries` call site is
   `1048:01ab`, and the only caller of `1048:010a` is `1118:001e` (NE relocation scan).
   `1048:0247` (load an RRGB by id and apply it, with a "current id" cache at `0x07d6`) is
   never referenced. The EXE does not import `AnimatePalette`. **Result: no palette
   animation, colour cycling or palette fades.** Any visual transitions must be done with
   pixels.

### 5.3 Pixel translation at load (256-colour) [obs]

The frame buffer holds *physical* (system-palette) indices, so every PMAP is translated
when loaded (`1038:03da` / `1038:0ba8` → `1058:1d28`):

* raw image: `1050:0b36` maps all `w*h` bytes through `xlat`;
* RUND image: `1058:1c1b` walks the RLE stream from byte 8 and maps every literal byte and
  every run value through `xlat`. The stream structure is unchanged.

Consequence: logical colours that are identical (e.g. 15 and 241, or 0 and 77) become the
same physical index. The game has 144 non-static unique colours, fewer than the 236 free
system slots, so every RRGB colour can be realised exactly [inf: assumes no other app holds
palette slots]. **A port can keep logical indices end to end, use RRGB 900 directly, and
treat "colour == 0000AA" as transparent.**

### 5.4 WinG identity palette (`1008:02ec`) [obs]

Used when `DAT_0074 != 0`: `[TMTWIN] UseWinG=TRUE` in `TMTWIN.INI` (strings at DGROUP
`0x6af`/`0x6c3`), or a command-line choice (`'4'` = WinG, `'3'` = GDI, written back to the
INI) in `1018:086d`. `1008:0000` loads `WING.DLL` (DGROUP `0x48e`) and imports by
ordinal: 1001 CreateDC, 1002 RecommendDIBFormat, 1003 CreateBitmap, 1004 GetDIBPointer,
1005/1006 Get/SetDIBColorTable, 1009 StretchBlt, 1010 BitBlt).

1. Read the 256 system palette entries.
2. Set `peFlags = 0` for entries 0–9 and 246–255 (the 20 static colours) and
   `PC_RESERVED (1)` for entries 10–245.
3. Copy them as RGBQUADs into the WinG DIB colour table at DGROUP `0x38ba` and call
   `WinGSetDIBColorTable(hWinGDC, 0, 256, …)`. Then `CreatePalette` from the same entries
   (header at DGROUP `0x8a` = `{0x300, 256}`), select it and realize it.

The DIB header at DGROUP `0x3892` is `biSize 0x28, biWidth 512, biHeight 0x41F × DAT_0084`
(`DAT_0084 = -1` in DGROUP, i.e. top-down), `biPlanes 1`, `biBitCount 8`. This gives an
identity mapping, so physical index = DIB index. This is a Windows-only detail; a port can
ignore it.

### 5.5 16-colour palette

* The 4 bpp working buffer is converted to a 4-plane device bitmap by `1050:09e7`. Pixel
  bit 0 goes to plane offset `+0x00`, bit 1 to `+0x40`, bit 2 to `+0x80`, bit 3 to `+0xC0`.
  Each scanline is 4 consecutive 64-byte plane rows (`0x100` bytes). The result is then
  `BitBlt(SRCCOPY)` to the window. The nibble value is therefore the physical VGA colour
  index, without a lookup [obs].
* The colours are the Windows VGA driver's 16 colours, in the order listed in §1. This is
  a generic Windows palette, not game data [inf, strongly supported by two pieces of
  evidence]:
  * The EXE's own "CGA colour → pixel value" table at DGROUP **`0x838`** (16 × u16:
    `0,4,2,6,1,5,1,8,7,12,10,14,9,13,11,15`) maps CGA blue→4, red→1, light-grey→8,
    dark-grey→7, yellow→11. This only makes sense with the Windows VGA order
    (bit0 = R, bit1 = G, bit2 = B, with 7/8 swapped). No colour maps to 3, which is the
    key. Brown (CGA 6) maps to 1 (dark red).
  * Empirically [obs, `extracted\scratch\pmap\cmp16.py`], averaging the RRGB colour of the
    same pixel in the 256-colour twin of every image gives:
    7 → (135,133,121), 8 → (186,185,179), 1 → (125,61,21), 11 → (239,222,81),
    14 → (79,234,241), and so on. That matches 808080 / C0C0C0 / dark-red-for-brown /
    yellow / cyan.
* Exact RGB values on real hardware depend on the driver's DAC programming. The values in
  §1 are the standard Windows ones [inf].

### 5.6 "Named" UI colours (both modes) [obs]

DGROUP **`0x7d8`** holds 16 entries × `{u16 R, u16 G, u16 B}` (values 0..255): the
standard CGA/EGA set (`000000, 0000AA, 00AA00, 00AAAA, AA0000, AA00AA, AA5500, C0C0C0,
555555, 5555FF, 55FF55, 55FFFF, FF5555, FF55FF, FFFF55, FFFFFF`). UI code chooses one by
number `k`:

* `1048:02fd(k)` sets the current drawing colour `DAT_4191`. In 16-colour mode the value is
  `table838[k]`. In 256-colour mode it is `GetNearestPaletteIndex(system palette, rgb7d8[k])`.
  `k` itself is kept in `DAT_4293`.
* `1058:097f` fills a rectangle with colour `k` (`1058:07d0`: 4 bpp `1050:097c`; GDI
  `FillRect` with a solid brush; WinG `1008:0418`). `1058:090f`/`09b8` frame a rectangle the
  same way. Example: `1048:02fd(0); 1058:097f(0,0,512,384)` clears to black.

In a port: map `k` to the nearest RRGB entry (256) or `table838[k]` (16).

---

## 6. Drawing and transparency

### 6.1 Frame buffer [obs]

* One off-screen buffer of **512 × 1055** pixels (`0x200 × 0x41F`; `1018:0563`,
  `1008:00ee(0x200, 0x41F)`), indexed directly by assembly blitters with 32-bit offsets.
  * 256-colour: 1 byte per pixel, stride `0x200`.
  * 16-colour: 4 bpp packed, stride `0x100`, high nibble = left (`1058:153b` allocates
    `512*1055/2`).
* Rows 0–383 are the visible 512×384 picture. Rows 384+ are scratch space for compositing
  and saved backgrounds. Examples: `1058:0b6a(0,0,512,384, 0,384)` copies the screen to
  y=384, sprites are drawn at y=0x1da, and the clip bottom is raised to 0x2FF by `10e0:039f`.
* The 512×384 picture is centred in the window at `(DAT_086e, DAT_0870)` (x rounded down to
  a multiple of 4, `1018:0188`). The border is filled with `BLACK_BRUSH` (`1058:1578`).
  Dirty rectangles (max 256) are collected at DGROUP `0x42a2` by `1058:068e` and presented
  by `1058:05bc`.
* Scene backgrounds are drawn opaque at **(0, 20)** (`1058:0cf3(0, 0x14, img, 0)` after
  `1038:088a(id)`, e.g. `10f0:0038`, `1138:011e`, `1150:0a9f`). A 512×364 background
  therefore fills rows 20–383. A 512×268 one fills rows 20–287, which leaves rows 288–383
  for the 96-row panels (e.g. 2183, 512×96).

### 6.2 The colour key `DAT_4190` [obs]

| Setter | 16-colour (`DAT_086c == 0`) | 256-colour |
|---|---|---|
| `1048:008b` (default; called after palette load and after white-key draws) | `3` | `GetNearestPaletteIndex(sysPal, PALETTERGB(0x00,0x00,0xAA))` (literal `0x02AA0000`) |
| `1048:00b7(r,g,b)` (only caller `1058:0cf3`, with 255,255,255) | `0x0F` (ignores rgb) | `GetNearestPaletteIndex(sysPal, PALETTERGB(r,g,b))` |
| `1048:02b2` (init) | `3` | (palette creation) |

Because pixels were translated through `xlat` (§5.3), comparing a frame-buffer pixel with
`DAT_4190` is the same as asking whether the logical colour is RGB(0,0,0xAA) (indices 15
and 241). Cross-check [obs]: of the 3,016,916 pixels with index 15 in 256-colour images,
2,978,557 (98.7 %) are nibble 3 in the 16-colour twin. The remainder are edge differences
between the two artworks.

### 6.3 Draw entry points (segment `1058`) and blitters (segment `1050`)

| Function | Purpose | 16-colour blitter | 256-colour blitter | Key |
|---|---|---|---|---|
| `1058:1320(x,y,img,mirror)` | **Main sprite draw.** Adds a dirty rect, then `0d43` (mirror=0) or `114c` (mirror=1). 44 far call sites (relocation scan) plus near calls inside `1058`. | – | – | – |
| `1058:0d43(x,y,img)` | Clipped, keyed draw | `1050:0120` (handles odd source/destination x) | Raw: `1050:02e3`. RUND: if fully inside the clip rect, `1050:03e7` (decodes straight to the screen); otherwise decompress (`1058:0400`) to a temp buffer and use `1050:02e3`. | `DAT_4190` |
| `1058:114c(x,y,img)` | Same, **mirrored horizontally** (destination pointer starts at x+w-1 and decrements; `STD; STOSB`) | `1050:04e4` | `1050:06a7` | `DAT_4190` |
| `1058:0c78(x,y,img)` | **Opaque**, unclipped copy (backgrounds) | `1050:0029` | Raw: `1050:00c9`. RUND: `1050:03e7` with key `-1` (no key). | none |
| `1058:0cf3(x,y,img,f)` | `f==0`: opaque (`0c78`). `f!=0`: set key to white (`00b7(255,255,255)`), call `1320(x,y,img,1)` (**mirrored**), then restore the default key. | | | white |
| `1058:0b6a(sx,sy,w,h,dx,dy)` | Copy a rectangle within the frame buffer | `1050:0732` | `1050:0899` | – |
| `1058:1643(x,y,w,h,buf)` | Grab a screen rectangle into a raw PMAP-format buffer | `1050:0c0b` | `1050:0c6e` | – |
| `1058:19b2(x,y,w,h)` | Recolour a screen rectangle through a LUT (highlight effect; called from `1150:0dcd`, `1150:0e14`) | `1050:0cfa` | `1050:0db5` | – |

Keyed blit, exact semantics [obs, `1050:02e3`/`0120`/`03e7`/`06a7`/`04e4`]: for every
source pixel inside the clipped rectangle, if `pixel != key` (4 bpp: the nibble compared
with `key & 0x0F`) write it to the destination, otherwise leave the destination
unchanged. There is no blending and no mask plane.

Clipping [obs]: the clip rectangle is DGROUP `0x868` left, `0x86a` right, `0x864` top,
`0x866` bottom, all inclusive. It is normally reset to (0,0)–(511,383) by `10c8:000c`. The
source is offset by the clipped amount. For the mirrored path the horizontal offset is
measured from the right edge.

Sprite list example [obs, `10b8:06e4`]: entries of 14 bytes at DGROUP `0x5666`, holding a
layer byte, x, y, a flags byte at `+6` (bit 0 = mirror), a far pointer to the image at
`+7`, and a flags byte at `+0xB` (bit 1 = use `0cf3`, i.e. opaque or white-keyed mirror).
Field details belong to the sprite/animation spec.

### 6.4 White key details

`1058:0cf3` with `f != 0` always draws mirrored with the white key. 16-colour: nibble 15
is transparent. 256-colour: `GetNearestPaletteIndex(FFFFFF)` gives the static white
entry, so logical 0 and 77 become transparent, while `FEFEFE` (12–14) and other near-whites
stay opaque [inf about which art relies on this]. It has 14 call sites (relocation scan).

### 6.5 Unused or legacy colour effects (present in the EXE, never referenced) [obs]

The NE relocation scan finds no references to the functions below. Do not implement
them unless another spec finds an indirect call.

* `1058:1379` → `0f7e`, a "shadow" draw. In 256-colour mode (`1050:035f`), each non-key
  source pixel replaces the destination with `shade[dest]`, using the 256-byte table at
  DGROUP `0x876` (made physical at palette load, §5.2). In 16-colour mode it falls back to
  a normal keyed blit. The stored table does not look like a darkening map of RRGB 900
  (e.g. `shade[0]=0xAD`, `shade[1..17]=0x7D`). It was probably built for another palette
  [inf].
* `1058:13be`: replace colour A with colour B inside a PMAP (`1050:0b6f` nibble version;
  `1050:0bc9`), then re-compress with `1058:0000`.
* `1058:174b` / `1712` / `16b5`: recolour a PMAP through a LUT built from a list
  `{u8 count; count × (u8 newIndex, u8 oldIndex)}` (`lut[old] = new`, identity elsewhere).
  `1825`/`187f` do the same to a screen rectangle.

`1058:19b2` (live) builds its LUT in code. In 16-colour mode everything becomes 11
(yellow) except black → 4 (dark blue), dark red stays dark red, and yellow → 4. In
256-colour mode the same rule is applied to the nearest RRGB matches of the named colours
14 (yellow), 0 (black) → 1 (blue), 4 (red) → 6 (brown), 14 → 1. It is a text-highlight
inversion.

### 6.6 Port guidance

* Load PMAPs from `TMT256.DLL` (preferred) or `TMT16.DLL`. Decode to an index buffer and
  composite in logical indices, using RRGB 900 for 256-colour or the VGA16 palette for
  16-colour.
* Implement sprite draws as: keyed copy (key = colour 0000AA or nibble 3), optional
  horizontal mirror, the white-key mirrored variant, and opaque copy. The frame is 512×384
  (plus scratch); backgrounds go at y=20.
* No palette effects are needed. Present by scaling or centring 512×384 with black borders.

---

## 7. Tool: `tools\assets\pmap.py`

Importable (no Pillow needed for decoding):

* `decode(data, depth=None|4|8|16|256, rid=None) -> PMap` with `.width .height .depth
  .pixels` (bytes, w*h indices), `.encoding` (`raw4`/`raw8`/`rund`), `.consumed`, `.size`,
  `.key`.
* `decode_file(path, depth)`, `iter_pmaps(Source, 16|256)`, `rund_decompress`,
  `rund_compress` (the EXE's encoder, exact), `unpack_4bpp`.
* `load_rrgb(path|bytes)`, `VGA16_PALETTE`,
  `transparent_set(depth, palette, key='default'|'white'|None)`,
  `to_rgba(pm, palette, key)`, `to_image(pm, palette, transparent, key)` (Pillow).
* `Source(res_dir=…)` reads `extracted\res`, and `Source(dll_dir=…)` reads the NE DLLs
  directly (through the engine's `tools\ne_resources.py`).

CLI (`python -P tools\assets\pmap.py …`):

* `export [--which 16|256|both] [--res-dir D | --dll-dir D] [--out D] [--no-sheets] [--opaque]`
  writes `extracted\previews\pmap16\<id>.png` and `…\pmap256\<id>.png` (palette PNGs with
  the key transparent) plus 16 labelled contact sheets per depth in `…\sheets\`.
* `info ID` prints the header, encoding and an index histogram.
* `verify` decodes everything, checks padding, and re-encodes the RUND streams.

Results: **1528/1528 images decode with 0 failures**. All 760 RUND streams re-encode
byte-identically. The contact sheets were inspected visually for both depths: the images
are not skewed, the colours are plausible, and transparency is correct.

---

## 8. Catalogue of PMAP id bands

Dimensions are identical in both DLLs. "BNDL" refers to `TMT.DLL` BNDL resources
(`"BNDL"`, u32 count, count × u32 ids), which group ids for loading. Ids ≥10000 inside a
BNDL are OTHR resources, probably animation scripts [inf]. Descriptions are from visual
inspection of the decoded images [obs]; the gameplay roles are [inf].

| Ids | n | Size(s) | Content | BNDL |
|---|---|---|---|---|
| 1000–1002 | 3 | 512×364 | Cave interior backgrounds (stalagmites) | |
| 1003–1024 | 22 | 512×364 / 512×268 | Forest / meadow scene backgrounds: clubhouse entrance (1003), stream, waterfall, caves, tree with ladder rungs and keyhole, signs, gated cave, bridge | |
| 1025–1039 | 15 | 512×364 / 512×268 | Grey-rock / snowy mountain scenes: doors, arched castle doors (1029–1031, 1039), plank bridge, prize room with shelves and "PRIZES" signs (1036–1038) | |
| 1040–1041 | 2 | 512×268 | Interior wall with window and ladder; stone-wall texture | |
| 1090–1092 | 3 | 68×267, 66×145, 66×33 | Ladders (long and short) and a short rail/bridge piece | |
| 1095–1098 | 4 | 83–150×89 | Master of Mischief (wizard) leaning out of a stone window, throwing | 326 |
| 1100–1265 | 166 | about 56–153 × 66–178 | **Player character** animation frames: front/back/side walking, climbing, jumping and falling (1119–1146), carrying the crown (1106–1116), net swing (1155–1162), throwing (1243–1250), magic zapper firing (1206–1237), "POOF" cloud (1176–1180), sparkle (1175), back views (1240–1242, 1260–1265) | 300, 321, 325, 410, 412 |
| 1300–1305 | 6 | 68×33/34 | Key icons, filled (magenta, yellow, red) and outlined (blue) | 301 |
| 1306 | 1 | 502×89 | Bottom status panel: Clues box, Coins, Nets, Treasures (backpack) | 301 |
| 1307–1308 | 2 | 12×11 | Diamond bullet, filled and outlined | 301 |
| 1309 | 1 | 164×60 | "CONTINUE" button | 301 |
| 1320–1325 | 6 | 74–87×123–127 | Stone alcoves with and without a net (net shop) | |
| 1350–1355 | 6 | 87×40/42 | "Nets cost 4 coins" sign, coin markers progressively filled (empty → 4 gold) | 302 |
| 1380–1383 | 4 | about 107×108 | Water fountain with frogs, 4 frames (**the only raw 8 bpp PMAPs**) | 328 |
| 1400–1511 | 112 | 118×133 (all) | Scene **hiding-place objects**, each in several counts/variants: round, conifer and square trees, bushes, sunflowers, daisies, bell flowers, red flags, toadstools, magenta cone plants, no-entry signs, road signs (falling rock, speed limit), apple bushes, trees with nests/acorns/beehives/birdhouses, rocks, spikes, stone blocks, snails, shovels, stumps with woodpecker | |
| 1600–1608 | 9 | 10×2 … 58×36 | Sparkle growing into a gold coin: the **thrower elf's projectile** (`code_1168-1198.md` §4.1; corrected per ARCHITECTURE.md §7.1 C25, was "zapped-elf effect [inf]") | 310 |
| 1609–1664 | 56 | about 24–50 × 36–68 | **Elf** animations (red hat, blue tunic): hit/falling, sitting, crouching, jumping, walking/running (some carrying a small striped object, 1627–1634), standing front and back | 311–320 |
| 1665–1668 | 4 | 27×28 … 76×10 | "zip" text burst and speed streaks | 320 |
| 1699 | 1 | 52×25 | Pile of gold coins | |
| 1700–1782, 1786–1832 | 130 | 71×51 | **Treasures**: about 51 items with 2–4 animation frames each (cat, candle, fish bowl, sailboat, ice-cream, shoes, flashlight, ballet shoes, telephone, drum, candy cane, disguise glasses, camera, pizza, cake, skateboard, ice skates, train, helicopter, propeller beanie, hourglass, bow-tie, glasses, oil lamp, binoculars, clover, dog, ball, balloon, pinwheel, lantern, ...) | 330–380 (one per item) |
| 2000 | 1 | 512×267 | Parchment scroll panel (16-colour version: grey box with blue border) | |
| 2001 | 1 | 44×64 | Elf standing (single frame) | |
| 2010 | 1 | 59×44 | Butterfly | |
| 2011 | 1 | 418×205 | Speech-bubble cloud (white, black outline) | |
| 2100 | 1 | 512×268 | **Title screen** (logo, version text, mountain) | |
| 2101–2110 | 10 | 174×268 | Animated mountain-with-castle overlay for the title | 400 (+OTHR 20100) |
| 2111–2129 | 19 | 10×13 … 99×114 | Player walking up to a rope barrier, stepping over it and walking away, shrinking with distance (2123–2129) | 401 (+20101) |
| 2130–2169 | 40 | about 59–94 × 37–115 | Crowned wizard (Master of Mischief [inf]) casting a spell with a wand and vanishing in a sparkle whirl | 402 (+20102) |
| 2180–2182 | 3 | 296×202, 280×25 ×2 | Wood-board panel and two text-field bars | 322 |
| 2183 | 1 | 512×96 | Blue message box (sign-in prompt) | 322 |
| 2200–2202 | 3 | 512×268 | Backgrounds: tree-top view down the mountain, rock face, rock stairs | |
| 2203–2210 | 8 | 80–109 × 63–265 | Water spout / geyser with frogs (rising column) | 412 (+20204) |
| 2215 | 1 | 127×224 | Foliage overlay (green) | 410 (+20200) |
| 2217–2219 | 3 | 10–20×25 | Keyhole plate, key turning | 415 |
| 2299 | 1 | 512×268 | Castle top room (window, slide hole, drum pedestal, ladder) | |
| 2300–2365 | 66 | up to 186×255 | Castle finale part 1: player with zapper vs. Master of Mischief, magic treasure chest sparkling and opening, slide | 323 (+20300) |
| 2380–2381 | 2 | 512×268, 334×126 | Castle vault room with treasure basket; magenta basket overlay | |
| 2400–2448 | 49 | up to 149×171 | Castle finale part 2: chest opening with glow, wizard defeated/shrinking, player lifting crown onto pedestal | 324 (+20305) |

Ids in 1000–2448 that do not exist [obs]: 1042–1089, 1093–1094, 1099, 1266–1299,
1310–1319, 1326–1349, 1356–1379, 1384–1399, 1512–1599, 1669–1698, 1783–1785, 1833–1999,
2002–2009, 2012–2099, 2170–2179, 2184–2199, 2211–2214, 2216, 2220–2298, 2366–2379,
2382–2399. Total present: 764 in each DLL, the same set.

No fonts are stored as PMAPs (text uses the `FFNT` resource, ids 950–999). No separate
mask images exist.

---

## Open questions

1. **Frame offsets and hotspots.** PMAPs have no origin. Do animation sequences (OTHR ids
   20100, 20101, 20102, 20200, 20204, 20300, 20305, 20308 referenced from BNDLs, and the
   tables at DGROUP `0x5666`) carry per-frame (dx, dy)? To be resolved by the
   animation/sprite spec.
2. **White-key draw is always mirrored** (`1058:0cf3` passes `1` to `1058:1320`). Is that
   intentional, with the art authored facing the other way, or a bug? Which images go
   through it (sprite-table entries with flags2 bit 1 and bit 0 set)?
3. Exact 16-colour RGB values: the doc assumes the standard Windows VGA palette
   (80/C0/FF levels). Is there any driver- or game-specific DAC programming? None was
   found in the EXE.
4. The shade table at DGROUP `0x876` and the colour-replace/LUT recolour code (§6.5) are
   unreferenced in this build. Do the DOS or Mac versions use them (shadows, recoloured
   elves)?
5. Two logical indices (15, 241) share the key colour, and only 15 is used. A port that
   keys on the index rather than the colour should key on 15 *and* 241 to match exactly.
6. Rows 0–19 of the frame (above backgrounds drawn at y=20) and the use of the scratch
   rows 384–1054 belong to the renderer/UI spec and have not been fully mapped here.
