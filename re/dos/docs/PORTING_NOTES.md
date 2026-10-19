# Porting notes: the 1990 DOS edition in the tmport engine

How the original DOS program (`SST.EXE`, Turbo C 2.0, 1990) maps onto the engine as a second
edition, which runtime model to use, how the original's switches become options, how saves stay
compatible, and which quirks to keep. Read `SCOPING.md` (engine plan, waves) and `ARCHITECTURE.md`
(program, rules) first. The algorithms are in the wave-1 topic docs:

| Doc | Modules |
|---|---|
| `code_runtime.md` | timer ISR, main loop (130f), start-up (1460), keyboard ISR (43a0), input (4a85); timing measurements |
| `code_world.md` | scenes (1437), cell handlers (1568, 176a), world grid, gates, links, castle |
| `code_player.md` | player state machine (18cf), animation scripts and sprites (4306), search (4a03), end sequence, treasure room |
| `code_elves_layout_hints.md` | elves (4521), layout generation (1a54), New Player Hints (49a6) |
| `code_ui.md` | menus, boxes, help, text (43cb), title (4ba4), sign-in (47c7), quiz screen (46ce), HUD drawing |
| `fmt_graphics.md`, `fmt_data.md`, `fmt_sound.md` | file formats, DGROUP map, sound driver |
| `REFERENCE_CAPTURES.md` | DOSBox-X reference frames, AVIs, audio, save states (Round 1 and Round 2) |

Tags: **[O]** observed (code, bytes, RAM of save states, captures), **[I]** inferred, **[R]**
recommendation of this document.

---

## 1. Inputs and validation [R]

| File | Needed for | Check |
|---|---|---|
| `SST.EXE` | DGROUP tables, texts, font (1000:2447), palettes, sound tables, input tables, switch letters | MZ + EXEPACK; unpacked image 288 992 bytes; Turbo C copyright at DS:0004; EGA table sanity (`tools/dos/dos_gfx_common.py`) |
| `MOUNTAIN.PAN`, `MOUNT2N3.PAN`, `MOUNT3.PAN`, `CLOSE.PAN`, `SHACK.PAN`, `OPENING.PAN` | scenery, title | header sizes add up to the file size |
| `SPRITES.PAK`, `BEGIN.PAK`, `CLOSE.PAK`, `SHACK.PAK` | sprites | offset table + FFFF FFFF trailer |
| `ALL_PUZS`, `PRG_DICT`, `HELP.DAT` | quiz, help | sizes as `fmt_data.md` |
| `SFX.IBM`, `TUNES.IBM`, `SFX.TAN`, `TUNES.TAN` | sound per device | header word 0x96 / 0x230 |
| `USERS.DAT` (optional) | import of the roster and saved games | 3243 bytes |
| `TLCINFO.DAT`, `ssrnet.dat`, `ssrquiet.dat` | — | ignore (floppy label check and an inert network refusal) |

The original reads every file from the directory of `SST.EXE` (`Path_InitDrives` 1460:08CC uses
argv[0]); the disk numbers in the file table DS:0476 only matter for floppies. A missing file stops
the original at start-up (`Game_Init` returns 0, nothing is printed for most files); the engine
should name the file.

Implemented in wave 2 by `DosFiles` (`src/dos/data/dos_files.h`): names are found without regard
to case through the EXE's file table, every check above runs at start-up, a missing file raises
`MissingFileError` with the file's name, a broken one `FormatError` starting with the name, and a
USERS.DAT of the wrong size is ignored with a warning (it is only imported).

---

## 2. Runtime model

### 2.1 What the original does [O]

* PIT channel 0 at 1193181.67 / 5041 = **236.69543 Hz** (exactly 3579545 / 3 / 5041 Hz). Every
  interrupt: tick counter, sound sequencer. Every 13th: BIOS clock (18.20734 Hz). Every **5th: the
  game tick, 47.339086 Hz**, run inside the
  interrupt with a re-entrancy guard (`Timer_ISR` 1000:0428).
