# Gizmos & Gadgets: engine map (wave 2, the foundation)

How the original program (`ssg.exe`, DOS v1.0) maps onto the engine's `src/gg/`: which engine file
ports which original module, the specs it follows, what it does now and what is still missing, how
the modules sit on the shared core, and the results of the wave 2 stop/go checks (SCOPING §9). Read
with `SCOPING.md`, `ARCHITECTURE.md`, `PORTING_NOTES.md` and the topic specs; the specs say *what*
each routine does, this map says *where* it is. Model: `re/dos/docs/ENGINE_MAP.md` (TM DOS).

**Status after wave 2 (integration):** the shared core `portcore` is extracted from tmport and both
Treasure Mountain editions are unchanged byte for byte (§7.5). Over it, `ggcore` holds the G&G data
layer, graphics, text, sound, input, runtime and app shell, the TLC UI library's drawing and state,
and the drawing halves of the world (hub, rooms, HUD, blueprint, the eight puzzle screens). Every
tool of every area runs from `ggport-cli`; `ggport` runs the **wave 2 preview** (§5). Every stop/go
check passed (§7): 63 screens composed by the tools and 15 moments of the preview at 0 differing
pixels against the DOSBox-X captures, the attract's fades frame-exact, 12 of 13 OPL captures write
for write. The game logic (input loops, walking, chimps, puzzle play, workshop, races) is wave 3's
(§9).

Sizes: about 24 800 lines in `src/gg/` (data 3 300, save 800, gfx 2 900, text 300, audio 3 600,
input 1 100, app 3 300, ui 3 800, game 5 600) and 5 500 lines of tests in `tests/gg/`.

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
   game     PicCache, ScreenAnim, actors, parts, rooms, HUD, Building, BlueprintView, HubScreen, puzzles
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
half is done, the logic around it is wave 3's; **stand-in** = not the original's code path.

### 2.1 Data and saves

| Original | Engine | Specs | Status, gaps |
|---|---|---|---|
| `ssg.exe` image, DGROUP, VROOMM overlays, stubs | `gg/data/gg_exe.{h,cpp}` `GgExe` | fmt_data §1, ARCHITECTURE §1 | done; refuses other versions (size, MZ, TLINK, copyright, FBOV, 35 stubs) |
| game folder | `gg/data/gg_files.{h,cpp}` `GgFiles`, `detectGgFolder` | PORTING_NOTES §1 | done; names a missing or broken file; ignores `gizmo004.dat` |
| archives 14ff, LZH 1b48 | `gg/data/archive`, `gg/data/lzh` | fmt_data §2-§4 | done; all 1897 entries equal `ggarc.py`; the decoder rejects broken streams (the original would hang); the LZH refill's key drain is not modelled (§8) |
| bundles, LPX/palette/font/DAM/BMF/text/table parsers | `gg/data/resources` | fmt_data §5-§7, fmt_graphics | done; `classify` equals `ggarc.py` |
| DGROUP labels | `gg/data/dgroup_offsets.h` | `re/names/gg/dgroup.tsv` | 626 labels (wave 2 added 9, moved `g_animChannels`) |
| tool helpers | `gg/data/cli_args` | — | the shared option, folder and output guard of every `<area>Cli` |
| `PLAYERS.DAT` 2b5a, 2caa:004f | `gg/save/players_dat`, `players_store` | code_ui §12, fmt_data §8 | done byte for byte; the file lives in the pref dir (`playersPathIn`), atomic writes with `.bak`, import / export; the write moments are wave 3's callers |

### 2.2 Graphics and text

