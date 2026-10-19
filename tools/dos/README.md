# Tools for the 1990 DOS edition

Python tools for the DOS edition (`SST.EXE` V1.0). They read the user's own files at run time
(`TM-DOS\`, the DOSBox-X reference captures in `extracted\captures\dos\`) and contain no game data
or text. Run them with `python -P` (keeps the current directory off `sys.path` but sees the user
site-packages, where Pillow is installed). The reference material is described in
`re/dos/docs/REFERENCE_CAPTURES.md`, the engine in `re/dos/docs/ENGINE_MAP.md`.

Common environment: `TM_CLI` (the engine's `tmport-cli.exe`; default `build\tmport-cli.exe`),
`TM_DATA_DOS` (the DOS folder; default `TM-DOS`), `TM_DOS_CAPTURES` (the captures; default
`extracted\captures\dos`). Every engine run of these tools passes `--headless`, `--pref-dir` (a
scratch folder) and SDL's offscreen/dummy drivers: no window, no sound, nothing in the real
preference folder.

## Decoders (wave 0-1)

| Tool | What |
|---|---|
| `pan.py`, `pak.py` | PAN tile pictures and PAK sprite packs to PNG |
| `dos_font.py`, `dos_text.py` | the 8x8 font in SST.EXE, text drawn like the game |
| `dos_gfx_common.py` | EXEPACK-free loading of SST.EXE, the four looks' palettes, picture helpers |
| `puzzles.py`, `users_dos.py` | ALL_PUZS / PRG_DICT, USERS.DAT |
| `dos_sound.py` | SFX/TUNES renders (speaker, Tandy) |

## DOSBox-X save states and frame logs

* `savmem.py` reads a DOSBox-X state (`.sav`, a zip: `Memory` = guest RAM after a 0x88-byte header,
  `Vga` = the video memory, `CPU` = the registers, `Machine_Type`). Commands: `vars`, `where`,
  `peek`, `far`, `rec`, `dgroup STATE OUT` (DS:0000..CC1F, the layout of the engine's
  `--dos-dump-state`), `screen STATE OUT.png` (the picture DOSBox-X showed, from the video memory),
  `poke`/`pokew` (crafted states). Library: `State` (`.d/.w/.b` DGROUP reads, `.screen()`,
  `.cpu()`, `.first_tick_ticks()`). The engine's reader is `src/dos/save/dosbox_state.h`.
* `framelog.py install STATE OUT` patches a frame and logic-step logger into a state (the r2 `fl_*`
  states carry it); `read`/`steps` print its rings. `stepchanges.py STATE` prints the ticks where the
  player, the camera, the keys or the coins changed.
* `fl2csv.py` turns a log into the engine trace's CSV layout (`csv`), compares a log with an engine
  trace (`cmp LOG TRACE --since START`: by g_ticks, `--align ticks`, or as a sequence of executed
  ticks, `--align seq`, with the lost ticks listed) and derives a starting `--keys` script from a
  log's g_keys (`keys`).

The phase of a loaded state: the next game tick comes DS:3D34 + 1 PIT ticks after the state, one
less when DOSBox-X stopped the CPU inside the timer interrupt before its countdown (Timer_ISR
1000:042f..046a, or Sound_Tick / the BIOS clock below it); `State.first_tick_ticks()` and the
engine's loader both read that from the `CPU` entry.

## Engine runs

* `play.py` / `stages.py` / `pt_full.py`: the headless playthrough driver of wave 3. A `Player`
  reruns the engine with the script so far and places the next key where the trace shows a
  condition (`hold_until`, `wait_for`); `stages.py` has the stages on the layout of seed 0x1EE2,
  `pt_full.py` the complete new-player game (`scenarios/level1/pt_full_script.txt`). Runs go to
  `TM_DOS_PLAY_SCRATCH` (default `extracted\scratch\dos_w4\play`).
* `imgcmp.py`: pixel differences between engine frames and captures (640x400 2x-doubled captures
  are reduced to 320x200), masks, side-by-side diff pictures, contact sheets (`--montage`).
* `audiocmp.py`: the engine's sound (`--frame-dump DIR` writes `DIR/sound.wav`) against the PCM track
  of a DOSBox-X AVI or a WAV: alignment by pitch features, the alignment tracked over time, sound
  on/off edges per PIT tick (wave 4, `re/dos/docs/fmt_sound.md` §11.6).
* `avifit.py`: an engine run against a DOSBox-X AVI. The run dumps the picture of every game tick of a
  range; every AVI frame of a range is looked up among them by exact picture, and the AVI's time base
  (tick = a + frame * k) is fitted; it reports the frames off it. EGA AVIs run at 60.29 frames per
  emulated second (frames against their audio track), not the 59.92 of their header; Tandy AVIs at
  59.92. `avifit.py SCENARIO.json` runs a scenario's `avi_fits` entries (avi, frames, ticks, masks,
  max_off, max_dev; exit 1 when a fit has more frames off or a larger deviation), `avifit.py --frames
  DIR --avi AVI --range A B` fits dumps already made (wave 4, castle_end).

## Fidelity runner

```
python -P tools/dos/fidelity.py [GROUP ...] [--scenario NAME ...] [--cli EXE] [--jobs N]
                                [--out DIR] [--baseline FILE] [--update-baseline] [--no-avi] [--list]
```

Runs the scenarios of `scenarios/<group>/*.json` (groups `ui`, `level1`, `levels23`, `castle_end`,
`looks`) through `tmport-cli` headless, compares each checkpoint's picture with its capture (or with
the screen of a DOSBox-X state), compares the trace with the r2 frame logs, checks timings, and
writes `report.md` (summary), `<group>/report.md`, `<group>/report.csv` (pixels per checkpoint,
ticks per log) and `<group>/montage.png` (engine | capture | differences) under
`extracted\scratch\dos_w4\report\`. Exit code 1 on a regression against `scenarios/baseline.json`
(more pixels or mismatching ticks than stored, a timing out of tolerance, an AVI fit with more
frames off its time base than allowed, a failed run); `--update-baseline` stores the current numbers
(for the scenarios run). The whole run (164 scenarios, with the AVI fits) takes about half a minute
with `--jobs 10`; `--no-avi` skips the AVI fits.

A scenario starts from the boot (`"start": {"boot": true, "seed": "0x1EE2"}`, optionally with a
roster file of the captures) or from a DOSBox-X state (`"start": {"state": "savestates/..."}`, the
engine's `--dos-load-state`), runs a `--keys` script and has checkpoints (a tick, a trace event or
the moment a log's end state was saved; a trace event is matched as a whole `;`-separated token),
logs, timings and AVI fits (`avi_fits`: `avifit.py`); the format is in `fidelity.py`'s header. A
checkpoint's reference is a capture, a state's screen (`capture_state`) or a frame of a reference AVI
(`"avi"`, `"frame"`). Masks carry a reason; a checkpoint with `"info"` compares with a reference of
another moment (a capture taken before its state was saved, another route) and is counted apart in
the summary. Scenario files hold only numbers, file names and key scripts. The pictures are what the
engine shows after the tick: the picture of a frame longer than a game tick (a bubble, a scene load,
a title page) appears when that frame ends, as on the reference machine, so a checkpoint right after
such a change sits a few ticks later than the logic event.

The seed scenarios:

* `state_*`: every DOSBox-X state whose screen matches a capture (`seed_scenarios.py match` /
  `states`): the loaded moment (tick 0: the state's own screen; 0 px = saved at that very frame) and
  the first engine frame against the capture.
* `log_*`: the r2 frame logs replayed from the states they started at, with the end state's screen
  as the last checkpoint (`at_log_end` + `capture_state`).
* `boot_*`, `menus_*`, `help_*`, `signin_*`: the title, sign-in, menus and help pages; the boot
  playthrough; the Tandy, MCGA and CGA looks; the attract page timing.
* `looks/` (wave 4): every `tandy/`, `mcga/` and `cga/` capture through the game (boots with the
  capture runs' rosters, the Tandy states with searched key ticks), the Round-3 CGA/MCGA UI frames
  (`boot_r3_cga`, `boot_r3_mcga`) and the frames of the Round-3 Tandy AVI at the ticks of their time
  (`boot_r3_tandy_session`). Generated by `extracted/scratch/dos_w4_looks/gen_looks.py`.
* `castle_end/` (wave 4): the castle, the end sequences (two found, all found, the crown), the end
  messages, the castle creature, the treasure room from the clubhouse and the prize rooms from the
  reference states, each capture at a tick inside its still picture; `timings` from the AVIs' frames
  and `avi_fits` (run with `avifit.py`). Generated by `extracted/scratch/dos_w4_castle_end/
  make_scenarios.py`.

`seed_scenarios.py` regenerates the state scenarios (`match`, `states`) and finds playthrough ticks
for captures (`boot`, `positions`).
