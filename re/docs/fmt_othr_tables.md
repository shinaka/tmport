# OTHR tables 10550–10555 and 20000–20308 (TMT.DLL, NE type 32515)

Scope: the OTHR (`Mac 'OTHR'` → Win type 32515) resources of `TMT.DLL` with ids
10550, 10551, 10552, 10553, 10555 and 20000–20308. (There is no 10554.) Evidence is the Ghidra
export of `TMTWINCD.EXE` (`re/win/ghidra_export`), the raw resources in
`extracted/res/TMT.DLL/32515/<id>`, and decoded previews (§7).

Tags: **[O]** = observed in bytes or code; **[I]** = inferred. Code addresses are `SSSS:OOOO`
(Ghidra selectors). DGROUP offsets are written `DS:XXXX` (file offset `0x23c00 + XXXX` in
`TMTWINCD.EXE`). All multi-byte values are **little-endian**. Every NE payload is padded with
zeros to a multiple of 512 bytes, so a resource file is longer than its data.

None of these resources holds walkability, collision, ladder or path data. They are:

| Id(s) | Size used | What it is | Loaded by |
|---|---|---|---|
| 10555 | 448 B | **Hiding-place picture table**: 112 × `{count, size, shape, noun}`. Entry *v* describes picture PMAP 1400+*v* | `1100:06d4` (`PUSH 0x293b`) |
| 10550–10553 | 19 002 B each | **Hiding-place layouts**, one table per difficulty 0–3. Each has 500 records of 38 bytes. A record fills the 18 hiding places of one mountain level and says which picture hides the key | `1100:04fa` (`ADD AX,0x2936`) |
| 20000–20050 | 41–81 B | **DAM animation scripts**, one per treasure *t* = id−20000. They animate treasure *t* (cels: BNDL 330+*t*) in the castle treasure rooms | `1190:0158` (`ADD AX,0x4e20`) |
| 20100, 20101, 20102 | 97 / 157 / 325 B | DAM scripts: member 0 of BNDL 400/401/402 (title/intro). 20102 is not used | `1138:0000` via `10d8:0000` |
| 20105–20108 | 226–320 B | **Intro story pages 1–4**: plain text | `1138:017a` (`LEA AX,[SI+0x4e88]`) |
| 20200, 20201, 20204, 20205 | 84–284 B | DAM scripts for the level-transition cutscenes (BNDL 410/412 and DGROUP id table `DS:2d6d`) | `1188:0000` |
| 20300, 20301, 20302, 20305, 20308 | 96–1579 B | DAM scripts for the "treasure delivered to the castle" cutscene (BNDL 323/324/325, plus direct ids `0x4f4d`/`0x4f4e`) | `10f0:0000` |

---

## 1. Loader conventions [O]

* `1038:088a(id)` = `Res_LoadToScratch`. It copies the whole resource into the shared scratch
  buffer at far pointer `DS:3d8c` (memcpy `1050:0e10`), frees the original and returns the size.
  It also keeps the maximum size in `DS:3d88`. **One scratch buffer is shared by everything**: 10550+d is
  read completely before 10555 is loaded over it (`1100:04b5` → `1100:06cc`).
* `1038:08c5(id)` → `1038:076f` loads a resource and returns a far pointer to it. If the data starts with
  `"BNDL"` (compared with `DS:0794`), every member id is loaded recursively and the member slot is
  replaced in place by the member's far pointer. `1038:0061(b,i)` returns member *i* (`b + 8 + 4i`, 32 bits)
  and `1038:0088(b)` returns the count (`b + 4`, 16 bits).
* The resource id selects the type and DLL (`1038:038a`; see `fmt_bndl_ffnt.md`). Ids ≥ 10000 are OTHR in
  TMT.DLL; ids 1000–3999 are PMAP in TMT256.DLL or TMT16.DLL; 7000–7099 are SSND in TM0SOUND.DLL; 7100–7999 are
  SSND in TM1SOUND.DLL.
* RNG: `1000:0859` is Borland `rand()`: `seed = seed*0x015A4E35 + 1; return (seed>>16) & 0x7FFF`.
  `1000:0842` is `srand()`, seeded from `timeGetTime()` at `1118:0000`.

---

## 2. OTHR 10555 — hiding-place picture table

### 2.1 Layout [O]

```
offset 4*v + 0  u8 count   1..7
       4*v + 1  u8 size    1..4
       4*v + 2  u8 shape   0..4     (0 = picture has no shape attribute; used only by v = 61..66)
       4*v + 3  u8 noun    1..15
v = 0..111 (448 bytes), then zero padding to 512.
```

There is no header and no count field. The code indexes the table directly with a picture id, 0..111
(`1100:06cc`: `p = DS:3d8c + id*4`). A loader can stop at the first entry whose noun is 0.
Entries are sorted by noun, then roughly by shape, size and count.

### 2.2 Meaning [O]

