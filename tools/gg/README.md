# Tools for Gizmos & Gadgets (DOS v1.0)

Tools for *Super Solvers: Gizmos & Gadgets!* (DOS v1.0, 1993, `ssg.exe`). They read the
user's own game folder at run time and contain no game data or text. Run the Python tools with
`python -P` (Pillow is needed for PNG output). The formats are specified in
`re/gg/docs/fmt_data.md`, `fmt_graphics.md` and `fmt_sound.md`.

Three groups: the [decoders](#decoders-wave-0) of the data files, the [Ghidra project](#ghidra-project)
(`vroomm.py`, `ghidra_import.sh`, `ghidra_export.sh`) and the
[reference captures](#reference-captures-dosbox-x) in DOSBox-X (`mouse_tsr.py`,
`capture_tour.py`, and for round 2 `capture_mem.py`, `capture_timing.py`, `capture_r2.py`).

Common to all tools:

* `--data DIR` selects the game folder; otherwise `GG_DATA`, otherwise `GadgetsAndGizmos\`
  under the repository root. The folder must hold `gizmo000.dat`; `ssg.exe` is read for the
  base palette, the movie cues and the default distance table check when present.
* The tools never write into the game folder (they refuse an output path inside it). Default
  output: `extracted\previews\gg\` (ignored by git).
* Resources are named by hex id (`1002`) and bundle members by id and member number
  (`0001/3`).

## Decoders (wave 0)

| Tool | What |
|---|---|
| `ggarc.py` | shared module: data folder, `MRn` archives, the method-3 decompressor, bundles, resource typing; CLI `selftest`, `list`, `info`, `get` |
| `lpx.py` | LPX pictures and palette resources to PNG; the picture/palette pairing; `--check` |
| `fnt.py` | the two `fnt` fonts to a labelled glyph grid, a metrics TSV and sample text |
| `ggtext.py` | text, hint and string resources with their markup made readable |
| `dam.py` | DAM animation scripts: listings, one picture per tick |
| `movie.py` | the final movie's 202 frames to PNG, the cue table |
| `bmf.py` | BMF songs: event listings, the speaker/FM bank pairs, `--check` |
| `players.py` | `PLAYERS.DAT`: header, index, one line per record, a record's fields |
| `extract_all.py` | everything above for every resource, with `index.tsv` and `summary.txt` |

## Usage

```
python -P tools/gg/ggarc.py selftest [--exe [SSG.EXE]] [-v]
python -P tools/gg/ggarc.py list [--vol N] [--leaves]
python -P tools/gg/ggarc.py info ID
python -P tools/gg/ggarc.py get ID OUT [--raw]

python -P tools/gg/lpx.py PATH [--pal ID ...] [--out FILE] [--opaque] [--scale N]
python -P tools/gg/lpx.py --palettes [--out DIR]
python -P tools/gg/lpx.py --check

python -P tools/gg/fnt.py [ID ...] [--out DIR] [--scale N] [--text STRING] [--check]
python -P tools/gg/ggtext.py PATH [PATH ...]
python -P tools/gg/dam.py [ID ...] [--render [--out DIR] [--pal ID]] [--check]
python -P tools/gg/movie.py [--out DIR] [--frames 0,5,10-20] [--pal ID ...] [--blank]
                            [--sheet] [--cues] [--check]
python -P tools/gg/bmf.py PATH [--events] | --all [--out FILE] | --banks | --check
python -P tools/gg/players.py [FILE] [--record N | --index]

python -P tools/gg/extract_all.py [--data DIR] [--out DIR] [--no-png] [--no-movie]
```

Examples:

* `ggarc.py selftest` decodes all 1897 entries and checks each one: decoded size equals the
  stated size, no read past the entry, no match before the start or past the end, complete
  Huffman codes, and the 0xA55A word at the end of each volume. It prints one line, for the
  shipped files: `ggarc selftest: 4 volumes, 1897 entries (1490 method 3, 407 stored),
  5753687 bytes unpacked, largest 64014; ...; 0 errors`. Exit code 1 on any error. `--exe`
  also re-reads the default distance table DS:13D0 from `ssg.exe` and decodes with it.
* `lpx.py 1002` renders the title town with its palette; `lpx.py 00ed` the parts of level 0
  as a sheet; `lpx.py 1157 --scale 2` the frames of a DAM script. The palette used is printed
  (`base+0034+00ed/5`: base palette from `ssg.exe`, then 0x34, then the bundle's member 5).
* `bmf.py 0027 --events` lists the workshop's speaker music bank; `bmf.py --banks` the 27
  speaker/FM pairs.
* `extract_all.py` writes `raw/`, `png/`, `pal/`, `text/`, `bmf/`, `dam/`, `fonts/`, `movie/`,
  `gizmo004.png`, `index.tsv` (one row per entry and bundle member: id, volume, file offset,
  type, method, sizes, kind, details including the palette of each picture, output files) and
  `summary.txt` (counts per type and kind, text markup statistics, errors). It takes about
  15 s and must report `errors: 0`.

Checks (each prints a summary and exits 1 on errors): `ggarc.py selftest`, `lpx.py --check`
(every picture decodes to exactly its bytes, every palette fits), `fnt.py --check`,
`dam.py --check`, `movie.py --check` (every frame ends at its terminator), `bmf.py --check`
(every song parses to its end event).

Library use from other scripts:

```python
import sys; sys.path.insert(0, 'tools/gg')
import ggarc, lpx
arc = ggarc.Archive()                 # --data / GG_DATA / default folder
for leaf in ggarc.walk_leaves(arc):   # entries and bundle members, with their kind
    if leaf.kind == 'picture':
        image, pic, note = lpx.render_leaf(arc, leaf)
```

## Ghidra project

`bash tools/gg/ghidra_import.sh` (Git Bash) rebuilds the Ghidra project from your `ssg.exe` and
exports it, in about two minutes. It reads `$GG_DATA/ssg.exe` (default
`GadgetsAndGizmos\ssg.exe`), deletes any old project first and writes only to folders git
ignores. After you edit `re/names/gg/*.tsv`, run `bash tools/gg/ghidra_export.sh` (about a
minute) to re-apply the names and re-export. Only one Ghidra process may open the project at a
time.

| Step | What runs | Output |
|---|---|---|
| 1 | `python -P tools/gg/vroomm.py flatten` | `extracted/gg/ssg_flat.exe`; set-up tables in `extracted/gg/ghidra_setup/` (`blocks.tsv`, `entries.tsv`, `overlays.tsv`, `farcode.tsv`, `strings.tsv`) |
| 2 | headless import with analysis: SetupOverlays and ApplyNames before analysis; FixRealModeRefs, FixSwitches, FixRealModeRefs and `FindMoreFunctions enter jmp16 afterdata` after it | `re/ghidra_proj/GGDos` (program `ssg.exe`), log `re/gg/ghidra_headless.log` |
| 3 | headless run without analysis: FixRealModeRefs, ApplyNames, DefineStrings | log `re/gg/ghidra_headless_finish.log`, reports `ghidra_setup/realmode_fixes.tsv` and `switches.tsv` |
| 4 | `tools/gg/ghidra_export.sh` | `re/gg/ghidra_export/` (same files as `re/dos/ghidra_export/`) |

**Why the image is flattened.** `ssg.exe` is a Borland C++ 3.0 program. It has 35 VROOMM
overlays (33 with code), stored after the MZ load module and loaded on demand through INT 3Fh.
Ghidra's MZ loader sees only the load module. `vroomm.py` writes a new MZ header for the
resident image (with the same 976 relocations) and appends each overlay, paragraph-aligned, at
its own segment: Ghidra 2a80..4140, in overlay order. Ghidra does not relocate bytes after the
load module, so the overlays are written pre-relocated for load segment 0x1000:

* every fix-up word (in the file, a segment-table offset, index*8) becomes that segment + 0x1000;
* every stub entry `CD 3F off 00` becomes `EA off seg` (JMP FAR), so a far call to stub:0020+5k
  reaches the overlay function through a 5-byte thunk.

The numbering is identical to the scoping export, so its addresses stay valid.
`python -P tools/gg/vroomm.py info` prints the layout.
`python -P tools/gg/vroomm.py map --roles re/gg/docs/overlay_map.tsv --check` checks the overlay
map against the exe.

**Ghidra scripts** (`tools/ghidra_scripts/`):

| Script | Role |
|---|---|
| `SetupOverlays.java` | overlay blocks; block names and kinds from `blocks.tsv`; one non-executable DGROUP block 1ea7:0000..bd0f; DS = 1dd7 in the overlay manager 1d42; functions at the 217 stub thunks and their targets, and at the far code pointers that the relocation table shows in DGROUP; turns off the decompiler's switch analysis |
| `ApplyNames.java` | names from `re/names/gg`. Named entries become functions before analysis. A `_Noreturn` signature (exit, _exit, abort, _terminate, _c0_OvrRuntimeError) is applied first, so disassembly stops after calls to them |
| `FixRealModeRefs.java` | near CALL/JMP across a 64-KB linear boundary, and CS-relative operand references (see below) |
| `FixSwitches.java` | reads the 52 Borland switch tables (dense and sparse; 452 targets) |
| `FindMoreFunctions.java` | prologue scan. The options `enter` (ENTER n,0), `jmp16` (prologue after JMP rel16) and `afterdata` (prologue after a switch table) are off by default, so the TM projects are unaffected |
| `DefineStrings.java` | the DGROUP strings, "gizmo.exe" in the overlay manager's data, and the overlay error text at 1000:0264, which has no NUL and ends at code |
| `ExportAll.java` | the export. Callers and callees are sorted, so two exports of the same project are identical |

**Ghidra artefacts the recipe corrects** (all were in the scoping export):

* Ghidra's x86 real-mode language takes the 64-KB page of a near rel16 target, and the CS of a
  `CS:` operand, from the linear address. The two overlays that cross a page boundary are 2fcb
  (crossing 0x30000) and 3f56 (crossing 0x40000).
  * 11 near branches in them resolved into the wrong page. One of them, 2fcb:0552 (the call of
    the rank-up message 2fcb:01dd), created the phantom function `FUN_3f56_092d` inside
    3f56:0907, which then ended after 35 bytes.
  * FixRealModeRefs gives each such branch an overriding reference to the real target. The
    decompiler uses that reference; the operand text still shows Ghidra's page form, e.g.
    `CALL 0x3000:fe8d ; -> Result_RankUpMessage`. It also removes the phantom and
    re-disassembles.
  * It moves the `CS:` operand references to the code segment: 1d42:0015 `MOV DS,CS:[3]` now
    refers to 1d42:0003, not 1000:0003. The decompiled C still prints the page segment.
* Headless Ghidra re-analyses whatever the post-scripts changed, and that re-creates the
  page-based `CS:` references. This is why step 3 runs without analysis.
* 12e0:0506 and 12e0:0820 (states 10 and 4) are reached only through the state table DS:00c2.
  They are now functions, via `farcode.tsv`.
* `abort` no longer runs into the "Runtime overlay error" text. The routine after it, at
  1000:027b (the overlay manager's fatal-error exit), is now a function.

**Result:** 1234 functions (217 stub thunks and 1017 real ones; the scoping export had 1205 and
988), 453 of them named in wave 0 and 982 after the wave 1 merge (`re/names/gg/README.md`), plus
155 DGROUP labels in wave 0 and 617 after wave 1. Functions cover 147 584 of 155 072 executable
bytes (95.2 %; scoping 92.6 %). The function entries equal the scoping export's, minus
3f56:092d and plus 30 new ones. Two rebuilds from scratch give identical exports.

## Reference captures (DOSBox-X)

| Tool | What |
|---|---|
| `mouse_tsr.py` | builds MOUSEKEY.COM, the guest TSR behind `tmctl.py mouse` (INT 33h/21h/9 hooks, key protocol): `build OUT`; `keys X Y ACTION` prints a command's keys (ACTION move, ldown, lup, rdown, rup; X in INT 33h units, 0..639); `status STATE` prints the TSR's and the game's mouse variables from a DOSBox-X state |
| `capture_tour.py` | replays the wave 0 tour in DOSBox-X through tmctl: `run door OUTDIR` (fresh boot to the first puzzle, with an AVI and a .dro of the title music), `run puzzle OUTDIR` (from the state w0/17_puzzle to the hub), `list SEGMENT` (the steps) |
| `capture_mem.py` | reads and patches the guest RAM of a G&G save state (DGROUP found by its signature): `vars`, `peek`, `poke` / `pokew` / `poked`, `diffds` (two states), `rand` / `calls` (the LCG forwards, and the number of `rand()` calls between two states), `seedfor` / `seedpuz` / `door` (a `rand()` state that deals a chosen puzzle kind at a door), `boxes` / `boxset` (the part boxes of the building's rooms). Reads states; writes new `.sav` files only where told |
| `capture_timing.py` | measurements on round 2 AVIs and `.dro` logs: `info`, `palette` (palette loads and fades), `pace` (the frame period of a screen box), `gaps` (runs of identical frames), `dro` (the OPL write grid) |
| `capture_r2.py` | replays the round 2 segments in DOSBox-X through tmctl: `list`, `run SEGMENT OUTDIR [--av]` (puzzle_k0..k7, the three races, the movie, the workshop, the resume), `compare DIR_A DIR_B` |

They drive or read DOSBox-X only; `tools/tmctl.py --profile gg` and
`re/gg/docs/REFERENCE_CAPTURES.md` describe the profile, the mouse protocol and the captures
(round 1 §1-§6, round 2 §7-§13). `capture_mem.py` and `capture_timing.py` only read files and need
no emulator. Only start DOSBox-X when your task says so.
