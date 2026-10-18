# Treasure Mountain! (Windows CD, TMTWINCD.EXE v1.01): architecture synthesis

This document ties the topic specs together. It describes the whole program as one system: the game
flow from launch to exit as a state machine, the subsystems and the modules behind them, the data
structures, the game rules in exact form, which resources each scene uses, and the places where the
topic specs disagree (with a resolution and its evidence). The behaviour details stay in the topic
docs; this file gives enough to see how they fit and links to them for the rest.

Companion documents: `PORTING_NOTES.md` (how to map this onto a C++17/SDL3 engine) and `GAPS.md`
(what is still missing, prioritised).

Conventions (same as the topic docs):

* **[O]** = observed in code, bytes or decoded data. **[I]** = inferred. Claims in this document
  inherit the tag of the topic doc they come from unless re-checked here; claims re-checked during
  this synthesis say so and give the evidence.
* Code addresses are Ghidra `SSSS:OOOO` (NE segment N = selector `0x1000 + 8(N-1)`). `DS:XXXX`
  means DGROUP (`11b8`) offset XXXX = file offset `0x23c00 + XXXX` of `TMTWINCD.EXE`.
* All multi-byte values are little-endian. Coordinates are in the 512x384 logical game screen unless
  stated otherwise.
* Game text is referred to by resource id or DGROUP offset with at most a few identifying words.
* Names follow `re/names/*.tsv`, which holds exactly one canonical name per address; all docs use it.
  §7.2 lists the decisions and the retired aliases.

Topic docs (all in `re/docs/`):

| Doc | Covers |
|---|---|
| `sys_resources_saves.md` | start-up checks, INI, DLL loading, Mac Resource Manager emulation, USERS.DAT |
| `code_1000.md`, `code_11a0-11a8.md` | Borland RTL (startup, rand, setjmp, heap, EH, string) |
| `code_1008-1030.md` | WinG, WinMain/WndProc, start-up, cursors, event queue, memory |
| `code_1038-1050.md` | resource manager, file wrappers, palette, 386 blitters |
| `code_1058-1090.md` | graphics core, font blit, effects, menus, Windows dialogs, wave, MIDI |
| `code_1098-10d8.md` | hot spots, timers, text renderer, DAM animation engine, game dialog boxes |
| `code_10e0-10f0.md` | room loop, player controller, HUD/inventory, celebration, rank screen |
| `code_10f8-1148.md` | net shop, climb layout and clues, coin pile, GameMain, music zones, hints, title, menu handlers |
| `code_1150-1160.md` | elf questions (quiz), users.dat I/O, Club Sign-In |
| `code_1168-1198.md` | castle-wall climb, sfx, elves, help, level cut-scenes, treasure hall, room composition |
| `fmt_pmap.md`, `fmt_bndl_ffnt.md`, `fmt_othr_tables.md`, `fmt_othr_puzzles.md`, `fmt_sound.md` | resource formats |
| `dgroup_map.md` | every DGROUP table and variable |

---

## 1. The executable at a glance

`TMTWINCD.EXE` is a Win16 NE program built with Borland C++ 4.x, large model, ported from the
Macintosh edition. It emulates a small part of the Mac Toolbox (Resource Manager, QuickDraw-style
offscreen drawing, Memory Manager `NewPtr/DisposePtr`, an event queue with `Button()`/`GetMouse()`)
on top of Win16. **The game code contains no C++ classes** [O, every topic doc]: every game function
is a plain `cdecl far` C function working on DGROUP globals. C++ appears only in the Borland RTL
(`xmsg`, `xalloc`, `string`, `typeinfo`), and no game code calls it except through `operator new`
inside the RTL.

All data lives in resource-only DLLs: `TMT.DLL` (BNDL, FFNT, OTHR, RRGB), `TMT256.DLL` or
`TMT16.DLL` (PMAP pixmaps, 8 bpp or 4 bpp), `TM0..TM8SOUND.DLL` (SSND), plus 20 MIDI files in
`SOUNDS\`. Many game tables (rooms, words, animation tables, menus, dialog texts, voice-id tables)
live in DGROUP of the EXE itself (`dgroup_map.md`).

### 1.1 Segment → module map

One Borland source module per code segment, except segment 1000 (packed RTL). "Live" means
reachable at run time.

| Seg | NE# | Module (proposed) | Role | Doc |
|---|---|---|---|---|
| 1000 | 1 | Borland RTL (C0W, heap, stdio, sprintf, rand, setjmp) | runtime | code_1000 |
| 1008 | 2 | `wing.cpp` | WinG back buffer and identity palette | code_1008-1030 §4 |
| 1010 | 3 | `main.cpp` | WinMain, MainWndProc, app activate/deactivate | code_1008-1030 §2, §5 |
| 1018 | 4 | `init.cpp` | single instance, window, INI, system checks, DLL loading | code_1008-1030 §3, sys_resources §1-4 |
| 1020 | 5 | `cursor.cpp` | cursors, mouse state, mouse-move hook | code_1008-1030 §6.4 |
| 1028 | 6 | `event.cpp` | Mac-style event queue, input codes | code_1008-1030 §6 |
| 1030 | 7 | `memory.cpp` | NewPtr / DisposePtr / DisposePtrDeep | code_1008-1030 §7 |
| 1038 | 8 | `resource.c` | Mac Resource Manager emulation, scratch buffer | code_1038-1050 §2, sys_resources §5 |
| 1040 | 9 | `fileio.c` | file wrappers for users.dat | code_1038-1050 §3 |
| 1048 | 10 | `palette.c` | game palette, logical→physical map, colour key, pen colour | code_1038-1050 §4 |
| 1050 | 11 | `blit.asm` | 386 blitters (4/8 bpp, keyed, mirrored, RUND, planar) | code_1038-1050 §5 |
| 1058 | 12 | graphics core | framebuffer, present, dirty rects, PixMap draws, RUND codec, save-under, highlight | code_1058-1090 §4 |
| 1060 | 13 | glyph blitters (asm) | FFNT glyph draw 4/8 bpp | code_1058-1090 §5 |
| 1068 | 14 | font manager | FFNT load/select/metrics | code_1058-1090 §5 |
| 1070 | 15 | screen effects | clear, dissolve, fade stubs | code_1058-1090 §6 |
| 1078 | 16 | menu bar | Win16 menu from Mac-style MenuBarDef | code_1058-1090 §7 |
| 1080 | 17 | Windows dialogs | DialogBox runner + 4 dialog procs | code_1058-1090 §8 |
| 1088 | 18 | wave sound | waveOut single voice, speech wait | code_1058-1090 §9, fmt_sound §5 |
| 1090 | 19 | MIDI music | MCI sequencer, B/E sets, loop, pause | code_1058-1090 §10, fmt_sound §8 |
| 1098 | 20 | hot spots | 50-slot mouse hot-spot table | code_1098-10d8 §4 |
| 10a0 | 21 | wait key | `Input_WaitCode` | code_1098-10d8 §5 |
| 10a8 | 22 | timers | busy delay + 8 deadline timers | code_1098-10d8 §6 |
| 10b0 | 23 | text | word-wrapping text renderer | code_1098-10d8 §7, fmt_bndl_ffnt §4-5 |
| 10b8 | 24 | animation | DAM script engine, 16 channels | code_1098-10d8 §8, fmt_othr_tables §4 |
| 10c0 | 25 | game dialogs | DlgDesc boxes, Yes/No, pager | code_1098-10d8 §10 |
| 10c8 | 26 | help pager from BNDL | **dead** | code_1098-10d8 §11 |
| 10d0 | 27 | music pause nesting | | code_1098-10d8 §12 |
| 10d8 | 28 | modal animation player | | code_1098-10d8 §9 |
| 10e0 | 29 | `player.cpp` | room loop (`Room_Run`), player controller | code_10e0-10f0 §4 |
| 10e8 | 30 | `status.cpp` | HUD, inventory mutators, rank function | code_10e0-10f0 §5 |
| 10f0 | 31 | `rank.cpp` | end-of-climb celebration, rank screen, See Rank | code_10e0-10f0 §6 |
| 10f8 | 32 | net shop | | code_10f8-1148 §4 |
| 1100 | 33 | climb layout and clues | | code_10f8-1148 §5, fmt_othr_tables §3 |
| 1108 | 34 | room-extra hooks | three empty `retf` | code_10f8-1148 §6 |
| 1110 | 35 | rescue coin pile | | code_10f8-1148 §7 |
| 1118 | 36 | `GameMain` | start-up, restart point, climb loop | code_10f8-1148 §8 |
| 1120 | 37 | game menu install/remove | | code_10f8-1148 §9 |
| 1128 | 38 | music manager | zone→tune, toggles | code_10f8-1148 §10, fmt_sound §8.4 |
| 1130 | 39 | New Player Messages | hint queue and popup | code_10f8-1148 §11 |
| 1138 | 40 | title / story attract | | code_10f8-1148 §12 |
| 1140 | 41 | menu command handlers | About, Start Another Game, Exit, options | code_10f8-1148 §13 |
| 1148 | 42 | gate hook | **dead** (no relocation reaches it) | code_10f8-1148 §14 |
| 1150 | 43 | `quiz.cpp` | elf questions | code_1150-1160 §3, fmt_othr_puzzles |
| 1158 | 44 | `usersfile.cpp` | users.dat read/write | code_1150-1160 §4 |
| 1160 | 45 | `signin.cpp` | Club Sign-In, record load/save | code_1150-1160 §5 |
| 1168 | 46 | `climb.cpp` | castle-wall ladder climb (room 49) | code_1168-1198 §2 |
| 1170 | 47 | `sfx.cpp` | sound effects 7000+n | code_1168-1198 §3 |
| 1178 | 48 | `elves.cpp` | elf actors | code_1168-1198 §4 |
| 1180 | 49 | `help.cpp` | Help-menu topics | code_1168-1198 §5 |
| 1188 | 50 | level cut-scene, gate lock | | code_1168-1198 §6 |
| 1190 | 51 | treasure hall | | code_1168-1198 §7 |
| 1198 | 52 | room scene composition, transitions | | code_1168-1198 §8 |
| 11a0 | 53 | Borland EH engine + RTTI | runtime | code_11a0-11a8 |
| 11a8 | 54 | Borland `string` | runtime | code_11a0-11a8 |
| 11b0 | 55 | 2-byte data segment (`g_prevInstWnd`) | | code_1008-1030 §2.3 |
| 11b8 | 56 | DGROUP | data | dgroup_map |

---

## 2. Game flow from launch to exit

### 2.1 State diagram

```
 [C0W 1000:0000] -> WinMain 1010:0000
      |  previous instance? -> activate it, exit            (1018:0000)
      v
 S1 INIT_WINDOW   RegisterClass 1018:00a8, InitInstance 1018:0115
      |  CheckSystemRequirements 1018:0281 fails -> exit (dialog, or silent if WinG missing)
      v
 S2 INIT_DATA     InitGraphicsAndData 1018:0563 (buffers, DLLs, palette obj, timer 16ms, scratch)
      v
 S3 FONT          Font_Init 1068:0024 (FFNT 999)
      v
 S4 GAME_START    GameMain 1118:0000: palette RRGB 900, Anim_ResetAll, Elf_InitSystem, cursors
      v
 S5 TITLE         Title_Run 1138:007a  --Esc+Yes--> S12 SHUTDOWN
      v
 S6 RESTART_POINT r = setjmp(DS:5eda)      r==2 ------------> S12 SHUTDOWN
      |  (r==1 arrives here from "Start Another Game"; r==0 first time)
      v
 S7 SIGN_IN       reset/free, SignIn_Run(r) 1160:00ee  --Exit+Yes--> longjmp(2) -> S6 -> S12
      v
 S8 CLIMB_SETUP   srand, Scene_LoadCommon, TreasureHall_BuildRooms, (new climb: quiz deal,
      |           Layout_GenerateClimb, start room 9 or 0), clue texts, elves, treasures, music
      v
 S9 ROOM          Room_Run 10e0:007d (10 Hz) until room changes
      |   sub-states (modal, return to S9): HINT 1130:0068, ELF_QUESTION 1150:100a (+result
      |   screen), QUIT_CONFIRM 1140:0098, menu commands (See Rank, options, help, About)
      |-- room change, same level ------------------------------> S9 (Room_Present transition)
      |-- level change (through a gate) -> S10 LEVEL_CUTSCENE 1188:0000 -> S9
      |-- room == 49 -> S11 CASTLE_CLIMB 1168:0000 -> room = 0xFE
      v (room == 0xFE)
 S13 CLIMB_END    save, Climb_Celebrate 10f0:0000 (animations, rank screen), Rank_UpdateTier
      `-----------------------------------------------------> S8
 Any state with the game menu: Exit+Yes -> save -> longjmp(5eda,2) -> S6 -> S12
                               Start Another Game+Yes -> longjmp(5eda,1) -> S6 -> S7
 S12 SHUTDOWN     1118:03eb: save, free; WinMain: font, scratch, sound, GDI/WinG, FreeLibrary x11
```

