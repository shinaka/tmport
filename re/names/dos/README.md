# DOS edition names (`re/names/dos/`)

One canonical name per address of `SST.EXE` (V 1.0), applied to the Ghidra project
`re/ghidra_proj/TMDos` with `tools/ghidra_scripts/ApplyNames.java` and carried by the export
`re/dos/ghidra_export/`. The docs in `re/dos/docs/` and the engine (`src/dos/data/dgroup_offsets.h`)
use exactly these names.

## Files

Four tab-separated columns: `SSSS:OOOO`, name, C signature or type, comment. One file per area;
an address appears in exactly one file.

| File | Segments |
|---|---|
| `code_1000_gfx.tsv` | 1000 (start-up, timer, video, blitters, scrolling, font) |
| `code_130f-153e.tsv` | 130f main loop/HUD, 1437 scenes, 1460 init/files, 150a/153e disk |
| `code_1568-176a_cells.tsv` | 1568, 176a cell handlers, end screens, treasure room |
| `code_18cf-1a54.tsv` | 18cf player, 1a54 layout |
| `code_4306-43cb.tsv` | 4306 animation scripts, 43a0 keyboard ISR, 43cb UI |
| `code_4521_elves.tsv` | 4521 elves |
| `code_46ce-47c7.tsv` | 46ce quiz, 47c7 sign-in and saves |
| `code_491b-4ba4.tsv` | 491b sound, 49a6 hints, 4a03 search, 4a85 input, 4ac9 AdLib, 4ba4 title (+ a few 43a0) |
| `rtl_turboc.tsv` | Turbo C 2.0 run-time library |
| `dgroup.tsv` | DGROUP (`4d94:XXXX` = DS:XXXX): always data labels, never functions |

## Conventions

* Functions `Module_Verb[Object]` (`Scene_Update`, `Elf_NetCatch`); cell handlers `Cell_P<type>` (object
  handler) / `Cell_V<type>` (visible handler) / `Cell_P<t>_V<t>` (both), followed by a short role
  (`Cell_P31_GateL0`); player states `PlrSt_<nn>_<Role>`, elf states `ElfSt_<nn>`.
* Globals `g_name`, strings `s_name`. Frame latches written by `Frame_Begin` are `g_frame*`
  (`g_frameCamX`, `g_frameRankState`); requests that a latch takes over end in `Req`
  (`g_fullRedrawReq` → latch `g_fullRedraw`).
* Places: "treasure shelf rooms" = cells 38–6B (four rooms reached from the clubhouse), "treasure room" =
  cells 98–9B (scene 4), "rank chest screen" = cell 1D (scene 4).

## Wave 2 merge (decision table)

Wave 1 left proposals of five agents (`wave1_{world,player,elves,ui,runtime}.tsv`). They were merged
into the files above (scripted); where an agent renamed
an older name, the owner of the module won. Addresses with competing proposals, and older names
that were wrong, were decided as follows (rule: prefer the module that owns the code; the name must
describe what the code does):

