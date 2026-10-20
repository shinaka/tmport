# Gizmos & Gadgets (DOS v1.0): archives, compression and data files

Scope: the data files of *Super Solvers: Gizmos & Gadgets!* (DOS v1.0, 1993, `ssg.exe`): the four
`MRn` resource archives `gizmo000..003.dat`, the method-3 compression, bundles, the resource
types and what each holds, the text encoding, the game tables known so far, `PLAYERS.DAT` and
`gizmo004.dat`. Pictures, palettes, fonts, animation scripts and the final movie are in
`fmt_graphics.md`; songs and the sound driver in `fmt_sound.md`.

Evidence: the G&G Ghidra export (addresses `SSSS:OOOO`: Ghidra segment = image segment +
0x1000; overlays sit at 2a80..4140). `DS:XXXX` is DGROUP 1ea7, file offset `0xFC70 + XXXX` in
`ssg.exe`. Decoders: `tools/gg/ggarc.py` (archive, decompressor, bundles, typing, self-test),
`tools/gg/ggtext.py` (text), `tools/gg/players.py` (PLAYERS.DAT),
`tools/gg/extract_all.py` (everything to `extracted/previews/gg/`). They read the user's files
at run time and contain no game data.

Labels: **[O]** = observed in code, bytes or decoder output; **[I]** = inferred. Game text is
identified by resource id and at most a few words.

---

## 1. Files

| File | Size | Contents |
|---|---|---|
| `ssg.exe` | 213 200 | the program (Borland C++ 3.0, large model, 35 VROOMM overlays) |
| `gizmo000.dat` | 337 881 | archive `MR0`: 878 entries, ids 0x000..0x36d. Mostly sprites, the level descriptions and part catalogues, the two fonts, palettes, race paths, tables [O] |
| `gizmo001.dat` | 349 371 | archive `MR1`: 479 entries, ids 0x1000..0x11de. Screens, texts, hints, help, sound banks, DAM scripts and their frames [O] |
| `gizmo002.dat` | 280 359 | archive `MR2`: 335 entries, ids 0x2000..0x214e. Puzzle screens, puzzle texts and per-variant puzzle data [O] |
| `gizmo003.dat` | 259 149 | archive `MR3`: 205 entries, ids 0x3000..0x30cc. The final movie (202 delta frames) and its music bank pair [O] |
| `gizmo004.dat` | 45 644 | not game data: a screen save-under the game writes at run time (§9) [O] |
| `PLAYERS.DAT` | 578 487 | roster and saved players, pre-allocated to full size (§8) [O] |
| `ssg.bat`, `Read_1st.txt`, `ssg.ico` | small | launcher (reports error level 5 = no 286), notes, icon |

The program opens `gizmo000..003.dat` once at start-up, read-only (`O_RDONLY|O_BINARY` 0x8001),
by bare name relative to the current directory, and keeps all four open (`Res_OpenFiles`
14ff:0167; name table of far pointers DS:0550, handles DS:6566[4]) [O]. The four archives are
independent: each has its own header and id range [O].

---

## 2. The MRn archive [O]

### 2.1 Layout

All numbers little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `'M' 'R'` + ASCII volume digit (`'0'`..`'3'`) + 0x1A |
| 4 | 4 (n + 1) | u32 `off[0..n]`, **relative to file offset 4**. `off[0] = 4 (n + 1)` gives the entry count; `off[n]` = file length - 4 (end sentinel) |
| 4 + off[k] | off[k+1] - off[k] | entry k: `u8 type`, `u8 method`, payload |

* Resource id = `volume << 12 | k`. Volume 0 holds ids 0x000..0x36d, volume 1 0x1000..0x11de,
  volume 2 0x2000..0x214e, volume 3 0x3000..0x30cc. There are no names.
* `type` is a letter (or 0x01 for a bundle) that says what the payload is (§5).
* `method` 0 = stored, 3 = compressed (§3). The game treats every non-zero value as compressed
  (14ff:00ba tests `== 0`); 3 is the only non-zero value in the data.
