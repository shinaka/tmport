# BNDL bundles, the FFNT font, and in-game text rendering (Windows CD)

Scope: `TMT.DLL` Mac-type `BNDL` (Win type 32513, ids 300–415), `FFNT` (Win type 32514,
id 999), the EXE's resource loader that consumes them, and the complete text pipeline
(`10b0:*`, `1068:*`, `1060:*`). Sources: `re/win/ghidra_export/` (functions, disassembly),
a re-disassembly of segment `10b0`'s jump-table handlers (`10b0:014e`–`0496`, not in the
original export), whole-EXE scans of NE relocations / immediates / DGROUP (to cover the code
Ghidra did not put into functions), and the raw resources under `extracted/res/`.

Notation: **[O]** = observed (bytes / disassembly / decoded output); **[I]** = inference.
`DS:xxxx` = DGROUP (`11b8:xxxx`, file offset `0x23c00 + xxxx` in `TMTWINCD.EXE`).
All multi-byte values are little-endian.

Tools: `tools/assets/bndl.py`, `tools/assets/ffnt.py` (importable + CLI, see §9).
Previews: `extracted/previews/bndl.json`, `extracted/previews/font/*.png`.

---

## 1. Resource-manager background (how ids become data)

### 1.1 Type is implied by the id **[O] `1038:038a`**

The game emulates a Mac Resource Manager. Callers usually give **only an id**, and
`1038:038a` derives the 4-char type name (pointers to strings at `DS:0741..075a`):

| id range | type | Win type | DLL |
|---|---|---|---|
| `< 900` | `BNDL` | 32513 | `TMT.DLL` |
| `900..949` | `RRGB` | 32516 | `TMT.DLL` |
| `950..999` | `FFNT` | 32514 | `TMT.DLL` |
| `1000..3999` | `PMAP` | 32513 | `TMT256.DLL` or `TMT16.DLL` (instance handle `DS:0872`) |
| `4000..9999` | `SSND` | 32513 | `TM0..8SOUND.DLL` by sub-range (below) |
| `>= 10000` | `OTHR` | 32515 | `TMT.DLL` (handle `DS:09f0`) |

SSND DLL selection (`1038:00c9` / `1038:03da` if-chain) **[O]**: 7000–7099 → handle `DS:09de`
(TM0), 7100–7999 → `09e0` (TM1), 4000–4399 → `09e2` (TM2), 4400–4799 → `09e4` (TM3),
4800–5199 → `09e6` (TM4), 5200–5599 → `09e8` (TM5), 5600–5999 → `09ea` (TM6),
6000–6399 → `09ec` (TM7), 6400–6799 → `09ee` (TM8). 6800–6999 and 8000–9999 have no DLL
(load fails). This matches the extracted id ranges of the sound DLLs.

### 1.2 Win16 lookup **[O]/[I]**

`FindResource(hInst, MAKEINTRESOURCE(id), "BNDL")` etc. (type passed as a *string*). Each DLL's
resource type 15 is a Windows 3.0 **RT_NAMETABLE**: records
`{u16 cb=12; u16 typeOrdinal (0xFF01..0xFF04 = 0x8000|32513..32516); u16 id; "BNDL\0"; "\0"}`
mapping the string type names to the numeric types **[I: standard RT_NAMETABLE semantics]**.
A port can simply hard-code `BNDL→32513, FFNT→32514, OTHR→32515, RRGB→32516, PMAP/SSND→32513`.
NE payload lengths are padded to 512 bytes; `SizeofResource` returns the padded length.

### 1.3 Loader functions (segment `1038`) **[O]**

| Function | Behaviour |
|---|---|
| `1038:00c9 ResSize(type, id)` | `SizeofResource` (u32) in the DLL chosen as above; **×2 if type == "BNDL"** (`strncmp(type,"BNDL",4)` at `1038:035c`, string `DS:073c`). |
| `1038:03da ResLoadCopy(type, id)` | `GlobalAlloc(ResSize)`, `FindResource/LoadResource/LockResource`, copies `ResSize` bytes (**halved again for BNDL**, i.e. the real resource length) into the new block, frees the Win resource. For `P*` types in 256-colour mode it also calls `1058:1d28` (PMAP post-processing; see PMAP doc). Shows *"You are out of memory."* MessageBox on failure. Returns far ptr. |
| `1038:076f GetResource(id)` | Recursive loader: `p = ResLoadCopy(type(id), id)`; if `p` starts with `"BNDL"` (`DS:0794`): for each entry *i* (count = u16 at `p+4`): `child = u16 at p+8+4i`; `p[8+4(count+i)] = ResSize(type(child), child)` (u32); `p[8+4i] = GetResource(child)` (far ptr). Returns `p`. |
| `1038:08c5` | `GetResource(id)` wrapper used by most callers. |
| `1030:0082 DisposePtrDeep(p)` | Recursive free: if `p` is `"BNDL"`, free each `p[8+4i]` recursively; also if `p+6` is `"BNDL"` (packed form, below) free its children; then `GlobalFree`. |
| `1038:0061 BndlEntry(p, i)` | returns u32 at `p + 8 + 4i` (far ptr after loading). |
| `1038:0088 BndlCount(p)` | returns **u16** at `p+4`. |
| `1038:00a1 BndlSetEntry(p, i, v)` | writes u32 at `p + 8 + 4i`. |
| `1038:082d LoadInto(id, dst)` | `size = ResSize(id)`, `p = GetResource(id)`, copy `size` bytes to `dst`, free **only the top block** (`1030:005d`); members stay allocated and are referenced from `dst`. Returns size. |
| `1038:088a LoadScratch(id)` | `LoadInto(id, scratch)`; scratch = 280 000-byte block (`1038:0022`, ptr `DS:3d8c`); max size seen kept in `DS:3d88`. |
| `1038:0ba8 LockScratch(id)` | frees scratch (`1038:0038`), then points `DS:3d8c` directly at the locked Win resource (no copy; handle in `DS:073a`, released by `1038:0ef8`). Only two callers (relocation scan of the whole EXE): `1088:01a9` (play SSND `id` via the wave player `1088:0014`, wait for a key from a caller list; used by `10e8`, `1130`, `1150`) and `1138:0283` (title narration) — i.e. it is used only for **sounds**, never for BNDL/FFNT. |
| `1038:08eb / 0956 / 09a9 / 093a / 09e4 / 0ad2` | "Packed" form `{u8 'F'(0x46); u8 0; u32 size; data[size]}` built in scratch, plus deep-copy back (bundle members re-allocated using the size table). No callers in the analysed code. |

