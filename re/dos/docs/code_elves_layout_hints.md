# SST.EXE segments 4521, 1a54, 49a6: elves, new-game layout, New Player Hints and game messages

Scope: every function of the three DOS-edition modules `elves.c` (segment `4521`, 53 functions),
`layout.c` (`1a54`, 11 functions) and `hints.c` (`49a6`, 6 functions) of `SST.EXE` (1990, V 1.0),
written so that wave 3 can implement them without reading the decompiled code. Names follow
`re/names/dos/*.tsv`; renames and new DGROUP names proposed by this pass are in
`re/names/dos/wave1_elves.tsv` (the names below already use them where they differ, with the old
name in brackets the first time).

Evidence: `re/dos/ghidra_export/` (`functions/*.c`, `disassembly.asm`), the unpacked image
`extracted/SST_unpacked.EXE` (DGROUP bytes), the SPRITES.PAK frame previews, the USERS.DAT
snapshots in `extracted/captures/dos/` and the EGA captures. Scratch scripts are in
`extracted/scratch/dos_w1_elves/`: `ds.py` (DGROUP reader), `anims.py` (animation-script decoder),
`layout_sim.py` + `brute.py` (layout simulator and seed search, §5.6), `stats.py` (layout data
checks), `craft_users.py` (crafted USERS.DAT for capture request R1). None of them is a
deliverable; they read the user's files at run time.

Notation: **[O]** observed in code, bytes, decoded data or captures; **[I]** inferred.
`SSSS:OOOO` = Ghidra address; `DS:XXXX` = DGROUP `4d94:XXXX` (file offset `0x3FB30 + XXXX`).
Widths: u8/i8, u16/i16 (all game arithmetic is 16-bit unless stated). "Bytes" as an x unit means
2-pixel units of the 4-bpp back buffer. `rand()` is Turbo C `rand` (4d12:001f: seed = seed ×
0x015A4E35 + 1 mod 2³², result (seed >> 16) & 0x7FFF; initial seed 1 at DS:8f48), the same LCG as
`src/rtl/borland_rand`. `r % n` on a rand result is always non-negative (rand ≤ 0x7FFF), so signed
`IDIV` and unsigned `DIV` give the same result. Game text is referred to by message number /
DS offset only.

---

## 1. Module overview

| Seg | Module | Role | Functions |
|---|---|---|---|
| `4521` | `elves.c` | spawn of up to 15 elves per scene, per-tick update, sprite-slot allocation, net classification, the 76-entry elf state machine (walkers, clue carriers, dust throwers, coin creatures), dust hits, catches, dodges, escapes | 53 (`4521:000c`..`4521:1ad6`) |
| `1a54` | `layout.c` | new-game layout: clue scheme, key, treasures, decoys and empty places of all 44 hiding places; mirror cells; variable-picture cells; castle ladder layout; slow-CPU cell patches | 11 (`1a54:0003`..`1a54:092f`) |
| `49a6` | `hints.c` | New Player Hints (15 one-shot flags, Flutter bubble), start-of-game and end-of-game messages; one sound-port helper at its start | 6 (`49a6:0001`..`49a6:05d2`) |

None of the three modules calls `Sfx_Play` or `Music_Play` itself: all elf sounds come from the
animation scripts they start (§3.5: effect 2 dust thrown, 5 dust hit, 3 coin creature runs / elf
escapes, 4 coin creature collected) [O callee lists].

Non-function bytes [O]: `4521:0000`–`000b` belong to the end of segment `43cb`; the byte at
`4521:000b` is `Null_Handler2` (43cb:156b, a lone `RETF`), which the elf state table uses as its
no-op handler. `49a6:0000` is a `RETF` padding byte. `1a54:0923`–`092f` is zero padding.

---

## 2. Runtime context used by these modules

These belong to other modules (cells, player/anim, main loop, quiz, UI) and are summarised only as
far as the three modules depend on them.

### 2.1 Tick and frame

* `Game_Tick` (130f:00fe) runs at 47.3 Hz from the timer interrupt [O]. If `g_stepDone` (DS:910d) is
  0 at the start of the tick it runs `Mouse_Poll`, `Input_Tick`, `Anim_Tick`, then — when the loaded
  scene is the wanted one — `Input_MouseWalk`, `Player_Update`, **`Elves_Update`**,
  `Cells_UpdateVisible`. If `g_stepDone` is 1 it runs only `Anim_Tick` (which then advances nothing)
  and `Input_Tick`. Ticks do nothing while `g_quit` (DS:912e) ≠ 0 (also used as "scene loading").
* `g_stepDone` is set by `Anim_Tick` whenever any sprite slot shows a new frame, and by camera
  moves; it is cleared by `Frame_Begin` at the start of every drawn frame. **All elf logic is per
  game tick, at most one logic tick per drawn frame** [O].
* The frame loop (`main` 130f:0007) calls, when no full redraw is pending (`g_fullRedraw` DS:8fbb,
  DS:8fb2 and DS:8fc8 all 0): `SignIn_Update`, **`Gameover_UpdateMessages`**, **`Hints_Update`**,
  `Menu_Update`, then the rest of the frame. **Hints and messages are per drawn frame** [O].
* `g_seconds` (DS:9115): the byte counter DS:9100 is decremented every tick (gated by nothing but
  `g_quit`); when it reaches 0 *and* `g_uiMode` == 0 it is reloaded with 60 and `g_seconds` += 1, so a
  hint "second" is 60 ticks = 1.27 s. If `g_uiMode` ≠ 0 at the tick it reaches 0, it wraps to 0xFF and
  the next "second" takes 256 more ticks (5.4 s) [O 130f:01a6].

### 2.2 Sprite slots and animation scripts (owned by `anim.c`, 4306)

13 slots of 22 bytes at DS:8fd6 [O]:

| Off | Field | Notes |
|---|---|---|
| +0x00 | x, screen bytes | for a child slot: offset from its parent |
| +0x02 | y, screen pixels | child: offset |
| +0x04 | depth | sprites are drawn deepest first (larger = further back) |
| +0x06 | frame code | `2·k` (+1 = mirrored) for PAK frame k; 0xFFFF = none |
| +0x08 | parent slot | the slot whose script drives this one; a slot ≠ its parent is drawn at parent position + own x/y (4306:059b) |
| +0x0A | current animation id (0 = idle) | |
| +0x0C | previous animation id | set when a script stops or is replaced |
| +0x0E | script position (words after the 2-word header) | |
| +0x10 | delay counter | |
| +0x12 | far pointer to the owning object | target of script op −4 |

Slot 0 is the mouse pointer, slot 1 the player (child slot 2). **Elves use slots 6, 8 and 10, each with its
child slot 7, 9, 11** (for the carried pencil, the dust cloud, the escape sprite) [O 4521:10f6].

Script (DS:4f5a = 194 far pointers into DS:3d36–4f59): word 0 = delay, word 1 = n (slots driven:
the slot itself and n−1 children), then entries. An entry is either n frame codes, or one command
followed by n frame codes: −2 v (player dx += v), −3 v (player dy += v), **−4 v (owner object
dx += v: this is how elves move)**, −5 id (`Sfx_Play(id)`), −6 (castle strike counter), −7 dx dy
(move the slot), −10 (stop: current → previous, anim = 0, position = 0, frames stay on screen).
One frame lasts `delay + 1` ticks; a script of F frames runs F·(delay+1) ticks plus one tick for the
stop entry [O Anim_Tick 4306:0022]. While `g_stepDone` is set, delay counters keep counting down but
a slot whose counter has run out waits (stays at 0) until the next free tick.

Calls used by elf code [O]: `Anim_SetIfIdle(slot, id)` (4306:0784: sets the id only if the slot is
idle; the first frame appears at the next `Anim_Tick`), `Anim_Set(slot, id)` (4306:07fa: if the id
differs, stops the current one and starts the new one at once, showing its first frame),
`Anim_Clear(slot)` (4306:092f: id, previous, delay, position = 0, frame = none). A looping walk is
"script ends → handler sees the slot idle → `Anim_SetIfIdle` restarts it" in the same tick.

### 2.3 Other interfaces

| Item | Behaviour |
|---|---|
| `g_curObj` (DS:8fab) | far pointer to the object being processed (player DS:37e2 or an elf) |
| `Cell_GetMask` (1568:1fe4) | calls the player-cell handler of the cell containing `g_curObj` (index `(x>>7) + (y>>7)·16`, not masked); handlers may move, turn (`Elf_Turn`) or convey the object; see `code_cells.md` |
| `Obj_WrapWorldX` (18cf:17c9) | x ≥ 0x8000 (negative): x += 0x800, y −= 0x80; then x > 0x7FF: x −= 0x800, y += 0x80; both clear +6 |
| `g_playerState` (DS:913c) | 17/18 jump left, 19/20 jump right, 21–23 net left, 24–26 net right (21 and 24 are the "net down" states); see `code_player_anim.md` |
| player position | world x/y DS:37e2/37e4; screen x (bytes) of slot 1 at DS:8fec |
| `g_searchTimer` (DS:8fa3) | > 0 while a hiding-place search animation runs (per frame, `Search_Update`) |
| `g_caughtSlot` (DS:921e) | question slot of the caught carrier; the quiz clears it after a wrong answer (`Quiz_Wrong` 46ce:069f) or when closed without a right answer (46ce:00dd); it stays non-zero after a right answer [O] |
| `g_uiMode` (DS:9119) | 0 play; bits 0x80 title, 0x40 menu, 0x20 dialog, 0x10 hint, 8 sign-in, 4 message, 2 quiz |
| `g_transition` (DS:9217) | ≠ 0 during teleports |
| `Sfx_Play(id)` | lower ids have priority (fmt_sound §7); elf effects come from scripts |

---

## 3. Elf data

### 3.1 The elf record (`g_elves` DS:37fa, 15 × 32 bytes) [O]

| Off | Type | Field |
|---|---|---|
| +0x00 | u16 | world x (0..0x7FF within a row) |
| +0x02 | u16 | world y (row·0x80 + 0x70 on a path) |
| +0x04 | i16 | dx for this tick (added to x by `Elves_Update`) |
| +0x06 | i16 | cleared by `Obj_WrapWorldX`; not used by elf code |
| +0x08 | u16 | base state (type and facing: 1/5 A, 9/13 B, 17/21 C, 25 D; 0 = unused record) |
| +0x0A | u16 | state (index into the handler table; ≥ 0x2D = has a sprite) |
| +0x0C | u16 | question slot 1..3 (clue carrier) or 0 |
| +0x0E | u16 | sprite slot 6/8/10 (0 after release; stale values are possible, §4.1) |
| +0x10..0x1F | – | not used by elf code |

