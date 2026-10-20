# Gizmos & Gadgets: engine map (wave 2, the foundation; wave 3, the game)

How the original program (`ssg.exe`, DOS v1.0) maps onto the engine's `src/gg/`: which engine file
ports which original module, the specs it follows, what it does now and what is still missing, how
the modules sit on the shared core, the results of the wave 2 stop/go checks (SCOPING §9) and of the
wave 3 playthroughs (§11). Read with `SCOPING.md`, `ARCHITECTURE.md`, `PORTING_NOTES.md` and the
topic specs; the specs say *what* each routine does, this map says *where* it is. Model:
`re/dos/docs/ENGINE_MAP.md` (TM DOS).

**Status after wave 3 (integration):** the whole game plays, from the attract through the sign-in,
the hub, the buildings (blueprint, workshop, warehouse rooms with the Cyber Chimps, the eight
puzzles, the races, the results and the post-race menu) to the final movie and the exit, with
`PLAYERS.DAT` written at the original's moments. `ggport` runs the game (the wave 2 preview stays
behind `--gg-preview`, §5). All 840 functions of `re/names/gg` are accounted for: 740 ported, 2
ported with a detail left out, 98 absorbed into engine structures, none stubbed (§10.8, §10.9). The
playthroughs through the app (§11) reach every round 2 shot they pass and every w0 tour shot at 0
differing pixels (a few with the chimp or the pointer masked), except w0/13-15, where w0's chimps
walked at the host's pace and so stand elsewhere; they write `PLAYERS.DAT` byte for byte as
the original did at the new player, Save & Exit and the final movie, and resume a Save & Exit in a
new boot. `ggtests` 284 of 284, `tmtests` 639 of 639 (Treasure Mountain unchanged). What remains is
fidelity work (§8): the per-frame CPU cost of the reference machine, resumes inside modal loops.

**Status after wave 2 (integration):** the shared core `portcore` is extracted from tmport and both
Treasure Mountain editions are unchanged byte for byte (§7.5). Over it, `ggcore` holds the G&G data
layer, graphics, text, sound, input, runtime and app shell, the TLC UI library's drawing and state,
and the drawing halves of the world (hub, rooms, HUD, blueprint, the eight puzzle screens). Every
tool of every area runs from `ggport-cli`; `ggport` runs the **wave 2 preview** (§5). Every stop/go
check passed (§7): 63 screens composed by the tools and 15 moments of the preview at 0 differing
pixels against the DOSBox-X captures, the attract's fades frame-exact, 12 of 13 OPL captures write
for write. The game logic (input loops, walking, chimps, puzzle play, workshop, races) is wave 3's
(§9).

Sizes after wave 3: about 43 800 lines in `src/gg/` (data 3 300, save 1 100, gfx 2 900, text 300,
audio 3 600, input 1 100, app 3 400, ui 5 300, game 22 700) and 14 400 lines of tests in `tests/gg/`
(after wave 2: 24 800 and 5 500).

---

## 1. Shape

```
portcore (cmake/portcore.cmake; tools/check_core_deps.py)      shared with Treasure Mountain
   util, vfs, rtl/borland_rand, timing (clock, pit_clock, deadline_timers), platform (host, console,
   exe_icon), gfx (presenter, palette, image_export, rect), text/text_engine, save/ini_file,
   mods (json, png_codec, wav_file), dos/audio (speaker, PSG, OPL2 ymfm, chip_renderer,
   chip_audio_device, opl_log), dos/input (bios_keyboard, pc_keyboard ScanKeyboard, int33_mouse),
   dos/app/script_input
        ^
ggcore (cmake/gg.cmake + cmake/gg_sources_<area>.cmake)       namespace tmport::gg
   data     GgExe, GgFiles, Archive (LZH), resources, dgroup_offsets, cli_args
   save     PlayersDat, PlayersStore
   gfx      Screen, DirtyMap, SpriteList, Cursor, Palette / Dac / PalFade, Wipe, MoviePlayer, Video
   text     GgFont / Fonts, textDrawBox (core TextEngine with TextDialect::gizmos())
   audio    BMF, SndDriver (1bb4), SndRes (1cdd), MusicCues (12e0), PzSnd (3924), GgAudio
   input    KbdIsr / GgKeyboard, GgMouse (MouseScreen), ClickZones, InputCodes
   app      GgClock, GgTimers, GgRuntime (yield points), Seq_Run, options, ggport.ini, Display, preview
   ui       dialogs, buttons, scroll bar, menu bar, help, hints, text viewer, attract, sign-in
   game     PicCache, ScreenAnim, actors, parts, rooms, HUD, Building, BlueprintView, HubScreen, puzzles;
            wave 3: GgState and GgGame, flow (main, Seq_Run's states, attract, hub), world (states 4,
            5, 7, 8, rooms, items, the HUD strip), actors (the step interpreter, player, chimps,
            collisions), puzzles (state 9, the eight runs, DAM), vehicles (workshop, races, results,
            movie), the state loader and the trace
        ^
ggport (WIN32) / ggport-cli (console, Windows) / ggtests     src/gg/main.cpp, tests/gg/
```

* G&G code includes only core headers and its own: `python -P tools/check_core_deps.py --gg` fails
  on any TM header; the build links `ggcore` against `portcore` alone (no `tmcore`).
* Every original function or module is named in its header with its address and canonical name
  (`re/names/gg/`), e.g. `// 1473:0479 Screen_Wipe`; the DGROUP tables are read from the user's
  `ssg.exe` through `dg::` offsets (`gg/data/dgroup_offsets.h`, generated from `dgroup.tsv`).

---

## 2. Module map

Status: **done** = the routine is ported and verified as noted; **drawing** = the drawing or data
half is done, the logic around it is wave 3's; **stand-in** = not the original's code path. After
wave 3 every "drawing" row has its logic: the wave 3 files are named in the rows (§10.2 lists them by
area, §10.9 by function).

### 2.1 Data and saves

| Original | Engine | Specs | Status, gaps |
|---|---|---|---|
| `ssg.exe` image, DGROUP, VROOMM overlays, stubs | `gg/data/gg_exe.{h,cpp}` `GgExe` | fmt_data §1, ARCHITECTURE §1 | done; refuses other versions (size, MZ, TLINK, copyright, FBOV, 35 stubs) |
| game folder | `gg/data/gg_files.{h,cpp}` `GgFiles`, `detectGgFolder` | PORTING_NOTES §1 | done; names a missing or broken file; ignores `gizmo004.dat` |
| archives 14ff, LZH 1b48 | `gg/data/archive`, `gg/data/lzh` | fmt_data §2-§4 | done; all 1897 entries equal `ggarc.py`; the decoder rejects broken streams (the original would hang); the LZH refill's key drain: a per-thread refill hook (`setLzhRefillHook`, `lzhReadCount`) that flow's `installLzhKeyDrain` sets on the game's thread (code_runtime §6.4) |
| bundles, LPX/palette/font/DAM/BMF/text/table parsers | `gg/data/resources` | fmt_data §5-§7, fmt_graphics | done; `classify` equals `ggarc.py` |
| DGROUP labels | `gg/data/dgroup_offsets.h` | `re/names/gg/dgroup.tsv` | 626 labels (wave 2 added 9, moved `g_animChannels`) |
| tool helpers | `gg/data/cli_args` | — | the shared option, folder and output guard of every `<area>Cli` |
| `PLAYERS.DAT` 2b5a, 2caa:004f | `gg/save/players_dat`, `players_store`; wave 3 `gg/ui/players` | code_ui §12, fmt_data §8 | done byte for byte; the file lives in the pref dir (`playersPathIn`), atomic writes with `.bak`, import / export; written at the original's moments by `playersSaveCurrent` (new player, Save & Exit, state 8, state 16, a won race left, the movie's end) and `rosterSave`: the playthroughs' files equal the captures' (§11) |

### 2.2 Graphics and text

| Original | Engine | Specs | Status, gaps |
|---|---|---|---|
| primitives 1399, screen helpers 1473 | `gg/gfx/screen` `Screen` | code_runtime §10 | done, with the original's clipping, marks and quirks; writes outside the buffer and zero counts are skipped and logged |
| LPX blitters 1399 | `gg/gfx/lpx` | fmt_graphics §2, code_runtime §10.4 | done; 2959 pictures decode as `lpx.py` |
| dirty tiles, sprites 1581 | `gg/gfx/screen` `DirtyMap`, `gg/gfx/sprites` | code_runtime §11 | done, incl. the `Sprites_EraseAll` bit-7 quirk |
| cursor and regions 17ab | `gg/gfx/cursor` | code_runtime §12 | done |
| palette, DAC, fades 1399/1473 | `gg/gfx/palette` | code_runtime §13 | done; DAC shown as `(v << 2) \| (v >> 4)` |
| wipes 1473:0042/0479, Screen_Show | `gg/gfx/transitions` | code_runtime §14 | done; the door wipe is 0x11DB frame by frame |
| movie 1399:0a85, 1473:07d9 | `gg/gfx/movie` | code_runtime §15, fmt_graphics §7 | done (stepper; 202 frames equal `movie.py`); state 15 is `game/results.cpp` (r2/movie shots at 0 px, §11) |
| display (Gfx_Init, Dirty_Flush, VRAM) | `gg/gfx/video` `Video` | code_runtime §10.1, §11.2 | done |
| fonts 2b16, text 2b89 | `gg/text/font`, `gg/text/text` | code_ui §3, §4 | done on the core `TextEngine` |
| the display on the runtime | `gg/app/display` `Display` | PORTING_NOTES §3 | done: presents VRAM after each flush, DAC write and wipe step; the mouse handler's `MouseScreen`; `Mouse_SetBounds` to the driver |

### 2.3 Sound

| Original | Engine | Specs | Status, gaps |
|---|---|---|---|
| BMF songs | `gg/audio/bmf` | fmt_sound §2-§4 | done; listing and event digest equal `bmf.py` for all 495 songs |
| driver 1bb4 | `gg/audio/snd_driver` `SndDriver` (a `SoundTickSource`) | code_sound §2-§12 | done, slot by slot; tables from `ssg.exe`; song state in the bank copies |
| bank layer 1cdd | `gg/audio/snd_res` `SndRes` | code_sound §13 | done; the original's handle numbering and hazards |
| music cues 12e0 | `gg/audio/music_cues` | code_sound §14 | done (room, workshop, movie) |
| puzzle sounds 3924 | `gg/audio/pz_snd` | code_puzzles_a §2.4 | done |
| start-up, modes, device | `gg/audio/gg_audio`, `sound_mode` | code_sound §3, PORTING_NOTES §7 | done on the core `ChipRenderer` / `ChipAudioDevice` / `OplChip` (65 µs per write) |
| captures | `gg/audio/soundtest`, `dro_compare`, `audio_cli` | fmt_sound §5 | 13 schedules; `.dro` capture rules modelled |

### 2.4 Input and runtime

| Original | Engine | Specs | Status, gaps |
|---|---|---|---|
| PIT and Timer_ISR 1454 | `gg/app/gg_clock` `GgClock` on the core `VirtualPit(0x2000)` | code_runtime §1 | done; the retrace clock (70.086 Hz) on the same time base |
| deadline timers 152c | `gg/app/timers.h` `GgTimers` on the core `DeadlineTimers(AfterDeadline)` | code_runtime §5 | done |
| Kbd_ISR, key ring, BIOS keys 1454/198d/1d3c | `gg/input/keyboard` | code_runtime §6 | done on the core `ScanKeyboard`; E0/E1 swallowed; the LZH refill drains it (flow); the races pop one key per frame |
| mouse handler 17a0 | `gg/input/mouse` `GgMouse` | code_runtime §7 | done on the core `Int33Mouse`; moves dropped while drawing; the puzzles' pointer warp moves the host pointer in a window (`game/pointer_warp.h` PointerWarpHook sets `onWarp`; not tried with a real pointer, §8) |
| zones 17ab | `gg/input/zones` `ClickZones` | code_runtime §8 | done |
| Input_GetCode 198d | `gg/input/input_codes` `InputCodes` | code_runtime §9 | done; the global keys call hooks the game installs |
| the program's waits | `gg/app/gg_runtime` `GgRuntime` | PORTING_NOTES §2 | done: yield points pump the host; deterministic headless runs; scripted input in PIT ticks |
| Seq_Run 12e0:000f, exit | `gg/app/seq_run`; wave 3 `gg/game/flow` (the tables DS:00c2 / DS:20a8 from the exe) | code_runtime §3, §4 | done |
| main, Init_CommandLine | `gg/app/gg_app`, `options`, `settings`, `icon`; wave 3 `gg/game/flow` `gameMain` | code_runtime §2, PORTING_NOTES §7 | done; `--gg-args` takes the original switches; ggport.ini in the pref dir; `g_cpuType` 4 and `g_displayCode` 0x1A as on the reference machine |

### 2.5 UI library and attract

| Original | Engine | Specs | Status, gaps |
|---|---|---|---|
| dialogs 3f56, buttons 4034, Yes/No 4106 | `gg/ui/dialog`, `button`, `menu_commands`; wave 3 `ui/dialog_run.cpp`, `commands.cpp` | code_ui §5, §6.5 | done: drawing, layout, zones, `Dlg_RunModal` (time freeze, `Snd_Pause`, the menu hook), the prebuilt boxes |
| scroll bar 406a | `gg/ui/scroll_bar` | code_ui §9 | done (click, auto-repeat, drag steps) |
| menu bar 40a4 | `gg/ui/menu_bar`; wave 3 `ui/menu_run.cpp` | code_ui §6 | done: drawing, zones, `Menu_SetMenuBar`, `Menu_RunDropDown` and every command |
| help 411d | `gg/ui/help` | code_ui §6.6 | done: pages and `Dlg_RunPager` |
| text viewer 4140 | `gg/ui/text_viewer`; wave 3 `ui/text_view_run.cpp` | code_ui §8 | done: drawing, scrolling, `TextView_Run` (the arrow repeat, the thumb drag with the pointer warp) |
| hints 3060 | `gg/ui/hints`; wave 3 `ui/hints_run.cpp` | code_ui §13 | done: box, queue, `Hint_Show`, `Hint_Update` |
| attract 2c11 | `gg/ui/attract`, `attract_anims`; wave 3 `game/flow_attract.cpp` | code_ui §10 | done: every step and `Attract_PlayAnim` as a stepper on the world's `ScreenAnim`, `State_01_Attract` on the game (frame for frame the preview's) |
| sign-in 2caa | `gg/ui/signin`; wave 3 `ui/signin_run.cpp`, `ui/players` | code_ui §11 | done: screen, rows, typing, buttons, `SignIn_Run`, `State_02_SignIn`, the chimps question, Ctrl+R, the boxes |
| ranks 2bd4 | `gg/ui/rank` | code_ui §11.2 | done |

### 2.6 World (drawing halves)

| Original | Engine | Specs | Status, gaps |
|---|---|---|---|
| picture cache 15df | `gg/game/pic_cache` | code_runtime §16.4 | done, with the 300-entry table mirrored by the original's slot rules (`slots()`, the dump's `g_picCache`; equal to DS:6640 after a door) |
| screen animations 174b | `gg/game/screen_anim` | code_world §19.1 | done (every op, `ScreenAnim_PlayModal`) |
| actors 1834, 18f5, 1adb, 2a80 | `gg/game/actors`; wave 3 `actors_run.cpp`, `player`, `collision`, `chimps` | code_world §9-§13 | done: sets, frames, drawing, the step interpreter (46 ops), the player's and chimps' brains, collisions, contacts, the projectile, the 286 path (rand() order exact: §11) |
| parts 153f | `gg/game/parts`; wave 3 `world_parts.cpp` | code_vehicles_races §2 | done: catalogue, deck (rand() calls exact), the list walks, the block |
| rooms 1622 | `gg/game/rooms`; wave 3 `world_rooms.cpp` | code_world §5-§8 | done: loading (3920 rand() calls from the w0 seed, the w0 deal), entry, `Room_Leave`, layers, items, doors, the save block |
| HUD 1a04 | `gg/game/hud`; wave 3 `hud_run.cpp` | code_vehicles_races §4 | done: drawing, `Hud_Update`, the strip layout and commands, the Esc hook |
| building, states 4/5/7/8 | `gg/game/building`; wave 3 `world.cpp` | code_world §4, §14, §15 | done: set-up, resume, `Building_NewVisit`, the warehouse loop |
| hub 12e0:0148 | `gg/game/hub`; wave 3 `flow.cpp` | code_world §3 | done: set-up, frame, dispatch (w0/06, 07 at 0 px) |
| blueprint and workshop 2dee | `gg/game/blueprint`; wave 3 `workshop.cpp` | code_vehicles_races §5, §6 | done: the viewer, `Blueprint_Run`, `State_06_Workshop` (belt, carry, install, paint, recycle, the race door) |
| races and results 3dd6, 3e77, 3f50, 2fcb | wave 3 `race.cpp`, `results.cpp` | code_vehicles_races §8-§14 | done: both race engines, the results, the rank-up, the post-race menu, the final movie |
| puzzles 38e2 and the 8 overlays | `gg/game/puzzles`, `pz_*`; wave 3 `puzzle_run.cpp`, `pz_*_run.cpp`, `dam` | code_puzzles_a, code_puzzles_b | done: state 9's choice, the framework, every kind's loop (the recorded solutions solve variant 0 of each, §11) |

---

## 3. Runtime model

* **Clock.** `GgClock` runs Timer_ISR's work at each interrupt of the virtual PIT (145.652 Hz):
  g_ticks, then the sound tick on even ticks. Real time (a window): the PIT follows the host clock
  at every yield point (at most 32 ticks after a stall). Headless: deterministic; a wait jumps to its
  end, a palette load to the next retrace, the work between yield points costs nothing, and every
  poll after the first since a wait lets 1/16 tick pass.
