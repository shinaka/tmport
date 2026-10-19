# Treasure Mountain! (DOS, 1990, SST.EXE): architecture and game rules

This document maps the code of the original DOS edition (the 16-colour EGA/Tandy/CGA/MCGA game
shipped as `SST.EXE` plus data files) and compares its gameplay with the Windows CD edition
described in `re/docs/ARCHITECTURE.md`. The data formats are in the companion docs
`fmt_graphics.md` (PAN/PAK pictures, palettes, video modes, font, the block grid),
`fmt_sound.md` (TUNES/SFX and the sound driver) and `fmt_data.md` (ALL_PUZS, PRG_DICT, HELP.DAT,
USERS.DAT, the DGROUP map); this document covers the program and the game rules. It is the
overview: the wave 1 topic docs specify every routine and win where they are more detailed —
`code_world.md` (cells, grid, camera), `code_player.md` (player, animation scripts, search, end
sequence, treasure room), `code_elves_layout_hints.md`, `code_ui.md` (menus, boxes, help, title,
sign-in, quiz, HUD), `code_runtime.md` (timer, main loop, start-up, input) and `PORTING_NOTES.md`.

Conventions (same as the Windows docs):

* **[O]** = observed in code, bytes or decoded data. **[I]** = inferred. Every rule cites the
  function that implements it.
* Code addresses are Ghidra `SSSS:OOOO` in the project `re/ghidra_proj/TMDos` (program
  `SST_unpacked.EXE`, the EXEPACK-free image; load segment 0x1000). File offset of `SSSS:OOOO` in
  `extracted/SST_unpacked.EXE` = `(SSSS − 0x1000)·16 + OOOO + 0x21F0`.
* `DS:XXXX` = DGROUP (segment `4d94`), file offset `0x3D940 + XXXX + 0x21F0 = 0x3FB30 + XXXX`.
  DS:0000–8F9F is initialised, DS:8FA0–CC1F is BSS (zero at start); the stack segment follows.
* Names follow `re/names/dos/*.tsv` (one canonical name per address, decision table in
  `re/names/dos/README.md`; the export `re/dos/ghidra_export/` carries them, and
  `src/dos/data/dgroup_offsets.h` uses the same names for the DGROUP tables). Numbers are hex where
  the code uses hex.
* "World units" are pixels of the 320×200 logical screen; x positions inside the play-field are
  often kept in *bytes* (2 pixels per byte, 4 bpp back buffer).
* Game text is referred to by DGROUP offset with at most a few identifying words.

---

## 1. The executable at a glance

`SST.EXE` (115 011 bytes) is a Turbo C 2.0 (1988) **large-model** DOS program packed with
EXEPACK; the unpacked image is 288 992 bytes plus a 0x21F0-byte header with 2163 relocations
[O MZ header]. Entry `1000:0000` is the C0L start-up, `main` is `130f:0007`. The game is plain C
(no C++), working on DGROUP globals; low-level graphics, timer and keyboard code is hand-written
assembly in segment `1000` and `43a0`, the AdLib driver is in `4ac9`.

