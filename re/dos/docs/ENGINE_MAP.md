# DOS edition: engine map of the game layer (waves 3-4)

How the game logic of `SST.EXE` (the 1990 DOS edition) maps onto the engine's `src/dos/game/`:
which engine function ports which original function, which wave-3 agent owns it, how the modules
call each other, who writes which globals, the memory-layout quirks and their helpers, the runtime
model, and how tests set up a scene. Read with `SCOPING.md` §5-6, `PORTING_NOTES.md` and the topic
specs (`code_world.md`, `code_player.md`, `code_elves_layout_hints.md`, `code_ui.md`,
`code_runtime.md`); the specs say *what* each routine does, this map says *where* it goes.

Agents of the logic phase: **world**, **player**, **elves**, **ui**, **mainloop** (§2).

**Status after wave 3 (integration):** every function of the map (§9) is ported; none is a stub.
The game plays from the title through the sign-in, the three levels, the castle, the end sequence,
the treasure room and the rank screen back to the clubhouse, saves at the original's moments and
resumes saved games; two end-to-end playthroughs run in `tests/test_dos_playthrough.cpp` (§10).
Known gaps and deviations: §8.

**Status after wave 4 (fidelity):** the engine runs at the reference machine's timing (long frames,
disk I/O, presentation at a frame's end: §3.9) and starts from any DOSBox-X state
(`--dos-load-state`). Every r2 frame log a key script can drive matches tick for tick (21 logs),
every checkpoint of the fidelity scenarios that shows the moment of its capture matches at 0 pixels
except two (129 pixels each, §8), in every look; the AVI time bases fit; the result table is §11.

---

## 1. Shape of the game layer

```
DosRuntime (dos/app/dos_runtime.h)       virtual PIT, lockstep, keyboard (Kbd_ISR + BIOS), mouse driver,
   |  RuntimeClient                      step gate, tick counters, original frame clock, --keys scripts
   v
DosGame : GameState, RuntimeClient       (dos/game/game.h) the context of every module function
   |-- GameState (inherited)             world / player / elves / ui / core sections (game_state.h)
   |-- tables (GameTables)               read-only EXE tables, loaded once
   |-- files (DosGfxFiles)               SST.EXE, data files, decoded PANs / PAKs, the font
   |-- help, puzzles, dictionary, quizTiers
   |-- rt, sound (SoundFacade), users (DosUsersStore), rand (BorlandRand)
   |-- inputTables, input (InputPoll), mouse (MousePointer), sprites (SpriteRenderer)
   |-- back, screen (BackBuffer), palette, pan / prevPan, bank
   v
module functions                         void sceneUpdate(DosGame& g); uint16_t cellP5CaveDoorW(DosGame& g); ...
```

* Every original function is a free function `name(DosGame& g, ...)` in `namespace tmport::dos`, named
  by the canonical name in camelCase (`Scene_Update` -> `sceneUpdate`, `Cell_P5_CaveDoorW` ->
  `cellP5CaveDoorW`, `PlrSt_10_OnPath` -> `plrSt10OnPath`, `ElfSt_47` -> `elfSt47`). The header
  comment gives the address, the original name and a one-line contract; the implementation repeats
  `// SSSS:OOOO Name` above the body (DESIGN.md).
* Globals are fields of the state: `g.world.cameraX` (g_cameraX DS:37EA), `g.core.coins` (g_coins
  DS:9134), `g.player.slots[1]` (sprite slot 1). Globals that a wave-2 object owns are reached
  through `DosGame` (§4.2).
* Original far pointers to DGROUP data are DS offsets (`DsPtr`); strings are read with
  `g.dsString(off)` (it sees the writable copies, §4.4).
* Read-only tables come from `g.tables` (or `g.exe()` through `dg::` offsets); nothing of the game is
  stored in the engine.

### 1.1 Files and owners

| Header / source | Original | Content | Owner |
|---|---|---|---|
| `game_state.h/.cpp` | DGROUP | GameState (5 sections), GameTables, field registry, `dumpToDgroup`, `loadFromDgroup`, `diffDgroup`, quirk helpers (§5) | shared header (additive, own section only); `.cpp`: mainloop |
| `game.h/.cpp` | – | DosGame (context, services, RuntimeClient, `dsString`, `dumpToDgroup(const DosGame&)`) | mainloop |
| `sound.h/.cpp` | 491b entry points | SoundFacade, RecordingSound, DosAudioSound, game-side sound calls, `tuneForArea` | mainloop |
| `main_loop.h/.cpp` | 130f main parts, 43cb:046e, 4a85 wrappers | frame body, Game_Tick, Frame_Present, Mouse_Poll, Sound_SyncState, Input_Tick / Input_MouseWalk | mainloop |
| `init.h/.cpp` | 1460 game parts | Game_Init (game part), Game_InitVars, Sprites_LoadBegin | mainloop |
| `scene.h/.cpp` | 1437, 1460:05e8 | Scene_Update, Scene_LoadPan, sprite bank switch | world |
| `camera.h/.cpp` | 130f:025a, 0326, 04e5, 01c2 dissolve | Frame_Begin, Playfield_Redraw, Scroll_Update, the dissolve | world |
| `cells.h/.cpp` | 1568 | cell dispatch and all 1568 handlers | world |
| `cells2.h/.cpp` | 176a world part | path links, level-2 gate, castle creature | world |
| `player.h/.cpp` | 18cf | player states 0-71, Player_Update/Move, wrap, search start, EndSeq_Setup | player |
| `anim.h/.cpp` | 4306 | slots, scripts, snapshot, sorted draw, erase | player |
| `search.h/.cpp` | 4a03 | search, showcase pick / hand-over, shelf rooms | player |
| `endgame.h/.cpp` | 176a end part | end screen, castle gate, treasure room, rank screen cells | player |
| `rank_chest.h/.cpp` | 130f:0edf, 1261 | rank chest fill, CHAMPION flash | player |
| `elves.h/.cpp` | 4521 | elves | elves |
| `layout.h/.cpp` | 1a54, 46ce:079c | layout, Clue_BuildWords, the srand seed | elves |
| `hints.h/.cpp` | 49a6 | New Player Hints, start/end messages, Flutter | elves |
| `ui.h/.cpp` | 43cb | menus, boxes, buttons, dialogs, menu commands, help, hot keys, text/line wrappers | ui |
| `title.h/.cpp` | 4ba4 | title and attract pages | ui |
| `signin.h/.cpp` | 47c7 | sign-in, USERS.DAT, pack/restore, Game_Start/Restore/End | ui |
| `quiz.h/.cpp` | 46ce | elf questions, deal, promotion, reorder, Clue_Reveal | ui |
| `hud.h/.cpp` | 130f HUD | Screen_DrawOverlay, Hud_Redraw, prompt icon, numbers | ui |

The rank chest is a 130f routine but belongs to the player agent (end flow), hence its own file.
Build lists: `cmake/sources_dos_game.cmake`, `cmake/tests_dos_game.cmake` (one line per file; add,
never reorder or remove). Test files: `test_dos_game_skeleton.cpp` (state, registry, quirk helpers),
`test_dos_mainloop.cpp`, `test_dos_world.cpp` and `test_dos_world_captures.cpp` (DOSBox-X save states
and frame-log replays), `test_dos_player.cpp` and `test_dos_player_end.cpp` (with
`test_dos_player_harness.h`), `test_dos_elves.cpp`, `test_dos_layout.cpp`, `test_dos_hints.cpp`,
`test_dos_ui.cpp`, `test_dos_signin.cpp`, `test_dos_quiz.cpp`, `test_dos_playthrough.cpp` (end to
end); the scenario helper is `tests/dos_game_fixture.h`. Diagnostics of the game (per-tick trace,
DGROUP dumps): `trace.h/.cpp` (mainloop).

---

## 2. Functions not ported, and wave-2 counterparts

| Segment | Original | Engine |
|---|---|---|
| 1000 | `Timer_Install`, `Timer_Remove`, `Timer_ISR` | `DosRuntime` (virtual PIT, `runPitTicksTo`, `gameStep`) |
| 1000 | `Hw_Detect`, `Video_DetectAdapter`, `Video_Init`, `Video_SetMode*`, `Gfx_InitBufferPtrs`, `Gfx_BuildCgaColorMap`, `Vga_SetDac16` | options (`edition/dos_options.h`) and the look's palette (`dos/gfx/palettes.h`); the CGA colour map and each routine's dither phase: the CGA plane of `BackBuffer` (`CgaBinding`, wave 4, `fmt_graphics.md` §2.4) |
| 1000 | `Dirty_AddRect` .. `Dirty_ResetPool`, `Present_Dirty*`, `Present_Full*` | `DirtyRegions`, `presentDirty`, `presentFull` (`dos/gfx/back_buffer.h`) |
| 1000 | `Gfx_FillRect`, `Gfx_ReplaceColorRect`, `Gfx_SetDirtyTracking`, `Scroll_*`, `Tile_DrawColumn` | `BackBuffer::fillRect`, `replaceColor`, `setColor`, `scrollPlayfield*`, `drawTileColumn`; `dos/gfx/scenery.h` |
| 1000 | `Sprite_Draw`, `Sprite_DrawRows`, `Sprite_SaveUnder`, `Sprite_GetHeader` | `drawSprite`, `saveUnder`, `PakFile::frame` (`dos/gfx/blitter.h`, `pak.h`) |
| 1000 | `Font_DrawChar` | `DosFont::drawChar` (`dos/text/dos_font.h`) |
| 1000 | `Map_Unpack`, `Map_UnpackRow`, `Tiles_Unpack` | `PanFile::load` (`dos/gfx/pan.h`) |
| 1437 | `Pal_Apply` | the look's palette at presentation |
| 1460 | `Sound_LoadData`, `StrIndexOf`, `ParseCommandLine`, `Video_GetMode`, `Video_RestoreMode`, `HardErr_Handler`, `Net_IsNetworkDrive`, `File_OpenByIndex`, `File_Read`, `File_Write`, `File_Close`, `Fatal_Print`, `Path_InitDrives`, `StrCopyBounded`, `StrConcatList`, `Mouse_Detect`, `Mouse_Setup` | `SoundBank`/`DosAudioDevice`, `applyDosSwitches`, `DosFiles` (errors name the file), `MouseDriver` |
| 150a, 153e | `Disk_*`, `MsgBox_ShowWaitKey`, `File_HandleError` | none: no floppies, no retry boxes (PORTING_NOTES §6 "drop") |
| 153e:02a7, 43cb:156b | `Null_Handler`, `Null_Handler2` | no-op entries of the dispatch switches |
| 1568:1d92..1d95 | `Cell_NopV58/67/68/69` | no-op entries of the visible dispatch |
| 43a0 | `Kbd_Install`, `Kbd_Remove`, `Kbd_ISR`, `Kbd_CtrlBreakCb` | `PcKeyboard`, `KbdIsr`, `BiosKeyboard` (`dos/input/keyboard.h`, `bios_keyboard.h`) |
| 491b | the sound driver (27 functions) | `Sequencer`, `DosAudioDevice` (`dos/audio/`); the game calls `sound.h` |
| 49a6:0001 | `SoundPort_Out` | part of the sound driver |
| 4a85 | `Input_Poll`, `Input_LookupKey`, `Input_HitZone`, `Mouse_ChangedMask`, `Input_MouseWalk`, `Input_Tick` | `InputPoll`, `inputLookupKey`, `inputHitZone` (`dos/input/input_poll.h`); game wrappers `inputTick` / `inputMouseWalk` (main_loop.h) |
| 4ac9 | the AdLib driver (34 functions) | `opl_backend` (wave 5); `DosAudioDevice` draws the orchestration rand() |
| 4ba4:057d.. | Turbo C `_rtl_creat`, `_rtl_trunc`, `open` | host file API |
| 4c15-4d90 | Turbo C RTL (stdio, string, heap, time, int86, rand) | C++ library; `rand`/`srand` = `BorlandRand`; `time()` = `layoutTimeSeed` |

---

## 3. Runtime model

### 3.1 Lockstep frames

`DosRuntime` runs one *original frame* per game tick (PORTING_NOTES §2.3): for game tick n it
applies the --keys events of tick n and every keyboard byte due (Kbd_ISR, BIOS), clears the step
gate, starts the original frame clock, then calls

1. `DosGame::frameBegin()` — `Frame_Begin` 130f:025a (camera.h) in the main loop; skipped while the
   title runs (`g_uiMode & 0x80`: the title loop has no Frame_Begin, and the runtime already reset
   `g_stepDone`). Wired by the skeleton; **mainloop** refines it (dissolve frames, §3.4).
2. `DosGame::gameTick(info)` — `gameTick(g, info)` (main_loop.h) = `Game_Tick` 130f:00fe without its
   `g_quit` test and its two counters (the runtime does both: `g.counters()`).
