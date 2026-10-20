# Gizmos & Gadgets! (DOS, 1993, ssg.exe): architecture

This document maps the program of *Super Solvers: Gizmos & Gadgets!* (DOS v1.0, `ssg.exe` plus the
archives `gizmo000-003.dat`): how the executable is built, which module does what, how the game
flows from the attract loop to the final movie, the DOS platform layer, the TLC UI library it
links, players and saves, hints, and how all of it relates to Treasure Mountain. It was written in
wave 0 from the six scoping passes and re-checked against the bytes where marked; the wave 1
integration corrected it from the topic specs below, which specify every routine and win where
they are more detailed. The wave 2 engine foundation and what it verified are mapped in
`ENGINE_MAP.md` (module → source files → spec sections → status).

Reading order (all in this folder):

| # | Doc | Contents |
|---|---|---|
| 1 | `SCOPING.md` | the decision report: overlap, reuse, estimate, plan |
| 2 | `ARCHITECTURE.md` (this file) | program map, game flow, platform, UI library, saves, hints, relationship to TM |
| 3 | `fmt_data.md`, `fmt_graphics.md`, `fmt_sound.md` | the formats: `MRn` archives, LZH, bundles, resource types, text, game tables, `PLAYERS.DAT`; `LPX` pictures, palettes, fonts, DAM, the movie; `BMF` songs and banks |
| 4 | `code_runtime.md` | wave 1 platform spec: clock, start-up, `Seq_Run`, exit paths, timers, keyboard, mouse, zones, `Input_GetCode`, graphics primitives, dirty tiles, cursor, palette and fades, wipes, movie player, memory, resources, overlays |
| 5 | `code_sound.md` | the BMF driver (1bb4), the bank layer (1cdd), the music cue callbacks and who plays what |
| 6 | `code_ui.md` | fonts, text renderer, dialogs and buttons, menu bar, help, text viewer, scroll bar, attract, sign-in, `PLAYERS.DAT` I/O, New Player Hints, options |
| 7 | `code_world.md` | the state table, hub, building set-up and resume, rooms, items, actors, player, bananas, Cyber Chimps, the warehouse loop, `rand()` in the world |
| 8 | `code_vehicles_races.md` | parts, the HUD (master copy), workshop and blueprint, race preparation, the two races, results, post-race menu, final movie |
| 9 | `code_puzzles_a.md`, `code_puzzles_b.md` | the puzzle framework (state 9) and the eight puzzles |
| 10 | `PORTING_NOTES.md` | how the original maps onto the engine, and the behaviours to keep |
| 11 | `REFERENCE_CAPTURES.md` | the DOSBox-X reference captures (rounds 1 and 2) and their measurements |
| 12 | `ENGINE_MAP.md` | the engine (`src/gg/`): module → source files → spec sections → status and known gaps, the wave 2 stop/go results |

This document does not repeat the format or topic specs; it names a format or a routine and points
there. The reference decoders for the formats are in `tools/gg/`.

Conventions:

* **[O]** = observed in code, bytes or decoded data. **[I]** = inferred. Every rule cites the
  function that implements it.
* Code addresses are Ghidra `SSSS:OOOO` of the G&G project (`re/CONTEXT.md`, G&G section). Ghidra
  segment = image segment + 0x1000, as in the TM DOS project.
  * Resident code (Ghidra 1000..1d42): file offset in `ssg.exe` = `(SSSS − 0x1000)·16 + OOOO + 0x1200`.
  * Overlay code (Ghidra 2a80..4140): file offset = the overlay's file offset (§1.4) + `OOOO`.
  * `DS:XXXX` = DGROUP, Ghidra segment `1ea7`, file offset `0xFC70 + XXXX`. DGROUP is 0xBD04 bytes;
    initialised data ends at DS:4552, BSS follows [O exe pass].
  * Stub segments 1e0d..1e9f hold the overlay thunks (`stub:0020 + 5k` = entry k). A call through
    a stub is cited by its target (`2caa:1240`, not `1e2d:0020`).
  * Checked in this pass [O]: `1454:0046` → file 0x5786 holds the PIT install bytes
    `B0 36 E6 43 B8 00 20 …`; the 2b89 character table CS:03BA is at file 0x1CB40 + 0x3BA; DS:0004
    holds the Borland copyright.
* Names: the canonical names of `re/names/gg/` (after wave 1: 982 of the 1017 real functions and
  617 DGROUP labels, 626 after the wave 2 integration; `re/names/gg/README.md` lists the wave 1
  renames). Functions without a name
  (LZH and overlay-manager internals) are cited by address.
* Game text is referred to by resource id (`0x1019`) or DGROUP offset, with at most a few
  identifying words.

---

## 1. The executable at a glance

### 1.1 Toolchain and memory model [O]

| | |
|---|---|
| File | `ssg.exe`, 213 200 bytes, MZ, not packed |
| Linker | TLINK 5.0 (bytes `01 00 FB 50 6A 72` at file 0x1C) |
| Compiler | **Borland C++ 3.0** (DS:0004 "Borland C++ - Copyright 1991 Borland Intl." with TLINK 5.0; 3.1 links with TLINK 5.1); C only, no C++ in the game code |
| Model | **large**: far calls, far data pointers, one DGROUP (1ea7) set once by C0 |
| CPU | 80286 code (PUSH imm, ENTER, shifts by immediates): compiled with `-2` [I] |
| Frames | 191 functions use the VROOMM overlay-safe frame `INC BP; PUSH BP; MOV BP,SP … DEC BP` |
| MZ header | 0xDD pages, 976 relocations, 0x120 header paragraphs; load module file 0x1200..0x1BA00 (108 544 bytes); SS:SP 1a78:0080, CS:IP 0000:0000 |
| Link name | the overlay manager's default program name is `gizmo.exe` (1de3:0290): the program was linked as GIZMO.EXE and renamed [I] |
| Source files | assert texts name `\source\electric.c`, `puzzler.c`, `simple.c` |

Compared: TM DOS is Turbo C 2.0, 8086 code, EXEPACK, no overlays; TM Windows is Borland C++ 4.x,
Win16 NE. G&G sits between them in the Borland family.

### 1.2 Borland VROOMM overlays [O]

About half the game code lives in **35 overlays** (2 of them empty) that the Borland VROOMM manager
loads on demand from `ssg.exe` itself.

* `FBOV` header at file 0x1BA00: overlay data size 0x186C0, segment table at file 0xF030 (image
  segment 0de3), 82 entries of 8 bytes `{seg, maxoff, flags, minoff}`; flags 1 code, 3 code with
  overlay (a stub segment), 4 data.
* A **stub segment** (resident) starts with a 0x20-byte header (`CD 3F`, u32 overlay offset relative
  to FBOV+0x10, u16 code size, u16 fix-up bytes, u16 entry count), then 5-byte entries
  `CD 3F <off16> 00`. Callers far-call `stub:0020 + 5k`; INT 3Fh traps into the manager, which
  loads the overlay (and, as VROOMM does in general, patches the stub entries into far jumps while
  the overlay is in memory [I]).
* An **overlay record** is the code followed by u16 fix-up offsets. A fixed-up word holds a
  segment-table byte offset (index·8) that the manager replaces with the segment: 1583 far calls
  into resident code, 431 calls through other stubs, 9 references to the overlay's own stub (far
  function pointers), 4 others; 2027 fix-ups in all.
* Overlay manager: segment 1d42 (31 functions, data segment 1dd7): `__OvrInit` 1d42:08e9 (an
  `_INIT_` entry), `__OvrExit` 1d42:0191, `__OvrOpenExe` 1d42:000e, `__OvrTrap` 1d42:04f2 (the
  INT 3Fh handler), `__OvrLoad` 1d42:05a2. Overlays are read from the program file into the overlay
  buffer; no EMS or XMS calls exist. An overlay load is a disk read during play [I timing].
* Ghidra needed help: its MZ loader moves an overlay's first bytes into the previous block, and its
  switch recovery mishandles Borland `JMP CS:[BX+T]` tables. The project is built from a flattened
  image (each overlay at its own segment, fix-ups pre-translated, each stub entry rewritten as a
  far jump) written by `tools/gg/vroomm.py`, with `SetupOverlays.java`, `FixSwitches.java` (52
  switch tables) and `FixRealModeRefs.java` (near branches and `CS:` references across the 64-KB
  boundary in ovl10 and ovl27). `bash tools/gg/ghidra_import.sh` rebuilds it; `tools/gg/README.md`
  ("Ghidra project") describes the method, which is reusable for any VROOMM program, and
  `re/CONTEXT.md` (G&G section) the project and its remaining decompiler limits.

A port needs none of this: all code can be resident (`PORTING_NOTES.md` §6).

### 1.3 Resident segments

One Borland module ≈ one segment. Roles are [O] where a function is named in `re/names/gg/` or the
behaviour is cited, otherwise [I] from callees, strings and data use.

| Seg | Role | Funcs | Bytes |
|---|---|---|---|
| 1000 | C0 start-up + Borland C runtime (far heap, `open`/`read`/`write`/`lseek`, `sprintf`, string/mem, long math, `rand`/`srand`, `time`, `atexit`, `assert`) | 133 | 11 573 |
| 12e0 | `main` 12e0:0b38, `Seq_Run` 12e0:000f, the main-table state functions, the hub, `Game_Shutdown` 12e0:0b19 | 19 | 2 476 |
| 1399 | mode 13h primitives through the row table: lines, fills, get/put image, `LPX` blitters (raw, keyed, RLE), mono glyph blit, `Gfx_RestoreRect` / `Gfx_SaveToBack`, `Pal_SetRange`, cursor hide, movie delta decoder 1399:0a85 | 20 | 2 507 |
| 1454 | `Input_InstallHandlers` / `Input_RemoveHandlers`, `Timer_ISR` 1454:0096, `Kbd_ISR` 1454:00e4 | 4 | 391 |
| 1473 | screen set-up 1473:0074 (buffer, row table, mode 13h via 12e0:0b92, `Mouse_Init`), fades, palette merge, picture-with-palette show, `Screen_Show`, random wipe, palette save/restore, `Movie_Play` 1473:07d9 | 25 | 2 227 |
| 14ff | resources: `Res_OpenFiles`, `Res_Read`, `Res_LoadCopy`, `Res_LoadTo`; the screen swap file (`Screen_SaveToSwap` 14ff:0226) | 7 | 716 |
| 152c | deadline timers (`Timer_Set`, `Timer_Wait`, `Timer_Expired`, `Delay`) | 4 | 158 |
| 1536 | `Mem_Alloc` / `Mem_Free` / `Mem_CheckHeap` | 3 | 139 |
| 153f | parts catalogue: load 153f:0002, part accessors, the room deck, save 153f:0385 (`code_vehicles_races.md` §3) | 16 | 1 060 |
| 1581 | 8×8 dirty-tile map (in the code segment, 1581:0006) and its flush | 7 | 511 |
| 15df | the picture cache (300 entries), picture and bundle loaders that apply leading palettes (`code_runtime.md` §16.4) | 9 | 1 069 |
| 1622 | rooms: loader 1622:0085, enter 1622:0411, layered object drawing, save 1622:0fb3 | 30 | 4 727 |
| 174b | screen animations of the static screens (hub, workshop, attract, results) and their modal player `ScreenAnim_PlayModal` 174b:0522; not the actors (`code_world.md` §19.1) | 12 | 1 297 |
| 17a0 | `Mouse_EventHandler` 17a0:0004 (INT 33h fn 0Ch callback) | 2 | 186 |
| 17ab | mouse cursor, bounds, click-zone lists (`Zones_Push` / `Zones_Pop`, hit test 17ab:0507) | 28 | 2 189 |
| 1834 | actor system: 12 slots, draw 1834:0733, tick 1834:0ae9, save 1834:0b5e | 14 | 2 973 |
| 18f5 | player and projectile actors (callbacks set in 18f5:0721) | 12 | 2 426 |
| 198d | `Input_GetCode` 198d:0106, the key ring, flush, wait, `Input_GetHeldKeys` 198d:02b8 (held keys and the idle bit, for the player) | 8 | 773 |
| 19c0 | doubly linked lists of far nodes (items, carried parts, screen animations, hover regions, hints) | 13 | 1 087 |
| 1a04 | HUD and parts inventory strip | 27 | 3 410 |
| 1adb | rectangle overlap, wall collision and contacts (`Actor_CollideWalls`, `Chimp_ProbeContacts`, `Actor_UpdateContacts`) | 4 | 1 702 |
| 1b48 | LZH decompressor (`Lzh_Decompress` 1b48:02aa) | 12 | 1 739 |
| 1bb4 | sound driver (64 song slots, `Snd_Tick`, `Snd_Init`, `AdLib_Write`, `AdLib_Detect`) | 58 | 3 583 |
| 1cdd | sound banks (speaker/FM pairs), sound pause and Ctrl+S toggle | 23 | 1 520 |
| 1d3c | `Kbd_BiosGetKey`, `Kbd_BiosPeekKey`, `Dos_PrintString`, `Ptr_Normalize` | 4 | 95 |
| 1d42 | VROOMM overlay manager | 31 | 2 239 |
| 1dd7, 1de3 | overlay manager data, overlay segment table | – | – |
| 1e0d–1e9f | 35 stub segments, 217 thunks | (217) | 1 085 |
| 1ea7 | DGROUP | – | 0xBD04 |

