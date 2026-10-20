# Gizmos & Gadgets (DOS v1.0): puzzles, part B

Scope: four of the eight puzzle mini-games in full: **machine pieces** (kind 2, ovl21 3a4a),
**gears and buttons** (kind 3, ovl22 3b2f), **balance** (kind 4, ovl14 32fe) and **magnets** (kind 7,
ovl16 35bb). The puzzle framework (the locked door, state 9, the choice of kind and variant, the
per-player history, the dispatch, the panel, the Hint viewer, the reward, the sound helpers of
ovl19 and the door that opens afterwards) is part A's: `code_puzzles_a.md` §1, §2 and §7. This
document describes only how the four puzzles plug into it (§1) and then each puzzle (§2-§5). Wave 3
implements these modules from this document alone.

Evidence: the named Ghidra export `re/gg/ghidra_export/` (the assembly was read for every function of
the four overlays; their switch tables were read from the bytes), the bytes of `ssg.exe` (DGROUP
tables and zone records), the archives through `tools/gg/ggarc.py`, the reference frames
`extracted/captures/gg/w0/17..20` (machine pieces) and `r2/puzzle/k2_*`, `k3_*`, `k4_*`, `k7_*` (one
variant 0 of each kind), and the save states of those captures (`savestates/w0/17_puzzle`,
`18_puzzle_piece1`, `20_puzzle_solved`, `r2/puzzle/k*_screen`, `k*_solved` and the intermediate
ones), whose DGROUP and heap were read with `tools/gg/capture_mem.py` (read only). The state checks
are listed in §8. Scratch tools in `extracted/scratch/gg/wave1/puzzles_b/`: `pieces.py`, `gears.py`,
`balance.py` (with a brute-force solvability check), `magnets.py` (variant decoders), `stmem.py`
(puzzle heap data from save states), `rd.py` (bytes at Ghidra or DS addresses), `split.py` (per-overlay
assembly).

Notation: **[O]** observed in code, bytes, data or captures; **[I]** inferred. `SSSS:OOOO` = Ghidra
address (overlay file offsets: `overlay_map.tsv`); `DS:XXXX` = DGROUP 1ea7. Names are
those of `re/names/gg/`; the names this document introduced (all functions of the four overlays, their
DGROUP variables and tables) are in `game_puzzles.tsv` and `dgroup.tsv` since the wave 1 merge
(`re/names/gg/README.md`). Engine helpers use the
names of part A and of the platform spec (`Zones_AddLocal`, `Zones_RemoveLocal`, `Zones_ClearLocal`,
`Mouse_SetHotSpot`, `Mouse_FreeCursor`, `Gfx_SaveToBack`, `Input_StripHeld`, `Input_Flush`,
`g_mouseX` DS:7a88, `g_mouseY` DS:7a86, `g_mouseButtons` DS:0a0c, `g_cursor` DS:7a6a, `g_cursorW`
DS:7a70, `g_cursorH` DS:7a72, `g_cursorArrow` DS:7a82). Ticks are PIT ticks (145.652 Hz). Game text
is cited by resource id only. "Effect n" = member n of the puzzle's effect bank (part A §2.4);
"prio" = the priority argument of `PzSnd_Play`.

Part A's conventions apply unchanged (`code_puzzles_a.md`, introduction): the 22-byte **zone**
record and its hit test, **cursor records**, `Input_StripHeld(Input_GetCode())` (only press edges,
releases and keys reach a puzzle), the **pascal** argument order of `Lpx_DrawKeyed(x, y, picture,
mode)` and `Lpx_Draw(x, y, picture)`, and `Gfx_RestoreRect` / `Gfx_SaveToBack` (back page rows
y+200). Two platform facts matter often below [O]:

* **Zone hit test** (17ab:0507): a rectangle zone contains (px, py) when x ≤ px < x+w and
  y ≤ py < y+h; a circle zone (shape ≠ 0, radius at +6) when dx² + dy² < r² measured from (x, y).
  The highest priority wins; among equal priorities the zone met first in the list wins, and
  `Zones_AddLocal` pushes at the head, so **the most recently added zone wins a tie**.
* **The cursor picture is drawn with its top-left corner at (`g_mouseX`, `g_mouseY`)**; the hot spot
  (+0x0a/+0x0c of the cursor record, `Mouse_SetHotSpot`) only shifts where zone presses are tested.
  Every "carried" object of these puzzles is the cursor picture itself, so its top-left is the mouse.
  Several puzzles move the mouse themselves (INT 33h function 4 plus `g_mouseX/Y`, §3.6, §5.6).

---

## 1. How the four puzzles plug into the framework

### 1.1 Which overlay is which puzzle [O]

The dispatch table CS:018e of `Puzzle_Dispatch` (38e2:00d4) sends kind 2 to 3a4a:0000, 3 to
3b2f:1ed0, 4 to 32fe:0aa0 and 7 to 35bb:1c03 (part A §1.4). Independent confirmation:

| Kind | Overlay | What the code does | Captures and states |
|---|---|---|---|
| 2 machine pieces | ovl21 3a4a | pieces out of a box assembled into a machine picture; hint text formatted with a machine name from DS:357c | `w0/17..20`, `r2/puzzle/k2_*`: "Pieces" box, assembled machine; DS:b13a = 2 |
| 3 gears | ovl22 3b2f | gears placed on pegs, a crank turned by mouse motion, racks moved, buttons pressed | `r2/puzzle/k3_*`: crank, gears, racks, ON/OFF buttons; DS:b13a = 3 |
| 4 balance | ovl14 32fe | weights hung on a beam; torque sums Σ mass·arm on both sides; the beam tilts | `r2/puzzle/k4_*`: scale with numbered weights; DS:b13a = 4 |
| 7 magnets | ovl16 35bb | magnets placed on a table; a pole-force simulation; target outlines | `r2/puzzle/k7_*`: N/S magnets, ROTATE button, outline; DS:b13a = 7 |

The "[I]" of ARCHITECTURE §1.4 / §2.8 and of the three `Pz…_Run` comments in `game_puzzles.tsv` can
go (part A §1.4 came to the same result).

### 1.2 The common shape [O]

`Puzzle_Dispatch` calls `Pz…_Run(variant)` (far, cdecl) and stores its return value: **1 when the
puzzle was solved during this visit, else 0** (part A §1.4). Each of the four puzzles:

1. loads its effect bank (`PzSnd_LoadBank`), its variant data and its pictures;
2. draws its screen picture with `Screen_LoadPicture(pic, pal)` + `Lpx_Draw(0, 10, g_resBuffer)` and
   the panel with `Puzzle_InitPanel(panel text)` (gears draws the panel first, §3.5), saves the
   screen with `Gfx_SaveToBack(0, 0, 320, 200)` and calls `Screen_FadeOutIn` (the room was faded out
   by state 8);
3. adds its zones and shows the puzzle New Player Hint `Hint_Show(0x1052)`;
4. runs an **unpaced** input loop: `code = Input_StripHeld(Input_GetCode())`, a switch on the code,
   draws only on change (explicit `Dirty_Flush`). Timed animations use timer slots 0 or 7 inside
   handlers; nothing runs between events (part A §2.3);
5. on leaving it frees everything and returns the solved flag, in its own order [O wave 3, the asm
   at each exit]: machine pieces `PzPieces_Free`, `PzSnd_FreeBank`, then `Pal_FadeOut` (not the
   fade first); gears `Puzzle_FreePanel`, `Pal_FadeOut`, `PzGears_Free`; balance and magnets fade
   out first (`Pal_FadeOut`, then the frees and `PzSnd_FreeBank`).

Common codes, as in part A §1.6:

| Code | Effect in all four |
|---|---|
| 0x0d (Enter), 0x5de (panel, `g_pzPanelZone` DS:331c, added by `Puzzle_ShowSolved`) | only when solved: `g_puzzleGoThrough` DS:b144 = 1, leave |
| 0x5dd (Go Back, DS:3306) | effect (table below, prio 5), `Puzzle_PressGoBack`, DS:b144 = 0, leave. Machine pieces: only while no piece is carried; the other three always (a carried gear, weight or magnet is simply dropped with the screen) |
| 0x1b (Esc) | `Cmd_SaveAndExit` 4106:00b2 (Yes/No box; Yes saves and exits) |
| 0x17a2 / 0x17a3 (menu) | `g_puzzleLeave` DS:3332 = 2 / 1, leave |
| Hint zone | pressed Hint picture 0x1095, flush, effect (prio 5; magnets plays it before the flush), busy-wait while `g_mouseButtons` ≠ 0, restore, flush, `Puzzle_ShowHintText(text)`. Machine pieces: only while no piece is carried |
| success | `Puzzle_ShowSolved(jingle)` (one `rand()`, part A §2.2) then `Puzzle_AwardPoints(points)` (+400) |

| | Machine pieces | Gears | Balance | Magnets |
|---|---|---|---|---|
| Run | 3a4a:0000 | 3b2f:1ed0 | 32fe:0aa0 | 35bb:1c03 |
| effect bank (members) | 0x11ca (23) | 0x11c8 (12) | 0x11c6 (14) | 0x11d0 (12) |
| screen / palette (size) | 0x2084 / 0x2085 (320×155) | 0x2021 / 0x2022 (320×157) | 0x2119 / 0x211a (320×156) | 0x119f / 0x11a0 (320×156) |
| panel text | T 0x20b0 | T 0x202a | T 0x214d | T 0x11c5 |
| hint text | T 0x20b1 with `%s` (§2.11) | T 0x202b + variant | T 0x212d + variant | T 0x11a7 + level (§5.3) |
| Hint zone → code | DS:3566 (26,13) 35×15 → 0x458 | DS:38ec (23,24) 36×15 → 0x3e8 | DS:2828 (6,13) 35×15 → 0x458 | DS:29b6 (13,13) 36×15 → 0x458 |
| pressed Hint at / restore | (26,13) / (26,13,35,15) | (23,24) / (23,24,36,15) | (6,13) / (6,13,35,15) | (13,13) / (13,13,36,15) |
| Hint effect | 0x12 | 9 | 0xb | 4 |
| Go Back effect | 0x15 | 0xa | 0xc | 0xa |
| `Puzzle_ShowSolved(n)`: jingle n + random(4) | 3 → 3..6 | 5 → 5..8 | 2 → 2..5 | 6 → 6..9 |
| `Puzzle_AwardPoints(n)` | 0x16 | 0xb | 0xd | 0xb |
| puzzle cursor | resource 3 | resource 3 | resource 3 | resource 3 |
| cursor at exit | the saved copy of `g_cursor` (§2.4: the 0x18-byte copy is put back, stale save-under fields included, and DS:7a6a itself is selected as the record; the engine selects the saved record) | `g_cursorArrow` | `g_cursorArrow` | `g_cursorArrow` |

Resource 3 is a 15×17 **hand** (not an arrow), the cursor the captures show over the puzzles
(REFERENCE_CAPTURES §4) [O picture]. The pressed Hint picture 0x1095 is 36×15: machine pieces and
balance restore only 35 columns, so its last column stays drawn, as in electric (part A §2.2) [O].