`1050:0e10` is `hmemcpy(src, dst, n)`; `1000:1344` is `_fstrncmp`.

---

## 2. BNDL format

### 2.1 On disk **[O]** (all 79 resources, e.g. `TMT.DLL/32513/300`)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `"BNDL"` |
| 4 | 4 | `count` u32 (EXE reads the low u16 only, `1038:0088`) |
| 8 | 4·count | member ids, u32 each (EXE uses the low u16, `1038:07d1`); all values < 0x10000 |
| 8+4·count | … | zero padding to 512 |

There is no per-member type, flag, timing or hotspot field; the member type comes from its
id range (§1.1). Observed member types: PMAP (532 refs) and OTHR (41 refs); no nested BNDL,
no SSND/RRGB/FFNT members. Every referenced id exists (PMAPs exist in both TMT16 and TMT256).
Max count = 69 (bundle 300).

### 2.2 In memory after `GetResource` **[O] `1038:076f`, `1038:00c9`**

Block size = 2 × SizeofResource(bundle) (1024 bytes for every shipped bundle):

| Offset | Field |
|---|---|
| 0 | `"BNDL"` |
| 4 | count (u32 as on disk) |
| 8 + 4i | far pointer (offset, selector) to member *i*'s private loaded copy (replaces the id) |
| 8 + 4(count+i) | u32 byte size of member *i* (padded SizeofResource; ×2 if member is a BNDL) |

Capacity: (1024 − 8)/8 = 127 members. Members are loaded eagerly, in order, recursively.
A member shared by two bundles is loaded twice (no caching) **[O]**: 20 PMAP ids are shared
(e.g. 1100–1103, 1119–1132, 2301, 2302, 2312).

Port recommendation **[I]**: represent a loaded bundle as `vector<shared_ptr<Resource>>`
indexed by position; the size table is only needed by the unused deep-copy helpers.

### 2.3 Catalogue (what each bundle is, who loads it) **[O]** unless marked

Holder = DGROUP variable receiving the far pointer. Full member lists: `previews/bndl.json`.