* The main loop runs as fast as the CPU allows. One iteration = one "frame": latch tick-side state
  (`Frame_Begin` 130f:025A, clears the step flag DS:910D), UI updaters, scene load, scroll, search,
  quiz, HUD/overlay painter, redraws, sprites, present.
* **Step gating**: a game tick whose predecessor already advanced an animation script or moved the
  camera since the last frame start runs only `Anim_Tick` (scripts wait) and `Input_Tick`. So the
  logic makes at most one visible step per frame; on a slow PC the game slows down.
* Some features count frames instead of ticks: the search blink, the HUD coin count-up, the
  CHAMPION flash, the screen dissolve (350 cells inside one frame), title sparkle attempts.

### 2.2 What the reference recordings show [O]

DOSBox-X with `cycles=fixed 6000` (`tools/dosbox_tmdos.conf`, all reference captures): the loop ran
**800-1700 frames per second** in normal play (idle 1690/s), 55-75/s during the search and the
coin count-up, single frames of 38-127 ms for full redraws and dissolves. Game ticks always ran at
47.34/s and DS:910D blocked a tick only behind frames longer than one tick (1 tick in 5 s of
walking, 4 after a search). So the reference game is **tick-paced**: every game tick executes a
logic step. Measured step timings (exact, from the per-tick logger) are in `code_runtime.md` §9:
walk 8 px per 5.125 ticks (41 ticks per 8 steps), climb 8 rows per 7.25 ticks, jump 58 ticks, net
swing 80 ticks, rank chest one row per 4 ticks; an attract page lasts 7201 PIT ticks after its draw,
then the next page is drawn (title 12.8, story 1 13.3, story 2 7.5, series page 14.3 PIT ticks):
5770 game ticks per cycle of four pages (wave 4, `code_ui.md` §8.5).

### 2.3 Engine model [R]

```
host clock (ns) --> virtual PIT accumulator (236.69543 Hz = 3579545 / 3 / 5041)
                     |-- every PIT tick: sound sequencer (or run it in the audio callback by
                     |                   sample count, fmt_sound.md §9)
                     '-- every 5th PIT tick: one ORIGINAL FRAME STEP:
                             gameTick()              Game_Tick 130f:00FE, unchanged order
                             frameBegin()            latches, g_stepDone = 0
                             frameBody()             main loop body 130f:0037..00C8 minus the present
                             (wave 5: the frame after its tick; a frame that begins when a longer one
                             ended runs before the next tick, §2.5)
presentation: at each display refresh, show the newest completed back buffer (320x200, 4:3)
```

* **Reference frame rate = the game-tick rate, 47.34 Hz, one original frame per game tick
  ("lockstep").** This is deterministic, independent of the display rate, and gives exactly the
  tick-paced behaviour of the recordings; the step gate can never block (it is cleared before each
  tick), which is what DOSBox-X showed. Keep the gate implemented: it is the switch for an optional
  "slow machine" mode (more ticks than frames).
* The original's race between the interrupt-time tick and the frame body disappears; choose
  "tick before frame body" consistently. Wave 5: on the reference machine idle frames of a fraction of a
  PIT tick run between two ticks, so the frame that does a tick's work begins right after it: the
  engine runs Game_Tick, then Frame_Begin and the body (§2.5 "Where a frame starts"; until wave 4 it ran
  Frame_Begin before the tick, so its pictures showed the sprites and camera of the tick before).
  Rand() users of both sides then interleave in a fixed order (the original's order was timing
  dependent, so bit-exact replay of a recording is not possible in any case).