### 1.3 Input codes in use

| Code | Machine pieces | Gears | Balance | Magnets |
|---|---|---|---|---|
| 0x3e8 | | Hint | | |
| 0x44c | | crank handle (DS:b818) | | |
| 0x44d / 0x44e | | inventory large / small gear | | |
| 0x44f | | board (place a gear) | | |
| 0x454 | work area (drop) | | beam area (no handler) | |
| 0x456 | | | | table (pick up / drop) |
| 0x458 | Hint | | Hint | Hint |
| 0x459 | box (take a piece) | | inventory (put back) | inventory (take / put back) |
| 0x45a | | | weight types 0..5 (0x45a..0x45f) | ROTATE |
| 0x45b..0x465 | pieces 0..10 | | | |
| 0x461..0x46f | | | left hooks 0..4 in 3 zone sets | |
| 0x4b0..0x513 | | placed gear i (0x4b0 + i) | | |

---

## 2. Machine pieces (kind 2, ovl21 3a4a)

The player takes the pieces of a machine one by one out of a box and assembles them in a work area
until they form the machine; the assembled machine picture is then revealed. The `w0` tour solved
variant 2 (a T-handle tool of three pieces).

### 2.1 Resources (`PzPieces_LoadResources` 3a4a:0ca8, `PzPieces_DrawScreen` 3a4a:0be5) [O]

| Id | Use | Kept in |
|---|---|---|
| F 0x209c + v | variant data (§2.2), read by `Res_LoadTo` straight into DS:b51e (`PzPieces_LoadVariant` 3a4a:0bcb) | `g_pieces` |
| bundle 0x2088 + v | members 0..n−1: the piece pictures; member n: the **assembled machine** picture | DS:b51a, DS:b50a (member n) |
| 0x2084 / 0x2085 | screen and palette | — |
| 0x1095 | pressed Hint | DS:b50e |
| bundle 0x2087 | box lid: 0 closed (80×32), 1 half open, 2 open (80×33) | DS:b512 |
| bundle 0x204f | digits 0..9 (the box count) | DS:b516 |
| T 0x20b1 | hint text format, one `%s` | DS:b506 |
| T 0x20b0 | panel text | DS:b502 |
| resource 3 | puzzle cursor (hand) | DS:b6c2 |
| resource 4 | a 1×1 picture (key 15): an invisible cursor while a group slides | DS:b6b6 |
| DS:357c | 20 far pointers to the machine names (NUL-terminated strings DS:35cc..36cf) | (initialised data) |
| effect bank 0x11ca | sounds (§2.12; shared with simple machines) | |

### 2.2 Variant data [O, `pieces.py`]

**F 0x209c + v** (20 entries, 180 bytes each) = 10 records of 18 bytes, the layout of `g_pieces`
(§2.3). Only two bytes per record are data: **+0x0c u8 dx and +0x0d u8 dy, the offset of the piece's
top-left corner inside the assembled machine picture**. Every other byte is 0 in the shipped files
and is overwritten at load; records beyond the piece count are all zero.

**Bundle 0x2088 + v**: n = member count − 1 pieces (2..5 in the shipped data), then the machine.
`PzPieces_LoadResources` sets n = (first offset word / 2 − 1) − 1 (DS:b5d4) and the box count
DS:b5d2 = n, and for each piece i < n: +8 far picture = member i, +4/+6 = the picture's width and
height (LPX +6/+8), +0x0e group = i. The machine picture is member n (DS:b50a).

Shipped facts: the machines are 31×123 .. 209×123 (all fit the 219×142 work area); a few pieces
reach 1-2 px outside the machine picture (variants 3, 16, 18), which is harmless [O].

### 2.3 State [O]

| DS | Type | Meaning |
|---|---|---|
| b51e + 18·i | rec[10] | piece i: +0 x, +2 y (top-left on screen), +4 w, +6 h, +8 far picture, +0x0c u8 dx, +0x0d u8 dy, +0x0e group, +0x10 placed (1 = on the work area) |
| b5d2 | i16 | pieces still in the box |
| b5d4 | i16 | n, pieces of the variant |
| b5d6 | i16 | the group a drop moves (§2.7) |
| b5d8 | i16 | piece in the hand; **0x0b = none** |
| b5da + 22·i | zone[10] | piece zones: rect, +2/+4 = the piece's x, y (set when it is placed), +6/+8 = w, h, priority **32000 − w·h/100**, press code 0x45b + i, release 0 (`PzPieces_InitZones` 3a4a:0b33). The product w·h is truncated to 16 bits before the division; every shipped piece is below 32768 |
| b6ba | far | cursor record of the piece in the hand |
| b6be | far | an 0x18-byte copy of `g_cursor` taken at start (restored at exit) |
| local SI of `PzPieces_Run` | | solved flag (returned) |

The **anchor** of a placed piece is (x − dx, y − dy): the top-left at which the machine picture would
be drawn if this piece were in its place. The puzzle is solved when all pieces are placed on one
anchor.

### 2.4 Screen and set-up [O 3a4a:0000, 0be5]

```
PzSnd_LoadBank(0x11ca); held = 0x0b
PzPieces_LoadVariant(v); PzPieces_LoadResources(v); PzPieces_InitZones()
PzPieces_DrawScreen():
    copy g_cursor (0x18 bytes) to a new block DS:b6be
    load cursors 3 (hand, DS:b6c2) and 4 (blank, DS:b6b6)
    screen 0x2084 at (0,10); Puzzle_InitPanel(T 0x20b0); Gfx_SaveToBack(0,0,320,200)
    digit n (box count) at (37,130)               not on the back page
    cursor = hand; Screen_FadeOutIn
PzPieces_AddStaticZones() 3a4a:0baf: Zones_AddLocal of DS:353a, DS:3550, DS:3566
Hint_Show(0x1052)
```

| Zone | Rect | Code |
|---|---|---|
| DS:353a work area | (93,14) 219×142 | 0x454 |
| DS:3550 box | (5,82) 80×62 | 0x459 |
| DS:3566 Hint | (26,13) 35×15 | 0x458 |

All three have priority 0; placed pieces have their own zones (above), added only while no piece is
carried. The **work area** is x 93..311, y 14..155 (constants 0x5d, 0x138, 0x0e, 0x9c in the code).

### 2.5 Input [O, codes 0x45b..0x465 tested first, then switch CS:01cb, 9 codes]

| Code | Action |
|---|---|
| 0x45b..0x465 | `PzPieces_PickUp(code)` (§2.8); reachable only while nothing is carried (zones removed meanwhile) |
| 0x459 box | `PzPieces_TakeFromBox` (§2.6) when nothing is carried and the box is not empty |
| 0x454 work area | `PzPieces_Drop` (§2.7; nothing when no piece is carried); then, **if not yet solved**, `PzPieces_CheckSolved` (§2.9) and on success `Puzzle_ShowSolved(3)`, `Puzzle_AwardPoints(0x16)` |
| 0x458 Hint | when nothing is carried: §1.2 with the formatted text of §2.11 |
| 0x5dd | when nothing is carried: effect 0x15, Go Back |
| 0x0d, 0x5de, 0x1b, 0x17a2, 0x17a3 | §1.2 |

After a success the puzzle stays playable: pieces can still be picked up and dropped (the check is not
repeated) [O].

### 2.6 Taking a piece out of the box: the `rand()` call [O 3a4a:0780]

```
if held != 0x0b or inBox <= 0: return
PzPieces_OpenBox():  effect 0xc (prio 3); lid frame 1, flush, 10 ticks; lid frame 2, flush, 10 ticks
inBox -= 1
PzPieces_RemoveZones()                      zones of all placed pieces off
repeat held = random(n)                     rand()·n / 0x8000 in 32 bits: ONE rand() PER TRY
until piece[held].placed != 1
PzPieces_CloseBox(): effect 0xd (prio 3); lid frame 1, flush, 10 ticks; lid frame 0, flush, 10 ticks
Gfx_RestoreRect(37,130,10,10); digit inBox at (37,130)
cursor record from the piece picture; hot spot (w/2, h/2); cursor = it; effect 4 (prio 3)
busy-wait until g_mouseX >= 93, g_mouseY >= 14, g_mouseX + w <= 312, g_mouseY + h <= 156
PzPieces_SetMouseBounds(1)
```

* The lid frames are drawn keyed at (5,82); each wait is `Timer_Set(7,10)` + `Timer_Wait(7)`. The
  whole box animation is 4 × 10 ticks (~0.27 s).
* **rand()**: the loop retries until it hits a piece that is not placed, so one take costs **1 or
  more** calls; with k pieces already out of n, each try succeeds with probability (n − k)/n. This
  is the only rand() of the four puzzles besides the jingle [O: the only call of `rand` 1000:06c1 in
  ovl14, 16, 21, 22 is 3a4a:07a4]. Checked against the states: `w0/17_puzzle` → `18_puzzle_piece1`
  is exactly 1 call (random(3) = 1, the shaft), `18` → `20_puzzle_solved` exactly 5 (2 + 2 tries for
  the two takes, 1 jingle) [O, §8].
* The busy-wait polls the interrupt-driven mouse variables only: the screen keeps updating the
  cursor (interrupt-time flush) but no input code is read until the player has moved the piece wholly
  into the work area. A port pumps the host in this loop.
* `PzPieces_SetMouseBounds(work)` 3a4a:01ef: work = 1: x 93..312 − `g_cursorW`, y 14..156 −
  `g_cursorH`; work = 0: x 0..319 − `g_cursorW`, y 0..199 − `g_cursorH`. It clamps `g_mouseX/Y` to the
  maxima, sets INT 33h functions 7/8 (x doubled), and copies `g_mouseX/Y` into the save-under position
  `g_cursorSavedX/Y` DS:7a6c/7a6e. So a carried piece can never leave the work area [O].

### 2.7 Dropping: snap, groups and the group move [O 3a4a:037d, 05b8, 071d]

```
if held == 0x0b: return
g = piece[held].group                               DS:b5d6
piece[held].x, y = g_mouseX, g_mouseY               the picture's top-left (no hot spot)
if the piece is not wholly inside the work area: return         (cannot happen with the bounds)
a = anchor(held)
for j in 0..n-1, j != held, piece[j].placed:
    b = anchor(j)
    if |b.x - a.x| < 10 and |b.y - a.y| < 10:       strict on both axes
        piece[held].x, y = b + (dx, dy) of held     snap to j's anchor
        PzPieces_MergeGroups(j); a = b; break       the first such j in index order
cursor = blank (resource 4)
PzPieces_MoveGroup(a.x, a.y)
piece[held].placed = 1; effect 1 (prio 3); PzPieces_RedrawWork()
cursor = hand; PzPieces_SetMouseBounds(0); free the piece cursor
PzPieces_AddZones(); held = 0x0b
```

* **Groups**: every piece starts in its own group (group = index). `PzPieces_MergeGroups(j)` gives
  both groups the smaller number: all pieces whose group is the larger number get the smaller
  (DS:b5d6 = the smaller). A group number is therefore always the smallest index of its members.
  When the two groups are already equal the renaming uses a stale register (the piece index j) as
  the "old" number; since a group number is its smallest member index this renames nothing [O code,
  I proof]. A group is never split again.
