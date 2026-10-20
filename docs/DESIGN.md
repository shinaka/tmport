# TM Port — engine design and conventions

A cross-platform C++20 re-implementation of the engine of *Super Solvers Treasure Mountain!*
(Windows CD edition, v1.01). The engine contains **no game data**: it loads art, sound, text and
tables from the user's own copy of the game at runtime. Behaviour is specified in `re/docs/`
(start with `ARCHITECTURE.md`, then `PORTING_NOTES.md`, which this document adopts).

## Ground rules

* **No game content in the repository.** No art, audio, text, tables or copied decompiled code.
  Offsets, ids, sizes and algorithms are facts and may appear in code. Game text is only ever read
  from the user's files. Tests that need game data read it from `TM_DATA` (env var) and skip when
  it is absent.
* **Reimplement, don't transliterate.** Write idiomatic C++ from the specs; use the decompiled
  output only to check behaviour.
* **Fidelity first, options second.** Default behaviour matches the original (timings, layout,
  randomness, quirks listed as "keep" in `PORTING_NOTES.md` §6.1). Comfort features are opt-in.

## Build

* CMake ≥ 3.24, C++20, warnings high (`/W4` MSVC, `-Wall -Wextra` others), warnings not fatal.
* SDL **3.4.18** via `FetchContent` (static), pinned by URL. Other third-party code only if pinned
  and license-compatible (MIT/zlib/BSD/public domain), fetched by CMake, never vendored blindly.
* Targets: `portcore` (the engine core shared by both games, below), `tmcore` (Treasure Mountain's
  engine, links `portcore`), `tmport` / `tmport-cli` (the game), `tmtests` (unit tests, doctest);
  with `TM_BUILD_GG` the second game's `ggcore`, `ggport` and `ggtests` (`cmake/gg.cmake`).
* Windows first (MSVC 2026 + Ninja; `scripts/build.bat`), code kept portable for Linux/macOS (no
  Win32 outside `src/platform/` and the WinMM MIDI back end).

## Source layout (`src/`)

| Dir | Contents |
|---|---|
| `util/` | logging, byte reader (LE), file helpers, PNG writer (uncompressed, for screenshots/debug) |
| `vfs/` | data-source abstraction: plain directory **or** CD image (`.cue`/`.bin`/`.iso`, ISO 9660), case-insensitive lookup |
| `ne/` | `NeImage`: MZ/NE parser (segments, relocations, resource table, type-15 name table) |
| `data/` | `ExeTables`: DGROUP of `TMTWINCD.EXE` with relocation fix-ups; typed accessors for every table (offsets in one header) |
| `res/` | `ResourceManager`: id → type/DLL routing, PMAP/BNDL/FFNT/OTHR/RRGB/SSND access, BNDL trees, mod overrides |
| `gfx/` | `PixMap` (+RUND), `Palette`, `FrameBuffer` (512×1055 indexed, clip, dirty rects), blit primitives, `Presenter`, screen effects |
| `text/` | FFNT font, `TextRenderer` (byte-exact port of `Text_Render`, on the shared `text_engine`) |
| `anim/` | DAM engine (16 channels, frame table), modal animation player |
| `input/` | input-code queue, hot-spot table (50 slots), held keys, mouse, cursors |
| `timing/` | deadline timers (8 slots), clock, the virtual PIT of the DOS games (`pit_clock`) |
| `audio/` | wave voice (single stream), speech wait, SFX, SMF sequencer + MIDI back ends, music manager |
| `ui/` | game dialogs (DlgDesc), pager, in-engine menu bar |
| `save/` | `users.dat` (byte-exact), settings, paths |
| `game/` | game modules (main state machine, title, sign-in, room, player, HUD, layout, elves, quiz, hints, net shop, coin pile, hall, cut-scenes, castle, celebration, help) |
| `rtl/` | Borland `rand`/`srand`/`R(n)`, `sprintf` subset |
| `platform/` | SDL3 app, window, event pump, audio device |
| `mods/` | the mod layer: mod folders, override formats (PNG, WAV, text, DAM assembler, JSON, MIDI), table overrides, mod-kit exporter, `--check-mods` (`docs/MODDING.md`) |
| `app/` | start-up (`bootstrap`: paths, settings, data, engine, audio, menus, `AppContext`), the game's command line, menu command handlers, scripted input (`--keys`) |
| `cli/` | the tool dispatcher and `--help` (`commands`, `options`), the data tools (`--info`, PMAP dumps, test frame, PMAP viewer), the text/UI renders, `--mods` for the tools (`tool_mods`); the other areas keep their own tools (`*_cli.cpp`) |

