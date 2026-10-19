# SST.EXE (DOS 1990) runtime: timer, main loop, start-up, keyboard and input

Scope: every function of the modules `130f` (main.c, 14 functions), `1460` (init.c, 21), `43a0`
(kbd.asm, 4) and `4a85` (input.c, 6), plus the timer routines of segment `1000` (`Timer_Install`,
`Timer_Remove`, `Timer_ISR`) and, as context for the options, `Hw_Detect` / `Video_DetectAdapter`
(1000:04a1 / 05bf). 48 functions are specified here, 50 counting the two context routines.
Module boundaries: the HUD *drawing* details belong to `code_ui.md` (the drawing calls are listed
here so the order is complete), animation scripts and `Player_Move` to `code_player.md`, cell
handlers to `code_world.md`, elves and hints to `code_elves_layout_hints.md`.

Evidence: `re/dos/ghidra_export/` (decompiled and `disassembly.asm`), raw bytes of
`extracted/SST_unpacked.EXE`, and run-time measurements in DOSBox-X (EGA, `cycles=fixed 6000`)
with two new scratch tools in `extracted/scratch/dos_w1_runtime/`:
* `savmem.py` reads guest RAM out of a DOSBox-X save state (the `.sav` is a zip; its `Memory`
  entry is a 0x88-byte header followed by physical RAM; DGROUP is found by the Turbo C copyright
  at DS:0004). Every "[O, RAM]" value below was read this way.
* `framelog.py` patches a copy of a save state with three tiny far-called stubs in free DOS
  memory (8000:0000..8800:7FFF, above the game's MCB): one appends `g_ticks` per main-loop
  iteration, one appends a record (ticks, player x/y, camera x/y, player state, `g_keys`, coins)
  per executed logic tick, one appends `g_ticks` per tick blocked by DS:910d. Behaviour is
  otherwise unchanged. States `extracted/captures/dos/savestates/r2/fl_*` carry it.

Notation: **[O]** observed (code, bytes, RAM, captures), **[I]** inferred. `SSSS:OOOO` are Ghidra
addresses (file offset `(SSSS-0x1000)*16 + OOOO + 0x21F0` of `SST_unpacked.EXE`); `DS:XXXX` is
DGROUP (segment `4d94`, file offset `0x3FB30 + XXXX`). In DOSBox-X the program is loaded so that
Ghidra segment S runs at S − 0x7DC (DGROUP at 0x45B8) [O, RAM]; a port never needs this.
Coordinates: x in **bytes** (2 pixels) where the code uses bytes, y in pixel rows; world
coordinates in pixels. "Tick" = PIT interrupt (236.70 Hz) unless "game tick" (47.34 Hz) is said.
"Frame" = one iteration of the main loop (`main` 130f:0037..00d1), whether or not it presents.

---

## 1. The time base

### 1.1 Two clocks [O]

```
 PIT channel 0, divisor 0x13B1 = 5041      1193181.67 / 5041 = 236.6974 Hz   (4.2248 ms)
   every interrupt:        g_ticks DS:3D2B (u32) += 1
                           Sound_Tick 491b:0004 if (music.status & DS:37CE) | sfx.status != 0
   every 13th interrupt:   chain to the BIOS INT 8 handler (18.2075 Hz, keeps the DOS clock)
   every 5th interrupt:    Game_Tick 130f:00FE, re-entrancy guarded    47.3395 Hz (21.124 ms)

 main loop (main 130f:0007): as fast as the CPU allows, no vertical-retrace wait, no limiter
   except a 1000-iteration busy loop in frames that drew nothing (Frame_Present 130f:01C2)
```

`Game_Tick` runs **inside the timer interrupt** with interrupts re-enabled; the main loop may be
anywhere in a frame when it runs. The two meet through latches: `Frame_Begin` (130f:025A) copies
the tick-side state (camera, scene wanted, redraw requests) into frame-side copies under `CLI`,
and the **step flag** `g_stepDone` DS:910D limits the logic to one visible step per frame.

### 1.2 Step gating (DS:910D) [O]

* Cleared by `Frame_Begin` at the start of every frame (130f:02BE, inside the CLI section).
* Set by `Anim_Tick` (4306:0298) when any animation script advanced to a new frame in this game
  tick, and by `Player_Move` (18cf:175F: `910D |= cameraDX | cameraDY`) when the camera moved.
* Read by `Game_Tick` (130f:0108): if set, the tick runs only `Anim_Tick` (whose scripts then do
  not advance, they wait with their delay counter at 0) and `Input_Tick`; the player, elves,
  visible cells and the mouse are not updated in that tick. `Anim_Tick` also samples the flag at
  its own entry (4306:004F), so all scripts of one tick advance together.

Purpose [I, from the code]: `Scroll_Update` draws exactly one new tile column (or row) per frame
and sprites are erased/redrawn once per frame; the gate guarantees that the camera moves at most
one 8-px step and every script at most one picture between two drawn frames. Consequence: on a
machine that draws fewer than ~47 frames per second the whole game slows down with the frame
rate; on a faster machine logic runs at exactly 47.34 Hz.

### 1.3 What DOSBox-X does (reference measurements) [O, frame logger]

All with `cycles=fixed 6000`, EGA (`tools/dosbox_tmdos.conf`), the configuration of every reference
capture:

| Situation | Loop frames / s | Frame cost | Blocked game ticks |
|---|---|---|---|
| nothing drawn (hint bubble, menu, Yes/No box, quiz screen, rank screen idle) | 1690-1698 | 0.6 ms (the 1000-iteration delay) | 0 |
| walking with scrolling | 820-880 | mostly 0-1 PIT ticks; scroll frames 6-8 PIT ticks (25-34 ms) | 1 in 5 s |
| HUD coin count-up (one coin per frame) | 73 | 3-4 PIT ticks (12.7-16.9 ms) | 0 |
| search (37 frames, partial play-field redraw each) | 55 | 4-5 PIT ticks (17-21 ms) | 4 (behind the final 97 ms frame) |
| CHAMPION flash on the rank screen | ~914 | ~1.1 ms | 0 |
| full play-field redraw / door dissolve | 1 frame of 38 ms / 1 frame of 127 ms | | 0-4 |

Game ticks ran at 47.34/s in every log; DS:910D blocked a tick only behind frames longer than
21 ms. One Game_Tick overrun was seen: at the level 0 -> 1 boundary during the trunk climb a tick
ran ~4 ticks long, so the re-entrancy guard delayed the next game ticks (`r2/fl_climb`, climb step
11 ticks instead of 7) [O]. **So in DOSBox-X the logic is effectively ungated: one logic step per
game tick, while the per-frame features run at the CPU-bound loop rate above.** §10 lists the
consequences per feature and PORTING_NOTES.md the model the engine should use.

---

## 2. Segment 1000: timer, hardware detection

### 2.1 `Timer_Install` 1000:03C5 (far, no args) [O]

1. `DS:3D34` (game-tick countdown, byte) = 4; `DS:3D33` (BIOS-chain countdown, byte) = 12;
   `g_inGameTick` DS:1309 = 0.
2. Saves the INT 08h vector (INT 21h AH=35h) into code-segment words `CS:049D` (offset) /
   `CS:049F` (segment).
3. `CLI`; installs `Timer_ISR` 1000:0428 as INT 08h (INT 21h AH=25h, DS = 1000).
4. PIT: port 43h = 36h (channel 0, lo/hi, mode 3), port 40h = 0xB1 then 0x13 (divisor 0x13B1);
   port 43h = 0B6h (channel 2, lo/hi, mode 3: prepares the speaker channel for the sound driver).
5. `STI`.

Called by `main` after `Kbd_Install` and before `Title_Run`.

### 2.2 `Timer_Remove` 1000:0407 (far) [O]

`CLI`; restores the saved INT 08h vector; PIT channel 0 mode 3 divisor 0 (= 65536, 18.2 Hz); `STI`.
Called by `Game_Shutdown` only (a crash or `exit` elsewhere leaves the timer reprogrammed).

### 2.3 `Timer_ISR` 1000:0428 (interrupt) [O]

Order per interrupt (interrupts are disabled on entry and until step 5):
1. Save AX, DS; DS = DGROUP.
2. `g_ticks` (DS:3D2B, u32) += 1.
3. `DS:3D33` -= 1 (byte). If the result is negative: `DS:3D33` = 12, call the old INT 08h handler
   (far call with flags pushed: the BIOS updates its clock and sends the EOI). Otherwise send the
   EOI itself (port 20h = 20h). So the BIOS sees interrupts 13, 26, 39, ...
4. Save ES, BP, SI, DI, DX, CX, BX. If `(DS:64C0 & DS:37CE) | DS:6448` != 0 (music block status
   AND the music-enable mask, OR sfx block status; `fmt_sound.md` §5.1) call `Sound_Tick`
   491b:0004.
5. `DS:3D34` -= 1 (byte). If the result is negative: `DS:3D34` = 0; if `g_inGameTick` == 0:
   `g_inGameTick` = 1, `STI`, `DS:3D34` = 4, call `Game_Tick`, `g_inGameTick` = 0.
   If `g_inGameTick` was 1 (a previous Game_Tick still running, i.e. this interrupt nested into
   it), nothing happens and `DS:3D34` stays 0, so the next interrupt tries again.
6. Restore registers, `IRET`.

Exact cadence: the countdown goes 4,3,2,1,0 and fires on the decrement to −1, so Game_Tick runs on
interrupt 5, 10, 15, ... after `Timer_Install`. A late Game_Tick (guard set) is not queued twice:
the game tick fires at the first interrupt after the long one returns, then the 5-interrupt
cadence restarts from there. Sound ticks are never skipped (they run before the guard).

### 2.4 `Hw_Detect` 1000:04A1 and `Video_DetectAdapter` 1000:05BF (context) [O]

Called once from `Game_Init`. Results the engine turns into options (§12, PORTING_NOTES.md §3):
* `g_machineId` DS:1300 = byte at F000:FFFE, then rewritten: on a 186+ CPU, < 0xF0 -> 2; 0xFF with
  0x21 at F000:C000 -> 0x21 (Tandy). On an 8086/8088: 0xFF + 0x21 -> 0x21; < 0xF0 -> 1.
* `g_slowCpu` DS:923C = 1 if `SHR BX,CL` with BX = 0x8000, CL = 32 leaves 0 (only an 8086/8088
  does not mask the count), else 0.
* INT 11h -> DS:1307 (equipment word); floppy count DS:1301 = ((equip & 0xC0) >> 6) + 1 if bit 0.
* INT 21h AH=1Bh media byte: 0xF8 -> DS:1302 = 2, DS:1303 = 1; 0xF9 -> DS:1302 = 1. BIOS 0040:0075
  (hard disks) != 0 -> DS:1303 = 1. INT 21h AH=19h -> DS:1304 = current drive + 1.
* `g_videoType` DS:912C by `Video_DetectAdapter`: Tandy -> 3. Else INT 10h AX=1A00h; if supported
  (AL = 1Ah): display code BL >= 0Bh -> 5 (MCGA); BL 4/5 -> 1 (EGA), or 2 if INT 10h AH=12h BL=10h
  returns BL = 0 (64 KB EGA); BL = 2 -> 2 (CGA); BL = 1 -> stays −1. Otherwise (no 1Ah support or
  another code, e.g. VGA 7/8): Hercules test (port 3BAh bit 7 toggles 10 times within 0x8000
  reads) -> 4; then INT 10h AH=12h BL=10h with BH = CL = 0FFh: BH <= 1 and CL <= 0Fh -> 1 (2 if
  BL = 0), overriding a Hercules result; failing that, CRTC register 0Fh write/read 66h at 3D4h
  -> 2 (3 if Tandy). Still −1 on a Tandy -> 3. A VGA therefore runs as EGA (1).
* `g_soundDevice` DS:63BC: 0, then Tandy -> 1; else `AdLib_Detect` 4ac9:0D0F -> 2; else INT 15h
  AH=C0h model word 0x0BFC (FC 0B) and port 203h echoing 0xA5 ten times -> 3; else 0.

---

## 3. Segment 130f (main.c)

### 3.1 `main` 130f:0007 [O]

```
g_quit (DS:912E) = 0
if Game_Init() == 0: exit(1)
Kbd_Install(); Timer_Install(); Title_Run()           (title/attract, returns at sign-in)
g_uiMode (DS:9119) = 8                                  (sign-in mode)
loop:
  g_dirty (DS:9255) = 0
  Frame_Begin()
  if g_fullRedraw(8FBB) == 0 and DS:8FB2 == 0 and DS:8FC8 == 0:
      SignIn_Update(); Gameover_UpdateMessages(); Hints_Update(); Menu_Update()
  Scene_Update(); Scroll_Update(); Search_Update(); Quiz_Update(); Sound_SyncState()
  Game_HandleHotkeys(); Screen_DrawOverlay(); Playfield_Redraw(); ShelfRoom_DrawShelf()
  RankChest_Update()
  DS:9283 = (g_uiMode == 0) ? 13 : 1                    (number of sprite slots to draw)
  Sprites_DrawSorted(0, DS:9283); Frame_Present(); Sprites_EraseAll(0, DS:9283)
until g_quit != 0
Game_Shutdown()
```

`DS:8FB2` is the latched transition flag and `DS:8FC8` the latched partial-redraw (search) flag
(§3.5); while either or a full redraw is pending, the four UI updaters are skipped for that frame.
With a menu, dialog, hint, message or quiz open (`g_uiMode` != 0) only sprite slot 0 (the mouse
pointer) is drawn. Sprites are drawn after all back-buffer drawing and erased right after the
present, so the back buffer between frames holds only the background (scenery + HUD + overlays).

### 3.2 `Game_Shutdown` 130f:00DB [O]

`Music_Stop` (491b:0811), `Sfx_Stop` (491b:074B), `Timer_Remove`, `Kbd_Remove`, INT 10h AX=0003h
(80x25 text), `exit(0)`. Called from `main` and `TitleStep_Wait` (Esc on the attract pages).
The saved BIOS keyboard flags DS:061B (§5.1) are not restored.

### 3.3 `Game_Tick` 130f:00FE (called from the timer ISR) [O]

```
if g_quit != 0: return
if g_stepDone == 0:
    DS:8FA7 = (g_player.x DS:37E2 - g_cameraX DS:37EA) >> 1          (signed shift: screen x, bytes)
    DS:8FA9 =  g_player.y DS:37E4 - g_cameraY DS:37EC                 (screen y, rows)
    if DS:8FA7 > 0x400 (unsigned):                                    (player in another row of the strip)
        DS:8FA7 += ((playerY & 0xFF80) - (cameraY & 0xFF80)) * 8
        DS:8FA9  =  (playerY & 0x7F) - (cameraY & 0x7F)
    Mouse_Poll(); Input_Tick(); Anim_Tick()
    if g_loadedScene (DS:8FBE) == g_level (DS:928F) or g_uiMode == 0x80:
        Input_MouseWalk(); Player_Update(); Elves_Update(); Cells_UpdateVisible()
else:
    Anim_Tick(); Input_Tick()
DS:90FE -= 1; if DS:90FE < 0 (signed): DS:90FE = 12                   (13-tick cycle, 12..0)
DS:9100 -= 1 (byte); if DS:9100 == 0 and g_uiMode == 0: DS:9100 = 60; g_seconds DS:9115 += 1
```

Notes:
* The screen position (8FA7/8FA9) is the input of `Input_MouseWalk` and `Player_Move`; the
  correction handles a player whose world x wrapped into the next strip row while the camera has
  not (difference > 0x400 bytes = 2048 px).
* The game-logic block runs only while the scene that `Scene_Update` loaded is the wanted level
  (during a scene change the world is frozen) or during the title (`g_uiMode` 0x80).
* Order matters for the random sequence: within one game tick the rand() users are, in this
  order, `Input_Tick` (none), `Anim_Tick` (none), `Input_MouseWalk` (none), `Player_Update`,
  `Elves_Update`, `Cells_UpdateVisible` (castle creature) [O: the first three contain no rand
  call]; the main loop's rand users (title sparkles, quiz, layout, `Frame_Present` dissolve,
  §3.4) run between ticks. In the original the interleaving of the two is timing dependent.