### 1.4 Overlays

`ovlNN` is the overlay number (stub order). File offset = start of the overlay's code in
`ssg.exe` [O; the same values as `overlay_map.tsv` in this folder, which `tools/gg/vroomm.py map
--check` verifies against the exe]. Funcs are the scoping export's counts. All eight puzzle kinds are [O]: 0, 1, 2, 5 and 6 from assert
texts, strings and DAM calls, 3, 4 and 7 from the dispatch targets, the screen picture each
overlay draws and the round 2 captures (`code_puzzles_a.md` §1.4, `code_puzzles_b.md` §1.1).

| Ovl | Ghidra seg | Stub | File offset | Code bytes | Funcs | Role |
|---|---|---|---|---|---|---|
| 00 | 2a80 | 1e0d | 0x1BA10 | 2 390 | 10 | Cyber Chimps: set-up, behaviour, taking and dropping parts |
| 01 | 2b16 | 1e13 | 0x1C3C0 | 1 074 | 9 | fonts (TLC library) |
| 02 | 2b5a | 1e18 | 0x1C810 | 750 | 8 | `PLAYERS.DAT` open, write, save |
| 03 | 2b89 | 1e1d | 0x1CB40 | 1 189 | 6 | text renderer (TLC library) |
| 04 | 2bd4 | 1e20 | 0x1D000 | 830 | 7 | helpers: `Rand_Shuffle`, rank from points, thousands-comma formatter |
| 05 | 2c08 | 1e25 | 0x1D360 | 131 | 3 | `Video_DccSupported`, `Mouse_DriverPresent`, `Cpu_Detect` |
| 06 | 2c11 | 1e28 | 0x1D3F0 | 2 448 | 9 | attract loop (state 1 and its sub-sequence) |
| 07 | 2caa | 1e2d | 0x1DE30 | 5 173 | 14 | sign-in and roster (state 2) |
| 08 | 2dee | 1e30 | 0x1F380 | 7 622 | 29 | workshop: blueprint and vehicle assembly (state 6) |
| 09 | – | 1e34 | 0x21320 | 0 | 0 | empty |
| 10 | 2fcb | 1e36 | 0x21330 | 1 771 | 6 | race result, rank-up, post-race menu, final movie (states 12-15) |
| 11 | 303a | 1e3a | 0x21AD0 | 602 | 3 | `Init_CommandLine`, `Init_FatalMessage`, shutdown |
| 12 | 3060 | 1e3d | 0x21D60 | 547 | 6 | New Player Hints |
| 13 | 3083 | 1e41 | 0x21FB0 | 10 148 | 44 | puzzle 0, electric circuits (`electric.c`) |
| 14 | 32fe | 1e46 | 0x24880 | 3 301 | 18 | puzzle 4, balance |
| 15 | 33cd | 1e4e | 0x25620 | 7 892 | 26 | puzzle 6, energy forms |
| 16 | 35bb | 1e51 | 0x27620 | 7 604 | 31 | puzzle 7, magnets |
| 17 | 3797 | 1e5d | 0x29480 | 5 287 | 38 | puzzle 5, launch (the only DAM user) |
| 18 | 38e2 | 1e60 | 0x2AA50 | 1 050 | 11 | puzzle choice, history and dispatch (state 9) |
| 19 | 3924 | 1e65 | 0x2AEE0 | 320 | 8 | puzzle effect-bank helpers: load, play, play looped, stop, stop all, wait, free, transpose |
| 20 | 3938 | 1e6a | 0x2B040 | 4 376 | 23 | puzzle 1, simple machines (`simple.c`) |
| 21 | 3a4a | 1e6d | 0x2C260 | 3 664 | 19 | puzzle 2, machine pieces |
| 22 | 3b2f | 1e70 | 0x2D180 | 8 346 | 36 | puzzle 3, gears and buttons |
| 23 | 3d39 | 1e73 | 0x2F2E0 | 2 506 | 15 | DAM animation engine (TLC library) |
| 24 | 3dd6 | 1e7a | 0x2FCC0 | 2 575 | 6 | race, Automotive and Alternative Energy |
| 25 | 3e77 | 1e7d | 0x30740 | 3 459 | 7 | race, Aircraft |
| 26 | 3f50 | 1e80 | 0x31550 | 82 | 1 | rounding signed 32/16 division, called only by the races (3dd6:044e, 3e77:05e3) [O] |
| 27 | 3f56 | 1e83 | 0x315B0 | 3 548 | 14 | dialog boxes (TLC library) |
| 28 | 4034 | 1e88 | 0x32420 | 858 | 11 | buttons (TLC library) |
| 29 | – | 1e8e | 0x327C0 | 0 | 0 | empty |
| 30 | 406a | 1e90 | 0x327D0 | 914 | 5 | vertical scroll bar (the sign-in list; `code_ui.md` §9) |
| 31 | 40a4 | 1e94 | 0x32B80 | 1 563 | 6 | menu bar (TLC library) |
| 32 | 4106 | 1e97 | 0x331F0 | 368 | 7 | menu commands, message boxes from resource bundles |
| 33 | 411d | 1e9b | 0x33370 | 552 | 7 | help topics and pages |
| 34 | 4140 | 1e9f | 0x335A0 | 2 736 | 20 | scrolling text viewer (puzzle instructions) |

The scoping report grouped ovl26 with the UI library; it is a division helper of the races.

### 1.5 Size by role

| Part | Funcs | Bytes |
|---|---|---|
| C runtime, overlay manager | 164 | 13.8 KB |
| Overlay thunks (not real functions) | 217 | 1.1 KB |
| Engine, low level: graphics, resources and LZH, sound, timers, input, memory | 214 | 17.8 KB |
| TLC UI library: fonts, text, DAM, dialogs, buttons, scroll bar, menu bar and commands, help, text viewer (and the 82-byte ovl26) | 101 | 14.8 KB |
| Game: 8 puzzle mini-games | 235 | 49.5 KB |
| Game: rooms, actors, player, chimps, parts, collision, lists | 111 | 17.7 KB |
| Game: workshop 7.5 / races 5.8 / sequencer, hub and HUD 5.9 / attract, sign-in, save, results, hints, start-up 12.1 / puzzle choice 1.4 | 163 | 32.6 KB |
| **Game code in all** | **824** | **132.3 KB** |

[O sizes from the function list, KB = 1000 bytes; I grouping.] For scale, TM DOS has about 400
functions / 61 KB of game logic.

### 1.6 Start-up [O]

```
c0_startup 1000:0000
  _INIT_ table DS:452e: __OvrInit 1d42:08e9, _setupio 1000:0740, _setargv, _setenvp, tzset
main 12e0:0b38
  atexit(Game_Shutdown 12e0:0b19)
  Init_CommandLine 303a:005c
     Cpu_Detect 2c08:0032 -> DS:2450 (1 8086, 2 286, 3 386, 4 486); < 2 -> exit(5)
     Res_OpenFiles 14ff:0167 (the four archives, kept open; 0xA55A check -> "Incorrect Resource File n")
     switches (character by character, '-' or '/', any case)
     display check: Video_DccSupported 2c08:0000 (INT 10h AX=1A00h) must be >= 7 -> else message 0x369, exit
         (it returns AL = 0x1A on any BIOS that supports the function, -1 when AH is unchanged; BL,
         the display code, is never read, so every supporting BIOS passes; code_runtime.md §18)
     Mouse_DriverPresent 2c08:0011 -> else message 0x36a, exit
     farcoreleft() >= 0x58BEC (363 500 bytes) -> else message 0x36c, exit
     Snd_Init 1bb4:1234(DS:2452); DS:a0d8 = FM present; device 0 -> silence 1bb4:0b97
         (Snd_Init detects an AdLib only for device -1; devices 1 and 8 skip detection; any other
         device (0 from -SN, 4 from -SA/-SB/-SM) runs AdLib_Detect and then forces the OPL flag,
         so -SN loads the FM banks and plays them silently; code_sound.md §3.1)
     Input_InstallHandlers 1454:0000 (PIT, INT 8, INT 9, INT 33h fn 0Ch)
     DS:2454 = low word of time(NULL); srand(DS:2454) 303a:0229
  1473:0074   screen: 128 000-byte buffer, 400-row table DS:5f22, mode 13h (12e0:0b92), Mouse_Init 17ab:000e
  2b16:021c   fonts 9 and 10
  15df:000d   picture and animation set-up
  12e0:0afb   actors 18f5:0721, workshop 2dee:1db5, HUD 1a04:025b, SndRes_Init 1cdd:0004, hint queue 3060:0013
  DS:455c = 1; Seq_Run(DS:00c2, &DS:455c)
  exit(0)
```

Command-line switches (`Init_CommandLine` 303a:005c; `-S` keys in a sparse switch at 303a:0234):

| Switch | Effect |
|---|---|
| `-?` | usage text (resource 0x368), exit |
| `-GV` | DS:244e = 8: **skips** the INT 10h/1A00h test (which any BIOS supporting the function passes anyway, see above); it selects nothing, mode 13h is the only mode |
| `-S` + `N`, `-` or nothing | sound device 0 (off): the FM banks are still loaded and played silently |
| `-SI` | 1, PC speaker |
| `-SA`, `-SB`, `-SM` | 4, FM: AdLib, Sound Blaster and Microsoft's sound card all mean the OPL2 at 388h; there is no digitised audio code; the OPL flag is forced, so without a card FM plays to nothing |
| none | DS:2452 = −1: auto-detect the AdLib |

`ssg.bat` (the launcher) reports exit code 5 as "no 286". `main` already executes `PUSH imm` before
the CPU test, so a real 8086 probably never reaches it [I]. The CPU class changes one game rule
(§2.6).

