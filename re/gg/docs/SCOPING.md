# Gizmos & Gadgets (DOS): scoping report

How much of the engine of *Super Solvers: Gizmos & Gadgets!* (DOS, v1.0, 1993) overlaps with
Treasure Mountain, and what would a tmport-style port of it cost? This report brings together six
static-analysis passes and a few checks of its own. The passes covered the program, the data files,
a function-by-function code comparison with both Treasure Mountain editions, the game structure,
the DOS platform layer, and a skeptical re-check of every overlap claim (the verifier). Where the
verifier weakened or refuted an analyst's claim, this report uses the verifier's version and says
so (marked **verifier**).

The repository versions of the scoping material are in this folder and in `tools/gg/`:
[ARCHITECTURE.md](ARCHITECTURE.md) (program, segment and overlay map, game flow, platform layer, UI
library, saves, hints, relationship to TM), the format specs [fmt_data.md](fmt_data.md),
[fmt_graphics.md](fmt_graphics.md) and [fmt_sound.md](fmt_sound.md), the wave 0 draft of
[PORTING_NOTES.md](PORTING_NOTES.md), and the reference decoders in `tools/gg/`. The scoping
passes' working notes stay in the local scratch area and are not part of the repository.

**Short answer.** G&G is a different game built on a sibling engine. Very little game code is shared:
about **3 KB of live code, roughly 2 % of G&G's 132 KB of game code**, descends from Treasure Mountain
code. All of it comes from the TLC library that the *Windows* TM also links: the text renderer, the
live part of the DAM animation engine, the deadline timers and a number formatter. Beyond the code, G&G
shares TM Windows' runtime model (nested blocking loops paced by deadline timers) and its menu-bar data
format. It shares TM DOS's way of handling the hardware: its own timer and keyboard interrupts, OPL2
and PC speaker, and a 320×200 screen shown at 4:3. tmport's **infrastructure** carries over: the SDL
host and presenter, the OPL2 and speaker emulation, clock, CLI, VFS, the mods/test/fidelity framework,
and four TLC modules that need generalising. That is roughly a fifth to a quarter of a finished port.
All data formats are already decoded. The game itself is new work. **Estimate: about 1.5 times the TM
DOS port** (range 1.25-1.7). The game logic is 1.3-1.6 times the size of TM DOS's and none of it can be
reused, and a game that needs the mouse needs new capture tooling.

**Decision (the user, wave 0).** One repository. A shared core library is extracted from tmport
later. G&G is a second game with its own executable **`ggport`**: engine code in `src/gg/`, specs in
`re/gg/docs/`, tools in `tools/gg/`, names in `re/names/gg/`, and tests find the game data through
the environment variable **`GG_DATA`** (default: the repository's `GadgetsAndGizmos\` folder, which
`.gitignore` keeps out of the repository). Details in §9.

Tags: **[O]** observed (bytes, code, decoded data), **[I]** inferred. Addresses are Ghidra `SSSS:OOOO`
of the G&G project (`re/CONTEXT.md`, G&G section). The image segment is the Ghidra segment − 0x1000,
overlays sit at 2a80..4140, and `DS:XXXX` = DGROUP 1ea7 = file 0xFC70 + XXXX (ARCHITECTURE, conventions).

---

## 1. What the game is

You play at a Technology Center (the hub) with three buildings: Automotive, Aircraft and Alternative
Energy. Morty Maxwell, the villain, challenges you to five races in each building, 15 in all. In each
building's warehouse rooms you open locked doors by solving science puzzles (8 kinds, 221 variants)
and collect vehicle parts while his Cyber Chimps get in the way. In the workshop you then build the
vehicle from a blueprint and race it. Points earn one of 8 ranks. A movie ends the game after the
15th race. The mouse is required. [O; `Seq_Run` 12e0:000f, state table DS:00c2, ARCHITECTURE §2]

| | G&G DOS v1.0 (1993) | TM DOS V1.0 (1990) | TM Windows CD v1.01 |
|---|---|---|---|
| Program | `ssg.exe`, 213 200 bytes, MZ, **Borland C++ 3.0** (TLINK 5.0 byte 0x50 at file 0x1C), large model, 286 code, not packed. 108 544-byte load module plus a **Borland VROOMM overlay area** of 35 overlays (2 empty, 99 730 bytes) loaded on demand through INT 3Fh [O] | `SST.EXE`, 115 011 bytes, Turbo C 2.0, 8086 code, EXEPACK, no overlays | `TMTWINCD.EXE`, Win16 NE, Borland C++ 4.x |
| Code | 1205 functions: 164 runtime and overlay manager, 217 overlay thunks, **824 game functions = 132 KB** [O] | 628 functions; game logic about 400 / 61 KB | 863 functions, about 112 KB |
| Named so far | 192 of 1205 (scoping); after wave 0: 453 of the repaired export's 1234 (1017 without the thunks); after wave 1: 982 of the 1017 (all but the LZH and overlay-manager internals) | all | all |
| Data | 4 compressed archives `gizmo000-003.dat`, 1897 resources, 1.2 MB packed / 5.75 MB unpacked [O] | 19 plain files, 667 KB | about 119 MB (DLLs, MIDI, WAV, speech) |
| Screen | VGA mode 13h only, 320×200 in 256 colours, palettes built from 6-bit DAC ranges, palette fades [O] | 320×200 in 16 colours (EGA, Tandy, MCGA, CGA looks) | 512×384 in 256 colours inside 640×480 |
| Sound | PC speaker or OPL2 (AdLib). Music and effects are 'BMF' MIDI-like songs, each in a speaker and an FM arrangement. No digitised audio: `-sb` and `-sm` just select the OPL2 [O] | speaker, Tandy PSG, PS/1, AdLib | MIDI, 22 kHz waves, speech |
| Input | **mouse required** (INT 33h event handler); own INT 9 handler for 7 held keys, the BIOS for the rest [O] | keyboard play, optional mouse (polled) | Windows messages |
| Timing | PIT at 145.65 Hz; the interrupt only counts ticks and runs the sound driver (72.8 Hz). Game loops wait 4 ticks per frame = 36.4 frames/s (races 8 ticks = 18.2 frames/s) [O] | PIT at 236.7 Hz, logic at 47.3 Hz inside the interrupt | 100 ms room tick on deadline timers |
| Saves | `PLAYERS.DAT`, 578 487 bytes: `USR` 0x1A, version 12, count, a 100-slot name index, 99 pre-allocated 5821-byte records [O] | `USERS.DAT`, 3 + 9 × 360 bytes | `users.dat`, `USR` 0x1A + 7 × 1583 bytes |
| Text | all in the archives (text and hint resources, many inside bundles); only 92 strings in the EXE [O] | mostly inside the EXE | DGROUP and OTHR resources |
| Start-up checks | 286 or better (else exit code 5, which `ssg.bat` reports), VGA, mouse driver, 363 500 bytes free [O] | adapter detection, slow-CPU flag | Windows |

