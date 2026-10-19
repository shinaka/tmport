# DOS edition (SST.EXE): world grid, cell handlers, scenes, camera and scrolling

Wave 1 logic spec, topic "world". Scope:

* segment `1568` (`cells.c`, 70 functions) and the world part of segment `176a` (`cells2.c`:
  path links, level-2 gate, castle creature; 17 functions). The end screens, chest, treasure
  room and crown sequence in `176a` (`Cell_P64_EndScreen`, `Cell_P20_CastleGate`, `Cell_P70_TreasureRoom`, `Shack_*`,
  `Cell_P71_V71_ToRankScreen`, `Cell_P72_RankScreen`, `Game_AdvanceEnd`, `Treasure_IsCollected`) belong to the player
  doc; §6.10 lists only where they put the player and the camera;
* segment `1437` (`scene.c`): `Pal_Apply`, `Scene_Update`, plus `Scene_LoadPan` (1460:05e8);
* camera and scrolling: `Frame_Begin`, `Playfield_Redraw`, `Scroll_Update` (130f), the camera
  part of `Game_Tick` (130f:00fe) and `Player_Move` (18cf:15f0), the transition dissolve of
  `Frame_Present` (130f:01c2), and the segment-1000 routines `Dirty_AddRect`, `Scroll_Setup`,
  `Scroll_PlayfieldLeft/Right/Up/Down`, `Tile_DrawColumn`;
* the 16×16 grid: which rows each scene uses, the level loops, transitions and landing points.

Evidence: `re/dos/ghidra_export/` (disassembly for every claim about widths, signedness and
immediates; decompiled C only as a reading aid), DGROUP bytes of `extracted/SST_unpacked.EXE`,
the PAN/PAK decoders in `tools/dos/`, the reference captures (`REFERENCE_CAPTURES.md`). Scratch
scripts: `extracted/scratch/dos_w1_world/` (`exe.py` DS reader, `tables.py` handler tables,
`grids.py` grid dump, `seams.py` PAN block comparison, `fn.py` function dumper, `gh/` Ghidra
repair). Names: the canonical names of `re/names/dos/` (wave 2 merge, decision table in
`re/names/dos/README.md`).

Notation: **[O]** observed in code, bytes, decoded files or captures; **[I]** inferred.
`SSSS:OOOO` = Ghidra address (file offset `(SSSS-0x1000)*16 + OOOO + 0x21F0`). `DS:XXXX` =
DGROUP (file offset `0x3FB30 + XXXX`). Numbers are hex unless written as decimal. All words are
16-bit little-endian. "u" = unsigned compare (JC/JNC/JA/JBE), "s" = signed compare
(JL/JGE/JLE/JG) in the original; when a comparison is listed without a tag the operand range
makes the two equivalent. Code levels are 0/1/2; the player sees them as levels 1/2/3.

---

## 1. Module overview

| Seg | Module | Functions | Role |
|---|---|---|---|
| `1568` | `cells.c` | 70 | the two cell dispatchers, the object handlers of 58 of the 75 cell types (17 are in `176a`) and all visible handlers except type 71's, decoration sprites, level boundaries, net shop, bonus spot, castle wall, creature hit |
| `176a` (part) | `cells2.c` | 17 of 35 | path links (conveyors), level-2 gate, castle creature spawn |
| `1437` | `scene.c` | 2 | palette set-up, scene loading |
| `130f` (part) | `main.c` | 3 + parts of 2 | camera latch, full play-field redraw, incremental scrolling; screen offset of the player (`Game_Tick`), transition dissolve (`Frame_Present`) |
| `1000` (part) | `gfx.asm` | 7 | scroll shifts, tile column drawing, dirty rectangles |
| `18cf` (part) | `player.c` | camera part of `Player_Move`, `Obj_WrapWorldX` | |

Everything the cell handlers touch in other modules (player states, animation scripts, elves,
sound) is referenced by address; the semantics of those are in the other wave-1 docs.

---

## 2. Runtime context used by these modules

### 2.1 Objects

The player and the elves are "objects" with the same first fields [O]:

| Off | Meaning |
|---|---|
| +0 | world x (pixels, 0..0x7FF in a row; see §3.2) |
| +2 | world y (pixels; row = y >> 7; an object stands on a path when `y & 0x7F == 0x70`) |
| +4 | dx: player = movement request in **bytes** (applied as `x += 2*dx`), elf = pixels per tick |
| +6 | dy: movement request in pixels |

The player is `g_player` DS:37E2 (so `g_playerDX` = DS:37E6, `g_playerDY` = DS:37E8); the elves
are `g_elves` DS:37FA + 32·i (i = 0..`DS:A212`−1). `g_curObj` DS:8FAB is a far pointer to the
object being processed. Handlers test "is the player" by comparing the far pointer with
`DS:37E2` (segment DGROUP, offset 0x37E2) [O, e.g. 1568:0008].

`g_playerDX/DY` are summed by `Anim_Tick` (4306:0103/0129) from the animation script of slot 1 and
consumed (zeroed) by `Player_Move` (18cf:1766) [O]. So they are non-zero only on ticks when the
player's animation advanced a frame.

### 2.2 Sprite slots used by the cells

`g_spriteSlots` DS:8FD6, 13 slots of 22 bytes [O `Anim_Start` 4306:088e, `Anim_Clear` 4306:092f]:
+0 x (screen bytes), +2 y (screen rows), +4 depth (draw order key), +6 frame code (PAK frame·2,
bit 0 = mirror; −1 = nothing), +8 draw-record index, +0xA current animation (0 = none),
+0xC last animation, +0xE script position, +0x10 delay.

| Slot | DS | Use here |
|---|---|---|
| 1 | 8FEC | player (x = screen byte of the player, written by `Player_Move`) |
| 3 | 9018 | showcase treasure (`Cell_V49_Showcase`) |
| 6 | 905A | castle creature (`Castle_PlaceCreature`), crown sequence |
| 7 | 9070 | second creature sprite |
| 8 | 9086 | treasure-room extra sprite (`Cell_V49_Showcase`) |
| 10 | 90B2 | treasure-room extra sprite (`Cell_V49_Showcase`) |
| 12 | 90DE | **cell decoration**: the one static or animated decoration of the visible cells |

`Anim_Set(slot, anim)` (4306:07fa): if the slot's current animation differs, stop it and start
`anim` (script table DS:4F5A). `Anim_SetIfIdle(slot, anim)` (4306:0784): only if the slot has no
animation, store `anim` as current. `Anim_Clear(slot)` (4306:092f): animation, last animation,
delay, position = 0, frame = −1. `Anim_InitSlots` (4306:02c0) clears all. Animation op −6 sets
`g_creatureStrike` DS:9264 [O 4306 names].

### 2.3 Sound

`Sfx_Play(id)` 491b:06d7, `Sfx_Stop` 491b:074b, `Music_Play(tune)` 491b:0791, `Music_Stop`
491b:0811 (`fmt_sound.md` §7). `g_area` DS:910B is the music area id saved in the record; the
handlers that change it also start the matching tune themselves (area < 4 → tune 0, < 7 → tune 1,
< 15 → tune 2, `fmt_sound.md` §7) [O].

### 2.4 Flags shared with the frame loop

| DS | Name | Set by | Meaning |
|---|---|---|---|
| 924F | `g_fullRedrawReq` | handlers (`INC` or `=1`) | request a full play-field redraw at the next frame (latched by `Frame_Begin` into `g_fullRedraw` DS:8FBB) |
| 9217 | `g_transition` | handlers (1 or 2) | teleport in progress: `Player_Update` is skipped, the camera snaps (§8.3), `Frame_Present` dissolves (fast CPU) and clears it |
| 9278 | (search partial redraw) | `Search_Update` | redraw only the searched cell (latched into DS:8FC8, §8.5) |
| 926C | `g_menuLock` | handlers | blank the menu bar when entering scene 3/4 (§7.1) |
| 910D | `g_stepDone` | `Anim_Tick`, `Player_Move` | one logic step per drawn frame (ARCHITECTURE §3) |
| 9274 | `g_cellCallKind` | dispatchers | 1 = visible call, 2 = object call |
| 92C9 | `g_cellMask` | handlers | terrain mask being built |
| 92CB | `g_curCell` | `Cell_GetMask` | cell index of the object (object calls only) |
| 8FC0 | `g_cellScreenX` | `Cells_UpdateVisible` | screen x (bytes) of the left edge of the visible cell being handled |
| 8FC2 | `g_cellScreenY` | `Cells_UpdateVisible` | screen y of the top edge of the visible cell row |
| 8FC4 | `g_decorCount` | decoration helpers | number of slot-12/creature placements this tick |
| 90FA | `g_castleCellIdx` | `Castle_CreatureSpawn` | counts visible castle cells this tick |
| 9287 | `g_creatureCell` | `Castle_CreatureSpawn` | 0 = no creature, else the `g_castleCellIdx` where it appeared |
| 923C / 8FA5 / 3D31 | `g_slowCpu` / saved flag / `g_slowScroll` | §4.6 | slow-CPU mode |

### 2.5 Tick order (47.3 Hz, `Game_Tick` 130f:00fe)

When `g_stepDone` is clear at the start of the tick [O]:

1. `DS:8FA7 = (player.x − camX) >> 1` (arithmetic shift) and `DS:8FA9 = player.y − camY`; if
   `DS:8FA7 > 0x400` (u) — the player is in another row than the camera — then
   `DS:8FA7 += ((player.y & 0xFF80) − (camY & 0xFF80)) << 3` and
   `DS:8FA9 = (player.y & 0x7F) − (camY & 0x7F)` (130f:0112..0158). These are the player's
   screen offsets in bytes/rows *before* this tick's movement.
2. `Mouse_Poll`, `Input_Tick`, `Anim_Tick`.
3. Only when `g_loadedScene` DS:8FBE == `g_level` DS:928F (or `g_uiMode` == 0x80):
   `Input_MouseWalk`, `Player_Update` (→ state handler → `Player_Move` → `Cell_GetMask` for the
   player), `Elves_Update` (→ `Cell_GetMask` per elf), `Cells_UpdateVisible`.

When `g_stepDone` is set: only `Anim_Tick` and `Input_Tick`. `Scene_Update` sets `g_quit` = 1
while it loads a scene, which makes `Game_Tick` return at once (130f:00fe) [O 1437:00e6/020a].

---

## 3. The world grid

### 3.1 Tables [O]

| DS | Size | Layout | Saved | Content |
|---|---|---|---|---|
| 0EAA `g_cellType` | 256 B | `type[row*16+col]`, signed char (read with CBW) | no | cell type 0..73 |
| 0CAA `g_cellPicture` | 256 words | same index | partly | PAN block number in bits 0..6 (`AND 0x7F` at every use) |
| 0FAA `g_cellContent` | 256 words | same index | partly | hiding-place content (layout doc) |
| 08A6 `g_cellPlayerHandlers` | 75 far ptrs | entry t = object handler of type t (types 0..74) | – | §5 |
| 09D2 `g_cellVisibleHandlers` | 74 far ptrs | entry t = visible handler of type t | – | §5 |
| 11AA `g_hidingCells` | 48 words | cell indices of the hiding places: 0..12 level 2, 13..28 level 1, 29..47 level 0 | via record +0xBB/+0xF4 | layout doc |
| 120A `g_variableCells` | 9 words | cells with a game-variable picture: 26 79 84 88 AE B7 BC EA EF | pictures at record +0xEB..0xF3 | §3.5 |
| 121C `g_variableCellDefaults` | 9 words | default picture of each `g_variableCells` entry | – | §3.5 |
| 1252 `g_castleLayouts` | 4 × 20 words | castle ladder layouts, 5 rows × 4 cells | layout id at record +0x166 | §3.5 |
| 0AFA `g_creatureMask` | 4 words | `rand()` mask per tier | – | §6.8 |

The far pointers in DGROUP are not relocated in the file: segment `0568` means `1568`, `076A`
means `176A` [O]. Example values: `type[0x02] = 12`, `picture[0x02] = 0x45`,
`g_variableCellDefaults = 004B 0026 002F 0026 0038 002F 0047 0047 0040`,
`g_creatureMask = 0000 01FF 00FF 003F` [O].

Visible handler 1568:0007 is a lone `RETF` (`Null_Handler`, also 153e:02a7) [O]. Entry 74 of the
object table exists (also `RETF`) but no cell has type 74 [O].

### 3.2 Coordinates and cell index

* A row of the grid is 16 cells × 128 px = world x 0..0x7FF; row r spans y `0x80*r .. 0x80*r+0x7F`.
* **Cell index** (`Cell_GetMask` 1568:1fe4, `Cells_UpdateVisible` 1568:1f22, `Frame_Begin`):
  `colpart = ROL16(v & 0xFF80, 1)` then swap bytes, i.e. `((v >> 7) & 0xFF) | ((v >> 15) << 8)`;
  index = `colpart(x) + (colpart(y) << 4)` [O]. For 0 ≤ x, y < 0x800 this is `(y>>7)*16 + (x>>7)`.
  A negative x gives a column of 0x100+ (e.g. x = −1 → 0x1FF). The index is **not** masked: an
  out-of-range index reads past DS:0EAA (DS:0FAA = content words) [O; never happens in play I].
* **Strip wrap** (`Obj_WrapWorldX` 18cf:17c9, objects only, called by `Player_Move` and
  `Elves_Update`): if `x >= 0x8000` (u, i.e. negative): `y -= 0x80`, `x += 0x800`, `dy = 0`; then
  if `x >= 0x800` (u): `y += 0x80`, `x -= 0x800`, `dy = 0` [O]. So walking off the right end of a
  row continues at the left end of the next row and vice versa: the 256 cells form one strip.
* Positions inside a cell: `x & 0x7F` (pixel) and `x & 0x78` (8-pixel column, used by most range
  tests), `y & 0x7F` (`0x70` = path line).

### 3.3 Which scene shows which cells

`g_level` DS:928F selects the scene; the frame loop loads PAN file `g_level` (0 MOUNTAIN,
1 MOUNT2N3, 2 MOUNT3, 3 CLOSE, 4 SHACK; §7.2). The grid is shared: each scene only ever shows its
own region, and the block numbers of a region are only meaningful in that region's PAN [O from
the teleport targets of §9, the scene each handler sets, and the block ranges: CLOSE has 5 and
SHACK 7 blocks]. Regions:

| Scene / PAN | Cells (hex) | What |
|---|---|---|
| 0 MOUNTAIN | 00–05 | clubhouse; new games start in 02 at (0x140, 0x70); 01 = door to the rank room, 03 = showcase treasure, 04 = door to the shelf rooms |
| 0 | 08–0B | level-0 cave (doors at EB and F7), 0A = passage up |
| 0 | 20–27 | upper room of the cave, bonus spot in 26 |
| 0 | 38–3B, 48–4B, 58–5B, 68–6B | treasure shelf rooms 0..3 (one row each) |
| 0 | E3–FF (rows 14–15) | **level 0 path** (loop, §3.4) |
| 0 / 1 | C7, CF, D7, DF, EF (cols 7 and 15, rows 12–14) | trunk ladder (DF, CF up to BF) and the drop shaft below the level-1 bridge (C7, D7 to E7) |
| 1 MOUNT2N3 | AB–C2 (rows 10–12) | **level 1 path** (loop) |
| 1 | 28–2A | mine tunnel (doors at B2 and BB) |
| 1 / 2 | 8E, 9E, AE, 94, A4 | geyser column above the level-1 gate AE; drop shaft below the level-2 bridge (94, A4 to B4) |
| 2 MOUNT3 | 77–88 (rows 7–8) | **level 2 path** (loop) |
| 2 | 70–76 | castle hall (door 73, ladder 74) |
| 2 | 30–37, 40–47, 50–57, 60–67 | castle wall (ladders; top row 3 holds the exit) |
| 2 | 2D–2F | ice cave (door at 7C) |
| 2 (3) | 3D, 4D | rock shaft from the ice cave down to the mine tunnel |
| 3 CLOSE | A8–AA | castle-top end screen |
| 3 | 5D, 6D (rows 5–6, cols 12–14) | chute from the end screen to the castle gate 6D |
| 4 SHACK | 98–9B | treasure room (prizes) |
| 4 | 1D–1F | rank room (treasure chest) |
| – | 10–11, 14–1A | a second copy of the clubhouse in MOUNTAIN blocks; no entry exists [O no teleport targets them; I unused] |
| – | all type-0 cells | empty; entering one is a bug path (§6.2 `Cell_P0_Void`) |

Seams: the cells where `g_level` changes while the player scrolls vertically show the same
picture in both PANs, so no redraw is needed: MOUNTAIN and MOUNT2N3 blocks 0x39, 0x3A, 0x3B,
0x3C, 0x3E are pixel-identical, MOUNT2N3 and MOUNT3 blocks 0x2E, 0x2F, 0x30, 0x36, 0x3A, 0x3B
are identical, and MOUNT2N3/MOUNT3 block 0x37 (geyser top 7E) is identical in its top 80 rows
[O `seams.py`]. Blocks of cells only one scene shows differ (e.g. 0x38 of DF/AE).

The row assignment matches the scoping capture matching (SCOPING §3) [O].

ASCII map (type numbers, `.` = 0; levels marked):