Entry *v* describes hiding-place **picture PMAP 1400+*v*** (TMT256.DLL; TMT16.DLL has the same ids, and all
112 are 118×133). The picture is loaded by `1100:0638`: `1038:08c5(obj + 0x578)`. It shows *count* copies
of *noun*, with the given size and shape. I verified this visually on all 112 pictures:
`extracted/previews/othr_maps/objects_10555.png` shows each picture labelled with its tuple and words.

The code value is an index into five tables of far pointers to strings in DGROUP. The pointers are
**filled by NE relocations**: the file holds placeholders `FF FF 00 00` (offset 0xFFFF, segment 0), and
segment 56's internal far-pointer fixups (type 3) point them at strings in `DS:1141..DS:127d`. `1100:0793` reads them:

| Field | Mask bit | Word table (4 B/entry, index = code) | Words for codes 1.. (index 0 → empty string) | Speech SSND id (`1100:08ae` + `10e8:01ef`) |
|---|---|---|---|---|
| count | 8 | `DS:10f9` (8 entries) | "one" … "seven" | 7300 + 0x25 + code = 7338–7344 |
| size | 4 | `DS:1119` (5) | big, small, tall, short | 7300 + 0x2c + code = 7345–7348 |
| shape | 2 | `DS:112d` (5) | round, triangle, square, rectangle | 7300 + 0x30 + code = 7349–7352 |
| noun (count = 1) | 1 | `DS:1079` (16) | tree … woodpecker hole (15 nouns) | 7300 + 7 + code = 7308–7322 |
| noun (count > 1) | 1 | `DS:10b9` (16) | plural forms | 7300 + 0x16 + code = 7323–7337 |

(Base 7300 = `0x1c84`, added in `10e8:01ef`. All 45 clips 7308–7352 exist in TM1SOUND.DLL and 7353 does not.)
Words are given here only to identify the codes. An engine reads them from the EXE, as
`othr_tables.ExeData.clue_words()` does.

---

## 3. OTHR 10550–10553 — hiding-place layouts ("where is the key")

### 3.1 File layout [O]

```
+0      u16 n                     = 500 in all four files
+2      record[n], 38 (0x26) bytes each   → data ends at 2 + 38n = 19002 (0x4a3a); rest zero
record:
 +0x00  u8 mask      which 3 of the 4 properties form the clue (bits 8=count 4=size 2=shape 1=noun)
 +0x01  u8 target    picture id (0..111) of the place that hides the key
 +0x02  u8 obj[18]   picture id shown at hiding place k (k = 0..17)
 +0x14  u8 cont[18]  content of hiding place k: 0, 1, 2 = treasure, 3 = key
```

The resource id is `10550 + difficulty` (`1100:04fa`, `DS:5e58`). Difficulty = `rank / 2` (`10e8:0790`).
Rank comes from `10e8:07a6`: the largest *r* with `points ≥ T[r]`, where `T = DS:0ea0` is u32
{0, 5, 25, 70, 115, 170, 230, 300} and points = `DS:5e62`.

Worked example: record 0 of 10550 (file offset 2):
`0b 35 | 0a 06 01 5b 48 2b 40 42 47 5a 17 29 35 20 23 62 69 61 | 01 01 00 02 02 00 00 00 01 01 00 00 03 00 00 00 01 00`
→ mask 0x0b (count, shape, noun); target 0x35 = 53 = (2, tall, rectangle, sign). The key is at k = 12,
where obj[12] = 0x35 and cont[12] = 3.

### 3.2 Invariants (hold for all 2000 records) [O]

Checked by `othr_tables.check_record`. The JSON reports 0 failures.

1. `mask` is always 0x0b, 0x0d or 0x07. The clue always has 3 properties and **always includes the noun**.
   Across the four files, 0x0b occurs 222–255 times, 0x0d 141–150 times and 0x07 104–129 times.
2. Exactly one `cont[k] == 3`, and there `obj[k] == target`.
3. Among the 18 pictures, **only the target** matches the target's values in all three masked
   properties. The clue therefore identifies the place uniquely; the puzzles were generated offline.
4. Every record has the same content counts. They depend only on difficulty:

   | file | difficulty | #0 | #1 | #2 (treasures) | #3 (key) |
   |---|---|---|---|---|---|
   | 10550 | 0 | 10 | 5 | 2 | 1 |
   | 10551 | 1 | 8 | 6 | 3 | 1 |
   | 10552 | 2 | 6 | 7 | 4 | 1 |
   | 10553 | 3 | 4 | 8 | 5 | 1 |

   The number of treasures equals the per-difficulty byte table `DS:0ec3` = {2,3,4,5}. `1198:0103` uses the
   same table to decide how many treasure bundles to load (§3.6).
5. All 112 picture ids occur in each file. The key position k is spread roughly evenly over 0..17.

### 3.3 Selection at the start of a round (`1100:04b5`) [O]

It runs when the room number `DS:5cca` holds the sentinel 0xFE (`1118:0000`, line "if (DAT_5cca == 0xfe)"):
at game start, and again after every trip to the summit (`1118:0000` stores 0xFE after the summit code
`1168:0000` returns).