Resources 0x368..0x36c are the start-up messages; there is no switch or driver text in the EXE,
which holds only 92 strings (run-time messages, file names, assert texts, rank names, energy
names, simple-machine examples, menu and button words) [O]. All other game text is in the
archives.

---

## 2. Game flow

### 2.1 `Seq_Run`: one table-driven state machine [O]

`Seq_Run(table, &state)` (12e0:000f) calls the far function at `table[state]` until it reaches a
NULL entry. The return value decides the next state:

| Return | Next state |
|---|---|
| 0 | state + 1 |
| n > 0 | n |
| −2 | the previous state (one step of history only; −1 before any change) |
| −1 | stop: `Seq_Run` returns |

Index 0 of every table is NULL and unused; the main loop starts at state 1. The same runner drives
two sub-sequences inside states 1 and 6. Inside a state the work is done by nested modal loops
(§3); a request to restart or quit travels as an input code that the active loop turns into a
state change (§2.11).

### 2.2 The main table DS:00c2 [O table bytes, checked in this pass]

| # | Function | Role |
|---|---|---|
| 1 | 2c11:0913 (ovl06) | attract loop (§2.3) |
| 2 | 2caa:1240 (ovl07) | sign-in (§6.1) |
| 3 | 12e0:0148 | Technology Center hub (§2.4) |
| 4 | 12e0:0820 | building and race set-up, or resume (§2.5) |
| 5 | 12e0:096a | enter the current room: menu bar on (`40a4:0000(DS:4053)`), play mode DS:0096 = 1, room 1622:0411; room 0 → 6, else → 7 |
| 6 | 2dee:1a90 (ovl08) | workshop (§2.7) |
| 7 | 12e0:0619 | warehouse room loop (§2.6) |
| 8 | 12e0:09aa | leave the room: puzzle door (DS:00bc) → 9; finished vehicle (DS:00be) → save, 10; else → 5 |
| 9 | 38e2:0000 (ovl18) | puzzle (§2.8) → 5, or 16 through the in-puzzle menu |
| 10 | 12e0:0506 | list the vehicle's parts (records with +0x0C = 0 and flag 0x8000) into DS:6616 → 11 |
| 11 | 12e0:0564 | race (§2.9) → 12, or 14 after a rematch (DS:455e ≠ 0; 12e0:05c4..05d3) |
| 12 | 2fcb:0325 (ovl10) | race result (§2.10) → 13, or 15 after the 15th win |
| 13 | 2fcb:0573 | post-race buttons |
| 14 | 2fcb:0315 | redraw the result → 13 |
| 15 | 2fcb:0640 | final movie, wait for a key, save → 17 |
| 16 | 12e0:012f | save (2b5a:01ea), free the building (12e0:05d5), return DS:455a (2 = sign-in, 3 = hub) |
| 17 | 12e0:0141 | returns 0 → index 18 is NULL → `Seq_Run` ends → `exit(0)`: the game ends after the final movie |

States 4 and 10 (12e0:0820, 12e0:0506) are reached only through this table [O systems pass]; the
scoping export lacked functions for them, the wave 0 project creates them from the far code
pointers in DGROUP.

```
 1 ATTRACT --Enter, Space, last page timed out--> 2 SIGN-IN --> 3 HUB --building--> 4 SET-UP / RESUME
   (Esc: quit)                                                                         |
                                                                                       v
                    +----------------------------------------------------------> 5 ENTER ROOM
                    |                                                            |          |
                    |                                                         room 0     room n
                    |                                                            v          v
                    |                                                     6 WORKSHOP   7 WAREHOUSE
                    |                                                            |          | door
                    |                                                            +----+-----+
                    |                                                                 v
                    +---- other door -------------------------------------------- 8 LEAVE ROOM
                    |                                                            |          |
                    +---- 9 PUZZLE <------------------ puzzle door --------------+          |
                                                                       vehicle built, save |
                                                                                            v
                                     13 POST-RACE <-- 12 RESULT <-- 11 RACE <-- 10 VEHICLE PARTS
                                     rematch 11, next race 4,   |  (14 redraws 12)
                                     warehouse 5, hub 3         +-- 15th win --> 15 FINAL MOVIE --> 17 END --> exit

 menu "Start New Player" / "Return to Technology Center" in 6, 7 or 9 --> 16 SAVE --> 2 / 3
```

The workshop returns its next state in DS:a0ca: 8 (leave through a door) or 16 (2dee:0e27..1317)
[O].

### 2.3 Attract loop (state 1, sub-sequence DS:20a8) [O]

`2c11:0913` runs `Seq_Run` over an 11-entry table: title animation 2c11:043f, animation 05cf, six
story pages 0630 (texts 0x1019..0x101e in a box at (57,38), 177 px wide; the first page also shows
the "Press Enter" line 0x1024), animations 077f and 07e5, and the sign-in prompt page 084c (picture
0x1029, text 0x1025).

* Each page waits **0x111C ticks = 30.07 s** on timer slot 1. The title has three timed phases
  of 1, 3 and 5 s (`Timer_Set(1, n·146)`) with effects 12 and 13 between them.
* Enter = next page, Space = skip to the sign-in **at every step**, **Esc = quit at once**
  (DS:455a = −1, no Yes/No box); a click anywhere is Enter (zone DS:0a3a).
* The last page's time-out returns 1: the attract loop starts again from the title.
* The attract makes no `rand()` call. Details: `code_ui.md` §10.

### 2.4 Technology Center hub (state 3) [O]

`12e0:0148`: the hub screen shows each building's trophies (DS:4596[b] + 1), recoloured with the
vehicle colour of each race (`g_vehicleColor` DS:5c10). Zones DS:0132 (release codes only): 0xBB9
Automotive (4 rectangles), 0xBBA Aircraft (1), 0xBBB Alternative Energy (2) are buildings 0..2; a
building with all five races won answers with dialog 0x106E. **0xBB8 is the sign-in booth**
(14, 118, 82×63) and returns state 2 (the sign-in) [O code, capture]. The hover labels are the
cursor shapes of resource 0x101F, chosen by zone code − 0xBB8. Esc runs Save & Exit (4106:00b2);
0xBBC is in the switch but no zone uses it. The loop paces at 4 ticks per frame and calls no
`rand()`. Details: `code_world.md` §3.

### 2.5 Buildings, races and levels (state 4) [O]

There are 3 buildings × 5 races = 15 races. Per building, DS:4596[b] counts the races won (−1..4).

* `12e0:0820`: level DS:4594 = DS:4596[b] + 1; race index **DS:4590 = b·5 + level** (0..14) selects
  every per-race table: the parts catalogue (resource 0x2F5 + DS:4590, 153f:0002), the rooms
  (1622:0085), the chimp list (DS:1bae[DS:4590]), the race backgrounds and paths (§2.9).
* New visit: player set-up 18f5:094d; bananas DS:457e = 10 **only at level 0** (12e0:08f8..0905),
  so they carry over between the races of a building; `Chimps_SetupActors` 2a80:0635 (the chimp
  actors: no placement, no `rand()`; it also runs on a resume); `Chimps_Spawn` 2a80:0784 places
  them (`rand()`). The set-up's `rand()` calls (deck and item shuffles, a coin, the chimps) are
  listed in order in `code_world.md` §4.4.
* Resumed visit (DS:459c[b] ≠ 0): the live state is restored from the player record's block for
  that building (DS:47e0 + b·0x6BA) **without asking** (TM DOS asks "Continue last game?") (§6.3).

### 2.6 Warehouse rooms (state 7) [O unless noted]

**World.** Rooms do not scroll: each is a fixed 320×165 picture over a 35-px HUD (y 165..199);
the menu bar is a 9-px strip drawn over the room's top rows. At room entry the room is drawn on
the screen and saved into the back page with `Screen_SavePlayArea` (1473:0337 =
`Gfx_SaveToBack(0, 0, 320, 165)`), from which sprites are erased every frame. Actors are clamped
to x ≥ 1 and x + w ≤ 319 in the step interpreter (`Actor_RunStep` 1834:068e..06b1).

**Rooms** come from resources per building and race (1622:0085): a u16 count, then per room a
0x120-byte header with counts and far-pointer slots patched at load; 4 depth layers of 0x20-byte
objects (type at +0x10: 3 picture, 7 locked puzzle door, 9 open door; doors 7..11 carry a link id
in `+0x12 >> 6` and a target room in `+0x12 & 0xF`), 4 lists of 0x48-byte animated objects and
3×6 lists of 10-byte records. At most 16 rooms (far pointers DS:7a04); the current room is copied
to DS:08c6. **Room 0 is the workshop** (state 6). Room flags bit 0 = a hall (6 floors, small
scale), else a small room (3 floors); open doors use picture 0x33C in halls and 0x30E in small
rooms. There are no ladders or elevators: floors change by falling at an edge, by hall pads and
flaps, or by doors, passages, holes and the workshop tube (`code_world.md` §5, §11).

**Frame order** (`12e0:0619`, 4 ticks per frame; `code_world.md` §14.2): `Hint_Update`;
`Timer_Set(0, 4)`; `Actors_Tick` (1834:0ae9; non-zero leaves the room at once, with no drawing,
wait or input in that frame); `Sprites_EraseAll`; layer 1 objects (1622:0922), animated list 0
(1622:0706), layer 2, the actors (1834:0733), animated list 3, layer 3 objects and then the room's
items; `Sprites_DrawAll`; `Hud_Update` (1a04:0b69); `Dirty_Flush` (1581:052a); `Timer_Wait(0)`;
`Input_GetCode` and dispatch.

**Actors**: 12 slots of 0xA0 bytes at DS:7a8a: 0 player, 1 projectile, 2..11 Cyber Chimps. Each
actor has function pointers (brain, contacts, sequence end, room entry and others) set in
18f5:0721 (player 18f5:029b brain, 04cf sequence end, 0409 room entry, 0180 hit by a chimp;
projectile 18f5:06fb, 0401) and 2a80:0112 (chimp brain); its animation comes from an actor set
(`S` 0x1033.., loaded by `Actors_LoadSet` 1834:0088) run by the step interpreter
`Actor_RunStep` (1834:01b6, 46 opcode slots); walls and contacts come from 1adb
(`Actor_CollideWalls` 0093, `Chimp_ProbeContacts` 01ef, `Actor_UpdateContacts` 03b0). Details:
`code_world.md` §9..§13.

**Doors and puzzles.** Up in front of a door starts the player's door sequence; at its end
`Player_SequenceEnd` (18f5:04cf) acts on the door's type, and for a type-7 door sets DS:00bc
(18f5:057b) and leaves the room; state 8 then goes to the puzzle (state 9). A solved puzzle turns
the door and its twin into type 9 (`Room_OpenPuzzleDoor` 12e0:0a24); the player goes through only
after Enter or a click on the puzzle's panel (DS:b144), otherwise he steps out of the same door.

**Parts.** The catalogue (resource 0x2F5 + DS:4590, 153f:0002) holds 0x8E-byte part records (text
resource at +0x16, picture at +0x10), followed by Morty's vehicle (DS:6634). At most 2 parts of a
category are carried, as a pair (the second on the upper row of the strip); only one of a pair can
be installed (1834:08d7 / 0990). Picking up the **first** part of a needed type decrements the
counter of needed parts DS:4582; a pick-up adds **20 points** (12e0:0ace(0x14)) unless the part's
flag 0x4000 is set (a recycled or trashed part, 2dee:10ff, 1a04:0a82). Hints 0x1044, 0x104A, 0x1050
and 0x1047 react to part pick-ups. Details: `code_vehicles_races.md` §3.

**Cyber Chimps** (2a80) [O code and data, I behaviour where marked]:

* The far pointers DS:1bae[DS:4590] (`g_chimpLists`) lead to lists `{u16 count, u16 room per
  chimp …}` in DGROUP. The counts are 2, 4, 6, 8, 10 for the 1st..5th race of every building, and
  every list's first room is 1 [O data].
* `Chimps_Spawn` (2a80:0784) fills actor slots 2..11: slot k < count + 2 becomes a chimp in the
  next listed room, on a random floor (R(6) in a hall, R(3) in a small room) at x = 10 or 300
  (R(100) < 50 → 10); the other slots are disabled (actor +0x2A = 1). "Without Chimps" (DS:4578 =
  0 → actor +0x2A = 1) disables them all.
* **286 quirk** (2a80:07b1): when `Cpu_Detect` returned 2 (DS:2450), the code halves **the word
  after the count**, in place in DGROUP. That word is the first chimp's room, so on a 286 the first
  chimp of every race goes to room 1/2 = 0, the workshop, and the halving repeats harmlessly at
  every set-up [O code 2a80:07a4..07c3, DS:1adc..1b98]. The count is unchanged. The scoping notes
  read this as "the chimp count is halved". Room 0 has no walls, doors or floor references, so
  that chimp can never leave and is never drawn: **one chimp fewer in the warehouse on a 286**;
  its first brain call still draws one R(1000) [O, `code_world.md` §13.1].
* Behaviour is data-driven: move lists with R(1000) chances per probe (DS:1d2e) and transitions
  (DS:1f10), run by `Chimp_Brain` 2a80:0112. A chimp takes the item it touches (step op −10,
  `Chimp_TakePart` 2a80:0000: a part unless it already holds one; a crate is re-placed in a random
  room) and, on contact with the player, a random part from him (`Player_HitByChimp` 18f5:0180,
  R(carried)). A banana stuns it (sequences 30/31) and it drops its part where it stands
  (`Chimp_DropPart` 2a80:00a1) [O, `code_world.md` §11.7, §13].
* The player's projectile (actor 1, a banana, launched by 18f5:0895) uses the counter DS:457e (10
  per building, set at level 0). Banana crates (item kind 1000) are dealt into the rooms with the
  parts at set-up; each gives 5 bananas, and when the last banana goes and no crate is left the
  crates are dealt again (`code_world.md` §7.6, §12).

**HUD** (1a04, y 165..199; master spec `code_vehicles_races.md` §4): 6-digit score (u32
DS:4572), 3-digit banana counter (DS:457e), 2-digit counter of needed parts (DS:4582), a scrolling
inventory strip of the carried parts (input codes 6000..6003 go to `Hud_TrashCursorPart`
1a04:0a82, `Hud_PutBackCursorPart` 0b5b, `Hud_ScrollLeftArrow` 08ad and `Hud_ScrollRightArrow`
084c; 5000 + n, `Hud_SelectPart` 090e, attaches part n to the cursor, 17ab:01f0), dirty bits
DS:132c (`g_hudDirty`).

**Leaving.** Esc → Save & Exit. The menu codes 0x17A2 / 0x17A3 (Return to Technology Center /
Start New Player) set DS:455a = 3 / 2 and go to state 16.

### 2.7 Workshop (state 6, ovl08) [O]

`2dee:1a90` calls the workshop loop `Workshop_Run` 2dee:0c89 (which always returns −1) and then
runs `Seq_Run` over DS:219a with the state index DS:a088, which nothing ever writes: entry 0 is
NULL, so **nothing runs** (dead code; DS:219e holds a far pointer to `Workshop_Run`, and the words
after the NULL at DS:21a2.. are the shelf zone records 1004 / 1005, not table entries) [O
2dee:1cda, 12e0:000f]. The blueprint opens by itself on the first visit to a level (while
DS:457c = 0); the player builds the vehicle from the shelf conveyor: zones 1000..1009 and part
codes 4000 + n. The loop paces at 4 ticks per frame (2dee:0d38). The state returns DS:a0ca = 8
or 16; a finished vehicle (DS:00be) leads through state 8 to the race. Details:
`code_vehicles_races.md` §5, §6.

### 2.8 Puzzles (state 9, ovl18 and the puzzle overlays) [O]

`38e2:0000`:

1. Kind = R(8) (38e2:01e8).
2. Candidates: the **first** up to 3 unsolved variants of that kind in index order (38e2:0202);
   a kind with nothing unsolved is drawn again (one `rand()` per try). When at most 4 unsolved
   variants remain over all kinds (`Puzzle_FewUnsolvedLeft` 38e2:019e), the variant is
   R(variants) over the whole kind, solved or not. One candidate is picked with R(k), even when
   k = 1 (`code_puzzles_a.md` §1.3).
3. Dispatch 38e2:00d4 (table CS:018e; read it from the assembly):

| Kind | Entry | Overlay | Variants (DS:3334) |
|---|---|---|---|
| 0 electric circuits | 3083:08a1 | ovl13 | 30 |
| 1 simple machines | 3938:0000 | ovl20 | 20 |
| 2 machine pieces | 3a4a:0000 | ovl21 | 20 |
| 3 gears and buttons | 3b2f:1ed0 | ovl22 | 31 |
| 4 balance | 32fe:0aa0 | ovl14 | 32 |
| 5 launch | 3797:1138 | ovl17 | 30 |
| 6 energy forms | 33cd:0000 | ovl15 | 28 |
| 7 magnets | 35bb:1c03 | ovl16 | 30 |
| | | | **221** [O bytes, checked] |

4. A solved puzzle sets its flag (DS:45d0 + kind·0x42 + variant·2; a set flag is never cleared)
   and `Room_OpenPuzzleDoor` (12e0:0a24) opens the door on both sides. The player goes through only
   after Enter or a click on the panel (DS:b144 = 1); Go Back leaves him in the room (DS:b144 = 0);
   leaving through the menu keeps the previous puzzle's DS:b144 (an original quirk). A solve gives
   +400 points (4 × 100 with effect waits).

Each kind is hand-written logic over per-variant data (`F` resources, `D` tables, DGROUP tables,
and for energy forms immediate stores in the code); instructions are shown in the scrolling text
viewer (ovl34); ovl19 holds the puzzle effect-bank helpers. The puzzle main loops are unpaced
polling loops. Zone codes: 0x44C..0x459 and 0x4B0..0x4BF (and more in gears), 0x5DD Go Back,
0x5DE the panel. Details: `code_puzzles_a.md`, `code_puzzles_b.md`.

### 2.9 Races (states 10-11, ovl24 and ovl25) [O]

`12e0:0564`: building 1 (Aircraft) runs 3e77:0a23 (ovl25), the others 3dd6:0762 (ovl24). The
player's vehicle (parts DS:6616) races Morty's (DS:6634, a fixed part list per race) along `R`
paths (6-byte points `{x, y, code}`; per-race tables DS:39f4 / DS:3a12); per-race backgrounds
DS:399a, sprites DS:39b8 / DS:39d6 and extra laps DS:3a30. Performance (top speed, limit,
accelerations) is computed **once per race** from the part lists (`Race_ComputePerformance`
3dd6:044e, `AirRace_ComputePerformance` 3e77:0036); each frame integrates the speed
(`Race_StepSpeed` 3dd6:0034); the land race moves int(speed) path points per frame (the fraction
is not carried), the aircraft race keeps the fraction. The player has **no input** during a race
(198d:00d4 discards one typed key per frame) and no `rand()` is called. The music arrangement 2 or
3 follows the leader. Ties: land race → the faster racer wins (the player on equal speed),
aircraft race → the player wins. Races pace at **8 ticks per frame** (3dd6:028e, 3e77:066d;
measured 7.94). The result is DS:4560 (0 = won). A win that is not a rematch (DS:455e = 0)
increments DS:4596[b] and the total DS:5c0e. Details: `code_vehicles_races.md` §7..§10.

### 2.10 Results, post-race menu, final movie (states 12-15, ovl10) [O]

* **Result** 2fcb:0325: "1st -" / "2nd -" with the player's name or "Morty" (DS:2442 / 2448,
  DS:243c), a winner animation, the reward **`I` 0x1071[DS:5c0e − 1] × 1000** (it follows the
  races won in all, not the race) counted up over 50 frames of 4 ticks with a click per frame (HUD
  bit 4) [O 2fcb:0398..03e5; the captures saw +4000 for a first win and +300 000 for the 15th].
  A lost race gives nothing. Won: the rank message 2fcb:01dd (called from 2fcb:0552) after
  **every** win while the rank is below 7, not only on a rank change. Lost: hint 0x104F. Then
  state 13, or state 15 when DS:5c0e = 15. A rematch goes 11 → 14 (redraw, no count-up, no
  points).
* **Post-race** 2fcb:0573: buttons (zones DS:22fe, codes 2000..2004): rematch (DS:455e = 1 → 11),
  next race (→ 4), back to the warehouse (→ 5), hub (→ 3), Save & Exit. Leaving after a win clears
  DS:459c[b] and saves.
* **Final movie** 2fcb:0640: `Movie_Play(0x3000, 0x30CA, 18, cues DS:236c, sound)` from 2fcb:06a5.
  202 delta frames (volume 3), drawn straight into VRAM at **18 ticks per frame = 8.09 frames/s**
  (the 18 is the frame period, not a cue count); the cue records `{type, frame, a, b}` (25 and an
  end record of type 6) do 0 = `Delay(a)`, 1 / 5 = start sound a, 2 = stop. A music-synchronised callback (2fcb:0667 sets slot
  tick 0x250 → `Music_MovieCue` 12e0:00ea: the music's arrangement pass by pass is 1, 2, 3, 4,
  all, all, all, 1, …; `code_sound.md` §14.3) is part of it. The movie loads no palette: it draws
  over the results screen with that screen's palette (base colours 0..31 plus 0x1077) [O: no fade
  or palette load between the rank box and the movie in `r2_movie_race15.avi`;
  `fmt_graphics.md` §7.4]. Then a key, a save, and the end of the game.

### 2.11 Quitting and restarting [O]

* Save & Exit (menu, Esc in the hub and the rooms): save 2b5a:01ea, `exit(0)` (4106:00b2).
* Start New Player / Return to Technology Center: the menu sets DS:130e = 2 / 1; `Input_GetCode` turns
  it into the input code 0x17A3 / 0x17A2 (198d:0209..0226), which the active loop answers by going to
  state 16 with DS:455a = 2 / 3.
* Esc in the attract loop quits without a box.

---

## 3. Runtime model and timing [O]

### 3.1 Clock

* `Input_InstallHandlers` (1454:0000) programs PIT channel 0, mode 3, divisor **0x2000**:
  1 193 181.67 / 8192 = **145.652 Hz** (6.866 ms per tick), then port 43h = 0xB6 (channel 2 for the
  speaker). `Input_RemoveHandlers` (1454:0058) restores divisor 0.
* `Timer_ISR` (1454:0096): u32 tick DS:021a += 1, u16 DS:021e += 1, `Snd_Tick` (1bb4:0db3) on even
  ticks = **72.826 Hz**, EOI. **No game logic runs in the interrupt.**
* The ISR is meant to chain the BIOS handler every 8th tick, but its counter DS:0216 starts at 0
  and is only ever reset to 0 (1454:00a5 / 00b0 are its only writers), so the BIOS handler runs on
  **every** tick: the DOS clock and INT 1Ch run 8× fast while the game runs [O code, I intent].
  The game reads the DOS time once, for `srand`, right after installing the handler (§8).

### 3.2 Deadline timers (152c)