```
       0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
 0:   28 71 12 49 50 28  .  .  6 33 34 10  .  .  .  .   clubhouse 00-05 | cave 08-0B
 1:   28 28  .  .  7 28 28 28 28 28  8  .  . 72  .  .   (unused copy)   | rank room 1D-1F
 2:    7 35 28 28 28 28 73  8 37 59 38  .  . 61 62 28   upper room | mine tunnel | ice cave
 3:   63 63 63 63 63 63 63 63 51 58 28 52  . 23  .  .   castle top | shelf room 0 | rock shaft
 4:   57 57 57 57 57 57 57 57 53 67 28 54  . 23  .  .   castle wall | shelf room 1
 5:   57 57 57 57 57 57 57 57 55 68 28 65  . 23  .  .   castle wall | shelf room 2 | chute
 6:   57 57 57 57 57 57 57 57 66 69  8  .  . 20  .  .   castle wall | shelf room 3 | castle gate
 7:    7 56 56 44 56 56  8 21 28 42 28 28 60 28 41 28   castle hall 70-76 | level 2 77-
 8:   28 28 28 28 45 28 43 28 22  .  .  .  .  . 40  .   level 2 -88 | geyser 8E
 9:    .  .  .  . 19  .  .  . 70 70 49 70  .  . 40  .   treasure room 98-9B | geyser 9E
10:    .  .  .  . 23  .  .  . 64 64 64 15 28 16 39 28   end screen A8-AA | level 1 AB-
11:   28 28 17 28 48 28 28 45 28 28 28 18 36 28 28 30   level 1
12:   14 28 13  .  .  .  . 47  .  .  .  .  .  .  . 46   level 1 -C2 | trunk CF
13:    .  .  .  .  .  .  . 23  .  .  .  .  .  .  . 25   trunk DF
14:    .  .  .  3 28  4 32 48 28 28 36  5 28 28 28 31   level 0 E3-
15:   28 28 28 11 28 28 28  9 28 28 28 28 28  2 28  1   level 0 -FF
```

### 3.4 Level loops and mirror cells

Each mountain level is a closed loop: the last cells repeat the first three, and a path-link
cell at each seam moves objects by a fixed offset into the twin cell (§6.2) [O]:

| Level | Loop cells | Twins | Right-mover at | moved by | Left-mover at | moved by |
|---|---|---|---|---|---|---|
| 0 | E3..FF | FD–FF = E3–E5 | FF (`Cell_P1`) | (−0x500, −0x80) → E5 | E3 (`Cell_P3`) | (+0x500, +0x80) → FD |
| 1 | AB..C2 | C0–C2 = AB–AD | C2 (`Cell_P13`) | (+0x580, −0x100) → AD | AB (`Cell_P15`) | (−0x580, +0x100) → C0 |
| 2 | 77..88 | 86–88 = 77–79 | 88 (`Cell_P22`) | (+0x80, −0x80) → 79 | 77 (`Cell_P21`) | (−0x80, +0x80) → 86 |

`Layout_MirrorSharedCells` (1a54:06dc) copies the hiding places of the first three cells into the
twins (layout doc). The camera follows the conveyed player without a jump because `Player_Move`
re-derives the camera from the player's unchanged screen offset (§8.3) [O].

### 3.5 Cells whose picture or type changes at run time

| Cell | Default picture | Changes | By | Saved |
|---|---|---|---|---|
| 26 | 0x4B | 0x49 after the bonus is paid | `Cell_BonusCoins` | yes (`g_variableCells`[0]) |
| 79, 88 | 0x26 | 0x3C level-2 gate open (both twins) | `Cell_P42_GateL2` | 79 yes ([1]), 88 yes ([3]) |
| 84, B7 | 0x2F | 0x30 bridge broken; slow CPU: 0x2E and type 28 | `Cell_P45_V45_Bridge`, `Cells_AdjustForCpu` | yes ([2],[5]); type no |
| AE | 0x38 | 0x35 level-1 gate open (geyser) | `Cell_P39_V39_GateL1` | yes ([4]) |
| BC, EA | 0x47 | 0x48 one coin owed, 0x46 open | `Cell_P36_V36_NetShop` | yes ([6],[7]) |
| EF | 0x40 | 0x4C trunk turned into a ladder | `Cell_P31_GateL0` | yes ([8]) |
| 7E | 0x37 | 0x2E on a slow CPU | `Cells_AdjustForCpu` | – |
| BF | 0x41 | 0x40 on a slow CPU | `Cells_AdjustForCpu` | – |
| 32–35, 42–45, 52–55, 62–65, 72–75 | static | castle layout | `Castle_ApplyLayout` | layout id |
| hiding places (DS:11AA) | 0 | layout pictures; the search toggles them | layout, search | yes |

`Cells_ResetVariable` (1a54:085f) writes the 9 defaults and content 0x80 to the 9 `g_variableCells`
at every new game, so the bonus spot, gates, bridges and shop are per game, not per session
[O 47c7:0c51, 1437:00c0]. (ARCHITECTURE §4.4 says the bonus picture is not saved; it is, as
entry 0 of DS:120A, record +0xEB [O].)

`Cells_AdjustForCpu` (1a54:0803, layout doc) [O]: fast CPU: `picture[BF] = 0x41`,
`picture[7E] = 0x37`, and for each of 84/B7 whose picture is 0x2E: picture 0x2F, type 45. Slow
CPU: `picture[BF] = 0x40`, `picture[7E] = 0x2E`, `picture[84] = picture[B7] = 0x2E`,
`type[84] = type[B7] = 28` (no breaking bridge). The geyser hole type 41 is not changed.

`Castle_ApplyLayout` (1a54:08dc): layout L (0..3) writes the 20 words `DS:1252 + 40·L` row by row
into cells 0x32+16r+c (r = 0..4, c = 0..3), i.e. rows 3..7 columns 2..5, including the hall cells
72–75 [O]. Pictures used: 0x32 exit, 0x33/0x34 top row, 0x31 full ladder, 0x2D short ladder,
0x2A wall, 0x27 hall ladder, 0x2C hall wall, 0x3D hall door [O bytes, meanings from the masks of
§6.8 I].

---

## 4. Cell dispatch and the terrain mask

### 4.1 `Cell_GetMask` 1568:1fe4 (object call)

Input: `g_curObj`. Output: AX = the handler's return value (the terrain mask); globals
`g_cellCallKind` = 2, `g_curCell` DS:92CB = cell index of the object (§3.2). Calls
`g_cellPlayerHandlers[(signed char)type[g_curCell]]` [O]. No other work.

Callers: `Player_Move` 18cf:1600 (player; the return value is stored in DS:926A and used next
tick, §4.4) and `Elves_Update` 4521:0f8b (each active elf, after `Elf_Think` and before
`x += dx`, `Obj_WrapWorldX`; the return value is ignored) [O]. Elves therefore only feel the
handlers' side effects: teleports (path links), `dx = 0` (walls), `dy = 0`, and `Elf_Turn`.

### 4.2 `Cells_UpdateVisible` 1568:1f0b (visible calls, every tick)

Returns at once if `g_uiMode` DS:9119 != 0 [O]. Otherwise:

1. `g_castleCellIdx` DS:90FA = 0.
2. `c = cellIndex(camX, camY)` (§3.2), `g_cellScreenX = 2 − ((camX & 0x78) >> 1)`,
   `g_cellScreenY = 0x10 − (camY & 0x7F)`, `g_decorCount` = 0, `g_cellCallKind` = 1.
3. For k = 0..3: call `g_cellVisibleHandlers[(signed char)type[c+k]]`, then
   `g_cellScreenX += 0x40` (one cell = 64 bytes). `c+k` is not wrapped: the next index is the
   next row's first cell, which matches the strip; index 0x100..0x102 would read the content
   table (DS:0FAA) as types — the content words of cells 0 and 1 are always 0, so type 0, `Null_Handler`
   [O; I harmless].
4. If `g_decorCount` == 0: slot 12 frame = −1, animation = 0, last animation = 0 (no decoration).

Only the four cells of the camera's top cell row are visited. While the camera is between two
rows (vertical scrolling) the lower row's decorations are not updated [O].

### 4.3 Stale object in visible calls

`Cells_UpdateVisible` does not set `g_curObj`. A visible call therefore sees the object left by
the last `Cell_GetMask` caller: normally the last elf slot `g_elves[A212−1]` (elf 14, or elf 6 on
a slow CPU), because `Elves_Update` walks all slots, active or not, before `Cells_UpdateVisible`
runs; it is the player only when `Elves_Update` returned early (`g_slowCpu` != DS:8FA5, §4.6)
[O 4521:0eb5..0f36, 130f:017f..0189]. Of the handlers in the visible table, only
`Cell_P40_V40_Geyser` and `Cell_P41_V41_GeyserTop` run object logic without testing
`g_cellCallKind` (P30, P36, P39, P45, P46, P56, P57, P63 test it), so they act on that stale
object. The visible effect in practice [I from the code paths]:

* `Cell_P41_V41_GeyserTop` while cell 7E is on screen: the elf branch calls `Elf_Turn` for the
  last elf slot whenever (its dx > 0 and its `x & 0x78 <= 0x40`) or (dx < 0 and
  `x & 0x78 >= 0x40`), wherever that elf is. With 15 elves (tier 3) that elf is active; with
  fewer the slot is inactive (dx = 0) and nothing happens.
* `Cell_P40_V40_Geyser`: calls the level boundary with the stale object; harmless because the
  player's dy is already 0 after `Player_Move` and elves have dy = 0 [I].

A port should reproduce the elf-14 turn (it changes elf movement) or at least know it exists.

### 4.4 Terrain mask bits

`Player_Update` (18cf:181a, player doc) copies `g_keys` DS:9105 to DS:923A and then **writes
back** `g_keys = (g_keys | 0xFF00) & DS:926A`, where DS:926A is the mask returned by
`Cell_GetMask` in the *previous* tick's `Player_Move` [O 18cf:182f..1846, 18cf:1600..1605]. The
low byte therefore removes key bits the cell forbids (they stay cleared until the keyboard ISR
sets them again on the next make code), and the high byte of `g_keys` becomes the high byte of
the mask. Bit meanings [O consumers cited; I for the wording]:

| Bit | Meaning | Consumer |
|---|---|---|
| 0x0001 | Up allowed | key mask; `Input_MouseWalk` 4a85:0228 (bits 0..1 clear → a mouse "down" drops a coin) |
| 0x0002 | Down allowed | same |
| 0x0004 / 0x0008 | Left / Right allowed | key mask |
| 0x0010 | net allowed | key mask |
| 0x0020 | jump allowed | key mask |
| 0x0040 | coin drop allowed | key mask |
| 0x0080 | (set in most masks, no consumer) | – |
| 0x0100 | ladder here: Up/Down climb (states 15/16), fall ends on a ladder | PlrSt_10_OnPath/13/14/15/16/31/32/33 |
| 0x0200 | a dropped coin pays the net shop instead of searching | PlrSt_27_CoinLeftDone/28 18cf:1282/12bc |
| 0x0400 | camera mode: moving up, the camera starts scrolling when the player is ≤ 0x70 below the camera top instead of ≤ 0x40 (§8.3) | `Player_Move` 18cf:16ce |
| 0x0800 | slide / lift: Up enters the slide (states 29/30), which continues while set | PlrSt_04_Arrive/10/13/29/30 |
| 0x1000 | fall: the player falls (anim 0x3F/0x40, states 31/32) while set; when it clears he grabs a ladder (0x100) or lands | PlrSt_04_Arrive/10/11/15/16/31/32/35 |
| 0x2000 | ladder jump allowed: on a ladder, Left/Right jumps (anim 0x3D/0x3E, state 33) | PlrSt_15_ClimbUp/16 18cf:0f24/0ff5 |
| 0x4000, 0x8000 | never set | – |

ARCHITECTURE §4.1 listed 0x400 and 0x1000 as unknown: 0x400 is the camera mode above, 0x1000 is
the fall bit. Masks seen, with their sense: 0xFC plain path (no Up/Down), 0xFF passage, 0x7C
exit cell, 0x5F3 ladder (Up/Down, no walking), 0x1400 / 0x14F0 shaft (falling), 0x25FF castle
ladder with jumps, 0x2FC net shop front, 0xCF1 / 0xCF4 geyser lift, 0x10FD start-room door.

### 4.5 Common handler steps (referred to below)

* **PATH(o)**: if `o.y & 0x7F == 0x70`: if `o.dy > 0` (s) then `o.dy = 0`; `mask &= 0xFFFD`.
* **COLUMN(lo, hi)**: if `(o.x & 0x78) < lo` (u) or `(o.x & 0x78) > hi` (u): `mask &= 0xFFFC`
  (Up/Down only inside the ladder or door column).
* **SNAP(X)**: if `o.x != X` then `g_fullRedrawReq++`; `o.x = X` (center on a ladder; requests a
  full redraw only when the position changed). `SNAPCELL(k)`: `X = (o.x & 0xFF80) + k`.
* **TELEPORT(x, y, cam, t, anim, state)**: `o.x = x`, `o.y = y`; if cam is given
  `camX = player.x + cam` (always computed from DS:37E2 after the move); `g_transition = t`;
  `g_fullRedrawReq = 1`; `Anim_Set(1, anim)`; `g_playerState = state`. The tick's dx/dy are still
  applied afterwards by `Player_Move` (§8.3).
* **ISPLAYER**: `g_curObj == &g_player` (far pointer compare).

Handlers that end without loading AX from `g_cellMask` return whatever AX holds (the offset of
`g_curObj`, an INT 10h result, ...). Only the player uses the value; every player path in these
handlers returns the mask [O]. A port can return the mask everywhere.

### 4.6 Slow-CPU flags

`g_slowCpu` DS:923C (8086 detection or `-X`), DS:8FA5 (the remembered value) and
`g_slowScroll` DS:3D31 (doubles scroll and camera steps) [O]:

| Where | Effect |
|---|---|
| `Game_InitVars` 1460:0416 | `g_slowScroll = (g_slowCpu == 1)`; DS:8FA5 = `g_slowCpu` |
| `Game_NewSetup` 47c7:0c5b | DS:8FA5 = `g_slowCpu` |
| `Game_Restore` 47c7:0de6 | DS:8FA5 = `g_slowCpu`; if `g_area == 0x0D` (castle): `g_slowCpu = 0`; `g_slowScroll = g_slowCpu` |
| `Cell_P42_GateL2` 176a:039b (into the castle) | DS:8FA5 = `g_slowCpu`; `g_slowScroll = 0` |
| `Cell_P44_CastleDoor` 1568:0d4c | on the hall floor: `g_slowCpu = 0` |
| `Cell_P44_CastleDoor` 1568:0d1d (leaving) | `g_slowCpu = DS:8FA5`; `g_slowScroll = g_slowCpu` |
| `Cell_P63_V63_CastleTop` 1568:1b93 (game end) | `g_slowCpu = DS:8FA5`; `g_slowScroll = g_slowCpu` |
| PlrSt_15_ClimbUp/16 18cf:0f34.. (ladder jump) | `g_slowCpu = 0` (player doc) |
| `Elves_Update` 4521:0ebf | does nothing while `g_slowCpu != DS:8FA5` |

So the castle always runs in fast mode [O]; on a slow machine elves freeze while the flags
differ (no elves live in the castle) [I].

---

## 5. Cell types

Type, cells using it (type-28 and type-57 lists abbreviated; see the grid), object handler
(DS:08A6), visible handler (DS:09D2; – = `Null_Handler`), role [O tables; roles from §6].