### 2.2 State table

| State | Function(s) | What happens | Leaves to |
|---|---|---|---|
| S0 start | `1000:0000` C0W | `_ExceptInit`, InitTask, clear BSS `3880..66a1`, `#pragma startup` (`_setupio`, `_setargv0`, `_setargv`, xalloc), call WinMain | S1 |
| S1 window + checks | `1010:0000`, `1018:0000`, `1018:00a8`, `1018:0115`, `1018:0281` | single instance; window class "RMPDEClass", maximised full-screen window, game area centred (x rounded down to 4); checks: 1.85 MB memory, Windows ≥ 3.10, 386 enhanced, 8 bpp palette or 4-plane display, optional 640x480 and sound dialogs, WinG choice/load | S2, or exit (WinMain returns 0) |
| S2 init | `1018:0563` | 512x1055 offscreen (WinG DIB or GDI DDB + 4 bpp buffer), `TMT256.DLL`/`TMT16.DLL` by palette capability, `TMT.DLL`, 9 sound DLLs (failure posts WM_QUIT, which is swallowed), `Pal_Init`, `timeBeginPeriod(16)`, 280 000-byte scratch | S3 |
| S3 font | `1068:0024` | FFNT 999 into slot 0 | S4 |
| S4 game start | `1118:0000` head | `GetResourceInto(900, DS:3d90)`, `Pal_SetFromRGB` (one-shot), `Anim_ResetAll`, `Elf_InitSystem`, `Cursor_LoadSet({128,129,130})`, arrow | S5 |
| S5 title | `1138:007a`, `1138:043d`, `1138:0283` | music −95 (OPEN1) then −94 (OPEN2); title picture (page 0) and story pages 1–4 loop with narration 7150–7159; 60 s per page; Enter/click next page, Space skip, Esc confirm quit; then animation 2 (BNDL 401) | S6, or S12 on confirmed Esc |
| S6 restart point | `1118:004c` `setjmp(DS:5eda)` | r = 0 first time, 1 after Start Another Game, 2 after Exit | S7 (r ≠ 2), S12 (r = 2) |
| S7 sign-in | `1118:0062..00e0`, `1160:00ee` | frees per-climb state (`RoomSlots_Free`, `Treasures_Free`, `Scene_FreeCommon`, `Elf_UnloadAll`, `Anim_ResetAll`, `TreasureHall_UnloadRoom`); if r ≠ 0 black screen; removes menu; Club Sign-In (saves the previous player first); returns with a player loaded (`Users_LoadSelected`) or created (`Users_InitNewPlayer`, room = 0xFE) | S8 |
| S8 climb set-up | `1118:00e5..0257` | `srand(timeGetTime())`; clear; wait cursor; `Scene_LoadCommon`, `TreasureHall_BuildRooms`; if room = 0xFE: reset per-climb counters, `Quiz_NewRound`, `Layout_GenerateClimb`, room = (climbsDone = 0 ? 9 : 0); `Clue_BuildTexts/VoiceIds`; `Elf_SpawnForLevel(6/8/10/12)`; `Treasures_PickForLevel`; `Music_PlayZone(room.music)`; player pose from room; HUD; menu; `Room_Present(1)` | S9 |
| S9 room | `10e0:007d` (+ `10e0:0447`) | 100 ms ticks until `nextRoom ≠ room` (§2.7) | S9, S10, S11 |
| S10 level cut-scene | `1188:0000(oldLevel)` | only when the room's level byte changes (levels 0→1, 1→2); two-part animation at 7 fps, Space skips part 2 | S9 |
| S11 castle climb | `1168:0000` | room 49: walk, climb 3 ladder screens, wall creature | room := 0xFE → S13 |
| S13 climb end | `1118:0378..03e8`, `10f0:0000`, `10e8:0790` | music stop, free, menu off, `Users_SaveCurrent`, `Climb_Celebrate` (climbsDone++, total += climbPoints, music −96/−98, 2–3 animations at 4 fps, final animation, rank screen), tier update | S8 |
| S12 shutdown | `1118:03eb..0423`, `1010:00b9..` | save, arrow, free cursors and all scene resources, stop music, free quiz; WinMain frees font/scratch, clears screen, stops wave and MCI, releases GDI/WinG, FreeLibrary ×11, `timeEndPeriod(16)` | process exit |

### 2.3 Start-up and system checks (S1–S3)

Exact order and conditions are in `sys_resources_saves.md` §1–§4 and `code_1008-1030.md` §2–§3. The
points a port must decide on:

* INI is always `C:\TLCWIN\TMTWINCD\TMTWIN.INI`; keys `Win40VersionNum`, `UseWinG`, `CheckSound`,
  `CheckDisplay` (case-sensitive `"TRUE"`). None of them holds a game option.
* The two optional warnings (`TOOBADNOSOUND`, `NOTOPTIMUMDISPLAYMODE`) return −1 = quit,
  0 = continue, 1 = continue and never show again.
* Sound check: `Wave_Init` (waveOutGetNumDevs) and, only if a wave device exists, `Midi_Init`
  (MIDI mapper caps, `midimap.cfg`, B/E file set, MCI sequencer routed to the mapper).
* DLL load failures are not reported (`PostQuitMessage` is swallowed); a port must refuse to start
  with a clear message instead.

### 2.4 Title / attract (S5)

`Title_Run` (`1138:007a`, `code_10f8-1148.md` §12). Page 0 = PMAP 2100 opaque at (0,20) saved to
the back page; pages 1–4 = OTHR 20105–20108 in a colour-11 box (33,59,284,196), text at (43,67)
width 264. DAM 20100 (BNDL 400, frames slot 0..9) animates at (0,20) on layer 0 all the time. Each
page waits up to 60 s (timer 4) and plays the page's clips (`g_titleVoiceFirst/Count`, DS:141c/1424).
Input: Enter/0x4B0/0x24B0 = next page (once any key was pressed, the loop ends after page 4), Space =
end now, Esc = Yes/No `DS:2234` (Yes → return 0 → program ends without cleanup of the title), Ctrl+S
toggles sound. After the loop DAM 20101 (BNDL 401, frames after 400's) plays at (32,21) until it
ends, a key, or 60 s. Music: zone −95 (OPEN1, plays once), and when it is no longer playing, −94
(OPEN2, loops).

### 2.5 Restart point and Club Sign-In (S6–S7)

`setjmp` at `1118:004c`; `longjmp` sites: `1140:008c` (value 2, Exit) and `1140:0062` (value 1,
Start Another Game; verified from bytes during this synthesis, see §7 C7). The reset block makes the
non-local jump safe by freeing everything the interrupted climb may hold.

`SignIn_Run` (`1160:00ee`, `code_1150-1160.md` §5): allocates the 7-record table, saves the current
player if one is signed in (`name[0] != '_'`), else loads users.dat; music −94 unless already
playing; backdrop: on restart PMAP 2100, at start-up whatever the back page holds (the title
screen); panel PMAP 2180 at (27,54); then `SignIn_Step` until a name is chosen:

* list of up to 7 players (row 25 px, from y 57; name, Trainee/stars/Champion by rank, total with
  thousands commas), "Start New Player" row, keyboard (Up/Down wrap, Enter, Ctrl+R remove, Esc exit
  prompt, any printable key starts a new name) and mouse (hot spots 0x4AD/0x4AE);
* name entry box (DlgDesc `DS:221a` drawn non-modally, name box (140,182,192,36), blinking caret
  333 ms, max 8 chars, first char uppercased, duplicate check case-sensitive);
* exit prompt `DS:2234` → `Game_ExitNow` (longjmp 2).

On return the globals hold the loaded record or the new-player defaults. The screen is cleared but
not presented until the first room is drawn.

### 2.6 Climb set-up (S8)

`GameMain` climb loop (`code_10f8-1148.md` §8.2). A *climb* = one trip from the foot of the
mountain (or the treasure hall) to the castle. Per-climb random content is generated only when
`g_room == 0xFE` (new player, or the previous climb finished); otherwise the saved climb is resumed
from the record (room, spot contents, clue state, quiz slots, counters).

New climb, in order: `g_haveKey = 0, 5e61 = 0, g_gemsFound = 0, g_climbPoints = 0, g_numClues = 0`;
`Quiz_NewRound` (`1150:0327`, quiz tier promotion + 9-question deal); `Layout_GenerateClimb`
(`1100:04b5`, three distinct records from OTHR `10550 + tier`, spots written into the room table,
clue attributes from OTHR 10555); start room = 9 if `g_climbsDone (DS:65ce) == 0`, else 0;
`g_newClimb = 1`. Then always: clue texts and voice ids, elves (`6/8/10/12` by tier), treasures for
the level, music for the room's zone, player pose = the room's start pose, gems reset if new climb,
menu, slot pictures, hall pedestals, clue box mode 0, HUD, present, `Room_Present(1)`.

### 2.7 Room loop (S9) and its sub-states

`Room_Run` (`10e0:007d`, `code_10e0-10f0.md` §4.1) runs one tick every 100 ms (timer slot 7):

```
per tick:
  Timer_Set(7, 100); clear 0b9b, g_rankPromoted
  QueueHint(0); QueueHint(2); if room==9 && climbsDone>0: QueueHint(rank-dependent 0e8f[rank])
  input = inputLocked ? (flush, 0) : Input_GetCode()
  Esc (0x1B, also window close)          -> MenuCmd_Exit 1140:0098 (Yes: save, longjmp 2)
  'c' / 'C' / 0x2063                     -> Hud_ReplayClues (voice 7300+id per clue)
  if g_caughtElf: g_elfQuizResult = Quiz_AskQuestion(caughtElf + 3*level - 1); redraw = 1
  else if hint pending: Hint_Show(hint); redraw = 2 if 0
  RoomExtra_Draw (empty); TreasureHall_Update
  clip play-field; dirty tracking on; clear dirty list
  ERASE  : gate lock, fountain (room 36), elves, coin pile, player fx, player, slot reveal, DAM sprites
  UPDATE : Player_Update, Elf_UpdateAll (incl. net catch), CoinPile_Update, Anim_Step
  REBUILD: redraw==1 -> Room_DrawBackground(0); redraw==2 -> copy (0,404,512,267)->(0,20)
  net-shop sign if shopCoinsLeft changed or redraw
  DRAW   : Room_DrawForeground(0)  (order in §3.1.4)
  if redraw: present play-field, Hud_Redraw
  Anim_FlushUpdateRects; present dirty rects; spin until timer 7 expires
until nextRoom != room; then commit the entry pose, clear flags, reset clip
```

Sub-states entered from inside a tick (all modal: they run their own input loops and return):

| Sub-state | Entry | Exit | Doc |
|---|---|---|---|
| Hint popup | `g_hintPending != -1` → `Hint_Show` 1130:0068 | Enter / Esc / click / right click; voice 7100+n can be cut | code_10f8-1148 §11 |
| Elf question | `g_caughtElf != 0` (set at net step 7 by a question-carrying elf) → `Quiz_AskQuestion` 1150:100a | answer chosen (correct/wrong result screen) or Esc (nothing recorded) | code_1150-1160 §3.6 |
| Quit confirm | Esc / window close → `MenuCmd_Exit` | No → continue; Yes → free, save, longjmp 2 | code_10f8-1148 §13 |
| Menu commands | WM_COMMAND while a modal loop pumps messages → `Menu_OnCommand` 1078:0135 (ignored during resource loads) | handler returns | code_1058-1090 §7 |
| See Rank | File menu item 0 → `Menu_SeeRank` 10f0:0487 | Enter/Space/click | code_10e0-10f0 §6.3 |
| Help topics | Help menu → `1180:0117..014a` → pager `10c0:0cde` | Done / Esc / click outside | code_1168-1198 §5, code_1098-10d8 §10.6 |
| Options | `1140:0015/00cc/00f7` Yes/No dialogs | | code_10f8-1148 §13 |
| About | `1140:0000` → DlgDesc `DS:1919` | | code_10f8-1148 §13 |

Room changes are decided inside `Player_Update` (walk off an edge, door, gate arrival). The climb
loop then frees the room's pictures, handles a music-zone change (stop music, re-pick treasures,
and on a level change: unload elves, clear clues, carry missed questions, clear key and gems,
cut-scene, transition 1, respawn elves), loads the new room's pictures and pedestals, and calls
`Room_Present(g_roomTransition | 0x80)`. Music for rooms 0 and 9 starts after the transition.