| Bundle | n | Members | Loaded at | Holder / consumer | Role |
|---|---|---|---|---|---|
| 300 | 69 | PMAP 1251–1262, 1136–1147, 1153–1174, 1243–1250, 1175–1180, 1240–1242, 1127–1132 | `1198:0009` | `DS:2db4`; w/h of each entry cached in `DS:5cd6[i]`, `DS:5d80[i]`; drawn by `10e0:0ff4`/`1079` as entry `DS:5cd0` at (`DS:5ccc`, `DS:5cce`) | player-character frames |
| 301 | 10 | PMAP 1300–1309 | `10e8:0008` | `DS:5e54`; entries 6 (502×89 panel) and 9 used by `10e8:0040/00dd` | bottom status panel |
| 302 | 6 | PMAP 1350–1355 | `1198:0085` | `DS:5e80`; drawn by `10f8:0285` as frame `4 − shopCoinsLeft` | net-shop sign (spot content 0xFD) (corrected per ARCHITECTURE.md §7.1 C23; was "UI art") |
| 310 | 9 | PMAP 1600–1608 | `1178:02e1` | `DS:65c6` | thrower elf's projectile: a sparkle that turns into a coin (corrected per ARCHITECTURE.md §7.1 C23; was "scene art") |
| 311–320 | 2–9 | PMAP 1609–1668 (consecutive) | 316,317,312,315,320,314,311 via `1178:0041(slot 0..6, id)` at `1178:0281..02db` (skipped in room `DS:5cca == 0x31` = 49, the castle climb) | per-slot record at `DS:6228 + slot·0x32`, 0x32 bytes: `+0` u16 count, `+2` u16 =1, `+4` u16 slot, `+6` far ptr frame[10], `+0x2e` far ptr of the bundle itself | elf animation sets (`ElfAnimSet`, `code_1168-1198.md` §4.1) (corrected per ARCHITECTURE.md §7.1 C23; was "frame cycles of animated scene objects") |
| 313, 318, 319 | 2, 5, 4 | PMAP 1620–1621, 1651–1659 | **never** (see note below) | – | dead data |
| 321 | 24 | PMAP 1100–1104, 1148–1152, 1119–1132 | `1168:00c3` | `DS:6192` | art set |
| 322 | 4 | PMAP 2180–2183 | `1160:0e97` | `DS:6166` | art set |
| 323 | 67 | OTHR 20300 + PMAP 2300–2365 | `10f0:0093` via `10d8:0000` | script → `DS:0f70`, frames → frame table slots 0.. | DAM animation |
| 324 | 53 | OTHR 20305 + PMAP 2312, 2301–2302, 2400–2448 | `10f0:0196` | script → `DS:0f80` | DAM animation |
| 325 | 8 | OTHR 20308 + PMAP 1119–1125 | `10f0:02a6` | script → `DS:0f84` | DAM animation |
| 326 | 4 | PMAP 1095–1098 | `1168:0182` | `DS:6202` | art set |
| 327 | 33 | OTHR 11500–11532 | `1130:00cb` via `LoadScratch` | entry *n* drawn by `10b0:0023(0x53,0x5c,w=0x141)` | text pages |
| 328 | 4 | PMAP 1380–1383 | `1198:009f` | `DS:2dd0` | art set |
| 330–372 | 2–4 | PMAP 1700–1803 (1783–1785 in no bundle) | `330 + k`, k < 43 (`10f0:00a9`, `1190:013c`, `1198:01ac`; algorithms in §2.4.1) | `1190:009b`: frames → frame table slots `DS:2da8[i] + j`, paired with OTHR DAM script `20000 + k` (`1190:0158`); `1198:0103`: far ptr in `DS:2dbc[i]`; `10f0:00a9`: entry 0 → frame slot 80 | item (treasure) art, 2–4 frames each |
| 373–380 | 3–4 | PMAP 1804–1832 | `330 + k`, 43 ≤ k ≤ 50, only through the random picks of `1198:0103` (§2.4.1) | far ptr in `DS:2dbc[i]` | extra item art; no DAM script (OTHR 20043–20050 exist but are never loaded) |
| 400 | 11 | OTHR 20100 + PMAP 2101–2110 | `1138:000d` via `10d8:0000(slot 0)` | script → `DS:5f13` | title animation |
| 401 | 20 | OTHR 20101 + PMAP 2111–2129 | `1138:0027` via `10d8:0000(slot = frames of 400)` | script → `DS:5f1d` | title animation |
| 402 | 41 | OTHR 20102 + PMAP 2130–2169 | **never** | – | dead data (a third title part?) |
| 410, 412 | 6, 11 | OTHR 20200 + PMAP 1100–1103, 2215; OTHR 20204 + PMAP 1133–1134, 2203–2210 | `1188:0047`, id table `DS:2d6d[k]` | then OTHR script `DS:2d71[k]` (20201/20205) and background PMAP `DS:2d75[k]` (2200/2201) | two-part cutscenes |
| 415 | 3 | PMAP 2217–2219 | `1188:01ed` | `DS:2d79` | art set |