## 2. Program and data map

### 2.1 ssg.exe [O]

* MZ header: 0xDD pages, 976 relocations, 0x120 header paragraphs. The load module is file 0x1200-0x1BA00.
  A single `FBOV` header at 0x1BA00 points to the segment table at file 0xF030 (82 entries).
* The Ghidra project was built with a method of its own (a flattened image, `SetupOverlays.java`,
  `FixSwitches.java`). Each overlay gets its own memory block, fix-ups are translated, and each stub
  entry is rewritten as a far jump. This was needed because Ghidra's MZ loader and its switch analysis
  both mishandle VROOMM code. The method can be reused for any Borland VROOMM program
  (ARCHITECTURE §1.2; `re/CONTEXT.md` says where the scripts and the project live).
* Known defects of the scoping export: ovl10 2fcb and ovl27 3f56 straddle a 64-KB linear boundary,
  so some near calls resolve into the wrong page. This created a phantom function `FUN_3f56_092d`.
  Calls that pass a struct by value lose their arguments in the decompiler, and switch-bearing
  overlay functions have to be read from the assembly. Two state functions (12e0:0506, 12e0:0820)
  are missing from the export. Wave 0 repaired the boundary references, the phantom and the two
  missing functions; struct-by-value calls and some overlay switches still need the assembly
  (`re/CONTEXT.md`, G&G section).

Where the 132 KB of game code goes (sizes from the function list, KB = 1000 bytes; roles from the
systems, code and platform passes) [O sizes, I grouping]:

| Part | Segments / overlays | Functions | Bytes |
|---|---|---|---|
| Engine, graphics: mode 13h primitives, LPX blitters, palette and fades, 8×8 dirty tiles, movie decoder | 1399, 1473, 15df, 1581 | 61 | 6.3 KB |
| Engine, resources: archive reader, LZH decompressor | 14ff, 1b48 | 19 | 2.5 KB |
| Engine, sound: BMF driver (64 song slots), sound banks, Ctrl+S | 1bb4, 1cdd | 81 | 5.1 KB |
| Engine, timers, interrupt handlers, keyboard, mouse, click zones, memory | 1454, 152c, 1536, 17a0, 17ab, 198d, 1d3c | 53 | 3.9 KB |
| TLC UI library: fonts, text renderer, DAM, dialogs, buttons, list box, menu bar, menu commands, help, text viewer | ovl01 2b16, ovl03 2b89, ovl23 3d39, ovl26-28, ovl30-34 | 101 | 14.8 KB |
| **8 puzzle mini-games** | ovl13 3083 electric circuits, ovl20 3938 simple machines, ovl21 3a4a machine pieces, ovl15 33cd energy forms, ovl17 3797 launch (uses DAM) [O asserts, strings, DAM calls]; ovl22 3b2f gears, ovl14 32fe balance, ovl16 35bb magnets [O: dispatch targets, screen pictures and the round 2 captures; wave 0 had them from the dispatch order only] | **235** | **49.5 KB** |
| Puzzle choice and helpers | ovl18 38e2, ovl19 3924 | 19 | 1.4 KB |
| Rooms, actors, player, projectile, Cyber Chimps, parts catalogue, collision | 1622, 1834, 18f5, 174b, 19c0, 1adb, 153f, ovl00 2a80 | 111 | 17.7 KB |
| Workshop and vehicle assembly | ovl08 2dee | 29 | 7.5 KB |
| Races (land and energy / aircraft) | ovl24 3dd6, ovl25 3e77 | 13 | 5.8 KB |
| State machine and hub, HUD | 12e0, 1a04 | 46 | 5.9 KB |
| Attract, sign-in, PLAYERS.DAT, results and final movie, hints, start-up, helpers | ovl06, ovl07, ovl02, ovl10, ovl12, ovl11, ovl05, ovl04 | 56 | 12.1 KB |
| **Total** | | **824** | **132.3 KB** |

So about 18 KB is low-level engine, 15 KB is the TLC UI library and about 100 KB is the game
(509 functions). The comparable TM DOS figure is about 400 functions and 61 KB of game logic.
(ovl26 3f50, 82 bytes, counted above with the UI library, turned out in wave 0 to be a rounding
division helper of the races; ARCHITECTURE §1.4.)

### 2.2 Files