### 2.8 Level gates and cut-scenes (S10)

The only links between mountain levels are the locked gates in rooms 11, 25 and 38 (spot content
0xFE). With the key, pressing Up in the gate's third makes the player auto-walk to x = 330 / 281 /
170, the lock turns (BNDL 415 frame 2), elves in the room flee and all elves become confined to
their rooms, and on arrival the room changes to 23 / 36 / 49 with transition 1. The room table has
no exit from a higher level to a lower one [O, `dgroup_map.md` §4.16], so a climb is one-way.
Leaving level 0 or 1 plays `LevelCutscene_Play(oldLevel)` (BNDL 410/412, scripts 20200+20201 or
20204+20205, backgrounds 2200/2201, 7 fps); level 2 → room 49 has no cut-scene.

### 2.9 Castle-wall climb (S11)

`ClimbScene_Run` (`code_1168-1198.md` §2): screen 0 is walked like a normal room (Space/Enter still
cost nets/coins and do nothing useful), Up at a ladder starts climbing; screens 1–3 are ladder grids
from one of `R(tier+1)` layouts; the Master of Mischief appears in windows with a per-tick chance of
0/5/10/20 % by tier and pushes the climber off an adjacent ladder (−1 coin); falling ends on a ladder
or with a landing on screen 0; climbing off the top of screen 3 ends the scene. Esc → quit dialog.
No elves, no hints from elves, no gems.

### 2.10 End of climb (S13)

Order (`1118:0378..03e8`): `Music_StopScene`, `Treasures_Free`, `Elf_UnloadAll`, `g_numClues = 0`,
`Inv_SetHaveKey(0)`, `Inv_ResetGems`, remove menu, arrow, **`Users_SaveCurrent`** (location 0xFE,
old total), `Scene_FreeCommon`, black screen, `Climb_Celebrate` (`10f0:0000`):

1. `climbsDone++`; `total += climbPoints` (saturating at 0xFFFFFFF0); rank before/after;
   promoted / champion flags; result hint queued (shown in the next room loop).
2. Music −98 (FINALE) if now champion, else −96 (THRONE).
3. PMAP 2299 background; DAM 20300 (BNDL 323) then DAM 20302 (the delivered treasure = BNDL
   `330 + (climbsDone−1) mod 43` entry 0 in frame slot 80), then DAM 20305 (BNDL 324) if champion
   else DAM 20301; then PMAP 2202 with DAM 20308 (BNDL 325). All at 4 fps at (0,20); Enter/Space skip
   the rest; Esc = quit dialog.
4. Rank screen (`10f0:054b`): PMAP 2380, player name, total, labels, sfx 22, a rising band fill
   (PMAP 2381) per rank up to the current one, 16 × 50 ms per rank; wait for Enter/Space/click.

Verified against the original (E10, per REFERENCE_CAPTURES §R5): order, 4 fps and the band fill. An
Enter/Space skip during the animations also skips the rank screen. An Exit (Esc + Yes) from these
screens saves the new total with the stale tier byte +0x09 (M7 confirmed; decision: keep).

Then `Rank_UpdateTier` (tier = rank / 2) and back to S8, which generates a new climb.

### 2.11 Saves

`Users_SaveCurrent` (`1160:0f28`) re-reads users.dat, merges the current player into its slot,
stable-sorts by total treasures (descending) and rewrites the whole file. It runs: on entering the
sign-in with a player signed in (after Start Another Game), at each climb end (before the score is
added, see §5.12), on Exit (twice: in `MenuCmd_Exit` and in the shutdown tail), and Remove Player
rewrites the file. `sys_resources_saves.md` §6.

### 2.12 Exit and Windows events

* Exit menu / Esc in a room / Esc in the climb / Esc during celebration animations → `MenuCmd_Exit`
  (Yes/No `DS:1533`) → `Scene_FreeCommon`, `Treasures_Free`, save, `longjmp(5eda, 2)` → shutdown.
* Esc on the title or the sign-in → Yes/No `DS:2234` → end (title: `Title_Run` returns 0; sign-in:
  `longjmp(2)`).
* Window close, `SC_CLOSE`, `WM_CLOSE` and `WM_QUERYENDSESSION` become event type 1, reported as
  input code 0x1B (Esc), so the game asks the player; Windows shutdown is refused.
* While the window is inactive, minimised or in a menu, `Evt_GetNextEvent` does not return: the game
  freezes; the current wave sound is cut and MIDI pauses (resumed on activation).
* **Ctrl+S** (WM_CHAR 0x13) toggles Music-and-Sound and Voice together inside the event fetch and
  interrupts speech. Verified visually (E15, per REFERENCE_CAPTURES §R7): during a hint voice it cuts
  the voice and dismisses the hint at once; on a title story page nothing changes on screen.

---

## 3. Subsystem map

### 3.1 Rendering

#### 3.1.1 Framebuffer and layers [O]

One indexed offscreen buffer, **512 × 1055**. 8 bpp (256-colour/WinG, stride 512) or packed 4 bpp
(16-colour, stride 256, high nibble = left pixel). Only rows 0..383 are ever presented.

| Rows | Name | Content |
|---|---|---|
| 0..383 | screen (layer 0) | composed frame; play-field y 20..286 (512×267), status bar y 287..383 (line at 287, panel from 288) |
| 384..767 (+0x180) | back page (layer 1) | room background + slot pictures + net-shop sign; sprites are erased by copying from here; other screens save the whole screen here |
| 788..1054 (+0x300, play-field only) | pristine (layer 2) | room background without slot pictures (used to blank a slot during the coin toss, restore sign/door) |

1055 = 768 + 20 + 267 (`code_1058-1090.md` §2.1). Clip rectangle (inclusive) `DS:0864/0866/0868/
086a` = top/bottom/left/right: play-field `{y dy+20..dy+286, x 0..511}` (`Gfx_SetPlayClip` 10e0:036b)
or full `{0..767}` (`Gfx_ResetClip` 10e0:0391).

#### 3.1.2 Palette and colour model [O]

* One palette for the whole game: RRGB 900 (256 × RGB), set once (`1048:010a`, one-shot flag). No
  palette animation, no fades (`AnimatePalette` is not imported; the fade entry points
  `1070:011c/0120` are stubs returning 1).
* 256-colour mode: PMAP pixels are translated on load to physical system-palette indices
  (`g_logToPhys` DS:4193, built with `GetNearestPaletteIndex`), so equal RGBs collapse.
* Colour key `DS:4190`: nearest to RGB(0,0,0xAA) (= logical 15 and 241) in 256-colour mode, nibble 3
  in 16-colour mode. A white-key variant exists (mirrored draw only), unused with shipped data.
* UI "pen" colours are EGA numbers 0..15 (`DS:07d8` RGB table; 16-colour device values `DS:0838`).
  `Pal_SetForeColor(n)` sets text and fill colour.
* Port: compose in logical RRGB indices end to end and treat colour (0,0,0xAA) as transparent.

#### 3.1.3 Primitives (segment 1058 over 1050)

| Primitive | Address | Clip | Dirty rect |
|---|---|---|---|
| `Gfx_BlitToWindow(x,y,w,h)` present | 1058:05bc | to 512×384 | – |
| `Gfx_AddDirtyRect`, `Gfx_FlushDirtyRects`, `Gfx_ClearDirtyRects` | 068e/0731/076f | 512×384 | list of 256 at DS:42a2, only while `g_dirtyEnabled` (DS:0862) |
| `Gfx_FillRect/FrameRect/HLine/VLine` (pen colour) | 097f/0b31/090f/0947 | GDI clip only | no |
| `Gfx_CopyOffscreenRect(sx,sy,w,h,dx,dy)` | 0b6a | none | yes (destination) |
| `PixMap_DrawOpaque(x,y,pm)` | 0c78 | none | no |
| `PixMap_DrawSprite(x,y,pm,flip)` keyed, optional mirror | 1320 (→0d43/114c) | clip rect | yes |
| `PixMap_DrawOpaqueOrFlippedWhiteKey(x,y,pm,f)` | 0cf3 | f=0 none | f≠0 yes |
| `Gfx_SaveRect` / `Gfx_RestoreRect` (save-under) | 1643/1695 | none | no |
| `Gfx_HighlightRect` (reading highlight LUT) | 19b2 | none | no |
| `Screen_DissolveCopy` 16-pass 2×2 dissolve | 1070:002d | none | queues if enabled |
| glyph draw | 1060:0000/01ef via 1068:00f6 | none | no |

#### 3.1.4 Room composition and per-tick draw order [O]

`Room_DrawBackground(yoff)` (`1198:0234`): background PMAP (room +0x12) opaque at (0,404) →
copy to (0,788) (pristine) → net-shop sign into layer 1 (rooms 13/24/36) → slot pictures at
x {26,197,368}, layer-1 y 474 (screen y 90) → if yoff = 0 copy layer 1 play-field to the screen.

`Room_DrawForeground(yoff)` (`1198:0305`), back to front:
1. slot reveal: on coin-toss steps with bit 1 set (2,3,6,7,10,11) the targeted slot picture is
   blanked from the pristine layer and the treasure (BNDL `2dbc[gemsFound]` frame 0) or key (HUD art
   frame 2·level) is drawn at x {49,220,391}, y 152;