Namespace `tmport` (a global namespace `tm` is impossible: it collides with the C library's
`struct tm`, which `<ctime>` declares globally and `<format>`/`<chrono>` pull in). Files
`snake_case.cpp/.h`, types `PascalCase`, functions/members `camelCase`,
constants `kName`. One original module ≈ one engine file; cite the original address in a short
comment at each ported routine (e.g. `// 1110:00aa CoinPile_Update`). The second game's code is in
`tmport::gg` (`src/gg/<module>/`).

## Core and games

The repository holds two games: *Treasure Mountain!* (both editions, `tmcore`, `tmport`) and
*Gizmos & Gadgets!* (DOS 1993, `src/gg/`, `ggcore`, `ggport`; `re/gg/docs/SCOPING.md` §9). What
they share is the static library **`portcore`** (`cmake/portcore.cmake`), a library over the
existing directories, not a separate tree. `tmcore` links it publicly, so TM code sees it as
before; G&G links only `portcore` and never includes TM's game code.

| Part | Files (`src/`) |
|---|---|
| utilities | `util/` (log, files, byte reader, errors, hashes, PNG writer, inflate) |
| data sources | `vfs/` (directory, CD image) |
| Borland RTL | `rtl/borland_rand` |
| time | `timing/clock.h`, `timing/deadline_timers`, `timing/pit_clock` |
| host | `platform/host` (window, events, focus pause, fullscreen, screenshots, frame dumps), `platform/console`, `platform/exe_icon` (icon decoding; TM's NE loader is `exe_icon_ne`) |
| pictures | `gfx/presenter` (layouts `windowsCd()` and `dos1990()`), `gfx/palette`, `gfx/rect.h`, `gfx/image_export` |
| text | `text/text_engine` (the TLC `Text_Render` of TM Windows and G&G) |
| DOS sound | `dos/audio/` speaker, SN76496 and OPL2 (ymfm) chips, `opl_writer.h`, `opl_output.h`, `opl_log`, `chip_renderer`, `chip_audio_device` |
| DOS input | `dos/input/` `bios_keyboard`, `pc_keyboard` (SDL to set-1 scan codes, typematic repeats), `int33_mouse`; `dos/app/script_input` (scripted input in ticks) |
| settings, mods | `save/ini_file`, `mods/json`, `mods/png_codec`, `mods/wav_file` |
| test helpers | `tests/app_test_util.h` (header-only) |

Rules:

* A core file includes only core files (plus the standard library, SDL and ymfm).
  `python -P tools/check_core_deps.py` follows every `#include "..."` of the files listed in
  `cmake/portcore.cmake` and fails on anything else; `--gg` also checks that `src/gg/` and
  `tests/gg/` include only core and G&G files. Run it after changing the core.
* Where the games differ, the core takes a parameter whose default is TM's behaviour, so TM's
  output stays byte for byte the same (checked against frame dumps, WAVs, OPL logs, the DOS
  fidelity suite and `tmtests` when the core was extracted):
  * `PitTimeBase(divisor)` / `VirtualPit(divisor)`: the PIT rate (TM DOS 0x13B1, G&G 0x2000),
    callbacks on every n-th counter value (G&G's sound tick on even ticks) and a writable counter
    (G&G's dialog time freeze); `dos::PitClock` is TM's time base on it.
  * `DeadlineTimers(clock, ExpiryRule)`: the unit is the clock's (`PitTickClock` counts PIT
    ticks), `expired()` is `>=` (TM) or `>` (`AfterDeadline`, G&G's `Timer_Expired`); `reached()`
    is the end of a wait in both games.
  * `dos::Int33Mouse(Int33MouseConfig)`: ranges (AX=07h/08h), the logical-to-driver x scale (TM
    x / 2, mode 13h x * 2) and an AX=0Ch event handler besides polling; TM's `dos::MouseDriver` is
    a subclass with its start positions.
  * `dos::ScanKeyboard`: the game's INT 09h handler is an interface (`ScanByteHandler`); TM's
    `PcKeyboard` overrides it with `KbdIsr`.
  * `dos::ChipRenderer(ChipRenderConfig, OplOutput*)` with a `SoundTickSource`: the chips ticked
    by any driver at `tickHz` (TM: every PIT tick; G&G: 72.83 Hz) with the OPL mix level;
    `ChipAudioDevice` plays it through SDL or offline and queues the game's calls (`post`).
    `OplWriteLog` / `OplLogTap` record OPL writes as `tick reg value` for `tools/dos/oplcmp.py`.
    TM's `DosSoundRenderer` (the sequencer as tick source) and `DosAudioDevice` are built on them.
  * `text::TextEngine(TextFont, TextCanvas*, TextDialect)`: `TextDialect::treasureMountain()`
    (TAB, FE and `FF 04` tab stops, '_' as an underlined blank, width from the left-most glyph)
    or `gizmos()` (none of these, width from x0, the centred start counts, the measure rule of
    `TextFont::runWidth`); TM's `TextRenderer` is the FFNT font and the frame buffer behind it.
* A file that mixes core and game parts is split, keeping the game part where it was (for
  example `platform/exe_icon` and `exe_icon_ne`, `save/settings` and `save/ini_file`,
  `mods/wav_codec` and `mods/wav_file`).

*Gizmos & Gadgets!* (`src/gg/`, namespace `tmport::gg`; `cmake/gg.cmake` with one source list per
area, `cmake/gg_sources_<area>.cmake`, and one test list, `cmake/gg_tests_<area>.cmake`; the map of
original modules to files is `re/gg/docs/ENGINE_MAP.md`):

| Dir | Contents |
|---|---|
| `gg/data/` | `GgExe` (`ssg.exe` with its overlays and DGROUP, `dgroup_offsets.h`), `GgFiles` (the game folder), the archives and LZH, resource parsers, the tools' shared argument helpers |
| `gg/save/` | `PLAYERS.DAT` byte for byte, `PlayersStore` (the engine's copy in the preference folder) |
| `gg/gfx/` | the 320×400 screen buffer and primitives, dirty tiles, sprites, the software cursor, VRAM, palette, DAC and fades, wipes, the movie, `Video` |
| `gg/text/` | the two fonts and the text renderer on the core's `TextEngine` |
| `gg/audio/` | BMF songs, the sound driver, the bank layer, music cues, the puzzles' sounds, `GgAudio` on the core's chips, the `.dro` comparison |
| `gg/input/` | the keyboard ISR and key ring, the INT 33h mouse handler, click zones, `Input_GetCode` |
| `gg/app/` | the clock and timers, `GgRuntime` (the yield points), `Seq_Run`, options and `ggport.ini`, the display on the runtime, the wave 2 preview, the tool dispatcher |
| `gg/ui/` | the TLC UI library: dialogs, buttons, scroll bar, menu bar, help, hints, text viewer, attract, sign-in |
| `gg/game/` | the world: picture cache, screen animations, actors, parts, rooms, HUD, building, hub, blueprint, puzzles |

Its runtime is TM Windows' model (nested blocking loops whose waits pump the engine) with PIT
ticks instead of milliseconds (`re/gg/docs/PORTING_NOTES.md` §2.2), not the DOS edition's lockstep.
`ggtests` find the game through `GG_DATA` (default: the repository's `GadgetsAndGizmos` folder) and
the DOSBox-X reference material through `GG_CAPTURES`, and skip what needs them otherwise.

## Runtime model

* **Nested blocking loops, as in the original** (`PORTING_NOTES.md` §3). Game code is ordinary
  nested functions on the main thread. Every wait primitive (`Input::getCode`, timer waits,
  `delayMs`, speech waits) calls `Engine::pump()`, which processes SDL events (translated to the
  original event/input model), delivers menu commands, services audio, and presents the frame
  (at most once per display refresh). Input that the original would have discarded is discarded.
* Non-local exits (`longjmp`) become exceptions `RestartToSignIn` / `QuitGame`, caught where the
  original called `setjmp` (`1118:004c`).
* Focus loss pauses the game (original freezes in the event fetch); window close → input `0x1B`.
* State: one plain struct per original module, grouped in `GameState`; keep the original's
  variable meanings and units so specs map line by line.

## Video

* `FrameBuffer`: 512×1055 bytes of **logical palette indices** (RRGB 900); layers at rows 0
  (screen), 384 (back page), 788 (pristine play-field). Colour key = logical index whose RGB is
  (0,0,0xAA). 16-colour art (TMT16) is optional and expanded to the same model.
* `Presenter`: logical canvas **640×480**, black, game area 512×384 at (64,48); in-engine menu bar
  drawn at the top while a climb is played. SDL streaming texture + logical presentation
  (letterbox; integer scaling option). F12 = PNG screenshot of the game area (512×384).
* `--screenshot-dir`, `--frame-dump` and scripted-input options exist for comparing with captures
  of the original (`extracted/captures/ref/`).

## Audio

* One `WaveVoice` (SDL audio stream, U8 mono at the SSND rate); a new sound cuts the current one.
* Music: own SMF parser/sequencer → `MidiBackend` interface. Back ends: WinMM `midiOut`
  (Windows default), SoundFont (TinySoundFont on a user-supplied General MIDI `.sf2`, rendered
  into an SDL stream on the wave voice's device; the Linux/macOS default when configured), null.
  `E*.MID` by default, `B*.MID` selectable.

## Data sources and saves

* Data path from `--data <dir|cue|bin|iso>` or `tmport.ini` next to the executable / in the pref
  path; auto-detect a `TMTWINCD` directory. Missing files → clear error dialog naming the file.
* Saves and settings in `SDL_GetPrefPath("TMPort", "TMPort")`; `users.dat` byte-compatible with
  the original, optional import from `C:\TLCWIN\TMTWINCD\users.dat`.

## Mod layer

Implemented in `src/mods/` on the hook points of `PORTING_NOTES.md` §7: the resource manager asks
the installed mod folders (`res/mod_overrides.h`) before it reads a DLL resource (PMAP → PNG,
SSND → WAV, OTHR → text or raw bytes, DAM → assembler text, BNDL → JSON, FFNT, RRGB), table files
(`TABLES/*.json`) are applied to the mutable copy of the `ExeTables`, and MIDI overrides sit in
front of the game's `SOUNDS` folder (`mods/mod_data_source.h`). `mods::applyMods` installs them
right after `GameFiles::open` (the game in `app/bootstrap.cpp`, the tools through
`cli/tool_mods.h`). The user documentation (folder layout, formats, constraints, tools) is
[`MODDING.md`](MODDING.md).

The 1990 DOS edition's mod layer is `src/dos/mods/` (namespace `tmport::dos`): `DosFiles::open`
applies the installed `DosOverrides` (`dos/data/dos_files.h`) to the checked original files, so the
game code reads modded bytes like originals; `dos::installDosMods` installs the folders at the DOS
start-up (`app/bootstrap.cpp`). PAK/PAN pictures go through PNG with the original packers' encodings,
sound files through a note text, `ALL_PUZS`/`PRG_DICT`/`HELP.DAT` through text, and the strings and
colour tables of `SST.EXE` are written into its DGROUP image (`DosExe::writeDs`) before any table is
read (MODDING.md, "The 1990 DOS edition").

## Testing

* `tmtests`: decoders vs the Python reference tools in `tools/assets/` (byte-exact RUND
  round-trip, BNDL trees, FFNT metrics, text layout), Borland RNG vectors, users.dat round trip.
* Visual: compare engine screenshots with reference captures of the original
  (`re/docs/REFERENCE_CAPTURES.md`).
