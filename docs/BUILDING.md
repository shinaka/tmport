# Building and running the engine

The engine (`tmport`) is C++20 with CMake. It contains no game data: at run time it reads the
user's own copy of *Super Solvers Treasure Mountain!* (Windows CD edition), either the
`TMTWINCD` folder or the CD image. For players, `README.md` describes the game's controls,
saves and settings; this document is the reference for building, running and the tools.

## Requirements

* CMake 3.24 or newer and a C++20 compiler. Windows: Visual Studio 2026 (MSVC, with the bundled
  CMake and Ninja). Linux: GCC (built and tested with GCC 15 on Ubuntu under WSL 2) or Clang, plus
  the development packages SDL needs (see "Linux and macOS"). macOS: Xcode's Clang (builds and
  plays both editions, packaged with `scripts/package.sh`; the CI workflow also builds it, but has
  not been run yet).
* Network access on the first configure. CMake downloads, pinned by URL and SHA-256:
  * SDL 3.4.18 (zlib license), built as a static library;
  * doctest 2.4.12 (MIT), header-only, for the unit tests only;
  * TinySoundFont (MIT, `tsf.h`, pinned to commit `853a0a1`), header-only: the software
    synthesizer of the SoundFont MIDI output (see "Music" below);
  * ymfm (BSD-3-Clause, pinned to commit `81aec25`): its YM3812 (OPL2) emulator plays the DOS
    edition's AdLib music; only its OPL sources are compiled (a static library of their own).

## Windows

```
scripts\build.bat
```

This runs `vcvars64.bat`, configures a Release build with Ninja into `build\` and builds:

* `build\tmport.exe`, the engine and its command-line tools, as a **windowed** program: a
  double-click opens only the game window, no console. Started from a console (cmd, PowerShell,
  Windows Terminal) it attaches to that console, so the tools and `--headless` runs print there;
  output redirected to a file or a pipe (`> out.txt`, `| more`, Git Bash) works as usual. Note that
  cmd and PowerShell do not wait for a windowed program: the prompt comes back at once and
  `%ERRORLEVEL%` / `$LASTEXITCODE` is not its exit code (use `start /wait` or `tmport-cli.exe`);
* `build\tmport-cli.exe`, the same program as a console application, for scripts: the shell waits
  for it and sees its exit code;
* `build\tmtests.exe`, the unit tests (a console program).

The engine itself is two static libraries: `portcore` (the core both games share: utilities, data
sources, clocks, the SDL host and presenter, the DOS sound chips and input models, the text engine;
`cmake/portcore.cmake`) and `tmcore` (Treasure Mountain, linking `portcore`). With
`-DTM_BUILD_GG=ON` (the default once `cmake/gg.cmake` exists) the build also makes the second game,
*Gizmos & Gadgets!*: `ggcore` (linking only `portcore`), `ggport.exe`, `ggport-cli.exe` and
`ggtests.exe`; `-DTM_BUILD_GG=OFF` builds Treasure Mountain alone.

If Visual Studio is not installed in the default place, set `VSROOT` first, e.g.
`set VSROOT=C:\Program Files\Microsoft Visual Studio\18\Community`. Extra arguments are passed on
to the CMake configure step: `scripts\build.bat -DTM_BUILD_TESTS=OFF` skips the tests. Set
`BUILD_DIR` to build somewhere other than `build\`.

## Linux and macOS

`CMakePresets.json` has `release` (Ninja, `build/`), `debug` (Ninja, `build-debug/`) and `ci`
(the platform's default generator, `build-ci/`) presets:

```
cmake --preset release
cmake --build --preset release
ctest --preset release                 (or: build/tmtests)
```

or without presets: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`.
There `tmport` is an ordinary terminal program (no `tmport-cli`).