2. net-shop door state machine (`NetShop_Tick`, yoff = 0 only);
3. gate lock (rooms 11/25/38);
4. treasure hall pedestals (zone 0) + DAM layer 0x4D;
5. DAM layer 0x14;
6. fountain animation (room 36, BNDL 328, one frame per tick);
7. elves;
8. coin pile;
9. player;
10. player effects sprite (coin, sparkle, POOF);
11. DAM layer 0x57;
12. DAM layer 0x00.

Erase happens before update in the same tick, by restoring each sprite's rectangle from layer 1
(`Gfx_RestoreFromLayer` 10e0:03a6, `Anim_EraseSprites`).

#### 3.1.5 Presentation and transitions [O]

* Presentation is rectangle-based: callers present explicit rectangles or flush the dirty list.
  WM_PAINT re-presents the invalid part; borders outside 512×384 are black.
* `Room_Present(t)` (`1198:0527`): t&0x7F = 0 instant redraw; 1 "fade" (with the stubs: a black
  fill and present first for room 9 and zone 0, then redraw); 2 dissolve from layer 1; 3–6 cut.
  Bit 7 is computed and never read. **With shipped data only 0 and 1 occur** [O this synthesis:
  `g_roomTransition` (DS:5e2a) is written only from room-table type bytes, which are all 0 or 1
  (`dgroup_map.md` §4.16), and the constant 1 (`10e0:09dd`, `1118:02d7`)], so the dissolve is never
  seen in normal play.
* Type-0 exits keep the player's pose and shift x by ±512 (scroll continuity); type-1/2 transitions
  use the destination room's start pose.

### 3.2 Animation (DAM scripts)

`code_1098-10d8.md` §8–9, `fmt_othr_tables.md` §4. 16 channels (`DS:4f82`, 29 bytes each), a
100-entry frame table `g_animFrames` (DS:5152, far PixMap pointers), per-tick sprite queue, erase
list and update-rect list. Scripts: `"DAM\x1A"` + opcodes `FD dx dy frame mode` (sprite),
`FE` (end of frame), `FF` (end), `FA off c` (loop, count 0 = forever), `FC pad n` (sfx 7000+n),
`FB pad` (stop sfx), `F9` (update rect). Layers used: 0x00 (title, cut-scenes, celebration), 0x4D
(treasure hall), 0x14 and 0x57 (drawn, never used). Frame slots: animation bundles from 0; treasure
hall pedestals at 20/30/…/70; slot 80 = delivered treasure. Players:

| Player | Tick | Used by |
|---|---|---|
| Room loop | 100 ms (timer 7) | treasure hall pedestals |
| Title wait loop `1138:0283` | 100 ms | title animations |
| `Anim_PlayModal` `10d8:0082` | `1000/fps` ms | celebration (4 fps), level cut-scenes (7 fps) |

Separate hand-coded animation systems (not DAM): the player controller (frame tables at DS:0ba0..
0e89), elves (behaviour scripts DS:2acb.., `code_1168-1198.md` §4), the castle climb (DS:2950..),
the net-shop door (state machine), the fountain (frame counter), the rank-screen band fill.

### 3.3 Input

`code_1008-1030.md` §5–6, `code_1098-10d8.md` §4. The window procedure stores one pending event
(`DS:3d82`) and held-arrow flags (`06ee/06f0/06f2/06f4`), mouse position in game coordinates
(`06f6/06f8`) and left-button state (`06e3`). `Input_GetCode` (`1028:0131`) turns the first event of
each message drain into a 16-bit input code:

| Source | Code |
|---|---|
| WM_CHAR | the character (0x0D Enter, 0x1B Esc, 0x20 Space, 0x08 Backspace, 0x12 Ctrl+R, 0x13 Ctrl+S, letters) |
| WM_KEYDOWN PgUp/PgDn/End/Home/arrows/Ins/Del | `0x100 + VK` (= 0x100 OR VK) (0x125 Left, 0x126 Up, 0x127 Right, 0x128 Down) |
| close / quit request | 0x1B |
| mouse over hot spot `id` | `id + 0x8000` left down, `id` left up, `id + 0x4000` right down, `id + 0x2000` right up; 0 when no hot spot |

Hot spots: 50 slots (`DS:4b7a`), lowest free slot on insert, update in place on the same id, hit test
from slot 49 down (highest slot wins, half-open rectangles). Ids double as key codes (0x0D = click
acts as Enter). Movement uses the held flags plus "mouse-directed walking" (hot spot 0x600 latched
while the button is held). The mouse-move hook (`10e0:0000`) shows the left/right walking cursors
(EXE cursors 129/130) over the play-field and the arrow elsewhere or when forced (`SetCursorForced`,
40 call sites around modal screens). Input arriving during a resource load or a busy wait is
discarded (`Input_Flush` after every load).

### 3.4 Timing

* `timeBeginPeriod(16)` at start-up; all timing from `timeGetTime` (`10a8`, 8 deadline slots
  `DS:4f62`): slot 7 = frame timer (100 ms in room loop, title and climb; `1000/fps` in
  `Anim_PlayModal`), slot 4 = 60 s title page timeout / 50 ms rank band step, slot 0 = 333 ms caret
  blink, slot 6 = waited on by the text renderer but never armed (no typewriter effect).
* Game logic runs at exactly one update per 100 ms tick; the rest of the tick is spent re-presenting
  dirty rects. There is no frame skipping or catch-up: a slow tick simply takes longer.
* Busy waits without message pumping: `Delay_ms` (button flash 4 × 62 ms, quiz read-aloud 250/
  500 ms pauses), waiting for sfx 12/8 before praise speech.

### 3.5 Audio

`fmt_sound.md`. One waveOut voice shared by effects and speech (a new sound cuts the current one);
music is a separate MCI sequencer stream. Settings: `g_soundOn` (DS:0b96, music + effects) and
`g_voiceOn` (DS:0b97, speech), per player.

| Kind | Ids | Played by |
|---|---|---|
| sound effects | SSND 7000+n (n 0..41) | `Sfx_Play(n)` 1170:0000 (copy, async), DAM opcode FC, elf flee table |
| hint narration | SSND 7100+e | `Hint_WaitDismiss` 1130:0166 (blocking, interruptible) |
| title narration | SSND 7150..7159 | title wait loop (polled) |
| quiz narration | SSND 4000..6615 | `Quiz_SpeakLine/Answer/All` (blocking, interruptible) |
| praise / sorry / clue words | SSND 7300/7400/7500 + k | result screens; clue replay uses 7300 + k |
| music | MIDI 9000..9009 → `SOUNDS\{B,E}*.MID` | `Music_PlayZone(zone)` 1128:0000 |

Music zones: 0 hall (CLUBHSE), 1 level 0 (LEVEL1), 3 level 1 (LEVEL2), 5 level 2 (LEVEL3), 7 castle
climb (CASTLE), −95 OPEN1 (once), −94 OPEN2, −96 THRONE, −98 FINALE; −97 TITLE and zones 2/4/6 are
never used. Every tune except OPEN1 loops from its start. Music stops for elf questions and hints and
restarts from the beginning afterwards; it pauses (position saved, 16-bit) for modal game dialogs,
Windows dialogs, menus and deactivation.

### 3.6 Resources

`sys_resources_saves.md` §5, `code_1038-1050.md` §2. Type from id range (`<900` BNDL, `900–949`
RRGB, `950–999` FFNT, `1000–3999` PMAP, `4000–9999` SSND, `≥10000` OTHR); DLL from the type's first
letter and the id (sound ids by range: 7000–7099 TM0, 7100–7999 TM1, 4000+400k TM(2+k)). Lookup via
each DLL's type-15 RT_NAMETABLE. No cache: every `GetResource` copies the NE-rounded resource into a
new block; BNDLs are expanded recursively into far-pointer arrays; a 280 000-byte scratch buffer is
reused for backgrounds and tables; speech is played in place (`Res_LockInPlace`). PMAPs are colour-
remapped on load in 256-colour mode.

### 3.7 Save data and settings

`users.dat` = `"USR\x1A"` + 7 × 1583-byte records (layout §4.3, `sys_resources_saves.md` §6). The
INI holds only start-up check flags. Music/Sound, Voice and New Player Messages are per player.

### 3.8 Text

FFNT 999: 256 glyphs, 17 px high, 1 bpp, proportional, Mac-Roman-like with key-cap glyphs at
0x80–0x95. `Text_Render` (`10b0:00b5`) implements word wrap at spaces, CR/LF line breaks, TAB stops,
`FE` (back to margin), `FF 01 n FF` substitution, `FF 02 FF` underline (with `_` blanks), `FF 03 c FF`
colour c−1, `FF 04` tab stops, `FF 05 FF` centring toggle, `FF 06 … FF` comment; measure and draw
share one routine. No clipping. `code_1098-10d8.md` §7, `fmt_bndl_ffnt.md` §3–5.

### 3.9 UI widgets

| Widget | Module | Notes |
|---|---|---|
| Windows dialogs (8 templates in the EXE) | 1080, 1018 | start-up warnings only |
| Windows menu bar (File/Options/Help) | 1078, 1120 | only while a climb is played; disabled during modal sequences |
| Game message box / Yes/No (DlgDesc) | 10c0:0ae5 | drawn with the game font, save-under, hot spots 0x44C..0x44F, music paused |
| Multi-page help pager (PagerDesc) | 10c0:0cde | Help menu |
| Non-modal box (name entry) | 10c0:0764 | sign-in |
| HUD / status bar | 10e8 | clue box (3 modes), key icon, coins, nets, gem icons, treasure counter |
| Hint popup | 1130 | speech bubble PMAP 2011 + butterfly PMAP 2010 |
| Quiz screen | 1150 | scroll PMAP 2000, answer rows, speaker button PMAP 2001 |
| Sign-in list | 1160 | wood panel BNDL 322 |
| Rank screen | 10f0 | PMAP 2380/2381 |

### 3.10 Memory

`NewPtr` = `GlobalAlloc(GMEM_MOVEABLE)` + lock (one compact-and-retry, out-of-memory box), free
with `DisposePtr` / recursive `DisposePtrDeep` for BNDL trees. Resource ownership is a strict tree;
the port can use RAII handles.

---

## 4. Classes and key data structures

### 4.1 What exists in the binary

* Game code: no classes, no vtables, no `this` pointers, no game static constructors [O]. The
  natural units are the modules of §1.1 and their DGROUP globals.
* Borland RTL classes only: `xmsg`, `xalloc`, `string` / `TStringRef` / `TSubString`, `typeinfo`,
  `Bad_cast`, `Bad_typeid`; no game code uses them, nothing in DGROUP is a vtable
  (`code_11a0-11a8.md`, `dgroup_map.md` §6).
* Non-local control flow: `setjmp/longjmp` on `DS:5eda` (restart = 1, exit = 2).

A suggested class model for the port is in `PORTING_NOTES.md` §2.

### 4.2 Structure index

