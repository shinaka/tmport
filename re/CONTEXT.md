# Treasure Mountain port — shared reverse-engineering context

Goal: reverse engineer *Super Solvers Treasure Mountain!* (Windows CD edition, "TMTWINCD",
Version 1.01, (c) 1990, 1992-95 The Learning Company) and re-implement it as a modern,
cross-platform C++ engine (Windows first, then Linux/macOS) that loads the **user's original
game files at runtime**. The DOS 1990 version is available as a secondary cross-reference only.

The same repository also hosts the port of *Gizmos & Gadgets!* (DOS, 1993): see the section
"Gizmos & Gadgets (DOS)" below.

## Ground rules (apply to every agent)

1. **Never modify** anything under `C:\Git\TM\TM-DOS`, `C:\Git\TM\TM-WIN` or
   `C:\Git\TM\GadgetsAndGizmos` (the user's originals).
2. **Clean-room-style port.** The engine must contain no game assets, no game text, and no
   copied decompiled code. It reads art/sound/text/tables from the user's original files at
   runtime. Specs may describe formats, algorithms, data layouts and behaviour; refer to game
   text by resource id / data-segment offset rather than pasting long passages (a few words to
   identify a string are fine).
3. Distinguish **observed** facts (bytes, decompiled code, decoded output) from **inferences**.
   Cite function addresses (e.g. `10e0:0447`) and file offsets for every claim.
4. Run Python as `python -P script.py ...` (keeps cwd off sys.path but sees user site-packages,
   where Pillow 13 is installed). Never `pip install` anything else without asking.
5. Put temporary junk in your own subfolder under `C:\Git\TM\extracted\scratch\<topic>\`.

## Layout

| Path | What |
|---|---|
| `TM-WIN\SSTM3x\CD\cd.BIN/.cue` | Original Windows CD image (ISO9660, MODE1/2352) |
| `extracted\wincd\` | Files extracted from the CD (`tools\extract_bin_iso.py`) |
| `extracted\wincd\TMTWINCD\` | The game: `TMTWINCD.EXE`, `TMT.DLL`, `TMT16.DLL`, `TMT256.DLL`, `TM0..8SOUND.DLL`, `SOUNDS\*.MID` |
| `extracted\res\<DLL>\<type>\<id>` | Every NE resource from the DLLs, raw (`tools\ne_resources.py`) |
| `extracted\SST_unpacked.EXE` | DOS version EXE after EXEPACK removal (`tools\unexepack.py`) |
| `TM-DOS\` | DOS version data files (`*.PAK`, `*.PAN`, `ALL_PUZS`, ...) |
| `re\win\ghidra_export\` | Ghidra 12.1.4 export of `TMTWINCD.EXE` (see below) |
| `re\dos\ghidra_export\` | Same for the DOS EXE |
| `re\docs\` | **Specs written by agents** (Markdown) |
| `tools\` | Python tools; `tools\assets\` for asset decoders |
| `tools\ghidra\ghidra_12.1.4_PUBLIC\` | Ghidra; run headless via `tools/ghidra_headless.sh` (bash) |
| `tools\ghidra_scripts\` | Ghidra Java scripts (`ExportAll.java`, `FindMoreFunctions.java`, `ApplyNames.java`; the VROOMM import scripts of the G&G project) |
| `re\ghidra_proj\` | Ghidra projects: `TMWin` (program `TMTWINCD.EXE`), `TMDos` (program `SST_unpacked.EXE`), `GGDos` (program `ssg.exe`, G&G) |
| `GadgetsAndGizmos\` | *Gizmos & Gadgets!* DOS original (read-only); its specs, tools and names are in `re\gg\`, `tools\gg\`, `re\names\gg\` (section "Gizmos & Gadgets (DOS)") |

## Spec documents (read in this order)

1. `re\docs\ARCHITECTURE.md` — whole-program synthesis. **Its §7.1 corrections override the topic
   docs** where they disagree.
2. `re\docs\PORTING_NOTES.md` — how the original maps onto the engine; must-keep behaviours.
3. Topic docs `re\docs\code_*.md`, `fmt_*.md`, `sys_*.md`, `dgroup_map.md` for algorithms.
4. `re\docs\GAPS.md` — what is still unknown.
5. `docs\DESIGN.md` — engine conventions (source layout, runtime model, rules).

## Ghidra export files (`re\win\ghidra_export\`)

The export now carries the analysts' names (functions like `Room_Run`, globals like
`g_coins`), so decompiled code reads close to source. Names come from `re\names\*.tsv`, which hold
exactly one canonical name per address (`re\docs\ARCHITECTURE.md` §7.2).

- `functions.tsv` — entry, name, size, callers, callees (863 functions, all named; ~98 KB of 112 KB code covered)
- `functions\SSSS_OOOO.c` — one decompiled function per file (address `SSSS:OOOO`)
- `decompiled.c` — all functions concatenated; `disassembly.asm` — per-function disassembly with bytes
- `imports.tsv` — Win16 imports (GDI/KERNEL/USER/MMSYSTEM/TOOLHELP, resolved by name), with call sites
- `segments.tsv`, `strings.tsv`

To run your own Ghidra script against the already-analysed program (e.g. to dump bytes, find
xrefs, or decompile with different options), write a Java GhidraScript into your scratch folder
and run (bash):

```
/c/Git/TM/tools/ghidra_headless.sh "C:\\Git\\TM\\re\\ghidra_proj" TMWin -process TMTWINCD.EXE -noanalysis \
   -scriptPath "C:\\path\\to\\your\\scripts" -postScript YourScript.java arg1 ... < /dev/null
```

Only one Ghidra process may open a given project at a time (the project is locked while open).
If you need Ghidra concurrently with other agents, copy `re\ghidra_proj\TMWin.gpr` + `TMWin.rep`
into your scratch folder first and point at the copy. Prefer reading the existing exports.
The REA MCP native tools are NOT usable in this session (Ghidra not wired into REA yet).

## Reference emulator (original game in DOSBox-X)

`tools\tmctl.py` drives the original game running under Windows 3.1 in DOSBox-X **without
taking focus from the user's desktop** (posted window messages only; window parked off-screen).
Commands: `start [--park]`, `stop`, `key enter esc up "text:Name" alt+o ...`, `shot OUT.png`
(exact 640x480 frame), `save N` / `load N` (save states), `turbo on|off`, `park`/`unpark`.
Keyboard only in the win and dos profiles — guest mouse input cannot be posted without grabbing the
user's mouse, so use the game's keyboard controls (the G&G profile types mouse commands to a guest
TSR instead; section "Gizmos & Gadgets (DOS)"). **Only one agent may drive the emulator at a time**; only use it if your
task explicitly says so. Never use `tools\wincap.py key` (that one does steal focus).
Captures go to `extracted\captures\` (`ref\` for curated reference frames).

**DOS profile** (1990 DOS edition): `tmctl.py start --profile dos [--machine ega|tandy|mcga|cga]
[--args "-E"] --park`, using `tools\dosbox_tmdos.conf`. C: is the writable copy
`extracted\dos_c\` (the game is in `C:\TM`; never mount `TM-DOS`). `tmctl` writes `C:\RUN.BAT` on
each start. Host sound is muted (`nosound=true`). Later commands follow the running profile;
`state save/load` use `extracted\captures\dos\savestates\` and `extracted\dos_c\TM\USERS.DAT`.
Raw slots 1-9 are shared with the Windows profile. The name box drops a shifted A (other shifted
letters work), so type names in lower case. Audio can be captured silently: `tmctl.py audio wav|opl|mtavi`. EGA shots are 640x400 (2x2 doubled); Tandy/CGA/MCGA shots are 320x200. Reference
frames, AVIs, states and USERS.DAT snapshots are in `extracted\captures\dos\`; see
`re\dos\docs\REFERENCE_CAPTURES.md`.

## DOS edition (1990, `TM-DOS\SST.EXE`)

Scoped as a possible second engine edition; start with `re\dos\docs\SCOPING.md` (decision report,
engine plan, waves), then `re\dos\docs\ARCHITECTURE.md` (program, rules, differences from Windows)
and the format specs `fmt_graphics.md`, `fmt_data.md`, `fmt_sound.md` in the same folder.
Wave 1 logic specs: `code_world.md` (grid, cells, camera, scenes), `code_player.md` (player,
animation scripts, search, end sequence, treasure room), `code_elves_layout_hints.md`, `code_ui.md`
(menus, boxes, help, title, sign-in, quiz, HUD, sound triggers), `code_runtime.md` (main loop,
timer, input, init) and `PORTING_NOTES.md` (runtime model, options, quirks).

- Decoders: `tools\dos\` (`pan.py`, `pak.py`, `dos_font.py`, `dos_gfx_common.py`, `dos_text.py`,
  `puzzles.py`, `users_dos.py`, `dos_sound.py`). They read `TM-DOS\` and unpack SST.EXE's EXEPACK
  in memory; outputs go to `extracted\previews\dos\` (side-by-side comparisons in `compare\`).
- Ghidra: project `re\ghidra_proj\TMDos` (program `SST_unpacked.EXE`), export
  `re\dos\ghidra_export\` (628 named functions), names in `re\names\dos\`.
- Addresses: `SSSS:OOOO` = file offset `(SSSS-0x1000)*16 + OOOO + 0x21F0` of
  `extracted\SST_unpacked.EXE`; DGROUP is segment `4d94`, `DS:XXXX` = file offset `0x3FB30 + XXXX`.
  Ghidra prints `MOV Sreg,r16` (opcode 8E) with its operands swapped.

## Gizmos & Gadgets (DOS)

*Super Solvers: Gizmos & Gadgets!* (DOS v1.0, 1993, The Learning Company) is a second game ported
in this repository: its own executable `ggport`, engine code in `src\gg\`, specs in `re\gg\docs\`,
tools in `tools\gg\`, names in `re\names\gg\`; tests find the game through the environment variable
`GG_DATA` (default: the repository's `GadgetsAndGizmos\` folder). The decision and the plan are in
`re\gg\docs\SCOPING.md` §9. The ground rules above apply unchanged.

- **The game**: `GadgetsAndGizmos\` holds the user's original files (`ssg.exe`, `gizmo000.dat` ..
  `gizmo004.dat`, `PLAYERS.DAT`, `ssg.bat`, `Read_1st.txt`, `ssg.ico`). **Never modify it**; tools
  open it read-only (it is git-ignored, like `TM-DOS\` and `TM-WIN\`). Anything that must write game
  files (DOSBox-X) uses the writable copy `extracted\gg_c\`; never mount `GadgetsAndGizmos\`.
  `gizmo004.dat` is not game data: the game writes it at run time as a screen save-under.
- **Read in this order** (all in `re\gg\docs\`; ARCHITECTURE's first table repeats it):
  `SCOPING.md` (decision report, overlap with TM, estimate, plan), `ARCHITECTURE.md` (program,
  segment and overlay map, game flow, platform layer, TLC UI library, saves, hints, relationship
  to TM), the format specs `fmt_data.md` (archives, compression, bundles, resource types, text,
  game tables, `PLAYERS.DAT`), `fmt_graphics.md` (pictures, palettes, fonts, DAM, movie) and
  `fmt_sound.md` (BMF songs), then the wave 1 logic specs: `code_runtime.md` (clock, start-up,
  timers, keyboard, mouse, zones, input codes, graphics, palette, fades, wipes, movie, memory,
  resources), `code_sound.md` (the BMF driver and bank layer), `code_ui.md` (fonts, text, dialogs,
  menus, help, text viewer, attract, sign-in, `PLAYERS.DAT` I/O, hints), `code_world.md` (states,
  hub, set-up, rooms, actors, player, chimps, the warehouse loop), `code_vehicles_races.md` (parts,
  HUD, workshop, races, results, movie), `code_puzzles_a.md` and `code_puzzles_b.md` (the puzzle
  framework and the eight puzzles); `PORTING_NOTES.md` (engine mapping, `rand()` order, timing,
  quirks to keep) and `REFERENCE_CAPTURES.md` (DOSBox-X profile, guest mouse, the round 1 and
  round 2 captures and their measurements). `overlay_map.tsv` is the overlay table.
- **Tools**: `tools\gg\` (`python -P`; `tools\gg\README.md` has the usage). Decoders: `ggarc.py`
  (archives, decompressor, bundles; the module the others share), `lpx.py` (pictures and
  palettes), `fnt.py` (fonts), `ggtext.py` (texts and hints), `dam.py` (DAM scripts), `movie.py`
  (the final movie), `bmf.py` (songs), `players.py` (`PLAYERS.DAT`) and `extract_all.py`
  (everything, with `index.tsv`); `python -P tools/gg/ggarc.py selftest` checks all 1897 archive
  entries. Ghidra: `vroomm.py` (overlay layout, flattened image), `ghidra_import.sh`,
  `ghidra_export.sh`. Captures: `mouse_tsr.py`, `capture_tour.py` (round 1), `capture_mem.py`
  (reads and patches game RAM in save states: variables, the `rand()` state and call counts, the
  puzzle kind at a door, part boxes), `capture_timing.py` (palette loads, fades, frame periods and
  gaps in AVIs; `.dro` grids) and `capture_r2.py` (replays the round 2 segments). The decoders and
  `vroomm.py`
  read `GadgetsAndGizmos\` (or `--data`, `GG_DATA`) read-only, refuse to write inside it, and write
  derived output only under `extracted\` (decoders: `extracted\previews\gg\`).

### Ghidra project

- **Project and export.** Project `re\ghidra_proj\GGDos` (program `ssg.exe`: the flattened image
  `extracted\gg\ssg_flat.exe` that `tools\gg\vroomm.py` writes). Export `re\gg\ghidra_export\`,
  with the same files as `re\dos\ghidra_export\`. Both are git-ignored.
  - Rebuild: `bash tools/gg/ghidra_import.sh` (about 2 minutes; details in `tools\gg\README.md`).
    After editing names: `bash tools/gg/ghidra_export.sh`.
  - The VROOMM import scripts in `tools\ghidra_scripts\`: `SetupOverlays.java` (overlay blocks,
    stub thunks), `FixRealModeRefs.java` (64-KB boundary and `CS:` references), `FixSwitches.java`
    (Borland switch tables), `DefineStrings.java`; `ApplyNames.java`, `FindMoreFunctions.java` and
    `ExportAll.java` are shared with the TM projects (their new options are off by default there).
  - The export has 1234 functions: 217 overlay stub thunks (stub:0020+5k; a thunk carries its
    target's name) and 1017 real functions, 982 of them named after wave 1 (all but the LZH and
    overlay-manager internals and one RTL helper). DGROUP has 617 labels.
  - The defects found by scoping are fixed: the 64-KB boundary in ovl10 and ovl27, the phantom
    `FUN_3f56_092d` (removed), the missing 12e0:0506 and 12e0:0820 (created), and the wrong `CS:`
    labels in the overlay manager.
- **Names.** `re\names\gg\*.tsv`, in the `re\names\dos` format: address, name, C signature or type,
  comment. One canonical name per address; each address is in exactly one file
  (`re\names\gg\README.md`).
  - Files: `rtl_bc30`, `ovrman`, `engine_lowlevel`, `engine_sound`, `ui_tlc`, `game_flow`,
    `game_world`, `game_puzzles`, and `dgroup` (labels only, never functions). The wave 1
    proposals were merged into them; the README records the renames (for example
    `Gfx_CopyFromBack` → `Gfx_SaveToBack`: it copies the screen **into** the back page).
  - Conventions: functions are `Module_Verb`; the 17 states of `Seq_Run` (table DS:00c2) are
    `State_NN_Role`; globals are `g_`, strings `s_`. A name that rests on inference says
    "(inferred)" in its comment. `_Noreturn` in a signature marks a non-returning function.
  - Docs and engine code use these names; add or change a name in the TSV first.
- **Addresses.** All of these were checked against the bytes of `ssg.exe`; verify against the file
  before relying on one.
  - Ghidra segment = image segment + 0x1000 (load segment 0x1000), as for TM DOS. The numbering is
    the scoping export's:

    | Ghidra segments | What |
    |---|---|
    | 1000..1d42 | resident code (1d42 = overlay manager; its data is 1dd7, the segment table 1de3) |
    | 1e0d..1e9f | overlay stubs |
    | 1ea7 | DGROUP |
    | 2a78 | stack |
    | 2a80..4140 | overlays |

  - Resident `SSSS:OOOO` (Ghidra 1000..2a7f): file offset in `ssg.exe` = `(SSSS-0x1000)*16 + OOOO +
    0x1200`. For example, 1000:0000 is C0's `BA A7 0E` at 0x1200, and 1454:0046 is the PIT set-up
    `B0 36 E6 43 B8 00 20` at 0x5786.
  - `DS:XXXX` = `1ea7:XXXX` = file offset `0xFC70 + XXXX` (DS:0004 holds "Borland C++ - Copyright
    1991"). Far pointers stored in DGROUP hold image segments (`0x0ea7` = DGROUP).
  - Overlay `SSSS:OOOO` (Ghidra 2a80..4140): file offset = the overlay's `file_offset` + `OOOO`, from
    its row in `re\gg\docs\overlay_map.tsv` (per overlay: number, stub segment, Ghidra stub and code
    segments, file offset, code size, fix-ups, entries and role; `python -P tools/gg/vroomm.py map
    --roles re/gg/docs/overlay_map.tsv --check` checks it against the exe). For example, 3d39:083a
    (the DAM opcode table) = 0x2F2E0 + 0x83A = 0x2FB1A.
  - Stub segments `1e0d`..`1e9f`: entry k of a stub is `stub:0020 + 5k` and jumps to an overlay
    function; cite the target, not the thunk.
  - Ghidra's bytes equal the file's except in two places: fix-up words (file: segment-table offset
    index*8; Ghidra: segment + 0x1000) and, in the resident stubs, the entries (file `CD 3F off 00`;
    Ghidra `EA off seg`).
- **Decompiler caveats.** Near CALL/JMPs that cross 0x30000 in ovl10 or 0x40000 in ovl27 still
  print Ghidra's page form in the operand (e.g. `CALL 0x3000:fe8d`); the reference comment and the
  decompiled C carry the real target. `CS:` operands have corrected references, but the decompiled C
  prints them with the page segment. Calls that pass a struct by value (`F_SPUSH`, all dialog calls)
  and some switch-bearing overlay functions must be read from the disassembly.

### DOSBox-X profile and mouse

- **G&G profile**: `tmctl.py start --profile gg [--args "-sb"] [--fresh] --park`, using
  `tools\dosbox_gg.conf` (vgaonly, 486 at fixed 26800 cycles, SB 2.0 OPL2, host sound muted,
  built-in INT 33h driver, autolock=false). As for the TM profiles, only the agent whose task says
  so may drive it.
  - C: is the writable copy `extracted\gg_c\`, with the game in `C:\GG`. It is copied from
    `GG_DATA` (default `GadgetsAndGizmos\`) on the first start or with `--fresh`; never mount the
    original.
  - `tmctl` writes `C:\RUN.BAT` on each start. It fixes the guest clock at 10/01/1993 09:00:00
    because the game seeds rand() from time() (`--clock` changes it; DOSBox-X's DATE needs
    slashes), and writes the guest clock to `C:\CLOCK.TXT`. It also writes `C:\MOUSEKEY.COM`, the
    TSR from `tools\gg\mouse_tsr.py`, and loads it before `SSG`.
- **Guest mouse**: `tmctl.py mouse move X Y | click X Y [left|right] | down/up [X Y] | drag X1 Y1 X2
  Y2 MS | wait MS | where`, in game pixels (320x200; the arrow tip lands exactly there).
  - Never post real mouse messages to DOSBox-X. This SDL1 build reads the host pointer
    (GetCursorPos), so a posted click lands where the real pointer is.
  - Instead, MOUSEKEY owns the game's INT 33h event handler (host mouse events are cut off) and
    takes commands as posted keys: F5..F9 plus 4 data keys, swallowed before the game's INT 9 sees
    them (`re\gg\docs\REFERENCE_CAPTURES.md` §2).
  - The TSR moves the game 86 paragraphs up (DGROUP at segment 1721; Ghidra segment S runs at
    S − 0x786), so state readers find DGROUP by the Borland signature at DS:0004
    (`python -P tools/gg/mouse_tsr.py status STATE`).
- **Captures**: states go to `extracted\captures\gg\savestates\` with `NAME.players`
  (`PLAYERS.DAT`). Shots are 640x400 (mode 13h, 2x2). AVIs are ZMBV at 70.0863 frames/s. Raw OPL
  `.dro` captures (DOSBox-X opens one at the first key-on and starts it with a register dump) and
  the wave 0 tour (`w0\`) are in `extracted\captures\gg\`; `tools\gg\capture_tour.py` replays
  the tour.
  - Round 2 (wave 1): states under `savestates\r2\`, shots under `r2\`, AVIs `avi\r2_*`, audio
    `audio\r2_*` (REFERENCE_CAPTURES §7-§13): all 8 puzzle kinds (the kind chosen by patching the
    `rand()` state at a door with `capture_mem.py door`), a race per building, the workshop, the
    final movie (reached by poking `g_racesWonTotal`), `-si` and `-sn` sound runs, menus, and a
    second player "Bob" (Without Chimps) with save and resume. The few state edits (door pokes,
    box moves and swaps, the races-won poke) are all listed in REFERENCE_CAPTURES §9.9.
  - `extracted\gg_c\GG\PLAYERS.DAT` now holds the players Ada and Bob; `tmctl.py start --fresh`
    restores the shipped file.
  - The warehouse rooms are not reproducible from a fresh boot: a walking chimp calls rand()
    whenever one of its probes touches an edge, pad, door or item, so runs depend on the pace.
    Start such runs from a state (without chimps the warehouse calls rand() never).

## Address conventions (Windows EXE)

- Ghidra maps NE segment N to selector `0x1000 + (N-1)*8`: segment 1 = `1000`, segment 56 = `11b8`.
- Segment 56 (`11b8`) is DGROUP (the automatic data segment, 26274 bytes initialised) at file
  offset `0x23c00` of `TMTWINCD.EXE`. `DAT_11b8_XXXX` ⇔ file offset `0x23c00 + XXXX`.
  Segment 55 (`11b0`) is a tiny 2-byte data segment.
- Segment `1248` holds Ghidra's import thunks.
- Compiler: **Borland C++ (1993; BC++ 4.x)** — C++ with RTTI/exceptions (`xalloc`, `string`,
  `TStringRef`, `typeinfo`). Far functions use the Windows prologue `inc bp; push bp; mov bp,sp`.
  Borland normally emits one code segment per source module, so a segment ≈ a `.cpp` file.
- Segments `1000` and `11a0` contain much of the Borland RTL (startup, stdio, string, math).
- Imports include WinG (`WING.DLL`, loaded dynamically by name), MMSYSTEM (wave/MIDI), TOOLHELP.

## What is already known about the data files

All DLLs are resource-only Win16 NE files. Each has an NE resource of type **15** that is a table
of 12-byte records `{u16 size=12; u8 kind(1..4); u8 0xFF; u16 id; char mactype[4]; u16 0}` mapping
resource ids to Macintosh-style 4-character types — the game was ported from the Mac edition and
emulates a Mac Resource Manager. Windows custom resource types 32513.. hold the payloads:

| DLL | Mac type(s) → Win type | Notes |
|---|---|---|
| `TMT.DLL` | `BNDL`→32513 (79), `FFNT`→32514 (1), `OTHR`→32515 (476), `RRGB`→32516 (1) | BNDL = lists of u32 resource ids; OTHR = misc game data (puzzles, word lists, sentences, tables); RRGB = palette (RGB triplets) |
| `TMT16.DLL` | `PMAP`→32513 (764) | 16-colour pixmaps, e.g. id 1000 = header `00 02 6c 01` then 4bpp data (512×364?) |
| `TMT256.DLL` | `PMAP`→32513 (764) | 256-colour pixmaps, same ids; compressed (`RUND` tag seen after 4-byte header) |
| `TM0..8SOUND.DLL` | `SSND`→32513 | header `u32 0; u32 rate (0x5622=22050); u32 nsamples` then 8-bit unsigned PCM |
| `SOUNDS\*.MID` | — | Standard MIDI files, `B*` and `E*` variants (base/extended GM maps?) |

NE resource lengths are rounded up to 512-byte alignment (shift 9), so payloads carry padding.
`TMTWINCD.EXE` itself has only cursors, an icon, and 8 dialogs as resources. Game text is mostly
in DGROUP. User data: `c:\tlcwin\tmtwincd\users.dat`, settings in `TMTWIN.INI`
(`[TMTWIN] Win40VersionNum, UseWinG, CheckDisplay, CheckSound, CDDrive`).

The runtime: 640×480, 256-colour (palette-managed, optionally WinG), 16-colour fallback using
TMT16.DLL. MIDI music via MCI sequencer with an AdLib/MVI/Thunder Board special case.