* The last entry of every volume is type `'I'`, stored, payload `5A A5` (the word 0xA55A).
  `Res_OpenFiles` reads the last entry of volumes 0, 1 and 2 (ids 0x036d, 0x11de, 0x214e) and
  prints "Incorrect Resource File n" (DS:05A5/05BF/05D9) through `Dos_PrintString` 1d3c:0027 if
  the word differs. That routine only prints (INT 21h); the game goes on. Volume 3 is not
  checked [O].

| Volume | Entries | Method 3 | Stored | Bytes stored (payloads) | Bytes unpacked |
|---|---|---|---|---|---|
| 0 | 878 | 600 | 278 | 332 605 | 1 907 471 |
| 1 | 479 | 354 | 125 | 346 489 | 1 830 050 |
| 2 | 335 | 332 | 3 | 278 341 | 1 537 118 |
| 3 | 205 | 204 | 1 | 257 911 | 479 048 |
| **all** | **1897** | **1490** | **407** | **1 215 346** | **5 753 687** |

(The data pass of the scoping counted 1059 compressed entries; the true count is 1490 [O,
`ggarc.py selftest`].)

### 2.2 Loading [O]

| Routine | What it does |
|---|---|
| `Res_Read(id)` 14ff:000d | handle = DS:6566[id >> 12]; seek to 4 + 4 * (id & 0xFFF), read `off` and `next` (two u32); seek to `off` + 4; read the type byte into DS:6572 and the method byte; method 0: read `next - off` bytes; else `Lzh_Decompress(fd, buffer)`. Always into the one shared load buffer (far pointer DS:6574, 0xFFFF bytes, allocated by `Res_OpenFiles`). Returns the size; sets DS:0560 = `id & 0xFFF`; DS:054e = 1 while it runs |
| `Res_LoadCopy(id)` 14ff:00fe | `Res_Read`, then a private copy (`Mem_Alloc` + copy); returns the copy |
| `Res_LoadTo(id, dst)` 14ff:013d | `Res_Read`, then copy into the caller's buffer |
| `Lzh_Decompress(fd, dst)` 1b48:02aa | §3 |

Quirks [O code; consequences I]:
* For a **stored** entry `Res_Read` reads and returns `next - off` bytes, which counts the type
  and method bytes already read: the buffer receives the payload plus the first 2 bytes of the
  next entry (for the integrity entries: 2 bytes past the end of the file, so the read is short),
  and the returned size is 2 too large. Copies made by `Res_LoadCopy` are 2 bytes longer than the
  payload. Nothing seen depends on it.
* DS:0560 keeps the index without the volume bits, while 15df:01a5 compares it with a full id
  to skip a reload. The shortcut can only fire for volume-0 ids.
* All lengths are computed in 16 bits. Every resource is smaller than 64 KB (largest 64 014 bytes,
  a 320x200 raw picture), and the buffer is 0xFFFF bytes [O].
* No read is checked for errors.

---

## 3. Method 3 compression [O]

A static (per resource) canonical Huffman code over literals and match lengths, a second
canonical code for distance high parts, and LZ77 copies inside the output. `Lzh_Decompress`
1b48:02aa; bit reader 1b48:0080 (one bit) and 1b48:00af (n bits); literal/length code lengths
1b48:0268; default distance lengths 1b48:021f; explicit distance lengths 1b48:028e; recursive
code builder 1b48:0109 (started by 1b48:01fd); input refill 1b48:0002 (4000-byte blocks read
straight from the file handle).

### 3.1 Stream

```
u32 LE   size word: bits 0..29 = unpacked size; bits 30..31 = 11 in every entry (the game
         masks them off: 1b48:02cf AND 0x3FFF on the high word)
then a bit stream, most significant bit of each byte first:
  1 bit      window flag w: window bits W = 12 + w; distance codes D = 2^(W-6) (64 or 128)
  16 bits    read and ignored by the game; in all 1490 entries it equals the number of
             tokens (literals + matches) in the stream, modulo 65536
  286 x      literal/length code lengths, symbol 0..285 in order:
               1 bit present; if 1: 4 bits L-1 (length 1..16); if 0: length 0 (unused)
  1 bit      e: 0 = default distance lengths (3.4), 1 = explicit:
               D x 4 bits, length 0..15 for distance code 0..D-1 (0 = unused)
  tokens until the output holds `size` bytes (there is no end symbol)
```