| Type | Cells | Object handler | Visible handler | Role |
|---|---|---|---|---|| 0 | 78 cells | `Cell_P0_Void` | – | empty cell: falls, flashes the border |
| 1 | FF | `Cell_P1` | – | level 0 seam, right-movers |
| 2 | FD | `Cell_P2` | – | level 0 seam, elves (left-to-right) |
| 3 | E3 | `Cell_P3` | – | level 0 seam, left-movers |
| 4 | E5 | `Cell_P4` | – | level 0 seam, elves (right-to-left) |
| 5 | EB | `Cell_P5_CaveDoorW` | – | cave door (up) to 08 |
| 6 | 08 | `Cell_P6_CaveExitW` | – | cave left exit to EB |
| 7 | 14 20 70 | `Cell_P7_WallLeft` | `Cell_V7` | left wall |
| 8 | 1A 27 6A 76 | `Cell_P8_WallRight` | `Cell_V8` | right wall |
| 9 | F7 | `Cell_P9_CaveDoorE` | – | cave door (up) to 0B |
| 10 | 0B | `Cell_P10_CaveExitE` | `Cell_V10` | cave right exit to F7 |
| 11 | F3 | `Cell_P11_ClubDoor` | – | clubhouse door (up) |
| 12 | 02 | `Cell_P12_StartRoom` | – | start room: door down, prize room, crown |
| 13 | C2 | `Cell_P13` | – | level 1 seam, right-movers |
| 14 | C0 | `Cell_P14` | – | level 1 seam, elves |
| 15 | AB | `Cell_P15` | – | level 1 seam, left-movers |
| 16 | AD | `Cell_P16` | – | level 1 seam, elves |
| 17 | B2 | `Cell_P17_MineDoorW` | – | mine tunnel door (up) |
| 18 | BB | `Cell_P18_MineDoorE` | – | mine tunnel door (up) |
| 19 | 94 | `Cell_P19_Boundary12` | – | shaft, scene 1/2 boundary |
| 20 | 6D | `Cell_P20_CastleGate (player doc)` | – | castle gate (end of climb) |
| 21 | 77 | `Cell_P21` | – | level 2 seam, left-movers |
| 22 | 88 | `Cell_P22` | – | level 2 seam, right-movers |
| 23 | 3D 4D 5D A4 D7 | `Cell_P23_FallShaft` | – | fall shaft (+ ice-cave special case) |
| 24 | – | `Cell_P24_Wall` | – | wall (unused type) |
| 25 | DF | `Cell_P25_TrunkShaft` | – | trunk ladder shaft |
| 26 | – | `Cell_P26_Unused` | – | camera clamp (unused type) |
| 27 | – | `Cell_P27_Unused` | – | camera clamp (unused type) |
| 28 | 59 cells | `Cell_P28_Path` | – | plain path |
| 29 | – | `Cell_P29_Unused` | – | camera clamp (unused type) |
| 30 | BF | `Cell_P30_V30_TrunkTop` | `Cell_P30_V30_TrunkTop` | top of the trunk ladder, treetop sprite |
| 31 | EF | `Cell_P31_GateL0` | – | level 0 gate: tree trunk |
| 32 | E6 | `Cell_P28_Path` | `Cell_V32_AnimDecor` | path + animated decoration |
| 33 | 09 | `Cell_P28_Path` | `Cell_V10` | path + rock decoration |
| 34 | 0A | `Cell_P34_CaveUp` | – | passage up to the upper room |
| 35 | 21 | `Cell_P35_UpperRoomDown` | – | passage down to the cave |
| 36 | BC EA | `Cell_P36_V36_NetShop` | `Cell_P36_V36_NetShop` | net shop |
| 37 | 28 | `Cell_P37_MineExitW` | – | mine tunnel left exit |
| 38 | 2A | `Cell_P38_MineExitE` | – | mine tunnel right exit |
| 39 | AE | `Cell_P39_V39_GateL1` | `Cell_P39_V39_GateL1` | level 1 gate: geyser cave |
| 40 | 8E 9E | `Cell_P40_V40_Geyser` | `Cell_P40_V40_Geyser` | geyser column, scene 1/2 boundary |
| 41 | 7E | `Cell_P41_V41_GeyserTop` | `Cell_P41_V41_GeyserTop` | geyser top hole |
| 42 | 79 | `Cell_P42_GateL2` | – | level 2 gate: stone arch |
| 43 | 86 | `Cell_P43` | – | level 2 seam, elves |
| 44 | 73 | `Cell_P44_CastleDoor` | – | castle hall door |
| 45 | 84 B7 | `Cell_P45_V45_Bridge` | `Cell_P45_V45_Bridge` | rope bridge (breaks at tier >= 2) |
| 46 | CF | `Cell_P46_V46_Boundary01` | `Cell_P46_V46_Boundary01` | trunk, scene 0/1 boundary, treetop sprite |
| 47 | C7 | `Cell_P47_Boundary01` | – | shaft, scene 0/1 boundary |
| 48 | B4 E7 | `Cell_P48_Landing` | – | shaft bottom: landing on a path |
| 49 | 03 9A | `Cell_P28_Path` | `Cell_V49_Showcase` | path + showcase / treasure-room sprites |
| 50 | 04 | `Cell_P50_ShelfEnter` | – | door to the shelf rooms |
| 51 | 38 | `Cell_P51_ShelfExit` | – | shelf room 0 left exit |
| 52 | 3B | `Cell_P52_Shelf0to1` | – | shelf room 0 -> 1 |
| 53 | 48 | `Cell_P53_Shelf1to0` | – | shelf room 1 -> 0 |
| 54 | 4B | `Cell_P54_Shelf1to2` | – | shelf room 1 -> 2 |
| 55 | 58 | `Cell_P55_Shelf2to1` | – | shelf room 2 -> 1 |
| 56 | 71 72 74 75 | `Cell_P56_V56_CastleHall` | `Cell_P56_V56_CastleHall` | castle hall floor and ladders |
| 57 | 24 cells | `Cell_P57_V57_CastleWall` | `Cell_P57_V57_CastleWall` | castle wall |
| 58 | 39 | `Cell_P28_Path` | `Cell_NopV58` | path |
| 59 | 29 | `Cell_P28_Path` | `Cell_V59` | path + post decoration |
| 60 | 7C | `Cell_P60_IceDoor` | – | ice cave door (up) |
| 61 | 2D | `Cell_P61_IceCaveExit` | – | ice cave left exit |
| 62 | 2E | `Cell_P62_IceSlide` | – | ice cave right end: slide into the shaft |
| 63 | 30 31 32 33 34 35 36 37 | `Cell_P63_V63_CastleTop` | `Cell_P63_V63_CastleTop` | castle wall top row, exit = game end |
| 64 | A8 A9 AA | `Cell_P64_EndScreen (player doc)` | – | end screen (player doc) |
| 65 | 5B | `Cell_P65_Shelf2to3` | – | shelf room 2 -> 3 |
| 66 | 68 | `Cell_P66_Shelf3to2` | – | shelf room 3 -> 2 |
| 67 | 49 | `Cell_P28_Path` | `Cell_NopV67` | path |
| 68 | 59 | `Cell_P28_Path` | `Cell_NopV68` | path |
| 69 | 69 | `Cell_P28_Path` | `Cell_NopV69` | path |
| 70 | 98 99 9B | `Cell_P70_TreasureRoom (player doc)` | – | treasure room (player doc) |
| 71 | 01 | `Cell_P71_V71_ToRankScreen (player doc)` | `Cell_P71_V71_ToRankScreen (player doc)` | clubhouse left: rank room door (player doc) |
| 72 | 1D | `Cell_P72_RankScreen (player doc)` | – | rank room (player doc) |
| 73 | 26 | `Cell_BonusCoins` | – | bonus coin spot |
| 74 | – | `Null_Handler` | – | (no cell) |

Type 57 cells: 40–47, 50–57, 60–67. Type 28 cells: 00 05 10 11 15–19 22–25 2F 3A 4A 5A 78 7A 7B 7D 7F
80–83 85 87 AC AF B0 B1 B3 B5 B6 B8–BA BD BE C1 E4 E8 E9 EC–EE F0–F2 F4–F6 F8–FC FE. Type 0: the
remaining 78 cells.

---

## 6. Handler specifications

Format: name, address [old export name], cell types/cells, then the exact steps. "o" is
`*g_curObj`. Unless a handler is said to test `g_cellCallKind`, it runs the same code for object
and visible calls (§4.3). A visible handler's return value is never used. Masks are built in
`g_cellMask` and returned in AX.

### 6.1 Shared helpers

**`Cell_GroundCheckObj`** 1568:0134 — used by `Cell_P35_UpperRoomDown`. Any object [O].
1. If `o.y & 0x7F == 0x70`: PATH(o); go to 3.
2. Else if `o.y & 0x7F < 0x6C` (u): `Music_Stop()`; `g_transition = 1`; `g_fullRedrawReq = 1`;
   `g_playerState = 9`; return 1.
3. COLUMN(0x26, 0x5A); return 0.

**`Cell_GroundCheckPlayer`** 1568:0203 — used by P9, P11, P17, P18, P34, P60. If not ISPLAYER:
return 0 (mask untouched). Otherwise as `Cell_GroundCheckObj` with threshold **0x68** and
`g_transition = 2` [O]. The player must rise more than 8 px above the path line (by the look-up
animation, player doc) before the passage fires.

**`Cell_ConveyLeft(dx, dy)`** 176a:0019 — "move right-movers": if `o.dx > 0` (s): if ISPLAYER:
`Elves_ShiftNearby(dx, dy)` (4521:12df, elves doc), `o.x += dx`, `o.y += dy` (any x in the cell:
the guard `TEST x,0x78 / JC` can never jump); else (elf) only when `o.x & 0x78 == 0x78`:
`o.x += dx`, `o.y += dy` [O].

**`Cell_ConveyRight(dx, dy)`** 176a:0110 — "move left-movers": if `o.dx < 0` (s): if ISPLAYER:
(the guard `(x & 0x78) > 0x80` never holds) `Elves_ShiftNearby(dx, dy)`, `o.x += dx`,
`o.y += dy`; else (elf) only when `o.x & 0x78 == 0` [O].

**`Cell_ConveyLeftIf(dx, dy, xmax, row)`** 176a:00a6 — elves only (returns at once for the
player): if `o.dx > 0` (s) and `player.x <= xmax` (u) and `(player.y & 0xFF80) == row`:
`o.x += dx`, `o.y += dy` (any x in the cell) [O].

**`Cell_ConveyRightIf(dx, dy, xmin, row)`** 176a:019d — elves only: if `o.dx < 0` (s) and
`player.x >= xmin` (u) and `(player.y & 0xFF80) == row`: `o.x += dx`, `o.y += dy` [O].
(The `If` variants move elves through a seam only while the player is near the far side, so the
player sees elves arrive [I].)

**`Cell_DrawObject(x, y, frame)`** 1568:1e50 (decompiles after the repair, §12) — slot 12:
`x`, `y`, depth = 0x1E, frame = `frame`, draw index = 0x0C, last animation = 0;
`g_decorCount++` [O]. It does not clear slot 12's current animation and does not test
`g_decorCount`: of several static decorations in view the last (rightmost) one wins.

**`Cell_DrawTreetop(wy)`** 1568:0ee3 — if `g_slowCpu == 0` and
`g_decorCount == 0`: `Cell_DrawObject(g_cellScreenX + 0x20, wy - camY + 0x80, 0x17C)`
(SPRITES frame 190, green foliage) [O]. Called with wy = 0x600 by types 30 and 46, so the
sprite stands at world (column-15 cell x + 0x40, y 0x670) whichever of cells BF/CF is visited.

**`Cell_DrawGeyser(wy, depth)`** 1568:1556 — only if
`g_cellCallKind == 1` and `picture[AE] == 0x35` (level-1 gate open):
`Anim_SetIfIdle(12, 0x54)`; `s = wy - camY` (s16); if `s >= 0x3B` (s) then `s = 0x3B`;
slot 12: `x = g_cellScreenX + 0x20`, `y = s + 0x54`, `depth = depth`; `g_decorCount++` [O].
Callers: types 39, 40 with (0x500, 0x50), type 41 with (0x500, 0x1E). Animation 0x54 is the
geyser column; its frames come from the script.

**`Castle_PlaceCreature`** 1568:1e9e — slot 6: `x = g_cellScreenX`, `y = g_cellScreenY + 0x70`,
depth 0x50; slot 7: `x = 0`, `y = 0`, depth 0x4F; `g_decorCount++` [O].

