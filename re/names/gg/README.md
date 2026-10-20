# Gizmos & Gadgets names (`re/names/gg/`)

One canonical name per address of `ssg.exe` (*Gizmos & Gadgets!* DOS v1.0), applied to the Ghidra
project `re/ghidra_proj/GGDos` with `tools/ghidra_scripts/ApplyNames.java` and carried by the export
`re/gg/ghidra_export/` (`bash tools/gg/ghidra_export.sh` re-applies them after an edit). The docs in
`re/gg/docs/` use exactly these names; engine code will too. Add or change a name here first.

Addresses are Ghidra `SSSS:OOOO` of the flattened image (`re/CONTEXT.md`, G&G section): resident code
1000..1d42, DGROUP 1ea7, overlays 2a80..4140. Overlay functions are named at their overlay address,
never at the stub thunk (`stub:0020 + 5k`, which Ghidra lists under its target's name, or as
`thunk_FUN_…` while the target is unnamed).

## Files

Four tab-separated columns: `SSSS:OOOO`, name, C signature or type (may be empty), comment. Rows are
sorted by address; an address appears in exactly one file.

| File | Contents |
|---|---|
| `rtl_bc30.tsv` | 1000: C0 start-up and the Borland C++ 3.0 run-time library |
| `ovrman.tsv` | 1d42: the VROOMM overlay manager |
| `engine_lowlevel.tsv` | graphics (1399, 1473, 1581, 15df), resources and LZH (14ff, 1b48), timers (152c), memory (1536), interrupt handlers, keyboard, mouse and zones (1454, 17a0, 17ab, 198d, 1d3c), lists (19c0), the video mode switch 12e0:0b92 and small helpers in overlays (2bd4: shuffle, zone and number formatting; 2c08 hardware detection; 3f50 division) |
| `engine_sound.tsv` | 1bb4 sound driver, 1cdd sound banks |
| `ui_tlc.tsv` | the TLC UI library: fonts 2b16, text 2b89, DAM 3d39 (and its launch-puzzle player 3797:100e), dialogs 3f56, buttons 4034, scroll bar 406a, menu bar 40a4, menu commands 4106, help 411d, text viewer 4140 |
| `game_flow.tsv` | `Seq_Run` and the state functions, the music cue callbacks, hub and HUD (12e0, 1a04), saves (2b5a), rank lookup (2bd4:0225), attract (2c11), sign-in (2caa), results and movie (2fcb), start-up (303a), hints (3060) |
| `game_world.tsv` | parts (153f), rooms (1622), screen animations (174b), actors (1834, 18f5), collision and contacts (1adb), Cyber Chimps (2a80), part and animation helpers in ovl04 (2bd4:0101..01c1), workshop and blueprint (2dee), races (3dd6, 3e77) |
| `game_puzzles.tsv` | the puzzle dispatcher and framework (38e2), the puzzle sound helpers (3924) and the eight puzzle overlays (3083, 32fe, 33cd, 35bb, 3797, 3938, 3a4a, 3b2f) |
| `dgroup.tsv` | DGROUP (`1ea7:XXXX` = DS:XXXX): always data labels, never functions |

## Conventions

* Functions `Module_Verb[Object]` (`Res_Read`, `Pal_FadeIn`, `Snd_RegisterSong`); the 17 states of
  the main `Seq_Run` table DS:00c2 are `State_NN_Role` (`State_03_Hub`, `State_15_FinalMovie`).
* Globals `g_name`, strings `s_name`.
* Run-time library and overlay-manager functions keep Borland's names (`__OvrLoad`, `_terminate`).
  The compiler helpers are written without Borland's trailing `@` (`F_LXMUL`, `F_SPUSH`: names
  must be identifiers); the comment gives the Borland spelling (`F_LXMUL@`).
* A name that rests on inference (call order, a dispatch table, a guess at the role) says
  "(inferred)" in its comment.
* `_Noreturn` in the signature column marks a function that does not return (`exit`, `_exit`,
  `abort`, `_terminate`, `_c0_OvrRuntimeError`). ApplyNames applies those first, so that Ghidra's
  disassembly stops after calls to them.
* The signature column is documentation; ApplyNames reads only `_Noreturn` from it.

## Wave 0 (decision table)

The scoping passes named 192 functions; wave 0 merged their names with those the other passes used
in their notes (600 rows), and the review added the names the format specs rely on (608 rows: 453
functions, 155 labels). Wave 1 brought the total to 1599 rows (last section). Where passes disagreed, the name that describes what the code does won:

