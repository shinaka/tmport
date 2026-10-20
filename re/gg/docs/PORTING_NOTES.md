# Porting notes: Gizmos & Gadgets in the tmport engine

**Status: wave 3.** Written in wave 0, extended at the wave 1 integration from the topic specs and
the round 2 reference captures, corrected at the wave 2 integration from the engine foundation and
at the wave 3 integration from the game logic and its playthroughs (what was built and verified:
[ENGINE_MAP.md](ENGINE_MAP.md)). It records what matters for the engine (`ggport`, `src/gg/`):
the runtime model, the screen, the clocks, the order of `rand()` calls, the CPU-dependent path, why
the overlays do not matter, the original's switches as options, input, saves, the UI library, and
which quirks to keep.

How the original DOS program (`ssg.exe`, Borland C++ 3.0, 1993) maps onto the engine. Read
[SCOPING.md](SCOPING.md) (plan, decision) and [ARCHITECTURE.md](ARCHITECTURE.md) (program, flow,
platform layer) first; the file formats are in [fmt_data.md](fmt_data.md),
[fmt_graphics.md](fmt_graphics.md) and [fmt_sound.md](fmt_sound.md). The algorithms are in the
wave 1 topic specs:

| Doc | Modules |
|---|---|
| [code_runtime.md](code_runtime.md) | clock and timer ISR (1454), start-up (303a, 2c08), `Seq_Run`, exit paths, deadline timers (152c), keyboard, key ring, `Input_GetCode` (198d), mouse handler (17a0), cursor and zones (17ab), primitives (1399), screen, palette, fades, wipes, movie (1473), dirty tiles and sprites (1581), picture cache (15df), memory and archives (1536, 14ff), overlays (1d42) |
| [code_sound.md](code_sound.md) | the BMF driver (1bb4), the bank layer (1cdd), the music cue callbacks (12e0) and every sound call site |
| [code_ui.md](code_ui.md) | fonts (2b16), text (2b89), dialogs and buttons (3f56, 4034, 4106), scroll bar (406a), menu bar (40a4), help (411d), text viewer (4140), attract (2c11), sign-in (2caa), `PLAYERS.DAT` (2b5a), hints (3060) |
| [code_world.md](code_world.md) | states 3, 4, 5, 7, 8, 10, 11, 16; rooms (1622), items, actors (1834), player and banana (18f5), collision (1adb), Cyber Chimps (2a80), screen animations (174b), lists (19c0) |
| [code_vehicles_races.md](code_vehicles_races.md) | parts (153f), the HUD (1a04, master copy), workshop and blueprint (2dee), races (3dd6, 3e77, 3f50), results, post-race menu and final movie (2fcb) |
| [code_puzzles_a.md](code_puzzles_a.md), [code_puzzles_b.md](code_puzzles_b.md) | state 9 and the puzzle framework (38e2, 3924), electric (3083), energy (33cd), launch (3797), simple machines (3938); machine pieces (3a4a), gears (3b2f), balance (32fe), magnets (35bb) |
| [REFERENCE_CAPTURES.md](REFERENCE_CAPTURES.md) | DOSBox-X reference frames, AVIs, audio, save states (rounds 1 and 2) and their measurements |

Tags: **[O]** observed (code, bytes, decoded data, captures), **[I]** inferred, **[R]**
recommendation of this document. Addresses as in ARCHITECTURE (conventions).

---

## 1. Inputs and validation [R]

| File | Needed for | Check |
|---|---|---|
| `ssg.exe` | DGROUP tables and strings: the `Seq_Run` tables, zone tables, menu records, chimp lists, puzzle counts and tables, race tables, rank table and names, start-up and UI words, the other tables the specs cite by address; a few tables are code (the energy-forms variants, the electric terminal geometry), read from the overlay bytes | 213 200 bytes; MZ; TLINK byte 0x50 at file 0x1C; "Borland C++ - Copyright 1991" at file 0xFC74 (DS:0004); `FBOV` at 0x1BA00 with the segment table at 0xF030 |
| `gizmo000.dat` .. `gizmo003.dat` | every picture, palette, font, text, hint, sound bank, DAM script, game table and the movie | `MR0`..`MR3` + 0x1A; offset table ends at file length − 4; last entry `I` stored `5A A5` (the original checks volumes 0-2 only, 14ff:0167) |
| `PLAYERS.DAT` (optional) | import of the roster and records | 578 487 bytes, `USR` 0x1A, version word 12 |
| `gizmo004.dat` | — | ignore: a screen save-under the original writes at run time (ARCHITECTURE §4.8) |
| `ssg.bat`, `Read_1st.txt`, `ssg.ico` | — | ignore |