| Structure | Where | Size | Layout summary | Doc |
|---|---|---|---|---|
| `RoomDef` | DS:23d8 [50] | 0x1C | +0 startX, +2 startY, +4 startFrame, +5 facingLeft, +6/+7/+8 exits L/R/door, +A/+B/+C transition types, +E doorX, +12 bg PMAP, +14 slotKind[3], +17 slotContent[3], +1A level, +1B music zone | dgroup_map §4.16 |
| layout record | OTHR 10550+tier | 0x26 | +0 clue mask, +1 key object kind, +2 kind[18], +14 content[18] (u16 count = 500 first) | fmt_othr_tables §3 |
| object attributes | OTHR 10555 | 4 × 112 | {count 1..7, size 0..4, shape 0..4, noun 1..15} | fmt_othr_tables §2 |
| `QuizSlot` | DS:5f3a [9] | 8 | +0 i16 q (−1 none), +2 perm[3], +5 tried[3] | code_1150-1160 §3.1 |
| user record | users.dat | 0x62F | see §4.3 | sys_resources §6.3 |
| `Hotspot` | DS:4b7a [50] | 10 | id, x, y, w, h | code_1098-10d8 §2.1 |
| `GameEvent` | stack | 6 | what, p1, p2 | code_1008-1030 §8 |
| `AnimChannel` | DS:4f82 [16] | 0x1D | layer, stop, flags, x, y, frameBase, loop[2], user0/1, onEnd, cursor, base | code_1098-10d8 §2.3 |
| `AnimSprite` | DS:5666 [100] | 14 | layer, noDirty, x, y, mode, pixmap, sflags, frame | code_1098-10d8 §2.4 |
| `AnimEraseRect` / `AnimUpdateRect` | DS:52e2 / DS:5bde | 9 / 6 | | code_1098-10d8 §2.5-2.6 |
| `DlgDesc` | static in DGROUP | 0x1A | type, title, footer, body, defaultButton, x, y, width, lines, picture, picDx/Dy | code_1098-10d8 §2.8 |
| `PagerDesc` | DS:2cfe | 0x1E | title, footer, texts, count, x, y, width, lines, pics, picDx, picDy | code_1098-10d8 §2.9 |
| `MenuBarDef` / `MenuPopupDef` | DS:13bf / DS:1383 | 6 / 0x14 | count+popups / title, ?, itemCount, items, handlers, ?, ? | code_1058-1090 §2.7 |
| `ElfAnimSet` | DS:6228 [7] | 0x32 | frameCount, 1, behaviour, frames[10], bndl | code_1168-1198 §4.1 |
| `Elf` | DS:6386 [32] | 0x12 | set, behaviour, step, x, y, facing, room, inNet, hasItem | code_1168-1198 §4.1 |
| `PixMap` | memory / PMAP | 4 + data | u16 w, u16 h, pixels (4bpp rows (w+1)/2, 8bpp raw or "RUND"+RLE) | fmt_pmap §3-4 |
| expanded BNDL | memory | 2× rounded | "BNDL", u16 n, far ptr[n], u32 size[n] | sys_resources §5.6 |
| FFNT | TMT.DLL 999 | | u8 0, u8 last, u8 height, width[n], offLo[n], offHi[n], bits | code_1058-1090 §2.5 |
| SSND | sound DLLs | 12 + n | u32 0, u32 rate, u32 count, u8 pcm[] | fmt_sound §3.2 |
| DAM script | OTHR 20000.. | | "DAM\x1A" + opcodes | code_1098-10d8 §8.1 |
| dirty rect list | DS:42a2 [256] | 8 | x, y, w, h | code_1058-1090 §2.3 |
| timers | DS:4f62 [8] | 4 | absolute `timeGetTime` deadlines | code_1098-10d8 §2.2 |

### 4.3 The user record (persisted game state)

Offsets in the 0x62F-byte record and the DGROUP variable each one is copied from/to [O]:

| Off | Size | Field | DS var |
|---|---|---|---|
| 0x000 | 9 | name (NUL-terminated, ≤ 8) | 224e |
| 0x009 | 1 | rank tier (rank/2) | 5e58 |
| 0x00A | 4 | total treasures (sort key) | 5e62 |
| 0x00E / 0x00F / 0x010 | 1 each | Music&Sound on / Voice on / New Player Messages on | 0b96 / 0b97 / 5f00 |
| 0x011 | 4 | unseen once-only hint mask | 5f01 |
| 0x015 | 1 | have key | 5e59 |
| 0x016 / 0x018 | 2 each | coins / nets | 5e5a / 5e5c |
| 0x01A | 1 | gems found this level | 5e5e |
| 0x01B | 2 | treasures this climb | 5e5f |
| 0x01D | 2 | coins still owed at the net shop | 5e7e |
| 0x01F | 1 | clue words earned this level | 1078 |
| 0x020 / 0x023 / 0x02C | 3 / 9 / 3 | clue mask / clue values / plural flag per level | 5ea8 / 5eab / 5eb4 |
| 0x02F | 1 | quiz tier 0..4 | 5f82 |
| 0x030 | 359 | question history (0..3, bit 7 = dealt) | 5f83 |
| 0x197 | 72 | 9 × QuizSlot | 5f3a |
| 0x1DF | 1 | current room (0xFE = start a new climb) | 5cca |
| 0x1E0 | 800 | unused | – |
| 0x500 / 0x596 | 150 each | spot kind / spot content, 50 rooms × 3 | 23ec / 23ef + 28·room |
| 0x62C | 1 | unused | – |
| 0x62D | 2 | climbs completed (= treasures won) | 65ce |

Not saved: elf positions (elves are re-placed randomly on every session start), `g_keyKind`, the
treasure picture list (re-picked), hint pending state, coin pile state.

---
## 5. Game rules (exact)

Everything in this section is [O] in the cited topic doc unless marked. "R(n)" means Borland
`rand()*n/0x8000` with 32-bit signed arithmetic, i.e. 0..n−1 (`code_1000.md` §5).

### 5.1 Two difficulty scales

| Scale | Variable | Range | Changes when | Controls |
|---|---|---|---|---|
| Rank tier | `g_rankTier` DS:5e58 (record +0x09) | 0..3 = rank / 2 | after each climb (`Rank_UpdateTier` 10e8:0790) | everything in the table below |
| Quiz tier | `g_quizTier` DS:5f82 (record +0x2F) | 0..4 | at each new climb, at most +1, when ≥ 80 % of the current tier's questions were answered right first time | which questions are dealt (§5.6) |

| Effect of rank tier t | t = 0 | 1 | 2 | 3 | Source |
|---|---|---|---|---|---|
| Layout resource | OTHR 10550 | 10551 | 10552 | 10553 | 1100:04b5 |
| Spots per level: empty (0) / decoy (1) / treasure (2) / key (3) | 10/5/2/1 | 8/6/3/1 | 6/7/4/1 | 4/8/5/1 | fmt_othr_tables §3.2 |
| Treasures per level (gem icons) `DS:0ec3` | 2 | 3 | 4 | 5 | 10e8:03cb, 1198:0103 |
| Elves per level `DS:128e` | 6 | 8 | 10 | 12 | 1118 climb set-up |
| Castle-climb ladder layout | layout 0 | R(2) | R(3) | R(4) | 1168:0047 |
| Wall-creature spawn chance per tick `DS:2aaa` | 0 ‰ | 50 ‰ | 100 ‰ | 200 ‰ | 1168:1143 |

### 5.2 The mountain

* 50 rooms (`RoomDef`): 0..8 treasure hall ("clubhouse", zone 0, level 0), 9..22 level 0 (zone 1),
  23..35 level 1 (zone 3), 36..48 level 2 (zone 5), 49 castle wall (level 3, zone 7).
* Each level is a ring (9→…→16→9, 23→…→29→23, 36→…→42→36) plus a side passage reached through two
  doors (11→17, 15→22; 25→30, 27→35; 38→43, 42→48), passage rooms 17→18→22 etc. Rooms 19–21, 32–34,
  45–47 have exits but no entry (unreachable [I]).
* Room 0's door leads to room 9 and back. The hall's length is rebuilt at every climb set-up:
  `rooms = climbsDone < 2 ? 2 : min(9, (climbsDone−2)/6 + 2)`; room 0 right exit = 1, rooms
  1..rooms−2 chain left/right with background 1037, the last room has background 1038 and no right
  exit (1190:0000).
* Start of a climb: room 9 if the player has never finished a climb, else room 0.
* Gates (locked, need the level's key): rooms 11, 25, 38 → 23, 36, 49. Net shops: rooms 13, 24, 36.
  The fountain obstacle: room 36 (x zone 176..326 must be jumped over).
* Movement limits: a wall (exit 0xFF) clamps x ≥ 75 on the left and x + w ≤ 437 on the right; an
  exit is taken when x < −40 (left) or x > 482 (right); a door is entered when `x < doorX < x + w`,
  its type is 1 or 2 and Up is pressed.

### 5.3 Hiding places ("spots")

* Each room has 3 spot slots, one per third of the screen (x 0–169, 170–339, 340–511). The picture
  (PMAP `1400 + kind`, 118×133) is drawn at x {26, 197, 368}, screen y 90.
* At climb set-up three distinct layout records (one per level, R(500) each, redrawn on collision)
  fill 18 spots per level in fixed room slots (`code_10f8-1148.md` §3.3 table) plus the gate slots
  (kind = content = 0xFE: room 11 slots 1,2; 25 slots 1,2; 38 slots 0,1) and the net-shop slot
  (0xFD: room 13 slot 1, 24 slot 0, 36 slot 2). All other slots are 0xFF.
* Contents: 0 nothing, 1 decoy (behaves exactly like 0 in the Windows EXE), 2 treasure, 3 key,
  0xFD shop coin slot, 0xFE gate. Exactly one key per level, under the spot whose picture equals the
  record's key kind.
* **Searching** = Enter (input code 0x0D; `Player_Update` tests 0x0D and 0x20 by exact match and no
  hot spot with id 0x0D or 0x20 exists during `Room_Run`, so in normal rooms the mouse can only steer
  walking [O: 10e0:0447, hot-spot registry in `code_1098-10d8.md` §4.1]): costs 1 coin. With no coins
  nothing is thrown; hint 6 (nets > 0) or 7 is queued. The toss takes 13 ticks (1.3 s). The target
  slot is fixed at step 1: `slot = clamp((facingLeft ? x − 41 : x + 78) / 170, 0, 2)` (C division).
  On steps 2, 3, 6, 7, 10, 11 the picture blinks off and a treasure or key underneath shows through.
  These numbers are the **post-increment** values of `g_coinStep` (the draw pass runs after
  `Player_Update` has incremented it), i.e. the ticks that process steps 1, 2, 5, 6, 9, 10; the first
  blink is visible together with toss frame 46. **Every** targeted spot that has a picture blinks,
  including empty ones (content 0) [O: `REFERENCE_CAPTURES.md` §4.6/§4.11].
  At step 13: content 2 → treasure collected, content := 0; 3 → key, content := 0; 0xFD →
  `shopCoinsLeft--`; anything else → the coin is lost. Sounds: step 4 sfx 34, step 7 sfx 0, step 8
  sfx 6.

### 5.4 Clue words

* Each layout record names the key object by exactly 3 attributes, always including the noun:
  mask 0x0B (count, shape, noun), 0x0D (count, size, noun) or 0x07 (size, shape, noun). Only the key
  object matches all three among the level's 18 pictures (data invariant, `fmt_othr_tables.md` §3.2).
* Words are taken in the fixed order count, size, shape, noun; the noun is plural when the key
  object's count > 1 (even if count is not named). Word tables and voice ids: count v → word
  `DS:10f9[v]`, voice 37+v; size → `DS:1119[v]`, 44+v; shape → `DS:112d[v]`, 48+v; noun →
  `DS:1079[v]` / plural `DS:10b9[v]`, 7+v / 22+v.
* The player earns the words one at a time by answering an elf question correctly
  (`g_numClues++`, max 3 per level; hint 3 at the second). The HUD shows the earned words at
  (17, 315 + 19k); `c`/`C` speaks them (SSND 7300 + voice id, narrator bank 1). The praise screen
  shows and speaks the newly earned word with the question's narrator bank (7300/7400/7500 + id).
* Clue words reset on every level change.

### 5.5 Elves

* Spawned at every climb set-up (including a resumed climb) and on every level change:
  `count = 6/8/10/12` by tier, none in room 49. Elf i: i < 3 → question carrier if
  `g_quizSlots[3·level + i].q != −1` (set 3 "carrying", BNDL 315; else set 0, BNDL 316);
  3 ≤ i < 6 → hopper (set 2, BNDL 312); i ≥ 6 → thrower (set 1, BNDL 317; throwing set 5, BNDL 314).
* Placement: x = 30 + R(250), y = 175, facing R(2); room: start at 11/25/38 for level 0/1/2, then
  15 steps, each to the right exit if R(3) == 0 else the left exit.
* Wandering: per-tick scripts (8 frames × 2 ticks, or 5 × 2 for hoppers) moving 40–64 px per loop. At
  the end of a loop: a thrower in the player's room facing the middle (x > 319 facing left or x < 193
  facing right) starts a throw if R(10) < 8 and nobody is throwing; otherwise it turns with
  probability 1/30; other elves re-pick their behaviour with probability 1/20, then turn with 1/30.
  At x + 21 ≥ 512 or x < −21 they change room (or turn at a wall or when confined); after a room
  change, if not in the player's room, they may take that room's door with probability 1/3 (same
  level, not the hall). They bounce off the room-36 fountain (x 124 / 336).