```
for s in 0..49: for i in 0..2: room[s].obj[i] = 0xFF; room[s].cont[i] = 0xFF
load OTHR 10550+difficulty into scratch
a = rand()*n / 0x8000                     (32-bit signed multiply, then divide)
place(zone 0, record a);  mask[0] = rec.mask;  target[0] = rec.target
do b = rand()*n/0x8000 while b == a;            place(zone 1, record b) ...
do c = rand()*n/0x8000 while c == a or c == b;  place(zone 2, record c) ...
1100:06cc  (derive the clue words, §3.5)
```

Here `n` = the u16 at +0. Zone *z* = mountain level *z* (room table `+0x1a`). The per-zone state lives in
DGROUP. `DS:5ea8+z` = mask. `DS:5ec9+z` = target. `DS:5eab+3z..+2` = clue values in mask order (count, size,
shape, noun). `DS:5eb4+z` = plural flag = `objtab[target].count > 1`. Note that the plural flag uses the
**count even when count is not part of the clue**.

### 3.4 Placement into the room table (`1100:0000`) [O]

The room ("screen") table holds 50 entries of 28 bytes at `DS:23d8`. Hiding-place fields:

| room offset | DS (room 0) | meaning |
|---|---|---|
| +0x12 | `23ea` | u16 background PMAP id (drawn by `1198:0234`) |
| +0x14..+0x16 | `23ec` | `obj[slot]`, slot 0..2 (picture id, or 0xFD/0xFE marker, 0xFF = none) |
| +0x17..+0x19 | `23ef` | `cont[slot]` |
| +0x1a | `23f2` | mountain level (0,1,2 = zones; 3 = summit) |
| +0x06/+0x07/+0x08 | `23de..23e0` | left / right / extra neighbour room (0xFF = none) — context only |

Record byte `2+k` goes to `obj` and byte `20+k` goes to `cont` of the same (room, slot) pair. Two slots also
receive constant markers. The value is written to both obj and cont:

| k | zone 0 (rooms 9–22) | zone 1 (rooms 23–35) | zone 2 (rooms 36–48) |
|---|---|---|---|
| 0 | 10.0 | 23.0 | 36.0 |
| 1 | 10.2 | 23.2 | 37.0 |
| 2 | 12.0 | 24.2 | 37.2 |
| 3 | 12.1 | 26.0 | 39.0 |
| 4 | 13.0 | 26.2 | 39.1 |
| 5 | 14.0 | 27.0 | 40.0 |
| 6 | 14.1 | 27.1 | 40.2 |
| 7 | 14.2 | 28.0 | 41.0 |
| 8 | 15.0 | 28.2 | 41.1 |
| 9 | 16.0 | 29.1 | 41.2 |
| 10 | 16.2 | 29.2 | 42.2 |
| 11 | 17.1 | 30.1 | 43.1 |
| 12 | 17.2 | 30.2 | 43.2 |
| 13 | 18.0 | 31.0 | 44.0 |
| 14 | 18.1 | 31.1 | 44.1 |
| 15 | 18.2 | 31.2 | 44.2 |
| 16 | 22.0 | 35.0 | 48.0 |
| 17 | 22.1 | 35.1 | 48.1 |
| 0xFE (gate) | 11.1, 11.2 | 25.1, 25.2 | 38.0, 38.1 |
| 0xFD (net-shop sign) | 13.1 | 24.0 | 36.2 |

(`room.slot`. Rooms 9, 19, 20, 21, 32–34 and 45–47 get nothing from the record.)

Marker semantics, observed only as far as the hiding places need them:

* **0xFE — the locked gate of the level.** Searching a third of the screen whose `cont == 0xFE` is checked
  at `10e0:0d18`. With the key (`DS:5e59 == 1`), the player walks to x = 330 / 281 / 170 (rooms 11 / 25 / 38,
  `10e0:1033`) and the gate opens. Without the key, message 8 is shown (`1130:0000(8)`).