**`Cell_LevelBoundary01`** 1568:0e6d — called by types 46, 47 when `o.y & 0x7F > 0x68` (u) [O]:
1. If `g_loadedScene == 1` and `o.dy > 0` (s): `g_level = 0`; `g_keyState` DS:9120 = 2.
2. Else if `g_loadedScene == 0` and `o.dy < 0` (s): `g_level = 1`; test hiding slots
   i = 13, 14, ... and stop at the first whose content `g_cellContent[g_hidingCells[i]] == 0x68`
   or when i reaches 29 (slots 13..29 are tested; 29 is level 0's first slot);
   `g_keyState` = 0x0A if the slot stopped at holds 0x68 (level-1 key not yet found), else 4.
   A searched key has bit 7 set (0xE8) and does not match.

**`Cell_LevelBoundary12`** 1568:12b4 — called by types 19, 40 [O]:
1. If `g_loadedScene == 2` and `o.dy > 0`: `g_level = 1`; `g_keyState = 4`.
2. Else if `g_loadedScene == 1` and `o.dy < 0`: `g_level = 2`; test slots 0..13 for content
   0x6A (level-2 key): `g_keyState = 0x0C` if found, else 6.

The boundaries only change `g_level`; `Scene_Update` loads the new PAN at the next frame and the
player keeps scrolling (no redraw, §3.3). Going down always sets the lower level's key state to
"found": nobody can be above a gate without its key [I].

**`Castle_CreatureHitCheck`** 1568:18f1 — called by the object calls of types 56, 57, 63 (any
object) [O]:
1. If `g_creatureStrike` DS:9264 == 0: return. Else clear it.
2. If `g_playerState == 0x21` (ladder jump): return.
3. `py = DS:8FA9` (player screen y offset, §2.5), `cy = slot6.y`: if `py - 0x1D > cy` (u) or
   `py + 0x10 < cy` (u): return (miss).
4. If `g_coins` DS:9134 != 0: `g_coins--`; `Sfx_Play(5)` (no sound when broke).
5. `px = DS:8FA7 + 2`: if `px > slot6.x` (u): `g_keys |= 8`; if `px < slot6.x` (u):
   `g_keys |= 4`. The forged Right/Left bit makes the climbing player ladder-jump away from the
   creature at the next tick (PlrSt_15_ClimbUp/16 with mask 0x2000); the jump states clear the bits
   again (`g_keys &= 0xFFF3`, PlrSt_31_FallLeft..33) [O player code; I effect].

### 6.2 Paths, walls and the void

**`Cell_P28_Path`** 1568:105e — types 28, 32, 33, 49, 58, 59, 67, 68, 69: if
`o.dy > 0` (s): `o.dy = 0`; return **0xFC** [O]. No Up/Down on a plain path; nothing checks the y
line (objects never get a positive dy applied there).

**`Cell_P0_Void`** 1568:1eca — type 0 (78 empty cells): byte DS:92C8 += 1;
INT 10h AX=1001h with BH = DS:92C8 (EGA/VGA overscan colour) [O]. AX is not reloaded, so the
"mask" is what INT 10h leaves in AX, normally 0x1001 (fall + Up) [I: the BIOS keeps AX]. An
object that gets into an empty cell falls and the screen border changes colour every tick
(a leftover debugging aid [I]). A port returns 0x1001 and may ignore the border.

**`Cell_P24_Wall`** 1568:0e33 — no cell uses type 24: `o.dx = 0`; return 0xFF.

**`Cell_P26_Unused` / `Cell_P27_Unused` / `Cell_P29_Unused`** 1568:1010 / 1037 / 1077 — no cell
uses them. P26: if DS:37F0 > 0 (s) and `camY == 0x500`: DS:37F0 = 0; if `camY != 0x500`:
DS:37EE = 0; return 0xFF. P27: the same with 0x380. P29: if DS:37F0 < 0 (s) and
`camY == 0x380`: DS:37F0 = 0; return 0xFF [O]. They would have no effect anyway: `Player_Move`
overwrites DS:37EE/37F0 right after `Cell_GetMask`.

**`Cell_P7_WallLeft`** 1568:05cb — type 7 (cells 14, 20, 70). If
`g_cellCallKind != 2`: call `Cell_V7` and return. Else, any object [O]:
1. mask = 0x10DC; if `(o.x & 0x78) <= 0x43` (u): mask = 0x10D8 (no Left) and if `o.dx < 0` (s):
   `o.dx = 0`.
2. If `(o.y & 0x78) >= 0x70` (u) (lowest 16 rows of the cell): if `o.dy > 0`: `o.dy = 0`;
   `mask &= 0x00FC` (drops the fall bit: 0xDC / 0xD8). Higher up, bit 0x1000 makes the player
   fall back to the floor.

**`Cell_P8_WallRight`** 1568:0638 — type 8 (cells 1A, 27, 6A, 76). Mirror image:
visible call → `Cell_V8`. mask 0x10DC; if `(o.x & 0x78) >= 0x38` (u): mask 0x10D4 (no Right) and
if `o.dx > 0`: `o.dx = 0`; floor step as P7 [O].

**`Cell_V7`** 1568:1e1c — `Cell_DrawObject(g_cellScreenX - 1, g_cellScreenY + 0x78, 0x17A)`;
**`Cell_V8`** 1568:1e35 — `Cell_DrawObject(g_cellScreenX + 0x41, g_cellScreenY + 0x78, 0x17B)`
(SPRITES frame 189, a rock edge; 0x17B is the mirrored code) [O]. No slow-CPU test.

**`Cell_V10`** 1568:1ddb — types 10, 33 (cave cells 0B, 09): if `g_slowCpu == 0`:
`Cell_DrawObject(g_cellScreenX, g_cellScreenY + 0x80, 0x176)` (frame 187, rock column) [O].

**`Cell_V59`** 1568:1dfa — type 59 (mine tunnel 29): if `g_slowCpu == 0`:
`Cell_DrawObject(g_cellScreenX + 0x18, g_cellScreenY + 0x80, 0x17E)` (frame 191, wooden post).

**`Cell_V32_AnimDecor`** 1568:1d96 — type 32 (cell E6, level 0): if
`g_slowCpu == 0`: `Anim_SetIfIdle(12, 0x53)`; slot 12 `x = g_cellScreenX + 0x20`,
`y = g_cellScreenY + 0x80`; `g_decorCount++`; depth and frame are left as they are [O].

**`Cell_NopV58/V67/V68/V69`** 1568:1d92..1d95 — single `RETF` each.

**Path links (176a)** [O]. All return 0xFC.

| Handler | Cell | Call |
|---|---|---|
| `Cell_P1` 176a:0006 | FF | `Cell_ConveyLeft(-0x500, -0x80)` |
| `Cell_P2` 176a:008a | FD | `Cell_ConveyLeftIf(-0x500, -0x80, xmax 0x300, row 0x700)` |
| `Cell_P3` 176a:00fd | E3 | `Cell_ConveyRight(+0x500, +0x80)` |
| `Cell_P4` 176a:0181 | E5 | `Cell_ConveyRightIf(+0x500, +0x80, xmin 0x680, row 0x780)` |
| `Cell_P13` 176a:01f8 | C2 | `Cell_ConveyLeft(+0x580, -0x100)` |
| `Cell_P14` 176a:020a | C0 | `Cell_ConveyLeftIf(+0x580, -0x100, xmax 0x700, row 0x500)` |
| `Cell_P15` 176a:0225 | AB | `Cell_ConveyRight(-0x580, +0x100)` |
| `Cell_P16` 176a:0237 | AD | own code, below |
| `Cell_P21` 176a:02af | 77 | `Cell_ConveyRight(-0x80, +0x80)` |
| `Cell_P22` 176a:029d | 88 | `Cell_ConveyLeft(+0x80, -0x80)` |
| `Cell_P43` 176a:02c1 | 86 | `Cell_ConveyLeftIf(+0x80, -0x80, xmax 0x500, row 0x380)` |
| `Cell_P42_GateL2` | 79 | begins with `Cell_ConveyRightIf(-0x80, +0x80, xmin 0x300, row 0x400)` (§6.8) |

`Cell_P16` 176a:0237: elves only; if `o.dx < 0` (s) and `o.x & 0x78 == 0x78` and (player row
`player.y & 0xFF80 == 0x600`, or player row == 0x580 and `(player.x & 0xFF80) > 0x600` (u)):
`o.x -= 0x580`, `o.y += 0x100` (into C2) [O]. Returns 0xFC.

### 6.3 Clubhouse, start room and treasure shelf rooms (scene 0, row 0 and rows 3–6)

**`Cell_P12_StartRoom`** 1568:03f6 — type 12, cell 02 (new games start here at
x 0x140). Object calls only by layout (no kind test; visible type 12 is Null); no ISPLAYER test.
Steps in order [O]:
1. mask = 0x10FD.
2. If `o.y & 0x7F == 0x70`: if `o.dy > 0`: `o.dy = 0`; mask = 0xFD; go to 4.
3. Else (above the floor): if Up is not held (`g_keys & 1 == 0`): go to 4 with 0x10FD (the
   player falls back). Else mask = 0xF1; if `o.y & 0x7F < 0x6C` (u) and `(o.x & 0x78) < 0x5A`
   (u) — **the door**: `Music_Stop`; `g_area = 0`; `Music_Play(0)`; `g_transition = 2`;
   `g_fullRedrawReq = 1`; `o.x = 0x1C0`, `o.y = 0x7F0` (cell F3, level 0, outside the clubhouse);
   `camX = player.x - 0xA0`; `Anim_Set(1, 6)`; `g_playerState = 9`; if `g_shownTreasure`
   DS:8FB0 > 0 (s): byte DS:9254 = low byte of `g_shownTreasure`, slot 3 frame (DS:901E) = −1,
   `g_shownTreasure = -1` (the showcase treasure is put away while you are outside).
4. If `o.y & 0x7F != 0x70` and `(o.x & 0x78) >= 0x5A` (u) — **the prize picture** (right part):
   `mask |= 3`; if `o.y & 0x7F < 0x6C` (u): `mask &= 0x0002` (only Down) and if
   `o.y & 0x7F == 0x60`: `o.x = 0x4F0`, `o.y = 0x4E0` (cell 99, treasure room), `g_level = 4`,
   `g_menuLock = 1`, `g_playerState = 1`, `g_fullRedrawReq = 1`, `Anim_Set(1, 0x51)`,
   byte DS:9282 += 1. No `g_transition` (no dissolve; the camera keeps the player's screen
   offset, §8.3).
5. **Crown hand-over**: if slot 1's last animation (DS:8FF8) == 0xA5: `g_level = 4`,
   `g_menuLock = 1`, `o.x = 0x4F0`, `o.y = 0x4E0`, `g_fullRedrawReq = 1`, `g_transition = 1`,
   `Anim_Clear(6)`, `Anim_Clear(8)`, then **calls `Cell_P71_V71_ToRankScreen`** with the new position (its
   object branch sees `y & 0x7F == 0x60` and moves the player on to the rank room at
   (0x680, 0x80), sets `g_transition = 2`, camX 0x680, §6.10); mask = 0.
6. COLUMN on the left only: if `(o.x & 0x78) < 0x26` (u) or `> 0x78` (never):
   `mask &= 0x00FC` (imm16: this also drops 0x1000).
Return mask.

**`Cell_P11_ClubDoor`** 1568:0386 — type 11, cell F3 (level 0, the clubhouse door
from outside) [O]:
1. mask = 0xFF. If `Cell_GroundCheckPlayer() == 1`: `o.x = 0x140`, `o.y = 0x70` (start room),
   `camX = player.x - 0x80` (= 0xC0, the new-game camera), `Anim_Set(1, 6)`, `g_area = 5`,
   `Music_Play(1)`.
2. Every call: if ISPLAYER and (signed byte) DS:9254 > 0: `g_shownTreasure` = DS:9254
   sign-extended (the showcase comes back; DS:9254 keeps its value).

**`Cell_V49_Showcase`** 1568:1c7b — visible handler of type 49 (cells 03 in the
clubhouse and 9A in the treasure room; object handler is `Cell_P28_Path`) [O, decompiles after
the repair]:
1. Slot 3 (DS:9018): `x = g_cellScreenX + DS:9213`, `y = g_cellScreenY + DS:9211`, depth 0x50,
   draw index 3; byte DS:9129 = 0.
2. If slot 3's current animation (DS:9022) < 0x5E (s) and `DS:9251 & 0x20 == 0`: slot 3 frame =
   `g_shownTreasure`, last animation = 0.
3. If `DS:9251 & 0x40` and DS:00F8 > 0 (s): slot 8 (DS:9086): `x = g_cellScreenX + DS:00FC`,
   `y = g_cellScreenY + 0x44`, depth 0x46, draw index 8, frame = DS:00F8, last animation 0.
4. If `DS:9251 & 0x40` and (signed byte) DS:00FA > 0 and DS:00FC < 0x36 (u): slot 10 (DS:90B2):
   `x = g_cellScreenX + DS:00FE`, `y = g_cellScreenY + 0x44`, depth 0x3C, draw index 0x0A,
   frame = DS:00FA sign-extended, last animation 0.
It does not count in `g_decorCount`. DS:9211 (initial 0x44, `Game_InitVars` 1460:0410),
DS:9213, DS:9251 and DS:00F8..00FF are written by the treasure-room code (player doc).

**Treasure shelf rooms** — four rooms of one cell row each (rows 3..6, cells x8..xB, scene 0):
room k is row 3+k. They are entered from the clubhouse (cell 04) and chained: the right end of
room k leads to the left end of room k+1 and back. Shelf drawing is `ShelfRoom_DrawShelf`
(4a03:04a8, player doc) driven by these flags [O]:

| DS | Meaning |
|---|---|
| 9201..9204 (bytes) | "player is in shelf room 0..3"; latched each frame into 8F9F..8FA2 by `Frame_Begin` |
| 7D7C + 2k | room k needs its shelves drawn (cleared by `ShelfRoom_DrawShelf`) |
| 8FBA | shelf redraw request (also set by `Playfield_Redraw`) |
| 9129 | equals k when room k was entered from its left end (shelf x positions relative to camX 0x440), any other value when entered from its right end (relative to 0x480) |

All eight handlers act only when `g_cellCallKind == 2` (else return), do not test ISPLAYER,
set mask = 0xDC and return it. "Always" means for any position in the cell (the guard is
`TEST x,0x78 / JC`, which never jumps); "walking left" means `o.dx < 0` (s) (their second guard
`(x & 0x78) <= 0x78` always holds). Each move is `o.x/o.y = target`, `g_transition = 2`,
`camX = player.x + cam`, `g_fullRedrawReq = 1`, `Anim_Set(1, anim)`, `g_playerState = 0x0A`, then
the flags [O]:

| Handler | Cell | When | Target (cell) | cam | anim | Flags set |
|---|---|---|---|---|---|---|
| `Cell_P50_ShelfEnter` 1568:06a5 | 04 | always | (0x460, 0x1F0) (38, room 0) | −0x20 | 3 | 7D7C=1, 9201=1, 8FBA=1, 9129=0; then slot 3 frame (DS:901E) = −1 |
| `Cell_P51_ShelfExit` 1568:0718 | 38 | walking left | (0x200, 0x70) (04 left edge) | −0x100 | 2 | 7D7C=0, 9201=0 |
| `Cell_P52_Shelf0to1` 1568:078a | 3B | always | (0x460, 0x270) (48, room 1) | −0x20 | 3 | 7D7E=1, 9202=1, 9201=0, 8FBA=1, 9129=1 |
| `Cell_P53_Shelf1to0` 1568:07fc | 48 | walking left | (0x580, 0x1F0) (3B left edge) | −0x100 | 2 | 7D7C=1, 9201=1, 9202=0, 8FBA=1, 9129=2 |
| `Cell_P54_Shelf1to2` 1568:0880 | 4B | always | (0x460, 0x2F0) (58, room 2) | −0x20 | 3 | 7D80=1, 9203=1, 9202=0, 8FBA=1, 9129=2 |
| `Cell_P55_Shelf2to1` 1568:08f2 | 58 | walking left | (0x580, 0x270) (4B left edge) | −0x100 | 2 | 7D7E=1, 9202=1, 9203=0, 8FBA=1, 9129=3 |
| `Cell_P65_Shelf2to3` 1568:0976 | 5B | always | (0x460, 0x370) (68, room 3) | −0x20 | 3 | 7D82=1, 9204=1, 9203=0, 8FBA=1, 9129=3 |
| `Cell_P66_Shelf3to2` 1568:09e8 | 68 | walking left | (0x580, 0x2F0) (5B left edge) | −0x100 | 2 | 7D80=1, 9203=1, 9204=0, 8FBA=1, 9129=4 |

Arrival points on a cell's left edge work because the tick's dx (negative, walking left) is
applied after the teleport (§8.3): the player ends a few pixels inside the cell to the left
(e.g. 0x578 in 3A), so the "always" handler of that edge cell does not fire again [O order in
`Player_Move`; I depends on the walk animation's dx, player doc]. Room 3 ends in the wall cell
6A (type 8). Music is not changed.

### 6.4 Level-0 cave and its upper room (scene 0, cells 08–0B, 20–27)

| Handler | Cell | Fires | Target (cell) | Camera | Then |
|---|---|---|---|---|---|
| `Cell_P5_CaveDoorW` 1568:0008 | EB (level 0) | player, `y&0x7F < 0x68` | (0x460, 0x70) (08) | x − 0x20 | area 7, tune 2 |
| `Cell_P9_CaveDoorE` 1568:01b4 | F7 (level 0) | `GroundCheckPlayer` | (0x5B0, 0x70) (0B) | x − **0x124** | area 0x0B, tune 2 |
| `Cell_P6_CaveExitW` 1568:00ce | 08 | `o.dx < 0` | (0x5B0, 0x770) (EB) | x − 0xA0 | area 0, tune 0 |
| `Cell_P10_CaveExitE` 1568:029d | 0B | `o.dx > 0` | (0x3B0, 0x7F0) (F7) | x − 0xA0 | area 0, tune 0 |
| `Cell_P34_CaveUp` 1568:0303 | 0A | `GroundCheckPlayer` | (0xC0, 0x170) (21) | x − 0x20 | music stopped, none started |
| `Cell_P35_UpperRoomDown` 1568:0341 | 21 | `GroundCheckObj` | (0x4C0, 0x70) (09) | kept (transition 1) | area 9, tune 2 |

Exact steps [O]:

* **P5**: if not ISPLAYER return (garbage AX). mask = 0xFF. If `o.y & 0x7F == 0x70`: PATH. Else
  if `o.y & 0x7F < 0x68` (u): `Music_Stop`; `o.x = 0x460`, `o.y = 0x70`; `camX = player.x -
  0x20`; `g_area = 7`; `Music_Play(2)`; `g_transition = 2`; `g_fullRedrawReq = 1`;
  `Anim_Set(1, 3)`; `g_playerState = 9`. Finally (always) if `(o.x & 0x78) > 0x40` (u):
  `mask &= 0xFFFC` (the door is the left half of the cell). Return mask.
* **P9**: mask = 0xFF; if `Cell_GroundCheckPlayer() == 1`: `o.x = 0x5B0`, `o.y = 0x70`;
  `camX = player.x - 0x124`; `Anim_Set(1, 2)`; `g_area = 0x0B`; `Music_Play(2)`. The camera x
  ends at 0x48C, **not a multiple of 8**: the tile background is drawn for camX & ~7 while the
  player sprite uses the exact camX, so inside this cave the player is drawn 4 px (2 bytes) to
  the left of where he stands, until another teleport resets the camera [O arithmetic; confirmed
  by capture: state `ega/w_cave_f7` has camX 0x48C, the background matches camX 0x488 and the
  player (SPRITES 73 mirrored) is anchored at byte 0x94, not 0x96, §12.4].
* **P6**: no ISPLAYER test. mask = 0x7C; if `o.dx < 0` (s): `Music_Stop`; `o.x = 0x5B0`,
  `o.y = 0x770`; `camX = player.x - 0xA0`; `g_transition = 2`; `g_fullRedrawReq = 1`;
  `Anim_Set(1, 6)`; `g_playerState = 9`; `g_area = 0`; `Music_Play(0)`. Return mask.
* **P10**: as P6 with `o.dx > 0` (s), target (0x3B0, 0x7F0).
* **P34**: mask = 0xFF; if `Cell_GroundCheckPlayer() == 1`: `o.x = 0xC0`, `o.y = 0x170`;
  `camX = player.x - 0x20`; `Anim_Set(1, 3)`. (`Cell_GroundCheckPlayer` already stopped the music;
  nothing restarts it, so the upper room is silent [O].)
* **P35**: mask = 0x0F; if `Cell_GroundCheckObj() == 1`: `o.x = 0x4C0`, `o.y = 0x70`;
  `Anim_Set(1, 6)`; `g_area = 9`; `Music_Play(2)`. Return mask (0x0F: Up/Down/Left/Right only).

**`Cell_BonusCoins`** 1568:1ed8 — type 73, cell 26 (upper room). No kind or ISPLAYER test [O]:
if `(o.x & 0x7F) == 0x48` and `picture[26]` (DS:0CF6) == 0x4B: `picture[26] = 0x49`;
`Sfx_Play(4)`; `g_fullRedrawReq++`; `g_coins += 5` (no cap). Return 0xFC. Once per game
(the picture is saved and reset by `Cells_ResetVariable`, §3.5). An elf at that spot would also pay
the player, but no elf lives in this room [I].

### 6.5 Gates and the climbs between levels

The three locks and how the player gets from one level to the next [O cells; captures 051–057,
078–081, 094–097 show the same sequence]:

| Climb | Gate (lock) | Shaft above | Boundary | Arrives |
|---|---|---|---|---|
| level 0 → 1 | EF tree trunk (`Cell_P31_GateL0`), needs key state 2 | DF (`Cell_P25_TrunkShaft`), CF (`Cell_P46_V46_Boundary01`) | CF: `Cell_LevelBoundary01` | BF top of the trunk (`Cell_P30_V30_TrunkTop`), level-1 path row 11 |
| level 1 → 2 | AE geyser cave (`Cell_P39_V39_GateL1`), needs 4 | 9E, 8E geyser column (`Cell_P40_V40_Geyser`) | 9E/8E: `Cell_LevelBoundary12` | 7E geyser top (`Cell_P41_V41_GeyserTop`), level-2 path row 7 |
| level 2 → castle | 79 stone arch (`Cell_P42_GateL2`), needs 6 | – (teleport) | – (same scene) | 73 castle hall door |

Falling back: the geyser hole 7E drops the player down the column to AE (level 1); the bridges
84 and B7 (tier ≥ 2) drop him through 94/A4 to B4 (level 1) and through C7/D7 to E7 (level 0);
the ice cave drops him to the mine tunnel (§6.7).

**`Cell_P31_GateL0`** 1568:0f17 — cell EF [O]:
1. mask = 0x4FF. If not ISPLAYER: return (garbage AX).
2. If `g_keyState == 2`:
   * if `o.y & 0x7F != 0x70`: `g_area = 2` (no music call); if `(o.x & 0x7F) != 0x48`:
     `o.x = (o.x & 0xFF80) + 0x48`, `g_fullRedrawReq++`; if `o.y & 0x7F < 0x68` (u): `mask |= 0x100`
     and if `picture[EF]` (DS:0E88) == 0x40: `picture[EF] = 0x4C` (trunk becomes a ladder),
     `Sfx_Play(8)`, `g_fullRedrawReq++`;
   * else PATH;
   * then COLUMN(0x18, 0x50).
3. Else (no key): if hint 8 (byte DS:9246 = `g_hintFlags`+8) has bit 7 clear and the raw keys
   DS:923A have Up: DS:9246 = 0 (hint 8 "you need the key" becomes due); `mask &= 0xFFFC`.
4. Return mask.

**`Cell_P25_TrunkShaft`** 1568:0f13 — cell DF: return 0x5F3 (ladder, camera mode,
Up/Down, no walking) [O].

**`Cell_P46_V46_Boundary01`** 1568:0e41 — cell CF: object call: if
`o.y & 0x7F > 0x68` (u): `Cell_LevelBoundary01()`. Visible call: `Cell_DrawTreetop(0x600)`.
Return 0x5F3 [O].

**`Cell_P30_V30_TrunkTop`** 1568:0a6c — cell BF [O]:
1. mask = 0x4FF. Visible call: `Cell_DrawTreetop(0x600)`, return mask. Object call, not
   ISPLAYER: return mask.
2. If `o.y & 0x7F == 0x70`: if `o.dy > 0` (s): SNAP(0x7CC); `mask = (mask & 0xFFF3) | 0x100`
   (0x5F3: onto the ladder); else `mask &= 0xFFFE` (no Up). Then if `o.dy < 0` (s): `o.dy = 0`.
3. Else: `g_area = 2`; `mask = (mask & 0xFFF3) | 0x100` (0x5F3).
4. COLUMN(0x30, 0x50). Return mask.

**`Cell_P47_Boundary01`** 1568:0df3 — cell C7: object call and `o.y & 0x7F > 0x68`
(u): `Cell_LevelBoundary01()`. Return 0x1400 (fall) [O].

**`Cell_P19_Boundary12`** 1568:0e13 — cell 94: the same with
`Cell_LevelBoundary12`. Return 0x1400 [O].

**`Cell_P23_FallShaft`** 1568:0d94 — cells 3D 4D 5D A4 D7 [O]:
mask = 0x1400; if object call and `g_level == 2` and `o.y == 0x270` (exact; only cell 4D is at
that height): `o.x = 0x4C0`, `o.y = 0x170` (mine tunnel cell 29), `g_transition = 1`,
`g_fullRedrawReq = 1`, `g_area = 0x0E`, `g_level = 1`, `o.dy = 0`, mask = 0. Return mask.
The ice-cave drop (§6.7) relies on the fall animation's steps landing exactly on y 0x270: the
player enters the shaft at y 0x1F0 with dy = 0, so any fall step that divides 0x80 works; a step
that skips 0x270 would carry him down to the castle gate 6D, which ends the game [I].

**`Cell_P48_Landing`** 1568:16ed — cells B4, E7 (bottoms of the bridge shafts):
object call and ISPLAYER: if `o.y & 0x7F == 0x70`: `o.dy = 0`, mask = 0xFC; else mask = 0x14F0
(keep falling). Return mask (other calls return garbage) [O].

**`Cell_P39_V39_GateL1`** 1568:1329 — cell AE [O]:
1. mask = 0x4FF. Visible call or not ISPLAYER: go to 4.
2. If `g_keyState == 4`:
   * if `o.y & 0x7F != 0x70`: SNAP(0x740); if `o.y & 0x7F < 0x68` (u): (a) if
     `o.y & 0x7F < 0x60` (u): `g_area = 3`, `mask = (mask & 0xFFF1) | 0x800` (0xCF1: the geyser
     lifts him); (b) if `picture[AE]` (DS:0E06) == 0x38: `picture[AE] = 0x35`, `Sfx_Play(8)`,
     `g_fullRedrawReq++` (gate open, geyser starts);
   * else PATH.
   (No column test: Up works anywhere in the cell.)
3. Else (no key): hint 8 as in P31; `mask &= 0xFFFC`.
4. `Cell_DrawGeyser(0x500, 0x50)` (object and visible calls). Return mask.

**`Cell_P40_V40_Geyser`** 1568:1286 — cells 8E, 9E. No kind or ISPLAYER test:
mask = 0xCF1; if `o.y & 0x7F > 0x68` (u): `Cell_LevelBoundary12()`;
`Cell_DrawGeyser(0x500, 0x50)`. Return mask [O]. (0xCF1 = lift/slide + camera mode + Up.)

**`Cell_P41_V41_GeyserTop`** 1568:141c — cell 7E, the hole at the top of the
geyser on level 2. No kind test (§4.3) [O]:
1. mask = 0x4FF.
2. Player:
   * if `o.y & 0x7F != 0x70` (inside the hole or the column): `g_area = 3`;
     `mask = (mask & 0xFFF4) | 0x800` (= 0xCF4);
   * else: (a) if `o.dy > 0` (s) and `g_playerState != 0x1D`: SNAP(0x740), mask = 0xCF4;
     (b) if `0x11 <= g_playerState <= 0x14` (jumping) or `g_playerState == 0x1D` (sliding) or
     `(o.x & 0x78) <= 0x14` (u) or `(o.x & 0x78) >= 0x58` (u): `mask &= 0xF7FC` and if
     `o.dy < 0` (s): `o.dy = 0` (walk past the hole); else (walking through the middle):
     mask = 0xCF4, `Anim_Set(1, 0x1D)`, `g_playerState = 0x1E` (slide down; PlrSt_30_SlideDown plays
     sfx 1 each tick while 0x800 lasts, player doc).
3. Elf: if (`o.dx > 0` (s) and `(o.x & 0x78) <= 0x40` (u)) or (`o.dx < 0` and
   `(o.x & 0x78) >= 0x40`): `Elf_Turn()` (elves turn back at the hole).
4. `Cell_DrawGeyser(0x500, 0x1E)`. Return mask.
The slide carries the player down 7E → 8E → 9E (boundary, level 1) → AE, where `Cell_P39_V39_GateL1`
stops him on the path line (the 0x800 bit is gone there, so the slide ends) [I from the masks].
Jumping over the middle is safe. There is no test that the geyser is running.

### 6.6 Rope bridges and the net shop

**`Cell_P45_V45_Bridge`** 1568:15bc — cells 84 (level 2) and B7 (level 1)
(repaired decompilation agrees with the disassembly) [O]:
1. mask = 0xFC. If `g_slowCpu != 0`: return 0xFC (no bridge logic, no sprite; on a slow CPU these
   cells are type 28 anyway, §3.5).
2. Visible call: slot 12 `x = g_cellScreenX + 0x1C`, `y = g_cellScreenY + 0x73`, depth 0x1E,
   frame 0x178 (SPRITES 188, the bridge rail), draw index 0x0C, last animation 0;
   `g_decorCount++`. Return.
3. Object call with `g_tier` DS:9130 < 2 (u): return 0xFC (the bridge holds below 4 stars).
4. Player: if `0x11 <= g_playerState <= 0x14` (jumping) or `(o.x & 0x78) <= 0x28` (u) or
   `(o.x & 0x78) >= 0x50` (u): return 0xFC. Else: mask = 0x1400 (fall); if
   `picture[g_curCell] == 0x2F`: `picture[g_curCell] = 0x30` (broken), `g_fullRedrawReq++`;
   `Sfx_Play(9)` (on every such tick, also when already broken). Return 0x1400.
5. Elf: if `picture[g_curCell] == 0x30`: if (`o.dx > 0` and `(o.x & 0x78) <= 0x3C`) or
   (`o.dx < 0` and `(o.x & 0x78) >= 0x3C`): `Elf_Turn()`. Return 0xFC.
This is the tier-dependent gap behind the 4-star rank message that warns about gaps in the path
(ARCHITECTURE §10 Q3 said none was found) [O code; I link to the text].

**`Cell_P36_V36_NetShop`** 1568:1090 — cells BC (level 1) and EA (level 0).
Ghidra could not decompile it before the repair; the steps below are from the disassembly and
agree with the repaired decompilation [O]. `g_shopCoinsOwed` DS:92A1 (record +0x158; 4 at a new game).

Object call, player only (elves: return garbage AX):
1. mask = 0x2FC.
2. **Climbing into the cave** (`o.y & 0x7F != 0x70`):
   * if `player.y < 0x600` (u, cell BC): SNAP(0x640); else (cell EA): SNAP(0x540);
   * `mask |= 3`;
   * if `o.y & 0x7F < 0x6C` (u): `mask &= 0x00FE` (no Up, high byte cleared); then if
     `g_shopCoinsOwed == 0` and `o.dy <= 0` (s): `g_shopCoinsOwed = -1`, `g_nets` DS:910F `+= 5` (no cap);
     else if `o.dy > 0` (s) (climbing back down): `g_shopCoinsOwed = 4`, `picture[BC] = picture[EA] =
     0x47` (both shops closed again), `g_fullRedrawReq++`.
3. **On the path in front** (`o.y & 0x7F == 0x70`): `mask &= 0xFFFC`; if `o.dy > 0`: `o.dy = 0`;
   by `picture[g_curCell]`:
   * 0x46 (open): `mask &= 0xFDFF` (a coin is a normal search here), `mask |= 1` (Up enters);
   * 0x47 or 0x48: if `g_shopCoinsOwed == 0` and `g_fullRedrawReq == 0`: both pictures = 0x46,
     `g_fullRedrawReq++`; else if `g_shopCoinsOwed == 1`: both pictures = 0x48 and `g_fullRedrawReq++`
     **on every tick** while standing there (a full redraw every frame; it also keeps
     `main` from running the hint and menu updates, §8.1) [O; I side effect].
4. If `g_shopCoinsOwed == 0`: `mask &= 0xFFBF` (no coin drop once paid). Return mask.

Coins pay because PlrSt_27_CoinLeftDone/28 decrement `g_shopCoinsOwed` when the mask has 0x200 (player doc).

Visible call (sign on the rock):
slot 12: depth 0x50, draw index 0x0C, last animation 0, `x = g_cellScreenX + 0x1E`,
`y = g_cellScreenY + 0x3A`; `g_decorCount++`; frame = 0x194 (SPRITES 202, a net) if
`g_shopCoinsOwed == 0` and `g_fullRedrawReq == 0`; −1 (none) if `g_shopCoinsOwed == -1`; else
`0x188 + 2*(g_shopCoinsOwed - 1)` (digits 1..4 = SPRITES 196..199). With `g_shopCoinsOwed == 0` and a
redraw pending the formula gives 0x186 (SPRITES 195, a treasure marker) for that tick [O; I
visible for at most a frame].

Sequence: 4 coins dropped in front (picture 0x47, sign 4→3→2→1; at 1 the picture is 0x48) → owed
0 → picture 0x46, sign shows the net → Up climbs in → at `y & 0x7F < 0x6C` nets + 5, owed −1,
sign hidden → climbing down (dy > 0) resets owed to 4 and closes the rock. REFERENCE_CAPTURES
§5.5 and frames 071–077 show the counter and the refill [O].

### 6.7 Mine tunnel (scene 1) and ice cave (scene 2)

The level-1 doorways B2 and BB lead into the mine tunnel 28–2A; the level-2 ice doorway 7C leads
into the ice cave 2D–2F, whose right end drops the player down the rock shaft 3D/4D into the
mine tunnel (captures 058–067, 089–093) [O].

| Handler | Cell | Fires | Target (cell) | Camera | Then |
|---|---|---|---|---|---|
| `Cell_P17_MineDoorW` 1568:0b38 | B2 | `GroundCheckPlayer` | (0x440, 0x170) (28) | x − 0x40 | `Anim_Set(1,3)`, area 0x0A, tune 2 |
| `Cell_P18_MineDoorE` 1568:0bfb | BB | `GroundCheckPlayer` | (0x540, 0x170) (2A) | x − 0x100 | `Anim_Set(1,2)`, area 0x0B, tune 2 |
| `Cell_P37_MineExitW` 1568:0b86 | 28 | `o.dx < 0` and `(o.x&0x78) <= 0x48` | (0x148, 0x5F0) (B2) | x − 0xA0 | area 0, tune 0 |
| `Cell_P38_MineExitE` 1568:0c49 | 2A | `o.dx > 0` and `(o.x&0x78) >= 0x38` | (0x5B8, 0x5F0) (BB) | x − 0xA0 | area 0, tune 0 |
| `Cell_P60_IceDoor` 1568:1953 | 7C | `GroundCheckPlayer` | (0x6C0, 0x170) (2D) | x − 0x40 | `Anim_Set(1,3)`, area 0x0C, `Music_Stop`, `Sfx_Stop`, tune 2 |
| `Cell_P61_IceCaveExit` 1568:19ab | 2D | `o.dx < 0` and `(o.x&0x78) <= 0x48` | (0x648, 0x3F0) (7C) | x − 0xA0 | area 0, tune 0 |
| `Cell_P62_IceSlide` 1568:1a20 | 2E | `o.dx > 0` and `(o.x&0x78) >= 0x68` | (0x6A8, 0x1F0) (3D) | **0x600** | fall, below |

Exact steps [O]: P17/P18/P60: mask = 0xFF; if `Cell_GroundCheckPlayer() == 1`: set x, y,
`camX = player.x + cam`, then the listed calls in order (P60: `Anim_Set`, area, `Music_Stop`,
`Sfx_Stop`, `Music_Play(2)`). P37/P38/P61: mask = 0x7C (no ISPLAYER test); when they fire:
`Music_Stop`; x, y; camX; `g_transition = 2`; `g_fullRedrawReq = 1`; `Anim_Set(1, 6)`;
`g_playerState = 9`; `g_area = 0`; `Music_Play(0)`. Comparisons: dx signed, `x & 0x78` unsigned.

**`Cell_P62_IceSlide`** 1568:1a20: mask = 0x7C; if `o.dx > 0` (s) and
`(o.x & 0x78) >= 0x68` (u): `o.dy = 0`; mask = 0x1400; `o.x = 0x6A8`, `o.y = 0x1F0`;
`g_transition = 2`; `camX = 0x600`; `g_fullRedrawReq = 1`; `Anim_Set(1, 0x3F)` (fall);
`g_playerState = 0x1F`; `g_area = 0x0E`; `g_keyState = 4` (the level-1 key state, set before
landing on level 1); `Sfx_Play(9)`. Return mask. The fall continues through 3D (`Cell_P23_FallShaft`,
0x1400) to y 0x270 in 4D, where `Cell_P23_FallShaft` moves the player to (0x4C0, 0x170) in the mine tunnel
and sets `g_level = 1` (§6.5).

### 6.8 Level-2 gate, castle hall, castle wall, castle top

**`Cell_P42_GateL2`** 176a:02dc — cell 79, the stone arch. No ISPLAYER test [O]:
1. mask = 0xFF; `Cell_ConveyRightIf(-0x80, +0x80, 0x300, 0x400)` (elves walking left out of 79
   go to 88 while the player is in row 8 at x ≥ 0x300).
2. If `g_keyState != 6`: `mask &= 0xFFFC`; return. (No hint 8 here, unlike the other gates.)
3. If `o.y & 0x7F == 0x70`: PATH; return.
4. SNAP(0x4C0). If `o.y & 0x7F < 0x68` (u) and `g_fullRedrawReq == 0`:
   * if `picture[79]` (DS:0D9C) == 0x26: `picture[79] = picture[88]` (DS:0DBA) = 0x3C (arch
     open, both twins), `Sfx_Play(8)`, `g_fullRedrawReq++`;
   * else if `o.y & 0x7F < 0x60` (u): `o.dy = 0`; `o.x = 0x1C0`, `o.y = 0x3F0` (cell 73, castle
     hall door); DS:8FA5 = `g_slowCpu`; `Music_Stop`; `Music_Play(2)`; `g_area = 0x0D`;
     `g_slowScroll = 0`; `g_transition = 1`; `g_fullRedrawReq = 1`; `Anim_Set(1, 6)`;
     `g_playerState = 9`.
5. Return mask. (Opening and entering need two separate ticks with the redraw done in between.)

**`Cell_P44_CastleDoor`** 1568:0cbe — cell 73, the hall door (leads back out to the
arch). No ISPLAYER test [O]:
1. mask = 0x14FF.
2. If `o.y & 0x7F == 0x70`: `g_slowCpu = 0`; `mask &= 0x0FFD` (= 0x4FD); if `o.dy > 0`:
   `o.dy = 0`.
3. Else if Up held (`g_keys & 1`) and `g_playerState < 0x1F` (u) and `o.y & 0x7F < 0x6C` (u):
   `o.x = 0x4C0`, `o.y = 0x3F0` (cell 79); `g_area = 0`; `Music_Stop`; `Music_Play(0)`;
   `g_slowCpu = DS:8FA5`; `g_slowScroll = g_slowCpu`; `g_transition = 1`; `g_fullRedrawReq = 1`;
   `Anim_Set(1, 6)`; `g_playerState = 9`. (Otherwise the 0x1000 bit lets him fall back.)
4. COLUMN(0x26, 0x5A). Return mask.

**`Cell_P56_V56_CastleHall`** 1568:1734 — cells 71, 72, 74, 75 (pictures from
the layout: 0x27 = ladder, 0x2C = wall) [O]:

Object call (any object):
1. mask = 0x14FF.
2. If `o.y & 0x7F == 0x70`: `mask &= 0x00FF`; if `o.dy > 0`: `o.dy = 0`, `mask &= 0x00FD`.
   Else if `o.y & 0x7F < 0x68` (u): `mask |= 0x2100` (ladder + ladder jump). Else (0x68..0x6F):
   if `(o.x & 0x7F) != 0x40`: `o.x = (o.x & 0xFF80) + 0x40`, `g_fullRedrawReq++`.
3. If `(o.x & 0x78) < 0x30` or `> 0x60` (u): `mask &= 0xFFFC`. Else if `picture[g_curCell] ==
   0x27`: `mask = (mask & 0x0FFF) | 0x200C`; else `mask &= 0xFFFC`.
4. `Castle_CreatureHitCheck()`. Return mask.
Resulting masks on a ladder picture: floor 0x20FF (Up looks up, rising), 0x68..0x6F 0x24FF,
higher 0x25FF (climb, jumps).

Visible call: if slot 6's current animation (DS:9064) != 0: slot 6 frame = −1, slot 7 frame
(DS:9076) = −1, slot 6 last animation = 0, slot 6 animation = 0, `g_creatureCell` = 0 (the hall
removes any creature still showing).

**`Cell_P57_V57_CastleWall`** 1568:182b — rows 4–6 of the wall (type 57) [O]:

Object call (any object):
1. mask = 0x15F0; by `picture[g_curCell]`: 0x31 (full ladder) → 0x25FF; 0x2D (short ladder in
   the lower part) → 0x25FF, then if `(o.y & 0x78) < 0x60` (u): 0x11F0 (fall onto the ladder),
   else if `(o.y & 0x78) < 0x78` (u): 0x25FE (no Up: jump sideways); 0x25 → 0x25F3, then if
   `(o.y & 0x78) > 0x28` (u): 0x25FD (0x25 is used by no layout); other pictures (0x2A wall)
   keep 0x15F0 (fall).
2. If `(o.x & 0x78) < 0x30` or `> 0x60` (u): mask = 0x1400 (outside the ladder column you fall).
3. `Castle_CreatureHitCheck()`. Return mask.

Visible call: `Castle_CreatureSpawn()`.

**`Cell_P63_V63_CastleTop`** 1568:1aa4 — row 3 (type 63; the layout puts the
exit picture 0x32 in one of 32..35) [O]:

Object call (any object):
1. mask = 0x15F0; pictures 0x31, 0x2D, 0x25 exactly as P57 step 1.
2. Picture 0x32 (exit): mask = 0x25F3; if `o.dy < 0` (s) and `(o.y & 0x78) <= 0x70` (u)
   (climbing up out of the top) — **end of the climb**, in this order: `o.dy = 0`;
   `g_keyState = 0`; mask = 0; `g_level = 3`; `g_area = 0`; `g_slowCpu = DS:8FA5`;
   `g_slowScroll = g_slowCpu`; `g_transition = 2`; `o.x = 0x400`, `o.y = 0x500` (cell A8, end
   screen); `camX = 0x400`; `g_fullRedrawReq = 1`; `g_menuLock = 1`; `g_playerState = 0x29`;
   `Anim_Clear(1)`; `EndSeq_Setup()` (18cf:0038); `Anim_Set(6, 0x78)`; `Anim_Set(7, 0x7E)`;
   `Anim_Set(8, 0x7C)`; `Anim_Set(10, 0x8E)`; `Anim_Set(11, 0x8F)`; `Anim_Set(4, 0x90)`;
   total treasures (far pointer DS:9138 = record +0x0A) `+= g_treasuresFound` DS:9113 (16-bit
   add, no saturation); `Treasure_PickShowcase()` (4a03:038c).
3. (All pictures, also after step 2) `Castle_CreatureHitCheck()`; if `(o.x & 0x78) < 0x30` or
   `> 0x60` (u): mask = 0x1400. After the end step the new x 0x400 gives 0x1400 [O].
4. Return mask.

Visible call: `Castle_CreatureSpawn()`.

**`Castle_CreatureSpawn`** 176a:159e — visible handler part of types 57 and 63 (the Master of
Mischief in a wall window) [O]:
1. `g_castleCellIdx++` (1..4: position of this castle cell among the castle cells visited this
   tick).
2. If `g_creatureCell == 0`:
   * if `g_tier == 0`: return;
   * if `slot1.x - 0x20 == g_cellScreenX` (the player stands at this cell's center):
     `r = rand()`; if `(r & g_creatureMask[g_tier]) == 1`: `g_creatureCell = g_castleCellIdx`;
     `Anim_SetIfIdle(6, 0x3C)`; `Castle_PlaceCreature()`; return;
   * if `slot1.x + 0x20 == g_cellScreenX` (the player stands at the center of the cell to the
     left): `r = rand()`; if `(r & g_creatureMask[g_tier]) == 0`: `g_creatureCell =
     g_castleCellIdx`; `Anim_SetIfIdle(6, 0x3B)`; `Castle_PlaceCreature()`.
   (The first case falls through to the second when its rand test fails; the two position tests
   cannot both hold.)
3. Else if `g_creatureCell == g_castleCellIdx`: if slot 6's animation (DS:9064) == 0 (finished):
   slot 6 frame = −1, slot 7 frame = −1, slot 6 last animation = 0, `g_creatureCell = 0`; then
   (always, also when the animation still runs) `Castle_PlaceCreature()` (re-anchor at this
   cell's current screen position).
Masks per tier: 0, 0x1FF, 0xFF, 0x3F, so the chance per tick and qualifying cell is 1/512,
1/256, 1/64 (ARCHITECTURE §5.12) [O]. The creature is anchored to the visible-cell index, not to
a world cell: if the camera scrolls while it shows, it moves with the index [O; I visible].

### 6.9 Summary of the castle

The arch 79 (key 6) teleports to the hall door 73; the hall 70..76 has the floor-level ladder(s)
from the layout (74 always, 72 in layouts 2/3, 75 in layout 3); the wall rows 4..6 hold ladders
0x31 (full), 0x2D (short, upper part empty, no Up from its top: jump sideways with Left/Right),
0x2A (wall: fall); row 3 holds 0x33/0x34 (fall) and one 0x32 exit; climbing up out of the exit
ends the climb (§6.8). The creature appears only at tier ≥ 1, strikes by its animation's op −6,
costs a coin and knocks the player into a ladder jump [O; I for the descriptive words].

### 6.10 Handlers documented in the player doc: where they put the player

Only the transition part; their other behaviour is in `code_player_anim.md` [O]:

| Handler | Cell | Trigger | Target (cell, scene) | Camera / transition |
|---|---|---|---|---|
| `Cell_P64_EndScreen` 176a:0404 | A8–AA end screen | `DS:92D2 != 0` or `g_cmd` DS:9226 != −1 | (0x6A4, 0x1F0) (3D, still scene 3): falls down the chute 3D→4D→5D→6D; `Anim_Clear(3..11)`, `Sfx_Stop`, `Anim_SetIfIdle(1, 0x5D)`, state 0x22, mask 0x14F0 | camX 0x600, `g_transition = 2` |
| `Cell_P20_CastleGate` 176a:0485 | 6D castle gate | `y & 0x7F == 0x70` | total ≥ 300 (u): (0x170, 0x70) (02, scene 0), crown animations 0xA5/0xA6/0xA7, DS:925B = 1; else (0x4F0, 0x4E0) (99, scene 4), state 0x23. Both: record +9 = 0, `g_endState = 1`, `g_menuLock = 1`, `Anim_InitSlots`; above the floor mask 0x14F0 | `g_transition = 1` |
| `Cell_P71_V71_ToRankScreen` 176a:0fa6 | 01 clubhouse left | `y & 0x7F == 0x60` anywhere in the cell (after looking up; mask 0xF2 above 0x6C); in the left part (`x & 0x78 <= 0x43`) Left is blocked: dx = 0, mask 0xDB; visible call: `Cell_V7` | (0x680, 0x80) (1D, scene 4, rank room); state 1, `Anim_Clear(1)`, `Anim_Clear(3)`, DS:925D = 1 (rank display) | camX 0x680, `g_transition = 2` |
| `Cell_P72_RankScreen` 176a:1094 | 1D rank room | mouse buttons changed (DS:90F4 != DS:9143), or `g_cmd` == 8 or 0, or `g_keys & 0x42` | (0xF0, 0x70) (01, scene 0); state 9, `Anim_Set(1,6)`, `Game_AdvanceEnd`, DS:0100 = 0 | camX 0xC0, `g_transition = 2` |
| `TreasureRoom_Leave` 176a:0f47 | (from `Cell_P70_TreasureRoom`) | – | (0x170, 0x70) (02, scene 0); state 9, `Anim_InitSlots`, `Anim_Set(1,6)`, `Game_AdvanceEnd` | `g_transition = 1` |

The chute cells 3D/4D are drawn with MOUNT3 block numbers (0x2A, 0x43) while CLOSE.PAN (5 blocks)
is loaded, so the PAN buffers hold a mix of CLOSE data and stale MOUNT3 data there (the PAN loader
unpacks in place from the end of its buffers, §7.2). Blocks 0x2A and 0x43 of MOUNT3 use map
bytes 21504..22015 and 34304..34815 and tile bytes 21216..44832, all outside what CLOSE.PAN
overwrites (map 0..2559 and 0x9D88..; tiles 0..9343 and 0xC969..), so the stale MOUNT3 shaft is
shown intact [O]: frame 1221 of `ega_r2_end_t2_nokey.avi` equals that render at camera
(0x600, 0x188) in 583 of 624 tiles (the rest under the player). Rows 5–6 (blocks 3, 4) are proper
CLOSE blocks with the same stone-wall shaft. A port must keep the previous PAN's buffers (or
load MOUNT3's blocks 0x2A/0x43 for these two cells) to show this fall correctly.

---

## 7. Scene loading (`scene.c` 1437) and palettes

### 7.1 `Scene_Update` 1437:0034 (per frame, main loop)

Called every frame after `Frame_Begin` [O main 130f:0007]. Steps [O]:

1. **Blank the menu bar for scenes 3/4.** If `g_fullRedraw` DS:8FBB != 0 and `g_menuLock`
   DS:926C == 1 and `g_sceneWanted` DS:8FCA >= 3 (u) and `g_loadedScene` DS:8FBE < 3 (u):
   `Gfx_FillRect(x 0..0x9F, y 0..0x0A, pattern 0xFF)` (rows 0..10 black); DS:90F8 = 6;
   `Hud_DrawPromptIcon()`; DS:036C = 1 (menu bar hidden); `g_dirty++`.
2. **Load a new scene** when `g_sceneWanted != g_loadedScene` and `g_uiMode == 0`:
   1. If DS:9136 <= 1 (s) (the program runs from drive A: or B: [I]): `MsgBox_Draw(0x16)` (the
      "one moment" box [I]) and `Present_Dirty()`.
   2. If `g_endState` DS:9234 == 2 (leaving the treasure room after a game): `Music_Stop`,
      `Sfx_Stop`, `Game_End`, `Cells_ResetVariable`, `Layout_ClearSlots`, `Game_Start`,
      `g_endState = 0`, `g_messagePending` DS:911C = 2, DS:922E = 1, `Dirty_Clear`.
   3. `g_quit = 1` — `Game_Tick` returns at once while the files load.
   4. If `g_sceneWanted < 3` (u): `n = Quiz_ReorderSlots()`; `Elves_Spawn(n)` (quiz and elves
      docs; this is where the elves are respawned and their `rand()` calls happen, in the frame
      loop).
   5. `Scene_LoadPan(g_sceneWanted)` (§7.2; file index = scene).
   6. Sprite bank: if `g_sceneWanted >= 3` (u): file `g_sceneWanted + 13` (16 CLOSE.PAK,
      17 SHACK.PAK); else if `g_loadedScene >= 3` (u; also the initial 0xFF): file 6
      (SPRITES.PAK). Open with mode 0x8004 — on failure **return** (with `g_quit` still 1, so the
      frame loop ends and the game exits); read up to 0xFFF0 bytes into the bank (DS:37BC far
      pointer); if exactly 0xFFF0 were read, read up to 0xFFF0 more at bank + 0xFFF0; close.
      Moving between scenes 0..2 keeps the bank.
   7. `g_loadedScene = g_sceneWanted`; DS:63BA = 1 (sound doc); `g_quit = 0`.
   8. If DS:9136 <= 1 (s): `g_fullRedraw++`; DS:0102 = 0; DS:37CC = DS:37CA = DS:37C8 = 0;
      `g_dirty++`; `g_inputContext` DS:922C = 0. (On a hard disk the scene change gets no redraw
      of its own; teleports request one themselves and level boundaries do not need one.)
   9. DS:9259 −= 1.
3. **Restore the menu bar.** If `g_fullRedraw != 0` and DS:036C == 1 and `g_sceneWanted < 3`:
   DS:036C = 0; `MenuBar_DrawTitle(0, 0)`, `(1, 0)`, `(2, 0)`; DS:91FB++; `g_dirty++`.

`Game_Tick` runs the player, elves and visible cells only while `g_loadedScene == g_level`, so
the world freezes between the tick that changes `g_level` and the frame that loads it [O].

### 7.2 `Scene_LoadPan(i)` 1460:05e8

Opens data file i (mode 0x8004; returns 0 if it fails, which `Scene_Update` ignores). Reads the
four header words (packed map size m, width, height, packed tile size t), reads the packed map
to `g_mapPtr + 0xA400 - m` (segment 2897) and the packed tiles to `g_tilesPtr + 0xE880 - t`
(segment 32D7), closes the file, then `Map_Unpack(SI = 0xA400 - m, BX = width, CX = height)` and
`Tiles_Unpack(SI = 0xE880 - t, DX = t)` unpack in place towards offset 0 (formats in
`fmt_graphics.md` §3) [O]. Nothing else in the buffers is cleared: after a small PAN (CLOSE,
SHACK) the map and tile buffers still hold the previous PAN's data beyond the new one's size.

### 7.3 `Pal_Apply` 1437:0001

Called once by `Game_Init`. MCGA (`g_videoType` == 5): copies the 48 bytes DS:0144 to DS:38EA and
calls `Vga_SetDac16`; CGA (2): `Gfx_BuildCgaColorMap(CX = DGROUP, DX = 0x174)`; other adapters:
nothing (their palettes are set with the video mode) [O]. Scenes do not change the palette: all
five scenes share the 16 logical colours (`fmt_graphics.md` §2) [O].

---

## 8. Camera and scrolling

### 8.1 Camera state

| DS | Meaning |
|---|---|
| 37EA / 37EC | `camX` / `camY`: world position of the play field's top-left pixel (live, changed in the tick) |
| 37EE / 37F0 | camera step of the last `Player_Move` (pixels for y; for x the doubled byte value, see §8.3) |
| 37F2 / 37F4, 37F6 / 37F8 | the above latched by `Frame_Begin` for this frame |
| 9124 / 9126 | camera cell column / camera cell row × 16 (latched) |
| 920D | `(camX & 0x78) << 2`: byte offset of the camera's tile column inside a block's map |
| 920F | `(camY >> 2) & 0x1E`: byte offset of the camera's tile row inside a map column |
| 9297, 9299, 9270 / 929B, 929D, 9272 | camX, 920D, 9124 / camY, 920F, 9126 as last drawn |

Screen mapping [O 4306:04e7, 130f:0326]: screen x byte = `((wx - camX) >> 1) + 2`, screen y =
`wy - camY + 0x10`. The tile background is drawn for `camX & ~7` and `camY & ~7`; sprites use the
exact values. Play field: x bytes 2..0x9D, y 0x10..0x8F (39 tile columns × 16 tile rows).

The camera moves only inside `Player_Move` (and when a handler sets it). Because `Player_Move`
sets `g_stepDone` whenever the camera moves, it moves **at most one step per drawn frame**, and
`Scroll_Update` only ever has to scroll one step [O].

### 8.2 `Frame_Begin` 130f:025a (per frame)

With interrupts disabled (CLI ... STI) [O]: `37F2 = camX`, `37F4 = camY`, `37F6 = 37EE`,
`37F8 = 37F0`, `g_sceneWanted = g_level`, `g_fullRedraw = g_fullRedrawReq`, `DS:8FB2 =
g_transition`, byte `DS:8FBD = DS:925D`, `DS:8FC8 = DS:9278`, bytes `8F9F..8FA2 = 9201..9204`,
`DS:922E |= DS:925B`, `Sprites_Snapshot(0, DS:9283)`, `g_stepDone = 0`. Then:
`920F = (37F4 >> 2) & 0x1E`; `9126 = colpart(37F4) << 4`; `920D = (37F2 << 2) & 0x1E0`;
`9124 = colpart(37F2)` (§3.2); if `9124 >= 0x10` (u): `9124 -= 0x1F0`, `9126 -= 0x10` (a
negative camera x is column 15 of the previous row); if `g_fullRedraw != 0`: `37F6 = 37F8 = 0`
(no scrolling in a redraw frame).

### 8.3 Camera follow in `Player_Move` 18cf:15f0 (per tick)

`SI = DS:8FA7`, `DI = DS:8FA9` (player screen offsets at the start of the tick, §2.5;
comparisons below signed) [O]:

1. `DS:926A = Cell_GetMask()` (handlers may move the player, set `camX`, set `g_transition`).
2. Camera re-anchor:
   * `g_transition & 1`: `camY = player.y & 0xFF80`; `camX = player.x - 2*SI` (the player keeps
     his horizontal screen position, the camera snaps to the cell row);
   * else `g_transition & 2`: `camY = player.y & 0xFF80`; `DI = player.y - camY`;
     `SI = (player.x - camX) >> 1` (arithmetic); `DS:8FA7 = SI` (camX is the handler's value);
   * else: `camY = player.y - DI`; `camX = player.x - 2*SI` (conveyed players do not jump on
     screen).
   Then if `camX > 0x800` (u): `camY -= 0x80`, `camX += 0x800` (only a negative camX occurs).
3. `37EE = g_playerDX`, `37F0 = g_playerDY`. If both are 0: go to 7.
4. Horizontal: if (`g_playerDX < 0` and `SI <= 0x20`) or (`g_playerDX > 0` and `SI >= 0x7C`):
   the camera scrolls — if `g_slowScroll == 1`: `g_playerDX *= 2`, `37EE = g_playerDX`.
   Otherwise the player moves on screen: `SI += 37EE`, `37EE = 0`.
5. Vertical: `lim = 0x70` if `DS:926A & 0x400` else `0x40`; if (`g_playerDY < 0` and `DI <= lim`)
   or (`g_playerDY > 0` and `DI >= 0x70`): scroll — if `g_slowScroll == 1`: `g_playerDY *= 2`,
   `37F0 = g_playerDY`. Else `DI += 37F0`, `37F0 = 0`.
6. `player.x += 2*g_playerDX`; `player.y += g_playerDY`; if not `g_transition & 2`:
   `Obj_WrapWorldX()`, `camY = player.y - DI`, `camX = player.x - 2*SI`, and the `> 0x800` fix of
   step 2 again.
7. `g_stepDone |= 37EE | 37F0`; `g_playerDX = g_playerDY = 0`. (The rest — player sprite slot
   position and depth — is in the player doc.)

Consequences [O]: walking, the player sits between screen bytes 0x22 and 0x7E (68..252 px), as
measured in REFERENCE_CAPTURES §5.4 ("about x ≈ 65 ... ≈ 252"); climbing up the camera starts at
0x40 rows below its top (0x70 in cells with mask 0x400, i.e. at once), climbing down at 0x70. On a
slow CPU a scrolling step moves player and camera twice as far (16 px) as a non-scrolling step.
After a teleport the tick's dx/dy are still added in step 6 (with transition 2 the camera is not
re-derived afterwards).

### 8.4 `Scroll_Update` 130f:04e5 (per frame)

Returns if `g_uiMode != 0` or DS:922E != 0 [O]. Horizontal part, when `37F2 != 9297` and
`37F6 != 0` and (`920D != 9299` or `9124 != 9270`):

1. `c = 9124 + 9126` (cell), `off = 920D` (tile column offset in that cell's map).
2. `37F6 < 0` (s) (camera moved left): `Scroll_PlayfieldRight(DI = 0x10, DX = bottom)`; new
   column x = 2. `37F6 > 0`: `Scroll_PlayfieldLeft(0x10, bottom)`; `c += 2`; `off += 0xA0`
   (slow scroll; x = 0x96) or `off += 0xC0` (x = 0x9A); if `off >= 0x200` (u): `off -= 0x200`,
   `c++`. Here `bottom = 0x6F` if `g_slowCpu == 1` else 0x8F — on a slow CPU the lowest 32 rows of
   the play field are neither shifted nor redrawn by horizontal scrolling [O; capture request 6].
3. `off += 920F`; `map = mapseg + (picture[c] & 0x7F) * 32` (paragraphs).
4. Bottom of the first piece: slow CPU: `b = 0x6F`, `r = 0`; else `r = off & 0x1F` (the camera's
   tile row in the cell) and `b = r ? 0x8F - 4*r : 0x8F`.
5. `Tile_DrawColumn(SI = x, DI = 0x10, DX = b, BX = off, CX = map)`.
6. If `g_slowScroll == 1`: second column: `off += 0x20`; if `off >= 0x200` (u):
   `map = map of cell c+1`, `off = 0` (the row offset is dropped — harmless because the slow CPU
   only scrolls horizontally with `camY` cell-aligned [I]); `x += 4`; `Tile_DrawColumn` again.
7. If `r != 0` (lower part from the next cell row): `Tile_DrawColumn(SI = x, DI = b + 1,
   DX = 0x8F, BX = off - 920F, CX = map of cell c + 0x10)`.
8. `9297 = 37F2`, `9299 = 920D`, `9270 = 9124`; `g_dirty++`.

Vertical part, when `37F4 != 929B` and `37F8 != 0` and (`920F != 929D` or `9126 != 9272`):

1. `c = 9124 + 9126`, `roff = 920F`, `coff = 920D`.
2. `37F8 < 0` (camera moved up): `Scroll_PlayfieldDown(0x10, 0x8F)`; new row y = 0x10.
   `37F8 > 0`: `Scroll_PlayfieldUp(0x10, 0x8F)`; slow scroll: `y = 0x80`, `roff += 0x1C`; else
   `y = 0x88`, `roff += 0x1E`; if `roff >= 0x20` (u): `roff -= 0x20`, `c += 0x10`.
3. For `x = 2; x < 0x9E (s); x += 4`: `Tile_DrawColumn(SI = x, DI = y, DX = y + (slow ? 0x0F :
   7), BX = coff + roff, CX = map of cell c)`; `coff += 0x20`; if `coff >= 0x200` (s):
   `coff -= 0x200`, `c++`.
4. `929B = 37F4`, `929D = 920F`, `9272 = 9126`; `g_dirty++`.

### 8.5 `Playfield_Redraw` 130f:0326 (per frame)

Only when `g_fullRedraw` (latched) != 0 [O]:

1. Column range: if DS:8FC8 (latched search redraw) == 0: `x0 = 2`, `x1 = 0x9E`. Else (redraw
   only the searched cell): `t = ((((player.y & 0xFF80) << 4) + (player.x & 0xFF80) -
   ((camY & 0xFF80) << 4) - camX) >> 1) & 0xFFFC` (arithmetic shift; the first terms turn the
   cell into an unwrapped strip position); `x0 = t + g_searchSide·0x40 + 2` (DS:9276 = −1/0/+1);
   `x1 = x0 + 0x44`; if `x0 < 2` (s): `x0 = 2`; if `x1 > 0x9E` (u): `x1 = 0x9E`.
2. `Gfx_SetDirtyTracking(0)` (bit 0 of DS:37AE clear: the tile routine adds no dirty rectangles).
3. For `y = 0x10; y < 0x8F (s); y += 8`: `rowcell = 9124 + 9126`, `roff = ((y - 0x10) >> 2) +
   920F`; if `roff >= 0x20` (u): `rowcell += 0x10`, `roff -= 0x20`; for `x = x0; x < x1 (u);
   x += 4`: `coff = (x - 2)*8 + 920D`; `cell = rowcell + (coff >> 9)`, `coff &= 0x1FF`;
   `Tile_DrawColumn(SI = x, DI = y, DX = y + 7, BX = coff + roff, CX = mapseg +
   (picture[cell] & 0x7F) * 32)` (one tile).
4. `Gfx_SetDirtyTracking(1)`; `Dirty_AddRect(AX = x0, BX = 0x10, DX = x1 - x0 + 1, CX = 0x80)`.
5. `9297 = 37F2`, `9299 = 920D`, `9270 = 9124`, `929B = 37F4`, `929D = 920F`, `9272 = 9126`;
   `37F6 = 37F8 = 0`; `g_fullRedraw = 0`; **`g_fullRedrawReq = 0`**; DS:8FC8 = DS:9278 = 0;
   `g_dirty++`; byte DS:8FBA = 1 (shelves redraw).

`g_fullRedrawReq` is cleared here even if a tick set it again after `Frame_Begin` latched it; such a
request is lost [O; I race].

### 8.6 Transition dissolve (`Frame_Present` 130f:01c2, per frame)

[O] If `g_dirty == 0`: an empty loop of 1000 iterations (the only frame pacing) and return.
Else if the latched transition DS:8FB2 != 0: if `g_slowCpu == 0`: `Dirty_Clear()`; 350 (0x15E)
times: `r1 = rand() & 0x0F`, `y = r1*8 + 0x10`; `r2 = rand() & 0x0F`, `xs = r2*10`, if `xs > 0x91`
(s) then `xs = 0x91`; `Dirty_AddRect(AX = xs + 2, BX = y, CX = 8, DX = 0x0A)`; `Present_Dirty()`.
Then (any CPU) `Present_Full()` and `g_transition = 0`. Else `Present_Dirty()`.

So a teleport is shown as up to 350 random 20 × 8-pixel blocks of the new picture (16 × 16
possible positions: x = 2 + 10·r2 bytes for r2 = 0..14 and 147 for r2 = 15 after the clamp,
y = 16..136), then the whole screen. The capture shows the dissolve taking about 7 frames (`ega_doorway_dissolve.avi`,
REFERENCE_CAPTURES §5.7) [O]. 700 `rand()` calls per transition on a fast CPU, none on a slow one.

### 8.7 Segment-1000 routines (register interfaces)

[O] Back buffer: segment 2005, row y at `CS:01C9[y]` = `0x10 + y*0xA0`; x in bytes. CGA
(`g_videoType == 2`) keeps a 2-bpp back buffer: the same routines halve the byte counts and
`Tile_DrawColumn` converts 4-bpp tiles through the dither tables at 2005:7E00 (the table half
alternates every row). All routines end in a common tail (1000:190c) that, if bit 0 of DS:37AE is
set, calls `Dirty_AddRect(AX = DS:3082, BX = DS:3084, CX = DS:3080, DX = DS:307E)`.

| Routine | In | Does |
|---|---|---|
| `Dirty_AddRect` 1000:1181 | AX x (bytes), BX y, CX height, DX width (bytes) | marks the rectangle dirty (row pairs, even byte bounds; gfx doc) |
| `Scroll_Setup` 1000:1934 (near) | DI top, DX bottom | DS:3082 = 2, DS:307E = 0x9C, DS:3084 = DI, DS:3080 = AX = DX − DI + 1; CL = video type, CH = `g_slowScroll`; ES = DS = back buffer; BX = DX = row address of DI |
| `Scroll_PlayfieldLeft` 1000:18b3 | DI, DX | content moves left (camera right): each row, bytes 6..0x9D → 2..0x99 (slow: 0x0A..0x9D → 2..0x95); CGA 3..0x4E → 1..0x4C (slow 5.. → 1..) |
| `Scroll_PlayfieldRight` 1000:196d | DI, DX | content moves right: bytes 2..0x99 → 6..0x9D, copied backwards (slow: 0x4C words from 0x94 down, i.e. bytes −2..0x95 → 6..0x9D; the extra word at 6..7 comes from the row above and is overwritten by the second new column) |
| `Scroll_PlayfieldUp` 1000:1a86 | DI, DX | content moves up 8 rows (slow 16): rows DI+8..DX → DI.., bytes 2..0x9D (CGA 1..0x4E) |
| `Scroll_PlayfieldDown` 1000:1aef | DI, DX | content moves down 8 rows (slow 16), copied from the bottom up |
| `Tile_DrawColumn` 1000:19cf | SI x, DI top, DX bottom, BX map byte offset, CX map paragraph | from map word `CX:BX` downwards, copy each 8 × 8 tile (32 bytes at paragraph `tileseg + word`) row by row (4 bytes per row) into x, rows DI..DX; stops after DX − DI + 1 rows, even inside a tile |

The scroll routines leave the uncovered strip unchanged; `Scroll_Update` fills it.

---

## 9. Transitions: where the player and the camera land

`t` = `g_transition` set (0 = none, only `g_fullRedrawReq`); camera per §8.3: t = 2 → `camX` as
listed, `camY = y & 0xFF80`; t = 1 or 0 → `camX = x - 2*SI` with SI the player's screen byte
offset before the move ("kept"), t = 1 → `camY = y & 0xFF80`, t = 0 → `camY = y - DI`. Every
row also requests a full redraw. "x/y" are the target before the tick's dx/dy are added [O].

| From | Handler | Trigger | To (x, y) | Cell | Scene | camX | t | Anim / state | Area, tune |
|---|---|---|---|---|---|---|---|---|---|
| new game | `Users_LoadPlayer` 47c7:0b7d (and `Users_InitNewPlayer` 47c7:0ab5) | – | (0x140, 0x70) | 02 | 0 | 0xC0 (camY 0) | – | – | 5 |
| 02 | P12 door | Up, rise < 0x6C, `x&0x78 < 0x5A` | (0x1C0, 0x7F0) | F3 | 0 | 0x120 | 2 | 6 / 9 | 0, 0 |
| 02 | P12 prize | rise to `y&0x7F == 0x60`, `x&0x78 >= 0x5A` | (0x4F0, 0x4E0) | 99 | 4 | kept | 0 | 0x51 / 1 | – |
| 02 | P12 crown | slot 1 last anim 0xA5 | (0x4F0, 0x4E0) then P71 → (0x680, 0x80) | 1D | 4 | 0x680 | 2 | – / 1 | – |
| F3 | P11 | rise < 0x68 | (0x140, 0x70) | 02 | 0 | 0xC0 | 2 | 6 / 9 | 5, 1 |
| 01 | P71 (player doc) | rise to 0x60 | (0x680, 0x80) | 1D | 4 | 0x680 | 2 | – / 1 | – |
| 1D | P72 (player doc) | any key / click | (0xF0, 0x70) | 01 | 0 | 0xC0 | 2 | 6 / 9 | – |
| 04 | P50 | always | (0x460, 0x1F0) | 38 | 0 | 0x440 | 2 | 3 / 0x0A | – |
| 38 | P51 | walking left | (0x200, 0x70) | 04 | 0 | 0x100 | 2 | 2 / 0x0A | – |
| 3B | P52 | always | (0x460, 0x270) | 48 | 0 | 0x440 | 2 | 3 / 0x0A | – |
| 48 | P53 | walking left | (0x580, 0x1F0) | 3B | 0 | 0x480 | 2 | 2 / 0x0A | – |
| 4B | P54 | always | (0x460, 0x2F0) | 58 | 0 | 0x440 | 2 | 3 / 0x0A | – |
| 58 | P55 | walking left | (0x580, 0x270) | 4B | 0 | 0x480 | 2 | 2 / 0x0A | – |
| 5B | P65 | always | (0x460, 0x370) | 68 | 0 | 0x440 | 2 | 3 / 0x0A | – |
| 68 | P66 | walking left | (0x580, 0x2F0) | 5B | 0 | 0x480 | 2 | 2 / 0x0A | – |
| EB | P5 | rise < 0x68 | (0x460, 0x70) | 08 | 0 | 0x440 | 2 | 3 / 9 | 7, 2 |
| F7 | P9 | rise < 0x68 | (0x5B0, 0x70) | 0B | 0 | **0x48C** | 2 | 2 / 9 | 0x0B, 2 |
| 08 | P6 | walking left | (0x5B0, 0x770) | EB | 0 | 0x510 | 2 | 6 / 9 | 0, 0 |
| 0B | P10 | walking right | (0x3B0, 0x7F0) | F7 | 0 | 0x310 | 2 | 6 / 9 | 0, 0 |
| 0A | P34 | rise < 0x68 | (0xC0, 0x170) | 21 | 0 | 0xA0 | 2 | 3 / 9 | music off |
| 21 | P35 | rise < 0x6C | (0x4C0, 0x70) | 09 | 0 | kept | 1 | 6 / 9 | 9, 2 |
| B2 | P17 | rise < 0x68 | (0x440, 0x170) | 28 | 1 | 0x400 | 2 | 3 / 9 | 0x0A, 2 |
| BB | P18 | rise < 0x68 | (0x540, 0x170) | 2A | 1 | 0x440 | 2 | 2 / 9 | 0x0B, 2 |
| 28 | P37 | walking left, `x&0x78 <= 0x48` | (0x148, 0x5F0) | B2 | 1 | 0xA8 | 2 | 6 / 9 | 0, 0 |
| 2A | P38 | walking right, `x&0x78 >= 0x38` | (0x5B8, 0x5F0) | BB | 1 | 0x518 | 2 | 6 / 9 | 0, 0 |
| 7C | P60 | rise < 0x68 | (0x6C0, 0x170) | 2D | 2 | 0x680 | 2 | 3 / 9 | 0x0C, 2 |
| 2D | P61 | walking left, `x&0x78 <= 0x48` | (0x648, 0x3F0) | 7C | 2 | 0x5A8 | 2 | 6 / 9 | 0, 0 |
| 2E | P62 | walking right, `x&0x78 >= 0x68` | (0x6A8, 0x1F0) | 3D | 2 | 0x600 | 2 | 0x3F / 0x1F | 0x0E, sfx 9 |
| 4D | P23 | falling, `g_level == 2`, `y == 0x270` | (0x4C0, 0x170) | 29 | 1 | kept | 1 | – | 0x0E |
| 79 | P42 | key 6, arch open, rise < 0x60 | (0x1C0, 0x3F0) | 73 | 2 | kept | 1 | 6 / 9 | 0x0D, 2 |
| 73 | P44 | Up, rise < 0x6C | (0x4C0, 0x3F0) | 79 | 2 | kept | 1 | 6 / 9 | 0, 0 |
| exit 0x32 | P63 | climbing up | (0x400, 0x500) | A8 | 3 | 0x400 | 2 | end seq. / 0x29 | 0 |
| A8–AA | P64 (player doc) | any key | (0x6A4, 0x1F0) | 3D | 3 | 0x600 | 2 | 0x5D / 0x22 | – |
| 6D | P20 (player doc) | lands, total ≥ 300 | (0x170, 0x70) | 02 | 0 | kept | 1 | 0xA5 / 9 | – |
| 6D | P20 (player doc) | lands, total < 300 | (0x4F0, 0x4E0) | 99 | 4 | kept | 1 | – / 0x23 | – |
| 98–9B | `TreasureRoom_Leave` (player doc) | – | (0x170, 0x70) | 02 | 0 | kept | 1 | 6 / 9 | – |

Seams and boundaries move no camera: path links shift player and camera together; level
boundaries change only `g_level` (§6.1).

---

## 10. `rand()` calls in these modules

Turbo C `rand` (4d12:001f; `src/rtl/borland_rand`) [O]:

| Order | Where | Calls | When |
|---|---|---|---|
| per tick, after `Elves_Update` | `Castle_CreatureSpawn` 176a:15c2 / 176a:1609 | 1 per visible castle cell (types 57, 63), left to right, whose center the player's sprite x matches (`slot1.x ∓ 0x20 == g_cellScreenX`) | only while `g_creatureCell == 0` and `g_tier >= 1`; the first test `(r & mask) == 1`, the second `(r & mask) == 0` |
| per transition, frame loop | `Frame_Present` 130f:01eb / 01fc | 2 × 350 = 700 (y first, then x, per block) | latched `g_transition != 0`, `g_dirty != 0`, `g_slowCpu == 0` |
| indirect | `Scene_Update` → `Elves_Spawn`; `Elf_Turn` (from P41, P45, the stale visible call of P41) → elf state handlers; `Elves_ShiftNearby` | elves doc | – |

In the original the timer interrupt runs ticks in the middle of frame-loop code, so the relative
order of tick-side and frame-side `rand()` calls depends on timing. A port has to fix one order
(suggestion in §14).

---

## 11. Sounds and music started here

| Event | Call | Handler |
|---|---|---|
| a gate opens (trunk, geyser cave, arch) | `Sfx_Play(8)` once | P31, P39, P42 |
| bridge breaking / standing over the broken middle (tier ≥ 2) | `Sfx_Play(9)` every such tick | P45 |
| sliding into the ice-cave shaft | `Sfx_Play(9)` | P62 |
| bonus coins | `Sfx_Play(4)` | `Cell_BonusCoins` |
| creature takes a coin | `Sfx_Play(5)` (only if coins > 0) | `Castle_CreatureHitCheck` |
| entering the ice cave | `Sfx_Stop` | P60 |
| doors and exits | `Music_Stop` then `Music_Play(tune)` with `g_area` as in §9 (tune 0 outside, 1 clubhouse, 2 caves, tunnels, castle) | P5, P6, P9, P10, P11, P12, P17, P18, P35, P37, P38, P42, P44, P60, P61 |
| up-passages | `Music_Stop` (in `Cell_GroundCheck*`) | P9, P11, P17, P18, P34, P35, P60 |
| trunk, geyser, falls | `g_area` = 2 (trunk), 3 (geyser), 0x0E (shafts) without a music call | P30, P31, P39, P41, P23, P62 |
| game end in `Scene_Update` | `Music_Stop`, `Sfx_Stop` | 1437:00b1 |

The slide sound (sfx 1 per tick while the 0x800 bit lasts) is played by PlrSt_29_PassageUp/30 (player doc).

---

## 12. Sprites drawn here, tables, and the Ghidra repair

### 12.1 Frames and animations

| Slot | Frame code (SPRITES frame) / animation | By |
|---|---|---|
| 12 | 0x176 (187, rock column) | `Cell_V10` (cells 09, 0B) |
| 12 | 0x178 (188, bridge rail) | `Cell_P45_V45_Bridge` (84, B7) |
| 12 | 0x17A / 0x17B (189 / 189 mirrored, rock edges) | `Cell_V7`, `Cell_V8` (walls) |
| 12 | 0x17C (190, treetop) | `Cell_DrawTreetop` (BF, CF) |
| 12 | 0x17E (191, wooden post) | `Cell_V59` (29) |
| 12 | 0x188/0x18A/0x18C/0x18E (196..199, digits 1..4), 0x194 (202, net), 0x186 (195, glitch) | `Cell_P36_V36_NetShop` |
| 12 | animation 0x53 | `Cell_V32_AnimDecor` (E6) |
| 12 | animation 0x54 (geyser) | `Cell_DrawGeyser` (7E, 8E, 9E, AE) |
| 6, 7 | animations 0x3C (own cell) / 0x3B (cell to the right) | `Castle_CreatureSpawn` |
| 3, 8, 10 | showcase treasure and treasure-room extras | `Cell_V49_Showcase` (03, 9A) |
| 1 | player animations 2, 3, 6 (walk left/right, stand [I]), 0x1D (slide), 0x3F (fall), 0x51 (prize room) | handlers of §9 |
| 4, 6, 7, 8, 10, 11 | 0x90, 0x78, 0x7E, 0x7C, 0x8E, 0x8F (end sequence) | `Cell_P63_V63_CastleTop` |

Frame contents identified by rendering the PAK frames (`extracted/scratch/dos_w1_world/
frames_187_202.png`) [O].

### 12.2 Data tables read from SST.EXE

Besides §3.1: DS:0B02 far pointer to slot 1 (`3d94:8fec`), DS:0AFA creature masks (4 words),
DS:7D7C..7D83 shelf-room redraw bytes (2 per room), DS:9201..9204 shelf-room flags, DS:9129,
DS:9254 (put-away showcase), DS:92C8 (border colour counter), DS:9211/9213 (showcase offsets,
initialised by `Game_InitVars`). The handler tables (DS:08A6, DS:09D2) hold code pointers; a port
implements the handlers in code and can use the tables only to check the EXE version [I].

### 12.3 Ghidra repair of the six undecompilable handlers

Cause [O]: these functions build a far pointer on the stack (`MOV [BP-2],DS`,
`MOV [BP-4],0x90DE`, `LES BX,[BP-4]`); with DS known to be 0x4D94 the decompiler folds it into the
linear value 0x4D9490DE and throws `AddressOutOfBoundsException`. Clearing the DS register
assumption over the bodies of 1568:1090, 1556, 15bc, 1c7b, 1d96 and 1e50 makes all six decompile
(globals then appear as raw DS offsets such as `*(int *)0x92a1`). The repaired decompilations agree
with §6. Script: `extracted/scratch/dos_w1_world/gh/scripts/FixFarPtr.java` (mode `wholeds`),
applied to `re/ghidra_proj/TMDos` and re-exported (`tools/ghidra_scripts/ExportAll.java`).

### 12.4 Checks against the real game

Runtime-agent answers in `extracted/scratch/dos_requests/world.answers.md` (EGA, DOSBox-X) and my
own pixel checks [O]:

| Claim | Evidence | Result |
|---|---|---|
| P9 camera 0x48C, sprite 4 px off | state `ega/w_cave_f7`: player (0x5B0, 0x70), camX 0x48C; `r2_w_cave_f7_01_inside.png`: tile vote puts the background at camX 0x488; SPRITES frame 73 mirrored found exactly with anchor byte 0x94 (`p9_sprite.py`) | confirmed |
| P34 door column 0x26..0x5A | Up at x 0x560 (x&0x78 0x60) did nothing, Up near 0x520..0x530 moved to (0xC0, 0x170) | consistent |
| bonus +5 once at x&0x7F 0x48 | coins 5 → 10 after passing x 0x348, picture gone, no second payment; AVI `ega_r2_bonus_coins.avi` | confirmed |
| geyser hole P41 | walking left from x 0x770: state 30 at x 0x750, next tick x 0x740 (SNAP) and y + 8, mask high byte 0x0C (0xCF4); slide to (0x740, 0x560), state 10, `g_level` 1, cell AE | confirmed |
| bridges P45 (tier forced to 2 in RAM) | level 3: breaks at x 0x230 (x&0x78 0x30), mask high byte 0x14, falls to (0x260, 0x5F0) cell B4, `g_level` 1; jump states 19/20 over 0x230 do not break it, standing at 0x240 does; level 2: breaks at 0x3B0, lands in E7, `g_level` 0 | confirmed |
| chute after the end screen | `ega_r2_end_t2_nokey.avi` frames 1212–1657: stone shaft, no garbage; frame 1221 = stale-buffer render at (0x600, 0x188), 583/624 tiles (`chute_render.py`) | confirmed |
| camera thresholds 0x20 / 0x7C | REFERENCE_CAPTURES §5.4: player held at x ≈ 65 (left) and ≈ 252 px (right) | consistent |
| seams need no redraw | PAN blocks of the boundary cells identical in both scenes (`seams.py`) | confirmed |
| dissolve | doorway dissolve over about 7 frames (`ega_doorway_dissolve.avi`) | consistent |

Timings measured by the runtime agent (for the player doc): walking 8 px per 5 ticks; geyser
slide and climbing 8 px per 7–8 ticks; bridge falls 8 px per 5 ticks.

---

## 13. Per tick versus per frame

| Work | Rate | Gate |
|---|---|---|
| `Cell_GetMask` for the player (`Player_Move`) and each elf | tick (47.3 Hz) | `g_stepDone == 0` at tick start, `g_loadedScene == g_level`, `g_uiMode == 0`; player also `g_transition == 0` |
| `Cells_UpdateVisible` (decorations, creature, geyser, stale-object effects) | tick | as above (not stopped by `g_transition`) |
| camera follow (`Player_Move`) | tick | at most one camera step per frame (`g_stepDone`) |
| `Frame_Begin` latch, `Scene_Update`, `Scroll_Update`, `Playfield_Redraw`, dissolve | frame | – |

Frame-rate dependent [O mechanism; I magnitude]: the dissolve length (350 presents), the time a
transition blocks the player (until the frame that presents it), the delay between a redraw
request and the redraw (next frame), and on slow machines everything (one step per frame).

---

## 14. Porting notes

### 14.1 Suggested engine structure (`src/dos/game/`)

* `world_grid.h/.cpp` — loads DS:0EAA/0CAA/0FAA, DS:11AA, DS:120A, DS:121C, DS:1252, DS:0AFA
  from the user's SST.EXE; `cellIndex(x, y)` with the original arithmetic (§3.2, unmasked index:
  assert instead of reading past the table), `wrapObject()` (§3.2), picture/type mutators,
  `castleReset()`, `applyCastleLayout()`, `adjustForCpu()` (if the layout module does not own
  them). Validate the handler tables DS:08A6/09D2 against the expected segment:offset values to
  refuse other EXE versions.
* `cells.h/.cpp` — `uint16_t cellObjectCall(Object&)` and `void cellsUpdateVisible()`, a switch
  on the cell type calling one function per original handler (names of
  `re/names/dos/code_1568-176a_cells.tsv`, addresses in comments). Pass a context struct
  {kind, obj, isPlayer, curCell, cellScreenX/Y, decorCount, castleCellIdx} instead of globals, but
  keep a `lastObject` pointer so visible calls see the stale object exactly as in §4.3.
* `camera.h/.cpp` — camX/camY, the latched copies and the step deltas; the follow algorithm of
  §8.3 (it lives in `Player_Move`; give the player module a `Camera::follow(...)` call).
* `scroller.h/.cpp` — full redraw, partial (search) redraw, incremental scroll (§8.4–8.5), on top
  of the wave-2 back buffer (`dos/gfx/back_buffer`: shift and tile-column primitives of §8.7).
  Draw tiles at `camX & ~7`, `camY & ~7`.
* `scene.h/.cpp` — `Scene_Update` (§7.1): PAN and PAK switching from the user's files, menu bar
  blank/restore, game-end hook, the "world frozen until the scene is loaded" rule.
* `transition.h/.cpp` — the dissolve (§8.6) as a presenter effect.

### 14.2 Must-keep behaviour and pitfalls

1. **Comparisons and widths.** `x & 0x78` and `y & 0x7F` tests are unsigned; dx, dy, the camera
   screen offsets SI/DI and `g_shopCoinsOwed` tests are signed; `camX > 0x800` is unsigned (catches
   negative camX). Several mask operations use 16-bit immediates that also clear the high byte
   (`& 0x00FC`, `& 0x00FE`, `& 0x00FD`, `& 0x00FF`, `& 0x00F2`, `& 0x0002`, `& 0x0FFD`): keep them
   literal.
2. **Never-taken guards**: `TEST x,0x78 / JC` (P50, P52, P54, P65, `Cell_ConveyLeft` player path,
   `Cell_ConveyLeftIf`) and `(x & 0x78) > 0x80` / `<= 0x78` are always true or false; the handler
   fires for any x in the cell.
3. **One-tick lag of the mask**: the state handler uses the mask of the previous tick; and the
   masked key bits are written back into `g_keys` (cleared until the next key make code). The
   input layer (wave 2) needs an equivalent of "the ISR sets the bit again on typematic repeat".
4. **Teleport landing** = target + the tick's (2·dx, dy); edge targets (shelf rooms, 04) rely on
   it. The ice-cave fall must hit y 0x270 exactly in cell 4D.
5. **Camera misalignment after P9** (camX 0x48C): keep the exact camX for sprites and use
   `camX & ~7` for tiles (confirmed by capture, §12.4).
6. **Stale object in visible calls** (§4.3): elf 14 turns while cell 7E is visible (tier 3).
7. **Redraw requests** are latched at the frame start and cleared by the redraw; with the
   original's asynchronous ticks a request made during the frame can be lost. In a deterministic
   port run all ticks of a frame before `Frame_Begin` of the next frame; then no request is lost
   (a deliberate, documented difference) [I].
8. **Scene change freeze**: after a handler changes `g_level`, no player/elf/visible-cell logic
   runs until the next frame has loaded the scene (`g_loadedScene == g_level`). Loads are instant
   in a port; keep the one-frame gap.
9. **Picture changes need a redraw**: gates, bridges, net shop, bonus spot all request one; the
   scroller only draws newly exposed columns.
10. **Net shop with one coin owed** requests a redraw every tick; `main` skips the hint, menu and
    sign-in updates while a redraw is latched (main-loop doc). Keep it or document the deviation.
11. **Elves see side effects only** (dx = 0 at walls, conveyors, `Elf_Turn`); their return mask is
    ignored. Handlers that do not test ISPLAYER would also teleport elves (P6, P10, P37, P38, P61,
    P62, P12, P44, P23, P42, shelf rooms, bonus spot); elves never reach those cells [I], so a
    port may restrict them to the player but should log if an elf ever triggers one.
12. **Void cells** (type 0): fall + border colour change; never reached in normal play.
13. **Slow-CPU mode** (`-X`): bottom 32 rows not scrolled, double camera steps (and double player
    steps while scrolling), no decorations (V10, V59, V32, treetop, bridge rail), no breaking
    bridges, no dissolve, castle forced to fast mode. Offer it as an option, default off.
14. **CGA**: the tile routine dithers per row with the 2005:7E00 tables (row parity counted from
    the top of each drawn piece; the play field starts at an even row). Easiest in a port: keep a
    4-bpp logical buffer and dither at present time with the same row parity (gfx doc).
15. **Dissolve timing**: the original's speed is that of 350 `Present_Dirty` calls; the capture
    shows about 7 frames at 59.92 Hz. Present about 50 random blocks per 60-Hz frame, and keep the
    700 `rand()` calls.

### 14.3 Frame-rate dependent items

The camera moves at most one step per presented frame; if the port presents at 60 Hz and ticks at
47.3 Hz, the gating is never the limit on a fast machine. Everything in §13 marked "frame" runs at
the presentation rate; nothing in these modules counts frames for gameplay except the dissolve.

---

## 15. Open points

1. Capture request 6 (slow-CPU horizontal scrolling, `extracted/scratch/dos_requests/world.md`)
   is unanswered; requests 1–5 are answered and agree with this spec (§12.4). The other cave
   door (EB) was not tried in a capture.
2. The look-up animation's dy and the walk animation's dx (player doc) decide exactly when doors
   fire and where edge teleports land.
3. DS:9136 is taken as the program's drive number (≤ 1 = floppy) [I]; message box 0x16 text not
   checked.
4. Whether INT 10h AX=1001h preserves AX (the void cell's mask) is BIOS behaviour [I].
5. Meaning of DS:63BA, DS:0102, DS:37C8..37CC, DS:9259, DS:91FB written by `Scene_Update`, and of
   DS:925B / DS:922E (HUD redraw), owned by other modules.
6. The visible effect of the elf-14 turn at the geyser (needs a tier-3 game and elf 14 near 7E).
7. Row 1 cells 10–11 and 14–1A have no entry; confirm they are unused in all paths (no
   `Obj_WrapWorldX` path leads there in play: row 0 ends in walls and type-0 cells) [I].

---

## Appendix: function index and coverage

**1568 (70/70 specified):** 0008 P5, 00ce P6, 0134 GroundCheckObj, 01b4 P9, 0203
GroundCheckPlayer, 029d P10, 0303 P34, 0341 P35, 0386 P11, 03f6 P12, 05cb P7, 0638 P8, 06a5 P50,
0718 P51, 078a P52, 07fc P53, 0880 P54, 08f2 P55, 0976 P65, 09e8 P66, 0a6c P30, 0b38 P17,
0b86 P37, 0bfb P18, 0c49 P38, 0cbe P44, 0d94 P23, 0df3 P47, 0e13 P19, 0e33 P24, 0e41 P46,
0e6d LevelBoundary01, 0ee3 DrawTreetop, 0f13 P25, 0f17 P31, 1010 P26, 1037 P27, 105e P28,
1077 P29, 1090 P36 (net shop, from disassembly), 1286 P40, 12b4 LevelBoundary12, 1329 P39,
141c P41, 1556 DrawGeyser (from disassembly), 15bc P45 (from disassembly), 16ed P48, 1734 P56,
182b P57, 18f1 CreatureHitCheck, 1953 P60, 19ab P61, 1a20 P62, 1aa4 P63, 1c7b V49 (from
disassembly), 1d92..1d95 NopV58/67/68/69, 1d96 V32 (from disassembly), 1ddb V10, 1dfa V59,
1e1c V7, 1e35 V8, 1e50 DrawObject (from disassembly), 1e9e PlaceCreature, 1eca P0_Void,
1ed8 BonusCoins, 1f0b Cells_UpdateVisible, 1fe4 Cell_GetMask.

**176a world part (17/17):** 0006 P1, 0019 ConveyLeft, 008a P2, 00a6 ConveyLeftIf, 00fd P3,
0110 ConveyRight, 0181 P4, 019d ConveyRightIf, 01f8 P13, 020a P14, 0225 P15, 0237 P16, 029d P22,
02af P21, 02c1 P43, 02dc P42, 159e Castle_CreatureSpawn. Transition part only (player doc):
0404 P64, 0485 P20, 0f47 TreasureRoom_Leave, 0fa6 P71_V71, 1094 P72.

**1437 (2/2):** 0001 Pal_Apply, 0034 Scene_Update. Plus 1460:05e8 Scene_LoadPan.

**130f scrolling (3/3 + 2 parts):** 025a Frame_Begin, 0326 Playfield_Redraw, 04e5 Scroll_Update;
camera part of 00fe Game_Tick; dissolve part of 01c2 Frame_Present.

**1000 scrolling (7/7):** 1181 Dirty_AddRect (interface), 18b3 Scroll_PlayfieldLeft, 1934
Scroll_Setup, 196d Scroll_PlayfieldRight, 19cf Tile_DrawColumn, 1a86 Scroll_PlayfieldUp, 1aef
Scroll_PlayfieldDown.

**18cf (camera parts):** 15f0 Player_Move (camera), 17c9 Obj_WrapWorldX.