* **The group move** `PzPieces_MoveGroup(ax, ay)`:
  1. Keep the group inside the work area: for each member i of group g in index order, with the
     running anchor: x_i' = ax + dx_i, y_i' = ay + dy_i; if x_i' < 93 then ax += 93 − x_i', else if
     x_i' + w_i > 312 then ax −= x_i' + w_i − 312; the same for y with 14 and 156.
  2. For each member i in index order: step = ((ax + dx_i − x_i) / 5, (ay + dy_i − y_i) / 5)
     (signed, truncated toward 0); when the step is not (0,0): **5 times** x_i, y_i += step and
     `PzPieces_RedrawWork`; then x_i, y_i = (ax + dx_i, ay + dy_i), `PzPieces_RedrawWork`, and the
     zone position = (x_i, y_i).
  So **dropping any member of an assembly moves the whole assembly** to stay aligned with the
  dropped piece: the members slide one after another, 5 redraws each.
* `PzPieces_RedrawWork` 3a4a:053f: `Gfx_RestoreRect(93,14,219,142)`, then every placed piece that is
  wholly inside the work area, keyed, in index order (higher index on top), flush.
* The slides are **not paced** (one redraw and flush per step, as fast as the machine runs) [O]. The
  piece in hand from the box is not yet "placed" during the move, so it is not drawn while it moves;
  the cursor is the blank picture meanwhile, so it simply appears at the end [O].
* After the first drop the mouse bounds are the whole screen less the hand's size (0..304, 0..182)
  [O]. These are the standard bounds: `Mouse_DrawCursor` calls `Mouse_SetBounds(0, 0, 319, 199)`
  whenever the active cursor record changes, so every later cursor change recomputes them for the
  new cursor's size (`code_runtime.md` §7.3).

### 2.8 Picking up a placed piece [O 3a4a:0265]

```
i = code - 0x45b
PzPieces_RemoveZones(); Gfx_RestoreRect(93,14,219,142)
held = i
draw every placed piece except i (no inside test, no flush)
cursor record from i's picture, hot spot (w/2, h/2)
clamp g_mouseX/Y: x < 93 -> 94; y < 14 -> 15; x + w > 312 -> 312 - w - 1; y + h > 156 -> 156 - h - 1
cursor = it; effect 0 (prio 3); PzPieces_SetMouseBounds(1); flush
```

The piece keeps placed = 1 and its group while in the hand. The hot spot puts zone tests at the
piece's centre; the drop position is still the top-left = mouse.

### 2.9 Success and the reveal [O 3a4a:08f6]

`PzPieces_CheckSolved` returns 0 unless piece 0 is placed and every piece 1..n−1 is placed with the
same anchor as piece 0. Then, with A = anchor(0) and the machine picture M:

```
Timer_Set(7, 200)                                   overwritten below: no effect
M keyed at A; Gfx_SaveToBack(93,14,219,142)         back page = pieces + machine
fill (93,14,219,142) with colour 0x76; draw all placed pieces
effect 0x13 (prio 5)
for y = A.y while y < A.y + M.h, y += 5:
    Gfx_RestoreRect(A.x, y, M.w, 5); Timer_Set(7,10); Timer_Wait(7); flush
fill the work area with colour 0x76; Gfx_SaveToBack(work area); M keyed at A   (flushed later)
PzSnd_Stop(0x13); return 1
```

The machine is revealed top to bottom, 5 rows per 10 ticks; afterwards the work area holds only the
machine on plain colour 0x76, and the back page is plain, so a later pick-up redraws the pieces on
plain ground without the machine [O]. The caller then runs `Puzzle_ShowSolved(3)` (its flush shows
the final machine) and `Puzzle_AwardPoints(0x16)`.

### 2.10 State check [O, §8]

`w0/20_puzzle_solved` (variant 2): pieces 0 (31×7, offset (0,0)) at (190,24), 1 (11×100, (10,6)) at
(200,30), 2 (11×22, (10,101)) at (200,125), all group 0, placed, box 0, nothing held: one anchor
(190,24). The tour dropped piece 2 at (200,126), anchor (190,25), which snapped by 1 px.

### 2.11 Hint text [O 3a4a:00ba..0178]

The text is `sprintf(buffer, T 0x20b1, name)` with name = the string DS:357c[v] (a machine name with
its article), into a block of `strlen(format) + strlen(name)` bytes (enough, because `%s` itself is
replaced), shown with `Puzzle_ShowHintText` and freed. The format text is the same for all 20
variants; only the name differs. Simple machines does not use these names (part A §6.7).

### 2.12 Sounds and timing [O]

| Effect (bank 0x11ca) | When | Prio |
|---|---|---|
| 0 | piece picked up | 3 |
| 1 | piece dropped | 3 |
| 4 | piece out of the box (cursor set) | 3 |
| 0xc / 0xd | box opens / closes | 3 |
| 0x12 | Hint | 5 |
| 0x13 | reveal (stopped at its end) | 5 |
| 0x15 | Go Back | 5 |
| 3..6 | jingle (`Puzzle_ShowSolved(3)`) | 4 |
| 0x16 | points | 5 |