* **Linux**: SDL is built from source and needs the development packages of the window systems
  and audio; on Debian/Ubuntu: `libasound2-dev libpulse-dev libx11-dev libxext-dev libxrandr-dev
  libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev
  wayland-protocols libdecor-0-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev
  libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libudev-dev` (the list CI installs). The whole
  tree (both editions, the tools and the tests) builds with GCC 15 without a warning in the
  project's code, and every unit test passes, also with `TM_DATA`, `TM_DATA_DOS` and
  `TM_DOS_CAPTURES` pointing at the game files and the DOS reference material (file names are matched
  case-insensitively, so the CD's upper-case names work); the DOS autoplay soak gives the same game
  ticks per seed as on Windows (checked under WSL 2, Ubuntu).
* **macOS**: Xcode (command-line tools) and CMake; `brew install ninja` for the Ninja presets.
* **Music** off Windows: there is no WinMM, so use the SoundFont output (see "Music" below);
  without a SoundFont the game runs without music.

## Continuous integration

`.github/workflows/ci.yml` builds the engine and runs `tmtests` on `windows-latest`,
`ubuntu-latest` and `macos-latest` (presets `ci`). CI has no game data: the tests that need
`TM_DATA` skip themselves, the data-free tests run. (The workflow has not run on GitHub yet; the
Linux part of it was tried locally under WSL 2.)

## Gizmos & Gadgets

The same build makes the second game (`-DTM_BUILD_GG=ON`, the default): `ggport` (a windowed
program on Windows), `ggport-cli` (Windows: the console version, for the tools and scripts) and
`ggtests`. They link only the shared core: `python -P tools/check_core_deps.py --gg` checks that
`src/gg/` and `tests/gg/` include nothing of Treasure Mountain. On Linux (WSL 2, GCC 15) `portcore`,
`ggcore`, `ggport` and `ggtests` build without a warning in the project's code and the tests pass.

* **Game data:** `--data <dir>` (the folder with `ssg.exe` and `gizmo000.dat` .. `gizmo003.dat`; the
  game's `gizmo004.dat` is ignored), else `data =` in `ggport.ini`, else `GG_DATA`, else a folder
  next to the program (or its parents, or a `GadgetsAndGizmos` folder there). The engine opens the
  game files read-only and refuses other versions with a message naming the file.
* **Preference folder:** `--pref-dir <dir>`, default SDL's `GGPort/GGPort` (`ggport.ini`,
  `players.dat`, screenshots).
* **Running:** `ggport` plays the game (README "Gizmos & Gadgets"); `--gg-preview` shows the wave 2
  preview of the engine's screens instead. Options: `ggport --help` (`--sound off|speaker|fm`, `--gg-args "<the original's switches>"`,
  `--cpu 286|386`, `--seed N`, `--fullscreen`, `--windowed`, `--square-pixels`, `--scale N`,
  `--integer-scale`, `--no-pause`, `--screenshot-dir`, `--verbose`). Headless and scripted runs:
  `--headless` (deterministic virtual time; the run ends after `--ticks N` PIT ticks or the script),
  `--keys` / `--mouse` (input in PIT ticks: `TICK:tap:KEY`, `TICK:click:X:Y`, ...), `--frame-dump
  DIR|FILE.png`, `--wav-dump FILE.wav` (the sound rendered offline), `--opl-log FILE`. For the
  fidelity work: `--gg-trace FILE` (a CSV row per frame, with `--gg-trace-polls` per input poll
  too), `--gg-dump-state TICK FILE` (the DGROUP image at a PIT tick, and `FILE.fields.tsv`),
  `--gg-load-state FILE` (start from a DOSBox-X save state of the hub, the workshop, a warehouse
  room, a puzzle or a race; `re/gg/docs/ENGINE_MAP.md` §10.6).
* **Tools** (`ggport-cli`, each with `--data <dir>`; they never write into the game folder):
  data `--gg-info`, `--gg-list [--leaves]`, `--gg-selftest`, `--gg-extract ID OUT`; graphics
  `--gg-pic`, `--gg-pal`, `--gg-font`, `--gg-movie`, `--gg-title`, `--gg-fade`, `--gg-wipe`,
  `--gg-lpx-list`, `--gg-font-list`, `--gg-movie-list`; sound `--gg-sound fm|speaker|off WHAT
  OUT.wav` (WHAT: `song ID`, `music ID`, `effect ID K`, `soundtest NAME|SCRIPT`), `--gg-oplog`,
  `--gg-dro-compare LOG REF.dro`, `--gg-songs`, `--gg-soundtest-check NAME|all`; runtime
  `--gg-clock`, `--gg-input-trace`; screens `--gg-screen NAME OUT.png` (the attract, sign-in and UI
  screens; `hub`, `warehouse`, `blueprint`, `puzzle KIND VARIANT` from the world), `--gg-ui-info`,
  `--gg-rooms RACE`.
* **Tests:** `ggtests` with `GG_DATA` (the game folder) and `GG_CAPTURES`
  (`extracted\captures\gg`, the DOSBox-X reference material); without them the tests that need
  them skip themselves.

## Pointing the engine at the game data

`--data <path>` takes either:

* the game folder `TMTWINCD` (it contains `TMTWINCD.EXE`, `TMT.DLL`, `TMT256.DLL`, `TMT16.DLL`,
  `TM0SOUND.DLL`..`TM8SOUND.DLL` and `SOUNDS\`), or a folder that contains it;
* a CD image: `.cue` (with its `.bin`, `MODE1/2352`), a raw `.bin` or a cooked `.iso`. The
  image is read in place; the game folder is found inside its ISO 9660 file system.

Without `--data` the engine uses the environment variable `TM_DATA`. Without that, it searches
the current directory and the executable's directory (and their parents) for a `TMTWINCD`
folder, or a single `.cue`/`.iso` file. File names are matched case-insensitively.

**The 1990 DOS edition** (`SST.EXE` V 1.0, see the README's "DOS edition") is played from its own
folder, the one
holding `SST.EXE`, `SPRITES.PAK`, `ALL_PUZS` and the other `.PAK`/`.PAN` files. `--data` may point
at that folder, at `SST.EXE`, or at a folder that contains it. The edition is detected from the
files: `TMTWINCD.EXE` or a CD image means the Windows CD edition, `SST.EXE` the DOS edition (an
`SST.EXE` of another version is refused with a message saying why). When a path holds both
editions (for example a folder with `TM-DOS\` and `TMTWINCD\`), choose one with
`--edition win|dos` or `edition = win|dos` in the `[engine]` section of `tmport.ini`; otherwise
the engine stops with a message that names both.

| Edition chosen | Where the data path comes from, in order |
|---|---|
| none (detect) | `--data`, `TM_DATA`, `[engine] data`, the Windows auto-detection above; then `TM_DATA_DOS`, `[dos] data`, a folder with `SST.EXE` next to the executable or the current directory (or one of their parents, or a subfolder of one of them). When both are found this way the Windows edition wins |
| `--edition win` | `--data`, `TM_DATA`, `[engine] data`, the Windows auto-detection |
| `--edition dos` | `--data`, `TM_DATA_DOS`, `[dos] data`, the DOS auto-detection |

## A ready-to-play copy

`scripts\package.bat` (Windows) and `scripts/package.sh` (Linux, macOS) build the engine and put
it next to a copy of your game data, so the game starts with no arguments:

```
scripts\package.bat                                  (Windows)
scripts/package.sh ~/Games/TMTWINCD                  (Linux, macOS)
scripts/package.sh ~/Games/tm.cue --soundfont ~/sf2/GeneralUser.sf2
```

| Platform | Result | Start it with |
|---|---|---|
| Windows | `dist\TMPort\tmport.exe` + `TMTWINCD\` | a double-click on `tmport.exe` |
| Linux | `dist/TMPort/tmport` + `TMTWINCD/` | `dist/TMPort/tmport` |
| macOS | `dist/TMPort.app` (data in `Contents/Resources`) | a double-click, or `open dist/TMPort.app` |

* **Game data** (first argument, or `--data`): the `TMTWINCD` folder (or a folder that contains
  it), or a CD image (`.cue` with its `.bin` files, or `.iso`), which is copied as it is. Without
  an argument the script uses `TM_DATA`, then the repository's `extracted/wincd/TMTWINCD` or
  `TMTWINCD/`, then a single `.cue`/`.iso` under `TM-WIN/`.
* `--soundfont <sf2>` copies a SoundFont next to the engine; a single `.sf2` there is used for
  the music when none is configured, so Linux and macOS copies play music without setup.
* `--out <dir>` writes somewhere other than `dist/`; `--zip` also writes `TMPort-<platform>.zip`.
* The build goes to `build-package/` (`BUILD_DIR` changes it), without the tests. On Windows the
  C runtime is linked in (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`), so the copy also runs on
  PCs without the Visual C++ runtime.
