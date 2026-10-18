#pragma once
// Named DGROUP (TMTWINCD.EXE segment 56, Ghidra selector 11b8) offsets of every table the engine
// reads. Offsets are facts about the user's EXE; the data itself is always read from the file.
// Sources: re/docs/dgroup_map.md (layouts) and re/docs/PORTING_NOTES.md §5 (what the engine needs).
// "1-based" marks tables whose element 0 is a dummy (dgroup_map.md §1.2).

#include <cstddef>
#include <cstdint>

namespace tmport::dg {

// --- Segment facts (dgroup_map.md §1) -----------------------------------------------------------
inline constexpr uint16_t kSegmentNumber = 56;       // NE auto-data segment
inline constexpr uint16_t kSelector = 0x11B8;        // Ghidra notation (0x1000 + 8*(56-1))
inline constexpr uint32_t kFileOffset = 0x23C00;     // in the shipped EXE (located via the NE header)
inline constexpr uint16_t kLength = 0x66A2;          // initialised image length
inline constexpr uint16_t kBssStart = 0x3880;        // 3880..66a1 are zero in the file
inline constexpr size_t kRelocationCount = 198;      // all 16:16 far pointers

// --- Validation markers ------------------------------------------------------------------------
inline constexpr uint16_t kUsersMagic = 0x2101;      // "USR\x1A\0" users.dat signature
inline constexpr uint16_t kRundTag = 0x0997;         // "RUND\0" PMAP compression tag
inline constexpr uint16_t kResTypeNames = 0x0741;    // "BNDL" 0741, "RRGB" 0746, "FFNT" 074b, "PMAP" 0750, "SSND" 0755, "OTHR" 075a
inline constexpr uint16_t kStrFFNT = 0x09A8;         // "FFNT" (font loader)

// --- Start-up strings (§4.3) -------------------------------------------------------------------
inline constexpr uint16_t kStrTmt256Dll = 0x05EB;    // DLL file names, see sys_resources_saves.md §4
inline constexpr uint16_t kStrTmt16Dll = 0x05F6;
inline constexpr uint16_t kStrTmtDll = 0x0600;
inline constexpr uint16_t kStrSoundDll0 = 0x0608;    // TM0SOUND.DLL; TMnSOUND.DLL at 0608 + 13*n
inline constexpr uint16_t kSoundDllStride = 13;

// --- Colours, effects (§4.5) -------------------------------------------------------------------
inline constexpr uint16_t kEgaRgb = 0x07D8;          // {u16 r, g, b}[16]: pen colours by EGA number
inline constexpr uint16_t kVga16Index = 0x0838;      // u16[16]: EGA number -> 16-colour pixel value
inline constexpr uint16_t kPaletteFirstLoad = 0x0858;
inline constexpr uint16_t kShadowRemap = 0x0876;     // u8[256] (unused shadow blit)
inline constexpr uint16_t kInvertSpecTemplate = 0x0976;
inline constexpr uint16_t kDissolveColOrder = 0x09AE;  // u8[16]
inline constexpr uint16_t kDissolveRowShift = 0x09BE;  // u8[16]
inline constexpr uint16_t kClipTop = 0x0864;         // initial clip rectangle (inclusive)
inline constexpr uint16_t kClipBottom = 0x0866;
inline constexpr uint16_t kClipLeft = 0x0868;
inline constexpr uint16_t kClipRight = 0x086A;

// --- Music (§4.6) ------------------------------------------------------------------------------
inline constexpr uint16_t kMusicIds = 0x09FA;        // i16[10] = 9000..9009
inline constexpr uint16_t kMusicLoopPos = 0x0A0E;    // i16[10]
inline constexpr uint16_t kMusicFiles = 0x0A22;      // far char*[10], placeholder first byte
inline constexpr uint16_t kStrSoundsDir = 0x0B16;    // "SOUNDS\"

// --- Dialog footer and button strings (§4.7) ---------------------------------------------------
inline constexpr uint16_t kStrFooter1 = 0x0B24;
inline constexpr uint16_t kStrFooter2 = 0x0B31;
inline constexpr uint16_t kStrBtnPrevPage = 0x0B40;
inline constexpr uint16_t kStrBtnNextPage = 0x0B4F;
inline constexpr uint16_t kStrBtnDone = 0x0B5D;
inline constexpr uint16_t kStrBtnCancel = 0x0B62;
inline constexpr uint16_t kStrBtnYes = 0x0B69;
inline constexpr uint16_t kStrBtnNo = 0x0B6D;

// --- Option flags and player animation tables (§4.8) -------------------------------------------
inline constexpr uint16_t kSoundOn = 0x0B96;
inline constexpr uint16_t kVoiceOn = 0x0B97;
inline constexpr uint16_t kNetSwingFrames = 0x0BA0;     // u8[12], 1-based
inline constexpr uint16_t kEmptySwingFrames = 0x0BAC;   // u8[12], 1-based
inline constexpr uint16_t kCoinTossFrames = 0x0BB8;     // u8[14], 1-based
inline constexpr uint16_t kCoinFxFrames = 0x0BC6;       // u8[14]
inline constexpr uint16_t kDoorFrames = 0x0BD4;         // u8[3], 1-based
inline constexpr uint16_t kBumpFrames = 0x0BD7;         // u8[13], 1-based
inline constexpr uint16_t kBumpDxRight = 0x0BE4;        // i16[13]
inline constexpr uint16_t kBumpDxLeft = 0x0BFE;         // i16[13]
inline constexpr uint16_t kObstacleMinXRight = 0x0C18;  // i16[7]
inline constexpr uint16_t kObstacleMaxXLeft = 0x0C26;   // i16[7]
inline constexpr uint16_t kNextFrame = 0x0C34;          // u8[85]
inline constexpr uint16_t kFrameDxRight = 0x0C89;       // i16[85]
inline constexpr uint16_t kFrameDxLeft = 0x0D33;        // i16[85]
inline constexpr uint16_t kFrameY = 0x0DDD;             // i16[85], absolute y
inline constexpr uint16_t kShopWalkX = 0x0E89;          // i16[3]
inline constexpr uint16_t kRoom9RankHints = 0x0E8F;     // i16[8]

// --- HUD, ranks, celebration (§4.9) ------------------------------------------------------------
inline constexpr uint16_t kRankThresholds = 0x0EA0;     // u32[8]
inline constexpr uint16_t kGemsNeeded = 0x0EC3;         // u8[4]
inline constexpr uint16_t kGemX = 0x0EC7;               // i16[4][5]
inline constexpr uint16_t kGemY = 0x0EEF;               // i16[4][5]
inline constexpr uint16_t kStrAnswerPrompt = 0x0F17;
inline constexpr uint16_t kClueAbortKeys = 0x0F54;      // u16[3]
inline constexpr uint16_t kTmplCoins = 0x0F5A;
inline constexpr uint16_t kTmplNets = 0x0F60;
inline constexpr uint16_t kTmplPoints = 0x0F66;
inline constexpr uint16_t kRankThresholdLabels = 0x0F8C;  // far char*[7]
inline constexpr uint16_t kRankNames = 0x0FA8;          // far char*[8]
inline constexpr uint16_t kRankPromoHints = 0x0FC8;     // i16[8]
inline constexpr uint16_t kRankStayHints = 0x0FD8;      // i16[8]
inline constexpr uint16_t kFmtName = 0x0FE8;
inline constexpr uint16_t kFmtTotalTreasures = 0x0FEE;

// --- Net shop, clue words (§4.10) --------------------------------------------------------------
inline constexpr uint16_t kNetSignX = 0x1066;           // i16[3]
inline constexpr uint16_t kStrNoun = 0x1079;            // far char*[16], 1-based
inline constexpr uint16_t kStrNounPlural = 0x10B9;      // far char*[16], 1-based
inline constexpr uint16_t kStrCount = 0x10F9;           // far char*[8], 1-based
inline constexpr uint16_t kStrSize = 0x1119;            // far char*[5], 1-based
inline constexpr uint16_t kStrShape = 0x112D;           // far char*[5], 1-based

// --- Coin pile, GameMain constants (§4.11) -----------------------------------------------------
inline constexpr uint16_t kElfCountByTier = 0x128E;     // u8[4]
inline constexpr uint16_t kCursorIds = 0x1292;          // u16[4]

// --- Menus (§4.12) -----------------------------------------------------------------------------
inline constexpr uint16_t kMenuBar = 0x13BF;            // MenuBarDef
inline constexpr uint16_t kMenuPopups = 0x1383;         // MenuPopupDef[3], 0x14 bytes each

// --- Hints, title (§4.13) ----------------------------------------------------------------------
inline constexpr uint16_t kHintBit = 0x13D0;            // i16[34]
inline constexpr uint16_t kHintVoiceStopKeys = 0x1414;  // u16[4]
inline constexpr uint16_t kTitleVoiceFirst = 0x141C;    // i16[4] (code base 141a)
inline constexpr uint16_t kTitleVoiceCount = 0x1424;    // i16[4] (code base 1422)

// --- Dialogs (DlgDesc, 0x1a bytes; §4.14, §4.15) -----------------------------------------------
inline constexpr uint16_t kDlgStartAnotherGame = 0x14D6;
inline constexpr uint16_t kDlgExit = 0x1533;
inline constexpr uint16_t kDlgNewPlayerMessages = 0x15CC;
inline constexpr uint16_t kDlgMusicAndSound = 0x1625;
inline constexpr uint16_t kDlgVoice = 0x163F;
inline constexpr uint16_t kDlgAbout = 0x1919;
inline constexpr uint16_t kDlgWriteError = 0x2084;
inline constexpr uint16_t kDlgAlreadyOnList = 0x2129;
inline constexpr uint16_t kDlgRemovePlayer = 0x21E6;
inline constexpr uint16_t kDlgListFull = 0x2200;
inline constexpr uint16_t kDlgEnterName = 0x221A;
inline constexpr uint16_t kDlgTitleExit = 0x2234;

// --- Quiz (§4.15) ------------------------------------------------------------------------------
inline constexpr uint16_t kQuizVoiceBase = 0x1990;      // u16[360]
inline constexpr uint16_t kQuizVoiceBank = 0x1C60;      // u8[359]
inline constexpr uint16_t kQuizSilentAnswers = 0x1DC7;  // i16[16]
inline constexpr uint16_t kQuizTierStart = 0x1DE7;      // i16[5]
inline constexpr uint16_t kQuizPraiseText = 0x1DF1;     // far char*[4]
inline constexpr uint16_t kQuizResultStopKeys = 0x1E01; // u16[3]
inline constexpr uint16_t kQuizSorryText = 0x1E07;      // far char*[4]
inline constexpr uint16_t kQuizSorryStopKeys = 0x1E17;  // u16[3]
inline constexpr uint16_t kQuizLineStopKeys = 0x1E1D;   // u16[19]
inline constexpr uint16_t kQuizAnswerStopKeys = 0x1E43; // u16[19]
inline constexpr uint16_t kStrQuizPeriod = 0x1F59;

// --- Sign-in (§4.15) ---------------------------------------------------------------------------
inline constexpr uint16_t kFmtAlreadyOnList = 0x2106;
inline constexpr uint16_t kFmtRemoveConfirm = 0x2143;
inline constexpr uint16_t kPlayerName = 0x224E;         // char[9]
inline constexpr uint16_t kRowH = 0x2257;               // i16
inline constexpr uint16_t kStrNameBlank = 0x2390;
inline constexpr uint16_t kStrStartNewPlayer = 0x23A2;
inline constexpr uint16_t kStrStartNewPlayer2 = 0x23B3;
inline constexpr uint16_t kStrRankTrainee = 0x23C4;
inline constexpr uint16_t kStrRankStar = 0x23CC;
inline constexpr uint16_t kStrRankChampion = 0x23CE;

// --- Rooms (§4.16) -----------------------------------------------------------------------------
inline constexpr uint16_t kRooms = 0x23D8;              // RoomDef[50], 0x1c bytes each
inline constexpr int kRoomCount = 50;
inline constexpr uint16_t kRoomSize = 0x1C;

// --- Castle climb (§4.17; several 1-based tables) ----------------------------------------------
inline constexpr uint16_t kClimbLayouts = 0x2950;       // i16[4][4][5]
inline constexpr uint16_t kClimbColX = 0x29F0;          // i16[5]
inline constexpr uint16_t kClimbFrames = 0x29FA;
inline constexpr uint16_t kClimbDx = 0x2A02;
inline constexpr uint16_t kClimbUpDy = 0x2A0A;
inline constexpr uint16_t kClimbDownDy = 0x2A12;
inline constexpr uint16_t kClimbJumpFrames = 0x2A1A;
inline constexpr uint16_t kClimbJumpDxLeft = 0x2A24;
inline constexpr uint16_t kClimbJumpDxRight = 0x2A2C;
inline constexpr uint16_t kClimbJumpDy = 0x2A36;
inline constexpr uint16_t kClimbFallFrames = 0x2A40;    // step 1..7 (code base 2a3e)
inline constexpr uint16_t kClimbFallDx = 0x2A4E;
inline constexpr uint16_t kClimbFallDy = 0x2A5C;
inline constexpr uint16_t kClimbLandFrames = 0x2A6A;
inline constexpr uint16_t kClimbLandDx = 0x2A76;
inline constexpr uint16_t kClimbLandY = 0x2A82;
inline constexpr uint16_t kClimbCreatSeq = 0x2A8E;
inline constexpr uint16_t kClimbCreatSpawnPermille = 0x2AAA;  // i16[4]

// --- Elves (§4.18) -----------------------------------------------------------------------------
inline constexpr uint16_t kElfLevelStartRoom = 0x2AC8;  // u8[3]
inline constexpr uint16_t kElfBeh0Frames = 0x2ACB;      // behaviour scripts 2acb..2c1f
inline constexpr uint16_t kElfBeh1Frames = 0x2B1C;
inline constexpr uint16_t kElfBeh2Frames = 0x2B6D;
inline constexpr uint16_t kElfBeh3Frames = 0x2BA0;
inline constexpr uint16_t kElfBeh4Frames = 0x2BF1;
inline constexpr uint16_t kElfBeh4Sfx = 0x2C1F;
inline constexpr uint16_t kElfNetFrames = 0x2C31;       // i8[10]
inline constexpr uint16_t kElfNetDx = 0x2C3B;
inline constexpr uint16_t kElfNetY = 0x2C4D;
inline constexpr uint16_t kElfThrowDxR = 0x2C5F;
inline constexpr uint16_t kElfThrowDxL = 0x2C69;
inline constexpr uint16_t kElfThrowY = 0x2C73;
inline constexpr uint16_t kElfProjY = 0x2C7D;           // i16[9]

// --- Help, cut-scenes, treasure hall, room scene (§4.19) ---------------------------------------
inline constexpr uint16_t kHelpDesc = 0x2CFE;           // PagerDesc
inline constexpr uint16_t kStrHelpTitles = 0x2D1C;
inline constexpr uint16_t kCutBndlIds = 0x2D6D;         // u16[2]
inline constexpr uint16_t kCutScript2Ids = 0x2D71;      // u16[2]
inline constexpr uint16_t kCutBgIds = 0x2D75;           // u16[2]
inline constexpr uint16_t kHallPedX = 0x2D90;           // i16[6]
inline constexpr uint16_t kHallPedY = 0x2D9C;           // i16[6]
inline constexpr uint16_t kHallPedFrameBase = 0x2DA8;   // i16[6]
inline constexpr uint16_t kSceneSlotX = 0x2DD4;         // i16[3]
inline constexpr uint16_t kSceneRevealX = 0x2DDA;       // i16[3]

// --- RTL (§4.20) -------------------------------------------------------------------------------
inline constexpr uint16_t kRandSeed = 0x3044;           // u32, initial value 1

}  // namespace tmport::dg
