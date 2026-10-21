# DOS edition backend: scoping report

Should tmport get a second "edition backend" that runs the original 1990 DOS game (SST.EXE, the
16-colour EGA/Tandy art) in the same engine as the finished Windows CD edition? This report brings
together the four investigation passes (graphics, data and sound, code, captures) and adds a
check of its own. Details are in the companion docs: [ARCHITECTURE.md](ARCHITECTURE.md) (program
and rules), [fmt_graphics.md](fmt_graphics.md), [fmt_data.md](fmt_data.md),
[fmt_sound.md](fmt_sound.md) and [REFERENCE_CAPTURES.md](REFERENCE_CAPTURES.md).

**Short answer:** yes, it can be built. Every data format is decoded, and the decoders already
rebuild real game screens from your files pixel for pixel. The cost is the game logic: the DOS
game plays differently (one long scrolling path, not 50 rooms), so only about a fifth of the
Windows game code carries over. Estimate: about **two thirds of the Windows port's effort**, in
four waves of at most 5 agents plus an optional polish wave (§7).

Tags: **[O]** observed (bytes, code, decoded output, pixel comparisons), **[I]** inferred.

---

## 1. What the DOS edition is

| | DOS edition (1990, "V 1.0") | Windows CD edition (1992-95, v1.01) |
|---|---|---|
| Program | `SST.EXE`, 115 011 bytes, Turbo C 2.0 large model, EXEPACK-packed (288 992-byte image) [O] | `TMTWINCD.EXE`, Win16 NE, Borland C++ 4.x |
| Code | 628 functions, 82 KB; game logic about 400 functions / 61 KB / 10 000 decompiled lines [O] | 863 functions, about 112 KB |
| Data | 19 files, 667 KB in all (`TM-DOS\`) [O] | about 119 MB (DLL resources, MIDI, speech) |
| Screen | 320×200, 16 colours (EGA mode 0Eh with doubled pixels, Tandy mode 09h, MCGA mode 13h; CGA 4 dithered colours). Pixels are 1.2:1, so the intended picture is 4:3 [O] | 640×480 window, 512×384 game area, 256 colours (16-colour fallback) |
| Look per adapter | EGA and Tandy are identical; MCGA uses softer colours from its own table; CGA is dithered to 4 colours [O] | one look |
| Art | 4-bit tiles and sprites: 5 scenery PANs of 128×128 blocks (297 blocks), 1 title PAN, 4 sprite PAKs (524 frames), 1 bitmap font inside the EXE [O] | 764 PMAPs per colour depth, DAM animations |
| Sound | PC speaker, Tandy/PCjr 3-voice PSG, IBM PS/1 audio card, AdLib (OPL2) music; 3 tunes, 12 effects; no speech [O] | MIDI music, 22 kHz wave effects, speech |
| Text | 360 elf questions (`ALL_PUZS` + `PRG_DICT`), help pages (`HELP.DAT`); all other text and tables inside the EXEPACKed EXE [O] | text in DGROUP and OTHR resources |
| Saves | `USERS.DAT`: 3 + 9 × 360 bytes, 9 players [O] | `users.dat`: 7 × 1583 bytes |
| Timing | PIT at 236.7 Hz; game logic at 47.3 Hz inside the timer interrupt, at most one step per drawn frame; the frame loop runs free [O] | 10 Hz room ticks |

## 2. Format status

| Area | Status | Spec | Decoder (`tools/dos/`) |
|---|---|---|---|
| PAN scenery and title (map RLE + A5-escape tile RLE) | **decoded**, all 6 files exact | fmt_graphics §3 | `pan.py` |
| PAK sprites (offset table, raw/run rows, hotspots, blitter flags) | **decoded**, all 524 frames exact; header words 62/58 and frame words +0A..+0E unexplained (not used for drawing) | fmt_graphics §5 | `pak.py` |
| Font (128 glyphs in SST.EXE 1000:2447) and text layout (`Text_Draw` 43cb:0b63) | **decoded**; byte 0 of the font unexplained | fmt_graphics §6, fmt_data §6 | `dos_font.py` |
| Palettes (EGA, Tandy, MCGA, CGA dither) and video modes | **decoded**; present-to-VRAM routines only outlined (not needed by a port) | fmt_graphics §2 | `dos_gfx_common.py` |
| World grid (16×16 cells of 128×128 px), scene to PAN mapping | **decoded**; row use per level confirmed by capture matching in this pass (§3) | ARCHITECTURE §4, fmt_graphics §4 | (scratch `world.py`) |
| TUNES/SFX (.IBM speaker, .TAN PSG), sequencer tick, envelopes | **decoded**, rendered to 30 WAVs; output not yet checked against a recording | fmt_sound §4-6 | `dos_sound.py` |
| AdLib music path (24 instruments, 4 random arrangements per tune) | **partial**: tables and note mapping known, register order inferred, not rendered | fmt_sound §5.5 | `dos_sound.py adlib` (tables only) |
| ALL_PUZS + PRG_DICT, level ranges, deal and history | **decoded** (360 questions; 326 identical to Windows, 33 edited, 1 dropped) | fmt_data §2 | `puzzles.py` |
| HELP.DAT | **decoded** (pages); page picture tables listed | fmt_data §3 | `dos_text.py help` |
| USERS.DAT | **decoded**; a few fields (+0x08A, +0x124, +0x130, +0x133, header bytes 1-2) described by use, not meaning (all explained since wave 1/2: fmt_data §4) | fmt_data §4, ARCHITECTURE §6 | `users_dos.py` |
| DGROUP (hints, menus, dialogs, word lists, ranks, tables) | **mapped** region by region; some code-pointer and word tables only labelled | fmt_data §7 | `dos_text.py exe` |
| School-network and disk files (`ssrnet.dat`, `ssrquiet.dat`, `TLCINFO.DAT`) | **understood**: inert in your install; a port ignores them | fmt_data §5 | – |

Nothing is unknown that blocks an engine; the open points are listed in §8.

## 3. Verified in this pass

The decoders were run on the files in `TM-DOS\`, and full screens were rebuilt from them with the
draw rules traced in the code, then compared pixel for pixel with the DOSBox-X EGA captures.
Scripts: `extracted/scratch/dos_scoping/` (`world.py`, `screens.py`, `render_check.py`,
`palette_check.py`, `compare.py`); outputs in `extracted/previews/dos/compare/`.

| Screen (capture) | Rebuilt from | Differing pixels |
|---|---|---|
| Club sign-in (`ega/004_signin`) | OPENING.PAN, BEGIN frame 31, roster from USERS.DAT, labels from SST.EXE, font, `SignIn_DrawPanel/DrawList/MoveHighlight` 47c7:00e7/01e0/071a | **0 / 64 000** [O] |
| New game, clubhouse (`ega/008_shack`) | frame and menu bar, MOUNTAIN.PAN at camera (0xC0,0), status bar, player (as the graphics pass) | **0** [O] |
| Elf question (`ega/030_quiz_question`) | scenery and HUD at the catch position, SPRITES 57/94/103, question 61 of ALL_PUZS expanded with PRG_DICT, answer order from DS:5E9C, `Quiz_Draw*` 46ce:0271-052d | **0** [O] |
| Same sign-in, Tandy palette (`tandy/06_signin`) and MCGA palette (`mcga/04_signin`) | as above | **0** each [O] (MCGA needs rounded 6-to-8-bit colour expansion, see §8) |
| Title (`ega/001_title`) | OPENING.PAN + BEGIN frame 3 | 479: the "Press Enter" line and sparkles, not drawn [O]. Wave 2: the rest was a BEGIN.PAK sparkle (slot 2, script 0x5C; a z tie goes to the higher record) plus the mouse pointer (slot 0, BEGIN frame 2); with them every title capture is **0 px** |
| 10 path and castle screens (levels 1-3, castle hall, ladder shaft) | scenery at the camera found by tile matching, hiding places and castle layout restored from the saved USERS.DAT, HUD | 1 401-2 242 each, all on actors (player, elves) and clue words [O]. (The "locked-gate overlay drawn by a cell handler" of this pass does not exist: `ega/042` was taken before the trunk was unlocked, i.e. with `picture[0xEF]` = block 0x40, wave 2.) The composed screens of wave 2 (`dos_screens.h`) are 0 px |
| Castle top (`ega/101_castle_top_treasure_room`) | CLOSE.PAN at the matched camera, HUD | 7 229: the chest, the creature and the player are actors, and the menu bar is drawn differently in that scene [O] |

Capture matching places the screens in the 16×16 cell grid where ARCHITECTURE §4.2 predicts [O]:
level 1 = MOUNTAIN.PAN rows 14-15, level 2 = MOUNT2N3.PAN row 11, level 3 = MOUNT3.PAN rows 7-8,
castle hall = MOUNT3.PAN row 7 (columns 1-3) with the ladder shaft in rows 4-6 above it, castle
top = CLOSE.PAN row 10 from column 8. The clubhouse interior appears with identical pixels in
MOUNTAIN row 0 and SHACK row 9.

Other claims re-checked [O]: 6 PANs / 223+42+133+126 PAK frames decode exactly; font 128 glyphs,
height 10; tune lengths 53.00/71.81/24.34 s (IBM); 360 puzzles with levels {0,63,180,303,360};
USERS.DAT 3243 bytes; PIT divisor 0x13B1 at 1000:03f8; the EGA and Tandy tables give the same
colours; DS:0004 holds the Turbo C copyright; 628 functions in the export.

Comparison images for you (DOS capture | DOS rebuilt | difference | Windows capture, DOS shown at
4:3): `01_title`, `02_signin`, `03_game_start_clubhouse`, `04_mountain_path_level1`, `05_quiz`,
`06_castle_climb`, plus `dos_sprites_contact_sheet.png` (403 frames).

## 4. Gameplay differences that matter

From the code pass (ARCHITECTURE §8). The DOS game plays differently, not only at another
resolution:

* **World:** one long horizontally scrolling path per level (256 cells of 128×128 px), with
  ladders and doors between path rows, holes you slide down through tunnels, path links, and jumps
  over gaps. Windows has 50 fixed rooms with exits.
* **Hiding places:** at most one per cell, 19/14/11 per level, generated each game from picture
  attributes (one key matching all 3 clue words, treasures matching 2). Windows reads 18 fixed spots
  per level from layout records.
* **Progress per level is kept for the whole game**; you can fall back a level and climb again.
  Windows resets clues and key at each level and is one-way.
* **Nets:** a swing is free; each catch uses a net, but catches resolved in the same tick share
  one (`code_elves_layout_hints.md` §4.8). Net shop: 4 coins for **5** nets (Windows 10).
* **Coins:** start with **5** (Windows 10); a coin creature (+2: a coin with legs that appears at a
  view edge when coins < 4 and nets < 5) and a one-time bonus spot (+5); no rescue coin pile.
* **Elves:** 9/11/13/15 by rank tier (Windows 6/8/10/12), four kinds, different catch zone.
* **Quiz:** promotion at **70 %** right first time (Windows 80 %); missed questions come first on
  the next level.
* **Castle and end:** the castle wall is part of the scrolling world; treasures fly into a chest
  that fills to your rank, then the treasure room; crown sequence at 300.
* **Same:** rank thresholds and names, treasures per level 2/3/4/5, clue logic, quiz rewards
  +2/+1/0, castle ladder layout choice, Champion at 300.

Because the world differs, "the DOS look on the Windows game" is not possible as a reskin: the
DOS scenery is a different map, not repainted rooms. The existing mod layer could only swap
individual sprites.

## 5. Engine plan: one engine, two editions

Follow docs/DESIGN.md: namespace `tmport`, one original module ≈ one engine file, idiomatic C++
from the specs, original addresses in comments, no game data in the repo.

### 5.1 Edition detection

* New `src/edition/edition.h/.cpp`: `enum class Edition { WinCd, Dos1990 }`,
  `detectEdition(path, optional preferred edition)`, `scanEditions(path)` and
  `checkDosFolder(dir)` (as implemented in wave 2; detection works on a path, not a data source).
* **Dos1990** when the folder holds `SST.EXE`, `SPRITES.PAK` and `ALL_PUZS`. Validate before using
  fixed offsets: MZ, EXEPACK signature, unpacked image of 288 992 bytes, Turbo C copyright at
  DS:0004, EGA palette sanity check (as `tools/dos/dos_gfx_common.py`). Anything else is reported as
  an unsupported DOS version.
* **WinCd** as today (`TMTWINCD.EXE`, or a `.cue`/`.bin`/`.iso`).
* Both present (e.g. a parent folder with `TM-DOS\` and `TMTWINCD\`): `--edition dos|win` or
  `[engine] edition =` in `tmport.ini`; otherwise a clear error that names both. Auto-detection
  also looks for a folder that contains `SST.EXE`. `scripts/package.*` learn to copy a DOS folder.

### 5.2 Shared as is, or with a small generalisation

| Part | Today | For two editions |
|---|---|---|
| `util/`, `vfs/` (directory source), `rtl/` | done | as is. Turbo C 2.0 `rand` is the same LCG as Borland's (`rtl/borland_rand`) |
| `platform/` window, SDL pump, fullscreen, focus pause, screenshots | inside `Engine(GameFiles&, ColorMode, …)` | split an edition-neutral `platform/host.h` (now the engine's `mischief/platform/host.h`) (window, renderer, event pump, clock, audio device, screenshot) out of `Engine`; the Windows `Engine` keeps its message/input-code model on top of it |
| `gfx/presenter` | fixed 640×480 canvas, 512×384 game area | a `PresenterLayout` (canvas, game rect, pixel aspect). DOS: 320×200 shown at 4:3 (pixel aspect 1.2), letterboxed; integer option 5×6 = 1600×1200 (exact 4:3); "square pixels" option |
| `timing/clock`, `input/` SDL event translation | Windows input codes | `Clock` as is; the SDL key/mouse capture is reused, the DOS translation is new (§5.3) |
| `audio/` | wave voice + MIDI back ends on SDL | reuse the SDL device/stream plumbing (`sdl_wave_output`); MIDI/SoundFont are Windows-only |
| `save/` paths and settings | `users.dat`, `tmport.ini` | paths and settings as is; DOS saves in their own file under the pref path |
| `mods/` | PMAP/SSND/OTHR/DAM/BNDL overrides | folder discovery, PNG and JSON codecs reusable; DOS override hooks optional (wave 5) |
| `cli/` dispatcher, `--keys`, `--headless`, `--frame-dump` | Windows tools | dispatcher as is, DOS tools added; scripted input and frame dumps shared |
| tests and fidelity pipeline | `tmtests` + `TM_DATA`, reference captures, `tmctl.py` | same pattern with `TM_DATA_DOS`; DOSBox-X DOS profile already exists (`tmctl.py --profile dos`) |
| `app/bootstrap` | opens Windows data | detect the edition, then call `wincd` or `dos` start-up |

### 5.3 New per edition (`src/dos/`, namespace `tmport::dos`)

| Folder | Contents (original module) |
|---|---|
| `dos/data/` | `exepack` (in-memory unpack, the algorithm of the engine's `tools/unexepack.py`), `dos_exe` (image, DGROUP and far-pointer resolution, code segment 1000 for the font), `dgroup_offsets.h`, `file_table` (DS:0476), `puzzles` (ALL_PUZS + PRG_DICT), `help_file` (HELP.DAT) |
| `dos/gfx/` | `pan`, `pak`, `palettes` (EGA, Tandy, MCGA, CGA dither), `back_buffer` (320×200 logical colours, fills, colour swap, dirty spans, scroll and tile-column ops of segment 1000), `blitter` (`Sprite_Draw` 1000:1b8d flags, clip window) |
| `dos/text/` | `dos_font`, `text_draw` (43cb:0b63, 43cb:0cfc) |
| `dos/audio/` | `sequencer` (491b, run at 236.696 Hz by sample count), `speaker_synth`, `psg_synth` (SN76496), later `opl2_driver` (4ac9) on a pinned BSD-licensed OPL2 core (ymfm) |
| `dos/input/` | `keyboard` (held-key bits like `Kbd_ISR` 43a0:0071, diagonal = jump, Tandy codes, BIOS-style buffer for menus and names), `mouse_zones` (4a85) |
| `dos/save/` | `users_dat` (3 + 9 × 360 bytes, byte exact, import from the DOS folder's `USERS.DAT`) |
| `dos/game/` | `main_loop` (130f: `Frame_Begin`, `Game_Tick`, step gating, HUD), `scene` (1437), `init` (1460, switches as options), `cells` + `cells2` (1568, 176a), `player` (18cf), `layout` (1a54), `anim` (4306), `ui` (43cb: menu bar, boxes, help pager), `elves` (4521), `quiz` (46ce, adapted from `game/quiz.cpp`), `signin` (47c7, adapted), `hints` (49a6), `search` (4a03), `title` (4ba4), `game_state.h` (as built in wave 3: file, function and owner map in `ENGINE_MAP.md`) |
| `dos/app/` | `dos_runtime` (virtual PIT: ticks from the clock, game tick every 5th, at most one logic step per presented frame), `dos_bootstrap`, `dos_cli` (`--dos-pan`, `--dos-pak`, `--dos-screen`, `--dos-sound`) |

The DOS runtime is simpler than the Windows one: a flat frame loop with modal states as bits of
`g_uiMode` (DS:9119), no nested blocking loops and no `longjmp`. Options map to the original
switches: video look `ega|tandy|mcga|cga`, sound `speaker|tandy|adlib`, slow-CPU mode (`-X`).

## 6. Work plan

Same method as the Windows edition: specs first, then the engine, then pixel-exact checks against
DOSBox-X captures. Each wave has at most 5 agents with disjoint files; only one agent drives the
emulator.

| Wave | Agents (files owned) | Done when |
|---|---|---|
| **0 Scoping** (this pass) | – | formats decoded, program mapped, captures, this report |
| **1 Logic specs and captures** | A `code_cells.md` (cells/cells2, all 74 cell types, grid rows, links, gates, castle; the 6 handlers Ghidra cannot decompile); B `code_player_anim.md` (player states, animation scripts, search, end sequence, treasure room); C `code_elves_layout_hints.md`; D `code_ui.md` (menus, dialogs, help, title, sign-in, HUD, quiz screen); E captures: missing scenes via crafted USERS.DAT and save states (treasure found, chest fill, rank-up, crown, prize rooms, elf dust, gap jumps), frame/tick measurements from AVIs, an audio recording if it can be made without sound on the host, plus a DOS `PORTING_NOTES.md` | every routine of the ~400 logic functions has a spec; capture list complete |
| **2 Engine foundation** | A host split + edition detection + presenter layout (`platform/`, `gfx/presenter`, `app/bootstrap`, `src/edition/`); B `dos/data/`; C `dos/gfx/` + `dos/text/` (with `--dos-screen`); D `dos/audio/`; E `dos/save/` + `dos/input/` + `dos/app/dos_runtime` | Windows tests and fidelity unchanged; title, sign-in, quiz and clubhouse screens equal the captures (0 pixels, as in §3); WAVs equal the `dos_sound.py` renders sample for sample |
| **3 Game logic** | A `cells*`, `scene`; B `player`, `anim`, `search`, end sequence; C `elves`, `layout`, `hints`; D `ui`, `title`, `signin`, `quiz`, HUD; E `main_loop`, game start/end, saves, integration | the whole game plays from title to treasure room and saves |
| **4 Fidelity** | up to 5 agents by scenario group (attract and sign-in; level 1; levels 2-3 and tunnels; castle and end; menus, help, Tandy/MCGA/CGA looks), each fixing its own modules | the 104 EGA frames and the Tandy/MCGA/CGA frames match, except random elves; timings match the AVIs |
| **5 Options (optional)** | AdLib via OPL2 core; mods for DOS (PAK/PAN as PNG, text/tables as JSON); packaging; autoplay bot for DOS | user's choice |

A good stop/go point is the end of wave 2: by then the engine shows the DOS screens pixel exact and
plays the music, and the cost of wave 3 is known from the wave 1 specs.

**Status (2026-10-19): waves 0-5 done.** Wave 1 specs (`code_*.md`), wave 2 the engine foundation,
wave 3 the game logic (playable from the title to the treasure room, `ENGINE_MAP.md`), wave 4
fidelity (the reference frame timing, starting from DOSBox-X states, `tools/dos/fidelity.py`: 438 of
439 checkpoints at 0 pixels, 21 of 21 frame logs tick for tick, 8 of 8 AVI time bases), wave 5 the
options: the original's AdLib driver on ymfm's OPL2 (register streams equal to DOSBox-X's captures
write for write, `fmt_sound.md` §11.7), mods for the DOS edition (`docs/MODDING.md`), the DOS autoplay
soak test, the frame order and sound calls at their place inside long frames (`ENGINE_MAP.md` §12).
Packaging (`scripts/package.*`, `--dos <folder>`) copies the DOS folder too. Still open (`ENGINE_MAP.md` §8):
the drawing costs of the UI screens and of the looks other than EGA, which need new frame logs.

## 7. Effort estimate (Windows port = 1.0)

| Wave | Relative to the Windows equivalent | Why |
|---|---|---|
| 1 Logic specs and captures | **0.4** | formats are already done and verified; one program of 400 logic functions; capture tooling exists |
| 2 Engine foundation | **0.35** | one 4-bit 320×200 buffer, one blitter, one font, tiny sound driver; no NE resources, RUND, BNDL, DAM, MIDI, speech or WinG. The host split touches finished code (risk, not size) |
| 3 Game logic | **0.9-1.0** | about the same size as the Windows game code (10 000 decompiled lines vs ~10 900 lines in `src/game/**`), only a fifth reusable (rules, quiz deal, sign-in flow, state machine, RNG); scrolling world, 74 cell handlers, player and elf state machines and animation scripts are new |
| 4 Fidelity | **0.7-0.9** | fewer screens and no 256/16-colour variants, but timing is harder to match (see §8) |
| 5 Options | 0.2-0.3 | optional |
| **Waves 1-4** | **about 0.6-0.7 of the Windows port** | |

Smaller than Windows: formats, rendering, sound, UI (the game draws its own menus and boxes), the
runtime model, platform/build/packaging (already exist), verification tooling (DOS profile,
104 EGA frames, 49 save states, 20 AVIs). Riskier than Windows: the game logic (new, not adapted),
the frame-rate-dependent timing, and the content not yet captured.

## 8. Risks and unknowns

1. **Timing.** Logic runs at 47.3 Hz in the timer interrupt but advances at most one step per
   drawn frame, and some features count frames (search animation, HUD count-up, rank chest). The
   port must pick a reference frame rate (the DOSBox-X one) and measure those counts from the AVIs;
   frame-by-frame comparison is harder than with the Windows 10 Hz ticks.
2. ~~**Code that does not decompile:**~~ resolved in wave 1: the six cell handlers (1568:1090,
   1556, 15bc, 1c7b, 1d96, 1e50) decompile after the DS repair (`code_world.md` §12.3) and the
   53-case treasure-room switch (176a:0596) is specified in `code_player.md` §7.2.
3. **Not yet captured:** treasure found, chest filling, star rank-up, crown ending, prize rooms,
   gap jumps, elf dust taking a coin, any audio. Wave 1 agent E must capture them (crafted
   USERS.DAT, save states) or their fidelity cannot be checked.
4. **Audio fidelity:** no reference recording yet (host sound is muted by design); SN76496 noise
   taps of the Tandy chip unknown (affects effect 1 only); AdLib instrument register order is
   inferred.
5. **Touching the finished Windows edition:** the host/presenter split must keep the Windows
   behaviour identical; guard it with `tmtests` and the Windows fidelity scenarios, and give it to
   one owner in wave 2.
6. **Other DOS releases** (other disk sets, later versions) may move DGROUP offsets. Only your
   V 1.0 is verified; others are refused with a clear message.
7. **Small open points:** ~~terrain-mask bits 0x400/0x1000~~ (resolved: camera mode and fall,
   `code_world.md` §4.4); hint texts 10-12 that no code arms; PAK header words; ~~some USERS.DAT
   fields~~ (resolved, `fmt_data.md` §4). None blocks the engine.
8. **Findings of this pass:** `tools/dos/dos_gfx_common.py` expands MCGA 6-bit colours with
   `v*255//63`, which is 1 lower than DOSBox-X in some channels (e.g. 0x9D vs 0x9E); rounding
   (`(v*255+31)//63`) matches the capture exactly. ~~The locked tree-trunk gate on level 1 is
   painted by its cell handler over the PAN block~~ — wrong (wave 2): no handler paints it; the
   capture predates the unlocking (`picture[0xEF]` = block 0x40), so scenery alone is the picture.
   DOSBox-X logs "Packed file is corrupt" at every start (an EXEPACK loader warning); the game runs
   normally and the port unpacks the EXE itself, so it does not matter.

## 9. Recommendation

Build it, as a second edition in the same engine, in the waves of §6. The risky part (game logic
and timing) is the same kind of work the Windows port already did successfully, the data side is
finished and proven pixel exact, and the engine infrastructure is reused. Start with wave 1 (logic
specs and the missing captures), and decide again after wave 2, when the DOS screens and music run
in the engine.