| Address | Canonical | Other proposals / old name | Reason |
|---|---|---|---|
| 1000:176c | `Gfx_ReplaceColorRect` | `Gfx_SwapColor`, `Gfx_XorRect` | every pixel of colour "from" inside the rectangle becomes "to"; not an XOR, and one call does not swap (the highlight swaps through a third colour with three calls) |
| 1000:18af | `Gfx_SetDirtyTracking` | `Gfx_SetColor` | stores AL in DS:37ae, whose bit 0 makes the fill and blit tails record dirty spans; no colour is set |
| 130f:0edf | `RankChest_Update` | `RankScreen_Update`, `Rank_Update` | the per-frame updater of the rank chest screen (scene 4, cell 1D) |
| 130f:1261 | `RankChest_DrawStar` | `RankScreen_DrawStar`, `Rank_DrawChestCoin` | draws one star (SHACK sprite 125) of the rank chest; prefix as RankChest_Update |
| 4a03:04a8 | `ShelfRoom_DrawShelf` | `PrizeRoom_DrawShelf`, `Treasures_DrawFound` | draws the collected treasures of the treasure shelf room being shown; "shelf room" as the world doc and the cell names (Cell_P50_ShelfEnter..), "prize room" was ambiguous with the treasure room 98-9B |
| 4d94:0102 | `g_boxSaved` | `g_fullPicPending` | set to 1 by MsgBox_Draw (43cb:0592) after Sprite_SaveUnder on a slow CPU; Screen_DrawOverlay restores the saved background |
| 4d94:37ee | `g_camStepX` | `g_scrollDX` | camera step of the last Player_Move; the world doc owns the camera |
| 4d94:37f0 | `g_camStepY` | `g_scrollDY` | as g_camStepX |
| 4d94:37f2 | `g_frameCamX` | `g_camXFrame` | frame latch written by Frame_Begin; all latches are g_frame* (g_frameCamStepX, g_frameTransition, ...) |
| 4d94:37f4 | `g_frameCamY` | `g_camYFrame` | as g_frameCamX |
| 4d94:8f9f | `g_frameInShelfRoom` |  | frame latch of DS:9201..9204 (in shelf room k); the old name g_frameTunnelSeen followed the ARCHITECTURE misreading of the shelf-room cells as tunnels |
| 4d94:8fa5 | `g_slowCpuSaved` | `g_savedSlowCpu` | three of four proposals |
| 4d94:8fa7 | `g_playerScrX` | `g_plrScreenX` | player-owned; matches g_playerX/g_playerY |
| 4d94:8fa9 | `g_playerScrY` | `g_plrScreenY` | as g_playerScrX |
| 4d94:8fbd | `g_frameRankState` | `g_rankScreenFrame` | frame latch of g_rankState |
| 4d94:90f8 | `g_promptIcon` | `g_promptFrame`, `g_statusPrompt` | selects the status-bar prompt frame drawn by Hud_DrawPromptIcon |
| 4d94:9100 | `g_secondTicks` | `g_tick60`, `g_tickSecond` | game ticks left in the current hint second, counted by Game_Tick |
| 4d94:9124 | `g_camCellCol` | `g_camBlockCol` | a 128x128 block is a cell everywhere in the world doc |
| 4d94:9126 | `g_camCellRow16` | `g_camBlockRow16` | as g_camCellCol |
| 4d94:9129 | `g_shelfCamRoom` | `g_prizeRoom` | compared with the shelf room index in ShelfRoom_DrawShelf (4a03:0607): that room is placed relative to camX 0x440, the others to 0x480 |
| 4d94:9136 | `g_startDrive` | `g_bootDrive` | getdisk() at start-up (1460); < 2 = a floppy drive |
| 4d94:9201 | `g_inShelfRoom` | `g_inPrizeRoom` | set and cleared by the shelf-room cells P50..P66 |
| 4d94:920d | `g_camTileColOff` | `g_camTileColOfs` | camera owner |
| 4d94:920f | `g_camTileRowOff` | `g_camTileRowOfs` | camera owner |
| 4d94:922e | `g_hudRedraw` | `g_redrawRequest` | redraw request for the screen painter (HUD, quiz or sign-in); consistent with g_hudRedrawReq2 (DS:925b); g_redrawRequest was also proposed for DS:924f |
| 4d94:9238 | `g_cmdConsumed` | `g_cmdImmediate`, `g_inputReady` | UI handlers set it to 1 when they have used g_cmd; Input_Poll only replaces g_cmd while it is non-zero (otherwise it queues in DS:90f6) |
| 4d94:924f | `g_fullRedrawReq` | `g_redrawReq`, `g_redrawRequest` | full play-field redraw request, latched into g_fullRedraw (DS:8fbb); pairs with g_partialRedrawReq |
| 4d94:9254 | `g_storedShowcase` | `g_shownStash` | the showcase treasure put away by Cell_P12/Cell_P11; "showcase" as Treasure_PickShowcase |
| 4d94:925d | `g_rankState` | `g_rankScreen` | a state (0 off, 1 start, 2 filling), latched into g_frameRankState |
| 4d94:926a | `g_playerMask` | `g_plrCellMask` | player-owned |
| 4d94:926c | `g_menuLock` | `g_busy`, `g_hideMenuBar` | set during searches and end scenes; Input_Tick drops menu commands, Menu_Update and Scene_Update keep the bar closed, hints and messages wait |
| 4d94:9278 | `g_partialRedrawReq` | `g_partialRedraw` | pairs with g_fullRedrawReq and its latch g_framePartialRedraw |
| 4d94:9283 | `g_spriteSlotsDrawn` | `g_drawSlotCount` | set by the main loop (13 or 1) |
| 4d94:929f | `g_saveHeapHigh` | `g_usersHeader1` | raised by the animation code as a high-water mark; its low byte only travels through USERS.DAT header byte 1 |
| 4d94:92a6 | `g_rankRowsDone` | `g_chestStep`, `g_rankFillRow` | rank chest fill: rows filled so far |
| 4d94:92a8 | `g_rankRowsWanted` | `g_chestTarget`, `g_rankFillTarget` | rank chest fill: rows to fill |
| 4d94:92b0 | `g_rankTickPhase` | `g_chestClock`, `g_rankTickGroup` | DS:9100 & 0xfc at the last chest row |