Bits are taken from the most significant end of each byte; a field of n bits is read high bit
first. The entry ends at the last byte that holds a used bit (0 bytes of slack in every entry).

### 3.2 Canonical codes

Both codes are built the same way (1b48:0109). Take the symbols with a non-zero length, ordered
by length, then by symbol number. The first gets code 0 with its length; each next symbol gets
`(previous code + 1) << (its length - previous length)`. A code of length L is read as L bits,
high bit first. This is the canonical code of Deflate.

The game builds it as a tree, depth first, 0 branch first, handing each leaf the next symbol in
that order, so the two are identical for a complete code. Every code in the data is complete
(Kraft sum exactly 1) [O, self-test]; an incomplete one would make the builder recurse without
end [I].

The game decodes with an 8-bit lookup table (built with the tree: every code of up to 8 bits
fills its 2^(8-L) entries with symbol and length; internal nodes at depth 8 are entered with
length 8) and walks the tree bit by bit below that. This is an implementation detail; any
canonical decoder gives the same result.

### 3.3 Tokens

Decode a literal/length symbol s:

| s | Meaning |
|---|---|
| 0..255 | literal byte s |
| 256..284 | match of length s - 253 (3..31) |
| 285 | read 8 bits n; match of length 32 + n (32..287) |

For a match, decode a distance symbol c (0..D-1), read 6 more bits b; distance = c * 64 + b + 1
(1..4096 or 1..8192). Copy `length` bytes one at a time from `distance` bytes back in the output
(so a copy may overlap its own output). The game copies straight within its output buffer: the
window is the output itself, W only limits the distance.

Observed over all 1490 entries [O, `ggarc.py selftest`]: W = 13 in all; 1313 use the default
distance lengths and 177 explicit ones; no match reaches before the start of the output or past
the stated size; no stream reads past its entry.

### 3.4 Default distance lengths (DS:13D0) [O]

Row `W - 12` of a table of 3 rows x 16 words at DS:13D0. Word 0 is the length of distance code
0; every following non-zero word is a code number at which the length grows by one (1b48:021f;
the zero words that fill a row never match because the scan is past code 0 by then). Rows:

| Row | Words | Lengths it gives |
|---|---|---|
| W = 12 (64 codes) | 2, 1, 2, 4, 7, 17, 34 | code 0: 2; 1: 3; 2-3: 4; 4-6: 5; 7-16: 6; 17-33: 7; 34-63: 8 |
| W = 13 (128 codes) | 2, 1, 1, 3, 6, 13, 31, 78 | code 0: 2; 1-2: 4; 3-5: 5; 6-12: 6; 13-30: 7; 31-77: 8; 78-127: 9 |
| (W = 14, unreachable) | 3, 1, 4, 12, 24, 48 | a 1-bit flag cannot select it |

Both reachable rows form complete codes. The values are those of the user's `ssg.exe`
(`ggarc.py selftest --exe` re-reads them and checks).

### 3.5 Example

Resource 0x0001 (a bundle): size word gives 3026 bytes; window bit 1; the ignored word 0x00CF
= 207 tokens (82 literals + 125 matches); default distance lengths; 372 stored payload bytes
[O, `ggarc.py info 0001`].

Nothing like this exists in Treasure Mountain (TM DOS: plain files with byte/word RLE and a word
dictionary; TM Windows: RUND RLE). tmport's `util/inflate` is Deflate and does not apply.

---

## 4. Bundles (type 0x01) [O]

```
u16 off[0..n]     off[0] = 2 (n + 1) = size of this table; off[n] = payload length
member i = payload[off[i] .. off[i+1]) = u8 type, u8 method (always 0), member payload
```

* 364 bundles, 3945 members; every bundle parses exactly, no bundle contains a bundle [O].
* A bundle is read with `Res_Read`/`Res_LoadCopy` like any resource, then walked in memory; members
  are not separate resources and have no ids. This document writes member m of bundle id as
  `id/m`.