| File | Size | Contents |
|---|---|---|
| `ssg.exe` | 213 200 | the program (§2.1) |
| `gizmo000.dat` | 337 881 | archive `MR0`: 878 resources, ids 0x000-0x36d. Mostly pictures and sprites, scene and level descriptions (15 part catalogues), the two fonts, palettes, motion paths [O] |
| `gizmo001.dat` | 349 371 | `MR1`: 479 resources, ids 0x1000-0x11de. Texts, hints, screens, sound banks, DAM scripts [O] |
| `gizmo002.dat` | 280 359 | `MR2`: 335 resources, ids 0x2000-0x214e. Puzzle grids, puzzle texts, pictures [O] |
| `gizmo003.dat` | 259 149 | `MR3`: 205 resources, ids 0x3000-0x30cc. The final movie (202 delta frames) and its sound bank [O] |
| `gizmo004.dat` | 45 644 | **not game data**: a screen save-under that the game writes at run time (w 280, h 163, pixels); this copy is a leftover [O] |
| `PLAYERS.DAT` | 578 487 | the save file, pre-allocated full size at first run [O] |
| `ssg.bat`, `Read_1st.txt`, `ssg.ico` | small | launcher (error level 5 = no 286), switches `-si/-sb/-sm/-sn/-gv` |

The four archives are self-contained, **not** continuation volumes: each has its own `MRn` 0x1A header
and id range.

### 2.3 Format status