| Address | Canonical | Other names in the notes | Reason |
|---|---|---|---|
| 14ff:000d | `Res_Read` | `Res_Load` (data pass) | reads an entry into the shared load buffer; nothing is allocated |
| 14ff:00fe | `Res_LoadCopy` | `Res_Load` (exe pass) | `Res_Read` plus a private copy the caller frees |
| 14ff:0167 | `Res_OpenFiles` | `Res_OpenVolumes` (data pass) | opens the four archives and checks their 0xA55A words |
| 1b48:02aa | `Lzh_Decompress` | `Res_Decompress` (exe pass, format pass) | the decompressor itself; it reads from a file handle, not a resource |
| 198d:0106 | `Input_GetCode` | `Input_Poll` (platform pass) | returns the next command code (mouse zone code, key word, idle code) |
| 1399:04d1 | `Lpx_BlitRleMirror` | `Lpx_BlitRleAlt` (first wave 0 name) | the format pass decoded it: the RLE picture mirrored left to right (`fmt_graphics.md` §2.4) |
| 1cdd:0024 | `SndRes_LoadMusicBank` | `SndRes_Load` (first wave 0 name) | one of three loaders: music bank, effect bank (`SndRes_LoadEffectBank` 1cdd:012d), single song (`SndRes_LoadSong` 1cdd:01e0) |

Added by the review from the format specs: `Screen_RandomWipe` 1473:0042, `Snd_StartSection`
1bb4:0045, `Snd_MetaEvent` 1bb4:08ef, `Snd_SetCue` 1bb4:0be5, `Snd_AllocSlot` 1bb4:0e71,
`SndRes_LoadEffectBank` 1cdd:012d, `SndRes_LoadSong` 1cdd:01e0, `Actors_LoadSet` 1834:0088. The
Help menu's topic names follow its handler table, not address order.

The weak names listed for wave 1 were all settled there (next section).

## Wave 1 merge (decision table)