| Original | Engine | Specs | Status, gaps |
|---|---|---|---|
| primitives 1399, screen helpers 1473 | `gg/gfx/screen` `Screen` | code_runtime §10 | done, with the original's clipping, marks and quirks; writes outside the buffer and zero counts are skipped and logged |
| LPX blitters 1399 | `gg/gfx/lpx` | fmt_graphics §2, code_runtime §10.4 | done; 2959 pictures decode as `lpx.py` |
| dirty tiles, sprites 1581 | `gg/gfx/screen` `DirtyMap`, `gg/gfx/sprites` | code_runtime §11 | done, incl. the `Sprites_EraseAll` bit-7 quirk |
| cursor and regions 17ab | `gg/gfx/cursor` | code_runtime §12 | done |
| palette, DAC, fades 1399/1473 | `gg/gfx/palette` | code_runtime §13 | done; DAC shown as `(v << 2) \| (v >> 4)` |
| wipes 1473:0042/0479, Screen_Show | `gg/gfx/transitions` | code_runtime §14 | done; the door wipe is 0x11DB frame by frame |
| movie 1399:0a85, 1473:07d9 | `gg/gfx/movie` | code_runtime §15, fmt_graphics §7 | done (stepper; 202 frames equal `movie.py`); the final movie state is wave 3's |
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
| Kbd_ISR, key ring, BIOS keys 1454/198d/1d3c | `gg/input/keyboard` | code_runtime §6 | done on the core `ScanKeyboard`; E0/E1 swallowed; the LZH refill's drain is not called yet |
| mouse handler 17a0 | `gg/input/mouse` `GgMouse` | code_runtime §7 | done on the core `Int33Mouse`; moves dropped while drawing; the puzzles' pointer warp does not move the host pointer (§8) |
| zones 17ab | `gg/input/zones` `ClickZones` | code_runtime §8 | done |
| Input_GetCode 198d | `gg/input/input_codes` `InputCodes` | code_runtime §9 | done; the global keys call hooks the game installs |
| the program's waits | `gg/app/gg_runtime` `GgRuntime` | PORTING_NOTES §2 | done: yield points pump the host; deterministic headless runs; scripted input in PIT ticks |
| Seq_Run 12e0:000f, exit | `gg/app/seq_run` | code_runtime §3, §4 | done |
| main, Init_CommandLine | `gg/app/gg_app`, `options`, `settings`, `icon` | code_runtime §2, PORTING_NOTES §7 | done; `--gg-args` takes the original switches; ggport.ini in the pref dir |

### 2.5 UI library and attract

| Original | Engine | Specs | Status, gaps |
|---|---|---|---|
| dialogs 3f56, buttons 4034, Yes/No 4106 | `gg/ui/dialog`, `button`, `menu_commands` | code_ui §5, §6.5 | drawing, layout, zones; `Dlg_RunModal` (time freeze, `Snd_Pause`) is wave 3's |
| scroll bar 406a | `gg/ui/scroll_bar` | code_ui §9 | done (click, auto-repeat, drag steps) |
| menu bar 40a4 | `gg/ui/menu_bar` | code_ui §6 | drawing and zones; `Menu_RunDropDown` is wave 3's |
| help 411d | `gg/ui/help` | code_ui §6.6 | pages; `Dlg_RunPager` is wave 3's |
| text viewer 4140 | `gg/ui/text_viewer` | code_ui §8 | drawing and scrolling; `TextView_Run` is wave 3's |
| hints 3060 | `gg/ui/hints` | code_ui §13 | box and queue; `Hint_Show`'s modal run is wave 3's |
| attract 2c11 | `gg/ui/attract`, `attract_anims` | code_ui §10 | every step and `Attract_PlayAnim` as a stepper on the world's `ScreenAnim`; the state loop runs in the preview |
| sign-in 2caa | `gg/ui/signin` | code_ui §11 | screen, rows, typing, buttons; `SignIn_Run` and the chimps question are wave 3's (the preview runs a simplified loop) |
| ranks 2bd4 | `gg/ui/rank` | code_ui §11.2 | done |

### 2.6 World (drawing halves)

| Original | Engine | Specs | Status, gaps |
|---|---|---|---|
| picture cache 15df | `gg/game/pic_cache` | code_runtime §16.4 | done without the 300-entry table and its slot numbers |
| screen animations 174b | `gg/game/screen_anim` | code_world §19.1 | done (every op) |
| actors 1834 | `gg/game/actors` | code_world §9 | sets, frames and drawing; the step interpreter and brains are wave 3's |
| parts 153f | `gg/game/parts` | code_vehicles_races §2 | catalogue and deck (rand() calls exact) |
| rooms 1622 | `gg/game/rooms` | code_world §5-§8 | loading (3920 rand() calls from the w0 seed, the w0 deal), entry, layers, items, save block; `Room_Leave` not ported |
| HUD 1a04 | `gg/game/hud` | code_vehicles_races §4 | drawing; the strip commands are wave 3's |
| building, states 4/5/7 drawing | `gg/game/building` | code_world §4, §14 | set-up and frames; `Building_NewVisit`, `Chimps_Spawn`, the actor tick are wave 3's |
| hub 12e0:0148 | `gg/game/hub` | code_world §3 | set-up and frame; the input dispatch runs in the preview |
| blueprint 2dee | `gg/game/blueprint` | code_vehicles_races §6 | viewer drawing; `Blueprint_Run` and the workshop are wave 3's |
| puzzles 38e2 and the 8 overlays | `gg/game/puzzles`, `pz_*` | code_puzzles_a, code_puzzles_b | set-up and first screen of every kind and variant; the input loops are wave 3's |

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