### 3.2 Elf globals [O]

| DS | Name | Meaning |
|---|---|---|
| 9132 | g_elfSpriteCount | elves holding a sprite this tick, +1 on a slow CPU; recomputed at the start of every `Elves_Update` |
| a204 | g_elfRelX | elf x relative to the camera in bytes (`Elves_Update`) |
| a206 | g_elfRelY | elf y within its cell minus camera y within its cell |
| a208 | g_playerRelX | player slot x − 2 (bytes relative to the view's left edge) |
| a20a | g_netCatchesThisTick | catches resolved in the current `Elves_Update`; **reset at its end** (4521:1056) |
| a20c | g_elfNetClass | result of `Elf_Think` for the current elf: 0 none, 1 released, 2 got a sprite, 3 visible, 4 net edge, 5 in the net, 6 net centre |
| a20e | g_elfCallCounter | +1 before every handler call; bit 1 gates hidden drift |
| a210 | g_elfAnimSel | scratch animation offset shared by handlers (0 or 0x16 for carriers, …) |
| a212 | g_elfActiveCount | number of records processed: 15, or 7 if `g_slowCpu` == 1 at spawn |
| 8fa5 | g_slowCpuSaved | `g_slowCpu` (DS:923c) saved at game start/restore; elves run only while the two are equal |

### 3.3 Tables (read from SST.EXE at run time)

| DS | Layout | Use / example |
|---|---|---|
| 5d34 | `u16 counts[4 tiers][4]` = {A walker/carrier, B thrower, C walker, D coin creature} | tier 0 row = 3, 0, 3, 3; B column is 0/2/4/6 by tier |
| 5d54 | `u16 range[3 levels][4]` = {x base, y, x mask, unused} | level 0 = 0x300, 0x770, 0x7FF; level 1 = 0, 0x5F0, 0x7FF; level 2 = 0, 0x470, 0x3FF |
| 5d6c | `far handler[76]` indexed by state | §3.4 |
| 4f5a | animation scripts | §3.5 |

Spawn ranges that follow [O]: level 0: x = 0x300 + r, so rows 14 (x 0x300–0x7FF, y 0x770) and 15
(x 0–0x2FF after the wrap, y 0x7F0); level 1: row 11 anywhere; level 2: row 8, x 0–0x3FF.

### 3.4 State table DS:5d6c [O]

States without an entry below use `Null_Handler2` (no-op): 0, 11, 15, 19, 23, 27, 29–44, 48, 75.

| State | Handler | Meaning |
|---|---|---|
| 1 / 5 | `ElfSt_01` / `ElfSt_05` | type A hidden, facing left / right |
| 2, 6 | `ElfSt_02` | type A resumes walking (only called as handler[base+1]) |
| 3, 7 | `ElfSt_03` | (only via state 67) |
| 4, 8 | `ElfSt_04` | type A turn (called as handler[base+3] by `Elf_Turn`) |
| 9 / 13 | `ElfSt_09` / `ElfSt_13` | type B hidden left / right |
| 10, 14 | `ElfSt_10` | type B resumes |
| 12, 16 | `ElfSt_12` | type B turn |
| 17 / 21 | `ElfSt_17` / `ElfSt_21` | type C hidden left / right |
| 18, 22 | `ElfSt_18` | type C resumes |
| 20, 24 | `ElfSt_20` | type C turn |
| 25, 26 | `ElfSt_25` | coin creature hidden (26 = handler[base+1]) |
| 28 | `ElfSt_28` | coin creature turn = release |
| 45 / 46 | `ElfSt_45` / `ElfSt_46` | type A walking left / right |
| 47 | `ElfSt_47` | type A pause |
| 49 / 50 | `ElfSt_49` / `ElfSt_50` | type B walking left / right |
| 51 | `ElfSt_51` | type B pause |
| 52–55 | `ElfSt_52..55` | type B throw to the left: wind-up, dust out, dust back with a coin, end |
| 56–59 | `ElfSt_56..59` | the same to the right |
| 60 / 61 | `ElfSt_60` / `ElfSt_61` | type C walking left / right |
| 62 / 65 | `ElfSt_62` / `ElfSt_65` | being pulled toward the net centre (moving right / left) |
| 63 / 66 | `ElfSt_63` / `ElfSt_66` | held in the net: resolve the catch |
| 64 | `ElfSt_64` | dodging, getting up: wait for the animation, then handler[base+1] |
| 67 | `ElfSt_03` | carrier after its question |
| 68 | `ElfSt_68` | frozen while a search runs |
| 69 | `ElfSt_69` | caught non-carrier zips away |
| 70 / 71 / 72 | `ElfSt_70/71/72` | coin creature lying / running away / collected |
| 73 / 74 | `ElfSt_73` / `ElfSt_74` | escaped from the net (1/64): jump, then fall away |

### 3.5 Animations used by elves (DS:4f5a; summary, the engine reads the scripts)

Frame numbers are SPRITES.PAK frames; "m" = mirrored. Native elf frames face right [I from the
walk scripts: walking left uses mirrored frames]. Durations with the formula of §2.2: a delay-6
frame lasts 7 ticks = 148 ms, matching the ~152 ms elf animation step measured in the EGA and
Tandy AVIs (REFERENCE_CAPTURES §5.9).

| Id | Used for | Summary [O decoded with `anims.py`] |
|---|---|---|
| 0x1F / 0x20 | type A walk left / right | delay 6, n 1, 10 frames (94–100), 8 moves of 8 px (64 px per 71-tick cycle) |
| 0x41 / 0x42 | carrier walk | as 0x1F/0x20 but n 2: parent = carried pencil (103–109), child = body |
| 0x21 | pause (front view, 101) | delay 10, n 2 (hides the child), 1 frame (12 ticks with the stop) |
| 0x43 | carrier pause | same frames as 0x21 |
| 0x22 | stand (102), search freeze | delay 6, 1 frame |
| 0x23 / 0x24 | type B walk left / right | delay 6, 8 frames (112–119), 6 moves of 8 px |
| 0x25 / 0x26 | throw wind-up (97, 120, 121) | 3 frames; 0x25 (thrower facing left) uses unmirrored frames [I: turns to throw over the shoulder] |
| 0x27 / 0x28 | dust flight | n 2 (122/123 + dust 125–127), sfx 2 first, 14 frames |
| 0x29 / 0x2A | dust hit | n 2, sfx 5 first, child shows a burst (128) then the coin with sparkles (129–131), 14 frames |
| 0x2B / 0x2C | thrower shows the coin | n 2, 3 frames (coin 132/133) |
| 0x2D / 0x2E | type C walk | delay 6, 6 frames (148–151, 97), 4 moves of 8 px |
| 0x2F / 0x30 | pulled into the net (152–155) | **delay 4**, 6 frames, 3 moves of +4 / −4 px |
| 0x45 / 0x46 | carrier pulled | n 2 (pencil 157–160 / body), **delay 6**, same moves |
| 0x31 / 0x32 | held in the net (155) | 1 frame; carrier 0x47 / 0x48 (n 2) |
| 0x33 / 0x34 | dodge hop (97, 152, 153, 161–163) | 0x33: 4 moves of −8 (−32 px); **0x34: +8, +8, −8, +8 (+16 px)** — data asymmetry |
| 0x49 / 0x4A | carrier dodge | same moves as 0x33/0x34 |
| 0x35 / 0x36 | get up from an empty net and walk off (156, 113, 148–151, 97) | 4 moves of ∓8 px; 0x4B / 0x4C are the same two scripts (same DS address) |
| 0x4D / 0x4E | caught non-carrier zips away (ZIP 178–181) | 4 frames |
| 0x4F | escape jump (221, 101, 222 ×4) | delay 8, sfx 3 before the last frame |
| 0x50 | escape sprite | n 2, parent hidden, child 222, 8 frames, delay 8 |
| 0x56 | coin creature lying (gold coin, 203) | 1 frame |
| 0x57 / 0x58 | coin creature runs right / left on legs (204–206) | sfx 3 first, 2 moves of ±8 px |
| 0x59 | coin creature collected (sparkle 174) | sfx 4 first, 1 frame |

The "coin elf" of ARCHITECTURE §5.10 is a **gold coin lying on the path that grows legs and runs
away** (frames 203–206 [O pictures]); this document calls it the coin creature.

---

## 4. Elves (segment 4521)

### 4.1 `Elves_Spawn(carriers)` (4521:182e) — scene change

Called only from `Scene_Update` (1437:00fe) when a scene is loaded and the wanted scene is a
mountain level (< 3), with `carriers` = return value of `Quiz_ReorderSlots` = the number of clue
words of `g_level` still hidden (bit 7 set) [O 46ce:0df2]. Steps [O]:

1. `g_elfActiveCount` = 15; = 7 if `g_slowCpu` == 1.
2. For all 15 records: base = 0, state = 0, question slot = 0. **x, y, dx and the sprite slot are
   not cleared** (they keep the previous scene's values; dx is BSS 0 at program start).
3. Using `g_tier` (DS:9130) and `g_level` (DS:928f), fill records sequentially from record 0:
   1. type A, `counts[tier][0]` times (index i = 0..): base = state = 1 if i even, 5 if odd;
      question slot = i+1 if i < `carriers` (signed compare), else 0;
   2. type D (coin creature), `counts[tier][3]` times: base = state = 25;
   3. type B, `counts[tier][1]` times: base = state = 9 (even i) / 13 (odd i);
   4. type C, `counts[tier][2]` times: base = state = 17 / 21.
   For every record, in this order: `r = rand()`; `x = (r & range[level].mask) + range[level].base`;
   `y = range[level].y`; if x > 0x7FF: x −= 0x800, y += 0x80.
4. **rand() order:** exactly one call per spawned elf, in record order A, D, B, C (9, 11, 13 or 15
   calls). On a slow CPU all records are still spawned; only the first 7 are processed later (at
   tier 0: 3 A, 3 D, 1 C).

Records beyond the spawned count keep base 0 and are skipped by `Elves_Update`. A record that held
a sprite at the scene change keeps its slot animation running and its slot is never released
[O: no `Elf_Release` here]; in practice scene changes happen during vertical moves when the old
row's elves have already been released [I] (open point O3).

### 4.2 `Elves_Update` (4521:0ead) — every game tick

Runs only if `g_uiMode` == 0 **and** `g_slowCpu` == `g_slowCpuSaved` [O]. (On a slow CPU the
ladder-jump and castle code clears `g_slowCpu` temporarily, which freezes the elves there; on a fast
machine both are 0.)

1. `cam = (camY & 0xFF80)·16 + camX` (u16; camera DS:37ea/37ec).
2. `g_elfSpriteCount` = (g_slowCpu ≠ 0 ? 1 : 0) + number of records i < `g_elfActiveCount` with
   state ≥ 0x2D.
3. For each record i < `g_elfActiveCount` with base ≠ 0, in index order:
   * `g_playerRelX` = slot1.x − 2;
   * `g_elfRelX` = `((y & 0xFF80)·16 + x − cam) >> 1` (16-bit, arithmetic shift);
     `g_elfRelY` = `(y & 0x7F) − (camY & 0x7F)`;
   * `Elf_Think()` (§4.3, which calls the state handler);
   * `Cell_GetMask()` for the elf (the cell handler may move or turn it);
   * `x += dx`; `Obj_WrapWorldX()`;
   * if state ≥ 0x2D (has a sprite): dx = 0; recompute `rel = (y & 0xFF80)·16 + x − cam` (pixels)
     and `ry = (y & 0x7F) − (camY & 0x7F)`; sprite slot s = record +0x0E:
     `slot[s].x = (rel >> 1) + 2`, `slot[s].y = ry + 0x10`, `slot[s].parent = s`,
     `slot[s].owner = this record`.
4. `g_netCatchesThisTick` = 0.

So a hidden elf keeps its dx between ticks; a visible elf's dx is rebuilt every tick by script op
−4 (applied by `Anim_Tick` earlier in the same tick) and consumed here.

### 4.3 `Elf_Think` (4521:1062) — visibility, sprite slot, net class

Inputs: `g_elfRelX` (call it X), `g_elfRelY` (Y), `g_playerRelX` (P), the record. Steps [O]:

1. class = 0.
2. **Out of range** if Y < 0, or Y > 0x80 (unsigned), or X ≤ −0x12, or X ≥ 0xAE: if state ≥ 0x2D
   → `Elf_Release` (class 1). Go to 5.
3. In range, state < 0x2D (no sprite): if `g_elfSpriteCount` < 3 (unsigned) and any of
   * `g_transition` ≠ 0,
   * X < 0 and dx ≥ 0 (just left of the view, not moving away),
   * X > 0x9C and dx ≤ 0 (just right of the view, not moving away)

   (the extra clauses "Y < 0" and "Y > 0x98" in the code can never be true here), then:
   `g_elfSpriteCount` += 1; class = 2; search slots 6, 8, 10 for the first whose current and
   previous animation ids are both 0; if found: record slot = it, and **`rand()`**: if odd,
   slot depth = 0x28, else 0x3C. (If no slot is free, class is still 2 and no rand is drawn.)
   If class is still 0 afterwards: dx = 0.
   → Hidden elves only get a sprite when they enter at a view edge (34 px wide zones beyond the
   312-px play-field, x bytes −17..−1 and 157..173) or during a teleport.
4. In range, state ≥ 0x2D: class = 3; then, with the player in a net-down state:
   * state 24 (net right): X in [P+9, P+42] → 4; X in [P+15, P+36] → 5; X in [P+23, P+28] → 6;
   * state 21 (net left): X in [P−44, P−11] → 4; [P−38, P−17] → 5; [P−30, P−25] → 6.

   (Inclusive, signed; the inner zone overrides the outer. The zones are mirror images about P−1.
   In pixels relative to the player sprite: edge 18–84, net 30–72, centre 46–56. REFERENCE_CAPTURES
   §5.5 measured the net landing 33–73 px in front of the player, consistent with the net zone.)
5. `g_elfCallCounter` += 1; call `handler[state]`.

### 4.4 Type A — walkers and clue carriers

`sel` below = 0x22 for a carrier (question slot ≠ 0), else 0, so `0x1F + sel` = 0x41.

* **Hidden (1 / 5, `ElfSt_01/05`)**: if class < 2: if (`g_elfCallCounter` & 2) == 0, dx = −1 / +1
  (otherwise dx unchanged). If class ≥ 2 (sprite just obtained): `Anim_SetIfIdle(slot, carrier ?
  0x41/0x42 : 0x1F/0x20)`, state = 0x2D / 0x2E. → Out of view a hidden elf moves 1 px every tick
  once dx is set (it is never cleared out of view); in view without a sprite it moves 1 px on about
  half of the calls.
* **Walking (45 / 46, `ElfSt_45/46` → `Elf_WalkCommon(walk id)`)**: `Elf_CheckNet()` (§4.8);
  **`rand()` every call**; if `(r & 0x0F) == 0` and the slot is idle: `Anim_SetIfIdle(slot, 0x21)`,
  state = 47; then `Anim_SetIfIdle(slot, walk id)`. Because the walk script is restarted the moment
  it ends, the slot is idle only on that tick: **the pause chance is 1/16 per finished walk cycle
  (64 px)**, while a rand is consumed on every tick [O].
* **Pause (47, `ElfSt_47`)**: `Elf_CheckNet()`; dx = 0; `r1 = rand()` (every call); if (r1 & 1) == 0
  and the slot is idle: `r2 = rand()`; even → base 1, state 0x2D (walk left); odd → base 5, state
  0x2E. Then `Anim_SetIfIdle(slot, carrier ? 0x43 : 0x21)`. A pause therefore lasts one or more
  12-tick pause scripts, and on the tick the walk state is chosen the slot is still idle, so one
  more pause script is armed; the walk script starts when that one ends (and `Elf_WalkCommon` may
  roll another pause at that moment).
* **Resume (`ElfSt_02`, via handler[base+1] from state 64)**: slot depth −= 10; if the elf is right
  of the player (P < X): base 5, `Anim_Set(slot, 0x20 + sel)`, state 0x2E; else base 1,
  `Anim_Set(slot, 0x1F + sel)`, state 0x2D. **It walks away from the player.**
* **Turn (`ElfSt_04`, via `Elf_Turn`)**: only if state ≥ 0x2D: base 5 → 1 with `Anim_Set(0x1F+sel)`,
  state 0x2D; otherwise → base 5, `Anim_Set(0x20+sel)`, state 0x2E. Hidden elves ignore turns.

### 4.5 Type B — elf-dust throwers

* **Hidden (9 / 13)**: as type A with dx −1/+1; on a new sprite `Anim_SetIfIdle(slot, 0x23 / 0x24)`,
  state 49 / 50.
* **Walking left (49, `ElfSt_49`)**: `Elf_CheckNet()`; if the slot is idle: `r1 = rand()`; if r1 even:
  if `P < X − 0x26` (player more than 38 bytes = 76 px ahead to the left) → `Anim_SetIfIdle(0x25)`,
  state 52 (throw); else `Anim_SetIfIdle(0x21)`, state 51 (pause). If r1 odd: `r2 = rand()`; if
  `(r2 & 0x0F) == 0` → pause (0x21, state 51). Finally `Anim_SetIfIdle(slot, 0x23)`.
  There is no upper distance limit: any visible thrower may throw.
* **Walking right (50, `ElfSt_50`)**: the same with the test `X + 0x2A < P` (player more than 42
  bytes ahead to the right) → 0x26, state 56; walk id 0x24.
* **Pause (51, `ElfSt_51`)**: `Elf_CheckNet()`; dx = 0; if idle: `r = rand()`: even →
  `Anim_SetIfIdle(0x23)`, base 9, state 49; odd → `Anim_SetIfIdle(0x24)`, base 13, state 50. Then
  `Anim_SetIfIdle(0x21)` (a no-op when a walk script was just set).
* **Resume (`ElfSt_10`, handler[base+1] from state 64)**: depth −= 10; if P < X: base 13,
  `Anim_Set(0x24)`, state 50; else base 9, `Anim_Set(0x23)`, state 49 (away from the player).
* **Turn (`ElfSt_12`)**: only if visible: base 13 → 9 with `Anim_Set(0x23)`, state 49; otherwise →
  base 13, `Anim_Set(0x24)`, state 50.
* **Throw left**:
  * 52 (`ElfSt_52`): `Elf_CheckNet()`; dx = 0; when the wind-up has ended (slot idle):
    `Anim_SetIfIdle(0x27)`, child slot x = y = 0, state 53.
  * 53 (`ElfSt_53`): `Elf_CheckNet()`; `D = child.x + X` (dust position, bytes); dx = 0.
    **Hit** if `P − 10 < D < P + 10` (signed, strict) **and** coins (DS:9134) ≠ 0 **and**
    `(player y & 0x78) == 0x70` (on the path line) **and** player state not in 17..20 (unsigned
    test < 17 or > 20): coins −= 1, state 54, `Anim_Set(slot, 0x29)` (sfx 5).
    Else if child.x == −0x50: the dust vanishes — `Anim_Set(slot, 0x23)`, child frame = none,
    base 9, state 49 (walks on to the left). Else `Anim_SetIfIdle(slot, 0x27)` and child.x −= 1
    (**the dust moves 1 byte = 2 px per tick, at most 80 ticks**).
  * 54 (`ElfSt_54`): dx = 0; if child.x == 0: state 55, `Anim_Set(0x2B)`; else child.x += 1 (the
    dust carries the coin back, 2 px per tick).
  * 55 (`ElfSt_55`): dx = 0; when idle: `Anim_Set(0x24)`, child frame = none, base 13, state 50
    (walks away to the right with the coin).
* **Throw right** (56–59): mirror image with 0x28 / 0x2A / 0x2C, child.x counting up to +0x50 and
  back, end walking left (base 9, state 49); a miss continues right (base 13, state 50). **Quirk:
  in state 57 only player states 17..18 (jumping left) avoid the hit** (test `< 17 or > 18`), so a
  player jumping right (19/20) is still hit by dust thrown to the right [O 4521:0b06].

Netting does not protect the player (states 21–26 are not excluded) [O; ARCHITECTURE §5.10 says
otherwise]. A thrower netted during its throw loses the dust (`Elf_CheckNet` hides the child).

### 4.6 Type C — plain walkers

* Hidden (17 / 21): as type A with dx −1/+1; new sprite → `Anim_SetIfIdle(0x2D / 0x2E)`, state 60 /
  61.
* Walking (60 / 61): `Elf_CheckNet()`; `Anim_SetIfIdle(slot, 0x2D / 0x2E)`. No pauses, no rand.
* Resume (`ElfSt_18`): depth −= 10; away from the player: base 21 / anim 0x2E / state 61 if P < X,
  else base 17 / 0x2D / 60. Turn (`ElfSt_20`): flip 17 ↔ 21 with the walk animation, visible only.

### 4.7 Type D — the coin creature

* Hidden (25, `ElfSt_25`): if class ≥ 2 (sprite obtained this tick) **and** coins < 4 **and** nets
  (DS:910f) < 5: `Anim_SetIfIdle(slot, 0x56)`, state 70. Otherwise nothing (it never moves while
  hidden; if it got a slot but the condition failed, the slot stays free and the counter was bumped
  for this tick only). It can therefore appear only when its position passes through a view edge
  zone while coins < 4 and nets < 5.
* Lying (70, `ElfSt_70`): `Anim_SetIfIdle(0x56)`, then `CoinElf_Check`.
* Running (71, `ElfSt_71`): `Anim_SetIfIdle(slot, slot.previous)` (restarts the last run script, so it
  **keeps running** in the same direction), then `CoinElf_Check`.
* `CoinElf_Check` (4521:0d93): if class < 4: if `P − 5 ≤ X ≤ P + 5` and player state not in 17..20 →
  state 72, `Anim_Set(0x59)` (sfx 4). If class ≥ 4 (inside the net-edge zone of a net-down swing):
  state 71, `Anim_Set(0x57)` if P < X (runs right, away) else `Anim_Set(0x58)` (runs left).
* Collected (72, `ElfSt_72`): when the slot is idle: `Elf_Release()`; state = base (25);
  **`rand()`**: new x/y from the level range exactly as in §4.1; **coins += 2**.
* Turn (`ElfSt_28`): if visible, `Elf_Release` (it disappears where it stands).

### 4.8 Net handling (`Elf_CheckNet` 4521:1648 and the catch states)

`Elf_CheckNet` is called by the walking, pause and throw-out handlers (45, 46, 47, 49–53, 56, 57,
60, 61). Steps [O]:

1. `g_elfAnimSel` = 0x16 if carrier else 0.
2. If `g_searchTimer` ≠ 0 and the slot is idle: **freeze** — state 68, child frame = none,
   `Anim_SetIfIdle(slot, 0x22)`. Done.
3. Net centre C = P + 26 (player state 24) or P − 28 (state 21). (Classes 4–6 only occur in those
   states.)
4. Class 4 (edge): slot depth += 10; child frame = none; **`rand()`**: if `(r & 0x3F) == 0`
   (**1/64**): state 73, `Anim_Set(0x4F)` (escape). Else state 64 and `Anim_Set(sel + 0x34)` if
   C < X (hop away to the right) else `Anim_Set(sel + 0x33)` (hop away to the left).
5. Class 5 or 6: depth += 10; child frame = none; if C < X: `Anim_Set(sel + 0x30)` (pulled left),
   state 65; else `Anim_Set(sel + 0x2F)` (pulled right), state 62.

Catch states [O]:

* 62 / 65 (`ElfSt_62/65`): when idle: `Anim_Set(carrier ? 0x47 : 0x31)` / `(0x48 : 0x32)`, state + 1.
  If class == 6 this tick: dx = 0 (the pull stops once the elf is in the centre zone).
* 63 / 66 (`ElfSt_63/66`): `g_elfAnimSel` = carrier ? 0x16 : 0 (state 63) or 0x17 : 1 (state 66);
  `Elf_NetCatch()`.
* `Elf_NetCatch` (4521:147c):
  * nets == 0: when idle: child frame = none, state 64, `Anim_SetIfIdle(sel + 0x35)` (gets up
    and walks off; the "torn net" catches nothing).
  * else, if player state in 21..26 and (`g_uiMode` & 2) == 0:
    * non-carrier: if `g_netCatchesThisTick` == 0: nets −= 1; `g_netCatchesThisTick` += 1;
      **coins += 1**; state 69; `Anim_Set(0x4E)` if P < X else `Anim_Set(0x4D)` (zips away);
    * carrier, only if player state in 21..25: nets −= 1 if `g_netCatchesThisTick` == 0; coins += 1;
      `g_netCatchesThisTick` += 1; `g_caughtSlot` = question slot; `g_uiMode` |= 2 (**opens the
      question**); state 67.
  * finally `Anim_SetIfIdle(slot, sel + 0x31)` (held pose).
  * If the player has already left the swing states, the elf stays in 63/66 (held) until it leaves
    the view or the next swing.
* 64 (`ElfSt_64`): when idle: call handler[base + 1] (ElfSt_02 / 10 / 18 / 25), which restores the
  depth and sets the walking state.
* 67 (`ElfSt_03`, first tick after the quiz closes): if `g_caughtSlot` ≠ 0 (answered right) the elf
  loses its question (slot 0); child frame = none; state 64; `Anim_SetIfIdle(g_elfAnimSel + 0x35)`.
  It uses the **stale** `g_elfAnimSel`, normally 0x16 / 0x17 left by its own `ElfSt_63/66` in the
  catch tick (or 0 / 1, the same scripts); another visible elf processed after it in that tick can
  leave a different value and select an unrelated script [O, rare].
* 69 (`ElfSt_69`): when idle: state = base; `Elf_Release`.
* 68 (`ElfSt_68`): when `g_searchTimer` == 0: `g_elfCallCounter` = 0; call handler[base].
* 73 (`ElfSt_73`): when idle: slot depth = 0x1E, `Anim_SetIfIdle(0x50)`, state 74.
* 74 (`ElfSt_74`): child.y += 2 per tick; when child.y > 0xA2 (signed): `Elf_Release` [O]; the
  escape sprite moves down out of the play-field [I visual].

**Net cost [O]:** every catch resolution uses one net, except that catches resolved in the same
`Elves_Update` call share one (`g_netCatchesThisTick` is reset at the end of every call). Two elves
pulled in by the same swing usually resolve in the same tick and cost one net; elves caught on
different ticks cost one each. This corrects ARCHITECTURE §5.9 ("first elf caught in a swing").

### 4.9 `Elf_Release`, `Elf_Turn`, `Elves_ShiftNearby`

* `Elf_Release` (4521:1268): `g_elfSpriteCount` −= 1; class = 1; state = base; s = sprite slot;
  record slot = 0; `Anim_Clear(s)`; child slot s+1: frame = none, x = y = 0 [O]. The released elf
  stays where it is, hidden.
* `Elf_Turn` (4521:181a): calls handler[base + 3] (`ElfSt_04/12/20/28`). Called from the cell
  handlers `Cell_P41_V41` (1568:141c; type 41 is only cell 0x7E: an elf moving right in the left
  half of the cell, or left in the right half, turns) and `Cell_P45_V45` (1568:15bc; type 45 =
  cells 0x84 and 0xB7, fast CPU only, §5.4) [O callers, cell types from DS:0eaa]. Hidden elves
  ignore turns and walk through.
* `Elves_ShiftNearby(dx, dy)` (4521:12df): called by `Cell_ConveyLeft/Right` (176a:0019/0110) only
  when the conveyed object is the player, before the player is moved. For each record
  i < `g_elfActiveCount`: if `(player.x − 0x100) < elf.x < (player.x + 0x100)` as **unsigned**
  16-bit compares (no row test) and state ≥ 0x2D: x += dx, y += dy [O]. (With player x < 0x100 the
  lower bound wraps and no elf is shifted.)

### 4.10 What a caught elf triggers

| Elf | Result |
|---|---|
| clue carrier (type A with a question slot) | nets −1 (see §4.8), coins +1, `g_caughtSlot` = slot 1..3, quiz opens (`Quiz_PickQuestion` uses question slot − 1). Right answer: clue word revealed (`Clue_Reveal`), coin reward +2/+1/0 (quiz.c), the elf loses its question. Wrong answer or Esc: it keeps it. The elf then gets up and walks off (state 67 → 64) |
| any other A, B or C | nets −1 (§4.8), coins +1, ZIP animation, released (hidden where it was) |
| any elf with nets == 0 | nothing; it gets up and walks off |
| edge of the net | hop away (state 64), or with chance 1/64 jump out and vanish (73/74) |
| coin creature | never caught; the net makes it run; touching gives +2 coins |

### 4.11 rand() calls in elf code (in call order within one handler) [O]

| Where | When | Use |
|---|---|---|
| `Elves_Spawn` | one per spawned elf, order A, D, B, C | x position |
| `Elf_Think` | an elf gets a free sprite slot | depth 0x28 / 0x3C |
| `Elf_CheckNet` | class 4 (net edge) | escape if `& 0x3F` == 0 |
| `Elf_WalkCommon` (45/46) | every call, after `Elf_CheckNet` | pause if `& 0x0F` == 0 and idle |
| `ElfSt_47` | every call, after `Elf_CheckNet`; second call only if the first is even and the slot idle | resume?; direction |
| `ElfSt_49/50` | only when the slot is idle (after `Elf_CheckNet`); second call only if the first is odd | throw-or-pause; pause 1/16 |
| `ElfSt_51` | only when idle | direction |
| `ElfSt_72` | when the sparkle ends | respawn x |

Elves are processed in record order every tick; each record's calls happen inside its own
`Elf_Think` (sprite rand first, then the handler's).

---

## 5. Layout (segment 1a54)

### 5.1 Data [O]

| DS | Layout | Meaning / example |
|---|---|---|
| 11aa | `u16 g_hidingCells[48]` | cell indices; **slots 0–10 = level 2 (11 places), 13–26 = level 1 (14), 29–47 = level 0 (19)**; slots 11, 12 (cells 0x86, 0x87) and 27, 28 (0xC0, 0xC1) are mirror cells: never chosen by `Layout_PutInSlot`, they get copies through `Layout_MirrorSharedCells` and are cleared and saved with the others. Example: slot 0 = cell 0x77 |
| 0caa / 0faa | `u16 g_cellPicture[256]`, `g_cellContent[256]` | per cell; hiding places: picture 1..count, content 0x80 empty, 6+2k treasure k (k 0..47), 0x66/0x68/0x6A key of level 0/1/2; bit 0x80 = searched |
| 120a | `u16 g_variableCells[9]` (was `g_castleCells`) | cells whose picture changes during play and is saved: 0x26 bonus coins, 0x79 level-2 gate (0x88 its mirror), 0xAE level-1 gate, 0xEF level-0 gate, 0xBC/0xEA net-shop doors, 0x84/0xB7 animated obstacles |
| 121c | `u16 g_variableCellDefaults[9]` | default pictures (example: cell 0x26 → 0x4B) |
| 122e | `u16 g_levelPicData[3][4]` = {picture count, key picture, unused, places} | level 0 = 50, –, 0, 18; level 1 = 38, –, 0, 13; level 2 = 31, –, 0, 10. "places" excludes the key; word [1] is written by the generator; word [2] is never written (0) but saved |
| 1246 | `far attr[3]` → DS:0184 / 0250 / 02ec | 4-byte attribute record per picture, record 0 unused (§5.3) |
| 1252 | `u16 castleLayouts[4][20]` | 5 rows × 4 cells each (example layout 0 starts 0x33, 0x34, 0x33, 0x32) |
| 5f9b…5fb3 | clue tables | M = {0x0F, 0xE0, 0x1F} (5f9b), S = {0, 5, 0} (5fa2), size masks {0xC0, 0x30, 0x0C, 0x03} (5f9e) and shifts {6, 4, 2, 0} (5fa5), byte index B = {0, 2, 2, 1, 3} (5fa9), word base W = {0, 15, 19, 45, 45} (5fae), schemes {0,3,2}, {0,1,2}, {4,1,2} (5fb3) |
| 9296 | `u8 g_clueScheme` | 0..2, not saved (only the generator uses it) |
| 9295 | `i8 g_keyPicture` | scratch: the level's key picture for `Clue_BuildWords` |
| 927a / 927c | `u16` | cell of the level-1 / level-2 key; written by `Layout_PutInSlot`, never read |
| 8f9e | `u8 g_castleLayout` | 0..3, saved at record +0x166 |
| CS 1a54:08a3 | 4 near pointers | switch table of `Castle_PickLayout` (tier 0..3 → count 1..4) |

Attribute record of picture k (bytes b0..b3) as the clue code reads it [O, fmt_data §7.4]:
attribute 0 number = `b0 & 0x0F`; attribute 1 shape = `(b2 & 0xE0) >> 5` (1 triangle, 2 round,
3 square, 4 rectangle); attribute 2 object = `b2 & 0x1F`; attributes 3 and 4 = size, four 2-bit
fields of b1 (attribute 3) or b3 (attribute 4), field f at bits `6−2f`, field value v (1 or 2) gives
size word `45 + 2f + v − 1` (field 0 big/small, field 1 tall/short, fields 2–3 the superlative pairs
[I from the word list order]). The clue word of attribute a is word-table index `W[a] + v − 1`.

### 5.2 `Layout_Generate` (1a54:0003) — the exact order

Called only by `Game_NewSetup` (47c7:0bd6), which first sets: treasures found 0, key state 8,
status prompt 4, shop price 4, 9 question slots 0xFFFF, answer masks 7, clue words 0, found
masks 0, then `Layout_ClearSlots()` and `Cells_ResetVariable()` (§5.4), and after it
`g_slowCpuSaved = g_slowCpu`. Inputs read during generation: total treasures (record +0x0A via
DS:9138), `g_tier` (as loaded from record +0x140 at sign-in), the collected list (record +0x8A via
DS:927e), the showcase treasure `g_shownTreasure` (DS:8fb0, record +0x164).

1. `srand(time(NULL))` — the low 16 bits of `time()` (§5.5). The only `srand` in the program.
2. `g_clueScheme = rand() % 3`.
3. For level = 0, 1, 2: `Layout_PlaceKey(level)` (which ends with `Clue_BuildWords(level·3)`), then
   `Layout_PlaceTreasures(level)`.
4. `Layout_MirrorSharedCells()`.
5. `Castle_PickLayout()` (one `rand()`), `Castle_ApplyLayout()`.
6. `Cells_AdjustForCpu()`.

Total rand calls per layout: 353–1181, mean 653 (simulated over 1352 seeds at tiers 0 and 3) [O
`stats.py`]. After the layout, `Game_Start` (47c7:08b5) continues the same sequence with
`Quiz_LoadQuestions` (the deal, quiz.c), `Music_Play` (one `rand()` on AdLib only, fmt_sound §5.5),
and the first `Scene_Update` then calls `Elves_Spawn` (§4.1).

### 5.3 The placement functions

**`Layout_PutInSlot(picture, content, level)`** (1a54:0059) [O]: repeat `s = rand() & 0x3F` until
s is in the level's slot range (level 0: 29..47, level 1: 13..26, level 2: 0..10) **and**
`g_cellPicture[g_hidingCells[s]] == 0`; then picture and content are stored in that cell. (Level 1
and 2 keys also store the cell in DS:927a / 927c.) Because every level gets exactly as many
placements as it has slots (19/14/11), **every hiding place is filled in every game**; the
random slot choice is a permutation.

**`Layout_PlaceKey(level)`** (1a54:0179) [O]:

1. Repeat: repeat `k = rand() & 0x3F` until `k ≠ 0` and `k ≤ count` (unsigned); reject k (and
   draw again from the start) if the scheme needs an attribute that is 0: scheme 0 and b1 == 0;
   scheme 1 or 2 and (b2 & 0xE0) == 0; scheme 2 and b3 == 0. (Number and object are not checked;
   no picture has them 0 [O `stats.py`].)
2. `g_levelPicData[level][1] = k`; `g_keyPicture = k`.
3. `Layout_PutInSlot(k, 0x66 + 2·level, level)`.
4. `Clue_BuildWords(level·3)` (46ce:079c, quiz.c, documented here because it draws from the same
   sequence): for a = 2, 1, 0 (in that order), attribute `at = scheme_table[scheme][a]`:
   * at < 3: `v = (byte[B[at]] & M[at]) >> S[at]` — no rand;
   * at = 3 or 4: repeat `f = rand() % 4`; `v = (byte & sizeMask[f]) >> sizeShift[f]` until v ≠ 0;
     then `v = 2f + v` (u8);
   * clue word byte `[level·3 + a] = (W[at] + v − 1) | 0x80` (bit 7 = not yet revealed).

   So scheme 0 draws size rands for a = 1 (after the object), scheme 2 for a = 0 (last), scheme 1
   draws none.

**`Layout_PlaceTreasures(level)`** (1a54:029a) [O]:

1. `off = (rand() % count) << 2` (byte offset of a candidate record; the first candidate tried is
   record `(r % count) + 1`).
2. Treasures per level t and tier, from the total T (unsigned compares):
   T ≥ 230 → t = 5, `g_tier` = 3; 115 ≤ T < 230 → t = 4, `g_tier` = 2; 25 ≤ T < 115 → t = 3,
   `g_tier` = 1; T < 25 → t = 2 and **`g_tier` is left unchanged** (it keeps the record value,
   normally 0).
3. Decoys d: level 0: t + 3; level 1: t + 2; level 2: t + 1, or t if T ≥ 381 (0x17D).
4. `want = 2`. For place i = 0 .. places−1 (signed compares on i):
   * if t ≤ i < t + d: want = 1; if i ≥ t + d: want = 0;
   * repeat: if `count·4 > off` then `off += 4` else `off = 4`; compute `m = matches(key, record at
     off)`; until `m == want`;
   * if i < t (treasure): `tries = 0`; `k = rand() % 48`; repeat { tries += 1; k = (k < 47) ? k+1 : 0;
     reject k if `6+2k` equals any of the 48 collected bytes (sign-extended, so 0x80 never matches),
     or equals `g_shownTreasure`, or equals the content of any of the 48 hiding cells } until k is
     not rejected **or tries == 48** (then k is taken anyway: the last candidate equals the first
     rand value); `Layout_PutInSlot(off/4, 6+2k, level)`;
   * else `Layout_PutInSlot(off/4, 0x80, level)`.

`matches(K, C)` with key record K and candidate C (bytes; all comparisons on the masked values):

| Scheme | Number `b0&0x0F` equal | Size | Shape `b2&0xE0` | Object `b2&0x1F` equal |
|---|---|---|---|---|
| 0 | +1 | +1 if `sizeMatch(K.b1, C.b1)` | – | +1 |
| 1 | +1 | – | +1 (rule below) | +1 |
| 2 | – | +1 if `sizeMatch(K.b3, C.b3)` | +1 (rule below) | +1 |

* Shape rule: if K's shape is 0x80 (rectangle), C matches when its shape is 0x80 **or 0x60
  (square)**; otherwise the shapes must be equal.
* `sizeMatch(kb, cb)`: shift kb right by 2 (arithmetic on the signed byte; only the low 2 bits are
  used, so it is a plain field scan) up to 3 times while `kb & 3 == 0`, giving the key's
  **lowest-order non-zero field value**; then shift cb the same way up to 3 times while
  `(kb & 3) ≠ (cb & 3)`; match if `(kb & 3) == (cb & 3)`. So C matches when **any** of its fields has
  the same 2-bit *value* as the key's lowest non-zero field, whichever size dimension that field is.

Consequences [O, reproduce exactly]: the "matches 2 of 3" count uses the key's lowest non-zero size
field and value-only comparison, while the clue word shown is a random non-zero field of the key
(`Clue_BuildWords`), so a treasure's size match can disagree with the displayed size word; a
square counts as a rectangle. Candidates are not removed after use, so when a class has fewer
pictures than places the scan wraps and **the same picture appears in several places of a level**
(122 of 676 sampled tier-0 layouts, 498 of 676 at tier 3) [O `stats.py`]. Every (level, scheme,
valid key) has at least 2 candidates per class, so the loops terminate [O `stats.py`].

### 5.4 Other layout functions [O]

* **`Layout_MirrorSharedCells`** (1a54:06dc): for i = 0..2, copy picture and content of cell
  0xE3+i → 0xFD+i, then 0xAB+i → 0xC0+i, then 0x77+i → 0x86+i. Called by `Layout_Generate` and
  `Game_Restore`. (0x79 → 0x88 copies the level-2 gate picture; cells 0xC2, 0xFD–0xFF are not saved
  and are restored only through this copy.)
* **`Layout_UnmarkSharedCells`** (1a54:078b): content &= 0x7F for cells 0xFD–0xFF, 0xC0–0xC2,
  0x86–0x88. Called by `PlrSt_48` (end sequence). Quirk: empty places (0x80) become **0x00**; seen in
  `USERS_after_castle_climb_Ada.DAT` (slots 11, 12, 27, 28 hold 0) [O].
* **`Layout_ClearSlots`** (1a54:07d6): all 48 cells of DS:11aa: picture 0, content 0x80. Called by
  `Game_NewSetup` and by `Scene_Update` at the end of a game.
* **`Cells_ResetVariable`** (was `Castle_ResetCells`, 1a54:085f): the 9 cells of DS:120a get
  picture = default (DS:121c), content 0x80. Called by `Game_NewSetup` and `Scene_Update` (end of
  game). Their pictures are saved at record +0xBB+48 and restored by `Game_Restore`, so the bonus-coin
  spot (cell 0x26) is once per game, also across save/restore [O; corrects ARCHITECTURE §4.4/§6].
* **`Castle_PickLayout`** (1a54:0892): if `g_tier` ≤ 3 (unsigned): `g_castleLayout` = tier + 1;
  then `g_castleLayout = rand() % g_castleLayout` (one rand even at tier 0, where the result is 0).
  With a tier > 3 the old value is the divisor (a zero would fault) — unreachable in normal play.
* **`Castle_ApplyLayout`** (1a54:08dc): `p = g_castleLayout·20`; for rows r = 0..4 and columns
  c = 0..3: `g_cellPicture[0x32 + 16r + c] = castleLayouts[p++]` (cells 0x32–0x35, 0x42–0x45, …,
  0x72–0x75). Contents untouched. Called by `Layout_Generate` and `Game_Restore`.
* **`Cells_AdjustForCpu`** (1a54:0803), called by `Layout_Generate` and `Game_Start`:
  * fast CPU (`g_slowCpu` == 0): picture[0xBF] = 0x41; picture[0x7E] = 0x37; if picture[0x84] == 0x2E:
    picture[0x84] = 0x2F and type[0x84] = 0x2D; the same for cell 0xB7;
  * slow CPU: picture[0xBF] = 0x40, picture[0x7E] = 0x2E, picture[0x84] = picture[0xB7] = 0x2E,
    type[0x84] = type[0xB7] = 0x1C (static cell type 28 instead of the animated type 45).

### 5.5 The seed [O]

`time()` (4d1a:0050) = `dostounix(getdate, gettime)`: local wall-clock seconds since 1970 as if
UTC, plus `timezone` (18000 for the Turbo C default TZ "EST5EDT", used when no TZ variable is set),
minus 3600 when Turbo C's built-in US daylight-saving rule says DST (`__isDST` 4d63:01e5). Only the
low 16 bits reach `srand`. Check: the seeds found in §5.6 for Ada's two games, 0x1EE2 and 0x2B40,
correspond to DOS clock 15:32:50 and 16:25:36 on 2026-10-18 (EDT), exactly the timestamps of the
USERS.DAT written in the same second (`USERS_after_signin_Ada.DAT`: the sign-in save just before
`Game_Start`; `USERS_after_castle_climb_Ada.DAT`: `Game_End` just before the next `Game_Start`).

### 5.6 Verification against USERS.DAT [O]

`layout_sim.py` implements §5.2–5.4 from the EXE tables; `brute.py` tries all 65 536 seeds
(pre-filtering on the level-0 key) and compares all 48 pictures, contents (bit 7 ignored),
the 9 clue words (bit 7 ignored), the 3 key pictures (+0x124) and the castle layout (+0x166):

| Record | Seed | Result |
|---|---|---|
| shipped `original/USERS.DAT`, "Rascal" | 0xA954 | all fields equal (scheme 0) |
| `USERS_after_exit_level2_Ada.DAT`, Ada game 1 | 0x1EE2 | all fields equal (scheme 0) |
| `USERS_after_exit_post_castle_Ada.DAT`, Ada game 2 | 0x2B40 | all fields equal (scheme 0) |

Each is the only seed of 65 536 that matches. All three are tier 0, total 0, nothing collected, so
the tier formula, the higher-tier decoy counts and the id exclusion are not yet confirmed by a
capture; request R1 asks for two crafted tier-2/3 players.

Cross-check of the sequence after the layout (`deal_check.py`): continuing the same generator after
the layout's last call and dealing 9 questions the way `Quiz_Deal` does for a player without
history (every slot falls back to status 0; slot 8 down to 0, `rand() % 63`, re-drawn on a
duplicate) reproduces Rascal's 9 dealt questions in the saved order, and the same set as Ada's game 1
(her record holds them reordered by `Quiz_ReorderSlots`). So nothing draws from `rand()` between
`Layout_Generate` and the deal [O]. (Ada's game 2 has history, which needs the full deal of quiz.c.)

### 5.7 Pitfalls of the generator

* Keep the exact order: scheme, then per level key (k loop, slot loop, clue size loops) before that
  level's treasures; the castle rand comes after all levels.
* `rand() & 0x3F` loops (key index, slot) are rejection loops, not `% n`.
* The treasure-id loop compares against *all* 48 hiding cells, including later levels' slots (still
  0x80 from `Layout_ClearSlots`) and the mirror slots.
* `g_tier` is only raised here (and loaded from the save); a total below 25 leaves it.
* With 40 or more treasures collected, fewer than t·3 ids are free and the 48-try fallback places
  collected ids or duplicates (crafted player `tierb` of R1 exercises this).

---

## 6. New Player Hints and game messages (segment 49a6)

### 6.1 Data [O]

| DS | Layout | Meaning |
|---|---|---|
| 923e | `u8 g_hintFlags[15]` | per hint id: 1 = waiting, 0 = due, 0x80 = shown or cancelled. `setmem(…, 15, 1)` at sign-in (`Users_LoadPlayer` 47c7:0bc8) and by Options → New Player Hints (43cb:0368) |
| 9224 | `u16 g_hintsOn` | 1 if total < 5 at sign-in; Options sets it |
| 913f | `u16 g_hintTimerReset` | 1 from `Game_Start` and from the Options switch (answer value) |
| c6d8 | u16 | hint timer base, always 0 after a reset |
| c6d0 / c6ce | u16 | hint 3 start second / level |
| c6d4 | u16 | key state copied at a reset; never read |
| c6d6 | u16 | saved status prompt |
| c6da | u16 | mouse buttons at the moment the bubble opened |
| 90f8 | `g_statusPrompt` | status-bar prompt frame 60 + v/2; 6 = SPRITES 63 "Press Enter to go on" |
| 90f4 / 9143 | u16 / u16 | mouse buttons now / previous tick (`Mouse_Poll`) |
| 9238 | `g_inputReady` | 0 while a frame-side handler reads `g_cmd`; `Input_Poll` (interrupt time) then queues one command in DS:90f6 |
| 7ca3 | `far text[37]` | message texts (ids in fmt_data §7.3) |
| 7d37 | `u16[7]` | rank thresholds 5..300 (same values as DS:629d) |
| 7d45 | `i8[8]` | start message per rank: 23..30 |
| 7d4d | `i16[12]` | digit patch offset per rank 0..7, then end messages 8..11 (−1 = no patch; example: rank 3 → 112) |
| 7d65 | `i16[8]` | plural-letter offset per rank |
| 7d75 | `i8[4]` | end messages 31..34 (none / one / some / all) |
| 7d79 | `u16 g_crownMsgToggle` | initial 1 |
| 911c | `g_messagePending` | 1 start message due (Game_Start), 2 end message due (Scene_Update after `Game_End`), 3 second crown message |
| 9232 / 9289 | u16 / u16 | treasures found / tier of the game just finished (`Game_End`) |

### 6.2 `Hints_Update` (49a6:000f) — per frame [O]

Runs only if `g_hintsOn` == 1, (`g_uiMode` & 0xEF) == 0, DS:922e == 0 (no overlay redraw
pending), DS:926c == 0 (no teleport/search transition) and `g_transition` == 0.

1. With interrupts off: `cmd = g_cmd`; `g_inputReady` = 0.
2. If `g_hintTimerReset` == 1: `g_seconds` = 0, c6d8 = 0, `g_hintTimerReset` = 0, c6d4 = key state,
   c6d0 = 0.
3. **No bubble open** (`g_uiMode` & 0x10 == 0): `Hints_CheckTriggers()`; `id = Hints_PickNext()`;
   if id ≠ −1: `g_uiMode` |= 0x10; if not a slow CPU: DS:922e = 2 and `Screen_DrawOverlay()`;
   `Flutter_Show(id)`; c6d6 = `g_statusPrompt`; `g_statusPrompt` = 6; if id == 13 and buttons ≠
   previous buttons: previous = 0; c6da = buttons.
4. **Bubble open**: close if `cmd` == 8 (Enter) or 0 (Esc), or the mouse buttons changed this tick
   while c6da == 0. Closing: `g_cmd` = −1, `g_fullRedraw` = 1 (the play-field is redrawn, which
   removes the bubble), `g_uiMode` &= ~0x10, `g_statusPrompt` = c6d6. Else, if buttons == 0 and
   c6da == 1 (a button held when the bubble opened has been released): c6da = 0, previous = 0.
5. `g_inputReady` = 1.

While the bubble is open the game is paused: `Elves_Update` and `Player_Update` do nothing
(`g_uiMode` ≠ 0), `Anim_Tick` runs slot 0 only, `g_seconds` stops (with the wrap quirk of §2.1),
the frame loop draws slot 0 only, and commands other than Enter/Esc have no effect here (F1 does
nothing, as REFERENCE_CAPTURES §5.2 saw) [O; the menu side is in `code_ui.md`].

### 6.3 `Hints_CheckTriggers` (49a6:0121) and `Hints_PickNext` (49a6:022b) [O]

`g_keys` (DS:9105) are the held keys of the interrupt handler (1 up, 2 down, 4 left, 8 right,
0x10 net, 0x20 jump, 0x40 drop coin); `g_area` = DS:910b (5 = start area at a new game, 0 = level-0
path [I]). Comparisons with `g_seconds` are unsigned. Each block runs only while the flag has bit 7
clear:

* hint 1: if neither Left nor Right is held and area == 5: due when `g_seconds ≥ c6d8 + 3`;
  otherwise (a walk key held, or another area) → 0x80 (cancelled for good).
* hint 2: if Net is held → 0x80; else due when `g_seconds ≥ c6d8 + 31` and area == 0.
* hint 3: if c6d0 == 0: count the revealed clue words of `g_level` (bit 7 clear); if ≥ 2: c6d0 =
  `g_seconds` + 1, c6ce = level. Else if the drop-coin bit is held or the level changed → 0x80;
  else due when `g_seconds ≥ c6d0 + 5`, i.e. 6 hint seconds (7.6 s) after the second word was
  first seen.
* hint 5: due when slot 1's previous animation (DS:8ff8) is 0x16 or 0x19 (the net-retract
  scripts) and nets == 0.

`Hints_PickNext`: the **highest** id 14..0 whose flag is 0 is returned and its flag set to 0x80.
If none is due and every flag has bit 7 set, `g_hintsOn` = 0 — which never happens, because flags
0, 10, 11, 12 and 14 stay 1 forever (§6.6).

### 6.4 All hint ids [O]

| Id | Flag | Armed by | Condition |
|---|---|---|---|
| 0 | 923e | – | never armed; text 0 is the first start-of-game message (§6.5) |
| 1 | 923f | 49a6:0121 | 3 hint seconds in the start area without walking |
| 2 | 9240 | 49a6:0121 | 31 hint seconds without a net swing, shown in area 0 |
| 3 | 9241 | 49a6:0121 | 2 clue words of the level known, no coin dropped for 5 more seconds |
| 4 | 9242 | `Search_Update` 4a03:01c0 | a treasure found (skipped if the new count equals the HUD's cached count DS:9259, a corner case right after a forced HUD refresh) |
| 5 | 9243 | 49a6:0121 | a net swing retracted with 0 nets |
| 6 | 9244 | `PlrSt_10` 18cf:0a94/0af4 | drop-coin key with no coins and no nets |
| 7 | 9245 | `PlrSt_10` 18cf:0a88/0ae8 | drop-coin key with no coins |
| 8 | 9246 | `Cell_P31` 1568:0ff3, `Cell_P39_V39` 1568:13f0 | Up held (DS:923a bit 0) at the level-0 or level-1 gate without its key |
| 9 | 9247 | `Search_Update` 4a03:022e/025d/028c | a key found |
| 10, 11, 12 | 9248–924a | – | never armed |
| 13 | 924b | `Gameover_UpdateMessages` 49a6:052a | an end-of-game message shown |
| 14 | 924c | – | never armed (placeholder text) |

All writers set the flag to 0 only if bit 7 is clear, so each hint shows at most once per sign-in
(or per Options switch-on). The EGA captures show hints 1 (`009`), 2 (`025`, outside the
clubhouse), 3 (`045`, with two clue words on the panel), 9 (`050`), 5 (`072`) and 13 (`104`, right
after the end message `103`), as the triggers predict [O captures; REFERENCE_CAPTURES §4.1].

### 6.5 `Gameover_UpdateMessages` (49a6:026f) — per frame [O]

Runs only if (`g_uiMode` & 0xFB) == 0.

**Message open** (`g_uiMode` & 4): read `cmd` as in §6.2; close on Enter/Esc or a button change
with c6da == 0: `g_messagePending` −= 1, `g_cmd` = −1, DS:922e = 1 (overlay redraw),
`g_uiMode` &= ~4, `g_statusPrompt` = c6d6, DS:923a = 0. The button-release rule is as in §6.2.

**No message open**: show one if `g_messagePending` ≠ 0, DS:922e == 0, DS:926c == 0,
`g_transition` == 0 and (`g_area` ≠ 5 or games played == 0 or pending == 2) — so after the first
game the start message waits until the player leaves the start area. Then:

* pending == 1 (start of game): rank = number of DS:7d37 thresholds ≤ total (0..7). Message =
  0 (Flutter introduction) if rank == 0 and games played == 0, else `7d45[rank]` (23 + rank).
  If `7d4d[rank]` ≠ −1: n = `7d37[rank]` − total; `itoa(n)` → text[off] = first digit, text[off+1]
  = second digit or a space; text[`7d65[rank]`] = ' ' if n == 1 else 's'. Then
  `g_gameInProgress` (DS:9205) = 1.
* pending == 2 (end of game): if 300 ≤ total ≤ 314: if `g_crownMsgToggle` ≠ 0 → message 35,
  toggle = 0, **pending = 3**, the 8 name bytes of the player record (NUL → space) are copied to
  DS:7bb2 (inside text 35); else message 36, toggle = 1. Patch index 9 (none).
  Otherwise with N = treasures of the last game: N == 0 → message `7d75[0]` (31), index 8; N == 1 →
  32, index 9; N ≥ (last tier + 2)·3 → 34 ("all"), index 11; else 33, index 10. If `7d4d[index]` ≠ −1
  the two digits of N are patched as above. Then hint 13 is armed.
* both: `g_uiMode` = 4 (assignment), overlay redraw as in §6.2, `Flutter_Show(message)`, c6d6 =
  prompt, prompt = 6, c6da = buttons.

Resulting sequences: end of a normal game: end message (31–34), then hint 13 if hints are on, then
the next game's rank message once the player walks out of the start area. Champion end (total
300–314): message 35, message 36, then rank message 30. The patches modify the text in DGROUP and
persist for the session (the engine should patch a copy).

### 6.6 Verdict on the unarmed hints [O]

No code writes flags 0, 10, 11, 12 or 14: the only references to DS:923e are the two `setmem`
calls and `Hints_PickNext`; a byte search of all code segments finds no other operand 0x923e,
0x9248, 0x9249, 0x924a or 0x924c. Texts 10 (on your way), 11 (slid down a tunnel), 12 (climb or
jump ladders) and 14 ("msg 14") are therefore never shown, and text 0 is shown only as the first
start message. This agrees with REFERENCE_CAPTURES §5.7 (no hint after the level-3 slide). A side
effect: the "all hints done → hints off" branch of `Hints_PickNext` is dead.

### 6.7 `Flutter_Show(id)` (49a6:0565) and `SoundPort_Out` (49a6:0001) [O]

* `Flutter_Show`: draws SPRITES frame 71 (the cloud, 264 × 106 at (24,17), its header offsets are
  the screen position) with flag 0x40 (colour 0 transparent) at (0,0); if `g_uiMode` has 0x10 or 4,
  also frame 72 (Flutter, 38 × 23 at (268,107)); text colour DS:9215 = 0xFFFF (black);
  `Text_Draw(0x14, 0x22, text[id])` (x in 2-px units = 40 px, y 34; texts may move themselves with
  `~XXXYYY`); `g_dirty` = 1. The quiz praise/oops messages call it with `g_uiMode` = 2 (cloud
  without Flutter).
* `SoundPort_Out(AL)`: `out(DS:656c, AL)` with a short delay jump; part of the sound driver (used by
  491b:025d, 033d, 05b8, 0607), see fmt_sound.md.

---

## 7. Function index

| Address | Name | Size | § |
|---|---|---|---|
| 4521:000c | ElfSt_01 | 80 | 4.4 |
| 4521:005c | ElfSt_05 | 80 | 4.4 |
| 4521:00ac | ElfSt_02 | 145 | 4.4 |
| 4521:013d | ElfSt_04 | 135 | 4.4 |
| 4521:01c4 | ElfSt_09 | 57 | 4.5 |
| 4521:01fd | ElfSt_13 | 57 | 4.5 |
| 4521:0236 | ElfSt_10 | 116 | 4.5 |
| 4521:02aa | ElfSt_12 | 103 | 4.5 |
| 4521:0311 | ElfSt_17 | 57 | 4.6 |
| 4521:034a | ElfSt_21 | 57 | 4.6 |
| 4521:0383 | ElfSt_18 | 116 | 4.6 |
| 4521:03f7 | ElfSt_20 | 103 | 4.6 |
| 4521:045e | ElfSt_25 | 51 | 4.7 |
| 4521:0491 | ElfSt_28 | 17 | 4.7 |
| 4521:04a2 | ElfSt_45 | 34 | 4.4 |
| 4521:04c4 | ElfSt_46 | 34 | 4.4 |
| 4521:04e6 | Elf_WalkCommon | 95 | 4.4 |
| 4521:0545 | ElfSt_47 | 147 | 4.4 |
| 4521:05d8 | ElfSt_49 | 179 | 4.5 |
| 4521:068b | ElfSt_50 | 179 | 4.5 |
| 4521:073e | ElfSt_51 | 153 | 4.5 |
| 4521:07d7 | ElfSt_52 | 126 | 4.5 |
| 4521:0855 | ElfSt_53 | 274 | 4.5 |
| 4521:0967 | ElfSt_54 | 93 | 4.5 |
| 4521:09c4 | ElfSt_55 | 106 | 4.5 |
| 4521:0a2e | ElfSt_56 | 126 | 4.5 |
| 4521:0aac | ElfSt_57 | 274 | 4.5 |
| 4521:0bbe | ElfSt_58 | 93 | 4.5 |
| 4521:0c1b | ElfSt_59 | 106 | 4.5 |
| 4521:0c85 | ElfSt_60 | 25 | 4.6 |
| 4521:0c9e | ElfSt_61 | 25 | 4.6 |
| 4521:0cb7 | ElfSt_70 | 25 | 4.7 |
| 4521:0cd0 | ElfSt_71 | 46 | 4.7 |
| 4521:0cfe | ElfSt_72 | 149 | 4.7 |
| 4521:0d93 | CoinElf_Check | 136 | 4.7 |
| 4521:0e1b | ElfSt_73 | 83 | 4.8 |
| 4521:0e6e | ElfSt_74 | 63 | 4.8 |
| 4521:0ead | Elves_Update | 437 | 4.2 |
| 4521:1062 | Elf_Think | 518 | 4.3 |
| 4521:1268 | Elf_Release | 119 | 4.9 |
| 4521:12df | Elves_ShiftNearby | 117 | 4.9 |
| 4521:1354 | ElfSt_64 | 46 | 4.8 |
| 4521:1382 | ElfSt_62 | 96 | 4.8 |
| 4521:13e2 | ElfSt_63 | 29 | 4.8 |
| 4521:13ff | ElfSt_65 | 96 | 4.8 |
| 4521:145f | ElfSt_66 | 29 | 4.8 |
| 4521:147c | Elf_NetCatch | 303 | 4.8 |
| 4521:15ab | ElfSt_03 | 78 | 4.8 |
| 4521:15f9 | ElfSt_68 | 30 | 4.8 |
| 4521:1617 | ElfSt_69 | 49 | 4.8 |
| 4521:1648 | Elf_CheckNet | 466 | 4.8 |
| 4521:181a | Elf_Turn | 20 | 4.9 |
| 4521:182e | Elves_Spawn | 681 | 4.1 |
| 1a54:0003 | Layout_Generate | 86 | 5.2 |
| 1a54:0059 | Layout_PutInSlot | 288 | 5.3 |
| 1a54:0179 | Layout_PlaceKey | 289 | 5.3 |
| 1a54:029a | Layout_PlaceTreasures | 1090 | 5.3 |
| 1a54:06dc | Layout_MirrorSharedCells | 175 | 5.4 |
| 1a54:078b | Layout_UnmarkSharedCells | 75 | 5.4 |
| 1a54:07d6 | Layout_ClearSlots | 45 | 5.4 |
| 1a54:0803 | Cells_AdjustForCpu | 92 | 5.4 |
| 1a54:085f | Cells_ResetVariable (Castle_ResetCells) | 51 | 5.4 |
| 1a54:0892 | Castle_PickLayout | 66 | 5.4 |
| 1a54:08dc | Castle_ApplyLayout | 71 | 5.4 |
| 49a6:0001 | SoundPort_Out | 14 | 6.7 |
| 49a6:000f | Hints_Update | 274 | 6.2 |
| 49a6:0121 | Hints_CheckTriggers | 265 | 6.3 |
| 49a6:022b | Hints_PickNext | 68 | 6.3 |
| 49a6:026f | Gameover_UpdateMessages | 756 | 6.5 |
| 49a6:0565 | Flutter_Show | 110 | 6.7 |

---

## 8. Corrections to ARCHITECTURE.md (for the next synthesis)

1. Nets (§5.9, §5.10): a net is used per catch resolution; catches resolved in the same tick share
   one (§4.8). Not "first elf per swing".
2. Elf dust (§5.10, §8.2): the dust moves 2 px per tick, vanishes after 160 px if it hits nothing,
   and only flies back (with the coin) after a hit; netting does not protect the player; right-thrown
   dust is only avoided by jumping left (§4.5).
3. "Coin elf" (§5.10, §8.2): a gold coin that grows legs; appears only at a view edge when coins < 4
   and nets < 5; netting makes it run away for good; touching gives +2 after the sparkle; it then
   respawns at a random x of the level.
4. DS:5d34 columns are A, B, C, D; spawn order A, D, B, C (§4.1).
5. The pause chance of walkers is 1/16 per walk cycle, not per step; the walker resumes away from
   the player after a dodge (§4.4).
6. Net zones are mirror images about P−1 byte; exact bounds in §4.3.
7. DS:11aa holds 44 hiding places plus 4 mirror cells (§5.1); every place is filled in every game.
8. DS:120a ("castle cells") are the 9 variable-picture cells; the bonus-coin picture is saved
   (§5.4).
9. The New Player Hints never switch themselves off (§6.6); the champion end messages come in the
   order 35, 36 (§6.5).
10. Layout rule details: tier not lowered below 25; square counts as rectangle; size matched by
    value; repeated pictures (§5.3).

---

## 9. Porting notes

### 9.1 Suggested engine structure (`src/dos/game/`)

* `elves.h/.cpp` — `struct Elf { u16 x, y; i16 dx, dy; u16 base, state, question, slot; }` × 15 plus
  the module globals of §3.2 as members of an `ElfSystem` (keep `animSel` and `callCounter` as
  persistent members: their stale values matter). `spawn(int carriers)`, `update()` (§4.2),
  `think(Elf&)` and a `switch` (or a 76-entry table) on `state` that reproduces §3.4 including the
  no-op entries; `turn(Elf&)` (for the cell handlers), `shiftNearby(dx, dy)` (for the conveyors).
  Original addresses in comments.
* `layout.h/.cpp` — `generate(Rng&, const PlayerRecordView&)` returning the 256-cell picture/content
  changes plus clue words, key pictures and castle layout; `putInSlot`, `placeKey`,
  `placeTreasures`, `matches`, `sizeMatch`, `mirrorShared`, `unmarkShared`, `clearSlots`,
  `resetVariableCells`, `pickCastle`, `applyCastle`, `adjustForCpu`. `Clue_BuildWords` belongs to
  quiz.c but must be called from `placeKey` with the same `Rng`.
* `hints.h/.cpp` — `HintSystem { u8 flags[15]; … }` with `updateHints()` and `updateMessages()`
  called from the frame loop in the original order (messages before hints), `checkTriggers()`,
  `pickNext()`, `showFlutter(id)`; text patching on a session copy of the DGROUP texts.
* All tables (DS:5d34, 5d54, 5d6c order, 11aa, 120a, 121c, 122e, 1246 → attribute records, 1252,
  5f9b–5fb3, 7ca3, 7d37–7d79) come from the unpacked SST.EXE at run time through
  `dos/data/dgroup_offsets.h`; the state-handler table only fixes the state numbering (implement
  the handlers in code).
* One `BorlandRand` instance for the whole DOS game; `srand(low16(time()))` only in
  `Layout_Generate`. For replay tests expose a "seed override"; for matching a DOSBox-X session use
  the §5.5 formula. Do not draw extra rands anywhere (e.g. no rand for engine-side effects).

### 9.2 All rand() consumers of the program (one shared generator) [O callee lists]

| Consumer | When | Calls |
|---|---|---|
| `Title_Run` 4ba4:0434 | title/attract frames (sparkles) | `& 0x7F`, then one more when 0 |
| `Layout_Generate` + `Clue_BuildWords` | new game | `srand`, then 353–1181 (§5.2) |
| `Quiz_Deal` 46ce:0b6a | new game, right after the layout | ≥ 9 |
| `AdLib_PlayTune` 4ac9:0002 | every `Music_Play` on the AdLib device only | 1 |
| `Elves_Spawn` | every scene load of a mountain level | 9–15 |
| elf handlers, `Elf_Think`, `Elf_CheckNet` | game ticks (§4.11) | 0–2 per elf per tick |
| `Castle_CreatureSpawn` 176a:159e | castle wall, `Cells_UpdateVisible` (after `Elves_Update` in the tick) | see code_cells.md |
| `Quiz_PickQuestion`, `Quiz_Correct`, `Clue_Reveal`, `Quiz_Wrong` | question screen (per frame) | R(6), R(4), R(6), R(4) |
| `Frame_Present` 130f:01c2 | every full-screen block dissolve (DS:8fb2 ≠ 0) on a fast CPU | 700 (350 random rectangles) |

Within a tick the order is `Player_Update` (no rand), `Elves_Update` (record order), then
`Cells_UpdateVisible`. A replay therefore matches only if the engine reproduces every consumer,
including the dissolve's 700 calls and the AdLib choice.

### 9.3 Things that depend on timing

* Everything in `elves.c` is per game tick (47.3 Hz) and only on ticks where `g_stepDone` was 0 at
  the start; the engine's DOS runtime must call `Elves_Update` from the virtual tick inside the
  same gate as `Player_Update`, after `Anim_Tick`, before `Cells_UpdateVisible`.
* Speeds are per tick: hidden drift 1 px/tick; dust 2 px/tick; escape sprite 2 px/tick; walking
  speed comes from the scripts (8 px per 7-tick frame). On a slow frame rate all of these slow down
  together (one tick per frame), as in the original.
* The search freeze length depends on the frame rate (`g_searchTimer` counts frames).
* Hints and messages are evaluated per frame, but their timers count ticks (`g_seconds`), paused
  whenever `g_uiMode` ≠ 0, with the byte-wrap quirk (§2.1).
* `Input_Poll` runs at interrupt time; the `g_inputReady` handshake means at most one command is
  queued while a hint/message handler reads `g_cmd`. In the engine (single thread) read the command
  once per frame and keep the one-deep queue semantics.

### 9.4 Pitfalls

* `Elf_Think`'s class is a module global read by the handlers of the same elf; `Elf_Release` sets
  it to 1. `ElfSt_25` relies on class == 2 meaning "got a slot this tick".
* A hidden elf's dx persists and is never cleared out of view; spawn does not clear dx or the sprite
  slot (§4.1).
* The slot allocator only accepts slots whose current **and previous** animation ids are 0; only
  `Elf_Release` (`Anim_Clear`) frees a slot.
* Unsigned versus signed compares as given (net zones and dust test signed; `Elves_ShiftNearby`,
  sprite-count and tier tests unsigned).
* `Elves_Update` keeps processing the remaining elves in the tick in which a carrier opens the quiz.
* Only one `Sfx_Play` priority rule applies to the script sound effects (lower id wins).
* The layout fills all 44 places; any refactoring that skips the rejection loops changes the rand
  sequence and therefore every later random event.

---

## 10. Open points

* O1. Capture request R1 (crafted tier-2/3 players) to confirm the higher-tier branches of the
  generator; R2 (dust hit/miss), R3 (coin creature), R4 (unarmed hints on level 1), R5 (slow-CPU
  elf limits) — `extracted/scratch/dos_requests/elves.md`.
* O2. Two catches in one tick costing one net (§4.8) is from the code only; no capture.
* O3. Whether a sprite slot can be leaked by a scene change while an elf is visible (§4.1); expected
  rare. If wave 4 sees a frozen elf sprite after a level change, this is the cause.
* O4. The meaning of size fields 2–3 (superlative words) is inferred from the word list order.
* O5. The escape (states 73/74) visuals: the child sprite moves down; whether this reads as a jump
  out of the net or a fall was not captured.