* **Yield points** (`GgRuntime`): `timerWait`, `timerExpired`, `delay`, `getInputCode`,
  `waitInputCode`, `idle` (any other busy-wait, e.g. a screen animation's wait on an effect),
  `waitRetrace`. Every one pumps the host (SDL events into the keyboard and mouse models).
* **Pictures.** `Display` presents VRAM through the DAC (`GgRuntime::showFrame`) after a flush that
  copied tiles, each retrace of a palette load and each wipe step; the frame sink and `--frame-dump`
  see the same pictures.
* **Sound.** `GgAudio`: in a window an SDL stream ticks the driver by sample count; headless (or
  without a device) the virtual PIT ticks it (`GgClock::setSoundTick`), and `--wav-dump` keeps the
  rendered samples. Game calls run synchronously under the render lock.
* **Input.** Scripted input (`--keys`, `--mouse`) is in PIT ticks: an event of tick n comes right
  before interrupt n. Host keys reach `Kbd_ISR` at once, mouse events the INT 33h handler.
* **The game (wave 3).** One `GgGame` (§10.1) per run on the launch's runtime; every module function
  runs on the main thread as the original's nested loops and yields only through `g.rt`.
  `GgRuntime::setYieldHook` reports every yield: `--gg-trace` writes a row per frame boundary (with
  `--gg-trace-polls` per input poll and busy-wait turn too), `--gg-dump-state` writes the DGROUP
  image at a PIT tick, and the test fixture steps the game frame by frame (§10.7).

---

## 4. Tools (`ggport-cli`, `gg/app/cli_dispatch.h`)

| Area | Commands |
|---|---|
| data | `--gg-info`, `--gg-list [--leaves]`, `--gg-selftest`, `--gg-extract ID\|ID/m OUT` |
| gfx | `--gg-pic`, `--gg-pal`, `--gg-font`, `--gg-movie`, `--gg-title`, `--gg-fade`, `--gg-wipe`, `--gg-lpx-list`, `--gg-font-list`, `--gg-movie-list` |
| audio | `--gg-sound fm\|speaker\|off WHAT OUT.wav`, `--gg-oplog`, `--gg-dro-compare`, `--gg-songs`, `--gg-soundtest-check NAME\|all` |
| runtime | `--gg-clock`, `--gg-input-trace` |
| world | `--gg-screen hub\|warehouse\|blueprint\|puzzle ...`, `--gg-rooms RACE` |
| ui | `--gg-screen NAME OUT.png` (title, after-title, story1..6, after-story, before-prompt, prompt, signin, name, chimps, hint:ID, yesno:ID, message:ID, menubar, menu:..., help:..., textview:...), `--gg-ui-info` |
| the game (wave 3, `ggport` / `ggport-cli` without a tool command) | `--gg-trace FILE [--gg-trace-polls]` (a CSV row per frame: `game/trace.h`), `--gg-dump-state TICK FILE` (the DGROUP image at a PIT tick, and `FILE.fields.tsv`: every field's DS offset, size and modelled bytes), `--gg-load-state FILE` (start from a DOSBox-X state: §10.6), `--gg-preview` (the wave 2 preview), with `--headless`, `--ticks`, `--keys` / `--mouse`, `--seed`, `--frame-dump`, `--wav-dump`, `--opl-log` |

Every command takes `--data DIR` and refuses to write into the game folder. The world tool answers
`--gg-screen` for its four screens, the UI tool for every other name. `python -P
tools/gg/gen_engine_map.py OUT.md` regenerates §10.8 / §10.9 from the address comments of `src/gg`.

---

## 5. The game and the wave 2 preview (`gg/game/flow.h`, `gg/app/preview.{h,cpp}`)

Since wave 3 `ggport` without a tool command runs **the game** (`gameMain`: main's start-up,
`Game_Init`, `Seq_Run` over the 17 states, `Game_Shutdown`; §9, §11). The wave 2 preview stays
behind `--gg-preview` (marked "Wave 2 preview: replaced by the game modules in wave 3"):

1. the attract as `State_01_Attract` runs it (title phases, after-title, six story pages,
   after-story, before-prompt, prompt, restart), with the title music and the animations' effects;
   Enter goes to the next page, Space (or Enter at the prompt) to the screens, Esc quits;
2. Enter or Space then steps through: the sign-in (typing, the list, Up / Down and the buttons on a
   read-only copy of the roster; nothing is saved), the hub (windmill, cursor labels; a click on
   the sign-in booth or a building goes there), a warehouse room of the first race of the building
   (no actors; Left / Right enter the next room with the random wipe), the eight puzzle screens
   (variant 0), and back to the attract;
3. every screen plays the music its state plays (title music arrangements 1 and 2, the room music
   with its cue, the puzzles' silence); number keys play the screen's effects (the effect bank, the
   global bank in the hub, the PzSnd bank in the puzzles); Ctrl+S toggles the sound through
   `Input_GetCode`'s global key.

Options as for the game (`ggport --help`), plus `--wav-dump FILE` (headless) and `--opl-log FILE`.
With `--verbose` the preview logs its sound calls in the sound tool's schedule syntax.

---

## 6. Tests (`tests/gg/`, `ggtests`)

After wave 3: **284 test cases** (541 601 assertions with the user's files and the captures). The
wave 3 areas added theirs on the game fixture (§10.7): the skeleton (11: linking, the start-up
state, the codec on all 143 states, the state reader, the fixture), flow (16), ui (16), world (19),
actors (25), puzzles part A (18) and B (17), vehicles (9), and the integration's playthroughs
through the app (9, `test_gg_playthrough.cpp`, §11). After wave 2: 144 test cases: data (archives, LZH, resources, `PLAYERS.DAT` round trips and write moments), gfx
(primitives, blitters, dirty tiles, sprites, cursor, palette, fades, wipes, movie; the five r2 movie
shots, the title and story page 1 at 0 px), text, audio (the 13 schedules against their `.dro`
captures, the song listing), input and runtime (the ISR models, zones, global keys, yield points,
scripts), app (options, ggport.ini, icon, headless runs), UI (32 captures at 0 px), world (31
captures at 0 px, the w0 set-up's rand() count and deal), and the integration (§7). Tests with the
user's files read `GG_DATA` and `GG_CAPTURES` and skip without them; they store counts, ticks,
coordinates and CRCs only.

---

## 7. Stop/go results (SCOPING §9, wave 2)

Run in the integration build (`build_gg2_integration`, MSVC, Release) with the user's files and the
DOSBox-X captures. Scratch scripts: `extracted/scratch/gg/wave2/integration/` (`stopgo.py`,
`timing.py`, `audiocmp.py`, `prev.py`).

### 7.1 Screens through the tools (`ggport-cli --gg-screen`): 63 of 63 at 0 differing pixels

* UI (32): title (phase 3), story pages 1 and 2, sign-in (empty roster, typed name, chimps question,
  roster of two players, a new player, Bob selected, after a restart), 4 hints, the File, Options and
  Help menus, the menu bar, 4 Yes/No boxes, the help pager's page 0, 5 text-viewer pages; whole
  frames or the UI rectangles, the cursor drawn at the shot's pointer.
* World (31): the hub twice, 9 warehouse rooms (w0 and r2 Automotive, Aircraft, Alternative Energy;
  w0/14 compared as its two torn halves), the 8 puzzle kinds and w0/17 (the hint box's leftover arrow
  masked, 81 px), 11 blueprint viewer boxes (the workshop around the box masked, wave 3).

### 7.2 The preview's own frames: 15 of 15 at 0 differing pixels

A headless preview run with the w0 tour's seed (`--seed 10703` = 0x29CF), Space on story page 1,
"ada" typed, the pointer on the sign-in booth, a click on the Aircraft building, the eight puzzles.
For each capture the closest preview frame: w0/01 title, w0/02 story page 1, w0/03 sign-in, w0/04 the
typed name, w0/06 hub: 0 px with no mask; w0/14 warehouse (the w0 deal: Rooms_Load from the same
rand() state): 0 px in each torn half, the player, the chimp and the crate the chimp took (wave 3)
masked; r2/puzzle k0..k7: 0 px with the two cursors masked. The same check runs in `ggtests`
("integration: the preview through every screen against the captures"). (The w0 tour itself typed
Space during story page 3: `g_storyTextId` DS:a04e = 0x101c in w0/03 and w0/06, `g_ticks` 0x3343 at
w0/03. Space on page 1 gives the same frames but not that word or `g_ticks`; a DGROUP comparison
types Space on page 3, as the wave 3 playthrough does, §11.)

### 7.3 Attract timing against `w0_replay_title_attract.avi` and `w0_title_attract.avi`

The preview's frame dump (PIT tick of every picture) against the AVIs' changed frames (70.0863
frames/s), matched by exact pixels:

* `w0_replay_title_attract.avi` (starts at the title's fade-in): 233 of its 245 changed frames equal
  a preview picture. The **title fade-in** (AVI 15..30, 16 frames), the **fade from the saved
  palette** after the title (651..668), the **fade to black** (861..882, 22 frames) and the **story
  page 1 fade-in** (887..902, 16 frames) are frame-exact: each AVI frame equals the preview picture
  of the same retrace (one picture per retrace, 2 per palette load).
* Offsets (AVI frame − preview frame, video frames): the title phases 1-3 hold +12.8 ± 0.4 from the
  fade-in to the after-title animation; the after-title step starts 1.5 frames (3 PIT ticks) later in
  the AVI, the story page 1.8 frames (3.7 ticks) later again; from story page 1 to page 2 the offset
  stays within 0.06 frames (the 0x111C-tick page time). The black gap between the fade to black and
  the story page's fade-in is 5 AVI frames against 3.4 in the preview. So the preview keeps the
  original's timing between events and is 3-5 frames early in all at whole-screen changes: the cost
  of loading and drawing a screen on the reference machine, which the engine does not model yet
  (PORTING_NOTES §2.4).
* `w0_title_attract.avi` (starts 6 s into the title): 180 of 184 changed frames match; the after-title
  step +2.5 frames, the story page +2.7 frames later than the preview's (the two boots differ by a
  few ticks, as REFERENCE_CAPTURES §3.3 noted for the sound).
* The sound shows the same: the title's effects 12 and 13 start 73 and 293 sound ticks after the
  title music in the preview, 80 and 301 in the capture (the loads before the first frame cost about
  14 PIT ticks there); between the phases the preview is within 1-2 sound ticks.

### 7.4 BMF songs against the `.dro` captures (`ggport-cli --gg-soundtest-check all`)

| Schedule | Capture | Writes identical |
|---|---|---|
| title, title_replay | w0_title_attract, w0_replay_title_attract | 4225 / 4225, 3586 / 3586 |
| hub, postrace | r2_hub_idle, r2_postrace_to_hub | 1521 / 1521, 549 / 549 |
| warehouse, workshop | r2_warehouse_walk, r2_workshop_idle | 1186 / 1186, 669 / 669 |
| puzzle_k4, puzzle_k5 | r2_puzzle_k4_success, r2_puzzle_k5_miss | 356 / 356, 162 / 162 |
| race_automotive, race_altenergy | r2_automotive_race1_won, r2_altenergy_race1_won | 1421 / 1421, 2899 / 2899 |
| race_aircraft_won, race_aircraft_lost | r2_aircraft_race1_won, r2_aircraft_race1_lost | 2685 / 2685, 2598 / 2598 |
| movie | r2_movie_race15 | 7050 / 8320 (to the movie's end; the wait for a key is not reconstructed) |

The preview's offline sound (the driver ticked by the virtual PIT, `--wav-dump`) equals the sound
tool's render of the same calls sample for sample: 3 460 301 samples (72.09 s of the attract); the
same check runs in `ggtests`. Longer preview runs differ from the tool's replay only after calls the
schedule syntax cannot express (the room music bank loaded before the warehouse effects, a PzSnd
bank freed), which change the driver's slot order.

### 7.5 Treasure Mountain unchanged

The core agent's baseline runs (Windows tools and screens, the Windows game headless, every DOS
look's screens and playthrough, the DOS sound renders and OPL logs, a state load, both autoplay
soaks) with the integrated build: all 417 output files equal the pre-core baseline (357 byte for
byte, 60 text logs after normalising the run folder they name and the wall times). `tmtests` 639 of
639, `ggtests` 144 of 144. Linux (WSL 2, GCC 15): `portcore`, `ggcore`, `ggport` and `ggtests`
build without a warning in the project's code and all 144 `ggtests` pass with the user's files.

---

## 8. Known gaps and deviations (after wave 3)

* **Frame costs.** The work between yield points costs no time, so whole-screen changes come 3-5
  video frames early against the AVIs (§7.3) and the long-frame model of PORTING_NOTES §2.4 is not
  implemented; in real time, `g_ticks` read between yield points is stale by the work time. Where
  the reference machine's cost moves an effect write onto the other sound-tick parity, the OPL order
  differs from the `.dro` by one sound tick: the aircraft race, the automotive and aircraft-lost
  result tails, and the movie after 32.8 s (the vehicles tests report these counts, they do not
  assert them).
* **Starting from a DOSBox-X state** (`--gg-load-state`, §10.6) works at the main loops' moments:
  the hub, the workshop, a warehouse room, a puzzle's main loop and a race (which restarts from its
  start: the racer records are not read back). It refuses a state saved inside a nested modal loop:
  a dialog box, a drop-down, the help pager or the text viewer (their save-unders and layouts live in
  the state's heap), a puzzle's inner loops (the electric rubber band, the launch plunger, a DAM
  transition, the success sequence, the energy eraser and line drawing), and states 1, 2, 4, 5, 8,
  10 and 12..17. A state saved inside a box (r2/aircraft/30, 35) loads with the box's zones pushed
  over the loop's, so it does not play on (the UI's restore warns). After a resume the music restarts
  from its beginning (the driver's position is not in DGROUP), the warehouse's local Alt+S flag is
  lost, and ties in zone order between modules are not exact.
* **Partial functions** (§10.9): `Actor_AddPart`'s two stores into the part record that nothing
  reads (+0x8A = 0, +0x1A = 0x2238); `Roster_LoadOrCreate`'s creation while `g_rosterIndex` ≠ 0 (the
  file deleted during a session) is logged, not modelled.
* **Deviations by design:** archives with a broken end marker or another version are refused; the
  LZH decoder rejects broken streams; guards where the original reads or writes past arrays or
  hangs (hint slot 5, the 17th typed byte, unbounded instance searches, zero counts, unknown actor
  ops and the end marker −1000, `PartList_FindHeld` with no eligible part, `Room_FindLinkedDoor` and
  `PartList_FindType` past the end, `Rooms_RestoreState`'s `List_Clear` of room 0's NULL list, the
  hall sequences' NULL layer, a 1 000 000-try guard on the machine pieces' rand() retry); the energy
  puzzle's uninitialised "last pointer" locals start defined (the engine draws at the first poll); a
  64-bit deadline instead of the u32 wrap after 341 days; the `-?` switch exits 0; saves are atomic
  with `.bak`; `Players_WriteError` shows box 0x106F and exits 1 but neither prints the text-mode
  message nor deletes the file.
* **Not exercised against the reference:** the real pointer warp (`PointerWarpHook`) with a real
  window and pointer (headless runs and the offscreen windowed smoke run do not install it); the
  launch roll-back, the gravity slider and the ramp, the energy eraser, puzzle variants other than
  the captured ones (variant 0 of each kind, machine pieces 2) and levels above 1; chimps throwing,
  stealing and dropping in front of a capture (no capture, REFERENCE_CAPTURES §13); a full vehicle
  build from the belt (the engine's belt, carry, install and race door were played, the original's
  build input was not recorded); the help pager's later pages, the Ctrl+R / duplicate / list-full
  boxes, a long roster's scroll bar and a pressed picture button against a shot (the UI tests check
  them in the engine); the movie's last 1 270 OPL writes; ten off-grid writes of the Alternative
  Energy race capture and a 3.5-s silent stretch of the `-si` WAV.
* **Tests only:** the fixture's `RecordingSound` numbers handles without reusing freed ones, so a
  fixture DGROUP comparison shows `g_roomMusicBank` and `g_effectBank` as different although the
  game is right (the app's `GgAudioSound` uses the bank layer's numbering); it never reports an
  effect as playing, so busy-waits on sound end at once there (the playthroughs of §11 run the real
  facade).
* **Unmodelled globals nothing reads:** DS:1328 (set to −1 when a flash ends), DS:132e / DS:874e
  (written 0 by `Hud_DrawStrip`).

---

## 9. Wave 3: the game logic

The plan followed SCOPING §9 and the wave 1 specs; each item built on the wave 2 interfaces above.
All of it is done (§10.9, §11):

1. **States and loops** (`Seq_Run` table DS:00c2): the attract on the game, the sign-in with
   `SignIn_Run` and the chimps question, `PLAYERS.DAT` written at the original's moments, the hub's
   dispatch, `Dlg_RunModal` (time freeze through `GgClock::setTicks`, `Snd_Pause`, the menu hook),
   `Menu_RunDropDown`, `Dlg_RunPager`, `Hint_Show` / `Hint_Update`, `TextView_Run`.
2. **Warehouse:** `Building_NewVisit`, `Chimps_Spawn` (with the 286 path), the actor step
   interpreter and brains (player, projectile, chimps), collisions and contacts, items, doors and
   the room transitions, the HUD strip commands, the warehouse loop with its rand() order,
   `Room_Leave`.
3. **Puzzles:** the choice (rand()), history and reward; the eight input loops on the `Pz*` classes
   with their zones, PzSnd calls and hint viewers; the DAM engine.
4. **Workshop, races, results, movie:** the workshop screen and `Blueprint_Run`, the two race
   engines, the results count-up and rank message, the post-race menu, the final movie state.
5. **Runtime details:** the LZH key drain, the races' key pop, the pointer warp (`PointerWarpHook`
   in a window); the per-operation frame costs remain (§8).
6. **Fidelity:** the playthroughs of §11 run the game through the app from a fresh boot and from the
   round 2 states and compare every picture with the captures (`tests/gg/test_gg_playthrough.cpp`);
   a frame-cost model (a resident-code frame logger, PORTING_NOTES §2.4) and resumes inside modal
   loops are the next fidelity steps.

The skeleton of this plan (the game context, the state, every module's declarations, the test
fixture) and who filled what is §10.

---

## 10. Wave 3 map

The skeleton of wave 3: one game context, one state, one header per original module with a declaration
(address, name, one-line contract) for every function of the game logic, linking stubs, the test
fixture and this map. Each module area then filled its stubs (`// TODO(wave3)` bodies with safe
defaults); after the wave 3 integration none is left (§10.8). The specs say what each function does;
§10.9 says where it is and who wrote it.

### 10.1 Shape

```
GgGame (game/game.h)  : public GgState                  one per run, handed to every module function
   GgState (game/game_state.h)      every DGROUP global the logic uses, typed, by owning module:
      player (PlayerRecord DS:4562)  flow  ui  world  actors  puzzles  vehicles  sound  runtime
   tables (GgTables)                 the read-only DGROUP tables parsed once (transitions, chimp moves, ...)
   files / exe() / archive()         the user's files
   rt (GgRuntime)                    clock, PIT, timers, yield points, keyboard, mouse, zones, Input_GetCode
   video, display, fonts, cache      Video (screen, back page, VRAM, sprites, cursor, palette), Display,
   hud, screenAnims                  Fonts, PicCache, the HUD's pictures, g_screenAnims DS:7a46
   sound (SoundFacade&)              GgAudioSound in the game, RecordingSound in tests
   rand (BorlandRand&)               the one rand() (counts its calls: BorlandRand::calls)
   players (PlayersStore&)           PLAYERS.DAT in the preference folder
   flowObjects() .. vehiclesObjects() each module's engine-side objects (pictures, sets, the running
                                     puzzle, the sign-in screen, the workshop's sprites)
```

* **Runtime model** (§3, PORTING_NOTES §2.2): the original's nested blocking loops are nested functions
  on one thread. A module function runs until the original's would return; every wait or poll is a
  `g.rt` yield point (`timerWait`, `timerExpired`, `delay`, `getInputCode`, `waitInputCode`, `idle`,
  `waitRetrace`), which runs the virtual PIT and pumps the host. No module sleeps, reads host time or
  owns a frame loop. `GgRuntime::setYieldHook` reports every yield (the fixture and `--gg-trace` use
  it).
* **Seq_Run.** `mainStateTable(g)` (game/flow.cpp) binds states 1..17 to `state01Attract` ..
  `state17End`; `runMainSequence` runs `seqRun` over it with `g.flow.state` (DS:455c).
* **Entry points.** A state function that can be resumed from a DOSBox-X state takes
  `StateEntry entry = StateEntry::Normal` (gg/app/seq_run.h); `Resume` skips the entry code (no
  load, fade or wipe) and goes on with the loop's next frame: `state03Hub`, `state06Workshop`,
  `state07Warehouse`, `state09Puzzle`, `state11Race`.
* **Start-up.** `GgGame`'s constructor is main's part before Seq_Run (`initialState(exe)`: the exe's
  initialised data, then `gameInitState`: g_state 1, Hud_Init's flash words, Hint_Init, Actors_Init's
    fixed fields; then Init_CommandLine's g_cpuType / g_soundDevice / g_displayCode / g_randSeed from
  `GgGameOptions`, Gfx_Init, Hud_Init's pictures, Ctrl+S). `gameMain` (flow) then stores what the
  checks give on the reference machine (a 486 under DOSBox-X): `g_cpuType` DS:2450 = 4 (2 with
  `--cpu 286`) and `g_displayCode` DS:244e = 0x1A (`Video_DccSupported`'s AL; 8 after -GV), as the
  w0 and r2 states hold them; `gameInit` runs Game_Init's calls (`actorsInit` sets every field of
  `Actors_Init`, `gameInitState` only part of them).

### 10.2 Areas, owners and files

Every area lists its sources in `cmake/gg_sources_<area>.cmake` and its tests in
`cmake/gg_tests_<area>.cmake` (included by `cmake/gg.cmake`; the wave 2 per-area lists stay).
Wave 2 files that a wave 3 area extends keep their place; their "wave 3: the run" sections belong to
the owner named here.

| Area (cmake) | Owner's work | Wave 3 files | Wave 2 files it extends |
|---|---|---|---|
| skeleton | the state and codec, the context, the facade interface, the trace record, the state reader, the fixture | `game/game_state`, `game/game`, `game/sound`, `game/trace`, `save/gg_state_file`; `tests/gg/gg_game_fixture`, `test_gg_game_skeleton` | (core) `dos/save/dosbox_state_file` |
| flow | main, Game_Init / Shutdown, Seq_Run's table, State_01 (the attract), State_03 (hub), 16, 17, Building_Free, Save & Exit, quit / exit, the LZH key drain, the facade over GgAudio, GgApp's switch from the preview to the game (the preview behind `--gg-preview`), `--gg-trace`, `--gg-dump-state`, `--gg-load-state`, `--seed` | `game/flow`, `game/flow_attract`, `game/sound_audio.cpp`, `game/load_state`, `game/trace_log` | `app/gg_app`, `app/preview`, `game/hub`, `ui/attract`, `ui/attract_anims`, `audio/*` (as the facade's back end) |
| ui3 (ui agent) | Dlg_RunModal and the boxes, Menu_SetMenuBar / Menu_RunDropDown, the commands and help topics, Dlg_RunPager, TextView_Run, list box and scroll bar on the game, SignIn_Run and State_02, PlayersStore's saves at the original's moments, the hints API | `ui/ui_objects.h`, `ui/players`, `ui/dialog_run.cpp`, `ui/menu_run.cpp`, `ui/commands.cpp`, `ui/text_view_run.cpp`, `ui/signin_run.cpp`, `ui/hints_run.cpp` | `ui/dialog`, `ui/menu_bar`, `ui/menu_commands`, `ui/help`, `ui/text_viewer`, `ui/signin`, `ui/hints`, `ui/scroll_bar`, `ui/button` |
| world3 (world agent) | states 4, 5, 7 (the warehouse loop), 8, Building_NewVisit, the room loader, doors, items, Room_Leave, the HUD strip commands and the Esc hook, PicCache's slot table, ScreenAnim_PlayModal | `game/world`, `game/world_rooms.cpp`, `game/world_parts.cpp`, `game/hud_run.cpp` | `game/building`, `game/rooms`, `game/parts`, `game/hud`, `game/pic_cache`, `game/screen_anim` |
| actors | the 12 slots, the step interpreter, tick and draw, the player controller, collisions and contacts, the projectile, the Cyber Chimps | `game/actors_run.cpp`, `game/player`, `game/collision`, `game/chimps` | `game/actors` |
| puzzles_a | state 9 and the framework (38e2), electric circuits, energy forms, launch with the DAM engine, simple machines | `game/puzzle_run.cpp`, `game/dam`, `game/pz_electric_run.cpp`, `game/pz_energy_run.cpp`, `game/pz_launch_run.cpp`, `game/pz_simple_run.cpp` | `game/puzzles`, `game/pz_electric`, `game/pz_energy`, `game/pz_launch`, `game/pz_simple`, `audio/pz_snd` |
| puzzles_b | machine pieces, gears (the pointer warp: `GgMouse::warp` and its `onWarp` hook), balance, magnets | `game/pz_pieces_run.cpp`, `game/pz_gears_run.cpp`, `game/pz_balance_run.cpp`, `game/pz_magnets_run.cpp` | `game/pz_pieces`, `game/pz_gears`, `game/pz_balance`, `game/pz_magnets` |
| vehicles | the workshop and Blueprint_Run, states 10 and 11 and the two race engines, the results with the points and the rank-up, the post-race menu, completion and trophies, the final movie | `game/workshop`, `game/race`, `game/results`, `game/vehicles_common.h`; `tests/gg/gg_vehicles_scenario` | `game/blueprint`, `ui/rank` |
| integration | the cross-module fixes, the playthroughs through the app, the docs | `tests/gg/test_gg_playthrough.cpp` (in `cmake/gg_tests_integration.cmake`); `tools/gg/gen_engine_map.py` | any file, minimally |

puzzles_b also added `game/pointer_warp` (`PointerWarpHook`: `GgMouse::onWarp` moves the host
pointer while gears or magnets run in a window) and `game/pz_restore.h` (the state's cursor record of
a resumed part-B puzzle).

### 10.3 Shared-state conventions

* **One home.** Every DGROUP global the logic uses is a field of `GgState` (named after
  `re/names/gg/dgroup.tsv`, its DS offset in the comment). Modules read and write `g.<section>.<field>`;
  no module keeps a second copy. The section says who writes it: a module writes its own section, the
  player record's fields that its functions write in the original (world: `live`, the building
  blocks, `level` / `raceIndex` / `building`; vehicles: the workshop and race fields, `racesWon`;
  puzzles: `puzzleSolved`; ui: the whole record at sign-in, `soundOff`, `hintsOff`, `hintSeen`), and
  other sections only through the owner's functions (`scoreAdd`, `roomRemoveItem`,
  `roomOpenPuzzleDoor`, `hudSetInfoText`, ...).
* **Service-held globals** live in the runtime's and Video's objects, not in GgState:
  g_ticks DS:021a and g_timers DS:6578 (`g.clock()`, `g.timers()`), g_kbdPlayMode, g_ctrlHeld,
  g_heldKeys, the key ring's indexes (`g.keyboard()`), g_lastKeyHeldTick, g_idleReported,
  g_keyHeldSeen, g_menuRequest, g_menuDisabled (`g.input()`), g_mouseButtons, g_mouseX / Y
  (`g.mouse()`), g_mouseHideState, g_drawColor (`g.video.screen()`), g_cursorHidden
  (`g.video.cursor()`), g_palFaded, g_palShadow, g_palSaved (`g.video.palette()`), g_randState
  (`g.rand`). `serviceFields()` lists them; `dumpToDgroup(const GgGame&)` writes them and
  `loadFromDgroup(GgGame&, ..., withTime)` restores all but the key ring indexes, the mouse buttons and
  the hide state (store-only).
* **Widgets are not state.** Click zones (`g.zones()`, the zone stack), picture buttons, cursor
  regions and save-unders are owned by the module objects that create them; GgState keeps only the
  globals the original keeps in DGROUP.
* **Heap structures** (the room set copy and item pool, the part catalogue and the carried lists, the
  roster copy, the puzzles' variant copies, the vehicle sprites) are engine objects inside GgState's
  sections (`world.rooms`, `world.parts`, `ui.roster`, ...) but not in the DGROUP image: the codec
  leaves them alone, and the `*RestoreFromState` functions rebuild them from a DOSBox-X state's RAM.
* **Sound.** Modules call the facade (`g.sound`), never GgAudio. A handle the original keeps in DGROUP
  (title, room, workshop, movie and race banks; g_effectBank DS:7a44) is kept in GgState's sound
  section (`-1` = none) by the module that loads it, and freed by the module the original frees it in;
  after changing the effect bank call `g.sound.setEffectBank(h)` (the screen animations and
  SndRes_StopAllEffects use it). `GgGame::sound` (the facade) hides `GgState::sound`, so the handles are
  written `g.GgState::sound.<field>`. Tests read `RecordingSound`'s calls. `SignIn_Free` frees bank
  0x102F but leaves its handle in g_effectBank, as the original does: the facade tolerates the stale
  handle until the next screen loads a bank.
* **rand().** Only `g.rand`, in the original's call order (PORTING_NOTES §4.1). The fixture's trace
  records `rand.calls()` per frame, so an extra or missing call shows at once.
* **Saves.** `playersSaveCurrent(g, moment)` (ui) at the original's moments (`GgSaveMoment`),
  `rosterSave` for the roster; nothing else writes PLAYERS.DAT.
* **Hooks.** Input_GetCode's global keys go through `g.input().hooks()` (Ctrl+S is installed by
  GgGame; the HUD codes 5000..6003 by `hudInstallHooks`), the Esc hook and the menu handler through
  InputCodes' setters; a module that installs a hook removes it when its state ends, except the HUD
  codes: the original hard-wires them in its global key table (CS:0275 / 028d) and they act in every
  state, so the world installs them at a building's set-up (and on a restore) and leaves them; only
  g_escHandler is the warehouse loop's own.
* **Binding** (done in wave 3): HUD and Building work on GgState (Hud::bind, Building's GgState
  constructor); the eight Pz* classes are bound to `g.puzzles.<kind>` (tools get an own copy); SignIn
  and TextViewer to `g.ui` (`SignInVars`, `TextViewer::bind`); HubScreen and AttractAnimHost run on
  `g.screenAnims` and release their pictures from the game's cache. So the DGROUP dump shows the live
  values everywhere.

### 10.4 Cross-module calls

| Caller | Calls | Declared in |
|---|---|---|
| flow: Seq_Run's table | `state02SignIn`; `state04BuildingSetup`, `state05EnterRoom`, `state07Warehouse`, `state08LeaveRoom`; `state06Workshop`, `state10ListVehicleParts` .. `state15FinalMovie`; `state09Puzzle` | ui/signin.h; game/world.h; game/workshop.h, race.h, results.h; game/puzzles.h |
| flow: hub, 16, Save & Exit | `menuSetMenuBar`, `dlgYesNo`, `playersSaveCurrent`, `buildingFree` (which also clears the player's carried list, actor 0 +0x72, keeping the +0x70 count: code_world §20 bug 7) | ui/menu_bar.h, ui/dialog.h, ui/players.h, game/flow.h |
| ui: menu commands | `cmdSaveAndExit`, `cmdReturnToCenter` (g_leaveRequest), `helpShowTopic`, the facade's `toggleSound` | game/flow.h, ui/menu_commands.h, ui/help.h |
| world: warehouse loop | `actorsTick`, `actorsDraw`; `hudUpdate`; `hintUpdate`, `hintQueue`; `menuSetMenuBar`; `cmdSaveAndExit` | game/actors.h; game/hud.h; ui/hints.h; ui/menu_bar.h; game/flow.h |
| world: states 4, 5, 8 | `chimpsSetupActors`, `chimpsSpawn`, `actorsLoadPlayerSets`, `actorsRestoreState`, `actorsEnterRoom`, `actorsLeaveRoom`, `chimpsFreeSets`, `actorsFreePlayerSets`; `playersSaveCurrent` | game/chimps.h, player.h, actors.h; ui/players.h |
| actors | `roomRemoveItem`, `roomDropItemAt`, `itemPlaceInRandomRoom`, `roomFindLinkedDoor`, `objHide` / `objShow`, `hudAddPart`, `hudFlashPart`, `scoreAdd`, `projectilesAdd`; `hintQueue`; door and puzzle hits through `g.world.puzzleDoorHit` / `vehicleDone` and `g.flow.leaveRequest` | game/rooms.h, hud.h, world.h; ui/hints.h |
| puzzles | `puzzleShowHintText` -> `textViewRun`; `hintShow(0x1052)`; `hudAddScoreNow`; `roomOpenPuzzleDoor`; `damPlayModal(g, PzLaunch&, script, frames, newSurface)`; the facade's `pz*` (`pzSndWaitEnd` is puzzles.h's busy-wait over `pzIsActive`); `GgMouse::warp` | ui/text_viewer.h, ui/hints.h, game/hud.h, game/world.h, game/dam.h, game/puzzles.h |
| vehicles | `hintQueue` / `hintShow`; `hudUpdate`, `hudSetInfoText`, `hudRefreshStrip`, `hudStripAnchorFor`; `actorRemovePart`, `playerSequenceEnd`; `itemPlaceInRandomRoom`, `scoreAdd(g, points, hud = 0)`, `screenAnimPlayModal(g, const ScreenAnim&)` (the animation's own state word +8, code_world §19.1); `cmdSaveAndExit`; `playersSaveCurrent`, `dlgMessageRes`; the facade's workshop, race and movie music | ui/hints.h; game/hud.h; game/actors.h, player.h; game/rooms.h, world.h, screen_anim.h; game/flow.h; ui/players.h, ui/dialog.h |
| every module | `g.rt` yields, `g.zones()`, `g.input()`, `g.timers()`, `g.rand`, `g.sound`, `g.cache`, `g.video`, `g.display.flush()` | gg/app, gg/gfx, game/game.h |

### 10.5 Quirk helpers (game/game_state.h)

Memory-layout behaviour the specs rely on, so modules do not re-derive it:

| Helper | What the original does |
|---|---|
| `chimpList(s, t, race)`, `chimpListFirstRoom` | g_chimpLists DS:1bae points into the initialised words DS:1adc..1bad; the 286 path writes the first room word of the list in place, so the change lasts for the session (GgState keeps the words: `actors.chimpListData`) |
| `typedNameByte(ui, 16)` | a 16th 0x1F marker of the name entry writes DS:a064, past g_typedName |
| `energySatisfied` / `energySent` / `energyLine` | the energy form rows DS:adb8: `sent` is indexed [device][form], the other two [form][device] |
| `elecPathWord(e, 10, 0)` | an 11th electric path row overwrites g_elecPathCount DS:a2c8; other words past the table are not modelled (nullptr: stop and log) |
| `puzzleSolved(record, kind, variant)`, `hintSeen(record, id)`, `buildingBlock(record, b)` | the player record's tables with the original's indexing (range-checked) |
| HintQueue's sixth slot | Hint_Queue with five pending hints writes a sixth record over DS:a138 (kept as a sixth slot, logged) |
| `GgTimers` | Timer_Expired is strict, Timer_Wait is not (a Timer_Set(n) + Expired loop lasts n + 1 ticks) |

### 10.6 Starting from a DOSBox-X state

`GgStateFile` (gg/save/gg_state_file.h) on the shared reader `dos::DosboxStateFile`
(dos/save/dosbox_state_file.h, also behind TM's `DosboxState`):

* DGROUP is found by the Borland signature at DS:0004; the program was loaded 0x786 or 0x7dc
  paragraphs below Ghidra's segments (relocation -0x786 / -0x7dc in the 143 states); the stack segment
  is DGROUP + 0xBD1 paragraphs. `stackReturns()` lists the resident code segments on the stack
  (Ghidra segment:offset), so a test can tell where the original was.
* The "Vga" entry ends with 256 KB of video memory followed by 2 576 bytes; mode 13h byte `a` is at
  `16 * (a >> 2) + (a & 3)` of that memory, and the 6-bit DAC (768 bytes) lies 6 767 bytes before it.
  Over the 143 states the DAC equals the shadow palette DS:024e in every state, and the RAM screen
  (g_screenBuffer DS:0248, 320 x 400 with the back page) equals VRAM except in the movie state and in 3
  states where two dirty tile groups were not yet flushed.
* `apply(g, withTime)`: `loadFromDgroup(g, ...)` (GgState and the service-held globals), the screen
    buffer into both pages, VRAM and the DAC into Video, the cursor's position; and the software
    cursor the state's RAM screen holds: the mouse hide state when DS:01e2 is 0, and, when
    g_cursorShown DS:7a7c is set, the active copy gets the state's save-under (DS:7a7e at DS:7a6c /
    7a6e), so the next `Mouse_RestoreUnder` erases the old arrow as the original's would (before the
    wave 3 integration it stayed on the screen). Heap structures follow through the modules'
    `uiRestoreFromState`, `worldRestoreFromState`, `actorsRestoreFromState`, `vehiclesRestoreFromState`,
    `puzzlesRestoreFromState`, `flowRestoreFromState` (each reads its far pointers with `farPointerAt` /
    `bytesAt`), called in that order by flow's `loadGgState`, which then puts back the picture cache's
    records (`picCacheRestoreFromState`: the restores' loads counted uses again) and the state's
    palette and DAC (`GgStateFile::applyPalette`: the restores' picture loads applied their palette
    members; without it a resumed puzzle's colours were wrong). `runFromLoadedState` enters g_state's
    function with `StateEntry::Resume` and runs Seq_Run on from its result.
* The resume points and the restores they need (`load_state.cpp`): 3 the hub (flow); 6 the workshop
    and 11 a race (world, actors, vehicles); 7 a warehouse room (world, actors; vehicles optional); 9 a
    puzzle at its main loop (world, actors, puzzles; vehicles optional; the kind's restore loads its
    effect bank, without which `PzSnd_WaitEnd` after a solve waited on the room's music); the UI's
    restore is optional everywhere (the bar's title zones and hook, the roster copy; it refuses a
    state inside a modal loop with a warning). Every other state is refused with the modules'
    reasons (§8).
* The codec round-trips every one of the 143 states (`r2`, `w0`): DGROUP -> GgState -> DGROUP gives
  back every modelled byte (test "real DOSBox-X states round-trip through GgState"). Unmodelled bytes:
  far pointers (their segments are run-time values), the actors' heap words, the item and hint list
  links.

### 10.7 Tests: the fixture (tests/gg/gg_game_fixture.h)

`gggame::GameFixture f(*ggFiles)` builds a headless, deterministic GgGame on GG_DATA with a
`RecordingSound`, a scratch preference folder (removed afterwards; never %APPDATA%\GGPort) and
`srand(seed)` (default 0x29CF). Scenarios:

| Scenario | Set-up |
|---|---|
| fresh game | the constructor: `initialState` + the GgGame start-up; call a state function directly |
| a PLAYERS.DAT record | `f.loadPlayers(path)` (a `savestates/**/*.players` copy or any PLAYERS.DAT), then `f.restorePlayer(slot)` or `f.restorePlayerByName(name)` (Players_LoadRecord and g_rosterIndex) |
| a DOSBox-X state | `f.loadState(path)` (GgStateFile::apply plus the state's `.players` copy); then the module's `*RestoreFromState` and its function with `StateEntry::Resume` |

Running: `f.keys("10:down:right, +40:up:right")` and `f.mouse("5:click:120:80")` add scripted input
in PIT ticks after now (dos::ScriptedInput's syntax); `f.start(fn)` runs `fn(g)` on a worker thread
that hands over at every frame boundary (Timer_Wait(0); with `f.polls(true)` every input poll and
busy-wait turn, for the unpaced loops); `f.step(n)` / `f.runUntil(pred, max)` return one
`GgTraceRecord` per boundary (frame, yield point, interrupts, g_ticks, g_state, the displayed and the
player's room, the player's x / y / sequence, rand() calls and state, the last input code, pictures
shown, the sound calls since the previous record); `f.finish()` stops the function at its next yield.
`f.frame()` is the last picture (`f.present()` presents VRAM now, e.g. after `loadState`);
`f.differingPixels(capture.png, masks)` compares it with a capture (640 x 400 shots as 2 x 2 blocks).
A host pointer move is dropped while `g_mouseHideState` is non-zero, so a test that moves the pointer
before the game function runs flushes the display first (the UI tests' `pointerAt`).
A state and the screenshot of the same name are not always the same instant (r2/aircraft/06_room1:
123 pixels in the moving part of the room). Tests keep only counts, ticks, coordinates and CRCs.

Each area adds its tests to `cmake/gg_tests_<area>.cmake` and uses the fixture; the skeleton's own
tests (`test_gg_game_skeleton.cpp`) check that every module links, Seq_Run's table, the start-up state
against the exe, the codec on the exe's image and on all 143 states, the state reader (VRAM, DAC,
apply), the quirk helpers and the fixture's stepping and script timing.

### 10.8 Status of the map

Counts of the rows below (generated by `python -P tools/gg/gen_engine_map.py OUT.md` from the address
comments of `src/gg` against `re/names/gg/*.tsv`): **done** = ported; **partial** = ported with a
detail left out (the row says which); **stub** = still a `// TODO(wave3)` body; **absorbed** = no
engine function of its own (the note says where its work is); **not ported** = no engine code names
it. After the skeleton there were 374 stubs (flow 53 and the 18 facade methods, world 104, puzzles
175, ui 23, engine 1); after the wave 3 integration:

| Table | Functions | Done | Partial | Stub | Absorbed | Not ported |
|---|---|---|---|---|---|---|
| `game_flow.tsv` | 94 | 87 | 1 | 0 | 6 | 0 |
| `game_world.tsv` | 143 | 139 | 1 | 0 | 3 | 0 |
| `game_puzzles.tsv` | 253 | 245 | 0 | 0 | 8 | 0 |
| `ui_tlc.tsv` | 100 | 84 | 0 | 0 | 16 | 0 |
| `engine_lowlevel.tsv` | 160 | 113 | 0 | 0 | 47 | 0 |
| `engine_sound.tsv` | 90 | 72 | 0 | 0 | 18 | 0 |
| all | 840 | 740 | 2 | 0 | 98 | 0 |

### 10.9 Function map

Original function (address and canonical name) -> the engine file and function whose comment
carries the address (a wave 2 half, when there is one, after "wave 2:") -> the area that owns it (or
"wave 2" for finished engine code no area changed) -> status. The table is generated from the
`SSSS:OOOO Name` comments in `src/gg` (the first declaration or definition after the comment is the
engine function), so every new function keeps the comment form `// 1622:0f37 Room_OfActor ...`.

#### Game flow, start-up, states 1..17 of 12e0, the workshop, results (game_flow.tsv)

| Original | Engine | Owner | Status |
|---|---|---|---|
| `12e0:000f` Seq_Run | `app/seq_run.h` `seqRun` | flow | done |
| `12e0:007f` Music_RoomCue | `audio/music_cues.cpp` `MusicCues::roomCue` | wave 2 | done |
| `12e0:00bd` Music_WorkshopCue | `audio/music_cues.cpp` `MusicCues::workshopCue` | wave 2 | done |
| `12e0:00ea` Music_MovieCue | `audio/music_cues.cpp` `MusicCues::movieCue` | wave 2 | done |
| `12e0:012f` State_16_SaveAndLeave | `game/flow.cpp` `state16SaveAndLeave` | flow | done |
| `12e0:0141` State_17_End | `game/flow.cpp` `state17End` | flow | done |
| `12e0:0148` State_03_Hub | `game/flow.cpp` `state03Hub` | flow | done |
| `12e0:0506` State_10_ListVehicleParts | `game/race.cpp` `state10ListVehicleParts` | vehicles | done |
| `12e0:0564` State_11_Race | `game/race.cpp` `state11Race` | vehicles | done |
| `12e0:05d5` Building_Free | `game/flow.cpp` `buildingFree` | flow | done |
| `12e0:0619` State_07_Warehouse | `game/world.cpp` `state07Warehouse`; wave 2: `app/preview.cpp` `Preview::warehouse` | world | done |
| `12e0:0820` State_04_BuildingSetup | `game/world.cpp` `state04BuildingSetup` | world | done |
| `12e0:096a` State_05_EnterRoom | `game/world.cpp` `state05EnterRoom`; wave 2: `game/building.cpp` `Building::enterRoom` | world | done |
| `12e0:09aa` State_08_LeaveRoom | `game/world.cpp` `state08LeaveRoom` | world | done |
| `12e0:0a24` Room_OpenPuzzleDoor | `game/puzzle_run.cpp` `roomOpenPuzzleDoor` | puzzles_a | done |
| `12e0:0ace` Score_Add | `game/world.cpp` `scoreAdd` | world | done |
| `12e0:0aea` Projectiles_Add | `game/world.cpp` `projectilesAdd` | world | done |
| `12e0:0afb` Game_Init | `game/flow.cpp` `gameInit` | flow | done |
| `12e0:0b19` Game_Shutdown | `game/flow.cpp` `gameShutdown`; wave 2: `app/preview.cpp` `shutdown` | flow | done |
| `12e0:0b38` main | `game/flow.cpp` `gameMain` | flow | done |
| `1a04:000e` Hud_SetDigitColors | `game/hud.cpp` `Hud::setDigitColors` | world | done |
| `1a04:0058` Hud_DrawNumber | `game/hud.cpp` `Hud::drawNumber` | world | done |
| `1a04:00eb` Hud_DrawBar | `game/hud.cpp` `Hud::drawBar` | world | done |
| `1a04:011c` Hud_ClearInfo | `game/hud.cpp` `Hud::clearInfo` | world | done |
| `1a04:0166` Hud_FillRect | `game/hud_run.cpp` `hudFillRect` | world | done |
| `1a04:018e` Hud_DrawLabels | `game/hud.cpp` `Hud::drawLabels` | world | done |
| `1a04:01fa` Hud_StripNeighbour | `game/hud_run.cpp` `hudStripNeighbour` | world | done |
| `1a04:025b` Hud_Init | `game/hud.cpp` `Hud::init` | world | done |
| `1a04:02c2` Hud_DrawAll | `game/hud_run.cpp` `hudDrawAll`; wave 2: `game/hud.cpp` `Hud::drawAll` | world | done |
| `1a04:02e9` Hud_DrawCounters | `game/hud.cpp` `Hud::drawCounters` | world | done |
| `1a04:0348` Hud_LayoutStrip | `game/hud_run.cpp` `hudLayoutStrip` | world | done |
| `1a04:0588` Hud_DrawStrip | `game/hud_run.cpp` `hudDrawStrip`; wave 2: `game/hud.cpp` `Hud::drawStrip` | world | done |
| `1a04:0690` Hud_FlashPart | `game/hud_run.cpp` `hudFlashPart` | world | done |
| `1a04:0754` Hud_SetInfoText | `game/hud_run.cpp` `hudSetInfoText` | world | done |
| `1a04:076b` Hud_AddPart | `game/hud_run.cpp` `hudAddPart` | world | done |
| `1a04:0809` Hud_StripAnchorFor | `game/hud_run.cpp` `hudStripAnchorFor` | world | done |
| `1a04:0823` Hud_RefreshStrip | `game/hud_run.cpp` `hudRefreshStrip` | world | done |
| `1a04:084c` Hud_ScrollRightArrow | `game/hud_run.cpp` `hudScrollRightArrow` | world | done |
| `1a04:08ad` Hud_ScrollLeftArrow | `game/hud_run.cpp` `hudScrollLeftArrow` | world | done |
| `1a04:090e` Hud_SelectPart | `game/hud_run.cpp` `hudSelectPart` | world | done |
| `1a04:0a11` Hud_DrawInfoText | `game/hud.cpp` `Hud::drawInfoText` | world | done |
| `1a04:0a58` Hud_CancelCursorPart | `game/hud_run.cpp` `hudCancelCursorPart` | world | done |
| `1a04:0a82` Hud_TrashCursorPart | `game/hud_run.cpp` `hudTrashCursorPart` | world | done |
| `1a04:0b5b` Hud_PutBackCursorPart | `game/hud_run.cpp` `hudPutBackCursorPart` | world | done |
| `1a04:0b69` Hud_Update | `game/hud_run.cpp` `hudUpdate`; wave 2: `game/hud.cpp` `Hud::update` | world | done |
| `1a04:0d36` Hud_AddScoreNow | `game/hud_run.cpp` `hudAddScoreNow` | world | done |
| `1a04:0d65` Hud_EscHook | `game/hud_run.cpp` `hudEscHook` | world | done |
| `2b5a:0000` Player_InitRecord | `ui/players.cpp` `playerInitRecord` | ui | done |
| `2b5a:004c` Players_Open | `save/players_store.cpp` PlayersStore (file I/O, the write error) | — | absorbed |
| `2b5a:00db` Players_Read | `save/players_store.cpp` PlayersStore (file I/O, the write error) | — | absorbed |
| `2b5a:010a` Players_WriteError | `ui/players.cpp` `playersWriteError` | ui | done |
| `2b5a:0156` Players_Write | `save/players_store.cpp` PlayersStore (file I/O, the write error) | — | absorbed |
| `2b5a:0189` Players_Close | `save/players_store.cpp` PlayersStore (file I/O, the write error) | — | absorbed |
| `2b5a:0198` Players_LoadRecord | `ui/players.cpp` `playersLoadRecord`; wave 2: `save/players_store.h` `loadRecord` | ui | done |
| `2b5a:01ea` Players_SaveCurrent | `ui/players.cpp` `playersSaveCurrent`; wave 2: `save/players_store.h` `saveCurrent` | ui | done |
| `2bd4:0225` Rank_FromPoints | `ui/rank.cpp` `RankTable::rankOf` | ui | done |
| `2c11:0000` Attract_PlayAnim | `game/flow_attract.cpp` `attractPlayAnim`; wave 2: `app/preview.cpp` `Preview::playAnim` | flow | done |
| `2c11:03a7` Attract_DrawPictureSet | `ui/attract.cpp` `attractDrawPictureSet` | flow | done |
| `2c11:043f` Attract_TitleAnim | `game/flow_attract.cpp` `attractTitleAnim`; wave 2: `app/preview.cpp` `Preview::titleAnim` | flow | done |
| `2c11:05cf` Attract_AfterTitle | `game/flow_attract.cpp` `attractAfterTitle`; wave 2: `app/preview.cpp` `Preview::afterTitle` | flow | done |
| `2c11:0630` Attract_StoryPage | `game/flow_attract.cpp` `attractStoryPage`; wave 2: `app/preview.cpp` `Preview::storyPage` | flow | done |
| `2c11:077f` Attract_AfterStory | `game/flow_attract.cpp` `attractAfterStory`; wave 2: `app/preview.cpp` `Preview::afterStory` | flow | done |
| `2c11:07e5` Attract_BeforePrompt | `game/flow_attract.cpp` `attractBeforePrompt`; wave 2: `app/preview.cpp` `Preview::beforePrompt` | flow | done |
| `2c11:084c` Attract_SignInPrompt | `game/flow_attract.cpp` `attractSignInPrompt`; wave 2: `app/preview.cpp` `Preview::signInPrompt` | flow | done |
| `2c11:0913` State_01_Attract | `game/flow_attract.cpp` `state01Attract`; wave 2: `app/preview.cpp` `Preview::attract` | flow | done |
| `2caa:0000` SignIn_HideButtons | `ui/signin.cpp` `SignIn::hideButtons` | ui | done |
| `2caa:0025` SignIn_ShowButtons | `ui/signin.cpp` `SignIn::showButtons` | ui | done |
| `2caa:004f` Roster_LoadOrCreate | `ui/signin_run.cpp` `rosterLoadOrCreate`; wave 2: `save/players_store.cpp` `PlayersDat::create` | ui | partial: the creation while g_rosterIndex is not 0 (the file deleted during a session) is logged, not modelled |
| `2caa:0145` Roster_Save | `ui/signin_run.cpp` `rosterSave` | ui | done |
| `2caa:016e` SignIn_DrawScreen | `ui/signin.cpp` `signInDrawScreen` | ui | done |
| `2caa:01b3` SignIn_ShowScreen | `ui/signin.h` `signInDrawScreen` | ui | done |
| `2caa:01c5` Roster_UpdateTyped | `ui/signin.cpp` `SignIn::updateTyped` | ui | done |
| `2caa:04ae` SignIn_DrawScoreRank | `ui/signin.cpp` `SignIn::drawScoreRank` | ui | done |
| `2caa:053a` SignIn_DrawList | `ui/signin.cpp` `SignIn::drawList` | ui | done |
| `2caa:082e` SignIn_HandleCode | `ui/signin.cpp` `SignIn::handleCode` | ui | done |
| `2caa:0eb3` SignIn_Run | `ui/signin_run.cpp` `signInRun`; wave 2: `app/preview.cpp` `Preview::signIn` | ui | done |
| `2caa:1240` State_02_SignIn | `ui/signin_run.cpp` `state02SignIn`; wave 2: `app/preview.cpp` `Preview::signIn` | ui | done |
| `2caa:1382` SignIn_LoadButtons | `ui/signin.h` `SignIn` | ui | done |
| `2caa:13f6` SignIn_Free | `ui/signin.h` SignIn’s destructor (ui) | — | absorbed |
| `2fcb:0000` Result_DrawScreen | `game/results.cpp` `resultDrawScreen` | vehicles | done |
| `2fcb:01dd` Result_RankUpMessage | `game/results.cpp` `resultRankUpMessage` | vehicles | done |
| `2fcb:0315` State_14_RedrawResult | `game/results.cpp` `state14RedrawResult` | vehicles | done |
| `2fcb:0325` State_12_RaceResult | `game/results.cpp` `state12RaceResult` | vehicles | done |
| `2fcb:0573` State_13_PostRaceMenu | `game/results.cpp` `state13PostRaceMenu` | vehicles | done |
| `2fcb:0640` State_15_FinalMovie | `game/results.cpp` `state15FinalMovie` | vehicles | done |
| `303a:0000` Init_FatalMessage | `app/gg_app.cpp` start-up errors (flow) | — | absorbed |
| `303a:005c` Init_CommandLine | `game/flow.cpp` `audio` | flow | done |
| `303a:0250` Init_Shutdown | `app/gg_runtime.h` `removeHandlers` | wave 2 | done |
| `3060:0000` Hint_CompareDue | `ui/hints.cpp` `begin` | ui | done |
| `3060:0013` Hint_Init | `ui/hints_run.cpp` `hintInit` | ui | done |
| `3060:0037` Hint_Queue | `ui/hints_run.cpp` `hintQueue` | ui | done |
| `3060:00bc` Hint_Update | `ui/hints_run.cpp` `hintUpdate`; wave 2: `ui/hints.cpp` `HintQueue::update` | ui | done |
| `3060:013b` Hint_Show | `ui/hints_run.cpp` `hintShow` | ui | done |
| `3060:01fb` Hint_ClearQueue | `ui/hints_run.cpp` `hintClearQueue` | ui | done |

#### World: rooms, parts, actors, player, chimps, collision, HUD (game_world.tsv)

| Original | Engine | Owner | Status |
|---|---|---|---|
| `153f:0002` Parts_LoadCatalogue | `game/world_parts.cpp` `partsLoadCatalogue` | world | done |
| `153f:00fd` Parts_FreeCatalogue | `game/world_parts.cpp` `partsFreeCatalogue` | world | done |
| `153f:015d` Part_GetType | `game/world_parts.cpp` `partGetType` | world | done |
| `153f:0173` Part_GetShelfPicId | `game/world_parts.cpp` `partGetShelfPicId` | world | done |
| `153f:0189` Part_IsNeeded | `game/world_parts.cpp` `partIsNeeded` | world | done |
| `153f:019f` Part_Record | `game/world_parts.cpp` `partRecord` | world | done |
| `153f:01b6` Parts_ShuffleDeck | `game/parts.cpp` `PartDeck::shuffle` | world | done |
| `153f:0203` Parts_DealNext | `game/parts.cpp` `PartDeck::dealNext` | world | done |
| `153f:0216` PartList_CountType | `game/world_parts.cpp` `partListCountType` | world | done |
| `153f:0276` PartList_FindType | `game/world_parts.cpp` `partListFindType` | world | done |
| `153f:02bb` PartList_CountHeldBy | `game/world_parts.cpp` `partListCountHeldBy` | world | done |
| `153f:0316` Parts_NeededCount | `game/world_parts.cpp` `partsNeededCount`; wave 2: `game/parts.h` `neededCount` | world | done |
| `153f:0323` PartList_NextColumn | `game/world_parts.cpp` `partListNextColumn` | world | done |
| `153f:0354` PartList_PrevColumn | `game/world_parts.cpp` `partListPrevColumn` | world | done |
| `153f:0385` Parts_SaveState | `game/world_parts.cpp` `partsSaveState` | world | done |
| `153f:03c9` Parts_RestoreState | `game/world_parts.cpp` `partsRestoreState` | world | done |
| `1622:000a` Room_HallFloorFromY | `game/rooms.cpp` `floorFromY` | world | done |
| `1622:004d` Room_SmallFloorFromY | `game/rooms.cpp` `floorFromY` | world | done |
| `1622:0071` ItemPool_Reset | `game/rooms.cpp` `Rooms::resetItemPool` | world | done |
| `1622:0085` Rooms_Load | `game/world_rooms.cpp` `roomsLoad`; wave 2: `game/rooms.cpp` `Rooms::load` | world | done |
| `1622:02dd` Rooms_Free | `game/world_rooms.cpp` `roomsFree` | world | done |
| `1622:0346` RoomMusic_Start | `game/sound_audio.cpp` `GgAudioSound::roomMusicStart`; wave 2: `app/preview.cpp` `roomMusic` | flow | done |
| `1622:03b6` RoomMusic_SelectLayers | `game/sound_audio.cpp` `GgAudioSound::roomMusicSelectLayers`; wave 2: `app/preview.cpp` `roomMusic` | flow | done |
| `1622:0411` Room_Enter | `game/world_rooms.cpp` `roomEnter` | world | done |
| `1622:05f2` Room_Leave | `game/world_rooms.cpp` `roomLeave` | world | done |
| `1622:06cf` Room_QueueEntryLayers | `game/world_rooms.cpp` `roomQueueEntryLayers`; wave 2: `game/rooms.cpp` `Rooms::queueEntryLayers` | world | done |
| `1622:0706` Room_DrawAnimLayer | `game/world_rooms.cpp` `roomDrawAnimLayer`; wave 2: `game/rooms.cpp` `Rooms::drawAnimLayer` | world | done |
| `1622:087f` Room_FindLinkedDoor | `game/world_rooms.cpp` `roomFindLinkedDoor` | world | done |
| `1622:08fb` Room_GetFlags | `game/world_rooms.cpp` `roomGetFlags` | world | done |
| `1622:090d` Room_IsHall | `game/world_rooms.cpp` `roomIsHall`; wave 2: `game/rooms.h` `isHall` | world | done |
| `1622:0922` Room_DrawLayer | `game/world_rooms.cpp` `roomDrawLayer`; wave 2: `game/rooms.cpp` `Rooms::drawLayer` | world | done |
| `1622:09e5` Obj_HideAt | `game/world_rooms.cpp` `objHideAt` | world | done |
| `1622:0a17` Obj_ShowAt | `game/world_rooms.cpp` `objShowAt` | world | done |
| `1622:0a42` Obj_Show | `game/world_rooms.cpp` `objShow` | world | done |
| `1622:0a4e` Obj_Hide | `game/world_rooms.cpp` `objHide` | world | done |
| `1622:0a61` ItemPool_Alloc | `game/rooms.cpp` `Rooms::allocItem` | world | done |
| `1622:0a76` Room_PlaceItems | `game/rooms.cpp` `Rooms::placeItems` | world | done |
| `1622:0d8f` Room_RemoveItem | `game/world_rooms.cpp` `roomRemoveItem` | world | done |
| `1622:0df0` Room_DropItemAt | `game/world_rooms.cpp` `roomDropItemAt`; wave 2: `game/rooms.cpp` `Rooms::dropItemAt` | world | done |
| `1622:0f37` Room_OfActor | `game/world_rooms.cpp` `roomOfActor` | world | done |
| `1622:0f4e` Item_PlaceInRandomRoom | `game/world_rooms.cpp` `itemPlaceInRandomRoom` | world | done |
| `1622:0fb3` Rooms_SaveState | `game/world_rooms.cpp` `roomsSaveState`; wave 2: `game/rooms.cpp` `Rooms::saveState` | world | done |
| `1622:10e7` Rooms_RestoreState | `game/world_rooms.cpp` `roomsRestoreState`; wave 2: `game/rooms.cpp` `Rooms::restoreState` | world | done |
| `1622:11f7` Room_GetObject | `game/world_rooms.cpp` `roomGetObject` | world | done |
| `1622:1234` Room_GetAnim | `game/world_rooms.cpp` `roomGetAnim` | world | done |
| `1622:1265` Room_GetItem | `game/world_rooms.cpp` `roomGetItem` | world | done |
| `174b:0009` ScreenAnim_Init | `game/game.h` `screenAnims` | skeleton | done |
| `174b:001a` ScreenAnim_Step | `game/screen_anim.cpp` `ScreenAnim::step` | world | done |
| `174b:0285` ScreenAnim_Start | `game/screen_anim.cpp` `ScreenAnims::start` | world | done |
| `174b:02ab` ScreenAnim_StartAt | `game/screen_anim.cpp` `ScreenAnims::startAt` | world | done |
| `174b:02d2` ScreenAnim_Stop | `game/screen_anim.cpp` `ScreenAnims::stop` | world | done |
| `174b:02f6` ScreenAnim_StopAll | `game/screen_anim.cpp` `ScreenAnims::stopAll` | world | done |
| `174b:030b` ScreenAnim_SetSequence | `game/screen_anim.cpp` `ScreenAnim::setSequence` | world | done |
| `174b:0324` ScreenAnim_Load | `game/screen_anim.cpp` `ScreenAnim::load` | world | done |
| `174b:0444` ScreenAnim_Free | `game/vehicles_common.h` `freeAnim` | vehicles | done |
| `174b:047e` ScreenAnim_UpdateAll | `game/screen_anim.cpp` `ScreenAnims::update` | world | done |
| `174b:0510` ScreenAnim_Frame | `game/vehicles_common.h` `animFrame` | vehicles | done |
| `174b:0522` ScreenAnim_PlayModal | `game/world_rooms.cpp` `screenAnimPlayModal` | world | done |
| `1834:000b` AnimSeq_CountSteps | no caller | — | absorbed |
| `1834:003b` Actor_SetSequence | `game/actors_run.cpp` `actorSetSequence` | actors | done |
| `1834:0088` Actors_LoadSet | `game/actors.cpp` `ActorSet::parse` | actors | done |
| `1834:015b` Actor_SelectScale | `game/actors_run.cpp` `actorSelectScale` | actors | done |
| `1834:01b6` Actor_RunStep | `game/actors_run.cpp` `actorRunStep` | actors | done |
| `1834:0733` Actors_Draw | `game/actors_run.cpp` `actorsDraw` | actors | done |
| `1834:07ad` Actors_EnterRoom | `game/actors_run.cpp` `actorsEnterRoom`; wave 2: `game/building.cpp` `Building::enterRoom` | actors | done |
| `1834:0870` Actors_LeaveRoom | `game/actors_run.cpp` `actorsLeaveRoom` | actors | done |
| `1834:08d7` Actor_AddPart | `game/actors_run.cpp` `actorAddPart` | actors | partial: its two stores into the part record nothing reads (+0x8A = 0, +0x1A = 0x2238) are not modelled |
| `1834:0990` Actor_RemovePart | `game/actors_run.cpp` `actorRemovePart` | actors | done |
| `1834:0ace` Actors_ResetCountdowns | `game/actors_run.cpp` `actorsResetCountdowns` | actors | done |
| `1834:0ae9` Actors_Tick | `game/actors_run.cpp` `actorsTick` | actors | done |
| `1834:0b5e` Actors_SaveState | `game/actors_run.cpp` `actorsSaveState` | actors | done |
| `1834:0b95` Actors_RestoreState | `game/actors_run.cpp` `actorsRestoreState` | actors | done |
| `18f5:0002` Player_PickUpItem | `game/player.cpp` `playerPickUpItem` | actors | done |
| `18f5:0180` Player_HitByChimp | `game/player.cpp` `playerHitByChimp` | actors | done |
| `18f5:029b` Player_Brain | `game/player.cpp` `playerBrain` | actors | done |
| `18f5:0401` Projectile_Brain | `game/player.cpp` `projectileBrain` | actors | done |
| `18f5:0409` Player_EnterRoom | `game/player.cpp` `playerEnterRoom` | actors | done |
| `18f5:04cf` Player_SequenceEnd | `game/player.cpp` `playerSequenceEnd` | actors | done |
| `18f5:05a3` Actors_LoadPlayerSets | `game/player.cpp` `actorsLoadPlayerSets` | actors | done |
| `18f5:06b7` Actors_FreePlayerSets | `game/player.cpp` `actorsFreePlayerSets` | actors | done |
| `18f5:06fb` Projectile_EnterRoom | `game/player.cpp` `projectileEnterRoom` | actors | done |
| `18f5:0721` Actors_Init | `game/player.cpp` `actorsInit` | actors | done |
| `18f5:0895` Projectile_Launch | `game/player.cpp` `projectileLaunch` | actors | done |
| `18f5:094d` Building_NewVisit | `game/world.cpp` `buildingNewVisit` | world | done |
| `1adb:000c` Rect_Overlap | `game/rooms.cpp` `rectOverlap` | world | done |
| `1adb:0093` Actor_CollideWalls | `game/collision.cpp` `actorCollideWalls` | actors | done |
| `1adb:01ef` Chimp_ProbeContacts | `game/collision.cpp` `chimpProbeContacts` | actors | done |
| `1adb:03b0` Actor_UpdateContacts | `game/collision.cpp` `actorUpdateContacts` | actors | done |
| `2a80:0000` Chimp_TakePart | `game/chimps.cpp` `chimpTakePart` | actors | done |
| `2a80:00a1` Chimp_DropPart | `game/chimps.cpp` `chimpDropPart` | actors | done |
| `2a80:0112` Chimp_Brain | `game/chimps.cpp` `chimpBrain` | actors | done |
| `2a80:032f` Chimp_EnterRoom | `game/chimps.cpp` `chimpEnterRoom` | actors | done |
| `2a80:0431` Chimp_SequenceEnd | `game/chimps.cpp` `chimpSequenceEnd` | actors | done |
| `2a80:051e` Chimps_LoadSets | `game/chimps.cpp` `chimpsLoadSets` | actors | done |
| `2a80:05e6` Chimps_FreeSets | `game/chimps.cpp` `chimpsFreeSets` | actors | done |
| `2a80:0635` Chimps_SetupActors | `game/chimps.cpp` `chimpsSetupActors` | actors | done |
| `2a80:076b` Chimps_Free | `game/chimps.cpp` `chimpsFree` | actors | done |
| `2a80:0784` Chimps_Spawn | `game/chimps.cpp` `chimpsSpawn` | actors | done |
| `2bd4:0101` PartList_FindHeld | `game/world_parts.cpp` `partListFindHeld` | world | done |
| `2bd4:017e` ScreenAnim_BindPictures | pictures are bound when a set is loaded (`game/screen_anim.h`, `game/actors.h`) | — | absorbed |
| `2bd4:01c1` AnimSeq_BindPictures | pictures are bound when a set is loaded (`game/screen_anim.h`, `game/actors.h`) | — | absorbed |
| `2dee:0000` Blueprint_ClearBox | `game/blueprint.cpp` `setDrawColor` | vehicles | done |
| `2dee:005e` Blueprint_DrawFrame | `game/blueprint.h` `open` | vehicles | done |
| `2dee:00a6` Workshop_UpdateRaceDoor | `game/workshop.cpp` `workshopUpdateRaceDoor` | vehicles | done |
| `2dee:01a0` Workshop_PaintSprite | `game/workshop.cpp` `workshopPaintSprite` | vehicles | done |
| `2dee:028f` Workshop_LayoutShelf | `game/workshop.cpp` `workshopLayoutShelf` | vehicles | done |
| `2dee:03b2` Workshop_ReleaseShelfPics | `game/workshop.cpp` `workshopReleaseShelfPics` | vehicles | done |
| `2dee:0401` Workshop_CompareLayer | `game/workshop.cpp` `workshopCompareLayer` | vehicles | done |
| `2dee:0414` Workshop_EnableVehicleZones | `game/workshop.cpp` `workshopEnableVehicleZones` | vehicles | done |
| `2dee:046d` Workshop_InstallPart | `game/workshop.cpp` `workshopInstallPart` | vehicles | done |
| `2dee:060f` Workshop_UninstallPart | `game/workshop.cpp` `workshopUninstallPart` | vehicles | done |
| `2dee:070a` Workshop_DrawVehicle | `game/workshop.cpp` `workshopDrawVehicle` | vehicles | done |
| `2dee:0797` Workshop_ReleaseVehiclePics | `game/workshop.cpp` `workshopReleaseVehiclePics` | vehicles | done |
| `2dee:0804` Workshop_DrawShelf | `game/workshop.cpp` `workshopDrawShelf` | vehicles | done |
| `2dee:0880` Workshop_ScrollStepLeft | `game/workshop.cpp` `workshopScrollStepLeft` | vehicles | done |
| `2dee:0986` Workshop_ScrollStepRight | `game/workshop.cpp` `workshopScrollStepRight` | vehicles | done |
| `2dee:0a8c` Workshop_RestoreShelfBack | `game/workshop.cpp` `workshopRestoreShelfBack` | vehicles | done |
| `2dee:0ab3` Workshop_SetShelfZones | `game/workshop.cpp` `workshopSetShelfZones` | vehicles | done |
| `2dee:0b3d` Workshop_CarryPart | `game/workshop.cpp` `workshopCarryPart` | vehicles | done |
| `2dee:0bb2` Workshop_ListShelfParts | `game/workshop.cpp` `workshopListShelfParts` | vehicles | done |
| `2dee:0c25` Part_GetPartner | `game/workshop.cpp` `partGetPartner` | vehicles | done |
| `2dee:0c4d` Workshop_DropCarried | `game/workshop.cpp` `workshopDropCarried` | vehicles | done |
| `2dee:0c89` Workshop_Run | `game/workshop.cpp` `workshopRun` | vehicles | done |
| `2dee:13eb` Blueprint_Load | `game/blueprint.cpp` `BlueprintView::load` | vehicles | done |
| `2dee:174f` Blueprint_Free | `game/workshop.cpp` `reset` | vehicles | done |
| `2dee:17eb` Blueprint_Open | `game/workshop.cpp` `blueprintOpen` | vehicles | done |
| `2dee:185d` Blueprint_Close | `game/workshop.cpp` `blueprintClose` | vehicles | done |
| `2dee:1894` Blueprint_Run | `game/workshop.cpp` `blueprintRun` | vehicles | done |
| `2dee:1a90` State_06_Workshop | `game/workshop.cpp` `state06Workshop` | vehicles | done |
| `2dee:1db5` Workshop_Init | `game/workshop.cpp` `workshopInit` | vehicles | done |
| `3dd6:0000` Race_MapPathFrames | `game/race.cpp` `raceMapPathFrames` | vehicles | done |
| `3dd6:0034` Race_StepSpeed | `game/race.cpp` `raceStepSpeed` | vehicles | done |
| `3dd6:00e0` Race_AdvancePoint | `game/race.cpp` `raceAdvancePoint` | vehicles | done |
| `3dd6:01ca` Race_Run | `game/race.cpp` `raceRun` | vehicles | done |
| `3dd6:044e` Race_ComputePerformance | `game/race.cpp` `raceComputePerformance` | vehicles | done |
| `3dd6:0762` Race_LandEnergy | `game/race.cpp` `raceLandEnergy` | vehicles | done |
| `3e77:0000` AirRace_MapScenery | `game/race.cpp` `airRaceMapScenery` | vehicles | done |
| `3e77:0036` AirRace_ComputePerformance | `game/race.cpp` `airRaceComputePerformance` | vehicles | done |
| `3e77:03c7` AirRace_DrawScenery | `game/race.cpp` `airRaceDrawScenery` | vehicles | done |
| `3e77:0428` AirRace_StepSpeed | `game/race.cpp` `airRaceStepSpeed` | vehicles | done |
| `3e77:04cc` AirRace_AdvancePoint | `game/race.cpp` `airRaceAdvancePoint` | vehicles | done |
| `3e77:05e3` AirRace_Run | `game/race.cpp` `airRaceRun` | vehicles | done |
| `3e77:0a23` Race_Aircraft | `game/race.cpp` `raceAircraft` | vehicles | done |

#### Puzzles (game_puzzles.tsv)

| Original | Engine | Owner | Status |
|---|---|---|---|
| `3083:0000` PzElec_StackHas | `game/pz_electric.cpp` (the path search of PzElectric::isSolved) | — | absorbed |
| `3083:002b` PzElec_SkipExit | `game/pz_electric.cpp` (the path search of PzElectric::isSolved) | — | absorbed |
| `3083:007c` PzElec_PathKnown | `game/pz_electric.cpp` (the path search of PzElectric::isSolved) | — | absorbed |
| `3083:00d5` PzElec_PathAdd | `game/pz_electric.cpp` (the path search of PzElectric::isSolved) | — | absorbed |
| `3083:0129` PzElec_BatteryShorted | `game/pz_electric.cpp` `PzElectric::batteryShorted` | puzzles_a | done |
| `3083:0492` PzElec_CheckBatteries | `game/pz_electric.cpp` `PzElectric::checkBatteries` | puzzles_a | done |
| `3083:04c7` PzElec_BulbLit | `game/pz_electric.cpp` `PzElectric::bulbLit` | puzzles_a | done |
| `3083:0844` PzElec_SetBulbPicture | `game/pz_electric_run.cpp` `PzElectric::setBulbPicture` | puzzles_a | done |
| `3083:08a1` PzElectric_Run | `game/pz_electric_run.cpp` `PzElectric::run` | puzzles_a | done |
| `3083:0b15` PzElec_LayWire | `game/pz_electric_run.cpp` `PzElectric::layWire` | puzzles_a | done |
| `3083:0d1a` PzElec_SetMouseRange | `game/pz_electric_run.cpp` `PzElectric::setMouseRange` | puzzles_a | done |
| `3083:0d97` PzElec_CutWire | `game/pz_electric_run.cpp` `PzElectric::cutWire` | puzzles_a | done |
| `3083:0f18` PzElec_OnWire | `game/pz_electric_run.cpp` `PzElectric::onWire` | puzzles_a | done |
| `3083:0fcf` PzElec_DrawWires | `game/pz_electric.cpp` `PzElectric::drawWires` | puzzles_a | done |
| `3083:10ac` PzElec_DrawWireL | `game/pz_electric.cpp` `PzElectric::drawWireL` | puzzles_a | done |
| `3083:11a8` PzElec_DrawRubberBand | `game/pz_electric_run.cpp` `PzElectric::drawRubberBand` | puzzles_a | done |
| `3083:12cc` PzElec_DrawTerminalDot | `game/pz_electric.cpp` `PzElectric::drawTerminalDot` | puzzles_a | done |
| `3083:1327` PzElec_ToggleSwitch | `game/pz_electric_run.cpp` `PzElectric::toggleSwitch` | puzzles_a | done |
| `3083:13c5` PzElec_PickUpPiece | `game/pz_electric_run.cpp` `PzElectric::pickUpPiece` | puzzles_a | done |
| `3083:156a` PzElec_ReturnToToolbox | `game/pz_electric_run.cpp` `PzElectric::returnToToolbox` | puzzles_a | done |
| `3083:15ad` PzElec_TakeFromToolbox | `game/pz_electric_run.cpp` `PzElectric::takeFromToolbox` | puzzles_a | done |
| `3083:163c` PzElec_DrawToolbox | `game/pz_electric.cpp` `PzElectric::drawToolbox` | puzzles_a | done |
| `3083:172a` PzElec_PlacePiece | `game/pz_electric_run.cpp` `PzElectric::placePiece` | puzzles_a | done |
| `3083:1850` PzElec_AutoConnect | `game/pz_electric.cpp` `PzElectric::autoConnect` | puzzles_a | done |
| `3083:19a9` PzElec_StripsOverlap | `game/puzzles.h` `spansOverlap` | puzzles_a | done |
| `3083:1a94` PzElec_UpdatePlugCursor | `game/pz_electric_run.cpp` `PzElectric::updatePlugCursor` | puzzles_a | done |
| `3083:1b75` PzElec_FindFreeTerminal | `game/pz_electric_run.cpp` `PzElectric::findFreeTerminal` | puzzles_a | done |
| `3083:1c63` PzElec_PointInRect | `game/pz_electric_run.cpp` `PzElectric::pointInRect` | puzzles_a | done |
| `3083:1c99` PzElec_DrawBoard | `game/pz_electric.cpp` `PzElectric::drawBoard` | puzzles_a | done |
| `3083:1d1d` PzElec_AddPieceZones | `game/pz_electric_run.cpp` `PzElectric::addPieceZones` | puzzles_a | done |
| `3083:1d43` PzElec_RemovePieceZones | `game/pz_electric_run.cpp` `PzElectric::removePieceZones` | puzzles_a | done |
| `3083:1d69` PzElec_AddToolZones | `game/pz_electric_run.cpp` `PzElectric::addToolZones` | puzzles_a | done |
| `3083:1d83` PzElec_AddToolCursorRegions | `game/pz_electric_run.cpp` `PzElectric::addToolCursorRegions` | puzzles_a | done |
| `3083:1dab` PzElec_InitToolZones | `game/pz_electric_run.cpp` `PzElectric::initToolZones` | puzzles_a | done |
| `3083:1e20` PzElec_CreatePiece | `game/pz_electric.cpp` `PzElectric::createPiece` | puzzles_a | done |
| `3083:1f22` PzElec_AddBoardZones | `game/pz_electric_run.cpp` `PzElectric::addBoardZones` | puzzles_a | done |
| `3083:1f35` PzElec_SetTerminalGeometry | `game/pz_electric.cpp` `PzElectric::setTerminalGeometry` | puzzles_a | done |
| `3083:216e` PzElec_AllBulbsLit | `game/pz_electric_run.cpp` `PzElectric::allBulbsLit` | puzzles_a | done |
| `3083:2196` PzElec_IsSolved | `game/pz_electric_run.cpp` `PzElectric::isSolved` | puzzles_a | done |
| `3083:22bf` PzElec_LoadVariant | `game/pz_electric.cpp` `PzElectric::loadVariant` | puzzles_a | done |
| `3083:24aa` PzElec_InitToolCursorRegions | `game/pz_electric_run.cpp` `size` | puzzles_a | done |
| `3083:250e` PzElec_DrawScreen | `game/pz_electric_run.cpp` `puzzleLoadScreen` | puzzles_a | done |
| `3083:256b` PzElec_LoadResources | `game/pz_electric.cpp` `PzElectric::loadResources` | puzzles_a | done |
| `3083:2695` PzElec_Cleanup | `game/pz_electric_run.cpp` `PzElectric::cleanup` | puzzles_a | done |
| `32fe:0000` PzBalance_LoadVariant | `game/pz_balance.cpp` `PzBalance::loadVariant` | puzzles_b | done |
| `32fe:0079` PzBalance_SelectHookZones | `game/pz_balance_run.cpp` `PzBalance::selectHookZones` | puzzles_b | done |
| `32fe:00d4` PzBalance_InitZones | `game/pz_balance_run.cpp` `PzBalance::initZones` | puzzles_b | done |
| `32fe:0235` PzBalance_DrawInventory | `game/pz_balance.cpp` `PzBalance::drawInventory` | puzzles_b | done |
| `32fe:02c8` PzBalance_DrawHook | `game/pz_balance.cpp` `PzBalance::drawHook` | puzzles_b | done |
| `32fe:0375` PzBalance_DrawBeam | `game/pz_balance.cpp` `PzBalance::drawBeam` | puzzles_b | done |
| `32fe:043b` PzBalance_LeftTorque | `game/pz_balance_run.cpp` `PzBalance::leftTorque` | puzzles_b | done |
| `32fe:0494` PzBalance_RightTorque | `game/pz_balance_run.cpp` `PzBalance::rightTorque` | puzzles_b | done |
| `32fe:04f0` PzBalance_Compare | `game/pz_balance_run.cpp` `PzBalance::compare` | puzzles_b | done |
| `32fe:0566` PzBalance_SwingLevel | `game/pz_balance_run.cpp` `PzBalance::swingLevel` | puzzles_b | done |
| `32fe:05eb` PzBalance_Swing | `game/pz_balance_run.cpp` `PzBalance::swing` | puzzles_b | done |
| `32fe:06b9` PzBalance_Settle | `game/pz_balance_run.cpp` `PzBalance::settle` | puzzles_b | done |
| `32fe:06de` PzBalance_HoldWeight | `game/pz_balance_run.cpp` `PzBalance::holdWeight` | puzzles_b | done |
| `32fe:0750` PzBalance_DropCursor | `game/pz_balance_run.cpp` `PzBalance::dropCursor` | puzzles_b | done |
| `32fe:0775` PzBalance_Hang | `game/pz_balance_run.cpp` `PzBalance::hang` | puzzles_b | done |
| `32fe:07c9` PzBalance_Unhang | `game/pz_balance_run.cpp` `PzBalance::unhang` | puzzles_b | done |
| `32fe:081a` PzBalance_Loop | `game/pz_balance_run.cpp` `PzBalance::loop` | puzzles_b | done |
| `32fe:0aa0` PzBalance_Run | `game/pz_balance_run.cpp` `PzBalance::run` | puzzles_b | done |
| `33cd:0000` PzEnergy_Run | `game/pz_energy_run.cpp` `PzEnergy::run` | puzzles_a | done |
| `33cd:022a` PzEnergy_SetMouseRange | `game/pz_energy_run.cpp` `PzEnergy::setMouseRange` | puzzles_a | done |
| `33cd:02a0` PzEnergy_SendEnergy | `game/pz_energy_run.cpp` `PzEnergy::sendEnergy` | puzzles_a | done |
| `33cd:03c8` PzEnergy_AnimateDevice | `game/pz_energy_run.cpp` `PzEnergy::animateDevice` | puzzles_a | done |
| `33cd:064d` PzEnergy_IdleAnimate | `game/pz_energy_run.cpp` `PzEnergy::idleAnimate` | puzzles_a | done |
| `33cd:0735` PzEnergy_BuildHintAndLabels | `game/pz_energy_run.cpp` `PzEnergy::buildHintAndLabels`; wave 2: `game/pz_energy.h` `buildHint` | puzzles_a | done |
| `33cd:0879` Str_ConcatList_ovl15 | std::string (the overlays’ private string copies) | — | absorbed |
| `33cd:08d1` Str_CopyEnd_ovl15 | std::string (the overlays’ private string copies) | — | absorbed |
| `33cd:08f5` PzEnergy_DrawLines | `game/pz_energy_run.cpp` `PzEnergy::drawLines` | puzzles_a | done |
| `33cd:099c` PzEnergy_DrawFromForm | `game/pz_energy_run.cpp` `PzEnergy::drawFromForm` | puzzles_a | done |
| `33cd:0ae7` PzEnergy_DrawFromDevice | `game/pz_energy_run.cpp` `PzEnergy::drawFromDevice` | puzzles_a | done |
| `33cd:0c3e` PzEnergy_IsSolved | `game/pz_energy_run.cpp` `PzEnergy::isSolved` | puzzles_a | done |
| `33cd:0c71` PzEnergy_SelectEraser | `game/pz_energy_run.cpp` `PzEnergy::selectEraser` | puzzles_a | done |
| `33cd:0c97` PzEnergy_EraseLines | `game/pz_energy_run.cpp` `PzEnergy::eraseLines` | puzzles_a | done |
| `33cd:0db4` PzEnergy_SelectPencil | `game/pz_energy_run.cpp` `PzEnergy::selectPencil` | puzzles_a | done |
| `33cd:0dde` PzEnergy_AddZones | `game/pz_energy_run.cpp` `PzEnergy::addZones` | puzzles_a | done |
| `33cd:0e0c` PzEnergy_DrawLine | `game/pz_energy_run.cpp` `PzEnergy::drawLine` | puzzles_a | done |
| `33cd:0ee7` PzEnergy_AnimatePulse | `game/pz_energy_run.cpp` `PzEnergy::animatePulse` | puzzles_a | done |
| `33cd:0ff7` PzEnergy_DrawPulse | `game/pz_energy_run.cpp` `PzEnergy::drawPulse` | puzzles_a | done |
| `33cd:10a9` PzEnergy_PointOnLine | `game/pz_energy_run.cpp` `PzEnergy::pointOnLine` | puzzles_a | done |
| `33cd:1135` PzEnergy_DrawPencilCount | `game/pz_energy.cpp` `PzEnergy::drawPencilCount` | puzzles_a | done |
| `33cd:1189` PzEnergy_AddConnectorZones | `game/pz_energy_run.cpp` `PzEnergy::addConnectorZones` | puzzles_a | done |
| `33cd:1274` PzEnergy_LoadVariant | `game/pz_energy_run.cpp` `readVariant` | puzzles_a | done |
| `33cd:1ba1` PzEnergy_DrawScreen | `game/pz_energy_run.cpp` `screen` | puzzles_a | done |
| `33cd:1cc7` PzEnergy_LoadResources | `game/pz_energy.cpp` `PzEnergy::loadResources` | puzzles_a | done |
| `33cd:1dd9` PzEnergy_Cleanup | `game/pz_energy_run.cpp` `PzEnergy::cleanup` | puzzles_a | done |
| `35bb:0000` PzMagnets_RectsOverlap | `game/puzzles.h` `spansOverlap` | puzzles_a | done |
| `35bb:0103` PzMagnets_WarpMouse | `game/pz_magnets_run.cpp` `PzMagnets::warpMouse` | puzzles_b | done |
| `35bb:0127` PzMagnets_DrawNumber | `game/pz_magnets.cpp` `PzMagnets::drawNumber` | puzzles_b | done |
| `35bb:01ac` PzMagnets_DrawInventory | `game/pz_magnets.cpp` `PzMagnets::drawInventory` | puzzles_b | done |
| `35bb:0236` PzMagnets_AllDrawn | `game/pz_magnets_run.cpp` `PzMagnets::allDrawn` | puzzles_b | done |
| `35bb:0271` PzMagnets_Overlap | `game/pz_magnets_run.cpp` `spansOverlap` | puzzles_b | done |
| `35bb:0401` PzMagnets_CanDraw | `game/pz_magnets_run.cpp` `PzMagnets::canDraw` | puzzles_b | done |
| `35bb:0456` PzMagnets_DrawTable | `game/pz_magnets.cpp` `PzMagnets::drawTable` | puzzles_b | done |
| `35bb:05be` PzMagnets_ISqrt | `game/pz_magnets_run.cpp` `PzMagnets::isqrt` | puzzles_b | done |
| `35bb:06aa` PzMagnets_Cube | `game/pz_magnets_run.cpp` `PzMagnets::cube` | puzzles_b | done |
| `35bb:06ba` PzMagnets_PoleForce | `game/pz_magnets_run.cpp` `PzMagnets::poleForce` | puzzles_b | done |
| `35bb:08ae` PzMagnets_Forces | `game/pz_magnets_run.cpp` `PzMagnets::forces` | puzzles_b | done |
| `35bb:09a5` PzMagnets_Blocked | `game/pz_magnets_run.cpp` `PzMagnets::blocked` | puzzles_b | done |
| `35bb:0aa4` PzMagnets_TouchesOpposite | `game/pz_magnets_run.cpp` `PzMagnets::touchesOpposite` | puzzles_b | done |
| `35bb:0c78` PzMagnets_TouchesSame | `game/pz_magnets_run.cpp` `PzMagnets::touchesSame` | puzzles_b | done |
| `35bb:0efc` PzMagnets_Step | `game/pz_magnets_run.cpp` `PzMagnets::step` | puzzles_b | done |
| `35bb:113c` PzMagnets_Simulate | `game/pz_magnets_run.cpp` `PzMagnets::simulate` | puzzles_b | done |
| `35bb:1208` PzMagnets_HoldMagnet | `game/pz_magnets_run.cpp` `PzMagnets::holdMagnet` | puzzles_b | done |
| `35bb:126f` PzMagnets_DropCursor | `game/pz_magnets_run.cpp` `PzMagnets::dropCursor` | puzzles_b | done |
| `35bb:12c4` PzMagnets_TakeFromInventory | `game/pz_magnets_run.cpp` `PzMagnets::takeFromInventory` | puzzles_b | done |
| `35bb:1387` PzMagnets_PutBack | `game/pz_magnets_run.cpp` `PzMagnets::putBack` | puzzles_b | done |
| `35bb:13a7` PzMagnets_PickUp | `game/pz_magnets_run.cpp` `PzMagnets::pickUp` | puzzles_b | done |
| `35bb:14f5` PzMagnets_Drop | `game/pz_magnets_run.cpp` `PzMagnets::drop` | puzzles_b | done |
| `35bb:16b6` PzMagnets_Rotate | `game/pz_magnets_run.cpp` `PzMagnets::rotate` | puzzles_b | done |
| `35bb:16f7` PzMagnets_CheckSolved | `game/pz_magnets_run.cpp` `PzMagnets::checkSolved` | puzzles_b | done |
| `35bb:1898` PzMagnets_Loop | `game/pz_magnets_run.cpp` `PzMagnets::loop` | puzzles_b | done |
| `35bb:1a47` PzMagnets_Init | `game/pz_magnets_run.cpp` `PzMagnets::init` | puzzles_b | done |
| `35bb:1a8f` PzMagnets_LoadResources | `game/pz_magnets.cpp` `PzMagnets::loadPictures` | puzzles_b | done |
| `35bb:1af4` PzMagnets_FreeResources | `game/pz_magnets_run.cpp` `PzMagnets::freeResources` | puzzles_b | done |
| `35bb:1b59` PzMagnets_DrawTargets | `game/pz_magnets.cpp` `PzMagnets::drawTargets` | puzzles_b | done |
| `35bb:1c03` PzMagnets_Run | `game/pz_magnets_run.cpp` `PzMagnets::run` | puzzles_b | done |
| `3797:0000` PzLaunch_DrawObject | `game/pz_launch_run.cpp` `PzLaunch::drawObject` | puzzles_a | done |
| `3797:0070` PzLaunch_DrawRamp | `game/pz_launch_run.cpp` `PzLaunch::drawRamp` | puzzles_a | done |
| `3797:009f` PzLaunch_DrawPlunger | `game/pz_launch_run.cpp` `PzLaunch::drawPlunger` | puzzles_a | done |
| `3797:0103` PzLaunch_Animate | `game/pz_launch_run.cpp` `PzLaunch::animate` | puzzles_a | done |
| `3797:018e` PzLaunch_GetCode | `game/pz_launch_run.cpp` `PzLaunch::getCode` | puzzles_a | done |
| `3797:01a5` PzLaunch_WaitTimer | `game/pz_launch_run.cpp` `PzLaunch::waitTimer` | puzzles_a | done |
| `3797:01c3` PzLaunch_WaitTimerAnim | `game/pz_launch_run.cpp` `PzLaunch::waitTimerAnim` | puzzles_a | done |
| `3797:01e1` PzLaunch_SetObjectSprite | `game/pz_launch_run.cpp` `PzLaunch::setObjectSprite` | puzzles_a | done |
| `3797:0217` PzLaunch_PutObjectOnLauncher | `game/pz_launch.cpp` `PzLaunch::putObjectOnLauncher` | puzzles_a | done |
| `3797:023c` PzLaunch_IsSolved | `game/pz_launch_run.cpp` `PzLaunch::isSolved` | puzzles_a | done |
| `3797:02c3` PzLaunch_AddZones | `game/pz_launch_run.cpp` `PzLaunch::addZones` | puzzles_a | done |
| `3797:030c` PzLaunch_LoadVariant | `game/pz_launch_run.cpp` `loadVariant` | puzzles_a | done |
| `3797:040d` PzLaunch_DrawScreen | `game/pz_launch_run.cpp` `puzzleLoadScreen` | puzzles_a | done |
| `3797:0464` PzLaunch_LoadResources | `game/pz_launch.cpp` `PzLaunch::loadResources` | puzzles_a | done |
| `3797:0725` PzLaunch_Cleanup | `game/pz_launch_run.cpp` `PzLaunch::cleanup` | puzzles_a | done |
| `3797:0870` PzLaunch_SetEnergy | `game/pz_launch_run.cpp` `PzLaunch::setEnergy` | puzzles_a | done |
| `3797:087d` PzLaunch_SetGravity | `game/pz_launch.cpp` `PzLaunch::setGravity` | puzzles_a | done |
| `3797:08b6` PzLaunch_DrawGravity | `game/pz_launch.cpp` `PzLaunch::drawGravity` | puzzles_a | done |
| `3797:08e2` PzLaunch_StepRamp | `game/pz_launch_run.cpp` `PzLaunch::stepRamp` | puzzles_a | done |
| `3797:0942` PzLaunch_SetOfferObject0 | `game/pz_launch_run.cpp` `PzLaunch::setOffer` | puzzles_a | done |
| `3797:094f` PzLaunch_SetOfferTool456 | `game/pz_launch.h` `setOffer` | puzzles_a | done |
| `3797:095c` PzLaunch_SetOfferTool457 | `game/pz_launch.h` `setOffer` | puzzles_a | done |
| `3797:0969` PzLaunch_SetSurface | `game/pz_launch_run.cpp` `PzLaunch::setSurface` | puzzles_a | done |
| `3797:0976` PzLaunch_SetOfferObject3 | `game/pz_launch.h` `setOffer` | puzzles_a | done |
| `3797:0983` PzLaunch_SetOfferObject2 | `game/pz_launch.h` `setOffer` | puzzles_a | done |
| `3797:0990` PzLaunch_SetOfferObject1 | `game/pz_launch.h` `setOffer` | puzzles_a | done |
| `3797:099d` PzLaunch_SetOfferTool452 | `game/pz_launch.h` `setOffer` | puzzles_a | done |
| `3797:09aa` PzLaunch_SetOfferTool453 | `game/pz_launch.h` `setOffer` | puzzles_a | done |
| `3797:09b7` PzLaunch_DrawBox | `game/pz_launch.cpp` `PzLaunch::drawBox` | puzzles_a | done |
| `3797:0ae9` PzLaunch_Redraw | `game/pz_launch_run.cpp` `PzLaunch::redraw` | puzzles_a | done |
| `3797:0b4b` PzLaunch_SetTarget | `game/pz_launch_run.cpp` `PzLaunch::setTarget` | puzzles_a | done |
| `3797:0b58` PzLaunch_ComputeLanding | `game/pz_launch_run.cpp` `PzLaunch::computeLanding` | puzzles_a | done |
| `3797:0bd9` PzLaunch_SelectObject | `game/pz_launch.cpp` `PzLaunch::selectObject` | puzzles_a | done |
| `3797:0c33` PzLaunch_Fly | `game/pz_launch_run.cpp` `PzLaunch::fly` | puzzles_a | done |
| `3797:0f33` PzLaunch_PullPlunger | `game/pz_launch_run.cpp` `PzLaunch::pullPlunger` | puzzles_a | done |
| `3797:0fca` PzLaunch_DragGravity | `game/pz_launch_run.cpp` `PzLaunch::dragGravity` | puzzles_a | done |
| `3797:1138` PzLaunch_Run | `game/pz_launch_run.cpp` `PzLaunch::run` | puzzles_a | done |
| `38e2:0000` State_09_Puzzle | `game/puzzle_run.cpp` `state09Puzzle` | puzzles_a | done |
| `38e2:00d4` Puzzle_Dispatch | `game/puzzle_run.cpp` `puzzleDispatch` | puzzles_a | done |
| `38e2:019e` Puzzle_FewUnsolvedLeft | `game/puzzle_run.cpp` `puzzleFewUnsolvedLeft` | puzzles_a | done |
| `38e2:01e8` Puzzle_RandomType | `game/puzzle_run.cpp` `puzzleRandomType` | puzzles_a | done |
| `38e2:0202` Puzzle_PickUnsolved | `game/puzzle_run.cpp` `puzzlePickUnsolved` | puzzles_a | done |
| `38e2:0286` Puzzle_ShowHintText | `game/puzzle_run.cpp` `puzzleShowHintText` | puzzles_a | done |
| `38e2:0299` Puzzle_ShowSolved | `game/puzzle_run.cpp` `puzzleShowSolved` | puzzles_a | done |
| `38e2:031a` Puzzle_PressGoBack | `game/puzzle_run.cpp` `puzzlePressGoBack` | puzzles_a | done |
| `38e2:0357` Puzzle_InitPanel | `game/puzzle_run.cpp` `puzzleInitPanel` | puzzles_a | done |
| `38e2:03bf` Puzzle_AwardPoints | `game/puzzle_run.cpp` `puzzleAwardPoints` | puzzles_a | done |
| `38e2:03ed` Puzzle_FreePanel | `game/puzzle_run.cpp` `puzzleFreePanel` | puzzles_a | done |
| `3924:0000` PzSnd_LoadBank | `audio/pz_snd.cpp` `PzSnd::loadBank` | puzzles_a | done |
| `3924:005b` PzSnd_WaitEnd | `game/sound_audio.cpp` `GgAudioSound::pzIsActive` | flow | done |
| `3924:0074` PzSnd_FreeBank | `audio/pz_snd.cpp` `PzSnd::freeBank` | puzzles_a | done |
| `3924:00ae` PzSnd_Play | `game/sound_audio.cpp` `GgAudioSound::pzPlay` | flow | done |
| `3924:00cb` PzSnd_PlayLoop | `audio/pz_snd.h` `playLoop` | puzzles_a | done |
| `3924:00e8` PzSnd_Stop | `game/sound_audio.cpp` `GgAudioSound::pzStop` | flow | done |
| `3924:00fc` PzSnd_StopAll | `audio/pz_snd.cpp` `PzSnd::stopAll` | puzzles_a | done |
| `3924:0124` PzSnd_SetTranspose | `audio/pz_snd.h` `setTranspose` | puzzles_a | done |
| `3938:0000` PzSimple_Run | `game/pz_simple_run.cpp` `PzSimple::run` | puzzles_a | done |
| `3938:0233` PzSimple_DropPart | `game/pz_simple_run.cpp` `PzSimple::dropPart` | puzzles_a | done |
| `3938:0345` PzSimple_PickUpPlaced | `game/pz_simple_run.cpp` `PzSimple::pickUpPlaced` | puzzles_a | done |
| `3938:040f` PzSimple_DrawPlaced | `game/pz_simple.cpp` `PzSimple::drawPlaced` | puzzles_a | done |
| `3938:046b` PzSimple_PointInRect | `game/pz_simple_run.cpp` `PzSimple::pointInRect` | puzzles_a | done |
| `3938:04a1` PzSimple_InitCursorRegions | `game/pz_simple_run.cpp` `PzSimple::initCursorRegions` | puzzles_a | done |
| `3938:0524` PzSimple_AddCursorRegions | `game/pz_simple_run.cpp` `PzSimple::addCursorRegions` | puzzles_a | done |
| `3938:054d` PzSimple_CheckParts | `game/pz_simple_run.cpp` `PzSimple::checkParts` | puzzles_a | done |
| `3938:0671` PzSimple_BounceBack | `game/pz_simple_run.cpp` `PzSimple::bounceBack` | puzzles_a | done |
| `3938:0750` PzSimple_JudgeAndReveal | `game/pz_simple_run.cpp` `PzSimple::judgeAndReveal` | puzzles_a | done |
| `3938:0886` PzSimple_AnimateFit | `game/pz_simple_run.cpp` `PzSimple::animateFit` | puzzles_a | done |
| `3938:099f` PzSimple_ReturnHeld | `game/pz_simple_run.cpp` `PzSimple::returnHeld` | puzzles_a | done |
| `3938:09df` PzSimple_TakeFromInventory | `game/pz_simple_run.cpp` `PzSimple::takeFromInventory` | puzzles_a | done |
| `3938:0a64` PzSimple_FindRecord | `game/pz_simple_run.cpp` `PzSimple::findRecord` | puzzles_a | done |
| `3938:0af7` PzSimple_RemoveRecordZones | `game/pz_simple_run.cpp` `PzSimple::removeRecordZones` | puzzles_a | done |
| `3938:0b59` PzSimple_BuildHintText | `game/pz_simple_run.cpp` `PzSimple::buildHintText`; wave 2: `game/pz_simple.h` `buildHint` | puzzles_a | done |
| `3938:0ce7` Str_ConcatList_ovl20 | std::string (the overlays’ private string copies) | — | absorbed |
| `3938:0d3f` Str_CopyEnd_ovl20 | std::string (the overlays’ private string copies) | — | absorbed |
| `3938:0d63` PzSimple_DrawInventory | `game/pz_simple.cpp` `PzSimple::drawInventory` | puzzles_a | done |
| `3938:0dfe` PzSimple_LoadVariant | `game/pz_simple.cpp` `PzSimple::loadVariant` | puzzles_a | done |
| `3938:0e76` PzSimple_DrawScreen | `game/pz_simple_run.cpp` `puzzleLoadScreen` | puzzles_a | done |
| `3938:0ed8` PzSimple_LoadResources | `game/pz_simple.cpp` `PzSimple::loadResources` | puzzles_a | done |
| `3938:1004` PzSimple_Cleanup | `game/pz_simple_run.cpp` `PzSimple::cleanup` | puzzles_a | done |
| `3a4a:0000` PzPieces_Run | `game/pz_pieces_run.cpp` `PzPieces::run` | puzzles_b | done |
| `3a4a:01ef` PzPieces_SetMouseBounds | `game/pz_pieces_run.cpp` `PzPieces::setMouseBounds` | puzzles_b | done |
| `3a4a:0265` PzPieces_PickUp | `game/pz_pieces_run.cpp` `PzPieces::pickUp` | puzzles_b | done |
| `3a4a:037d` PzPieces_Drop | `game/pz_pieces_run.cpp` `PzPieces::drop` | puzzles_b | done |
| `3a4a:053f` PzPieces_RedrawWork | `game/pz_pieces.cpp` `PzPieces::redrawWork` | puzzles_b | done |
| `3a4a:05b8` PzPieces_MoveGroup | `game/pz_pieces_run.cpp` `PzPieces::moveGroup` | puzzles_b | done |
| `3a4a:071d` PzPieces_MergeGroups | `game/pz_pieces_run.cpp` `PzPieces::mergeGroups` | puzzles_b | done |
| `3a4a:0780` PzPieces_TakeFromBox | `game/pz_pieces_run.cpp` `PzPieces::takeFromBox` | puzzles_b | done |
| `3a4a:089c` PzPieces_AddZones | `game/pz_pieces_run.cpp` `PzPieces::addZones` | puzzles_b | done |
| `3a4a:08c9` PzPieces_RemoveZones | `game/pz_pieces_run.cpp` `PzPieces::removeZones` | puzzles_b | done |
| `3a4a:08f6` PzPieces_CheckSolved | `game/pz_pieces_run.cpp` `PzPieces::checkSolved` | puzzles_b | done |
| `3a4a:0a88` PzPieces_OpenBox | `game/pz_pieces_run.cpp` `PzPieces::openBox` | puzzles_b | done |
| `3a4a:0ade` PzPieces_CloseBox | `game/pz_pieces_run.cpp` `PzPieces::closeBox` | puzzles_b | done |
| `3a4a:0b33` PzPieces_InitZones | `game/pz_pieces_run.cpp` `PzPieces::initZones` | puzzles_b | done |
| `3a4a:0baf` PzPieces_AddStaticZones | `game/pz_pieces_run.cpp` `PzPieces::addStaticZones` | puzzles_b | done |
| `3a4a:0bcb` PzPieces_LoadVariant | `game/pz_pieces.cpp` `PzPieces::loadVariant` | puzzles_b | done |
| `3a4a:0be5` PzPieces_DrawScreen | `game/pz_pieces_run.cpp` `PzPieces::drawScreen` | puzzles_b | done |
| `3a4a:0ca8` PzPieces_LoadResources | `game/pz_pieces.cpp` `PzPieces::loadResources` | puzzles_b | done |
| `3a4a:0d95` PzPieces_Free | `game/pz_pieces_run.cpp` `PzPieces::free` | puzzles_b | done |
| `3b2f:0000` PzGears_AllButtonsPressed | `game/pz_gears_run.cpp` `PzGears::allButtonsPressed` | puzzles_b | done |
| `3b2f:0033` PzGears_CountStock | `game/pz_gears.cpp` `PzGears::countStock` | puzzles_b | done |
| `3b2f:0070` PzGears_AddZones | `game/pz_gears_run.cpp` `PzGears::addZones` | puzzles_b | done |
| `3b2f:00a9` PzGears_DrawInventory | `game/pz_gears.cpp` `PzGears::drawInventory` | puzzles_b | done |
| `3b2f:01e4` PzGears_DrawPeg | `game/pz_gears.cpp` `PzGears::drawPeg` | puzzles_b | done |
| `3b2f:024f` PzGears_Draw | `game/pz_gears.cpp` `PzGears::draw` | puzzles_b | done |
| `3b2f:071c` PzGears_DrawScreen | `game/pz_gears_run.cpp` `puzzleLoadScreen` | puzzles_b | done |
| `3b2f:079b` PzGears_InitCrankRects | `game/pz_gears_run.cpp` `PzGears::initCrankRects` | puzzles_b | done |
| `3b2f:081e` PzGears_SetCrankZone | `game/pz_gears_run.cpp` `PzGears::setCrankZone` | puzzles_b | done |
| `3b2f:0892` PzGears_AddGearZone | `game/pz_gears_run.cpp` `PzGears::addGearZone` | puzzles_b | done |
| `3b2f:0972` PzGears_RemoveGearZone | `game/pz_gears_run.cpp` `PzGears::removeGearZone` | puzzles_b | done |
| `3b2f:098f` PzGears_WarpMouse | `game/pz_gears_run.cpp` `PzGears::warpMouse` | puzzles_b | done |
| `3b2f:09b3` PzGears_PutBack | `game/pz_gears_run.cpp` `PzGears::putBack` | puzzles_b | done |
| `3b2f:0a22` PzGears_TakeFromStock | `game/pz_gears_run.cpp` `PzGears::takeFromStock` | puzzles_b | done |
| `3b2f:0b18` PzGears_PickUp | `game/pz_gears_run.cpp` `PzGears::pickUp` | puzzles_b | done |
| `3b2f:0c2b` PzGears_Place | `game/pz_gears_run.cpp` `PzGears::place` | puzzles_b | done |
| `3b2f:0ecc` PzGears_GearToGear | `game/pz_gears_run.cpp` `PzGears::gearToGear` | puzzles_b | done |
| `3b2f:0ff9` PzGears_HRackToHRack | `game/pz_gears_run.cpp` `PzGears::hRackToHRack` | puzzles_b | done |
| `3b2f:111d` PzGears_HRackToButton | `game/pz_gears_run.cpp` `PzGears::hRackToButton` | puzzles_b | done |
| `3b2f:11dc` PzGears_VRackToButton | `game/pz_gears_run.cpp` `PzGears::vRackToButton` | puzzles_b | done |
| `3b2f:129b` PzGears_VRackToVRack | `game/pz_gears_run.cpp` `PzGears::vRackToVRack` | puzzles_b | done |
| `3b2f:13bf` PzGears_GearToHRack | `game/pz_gears_run.cpp` `PzGears::gearToHRack` | puzzles_b | done |
| `3b2f:14ae` PzGears_HRackToGear | `game/pz_gears_run.cpp` `PzGears::hRackToGear` | puzzles_b | done |
| `3b2f:15c3` PzGears_GearToVRack | `game/pz_gears_run.cpp` `PzGears::gearToVRack` | puzzles_b | done |
| `3b2f:16b2` PzGears_VRackToGear | `game/pz_gears_run.cpp` `PzGears::vRackToGear` | puzzles_b | done |
| `3b2f:17c7` PzGears_Propagate | `game/pz_gears_run.cpp` `PzGears::propagate` | puzzles_b | done |
| `3b2f:185c` PzGears_TurnCrank | `game/pz_gears_run.cpp` `PzGears::turnCrank` | puzzles_b | done |
| `3b2f:1991` PzGears_RectContains | `game/pz_gears_run.cpp` `PzGears::rectContains` | puzzles_b | done |
| `3b2f:19bf` PzGears_DragCrank | `game/pz_gears_run.cpp` `PzGears::dragCrank` | puzzles_b | done |
| `3b2f:1b83` PzGears_LoadVariant | `game/pz_gears.cpp` `PzGears::loadVariant` | puzzles_b | done |
| `3b2f:1bd5` PzGears_LoadResources | `game/pz_gears.cpp` `PzGears::loadResources` | puzzles_b | done |
| `3b2f:1cad` PzGears_Free | `game/pz_gears_run.cpp` `PzGears::free` | puzzles_b | done |
| `3b2f:1da8` PzGears_SaveHome | `game/pz_gears.cpp` `PzGears::saveHome` | puzzles_b | done |
| `3b2f:1e0a` PzGears_SaveFrames | `game/pz_gears_run.cpp` `PzGears::saveFrames` | puzzles_b | done |
| `3b2f:1e46` PzGears_Reset | `game/pz_gears_run.cpp` `PzGears::reset` | puzzles_b | done |
| `3b2f:1ed0` PzGears_Run | `game/pz_gears_run.cpp` `PzGears::run` | puzzles_b | done |

#### TLC UI library, sign-in, PLAYERS.DAT, hints (ui_tlc.tsv)

| Original | Engine | Owner | Status |
|---|---|---|---|
| `2b16:0000` Font_Load | `text/font.cpp` `GgFont::parse` | wave 2 | done |
| `2b16:021c` Font_Init | `text/font.cpp` `Fonts::load` | wave 2 | done |
| `2b16:0243` Font_SetColor | `text/font.cpp` `Fonts::setColor` | wave 2 | done |
| `2b16:0256` Font_Select | `text/font.cpp` `Fonts::select` | wave 2 | done |
| `2b16:029c` Font_DrawString | `text/font.cpp` `Fonts::drawString` | wave 2 | done |
| `2b16:0368` Font_DrawChar | `text/font.cpp` `Fonts::drawChar` | wave 2 | done |
| `2b16:03ea` Font_CharWidth | `text/text.cpp` `FontMetrics::charWidth` | wave 2 | done |
| `2b16:0418` Font_GetHeight | `text/text.cpp` `FontMetrics::lineHeight` | wave 2 | done |
| `2b16:0426` Font_GetCurrent | `text/font.h` `current` | wave 2 | done |
| `2b89:0000` Text_DrawBox | `text/text.cpp` `textDrawBox` | wave 2 | done |
| `2b89:002a` Text_Measure | `text/text.cpp` `textMeasure` | wave 2 | done |
| `2b89:0056` Text_Width | `text/text.cpp` `textWidth` | wave 2 | done |
| `2b89:007a` Text_Render | `text/text.cpp` (measure and draw as Text_Measure / Text_DrawBox) | — | absorbed |
| `2b89:03ca` Text_MeasureRun | `text/text.cpp` `FontMetrics::runWidth` | wave 2 | done |
| `2b89:0466` Text_DrawChar | `text/text.cpp` `FontCanvas::underline` | wave 2 | done |
| `3797:100e` Anim_PlayModal | `game/dam.cpp` `damPlayModal` | puzzles_a | done |
| `3d39:0000` Anim_Init | no caller in the shipped game (`game/dam.h`) | — | absorbed |
| `3d39:0049` Anim_RestoreBgRect | no caller in the shipped game (`game/dam.h`) | — | absorbed |
| `3d39:00ba` Anim_PresentRect | `game/dam.cpp` `DamPlayer::presentRect` | puzzles_a | done |
| `3d39:011c` Anim_ResetAll | no caller in the shipped game (`game/dam.h`) | — | absorbed |
| `3d39:013a` Anim_SeekTick | no caller in the shipped game (`game/dam.h`) | — | absorbed |
| `3d39:0323` Anim_DrawCel | no caller in the shipped game (`game/dam.h`) | — | absorbed |
| `3d39:037e` Anim_EraseList | no caller in the shipped game (`game/dam.h`) | — | absorbed |
| `3d39:03cb` Anim_Start | `game/dam.cpp` `DamPlayer::start` | puzzles_a | done |
| `3d39:0468` Anim_IsRunning | `game/dam.cpp` `DamPlayer::isRunning` | puzzles_a | done |
| `3d39:04ae` Anim_Stop | no caller in the shipped game (`game/dam.h`) | — | absorbed |
| `3d39:04f9` Anim_SetPause | no caller in the shipped game (`game/dam.h`) | — | absorbed |
| `3d39:0553` Anim_EraseSprites | no caller in the shipped game (`game/dam.h`) | — | absorbed |
| `3d39:058f` Anim_Step | `game/dam.cpp` `DamPlayer::step` | puzzles_a | done |
| `3d39:084a` Anim_DrawSprites | `game/dam.cpp` `DamPlayer::drawSprites` | puzzles_a | done |
| `3d39:0983` Anim_FlushUpdateRects | no caller in the shipped game (`game/dam.h`) | — | absorbed |
| `3f56:0000` Dlg_Layout | `ui/dialog.cpp` `dlgLayout` | ui | done |
| `3f56:033e` Dlg_SaveUnder | `ui/dialog.cpp` `getImage` | ui | done |
| `3f56:0367` Dlg_RestoreUnder | `ui/dialog.cpp` `dlgRestoreUnder` | ui | done |
| `3f56:0389` Dlg_Draw | `ui/dialog_run.cpp` `dlgDraw` | ui | done |
| `3f56:05d7` Dlg_AddZones | `ui/dialog.cpp` `dlgAddZones` | ui | done |
| `3f56:061f` Dlg_RemoveZones | `ui/dialog.cpp` `dlgRemoveZones` | ui | done |
| `3f56:0655` Dlg_RunModal | `ui/dialog_run.cpp` `dlgRunModal` | ui | done |
| `3f56:08ee` Dlg_Show | `ui/dialog_run.cpp` `dlgShow` | ui | done |
| `3f56:0907` Dlg_RunPager | `ui/dialog_run.cpp` `dlgRunPager` | ui | done |
| `3f56:0b1c` Dlg_MessageBox | `ui/dialog_run.cpp` `dlgMessageBox` | ui | done |
| `3f56:0bb6` Dlg_YesNo | `ui/dialog_run.cpp` `dlgYesNo` | ui | done |
| `3f56:0c5e` Dlg_AskChimps | `ui/dialog_run.cpp` `dlgAskChimps` | ui | done |
| `3f56:0d2a` Dlg_MessageRes | `ui/dialog_run.cpp` `dlgMessageRes`; wave 2: `ui/signin.h` `messageRes` | ui | done |
| `4034:0000` Btn_Draw | `ui/button.cpp` `btnDraw` | ui | done |
| `4034:016b` Btn_ToggleFocus | `ui/button.cpp` `btnToggleFocus` | ui | done |
| `4034:01d6` Btn_Flash | `ui/button.cpp` `btnFlash` | ui | done |
| `4034:0201` PicBtn_Load | `ui/button.cpp` `picBtnLoad` | ui | done |
| `4034:026a` PicBtn_Free | `ui/button.cpp` `picBtnFree` | ui | done |
| `4034:027c` PicBtn_Draw | `ui/button.cpp` `picBtnDraw` | ui | done |
| `4034:02c4` PicBtn_Erase | `ui/button.cpp` `picBtnErase` | ui | done |
| `4034:02e4` Btn_AddZone | `ui/button.h` `btnAddZone` | ui | done |
| `4034:02f4` Btn_RemoveZone | `ui/button.h` `btnAddZone` | ui | done |
| `4034:0304` PicBtn_DrawPressed | `ui/button.cpp` `picBtnDrawPressed` | ui | done |
| `4034:032f` PicBtn_DrawNormal | `ui/button.cpp` `picBtnDrawNormal` | ui | done |
| `406a:0000` ScrollBar_DrawThumb | `ui/scroll_bar.cpp` `scrollBarDrawThumb` | ui | done |
| `406a:003f` ScrollBar_Draw | `ui/scroll_bar.cpp` `scrollBarDraw` | ui | done |
| `406a:013e` ScrollBar_SetPos | `ui/scroll_bar.cpp` `scrollBarSetPos` | ui | done |
| `406a:017f` ScrollBar_SetRange | `ui/scroll_bar.cpp` `scrollBarSetRange` | ui | done |
| `406a:01b6` ScrollBar_HandleClick | `ui/scroll_bar.cpp` `scrollBarHandleClick` | ui | done |
| `40a4:0000` Menu_SetMenuBar | `ui/menu_run.cpp` `menuSetMenuBar` | ui | done |
| `40a4:00ee` Menu_AddItemZones | `ui/menu_run.cpp` `addItemZones` | ui | done |
| `40a4:01c7` Menu_RemoveItemZones | `ui/menu_run.cpp` `removeItemZones` | ui | done |
| `40a4:020b` Menu_RunDropDown | `ui/menu_run.cpp` `menuRunDropDown` | ui | done |
| `40a4:04db` Menu_DrawBar | `ui/menu_run.cpp` `menuDrawBar` | ui | done |
| `40a4:056c` Menu_DrawDropDown | `ui/menu_run.cpp` `menuDrawDropDown` | ui | done |
| `4106:0000` MsgDesc_LoadRes | `ui/dialog_run.cpp` `YesNoMessage::load` | ui | done |
| `4106:006d` Dlg_YesNoRes | `ui/dialog_run.cpp` `dlgYesNoRes`; wave 2: `ui/signin.h` `yesNoRes` | ui | done |
| `4106:00b2` Cmd_SaveAndExit | `game/flow.cpp` `cmdSaveAndExit` | flow | done |
| `4106:00d7` Cmd_StartNewPlayer | `ui/commands.cpp` `cmdStartNewPlayer` | ui | done |
| `4106:00f8` Cmd_ToggleHints | `ui/commands.cpp` `cmdToggleHints` | ui | done |
| `4106:011e` Cmd_ToggleSound | `ui/commands.cpp` `cmdToggleSound` | ui | done |
| `4106:014f` Cmd_ReturnToCenter | `ui/commands.cpp` `cmdReturnToCenter` | ui | done |
| `411d:0000` Help_ShowTopic | `ui/commands.cpp` `helpShowTopic` | ui | done |
| `411d:01e0` Help_AboutGame | `ui/help.cpp` `kHelpAboutGame` | ui | done |
| `411d:01ec` Help_BuildingVehicles | `ui/help.cpp` (one topic table for the four Help commands) | — | absorbed |
| `411d:01f8` Help_FindingParts | `ui/help.cpp` (one topic table for the four Help commands) | — | absorbed |
| `411d:0204` Help_MovingAround | `ui/help.cpp` (one topic table for the four Help commands) | — | absorbed |
| `411d:0210` Help_ChallengeRaces | `ui/help.cpp` (one topic table for the four Help commands) | — | absorbed |
| `411d:021c` Help_SolvingPuzzles | `ui/help.cpp` `kHelpSolvingPuzzles` | ui | done |
| `4140:0000` TextView_WarpMouse | `ui/text_view_run.cpp` `textViewWarpMouse` | ui | done |
| `4140:0034` TextView_ZoneHit | `ui/text_view_run.cpp` `textViewZoneHit` | ui | done |
| `4140:0081` TextView_SetThumbY | `ui/text_viewer.cpp` `TextViewer::setThumbY` | ui | done |
| `4140:00a9` TextView_ThumbStep | `ui/text_viewer.h` TextViewer (ui) | — | absorbed |
| `4140:00b1` TextView_ThumbDown | `ui/text_viewer.cpp` `TextViewer::thumbDown` | ui | done |
| `4140:00d3` TextView_ThumbUp | `ui/text_viewer.cpp` `TextViewer::thumbUp` | ui | done |
| `4140:00f3` TextView_SetThumbStep | `ui/text_viewer.cpp` `thumbStep_` | ui | done |
| `4140:0102` TextView_UpdateBarZones | `ui/text_viewer.cpp` `TextViewer::updateBarZones` | ui | done |
| `4140:016e` TextView_DrawBar | `ui/text_viewer.cpp` `TextViewer::drawBar` | ui | done |
| `4140:01bc` TextView_GlyphVisible | `ui/text_viewer.cpp` `TextViewLayout::glyphVisible` | ui | done |
| `4140:01ff` TextView_WordWidth | `ui/text_viewer.cpp` `TextViewLayout::wordWidth` | ui | done |
| `4140:027a` TextView_DrawText | `ui/text_viewer.cpp` `TextViewLayout::drawText` | ui | done |
| `4140:03d7` TextView_CountLines | `ui/text_viewer.cpp` `TextViewLayout::countLines` | ui | done |
| `4140:04e5` TextView_DrawArrow | `ui/text_viewer.cpp` `TextViewer::drawArrow` | ui | done |
| `4140:052f` TextView_DrawPanel | `ui/text_viewer.cpp` `TextViewer::drawPanel` | ui | done |
| `4140:056b` TextView_FlashArrow | `ui/text_view_run.cpp` `textViewFlashArrow` | ui | done |
| `4140:05b3` TextView_MouseOnArrow | `ui/text_view_run.cpp` `textViewMouseOnArrow` | ui | done |
| `4140:0605` TextView_Redraw | `ui/text_viewer.cpp` `TextViewer::redraw` | ui | done |
| `4140:063d` TextView_ScrollBy | `ui/text_viewer.cpp` `TextViewer::scrollBy` | ui | done |
| `4140:0667` TextView_Run | `ui/text_view_run.cpp` `textViewRun` | ui | done |

#### Engine low level (engine_lowlevel.tsv)

| Original | Engine | Owner | Status |
|---|---|---|---|
| `12e0:0b92` Video_SetMode13h | the host layer (no DOS video mode, DCC, DOS strings or far pointers) | — | absorbed |
| `1399:000e` Video_RestoreMode | the host layer (no DOS video mode, DCC, DOS strings or far pointers) | — | absorbed |
| `1399:0021` Gfx_ClipX | `gfx/screen.cpp` `clipX` | wave 2 | done |
| `1399:004c` Gfx_ClipY | `gfx/screen.cpp` `clipY` | wave 2 | done |
| `1399:0072` Gfx_HLine | `gfx/screen.cpp` `Screen::hLine` | wave 2 | done |
| `1399:00b3` Gfx_VLine | `gfx/screen.cpp` `Screen::vLine` | wave 2 | done |
| `1399:0100` Gfx_FillRect | `gfx/screen.cpp` `Screen::fillRect` | wave 2 | done |
| `1399:0146` Gfx_XorRect | `gfx/screen.cpp` `Screen::xorRect` | wave 2 | done |
| `1399:0194` Gfx_GetImage | `gfx/screen.cpp` `Screen::getImage` | wave 2 | done |
| `1399:01f1` Gfx_PutImage | `gfx/screen.cpp` `Screen::putImage` | wave 2 | done |
| `1399:0233` Gfx_RestoreRect | `gfx/screen.cpp` `Screen::restoreRect` | wave 2 | done |
| `1399:02a2` Gfx_SaveToBack | `gfx/screen.cpp` `Screen::saveToBack` | wave 2 | done |
| `1399:02ee` Lpx_DrawKeyed | `game/dam.cpp` `lpxDrawKeyed` | puzzles_a | done |
| `1399:0335` Lpx_Draw | `game/dam.cpp` `lpxDraw` | puzzles_a | done |
| `1399:036d` Lpx_BlitRaw | `gfx/screen.cpp` `Screen::blitRaw` | wave 2 | done |
| `1399:0399` Lpx_BlitRawKeyed | `gfx/screen.cpp` `Screen::blitRawKeyed` | wave 2 | done |
| `1399:0465` Lpx_BlitRle | `gfx/screen.cpp` `Screen::blitRle` | wave 2 | done |
| `1399:04d1` Lpx_BlitRleMirror | `gfx/screen.cpp` `Screen::blitRleMirror` | wave 2 | done |
| `1399:0541` Gfx_DrawGlyph | `gfx/screen.cpp` `Screen::drawGlyph` | wave 2 | done |
| `1399:05d6` Pal_SetRange | `app/display.cpp` `pending` | wave 2 | done |
| `1399:0a85` Movie_DecodeFrame | `gfx/movie.cpp` `movieDecodeFrame` | wave 2 | done |
| `1399:0b22` Gfx_HideMouseOnce | `gfx/screen.cpp` `Screen::hideMouseOnce` | wave 2 | done |
| `1399:0b3e` Gfx_ResetMouseHide | `gfx/screen.h` `resetMouseHide` | wave 2 | done |
| `1399:0b45` Gfx_PutPixel | `gfx/screen.cpp` `Screen::putPixel` | wave 2 | done |
| `1399:0b71` Gfx_RemapPicture | `gfx/lpx.cpp` `remapPicture` | wave 2 | done |
| `1454:0000` Input_InstallHandlers | `app/gg_app.cpp` `installHandlers` | flow | done |
| `1454:0058` Input_RemoveHandlers | `app/gg_runtime.cpp` `removeHandler` | wave 2 | done |
| `1454:0096` Timer_ISR | `app/gg_clock.h` `tick` | wave 2 | done |
| `1454:00e4` Kbd_ISR | `input/keyboard.cpp` `KbdIsr::onScanByte` | wave 2 | done |
| `1454:0187` Kbd_MakeLeft | `input/keyboard.cpp` `KbdIsr::makeHandler` | wave 2 | done |
| `1454:018f` Kbd_BreakLeft | `input/keyboard.cpp` `KbdIsr::breakHandler` | wave 2 | done |
| `1454:0197` Kbd_MakeRight | `input/keyboard.cpp` GgKeyboard (the ISR’s make / break table as one switch) | — | absorbed |
| `1454:019f` Kbd_BreakRight | `input/keyboard.cpp` GgKeyboard (the ISR’s make / break table as one switch) | — | absorbed |
| `1454:01a7` Kbd_MakeSpace | `input/keyboard.cpp` GgKeyboard (the ISR’s make / break table as one switch) | — | absorbed |
| `1454:01af` Kbd_BreakSpace | `input/keyboard.cpp` GgKeyboard (the ISR’s make / break table as one switch) | — | absorbed |
| `1454:01b7` Kbd_MakeCtrl | `input/keyboard.cpp` GgKeyboard (the ISR’s make / break table as one switch) | — | absorbed |
| `1454:01c3` Kbd_BreakCtrl | `input/keyboard.cpp` GgKeyboard (the ISR’s make / break table as one switch) | — | absorbed |
| `1454:01cf` Kbd_MakeUp | `input/keyboard.cpp` GgKeyboard (the ISR’s make / break table as one switch) | — | absorbed |
| `1454:01d7` Kbd_BreakUp | `input/keyboard.cpp` GgKeyboard (the ISR’s make / break table as one switch) | — | absorbed |
| `1454:01df` Kbd_MakeDown | `input/keyboard.cpp` GgKeyboard (the ISR’s make / break table as one switch) | — | absorbed |
| `1454:01e7` Kbd_BreakDown | `input/keyboard.cpp` GgKeyboard (the ISR’s make / break table as one switch) | — | absorbed |
| `1454:01ef` Kbd_KeyC | `input/keyboard.cpp` GgKeyboard (the ISR’s make / break table as one switch) | — | absorbed |
| `1473:000c` Pal_Blackout | `gfx/palette.cpp` `Palette::blackout` | wave 2 | done |
| `1473:0042` Screen_RandomWipe | `gfx/transitions.cpp` `randomWipePattern` | wave 2 | done |
| `1473:0074` Gfx_Init | `game/game.cpp` `init` | skeleton | done |
| `1473:0103` Gfx_Shutdown | the host layer (no DOS video mode, DCC, DOS strings or far pointers) | — | absorbed |
| `1473:010d` Gfx_FrameRect | `gfx/screen.cpp` `Screen::frameRect` | wave 2 | done |
| `1473:015c` Pal_Fade | `gfx/palette.cpp` `PalFade::PalFade` | wave 2 | done |
| `1473:0265` Pal_FadeOut | `gfx/palette.cpp` `Palette::fadeOut` | wave 2 | done |
| `1473:02a0` Pal_FadeIn | `gfx/palette.cpp` `Palette::fadeIn` | wave 2 | done |
| `1473:02db` Pal_FadeFromSaved | `app/display.h` `fadeFromSaved` | wave 2 | done |
| `1473:02f0` Pal_SetShadowRange | `gfx/palette.cpp` `Palette::setShadowRange` | wave 2 | done |
| `1473:0325` Screen_FadeOutIn | `app/display.h` `fadeOutIn` | wave 2 | done |
| `1473:0337` Screen_SavePlayArea | `gfx/screen.h` `savePlayArea` | wave 2 | done |
| `1473:034e` Screen_SaveAll | `gfx/screen.h` `saveAll` | wave 2 | done |
| `1473:0365` Screen_RestoreAll | `gfx/screen.h` `restoreAll` | wave 2 | done |
| `1473:037c` Screen_ClearPlayArea | `gfx/screen.cpp` `Screen::clearPlayArea` | wave 2 | done |
| `1473:0399` Screen_LoadPicture | `gfx/video.cpp` `Video::loadPicture` | wave 2 | done |
| `1473:03df` Obj_DrawKeyed | `gfx/screen.cpp` `Screen::objDrawKeyed` | wave 2 | done |
| `1473:040e` Gfx_ImageSize | `gfx/screen.h` `imageSize` | wave 2 | done |
| `1473:041c` Pal_LoadRes | `gfx/palette.cpp` `Palette::store` | wave 2 | done |
| `1473:045a` Screen_Show | `gfx/transitions.cpp` `ScreenShow::ScreenShow` | wave 2 | done |
| `1473:0479` Screen_Wipe | `app/display.cpp` `wipe` | wave 2 | done |
| `1473:074e` Pal_Save | `game/puzzle_run.cpp` `palette` | puzzles_a | done |
| `1473:0766` Pal_Restore | `game/puzzle_run.cpp` `palette` | puzzles_a | done |
| `1473:077e` Gfx_RecolorPicture | `gfx/lpx.cpp` `recolorPicture` | wave 2 | done |
| `1473:07d9` Movie_Play | `game/results.cpp` `movie` | vehicles | done |
| `14ff:000d` Res_Read | `data/archive.h` Archive (resources read once, copies are spans) | — | absorbed |
| `14ff:00fe` Res_LoadCopy | `data/archive.h` Archive (resources read once, copies are spans) | — | absorbed |
| `14ff:013d` Res_LoadTo | `data/archive.h` Archive (resources read once, copies are spans) | — | absorbed |
| `14ff:0167` Res_OpenFiles | `data/archive.cpp` `size` | wave 2 | done |
| `14ff:0209` Res_CloseFiles | `data/archive.h` Archive (resources read once, copies are spans) | — | absorbed |
| `14ff:0226` Screen_SaveToSwap | in-memory copies (the blueprint’s save-under: gizmo004.dat is not written) | — | absorbed |
| `14ff:0284` Screen_RestoreFromSwap | in-memory copies (the blueprint’s save-under: gizmo004.dat is not written) | — | absorbed |
| `152c:0009` Timer_Set | `app/timers.h` `set` | wave 2 | done |
| `152c:0029` Timer_Wait | `app/gg_runtime.h` `timerWait` | wave 2 | done |
| `152c:004a` Timer_Expired | `app/gg_runtime.h` `timerExpired` | wave 2 | done |
| `152c:0072` Delay | `ui/button.cpp` `delay` | ui | done |
| `1536:0007` Mem_Alloc | C++ ownership (Mem_Alloc cannot fail; the heap checks have nothing to check) | — | absorbed |
| `1536:0047` Mem_Free | C++ ownership (Mem_Alloc cannot fail; the heap checks have nothing to check) | — | absorbed |
| `1536:007c` Mem_CheckHeap | C++ ownership (Mem_Alloc cannot fail; the heap checks have nothing to check) | — | absorbed |
| `1581:03ee` Dirty_Mark | `gfx/screen.cpp` `DirtyMap::mark` | wave 2 | done |
| `1581:0470` Dirty_Clear | `gfx/screen.cpp` `DirtyMap::clear` | wave 2 | done |
| `1581:0484` Sprites_Reset | `ui/attract.cpp` `resetSprites` | flow | done |
| `1581:0496` Sprites_Add | `gfx/sprites.cpp` `SpriteList::add` | wave 2 | done |
| `1581:04b6` Sprites_EraseAll | `gfx/sprites.cpp` `SpriteList::eraseAll` | wave 2 | done |
| `1581:04fc` Sprites_DrawAll | `gfx/sprites.cpp` `SpriteList::drawAll` | wave 2 | done |
| `1581:052a` Dirty_Flush | `gfx/video.cpp` `dirtyFlush` | wave 2 | done |
| `15df:000d` PicCache_Init | `game/pic_cache.h` PicCache (its map and the mirrored slot table replace the lookups) | — | absorbed |
| `15df:003e` PicCache_Find | `game/pic_cache.h` PicCache (its map and the mirrored slot table replace the lookups) | — | absorbed |
| `15df:007d` PicList_Find | `game/pic_cache.h` PicCache (its map and the mirrored slot table replace the lookups) | — | absorbed |
| `15df:00a1` PicCache_Load | `game/pic_cache.cpp` `PicCache::pictureOf` | world | done |
| `15df:01a5` Pic_Read | `game/pic_cache.h` `read` | world | done |
| `15df:023b` PicCache_LoadBundle | `game/pic_cache.h` `loadBundle` | world | done |
| `15df:0359` PicCache_Release | `game/pic_cache.h` `release` | world | done |
| `15df:03a2` PicCache_ReleaseBundle | `game/pic_cache.h` `release` | world | done |
| `15df:0414` Obj_SetSizeFromPicture | `game/vehicles_common.h` `sizeFromPicture` | vehicles | done |
| `17a0:0004` Mouse_EventHandler | `input/mouse.cpp` `setEventHandler` | wave 2 | done |
| `17a0:009b` Mouse_MoveCursor | `input/mouse.cpp` `GgMouse::moveCursor` | wave 2 | done |
| `17ab:000e` Mouse_Init | `app/test_pattern.cpp` `mouse` | wave 2 | done |
| `17ab:0083` Mouse_LoadCursors | `game/puzzles.h` `loadCursor` | puzzles_a | done |
| `17ab:0140` Mouse_FreeCursor | `gfx/cursor.h` (the cursor owns its picture) | — | absorbed |
| `17ab:017f` Mouse_SetBounds | `gfx/cursor.cpp` `Cursor::setBounds` | wave 2 | done |
| `17ab:01da` Mouse_SetBoundsFull | `gfx/cursor.h` `setBoundsFull` | wave 2 | done |
| `17ab:01f0` Mouse_SetCursorPicture | `gfx/cursor.cpp` `Cursor::setCursorPicture` | wave 2 | done |
| `17ab:0222` Mouse_SetHotSpot | `gfx/cursor.cpp` `Cursor::setHotSpot` | wave 2 | done |
| `17ab:0263` Mouse_RestoreDefaultCursor | `game/workshop.cpp` `cursor`; wave 2: `gfx/cursor.cpp` `Cursor::restoreDefaultCursor` | vehicles | done |
| `17ab:0294` Zones_AddLocal | `input/zones.h` `addLocal` | wave 2 | done |
| `17ab:02ba` Zones_AddGlobal | `input/zones.h` `addGlobal` | wave 2 | done |
| `17ab:02de` Zones_AddLocalArray | `input/zones.h` `addLocalArray` | wave 2 | done |
| `17ab:0314` Zones_RemoveLocal | `input/zones.h` `removeLocal` | wave 2 | done |
| `17ab:03b7` Zones_RemoveGlobal | `input/zones.h` `removeGlobal` | wave 2 | done |
| `17ab:0441` Zones_RemoveLocalArray | `input/zones.h` `removeLocalArray` | wave 2 | done |
| `17ab:0465` Zones_ClearLocal | `input/zones.h` `clearLocal` | wave 2 | done |
| `17ab:04bd` Zones_Push | `game/puzzle_run.cpp` `zones` | puzzles_a | done |
| `17ab:04e8` Zones_Pop | `input/zones.h` `pop` | wave 2 | done |
| `17ab:0507` Zones_HitTest | `input/zones.h` `hitTest` | wave 2 | done |
| `17ab:0612` Mouse_DrawCursor | `gfx/cursor.cpp` `Cursor::draw` | wave 2 | done |
| `17ab:072a` Mouse_RestoreUnder | `gfx/cursor.cpp` `Cursor::restoreUnder` | wave 2 | done |
| `17ab:0754` MouseRegion_Add | `gfx/cursor.cpp` `Cursor::addRegion` | wave 2 | done |
| `17ab:0771` MouseRegion_AddArray | `gfx/cursor.cpp` `Cursor::addRegions` | wave 2 | done |
| `17ab:07a4` MouseRegion_Clear | `gfx/cursor.cpp` `Cursor::clearRegions` | wave 2 | done |
| `17ab:07dd` Mouse_ForceDefault | `gfx/cursor.cpp` `Cursor::forceDefault` | wave 2 | done |
| `17ab:07f4` Zones_RemoveLocalGroup | `input/zones.h` `removeLocalGroup` | wave 2 | done |
| `17ab:083b` Zone_Init | `input/zones.h` `init` | wave 2 | done |
| `17ab:0885` Mouse_Show | `game/race.cpp` `cursor` | vehicles | done |
| `17ab:0890` Mouse_Hide | `game/race.cpp` `cursor` | vehicles | done |
| `198d:0008` Input_HitZones | `input/input_codes.h` `hitZones` | wave 2 | done |
| `198d:0084` KbdRing_Pop | `input/keyboard.h` `pop` | wave 2 | done |
| `198d:00bf` Input_Flush | `input/input_codes.cpp` `ring` | wave 2 | done |
| `198d:00d4` Kbd_PopQueuedKey | `input/keyboard.h` `popQueuedKey` | wave 2 | done |
| `198d:0106` Input_GetCode | `input/input_codes.cpp` `InputCodes::getCode` | wave 2 | done |
| `198d:02a5` Input_WaitCode | `app/gg_runtime.h` `waitInputCode` | wave 2 | done |
| `198d:02b8` Input_GetHeldKeys | `input/input_codes.cpp` `heldKeys` | wave 2 | done |
| `198d:0328` Input_StripHeld | `input/input_codes.h` `stripHeld` | wave 2 | done |
| `19c0:000f` List_Head | std containers in the owners (the lists hold no state of their own) | — | absorbed |
| `19c0:001e` List_Tail | std containers in the owners (the lists hold no state of their own) | — | absorbed |
| `19c0:002e` Node_Next | std containers in the owners (the lists hold no state of their own) | — | absorbed |
| `19c0:003d` Node_Prev | std containers in the owners (the lists hold no state of their own) | — | absorbed |
| `19c0:004d` List_GetAt | std containers in the owners (the lists hold no state of their own) | — | absorbed |
| `19c0:00a6` List_InsertSorted | std containers in the owners (the lists hold no state of their own) | — | absorbed |
| `19c0:0186` List_InsertAt | std containers in the owners (the lists hold no state of their own) | — | absorbed |
| `19c0:0267` List_Remove | std containers in the owners (the lists hold no state of their own) | — | absorbed |
| `19c0:02e1` List_RemoveAt | std containers in the owners (the lists hold no state of their own) | — | absorbed |
| `19c0:03a2` List_Count | std containers in the owners (the lists hold no state of their own) | — | absorbed |
| `19c0:03ea` List_New | std containers in the owners (the lists hold no state of their own) | — | absorbed |
| `19c0:0420` List_Free | std containers in the owners (the lists hold no state of their own) | — | absorbed |
| `19c0:0433` List_Clear | std containers in the owners (the lists hold no state of their own) | — | absorbed |
| `1b48:02aa` Lzh_Decompress | `data/lzh.h` (the decoder; the key drain is flow’s installLzhKeyDrain) | — | absorbed |
| `1d3c:0004` Kbd_BiosGetKey | `input/keyboard.h` `biosGetKey` | wave 2 | done |
| `1d3c:0015` Kbd_BiosPeekKey | `input/keyboard.h` `biosPeekKey` | wave 2 | done |
| `1d3c:0027` Dos_PrintString | the host layer (no DOS video mode, DCC, DOS strings or far pointers) | — | absorbed |
| `1d3c:004d` Ptr_Normalize | the host layer (no DOS video mode, DCC, DOS strings or far pointers) | — | absorbed |
| `2bd4:0000` Rand_Shuffle | `game/parts.cpp` `randShuffle` | world | done |
| `2bd4:00c1` Zone_SetFromObject | Zone_SetFromObject: the owners build the zone from the object (world, vehicles) | — | absorbed |
| `2bd4:024d` Str_FormatThousands | `ui/rank.cpp` `formatThousands` | ui | done |
| `2c08:0000` Video_DccSupported | the host layer (no DOS video mode, DCC, DOS strings or far pointers) | — | absorbed |
| `2c08:0011` Mouse_DriverPresent | `app/options.h` (the mouse is always there; the CPU class is an option) | — | absorbed |
| `2c08:0032` Cpu_Detect | `app/options.h` (the mouse is always there; the CPU class is an option) | — | absorbed |
| `3f50:0000` Math_DivRound | `game/race.cpp` `mathDivRound` | vehicles | done |

#### Sound (engine_sound.tsv)

| Original | Engine | Owner | Status |
|---|---|---|---|
| `1bb4:000e` Snd_GetSlotSong | `audio/snd_driver.cpp` `SndDriver::slotSong` | wave 2 | done |
| `1bb4:002b` Snd_SectionEntry | `audio/snd_driver.cpp` `SndDriver::sectionValid` | wave 2 | done |
| `1bb4:0045` Snd_StartSection | `audio/snd_driver.cpp` `SndDriver::startSection` | wave 2 | done |
| `1bb4:00a9` AdLib_Write | `audio/snd_driver.h` `oplWrite` | wave 2 | done |
| `1bb4:00de` AdLib_InitChip | `audio/snd_driver.cpp` `SndDriver::adlibInitChip` | wave 2 | done |
| `1bb4:0142` AdLib_Detect | `audio/snd_driver.cpp` `SndDriver::adlibDetect` | wave 2 | done |
| `1bb4:0198` Mpu_WriteData | MPU-401 output is not ported (OPL and the speaker are) | — | absorbed |
| `1bb4:01a9` Mpu_WriteCommand | MPU-401 output is not ported (OPL and the speaker are) | — | absorbed |
| `1bb4:01c7` Mpu_WantSendData | MPU-401 output is not ported (OPL and the speaker are) | — | absorbed |
| `1bb4:01db` Mpu_SendSysEx | MPU-401 output is not ported (OPL and the speaker are) | — | absorbed |
| `1bb4:0218` Opl_SetVolume | `audio/snd_driver.cpp` `SndDriver::oplSetVolume` | wave 2 | done |
| `1bb4:0270` Opl_SetPatch | `audio/snd_driver.cpp` `SndDriver::oplSetPatch` | wave 2 | done |
| `1bb4:0302` Opl_NoteOff | `audio/snd_driver.cpp` `SndDriver::oplNoteOff` | wave 2 | done |
| `1bb4:0343` Opl_NoteOn | `audio/snd_driver.cpp` `SndDriver::oplNoteOn` | wave 2 | done |
| `1bb4:041d` Psg_SetVolume | PSG (Tandy) output is dead code in the shipped game | — | absorbed |
| `1bb4:043a` Psg_ChannelOff | PSG (Tandy) output is dead code in the shipped game | — | absorbed |
| `1bb4:0461` Psg_SetPeriod | PSG (Tandy) output is dead code in the shipped game | — | absorbed |
| `1bb4:048f` Psg_SetNoise | PSG (Tandy) output is dead code in the shipped game | — | absorbed |
| `1bb4:04a0` Psg_StepEnvelopes | PSG (Tandy) output is dead code in the shipped game | — | absorbed |
| `1bb4:0541` Spk_PitchBend | `audio/snd_driver.cpp` `SndDriver::spkPitchBend` | wave 2 | done |
| `1bb4:05a3` Psg_NoteOn | PSG (Tandy) output is dead code in the shipped game | — | absorbed |
| `1bb4:05f1` Psg_SetEnvelope | PSG (Tandy) output is dead code in the shipped game | — | absorbed |
| `1bb4:0641` Snd_HwChannelOff | `audio/snd_driver.cpp` `SndDriver::hwChannelOff` | wave 2 | done |
| `1bb4:0674` Snd_HwSilenceAll | `audio/snd_driver.cpp` `SndDriver::hwSilenceAll` | wave 2 | done |
| `1bb4:06bb` Snd_ClassChannelInfo | `audio/snd_driver.cpp` `SndDriver::owner` | wave 2 | done |
| `1bb4:06d9` Snd_ApplyChannelProgram | `audio/snd_driver.cpp` `SndDriver::applyChannelProgram` | wave 2 | done |
| `1bb4:077c` Snd_ReleaseChannels | `audio/snd_driver.cpp` `SndDriver::releaseChannels` | wave 2 | done |
| `1bb4:084a` Snd_Stop | `audio/snd_driver.cpp` `SndDriver::stop` | wave 2 | done |
| `1bb4:087e` Snd_StepSpeakerSweep | `audio/snd_driver.cpp` `SndDriver::stepSweep` | wave 2 | done |
| `1bb4:08ef` Snd_MetaEvent | `audio/snd_driver.cpp` `SndDriver::metaEvent` | wave 2 | done |
| `1bb4:093f` Snd_ReadEvent | `audio/snd_driver.cpp` `SndDriver::readEvent` | wave 2 | done |
| `1bb4:0966` Snd_StepSpeaker | `audio/snd_driver.cpp` `SndDriver::stepSpeaker` | wave 2 | done |
| `1bb4:0a1f` Snd_StepPsg | PSG (Tandy) output is dead code in the shipped game | — | absorbed |
| `1bb4:0ad6` Snd_StepOpl | `audio/snd_driver.cpp` `SndDriver::stepOpl` | wave 2 | done |
| `1bb4:0b7f` Snd_PauseLock | `audio/snd_driver.h` `pauseLock` | wave 2 | done |
| `1bb4:0b90` Snd_ResumeUnlock | `audio/snd_driver.h` `resumeUnlock` | wave 2 | done |
| `1bb4:0b97` Snd_Silence | `audio/snd_driver.h` `silence` | wave 2 | done |
| `1bb4:0bba` Snd_SetLoop | `audio/snd_driver.h` `setLoop` | wave 2 | done |
| `1bb4:0bd1` Snd_GetLoop | `audio/snd_driver.h` `getLoop` | wave 2 | done |
| `1bb4:0be5` Snd_SetCue | `audio/snd_driver.h` `setCue` | wave 2 | done |
| `1bb4:0c0c` Snd_GetTick | `audio/snd_driver.h` `getTick` | wave 2 | done |
| `1bb4:0c1e` Snd_IsActive | `audio/snd_driver.h` `isActive` | wave 2 | done |
| `1bb4:0c34` Snd_GetPriority | `audio/snd_driver.h` `getPriority` | wave 2 | done |
| `1bb4:0c4a` Snd_GetSection | `audio/snd_driver.h` `getSection` | wave 2 | done |
| `1bb4:0c5e` Snd_GetLoopSection | `audio/snd_driver.h` `getLoopSection` | wave 2 | done |
| `1bb4:0c72` Snd_SetSection | `audio/snd_driver.h` `setSection` | wave 2 | done |
| `1bb4:0c91` Snd_SetLoopSection | `audio/snd_driver.h` `setLoopSection` | wave 2 | done |
| `1bb4:0cb0` Snd_GetSectionTranspose | `audio/snd_driver.h` `getSectionTranspose` | wave 2 | done |
| `1bb4:0cd2` Snd_GetSectionNext | `audio/snd_driver.h` `getSectionNext` | wave 2 | done |
| `1bb4:0cf4` Snd_SetSectionNext | `audio/snd_driver.cpp` `SndDriver::setSectionNext` | wave 2 | done |
| `1bb4:0d26` Snd_SetTranspose | `audio/snd_driver.cpp` `SndDriver::setTranspose` | wave 2 | done |
| `1bb4:0d8f` Snd_Free | `audio/snd_driver.cpp` `SndDriver::freeSlot` | wave 2 | done |
| `1bb4:0db3` Snd_Tick | `audio/snd_driver.cpp` `SndDriver::soundTick` | wave 2 | done |
| `1bb4:0e15` Snd_Off | `game/sound_audio.cpp` `GgAudioSound::shutdown` | flow | done |
| `1bb4:0e71` Snd_AllocSlot | `audio/snd_driver.cpp` `SndDriver::allocSlot` | wave 2 | done |
| `1bb4:0ebc` Snd_RegisterSong | `audio/snd_driver.cpp` `SndDriver::registerSong` | wave 2 | done |
| `1bb4:0eec` Snd_StopOwnerChannel | `audio/snd_driver.cpp` `SndDriver::stopOwnerChannel` | wave 2 | done |
| `1bb4:0f01` Snd_MuteChannel | `audio/snd_driver.cpp` `SndDriver::muteChannel` | wave 2 | done |
| `1bb4:0f50` Snd_UnmuteChannel | `audio/snd_driver.cpp` `SndDriver::unmuteChannel` | wave 2 | done |
| `1bb4:0f70` Snd_ScaleDelay | dead or without caller (`audio/snd_driver.h`) | — | absorbed |
| `1bb4:0fb2` Snd_ScaleSectionTempo | dead or without caller (`audio/snd_driver.h`) | — | absorbed |
| `1bb4:102c` Snd_SetPriority | `audio/snd_driver.cpp` `SndDriver::setPriority` | wave 2 | done |
| `1bb4:10c1` Snd_Play | `audio/snd_driver.cpp` `SndDriver::play` | wave 2 | done |
| `1bb4:11a2` Snd_SetMasterVolume | dead or without caller (`audio/snd_driver.h`) | — | absorbed |
| `1bb4:11df` Snd_SetChannelVolume | dead or without caller (`audio/snd_driver.h`) | — | absorbed |
| `1bb4:1225` Snd_GetChannelVolume | dead or without caller (`audio/snd_driver.h`) | — | absorbed |
| `1bb4:1234` Snd_Init | `audio/snd_driver.cpp` `SndDriver::init` | wave 2 | done |
| `1cdd:0004` SndRes_Init | `audio/snd_res.cpp` `SndRes::init` | wave 2 | done |
| `1cdd:0024` SndRes_LoadMusicBank | `game/sound_audio.cpp` `GgAudioSound::loadMusicBank` | flow | done |
| `1cdd:012d` SndRes_LoadEffectBank | `game/sound_audio.cpp` `GgAudioSound::loadEffectBank` | flow | done |
| `1cdd:01e0` SndRes_LoadSong | `game/sound_audio.cpp` `GgAudioSound::loadSong` | flow | done |
| `1cdd:0237` SndRes_FreeMusicBank | `game/sound_audio.cpp` `GgAudioSound::freeMusicBank` | flow | done |
| `1cdd:027d` SndRes_FreeEffectBank | `game/sound_audio.cpp` `GgAudioSound::freeEffectBank` | flow | done |
| `1cdd:02e3` SndRes_FreeSong | `game/sound_audio.cpp` `GgAudioSound::freeSong` | flow | done |
| `1cdd:031a` SndRes_PlayMusic | `game/sound_audio.cpp` `GgAudioSound::playMusic` | flow | done |
| `1cdd:03c9` SndRes_PlayEffect | `game/sound_audio.cpp` `GgAudioSound::playEffect` | flow | done |
| `1cdd:03ef` SndRes_StopMusic | `game/sound_audio.cpp` `GgAudioSound::stopMusic` | flow | done |
| `1cdd:0409` SndRes_StopEffect | `game/sound_audio.cpp` `GgAudioSound::stopEffect` | flow | done |
| `1cdd:0423` SndRes_SelectArrangement | `game/sound_audio.cpp` `GgAudioSound::selectArrangement` | flow | done |
| `1cdd:04ef` SndRes_MuteChannel | `game/sound_audio.cpp` `GgAudioSound::muteChannel` | flow | done |
| `1cdd:0508` SndRes_UnmuteChannel | `game/sound_audio.cpp` `GgAudioSound::unmuteChannel` | flow | done |
| `1cdd:0521` SndRes_StopAllEffects | `game/sound_audio.cpp` `GgAudioSound::stopAllEffects` | flow | done |
| `1cdd:0551` SndRes_GetSlotB | `audio/snd_res.h` `slot` | wave 2 | done |
| `1cdd:055f` SndRes_GetSlot | `audio/snd_res.h` `slot` | wave 2 | done |
| `1cdd:056d` SndRes_IsPlaying | `game/sound_audio.cpp` `GgAudioSound::isPlaying` | flow | done |
| `1cdd:0592` SndRes_ApplySoundOption | `audio/snd_res.cpp` `SndRes::applySoundOption` | wave 2 | done |
| `1cdd:05c8` Snd_ToggleOption | `app/preview.cpp` `toggle` | flow | done |
| `1cdd:05db` Snd_Pause | `game/sound_audio.cpp` `GgAudioSound::pause` | flow | done |
| `1cdd:05e5` Snd_Resume | `game/sound_audio.cpp` `GgAudioSound::resume` | flow | done |
| `1cdd:05ef` SndRes_Nop | an empty function (the sign-in calls it): nothing to port | — | absorbed |

---

## 11. Wave 3 results: the game played through the app (integration)

Run in the integration build (`build_gg3_integration`, MSVC, Release) through the app:
`ggport-cli --headless` (deterministic virtual time, the real sound facade over GgAudio,
`PLAYERS.DAT` in a scratch preference folder) with scripted keys and mouse in PIT ticks,
`--gg-trace`, `--frame-dump` and `--gg-dump-state`, from a fresh boot with the w0 tour's seed 0x29CF
and from the round 2 states with `--gg-load-state`. The same runs are the ggtests cases of
`tests/gg/test_gg_playthrough.cpp`, which compare every picture with the captures as it is shown.
Scratch scripts: `extracted/scratch/gg/wave3/integration/` (`e2e3.py`, `tour.py`, `seg.py`,
`puzzles_e2e.py`, `resume_e2e.py`, `build_e2e.py`).

### 11.1 Stages

| Stage | Input (PIT ticks) | Result |
|---|---|---|
| boot, the attract, the story pages | the w0 seed | w0/01, 02 at 0 px |
| sign-in of a new player With Chimps | Space on story page 3 (12000, as w0), "ada", Start Game (164, 180), With Chimps (93, 137) | w0/03, 04, 05 at 0 px; `PLAYERS.DAT` = `w0/06_hub.players` (the 8 bytes of slot 98) |
| the hub | the pointer on the Aircraft building | w0/06, 07 at 0 px; the DGROUP equals w0/06_hub's in every modelled field but the clock, the timers, the dialog freeze's tick, the roster scroll bar's tick stamp and the key ring's indexes |
| the Aircraft building: hint, blueprint, part page, Done, workshop hint, workshop | (62, 85), Enter, (53, 51), Done twice, Enter | w0/08..12 at 0 px; the set-up's 3925 rand() calls (code_world §4.4) |
| the tube, the warehouse hint, room 1 with two chimps, right to the X door | Up, Enter, right 2 s | w0/13 at 25 px, 14 and 15 with the chimps elsewhere (w0's walked at the host's pace) |
| a puzzle | Up 280 ticks later than w0: the chimps' rand() calls deal w0's machine pieces variant 2 | w0/16, 17 at 0 px; capture_tour.py's T-handle solution: w0/18, 19, 20 at 0 px |
| the room behind the door, a part hovered, File > Return to Technology Center > Yes | Enter, (290, 128), (14, 4), (107, 27), (221, 133) | w0/21..23 at 0 px outside the chimp's strip, w0/24, 25 at 0 px; `PLAYERS.DAT` = w0/25's but 19 bytes of the two chimps' records in the Aircraft block |
| a banana thrown in room 1 | Space | sequence 23 (throw right), `g_projectiles` 10 → 9, the HUD counter 009 |
| a part picked up (r2/aircraft/17) | right through two floor gaps, Down over the box at (235, 150) | sequence 47 (crouch and pick up); the HUD (strip, counters, score) = r2/aircraft/18 at 0 px outside the room |
| the eight puzzles from their door states (r2/puzzle/kK_door, Up held) | Enter at the hint, capture_r2.py's solutions (gears: the 21 steps of the solved state, code_puzzles_b §8), Enter | kK_00_hint, kK_01_screen and the solved shot at 0 px for every kind; through the opened door into room 6 |
| the eight puzzles resumed in their screen states | the same solutions | the solved shots at 0 px, through the door (after the integration's fixes, §11.2) |
| a puzzle left through the menu (r2/puzzle/k0_screen) | File > Return to Technology Center > Yes | state 16 saves the building, `g_leaveRequest` 3, the hub |
| the tube with all parts (r2/aircraft/29) | Up | r2/aircraft/30 (the parts hint) at 0 px |
| the workshop (r2/aircraft/36) | capture_r2.py workshop_build: the engine off, onto the belt, the belt moved, fitted again; the race door | 37, 38 at 0 px; the race starts (state 11) |
| the workshop's belt (r2/aircraft/31) | arrows, a part carried and dropped, the race door with the vehicle incomplete | the belt scrolls, parts are carried and dropped; the "not completely built" hint |
| the races to the result, the rank-up box and the post-race menu | capture_r2.py race(): the race door (305, 135), Enter at the box | automotive 12, 13, 14; Alternative Energy 12, 13, 14; Aircraft won 56, 57; Aircraft lost 40, 41, 43: all at 0 px |
| the post-race menu's ways on | Next Level (93, 89), Tech. Center (221, 89), the warehouse after a loss (157, 89) | the level-2 blueprint r2/altenergy/15 at 0 px outside the pointer; the hub; the workshop again |
| the 15th race, the rank-up, the final movie, a key (r2/movie/00) | the race door, Enter, a key at 27000 | r2/movie/01..06 at 0 px; exit(0) at the key; `PLAYERS.DAT` = `07_after_key.players` byte for byte |
| Save & Exit in a building (r2/aircraft/26) | File, Save & Exit, y | r2/resume/02, 03 at 0 px; exit(0); `PLAYERS.DAT` = `04_after_save_exit.players` (the 295 bytes the original wrote) |
| the resume in a new boot with that file | Space, Bob (100, 62), Start Game, the Aircraft building | r2/resume/05, 06, 07 at 0 px; room 4 at Bob's x 185 with the 3920 discarded rand() calls; 08 at 0 px once the idle bit has turned him to the front (REFERENCE_CAPTURES §9.5) |
| a windowed run (SDL offscreen video, dummy audio, real time) | a new player, the hub, the Aircraft building | the workshop hint after 3500 ticks (24 s), exit 0 |

### 11.2 What the integration fixed

* **The stale cursor of a loaded state:** `GgStateFile::apply` now gives the active cursor copy the
  state's save-under and the hide state (§10.6); before, the arrow drawn in the state's RAM screen
  stayed behind when the pointer moved (48 px in r2/resume/02 and 03).
* **Resumed puzzles:** the part-A kinds' restores load their effect bank (without it
  `PzSnd_WaitEnd` after the solve waited forever on the room's looping music, so the game never left
  state 9 in the app), and `loadGgState` puts the state's palette and DAC back after the modules'
  restores (their picture loads had applied the actors' and rooms' palette members: the resumed
  puzzles' solved screens had the wrong colours) and the picture cache's records.
* `g_dlgTitleY` after a box without a title (code_ui §5.2); `g_picCache` in the whole-game DGROUP
  dump; the trace CSV's sound column quoted; `g_elecWiresLost` folded into `g_elecToolCounts`; the
  sound schedules' priorities (fmt_sound §5, the 13 `.dro` checks unchanged); `Cursor::activeCopy`
  for the code that writes the active copy directly; the tests that only reported a missing module
  now check strictly.

### 11.3 Builds and checks

* `ggtests` 284 of 284 (541 601 assertions), `tmtests` 639 of 639 (4 459 468 assertions, as before
  wave 3: Treasure Mountain is unchanged); `python -P tools/check_core_deps.py` and `--gg`: clean.
* MSVC /W4: no warning; Clang diagnostics (`clang-tidy` with `clang-diagnostic-*` over the compile
  database, 413 project files): none; GCC 15 under WSL 2 (`ggcore`, `ggport`, `ggtests`): no warning
  in the project's code, and `ggtests` passes there too: 284 of 284 with the user's files and the
  captures (541 601 assertions, as on Windows: the playthroughs give the same pictures and files on
  Linux).