* **Per-frame features under lockstep** (DOSBox-X value in brackets) [O measurements, R choice]:

  | Feature | Lockstep result | Recommendation |
  |---|---|---|
  | search blink and reveal, 37 frames | 0.78 s (0.79 s) | keep per frame |
  | HUD coin count-up, 1 coin per frame + effect 4 | 21 ms per coin (14.0 ms = 3.32 PIT ticks, r2/fl_countup_after; wave 4) | default: a coin every 3.32 PIT ticks from the tick that changed g_coins (hud.cpp, `code_ui.md` §11.2); option: 1 per frame |
  | CHAMPION flash, 800 frames, toggle every 80 | 16.9 s (0.87 s) | default: ~918 flash frames/s (640 frames in 0.697 s at the EGA AVIs' 60.29 frames/s; ≈ 87 ms per phase); option: 1 per frame |
  | dissolve, 350 cells in one frame | instant | spread over 127 ms (about 6 frames at 47 Hz, ~58 cells each; 127 ms = 6.01 game ticks, so at the exact rates the last cell lands in a **7th** lockstep frame, wave 2); keep its 700 rand() calls |
  | title sparkle attempts, chance 1/128 per frame | 0.37/s | DOSBox-X ~1730 attempts/s from 0.75 PIT ticks after the game tick that ended the previous sparkle, spots in rand() order: 233 of 249 sparkles of three AVIs take their first step on the observed tick (`code_ui.md` §8.5, title.cpp) |
  | UI updaters | per frame | per frame (invisible) |

  "Original frame clock" = a counter advanced by `rate × dt` per engine frame; a feature runs as many
  of its per-frame steps as the clock gives. The two options correspond to "like the DOSBox-X
  reference" (default, so the fidelity wave can compare against the captures) and "like a period
  PC" (one step per original frame).
* Presentation: the original presents dirty spans without retrace sync, so DOSBox-X frames show
  half-drawn scroll steps (two bands over two 60 Hz frames, REFERENCE_CAPTURES §5.4). The engine
  presents complete frames; fidelity checks compare the settled frames. Wave 4: at the reference
  speed a long frame's picture appears when the frame ends (§2.5), as on the reference machine.
* Pacing on slow hosts: if the engine falls behind by more than a few ticks, drop ticks (cap the
  accumulator) rather than run many logic steps per presented frame.
* Wave 4: the frames the reference machine needed more than a game tick for, and the ticks lost
  in its disk I/O, are part of the reference timing (§2.5).

### 2.4 Start-up order [O, R]

Original: `Game_Init` (memory, critical-error handler, keyboard flags, hardware detection, switches,
paths, mouse, video, palette, sound files, help, dictionary, BEGIN.PAK, `Game_InitVars`), then
`Kbd_Install`, `Timer_Install`, `Title_Run`, `g_uiMode` = 8, main loop. The engine needs the same
`Game_InitVars` values (`code_runtime.md` §4.4) and must start the Turbo C `rand` sequence from its
default seed **1** (the u32 seed lives at DS:8F48, initialised to 1 in the EXE [O]; `rand` 4d12:001F
returns bits 16..30 of seed = seed * 0x015A4E35 + 1): `srand` is only called by `Layout_Generate`
for a new game, so everything before it (title sparkles, a dissolve, the elves of a resumed game)
runs on the unseeded sequence (`src/rtl/borland_rand` is the same LCG). The seed can be read out of
any DOSBox-X save state (`savmem.py peek STATE 8f48 4`) to check the engine's sequence.
On the AdLib device every `Music_Play` with the music on draws one `rand()` (4ac9:0002: the
orchestration `4·tune + (rand() & 3)`), so the sound device changes the random sequence;
`DosAudioDevice::musicPlay` draws it from the game's generator (wave 2).

### 2.5 Long frames and disk I/O at the reference speed (wave 4) [O, R]

On the reference machine an idle frame takes a fraction of a PIT tick, so a frame that does more
starts right after whatever asked for its work, and lasts as long as its work [O frame logs]:

* **Disk I/O** runs with `g_quit` = 1: the game ticks that fall into it return at once (no logic, no
  counters, no STEP record in the logs). DOSBox-X serves DOS file reads at once; the time is the
  unpacking of a PAN and the CPU work around a few calls. The lost ticks are known for every kind
  from the tick counters of every DOSBox-X state (g_ticks / 5 game ticks fired, DS:90FE = 12 -
  executed Game_Ticks mod 13: `extracted/scratch/dos_w4_timing/lost.py`) and from the STEP gaps of
  the logs: Title_Run's OPENING load 1 (every state at the sign-in), a MOUNT2N3 or MOUNT3 load 4
  (r2/fl_climb, fl_bridge_l3, fl_bridge_l3_jump, fl_geyser_slide; the level changes of the EGA
  climb), MOUNTAIN 5 (r2/fl_bridge_l2), CLOSE 1, SHACK 1, a new player's game start (Users_WriteFile,
  Quiz_LoadQuestions, MOUNTAIN) 7 (ega/shack_hint1), a resumed level-2 game (Quiz_LoadQuestions,
  MOUNT2N3) 5 (tandy/level2_resumed), the return from the treasure room (Game_End's write, Game_Start,
  MOUNTAIN) 7 (r2/end_t6_clubhouse, ega/after_castle), the treasure room from the clubhouse and back 6
  (r2/tierb_after_troom), the champion's arrival in the clubhouse 6 (r2/fl_crown_wait). Model
  (`main_loop.cpp`): a PAN costs 3.4 PIT ticks + 0.35 per 1000 bytes of the file, the sprite bank a
  scene change reads with it (CLOSE.PAK, SHACK.PAK, SPRITES.PAK after one of them) 0.019 per 1000
  bytes (SPRITES.PAK 2.2), Users_WriteFile 4.5, Quiz_LoadQuestions 7.