* `g_seconds` ("seconds" of the hint timers) advance every 60 game ticks = **1.2675 s** and only
  while no UI mode is active. While `g_uiMode` != 0 the byte keeps counting down through 0 to 255
  and wraps (period 256 ticks), so after closing a menu the next "second" comes after a random
  0..255 ticks. `DS:90FE` (12..0) has no reader in these modules [I: used by animations].

### 3.4 `Frame_Present` 130f:01C2 [O]

```
if g_dirty == 0:            busy loop i = 0 .. 999 (no other effect)
elif DS:8FB2 == 0:          Present_Dirty()                 (copy the dirty spans to video RAM)
else:                       (screen transition latched by Frame_Begin)
    if g_slowCpu == 0:
        Dirty_Clear()
        repeat 0x15E = 350 times:
            a = rand() & 0x0F;  row = a * 8                          (0..120)
            b = rand() & 0x0F;  col = b * 10; if col > 0x91: col = 0x91   (0..145 bytes)
            Dirty_AddRect(AX x = col + 2, BX y = row + 0x10, CX h = 8, DX w = 10)
            Present_Dirty()
    Present_Full()
    g_transition DS:9217 = 0
```

`Dirty_AddRect` (1000:1181) takes AX = x bytes, BX = y, CX = height (rows), DX = width (bytes),
all rounded to even values [O, same convention as `Playfield_Redraw`]. So the **dissolve** copies
350 random cells of 10 bytes x 8 rows (20 x 8 pixels) of the already-complete back buffer to the
screen one at a time, on a 16 x 16 grid covering the play-field (x bytes 2..156, rows 16..143; the
last column is clamped to x 0x93 and overlaps its neighbour), then copies the whole buffer. Each
cell uses two rand() calls (row first, then column): **700 rand() calls per transition** in the
shared Turbo C sequence. On a slow CPU (g_slowCpu = 1) there is no dissolve and no rand() call. In DOSBox-X the
dissolve frame took 30 PIT ticks = **127 ms** (7-8 AVI frames, `avi/ega_doorway_dissolve.avi`
and the frame log `r2/fl_l2door_after`) [O].

### 3.5 `Frame_Begin` 130f:025A [O]

Under `CLI`:
```
DS:37F2 = g_cameraX;   DS:37F4 = g_cameraY             (camera of this frame)
DS:37F6 = DS:37EE;     DS:37F8 = DS:37F0               (camera delta of the last Player_Move: x bytes, y rows)
g_sceneWanted DS:8FCA = g_level
g_fullRedraw DS:8FBB = DS:924F                         (full play-field redraw request)
DS:8FB2 = g_transition DS:9217                          (dissolve request)
DS:8FBD = DS:925D (byte)                                (rank-screen state)
DS:8FC8 = DS:9278                                       (partial redraw request, search)
DS:8F9F..8FA2 = DS:9201..9204 (bytes)                   (tunnel-seen flags, drawn by the HUD)
DS:922E |= DS:925B                                      (HUD full-redraw request)
Sprites_Snapshot(0, DS:9283)                            (4306:0393: latch sprite positions)
g_stepDone = 0
```
Then `STI` and, from the latched camera (cx = 37F2, cy = 37F4):
```
DS:920F = (cy >> 2) & 0x1E             tile row inside the block * 2        (byte offset in a map column)
DS:9126 = ((cy & 0xFF80) as 9-bit) >> 7 << 4     block row * 16           (index base into g_cellPicture)
DS:920D = (cx << 2) & 0x1E0            tile column inside the block * 32    (32 bytes per map column)
DS:9124 = ((cx & 0xFF80) as 9-bit) >> 7          block column
if DS:9124 >= 16:  DS:9124 -= 0x1F0;  DS:9126 -= 16    (a camera x of -128..-1 = column 15 of the row above)
if g_fullRedraw:   DS:37F6 = DS:37F8 = 0              (no incremental scroll in a redraw frame)
```
The "9-bit" value is `ROL(t,1)` byte-swapped, i.e. `(t >> 7) & 0x1FF` for t = cx & 0xFF80 (so a
negative camera gives 0x1FF). Only the sub-block tile position is kept: **the camera's low 3 bits
are ignored for the scenery** (tiles are always drawn on the 8-px world grid), while sprites are
placed with the exact camera (`screen x bytes = ((wx - cx) >> 1) + 2`, `fmt_graphics.md` §4.3).
A camera that is not a multiple of 8 therefore shifts sprites against the scenery (the cave of
cell 0xF7 sets camera = player.x − 0x124: 4 px; capture `ega/r2_w_cave_f7_01_inside.png`).

