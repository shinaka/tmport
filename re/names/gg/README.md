# Gizmos & Gadgets names (`re/names/gg/`)

One canonical name per address of `ssg.exe` (*Gizmos & Gadgets!* DOS v1.0), applied to the Ghidra
project `re/ghidra_proj/GGDos` with `tools/ghidra_scripts/ApplyNames.java` and carried by the export
`re/gg/ghidra_export/` (`bash tools/gg/ghidra_export.sh` re-applies them after an edit). The docs in
`re/gg/docs/` use exactly these names; engine code will too. Add or change a name here first.

Addresses are Ghidra `SSSS:OOOO` of the flattened image (`re/CONTEXT.md`, G&G section): resident code
1000..1d42, DGROUP 1ea7, overlays 2a80..4140. Overlay functions are named at their overlay address,
never at the stub thunk (`stub:0020 + 5k`, which Ghidra names `thunk_<target>`).

## Files

Four tab-separated columns: `SSSS:OOOO`, name, C signature or type (may be empty), comment. Rows are
sorted by address; an address appears in exactly one file.

| File | Contents |
|---|---|
| `rtl_bc30.tsv` | 1000: C0 start-up and the Borland C++ 3.0 run-time library |
| `ovrman.tsv` | 1d42: the VROOMM overlay manager |
| `engine_lowlevel.tsv` | graphics (1399, 1473, 1581, 15df), resources and LZH (14ff, 1b48), timers (152c), memory (1536), interrupt handlers, keyboard, mouse and zones (1454, 17a0, 17ab, 198d, 1d3c), lists (19c0), the video mode switch 12e0:0b92 and small helpers in overlays (2bd4, 2c08, 3f50) |
| `engine_sound.tsv` | 1bb4 sound driver, 1cdd sound banks |
| `ui_tlc.tsv` | the TLC UI library: fonts 2b16, text 2b89, DAM 3d39 (and its launch-puzzle player in 3797), dialogs 3f56, buttons 4034, menu bar 40a4, menu commands 4106, help 411d, text viewer 4140 |
| `game_flow.tsv` | `Seq_Run` and the state functions, hub and HUD (12e0, 1a04), saves (2b5a), attract (2c11), sign-in (2caa), results and movie (2fcb), start-up (303a), hints (3060) |
| `game_world.tsv` | parts (153f), rooms (1622), animation sequences (174b), actors (1834, 18f5), collision (1adb), Cyber Chimps (2a80), workshop (2dee), races (3dd6, 3e77) |
| `game_puzzles.tsv` | the puzzle overlays and their dispatcher (38e2) |
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
functions, 155 labels). Where passes disagreed, the name that describes what the code does won:

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

Weak names to confirm in wave 1: `Collide_Mask1..3`, `Race_StepSpeed` / `Race_StepSpeed2`,
`Hud_Cmd6000..6003` (mapped in the order the systems notes give), `List_Head` / `Remove` / `New` /
`Clear`, `Btn_AddZone` / `RemoveZone`, `Pal_Save` / `Pal_Restore`, and the puzzle entries
`PzBalance_Run`, `PzMagnets_Run`, `PzGears_Run` (from the dispatch order only).
