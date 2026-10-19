#pragma once
// Named DGROUP offsets (DS:XXXX = Ghidra 4d94:XXXX) of SST.EXE V 1.0, the 1990 DOS edition: every
// table the engine reads from the user's EXE and every run-time global of the original, with the
// canonical name of re/names/dos/dgroup.tsv (g_cellPicture -> kCellPicture, s_btnDone ->
// kStrBtnDone) and the doc section. The data itself is always read from the user's file through
// DosExe (dos_exe.h); the run-time globals are listed for state dumps and for comparing the engine
// with DOSBox-X save states (`savmem.py peek`), their values are not in the file (BSS is zero).
// Types: u8/i8/u16/i16/u32, far = {u16 off, u16 seg} with an image-relative segment (seg 0x3d94 is
// DGROUP, DosExe::readFarPtr resolves it), near = u16 offset in the owning segment.
//
// Docs (re/dos/docs/): ARCHITECTURE.md (overview), fmt_data.md §7 (DGROUP map), the topic docs
// cited per section. Keep the names in step with re/names/dos/dgroup.tsv (a new table gets its row
// there first).

#include <cstddef>
#include <cstdint>

namespace tmport::dos::dg {

// --- Segment facts (fmt_data.md §7.1) ------------------------------------------------------------
inline constexpr uint16_t kInitSize = 0x8FA0;   // DS:0000..8F9F are initialised in the file
inline constexpr uint16_t kBssStart = 0x8FA0;   // DS:8FA0..CC1F are zero at start (BSS)
inline constexpr uint16_t kBssEnd = 0xCC20;     // the stack segment starts here (DGROUP + 0xCC2 paragraphs)

// --- Validation and the Turbo C run-time (fmt_data.md §7.1, PORTING_NOTES.md §2.4) -------------------
inline constexpr uint16_t kStrTurboCCopyright = 0x0004; // s_turboCCopyright char[]: Turbo C 2.0 copyright string,
                                                        // checked by DosExe::load
inline constexpr uint16_t kRandSeed = 0x8F48;       // g_randSeed u32: Turbo C rand() seed, 1 in the file (rand
                                                    // 4d12:001f, srand only in Layout_Generate)

// --- Main loop, HUD and rank chest (130f) (code_runtime.md §7, code_ui.md §11, code_player.md §7.4) ---
inline constexpr uint16_t kChampFlashTimer = 0x0100; // g_champFlashTimer u16: CHAMPION flash countdown (set to 800 by
                                                     // RankChest_Update)
inline constexpr uint16_t kBoxSaved = 0x0102;       // g_boxSaved u16: slow CPU: MsgBox_Draw saved the background
                                                    // under a box
inline constexpr uint16_t kScreenPainters = 0x0104; // g_screenPainters far[3]: painter by g_screen: Hud_Redraw,
                                                    // Quiz_DrawScreen, SignIn_Draw
inline constexpr size_t kScreenPaintersCount = 3;
inline constexpr uint16_t kRankTable = 0x0110;      // g_rankTable i16[21]: (threshold, chest rows) pairs, -1
                                                    // terminated; the row words double as the star-row counts
                                                    // (DS:0116..012e)
inline constexpr size_t kRankTablePairs = 10;       // pairs before the -1
inline constexpr uint16_t kStrChampion = 0x013A;    // s_CHAMPION char[]: the champion banner text (RankChest_Update)

// --- Palettes and font metrics (fmt_graphics.md §2.2, §6.1) ------------------------------------------
inline constexpr uint16_t kPalMcga = 0x0144;        // g_palMcga u8[16][3]: MCGA DAC colours (6-bit r, g, b), applied
                                                    // after the mode set
inline constexpr uint16_t kPalCga = 0x0174;         // g_palCga u8[16]: CGA 2x2 dither pattern per logical colour
inline constexpr uint16_t kPalMcgaModeSet = 0x3C1A; // g_palMcgaModeSet u8[16][3]: MCGA colours loaded by the mode set
                                                    // (Video_SetModeMCGA), replaced by g_palMcga
inline constexpr uint16_t kPalEga = 0x3C4A;         // g_palEga u8[17]: EGA attribute registers (RGBI) + overscan byte
inline constexpr uint16_t kFontRowBytes = 0x3C5B;   // g_fontRowBytes u16[]: bytes per glyph row, indexed by glyph
                                                    // width (Font_DrawChar)
inline constexpr uint16_t kPalTandy = 0x3D1B;       // g_palTandy u8[16]: Tandy palette registers (IRGB)

// --- Data files and start-up (1460) (fmt_data.md §1, code_runtime.md §4, §7) -------------------------
inline constexpr uint16_t kPathDisk1 = 0x036E;      // g_pathDisk1 char[0x5b]: directory of the disk-1 files
                                                    // (Path_InitDrives)
inline constexpr uint16_t kPathDisk2 = 0x03C9;      // g_pathDisk2 char[0x5b]: directory of the disk-2 files
inline constexpr uint16_t kFileTable = 0x0476;      // g_fileTable rec22[19]: 18 data files (name[13], disk u8, 8
                                                    // unused bytes) and an all-zero record
inline constexpr size_t kFileTableCount = 18;       // used records; record 18 is all zero
inline constexpr size_t kFileTableStride = 22;
inline constexpr uint16_t kStrSoundSwitchChars = 0x0620; // s_soundSwitchChars char[]: letters after -S
                                                         // (ParseCommandLine)
inline constexpr uint16_t kSoundSwitchMap = 0x0629; // g_soundSwitchMap u8[8]: sound device per letter of
                                                    // s_soundSwitchChars
inline constexpr uint16_t kDiskPathPtrs = 0x0631;   // g_diskPathPtrs far[3]: {null, g_pathDisk1, g_pathDisk2} by disk
                                                    // number (File_OpenByIndex)

// --- Hiding-place pictures and clue attributes (1a54, 46ce) (fmt_data.md §7.4, code_elves_layout_hints.md §5.1) ---
inline constexpr uint16_t kPictureAttrL0 = 0x0184;  // g_pictureAttrL0 u8[51][4]: level 0: 4-byte attribute record per
                                                    // hiding-place picture (record 0 unused)
inline constexpr size_t kPictureAttrL0Count = 51;
inline constexpr uint16_t kPictureAttrL1 = 0x0250;  // g_pictureAttrL1 u8[39][4]: level 1
inline constexpr size_t kPictureAttrL1Count = 39;
inline constexpr uint16_t kPictureAttrL2 = 0x02EC;  // g_pictureAttrL2 u8[32][4]: level 2
inline constexpr size_t kPictureAttrL2Count = 32;
inline constexpr uint16_t kPictureAttrTables = 0x1246; // g_pictureAttrTables far[3]: far pointers to the three tables
                                                       // above, by level

// --- World grid and cell handlers (1568, 176a) (code_world.md §3.1, §3.5, §12.2) ---------------------
inline constexpr uint16_t kCellPlayerHandlers = 0x08A6; // g_cellPlayerHandlers far[75]: object handler per cell type
                                                        // (Cell_GetMask)
inline constexpr size_t kCellPlayerHandlersCount = 75;
inline constexpr uint16_t kCellVisibleHandlers = 0x09D2; // g_cellVisibleHandlers far[74]: visible handler per cell
                                                         // type (Cells_UpdateVisible)
inline constexpr size_t kCellVisibleHandlersCount = 74;
inline constexpr uint16_t kCreatureMask = 0x0AFA;   // g_creatureMask u16[4]: castle creature rand() mask per tier
                                                    // (Castle_CreatureSpawn)
inline constexpr uint16_t kCellPicture = 0x0CAA;    // g_cellPicture u16[256]: scenery block per cell (low 7 bits),
                                                    // index row*16 + col
inline constexpr size_t kCellPictureCount = 256;
inline constexpr uint16_t kCellType = 0x0EAA;       // g_cellType u8[256]: cell type 0..73 per cell (signed char)
inline constexpr uint16_t kCellContent = 0x0FAA;    // g_cellContent u16[256]: hidden content per cell (0x80 empty,
                                                    // 6+2k treasure, 0x66/68/6a key; bit 7 searched)
inline constexpr uint16_t kHidingCells = 0x11AA;    // g_hidingCells u16[48]: cells of the hiding places: slots 0-10
                                                    // level 2, 13-26 level 1, 29-47 level 0; 11, 12, 27, 28 mirrors
inline constexpr size_t kHidingCellsCount = 48;
inline constexpr uint16_t kVariableCells = 0x120A;  // g_variableCells u16[9]: cells whose picture changes in play and
                                                    // is saved (bonus spot, gates, bridges, net shop)
inline constexpr size_t kVariableCellsCount = 9;
inline constexpr uint16_t kVariableCellDefaults = 0x121C; // g_variableCellDefaults u16[9]: default picture of each
                                                          // g_variableCells entry
inline constexpr uint16_t kLevelPicData = 0x122E;   // g_levelPicData u16[3][4]: per level {picture count, key
                                                    // picture, unused, places without the key}
inline constexpr size_t kLevelPicDataStride = 8;
inline constexpr uint16_t kCastleLayouts = 0x1252;  // g_castleLayouts u16[4][20]: castle ladder layouts: 5 rows x 4
                                                    // cells (Castle_ApplyLayout)
inline constexpr size_t kCastleLayoutsCount = 4;
inline constexpr size_t kCastleLayoutsStride = 40;
inline constexpr uint16_t kShelfRoomRedraw = 0x7D7C; // g_shelfRoomRedraw u8[4][2]: per treasure shelf room {draw its
                                                     // shelves, first collected-list index}

// --- Player, end sequence and treasure room (18cf, 176a) (code_player.md §4.1, §6.3, §7.1, §10) ------
inline constexpr uint16_t kShackSavedShown = 0x00F6; // g_shackSavedShown i16: treasure room: shown treasure saved
                                                     // during a fly display (initial 0xffff)
inline constexpr uint16_t kShackPartner1 = 0x00F8;  // g_shackPartner1 i16: treasure room: partner treasure in slot 8
                                                    // (initial 0xffff)
inline constexpr uint16_t kShackPartner2 = 0x00FA;  // g_shackPartner2 i8: treasure room: partner treasure in slot 10
                                                    // (initial 0xff)
inline constexpr uint16_t kShackState = 0x00FB;     // g_shackState u8: treasure room: bit 7 display started, low bits
                                                    // carousel running
inline constexpr uint16_t kShackPartner1X = 0x00FC; // g_shackPartner1X u16: treasure room: x of partner 1 (initial
                                                    // 0x50)
inline constexpr uint16_t kShackPartner2X = 0x00FE; // g_shackPartner2X u16: treasure room: x of partner 2 (initial
                                                    // 0x50)
inline constexpr uint16_t kPlayerSlotPtr = 0x0B02;  // g_playerSlotPtr far: far pointer to sprite slot 1 (DS:8fec)
inline constexpr uint16_t kEndFlightPathY = 0x0B06; // g_endFlightPathY i16[]: treasure flight y per step, -1
                                                    // terminated (26 steps)
inline constexpr uint16_t kEndShowcasePathY = 0x0B3C; // g_endShowcasePathY i16[]: showcase flight y per step, -1
                                                      // terminated (16 steps)
inline constexpr uint16_t kEndRankSparkleX = 0x0B5E; // g_endRankSparkleX i16[6]: rank sparkle x
inline constexpr uint16_t kEndRankSparkleY = 0x0B6A; // g_endRankSparkleY i16[6]: rank sparkle y
inline constexpr uint16_t kEndChampSparkleX = 0x0B76; // g_endChampSparkleX i16[5]: champion sparkle x
inline constexpr uint16_t kEndChampSparkleY = 0x0B80; // g_endChampSparkleY i16[5]: champion sparkle y
inline constexpr uint16_t kPlayerStateHandlers = 0x0B8A; // g_playerStateHandlers far[72]: player state handlers by
                                                         // g_playerState
inline constexpr size_t kPlayerStateHandlersCount = 72;

// --- Animation scripts (4306) (code_player.md §3.1, §10) ---------------------------------------------
inline constexpr uint16_t kAnimScriptData = 0x3D36; // g_animScriptData i16[]: the script words (delay, slot count,
                                                    // entries, -10 end)
inline constexpr uint16_t kAnimScript88Frame = 0x48DE; // g_animScript88Frame i16: frame word of script 0x88, patched
                                                       // at run time (treasure flight)
inline constexpr uint16_t kAnimScripts = 0x4F5A;    // g_animScripts far[194]: far pointer per script id; ids 0..0xbe
                                                    // are scripts, 0xbf..0xc1 point into text
inline constexpr size_t kAnimScriptsCount = 194;    // pointers in the table
inline constexpr size_t kAnimScriptsValid = 191;    // script ids 0..0xbe

// --- Menus, boxes and help (43cb) (code_ui.md §4, §5, §6, §14) ---------------------------------------
inline constexpr uint16_t kBoxFooterText = 0x5256;  // g_boxFooterText far[3]: footer strings of the boxes
inline constexpr uint16_t kStrMenuHelpItems = 0x5262; // s_menuHelpItems char[]: Help drop-down items (CR-separated)
inline constexpr uint16_t kStrMenuFileItems = 0x52A7; // s_menuFileItems char[]: File drop-down items
inline constexpr uint16_t kStrMenuOptionsItems = 0x52D1; // s_menuOptionsItems char[]: Options drop-down items
inline constexpr uint16_t kHelpTopicTitles = 0x52EE; // g_helpTopicTitles far[4]: help topic titles
inline constexpr size_t kHelpTopicTitlesCount = 4;
inline constexpr uint16_t kMsgBoxes = 0x59CC;       // g_msgBoxes rec16[23]: message boxes {type, pad, x1, y1, default
                                                    // button, pad, far title, far text}
inline constexpr size_t kMsgBoxesCount = 23;
inline constexpr size_t kMsgBoxesStride = 16;
inline constexpr uint16_t kLineHeight = 0x5B3C;     // g_lineHeight u16: Text_Draw line height (12; 10 on the title
                                                    // and help pages)
inline constexpr uint16_t kDialogShown = 0x5B3E;    // g_dialogShown u16: dialog or help box already drawn
inline constexpr uint16_t kMenuStepH = 0x5B40;      // g_menuStepH i8[17]: drop-down change by command (left/right)
inline constexpr uint16_t kMenuStepV = 0x5B51;      // g_menuStepV i8[18]: item change by command (up/down)
inline constexpr uint16_t kMenuWrap = 0x5B63;       // g_menuWrap i8[]: drop-down index wrap: element 0 here, indexed
                                                    // -2..5 (elements -2 and -1 are the last two bytes of
                                                    // g_menuStepV)
inline constexpr uint16_t kMenuFirstDialog = 0x5B69; // g_menuFirstDialog u16[3]: first dialog id per drop-down {0, 8,
                                                     // 11}
inline constexpr uint16_t kMenuHandlers = 0x5B6F;   // g_menuHandlers far[14]: unused copy of the confirm handlers
                                                    // 0..13
inline constexpr uint16_t kDialogBox = 0x5BA7;      // g_dialogBox i16[14]: box per dialog id (-1 = help page)
inline constexpr uint16_t kHelpPageStep = 0x5BC3;   // g_helpPageStep i8[2]: help page step by button {-1, +1}
inline constexpr uint16_t kSoundChanged = 0x5BC5;   // g_soundChanged u16: music/sound option changed: Sound_SyncState
                                                    // restarts or stops the music
inline constexpr uint16_t kMenuTitleX0 = 0x5BC7;    // g_menuTitleX0 u16[3]: menu title left x (bytes)
inline constexpr uint16_t kMenuTitleX1 = 0x5BCD;    // g_menuTitleX1 u16[3]: menu title right x
inline constexpr uint16_t kMenuTitleGlyph1 = 0x5BD3; // g_menuTitleGlyph1 u16[6]: first glyph of each menu title: 3
                                                     // normal, 3 highlighted
inline constexpr uint16_t kMenuTitleGlyph2 = 0x5BDF; // g_menuTitleGlyph2 u16[6]: second glyph (the key cap): 3
                                                     // normal, 3 highlighted
inline constexpr uint16_t kHelpLoaded = 0x5BEB;     // g_helpLoaded u16: HELP.DAT loaded
inline constexpr uint16_t kHelpPicFrame1 = 0x5BED;  // g_helpPicFrame1 i16[15]: help page: first picture (frame*2, -1
                                                    // none)
inline constexpr size_t kHelpPicFrame1Count = 15;
inline constexpr uint16_t kHelpPicFrame2 = 0x5C0B;  // g_helpPicFrame2 i16[15]: help page: second picture
inline constexpr uint16_t kHelpPicX1 = 0x5C29;      // g_helpPicX1 i16[15]: help page: x of the first picture
inline constexpr uint16_t kHelpPicX2 = 0x5C47;      // g_helpPicX2 i16[15]: help page: x of the second picture
inline constexpr uint16_t kHelpPicY = 0x5C65;       // g_helpPicY i16[15]: help page: y of both pictures
inline constexpr uint16_t kMenuZoneCounts = 0x5C83; // g_menuZoneCounts u8[3]: mouse zones per drop-down
inline constexpr uint16_t kButtonZoneWidths = 0x5C86; // g_buttonZoneWidths u8[3]: button zone width per box type
inline constexpr uint16_t kFooterZoneX = 0x5C89;    // g_footerZoneX u8[8]: footer zone x offset per box type
inline constexpr uint16_t kStrBtnNo = 0x5D00;       // s_btnNo char[]: button label (box type 1, button 0)
inline constexpr uint16_t kStrBtnYes = 0x5D03;      // s_btnYes char[]: button label (box type 1, button 1)
inline constexpr uint16_t kStrBox5GlyphLeft = 0x5D07; // s_box5GlyphLeft char[]: type-5 box corner glyph string
inline constexpr uint16_t kStrBox5GlyphRight = 0x5D09; // s_box5GlyphRight char[]: type-5 box corner glyph string
inline constexpr uint16_t kStrBtnDone = 0x5D0B;     // s_btnDone char[]: help button on the last page
inline constexpr uint16_t kStrBtnNextPage = 0x5D16; // s_btnNextPage char[]: help button
inline constexpr uint16_t kStrBtnPrevPage = 0x5D24; // s_btnPrevPage char[]: help button from page 1 on

// --- Elves (4521) (code_elves_layout_hints.md §3.3, §3.4) --------------------------------------------
inline constexpr uint16_t kElfCounts = 0x5D34;      // g_elfCounts u16[4][4]: elves per tier, columns A
                                                    // walker/carrier, B thrower, C walker, D coin creature
inline constexpr size_t kElfCountsStride = 8;       // bytes per tier
inline constexpr uint16_t kLevelXRange = 0x5D54;    // g_levelXRange u16[3][4]: elf placement per level {x base, y, x
                                                    // mask, unused}
inline constexpr size_t kLevelXRangeStride = 8;
inline constexpr uint16_t kElfStateHandlers = 0x5D6C; // g_elfStateHandlers far[76]: elf state handlers by state
inline constexpr size_t kElfStateHandlersCount = 76;

// --- Elf questions and clue words (46ce) (code_ui.md §10, fmt_data.md §2, §7.4) ----------------------
inline constexpr uint16_t kAnswerPerms = 0x5E9C;    // g_answerPerms u8[6][3]: the 6 answer display orders
inline constexpr uint16_t kHistoryMasks = 0x5EAE;   // g_historyMasks u8[4]: 2-bit history mask by question index % 4
inline constexpr uint16_t kClueWordText = 0x5EB2;   // g_clueWordText far[53]: clue word strings (number, digit,
                                                    // shape, object, plural, size words)
inline constexpr size_t kClueWordTextCount = 53;
inline constexpr uint16_t kQuizFocusStep = 0x5F86;  // g_quizFocusStep i16[2]: focus step for up/down {-1, +1}
inline constexpr uint16_t kAnswerBit = 0x5F8A;      // g_answerBit u8[3]: choice-mask bit by answer index
                                                    // (Quiz_Update)
inline constexpr uint16_t kAnswerBit2 = 0x5F8D;     // g_answerBit2 u8[3]: identical copy (Quiz_SetupAnswers)
inline constexpr uint16_t kAnswerBit3 = 0x5F90;     // g_answerBit3 u8[3]: identical copy (Quiz_DrawAnswers)
inline constexpr uint16_t kQuizReward = 0x5F93;     // g_quizReward u8[8]: coins by the choice mask after a right
                                                    // answer (6 -> 2, 4/2 -> 1)
inline constexpr uint16_t kClueAttrMask = 0x5F9B;   // g_clueAttrMask u8[3]: attribute masks M of attributes 0..2
inline constexpr uint16_t kClueSizeMask = 0x5F9E;   // g_clueSizeMask u8[4]: size field masks {0xc0, 0x30, 0x0c, 0x03}
inline constexpr uint16_t kClueAttrShift = 0x5FA2;  // g_clueAttrShift u8[3]: attribute shifts S of attributes 0..2
inline constexpr uint16_t kClueSizeShift = 0x5FA5;  // g_clueSizeShift u8[4]: size field shifts {6, 4, 2, 0}
inline constexpr uint16_t kClueAttrByte = 0x5FA9;   // g_clueAttrByte u8[5]: byte of the attribute record per
                                                    // attribute 0..4
inline constexpr uint16_t kClueWordBase = 0x5FAE;   // g_clueWordBase u8[5]: first word-table index per attribute
inline constexpr uint16_t kClueSchemes = 0x5FB3;    // g_clueSchemes u8[3][3]: attributes described by each clue
                                                    // scheme
inline constexpr uint16_t kClueRevealOrders = 0x5FBC; // g_clueRevealOrders u8[6][3]: reveal orders (Clue_Reveal)
inline constexpr uint16_t kQuizTierStart = 0x5FD2;  // g_quizTierStart u16[7]: first question per tier {0, 63, 180,
                                                    // 303, 360, X, 360}; words 5 and 6 are written at load
inline constexpr size_t kQuizTierStartCount = 7;
inline constexpr uint16_t kQuizTier5Start = 0x5FDC; // g_quizTier5Start u16: X: first question of tier 5
                                                    // (Quiz_FindTierEnd)
inline constexpr uint16_t kDealTemplate = 0x5FE0;   // g_dealTemplate u8[9]: wanted history status per dealt slot
                                                    // {0,3,2, 0,3,2, 0,3,2}
inline constexpr uint16_t kDealFallback = 0x5FE9;   // g_dealFallback u8[3]: fallback statuses
inline constexpr uint16_t kDealFallbackIndex = 0x5FEC; // g_dealFallbackIndex u8[4]: index into g_dealFallback by
                                                       // status
inline constexpr uint16_t kHistoryResetMasks = 0x5FF0; // g_historyResetMasks u8[4]: masks of Quiz_ResetHistory by
                                                       // start % 4
inline constexpr uint16_t kStrClueWords = 0x5FF4;   // s_clueWords char[]: first of the clue word strings (through
                                                    // DS:6131)

// --- Sign-in and USERS.DAT (47c7) (code_ui.md §9, fmt_data.md §4) ------------------------------------
inline constexpr uint16_t kUserRecOffsets = 0x6132; // g_userRecOffsets u16[9]: record offsets in USERS.DAT (3 +
                                                    // 360*i)
inline constexpr size_t kUserRecOffsetsCount = 9;
inline constexpr uint16_t kUserNamePtrs = 0x6144;   // g_userNamePtrs far[9]: far pointers to the record names in the
                                                    // USERS.DAT image
inline constexpr uint16_t kSigninInit = 0x6168;     // g_signinInit u8: 1 = reinitialise the sign-in screen on the
                                                    // next frame
inline constexpr uint16_t kUsersLoaded = 0x6169;    // g_usersLoaded u8: USERS.DAT image valid in memory
inline constexpr uint16_t kSigninHandlers = 0x616A; // g_signinHandlers far[7]: sign-in input handler per sub-state
                                                    // key
inline constexpr uint16_t kSigninStateKeys = 0x6186; // g_signinStateKeys u16[7]: sub-state values of g_signinHandlers
                                                     // {1, 3, 5, 9, 0x0d, 0x11, 0x21}
inline constexpr uint16_t kSigninZones = 0x6194;    // g_signinZones zones: mouse zone list of input context 4 (count
                                                    // byte + 8-byte records)
inline constexpr uint16_t kSigninZone0Template = 0x622D; // g_signinZone0Template u8[8]: copied to zone 0 by
                                                         // SignIn_SetupZones
inline constexpr uint16_t kSigninZone8Template = 0x6235; // g_signinZone8Template u8[32]: copied to zones 8..11 by
                                                         // SignIn_SetupZones
inline constexpr uint16_t kSigninHeadings = 0x6255; // g_signinHeadings far[2]: sign-in headings (remove mode, select
                                                    // mode)
inline constexpr uint16_t kRankNames = 0x625D;      // g_rankNames char[8][8]: rank labels: Trainee, 1..6 stars, the
                                                    // crown glyph
inline constexpr size_t kRankNamesCount = 8;
inline constexpr size_t kRankNamesStride = 8;
inline constexpr uint16_t kSigninRankThresholds = 0x629D; // g_signinRankThresholds u16[7]: treasure totals of ranks
                                                          // 1..7
inline constexpr uint16_t kRecPicDataRestore = 0x62AB; // g_recPicDataRestore u8[6]: g_levelPicData word indices
                                                       // restored from record +0x124
inline constexpr uint16_t kRecPicDataPack = 0x62B1; // g_recPicDataPack u8[6]: g_levelPicData word indices packed into
                                                    // record +0x124
inline constexpr uint16_t kDialogYesHandlers = 0x62B7; // g_dialogYesHandlers far[19]: confirm handler per dialog id
                                                       // (g_dialogId)
inline constexpr size_t kDialogYesHandlersCount = 19;
inline constexpr uint16_t kDialogNoHandlers = 0x6303; // g_dialogNoHandlers far[19]: cancel handler per dialog id
inline constexpr uint16_t kStrNewPlayer = 0x6375;   // s_newPlayer char[]: last row of the sign-in list
inline constexpr uint16_t kStrSigninKeyLine = 0x6380; // s_signinKeyLine char[]: key prompt line of the sign-in screen

// --- Sound driver (491b) (fmt_sound.md §2, §5) -------------------------------------------------------
inline constexpr uint16_t kSfxData = 0x63A4;        // g_sfxData far: effects file buffer (segment 415f)
inline constexpr uint16_t kTunesData = 0x63A8;      // g_tunesData far: music file buffer (segment 41ca)
inline constexpr uint16_t kAdlibPitch = 0x63AC;     // g_adlibPitch u16[4]: AdLib pitch per voice
inline constexpr uint16_t kSoundOn = 0x63B4;        // g_soundOn u16: sound effects on
inline constexpr uint16_t kMusicOn = 0x63B6;        // g_musicOn u16: music on
inline constexpr uint16_t kSfxLoop = 0x63B8;        // g_sfxLoop u16: effects loop flag (never set)
inline constexpr uint16_t kMusicLoop = 0x63BA;      // g_musicLoop u16: music loop flag
inline constexpr uint16_t kSoundDevice = 0x63BC;    // g_soundDevice u16: 0 speaker, 1 Tandy, 2 AdLib, 3 IBM PS/1
                                                    // (rewritten to 1 by the driver set-up)
inline constexpr uint16_t kVoiceCount = 0x63BE;     // g_voiceCount u16: voices driven: 1 speaker, 4 otherwise
inline constexpr uint16_t kSndDoingMusic = 0x63C0;  // g_sndDoingMusic u16: 1 while the music block is processed
inline constexpr uint16_t kSndBlockPtr = 0x63C2;    // g_sndBlockPtr u16: block being processed (near pointer)
inline constexpr uint16_t kSndBlockSize = 0x63C4;   // g_sndBlockSize u16: state block size (0x78)
inline constexpr uint16_t kSfxOwnsVoice = 0x63C6;   // g_sfxOwnsVoice u16[4]: voice owned by an effect this tick
inline constexpr uint16_t kSndWork = 0x63CE;        // g_sndWork u8[0x78]: work copy of the block being processed
                                                    // (field offsets in namespace sndblock)
inline constexpr uint16_t kSfxBlock = 0x6448;       // g_sfxBlock u8[0x78]: effect state block
inline constexpr uint16_t kMusicBlock = 0x64C0;     // g_musicBlock u8[0x78]: music state block
inline constexpr uint16_t kSpeakerPeriods = 0x6538; // g_speakerPeriods u16[12]: PIT divisors of the lowest octave,
                                                    // C..B
inline constexpr uint16_t kPsgPeriods = 0x6550;     // g_psgPeriods u16[12]: SN76496 periods, C..B
inline constexpr uint16_t kPeriodTables = 0x6568;   // g_periodTables near[2]: near pointers to the two period tables
                                                    // by device
inline constexpr uint16_t kSoundPort = 0x656C;      // g_soundPort u16: sound chip port (0x388 initial, 0xc0 Tandy,
                                                    // 0x205 PS/1)
inline constexpr uint16_t kEnvelopes = 0x656E;      // g_envelopes near[36]: near pointers to the 0xff-terminated
                                                    // attenuation envelopes
inline constexpr size_t kEnvelopesCount = 36;
inline constexpr uint16_t kEnvelopeData = 0x65C3;   // g_envelopeData u8[]: envelope bytes (through DS:6bbd)

// --- New Player Hints and game messages (49a6) (code_elves_layout_hints.md §6.1, fmt_data.md §7.3) ---
inline constexpr uint16_t kFlutterText = 0x7CA3;    // g_flutterText far[37]: hint and message texts by id
inline constexpr size_t kFlutterTextCount = 37;
inline constexpr uint16_t kHintRankThresholds = 0x7D37; // g_hintRankThresholds u16[7]: rank thresholds (same values
                                                        // as DS:629d)
inline constexpr uint16_t kRankStartMsg = 0x7D45;   // g_rankStartMsg i8[8]: start-of-game message per rank (23..30)
inline constexpr uint16_t kMsgDigitOffsets = 0x7D4D; // g_msgDigitOffsets i16[12]: offset of the two-digit count in
                                                     // messages, per rank then end messages (-1 none)
inline constexpr uint16_t kMsgPluralOffsets = 0x7D65; // g_msgPluralOffsets i16[8]: offset of the plural letter per
                                                      // rank
inline constexpr uint16_t kEndMsgIds = 0x7D75;      // g_endMsgIds i8[4]: end-of-game messages: none, one, some, all
                                                    // found
inline constexpr uint16_t kCrownMsgToggle = 0x7D79; // g_crownMsgToggle u16: initial 1; alternates the two champion
                                                    // end messages (35, then 36)

// --- Input tables (4a85) (code_runtime.md §6.1, code_ui.md §2.3) -------------------------------------
inline constexpr uint16_t kKeysPlay = 0x7D84;       // g_keysPlay keylist: keys of contexts 0, 3, 5 (u16 count, then
                                                    // BIOS key codes)
inline constexpr uint16_t kCmdsPlay = 0x7DA0;       // g_cmdsPlay cmdlist: commands of contexts 0, 3, 5 (entry 0 = -1,
                                                    // then one per key)
inline constexpr uint16_t kKeysMenu = 0x7DBE;       // g_keysMenu keylist: keys of context 1 (menu bar)
inline constexpr uint16_t kCmdsMenu = 0x7DD2;       // g_cmdsMenu cmdlist: commands of context 1
inline constexpr uint16_t kKeysDialog = 0x7DE6;     // g_keysDialog keylist: keys of context 2 (dialogs, help)
inline constexpr uint16_t kCmdsDialog = 0x7DFE;     // g_cmdsDialog cmdlist: commands of context 2
inline constexpr uint16_t kKeysName = 0x7E16;       // g_keysName keylist: keys of context 4 (sign-in, name entry)
inline constexpr uint16_t kCmdsName = 0x7EA2;       // g_cmdsName cmdlist: commands of context 4
inline constexpr uint16_t kZonesPlay = 0x7F2E;      // g_zonesPlay zones: mouse zones of context 0 (count byte +
                                                    // 8-byte records)
inline constexpr uint16_t kZonesMenu = 0x7F87;      // g_zonesMenu zones: mouse zones of context 1
inline constexpr uint16_t kZonesDialog = 0x8010;    // g_zonesDialog zones: mouse zones of context 2
inline constexpr uint16_t kZonesQuiz = 0x8069;      // g_zonesQuiz zones: mouse zones of context 3
inline constexpr uint16_t kQuizAnswerZones = 0x80B2; // g_quizAnswerZones u8[3][8]: zones 9..11 of context 3: the
                                                     // answer rows
inline constexpr uint16_t kZonesTitle = 0x80CA;     // g_zonesTitle zones: mouse zones of context 5 (title pages)
inline constexpr uint16_t kTitleBottomZoneCmd = 0x80DA; // g_titleBottomZoneCmd i8: release command of context-5 zone
                                                        // 1: 13 in the file, 8 from the title page on, 13 again on
                                                        // story page 2
inline constexpr uint16_t kZoneLists = 0x80DB;      // g_zoneLists far[6]: zone list per input context
inline constexpr size_t kZoneListsCount = 6;        // input contexts
inline constexpr uint16_t kKeyLists = 0x80F3;       // g_keyLists far[6]: key list per input context
inline constexpr uint16_t kCmdLists = 0x810B;       // g_cmdLists far[6]: command list per input context

// --- AdLib driver (4ac9) (fmt_sound.md §5.5) ---------------------------------------------------------
inline constexpr uint16_t kOplSlotMap = 0x8124;     // g_oplSlotMap u8[9][2]: modulator and carrier slot per OPL
                                                    // channel
inline constexpr uint16_t kOplSlotOffsets = 0x8136; // g_oplSlotOffsets u8[18]: operator register offset per slot
inline constexpr uint16_t kOplInstruments = 0x816C; // g_oplInstruments rec4e[24]: instrument records (timbre layout,
                                                    // namespace oplinstr)
inline constexpr size_t kOplInstrumentsCount = 24;
inline constexpr size_t kOplInstrumentsStride = 78;
inline constexpr uint16_t kOplInstrumentPtrs = 0x88BC; // g_oplInstrumentPtrs far[24]: far pointer per instrument
inline constexpr uint16_t kOplOrchInstrument = 0x891C; // g_oplOrchInstrument u8[][9]: instrument per OPL channel for
                                                       // arrangement k = 4*tune + (rand() & 3) (0xff unused)
inline constexpr uint16_t kOplOrchTranspose = 0x8988; // g_oplOrchTranspose i8[][9]: transposition (semitones) per
                                                      // channel and arrangement
inline constexpr uint16_t kOplOrchUnused = 0x89F4;  // g_oplOrchUnused u8[][9]: third row: addressed but never read

// --- Title and attract pages (4ba4) (code_ui.md §8) --------------------------------------------------
inline constexpr uint16_t kSsPage = 0x8A14;         // g_ssPage u16: Super Solvers page shown (Esc returns to the
                                                    // title)
inline constexpr uint16_t kTitlePhase = 0x8A16;     // g_titlePhase u16: index into g_titlePhases
inline constexpr uint16_t kTitleSparkleX = 0x8A18;  // g_titleSparkleX i16[12]: sparkle spots, x bytes
inline constexpr size_t kTitleSparkleXCount = 12;
inline constexpr uint16_t kTitleSparkleY = 0x8A30;  // g_titleSparkleY i16[12]: sparkle spots, y
inline constexpr uint16_t kStrTitlePrompt = 0x8A48; // s_titlePrompt char[]: the prompt line under the title pages
inline constexpr uint16_t kStrStory1 = 0x8A5A;      // s_story1 char[]: story page 1 (LF/TAB markup)
inline constexpr uint16_t kStrStory2 = 0x8B54;      // s_story2 char[]: story page 2
inline constexpr uint16_t kStrStory2Keys = 0x8C49;  // s_story2Keys char[]: key line of story page 2 (series page)
inline constexpr uint16_t kStrSsPage1 = 0x8C6F;     // s_ssPage1 char[]: Super Solvers page, heading
inline constexpr uint16_t kStrSsPage2 = 0x8CCA;     // s_ssPage2 char[]: Super Solvers page, first title line
inline constexpr uint16_t kStrSsPage3 = 0x8CF0;     // s_ssPage3 char[]: Super Solvers page, second block
inline constexpr uint16_t kStrSsPage4 = 0x8D2B;     // s_ssPage4 char[]: Super Solvers page, third block
inline constexpr uint16_t kTitlePhases = 0x8D83;    // g_titlePhases near[14]: phase functions (segment 4ba4); 0 ends
                                                    // the title (phase 9) and phase 13
inline constexpr size_t kTitlePhasesCount = 14;

// --- Record layouts ---------------------------------------------------------------------------------

namespace filerec {  // g_fileTable record (fmt_data.md §1)
inline constexpr uint16_t kName = 0x00;          // char[13], NUL-padded
inline constexpr size_t kNameSize = 13;
inline constexpr uint16_t kDisk = 0x0D;          // u8: 1 or 2
inline constexpr size_t kSize = 22;
}  // namespace filerec

namespace boxrec {  // g_msgBoxes record (code_ui.md §4.1)
inline constexpr uint16_t kType = 0x00;          // u8
inline constexpr uint16_t kX1 = 0x02;            // i16, -1 = centred
inline constexpr uint16_t kY1 = 0x04;            // i16, -1 = centred
inline constexpr uint16_t kDefaultButton = 0x06; // u8 (rewritten for boxes 6..8 with the current setting)
inline constexpr uint16_t kTitle = 0x08;         // far char*
inline constexpr uint16_t kText = 0x0C;          // far char*, CR-separated lines
inline constexpr size_t kSize = 16;
}  // namespace boxrec

namespace zonerec {  // mouse zone list: u8 count, then records (code_ui.md §2.3, code_runtime.md §6.1)
inline constexpr uint16_t kFirst = 1;            // offset of record 0 after the count byte
inline constexpr uint16_t kX1 = 0, kY1 = 1, kX2 = 2, kY2 = 3;  // u8: x in bytes 0..159 (x1 = 200: disabled), y rows
inline constexpr uint16_t kHover = 4, kPress = 5, kHeld = 6, kRelease = 7;  // i8 commands, -1 none
inline constexpr size_t kSize = 8;
}  // namespace zonerec

namespace objrec {  // player and elf object head (code_world.md §2.1)
inline constexpr uint16_t kX = 0x00;             // u16 world x, 0..0x7ff within a row
inline constexpr uint16_t kY = 0x02;             // u16 world y; row = y >> 7, path line y & 0x7f == 0x70
inline constexpr uint16_t kDx = 0x04;            // i16: player bytes (x += 2*dx), elf pixels
inline constexpr uint16_t kDy = 0x06;            // i16 pixels
}  // namespace objrec

namespace elfrec {  // g_elves record (code_elves_layout_hints.md §3.1)
inline constexpr uint16_t kBase = 0x08;          // u16 base state (type and facing), 0 = unused
inline constexpr uint16_t kState = 0x0A;         // u16 index into g_elfStateHandlers
inline constexpr uint16_t kQuestion = 0x0C;      // u16 question slot 1..3, 0 none
inline constexpr uint16_t kSpriteSlot = 0x0E;    // u16 sprite slot 6/8/10
inline constexpr size_t kSize = 0x20;
inline constexpr size_t kCount = 15;
}  // namespace elfrec

namespace slotrec {  // g_spriteSlots record (code_player.md §3.1)
inline constexpr uint16_t kX = 0x00, kY = 0x02;  // screen bytes / rows (a child slot: offsets from its owner)
inline constexpr uint16_t kZ = 0x04;             // depth, drawn in descending order
inline constexpr uint16_t kFrame = 0x06;         // frame*2 + mirror bit, 0xffff = none
inline constexpr uint16_t kOwner = 0x08;         // slot whose position this one is relative to
inline constexpr uint16_t kAnim = 0x0A;          // running script id, 0 none
inline constexpr uint16_t kPrev = 0x0C;          // last script that ended or was stopped
inline constexpr uint16_t kPos = 0x0E;           // script position in words
inline constexpr uint16_t kDelay = 0x10;         // ticks left on the current entry
inline constexpr uint16_t kObj = 0x12;           // far pointer to the object of script op -4
inline constexpr size_t kSize = 22;
inline constexpr size_t kCount = 13;
}  // namespace slotrec

namespace drawrec {  // g_drawRecs record (code_player.md §3.1)
inline constexpr uint16_t kX = 0x00, kY = 0x02, kZ = 0x04, kFrame = 0x06;
inline constexpr uint16_t kOwner = 0x08;         // bit 15 = already drawn this pass
inline constexpr uint16_t kSave = 0x0A;          // far pointer to the saved background, 0 none
inline constexpr size_t kSize = 14;
}  // namespace drawrec

namespace levelpic {  // g_levelPicData row (code_elves_layout_hints.md §5.1)
inline constexpr uint16_t kPictureCount = 0, kKeyPicture = 2, kUnused = 4, kPlaces = 6;
}  // namespace levelpic

namespace spawnrange {  // g_levelXRange row (code_elves_layout_hints.md §3.3)
inline constexpr uint16_t kXBase = 0, kY = 2, kXMask = 4;
}  // namespace spawnrange

namespace sndblock {  // g_sfxBlock / g_musicBlock / g_sndWork, voices 0..3 (fmt_sound.md §5.1)
inline constexpr uint16_t kStatus = 0x00;        // 0 idle, 1 start requested, 2 playing
inline constexpr uint16_t kId = 0x02;            // 0xffff idle
inline constexpr uint16_t kTempo = 0x04, kTempoAdj = 0x06;
inline constexpr uint16_t kStream = 0x08, kStreamStart = 0x10, kTicksLeft = 0x18, kRawDur = 0x20;  // u16[4] each
inline constexpr uint16_t kActive = 0x28, kLegato = 0x30, kRelease = 0x38, kEnvStart = 0x40;
inline constexpr uint16_t kEnvPos = 0x48, kEnvDone = 0x50, kSustain = 0x58, kNewNote = 0x60;
inline constexpr uint16_t kRest = 0x68, kTie = 0x70;
inline constexpr size_t kSize = 0x78;
}  // namespace sndblock

namespace oplinstr {  // g_oplInstruments record (fmt_sound.md §5.5)
inline constexpr uint16_t kModulator = 0x00;     // 13 x i16 operator parameters
inline constexpr uint16_t kCarrier = 0x1A;       // 13 x i16
inline constexpr uint16_t kWaveModulator = 0x34, kWaveCarrier = 0x36;
}  // namespace oplinstr

// --- Run-time globals, by the module that owns them ----------------------------------------------
// Mostly BSS (zero at start); the ones below DS:8FA0 are in the zero-filled part of the initialised
// image unless their comment gives a value. Element sizes as in re/names/dos/dgroup.tsv.

// --- World, scenes and camera (code_world.md §2.4, §8.1) ---------------------------------------------
inline constexpr uint16_t kCamStepX = 0x37EE;       // g_camStepX i16: camera x step of the last Player_Move (0 = no
                                                    // scroll); latched in DS:37f6
inline constexpr uint16_t kCamStepY = 0x37F0;       // g_camStepY i16: camera y step of the last Player_Move; latched
                                                    // in DS:37f8
inline constexpr uint16_t kSlowScroll = 0x3D31;     // g_slowScroll u16: double scroll/camera steps (copy of the
                                                    // slow-CPU flag; 0 in the castle)
inline constexpr uint16_t kSlowCpuSaved = 0x8FA5;   // g_slowCpuSaved u16: real slow-CPU flag kept while the castle
                                                    // forces fast mode
inline constexpr uint16_t kCellScreenY = 0x8FC2;    // g_cellScreenY i16: screen y of the top of the visible cell row
                                                    // (0x10 - (camY & 0x7f))
inline constexpr uint16_t kDecorCount = 0x8FC4;     // g_decorCount u16: decorations/creature placed by visible
                                                    // handlers this tick (0 -> slot 12 hidden)
inline constexpr uint16_t kCastleCellIdx = 0x90FA;  // g_castleCellIdx u16: counts the visible castle cells within one
                                                    // Cells_UpdateVisible call
inline constexpr uint16_t kCamCellCol = 0x9124;     // g_camCellCol u16: camera cell column (Frame_Begin)
inline constexpr uint16_t kCamCellRow16 = 0x9126;   // g_camCellRow16 u16: camera cell row * 16 (Frame_Begin)
inline constexpr uint16_t kShelfCamRoom = 0x9129;   // g_shelfCamRoom u8: shelf room entered from its left end (shelf
                                                    // x relative to camX 0x440); other values: entered from the right
                                                    // (0x480)
inline constexpr uint16_t kInShelfRoom = 0x9201;    // g_inShelfRoom u8[4]: player is in treasure shelf room 0..3
inline constexpr uint16_t kCamTileColOff = 0x920D;  // g_camTileColOff u16: (camX & 0x78) << 2: tile column offset in
                                                    // a block map
inline constexpr uint16_t kCamTileRowOff = 0x920F;  // g_camTileRowOff u16: (camY >> 2) & 0x1e: tile row offset in a
                                                    // map column
inline constexpr uint16_t kStoredShowcase = 0x9254; // g_storedShowcase i8: showcase treasure put away while outside
                                                    // the clubhouse (Cell_P12_StartRoom / Cell_P11_ClubDoor)
inline constexpr uint16_t kCreatureCell = 0x9287;   // g_creatureCell u16: 0 = no castle creature, else the visible
                                                    // castle-cell index where it appeared
inline constexpr uint16_t kBorderColor = 0x92C8;    // g_borderColor u8: overscan colour counter of Cell_P0_Void
inline constexpr uint16_t kCurCell = 0x92CB;        // g_curCell u16: cell index of the object in the last
                                                    // Cell_GetMask

// --- Player, animation, search and end sequence (code_player.md §3.1, §4.1, §7.1) --------------------
inline constexpr uint16_t kPlayerScrX = 0x8FA7;     // g_playerScrX i16: player x relative to the camera, bytes
                                                    // (Game_Tick)
inline constexpr uint16_t kPlayerScrY = 0x8FA9;     // g_playerScrY i16: player y relative to the camera, rows
                                                    // (Game_Tick)
inline constexpr uint16_t kShackNoCase = 0x8FAF;    // g_shackNoCase u8: treasure room: shown treasure had no display
                                                    // case
inline constexpr uint16_t kSearchPicture = 0x9128;  // g_searchPicture u8: closed picture of the cell being searched
inline constexpr uint16_t kDrawRecs = 0x9145;       // g_drawRecs Drec[13]: draw records, 14 bytes: x, y, depth,
                                                    // frame, owner (bit 15 = drawn), far save pointer
inline constexpr uint16_t kShackY = 0x9211;         // g_shackY i16: treasure room: pedestal treasure y offset (rest
                                                    // 0x44)
inline constexpr uint16_t kShackX = 0x9213;         // g_shackX u16: treasure room: pedestal treasure x offset (rest
                                                    // 0x1a, off-screen 0x50)
inline constexpr uint16_t kShowcasePick = 0x9219;   // g_showcasePick u8: showcase treasure chosen at the castle top
                                                    // (bit 7 = new pick); 0xff after hand-over
inline constexpr uint16_t kKeysUnmasked = 0x923A;   // g_keysUnmasked u16: g_keys before the terrain mask was applied
                                                    // this tick
inline constexpr uint16_t kShackFlags = 0x9251;     // g_shackFlags u8: treasure room: 1 carousel move, 0x80 right/fly
                                                    // started, 2 swap done, 0x10 fly phase 2, 0x20 display anim owns
                                                    // slot 3, 0x40 pair/fly running
inline constexpr uint16_t kPlayerMask = 0x926A;     // g_playerMask u16: terrain mask of the player's cell, applied to
                                                    // g_keys next tick
inline constexpr uint16_t kShackStay = 0x9282;      // g_shackStay u8: treasure room: do not auto-advance to the rank
                                                    // screen
inline constexpr uint16_t kSaveHeapHigh = 0x929F;   // g_saveHeapHigh u16: high-water mark of the sprite save heap
                                                    // offset; low byte saved as USERS.DAT header byte 1
inline constexpr uint16_t kEndCount = 0x92D0;       // g_endCount i16: end sequence: sparkles still to show
inline constexpr uint16_t kEndSeqDone = 0x92D2;     // g_endSeqDone u16: end sequence finished
inline constexpr uint16_t kEndScanIdx = 0x92D4;     // g_endScanIdx u16: end sequence: next hiding slot to scan
inline constexpr uint16_t kEndAllFound = 0x92D6;    // g_endAllFound u16: end sequence: all treasures of the game
                                                    // found
inline constexpr uint16_t kEndPathIdx = 0x92D8;     // g_endPathIdx u16: end sequence: index into the flight/sparkle
                                                    // tables
inline constexpr uint16_t kEndTreasureCount = 0x92DA; // g_endTreasureCount u16: end sequence: treasures flown into
                                                      // the chest
inline constexpr uint16_t kDrawOrder = 0x92DC;      // g_drawOrder u16[13]: draw record indices in drawing order (for
                                                    // the erase)
inline constexpr uint16_t kSpritesChanged = 0x92F6; // g_spritesChanged u16: a draw record changed since the last
                                                    // erase
inline constexpr uint16_t kSaveHeapReset = 0x92F8;  // g_saveHeapReset u16: next save-under starts a new save heap

// --- Elves, layout and hints (code_elves_layout_hints.md §3.2, §5.1, §6.1) ---------------------------
inline constexpr uint16_t kElfSpriteCount = 0x9132; // g_elfSpriteCount u16: elves holding a sprite (+1 on a slow
                                                    // CPU); a new sprite only while < 3
inline constexpr uint16_t kHintTimerReset = 0x913F; // g_hintTimerReset u16: 1 = Hints_Update resets g_seconds and the
                                                    // hint timers (set by Game_Start and the Options hint switch)
inline constexpr uint16_t kKeyCellL1 = 0x927A;      // g_keyCellL1 u16: cell of the level-1 key (Layout_PutInSlot);
                                                    // written, never read
inline constexpr uint16_t kKeyCellL2 = 0x927C;      // g_keyCellL2 u16: cell of the level-2 key (Layout_PutInSlot);
                                                    // written, never read
inline constexpr uint16_t kLastGameTier = 0x9289;   // g_lastGameTier u16: tier at Game_End (for the found-all end
                                                    // message)
inline constexpr uint16_t kKeyPicture = 0x9295;     // g_keyPicture i8: key picture of the level being laid out
                                                    // (Layout_PlaceKey -> Clue_BuildWords)
inline constexpr uint16_t kElfRelX = 0xA204;        // g_elfRelX i16: elf x relative to the camera, bytes
                                                    // (strip-linear)
inline constexpr uint16_t kElfRelY = 0xA206;        // g_elfRelY i16: elf y within the cell relative to the camera
inline constexpr uint16_t kPlayerRelX = 0xA208;     // g_playerRelX i16: player sprite x - 2 (slot 1), bytes
inline constexpr uint16_t kNetCatchesThisTick = 0xA20A; // g_netCatchesThisTick u16: catches resolved in this
                                                        // Elves_Update (first one uses a net); reset at its end
inline constexpr uint16_t kElfNetClass = 0xA20C;    // g_elfNetClass u16: Elf_Think result: 0 none, 1 released, 2 got
                                                    // a sprite, 3 visible, 4 net edge (dodge), 5 net, 6 net centre
inline constexpr uint16_t kElfCallCounter = 0xA20E; // g_elfCallCounter u16: +1 per Elf_Think call; bit 1 gates
                                                    // hidden-elf drift; 0 when a search freeze ends
inline constexpr uint16_t kElfAnimSel = 0xA210;     // g_elfAnimSel u16: scratch: animation id offset (0 / 0x16
                                                    // carrier) shared by the elf handlers; ElfSt_03 reads the stale
                                                    // value
inline constexpr uint16_t kElfActiveCount = 0xA212; // g_elfActiveCount u16: elves processed: 15, or 7 on a slow CPU
                                                    // (set by Elves_Spawn)
inline constexpr uint16_t kHint3Level = 0xC6CE;     // g_hint3Level u16: level of g_hint3Start
inline constexpr uint16_t kHint3Start = 0xC6D0;     // g_hint3Start u16: g_seconds+1 when the second clue word of a
                                                    // level was seen (hint 3)

// --- UI, quiz, sign-in and title (code_ui.md §2, §9, §10) --------------------------------------------
inline constexpr uint16_t kMusicGate = 0x37CE;      // g_musicGate u16: 0xFFFF music runs, 0 music paused (UI mode
                                                    // active); ANDed with the music status in the timer ISR
inline constexpr uint16_t kPromptIcon = 0x90F8;     // g_promptIcon u16: HUD message-box prompt: 0 none, 2 move
                                                    // box/select, 4 drop coin, 6 press Enter (record +0x14c)
inline constexpr uint16_t kMenuOpen = 0x90FC;       // g_menuOpen i16: open drop-down 0 Help, 1 File, 2 Options, -1
                                                    // none
inline constexpr uint16_t kMenuItem = 0x9111;       // g_menuItem u16: highlighted drop-down item
inline constexpr uint16_t kHudPrompt = 0x91FB;      // g_hudPrompt u16: HUD cache of g_promptIcon
inline constexpr uint16_t kTextInk = 0x9215;        // g_textInk u16: text colour word used by Text_Draw
inline constexpr uint16_t kQuizClosed = 0x9220;     // g_quizClosed u16: 1 = no elf question shown
inline constexpr uint16_t kCmdConsumed = 0x9238;    // g_cmdConsumed u16: 1 = g_cmd consumed by a UI handler;
                                                    // Input_Poll may load the next command
inline constexpr uint16_t kHudNets = 0x924D;        // g_hudNets u16: HUD cache of g_nets
inline constexpr uint16_t kHudFound = 0x9259;       // g_hudFound u16: HUD cache of g_treasuresFound
inline constexpr uint16_t kHudKeyState = 0x9268;    // g_hudKeyState u16: HUD cache of g_keyState (moved to force a
                                                    // clue-box redraw)
inline constexpr uint16_t kMenuLock = 0x926C;       // g_menuLock u16: menu bar cannot be opened (searches, end
                                                    // sequence)
inline constexpr uint16_t kHudCoins = 0x928D;       // g_hudCoins u16: HUD cache of g_coins
inline constexpr uint16_t kLastUiMode = 0x9300;     // g_lastUiMode u8: g_uiMode seen by Sound_SyncState
inline constexpr uint16_t kHelpPage = 0x9301;       // g_helpPage i8: current help page of the topic
inline constexpr uint16_t kUiCmd = 0x930C;          // g_uiCmd i16: command being handled by the UI code (copy of
                                                    // g_cmd)
inline constexpr uint16_t kButtonLabel = 0x930E;    // g_buttonLabel far[2]: button labels (0 No / Go Back, 1 Yes /
                                                    // See Next Page / Done)
inline constexpr uint16_t kButtonX = 0x9316;        // g_buttonX u16[2]: button x (bytes)
inline constexpr uint16_t kButtonY = 0x931A;        // g_buttonY u16[2]: button y
inline constexpr uint16_t kBoxWidth = 0x931E;       // g_boxWidth u16: width of the last box (bytes)
inline constexpr uint16_t kHelpPageCount = 0x9322;  // g_helpPageCount u8: pages of the open help topic
inline constexpr uint16_t kDialogId = 0x9323;       // g_dialogId u16: dialog id: 0..7 help topics, 8..13 menu boxes,
                                                    // 14..18 sign-in boxes
inline constexpr uint16_t kButtonSel = 0xA1FD;      // g_buttonSel u16: selected button (0 No / back, 1 Yes / next)
inline constexpr uint16_t kBoxTextY = 0xA1FF;       // g_boxTextY u16: y of the first text line of the last box
inline constexpr uint16_t kBoxLines = 0xA201;       // g_boxLines u16: text lines of the last box (= drop-down item
                                                    // count)
inline constexpr uint16_t kQuizFocus = 0xA214;      // g_quizFocus i16: focused answer row 0..2
inline constexpr uint16_t kQuizQuestion = 0xA216;   // g_quizQuestion u16: question index of the open slot
inline constexpr uint16_t kQuizSlot = 0xA21C;       // g_quizSlot u16: slot of the open question (g_caughtSlot - 1)
inline constexpr uint16_t kQuizOrder = 0xA21E;      // g_quizOrder u16: answer display order 0..5 (DS:5e9c)
inline constexpr uint16_t kPraiseWord = 0xA220;     // g_praiseWord far: clue word revealed by the right answer
inline constexpr uint16_t kTierEnd = 0xA224;        // g_tierEnd u16: end (exclusive) of the tier
inline constexpr uint16_t kAnswerText = 0xA22A;     // g_answerText char[3][35]: the three answers of the open
                                                    // question
inline constexpr uint16_t kQuestionText = 0xA293;   // g_questionText char[]: question text of the open question
inline constexpr uint16_t kDealPlan = 0xA67B;       // g_dealPlan u8[9]: wanted history status per slot
                                                    // (Quiz_PlanStatuses)
inline constexpr uint16_t kPuzzleText = 0xA684;     // g_puzzleText far[9]: expanded text of the 9 dealt questions
inline constexpr uint16_t kAnswerWidth = 0xA6A8;    // g_answerWidth u16: widest answer (bytes)
inline constexpr uint16_t kTierFirst = 0xA6AA;      // g_tierFirst u16: first question of the tier
inline constexpr uint16_t kStatusCount = 0xA6AC;    // g_statusCount u16[4]: questions of the tier per history status
inline constexpr uint16_t kQuizAnswered = 0xB1FA;   // g_quizAnswered u16: the open question was answered
inline constexpr uint16_t kQuizMask = 0xBA04;       // g_quizMask u8: answer mask of the open question at opening
inline constexpr uint16_t kSigninChar = 0xBA06;     // g_signinChar u8: character of the current sign-in command
                                                    // (first letter upper-cased)
inline constexpr uint16_t kSigninState = 0xBA07;    // g_signinState u16: 1 list, 3 name entry, 5 remove, 9 exit box,
                                                    // 0x0d remove box, 0x11 saved-game box, 0x21 OK box
inline constexpr uint16_t kCaretArmed = 0xBA09;     // g_caretArmed u8: caret blink armed (toggles when DS:90fe <= 6)
inline constexpr uint16_t kNewName = 0xBA0A;        // g_newName char[9]: new player name being typed
inline constexpr uint16_t kSigninRow = 0xBA13;      // g_signinRow i8: highlighted sign-in row
inline constexpr uint16_t kNewNameLen = 0xC6BF;     // g_newNameLen i8: length of g_newName
inline constexpr uint16_t kSigninAny = 0xC6C0;      // g_signinAny u8: at least one player exists
inline constexpr uint16_t kSigninRecIdx = 0xC6C1;   // g_signinRecIdx u8[9]: record index per sorted row
inline constexpr uint16_t kCaretOn = 0xC6CA;        // g_caretOn u8: name cursor block drawn
inline constexpr uint16_t kSigninStart = 0xC6CB;    // g_signinStart u8: a player was chosen: start the game
inline constexpr uint16_t kSigninCount = 0xC6CC;    // g_signinCount i8: number of players
inline constexpr uint16_t kTitleTimer = 0xCB7E;     // g_titleTimer u32: title page time-out (g_ticks + 7200), 0 = not
                                                    // armed

// --- Main loop, timer and input (code_runtime.md §8) -------------------------------------------------
inline constexpr uint16_t kSavedVideoMode = 0x0618; // g_savedVideoMode u8: video mode at start (INT 10h AH=0Fh),
                                                    // restored by Video_RestoreMode
inline constexpr uint16_t kSavedLogicalFloppy = 0x0619; // g_savedLogicalFloppy u8: saved BIOS byte 0040:0504
inline constexpr uint16_t kNetAllowed = 0x061A;     // g_netAllowed u8: 1 when ssrnet.dat exists
inline constexpr uint16_t kSavedKbdFlags = 0x061B;  // g_savedKbdFlags u8: BIOS keyboard flags 0040:0017 at start
                                                    // (bits 5/6 cleared in the BIOS copy, never restored)
inline constexpr uint16_t kFrameCamX = 0x37F2;      // g_frameCamX u16: camera x latched by Frame_Begin for this frame
inline constexpr uint16_t kFrameCamY = 0x37F4;      // g_frameCamY u16: camera y latched by Frame_Begin for this frame
inline constexpr uint16_t kFrameCamStepX = 0x37F6;  // g_frameCamStepX i16: latched camera delta x (sign selects the
                                                    // scroll direction; 0 in redraw frames)
inline constexpr uint16_t kFrameCamStepY = 0x37F8;  // g_frameCamStepY i16: latched camera delta y
inline constexpr uint16_t kBiosChainCount = 0x3D33; // g_biosChainCount u8: Timer_ISR countdown 12..0: chain the BIOS
                                                    // INT 8 every 13th interrupt
inline constexpr uint16_t kGameTickCount = 0x3D34;  // g_gameTickCount u8: Timer_ISR countdown 4..0: Game_Tick every
                                                    // 5th interrupt
inline constexpr uint16_t kFrameInShelfRoom = 0x8F9F; // g_frameInShelfRoom u8[4]: frame latch of g_inShelfRoom
                                                      // DS:9201..9204 (Frame_Begin); read by ShelfRoom_DrawShelf and
                                                      // Hud_Redraw
inline constexpr uint16_t kFrameTransition = 0x8FB2; // g_frameTransition u16: frame latch of g_transition (dissolve
                                                     // in Frame_Present)
inline constexpr uint16_t kPlayfieldRedrawn = 0x8FBA; // g_playfieldRedrawn u8: set by Playfield_Redraw, read and
                                                      // cleared by ShelfRoom_DrawShelf
inline constexpr uint16_t kFrameRankState = 0x8FBD; // g_frameRankState u8: frame latch of the rank chest state
                                                    // DS:925d
inline constexpr uint16_t kFramePartialRedraw = 0x8FC8; // g_framePartialRedraw u16: frame latch of DS:9278: redraw
                                                        // only the searched cell's columns
inline constexpr uint16_t kMouseButtons = 0x90F4;   // g_mouseButtons u16: mouse buttons (INT 33h AX=3 BX), this tick
inline constexpr uint16_t kCmdQueued = 0x90F6;      // g_cmdQueued i16: one-deep command queue of Input_Poll (also
                                                    // used to inject Enter / F1)
inline constexpr uint16_t kTick13 = 0x90FE;         // g_tick13 i16: counts 12..0 per game tick (caret blink)
inline constexpr uint16_t kSecondTicks = 0x9100;    // g_secondTicks u8: game ticks left in the current hint "second"
                                                    // (60 per second, only counted down to a reset while g_uiMode ==
                                                    // 0)
inline constexpr uint16_t kMouseX = 0x911A;         // g_mouseX u16: mouse x 0..159 (byte units)
inline constexpr uint16_t kMouseY = 0x911E;         // g_mouseY u16: mouse y 0..199
inline constexpr uint16_t kCoinsShown = 0x9122;     // g_coinsShown u16: coin number shown on the HUD (counts up/down
                                                    // one per frame towards g_coins)
inline constexpr uint16_t kMouse = 0x912A;          // g_mouse i16: -1 when a mouse driver answered INT 33h AX=0
inline constexpr uint16_t kStartDrive = 0x9136;     // g_startDrive u16: current drive at start (getdisk)
inline constexpr uint16_t kMouseButtonsPrev = 0x9143; // g_mouseButtonsPrev u16: buttons of the previous Mouse_Poll
inline constexpr uint16_t kHudRedraw = 0x922E;      // g_hudRedraw u16: HUD full redraw request (1, or 2 = also redraw
                                                    // the sprites)
inline constexpr uint16_t kFullRedrawReq = 0x924F;  // g_fullRedrawReq u16: full play-field redraw request (latched
                                                    // into g_fullRedraw)
inline constexpr uint16_t kJumpFromDiagonal = 0x9257; // g_jumpFromDiagonal u16: jump bit 0x20 came from a diagonal or
                                                      // a mouse jump; the next key event clears it
inline constexpr uint16_t kHudRedrawReq2 = 0x925B;  // g_hudRedrawReq2 u16: extra HUD redraw request ORed into DS:922e
                                                    // at Frame_Begin
inline constexpr uint16_t kRankState = 0x925D;      // g_rankState u8: rank chest screen state: 0 off, 1 start, 2
                                                    // filling
inline constexpr uint16_t kMouseXPrev = 0x925E;     // g_mouseXPrev u16: mouse x of the previous tick
inline constexpr uint16_t kMouseYPrev = 0x9266;     // g_mouseYPrev u16: mouse y of the previous tick
inline constexpr uint16_t kDrawnCellCol = 0x9270;   // g_drawnCellCol u16: camera block column of the last drawn
                                                    // play-field
inline constexpr uint16_t kDrawnCellRow16 = 0x9272; // g_drawnCellRow16 u16: camera block row * 16 of the last drawn
                                                    // play-field
inline constexpr uint16_t kPartialRedrawReq = 0x9278; // g_partialRedrawReq u16: partial redraw request (searched
                                                      // cell)
inline constexpr uint16_t kSpriteSlotsDrawn = 0x9283; // g_spriteSlotsDrawn u16: sprite slots drawn this frame: 13 in
                                                      // play, 1 (pointer only) with a UI mode
inline constexpr uint16_t kLastBiosKey = 0x9285;    // g_lastBiosKey u16: last BIOS key read by Input_Poll (0 none)
inline constexpr uint16_t kDrawnCamX = 0x9297;      // g_drawnCamX u16: camX as last drawn (Scroll_Update /
                                                    // Playfield_Redraw); 9299 = 920d, 9270 = 9124, 929b = camY, 929d
                                                    // = 920f, 9272 = 9126 as drawn
inline constexpr uint16_t kDrawnTileColOff = 0x9299; // g_drawnTileColOff u16: tile column offset of the last drawn
                                                     // play-field
inline constexpr uint16_t kDrawnCamY = 0x929B;      // g_drawnCamY u16: camera y of the last drawn play-field
inline constexpr uint16_t kDrawnTileRowOff = 0x929D; // g_drawnTileRowOff u16: tile row offset of the last drawn
                                                     // play-field
inline constexpr uint16_t kRankRowsDone = 0x92A6;   // g_rankRowsDone u16: rank chest rows drawn so far
inline constexpr uint16_t kRankRowsWanted = 0x92A8; // g_rankRowsWanted u16: rank chest rows to draw (fill value of
                                                    // the next rank pair)
inline constexpr uint16_t kMousePtrVisible = 0x92AC; // g_mousePtrVisible u16: mouse pointer shown (hidden after 600
                                                     // idle ticks)
inline constexpr uint16_t kMouseIdleTicks = 0x92AE; // g_mouseIdleTicks u16: ticks without mouse change
inline constexpr uint16_t kRankTickPhase = 0x92B0;  // g_rankTickPhase u16: DS:9100 & 0xfc at the last chest row (one
                                                    // row per 4 game ticks)
inline constexpr uint16_t kFloppyCount = 0x92B2;    // g_floppyCount u8: floppy drives from the equipment word
inline constexpr uint16_t kCritErr = 0x92C1;        // g_critErr u16: critical error flag set by HardErr_Handler
inline constexpr uint16_t kCritErrCode = 0x92C3;    // g_critErrCode u16: critical error errval
inline constexpr uint16_t kCritErrAx = 0x92C5;      // g_critErrAx u16: critical error AX (AL = drive)
inline constexpr uint16_t kCtrlReleased = 0x92FA;   // g_ctrlReleased u16: Kbd_ISR Ctrl flag (1 after Ctrl release, 0
                                                    // after press); no reader
inline constexpr uint16_t kOldInt9 = 0x92FC;        // g_oldInt9 far: saved INT 09h vector

// --- Other globals (fmt_data.md §7.6) ----------------------------------------------------------------
inline constexpr uint16_t kMachineId = 0x1300;      // g_machineId u8: F000:FFFE byte, 0x21 = Tandy 1000
inline constexpr uint16_t kInGameTick = 0x1309;     // g_inGameTick u16: re-entrancy guard of Game_Tick
inline constexpr uint16_t kTilesPtr = 0x37B4;       // g_tilesPtr far: unpacked tiles (32d7:0000)
inline constexpr uint16_t kMapPtr = 0x37B8;         // g_mapPtr far: unpacked map (2897:0000)
inline constexpr uint16_t kSpriteBank = 0x37BC;     // g_spriteBank far: 0x1C908-byte sprite bank (PAK)
inline constexpr uint16_t kPlayer = 0x37E2;         // g_player Obj: player object: +0 world x, +2 world y, +4 dx, +6
                                                    // dy ...
inline constexpr uint16_t kPlayerDX = 0x37E6;       // g_playerDX i16: movement request x
inline constexpr uint16_t kPlayerDY = 0x37E8;       // g_playerDY i16: movement request y
inline constexpr uint16_t kCameraX = 0x37EA;        // g_cameraX u16: camera world x
inline constexpr uint16_t kCameraY = 0x37EC;        // g_cameraY u16: camera world y (multiple of 0x80 = one cell row)
inline constexpr uint16_t kElves = 0x37FA;          // g_elves Elf[15]: elves, 32 bytes: +0 x, +2 y, +4 dx, +8 base
                                                    // state, +0xa state, +0xc question slot (0 = none), +0xe sprite
                                                    // slot
inline constexpr uint16_t kTicks = 0x3D2B;          // g_ticks u32: 236.7 Hz tick counter
inline constexpr uint16_t kCastleLayout = 0x8F9E;   // g_castleLayout u8: chosen castle layout (record +0x166)
inline constexpr uint16_t kSearchTimer = 0x8FA3;    // g_searchTimer i16: hiding-place search countdown
inline constexpr uint16_t kCurObj = 0x8FAB;         // g_curObj far: object being processed (player or elf)
inline constexpr uint16_t kShownTreasure = 0x8FB0;  // g_shownTreasure i16: treasure shown in the treasure room
                                                    // (record +0x164)
inline constexpr uint16_t kShelfBits = 0x8FB6;      // g_shelfBits far: collected-treasure shelf bits (record +0x133)
inline constexpr uint16_t kFullRedraw = 0x8FBB;     // g_fullRedraw u8: request a full play-field redraw (counter)
inline constexpr uint16_t kLoadedScene = 0x8FBE;    // g_loadedScene u16: scene currently loaded (0xff none)
inline constexpr uint16_t kCellScreenX = 0x8FC0;    // g_cellScreenX i16: screen x (bytes) of the cell being handled
                                                    // by Cells_UpdateVisible
inline constexpr uint16_t kSearchCell = 0x8FC6;     // g_searchCell u16: cell being searched
inline constexpr uint16_t kSceneWanted = 0x8FCA;    // g_sceneWanted u16: scene to show (copy of DS:928f each frame)
inline constexpr uint16_t kQuizHistory = 0x8FD2;    // g_quizHistory far: 2 bits per question (record +0x0c)
inline constexpr uint16_t kSpriteSlots = 0x8FD6;    // g_spriteSlots Slot[13]: sprite/animation slots, 22 bytes: x, y,
                                                    // ?, frame, slot, anim, prev, pos, delay, far obj
inline constexpr uint16_t kSlot2X = 0x9002;         // g_slot2X i16: sprite slot 2 x (coin / search content sprite)
inline constexpr uint16_t kQuizTier = 0x9101;       // g_quizTier far: quiz tier byte (record +0x13f)
inline constexpr uint16_t kKeys = 0x9105;           // g_keys u16: held keys: 1 up 2 down 4 left 8 right 0x10 net 0x20
                                                    // jump 0x40 coin; high byte = cell mask
inline constexpr uint16_t kAnswerMasks = 0x9107;    // g_answerMasks far: 9 enabled-answer masks (record +0x78)
inline constexpr uint16_t kArea = 0x910B;           // g_area u16: area/music id (record +0x156)
inline constexpr uint16_t kStepDone = 0x910D;       // g_stepDone u16: set when an animation advanced or the camera
                                                    // moved; cleared by Frame_Begin; blocks further logic ticks until
                                                    // the next frame
inline constexpr uint16_t kNets = 0x910F;           // g_nets u16: nets (record +0x146)
inline constexpr uint16_t kTreasuresFound = 0x9113; // g_treasuresFound u16: treasures found this game (record +0x148)
inline constexpr uint16_t kSeconds = 0x9115;        // g_seconds u16: 1.27 s counter
inline constexpr uint16_t kUiMode = 0x9119;         // g_uiMode u8: 0 play; 0x80 title; 0x40 menu; 0x20 dialog; 0x10
                                                    // hint; 8 sign-in; 4 message; 2 quiz
inline constexpr uint16_t kMessagePending = 0x911C; // g_messagePending u16: 1 start-of-game message, 2 end-of-game
                                                    // message
inline constexpr uint16_t kKeyState = 0x9120;       // g_keyState u16: 2/4/6 key of level 0/1/2 held, 8 new game,
                                                    // 10/12 key of level 1/2 hidden
inline constexpr uint16_t kVideoType = 0x912C;      // g_videoType i16: 1 EGA, 2 CGA, 3 Tandy, 4 Hercules, 5 MCGA; -1
                                                    // none
inline constexpr uint16_t kQuit = 0x912E;           // g_quit u16: leave the frame loop
inline constexpr uint16_t kTier = 0x9130;           // g_tier u16: rank tier 0..3 by total treasures (record +0x140)
inline constexpr uint16_t kCoins = 0x9134;          // g_coins u16: coins (record +0x144)
inline constexpr uint16_t kTotalTreasures = 0x9138; // g_totalTreasures far: total treasures (record +0x0a)
inline constexpr uint16_t kPlayerState = 0x913C;    // g_playerState u16: player state
inline constexpr uint16_t kScreen = 0x913E;         // g_screen u16: screen painter index
inline constexpr uint16_t kClueWords = 0x91FD;      // g_clueWords far: 9 clue word bytes, 3 per level (record +0x81)
inline constexpr uint16_t kGameInProgress = 0x9205; // g_gameInProgress u16: set after the start message; copied to
                                                    // record +9 when leaving with the game kept
inline constexpr uint16_t kFoundMasks = 0x9209;     // g_foundMasks far: per level found-treasure bits (record +0x130)
inline constexpr uint16_t kTransition = 0x9217;     // g_transition u16: teleport/transition in progress (1/2)
inline constexpr uint16_t kQuizSlots = 0x921A;      // g_quizSlots far: 9 question ids (record +0x66)
inline constexpr uint16_t kCaughtSlot = 0x921E;     // g_caughtSlot u16: question slot of the caught elf (0 none)
inline constexpr uint16_t kHintsOn = 0x9224;        // g_hintsOn u16: new Player Hints
inline constexpr uint16_t kCmd = 0x9226;            // g_cmd i16: current input command
inline constexpr uint16_t kUserRec = 0x9228;        // g_userRec far: selected 360-byte record
inline constexpr uint16_t kInputContext = 0x922C;   // g_inputContext u16: key/zone table set
inline constexpr uint16_t kLastGameTreasures = 0x9232; // g_lastGameTreasures u16: treasures found in the game just
                                                       // finished (end message)
inline constexpr uint16_t kEndState = 0x9234;       // g_endState u16: end of game: 1 at the castle gate, 2 when
                                                    // leaving the treasure room (Scene_Update then runs Game_End and
                                                    // a new game)
inline constexpr uint16_t kSlowCpu = 0x923C;        // g_slowCpu u16: 1 on an 8088/8086 (or toggled with X)
inline constexpr uint16_t kHintFlags = 0x923E;      // g_hintFlags u8[15]: 0 pending, 0x80 shown
inline constexpr uint16_t kDirty = 0x9255;          // g_dirty u16: something was drawn this frame (present needed)
inline constexpr uint16_t kCreatureStrike = 0x9264; // g_creatureStrike u16: set by animation op -6 (castle creature
                                                    // strike)
inline constexpr uint16_t kGamesPlayed = 0x926E;    // g_gamesPlayed u16: games played (record +0x162)
inline constexpr uint16_t kCellCallKind = 0x9274;   // g_cellCallKind u16: 1 = visible-cell call, 2 = object-in-cell
                                                    // call
inline constexpr uint16_t kSearchSide = 0x9276;     // g_searchSide i16: -1/0/+1: search the left/own/right cell
inline constexpr uint16_t kCollected = 0x927E;      // g_collected far: 48 collected treasure ids (record +0x8a)
inline constexpr uint16_t kLevel = 0x928F;          // g_level u16: 0..2 mountain level, 3 castle top, 4 treasure room
inline constexpr uint16_t kClueScheme = 0x9296;     // g_clueScheme u8: clue scheme 0..2
inline constexpr uint16_t kShopCoinsOwed = 0x92A1;  // g_shopCoinsOwed i16: net-shop price still to pay (4..0, -1 =
                                                    // net taken; record +0x158)
inline constexpr uint16_t kCellMask = 0x92C9;       // g_cellMask u16: terrain mask returned by the cell handlers
inline constexpr uint16_t kHelpData = 0x9325;       // g_helpData u8[0xed8]: HELP.DAT
inline constexpr uint16_t kQuizCoins = 0xB1F8;      // g_quizCoins u16: coins including the pending quiz reward
inline constexpr uint16_t kDictionary = 0xB204;     // g_dictionary u8[]: PRG_DICT
inline constexpr uint16_t kUsersFile = 0xBA14;      // g_usersFile u8[0xcab]: USERS.DAT image: 3-byte header + 9
                                                    // records of 360 bytes

}  // namespace tmport::dos::dg
