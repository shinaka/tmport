# Treasure Mountain port — shared reverse-engineering context

Goal: reverse engineer *Super Solvers Treasure Mountain!* (Windows CD edition, "TMTWINCD",
Version 1.01, (c) 1990, 1992-95 The Learning Company) and re-implement it as a modern,
cross-platform C++ engine (Windows first, then Linux/macOS) that loads the **user's original
game files at runtime**. The DOS 1990 version is available as a secondary cross-reference only.

## Ground rules (apply to every agent)

1. **Never modify** anything under `C:\Git\TM\TM-DOS` or `C:\Git\TM\TM-WIN` (the user's originals).
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
| `tools\ghidra_scripts\` | Ghidra Java scripts (`ExportAll.java`, `FindMoreFunctions.java`) |
| `re\ghidra_proj\` | Ghidra projects: `TMWin` (program `TMTWINCD.EXE`), `TMDos` (program `SST_unpacked.EXE`) |

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
Keyboard only — guest mouse input is impossible without grabbing the user's mouse, so use the
game's keyboard controls. **Only one agent may drive the emulator at a time**; only use it if your
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