* Kinds seen (member type sequences): picture with palette `[P palette, G]` (picture loaders
  15df:00a1 / 01a5: member 0 of type `'P'` is applied as a palette, member 1 is the picture);
  sprite and frame sets `[G ...]`, often with palette members and ending in an `L` member whose
  payload starts with 0 (15df:023b walks members until a payload starts with 0, applying every
  `'P'` palette member it meets and caching the others as pictures); DAM frame sets (3797:100e
  turns the offset table into the frame table DS:ba26); music banks `[M ..., I 0, S BMF]`
  (1cdd:0024) and effect banks `[S BMF ...]` (1cdd:012d), see `fmt_sound.md` §2; text sets
  (dialog boxes `[S title, S text, ...]`, 4106:0000; help pages `[I, S, I, I, H, H]`); part
  name lists in volume 0 `[I, I, S, T x n, G x n, S x n, I, M x m]` (0x2c..0x30).
* The type letter of a member means what its loader expects: the same letter serves different
  record kinds in different bundle kinds (`M` is a channel list in a music bank and a 12-byte
  record in a part list) [O].

The "first u16 = table size" header is a TLC habit also seen in TM DOS's TUNES/SFX and
ALL_PUZS; TM Windows' BNDL refers to its members by id instead.

---

## 5. Resource types

Counts: entries / bundle members [O, `extract_all.py` summary]. Kind = what `ggarc.classify`
reports; it is decided by type letter, magic and place.

| Type | Entries | Members | Kind and content | Loader / evidence | Confidence |
|---|---|---|---|---|---|
| 01 | 364 | - | bundle (§4) | 15df, 1cdd, 3797, 4106, 411d | O |
| `G` | 502 | 2387 | `LPX` 0x1A picture, raw or RLE (`fmt_graphics.md` §2) | 1399:02ee / 0335 | O |
| `P` | 26 | 94 | palette range (24 + 26 = 50; `fmt_graphics.md` §3) **or** an `LPX` picture (70 members, bundles 0x1141..0x114a) | 1473:02f0 / 041c, 15df | O |
| `F` | 103 | - | ids 9 and 10: `fnt` 0x1A fonts (`fmt_graphics.md` §5); the other 101 (volume 2): per-variant puzzle data, e.g. 0x206d + variant read by the simple-machines puzzle (3938:0dfe) | 2b16:0000; puzzle overlays | O fonts, I puzzle data |
| `D` | 231 | - | `DAM` 0x1A scripts 0x114b..0x1156 (12); final-movie delta frames 0x3000..0x30c9 (202); level descriptions 0x2f5..0x303 (15, §7.1); puzzle tables 0x11a1 (magnets, 35bb:1c03) and 0x211b (balance, 32fe:0aa0) | 3d39, 1473:07d9, 153f:0002 | O |
| `T` | 222 | 149 | text, NUL-terminated (§6) | text renderer 2b89 | O |
| `H` | 19 | 13 | New Player Hints 0x1043..0x1055 (`FF 06` note, then text) and help page texts (members of 0x105d..0x1062) | 3060:013b, 411d | O |
| `S` | 278 | 687 | `BMF` 0x1A songs (2 entries + 493 members; `fmt_sound.md`); short strings (251 + 186: titles, labels, part names); 25 + 8 binary records, among them animation sequence sets and actor sets (§7.3) | 1bb4:0ebc, 174b:0324, 1834:0088 | O |
| `I` | 99 | 120 | small integer arrays (2..40 bytes): 0x350 + race (32 bytes, read with the room set, §7.2), 0x1071, ...; the 4 integrity words; in banks and sprite sets `I 0` ends a member list | many | O shape, partly I use |
| `A` | 22 | - | room sets 0x347..0x34f, one per race through DS:09e8 (§7.2); animation sequence sets like the `S` ones (§7.3; 0x0c, 0x0d, 0x0f, 0x11, 0x1b read by 174b:0324 for the workshop, 2dee:1a90); 0x100b..0x1017 (attract, 2c11:0000 [I]) | 1622:0085, 174b:0324 | O, attract I |
| `R` | 18 | - | race paths, records of 6 bytes (all sizes are multiples of 6) | DS:39f4 / DS:3a12 per race (3dd6) | O ids, I fields |
| `W` | 13 | - | 0x11d6..0x11dd: the 8 patterns of the random wipe (1473:0042 picks 0x11d6 + random, 1473:0479 draws); 0x264..0x268: aircraft-race data per level (3e77) | 1473:0042, 3e77 | O |
| `M` | - | 388 | music-bank channel lists (`01 ... FF`), 12-byte records in part lists | 1cdd | O shape, I meaning |
| `L` | - | 86 | `00 00 00 00` end markers of sprite sets (84); `04` + u16 song length in ticks in music banks 0x27/0x28 (2), read one byte off by 1cdd:0024 (`fmt_sound.md` §2.1) | 15df:023b, 1cdd:0024 | O |
| `C` | - | 21 | `FF`: a hole in a speaker effect bank (no speaker version of that effect) | 1bb4:0ebc rejects it | O |

