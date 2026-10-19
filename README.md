# TM Port: an engine for *Super Solvers Treasure Mountain!*

TM Port is a fan-made re-implementation of the engine of *Super Solvers Treasure Mountain!*
(Windows CD edition, version 1.01, by The Learning Company). It is a new program, written in
C++20 with SDL 3, that runs the original game on current Windows, Linux and macOS systems.

**You need your own copy of the game.** This project contains no game data of any kind: no
pictures, sounds, music, text or tables, and no code taken from the original program. At start-up
the engine reads everything game-specific from your copy of the Windows CD edition (the
`TMTWINCD` folder, or an image of the CD).

TM Port is not affiliated with or endorsed by The Learning Company or any later owner of
*Treasure Mountain!*.

Contents: [Features](#features) · [Requirements](#requirements) ·
[Getting started](#getting-started) · [Controls](#controls) · [Menus](#menus) ·
[Saved players](#saved-players) · [Options](#options) ·
[DOS edition](#dos-edition) · [Modding](#modding) ·
[Building](#building) · [Command-line tools](#command-line-tools) ·
[Project layout](#project-layout) · [Status and known differences](#status-and-known-differences) ·
[Credits and licences](#credits-and-licences)

## Features

* **The whole game**: the title and story pages, the Club Sign-In, the climbs through the three
  levels of the mountain (hiding places, coins and nets, elves and their questions, clue words,
  keys and gates, net shops, the treasure hall), the castle wall, the celebration and rank
  screen, the New Player Messages, the help topics, voice and music.
* **Faithful to the original**: the same 100 ms game tick, the same random number generator (so
  the same layouts and question deals for the same seed), the same rules, text layout and screen
  layout, and the original's quirks where they are visible (see
  [Status](#status-and-known-differences)). The behaviour is specified from a study of the
  original program and checked against frame captures of the original game.
* **Your existing players**: `users.dat` is read and written in the original's exact format; on
  Windows the original's saves are imported on the first start (from elsewhere with one setting).
* **Runs from the CD image**: point it at the `TMTWINCD` folder or directly at a `.cue`/`.bin`/
  `.iso` image of the CD; nothing has to be installed.
* **Comfort, all optional**: a resizable window with letterboxing, fullscreen, integer scaling,
  PNG screenshots, pausing when the window loses focus, music through the system's MIDI
  synthesizer or a SoundFont of your choice on every platform, the 16-colour art set.
* **Mods**: replace pictures, sounds, texts, animations, music and game tables with ordinary
  files (PNG, WAV, text, JSON, MIDI); your game files are never changed ([Modding](#modding)).
* **Tools**: headless command-line tools that inspect the game data and render screens and
  scenes to PNG files ([Command-line tools](#command-line-tools)).

## Requirements

* **Your copy of *Super Solvers Treasure Mountain!*, Windows CD edition** (the CD carries a
  `TMTWINCD` folder with `TMTWINCD.EXE`, `TMT.DLL`, `TMT256.DLL`, `TMT16.DLL`, `TM0SOUND.DLL` to
  `TM8SOUND.DLL` and a `SOUNDS` folder), **or the 1990 DOS edition** (`SST.EXE` version 1.0, see
  [DOS edition](#dos-edition)); other editions are not supported.
* **A computer** with Windows 10 or 11 (64-bit), Linux (X11 or Wayland) or macOS. Windows is the
  main platform. On Linux the engine is built and tested with GCC (under WSL 2); macOS builds
  and plays with Xcode's Clang.
* **Music on Linux and macOS** needs a General MIDI SoundFont (`.sf2`), which you supply (see
  [Options](#options)); Windows uses its built-in MIDI synthesizer.
* **To build it**: CMake 3.24 or newer and a C++20 compiler. See [Building](#building).

There are no prebuilt releases at the moment: build the engine from source.

## Getting started

1. **Build** the engine (see [Building](#building)). On Windows, `scripts\build.bat` produces
   `build\tmport.exe`.
2. **Point it at your game.** Any of these works; the first one found is used:
   1. the command line: `tmport --data D:\TMTWINCD` (the CD's folder, a copy of it, or a folder
      that contains it) or `tmport --data C:\Games\Treasure.cue` (a CD image: `.cue` with its
      `.bin` in MODE1/2352, a raw `.bin` or an `.iso`; it is read in place);
   2. the environment variable `TM_DATA` with the same kind of path;
   3. `data = <path>` in the `[engine]` section of `tmport.ini` (see [Options](#options));
   4. automatic detection: a `TMTWINCD` folder, or a single `.cue`/`.iso` file, in the current
      folder, next to `tmport.exe`, or in a parent of either.
3. **Start** `tmport` (on Windows a double-click on `tmport.exe` is enough).

**Or let a script do it all:** `scripts\package.bat` (Windows) or `scripts/package.sh <game data>`
(Linux, macOS) builds the engine and puts it next to a copy of your game data in `dist/`
(`TMPort.app` on macOS), so it starts with no arguments. That copy contains your copy of the
game, so keep it to yourself. See "A ready-to-play copy" in [`docs/BUILDING.md`](docs/BUILDING.md).

If the game files cannot be found, or a file is missing or damaged, `tmport` says which file and
how to point it at the data, and exits. File names are matched without regard to case, so the
upper-case names of the CD work on every platform.

## Controls

The controls are the original's. In a climb:

| Key | Action |
|---|---|
| Left / Right (hold) | walk; walk off the edge of the screen to the next room |
| Up | go through a door, open a gate (with the level's key), jump; on the castle wall: climb a ladder |
| Enter | toss a coin at the hiding place in front of you (costs one coin) |
| Space | swing the net to catch an elf (costs one net) |
| C | hear the clue words again |
| Esc | ask whether to exit (closing the window does the same) |
| Ctrl+S | music and sound and the voice off / on |

**Mouse**: hold the left button over the play field to walk towards the pointer (the pointer
shows the direction). Click buttons, answers and players; the mouse works in every dialog, in the
questions, on the sign-in list and in the menus.

**Title**: Enter or a click shows the next page, Space skips the story, Esc asks whether to quit.
**Club Sign-In**: type a name (up to eight letters) to make a new player, or choose a player with
Up / Down or the mouse and press Enter; Ctrl+R removes the chosen player; the club has room for
seven players. **Dialogs, questions and messages**: Enter, Esc or a click, as the dialog shows.

Keys of the engine (they never reach the game):

| Key | Action |
|---|---|
| Alt+Enter or F11 | window / fullscreen (the setting `fullscreen` chooses how it starts) |
| F12 | screenshot of the game area as a PNG (into `screenshots` in the [preference folder](#saved-players), or `--screenshot-dir`) |

The game pauses while its window is in the background (as the original did; `--no-pause` keeps
it running).

## Menus

While a climb is played the menu bar works as in the original:

* **File**: See Rank, Start Another Game (back to the sign-in), Exit;
* **Options**: New Player Messages, Music and Sound, Voice;
* **Help**: the four help topics and About Treasure Mountain!.

Open a menu with the mouse, with Alt+F / Alt+O / Alt+H, or with F10 (or a lone Alt) and the arrow
keys; Enter chooses, Esc closes. The game stops while a menu is open. The menu bar is drawn by the
engine inside the game window, is shown only during a climb and is greyed out during the game's
own sequences, as the original's was.

## Saved players

Players and their climbs are saved in `users.dat`, in the original's format, at the same moments
as in the original: on Exit, at Start Another Game and at the end of every climb.

`users.dat`, `tmport.ini` and the `screenshots` folder live in the **preference folder**:

| System | Preference folder |
|---|---|
| Windows | `%APPDATA%\TMPort\TMPort\` |
| Linux | `~/.local/share/TMPort/TMPort/` |
| macOS | `~/Library/Application Support/TMPort/TMPort/` |

`--pref-dir <folder>` uses another folder (for a portable installation, say). A `tmport.ini` next
to the executable is used instead of the one in the preference folder.

**Importing your original players.** On the first start the engine copies the original's
`C:\TLCWIN\TMTWINCD\users.dat` (on Windows) into the preference folder, once. The original file is
never changed. To import a `users.dat` from somewhere else (another computer, a backup, Linux or
macOS), either

* set `import_from = <path to users.dat>` and `imported_original = false` in the `[saves]` section
  of `tmport.ini`: the next start copies it, provided the engine has no `users.dat` of its own yet
  (the import never replaces one), or
* close the game and copy the file into the preference folder yourself (this replaces the players
  saved there).

The format is the original's, byte for byte, so a `users.dat` written by the engine also works in
the original game.

## Options

The engine works without any configuration. Settings live in `tmport.ini`, a text file in the
preference folder (the engine writes one when it imports your players; otherwise create it) or
next to the executable. Every key is optional:

```ini
[engine]
data = D:\TMTWINCD          ; the game folder or CD image
window_scale = 2            ; initial window size: 640x480 times this
integer_scale = false       ; scale the picture by whole numbers only
fullscreen = false          ; start in fullscreen (Alt+Enter / F11 switch at run time)
pause_on_focus_loss = true
edition = auto              ; win or dos: the edition to play when the data holds both

[input]
mode = lossless             ; or first-event: keep only the first input event per message
                            ; drain, as the original did
tap_latch = false           ; true: a very short arrow tap still counts for one tick

[audio]
midi_set = E                ; E: the General MIDI music files (E*.MID); B: the B*.MID set
midi = auto                 ; auto, winmm (Windows synthesizer), soundfont or null (no music)
soundfont = FluidR3_GM.sf2  ; General MIDI SoundFont for midi = soundfont (relative to this file)
music_volume = 100          ; 0..100, SoundFont output only

[saves]
imported_original = false
import_from = C:\TLCWIN\TMTWINCD\users.dat

[mods]
enabled = true
paths = mods                ; mod folders, ';'-separated (see docs/MODDING.md)

[debug]
show_treasure = false       ; true: box the hiding places that hold a treasure (gold) or the key (cyan)
keys = false                ; true: F9 switches the treasure boxes on and off (show_treasure implies it)

[dos]                       ; the 1990 DOS edition
data = D:\TM-DOS            ; its folder (the one with SST.EXE)
look = ega                  ; ega, tandy, mcga or cga: the picture of that video adapter
sound = tandy               ; tandy, speaker, adlib or off
slow_cpu = false            ; the original's slow-machine mode (-X)
frame_speed = reference     ; or period: frame-counted effects at one step per frame
square_pixels = false       ; true: 320x200 with square pixels instead of 4:3
window_scale = 3            ; initial window: 320x240 times this
```

The most useful command-line options (`tmport --help` lists them all):

| Option | Effect |
|---|---|
| `--data <path>` | the game folder or CD image |
| `--16` | play with the 16-colour art (`TMT16.DLL`) |
| `--fullscreen`, `--windowed` | start in fullscreen / in a window |
| `--integer-scale` | whole-number scaling of the picture |
| `--midi-set E\|B` | the music files: General MIDI `E*.MID` (default) or `B*.MID` |
| `--midi auto\|winmm\|soundfont\|null` | the MIDI output |
| `--soundfont <file.sf2>` | play the music through this SoundFont (selects the `soundfont` output) |
| `--no-audio` | no sound at all |
| `--pref-dir <folder>`, `--config <ini>` | other folder for saves and settings, other settings file |
| `--mods <folder>`, `--no-mods` | load another mod folder / no mods at all |
| `--screenshot-dir <folder>` | where F12 saves screenshots |
| `--no-pause` | keep running while the window is in the background |
| `--show-treasure` | debug: box the hiding places that hold a treasure (gold) or the key (cyan); F9 switches the boxes off and on |
| `--debug` | debug keys only: F9 switches the treasure boxes on and off |
| `--verbose` | detailed log |
| `--edition win\|dos` | the edition to play when the data holds both the Windows CD and the 1990 DOS edition |
| `--look`, `--sound`, `--slow-cpu`, `--frame-speed`, `--dos-args "<switches>"`, `--square-pixels` | options of the 1990 DOS edition (`docs/BUILDING.md` lists their values) |

**Music.** `auto` uses the Windows MIDI synthesizer on Windows. On Linux and macOS there is no
system synthesizer: configure a General MIDI SoundFont (`.sf2`, not `.sf3`), for example
FluidR3_GM (`fluid-soundfont-gm` on Debian/Ubuntu) or GeneralUser GS; without one the game runs
without music. The engine does not include or download a SoundFont. `docs/BUILDING.md` ("Music")
has the details.

## DOS edition

The engine also plays the original 1990 DOS edition (`SST.EXE` version 1.0, the 16-colour
EGA/Tandy game) from its own folder (the one with `SST.EXE`, `SPRITES.PAK` and `ALL_PUZS`). Its game
logic is a port of the DOS program, so it plays as the DOS game did, not as the Windows CD edition:

* **The whole game**: the title and attract pages, the Club Sign-In, the clubhouse (rank chest and
  prize shelves), the three levels of the mountain with their hiding places, elves, elf questions,
  clue words, keys and locks, net shops, caves, mine tunnels, bridges and the geyser, the castle
  wall, the end sequence, the treasure room and the rank screen, Flutter's New Player Hints and the
  game messages, the menus (Help, File, Options) and the help pages.
* **Faithful to the original**: the game advances in the original's 47.3 Hz timer ticks with the
  original's order of events, the same random number generator (the same layouts and question deals
  for the same seed), the original's movement timings and the reference machine's slow frames (21
  per-tick recordings of the original in DOSBox-X replayed from the same moment match on every tick),
  and the original's screens: every EGA, Tandy, MCGA and CGA capture reachable from a recorded moment
  of the original matches pixel for pixel, apart from two (`re/dos/docs/ENGINE_MAP.md` §11).
* **Saves** at the original's moments (new player, remove player, Start Over, Start New Player,
  Exit and Save, the end of each game) in `dos_users.dat` in the preference folder, in the DOS
  `USERS.DAT` format byte for byte. On the first start the DOS folder's `USERS.DAT` is imported (the
  DOS file is never changed); copy `dos_users.dat` back as `USERS.DAT` to continue in DOS.
* **Sound**: the original's tunes and effects on the PC speaker, the Tandy 3-voice chip, or AdLib
  (for now with a simple stand-in for the AdLib music).

**Keys** (the original's): Left / Right walk; Up goes through a door or cave, opens a lock with the
level's key, or climbs; Up+Left / Up+Right (or Home / PgUp) jump; Down drops a coin at the hiding
place in front of you (or climbs down); Space swings the net; F1 / F2 / F3 open the Help, File and
Options menus, Up / Down / Enter choose; Esc in play asks to Exit and Save; Ctrl+S switches music
and sound off and on. The mouse works as in the original: hold the button over the play field to
walk, click the status bar to drop a coin, click buttons, answers and players. Alt+Enter / F11
(fullscreen), F12 (screenshot) and the pause in the background work as in the Windows edition.

Start it with `tmport --data D:\TM-DOS` (or set `TM_DATA_DOS`, or `data =` in the `[dos]` section
of `tmport.ini`); when a folder holds both editions, add `--edition dos`. The picture is 320x200
shown at 4:3, as on a monitor of the time. DOS options (also in the `[dos]` section):

| Option | Effect |
|---|---|
| `--look ega\|tandy\|mcga\|cga` | the picture of that video adapter (EGA and Tandy look the same; MCGA has softer colours; CGA is dithered to 4 colours) |
| `--sound tandy\|speaker\|adlib\|off` | the sound device (default `tandy`) |
| `--slow-cpu` | the original's slow-machine mode (`-X`: fewer elves, coarser scrolling, shorter search) |
| `--frame-speed reference\|period` | the speed of the effects the original counted in frames (search, coin count-up, CHAMPION flash, dissolve) and of its slow frames: as in the reference recordings (default: scene loads and full redraws take the game ticks they took there) or one step per frame like a period PC, every frame within its tick and files loading at once |
| `--dos-args "<switches>"` | the original's own switches, e.g. `"-T -St"` |
| `--square-pixels`, `--integer-scale` | 320x200 with square pixels; whole-number scaling (5x6 per pixel) |

For testing and comparisons: `--headless --keys "<script>"` plays a script in game ticks
(`60:tap:enter, 400:down:left, +20:up:left`; `806.4923:down:down` presses a key inside a game tick,
which decides when its typematic repeats come), `--dos-trace <csv>` writes one row per game tick that
ran, `--dos-dump-state <tick>:<file>` the game's memory in the original's layout (and the picture
shown), `--frame-dump <folder>` the last frame of each screen, `--dos-layout-seed <n>` makes new
games use a fixed layout instead of one from the clock, and `--dos-load-state <file.sav>` starts from
a DOSBox-X save state of the original. `tools/dos/fidelity.py` compares the engine with the
reference captures, recordings and logs of the original (`docs/BUILDING.md`).
`tmport --data <folder> --dos-preview` shows the earlier static preview of six screens.

**Known differences** (`re/dos/docs/ENGINE_MAP.md` §8): the slow frames of the original (scene loads,
full redraws, message bubbles, scrolling) last as long as in the reference recordings and their
pictures appear when they end, but only the measured kinds are modelled (menus, dialog boxes and the
quiz screen draw within one timer tick); the engine latches a frame's sprites before its timer tick
where the original usually latches them after it, which shows only under a bubble that freezes the
picture right after a tick (an elf's feet under the hint after a treasure search) and in whether the
castle top's tick removes the key icon from the status bar at once; sounds that the original starts
in the middle of a slow frame start at its beginning; the slow-CPU mode's box save does not model
the original's save heap; the AdLib music is a stand-in; there are no mods for the DOS edition
yet.

## Modding

A mod is a folder of replacement files in ordinary formats: PNG pictures, WAV sounds, UTF-8
texts, a small animation-script language, JSON bundles and tables, MIDI music. The engine looks in
the installed mod folders before it reads a resource from the game files; nothing is written back
into your game.

```
tmport --export-mod-kit <folder>      your game's assets in the editable formats, as a starting point
tmport --check-mods <mod folder>      convert and check a mod, list every problem
```

The kit made by `--export-mod-kit` is a copy of your game's data: keep it to yourself. Read
[`docs/MODDING.md`](docs/MODDING.md) for the folder layout, the formats, the rules a mod must keep
and the tools.

## Building

Windows (Visual Studio 2026 with its CMake and Ninja):

```
scripts\build.bat
```

Linux and macOS:

```
cmake --preset release
cmake --build --preset release
```

The first configure downloads SDL, TinySoundFont and doctest (pinned versions, checked by
SHA-256). [`docs/BUILDING.md`](docs/BUILDING.md) lists the requirements (on Linux, the development
packages SDL needs), the build outputs (`tmport`, and on Windows also the console program
`tmport-cli` for scripts), the tests and continuous integration.

`scripts\package.bat` / `scripts/package.sh` make a ready-to-play copy with your game data next
to the engine (see [Getting started](#getting-started)).

## Command-line tools

`tmport` with a tool command runs a tool instead of the game, for checking the game data and for
comparing the engine with the original. The tools open no window (except the picture browser
`--view`) and play no sound:

* game data: `--info` (checks every file and resource), `--dump-pmap`, `--dump-sheet`, `--view`;
* rendering to PNG: dialogs, help pages, texts and the font (`--render-*`), animations
  (`--render-dam`), the title and sign-in (`--screen`), the game's scenes (`--scene`: questions,
  hints, cut-scenes, celebration, rank screen, castle wall) and the room loop (`--room-run`),
  all with scripted input on a simulated clock;
* audio: `--dump-ssnd`, `--midi-dump`, `--midi-render` (a tune through a SoundFont into a WAV);
* mods: `--export-mod-kit`, `--check-mods`;
* the DOS edition: `--dos-pan`, `--dos-pak`, `--dos-screen` (scenery, sprites and the preview's
  screens to PNG) and `--dos-sound` (tunes and effects to WAV); `tmport --help dos-gfx` and
  `tmport --help dos-sound` list their options. The DOS game itself runs headless with
  `--headless --keys`, `--dos-trace`, `--dos-dump-state` and `--dos-layout-seed` (see
  [DOS edition](#dos-edition)).

The game itself can also run headless with scripted input (`--headless --keys "..."`).
`tmport --help` gives an overview and `tmport --help <topic>` the options of each group;
[`docs/BUILDING.md`](docs/BUILDING.md) has examples.

## Project layout

| Path | Contents |
|---|---|
| `src/` | the engine: `util`, `vfs` (folders and CD images), `ne` (Windows NE executables), `data` (tables of `TMTWINCD.EXE`), `res` (resources), `gfx`, `text`, `anim`, `input`, `timing`, `audio`, `ui` (dialogs, menu bar), `save`, `game` (the game itself), `mods`, `rtl`, `platform` (SDL), `app` (start-up), `cli` (tools); `docs/DESIGN.md` describes them |
| `tests/` | unit tests (doctest); tests that need the game read it from `TM_DATA` (`TM_DATA_DOS` for the DOS edition, `TM_DOS_CAPTURES` for the DOS reference captures) and skip themselves without it |
| `docs/` | `BUILDING.md` (building, running, tools), `DESIGN.md` (engine conventions), `MODDING.md` |
| `re/` | the specifications written from the study of the original: `re/docs/ARCHITECTURE.md` first, then `PORTING_NOTES.md` and the topic documents; reviews in `re/reviews/` |
| `tools/` | Python tools used for the study (resource extraction and reference decoders); `tools/dos/` the DOS edition's decoders and its fidelity runner with the scenario files (`tools/dos/README.md`) |
| `cmake/`, `CMakeLists.txt`, `CMakePresets.json`, `scripts/` | the build |
| `.github/workflows/` | continuous integration (Windows, Linux, macOS) |

Your game files, extracted data and disassembly output stay outside the repository (`.gitignore`
lists them).

## Status and known differences

The game is playable from the title to the end of a climb and the next one. The unit tests cover
the decoders, the rules, the scenes and scripted runs of the whole game; the screens have been
compared with frame captures of the original (dialogs, HUD, room transitions, movement per tick,
coin toss and net, sign-in, title, net shop, elf questions, castle wall, treasure hall,
celebration and rank screen; `re/docs/REFERENCE_CAPTURES.md`).

**Kept on purpose** because the original does it (`re/docs/PORTING_NOTES.md` §6.1), for example:
on the first castle screen walking right uses the left-walking step sizes; dialog frames have the
original's one-pixel-short bottom edge; holding Down keeps the current frame; the question
screen's mouse rows ignore blank lines; signing in with a saved player can switch the sound off
but never on; after an Exit from the end-of-climb screens the saved rank tier is the old one (save
compatibility).

**Different from the original:**

* Missing or damaged game files stop the start with a message naming the file (the original
  carried on and failed later).
* Saves and settings live in the preference folder instead of `C:\TLCWIN\TMTWINCD`; the original's
  `users.dat` is imported once and never changed. `TMTWIN.INI` and the original's start-up checks
  (display, sound card, CD drive, Windows version) are not used.
* Input is lossless by default: the original kept only the first input event of each burst, which
  could swallow quick key presses (`mode = first-event` restores that).
* The menu bar is drawn by the engine inside the window instead of a Windows menu; it has the same
  items, keys and greying.
* The picture is scaled to the window (letterboxed); fullscreen, integer scaling and F12
  screenshots are additions. The 16-colour art is chosen with `--16` instead of by the display's
  colour depth.
* Music is played by the engine's own MIDI sequencer (the original used the Windows MCI
  sequencer) through the Windows synthesizer or a SoundFont; the SoundFont synthesizer has no
  reverb or chorus.
* The engine does not prevent a second copy of itself from running (the original refused): do not
  play two copies at once, they share `users.dat`.

Not tried yet: the continuous-integration workflow on GitHub (it runs only when started by hand). Limits of the mod layer
(no high-resolution art, no new rooms or questions) are listed in `docs/MODDING.md`.

## Credits and licences

*Super Solvers Treasure Mountain!* © 1990, 1992–95 The Learning Company. The game, its pictures,
sounds, music and texts belong to their rights holders; none of it is part of this project.

Third-party components, downloaded by CMake at configure time (not stored in this repository) and
built into the programs:

| Component | Version | Licence | Used for |
|---|---|---|---|
| [SDL](https://libsdl.org) | 3.4.18 | zlib | window, input, audio, timing (linked statically) |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) (`tsf.h`, Bernhard Schelling) | commit `853a0a1` | MIT | the SoundFont MIDI synthesizer |
| [doctest](https://github.com/doctest/doctest) | 2.4.12 | MIT | the unit tests only (`tmtests`) |

If you pass on builds of `tmport`, include the MIT licence text of TinySoundFont (SDL's zlib
licence does not require a notice in binaries). Everything else (the PNG and zlib codecs, the
MIDI sequencer, the CD-image reader, the NE parser) is the project's own code. The
reverse-engineering work used Ghidra and Python, which are not part of the engine.

The project's own code and documentation are released under the [MIT licence](LICENSE). The licence
covers this project only: it grants no rights to *Treasure Mountain!* itself, whose files you
need to supply from your own copy.