* **Net catch**: during net-swing steps 2..7, an elf in the player's room with `|x − netX| ≤ 30`
  (netX = x + 99 facing right, x − 98 facing left, set at step 1) is caught if no elf is already in the
  net: **+1 coin**. A carrier triggers its question at net step 7 (`g_caughtElf = i + 1`); the
  question runs on the next tick. At net step 9 the elf is released: if it carried no question or
  the answer was correct it flees (behaviour 4: runs off to the next room, sfx 1/37); after a wrong
  answer or Esc it keeps its question and can be caught again.
* **Thrower**: wind-up 4 ticks, throw 4 ticks (sparkle grows, sfx 31), then the sparkle flies 20 px
  per tick for up to 15 ticks; if `[projX, projX + 58)` overlaps `[playerX, playerX + w)` while the
  player is not jumping and has coins: **−1 coin** (sfx 8) and a coin flies back to the elf. Only one
  throw at a time in the whole game.
* When the player starts the walk to an unlocked gate, elves in that room flee and all elves become
  confined (they turn at room edges) until the next spawn.

### 5.6 Elf questions

* 359 questions (OTHR 11000 + q) in 4 tiers by index: 0–62, 63–178, 179–301, 302–358; tier 4 = all.
* Per question history (record +0x30): 0 never answered, 1 right first try, 2 last attempt wrong,
  3 right after a miss; bit 7 = dealt this climb.
* Deal at each new climb (`1150:01d5`): 3 from status 0, 3 from status 2, 3 from status 3, each with
  the fallback chain 0→1→2→3→0, assigned to random slots, each with a random answer order. Slot
  `3·level + elf` belongs to elf 0..2 of that level.
* The question screen is not read aloud automatically. Arrows move the focus (Left/Right toggles the
  speaker button when voice is on), Enter chooses or reads, clicks choose. **Choice 0 in the file is
  the correct answer.** One attempt per catch.
* Correct: status := 1 (no earlier wrong try) or 3; slot cleared; praise screen (sfx 12, random text
  0..3 + voice, clue word); clue word earned; **coins +2 / +1 / 0** for 0 / 1 / 2 earlier wrong tries.
  Wrong: status := 2; that answer greyed and disabled; "sorry" screen (sfx 8). Esc: nothing changes.
* Level change: for each slot of the old level whose question has status 2, the slot of the same
  elf on the next level receives a copy of it (the old slot keeps it too; the next level's original
  question is dropped this climb). This is the observed "broken swap" (`1150:0375`).
* Quiz tier promotion at a new climb: `total·80 ≤ count1·100` within the current tier → tier + 1 (at
  most once per climb, never beyond 4).
* Verified against the original (E8, per REFERENCE_CAPTURES §R2): screen layout pixel-exact (with the
  WinG frame quirk on the focus frames), selection order, reading-highlight timing, coins +2/+1/0,
  greyed answers, praise/sorry screens and Esc.

### 5.7 Coins and nets

| | Coins (`DS:5e5a`) | Nets (`DS:5e5c`) |
|---|---|---|
| New player | 10 | 10 |
| Spent by | Enter: search a spot or pay the net shop (−1) | Space: swing the net (−1) |
| Lost by | thrower elf hit (−1); wall-creature push in the castle climb (−1) | – |
| Gained by | catching any elf (+1); correct answer (+2/+1/0); coin pile (+10) | net shop (+10) |
| When none left | Enter: no toss, hint 6 (nets > 0) or 7 | Space: empty-handed swing (11 ticks), hint 5 (coins > 0) or 7 |

Decrements below 0 are refused silently. The HUD shows coins and nets modulo 100 (two digits) and
the climb's treasures with 1–5 digits.

### 5.8 Net shop

Rooms 13/24/36 (one per level). The sign (BNDL 302 frame `4 − shopCoinsLeft`) is a spot with
content 0xFD: each coin tossed at it decrements `shopCoinsLeft` (4 → 0). At 0 the player auto-walks to
x 220 / 112 / 378, the niche door opens showing a net (PMAP 1320/1322/1324), the player takes it:
**+10 nets**, `shopCoinsLeft = 4`; the empty niche (1321/1323/1325) shows for 4 ticks, then the door
closes. `shopCoinsLeft` is saved (record +0x1D) and shared by all three shops.

### 5.9 Rescue coin pile

When coins = 0 and nets = 0, a pile of 10 coins (PMAP 1699, y 230) appears in the next room entered
that is not in the hall and is not the room where the player ran out, at x = 50 + R(412); if left
behind it moves to the next room entered, which may now be the room where the player ran out;
touching it (horizontal overlap) gives **+10 coins** (sfx 7). It is cancelled as soon as the player
has a coin or a net again (`code_10f8-1148.md` §7). Verified (E14, per REFERENCE_CAPTURES §R6).

### 5.10 Keys and gates

Finding the key (content 3): `g_haveKey = 1`, sfx 12, hint 9, HUD key icon filled (BNDL 301 frame
2·level; outline = 2·level+1). At the gate (Up in a third whose content is 0xFE): with the key the gate
opens (§2.8, sfx 17/15/16 for level 0/1/2); without it hint 8. On the level change: key, gems and
clue count are cleared.

### 5.11 Treasures

* Per level the layout hides `gemsNeeded[tier]` treasures (2/3/4/5). Finding one: hint 4,
  `g_gemsFound++` (gem icon filled), `5e61++`, `g_climbPoints++` (levels 0–2 only), sfx 41.
* Pictures: at climb set-up and at each zone change on levels 0–2, `Treasures_PickForLevel` builds a
  list of `gemsNeeded` treasure indices: base = `climbsDone mod 43`; element 0 = base if no treasure
  was found yet this climb (`5e61 == 0`), otherwise random; the others `base + R(51 − base)`, distinct.
  The n-th treasure found on the level shows list[n] (BNDL 330 + index). Indices 43..50 occur only as
  these random extras. The treasure awarded at the end of the climb is `330 + (climbsDone−1) mod 43`
  after the increment, i.e. the base of the climb just finished.
* Gems are only a counter: the climb ends at the castle however many were found.

### 5.12 Score, ranks, stars

* Score = **total treasures** (u32, record +0x0A) += treasures found in the climb, at the climb end
  (saturating at 0xFFFFFFF0). The climb-end save happens before the addition, so the new total reaches
  disk at the next save.
* Rank = (number of thresholds ≤ total) − 1 with thresholds {0, 5, 25, 70, 115, 170, 230, 300}:
  0 Trainee, 1..6 = that many stars, 7 Champion. Tier = rank / 2.
* Result hint after a climb: promoted → hint `{−,12,15,18,21,24,27,30}[rank]` (once-only), else
  `{10,14,17,20,23,26,29,32}[rank]` (always). Entering room 9 after the first climb queues
  `{11,13,16,19,22,25,28,31}[rank]` (once-only).
* Becoming Champion plays FINALE and the champion celebration (DAM 20305).
* The sign-in list sorts players by total treasures, descending.

### 5.13 Castle-wall climb

`code_1168-1198.md` §2. Screen 0: walk (x clamped to 15..497−w), Up under a ladder column (x centres
82, 169, 256, 343, 430) starts climbing. Climb phase cycle of 4 frames, up 12/20/12/20 px, down
20/12/20/12 px; Left/Right jump to the next column (±87 px over 4 ticks); no ladder → 7-step fall
cycle (+32 px per tick plus jitter) until a ladder is grabbed or the ground of screen 0 is reached
(landing 6 ticks, back to walking). y < −35 → next screen up (y += 267); y > 231 → screen down.
Leaving screen 3 at the top ends the climb. Wall creature: never on screen 0; appears only in columns
without a full ladder; 12-tick emerge/hold/retreat cycle; pushes when fully extended (frame 3), on the
same screen, with `playerY < creatY + 52 < playerY + 110`, from the column next to the climber facing
toward it: the climber jumps away and loses a coin. On screen 0 walking right adds the **left** step
table (`dxLeft`, `1168:09e3`). Verified value by value (E7, per REFERENCE_CAPTURES §R3); the `dxLeft`
quirk is confirmed (M8; decision: keep).

### 5.14 Treasure hall

Rooms 0..N−1 (§5.2). Room 0 shows the newest treasure (index `climbsDone − 1`, mod 43) on one
pedestal at (358, 73); room r ≥ 1 shows six older ones (`climbsDone − 2 − 6(r−1) − i`, mod 43;
negative = empty) at x {112,143,174,272,303,334}, y {40,92,40,92,40,92}. A treasure animates (DAM
20000 + t, layer 0x4D) only while the player's centre x is within [x, x + 30]; otherwise its frame 0
is drawn. Verified (E9, per REFERENCE_CAPTURES §R4); the windows are disjoint, so at most one hall
animation runs at a time (M6 settled: nothing to emulate).

### 5.15 New Player Messages (hints)

One pending hint at a time; once-only hints use bit `g_hintBit[e]` of `g_hintUnseen` (cleared when
queued, not when shown); "always" hints (bit −1) show even when the option is off (turning the option
off only clears the mask). Triggers:

| Event | Trigger | Once? |
|---|---|---|
| 0, 2 | every room tick (so: the first room after sign-in) | yes |
| 1 | never | – |
| 3 | second clue word of a level | yes |
| 4 | treasure found | yes |
| 5 / 6 / 7 | Space without nets (coins > 0) / Enter without coins (nets > 0) / either with neither | yes |
| 8 | gate without the key | yes |
| 9 | key found | yes |
| 10, 14, 17, 20, 23, 26, 29, 32 | climb end, not promoted, by rank | no |
| 12, 15, 18, 21, 24, 27, 30 | climb end, promoted to rank 1..7 | yes |
| 11, 13, 16, 19, 22, 25, 28, 31 | room 9 after the first climb, by rank | yes |
| 33 | never (no text) | – |