Data files (all in `TM-DOS\`, opened by index through the table `DS:0476`, 22-byte records:
name, disk number) [O `File_OpenByIndex` 1460:06c8]:

| # | File | Disk | Loaded by | Use |
|---|---|---|---|---|
| 0/1/2 | MOUNTAIN.PAN, MOUNT2N3.PAN, MOUNT3.PAN | 2 | `Scene_LoadPan` 1460:05e8 | tile map + tiles of scenes 0/1/2 (mountain levels) |
| 3, 4 | CLOSE.PAN, SHACK.PAN | 1 | `Scene_LoadPan` via `Scene_Update` (file = scene) | scene 3 (castle top close-up), scene 4 (treasure room) |
| 5 | HELP.DAT | 1 | `Help_LoadFile` 43cb:107a | help pages (0xED8 bytes into DS:9325) |
| 6 | SPRITES.PAK | 2 | `Scene_Update` 1437:0034 | sprite bank for scenes 0–2 |
| 7 | ALL_PUZS | 2 | `Quiz_LoadQuestions` 46ce:09ab | 360 elf questions (0x2D0-byte index + texts) |
| 8 | PRG_DICT | 1 | `Quiz_LoadDictionary` 46ce:095f | word dictionary for the question texts |
| 9 | USERS.DAT | 1 | `Users_ReadFile`/`Users_WriteFile` 47c7:0514/055a | players and saved games |
| 10/12 | SFX.IBM / SFX.TAN | 1 | `Sound_LoadData` 1460:022a | sound effects (TAN for Tandy and PS/1) |
| 11/13 | TUNES.IBM / TUNES.TAN | 1 | `Sound_LoadData` | music (IBM for the speaker, TAN for Tandy, AdLib, PS/1) |
| 14 | OPENING.PAN | 1 | `Title_Run` 4ba4:0434, `SignIn_Update` 47c7:0007 | title / sign-in backdrop |
| 15 | BEGIN.PAK | 1 | `Sprites_LoadBegin` 1460:02c6 | sprite bank for title, sign-in, menus |
| 16/17 | CLOSE.PAK, SHACK.PAK | 1 | `Scene_Update` (file = scene + 13) | sprite banks of scenes 3/4 |

`INSTALL.BAT` copies the disks; the program builds its two data paths from `A:\TLC\sst\` or from
the directory of `argv[0]` (`Path_InitDrives` 1460:08cc) and prompts for disk 1/2 on floppy
drives (`Disk_PromptForDisk` 150a:00f5, message boxes 0x10/0x11/0x15). `TLCINFO.DAT` (DS:0898) is
the disk check file read by `Disk_CheckInfoFile` 150a:0002 [I]. `ssrnet.dat`/`ssrquiet.dat`
drive a school-network refusal (the network message at DS:070b) that can never fire:
`Net_IsNetworkDrive` 1460:05a3 always returns 0 [O].

### 1.1 Module map and size

Turbo C large model gives each source module its own code segment; Ghidra's segments follow the
relocation targets, so one segment ≈ one module (the RTL is split into many tiny segments). Six
"code" segments of the loader are zero-filled far arrays; this pass marked them as data
(`FARDATA_*`, §1.2).

| Seg | Module (proposed) | Role | Funcs | Bytes | Decompiled lines |
|---|---|---|---|---|---|
| 1000 | `c0.asm` + `gfx.asm` + RTL helpers | start-up, PIT timer ISR, hardware detection, video modes, dirty-span present, tile/sprite blitters, scrolling, font, map/tile unpackers; long arithmetic | 69 | 7 412 | 3 635 |
| 130f | `main.c` | main loop, 47 Hz game tick, HUD, scrolling, mouse poll, rank/chest display | 14 | 4 725 | 649 |
| 1437 | `scene.c` | palette, scene loading | 2 | 657 | 100 |
| 1460 | `init.c` | `Game_Init`, files, paths, command line, mouse set-up | 21 | 2 641 | 523 |
| 150a, 153e | `disk.c` | floppy disk checks, critical-error boxes | 9 | 1 499 | 297 |
| 1568 | `cells.c` | terrain handlers per cell type (gaps, ladders, tunnels, gates, links, net shop, castle) | 70 | 8 213 | 1 210 |
| 176a | `cells2.c` | path links, level-2 gate, end screens, treasure room, castle creature | 35 | 5 502 | 1 144 |
| 18cf | `player.c` | player state machine, movement, search start, end sequence | 69 | 6 208 | 1 261 |
| 1a54 | `layout.c` | new-game layout (hiding places, key, treasures), castle layout | 11 | 2 328 | 361 |
| 4306 | `anim.c` | 13 sprite slots, animation scripts, z-sorted draw/erase | 11 | 2 432 | 331 |
| 43a0 | `kbd.asm` | INT 9 keyboard ISR | 4 | 551 | 177 |
| 43cb | `ui.c` | menu bar, message boxes, buttons, text, help pager | 30 | 5 465 | 857 |
| 4521 | `elves.c` | elf spawn, AI state machine, net catch, elf dust, coin elf | 53 | 6 859 | 1 096 |
| 46ce | `quiz.c` | elf questions, clue words, dictionary | 22 | 3 975 | 664 |
| 47c7 | `signin.c` | sign-in, USERS.DAT, game start/restore/end, save packing | 39 | 5 423 | 934 |
| 491b | `sound.c` | tune/effect sequencer for speaker, Tandy, AdLib, PS/1 | 27 | 2 165 | 684 |
| 49a6 | `hints.c` | New Player Hints (Flutter), start/end-of-game messages | 6 | 1 487 | 283 |
| 4a03 | `search.c` | hiding-place search, showcase treasure, treasure shelves | 4 | 2 082 | 278 |
| 4a85 | `input.c` | key tables, mouse zones, mouse walking | 6 | 1 068 | 204 |
| 4ac9 | `adlib.c` | AdLib (OPL2) driver and detection | 34 | 3 481 | 573 |
| 4ba4 | `title.c` (+ RTL `open`) | title/story attract, Super Solvers page | 15 | 1 802 | 307 |
| 4c15–4d90 | Turbo C RTL | stdio/io, string, far heap, time, int86, rand | 77 | 6 096 | 1 679 |
| | **total** | | **628** | **82 071** | **17 247** |

Game logic proper (everything except RTL, `1000`, sound, AdLib, disk) is about 400 functions /
61 KB / 10 000 decompiled lines.

### 1.2 What this pass changed in the Ghidra project

The first export had 353 functions (60 KB of ~90 KB of code). This pass [O, scripts in
`extracted/scratch/dos_code/scripts/`]:

* added 275 functions: 254 at every far-call target and every code pointer stored in DGROUP (found
  through the MZ relocation table: the cell, player, elf, menu and sign-in dispatch tables), the
  rest at call targets, `push bp; mov bp,sp` prologues after function ends and the hand-found
  routines below;
* repaired 8 Turbo C switch tables Ghidra had missed (dense `jmp cs:[bx+T]` and sparse value/target
  tables: 1460:04be, 176a:0650, 1a54:089e, 43cb:09da, 4a85:03f4, 4ac9:08ee, 43a0:00d8, 43a0:019b),
  the near call table DS:8d83 (title phases) and removed a bogus function in zero data;
* added the interrupt handlers and register-argument asm routines (`Timer_ISR` 1000:0428,
  `Kbd_ISR` 43a0:0071, `_harderr_isr`, `_ctrlbrk_isr`, present routines);
* marked DGROUP (`4d94`) and the six zero-filled far arrays non-executable and renamed the blocks
  (`DGROUP`, `DGROUP_BSS`, `FARDATA_1ae7/2005/2897/32d7/415f/41ca`);
* applied `re/names/dos/` (628 functions, 114 DGROUP variables).

Wave 1 repaired the six cell handlers that did not decompile (far pointers built on the stack:
1568:1090, 1556, 15bc, 1c7b, 1d96, 1e50) by clearing the DS assumption over their bodies
(`code_world.md` §12.3); since then all 628 functions decompile. Wave 2 merged the five wave 1
name proposals into one canonical name per address (628 functions, 429 DGROUP labels). About 2 KB
of data and padding in code segments stay uncovered.

---

## 2. Game flow

### 2.1 State diagram

```
 c0_startup 1000:0000 -> main 130f:0007
   Game_Init 1460:0003  (fails -> exit 1)
     sprite bank 0x1C908 bytes (farmalloc), harderr, Hw_Detect, ParseCommandLine,
     the no-graphics-adapter message (DS:0693) -> exit, disk paths, Mouse_Detect,
     buffers, Video_Init, Pal_Apply, Mouse_Setup, Sound_LoadData, Help_LoadFile,
     Quiz_LoadDictionary, Sprites_LoadBegin (BEGIN.PAK), Game_InitVars
   Kbd_Install 43a0:0002, Timer_Install 1000:03c5
 T  TITLE       Title_Run 4ba4:0434: OPENING.PAN, phases DS:8d83: title, wait, story 1
                (DS:8a5a), wait, story 2 (DS:8b54), wait, finish -> sign-in. A wait
                (TitleStep_Wait 4ba4:00b2) ends after 0x1C20 ticks = 30.4 s or on Enter;
                the last wait times out (or "s") into the Super Solvers advert
                (4ba4:02b1) and the attract loop restarts; Esc quits (Game_Shutdown),
                on the advert it returns to the title
 S  SIGN-IN     g_uiMode DS:9119 = 8; frame loop runs SignIn_Update 47c7:0007 /
                SignIn_HandleInput 47c7:0f8b: list of up to 9 players, New Player,
                remove, "Continue last game?" -> Users_LoadPlayer 47c7:0b19, Game_Start 47c7:08b5
 G  GAME SET-UP record +9 == 0: Game_NewSetup 47c7:0bd6 -> Layout_Generate 1a54:0003
                else Game_Restore 47c7:0c62;  Quiz_LoadQuestions 46ce:09ab; music by area
 P  PLAY        frame loop (§3) with Player_Update / Elves_Update in the timer tick;
                sub-states (modal inside the frame loop, g_uiMode bits): menu 0x40,
                dialog/help 0x20, hint 0x10, message 4, quiz 2
                scene changes (Scene_Update 1437:0034) when g_level DS:928f changes:
                  0,1,2 mountain levels  -> 3 castle top close-up -> 4 treasure room
 E  END         castle top cell (1568:1aa4): total += treasures found, scene 3,
                end sequence (player states 41..71): treasures fly into the chest, chest fill
                to the rank; any key (176a:0404) -> showcase treasure added (PlrSt_34_Chute);
                castle gate cell (176a:0485): saved game cleared; total < 300 -> treasure
                room (scene 4), else crown sequence
 N  NEXT GAME   g_endState DS:9234 1 -> 2 (Game_AdvanceEnd 176a:1119); Scene_Update:
                Game_End 47c7:09a2 (save), Cells_ResetVariable, Layout_ClearSlots,
                Game_Start (a new layout at once), end-of-game message (DS:911c = 2);
                the player walks out of the treasure room (176a:1094) into scene 0
 X  EXIT        File menu "Exit and Save" (43cb:032c) -> g_quit -> Game_Shutdown 130f:00db
                (music/sfx off, Timer_Remove, Kbd_Remove, text mode, exit)
```

### 2.2 Start-up and hardware detection

`Hw_Detect` (1000:04a1) [O]:

* machine id byte F000:FFFE; Tandy 1000 if it is 0xFF and F000:C000 holds 0x21 (`DS:1300 = 0x21`);
* **slow CPU** flag `g_slowCpu` (DS:923c) = 1 when `shr bx,32` leaves 0 (an 8088/8086 does not mask
  the shift count); command-line `X` toggles it. On a slow CPU only 7 elves are active, Esc opens
  the exit box instead of the menu bar, some animated cells are simplified
  (`Cells_AdjustForCpu` 1a54:0803), scrolling moves in double steps and searches are shortened;
* video adapter `g_videoType` (DS:912c) by `Video_DetectAdapter` (1000:05bf): INT 10h/1Ah display
  combination (MCGA/VGA → 5, EGA → 1, EGA with 64 KB → 2), Hercules status-bit toggle → 4,
  INT 10h/12h, CRTC register probe (CGA → 2, Tandy → 3); none → −1 and the game refuses to start;
* sound device `g_soundDevice` (DS:63bc): 1 Tandy (machine id), 2 AdLib (`AdLib_Detect`
  4ac9:0d0f, timer-status test at port DS:656c = 0x388), 3 IBM PS/1 audio card (INT 15h/C0h model
  FC/0B and a loop-back test on port 203h), else 0 PC speaker.

Command-line switches (`ParseCommandLine` 1460:0463, `-x` or `/x`, case-insensitive) [O]: `C` CGA,
`E` EGA, `H` type 4 (the mode switch treats it like CGA, 1000:0738), `M` MCGA, `T` Tandy, `X`
toggle slow-CPU mode, `S<c>` sound device by the character after S: `i` speaker, `t` Tandy, `a`/`s`
AdLib (map DS:0629).

Video modes (`Video_SetModeByType` 1000:0738): EGA uses mode 0Eh (640×200×16) with latch patterns
and horizontally doubled pixels, MCGA mode 13h with the 16-colour palette expanded into the DAC,
Tandy mode 9 (320×200×16), CGA mode 4 (4 colours via a 16→4 table). All draw into one 4-bpp
320×200 back buffer (`2005:0010`, 160 bytes per row) that is presented by mode-specific copy
routines (`Present_Dirty` / `Present_Full` thunks patched at start-up).

### 2.3 Sign-in

`SignIn_Update` (47c7:0007) loads OPENING.PAN and BEGIN.PAK, reads USERS.DAT once
(`Users_EnsureLoaded` 47c7:0452), and the sign-in painter draws up to 9 players sorted by total
treasures (shell sort, `SignIn_DrawList` 47c7:01e0) with a rank label from `DS:625c` (Trainee,
1–6 stars) by the thresholds `DS:629d` = 5, 25, 70, 115, 170, 230, 300, plus "New Player".
Up/Down move, Enter chooses, typing starts a new name (max 8 characters, first letter
upper-cased; duplicates are checked case-insensitively by `Users_NameExists` 47c7:04a4), Ctrl+R
"Remove Player Name" (47c7:1328), Esc asks to exit (DS:5602). Choosing a player with a saved game
asks "Continue last game?" (DS:578e); "no" loses it (DS:5807). A full list (9 players) asks the
user to remove a name (DS:56a2) [O strings, handlers DS:616a/62b7].

`Users_LoadPlayer` (47c7:0b19) loads tier, coins, nets, games played and sound settings and turns
New Player Hints on when the total is below 5 (and re-arms all hints). `Game_Start` (47c7:08b5)
then sets up a new or saved game (§2.4) and starts the music of the area.

### 2.4 Game set-up

New game (`Game_NewSetup` 47c7:0bd6): treasures found = 0, key state = 8, all 9 question slots
empty (0xFFFF), answer masks 7, clue words 0, per-level found masks 0, all 48 hiding-place
cells cleared, the 9 variable cells reset (`Cells_ResetVariable`), then `Layout_Generate` (§5.3). The player starts at world
(0x140, 0x70) with the camera at (0xC0, 0) (start area), area id 5 (`DS:910b`) [O 47c7:0b19,
47c7:08b5]. A new player record gets 5 coins and 10 nets (`Users_InitNewPlayer` 47c7:0a8b); later
games start with the coins and nets banked at the end of the previous game (record +0x15C/+0x15A,
`Game_End` 47c7:09a2) [O].

Resumed game (`Game_Restore` 47c7:0c62): the whole layout, the per-level state, position, counters
and settings come from the record (§6).

---

## 3. Main loop and timing

### 3.1 Two clocks

* **Timer ISR** (`Timer_ISR` 1000:0428): PIT channel 0 reprogrammed to divisor 0x13B1 =
  **236.7 Hz** (`Timer_Install` 1000:03c5). Each interrupt: `g_ticks` (DS:3d2b, u32) += 1; every 13th
  interrupt chains the BIOS handler (keeps the 18.2 Hz clock); `Sound_Tick` (491b:0004) runs every
  interrupt while sound is active; every **5th** interrupt (counter DS:3d34) calls `Game_Tick`
  (130f:00fe), i.e. **47.3 Hz**, with interrupts re-enabled and a re-entrancy guard (DS:1309) [O].
* **Frame loop** (`main` 130f:0007): runs as fast as the machine allows; there is no frame limiter
  apart from a 1000-iteration delay loop when nothing changed (`Frame_Present` 130f:01c2) [O].

### 3.2 Game tick (47.3 Hz, interrupt time)

`Game_Tick` (130f:00fe) [O]: if the "step done" flag DS:910d is clear: player screen position;
`Mouse_Poll` (INT 33h/3); `Input_Tick` (4a85:03ac: key/mouse command, menu opening); `Anim_Tick`
(4306:0022, advances the 13 animation scripts); and, when the loaded scene is the wanted one (or on
the title): `Input_MouseWalk` (4a85:0208), `Player_Update` (18cf:181a), `Elves_Update` (4521:0ead),
`Cells_UpdateVisible` (1568:1f0b). If DS:910d is set, only input is polled and animations wait.
Counters: DS:90fe cycles 12..0; DS:9100 counts 60 ticks and then increments `g_seconds`
(DS:9115) — the "second" of the hint timers is **1.27 s** [O].

DS:910d is set when an animation script advanced a frame (`Anim_Tick`) or the camera moved
(`Player_Move` 18cf:15f0) and is cleared by `Frame_Begin` (130f:025a) at the start of every frame
[O]. So the logic advances **at most one visible step per drawn frame**: the game runs at 47.3 Hz on
a machine that draws faster than that and slows down with the frame rate on a slow machine. The
logic runs *inside the timer interrupt* while the frame loop may be half-way through drawing.

### 3.3 Frame (variable rate, main program)

`main` per frame [O 130f:0007]: `Frame_Begin` (latch camera and redraw flags, `Sprites_Snapshot`);
if no full redraw is pending: `SignIn_Update`, `Gameover_UpdateMessages`, `Hints_Update`,
`Menu_Update`; then `Scene_Update`, `Scroll_Update` (draw newly exposed tile columns/rows),
`Search_Update`, `Quiz_Update`, `Sound_SyncState`, `Game_HandleHotkeys`, `Screen_DrawOverlay`
(HUD / quiz / sign-in painter `DS:0104[g_screen]`), `Playfield_Redraw` (full redraw when requested),
`ShelfRoom_DrawShelf`, `RankChest_Update`; `Sprites_DrawSorted` (back to front by y, saving the
background under each), `Frame_Present` (dirty spans or full screen), `Sprites_EraseAll`.

Consequence: per-frame features (the hiding-place search animation, HUD count-up, rank/chest
animation, hint/quiz handling) run at the machine's frame rate, while walking, jumping, elves and
animation scripts run at up to 47.3 Hz (one step per frame at most, §3.2) [O]. The search is
shortened by 32 frames on a slow CPU (`Search_Update` 4a03:0003).

### 3.4 Screen layout

320×200 logical screen: menu bar y 0–10 (`MenuBar_DrawTitle` 43cb:0f1d), play-field
**312×128 px** (x bytes 2..0x9E, y 16..143, `Sprites_DrawSorted` clip 4306:0497), status bar below
(numbers at y 0xBA: coins x 0x6E, nets x 0x7D, treasures found x 0x8F, `Hud_Redraw` 130f:07a5).
The play-field shows one **cell row** (128 px) and about 2.4 cells horizontally (§4).

---

## 4. The world model

### 4.1 Cells and the map [O]

The whole game world is a strip of **256 cells of 128×128 px** stored as 16 rows × 16 columns:
world x 0..0x7FF, world y in steps of 0x80. Objects that walk off the right end of a row continue at
the left of the next row down the table and vice versa (`Obj_WrapWorldX` 18cf:17c9: x ≥ 0x800 →
x −= 0x800, y += 0x80). Per cell there are three tables:

| Table | Type | Meaning |
|---|---|---|
| `g_cellType` DS:0eaa | u8[256] | cell type 0..73, selects the two handlers below (fixed data) |
| `g_cellPicture` DS:0caa | u16[256] | scenery block of the cell: low 7 bits = 128×128 block number in the scene's PAN (`fmt_graphics.md` §4.1); hiding-place cells and castle cells get their block from the layout (saved) |
| `g_cellContent` DS:0faa | u16[256] | hidden content (layout, saved) |

Handlers per cell type [O]:

* `g_cellPlayerHandlers` DS:08a6[75] — called by `Cell_GetMask` (1568:1fe4) for the cell containing
  the object being moved (`g_curObj` DS:8fab: the player or an elf). It returns a **terrain mask**
  (DS:92c9) and may teleport the object, change the scene or play a sound. Mask bits: low
  byte = allowed keys (1 up, 2 down, 4 left, 8 right, 0x10 net, 0x20 jump, 0x40 coin), 0x100 ladder,
  0x200 a dropped coin pays the net shop instead of searching, 0x400 camera mode (scroll up earlier),
  0x800 slide/lift, 0x1000 fall, 0x2000 ladder jump [O; table in `code_world.md` §4.4]. The mask
  of the previous tick is applied to the held-key word (`Player_Update`: low byte ANDed, high byte
  copied).
* `g_cellVisibleHandlers` DS:09d2[74] — called every tick for the 4 cells across the view
  (`Cells_UpdateVisible` 1568:1f0b): static decorations (`Cell_DrawObject` 1568:1e50), the net-shop
  sign, the castle creature.

The 75 handler addresses and the cell types that use them are listed in
`re/names/dos/code_1568-176a_cells.tsv` (names `Cell_P<n>` = player handler of type n, `_V<n>` =
visible handler).

### 4.2 Scenes and how the mountain is laid out

`g_level` (DS:928f) selects the scene; `Scene_Update` (1437:0034) loads `MOUNTAIN.PAN`,
`MOUNT2N3.PAN`, `MOUNT3.PAN` (levels 0/1/2, with SPRITES.PAK), `CLOSE.PAN` + CLOSE.PAK (3, castle
top) or `SHACK.PAN` + SHACK.PAK (4, treasure room) and forces a full redraw [O]. Each scene uses a
part of the 16×16 strip; the block numbers of a region are only meaningful in its scene's PAN. The
regions, traced from every teleport target and confirmed by the capture matching (full table and an
ASCII map of the cell types in `code_world.md` §3.3) [O]:

| Scene / PAN | Cells (hex) | Part of the world |
|---|---|---|
| 0 MOUNTAIN | 00–05; 08–0B, 20–27; 38–6B (cols 8–B, rows 3–6) | clubhouse (new games start in 02 at (0x140,0x70)); level-0 cave and its upper room (bonus spot 26); the four treasure shelf rooms |
| 0 | E3–FF (rows 14–15); DF, CF | **level 0 path**; the trunk ladder up to level 1 |
| 1 MOUNT2N3 | AB–C2 (rows 10–12); 28–2A; 8E, 9E, AE | **level 1 path**; mine tunnel; geyser column above the level-1 gate AE |
| 2 MOUNT3 | 77–88 (rows 7–8); 70–76; 30–67 (cols 0–7, rows 3–6); 2D–2F | **level 2 path**; castle hall; castle wall; ice cave |
| 3 CLOSE | A8–AA; 5D, 6D | castle-top end screen; the chute down to the castle gate 6D |
| 4 SHACK | 98–9B; 1D–1F | treasure room; rank chest screen |

Three cell pairs show the same hiding place from two scenes; `Layout_MirrorSharedCells` (1a54:06dc)
copies 0xE3–E5 → 0xFD–FF, 0xAB–AD → 0xC0–C2, 0x77–79 → 0x86–88 and `Search_Update` marks both
copies searched [O].

### 4.3 Camera and scrolling

The camera (DS:37ea/37ec) follows the player horizontally in 8-px steps; `Scroll_Update`
(130f:04e5) shifts the play-field by one 8-px tile column (`Scroll_PlayfieldLeft/Right`
1000:18b3/196d) and draws the new column from the tile map (`Tile_DrawColumn` 1000:19cf).
Vertical moves (ladders) scroll by 8-row steps (`Scroll_PlayfieldUp/Down`); teleports
(up-passages, tunnels, doors) request a full redraw (`Playfield_Redraw` 130f:0326) [O]. On a slow
CPU the scroll steps are doubled (DS:3d31) [O `Player_Move` 18cf:15f0].

### 4.4 Terrain features [O unless noted]

* **Path line**: an object stands when `y & 0x7F == 0x70`; cell handlers stop downward motion there
  (`Cell_GroundCheckObj`/`Cell_GroundCheckPlayer` 1568:0134/0203).
* **Ladders and up-passages between paths**: in these cells the mask allows Up/Down only in the
  ladder's x range (e.g. x & 0x78 within 0x26..0x5A) and Up climbs (`PlrSt_15_ClimbUp`); once the player is more than 8 px
  above the path line (`y & 0x7F < 0x68`, `Cell_GroundCheckPlayer` returns 1) he is moved to the
  ladder's destination on another path row and the scene/music may change: `Cell_P5_CaveDoorW` 1568:0008 →
  (0x460,0x70), `Cell_P9_CaveDoorE` 1568:01b4 → (0x5b0,0x70), `Cell_P11_ClubDoor` 1568:0386 → (0x140,0x70), `Cell_P17_MineDoorW`
  1568:0b38 → (0x440,0x170), `Cell_P18_MineDoorE` 1568:0bfb → (0x540,0x170), `Cell_P34_CaveUp` 1568:0303 →
  (0xc0,0x170), `Cell_P35_UpperRoomDown` 1568:0341 → (0x4c0,0x70), `Cell_P60_IceDoor` 1568:1953 → (0x6c0,0x170). Other
  ladder cells keep the player in the same column (`Cell_P30_V30_TrunkTop` 1568:0a6c, `Cell_P40_V40_Geyser`
  1568:1286, `Cell_P7_WallLeft/P8` 1568:05cb/0638, `Cell_P48_Landing` 1568:16ed); at a ladder-jump spot (mask
  0x2000) Left/Right jumps to another ladder (state 0x21, hint text 12) [O].
* **Geyser hole**: cell type 41 is only cell 7E, the top of the geyser on level 2
  (`Cell_P41_V41_GeyserTop` 1568:141c). A player walking through its middle (x & 0x78 within
  0x18..0x50) who is not jumping (states 17–20) drops in: slide animation 0x1D, state 0x1E, mask 0x800
  (`PlrSt_30_SlideDown` 18cf:1367, sfx 1), down 7E → 8E → 9E to the level-1 gate cell AE (`g_level`
  1). Jumping over it avoids it. Message 11, the slid-down-a-tunnel hint (DS:70c1), has no code
  that arms it (§5.14) [O, `code_world.md` §6.5].
* **Rope bridges** (type 45, cells 84 on level 2 and B7 on level 1, `Cell_P45_V45_Bridge` 1568:15bc):
  at tier ≥ 2 a bridge breaks under a walking player in its middle (picture 0x2F → 0x30, sfx 9) and
  he falls to the level below (B4 / E7); jumping over avoids it. This is the tier-dependent gap of the
  4-star message that warns about gaps in the path. On a slow CPU the bridges are plain path
  (`Cells_AdjustForCpu`) [O, `code_world.md` §6.6].
* **Treasure shelf rooms** (cells 38–6B, from the clubhouse door cell 04): the cells
  `Cell_P50_ShelfEnter`..`Cell_P66_Shelf3to2` (1568:06a5..09e8) move the player between the clubhouse
  and the four rooms where the collected treasures stand on shelves (DS:9201.., DS:7d7c..,
  `ShelfRoom_DrawShelf` 4a03:04a8) [O, `code_world.md` §6.3]. (The first synthesis took them for
  slide-down tunnels.)
* **Path links**: some cells convey a walking object into another part of the strip
  (`Cell_ConveyLeft/Right` 176a:0019/0110, offsets like (−0x500,−0x80)); elves near the player are
  moved along (`Elves_ShiftNearby` 4521:12df).
* **Exits/doors**: cells that teleport when walking out (e.g. `Cell_P6_CaveExitW` 1568:00ce → (0x5b0,0x770),
  `Cell_P10_CaveExitE` 1568:029d) and the start-area door (`Cell_P12_StartRoom` 1568:03f6: Up enters level 0 at
  (0x1c0,0x7f0)).
* **Level gates (locks)**: `Cell_P31_GateL0` (1568:0f17) level 0 → 1, `Cell_P39_V39_GateL1` (1568:1329) level 1 → 2,
  `Cell_P42_GateL2` (176a:02dc) level 2 → castle. Each opens only with the level's key (`g_keyState`
  DS:9120 == 2/4/6); otherwise Up arms hint 8 "You must have the key" [O].
* **Level boundaries**: `Cell_LevelBoundary01/12` (1568:0e6d/12b4) switch `g_level` when the
  player crosses between scenes, in either direction, and set the key icon state for the level
  entered (10/12 = that level's key still hidden, 2/4 = already found) [O].
* **Bonus coins**: cell type 73 (cell 26, the upper room of the level-0 cave; `Cell_BonusCoins`
  1568:1ed8): once per game, an object at `x & 0x7F == 0x48` gives the player **+5 coins** (sfx 4)
  and changes the cell picture (0x4B → 0x49) [O]. The handler does not check that the object is the
  player, so an elf passing the spot would also pay out [O]. The picture is entry 0 of the 9
  variable cells (DS:120a): it is saved with the game and reset for every new game
  (`Cells_ResetVariable`) [O, `code_world.md` §3.5].
* **Net shop**: §5.9. **Castle wall**: §5.12.

---

## 5. Game rules

`R(n)` below means `rand() % n` (Turbo C `rand`: seed·0x015A4E35+1, result bits 16–30;
`rand` 4d12:001f). Masks like `rand() & 0x3F` are written as such.

### 5.1 Tier and rank

| Total treasures (record +0x0A) | < 5 | 5–24 | 25–69 | 70–114 | 115–169 | 170–229 | 230–299 | ≥ 300 |
|---|---|---|---|---|---|---|---|---|
| Rank label (DS:625c by DS:629d) | Trainee | 1 star | 2 stars | 3 stars | 4 stars | 5 stars | 6 stars | Champion |
| Tier `g_tier` DS:9130 | 0 | 0 | 1 | 1 | 2 | 2 | 3 | 3 |

The tier is raised at every new layout to the total's tier (25–114 → 1, 115–229 → 2, ≥ 230 → 3;
`Layout_PlaceTreasures` 1a54:029a; a total below 25 leaves the loaded tier) and saved (record
+0x140) [O, `code_elves_layout_hints.md` §5.7].

| Effect of the tier | 0 | 1 | 2 | 3 | Source |
|---|---|---|---|---|---|
| Treasures hidden per level | 2 | 3 | 4 | 5 | 1a54:029a |
| HUD treasure slots per level | 2 | 3 | 4 | 5 | `Hud_Redraw` 130f:07a5 (tier+2) |
| Elves (A walkers / D coin creatures / B throwers / C walkers, spawn order) | 3/3/0/3 = 9 | 3/3/2/3 = 11 | 3/3/4/3 = 13 | 3/3/6/3 = 15 | `Elves_Spawn` 4521:182e, DS:5d34 (columns A, B, C, D) |
| Castle ladder layout | 0 | R(2) | R(3) | R(4) | `Castle_PickLayout` 1a54:0892 |
| Castle creature chance per aligned tick | never | 1/512 | 1/256 | 1/64 | `Castle_CreatureSpawn` 176a:159e, DS:0afa |
| "Found all" in a game | 6 | 9 | 12 | 15 | `Gameover_UpdateMessages` 49a6:026f |

### 5.2 Player controls and states

`Player_Update` (18cf:181a) runs `g_playerStateHandlers` DS:0b8a[`g_playerState` DS:913c] with the
held-key word `g_keys` (DS:9105) masked by the cell mask, then `Player_Move` (18cf:15f0) [O].
Key bits (set by `Kbd_ISR` 43a0:0071 and `Input_MouseWalk` 4a85:0208): 1 up, 2 down, 4 left,
8 right, 0x10 net, 0x20 jump, 0x40 drop a coin [O].

State 10 (`PlrSt_10_OnPath` 18cf:08cc, standing on the path) [O]:

| Input | Effect |
|---|---|
| Left / Right | walk (states 11/12) |
| Up | climb a ladder (mask 0x100, state 15), else slide (0x800) or look up (state 13) |
| Down | climb down at a ladder; elsewhere **drop a coin** (`Input_MouseWalk` turns Down into 0x40 when the cell allows no vertical move) |
| Up+Left / Up+Right, Home / PgUp | jump left/right (states 17–20; the ISR converts a diagonal into the jump bit) |
| Space (+direction) | swing the net (`Player_NetLeft/Right` 18cf:0b36/0b6a, states 21–26) |
| drop a coin | if coins > 0: coins −1, drop animation (states 27/28, `DS:9002` offset ±25); then §5.4 |

With no coins nothing is dropped; hint 7 is armed, and hint 6 as well when nets are also 0 [O 18cf:08cc].

### 5.3 Hiding places and the layout

`g_hidingCells` (DS:11aa) lists **44 hiding places plus 4 mirror cells** (slots 11, 12, 27, 28:
cells 86, 87, C0, C1, which only receive copies, §4.2). Each place holds a picture
(`g_cellPicture`) and a content (`g_cellContent`), and **every place is filled in every game**
(`code_elves_layout_hints.md` §5.3). Per level the picture set and the number of places come from
`g_levelPicData` (DS:122e) [O]:

| Level | Pictures | Places = slots in DS:11aa | Key | Treasures (match 2) | Decoys (match 1) | Others (match 0) |
|---|---|---|---|---|---|---|
| 0 | 50 | 19 (29–47) | 1 | t = 2..5 | t + 3 | 15 − 2t |
| 1 | 38 | 14 (13–26) | 1 | t | t + 2 | 11 − 2t |
| 2 | 31 | 11 (0–10) | 1 | t | t + 1 (t when total > 380) | 9 − 2t (or 10 − 2t) |

(t = treasures per level by tier, §5.1; the "places" value of the table, 18/13/10, excludes the
key.) Details that change the result: a square counts as a rectangle, sizes match by value (the
key's lowest non-zero size field), and the same picture can appear in several places of a level
(`code_elves_layout_hints.md` §5.3, §5.7).

`Layout_Generate` (1a54:0003) [O]:

1. `srand(time())` — the only `srand` in the program; a resumed game keeps the unseeded sequence.
2. Clue scheme `g_clueScheme` (DS:9296) = R(3): which picture attributes the clue words describe
   (DS:5fb3 = {0,3,2}, {0,1,2}, {4,1,2}; each picture has a 4-byte attribute record in the level's
   table `DS:1246[level]`).
3. Per level, `Layout_PlaceKey` (1a54:0179): a random key picture (index 1..count, re-drawn until it
   has every attribute the scheme needs), key content `0x66 + 2·level` placed by `Layout_PutInSlot`
   (1a54:0059, random free slot of the level), then `Clue_BuildWords` (46ce:079c) derives the 3
   clue words of the level from the key picture.
4. `Layout_PlaceTreasures` (1a54:029a): for place i = 0..places−1, the next picture (cycling from a
   random start) that matches the key picture in exactly **2** attributes (i < treasures), **1**
   (decoys) or **0** (the rest); treasures get content `6 + 2k` with k = a random treasure 0..47 not
   in the player's collected list, not shown, not already placed; all other places get 0x80 (empty).
5. Castle: layout R(tier+1) written into the castle cells (`Castle_ApplyLayout` 1a54:08dc).

So **one key per level** (the key fits all 3 clue words), **treasures match exactly 2 of the 3**, decoys
1, the rest none. The Windows layout records have the same structure and the same treasure and
decoy counts as DOS level 0, with one empty spot fewer (Windows 18 spots, tier 0: 10 empty /
5 decoy / 2 treasure / 1 key; DOS level 0, 19 places: 11 / 5 / 2 / 1) [O both]. Levels 1 and 2 have
fewer places in DOS (14, 11) but 18 spots in Windows.

### 5.4 Searching (dropping a coin)

When the drop animation ends (`PlrSt_27_CoinLeftDone/28` 18cf:1282/12bc) [O]:

* in the net shop (mask 0x200): the coin pays the shop (§5.9);
* else `Search_Start` (18cf:14e0): `Search_CanSearch` (18cf:1532) picks the cell in front (the
  neighbour cell when the player stands within 32 px of a cell edge facing it, DS:9276) and accepts
  it when its block number is at most the level's picture count + 3 (closed hiding-place blocks
  1..count, their opened variants count..count+3, and 0 = an unused slot; path, ladder and other
  scenery blocks have higher numbers); if so the search starts (timer DS:8fa3 = 0x39, `g_searchCell` DS:8fc6). Otherwise the
  coin is simply lost.

`Search_Update` (4a03:0003, per frame) alternates the opening picture and shows the content sprite;
on the last step [O]:

| Content | Result |
|---|---|
| < 0x66 (treasure 6+2k, not yet searched) | `g_treasuresFound` (DS:9113) += 1, next free bit of the level's found mask (record +0x130), sfx 7, hint 4 (the two-of-three-clues hint) |
| 0x66 / 0x68 / 0x6A (key of level 0/1/2) | `g_keyState` = 2/4/6, sfx 6, hint 9 ("You found the key!") |
| 0x80 (empty, decoy) | nothing |

During the search the picture alternates between the closed picture and its opened variant
(count + bits 4–5 of the picture's first attribute byte, tables DS:0184/0250/02ec) and ends closed
again. The content is marked searched (bit 0x80, also in the mirror cell), so a second search of the
same place costs a coin and finds nothing; nothing on screen shows that a place was searched [O].

### 5.5 Clue words

Three clue words per level, stored in `g_clueWords` (record +0x81, 3 bytes per level, bit 7 =
not yet revealed) [O `Clue_BuildWords` 46ce:079c]. A correct elf answer reveals one hidden word of
the current level in a random order (`Clue_Reveal` 46ce:06f0, 6 orders at DS:5fbc) and shows it on
the praise screen; the HUD lists the revealed words of the level (`Hud_Redraw`) [O]. Words are per
level and kept when the player moves between levels during a game [O: never cleared except by
`Game_NewSetup`].

### 5.6 Keys and gates

`g_keyState` (DS:9120, record +0x14A): 8 at a new game (level-0 key not found), 2/4/6 = key of level
0/1/2 found, 10/12 = entering level 1/2 whose key is still hidden; the HUD shows the key icon
accordingly. Gates need 2, 4, 6 respectively (§4.4) [O]. Because the state is per level and the
boundaries work both ways, a player can go back down (falls, tunnels) and return through an opened
gate.

### 5.7 Treasures and score

* Found treasures count in `g_treasuresFound` (shown on the HUD) and the level's found mask; they
  stay in their hiding places (content | 0x80) until the end [O].
* At the castle top (`Cell_P63_V63_CastleTop` 1568:1aa4, picture 0x32): **total treasures += treasures found
  this game** (u16, no saturation) and the end sequence starts (§5.13) [O].
* Collection: each finished game adds one treasure to the player's collection: the "showcase"
  treasure is chosen among those found this game, preferring one not yet collected
  (`Treasure_PickShowcase` 4a03:038c), added to the 48-entry collected list and the shelves
  of the treasure shelf rooms (`Treasure_AddCollected` 4a03:078c, record +0x8A/+0x133/+0x164) [O mechanics, purpose I].
* Champion: total ≥ 300. Arriving at the castle gate with a total of 300 or more plays the crown
  sequence instead of the treasure room (176a:0485); the end messages for a total of 300–314 are the
  crown texts (messages 35, then 36, "won back the crown") [O].

### 5.8 Coins

| | |
|---|---|
| New player | 5 (`Users_InitNewPlayer` 47c7:0a8b) |
| Carried over | yes, banked at the end of each game (`Game_End` 47c7:09a2) |
| Spent | dropping a coin (search or shop payment), −1 |
| Lost | elf dust hit −1 (`ElfSt_53/57` 4521:0855/0aac), castle creature −1 (`Castle_CreatureHitCheck` 1568:18f1) |
| Gained | each net catch +1 (`Elf_NetCatch` 4521:147c); correct answer **+2/+1/0** by earlier wrong tries (`Quiz_Correct` 46ce:05bb, DS:5f93); coin creature +2 (`ElfSt_72` 4521:0cfe); bonus cell +5 once per game (1568:1ed8) |

The HUD counts the coin display up one by one with sfx 4 (`Hud_Redraw`) [O].

### 5.9 Nets and the net shop

* `g_nets` (DS:910f): 10 for a new player, carried over between games [O].
* A net swing costs nothing by itself. **Each catch uses up one net**; catches resolved in the
  same tick share one (`Elf_NetCatch` 4521:147c, `g_netCatchesThisTick`, reset at the end of every
  `Elves_Update`), so two elves pulled in by one swing usually cost one net and elves caught on
  different ticks one each [O, `code_elves_layout_hints.md` §4.8].
* With 0 nets the swing uses the torn-net animation (0x15/0x18 instead of 0x14/0x17) and elves
  escape (`Player_NetLeft/Right`, `Elf_NetCatch`); hint 5, the worn-out-net hint [O].
* **Net shop** (cell type 36, `Cell_P36_V36_NetShop` 1568:1090; cells 188 and 234 [O table], i.e. on levels
  1 and 0 [I]): the price counter DS:92a1 (record +0x158) starts at 4; each coin dropped at the shop
  pays one (mask 0x200 → `DS:92a1 −= 1`); the sign shows the coins still owed (frames 0x188+2·(n−1),
  0x194 when paid); when paid, climbing into the cave gives **+5 nets** (DS:92a1 = −1), leaving resets
  the price to 4; the door picture changes (0x46/0x47/0x48) [O].

### 5.10 Elves

Up to **15 elves** (`g_elves` DS:37fa, 32 bytes each; only 7 are processed on a slow CPU,
DS:a212), respawned at every scene change with `Elves_Spawn(hidden clue words of the level)`
(4521:182e from `Scene_Update`) [O]:

| Type | Base states | Count | Behaviour |
|---|---|---|---|
| A walker / clue carrier | 1, 5 | 3 | walks; the first *n* carry question slots 1..n, where n = clue words of the level still hidden; carriers use a different sprite set (anim offset 0x22) |
| D coin creature | 25 | 3 | a gold coin lying on the path that grows legs: it appears only at a view edge and only when coins < 4 and nets < 5; touching it (±5 bytes) gives **+2 coins** after its sparkle and it respawns at a random x of the level; netting it makes it run away for good (`ElfSt_25/70/71/72`, `CoinElf_Check` 4521:0d93) |
| B thrower | 9, 13 | 0/2/4/6 by tier | when the player is ahead may throw **elf dust** (anim 0x25/0x26); the dust moves 2 px per tick and vanishes after 160 px if it hits nothing; a hit (within ±10 bytes, player on the path line, not jumping, coins > 0) costs a coin and only then does the dust fly back with it; netting does not protect, and dust thrown to the right is only avoided by jumping left (`ElfSt_52..59`, `code_elves_layout_hints.md` §4.5) |
| C walker | 17, 21 | 3 | walks only |

Placement: random x inside the level's range (DS:5d54: base, row, mask; `Elves_Spawn`), facing by
type. Movement is driven by the animation scripts; a walking type-A elf pauses with chance 1/16 per
walk cycle (`Elf_WalkCommon` 4521:04e6) and then picks a random direction (`ElfSt_47` 4521:0545);
after a dodge a walker resumes away from the player; elves
turn at some terrain cells (`Elf_Turn` 4521:181a, called from cell handlers such as 1568:141c); at
most 3 elves have sprites at a time (slots 6/8/10, `Elf_Think` 4521:1062) [O].

**Net catch** [O `Elf_Think`, `Elf_CheckNet` 4521:1648, `Elf_NetCatch`]: only while the player is in
net state 21 (left) or 24 (right). Three nested zones in front of the player, mirror images about
P − 1 byte for the two directions (exact bounds in `code_elves_layout_hints.md` §4.3): edge → class
4, net → 5, centre → 6. Class 4: the elf hops
aside (anim 0x33/0x34, state 64) and walks on, or with chance 1/64 leaves the screen (states 73/74;
the visuals are [I]). Classes 5/6: caught; after the animation
`Elf_NetCatch` needs nets > 0, uses one net (shared by the catches of one tick), **+1 coin**, and a
carrier opens its question (`g_caughtSlot` DS:921e = its slot, `g_uiMode |= 2`). After a correct
answer the elf loses its question; after a wrong answer or Esc it keeps it (`ElfSt_03`
4521:15ab) [O].

### 5.11 Elf questions

* 360 questions in ALL_PUZS (index of 0x2D0 bytes), texts compressed with PRG_DICT (bytes ≥ 0x80 →
  16-byte dictionary words, `Dict_Expand` 46ce:08f1) [O].
* Tiers by index (`g_quizTierStart` DS:5fd2): 0–62, 63–179, 180–302, 303–359; tier 5 = questions
  X..359, where X (123 with the shipped file) is the first question whose text no longer fits one
  0x7B00-byte read from the start (`Quiz_FindTierEnd` 46ce:0d9a; details in `fmt_data.md` §2.3)
  [O].
* History: 2 bits per question (record +0x0C): 0 never answered, 1 right first time, 2 right after a
  miss, 3 last answer wrong (`Quiz_Correct` writes 1/2, `Quiz_Wrong` sets both bits) [O]. Windows
  uses the same four states with 2 and 3 swapped.
* New game: if right-first-time ≥ **70 %** of the tier, the tier goes up (`Quiz_TierHasWork`
  46ce:0cd0); past tier 3 it becomes 5 and the history is reset (`Quiz_ResetHistory`) [O].
* Deal (`Quiz_Deal` 46ce:0b6a with `Quiz_PlanStatuses` 46ce:0c39): 9 distinct questions of the tier
  with wanted statuses {0,3,2,0,3,2,0,3,2} (DS:5fe0) and fallbacks, i.e. 3 new, 3 last-wrong and
  3 right-after-a-miss questions, as in Windows [O].
* Level change (`Quiz_ReorderSlots` 46ce:0df2): the 9 slots are reordered — missed questions first,
  then unanswered, answered last — so the carriers of the new level (slots 1..3) ask missed questions
  first [O].
* The question screen (`Quiz_Update` 46ce:0007): answers in one of 6 orders (DS:5e9c, R(6)); Up/Down
  or mouse select, Enter answers, Esc closes without effect. The chosen answer is disabled
  (`DS:9107[slot] &= ~bit`). **Choice 0 of the file is correct.**
* Correct (`Quiz_Correct` 46ce:05bb): slot answered (bit 15), praise text 15–18 (R(4)), clue word,
  sfx 10, reward by the remaining mask: first try +2, one miss +1, two misses 0 (DS:5f93); history 1
  or 2. Wrong (`Quiz_Wrong` 46ce:069e): "Oops" text 19–22, sfx 11, history 3. Coins are applied
  when the screen closes (DS:b1f8) [O].

### 5.12 Castle wall

The castle wall (rows 3–6) is part of the scrolling world: ladder cells (pictures 0x25, 0x27, 0x2D,
0x31) with masks for climbing and ladder jumps (`Cell_P56_V56_CastleHall` 1568:1734, `Cell_P57_V57_CastleWall` 1568:182b),
the layout chosen per game (§5.1). The **Master of Mischief** appears in a window next to the
climber when a window cell is exactly 32 bytes (64 px) left or right of the player sprite and
`rand() & mask(tier)` hits (`Castle_CreatureSpawn` 176a:159e: never at tier 0, 1/512, 1/256, 1/64);
its strike (animation
op −6) at the player's height costs a coin (sfx 5) and pushes the player sideways
(`Castle_CreatureHitCheck` 1568:18f1) [O]. Reaching picture 0x32 at the top ends the climb (§5.13).

### 5.13 End of a game

1. Castle top (1568:1aa4): total += treasures found; scene 3; player state 41 (`EndSeq_Setup`
   18cf:0038) [O].
2. End sequence (player states 41–71, 18cf:00a9..07ea): every treasure found this game flies into
   the treasure chest (`PlrSt_49_EndFlightNext` 18cf:0205, paths DS:0b06/0b3c); "all found" (tier·3+6) gets a
   special animation (`PlrSt_55_EndCrown`); the chest fills to the level of the rank (`PlrSt_52_EndRank`, `PlrSt_70_ChampSparkle`,
   table DS:0110: 5→1, 25→8, 70→16, 115→24, 170→32, 230→40, 300→48, 400→56) [O].
3. Any key or click (`Cell_P64_EndScreen` 176a:0404) → the showcase treasure is handed over (`PlrSt_34_Chute`,
   `Treasure_AddCollected`) [O].
4. Castle gate (`Cell_P20_CastleGate` 176a:0485): the saved game is cleared (record +9 = 0),
   `g_endState` = 1; total < 300 → treasure room (scene 4), else the crown sequence [O].
5. Treasure room (`Cell_P70_TreasureRoom` 176a:0596, scene 4): the showcase treasure on its pedestal
   with a display per treasure (a 53-case switch), then, unless the player stays, the rank chest
   screen (`Cell_P71_V71_ToRankScreen` 176a:0fa6, `RankChest_Update` 130f:0edf); any key or click
   there (`Cell_P72_RankScreen` 176a:1094) leads back to the clubhouse (scene 0), as does
   `TreasureRoom_Leave` (176a:0f47) [O, `code_player.md` §7]. Both rooms can also be visited from the
   clubhouse: the right-hand part of the start cell (`Cell_P12_StartRoom` 1568:03f6) leads into the
   treasure room, the chest picture (cell 01) into the rank screen [O].
6. `Scene_Update` with `g_endState` == 2: `Game_End` (games played +1, banked coins and nets, save),
   immediately a new layout (`Game_Start`), and the end-of-game message: 0 treasures → 31, 1 → 32,
   all → 34, else 33 with the number; total 300–314 → crown messages 35, then 36 (`Gameover_UpdateMessages`
   49a6:026f). The next game's start message (`DS:911c` = 1) is the rank message 23–30 with "N more
   treasures" filled in, or the Flutter introduction (message 0) for a first game [O].

### 5.14 New Player Hints (Flutter)

15 one-shot hint flags `g_hintFlags` DS:923e (0 pending, 0x80 shown, 1 idle);
`Hints_Update` (49a6:000f) shows the highest pending one with `Flutter_Show` (49a6:0565, text
`DS:7ca3[i]`) while no dialog is open; Enter/Esc/click closes. Hints are on when the total is below 5
at sign-in or via Options; they never switch themselves off during play
(`code_elves_layout_hints.md` §6.6). Triggers [O]:

| # | Trigger | Code | Message |
|---|---|---|---|
| 1 | no Left/Right for 3 "seconds" in the start area (area 5) | 49a6:0121 | how to walk |
| 2 | no net swing for 31 "seconds" in area 0 | 49a6:0121 | how to use the net |
| 3 | 2 clue words known and no coin dropped for 5 "seconds" | 49a6:0121 | how to drop a coin |
| 4 | first treasure found | 4a03:0003 | the two-of-three-clues hint |
| 5 | a net swing (anim 0x16/0x19) with 0 nets | 49a6:0121 | net worn out |
| 6 | coin key with no coins and no nets | 18cf:08cc | need coins to fix the net |
| 7 | coin key with no coins | 18cf:08cc | catch an elf to get a coin |
| 8 | gate without the key | 1568:0f17, 1329 | need the key |
| 9 | key found | 4a03:0003 | found the key |
| 13 | end-of-game message shown | 49a6:026f | File menu / walk out the door |
| 0, 10, 11, 12, 14 | no writer found (0 is shown as the first start message instead; 14's text is a placeholder) | – | intro, "on your way", tunnel, ladders, – |

A "second" here is 1.27 s (`g_seconds`).

### 5.15 Randomness inventory

| Use | Call | Address |
|---|---|---|
| seed | `srand(time())`, new games only | 1a54:0003 |
| clue scheme, key picture, places, treasure ids | R(3), `rand()&0x3F`, R(count), R(48) | 1a54:0003..029a |
| castle layout, creature | R(tier+1); `rand()&mask` | 1a54:0892, 176a:159e |
| clue words, reveal order | `rand()%4`, R(6) | 46ce:079c, 06f0 |
| quiz deal, answer order, praise/oops | various, R(6), R(4) | 46ce:0b6a, 02d9, 05bb, 069e |
| elves | spawn x, pauses 1/16, dodges 1/64, throws, coin-elf respawn | 4521 |

---

## 6. Save data (USERS.DAT)

USERS.DAT is **3 bytes header + 9 records of 360 (0x168) bytes = 3243 bytes**, read and written as
one block into DS:ba14 (`Users_ReadFile`/`Users_WriteFile` 47c7:0514/055a; record offsets
DS:6132 = 3 + 360·i) [O]. Header byte 1 is saved from/to DS:929f [O]. Record layout
(`Users_Pack` 47c7:0e0e, `Game_Restore` 47c7:0c62, `Users_SelectRecord` 47c7:09dd) [O]:

| Off | Size | Field |
|---|---|---|
| 0x000 | 9 | name (NUL-terminated, ≤ 8) |
| 0x009 | 1 | saved game present (0 none; set from DS:9205 when leaving) |
| 0x00A | 2 | total treasures (sort key, rank) |
| 0x00C | 90 | question history, 2 bits × 360 |
| 0x066 | 18 | 9 question slots (u16, bit 15 answered) |
| 0x078 | 9 | 9 enabled-answer masks |
| 0x081 | 9 | clue words, 3 per level (bit 7 hidden) |
| 0x08A | 48 | collected treasure ids (0x80 none) |
| 0x0BA | 1 | (unused) |
| 0x0BB | 57 | pictures of the 48 hiding-place cells + the 9 variable cells (bonus spot, gates, bridges, net shop) |
| 0x0F4 | 48 | contents of the 48 hiding places |
| 0x124 | 12 | per-level picture data (6 words of DS:121c+18 via DS:62ab) |
| 0x130 | 3 | found-treasure masks per level |
| 0x133 | 12 | shelf bits of the treasure shelf rooms |
| 0x13F | 1 | quiz tier (0..3, 5 = all) |
| 0x140 | 2 | tier |
| 0x142 | 2 | level (scene) |
| 0x144 / 0x146 | 2 / 2 | coins / nets |
| 0x148 | 2 | treasures found this game |
| 0x14A | 2 | key state |
| 0x14C | 2 | HUD icon state (DS:90f8) |
| 0x14E / 0x150 | 2 / 2 | player world x / y |
| 0x152 / 0x154 | 2 / 2 | camera x / y |
| 0x156 | 2 | area id (music) |
| 0x158 | 2 | net-shop coins owed |
| 0x15A / 0x15C | 2 / 2 | banked nets / coins for the next game |
| 0x15E / 0x160 | 2 / 2 | music on / sound on |
| 0x162 | 2 | games played |
| 0x164 | 2 | showcase treasure |
| 0x166 | 2 | castle layout |

Not saved: elf positions (respawned), the RNG seed, hint flags (re-armed at
sign-in).

When it is written [O]: new player (`Users_AddPlayer` 47c7:07b9), remove player
(`Users_ClearRecord` 47c7:075d), File → Start Over (record +9 = 0, game abandoned, 43cb:028b),
File → Start New Player (game kept, 43cb:02f8), File → Exit and Save (game kept, 43cb:032c), and
`Game_End` at the end of every game (47c7:09a2). There is no save on the title screen or at a level
change.

---

## 7. Options, menus and controls

### 7.1 Menus (drawn by the game, `Menu_Update` 43cb:0001)

| Menu | Items (handlers DS:5b6f) |
|---|---|
| File | Start Over (43cb:028b), Start New Player (02f8), Exit and Save (032c) — each with a Yes/No box (DS:5345, 53bc, 545b) |
| Options | New Player Hints (034f), Music (0371: on also turns sound on), Sound (039d: sound and music) |
| Help | About the Game, Using the Keyboard, Using the Mouse, Getting More Hints (pager 43cb:021d over HELP.DAT, Done / See Next Page / Go Back a Page) |

Esc or F1/F2/F3 (or a click on a menu title) opens the bar; arrows move; Enter chooses. On a slow
CPU Esc opens the "Exit and Save" Yes/No box directly (dialog 10 → box 5 via DS:5ba7) instead of
the bar (`Input_Tick` 4a85:03ac) [O].

### 7.2 Keyboard (play)

| Key | Effect | Source |
|---|---|---|
| ← → | walk | `Kbd_ISR` 43a0:0071 (scan 4B/4D; Tandy 2B/4E) |
| ↑ ↓ | ladders / look; ↓ elsewhere drops a coin | scan 48/50 (Tandy 29/4A); `Input_MouseWalk` |
| Home / PgUp, or ↑ with ←/→ | jump left / right | scan 47/49 → bits 5/9 → jump 0x20 |
| Space | swing the net (in the facing direction, or with ←/→) | scan 39 |
| Esc, F1–F3 | menu bar | key table DS:7d84 → commands 0..3 |
| Ctrl+S | sound and music on/off | command 14, `Game_HandleHotkeys` 43cb:03cb |
| Y / N | Yes / No in dialogs | context 2 table |

The ISR keeps held-key bits for the movement keys itself (and swallows those scan codes); all other
keys go to the BIOS buffer and are read with `bioskey` through per-context tables (`Input_Poll`
4a85:000e; contexts DS:922c: 0 play, 1 menu, 2 dialog, 4 name entry) [O].

### 7.3 Mouse (play)

Mouse zones of context 0 (DS:7f2e) [O]: menu titles (y 0–10); the play-field (y 16–143): press or
hold = command 9, release = stop; the status bar (y 146–199): press = drop a coin.
`Input_MouseWalk` (4a85:0208) turns command 9 into key bits from the click position relative to the
player: beside him → walk; above → up; below → down; diagonally above → jump; diagonally below → net
[O]. An idle pointer is hidden after 600 ticks (`Mouse_Poll` 130f:0d94).

### 7.4 Sound and video choices

Detected automatically (§2.2) or forced with `/C /E /H /M /T` and `/S<i|t|a|s>`. Music (3 tunes by
area, `Music_Play` 491b:0791) and effects (`Sfx_Play` 491b:06d7) on the speaker, Tandy 3-voice
chip, AdLib or PS/1 card; there is no speech. Music and Sound are per-player settings.

---

## 8. Differences from the Windows CD edition

Legend: **same**, **different (how)**, **DOS only**, **Win only**. Impact: **G** = changes how the
game plays, **C** = cosmetic or technical only. Windows facts are from `re/docs/ARCHITECTURE.md`.

### 8.1 Structure and flow

| Feature | Status | Impact | Notes |
|---|---|---|---|
| Overall goal (find treasures, keys open 3 gates, deliver at the castle, ranks to Champion) | same | – | |
| Mountain representation | different | **G** | DOS: one continuous **scrolling** path (256 cells of 128×128 px wrapped in a 16×16 table) with ladders, gaps, slide-down tunnels and path links; Win: 50 fixed rooms with left/right exits and doors |
| Hiding places | different | **G** | DOS: at most one per 128-px cell, 19/14/11 per level (key included), generated from picture attributes; Win: 3 per room, 18 per level from precomputed OTHR layout records |
| Going back down | different | **G** | DOS: possible (gaps, tunnels, boundaries work both ways; per-level state kept); Win: one-way |
| Level change resets | different | **G** | DOS: clue words, key and found treasures are per level and persist all game; Win: clues, key and gems reset at each level change |
| Start of a game | different | **G** | DOS: start area at the foot of the mountain, new layout immediately after each game; Win: room 9 (first climb) or the treasure hall |
| End of a game | different | **G/C** | DOS: castle top close-up, treasures fly into a chest, chest fills to the rank, treasure room with shelves, crown sequence ≥ 300; Win: celebration animations, rank screen, treasure hall with pedestals |
| One treasure added to the collection per game | same idea | C | DOS: showcase treasure chosen among those found; Win: `330 + (climbsDone−1) mod 43` |
| Level cut-scenes | Win only | C | |
| Title / story pages | different | C | DOS: 2 story pages and a Super Solvers advert, 30 s waits; Win: 4 pages with narration |
| Sign-in | different | C | DOS 9 players, Win 7; both sort by total and show Trainee/stars |
| Saved game | different | **G** | DOS: explicit "Continue last game?"; "Start Over" abandons the game; Win: the climb is always resumed |

### 8.2 Rules

| Rule | Status | Impact | DOS vs Windows |
|---|---|---|---|
| Rank thresholds 0/5/25/70/115/170/230/300, tier = rank/2 | same | – | |
| Treasures per level 2/3/4/5 by tier | same | – | |
| Clue structure: key matches 3 clue words, treasures 2, decoys 1 | same | – | DOS generates it, Win reads it |
| Clue word choice | different | **G** | DOS: 3 schemes R(3) over picture attributes, revealed in random order; Win: fixed attribute masks, fixed order count/size/shape/noun |
| Decoys | same in effect | – | empty in both |
| Searching | different | **G** | DOS: ↓ drops a coin on the hiding place in front (the neighbouring cell near a cell edge); no place there, or a place already searched, means the coin is lost, and searched places look unchanged; search length is frame-rate dependent; Win: Enter tosses at the spot in the player's third, 13 ticks, every targeted spot blinks |
| Starting coins | different | **G** | DOS 5, Win 10 |
| Coins and nets carried between games | same | – | |
| Net cost | different | **G** | DOS: a swing is free; each catch uses a net (catches of the same tick share one); 0 nets = torn net, catches nothing; Win: every swing costs a net |
| Net shop | different | **G** | DOS: one shop type (2 entrances), 4 coins → **+5 nets**; Win: 3 shops, 4 coins → +10 nets |
| Coin sources | different | **G** | DOS: elf +1, quiz +2/+1/0, **coin creature +2** (a coin with legs, appears when coins < 4 and nets < 5), **bonus spot +5** once per game; Win: elf +1, quiz +2/+1/0, **rescue coin pile +10** when broke |
| Coin losses | same idea | – | elf dust (thrower) −1, castle creature −1 |
| Elves | different | **G** | DOS 9/11/13/15 by tier (7 on slow CPUs), 4 types incl. coin elf; Win 6/8/10/12; both have 3 clue carriers and 0/2/4/6 throwers by tier |
| Clue carriers | different | **G** | DOS: as many carriers as clue words still hidden on the level; Win: carriers for slots that still have questions |
| Net catch geometry | different | **G** | DOS: catch zone 30–72 px in front with a dodge edge (escape chance 1/64); Win: `\|x − netX\| ≤ 30` during steps 2–7 |
| Elf dust | similar | **G** | DOS: hit = within ±10 bytes (20 px) on the path line, not jumping (netting does not protect); the dust returns only with a coin; Win: overlap test, not jumping |
| Quiz tiers | almost same | **G** (small) | DOS 0–62, 63–179, 180–302, 303–359, then 123–359 (360 questions); Win 0–62, 63–178, 179–301, 302–358, then all (359) |
| Quiz promotion | different | **G** | DOS at ≥ 70 % right first time; Win ≥ 80 % |
| Quiz deal | similar | – | 9 questions by status in both |
| Missed questions on the next level | different | **G** | DOS: slots reordered, missed first; Win: copied into the next level's slot ("broken swap") |
| Quiz reward +2/+1/0, choice 0 correct, wrong answers greyed | same | – | |
| Castle layout R(tier+1) | same | – | |
| Castle creature chance | different | **G** | DOS 0, 1/512, 1/256, 1/64 per aligned tick; Win 0/5/10/20 % per tick |
| Castle climb | different | **G** | DOS: castle wall is part of the scrolling world; Win: separate 3-screen ladder scene |
| Champion at 300 | same | – | crown sequence in both |
| Jumping | same idea | **G** | DOS has jumps over gaps (Home/PgUp/diagonals); Win jumps exist (elf dust test) |
| Ladders, the geyser hole, breaking rope bridges (tier ≥ 2), cave and tunnel doors on the paths | DOS only | **G** | Win has ladders only in the castle climb |
| Gates need the level's key | same | – | |
| New Player Hints | different | C/G | DOS: 10 armed hints (Flutter, no voice), on while total < 5; Win: 33 events, spoken |
| Help topics | different | C | |
| Music/Sound options | different | C | DOS Music, Sound; Win Music & Sound, Voice, New Player Messages |
| Randomness | different | **G** (replays) | DOS seeds once per new game, `rand() % n` and bit masks; Win seeds per climb, `rand()*n/0x8000` |

### 8.3 Technical

| Area | Status | Notes |
|---|---|---|
| Resolution / colours | different (C) | DOS 320×200, 16 colours (CGA 4); Win 512×384, 256 colours |
| Timing | different (**G**) | DOS: logic at up to 47.3 Hz in the timer interrupt, at most one step per drawn frame, frames free-running (game slows down on slow PCs; search length depends on the frame rate); Win: fixed 10 Hz room ticks |
| Speech | Win only (C) | |
| Music/effects | different (C) | DOS: speaker/Tandy/AdLib/PS/1 tunes; Win: MIDI + wave effects |
| Mouse | same idea (C) | both allow mouse walking; DOS also nets/drops by mouse zones |
| Save file | different (C) | DOS 3 + 9×360 bytes; Win `USR\x1A` + 7×1583 bytes |

**Verdict.** The DOS mountain plays noticeably differently, not just at another resolution: the
world is a long scrolling path with ladders, gaps and tunnels instead of rooms; hiding places are
spread along it (at most one per 128-px cell, fewer on the upper levels); nets wear out per catch rather than per swing; coins come from coin
creatures and a bonus spot; the shop gives half as many nets; per-level progress persists and the
player can go back down; the quiz promotes earlier. The scoring frame (ranks, treasures per level,
clue logic, quiz rewards, castle layout choice) is the same, which shows the Windows design was
derived from this game.

---

## 9. Sizing the logic and reuse of the Windows port

DOS game logic to port (from §1.1, excluding RTL, low-level graphics, AdLib/sound drivers and disk
code): about **400 functions / 10 000 decompiled lines**. Breakdown by area: world/terrain cells
and links 105 funcs / 2 350 lines; player and end sequence 69 / 1 260; elves 53 / 1 100; UI
(menus, dialogs, help, text) 30 / 860; sign-in and users 39 / 930; quiz and clues 22 / 660; layout
11 / 360; main loop, HUD, scrolling 16 / 750; animation scripts and sprites 11 / 330; hints and
messages 6 / 280; search/treasures 4 / 280; input 10 / 380; title 12 / 250.

Windows port `src/game/**` is about 10 900 lines. Reuse [I, by comparing the specs]:

| Windows module | Reuse for DOS | Why |
|---|---|---|
| `rtl` Borland rand | **as is** | Turbo C 2.0 `rand` is the same LCG; only the range mapping differs (`%`, masks) |
| `rules.cpp` rank/tier, coin/net counters, quiz reward | **adapt (small)** | same thresholds and rewards; different net cost, shop, coin sources |
| `quiz.cpp` deal/tier/judge | **adapt** | same 9-slot deal by status and +2/+1/0; new: 70 % promotion, 360-question tiers, reorder-on-level-change, PRG_DICT text decompression, no voice |
| `quiz_screen.cpp` | **rewrite UI** | different screen; logic shape similar |
| `signin.cpp` | **adapt** | same flow; 9×360-byte records, "Continue last game?" |
| `save/users_file.cpp` | **new format** | different record; same "rewrite whole file" model |
| `layout.cpp` | **new** | DOS generates hiding places from picture attributes instead of reading layout records |
| `room/*` (room loop, room scene, player, elves, HUD) | **new** | scrolling cell world, terrain masks, 47 Hz animation-script-driven movement, different elf state machine |
| `scenes/castle_climb.cpp` | **new** (rules partly reusable) | castle wall is part of the world; layout choice same, creature chance different |
| `scenes/celebration.cpp`, `level_cutscene.cpp`, hall | **new** | chest/treasure-room end sequence; no cut-scenes |
| `title.cpp`, `hint_popup.cpp` | **new/adapt** | different pages; Flutter hints instead of speech |
| `game_main.cpp` state machine | **adapt** | similar top-level flow (title → sign-in → play → end → next) |
| engine layers (`gfx`, `text`, `anim`, `audio`, `input`, `ui`) | **new back ends** | 4-bpp 320×200 tiles+sprites, DOS font, animation scripts (not DAM), tune sequencer (not MIDI) |

Roughly a fifth of `src/game/**` (rules, quiz logic, sign-in flow, top-level state machine, RNG)
carries over with adaptation; the room/world simulation, elves, player, layout, castle, end
sequence and all asset/engine back ends are new work.

---

## 10. Open questions

1. ~~Terrain-mask bits 0x400, 0x1000 and the six handlers that did not decompile~~ — answered:
   0x400 = camera mode, 0x1000 = fall (§4.1, `code_world.md` §4.4); the six handlers decompile since
   wave 1 (§1.2) and are specified in `code_world.md` §6.
2. Hints 10, 11, 12 (on your way, tunnel, ladders) have texts but no code arms them; check in a run
   whether they ever appear.
3. ~~The gap warning (4-star message)~~ — answered: the tier-dependent gaps are the
   rope bridges of type 45, which break at tier ≥ 2 (§4.4; type 41 is the geyser hole, at every tier).
4. ~~The row assignment of §4.2~~ — answered by `code_world.md` §3.3 (every region traced from the
   teleport targets, confirmed by the capture matching).
5. ~~The treasure-room animations~~ — specified in `code_player.md` §7.2–7.3 (the 53-case switch
   of `Cell_P70_TreasureRoom` 176a:0596, the pair, trio and fly displays).
6. Behaviour that depends on frame rate (search length, HUD count-up) should be measured on period
   hardware speeds before porting.
