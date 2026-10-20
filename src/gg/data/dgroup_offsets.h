#pragma once
// Named DGROUP offsets (DS:XXXX = Ghidra 1ea7:XXXX = ssg.exe file offset 0xFC70 + XXXX) of Gizmos &
// Gadgets DOS v1.0: every table the engine reads from the user's ssg.exe and every run-time global
// of the original, with the canonical name of re/names/gg/dgroup.tsv (g_roomSetIds -> kRoomSetIds,
// s_playersDat -> kStrPlayersDat) and the doc section that specifies it. The data itself is always
// read from the user's file through GgExe (gg_exe.h: ds8/ds16/ds32/dsBytes/dsString/dsFarPtr); the
// run-time globals (DS:4552 and above: BSS, zero in the file) are listed for the engine's state
// structs and for comparing the engine with DOSBox-X save states (tools/gg/capture_mem.py).
//
// Types (the dgroup.tsv column): u8/s8/u16/i16/u32, far = {u16 off, u16 seg} with an image segment
// (0x0ea7 = DGROUP; GgExe::dsFarPtr adds 0x1000, GgExe::resolveCode follows overlay stubs), near = a
// u16 offset in the owning segment (DGROUP for the data tables: the player transitions DS:127e, the
// chimp tables DS:1d2e / DS:1f10), zone = a 22-byte click-zone record (code_runtime.md §8.1),
// MenuDef = 0x14 bytes, MenuBarDef = {u16 count, far MenuDef[]} (code_ui.md §6.1), rec[N] = a record
// of N bytes, rec[n][N] = n records of N bytes. For arrays the header gives kNameCount (first
// dimension) and kNameStride (bytes per element of the first dimension, when it has more than one
// dimension), for single records kNameSize; an empty [] means the length is not fixed by the code.
//
// Groups follow the spec section that describes the labels (the section with the most mentions),
// in the reading order of re/gg/docs. Keep the names in step with re/names/gg/dgroup.tsv (a new
// table gets its row there first); the few tables the docs use that have no row yet are at the end.

#include <cstddef>
#include <cstdint>