* Saves and settings stay in the preference folder (see "Usage"), so they carry over between
  rebuilt copies and the development build. `--data`, `TM_DATA` and `data =` in `tmport.ini`
  still win over the copy's own data.
* The macOS bundle is signed ad hoc for local use. It has no icon of its own (the game window
  takes its icon from the game's files).

**Gizmos & Gadgets:** `scripts\package_gg.bat` (Windows) and `scripts/package_gg.sh` (Linux,
macOS) do the same for `ggport`:

```
scripts\package_gg.bat                               (Windows)
scripts/package_gg.sh ~/Games/GIZMOS                 (Linux, macOS)
```

| Platform | Result | Start it with |
|---|---|---|
| Windows | `dist\GGPort\ggport.exe` + `GadgetsAndGizmos\` | a double-click on `ggport.exe` |
| Linux | `dist/GGPort/ggport` + `GadgetsAndGizmos/` | `dist/GGPort/ggport` |
| macOS | `dist/GGPort.app` (the folder in `Contents/Resources`) | a double-click, or `open dist/GGPort.app` |

* **Game folder** (first argument, or `--data`): the folder with `SSG.EXE` and `GIZMO000.DAT` (or
  a folder that contains it), copied whole. Without an argument the script uses `GG_DATA`, then the
  repository's `GadgetsAndGizmos/`.
* `--out <dir>` and `--zip` (`GGPort-<platform>.zip`) work as above, and the build goes to the same
  `build-package/`. The script replaces only `GGPort` (and `GGPort-README.txt`) in `dist/`.
* Players and settings stay in `ggport`'s preference folder (`GGPort`); the copy's `PLAYERS.DAT`
  is imported on the first start and never written. `--data`, `GG_DATA` and `data =` in
  `ggport.ini` still win over the copy's own folder.

The copy contains your copy of the game: it is for your own use, not for sharing. `dist/` is in
`.gitignore`.

## Usage: playing the game

```
build\tmport.exe                         (or double-click tmport.exe)
build\tmport.exe --data D:\TMTWINCD      (game folder or CD image, see above)
```

`tmport` without a tool command starts the game: it opens a window (640x480 canvas, scaled), shows
the title and story pages, then the Club Sign-In, and then the game itself: the climb from the foot
of the mountain (or the treasure hall) to the castle, the celebration and the rank screen, and the
next climb. Players and their climbs are saved in `users.dat` as in the original (on Exit, at Start
Another Game and at the end of every climb). While a climb is played the **menu bar** works:

* **File**: See Rank, Start Another Game (back to the sign-in), Exit;
* **Options**: New Player Messages, Music and Sound, Voice (the game's own Yes/No boxes);
* **Help**: the four help topics and About Treasure Mountain!.

Open a menu with the mouse, with Alt+F / Alt+O / Alt+H, or with F10 (or a lone Alt) and the arrow
keys; Enter chooses, Esc closes. While a menu is open the game is frozen and the sound paused, as
in the original. The menu bar is only shown while a climb is played and is greyed out during the
game's modal sequences. Ctrl+S switches Music and Sound and Voice together. Esc (or closing the
window) asks whether to exit. Losing the window focus pauses the game (`--no-pause` keeps it
running).

**Alt+Enter** or **F11** switches between the window and fullscreen (borderless, on the window's
display; the picture keeps its letterbox and, with `--integer-scale`, integer scaling). These keys
never reach the game or the menu bar. `fullscreen = true` in the `[engine]` section of
`tmport.ini`, or `--fullscreen`, starts in fullscreen; `--windowed` overrides the setting for one
run. The switch at run time is not saved. The window icon is the game's own icon, read from
`TMTWINCD.EXE` (icon resource 15000).

If the game data cannot be found or a file is missing, `tmport` shows a message box naming the
missing file and how to point it at the data, and exits (exit code 3).

The DOS edition's 320x200 picture is shown at 4:3, as on a monitor of its time (each pixel 1.2
times as tall as wide), letterboxed, in a 960x720 window by default (`window_scale =` in `[dos]`:
320x240 times this). With `--integer-scale` (`[engine] integer_scale`) every pixel becomes 5x6
screen pixels (1600x1200, exactly 4:3), or the largest whole-number size that fits the screen;
`--square-pixels` shows it unstretched. It has no in-engine menu bar: the DOS game draws its own.

Game options (any other argument selects the command-line tools below; `tmport --help` lists them):

| Option | What it does |
|---|---|
| `--data <path>` | game folder or CD image (default: `TM_DATA`, then `data =` in `[engine]` of `tmport.ini`, then auto-detection) |
| `--config <ini>` | settings file (default: `tmport.ini` next to the executable, else in the preference directory) |
| `--pref-dir <dir>` | directory for `users.dat`, `tmport.ini` and screenshots (default: `%APPDATA%\TMPort\TMPort\`) |
| `--16` | 16-colour art (`TMT16.DLL`) |
| `--integer-scale`, `--no-pause`, `--input-mode lossless\|first-event`, `--tap-latch` | display and input options (also in `tmport.ini`) |
| `--fullscreen`, `--windowed` | start in fullscreen / in a window (default: `fullscreen =` in `[engine]` of `tmport.ini`, else a window); Alt+Enter / F11 switch at run time |
| `--midi-set E\|B`, `--midi auto\|winmm\|soundfont\|null`, `--no-audio` | music files (General MIDI `E*.MID` by default), MIDI output (see "Music" below), no sound at all |
| `--soundfont <file.sf2>` | General MIDI SoundFont for the `soundfont` MIDI output (default: `soundfont =` in `[audio]` of `tmport.ini`); selects that output unless `--midi` is given |
| `--screenshot-dir <dir>` | where F12 writes PNG screenshots of the game area |
| `--show-treasure`, `--debug` | debug: boxes around the hiding places that hold a treasure (gold) or the key (cyan), drawn over the picture by the presenter (the game's own frame is unchanged); `--debug` only enables F9, which switches them on and off (`[debug] show_treasure` / `keys` in `tmport.ini`). `--room-run` takes `--show-treasure` too |
| `--verbose` | debug logging |
| `--headless` | no window, no message boxes, no sound (automation); errors go to stderr; the run ends when the `--keys` script is used up |
| `--keys <script>` | scripted input: comma-separated steps `enter`, `esc`, `space`, arrows, `f1`..`f12`, `alt`, letters/digits, `alt+o`/`ctrl+s`/`shift+a`, `click:<x>:<y>`, `move:<x>:<y>` (canvas coordinates), `wait:<ms>`, `shot:<file.png>` (writes the 640x480 canvas), `quit`. The DOS edition counts in game ticks (47.34 per second): items `TICK:down:KEY`, `TICK:up:KEY`, `TICK:tap:KEY[:N]`, `TICK:move:X:Y`, `TICK:click:X:Y`, `TICK:buttons:MASK` (`+N` = N ticks after the previous item; keys `enter`, `esc`, `space`, `up`, `kp8`, `lctrl`, `a`, `5`, `minus`, ...), e.g. `--keys "50:tap:enter, +50:tap:enter, +10:tap:3"`; a TICK with a fraction (`806.4923:down:down`) lies inside the game tick, as a key press on the original falls between two timer interrupts (it decides the phase of the key's typematic repeats) |
| `--frame-dump <png>` | writes the 640x480 canvas when the game ends (the DOS edition: its 320x200 canvas). DOS edition: a folder (an existing one, or a name without an extension) receives a PNG of every screen shown, `NN_<screen>_<look>.png`, and with `--headless` the sound of the run as `sound.wav` |
| `--dos-trace <csv>` | DOS edition: one CSV row per game tick that ran (player, camera, state, keys, coins, nets, level, scene, UI mode, area and the events of the tick; a tick the original lost in disk I/O has no row, and the events of a frame whose own tick was lost start the next row) |
| `--dos-dump-state <tick>:<file>` | DOS edition, repeatable: after that game tick, the game state in the original's memory layout (DGROUP, 52 256 bytes), `<file>.fields.tsv` naming its fields, and `<file>.png`, the picture shown |
| `--dos-ticks <n>` | DOS edition with `--headless`: run at least n game ticks (otherwise the run ends when the `--keys` script is used up and a flash or dissolve it started has ended) |
| `--dos-layout-seed <n>` | DOS edition: new games take their layout from this srand value (0..65535, decimal or `0x` hex) instead of the clock, so scripted runs repeat exactly |
| `--dos-load-state <file.sav>` | DOS edition: start from a DOSBox-X save state of the original instead of the boot (the reference states of `extracted\captures\dos\savestates`): the game, the screen, the random generator and the timer phase continue from that moment; the state's look and sound device are used (a `--look` wins), and its sibling `<name>.users` becomes the DOS save of the preference folder (a replaced file is kept as `.bak`: use a scratch `--pref-dir`). Ticks of `--keys`, `--dos-ticks` and `--dos-dump-state` count from the load, `--dos-dump-state 0:<file>` writes the loaded moment, and the trace's `ticks` column is the original's g_ticks (`tools/dos/README.md`); the sound driver's effect and tune go on where the state left them |
| `--dos-grey-key-artefact` | DOS edition: the E0 prefix bytes of the grey keys (PgUp, Home, the grey arrows) reach the game's keyboard handler as on DOSBox-X, which clears the jump bit for a tick per PgUp repeat; to replay the reference frame logs exactly |
| `--mods <dir>`, `--no-mods` | also load this mod folder (repeatable, after the `[mods]` folders of `tmport.ini`; the DOS edition's after `[mods] dos_paths`) / load no mods at all (`docs/MODDING.md`) |
| `--play` | accepted and ignored (`tmport --play` = `tmport`) |
| `--edition win\|dos\|auto` | which edition to play when the data holds both (default: `edition =` in `[engine]` of `tmport.ini`, else detected; see "Pointing the engine at the game data") |
| `--look ega\|tandy\|mcga\|cga` | DOS edition: the picture of that video adapter (EGA and Tandy look the same; MCGA has softer colours; CGA is dithered to 4 colours; `hercules` is accepted and looks like CGA, as in the original); default `ega`, `[dos] look` |
| `--sound tandy\|speaker\|adlib\|off` | DOS edition: the sound device (Tandy 3-voice, PC speaker, AdLib music); default `tandy`, `[dos] sound`; `--no-audio` = `off` |
| `--slow-cpu` | DOS edition: the original's slow-machine mode (`-X`: fewer elves, coarser scrolling); `[dos] slow_cpu` |
| `--frame-speed reference\|period` | DOS edition: effects that the original counted in drawn frames run at the speed of the reference recordings (default; then the reference machine's slow frames also take their time: scene loads lose the timer ticks they lost there, a slow frame's picture appears when it ends) or one step per frame, like a period PC, every frame within its tick; `[dos] frame_speed` |
| `--dos-args "<switches>"` | DOS edition: the original's switches, e.g. `"-T -St"`: `-E -C -T -H -M` look, `-X` slow CPU (toggles), `-Si -St -Sa` sound; only the second letter counts, later ones win; `--look`, `--sound`, `--slow-cpu`, `--frame-speed` win over them |
| `--square-pixels` | DOS edition: show the 320x200 picture with square pixels (16:10) instead of 4:3; `[dos] square_pixels` |

On the first start the original's saves (`C:\TLCWIN\TMTWINCD\users.dat`) are copied once into the
preference directory; the original file is never changed. `users.dat` (same format as the original's)
and `tmport.ini` are replaced atomically, and the previous version is kept as `users.dat.bak` /
`tmport.ini.bak`. If `users.dat` is damaged (too short or unreadable), the game reads the players from
`users.dat.bak`; without a usable backup it leaves the damaged file alone and shows the game's "Write
Error" box instead of saving over it.

Example of a headless check (no window, no sound): run the title, sign in a new player "Ann",
dismiss the first two New Player Messages in room 9 (the menu bar is greyed while they show) and
capture the Options menu:

```
build\tmport.exe --headless --data D:\TMTWINCD --pref-dir C:\temp\tmprefs ^
    --keys "space,wait:800,space,wait:1500,a,n,n,enter,wait:1500,enter,wait:800,enter,wait:800,alt+o,shot:C:\temp\menu.png"
```

The arrow keys of a script are presses (key down and up together); the game polls held arrows once
per 100 ms tick, so to walk or climb in a script add `--tap-latch` (each press then counts as one
tick of a held arrow) and repeat the press, e.g. `right,right,right,...`. A script whose last step
answers a dialog may end before the game acts on the answer: end it with a `wait:<ms>` step.

## Music: MIDI output and SoundFonts

The game's music is a set of Standard MIDI Files (`SOUNDS\E*.MID`, General MIDI). The engine plays
them with its own sequencer and sends the notes to one of these outputs:

| `midi =` | Output |
|---|---|
| `winmm` | the Windows MIDI synthesizer (Microsoft GS Wavetable Synth or whatever the system maps MIDI to); Windows only |
| `soundfont` | a built-in software synthesizer (TinySoundFont) playing a General MIDI SoundFont (`.sf2`) that you supply; every platform. Its sound is mixed with the game's sound effects and speech on the same audio device |
| `null` | no music |
| `auto` (default) | `winmm` on Windows; on Linux and macOS `soundfont` when a SoundFont is configured, otherwise no music (the log says once how to configure one) |

Linux and macOS have no system MIDI synthesizer, so the music needs a SoundFont there. Settings in
`tmport.ini` (next to the executable, or in the preference directory):

```
[audio]
midi = soundfont                 ; or auto / winmm / null ("midi_backend" is the same key)
soundfont = /usr/share/sounds/sf2/FluidR3_GM.sf2
music_volume = 80                ; 0..100, the SoundFont output only (default 100)
```

A relative `soundfont` path is relative to the folder of `tmport.ini`. On the command line,
`--soundfont <file.sf2>` does the same (and selects the `soundfont` output unless `--midi` says
otherwise); on Windows, `--midi soundfont` (or `midi = soundfont`) uses the SoundFont instead of
the Windows synthesizer. If the SoundFont cannot be loaded, the log says why and the game runs
without music.

The engine does not include or download a SoundFont. Any General MIDI SoundFont 2 file works;
freely licensed ones include:

* **FluidR3_GM** (MIT license, about 140 MB). Debian/Ubuntu package `fluid-soundfont-gm`
  (installs `/usr/share/sounds/sf2/FluidR3_GM.sf2`); Fedora `fluid-soundfont-gm`.
* **GeneralUser GS** by S. Christian Collins (its own permissive license allowing free use and
  redistribution; about 30 MB). A good balance of size and quality.
* **MuseScore_General** (MIT license; derived from FluidR3). Take the `.sf2` download, not `.sf3`.
* **TimGM6mb** (GPL-2.0, about 6 MB, small and plain). Debian/Ubuntu package `timgm6mb-soundfont`.

Only uncompressed `.sf2` files are supported (not the Ogg-compressed `.sf3` variant). Keep
`--midi-set E` (the default): the `B*.MID` files are written for base-level synthesizers that
listen on MIDI channels 13-16, which a General MIDI SoundFont does not map the same way.

The SoundFont synthesizer handles note on/off, program changes (with channel 10 on the drum
kits), bank select, volume, expression, pan, sustain, pitch bend and its range (RPN 0), all notes
off / all sound off / reset all controllers. It has no reverb or chorus.

Audio tools (offline: nothing is played and no audio device is opened):

| Command | What it does |
|---|---|
| `tmport --midi-render <file\|tune id> <out.wav> --soundfont <file.sf2> [--seconds N] [--loop] [--rate R] [--midi-set B\|E]` | plays a tune through the SoundFont synthesizer on a simulated clock and writes a 16-bit stereo WAV (default length: the song plus a 2 s release tail; `--loop` repeats it as the game does). Prints the song length, the programs missing from the SoundFont, and the level statistics (peak, RMS, clipping, first/last sound) |
| `tmport --make-test-soundfont <out.sf2>` | writes a tiny synthetic SoundFont (440 Hz sine and square tones, a fixed-pitch drum kit) generated in code, for checking the SoundFont path without a real SoundFont. It sounds like a test tone generator, not like music |
| `tmport --midi-dump <file\|tune id> [--midi-set B\|E] [--brief]` | parses a tune and prints its events, tempo map and timing, plus a simulated sequencer run |
| `tmport --dump-ssnd <id> <out.wav>` | one SSND sound resource as a WAV file |

`<file|tune id>` is a file on disk, a path inside the game folder such as `SOUNDS\EOPEN1.MID`, or
a tune id `9000`..`9009` (resolved like the game does; needs `--data` or `TM_DATA`). Example:

```
build\tmport.exe --make-test-soundfont C:\temp\test.sf2
build\tmport.exe --midi-render SOUNDS\EOPEN1.MID C:\temp\eopen1.wav --soundfont C:\temp\test.sf2 --data D:\TMTWINCD
```

## Command-line tools

`tmport` with a tool command runs that tool instead of the game. Every tool runs without a window
(except `--view`) and without sound, and takes `--data <path>` (default: `TM_DATA`, then
auto-detection; the game-data and mod tools also read `data =` in `tmport.ini`) and `--verbose`.
`tmport --help` lists the game options and every tool in one screen; `tmport --help <topic>` the
options of one group (`game`, `inspect`, `text`, `anim`, `screen`, `scene`, `room`, `audio`,
`mods`; `all` prints everything).

On Windows every command works with `tmport.exe` and with `tmport-cli.exe`; in cmd or PowerShell
scripts prefer `tmport-cli.exe` (see "Windows" above), e.g. `build\tmport-cli.exe --info`.

**Mods in the tools.** `--mods <dir>` (repeatable, later folders win) loads a mod folder into every
tool that reads the game files except `--info` (which always checks the original files),
`--export-mod-kit` and the audio tools. The tools load only the folders given with `--mods`, not
the `[mods]` folders of `tmport.ini` (the game loads those).

### Game data

| Command | What it does |
|---|---|
| `tmport --info` | file inventory, NE/resource counts per DLL, DGROUP validation, decode check of every PMAP, BNDL and SSND summary; exit code 1 if anything is wrong |
| `tmport --dump-pmap <id> <out.png> [--16] [--opaque]` | one PMAP as an indexed PNG (key colour transparent unless `--opaque`) |
| `tmport --dump-sheet <first> <last> <out.png> [--16]` | contact sheet of the PMAPs in an id range |
| `tmport --headless-frame <out.png> [--16] [--fb-dump <fb.png>]` | composes a test scene through the frame buffer and presenter **without opening a window** and writes the 640x480 canvas (optionally the whole 512x1055 buffer) |
| `tmport --view [id] [--16] [--integer-scale]` | interactive PMAP browser (opens a window): Left/Right previous/next id, PgUp/PgDn -/+10, Home/End, M mirror, O opaque, T switch 16/256 colours, I integer scaling, F12 screenshot, Esc quit |

`--16` selects the 16-colour art (`TMT16.DLL`) and the VGA palette instead of `TMT256.DLL` and
RRGB 900. F12 screenshots (512x384 game area) go to `--screenshot-dir` or, by default, to
`screenshots\` in the SDL preference directory (`%APPDATA%\TMPort\TMPort\` on Windows).

### Text, dialogs and animations

These write the 512x384 game area as a PNG, for comparison with captures of the original
(`re/docs/REFERENCE_CAPTURES.md`).

| Command | What it does |
|---|---|
| `tmport --render-dialog <name\|hexoffset> <out.png>` | draws a game dialog (DlgDesc), e.g. `exit`, `about`, `1533` |
| `tmport --render-help <topic 1..4> <page> <out.png>` | one page of a help topic |
| `tmport --render-text <othrId> <x> <y> <w> <out.png>` | lays out an OTHR text with the game's text renderer |
| `tmport --render-font <out.png>` | glyph sheet of the game font (FFNT 999) |
| `tmport --text-metrics <othrId> [<w>]` | prints the measured extent and line count of a text |
| `tmport --render-dam <script> <bndl> <frameBase> <tick> <out.png>` | runs a DAM animation script for `<tick>` ticks over a background (many options: `tmport --help anim`) |
| `tmport --dam-info <script>` | disassembles one DAM script and simulates it tick by tick |
| `tmport --dam-check` | parses every DAM script in `TMT.DLL`; exit code 1 if one fails |

Background options of the text renders: `--bg <512x384 P6 PPM>` (e.g. a cropped capture),
`--bg-pmap <id> [--bg-at <x> <y>]`, `--fill <x> <y> <w> <h> <ega>`; and `--sel <n>` (dialog button
highlight, -1 none), `--arg <text>` (the `%s` of a sign-in dialog), `--colour <ega>`,
`--fb-dump <png>`.

### Screens, scenes and the room loop

These run parts of the game headless on a **simulated clock** (a run of minutes of game time
takes a second or two) with scripted input, and write PNG frames of the 512x384 game area.
Audio is silent but timed: speech lengths follow the real sounds.

| Command | What it runs |
|---|---|
| `tmport --screen title\|signin\|attract` | the title, the Club Sign-In, or both as at program start (`--users <users.dat>`, `--restart`, `--player <name>`) |
| `tmport --scene <kind> [arg]` | one modal scene: `quiz <q>`, `hint <n>`, `cutscene <level>`, `celebrate <climbs>`, `rank <total>`, `seerank <total>`, `castle` (`--tier`, `--seed`, `--coins`, `--nets`, `--room`, `--total`, `--points`, ...) |
| `tmport --room-run --new-player <name>` (or `--record <users.dat> <slot\|name>`) | climb set-up and the room loop, with state overrides to reproduce a save state (`--start`, `--coins`, `--key`, `--elf`, `--quiz correct\|wrong\|esc`, ...) |

Common options: `--keys "<script>"` or `--keys-file <file>`, `--out <dir>` (where `shot:NAME`
writes `NAME.png`); for screens and scenes also `--trace` (events with their simulated time),
`--max-ms <n>` (stop after n ms of simulated time; scenes default 600000) and `--no-audio`; for the
room loop `--ticks <n>` (default 600).
Frames: `--scene ... --shot-every <ms>` writes `NNNN.png` every `<ms>` of simulated time and always
`final.png` at the end; `--room-run --dump-every <N> <dir>` writes one frame every N ticks
(`tick_NNNNN.png`), `--log` prints one line per tick. `tmport --help screen|scene|room` lists every
option.

The script tokens differ a little from the game's `--keys` (above):

* `--scene` tokens are separated by spaces or commas; `--screen` and `--room-run` tokens by
  spaces. Times are simulated milliseconds from the start: `wait:MS` advances the script time,
  `shot:NAME` writes a frame, `end` stops the run.
* Keys: `enter esc space tab` and the arrows; `--scene` also `ctrl+s y n c`, `--screen` also
  `bs ctrl+r ctrl+s pgup pgdn home`, `char:C` and `text:STRING`, `--room-run` also `c` and
  `tick:N` (wait until tick N).
* An arrow token is a press and release in the same instant, which the game's 100 ms tick does
  not see (as in the original). **To walk or climb, hold the arrow**: `hold:right:2000`, or
  `down:KEY` / `up:KEY` (`--scene`), `kd:KEY` / `ku:KEY` (`--room-run`). In `--scene` a `hold`
  advances the script time by its length; in `--room-run` it does not.
* Mouse: `click:X,Y`, `press:X,Y`, `release:X,Y`, `move:X,Y` and (not `--room-run`) `rclick:X,Y`
  in **game** coordinates (0..511, 0..383; the game's `--keys` uses 640x480 canvas
  coordinates). `code:0xNNNN` (not `--room-run`) posts a raw input code.

Without `end`, a scene runs until it returns or until `--max-ms`: with `--shot-every 1000` that is
up to 600 frames. Example, the castle wall at rank tier 2: walk right to the ladder (1.2 s), climb
for 3 s (onto the second screen), one frame per simulated second:

```
build\tmport-cli.exe --scene castle --tier 2 --out C:\temp\castle --shot-every 1000 ^
    --keys "wait:500,hold:right:1200,hold:up:3000,wait:1000,end"
```

A script with a malformed token stops before the scene starts (exit code 2, no frames) and prints
the token syntax.

### Mods

| Command | What it does |
|---|---|
| `tmport --export-mod-kit <dir> [--force] [--no-verify]` | writes your game's assets in the editable mod formats into `<dir>` and re-imports every file to check the round trip (`docs/MODDING.md`) |
| `tmport --check-mods [<dir> ...] [--compare]` | converts and checks every file of the given mod folders (default: the `[mods]` folders of `tmport.ini`); `--compare` compares each override with the game's own data; exit code 1 on errors |
| `--mods <dir>` | with the game or any tool above: also load this mod folder |

The audio tools (`--dump-ssnd`, `--midi-dump`, `--midi-render`, `--make-test-soundfont`) are
described in "Music" above.

### The DOS edition's tools

`tmport --help dos-gfx`, `--help dos-sound` and `--help dos-autoplay` list every option.

| Command | What it does |
|---|---|
| `tmport --dos-pan <file> <out.png>`, `--dos-pak <file> <out.png>`, `--dos-screen <name> <out.png>` | a PAN (the title picture or a scenery PAN as its world map), a contact sheet of a PAK's frames and the preview's screens to PNG, in any look (`--look`) |
| `tmport --dos-sound <speaker\|tandy\|adlib> sfx <0..11>\|tune <0..2>\|soundtest\|all <out>` | effects and tunes to WAV through the game's own sound driver; AdLib tunes through the original's AdLib driver on the emulated OPL2 (`--variant 0..3` picks one of a tune's four orchestrations, `m:tune/v` in a `--script`). `--opl-log <txt>` writes every OPL register write (`tick reg value`), `--from-state <sav>` starts the sound test from the driver of a DOSBox-X state |
| `tmport --export-mod-kit <dir> --edition dos`, `tmport --check-mods [<dir> ...] --edition dos` | the DOS edition's mod kit and mod checker (`docs/MODDING.md`, "The 1990 DOS edition"); a `--data` that names a DOS folder selects the edition too |
| `tmport --edition dos --autoplay [--seeds A-B] [--climbs N] [--soak] [--look ...] [--sound ...] [--pref-dir <dir>]` | the soak test: a bot plays the game headless with the keyboard (sign-in, elves, questions, keys, treasures, the three levels, the castle, the end sequence, the treasure room, Start Over, Start New Player, Exit and Save with a resumed run) and checks the game's rules after every game tick (coins and nets, the layout, key states, clue words, the elves, USERS.DAT after every save, a resumed game against its record). `--soak` varies the look, the sound device, slow CPU, the frame speed, crafted players and the exercises per seed; a seed always plays the same way. Exit code 0 clean, 1 a rule broken or a stall (the line names the seed and the tick), 4 a hang, 5 a crash |
| `tmport --edition dos --autoplay-vision-check` | what the bot sees on the screen against the game's elf table |

A regression soak after a change: `tmport --edition dos --autoplay --soak --seeds 1-200 --climbs 3
--pref-dir <scratch>` (a few minutes); since every seed replays exactly, a changed tick count or a
failing seed points at a change of behaviour. `tools/dos/oplcmp.py` compares an `--opl-log` with a
DOSBox-X OPL capture write for write and `tools/dos/audiocmp.py` the engine's sound with a recording
(`tools/dos/README.md`).

## Tests

```
set TM_DATA=C:\path\to\TMTWINCD
set TM_IMAGE=C:\path\to\cd.cue
set TM_DATA_DOS=C:\path\to\TM-DOS
set TM_DOS_CAPTURES=C:\path\to\extracted\captures\dos
build\tmtests.exe
```

* Tests that need game data read `TM_DATA` (a folder or an image) and skip themselves when it
  is not set; the data-free tests (PNG writer, CRC/Adler, RUND codec, frame buffer primitives,
  palette, Borland RNG, id routing, icon decoder, ...) always run. This is what CI runs.
* The tests of the 1990 DOS edition (file checks, decoders, screens, sound, input, saves, the
  runtime, the preview, the game logic and two end-to-end playthroughs) read the DOS folder from `TM_DATA_DOS` (the folder with `SST.EXE`) and
  skip themselves without it. They too store only sizes, counts and hashes.
* The DOS tests that compare with the DOSBox-X reference material of the original (its save states,
  frame logs and USERS.DAT snapshots: the play-field redraws, the frame-log replays, the state
  loader, the save round trips) read it from `TM_DOS_CAPTURES` (`extracted\captures\dos`, see
  `re/dos/docs/REFERENCE_CAPTURES.md`) and skip themselves without it.
* The core's parameters for the second game (PIT rates, deadline timers in ticks, the INT 33h
  event handler, a game's own keyboard handler, the chip renderer and audio device with any driver,
  the OPL write log, the text dialects) have data-free tests (`tests/test_portcore.cpp`).
* `python -P tools/check_core_deps.py` checks that every file of `portcore` includes only core
  files (`--gg`: and that `src/gg/` and `tests/gg/` include only core and G&G files); exit code 1
  on a violation (docs/DESIGN.md "Core and games").

### The DOS fidelity runner

`tools/dos/fidelity.py` runs the scenario files of `tools/dos/scenarios/<group>/` (`ui`, `level1`,
`levels23`, `castle_end`, `looks`) through `tmport-cli --headless` and compares the engine with the
same reference material: the picture after a tick against a capture, an AVI frame or a state's
screen; the per-tick trace against the r2 frame logs (by timer-tick slot and as a sequence of
executed ticks); timings between trace events; AVI time bases (`tools/dos/avifit.py`). It writes a
report (`report.md`, per group `report.md`, `report.csv` and a montage of the differences) and exits
with 1 on a regression against `tools/dos/scenarios/baseline.json`:

```
set TM_CLI=C:\path\to\build\tmport-cli.exe
set TM_DATA_DOS=C:\path\to\TM-DOS
set TM_DOS_CAPTURES=C:\path\to\extracted\captures\dos
python -P tools\dos\fidelity.py --jobs 8 [GROUP ...] [--scenario NAME] [--no-avi] [--out DIR]
python -P tools\dos\fidelity.py --update-baseline        (store the current numbers)
```

Every run passes a scratch `--pref-dir`, a headless window and dummy audio; nothing of the game is
stored in the scenarios (file names, ticks, key scripts and rectangles only). The scenario format,
the tools around it (`savmem.py`, `fl2csv.py`, `imgcmp.py`, `avifit.py`, the playthrough driver) and
the reference material are described in `tools/dos/README.md` and
`re/dos/docs/ENGINE_MAP.md` §10-12 (the results after waves 4 and 5).
* No test opens a window or plays sound. The app-area tests write their PNGs (the decoded window
  icon, scripted-run screenshots) to a temporary folder; set `TM_APP_SHOTS=<folder>` to keep them.
* The image comparison test needs `TM_DATA` to be the extracted folder and `TM_IMAGE` the CD
  image; it checks that both return identical bytes.
* Expected values for the decoders (PMAP pixel hashes, BNDL member lists, SSND headers) were
  computed with the Python reference tools in `tools/assets/`. The tests store only hashes and
  counts, never game data.

`build\tmtests.exe --help` lists doctest's options (e.g. `-tc="PMAP*"` to run some cases).