3. `DosGame::frameBody()` — `mainFrameBody(g)`: the loop body of `main` after Frame_Begin, ending
   with `Sprites_DrawSorted`, `Frame_Present`, `Sprites_EraseAll`; or `titleFrame(g)` while the title
   runs.

and every PIT tick `DosGame::soundTick()` (the facade's `pitTick`). `g_quit` (`core.quit`) ends the
run after the frame (`quitRequested()`); the original also sets `g_quit` around disk I/O and scene
loads to freeze the tick — the engine's loads are instant, so code that sets it to 1 and back to 0 in
the same call can drop both writes (keep them if it is simpler: the runtime only looks at the end of
a frame). The ticks the original lost in that I/O are lost through the frame timing instead (§3.9).

The original order inside `Game_Tick` (it fixes the order of the `rand()` calls, `code_runtime.md`
§3.3, §12): not gated: player screen offsets, `mousePoll`, `inputTick`, `animTick`, then with
`g_loadedScene == g_level` (or the title) `inputMouseWalk`, `playerUpdate`, `elvesUpdate`,
`cellsUpdateVisible`; gated: `animTick`, `inputTick`. The frame body's rand users (title sparkles,
quiz, layout, dissolve, elf spawn in `sceneUpdate`) run after the tick of the same game tick.

### 3.2 Step gate, slow machines

`g_stepDone` is `g.stepGate()` (runtime-owned): `animTick` marks it when a script shows a new frame,
`playerMove` when the camera moves, `frameBegin` (and Title_Run) clear it. In lockstep it only blocks
inside a long frame (§3.9); `RuntimeOptions::frameMode = SlowMachine` makes several ticks share one
frame, and the gated path then runs as in the original.

### 3.3 The title, the sign-in, the game