* **0xFD — the net-shop sign, paid four times.** Counter `DS:5e7e` is set to 4 at `1160:1632` and
  `10f8:0241`. Each search decrements it (`10e0:0766`). Its picture is BNDL **302** member `4 − counter`
  (PMAP 1350–1355, loaded by `1198:0085 PUSH 0x12e` into DS:5e80, read by `10f8:0285`; not BNDL 310, which
  is the thrower elf's sparkle/coin projectile; corrected per ARCHITECTURE.md §7.1 C13), drawn at
  x = `DS:1066[level]` = {207, 99, 365} and y = window 0x18 (`10f8:0285`). At 0 the player walks to x =
  `DS:0e89[level]` = {220, 112, 378}, and the open-door sequence in `10f8:0000` runs (PMAP 1320–1325).
  This is the **net shop** of the level (rooms 13/24/36): the player takes a net, +10 nets
  (`10f8:0241`), and the counter is reset to 4 (corrected per ARCHITECTURE.md §7.1 C13; an earlier
  version guessed "hidden passage").
* **0xFF** means no hiding place. `1100:0638` loads a picture only for `obj < 0xFD`.

### 3.5 Clue words (`1100:06cc`, `1100:0793`, `1100:08ae`) [O]

For each zone *z*, take `e = objtab[target]` (10555 is now loaded in scratch). Walk the mask bits in the order
8, 4, 2, 1. For each set bit, append that property's value to `DS:5eab[3z..]`. Then:

* `DS:5e84[z*12 + 4j]` = far pointer to the word for clue *j*. Nouns use the plural table if `DS:5eb4[z]`
  is set. These pointers are 12 bytes per zone.
* `DS:5eb7[z*6 + 2j]` = speech index (`code + bias`, biases as in §2.2). It is played as SSND `0x1c84 + index`
  by `10e8:01ef`.

`DS:1078` counts the clue words revealed in the current level (0..3). `10e8:06e7` increments it when the
player earns a clue. The words revealed so far are drawn at (17, 315 + 19·*j*) by `10e8:019f`, and spoken by
`10e8:0228`. `1150:049a` shows the newly earned word. Because the clue is always 3 words with the noun
last, the third word revealed is always the noun.

### 3.6 Behaviour of the hiding places at run time [O]

* **Geometry** (`1198:0234`, `1198:0305`; tables `DS:2dd4`, `DS:2dda`). The picture for slot *i* is drawn at
  x = {26, 197, 368}[i], y = window 90 (0x5a), which is scene y 70 because the play area starts at window
  y = 20. Its size is 118×133. A revealed item is drawn at x = {49, 220, 391}[i], window y = 152 (0x98),
  scene y 132.
* **Which slot a search hits** (`10e0:0447`): `slot = clamp((player_x + DS:0cf5 or −DS:0d9f) / 170, 0, 2)`.
  The screen is split into thirds 0–169, 170–339 and 340–511. The offset depends on the facing direction
  `DS:5cd1`.
* **Searching** costs one coin (Enter key 0x0d → `10e8:06ff(-1)`). Coins are `DS:5e5a` and start at 10
  (`1160:1632`). With no coins, message 6 or 7 is shown. The search animation counter `DS:5e2c` runs from 1
  to 13. At step 1 the slot is latched (`DS:0b9e`). During the steps with bit 1 set (2, 3, 6, 7, 10, 11),
  the picture area is replaced by the background without pictures (buffer rows +0x300). If `cont ≥ 2`, the item is drawn there
  (`1198:0305`, `1198:0499`). At step 13 (`10e0:0766`):
  * `cont == 2` → **treasure**: `10e8:074a` shows message 4. It increments `DS:5e5e` (treasures found in this
    level, which picks the next treasure picture), `DS:5e61`, and `DS:5e5f` (score, only on levels 0–2). It
    plays SSND 7041. Then cont = 0.
  * `cont == 3` → **key**: `10e8:06c9(1)` sets `DS:5e59 = 1` and plays SSND 7012, then message 9. Then
    cont = 0. The key picture is BNDL 301 member `2*level` (PMAP 1300/1302/1304).
  * `cont == 0xFD` → the net-shop counter (§3.4).
  * `cont == 0` or `1` → nothing. **No code distinguishes 0 from 1.** A full scan for accesses to
    `+0x23ec` and `+0x23ef` finds only `10e0:0766/07a6/0d18`, `1100:04da/04e4/0648`,
    `1160:11f1/121a` (save), `1160:15e6/1609` (restore), and `1198:0390/03d8/04c7`.
    The scan is a raw byte search of **every code segment** of `TMTWINCD.EXE` for the displacement bytes
    `ec 23`, `ef 23`, `f2 23` and `d8 23` (room-table base), so it also covers code outside the Ghidra
    functions (`extracted/scratch/othr_tables_xcheck/scan.py`). The two restore stores are in the
    uncovered range `1160:137d..1631` (after `1160:0f28` ends). Both are `mov [bx+di+disp16], al`
    (`88 81 ec 23`, `88 81 ef 23`): writes only. They copy back the bytes that the save at `1160:0f28`
    wrote into the per-user record (0x62f bytes per user at far `DS:6172` + `DS:6171`·0x62f):
    obj at +0x500 + 3·room + slot, cont at +0x596 + 3·room + slot (150 bytes each). The only other
    reads of `+0x23ef` test for exactly 2, 3, 0xFD, 0xFE, or `> 1` (`1198:04c7`, the erase pass that clears
    a drawn treasure/key before the next frame). [O]
  * After step 13, the slot's cont is set to 0 only for 2 and 3. The values 0, 1, 0xFD and 0xFE are left
    as they are (`10e0:07ee` path). [O]
  * Search sound effects (via `1170:0000`, SSND 7000 + n): step 4 → 7034, step 7 → 7000, step 8 → 7006.
    The whole search runs for 13 game-loop ticks of 100 ms ≈ 1.3 s. The player pose for step *s* is
    the byte at `DS:0bb8 + s`. A second (overlay) pose index, 0 = none, is the byte at `DS:0bc6 + s`
    (`10e0:0447`, the lines that read `DAT_5e2c + 3000` and `DAT_5e2c + 0xbc6`). Its position comes from the
    same per-pose tables `DS:0c89`/`DS:0d33`/`DS:0ddd` that move the player. [O]
* The treasure shown for the *n*-th `cont == 2` place found is entry *n* of the list that `1198:0103` builds.
  It loads `N = DS:0ec3[difficulty]` treasure bundles. Element 0 is treasure `count % 43` (count =
  `DS:65ce`, treasures already delivered) if no treasure has been found yet (`DS:5e61 == 0`). The other
  elements are random and distinct: `t = rand()*(51 − c)/0x8000 + c`, with `c = count % 43`.
* The realised layout is saved: `1160:0f28` copies the obj and cont bytes of every room plus
  mask, clue values and plural flag of each zone. It saves the realised layout, not the record index.

---

## 4. DAM animation scripts (20000–20050, 20100–20102, 20200–20205, 20300–20308)

### 4.1 File format [O]

```
+0   char[4] "DAM\x1a"     (44 41 4d 1a). The interpreter only distinguishes "DOP" (first 3 bytes)
+4   byte-code until 0xFF, then NE zero padding
```

The interpreter is `10b8:0312`, which runs one tick of all 16 actors. It starts at `base + 4` (`10b8:0119`)
and reads one opcode byte at a time. **Any byte below 0xF9 is a 1-byte no-op** because the switch has no
default case. Positions are signed 16-bit offsets from the actor origin.

| Op | Operands (after the op byte) | Size | Semantics |
|---|---|---|---|
| `FD` | `i16 dx, i16 dy, u8 cel, u8 flags` | 7 | Queue a draw for this tick: image = celTable[actor.celBase + cel], at (actor.x + dx, actor.y + dy). |
| `FE` | – | 1 | End of frame: stop this actor until the next tick. |
| `FF` | – | 1 | End: call actor.callback (far, if non-null), then free the actor (script pointer = 0). Draws queued earlier in the same tick are still drawn. |
| `FA` | `u16 target, u8 c` | 4 | Loop. `k = c >> 7` selects one of the actor's two counters (initially −1). If counter[k] == −1, set it to `c & 0x7F`. Then counter[k] −= 1. If it is now 0, set it to −1 and continue; otherwise jump to `base + target`. The body runs `c & 0x7F` times in total. **`c & 0x7F == 0` loops forever.** |
| `FC` | `u8 pad, u8 n` | 3 | Play sound effect SSND `7000 + n` (`1170:0000`: stops and frees the previous effect; does nothing when effects are off, `DS:0b96`). |
| `FB` | `u8 pad` | 2 | Stop the current sound effect (`1170:005b`). The operand byte is read and discarded (`10b8:04f3`). |
| `F9` | `i16 x, u8 y, i16 w, u8 h` | 7 | Queue a present rect `(actor.x + x, (actor.y + y) & 0xFF, w + 2, (h + 2) & 0xFF)` in `DS:5bde` (6-byte entries). `10b8:090a` flushes it with `Gfx_BlitToWindow` (`1058:05bc`), clipped by `10b8:0082`. y and h are zero-extended. |

Usage in the shipped data, observed by `othr_tables.parse_dam` over all 63 scripts:

* Only `FD`, `FE`, `FF`, `FA` and `FC` occur. There are no no-op bytes and no `F9` or `FB`.
* Every `FD` flags byte is 0.
* The only `FA` is `FA 04 00 00` (loop forever back to the first opcode). It appears in 20000–20050 and
  20100.
* Every script ends with exactly one `FF`.
* No script starts with `"DOP"`.

Example (20000, treasure 0): `DAM\x1a` then `FD 0000 0000 00 00 FE` ×3, `FD 0000 0000 01 00 FE` ×3,
`FA 0400 00`, then the unreachable `FD … FE FF`. Cel 0 is held for 3 ticks, then cel 1 for 3 ticks, and this
repeats until the actor is stopped. Stopping executes `FF` (below).

### 4.2 Actor table and engine state [O]

There are 16 actors × 0x1d bytes at `DS:4f82`. `10b8:00fd` clears them and `10b8:0119` allocates the
first one whose script pointer is 0. A full table returns 0, which is indistinguishable from slot 0.

| off | DS (actor 0) | type | field |
|---|---|---|---|
| +0x00 | 4f82 | u8 | layer (draw group, see 4.3) |
| +0x01 | 4f83 | u8 | stop request (set by `10b8:024e`) |
| +0x02 | 4f84 | u8 | actor flags: bit0 = erase cels next tick; bit1 = opaque draw |
| +0x03 | 4f85 | i16 | origin x |
| +0x05 | 4f87 | i16 | origin y |
| +0x07 | 4f89 | i16 | celBase (index into the cel table) |
| +0x09 | 4f8b | i16[2] | loop counters (init −1) |
| +0x0d | 4f8f | u32 | user value (`10b8:0119` params 6–7); never read [O, by search] |
| +0x11 | 4f93 | far fn | completion callback (called at `FF`) |
| +0x15 | 4f97 | far ptr | current script pointer (0 = free) |
| +0x19 | 4f9b | far ptr | script base (resource start, header included) |

`10b8:0119(layer, x, y, celBase, script far, user u32, flags, callback far)` creates an actor.
`10b8:020a(actor, script)` returns 1 while that actor is still running *that* script.
`10b8:024e(actor, script)` requests a stop. On the next tick the stop is handled exactly like `FF`, without
reading further opcodes and without drawing.

**Cel table** [O]: 100 far pointers at `DS:5152..52e1`, one per slot. `10d8:0000(bndl, slotBase, &bundle,
&script)` loads a BNDL, returns member 0 (the DAM script) in `*script`, and stores members 1..n−1 in slots
`slotBase..slotBase+n−2`. It returns n−1. Code can also store a slot directly, e.g. slot 80 (`DS:5292`) at
`10f0:00d8`.

### 4.3 One tick, and compositing [O]

The offscreen buffer is 512 wide (`1050:06a7` uses stride 0x200). From the call arguments used here:
rows 0..383 are the **composition** image (window coordinates; the play area is rows 20..286). Rows
384..767 (`+0x180`) are the **clean background**. Rows 788..1054 (`+0x300`, the play-field rows only; the
buffer has 1055 rows, ARCHITECTURE.md §7.1 C2) are the background without hiding-place pictures. See `code_1058-1090.md` for the blitters.

Each tick of a player loop (`10d8:0082`, `1138:0283` or the game loop `10e0:007d`) does the following:

1. `10b8:029d(0x180)`: for every rect in the restore list `DS:52e2` (9-byte entries `{u8 isDOP, i16 x, y, w,
   h}`, count `DS:0b1e`), copy (x, y+0x180, w, h) → (x, y) with `Gfx_CopyOffscreenRect` (`10b8:0000`
   clips to `DS:0864..086a`). Then clear the list. This erases the previous frame's cels.
2. `10b8:0312`: clear the draw queue (`DS:0b20 = 0`) and run every active actor until `FE`, `FF` or a stop.
   Each `FD` appends a 14-byte entry at `DS:5666`: `{u8 layer, u8 isDOP, i16 x, i16 y, far pmap, u8 cel
   flags, u8 actor flags, i16 slot}`.
3. `10b8:06e4(layer, dy, bgOff)`: draws, in queue order, every entry of the given layer at (x, y+dy):
   * Actor flags bit1 clear: `PixMap_DrawSprite` (`1058:1320`). It is colour-keyed. **Cel flags bit0 = draw
     mirrored horizontally** (`1058:114c` → `1050:06a7`, which writes right to left). Actor flags bit1 set:
     `1058:0cf3` (opaque; with cel bit0 it is white-keyed and mirrored). When `dy == 0` it is also presented
     immediately with `Gfx_BlitToWindow`.
   * Cel flags bit1 clear and actor flags bit0 set: push `{isDOP, x, y, w+2, h+2}` onto the restore list,
     so the cel is erased next tick.
   * Cel flags bit1 set: **stamp**. Copy the drawn rect (x, y, w, h) → (x, y+bgOff) into the clean
     background (`10b8:084b`), so the cel persists. No restore rect is pushed.
   * `isDOP` (script header starts `"DOP"`) clears `DS:0862` (dirty-rect recording,
     `Gfx_AddDirtyRect` `1058:068e`) while the entry is drawn and while its rect is restored.
4. `10b8:090a`: present the `F9` rects, then `Gfx_FlushDirtyRects` (`1058:0731`).

Layers in use: 0 (cutscenes, title), 0x4D (treasure rooms, drawn inside `1190:03a3`). The game frame
(`1198:0305`) also draws layers 0x14 and 0x57, but nothing in the analysed code creates actors on them.
All creators pass actor flags = 1: keyed draw plus erase next tick.

Consequences an engine must reproduce:

* A frame with no `FD` shows nothing for that actor, because the previous cels were erased.
* Cels drawn on the **final** tick of a script that ends `FD… FF` without `FE` stay on screen. Among the
  shipped scripts only **20300** does (it ends `FD FD FD FD FF`); 20301, 20302 and 20305 end `FE FF`
  (corrected per ARCHITECTURE.md §7.1 C14). Their restore rects stay queued and are erased at the start of the next tick of
  any actor loop. Scripts ending `FE FF` (20101, 20200, 20201, …) erase their last cels on the extra tick.

### 4.4 Players and timing [O]

* **Cutscene player `10d8:0082(script far, celBase, user u32, skippable, escHandler far, x, y, fps)`**.
  It saves `DS:0862` and sets it to 1, then creates actor (layer 0, x, y, celBase, script, user, flags 1,
  no callback). The first tick is immediate. Later ticks run every `1000/fps` ms (integer division) on
  timer 7 (`10a8:002a`/`10a8:0066`, `timeGetTime`). If `skippable` is set, Enter stops the actor and the call
  returns 1; Space returns 2. Esc calls `escHandler` (always `1140:0098`, the quit dialog). It returns 0 when
  the script ends.
* **Title loop** `1138:0283`: 100 ms per tick (timer 7). It also waits for narration and for a 60 s
  timeout (timer 4).
* **Game loop** `10e0:007d`: 100 ms per tick (timer 7). It runs actors for the treasure rooms.

### 4.5 Catalogue [O unless marked]

| DAM | Cels (BNDL, slot base) | Started by | Origin | Rate | Ticks | Sounds (tick:SSND) | Role |
|---|---|---|---|---|---|---|---|
| 20000+t, t = 0..50 | BNDL 330+t (2–4 PMAPs 1700–1832), slot base `DS:2da8[i]` = 20,30,…,70 | `1190:02a0`, layer 0x4D | room 0: (358, 73); rooms 1–8: (`DS:2d90[i]`, `DS:2d9c[i]`) = x {112,143,174,272,303,334}, y {40,92,40,92,40,92} | 100 ms | 3/4/6/8-tick loop, forever | – | Treasure *t* animates while the player's centre x (`5ccc + w/2`) is within [x, x+30]. Otherwise member 0 is drawn static (`1190:03a3`) and the actor is stopped. Room 0 shows treasure `(count−1) % 43` and rooms 1–8 show older ones, 6 per room (`1190:009b`), so **20043–20050 are never played** (their BNDLs are still used as treasure pictures, §3.6). Looped cel sequence (one cel per tick; each script also has an unreachable `FD cel 0 FE FF` tail): 16× (0,0,0,1,1,1), 13× (0,0,1,1), 8× (0,0,1,1,2,2,3,3), 4× (0,0,0,0,1,1,1,1), 6× (0,0,1,1,2,2), 4× (0,1,2) |
| 20100 | BNDL 400 (PMAP 2101–2110), base 0 | `1138:00ee` | (0, 20) | 100 ms | 10-frame loop, forever | – | Title: animated mountain picture at window x 338 on top of PMAP 2100. It runs during the story pages |
| 20101 | BNDL 401 (2111–2129), base 10 (after 400's 10 cels) | `1138:0246` | (32, 21) | 100 ms | 20 | – | End of the intro: the Super Seeker character jumps away and shrinks. Played after the pages |
| 20102 | BNDL 402 (2130–2169) | **none found** | – | – | 41 | – | No reference to BNDL 402 or id 20102 (0x4e86) in code or DGROUP [O, by search] |
| 20200 | BNDL 410 (1100–1103, 2215), base 0 | `1188:007c` | (0, 0) | 7 fps = 142 ms | 12 | 1:7027 3:7025 5:7027 7:7025 9:7027 | Leaving mountain level 0 (`1188:0000(oldLevel=0)`, called from `1118:0000` when room `+0x1a` changes). Ids come from the table `DS:2d6d`: {BNDL 410, 412}, `DS:2d71` {20201, 20205}, `DS:2d75` {PMAP 2200, 2201} |
| 20201 | BNDL 410 cels, base 0; loaded by id | `1188:0158` | (0, 0) | 142 ms | 18 | 8 alternating 7025/7027 | Part 2 over background PMAP 2200 (climbing view). Skipped if part 1 returned 2 (Space) |
| 20204 | BNDL 412 (1133, 1134, 2203–2210), base 0 | `1188:007c` | (0, 0) | 142 ms | 7 | 0:7013 | Leaving level 1, part 1 |
| 20205 | BNDL 412 cels | `1188:0158` | (0, 0) | 142 ms | 9 | – | Part 2 over PMAP 2201 |
| 20300 | BNDL 323 (2300–2365 → slots 0..65) | `10f0:0126` | (0, 20) | 4 fps = 250 ms | 24 | 2:7016 5:7018 15:7017 18:7014 22:7010 | Treasure delivered (`10f0:0000`), part 1, over PMAP 2299 |
| 20302 | slots 0..65 + **slot 80 = delivered treasure** (BNDL `330 + (count−1)%43` member 0) | `10f0:0155` | (0, 20) | 250 ms | 33 | – | Part 2: the treasure (cel 0x50) flies into the chest |
| 20301 | slots 0..65 | `10f0:0209` | (0, 20) | 250 ms | 21 | 1:7011 4:7023 6:7035 9:7024 13:7003 | Part 3 when the player did not just reach rank 7 (`DS:5ef2 == 0`, `10f0:030d`) |
| 20305 | BNDL 324 (2312, 2301, 2302, 2400–2448 → slots 0..51) | `10f0:01b9` | (0, 20) | 250 ms | 49 | 1:7011 3:7014 5:7009 8:7020 21:7002 30:7040 32:7021 | Part 3 when the player reached rank 7 |
| 20308 | BNDL 325 (1119–1125) | `10f0:02db` | (0, 20) | 250 ms | 12 | 0:7033 | Part 4 over PMAP 2202 (drawn at `10f0:024b`) |

In the `10f0` and `1188` sequences, a non-zero (skip) result ends the whole sequence (`10f0:037e` frees
everything). "Ticks" counts the ticks until `FF`, simulated by `othr_tables.simulate_dam`. For a looping
script (`FA 04 00 00`) the simulation covers two passes plus one tick. All cel indices used stay below the bundle's cel count (checked).

---

## 5. OTHR 20105–20108 — intro story pages [O]

The format is plain 8-bit text, one paragraph per resource, ending in `0D 00` (CR, NUL). Sentences are
separated by two spaces. Lengths excluding the NUL: 285, 320, 231 and 226 bytes. The opening words of page 1
are "Once upon a time…", for identification.

`1138:007a` cycles page = 0..4. Page 0 draws PMAP 2100 at (0, 20). Pages 1–4 load OTHR `20104 + page` into
the scratch buffer and draw it with `10b0:0023(0x2b, 0x43, 0x108, text)`. [I] That is a word-wrapped text
box at (43, 67), 264 px wide. All of this happens while DAM 20100 animates. If speech is on (`DS:0b97`),
`1138:043d(page)` plays narration SSND `DS:141a[page]` … for `DS:1422[page]` clips. That is
7150–7152, 7153–7155, 7156–7157 and 7158–7159 for pages 1–4. Each clip waits for its end (`1088:0175`), or
60 s, or a key. Without input, pages 0–4 repeat as an attract loop. Enter ends the current page (after
page 4 the intro finishes). Space ends the intro at once. In both cases DAM 20101 then plays and the
function returns 1 (or 0 if Esc is confirmed during 20101). Esc with the quit dialog confirmed
(`10c0:0ae5(0x2234)` == 1) returns 0 at once, without 20101.

---

## 6. Re-implementation checklist

1. Parse 10555 into 112 tuples, and parse 10550+d into 500 records. Read the word tables from the EXE by
   relocation, or ship none and read them at run time.
2. New round: Borland `rand` and the three distinct record indices (§3.3), then fill the room slots using the
   §3.4 table plus markers. Derive clue values, plural flag, word pointers and speech ids (§3.5).
3. Hiding place = third of the screen. Picture at (`{26,197,368}`, scene y 70). Search costs a coin.
   Content handling as in §3.6 (0 and 1 are equivalent).
4. DAM: implement the §4.1 opcode set exactly, including ignoring unknown bytes, the two loop counters, and
   the `FB` pad byte. Also implement the 100-slot cel table, the per-tick erase/draw/stamp model of §4.3,
   and the per-context timing of §4.4.

## 7. Tool and previews

* `tools/assets/othr_tables.py` is importable (`parse_object_table`, `parse_layout`, `place_record`,
  `pick_records`, `BorlandRand`, `check_record`, `clue_values`, `clue_speech_ids`, `parse_dam`,
  `simulate_dam`, `ExeData`).
  * `python -P othr_tables.py dump` writes `extracted/previews/othr_tables.json`, containing all records
    with clue/speech/check results, all DAM ops with context, the story text, and the room
    backgrounds/links.
  * `python -P othr_tables.py render [--records N]` writes into `extracted/previews/othr_maps/`.
* Previews:
  * `objects_10555.png` — all 112 pictures with tuples and words.
  * `layout_<id>_r<rec>_level<z>.png` — every room of a zone with its background, the pictures placed by the
    record, the content colour-coded (red = key, yellow = treasure, blue = 1, grey = 0), the key picture or a
    stand-in treasure at the reveal position, the gate and special markers (magenta), and the thirds lines.
  * `dam_<id>.gif` — every DAM script simulated over its background where one is known.

  I checked these images visually. The pictures sit on the ground line at scene y 70 in every room. The
  gate slots cover the ladder tree (room 11), the stone-well area (25) and the castle door (38). The 0xFD places are rock
  doors (the net-shop niches, C13). The treasure arc in 20302 ends in the chest. The treasure loops (cat, drum, train, fan, …) are
  correct.

## Open questions

1. **Content value 1 vs 0.** The data places `#2 + 3` "1"s per level (5/6/7/8 by difficulty). The Windows EXE
   never distinguishes 1 from 0. A raw byte scan of all code segments, including the ~14 KB outside
   Ghidra functions, finds no other direct access (§3.6). [I] The remaining explanations are generator or
   Mac-edition residue, or an access through a computed pointer that the scan cannot see. The DOS version
   is still worth checking.
2. Are rooms 19–21, 32–34 and 45–47 (background PMAP 1000, linked 18↔22 etc.) reachable? They never receive
   hiding places. That belongs to the room-graph spec.
3. DAM 20102 (BNDL 402, 40 cels) is not referenced. It may be used through code outside the analysed
   functions, or be a Mac leftover.
4. The meaning of the `"DOP"` header variant and of cel flag bit1 (stamp into background) and actor flag
   bit1 (opaque draw). The behaviour is fully specified above, but no shipped data or caller uses them.
5. The `F9` rectangles store y and h as bytes, which looks like a Mac-era 8-bit field. Its intended use is
   unknown (unused in data).
6. The `FC`/`FB` pad byte may be the high byte of a big-endian u16 from the Mac original (the interpreter
   ignores it). This only matters for writing new scripts.
7. Treasure-room pictures use a signed 8-bit index (`DS:2d7e`, `1190:009b`). After 128 delivered treasures,
   the indices for rooms 1–8 go negative and are skipped. This needs checking in the original before
   replicating.