The specs are `fmt_data.md` (archive, compression, bundles, resource types, text, tables,
`PLAYERS.DAT`), `fmt_graphics.md` (pictures, palettes, fonts, DAM, movie) and `fmt_sound.md` (BMF) in
this folder; the reference decoders are in `tools/gg/` (`python -P`, read-only on
`GadgetsAndGizmos\`, outputs under `extracted/`).

| Format | Status |
|---|---|
| `MRn` archive: u32 offset table relative to file offset 4; entry = {type letter, method, payload}; id = volume << 12 \| index; last entry `I` = 0xA55A integrity word | **decoded**, all 1897 entries [O] |
| Method 3 compression: static canonical Huffman + LZ77 (4 or 8 KB window; every entry uses 8 KB), `Lzh_Decompress` 1b48:02aa | **decoded**. All 1490 compressed entries decode to their stated size (the data pass wrote 1059, which is wrong) [O]. 16 header bits are read and ignored; they hold the token count (wave 0) |
| Bundles (type 0x01): u16 offset table, members inline | **decoded**, 364 bundles, 3945 members [O] |
| `LPX` 0x1A pictures: raw 8 bpp or skip/literal RLE, colour key | **decoded**; a full town screen and the sprite sheets render correctly [O] |
| Palettes: {byte offset into the DAC table, byte count, 6-bit RGB} ranges | **decoded**; 3 of the 8 full palettes are 782 bytes, and no loader reads their 10 extra bytes (wave 0) |
| `fnt` 0x1A fonts (ids 9, 10): proportional 1-bpp, key-cap glyphs | **decoded** [O] |
| Text (`T`) and New Player Hints (`H`, with a `FF 06 … FF` designer note; `FF 03` colour escapes in part and help texts) | **decoded** [O] |
| `DAM` 0x1A animation scripts, 16-bit opcodes 0x7FF8..0x7FFF | **decoded** (12 scripts) [O] |
| Final movie: Deluxe Paint Animation "RunSkipDump" deltas (colour 1 = skip) | **decoded**, all 202 frames [O]; palette: none is loaded, the results screen's (base + 0x1077) stays [O, round 2 capture] |
| `BMF` 0x1A music and effects: one track of MIDI-like events, speaker class 1 / OPL class 3 with 11-byte patches | **tokenised**, all 495 songs. Tick = 72.83 Hz (platform); wave 0 explained the channel flags, the "4 bytes after the end code" (the end-of-section meta event) and the `L` member (`fmt_sound.md`) |
| `PLAYERS.DAT` | **specified** (header, index, records, the 3 × 1722-byte per-building blocks; wave 1, `fmt_data.md` §8, `code_ui.md` §12) |
| Game tables (`R` paths, `W` scenery and wipes, `A` room and animation sets, `F` puzzle data, `D` level descriptions and puzzle tables, `S` actor sets) | **specified** in wave 1 (`fmt_data.md` §7 and the `code_*.md` specs) |

## 3. Checked again

* Header facts of `ssg.exe`, the four archive headers and entry counts, the `gizmo004.dat` and
  `PLAYERS.DAT` arithmetic, and the TLINK and copyright bytes all match what the analysts reported [O].
* The PIT install sequence, `B0 36 E6 43 B8 <divisor> E6 40 8A C4 E6 40 B0 B6 E6 43`, is
  byte-identical apart from the divisor in ssg.exe (file 0x5786, divisor 0x2000) and TM DOS (unpacked
  file 0x25E4, divisor 0x13B1) [O].
* The archive decoder decodes every compressed entry (1490) to its stated size with no over-read
  [O]. Correct bytes are shown by the downstream formats, which all parse exactly, and by the
  rendered screens.
* `tools/tmctl.py` cannot inject guest mouse input (its docstring) [O]. This matters for §8. (Wave 0
  solved it for G&G with a guest-side TSR driven by posted keys: `REFERENCE_CAPTURES.md` §2.)
* Wave 0 (architecture pass), from the bytes of `ssg.exe` and `TMTWINCD.EXE` [O]: the three
  `Seq_Run` tables (DS:00c2 17 states, DS:20a8 11, DS:219a 1), the puzzle variant counts DS:3334
  (221), the rank thresholds DS:1f9c, the menu records of both games (DS:4017.., TM Win DS:1383..),
  the timer module (`Timer_Wait` ends at tick ≥ deadline, `Timer_Expired` needs tick > deadline),
  the dialog loop's tick freeze, and the chimp lists DS:1bae (the 286 path halves the first chimp's
  room, not the count; ARCHITECTURE §2.6).

## 4. Overlap, subsystem by subsystem

Grades: **identical** (same logic or same data layout), **same lineage** (the same routine or design,
evolved), **similar idea** (same role, new code), **different**.

| Subsystem | vs TM Windows | vs TM DOS | Evidence and notes |
|---|---|---|---|
| Toolchain and C runtime | same lineage (toolchain only) | same lineage (toolchain only) | Three Borland compilers. 65 of the 133 runtime functions match a named TM runtime function, 17 exactly (**verifier**: the exe pass said 66 / 18). This is not engine overlap |
| Program format | different | different | MZ with 35 VROOMM overlays vs NE / EXEPACK. A port needs no overlays |
| Top-level flow | different | similar idea | One table-driven state machine `Seq_Run` 12e0:000f with 17 states (DS:00c2). TM Win uses nested loops plus setjmp/longjmp; TM DOS uses a phase table for its title [O] |
| **Runtime model and frame pacing** | **same lineage** | different | Nested blocking loops: `Timer_Set(0,4)` … `Timer_Wait(0)` at 12e0:03bf, 174b:052c, 2dee:0d38, 2fcb:044d; races use 8 ticks (3dd6:028e). That is TM Win's model. TM DOS runs its logic inside the interrupt (**verifier: confirmed**, the overlap that matters most for porting) [O] |
| Deadline timers (152c) | same lineage | similar idea | Same four functions in the same order as TM Win 10a8, with 8 u32 slots. **Verifier: not identical**: `Timer_Expired` 152c:004a tests tick > deadline, TM Win 10a8:0066 tests now ≥ deadline (G&G's own `Timer_Wait` uses ≥). Clock = PIT ticks, not ms [O] |
| PIT and timer interrupt | n/a | similar idea | Install sequence byte-identical apart from the divisor (above). The ISR is new: it counts ticks and runs sound only. It chains the BIOS on every tick, so the DOS clock runs 8 times fast in the game [O, medium] |
| Keyboard interrupt | different | similar idea | Held bits for 7 keys, the rest via the BIOS, a 16-entry ring. **Verifier**: common DOS design, code different (TM DOS: C switch over 17 keys) [O] |
| Input codes, mouse, click zones | similar idea | similar idea | **Verifier refuted** the "same encoding" claim. G&G's 0x4000/0x8000 mean first poll / still held (left button only); TM Win's mean left / right button. Zones are 22-byte records in linked lists with push/pop; the mouse goes through an INT 33h fn 0Ch event handler. Weak TM Win signals: zone id used as the input code, Ctrl+S handled in the fetch [O 198d:0106, 17a0:0004] |
| Archive and compression | different | different | MRn + LZH has no counterpart. Only the API idea is shared with TM Win: load into one shared buffer, a private copy, or a caller's buffer (14ff:000d/00fe/013d vs 1038:088a/08c5/08d4) [O] |
| Bundles | similar idea | similar idea | Members inline with u16 offsets (TM Win BNDL refers to resources by id); "first u16 = table length" is also a TM DOS habit [O] |
| Pictures | similar idea | different | 8-bit LPX with a skip/literal RLE and colour key; no RUND code in ssg.exe [O] |
| Palettes and fades | different | different | DAC ranges merged into a shadow palette; fades of 12 and 9 palette loads, two vertical retraces each (~0.34 s / 0.26 s). TM Win's fades are stubs, TM DOS has none [O] |
| Frame buffer | similar idea | different | One 320×400 byte buffer: screen rows 0-199, back page 200-399, sprites erased by copying from y+200. **Verifier**: TM Win's stacked back page (y+384) as a design, no shared code [O 1473:0074, 1399:0233] |
| Presentation | similar idea | similar idea | 40×25 map of dirty 8×8 tiles copied to A000 with the cursor composited in, no retrace wait; TM Win keeps a rect list, TM DOS dirty spans [O 1581:03ee/052a] |
| Video mode | different | different (same 320×200 at 4:3) | Mode 13h only; `-gv` only skips the display check [O] |
| Fonts | similar idea | similar idea | **Verifier**: a generic font API (TM Win `Font_Select` returns nothing, G&G's returns the previous font; 1 font vs 2), new glyph format [O] |
| **Text renderer** | **same lineage** | different | Overlay 2b89 is TM Win's 10b0 in the same function order. It has the same `FF 01/02/03/05/06` escape meanings and the same signed centring, and it still skips TAB after a wrap although it has no TAB case, a leftover of TM Win's code. Wrappers are evolved, not identical (**verifier**) [O] |
| Text markup | same lineage | different | `FF 06 … FF` comments in the hint texts, as in TM Win [O] |
| **DAM animation** | **same lineage** | different | Same magic and the same opcode numbering widened to 16-bit words; 2 channels instead of 16. **Verifier corrections**: the interpreter is 3d39:058f, not 3d39:013a. 11 of its 15 functions have no caller. The only user is one puzzle (ovl17 3797). The live part is about 1.5 KB. FB/FC play no sound in G&G, and 0x7FF8 (an indexed callback) is new [O] |
| Final movie | different | different | Deluxe Paint ANM-style deltas drawn straight to VRAM at 18 PIT ticks per frame, with an 18-entry cue table (DS:236c) [O] |
| Sound driver and music | different | similar idea | New 64-slot BMF driver with speaker and OPL2 rhythm-mode classes. Its PSG and MT-32 (MPU-401) paths are dead code. Only the standard AdLib write (6 and 35 status reads, identical to TM DOS 491b:0875) and the detect sequence match. TM DOS also keeps per-device arrangements [O] |
| **Menu bar** | **identical data format**; drawing code new | similar idea (drawn in game) | `MenuBarDef` {count, far popups} and the 0x14-byte popup record match in all six fields (DS:4053, DS:4017..). **Verifier: stronger than claimed**: G&G reads +0x04 / +0x10 / +0x12 as title width / drop-down width / title x, and TM Win's never-read values fit the same reading with 3-px gaps. This answers open question 1 of `re/docs/dgroup_map.md`, now updated [O values, I reading] |
| Dialogs and buttons | same lineage (design) | similar idea | Same module order and draw recipe step by step as TM Win 10c0 (3f56:0389 vs 10c0:0764), but rewritten: descriptor by value, N buttons with key arrays, 2 frames. Flash button 4034:01d6 is evolved [O] |
| Help pager | similar idea | similar idea | **Verifier**: the button labels are house vocabulary found in all three EXEs; no shared pager code [O] |
| Sign-in and roster | similar idea | similar idea | **Verifier**: Ctrl+R, capitalised first letter, duplicate and full-list checks are also in TM DOS, so they are a series convention. 99 players, alphabetical roster, new list widget [O] |
| Ranks and score | similar idea | similar idea | **Verifier**: generic threshold lookup. 8 ranks from 5000 to 750 000 points. The thousands-comma formatter (2bd4:024d vs TM Win 1160:0cdb) is a real fingerprint [O] |
| Random numbers | **not engine overlap** | – | **Verifier refuted** "identical to TM Win": `rand()*n/0x8000` is Borland C++ 3.x/4.x's `random()` macro (TC 2.0's was `rand()%n`) [O sites, I macro]. Same Borland LCG; one `srand(time(NULL))` at 303a:0229 |
| Hints | similar idea | similar idea | New Player Hints (ids from 0x1043), each shown once per player, a 5-entry delayed queue, no helper character or voice. **Verifier**: concept and name shared, code new [O 3060] |
| Save file | similar idea | similar idea | **Verifier**: only the 4-byte `USR` 0x1A magic is shared with TM Win. The version word, count, index, single-record I/O and contents are new [O 2b5a, 2caa:00a1] |
| World and rooms | similar idea | different | Fixed 320×165 rooms over a 35-px HUD, 4 object layers, doors that puzzles unlock; data per building and level. TM Win has fixed rooms too; TM DOS scrolls [O] |
| Actors and Cyber Chimps | similar idea | similar idea | 12 actor slots with callbacks, data-driven chimp behaviour tables; new code [O, behaviour partly I] |
| Puzzles | similar idea | similar idea | The "unsolved first, per-player history" rule resembles TM's question deal; the 8 interactive mini-games (49.5 KB) are new [O 38e2] |
| Workshop, vehicles, races | different | different | Nothing comparable in TM [O] |
| Title and attract | similar idea | similar idea | 30 s pages, Esc quits at once, the loop restarts. **Verifier**: behaviour like TM DOS, no shared code [O 2c11] |
| Options, Ctrl+S | similar idea | similar idea | **Verifier**: a series convention; handling it inside the input fetch is a weak TM Win signal [O] |
| Start-up and switches | n/a | similar idea | Same `-S<letter>` sound-switch convention ('-' or '/', any case; TM DOS `-Si/-St/-Sa/-Ss`), rewritten parser [O 303a:005c vs 1460:0463] |

**How much code is shared.** The code pass matched all 824 game functions against both TMs and
reviewed every candidate by hand (method calibrated on runtime functions: precision 1.00 at score 0.6).
It reported 49 counterparts in TM Win (10 identical, 17 evolved, 22 similar) and 14 in TM DOS.
**Verifier:** after review only about 6 are identical. Of those, `Input_WaitCode` is trivial,
`Timer_Expired` has a different boundary and two text wrappers take different arguments. Three of the
remaining identical DAM functions are dead code. **Shared live code is about 2.5-3 KB, around 2 % of
the game code**: the text renderer (~1.2 KB, used by every screen), the live DAM part (~1.5 KB, one
puzzle), the timers (~0.16 KB), the comma formatter and the button flash. Another roughly 10 % of the
engine (by function count) shares a design or data layout with new code: the menu format, the dialog
recipe, the timer-paced loops, the FF markup, the USR magic and the stacked back page. For scale, TM
Win and TM DOS (the same game, rewritten) share only 1 of 323 non-trivial functions above score 0.6;
G&G and TM Win share 10 of 635 [O].

**What this says about the engine.** The menu, dialog, text and DAM modules form a TLC UI library that
G&G links wholesale. Evidence: an unreferenced File-menu item list from another title (DS:3f2b), the
dead DAM functions, and TM Win's menu data laid out for a self-drawn DOS menu bar. TM Windows was built
on the same library. Nearly all of the lineage runs to TM Windows. The TM DOS links are conventions
(attract timing, Ctrl+S, sign-in keys) and DOS idioms (the PIT and AdLib code) [I].

## 5. What tmport contributes

"As is" means the module can be used unchanged or with constants only; "generalise" means it needs
parameters or an interface; "pattern" means the design guides new code; "no" means it is TM-specific.

| tmport part | Use for G&G | Why |
|---|---|---|
| `src/util`, `src/vfs` (directory source), `src/rtl/borland_rand`, `timing/clock.h` | **as is** | same Borland LCG; plain files |
| `platform/host` (SDL window, event pump, focus pause, fullscreen, screenshots, frame dumps) | **as is** | presents an RGBA frame |
| `gfx/presenter` with `PresenterLayout::dos1990()` (320×200 at 4:3, integer 5×6, square-pixel option) | **as is** | same screen size and aspect |
| `dos/audio/opl_chip` (ymfm OPL2, also does rhythm mode), `opl_backend`, `speaker_synth`, `synth_common`; `psg_synth` only if the dead PSG class is ever wanted | **as is** | same chips; the AdLib write timing matches TM DOS |
| `dos/audio/dos_audio_device`, `dos_sound_renderer` | **generalise** | decouple from TM's `Sequencer`/`SoundBank`; tick at 72.83 Hz |
| `dos/input/bios_keyboard`, `PcKeyboard` (SDL to set-1 scan codes, typematic) | **as is**; the TM `KbdIsr` is replaced | same BIOS model |
| `dos/input/mouse` | **generalise** | driver x range 0..639 (halved by the game); event callback (fn 0Ch) instead of polling |
| `dos/app/dos_runtime` `PitClock` | **generalise** | divisor 0x2000 instead of 0x13B1; sound every 2nd tick. Its lockstep `Game_Tick` and step gate do not apply |
| `timing/deadline_timers` | **generalise** | unit = PIT ticks, strict `>` in `Expired` (≥ in `Wait`) |
| `platform/engine` nested-loop pump | **pattern, close** | every G&G wait (`Timer_Wait`, `Delay`, `Input_GetCode`) maps onto it one to one |
| `text/text_renderer` | **generalise** | same escape engine; 4 control codes instead of 6, font behind an interface |
| `anim/` (DAM script, engine, player, bundle) | **generalise** | 16-bit opcodes, 2 channels, pause field, 0x7FF8 callback, frame table from a G&G bundle; one puzzle only |
| `ui/menu_bar` | **generalise** (data model) + new drawing | identical record layout; G&G draws the bar and drop-downs itself |
| `ui/game_dialog`, `dialog_desc`, `help_topics` | **pattern** | same recipe, different descriptor and metrics |
| `gfx/frame_buffer`, `palette` | **generalise or new small class** | 320×400 8-bit with an 8×8 dirty-tile map; DAC ranges and fades |
| `input/input_queue`, `hotspot_table` | **pattern** | G&G zones are linked lists with priorities, circles, push/pop |
| `save/users_file`, `dos/save/users_dat` | **pattern** | byte-exact slot I/O; new layout |
| `res/resource_manager` | **pattern** | id routing and typed accessors; the NE/DLL parts do not apply |
| `src/cli` dispatcher, `app/script_input` (`--keys`), `app/launch_options`, `save/settings`, `src/edition` | **as is / generalise** | a G&G folder check like `checkDosFolder` |
| `src/mods` (PNG, WAV, JSON codecs, mod folders, `--check-mods` framework) | **as is** for the framework | new override formats (LPX, BMF, text) |
| `tests/` helpers, `tools/dos/` (`savmem.py`, `imgcmp.py`, `audiocmp.py`, `oplcmp.py`, `avifit.py`, `fidelity.py`), `tools/tmctl.py` | **as is / generalise** | a G&G DOSBox-X profile (VGA, OPL, mouse driver); see §8 for the mouse gap |
| `src/ne`, `src/data`, `gfx/pixmap` (RUND), `text/font` (FFNT), `audio/` MIDI and wave, `src/game`, `src/dos/game`, `dos/gfx`, `dos/text`, `dos/data`, `dos/autoplay` | **no** (TM-specific); game modules only as patterns (sign-in flow, hint triggers, rank table) | different game and formats |

By volume, the reusable files are about 14.5k of tmport's 72.6k lines, and part of that is only a
pattern. The TM DOS backend is about 26k lines without its mods and autoplay. G&G's own code is likely
to be about 1.5 times that, around 35-40k lines. So tmport covers **about a fifth to a quarter** of a
finished G&G port, nearly all of it infrastructure. This agrees with the verifier's 20-30 % [I].

## 6. New work

1. **Data layer:** the MRn archive and LZH decompressor (fully specified; about 150 lines of C++),
   bundles, and typed accessors for the resource types `G P T H S I M L C A R W F D`.
2. **Graphics:** the 320×400 8-bit buffer, the 1399 primitives (lines, fills, get/put, keyed and RLE
   LPX blitters, glyphs), the dirty-tile map and its flush, the software cursor, DAC palette ranges and
   retrace-paced fades, random wipes, and the movie player with its cue table.
3. **Sound:** a port of the 1bb4 driver (64 song slots, cue callbacks, speaker class, OPL2 rhythm-mode
   class with 11-byte patches and volume handling) and the speaker/FM bank pairing, running on the
   existing chip emulators.
4. **Platform models:** a virtual PIT at 145.652 Hz feeding the tick counter and timers; the G&G
   keyboard handler (7 held keys, play-mode swallowing, ring); the INT 33h event-latch mouse model;
   click-zone lists with push/pop; `Input_GetCode` command codes (Ctrl+S, Esc, Alt+F/H/O, F1-F3, the
   5000/6000 ranges).
5. **UI library:** fonts (`fnt` 0x1A loader), generalised text renderer and DAM, dialogs and buttons in
   G&G's descriptor format, the self-drawn menu bar, the list box, the help pager and the scrolling
   text viewer.
6. **Game:** the 17-state sequencer and hub; building set-up and resume; rooms with 4 layers and doors;
   the 12-slot actor system, player, projectile and Cyber Chimp AI; parts, inventory strip and HUD; the
   workshop and vehicle assembly; two race overlays; results, rank-up, the post-race menu and the final
   movie; **8 puzzle mini-games with 221 variants** (the largest single item, 37 % of the game code);
   puzzle choice and history; New Player Hints; sign-in and roster; options; PLAYERS.DAT records.
7. **Reverse engineering still to do:** names and specs for about 630 unnamed game functions; the
   meaning of the per-building save blocks; the puzzle grid and table semantics; the race model; the
   BMF details; the movie palette. **Done in wave 1** (`code_*.md`; the remaining gaps are their open
   questions, summarised in `PORTING_NOTES.md`).
8. **Tooling:** guest mouse input for DOSBox-X and a G&G profile in `tmctl.py` (both done in wave 0,
   `REFERENCE_CAPTURES.md`), a G&G frame/step logger, a G&G profile in `fidelity.py`, and the
   reference captures (§8).

## 7. Effort estimate (TM DOS port = 1.0)

The TM DOS port's waves 1-4 are the baseline. I assume they split roughly 20 % specs, 20 % engine
foundation, 35 % game logic and 25 % fidelity [I]. Each G&G factor below compares the G&G wave with the
TM DOS wave.

| Wave | Factor (range) | Why |
|---|---|---|
| 1 Specs and RE | **1.7** (1.5-2.0) | 824 game functions vs ~530, with 192 named; overlay code is harder to read in Ghidra (overlay switch tables, 64-KB boundary, struct-by-value calls); the 8 puzzles are 235 functions of interactive simulation. Formats are already decoded, as they were for TM DOS |
| 2 Engine foundation | **1.0** (0.9-1.2) | Less than TM DOS: one video mode instead of four looks, and the host split, presenter layout, edition detection, ymfm and PIT clock already exist. More than TM DOS: extracting a shared core from finished code, LZH, fades, the movie, the mouse event model, and generalising text, DAM, timers and menu. The sound driver is about TM DOS's size (5.1 KB vs 5.6 KB with its AdLib driver), but it uses a new song format and OPL rhythm mode |
| 3 Game logic | **1.6** (1.4-1.9) | 509 functions / 100 KB vs ~400 / 61 KB. Almost nothing reusable: the TM DOS port could adapt about a fifth of the Windows game code (quiz deal, sign-in, rules), G&G can adapt only patterns |
| 4 Fidelity | **1.3** (1.1-1.6) | New mouse-input tooling before anything beyond the title can be captured; more distinct screens (8 puzzle kinds, 3 vehicle kinds, races); overlay-load stalls and retrace-paced fades. Easier: one look, and TM-Win-style pacing instead of TM DOS's interrupt-gated logic |
| **Waves 1-4** | **about 1.4-1.5 (1.25-1.7)** | 0.2×1.7 + 0.2×1.0 + 0.35×1.6 + 0.25×1.3 = 1.43 |
| 5 Options | 1.0-1.5 × TM DOS's wave 5, if wanted | mods need new codecs (LPX, BMF, text); an autoplay bot would have to solve 8 puzzle kinds and build vehicles |

Cheaper than TM DOS: everything under the game (host, presenter, audio chips, CLI, packaging, test
and fidelity frameworks) exists and is proven, all formats are decoded, there is a single video mode,
and the runtime model is the one tmport already implements for Windows. Dearer: a game about 1.5
times as big with almost no reusable logic, less reverse engineering done so far, and a mouse-only
game that the current capture tooling cannot drive.

**After wave 1 (specs and round 2 captures).** The game-logic specs come to about 6 900 lines
(world, vehicles and races, two puzzle parts, UI) plus 1 950 for the platform and sound, against
7 400 for all of TM DOS's wave 1. Two findings shrink wave 3: the races are deterministic
functions of two part lists with no input and no `rand()` (the feared steering and physics do not
exist), and the world is data-driven (one 46-op actor interpreter runs the player, the banana and
every chimp). Two make it bigger: the puzzles are full simulations (an electric path search, gear
drive propagation, a magnet force model with 16-bit arithmetic, a balance, launch tables, a DAM
cut-scene), about 37 % of the logic with little shared code, and the 1bb4 driver is new code, not
an adaptation. The game-logic factor therefore stays at **1.6**, with a narrower range
(**1.45-1.75**: no unknown mechanics are left, the remaining gaps are captures, not missing
RE), and the total stays at **about 1.4-1.45** (range 1.25-1.65 with the other waves' ranges
unchanged). In lines: TM DOS's game logic is about
13 600 lines of C++ (`src/dos/game`); G&G's is estimated at 20 000-24 000 [I].

## 8. Risks and unknowns

1. **Fidelity tooling (largest risk).** DOSBox-X feeds the guest mouse only while it has captured the
   host mouse (`tools/tmctl.py`), and G&G cannot be played without the mouse. A mouse injector is
   needed before reference captures beyond the attract loop: for example, a stub patched into save
   states that writes the INT 33h handler's latches (DS:0a10..0a2c, handler 17a0:0004), or a
   DOSBox-X-side solution. Overlay code moves in memory, so patches must target resident code [I].
   **Wave 0 outcome: solved.** Posted mouse messages cannot work with this DOSBox-X build (it reads
   the host pointer), so a guest-side TSR (`MOUSEKEY.COM`, built by `tools/gg/mouse_tsr.py`) owns
   the game's INT 33h event handler and takes mouse commands as posted keys; a puzzle was solved
   with it and the tour replays (`REFERENCE_CAPTURES.md`, `re/CONTEXT.md` G&G section). What
   remains: warehouse runs depend on real-time pace, because a walking chimp calls `rand()`
   whenever one of its probes touches an edge, pad, door or item, so such runs start from save
   states. Round 2 (wave 1) captured all 8 puzzle kinds, a race per building, the workshop, the
   movie and sound runs this way.
2. **Reverse-engineering scale and Ghidra quality.** Only 192 of 1205 functions were named in the
   scoping pass (453 of 1017 real functions after wave 0, 982 after wave 1). Overlay switch tables, the 64-KB boundary problem and struct-by-value calls make
   parts of the decompiled C unreliable, so assembly must be read [O].
3. **Puzzles and races.** 221 puzzle variants run on grid and table data whose meaning is partly
   inferred. The race model (speed from parts, paths, whether the player steers) is inferred. Exact
   simulation matters for pixel-exact checks [I]. **Wave 1:** every puzzle's data and algorithm
   and both race models are specified; the races have no steering and no randomness; the simulated length of
   the captured lost Aircraft race (Morty's finish) matches the AVI within about 0.2 s [I].
4. **Sound.** BMF channel flags, the 4 bytes after the end code and the `L` member were unexplained;
   wave 0 explained them (`fmt_sound.md`). The first reference recordings exist: DOSBox-X `.dro` OPL
   captures of the title music, identical write for write across two boots; `oplcmp.py` can compare
   them, as for TM DOS [O/I].
5. **Movie palette** not found by scoping (none of the 8 full palettes fits). Wave 0: the movie
   loads none and shows the results screen's palette, base colours 0..31 plus 0x1077
   (`fmt_graphics.md` §7.4); confirmed by the round 2 capture of the ending.
6. **Timing details.** Overlay loads are disk reads inside frames. Fades are paced by the 70 Hz
   retrace. The BIOS clock runs 8 times fast in the game (it only feeds `srand(time)`). Frame counts
   (the results count-up over 50 frames) depend on frame time. Fidelity runs must take the random seed
   from the DOSBox-X state, as for TM DOS [O/I]. **Round 2:** no overlay stall is visible in
   DOSBox-X; fades are 22 and 16 visible frames; frame periods hold (REFERENCE_CAPTURES §10).
7. **Touching finished code.** Extracting a shared core changes both TM editions. Every step must keep
   `tmtests` and both fidelity suites unchanged, with one owner, as the host split was handled in the
   TM DOS wave 2 [I].
8. **Other releases.** Only DOS v1.0 is examined. Later releases (for example Windows or CD versions)
   are different programs; refuse them with a clear message [I].
9. **Small open points:** the 286 path at 2a80:07b1 (wave 0: it halves the first chimp's room word
   in place, sending that chimp to room 0, the workshop; the scoping pass read it as halving the
   chimp count; ARCHITECTURE §2.6); the 10 extra palette bytes (wave 0: 3 palettes, not colour
   data); the ignored 16 LZH header bits (wave 0: the token count); why roster entry 0 holds 98
   (wave 0: the shipped index lists slot 98 twice and slot 0 never; the first new player gets slot
   98, `fmt_data.md` §8.2). MenuDef semantics in TM Win are now explained and recorded in
   `re/docs/dgroup_map.md` [O/I].

## 9. Recommendation and decision

**Structure: one repository, a shared engine core and two games.** Do not add G&G as a third
*edition* inside the `tmport` executable. It is a different game, and its detection, start-up, menus
and save handling would branch through TM's code. Do not start a new repository that copies the
platform layer either. That would duplicate about 10-14k lines plus the Python fidelity tools, and the
generalised TLC modules (text renderer, DAM, timers, menu data) would fork. Instead:

* Extract a **core library** (CMake target) from the parts that §5 marks "as is" or "generalise":
  util, vfs, rtl, clock, deadline timers, host, presenter, the DOS chip synths and audio device, the
  DOS keyboard and mouse models, the PIT clock, the CLI dispatcher, scripted input, settings, the
  mods framework and the test helpers, plus the TLC modules (text renderer, DAM, menu data model).
  Do it **without a big-bang move**: first a library target over the existing directories, then
  parameters where G&G differs (PIT divisor, timer unit, frame-buffer geometry, DAM opcode width and
  channel count, text control codes). Check after each step that TM behaves byte for byte as before.
* Add **G&G as a second game** with its own executable, its own namespace and
  `src/gg/{data,gfx,text,audio,input,save,app,game,mods}`. No game data goes into the repository.
* If G&G is later wanted as a separate public project, the core can be split into its own repository
  then, once it is stable.

**Decision (taken by the user in wave 0; recorded, not re-argued):**

| Item | Choice |
|---|---|
| Repository | one repository (this one) |
| Shared code | a core library extracted from tmport **later**, step by step as above |
| G&G | a second game, executable **`ggport`** |
| Engine code | `src/gg/` |
| Specs | `re/gg/docs/` (this folder) |
| Tools | `tools/gg/` |
| Names | `re/names/gg/` |
| Test data | environment variable **`GG_DATA`**; default: the repository's `GadgetsAndGizmos\` folder (git-ignored, read-only to every tool) |

**Plan:**

1. **Wave 0: promote the scoping material** into the repository (this folder, `tools/gg/`,
   `re/names/gg/`, the VROOMM import scripts in `tools/ghidra_scripts/`), repair the export defects
   (§2.1), and **spike the mouse injector** (risk 1). Without it nothing past the attract loop can
   be verified.
2. **Wave 1, specs and captures (5 agents):** A sequencer, hub, set-up, rooms, actors and chimps;
   B workshop, parts, HUD, races, results and movie; C the 8 puzzles with their choice and history (the
   biggest share; maybe two agents); D the UI library, sign-in, save, hints, attract and options;
   E the platform and sound spec (the 1bb4 driver, mode 13h primitives, dirty tiles, fades, keyboard
   and mouse models) plus tooling and reference captures (screens, frame logs, `.dro` OPL streams).
   Each extends `PORTING_NOTES.md` for its area.
3. **Wave 2, foundation:** core extraction (one owner, first), `gg/data`, `gg/gfx` (with a screen
   tool like `--dos-screen`), `gg/audio`, `gg/input` and the runtime. **Stop/go point:** title,
   attract, sign-in, a warehouse room and a puzzle screen pixel exact, and BMF songs equal to DOSBox-X
   OPL captures write for write. By then the cost of the game-logic wave is known from the wave 1 specs.
4. Waves 3 (game logic) and 4 (fidelity) as in the TM DOS plan. Mods and an autoplay bot are optional.

---

Model: `re/dos/docs/SCOPING.md`. Repository sources: [ARCHITECTURE.md](ARCHITECTURE.md) (program,
flow, platform, UI library, relationship to TM), `fmt_data.md`, `fmt_graphics.md`, `fmt_sound.md` (formats),
[PORTING_NOTES.md](PORTING_NOTES.md) (engine mapping), `tools/gg/` (decoders), `re/names/gg/`
(names), and `re/docs/dgroup_map.md` §4.12 (the TM Windows menu fields).