---

## 6. Text encoding [O]

Texts (`T`, `H`, string `S`) are NUL-terminated byte strings drawn by the text renderer in
overlay 2b89 (2b89:007a measures and draws in one pass; character switch CS:03ba, escape switch
CS:03ae). It is the TLC renderer of TM Windows (10b0) with fewer control codes (see the scoping
report §4).

| Bytes | Meaning | Use in the data |
|---|---|---|
| `00` | end | every text |
| `0D`, `0A` | line break | CR in 462 places, LF in 37 |
| `20` | space; the word-wrap point | |
| `FF 01 n` | insert the caller's substitution string n - 1 | none |
| `FF 02 ... FF` | underline on/off | none |
| `FF 03 c ... FF` | text colour c - 1 until the next colour escape | 116: part texts (c = 0x10, colour 15) and help pages (c = 0x0A and 0x01 to highlight words) |
| `FF 04` | ignored | none |
| `FF 05 ... FF` | centring on/off (re-centres the line) | none |
| `FF 06 ... FF` | comment, not drawn | 19: a designer's trigger note at the start of each New Player Hint |
| `11`, `17`..`1D`, `1F` | key-cap pictograms of font 9: 0x11 Spacebar, 0x17 Ctrl, 0x18 Enter, 0x19 Left, 0x1A Right, 0x1B Down, 0x1C Up, 0x1D Ctrl+R, 0x1F Esc | 38 |
| `0C`, `12` | codes without a glyph in font 9: drawn as nothing, advance 0 (2b16:0368) | 5, next to a key-cap in two help/hint texts |
| `%d`, `%s` | printf fields; the caller formats the text with `sprintf` before drawing | 2 |

No other bytes below 0x20 and no bytes of 0x80 or more (other than `FF` escapes) occur [O,
`extract_all.py` text statistics]. A text box wraps at spaces (`3f56:0000` lays out message
boxes 280 pixels wide); after a wrap the renderer also skips following TAB bytes, a vestige of
TM Windows' code (G&G has no TAB handling) [O, verify pass].

---

## 7. Game tables known so far (partial)

Only shapes and the loaders are established; field meanings are wave-1 work.

### 7.1 Level descriptions and part catalogues, D 0x2f5 + race (15) [O offsets, I meaning]

Loaded by 153f:0002 for race index DS:4590 = building * 5 + level (0..14) into the far pointer
DS:6638.

| Offset | Field |
|---|---|
| +0x00 | u16 building (0..2) |
| +0x02 | u16 n = number of part records |
| +0x04 | u16 (5..11; not traced) |
| +0x06 | u16 k = number of words in the table at +0x2d |
| +0x08 | u16 level palette id (a `P` range, stored into the shadow palette by 2dee:028f via 1473:041c) |
| +0x0a | u16 sprite bundle id (pictures of the parts; 15df:023b) |
| +0x0c..+0x24 | u16 fields, not traced |
| +0x25, +0x29 | far pointers patched at load: to the word table, to the part records |
| +0x2d | k u16 |
| +0x2d + 2k | n part records of **0x8E (142) bytes**: +0x10 picture index in the sprite bundle, +0x16 text resource id (loaded and its pointer patched in at +0x86), +0x68/+0x6a cleared, +0x82 picture pointer and +0x8c index patched at load |
| after the records | Morty's vehicle (DS:6634 points here); layout not traced |