8 u32 slots at DS:6578 (slot = 4 bytes; DS:6598 is the next variable).

| Function | Behaviour |
|---|---|
| `Timer_Set(slot, n)` 152c:0009 | deadline = tick + n |
| `Timer_Wait(slot)` 152c:0029 | busy-waits until **tick ≥ deadline** |
| `Timer_Expired(slot)` 152c:004a | returns 1 only when **tick > deadline** |
| `Delay(n)` 152c:0072 | busy-waits until tick ≥ start + n, calling `Input_GetCode` (198d:0106) in the loop and discarding its codes |

`Timer_Wait` and `Timer_Expired` disagree by one tick: a deadline tested with `Timer_Expired` fires
one tick later than a wait on the same slot [O, checked in this pass]. Because `Delay` runs
`Input_GetCode`, the global keys of §4.6 (Ctrl+S, Esc, the menu keys) still act during a delay while
other codes are lost [O loop, I consequence].

Slots in use (every call site, `code_runtime.md` §5): 0 the frame pacer (and the magnets
simulation substeps, the movie frames), 1 the attract pages and the title phases, 2 the wipe, 6
the text viewer and the launch puzzle, 7 puzzle animations; 3, 4 and 5 are unused. `Timer_Set`
takes a u16 count.

### 3.3 Nested blocking loops and frame pacing

Every interactive screen is an ordinary loop inside a state function:

```
loop:
    Timer_Set(0, 4)                 # 8 in the races
    update, draw into the RAM screen
    Dirty_Flush 1581:052a           # copy dirty 8x8 tiles to A000
    Timer_Wait(0)
    code = Input_GetCode()             # 198d:0106
    dispatch(code)                  # may return a state number
```

Seen at the hub 12e0:03bf, warehouse 12e0:06a7, workshop 2dee:0d38, animation players 174b:052c and
2c11:010f, results 2fcb:044d; 8 ticks at 3dd6:028e and 3e77:066d. The 4-tick timer of the magnets
puzzle (35bb:119f) paces its simulation substeps, not a frame loop. **Unpaced** polling loops (no
frame timer; they draw only on change): the eight puzzle main loops, the post-race menu, the
blueprint viewer, the dialog and menu loops and `Input_WaitCode` (`code_puzzles_a.md` §2.3).
Measured on the round 2 AVIs (REFERENCE_CAPTURES §10.1): 3.94 ticks (count-up), 7.94 (races),
12.0 (walk animation, 3 frames), 16.1 (hub windmill), 18.0 (movie).

* 4 ticks = 27.46 ms = **36.41 frames/s**; 8 ticks = **18.21 frames/s**. Because the next
  `Timer_Set` follows the wait at once, a frame that fits takes exactly 4 ticks.
* One logic step per frame and no catch-up: a frame whose work takes longer than its ticks simply
  makes the game slower [I].
* This is TM Windows' model (nested blocking loops on deadline timers), not TM DOS's (logic inside
  the timer interrupt) [O verify pass].

### 3.4 Other time bases

| What | Time | Source |
|---|---|---|
| Attract pages, idle | 0x111C ticks = 30.07 s | 2c11:0725 / 08c0; the idle bit 0x40 of `Input_GetHeldKeys` 198d:02b8 after 0x111C ticks with no held key (for the player actor) |
| Hint delays | 0x490 = 8.02 s, 0x2238 = 60.14 s | 3060 (§7) |
| Results count-up | 50 frames of 4 ticks (1.37 s) | 2fcb:0325 |
| Button flash | 4 toggles with `Delay(4)` | 4034:01d6 |
| Final movie | 18 ticks per frame; `Delay(1460)` cues of 10.06 s | 1473:07d9 |
| Palette loads and fades | vertical retraces (70.086 Hz), not PIT ticks: 22 visible frames out, 16 in | §4.2 |
| Random wipe | 2 ticks per step; 18..48 steps by pattern (45 frames measured) | §4.2 |
| Sound | 72.826 Hz (every 2nd PIT tick) | §4.7 |

### 3.5 Time stands still in dialog boxes [O]

The dialog modal loop (3f56:0655) saves the u32 tick DS:021a into DS:6598 at entry and writes it
back at exit (3f56:08c3..08cd), so every deadline and every queued hint sees no time pass while a
box was open. It also clears play mode (DS:0096) and pauses the sound (1cdd:05db / 05e5) for the
duration. This is the only writer of DS:021a outside the ISR. The help pager freezes time too,
because each of its pages is a `Dlg_RunModal`; the menu drop-down loop (40a4:020b) and the puzzles'
text viewer (`TextView_Run` 4140:0667) do not (`code_ui.md` §2.2).

---

## 4. Platform layer

### 4.1 Video and frame buffer [O]

* Mode: only VGA/MCGA mode 13h (320×200, 256 colours), set with `INT 10h AX=0013h` (12e0:0b92,
  flag DS:01f6 = 1) and left with `AX=0003h` (`Video_RestoreMode` 1399:000e). INT 10h is used three
  times in the whole program. No mode X, no CRTC/sequencer/GC writes, no page flipping, no hardware
  scrolling; VGA ports 3C8h, 3C9h, 3DAh only.
* Buffer: `1473:0074` allocates **128 000 bytes** and builds a 400-entry table of normalised far
  row pointers (DS:5f22, stride 320). **Rows 0..199 are the composed screen, rows 200..399 the back
  page** (the clean background). All primitives draw through the row table; sprites are erased by
  copying rows y+200 → y (`Gfx_RestoreRect` 1399:0233, the only back-page → screen copy); a
  background drawn on the screen is saved into the back page with `Gfx_SaveToBack` 1399:02a2
  (screen rows y.. → rows y+200.., no dirty mark; `Screen_SavePlayArea` 1473:0337,
  `Screen_SaveAll` 1473:034e). VRAM is the
  far pointer DS:01f8 = A000:0000.
* Primitives (1399): HLine, VLine, FillRect, PutPixel, GetImage, PutImage, `LPX` blitters through
  tables DS:01fc / DS:0204 (raw, keyed, RLE), XOR-0x0F rectangle 1399:0146, LUT remap of a raw
  picture 1399:0b71, mono glyph blit 1399:0541. Every primitive first calls `Gfx_HideMouseOnce`
  (1399:0b22), which restores the background under the software cursor on the first draw.

### 4.2 Palette, fades and transitions [O]

* Shadow palette DS:024e (768 bytes of 6-bit RGB); DS:024c = 1 while faded to black. `P`
  resources are DAC ranges merged into the shadow (1473:02f0 / 041c) and loaded only when not
  faded (format: `fmt_graphics.md`).
* `Pal_SetRange` (1399:05d6) writes 128 colours per group after waiting for the **start** of a
  vertical retrace (3DAh bit 3 off, then on), with CLI around each group. Every call passes 2 groups,
  so **one palette load spans 2 retraces** (~28.5 ms at 70.086 Hz). There are only 3 call sites and
  no other DAC writes.
* `Pal_Fade(n, from, to)` (1473:015c): step = 0x3F/n + 1, per-colour delta |d|/step + 1, step + 1
  loads. Fade to black `Pal_FadeOut` 1473:0265 (n 6) = 12 loads = 24 retraces ≈ 0.34 s, of which 11
  change colours: **22 visible frames** (the 12th load rewrites black); fade in `Pal_FadeIn`
  1473:02a0 (n 8) = 9 loads ≈ 0.26 s, 8 changing: **16 visible frames**; `Pal_FadeFromSaved`
  1473:02db fades from DS:5c22 to the shadow in 9 loads. **Fades are paced by the retrace**, not by
  the PIT. The round 2 AVIs show exactly 22 and 16 frames, 2 frames per load; between a fade-out
  and the next fade-in the screen stays black for 7-18 frames while the next screen is loaded
  (`code_runtime.md` §13.4, REFERENCE_CAPTURES §10.2).
* `Screen_Show` (1473:045a): if not faded, a random wipe and a cleared dirty map; else flush and
  fade in. 1473:0325 = fade out, flush, fade in.
* `Screen_RandomWipe` (1473:0042) reads one of the 8 wipe patterns, resource 0x11D6 + R(8), and
  `Screen_Wipe` (1473:0479) copies rows 9..164 from the RAM screen straight to A000 column span by
  column span in that pattern, paced by timer slot 2 (2 ticks per step); it also starts one of two
  wipe sounds (R(2)). The `W` pattern format (period, sweep flags, 156 left and right start
  columns, per-step increments) and the eight patterns are decoded in `code_runtime.md` §14.2,
  including an original bug: pattern 0x11DA never copies column 319. A door entry measured 45
  frames (642 ms) with no palette change: pattern 0x11DB, as the wave 2 engine showed frame by frame. The movie (1399:0a85, 1473:07d9) also writes straight to
  VRAM. These two paths bypass the dirty-tile map.
* Palette save/restore around sub-screens: `Pal_Save` 1473:074e / `Pal_Restore` 0766 (shadow ↔
  DS:5c22, no DAC write).

### 4.3 Presentation: dirty tiles and the software cursor [O]

* `Dirty_Mark` (1581:03ee, called by every primitive for rows < 200) sets bytes in a 40×25 map of
  8×8 tiles that lives in the code segment (1581:0006, 1000 bytes); `Dirty_Clear` 1581:0470.
* `Dirty_Flush` (1581:052a) draws the software cursor into the RAM screen (17ab:0612), copies every
  dirty tile to A000 (4 MOVSW per row), counts tiles (DS:08b8) and flushes (DS:08bc). There is **no
  retrace wait**, so tearing is possible. The game loops call it once per frame.
* The mouse event handler also calls `Dirty_Flush` **at interrupt time** when the pointer moves, so
  the cursor follows the mouse while the main loop waits (§4.5).
* Cursor shapes are resources (17ab:0083); the active cursor is a 24-byte copy at DS:7a6a whose
  save-under position is DS:7a6c / 7a6e and buffer DS:7a7e (`Mouse_RestoreUnder` 17ab:072a). The
  cursor picture's top-left is at the pointer; the hot spot (DS:7a74 / 7a76) only shifts the click
  position (§4.5).
* The DAM engine keeps its own update-rectangle list (8-byte x, y, w, h at DS:b86e, count
  DS:398a), flushed by 3d39:0983 through the tile flush.

### 4.4 Keyboard [O]

* `Kbd_ISR` (1454:00e4, own INT 9) reads port 60h; E0/E1 prefix bytes are acknowledged and
  swallowed, so the BIOS never sees them (the grey arrows arrive as the keypad arrows). A 7-entry scan table DS:0224 (4B 4D 48 50 39 1D 2E = Left, Right, Up, Down, Space,
  Ctrl, C) has make and break handlers (DS:022c / DS:023a) that keep **held bits DS:0218**: 1 Left,
  2 Right, 4 Up, 8 Down, 0x10 Ctrl, 0x20 Space; DS:020c = Ctrl held; C with Ctrl held is swallowed
  (no Ctrl+C / Break).
* A handler's result decides the key's fate: < 0 swallow; > 0 queue and chain to the BIOS; 0
  swallow in **play mode** (DS:0096 = 1) and queue and chain otherwise. Only `State_05_EnterRoom`
  (12e0:0972) sets play mode; the hub (12e0:0193), the sign-in (2caa:1249) and the workshop
  (2dee:1aa5) clear it; the dialog loop (3f56:0682) and the drop-down (40a4:024b) clear it and
  restore it at exit. So the arrows and Space are swallowed in the warehouse rooms only.