* **Drawing**: a full HUD redraw (Hud_Redraw with DS:922E: frame fills, menu bar, the whole play field,
  the actors, the status bar, the whole screen presented) 16 PIT ticks (closing the tierc start
  message, r2/fl_tierc_outside g_ticks 13805..13836); a full play-field redraw a tick asked for (a
  bridge breaking) 12.5 PIT ticks from that tick (r2/fl_bridge_l3_jump log tick 264); opening a
  message or hint bubble (its HUD redraw, then the cloud, the text and Flutter) 27.5 PIT ticks from
  where its frame began (r2/fl_tierc_outside log ticks 79..82 blocked behind the start message that
  opened when the door's dissolve ended; r2/fl_search_t 331535..331550 behind hint 4); a Button_Flash
  72 PIT ticks; a scroll frame 6.5 PIT ticks horizontal, 7 vertical, 7.5 both, from the tick that moved
  the camera (the FRAME rings; camera.cpp scrollUpdate); a full HUD redraw in a frame that dissolves 6 (the dissolve presents);
  the title loop's page draws (OPENING.PAN and the page) title 12.8, story page 1 13.3, story page 2
  7.5, series page 14.3, the walk-off's backdrop about 12 (title.cpp keeps that clock itself,
  `code_ui.md` §8.5; the walk-off agrees with `tandy_r3_session.avi`, frames 419-663).