### 7.2 Room sets, A 0x347..0x34f [O loader, I fields]

`1622:0085` (building set-up) loads, for race index r = DS:4590: music bank 0x35f + 2 *
building (`fmt_sound.md` §2), the room set DS:09e8[r] (0x347..0x34f: three per building, 8, 12
or 16 rooms) and `I` 0x350 + r (16 words; its first word minus the part count of the level
description goes to DS:7a00). A room set is `u16 room count`, then per room a 0x120-byte
header whose count words select what follows: up to 4 layers of 0x20-byte objects, 4 lists of
0x48-byte animated objects and lists of 10-byte records, with far pointers to them patched into
the header at load; the room pointers go to the table DS:7a04 (16 entries). The systems pass
describes the object types (pictures, locked puzzle doors, open doors with link and target
room) [O shapes, I field meanings].

### 7.3 Sequence sets and actor sets (S records) [O loader, I fields]

* Animation sequence sets (`S` 0x1005, 0x1007, 0x1072..0x1087; `A` 0x0c, 0x0d, 0x0f, 0x11, 0x1b), loaded by
  174b:0324: a header of 0x32 bytes with run-time fields; +0x18 = id of the frame bundle
  (15df:023b), +0x2c = number of sequences s; then s far-pointer slots (filled at load); then s
  sequences, each 8 bytes, u16 count c, c steps of 10 bytes.
* Actor sets, loaded by 1834:0088: the player's 0x1033..0x1036 (18f5:05a3) and the Cyber Chimps'
  0x1041/0x1042 for levels 0..2 and 0x103d/0x103e from level 3 (2a80:051e).

### 7.4 Other tables [O shapes]

* `R` race paths 0x00e7..0x00ec (land races: player path DS:39f4[race], Morty's path
  DS:3a12[race]) [O]; 12 more `R` between 0x24b and 0x25f, near the aircraft resources
  [I: aircraft races]; 6-byte records, read as s16 x, s16 y, u16 [fields I].
* Race id tables in DGROUP, 15 words each, indexed by race: background picture DS:399a (palette
  = id + 1), sprite bundles DS:39b8 and DS:39d6, paths DS:39f4 and DS:3a12, DS:3a30 (0..2) [O].
  The five aircraft entries (5..9) are 0; aircraft races use 3e77 with `W` 0x264 + level and
  palette 0x245.
* `F` (volume 2, sizes 180..1392): four runs of per-variant puzzle data, 0x2002..0x2020 (31),
  0x206d..0x2080 (20), 0x209c..0x20af (20) and 0x20bf..0x20dc (30) [O]. The run lengths equal
  the variant counts of the gears, simple-machines, machine-pieces and electric puzzles
  (DS:3334 = 30, 20, 20, 31, ... for types 0..7), and 0x206d + variant is read by the
  simple-machines puzzle (3938:0dfe) [O]; the other assignments are inferred. `I` 0x1071 (30
  bytes) is read by the results screen (2fcb:0325) [I: race reward values]. The puzzles also read
  variant data at 0x20dd/0x20fb + v (electric, 3083:256b), 0x2051/0x2059 + v
  (simple machines, 3938:0ed8), 0x2088 + v (machine pieces, 3a4a:0ca8), 0x202b + v (gears,
  3b2f:1bd5) [O load sites].
* Rank names and thresholds, menus, the start-up messages are in the EXE or in resources
  0x368..0x36c; see the scoping report.

---

## 8. PLAYERS.DAT [O]

Overlay 2b5a (open 2b5a:004c, read a slot 2b5a:0198, save 2b5a:01ea, blank record
2b5a:0000) and the sign-in overlay 2caa (create 2caa:004f, insert/remove 2caa:01c5).

### 8.1 Layout