Wave 1 left proposals of six topic specs (`wave1_{platform,world,vehicles,puzzles_a,puzzles_b,ui}.tsv`,
1037 rows). The integration merged them into the area files above (scripted, by segment) and
deleted the proposal files; ApplyNames re-applied everything. **Result: 1599 rows, 982 functions
and 617 DGROUP labels** (wave 0: 453 and 155). Every real function of the program is named except
the LZH internals (1b48, 11 functions), the overlay manager internals (1d42, 23) and one RTL
helper (1000:2dd7); the export's function count is unchanged (1234: no name created a function).
The wave 2 integration added 9 DGROUP labels for tables the specs and the engine use
(`g_projectileTransitions`, `g_hudColorScheme`, `g_chimpListData`, `g_attractScripts`,
`g_attractChannels`, `g_attractAnims`, `g_animCelCount`, `g_animCels`, `g_animFrames`), moved
`g_animChannels` to DS:ba66 (the start of the two channel records; DS:ba80 is channel 0's cursor)
and retyped `g_lzhDefaultDistLengths` as `u16[3][16]`: 626 DGROUP labels.
Where an agent renamed an older name, the spec that owns the module won; the name must describe
what the code does. Corrections of wave 0 names (old → canonical):

| Address | Canonical | Old name | Reason (owner spec) |
|---|---|---|---|
| 12e0:0a24 | `Room_OpenPuzzleDoor` | `Room_SetDoorArt` | turns the solved door and its twin into type 9 and moves the player when DS:b144 is set (`code_world.md` §6.6) |
| 1399:02a2 | `Gfx_SaveToBack` | `Gfx_CopyFromBack` | copies screen rows y.. **to** the back page rows y+200.. (`LES DI` = row y+200, `LDS SI` = row y); the reverse copy is `Gfx_RestoreRect` (`code_runtime.md` §10.3) |
| 1473:0337 | `Screen_SavePlayArea` | `Screen_RestorePlayArea` | `Gfx_SaveToBack(0, 0, 320, 165)` |
| 174b:0324 | `ScreenAnim_Load` | `AnimSeq_Load` | the 174b sets animate static screens (hub, workshop, attract, results), not actors (`code_world.md` §19.1) |
| 174b:0522 | `ScreenAnim_PlayModal` | `AnimSeq_PlayModal` | as `ScreenAnim_Load` |
| 198d:02b8 | `Input_GetHeldKeys` | `Input_CheckIdle` | returns the held-key bits (plus the idle bit 0x40) to the player actor; not part of `Input_GetCode` (`code_runtime.md` §6.5) |
| 1a04:0a82 / 0b5b / 08ad / 084c | `Hud_TrashCursorPart` / `Hud_PutBackCursorPart` / `Hud_ScrollLeftArrow` / `Hud_ScrollRightArrow` | `Hud_Cmd6000..6003` | the four HUD commands 6000..6003 (`code_vehicles_races.md` §4.4) |
| 1adb:0093 / 01ef / 03b0 | `Actor_CollideWalls` / `Chimp_ProbeContacts` / `Actor_UpdateContacts` | `Collide_Mask1..3` | walls, chimp probes, contact flags (`code_world.md` §10) |
| 1bb4:06d9 | `Snd_ApplyChannelProgram` | `Mpu_InitChannel` | applies the program of a taken channel for every class, not only MIDI (`code_sound.md` §10.1) |
| 1bb4:11a2 | `Snd_SetMasterVolume` | `Mpu_SetMasterVolume` | sets the OPL master volume DS:1686, plus a SysEx when an MPU is present |
| 1ea7:1712 | `g_noteOctave` | `g_oplNoteTable` | a 128-entry n / 12 table; DS:1792 is n % 12 (`code_sound.md` §2) |
| 1ea7:1bae | `g_chimpLists` | `g_chimpCounts` | far pointers to the chimp lists, not counts (`code_world.md` §13.1) |
| 1ea7:20dc | `g_rosterScrollBar` | `g_rosterList` | the sign-in list's scroll bar record (ovl30 is a scroll bar, `code_ui.md` §9) |
| 1ea7:457c | `g_blueprintSeen` | `g_buildingLive` | first word of the live building block: the blueprint was shown (`code_vehicles_races.md` §6.1) |
| 1ea7:4580 | `g_tubeRoom` | `g_levelParam` | the hall the workshop tube leads to (`code_world.md` §4.5) |
| 1ea7:7a6c | `g_cursorSavedX` | `g_cursorUnder` | x of the cursor save-under; the buffer pointer is DS:7a7e (`code_runtime.md` §12.1) |
| 1ea7:7aa4 | `g_playerRoom` | `g_roomIndex` | actor 0 +0x1A |
| 1ea7:a0e6 | `g_hintSlotId` | `g_hintQueue` | the id field of hint slot 0; the slots are `g_hintSlots` DS:a0de (`code_ui.md` §13.1) |
| 2a80:0635 | `Chimps_SetupActors` | `Chimps_Place` | sets up the chimp actors; places nothing, no rand() (`code_world.md` §13.2) |
| 2a80:0784 | `Chimps_Spawn` | `Chimps_Init` | places the chimps (rand) |
| 2c08:0000 | `Video_DccSupported` | `Video_GetDisplayCode` | returns AL of INT 10h 1A00h (0x1A when supported), never the display code in BL (`code_runtime.md` §18) |
| 2caa:004f | `Roster_LoadOrCreate` | `Players_CreateFile` | reads the header and index, or creates the file (`code_ui.md` §12.3) |
| 2caa:01c5 | `Roster_UpdateTyped` | `Roster_Insert` | prefix selection or insertion of the typed name (`code_ui.md` §11.6) |
| 38e2:019e | `Puzzle_FewUnsolvedLeft` | `Puzzle_PickAny` | a test (at most 4 unsolved variants left); it picks nothing (`code_puzzles_a.md` §1.3) |
| 3dd6:044e | `Race_ComputePerformance` | `Race_StepSpeed2` | runs once per race (`code_vehicles_races.md` §8.1) |

Addresses with competing wave 1 proposals:

| Address | Canonical | Other proposal | Reason |
|---|---|---|---|
| 1473:077e | `Gfx_RecolorPicture` | `Pic_RemapColors` (vehicles doc) | 1473 is the platform's segment; it builds a LUT and calls `Gfx_RemapPicture` |
| 1ea7:5c10 | `g_vehicleColor` | `g_trophyColors` (world) | the vehicle colour per race, written by the workshop paint and read by the races and the hub trophies |
| 1ea7:5c1f | `g_keyHeldSeen` | `g_playerActive` (world) | set by `Input_GetHeldKeys` (and the race state, a HUD recycle), read only by `Hud_Update` |
| 1ea7:7a44 | `g_effectBank` | `g_warehouseSfx` (world) | the effect bank of whichever screen is up (warehouse, workshop, results) |

Comment or signature corrections without a rename: `Pal_SetShadowRange` (rgb, byteOffset, count),
`Hint_Queue` (u16 delay), `g_randSeed` (u16), `g_dirtyTileCount` (u32 running total),
`g_spkDivisors`, `g_hintSeen` (19 words), `g_yesNoButtons`, `g_pagerButtons`, `g_hudDirty` (the
bits), `Btn_AddZone` / `Btn_RemoveZone`, `Chimp_TakePart` / `Chimp_DropPart`, `Race_StepSpeed`,
`Pal_Save` / `Pal_Restore`, `List_Head` / `List_Remove` / `List_New` / `List_Clear` (no longer
inferred), and the puzzle entries `PzPieces_Run`, `PzGears_Run`, `PzBalance_Run`, `PzMagnets_Run`
(kinds 2, 3, 4, 7 confirmed by code, data and the round 2 captures). Added by the integration:
`Zone_SetFromObject` 2bd4:00c1 (the HUD strip's zone helper, the last unnamed function of ovl04).

DGROUP labels that the code reaches only through an immediate or a computed offset (for example
DS:1792 = DS:1712 + 0x80, the launch table DS:2c62) had no Ghidra label before; ApplyNames creates
them.