Every command takes `--data DIR` and refuses to write into the game folder. The world tool answers
`--gg-screen` for its four screens, the UI tool for every other name.

---

## 5. The wave 2 preview (`gg/app/preview.{h,cpp}`)

`ggport` without a tool command runs the preview (marked "Wave 2 preview: replaced by the game
modules in wave 3"):

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

144 test cases: data (archives, LZH, resources, `PLAYERS.DAT` round trips and write moments), gfx
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
("integration: the preview through every screen against the captures").

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

## 8. Known gaps and deviations (after wave 2)

* **Frame costs.** The work between yield points costs no time, so screen changes come 3-5 video
  frames early against the captures (§7.3) and the long-frame model of PORTING_NOTES §2.4 is not
  implemented; in real time, `g_ticks` read between yield points is stale by the work time.
* **Not modelled yet:** the LZH refill's key drain (`GgKeyboard::drainQueuedKeys` exists, nothing
  calls it per 4000-byte read); the races' one key per frame; the puzzles' pointer warp on the host
  pointer (`GgMouse::onWarp` is unset); `PicCache`'s 300-entry table and slot numbers; `Room_Leave`.
* **Deviations by design:** archives with a broken end marker or another version are refused; the
  LZH decoder rejects broken streams; guards where the original reads or writes past arrays
  (hint slot 5, the 17th typed byte, unbounded instance searches, zero counts); a 64-bit deadline
  instead of the u32 wrap after 341 days; the `-?` switch exits 0; saves are atomic with `.bak`.
* **Unverified against a capture:** the help pager's later pages, the Ctrl+R / duplicate / list-full
  boxes, a long roster's scroll bar, a pressed picture button, after-story, before-prompt and the
  prompt; the movie's last 1 270 OPL writes; ten off-grid writes of the Alternative Energy race
  capture and a 3.5-s silent stretch of the `-si` WAV.
* **Preview only:** the sign-in and hub loops are simplified (no dialogs, nothing saved), the
  warehouse has no actors, the puzzles show their first screen; all of it is replaced in wave 3.

---

## 9. Wave 3: the game logic

The plan follows SCOPING §9 and the wave 1 specs; each item builds on the wave 2 interfaces above.

1. **States and loops** (`Seq_Run` table DS:00c2): attract (the preview's loop moves into
   `State_01`), sign-in with `SignIn_Run` and the chimps question, `PlayersStore` saves at the
   original's moments; hub dispatch; `Dlg_RunModal` (time freeze through `GgClock::setTicks`,
   `Snd_Pause`, the menu hook), `Menu_RunDropDown`, `Dlg_RunPager`, `Hint_Show` / `Hint_Update`,
   `TextView_Run`.
2. **Warehouse:** `Building_NewVisit`, `Chimps_Spawn` (incl. the 286 path), the actor step
   interpreter and brains (player, projectile, chimps), collisions and contacts, items, doors and the
   room transitions, the HUD strip commands, the warehouse loop with its rand() order; `Room_Leave`.
3. **Puzzles:** the choice (rand()), history and reward; the eight input loops on the `Pz*` classes
   with their zones, PzSnd calls and hint viewers.
4. **Workshop, races, results, movie:** the workshop screen and `Blueprint_Run`, the two race
   engines, the results count-up and rank message, the post-race menu, the final movie state.
5. **Runtime details:** the LZH key drain, the races' key pop, the pointer warp, per-operation frame
   costs from a frame logger (fidelity wave), whether `ggport.ini` exposes the move-drop rule.
6. **Fidelity:** a scenario harness like TM DOS's `tools/dos/fidelity.py` over the r2 states and
   AVIs, reusing the wave 2 scratch comparisons.