Other renames of older names by the owning wave 1 agent (old → canonical):

130f:0c2e `Hud_DrawHintIcon` → `Hud_DrawPromptIcon`, 1568:0008 `Cell_P5` → `Cell_P5_CaveDoorW`, 1568:00ce `Cell_P6` → `Cell_P6_CaveExitW`, 1568:01b4 `Cell_P9` → `Cell_P9_CaveDoorE`, 1568:029d `Cell_P10` → `Cell_P10_CaveExitE`, 1568:0303 `Cell_P34` → `Cell_P34_CaveUp`, 1568:0341 `Cell_P35` → `Cell_P35_UpperRoomDown`, 1568:0386 `Cell_P11` → `Cell_P11_ClubDoor`, 1568:03f6 `Cell_P12` → `Cell_P12_StartRoom`, 1568:05cb `Cell_P7` → `Cell_P7_WallLeft`, 1568:0638 `Cell_P8` → `Cell_P8_WallRight`, 1568:06a5 `Cell_P50` → `Cell_P50_ShelfEnter`, 1568:0718 `Cell_P51` → `Cell_P51_ShelfExit`, 1568:078a `Cell_P52` → `Cell_P52_Shelf0to1`, 1568:07fc `Cell_P53` → `Cell_P53_Shelf1to0`, 1568:0880 `Cell_P54` → `Cell_P54_Shelf1to2`, 1568:08f2 `Cell_P55` → `Cell_P55_Shelf2to1`, 1568:0976 `Cell_P65` → `Cell_P65_Shelf2to3`, 1568:09e8 `Cell_P66` → `Cell_P66_Shelf3to2`, 1568:0a6c `Cell_P30_V30` → `Cell_P30_V30_TrunkTop`, 1568:0b38 `Cell_P17` → `Cell_P17_MineDoorW`, 1568:0b86 `Cell_P37` → `Cell_P37_MineExitW`, 1568:0bfb `Cell_P18` → `Cell_P18_MineDoorE`, 1568:0c49 `Cell_P38` → `Cell_P38_MineExitE`, 1568:0cbe `Cell_P44` → `Cell_P44_CastleDoor`, 1568:0d94 `Cell_P23` → `Cell_P23_FallShaft`, 1568:0df3 `Cell_P47` → `Cell_P47_Boundary01`, 1568:0e13 `Cell_P19` → `Cell_P19_Boundary12`, 1568:0e33 `Cell_P24` → `Cell_P24_Wall`, 1568:0e41 `Cell_P46_V46` → `Cell_P46_V46_Boundary01`, 1568:0ee3 `Cell_DrawSignTile` → `Cell_DrawTreetop`, 1568:0f13 `Cell_P25` → `Cell_P25_TrunkShaft`, 1568:0f17 `Cell_P31` → `Cell_P31_GateL0`, 1568:1010 `Cell_P26` → `Cell_P26_Unused`, 1568:1037 `Cell_P27` → `Cell_P27_Unused`, 1568:105e `Cell_P28` → `Cell_P28_Path`, 1568:1077 `Cell_P29` → `Cell_P29_Unused`, 1568:1090 `Cell_P36_V36` → `Cell_P36_V36_NetShop`, 1568:1286 `Cell_P40_V40` → `Cell_P40_V40_Geyser`, 1568:1329 `Cell_P39_V39` → `Cell_P39_V39_GateL1`, 1568:141c `Cell_P41_V41` → `Cell_P41_V41_GeyserTop`, 1568:1556 `Cell_AnimateWater` → `Cell_DrawGeyser`, 1568:15bc `Cell_P45_V45` → `Cell_P45_V45_Bridge`, 1568:16ed `Cell_P48` → `Cell_P48_Landing`, 1568:1734 `Cell_P56_V56` → `Cell_P56_V56_CastleHall`, 1568:182b `Cell_P57_V57` → `Cell_P57_V57_CastleWall`, 1568:1953 `Cell_P60` → `Cell_P60_IceDoor`, 1568:19ab `Cell_P61` → `Cell_P61_IceCaveExit`, 1568:1a20 `Cell_P62` → `Cell_P62_IceSlide`, 1568:1aa4 `Cell_P63_V63` → `Cell_P63_V63_CastleTop`, 1568:1c7b `Cell_V49` → `Cell_V49_Showcase`, 1568:1d96 `Cell_V32` → `Cell_V32_AnimDecor`, 1568:1eca `Cell_SetBorderColor` → `Cell_P0_Void`, 176a:02dc `Cell_P42` → `Cell_P42_GateL2`, 176a:0404 `Cell_P64` → `Cell_P64_EndScreen`, 176a:0485 `Cell_P20` → `Cell_P20_CastleGate`, 176a:0596 `Cell_P70` → `Cell_P70_TreasureRoom`, 176a:0f47 `Shack_Leave` → `TreasureRoom_Leave`, 176a:0fa6 `Cell_P71_V71` → `Cell_P71_V71_ToRankScreen`, 176a:1094 `Cell_P72` → `Cell_P72_RankScreen`, 176a:1162 `Shack_AnimA` → `ShackPair_08_36_Approach`, 176a:11b7 `Shack_AnimB` → `ShackPair_08_36_Swap`, 176a:1220 `Shack_AnimC` → `ShackPair_0e_2c`, 176a:1275 `Shack_AnimD` → `ShackPair_06_5a`, 176a:129d `Shack_AnimE` → `ShackPair_1c_22`, 176a:12f2 `Shack_AnimF` → `ShackPair_18_58`, 176a:1347 `Shack_AnimG` → `ShackFly_1e`, 176a:13d7 `Shack_AnimH` → `ShackFly_50`, 176a:1463 `Shack_AnimI` → `ShackTrio_4a_4c_4e`, 18cf:000d `PlrSt_00` → `PlrSt_00_Idle`, 18cf:00a9 `PlrSt_41` → `PlrSt_41_EndIntro`, 18cf:00c4 `PlrSt_42` → `PlrSt_42_EndHorn`, 18cf:00f8 `PlrSt_43` → `PlrSt_43_EndBeam`, 18cf:0122 `PlrSt_44` → `PlrSt_44_EndSparkle`, 18cf:014d `PlrSt_45` → `PlrSt_45_EndChestOpen`, 18cf:0182 `PlrSt_46` → `PlrSt_46_EndStep`, 18cf:01a3 `PlrSt_47` → `PlrSt_47_EndStep`, 18cf:01cd `PlrSt_48` → `PlrSt_48_EndFlightInit`, 18cf:0205 `PlrSt_49` → `PlrSt_49_EndFlightNext`, 18cf:0280 `PlrSt_50` → `PlrSt_50_EndFlightStart`, 18cf:02a7 `PlrSt_51` → `PlrSt_51_EndFlightStep`, 18cf:02f2 `PlrSt_52` → `PlrSt_52_EndRank`, 18cf:0343 `PlrSt_53` → `PlrSt_53_EndRankSparkle`, 18cf:0398 `PlrSt_54` → `PlrSt_54_EndRankWait`, 18cf:03a4 `PlrSt_55` → `PlrSt_55_EndCrown`, 18cf:0441 `PlrSt_56` → `PlrSt_56_EndCreature`, 18cf:049b `PlrSt_57` → `PlrSt_57_EndPlayer`, 18cf:04ca `PlrSt_58` → `PlrSt_58_EndShowcaseCheck`, 18cf:050e `PlrSt_59` → `PlrSt_59_EndShowcaseStart`, 18cf:0546 `PlrSt_60` → `PlrSt_60_EndShowcaseStep`, 18cf:05b2 `PlrSt_61` → `PlrSt_61_EndDone`, 18cf:05c4 `PlrSt_63` → `PlrSt_63_ChampSparkle`, 18cf:05eb `PlrSt_64` → `PlrSt_64_ChampChest`, 18cf:0625 `PlrSt_65` → `PlrSt_65_ChampArcs`, 18cf:0685 `PlrSt_66` → `PlrSt_66_ChampScream`, 18cf:06be `PlrSt_67` → `PlrSt_67_ChampCrownFly`, 18cf:06f4 `PlrSt_68` → `PlrSt_68_ChampCatch`, 18cf:0738 `PlrSt_69` → `PlrSt_69_ChampCrowned`, 18cf:0773 `PlrSt_70` → `PlrSt_70_ChampSparkle`, 18cf:07ea `PlrSt_71` → `PlrSt_71_ChampSparkleWait`, 18cf:07f6 `PlrSt_05` → `PlrSt_05_Searching`, 18cf:081b `PlrSt_09` → `PlrSt_09_AfterTeleport`, 18cf:082a `PlrSt_03` → `PlrSt_03_Spawn`, 18cf:0831 `PlrSt_04` → `PlrSt_04_Arrive`, 18cf:08cc `PlrSt_10` → `PlrSt_10_OnPath`, 18cf:0b9e `PlrSt_11` → `PlrSt_11_WalkLeft`, 18cf:0ca0 `PlrSt_12` → `PlrSt_12_WalkRight`, 18cf:0da2 `PlrSt_13` → `PlrSt_13_LookUp`, 18cf:0e39 `PlrSt_14` → `PlrSt_14_Front`, 18cf:0ed8 `PlrSt_15` → `PlrSt_15_ClimbUp`, 18cf:0fa9 `PlrSt_16` → `PlrSt_16_ClimbDown`, 18cf:107a `PlrSt_17` → `PlrSt_17_JumpLeft`, 18cf:10df `PlrSt_18` → `PlrSt_18_JumpLeftLand`, 18cf:1120 `PlrSt_19` → `PlrSt_19_JumpRight`, 18cf:1185 `PlrSt_20` → `PlrSt_20_JumpRightLand`, 18cf:11c6 `PlrSt_21` → `PlrSt_21_NetLeftSwing`, 18cf:11d6 `PlrSt_22` → `PlrSt_22_NetLeftHold`, 18cf:11fd `PlrSt_23` → `PlrSt_23_NetLeftDone`, 18cf:1224 `PlrSt_24` → `PlrSt_24_NetRightSwing`, 18cf:1234 `PlrSt_25` → `PlrSt_25_NetRightHold`, 18cf:125b `PlrSt_26` → `PlrSt_26_NetRightDone`, 18cf:1282 `PlrSt_27` → `PlrSt_27_CoinLeftDone`, 18cf:12bc `PlrSt_28` → `PlrSt_28_CoinRightDone`, 18cf:12f6 `PlrSt_29` → `PlrSt_29_PassageUp`, 18cf:1367 `PlrSt_30` → `PlrSt_30_SlideDown`, 18cf:13b9 `PlrSt_31` → `PlrSt_31_FallLeft`, 18cf:140c `PlrSt_32` → `PlrSt_32_FallRight`, 18cf:145f `PlrSt_33` → `PlrSt_33_LadderJump`, 18cf:1495 `PlrSt_34` → `PlrSt_34_Chute`, 18cf:14b1 `PlrSt_35` → `PlrSt_35_TreasureRoomLand`, 1a54:085f `Castle_ResetCells` → `Cells_ResetVariable`, 43cb:12cf `HotZones_Setup` → `Box_SetupInput`, 47c7:05a9 `SignIn_DrawCaret` → `SignIn_DrawTypedChar`, 4ba4:0233 `TitleStep_Finish` → `TitleStep_StartWalkoff`, 4ba4:027a `TitleStep_Clear` → `TitleStep_WaitWalkoff`, 4ba4:02a3 `TitleStep_Done` → `TitleStep_WaitActorGone`, 4d94:120a `g_castleCells` → `g_variableCells`, 4d94:121c `g_castleDefaultPics` → `g_variableCellDefaults`.