### 3.6 `Playfield_Redraw` 130f:0326 [O]

Runs every frame; does nothing unless `g_fullRedraw` (DS:8FBB) != 0.
* Column range (x bytes): normally 2 .. 0x9E. With the partial flag DS:8FC8 set (search):
  `x0 = ((((py & 0xFF80) << 4) + (px & 0xFF80) - ((cy & 0xFF80) << 4) - cx) >> 1 & 0xFFFC)
  + g_searchSide * 0x40 + 2`, `x1 = x0 + 0x44`, clamped to x0 >= 2, x1 <= 0x9E (unsigned) — the
  player's 128-px cell (or the neighbour selected by `g_searchSide` DS:9276 = −1/0/+1, set by
  `Search_CanSearch`) plus one tile column, using a linear strip position (row * 0x800 + x).
* Rows: y = 0x10, 0x18, ... while y < 0x8F (16 tile rows, y 16..143).
* For each row y and each column x (step 4 bytes): map entry of world tile
  `(tile column = (x - 2) * 2 / 8 + camera tile column, tile row = (y - 0x10) / 8 + camera tile row)`:
  `row index r = (y - 0x10) / 4 + DS:920F` (bytes); if r >= 0x20: next block row
  (`block index + 16`, r -= 0x20); `col offset c = (x - 2) * 8 + DS:920D`; if c >= 0x200 the
  block column advances by c >> 9 and c &= 0x1FF; block = `g_cellPicture[blockRow + blockCol] &
  0x7F` (DS:0CAA, u16); the block's map is at paragraph `mapSeg (DS:37BA) + block * 32`, the entry
  at byte `c + r`; `Tile_DrawColumn` (1000:19CF, register args SI = x, DI = y, DX = y + 7,
  CX = block map segment, BX = entry offset) draws that one 8x8 tile.
  (Block column overflow past 15 is not wrapped here; it never happens with cameras < 0x800.)
* Colour 0 is set before (`Gfx_SetDirtyTracking(0)`) and colour 1 after; then `Dirty_AddRect(x0, 0x10,
  w = x1 − x0 + 1, h = 0x80)`.
* Bookkeeping: DS:9297 = cx, DS:9299 = DS:920D, DS:9270 = DS:9124, DS:929B = cy, DS:929D =
  DS:920F, DS:9272 = DS:9126 (the "last drawn" camera used by `Scroll_Update`); DS:37F6 = DS:37F8
  = 0; `g_fullRedraw` = DS:924F = DS:8FC8 = DS:9278 = 0; `g_dirty` += 1; DS:8FBA = 1.

Called from `main` and from `Hud_Redraw` (full HUD redraw forces `g_fullRedraw` = 1 first).

### 3.7 `Scroll_Update` 130f:04E5 [O]

Does nothing while `g_uiMode` != 0 or a HUD full redraw is pending (DS:922E != 0).
**Horizontal**, if `DS:37F2 != DS:9297` (camera x changed since drawn) and `DS:37F6 != 0`
(latched delta) and (`DS:920D != DS:9299` or `DS:9124 != DS:9270`) (another tile column):
* delta < 0 (camera moved left): `Scroll_PlayfieldRight` (1000:196D, DI = 0x10, DX = 0x6F on a slow
  CPU else 0x8F): the play-field contents move right by one tile column; the new column is drawn at
  x = 2 from the column at the camera's tile position.
* delta > 0: `Scroll_PlayfieldLeft` (1000:18B3, same DI/DX): contents move left; the new column is
  the one 0x98 bytes (38 tiles) to the right of the camera, drawn at x = 0x9A (normal) — the
  column offset `DS:920D + 0xC0` (0x9A on screen) or, with `g_slowScroll` DS:3D31 = 1,
  `+ 0xA0` drawn at x = 0x96 (two columns, see below); offsets >= 0x200 wrap into the next block
  column (+1).
* The new column is drawn top to bottom from tile row `DS:920F` of its block: first the
  `16 − tileRow` tiles of the current block row (DX = 0x8F − tileRow * 8), then, if tileRow != 0,
  the remaining tiles from the block row below (block index + 16) starting at its tile row 0.
  On a slow CPU (g_slowCpu = 1) only rows 0x10..0x6F (12 tiles, DX = 0x6F) are drawn and there is
  no second part.
* With `g_slowScroll` = 1 a second adjacent column is drawn (x + 4, map offset + 0x20, wrapping to
  the next block column), because the camera moves 16 px per step then (`Player_Move` doubles the
  delta, 18cf:16A9).
* Bookkeeping DS:9297 = cx, DS:9299 = DS:920D, DS:9270 = DS:9124; `g_dirty` += 1.
**Vertical**, if `DS:37F4 != DS:929B` and `DS:37F8 != 0` and (`DS:920F != DS:929D` or
`DS:9126 != DS:9272`):
* delta < 0: `Scroll_PlayfieldDown` (1000:1AEF, DX = 0x8F, DI = 0x10), new tile row at y = 0x10;
* delta > 0: `Scroll_PlayfieldUp` (1000:1A86), new row at y = 0x88 (map row offset + 0x1E), or
  with slow scroll y = 0x80 (+ 0x1C), two rows (DX = y + 0x0F instead of + 7).
* The row is drawn tile by tile for x = 2 .. 0x9A (step 4), the map column offset advancing by
  0x20 per tile and wrapping into the next block column.
* Bookkeeping DS:929B = cy, DS:929D = DS:920F, DS:9272 = DS:9126; `g_dirty` += 1.

Equivalence [I, checked on captures]: for g_slowCpu = 0 the incrementally scrolled play-field equals
a full redraw at the new camera (the reference captures show no seams), except for pictures drawn
into the back buffer by cell handlers (they scroll along until the next redraw; `code_world.md`).
**Slow-CPU quirk** [O, `avi/ega_r2_slowcpu_X_walk.avi`]: with `-X` the horizontal scroll shifts and
redraws only rows 16..111; rows 112..143 keep the old picture (visible tearing at the bottom of the
play-field) until a full redraw.

### 3.8 `Hud_Redraw` 130f:07A5 (painter 0 of DS:0104) [O]

Called every frame by `Screen_DrawOverlay` while `g_screen` DS:913E = 0 and the scene is < 3.
Drawing details: `code_ui.md`; the logic and order are:
1. If DS:922E != 0 (full HUD redraw requested): frame fills (rows 0..10 colour 0xFF pattern,
   11..14, play-field border, rows 0x91..0xC7), `MenuBar_DrawTitle(0..2, 0)`, `g_fullRedraw` = 1 and
   `Playfield_Redraw()`; if any of DS:8F9F..8FA2 (tunnel flags) is set, copy them to DS:7D7C,
   7D7E, 7D80, 7D82 and call `ShelfRoom_DrawShelf()`; unless DS:922E == 1, `Sprites_DrawSorted(1, 13)`;
   DS:922E = DS:925B = 0; `g_dirty` += 1; and force every cached value below to mismatch
   (DS:928D, 924D, 9259, 9268, 91FB each += 1).
2. If coins != DS:928D or nets != DS:924D or found != DS:9259 or coins != DS:9122:
   * draw status-bar frames 0x78/2 = 60 (box), coin bag 64/65/66 (coins > 10 / > 4 / else),
     net 67/68/69 (nets > 3 / > 0 / else), 70, then for each level L = 0..2 that equals g_level,
     the tier+2 treasure markers (frame 192 not found, 193/194/195 found on level 0/1/2) at
     x bytes 0x8D/0x90 alternating, y 0xA4 + 6 i, by the level's found mask bits (record +0x130);
   * **coin count-up**: if coins > DS:9122: `Sfx_Play(4)`, DS:9122 += 1; else if coins < DS:9122:
     DS:9122 -= 1 (silent). One step per call = **one per frame**.
   * numbers (`Hud_DrawNumber`, colour 0xCCCC, y 0xBA): DS:9122 at x 0x6E, nets at 0x7D, found
     at 0x8F; cache DS:928D = coins, DS:924D = nets, DS:9259 = found.
3. If `g_keyState` DS:9120 != DS:9268: frame 0x74/2 = 58 (clue box), key frame 50 + keyState/2 at
   (6, 0xBE) unless 0, and if the scene is < 3 the revealed clue words of the current level
   (`Text_Draw` colour 0xCCCC at x 0x0C, y 0xBA − 13 k for slots 2, 1, 0, skipping words with bit 7
   set) from DS:5EB2[word]; DS:9268 = keyState; `g_dirty` += 1.
4. If DS:90F8 (prompt) != DS:91FB: `Hud_DrawPromptIcon()`.

Note the count-up compares with DS:9122 (displayed coins) separately from the cache DS:928D, so the
bag picture and numbers are redrawn every frame until the display has caught up.

### 3.9 `Hud_DrawPromptIcon` 130f:0C2E [O]

Draws frame 0x76/2 = 59 (middle panel) and, if DS:90F8 != 0, the prompt frame `60 + DS:90F8 / 2`
(DS:90F8 = 4: "Press ↓ to drop coin", 6: "Press Enter to go on", 2: the move/select prompt);
DS:91FB = DS:90F8; `g_dirty` += 1. Also called by `Scene_Update`.

### 3.10 `Screen_DrawOverlay` 130f:0C73 [O]