Display: speech bubble PMAP 2011 at (33,60), butterfly PMAP 2010 at (430,230), text OTHR 11500+e at
(83,92) width 321, voice 7100+e; music stops and restarts from the beginning afterwards.

### 5.16 Randomness inventory (for deterministic replays)

`srand(low 16 bits of timeGetTime())` once per climb-loop pass (S8). Every other random number is
`R(n)`:

| Use | Call | Address |
|---|---|---|
| layout records per level | R(500) ×3, distinct | 1100:04b5 |
| quiz picks, slot order, answer order | R(cnt), R(i+1), R(k+1) | 1150:00a7, 01d5 |
| elf placement and behaviour | R(250), R(2), 15 × R(3); R(10), R(30), R(20), R(3) | 1178 |
| treasure pictures | R(51 − base) | 1198:0103 |
| praise / sorry text | R(4) | 1150:049a, 0748 |
| coin pile x | R(412) | 1110:00aa |
| castle climb layout, creature | R(tier+1); R(1000), R(5), R(range − h), R(2) | 1168:0047, 1143 |

The order of calls depends on input timing (elves update every tick), so bit-exact replay needs the
same seed **and** the same input on the same ticks.

---

## 6. Resource usage index (by scene)

Ids are decimal. Type is implied by the id range (§3.6). "DS:" items are tables or text in the EXE.

| Scene | PMAP | BNDL | OTHR | SSND | MIDI | EXE data |
|---|---|---|---|---|---|---|
| Start-up | – | – | – | – | 9000 (MCI probe) | DIALOG 1–8, cursors 128–130, icon 15000 |
| Global | – | – | – | – | – | RRGB 900, FFNT 999 |
| Title | 2100 | 400 (20100 + 2101–2110), 401 (20101 + 2111–2129) | 20105–20108 | 7150–7159 | 9006, 9007 | DS:141c/1424, DlgDesc 2234 |
| Club Sign-In | 2100 (restart) | 322 (2180–2183) | – | – | 9007 | DlgDesc 2129, 21e6, 2200, 221a, 2234; strings 2390–23ce |
| Climb common | 1699 | 300 (69 player frames), 301 (1300–1309 HUD), 302 (1350–1355 sign), 415 (2217–2219 lock), 328 (1380–1383 fountain) | 10550+tier, 10555 (new climb only) | – | – | DS:23d8 rooms, 0ba0–0e89 player tables, 1079–1287 words |
| Mountain rooms | backgrounds 1000–1007, 1010–1014, 1017, 1021–1025, 1028–1029, 1032–1035; spots 1400–1511; shop 1320–1325 | 330–380 (picked treasures) | – | sfx (§5.3–§5.10) | 9003, 9004, 9005 | – |
| Treasure hall | 1036, 1037, 1038 | 330–372 | 20000–20042 | – | 9001 | DS:2d7e–2da8 |
| Elves | – | 310, 311, 312, 314, 315, 316, 317, 320 | – | 7001, 7008, 7031, 7037 | – | DS:2acb–2ca7 |
| Elf question | 2000, 2001, 2011 | – | 11000–11358 | 4000–6615, 7300–7552, 7008, 7012 | (stopped) | DS:1990, 1c60, 1dc7, 1de7, 1df1–1e43 |
| Hint popup | 2010, 2011 | 327 (11500–11532) | 11500–11532 | 7100–7132 | (stopped) | DS:13d0, 1414 |
| Level cut-scenes | 2200, 2201 | 410 (20200 + 1100–1103, 2215), 412 (20204 + 1133–1134, 2203–2210) | 20201, 20205 | 7013, 7025, 7027 | – | DS:2d6d/2d71/2d75 |
| Castle climb | 1040, 1041, 1090–1092 | 321 (24 climber frames), 326 (1095–1098), 300 | – | 7000, 7005, 7006, 7025, 7027, 7029, 7030, 7033, 7034, 7040 | 9000 | DS:2950–2aaa |
| Celebration | 2299, 2202 | 323 (20300 + 2300–2365), 324 (20305 + 2312, 2301–2302, 2400–2448), 325 (20308 + 1119–1125), 330+(n−1)%43 | 20301, 20302 | 7002, 7003, 7009–7011, 7014, 7016–7018, 7020, 7021, 7023, 7024, 7033, 7035, 7040 | 9002 / 9008 | – |
| Rank screen / See Rank | 2380, 2381 | – | – | 7022 | – | DS:0ea0, 0f8c, 0fa8, 0fe8, 0fee |
| HUD | (BNDL 301) | 301 | – | 7300+ (clue replay), 7012, 7041 | – | DS:0ec3–0f66 |
| Help menu | – | – | 11600–11603, 11605–11606, 11607, 11608–11611 | – | – | DS:2cfe, 2d1c titles, 0b24–0b6d button text |
| Menus / options / About | – | – | – | – | – | DS:129a–13c5 menu bar, 142c–198d dialogs |

Sound effects by number: `fmt_sound.md` §7.2. Unused resources: `GAPS.md` §2.

---

## 7. Contradictions between topic docs

### 7.1 Factual conflicts and their resolution

Items marked "verified" were re-checked against the binary or the data during this synthesis. All
resolutions have been applied to the topic docs (marked "corrected per ARCHITECTURE.md §7.1 Cn" there;
`GAPS.md` §5).

| # | Conflict | Resolution | Evidence |
|---|---|---|---|
| C1 | `code_10f8-1148.md` §1.1/§16: BNDL 328 = PMAP 1636..1639 | **PMAP 1380–1383** (fountain with frogs, room 36) | verified: `TMT.DLL/32513/328` = count 4, ids 1380–1383; agrees with `fmt_bndl_ffnt.md`, `code_1168-1198.md` |
| C2 | `code_10e0-10f0.md` §2.1 and `code_10f8-1148.md` §2.1: layer 2 = rows 768..1151 | Only rows **788..1054** exist (pristine play-field); the buffer is 1055 rows | `1198:0234` copies (0,404,512,267) → (0,788); `code_1058-1090.md` §2.1 |
| C3 | `code_10f8-1148.md` §2.2/§10.2/§12.5: input 0x13 = Pause key (VK_PAUSE) | **Ctrl+S** (WM_CHAR 0x13); WM_KEYDOWN only maps VK 0x21–0x2E | `1028:004f` tests `what == 2 && p1 == 0x13`; `code_1008-1030.md` §5/§6.2 |
| C4 | `code_10f8-1148.md` §1.1: bit 7 of `Room_Present`'s argument is passed to 1198:04f0 | Computed into a local and **never read** | `code_1168-1198.md` §8.2 (1198:052e-053b, 0557/063a/065f) |
| C5 | DS:65ce named treasuresFound / gamesWon / "current item index" / climbsDone | One variable: **number of completed climbs** = treasures won (incremented once per climb by `10f0:0009`) | all docs agree on the arithmetic; use `g_climbsDone` |
| C6 | Old `code_1168-1198.md` draft (still mentioned in `dgroup_map.md` §4.12): 5th Help handler = null | **1140:0000** `MenuCmd_AboutTM` (relocation at DS:130d) | current `code_1168-1198.md` and `dgroup_map.md` agree |
| C7 | `code_1000.md` §10: the only `longjmp` passes 2, so GameMain's restart branch (r = 1) is unreachable | **Wrong.** `1140:0062` (Start Another Game, not a Ghidra function) calls `longjmp(DS:5eda, 1)` | verified bytes at file 0x16A62: `1e 68 d6 14 9a…` (GameDlg_RunModal DS:14d6), `3d 01 00 75 16 c7 06 ce 13 ff ff 9a…` (hint := −1, Music_StopScene 1128:00ef), `1e 68 da 5e 6a 01 9a…` (longjmp(5eda, 1)); `sys_resources_saves.md` §6.5, `code_10f8-1148.md` §13 |
| C8 | `code_10e0-10f0.md` §3.1/Q12: rooms 1..8 unreachable | Reachable: `TreasureHall_BuildRooms` (1190:0000) rewrites the exits of rooms 0..8 at each climb set-up | `code_1168-1198.md` §7.2, `dgroup_map.md` §4.16 |
| C9 | `code_10e0-10f0.md` §4.1 "1198:06b4 room-36 tree animation"; `dgroup_map.md` §4.19 "scene_treeBndl (room 36 tree)" | **Fountain with frogs** (BNDL 328, PMAP 1380–1383); it is also the obstacle | visual check in `fmt_pmap.md` §8, `code_1168-1198.md` §8.1 |
| C10 | `code_1168-1198.md` Q6: where does the gate auto-walk target come from? | Constants in `Player_Update`: x 330 / 281 / 170 for rooms 11 / 25 / 38, y 142 | `code_10e0-10f0.md` §4.3.3 (`10e0:0447`) |
| C11 | `code_10e0-10f0.md` Q10: could 0xFE appear outside rooms 11/25/38? | No: `Layout_ApplyRecord` writes 0xFE only to those slots; all others are cleared to 0xFF first | `code_10f8-1148.md` §3.3/§5.2 |
| C12 | `code_10e0-10f0.md` §2.3/§6.2: `1170:005b` "waits for the effect to end" | Stops **immediately** (`Wave_Stop` + free) | `fmt_sound.md` §7.1, `code_1168-1198.md` §3 (1170:005b..0081) |
| C13 | `fmt_othr_tables.md` §3.4: the 0xFD spot's picture is BNDL 310 member `4 − counter`, "[I] hidden passage" | **BNDL 302** (net-shop sign PMAP 1350–1355); 0xFD is the net shop; BNDL 310 is the thrower's sparkle/coin projectile | verified: `1198:0085 PUSH 0x12e` (302) → DS:5e80; `10f8:0285` reads DS:5e80 |
| C14 | `fmt_othr_tables.md` §4.3: scripts 20300, 20301, 20302, 20305 end `FD … FF` and leave their last frame on screen | **Only 20300** ends `FD FD FD FD FF`; 20301, 20302 and 20305 end `FE FF` | verified by parsing the four scripts; agrees with `code_1098-10d8.md` §8.1/§9.2 |
| C15 | `code_1150-1160.md` §3.4: question 225's first line is escapes only, so its narration is shifted by one | **No shift.** OTHR 11225 has the normal header and 5 text lines; SSND 5613–5617 lines, 5618–5620 answers; 5621 duplicates 5620 (PCM identical except the last byte) and is never played | verified OTHR 11225 bytes; `fmt_othr_puzzles.md` §6.1 |
| C16 | `fmt_othr_puzzles.md` §8.2: question background PMAP 2000 drawn "white transparent" | **Opaque** (flag 0) | verified `1150:0ab4 PUSH 0` before `CALLF 1058:0cf3` |
| C17 | `fmt_bndl_ffnt.md` §2.4.1: treasure count = byte at `[DS:5e58] + 0xec3` through a near pointer; `DS:5cca` = level | `DS:5e58` is the u8 rank tier used as an index: count = `DS:0ec3[tier]`; `DS:5cca` is the room number (level = room +0x1a) | `code_1168-1198.md` §8.2, `dgroup_map.md` §4.9 |
| C18 | `code_1058-1090.md` §10.1: without `midimap.cfg` music never plays; `fmt_sound.md` §8.2: `Midi_PlayTrack` does not check `midiOk`, so tracks may still open and play | The code paths in `fmt_sound.md` are correct [O]; the audible result needs a run (`GAPS.md` §4) | `1090:0388` has no `midiOk` test |
| C19 | `fmt_sound.md` §7.2 sfx 8/31 "snowball" | A sparkle that turns into a coin (BNDL 310) | `code_1168-1198.md` §4.1 (rendered frames) |
| C20 | `dgroup_map.md` §4.10: the spoken clue word uses the narrator bank of the question | True for the praise screen (`1150:06ac`); the HUD replay (`10e8:01ef`) always uses 7300 (bank 1). Both paths exist | `code_10e0-10f0.md` §5.3, `fmt_sound.md` §6.4 |
| C21 | `fmt_othr_puzzles.md` OQ1 (record load path unknown), OQ2 (PlaySpeech return value), OQ3 (who registers 0x409) | Load = `Users_LoadSelected` 1160:137d; return = "interrupted"; 0x409 = `Hud_SetClueBoxMode` modes 1/2 (0x125..0x128 are never registered) | `sys_resources_saves.md` §6.6, `code_1058-1090.md` §9, `code_10e0-10f0.md` §5.2 |
| C22 | `code_1150-1160.md` Q4: can a lower level be re-entered (making the carry-over copy matter)? | No exit leads from a higher level to a lower one | `dgroup_map.md` §4.16 room table |
| C23 | `fmt_othr_tables.md` §1/§3.4 and `fmt_bndl_ffnt.md` §2.3 call BNDL 302/310/311–320 "UI art", "scene art", "frame cycles of animated scene objects" | 302 net-shop sign, 310 thrower projectile, 311–320 elf animation sets | `code_1168-1198.md` §4.1, `code_10f8-1148.md` §4 |
| C24 | Title page count: `fmt_sound.md` §6.2 "six pages 0..5"; `code_10f8-1148.md` §12 "pages 0..4 loop, then the ending animation" | Same code: `Title_WaitPage(5)` is the ending-animation wait, not a text page | `1138:007a` |
| C25 | `fmt_pmap.md` §8 catalogue: 1600–1608 "zapped-elf effect" | Thrower projectile (sparkle → coin) | `code_1168-1198.md` §4.1 |
| C26 | `code_10e0-10f0.md` §2.2: `FF 05 FF` "probably centre" | Toggle centring; when switched on the current line is re-centred | `code_1098-10d8.md` §7.2 (10b0:0326) |