`g_redrawRequest` was proposed for two addresses: in `code_ui.md` it meant DS:922e (now
`g_hudRedraw`), in `code_player.md` DS:924f (now `g_fullRedrawReq`).

After the merge, for consistency with the decision table: `4d94:9270` `g_drawnBlockCol` →
`g_drawnCellCol`, `4d94:9272` `g_drawnBlockRow16` → `g_drawnCellRow16`, `4d94:9299`
`g_drawnTileColOfs` → `g_drawnTileColOff`, `4d94:929d` `g_drawnTileRowOfs` → `g_drawnTileRowOff`.

## DGROUP tables (wave 2)

130 data labels were added to `dgroup.tsv` for the tables the engine reads (palettes, picture
attribute tables, menus and boxes, help pictures, quiz and clue tables, sign-in tables, sound
driver, hint tables, input key/command/zone lists, AdLib instruments, title tables, the Turbo C
`rand` seed), and some types were corrected against the bytes (DS:0476 `rec22[19]` = 18 files and
an all-zero record, DS:4f5a `far[194]`, DS:5fd2 `u16[7]`, DS:62b7 `far[19]`, DS:1252 `u16[4][20]`,
DS:8d83 `near[14]`). The same names, as `k<Name>` constants (`g_cellPicture` → `kCellPicture`,
`s_btnDone` → `kStrBtnDone`), are in `src/dos/data/dgroup_offsets.h`; add a row here before adding a
constant there.