```
if DS:922E != 0 and DS:0102 == 1:                  (full-screen picture request)
    Sprite_Draw(frame DS:37CA, x 0, y 0, flags 1)   (opaque, honours the frame's own offsets)
    DS:0102 = DS:37CC = DS:37CA = DS:37C8 = 0
    if g_area DS:910B != 4: MenuBar_DrawTitle(0,0), (1,0), (2,0)
    g_dirty += 1; DS:922E = 0
elif g_sceneWanted < 3:
    far call g_screenPainters DS:0104[g_screen]     (0 Hud_Redraw, 1 Quiz_DrawScreen, 2 SignIn_Draw)
```

### 3.11 `Hud_DrawNumber(x, y, colour, value)` 130f:0D00 [O]

Prints a value 0..999 with the font (`Font_DrawChar` 1000:2180) at (x bytes, y): hundreds digit
if non-zero, tens digit if non-zero or hundreds printed, units always; a suppressed leading digit
still advances x by 2 bytes (fixed 3-digit field, right-aligned in 4-px steps). The digits are
computed by repeated subtraction (signed); values >= 1000 print a hundreds "digit" >= 10 (glyph
0x30 + n) [O arithmetic, I appearance].

### 3.12 `Mouse_Poll` 130f:0D94 (per executed game tick) [O]

Only if a mouse driver answered (DS:912A == −1):
```
prev: DS:925E = mouseX (DS:911A), DS:9266 = mouseY (DS:911E), DS:9143 = buttons (DS:90F4)
INT 33h AX=3 -> buttons BX -> DS:90F4, x CX -> DS:911A (0..159, bytes), y DX -> DS:911E (0..199)
pointer sprite slot 0: x = mouseX, y = mouseY, DS:8FDA = 0
if pointer visible (DS:92AC == 1):
    if 0x10 <= mouseY <= 0x8F and g_uiMode == 0:          (pointer over the play-field)
        if prevX > x: Anim_Set(0, 7)                       (pointer animation facing left)
        if prevX < x: Anim_Set(0, 8)                       (facing right)
        if prevX == x and DS:8FE2 == 1: Anim_Set(0, 7)
        if prevX == x: Anim_SetIfIdle(0, DS:8FE2)
    else: Anim_Set(0, 1)                                   (arrow pointer)
    if x, y and buttons all unchanged: DS:92AE += 1; at 600 (0x258): DS:92AC = 0, Anim_Clear(0)
    else DS:92AE = 0
else (hidden): any change of x, y or buttons: DS:92AE = 0, DS:92AC = 1
```
The pointer hides after **600 executed game ticks (12.7 s)** without mouse activity and reappears
on the next movement or button change. `Anim_Set/SetIfIdle/Clear` are 4306:07FA/0784/092F
(`code_player.md`). DS:8FE2 is slot 0's current script [I].

### 3.13 `RankChest_Update` 130f:0EDF (per frame) [O]

Drives the rank chest screen (clubhouse chest picture and the screen after the castle) through the
state byte DS:8FBD (latched from DS:925D, set to 1 by 176a:102A and cleared by 176a:10B9):
* **State 1 (start)**: text colour DS:9215 = 0xFFFF; `Text_Draw(0x44, 0x1D, player name)`;
  `Hud_DrawNumber(0x6C, 0x77, 0xFFFF, total)`; find the first pair index i (step 2 words) of
  `g_rankTable` DS:0110 with `table[i] == 0xFFFF` or `table[i] >= total + 1` (unsigned), then
  **fill = table[i + 3]** (the value of the *next* pair: total 0..4 -> 8, 5..24 -> 16, 25..69 -> 24,
  70..114 -> 32, 115..169 -> 40, 170..229 -> 48, 230..299 -> 56, 300..399 -> 66, >= 400 -> 86);
  DS:92A8 = fill; DS:92B0 = DS:9100 & 0xFC; DS:92A6 = 0 (rows done); DS:925D += 1 (-> 2).
* **State != 0, != 1**: when `(DS:9100 & 0xFC) != DS:92B0` — i.e. **every 4 game ticks**, at most
  once per frame — DS:92B0 = that value and one row is added: `Gfx_ReplaceColorRect` (1000:176C, register
  args AX = 0x7777, BX = 0x6666, SI = 0x1A, CX = 0x98, DI = y, DX = y + 1 with y = 0x6F − n,
  n = DS:92A6; the XOR of the two patterns turns logical colour 7 into 6, the cyan band into the
  pink fill; the exact rectangle convention is in `code_ui.md`); when n equals DS:0116 (= 8), DS:011A (16), DS:011E
  (24), DS:0122 (32), DS:0126 (40) or DS:012A (48) the coin row for that rank is drawn with
  `RankChest_DrawStar(x, y, 0xFA)` (SPRITES frame 0xFA/2 = 125, masked): n = 8: (0x51, 0x67);
  16: (0x4E, 0x5F), (0x54, 0x5F); 24: (0x4B,0x57), (0x51,0x57), (0x57,0x57); 32: x 0x48, 0x4E,
  0x54, 0x5A at y 0x4F; 40: x 0x45, 0x4B, 0x51, 0x57, 0x5D at y 0x47; 48: x 0x42, 0x48, 0x4E,
  0x54, 0x5A, 0x60 at y 0x3F; then `Sfx_Play(3)` (every row), `g_dirty` += 1, n += 1. When n ==
  fill: DS:925D = DS:8FBD = 0 and, if n − 1 >= DS:012E (56), **g_champFlashTimer** DS:0100 = 800.
* **CHAMPION flash** (every frame while DS:0100 > 0): if timer % 160 == 0 draw "CHAMPION"
  (DS:013A) at (0x40, 0x2E) in colour 0x6666 (the band colour: invisible), if timer % 160 == 80 in
  0xFFFF (black: visible); timer −= 1 (−4 on a slow CPU); `g_dirty` += 1. Starting at 800 the
  text is visible during 720..641, 560..481, 400..321, 240..161 and from 80 on (it stays): five
  "blinks" of 80 frames, **ending visible**. Counted in frames (DOSBox-X: 0.70 s for timer 720 ->
  80, ~914 frames/s, AVI frames 3042-3084 of `avi/ega_r2_crown_nokey.avi`).

Timing [O]: the fill steps follow the game tick (4 ticks per row, 84.5 ms; 8 rows for a Trainee in
`avi/ega_r2_end_t2_nokey.avi` frames 1782-1812), the CHAMPION flash follows the frame rate.

### 3.14 `RankChest_DrawStar(x, y, frame2)` 130f:1261 [O]