| Offset | Size | Content |
|---|---|---|
| 0x000 | 4 | `'U' 'S' 'R'` 0x1A (written as the words 0x5355, 0x1A52) |
| 0x004 | 2 | u16 version = 12 |
| 0x006 | 2 | u16 player count |
| 0x008 | 100 x 22 | index entries (only 0..98 are written) |
| 0x8A0 + s * 0x16BD | 99 x 5821 | player records, slot s = 0..98 |

0x8A0 + 99 * 0x16BD = 578 487 = the file size.

* **Open** (2b5a:004c): mode 1 = read-only binary (0x8001), otherwise read/write (0x8004), with
  create and truncate when bit 2 is set (0x8304). A read-mode open reads the 8 header bytes and
  fails unless the version word is 12.
* **Create** (2caa:004f, when the open fails): the header with count 0, 99 index entries of
  zeros with slot = entry number, then 99 blank records written one by one through the save
  routine. The file is **pre-allocated** at full size [O]. The 100th entry's 22 bytes are never
  written (the shipped file has leftover bytes there).
* **Write failure** (2b5a:0156 → 2b5a:010a): shows message 0x106f, deletes `players.dat` and
  exits.

### 8.2 Index entries

An index entry is a copy of the first 22 bytes of the record: `char name[16]`, `u32 points`,
`u16 slot`. Entries 0..count-1 are the players, kept in name order by 2caa:01c5; the rest hold
free slots in the order they will be taken (a new player takes the slot of entry `count`; a
removed player's slot goes back to entry `count` after the shift). 2caa:01c5 also tests the
points field for the values -1/-2 [O; purpose not traced]. The save writes the record to its
slot and the 22 bytes to entry DS:a079 (the player's index position).

The shipped file has count 0, but entry 0 holds slot 98 and slot 0 appears nowhere, so slot 98
is listed twice [O]. With this index the first new player gets slot 98 (seen in the wave 0
captures: the first player signed in was written to the record at 0x8A0 + 98 * 0x16BD,
`REFERENCE_CAPTURES.md` §3.4 [O]) and slot 0 is never used; if all 99 entries were ever filled,
two players would share slot 98 [I].

### 8.3 Record (0x16BD bytes = DGROUP DS:4562..5C1E)

The record is the memory image of the current player; DS address = 0x4562 + offset.

| Offset | Size | Field | Evidence |
|---|---|---|---|
| +0x000 | 16 | name, NUL-terminated (up to 15 characters, first letter upper case) | 2caa [O] |
| +0x010 | 4 | u32 points (DS:4572) | [O] |
| +0x014 | 2 | slot number (DS:4576); the save seeks to 0x8A0 + slot * 0x16BD | 2b5a:01ea [O] |
| +0x016 | 2 | "with/without chimps" choice (DS:4578) | 2caa [O] |
| +0x01A | 20 | live state DS:457c..458f (projectiles DS:457e, DS:4580, parts needed DS:4582, hint conditions DS:458a/458c) | 2b5a:01ea [O] |
| +0x02E | 2 | race index DS:4590 = building * 5 + level | 12e0:0820 [O] |
| +0x030 | 2 | current building DS:4592 (-1 none) | 2b5a:0000 [O] |
| +0x032 | 2 | level DS:4594 | [O] |
| +0x034 | 3 x 2 | races won per building DS:4596 (-1..4) | [O] |
| +0x03A | 3 x 2 | building has a saved block DS:459c | 2b5a:01ea [O] |
| +0x040 | 2 | hints off DS:45a2 | [O] |
| +0x042 | 19 x 2 | hint shown, DS:45a4 + 2 * (hint id - 0x1043) | 3060:013b [O]; count I |
| +0x068 | 2 | not identified | |
| +0x06A | 2 | sound off DS:45cc (a new record copies DS:1ad4) | 2b5a:0000 [O] |
| +0x06C | 2 | not identified | |
| +0x06E | 8 x 33 x 2 | puzzle flags DS:45d0 + type * 0x42 + variant * 2 | 38e2 [O] |
| +0x27E | 3 x 0x6BA | per-building blocks (§8.4) | [O] |
| +0x16AC | 2 | races won in all DS:5c0e (15 = the game is over) | 12e0:0564 [O] |
| +0x16AE | 15 | not identified | |

A blank record (2b5a:0000) is all zero except +0x30 and +0x34..+0x39 = 0xFFFF and +0x6A [O].

### 8.4 Per-building blocks (0x6BA = 1722 bytes, DS:47e0 + building * 0x6BA)

When the player leaves a building in progress (DS:0094 set), 2b5a:01ea copies the building's
live state into its block and sets the flag at +0x3A:

| Block offset | Size | Written by |
|---|---|---|
| +0x000 | 0x14 | copy of record +0x1A |
| +0x014 | 0x258 | 153f:0385 (parts and objects) |
| +0x26C | 0x96 | 1622:0fb3, first pointer (rooms) |
| +0x302 | 0x190 | 1622:0fb3, second pointer (rooms) |
| +0x492 | 0x228 | 1834:0b5e (actors) |

Offsets [O]; sizes are the gaps between them [I]; contents to be specified in wave 1. Entering a
building whose flag is set restores the live state from the block (state 4, 12e0:0820) without
asking [O, systems pass].

Compared with Treasure Mountain: only the `USR` 0x1A magic is shared with TM Windows'
`users.dat`; the version word, count, index and random-access slots are new (scoping §4).

---

## 9. gizmo004.dat [O]

Not game data. `Screen_SaveToSwap` 14ff:0226 creates it at run time with
`O_RDWR|O_CREAT|O_TRUNC|O_BINARY` (0x8304, mode 0x180), writes `u16 w, u16 h` and w * h bytes
grabbed from the screen (`Gfx_GetImage` 1399:0194); `Screen_RestoreFromSwap` 14ff:0284 reads it
back (both called from the workshop overlay, 2dee:17eb / 2dee:185d). Neither checks for
errors [O]. The shipped file is a leftover (w 280, h 163: 4 + 280 * 163 = 45 644 = its size)
showing a warehouse screen. A port can keep this area in memory.

---

## 10. Decoder outputs

`python -P tools/gg/extract_all.py` writes every entry and member to
`extracted/previews/gg/raw/`, the decoded forms (PNG, text, listings) beside them and
`index.tsv` with one row per entry and member (id, volume, file offset, type, method, sizes,
kind, details, the palette used for pictures, outputs). See `tools/gg/README.md`.

---

## 11. Porting notes [I]

* The archive reader and decompressor are about 150 lines of C++; a canonical-Huffman table
  decoder is simpler than the game's recursive builder and gives the same codes (§3.2). Keep a
  self-test like `ggarc.py selftest` (sizes, no over-read, the four 0xA55A words) in the test
  suite, reading the user's files through `GG_DATA`.
* Reproduce `Res_Read`'s returned size for stored entries (payload + 2) only if some caller turns
  out to depend on it; nothing seen does.
* Bundles are parsed in memory; offsets are relative to the bundle payload.
* PLAYERS.DAT: write the header, index and 99 slots exactly as 2caa:004f does when creating, and
  only the player's slot and index entry on save, so files stay byte-compatible with the
  original. Keep the duplicate-slot behaviour of a shipped file as it is (§8.2) and decide in
  wave 1 whether to repair it.

## 12. Open questions

* The meaning of the per-building blocks (§8.4) and of the unidentified record fields
  (+0x68, +0x6C, +0x16AE).
* How the shipped index came to hold slot 98 twice and no slot 0, and whether 2caa:01c5 can
  produce that (§8.2); what the -1/-2 test on the points field is for.
* Field meanings of the level descriptions (+0x04, +0x0c..+0x24, the tail), the room sets and
  their objects, the sequence and actor sets, `R` records, `A` 0x100b..0x1017, `W` 0x264..,
  `I` arrays, the `F` puzzle data and the puzzle tables 0x11a1 / 0x211b.
* What the `M` channel lists of music banks select (`fmt_sound.md` §2).
* Which tool wrote the token count into the 16 ignored bits, and whether any other
  build of the decompressor reads it.