* A chained **make** code also writes a 16-byte record `{type 3, u32 tick}` into a 16-entry ring
  (DS:8556, write index DS:1310, read DS:1312); break codes write none. The full-ring test compares
  the wrong register and never fires, so the ring can overrun [O, bug]. `Input_GetCode` pops one
  record per call and takes a BIOS key only with a record; `Kbd_PopQueuedKey` 198d:00d4 drains
  one (the LZH input refill 1b48:002e drains them all: keys typed while a resource decompresses
  are lost; the races call it once per frame). Details: `code_runtime.md` §6.
* Key words come from the BIOS: `Kbd_BiosGetKey` 1d3c:0004 (INT 16h AH=0: ASCII, or 0x100 | scan
  when ASCII is 0), `Kbd_BiosPeekKey` 1d3c:0015.

### 4.5 Mouse and click zones [O]

* The mouse is **required**: `Mouse_DriverPresent` (2c08:0011) checks the INT 33h vector (not 0,
  not an IRET) and INT 33h AX=0.
* `Mouse_EventHandler` (17a0:0004) is an INT 33h fn 0Ch user handler (mask 0x1F) that runs at
  interrupt time: x = CX/2 (driver x 0..639); on a move (only while DS:01e2 = 0, i.e. between a
  flush and the next drawing call: moves during drawing are dropped) it restores under the cursor,
  stores the position and calls `Dirty_Flush`; a left press latches DS:0a10 / 0a1a / 0a1c / 0a1e;
  a left release sets DS:0a20 = 2 with the position and a u32 tick stamp. The latched positions
  are the pointer **plus the current cursor's hot spot** (DS:7a74 / 7a76), and zones are hit there
  [O code and capture]. **The right button is ignored** (it only updates DS:0a0c).