Timing: box 4 × 10 ticks; reveal 10 ticks per strip of 5 rows, ⌈machine height / 5⌉ strips (e.g.
25 strips, 250 ticks, for variant 2's 123-row machine); slides and everything else unpaced.

### 2.13 Quirks to keep [O]

* The rand() retry loop (§2.6): the number of calls depends on the draws.
* The busy-wait after a take (§2.6) blocks all input codes.
* Dropping one member drags the whole assembly; groups never split (§2.7).
* After solving, the pieces stay movable but the machine picture vanishes at the next pick-up
  (§2.9).
* The `Timer_Set(7, 200)` before the reveal has no effect.
* The pressed Hint button's last column stays drawn (§1.2).

---

## 3. Gears and buttons (kind 3, ovl22 3b2f)

A board with a fixed crank, pegs, horizontal and vertical racks and buttons. The player places small
and large gears from an inventory on the pegs and turns the crank by dragging its handle round. The
gears turn each other, toothed rack segments are moved by the gears, and the racks press buttons.
All buttons must be pressed **during one drag**: when the mouse button is released and not every
button is down, everything springs back.

### 3.1 Resources (`PzGears_LoadVariant` 3b2f:1b83, `PzGears_LoadResources` 3b2f:1bd5) [O]

| Id | Use | Kept in |
|---|---|---|
| F 0x2002 + v | variant data (§3.2), private copy | DS:b6d2 (grid DS:b7da, objects DS:b7d6) |
| 0x2021 / 0x2022 | screen (320×157) and palette | — |
| bundle 0x2023 | small gear, 6 rotation frames (22×20, 22×19) | DS:b6ea |
| bundle 0x2024 | large gear, 8 rotation frames (43×40) | DS:b6ee |
| bundle 0x2026 | crank, 24 frames (63×56) | DS:b6da |
| bundle 0x2025 | crank with the hand on the handle, 24 frames (shown while turning) | DS:b6de |
| bundle 0x2027 | buttons: (unpressed, pressed) × 4 placements (§3.4) | DS:b6e6 |
| bundle 0x2028 | digits 0..9 (10×10) | DS:b6f2 |
| bundle 0x2029 | rack segments: 0 toothed horizontal (7×10), 1 plain horizontal (7×5), 2 toothed vertical (10×7), 3 plain vertical (5×7) | DS:b6d6 |
| 0x1095 | pressed Hint | DS:b6fe |
| T 0x202b + v | hint text (31) | DS:b6fa |
| T 0x202a | panel text | DS:b6f6 |
| resources 3, 4 | hand (DS:b6ca); 1×1 blank (DS:b6ce, while turning) | |
| effect bank 0x11c8 | sounds (§3.10) | |

### 3.2 Variant data: F 0x2002 + v (31 entries, 478..1298 bytes) [O, `gears.py`]

```
u8  grid[12][23]          0x114 bytes: non-zero = a peg (a gear may be placed on this cell)
u16 n                     at +0x114: objects (10..51)
rec obj[n]                at +0x116: 20 bytes = 10 i16 each
```

| Word | Field |
|---|---|
| +0 | type (below) |
| +2 | state: rotation frame of a gear or the crank; 1 = pressed for a button; for racks the code stores the home position here at start (§3.3) |
| +4 | column: gears, crank, vertical racks and vertical buttons in cells (9 px); horizontal racks and buttons in **units of 3 px**; −99 = a gear not on the board |
| +6 | row: in cells; vertical racks and buttons in units of 3 px; −99 with +4 |
| +8 | length of a rack in segments (6 px = 2 units each); for gears and the crank a saved frame (run time) |
| +0xa | run time: direction during a propagation (−1, 0, +1) |
| +0xc | run time: visited during a propagation |
| +0xe | run time: state saved before a crank step |
| +0x10, +0x12 | run time: position saved before a crank step |

Data fields are +0, +2 (start frame of a gear, 0 for a button), +4, +6 and +8 of racks. The shipped
files carry leftover values in +8 of gears and buttons and in +0xe..+0x12 of some records (in
variants 0, 1, 2, 3, 4 and 26); the code overwrites them before use, except that the redraws before
the first crank step compare a button's state with its +0xe (§3.4), which is harmless because every
leftover there is positive [O, `gears.py`].

| Type | Object | Notes |
|---|---|---|
| 0 | small gear | 6 frames, radius 10 |
| 1 | large gear | 8 frames, radius 20 |
| 2 | horizontal rack, plain segment | |
| 3 | vertical rack, plain segment | |
| 4 | horizontal rack, toothed segment | meshes with gears |
| 5 | vertical rack, toothed segment | meshes with gears |
| 6 | crank | always object 0 |
| 7 | button at the left end of a horizontal rack | pressed when the rack moves left onto it |
| 8 | button at the right end of a horizontal rack | pressed when the rack moves right onto it |
| 9 | button below a vertical rack | pressed when the rack moves down onto it |
| 10 | button above a vertical rack | pressed when the rack moves up onto it |

Shipped facts [O]: object 0 is the crank (type 6) in all 31 variants and no other object has type 6;
all gears start in the inventory (column −99), none on the board; every button starts unpressed
(state 0); 2..5 buttons; a rack is a chain of plain and toothed segments on one row (or column);
some rack segments start at row or column −1 or at negative 3-px units (left of or above the
board).

### 3.3 State [O]

| DS | Type | Meaning |
|---|---|---|
| b6d2 | far | the F copy; b7da = grid (same address), b7d6 = obj (+0x116) |
| b7e0 | i16 | n |
| b702 | i16[2] | gears in the inventory per type (`PzGears_CountStock` 3b2f:0033: type ≤ 1 with column −99) |
| b7de | i16 | object index of the gear in the hand (also left set after it is placed) |
| b6c6 | far | cursor record of the gear in the hand. **"A gear is carried" ⇔ the current cursor (DS:0a6c, the record pointer of `g_cursorRegionDefault`) equals DS:b6c6** |
| b7e2 | i16 | jam flag of a crank step |
| b7e6, b7e4 | i16 | crank centre x = 9·c₀ + 97, y = 9·r₀ + 40 (c₀, r₀ = crank cell) |
| b706 + 8·k | rec[26] | crank handle rectangle {x0, y0, x1, y1} on screen for crank frame k − 1: DS:37f4 record k + (9·c₀ + 67, 9·r₀ + 10) (`PzGears_InitCrankRects` 3b2f:079b) |
| b818 | zone | crank handle zone: rect b706[frame + 1], priority 99, press 0x44c (`PzGears_SetCrankZone` 3b2f:081e) |
| b7e8 | far | zone[100] (`Mem_Alloc(0x898)`): one circle zone per placed gear |
| b6e2 | far | crank picture set in use (0x2026; 0x2025 while turning) |

At start `PzGears_SaveHome` (3b2f:1da8) stores, per object type: gears +8 = frame; horizontal racks
+2 = column; vertical racks +2 = row (the **home** positions used by the reset, §3.9).

### 3.4 Board geometry and drawing [O 3b2f:024f, 01e4, tables DS:38c4..38e8]

Cells are 9 px; cell (c, r), 0 ≤ c < 23, 0 ≤ r < 12, has its peg dot at x = 95 + 9c .. 98 + 9c,
y = 39 + 9r .. 41 + 9r (`PzGears_DrawPeg`: three short lines, 2, 4 and 2 px). `PzGears_DrawScreen`
draws **all 276 dots**, colour 7 where the grid byte is non-zero and colour 1 (the board colour)
elsewhere, before saving the back page.

| Object | Picture | Top-left on screen |
|---|---|---|
| gear type t at (c, r) | member `frame` of 0x2023 / 0x2024 | (picX[t] + 9c, picY[t] + 9r); picX = DS:38c4 (86, 75), picY = DS:38c8 (31, 21); centre = top-left + radius[t] (DS:38d4: 10, 20) |
| crank at (c, r) | member `frame` of the crank set in use | (67 + 9c, 10 + 9r) |
| horizontal plain at (u, r), length L | 0x2029 member 1, segment k = 0..L−1 | (96 + 3u + 6k, 43 + 9r) |
| horizontal toothed | member 0 | (96 + 3u + 6k, 40 + 9r) |
| vertical plain at (c, u) | member 3 | (99 + 9c, 39 + 3u + 6k) |
| vertical toothed | member 2 | (97 + 9c, 39 + 3u + 6k) |
| button 7 at (u, r) | 0x2027 member state | (96 + 3u, 34 + 9r) |
| button 8 | member state + 2 | (94 + 3u, 34 + 9r) |
| button 10 at (c, u) | member state + 4 | (91 + 9c, 41 + 3u) |
| button 9 | member state + 6 | (91 + 9c, 39 + 3u) |

A rack segment chain occupies, in its 3-px unit axis, [u, u + 2L).

`PzGears_Draw(sounds)` 3b2f:024f:

```
Gfx_RestoreRect(84, 1, 226, 160)                    back page (board, pegs)
clip = x 86..308, y 23..152                         DS:01e4 / 01e6 / 01e8 / 01ea, restored at the end
for i = n-1 down to 0:                              types 0..6, back to front
    obj[i].dir = 0; obj[i].visited = 0              (clears the propagation fields of every object)
    draw by type; a gear with column -99 is not drawn
allDown = 1; newDown = 0
for i = 1 .. n-1 with type >= 7:                    buttons on top, in index order
    if state > saved state (+0xe): newDown = 1
    else if state == 0: allDown = 0
    draw the button
if sounds: if allDown: effect 4 (prio 2) else if newDown: effect 3 (prio 2)
restore the clip; flush
```

"saved state" is the value `PzGears_TurnCrank` saved before the step (§3.7), so effect 3 means "a
button went down in this step" and effect 4 plays on every redraw while all buttons are down.

The inventory (`PzGears_DrawInventory` 3b2f:00a9), for type 1 then 0: two digits of 0x2028, tens at
(tensX[t], tensY[t]) and units at (unitsX[t], unitsY[t]) (DS:38d8..38e6), then frame 0 of the gear
picture at (invX[t], invY[t]) (DS:38cc / 38d0) when the count is non-zero, else
`Gfx_RestoreRect` of that picture's rectangle; flush.

### 3.5 Set-up and input [O 3b2f:1ed0; switch CS:206e, 11 codes; 0x4b0..0x513 tested after]

```
PzGears_LoadVariant(v)          F copy; crank rectangles; crank zone added
PzGears_LoadResources(v)
PzSnd_LoadBank(0x11c8); cursor = hand
Puzzle_InitPanel(T 0x202a)      BEFORE the screen picture: its 157 rows cover the panel's top 2 rows
PzGears_DrawScreen()            screen at (0,10), pegs, Gfx_SaveToBack, PzGears_Draw(1),
                                PzGears_CountStock, PzGears_DrawInventory, Screen_FadeOutIn
PzGears_AddZones()              zone array; DS:37b2, 37c8, 37de, 38ec
PzGears_SaveHome()
Hint_Show(0x1052)
```

| Zone | Shape | Priority | Code |
|---|---|---|---|
| DS:37b2 board | rect (84,10) 226×151 | 0 | 0x44f |
| DS:37c8 large gear in the inventory | circle, centre (53,109), radius 20 | 0 | 0x44d |
| DS:37de small gear in the inventory | circle, centre (52,139), radius 10 | 0 | 0x44e |
| DS:38ec Hint | rect (23,24) 36×15 | 0 | 0x3e8 |
| DS:b818 crank handle | rect of the current frame | 99 | 0x44c |
| gear i on the board | circle at its centre, radius 10 or 20 | 10 | 0x4b0 + i |

| Code | Action |
|---|---|
| 0x44d / 0x44e | `PzGears_TakeFromStock(1 / 0)` (§3.6) |
| 0x44f | `PzGears_Place(g_mouseX, g_mouseY)` (§3.6; nothing unless a gear is carried) |
| 0x44c | a gear is carried: `PzGears_Place(g_mouseX, g_mouseY)`. Else: `PzGears_SaveFrames` (gears and crank +8 = frame), `PzGears_DragCrank` (§3.7); then, if not yet solved: `PzGears_AllButtonsPressed` → solved: `Puzzle_ShowSolved(5)`, `Puzzle_AwardPoints(0xb)`; not solved: `PzGears_Reset` (§3.9) |
| 0x4b0 + i | `PzGears_PickUp(i)` (§3.6) |
| 0x3e8 Hint, 0x5dd, 0x0d, 0x5de, 0x1b, 0x17a2/3 | §1.2 |

After a success further drags neither check nor reset [O].

### 3.6 Gears into and out of the hand, placement rules [O 3b2f:0a22, 09b3, 0b18, 0c2b, 0892]

* **Take** `PzGears_TakeFromStock(t)`: if a gear is carried, put it back (`PzGears_PutBack`, below)
  whatever circle was clicked. Else, if stock[t] = 0, nothing. Else the first object of type t with
  column −99 becomes the gear in hand (b7de); its cursor = picture member `frame` of its bundle, hot
  spot (radius, radius); stock[t] −= 1; **the mouse is moved** to (invX[t], invY[t]) so that the
  carried gear covers the inventory picture; effect 0 (prio 1); cursor = it; inventory redrawn.
* **Put back** `PzGears_PutBack`: stock[type of b7de] += 1; the mouse is moved to the inventory
  picture's centre (invX + radius, invY + radius); effect 1 (prio 1); cursor = hand; the gear cursor
  freed; inventory redrawn. The object keeps column −99.
* **Pick up** `PzGears_PickUp(i)` (code 0x4b0 + i; nothing while a gear is carried): cursor from i's
  current frame, hot spot (radius, radius); the mouse is moved to the gear's top-left; i's column
  and row = −99 (the stock count is not changed: a carried gear is in neither place); b7de = i;
  effect 0 (prio 1); its zone removed; cursor = it; `PzGears_Draw(1)`.
* **Place** `PzGears_Place(mx, my)` (nothing unless a gear is carried), t = type of b7de:
  ```
  (px, py) = (mx + radius[t], my + radius[t])           the carried gear's centre
  c = (px - 91) / 9; r = (py - 34) / 9                  signed division, truncated toward 0
  refuse unless 0 <= c < 23, 0 <= r < 12 and grid[r][c] != 0
  refuse if (c - c0)^2 + (r - r0)^2 < minDist2[t][0]    the crank counts as a small gear
  partner = -1
  for j in index order, j != b7de, column(j) != -99, type(j) <= 1:
      d2 = dx^2 + dy^2 (cells); refuse if d2 < minDist2[t][type j]
      if |dx| <= 4 and |dy| <= 4 and mesh[t][type j][|dy|][|dx|]: partner = j     (the last wins)
  if partner != -1 and (frame & 1) == (frame(partner) & 1):
      frame += 1, wrapping to 0 at frames[t]            teeth interleave visually
  column, row = c, r; PzGears_AddGearZone(b7de)
  move the mouse to (px, py); effect 1 (prio 1); cursor = hand; free the gear cursor
  PzGears_Draw(1)
  ```
  `minDist2` DS:36e2 is i16[2][2] (squared cell distances); `mesh` DS:36ea is i16[2][2][5][5]
  indexed [type a][type b][|dy|][|dx|], 1 where two gears of those types mesh (small-small at
  distance 2, small-large 3, large-large 4, plus a few diagonal offsets). `frames` DS:38e8 = 6, 8.
  The engine reads both tables from the EXE. The code reads the grid byte before the range test
  (an out-of-range cell reads beyond the grid); the result is a refusal either way [O].
  Racks are not checked at placement: a gear may be placed over a rack [O: the rack comparisons at
  3b2f:0dce..0e0b have empty branches].
* A refused placement does nothing at all (no sound): the gear stays in the hand.
* While a gear is carried, a press over a placed gear's circle (priority 10) gives 0x4b0 + i, which
  is ignored: gears cannot be dropped onto another gear's circle. A press on the crank handle
  (priority 99) is a placement attempt.

### 3.7 Turning the crank [O 3b2f:19bf, 185c]

`PzGears_DragCrank` (entered by a press on the handle zone, nothing carried):

```
remove the handle zone; cursor = blank (resource 4); crank set = 0x2025 (hand on the handle)
PzGears_Draw(0)
prev = (g_mouseX, g_mouseY)
while g_mouseButtons != 0:                              busy loop, no Input_GetCode
    cur = (g_mouseX, g_mouseY); if cur == prev: continue
    frame0 = crank frame
    q(p) = quadrant of p around the crank centre (cx, cy) = (DS:b7e6, DS:b7e4):
           0 if x > cx and y < cy; 1 if x > cx and y >= cy; 2 if x <= cx and y > cy; 3 otherwise
    d = (q(cur) - q(prev)) & 3
    d == 1: PzGears_TurnCrank(-1)             clockwise across a quadrant border
    d == 3: PzGears_TurnCrank(+1)             counter-clockwise
    d == 2: nothing
    d == 0: s(p) = (p.y - cy)·100 / (p.x - cx)  (16-bit, truncated; 0x7fff when p.x == cx)
            s(cur) < s(prev): TurnCrank(+1); s(cur) > s(prev): TurnCrank(-1); equal: nothing
    if the crank frame changed: move the mouse to the centre of rectangle b706[frame + 1]
                                 ((x0+x1)/2, (y0+y1)/2) and prev = that point
    else prev = cur
cursor = hand; PzGears_SetCrankZone() (new frame); crank set = 0x2026; PzGears_Draw(0)
```

The slope comparison makes any motion around the centre count: within a quadrant, clockwise on
screen increases s. One mouse event gives at most one step; the warp keeps the pointer on the
handle. No timer is involved: the crank turns as fast as the mouse moves.

`PzGears_TurnCrank(dir)` (dir −1 = clockwise, +1 = counter-clockwise):

```
for every object: types 0, 1, 6..10: +0xe = state; all: +0x10 = column, +0x12 = row   (save)
dir == -1: crank frame += 1, 24 -> 0;  dir == +1: crank frame -= 1, -1 -> 23
jam = 0
obj[0].dir = dir; obj[0].type = 0                   the crank acts as a small gear
PzGears_Propagate(0)
obj[0].type = 6
effect 0 (prio 1); PzGears_Draw(1)
if jam:
    restore every saved field (states of types 0, 1, 6..10; positions of all), effect 0 (prio 1),
    PzGears_Draw(1)
```

A jammed step is drawn once moved and then once restored (a visible flick), and the crank does not
turn [O].

### 3.8 Propagation: the drive rules [O 3b2f:17c7, table DS:3902, 3b2f:0ecc..16b2]

Direction convention: **d = −1 clockwise, +1 counter-clockwise** (on screen) for gears; for
horizontal racks +1 = right, for vertical racks +1 = down. Whenever a gear (or the crank) gets a
direction d in a step, its frame changes by **−d** (wrapping at its frame count): **one frame per
crank step whatever its size** (no gear ratios).

`PzGears_Propagate(i)`: visited(i) = 1; for each j ≠ i in index order with column(j) ≠ −99:
f = driveTable[type i][type j] (DS:3902, near pointers, 6 rows × 11 columns; 0 = no relation); if
f and f(i, j) returns 1 and not visited(j): `PzGears_Propagate(j)`. Depth first. Only rows 0..5 are
ever used (the source is always a gear, the crank as type 0, or a rack; button handlers return 0);
the words after row 5 are other data.

| Source \ target | gear 0/1 | h rack 2/4 | v rack 3/5 | buttons 7/8 | buttons 9/10 |
|---|---|---|---|---|---|
| gear (0, 1) | GearToGear | toothed 4 only: GearToHRack | toothed 5 only: GearToVRack | — | — |
| h plain (2) | — | HRackToHRack | — | HRackToButton | — |
| h toothed (4) | HRackToGear | HRackToHRack | — | HRackToButton | — |
| v plain (3) | — | — | VRackToVRack | — | VRackToButton |
| v toothed (5) | VRackToGear | — | VRackToVRack | — | VRackToButton |

Each handler sets the jam flag DS:b7e2 = 1 when it finds a contradiction and returns 1 when j is
linked (so it is visited next). In what follows dir(j) = 0 means "not yet driven in this step";
"move j by s" means dir(j) = s and j's position coordinate += s (only when dir(j) was 0).

* **GearToGear(i, j)** 3b2f:0ecc: dx = |Δcolumn|, dy = |Δrow|; not linked unless dx ≤ 4, dy ≤ 4 and
  mesh[type i][type j][dy][dx]. If dir(j) ≠ 0: jam when dir(j) = dir(i). Else: dir(i) = −1 →
  dir(j) = +1 and frame(j) − 1 (wrapping to frames − 1); any other dir(i) → dir(j) = −1 and
  frame(j) + 1 (wrapping to 0). Linked.
* **GearToHRack(i, j)** 3b2f:13bf, t = type(i) (0 or 1): Δ = row(j) − row(i); the rack is below
  when Δ = t + 1 (then s = dir(i)), above when Δ = −(t + 2) (then s = −dir(i)); otherwise not
  linked. Let u' = column(j) − dir(j) + s (the rack's position as if moved by s from where it was
  before this step). Not linked unless u' ≤ 3(column(i) + t) + (t xor 1) and
  3(column(i) − t) ≤ u' + 2·length(j). Then: if dir(j) = 0 move j by s; jam if dir(j) ≠ s; jam if
  column(j) < 0. Linked.