**Unreferenced bundles [O]**: 313, 318, 319 and 402 are loaded nowhere. A raw scan of every
code segment of `TMTWINCD.EXE` (including the ≈14 KB outside Ghidra's functions) finds no
`PUSH imm16` / `MOV r16,imm16` / `ADD r16,imm16` / `CMP` with these values, and DGROUP holds no
u16 copy of them (the only id tables, `DS:2d6d` = {410, 412} and the `330 + k` formulas, cannot
produce them). Likewise OTHR 20043–20050 (DAM scripts for items 43–50) and 20102 (script of
402) are never requested. A port may ignore them. (Scratch: `extracted/scratch/bndl_ffnt2/imm.py`, `dg.py`.)

### 2.4 Usage patterns (semantics) **[O]**

1. **Indexed art set** — code keeps the bundle pointer and calls `BndlEntry(b, i)` with a
   constant or state index, then draws the PMAP with `1058:1320(x, y, pmap, mode)` (PMAP
   in memory starts with u16 width, u16 height: `1058:0bbc`, `1058:0bd4`).
2. **Frame cycle** (`1178:0041`) — all entries copied into a per-object frame array.
3. **DAM animation bundle** (`10d8:0000(id, slotBase, &bundle, &script)`) — entry 0 must
   be an OTHR "DAM" script; entries 1..n−1 are written to the **global frame table**
   `DS:5152 + 4·(slotBase + i − 1)` (far ptrs; 100 slots up to `DS:52e2`); returns
   n−1 (frame count). Several bundles can be stacked (401 is placed after 400's frames).
   Slot 80 (`DS:5292`) is set to entry 0 of the current item bundle (`10f0:00d8`) so a
   cutscene script can draw the found treasure. The table is exactly 100 far pointers:
   `DS:52e2 − DS:5152 = 0x190 = 400` bytes, and `DS:52e2` starts a different structure
   (byte flag + 4 words, used by `10b8:02ae`/`0874`). Slot map **[O]**: animation bundles
   from slot 0 upward (323 alone fills 0..65); item bundles at 20, 30, 40, 50, 60, 70
   (`DS:2da8` = u16 {20,30,40,50,60,70}); slot 80 = current treasure.
4. **Text-page bundle** (327) — entries are NUL-terminated strings for the text engine (§4–§5).

There are no hotspots/offsets/timing in BNDL itself; positions and timing come from the
DAM script and the calling code.

### 2.4.1 Item bundle selection **[O]**

`DS:65ce` (u16) is `g_climbsDone`, the number of completed climbs (= treasures won; ARCHITECTURE.md
§7.1 C5). `DS:5cca` (u8) is the current **room** number, not the level; `DS:23f2 + 0x1c·room` and
`DS:23f3 + 0x1c·room` are the room's `RoomDef` bytes +0x1a (mountain level) and +0x1b (music zone)
(corrected per ARCHITECTURE.md §7.1 C17).

- `1190:009b` (called from `1118:0000`), only when byte `DS:23f3 + 0x1c·room` == 0 (music zone 0,
  the treasure hall): room 0: `k[0] = DS:65ce − 1`, `k[1..5] = −1`; other hall rooms r:
  `k[i] = DS:65ce − 2 − 6·(r − 1) − i` for i = 0..5. Any `k ≥ 43` is reduced mod 43
  (signed byte arithmetic). For each `k[i] ≥ 0`: bundle `330 + k[i]` → far ptr `DS:65d0[i]`,
  OTHR `20000 + k[i]` (DAM script) → `DS:65e8[i]`, and bundle entry *j* → frame-table slot
  `DS:2da8[i] + j`.
- `1198:0103` (called from `1118:0000`), only when byte `DS:23f2 + 0x1c·room` < 3 (mountain
  levels 0–2): `base = DS:65ce mod 43`; for i = 0 .. n−1 (n = `DS:0ec3[g_rankTier]` = 2/3/4/5:
  `DS:5e58` is the u8 rank tier used as an index, not a near pointer; corrected per ARCHITECTURE.md
  §7.1 C17):
  `k = base` if i = 0 and byte `DS:5e61` == 0, otherwise `k = base + rand()·(51 − base) / 32768`
  (`1000:0859`, [I] Borland `rand()` 0..0x7fff), redrawn until distinct from the earlier picks;
  bundle `330 + k` → far ptr `DS:2dbc[i]`; then `1108:0000`. This is the only path that can
  reach bundles 373–380.
- `10f0:009e`: bundle `330 + (DS:65ce − 1) mod 43` → `DS:0f78`, entry 0 → frame slot 80.

### 2.5 DAM animation scripts (the OTHR entry 0 of animation bundles) **[O] `10b8:0312`**

All 63 OTHR resources starting with `"DAM\x1a"` (20000–20050, 20100–20102, 20200/01/04/05,
20300/01/02/05/08) parse cleanly with this opcode set and end exactly at `FF`:

| Byte | Operands | Meaning |
|---|---|---|
| `FD` | `s16 dx, s16 dy, u8 frame, u8 flag` | queue a cel: image = frameTable[actor.frameBase + frame], at (actor.x+dx, actor.y+dy); `flag` copied to the draw record (always 0 in data) |
| `FE` | – | end of this tick (resume here next tick) |
| `FF` | – | end of script: call actor completion callback (if any), actor stops |
| `FA` | `s16 target, u8 c` | loop: counter k = c>>7 (two per actor, init −1); if counter == −1 set it to c&0x7F; decrement; if now 0 → set −1 and fall through, else jump to script start + target (target counts from the 4-byte header, so 4 = first opcode), so the body runs c&0x7F times in total; c&0x7F == 0 ⇒ loop forever (all 52 `FA` in data are `FA 04 00 00` = repeat whole script forever) |
| `FC` | `u8 pad, u8 n` | play sound SSND 7000+n (`1170:0000`; `1170:0031` adds 0x1b58) |
| `FB` | – | stop sound (`1170:005b`) |
| `F9` | `s16 x, u8 y, s16 w, u8 h` | append rect (actor.x+x, (actor.y+y)&0xFF, w+2, h+2) to list `DS:5bde` (unused in data) |
| other | – | ignored (1 byte) |

A header beginning `"DOP"` sets a per-draw flag (`DS:5667`); no shipped script uses it.
Actor table: 16 × 0x1d bytes at `DS:4f82` (`10b8:0119` allocates). Player
`10d8:0082(script, frameBase, …, skippable, escCallback, x, y, fps)`: timer slot 7 period
`1000/fps` ms; Enter/Space ends early if `skippable`. Observed fps: 4 (250 ms, cutscenes
`10f0`), 7 (142 ms, `1188`); the title (`1138:043d`) and in-game loops use 100 ms ticks.
(The full sprite engine belongs to the sprite/animation spec.)

---

## 3. FFNT font format (`TMT.DLL` 32514 / 999, 8704 bytes incl. padding)

### 3.1 Layout **[O] `1068:0073` (select font) and `1060:01ef` (draw glyph)**

| Offset | Size | Field | Value in id 999 |
|---|---|---|---|
| 0 | 1 | unused by EXE ([I] Mac "first char") | 0x00 |
| 1 | 1 | `last` → table length N = last+1 | 0xFF → N = 256 |
| 2 | 1 | `height` (rows per glyph = line height) | 17 |
| 3 | N | `width[c]` advance in pixels (= glyph bitmap width) | 2..46 |
| 3+N | N | `offLo[c]` | |
| 3+2N | N | `offHi[c]` | |
| 3+3N | … | bitmap pool (base 0x303) | 7718 bytes used |

Glyph *c* (tables indexed **directly** by the byte value; byte 0 is not subtracted):
bitmap at `base + (offHi[c] << 8 | offLo[c])`, `height` rows, each row
`ceil(width[c]/8)` bytes, row-major, **MSB = leftmost pixel**, bit 1 = paint, bit 0 =
transparent. Glyphs are stored consecutively in code order. Inter-character spacing is
built into the width (blank right-hand columns); there is **no kerning, no baseline offset**,
the cell origin is the top-left; line height = `height` (17).

Metrics **[O]**: space (0x20) = 5 px; digits 0 and 2–9 = 10 px each, but '1' = 6 px (corrected:
an earlier revision said all digits are 10 px; checked by `tests/test_text_ui.cpp`); '_' = 10 px; the 43 empty
codes (0x00–0x1F except 0x06/0x17, 0x7F, 0xB0, 0xB8, 0xB9, 0xBD, 0xC3–0xC6, 0xCA, 0xD7, 0xF0)
are 2 px wide (space 5 px); 213 codes have ink; widest = 46 (0x8B, 0x91). 0xFE (˛, 5 px) and
0xFF (ˇ) have glyphs but are control codes for the layout engine, so they are only reachable
inside a substitution string; TAB (2 px) and 0xFE still add their widths inside `Measure`.

### 3.2 Character map **[O]** (sheet: `previews/font/ffnt999_sheet.png`)

0x20–0x7E ASCII; 0x96–0xFF follow **Mac Roman** (ñ ó ò … † ° ¢ £ § • ¶ ß ® © ™ … « » …
À Ã Õ Œ œ – — … ‡ · ‰ Â Ê Á Ë È Í Î Ï Ì Ó Ô Ò Ú Û Ù ı ˆ ˜ ¯ ˘ ˙ ˚ ¸ ˝ ˛ ˇ), some Mac Roman
codes left blank (e.g. 0xB0, 0xB8, 0xB9, 0xC3–0xC6, 0xF0). Replaced by keyboard-key icons:

| Code | Icon | Code | Icon |
|---|---|---|---|
| 0x06 | boxed "R" key | 0x17 | boxed "S" key |
| 0x80 / 0x81 / 0x82 / 0x83 | ← / → / ↑ / ↓ keys | 0x84+0x85 | "Spacebar" (two halves) |
| 0x86, 0x92 | "Esc" | 0x87+0x88 | "return" |
| 0x89+0x8A | "backspace" | 0x8B, 0x91 | "↵Enter" |
| 0x8C | "Ctrl" | 0x8D, 0x90 | "Tab" |
| 0x8E+0x8F | "Spacebar" (variant) | 0x93 | "Delete" |
| 0x94+0x95 | "Return" | 0x99, 0xB6 | ⌘ key |
| 0xB7 | boxed "R" | | |

Game text uses these directly, e.g. `DS:0b24` "`\x86` to Go Back", `DS:230a` "press `\x8b`",
OTHR 11605 help page (`\x80` or `\x81`, `\x84\x85`, `\x8b`). A port should keep text as raw
bytes and index the font by byte, never transcode to Unicode before rendering.

### 3.3 Font runtime state & API **[O]**

| Address | Meaning |
|---|---|
| `DS:4aa2 + 4·slot` | far ptr of loaded font (3 slots; only slot 0 used) |
| `DS:4aae` | current slot; `DS:4ab0` height; `DS:4ab2` font selector; `DS:4ab4/4ab6/4ab8/4aba` offsets of width / offLo / offHi / bitmap |
| `DS:4293` | current logical text colour 0..15 (`1048:03bb` returns it) |
| `DS:4191` | current device pen (byte palette index) |
| `DS:429e`, `DS:099c` | destination buffer selector; row pitch = 512 (u32) |

- `1068:0024` (startup, from `1010:0000`): `1068:0000(999, 0)` = `ResLoadCopy("FFNT"@DS:09a8, 999)` into slot 0, then `1068:0073(0)` select. `1068:006b` frees slot 0 at exit.
- `1068:00d9()` → height. `1068:00dd(c)` → `width[c]` (via `1060:02e4`).
- `1068:00f6(x, y, c)` → draws glyph, returns `width[c]`: 256-colour mode (`DS:086c≠0`) →
  `1060:01ef`, else `1060:0000`.
- `1060:01ef` (8 bpp): dest = `y·pitch + x` (32-bit offset); for each row, for each of
  `width` bits: if set, `dst = pen` (low byte of `DS:4191`).
- `1060:0000` (4 bpp): pitch = (`DS:099c`+1)>>1 = 256 bytes, dest = `y·256 + x>>1`; even x:
  first pixel in the **high nibble** (`AND 0x0F; OR pen<<4`), odd x: low nibble first.
- **No clipping** in either routine; callers keep text inside the 512×384 buffer.
- Loop mechanics **[O] `1060:024d–02d7`**: per row, `CX = width`, one `LODSB` per 8 pixels,
  `SHL AL,1` / `JNC` / store / `INC EDI` / `LOOP`; when CX runs out mid-byte the rest of the
  byte is discarded (hence row stride = ceil(width/8)); then `EDI = rowStart + pitch`,
  `DEC DX` (DX = height). A glyph of width 0 or a font of height 0 would wrap to 65 536
  iterations in the EXE (`LOOP`/`DEC` from 0); id 999 has none, but a port should treat
  width 0 as "draw nothing".
- Returns `width[c]` in AX (`1060:02db MOV AX,BX`), i.e. the advance.

### 3.4 Colours **[O] `1048:02fd(c)`**

`DS:4293 = c`; pen = 256-colour: `GetNearestPaletteIndex(palette, PALETTERGB(R,G,B))` with
(R,G,B) = the three u16 at `DS:07d8 + 6c` (low bytes); 16-colour: u16 at `DS:0838 + 2c`.
Table contents (standard EGA colours; index = c):

| c | RGB | 16-col idx | c | RGB | 16-col idx |
|---|---|---|---|---|---|
| 0 | 0,0,0 | 0 | 8 | 85,85,85 | 7 |
| 1 | 0,0,170 | 4 | 9 | 85,85,255 | 12 |
| 2 | 0,170,0 | 2 | 10 | 85,255,85 | 10 |
| 3 | 0,170,170 | 6 | 11 | 85,255,255 | 14 |
| 4 | 170,0,0 | 1 | 12 | 255,85,85 | 9 |
| 5 | 170,0,170 | 5 | 13 | 255,85,255 | 13 |
| 6 | 170,85,0 | 1 | 14 | 255,255,85 | 11 |
| 7 | 192,192,192 | 8 | 15 | 255,255,255 | 15 |

The text engine does not reset the colour at the start of a string: callers call
`1048:02fd` first (usually 0 = black, 15 = white on coloured panels, e.g. `10e8:00ff`).

---

## 4. Text strings and control codes

Strings are NUL-terminated byte strings (DGROUP literals, OTHR text resources, bundle 327
entries, `sprintf` results). Byte meanings inside the layout engine **[O] `10b0:00b5`**:

| Byte(s) | Meaning |
|---|---|
| `00` | end of string |
| `09` TAB | jump to the first tab stop `x0 + tab[k]` that is `> curX` (default tab[k] = 40·(k+1), k = 0..7); no change if none |
| `0A` or `0D` | new line: `curY += height`; `curX = x0`, or the centred start if centring is on |
| `20` space | word-wrap point (algorithm in §5) |
| `FE` | carriage return without line feed: `curX = x0` (used to build two-column layouts by overprinting with runs of spaces, OTHR 11605) |
| `FF 01 n FF` | substitute string `subst[n−1]` (caller-supplied table of far char*) |
| `FF 02 FF` | toggle underline |
| `FF 03 n FF` | set colour `n−1` (`1048:02fd`) |
| `FF 04 b0 b1 … FF` | set up to 8 tab stops (quirky, see §5.4) |
| `FF 05 FF` | toggle centring; when switched on, the current line is re-centred immediately |
| `FF 06 … FF` | ignored by the renderer (payload skipped) |
| `FF 00` / `FF 07..FF` | only the sub-code byte is consumed; nothing else happens |
| anything else | glyph |

All `FF` escapes except `00`/`07+` are closed by the next `FF` byte (everything in between
after the operand is skipped).

Codes found in shipped text **[O]** (census over OTHR text and DGROUP): `FF 02 FF` (301×),
`FF 03 01 FF` (black, 831×), `FF 03 05 FF` (dark red, 471×), `FF 05 FF` (309×),
`FF 06 "L=1".."L=4" FF` (359×, puzzle level tags read by puzzle code, invisible to the
renderer). `FF 01` and `FF 04` never occur; player names are inserted with `sprintf("%s")`
(e.g. `DS:2106` `FF 03 05 FF %s FF 03 01 FF is already…`).

**`~NNNNNN`, `|NN`, `\tNN` codes**: these exist only in the **DOS** version. **[O]** A scan
of every Windows resource (`extracted/res/**`) and of DGROUP finds no `~` followed by digits,
no `|` followed by digits (the only two `|` hits are bytes inside the FFNT bitmap) and no
leading-digit position prefixes; DOS `HELP.DAT` has 35 `~XXXYYY` codes and `SST.EXE` 30 plus
two `|NN`. The Windows text replaced them with the `FF`-escapes above, so a Windows port
needs none of them. For reference, the DOS renderer (`43cb:0b63` in
`re/dos/ghidra_export`, `DrawText(x, y, char far *s)`, returns the line count) does **[O]**:

| DOS code | Effect |
|---|---|
| CR / LF | `x = x0`, `y += DS:5b3c` (line height), lines++ |
| `~XXXYYY` | `x = x0 = XXX / 2`, `y = YYY` (3 decimal digits each; 7 bytes consumed) |
| `` ` `` | toggle highlight colour (0x4444 ↔ default `DS:9215`) |
| `\|NN` | colour = NN (2 decimal digits), replicated into both nibbles (`(c<<4) + c` for c < 16) |
| TAB `NN` | `x += NN` (2 decimal digits) |
| other | drawn (`c & 0x7F`) via `1000:2180` only while `x < 154` (0x9a), `x +=` advance |

---

## 5. Layout engine

### 5.1 Entry points (all far cdecl) **[O]**

| Function | Signature | Notes |
|---|---|---|
| `10b0:0000` | `DrawString(int x, int y, char far *s)` | box width 512, no subst, no flush |
| `10b0:0023` | `DrawTextBox(int x, int y, int w, char far *s, char far * far *subst, int flush)` | |
| `10b0:0050` | `MeasureText(char far *s, char far * far *subst, int w, int far *outW, int far *outH)` | layout without drawing |
| `10b0:0080` | `int TextWidth(char far *s)` | 0 for empty string; else `MeasureText(s, 0, 512, &w, 0)` |
| `10b0:00b5` (near) | `Layout(x0, y0, w, s, subst, flush, outW*, outH*, draw)` | core |

Typical calls: `10e8:0125` `DrawTextBox(10, 310, 190, DS:0f17, 0, 0)` (centred hint text,
white on colour-9 box); `1130:0110` `DrawTextBox(0x53, 0x5c, 0x141, bundle327[n], 0, 0)`;
`10e8:01db` `DrawString(0x11, 0x13b + 19·i, …)` (manual 19-px line spacing).

### 5.2 Core algorithm **[O] `10b0:00b5`** (pseudocode in our own words)

```
state: cx = x0, cy = y0, underline = off, centre = off,
       lineH = font.height, spaceW = width[' '], right = x0 + w - 1,
       minX = x0 + w - 1, maxX = x0, tab[k] = 40*(k+1)
put(c): if drawing:
            if underline and c == '_': adv = width[c]          // '_' not drawn
            else: adv = drawGlyph(cx, cy, c)
            if underline: fillRect(cx, cy + lineH - 1, adv, 1, colour)
            if flush: presentRect(cx, cy, adv, lineH)          // 1058:05bc
        else adv = width[c]
        minX = min(minX, cx) (before), cx += adv, maxX = max(maxX, cx)
centreStart(p) = x0 + trunc((w - Measure(LF, p)) / 2)          // C division, toward 0
loop: c = next byte
  0      -> stop
  TAB    -> first k with x0+tab[k] > cx: cx = x0+tab[k]
  LF, CR -> cx = centre ? centreStart(after c) : x0;  cy += lineH
  FE     -> cx = x0
  ' '    -> need = Measure(' ', after c) + spaceW
            if cx + need > right:            // wrap: the space is not drawn
                 skip following ' ' and TAB bytes; cx = x0; cy += lineH
            else put(' ')
  FF     -> sub = next byte; handle per §4 (05: skip to FF, toggle, if on cx = centreStart(here))
  other  -> put(c)
  after every byte: busy-wait until timer slot 6 (DS:4f7a) expires  // never armed: no-op (see note)
outW = maxX - minX;  outH = cy - y0 + lineH
```

Timer note **[O]**: deadlines live in `DS:4f62 + 4·slot` (u32 `timeGetTime()` ms; `10a8:002a(slot,
ms)` arms, `10a8:0049(slot)` busy-waits, `10a8:0066(slot)` polls). The table is zero in the
file image, only `10a8:0041` writes it, and a relocation scan of the whole EXE finds exactly 11
calls of `10a8:002a`, all with slot 0, 4 or 7 (`10d8:00c7/00ed`, `10e0:012b`, `10f0:073d`,
`1138:02e7/0453/0460`, `1160:033b/0389`, `1168:04ea/0b0a`). Slot 6 therefore stays 0 and the
per-character wait (`10b0:025f`, `10b0:04d3`) returns immediately: text appears instantly,
there is no typewriter effect. A port omits it.

Notes **[O]**: word-wrap lines are **not** re-centred (wrap sets `cx = x0` even in centre
mode); a centred line wider than the box starts left of `x0`; tabs and `FE` do not touch
minX/maxX; if nothing is drawn `outW = 1 − w`.

### 5.3 `Measure(delim, p, subst, inSub, subPtr)` **[O] `10b0:053c`**

Sums `width[c]` from `p` until `c == delim` or `c ∈ {00, 0A, 0D}` (TAB, `FE` and glyphs all
add their width). `FF 01 n …FF` switches to scanning `subst[n−1]` (which ends at its NUL, then
scanning resumes in the main text; it stops on delim/LF/CR inside the substitution); any
other `FF sub … FF` contributes 0. With `inSub` set, scanning starts at `subPtr`.

### 5.4 Substitution and other quirks **[O]**

- `FF 01 n FF` rendering (`10b0:016d`): the characters of `subst[n−1]` are drawn one by one;
  only space is special (wrap test as above, skipping spaces/TABs inside the substitution);
  TAB/LF/CR/FF inside a substitution are drawn as glyphs. Afterwards the main text is
  skipped to after the closing `FF`. If `flush`, the timer-6 wait runs per character.
- **Measure bug**: the wrap test inside a substitution calls `Measure` with the main-text
  pointer still on the *closing* `FF`, so when the substitution ends without a space the
  closing `FF` is taken as a new escape and the following byte as its sub-code, swallowing
  text up to the next `FF` (unbounded). Harmless in practice because `FF 01` is never used.
  `ffnt.py draw_text(exe_quirks=False)` implements the obvious fix.
- `FF 04` (`10b0:02d7`): reads a **u16 at every byte position** while advancing one byte
  (tab[k] = b[k] | b[k+1]<<8), stopping at `FF` or 8 entries. Unused in data.
- The renderer never clips; out-of-box text simply overflows.

### 5.5 Reference implementation

`ffnt.py draw_text / measure_text / text_width` implement §5 exactly (verified visually on
OTHR 11605 — two-column key help, underlined headings — and OTHR 11000 — centred puzzle text
with red highlights; previews `othr11605_layout.png`, `othr11000_layout.png`).
`sample_line.png` exercises centring, key icons (0x8B, 0x84+0x85), colour 5 (`FF 03 05 FF`),
underline, word wrap in a 300-px box, TAB stops and the `FE` overprint.

---

## 6. Does the game use Windows GDI fonts?

**Not for in-game text.** **[O]** The EXE imports no `TextOut`, `ExtTextOut`, `DrawText`,
`CreateFont*`, `GetTextExtent` (NE import relocations of the whole EXE use GDI ordinals
34, 45, 48, 52, 66, 68, 69, 80, 87, 93, 360, 364, 370, 375 only). The single text-related call
is `GetTextMetrics` (GDI.93) at `1010:01d6`, inside the **WM_CREATE** handler `1010:01c5` of
the main window procedure `1010:0196` (handler code is outside Ghidra's function; reached via
the CS-resident message table at `1010:0574`: 21 message ids followed by 21 handler offsets).
It does `hdc = GetDC(hwnd)`, `GetTextMetrics(hdc, &tm)` (system font), stores
`tmAveCharWidth → DS:09a0`, `tmMaxCharWidth → DS:09a2`, `tmHeight + tmExternalLeading →
DS:09a4`, `tmHeight → DS:09a6`, then `ReleaseDC`. A raw scan of all code finds **no reader**
of `DS:09a0..09a6`, so this is dead template code. All in-game text goes through the FFNT engine ([I] including the in-game Yes/No message
panels `DS:142c`, `DS:14f0`, …, since nothing else can draw text) (the only callers of `1068:00f6` /
`1068:00dd` are in `10b0`). Windows-drawn text is limited to:

- the 8 `DIALOG` resources of the EXE (startup/system checks such as
  `NOT_ENOUGH_MEMORY_DIALOG`, `TooMuchColor`, …): 7 use **MS Sans Serif 8 pt**, one uses
  the system font;
- the window menu bar built by `1078:0000` with `CreateMenu/CreatePopupMenu/AppendMenu` from
  `\n`-separated DGROUP strings (`DS:1298` "&Help", `DS:12a0` items, `DS:1344` "&Options", …);
- `MessageBox` errors ("You are out of memory.", "Write Error").

---

## 7. Port checklist

1. Resource lookup by id → type by range (§1.1), DLL by range; BNDL/FFNT/OTHR from `TMT.DLL`.
2. BNDL: parse `count` + ids; load members eagerly; expose `entry(i)`.
3. Animation bundles: entry 0 = DAM script, entries 1.. → frame-table slots; run scripts at
   1000/fps ms ticks (§2.5).
4. FFNT: decode tables; glyph draw = 1-bit mask in the current colour, no clipping needed
   if callers behave; advance = width.
5. Text: implement §5 byte-exactly (wrap rule `cx + spaceW + nextWordW > x0 + w − 1`,
   centring `trunc((w − lineW)/2)`, underline row = `y + 16`).
6. Colours from the 16-entry EGA table, mapped to the nearest palette index in 256-colour mode.

## 8. Evidence index

`1038:0022/0038/0061/0088/00a1/00c9/038a/03da/076f/082d/088a/08c5/08eb/0956/09a9/09e4/0ad2/0ba8/0ef8`,
`1030:0000/005d/0082`, `10d8:0000/0082`, `10b8:0119/020a/024e/0312`, `1178:0041`,
`1190:009b`, `10f0:0000`, `1138:0000/007a/043d`, `1188:0000/01ed`, `1198:0000`, `10e8:0000/0040/00dd/019f`,
`1068:0000/0024/0039/0073/00d9/00dd/00f6`, `1060:0000/01ef/02e4`, `10b0:0000–06c5`
(jump tables at `10b0:0518` (FF sub-codes 1..6 → `016d, 028d, 02ae, 02d7, 0326, 037d`) and
`10b0:0524` (codes `09 0A 0D 20 FE FF` → `0393, 03cf, 03cf, 0418, 0412, 014e`)),
`1048:02fd/03bb`, `1058:090f/05bc/0bbc/0bd4/1320`, `10a8:002a/0041/0049/0066`, `1078:0000`,
`1190:009b`, `1198:0103`, `1178:0251`, `1088:01a9`, `1010:0196/01c5` (WM_CREATE,
`GetTextMetrics` at `1010:01d6`), DGROUP `2da8` (item frame bases), `2d6d` (cutscene id
table), `4f62` (timer table), `09a0–09a6` (unused text metrics). DOS: `43cb:0b63`.
Scratch scripts for the whole-EXE scans: `extracted/scratch/bndl_ffnt2/reloc.py` (internal
relocations → callers, incl. uncovered code), `imp.py` (import relocations), `imm.py`
(immediate operands), `dg.py` (DGROUP u16 search).

## 9. Tools

- `python -P tools/assets/bndl.py extracted/res` — one line per bundle;
  `--json extracted/previews/bndl.json` (members, types, PMAP sizes, decoded DAM scripts,
  EXE call sites); `--id N` recursive tree; `--dam OTHRFILE` decode one script.
  API: `parse_bndl`, `type_for_id`, `dll_for_id`, `resource_path`, `load_bundle_tree`, `parse_dam`.
- `python -P tools/assets/ffnt.py info|sheet|text|file …` — metrics, glyph sheet, render text
  with escapes (`--text '\xff\x05\xffHello'`), render a raw text resource.
  API: `Font`, `draw_text`, `measure_text`, `measure_word`, `text_width`, `Canvas`, `EGA_RGB`.

## Open questions

Resolved in this revision (kept here for traceability): bundles 373–380 are reachable only via
the random picks of `1198:0103` (§2.4.1); 313/318/319/402 are dead data (§2.3 note); timer
slot 6 is never armed (§5.2 timer note); `1038:0ba8` is used only for SSND playback (§1.3);
the frame table is exactly 100 slots (§2.4); `GetTextMetrics` is dead template code (§6).

1. Semantics of the game variables that select item bundles — `DS:65ce` (item index),
   `DS:5e61` (forces a random first pick), byte `[DS:5e58] + 0xec3` (number of picks), and the
   per-level record bytes `DS:23f2/23f3 + 0x1c·level` — belong to the game-logic spec; only
   their arithmetic is documented here.
2. FFNT byte 0 (0x00) is ignored by the EXE; it is probably the Mac `firstChar` (matters only
   for other fonts; none shipped).
3. The `flag` byte of DAM `FD` cels and the `"DOP"` header variant are never non-zero/used in
   data; their draw-mode meaning lives in the sprite renderer (`1058`/`10b8:06e4`) — to be
   confirmed by the sprite-engine spec.
4. Meaning of DAM `F9` rectangles (`DS:5bde` list) — unused in data; consumer not traced.
5. Purpose of the structure at `DS:52e2` that directly follows the frame table (written by
   `10b8:0874/0888`, read by `10b8:02ae–02f4`) — sprite-engine spec.
6. Whether the original renders identically in 16-colour mode for colours that share a
   16-colour index (c = 4 and 6 both map to index 1, §3.4) was not checked in the emulator.
