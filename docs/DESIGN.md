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
* Targets: `tmport` (the game), `tmtests` (unit tests, doctest), optional tools.
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
| `text/` | FFNT font, `TextRenderer` (byte-exact port of `Text_Render`) |
| `anim/` | DAM engine (16 channels, frame table), modal animation player |
| `input/` | input-code queue, hot-spot table (50 slots), held keys, mouse, cursors |
| `timing/` | deadline timers (8 slots), clock |
| `audio/` | wave voice (single stream), speech wait, SFX, SMF sequencer + MIDI back ends, music manager |
| `ui/` | game dialogs (DlgDesc), pager, in-engine menu bar |
| `save/` | `users.dat` (byte-exact), settings, paths |
| `game/` | game modules (main state machine, title, sign-in, room, player, HUD, layout, elves, quiz, hints, net shop, coin pile, hall, cut-scenes, castle, celebration, help) |
| `rtl/` | Borland `rand`/`srand`/`R(n)`, `sprintf` subset |
| `platform/` | SDL3 app, window, event pump, audio device |
| `cli/` | command-line tools of `tmport` (`--info`, PMAP dumps, test frame, PMAP viewer) |

Namespace `tmport` (a global namespace `tm` is impossible: it collides with the C library's
`struct tm`, which `<ctime>` declares globally and `<format>`/`<chrono>` pull in). Files
`snake_case.cpp/.h`, types `PascalCase`, functions/members `camelCase`,
constants `kName`. One original module ≈ one engine file; cite the original address in a short
comment at each ported routine (e.g. `// 1110:00aa CoinPile_Update`).

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

## Mod layer (hook points, implemented incrementally)

`ResourceManager::find(type, id)` checks `mods/<TYPE>/<id>.<ext>` first (PMAP → PNG, SSND → WAV,
OTHR → raw bytes, BNDL → JSON, MIDI → `.mid`); `ExeTables` accepts JSON table overrides. See
`PORTING_NOTES.md` §7.

## Testing

* `tmtests`: decoders vs the Python reference tools in `tools/assets/` (byte-exact RUND
  round-trip, BNDL trees, FFNT metrics, text layout), Borland RNG vectors, users.dat round trip.
* Visual: compare engine screenshots with reference captures of the original
  (`re/docs/REFERENCE_CAPTURES.md`).