`main` calls `Title_Run` (a blocking loop) before its frame loop. The engine splits it:
`titleStart(g)` once (mainloop's start-up, after `gameInit`), then `titleFrame(g)` per frame while
it returns true, then `titleFinish(g)` and `g_uiMode = 8` (sign-in), after which frames run
`mainFrameBody`. The sign-in, the game and the end flow then live in the one frame loop as in the
original (modes as bits of `g_uiMode`).

### 3.4 Blocking waits

Lockstep has no nested loops: a blocking wait of the original becomes a state that spans frames.

| Original | Wait | Engine |
|---|---|---|
| `Button_Flash` 43cb:0eb6 | 4 x `Delay_Ticks(18)` = 72 PIT ticks, ticks running | ui: a flash state advanced by `g.ticks()` (`uiFlashPending` / `uiFlashContinue`, ui.h); then the confirm handler and the tail of the interrupted updater. Wired (integration): `DosGame::frameBegin` skips Frame_Begin while it runs and keeps `g_stepDone` across its frames (`flashGate`), `mainFrameBody` stops after the updater that started it and then only continues it, and resumes the frame after that updater when it ends (after SignIn_Update with the other three updaters, after Menu_Update with Scene_Update). The game ticks run on during the wait, as in the original. A headless run also finishes a flash (or a dissolve) the last keys started |
| `Delay_Ticks` 43cb:0ee8 | busy wait | only used by `Button_Flash` |
| `MsgBox_ShowWaitKey` 153e:0000 | disk boxes | not ported |
| I/O with `g_quit = 1` (`Users_ReadFile/WriteFile`, `Scene_Update`, `Quiz_LoadQuestions`, `SignIn_Update`) | ticks suspended during the disk access | instant: nothing to wait for |
| the dissolve (`Frame_Present`) | 350 presents in one frame (127 ms in DOSBox-X, ~6 game ticks) | world: `transitionDissolve` returns true while cells remain (`g.frameClock().steps(FrameFeature::Dissolve)` cells per frame); mainloop decides how the frames in between behave (suggested: treat them as one long original frame: while it runs, `DosGame::frameBegin` re-marks the gate after the runtime cleared it (`g.stepGate().mark()`, so ticks take the gated path as they did behind the long DOSBox-X frame) and the frame body only continues the dissolve) |

### 3.5 Per-frame features

Features that count main-loop frames take their step count from `g.frameClock()` (wave 2:
`OriginalFrameClock`, PORTING_NOTES §2.3): `CoinCountUp` (Hud_Redraw), `ChampionFlash`
(RankChest_Update), `TitleSparkle` (titleFrame), `Dissolve` (transitionDissolve). The search's frames
are timed by main_loop.cpp at the reference speed (4.25 / 4.65 PIT ticks each, wave 4: §3.9). In
`frame_speed = period` every feature runs one step per frame. Wave 4 (ui): at the reference speed the
coin count-up (hud.cpp: a coin every 3.32 PIT ticks from the tick that changed g_coins) and the title
loop (title.cpp: its iterations on the reference machine's clock, page draws of 7.5-14.3 PIT ticks,
time-outs on their PIT interrupt, sparkle attempts at 7.3 per PIT tick from 0.75 PIT ticks after the
tick that ended the last sparkle; UiState::titleLoopAt) keep their own clocks (`code_ui.md` §8.5,
§11.2); `CoinCountUp` and `TitleSparkle` of the frame clock remain the period-speed steps.

### 3.6 Drawing and presentation

All drawing goes into `g.back` (segment 2005) with the wave-2 primitives and the module wrappers
(ui.h: `textDraw`, `gfxHLine`, ...). `framePresent` copies to `g.screen` (`presentDirty` /
`presentFull`, the dissolve); the host shows `g.screen` through `g.palette` (`toRgba32`). Sprites are
drawn after all background drawing and erased after the present (`spritesDrawSorted` /
`spritesEraseAll` over `g.sprites`), so `g.back` holds only the background between frames.

`g.pan` is the PAN in the map/tile buffers, `g.prevPan` the one before it (set by
`sceneLoadPan`): CLOSE.PAN (5 blocks) does not overwrite MOUNT3's blocks 0x2A/0x43, which the chute
cells 3D/4D show (`code_world.md` §6.10); the world agent draws such cells from `prevPan` when the
block lies past the loaded PAN's blocks. `g.bank` is the loaded PAK (frame codes: `g.bankFrame`).

### 3.7 Randomness

One `g.rand` (Turbo C LCG, seed 1). `srand` only in `layoutGenerate`, with `layoutTimeSeed(g)`
(tests set `g.layoutSeedOverride`). On AdLib `Music_Play` draws one rand() (the device draws it
through the function `runDosEdition` passes; wire it to `g.rand`). No engine-side code may draw from
it.

### 3.8 Sound

Game code calls `sfxPlay(g, id)`, `musicPlay(g, tune)`, `setMusicOn(g, on)` ... (sound.h): the call
reaches the facade and the DGROUP copy (`core.soundOn`, `musicOn`, `sfxLoop`, `musicLoop`,
`musicGate`) changes where the original writes it. `RecordingSound` records calls for tests;
`DosAudioSound` drives `DosAudioDevice`. `sfxBlockActive(g)` is DS:6448 != 0.

### 3.9 Long frames and disk I/O (wave 4: the reference frame timing)

At `frame_speed = reference` (the default) a frame lasts as long as on the reference machine
(PORTING_NOTES §2.5). The game reports each frame's cost to the runtime: `g.rt.frameTiming()`
(`FrameTiming`, dos_runtime.h: `quitSection(units)` for I/O with g_quit = 1, `spend(units)` for
drawing, `fromPreviousTick()` when the work was asked for before the frame's tick; units are
thousandths of a PIT tick). `frameBeginTiming(g)` (main_loop.h, called by `DosGame::frameBegin`)
reports what Frame_Begin's latches start: a scene the ticks asked for (`DosGame::latchedLoad`) and a
full play-field redraw (`latchedRedraw`); `mainFrameBody` reports the rest from what the frame did
(`FrameCosts`: USERS.DAT writes, Game_Start's Quiz_LoadQuestions, PAN loads, a full HUD redraw, a
message or hint bubble opening). The runtime then runs the game ticks that fall inside the frame
without Frame_Begin or a frame body (so `g_stepDone` gates a tick after one that advanced a script or
moved the camera) and skips Game_Tick, its counters and the trace row of a tick inside the I/O. The
dissolve and a Button_Flash are long frames `DosGame` spreads over its frames itself
(`dissolveRunning`, `flashGate`): `framePresent` sets the cells of a dissolve's first frame so that it
ends in the frame of the last tick before its end on the reference machine (`setDissolveStart`: after
the frame's other work, 3.15 PIT ticks of redraw and 5 for a tune the teleporting tick started, then
30), the following frames take the frame clock's rate. The search runs as many Search_Update calls per
frame as reference search frames fit (`DosGame::searchClock`). `frame_speed = period` keeps strict
lockstep. `DosRuntime::resumeAt(g_ticks, PIT ticks to the next game tick)` starts the time base at a
DOSBox-X state's moment (for `loadDosboxState`).

Wave 4 integration: a frame whose work counts from the previous tick begins when the frame before it
has ended if that is later (`FrameTiming::startOffset`; scroll frames of 6.5 / 7 / 7.5 PIT ticks are
reported by `scrollUpdate`): r2/fl_bridge_l3's gated tick 1111 and the champion's arrival in
r2/fl_crown_wait. A scene change's sprite bank costs 0.019 PIT ticks per 1000 bytes with the PAN, a
full HUD redraw in a dissolving frame 6 PIT ticks, the dissolve's redraw head 1; the tune that delays a
dissolve by 5 PIT ticks is a Music_Play with the music on in the teleporting tick (`DosGame::musicPlays`).
**Presentation**: a long frame's picture appears when the frame ends, as the reference machine
presents at the end of a frame: `runDosEdition` (and the frame and state dumps) shows the screen from
the last game tick before `DosRuntime::frameEndUnits()`, or before `DosGame::screenShownAt` for the
title loop's page draws, which keep their own clock; until then the previous picture stays.

---

## 4. Shared-state conventions

### 4.1 Sections and writers

Each `GameState` section belongs to one agent (it adds fields only there, with the DS address and a
`visit` line). Most globals are written by their section's module; the game-wide ones in `core` are
written by many modules exactly as in the original:

| Global (field) | Written by (modules) |
|---|---|
| `g_uiMode` (`core.uiMode`) | title, main loop (8), input tick (0x40/0x20), menus, dialogs, hints (0x10), messages (4), elves (2: a catch), quiz, sign-in, Game_Start |
| `g_coins`, `g_nets` | player (coin drop), elves (catch, dust, coin creature), quiz (reward on close), cells (bonus, creature, net shop), sign-in (load, pack) |
| `g_keyState` | cells (boundaries, ice slide, castle top), search (key found), sign-in (new game, restore) |
| `g_level`, `g_area` | cells (doors, boundaries, gates), endgame, sign-in; area also player (states 29/30), title (6), sign-in (4) |
| `g_treasuresFound`, `g_tier` | search; layout (tier), sign-in |
| `g_endState`, `g_menuLock`, `g_gameInProgress`, `g_caughtSlot`, `g_shopCoinsOwed` | endgame/scene; cells/player/search/endgame; hints/menus; elves/quiz; cells/player/sign-in |
| `g_fullRedrawReq`, `g_transition` (`world.*`) | cells and endgame (teleports, snaps, picture changes), player (PlrSt_04 snap); `g_fullRedrawReq` cleared by Playfield_Redraw, `g_transition` by Frame_Present (the dissolve) |
| `g_hudRedraw` DS:922E, `g_hudRedrawReq2` DS:925B (`ui.*`) | menus, dialogs, hints, messages, scene, search (key), sign-in, endgame (crown) |
| `g_promptIcon` (`ui.promptIcon`) | input mouse-walk, quiz, menus, hints/messages, scene, sign-in |
| `g_dirty` (`core.dirty`) | every drawing routine (`+= 1` or `= 1` as the original) |
| `g_keys` (`g.keys()`) | Kbd_ISR, mouse walk, Player_Update (mask), cells (creature hit), player (falls), endgame (treasure room), menus (`&= 0xFFF0`) |
| `g_cmd` (`g.input.cmd`) | Input_Poll; consumed (`= -1`) by each UI handler, mouse walk, hot keys, Input_Tick, title |
| cell grids (`world.cellPicture/Content/Type`) | layout, cells (gates, bridges, shop, bonus), search (blink, searched bit), sign-in (restore) |
| sprite slots (`player.slots`) | anim, player, cells (decorations, creature, showcase), elves (6-11), endgame, search (slot 2), title (1, 2), mouse poll (0) |

### 4.2 Globals held by wave-2 objects

| DS | Global | Engine |
|---|---|---|
| 9105 | `g_keys` | `g.keys()` (`KbdIsr::keys`) |
| 9257 | `g_jumpFromDiagonal` | `g.jumpFromDiagonal()` |
| 92FA | `g_ctrlReleased` | `KbdIsr::ctrlReleased` (no reader) |
| 910D | `g_stepDone` | `g.stepGate()`, `g.stepDone()` |
| 90FE, 9100, 9115 | `g_tick13`, `g_secondTicks`, `g_seconds` | `g.counters()` (`TickCounters`: `cycle13`, `secondCountdown`, `seconds`) |
| 3D2B | `g_ticks` | `g.ticks()` |
| 8F48 | `g_randSeed` | `g.rand` |
| 9226, 90F6, 9238, 9285, 922C | `g_cmd`, `g_cmdQueued`, `g_cmdConsumed`, `g_lastBiosKey`, `g_inputContext` | `g.input.cmd`, `queuedCmd`, `deliverNext`, `lastKey`, `context` |
| 911A, 911E, 90F4, 925E, 9266, 9143, 92AC, 92AE | mouse | `g.mouse` (`MousePointer`); the driver `g.rt.mouse()` |
| 7D84..80DA, 6194 | key, command and zone lists | `g.inputTables` (zones and keys rewritten by Box_SetupInput, SignIn_SetupZones, the title's DS:80DA) |
| 9145, 92DC, 92F6, 92F8, 929F, 37C8..37CC | draw records, order, flags, save heap | `g.sprites` (`SpriteRenderer`) |
| 37DA..37E0 | clip window | `g.back.clip()` |
| BA14 | `g_usersFile` | `g.users.users()` (`UsersDat`); writes `g.users.write(moment)` / `usersWriteFile` |
| 9228 (+ 8FB6, 8FD2, 9101, 9107, 9138, 91FD, 9209, 921A, 927E) | `g_userRec` and the record field pointers | `g.ui.userRec` (index), `g.rec()` (`UserRecord&`: `rec().totalTreasures` for `*g_totalTreasures`, `rec().quizHistory` for DS:8FD2, ...) |
| 5FDC, 5FDE | `g_quizTier5Start`, word 6 | `g.quizTiers` |
| 9325, B204 | HELP.DAT, PRG_DICT | `g.help`, `g.dictionary` |
| 37BC, 37B4, 37B8 | sprite bank, tile and map buffers | `g.bank`, `g.pan` (and `g.prevPan`) |

### 4.3 Objects, slots, masks

* `g.world.curObj` (`g_curObj`) holds `kPlayerObjOff` or `elfObjOff(i)`; `objAt(g, g.world.curObj)` is
  the object; `elfIndexOf` its elf index. Visible cell calls see the stale value (`code_world.md`
  §4.3): never set it in `cellsUpdateVisible`.
* The player object is `g.player.obj` (`g_playerDX/DY` = `obj.dx/dy`); elf records `g.elves.elves[i]`
  (`obj`, `base`, `state`, `question`, `spriteSlot`).
* Slots: `g.player.slots[k]` (`SpriteSlot`), slot 1 player, 2 net/coin/content, 3 showcase, 4 end
  sequence, 6/8/10 elves (children 7/9/11), 12 cell decoration, 0 mouse pointer.
* Cell handlers return the mask (`uint16_t`) and keep `g.world.cellMask`; `callCellObjectHandler` /
  `callCellVisibleHandler` (cells.cpp) dispatch every type, including the 176a handlers of cells2.h
  and endgame.h.
* Widths and signedness follow the original (the specs mark signed / unsigned compares): fields are
  typed as the original's accesses (a scan of every access confirmed the widths, §8).

### 4.4 Strings and writable copies

`g.dsString(off)` returns the NUL-terminated string at DS:off as the game would read it: the
writable message texts (`elves.messageText`, DS:6BC0..7CA2, patched by `Gameover_UpdateMessages`),
the quiz buffers (`ui.answerText`, `ui.questionText`, `ui.expandedText`), the typed name, the record
names, else the EXE's bytes. Far string pointers in the state are `DsPtr` (`ui.praiseWord`,
`ui.buttonLabel`, `ui.puzzleText` into `expandedText`). The animation scripts are a writable copy
too (`player.animScriptData`, the DS:48DE patch): read them with `animScriptWord`.

### 4.5 Saving

Saves happen only at the original's moments through `usersWriteFile(g, DosSaveMoment)` (signin.h):
new player, remove player, Start Over, Start New Player, Exit and Save, Game_End. `Users_Pack` /
`Game_Restore` copy between the state and `g.rec()` (`code_ui.md` §9.8); the header word is
`g.sprites.saveHeapHigh` (DS:929F, `UsersDat::spriteHighWater`).

---

## 5. Memory-layout quirks and their helpers

The original reads past tables in a few places the specs rely on; the helpers in `game_state.h`
reproduce them (tests in `test_dos_game_skeleton.cpp`):

| Quirk | Where | Helper |
|---|---|---|
| unmasked cell index (x = -1 gives column 0x1FF) | Cell_GetMask, Cells_UpdateVisible, Frame_Begin | `cellIndexOf(x, y)` |
| `g_cellType[256..]` reads `g_cellContent` | Cells_UpdateVisible at the strip end | `cellTypeRaw(s, idx)` |
| `g_cellPicture[-1]` (DS:0CA8) and `[256..]` (g_cellType) | Search_CanSearch at the strip ends | `cellPictureRaw(s, t, idx)` |
| `g_hidingCells[-1]` = `g_cellContent[255]` | Treasure_PickShowcase | `hidingCellAt(s, t, -1)` |
| `g_menuWrap[-2..5]`: -2, -1 are the last bytes of `g_menuStepV` | Menu_Update | `menuWrapAt(t, i)` |
| Quiz_ResetHistory writes one byte past the 90-byte history (record +0x66, slot 0's low byte) | Quiz_ResetHistory | `historyByte` / `setHistoryByte(rec, 90, v)` |
| script 0x88's frame word patched at run time | end sequence | `animScriptWordAt(s, dg::kAnimScript88Frame)`, `animScriptWord` |
| message texts patched for the session | Gameover_UpdateMessages | `messageTextByte(s, ds)`, `g.dsString` |
| stale `g_curObj` in visible cell calls (elf 14 turns at the geyser) | Cells_UpdateVisible | keep `world.curObj` as state |
| stale `g_elfAnimSel` read by ElfSt_03 | elves | keep `elves.elfAnimSel` as state |
| Elves_Spawn keeps x, y, dx and the sprite slot | elves | the records persist in the state |
| negative z draws the previous pick again (owner \| 0x8000 offset) | Sprites_DrawSorted | `SpriteRenderer` draws it at its own position (documented deviation) |
| the save-heap high-water word DS:929F is USERS.DAT header bytes 1-2 | Sprites_DrawSorted, Users_Pack, Game_Restore | `g.sprites.saveHeapHigh` <-> `UsersDat::spriteHighWater` |
| `g_shackNoCase` never cleared | treasure room | state field |
| stale PAN buffers after a smaller PAN | the chute | `g.prevPan` |
| a byte at DS:913E (`g_screen`) next to the word at DS:913F | painters / hints | typed fields (§8) |

---

## 6. Testing a module

* `tests/dos_game_fixture.h`: `dosgame::GameFixture f(*gameFiles)` (after
  `TM_REQUIRE_DOS_GAME_FILES()`) builds a DosGame on TM_DATA_DOS with a `RecordingSound`, a users
  store in a temporary file (written only at save moments, removed afterwards), the headless
  runtime and `gameInit`. Set a scene up with `loadUsers(path)` (a `savestates/*.users` or
  `USERS_*.DAT` snapshot), `restoreRecord(n)` (Users_LoadPlayer + Game_Restore),
  `placePlayer(x, y, camX, camY, level, state)` (direct state), `setScript("55:down:right, ...")`
  (game ticks, relative to now), then `auto log = f.step(n)`.
* `TickRecord` holds what framelog.py's STEP record holds (x, y, camera, state, keys, coins, plus
  nets, level, uiMode, gated) **after** each tick; a DOSBox-X STEP record is taken at the start of a
  tick, so engine record n matches log record n + 1. `GameFixture::changes(log)` prints the
  stepchanges.py view. Reference logs: the `savestates/r2/fl_*` states of the captures
  (`tools/dos/stepchanges.py`, `tools/dos/fl2csv.py`).
* **The captures variable.** `TM_DOS_CAPTURES` names the reference material
  (`extracted\captures\dos`: `ega\`, `tandy\`, `savestates\`, `USERS_*.DAT`, `audio\`, `avi\`);
  `testdata::dosCapturesPath()` / `testdata::dosCapture("savestates/r2/fl_walk2.sav")` and
  `TM_REQUIRE_DOS_CAPTURES()` (`tests/dos_test_data.h`) read it, and every capture-dependent test
  skips without it (there is no guess from TM_DATA_DOS any more).
* **Starting from a DOSBox-X state.** `DosboxState::load(sav)` (`dos/save/dosbox_state.h`) reads a
  state: guest RAM, DGROUP and its relocation, the back buffer, the screen from the video memory
  (EGA planes / the Tandy page), the CPU registers, the BIOS key buffer. `loadDosboxState(g, s)`
  (`dos/game/load_state.h`, after constructing the fixture) puts the game at that moment: DGROUP, the
  service-held globals, the PANs and the sprite bank found in RAM, the back buffer (sprites of a
  frame in progress erased from the save heap), the dirty spans, the screen, the rand seed and the
  PIT phase (the next game tick on the interrupt DS:3D34 says, one PIT tick earlier when the state was
  saved inside the timer interrupt before its countdown). Then `f.setScript(...)` and `f.step(n)`
  replay a log from its start state: record e (after tick e) is the log's record e + 1 of the same
  g_ticks. `composeShownFrame(g)` draws what the original showed (the back buffer and the sprites of
  the draw records) to compare with `s.screen()`. The CLI does the same with `--dos-load-state`
  (§10). Tests: `test_dos_load_state.cpp` (every reference state: the DGROUP round trip, the files,
  the shown frame, the phase), the replays of `test_dos_world_captures.cpp` and `test_dos_elves.cpp`.
* State comparison: `dumpToDgroup(g)` gives DS:0000..CC1F in the original's layout;
  `diffDgroup(a, b)` names the differing fields (far pointers by offset only); `loadFromDgroup`
  loads a DOSBox-X DGROUP (`DosboxState::dgroup()`, or `tools/dos/savmem.py dgroup`) into a
  GameState. The registry: `gameStateFields()`, `serviceFields()`.
* Rules: tests store sizes, counts, hashes, coordinates and per-tick numbers only (no text, no
  pixels); data-dependent tests skip without TM_DATA_DOS (and TM_DOS_CAPTURES); one test file per
  area.

---

## 7. Cross-module calls

| Module | Calls into |
|---|---|
| main_loop | camera (frameBegin, playfieldRedraw, scrollUpdate, transitionDissolve), scene, search, quiz, hud, rank_chest, signin (signInUpdate), hints (gameoverUpdateMessages, hintsUpdate), ui (menuUpdate, gameHandleHotkeys), anim (animTick, spritesDrawSorted/EraseAll, animSet...), player (playerUpdate), elves (elvesUpdate), cells (cellsUpdateVisible), title (titleFrame) |
| init | scene (loadSpriteBank), anim (animInitSlots) |
| scene | signin (gameEnd, gameStart), layout (cellsResetVariable, layoutClearSlots), quiz (quizReorderSlots), elves (elvesSpawn), hud (hudDrawPromptIcon), ui (menuBarDrawTitle, msgBoxDraw for floppies only), sound |
| camera | anim (spritesSnapshot) |
| cells, cells2 | anim (animSet, animSetIfIdle, animClear), player (endSeqSetup, playerState writes), search (treasurePickShowcase), endgame (cellP71V71ToRankScreen, the 176a handlers via the dispatch), elves (elfTurn, elvesShiftNearby), sound |
| player | cells (cellGetMask), anim, search (searchCanSearch, treasureAddCollected), layout (layoutUnmarkSharedCells), sound |
| search | anim (animClear), sound |
| endgame | anim (animInitSlots, animSet, animClear), cells (cellV7), sound |
| rank_chest | ui (textDraw), hud (hudDrawNumber), sound |
| elves | anim, cells (cellGetMask), player (objWrapWorldX) |
| layout | – (Clue_BuildWords, which it calls, is declared in layout.h) |
| hints | hud (screenDrawOverlay), ui (textDraw) |
| ui | hud (screenDrawOverlay), signin (usersPack, usersWriteFile, usersLoadPlayer, gameStart, dialog handlers), scene (sceneUpdate), anim (animInitSlots), main_loop/sound |
| title | scene (sceneLoadPan), signin (screenDrawBackdrop), anim, ui (textDraw, gameHandleHotkeys), main_loop (framePresent), sound |
| signin | scene (sceneLoadPan, loadSpriteBank via spritesLoadBegin), layout (layoutGenerate, layoutClearSlots, cellsResetVariable, layoutMirrorSharedCells, castleApplyLayout, cellsAdjustForCpu), quiz (quizLoadQuestions), camera (frameBegin), ui (msgBoxDraw, buttonDraw, textDraw), hud (screenDrawOverlay), sound |
| quiz | hints (flutterShow), hud (hudRedraw), ui (textDraw, gfxFrameRect), sound |
| hud | camera (playfieldRedraw), search (shelfRoomDrawShelf), anim (spritesDrawSorted), ui (menuBarDrawTitle, textDraw), signin (signInDraw), quiz (quizDrawScreen), sound |

A function still a stub in another module is coded against its declared contract; test your module
with its own scenario set-up.

---

## 8. Known gaps and deviations (after wave 4)

Fixed in the wave-3 integration: the effect loop flag DS:63B8 reaches the driver
(`DosAudioDevice::setSfxLoop`); the Button_Flash wait is wired into the frame loop (§3.4);
`runDosEdition` runs the game (the wave-2 preview is behind the hidden `--dos-preview`).

Done in wave 4 (fidelity, §3.9, §10, §11):
* the reference frame timing: scene loads, USERS.DAT writes and Quiz_LoadQuestions lose the ticks the
  logs lose; full HUD and play-field redraws, bubbles, scroll frames, the search frames and the
  dissolve last as long as on the reference machine; a frame begins when the one before it ended; the
  picture of a long frame shows when it ends;
* the title loop on the reference machine's clock (page draws, time-outs on their PIT interrupt, the
  sparkle attempts), the coin count-up at 3.32 PIT ticks per coin, main's first frame in the title's
  last tick interval;
* the mouse driver's start per look, sub-tick key times, the grey-key artefact as a CLI switch;
* the CGA look's 2-bpp picture with each routine's dither phase (`BackBuffer`'s CGA plane);
* starting from a DOSBox-X state (`--dos-load-state`): the time base, the sound driver's effect and
  tune where they were (`soundRestoreDriver`), the clip window of a state saved inside
  Sprites_DrawSorted.

Still open (each with its evidence in §11):
* **The order of a frame against its tick.** The engine runs Frame_Begin, the tick, the frame body;
  on the reference machine a tick usually lands in the drawing part of a frame, so the next
  Frame_Begin is the first to see it (the engine's order is the original's rarer case). Visible only
  where a picture freezes: the walking elf's feet under the hint bubble after a search (129 pixels in
  `level1/treasure5_found` and `treasure_found_avi`: the bubble's HUD redraw draws the actors from a
  snapshot taken before the elf's last step); the castle-top tick always removes the HUD key icon,
  which 4 of 5 reference runs keep (masked in the castle_end scenarios). Changing the order means
  re-deriving the long-frame model (which frames count from the previous tick) and moving the
  scenario ticks; left for a later wave.
* The hint bubble that opens right after a search begins after the search's last frame on the
  reference machine: r2/fl_search_t blocks 4 ticks behind it, the engine gates 3 (the end state
  matches).
* Sound calls inside a long frame take effect at the frame's start (`tandy_r3_session.avi`: the
  resumed game's tune 23 PIT ticks early, the tune after Flutter's first hint 10 early); a per-command
  delay in `DosAudioDevice` with costs reported as the frame goes would fix it (`fmt_sound.md` §11.6).
* Not measured, so not modelled: the cost of menus, Yes/No and OK boxes, the quiz screen, the sign-in
  and the rank screen (no log gated a tick behind them); the occasional long Game_Tick (r2/fl_climb,
  4 PIT ticks at the level boundary); diagonal and slow-CPU scroll frames.
* The search frames in the three EGA AVIs of a search last 4.9-5.5 PIT ticks, in every logged run
  4.25 / 4.65 (the model): `level1/treasure_found_avi`'s timing (AVI 113 ticks from the throw to the
  hint, engine 105) is kept as off; the logged runs of the same search agree with the engine.
* The AdLib music is a square-wave stand-in until the OPL2 core (wave 5); effect timing was compared
  on the Tandy only.
* Not carried over from a DOSBox-X state: the frame clock's fractions, an engine-side wait in progress
  (a Button_Flash or a dissolve), the rest of a Game_Tick the state was saved inside (5 of the 124
  states: the engine starts with the next tick), the slow-CPU box save.

Deviations kept on purpose:
* `Elves_Spawn` leaves `g_curObj` one record past the last spawned elf; with all 15 spawned that is
  past the table (DS:39DA), which a `DsPtr` cannot name, so the engine keeps record 14. The next
  tick's `Player_Update` or `Elves_Update` overwrites it before anything reads it.
* The stale map/tile buffers are modelled as two PANs (`g.pan`, `g.prevPan`), exact for every
  reachable case found (MOUNT3 then CLOSE, CLOSE then SHACK, OPENING then a scene).
* `Cell_P0_Void` counts the border colour (INT 10h overscan) in `g_borderColor` but nothing shows it.
  Writes past the 256 cells of `g_cellPicture` (the original lands in `g_cellType`) are dropped;
  only a corrupt state reaches them.
* Slow CPU (`-X`): the save-heap handle MsgBox_Draw writes into DS:37C8/37CC is not modelled
  (`DosGame::boxSave` holds the saved background); no reference shows it.
* Floppy-only paths (box 22 when `g_startDrive < 2`) are kept but never run (`g_startDrive` = 2).
* `Flutter_Show` draws nothing when no sprite bank is loaded (never in play).
* The test seam `setTerrainMaskHookForTests` (player.h) is never set by the game; 12 test cases of
  the player harness (`tests/test_dos_player_harness.h`) run the player modules without the world's
  cell handlers through it.
* The reference composer (`dos_screens`, `--dos-preview`) converts CGA with the tile rule only; the
  game's buffers keep the CGA plane.

Not observable yet: the elf-14 turn at the geyser top from the stale `g_curObj` (no tier-3 capture of
it). Field widths were checked against every direct access in the disassembly (`g_fullRedraw` DS:8FBB
a word, `g_screen` DS:913E a byte, the box defaults DS:5A32/5A42/5A52 words).

---

## 9. Function map

Every game function of the original, its engine function and file, the owning agent and the status
after wave 3 (generated from the module headers by
`extracted/scratch/dos_w3_integration/gen_status.py write`; "done" = ported from the spec, notes name
deviations, §8). Engine helpers (dispatch switches, the srand seed, the flash wait) have no address of
their own. `Title_Run` is split into three.

| Address | Original | Engine function | File | Owner | Status |
|---|---|---|---|---|---|
| 1000:19cf | `on` | `tileDrawColumn` | `camera.cpp` | world | done |
| 1000:1b8d | `as` | `drawBankFrame` | `ui.cpp` | ui | done |
| 130f:0007 | `main` | `mainStart` | `main_loop.cpp` | mainloop | done (main before its loop) |
| 130f:0007 | `main` | `mainFrameBody` | `main_loop.cpp` | mainloop | done |
| 130f:00db | `Game_Shutdown` | `gameShutdown` | `main_loop.cpp` | mainloop | done |
| 130f:00fe | `Game_Tick` | `gameTick` | `main_loop.cpp` | mainloop | done |
| 130f:01c2 | `Frame_Present` | `framePresent` | `main_loop.cpp` | mainloop | done; long frames: the reference frame timing (§3.9) |
| 130f:01c2 | `Frame_Present` | `transitionDissolve` | `camera.cpp` | world | done (7 frames at the reference speed) |
| 130f:025a | `Frame_Begin` | `frameBegin` | `camera.cpp` | world | done |
| 130f:0326 | `Playfield_Redraw` | `playfieldRedraw` | `camera.cpp` | world | done |
| 130f:04e5 | `Scroll_Update` | `scrollUpdate` | `camera.cpp` | world | done |
| 130f:07a5 | `Hud_Redraw` | `hudRedraw` | `hud.cpp` | ui | done |
| 130f:0c2e | `Hud_DrawPromptIcon` | `hudDrawPromptIcon` | `hud.cpp` | ui | done |
| 130f:0c73 | `Screen_DrawOverlay` | `screenDrawOverlay` | `hud.cpp` | ui | done; slow CPU: restores DosGame::boxSave |
| 130f:0d00 | `Hud_DrawNumber` | `hudDrawNumber` | `hud.cpp` | ui | done |
| 130f:0d94 | `Mouse_Poll` | `mousePoll` | `main_loop.cpp` | mainloop | done |
| 130f:0edf | `RankChest_Update` | `rankChestUpdate` | `rank_chest.cpp` | player | done |
| 130f:1261 | `RankChest_DrawStar` | `rankChestDrawStar` | `rank_chest.cpp` | player | done |
| 1437:0034 | `Scene_Update` | `sceneUpdate` | `scene.cpp` | world | done |
| 1460:0003 | `Game_Init` | `gameInit` | `init.cpp` | mainloop | done (hardware, floppy and file parts are services, §2) |
| 1460:02c6 | `Sprites_LoadBegin` | `spritesLoadBegin` | `init.cpp` | mainloop | done |
| 1460:033a | `Game_InitVars` | `gameInitVars` | `init.cpp` | mainloop | done |
| 1460:05e8 | `Scene_LoadPan` | `sceneLoadPan` | `scene.cpp` | world | done; stale buffers = loaded + previous PAN (two-PAN model) |
| 1568:0008 | `Cell_P5_CaveDoorW` | `cellP5CaveDoorW` | `cells.cpp` | world | done |
| 1568:00ce | `Cell_P6_CaveExitW` | `cellP6CaveExitW` | `cells.cpp` | world | done |
| 1568:0134 | `Cell_GroundCheckObj` | `cellGroundCheckObj` | `cells.cpp` | world | done |
| 1568:01b4 | `Cell_P9_CaveDoorE` | `cellP9CaveDoorE` | `cells.cpp` | world | done |
| 1568:0203 | `Cell_GroundCheckPlayer` | `cellGroundCheckPlayer` | `cells.cpp` | world | done |
| 1568:029d | `Cell_P10_CaveExitE` | `cellP10CaveExitE` | `cells.cpp` | world | done |
| 1568:0303 | `Cell_P34_CaveUp` | `cellP34CaveUp` | `cells.cpp` | world | done |
| 1568:0341 | `Cell_P35_UpperRoomDown` | `cellP35UpperRoomDown` | `cells.cpp` | world | done |
| 1568:0386 | `Cell_P11_ClubDoor` | `cellP11ClubDoor` | `cells.cpp` | world | done |
| 1568:03f6 | `Cell_P12_StartRoom` | `cellP12StartRoom` | `cells.cpp` | world | done |
| 1568:05cb | `Cell_P7_WallLeft` | `cellP7WallLeft` | `cells.cpp` | world | done |
| 1568:0638 | `Cell_P8_WallRight` | `cellP8WallRight` | `cells.cpp` | world | done |
| 1568:06a5 | `Cell_P50_ShelfEnter` | `cellP50ShelfEnter` | `cells.cpp` | world | done |
| 1568:0718 | `Cell_P51_ShelfExit` | `cellP51ShelfExit` | `cells.cpp` | world | done |
| 1568:078a | `Cell_P52_Shelf0to1` | `cellP52Shelf0to1` | `cells.cpp` | world | done |
| 1568:07fc | `Cell_P53_Shelf1to0` | `cellP53Shelf1to0` | `cells.cpp` | world | done |
| 1568:0880 | `Cell_P54_Shelf1to2` | `cellP54Shelf1to2` | `cells.cpp` | world | done |
| 1568:08f2 | `Cell_P55_Shelf2to1` | `cellP55Shelf2to1` | `cells.cpp` | world | done |
| 1568:0976 | `Cell_P65_Shelf2to3` | `cellP65Shelf2to3` | `cells.cpp` | world | done |
| 1568:09e8 | `Cell_P66_Shelf3to2` | `cellP66Shelf3to2` | `cells.cpp` | world | done |
| 1568:0a6c | `Cell_P30_V30_TrunkTop` | `cellP30V30TrunkTop` | `cells.cpp` | world | done |
| 1568:0b38 | `Cell_P17_MineDoorW` | `cellP17MineDoorW` | `cells.cpp` | world | done |
| 1568:0b86 | `Cell_P37_MineExitW` | `cellP37MineExitW` | `cells.cpp` | world | done |
| 1568:0bfb | `Cell_P18_MineDoorE` | `cellP18MineDoorE` | `cells.cpp` | world | done |
| 1568:0c49 | `Cell_P38_MineExitE` | `cellP38MineExitE` | `cells.cpp` | world | done |
| 1568:0cbe | `Cell_P44_CastleDoor` | `cellP44CastleDoor` | `cells.cpp` | world | done |
| 1568:0d94 | `Cell_P23_FallShaft` | `cellP23FallShaft` | `cells.cpp` | world | done |
| 1568:0df3 | `Cell_P47_Boundary01` | `cellP47Boundary01` | `cells.cpp` | world | done |
| 1568:0e13 | `Cell_P19_Boundary12` | `cellP19Boundary12` | `cells.cpp` | world | done |
| 1568:0e33 | `Cell_P24_Wall` | `cellP24Wall` | `cells.cpp` | world | done |
| 1568:0e41 | `Cell_P46_V46_Boundary01` | `cellP46V46Boundary01` | `cells.cpp` | world | done |
| 1568:0e6d | `Cell_LevelBoundary01` | `cellLevelBoundary01` | `cells.cpp` | world | done |
| 1568:0ee3 | `Cell_DrawTreetop` | `cellDrawTreetop` | `cells.cpp` | world | done |
| 1568:0f13 | `Cell_P25_TrunkShaft` | `cellP25TrunkShaft` | `cells.cpp` | world | done |
| 1568:0f17 | `Cell_P31_GateL0` | `cellP31GateL0` | `cells.cpp` | world | done |
| 1568:1010 | `Cell_P26_Unused` | `cellP26Unused` | `cells.cpp` | world | done |
| 1568:1037 | `Cell_P27_Unused` | `cellP27Unused` | `cells.cpp` | world | done |
| 1568:105e | `Cell_P28_Path` | `cellP28Path` | `cells.cpp` | world | done |
| 1568:1077 | `Cell_P29_Unused` | `cellP29Unused` | `cells.cpp` | world | done |
| 1568:1090 | `Cell_P36_V36_NetShop` | `cellP36V36NetShop` | `cells.cpp` | world | done |
| 1568:1286 | `Cell_P40_V40_Geyser` | `cellP40V40Geyser` | `cells.cpp` | world | done |
| 1568:12b4 | `Cell_LevelBoundary12` | `cellLevelBoundary12` | `cells.cpp` | world | done |
| 1568:1329 | `Cell_P39_V39_GateL1` | `cellP39V39GateL1` | `cells.cpp` | world | done |
| 1568:141c | `Cell_P41_V41_GeyserTop` | `cellP41V41GeyserTop` | `cells.cpp` | world | done |
| 1568:1556 | `Cell_DrawGeyser` | `cellDrawGeyser` | `cells.cpp` | world | done |
| 1568:15bc | `Cell_P45_V45_Bridge` | `cellP45V45Bridge` | `cells.cpp` | world | done |
| 1568:16ed | `Cell_P48_Landing` | `cellP48Landing` | `cells.cpp` | world | done |
| 1568:1734 | `Cell_P56_V56_CastleHall` | `cellP56V56CastleHall` | `cells.cpp` | world | done |
| 1568:182b | `Cell_P57_V57_CastleWall` | `cellP57V57CastleWall` | `cells.cpp` | world | done |
| 1568:18f1 | `Castle_CreatureHitCheck` | `castleCreatureHitCheck` | `cells.cpp` | world | done |
| 1568:1953 | `Cell_P60_IceDoor` | `cellP60IceDoor` | `cells.cpp` | world | done |
| 1568:19ab | `Cell_P61_IceCaveExit` | `cellP61IceCaveExit` | `cells.cpp` | world | done |
| 1568:1a20 | `Cell_P62_IceSlide` | `cellP62IceSlide` | `cells.cpp` | world | done |
| 1568:1aa4 | `Cell_P63_V63_CastleTop` | `cellP63V63CastleTop` | `cells.cpp` | world | done |
| 1568:1c7b | `Cell_V49_Showcase` | `cellV49Showcase` | `cells.cpp` | world | done |
| 1568:1d96 | `Cell_V32_AnimDecor` | `cellV32AnimDecor` | `cells.cpp` | world | done |
| 1568:1ddb | `Cell_V10` | `cellV10` | `cells.cpp` | world | done |
| 1568:1dfa | `Cell_V59` | `cellV59` | `cells.cpp` | world | done |
| 1568:1e1c | `Cell_V7` | `cellV7` | `cells.cpp` | world | done |
| 1568:1e35 | `Cell_V8` | `cellV8` | `cells.cpp` | world | done |
| 1568:1e50 | `Cell_DrawObject` | `cellDrawObject` | `cells.cpp` | world | done |
| 1568:1e9e | `Castle_PlaceCreature` | `castlePlaceCreature` | `cells.cpp` | world | done |
| 1568:1eca | `Cell_P0_Void` | `cellP0Void` | `cells.cpp` | world | partial: the border colour is counted, not shown |
| 1568:1ed8 | `Cell_BonusCoins` | `cellBonusCoins` | `cells.cpp` | world | done |
| 1568:1f0b | `Cells_UpdateVisible` | `cellsUpdateVisible` | `cells.cpp` | world | done |
| 1568:1fe4 | `Cell_GetMask` | `cellGetMask` | `cells.cpp` | world | done |
| 176a:0006 | `Cell_P1` | `cellP1` | `cells2.cpp` | world | done |
| 176a:0019 | `Cell_ConveyLeft` | `cellConveyLeft` | `cells2.cpp` | world | done |
| 176a:008a | `Cell_P2` | `cellP2` | `cells2.cpp` | world | done |
| 176a:00a6 | `Cell_ConveyLeftIf` | `cellConveyLeftIf` | `cells2.cpp` | world | done |
| 176a:00fd | `Cell_P3` | `cellP3` | `cells2.cpp` | world | done |
| 176a:0110 | `Cell_ConveyRight` | `cellConveyRight` | `cells2.cpp` | world | done |
| 176a:0181 | `Cell_P4` | `cellP4` | `cells2.cpp` | world | done |
| 176a:019d | `Cell_ConveyRightIf` | `cellConveyRightIf` | `cells2.cpp` | world | done |
| 176a:01f8 | `Cell_P13` | `cellP13` | `cells2.cpp` | world | done |
| 176a:020a | `Cell_P14` | `cellP14` | `cells2.cpp` | world | done |
| 176a:0225 | `Cell_P15` | `cellP15` | `cells2.cpp` | world | done |
| 176a:0237 | `Cell_P16` | `cellP16` | `cells2.cpp` | world | done |
| 176a:029d | `Cell_P22` | `cellP22` | `cells2.cpp` | world | done |
| 176a:02af | `Cell_P21` | `cellP21` | `cells2.cpp` | world | done |
| 176a:02c1 | `Cell_P43` | `cellP43` | `cells2.cpp` | world | done |
| 176a:02dc | `Cell_P42_GateL2` | `cellP42GateL2` | `cells2.cpp` | world | done |
| 176a:0404 | `Cell_P64_EndScreen` | `cellP64EndScreen` | `endgame.cpp` | player | done |
| 176a:0485 | `Cell_P20_CastleGate` | `cellP20CastleGate` | `endgame.cpp` | player | done |
| 176a:0596 | `Cell_P70_TreasureRoom` | `cellP70TreasureRoom` | `endgame.cpp` | player | done |
| 176a:0f47 | `TreasureRoom_Leave` | `treasureRoomLeave` | `endgame.cpp` | player | done |
| 176a:0fa6 | `Cell_P71_V71_ToRankScreen` | `cellP71V71ToRankScreen` | `endgame.cpp` | player | done |
| 176a:1094 | `Cell_P72_RankScreen` | `cellP72RankScreen` | `endgame.cpp` | player | done |
| 176a:1119 | `Game_AdvanceEnd` | `gameAdvanceEnd` | `endgame.cpp` | player | done |
| 176a:1136 | `Treasure_IsCollected` | `treasureIsCollected` | `endgame.cpp` | player | done |
| 176a:1162 | `ShackPair_08_36_Approach` | `shackPair0836Approach` | `endgame.cpp` | player | done |
| 176a:11b7 | `ShackPair_08_36_Swap` | `shackPair0836Swap` | `endgame.cpp` | player | done |
| 176a:1220 | `ShackPair_0e_2c` | `shackPair0e2c` | `endgame.cpp` | player | done |
| 176a:1275 | `ShackPair_06_5a` | `shackPair065a` | `endgame.cpp` | player | done |
| 176a:129d | `ShackPair_1c_22` | `shackPair1c22` | `endgame.cpp` | player | done |
| 176a:12f2 | `ShackPair_18_58` | `shackPair1858` | `endgame.cpp` | player | done |
| 176a:1347 | `ShackFly_1e` | `shackFly1e` | `endgame.cpp` | player | done |
| 176a:13d7 | `ShackFly_50` | `shackFly50` | `endgame.cpp` | player | done |
| 176a:1463 | `ShackTrio_4a_4c_4e` | `shackTrio4a4c4e` | `endgame.cpp` | player | done |
| 176a:1531 | `Shack_ResetDisplay` | `shackResetDisplay` | `endgame.cpp` | player | done |
| 176a:159e | `Castle_CreatureSpawn` | `castleCreatureSpawn` | `cells2.cpp` | world | done |
| 18cf:000d | `PlrSt_00_Idle` | `plrSt00Idle` | `player.cpp` | player | done |
| 18cf:0038 | `EndSeq_Setup` | `endSeqSetup` | `player.cpp` | player | done |
| 18cf:00a9 | `PlrSt_41_EndIntro` | `plrSt41EndIntro` | `player.cpp` | player | done |
| 18cf:00c4 | `PlrSt_42_EndHorn` | `plrSt42EndHorn` | `player.cpp` | player | done |
| 18cf:00f8 | `PlrSt_43_EndBeam` | `plrSt43EndBeam` | `player.cpp` | player | done |
| 18cf:0122 | `PlrSt_44_EndSparkle` | `plrSt44EndSparkle` | `player.cpp` | player | done |
| 18cf:014d | `PlrSt_45_EndChestOpen` | `plrSt45EndChestOpen` | `player.cpp` | player | done |
| 18cf:0182 | `PlrSt_46_EndStep` | `plrSt46EndStep` | `player.cpp` | player | done |
| 18cf:01a3 | `PlrSt_47_EndStep` | `plrSt47EndStep` | `player.cpp` | player | done |
| 18cf:01cd | `PlrSt_48_EndFlightInit` | `plrSt48EndFlightInit` | `player.cpp` | player | done |
| 18cf:0205 | `PlrSt_49_EndFlightNext` | `plrSt49EndFlightNext` | `player.cpp` | player | done |
| 18cf:0280 | `PlrSt_50_EndFlightStart` | `plrSt50EndFlightStart` | `player.cpp` | player | done |
| 18cf:02a7 | `PlrSt_51_EndFlightStep` | `plrSt51EndFlightStep` | `player.cpp` | player | done |
| 18cf:02f2 | `PlrSt_52_EndRank` | `plrSt52EndRank` | `player.cpp` | player | done |
| 18cf:0343 | `PlrSt_53_EndRankSparkle` | `plrSt53EndRankSparkle` | `player.cpp` | player | done |
| 18cf:0398 | `PlrSt_54_EndRankWait` | `plrSt54EndRankWait` | `player.cpp` | player | done |
| 18cf:03a4 | `PlrSt_55_EndCrown` | `plrSt55EndCrown` | `player.cpp` | player | done |
| 18cf:0441 | `PlrSt_56_EndCreature` | `plrSt56EndCreature` | `player.cpp` | player | done |
| 18cf:049b | `PlrSt_57_EndPlayer` | `plrSt57EndPlayer` | `player.cpp` | player | done |
| 18cf:04ca | `PlrSt_58_EndShowcaseCheck` | `plrSt58EndShowcaseCheck` | `player.cpp` | player | done |
| 18cf:050e | `PlrSt_59_EndShowcaseStart` | `plrSt59EndShowcaseStart` | `player.cpp` | player | done |
| 18cf:0546 | `PlrSt_60_EndShowcaseStep` | `plrSt60EndShowcaseStep` | `player.cpp` | player | done |
| 18cf:05b2 | `PlrSt_61_EndDone` | `plrSt61EndDone` | `player.cpp` | player | done |
| 18cf:05c4 | `PlrSt_63_ChampSparkle` | `plrSt63ChampSparkle` | `player.cpp` | player | done |
| 18cf:05eb | `PlrSt_64_ChampChest` | `plrSt64ChampChest` | `player.cpp` | player | done |
| 18cf:0625 | `PlrSt_65_ChampArcs` | `plrSt65ChampArcs` | `player.cpp` | player | done |
| 18cf:0685 | `PlrSt_66_ChampScream` | `plrSt66ChampScream` | `player.cpp` | player | done |
| 18cf:06be | `PlrSt_67_ChampCrownFly` | `plrSt67ChampCrownFly` | `player.cpp` | player | done |
| 18cf:06f4 | `PlrSt_68_ChampCatch` | `plrSt68ChampCatch` | `player.cpp` | player | done |
| 18cf:0738 | `PlrSt_69_ChampCrowned` | `plrSt69ChampCrowned` | `player.cpp` | player | done |
| 18cf:0773 | `PlrSt_70_ChampSparkle` | `plrSt70ChampSparkle` | `player.cpp` | player | done |
| 18cf:07ea | `PlrSt_71_ChampSparkleWait` | `plrSt71ChampSparkleWait` | `player.cpp` | player | done |
| 18cf:07f6 | `PlrSt_05_Searching` | `plrSt05Searching` | `player.cpp` | player | done |
| 18cf:081b | `PlrSt_09_AfterTeleport` | `plrSt09AfterTeleport` | `player.cpp` | player | done |
| 18cf:082a | `PlrSt_03_Spawn` | `plrSt03Spawn` | `player.cpp` | player | done |
| 18cf:0831 | `PlrSt_04_Arrive` | `plrSt04Arrive` | `player.cpp` | player | done |
| 18cf:08cc | `PlrSt_10_OnPath` | `plrSt10OnPath` | `player.cpp` | player | done |
| 18cf:0b36 | `Player_NetLeft` | `playerNetLeft` | `player.cpp` | player | done |
| 18cf:0b6a | `Player_NetRight` | `playerNetRight` | `player.cpp` | player | done |
| 18cf:0b9e | `PlrSt_11_WalkLeft` | `plrSt11WalkLeft` | `player.cpp` | player | done |
| 18cf:0ca0 | `PlrSt_12_WalkRight` | `plrSt12WalkRight` | `player.cpp` | player | done |
| 18cf:0da2 | `PlrSt_13_LookUp` | `plrSt13LookUp` | `player.cpp` | player | done |
| 18cf:0e39 | `PlrSt_14_Front` | `plrSt14Front` | `player.cpp` | player | done |
| 18cf:0ed8 | `PlrSt_15_ClimbUp` | `plrSt15ClimbUp` | `player.cpp` | player | done |
| 18cf:0fa9 | `PlrSt_16_ClimbDown` | `plrSt16ClimbDown` | `player.cpp` | player | done |
| 18cf:107a | `PlrSt_17_JumpLeft` | `plrSt17JumpLeft` | `player.cpp` | player | done |
| 18cf:10df | `PlrSt_18_JumpLeftLand` | `plrSt18JumpLeftLand` | `player.cpp` | player | done |
| 18cf:1120 | `PlrSt_19_JumpRight` | `plrSt19JumpRight` | `player.cpp` | player | done |
| 18cf:1185 | `PlrSt_20_JumpRightLand` | `plrSt20JumpRightLand` | `player.cpp` | player | done |
| 18cf:11c6 | `PlrSt_21_NetLeftSwing` | `plrSt21NetLeftSwing` | `player.cpp` | player | done |
| 18cf:11d6 | `PlrSt_22_NetLeftHold` | `plrSt22NetLeftHold` | `player.cpp` | player | done |
| 18cf:11fd | `PlrSt_23_NetLeftDone` | `plrSt23NetLeftDone` | `player.cpp` | player | done |
| 18cf:1224 | `PlrSt_24_NetRightSwing` | `plrSt24NetRightSwing` | `player.cpp` | player | done |
| 18cf:1234 | `PlrSt_25_NetRightHold` | `plrSt25NetRightHold` | `player.cpp` | player | done |
| 18cf:125b | `PlrSt_26_NetRightDone` | `plrSt26NetRightDone` | `player.cpp` | player | done |
| 18cf:1282 | `PlrSt_27_CoinLeftDone` | `plrSt27CoinLeftDone` | `player.cpp` | player | done |
| 18cf:12bc | `PlrSt_28_CoinRightDone` | `plrSt28CoinRightDone` | `player.cpp` | player | done |
| 18cf:12f6 | `PlrSt_29_PassageUp` | `plrSt29PassageUp` | `player.cpp` | player | done |
| 18cf:1367 | `PlrSt_30_SlideDown` | `plrSt30SlideDown` | `player.cpp` | player | done |
| 18cf:13b9 | `PlrSt_31_FallLeft` | `plrSt31FallLeft` | `player.cpp` | player | done |
| 18cf:140c | `PlrSt_32_FallRight` | `plrSt32FallRight` | `player.cpp` | player | done |
| 18cf:145f | `PlrSt_33_LadderJump` | `plrSt33LadderJump` | `player.cpp` | player | done |
| 18cf:1495 | `PlrSt_34_Chute` | `plrSt34Chute` | `player.cpp` | player | done |
| 18cf:14b1 | `PlrSt_35_TreasureRoomLand` | `plrSt35TreasureRoomLand` | `player.cpp` | player | done |
| 18cf:14e0 | `Search_Start` | `searchStart` | `player.cpp` | player | done |
| 18cf:1532 | `Search_CanSearch` | `searchCanSearch` | `player.cpp` | player | done |
| 18cf:15f0 | `Player_Move` | `playerMove` | `player.cpp` | player | done |
| 18cf:17c9 | `Obj_WrapWorldX` | `objWrapWorldX` | `player.cpp` | player | done |
| 18cf:181a | `Player_Update` | `playerUpdate` | `player.cpp` | player | done |
| 1a54:0003 | `Layout_Generate` | `layoutGenerate` | `layout.cpp` | elves | done |
| 1a54:0059 | `Layout_PutInSlot` | `layoutPutInSlot` | `layout.cpp` | elves | done |
| 1a54:0179 | `Layout_PlaceKey` | `layoutPlaceKey` | `layout.cpp` | elves | done |
| 1a54:029a | `Layout_PlaceTreasures` | `layoutPlaceTreasures` | `layout.cpp` | elves | done |
| 1a54:06dc | `Layout_MirrorSharedCells` | `layoutMirrorSharedCells` | `layout.cpp` | elves | done |
| 1a54:078b | `Layout_UnmarkSharedCells` | `layoutUnmarkSharedCells` | `layout.cpp` | elves | done |
| 1a54:07d6 | `Layout_ClearSlots` | `layoutClearSlots` | `layout.cpp` | elves | done |
| 1a54:0803 | `Cells_AdjustForCpu` | `cellsAdjustForCpu` | `layout.cpp` | elves | done |
| 1a54:085f | `Cells_ResetVariable` | `cellsResetVariable` | `layout.cpp` | elves | done |
| 1a54:0892 | `Castle_PickLayout` | `castlePickLayout` | `layout.cpp` | elves | done |
| 1a54:08dc | `Castle_ApplyLayout` | `castleApplyLayout` | `layout.cpp` | elves | done |
| 4306:0022 | `Anim_Tick` | `animTick` | `anim.cpp` | player | done |
| 4306:02c0 | `Anim_InitSlots` | `animInitSlots` | `anim.cpp` | player | done |
| 4306:0393 | `Sprites_Snapshot` | `spritesSnapshot` | `anim.cpp` | player | done |
| 4306:0497 | `Sprites_DrawSorted` | `spritesDrawSorted` | `anim.cpp` | player | done; negative z drawn at its own position (deviation, §5) |
| 4306:06dd | `Sprites_EraseAll` | `spritesEraseAll` | `anim.cpp` | player | done |
| 4306:0784 | `Anim_SetIfIdle` | `animSetIfIdle` | `anim.cpp` | player | done |
| 4306:07b9 | `Anim_Queue` | `animQueue` | `anim.cpp` | player | done |
| 4306:07fa | `Anim_Set` | `animSet` | `anim.cpp` | player | done |
| 4306:0844 | `Anim_Stop` | `animStop` | `anim.cpp` | player | done |
| 4306:088e | `Anim_Start` | `animStart` | `anim.cpp` | player | done |
| 4306:092f | `Anim_Clear` | `animClear` | `anim.cpp` | player | done |
| 43cb:0001 | `Menu_Update` | `menuUpdate` | `ui.cpp` | ui | done |
| 43cb:014a | `Menu_MoveSelection` | `menuMoveSelection` | `ui.cpp` | ui | done |
| 43cb:019a | `Dialog_Update` | `dialogUpdate` | `ui.cpp` | ui | done |
| 43cb:021d | `MenuCmd_HelpPage` | `menuCmdHelpPage` | `ui.cpp` | ui | done |
| 43cb:028b | `MenuCmd_StartOver` | `menuCmdStartOver` | `ui.cpp` | ui | done |
| 43cb:02f8 | `MenuCmd_StartNewPlayer` | `menuCmdStartNewPlayer` | `ui.cpp` | ui | done |
| 43cb:032c | `MenuCmd_ExitAndSave` | `menuCmdExitAndSave` | `ui.cpp` | ui | done |
| 43cb:034f | `MenuCmd_NewPlayerHints` | `menuCmdNewPlayerHints` | `ui.cpp` | ui | done |
| 43cb:0371 | `MenuCmd_Music` | `menuCmdMusic` | `ui.cpp` | ui | done |
| 43cb:039d | `MenuCmd_Sound` | `menuCmdSound` | `ui.cpp` | ui | done |
| 43cb:03cb | `Game_HandleHotkeys` | `gameHandleHotkeys` | `ui.cpp` | ui | done |
| 43cb:046e | `Sound_SyncState` | `soundSyncState` | `main_loop.cpp` | mainloop | done |
| 43cb:04ff | `Menu_HighlightItem` | `menuHighlightItem` | `ui.cpp` | ui | done |
| 43cb:0592 | `MsgBox_Draw` | `msgBoxDraw` | `ui.cpp` | ui | done; slow CPU: the save-heap handle DS:37C8/37CC is not modelled |
| 43cb:0848 | `Box_DrawFrame` | `boxDrawFrame` | `ui.cpp` | ui | done |
| 43cb:0ab9 | `Gfx_HLine` | `gfxHLine` | `ui.cpp` | ui | done |
| 43cb:0ada | `Gfx_VLine` | `gfxVLine` | `ui.cpp` | ui | done |
| 43cb:0afb | `Gfx_FillRectColor` | `gfxFillRectColor` | `ui.cpp` | ui | done |
| 43cb:0b18 | `Gfx_FrameRect` | `gfxFrameRect` | `ui.cpp` | ui | done |
| 43cb:0b63 | `Text_Draw` | `textDraw` | `ui.cpp` | ui | done |
| 43cb:0cfc | `Text_Width` | `textWidth` | `ui.cpp` | ui | done |
| 43cb:0d50 | `Button_Draw` | `buttonDraw` | `ui.cpp` | ui | done |
| 43cb:0eb6 | `Button_Flash` | `buttonFlash` | `ui.cpp` | ui | done (a wait over frames, §3.4) |
| 43cb:0ee8 | `Delay_Ticks` | `delayTicks` | `ui.cpp` | ui | done (no-op: the wait is the flash state) |
| 43cb:0f1d | `MenuBar_DrawTitle` | `menuBarDrawTitle` | `ui.cpp` | ui | done |
| 43cb:107a | `Help_LoadFile` | `helpLoadFile` | `ui.cpp` | ui | done |
| 43cb:10c4 | `Help_DrawPage` | `helpDrawPage` | `ui.cpp` | ui | done |
| 43cb:1273 | `Help_Open` | `helpOpen` | `ui.cpp` | ui | done |
| 43cb:12cf | `Box_SetupInput` | `boxSetupInput` | `ui.cpp` | ui | done |
| 4521:000c | `ElfSt_01` | `elfSt01` | `elves.cpp` | elves | done |
| 4521:005c | `ElfSt_05` | `elfSt05` | `elves.cpp` | elves | done |
| 4521:00ac | `ElfSt_02` | `elfSt02` | `elves.cpp` | elves | done |
| 4521:013d | `ElfSt_04` | `elfSt04` | `elves.cpp` | elves | done |
| 4521:01c4 | `ElfSt_09` | `elfSt09` | `elves.cpp` | elves | done |
| 4521:01fd | `ElfSt_13` | `elfSt13` | `elves.cpp` | elves | done |
| 4521:0236 | `ElfSt_10` | `elfSt10` | `elves.cpp` | elves | done |
| 4521:02aa | `ElfSt_12` | `elfSt12` | `elves.cpp` | elves | done |
| 4521:0311 | `ElfSt_17` | `elfSt17` | `elves.cpp` | elves | done |
| 4521:034a | `ElfSt_21` | `elfSt21` | `elves.cpp` | elves | done |
| 4521:0383 | `ElfSt_18` | `elfSt18` | `elves.cpp` | elves | done |
| 4521:03f7 | `ElfSt_20` | `elfSt20` | `elves.cpp` | elves | done |
| 4521:045e | `ElfSt_25` | `elfSt25` | `elves.cpp` | elves | done |
| 4521:0491 | `ElfSt_28` | `elfSt28` | `elves.cpp` | elves | done |
| 4521:04a2 | `ElfSt_45` | `elfSt45` | `elves.cpp` | elves | done |
| 4521:04c4 | `ElfSt_46` | `elfSt46` | `elves.cpp` | elves | done |
| 4521:04e6 | `Elf_WalkCommon` | `elfWalkCommon` | `elves.cpp` | elves | done |
| 4521:0545 | `ElfSt_47` | `elfSt47` | `elves.cpp` | elves | done |
| 4521:05d8 | `ElfSt_49` | `elfSt49` | `elves.cpp` | elves | done |
| 4521:068b | `ElfSt_50` | `elfSt50` | `elves.cpp` | elves | done |
| 4521:073e | `ElfSt_51` | `elfSt51` | `elves.cpp` | elves | done |
| 4521:07d7 | `ElfSt_52` | `elfSt52` | `elves.cpp` | elves | done |
| 4521:0855 | `ElfSt_53` | `elfSt53` | `elves.cpp` | elves | done |
| 4521:0967 | `ElfSt_54` | `elfSt54` | `elves.cpp` | elves | done |
| 4521:09c4 | `ElfSt_55` | `elfSt55` | `elves.cpp` | elves | done |
| 4521:0a2e | `ElfSt_56` | `elfSt56` | `elves.cpp` | elves | done |
| 4521:0aac | `ElfSt_57` | `elfSt57` | `elves.cpp` | elves | done |
| 4521:0bbe | `ElfSt_58` | `elfSt58` | `elves.cpp` | elves | done |
| 4521:0c1b | `ElfSt_59` | `elfSt59` | `elves.cpp` | elves | done |
| 4521:0c85 | `ElfSt_60` | `elfSt60` | `elves.cpp` | elves | done |
| 4521:0c9e | `ElfSt_61` | `elfSt61` | `elves.cpp` | elves | done |
| 4521:0cb7 | `ElfSt_70` | `elfSt70` | `elves.cpp` | elves | done |
| 4521:0cd0 | `ElfSt_71` | `elfSt71` | `elves.cpp` | elves | done |
| 4521:0cfe | `ElfSt_72` | `elfSt72` | `elves.cpp` | elves | done |
| 4521:0d93 | `CoinElf_Check` | `coinElfCheck` | `elves.cpp` | elves | done |
| 4521:0e1b | `ElfSt_73` | `elfSt73` | `elves.cpp` | elves | done |
| 4521:0e6e | `ElfSt_74` | `elfSt74` | `elves.cpp` | elves | done |
| 4521:0ead | `Elves_Update` | `elvesUpdate` | `elves.cpp` | elves | done |
| 4521:1062 | `Elf_Think` | `elfThink` | `elves.cpp` | elves | done |
| 4521:1268 | `Elf_Release` | `elfRelease` | `elves.cpp` | elves | done |
| 4521:12df | `Elves_ShiftNearby` | `elvesShiftNearby` | `elves.cpp` | elves | done |
| 4521:1354 | `ElfSt_64` | `elfSt64` | `elves.cpp` | elves | done |
| 4521:1382 | `ElfSt_62` | `elfSt62` | `elves.cpp` | elves | done |
| 4521:13e2 | `ElfSt_63` | `elfSt63` | `elves.cpp` | elves | done |
| 4521:13ff | `ElfSt_65` | `elfSt65` | `elves.cpp` | elves | done |
| 4521:145f | `ElfSt_66` | `elfSt66` | `elves.cpp` | elves | done |
| 4521:147c | `Elf_NetCatch` | `elfNetCatch` | `elves.cpp` | elves | done |
| 4521:15ab | `ElfSt_03` | `elfSt03` | `elves.cpp` | elves | done |
| 4521:15f9 | `ElfSt_68` | `elfSt68` | `elves.cpp` | elves | done |
| 4521:1617 | `ElfSt_69` | `elfSt69` | `elves.cpp` | elves | done |
| 4521:1648 | `Elf_CheckNet` | `elfCheckNet` | `elves.cpp` | elves | done |
| 4521:181a | `Elf_Turn` | `elfTurn` | `elves.cpp` | elves | done |
| 4521:182e | `Elves_Spawn` | `elvesSpawn` | `elves.cpp` | elves | done; g_curObj stays on record 14 after 15 spawns (deviation, §8) |
| 46ce:0007 | `Quiz_Update` | `quizUpdate` | `quiz.cpp` | ui | done |
| 46ce:0271 | `Quiz_DrawScreen` | `quizDrawScreen` | `quiz.cpp` | ui | done |
| 46ce:028d | `Quiz_DrawFrame` | `quizDrawFrame` | `quiz.cpp` | ui | done |
| 46ce:02d9 | `Quiz_PickQuestion` | `quizPickQuestion` | `quiz.cpp` | ui | done |
| 46ce:0323 | `Quiz_SplitText` | `quizSplitText` | `quiz.cpp` | ui | done |
| 46ce:03ff | `Quiz_SetupAnswers` | `quizSetupAnswers` | `quiz.cpp` | ui | done |
| 46ce:0482 | `Quiz_DrawQuestion` | `quizDrawQuestion` | `quiz.cpp` | ui | done |
| 46ce:049e | `Quiz_DrawAnswers` | `quizDrawAnswers` | `quiz.cpp` | ui | done |
| 46ce:052d | `Quiz_SetFocus` | `quizSetFocus` | `quiz.cpp` | ui | done |
| 46ce:05bb | `Quiz_Correct` | `quizCorrect` | `quiz.cpp` | ui | done |
| 46ce:069e | `Quiz_Wrong` | `quizWrong` | `quiz.cpp` | ui | done |
| 46ce:06f0 | `Clue_Reveal` | `clueReveal` | `quiz.cpp` | ui | done |
| 46ce:079c | `Clue_BuildWords` | `clueBuildWords` | `layout.cpp` | elves | done |
| 46ce:08f1 | `Dict_Expand` | `dictExpand` | `quiz.cpp` | ui | done |
| 46ce:095f | `Quiz_LoadDictionary` | `quizLoadDictionary` | `quiz.cpp` | ui | done |
| 46ce:09ab | `Quiz_LoadQuestions` | `quizLoadQuestions` | `quiz.cpp` | ui | done |
| 46ce:0b6a | `Quiz_Deal` | `quizDeal` | `quiz.cpp` | ui | done |
| 46ce:0c39 | `Quiz_PlanStatuses` | `quizPlanStatuses` | `quiz.cpp` | ui | done |
| 46ce:0cd0 | `Quiz_TierHasWork` | `quizTierHasWork` | `quiz.cpp` | ui | done |
| 46ce:0d60 | `Quiz_ResetHistory` | `quizResetHistory` | `quiz.cpp` | ui | done |
| 46ce:0d9a | `Quiz_FindTierEnd` | `quizFindTierEnd` | `quiz.cpp` | ui | done |
| 46ce:0df2 | `Quiz_ReorderSlots` | `quizReorderSlots` | `quiz.cpp` | ui | done |
| 47c7:0007 | `SignIn_Update` | `signInUpdate` | `signin.cpp` | ui | done |
| 47c7:00e7 | `SignIn_DrawPanel` | `signInDrawPanel` | `signin.cpp` | ui | done |
| 47c7:0189 | `SignIn_DrawNameEntry` | `signInDrawNameEntry` | `signin.cpp` | ui | done |
| 47c7:01e0 | `SignIn_DrawList` | `signInDrawList` | `signin.cpp` | ui | done |
| 47c7:03cf | `SignIn_Draw` | `signInDraw` | `signin.cpp` | ui | done |
| 47c7:042d | `Screen_DrawBackdrop` | `screenDrawBackdrop` | `signin.cpp` | ui | done |
| 47c7:0452 | `Users_EnsureLoaded` | `usersEnsureLoaded` | `signin.cpp` | ui | done |
| 47c7:04a4 | `Users_NameExists` | `usersNameExists` | `signin.cpp` | ui | done |
| 47c7:0514 | `Users_ReadFile` | `usersReadFile` | `signin.cpp` | ui | done |
| 47c7:055a | `Users_WriteFile` | `usersWriteFile` | `signin.cpp` | ui | done |
| 47c7:05a9 | `SignIn_DrawTypedChar` | `signInDrawTypedChar` | `signin.cpp` | ui | done |
| 47c7:05d4 | `SignIn_DrawField` | `signInDrawField` | `signin.cpp` | ui | done |
| 47c7:06d4 | `SignIn_HighlightRow` | `signInHighlightRow` | `signin.cpp` | ui | done |
| 47c7:071a | `SignIn_MoveHighlight` | `signInMoveHighlight` | `signin.cpp` | ui | done |
| 47c7:075d | `Users_ClearRecord` | `usersClearRecord` | `signin.cpp` | ui | done |
| 47c7:07b9 | `Users_AddPlayer` | `usersAddPlayer` | `signin.cpp` | ui | done |
| 47c7:0831 | `SignIn_SetupZones` | `signInSetupZones` | `signin.cpp` | ui | done |
| 47c7:08b5 | `Game_Start` | `gameStart` | `signin.cpp` | ui | done |
| 47c7:09a2 | `Game_End` | `gameEnd` | `signin.cpp` | ui | done |
| 47c7:09dd | `Users_SelectRecord` | `usersSelectRecord` | `signin.cpp` | ui | done |
| 47c7:0a8b | `Users_InitNewPlayer` | `usersInitNewPlayer` | `signin.cpp` | ui | done |
| 47c7:0b19 | `Users_LoadPlayer` | `usersLoadPlayer` | `signin.cpp` | ui | done |
| 47c7:0bd6 | `Game_NewSetup` | `gameNewSetup` | `signin.cpp` | ui | done |
| 47c7:0c62 | `Game_Restore` | `gameRestore` | `signin.cpp` | ui | done |
| 47c7:0e0e | `Users_Pack` | `usersPack` | `signin.cpp` | ui | done |
| 47c7:0f8b | `SignIn_HandleInput` | `signInHandleInput` | `signin.cpp` | ui | done |
| 47c7:1011 | `SignInCmd_Select` | `signInCmdSelect` | `signin.cpp` | ui | done |
| 47c7:1199 | `SignInCmd_NewName` | `signInCmdNewName` | `signin.cpp` | ui | done |
| 47c7:1328 | `SignInCmd_Remove` | `signInCmdRemove` | `signin.cpp` | ui | done |
| 47c7:13d7 | `Dialog_CloseRedraw` | `dialogCloseRedraw` | `signin.cpp` | ui | done |
| 47c7:13dd | `SignInDlg_ExitYes` | `signInDlgExitYes` | `signin.cpp` | ui | done |
| 47c7:13e9 | `SignInDlg_Back` | `signInDlgBack` | `signin.cpp` | ui | done |
| 47c7:13fa | `SignInDlg_RemoveYes` | `signInDlgRemoveYes` | `signin.cpp` | ui | done |
| 47c7:1414 | `SignInDlg_ContinueAnswer` | `signInDlgContinueAnswer` | `signin.cpp` | ui | done |
| 47c7:1436 | `SignInDlg_Reset` | `signInDlgReset` | `signin.cpp` | ui | done |
| 47c7:1442 | `SignInDlg_ListFull` | `signInDlgListFull` | `signin.cpp` | ui | done |
| 47c7:1454 | `Dialog_HandleButtons` | `dialogHandleButtons` | `signin.cpp` | ui | done |
| 47c7:1502 | `Dialog_HandleOk` | `dialogHandleOk` | `signin.cpp` | ui | done |
| 47c7:152b | `UI_ForceRedraw` | `uiForceRedraw` | `signin.cpp` | ui | done |
| 49a6:000f | `Hints_Update` | `hintsUpdate` | `hints.cpp` | elves | done |
| 49a6:0121 | `Hints_CheckTriggers` | `hintsCheckTriggers` | `hints.cpp` | elves | done |
| 49a6:022b | `Hints_PickNext` | `hintsPickNext` | `hints.cpp` | elves | done |
| 49a6:026f | `Gameover_UpdateMessages` | `gameoverUpdateMessages` | `hints.cpp` | elves | done |
| 49a6:0565 | `Flutter_Show` | `flutterShow` | `hints.cpp` | elves | done |
| 4a03:0003 | `Search_Update` | `searchUpdate` | `search.cpp` | player | done |
| 4a03:038c | `Treasure_PickShowcase` | `treasurePickShowcase` | `search.cpp` | player | done |
| 4a03:04a8 | `ShelfRoom_DrawShelf` | `shelfRoomDrawShelf` | `search.cpp` | player | done |
| 4a03:078c | `Treasure_AddCollected` | `treasureAddCollected` | `search.cpp` | player | done |
| 4a85:0208 | `Input_MouseWalk` | `inputMouseWalk` | `main_loop.cpp` | mainloop | done |
| 4a85:03ac | `Input_Tick` | `inputTick` | `main_loop.cpp` | mainloop | done |
| 4ba4:000f | `Title_ClearBox` | `titleClearBox` | `title.cpp` | ui | done |
| 4ba4:0045 | `TitleStep_DrawTitle` | `titleStepDrawTitle` | `title.cpp` | ui | done |
| 4ba4:00b2 | `TitleStep_Wait` | `titleStepWait` | `title.cpp` | ui | done |
| 4ba4:0156 | `Title_DrawSprite` | `titleDrawSprite` | `title.cpp` | ui | done |
| 4ba4:017f | `TitleStep_Story1` | `titleStepStory1` | `title.cpp` | ui | done |
| 4ba4:01cc | `TitleStep_Story2` | `titleStepStory2` | `title.cpp` | ui | done |
| 4ba4:0233 | `TitleStep_StartWalkoff` | `titleStepStartWalkoff` | `title.cpp` | ui | done |
| 4ba4:027a | `TitleStep_WaitWalkoff` | `titleStepWaitWalkoff` | `title.cpp` | ui | done |
| 4ba4:02a3 | `TitleStep_WaitActorGone` | `titleStepWaitActorGone` | `title.cpp` | ui | done |
| 4ba4:02b1 | `Title_SuperSolversPage` | `titleSuperSolversPage` | `title.cpp` | ui | done |
| 4ba4:042b | `Title_ResetPhase` | `titleResetPhase` | `title.cpp` | ui | done |
| 4ba4:0434 | `Title_Run` | `titleStart` | `title.cpp` | ui | done (Title_Run split) |
| 4ba4:0434 | `Title_Run` | `titleFrame` | `title.cpp` | ui | done (Title_Run split) |
| 4ba4:0434 | `Title_Run` | `titleFinish` | `title.cpp` | ui | done (Title_Run split) |
| 4d2d:0009 | `with` | `turboCTime` | `layout.cpp` | elves | done |
| 4d63:01e5 | `__isDST` | `turboCIsDst` | `layout.cpp` | elves | done |
| - | (engine helper) | `loadSpriteBank` | `scene.cpp` | world | done |
| - | (engine helper) | `transitionDissolveRunning` | `camera.cpp` | world | done |
| - | (engine helper) | `cellMapWord` | `camera.cpp` | world | done |
| - | (engine helper) | `callCellObjectHandler` | `cells.cpp` | world | done |
| - | (engine helper) | `callCellVisibleHandler` | `cells.cpp` | world | done |
| - | (engine helper) | `setTerrainMaskHookForTests` | `player.cpp` | player | test seam (the game never sets it) |
| - | (engine helper) | `callPlayerStateHandler` | `player.cpp` | player | done |
| - | (engine helper) | `callElfStateHandler` | `elves.cpp` | elves | done |
| - | (engine helper) | `layoutTimeSeed` | `layout.cpp` | elves | done (--dos-layout-seed overrides it) |
| - | (engine helper) | `callDialogConfirm` | `ui.cpp` | ui | done |
| - | (engine helper) | `callDialogCancel` | `ui.cpp` | ui | done |
| - | (engine helper) | `uiFlashPending` | `ui.cpp` | ui | done (engine: Button_Flash wait) |
| - | (engine helper) | `uiFlashContinue` | `ui.cpp` | ui | done (engine: Button_Flash wait) |
| - | (engine helper) | `callTitlePhase` | `title.cpp` | ui | done |
| - | (engine helper) | `signInFlashDone` | `signin.cpp` | ui | done |
| - | (engine helper) | `callScreenPainter` | `hud.cpp` | ui | done |

---

## 10. Integration and the fidelity harness

* **Start-up.** `runDosEdition` (dos/app/dos_entry.cpp) builds the `DosGame`, calls `mainStart`
  (Game_Init, Title_Run's start) and runs the lockstep loop: windowed with the host clock, or headless
  driven by the `--keys` script (game ticks) until the script, `--dos-ticks`, the last
  `--dos-dump-state`, the BIOS buffer and a Button_Flash or dissolve the last keys started are done,
  or the game quits.
* **Starting from a DOSBox-X state** (wave 4). `--dos-load-state <file.sav>` replaces `mainStart` by
  `loadDosboxState` (§6): the state's machine decides the look (unless `--look` / a look switch is
  given) and the sound device (the AdLib driver's rand() draw is part of the game's sequence), the
  state's sibling `<name>.users` becomes the preference folder's `dos_users.dat` (the replaced file
  kept as `.bak`; tools always pass a scratch `--pref-dir`), and the store's image is the state's
  in-memory USERS.DAT (DS:BA14). Ticks of `--keys`, `--dos-ticks` and `--dos-dump-state` count from
  the load; tick 0 is the loaded moment (`--dos-dump-state 0:<file>` writes the state's own screen).
  The runtime goes on from the state's g_ticks (`DosRuntime::resumeAt`): the trace's `ticks` column
  and the DGROUP dumps carry the original's g_ticks. The sound driver's effect and tune go on where
  the state left them (`soundRestoreDriver`); a state saved inside Sprites_DrawSorted gets the full
  clip window the rest of that call sets. Not carried over: the frame clock's fractions, an
  engine-side wait (a Button_Flash or a dissolve in progress), the rest of a Game_Tick the state was
  saved inside (g_inGameTick: the engine starts with the next tick; 5 of the 124 reference states),
  the slow-CPU box save. `--dos-grey-key-artefact` feeds the grey keys' E0 prefix bytes to Kbd_ISR as
  DOSBox-X does (r2/fl_jumps and fl_typematic replay in every column from the CLI).
* **Diagnostics.** `--dos-trace <csv>` (one row per game tick that ran Game_Tick; the events of a frame
  whose own tick fell into its I/O start the next row), `--dos-dump-state <tick>:<file>`
  (DGROUP image, `<file>.fields.tsv` and the picture shown, `<file>.png`), `--frame-dump <folder>`
  (the last frame of each screen and the run's sound), `--dos-layout-seed <n>` (the srand value of
  new games instead of the clock, for reproducible runs; 0x1EE2 gives the layout of the Round-1 EGA
  capture climb).
* **Tools** (`tools/dos/README.md`): `savmem.py` (states: variables, DGROUP, the screen, the CPU),
  `framelog.py` / `stepchanges.py` (the r2 frame logger), `fl2csv.py` (logs against engine traces,
  by g_ticks or as a sequence of executed ticks; key scripts from a log), `imgcmp.py` (frames against
  captures), `play.py` / `stages.py` / `pt_full.py` (the headless playthrough driver: each step reruns
  the game with the script so far and reads the trace to place the next key; `pt_full.py` produced the
  10914-tick new-player game of `tests/test_dos_playthrough.cpp` and
  `tools/dos/scenarios/level1/pt_full_script.txt`), `seed_scenarios.py`, `avifit.py` (an engine run
  against a DOSBox-X AVI: every AVI frame looked up among the run's per-tick pictures, the AVI's time
  base fitted; the `avi_fits` of a scenario; EGA AVIs run at 60.29 frames per emulated second).
* **The fidelity runner** `tools/dos/fidelity.py` runs the scenarios of `tools/dos/scenarios/<group>/`
  (`ui`, `level1`, `levels23`, `castle_end`, `looks`): start (boot with a seed and a roster, or a
  state), look, keys, checkpoints (a tick, a trace event matched as a whole token, or the moment a
  log's end state was saved; a capture, a state's screen or a frame of an AVI; masks with reasons;
  max_px; `info` for a reference of another moment, counted apart), the frame logs to compare,
  timings and AVI time-base fits (`avi_fits`, through `avifit.py`; `--no-avi` skips them). It writes
  `report.md`, `<group>/report.md|csv|montage.png` under `extracted/scratch/dos_w4/report/` and
  exits with 1 on a regression against `tools/dos/scenarios/baseline.json` (`--update-baseline`
  stores the current numbers). 164 scenarios in five groups; the whole run takes about half a minute
  with `--jobs 10`. The result after wave 4: §11.

---

## 11. Fidelity after wave 4

`python -P tools/dos/fidelity.py --jobs 10` (164 scenarios, 0 regressions against
`tools/dos/scenarios/baseline.json`; build of the wave-4 integration). A checkpoint compares the
picture the engine shows after a game tick with a DOSBox-X capture, an AVI frame or a state's screen;
"informational" ones compare with a capture of another moment (taken before its state was saved, or
a different route). Logs: the r2 frame logs replayed through the CLI from their start states, x, y,
camera, state and coins per game tick, both by g_ticks slot and as a sequence of executed ticks (a
log counts as matching when both alignments have 0 mismatches over all its records).

| Group | Scenarios | Checkpoints at 0 px | Informational | Logs tick for tick | Timings | AVI fits |
|---|---|---|---|---|---|---|
| ui | 24 | 73 / 73 | 0 | - | 4 / 4 | - |
| level1 | 53 | 75 / 77 | 35 | 11 / 11 (10696 ticks) | 1 / 2 | - |
| levels23 | 47 | 112 / 112 | 0 | 7 / 7 (5995 ticks) | 3 / 3 | - |
| castle_end | 27 | 81 / 81 | 0 | 3 / 3 (4943 ticks) | 4 / 4 | 8 / 8 |
| looks | 13 | 98 / 98 | 10 | - | - | - |
| all | 164 | 439 / 441 | 45 | 21 / 21 (21634 ticks) | 12 / 13 | 8 / 8 |

Captures (best checkpoint per capture): EGA Round 1 (106 frames with 070a-c): 65 at 0 pixels, 18 at
0 pixels with masks (the player or the elves where the Round-1 key times between two states are
unknown, torn presents, the castle-top key icon), 4 informational, 19 not reachable from any state
(026-028, 037-042, 044, 046-048, 050, 071-074: taken before the next state with unknown net swings,
quizzes and walks in between; 064: a mid-dissolve frame, whose cells lie between two engine dissolve
frames; 073/074 need a crafted state: 073 0 px, 074 differs only by the caught elf, offline). EGA
Round 2 (108): 91 at 0 pixels, 10 with masks (the key icon race, half-presented frames), 2
informational, 5 not compared (pictures before their state, one duplicate). Tandy 37 (22 Round 1,
15 Round 3): all at 0 pixels (5 with the title sparkles masked). MCGA 29 and CGA 30 (Round 1 and the
24 Round-3 UI frames each): all at 0 pixels, masks only on the random title sparkles and one random
elf.

Remaining differences, each explained:
* `level1/treasure5_found` (end state `r2/fl_treasure5_found`) and `level1/treasure_found_avi`
  (`ega/r2_treasure_01_found_hint`): 129 pixels each, the walking elf's feet under hint 4's bubble:
  the bubble frame draws the actors from the sprite snapshot of its Frame_Begin, which the engine
  takes before the frame's tick and DOSBox-X after it (§8, the order of a frame against its tick).
  The seed, every elf record and every sprite slot equal the reference.
* `level1/treasure_found_avi` timing from the throw to the hint: AVI 113 ticks, engine 105; every
  logged run of the same search agrees with the engine (the EGA AVIs of a search ran slower, §8).
* Informational: the 32 checkpoints of 16 level1 state scenarios and the 10 of 5 Tandy state
  scenarios (a capture taken before the state was saved: elves, sparkles or the pointer moved), the
  boot playthrough's 3 views of the Round-1 climb (another route and other elves on the same layout).
* The r2 logs not compared: `fl_countup_after` (its coins were set in RAM during the run),
  `fl_menu_open`, `fl_box_open`, `fl_quiz_open` (their keys are BIOS keys, not in the STEP records;
  no tick gated), `fl_walk`, `fl_search_t`, `fl_treasure5_found` (FRAME rings only; the scenarios
  `level1/treasure_search_log` and `treasure5_found` reproduce their end states).

Timings checked against the references: the attract cycle 5770 game ticks and each page
(`tandy_attract_full_cycle.avi`); the doorway dissolve; the castle top to the gate 1175 ticks, the
chute to the rank chest 443 and 467, the rank chest's rows 20 (the end-sequence AVIs); the AVI time
bases of the castle, end-sequence, crown, castle-creature and treasure-room runs (`avifit.py`: at most
1.2 ticks off, a frame for each AVI frame's own tick).

Windows edition: the whole test suite passes and the headless frame dumps of the wave-2 platform
scenarios (`extracted/scratch/dos_w2_platform/run_scenarios.sh`) are byte-identical to their baseline.