`Sprite_Draw(frame2 / 2, x bytes, y, flags 0x40)` (masked, honours the frame's hotspot).

---
## 4. Segment 1460 (init.c)

### 4.1 `Game_Init` 1460:0003 → 1 ok / 0 fail [O]

Steps in order (a failure returns 0 and `main` exits with code 1):
1. `g_spriteBank` DS:37BC/37BE = `farmalloc(0x1C908)` (117 000 bytes, the largest PAK fits);
   null -> `Fatal_Print(DS:063D)` (not enough memory text), return 0.
2. `harderr(HardErr_Handler 1460:057D)` (critical-error handler).
3. DS:9136 = `getdisk()` (current drive, 0 = A:).
4. DS:061B = BIOS keyboard flags byte 0040:0017; the byte is written back with bits 5 and 6
   cleared (**Num Lock and Caps Lock forced off**; never restored at exit).
5. `Hw_Detect()` (§2.4).
6. DS:92B2 = ((DS:1307 >> 6) & 3) + 1 (floppy count from the equipment word, signed byte shift).
7. DS:0618 = `Video_GetMode()` (INT 10h AH=0Fh).
8. `ParseCommandLine()` (§4.6) — switches override the detection.
9. `g_videoType` == −1 -> `Fatal_Print(DS:0693)` (no graphics adapter message), return 0.
10. DS:0619 = byte 0040:0504 (DOS "current logical floppy"); if it is 0xFF and there is one floppy,
    it (and 0040:0504) become 0.
11. `Path_InitDrives(DS:9136)` (§4.17). If the disk-1 path starts with A or B:
    `Disk_CheckInfoFile` (150a:0002, TLCINFO.DAT); if its byte DS:07AB == 8 both disk labels DS:0424
    and DS:044D become "Program Disk" (DS:06E6).
12. Network test (`fmt_data.md` §5): DS:061A = 1 if `<disk-1 dir>ssrnet.dat` (DS:06F3) exists
    (`access(path, 0)`); if `<disk-1 dir>ssrquiet.dat` (DS:06FE) exists and DOS >= 3.10
    (DS:007D/007E): if `Net_IsNetworkDrive(current drive)` == 1 or `Net_IsNetworkDrive(path drive)`
    == 1, and DS:061A == 0 -> `Fatal_Print(DS:070B)`, return 0. The probe is a stub returning 0,
    so this never fails.
13. `g_mouse` DS:912A = `Mouse_Detect()` (−1 = driver present).
14. `Gfx_InitBufferPtrs()` (1000:037D), `Video_Init()` (1000:0361, mode set), `Pal_Apply()`
    (1437:0001), `Mouse_Setup()`.
15. `Sound_LoadData()`, `Help_LoadFile()` (43cb:107A), `Quiz_LoadDictionary()` (46ce:095F),
    `Sprites_LoadBegin()`; each must return non-zero.
16. `Game_InitVars()`; return 1.

### 4.2 `Sound_LoadData` 1460:022A → 1 / 0 [O]

Effects file: index 12 (SFX.TAN) for device 1 or 3, else 10 (SFX.IBM); opened with mode 0x8004,
read whole (`File_Read(DS:63A4/63A6 far buffer, 9999, handle)`), closed. Music: index 11
(TUNES.IBM) for device 0, else 13 (TUNES.TAN), into DS:63A8/63AA. Then `Sound_Init` (491b:067B,
`fmt_sound.md` §2.4); return 1. An open failure returns 0 (the game does not start).

### 4.3 `Sprites_LoadBegin` 1460:02C6 → 1 / 0 [O]

Opens index 15 (BEGIN.PAK); `File_Read(g_spriteBank, 0xFFF0)`; if exactly 0xFFF0 bytes came back,
reads a further 0xFFF0 bytes at the far pointer `g_spriteBank + 0xFFF0` (32-bit add with carry
into the segment word); closes; returns 1. Also called by `SignIn_Update` to reload BEGIN.PAK.

### 4.4 `Game_InitVars` 1460:033A [O]

Initial values (all BSS, so zero unless listed): `g_hintsOn` DS:9224 = 0; DS:922E = 0; DS:9238 =
1 (input: deliver the next command at once, §6.2); DS:90F6 = −1 (queued command); DS:90FC = −1
(menu); `g_cmd` DS:9226 = −1; DS:90FE = 12; DS:9100 = 60; `g_seconds` = 0; DS:9111 = 1;
DS:928D = DS:924D = DS:9259 = DS:9268 = DS:91FB = 0 (HUD caches); `g_transition` = 0; DS:9260 =
DS:9262 = DS:9291 = 0; clip window DS:37DA = 0, 37DC = 0x9F, 37DE = 0, 37E0 = 199 (whole screen);
`g_keys` = 0; `g_loadedScene` = 0xFF; `g_endState` = 0; `g_uiMode` = 0x80 (title); DS:926C = 0;
camera deltas DS:37EE = DS:37F0 = 0; `g_playerDX` = `g_playerDY` = 0; DS:8FB4 = 0x32 (base of the
opened-picture numbers of level 0, `Search_Update`); DS:928B = 6; `g_shownTreasure` DS:8FB0 = −1;
DS:9219 = 0; DS:9254 = 0xFF; DS:9213 = 0x1A; DS:9211 = 0x44; `g_slowScroll` DS:3D31 =
(g_slowCpu == 1); `Anim_InitSlots()` (4306:02C0); DS:8FA5 = g_slowCpu.

### 4.5 `StrIndexOf(ch, far str)` 1460:0437 → index or −1 [O]

Scans the string; the end test comes before the comparison, so `ch` = 0 returns −1.

### 4.6 `ParseCommandLine` 1460:0463 [O]

For each argv[i], i = 1 .. argc−1, whose first character is `-` or `/`: the second character (sign-
extended) is looked up in the 14-word code-segment table 1460:04C2 (`C E H M S T X c e h m s t x`)
and dispatched through the word table 1460:04DE:

| Switch | Effect |
|---|---|
| `E` | `g_videoType` = 1 (EGA) |
| `C` | 2 (CGA) |
| `T` | 3 (Tandy) |
| `H` | 4 (Hercules; drawn like CGA) |
| `M` | 5 (MCGA) |
| `X` | `g_slowCpu` ^= 1 |
| `S<c>` | `g_soundDevice` = DS:0629[index of c in "iItTaAsS" (DS:0620)] = 0 0 1 1 2 2 2 2: i speaker, t Tandy, a / s AdLib; unknown or missing c: no change |

Only the second character counts (`-EGA` = `-E`); several switches need several arguments
(`-E -Si`); later switches win; nothing is validated (forcing EGA on a CGA card is the user's
problem). The PS/1 card (device 3) cannot be selected.

### 4.7 `Video_GetMode` 1460:0567 (near) [O]

INT 10h AH=0Fh; returns AL (current video mode).

### 4.8 `Video_RestoreMode` 1460:056E [O]

If DS:0618 != 0xFF: INT 10h AH=0, AL = DS:0618.

### 4.9 `HardErr_Handler(errval, ax, bp, si)` 1460:057D [O]

Critical-error handler (Turbo C `harderr`): DS:92C1 = 1 (error flag), DS:92C3 = errval, DS:92C5 = ax
(AL = drive), then `hardretn(−1)` (the DOS call fails with −1; execution continues in the caller).
The file wrappers below clear DS:92C1 before each call and inspect it afterwards.

### 4.10 `Net_IsNetworkDrive(drive)` 1460:05A3 → 0 [O]

Stub: returns 0 at once; the original IOCTL 4409h test after it is dead code.

### 4.11 `Scene_LoadPan(index)` 1460:05E8 → non-zero ok / 0 [O]

Opens the file (mode 0x8004), reads the four header words (map packed size, width, height, tiles
packed size), reads the packed map to the end of the 0xA400-byte map buffer (`g_mapPtr` DS:37B8 +
0xA400 − size) and the packed tiles to the end of the 0xE880-byte tile buffer (`g_tilesPtr`
DS:37B4 + 0xE880 − size), closes, then `Map_Unpack` (1000:2368; SI = buffer offset, BX = width,
CX = height) and `Tiles_Unpack` (1000:240E; SI, DX = size) unpack in place. Formats:
`fmt_graphics.md` §3. Called by `Scene_Update`, `SignIn_Update` (OPENING.PAN) and `Title_Run`.

### 4.12 `File_OpenByIndex(index, mode)` 1460:06C8 → handle or −1 [O]

* Record = DS:0476 + 22 · index (`g_fileTable`: name[13] at +0, disk number byte at +0x0D, 8 unused
  bytes; 18 entries + an empty one, list in `fmt_data.md` §1).
* Directory = DS:036E if disk == 1 else DS:03C9 (both "A:\TLC\sst\" until `Path_InitDrives`).
  A 3-entry far-pointer table copied from DS:0631 ({null, DS:036E, DS:03C9}) gives the drive letter
  (first character of the path) for `Disk_Ensure(disk, letter)` (150a:00C4: on a floppy, asks for
  the right disk).
* Loop: DS:92C1 = 0; path = directory + name (`StrConcatList`); handle = `open(path, mode, 0x180)`;
  `r = File_HandleError(1, 0, 0, handle)` (153e:0137); repeat while r == 3 (retry after a
  message box). Returns the handle (−1 when the user gave up or the file is missing).
Modes used: 0x8004 (binary read/write), 0x8001, 0x8104 (+ create, USERS.DAT).

### 4.13 `File_Read(far buf, count, handle)` 1460:07B4 → bytes read [O]

`read()`; if a critical error was flagged (DS:92C1), seek back to the current position (`tell` +
`lseek`) and read again, until no error.

### 4.14 `File_Write(far buf, count, handle)` 1460:080B → File_HandleError code [O]

DS:92C1 = 0; `n = write()`; returns `File_HandleError(4, 0, 0, n)` (4 when n > 0 and no critical
error, else after a message box).

### 4.15 `File_Close(handle)` 1460:0849 → 0 / rc [O]

`close()`; on failure repeat while `File_HandleError(5, 0, 0, rc)` == 3.

### 4.16 `Fatal_Print(far msg)` 1460:0887 [O]

`Video_RestoreMode()`, then DOS write (INT 21h AH=40h) of `strlen(msg) − 1` bytes of msg to handle
2 (stderr): the last character of the message is not printed.

### 4.17 `Path_InitDrives(drive)` 1460:08CC [O]

* If drive < 2 (started from A: or B:): by floppy count DS:92B2: 0 or 1 -> both directory drive
  letters = 'A' + drive; 2 -> disk 1 on 'A' + drive and disk 2 on the other floppy; if DS:1302 ==
  1 (a high-capacity drive, §2.4) disk 2 uses disk 1's letter.
* If DOS < 3 or the second character of argv[0] is not ':': both letters = 'A' + drive (the
  directory stays "\TLC\sst\"). Otherwise argv[0] is copied, cut after its last backslash
  (keeping it), and copied into both DS:036E and DS:03C9 — **the game reads all data from the
  directory of SST.EXE** (on a hard disk both "disks" are that directory).

### 4.18 `StrCopyBounded(far dst, far src)` 1460:09B7 → far pointer to dst's NUL [O]

Copies including the NUL, at most 1001 characters.

### 4.19 `StrConcatList(far dst, far s1, far s2, ..., NULL)` 1460:09F2 → length [O]

Concatenates the NULL-terminated list of far strings (at most 2000) into dst.

### 4.20 `Mouse_Detect` 1460:0A5E → 0 / INT 33h AX=0 result [O]

INT 21h AX=3533h; if the INT 33h vector segment is 0 -> 0; else INT 33h AX=0 (reset) -> AX
(0xFFFF when a driver is installed).

### 4.21 `Mouse_Setup` 1460:0A74 [O]

Only with a driver (DS:912A == −1): INT 33h AX=0Ch CX=0 (no event handler); AX=7 CX=0 DX=0x9F
(**x range 0..159 = byte units**); AX=8 CX=0 DX=0xC7 (y 0..199); AX=0Fh CX=DX=0x20 (32 mickeys
per 8 pixels: slower than the driver default).

---

## 5. Segment 43a0 (kbd.asm)

### 5.1 `Kbd_Install` 43a0:0002 [O]

Saves INT 09h (`getvect`) into DS:92FC/92FE, installs `Kbd_ISR` (`setvect(9, 43a0:0071)`),
`ctrlbrk(Kbd_CtrlBreakCb)`, DS:9257 = 0 (jump-from-diagonal flag), DS:92FA = 0 (Ctrl flag).

### 5.2 `Kbd_Remove` 43a0:0043 [O]

Restores INT 09h, then empties the BIOS keyboard buffer (`while bioskey(1): bioskey(0)`).

### 5.3 `Kbd_ISR` 43a0:0071 (INT 09h) [O]

```
save registers; DS = DGROUP; STI                         (timer interrupts may nest in here)
s = IN 60h; swallow = 0
if (signed)s == 0xE1 or 0xE0: acknowledge + EOI, return  -- never true, see below
if s & 0x80 (break code): look up s & 0x7F in the RELEASE table (CS:00DC, 17 words, targets CS:00FE)
else (make code):        look up s in the PRESS table (CS:019F, 17 words, targets CS:01C1)
diagonal rule (always):
    if (g_keys & 9) == 9 or (g_keys & 5) == 5:          (Up+Right or Up+Left)
        g_keys = (g_keys & ~3) | 0x20; DS:9257 = 1        (Up/Down off, jump on)
    elif DS:9257 == 1:
        g_keys &= ~0x20; DS:9257 = 0
if swallow: acknowledge the keyboard (port 61h bit 7 pulse), EOI
else: far call the old INT 09h handler (the BIOS puts the key into its buffer)
restore, IRET
```

Key actions (`g_keys` DS:9105 bits: 1 Up, 2 Down, 4 Left, 8 Right, 0x10 net (Space), 0x20 jump):

| Scan | Key | Make | Break |
|---|---|---|---|
| 0x48 / 0x50 / 0x4B / 0x4D | Up / Down / Left / Right (keypad or grey) | set 1 / 2 / 4 / 8 | clear it |
| 0x47 | Home | set 1+4 (-> diagonal -> 4+0x20) | clear 5 |
| 0x49 | PgUp | set 1+8 (-> 8+0x20) | clear 9 |
| 0x58 | F12 (Tandy Home) | set 5 | clear 5 |
| 0x39 | Space | set 0x10 | clear 0x10 |
| 0x29 / 0x4A / 0x2B / 0x4E | Tandy Up / Down / Left / Right (only if DS:1300 == 0x21; on other machines these are the keys backquote, keypad minus, backslash, keypad plus and behave as ordinary keys) | set 1 / 2 / 4 / 8 | clear |
| 0x1D | Ctrl | DS:92FA = 0 | DS:92FA = 1 (DS:92FA has no reader) |
| 0x37, 0x45, 0x46, 0x54 | PrtSc/keypad *, Num Lock, Scroll Lock, SysRq | swallowed (not passed to the BIOS) | swallowed |

Facts that matter for a port:
* **Movement keys are not swallowed**: the BIOS also queues them (0x4800, 0x5000, 0x4B00, 0x4D00,
  0x3920 ...), which is how menus, dialogs and the quiz read arrows and Space (`Input_Poll`).
* The E0/E1 prefix test compares the sign-extended byte with 0x00E0/0x00E1 and never matches, so a
  grey-key prefix byte 0xE0 is treated as the break code of scan 0x60 (no table entry) and only
  runs the diagonal rule. Consequence [O, `r2/fl_jumps`]: holding grey PgUp, every typematic repeat
  sends E0 49; the E0 clears the jump bit (DS:9257 path) until the 0x49 sets it again, so a game
  tick can see `g_keys` = 0x08 for one tick. Keypad keys (no prefix) do not do this.
* The jump bit set by a diagonal lasts until the next keyboard interrupt of any kind; Up and Down are
  removed from `g_keys` when a diagonal forms, so after the jump Up stays off until Up is pressed
  again (only the last key pressed repeats).
* `Player_Update` writes the held-key bits masked by the terrain back into `g_keys`, so a key the
  terrain forbids is dropped at the next tick and only comes back with a new make code (a
  typematic repeat). DOSBox-X's emulated keyboard repeats after **500 ms** (24 ticks measured) at
  about **30 per second** (every 1.5 ticks) [O, `r2/fl_typematic`, `r2/fl_shack_left`]. Hence
  "Up, then Right" does not jump (Up was dropped before Right arrived) while "Right, then Up" does
  [O, `r2/fl_jumps`].
* Shift is passed to the BIOS like any key; shifted letters reach the game as capital ASCII (the
  name box rejects only shifted A, see `extracted/scratch/dos_requests/ui.answers.md`) [O].

### 5.4 `Kbd_CtrlBreakCb` 43a0:02AB → 1 [O]

Ctrl-Break callback: returns 1 (continue; Ctrl-Break is ignored).

---

## 6. Segment 4a85 (input.c)

### 6.1 Contexts and tables [O]

`g_inputContext` DS:922C selects three far-pointer tables (DGROUP pointers, segment 0x3D94 in the
file): zones DS:80DB[ctx], key list DS:80F3[ctx], command list DS:810B[ctx]. Contexts and writers:
0 play (1437:0237, 43cb:006F/01CA, 153e:0058, 46ce:00A3), 1 menu (43cb:0261, 12E1), 2 dialog
(43cb:12F6), 3 quiz (46ce:0023), 4 name entry (43cb:12EE), 5 (zones only, a 2-zone screen)
[I: message/hint bubble].

* **Key list** (u16 count n, then n BIOS key words scan<<8|ascii) and **command list** (n+1 s16,
  entry 0 = −1 "no command"). Contexts 0, 3 and 5 share DS:7D84/7DA0 (13 keys): Esc 011B -> 0,
  F1 3B00 -> 1, F2 3C00 -> 2, F3 3D00 -> 3, Left 4B00 -> 4, Right 4D00 -> 5, Up 4800 -> 6,
  Down 5000 -> 7, Enter 1C0D -> 8, Alt+M 3200 -> 12, `s` 1F73 -> 13, `S` 1F53 -> 13,
  Ctrl+S 1F13 -> 14. Context 1 (DS:7DBE/7DD2): the first 9 of these. Context 2 (DS:7DE6/7DFE, 11
  keys): Esc -> 0, Left -> 4, Right -> 5, Enter -> 8, Tab 0F09 -> 4, Up -> 4, Down -> 5, `Y` -> 14,
  `N` -> 15, `y` -> 14, `n` -> 15. Context 4 (DS:7E16/7EA2, 69 keys): Esc -> 0, Up -> 6,
  Down -> 7, Enter -> 8, Space -> 32, Ctrl+R 1312 -> 30, Backspace 0E08 -> 31, `0`-`9` -> 48-57,
  `A`-`Z` -> 65-90, `a`-`z` -> 97-122. Keys not in the list give no command.
  Command 12 (Alt+M) has no consumer; command 15 is also what `Game_HandleHotkeys` treats as the
  slow-CPU toggle in area 6 (`code_ui.md`).
* **Zones**: byte count n, then n 8-byte records at +1: x1, y1, x2, y2 (x in bytes 0..159, y rows),
  then the command for hover, press, held, release (s8, −1 none). Context 0 (DS:7F2E, 11 records):
  #1-3 the menu titles (x 0-25 / 26-51 / 52-84, y 0-10) -> 1/2/3 on press, held and release;
  #9 the play-field (x 0-159, y 16-143): press 9, held 9, release 10; #10 the status bar (y 146-199):
  press 11, held 11, release 10; #0, #4-#8 empty (x2 < x1 placeholders). Contexts 1-4 hold
  rectangles that the menu, dialog and name-entry code fill in at run time (records with x1 =
  200 are placeholders) [O static data; dynamic contents `code_ui.md`]. Context 5 (DS:80CA): two
  zones covering the screen: y 0-189 release -> 8 (Enter), y 189-199 release -> 13.

### 6.2 `Input_Poll` 4a85:000E (from Input_Tick, and from 153e:0000) [O]

```
cmd = -1
if bioskey(1):                                        (a key is waiting)
    DS:9285 = bioskey(0)
    cmd = commands[ctx][ Input_LookupKey(DS:9285, keys[ctx]) ]   (index 0 = -1)
else:
    DS:9285 = 0
    cmd = (signed byte) Input_HitZone(zones[ctx])
if DS:9238 != 0 or g_uiMode == 0:                      (deliver)
    if DS:90F6 != -1: g_cmd = DS:90F6; DS:90F6 = cmd   (a queued command goes first, the new one queues)
    else:             g_cmd = cmd
    if g_cmd != -1: DS:9238 = 0
elif cmd != -1 and cmd != g_cmd:                       (UI mode, previous command not consumed: queue)
    DS:90F6 = cmd
```
At most **one BIOS key per game tick** is consumed (both paths of Game_Tick call Input_Tick); mouse
zones are only polled in ticks without a key. `g_cmd` is consumed (set back to −1) by its user
(menu, dialog, quiz, sign-in, `Input_MouseWalk`, `Game_HandleHotkeys`, `Input_Tick`). DS:9238 = 1
is set by UI code that wants the next command at once (1460:0342, 153e:0075/0120, 43cb:0126/0452,
46ce:0265, 47c7:00E0/08A9, 49a6:0118) and cleared by consumers (46ce:0035, 47c7:0F93, 49a6:0048).

### 6.3 `Mouse_ChangedMask` 4a85:00CD → bits [O]

1 if x changed, 2 if the **previous y differs from the current x** (`DS:9266 != DS:911A`: a bug),
4 if the buttons changed. No caller was found [I: unused].

### 6.4 `Input_LookupKey(key, far list)` 4a85:0107 → index 0..n [O]

Searches the list from the last entry down to entry 1; returns the first index whose word equals
the key, or 0.

### 6.5 `Input_HitZone(far zones)` 4a85:0130 → command (s8) [O]

Records are tested from the last (n−1) down to 0; the first record with x1 <= mouseX <= x2 and
y1 <= mouseY <= y2 (unsigned bytes vs the words DS:911A/911E) wins. Command by button history
(previous DS:9143, current DS:90F4): previous != 0 and current == 0 -> release (+8); previous == 0
and current != 0 -> press (+6); previous != 0 -> held (+7); else hover (+5). No zone -> −1.
Any button counts (non-zero).

### 6.6 `Input_MouseWalk` 4a85:0208 (per executed game tick, before Player_Update) [O]

```
px = DS:8FA7 + 2 (player screen x, bytes); py = DS:8FA9 + 0x10 (screen y of the feet)
if g_uiMode != 0: return
if DS:926C == 0:                                       (normal play; 926C = 1 during scripted moves)
    if (DS:926A & 3) == 0:                             (terrain mask: no Up/Down here)
        DS:90F8 = 4                                    (prompt "Press Down to drop coin")
        if g_keys & 2: g_keys = (g_keys & 0xFC) | 0x40 (Down becomes "drop a coin"; clears Up/Down)
        else:          g_keys &= 0x00BF               (clears 0x40 and the whole high byte)
    else:
        if g_cmd == 11: g_cmd = -1                     (status-bar click ignored where Down climbs)
        DS:90F8 = 0                                    (no prompt)
if 4 <= g_cmd <= 7: g_cmd = -1                         (arrow-key commands are not used in play)
if g_mouse != -1: return
if g_cmd == 9:                                         (play-field pressed or held)
    g_keys &= 0xFFD0                                   (clear Up/Down/Left/Right/jump)
    mx = DS:911A; my = DS:911E                         (unsigned 16-bit compares throughout)
    if py-0x2D <= my <= py:                            (beside him)
        if px-3 > mx: g_keys |= 4;  if px+3 < mx: g_keys |= 8
    elif px-10 <= mx <= px+10:                         (above or below him)
        if py-0x2D > my: g_keys |= 1;  if my > py: g_keys |= 2
    elif px-10 > mx and py-0x2D > my: DS:9257 = 1; g_keys |= 0x24   (up-left: jump left)
    elif px+10 < mx and py-0x2D > my: DS:9257 = 1; g_keys |= 0x28   (up-right: jump right)
    elif px-10 > mx and my > py: g_keys |= 0x14                       (down-left: net left)
    elif px+10 < mx and my > py: g_keys |= 0x18                       (down-right: net right)
    g_cmd = -1
if g_cmd == 11: g_keys |= 0x40; g_cmd = -1             (status bar pressed: drop a coin)
if g_cmd == 10: g_keys = 0; g_cmd = -1                 (button released: stop)
```
Distances are in bytes (2 px) horizontally: "beside" = more than 3 bytes (6 px) left/right within
the 45 rows above the feet; "above/below" = within ±10 bytes (20 px). Because the compares are
unsigned, `px − 10` or `py − 0x2D` below zero wrap to large values (player near the left/top edge:
the "left" and "above" branches win) [O]. The keyboard and the mouse write the same `g_keys`;
mouse bits stay set while the button is held (command 9 each tick) and are cleared by the release
command 10. Note `g_keys &= 0x00BF` also clears the high byte, which `Player_Update` uses for the
terrain mask copy [I].

### 6.7 `Input_Tick` 4a85:03AC (per game tick, both paths of Game_Tick) [O]

```
Input_Poll()
if g_cmd == -1 or g_uiMode != 0: return
if DS:926C != 0:                                       (scripted move in progress)
    if 1 <= g_cmd <= 3 or g_cmd > 11: g_cmd = -1      (no menus; 0 = Esc and 4..11 kept)
    return
switch g_cmd (0..3):
  0 (Esc): if g_slowCpu == 1: g_uiMode |= 0x20; DS:9323 = 10       (dialog 10 = Exit and Save box)
           else: g_uiMode |= 0x40; g_cmd = 8; DS:90FC = 1; DS:9111 = 2   (menu bar with a queued Enter)
  1, 2, 3 (F1/F2/F3 or a click on a title): g_uiMode |= 0x40; DS:90FC = -1; DS:9111 = 0
```
Esc in play showed the Exit-and-Save box directly in the captures (fast CPU), i.e. the queued
command 8 selects it at once through the menu code (`code_ui.md`) [O capture, I path].

---

## 7. Data tables used by these modules

| DS | Layout | Use |
|---|---|---|
| 0100 | u16 | `g_champFlashTimer` (RankChest_Update) |
| 0102 | u16 | full-screen picture pending (Screen_DrawOverlay); 37C8/37CA/37CC its frame data |
| 0104 | far[3] | screen painters by `g_screen`: Hud_Redraw, Quiz_DrawScreen, SignIn_Draw |
| 0110 | u16 pairs (threshold, fill) ×10, then 0xFFFF | rank chest fill (§3.13); e.g. (5,1) (25,8) ... (400,56) (9999,66) (9999,86) |
| 013A | char[] | "CHAMPION" |
| 036E, 03C9 | char[0x5B] each | disk-1 / disk-2 directory, default "A:\TLC\sst\" |
| 0476 | 19 × 22 bytes | file table: name[13], disk u8 (+0x0D), 8 unused; index list `fmt_data.md` §1 |
| 0618..061B | u8 ×4 | saved video mode, saved 0040:0504, ssrnet flag, saved keyboard flags |
| 0620 / 0629 | char[9] / u8[8] | `-S` letters "iItTaAsS" / devices 0 0 1 1 2 2 2 2 |
| 0631 | far[3] | {null, DS:036E, DS:03C9} by disk number |
| 063D, 0693, 070B | CR/LF text | fatal messages: memory, graphics adapter, network |
| 06E6, 06F3, 06FE | char[] | "Program Disk", "ssrnet.dat", "ssrquiet.dat" |
| 7D84 .. 7F2D | key lists / command lists | §6.1 |
| 7F2E, 7F87, 8010, 8069, 6194, 80CA | zone tables (count byte + 8-byte records) | §6.1, contexts 0..5 |
| 80DB / 80F3 / 810B | far[6] each | zone / key / command list per context |
| CS 1460:04C2 / 04DE | 14 words / 14 near ptrs | switch letters / handlers |
| CS 43a0:00DC / 00FE, 019F / 01C1 | 17 words + 17 near ptrs each | Kbd_ISR release / press tables |
| CS 1000:049D | far | saved INT 08h vector |

---

## 8. Globals (owned or latched here)

| DS | Name | Meaning |
|---|---|---|
| 1309 | g_inGameTick | ISR re-entrancy guard for Game_Tick |
| 3D2B | g_ticks | u32 PIT interrupt counter |
| 3D31 | g_slowScroll | 1 = double scroll steps (= g_slowCpu at start) |
| 3D33 / 3D34 | — | BIOS-chain (12..0) and game-tick (4..0) countdowns, bytes |
| 37E2/37E4 | g_player (+0 x, +2 y) | player world position (pixels) |
| 37EA/37EC | g_cameraX/Y | camera (pixels; low 3 bits ignored by the scenery) |
| 37EE/37F0 | — | camera delta of the last Player_Move (x bytes, y rows) |
| 37F2..37F8 | — | frame latches: camera x, y, delta x, delta y |
| 8F9F..8FA2 | — | frame latch of the tunnel-seen flags DS:9201..9204 |
| 8FA7/8FA9 | — | player screen x (bytes) / y (rows) of this tick |
| 8FB2 | — | frame latch of g_transition (dissolve) |
| 8FBA | — | set when the play-field was fully redrawn (read by ShelfRoom_DrawShelf 4a03:0775) |
| 8FBB | g_fullRedraw | frame latch of DS:924F |
| 8FBD | — | frame latch of the rank state DS:925D |
| 8FBE | g_loadedScene | scene in memory (0xFF none) |
| 8FC8 | — | frame latch of DS:9278 (partial redraw around the searched cell) |
| 8FCA | g_sceneWanted | frame latch of g_level |
| 90F4 / 9143 | — | mouse buttons now / previous tick |
| 90F6 | — | queued command (−1 none) |
| 90F8 / 91FB | — | prompt frame selector / its last drawn value |
| 90FE | — | 12..0 tick cycle |
| 9100 | — | 60-tick countdown of g_seconds (byte) |
| 9105 | g_keys | held-key bits; high byte = terrain mask copy (Player_Update) |
| 910D | g_stepDone | step gating flag |
| 9115 | g_seconds | hint "seconds" (60 game ticks) |
| 9119 | g_uiMode | bits: 0x80 title, 0x40 menu, 0x20 dialog/help, 0x10 hint, 8 sign-in, 4 message, 2 quiz |
| 911A / 911E | — | mouse x (0..159) / y (0..199); 925E/9266 previous |
| 9122 | — | coins as displayed (count-up) |
| 912A | g_mouse | −1 driver present |
| 912C | g_videoType | 1 EGA, 2 CGA, 3 Tandy, 4 Hercules, 5 MCGA, −1 none |
| 912E | g_quit | leave the main loop |
| 9124/9126/920D/920F | — | camera block column / block row·16 / tile column·32 / tile row·2 |
| 9217 | g_transition | dissolve request (1 or 2) |
| 922C | g_inputContext | 0 play, 1 menu, 2 dialog, 3 quiz, 4 name entry, 5 two-zone screen |
| 922E / 925B | — | HUD full-redraw request (1 or 2) / extra request ORed in at Frame_Begin |
| 9226 | g_cmd | current command (−1 none) |
| 9238 | — | deliver the next command immediately |
| 923C | g_slowCpu | slow-CPU mode |
| 924F | — | full redraw request |
| 9255 | g_dirty | something was drawn this frame |
| 9257 | — | jump bit came from a diagonal / mouse jump |
| 9268, 924D, 9259, 928D | — | HUD caches (key state, nets, found, coins) |
| 9270/9272/9297/9299/929B/929D | — | last drawn camera (block col, block row, x, col offset, y, row offset) |
| 9278 | — | partial redraw request |
| 9283 | — | sprite slots drawn this frame (13 or 1) |
| 92A6/92A8/92B0 | — | rank chest rows done / rows wanted / last tick phase |
| 92AC / 92AE | — | mouse pointer visible / idle tick counter |
| 92B2 | — | floppy count |
| 92C1/92C3/92C5 | — | critical error flag / errval / AX |
| 92FA, 92FC | — | Ctrl flag (unused) / saved INT 09h vector |
| 9136 | — | start-up drive |

---
## 9. Timing reference (DOSBox-X, cycles=fixed 6000) [O]

Tick numbers come from the per-tick logger (exact); AVI numbers are 59.9228 Hz frames. "State n"
is `g_playerState` DS:913C.

| What | Driven by | Measured | Source |
|---|---|---|---|
| game tick | PIT / 5 | 47.3395 Hz = 21.124 ms | ISR, every log |
| walk step (8 px) | player script | **41 ticks per 8 steps**: seven of 5 ticks, one of 6 (5.125 ticks = 108.3 ms); state 11/12 one tick after the key bit, first step 9 ticks later | `r2/fl_walk2` |
| horizontal scroll step (8 px) | the walk step | one per walk step once the player is past the edge (screen x <= 0x20 bytes moving left, >= 0x7C moving right); AVI 6-7 frames (6.54 mean) | `ega_r2_jumps_path`, `fl_walk.avi` |
| climb step (8 rows) | player script | **29 ticks per 4 steps** (7, 7, 7, 8) = 7.25 ticks = 153 ms; AVI 9.2 frames | `r2/fl_climb` |
| vertical scroll step | the climb step | one per climb step while the camera follows | `r2/fl_climb` |
| jump (tap) | player script | state 19 (right) 43 ticks + state 20 15 ticks = 58 ticks (1.23 s); 7 steps of 8 px: first 8 ticks after the jump state, then every 7 ticks (one 8) | `r2/fl_actions`, `r2/fl_jumps` |
| jump (key held) | typematic | chained jumps without landing while PgUp repeats | `r2/fl_jumps` |
| net swing (Space) | player script | state 24 for 50 ticks, 25 for 1, 26 for 29: **80 ticks = 1.69 s** | `r2/fl_actions` |
| coin drop, nothing there | player script | state 28 for 71 ticks then state 5 for 30 ticks (2.13 s); throw pictures every 7 ticks | `r2/fl_typematic` |
| search (blink and reveal) | **frames** | 37 frames (57 -> 0, §10); DOSBox-X 38 frames in 693 ms (17-21 ms each) + a 97 ms final redraw | `r2/fl_search_t`, `ega_r2_treasure_found.avi` 154-198 |
| HUD coin count-up | **frames** | one coin per frame; DOSBox-X 13.1 ms per coin (20 coins in 262 ms) | `r2/fl_countup_after` |
| rank chest fill | game tick | one row per 4 ticks (84.5 ms), at most one per frame | `ega_r2_end_t2_nokey.avi` 1782-1812 |
| CHAMPION flash | **frames** | 800 frames, a toggle every 80; DOSBox-X 640 frames in 0.70 s (~914/s) | `ega_r2_crown_nokey.avi` 3042-3084 |
| screen dissolve | one frame | 350 cells inside one Frame_Present: 127 ms (7-8 AVI frames) | `r2/fl_l2door_after` |
| door (mine doorway) | player script | Up at tick 0, state 13 at +1, two 8-row moves at +14 and +23, teleport at +24, state 9 for 8 ticks | `r2/fl_l2door_after` |
| geyser-hole slide | player script | +8 rows every 7-8 ticks (7, 7, 8) | `r2/fl_geyser_slide` |
| bridge fall (tier >= 2) | player script | +8 rows every 5 ticks | `r2/fl_bridge_l3` |
| elf picture | elf scripts | ~9.1 AVI frames (~7.2 ticks) | REFERENCE_CAPTURES §5.5 |
| attract page | `TitleStep_Wait` | 0x1C20 = 7200 PIT ticks = 30.42 s (AVI 1826 frames) | REFERENCE_CAPTURES §5.1 |
| hint "second" | game tick | 60 ticks = 1.2675 s, paused while `g_uiMode` != 0 | §3.3 |
| mouse pointer hide | game tick | 600 executed ticks = 12.7 s | §3.12 |
| keyboard repeat | DOSBox-X keyboard | delay ~500 ms (24 ticks), then ~30/s | `r2/fl_typematic` |

---

## 10. Per-frame features and the frame rate

Features counted in **frames** (main-loop iterations) rather than game ticks, with their counter
and the DOSBox-X result. Everything else in these modules runs per game tick.

| Feature | Counter (per frame) | Count | DOSBox-X duration | Owner doc |
|---|---|---|---|---|
| search blink/reveal | `g_searchTimer` DS:8FA3 −1 (−2 on odd values < 0x2A, −0x20 once on a slow CPU) | 37 frames | ~0.79 s | code_player |
| HUD coin count-up | DS:9122 ±1 (+ effect 4 per coin) | n frames | 13.1 ms per coin | this doc §3.8 |
| CHAMPION flash | DS:0100 −1 (−4 slow CPU) | 800 frames | ~0.9 s | this doc §3.13 |
| screen dissolve | 350 Present_Dirty calls in one frame | 1 frame | 127 ms | this doc §3.4 |
| title sparkles | one start attempt per frame, chance 1/128 | — | ~1690 attempts/s on idle pages | code_ui |
| UI updaters (sign-in, messages, hints, menus, quiz) | per frame | — | event-driven, frame rate invisible | code_ui |

The search is the only per-frame feature whose DOSBox-X frames are expensive enough (a partial
play-field redraw each frame, ~19 ms) to land close to one frame per game tick; the count-up and
the CHAMPION flash ran much faster than the game tick there, and the dissolve's duration is pure
CPU cost.

---

## 11. Function index and coverage

| Seg | Functions specified / in module |
|---|---|
| 1000 (timer part) | 3 / 3 (`Timer_Install`, `Timer_Remove`, `Timer_ISR`), plus `Hw_Detect` and `Video_DetectAdapter` as context |
| 130f main.c | 14 / 14 |
| 1460 init.c | 21 / 21 |
| 43a0 kbd.asm | 4 / 4 |
| 4a85 input.c | 6 / 6 |

Rename proposals: none needed; the names in `re/names/dos/` fit the behaviour. The names proposed for
unnamed globals are canonical in `re/names/dos/dgroup.tsv` since the wave 2 merge.

---

## 12. Porting notes

Engine structure (`src/dos/app/dos_runtime`, `src/dos/game/main_loop`, `src/dos/input/`,
`src/dos/game/init`; see also `PORTING_NOTES.md`):

* **Virtual PIT.** Accumulate host time and convert it to PIT ticks at 1193181.67 / 5041 Hz
  (`ticks = floor(elapsed_us * 1193182 / 5041 / 1e6)`, keep the remainder). Every PIT tick: the
  sound sequencer (better: run it in the audio callback by sample count, `fmt_sound.md` §9);
  every 5th: one game tick. No BIOS chain.
* **Frame loop in lockstep.** DOSBox-X ran 50-1700 loop frames per second and practically never
  blocked a game tick, so the reference behaviour is "every game tick executes its logic step". The
  simplest exact model: for each game tick, run `Frame_Begin` semantics (latch, clear
  `g_stepDone`), the tick (`Game_Tick`), then the frame body (`main` loop body without the present),
  and present the back buffer at the display refresh. This keeps DS:910D's meaning (it can never
  block) and removes the original's ISR races. Keep the gate implemented for a "slow machine" option.
* **Per-frame features** (§10): with one frame per game tick the search lasts 37 ticks = 0.78 s
  (DOSBox-X 0.79 s, good), the count-up 21 ms per coin (DOSBox-X 13 ms) and the CHAMPION flash
  16.9 s (DOSBox-X 0.9 s). Recommended: keep the search per frame, run the count-up and the
  CHAMPION flash from an "original frame clock" at the DOSBox-X rates of §9 (76 coins/s, ~914
  flash frames/s), and play the dissolve over 127 ms (350 cells: draw them in 6 slices of ~58).
  Title sparkle attempts: see `code_ui.md` (the DOSBox-X rate is ~1690/s on idle pages).
* **Order inside a game tick** must stay: (if not gated) screen position, `Mouse_Poll`,
  `Input_Tick`, `Anim_Tick`, then if the scene is loaded `Input_MouseWalk`, `Player_Update`,
  `Elves_Update`, `Cells_UpdateVisible`; then the two counters. The rand() sequence depends on it.
* **Rand interleaving.** In the original, main-loop rand users (dissolve: 700 calls per
  transition; sparkles; quiz answer order; layout) and tick-side users (elves, creature) interleave
  by timing. Lockstep makes this deterministic; exact replay of a DOSBox-X run is not possible
  anyway, so pick "frame body after the tick" and document it.
* **Scenery** is a pure function of the latched camera with the low 3 bits of x and y dropped
  (`(cx & ~7, cy & ~7)`), drawn from `g_cellPicture` blocks; sprites use the exact camera. A full
  redraw every frame reproduces the incremental scroll, except the slow-CPU band (rows 112..143 not
  scrolled with `-X`) and pictures cell handlers paint into the back buffer (redraw them at their
  world position each frame).
* **Keyboard.** Keep two inputs: held bits (`g_keys`) from key down/up events with the Kbd_ISR
  table (Home/PgUp, Tandy codes, diagonal -> jump, the jump bit dropped at the next key event), and
  a BIOS-style queue of 16-bit key codes fed by key down **and typematic repeats** (500 ms, ~30/s,
  only the last key pressed repeats; synthesise repeats in the engine rather than using the host's
  repeat rate). `Input_Poll` takes one code per game tick. The terrain clears forbidden held bits,
  so a repeat is needed to bring them back; "Up then Right" therefore does not jump.
  Optional quirks: the grey-key E0 break artefact (§5.3) — drop by default; NumLock/CapsLock
  forcing — drop.
* **Mouse.** Mouse x is in bytes (0..159 = 2-px steps), y in rows (0..199). Map the host pointer
  into the 320x200 logical screen and divide x by 2. Zone commands are evaluated only in ticks
  without a key. `Input_MouseWalk`'s unsigned compares near the screen edge are part of the
  behaviour.
* **Options from switches** (`ParseCommandLine`): video look `ega|cga|tandy|mcga` (Hercules = CGA
  look), sound `speaker|tandy|adlib` (`-Si/-St/-Sa`), `slow_cpu` (`-X`: 7 elves, double scroll
  with the bottom band quirk, shorter search, CHAMPION −4, Esc -> Exit box). Auto-detection maps to
  EGA + AdLib on a modern PC in DOSBox-X; for the engine the defaults should be EGA look and Tandy
  or speaker sound (AdLib needs an OPL2 core).
* **Files.** All data from the game directory (the directory of SST.EXE); the disk table DS:0476 and
  its disk numbers only matter for floppies; ignore TLCINFO.DAT, ssrnet.dat, ssrquiet.dat. Read
  errors: report and stop (no retry dialogs).
* **Frame-rate dependencies to keep in mind** when the engine is paced differently: the search
  length, the count-up speed, the CHAMPION flash, the dissolve, the title sparkle rate; everything
  else is tick-based.

---

## 13. Open points

* The cause of the long Game_Tick at the level 0 -> 1 boundary during the trunk climb (`r2/fl_climb`,
  ~4 ticks): which handler is slow is not identified; irrelevant for a port except as a 4-tick
  hiccup in the reference recording.
* `Mouse_ChangedMask` has no caller in the export; the mouse could not be driven in DOSBox-X
  (tmctl cannot inject guest mouse input without the host mouse), so §3.12 and §6.5-6.6 are from
  code only.
* Context 5's two zones (DS:80CA) and DS:0102/37CA (full-screen picture request): users not traced
  here (`code_ui.md`).