* **The dissolve** (30 PIT ticks = 127 ms, r2/fl_l2door_after) starts after the rest of its frame,
  which began right after the teleporting tick (or the frame before it): that frame's I/O and
  drawing, then 1 PIT tick of play-field redraw (none after a HUD redraw), plus 5 when the
  teleporting tick started a tune (a Music_Play with the music on: the AdLib driver's set-up; the
  doors, not the fall shaft or the ice slide). It continues one lockstep frame per game tick after the
  frame's other work and ends in the frame of the last tick before its end: the doorways (tune) end
  it after the 7th tick, the end sequence's chute after the 6th, the castle top (CLOSE load) after the
  7th, the castle gate (SHACK load) after the 8th, the champion's arrival in the clubhouse 65..70 PIT
  ticks after the gate tick (`main_loop.cpp` setDissolveStart; the cells per frame stay the frame
  clock's). The frame after it begins when it ends (wave 5: before the next tick).
* **Search frames** (a partial play-field redraw and present each): 4.25 PIT ticks, 4.65 while the
  found treasure or key is shown (the 36 intervals of r2/fl_typematic and r2/fl_search_t), back to
  back from 0.5 PIT ticks after the starting tick: the 37 frames take 31 game ticks (lockstep 37); the
  frame after the result frame (hint 4 after a find) begins when it ends.
* **Where a frame starts** (wave 5) [O frame logs, R]: idle frames last a fraction of a PIT tick, so
  the frame that does a tick's work begins right after that tick, whether the tick asked for it
  (Frame_Begin latches a scene, a redraw, a camera step) or delivered a key; its Frame_Begin comes after
  the tick (the r2 log end states hold draw records equal to the live sprite slots: DOSBox-X's last
  frame began after the last tick). A frame that runs past the next ticks (a scroll frame, a search
  frame, a bubble, a scene load) has them inside it: they run Game_Tick without a frame of their own
  (g_stepDone as its Frame_Begin left it), inside its I/O not at all; the loop's next frame begins when
  it ends and latches what they did, before the following tick when it ended between two ticks
  (r2/fl_bridge_l3 gates log tick 1111 behind a scroll frame, the breaking bridge's redraw and the
  fall's first scroll frame run back to back; the champion's load begins after the chute's last scroll
  frame; hint 4 begins when the search's result frame ends, r2/fl_search_t: the ticks it blocks are
  DOSBox-X's 331535..331550). A dissolve and a Button_Flash wait are such long frames (no Frame_Begin
  until they end); the flash's rest of the frame (the confirm handler, a HUD redraw) counts from the
  wait's end (+72 PIT ticks), between two ticks. The search frames run back to back from 0.5 PIT ticks
  after the starting tick, so a tick falls inside one of them and the next begins when it ends.
* Engine: the game reports each frame's cost to `DosRuntime::frameTiming()` (`FrameTiming`: g_quit
  sections and drawing time in thousandths of a PIT tick, from the frame's start; `continueFrame()`
  for a dissolve or a flash wait going on, `backdateStart()` for the flash's end); the runtime runs
  the ticks inside the frame without a frame of their own (no Frame_Begin: a tick after one that
  advanced a script or moved the camera is gated), skips Game_Tick in the I/O, and runs the frame that
  began when a long one ended before the next tick. Deterministic: costs come
  from what the frame loaded and drew, never from host time. The trace (`--dos-trace`) has no row for
  a tick lost in I/O, like the logs' STEP ring (the events of a frame whose own tick was lost start
  the next row).