## Wave 5: the AdLib driver (segment 4ac9)

The wave-5 port of the AdLib driver (`src/dos/audio/adlib_driver.h`) found that many wave-0 names of
segment 4ac9 did not describe the functions: they are the AdLib sample driver's routines (`SetMode`,
`SetVoiceTimbre`, `SndSetAllPrm`, ...), and the register each writes is in the disassembly
(`re/dos/docs/fmt_sound.md` §5.5). Renamed (old → canonical): 4ac9:0103 `AdLib_KeyOffAll` →
`AdLib_SetMode`, 4ac9:010d `AdLib_ResetRegs` → `AdLib_SetWaveSel`, 4ac9:0150 `AdLib_InitTables` →
`AdLib_SetPitchRange`, 4ac9:0179 `AdLib_Silence` → `AdLib_SetGlobalParams`, 4ac9:019a `AdLib_LoadTune`
→ `AdLib_SetVoiceTimbre`, 4ac9:054a `AdLib_SetTempo` → `AdLib_InitSlotVolume`, 4ac9:055c
`AdLib_CalcFreq` → `AdLib_CalcFNum`, 4ac9:0601 `AdLib_CalcBlock` → `AdLib_SetFNumRow`, 4ac9:067e
`AdLib_BuildFreqTable` → `AdLib_InitFNums`, 4ac9:081a `AdLib_SetInstrument` → `AdLib_SetSlotParam`,
4ac9:0847 `AdLib_SetVoiceInstr` → `AdLib_SetSlotParams`, 4ac9:08db `AdLib_WriteOperator` →
`AdLib_WriteParam`, 4ac9:095e `AdLib_WriteOperator2` → `AdLib_WriteAllParams`, 4ac9:099c
`AdLib_SetFeedback` → `AdLib_WriteKslLevel` (0x40), 4ac9:0a0e `AdLib_SetAM` → `AdLib_WriteNoteSel`
(0x08), 4ac9:0a29 `AdLib_SetLevel` → `AdLib_WriteFeedbackFm` (0xC0), 4ac9:0b26 `AdLib_SetMult` →
`AdLib_WriteAmVibEgKsrMulti` (0x20), 4ac9:0bd6 `AdLib_SetRhythm` → `AdLib_WriteAmVibRhythm` (0xBD).
The driver's variables (BSS DS:C6E4..CB7C and the slot tables DS:8148, DS:815A) were added to
`dgroup.tsv` as `g_opl*`, at the offsets of `AdLibDriver::kDs*`.