### 7.2 Canonical names (one name per address)

The topic docs used to give some functions and variables different names, and two names
(`Dlg_RunModal`, `Music_Stop`) meant different functions in different docs. **This has been resolved:**
`re/names/*.tsv` now lists every address exactly once with one valid C identifier, and every doc in
`re/docs/` uses that name. DGROUP variables are listed only in `re/names/dgroup.tsv` (the copies in the
code-segment files were merged into it). The table records the decisions and the retired aliases, so
older notes and scratch files can still be read; the retired names should not be used any more.

| Address | Canonical name | Retired aliases |
|---|---|---|
| 1030:0000 / 005d / 0082 | `NewPtr` / `DisposePtr` / `DisposePtrDeep` (Mac Memory Manager names) | `Mem_AllocLocked` / `Mem_FreeLocked` / `Mem_FreeBundleTree`; "FreeResource" (`fmt_bndl_ffnt.md`) and `Res_Free` for 0082 |
| 1028:0126 | `Input_Flush` | `PumpMessages` |
| 1080:0000 | **`WinDlg_RunModal`** (the Windows `DialogBox` runner, start-up dialogs) | `Dlg_RunModal` (code_1058-1090), `ShowDialog` (sys_resources_saves) |
| 10c0:0ae5 | **`GameDlg_RunModal`** (the game's own DlgDesc message / Yes-No box) | `Dlg_RunModal` (code_1098-10d8, code_1150-1160), `Dlg_Run` (code_10f8-1148) |
| 1098:0016 / 0089 | `Hotspot_Set` / `Hotspot_Remove` | `HotSpot_Add`, `AddHotspot` / `HotSpot_Remove` |
| 10b8:0119/020a/024e/029d/0312/06e4 | `Anim_Start/IsRunning/Stop/EraseSprites/Step/DrawSprites` | `AnimObj_Start/IsRunning/Stop/…/Update/DrawLayer` |
| 10d8:0000 / 0082 | `Anim_LoadBundle` / `Anim_PlayModal` | `Anim_LoadBndl`, `Anim_Load`, `Cutscene_LoadBndl` / `Anim_Play`, `Cutscene_Play` |
| 1128:0000 | `Music_PlayZone` | `Music_Select`, `PlayMusic` |
| 1128:00d6 | **`Music_Halt`** (`Midi_Stop` only; zone and id kept for `Music_Resume`) | `Music_Stop` (fmt_sound) |
| 1128:00dc | `Music_Resume` | `Music_Restart` |
| 1128:00ef | **`Music_StopScene`** (stop and forget the zone) | `Music_Stop` (code_10f8-1148, code_1150-1160, earlier versions of this file) |
| 1128:012f / 0151 | `Audio_ToggleSound` / `Audio_ToggleAll` (Ctrl+S toggles music, sfx and voice; C3) | `Sound_ToggleMusicSfx` / `Audio_TogglePause`, `Sound_ToggleAll` |
| 1070:011c / 0120 | `Fx_FadeIn_Stub` / `Fx_FadeOut_Stub` | `Pal_FadeIn` / `Pal_FadeOut` |
| 1138:007a / 043d / 0283 | `Title_Run` / `Title_WaitPage` / `Title_WaitStep` | `Intro_Run` / `Intro_Page` / `Intro_Wait` |
| 1130:0000 / 0068 | `QueueHint` / `Hint_Show` | `Hint_Queue` |
| 1150:100a | `Quiz_AskQuestion` | `AskElfQuestion` |
| 10e0:03a6 | `Gfx_RestoreFromLayer` (clip, then copy from layer 1/2: the sprite eraser) | `Gfx_RestoreRect` (clashed with the save-under restore `Gfx_RestoreRect` 1058:1695, which keeps the name) |
| DS:5f01 | `g_hintUnseen` | `g_hintsPending`, hintMask |
| DS:086c | `g_paletteDevice` | `g_palDevice` |
| DS:0876 | `g_shadowTable` | `g_shadowRemap`, `g_maskRemapTable` |
| DS:09d4 | `g_inResourceLoad` | `g_menuLock` |
| DS:2ab6 | `g_caughtElf` | `elf_caughtCarrier` |
| DS:5e5e / 5e5f / 5e61 | `g_gemsFound` / `g_climbPoints` / `g_treasureCount2` | `g_treasFoundLvl` / – / `g_treasFoundGame` |
| DS:65ce | `g_climbsDone` (C5) | `g_treasuresFound`, `g_gamesWon`, "current item index" |
| DS:2dd0 / 2de0 | `scene_fountainBndl` / `scene_fountainFrame` (C9) | `scene_treeBndl` / `scene_treeFrame` |
| DS:2a3e, 2a4c, 2a5a, 2a68, 2a74, 2a80, 2a8c | `cl_fallFrames_base`, `cl_fallDx_base`, `cl_fallDy_base`, `cl_landFrames_base`, `cl_landDx_base`, `cl_landY_base`, `cl_creatSeq_base`: the 1-based *code base* addresses; the tables themselves (`cl_fallFrames` …) start 2 bytes later (`dgroup_map.md` §4.17) | the same names without `_base` (they clashed with the table names) |
| other DGROUP variables named twice | the `dgroup.tsv` name, e.g. `g_keyUpHeld` (06ee..06f4), `g_egaRGB` (07d8), `g_dirtyEnabled` (0862), `g_viewOriginX/Y` (086e/0870), `g_hGfxLib` (0872), `g_unk13cd` (13cd), `g_dlgExitGame` (1533), `g_dlgTitleExit` (2234), `g_hdcScreen` / `g_hbmBack` / `g_selPlanarBits` / `g_selPixels` / `g_hGdiHeap` (4296..42a0), `eh_*` (Borland EH globals) | `g_keyUp…`, `g_vga16Colors`, `g_recordDirty`, `g_viewX/Y`, `g_hPmapDll`, `g_hPixmapDll`, `g_musicCue`, `g_dlgExit` (both), `g_hdcWin`, `g_hbmOffscreen`, `g_selDdbBits`, `g_selBackBits`, `g_gdiHeapSel`, `__except*`/`__task*` |
| minor aliases (single docs) | the names-file names | `PlaySound` (→ `Sfx_Play` 1170:0000), `Sound_StopAndFree` (→ `Sfx_Stop` 1170:005b), `App_Suspend` / `App_Resume` (→ `App_Deactivate` / `App_Activate`), `GetInputEvent` (→ `Input_GetCode`), `Pix_RemapToPhysical` / `Pix_IsRund` / `Pix_DecodeRund` / `DrawPixmapOpaque` (→ `PixMap_RemapToSystem` / `PixMap_IsRUND` / `PixMap_UnpackRUND` / `PixMap_DrawOpaque`), `Music_IsScenePlaying` (→ `Music_IsZonePlaying`), `Hint_Speak` (→ `Hint_WaitDismiss`), `Puzzle_ReadAloud` (→ `Quiz_SpeakAll`), `Users_Read` / `Users_Write` (→ `Users_ReadFile` / `Users_WriteFile`) |
| "level", "path", "zone" | mountain level 0..2 (room +0x1a). `fmt_othr_tables.md` "zone" and `fmt_othr_puzzles.md` "path" = level; `code_10f8-1148.md` "zone" = music zone (room +0x1b) | – |

The Ghidra export (`re/win/ghidra_export/`) was regenerated from these names, so `functions.tsv`,
`decompiled.c` and `disassembly.asm` show the canonical names (no `_SSSS_OOOO` suffixes from name
clashes; all 863 exported functions carry a names-file name). An earlier Ghidra re-analysis had split
the shared switch-case code of `Player_Update` and `Music_PlayZone` into 8 extra functions; they are named
in the names files (`Player_Update_caseL_sfx35/_playSfx/_move`, `Player_Update_caseR_…`,
`Music_PlayZone_case12`, `Music_PlayZone_start`), so `Player_Update_caseL/caseR` and
`Music_PlayZone_cases` are now only 4–8 bytes each in the export. Doc-local pseudo-code shorthand that a
doc maps to owner names in its own table (e.g. `code_1168-1198.md`: `Res_Load`, `Bndl_Get`, `Surf_Restore`)
is left as is.

---

## 8. Open questions

Only cross-cutting questions are listed here; `GAPS.md` §3 ranks every open question of every
topic doc by port impact.

1. Behaviour that only a run can show: music without `midimap.cfg` and the class cursor
   (`LoadCursor(hInst, IDC_ARROW)`). See `GAPS.md` §4 for the capture plan. Settled by captures: the
   look of transition type 1 (`REFERENCE_CAPTURES.md` §4.3), the right-walk `dxLeft` quirk on the
   castle screen 0 (confirmed, keep; per REFERENCE_CAPTURES §R3) and the stale tier after an Exit on the
   rank screen (confirmed, keep; per REFERENCE_CAPTURES §R5.3).
2. Decoy content 1 (25–40 % of all spots) has no behaviour in the Windows EXE. Is it a lost Mac/DOS
   feature? The DOS version has not been checked.
3. 89 PMAPs and several sounds, texts and scripts are never referenced (`GAPS.md` §2). They may hint
   at Mac-edition features (carrying the crown, the magic zapper) that the Windows build dropped.
4. Unreachable rooms 19–21, 32–34, 45–47 and the unused fourth-exit fields (`RoomDef` +0x09/+0x0d/
   +0x10).
5. `g_gfxConst4` (DS:0860 = 4) gates the fade stubs; its meaning is unknown and irrelevant while the
   stubs do nothing.