* **GearToVRack(i, j)** 3b2f:15c3: the same across: Δ = column(j) − column(i); right of the gear when
  Δ = t + 1 (s = −dir(i)), left when Δ = −(t + 2) (s = dir(i)); range test on rows; move by s; jam if
  dir(j) ≠ s or row(j) < 0.
* **HRackToGear(i, j)** 3b2f:14ae, t = type(j): Δ = row(i) − row(j); Δ = t + 1 → s = dir(i);
  Δ = −(t + 2) → s = −dir(i); range: column(i) ≤ 3(column(j) + t) + (t xor 1) and
  column(i) + 2·length(i) ≥ 3(column(j) − t) (the rack's current position). If dir(j) = 0: dir(j) =
  s and frame(j) −1 when s = +1, +1 otherwise (wrapping). Jam if dir(j) ≠ s. Linked.
* **VRackToGear(i, j)** 3b2f:16b2: across: Δ = column(i) − column(j); Δ = t + 1 → s = −dir(i);
  Δ = −(t + 2) → s = dir(i); range on rows; as HRackToGear.
* **HRackToHRack(i, j)** 3b2f:0ff9, same row only: a = column(i) − dir(i) (i before its move),
  b = a + 2·length(i), e = column(j).
  If e < a (j on the left): not linked when type(i) = type(j) and dir(i) > 0, or when
  e + 2·length(j) < a. Else (e ≥ a): not linked when type(i) = type(j) and dir(i) < 0, or when
  e > b. Otherwise: if dir(j) = 0 move j by dir(i); jam if dir(j) ≠ dir(i). Linked.
  So segments of **different** types (plain and toothed) touching end to end are rigidly joined;
  segments of the **same** type only push in the direction of motion [O code, I purpose].
* **VRackToVRack(i, j)** 3b2f:129b: the same with rows, same column only.
* **HRackToButton(i, j)** 3b2f:111d, same row only, a = column(i) − dir(i):
  type 7 (left button at column u): a = u + 7 and dir(i) > 0 → state(j) = 0; a = u + 8 and
  dir(i) < 0 → state(j) = 1. Type 8 (right button at u): with e = a + 2·length(i), e = u + 1 and
  dir(i) < 0 → state 0; e = u and dir(i) > 0 → state 1. Returns 0 (buttons do not propagate).
* **VRackToButton(i, j)** 3b2f:11dc, same column only, a = row(i) − dir(i): type 10 (above, at row
  u): a = u + 8 and dir(i) > 0 → 0; a = u + 9 and dir(i) < 0 → 1. Type 9 (below): e = a +
  2·length(i); e = u + 1 and dir(i) < 0 → 0; e = u and dir(i) > 0 → 1. Returns 0.

A button is pressed when the rack's end crosses its edge in the pressing direction and released only
when the end crosses back; racks are not stopped by buttons or by the board edge (only a toothed rack
driven directly by a gear jams below position 0) [O]. A toothed segment stays driven only while it
overlaps the gear's reach, so a rack moves at most about its toothed length and then disengages
(§8: variant 0's upper rack moves 9 units and stops).

### 3.9 Success and the reset [O 3b2f:0000, 1e46]

`PzGears_AllButtonsPressed`: 1 when every object of type ≥ 7 has state ≠ 0. It is evaluated only
when a drag ends (button released). When it fails, `PzGears_Reset`:

| Type | Reset |
|---|---|
| 0, 1, 6 (gears, crank) | frame = +8 (the frame at the start of the drag, `PzGears_SaveFrames`) |
| 2, 4 | column = +2 (home) |
| 3, 5 | row = +2 (home) |
| 7..10 | state = 0 |

then the handle zone is re-made for the restored crank frame and `PzGears_Draw(1)`. Placed gears
stay placed. So the buttons must all be down at the moment of release; the order does not matter.

### 3.10 Sounds and timing [O]

| Effect (bank 0x11c8) | When | Prio |
|---|---|---|
| 0 | gear taken or picked up; every crank step (twice when jammed) | 1 |
| 1 | gear placed or put back | 1 |
| 3 | a button went down in this step (redraws with sounds) | 2 |
| 4 | all buttons down (every such redraw) | 2 |
| 9 | Hint | 5 |
| 0xa | Go Back | 5 |
| 5..8 | jingle (`Puzzle_ShowSolved(5)`) | 4 |
| 0xb | points | 5 |

No timer at all in this overlay: every update is driven by input [O: no call of 152c in ovl22].

### 3.11 Quirks to keep [O]

* The drag loop reads no input codes: keys typed while turning wait in the queue; global keys do not
  act until the drag ends.
* The mouse is moved by the game when taking, picking up, placing and putting back gears and while
  turning (§3.6, §3.7).
* The screen picture is drawn after the panel and covers its top 2 rows (§3.5).
* Gear ratios are not modelled (§3.8); a jammed step flicks.
* `PzGears_RectContains` 3b2f:1991 has no caller.
* A carried gear is dropped silently by Go Back or a menu exit; its cursor record is not freed.

---

## 4. Balance (kind 4, ovl14 32fe)

A beam with five hooks on each side (arms 1..5 from the pivot). The right side carries preset
weights; the player hangs weights from an inventory on the left hooks (up to three per hook) until
the torques are equal: the beam then swings to level and the puzzle is solved.

### 4.1 Resources (`PzBalance_Run` 32fe:0aa0) [O]

| Id | Use | Kept in |
|---|---|---|
| D 0x211b | variant table (§4.2), private copy | DS:ab28 |
| 0x2119 / 0x211a | screen (320×156) and palette | — |
| 0x211e + t | beam picture for tilt index t = 0..10 (265×146, keyed), read into the shared buffer when drawn | — |
| bundle 0x2129 | weights with their hook, 20 px tall, 6 types: inventory and carried cursor | DS:ab34 |
| bundle 0x212a / 0x212b | hanging weights, 13 px tall, left side / right side | DS:ab3c / ab38 |
| bundle 0x212c | inventory count pictures for 0..8 (7×7) | DS:ab40 |
| 0x211c | 1×2 ring drawn above each hanging weight | DS:ab30 |
| 0x211d | 4×3 hook drawn below the last of 1..2 left weights | DS:ab2c |
| 0x1095 | pressed Hint | DS:ab4c |
| T 0x212d + v | hint text (32) | DS:ab48 |
| T 0x214d | panel text | DS:ab44 |
| resource 3 | hand | DS:ab56 |
| effect bank 0x11c6 | sounds (§4.7) | |

### 4.2 Variant data: D 0x211b [O 32fe:0000, `balance.py`]

```
u16 count                 32
rec[32]                   21 i16 each (42 bytes), record v at 2 + 42·v
  [0..5]   inventory: number of weights of each type 0..5
  [6..20]  right hooks 5..9 (arms 1..5), 3 slots each: weight type or -1, filled from slot 0
```

`PzBalance_LoadVariant(v)`: g_balanceStock DS:ad64[6] = words 0..5; hooks 0..4 (left) = −1;
hooks 5..9 = words 6..20. Shipped: 1..5 weights in the inventory, every variant solvable with them
(brute force) [O].

**Weight types** (initialised data DS:25cc, 6 records of 18 bytes, `g_balanceWeights`): +0 mass
(1, 2, 3, 4, 5, 10 for types 0..5), +2/+4 inventory picture x, y, +6/+8 count picture x, y,
+0x0a..+0x10 inventory zone x, y, w, h. The engine reads the table from the EXE.

### 4.3 State [O]

| DS | Type | Meaning |
|---|---|---|
| ad28 + 6·h + 2·k | i16[10][3] | hook h (0..4 left, arm 5 − h; 5..9 right, arm h − 4), slot k (top first): weight type or −1 |
| ad64 | i16[6] | inventory count per type |
| ab24 | i16 | **tilt** −5..5: negative = right side down, 0 = level; starts at −5 |
| ab20 | i16 | 1 when the right side was heavier as the beam left level (direction of a swing from level) |
| ab22 | i16 | hook zone set in use (0 for tilt −5, 1 for +5, 2 for level; −1 at start) |
| ab26 | i16 | weight type in the hand, −1 none |
| ab50 | i16 | solved |
| ab52 | far | cursor record of the carried weight |
| ab5a + 22·(5s + i) | zone[3][5] | left hook i in set s: rect (hx − 10, hy − 10) 20×67, priority 0x4b, press 0x461 + 5s + i |
| aca4 + 22·i | zone[6] | inventory weight type i: rect from DS:25cc, priority 0x4b, press 0x45a + i |

Hook positions (initialised data DS:2638, `g_balanceHooks`): i16 [11 tilts][10 hooks][x, y]; tilt
index = ab24 + 5. Only the three resting tilts −5, 0, +5 have zones (sets 0, 1, 2 use tilt indexes
0, 10 and 5); only the left hooks have zones [O].

### 4.4 Screen and drawing [O 32fe:0375, 02c8, 0235]

Set-up order: tilt −5, nothing held, set −1, unsolved; `PzSnd_LoadBank(0x11c6)`; resources;
`PzBalance_LoadVariant(v)`; cursor = hand; zones DS:27fc (inventory area (8,67) 38×93 → 0x459),
DS:2812 (beam area (53,14) 265×146 → 0x454, which has no handler), DS:2828 (Hint);
`PzBalance_InitZones` (the six inventory zones, set 0); screen at (0,10); `Puzzle_InitPanel(T
0x214d)`; `Gfx_SaveToBack(0,0,320,200)`; `PzBalance_DrawInventory`; `PzBalance_DrawBeam`;
`Screen_FadeOutIn`; `Hint_Show(0x1052)`.

`PzBalance_DrawBeam`:

```
tilt > 4:  select hook zones 1, tilt = 5         (PzBalance_SelectHookZones(set, 1): removes the old
tilt < -4: select hook zones 0, tilt = -5         set's 5 zones, adds the new set's, when they differ)
tilt == 0: select hook zones 2
Res_Read(0x211e + tilt + 5); Lpx_DrawKeyed(53, 14, it)        the beam with its background
for hook h = 0..9: PzBalance_DrawHook(hookX[t][h], hookY[t][h], hook list h, right = h >= 5)
flush
```

`PzBalance_DrawHook(x, y, list, right)`: for each weight w in the list (up to 3, stopping at −1):
ring 0x211c at (x, y); weight picture member w of 0x212b (right) or 0x212a (left) at (x + dx[w],
y + 2) with dx = DS:27f0 (i16[6]); y += 15. On the left side, when 1 or 2 weights hang, the hook
0x211d is drawn at (x − 1, y) below the last one (where another weight can go) [O].

`PzBalance_DrawInventory`: `Gfx_RestoreRect(8,67,38,93)`; for each type with a count > 0: picture
member type of 0x2129 at (+2, +4) of its DS:25cc record and count picture member count of 0x212c at
(+6, +8); flush.

### 4.5 Input [O, switch CS:0a2c, 29 codes]

| Code | Action |
|---|---|
| 0x45a..0x45f (type = code − 0x45a) | nothing carried and stock[type] > 0: `PzBalance_HoldWeight(type)` (cursor = member type of 0x2129, hot spot (w/2, 0), effect 0 prio 3), stock −= 1, inventory redrawn, held = type. Something carried: as 0x459 |
| 0x459 | something carried: puzzle cursor back, the weight cursor freed, stock[held] += 1, inventory redrawn, held = −1 |
| 0x461..0x465, 0x466..0x46a, 0x46b..0x46f (hook = code − base) | carried: `PzBalance_Hang(hook)`; else `PzBalance_Unhang(hook)`. Then `PzBalance_Settle`: if not yet solved its result is stored and on 1: `Puzzle_ShowSolved(2)`, `Puzzle_AwardPoints(0xd)`; if already solved it runs without storing. Then `Input_Flush` |
| 0x458 Hint, 0x5dd, 0x0d, 0x5de, 0x1b, 0x17a2/3 | §1.2 |

* `PzBalance_Hang(hook)`: first free slot of the hook; if all 3 are taken nothing happens (the
  weight stays in the hand). Else the slot = held, cursor back and freed, `PzBalance_DrawBeam`,
  held = −1, effect 1 (prio 3).
* `PzBalance_Unhang(hook)`: the last weight of the hook (if any) goes into the hand
  (`PzBalance_HoldWeight`), its slot = −1, `PzBalance_DrawBeam`.
* The hot spot (w/2, 0) makes the hook zones test the weight's ring at its top centre.

### 4.6 Torques and the beam motion [O 32fe:043b, 0494, 04f0, 0566, 05eb, 06b9]

```
L = Σ over hooks 0..4 and their weights of mass · (5 - hook)
R = Σ over hooks 5..9 of mass · (hook - 4)

PzBalance_Compare():
    if tilt != 0:
        if (tilt < 0 and L < R) or (tilt > 0 and L > R): return 0       already on the heavier side
        if L == R: return -1                                            come to level
    else:
        if L == R: return 0
        ab20 = (L < R)
    return (L < R ? 100·L / R : 100·R / L) + 1                          16-bit, 1..100

PzBalance_Settle():
    r = PzBalance_Compare()
    r == 0:  return 0
    r == -1: PzBalance_SwingLevel(); return 1
    else:    PzBalance_Swing(r); return 0
```

* `PzBalance_SwingLevel`: step = +1 if tilt < 0 else −1; for t from the current tilt while t ≠ 0:
  `Timer_Set(0, 50)`, tilt = t, `PzBalance_DrawBeam`, the swing effect (below), `Timer_Wait(0)`,
  t += step; then tilt = 0 and a last `PzBalance_DrawBeam`. From ±5: 5 steps of 50 ticks
  (~1.72 s); the first step redraws the starting tilt.
* `PzBalance_Swing(r)`: from level: step = −1 and target −5 when ab20 (right heavier), else +1 and
  +5; from a tilt: step = +1 if tilt < 0 else −1, target = −tilt (the other end). For t from the
  current tilt while t ≠ target: `Timer_Set(0, r / 2)` (truncated), tilt = t, draw, swing effect,
  `Timer_Wait(0)`, t += step; then tilt = target, draw, effect 0xa (prio 4). **The nearer the sides
  are to balance, the slower the swing** (r/2 = 0..50 ticks per step; an empty side gives r = 1,
  0 ticks). From one end to the other: 10 steps.
* The swing effect: step > 0: `PzSnd_SetTranspose(8, n)` then effect 8 (prio 3) with n = 0, 1, 2 …
  per step (rising); step < 0: `PzSnd_SetTranspose(9, n)` then effect 9 with n = 0, −1, −2 …
  (falling) [O].
* Hook zones follow the beam only at −5, 0 and +5 (`PzBalance_DrawBeam`).
* **Solving**: only a swing to level makes `PzBalance_Settle` return 1. The beam starts at −5 and
  every shipped variant has R > 0 = L at the start, so level is reached only by balancing [O data].
  Once solved, further changes still move the beam but the puzzle stays solved.

### 4.7 Sounds and timing [O]

| Effect (bank 0x11c6) | When | Prio |
|---|---|---|
| 0 | weight taken into the hand | 3 |
| 1 | weight hung | 3 |
| 8 / 9 | each swing step, transposed by the step number, rising / falling | 3 |
| 0xa | swing finished at an end | 4 |
| 0xb | Hint | 5 |
| 0xc | Go Back | 5 |
| 2..5 | jingle (`Puzzle_ShowSolved(2)`) | 4 |
| 0xd | points | 5 |

Timing: `Timer_Set(0, n)` / `Timer_Wait(0)` per beam step (slot 0, the frame slot, unused otherwise
by the puzzle), n = 50 or r/2; nothing else is timed.

### 4.8 State check [O, §8]

`r2/puzzle/k4_*` (variant 0: one weight of mass 3 in the inventory, a mass-3 weight on right hook 7,
arm 3): screen tilt −5, set 0; `k4_unbalanced`: the weight on left hook 0 (arm 5, L = 15 > R = 9),
tilt +5, set 1, ab20 still 0 (a swing from an end does not set it); `k4_solved`: the weight on hook 2
(arm 3), tilt 0, set 2, solved.

---

## 5. Magnets (kind 7, ovl16 35bb)

A table with the outline of a target shape. The player places bar magnets and horseshoe magnets from
an inventory, rotating them with the ROTATE button while carrying them. After every placement or
removal a pole-force simulation moves the magnets: unlike poles attract and stick, like poles repel.
The puzzle is solved when every outline has a magnet of its kind and orientation on it.

### 5.1 Resources [O 35bb:1c03, 1a8f]

| Id | Use | Kept in |
|---|---|---|
| D 0x11a1 | targets of the 30 levels (§5.3), private copy | DS:af60 |
| 0x119f / 0x11a0 | screen (320×156) and palette | — |
| bundle 0x11a4 | magnets, member kind·4 + orientation (12; 26×12, 13×25, 26×23, 24×25) | DS:af6c |
| bundle 0x11a5 | their shadows, same members, drawn at (+2, +2) | DS:af68 |
| bundle 0x11a6 | target outlines (6): bar horizontal, bar vertical, horseshoe orientations 0..3 | DS:af64 |
| bundle 0x11a3 | digits 0..9 (red, 7 px tall) | DS:af70 |
| bundle 0x11a2 | ROTATE button: 0 normal, 1 pressed (61×25) | DS:af74 |
| 0x1095 | pressed Hint | DS:af80 |
| T 0x11a7 + level | hint text (30, by level) | DS:af7c |
| T 0x11c5 | panel text | DS:af78 |
| resource 3 | hand | DS:af88 |
| effect bank 0x11d0 | sounds (§5.9) | |

Magnet **kinds**: 0 bar, 1 horseshoe, 2 horseshoe with the poles swapped. Orientations 0..3 are four
quarter turns (for bars 0 and 2 are both horizontal with the poles swapped, 1 and 3 vertical) [O
pictures].

### 5.2 Initialised tables [O]

| DS | Format | Use |
|---|---|---|
| 2ae4 | u8[30] | level + 1 for each variant (a permutation of 1..30: **the variant number is not the level**) |
| 2a8a | s8[30][3] | inventory per level: number of magnets of kind 0, 1, 2 |
| 29fa | s8[12][6][2] | poles per picture kind·4 + orientation: 6 (dx, dy) offsets from the magnet's top-left; poles 0..2 are one polarity (class N), 3..5 the other (class S); (−1, −1) = no pole. Bars have 3 poles at each end, horseshoes 2 at each tip and pole 2 and 5 absent |
| 29e2 | i16[3][2] | inventory picture x, y per kind (orientation 0) |
| 29ee | i16[3][2] | inventory count x, y per kind |
| 298a, 29a0, 29b6, 29cc | zones | inventory (8,47) 58×75 → 0x459; table (70,23) 239×128 → 0x456; Hint → 0x458; ROTATE (3,124) 61×29 → 0x45a; all priority 0 |

The class names N/S follow the pictures (bar orientation 0 shows N on the left, where poles 0..2 are)
[I]; only "same class" and "other class" matter to the code.

### 5.3 Level data: D 0x11a1 (9660 bytes) [O 35bb:1c03, 1b59, 16f7, `magnets.py`]

```
i16 count[30]                       targets per level (3..25)
rec target[30][40] at +0x3c         8 bytes each: i16 x, i16 y, i16 a, i16 b
```

b = 0 for a bar target, 1 for a horseshoe target; a = the orientation (bars: 0 horizontal,
1 vertical; horseshoes 0..3). The outline picture is member 2b + a of 0x11a6, drawn at (x, y).
Unused records are zero. For every level the inventory has at least as many bars as bar targets and
as many horseshoes (kinds 1 and 2 together) as horseshoe targets; some levels have spare magnets
[O data].

`PzMagnets_Run(v)`: level = DS:2ae4[v] − 1 selects the inventory, the targets and the hint text;
the history flag and the dispatch use v.

### 5.4 State [O]

| DS | Type | Meaning |
|---|---|---|
| af52 | far | 64 magnet records of 0x12 bytes (`Mem_Alloc(0x480)`), below |
| af4a | i16 | kind in the hand, −1 none |
| af48 | i16 | orientation in the hand |
| af4c | i16[3] | inventory counts (from DS:2a8a at start) |
| af58 / af5c | far | target counts / target records in the D copy |
| af84 | far | cursor record of the carried magnet |
| af56 | i16 | set to 0 at start, never read |

Magnet record: +0 kind (−1 = free slot), +2 orientation, +4 x, +6 y (top-left of the magnet
picture), +8 drawn (draw pass), +0x0a fx, +0x0c fy (force accumulators, i16, wrapping), +0x0e
attached, +0x10 matched (success check).

### 5.5 Screen and set-up [O 35bb:1c03, 1b59, 01ac, 0456]

```
level = DS:2ae4[v] - 1; PzSnd_LoadBank(0x11d0)
D 0x11a1 copy; 64 free slots (PzMagnets_Init: nothing held, orientation 0, all kinds -1)
hint text 0x11a7 + level; panel text 0x11c5; PzMagnets_LoadResources; cursor = hand
zones DS:298a, 29a0, 29b6, 29cc
screen at (0,10); Puzzle_InitPanel(T 0x11c5)
PzMagnets_DrawTargets(level): inventory counts from DS:2a8a; every target outline on the screen
Gfx_SaveToBack(0,0,320,200)             the outlines become part of the background
PzMagnets_DrawInventory(); Screen_FadeOutIn(); Hint_Show(0x1052)
```

`PzMagnets_DrawInventory`: `Gfx_RestoreRect(8,47,58,75)`; for each kind with a count > 0: its
orientation-0 picture at DS:29e2[kind] and the count with `PzMagnets_DrawNumber` at DS:29ee[kind]
(tens digit only when ≥ 10, the units digit 1 px right of it); flush.

`PzMagnets_DrawTable` (painter's order):

```
Gfx_RestoreRect(70,23,239,128); every slot: drawn = 0
repeat:
    for each used slot i not drawn, in index order:
        if no other used, undrawn slot j "covers" i:          PzMagnets_CanDraw
            shadow (0x11a5 member) at (x+2, y+2); magnet (0x11a4 member) at (x, y); drawn = 1
until every used slot is drawn
flush
```

j covers i when the rectangle (x_j, y_j, w_j + 2, h_j + 2) (j with its shadow) overlaps
(x_i, y_i, w_i − 2, h_i − 1) (`PzMagnets_Overlap(j, i, 0)`), sizes from the 0x11a4 picture. Shadows
therefore fall under the magnets to their right and below. Overlap = `PzMagnets_RectsOverlap`, the
usual positive-area intersection [O]. Two magnets covering each other would loop for ever; the
placement rules keep magnets from overlapping enough for that [I].

### 5.6 Input [O, switch CS:1a1f, 10 codes]

| Code | Action |
|---|---|
| 0x459 inventory | nothing carried: `PzMagnets_TakeFromInventory`; carried: `PzMagnets_PutBack` |
| 0x456 table | nothing carried: `PzMagnets_PickUp`; carried: `PzMagnets_Drop`. Then, if not yet solved, `PzMagnets_CheckSolved(level)` and on success `Puzzle_ShowSolved(6)`, `Puzzle_AwardPoints(0xb)`. Then `Input_Flush` |
| 0x45a ROTATE | carried: pressed picture (0x11a2 member 1) at (3,128), flush, wait while `g_mouseButtons` ≠ 0, `PzMagnets_Rotate`, `Gfx_RestoreRect(3,128,61,25)`, flush. Else nothing |
| 0x458 Hint, 0x5dd, 0x0d, 0x5de, 0x1b, 0x17a2/3 | §1.2 (Hint: picture, effect 4, flush, wait, restore 36 columns) |

* `PzMagnets_TakeFromInventory`: the first kind whose orientation-0 picture rectangle at DS:29e2
  contains (`g_mouseX`, `g_mouseY`) and whose count > 0: held = kind, orientation = 0,
  `PzMagnets_HoldMagnet(kind, 0, picture x, picture y)`, effect 2 (prio 3), count −= 1, inventory
  redrawn.
* `PzMagnets_HoldMagnet(kind, o, x, y)`: cursor record from picture kind·4 + o of 0x11a4, hot spot
  (0, 0), **the mouse is moved to (x, y)** (so the carried magnet appears exactly where it was),
  cursor = it.
* `PzMagnets_PutBack`: count[held] += 1, held = −1, `PzMagnets_DropCursor(1)`, inventory redrawn.
* `PzMagnets_DropCursor(offset)`: the mouse is moved to (`g_mouseX` + 5, `g_mouseY` + 5) when offset,
  else to where it is; cursor = hand; the magnet cursor freed; effect 3 (prio 3).
* `PzMagnets_PickUp`: the first used slot (index order) whose picture rectangle contains the mouse:
  held = its kind, orientation = its orientation, `PzMagnets_HoldMagnet(kind, o, x, y)`, effect 2
  (prio 3), the slot freed, `PzMagnets_Simulate` (the others react to the removal).
* `PzMagnets_Drop`: (x, y) = mouse; refused (nothing happens) when x < 70, y < 23,
  x + w + 2 > 309 or y + h + 2 > 151 (the picture with its shadow must lie on the table), or when
  (x, y, w − 2, h − 1) overlaps (x_j, y_j, w_j − 2, h_j − 1) of any used slot, or when all 64
  slots are used. Else the first free slot = {held kind, orientation, x, y}, held = −1,
  `PzMagnets_DropCursor(1)`, `PzMagnets_Simulate`.
* `PzMagnets_Rotate`: `PzMagnets_DropCursor(0)` (cursor freed, effect 3), orientation = (orientation
  + 1) mod 4, `PzMagnets_HoldMagnet(held, orientation, g_mouseX, g_mouseY)`, effect 5 (prio 3).

### 5.7 The simulation [O 35bb:113c, 08ae, 06ba, 0efc, 0aa4, 0c78, 09a5, 05be, 06aa]

`PzMagnets_Simulate`:

```
PzMagnets_DrawTable()
every slot: attached = 0, fx = 0, fy = 0
PzSnd_PlayLoop(1, 5)                                   the hum
rounds = 0
repeat:
    moved = 0
    for substep s = 0..3:
        Timer_Set(0, 4)
        for each used slot i in index order: PzMagnets_Forces(i)
        if PzMagnets_Step(s): moved = 1
        PzMagnets_DrawTable()
        Timer_Wait(0)
    rounds += 1
until moved == 0 or rounds == 16
PzSnd_Stop(1)
```

At most 64 substeps of 4 ticks (~1.76 s); it ends early after a whole round without motion.

**Forces** `PzMagnets_Forces(i)`: for each pole p of i that exists (dx byte ≠ −1), at
P = (x_i + dx, y_i + dy), and each other used slot j: if i is attached, stop (nothing more is added
for i); else `PzMagnets_PoleForce(class(p) = N, P, i, j)`.

`PzMagnets_PoleForce(n, P, i, j)`: sx = sy = 0 (locals); for each of j's 6 poles q at
Q = (x_j + dx_q, y_j + dy_q) — **absent poles included, at (x_j − 1, y_j − 1)**:

```
same = (class(q) == class(p))
(dx, dy) = same ? P - Q : Q - P              repel away from Q, attract toward Q
dist = PzMagnets_ISqrt(dx^2 + dy^2)          32-bit argument
if (|dx| == 1 and dy == 0) or (dx == 0 and |dy| == 1):     poles 1 px apart
    if not same: magnet i: attached = 1, fx = 0, fy = 0; return   (accumulators zeroed, sx/sy dropped)
    (like poles 1 px apart fall through to the force)
if dist < 18:
    k = (18 - dist)^3                        PzMagnets_Cube, 16-bit
    sx += (k·dx) / dist; sy += (k·dy) / dist        16-bit product, signed division, truncated
after the 6 poles: magnet i: fx += sx, fy += sy     (i16 wrapping)
```

`PzMagnets_ISqrt(n)`: 1 for n < 4 (so a distance of 0 gives 1 and no division by zero); otherwise a
binary search with low = 2, high = n/2, first guess n/4 + 1, guess = low + (high − low)/2 (unsigned
32-bit compares of guess² with n), stopping when the low words of high and low differ by at most 1,
returning low (or the exact root when guess² = n). **Correction (wave 3):** the routine returns 2,
not floor(√n), for n = 65 540 and 65 541: high = n/2 = 32 770 gives a low-word difference of 0x8000,
and the **16-bit** abs of −32 768 is ≤ 1, which ends the loop (`isqrt.py` used Python's abs and
missed these two); the next failures are at n = 131 074..131 079 (the loop test sees the low word of
n/2 and stops at once). Squared distances of 65 540 and 65 541 do occur on the table (poles up to
about 271 px apart, n < 74 000), so the engine keeps the literal routine [O wave 3, unit test
"magnets square root": the misses are exactly {65540, 65541}]. Since dist ≥ |dx|, the 16-bit
products k·dx stay below 11 000 and never wrap [O, arithmetic].

The accumulators are **not cleared between substeps**: forces add up until a move is attempted
(below). An attached magnet receives no force for the rest of the simulation (attached is cleared
only when a simulation starts).

**Step** `PzMagnets_Step(s)` — first a horizontal pass over all 64 slots, then a vertical pass:

```
for each slot i (index order), horizontal:
    v = fx
    go = |v| > 0x200 or (|v| > 0x40 and s odd) or (|v| > 0 and s == 1)
    if unused: continue
    if go and not attached and not PzMagnets_TouchesOpposite(i): move
    else if PzMagnets_TouchesSame(i, 1): move          (sets fy = ±1, the cross nudge)
    else: continue                                      (fx kept for the next substep)
    move: d = (fx < 0) ? -1 : +1; x += d
          if PzMagnets_Blocked(i): x -= d else moved = 1
          fx = 0
then the same vertically with fy, y and the thresholds 0x40 / 0x10 (instead of 0x200 / 0x40),
using PzMagnets_TouchesSame(i, 0), which sets fx = ±1
return moved
```

So within a round of 4 substeps a strong force moves a magnet 4 px, a medium one 2 px (substeps 1
and 3) and a weak one 1 px (substep 1). **Vertical motion has thresholds 8 times lower than
horizontal** [O; whether intended is unknown].

* `PzMagnets_TouchesOpposite(i)`: some pole p of i (absent ones included, at (x_i − 1, y_i − 1)) is
  orthogonally 1 px from an existing pole of the other class of another used magnet.
* `PzMagnets_TouchesSame(i, vertical)`: the same with an existing pole of the same class. On the
  first hit: vertical = 1 (called from the horizontal pass): y += 1, fy = −1 if that position is
  blocked else +1, y −= 1; vertical = 0 (from the vertical pass): the same with x and fx. Returns 1.
  So like poles in contact push the magnet along the other axis in the next pass, toward a free side
  [O code, I purpose].
* `PzMagnets_Blocked(i)`: picture with shadow off the table (x < 70, y < 23, x + w + 2 > 309,
  y + h + 2 > 151) or (x, y, w − 2, h − 1) overlapping another used slot's shrunk rectangle.

Note the asymmetric use of absent poles: as a *source* of force on i they count (j's phantom N and
S at the same point cancel exactly in the force, but the opposite one can attach i when i's pole is
1 px from it); as a pole *of i* they are skipped in the forces but tested in both contact tests [O].
Keep these as coded: they decide where magnets stick.

### 5.8 Success [O 35bb:16f7]

```
every slot: matched = 0
for each target t of the level (index order):
    best = none; bestd = 0x3fff + 0x3fff
    for each used slot m (index order):
        notBar = (kind > 0); o = orientation mod (notBar ? 4 : 2)
        ddx = |x - t.x|, ddy = |y - t.y|
        if not matched(m) and t.b == notBar and t.a == o and ddx < 5 and ddy < 5
           and ddx + ddy < bestd: best = m, bestd = ddx + ddy
    if best == none: return 0
    matched(best) = 1
return 1
```

Each target needs its own magnet of the right family (bar, or either horseshoe kind) and orientation
(bars modulo 2: the poles' order does not matter) within 4 px on both axes; extra magnets on the
table do not matter [O]. The check runs after every table action, after the simulation.

### 5.9 Sounds and timing [O]

| Effect (bank 0x11d0) | When | Prio |
|---|---|---|
| 1 | simulation hum (looping, stopped at its end) | 5 |
| 2 | magnet taken into the hand | 3 |
| 3 | magnet put down or back; also at every ROTATE | 3 |
| 5 | ROTATE | 3 |
| 4 | Hint | 5 |
| 0xa | Go Back | 5 |
| 6..9 | jingle (`Puzzle_ShowSolved(6)`) | 4 |
| 0xb | points | 5 |

Timing: simulation substeps `Timer_Set(0, 4)` … `Timer_Wait(0)` (this is the "4 ticks" ARCHITECTURE
§3.3 cites at 35bb:119f); everything else unpaced.

### 5.10 State check [O, §8]

`r2/puzzle/k7_*` (variant 0 = level 0: three bar targets at y 82, three bars in the inventory):
`k7_rotated_placed`: a horizontal bar at (160,83) and a vertical one at (184,83), both attached;
`k7_repelled`: an N-S bar and an S-N bar pushed apart to x 151 and 193 (like poles facing);
`k7_solved`: three horizontal bars at x 160, 184, 208, y 83, all attached and matched (each target
within 1 px).

---

## 6. Timing summary [O]

| Where | Timer | Duration |
|---|---|---|
| main loops of the four puzzles | none | unpaced polling, drawing on change |
| machine pieces: box | slot 7, `Timer_Wait` | 4 × 10 ticks |
| machine pieces: group slides | none | 5 redraws per moving member |
| machine pieces: reveal | slot 7, `Timer_Wait` | 10 ticks per 5 rows |
| gears | none | input-driven (crank steps follow mouse events) |
| balance: to level | slot 0, `Timer_Wait` | 50 ticks per tilt step (5 steps from an end) |
| balance: swing | slot 0, `Timer_Wait` | (100·lighter/heavier + 1)/2 ticks per step, 5 or 10 steps |
| magnets: simulation | slot 0, `Timer_Wait` | 4 ticks per substep, at most 16 × 4 substeps |
| busy-waits | none | Hint and ROTATE button release; machine pieces after a take; gears' drag |
| rewards | effect waits | part A §2.2 |

`Timer_Wait` ends at tick ≥ deadline: a `Timer_Set(n)` / `Timer_Wait` pair lasts n ticks from the
set (part A §7).

## 7. rand() summary [O]

| Puzzle | Calls |
|---|---|
| machine pieces | one per try in `PzPieces_TakeFromBox` (≥ 1 per piece taken out of the box), then 1 for the jingle |
| gears, balance, magnets | 1, the jingle (`Puzzle_ShowSolved`) |

Checked: `calls` between `k3/k4/k7_screen` and `_solved` = 1; `k2_screen` → `k2_solved` (2 pieces) = 3;
`w0/17` → `18` = 1, `18` → `20` = 5 (§2.6). No other code runs during a puzzle (the room, chimps and
actors are not ticked), so a seeded run stays in step as long as the same takes and the solve happen.

## 8. Reference captures and state checks

| Capture / state | What was checked |
|---|---|
| `w0/17_puzzle`, `18_puzzle_piece1`, `20_puzzle_solved` | kind 2 variant 2; DS:b51e table, box count, held 0x0b, groups and anchors; rand calls 1 and 5 (§2.6, §2.10) |
| `r2/puzzle/k2_screen`, `k2_solved` | kind 2 variant 0; 3 rand calls |
| `r2/puzzle/k3_screen`, `k3_gear_placed`, `k3_solved` | kind 3 variant 0: home positions in +2; the large gear placed at (11,5); after the solving drag the crank at frame 5, the gear at frame 5 (8-frame wrap), the upper toothed rack +9 units (disengaged at 36 = 3·(11+1)), the lower +4 (34 = 3·11 + 1), both buttons pressed (§3.8). Wave 3: the solved state is 19 counter-clockwise, 1 clockwise and 1 counter-clockwise steps (21 steps, net 19); the last step moves both racks back onto their limits and presses button 9. `k3_04_one_button` is the moment after 19 counter-clockwise and 1 clockwise steps (racks 35 and 33). The documented input "two turns, release", posted as capture_r2.py's 49 moves, ends one step short at the k3_04 state (crank frame 6) when no move is dropped [O wave 3, the gears solve and posted-drag tests] |
| `r2/puzzle/k4_screen`, `k4_unbalanced`, `k4_solved` | §4.8 |
| `r2/puzzle/k7_screen`, `k7_rotated_placed`, `k7_repelled`, `k7_solved` | §5.10 |
| frames `w0/17..20`, `r2/puzzle/k2..k7_*` | screen layout, box and count at (5,82) / (37,130), inventory positions, hook and weight positions, buttons, hand cursor |

## 9. Porting notes [R]

* Read every table from the EXE (DGROUP, `ssg.exe` file offset 0xFC70 + DS): the zone records
  DS:353a..3566, 27fc..2828, 298a..29cc, 37b2..37de, 38ec; the machine names DS:357c → DS:35cc..;
  the balance weights DS:25cc, hooks DS:2638, weight offsets DS:27f0; the magnet poles DS:29fa,
  inventory DS:2a8a, level map DS:2ae4, inventory positions DS:29e2 / 29ee; the gear tables DS:36e2,
  DS:36ea, DS:37f4 and DS:38c4..38e8. The drive table DS:3902 holds code addresses: the engine
  implements the mapping of §3.8 in code. Variant data comes from F 0x209c + v, bundle 0x2088 + v,
  F 0x2002 + v, D 0x211b and D 0x11a1.
* Reproduce the integer arithmetic literally: 16-bit wrapping of the magnet force accumulators,
  16-bit products (balance ratio, crank slopes), truncation toward zero in every division (piece
  slide steps, gear cell snapping, magnet forces, crank slopes), the magnet square root's 1 for
  n < 4 (an exact floor square root otherwise, §5.7).
* The puzzles poll without pacing; the engine's poll pumps the host and must not add frame waits.
  The busy-waits (§6) need host pumping too. Unpaced animations (piece slides, gear steps) should
  present one frame per redraw.
* Mouse warps (gears, magnets) must move the host-side pointer model and the guest-visible
  `g_mouseX/Y` together; with a real host pointer the engine may prefer a virtual pointer while a
  puzzle warps.
* rand(): §7. The retry loop of the machine-pieces box must call rand() exactly as often.
* Quirks to keep: §2.13, §3.11, the balance swing speed and the transposed swing effects, the magnet
  vertical/horizontal threshold asymmetry and the phantom poles, the 35-column Hint restore of
  machine pieces and balance.

## 10. Open questions

1. Machine pieces: how the unpaced group slides look on the reference machine (a capture of a
   multi-piece assembly being moved would show the step count per frame). (The mouse bounds are
   answered in §2.7.)
2. Gears: a capture of a jammed step (the forward-and-back flick) and of a drag that ends with only
   some buttons pressed (the reset), to confirm the redraw sequence and the effects 3/4.
3. Balance: a capture of a slow swing (sides nearly equal) to confirm r/2 ticks per step. (The
   transpose is specified in `code_sound.md` §11: `Snd_SetTranspose` sets the section's transpose
   and the live one when it is the current section; notes already sounding keep their pitch.)
4. Magnets: whether `PzMagnets_DrawTable` can ever deadlock with shipped levels (two magnets
   covering each other); a capture of horseshoe magnets (levels 9+) to confirm the pole tables and
   the cross-axis nudge of like poles.
5. *Answered by `code_ui.md` §8:* `TextView_Run` (the Hint viewer) does not freeze the tick
   counter; none of these four puzzles has an animation running across it.
