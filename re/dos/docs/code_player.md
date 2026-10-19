# SST.EXE (DOS 1990) segments 18cf, 4306, 4a03 and the end screens of 176a: player, animation scripts, search, end of game, treasure room

Scope (wave 1, "player" topic): every function of segment `18cf` (player.c, 69 functions), `4306`
(anim.c, 11), `4a03` (search.c, 4), the end-of-game and treasure-room functions of `176a` (cells2.c,
18 of its 35 functions: `Cell_P64_EndScreen` .. `Shack_ResetDisplay`, 176a:0404-159d), and the rank chest
screen of `130f` (`RankChest_Update`, `RankChest_DrawStar`), which is the last step of the end flow.
Wave-3 engineers implement from this document without reading the decompiled code.

Evidence: Ghidra export `re/dos/ghidra_export/` (`functions/*.c`, `disassembly.asm`), bytes of
`extracted/SST_unpacked.EXE`, the animation script table decoded from DGROUP, the PAK previews in
`extracted/previews/dos/gfx/`, and the EGA AVIs in `extracted/captures/dos/avi/` (decoded with
`tools/avi_frames.py`). Scratch scripts are in `extracted/scratch/dos_w1_player/`: `dis.py`
(disassembly with DGROUP names), `dec.py` (decompile with slot/field names), `scripts.py` (script
table dump), `animsum.py` (per-script summary: steps, ticks, movement, sfx). Capture requests:
`extracted/scratch/dos_requests/player.md`. Names: the canonical names of
`re/names/dos/` (merged in wave 2, decision table in `re/names/dos/README.md`).

Notation: **[O]** observed in code, bytes or captures; **[I]** inferred. Addresses `SSSS:OOOO`
(Ghidra, load segment 0x1000; file offset `(SSSS-0x1000)*16 + OOOO + 0x21F0`). `DS:XXXX` = DGROUP
`4d94:XXXX` (file offset `0x3FB30 + XXXX`). Numbers are hex where the code uses hex. All words are
16-bit; "signed"/"unsigned" says how the original compares them (JL/JG vs JB/JA). A *tick* is one
call of `Game_Tick` (130f:00fe), 236.696 / 5 = **47.34 Hz**; a *frame* is one pass of the frame loop
in `main` (130f:0007), free-running (ARCHITECTURE §3). Screen x positions of sprites are in
**bytes** of the 4-bpp back buffer (1 byte = 2 pixels); y positions are pixel rows; world
coordinates are pixels.

Cross-references: terrain masks and cell handlers are the world agent's topic (`code_world.md`:
mask bits §4.4, scenes and cell rows §3.3, cell types §5; its names "shelf rooms" and "rank room"
are used here too). Elves (4521, `code_elves_layout_hints.md`), input, HUD and menus (43a0, 4a85,
130f, 43cb, `code_ui.md`) belong to the other wave-1 documents; their interfaces with the player
are summarised in §2. Runtime measurements by the capture agent are in
`extracted/scratch/dos_requests/player.answers.md` and are cited as "answers §n".

---

## 1. Module overview

| Seg | Module | Functions | Role |
|---|---|---|---|
| `18cf` | player.c | 69 (18cf:000d-1852) | player state machine (72-entry handler table), movement and camera (`Player_Move`), world wrap, search start, castle-top end sequence (states 41-71) |
| `4306` | anim.c | 11 (4306:0022-09a1) | 13 sprite slots, animation script interpreter (`Anim_Tick`), slot start/stop helpers, snapshot / z-sorted draw / erase of actor sprites |
| `4a03` | search.c | 4 (4a03:0003-082d) | per-frame hiding-place search, showcase-treasure choice and hand-over, prize-room shelves |
| `176a` (part) | cells2.c | 18 (176a:0404-159d) | end-screen cell, castle gate, treasure room with its 53-case display switch, rank-screen entry/exit, treasure-room helpers |
| `130f` (part) | main.c | 2 (130f:0edf-1280) | rank chest screen (fill, star rows, CHAMPION blink) |

None of these functions calls `rand()` [O: callee lists in `functions.tsv`; no call to 4d12:001f
in 18cf, 4306, 4a03, 176a:0404-159d, 130f:0edf-1280]. The random sequence is therefore not touched by
anything specified here; the castle creature (176a:159e, world agent) and the elves are the random
consumers during play.

---

## 2. Runtime context (summaries of other modules)

### 2.1 Tick and frame order

`Game_Tick` (130f:00fe, every 5th timer interrupt) [O]:

```
if g_quit: return
if g_stepDone (DS:910d) == 0:
    g_playerScrX (DS:8fa7) = (player.x - camX) SAR 1           (signed 16-bit shift)
    g_playerScrY (DS:8fa9) = player.y - camY
    if (unsigned) g_playerScrX > 0x400:                       (player wrapped to the next row)
        g_playerScrX += ((player.y & 0xff80) - (camY & 0xff80)) << 3
        g_playerScrY  = (player.y & 0x7f) - (camY & 0x7f)
    Mouse_Poll; Input_Tick; Anim_Tick
    if g_loadedScene (DS:8fbe) == g_level (DS:928f) or g_uiMode == 0x80:
        Input_MouseWalk; Player_Update; Elves_Update; Cells_UpdateVisible
else:
    Anim_Tick; Input_Tick
DS:90fe -= 1, reload 12 below 0
byte DS:9100 -= 1; if it reached 0 and g_uiMode == 0: DS:9100 = 0x3c, g_seconds += 1
```

`g_stepDone` is set by `Anim_Tick` when any slot shows a new frame (§3.2) and by `Player_Move` when
the camera scrolls (§4.3); `Frame_Begin` (130f:025a) clears it at the start of each frame [O]. So at
most one "step" of logic happens per drawn frame. Frame loop order (ARCHITECTURE §3.3) relevant
here: `Frame_Begin` (latches, `Sprites_Snapshot(0, g_spriteSlotsDrawn)`) ... `Search_Update` ...
`Playfield_Redraw` ... `ShelfRoom_DrawShelf`, `RankChest_Update`, `Sprites_DrawSorted(0, n)`,
`Frame_Present`, `Sprites_EraseAll(0, n)` with n = DS:9283 = 13 while `g_uiMode == 0`, else 1
(only the pointer, slot 0) [O 130f:0097-00c3].

### 2.2 Keys and the terrain mask

`g_keys` (DS:9105) low byte = held keys: 1 up, 2 down, 4 left, 8 right, 0x10 net, 0x20 jump,
0x40 drop coin. The keyboard ISR sets/clears bits on make/break codes (Home/PgUp set Up+Left /
Up+Right); after each key event, if Up+Right or Up+Left is held it clears Up and Down and sets jump
0x20 (flag DS:9257 = 1), otherwise it clears 0x20 if DS:9257 was set; `Input_MouseWalk` turns Down
into 0x40 when the previous mask allows no vertical move (`DS:926a & 3 == 0`) and generates bits
from mouse clicks [O 43a0:0245-0274, 4a85:0228-024c; input agent].

`Player_Update` replaces `g_keys` by `(g_keys | 0xff00) & g_playerMask` (DS:926a) every tick
before the state handler runs [O 18cf:1835-183e]. Consequences:

* The **high byte** of `g_keys` is the high byte of the terrain mask of the player's cell
  (computed by the previous tick's `Player_Move`): 0x100 ladder, 0x200 net-shop payment spot,
  0x400 camera mode (§4.3), 0x800 up-passage / slide, 0x1000 no floor (fall), 0x2000 ladder jump
  possible [O uses in this module; meanings per ARCHITECTURE §4.1].
* The **low byte** is masked destructively: a held key whose bit the cell forbids is lost until the
  next make code (typematic repeat) arrives [O]. Measured (answers §10, §7): DOSBox-X repeats a
  held key after ≈24 ticks (507 ms) and then every ≈1.5 ticks (31 ms), so a player held back by a
  wall resumes 1-2 ticks after reaching open ground; pressing Up first and then Right gives **no
  jump** on a plain path (the mask clears Up at the next tick, so the diagonal never forms), while
  Right first and then Up jumps. A repeated grey PgUp sends an E0 prefix that the ISR treats as a
  release, clearing the jump bit for one tick (input agent).
* `DS:923a` keeps the unmasked value of this tick (read by some cell handlers) [O 18cf:1832].

### 2.3 Objects, camera, cells

The player object `g_player` (DS:37e2): +0 world x (0..0x7ff within a 16-cell row), +2 world y,
+4 = `g_playerDX` (DS:37e6), +6 = `g_playerDY` (DS:37e8). dx is in **bytes** (the object moves
2·dx pixels), dy in pixels. Elf objects have the same first four fields. `g_curObj` (DS:8fab, far)
points at the object being processed; `Player_Update` sets it to the player [O].

Camera `camX`/`camY` (DS:37ea/37ec, camY a multiple of 0x80 except during vertical scrolls). Scroll
requests `g_camStepX`/`g_camStepY` (DS:37ee/37f0) are latched by `Frame_Begin` and executed by
`Scroll_Update` (130f:04e5) [O].

`Cell_GetMask` (1568:1fe4, world agent) calls the player handler of the cell containing
`g_curObj`, which returns the terrain mask (DS:92c9) and may teleport the object, change
`g_level`, set `g_transition` (DS:9217: 1 or 2 while a teleport redraw is pending) and set
`g_fullRedrawReq` (DS:924f) [O]. `Player_Update` and `Anim_Tick` do nothing while `g_transition`
is non-zero [O 18cf:181a, 4306:0032].

### 2.4 Sound calls used here

`Sfx_Play(id)` (491b:06d7) starts effect `id` only if `id <= current effect id` (0xFFFF when idle;
lower ids have priority; an equal id restarts it). `Sfx_Stop` 491b:074b, `Music_Play(t)` 491b:0791,
`Music_Stop` 491b:0811. `DS:63b8` is the effect **loop** flag (the end sequence sets it around two
effects). Details in fmt_sound.md §7 [O].

---

## 3. anim.c (segment 4306): sprite slots and animation scripts

### 3.1 Data

**Slots** `g_spriteSlots` DS:8fd6, 13 × 22 bytes [O 4306:0022, 02c0]:

| Off | Field | Meaning |
|---|---|---|
| +0x00 | x | screen x in bytes; for a *child* slot an offset from its owner (§3.5) |
| +0x02 | y | screen y in rows (child: offset) |
| +0x04 | z | depth: records are drawn in **descending** z order (larger = further back) |
| +0x06 | frame | frame code: sprite index·2 + mirror bit; 0xFFFF = nothing |
| +0x08 | owner | slot index whose position this slot is relative to (itself = absolute) |
| +0x0a | anim | running script id, 0 = none |
| +0x0c | prev | last script that ended or was stopped (0 after `Anim_Clear`) |
| +0x0e | pos | script position in words, relative to the first entry (0 = before the first entry) |
| +0x10 | delay | ticks left on the current entry (decremented before testing) |
| +0x12 | obj | far pointer to an object, target of script op -4 (set by the elf code; never cleared by `Anim_InitSlots`) |

**Draw records** `g_drawRecs` DS:9145, 13 × 14 bytes: +0 x, +2 y, +4 z, +6 frame, +8 owner (bit 15
= already drawn this pass), +0x0a/+0x0c far pointer to the saved background (0 = none) [O].
**Draw order** `g_drawOrder` DS:92dc, 13 words [O]. Flags: `g_spritesChanged` DS:92f6,
`g_saveHeapReset` DS:92f8, save-heap pointer DS:37c8/37ca, high-water mark `g_saveHeapHigh`
DS:929f [O].

**Scripts**: `g_animScripts` DS:4f5a, far pointers (segment word 0x3d94 = DGROUP, unrelocated in
the file), ids 0..0xbe used (191 entries; entries 0xbf-0xc1 point into text and are never used)
[O]. Script layout (16-bit signed words):

```
+0  delay D      each entry lasts D+1 ticks
+2  count n      number of slots driven (slot s .. s+n-1); 1 or 2 in all scripts
+4  entries ...  each entry = [one optional op] + n frame codes (-1 = no sprite for that slot)
    -10          end of script
```

Ops (only one op per entry; it prefixes the entry's frame words) [O 4306:00e7-022e]:

| Op | Words | Effect |
|---|---|---|
| -2 v | 2 | `g_playerDX += v` (player movement, bytes) |
| -3 v | 2 | `g_playerDY += v` (rows) |
| -4 v | 2 | `slot.obj->[+4] += v` (the owning object's dx; used by elf scripts) |
| -5 v | 2 | `Sfx_Play(v)` |
| -6 | 1 | `g_creatureStrike` (DS:9264) += 1 (castle creature hit test) |
| -7 a b | 3 | `slot[s].x += a; slot[s].y += b` (moves the script's own slot) |
| -10 | 1 | end: `prev = anim; anim = 0; delay = 0; pos = 0`; frames unchanged |

Any other negative value (-1, -8, -9, ...) at the start of an entry is not an op: it is taken as the
first frame code of the entry [O]. No shipped script has two consecutive ops [O, `animsum.py`].
Example: script 4 (walk left) = `4, 1, -2 -4 149, -2 -4 151, ... -2 -4 163, -10`: 8 entries, each
moving the player 4 bytes (8 px) left and showing the mirrored walk frames 74..81 [O].

### 3.2 Anim_Tick (4306:0022), once per tick

```
if g_transition != 0: return
count = 13 if (g_uiMode & 0x7f) == 0 else 1      (menus/dialogs/hints/quiz: only the pointer)
held  = g_stepDone at entry                        (copied once)
for i in 0..count-1, slot = slots[i]:
    if slot.anim == 0: continue
    P = scripts[slot.anim]; n = P[1]
    slot.delay -= 1
    if slot.delay != -1: continue
    if held != 0: slot.delay = 0; continue         (retry next tick)
    slot.delay = P[0]
    k = 2 + slot.pos;  pos = slot.pos + n           (k = index of the next entry)
    op handling at P[k] as in the table; each op adds its length to k and pos;
      -10: (stop) pos = 0, delay = 0, no frames written, step NOT flagged
    slot.pos = pos
    if pos != 0:
        for j in 0..n-1: slots[i+j].frame = P[k+j]; slots[i+j].z = slot.z; slots[i+j].owner = i
        g_stepDone = 1
```

Timing rules that follow [O]:

* An entry is shown for **D+1 ticks** when no step is pending; when a step is pending (`held`) the
  advance is postponed tick by tick (delay held at 0).
* `held` is the value at entry: all slots due in the same tick advance together even though the
  first one sets `g_stepDone`.
* With `Anim_Set` (§3.3) the first entry's frames are shown at once; the first tick then
  **re-processes entry 0** (its op, e.g. a movement or a sound, happens at that tick) and shows the
  same frames again. With `Anim_SetIfIdle` or `Anim_Queue` nothing is shown until the next tick,
  which processes entry 0. In both cases entry *k* is processed at tick 1 + k·(D+1) and the -10 at
  tick 1 + E·(D+1) (E = number of entries); this is the "ticks" column of §4.6.
* The -10 tick shows no new frame; the last frames stay visible. A script restarted from the
  state machine in the same tick (common: `Anim_SetIfIdle` after the -10) processes its entry 0 on
  the next tick. Walking therefore takes 8·5 + 1 = **41 ticks per 8 steps** (5.125 ticks = 108.3
  ms per 8-px step), which matches the measured 108 ms (REFERENCE_CAPTURES §5.4) [O+O].

### 3.3 Slot helpers

| Function | Behaviour [O] |
|---|---|
| `Anim_InitSlots` 4306:02c0 | for i in 0..12: slot x = y = z = 0, frame = 0xFFFF, anim = prev = delay = pos = 0, owner = i; record x = y = z = 0, frame = 0xFFFF, owner = i, save pointer = 0; `g_drawOrder[i] = i`. Then DS:9236 = 0 (written nowhere else, read nowhere), `g_spritesChanged` = 0, `g_saveHeapReset` = 1. Slot +0x12 is not touched. |
| `Anim_Set(s, a)` 4306:07fa | if `slot.anim == a`: nothing. Else: if `slot.anim != 0` call `Anim_Stop(s)`; then `Anim_Start(s, a)`. |
| `Anim_Stop(s)` 4306:0844 | if `anim != 0`: `prev = anim; delay = 0; pos = 0` (anim itself is left; the caller overwrites it). |
| `Anim_Start(s, a)` 4306:088e | `anim = a`; `k = 2`, or 4 if `P[2] < -1` (skips a leading two-word op without executing it); for j in 0..n-1: `slots[s+j].frame = P[k+j]`, `.z = slots[s].z`, `.owner = s`. pos and delay are not set (they are 0 from `Anim_Stop`, the -10 op or `Anim_Clear`). |
| `Anim_SetIfIdle(s, a)` 4306:0784 | if `anim == 0`: `anim = a` (no frame change, pos/delay untouched). |
| `Anim_Queue(s, a)` 4306:07b9 | if `prev != a`: `Anim_Stop(s)`, then `anim = a` (no frame change). |
| `Anim_Clear(s)` 4306:092f | `anim = prev = delay = pos = 0; frame = 0xFFFF`. |

Callers outside this module write slot fields directly (x/y/z, and for "static" sprites `frame` +
`owner` + `prev = 0`); `prev = 0` matters for the snapshot rule below.

### 3.4 Sprites_Snapshot (4306:0393), once per frame from Frame_Begin

`Sprites_Snapshot(from, to)`: only if `g_sceneWanted == g_level` (always true from `Frame_Begin`,
which copies the level just before). For each slot i in [from, to) [O]:

* **skip** the slot if `anim == 0 && prev != 0` (an animation that ended or was stopped: its draw
  record is frozen at its last position and frame until the slot gets a new animation or is
  cleared);
* otherwise copy x, y, z, frame into record i, setting `g_spritesChanged = 1` if any differed;
* always copy `owner` into the record.

Quirk [O]: because of the skip rule, moving a stopped slot (writing x/y) does not move its sprite.
Code that drives sprites without scripts sets `prev = 0` (Search_Update, Cell_V49_Showcase).

### 3.5 Sprites_DrawSorted (4306:0497) and Sprites_EraseAll (4306:06dd)

`Sprites_DrawSorted(from, to)` [O]:

```
if g_spritesChanged == 0 and g_dirty == 0: return
clip = full screen (x bytes 0..0x9f, y 0..0xc7) if g_uiMode == 0x80 else play-field (2..0x9d, 0x10..0x8f)
for out in from..to-1:
    best = 0; pick = previous pick (not reset)
    for k in from..to-1:
        if rec[k].z >= best (signed) and not (rec[k].owner & 0x8000): best = rec[k].z; pick = k
    g_drawOrder[out] = pick
    r = rec[pick]; o = r.owner; x = r.x; y = r.y
    if o != pick: x += rec[o].x; y += rec[o].y          (child: offset from the owner's record)
    r.owner = o | 0x8000
    if r.frame == 0xFFFF: continue
    flags = 0x60 if r.frame & 1 else 0x40               (0x20 mirror, 0x40 colour 0 transparent)
    if r.frame < 6: clip = full screen                   (pointer frames; stays for the rest of the call)
    header of sprite r.frame >> 1 in the loaded PAK (1000:2159)
    draw position: x + x_off (or x - (width + x_off) when mirrored), y + y_off   (fmt_graphics §5.3)
    save the background under it (1000:2056, AX = g_saveHeapReset): returns the new heap end or 0
    if 0: r.save = 0, sprite NOT drawn
    else: r.save = old heap pointer; heap pointer = returned value;
          g_saveHeapHigh = max(g_saveHeapHigh, heap offset)    (if the heap segment word is 0)
          Sprite_Draw(flags, x, y); g_dirty += 1; g_spritesChanged = 1; g_saveHeapReset = 0
clip = full screen
```

Ties in z go to the **higher** record index (`>=`). A record with negative z is never picked; the
previous pick is then drawn again (no shipped caller uses negative z) [O].

`Sprites_EraseAll(from, to)`: only if `g_spritesChanged`. For j = (to - from) - 1 down to 0 (index
into `g_drawOrder` from 0, not from `from`): record `g_drawOrder[j]`; if frame != 0xFFFF and save
pointer != 0: `Sprite_Draw(AL = 1, 0, 0, save pointer)` (restores the background). Then
`g_saveHeapReset = 1`, `g_spritesChanged = 0`, bit 15 of every record's owner cleared [O].

Finding [O]: `g_saveHeapHigh` (DS:929f) is the high-water mark of the save heap. Its low byte is
the USERS.DAT header byte 1 (fmt_data §4 "what DS:929f measures"): `Game_Restore`/`Users_Pack`
load and store it [O 47c7:0c62, 47c7:0e0e]. It has no effect on play.

### 3.6 Slot allocation [O from all callers]

| Slot | User |
|---|---|
| 0 | mouse pointer (`Mouse_Poll` 130f:0d94, scripts 1, 7, 8) |
| 1 | the player (`g_playerSlotPtr` DS:0b02 = far pointer to DS:8fec) |
| 2 | player's second sprite: net (2-slot scripts 0x14-0x19), coin and POOF (0x1a/0x1b), the content sprite of a search (§5.1); `g_slot2X` DS:9002 = its x |
| 3 | treasure-room / clubhouse showcase treasure (`Cell_V49_Showcase` 1568:1c7b, display scripts 0x5e-0xbe); end sequence |
| 4 | end sequence (creature eyes, puffs) |
| 6, 8, 10 | elves (one per visible elf, slot from the elf's +0x0e; ARCHITECTURE §5.10); castle creature on slots 6/7 (code_world.md §2.2); end sequence; treasure-room partners (8, 10); champion figures (6, 8) |
| 7, 9, 11 | elf companions (second slot of 2-slot elf scripts) [I]; end sequence |
| 12 | the visible cells' decoration (`Cell_DrawObject` 1568:1e50 and animated decorations such as `Anim_SetIfIdle(12, 0x53)`, code_world.md §2.2, §6.1); `PlrSt_29_PassageUp/30` play their sound only while it has no script |

### 3.7 Porting notes for anim.c

* Implement slots, scripts and the four start helpers exactly; the difference between `Anim_Set`
  (immediate first frame, entry 0 processed again at the first tick) and `Anim_SetIfIdle`/
  `Anim_Queue` (no frame until the next tick) changes timings by one tick and is visible.
* The engine may composite all draw records over a clean background every frame instead of the
  save-under/erase protocol; the visible result is the same as long as the snapshot rule (§3.4),
  the z order with ties, child offsets, mirror placement and the clip rules are kept. The
  save-heap overflow case (sprite skipped) and the full-screen clip after a pointer frame are
  quirks; the latter can let actor sprites drawn after the pointer spill over the HUD [O].
* Read the script table from DS:4f5a at run time (the end sequence patches one word of script
  0x88, §6.3: keep the scripts in writable memory).

---

## 4. player.c (segment 18cf): state machine and movement

### 4.1 Globals

| DS | Name | Use |
|---|---|---|
| 913c | `g_playerState` | index into `g_playerStateHandlers` DS:0b8a (72 far pointers) |
| 0b02 | `g_playerSlotPtr` | far pointer to slot 1 (DS:8fec); handlers read `slot1.anim` (+0x0a), `slot1.prev` (+0x0c) and `slot1.delay` (+0x10) through it |
| 926a | `g_playerMask` | terrain mask of the player's cell, from `Cell_GetMask` in `Player_Move` |
| 9002 | `g_slot2X` | x offset of slot 2 (child of slot 1) |
| 8fa7 / 8fa9 | `g_playerScrX/Y` | player position relative to the camera (bytes / rows), from `Game_Tick` |
| 37ee / 37f0 | `g_camStepX/Y` | scroll request from `Player_Move` |
| 8fa3 | `g_searchTimer` | search countdown (§5.1) |
| 8fc6 | `g_searchCell` | cell being searched |
| 9276 | `g_searchSide` | -1 / 0 / +1: search the left / own / right cell |
| 9128 | `g_searchPicture` | closed picture (low byte of `g_cellPicture`) of the searched cell |
| 926c | `g_menuLock` | 1 while a search or an end scene runs: blocks hints, end messages and menu commands [O 49a6:002f, 49a6:030b, 43cb:0016] |
| 92d0..92da | end sequence counters | §6.3 |

The handler table [O DS:0b8a decoded]: states 0, 3, 4, 5, 9-35, 41-61 and 63-71 have handlers in
18cf; states **1, 2, 6, 7, 8, 36-40 and 62** point to `1568:0007`, a lone `RETF` (do nothing: the
player is frozen while a cell handler or another module drives him). States 1 and 62 are used
(treasure room / rank screen; end of the end sequence); 2, 6-8 and 36-40 are never set [O: all
writes of DS:913c].

State groups that other modules test [O]: 17-20 (0x11-0x14) = jumping (elf dust and gaps ignore
the player, 4521:08a8, 1568:1497); 21-26 (0x15-0x1a) = net; 21 and 24 = net out (elf catch window,
4521:1190); 29 (0x1d) and 30 (0x1e, set by the gap cell 1568:14e5); 31 (0x1f, set by 1568:1a84);
33 (0x21, castle creature 1568:18fe); 41 (0x29, set at the castle top 1568:1bca); 3 (set at
sign-in 47c7:0ad3/0b95); 9 (after teleports, many cell handlers); 34/35 (176a:047a/056d); 1
(176a:1035, 1568:0532).

### 4.2 Player_Update (18cf:181a), per tick

Only if `g_uiMode == 0` and `g_transition == 0` [O]:

1. `g_curObj = &g_player`; `DS:923a = g_keys`; `g_keys = (g_keys | 0xff00) & g_playerMask`.
2. Call `g_playerStateHandlers[g_playerState]`.
3. `Player_Move()`.

### 4.3 Player_Move (18cf:15f0)

Inputs: `SI = g_playerScrX`, `DI = g_playerScrY` (from `Game_Tick` this tick), `g_playerDX/DY`
(accumulated by script ops -2/-3 in `Anim_Tick` and by cell handlers) [O]:

```
g_playerMask = Cell_GetMask()          (handler of the cell at the position BEFORE this move;
                                        may teleport the player and set g_transition)
if g_transition & 1:   camY = player.y & 0xff80;  camX = player.x - 2*SI
elif g_transition & 2: camY = player.y & 0xff80;  DI = player.y - camY
                       SI = (player.x - camX) SAR 1;  g_playerScrX = SI          (camX kept)
else:                  camY = player.y - DI;      camX = player.x - 2*SI
if (unsigned) camX > 0x800: camY -= 0x80; camX += 0x800      (camera wrapped to the previous row)
g_camStepX = dx; g_camStepY = dy
if dx != 0 or dy != 0:
    if (dx < 0 and SI <= 0x20) or (dx > 0 and SI >= 0x7c):   (signed)
        if g_slowScroll (DS:3d31) == 1: dx *= 2; g_camStepX = dx      (camera scrolls, player stays)
    else: SI += dx; g_camStepX = 0                                    (player moves on screen)
    lim = 0x70 if g_playerMask & 0x400 else 0x40
    if (dy < 0 and DI <= lim) or (dy > 0 and DI >= 0x70):
        if g_slowScroll == 1: dy *= 2; g_camStepY = dy
    else: DI += dy; g_camStepY = 0
    player.x += 2*dx; player.y += dy
    if not (g_transition & 2):
        Obj_WrapWorldX()                     (on g_curObj = the player)
        camY = player.y - DI; camX = player.x - 2*SI
    if (unsigned) camX > 0x800: camY -= 0x80; camX += 0x800
g_stepDone |= g_camStepX | g_camStepY        (a scroll counts as a step)
g_playerDX = g_playerDY = 0
slot1.x = SI + 2;  slot1.y = DI + 0x10
r = player.y & 0x7f
depth = 0x32 if r == 0x70 else (0x3d if 0x18 < r < 0x70 else 0x27)     (unsigned compares)
slot1.z = depth + 1
if g_searchTimer == 0: slot2.z = depth      (net/coin drawn in front of the player)
```

So the camera follows horizontally once the player is within 0x20 bytes (64 px) of the left edge
or at 0x7c bytes (248 px) and beyond, which matches the observed player positions (≈65 px and
≈252 px centre, REFERENCE_CAPTURES §5.4) [O+O]; vertically it scrolls when climbing above row 0x40
(0x70 with mask bit 0x400) or below 0x70. On a slow CPU (`g_slowScroll`) each scrolling step is
doubled (the player moves 2·dx and the screen scrolls twice as far) [O].

`Obj_WrapWorldX` (18cf:17c9), also called by `Elves_Update` [O]:

```
if (unsigned) obj.x > 0x7fff: obj.y -= 0x80; obj.x += 0x800; obj[+6] = 0
if (unsigned) obj.x > 0x7ff:  obj.y += 0x80; obj.x -= 0x800; obj[+6] = 0
```

(+6 is dy for the player.) The two tests are sequential, not exclusive.

### 4.4 Mask bits as seen by the player [O uses; meanings I where noted]

| Bit (in the `g_keys` high byte) | Player reaction |
|---|---|
| 0x0100 ladder | Up/Down climb (states 15/16); losing it ends the climb; after falls and ladder jumps: grab the ladder |
| 0x0200 | a coin dropped here pays the net shop instead of searching (states 27/28) |
| 0x0400 | vertical scroll limit 0x70 instead of 0x40 (`Player_Move`) [I: tall scenes] |
| 0x0800 | Up enters an up-passage (state 29, moving up); state 30 slides down while it lasts |
| 0x1000 | no floor: fall (scripts 0x3f/0x40, states 31/32) |
| 0x2000 | on a ladder, Left/Right jumps to the next ladder (state 33) |

### 4.5 States

Notation: `idle` = `slot1.anim == 0`; `Set(a)` = `Anim_Set(1,a)`; `SetIdle(a)` =
`Anim_SetIfIdle(1,a)`; "facing left" = `slot1.prev` is 2 or 4. Conditions are tested in the order
written and the first true branch wins, unless a test is marked *independent*. All tests use the
masked `g_keys` [O, each handler's disassembly].

**State 0** `PlrSt_00_Idle` (18cf:000d; only the BSS start value): if 0x100: state = 15. Then `Set(6)`.
Then, independent: if `slot1.prev == 6`: state = 10.

**State 3** `PlrSt_03_Spawn` (18cf:082a): state = 4. (Set by `Users_InitNewPlayer`/`Users_LoadPlayer`.)

**State 4** `PlrSt_04_Arrive` (18cf:0831), arrival:
* `(keys & 0x3900) == 0` → `Set(6)` (front pose), state 10;
* else 0x100 → if `(x & 0x7f) != 0x40`: `x = (x & 0xff80) + 0x40` and `g_fullRedrawReq += 1`;
  then `Set(0x1e)` (ladder pose), state 15;
* else 0x800 → `Set(0x1c)`, state 29;
* else (0x1000 or 0x2000) → `Set(0x3f)`, state 31.

**State 5** `PlrSt_05_Searching` (18cf:07f6), searching (frozen): if `(keys & 0xf) != 0` or
`g_searchTimer == 1`: if `Search_CanSearch()` returns non-zero, `g_searchTimer = 1`; state = 10.
A direction key therefore ends the wait at once and makes the search jump to its last step when
the place is still "in front"; otherwise the search keeps running while the player walks away.

**State 9** `PlrSt_09_AfterTeleport` (18cf:081b): if `(keys & 0xf) == 0`: state 10 (after a teleport the player
is frozen until all direction keys are released).

**State 10** `PlrSt_10_OnPath` (18cf:08cc), standing:

```
if (keys & 1) and idle:                       (Up acts only when no script runs)
    0x100 → Set(0xc), state 15;  else 0x800 → Set(0x1c), state 29;  else Set(0xb), state 13
elif keys & 2:
    Set(6), state 14
elif (player.y & 0x7f) == 0x70:               (on the path line)
    (keys & 0x24) == 0x24 → Set(0xe), state 17             jump left
    (keys & 0x28) == 0x28 → Set(0xf), state 19             jump right
    (keys & 0x14) == 0x14 → Player_NetLeft
    (keys & 0x18) == 0x18 → Player_NetRight
    keys & 4              → Set(2), state 11               (stand left first, walk next tick)
    keys & 8              → Set(3), state 12
    keys & 0x1000         → Set(0x3f), state 31
    idle:
        facing left:  0x40 → coin drop left;  else 0x10 → Player_NetLeft
        otherwise:    0x40 → coin drop right; else 0x10 → Player_NetRight
elif keys & 0xc:                              (above the line, e.g. in a doorway)
    Set(6), state 14                          (walk down to the line first)
```

Coin drop (0x40) [O]: if `g_coins == 0`: arm hint 7 (`DS:9245 = 0` unless its bit 7 is set) and,
when `g_nets == 0` as well, hint 6 (`DS:9244`); nothing else happens. Otherwise `g_coins -= 1`
immediately; left: `Set(0x1a)`, state 27, `g_slot2X = -25` (0xFFE7); right: `Set(0x1b)`, state
28, `g_slot2X = +25`.

Notes [O]: Left/Right first set the *standing* script (2/3) and state 11/12; state 11/12 starts the
walk script on the next tick (`SetIdle(4/5)` is a no-op until the 8-tick standing script ends, so
the first step comes 9 ticks after the key press). Space alone and the coin key need `idle`, so
they are ignored until the standing/front script has finished (8 or 12 ticks after stopping);
Space with a direction works at once.

**State 11** `PlrSt_11_WalkLeft` (18cf:0b9e), walking left:

```
(keys & 0x2c) == 0x24 → Set(0xe), state 17          (jump left, Right not held)
(keys & 0x2c) == 0x28 → Set(0xf), state 19
(keys & 0x14) == 0x14 → Player_NetLeft
(keys & 0x18) == 0x18 → Player_NetRight
keys & 4              → SetIdle(4)                   (keep walking: restart the 8-step cycle)
keys & 9              → Set(0xb), state 10           (Up or Right)
keys & 2              → Set(6), state 10
keys & 0x1000         → Set(0x3f), state 31
slot1.delay >= 0      → Set(2), state 10             (always true, §3.2)
```

**State 12** `PlrSt_12_WalkRight` (18cf:0ca0), walking right: the same with `keys & 8` → `SetIdle(5)`;
`keys & 1` → `Set(0xb)`, state 10; `keys & 6` → `Set(6)`, state 10; 0x1000 → `Set(0x40)`, state
32; last → `Set(3)`, state 10. Releasing the key stops the player at once (the walk script is
replaced mid-step) [O].

**State 13** `PlrSt_13_LookUp` (18cf:0da2), looking up / walking into the background:
* if idle: `keys & 0xe` → `Set(6)`, state 14; else not Up → `SetIdle(0xb)`, state 10; else
  `SetIdle(9)` (two 8-px steps upward per cycle, back view);
* else (script running): 0x100 → `Set(0xc)`, state 15; else 0x800 → `Set(0x1c)`, state 29.

**State 14** `PlrSt_14_Front` (18cf:0e39), front / walking down:
* Up → `Set(0xb)`, state 13;
* else Down → 0x100: `Set(0xd)`, state 16; otherwise `SetIdle(10)` (two 8-px steps down);
* else if `keys & 0xc` and `(y & 0x7f) < 0x70` → `SetIdle(10)`;
* then, independent: if idle → `Set(6)`, state 10.

**State 15** `PlrSt_15_ClimbUp` (18cf:0ed8), on a ladder going up:

```
if not 0x100: Set(0xb), state 10, return
if idle: Down → SetIdle(0xd), state 16;  else Up → SetIdle(0xc)       (4 steps of 8 px per cycle)
independent: if 0x2000:
    Left → g_slowCpu = 0, Set(0x3d), state 33;  else Right → g_slowCpu = 0, Set(0x3e), state 33
independent: if 0x1000: Set(0x3f), state 31
```

**State 16** `PlrSt_16_ClimbDown` (18cf:0fa9), going down: the same with no ladder → `Set(6)`, state 10;
idle: Up → `SetIdle(0xc)`, state 15, else Down → `SetIdle(0xd)`; the 0x2000 and 0x1000 parts are
identical. Quirk [O]: a ladder jump clears the slow-CPU flag `g_slowCpu` (DS:923c); the castle top
restores it from `DS:8fa5` (1568:1b93).

**States 17-20**, flips [O]:
* 17 `PlrSt_17_JumpLeft` (18cf:107a), jump left: if idle: if `slot1.prev == 0x10` or
  `(keys & 0x24) != 0x24`: Right held → `SetIdle(0x11)`, state 19; else `SetIdle(0x12)`, state 18.
  Else (jump-left still held after the first flip) → `SetIdle(0x10)` (one more flip).
* 18 `PlrSt_18_JumpLeftLand` (18cf:10df), landing left: if idle: `(keys & 0x24) == 0x24` → `SetIdle(0xe)`,
  state 17; else `SetIdle(2)`, state 10.
* 19 `PlrSt_19_JumpRight` (18cf:1120) / 20 `PlrSt_20_JumpRightLand` (18cf:1185): the mirror with 0x28, scripts
  0x11 / 0x13 / 0xf / 3, and Left held → `SetIdle(0x10)`, state 17.

A tap gives flip 0xe (5 moving entries, 40 px, 43 ticks) and landing 0x12 (16 px, 15 ticks);
holding repeats 0xe, 0x10 (32 px), 0x12, 0xe, ... The flip has **no vertical motion**: gaps and
elf dust test the state, not a height [O]. Verified with the per-tick log of answers §7 [O]: a
PgUp tap gives state 19 for 43 ticks, state 20 for 15 ticks, 7 steps of 8 px (56 px), the first
step 8 ticks after the key (entry 0 has no op; `Anim_Set` shows its frame at once and the first
moving entry comes D+1 = 7 ticks after the first tick); PgUp held 2 s gives state 19 for 72 ticks
(0xf + 0x11 = 43 + 29) and a second jump chained from state 20.

**States 21-26**, net [O]:
* `Player_NetLeft` (18cf:0b36): `Set(0x15)` if `g_nets == 0` (torn net), else `Set(0x14)`;
  `g_slot2X = 0`; state 21. `Player_NetRight` (18cf:0b6a): scripts 0x18 / 0x17, state 24.
* 21 / 24 (18cf:11c6 / 1224): if idle: state + 1. The throw lasts 50 ticks; elves are caught only
  in states 21/24.
* 22 / 25 (18cf:11d6 / 1234): if Space is released and idle: `Anim_Queue(1, 0x16 / 0x19)`
  (retract, 29 ticks), state + 1. While Space is held the net stays out on its last frame.
* 23 / 26 (18cf:11fd / 125b): if idle: `Set(2 / 3)`, state 10, `slot2.frame = 0xFFFF`.

**States 27/28**, coin toss finished (18cf:1282 / 12bc) [O]: if idle: `SetIdle(2 / 3)`,
`slot2.frame = 0xFFFF`; then if `keys & 0x200`: state 10 and `g_shopCoinsOwed` (DS:92a1) -= 1;
else `Search_Start()`.

**State 29** `PlrSt_29_PassageUp` (18cf:12f6), carried up a passage: if `g_slowCpu == 0` and
`slot12.anim == 0`: `Sfx_Play(1)` (every tick, so the effect restarts each tick). If 0x800 is gone:
`g_area` (DS:910b) = 0, `Sfx_Stop()`, and Left held → state 17, `Set(0x10)`; else state 19,
`Set(0x11)` (the player comes out flipping). Otherwise `SetIdle(0x1c)`.

**State 30** `PlrSt_30_SlideDown` (18cf:1367), sliding down: same sound rule; 0x800 gone → `g_area = 0`,
`Sfx_Stop()`, `Set(6)`, state 10; otherwise `SetIdle(0x1d)`.

**States 31/32**, falling (18cf:13b9 / 140c): if 0x1000 is gone: without 0x100 → state 17 / 19,
`g_keys &= 0xfff3`, `Set(0x10 / 0x11)` (land with a flip); with 0x100 → `g_keys &= 0xfff3`,
`Set(0x1e)`, state 16 (catch the ladder). Otherwise `SetIdle(0x3f / 0x40)` (8 px per 5 ticks).

**State 33** `PlrSt_33_LadderJump` (18cf:145f), after a ladder jump: if idle: no ladder → state 32,
`g_keys &= 0xfff3`, `SetIdle(0x40)`; ladder → state 15, `g_keys &= 0xfff3`.

**State 34** `PlrSt_34_Chute` (18cf:1495), down the castle chute: `SetIdle(0x5d)` (8 px per 7.25 ticks
downward on average); then, if `slot1.prev != 0x5d`: `Treasure_AddCollected()` (a no-op after its
first effective call, §5.3, so effectively once).

**State 35** `PlrSt_35_TreasureRoomLand` (18cf:14b1), arriving in the treasure room: if 0x1000 is gone: state 1,
`Set(0x52)` (landing); otherwise `SetIdle(0x5d)` (keep falling).

**States 41-71**: the end sequence, §6.3.

### 4.6 Player scripts [O, `animsum.py`; dx in bytes = 2 px]

| Script | D | Entries | Ticks | Movement | Sprites (SPRITES.PAK unless noted) | Use |
|---|---|---|---|---|---|---|
| 2 / 3 | 6 | 1 | 8 | – | 73 mirrored / 73 | stand left / right |
| 4 / 5 | 4 | 8 | 41 | dx ∓4 per entry (64 px) | 74-81 | walk |
| 6 | 10 | 1 | 12 | – | 82 | front pose |
| 9 / 0xa | 8 | 2 | 19 | dy −8 / +8 per entry | 86-87 / 84-85 | walk into / out of the background |
| 0xb | 10 | 1 | 12 | – | 83 | back pose (look up) |
| 0xc / 0xd | 6 | 4 | 29 | dy −8 / +8 | 172-173 | climb up / down |
| 0xe / 0xf | 6 | 6 | 43 | dx ∓4 on entries 1-5 (40 px) | 88-93 | first flip |
| 0x10 / 0x11 | 6 | 4 | 29 | dx ∓4 (32 px) | 90-93 | further flip; landing from a fall or a passage |
| 0x12 / 0x13 | 6 | 2 | 15 | dx ∓4 (16 px) | 90, 88 | flip landing |
| 0x14 / 0x17 | 6 | 7 | 50 | – | 134-140 + net 141-146 (2 slots) | net throw |
| 0x15 / 0x18 | 6 | 7 | 50 | – | as above, torn net 147 last | net throw with 0 nets |
| 0x16 / 0x19 | 6 | 4 | 29 | – | reverse of the throw | net retract |
| 0x1a / 0x1b | 6 | 10 | 71 | – | 167-171, coin 174, POOF 175-177 (2 slots); sfx 5 at entry 3, sfx 0 at entry 4 | coin toss |
| 0x1c / 0x1d | 6 | 3 | 22 | dy −8 / +8 | 211-213 | up-passage / slide |
| 0x1e | 6 | 1 | 8 | – | 173 | ladder pose |
| 0x3d / 0x3e | 4 | 18 | 91 | dx ∓4 on 16 entries (128 px) | 214-216, 172 | ladder jump |
| 0x3f / 0x40 | 3 | 1 | 5 | dy +8 | 211 mirrored / 211 | fall |
| 0x51 | 6 | 1 | 8 | – | SHACK 123 | standing in the treasure room |
| 0x52 | 6 | 6 | 43 | – | SHACK 118-123 | landing in the treasure room |
| 0x5d | 6 | 4 | 29 | dy +8 | CLOSE/SHACK 121-123 | chute fall |

Checks against captures [O+O]: the coin toss in `ega_coin_drop_nothing.avi` runs from AVI frame
44 to 134 = 1.50 s = 71 ticks; climbing 7 ticks per 8 px = 148 ms (measured ≈153 ms); the net
swing ≈2 s = 50 + 29 ticks (REFERENCE_CAPTURES §5.5).

### 4.7 Search start

**`Search_Start`** (18cf:14e0), from states 27/28 [O]:

```
if g_searchTimer >= 1: return            (a search is still running: state stays 27/28,
                                          retried every tick until it ends)
g_playerState = 10
if Search_CanSearch():
    g_playerState = 5; g_searchTimer = 0x39
    g_searchCell = (player.y >> 7)*16 + (player.x >> 7) + g_searchSide      (unsigned shifts)
    g_searchPicture = low byte of g_cellPicture[g_searchCell]
    g_menuLock = 1
```

Without a hiding place in front the coin is simply lost (state 10).

**`Search_CanSearch`** (18cf:1532) [O]:

```
(g_searchSide is not reset here; Search_Update sets it to 0 every frame no search runs)
if slot1.anim == 3 and (player.x & 0x7f) > 0x60:  g_searchSide = +1
if slot1.anim == 2 and (player.x & 0x7f) < 0x20:  g_searchSide = -1
if g_searchSide == 0 and slot1.prev == 3 and (player.x & 0x7f) > 0x60: g_searchSide = +1
if g_searchSide == 0 and slot1.prev == 2 and (player.x & 0x7f) < 0x20: g_searchSide = -1
c = (player.y >> 7)*16 + (player.x >> 7) + g_searchSide
return (unsigned) g_cellPicture[c] <= word DS:122e[8*g_level] + 3      (the whole word)
```

`DS:122e + 8·level` is the picture count of the level (50 / 38 / 31, the first word of the
per-level picture data DS:121c+18, ARCHITECTURE §5.3) [O]. Pictures 1..count are closed hiding
places, count..count+3 their opened variants and 0 an unused slot; all of them are accepted. Edge:
at the first or last cell of the strip `c` can be -1 or 256 (outside the table); no hiding place is
there [O arithmetic; I harmless]. "Facing right" here means script 3 (or `prev` 3), so after a
walk to the right (`prev` = 5) the side is only chosen through the script 3 that states 27/28
start just before calling `Search_Start` [O].

### 4.8 Porting notes for player.c

* One `tick()` per 47.34 Hz tick in this order: script advance (all slots), state handler with the
  masked keys, `Player_Move` (which runs the cell handler). Reproduce the step gating (§2.1), or run
  exactly one frame per tick (§9).
* Keep the destructive key masking and the unmasked copy; the input layer must turn typematic
  repeats into fresh key-down events (capture request 10).
* World x is 0..0x7ff per row with row wrap; sprite x is in bytes; script dx is in bytes and moves
  the player twice as many pixels.
* `Player_Move`'s camera logic depends on `g_transition` set by cell handlers in the same call;
  implement it together with the world module's cell dispatch.

---

## 5. search.c (segment 4a03)

### 5.1 Search_Update (4a03:0003), once per frame

Called from the frame loop (not the tick) [O 130f:0007]. `[BP-2]` (the found-mask bit) starts at
1 on every call.

```
if g_searchTimer < 1 or g_uiMode != 0:  g_searchSide = 0;  return
t = g_searchTimer
if t is even (t % 2 == 0, signed):                         ("open" frame)
    a = attribute byte 0 of picture g_searchPicture in the level's table:
        level 0: DS:0184 + 4*pic,  level 1: DS:0250 + 4*pic,  level 2: DS:02ec + 4*pic
        (pic = g_searchPicture sign-extended; levels 3/4 write nothing)
    g_cellPicture[g_searchCell] = ((a & 0x30) >> 4) + base,
        base = DS:8fb4 (= 0x32, set at start-up 1460:03ee) for level 0, 0x26 for 1, 0x1f for 2
    Anim_Clear(2)
elif t < 0x2a and t != 1:                                  ("held open")
    if g_slowCpu == 1 and t > 0x26: g_searchTimer -= 0x20
    g_searchTimer -= 1
    ShowContent()
else:                                                       ("closed" frame: t >= 0x2a odd, or t == 1)
    g_cellPicture[g_searchCell] = g_searchPicture (sign-extended byte)
    ShowContent()
if g_searchTimer == 1:  Result()
g_searchTimer -= 1
g_fullRedraw (DS:8fbb) += 1;  DS:8fc8 += 1                 (redraw the searched cell's column band)

ShowContent: c = g_cellContent[g_searchCell]
    if c != 0 and (c & 0x80) == 0:
        slot2.y = -24 (0xFFE8); slot2.z = 0x50; slot2.frame = c; slot2.owner = 1; slot2.prev = 0
        (slot2.x is still the coin's ±25; the content value is the frame code: treasure 6+2k =
         sprite 3+k, key 0x66/0x68/0x6a = sprites 51/52/53)
```

`Result()` (only on the frame where the timer is 1) [O]:

```
c = g_cellContent[g_searchCell]
if c < 0x66 (signed) and (c & 0x80) == 0:                  (treasure, not yet taken)
    g_treasuresFound += 1
    if g_treasuresFound != DS:9259 and hint 4 not shown: hint 4 = 0 (armed)
        (DS:9259 = the count the HUD last drew; it lags, so in practice the hint is always armed [I])
    Sfx_Play(7)
    set the lowest clear bit of g_foundMasks[g_level] (record +0x130; bit 0 first)
if c == 0x66: g_keyState = 2;  DS:922e = 1 (HUD redraw);  Sfx_Play(6);  arm hint 9
if c == 0x68: g_keyState = 4;  same
if c == 0x6a: g_keyState = 6;  same
g_cellContent[g_searchCell] |= 0x80                        (searched)
mirror copy also marked: cells 0xe3-0xe5 <-> 0xfd-0xff (±0x1a), 0xab-0xad <-> 0xc0-0xc2 (±0x15),
    0x77-0x79 <-> 0x86-0x88 (±0x0f)                         (ranges are exclusive bounds in the code)
slot2.y = 0;  g_menuLock = 0;  Anim_Clear(2)
```

Notes [O]: a content of exactly 0 would count as a treasure (no 0 check in `Result`), but the layout
never leaves 0 in a hiding place (empty = 0x80) [I]. The level used for the found mask is the
current `g_level`, while the key state follows the content. The searched place looks unchanged
afterwards (the picture is restored), so a second search costs a coin and finds nothing
(ARCHITECTURE §5.4).

**Frame schedule** (timer 0x39 = 57 at the first frame) [O]:

| Frames | Timer values seen | Picture | Content sprite (visible one frame later, at the next snapshot) |
|---|---|---|---|
| 1-16 | 57, 56, ..., 42 | closed on odd t, opened on even t (blinks every frame, 15 visible changes) | set on odd frames, cleared on even ones, so it is visible together with the **opened** picture |
| 17-36 | 41, 39, 37, ..., 3 (2 per frame) | stays opened (the last even frame) | shown every frame |
| 37 | 1 | closed | cleared by `Result` |
| slow CPU | 57..42, 41 → 9, then 7, 5, 3, 1 | as above | 16 + 4 + 1 = **21 frames** |

So the search lasts **37 drawn frames** (21 on a slow CPU), whatever the tick rate. Measured
[O]: `ega_coin_drop_nothing.avi` frames 135-152 show the 15 blink changes and the final close at
frame 178; `ega_coin_drop_key_found.avi` blinks at 135-153, the key sprite shows from 155, the
close and the HUD change come at 184; `ega_r2_treasure_found.avi` (answers §1, treasure 5 in cell
0xe9) blinks at AVI frames 154-171 with the treasure sprite, holds the opened picture with the
sprite at 172-197, closes at 198 (HUD count 0 → 1) and shows hint 4 at 204. The capture agent's
loop-frame log of that search: 38 main-loop frames of 4-5 PIT ticks (17-21 ms) = 693 ms, then
one 97 ms frame (the redraw of the result), during which 4 game ticks were blocked by
`g_stepDone` (answers §1, §9). The player is frozen in state 5 meanwhile unless a direction key
is pressed (§4.5).

`Playfield_Redraw` (130f:0326, main loop) redraws only the band of the searched cell when DS:8fc8 is
set: x bytes from `((cell origin - camera) SAR 1 & ~3) + g_searchSide·64 + 2` to that + 0x44,
clamped to 2..0x9e [O].

### 5.2 Treasure_PickShowcase (4a03:038c)

Called at the castle top after the total is updated (1568:1aa4). Chooses `g_showcasePick`
(DS:9219, a byte) [O]:

```
DI = -2
for SI in 0..47 while DI < 0:
    c = g_cellContent[g_hidingCells[SI]]
    if 0x80 < c < 0xe6 (signed):                    (a treasure found this game: 0x81..0xe5)
        t = c & 0x7f
        if t == (signed char) g_collected[0] or t == g_shownTreasure:   DI = -1    (next SI)
        else: DI = SI; g_showcasePick = low byte of c (bit 7 SET); stop
        (the inner loop over the 48 collected entries exits after its first comparison: only
         g_collected[0] is ever compared — a bug [O])
if DI == -1:                                         (only "known" treasures were found)
    for SI in 0..47:
        c = g_cellContent[g_hidingCells[SI]]
        if 0x80 < c < 0xe6:
            if (c & 0x7f) == g_shownTreasure: break
            DI = SI
    g_showcasePick = g_cellContent[g_hidingCells[DI]] & 0x7f        (bit 7 CLEAR)
(DI == -2: nothing found this game; g_showcasePick unchanged — 0xff after the previous
 hand-over, 0 at start-up)
```

Quirk [O]: in the second loop, if the first found treasure equals the shown one, `DI` is still -1
and the code reads `g_hidingCells[-1]` = the word at DS:11a8 = `g_cellContent[255]`, i.e. it takes
`g_cellContent[g_cellContent[255]] & 0x7f`. `g_shownTreasure` is normally -1 during a game
(the clubhouse door parks it in DS:9254, 1568:04ae), so in practice the comparisons are against
`g_collected[0]` only [O data flow].

### 5.3 Treasure_AddCollected (4a03:078c)

Called by player state 34 every tick on the chute; acts once [O]:

```
p = g_showcasePick
if p == 0 or p == 0xff or p == 0x80: return          (unsigned byte compares)
i = first index with g_collected[i] == 0x80           (no bound check)
if g_shownTreasure > 0 (signed): g_collected[i] = low byte of g_shownTreasure   (old showcase to the shelf)
if p & 0x80:
    p &= 0x7f
    for j in 0..47: if g_collected[j] == p: set bit j of g_shelfBits (record +0x133, bit j%8 of
                                            byte j/8); g_collected[j] = 0x80         (remove duplicates)
g_shownTreasure = p (zero-extended);  DS:9254 = p;  g_showcasePick = 0xff
```

Quirk [O, data flow; I consequence]: `g_shownTreasure` is -1 while the player is out of the
clubhouse (parked in DS:9254 by the door cell 1568:04ae and restored by 1568:03e4 when the player
comes back through cell type 11), so if the player never re-enters the start area during the game,
the previous showcase treasure is **not** moved to a shelf and is replaced. Also, a game saved while
out of the clubhouse stores `g_shownTreasure = -1` (record +0x164); `Users_LoadPlayer` sets DS:9254
from it, so the parked treasure is lost [O 47c7:0b47-0b4f, 47c7:0f67]. Capture request 5/6 can
confirm.

### 5.4 ShelfRoom_DrawShelf (4a03:04a8): the prize-room shelves

Called every frame (130f:0007) and after a HUD full redraw (130f:0909). The four **shelf rooms**
(prize rooms) are reached from the clubhouse door in cell 0x04: rows 3-6, columns 8-11 of the strip,
door/exit cells of types 50-55, 65, 66 (`Cell_P50_ShelfEnter` ... `Cell_P66_Shelf3to2` in
code_world.md §5; the older export names call them "tunnel" cells) [O]. Their cell handlers set
`g_inShelfRoom[n]` (DS:9201+n), `g_shelfCamRoom` (DS:9129 = n) and `DS:8fba = 1`; `Frame_Begin`
latches DS:9201..9204 into DS:8f9f..8fa2; `Scroll_Update` sets DS:8fba = 1 after every
scroll/redraw [O].

```
for room in 0..3:
    if latched in-room flag (DS:8f9f+room) != 1: continue
    if DS:8fba == 0: DS:8fba = 0; continue          (only the first flagged room is drawn per call)
    1000:18af(0)                                     (DS:37ae = 0: dirty tracking off [I])
    for r in 0..1, c in 0..5, k in 0..1:
        i = 6*r + c + (signed char) DS:7d7d[2*room]  (room bases 0, 12, 24, 36)
        if k == 0: draw only if bit (12*room + 6*r + c) of g_shelfBits is set; lift = 2
        else:      draw always; lift = 0
        if not (g_collected[i] > 0 or g_collected[i+1] > 0) (signed chars): skip
        frame = g_collected[i]  if g_collected[i] != 0x80
              = g_shownTreasure if g_collected[i] == 0x80 and k == 0
              = none otherwise
        if frame > 0: Sprite_Draw(flag 0x40, x, y) with
            y = 0x28*r + 0x2d - 2*lift
            x = 0x2c - ((camX - 0x440) >> 1) + 0x14*c + 2*k     if room == g_shelfCamRoom
              = ((0x480 - camX) >> 1) + 0x14*c + 2*k + 0x0c      otherwise      (16-bit unsigned shifts)
    1000:18af(1);  DS:7d7c[2*room] = 0;  DS:8fba = 0
```

So a shelf slot shows its treasure, and a slot whose shelf bit is set (a duplicate was removed
there when that treasure became the showcase, §5.3) additionally shows a copy 4 px higher and 4 px
to the left: the stored treasure, or the showcase treasure when the slot is empty [O; I visual].

### 5.5 Porting notes for search.c

* `Search_Update`, the shelves and the rank screen run **per frame**. The search has a fixed length
  of 37 frames; in DOSBox-X each search frame costs 17-21 ms (full cell redraw), so the search
  takes ≈0.69 s plus a ≈0.1 s result frame (answers §1). Reproduce that duration, not "37 engine
  frames".
* The content sprite is a direct slot write (`prev = 0`), drawn behind everything (z 0x50) at the
  coin's x and 24 rows above the player's feet; it appears one frame after the write.
* Reproduce the `Treasure_PickShowcase` bug (only `g_collected[0]` compared) and the parking of
  `g_shownTreasure`; they decide which treasure the player keeps.

---

## 6. End of a game: castle top, end sequence (player states 41-71)

### 6.1 Flow overview [O]

```
castle wall: player reaches picture 0x32 moving up   (Cell_P63_V63_CastleTop 1568:1aa4, world agent)
  -> scene 3 (CLOSE.PAN/PAK), player object at (0x400,0x500), slot 1 hidden, state 41,
     total += treasures found, Treasure_PickShowcase
end sequence, player states 41..61 (or 63..71 + 61 for a total >= 300)      (§6.3)
Cell_P64_EndScreen (cells 0xa8-0xaa): end sequence done, or any command -> chute       (§6.4)
  -> (0x6a4,0x1f0), state 34: fall down the chute (cells type 23, rows 3-5 column 13);
     Treasure_AddCollected on the way
Cell_P20_CastleGate (cell 0x6d) on the path line: saved game cleared, g_endState = 1    (§6.5)
  total < 300:  treasure room, scene 4 at (0x4f0,0x4e0), state 35 -> land -> display
                -> automatically to the rank screen (Cell_P71_V71_ToRankScreen) (§7)
  total >= 300: clubhouse, scene 0 at (0x170,0x70), celebration anims 0xa5-0xa7;
                when the player's anim ends, Cell_P12_StartRoom sends him to the rank screen (§6.6)
rank screen (scene 4 at (0x680,0x80), RankChest_Update fills the chest; CHAMPION blink at >= 300)
Cell_P72_RankScreen: Enter/Esc/click/Down -> clubhouse (0xf0,0x70), Game_AdvanceEnd: g_endState 1 -> 2
Scene_Update (1437:0034): Game_End, new layout, end-of-game message (ARCHITECTURE §5.13)
```

### 6.2 Castle-top trigger and EndSeq_Setup

Trigger (part of `Cell_P63_V63_CastleTop` 1568:1aa4, world agent; summarised because it starts this
module's sequence) [O]: when the cell's picture is 0x32, `obj.dy < 0` and `(y & 0x78) < 0x71`:
`dy = 0`, `g_keyState = 0`, mask 0, `g_level = 3`, `g_area = 0`, `g_slowCpu = g_slowScroll =
DS:8fa5`, `g_transition = 2`, player = (0x400, 0x500), `camX = 0x400`, `g_fullRedrawReq = 1`,
`g_menuLock = 1`, `g_playerState = 0x29` (41), `Anim_Clear(1)`, `EndSeq_Setup()`, then
`Anim_Set(6,0x78)`, `Anim_Set(7,0x7e)`, `Anim_Set(8,0x7c)`, `Anim_Set(10,0x8e)`,
`Anim_Set(11,0x8f)`, `Anim_Set(4,0x90)`, `*g_totalTreasures += g_treasuresFound` (u16, no
saturation), `Treasure_PickShowcase()`.

**`EndSeq_Setup`** (18cf:0038) [O]: `g_endSeqDone` (DS:92d2) = 0; slots 4, 6, 7, 8, 9, 10: x = 2,
y = 0x10 (the play-field origin, so their CLOSE.PAK frames are placed by the frames' own offsets);
slot 11: x = 0x18, y = 0x2a; depths: slot 6 = 10, slots 7, 8, 10, 11 = 0x0f, slots 3, 4 = 0x0d,
slot 9 = 8; `Sfx_Stop()`, `Music_Stop()`. (Slot 3's position is not set here.)

### 6.3 The end sequence states [O]

Each state waits for one slot's script to end (`slot.anim == 0`), then starts the next scripts and
advances. All `Anim_Set` unless noted. Scripts are in CLOSE.PAK (the scene 3 bank); "sprites"
are PAK frame numbers, descriptions [I] from the previews. Counters: DS:92d0 `g_endCount`, 92d2
`g_endSeqDone`, 92d4 `g_endScanIdx`, 92d6 `g_endAllFound`, 92d8 `g_endPathIdx`, 92da
`g_endTreasureCount`.

| State | Handler | Waits for | Then |
|---|---|---|---|
| 41 | 18cf:00a9 | slot 6 (0x78: player standing, sprite 51, 62 ticks) | slot 6 ← 0x80 (51/52, 23 ticks); state 42 |
| 42 | 18cf:00c4 | slot 6 | `Sfx_Play(8)`; slot 7 ← 0x7f (horn part 63); slot 6 ← 0x79 (raising the horn 53-55, 22 ticks); 43 |
| 43 | 18cf:00f8 | slot 6 | slot 9 ← 0x7a (beam 64-66, 22 ticks); slot 6 ← 0x7b (blowing 54-61, moves slot 6 by +8 bytes once); 44 |
| 44 | 18cf:0122 | slot 9 | DS:63b8 = 1 (effect loop on); `Sfx_Play(2)`; slot 9 ← 0x7d (magic sparkle 72-91, 121 ticks); 45 |
| 45 | 18cf:014d | slot 9 | DS:63b8 = 0; `Sfx_Play(8)`; `Anim_Clear(9)`; slot 8 ← 0x83 (chest opens 68-71, 29 ticks); 46 |
| 46 | 18cf:0182 | slot 8 | slot 6.x = 10; slot 6 ← 0x82 (53, 8 ticks); 47 |
| 47 | 18cf:01a3 | slot 6 | slot 7 ← 0x7e (62); slot 6 ← 0x8b (52, 8 ticks); 48 |
| 48 | 18cf:01cd | slot 6 | slot 6.x = 2; slot 6 ← 0x86 (92, 8 ticks); 49; `g_endScanIdx = 0`; `g_endTreasureCount = 0`; `Layout_UnmarkSharedCells()` (clears bit 7 of the mirror cells 0xfd-0xff, 0xc0-0xc2, 0x86-0x88 so each treasure is counted once); `Music_Play(1)` |
| 49 | 18cf:0205 | slot 6 | if `g_endScanIdx < 48`: c = content of `g_hidingCells[g_endScanIdx++]`; if 0x80 < c < 0xe6 (signed; a found treasure): `g_endPathIdx = 0`, slot 9 = (0x9a, 0x5a), **word DS:48de = c & 0x7f** (the frame of script 0x88, see below), slot 9 ← 0x87 (sparkle 93-97 with four slot moves, 26 ticks), state 50, `g_endTreasureCount++`. (One hiding slot per tick.) Else (all 48 scanned): slot 8 ← 0x8c (chest closes 67-71, 29 ticks), **state 52** |
| 50 | 18cf:0280 | slot 9 | slot 9 = (0x88, 0x40); slot 9 ← 0x88 (the treasure, 3 ticks); 51 |
| 51 | 18cf:02a7 | slot 9 | if `DS:0b06[g_endPathIdx] < 0`: `Anim_Clear(9)`, state 49. Else slot 9.x -= 4, slot 9.y = `DS:0b06[g_endPathIdx++]`, `Anim_SetIfIdle(9, 0x88)` (one step per 3 ticks, 26 steps) |
| 52 | 18cf:02f2 | slot 8 | state 53; i = 0; while `DS:0110[i] != -1` and `DS:0110[i] < total + 1` (unsigned): i += 2. If i < 14: `g_endCount = i/2` (stars of the rank, 0..6), `g_endPathIdx = 0`; else **state 63** (champion) |
| 53 | 18cf:0343 | slot 8 | if `g_endCount == 0`: `g_endCount = -1`, `Anim_Clear(9)`, state 55. Else slot 9 = (`DS:0b5e[g_endPathIdx]`, `DS:0b6a[g_endPathIdx]`), `g_endPathIdx++`, `g_endCount--`, slot 9 ← 0x98 (sparkle 93-96, 21 ticks), state 54 |
| 54 | 18cf:0398 | slot 9 | state 53 |
| 55 | 18cf:03a4 | slot 8 | if `g_endTreasureCount == 0`: slot 4 ← 0x91 (eyes 103/104, 111 ticks), state 57. Else `Anim_Clear(4)`, slot 11.y = 0x1c, and if `g_tier*3 + 6 == g_endTreasureCount` (all found): `g_endAllFound = 1`, slot 11 ← 0x97 (crown drops 22 rows), slot 10 ← 0x92 (creature 99/101); else slot 11 ← 0x94 (crown drops 14), `g_endAllFound = 0`, slot 10 ← 0x8a (99/100); state 56 |
| 56 | 18cf:0441 | slot 10 | if `g_endAllFound == 0`: slot 4 ← 0x93 (eyes 105-107, 64 ticks). Else slot 4 = (0x11, 0x34), slot 4 ← 0x95 (puffs 108-110), slot 3 = (0x1f, 0x34), slot 3 ← 0x96 (mirrored puffs); state 57 |
| 57 | 18cf:049b | slot 4 | `Anim_Clear(4)`, `Anim_Clear(3)`; slot 6 ← 0x84 (player 51, 124-126 with four slot moves, 50 ticks); 58 |
| 58 | 18cf:04ca | slot 6 | if `g_endTreasureCount == 0`: slot 6 = (2, 0x10), slot 6 ← 0x85 (celebration 127-132, 43 ticks), state 61. Else slot 8 ← 0x8d (open chest 69), state 59 |
| 59 | 18cf:050e | slot 7 (long idle: passes at once) | word DS:48de = `g_showcasePick & 0x7f`; slot 9 = (0x20, 0x6e); `g_endPathIdx = 0`; `Anim_SetIfIdle(9, 0x88)`; 60 |
| 60 | 18cf:0546 | slot 9 | if `DS:0b3c[g_endPathIdx] < 0`: slot 6 = (2, 0x10), `Anim_Clear(9)`, slot 6 ← 0x85, state 61. Else if `g_endPathIdx < 8`: slot 9.x += 4; slot 9.y = `DS:0b3c[g_endPathIdx++]`; `Anim_SetIfIdle(9, 0x88)` (16 steps of 3 ticks; x moves only on the first 8) |
| 61 | 18cf:05b2 | slot 6 | `g_endSeqDone = 1`; state 62 (no-op; `Cell_P64_EndScreen` takes over) |
| 63 | 18cf:05c4 | slot 8 | slot 9 = (2, 0x10); slot 9 ← 0x99 (sparkle 76/77, 57 ticks); 64 |
| 64 | 18cf:05eb | slot 9 | DS:63b8 = 1; `Sfx_Play(7)`; slot 8 ← 0x9a (chest opens 69-71, 22 ticks); slot 9 ← 0x9c (sprite 111, a cloud [I]); 65 |
| 65 | 18cf:0625 | slot 8 | DS:63b8 = 0; `Sfx_Stop()`; `Anim_Clear(4)`; slot 3 = (2, 0x10); slot 11.y = 0x1c; slot 3 ← 0x9b (arcs 115-117, 118 ticks); slot 11 ← 0x97; slot 10 ← 0x92; 66 |
| 66 | 18cf:0685 | slot 3 | `Anim_Clear(3)`, `Anim_Clear(11)`; slot 10 ← 0x9d (creature 118, 10 ticks); `Sfx_Play(9)`; 67 |
| 67 | 18cf:06be | slot 10 | slot 11 = (0x18, 0x1b); slot 11 ← 0xa0 (crown 102 flying, 21 slot moves of (+4,+1/+2), 89 ticks); slot 10 ← 0x9e (119/120, 19 ticks); 68 |
| 68 | 18cf:06f4 | – | if slot 10 idle: `Anim_Clear(10)`. If slot 11 idle: slot 6 ← 0x9f (player 112-114, 124, 36 ticks); slot 11.x += 4; slot 11.y += 1; slot 11 ← 0xa1 (crown, 36 ticks); state 69 |
| 69 | 18cf:0738 | slot 6 | `Sfx_Play(6)`; slot 6.x -= 0x37; slot 6.y += 2; slot 6 ← 0xa2 (51 + 124, 15 ticks); state 70; `g_endCount = 5`; `g_endPathIdx = 0` |
| 70 | 18cf:0773 | slot 6 | if `g_endCount == 0`: `g_endCount = -1`, `Anim_Clear(10)`, slot 6 = (2, 0x10), slot 6 ← 0x85, **state 61**. Else slot 10 = (`DS:0b76[i]`, `DS:0b80[i]`), i = `g_endPathIdx++`, slot 10.z = 8, `g_endCount--`, slot 10 ← 0x98, state 71 |
| 71 | 18cf:07ea | slot 10 | state 70 |

What the captures show for these states (answers §2) [O]: treasures flying into the chest and
the chest closing (states 49-52); with all treasures found the creature's reaction (states 55-56,
AVI frames ≈1508-1653 of `ega_r2_end_t6_allfound_nokey.avi`); in the champion branch a chest glow
(≈1651, state 63), the creature losing the crown (≈1800, states 65-67) and the crown on the
pedestal (≈1950-2105, states 67-69) in `ega_r2_crown_nokey.avi`.

The treasure sprite of the flights is script 0x88 (`delay 1, count 1, frame, -10` at DS:48da); the
code overwrites its frame word DS:48de with the treasure code (6+2k = sprite 3+k, the treasure
frames 3..50 are the same in every PAK) before each flight [O 18cf:023e, 18cf:051b]. An engine
that reads the scripts from SST.EXE must keep them writable or special-case script 0x88.

Durations [O from the script lengths, ignoring step gating]: states 41-48 ≈ 62+23+22+22+121+29+8+8
= 295 ticks; then the scan 48 ticks plus ≈107 ticks per found treasure (26 + 3 + 26·3); chest
close 29; 21 per rank star; 15 + 64 (or 111 when nothing was found); 50; showcase flight 8 + 48;
celebration 43. A game with 0 treasures at rank 0 takes ≈584 ticks ≈ 12.3 s before `Cell_P64_EndScreen`
continues by itself.

Check [O+O]: `ega_castle_ladder_jump_top.avi` reaches the castle top at AVI frame 559; the first
changes of the end scene come at frames 654, 668 (14 AVI frames = 11 ticks: the second entry and
the end of script 0x80, D = 10), then 678, 687, 696, 706 (≈7-tick entries of 0x79/0x7b, D = 6), and
frame 766 shows the sparkle of state 44 on the chest with the creature, crown, horn beam and the
"Press Enter" box, as the table predicts. The ladder jump earlier in the same AVI (frames 43-153,
one change every ≈6 AVI frames = 5 ticks) matches script 0x3d/0x3e.

Sound note [O fmt_sound §7; I audible result]: state 44 starts effect 2 with the loop flag; state
45 clears the flag and asks for effect 8, which is refused while effect 2 is still playing its last
pass (8 > 2); state 65 stops effect 7 explicitly.

### 6.4 Cell_P64_EndScreen (176a:0404): leaving the castle top

Player handler of cells 0xa8-0xaa (type 64). Every tick [O]:

```
g_cellMask = 0
if g_endSeqDone != 0 or g_cmd != -1:                 (finished, or any command key/click)
    for s in 3..11: Anim_Clear(s)
    DS:63b8 = 0; Sfx_Stop()
    g_cellMask = 0x14f0
    obj.dy = 0; obj.x = 0x6a4; obj.y = 0x1f0           (top of the chute, row 3 column 13)
    g_transition = 2; camX = 0x600; g_fullRedrawReq = 1
    Anim_SetIfIdle(1, 0x5d); g_playerState = 0x22 (34)
return g_cellMask
```

A key press skips the rest of the sequence at once; the total was already added at the castle top
and the showcase is handed over on the chute, so skipping changes nothing but the pictures [O].

The chute: state 34 lets the player fall with script 0x5d (8 rows per 7.25 ticks) through the
type-23 cells 0x3d, 0x4d, 0x5d (mask 0x1400: fall + camera mode; `Cell_P23_FallShaft` 1568:0d94 teleports
only on level 2; code_world.md §3.3 lists the chute as 5D/6D, the cells that show it) from y 0x1f0
to y 0x370, 48 steps ≈ 348 ticks ≈ 7.4 s [O arithmetic]. Measured: the fall in
`ega_r2_end_t2_nokey.avi` runs from AVI frame ≈1212 to 1657 = 7.4 s (answers §2) [O].

Captures of the whole end (answers §2-§4) [O]: `ega_r2_end_t2_nokey.avi` (2 treasures: flights,
chest closes, chute, dissolve into the treasure room at 1663, rank chest fill from 1782),
`ega_r2_end_t2_enter_skip.avi` (Enter at ≈700 skips straight to the chute), 
`ega_r2_end_t6_allfound_nokey.avi` (all 6 found: the creature reaction of states 55-56),
`ega_r2_crown_nokey.avi` and `ega_r2_crown_t2_total300_nokey.avi` (total reaching 300 with 6 and
with 2 treasures: both take the champion branch, which confirms that state 52 tests the total, not
"all found").

### 6.5 Cell_P20_CastleGate (176a:0485): the castle gate

Cell 0x6d (type 20), bottom of the chute [O]:

```
if (obj.y & 0x7f) == 0x70:
    obj.dy = 0; user record byte +9 = 0 (no saved game); g_endState = 1; g_menuLock = 1
    if *g_totalTreasures < 300 (unsigned):
        obj = (0x4f0, 0x4e0); g_level = 4; g_playerState = 0x23 (35)
        g_fullRedrawReq = 1; g_transition = 1; Anim_InitSlots()
    else:                                                                (champion)
        obj = (0x170, 0x70); g_level = 0; g_fullRedrawReq = 1; g_transition = 1; Anim_InitSlots()
        g_playerState = 9; DS:925b = 1 (HUD full redraw request [I])
        Anim_SetIfIdle(1, 0xa5)
        slot 6 = (0x52, 0x84), z 10; Anim_SetIfIdle(6, 0xa6)
        slot 8 = (0x62, 0x86), z 10; Anim_SetIfIdle(8, 0xa7)
    g_cellMask = 0
else: g_cellMask = 0x14f0
```

### 6.6 The champion celebration in the clubhouse [O; visuals I]

With a total of 300 or more the player lands in the clubhouse start cell (cell 2, type 12, SPRITES
bank). Scripts: 0xa5 on slot 1 (player: walk frame 74, slides 211-213, flips 90-93; 20 entries,
121 ticks), 0xa6 / 0xa7 on slots 6 / 8 (two elf figures, sprites 97 and 112-114, the second
mirrored; 121 ticks), all absolute positions. The start-cell handler `Cell_P12_StartRoom` (1568:03f6, world
agent) watches for `slot1.prev == 0xa5` (the player's script ended): then `g_level = 4`,
`g_menuLock = 1`, player = (0x4f0, 0x4e0), `g_fullRedrawReq = 1`, `g_transition = 1`,
`Anim_Clear(6)`, `Anim_Clear(8)`, `Cell_P71_V71_ToRankScreen()` (→ rank screen, §7.3), mask 0 [O 1568:0551-05a3].
The rank screen then blinks CHAMPION (§7.4). Captured in `ega_r2_crown_nokey.avi` (the scene with
the two figures from AVI frame ≈2703, the rank chest from ≈2853; answers §2, §4) [O]; the capture
agent describes the room as the treasure room, the code places it in the clubhouse start cell
(scene 0, `g_level = 0`) [O code].

---

## 7. Treasure room, rank screen and the return to the clubhouse (176a, 130f)

### 7.1 Places [O cell table DS:0eaa]

| Cells (type) | Scene | Use |
|---|---|---|
| 0x01 (71) `Cell_P71_V71_ToRankScreen` | 0 / 4 | clubhouse cell with the chest picture: Up leads to the rank screen |
| 0x02 (12) `Cell_P12_StartRoom` | 0 | clubhouse start cell; in its right part (x & 0x78 >= 0x5a) walking up to y & 0x7f == 0x60 leads into the treasure room at (0x4f0, 0x4e0): `g_level = 4`, `g_menuLock = 1`, state 1, `g_fullRedrawReq = 1`, `Anim_Set(1, 0x51)`, `DS:9282 += 1` (no auto-advance) [O 1568:04e1-054d] |
| 0x03, 0x9a (49) `Cell_V49_Showcase` | 0 / 4 | showcase display (visible-cell handler, below) |
| 0x98, 0x99, 0x9b (70) `Cell_P70_TreasureRoom` | 4 | treasure room floor |
| 0x1d (72) `Cell_P72_RankScreen` | 4 | rank chest screen |

`Cell_V49_Showcase` (1568:1c7b, world agent; the drawing side of the treasure room) [O]: every tick for a
visible type-49 cell (`DS:8fc0`/`8fc2` = the cell's screen x bytes / y): slot 3 = (cellX + `g_shackX`
DS:9213, cellY + `g_shackY` DS:9211), z 0x50, owner 3; `g_shelfCamRoom = 0`; if `slot3.anim < 0x5e`
and not (`g_shackFlags` & 0x20): `slot3.frame = g_shownTreasure`, `slot3.prev = 0`. If
`g_shackFlags & 0x40` and partner 1 (DS:00f8, signed word) > 0: slot 8 = (cellX + DS:00fc,
cellY + 0x44), z 0x46, owner 8, frame = DS:00f8, prev 0. If `g_shackFlags & 0x40`, partner 2
(DS:00fa, signed byte) > 0 and DS:00fc < 0x36: slot 10 = (cellX + DS:00fe, cellY + 0x44), z 0x3c,
owner 10, frame = DS:00fa, prev 0.

Treasure-room variables (initial values from the data image or `Game_InitVars` 1460:03ee+) [O]:

| DS | Name | Init | Meaning |
|---|---|---|---|
| 8fb0 | `g_shownTreasure` | 0xffff | treasure (frame code) on the pedestal; also the special display codes 0x6a, 0x84, 0x86, 0x8a, 0xcc |
| 9213 | `g_shackX` | 0x1a | pedestal treasure x offset (bytes) |
| 9211 | `g_shackY` | 0x44 | pedestal treasure y offset |
| 00f6 | `g_shackSavedShown` | 0xffff | shown treasure saved during a "fly" display |
| 00f8 | `g_shackPartner1` | 0xffff | partner treasure in slot 8 |
| 00fa | `g_shackPartner2` | 0xff | partner treasure in slot 10 (byte) |
| 00fb | `g_shackState` | 0 | bit 7: display started (sfx 2 given); low 7 bits: carousel running |
| 00fc / 00fe | `g_shackPartner1X/2X` | 0x50 / 0x50 | partner x offsets |
| 9251 | `g_shackFlags` | 0 | 0x01 carousel move, 0x80 carousel right / fly started, 0x02 swap done, 0x10 fly second half, 0x20 display script owns slot 3, 0x40 pair/fly running (also set by `Treasure_IsCollected`) |
| 9282 | `g_shackStay` | 0 | non-zero: no automatic move to the rank screen |
| 8faf | `g_shackNoCase` | 0 | set when the shown treasure matched no case; **never cleared** |

### 7.2 Cell_P70_TreasureRoom (176a:0596): the treasure room, every tick [O]

```
g_cellMask = 0x104f
if (obj.y & 0x7f) == 0x70: g_cellMask = 0; if obj.dy > 0: obj.dy = 0
if g_endState == 1: g_keys = 0                                    (no keys during the end visit)
if g_keys & 1: g_shackStay = 1
-- display trigger --
if ((slot1.prev == 0x52 and slot3.prev < 0x5e) or (g_keys & 1) or (g_shackFlags & 0x40))
   and (g_shackState & 0x7f) == 0:
    if sfx block status (DS:6448) == 0 and not (g_shackState & 0x80): Sfx_Play(2)
    g_shackState |= 0x80; slot1.prev = 0; g_keys &= ~1
    look g_shownTreasure up in the 53-value case table (CS 176a:0654, targets 176a:06be);
    found: run the case (table below); not found: g_shackNoCase = 1
-- automatic exit to the rank screen --
if g_shackStay == 0 and (slot3.prev >= 0x5e (signed) or g_shackNoCase == 1):
    Cell_P71_V71_ToRankScreen(); g_shackState &= 0x7f
-- leaving --
if ((g_keys & 0x42) or g_cmd != -1) and g_endState == 0:
    g_cellMask = 0; Shack_ResetDisplay(<no argument pushed>); TreasureRoom_Leave()
-- carousel start --
if (g_keys & 8 or g_keys & 4) and obj.y == 0x4e0 and (g_shackFlags & 0xbf) == 0
   and ((signed char) g_collected[0] > 0 or g_collected[1] > 0) and g_shackState == 0
   and slot3.anim < 0x5e:
    g_shackState += 1; g_shackFlags |= 1; if Right: g_shackFlags |= 0x80
-- carousel motion (unsigned compares on g_shackX) --
if g_shackX < 0x50 and (g_shackFlags & 0xf) == 1:
    g_shackX += 2; partner1X += 2; partner2X += 2                  (slide out to the right)
elif g_shackX == 0x50 and (g_shackFlags & 0xf) == 1:               (swap)
    gap = first i with g_collected[i] == 0x80
    if g_shackFlags & 0x80: next = 0 if (g_collected[gap+1] == 0x80 or gap == 0x2f) else gap+1
    else:                   next = (last i, scanning down from 0x2f, with g_collected[i] != 0x80)
                                   if gap == 0 else gap-1
    partner1 = -1; partner2 = 0xff; partner1X = partner2X = 0x50
    old = g_shownTreasure; g_shownTreasure = (signed char) g_collected[next]
    g_collected[next] = 0x80; g_collected[gap] = low byte of old
    g_shackFlags = 2; Sfx_Play(7)
elif g_shackX > 0x1a and g_shackFlags == 2: g_shackX -= 2          (slide back in)
elif g_shackX == 0x1a and g_shownTreasure < 0x80 and g_shackY > 0x43:
    g_shackState = 0; g_shackFlags &= 0x40                         (carousel finished)
return g_cellMask
```

Notes [O]: the collected list works as a ring with one gap (the first 0x80 entry): Right brings the
entry after the gap onto the pedestal and puts the old pedestal treasure into the gap. The missing
argument of `Shack_ResetDisplay` on leaving reads the caller's saved DI; any non-zero value only
sets `slot3.anim`, which `TreasureRoom_Leave` clears at once (`Anim_InitSlots`) [O; I harmless]. Because
`g_shackNoCase` is never cleared, once a shown value without a case has been displayed (for example
0xffff of a new player), every later end-of-game visit (where `g_shackStay` is 0) jumps to the rank
screen on its first tick without the display [O data flow]. `Cell_P70_TreasureRoom` assumes `g_curObj` is the
player [I].

**The case table** (value = `g_shownTreasure`; "→ s" = `Anim_SetIfIdle(3, s)`, the display script
of that treasure on slot 3, all in SHACK.PAK, 60-120 ticks; `IC(x)` = `Treasure_IsCollected(x)`,
which also sets `g_shackFlags = 0x40` when true; `R(s)` = `Shack_ResetDisplay(s)`; X, P1X, P2X =
`g_shackX`, partner1X, partner2X; all position compares unsigned) [O 176a:0728-0d95]:

| Value | Action |
|---|---|
| 0x0a, 0x0c, 0x10, 0x12, 0x14, 0x16 | → 0xac, 0xa9, 0xaf, 0xb2, 0xb0, 0xb1 |
| 0x1a, 0x20, 0x24, 0x26, 0x28, 0x2a | → 0x6a, 0xa8, 0xa3, 0xab, 0xaa, 0xae |
| 0x2e, 0x30, 0x32, 0x34 | → 0x76, 0xad, 0x5e, 0x5f |
| 0x38, 0x3a, 0x3c, 0x3e, 0x40, 0x42, 0x44, 0x46, 0x48 | → 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69 |
| 0x52, 0x54, 0x56 | → 0x6b, 0x6c, 0x6d |
| 0x5c, 0x5e, 0x60, 0x62, 0x64 | → 0x70, 0x71, 0x72, 0x73, 0x74 |
| 0x06 | IC(0x5a) false → 0xb4; else P1X > 0x1a → `ShackPair_06_5a`; else R(0x6f) |
| 0x5a | IC(0x06) false → 0xbe; else P1X > 0x1a → `ShackPair_06_5a`; else R(0x6f) |
| 0x08, 0x8a | IC(0x36) false → 0x60; else if (X > 0x12 or P1X > 0x22) and partner1 != 0x8a → `ShackPair_08_36_Approach(0)`; elif P1X < 0x50 → `ShackPair_08_36_Swap(0)`; else R(0), `g_shownTreasure = 0x36`, `slot3.prev = 0x5e` |
| 0x36, 0x6a | IC(0x08) false → 0xb5; else if (X < 0x22 or P1X > 0x12) and `g_shownTreasure == 8` → `ShackPair_08_36_Approach(1)` (cannot happen in this case); elif X < 0x50 → `ShackPair_08_36_Swap(1)`; else R(0), `g_shownTreasure = 8`, `slot3.prev = 0x5e` |
| 0x0e | IC(0x2c) false → 0xb6; else (X < 0x20 or P1X > 0x12) → `ShackPair_0e_2c(1)`; else R(0xb3) |
| 0x2c | IC(0x0e) false → 0xba; else (X > 0x12 or P1X > 0x20) → `ShackPair_0e_2c(0)`; else R(0xb3) |
| 0x18 | IC(0x58) false → 0xb7; else (X > 0x12 or P1X > 0x20) → `ShackPair_18_58(0)`; else R(0x75) |
| 0x58 | IC(0x18) false → 0x6e; else (X < 0x20 or P1X > 0x12) → `ShackPair_18_58(1)`; else R(0x75) |
| 0x1c | IC(0x22) false → 0xb8; else (X > 0x13 or P1X > 0x22) → `ShackPair_1c_22(0)`; else R(0xa4) |
| 0x22 | IC(0x1c) false → 0xb9; else (X < 0x22 or P1X > 0x13) → `ShackPair_1c_22(1)`; else R(0xa4) |
| 0x4a | IC(0x4c) and IC(0x4e) false → 0xbb and `g_shackFlags &= 0xbf`; else (X > 0xc or P1X > 0x1a or P2X > 0x26) → `ShackTrio_4a_4c_4e(0)`; else R(0x77) |
| 0x4c | IC(0x4a) and IC(0x4e) false → 0xbc, `&= 0xbf`; else (X < 0x1a or P1X > 0xc or P2X > 0x26) → `ShackTrio_4a_4c_4e(1)`; else R(0x77) |
| 0x4e | IC(0x4a) and IC(0x4c) false → 0xbd, `&= 0xbf`; else (X < 0x26 or P1X > 0xc or P2X > 0x1a) → `ShackTrio_4a_4c_4e(2)`; else R(0x77) |
| 0x1e, 0xcc | `ShackFly_1e()` |
| 0x50, 0x84, 0x86 | `ShackFly_50()` |

(The 48 treasure codes 6..0x64 plus 0x6a, 0x84, 0x86, 0x8a, 0xcc = 53 values. The IC tests use
`&&` short-circuit: for 0x4a the second test runs only if the first was true.)

Because `Treasure_IsCollected` sets `g_shackFlags = 0x40`, a pair case re-runs every tick (trigger
condition `g_shackFlags & 0x40`) and moves the partner one byte per tick until the positions meet;
then `Shack_ResetDisplay(s)` clears 0x40, sets 0x20 and starts the combined display script on slot 3.

### 7.3 Treasure-room helpers [O]

**`Shack_ResetDisplay(a)`** (176a:1531): if `g_shackSavedShown > 0` (signed): `g_shownTreasure =
g_shackSavedShown`, `g_shackSavedShown = -1`. Then partner1 = -1, partner2 = 0xff, partner1X =
partner2X = 0x50, `g_shackX = 0x1a`, `g_shackY = 0x44`, `g_shackFlags = (g_shackFlags & 0xbf) |
0x20`, `Anim_Clear(8)`, `Anim_Clear(10)`, and if `a != 0`: `Anim_SetIfIdle(3, a)`.

**Pair steppers** (each first sets the partner if partner1 < 0 (signed): partner1 = the other
treasure of the pair, chosen by `g_shownTreasure`):

| Function | Partner choice | dir = 0 | dir = 1 |
|---|---|---|---|
| `ShackPair_08_36_Approach` 176a:1162 (0x08/0x36) | shown == 8 ? 0x36 : 8 | P1X > 0x22 → P1X--; X > 0x12 → X-- | P1X > 0x12 → P1X--; X < 0x22 → X++ |
| `ShackPair_0e_2c` 176a:1220 (0x0e/0x2c) | shown == 0x0e ? 0x2c : 0x0e | P1X > 0x20 → P1X--; X > 0x12 → X-- | P1X > 0x12 → P1X--; X < 0x20 → X++ |
| `ShackPair_06_5a` 176a:1275 (0x06/0x5a) | shown == 6 ? 0x5a : 6 | P1X > 0x1a → P1X-- (argument ignored) | same |
| `ShackPair_1c_22` 176a:129d (0x1c/0x22) | shown == 0x1c ? 0x22 : 0x1c | P1X > 0x22 → P1X--; X > 0x13 → X-- | P1X > 0x13 → P1X--; X < 0x22 → X++ |
| `ShackPair_18_58` 176a:12f2 (0x18/0x58) | shown == 0x58 ? 0x18 : 0x58 | P1X > 0x20 → P1X--; X > 0x12 → X-- | P1X > 0x12 → P1X--; X < 0x20 → X++ |

**`ShackPair_08_36_Swap(dir)`** (176a:11b7): dir 0: if partner1 == 8 → partner1 = 0x8a; if P1X < 0x50:
P1X++ and `g_shownTreasure` = 0x36 if P1X is even else 0x6a. dir 1: if shown == 8 → shown = 0x8a;
if X < 0x50: X++ and partner1 = 0x36 if X is even else 0x6a.

**`ShackTrio_4a_4c_4e(which)`** (176a:1463, trio 0x4a/0x4c/0x4e): if partner1 < 0: shown 0x4a →
(0x4c, 0x4e); 0x4c → (0x4a, 0x4e); else (0x4a, 0x4c) as (partner1, partner2). which 0: X > 0xc →
X--; P1X > 0x1a → P1X--; if P2X > 0x26 and P1X < 0x36: P2X--. which 1: X < 0x1a → X++; P1X > 0xc →
P1X--; if P2X > 0x26 and P1X < 0x36: P2X--. which 2: X < 0x26 → X++; P1X > 0xc → P1X--; if P2X >
0x1a and P1X < 0x36: P2X--. (Other values: nothing.)

**`ShackFly_1e`** (176a:1347, treasure 0x1e) and **`ShackFly_50`** (176a:13d7, 0x50), "fly"
displays: run only if `not (g_shackFlags & 0x80)` or `g_shackFlags & 0x40` (G also needs
`not (g_shackFlags & 1)`):

```
if not (g_shackFlags & 0x80):           (start)
    g_shackSavedShown = g_shownTreasure; g_shackFlags |= 0xc0; g_shackY = 0x43
    (H only: g_shownTreasure = 0x84)
elif not (g_shackFlags & 0x10) and g_shackY > 0 (signed): g_shackY -= 2          (rise)
elif g_shackY <= 0: g_shownTreasure = 0xcc (G) / 0x86 (H); g_shackFlags |= 0x10; g_shackY = 2
elif g_shackY == 0x44:                  (back down)
    g_shackFlags &= 0x3f; g_shownTreasure = 0x1e (G) / 0x50 (H); g_shackSavedShown = -1
    slot3.prev = 0x5e
else: g_shackY += 1                     (descend)
```

**`Treasure_IsCollected(id)`** (176a:1136): for i in 0..47: if `(signed char) g_collected[i] ==
id`: `g_shackFlags = 0x40` (assignment), return 1. Return 0.

**`TreasureRoom_Leave`** (176a:0f47): player = (0x170, 0x70), dy = 0, `g_level = 0`, `g_menuLock = 0`,
`g_playerState = 9`, `Anim_InitSlots()`, `Anim_Set(1, 6)`, `g_fullRedrawReq = 1`,
`g_transition = 1`, `g_shackStay = 0`, `g_shackFlags = 0`, `Game_AdvanceEnd()`.

**`Game_AdvanceEnd`** (176a:1119): if `g_endState == 1`: `g_endState = 2`; else `Anim_Set(1, 6)`.

**`Cell_P71_V71_ToRankScreen`** (176a:0fa6) [O]: as a visible-cell handler (`g_cellCallKind != 2`) it calls
`Cell_V7` (a decoration). As the player's cell (`g_cellCallKind == 2`):

```
g_cellMask = 0xdf
if (obj.x & 0x78) < 0x44: if obj.dx < 0: obj.dx = 0;  g_cellMask = 0xdb       (left wall)
if (obj.y & 0x7f) < 0x6c:
    g_cellMask &= 0xf2
    if (obj.y & 0x7f) == 0x60:                                                 (enter the rank screen)
        obj = (0x680, 0x80); camX = 0x680; g_level = 4; g_rankState (DS:925d) = 1; g_menuLock = 1
        g_playerState = 1; Anim_Clear(1); Anim_Clear(3); g_fullRedrawReq = 1; g_transition = 2
if (obj.y & 0x78) > 0x6f: if obj.dy > 0: obj.dy = 0;  g_cellMask &= 0xfd
return g_cellMask
```

`Cell_P70_TreasureRoom` and `Cell_P12_StartRoom` call it directly with the player at (0x4f0, 0x4e0) (`y & 0x7f` = 0x60),
which moves him straight to the rank screen [O].

**`Cell_P72_RankScreen`** (176a:1094), the rank screen cell [O]: `g_cellMask = 0x104f`; if the mouse button
state changed (`DS:90f4 != DS:9143`, set by `Mouse_Poll`), or `g_cmd` is 8 or 0 (Esc is command
0, ARCHITECTURE §7.2; 8 is [I] Enter), or
`g_keys & 0x42`: `g_rankState = 0`, player = (0xf0, 0x70), `camX = 0xc0`, `g_level = 0`,
`g_menuLock = 0`, `g_playerState = 9`, `g_fullRedrawReq = 1`, `g_transition = 2`, `g_cellMask = 3`,
`Anim_Set(1, 6)`, `Game_AdvanceEnd()`, `g_champFlashTimer = 0`.

### 7.4 The rank chest screen: RankChest_Update (130f:0edf) and RankChest_DrawStar (130f:1261)

Called every frame from `main` [O 130f:0092]. DS:8fbd is `g_rankState` (DS:925d) latched by
`Frame_Begin`. The chest picture is SHACK.PAN block of cell 0x1d.

```
if latched == 1:                                     (first frame)
    text colour DS:9215 = 0xffff; Text_Draw(0x44, 0x1d, player name)       (43cb:0b63)
    Hud_DrawNumber(0x6c, 0x77, colour 0xffff, *g_totalTreasures)          (130f:0d00)
    i = 0; while DS:0110[i] != -1 and DS:0110[i] < total + 1 (unsigned): i += 2
    g_rankRowsWanted (DS:92a8) = DS:0110[i + 3]     (the "fill" word of the NEXT pair)
    g_rankTickPhase (DS:92b0) = byte DS:9100 & 0xfc;  g_rankRowsDone (DS:92a6) = 0
    g_rankState += 1                                (-> 2)
elif latched != 0 and (byte DS:9100 & 0xfc) != g_rankTickPhase:      (one step)
    g_rankTickPhase = byte DS:9100 & 0xfc
    y = 0x6f - g_rankRowsDone
    Gfx_ReplaceColorRect(AX 0x7777, BX 0x6666, x bytes 0x1a..0x98, rows y..y+1)    (1000:176c: every pixel
                                       of colour 7 in the rectangle becomes colour 6)
    star rows: when g_rankRowsDone == DS:0116 (8): 1 star at x 0x51, y 0x67
               == DS:011a (16): 2 at 0x4e, 0x54, y 0x5f
               == DS:011e (24): 3 at 0x4b, 0x51, 0x57, y 0x57
               == DS:0122 (32): 4 at 0x48 + 6k, y 0x4f
               == DS:0126 (40): 5 at 0x45 + 6k, y 0x47
               == DS:012a (48): 6 at 0x42 + 6k, y 0x3f
       each by RankChest_DrawStar(x, y, 0xfa) = Sprite_Draw(flag 0x40) of frame 0xfa (SHACK sprite
       125, a small star) at (x, y)
    Sfx_Play(3); g_dirty += 1; g_rankRowsDone += 1
    if g_rankRowsDone == g_rankRowsWanted:
        g_rankState = 0; latched = 0
        if g_rankRowsDone - 1 >= DS:012e (56, unsigned): g_champFlashTimer (DS:0100) = 800
if g_champFlashTimer != 0:                           (runs every frame, any scene)
    if g_champFlashTimer % 160 == 0:  colour 0x6666; Text_Draw(0x40, 0x2e, "CHAMPION" DS:013a)
    elif g_champFlashTimer % 160 == 80: colour 0xffff; Text_Draw(0x40, 0x2e, DS:013a)
    g_champFlashTimer -= 1 (or -= 4 when g_slowCpu); g_dirty += 1      (unsigned % and compare)
```

Rows filled per total [O table DS:0110]: < 5 → 8, 5-24 → 16, 25-69 → 24, 70-114 → 32, 115-169 → 40,
170-229 → 48, 230-299 → 56, 300-399 → 66 (+ CHAMPION), ≥ 400 → 86 (+ CHAMPION). One step happens
whenever the byte counter DS:9100 enters a new group of 4: in each 60-tick cycle the groups are 60
(1 tick), 59-56, ..., 7-4 (4 ticks each), 3-1 (3 ticks), so 16 steps per 60 ticks (≈12.6 rows/s),
tick-driven but polled per frame [O 130f:019a-01c1]; measured one row per ≈4 ticks, and 8 rows for
a Trainee (answers §3) [O]. The CHAMPION text is drawn in colour 6 (the colour of the band, so it
is invisible there) at 800, 640, 480, 320, 160 and in black (visible) at 720, 560, 400, 240, 80: it
appears 5 times for 80 frames each and **ends visible** (answers §4: visible at AVI frames 3042,
3053, 3063, 3074, 3084) [O]. It counts **main-loop frames**: on that screen the loop runs at ≈914
frames/s in DOSBox-X, so the 800 frames last ≈0.88 s (640 frames took 0.70 s) [O answers §4]; on a
slow CPU the timer drops by 4 per frame.

Observation (answers §2) [O]: after a game that brought the total to 6, the rank chest showed one
star row (16 rows filled) while a rank label in the band still read "Trainee". `RankChest_Update` draws
only the name and the total; the label is drawn elsewhere (UI agent, open point §11).

---

## 8. What runs per tick and what per frame

| Per tick (47.34 Hz, inside `Game_Tick`) | Per frame (free-running loop) |
|---|---|
| `Anim_Tick` (all script timing, script movement, sfx ops, the -6 strike counter) | `Sprites_Snapshot` (in `Frame_Begin`), `Sprites_DrawSorted`, `Sprites_EraseAll` |
| `Player_Update`: state handlers 0-71, `Player_Move`, the cell handlers called from it (`Cell_P64_EndScreen`, `Cell_P20_CastleGate`, `Cell_P70_TreasureRoom`, `Cell_P71_V71_ToRankScreen`, `Cell_P72_RankScreen`, the treasure-room steppers) | `Search_Update` (37 frames per search) |
| treasure-room carousel and pair motion (one step per tick) | `ShelfRoom_DrawShelf` (prize-room shelves) |
| DS:9100 counter that times the rank-chest fill | `RankChest_Update` (fill steps polled per frame, CHAMPION blink counted in frames) |

Step gating (§2.1): when a tick finds `g_stepDone` set (no frame drawn since the last step), only
`Anim_Tick` (holding every due advance) and `Input_Tick` run.

Main-loop rates measured in DOSBox-X at `cycles=fixed 6000` (answers §9) [O]:

| Situation | Loop frames per second | Frame cost |
|---|---|---|
| nothing drawn (hint bubble up) | ≈1690 | the 1000-iteration delay loop |
| walking with scrolling | ≈820-880 | most frames 0-1 PIT ticks, scroll frames 25-34 ms |
| rank screen during the CHAMPION blink | ≈914 | |
| HUD coin count-up | ≈73 | 13.7 ms |
| search blink | ≈55 | 17-21 ms (cell band redraw every frame) |

So the frame loop normally runs far faster than the 47.34 Hz tick; gating only happens behind slow
frames (4 ticks after a search's result frame, about 1 tick in 5 s of walking). The walk timing
measured from the AVIs (108 ms per step) equals the ungated value [O+O].

---

## 9. Sounds started by these modules

| Id | Where | When |
|---|---|---|
| 0 | coin script 0x1a/0x1b, entry 4 | the POOF cloud |
| 1 | `PlrSt_29_PassageUp`, `PlrSt_30_SlideDown` | every tick while carried up / sliding (not on a slow CPU) |
| 2 | `PlrSt_44_EndSparkle` (looping), `Cell_P70_TreasureRoom` trigger (if no effect is playing) | magic sparkle; treasure-room display start |
| 3 | `RankChest_Update` | each chest-fill step |
| 5 | coin script entry 3 | the throw |
| 6 | `Search_Update` (key found), `PlrSt_69_ChampCrowned` | key; champion crowned |
| 7 | `Search_Update` (treasure found), `PlrSt_64_ChampChest` (looping), carousel swap | treasure; champion chest; carousel |
| 8 | `PlrSt_42_EndHorn`, `PlrSt_45_EndChestOpen` | horn blow; chest opens (likely masked by effect 2, §6.3) |
| 9 | `PlrSt_66_ChampScream` | creature screams |
| music 1 | `PlrSt_48_EndFlightInit` | treasures fly into the chest (music was stopped by `EndSeq_Setup`) |

Elf scripts and other modules start their own effects (fmt_sound §7).

---

## 10. Data tables used (read from SST.EXE at run time)

| DS | Layout | Example values [O] |
|---|---|---|
| 0b8a | 72 far code pointers (offset, segment relative to load 0) — player state handlers | [10] = 18cf:08cc |
| 4f5a | 194 far pointers (seg 0x3d94) to animation scripts; ids 0..0xbe valid | [4] = 3d94:3da0 |
| 3d36-4f59 | the scripts (§3.1) | script 6 = `10, 1, 164, -10` |
| 0b02 | far pointer to slot 1 | 3d94:8fec |
| 0110 | i16 pairs (threshold, value), -1 terminated: end-sequence rank stars and rank-screen fill rows | (5,1), (25,8), (70,16) ... (9999,86), -1 |
| 0116, 011a, 011e, 0122, 0126, 012a, 012e | words inside DS:0110 used as row counts by `RankChest_Update` | 8, 16, 24, 32, 40, 48, 56 |
| 013a | "CHAMPION" (NUL-terminated) | – |
| 0b06 | i16 y positions, -1 terminated (treasure flight) | 64, 66, 68, ... 110, -1 (26 values) |
| 0b3c | i16 y positions, -1 terminated (showcase flight) | 108, 106, 102, ... 134, -1 (16 values) |
| 0b5e / 0b6a | 6 × i16 x / y (rank sparkles) | (7,108), (24,113) ... |
| 0b76 / 0b80 | 5 × i16 x / y (champion sparkles) | (81,49), (88,41) ... |
| 0184 / 0250 / 02ec | 4-byte picture attribute records per level; bits 4-5 of byte 0 = opened-picture offset | fmt_data §7.4 |
| 121c+18 (0x122e) | per level 4 words: picture count, ... (stride 8 bytes) | 50, 38, 31 |
| 11aa | 48 hiding-place cell indices | 0x77, 0x78, 0x7a ... |
| 7d7c | 4 × (flag byte, signed shelf base) for the prize rooms | (0,0), (0,12), (0,24), (0,36) |
| 00f6-00ff | treasure-room state words (initialised data) | 0xffff, 0xffff, 0xff, 0, 0x50, 0x50 |
| CS 176a:0654 / 06be | 53 case values / near targets of the treasure-room switch (code, §7.2) | 6, 8, 10 ... |
| 48de | frame word of script 0x88, patched at run time | 6 in the file |

---

## 11. Open points and capture requests

Requests are in `extracted/scratch/dos_requests/player.md`, answers in `player.answers.md`.
Answered and used above: 1 (treasure search), 2-4 (end sequence, treasure room, rank chest,
champion), 7 (jumps), 9 (loop rates), 10 (typematic). Still open:

1. Requests 5, 6, 8 (treasure room visited from the clubhouse with a crafted collection and the
   carousel, the shelf rooms with collected treasures and shelf bits, a gap slide) were not
   answered yet; §7.2-§7.3, §5.4 and states 29/30 are from code only.
2. Visual meaning of the treasure-room pair/fly displays and of the raised shelf copies (§5.4,
   §7.3) is inferred; the logic is complete.
3. Whether the "lost showcase treasure" cases of §5.3 happen in play (data-flow inference; a
   capture of two consecutive games with the door left in between would show it).
4. The "Trainee" label on the rank chest after a game that reached a total of 6 (§7.4): drawn by
   another module (UI agent).
5. Slot 12 is the cell-decoration slot (code_world.md §2.2): the sliding sound of states 29/30
   is suppressed while an animated decoration runs there; which slides have one is a world
   question.
6. The capture agent counted 38 loop frames for a search, the code gives 37 calls with the timer
   ≥ 1; the extra frame is presumably the frame that started the search or the timer-0 frame
   (counting convention) [I].
7. Interfaces with the world agent: the cell handlers named here (`Cell_P63_V63_CastleTop`, `Cell_P12_StartRoom`,
   `Cell_P11_ClubDoor`, `Cell_V49_Showcase`, shelf-room cells 50-55/65/66, type 23) are summarised only where they
   drive this module; their full specs are in `code_world.md`.

---

## 12. Porting notes (engine structure)

Suggested files in `src/dos/game/` (SCOPING §5.3):

* `anim.h/.cpp`: `struct Slot` (fields of §3.1), `struct DrawRec`, `ScriptTable` (a writable
  copy of DS:3d36-5261 read from the unpacked EXE plus the pointer table), `tick()`, `set()`,
  `setIfIdle()`, `queue()`, `stop()`, `start()`, `clear()`, `initSlots()`, `snapshot()`,
  `drawSorted()` (z order with ties to the higher index, child offsets, mirror, clip rules).
* `player.h/.cpp`: the 72-entry state table as a `switch` or array of member functions
  (unimplemented entries = no-op), `update(keys)`, `move()`, `wrapWorldX(Obj&)`, search start.
  Keep `g_keys` semantics (high byte = mask) because cell handlers and elves read it.
* `search.h/.cpp`: `Search_Update` as a per-frame function, showcase pick/hand-over, shelf
  drawing.
* `endseq.h/.cpp`: states 41-71 (they live in the player state table but form one script);
  `Cell_P64_EndScreen`, `Cell_P20_CastleGate`, the treasure room (`Cell_P70_TreasureRoom` + helpers) and the rank screen. The 53-case
  switch is code: implement it as a lookup table of {value, action} built from §7.2 (the values
  are game logic, not assets; the scripts and pictures still come from the user's files).
* Shared state: one `GameState` struct holding the DS variables named in this document (and in
  `re/names/dos/dgroup.tsv`), so that save/restore (USERS.DAT fields +0x164 shown treasure, +0x08A
  collected list, +0x133 shelf bits, header byte 1 = low byte of `g_saveHeapHigh`) maps 1:1.

Pitfalls:

* Two clocks. Everything that moves the game on (walking, climbing, scripts, elves, the end
  sequence, the treasure room, the rank fill) is tick-based and exact at 47.34 Hz. The few
  features counted in main-loop frames (§8) ran at very different loop rates in the original
  (≈55/s during a search, ≈900/s on the rank screen), so neither "one frame per tick" nor "one
  frame per presented engine frame" reproduces them. Give each its measured duration instead:
  search = 37 steps over ≈0.69 s (≈18.7 ms per step) plus the result frame; CHAMPION blink = 800
  steps over ≈0.88 s (≈1.1 ms per step, i.e. ≈88 ms per visible/invisible phase); HUD count-up
  ≈73 steps/s (UI agent). A "frame-step" helper driven by elapsed time with a per-feature step
  length is enough; offer the original speed as the default and record the measurements as
  constants with their source.
* Step gating only matters if the engine ever runs fewer frame steps than ticks; keep
  `g_stepDone` (elves and cells read it) but in practice it never blocks when the engine steps
  the logic once per tick.
* `Anim_Set` vs `Anim_SetIfIdle`/`Anim_Queue` first-tick difference (§3.2) and the snapshot skip
  rule for ended scripts (§3.4) are easy to "fix" by accident; both are visible.
* Script ops move the player (`-2/-3`) only through `g_playerDX/DY`, which `Player_Move` consumes
  in the same tick; elf scripts use `-4` through the slot's object pointer.
* Integer widths: positions are 16-bit; camera wrap tests are unsigned (`camX > 0x800`); the
  carousel and pair positions are unsigned; `g_shownTreasure`, partner 1, `g_shackY` and the
  saved shown value are signed; `g_collected` entries are signed chars (0x80 = empty, negative).
* Quirks to keep (all [O]): ladder jumps clear `g_slowCpu`; `Treasure_PickShowcase` compares only
  with `g_collected[0]`; out-of-range read `g_hidingCells[-1]`; `g_shackNoCase` never cleared;
  `Sfx_Play(1)` restarted every tick while sliding; walking starts 9 ticks after the key press;
  Space alone and the coin key are ignored while a standing/front script is still running.
* Nothing here consumes random numbers, so these modules cannot desynchronise the Borland LCG.

---

## 13. Function index and coverage

| Module | Functions | Specified here |
|---|---|---|
| 18cf player.c | `PlrSt_00_Idle` 000d, `EndSeq_Setup` 0038, `PlrSt_41_EndIntro`-`PlrSt_61_EndDone` 00a9-05b2 (21), `PlrSt_63_ChampSparkle`-`PlrSt_71_ChampSparkleWait` 05c4-07ea (9), `PlrSt_05_Searching` 07f6, `PlrSt_09_AfterTeleport` 081b, `PlrSt_03_Spawn` 082a, `PlrSt_04_Arrive` 0831, `PlrSt_10_OnPath` 08cc, `Player_NetLeft` 0b36, `Player_NetRight` 0b6a, `PlrSt_11_WalkLeft`-`PlrSt_35_TreasureRoomLand` 0b9e-14b1 (25), `Search_Start` 14e0, `Search_CanSearch` 1532, `Player_Move` 15f0, `Obj_WrapWorldX` 17c9, `Player_Update` 181a | 69 / 69 |
| 4306 anim.c | `Anim_Tick`, `Anim_InitSlots`, `Sprites_Snapshot`, `Sprites_DrawSorted`, `Sprites_EraseAll`, `Anim_SetIfIdle`, `Anim_Queue`, `Anim_Set`, `Anim_Stop`, `Anim_Start`, `Anim_Clear` | 11 / 11 |
| 4a03 search.c | `Search_Update`, `Treasure_PickShowcase`, `ShelfRoom_DrawShelf`, `Treasure_AddCollected` | 4 / 4 |
| 176a cells2.c (end screens) | `Cell_P64_EndScreen`, `Cell_P20_CastleGate`, `Cell_P70_TreasureRoom`, `TreasureRoom_Leave`, `Cell_P71_V71_ToRankScreen`, `Cell_P72_RankScreen`, `Game_AdvanceEnd`, `Treasure_IsCollected`, `ShackPair_08_36_Approach`-`ShackTrio_4a_4c_4e` (9), `Shack_ResetDisplay` | 18 / 18 (of the 35 in 176a; the path links, `Cell_P42_GateL2` and `Castle_CreatureSpawn` belong to the world topic) |
| 130f main.c (rank screen) | `RankChest_Update`, `RankChest_DrawStar` | 2 / 2 (of 14 in 130f) |
| outside, summarised | `Cell_P63_V63_CastleTop` trigger part (1568:1aa4), `Cell_V49_Showcase` (1568:1c7b), `Cell_P12_StartRoom` treasure-room/champion parts (1568:03f6), `Game_Tick` order (130f:00fe), `Playfield_Redraw` search band (130f:0326), `Gfx_ReplaceColorRect` (1000:176c), 1000:18af | interface only |

Suggested descriptive names for all these functions (e.g. `PlrSt_10_OnPath`, `PlrSt_49_EndFlightNext`,
`Cell_P70_TreasureRoom`, `ShackPair_06_5a`, `RankChest_Update`, `ShelfRoom_DrawShelf`) and for 50
DGROUP variables named in this document are now canonical in `re/names/dos/` (wave 2 merge).