* **Presentation**: the reference machine presents at the end of a frame, so a long frame's picture
  (a bubble, a scene load, a HUD redraw, a title page) appears when the frame ends, not when it
  began. The host (and the frame dumps) show it from the last game tick before that end
  (`DosRuntime::frameEndUnits`, `DosGame::screenShownAt` for the title's page draws): the AVIs show
  the message bubbles 4.7-5.5 ticks after their frame began (`ega_r2_crown_to_end_messages.avi`), the
  walk-off's redrawn title 2-3 ticks after the Enter (`tandy_r3_session.avi` frame 421) and the
  resumed game's level about 8 ticks after Yes (frame 1185); all three now match at 0 pixels.
* Checks: frame logs replayed from their start states (`test_dos_mainloop.cpp`, tick for tick in x,
  y, camera, state, keys, coins, gated and lost ticks): every one matches on every tick: r2/fl_climb
  640, fl_bridge_l2 746, fl_bridge_l3 1596, fl_bridge_l3_jump 438, fl_geyser_slide 663,
  fl_l2door_after 190, fl_shack_left 366, fl_slowcpu_walk 1745, fl_walk2 544, fl_jumps 752,
  fl_typematic 975, fl_tierc_outside 1161 (with the reference run's host mouse moves), fl_end_t2_wait
  2049 (the whole end sequence), fl_crown_wait 2055; the 21 logs of the fidelity scenarios match in
  both alignments (`tools/dos/fidelity.py`); the game start's sprite slots equal ega/shack_hint1 (the
  first start message, `ega/007_after_name` 0 pixels).
* `frame_speed = period` keeps strict lockstep: every frame fits into its game tick, files load at
  once and the search runs one frame per tick (the idealised period PC whose loop runs once per tick;
  the slow-machine mode of the runtime keeps its own grouping of ticks).
* Sound calls inside a long frame (wave-5 integration) [O, R]: the original's driver entry points
  write its blocks at once and the next timer interrupt plays them, so a call made partway through a
  long frame sounds that far into it (Game_Start's tune after Quiz_LoadQuestions and the level's PAN,
  the music's pause after a message bubble's drawing). The engine reports the frame's costs where the
  work happens and delays each call by the PIT ticks to its place (`ENGINE_MAP.md` §3.8): the resumed
  game's tune of `tandy_r3_session.avi` now starts 3 PIT ticks after the recording's (23 before).
* Not modelled: the rare long Game_Tick (r2/fl_climb: a tick 4 PIT ticks long); the sub-tick landing point of a
  tick inside an idle frame (the castle-top tick removed the HUD key icon in 1 of 5 reference runs: its
  tick fell between a frame's Frame_Begin and its Screen_DrawOverlay; the engine keeps it as the other
  4). Measured in wave 5 but not modelled: the end sequence's animation frames (several large sprites
  drawn and presented) appear in `ega_r2_end_t2_nokey.avi` about 6 PIT ticks after their tick, as late
  as the chute's scroll steps (relative to the logged ticks of r2/fl_end_t2_wait), while the climbing
  player's sprite steps in the trunk-climb AVI of r2/fl_climb show within a PIT tick; on the Tandy a
  full HUD redraw (closing the Help menu or Flutter's hint) shows 4-6 PIT ticks after the key's tick,
  against 16 on EGA (inferred: the EGA present writes four planes), and a menu opens within a game tick
  (`tandy_r3_session.avi`: the music pauses at the menu's Sound_SyncState, the menu is complete one AVI
  frame later); in the same AVI the resumed game's HUD redraw and the start message's bubble (HUD redraw
  and cloud) end about 12 PIT ticks earlier than the EGA costs give (the music's pause after the bubble
  and the bubble's AVI frame 1192): about 10 PIT ticks per Tandy HUD redraw (wave-5 integration, the
  sound-timing check). Menus, Yes/No and OK boxes, the quiz screen, the sign-in and the rank screen block no
  tick in any log (the UI modes freeze the world) and their frames are not in any FRAME ring; their
  cost (and the per-look costs) needs new logs: `framelog.py install` on a state right before the
  action, the key within 2 s (the FRAME ring holds 4096 frames), then save. Mouse moves of the
  reference runs are replayed with `--keys` move events.

---

## 3. Options and the original switches [O 1460:0463, R names]

| Option (engine) | Original | Values | Default [R] |
|---|---|---|---|
| `look` | `-E` / `-C` / `-T` / `-M` / `-H` | `ega` (also Tandy: identical pixels), `cga` (4-colour dither), `tandy`, `mcga` (softer palette), `hercules` = CGA path | `ega` |
| `sound` | `-Si` / `-St` / `-Sa` (`-Ss` = AdLib) | `speaker` (SFX.IBM + TUNES.IBM), `tandy` (SFX.TAN + TUNES.TAN on SN76496), `adlib` (TUNES.TAN on OPL2, effects SFX.IBM on the speaker; every `Music_Play` with music on draws one `rand()`, §2.4), `off` (engine only) | `tandy` (implemented in wave 2) until an OPL2 core exists; the DOSBox-X reference machine was AdLib. Wave 5: `adlib` is the original's AdLib driver on an emulated OPL2 (`fmt_sound.md` §5.5, §11.7); the default is unchanged |
| `slow_cpu` | `-X` (toggles the 8086 detection) | off / on: 7 elves processed, scroll and the player's scroll step doubled with rows 112..143 not scrolled, search shortened by 32 frames, CHAMPION timer −4 per frame, Esc opens the Exit box directly | off |
| `music`, `sound effects` | per player (USERS.DAT +0x15E/+0x160), Ctrl+S toggles both | | from the save |
| `frame_speed` | — (CPU speed) | `reference` (DOSBox-X per-frame rates, §2.3, and its long frames and disk I/O, §2.5) / `period` (one step per original frame, every frame within its game tick) | `reference` |

The PS/1 audio card (device 3) is a Tandy PSG on another port: no separate option. Only the second
character of a switch counts and switches are separate arguments; the engine may accept them on its
command line for familiarity (`--dos-args "-T -St"`).

As implemented in wave 2 (`edition/dos_options.h`): defaults look `ega`, sound **`tandy`**, slow CPU
off, frame speed `reference`; precedence defaults < `tmport.ini` [dos] < `--dos-args` (the switches
in order) < `--look` / `--sound` / `--slow-cpu` / `--frame-speed`. `-H` (Hercules) gives the CGA look;
`-X` toggles the value it finds (the ini value), while `--slow-cpu` forces slow-CPU mode on after
`--dos-args`.

---

## 4. Input [O code_runtime.md §5-6, R]

* Two input paths, both needed: **held bits** `g_keys` (Up 1, Down 2, Left 4, Right 8, Space 0x10,
  jump 0x20, coin 0x40) from key down/up with the `Kbd_ISR` rules (Home = Up+Left, PgUp = Up+Right,
  any Up+Left/Right forms a jump and drops Up/Down, the jump bit is cleared by the next key event,
  Tandy arrow codes only on a Tandy), and a **BIOS-style queue** of 16-bit key codes
  (scan << 8 | ASCII) fed by key down and by **typematic repeats** for menus, dialogs, quiz and name
  entry. `Input_Poll` consumes one code per game tick.
* Synthesise typematic repeats in the engine (DOSBox-X: first repeat **500 ms** after the press,
  then exactly every **33 ms** (30.3/s, one per 1.562 game ticks); only the last key pressed
  repeats). These two values reproduce the key bits of every r2 per-tick log with 0 mismatches
  (wave 2, `dos/input/keyboard.h` KeyboardOptions). They matter for play too: the terrain clears held bits it forbids
  every tick and only a repeat sets them again (holding Left against a wall, walking off a ladder).
  "Up then Right" does not jump; "Right then Up" does.
* Mouse: logical coordinates x 0..159 (2-px units), y 0..199; zones per input context (DS:80DB);
  play-field press/hold walks, climbs, jumps or nets relative to the player (unsigned compares near
  the edges kept); status-bar press drops a coin; the pointer hides after 600 ticks idle.
* Scripted input for tests (`--keys`) should express key down/up in game ticks. Wave 4: a time inside
  a game tick (`806.4923:down:down`) places a key between two PIT interrupts as DOSBox-X key presses
  fall; the phase of the press decides its typematic repeats (r2/fl_typematic matches in every column
  with Down at 807.4923 and PgUp at 195.136, the latter with the optional grey-key artefact).

---

## 5. Saves [O fmt_data.md §4, ARCHITECTURE §6, R]

* Keep the DOS record format byte for byte: 3-byte header + 9 × 360-byte records = 3243 bytes, in
  the engine's preference directory (separate from the Windows `users.dat`); offer import from the
  DOS folder's `USERS.DAT` and an export back, so players can move between the engine and DOSBox.
* Write at the same moments as the original: new player, remove player, File > Start Over (saved
  game dropped), File > Start New Player, File > Exit and Save, and `Game_End` after each game.
  Nothing is written at level changes or while playing.
* Not saved (rebuilt on resume): elf positions, the RNG state, the bonus-coin picture, hint flags.
  A resumed game continues on the unseeded rand sequence of the new session (§2.4).
* Header bytes 1-2 are the u16 DS:929F (`g_saveHeapHigh`, the sprite save-heap end of
  `Sprites_DrawSorted`; word moves 47c7:0de0 / 0f80) [O]; keep the value round trip.

---

## 6. Quirks: keep, make optional, or drop [R]

Keep (they change what the player sees or how the game plays):
* tick-paced logic with the exact per-tick order; walk/climb/jump/net timings that follow from the
  scripts (41 ticks per 8 walk steps, 29 per 4 climb steps);
* one BIOS key per game tick; typematic re-arming of terrain-cleared bits; the diagonal jump rules;
* the camera's low 3 bits ignored for scenery but used for sprites (e.g. the cave of cell 0xF7 sets
  camera = player x − 0x124: sprites 4 px off the tile grid);