* `Mouse_SetBounds` (17ab:017f, INT 33h fn 7/8) passes the x minimum undoubled, but every caller
  passes x = 0, so it has no effect. `Mouse_DrawCursor` resets the bounds to the full screen (less
  the cursor's size) whenever the active cursor record changes (17ab:06b2) [O].
* **Click zones** (17ab): 22-byte records in linked lists `{shape (0 rect, else circle with radius
  at +6), x, y, w, h, priority, next, press code (+0x10), release code (+0x12), …}`. Two lists: local
  DS:0a32 and global DS:0a36 (menu titles, HUD). The hit test (17ab:0507) takes the highest
  priority. `Zones_Push` / `Zones_Pop` (17ab:04bd / 04e8) save and restore the local list around
  modal screens. Static zone tables live in DGROUP (e.g. DS:0132 hub, DS:21ce workshop, DS:22fe
  post-race).

### 4.6 `Input_GetCode` and input codes [O]

`Input_GetCode` (198d:0106) returns one 16-bit code per call:

| Source | Code |
|---|---|
| left button pressed over a zone | the zone's press code, \| 0x4000 on the first poll after the press, \| 0x8000 on later polls while held |
| left button released | the zone's release code |
| key | the BIOS word (ASCII, or 0x100 \| scan) |
| DS:130e = 1 / 2 (set by menu commands) | 0x17A2 / 0x17A3, only right after the menu hook has run |

A popped key-ring record **replaces** a mouse code computed in the same poll (by the BIOS key, or
by 0 when no BIOS key waits, e.g. after a Shift or Ctrl make), so a key can erase a click
(198d:015a..01a2). The idle bit 0x40 is not an input code: it comes from `Input_GetHeldKeys`
(198d:02b8), which the player actor calls (`code_runtime.md` §6.5).

A 12-key switch (198d:01be, table CS:0275) acts on global keys before the code is returned:
Ctrl+S (0x13) toggles the sound (1cdd:05c8); Esc (0x1B) calls the hook DS:8656; Alt+H / F1 →
menu index 0 (Help), Alt+F / F2 → 1 (File), Alt+O / F3 → 2 (Options): the code becomes 0x13B +
index, the Esc hook is called for these too (198d:01df), and then the menu hook DS:865a opens the
drop-down unless DS:054e is set (only inside `Res_Read`); 6000..6003 go to the HUD inventory
(`Hud_TrashCursorPart`, `Hud_PutBackCursorPart`, `Hud_ScrollLeftArrow`, `Hud_ScrollRightArrow`);
5000..5100 select part n (`Hud_SelectPart` 1a04:090e). The menu titles' zones use the release
codes 0x13B + i, so a click on a title is the same code as its F key.

Zone code blocks in use: 0x13B.. menu titles, 0x44F click outside a drop-down (ignored),
0x44C..0x459 and 0x4B0.. puzzles, 0x5DD / 0x5DE the puzzle Go Back and panel, 0xBB8..0xBBB hub,
0xFD0 + row menu items, 1000..1009 and 4000 + n workshop, 2000..2004 post-race, 5000.. / 6000.. HUD,
0x1B for a dialog's footer line (a click = Esc).

### 4.7 Sound driver (overview) [O]

The song format is in `fmt_sound.md`; the driver and the bank layer are specified in
`code_sound.md`.

* One resident hand-written driver, segment 1bb4: **64 song slots** (far pointers DS:1458, active
  flags DS:1558), stepped by `Snd_Tick` at 72.826 Hz. Per slot and tick: the slot's tick count
  (+3; a callback at +9 fires when it equals +7, used for music-synchronised cues), the delay
  countdown (+1), then the class interpreter (table DS:1aaa).
* Classes: 0 PIT-divisor sweeps on the speaker (1bb4:087e); **1 MIDI-like events, monophonic on the
  speaker** (1bb4:0966); 2 SN76496-style PSG (1bb4:0a1f); **3 OPL2 with rhythm mode** (1bb4:0ad6:
  6 melodic + 5 rhythm voices, 11-byte patches, transpose, bend, master and channel volume);
  4 = class 1 again. The shipped songs use classes 1 and 3 only.
* Dead code in this build: the PSG path (port DS:16ae never written) and an MPU-401 intelligent-mode
  path for the MT-32 (330h/331h, Roland SysEx); `Snd_Init` treats devices 1 and 8 specially, but no
  switch selects 8 [O byte scan].
* `AdLib_Write` (1bb4:00a9) uses the standard 6 and 35 status-read delays; `AdLib_Detect`
  (1bb4:0142) the standard timer test.
* `Snd_Init(dev)` returns capabilities (bit 0 speaker, bit 2 OPL); `Init_CommandLine` stores bit 2
  in DS:a0d8. `Snd_Init` detects an AdLib only for device −1, skips detection for 1 and 8, and for
  any other device (0 from `-SN`, 4 from `-SA` / `-SB` / `-SM`) runs `AdLib_Detect` and then forces
  the OPL flag to 1 (1bb4:125a..1274): `-SN` loads the FM banks and plays them silently, `-SA`
  without a card plays FM to nothing. Sound banks come in pairs: id = speaker arrangement, id + 1 =
  FM arrangement; the bank loader (1cdd:0024) picks by DS:a0d8. Device 0 silences the output
  (1bb4:0b97, DS:1680 = 0): songs keep running, patches and volumes are still written, but nothing
  is keyed.
* Ctrl+S (1cdd:05c8 → 1cdd:0592) toggles `g_soundMuted` DS:1ad4 and the player's DS:45cc, then
  plays (or stops) effect 0 of the global bank 0x105B / 0x105C with priority 7Fh, looping: a
  silence song (65 278 ticks of nothing) that takes every channel by priority while the other
  songs keep running silently. Dialogs and drop-downs pause the driver by locking it (busy = 1)
  after silencing every voice (1cdd:05db / 05e5; not nestable). Music `M` members are lists of
  channels to **mute** for an arrangement (`SndRes_SelectArrangement` 1cdd:0423; arrangement 0 =
  every channel). One BMF delay unit = one `Snd_Tick` = 2 PIT ticks = 13.73 ms (the round 2 `.dro`
  logs fall on a 13.731-ms grid).

### 4.8 Files, memory and CPU [O]

* Interrupts: INT 10h ×3, 16h ×2, 1Ah (C0), 21h ×57, 33h ×15, 3Fh (overlays). No INT 24h handler
  (DOS's own Abort/Retry/Fail would appear [I]), no INT 15h, 67h (EMS) or 2Fh (XMS), no joystick.
* Files through the Borland RTL, bare names in the current directory: the four archives opened
  read-only once and kept open (14ff:0167); `players.dat` (ovl02); `gizmo004.dat` created with
  O_RDWR | O_CREAT | O_TRUNC | O_BINARY by `Screen_SaveToSwap` (14ff:0226) and read back by 14ff:0284
  **with no error checks** — it is a disk save-under for a 280×163 area, not game data.
* Memory: conventional far heap only (`Mem_Alloc` 1536:0007, exit(1) on failure; `farheapcheck`
  exit(2/3)). Big blocks: 128 000 (screen + back page) and 65 535 (the shared resource and
  decompression buffer DS:6574). Start-up needs 363 500 bytes free.
* CPU: `Cpu_Detect` (2c08:0032): FLAGS bits 12-15 → 8086 = 1 / 286 = 2; EFLAGS.AC → 386 = 3 /
  486 = 4. The result changes the chimp set-up (§2.6).

---

## 5. The TLC UI library

The menu, dialog, button, text, font and DAM modules form a library that G&G links whole: an
unreferenced File-menu item list from another title (DS:3f2b), 11 DAM functions with no caller and
TM Windows' menu data laid out for a self-drawn bar all show it [O, I conclusion]. TM Windows was
built on the same library (§9).

### 5.1 Fonts (ovl01 2b16) [O]

Load 2b16:0000 (both fonts by 2b16:021c), select 2b16:0256 (returns the previous font), colour
0243, draw string 029c, draw char 0368, char width 03ea (space = 3), height 0418, current 0426.
Two `fnt` fonts: id 9, the UI font with key-cap glyphs in spare codes (Spacebar, Ctrl, Enter,
arrows, Ctrl-R, Esc), and id 10, a small font. Line height: 10 in `Text_Render` (`Font_GetHeight` =
height + 1, 2b16:0418) and 11 for CR/LF in `Font_DrawString` (height + 2, 2b16:02d4).
`Font_CharWidth` returns the spacing for a code without a glyph while `Font_DrawChar` returns 0.
The colour is stored **per font** (record +0x0A, `Font_SetColor` 2b16:0243, `Font_Select`
2b16:0256), so selecting the other font switches the colour too. Glyph format: `fmt_graphics.md`.
Glyphs are drawn by the mono blitter 1399:0541 in colour DS:1f6c. Details: `code_ui.md` §3.

### 5.2 Text renderer (ovl03 2b89) [O]

`Text_Render` (2b89:007a) measures and draws in one routine (wrappers `Text_DrawBox` 2b89:0000,
`Text_Measure` 002a, `Text_Width` 0056 with a 320-px maximum, `Text_MeasureRun` 03ca, `Text_DrawChar`
0466).

* Character table CS:03ba: 0x0A and 0x0D break the line, 0x20 is the wrap point, 0xFF starts an
  escape; everything else is measured or drawn.
* Escapes (table CS:03ae): `FF 01 n` substitute string n−1, then skip up to and including the
  next FF; `FF 02` underline toggle, `FF 03 c` colour c−1 (also while only measuring, and not
  restored at the end), `FF 05` centring toggle (re-centres the line with a signed halving), `FF 06
  … FF` comment — each of these consumes up to and including the next FF; `FF 04` consumes only
  its 2 bytes and does nothing; k = 0 or k > 6 are ignored (2 bytes).
* `Text_MeasureRun` (hand-written assembly) sums **raw width + 1** per character, not the drawing
  advance (width + spacing): with font 9 a word measures one pixel per character narrower than it
  draws (`code_ui.md` §4.3).
* After a wrap at a space it skips following 0x20 **and 0x09** bytes, although G&G has no TAB case
  (a leftover of the TM Windows code).

Same code family as TM Windows' `Text_Render` (10b0:00b5), which also has TAB, `FE` and `FF 04` tab
stops (§9).

### 5.3 DAM animation engine (ovl23 3d39) [O]

* Scripts: 12 `DAM` resources (0x114B..0x1156) with **16-bit opcodes 0x7FF8..0x7FFF** (format:
  `fmt_graphics.md`). Frames come from a bundle loaded beside the script.
* Per-tick interpreter 3d39:058f (opcode table CS:083a; switch read from the assembly): 0x7FF8
  indexed callback (u16 index, u32 argument → far table DS:ba1e), 0x7FF9 update rectangle, 0x7FFA
  loop, 0x7FFB / 0x7FFC operands skipped (**no sound**), 0x7FFD queue a cel (dx, dy, frame, flag),
  0x7FFE end of tick, 0x7FFF end (callback, clear script); the stop flag forces 0x7FFF.
* **2 channels** of 0x22 bytes at DS:ba66; a channel is free when its script pointer (+0x1A,
  DS:ba80 for channel 0) is NULL (3d39:03d3..03da). Live functions: start 3d39:03cb, is-running
  0468, the interpreter 058f, draw 084a.
* Only user: the launch puzzle (ovl17), through 3797:0103 and the modal player 3797:100e (frame
  table built from the bundle's offset table, frame timer slot 7). 11 of the 15 DAM functions have
  no caller: 3d39:0000, 011c (reset all), 013a (a skip-to-tick routine, not the interpreter), 0323,
  037e, 04ae (stop), 04f9 (set pause), 0553 (erase), 0983 (flush update rectangles), and 0049 / 00ba,
  which only those call.

### 5.4 Dialogs and buttons (ovl27 3f56, ovl28 4034, ovl32 4106) [O]

* A dialog descriptor (0x32 bytes, `code_ui.md` §5.1) is passed **by value** (Ghidra loses these
  arguments: read the assembly); at most 3 buttons (records DS:bbbc..bc10). Message boxes from
  resources are bundles `{S title, S text, u16}` (4106:0000 / 006d): for Yes/No boxes member 2 is
  the default button; for `Dlg_MessageRes` (3f56:0d2a) it selects the footer (0 → DS:3e38 "Esc to
  Go Back", 1 → DS:3e45 "Enter to Go On", any other value → an uninitialised pointer, which the
  disk-full box 0x106F hits).
* `Dlg_Layout` 3f56:0000: size from the text width, wrap width 280, centred when x/y = −1, button
  row left / centre / right. Save-under 3f56:033e / 0367.
* `Dlg_Draw` 3f56:0389: save pen, layout, save-under, colour 15 fill, two colour-0 frames, colour 7
  2-px drop shadow (vertical, then horizontal), title and separator, body through `Text_DrawBox`,
  footer separator and text, buttons and focus, present, restore pen.
* Zones 3f56:05d7 / 061f; the footer line is a zone with release code 0x1B (DS:3dd4, 3f56:05f8):
  a click on it is Esc. **Modal loop** 3f56:0655: `Zones_Push`, sound paused, menu disabled, tick
  frozen (§3.5); Enter = default button, Esc, Left/Right move the focus, letter keys pick a button.
  Yes/No 3f56:0bb6 (keys `y` / `n`), messages 3f56:0b1c / 0d2a, With/Without Chimps 3f56:0c5e
  (resource 0x1067; Esc disabled, flags 2, 3f56:0cee), pager 3f56:0907.
* Buttons (4034): draw 0000, focus 016b, flash 01d6 (4 toggles with `Delay(4)`), zones 02e4 / 02f4.
* Menu commands (4106): Save & Exit 00b2, Start New Player 00d7, Return to Technology Center 014f,
  Sound & Music 011e, New Player Hints 00f8.

### 5.5 Menu bar (ovl31 40a4) [O]

Data: `MenuBarDef` DS:4053 = `{u16 3, far DS:4017}`; three 0x14-byte `MenuPopupDef` records:

| Off | Field | Help DS:4017 | File DS:402b | Options DS:403f |
|---|---|---|---|---|
| +0x00 | far title | | | |
| +0x04 | title width | 54 | 54 | 54 |
| +0x06 | item count | 6 | 3 | 2 |
| +0x08 | far items, LF-separated | | | |
| +0x0C | far handler table | | | |
| +0x10 | drop-down width | 164 | 220 | 145 |
| +0x12 | title x | 108 | 0 | 54 |

[O bytes at file 0x13C87.., checked in this pass.] On screen: File | Options | Help at x 0, 54,
108.

* Install `40a4:0000(bar)`: copies each record (F_SCOPY) and adds a global zone per title at
  (x = +0x12, y 0, w = +0x04 + 2, h 9) with release code 0x13B + i and priority 8; sets the menu
  hook DS:865a to the drop-down 40a4:020b. Install with NULL removes the zones and clears the 9-px
  strip.
* Bar drawing 40a4:04db: a 9-px strip with title pictures from bundle resource 0x0001 at x = +0x12,
  normal or selected.
* Drop-down 40a4:020b: a dialog box (40a4:056c → `Dlg_Draw` 3f56:0389) at x = +0x12, y 9, width
  +0x10, height items·10 − 10; items from +0x08, rows of 10 px, item zones 0xFD0 + row, a
  click-outside zone (0, 12, 320, 188) with code **0x44F**, which the loop **ignores**: a click
  outside does not close the menu (40a4:0358); the footer line is a zone with code 0x1B (Esc);
  keys Enter, Esc, F1..F10, arrows; sound paused; time not frozen (§3.5). A handler that returns
  non-zero reopens the same menu (after the Help pager's Done, after Esc in Save & Exit;
  40a4:03c5).
* Items: File = Start New Player, Return to Technology Center, Save & Exit; Options = Sound & Music,
  New Player Hints; Help = 6 topics (411d:01e0..021c, resources 0x105D..0x1062).

The record layout is identical to TM Windows' (DS:13bf / 1383); TM Windows maps it onto a Win16
menu and never reads +0x04 / +0x10 / +0x12, but its values fit the same layout reading
(`re/docs/dgroup_map.md` §4.12) [I].

### 5.6 Scroll bar, help pager, text viewer [O]

* **Scroll bar** (ovl30 406a): a vertical scroll bar widget (record DS:20dc `g_rosterScrollBar`,
  `ScrollBar_Draw` 406a:003f, `ScrollBar_HandleClick` 406a:01b6), used by the sign-in. The roster's
  8 rows of 11 px are drawn by the sign-in itself (`SignIn_DrawList` 2caa:053a).
* **Help** (ovl33 411d): 411d:0000 builds pages from a bundle and shows them with the dialog pager
  3f56:0907; buttons "See Next Page" / "Go Back a Page" / "Done" (DS:3dfe..3e0e; PgDn / PgUp).
  Each page is its own `Dlg_RunModal`, so time is frozen while the pager is open.
* **Text viewer** (ovl34 4140:0667): scrolling text with a thumb (x 0x76, y 0x2D..0x8A), wrap width
  0x73, its own word wrap and auto-repeat; used by the puzzles for their instructions. It does not
  freeze time (timer slot 6 only) and leaves `Mouse_ForceDefault(0)` behind (`code_ui.md` §8).

---

## 6. Players, sign-in and saves

### 6.1 Sign-in (state 2, ovl07 2caa) [O]

* Roster from `PLAYERS.DAT`: up to **99 players**, 8 visible rows of 11 px drawn by
  `SignIn_DrawList` (2caa:053a) with a scroll bar (DS:20dc); picture buttons Cancel DS:20f0, New
  Player DS:2110, Start Game DS:2130. Entries are kept in name order (`Roster_UpdateTyped`
  2caa:01c5 inserts the typed name in order) [O].
* Typing selects the first entry the typed text is a prefix of, unless the entry came from New
  Player, where it builds a new name. Name rules (2caa:0cc6): 1..15 characters, the first an A..Z
  letter (lower case is upper-cased), then any byte 0x20..0x7F, narrower than 60 px in the small
  font; trailing spaces are dropped; a duplicate → dialog 0x1068. Ctrl+R removes a player (dialog
  0x106A; the index is saved at once, the record stays in its slot); a full list → 0x1069.
* A new player is asked "With/Without Chimps" (3f56:0c5e, resource 0x1067, Esc disabled) →
  DS:4578. The file is created full size on the first run (`Roster_LoadOrCreate` 2caa:004f) and
  the new player saved (2caa:1240).
* The Esc handler 2caa:0fb5 runs `Cmd_SaveAndExit` with `Players_SaveCurrent` disabled through
  `g_saveDisabled` DS:1f7a (2b5a:01f0): a Yes leaves the program without writing anything (no
  player is loaded). Details: `code_ui.md` §11.

### 6.2 `PLAYERS.DAT` (overview) [O]

`USR` 0x1A, u16 version 12 (checked on open, 2b5a:004c; a mismatch re-creates the file), u16
player count, a roster area of 100 × 22 bytes from offset 8 (entries are copies of each record's
first 22 bytes with a record index), then **99 pre-allocated records of 5821 (0x16BD) bytes** at
0x8A0 + n·0x16BD:
578 487 bytes in all. A record holds three 1722-byte (0x6BA) blocks, one per building, for a visit
in progress. Layout and fields: `fmt_data.md` §8, `code_ui.md` §12. Index entries count..98 hold
the free slots in the order they will be taken; entry 99 is written only at creation, with
uninitialised heap bytes (2caa:00f9). The creation loop saves the 99 blank records through
`Players_SaveCurrent`, which also writes index entry `g_rosterIndex` = 0 each time, so entry 0
ends as slot 98 and the first new player gets slot 98 (the "slot 98 anomaly" of wave 0, explained)
[O 2caa:010d..011a, 2b5a:02bc..02e5].

The memory image of the current record is DS:4562..5c1e. Known variables (offsets in the record =
DS − 0x4562):

| DS | Meaning |
|---|---|
| 4572 | score, u32 (record +0x10) |
| 4576 | record slot |
| 4578 | chimps on (0 = Without Chimps) |
| 457c..458f | the 20-byte live building block: 457c blueprint seen (`g_blueprintSeen`), 457e bananas, 4580 the tube's hall (`g_tubeRoom`), 4582 parts needed, 4584 required parts installed, 4586 completion bonus given, 4588 paint colour, 458a / 458c hint cancel flags, 458e set-up coin |
| 4596 / 4598 / 459a | races won per building, −1..4 |
| 459c[3] | building in progress |
| 45a2 | hints off |
| 45a4[19] | hint seen, per hint 0x1043..0x1055 (set even when hints are off) |
| 45cc | sound off (a new player's is always 0, `code_ui.md` §7) |
| 45d0 + kind·0x42 + variant·2 | puzzle solved |
| 47e0 + b·0x6BA | saved building blocks |
| 5c0e | races won in all (15 = the end) |
| 5c10[15] | vehicle colour per race (record +0x16AE; races and hub trophies) |

### 6.3 Saving and resuming [O]

`2b5a:01ea` saves: if a building is in progress (DS:0094) it copies the live state (20 bytes at
DS:457c, objects 153f:0385, rooms 1622:0fb3, actors 1834:0b5e) into the building's block and sets
DS:459c[b]; then it writes **only this player's record** (0x8A0 + DS:4576·0x16BD) and roster entry
(8 + DS:a079·22). A failed write (2b5a:010a) shows resource 0x106F, deletes `players.dat` and exits.

Called by: Save & Exit (then `exit(0)`), state 8 before a race, state 16, the post-race menu, the
final movie and a new player. State 4 restores a building in progress without a prompt. Block
contents: +0x000 the 20 live bytes, +0x014 per part {holder, flags}, +0x26C one byte (7 or 9) per
door object, +0x302 per room {n, n × {x, floor, kind}} item lists, +0x492 12 × 0x2E bytes of actor
state (`code_world.md` §4.2).

### 6.4 Score and ranks [O]

Score: +20 per new part (not for a recycled one), +400 per solved puzzle, +100 the first time a
vehicle is complete, race rewards counted up on the result screen. Rank = number of thresholds
≤ score (2bd4:0225) over the u32 table DS:1f9c = 5000, 30000, 60000, 120000, 200000, 300000,
750000 [checked in this pass] → 8 rank names (DS:1f7c, Trainee .. Head Scientist). The rank-up
message (2fcb:01dd, bundle 0x1070) names the next threshold. Numbers are formatted with thousands
commas by 2bd4:024d.

---

## 7. New Player Hints (ovl12 3060) [O]

* Hint texts are `H` resources from 0x1043 (`FF 06 <designer note> FF <text>`; hints 0x1043..0x1055,
  19 flags DS:45a4).
* `Hint_Show` (3060:013b): unless hints are off (DS:45a2) or the hint was already seen
  (DS:45a4[id − 0x1043]), it shows the text in a message box (3f56:0b1c, titled "New Player Hint",
  "Press Enter to continue") with the chime 0x1056 / 0x1057; it marks the hint seen **even when
  hints are off** (3060:0151 → 01ed); the flag is saved with the player. The dialog pauses the
  driver right after the chime starts, so the chime is heard for an instant at most [I].
* `Hint_Queue(id, delay, cancel*)` (3060:0037): the delay is a **u16** tick count (3060:0081 adds
  one word); 5 slots `g_hintSlots` DS:a0de (18 bytes each), kept sorted by due tick (3060:0000);
  with no free slot a sixth record is written past the queue, over DS:a138. `Hint_Update`
  (3060:00bc) runs once per frame of the warehouse and the workshop loops only and shows one due
  hint (due < tick, strict) unless its cancel flag became non-zero. Examples: 0x1046 after 8 s
  unless DS:458a; 0x1048 after 60 s unless DS:458c.
* Triggers: the full table is `code_ui.md` §13.2 (blueprint, pick-ups, the workshop, warehouse
  entry, the trash can, the tube, a chimp hit, a lost race, parts complete, the last banana, every
  puzzle, the completion bonus, the race door). 0x104C and 0x104E are never triggered.
* No helper character and no voice.

---

## 8. Randomness [O]

* One seed: `srand(time(NULL))` at 303a:0229, right after `Input_InstallHandlers`; the low word of
  `time()` is kept at DS:2454. The RTL seed is the u32 at DS:442a (`srand` sets the low word and
  clears the high word; `rand` 1000:06c1 = Borland LCG `seed·0x015A4E35 + 1`, returns bits
  16..30).
* All **18** `rand()` call sites (`CALLF 1000:06c1`) scale as `rand()·n / 0x8000` (Borland C++
  3.x `random(n)`): `Rand_Shuffle` 2bd4:0043 / 0064 (decks and item grids), the set-up coin
  12e0:0910, `Chimps_Spawn` 2a80:0875 / 08a8 / 08ee, `Chimp_Brain` 2a80:0212,
  `Item_PlaceInRandomRoom` 1622:0f5b, `Player_HitByChimp` 18f5:01ac, the wipe pattern 1473:0047 and
  sound 1473:0513, the screen-animation ops −34/−35 174b:00f1 / 0116 (no shipped set uses them),
  the puzzle choice 38e2:01ed / 022d / 0052, the success jingle 38e2:02a3 and the machine-pieces
  box 3a4a:07a4. The UI, the races, the workshop and the sound driver call none. The order of the
  calls within a frame, which seeded replays depend on, is in `PORTING_NOTES.md` §4.1.

---

## 9. Relationship to Treasure Mountain

G&G is a different game on a sibling engine. The verifier's grades win where the analysts claimed
more; the evidence is in `SCOPING.md` §4. Grades: **identical** (same logic or data layout),
**same lineage** (the same routine or design, evolved), **similar idea** (same role, new code),
**different**.

| Subsystem | vs TM Windows | vs TM DOS | Short evidence |
|---|---|---|---|
| Toolchain and C runtime | same lineage (toolchain only) | same lineage (toolchain only) | three Borland compilers; 65 of 133 runtime functions match a TM runtime function, 17 exactly. Not engine overlap |
| Program format | different | different | MZ with VROOMM vs NE / EXEPACK |
| Top-level flow | different | similar idea | one table-driven state machine; TM Windows uses nested loops with setjmp/longjmp, TM DOS a phase table for its title |
| **Runtime model, frame pacing** | **same lineage** | different | nested blocking loops on deadline timers, as TM Windows; TM DOS runs its logic in the interrupt. The overlap that matters most for porting |
| Deadline timers (152c) | same lineage, **not identical** | similar idea | same four functions and slot model as TM Windows 10a8; `Timer_Expired` is strict here, `≥` there; unit PIT ticks, not ms |
| PIT and timer ISR | n/a | similar idea | install sequence byte-identical to TM DOS apart from the divisor; ISR new |
| Keyboard ISR | different | similar idea | common DOS design (held bits, rest to the BIOS); code different |
| Input codes, mouse, zones | similar idea | similar idea | 0x4000/0x8000 mean first poll / held (left button only), not TM Windows' left / right buttons; zones are linked lists with priorities, circles, push/pop |
| Archive and compression | different | different | only the three-call load API idea is shared with TM Windows |
| Bundles | similar idea | similar idea | members inline with u16 offsets |
| Pictures | similar idea | different | 8-bit `LPX` with skip/literal RLE; no RUND |
| Palettes and fades | different | different | DAC ranges, retrace-paced fades; TM Windows' fades are stubs, TM DOS has none |
| Frame buffer | similar idea | different | stacked back page (y+200), as TM Windows' (y+384) design; no shared code |
| Presentation | similar idea | similar idea | 8×8 dirty tiles with the cursor composited; TM Windows a rect list, TM DOS dirty spans |
| Fonts | similar idea | similar idea | generic font API, new glyph format |
| **Text renderer** | **same lineage** | different | same escape meanings, same signed centring, same TAB-skip leftover, same function order; wrappers evolved |
| Text markup | same lineage | different | `FF 06 … FF` comments |
| **DAM animation** | **same lineage** | different | same magic and opcode numbering widened to words; 2 channels; live part ~1.5 KB, one puzzle; FB/FC silent; 0x7FF8 new |
| Final movie | different | different | ANM-style deltas straight to VRAM |
| Sound driver and music | different | similar idea | new 64-slot BMF driver; only the standard AdLib write and detect match TM DOS |
| **Menu bar** | **identical data format**, drawing new | similar idea | same 0x14-byte record in all six fields; TM Windows' unread fields are this layout data (§5.5) |
| Dialogs and buttons | same lineage (design) | similar idea | same module order and draw recipe as TM Windows 10c0; rewritten (descriptor by value, N buttons, 2 frames) |
| Help pager | similar idea | similar idea | house button vocabulary in all three EXEs; no shared pager code |
| Sign-in and roster | similar idea | similar idea | Ctrl+R, capitalised first letter, duplicate and full-list checks are a series convention |
| Ranks and score | similar idea | similar idea | generic threshold lookup; the thousands-comma formatter is a real fingerprint |
| Random numbers | not engine overlap | – | `rand()·n/0x8000` is the Borland C++ 3.x/4.x `random()` macro |
| Hints | similar idea | similar idea | New Player Hints concept and name; 5-entry delayed queue, no character or voice |
| Save file | similar idea | similar idea | only the `USR` 0x1A magic is shared with TM Windows |
| World and rooms | similar idea | different | fixed rooms like TM Windows, but data per building and race, 4 layers |
| Actors and chimps | similar idea | similar idea | 12 slots with callbacks, data-driven behaviour; new code |
| Puzzles | similar idea | similar idea | "unsolved first, per-player history" resembles TM's question deal; the 8 mini-games are new |
| Workshop, vehicles, races | different | different | nothing comparable in TM |
| Title and attract | similar idea | similar idea | 30-s pages, Esc quits, loop restarts: behaviour like TM DOS, no shared code |
| Options, Ctrl+S | similar idea | similar idea | series convention; Ctrl+S inside the input fetch is a weak TM Windows signal |
| Start-up and switches | n/a | similar idea | same `-S<letter>` convention, rewritten parser |

**Shared live code** is about **2.5-3 KB, around 2 % of the game code**: the text renderer (~1.2 KB,
used by every screen), the live DAM part (~1.5 KB, one puzzle), the timers (~0.16 KB), the comma
formatter and the button flash. Another ~10 % of the engine (by function count) shares a design or
data layout with new code: the menu format, the dialog recipe, the timer-paced loops, the FF markup,
the `USR` magic and the stacked back page. Nearly all of the lineage runs to TM Windows; the TM DOS
links are conventions (attract timing, Ctrl+S, sign-in keys) and DOS idioms (PIT and AdLib code).

---

## 10. Open questions

Wave 1 answered the wave 0 questions:

1. **Movie palette** — answered: none is loaded; the movie draws over the results screen with
   that screen's palette (round 2 capture, §2.10).
2. **286 quirk** — answered: the first chimp goes to room 0, which has no walls, doors or floor
   references, so it never leaves and is never drawn: one chimp fewer on a 286 (§2.6,
   `code_world.md` §13.1).
3. **Race model** — answered: rewards `I` 0x1071[races won − 1] × 1000; DS:4580 is the tube's
   hall, not a race parameter; performance once per race from the parts; no steering and no
   `rand()` (§2.9, `code_vehicles_races.md` §8..§11).
4. **Chimps and the projectile** — answered: a banana stuns a chimp, which drops its part; chimps
   take a random part from the player on contact (§2.6).
5. **Puzzle kinds 3, 4, 7** — confirmed, and every puzzle's data is specified
   (`code_puzzles_a.md`, `code_puzzles_b.md`).
6. **Unexplained hooks** — answered: hub zone 0xBB8 is the sign-in booth (§2.4); the sign-in Esc
   handler runs Save & Exit with saving disabled (§6.1); the words after the workshop table's NULL
   are shelf zone records (§2.7).
7. **Play mode** — answered: §4.4.
8. **Overlay loads** — no stall is visible in DOSBox-X, whose file reads take no emulated time
   (REFERENCE_CAPTURES §10.4); the engine need not model them against these references.
9. **Time in other modal screens** — answered: the help pager freezes time (each page is a dialog);
   the drop-down and the text viewer do not; the puzzle screens are ordinary unpaced loops (§3.5).
10. **BIOS chain counter.** DS:0216 behaviour is certain (chained every tick); whether −8 was the
    intended reload is not, and does not matter for a port.
11. **Format details** — answered: the per-building save blocks (§6.3, `code_world.md` §4.2), the
    BMF `M` lists (channels to mute, §4.7) and the wipe pattern format (`code_runtime.md` §14.2).
12. **Roster order** — answered: name order (§6.1, `code_ui.md` §11.6).

The open points that remain are listed at the end of each topic spec (`code_*.md`, "Open
questions") and in REFERENCE_CAPTURES §13; the ones that block an implementation are summarised in
`PORTING_NOTES.md` (open questions).