namespace tmport::gg::dg {

// --- Segment facts (ARCHITECTURE.md §1, conventions) ------------------------------------------------------
inline constexpr uint16_t kDgroupSize = 0xBD04;       // DGROUP, from the segment table
inline constexpr uint16_t kInitEnd = 0x4552;          // DS:0000..4551 initialised in the file, BSS after
inline constexpr uint32_t kDsFileOffset = 0xFC70;     // file offset of DS:0000 in ssg.exe
inline constexpr uint16_t kDgroupImageSeg = 0x0EA7;   // DGROUP's segment as stored in far pointers

// --- Time base (code_runtime.md §1) ----------------------------------------------------------------------------------
inline constexpr uint16_t kOldInt8 = 0x0212;        // g_oldInt8 far: previous INT 8 vector (§1.2)
inline constexpr uint16_t kBiosChainCounter = 0x0216; // g_biosChainCounter i16: Timer_ISR chains the BIOS INT 8 when it
                                                      // is >= 0 after the increment; starts at 0 and is never set
                                                      // negative, so the BIOS runs on every tick (8x fast) (§1.2)
inline constexpr uint16_t kTicks16 = 0x021E;        // g_ticks16 u16: second tick counter (Timer_ISR) (§1.2)

// --- Start-up (code_runtime.md §2) -----------------------------------------------------------------------------------
inline constexpr uint16_t kStateTable = 0x00C2;     // g_stateTable far[19]: state functions run by Seq_Run from main
                                                    // (index = state 1..17; entry 18 is NULL) (§2.1)
inline constexpr size_t kStateTableCount = 19;
inline constexpr uint16_t kDisplayCode = 0x244E;    // g_displayCode i16: -1 = test; Video_DccSupported result (0x1a) or
                                                    // 8 (-GV); must be >= 7 (§2.2)
inline constexpr uint16_t kCpuType = 0x2450;        // g_cpuType i16: Cpu_Detect result (1 8086, 2 286, 3 386, 4 486); 2
                                                    // halves the chimp count (§2.2)
inline constexpr uint16_t kSoundDevice = 0x2452;    // g_soundDevice i16: -s switch: -1 auto (default), 0 off, 1
                                                    // speaker, 4 FM (§2.2)
inline constexpr uint16_t kRandSeed = 0x2454;       // g_randSeed u16: the low word of time(NULL) passed to srand (§2.2)
inline constexpr uint16_t kInitTable = 0x452E;      // g_initTable rec[]: _INIT_ table (6-byte records, DS:452e..454c)
                                                    // (§2.1)
inline constexpr uint16_t kState = 0x455C;          // g_state i16: Seq_Run state of the main sequence (§2.1)

// --- Shutdown and exit paths (code_runtime.md §4) --------------------------------------------------------------------
inline constexpr uint16_t kShutdownFlag = 0x00C0;   // g_shutdownFlag i16: set to -1 by Game_Shutdown; no reader
inline constexpr uint16_t kVideoModeSet = 0x01F6;   // g_videoModeSet i16: 1 after Video_SetMode13h (Video_RestoreMode
                                                    // switches back only then)
inline constexpr uint16_t kExitTable = 0x454C;      // g_exitTable rec[]: _EXIT_ table (DS:454c..4552)

// --- Deadline timers (152c) (code_runtime.md §5) ---------------------------------------------------------------------
inline constexpr uint16_t kTicks = 0x021A;          // g_ticks u32: PIT ticks at 145.652 Hz (Timer_ISR); the clock of
                                                    // the deadline timers
inline constexpr uint16_t kTimers = 0x6578;         // g_timers u32[8]: deadline timer slots (Timer_Set)
inline constexpr size_t kTimersCount = 8;

// --- Keyboard (code_runtime.md §6) -----------------------------------------------------------------------------------
inline constexpr uint16_t kKbdPlayMode = 0x0096;    // g_kbdPlayMode i16: 1 = play mode: Kbd_ISR swallows the held keys
                                                    // instead of queueing them (State_05_EnterRoom sets it) (§6.2)
inline constexpr uint16_t kCtrlHeld = 0x020C;       // g_ctrlHeld i16: Ctrl held (Kbd_MakeCtrl / Kbd_BreakCtrl) (§6.2)
inline constexpr uint16_t kOldInt9 = 0x020E;        // g_oldInt9 far: previous INT 9 vector (§6.1)
inline constexpr uint16_t kHeldKeys = 0x0218;       // g_heldKeys u16: held-key bits: 1 Left, 2 Right, 4 Up, 8 Down,
                                                    // 0x10 Ctrl, 0x20 Space (§6.2)
inline constexpr uint16_t kKbdScanCodes = 0x0224;   // g_kbdScanCodes u8[8]: scan codes handled by Kbd_ISR: 4B 4D 48 50
                                                    // 39 1D 2E (Left Right Up Down Space Ctrl C), 0 (§6.2)
inline constexpr size_t kKbdScanCodesCount = 8;
inline constexpr uint16_t kKbdMakeHandlers = 0x022C; // g_kbdMakeHandlers near[7]: make handlers per scan code
                                                     // (1454:0187..) (§6.2)
inline constexpr size_t kKbdMakeHandlersCount = 7;
inline constexpr uint16_t kKbdBreakHandlers = 0x023A; // g_kbdBreakHandlers near[7]: break handlers per scan code
                                                      // (1454:018f..) (§6.2)
inline constexpr size_t kKbdBreakHandlersCount = 7;
inline constexpr uint16_t kKbdRingWrite = 0x1310;   // g_kbdRingWrite i16: write index of the key event ring DS:8556
                                                    // (§6.4)
inline constexpr uint16_t kKbdRingRead = 0x1312;    // g_kbdRingRead i16: read index of the key event ring (§6.4)
inline constexpr uint16_t kKbdRing = 0x8556;        // g_kbdRing rec[16][16]: key event ring {type 3, u32 tick} (§6.4)
inline constexpr size_t kKbdRingCount = 16;
inline constexpr size_t kKbdRingStride = 16;

// --- Mouse (code_runtime.md §7) --------------------------------------------------------------------------------------
inline constexpr uint16_t kMouseButtons = 0x0A0C;   // g_mouseButtons u16: BX of the last mouse event (bit 0 left, bit 1
                                                    // right); read by puzzles (§7.2)
inline constexpr uint16_t kMouseLatch = 0x0A10;     // g_mouseLatch: mouse event latches written by Mouse_EventHandler
                                                    // (DS:0a10..0a2c: press, release + position + tick stamp) (§7.2)
inline constexpr uint16_t kMousePressX = 0x0A1A;    // g_mousePressX i16: press record x (event x/2 + hot spot) (§7.2)
inline constexpr uint16_t kMousePressY = 0x0A1C;    // g_mousePressY i16: press record y (§7.2)
inline constexpr uint16_t kMouseRelease = 0x0A20;   // g_mouseRelease rec[16]: release record: +0 = 2 when pending, +6
                                                    // u32 tick, +0xa x, +0xc y (§7.2)
inline constexpr size_t kMouseReleaseSize = 16;
inline constexpr uint16_t kMouseReleaseTick = 0x0A26; // g_mouseReleaseTick u32: tick of the last left release (§7.2)
inline constexpr uint16_t kMouseReleaseX = 0x0A2A;  // g_mouseReleaseX i16: release x (event x/2 + hot spot) (§7.2)
inline constexpr uint16_t kMouseReleaseY = 0x0A2C;  // g_mouseReleaseY i16: release y (§7.2)
inline constexpr uint16_t kCursorRegionDefault = 0x0A54; // g_cursorRegionDefault rec[28]: default cursor region: full
                                                         // screen, priority 0, record pointer DS:0a6c
                                                         // (Mouse_SetCursorPicture) (§7.1)
inline constexpr size_t kCursorRegionDefaultSize = 28;
inline constexpr uint16_t kMouseMaxY = 0x7A62;      // g_mouseMaxY i16: Mouse_SetBounds: y + h - cursor height (§7.3)
inline constexpr uint16_t kMouseMaxX = 0x7A64;      // g_mouseMaxX i16: Mouse_SetBounds: x + w - cursor width (§7.3)
inline constexpr uint16_t kCursorHotY = 0x7A76;     // g_cursorHotY i16: active cursor hot spot y (§7.2)
inline constexpr uint16_t kMouseY = 0x7A86;         // g_mouseY i16: cursor y (top-left of the cursor picture) (§7.1)

// --- Click zones (17ab) (code_runtime.md §8) -------------------------------------------------------------------------
inline constexpr uint16_t kHubZones = 0x0132;       // g_hubZones rec[8][0x16]: hub click zones (release codes
                                                    // 0xbb8..0xbbb); hover cursor = code - 0xbb8 (§8.1)
inline constexpr size_t kHubZonesCount = 8;
inline constexpr size_t kHubZonesStride = 22;
inline constexpr uint16_t kZonesLocal = 0x0A32;     // g_zonesLocal far: active click-zone list (pushed and popped by
                                                    // Zones_Push/Zones_Pop) (§8.2)
inline constexpr uint16_t kZonesGlobal = 0x0A36;    // g_zonesGlobal far: global click zones (menu titles, HUD) (§8.2)
inline constexpr uint16_t kZoneClickEnter = 0x0A3A; // g_zoneClickEnter rec[22]: full-screen zone (0,0,320,200, priority
                                                    // 5, release code 0x0d): a click anywhere = Enter (attract loop)
                                                    // (§8.1)
inline constexpr size_t kZoneClickEnterSize = 22;
inline constexpr uint16_t kZoneStackDepth = 0x0A50; // g_zoneStackDepth i16: Zones_Push depth (§8.2)
inline constexpr uint16_t kWorkshopZones = 0x21CE;  // g_workshopZones: static click zones of the workshop (§8.1)
inline constexpr uint16_t kWorkbenchZone = 0x2268;  // g_workbenchZone rec[22]: workshop drop zone for a carried part
                                                    // (72,117,180,48), release 1006, priority 50 (§8.1)
inline constexpr size_t kWorkbenchZoneSize = 22;
inline constexpr uint16_t kPostRaceZones = 0x22FE;  // g_postRaceZones: post-race buttons (codes 2000..2004) (§8.1)
inline constexpr uint16_t kZoneStack = 0x7A4E;      // g_zoneStack far[]: saved zone lists (Zones_Push) (§8.2)

// --- Input_GetCode 198d:0106 and related (code_runtime.md §9) --------------------------------------------------------
inline constexpr uint16_t kMenuDisabled = 0x054E;   // g_menuDisabled i16: non-zero: Input_GetCode does not open the
                                                    // menus (§9.3)
inline constexpr uint16_t kMouseNewPress = 0x0A1E;  // g_mouseNewPress i16: 1 after a left press until Input_GetCode
                                                    // reports it (| 0x4000) (§9.1)
inline constexpr uint16_t kEscHandler = 0x8656;     // g_escHandler far: Esc callback of Input_GetCode (§9.3)

// --- Screen buffer and graphics primitives (1399, 1473) (code_runtime.md §10) ----------------------------------------
inline constexpr uint16_t kClipLeft = 0x01E4;       // g_clipLeft i16: clip rectangle left (0); with DS:01e6 right
                                                    // (319), DS:01e8 top (0), DS:01ea bottom (199) (§10.2)
inline constexpr uint16_t kClipRight = 0x01E6;      // g_clipRight i16: clip rectangle right (319, inclusive in the
                                                    // keyed blit, exclusive in Gfx_ClipX) (§10.2)
inline constexpr uint16_t kClipTop = 0x01E8;        // g_clipTop i16: vertical clip of Lpx_BlitRawKeyed (inferred: top)
                                                    // (§10.2)
inline constexpr uint16_t kClipBottom = 0x01EA;     // g_clipBottom i16: vertical clip of Lpx_BlitRawKeyed (inferred:
                                                    // bottom) (§10.2)
inline constexpr uint16_t kBlitX = 0x01EC;          // g_blitX i16: x of the last blit's rectangle (-1 = nothing drawn:
                                                    // no dirty mark); with DS:01ee y, DS:01f0 w, DS:01f2 h (§10.4)
inline constexpr uint16_t kBlitY = 0x01EE;          // g_blitY i16: y of the last blit's rectangle
inline constexpr uint16_t kBlitW = 0x01F0;          // g_blitW i16: width of the last blit's rectangle
inline constexpr uint16_t kBlitH = 0x01F2;          // g_blitH i16: height of the last blit's rectangle
inline constexpr uint16_t kDrawColor = 0x01F4;      // g_drawColor u8: colour of the line, rectangle and pixel
                                                    // primitives (§10.1)
inline constexpr uint16_t kVram = 0x01F8;           // g_vram far: A000:0000 (§10.1)
inline constexpr uint16_t kLpxKeyedBlitters = 0x01FC; // g_lpxKeyedBlitters near[4]: Lpx_DrawKeyed blitters by
                                                      // (fmt+mode)&3: 0399, 0465, 0399, 04d1 (§10.4)
inline constexpr size_t kLpxKeyedBlittersCount = 4;
inline constexpr uint16_t kLpxBlitters = 0x0204;    // g_lpxBlitters near[2]: Lpx_Draw blitters by format: 036d, 0465
                                                    // (§10.4)
inline constexpr size_t kLpxBlittersCount = 2;
inline constexpr uint16_t kScreenBuffer = 0x0248;   // g_screenBuffer far: the 128 000-byte screen + back page
                                                    // (Gfx_Init) (§10.1)
inline constexpr uint16_t kPalFaded = 0x024C;       // g_palFaded i16: 1 while faded to black (palette loads are
                                                    // deferred) (§10.1)
inline constexpr uint16_t kRowTable = 0x5F22;       // g_rowTable far[400]: row pointers: rows 0..199 screen, 200..399
                                                    // back page (§10.1)
inline constexpr size_t kRowTableCount = 0x190;

// --- Dirty tiles, flush and the sprite list (1581) (code_runtime.md §11) ---------------------------------------------
inline constexpr uint16_t kMouseHideState = 0x01E2; // g_mouseHideState i16: 0 idle; the first drawing call after a
                                                    // reset restores the background under the cursor (§11.2)
inline constexpr uint16_t kSpriteCount = 0x05F4;    // g_spriteCount i16: records in the sprite list DS:05f6 (§11.3)
inline constexpr uint16_t kSprites = 0x05F6;        // g_sprites rec[50][14]: sprite list {u16 flags, x, y, w, h, far
                                                    // picture} (§11.3)
inline constexpr size_t kSpritesCount = 50;
inline constexpr size_t kSpritesStride = 14;
inline constexpr uint16_t kSpriteWritePtr = 0x08B2; // g_spriteWritePtr far: next free record of the sprite list (§11.3)
inline constexpr uint16_t kDirtyPending = 0x08B6;   // g_dirtyPending i16: Dirty_Mark calls since the last flush or
                                                    // clear; Dirty_Flush does nothing while it is 0 (§11.1)
inline constexpr uint16_t kDirtyTileCount = 0x08B8; // g_dirtyTileCount u32: running total of tiles copied by
                                                    // Dirty_Flush (statistics, no reader) (§11.2)
inline constexpr uint16_t kFlushCount = 0x08BC;     // g_flushCount i16: Dirty_Flush calls that copied (statistics, no
                                                    // reader) (§11.2)

// --- Software cursor (17ab) (code_runtime.md §12) --------------------------------------------------------------------
inline constexpr uint16_t kCursorHidden = 0x0A30;   // g_cursorHidden i16: 1 = Mouse_DrawCursor draws nothing
                                                    // (Mouse_Hide / Mouse_Show) (§12.2)
inline constexpr uint16_t kCursorForceDefault = 0x0A52; // g_cursorForceDefault i16: Mouse_ForceDefault: use the region
                                                        // DS:0a70 instead of the region list (§12.1)
inline constexpr uint16_t kCursorRegionForced = 0x0A70; // g_cursorRegionForced rec[28]: cursor region used while
                                                        // DS:0a52 is set (record pointer DS:0a88) (§12.1)
inline constexpr size_t kCursorRegionForcedSize = 28;
inline constexpr uint16_t kCursorRegions = 0x0A8C;  // g_cursorRegions far: list of cursor regions (19c0 list) (§12.1)
inline constexpr uint16_t kCursorActiveRecord = 0x7A4A; // g_cursorActiveRecord far: cursor record currently copied to
                                                        // DS:7a6a (§12.1)
inline constexpr uint16_t kCursor = 0x7A6A;         // g_cursor rec[0x18]: active cursor record copy: +2/+4 save-under
                                                    // position, +6/+8 size, +0xa/+0xc hot spot, +0xe picture, +0x12
                                                    // shown, +0x14 save buffer (§12.1)
inline constexpr size_t kCursorSize = 24;
inline constexpr uint16_t kCursorShown = 0x7A7C;    // g_cursorShown i16: the software cursor is drawn (§12.1)
inline constexpr uint16_t kCursorSaveBuf = 0x7A7E;  // g_cursorSaveBuf far: save-under buffer of the active cursor
                                                    // (Gfx_GetImage format) (§12.1)

// --- Palette and fades (1473, 1399) (code_runtime.md §13) ------------------------------------------------------------
inline constexpr uint16_t kPalShadow = 0x024E;      // g_palShadow u8[768]: shadow DAC palette, 6-bit RGB (§13.1)
inline constexpr size_t kPalShadowCount = 0x300;
inline constexpr uint16_t kPalSaved = 0x5C22;       // g_palSaved u8[768]: saved palette (Pal_FadeFromSaved) (§13.1)
inline constexpr size_t kPalSavedCount = 0x300;

// --- Memory and resources (code_runtime.md §16) ----------------------------------------------------------------------
inline constexpr uint16_t kResFileNames = 0x0550;   // g_resFileNames far[4]: names of gizmo000..003.dat for
                                                    // Res_OpenFiles (§16.2)
inline constexpr size_t kResFileNamesCount = 4;
inline constexpr uint16_t kResLastIndex = 0x0560;   // g_resLastIndex i16: index of the last resource read (Res_Read)
                                                    // (§16.2)
inline constexpr uint16_t kSwapHandle = 0x0562;     // g_swapHandle i16: gizmo004.dat handle (Screen_SaveToSwap; -1
                                                    // after the restore) (§16.3)
inline constexpr uint16_t kStrSwapFile = 0x0564;    // s_swapFile char[]: gizmo004.dat (Screen_SaveToSwap)
inline constexpr uint16_t kStrBadResFile0 = 0x05A5; // s_badResFile0 char[]: message for a broken volume 0 (§16.2)
inline constexpr uint16_t kStrBadResFile1 = 0x05BF; // s_badResFile1 char[]: message for a broken volume 1
inline constexpr uint16_t kStrBadResFile2 = 0x05D9; // s_badResFile2 char[]: message for a broken volume 2
inline constexpr uint16_t kSwapSize = 0x6562;       // g_swapSize u16: bytes written to gizmo004.dat (w*h + 4) (§16.3)
inline constexpr uint16_t kResFile = 0x6564;        // g_resFile i16: handle of the archive of the current Res_Read
                                                    // (§16.2)
inline constexpr uint16_t kResHandles = 0x6566;     // g_resHandles i16[4]: file handles of gizmo000..003.dat (§16.2)
inline constexpr size_t kResHandlesCount = 4;
inline constexpr uint16_t kResType = 0x6572;        // g_resType u8: type byte of the last resource read (§16.2)
inline constexpr uint16_t kPicCache = 0x6640;       // g_picCache rec[300]: picture cache (8 bytes: id, uses, far data)
                                                    // (§16.4)
inline constexpr size_t kPicCacheSize = 0x12C;

// --- Overview (code_sound.md §1) -------------------------------------------------------------------------------------
inline constexpr uint16_t kSndSlots = 0x1458;       // g_sndSlots far[64]: registered songs (BMF+0x10)
inline constexpr size_t kSndSlotsCount = 64;
inline constexpr uint16_t kSndActive = 0x1558;      // g_sndActive u8[64]: active song slots
inline constexpr size_t kSndActiveCount = 64;
inline constexpr uint16_t kSndPriority = 0x1598;    // g_sndPriority u8[64]: priority of each slot's song
inline constexpr size_t kSndPriorityCount = 64;

// --- Driver state in DGROUP (code_sound.md §2) -----------------------------------------------------------------------
inline constexpr uint16_t kSndChannelOwner = 0x15D8; // g_sndChannelOwner u16[4][16]: channel owner per class 1..4: low
                                                     // byte slot, high byte priority; 0x00ff free, 0x00fe released
inline constexpr size_t kSndChannelOwnerCount = 4;
inline constexpr size_t kSndChannelOwnerStride = 32;
inline constexpr uint16_t kSndClassChannels = 0x1658; // g_sndClassChannels u8[5]: channels per class: 1, 16, 4, 11, 16
inline constexpr size_t kSndClassChannelsCount = 5;
inline constexpr uint16_t kSndBusy = 0x165D;        // g_sndBusy u8: driver busy lock
inline constexpr uint16_t kPsgChannelNote = 0x166F; // g_psgChannelNote u8[4]: class 2 (dead): last key per channel
inline constexpr size_t kPsgChannelNoteCount = 4;
inline constexpr uint16_t kSndCurrentSlot = 0x167E; // g_sndCurrentSlot u16: slot being stepped by Snd_Tick
inline constexpr uint16_t kHasPsg = 0x1683;         // g_hasPsg u8: PSG present (never written: the PSG path is dead)
inline constexpr uint16_t kHasMpu = 0x1685;         // g_hasMpu u8: MPU-401 present (never written: the MT-32 path is
                                                    // dead)
inline constexpr uint16_t kOplMasterVolume = 0x1686; // g_oplMasterVolume u16: master volume for Opl_SetVolume
inline constexpr uint16_t kOplChannelVolume = 0x1688; // g_oplChannelVolume: channel volumes for Opl_SetVolume
inline constexpr uint16_t kPsgPort = 0x16AE;        // g_psgPort u16: PSG port (never written)
inline constexpr uint16_t kOplVoiceOperators = 0x16E5; // g_oplVoiceOperators: voice -> operator table
inline constexpr uint16_t kOplOperatorOffsets = 0x16FB; // g_oplOperatorOffsets: operator register offsets
inline constexpr uint16_t kOplPatchRegs = 0x170D;   // g_oplPatchRegs u8[5]: operator registers of a patch: 20 40 60 80
                                                    // e0
inline constexpr size_t kOplPatchRegsCount = 5;
inline constexpr uint16_t kNoteOctave = 0x1712;     // g_noteOctave u8[128]: n / 12 (block / octave)
inline constexpr size_t kNoteOctaveCount = 128;
inline constexpr uint16_t kNoteInOctave = 0x1792;   // g_noteInOctave u8[128]: n % 12 (reached as DS:1712 + 0x80 in
                                                    // Opl_NoteOn and Spk_PitchBend)
inline constexpr size_t kNoteInOctaveCount = 128;
inline constexpr uint16_t kPsgPeriods = 0x1836;     // g_psgPeriods u16[12]: base of the PSG period table (same layout
                                                    // as g_spkDivisors, entries DS:1832..1851; dead class 2)
inline constexpr size_t kPsgPeriodsCount = 12;
inline constexpr uint16_t kSndClassStep = 0x1AAA;   // g_sndClassStep near[5]: class interpreters: 087e, 0966, 0a1f,
                                                    // 0ad6, 0966
inline constexpr size_t kSndClassStepCount = 5;
inline constexpr uint16_t kSndEventSizes = 0x1AB4;  // g_sndEventSizes u8[4][8]: event size per class 1..4 and status
                                                    // nibble 8..F (dead tempo scaler)
inline constexpr size_t kSndEventSizesCount = 4;
inline constexpr size_t kSndEventSizesStride = 8;

// --- Initialisation (code_sound.md §3) -------------------------------------------------------------------------------
inline constexpr uint16_t kHasOpl = 0x1684;         // g_hasOpl u8: OPL2 present (AdLib_Detect, or forced by the -s
                                                    // switch) (§3.1)

// --- Class 1: PC speaker (Snd_StepSpeaker 1bb4:0966) (code_sound.md §7) ----------------------------------------------
inline constexpr uint16_t kSpkChannelNote = 0x165F; // g_spkChannelNote u8[16]: class 1: last key per channel (>= 0x80 =
                                                    // none)
inline constexpr size_t kSpkChannelNoteCount = 16;
inline constexpr uint16_t kSpkDivisors = 0x1816;    // g_spkDivisors u16[12]: base of the speaker divisor table for
                                                    // semitones 0..11 of octave 0 (C1 = 0x8e88); Spk_PitchBend also
                                                    // reads the two entries before (DS:1812, 1814) and after (DS:182e,
                                                    // 1830) for bends; divisor = T[n % 12] >> (n / 12)
inline constexpr size_t kSpkDivisorsCount = 12;

// --- Class 3: OPL2 (Snd_StepOpl 1bb4:0ad6) (code_sound.md §8) --------------------------------------------------------
inline constexpr uint16_t kOplVoiceKey = 0x1673;    // g_oplVoiceKey u8[11]: class 3: last key per voice (>= 0x80 =
                                                    // none)
inline constexpr size_t kOplVoiceKeyCount = 11;
inline constexpr uint16_t kSndEnabled = 0x1680;     // g_sndEnabled u8: 0 = output disabled (Snd_Silence) (§8.1)
inline constexpr uint16_t kOplB0Shadow = 0x16DE;    // g_oplB0Shadow u8[6]: last B0 value of melodic channels 0..5
                                                    // (§8.3)
inline constexpr size_t kOplB0ShadowCount = 6;
inline constexpr uint16_t kOplBdShadow = 0x16E4;    // g_oplBdShadow u8: OPL register BD (0x20 after init) (§8.3)
inline constexpr uint16_t kOplFNumbers = 0x1852;    // g_oplFNumbers u16[25][12]: OPL F-numbers, row r detuned by r/25
                                                    // semitone (identical to the AdLib driver's InitFNums table) (§8.3)
inline constexpr size_t kOplFNumbersCount = 25;
inline constexpr size_t kOplFNumbersStride = 24;

// --- Dead classes (code_sound.md §9) ---------------------------------------------------------------------------------
inline constexpr uint16_t kSndScaleRemainder = 0x16B0; // g_sndScaleRemainder u16: remainder of the dead tempo scaler

// --- Pause, the sound switch, silence (code_sound.md §12) ------------------------------------------------------------
inline constexpr uint16_t kSoundMuted = 0x1AD4;     // g_soundMuted i16: sound switch: 1 = the silence song holds every
                                                    // channel (Snd_ToggleOption, SndRes_ApplySoundOption) (§12.2)

// --- The bank layer (1cdd) (code_sound.md §13) -----------------------------------------------------------------------
inline constexpr uint16_t kSndLastEffectSlot = 0x1AD6; // g_sndLastEffectSlot i16: slot of the last effect started (-1
                                                       // after a stop); no reader (§13.1)
inline constexpr uint16_t kSndLastMusicSlot = 0x1AD8; // g_sndLastMusicSlot i16: slot of the last music started (-1
                                                      // after a stop); no reader (§13.1)
inline constexpr uint16_t kSndBankMem = 0x9C58;     // g_sndBankMem far[60]: loaded bank copies per sound handle (§13.1)
inline constexpr size_t kSndBankMemCount = 60;
inline constexpr uint16_t kSndTable = 0x9D48;       // g_sndTable rec[60]: sound bank table (SndRes_Init) (§13.1)
inline constexpr size_t kSndTableSize = 60;

// --- Music cue callbacks (12e0) (code_sound.md §14) ------------------------------------------------------------------
inline constexpr uint16_t kRoomMusicTransposes = 0x010E; // g_roomMusicTransposes u8[36]: room music transposes: 3
                                                         // buildings x 6 steps x 2 bytes (low byte used); Music_RoomCue
                                                         // reads with stride 12, 1622:0346 with stride 6 (bug) (§14.1)
inline constexpr size_t kRoomMusicTransposesCount = 36;
inline constexpr uint16_t kRoomMusicStep = 0x08BE;  // g_roomMusicStep i16: room music transpose step 0..5
                                                    // (Music_RoomCue) (§14.1)
inline constexpr uint16_t kRoomMusicBank = 0x09E6;  // g_roomMusicBank i16: sound handle of the building's room music
                                                    // (0x35f + 2*building) (§14.1)
inline constexpr uint16_t kRoomMusicCueTicks = 0x0A06; // g_roomMusicCueTicks u16[3]: room music cue tick per building
                                                       // (1260, 3555, 2119) (§14.1)
inline constexpr size_t kRoomMusicCueTicksCount = 3;
inline constexpr uint16_t kMovieMusicBank = 0x4552; // g_movieMusicBank i16: sound handle of the final movie music
                                                    // (0x30ca) (§14.3)
inline constexpr uint16_t kMovieMusicStep = 0x4554; // g_movieMusicStep i16: Music_MovieCue counter (arrangement 1..4,
                                                    // all, reset at 7) (§14.3)
inline constexpr uint16_t kWorkshopArrangement = 0x4558; // g_workshopArrangement i16: workshop music arrangement (1, or
                                                         // 2 from the cue) (§14.2)
inline constexpr uint16_t kMusicBank = 0x9DC4;      // g_musicBank i16: sound handle of the current screen's music
                                                    // (attract, workshop) (§14.2)

// --- Who plays what (code_sound.md §15) ------------------------------------------------------------------------------
inline constexpr uint16_t kTitleMusicBank = 0x0098; // g_titleMusicBank i16: sound handle of the title music 0x1020
                                                    // loaded by the sign-in and the hub (-1 = not loaded; freed by
                                                    // State_04_BuildingSetup)
inline constexpr uint16_t kScore = 0x4572;          // g_score u32: points
inline constexpr uint16_t kEffectBank = 0x7A44;     // g_effectBank i16: sound handle of the current screen's effect
                                                    // bank (effect k = handle + k): the warehouse, the workshop (0x29)
                                                    // and the results (0x108f) load it; used by the animation opcodes
                                                    // -36/-37/-45 and 1cdd:0521
inline constexpr uint16_t kPzSndSlots = 0xB146;     // g_pzSndSlots i16[53]: song slot per effect of the puzzle bank
inline constexpr size_t kPzSndSlotsCount = 53;
inline constexpr uint16_t kRaceEffects = 0xBAAA;    // g_raceEffects i16: land race effect bank handle (0xf4)
inline constexpr uint16_t kRaceMusic = 0xBAAC;      // g_raceMusic i16: land race music bank handle
inline constexpr uint16_t kAirEffects = 0xBBB0;     // g_airEffects i16: aircraft race effect bank handle (0x260)
inline constexpr uint16_t kAirMusic = 0xBBB2;       // g_airMusic i16: aircraft race music bank handle (0x262)

// --- Runtime context (code_ui.md §2) ---------------------------------------------------------------------------------
inline constexpr uint16_t kResBuffer = 0x6574;      // g_resBuffer far: shared 64 KB load and decompression buffer
                                                    // (§2.3)
inline constexpr uint16_t kSavedTicks = 0x6598;     // g_savedTicks u32: tick counter saved by Dlg_RunModal (§2.2)
inline constexpr uint16_t kMenuHandler = 0x865A;    // g_menuHandler far: menu callback (Menu_RunDropDown) (§2.1)
inline constexpr uint16_t kDlgSaveBuf = 0xBBB4;     // g_dlgSaveBuf far: save-under of the dialog box: always
                                                    // g_resBuffer (Dlg_SaveUnder) (§2.3)
inline constexpr uint16_t kDlgPicY = 0xBBB8;        // g_dlgPicY i16: picture origin y of a dialog (body y - 2)

// --- Fonts (ovl01 2b16) (code_ui.md §3) ------------------------------------------------------------------------------
inline constexpr uint16_t kFontIndex = 0x1F66;      // g_fontIndex i16: index of the selected font (0 = resource 9, the
                                                    // UI font; 1 = resource 10, the small font); Font_Select (§3.1)
inline constexpr uint16_t kFont = 0x1F68;           // g_font far: the selected font record (0x24 bytes, Font_Load);
                                                    // Font_GetCurrent returns it (§3.1)
inline constexpr uint16_t kGlyphColor = 0x1F6C;     // g_glyphColor u8: colour of Gfx_DrawGlyph (§3.1)
inline constexpr uint16_t kStrHintFooter = 0x2472;  // s_hintFooter char[]: footer of the hint box (an Enter key-cap and
                                                    // a short line) (§3.1)
inline constexpr uint16_t kFonts = 0xA040;          // g_fonts far[2]: font records of resources 9 and 10 (Font_Init)
                                                    // (§3.1)
inline constexpr size_t kFontsCount = 2;
inline constexpr uint16_t kFontDrawGlyph = 0xA048;  // g_fontDrawGlyph far: glyph routine of the selected font:
                                                    // Gfx_DrawGlyph (font flag bit 0) or Lpx_DrawKeyed (§3.1)

// --- Dialog boxes and buttons (ovl27 3f56, ovl28 4034, ovl32 4106) (code_ui.md §5) -----------------------------------
inline constexpr uint16_t kDlgFooterZone = 0x3DD4;  // g_dlgFooterZone zone: footer line of a dialog: x/y/w set by
                                                    // Dlg_AddZones, h 10, priority 5, press = release = 0x1b (a click
                                                    // on the footer is Esc) (§5.5)
inline constexpr uint16_t kYesNoButtons = 0x3DEA;   // g_yesNoButtons far[11]: dialog button label table {Yes, No,
                                                    // Cancel, Done, OK, Done, See Next Page, Go Back a Page, See Next
                                                    // Page, Go Back a Page, Done}; Yes/No = entries 0..1, the pager
                                                    // uses DS:3dfe/3e02/3e06/3e0e (§5.7)
inline constexpr size_t kYesNoButtonsCount = 11;
inline constexpr uint16_t kDlgKeys = 0x3E16;        // g_dlgKeys u16[5]: button keys 'y' 'n' 'c' 'd' 'o' (Yes/No and the
                                                    // chimps box use the first two) (§5.7)
inline constexpr size_t kDlgKeysCount = 5;
inline constexpr uint16_t kPagerKeys = 0x3E20;      // g_pagerKeys u16[6]: pager keys: DS:3e20 {PgDn} single page,
                                                    // DS:3e22 {PgDn} first page, DS:3e24 {PgUp, PgDn} middle, DS:3e28
                                                    // {PgUp, PgDn} last (0x149 PgUp, 0x151 PgDn)
inline constexpr size_t kPagerKeysCount = 6;
inline constexpr uint16_t kChimpButtons = 0x3E2C;   // g_chimpButtons: With Chimps / Without Chimps (§5.7)
inline constexpr uint16_t kStrFooterGoBack = 0x3E38; // s_footerGoBack char[]: dialog footer with the Esc key-cap
                                                     // (Yes/No boxes, drop-downs, message bundles with flag 0) (§5.7)
inline constexpr uint16_t kStrFooterGoOn = 0x3E45;  // s_footerGoOn char[]: dialog footer with the Enter key-cap
                                                    // (message bundles with flag 1) (§5.7)
inline constexpr uint16_t kDlgPicX = 0xBBBA;        // g_dlgPicX i16: picture origin x of a dialog (box x + 4) (§5.2)
inline constexpr uint16_t kDlgButtons = 0xBBBC;     // g_dlgButtons rec[3][28]: dialog buttons: a 22-byte zone {0, x, y,
                                                    // w, h, 5, next, key, key, -} + far label (+0x16) + enabled (+0x1a)
                                                    // (§5.1)
inline constexpr size_t kDlgButtonsCount = 3;
inline constexpr size_t kDlgButtonsStride = 28;
inline constexpr uint16_t kDlgLineH = 0xBC10;       // g_dlgLineH i16: line height of the dialog font (Font_GetHeight,
                                                    // 10) (§5.1)
inline constexpr uint16_t kDlgFooterY = 0xBC12;     // g_dlgFooterY i16: footer text y
inline constexpr uint16_t kDlgFooterX = 0xBC14;     // g_dlgFooterX i16: footer text x (§5.2)
inline constexpr uint16_t kDlgBodyW = 0xBC16;       // g_dlgBodyW i16: body wrap width (measured, or the descriptor's)
inline constexpr uint16_t kDlgTitleY = 0xBC18;      // g_dlgTitleY i16: title text y
inline constexpr uint16_t kDlgTitleX = 0xBC1A;      // g_dlgTitleX i16: title text x (§5.2)
inline constexpr uint16_t kDlgBodyY = 0xBC1C;       // g_dlgBodyY i16: body text y
inline constexpr uint16_t kDlgBodyX = 0xBC1E;       // g_dlgBodyX i16: body text x (§5.2)
inline constexpr uint16_t kDlgH = 0xBC20;           // g_dlgH i16: box height (§5.2)
inline constexpr uint16_t kDlgW = 0xBC22;           // g_dlgW i16: box width (§5.2)
inline constexpr uint16_t kDlgY = 0xBC24;           // g_dlgY i16: box y (§5.2)
inline constexpr uint16_t kDlgX = 0xBC26;           // g_dlgX i16: box x (§5.2)

// --- Menu bar and commands (ovl31 40a4, ovl32 4106, ovl33 411d) (code_ui.md §6) --------------------------------------
inline constexpr uint16_t kMenuRequest = 0x130E;    // g_menuRequest i16: 1/2 -> Input_GetCode returns 0x17a2 (return to
                                                    // the Technology Center) / 0x17a3 (start new player) (§6.5)
inline constexpr uint16_t kMenuNesting = 0x3DD2;    // g_menuNesting i16: Menu_RunDropDown nesting count: Zones_Push
                                                    // only at 0 -> 1, Zones_Pop when it drops back to 0 (§6.4)
inline constexpr uint16_t kStrUnusedFileItems = 0x3F2B; // s_unusedFileItems char[]: File item list of another title,
                                                        // never referenced (§6.1)
inline constexpr uint16_t kHelpHandlers = 0x3FCF;   // g_helpHandlers far[6]: Help menu item handlers (411d stub
                                                    // entries)
inline constexpr size_t kHelpHandlersCount = 6;
inline constexpr uint16_t kMenuHelp = 0x4017;       // g_menuHelp MenuDef: 0x14 bytes: far title, +04 title width, +06
                                                    // count, far items, far handlers, +10 drop-down width, +12 title x
                                                    // (§6.1)
inline constexpr uint16_t kMenuFile = 0x402B;       // g_menuFile MenuDef: File menu (§6.1)
inline constexpr uint16_t kMenuOptions = 0x403F;    // g_menuOptions MenuDef: Options menu (§6.1)
inline constexpr uint16_t kMenuBar = 0x4053;        // g_menuBar MenuBarDef: {u16 3, far DS:4017} (§6.1)
inline constexpr uint16_t kMenuBarNone = 0x405A;    // g_menuBarNone rec[8]: {u16 0, far NULL}: the bar pointer after
                                                    // Menu_SetMenuBar(NULL) (§6.1)
inline constexpr size_t kMenuBarNoneSize = 8;
inline constexpr uint16_t kHintsOff = 0x45A2;       // g_hintsOff i16: New Player Hints switched off (§6.5)
inline constexpr uint16_t kDlgInnerW = 0xBC28;      // g_dlgInnerW i16: content width + 12 (before the button row and
                                                    // the outer 10): menu item zones and highlight width (§6.4)
inline constexpr uint16_t kMenuItemZones = 0xBC2A;  // g_menuItemZones far: Mem_Alloc'd zones of the open drop-down
                                                    // (outside zone, items, footer) (§6.4)
inline constexpr uint16_t kMenuTitleZones = 0xBC2E; // g_menuTitleZones zone[3]: global zones of the menu titles
                                                    // (release 0x13b + i) (§6.2)
inline constexpr size_t kMenuTitleZonesCount = 3;
inline constexpr uint16_t kMenuBarCur = 0xBC70;     // g_menuBarCur far: the installed MenuBarDef (DS:4053, or DS:405a
                                                    // after removal) (§6.2)

// --- Hot keys and options (summary) (code_ui.md §7) ------------------------------------------------------------------
inline constexpr uint16_t kSoundOff = 0x45CC;       // g_soundOff i16: sound switched off

// --- Scrolling text viewer (ovl34 4140) (code_ui.md §8) --------------------------------------------------------------
inline constexpr uint16_t kViewUpZone = 0x4060;     // g_viewUpZone zone: text viewer up arrow (117,29) 11x16, priority
                                                    // 503, press 0x51a, release 0x51b (§8.1)
inline constexpr uint16_t kViewDownZone = 0x4076;   // g_viewDownZone zone: text viewer down arrow (117,149) 11x16,
                                                    // priority 503, press 0x51c, release 0x51d (§8.1)
inline constexpr uint16_t kViewTextZone = 0x408C;   // g_viewTextZone zone: text viewer text area (0,28) 129x138,
                                                    // priority 502, press 0x51e (ignored) (§8.1)
inline constexpr uint16_t kViewCloseZone = 0x40A2;  // g_viewCloseZone zone: whole screen, priority 501, press 0x51f: a
                                                    // click outside the viewer closes it (§8.1)
inline constexpr uint16_t kViewPageUpZone = 0x40B8; // g_viewPageUpZone zone: text viewer track above the thumb (118,45)
                                                    // 9x(thumb y - 45), priority 503, press 0x520 (§8.1)
inline constexpr uint16_t kViewThumbZone = 0x40CE;  // g_viewThumbZone zone: text viewer thumb (118, thumb y) 9x11,
                                                    // priority 503, press 0x521 (§8.1)
inline constexpr uint16_t kViewPageDownZone = 0x40E4; // g_viewPageDownZone zone: text viewer track below the thumb
                                                      // (118, thumb y + 11) 9x(138 - thumb y), priority 503, press
                                                      // 0x522 (§8.1)
inline constexpr uint16_t kAtexitCount = 0x4100;    // g_atexitCount i16: atexit table entries
inline constexpr uint16_t kViewThumbStep = 0xBC74;  // g_viewThumbStep i16: text viewer: thumb pixels per line = 104 /
                                                    // g_viewScrollMax (§8.1)
inline constexpr uint16_t kViewThumbY = 0xBC76;     // g_viewThumbY i16: text viewer thumb y (45..138) (§8.1)
inline constexpr uint16_t kViewScrollMax = 0xBC78;  // g_viewScrollMax i16: text viewer scroll positions = max(1, line
                                                    // breaks - 10) (§8.1)

// --- Scroll bar (ovl30 406a) (code_ui.md §9) -------------------------------------------------------------------------
inline constexpr uint16_t kRosterScrollBar = 0x20DC; // g_rosterScrollBar rec[20]: the scroll bar record of the sign-in
                                                     // list {x 244, y 26, w 11, h 109, pos (top row), max, page 8, u32
                                                     // next repeat tick, thumb y} (406a)
inline constexpr size_t kRosterScrollBarSize = 20;

// --- Attract sequence (state 1, ovl06 2c11) (code_ui.md §10) ---------------------------------------------------------
inline constexpr uint16_t kAttractSequence = 0x20A8; // g_attractSequence far[]: attract sub-sequence run by Seq_Run
                                                     // (State_01_Attract) (§10.1)
inline constexpr uint16_t kLeaveRequest = 0x455A;   // g_leaveRequest i16: -1 quit, 2 back to sign-in, 3 back to the hub
                                                    // (§10.1)
inline constexpr uint16_t kStoryTextId = 0xA04E;    // g_storyTextId i16: text of the next story page (0x1019..0x101e;
                                                    // 0x1019 = first page) (§10.3)

// --- Sign-in and roster (state 2, ovl07 2caa) (code_ui.md §11) -------------------------------------------------------
inline constexpr uint16_t kSaveDisabled = 0x1F7A;   // g_saveDisabled i16: 1 while the sign-in's Esc runs
                                                    // Cmd_SaveAndExit: Players_SaveCurrent then writes nothing (no
                                                    // player is loaded) (§11.1)
inline constexpr uint16_t kRankNames = 0x1F7C;      // g_rankNames far[8]: Trainee .. Head Scientist (§11.2)
inline constexpr size_t kRankNamesCount = 8;
inline constexpr uint16_t kBtnCancel = 0x20F0;      // g_btnCancel rec[0x20]: sign-in picture button Cancel: zone
                                                    // (250,172) 49x20 press 0x706 release 0x702, +0x16 bundle 0x102d,
                                                    // +0x18 enabled, +0x1a/1c/1e picture cache indices (normal,
                                                    // pressed, disabled) (§11.1)
inline constexpr size_t kBtnCancelSize = 32;
inline constexpr uint16_t kBtnNewPlayer = 0x2110;   // g_btnNewPlayer rec[0x20]: sign-in picture button New Player: zone
                                                    // (197,170) 42x24 press 0x707 release 0x703, bundle 0x102c (§11.1)
inline constexpr size_t kBtnNewPlayerSize = 32;
inline constexpr uint16_t kBtnStartGame = 0x2130;   // g_btnStartGame rec[0x20]: sign-in picture button Start Game: zone
                                                    // (146,170) 42x24 press 0x708 release 0x704, bundle 0x102b (§11.1)
inline constexpr size_t kBtnStartGameSize = 32;
inline constexpr uint16_t kRosterZone = 0x2150;     // g_rosterZone zone: sign-in name list (62,46) 180x88, priority 4,
                                                    // release 0x700 (§11.1)
inline constexpr uint16_t kRosterScrollZone = 0x2166; // g_rosterScrollZone zone: sign-in scroll bar (244,26) 11x109,
                                                      // priority 5, press = release = 0x701 (§11.1)
inline constexpr uint16_t kTypedName = 0xA054;      // g_typedName char[16]: sign-in: the name being typed (0x1f =
                                                    // placeholder of an empty new entry) (§11.1)
inline constexpr size_t kTypedNameSize = 16;
inline constexpr uint16_t kSigninArrowRepeat = 0xA065; // g_signinArrowRepeat i16: written only (0, 2 Up, 3 Down):
                                                       // vestige of a key-repeat throttle that never throttles (§11.5)
inline constexpr uint16_t kSigninArrowCount = 0xA067; // g_signinArrowCount i16: never incremented: always 0 (see
                                                      // g_signinArrowRepeat)
inline constexpr uint16_t kSigninPrevClick = 0xA069; // g_signinPrevClick u32: written only: copy of g_signinLastClick
                                                     // while no input is pending (vestige of a double click)
inline constexpr uint16_t kSigninLastClick = 0xA06D; // g_signinLastClick u32: written only: tick of the last click on a
                                                     // name
inline constexpr uint16_t kTypedLen = 0xA071;       // g_typedLen i16: characters in g_typedName (-1 = sign-in just
                                                    // entered) (§11.1)
inline constexpr uint16_t kSigninClicked = 0xA073;  // g_signinClicked i16: written only: 1 after a click on a name
                                                    // (§11.3)
inline constexpr uint16_t kRosterIndex = 0xA079;    // g_rosterIndex i16: roster entry of the current player (written at
                                                    // 8 + index*22) (§11.1)

// --- PLAYERS.DAT (ovl02 2b5a, ovl07 2caa) (code_ui.md §12) -----------------------------------------------------------
inline constexpr uint16_t kStrPlayersDat = 0x1F6E;  // s_playersDat char[]: players.dat (§12.1)
inline constexpr uint16_t kPlayer = 0x4562;         // g_player rec[0x16bd]: current PLAYERS.DAT record (§12.1)
inline constexpr size_t kPlayerSize = 0x16BD;
inline constexpr uint16_t kPlayerSlot = 0x4576;     // g_playerSlot i16: record slot in players.dat (§12.3)
inline constexpr uint16_t kBuildingInProgress = 0x459C; // g_buildingInProgress i16[3]: building has saved live state
                                                        // (§12.5)
inline constexpr size_t kBuildingInProgressCount = 3;
inline constexpr uint16_t kBuildingRecords = 0x47E0; // g_buildingRecords rec[3][0x6ba]: per-building saved state in the
                                                     // player record (§12.5)
inline constexpr size_t kBuildingRecordsCount = 3;
inline constexpr size_t kBuildingRecordsStride = 0x6BA;
inline constexpr uint16_t kPlayersFile = 0xA04C;    // g_playersFile i16: players.dat file handle (§12.1)
inline constexpr uint16_t kRoster = 0xA075;         // g_roster far: roster buffer (players.dat header + 99 x 22-byte
                                                    // entries in name order) (§12.1)

// --- New Player Hints (ovl12 3060) (code_ui.md §13) ------------------------------------------------------------------
inline constexpr uint16_t kHintBox = 0x2456;        // g_hintBox rec[12]: message of Hint_Show: far title (DS:2462), far
                                                    // body (the hint text copy, set per call), far footer (DS:2472)
                                                    // (§13.1)
inline constexpr size_t kHintBoxSize = 12;
inline constexpr uint16_t kStrNewPlayerHint = 0x2462; // s_newPlayerHint char[]: title of the hint box (§13.1)
inline constexpr uint16_t kHintSeen = 0x45A4;       // g_hintSeen u16[19]: one word per hint 0x1043..0x1055 (19, record
                                                    // +0x42..+0x67); set by Hint_Show also when hints are off (§13.1)
inline constexpr size_t kHintSeenCount = 19;
inline constexpr uint16_t kHintList = 0xA0DA;       // g_hintList far: hint list (Hint_Init) (§13.1)
inline constexpr uint16_t kHintSlots = 0xA0DE;      // g_hintSlots rec[5][18]: hint queue records {list node, +8 id (-1
                                                    // free), +0xa u32 due tick, +0xe far cancel flag} (§13.1)
inline constexpr size_t kHintSlotsCount = 5;
inline constexpr size_t kHintSlotsStride = 18;
inline constexpr uint16_t kHintSlotId = 0xA0E6;     // g_hintSlotId i16: the id field of hint slot 0 (records at
                                                    // DS:a0de, 18 bytes each) (§13.1)
inline constexpr uint16_t kElecPaths = 0xA138;      // g_elecPaths i16[10][20]: closed paths found by the path search
                                                    // (rows end with the start piece) (§13.1)
inline constexpr size_t kElecPathsCount = 10;
inline constexpr size_t kElecPathsStride = 40;

// --- Building set-up and resume (state 4, 12e0:0820) (code_world.md §4) ----------------------------------------------
inline constexpr uint16_t kInBuilding = 0x0094;     // g_inBuilding i16: a building is in progress (Players_SaveCurrent
                                                    // saves its live state) (§4.1)
inline constexpr uint16_t kTubeRoomByLevel = 0x00B2; // g_tubeRoomByLevel u16[5]: hall the workshop tube leads to at a
                                                     // new visit, per level (-> DS:4580) (§4.1)
inline constexpr size_t kTubeRoomByLevelCount = 5;
inline constexpr uint16_t kPlayerSets = 0x0A9E;     // g_playerSets far[4]: cached actor sets 0x1034, 0x1033, 0x1036,
                                                    // 0x1035 (§4.5)
inline constexpr size_t kPlayerSetsCount = 4;
inline constexpr uint16_t kBlueprintSeen = 0x457C;  // g_blueprintSeen i16: first word of the 20-byte live building
                                                    // block DS:457c..458f (saved with the building); 1 once the
                                                    // blueprint was shown this level (§4.1)
inline constexpr uint16_t kProjectiles = 0x457E;    // g_projectiles i16: projectile counter (10 per level) (§4.1)
inline constexpr uint16_t kSetupCoin = 0x458E;      // g_setupCoin i16: R(100) < 50 at a new visit; written only (§4.1)
inline constexpr uint16_t kBuilding = 0x4592;       // g_building i16: current building (0 Automotive, 1 Aircraft, 2
                                                    // Alternative Energy) (§4.1)
inline constexpr uint16_t kLevel = 0x4594;          // g_level i16: current level in the building (§4.1)
inline constexpr uint16_t kCratesTotal = 0x7A02;    // g_cratesTotal i16: crates of the level (item total - part count)
                                                    // (§4.2)
inline constexpr uint16_t kActors = 0x7A8A;         // g_actors rec[12][0xa0]: 0 player, 1 projectile, 2..11 Cyber
                                                    // Chimps (§4.5)
inline constexpr size_t kActorsCount = 12;
inline constexpr size_t kActorsStride = 160;
inline constexpr uint16_t kChimpSetRoom = 0x9F1E;   // g_chimpSetRoom far: cached room-scale chimp set (§4.5)
inline constexpr uint16_t kChimpSetHall = 0x9F22;   // g_chimpSetHall far: cached hall-scale chimp set (§4.5)

// --- Room data model (code_world.md §5) ------------------------------------------------------------------------------
inline constexpr uint16_t kRoomCount = 0x08C4;      // g_roomCount i16: rooms in the building's room set (§5.7)
inline constexpr uint16_t kRoomSetIds = 0x09E8;     // g_roomSetIds u16[15]: room set resource ('A' 0x347..0x34f) per
                                                    // race index (§5.1)
inline constexpr size_t kRoomSetIdsCount = 15;

// --- Loading, entering and leaving rooms (code_world.md §6) ----------------------------------------------------------
inline constexpr uint16_t kRoomLoaded = 0x08C0;     // g_roomLoaded i16: the displayed room's pictures are loaded
                                                    // (Room_Leave releases them) (§6.2)
inline constexpr uint16_t kDisplayRoom = 0x08C2;    // g_displayRoom i16: room shown on the screen (Room_Enter) (§6.1)
inline constexpr uint16_t kRoom = 0x08C6;           // g_room rec[0x120]: current room header (copied from the room
                                                    // table) (§6.2)
inline constexpr size_t kRoomSize = 0x120;
inline constexpr uint16_t kRoomSet = 0x79F8;        // g_roomSet far: private copy of the room set resource (§6.1)
inline constexpr uint16_t kRoomItemCounts = 0x79FC; // g_roomItemCounts far: copy of 'I' 0x350 + race: word 0 all items,
                                                    // word r items of room r (§6.1)
inline constexpr uint16_t kRoomTable = 0x7A04;      // g_roomTable far[16]: rooms of the building (§6.1)
inline constexpr size_t kRoomTableCount = 16;

// --- Items lying in rooms (parts and banana crates) (code_world.md §7) -----------------------------------------------
inline constexpr uint16_t kPartDealIndex = 0x6614;  // g_partDealIndex i16: next deck entry (Parts_DealNext) (§7.2)
inline constexpr uint16_t kItemPool = 0x6FA8;       // g_itemPool rec[60][0x2c]: item nodes of all rooms (+8 = 1 free)
                                                    // (§7.1)
inline constexpr size_t kItemPoolCount = 60;
inline constexpr size_t kItemPoolStride = 44;
inline constexpr uint16_t kCratesLeft = 0x7A00;     // g_cratesLeft i16: banana crates in the rooms (§7.5)

// --- The actor system (1834) (code_world.md §9) ----------------------------------------------------------------------
inline constexpr uint16_t kChimpStunLoops = 0x0A90; // g_chimpStunLoops u16[5]: per level: loop count of the stunned
                                                    // chimp (step op -31) (§9.3)
inline constexpr size_t kChimpStunLoopsCount = 5;
inline constexpr uint16_t kPlayerBlink = 0x0A9A;    // g_playerBlink i16: 0..3 blink phase while the player is immune
                                                    // (drawn when < 2) (§9.5)
inline constexpr uint16_t kActorSpritesLoaded = 0x0A9C; // g_actorSpritesLoaded i16: Actors_EnterRoom loaded the sprite
                                                        // bundles (§9.6)
inline constexpr uint16_t kProjectilePicSlots = 0x820A; // g_projectilePicSlots u16[5]: picture-cache slots of the
                                                        // projectile's bundle (§9.1)
inline constexpr size_t kProjectilePicSlotsCount = 5;
inline constexpr uint16_t kPlayerPicSlots = 0x8214; // g_playerPicSlots u16[]: picture-cache slots of the player's
                                                    // sprite bundle (§9.1)
inline constexpr uint16_t kProjectileSeqHall = 0x8304; // g_projectileSeqHall far[]: sequence table of set 0x1036 (§9.1)
inline constexpr uint16_t kProjectileSeqRoom = 0x8318; // g_projectileSeqRoom far[]: sequence table of set 0x1035 (§9.1)
inline constexpr uint16_t kPlayerSeqHall = 0x832C;  // g_playerSeqHall far[]: sequence table of set 0x1034 (68
                                                    // sequences) (§9.1)
inline constexpr uint16_t kPlayerSeqRoom = 0x8440;  // g_playerSeqRoom far[]: sequence table of set 0x1033 (53
                                                    // sequences) (§9.1)
inline constexpr uint16_t kChimpSeqRoom = 0x9DC6;   // g_chimpSeqRoom far[]: sequence table of the chimps' room set
                                                    // (0x1041 / 0x103d) (§9.1)
inline constexpr uint16_t kChimpSeqHall = 0x9E72;   // g_chimpSeqHall far[]: sequence table of the chimps' hall set
                                                    // (0x1042 / 0x103e) (§9.1)
inline constexpr uint16_t kChimpPicSlots = 0x9F26;  // g_chimpPicSlots u16[]: picture-cache slots shared by all chimps
                                                    // (§9.1)

// --- Collision and contacts (1adb) (code_world.md §10) ---------------------------------------------------------------
inline constexpr uint16_t kContactsPlayer = 0x13A0; // g_contactsPlayer i16[]: actors the player touches (chimps 2..11),
                                                    // -1 terminated (§10.3)
inline constexpr uint16_t kContactsProjectile = 0x13B6; // g_contactsProjectile i16[]: actors the projectile touches
                                                        // (chimps 2..11) (§10.3)
inline constexpr uint16_t kContactsChimp = 0x13CC;  // g_contactsChimp i16[]: actors a chimp touches (the projectile
                                                    // only) (§10.3)

// --- The player (18f5) (code_world.md §11) ---------------------------------------------------------------------------
inline constexpr uint16_t kPlayerTransitions = 0x127E; // g_playerTransitions near[68]: per player sequence: 12-byte
                                                       // transition list {u32 match, u32 mask, next, unused}, end
                                                       // {-1,-1} (§11.2)
inline constexpr size_t kPlayerTransitionsCount = 68;
inline constexpr uint16_t kLastKeyHeldTick = 0x1314; // g_lastKeyHeldTick u32: tick of the last held key
                                                     // (Input_GetHeldKeys), reset at room / workshop start (§11.1)
inline constexpr uint16_t kTubeRoom = 0x4580;       // g_tubeRoom i16: hall the workshop tube leads to (DS:00b2[level]
                                                    // at a new visit; the hall the player left through the tube)
                                                    // (§11.4)
inline constexpr uint16_t kPartsNeeded = 0x4582;    // g_partsNeeded i16: parts still needed (HUD) (§11.6)
inline constexpr uint16_t kHintCancel1048 = 0x458C; // g_hintCancel1048 i16: cancels hint 0x1048 (§11.4)
inline constexpr uint16_t kIdleReported = 0x8554;   // g_idleReported i16: 1 after Input_GetHeldKeys reported the idle
                                                    // bit 0x40 (§11.1)

// --- Cyber Chimps (ovl00 2a80 and DGROUP tables) (code_world.md §13) -------------------------------------------------
inline constexpr uint16_t kChimpLists = 0x1BAE;     // g_chimpLists far[15]: far pointers, per race index, to {u16
                                                    // count, u16 room per chimp} (DS:1adc..); the 286 path halves the
                                                    // first room word (§13.1)
inline constexpr size_t kChimpListsCount = 15;
inline constexpr uint16_t kChimpMoves = 0x1D2E;     // g_chimpMoves: Cyber Chimp move lists with random chances (§13.3)
inline constexpr uint16_t kChimpTransitions = 0x1F10; // g_chimpTransitions: Cyber Chimp state transitions (§13.3)
inline constexpr uint16_t kWithChimps = 0x4578;     // g_withChimps i16: With/Without Chimps (§13.1)
inline constexpr uint16_t kRoomChimpCount = 0xA01C; // g_roomChimpCount i16[16]: chimps counted per room (music layer);
                                                    // re-incremented at every display (bug) (§13.1)
inline constexpr size_t kRoomChimpCountCount = 16;
inline constexpr uint16_t kChimpParts = 0xA03C;     // g_chimpParts far: carried-part list shared by all chimps (holder
                                                    // in part +0x0c) (§13.2)

// --- States 5, 8, 10, 11, 16 (code_world.md §15) ---------------------------------------------------------------------
inline constexpr uint16_t kVehicleDone = 0x00BE;    // g_vehicleDone i16: the vehicle is finished -> race
                                                    // (State_08_LeaveRoom) (§15.2)
inline constexpr uint16_t kRaceResult = 0x4560;     // g_raceResult i16: 0 = the player won (§15.3)
inline constexpr uint16_t kPlayerVehicle = 0x6616;  // g_playerVehicle i16[]: part list of the player's vehicle (-1
                                                    // terminated) (§15.3)

// --- The HUD strip (1a04) (code_world.md §17) ------------------------------------------------------------------------
inline constexpr uint16_t kHudDirty = 0x132C;       // g_hudDirty u16: HUD redraw bits: 1 strip, 2 parts needed, 4 score
                                                    // (and global effect 1), 8 bananas (and global effect 5), 0x10 name
                                                    // box, 0x40 flash, 0x4000 bar and score label, 0x8000 full redraw
                                                    // (Hud_Update)

// --- Porting notes (code_world.md §22) -------------------------------------------------------------------------------
inline constexpr uint16_t kHudStripZones = 0x1330;  // g_hudStripZones rec[2][22]: strip arrow zones, release 6002 /
                                                    // 6003, tag 4
inline constexpr size_t kHudStripZonesCount = 2;
inline constexpr size_t kHudStripZonesStride = 22;
inline constexpr uint16_t kHudCarryZones = 0x135C;  // g_hudCarryZones rec[2][22]: while a strip part is selected: 6001
                                                    // whole HUD, 6000 recycle icon
inline constexpr size_t kHudCarryZonesCount = 2;
inline constexpr size_t kHudCarryZonesStride = 22;
inline constexpr uint16_t kPow10 = 0x1384;          // g_pow10 u32[7]: HUD digit divisors: [n] = 10^(n-1) for n = 1..6
                                                    // (entry 0 unused)
inline constexpr size_t kPow10Count = 7;

// --- Runtime context used here (summaries of other topics) (code_vehicles_races.md §2) -------------------------------
inline constexpr uint16_t kScreenAnims = 0x7A46;    // g_screenAnims far: list of running screen animations

// --- The parts model (code_vehicles_races.md §3) ---------------------------------------------------------------------
inline constexpr uint16_t kPartDeck = 0x659C;       // g_partDeck i16[]: shuffled deck of item kinds (part index or
                                                    // 1000) (§3.4)
inline constexpr uint16_t kMortyVehicle = 0x6634;   // g_mortyVehicle rec: Morty's vehicle (after the part catalogue)
                                                    // (§3.1)
inline constexpr uint16_t kLevelDesc = 0x6638;      // g_levelDesc far: level description and part catalogue (resource
                                                    // 0x2f5 + race) (§3.1)
inline constexpr uint16_t kParts = 0x663C;          // g_parts far: first part record (0x8e bytes) (§3.1)

// --- The HUD (segment 1a04) (code_vehicles_races.md §4) --------------------------------------------------------------
inline constexpr uint16_t kHudDigitColors = 0x131A; // g_hudDigitColors u8[3][4]: HUD digit colour triples (3 colours +
                                                    // FF) for Hud_SetDigitColors (§4.1)
inline constexpr size_t kHudDigitColorsCount = 3;
inline constexpr size_t kHudDigitColorsStride = 4;
inline constexpr uint16_t kHudSelectedPart = 0x1326; // g_hudSelectedPart i16: part selected in the HUD strip (-1 none)
                                                     // (§4.4)
inline constexpr uint16_t kHudLabelsShown = 0x132A; // g_hudLabelsShown i16: 0 while the part name box covers the HUD
                                                    // labels (§4.2)
inline constexpr uint16_t kKeyHeldSeen = 0x5C1F;    // g_keyHeldSeen i16: set to 1 by Input_GetHeldKeys when a key is
                                                    // held; read by the HUD (1a04:0b7c) (§4.2)
inline constexpr uint16_t kSndGlobalEffects = 0x9DC0; // g_sndGlobalEffects i16: sound handle of the global effect bank
                                                      // 0x105b (effect 0 = the silence song) (§4.2)

// --- The workshop (state 6, ovl08) (code_vehicles_races.md §5) -------------------------------------------------------
inline constexpr uint16_t kWorkshopSequence = 0x219A; // g_workshopSequence far[]: workshop sub-sequence run by Seq_Run
                                                      // (§5.2)
inline constexpr uint16_t kShelfZones = 0x21A2;     // g_shelfZones rec[2][22]: workshop shelf part zones: release 1004
                                                    // (lower part) / 1005 (upper part), rectangles set at run time;
                                                    // they follow the NULL of g_workshopSequence (§5.2)
inline constexpr size_t kShelfZonesCount = 2;
inline constexpr size_t kShelfZonesStride = 22;
inline constexpr uint16_t kCarriedRow = 0x22A4;     // g_carriedRow i16: -1 nothing carried in the workshop; else the
                                                    // carried part's flags without 0x8000 (bit 0 = upper row) (§5.4)
inline constexpr uint16_t kTrophyX = 0x22E2;        // g_trophyX i16[5]: workshop trophy x per level (§5.2)
inline constexpr size_t kTrophyXCount = 5;
inline constexpr uint16_t kTrophyY = 0x22EC;        // g_trophyY i16[3]: workshop trophy shelf y per building (§5.2)
inline constexpr size_t kTrophyYCount = 3;
inline constexpr uint16_t kWorkshopMusicCues = 0x4556; // g_workshopMusicCues i16: Music_WorkshopCue call count (§5.2)
inline constexpr uint16_t kPartsInstalled = 0x4584; // g_partsInstalled i16: required parts installed on the vehicle
                                                    // (complete when = catalogue +0x06) (§5.3)
inline constexpr uint16_t kVehicleBonusGiven = 0x4586; // g_vehicleBonusGiven i16: the 100-point vehicle completion
                                                       // bonus was given this level (§5.4)
inline constexpr uint16_t kPaintColor = 0x4588;     // g_paintColor i16: paint colour applied in the workshop (-1 none)
                                                    // (§5.3)
inline constexpr uint16_t kHintCancel1046 = 0x458A; // g_hintCancel1046 i16: cancels hint 0x1046 (§5.4)
inline constexpr uint16_t kVehicleColor = 0x5C10;   // g_vehicleColor u8[15]: vehicle colour per race index (player
                                                    // record +0x16ae), written by the workshop paint (2dee:055d, 11fa:
                                                    // paint 0..3 or the body default 4/5), read by the race sprites
                                                    // (3dd6:0825, 3e77) and the hub trophies (12e0:024d) (§5.3)
inline constexpr size_t kVehicleColorCount = 15;
inline constexpr uint16_t kSndBankCueTick = 0x9DC2; // g_sndBankCueTick u16: word read from a music bank's L member
                                                    // (misread: 0x4902 / 0x4905 for the workshop banks) (§5.1)
inline constexpr uint16_t kCarriedPart = 0xA07C;    // g_carriedPart far: part carried on the cursor in the workshop
                                                    // (§5.6)
inline constexpr uint16_t kShelfDirty = 0xA080;     // g_shelfDirty i16: workshop shelf needs redrawing (holds the
                                                    // scroll direction while scrolling) (§5.4)
inline constexpr uint16_t kScrollLeft = 0xA084;     // g_scrollLeft i16: conveyor scroll distance left (px) (§5.4)
inline constexpr uint16_t kScrollDir = 0xA086;      // g_scrollDir i16: conveyor scroll: 0 none, -1 parts move left, 1
                                                    // right (§5.4)
inline constexpr uint16_t kWorkshopSeqState = 0xA088; // g_workshopSeqState i16: state of the workshop sub-sequence
                                                      // (never written: 0, so Seq_Run(DS:219a) runs nothing) (§5.2)
inline constexpr uint16_t kBlueprintBundle = 0xA08A; // g_blueprintBundle far: loaded blueprint bundle
inline constexpr uint16_t kBlueprintPageCount = 0xA092; // g_blueprintPageCount i16: number of part pages of the
                                                        // blueprint
inline constexpr uint16_t kBlueprintPage = 0xA094;  // g_blueprintPage i16: part page shown
inline constexpr uint16_t kVehicleSprites = 0xA0B4; // g_vehicleSprites far: list of vehicle sprite records (0x36 bytes,
                                                    // layer-sorted) (§5.3)
inline constexpr uint16_t kShelfCount = 0xA0C8;     // g_shelfCount i16: lower-row parts on the workshop shelf (§5.3)
inline constexpr uint16_t kWorkshopNextState = 0xA0CA; // g_workshopNextState i16: state returned by the workshop: 8 or
                                                       // 16 (§5.2)
inline constexpr uint16_t kVehicleSpritePool = 0xA0CC; // g_vehicleSpritePool far: 20 vehicle sprite records of 0x36
                                                       // bytes (§5.1)
inline constexpr uint16_t kRaceDoorOpen = 0xA0D0;   // g_raceDoorOpen i16: workshop race-track door open

// --- The blueprint viewer (code_vehicles_races.md §6) ----------------------------------------------------------------
inline constexpr uint16_t kBlueprintFramePos = 0x217C; // g_blueprintFramePos i16[6][2]: x, y of the blueprint opening
                                                       // pictures 0x13..0x18 (§6.1)
inline constexpr size_t kBlueprintFramePosCount = 6;
inline constexpr size_t kBlueprintFramePosStride = 4;
inline constexpr uint16_t kBlueprintOpen = 0x2198;  // g_blueprintOpen i16: 1 while the blueprint viewer is open (the
                                                    // workshop music cue then does not switch channels) (§6.1)
inline constexpr uint16_t kBlueprintDoneButton = 0x2280; // g_blueprintDoneButton rec: Done button of the blueprint
                                                         // viewer at (248,142), code 1007 (§6.1)
inline constexpr uint16_t kBlueprintTitle = 0x22A0; // g_blueprintTitle far: -> the 'Level-N' string at DS:22f2, char 6
                                                    // = '1' + level (§6.1)
inline constexpr uint16_t kBlueprintPics = 0x22A6;  // g_blueprintPics u16[15]: blueprint picture per race index (§6.2)
inline constexpr size_t kBlueprintPicsCount = 15;
inline constexpr uint16_t kBlueprintBundles = 0x22C4; // g_blueprintBundles u16[15]: blueprint bundle per race index
                                                      // ([I,I,S,T*n,G*n,S*n,I,M*z]) (§6.1)
inline constexpr size_t kBlueprintBundlesCount = 15;

// --- Vehicle performance (code_vehicles_races.md §8) -----------------------------------------------------------------
inline constexpr uint16_t kLandPerfTables = 0x3A54; // g_landPerfTables u32[]: land-race performance tables (16.16 per
                                                    // variant) DS:3a54..3b33, read by Race_ComputePerformance (§8.1)
inline constexpr uint16_t kLandBaseSpeed = 0x3B34;  // g_landBaseSpeed u32[15]: base top speed per race index (16.16)
                                                    // (§8.1)
inline constexpr size_t kLandBaseSpeedCount = 15;
inline constexpr uint16_t kAirBaseSpeed = 0x3DBE;   // g_airBaseSpeed u32[5]: base speed per aircraft level (16.16)
                                                    // (§8.2)
inline constexpr size_t kAirBaseSpeedCount = 5;

// --- The land and alternative-energy race (ovl24) (code_vehicles_races.md §9) ----------------------------------------
inline constexpr uint16_t kRaceMusicBanks = 0x3994; // g_raceMusicBanks u16[3]: race music bank per building (0xf7,
                                                    // unused, 0xf9) (§9.1)
inline constexpr size_t kRaceMusicBanksCount = 3;
inline constexpr uint16_t kRaceBackgrounds = 0x399A; // g_raceBackgrounds u16[15]: per race index: background (§9.1)
inline constexpr size_t kRaceBackgroundsCount = 15;
inline constexpr uint16_t kRaceSprites1 = 0x39B8;   // g_raceSprites1 u16[15]: per race index: sprite set (§9.1)
inline constexpr size_t kRaceSprites1Count = 15;
inline constexpr uint16_t kRaceSprites2 = 0x39D6;   // g_raceSprites2 u16[15]: per race index: sprite set (§9.1)
inline constexpr size_t kRaceSprites2Count = 15;
inline constexpr uint16_t kRacePaths1 = 0x39F4;     // g_racePaths1 u16[15]: per race index: 'R' path (§9.1)
inline constexpr size_t kRacePaths1Count = 15;
inline constexpr uint16_t kRacePaths2 = 0x3A12;     // g_racePaths2 u16[15]: per race index: 'R' path (§9.1)
inline constexpr size_t kRacePaths2Count = 15;
inline constexpr uint16_t kRaceExtraLaps = 0x3A30;  // g_raceExtraLaps u16[15]: laps to run before the finish line
                                                    // counts, per race index (§9.1)
inline constexpr size_t kRaceExtraLapsCount = 15;
inline constexpr uint16_t kRaceIntroTicks = 0x3A4E; // g_raceIntroTicks u16[3]: music sound ticks to wait before a land
                                                    // race starts, per building (§9.1)
inline constexpr size_t kRaceIntroTicksCount = 3;
inline constexpr uint16_t kRaceMorty = 0xBAAE;      // g_raceMorty rec[0x3a]: land race racer record of Morty (§9.1)
inline constexpr size_t kRaceMortySize = 58;
inline constexpr uint16_t kRacePlayer = 0xBAE8;     // g_racePlayer rec[0x3a]: land race racer record of the player
                                                    // (§9.1)
inline constexpr size_t kRacePlayerSize = 58;
inline constexpr uint16_t kRaceFinishBits = 0xBB22; // g_raceFinishBits u16: racer ids that crossed the finish (1
                                                    // player, 2 Morty) (§9.3)

// --- The aircraft race (ovl25) (code_vehicles_races.md §10) ----------------------------------------------------------
inline constexpr uint16_t kAirCamera10 = 0x3B70;    // g_airCamera10 u32: aircraft race camera / 10 (16.16): scroll of
                                                    // the far scenery layer (§10.5)
inline constexpr uint16_t kAirCamera = 0x3B74;      // g_airCamera u32: aircraft race camera x (16.16) (§10.5)
inline constexpr uint16_t kAirAnimFrames = 0x3B78;  // g_airAnimFrames u16[5]: aircraft sprite animation frames per
                                                    // level (§10.1)
inline constexpr size_t kAirAnimFramesCount = 5;
inline constexpr uint16_t kAirSprites1 = 0x3B82;    // g_airSprites1 u16[5]: player aircraft sprite bundle per level
                                                    // (painted) (§10.1)
inline constexpr size_t kAirSprites1Count = 5;
inline constexpr uint16_t kAirSprites2 = 0x3B8C;    // g_airSprites2 u16[5]: Morty's aircraft sprite bundle per level
                                                    // (§10.1)
inline constexpr size_t kAirSprites2Count = 5;
inline constexpr uint16_t kAirPaths1 = 0x3B96;      // g_airPaths1 u16[5]: player aircraft path per level (§10.1)
inline constexpr size_t kAirPaths1Count = 5;
inline constexpr uint16_t kAirPaths2 = 0x3BA0;      // g_airPaths2 u16[5]: Morty's aircraft path per level (§10.1)
inline constexpr size_t kAirPaths2Count = 5;
inline constexpr uint16_t kAirPerfTables = 0x3BAA;  // g_airPerfTables u32[]: aircraft performance tables (16.16 per
                                                    // variant, 3 or 4 entries each) DS:3baa..3da9, read by
                                                    // AirRace_ComputePerformance
inline constexpr uint16_t kAirCameraLimit = 0x3DAA; // g_airCameraLimit i16[5][2]: screen-x limit (player, Morty) for
                                                    // the camera to follow, per level (never binds) (§10.1)
inline constexpr size_t kAirCameraLimitCount = 5;
inline constexpr size_t kAirCameraLimitStride = 4;
inline constexpr uint16_t kAirPathEnd = 0xBB26;     // g_airPathEnd i16: set when an aircraft path's -1 record is
                                                    // reached (never read) (§10.4)
inline constexpr uint16_t kAirMorty = 0xBB28;       // g_airMorty rec[0x3c]: aircraft racer record of Morty (§10.1)
inline constexpr size_t kAirMortySize = 60;
inline constexpr uint16_t kAirPlayer = 0xBB64;      // g_airPlayer rec[0x3c]: aircraft racer record of the player
                                                    // (§10.1)
inline constexpr size_t kAirPlayerSize = 60;
inline constexpr uint16_t kAirScenery2 = 0xBBA0;    // g_airScenery2 far: near scenery layer (W 0x264 + level)
inline constexpr uint16_t kAirScenery1 = 0xBBA4;    // g_airScenery1 far: far scenery layer (backdrop tiles)
inline constexpr uint16_t kAirLaunchPath2 = 0xBBA8; // g_airLaunchPath2 far: level 1 launch path 0x253
inline constexpr uint16_t kAirLaunchPath1 = 0xBBAC; // g_airLaunchPath1 far: level 1 launch path 0x252 (NULL on other
                                                    // levels)

// --- Results (states 12 and 14, ovl10) (code_vehicles_races.md §11) --------------------------------------------------
inline constexpr uint16_t kRankThresholds = 0x1F9C; // g_rankThresholds u32[7]: 5000, 30000, 60000, 120000, 200000,
                                                    // 300000, 750000 (§11.3)
inline constexpr size_t kRankThresholdsCount = 7;
inline constexpr uint16_t kMortyNamePtr = 0x22FA;   // g_mortyNamePtr far: -> s_morty (result screen) (§11.1)
inline constexpr uint16_t kStrFirst = 0x2442;       // s_first char[]: result screen label of the winner (§11.1)
inline constexpr uint16_t kStrSecond = 0x2448;      // s_second char[]: result screen label of the loser
inline constexpr uint16_t kRacesWonTotal = 0x5C0E;  // g_racesWonTotal i16: 15 = the end (§11.1)
inline constexpr uint16_t kResultLost = 0xA0D2;     // g_resultLost i16: result screen: 1 = lost (warehouse button
                                                    // available) (§11.1)
inline constexpr uint16_t kResultNextRace = 0xA0D4; // g_resultNextRace i16: result screen: 1 = won and a next race
                                                    // exists in the building (§11.1)

// --- The final movie (state 15, 2fcb:0640) (code_vehicles_races.md §14) ----------------------------------------------
inline constexpr uint16_t kMovieCues = 0x236C;      // g_movieCues rec[]: final movie cues, words {type, frame, a, b}

// --- From a locked door to the puzzle (code_puzzles_a.md §1) ---------------------------------------------------------
inline constexpr uint16_t kPuzzleDoorHit = 0x00BC;  // g_puzzleDoorHit i16: set when the player touches a locked puzzle
                                                    // door (type 7) -> state 9 (§1.1)
inline constexpr uint16_t kPzGoBackZone = 0x3306;   // g_pzGoBackZone zone: (249,168) 65x19, press 0x5dd (§1.6)
inline constexpr uint16_t kPuzzleLeave = 0x3332;    // g_puzzleLeave i16: 1 = Start New Player, 2 = Return to Technology
                                                    // Center (menu codes 0x17a3 / 0x17a2 inside a puzzle) (§1.2)
inline constexpr uint16_t kPuzzleVariants = 0x3334; // g_puzzleVariants u16[8]: variants per puzzle type: 30, 20, 20,
                                                    // 31, 32, 30, 28, 30 (221) (§1.3)
inline constexpr size_t kPuzzleVariantsCount = 8;
inline constexpr uint16_t kPuzzleSolved = 0x45D0;   // g_puzzleSolved u16[8][33]: solved flags: + type*0x42 + variant*2
                                                    // (§1.3)
inline constexpr size_t kPuzzleSolvedCount = 8;
inline constexpr size_t kPuzzleSolvedStride = 66;
inline constexpr uint16_t kPlayerRoom = 0x7AA4;     // g_playerRoom i16: g_actors[0] +0x1a, the player's room (0 =
                                                    // workshop) (§1.1)
inline constexpr uint16_t kPuzzleVariant = 0xB138;  // g_puzzleVariant i16: variant chosen by State_09_Puzzle (§1.2)
inline constexpr uint16_t kPuzzleResult = 0xB13C;   // g_puzzleResult i16: the puzzle's return value (1 solved) (§1.2)
inline constexpr uint16_t kPuzzleCandidates = 0xB13E; // g_puzzleCandidates i16[3]: candidate variants (-1 empty) (§1.3)
inline constexpr size_t kPuzzleCandidatesCount = 3;
inline constexpr uint16_t kPuzzleGoThrough = 0xB144; // g_puzzleGoThrough i16: 1 = go through the opened door; set by
                                                     // Enter or the panel, 0 by Go Back, kept otherwise (§1.5)

// --- Common screen, panel and helpers (code_puzzles_a.md §2) ---------------------------------------------------------
inline constexpr uint16_t kElecHintZone = 0x24D0;   // g_elecHintZone zone: (26,13) 35x15, press 0x458 (§2.2)
inline constexpr uint16_t kElecTools = 0x24E6;      // g_elecTools rec[5][16]: toolbox: x, y, far picture (filled at run
                                                    // time), digit x, digit y, hot x, hot y
inline constexpr size_t kElecToolsCount = 5;
inline constexpr size_t kElecToolsStride = 16;
inline constexpr uint16_t kLaunchZones = 0x2B02;    // g_launchZones zone[16]: hint, box items (tag 1000), ramp clicks,
                                                    // gravity, plunger zones (22 bytes each, DS:2b02..2c61) (§2.2)
inline constexpr size_t kLaunchZonesCount = 16;
inline constexpr uint16_t kPzSndCount = 0x338C;     // g_pzSndCount i16: members of the loaded puzzle effect bank (§2.4)
inline constexpr uint16_t kPzGoBackPic = 0xB130;    // g_pzGoBackPic far: pressed Go Back picture 0x1092
                                                    // (Puzzle_InitPanel) (§2.2)
inline constexpr uint16_t kPzPanelPic = 0xB134;     // g_pzPanelPic far: panel picture 0x1091 (Puzzle_InitPanel) (§2.2)
inline constexpr uint16_t kPzSndBank = 0xB1B0;      // g_pzSndBank far: copy of the puzzle effect bank (§2.4)

// --- Electric circuits (kind 0, ovl13 3083, electric.c) (code_puzzles_a.md §3) ---------------------------------------
inline constexpr uint16_t kElecScreenRegion = 0x2488; // g_elecScreenRegion rec[0x1c]: whole-screen cursor region,
                                                      // cursor = the puzzle cursor DS:a8d8 (§3.5)
inline constexpr size_t kElecScreenRegionSize = 28;
inline constexpr uint16_t kElecToolboxZone = 0x24A4; // g_elecToolboxZone zone: (7,67) 78x94, priority 100, press 0x459:
                                                     // put the held item back (§3.5)
inline constexpr uint16_t kElecBoardZone = 0x24BA;  // g_elecBoardZone zone: (91,17) 222x135, press 0x454 (§3.5)
inline constexpr uint16_t kElecPathCount = 0xA2C8;  // g_elecPathCount i16: rows in DS:a138 (§3.3)
inline constexpr uint16_t kElecPieces = 0xA2FC;     // g_elecPieces rec[15][16]: type, x, y, far picture, used, switch
                                                    // closed, fixed (§3.2)
inline constexpr size_t kElecPiecesCount = 15;
inline constexpr size_t kElecPiecesStride = 16;
inline constexpr uint16_t kElecTerminals = 0xA3EC;  // g_elecTerminals rec[15][3][26]: per piece and terminal: hit rect,
                                                    // anchor, dot offset, overlap height, link piece, link terminal,
                                                    // wire flag (§3.2)
inline constexpr size_t kElecTerminalsCount = 15;
inline constexpr size_t kElecTerminalsStride = 78;
inline constexpr uint16_t kElecBatteryShorted = 0xA87E; // g_elecBatteryShorted i16[15]: PzElec_BatteryShorted per
                                                        // battery (§3.8)
inline constexpr size_t kElecBatteryShortedCount = 15;
inline constexpr uint16_t kElecOnPath = 0xA89C;     // g_elecOnPath i16[15]: pieces on a closed path of the last bulb
                                                    // tested (§3.11)
inline constexpr size_t kElecOnPathCount = 15;
inline constexpr uint16_t kElecMode = 0xA8BE;       // g_elecMode i16: 99 idle, 0..3 a piece in hand, 4 a wire in hand
                                                    // (§3.2)
inline constexpr uint16_t kElecFoundTerm = 0xA8C0;  // g_elecFoundTerm i16: terminal found by PzElec_FindFreeTerminal
                                                    // (§3.7)
inline constexpr uint16_t kElecFoundPiece = 0xA8C2; // g_elecFoundPiece i16: piece found by PzElec_FindFreeTerminal (99
                                                    // none) (§3.7)
inline constexpr uint16_t kElecPlugShown = 0xA8C4;  // g_elecPlugShown i16: 1 while the plug picture replaces the held
                                                    // cursor (§3.7)
inline constexpr uint16_t kElecToolCounts = 0xA8C6; // g_elecToolCounts i16[5]: toolbox counts: battery, bulb, switch,
                                                    // Y-wire, wire (§3.2)
inline constexpr size_t kElecToolCountsCount = 5;
inline constexpr uint16_t kElecHeldCursor = 0xA8D4; // g_elecHeldCursor far: cursor record of the item in hand
inline constexpr uint16_t kElecCursor = 0xA8D8;     // g_elecCursor far: puzzle cursor (picture 0x20b4, hot x 1) (§3.1)
inline constexpr uint16_t kElecToolZones = 0xA8DC;  // g_elecToolZones zone[5]: toolbox item zones, press 0x45b + item
                                                    // (PzElec_InitToolZones) (§3.5)
inline constexpr size_t kElecToolZonesCount = 5;
inline constexpr uint16_t kElecPieceZones = 0xA94A; // g_elecPieceZones zone[15]: placed piece zones, press 0x4b0 + slot
                                                    // (PzElec_CreatePiece) (§3.5)
inline constexpr size_t kElecPieceZonesCount = 15;
inline constexpr uint16_t kElecToolRegions = 0xAA94; // g_elecToolRegions rec[5][0x1c]: label cursor regions over the
                                                     // toolbox items (cursor at +0x18) (§3.5)
inline constexpr size_t kElecToolRegionsCount = 5;
inline constexpr size_t kElecToolRegionsStride = 28;
inline constexpr uint16_t kBalanceRightHeavy = 0xAB20; // g_balanceRightHeavy i16: 1 when the right side was heavier as
                                                       // the beam left the level (PzBalance_Compare); swing direction
                                                       // from level
inline constexpr uint16_t kBalanceZoneSet = 0xAB22; // g_balanceZoneSet i16: hook zone set in use: 0 tilt -5, 1 tilt +5,
                                                    // 2 level (-1 none)
inline constexpr uint16_t kBalanceTilt = 0xAB24;    // g_balanceTilt i16: beam tilt -5..5: negative = right side down, 0
                                                    // level
inline constexpr uint16_t kBalanceHeld = 0xAB26;    // g_balanceHeld i16: weight type carried, -1 none

// --- Energy forms (kind 6, ovl15 33cd) (code_puzzles_a.md §4) --------------------------------------------------------
inline constexpr uint16_t kEnergyWorkZone = 0x285A; // g_energyWorkZone zone: (59,16) 254x141, press 0x44c (§4.3)
inline constexpr uint16_t kEnergyLayout = 0x28C8;   // g_energyLayout i16[13]: device x, device y[4], form zone x,
                                                    // device zone x, zone y[4], line x at forms, line x at devices
                                                    // (line y at DS:28e2) (§4.3)
inline constexpr size_t kEnergyLayoutCount = 13;
inline constexpr uint16_t kEnergyLineY = 0x28E2;    // g_energyLineY i16[4]: y of the line ends per row (§4.3)
inline constexpr size_t kEnergyLineYCount = 4;
inline constexpr uint16_t kStrEnergyNames = 0x28EA; // s_energyNames char[8][20]: the eight energy-form names, 20 bytes
                                                    // each (§4.2)
inline constexpr size_t kStrEnergyNamesCount = 8;
inline constexpr size_t kStrEnergyNamesStride = 20;
inline constexpr uint16_t kEnergyDevices = 0xAD70;  // g_energyDevices rec[4][18]: device resource id (-1 none) and 8
                                                    // uses-form flags (§4.2)
inline constexpr size_t kEnergyDevicesCount = 4;
inline constexpr size_t kEnergyDevicesStride = 18;
inline constexpr uint16_t kEnergyForms = 0xADB8;    // g_energyForms rec[4][26]: form index (-1 none), needed-line-
                                                    // satisfied[4], sent[4] (indexed device, form), line[4] (§4.2)
inline constexpr size_t kEnergyFormsCount = 4;
inline constexpr size_t kEnergyFormsStride = 26;
inline constexpr uint16_t kEnergySatisfied = 0xADBA; // g_energySatisfied i16: satisfied[k][d] at + 0x1a*k + 2*d (inside
                                                     // the DS:adb8 rows): no line needed, or it is drawn (§4.2)
inline constexpr uint16_t kEnergySent = 0xADC2;     // g_energySent i16: sent[d][k] at + 0x1a*d + 2*k (inside the
                                                    // DS:adb8 rows): the line k->d delivered energy
inline constexpr uint16_t kEnergyLines = 0xADCA;    // g_energyLines i16: line[k][d] at + 0x1a*k + 2*d (inside the
                                                    // DS:adb8 rows): drawn
inline constexpr uint16_t kEnergyIdleFrame = 0xAE20; // g_energyIdleFrame i16[4]: idle frame per device, -1 once it has
                                                     // worked
inline constexpr size_t kEnergyIdleFrameCount = 4;
inline constexpr uint16_t kEnergyMode = 0xAE88;     // g_energyMode i16: 0 pencil, 1 eraser, -1 no line left
inline constexpr uint16_t kEnergyLinesLeft = 0xAE8A; // g_energyLinesLeft i16: lines the player may still draw
inline constexpr uint16_t kEnergyDeviceZones = 0xAE8C; // g_energyDeviceZones zone[4]: circular device connector zones,
                                                       // press 0x456 + d
inline constexpr size_t kEnergyDeviceZonesCount = 4;
inline constexpr uint16_t kEnergyFormZones = 0xAEE4; // g_energyFormZones zone[4]: circular form connector zones, press
                                                     // 0x452 + k
inline constexpr size_t kEnergyFormZonesCount = 4;
inline constexpr uint16_t kMagnetsHeldOrient = 0xAF48; // g_magnetsHeldOrient i16: orientation 0..3 of the carried
                                                       // magnet
inline constexpr uint16_t kMagnetsHeld = 0xAF4A;    // g_magnetsHeld i16: kind of the carried magnet, -1 none
inline constexpr uint16_t kMagnetsCounts = 0xAF4C;  // g_magnetsCounts i16[3]: magnets left in the inventory per kind
inline constexpr size_t kMagnetsCountsCount = 3;
inline constexpr uint16_t kMagnetsSlots = 0xAF52;   // g_magnetsSlots far: 64 magnet records of 0x12 bytes (kind -1 =
                                                    // free)
inline constexpr uint16_t kMagnetsTargetCounts = 0xAF58; // g_magnetsTargetCounts far: D 0x11a1: i16 targets per level
                                                         // [30]
inline constexpr uint16_t kMagnetsTargets = 0xAF5C; // g_magnetsTargets far: D 0x11a1 + 0x3c: target records [30][40] of
                                                    // 4 i16

// --- Launch (kind 5, ovl17 3797) (code_puzzles_a.md §5) --------------------------------------------------------------
inline constexpr uint16_t kLaunchObjectTerm = 0x2FE6; // g_launchObjectTerm i16[4]: landing index term per object (§5.5)
inline constexpr size_t kLaunchObjectTermCount = 4;
inline constexpr uint16_t kLaunchSurfaceTerm = 0x2FEE; // g_launchSurfaceTerm i16[3]: landing index term per surface
                                                       // state (§5.5)
inline constexpr size_t kLaunchSurfaceTermCount = 3;
inline constexpr uint16_t kLaunchGravityTerm = 0x2FF4; // g_launchGravityTerm i16[3]: landing index term per gravity
                                                       // (DS:2ff6 when gravity is locked) (§5.5)
inline constexpr size_t kLaunchGravityTermCount = 3;
inline constexpr uint16_t kLaunchRampTerm = 0x2FFA; // g_launchRampTerm i16[3]: landing index term per ramp (§5.5)
inline constexpr size_t kLaunchRampTermCount = 3;
inline constexpr uint16_t kLaunchLandingTable = 0x3000; // g_launchLandingTable i16[29]: landing position 0..12 by index
                                                        // (§5.5)
inline constexpr size_t kLaunchLandingTableCount = 29;
inline constexpr uint16_t kLaunchFlightFrames = 0x303A; // g_launchFlightFrames i16[9]: object frame per flight step
                                                        // (§5.5)
inline constexpr size_t kLaunchFlightFramesCount = 9;
inline constexpr uint16_t kLaunchHoleClipLeft = 0x304C; // g_launchHoleClipLeft i16[9]: clip left x of the last flight
                                                        // step, per landing 4..12 (§5.5)
inline constexpr size_t kLaunchHoleClipLeftCount = 9;
inline constexpr uint16_t kLaunchHoleClipBottom = 0x305C; // g_launchHoleClipBottom i16[9]: clip bottom y of the last
                                                          // flight step, per landing 4..12 (§5.5)
inline constexpr size_t kLaunchHoleClipBottomCount = 9;
inline constexpr uint16_t kLaunchPathX = 0x3120;    // g_launchPathX far[9]: x arrays of the flight per landing 4..12
                                                    // (§5.5)
inline constexpr size_t kLaunchPathXCount = 9;
inline constexpr uint16_t kLaunchPathYA = 0x3144;   // g_launchPathYA far[9]: y arrays per landing, ramps 2 and 3 (§5.5)
inline constexpr size_t kLaunchPathYACount = 9;
inline constexpr uint16_t kLaunchClimbX = 0x3168;   // g_launchClimbX i16[4]: x of the climb and roll-back steps (§5.5)
inline constexpr size_t kLaunchClimbXCount = 4;
inline constexpr uint16_t kLaunchClimbYA = 0x3170;  // g_launchClimbYA i16[4]: y of the climb and roll-back steps, ramps
                                                    // 2 and 3 (§5.5)
inline constexpr size_t kLaunchClimbYACount = 4;
inline constexpr uint16_t kLaunchPathYB = 0x31F6;   // g_launchPathYB far[9]: y arrays per landing, ramps 1 and 4 (§5.5)
inline constexpr size_t kLaunchPathYBCount = 9;
inline constexpr uint16_t kLaunchClimbYB = 0x321A;  // g_launchClimbYB i16[4]: roll-back y, ramps 1 and 4 (§5.5)
inline constexpr size_t kLaunchClimbYBCount = 4;
inline constexpr uint16_t kLaunchPathYC = 0x32A0;   // g_launchPathYC far[9]: y arrays per landing, ramps 0 and 5 (§5.5)
inline constexpr size_t kLaunchPathYCCount = 9;
inline constexpr uint16_t kLaunchClimbYC = 0x32C4;  // g_launchClimbYC i16[4]: roll-back y, ramps 0 and 5 (§5.5)
inline constexpr size_t kLaunchClimbYCCount = 4;
inline constexpr uint16_t kLaunchHoleX = 0x32CC;    // g_launchHoleX i16[8]: target glow x per hole (§5.5)
inline constexpr size_t kLaunchHoleXCount = 8;
inline constexpr uint16_t kLaunchHoleY = 0x32DC;    // g_launchHoleY i16[8]: target glow y per hole (§5.5)
inline constexpr size_t kLaunchHoleYCount = 8;
inline constexpr uint16_t kLaunchRampX = 0x32EC;    // g_launchRampX i16[6]: ramp picture x per ramp 0..5 (§5.5)
inline constexpr size_t kLaunchRampXCount = 6;
inline constexpr uint16_t kLaunchRampY = 0x32F8;    // g_launchRampY i16[6]: ramp picture y per ramp 0..5 (§5.5)
inline constexpr size_t kLaunchRampYCount = 6;
inline constexpr uint16_t kLaunchGlowFrame = 0x3304; // g_launchGlowFrame i16: target glow frame 0..7 (§5.5)
inline constexpr uint16_t kLaunchGlowOn = 0xAF90;   // g_launchGlowOn i16: 1 until solved: the target hole glows (§5.2)
inline constexpr uint16_t kLaunchObjectShown = 0xAF9A; // g_launchObjectShown i16: object sprite valid (§5.4)
inline constexpr uint16_t kLaunchRampSet = 0xAFA0;  // g_launchRampSet far: ramp picture set of the current surface
                                                    // (DS:b02c / b014 / affc) (§5.5)
inline constexpr uint16_t kLaunchObject = 0xB118;   // g_launchObject i16: selected object 0..3, -1 none (§5.4)
inline constexpr uint16_t kLaunchSurface = 0xB11A;  // g_launchSurface i16: surface state 0..2 (§5.2)
inline constexpr uint16_t kLaunchLanding = 0xB11C;  // g_launchLanding i16: landing position 0..12 (4..11 = holes 0..7)
inline constexpr uint16_t kLaunchGravity = 0xB11E;  // g_launchGravity i16: 0 high, 1 normal, 2 low, -1 locked (§5.2)
inline constexpr uint16_t kLaunchObjectFrames = 0xB120; // g_launchObjectFrames far: frame set of the selected object
inline constexpr uint16_t kLaunchRamp = 0xB124;     // g_launchRamp i16: ramp 0..2 (choosable) or 3..5 (fixed) (§5.2)
inline constexpr uint16_t kLaunchTarget = 0xB126;   // g_launchTarget i16: target hole 0..7
inline constexpr uint16_t kLaunchGravityPic = 0xB128; // g_launchGravityPic i16: gravity slider picture 1..3 (0x114a)
                                                      // (§5.4)
inline constexpr uint16_t kLaunchEnergy = 0xB12A;   // g_launchEnergy i16: plunger energy 0..7
inline constexpr uint16_t kLaunchVariant = 0xB12C;  // g_launchVariant far: pointer to the variant record (§5.2)

// --- Simple machines (kind 1, ovl20 3938, simple.c) (code_puzzles_a.md §6) -------------------------------------------
inline constexpr uint16_t kSimpleScreenRegion = 0x338E; // g_simpleScreenRegion rec[0x1c]: whole-screen cursor region,
                                                        // cursor = the puzzle cursor DS:b41a (§6.3)
inline constexpr size_t kSimpleScreenRegionSize = 28;
inline constexpr uint16_t kSimpleWorkZone = 0x33AA; // g_simpleWorkZone zone: (66,14) 245x142, press 0x4b1 (§6.3)
inline constexpr uint16_t kSimpleCheckZone = 0x33C0; // g_simpleCheckZone zone: (10,150) 36x13, press 0x4b0 (§6.3)
inline constexpr uint16_t kSimpleInventoryZone = 0x33D6; // g_simpleInventoryZone zone: (4,49) 48x98, priority 100,
                                                         // press 0x44c: put the held part back (§6.3)
inline constexpr uint16_t kSimpleHintZone = 0x33EC; // g_simpleHintZone zone: (11,13) 35x15, press 0x458 (§6.3)
inline constexpr uint16_t kSimplePartLayout = 0x3402; // g_simplePartLayout rec[8][15]: per part type: icon x, y, (byte
                                                      // type), digit x, y, test-point dx, dy, fit sound (§6.6)
inline constexpr size_t kSimplePartLayoutCount = 8;
inline constexpr size_t kSimplePartLayoutStride = 15;
inline constexpr uint16_t kSimpleFitDx = 0x347A;    // g_simpleFitDx i16[8]: x offset of the fit animation per part type
                                                    // (§6.5)
inline constexpr size_t kSimpleFitDxCount = 8;
inline constexpr uint16_t kSimpleFitDy = 0x348A;    // g_simpleFitDy i16[8]: y offset of the fit animation per part type
                                                    // (§6.5)
inline constexpr size_t kSimpleFitDyCount = 8;
inline constexpr uint16_t kPiecesExampleNames = 0x357C; // g_piecesExampleNames far[20]: per variant: the machine's name
                                                        // (DS:35cc..) for the %s of the hint text 0x20b1 (§6.7)
inline constexpr size_t kPiecesExampleNamesCount = 20;
inline constexpr uint16_t kStrPiecesExamples = 0x35CC; // s_piecesExamples char[]: the 20 machine names of the machine-
                                                       // pieces hint (NUL-terminated, DS:35cc..36cf) (§6.7)
inline constexpr uint16_t kSimpleData = 0xB1B4;     // g_simpleData far: copy of F 0x206d + variant (0x570 bytes) (§6.1)
inline constexpr uint16_t kSimpleHeld = 0xB1B8;     // g_simpleHeld i16: part type in hand, 9 none
inline constexpr uint16_t kSimpleSlotZones = 0xB21A; // g_simpleSlotZones zone[8]: inventory slot zones, press 0x44d +
                                                     // type
inline constexpr size_t kSimpleSlotZonesCount = 8;
inline constexpr uint16_t kSimplePlaced = 0xB38A;   // g_simplePlaced rec[10][14]: placed parts: type (9 free), x, y, w,
                                                    // h, test x, test y
inline constexpr size_t kSimplePlacedCount = 10;
inline constexpr size_t kSimplePlacedStride = 14;
inline constexpr uint16_t kSimpleCursor = 0xB41A;   // g_simpleCursor far: puzzle cursor (resource 3) (§6.1)
inline constexpr uint16_t kSimpleSlotRegions = 0xB41E; // g_simpleSlotRegions rec[8][0x1c]: label cursor regions over
                                                       // the inventory slots (cursor at +0x18)
inline constexpr size_t kSimpleSlotRegionsCount = 8;
inline constexpr size_t kSimpleSlotRegionsStride = 28;

// --- Porting notes (code_puzzles_a.md §9) ----------------------------------------------------------------------------
inline constexpr uint16_t kLaunchVariants = 0x2C62; // g_launchVariants i16[30][15]: launch variant records (30 bytes)
inline constexpr size_t kLaunchVariantsCount = 30;
inline constexpr size_t kLaunchVariantsStride = 30;

// --- How the four puzzles plug into the framework (code_puzzles_b.md §1) ---------------------------------------------
inline constexpr uint16_t kBalanceHintZone = 0x2828; // g_balanceHintZone zone: (6,13) 35x15, press 0x458 (§1.2)
inline constexpr uint16_t kMagnetsHintZone = 0x29B6; // g_magnetsHintZone zone: (13,13) 36x15, press 0x458 (§1.2)
inline constexpr uint16_t kMagnetsRotateZone = 0x29CC; // g_magnetsRotateZone zone: (3,124) 61x29, press 0x45a: ROTATE
                                                       // (only while a magnet is carried)
inline constexpr uint16_t kPzPanelZone = 0x331C;    // g_pzPanelZone zone: (2,168) 212x32, priority 100, release 0x5de
                                                    // (pushed when solved) (§1.2)
inline constexpr uint16_t kGearsHintZone = 0x38EC;  // g_gearsHintZone zone: (23,24) 36x15, press 0x3e8 (§1.2)
inline constexpr uint16_t kCursorArrow = 0x7A82;    // g_cursorArrow far: the default cursor record (picture resource
                                                    // 0x0000) (§1.2)
inline constexpr uint16_t kPuzzleKind = 0xB13A;     // g_puzzleKind i16: kind 0..7 chosen by State_09_Puzzle (§1.1)
inline constexpr uint16_t kGearsCrankZone = 0xB818; // g_gearsCrankZone zone: crank handle rectangle of the current
                                                    // frame, priority 99, press 0x44c (§1.3)

// --- Machine pieces (kind 2, ovl21 3a4a) (code_puzzles_b.md §2) ------------------------------------------------------
inline constexpr uint16_t kPiecesWorkZone = 0x353A; // g_piecesWorkZone zone: (93,14) 219x142, press 0x454: drop the
                                                    // carried piece (§2.4)
inline constexpr uint16_t kPiecesBoxZone = 0x3550;  // g_piecesBoxZone zone: (5,82) 80x62, press 0x459: take a piece out
                                                    // of the box (§2.4)
inline constexpr uint16_t kPiecesHintZone = 0x3566; // g_piecesHintZone zone: (26,13) 35x15, press 0x458 (§2.4)
inline constexpr uint16_t kCursorSavedX = 0x7A6C;   // g_cursorSavedX i16: x of the saved background (the buffer is
                                                    // DS:7a7e) (§2.6)
inline constexpr uint16_t kCursorW = 0x7A70;        // g_cursorW i16: active cursor width (§2.6)
inline constexpr uint16_t kCursorH = 0x7A72;        // g_cursorH i16: active cursor height (§2.6)
inline constexpr uint16_t kMouseX = 0x7A88;         // g_mouseX i16: cursor x = driver x / 2 (top-left of the cursor
                                                    // picture) (§2.6)
inline constexpr uint16_t kPiecesPanelText = 0xB502; // g_piecesPanelText far: text 0x20b0 (§2.1)
inline constexpr uint16_t kPiecesHintFormat = 0xB506; // g_piecesHintFormat far: text 0x20b1 (one %s: the machine's
                                                      // name) (§2.1)
inline constexpr uint16_t kPiecesMachinePic = 0xB50A; // g_piecesMachinePic far: last member of bundle 0x2088 + variant:
                                                      // the assembled machine (§2.1)
inline constexpr uint16_t kPiecesHintPic = 0xB50E;  // g_piecesHintPic far: pressed Hint 0x1095 (§2.1)
inline constexpr uint16_t kPiecesBoxPics = 0xB512;  // g_piecesBoxPics far: bundle 0x2087: box lid closed, half open,
                                                    // open (§2.1)
inline constexpr uint16_t kPiecesDigits = 0xB516;   // g_piecesDigits far: bundle 0x204f: digits (§2.1)
inline constexpr uint16_t kPiecesBundle = 0xB51A;   // g_piecesBundle far: copy of bundle 0x2088 + variant (§2.1)
inline constexpr uint16_t kPieces = 0xB51E;         // g_pieces rec[10][18]: F 0x209c + variant: x, y, w, h, far
                                                    // picture, u8 dx, u8 dy, group, placed (§2.1)
inline constexpr size_t kPiecesEntries = 10;
inline constexpr size_t kPiecesStride = 18;
inline constexpr uint16_t kPiecesInBox = 0xB5D2;    // g_piecesInBox i16: pieces still in the box (§2.2)
inline constexpr uint16_t kPiecesCount = 0xB5D4;    // g_piecesCount i16: pieces of the variant (bundle members - 1)
                                                    // (§2.2)
inline constexpr uint16_t kPiecesGroup = 0xB5D6;    // g_piecesGroup i16: group being moved by a drop
                                                    // (PzPieces_MergeGroups, PzPieces_MoveGroup) (§2.7)
inline constexpr uint16_t kPiecesHeld = 0xB5D8;     // g_piecesHeld i16: piece carried, 0xb = none
inline constexpr uint16_t kPiecesZones = 0xB5DA;    // g_piecesZones zone[10]: piece zones, press 0x45b + i, priority
                                                    // 32000 - w*h/100
inline constexpr size_t kPiecesZonesCount = 10;
inline constexpr uint16_t kPiecesBlankCursor = 0xB6B6; // g_piecesBlankCursor far: cursor resource 4 (a 1x1 picture),
                                                       // shown while a group slides (§2.1)
inline constexpr uint16_t kPiecesHeldCursor = 0xB6BA; // g_piecesHeldCursor far: cursor record of the carried piece
inline constexpr uint16_t kPiecesSavedCursor = 0xB6BE; // g_piecesSavedCursor far: copy of g_cursor (DS:7a6a) taken at
                                                       // start and restored at exit (§2.4)
inline constexpr uint16_t kPiecesCursor = 0xB6C2;   // g_piecesCursor far: puzzle cursor (resource 3, a hand) (§2.1)

// --- Gears and buttons (kind 3, ovl22 3b2f) (code_puzzles_b.md §3) ---------------------------------------------------
inline constexpr uint16_t kGearsMinDist2 = 0x36E2;  // g_gearsMinDist2 i16[2][2]: minimum squared cell distance between
                                                    // gear centres by gear types (4, 9, 9, 16) (§3.6)
inline constexpr size_t kGearsMinDist2Count = 2;
inline constexpr size_t kGearsMinDist2Stride = 4;
inline constexpr uint16_t kGearsMesh = 0x36EA;      // g_gearsMesh i16[2][2][5][5]: 1 where two gears of types (a, b)
                                                    // mesh at cell offset (|dy|, |dx|) (§3.6)
inline constexpr size_t kGearsMeshCount = 2;
inline constexpr size_t kGearsMeshStride = 100;
inline constexpr uint16_t kGearsBoardZone = 0x37B2; // g_gearsBoardZone zone: (84,10) 226x151, press 0x44f: place the
                                                    // carried gear (§3.5)
inline constexpr uint16_t kGearsLargeZone = 0x37C8; // g_gearsLargeZone zone: circle at (53,109) radius 20, press 0x44d:
                                                    // large gear from the inventory (or put back) (§3.5)
inline constexpr uint16_t kGearsSmallZone = 0x37DE; // g_gearsSmallZone zone: circle at (52,139) radius 10, press 0x44e:
                                                    // small gear from the inventory (or put back) (§3.5)
inline constexpr uint16_t kGearsCrankRects = 0x37F4; // g_gearsCrankRects i16[26][4]: crank handle rectangle x0, y0, x1,
                                                     // y1 per crank frame + 1, relative to the crank picture (§3.3)
inline constexpr size_t kGearsCrankRectsCount = 26;
inline constexpr size_t kGearsCrankRectsStride = 8;
inline constexpr uint16_t kGearsPicX = 0x38C4;      // g_gearsPicX i16[2]: x of a gear picture at column 0, per gear
                                                    // type (86, 75) (§3.4)
inline constexpr size_t kGearsPicXCount = 2;
inline constexpr uint16_t kGearsPicY = 0x38C8;      // g_gearsPicY i16[2]: y of a gear picture at row 0, per gear type
                                                    // (31, 21) (§3.4)
inline constexpr size_t kGearsPicYCount = 2;
inline constexpr uint16_t kGearsInvX = 0x38CC;      // g_gearsInvX i16[2]: inventory gear picture x, per gear type
                                                    // (§3.4)
inline constexpr size_t kGearsInvXCount = 2;
inline constexpr uint16_t kGearsInvY = 0x38D0;      // g_gearsInvY i16[2]: inventory gear picture y, per gear type
inline constexpr size_t kGearsInvYCount = 2;
inline constexpr uint16_t kGearsRadius = 0x38D4;    // g_gearsRadius i16[2]: gear half size (hot spot, zone radius,
                                                    // centre offset), per gear type (10, 20) (§3.4)
inline constexpr size_t kGearsRadiusCount = 2;
inline constexpr uint16_t kGearsTensX = 0x38D8;     // g_gearsTensX i16[2]: inventory count: tens digit x, per gear type
                                                    // (§3.4)
inline constexpr size_t kGearsTensXCount = 2;
inline constexpr uint16_t kGearsUnitsX = 0x38DC;    // g_gearsUnitsX i16[2]: inventory count: units digit x, per gear
                                                    // type
inline constexpr size_t kGearsUnitsXCount = 2;
inline constexpr uint16_t kGearsTensY = 0x38E0;     // g_gearsTensY i16[2]: inventory count: tens digit y, per gear type
inline constexpr size_t kGearsTensYCount = 2;
inline constexpr uint16_t kGearsUnitsY = 0x38E4;    // g_gearsUnitsY i16[2]: inventory count: units digit y, per gear
                                                    // type
inline constexpr size_t kGearsUnitsYCount = 2;
inline constexpr uint16_t kGearsFrames = 0x38E8;    // g_gearsFrames i16[2]: rotation frames per gear type (6, 8) (§3.6)
inline constexpr size_t kGearsFramesCount = 2;
inline constexpr uint16_t kGearsDriveTable = 0x3902; // g_gearsDriveTable near[6][11]: propagation handler per source
                                                     // type 0..5 and target type 0..10 (0 = none); rows 6..10 are other
                                                     // data and never used (§3.8)
inline constexpr size_t kGearsDriveTableCount = 6;
inline constexpr size_t kGearsDriveTableStride = 22;
inline constexpr uint16_t kGearsHeldCursor = 0xB6C6; // g_gearsHeldCursor far: cursor record of the carried gear (the
                                                     // current cursor is compared with it) (§3.3)
inline constexpr uint16_t kGearsCursor = 0xB6CA;    // g_gearsCursor far: puzzle cursor (resource 3, a hand) (§3.1)
inline constexpr uint16_t kGearsBlankCursor = 0xB6CE; // g_gearsBlankCursor far: cursor resource 4 (1x1), shown while
                                                      // the crank is turned (§3.1)
inline constexpr uint16_t kGearsData = 0xB6D2;      // g_gearsData far: copy of F 0x2002 + variant (§3.1)
inline constexpr uint16_t kGearsRackPics = 0xB6D6;  // g_gearsRackPics far: bundle 0x2029: toothed and plain rack
                                                    // segments (§3.1)
inline constexpr uint16_t kGearsCrankPics = 0xB6DA; // g_gearsCrankPics far: bundle 0x2026: crank, 24 frames (§3.1)
inline constexpr uint16_t kGearsCrankGripPics = 0xB6DE; // g_gearsCrankGripPics far: bundle 0x2025: crank with the hand
                                                        // on the handle, 24 frames (§3.1)
inline constexpr uint16_t kGearsCrankCur = 0xB6E2;  // g_gearsCrankCur far: crank picture set in use (0x2026, or 0x2025
                                                    // while turning)
inline constexpr uint16_t kGearsButtonPics = 0xB6E6; // g_gearsButtonPics far: bundle 0x2027: buttons, 2 states x 4
                                                     // placements (§3.1)
inline constexpr uint16_t kGearsGearPics = 0xB6EA;  // g_gearsGearPics far[2]: bundles 0x2023 (small gear, 6 frames) and
                                                    // 0x2024 (large gear, 8 frames) (§3.1)
inline constexpr size_t kGearsGearPicsCount = 2;
inline constexpr uint16_t kGearsDigits = 0xB6F2;    // g_gearsDigits far: bundle 0x2028: count digits (§3.1)
inline constexpr uint16_t kGearsPanelText = 0xB6F6; // g_gearsPanelText far: text 0x202a (§3.1)
inline constexpr uint16_t kGearsHintText = 0xB6FA;  // g_gearsHintText far: text 0x202b + variant (§3.1)
inline constexpr uint16_t kGearsHintPic = 0xB6FE;   // g_gearsHintPic far: pressed Hint 0x1095 (§3.1)
inline constexpr uint16_t kGearsStock = 0xB702;     // g_gearsStock i16[2]: gears left in the inventory per type
inline constexpr size_t kGearsStockCount = 2;
inline constexpr uint16_t kGearsCrankRect = 0xB706; // g_gearsCrankRect rec[26][8]: crank handle rectangles x0, y0, x1,
                                                    // y1 on screen per crank frame + 1
inline constexpr size_t kGearsCrankRectCount = 26;
inline constexpr size_t kGearsCrankRectStride = 8;
inline constexpr uint16_t kGearsObjects = 0xB7D6;   // g_gearsObjects far: object records (20 bytes) in the F copy, +
                                                    // 0x116 (§3.1)
inline constexpr uint16_t kGearsGrid = 0xB7DA;      // g_gearsGrid far: peg grid u8[12][23] at the start of the F copy
                                                    // (§3.1)
inline constexpr uint16_t kGearsHeld = 0xB7DE;      // g_gearsHeld i16: object index of the gear carried (or last
                                                    // carried)
inline constexpr uint16_t kGearsObjectCount = 0xB7E0; // g_gearsObjectCount i16: objects of the variant
inline constexpr uint16_t kGearsJam = 0xB7E2;       // g_gearsJam i16: set by the propagation when a turn is impossible
                                                    // (§3.8)
inline constexpr uint16_t kGearsCrankCY = 0xB7E4;   // g_gearsCrankCY i16: crank centre y (§3.7)
inline constexpr uint16_t kGearsCrankCX = 0xB7E6;   // g_gearsCrankCX i16: crank centre x (§3.7)
inline constexpr uint16_t kGearsGearZones = 0xB7E8; // g_gearsGearZones far: zone[100] (Mem_Alloc 0x898): circle zone
                                                    // per placed gear, press 0x4b0 + i

// --- Balance (kind 4, ovl14 32fe) (code_puzzles_b.md §4) -------------------------------------------------------------
inline constexpr uint16_t kBalanceWeights = 0x25CC; // g_balanceWeights rec[6][18]: per weight type 0..5: mass
                                                    // (1,2,3,4,5,10), inventory picture x, y, count picture x, y,
                                                    // inventory zone x, y, w, h (PzBalance_InitZones,
                                                    // PzBalance_DrawInventory, torque) (§4.2)
inline constexpr size_t kBalanceWeightsCount = 6;
inline constexpr size_t kBalanceWeightsStride = 18;
inline constexpr uint16_t kBalanceHooks = 0x2638;   // g_balanceHooks i16[11][10][2]: hook x, y per beam tilt 0..10
                                                    // (g_balanceTilt + 5) and hook 0..9 (0..4 left, arm 5..1; 5..9
                                                    // right, arm 1..5) (§4.3)
inline constexpr size_t kBalanceHooksCount = 11;
inline constexpr size_t kBalanceHooksStride = 40;
inline constexpr uint16_t kBalanceWeightDx = 0x27F0; // g_balanceWeightDx i16[6]: x offset of a hanging weight picture
                                                     // from its hook, per weight type (§4.4)
inline constexpr size_t kBalanceWeightDxCount = 6;
inline constexpr uint16_t kBalanceInvZone = 0x27FC; // g_balanceInvZone zone: (8,67) 38x93, press 0x459: put the carried
                                                    // weight back (§4.4)
inline constexpr uint16_t kBalanceBeamZone = 0x2812; // g_balanceBeamZone zone: (53,14) 265x146, press 0x454 (no
                                                     // handler: the code is ignored) (§4.4)
inline constexpr uint16_t kBalanceTable = 0xAB28;   // g_balanceTable far: copy of D 0x211b (variant table) (§4.1)
inline constexpr uint16_t kBalanceStackPic = 0xAB2C; // g_balanceStackPic far: 0x211d: hook drawn under the last of 1..2
                                                     // left weights (§4.1)
inline constexpr uint16_t kBalanceRingPic = 0xAB30; // g_balanceRingPic far: 0x211c: ring drawn above every hanging
                                                    // weight (§4.1)
inline constexpr uint16_t kBalanceCarryPics = 0xAB34; // g_balanceCarryPics far: bundle 0x2129: weights with their hook
                                                      // (inventory, carried cursor) (§4.1)
inline constexpr uint16_t kBalanceRightPics = 0xAB38; // g_balanceRightPics far: bundle 0x212b: hanging weights, right
                                                      // side
inline constexpr uint16_t kBalanceLeftPics = 0xAB3C; // g_balanceLeftPics far: bundle 0x212a: hanging weights, left side
                                                     // (§4.1)
inline constexpr uint16_t kBalanceCountPics = 0xAB40; // g_balanceCountPics far: bundle 0x212c: inventory count pictures
                                                      // 0..8 (§4.1)
inline constexpr uint16_t kBalancePanelText = 0xAB44; // g_balancePanelText far: text 0x214d (§4.1)
inline constexpr uint16_t kBalanceHintText = 0xAB48; // g_balanceHintText far: text 0x212d + variant (§4.1)
inline constexpr uint16_t kBalanceHintPic = 0xAB4C; // g_balanceHintPic far: pressed Hint 0x1095 (§4.1)
inline constexpr uint16_t kBalanceSolved = 0xAB50;  // g_balanceSolved i16: 1 once the beam came to level (return value)
inline constexpr uint16_t kBalanceHeldCursor = 0xAB52; // g_balanceHeldCursor far: cursor record of the carried weight
inline constexpr uint16_t kBalanceCursor = 0xAB56;  // g_balanceCursor far: puzzle cursor (resource 3, a hand) (§4.1)
inline constexpr uint16_t kBalanceHookZones = 0xAB5A; // g_balanceHookZones zone[3][5]: left hook zones per resting tilt
                                                      // (-5, +5, level): press 0x461 + i, 0x466 + i, 0x46b + i
inline constexpr size_t kBalanceHookZonesCount = 3;
inline constexpr size_t kBalanceHookZonesStride = 110;
inline constexpr uint16_t kBalanceInvZones = 0xACA4; // g_balanceInvZones zone[6]: inventory weight zones, press 0x45a +
                                                     // type
inline constexpr size_t kBalanceInvZonesCount = 6;
inline constexpr uint16_t kBalanceHookWeights = 0xAD28; // g_balanceHookWeights i16[10][3]: weight types on each hook,
                                                        // top first, -1 = empty
inline constexpr size_t kBalanceHookWeightsCount = 10;
inline constexpr size_t kBalanceHookWeightsStride = 6;
inline constexpr uint16_t kBalanceStock = 0xAD64;   // g_balanceStock i16[6]: weights left in the inventory per type
                                                    // (§4.2)
inline constexpr size_t kBalanceStockCount = 6;

// --- Magnets (kind 7, ovl16 35bb) (code_puzzles_b.md §5) -------------------------------------------------------------
inline constexpr uint16_t kMagnetsInvZone = 0x298A; // g_magnetsInvZone zone: (8,47) 58x75, press 0x459: take a magnet
                                                    // or put the carried one back (§5.5)
inline constexpr uint16_t kMagnetsTableZone = 0x29A0; // g_magnetsTableZone zone: (70,23) 239x128, press 0x456: pick up
                                                      // or drop a magnet
inline constexpr uint16_t kMagnetsInvPos = 0x29E2;  // g_magnetsInvPos i16[3][2]: inventory picture x, y per magnet kind
                                                    // (bar, horseshoe N-S, horseshoe S-N) (§5.5)
inline constexpr size_t kMagnetsInvPosCount = 3;
inline constexpr size_t kMagnetsInvPosStride = 4;
inline constexpr uint16_t kMagnetsInvCountPos = 0x29EE; // g_magnetsInvCountPos i16[3][2]: inventory count x, y per
                                                        // magnet kind (§5.5)
inline constexpr size_t kMagnetsInvCountPosCount = 3;
inline constexpr size_t kMagnetsInvCountPosStride = 4;
inline constexpr uint16_t kMagnetsStock = 0x2A8A;   // g_magnetsStock s8[30][3]: magnets in the inventory per level and
                                                    // kind (§5.4)
inline constexpr size_t kMagnetsStockCount = 30;
inline constexpr size_t kMagnetsStockStride = 3;
inline constexpr uint16_t kMagnetsLevelOfVariant = 0x2AE4; // g_magnetsLevelOfVariant u8[30]: level + 1 per puzzle
                                                           // variant (a permutation of 1..30) (§5.3)
inline constexpr size_t kMagnetsLevelOfVariantCount = 30;
inline constexpr uint16_t kMagnetsTable = 0xAF60;   // g_magnetsTable far: copy of D 0x11a1 (§5.1)
inline constexpr uint16_t kMagnetsOutlinePics = 0xAF64; // g_magnetsOutlinePics far: bundle 0x11a6: target outlines
                                                        // (§5.1)
inline constexpr uint16_t kMagnetsShadowPics = 0xAF68; // g_magnetsShadowPics far: bundle 0x11a5: magnet shadows (drawn
                                                       // at +2,+2) (§5.1)
inline constexpr uint16_t kMagnetsPics = 0xAF6C;    // g_magnetsPics far: bundle 0x11a4: magnets, kind*4 + orientation
                                                    // (§5.1)
inline constexpr uint16_t kMagnetsDigits = 0xAF70;  // g_magnetsDigits far: bundle 0x11a3: count digits (§5.1)
inline constexpr uint16_t kMagnetsRotatePics = 0xAF74; // g_magnetsRotatePics far: bundle 0x11a2: ROTATE button normal,
                                                       // pressed (§5.1)
inline constexpr uint16_t kMagnetsPanelText = 0xAF78; // g_magnetsPanelText far: text 0x11c5 (§5.1)
inline constexpr uint16_t kMagnetsHintText = 0xAF7C; // g_magnetsHintText far: text 0x11a7 + level (§5.1)
inline constexpr uint16_t kMagnetsHintPic = 0xAF80; // g_magnetsHintPic far: pressed Hint 0x1095 (§5.1)
inline constexpr uint16_t kMagnetsHeldCursor = 0xAF84; // g_magnetsHeldCursor far: cursor record of the carried magnet
inline constexpr uint16_t kMagnetsCursor = 0xAF88;  // g_magnetsCursor far: puzzle cursor (resource 3, a hand) (§5.1)
inline constexpr uint16_t kLaunchObjectPic = 0xAF8C; // g_launchObjectPic far: object sprite picture
                                                     // (PzLaunch_SetObjectSprite)

// --- Porting notes (code_puzzles_b.md §9) ----------------------------------------------------------------------------
inline constexpr uint16_t kMagnetsPoles = 0x29FA;   // g_magnetsPoles s8[12][6][2]: pole offsets dx, dy per picture
                                                    // kind*4+orientation: poles 0..2 one polarity, 3..5 the other;
                                                    // (-1,-1) = no pole
inline constexpr size_t kMagnetsPolesCount = 12;
inline constexpr size_t kMagnetsPolesStride = 12;

// --- Method 3 compression (fmt_data.md §3) ---------------------------------------------------------------------------
inline constexpr uint16_t kLzhDefaultDistLengths = 0x13D0; // g_lzhDefaultDistLengths u8[2][32]: default distance code
                                                           // lengths of the LZH codec (12- and 13-bit windows) (§3.4);
                                                           // read by 1b48:021f as 3 rows of 16 u16 words, row W - 12
                                                           // (the third row is unreachable): LzhDistanceRows
inline constexpr size_t kLzhDefaultDistLengthsCount = 3;
inline constexpr size_t kLzhDefaultDistLengthsStride = 32;

// --- DAM animation scripts (D 0x114b..0x1156) (fmt_graphics.md §6) ---------------------------------------------------
inline constexpr uint16_t kAnimRectCount = 0x398A;  // g_animRectCount i16: entries in the DAM update-rect list DS:b86e
                                                    // (§6.1)
inline constexpr uint16_t kAnimRects = 0xB86E;      // g_animRects rec[][8]: DAM update rectangles (§6.1)
inline constexpr uint16_t kAnimCallbacks = 0xBA1E;  // g_animCallbacks far[]: DAM opcode 0x7FF8 callbacks (§6.1)

// --- Sound banks and the speaker/FM pairing (fmt_sound.md §2) --------------------------------------------------------
inline constexpr uint16_t kFmPresent = 0xA0D8;      // g_fmPresent i16: 1 = FM sound banks (id+1)

// --- The driver at format level (fmt_sound.md §4) --------------------------------------------------------------------
inline constexpr uint16_t kSndClassAvailable = 0x1681; // g_sndClassAvailable u8[5]: song class available (1, 1, 0, 0,
                                                       // 0; [3] = DS:1684 set by the OPL detection)
inline constexpr size_t kSndClassAvailableCount = 5;

// --- The executable at a glance (ARCHITECTURE.md §1) -----------------------------------------------------------------
inline constexpr uint16_t kStrCopyright = 0x0004;   // s_copyright char[]: Borland C++ copyright string (identifies the
                                                    // compiler) (§1.1)
inline constexpr uint16_t kStrDivideError = 0x002F; // s_divideError char[14]: Borland RTL divide-error message + CR LF
                                                    // (no NUL; the handler prints 14 bytes)
inline constexpr size_t kStrDivideErrorSize = 14;
inline constexpr uint16_t kStrAbnormalTermination = 0x003D; // s_abnormalTermination char[]: abort's message

// --- Game flow (ARCHITECTURE.md §2) ----------------------------------------------------------------------------------
inline constexpr uint16_t kStrMorty = 0x243C;       // s_morty char[]: race result name of the opponent (§2.10)
inline constexpr uint16_t kRematch = 0x455E;        // g_rematch i16: 1 = rematch of the last race (§2.10)
inline constexpr uint16_t kRaceIndex = 0x4590;      // g_raceIndex i16: building*5 + level (0..14) (§2.5)
inline constexpr uint16_t kRacesWon = 0x4596;       // g_racesWon i16[3]: races won per building (-1..4) (§2.4)
inline constexpr size_t kRacesWonCount = 3;

// --- Platform layer (ARCHITECTURE.md §4) -----------------------------------------------------------------------------
inline constexpr uint16_t kCursorHotX = 0x7A74;     // g_cursorHotX i16: active cursor hot spot x (added to click
                                                    // positions) (§4.3)

// --- The TLC UI library (ARCHITECTURE.md §5) -------------------------------------------------------------------------
inline constexpr uint16_t kPagerOneButtons = 0x3DFE; // g_pagerOneButtons far[1]: pager, single page: {Done} (§5.6)
inline constexpr size_t kPagerOneButtonsCount = 1;
inline constexpr uint16_t kPagerButtons = 0x3E02;   // g_pagerButtons far[1]: pager, first page of several: {See Next
                                                    // Page}
inline constexpr size_t kPagerButtonsCount = 1;
inline constexpr uint16_t kPagerMidButtons = 0x3E06; // g_pagerMidButtons far[2]: pager, middle page: {Go Back a Page,
                                                     // See Next Page}
inline constexpr size_t kPagerMidButtonsCount = 2;
inline constexpr uint16_t kPagerLastButtons = 0x3E0E; // g_pagerLastButtons far[2]: pager, last page: {Go Back a Page,
                                                      // Done}
inline constexpr size_t kPagerLastButtonsCount = 2;
inline constexpr uint16_t kAnimChannels = 0xBA66;   // g_animChannels rec[2][0x22]: the two DAM channels (§5.3);
                                                    // DS:ba80 is channel 0's +0x1A far cursor (3d39:03d6)
inline constexpr size_t kAnimChannelsCount = 2;
inline constexpr size_t kAnimChannelsStride = 0x22;

// --- Clocks and randomness (PORTING_NOTES.md §4) ---------------------------------------------------------------------
inline constexpr uint16_t kRandState = 0x442A;      // g_randState u32: rand() LCG state

// --- Tables the docs use, labelled in re/names/gg/dgroup.tsv at the wave 2 integration -----------------------
// (kSpkDivisorTable is g_spkDivisors DS:1816 as the speaker code indexes it, from semitone -2.)
inline constexpr uint16_t kProjectileTransitions = 0x1276; // near target: the projectile's transition index (actor
                                                           // +0x90, written only) (code_world.md §9.1)
inline constexpr uint16_t kHudColorScheme = 0x1318;   // i16: the HUD digit colour scheme in effect; schemes are
                                                      // the triples of g_hudDigitColors DS:131a (code_vehicles_races.md §4)
inline constexpr uint16_t kChimpListData = 0x1ADC;    // u16 lists DS:1adc..1bad that g_chimpLists DS:1bae points
                                                      // into: per race the chimp count, then per chimp its room
                                                      // (the 286 path halves the first room word; code_world.md §13.1,
                                                      // PORTING_NOTES.md §5)
inline constexpr uint16_t kChimpListDataEnd = 0x1BAE; // end of the lists (= g_chimpLists)
inline constexpr uint16_t kSpkDivisorTable = 0x1812;  // u16: the speaker divisor table as indexed, semitones -2..13
                                                      // of g_spkDivisors DS:1816 (DS:1812..1831) (code_sound.md §7)
inline constexpr uint16_t kAttractScripts = 0x2008;   // u16 lists: attract channel scripts (n > 0 animation, -2 t
                                                      // wait, -1 end) (code_ui.md §10.2, §18)
inline constexpr uint16_t kAttractChannels = 0x205C;  // far tables of far channel scripts (code_ui.md §10.2)
inline constexpr uint16_t kAttractAnims = 0x2090;     // {u16 channel count, far channel table}: the four attract
                                                      // animations DS:2090, 2096, 209c, 20a2 (code_ui.md §10.3)
inline constexpr size_t kAttractAnimsCount = 4;
inline constexpr size_t kAttractAnimsStride = 6;
inline constexpr uint16_t kAnimCelCount = 0x3988;     // i16: queued DAM cels in DS:b87e (fmt_graphics.md §6.3)
inline constexpr uint16_t kAnimCels = 0xB87E;         // rec[][16]: queued DAM cels {layer, x, y, flags, far picture,
                                                      // channel flags} (fmt_graphics.md §6.3)
inline constexpr size_t kAnimCelsStride = 16;
inline constexpr uint16_t kAnimFrames = 0xBA26;       // far[]: the DAM frame table built from a frame bundle by
                                                      // 3797:100e (fmt_graphics.md §6.2)

}  // namespace tmport::gg::dg