* HUD count-up one coin at a time with effect 4 per coin; CHAMPION flash ending visible; rank chest
  one row per 4 ticks; search length in frames;
* the dissolve's 700 rand() calls (they shift the random sequence of everything after a transition);
* the CGA look's per-routine dither phases (tiles from the first row of their column, fills on the
  even tables, sprites from their first drawn row, text turning one pixel per glyph row,
  Gfx_ReplaceColorRect's odd-start compare offset; `fmt_graphics.md` §2.4, the CGA plane of
  `BackBuffer`);
* hint "seconds" of 60 ticks (1.27 s), paused and phase-shifted by UI modes;
* music frozen while an effect plays (speaker/Tandy), effect priority by id on the speaker and
  AdLib (`fmt_sound.md`); on the Tandy/PS/1 there is no priority (`Sfx_Play` 491b:06d7 calls
  `Sfx_Stop` first, which resets the id to 0xFFFF, so any effect replaces any other);
* Esc in play opens the Exit-and-Save box. (The shifted-A drop seen in name entry is not in the
  game's code, so the engine has none: `code_ui.md`, Wave 3 corrections.)

Make optional:
* slow-CPU mode (`-X`) with its scroll tear;
* the grey-key E0 artefact (a held grey PgUp clears the jump bit for one tick per repeat);
* the occasional Game_Tick overrun (one tick of ~4 ticks at a level boundary in the recording);
* `frame_speed = period`.

Drop:
* Num Lock / Caps Lock forced off at start; the BIOS clock chain; the 1000-iteration delay loop;
* floppy disk prompts, TLCINFO.DAT, the network files, critical-error retry boxes (report and stop);
* the unused Ctrl flag (DS:92FA) and `Mouse_ChangedMask`'s y/x mix-up (no caller);
* `Fatal_Print` dropping the last character of its message.

---

## 7. Verification [R]

* Reference material: `REFERENCE_CAPTURES.md` (Round 1: 104 EGA frames and the Tandy/MCGA/CGA sets;
  Round 2: treasure found, end sequences with chest fill, rank-up and crown, castle creature, elf
  dust, coin creature, bridges, geyser hole, prize rooms, treasure room, slow-CPU scrolling, UI boxes,
  and audio of all 3 tunes and 12 effects on speaker, Tandy and AdLib).
* Timing checks against the per-tick logs (`extracted/scratch/dos_w1_runtime/stepchanges.py` on the
  `r2/fl_*` states): the engine's per-tick player x/y, camera and state sequence for the same input
  should match tick for tick (walk, climb, jump, net, slide, falls).
* `savmem.py` reads any DGROUP variable out of a DOSBox-X save state; use it to compare engine state
  dumps with the original at the same moment (layout, elf table, counters).
* Audio: compare engine renders with `extracted/captures/dos/audio/*_soundtest_*.wav` (each effect
  every 600 PIT ticks after a music stop, then tunes 0, 1, 2; schedule in
  `extracted/scratch/dos_w1_runtime/sound_script.txt`) and the AdLib register log (`.dro`).

---

## Open questions

* Which per-frame speed users prefer (`reference` vs `period`) — decide after seeing both in the
  engine.
* The title sparkle rate under lockstep (owner: `code_ui.md`).
* ~~Exact DOSBox-X keyboard repeat parameters~~ — answered in wave 2: 500 ms delay, then exactly
  33 ms per repeat reproduce the r2 logs with 0 mismatches (§4). (A host keyboard's own repeat
  rate must not leak into the game).