* The game folder is found through `GG_DATA` for tests (default: the repository's
  `GadgetsAndGizmos\` folder) and through the command line or settings for the game. The engine
  never writes into it.
* Far pointers in DGROUP hold image segments (0x0ea7 = DGROUP, others = code or stub segments) and
  must be resolved like TM Windows' relocated pointers [O]. Code pointers to stub entries (the state
  tables, menu handler tables, the menu hook DS:865a = 1e94:0025, the puzzle drive table DS:3902)
  are mapped to engine functions by their stub entry or address (§6). Near pointers in DGROUP
  tables (the player transitions DS:127e, the chimp move tables DS:1d2e / DS:1f10) are DGROUP
  offsets.
* Tables are read at run time, as the TM DOS engine reads SST.EXE; the specs give addresses and
  formats, never contents. No game value goes into the repository.
* Other releases of the game (other versions, Windows or CD editions) are refused with a clear
  message when a size or magic check fails (SCOPING §8 risk 8). A missing file is named.

---

## 2. Runtime model

### 2.1 What the original does [O]

* PIT channel 0 at 3 579 545 / 3 / 8192 = **145.652 059 Hz** (6.866 ms); the ISR increments the u32
  tick DS:021a and runs the sound driver on every second (even) tick (**72.826 Hz**), after the
  tick increment. No game logic runs in the interrupt (`code_runtime.md` §1). The only things that
  run at interrupt time are the sound driver (with its cue callbacks), the keyboard handler and the
  mouse handler (which flushes the screen when the pointer moves).
* The game is a tree of ordinary nested loops: `Seq_Run` calls a state function, which runs modal
  loops, which call dialogs with their own loops. The frame loops pace themselves on a deadline
  timer: `Timer_Set(0, 4)` … draw, flush … `Timer_Wait(0)`, `Input_GetCode`, dispatch. **4 ticks
  per frame = 36.41 frames/s; races 8 ticks = 18.21 frames/s** (ARCHITECTURE §3.3).
* **Unpaced loops** poll `Input_GetCode` as fast as the machine runs and draw only on change: the
  eight puzzle main loops, the post-race menu, the blueprint viewer, the dialog and menu loops,
  `Input_WaitCode` and several busy-waits on sound or on the mouse (`code_puzzles_a.md` §2.3,
  `code_vehicles_races.md` §16).
* Measured on the round 2 AVIs (REFERENCE_CAPTURES §10.1): count-up 3.94 ticks per update, races
  7.94, warehouse walk animation 12.0 (3 frames), hub windmill 16.1, movie 18.0; no overrun was seen
  in those spans.
* No catch-up: a frame whose work takes longer than its ticks makes the game slower [I].
* `Timer_Set` takes a u16 count; `Timer_Wait` and `Delay` end at tick ≥ deadline; `Timer_Expired`
  only at tick > deadline (152c:0029 / 0072 / 004a), so a `Timer_Set(n)` followed by an Expired
  loop lasts n + 1 ticks.
* `Delay(n)` calls `Input_GetCode` while it waits and discards the codes; global keys (Ctrl+S, Esc,
  the menu keys) still act inside it [O].
* **Time stands still in dialog boxes**: the dialog loop (3f56:0655) restores the tick it had at
  entry when it closes; the help pager is a series of dialogs and freezes time too. The menu
  drop-down and the puzzles' text viewer do not.
* The sound driver is paused (locked) by dialogs and drop-downs; a pause is not nestable, so a
  drop-down reopened after a dialog runs with sound (`code_sound.md` §12.1, `code_ui.md` §2.2).
* The mouse is serviced at interrupt time: the INT 33h event handler latches buttons and redraws
  the cursor (a dirty-tile flush) even while the main loop waits, but only while no frame is
  drawing (§8).
* Overlays load from disk on demand (§6).

### 2.2 Engine model [R]

```
host clock (ns) --> virtual PIT: tick = floor(elapsed * 3579545 / (3 * 8192)) - frozen   (u32, as DS:021a)
                     '-- every even PIT tick, after the increment: Snd_Tick (rendered by sample count)
game code: nested functions on the main thread, as the original
   every wait primitive (Timer_Wait, Delay, the retrace waits of palette loads, the dialog, menu
   and puzzle polls, Input_WaitCode, the sound and mouse busy-waits) pumps the host: SDL events ->
   keyboard and mouse models, audio service, present at most once per display refresh
```

* This is the Windows edition's model (`docs/DESIGN.md` "Runtime model": nested blocking loops,
  every wait pumps the engine) with PIT ticks instead of milliseconds. It is **not** the DOS
  edition's lockstep runtime (`dos/app/dos_runtime`): G&G has no logic in the interrupt, no step
  gate and no per-tick game step.
* The dialog freeze is an offset (`frozen` above): on closing a box the engine adds the ticks spent
  inside it, so the counter reads as it did at entry and later deadlines keep their meaning.
* Deterministic runs (tests, frame dumps, headless tools) use a manual clock: a wait advances the
  clock to its deadline, and the work inside a frame costs 0 ticks unless a cost model says
  otherwise (§2.4). Cost models are expressed in PIT ticks, never in host time. An unpaced poll
  with no input advances the clock by a small fixed step so that busy-waits on sound end.
* Sound reuses the DOS edition's sample-count ticking, generalised in wave 2 as the core's
  `dos::ChipRenderer` and `dos::ChipAudioDevice` (`src/dos/audio/chip_renderer.h`,
  `chip_audio_device.h`; TM's `DosSoundRenderer` / `DosAudioDevice` are built on them): one driver
  tick per 2 PIT ticks, on even ticks (`code_sound.md` §17).
* As built in wave 2 [O]: the virtual PIT is the core's `timing/pit_clock.h` (`PitTimeBase(0x2000)`,
  `VirtualPit` with `onTick(every, fn, phase)` on the counter value and `setTicks` for the dialog
  freeze, `PitTickClock`) under `GgClock`; the runtime is `GgRuntime` (`gg/app/gg_runtime.h`) on
  `platform::Host`, not TM DOS's lockstep `dos::DosRuntime`. Its yield points are `timerWait`,
  `timerExpired`, `delay`, `getInputCode`, `waitInputCode`, `idle` and `waitRetrace`. In deterministic
  runs the first poll after a wait is free, every later poll moves 1/16 tick (capped at the tick
  edge), a wait that ends at once counts as a poll, and a script event of tick n comes right before
  interrupt n; every busy-wait loop must call `GgRuntime::idle()`. The display (`gg/app/display.h`)
  presents VRAM after every flush, wipe step and DAC write.
* Mouse events from the host are delivered as INT 33h events at PIT-tick times; in a 0-cost frame
  model every event during a wait sees the hide state 0, which matches the original except for
  events that fall inside a long frame's drawing (`code_runtime.md` §20).

### 2.3 Frame pacing details [O, R]

* A frame begins where the previous `Timer_Wait` returned, i.e. on a tick edge, so frames that fit
  are exactly 4 (or 8) ticks long. The first frame after a long operation starts wherever that
  operation ended [O].
* Keep the one-tick difference between `Timer_Expired` and `Timer_Wait` (TM Windows' `Timer_Expired`
  uses ≥). Done in wave 2: the core's `timing/deadline_timers` counts in its Clock's unit
  (`PitTickClock`: PIT ticks), takes `ExpiryRule::AfterDeadline` (`expired` strict >) and has
  `reached()` (≥) for `Timer_Wait` and `Delay` (`GgTimers`, `gg/app/timers.h`).
* Per-loop order of operations, which decides what a frame shows and when input is read, is in the
  specs: the warehouse (`code_world.md` §14.2: `Hint_Update`, `Timer_Set`, `Actors_Tick` — which may
  leave at once with no drawing, wait or input —, erase, layers, actors, items, draw, HUD, flush,
  wait, input), the hub (§3.3 there), the workshop (`code_vehicles_races.md` §5.4), the races (§9.5 and
  §10.5 there), the results (§11.2 there), the attract (`code_ui.md` §10.2).
* Other time bases to keep (ARCHITECTURE §3.4): 0x111C ticks (30.07 s) for attract pages and the
  idle bit; the title phases 1/3/5 s (`Timer_Set(1, n·146)`); hint delays 0x490 and 0x2238 ticks;
  the button flash (4 × `Delay(4)`); the results count-up (50 frames of 4 ticks); the movie at 18
  ticks per frame with `Delay` cues (10.06 s measured for `Delay(1460)`); the wipe at 2 ticks per
  step; the text viewer's auto-repeat (146, then 29 ticks); the sign-in scroll bar's (27 ticks); the
  puzzle animations (`code_puzzles_a.md` §7, `code_puzzles_b.md` §6).

### 2.4 Long frames and unknown costs [O, I]

On the reference machine some frames take longer than their 4 ticks: room entries (resource loads
through the LZH decompressor, the 90-cell item shuffles of 1800 `rand()` calls), full redraws,
overlay loads, the random wipe (paced by timer slot 2) and palette fades (paced by the retrace,
§3.3). Their costs decide when the next frame starts and so the timing of everything after them.

What round 2 measured in DOSBox-X (REFERENCE_CAPTURES §10):

* **Overlay loads cost nothing visible** there: DOSBox-X's file reads take no emulated time, and a
  first and a later entry to the Aircraft race show the same black gap. The engine ignores overlay
  timing against these references [O].
* The black gap between a fade-out and the next fade-in (loading and drawing the next screen) is
  7-18 video frames (100-257 ms): workshop → race 7, 8 or 14 frames by building, race → results
  7-11, post-race → hub 8, warehouse tube → workshop 18 [O].
* The random wipe of a door entry changed the screen for 45 frames (642 ms ≈ 93 ticks) [O].
* Frame periods hold in steady loops (§2.1).

Still unknown [I]: the per-operation costs (picture load and decode, full flush, wipe step, room
entry) as PIT ticks, which a resident-code frame logger would give (as `re/dos/docs/PORTING_NOTES.md`
§2.5 did for TM DOS); they matter for frame-exact comparisons, not for the game's behaviour.

---

## 3. Screen presentation

### 3.1 Geometry [O, R]

* Mode 13h, 320×200, shown at 4:3 (pixel aspect 6:5): `PresenterLayout::dos1990()` as is (integer
  5×6, square-pixel option).
* Frame buffer: 320×400 bytes of palette indices; rows 0..199 the composed screen, rows 200..399
  the back page. Sprites are erased by copying from y+200 (`Gfx_RestoreRect`), and a background
  drawn on the screen is saved into the back page with `Gfx_SaveToBack` (wave 0 called it
  `Gfx_CopyFromBack`, which reversed its meaning) [O]. A G&G frame-buffer class (or a
  parameterised `gfx/frame_buffer`) keeps this layout so that the original's coordinates work
  unchanged [R].
* Implement each primitive with its exact clipping (`code_runtime.md` §10): the exclusive
  right/bottom of `Gfx_ClipX/Y` and the wrong sign for a negative y, the unclipped `Gfx_HLine`
  draw, the keyed raw blit's unsigned y test and 8-bit height, the unclipped RLE blits (no dirty
  mark at negative x). Where the original would write outside the buffer, clip in the engine and
  log in debug builds [R]. Zero counts also run away in the original [O wave 2]: `Gfx_FillRect`,
  `PutImage`, `RestoreRect`, `SaveToBack` and `GetImage` with h = 0 (DEC DX; JNZ: 65 536 rows),
  `Gfx_VLine`, `XorRect` and the keyed raw blit with a length or width of 0 (LOOP: 65 536),
  `Dirty_Mark` with (y & 7) + h ≤ 0 and cols > 0, `Movie_DecodeFrame`'s long literal or run of length
  0 at an odd address (DEC CX before SHR: 65 535 bytes): the engine draws nothing there and logs in
  debug builds.

### 3.2 What reaches the screen [O, R]

* The original copies dirty 8×8 tiles from the RAM screen to VRAM once per frame without waiting
  for the retrace, so a capture can show a half-copied frame (round 2 saw updates caught on both
  sides of a retrace). The engine presents complete frames; fidelity checks compare settled frames
  [R].
* Two paths bypass the RAM screen: the random wipe copies RAM-screen columns into VRAM over several
  timer-paced steps, and the movie decodes straight into VRAM, leaving the RAM screen untouched
  [O]. So the engine keeps a separate 320×200 **VRAM surface** that the tile flush, the wipe and the
  movie write and the presenter shows [R]. After a wipe the dirty map is cleared without a flush,
  so tiles outside the wiped rows reach the screen only when marked again; pattern 0x11DA never
  copies column 319 [O, keep].
* The software cursor is drawn into the RAM screen at each flush and redrawn at interrupt time
  whenever the mouse moves; primitives first restore the background under it (`Gfx_HideMouseOnce`
  1399:0b22). The wipe hides it first, so the RAM screen it copies has no cursor; the old cursor
  image stays in VRAM until the wipe covers it, and the cursor returns at the next flush with dirty
  tiles. The movie runs with the cursor hidden (`Mouse_Hide` from 2fcb:0000) and never touches it
  [O, `code_runtime.md` §12.3, §14.2, §15]. The engine draws the cursor into the buffer at the
  flush as the original does; it composites nothing else [R].
* DAC colours are 6-bit. DOSBox-X shows them in 8 bits as `(v << 2) | (v >> 4)` (bit replication),
  **not** `(v*255 + 31) / 63` (the rule TM DOS's MCGA look uses, in TM's `src/dos/gfx/palettes.h`,
  which is not part of the core): they differ for v = 11..15 and 49..52 [O wave 2: per-pixel scoring
  of w0/01_title, w0/03_signin and four r2 puzzle screens; `gg/gfx/palette.h` `dacTo8`].

### 3.3 Palette loads and fades [O, R]

* A palette load (`Pal_SetRange` 1399:05d6) writes 128 colours at the start of a vertical retrace,
  then the other 128 at the next one: **2 retraces per load**. Mode 13h on VGA refreshes at
  25.175 MHz / (800 × 449) = **70.086 Hz** [O code, I rate]. The round 2 AVIs show every load as
  two consecutive frames [O].
* Fades: out = 12 loads (24 retraces, ~0.34 s), in = 9 loads (18 retraces, ~0.26 s); step sizes from
  `Pal_Fade` 1473:015c. The last load of each repeats the target, so the visible lengths are **22
  frames out and 16 in**, as measured on 10 transitions (`code_runtime.md` §13.4, REFERENCE_CAPTURES
  §10.2). Wave 0's "story fade-in 22 frames" was the title's fade to black, and its 19-frame span a
  fade-in after two redraw frames.
* Engine: a retrace clock on the same time base as the virtual PIT (retrace k at k / 70.086 s); a
  load waits for the next retrace start and becomes visible there. The PIT keeps running during a
  fade (only the dialog loop writes the tick counter), so fade time counts against deadlines [O, R].
* The phase of the retrace relative to the PIT at the start of a fade is a timing detail for the
  fidelity wave (§2.4).

---

## 4. Clocks and randomness

* **The BIOS clock runs 8× fast** while the game runs: the timer ISR chains the BIOS handler on
  every one of its 145.65 Hz ticks instead of every 8th (ARCHITECTURE §3.1) [O code, I intent]. The
  game reads the DOS time only once, for `srand`, right after it installs the handler, so the game
  is not affected; the DOS clock is wrong after the game exits. **Drop it** (and the double EOI)
  [R].
* Seed: `srand(time(NULL))` at 303a:0229 with the low word of `time()` (kept at DS:2454). The RTL
  seed is the u32 at DS:442a; `rand` is the Borland LCG (`seed·0x015A4E35 + 1`, bits 16..30) and
  every call site scales as `rand()·n / 0x8000` in 32 bits [O]. Reuse `rtl/borland_rand` [R].
* Engine: seed from the host time like the original; a fixed seed option for tests; fidelity runs
  take DS:442a from the DOSBox-X state they start from [R].

### 4.1 Every `rand()` call, in order [O]

There are 18 call sites (`CALLF 1000:06c1`). Seeded runs replay exactly only if the engine makes
the same calls in the same order, **including calls whose results are discarded**. One Borland LCG
is shared by everything.

| When | Calls, in order | Spec |
|---|---|---|
| building set-up, new visit (state 4) | `Parts_ShuffleDeck`: `Rand_Shuffle(I[0])` = 20·I[0] calls (2 per swap: 2bd4:0043, 0064); for each room r ≥ 1 with items: `Room_PlaceItems` → `Rand_Shuffle(cells)` = 20·90 (hall) or 20·21 (small room); the coin R(100) (12e0:0910); `Chimps_Spawn`, per chimp in list order: R(6) hall / R(3) small floor (2a80:0875 / 08a8), then R(100) side (08ee) | `code_world.md` §4.4 (checked: 3925 calls in the w0 capture) |
| building set-up, resume | the deck and item shuffles run and are discarded (no coin, no spawn) | `code_world.md` §4.2 |
| warehouse room entry (state 7 entry) | the entry `Actors_Tick` (as a frame, below), then, when not faded, the wipe: R(8) pattern (1473:0047), R(2) sound (1473:0513) | `code_world.md` §14.1, `code_runtime.md` §14.2 |
| warehouse frame | `Hint_Update` none; `Actors_Tick` in slot order 0..11: per chimp brain zero or more R(1000) (2a80:0212), one per probe that fires, in entry order, until one succeeds; then the step commands of the frame in order: player op −29 (hit by a chimp) R(carried) (18f5:01ac) and, if the chimps hold 2 of that type, an `Item_PlaceInRandomRoom`; player op −27 when the last banana goes and no crate is left: `g_cratesTotal` × `Item_PlaceInRandomRoom(1000)`; chimp op −10 on a crate: one `Item_PlaceInRandomRoom(1000)`; drawing none; `Input_GetCode`: code 6000 (the trash can) → `Item_PlaceInRandomRoom(part)` | `code_world.md` §16 |
| `Item_PlaceInRandomRoom` | R(roomCount − 1) + 1 (1622:0f5b) per draw, then that room's grid shuffle (20·cells) | `code_world.md` §7.4 |
| puzzle choice (state 9) | per kind tried: R(8) (38e2:01ed; a kind with nothing unsolved is retried); when at most 4 variants are unsolved in the whole game, R(variants) (022d); then R(k) for the candidate (0052), also when k = 1 | `code_puzzles_a.md` §1.3 |
| machine pieces, a take from the box | R(n) per try (3a4a:07a4) until an unplaced piece comes up | `code_puzzles_b.md` §2.6 |
| a solved puzzle | R(4) for the jingle (38e2:02a3) | `code_puzzles_a.md` §2.2 |
| screen animations | ops −34 / −35 (174b:00f1, 0116) would call R(b − a); no shipped set checked by the specs uses them (hub, attract, workshop, results) | `code_world.md` §19.1 |

No `rand()` in: the platform layer apart from the wipe, the sound driver, the UI (attract,
sign-in, dialogs, menus, hints), the hub, the workshop, the races and results, and the seven other
puzzles' logic. Round 2 confirmed it: without chimps the warehouse frames call `rand()` never;
with the 2 chimps of Aircraft level 1, 11 calls came before the puzzle choice; races are
deterministic from a state (REFERENCE_CAPTURES §10.7). A walking chimp calls `rand()` whenever a
probe touches an edge, pad, door or item, so warehouse runs replay only from a state with the same
input per frame.

---

## 5. CPU class: the 286 path

* `Cpu_Detect` (2c08:0032) stores 1 (8086), 2 (286), 3 (386) or 4 (486) in DS:2450; below 2 the
  game exits with code 5 [O].
* On a 286 the chimp set-up (2a80:07b1) halves the word after the count in the race's chimp list,
  in place: that word is the first chimp's room, 1 in every shipped list, so the first chimp goes to
  room 0, the workshop, at every set-up (1 → 0 → 0) [O code and data]. Room 0 has no walls, doors or
  floor references, so that chimp can never leave and is never drawn: **one chimp fewer in the
  warehouse** on a 286; its first brain call still draws one R(1000), so the `rand()` sequence
  differs too [O, `code_world.md` §13.1]. The scoping report's "the chimp count is halved" is wrong.
* Engine: an option `cpu = 286 | 386` (default 386, matching a 386-class reference machine [I: the
  DOSBox-X profile emulates a 486]). Implement it as the original does (halve the first room word of
  the list once per set-up, in the session's mutable copy of DS:1adc..1bae) so that modded lists
  behave the same [R].
* The 8086 refusal is dropped [R].

---

## 6. Overlays are not needed [O, R]

* The engine runs all code resident: no overlay manager, no stub segments, no INT 3Fh.
* DGROUP code pointers that point at stub entries (`stub:0020 + 5k`) are resolved through the stub
  to the overlay function and mapped to the engine function that ports it (ARCHITECTURE §1.2,
  §1.4) [R]. Tables of code addresses (the gears drive table DS:3902) are mapped to engine code.
* Overlay loads are disk reads during play on the reference machine; DOSBox-X shows no stall
  (§2.4), so they are ignored.
* The VROOMM import method (flattened image, `SetupOverlays.java`, `FixSwitches.java`) matters for
  reverse engineering only.

---

## 7. Options and the original switches [O 303a:005c, R names]

| Option (engine) | Original | Values | Default [R] |
|---|---|---|---|
| `sound` | `-SN` / `-S-` / `-S` (off), `-SI` (speaker), `-SA` / `-SB` / `-SM` (FM); none = detect an AdLib | `off`, `speaker`, `fm` (OPL2 at 388h on the emulated chip) | `fm` [I: the reference machine has an OPL2] |
| `cpu` | CPU detection | `286`, `386` (§5) | `386` |
| — | `-GV` (skips a BIOS display check that every supporting BIOS passes anyway) | — | dropped |
| — | `-?` (usage) | — | the engine's own `--help` |
| sound on/off | Ctrl+S, Options > Sound & Music | per player (DS:45cc) and the live switch DS:1ad4 | from the save |
| New Player Hints | Options menu | per player (DS:45a2) | from the save |
| chimps | asked for each new player | per player (DS:4578) | from the save |

* `sound = off` reproduces `-SN`: the original then loads the **FM** banks and runs every song
  silently (`Snd_Init` forces the OPL flag for any device other than −1/1/8; `Snd_Silence` stops all
  keying), so sound-driven waits and cues behave as with sound on (`code_sound.md` §3.1, §12.3).
* Ctrl+S is a looping priority-7Fh silence song that takes every channel; the other songs keep
  running. On the speaker it does not use channel 8, so the two speaker effects that do still sound
  with the sound off [O, keep].
* A new player's record always has the sound option 0 (the init's copy of the live switch is
  overwritten), and the sign-in does not apply it, so after a mute the next Ctrl+S inverts the two
  flags (`code_ui.md` §7). Keep it for fidelity; offer a fix as an option [R].

The engine may accept the original switches for familiarity (for example `--gg-args "-SI"`), as
the DOS edition accepts `--dos-args`. Done in wave 2 (`gg/app/options.h`): the original's `-?` prints
message 0x368 and exits 1, and its parse stops at the first switch that asks for the usage
(303a:0147); the engine's `-?` prints the help and exits 0, a bad switch exits 1.

With `-SN` the speaker gate-off writes still happen (`Snd_HwChannelOff` 1bb4:0648 and
`Snd_HwSilenceAll` 1bb4:0674 write port 61h &= FEh without checking `g_sndEnabled`); every key-on,
frequency, BD write and speaker gate-on stops (`code_sound.md` §12.3) [O wave 2].

---

## 8. Input [O, R]

* **Keyboard**: the G&G INT 9 model (`code_runtime.md` §6): held bits for Left, Right, Up, Down,
  Space and Ctrl (DS:0218), everything else to the BIOS queue; in play mode (DS:0096 = 1, set only
  by state 5, i.e. in the warehouse rooms) the arrows and Space are swallowed instead of queued;
  Ctrl+C is swallowed. **E0/E1 prefixes are swallowed**, so the BIOS never sees them: feed the grey
  arrow keys as the keypad scan codes. Reuse `PcKeyboard` and `bios_keyboard` with DOSBox-X
  typematic (500 ms, then 33 ms, as measured for TM DOS) [R, I]. Done in wave 2 on the core's
  `dos::ScanKeyboard`: the engine feeds the E0/E1 bytes to `Kbd_ISR` (`prefixBytesToHandler`, lead 0),
  which swallows them. Open: G&G never clears Num Lock (unlike TM DOS 1460:0003); the engine's BIOS
  model starts with Num Lock off, DOSBox-X's initial state is unchecked.
* **The key ring** (16 records, written for make codes that reach the BIOS): `Input_GetCode` pops
  one record per call and takes a BIOS key only with a record, and a popped record **replaces** a
  mouse code of the same poll (a Shift or Ctrl make, or a repeat whose key was already taken,
  erases a click). The LZH input refill drains the ring after each 4000-byte read (keys typed while
  a resource decompresses are lost), and the races discard one key per frame. The ring's full test
  never fires (it overruns); keep it, the readers above depend on its contents [O].
* **Held keys**: `Input_GetHeldKeys` (198d:02b8) gives the player actor the held bits, plus the
  idle bit 0x40 once after 0x111C ticks without a held key; it is not an input code.
* **Mouse**: always present in the engine (the original refuses to start without a driver). Host
  pointer → driver coordinates x 0..639, y 0..199; the game halves x; the right button only updates
  DS:0a0c (some puzzles read the button bits directly, so a right button held counts there). Model
  the INT 33h fn 0Ch event latches: press (first poll → zone press code | 0x4000), held (| 0x8000),
  release (release code with a tick stamp) [O]. The latched positions are the pointer **plus the
  current cursor's hot spot** (DS:7a74 / 7a76): zones are hit there (round 2: a press at (138, 120)
  with the wire spool's hot spot (36, 8) hit at (174, 128)). Moves are processed only while
  `g_mouseHideState` DS:01e2 is 0, i.e. between a flush and the next drawing call; moves during
  drawing are dropped (round 2 saw it in the hub) [O].
* `Mouse_SetBounds` passes the x minimum undoubled, but every caller passes 0: no effect; a cursor
  change resets the bounds to the full screen less the cursor's size [O].
* Several puzzles **move the pointer** themselves (INT 33h fn 4 plus `g_mouseX/Y`: gears, magnets,
  machine pieces' bounds): the engine moves its pointer model and the game-visible position
  together and may use a virtual pointer while a puzzle warps (`code_puzzles_b.md` §9) [R].
* **Click zones**: linked lists with priorities, circles and push/pop (17ab); a local zone beats
  any global zone; ties go to the zone added last (`code_runtime.md` §8). `Input_GetCode`'s global
  keys and code blocks as ARCHITECTURE §4.6.
* **Scripted input** (`--keys` and the fidelity scenarios) must carry mouse moves, presses and
  releases in PIT ticks, because the game cannot be played from the keyboard [R].

---

## 9. Saves [O, R]

* Keep `PLAYERS.DAT` byte for byte: header, the 100-entry roster index, 99 pre-allocated
  5821-byte records, in the engine's preference directory (separate from TM's files); offer import
  from the game folder's `PLAYERS.DAT` and an export back. Done in wave 2 (`gg/save/players_store.h`):
  the file is `<G&G pref dir>/players.dat` (`playersPathIn`); every write is atomic and keeps the
  previous file as `.bak`; a wrong-version or wrong-size file is re-created with the old one kept as
  `.bak`; a write failure is reported and never deletes the file; entry 99 is zeros on creation.
* Write exactly what the original writes: only the current player's record and index entry
  (`Players_SaveCurrent` 2b5a:01ea), at the same moments: a new player, Save & Exit, before a race
  (state 8), state 16, leaving the post-race menu after a win, after the final movie; Ctrl+R writes
  the index at once (`code_ui.md` §12.5). Round 2 saw the byte counts (a Save & Exit from a building:
  295 bytes).
* The building block (+0x27E + b·0x6BA in the record) holds the live 20 bytes, part holders and
  flags, door types, room item lists and actor bytes (`code_world.md` §4.2); a building in progress
  is resumed without a prompt (state 4). Keep the resume quirks (§10).
* File creation writes index entry 0 ninety-nine times (the "slot 98" anomaly: the first new
  player gets slot 98, and a 99th player would share it); entry 99 holds uninitialised bytes.
  Keep the creation loop's result so that files stay byte-compatible; write zeros for entry 99
  [R].
* The sign-in's Esc runs Save & Exit with saving disabled (no player is loaded) [O].
* The original re-creates the file when the version word is not 12, and deletes it and exits when
  a write fails (with a disk-full box whose footer pointer is uninitialised): the engine reports
  both, shows the box with the "Esc to Go Back" footer and never destroys a file [R].

---

## 10. The UI library [R]

From `code_ui.md` §22:

* Implement `Text_Render`, `Dlg_Layout`, `Dlg_Draw` and the sign-in rows exactly: they are
  pixel-exact against the captures, including `Text_MeasureRun`'s raw width + 1 measure rule (a
  word measures narrower than it draws with font 9) and the first story page's font spacing patch.
* Time freezes only in `Dlg_RunModal` (and so in the help pager); the drop-down and the text viewer
  run with the clock. `Snd_Pause` is not nested.
* Menu handlers' return values matter: non-zero reopens the same menu (the Help pager's Done, an
  Esc'd Save & Exit). A click outside a drop-down does not close it; the footer line of a box is
  Esc.
* Give the save-unders, the text viewer's saved area and the menu pictures their own buffers (the
  original keeps them in the shared resource buffer); nothing visible changes.
* Guard what the shipped data never reaches: the 0x106F footer, `Font_*` codes out of range (0x7F
  can be typed into a name), the hint queue's sixth slot (it writes over DS:a138), the hint due
  test's low-word compare.

---

## 11. Quirks: keep, make optional, or drop [R]

Keep (they change what the player sees, hears or how the game plays). Platform and timing:

* 4- and 8-tick frame pacing with no catch-up; frames start where the previous wait returned;
  unpaced polls stay unpaced;
* `Timer_Expired` one tick later than `Timer_Wait` for the same deadline;
* time frozen in dialog boxes and the help pager only; sound paused in dialogs and drop-downs
  (not nested);
* `Delay` running `Input_GetCode` (codes lost, global keys act);
* the key ring: one BIOS key per `Input_GetCode`, a key record erasing a click, keys lost while a
  resource decompresses; E0/E1 swallowed;
* mouse moves dropped while drawing; clicks at pointer + hot spot;
* retrace-paced palette loads (2 retraces each) and fades of 12 and 9 loads (22 and 16 visible
  frames);
* the random wipe (pattern by `rand()`, paced by timer slot 2, the dirty map cleared after it, the
  0x11DA column-319 bug) and the movie at 18 ticks per frame with its `Delay` cues, both written
  straight to the screen;
* the idle bit after 30 s without a held key; 30-s attract pages; Esc in the attract loop quits at
  once; Space goes to the sign-in from every attract step;
* the 286 chimp path behind the `cpu` option;
* the right mouse button ignored by the input fetch.

Sound (`code_sound.md` §13.4, §14, §17):

* the final movie's speaker arrangement 4 bug (its list ends in FDh: a silent music pass, 256 ticks
  longer);
* the room-music start transpose read with stride 6, and the speaker Aircraft room music that never
  reaches its cue (it never changes key);
* the workshop music cue that never fires (arrangement 1 always);
* Ctrl+S as a priority-7Fh silence song, which leaves speaker channel 8 audible; the hint chime cut
  by the dialog's pause.

UI (`code_ui.md` §22):

* names of 1..15 characters, an upper-case first letter, narrower than 60 px; prefix selection; the
  roster in name order; Ctrl+R saving the index at once; resume without a prompt;
* `Hint_Show` marking a hint seen also when hints are off; the strict `due < tick`; hints 0x104C
  and 0x104E never shown;
* the default buttons from the bundles (Save & Exit and Start New Player default to No); the
  Esc-disabled chimps box.

World (`code_world.md` §20):

* bananas set to 10 only at level 0 (they carry over between the races of a building);
* `g_roomChimpCount` growing at every room display (the chimp music voice lingers);
* after a resume the crate count is the last room's only, and the carried counts are not reset;
* a chimp that picks up a part of a type the chimps already hold twice destroys it;
* `Room_PlaceItems` never trying the last shuffled cell; the asymmetric chimp probes and wall sweep;
* the emergency reset (Alt+S then Alt+W) in the warehouse.

Vehicles and races (`code_vehicles_races.md` §17):

* races are deterministic functions of the two part lists: no `rand()`, no input (keys discarded
  one per frame); the land race moves only the integer part of the speed; the tie rules (land: the
  faster wins, the player on equal speed; aircraft: the player wins);
* race 14's uninitialised local: use the fuel-cell variant; the aircraft flight mode never reset;
* the reward follows the races won in all; the rank message after every win below the top rank;
  the count-up's click per frame; a rematch without count-up; the possible double count after a
  lost rematch;
* painting as a palette-index remap of shared pictures (undo loses shades of 14-colour parts); the
  conveyor's 26 frames and double stop sound; the completion bonus once per level;
* the busy-waits on sound (race intro, start effect, results music) and the unpaced blueprint loop,
  pumping the host.

Puzzles (`code_puzzles_a.md` §9, `code_puzzles_b.md` §9):

* the choice rule (the first three unsolved variants of a random kind) and its exact `rand()`
  calls; a stale DS:b144 after leaving a puzzle through the menu;
* electric: no success test after placing a piece, a second board click after a miss with the
  pointer unmoved stringing a wire (the wires of a picked-up piece and a missed wire go back into
  the toolbox: wave 3 corrected the "wires lost" reading), stale piece types in free slots, the Hint button's 35-column restore, the literal path
  search with its 10-row table;
* launch: solvable with no object in variants 24, 25, 27, 28 and 29; the 19-tick DAM step;
* machine pieces: the `rand()` retry loop, groups that never split; gears: the pointer moved by the
  game, a jammed step that flicks, no input during the drag; balance: the swing speed and the
  transposed swing effects; magnets: the 16-bit force arithmetic (and the literal square root,
  which returns 2 for 65 540 and 65 541), the threshold asymmetry and the phantom poles.

Make optional:

* `cpu = 286`;
* the new player's sound-flag inversion (fix offered, original by default);
* fixed sound data (the movie list 4, the room-music stride) as a later mod, not by default.

Drop:

* the 8× BIOS clock and the double EOI; the PIT, INT 9 and INT 33h installation; the overlay
  manager;
* the start-up refusals (CPU below 286, display code, mouse driver, free memory) and the `-GV`
  switch;
* the `gizmo004.dat` swap file (keep the save-under in memory) and its missing error checks;
* deleting `players.dat` on a write error; DOS critical-error prompts;
* the dead PSG and MT-32 driver paths (classes 0, 2, 4);
* reads through NULL or past arrays that the shipped data never reaches (unknown opcodes, the hall
  sequences' NULL layer, the hint queue overflow, `Dirty_Mark` past the map): guard them and assert
  in debug builds.

---

## 12. Verification

* Reference captures need guest mouse input in DOSBox-X: the `tmctl.py` gg profile and the
  MOUSEKEY TSR (`REFERENCE_CAPTURES.md`; `re/CONTEXT.md`, G&G section). Scripted mouse input is
  posted in host time, so runs through the warehouse rooms start from save states.
* **A reference set exists** (REFERENCE_CAPTURES round 2): all 8 puzzle kinds (variant 0, with
  their solutions in game coordinates), a race in every building, the workshop, the final movie,
  sound runs, a second player and a save/resume, with timing measured on the AVIs. Races are
  deterministic from their states (identical results); puzzle replays reproduce the solved screens
  except gears (the game moves the pointer during the drag).
* Screens: pixel comparisons of settled frames, as for TM DOS (`tools/dos/imgcmp.py`). Frame-exact
  AVI comparisons need alignment: the host-timed input shifts the PIT phase against the retrace
  between runs.
* Sound: DOSBox-X `.dro` OPL register streams against the engine's driver output
  (`tools/dos/oplcmp.py`), song by song; skip the ~100 register-dump writes at the start of every
  `.dro` (DOSBox-X opens a capture at the first key-on and dumps the registers then).
* Timing: frame logs from a logger patched into **resident** code (overlay code moves in memory),
  read with a DGROUP-aware state reader (`tools/dos/savmem.py` pattern; `tools/gg/capture_mem.py`
  reads and patches G&G states). The logger does not exist yet.
* Unit tests from the specs' test vectors: the race simulations (`code_vehicles_races.md` §18), the
  `rand()` count of a set-up (3925 calls for the w0 state, `code_world.md` §4.4), the puzzle
  `rand()` counts (`code_puzzles_b.md` §7).

---

## Open questions

1. The costs of long frames on a reference machine slower than DOSBox-X: room entries, wipe steps,
   full flushes, the phase of the retrace at the start of a fade (§2.4). Not needed against the
   DOSBox-X references.
2. *Answered:* the help pager freezes time (each page is a dialog); the puzzle screens are
   unpaced loops that run with the clock, and their Hint viewer does not freeze it.
3. *Answered in wave 2:* the DAC expansion is `(v << 2) | (v >> 4)` (§3.2).
4. *Answered:* the key ring is read by `Input_GetCode` (one record per call), `Kbd_PopQueuedKey`
   (the LZH refill drains it, the races pop one per frame) (§8).
5. *Answered:* the wipe hides the cursor before copying; the movie runs with it hidden (§3.2).
6. The reference machine's CPU class and sound device (they set the defaults of `cpu` and `sound`).
   The DOSBox-X profile emulates a 486 (not the 286 path) with an OPL2 (`REFERENCE_CAPTURES.md`
   §1).
7. Not captured yet: chimps in action (banana throws, part thefts, stuns), puzzle variants other
   than 0, races of levels 2-5, the rematch double count, a recycle (REFERENCE_CAPTURES §13).
