#!/usr/bin/env python3
"""dgroup.py - extract and decode DGROUP (automatic data segment) of TMTWINCD.EXE.

Treasure Mountain! (Windows CD edition, TMTWINCD.EXE v1.01, Borland C++ 1993).

DGROUP is NE segment 56 (Ghidra selector 11b8).  In the shipped EXE it starts at
file offset 0x23c00 and is 0x66a2 (26274) bytes long; bytes 0x0000..0x387f are
initialised data (_DATA), 0x3880..0x66a1 are zero bytes that C0W clears again at
start-up (_BSS).  The NE header adds a 0x1000-byte local heap and a 0x1400-byte
stack after that.  Far pointers stored in DGROUP are NE relocation records
(198 of them, all "far pointer, internal reference"); the raw words in the image
are placeholders (offset 0xFFFF = end of fix-up chain, selector 0), so this tool
resolves every pointer through the relocation table.

The tool
  * locates DGROUP through the NE header (auto-data segment) - no hard-coded offset,
  * applies the relocation table,
  * carries the complete region map of DGROUP (REGIONS below: offset, size, name,
    type, owning code segment(s), summary) and decodes every initialised region
    generically,
  * decodes the game-content tables semantically (rooms, clue words + voice ids,
    ranks, music, menus, dialogs, hints, quiz voice tables, player / climb / elf
    animation tables, ...),
  * optionally scans the Ghidra disassembly export to list which functions
    reference each region (--asm), and writes the names TSV (--names-out).

Usage:
  python -P dgroup.py [--exe TMTWINCD.EXE] [--out dgroup.json]
                      [--asm disassembly.asm] [--names-out dgroup.tsv]
                      [--dump-bin dgroup.bin] [--check]

Only reads the user's original file; writes JSON/TSV/bin outputs.
"""
import argparse
import bisect
import collections
import json
import os
import re
import struct
import sys

DEFAULT_EXE = r'C:\Git\TM\extracted\wincd\TMTWINCD\TMTWINCD.EXE'
DEFAULT_OUT = r'C:\Git\TM\extracted\previews\dgroup.json'
DEFAULT_ASM = r'C:\Git\TM\re\win\ghidra_export\disassembly.asm'
DGSEL = 0x11b8          # Ghidra selector of DGROUP (segment 56)
BSS_START = 0x3880      # first byte of _BSS (cleared by C0W entry 1000:0000)

# ----------------------------------------------------------------------------
# Region map.  One line per region:  off | size | name | type | owner | summary
#   off/size hex, owner = referencing code
#   segment(s) (Ghidra selectors), '-' if none found.
# Type grammar for the generic decoder:
#   u8 i8 u16 i16 u32 i32 far near cstr strs bytes  (+ [n] or [n][m])
#   anything else (struct names) is emitted as hex and decoded in TABLES.
# Values in the summary: "init X" = initial value in the EXE image.
# ----------------------------------------------------------------------------
REGIONS_TXT = r"""
0000|10|win16_instanceHeader|bytes|KERNEL|Win16 reserved instance-data header (KERNEL: +06 pLocalHeap, +08 pAtomTable, +0A pStackTop, +0C pStackMin, +0E pStackBottom); zero in image
0010|4|eh_CtorCount|u32|1000,11a0|Borland EH: constructed-object counter (SS:0010; SS==DS in the EXE)
0014|2|eh_FrameChainHead|u16|1000,11a0|Borland EH: head of registration-frame chain; set to FFFF by entry 1000:001b
0016|4|eh_VarsPtr|far|1000,11a0|Borland EH: far pointer to EH globals (set to 11b8:31b2 by 1000:2e99)
001a|2|eh_Magic|u16|1000|Borland EH: 0xFEED when SS:0010..0017 are valid
001c|14|rtl_reserved001c|bytes|-|zero; no reference found
0030|2|errno|i16|1000|C errno
0032|2|_protected|u16|1000|8 if GetWinFlags()&WF_PMODE
0034|2|_cpuType|u16|1000|2/3/4 = 286/386/486 (GetWinFlags)
0036|2|_8087|u16|1000|1 if coprocessor present
0038|2|_pszCmdLineSeg|u16|1000|selector of lpCmdLine (InitTask ES)
003a|2|_hInstance|u16|1000|instance handle (InitTask DI)
003c|2|_hPrevInstance|u16|1000|previous instance (InitTask SI)
003e|2|_pszCmdLineOff|u16|1000|offset of lpCmdLine (InitTask BX)
0040|2|_cmdShow|i16|1000|nCmdShow (InitTask DX)
0042|2|_osversion|u16|1000|DOS version (INT 21h AH=30h)
0044|1|rtl_unk0044|u8|-|zero; no reference found
0045|2|_WinAllocFlag|u16|1000|extra GlobalAlloc flags for malloc family (init 0)
0047|2|rtl_unk0047|u16|-|init 1; no reference found (Borland RTL)
0049|2b|s_BorlandCopyright|cstr|-|"Borland C++ - Copyright 1993 Borland Intl." (RTL signature)
0074|2|g_useWinG|i16|1008,1010,1018,1048,1058|1 = WinG back buffer, 0 = GDI DDB path (init 1; INI UseWinG)
0076|2|g_bufWidth|i16|1008|back-buffer width (512 at run time)
0078|2|g_bufHeight|i16|1008|back-buffer height (1055 at run time)
007a|2|unused007a|i16|-|zero; no reference found
007c|2|g_hdcWindow|u16|1008,1018|HDC of main window used by the WinG path
007e|2|g_hdcWinG|u16|1008|WinG DC
0080|4|g_wingBits|far|1008|pointer to WinG DIB bits
0084|2|g_dibOrientation|i16|1008|init -1 (top-down DIB: biHeight = -height)
0086|2|g_hbmWinGOld|u16|1008|bitmap originally selected into the WinG DC
0088|2|g_hWinGLib|u16|1008|WING.DLL module handle
008a|404|g_logPalTemplate|LOGPALETTE|1008,1048|LOGPALETTE {palVersion 0x300, palNumEntries 256, PALETTEENTRY[256]=0} template
048e|9|s_WING_DLL|cstr|1008|"WING.DLL" (LoadLibrary)
0497|1|pad0497|bytes|-|alignment
0498|2|g_initDone|i16|1010,1018|set at end of InitGraphicsAndData
049a|2|g_appActive|i16|1010,1028,1090|init 1; 0 while deactivated / in menu or dialog
049c|2|g_quitRequested|i16|1010,1028|set by WM_CLOSE / WM_QUERYENDSESSION
049e|2|g_holdMainLoop|i16|1010|init 1; cleared by WM_CREATE
04a0|2|g_unused04a0|i16|1010|init 1; cleared by WM_CREATE, never read
04a2|2|g_noResumeOnMenuExit|i16|1010|init 0, never written
04a4|2|g_isWin40|i16|1018|result of IsWindows40 (1018:07ff)
04a6|23c|strs_initApp|strs|1018|window class "RMPDEClass" (04a6, 04b1), title (04bc), INI dir (04cf) / file (04e3), dialog template names (04f0..05d9), INI section/keys/values, DLL names TMT256/TMT16/TMT/TM0..8SOUND (05eb..0670)
06e2|1|g_cursorForced|u8|1088,10c0,10e0|1 = force arrow cursor
06e3|2|g_mouseButtonDown|u16|1010,1020|left button held
06e5|4|g_mouseMoveHook|far|1020|callback after WM_MOUSEMOVE
06e9|2|g_curCursorIndex|i16|1020|last selected cursor (init -1)
06eb|1|pad06eb|bytes|-|alignment
06ec|2|g_pendingInputCode|u16|1028|one-slot input look-ahead
06ee|2|g_keyUpHeld|i16|1010,10e0,1168|arrow-key held flags (WM_KEYDOWN/UP)
06f0|2|g_keyDownHeld|i16|1010,10e0,1168|
06f2|2|g_keyLeftHeld|i16|1010,10e0,1168|
06f4|2|g_keyRightHeld|i16|1010,10e0,1168|
06f6|2|g_mouseX|i16|1010,1020,10e0,1168|mouse x in 512x384 game coordinates
06f8|2|g_mouseY|i16|1010,1020,10e0,1160,1168|mouse y in game coordinates
06fa|2|unused06fa|i16|-|zero; no reference found
06fc|3a|strs_1030|strs|1030|NewPtr failure message box text/caption (06fc, 0713), "BNDL" x2 (072c, 0731)
0736|4|g_resCachedTree|far|1038|only touched by unreferenced 1038:0000
073a|2|g_resLockedHandle|u16|1038|resource handle locked by Res_LockInPlace
073c|98|strs_1038|strs|1038|Mac resource type names BNDL/RRGB/FFNT/PMAP/SSND/OTHR (073c..075a), out-of-memory messages (075f, 0776, 07a3, 07ba), "BNDL" compares (078f..079e)
07d4|2|g_hPalette|u16|1008,1010,1048,1058|game logical palette
07d6|2|g_curPaletteId|i16|1048|last RRGB id loaded (init -1)
07d8|60|g_egaRGB|u16[16][3]|1008,1048,1058|16 EGA colours {r,g,b} 0..255
0838|20|g_vga16Index|u16[16]|1048,1058|16-colour-mode pixel value per EGA colour
0858|2|g_paletteFirstLoad|u16|1048|init 1; Pal_SetFromRGB acts once
085a|5|s_RRGB|cstr|1048|"RRGB"
085f|1|pad085f|bytes|-|alignment
0860|2|g_gfxConst4|i16|1160,1198|init 4, never written (fade stubs run only if 3 or 4)
0862|2|g_dirtyEnabled|i16|1058,10b8,10c0,10d8,10e0,10f0,1138,1150,1168|1 = record dirty rects
0864|2|g_clipTop|i16|1058,10b8,10c8,10e0,10f0|inclusive clip rectangle (init 0,0x180,0,0x200)
0866|2|g_clipBottom|i16|1058,10b8,10c8,10e0,10f0|
0868|2|g_clipLeft|i16|1058,10b8,10c8,10e0|
086a|2|g_clipRight|i16|1058,10b8,10c8,10e0|
086c|2|g_paletteDevice|i16|1010,1018,1038,1048,1058,1068|1 = 256-colour path (TMT256), 0 = 16-colour (TMT16); init 1
086e|2|g_viewOriginX|i16|1008,1010,1018,1020,1058|client x of game pixel (0,0)
0870|2|g_viewOriginY|i16|1008,1010,1018,1020,1058|client y of game pixel (0,0)
0872|2|g_hGfxLib|u16|1010,1018,1038|TMT256.DLL or TMT16.DLL handle
0874|2|g_dirtyCount|i16|1058|entries in g_dirtyRects
0876|100|g_shadowRemap|u8[256]|1048,1058|colour remap for shadow / masked blits (logical indices, converted once by 1048:010a)
0976|21|g_invertSpecTemplate|u8[33]|1058|{0x10, 32 x 0}: remap spec template of Gfx_InvertRect16
0997|5|s_RUND|cstr|1058|"RUND" PMAP compression tag
099c|4|g_rowPitch|u32|1060|512 (offscreen row width in pixels)
09a0|2|g_tmAveCharWidth|i16|1010|WM_CREATE text metrics (unused)
09a2|2|g_tmMaxCharWidth|i16|1010|
09a4|2|g_tmHeightExt|i16|1010|
09a6|2|g_tmHeight|i16|1010|
09a8|5|s_FFNT|cstr|1068|"FFNT"
09ad|1|pad09ad|bytes|-|alignment
09ae|10|g_dissolveColOrder|u8[16]|1070|Screen_DissolveCopy column phase per pass
09be|10|g_dissolveRowShift|u8[16]|1070|Screen_DissolveCopy column shift per row (y%16)
09ce|4|g_menuDef|far|1078|current MenuBarDef (NULL none)
09d2|2|g_menusEnabled|i16|1078|
09d4|2|g_menuLock|i16|1038,1078|nonzero while resource loading pumps messages
09d6|3|s_fmt_s1|cstr|1078|"%s"
09d9|3|s_fmt_s2|cstr|1078|"%s"
09dc|2|g_inDialog|i16|1010,1080|1 inside DialogBox
09de|12|g_hSoundLib|u16[9]|1010,1018,1038|TM0..TM8SOUND.DLL handles
09f0|2|g_hTmtLib|u16|1010,1018,1038,1048|TMT.DLL handle
09f2|2|g_waveAvail|i16|1088|waveOutGetNumDevs()!=0
09f4|4|g_waveFmtPtr|far|1088|-> 4ac2 while a wave device is open
09f8|2|g_waveState|i16|1088|1 playing, -1 idle (init -1)
09fa|14|g_musicIds|i16[10]|1090|music ids 9000..9009
0a0e|14|g_musicLoopPos|i16[10]|1090|restart position at end of track; -1 = play once
0a22|28|g_musicFiles|far[10]|1090|-> " castle.mid".." title.mid"; byte 0 patched to 'B' or 'E'
0a4a|2|g_midiOk|i16|1090|1 after Midi_Init
0a4c|2|g_curMusic|i16|1090|playing music id, -1 none (init -1)
0a4e|2|g_midiPaused|i16|1090|
0a50|2|g_seqDev|u16|1090|MCI "sequencer" device id
0a52|2|g_trackDev|u16|1090|MCI element device id
0a54|2|g_midiSavedPos|i16|1090|saved/loop position (16-bit)
0a56|76|strs_musicFiles|strs|1090|10 MIDI file names, each with a leading placeholder byte
0acc|14|s_midimapCfg|cstr|1090|"\system\midimap.cfg"
0ae0|2c|strs_fmSynthNames|strs|1090|6 driver-name substrings that select the B (base/FM) MIDI set
0b0c|a|s_sequencer|cstr|1090|"sequencer"
0b16|8|s_SOUNDS|cstr|1090|"SOUNDS\"
0b1e|2|g_animEraseCount|i16|10b8|entries in g_animEraseRects
0b20|2|g_animSpriteCount|i16|10b8|entries in g_animSprites
0b22|2|g_animUpdateCount|i16|10b8|entries in g_animUpdateRects
0b24|4c|strs_dialogButtons|strs|10c0|dialog footers (0b24 glyph 0x86 "to Go Back", 0b31 "Click to Go On") and button labels (0b40 Go Back a Page, 0b4f See Next Page, 0b5d Done, 0b62 Cancel, 0b69 Yes, 0b6d No)
0b70|4|g_dlgSaveBuf|far|10c0|save-under pixmap of the open dialog
0b74|4|unused0b74|bytes|-|zero; no reference found
0b78|1e|g_bndlPagerDesc|PagerDesc|10c8|static PagerDesc (dead code 10c8:0000), arrays -> 5c74/5c90/5cac/5cba
0b96|1|g_soundOn|u8|1128,1140,1160,1170|music+sound effects enabled (init 1; user record +0x0e)
0b97|1|g_voiceOn|u8|10e8,1128,1130,1138,1140,1150,1160|voice narration enabled (init 1; user record +0x0f)
0b98|1|g_gateWalkStarted|u8|10e0,1178|auto-walk to the gate started (elves leave)
0b99|1|g_inputLocked|u8|10e0,10f8|player input ignored (auto-walk)
0b9a|1|g_jumping|u8|10e0,1118,1168|player in jump (frames 12..21)
0b9b|1|g_unused0b9b|u8|10e0|cleared each frame, never set
0b9c|1|g_rankPromoted|u8|10e0,10f0,1118|rank changed during this climb
0b9d|1|g_redrawRequest|u8|10e0|0 none, 1 rebuild room, 2 restore play area
0b9e|1|g_coinTargetSlot|u8|10e0,1118,1198|slot hit by coin toss; 0xFF none (init 0xFF)
0b9f|1|g_elfQuizResult|u8|10e0,1178|result of the elf question
0ba0|c|g_netSwingFrames|u8[12]|10e0,1168|player frame for net-swing step 1..11 (index 0 unused)
0bac|c|g_emptySwingFrames|u8[12]|10e0,1168|player frame for empty-hand swing step 1..11
0bb8|e|g_coinTossFrames|u8[14]|10e0,1168|player frame for coin-toss step 1..13
0bc6|e|g_coinFxFrames|u8[14]|10e0,1168|FX sprite frame for coin-toss step (0 = none)
0bd4|3|g_doorFrames|u8[3]|10e0|door-entry frame for step 1..2
0bd7|d|g_bumpFrames|u8[13]|10e0|bump/fall-back frame for step 1..12
0be4|1a|g_bumpDxRight|i16[13]|10e0|bump x delta (facing right) per step
0bfe|1a|g_bumpDxLeft|i16[13]|10e0|bump x delta (facing left, subtracted) per step
0c18|e|g_obstacleMinXRight|i16[7]|10e0|room 36 obstacle: min x per jump frame 15..21 (facing right)
0c26|e|g_obstacleMaxXLeft|i16[7]|10e0|room 36 obstacle: max x per jump frame 15..21 (facing left)
0c34|55|g_nextFrame|u8[85]|10e0,1168|player walk/jump successor frame (85 frames of BNDL 300 incl. extras)
0c89|aa|g_frameDxRight|i16[85]|10e0,1168|x delta when entering frame (facing right)
0d33|aa|g_frameDxLeft|i16[85]|10e0,1168|x delta when entering frame (facing left, subtracted)
0ddd|aa|g_frameY|i16[85]|10e0,1168|absolute player y per frame
0e87|2|g_mouseWalkLatched|u16|10e0|mouse-driven movement latched
0e89|6|g_shopWalkX|i16[3]|10e0|auto-walk x to the net shop per level
0e8f|10|g_room9RankHints|i16[8]|10e0|hint event per rank shown in room 9 on later climbs
0e9f|1|pad0e9f|bytes|-|alignment
0ea0|20|g_rankThresholds|u32[8]|10e8|treasure-point thresholds of ranks 0..7
0ec0|1|g_clueBoxMode|u8|10e8|0 clue list, 1 answer prompt, 2 CONTINUE
0ec1|1|g_showCoins|u8|10e8|init 1, never changed
0ec2|1|g_showNets|u8|10e8|init 1, never changed
0ec3|4|g_gemsNeeded|u8[4]|10e8,1198|treasures per climb by rank tier
0ec7|28|g_gemX|i16[4][5]|10e8|HUD gem icon x by tier/slot
0eef|28|g_gemY|i16[4][5]|10e8|HUD gem icon y by tier/slot
0f17|24|str_AnswerPrompt|cstr|10e8|FF 05 FF "Click on / an answer / to choose it." (clue box mode 1)
0f3b|19|str_ClickToGoOn|cstr|-|FF 05 FF "Click mouse / to go on." (unreferenced)
0f54|6|g_clueAbortKeys|u16[3]|10e8|{0x0d,0x1b,0}
0f5a|6|tmpl_Coins|bytes|10e8|FF 05 FF "00" NUL - coin counter template
0f60|6|tmpl_Nets|bytes|10e8|FF 05 FF "00" NUL - net counter template
0f66|9|tmpl_Points|bytes|10e8|FF 05 FF "00000" NUL - treasure points template
0f6f|1|pad0f6f|bytes|-|alignment
0f70|4|g_celebBndl323|far|10f0|BNDL 323 (celebration)
0f74|4|g_celebScript20301|far|10f0|OTHR 20301 animation script
0f78|4|g_treasureBndl|far|10f0|BNDL 330+((treasuresFound-1)%43)
0f7c|4|g_celebScript20302|far|10f0|OTHR 20302 animation script
0f80|4|g_celebBndl324|far|10f0|BNDL 324 (champion)
0f84|4|g_celebBndl325|far|10f0|BNDL 325 (final)
0f88|4|g_rankImage|far|10f0|PMAP 2381 (rank screen)
0f8c|1c|g_rankThresholdLabels|far[7]|10f0|-> "5".."300" (rank screen labels)
0fa8|20|g_rankNames|far[8]|10f0|-> rank names (FF 05 FF prefix) Trainee, 1..6 stars, Champion
0fc8|10|g_rankPromoHints|i16[8]|10f0|hint event when promoted to rank r (-1 none)
0fd8|10|g_rankStayHints|i16[8]|10f0|hint event when the climb ends without promotion at rank r
0fe8|6|fmt_Name|cstr|10f0|FF 05 FF "%s"
0fee|17|fmt_TotalTreasures|cstr|10f0|FF 05 FF "Total Treasures %lu"
1005|18|strs_rankThresholds|strs|10f0|"5","25","70","115","170","230","300"
101d|46|strs_rankNames|strs|10f0|8 rank name strings
1063|1|pad1063|bytes|-|alignment
1064|2|g_netShopState|i16|10e0,10f8|NetShop_Tick state 0..7
1066|6|g_netSignX|i16[3]|10f8|net-shop sign x per level
106c|c|g_spotPict|far[3]|1100,1198|pictures (PMAP 1400+kind) of the 3 hiding places of the current room
1078|1|g_numClues|u8|10e8,1118,1150,1160|clue words collected on the current level (0..3)
1079|40|g_strNoun|far[16]|1100|noun strings, index 1..15 ([0] = "")
10b9|40|g_strNounPlural|far[16]|1100|plural noun strings, index 1..15
10f9|20|g_strCount|far[8]|1100|count words index 1..7
1119|14|g_strSize|far[5]|1100|size words index 1..4
112d|14|g_strShape|far[5]|1100|shape words index 1..4
1141|13d|strs_clueWords|strs|1100|"" + 15 nouns + 15 plurals + 7 counts + 4 sizes + 4 shapes
127e|2|unk127e|i16|-|init -1; no reference found (tail of module 1100)
1280|8|unk1280|bytes|-|zero; no reference found
1288|1|g_coinPileRoom|u8|1110|room of the rescue coin pile; 0xFF none, 0xFE armed (init 0xFF)
1289|4|g_coinPilePmap|far|1110|PMAP 1699
128d|1|pad128d|bytes|-|alignment
128e|4|g_elfCountByTier|u8[4]|1118|elves per level by rank tier
1292|8|g_cursorIds|u16[4]|1118|EXE cursor resource ids for Cursor_LoadSet, 0xFFFF-terminated
129a|63|strs_menuHelp|strs|1120|"&Help" + 5 items separated by LF (modified in place)
12fd|14|g_menuHelpCmds|far[5]|1120|Help item handlers
1311|27|strs_menuFile|strs|1120|"&File" + 3 items
1338|c|g_menuFileCmds|far[3]|1120|File item handlers
1344|33|strs_menuOptions|strs|1120|"&Options" + 3 items
1377|c|g_menuOptionCmds|far[3]|1120|Options item handlers
1383|3c|g_menuPopups|MenuPopupDef[3]|1120|3 popup records (0x14 bytes)
13bf|6|g_gameMenuBar|MenuBarDef|1120|{i16 count=3; far popups}
13c5|1|pad13c5|bytes|-|alignment
13c6|4|g_musicActive|u32|1128|1 after Music_Select
13ca|2|g_musicId|i16|1128|tune id chosen by Music_Select (init -1)
13cc|1|unk13cc|u8|-|zero; no reference found
13cd|1|g_unk13cd|u8|1150,1160|written 0/1/0xFF, never read (init 0xFF)
13ce|2|g_hintPending|i16|10e0,10f0,1130,1140|queued hint event, -1 none (init -1)
13d0|44|g_hintBit|i16[34]|1130|hint event -> bit in g_hintUnseen, -1 = always shown
1414|8|g_hintVoiceStopKeys|u16[4]|1130|{0x0d,0x1b,0x4b0,0}
141c|8|g_titleVoiceFirst|i16[4]|1138|first narration SSND per story page 1..4 (code base 141a, 1-based)
1424|8|g_titleVoiceCount|i16[4]|1138|narration clip count per page 1..4 (code base 1422)
142c|aa|s_textStartAnother|cstr|1140|body of "Start Another Game" dialog
14d6|1a|g_dlgStartAnother|DlgDesc|1140|yes/no dialog
14f0|43|s_textExitGame|cstr|1140|body of "Exit" (in-game)
1533|1a|g_dlgExitGame|DlgDesc|1140|yes/no dialog
154d|7f|s_textNewPlayerMsgs|cstr|1140|body of "New Player Messages"
15cc|1a|g_dlgNewPlayerMsgs|DlgDesc|1140|yes/no; +0x0b preset with g_hintsOn
15e6|24|s_textMusicSound|cstr|1140|body of "Music and Sound"
160a|1b|s_textVoice|cstr|1140|body of "Voice"
1625|1a|g_dlgMusicSound|DlgDesc|1140|yes/no; +0x0b preset with g_soundOn
163f|1a|g_dlgVoice|DlgDesc|1140|yes/no; +0x0b preset with g_voiceOn
1659|2c0|s_textAbout|cstr|1140|product blurb of "About Treasure Mountain!"
1919|1a|g_dlgAbout|DlgDesc|1140|info box (kind 1) with fixed geometry
1933|5b|strs_dialogCaptions|strs|1140|captions of the 6 dialogs above
198e|2|g_quizReadingAll|i16|1150|set when Quiz_SpeakAll starts
1990|2d0|g_quizVoiceBase|u16[360]|1150|first narration SSND per question 0..358; [359]=6616 end sentinel
1c60|167|g_quizVoiceBank|u8[359]|1150|narrator bank (1..3) per question -> 7300/7400/7500
1dc7|20|g_quizSilentAnswers|i16[16]|1150|questions whose answers are never spoken
1de7|a|g_quizTierStart|i16[5]|1150|question tier start indices
1df1|10|g_quizPraiseText|far[4]|1150|correct-answer texts
1e01|6|g_quizResultStopKeys|u16[3]|1150|{0x1b,0x0d,0}
1e07|10|g_quizSorryText|far[4]|1150|wrong-answer texts
1e17|6|g_quizSorryStopKeys|u16[3]|1150|{0x1b,0x0d,0}
1e1d|26|g_quizLineStopKeys|u16[19]|1150|input codes that interrupt question narration
1e43|26|g_quizAnswerStopKeys|u16[19]|1150|same list, used for answers
1e69|1df|strs_quizResults|strs|1150|4 praise + 4 sorry texts (FF 05 FF prefix) and "." (1f59)
2048|3c|s_textWriteError|cstr|1158|body of "Write Error"
2084|1a|g_dlgWriteError|DlgDesc|1158|info box
209e|c|s_capWriteError|cstr|1158|"Write Error"
20aa|a|s_dirTlcwin|cstr|1158|"c:\tlcwin"
20b4|13|s_dirTmtwincd|cstr|1158|"c:\tlcwin\tmtwincd"
20c7|1d|s_usersDatR|cstr|1158|users.dat path (read)
20e4|1d|s_usersDatW|cstr|1158|users.dat path (write)
2101|5|g_usersMagic|bytes|1158|"USR" 0x1A + NUL: users.dat signature
2106|23|s_fmtAlreadyOnList|cstr|1160|printf format "%s ... already on the list"
2129|1a|g_dlgAlreadyOnList|DlgDesc|1160|OK box; body pointer patched at 2130
2143|a3|s_fmtRemoveConfirm|cstr|1160|printf format for the remove confirmation
21e6|1a|g_dlgRemovePlayer|DlgDesc|1160|yes/no; body pointer patched at 21ed
2200|1a|g_dlgListFull|DlgDesc|1160|OK box
221a|1a|g_dlgEnterName|DlgDesc|1160|non-modal name-entry box
2234|1a|g_dlgTitleExit|DlgDesc|1138,1160|yes/no "Exit" on title / sign-in
224e|9|g_playerName|bytes|10f0,1160|current player name, '_' = empty cell
2257|2|g_rowH|i16|1160|sign-in row height (init 25)
2259|1|g_signInBackdrop|u8|1160|backdrop 2100 drawn
225a|17|s_capRemovePlayer|cstr|1160|"Remove A Player's Name"
2271|83|s_textListFull|cstr|1160|"The list is full..."
22f4|16|s_capEnterName|cstr|1160|"Enter New Player Name"
230a|32|s_textEnterName|cstr|1160|"Type your name in the box below..."
233c|5|s_capExit|cstr|1160|"Exit"
2341|4f|s_textTitleExit|cstr|1160|"You are about to exit the program..."
2390|9|s_nameBlank|cstr|1160|"________"
2399|9|s_nameBlank2|cstr|1160|"________"
23a2|11|s_startNewPlayer|cstr|1160|"Start New Player" (measured)
23b3|11|s_startNewPlayer2|cstr|1160|"Start New Player" (drawn)
23c4|8|s_rankTrainee|cstr|1160|"Trainee"
23cc|2|s_rankStar|cstr|1160|"*"
23ce|9|s_rankChampion|cstr|1160|"Champion"
23d7|1|pad23d7|bytes|-|alignment
23d8|578|g_rooms|RoomDef[50]|10e0,10e8,1100,1110,1118,1160,1178,1188,1190,1198|50 room records of 28 bytes; slot fields set at run time (1100), hall rooms 0..8 exits/bg rebuilt by 1190:0000
2950|a0|cl_layouts|i16[4][4][5]|1168|ladder layouts [tier][screen][column]: 0 none, 1 full, 2 lower half
29f0|a|cl_colX|i16[5]|1168|ladder column centre x
29fa|8|cl_climbFrames|i16[4]|1168|climb cycle frames (phase 0..3)
2a02|8|cl_climbDx|i16[4]|1168|climb x offset per phase
2a0a|8|cl_climbUpDy|i16[4]|1168|climb-up dy per phase
2a12|8|cl_climbDownDy|i16[4]|1168|climb-down dy per phase
2a1a|a|cl_jumpFrames|i16[5]|1168|sideways jump frames (index 0..4)
2a24|8|cl_jumpDxLeft|i16[4]|1168|left jump |dx| (index 0..3)
2a2c|a|cl_jumpDxRight|i16[5]|1168|right jump dx (index 1..4; [0] = 0)
2a36|a|cl_jumpDy|i16[5]|1168|jump dy (index 0..4)
2a40|e|cl_fallFrames|i16[7]|1168|fall frames, steps 1..7 (code base 2a3e)
2a4e|e|cl_fallDx|i16[7]|1168|fall dx, steps 1..7 (code base 2a4c)
2a5c|e|cl_fallDy|i16[7]|1168|fall dy before +32, steps 1..7 (code base 2a5a)
2a6a|c|cl_landFrames|i16[6]|1168|landing frames, steps 1..6 (code base 2a68)
2a76|c|cl_landDx|i16[6]|1168|landing dx, steps 1..6 (code base 2a74)
2a82|c|cl_landY|i16[6]|1168|landing absolute y, steps 1..6 (code base 2a80)
2a8e|18|cl_creatSeq|i16[12]|1168|creature frame per tick 1..12, -1 hidden (code base 2a8c)
2aa6|2|cl_walkMouseFollow|i16|1168|
2aa8|2|cl_climbMouseFollow|i16|1168|
2aaa|8|cl_creatSpawnPermille|i16[4]|1168|creature spawn chance per tick (per mille) by tier
2ab2|4|sfx_current|far|1170|currently loaded SSND
2ab6|1|g_caughtElf|u8|10e0,1118,1178|1-based index of the elf caught in the net
2ab7|2|elf_count|i16|1178|active elves
2ab9|8|unk2ab9|bytes|-|zero; no reference found
2ac1|1|elf_confined|u8|1178|elves may not change rooms (after gate unlock)
2ac2|2|elf_netBusy|i16|1178|an elf is in the net
2ac4|2|elf_throwState|i16|1178|snowball throw state 0..13
2ac6|2|elf_projFrame|i16|1178|projectile frame (BNDL 310)
2ac8|3|elf_levelStartRoom|u8[3]|1178|first room of each level for elf placement
2acb|11|elf_beh0Frames|i8[17]|1178|behaviour 0 frame script, -1 terminated
2adc|20|elf_beh0Dx|i16[16]|1178|behaviour 0 dx per step
2afc|20|elf_beh0Y|i16[16]|1178|behaviour 0 y per step
2b1c|11|elf_beh1Frames|i8[17]|1178|behaviour 1 frame script
2b2d|20|elf_beh1Dx|i16[16]|1178|
2b4d|20|elf_beh1Y|i16[16]|1178|
2b6d|b|elf_beh2Frames|i8[11]|1178|behaviour 2 frame script
2b78|14|elf_beh2Dx|i16[10]|1178|
2b8c|14|elf_beh2Y|i16[10]|1178|
2ba0|11|elf_beh3Frames|i8[17]|1178|behaviour 3 frame script
2bb1|20|elf_beh3Dx|i16[16]|1178|
2bd1|20|elf_beh3Y|i16[16]|1178|
2bf1|a|elf_beh4Frames|i8[10]|1178|behaviour 4 (flee) frame script, -2 = leave room
2bfb|12|elf_beh4Dx|i16[9]|1178|
2c0d|12|elf_beh4Y|i16[9]|1178|
2c1f|12|elf_beh4Sfx|i16[9]|1178|sound effect per step (0 none)
2c31|a|elf_netFrames|i8[10]|1178|net-ride frame per net step, -1 release
2c3b|12|elf_netDx|i16[9]|1178|
2c4d|12|elf_netY|i16[9]|1178|
2c5f|a|elf_throwDxR|i16[5]|1178|thrower dx facing right per throw step
2c69|a|elf_throwDxL|i16[5]|1178|thrower |dx| facing left
2c73|a|elf_throwY|i16[5]|1178|thrower y
2c7d|12|elf_projY|i16[9]|1178|projectile y per projectile frame
2c8f|18|elf_behHandlers|far[6]|1178|behaviour handler table
2ca7|2|elf_projSteps|i16|1178|projectile flight step counter
2ca9|1|pad2ca9|bytes|-|alignment
2caa|1c|help_pages|far[7]|1180|loaded help page texts (OTHR)
2cc6|1c|help_pics|far[7]|1180|per-page pictures (PagerDesc.pics, freed after)
2ce2|e|help_picDx|u16[7]|1180|per-page picture dx
2cf0|e|help_picDy|u16[7]|1180|per-page picture dy
2cfe|1e|help_desc|PagerDesc|1180|help page viewer descriptor
2d1c|44|strs_helpTitles|strs|1180|4 help topic titles
2d60|1|gate_unlocking|u8|10e0,1188|gate unlock animation flag
2d61|4|cut_bndl|far|1188|level cut-scene BNDL
2d65|4|cut_script1|far|1188|cut-scene script (BNDL entry 0)
2d69|4|cut_script2|far|1188|second cut-scene script
2d6d|4|cut_bndlIds|u16[2]|1188|cut-scene BNDL ids
2d71|4|cut_script2Ids|u16[2]|1188|second script OTHR ids
2d75|4|cut_bgIds|u16[2]|1188|cut-scene background PMAP ids
2d79|4|gate_lockBndl|far|1188|BNDL 415 lock frames
2d7d|1|pad2d7d|bytes|-|alignment
2d7e|6|hall_pedTreasure|i8[6]|1190|treasure index per hall pedestal (-1 empty)
2d84|c|hall_pedAnim|i16[6]|1190|animation channel per pedestal
2d90|c|hall_pedX|i16[6]|1190|pedestal x
2d9c|c|hall_pedY|i16[6]|1190|pedestal y
2da8|c|hall_pedFrameBase|i16[6]|1190|base index into g_animFrames
2db4|4|g_playerBndl|far|10e0,1198|BNDL 300 player frames
2db8|4|unused2db8|bytes|-|zero; no reference found
2dbc|14|scene_levelTreasures|far[5]|1198|treasure BNDLs of this level's chests
2dd0|4|scene_treeBndl|far|1198|BNDL 328 room-36 tree animation
2dd4|6|scene_slotX|i16[3]|1198|hiding-place picture x
2dda|6|scene_revealX|i16[3]|1198|revealed treasure/key x
2de0|2|scene_treeFrame|i16|1198|room-36 animation frame
2de2|20|s_AbnormalTermination|cstr|1000|RTL abort message
2e02|2|_atexitcnt|i16|1000|
2e04|4|_exitbuf|far|1000|RTL exit hook (init 1000:0218)
2e08|4|_exitfopen|far|1000|RTL exit hook (init 1000:0218)
2e0c|4|_exitopen|far|1000|RTL exit hook (init 1000:0218)
2e10|2|__exitCleanupDone|i16|1000|
2e12|190|_streams|FILE[20]|1000|Borland FILE[20] (20 bytes each)
2fa2|2|_nfile|u16|1000|20
2fa4|28|_openfd|u16[20]|1000|per-handle open flags
2fcc|2|_fmode|u16|1000|0x4000 O_TEXT
2fce|2|_notUmask|u16|1000|0xFFFF
2fd0|2|_doserrno|i16|1000|
2fd2|59|_dosErrorToSV|i8[89]|1000|DOS error -> errno
302b|1|pad302b|bytes|-|alignment
302c|6|__xalloc|bytes|1000|static xalloc 'Out of memory'
3032|4|_new_handler|far|1000|init 1000:055c
3036|e|s_OutOfMemory|cstr|1000|
3044|4|__randSeed|u32|1000|rand() state, init 1
3048|101|_ctype|u8[257]|1000,1160|Borland ctype table (index c+1)
3149|1|pad3149|bytes|-|alignment
314a|7|s_null|cstr|1000|"(null)"
3151|60|__vprinterClass|u8[96]|1000|printf char-class table
31b1|1|pad31b1|bytes|-|alignment
31b2|128|eh_Vars|bytes|1000,11a0|Borland EH globals; +0A terminate (11a0:00da), +0E unexpected (11a0:02b8)
32da|c0|_sys_errlist|far[48]|1000|errno message pointers
339a|2|_sys_nerr|i16|1000|48
339c|210|strs_sysErrList|strs|1000|errno messages, "Unknown error", ": ", "\n", "\r"
35ac|2|__stdinBufSet|i16|1000|
35ae|2|__stdoutBufSet|i16|1000|
35b0|9|s_notype|cstr|11a0|"<notype>"
35b9|9|s_BCCxh1|cstr|11a0|"**BCCxh1" EH frame signature
35c2|2|string_case_sensitive|i16|11a8|string::case_sensitive (1)
35c4|2|string_paranoid_check|i16|11a8|(0)
35c6|2|string_skip_white|i16|11a8|(1)
35c8|2|string_initial_capac|u16|11a8|(63)
35ca|2|string_resize_inc|u16|11a8|(64)
35cc|2|string_freeboard|u16|11a8|(63)
35ce|5a|strs_stringRange|strs|11a8|3 x "String reference out of range"
3628|30|s_fpFormatsNotLinked|cstr|1000|printf/scanf "floating point formats not linked" (3628, 362d)
3658|c|eh_InstanceCtx|far[3]|1000|EH context table {&SS:0010,&SS:0014,&SS:0016}
3664|c|unk3664|bytes|-|zero; no reference found
3670|2|eh_TaskTableCap|u16|1000|30
3672|2|eh_CachedSS|u16|1000,11a0|
3674|4|eh_CurCtx|far|1000,11a0|
3678|2|unk3678|u16|-|zero; no reference found
367a|6f|strs_setargv|strs|1000|_setargv out-of-memory messages
36e9|1|pad36e9|bytes|-|alignment
36ea|4|_argv0|far|1000|program path
36ee|1b|s_OutOfMemSetargv0|cstr|1000|"Out of memory in _setargv0"
3709|1|pad3709|bytes|-|alignment
370a|4|_ReadHook|far|1000|console read hook (NULL)
370e|4|_WriteHook|far|1000|console write hook (NULL)
3712|18|__sigHandlers|far[6]|1000|signal() handlers
372a|6|__sigNumbers|u8[6]|1000|{2,4,8,11,15,22}
3730|6|__sigSubcode|u8[6]|1000|
3736|124|strs_fpErrors|strs|1000|"Floating Point: " + reasons, signal messages
385a|2|__realcvt|near|1000|printf float hook (1000:2a7e)
385c|6|__scanfFloatHooks|near[3]|1000|
3862|18|__startupTable|bytes|1000|#pragma startup records {u8 kind, u8 prio, far fn}
387a|6|__exitTable|bytes|1000|#pragma exit records
3880|8|g_wingRect|i16[4]|1008|_BSS starts here. RECT {0,0,w,h} of WinG buffer
3888|8|g_wingRect2|i16[4]|1008|copy (no reader)
3890|2|g_wingStride|u16|1008|DWORD-aligned row bytes (no reader)
3892|28|g_wingBmi|bytes|1008|BITMAPINFOHEADER of the WinG DIB
38ba|400|g_wingColorTable|bytes|1008|RGBQUAD[256]
3cba|20|p_WinG|far[8]|1008|WING.DLL ordinals 1001,1002,1003,1004,1005,1006,1010,1009
3cda|2|g_hInstance|u16|1018,1020,1080,1090|
3cdc|2|g_hwndMain|u16|1010..1090|main window
3cde|82|g_iniPath|bytes|1018|"C:\TLCWIN\TMTWINCD\TMTWIN.INI"
3d60|4|g_lpfnEnumProc|far|1018|
3d64|1e|g_cursors|u16[15]|1020|loaded HCURSORs
3d82|2|g_evType|i16|1010,1028|pending event: 0 none,1 quit,2 key,3 Ldown,4 Lup,5 Rdown,6 Rup
3d84|2|g_evP1|i16|1010,1028|key code or x
3d86|2|g_evP2|i16|1010,1028|y
3d88|4|g_resScratchMax|i32|1038|largest Res_LoadToScratch size
3d8c|4|g_resScratch|far|1038..1198|280000-byte scratch buffer
3d90|400|g_paletteRGB|bytes|1118|RRGB 900 loaded by GameMain (GetResourceInto) and passed to Pal_SetFromRGB; 768 bytes used
4190|1|g_transparentIndex|u8|1048,1050,1058|sprite colour key
4191|2|g_foreColorPhys|u16|1048,1060|
4193|100|g_logToPhys|u8[256]|1048,1050,1058|logical -> physical palette index
4293|2|g_foreColorIndex|u16|1048|
4295|1|pad4295|bytes|-|
4296|2|g_hdcScreen|u16|1008..1058|
4298|2|g_hdcMem|u16|1010..1058|
429a|2|g_hbmBack|u16|1010,1018|
429c|2|g_selPlanarBits|u16|1018,1050|
429e|2|g_selPixels|u16|1008..1060|
42a0|2|g_hGdiHeap|u16|1018|
42a2|800|g_dirtyRects|bytes|1058|DirtyRect[256] {i16 x,y,w,h}
4aa2|c|g_fonts|far[3]|1068|
4aae|2|g_curFont|i16|1068|
4ab0|2|g_fontHeight|u16|1060,1068|
4ab2|2|g_fontSeg|u16|1060,1068|
4ab4|2|g_fontWidths|u16|1060,1068|
4ab6|2|g_fontOffLo|u16|1060,1068|
4ab8|2|g_fontOffHi|u16|1060,1068|
4aba|2|g_fontBits|u16|1060,1068|
4abc|2|unk4abc|u16|-|no reference found
4abe|2|g_music4abe|i16|1128|zeroed by Music_Select, never read
4ac0|2|unk4ac0|u16|-|no reference found
4ac2|10|g_waveFmt|bytes|1088|PCMWAVEFORMAT
4ad2|2|g_hWaveOut|u16|1088|
4ad4|20|g_waveHdr|bytes|1088|WAVEHDR
4af4|82|g_soundsDir|bytes|1090|"<exe dir>\SOUNDS\"
4b76|2|g_midiExtended|i16|1090|1 = 'E' files, 0 = 'B' files
4b78|2|g_musicIdx|i16|1090|
4b7a|1f4|g_hotspots|bytes|1098|Hotspot[50] {u16 id; i16 x,y,w,h}
4d6e|1f4|g_hotspotsSaved|bytes|1098|backup (dead code)
4f62|20|g_timerDeadline|u32[8]|10a8|timeGetTime deadlines
4f82|1d0|g_animChan|bytes|10b8|AnimChannel[16] (29 bytes)
5152|190|g_animFrames|far[100]|10b8,10d8,1190|frame pixmap table
52e2|384|g_animEraseRects|bytes|10b8|AnimEraseRect[100] (9 bytes)
5666|578|g_animSprites|bytes|10b8|AnimSprite[100] (14 bytes)
5bde|60|g_animUpdateRects|bytes|10b8|AnimUpdateRect[16] (6 bytes)
5c3e|4|g_btnX|i16[2]|10c0|
5c42|4|g_btnY|i16[2]|10c0|
5c46|4|g_btnW|i16[2]|10c0|
5c4a|4|g_btnH|i16[2]|10c0|
5c4e|4|unk5c4e|bytes|-|no reference found
5c52|2|g_dlgX|i16|10c0|
5c54|2|g_dlgY|i16|10c0|
5c56|2|g_dlgW|i16|10c0|
5c58|2|g_dlgH|i16|10c0|
5c5a|2|g_dlgMarginL|i16|10c0|
5c5c|2|g_dlgMarginT|i16|10c0|
5c5e|2|g_dlgMarginB|i16|10c0|
5c60|2|g_dlgTitleX|i16|10c0|
5c62|2|g_dlgFooter|near|10c0|DGROUP offset of footer text
5c64|2|g_dlgFooterX|i16|10c0|
5c66|2|g_dlgFooterY|i16|10c0|
5c68|4|g_btnLabel|near[2]|10c0|
5c6c|4|g_btnLabelX|i16[2]|10c0|
5c70|4|g_btnState|i16[2]|10c0|
5c74|1c|g_pagerText|far[7]|10c8|
5c90|1c|g_pagerPic|far[7]|10c8|
5cac|e|g_pagerPicDx|i16[7]|10c8|
5cba|e|g_pagerPicDy|i16[7]|10c8|
5cc8|1|g_musicPauseDepth|i8|10d0|
5cc9|1|pad5cc9|bytes|-|
5cca|1|g_room|u8|10e0..1198|current room 0..49 (0xFE = climb over); user record +0x1df
5ccb|1|g_nextRoom|u8|10e0|
5ccc|2|g_playerX|i16|10e0,1110,1118,1168,1190|
5cce|2|g_playerY|i16|10e0,1118,1168|
5cd0|1|g_playerFrame|u8|10e0,10f8,1118,1168|index into BNDL 300
5cd1|1|g_playerFacingLeft|u8|10e0,1118,1168,1178|
5cd2|2|g_playerW|i16|10e0,10f8,1110,1168,1190|
5cd4|2|g_playerH|i16|10e0,10f8,1168|
5cd6|aa|g_frameW|i16[85]|10e0,10f8,1168,1198|player frame widths
5d80|aa|g_frameH|i16[85]|10e0,10f8,1168,1198|player frame heights
5e2a|1|g_roomTransition|u8|10e0,1118|
5e2b|1|g_doorStep|u8|10e0|
5e2c|1|g_coinStep|u8|10e0,1118,1168,1198|
5e2d|1|g_walkActive|u8|10e0|
5e2e|1|g_arriveAction|u8|10e0|
5e2f|1|g_playerHidden|u8|10e0,10f8,1118,1148|
5e30|1|g_netStep|u8|10e0,1118,1168,1178|
5e31|1|g_emptySwingStep|u8|10e0,1168|
5e32|2|g_netX|i16|10e0,1178|
5e34|2|g_fxFrame|u16|10e0,1168|
5e36|2|g_fxX|i16|10e0,1168|
5e38|2|g_fxY|i16|10e0,1168|
5e3a|2|g_fxW|i16|10e0,1168|
5e3c|2|g_fxH|i16|10e0,1168|
5e3e|2|g_walkTargetX|i16|10e0|
5e40|2|g_walkTargetY|i16|10e0|
5e42|1|g_walkArg3|u8|10e0|
5e43|1|g_walkArriveAction|u8|10e0|
5e44|1|g_prevShopCoins|u8|10e0|
5e45|2|g_entryX|i16|10e0|
5e47|2|g_entryY|i16|10e0|
5e49|1|g_entryFrame|u8|10e0|
5e4a|1|g_entryFacing|u8|10e0|
5e4b|2|g_inputEvent|u16|10e0|
5e4d|2|g_bumpStep|u16|10e0|
5e4f|2|g_lastMouseX|i16|10e0|
5e51|2|g_lastMouseY|i16|10e0|
5e53|1|pad5e53|bytes|-|
5e54|4|g_hudArt|far|10e8,1198|BNDL 301
5e58|1|g_rankTier|u8|10e8,1100,1118,1160,1168,1198|rank/2; record +0x09
5e59|1|g_haveKey|u8|10e0,10e8,1118,1160,1188|record +0x15
5e5a|2|g_coins|u16|10e0,10e8,1110,1160,1168|record +0x16
5e5c|2|g_nets|u16|10e0,10e8,1110,1160,1168|record +0x18
5e5e|1|g_gemsFound|u8|10e8,1118,1160,1198|record +0x1a
5e5f|2|g_climbPoints|u16|10e8,10f0,1118,1160|record +0x1b
5e61|1|g_treasureCount2|u8|10e8,1118,1198|
5e62|4|g_totalTreasures|u32|10e0,10e8,10f0,1160|record +0x0a
5e66|4|g_celebScript323|far|10f0|
5e6a|2|g_celebImageCount|i16|10f0|
5e6c|4|g_treasureImage|far|10f0|
5e70|4|g_celebScript324|far|10f0|
5e74|4|g_celebScript325|far|10f0|
5e78|2|g_rank|i16|10f0|rank 0..7
5e7a|4|g_seeRankSave|far|10f0|
5e7e|2|g_shopCoinsLeft|i16|10e0,10f8,1160|record +0x1d
5e80|4|g_shopSignBndl|far|10f8,1198|BNDL 302
5e84|24|g_clueText|far[3][3]|10e8,1100,1150|clue word strings per level
5ea8|3|g_clueFlags|u8[3]|1100,1160|per level attribute mask 8 count,4 size,2 shape,1 noun; record +0x20
5eab|9|g_clueAttr|u8[3][3]|1100,1160|selected attribute values; record +0x23
5eb4|3|g_cluePlural|u8[3]|1100,1160|record +0x2c
5eb7|12|g_clueVoice|u16[3][3]|10e8,1100,1150|clue word voice offsets (SSND 7300/7400/7500 + v)
5ec9|3|g_keyKind|u8[3]|1100|object kind hiding the key per level (OTHR 10555 index)
5ecc|6|unk5ecc|bytes|-|no reference found
5ed2|1|g_coinPileNoSpawnRoom|u8|1110|
5ed3|2|g_coinPileX|i16|1110|
5ed5|2|g_coinPileW|i16|1110|
5ed7|2|g_coinPileH|i16|1110|
5ed9|1|pad5ed9|bytes|-|
5eda|18|g_restartJmp|bytes|1118,1140|jmp_buf: KERNEL CATCHBUF + Borland words
5ef2|1|g_isChampion|u8|10f0,1118|
5ef3|1|g_newClimb|u8|1118|
5ef4|9|unk5ef4|bytes|-|no reference found
5efd|1|g_music5efd|u8|1128|
5efe|2|g_musicZone|i16|1128|
5f00|1|g_hintsOn|u8|1130,1140,1160|record +0x10
5f01|4|g_hintUnseen|u32|1130,1140,1160|record +0x11
5f05|e|unk5f05|bytes|-|no reference found
5f13|4|g_titleBndl1|far|1138|BNDL 400
5f17|4|g_titleScript1|far|1138|
5f1b|2|g_titleAnim1|i16|1138|
5f1d|4|g_titleBndl2|far|1138|BNDL 401
5f21|2|g_titleFrames1|i16|1138|
5f23|4|g_titleScript2|far|1138|
5f27|2|g_titleAnim2|i16|1138|
5f29|4|unk5f29|bytes|-|no reference found
5f2d|2|g_titleFramesTotal|i16|1138|
5f2f|4|g_titleScript3|far|1138|
5f33|2|g_titleAnim3|i16|1138|
5f35|2|g_titleMusicPending|i16|1138|
5f37|3|unk5f37|bytes|-|no reference found
5f3a|48|g_quizSlots|bytes|1150|QuizSlot[9] {i16 q; u8 perm[3]; u8 tried[3]}; record +0x197
5f82|1|g_quizTier|u8|1150,1160|record +0x2f
5f83|167|g_quizStatus|u8[359]|1150,1160|record +0x30
60ea|4|g_quizTextRes|far|1150|
60ee|2|g_quizRangeLo|i16|1150|
60f0|2|g_quizRangeHi|i16|1150|
60f2|8|g_quizCount|i16[4]|1150|
60fa|2|g_quizSlot|i16|1150|
60fc|2|g_quizQ|i16|1150|
60fe|4|g_quizText|far|1150|
6102|c|g_quizAnswer|far[3]|1150|
610e|1|g_quizLineSlots|u8|1150|
610f|2|g_quizNumLines|i16|1150|
6111|2|g_quizTextX|i16|1150|
6113|2|g_quizTextY|i16|1150|
6115|14|g_quizLineY|i16[10]|1150|
6129|2|g_quizTextW|i16|1150|
612b|2|g_quizAnsX|i16|1150|
612d|6|g_quizAnsY|i16[3]|1150|
6133|2|g_quizAnsW|i16|1150|
6135|2|g_quizAnsH|i16|1150|
6137|2|g_quizInterrupted|i16|1150|
6139|4|g_quizSpeakerIcon|far|1150|PMAP 2001
613d|8|g_quizSpkRect|i16[4]|1150|
6145|2|g_quizVoice|i16|1150|
6147|2|g_quizNoAnswerVoice|i16|1150|
6149|15|unk6149|bytes|-|no reference found
615e|4|g_rowBarNormal|far|1160|
6162|4|g_rowBarHilite|far|1160|
6166|4|g_signInBndl|far|1160|BNDL 322
616a|4|g_nameCursor|far|1160|
616e|2|g_rowW|i16|1160|
6170|1|g_userCount|i8|1160|
6171|1|g_userSel|i8|1160|
6172|4|g_users|far|1160|UserRecord[7] (1583 bytes each)
6176|2|g_nameDone|i16|1160|
6178|2|cl_input|u16|1168|
617a|2|cl_onLadder|i16|1168|
617c|2|cl_done|i16|1168|
617e|2|cl_screen|i16|1168|
6180|2|cl_nextScreen|i16|1168|
6182|2|cl_nextY|i16|1168|
6184|2|cl_fullPresent|i16|1168|
6186|2|cl_fadePending|i16|1168|
6188|2|cl_column|i16|1168|
618a|2|cl_climbPhase|i16|1168|
618c|2|cl_jumpStep|i16|1168|
618e|2|cl_fallStep|i16|1168|
6190|2|cl_landStep|i16|1168|
6192|4|cl_climbBndl|far|1168|BNDL 321
6196|30|cl_climbW|i16[24]|1168|
61c6|30|cl_climbH|i16[24]|1168|
61f6|4|cl_ladderFull|far|1168|PMAP 1090
61fa|4|cl_ladderLow|far|1168|PMAP 1091
61fe|4|cl_ladderStub|far|1168|PMAP 1092
6202|4|cl_creatBndl|far|1168|BNDL 326
6206|8|cl_creatW|i16[4]|1168|
620e|8|cl_creatH|i16[4]|1168|
6216|2|cl_creatTick|i16|1168|
6218|2|cl_creatX|i16|1168|
621a|2|cl_creatY|i16|1168|
621c|2|cl_creatFrame|i16|1168|
621e|2|cl_creatFlip|i16|1168|
6220|2|cl_creatCol|i16|1168|
6222|2|cl_creatScreen|i16|1168|
6224|4|cl_layout|far|1168|-> selected cl_layouts[tier]
6228|15e|elf_animSets|bytes|1178|ElfAnimSet[7] (0x32 bytes)
6386|240|elf_actors|bytes|1178|Elf[32] (0x12 bytes)
65c6|4|elf_projBndl|far|1178|BNDL 310
65ca|2|elf_projX|i16|1178|
65cc|2|elf_drawYOff|i16|1178|
65ce|2|g_treasuresFound|u16|10e0,10f0,1118,1160,1190,1198|completed climbs; record +0x62d
65d0|18|hall_treasureBndl|far[6]|1190|
65e8|18|hall_treasureAnim|far[6]|1190|
6600|80|_atexittbl|far[32]|1000|
6680|4|unk6680|bytes|-|no reference found
6684|1|__fputcChar|u8|1000|
6685|1|pad6685|bytes|-|
6686|4|eh_CleanupFnTmp|far|11a0|
668a|4|eh_FilterFnTmp|far|11a0|
668e|1|g_badTypeid|u8|11a0|static Bad_typeid object
668f|1|g_badCast|u8|11a0|static Bad_cast object
6690|4|eh_TaskTable|far|1000,11a0|
6694|2|_argc|i16|1000,1018|
6696|4|_argv|far|1000,1018|
669a|2|__argvCap|i16|1000|
669c|6|__heapDesc|bytes|1000|far-heap descriptor
"""


def parse_regions():
    regs = []
    for ln in REGIONS_TXT.strip().splitlines():
        p = ln.split('|')
        if len(p) < 6:
            raise ValueError('bad region line: %r' % ln)
        off = int(p[0], 16)
        sz = int(p[1], 16)
        regs.append({'off': off, 'size': sz, 'name': p[2], 'type': p[3],
                     'owner': p[4], 'summary': '|'.join(p[5:])})
    return regs


REGIONS = parse_regions()

# ----------------------------------------------------------------------------
# NE parsing
# ----------------------------------------------------------------------------


class NE:
    def __init__(self, data):
        self.f = data
        ne = struct.unpack_from('<H', data, 0x3c)[0]
        if data[ne:ne + 2] != b'NE':
            raise ValueError('not an NE executable')
        self.ne = ne
        self.autods = struct.unpack_from('<H', data, ne + 0x0e)[0]
        self.heap = struct.unpack_from('<H', data, ne + 0x10)[0]
        self.stack = struct.unpack_from('<H', data, ne + 0x12)[0]
        self.segcnt = struct.unpack_from('<H', data, ne + 0x1c)[0]
        segtab = ne + struct.unpack_from('<H', data, ne + 0x22)[0]
        self.align = struct.unpack_from('<H', data, ne + 0x32)[0]
        modtab = ne + struct.unpack_from('<H', data, ne + 0x28)[0]
        impn = ne + struct.unpack_from('<H', data, ne + 0x2a)[0]
        self.modtab, self.impn = modtab, impn
        self.segs = []
        for i in range(self.segcnt):
            off, ln, fl, mn = struct.unpack_from('<HHHH', data, segtab + i * 8)
            self.segs.append({'fileoff': off << self.align, 'len': ln or 0x10000,
                              'flags': fl, 'minalloc': mn or 0x10000})

    @staticmethod
    def selector(segnum):
        return 0x1000 + (segnum - 1) * 8

    def segment(self, n):
        s = self.segs[n - 1]
        return self.f[s['fileoff']:s['fileoff'] + s['len']]

    def relocs(self, n):
        """Return {offset: target} for segment n.  target = ('int', sel, off) |
        ('ord', mod, ord) | ('name', mod, name) | ('os', a, b)."""
        s = self.segs[n - 1]
        out = {}
        if not s['flags'] & 0x100:
            return out
        p = s['fileoff'] + s['len']
        data = self.segment(n)
        cnt = struct.unpack_from('<H', self.f, p)[0]
        p += 2
        for k in range(cnt):
            atype, rtype, soff, a, b = struct.unpack_from('<BBHHH', self.f, p + k * 8)
            t = rtype & 3
            if t == 0:
                tgt = ('int', self.selector(a), b) if a != 0xff else ('movable', a, b)
            elif t == 1:
                tgt = ('ord', self._mod(a), b)
            elif t == 2:
                tgt = ('name', self._mod(a), self._name(b))
            else:
                tgt = ('os', a, b)
            if rtype & 4:                      # additive: single site
                out[soff] = (atype, tgt)
                continue
            o, guard = soff, 0
            while o != 0xffff and o + 1 < len(data) and guard < 4096:
                out[o] = (atype, tgt)
                o = struct.unpack_from('<H', data, o)[0]
                guard += 1
        return out

    def _mod(self, i):
        o = struct.unpack_from('<H', self.f, self.modtab + (i - 1) * 2)[0]
        return self._name(o)

    def _name(self, o):
        n = self.f[self.impn + o]
        return self.f[self.impn + o + 1:self.impn + o + 1 + n].decode('latin-1')


# ----------------------------------------------------------------------------
# DGROUP accessor
# ----------------------------------------------------------------------------


def esc(bs):
    """Exact text rendering: printable ASCII literal, every other byte as \\xNN."""
    return ''.join(chr(c) if 32 <= c < 127 and c != 0x5c else
                   ('\\\\' if c == 0x5c else '\\x%02x' % c) for c in bs)


class DG:
    def __init__(self, exe_path):
        self.exe_path = exe_path
        raw = open(exe_path, 'rb').read()
        self.ne = NE(raw)
        self.segnum = self.ne.autods
        self.sel = NE.selector(self.segnum)
        self.seginfo = self.ne.segs[self.segnum - 1]
        self.b = self.ne.segment(self.segnum)
        self.rel = self.ne.relocs(self.segnum)

    # scalar readers
    def u8(self, o): return self.b[o]
    def i8(self, o): return struct.unpack_from('<b', self.b, o)[0]
    def u16(self, o): return struct.unpack_from('<H', self.b, o)[0]
    def i16(self, o): return struct.unpack_from('<h', self.b, o)[0]
    def u32(self, o): return struct.unpack_from('<I', self.b, o)[0]
    def i32(self, o): return struct.unpack_from('<i', self.b, o)[0]

    def arr(self, kind, o, n):
        f = {'u8': ('B', 1), 'i8': ('b', 1), 'u16': ('H', 2), 'i16': ('h', 2),
             'u32': ('I', 4), 'i32': ('i', 4)}[kind]
        return list(struct.unpack_from('<%d%s' % (n, f[0]), self.b, o))

    def cstr_bytes(self, o):
        e = self.b.index(b'\0', o)
        return self.b[o:e]

    def cstr(self, o):
        return esc(self.cstr_bytes(o))

    def far(self, o):
        """Resolve a far pointer stored at DGROUP offset o.
        Returns None (NULL), {'dg': off} for a DGROUP target, or {'code': 'SSSS:OOOO'}."""
        r = self.rel.get(o)
        if r:
            atype, t = r
            if t[0] == 'int':
                if t[1] == self.sel:
                    return {'dg': t[2]}
                return {'code': '%04x:%04x' % (t[1], t[2])}
            return {'import': '%s.%s' % (t[1], t[2])}
        off, seg = self.u16(o), self.u16(o + 2)
        if off == 0 and seg == 0:
            return None
        return {'raw': '%04x:%04x' % (seg, off)}

    def far_str(self, o):
        p = self.far(o)
        if p and 'dg' in p:
            return self.cstr(p['dg'])
        return None

    def far_fmt(self, o):
        p = self.far(o)
        if p is None:
            return None
        if 'dg' in p:
            return '%04x:%04x' % (self.sel, p['dg'])
        return list(p.values())[0]

    def strings_in(self, a, b):
        out, o = [], a
        while o < b:
            e = self.b.find(b'\0', o, b)
            if e < 0:
                e = b
            if e > o:
                out.append({'off': '%04x' % o, 'text': esc(self.b[o:e])})
            o = e + 1
        return out


def h4(v):
    return '%04x' % v


def ptr_str(dg, p):
    if p is None:
        return None
    if 'dg' in p:
        return '%04x:%04x' % (dg.sel, p['dg'])
    return list(p.values())[0]

# ----------------------------------------------------------------------------
# generic region decoder
# ----------------------------------------------------------------------------


TYPE_RE = re.compile(r'^(u8|i8|u16|i16|u32|i32|far|near|cstr|strs|bytes)((?:\[\d+\])*)$')
TSIZE = {'u8': 1, 'i8': 1, 'u16': 2, 'i16': 2, 'u32': 4, 'i32': 4, 'far': 4, 'near': 2}


def decode_generic(dg, r):
    o, n, t = r['off'], r['size'], r['type']
    m = TYPE_RE.match(t)
    if not m:
        return {'hex': dg.b[o:o + n].hex()}
    base, dims = m.group(1), [int(x) for x in re.findall(r'\[(\d+)\]', m.group(2))]
    if base == 'cstr':
        return dg.cstr(o)
    if base == 'strs':
        return dg.strings_in(o, o + n)
    if base == 'bytes':
        if all(c == 0 for c in dg.b[o:o + n]):
            return {'zero': n}
        return {'hex': dg.b[o:o + n].hex()}
    cnt = 1
    for d in dims:
        cnt *= d
    es = TSIZE[base]
    if base == 'far':
        vals = [dg.far_fmt(o + i * 4) for i in range(cnt)]
    elif base == 'near':
        vals = [h4(dg.u16(o + i * 2)) for i in range(cnt)]
    else:
        vals = dg.arr(base, o, cnt)
    if not dims:
        return vals[0]

    def shape(v, ds):
        if len(ds) == 1:
            return v
        step = len(v) // ds[0]
        return [shape(v[i * step:(i + 1) * step], ds[1:]) for i in range(ds[0])]
    return shape(vals, dims)


def region_type_size(t):
    m = TYPE_RE.match(t)
    if not m or m.group(1) in ('cstr', 'strs', 'bytes'):
        return None
    cnt = 1
    for d in re.findall(r'\[(\d+)\]', m.group(2)):
        cnt *= int(d)
    return TSIZE[m.group(1)] * cnt

# ----------------------------------------------------------------------------
# semantic table decoders
# ----------------------------------------------------------------------------


def dec_dlgdesc(dg, o):
    """DlgDesc, 26 bytes (consumer 10c0:0ae5 / 10c0:0764)."""
    title = dg.far(o + 2)
    body = dg.far(o + 7)
    pic = dg.far(o + 0x14)
    return {
        'off': h4(o),
        'type': dg.u16(o),
        'title_ptr': ptr_str(dg, title),
        'title': dg.cstr(title['dg']) if title and 'dg' in title else None,
        'footer': dg.u8(o + 6),
        'body_ptr': ptr_str(dg, body),
        'body': dg.cstr(body['dg']) if body and 'dg' in body else None,
        'default_button': dg.u8(o + 0x0b),
        'x': dg.i16(o + 0x0c), 'y': dg.i16(o + 0x0e),
        'text_width': dg.i16(o + 0x10), 'line_count': dg.i16(o + 0x12),
        'picture_ptr': ptr_str(dg, pic),
        'pic_dx': dg.u8(o + 0x18), 'pic_dy': dg.u8(o + 0x19),
    }


def dec_pager(dg, o):
    """PagerDesc, 30 bytes (consumer 10c0:0cde)."""
    return {
        'off': h4(o),
        'title_ptr': dg.far_fmt(o), 'footer': dg.u8(o + 4),
        'texts_ptr': dg.far_fmt(o + 5), 'page_count': dg.u8(o + 9),
        'x': dg.i16(o + 0x0a), 'y': dg.i16(o + 0x0c),
        'text_width': dg.i16(o + 0x0e), 'line_count': dg.i16(o + 0x10),
        'pics_ptr': dg.far_fmt(o + 0x12), 'pic_dx_ptr': dg.far_fmt(o + 0x16),
        'pic_dy_ptr': dg.far_fmt(o + 0x1a),
    }


def dec_rooms(dg):
    rooms = []
    for r in range(50):
        o = 0x23d8 + r * 28
        rooms.append({
            'room': r, 'off': h4(o),
            'start_x': dg.i16(o), 'start_y': dg.i16(o + 2),
            'start_frame': dg.u8(o + 4), 'start_facing_left': dg.u8(o + 5),
            'exit_left': dg.u8(o + 6), 'exit_right': dg.u8(o + 7),
            'exit_door': dg.u8(o + 8), 'exit_unused09': dg.u8(o + 9),
            'type_left': dg.u8(o + 0x0a), 'type_right': dg.u8(o + 0x0b),
            'type_door': dg.u8(o + 0x0c), 'type_unused0d': dg.u8(o + 0x0d),
            'door_x': dg.i16(o + 0x0e), 'unused10': dg.i16(o + 0x10),
            'bg_pmap': dg.u16(o + 0x12),
            'slot_object': dg.arr('u8', o + 0x14, 3),
            'slot_content': dg.arr('u8', o + 0x17, 3),
            'level': dg.u8(o + 0x1a), 'music_code': dg.u8(o + 0x1b),
        })
    return rooms


def dec_words(dg):
    def ptab(o, n):
        return [dg.far_str(o + 4 * i) for i in range(n)]
    nouns, plurals = ptab(0x1079, 16), ptab(0x10b9, 16)
    counts, sizes, shapes = ptab(0x10f9, 8), ptab(0x1119, 5), ptab(0x112d, 5)
    # voice offsets from Clue_BuildVoiceIds 1100:08ae; spoken SSND = bank base
    # (7300/7400/7500, g_quizVoiceBank of the question) + offset.
    out = {'_voice_note': 'voice offset per Clue_BuildVoiceIds 1100:08ae (code, not DGROUP): '
                          'count v+0x25, size v+0x2c, shape v+0x30, noun v+7 (singular) / '
                          'v+0x16 (plural); spoken SSND = 7300/7400/7500 (narrator bank 1/2/3) + offset',
           'nouns': [], 'counts': [], 'sizes': [], 'shapes': []}
    for v in range(1, 16):
        out['nouns'].append({'value': v, 'singular': nouns[v], 'plural': plurals[v],
                             'voice_singular': v + 7, 'voice_plural': v + 0x16})
    for v in range(1, 8):
        out['counts'].append({'value': v, 'text': counts[v], 'voice': v + 0x25})
    for v in range(1, 5):
        out['sizes'].append({'value': v, 'text': sizes[v], 'voice': v + 0x2c})
    for v in range(1, 5):
        out['shapes'].append({'value': v, 'text': shapes[v], 'voice': v + 0x30})
    out['index0'] = {'noun': nouns[0], 'plural': plurals[0], 'count': counts[0],
                     'size': sizes[0], 'shape': shapes[0]}
    return out


def dec_ranks(dg):
    return {
        'thresholds': dg.arr('u32', 0x0ea0, 8),
        'threshold_labels': [dg.far_str(0x0f8c + 4 * i) for i in range(7)],
        'names': [dg.far_str(0x0fa8 + 4 * i) for i in range(8)],
        'promo_hint': dg.arr('i16', 0x0fc8, 8),
        'stay_hint': dg.arr('i16', 0x0fd8, 8),
        'room9_rank_hint': dg.arr('i16', 0x0e8f, 8),
        'gems_needed_by_tier': dg.arr('u8', 0x0ec3, 4),
        'gem_x_by_tier': [dg.arr('i16', 0x0ec7 + 10 * t, 5) for t in range(4)],
        'gem_y_by_tier': [dg.arr('i16', 0x0eef + 10 * t, 5) for t in range(4)],
        'signin_rank_strings': {'trainee': dg.cstr(0x23c4), 'star': dg.cstr(0x23cc),
                                'champion': dg.cstr(0x23ce)},
        'elves_by_tier': dg.arr('u8', 0x128e, 4),
        'creature_spawn_permille_by_tier': dg.arr('i16', 0x2aaa, 4),
    }


def dec_music(dg):
    ids, loop = dg.arr('i16', 0x09fa, 10), dg.arr('i16', 0x0a0e, 10)
    tracks = []
    for i in range(10):
        p = dg.far(0x0a22 + 4 * i)
        tracks.append({'index': i, 'id': ids[i], 'loop_pos': loop[i],
                       'file_ptr': ptr_str(dg, p),
                       'file_template': dg.cstr(p['dg']) if p and 'dg' in p else None})
    return {
        'tracks': tracks,
        'prefix_rule': "byte 0 of the file name is replaced by 'B' (base/FM set) or 'E' (extended/GM set) "
                       "at open time (1090:02e9); 'B' when the MIDI mapper setup names one of strs_fmSynthNames",
        'fm_synth_names': [s['text'] for s in dg.strings_in(0x0ae0, 0x0b0c)],
        'midimap_cfg': dg.cstr(0x0acc), 'mci_device': dg.cstr(0x0b0c),
        'sounds_dir': dg.cstr(0x0b16),
        'zone_to_track_from_code': {'_source': 'switch in Music_Select 1128:001a (code, not DGROUP)',
                                    '0': 9001, '1': 9003, '2': 9003, '3': 9004, '4': 9004,
                                    '5': 9005, '6': 9005, '7': 9000, '-98': 9002, '-97': 9009,
                                    '-96': 9008, '-95': 9006, '-94': 9007},
    }


def dec_menus(dg):
    bar = 0x13bf
    count = dg.i16(bar)
    pp = dg.far(bar + 2)
    menus = []
    for k in range(count):
        o = pp['dg'] + k * 0x14
        title = dg.far_str(o)
        n = dg.i16(o + 6)
        items_p = dg.far(o + 8)
        items = dg.cstr_bytes(items_p['dg']).split(b'\n')
        hp = dg.far(o + 0x0c)
        handlers = [dg.far_fmt(hp['dg'] + 4 * i) for i in range(n)]
        menus.append({'off': h4(o), 'title': title, 'unk04': dg.i16(o + 4), 'item_count': n,
                      'items_ptr': ptr_str(dg, items_p),
                      'items': [{'index': i, 'text': esc(items[i]) if i < len(items) else None,
                                 'command_id': '%04x' % (k * 0x100 + i), 'handler': handlers[i]}
                                for i in range(n)],
                      'handlers_ptr': ptr_str(dg, hp),
                      'unk10': dg.i16(o + 0x10), 'unk12': dg.i16(o + 0x12)})
    return {'menubar_off': h4(bar), 'count': count, 'popups_ptr': ptr_str(dg, pp), 'menus': menus}


DLG_OFFS = [0x14d6, 0x1533, 0x15cc, 0x1625, 0x163f, 0x1919, 0x2084, 0x2129, 0x21e6, 0x2200,
            0x221a, 0x2234]


def dec_dialogs(dg):
    users = {0x14d6: '1140:0062 Start Another Game', 0x1533: '1140:0098 Exit',
             0x15cc: '1140:0015 New Player Messages', 0x1625: '1140:00cc Music and Sound',
             0x163f: '1140:00f7 Voice', 0x1919: '1140:0000 About',
             0x2084: '1158:011f write error', 0x2129: '1160:0957 name already on list',
             0x21e6: '1160:089f remove player', 0x2200: '1160:075d list full',
             0x221a: '1160:0353/0a43 enter name', 0x2234: '1138:0372, 1160:04fa exit'}
    out = []
    for o in DLG_OFFS:
        d = dec_dlgdesc(dg, o)
        d['used_by'] = users[o]
        out.append(d)
    return out


def dec_quiz(dg):
    base = dg.arr('u16', 0x1990, 360)
    bank = dg.arr('u8', 0x1c60, 359)
    tiers = dg.arr('i16', 0x1de7, 5)
    qs = []
    for q in range(359):
        qs.append({'q': q, 'text_othr': 11000 + q, 'voice_first': base[q],
                   'voice_count': base[q + 1] - base[q], 'bank': bank[q],
                   'bank_base': 7300 + 100 * (bank[q] - 1)})
    return {
        'tier_start': tiers,
        'silent_answers': dg.arr('i16', 0x1dc7, 16),
        'voice_base_sentinel_359': base[359],
        'questions': qs,
        'praise': [{'index': i, 'text_ptr': dg.far_fmt(0x1df1 + 4 * i),
                    'text': dg.far_str(0x1df1 + 4 * i), 'voice_offset': i} for i in range(4)],
        'sorry': [{'index': i, 'text_ptr': dg.far_fmt(0x1e07 + 4 * i),
                   'text': dg.far_str(0x1e07 + 4 * i), 'voice_offset': 4 + i} for i in range(4)],
        '_voice_note': 'praise i -> SSND bank_base+i, sorry i -> bank_base+4+i (1150:06ac); '
                       'question line k -> voice_first+k, answer a -> voice_first+nLines+a (1150:1409/14d7); '
                       'voice_count = nLines + 3 (derived from the table, [I])',
        'stop_keys': {'result': dg.arr('u16', 0x1e01, 3), 'sorry': dg.arr('u16', 0x1e17, 3),
                      'line': dg.arr('u16', 0x1e1d, 19), 'answer': dg.arr('u16', 0x1e43, 19)},
        'answer_prompt': dg.cstr(0x0f17), 'click_to_go_on': dg.cstr(0x0f3b),
        'clue_abort_keys': dg.arr('u16', 0x0f54, 3),
    }


def dec_hints(dg):
    bits = dg.arr('i16', 0x13d0, 34)
    return {
        'events': [{'event': e, 'unseen_bit': bits[e], 'text_othr': 11500 + e,
                    'voice_ssnd': 7100 + e} for e in range(34)],
        '_note': 'text OTHR 11500+event = entry event of BNDL 327, voice SSND 7100+event (1130, code)',
        'voice_stop_keys': dg.arr('u16', 0x1414, 4),
        'title_story_voice': [{'page': p + 1, 'first_ssnd': dg.i16(0x141c + 2 * p),
                               'count': dg.i16(0x1424 + 2 * p)} for p in range(4)],
    }


def dec_player(dg):
    return {
        'next_frame': dg.arr('u8', 0x0c34, 85),
        'frame_dx_right': dg.arr('i16', 0x0c89, 85),
        'frame_dx_left': dg.arr('i16', 0x0d33, 85),
        'frame_y': dg.arr('i16', 0x0ddd, 85),
        'net_swing_frames': dg.arr('u8', 0x0ba0, 12),
        'empty_swing_frames': dg.arr('u8', 0x0bac, 12),
        'coin_toss_frames': dg.arr('u8', 0x0bb8, 14),
        'coin_fx_frames': dg.arr('u8', 0x0bc6, 14),
        'door_frames': dg.arr('u8', 0x0bd4, 3),
        'bump_frames': dg.arr('u8', 0x0bd7, 13),
        'bump_dx_right': dg.arr('i16', 0x0be4, 13),
        'bump_dx_left': dg.arr('i16', 0x0bfe, 13),
        'obstacle_min_x_right_jumpframe15_21': dg.arr('i16', 0x0c18, 7),
        'obstacle_max_x_left_jumpframe15_21': dg.arr('i16', 0x0c26, 7),
        'shop_walk_x_by_level': dg.arr('i16', 0x0e89, 3),
        'net_sign_x_by_level': dg.arr('i16', 0x1066, 3),
        'scene_slot_x': dg.arr('i16', 0x2dd4, 3),
        'scene_reveal_x': dg.arr('i16', 0x2dda, 3),
    }


def dec_climb(dg):
    lay = dg.arr('i16', 0x2950, 80)
    return {
        'layouts_tier_screen_column': [[lay[t * 20 + s * 5:t * 20 + s * 5 + 5] for s in range(4)]
                                       for t in range(4)],
        'column_x': dg.arr('i16', 0x29f0, 5),
        'climb_frames_phase0_3': dg.arr('i16', 0x29fa, 4),
        'climb_dx_phase0_3': dg.arr('i16', 0x2a02, 4),
        'climb_up_dy_phase0_3': dg.arr('i16', 0x2a0a, 4),
        'climb_down_dy_phase0_3': dg.arr('i16', 0x2a12, 4),
        'jump_frames_idx0_4': dg.arr('i16', 0x2a1a, 5),
        'jump_dx_left_idx0_3': dg.arr('i16', 0x2a24, 4),
        'jump_dx_right_idx1_4': dg.arr('i16', 0x2a2e, 4),
        'jump_dy_idx0_4': dg.arr('i16', 0x2a36, 5),
        'fall_frames_step1_7': dg.arr('i16', 0x2a40, 7),
        'fall_dx_step1_7': dg.arr('i16', 0x2a4e, 7),
        'fall_dy_minus32_step1_7': dg.arr('i16', 0x2a5c, 7),
        'land_frames_step1_6': dg.arr('i16', 0x2a6a, 6),
        'land_dx_step1_6': dg.arr('i16', 0x2a76, 6),
        'land_y_step1_6': dg.arr('i16', 0x2a82, 6),
        'creature_frame_tick1_12': dg.arr('i16', 0x2a8e, 12),
        'creature_spawn_permille_by_tier': dg.arr('i16', 0x2aaa, 4),
    }


def frames_until(dg, o, term):
    out = []
    while True:
        v = dg.i8(o)
        out.append(v)
        if v in term:
            return out
        o += 1


def dec_elves(dg):
    beh = []
    spec = [(0, 0x2acb, 0x2adc, 0x2afc, None, 16), (1, 0x2b1c, 0x2b2d, 0x2b4d, None, 16),
            (2, 0x2b6d, 0x2b78, 0x2b8c, None, 10), (3, 0x2ba0, 0x2bb1, 0x2bd1, None, 16),
            (4, 0x2bf1, 0x2bfb, 0x2c0d, 0x2c1f, 9)]
    for b, fo, dxo, yo, so, n in spec:
        e = {'behaviour': b, 'frames_off': h4(fo), 'frames': frames_until(dg, fo, (-1, -2)),
             'dx': dg.arr('i16', dxo, n), 'y': dg.arr('i16', yo, n)}
        if so:
            e['sfx'] = dg.arr('i16', so, n)
        beh.append(e)
    return {
        'level_start_room': dg.arr('u8', 0x2ac8, 3),
        'behaviours': beh,
        'net_frames': dg.arr('i8', 0x2c31, 10), 'net_dx': dg.arr('i16', 0x2c3b, 9),
        'net_y': dg.arr('i16', 0x2c4d, 9),
        'throw_dx_right': dg.arr('i16', 0x2c5f, 5), 'throw_dx_left': dg.arr('i16', 0x2c69, 5),
        'throw_y': dg.arr('i16', 0x2c73, 5), 'projectile_y': dg.arr('i16', 0x2c7d, 9),
        'behaviour_handlers': [dg.far_fmt(0x2c8f + 4 * i) for i in range(6)],
        'elves_by_tier': dg.arr('u8', 0x128e, 4),
    }


def dec_misc(dg):
    return {
        'help': {'titles': [s['text'] for s in dg.strings_in(0x2d1c, 0x2d60)],
                 'title_offs': [s['off'] for s in dg.strings_in(0x2d1c, 0x2d60)],
                 'pager': dec_pager(dg, 0x2cfe),
                 'topics_from_code': {'_source': '1180:0117/0128/0139/014a (code)',
                                      'About the Game': [11600, 11603],
                                      'Using the Keyboard': [11605, 11606],
                                      'Using the Mouse': [11607, 11607],
                                      'Educational Focus': [11608, 11611]}},
        'bndl_pager': dec_pager(dg, 0x0b78),
        'cutscene': {'bndl_ids': dg.arr('u16', 0x2d6d, 2), 'script2_othr_ids': dg.arr('u16', 0x2d71, 2),
                     'bg_pmap_ids': dg.arr('u16', 0x2d75, 2)},
        'hall': {'ped_treasure': dg.arr('i8', 0x2d7e, 6), 'ped_anim': dg.arr('i16', 0x2d84, 6),
                 'ped_x': dg.arr('i16', 0x2d90, 6), 'ped_y': dg.arr('i16', 0x2d9c, 6),
                 'ped_frame_base': dg.arr('i16', 0x2da8, 6)},
        'cursor_ids': dg.arr('u16', 0x1292, 4),
        'palette': {'ega_rgb': [dg.arr('u16', 0x07d8 + 6 * i, 3) for i in range(16)],
                    'vga16_index': dg.arr('u16', 0x0838, 16),
                    'shadow_remap': dg.arr('u8', 0x0876, 256),
                    'invert_spec_template': dg.arr('u8', 0x0976, 33),
                    'dissolve_col_order': dg.arr('u8', 0x09ae, 16),
                    'dissolve_row_shift': dg.arr('u8', 0x09be, 16),
                    'logpalette_header': [dg.u16(0x008a), dg.u16(0x008c)]},
        'users': {'dir1': dg.cstr(0x20aa), 'dir2': dg.cstr(0x20b4), 'read_path': dg.cstr(0x20c7),
                  'write_path': dg.cstr(0x20e4), 'magic_hex': dg.b[0x2101:0x2105].hex(),
                  'player_name_init': dg.cstr(0x224e), 'row_h_init': dg.i16(0x2257)},
        'hud_templates': {'coins': esc(dg.b[0x0f5a:0x0f5f]), 'nets': esc(dg.b[0x0f60:0x0f65]),
                          'points': esc(dg.b[0x0f66:0x0f6e]), 'name': dg.cstr(0x0fe8),
                          'total_treasures': dg.cstr(0x0fee)},
        'dialog_strings': [s for s in dg.strings_in(0x0b24, 0x0b70)],
        'init_app_strings': dg.strings_in(0x04a6, 0x06e2),
        'rtl': {
            'startup_table': [{'kind': dg.u8(o), 'prio': dg.u8(o + 1), 'fn': dg.far_fmt(o + 2)
                               if dg.u8(o) else '1000:%04x' % dg.u16(o + 2)}
                              for o in range(0x3862, 0x387a, 6)],
            'exit_table': [{'kind': dg.u8(0x387a), 'prio': dg.u8(0x387b), 'fn': dg.far_fmt(0x387c)}],
            'sys_nerr': dg.i16(0x339a),
            'exit_hooks': [dg.far_fmt(o) for o in (0x2e04, 0x2e08, 0x2e0c)],
            'eh_terminate': dg.far_fmt(0x31bc), 'eh_unexpected': dg.far_fmt(0x31c0),
            'new_handler': dg.far_fmt(0x3032),
        },
    }


def all_strings(dg):
    out = []
    for r in REGIONS:
        if r['off'] >= BSS_START:
            break
        if r['type'] in ('cstr', 'strs'):
            for s in dg.strings_in(r['off'], r['off'] + r['size']):
                s['region'] = r['name']
                out.append(s)
    return out

# ----------------------------------------------------------------------------
# optional cross references from the Ghidra disassembly export
# ----------------------------------------------------------------------------


def scan_refs(asm_path, dglen):
    hdr = re.compile(r'^// ==== (\S+) @ ([0-9a-f]{4}:[0-9a-f]{4})')
    ins = re.compile(r'^\s+([0-9a-f]{4}:[0-9a-f]{4})\s+[0-9a-f]+\s+(.*)$')
    memre = re.compile(r'(?<![A-Z]S:)\[(?:([A-Z]{2}(?: \+ [A-Z]{2})?) \+ )?0x([0-9a-f]+)\]')
    annre = re.compile(r'(?:_11b8_|-> 11b8:)([0-9a-f]{4})')
    immre = re.compile(r'(?:PUSH|MOV (?:AX|BX|DX|SI|DI),|MOV word ptr \[[^\]]+\],)\s*0x([0-9a-f]+)$')
    parsed = []
    for ln in open(asm_path, encoding='utf-8', errors='replace'):
        m = hdr.match(ln)
        if m:
            parsed.append(('F', m.group(2)))
            continue
        m = ins.match(ln)
        if m:
            parsed.append(('I', m.group(1), m.group(2)))
    refs = []          # (dgoff, kind, insn, func)
    func = '?'
    for i, p in enumerate(parsed):
        if p[0] == 'F':
            func = p[1]
            continue
        addr, text = p[1], p[2]
        if addr.startswith('1248'):
            continue
        code = text.split(' ; ')[0]
        got = set()
        if not re.match(r'(?:CALLF?|J[A-Z]+)\s+0x', code):
            for mm in memre.finditer(code):
                regs, disp = mm.group(1), int(mm.group(2), 16)
                if regs and 'BP' in regs:
                    continue
                s = mm.start()
                if s >= 3 and code[s - 3:s] in ('ES:', 'SS:', 'CS:'):
                    continue
                if disp < dglen and not (regs and disp < 0x80):
                    refs.append((disp, 'idx' if regs else 'mem', addr, func))
                    got.add(disp)
        mi = immre.search(code)
        if mi:
            v = int(mi.group(1), 16)
            if 0x40 <= v < dglen:
                ctx = ' | '.join(parsed[j][2] for j in range(max(0, i - 3), min(len(parsed), i + 4))
                                 if parsed[j][0] == 'I')
                if re.search(r'PUSH DS|,DS\b|0x11b8', ctx):
                    refs.append((v, 'imm', addr, func))
                    got.add(v)
        for am in annre.finditer(text):
            v = int(am.group(1), 16)
            if v not in got and v < dglen:
                refs.append((v, 'ann', addr, func))
                got.add(v)
    return refs


# 1-based tables: the code indexes from (first element - element size), an
# address that lies inside the preceding table.  Map those code bases to the
# region they really address.
CODE_BASE_ALIASES = {0x141a: 0x141c, 0x1422: 0x1424, 0x2a3e: 0x2a40, 0x2a4c: 0x2a4e,
                     0x2a5a: 0x2a5c, 0x2a68: 0x2a6a, 0x2a74: 0x2a76, 0x2a80: 0x2a82,
                     0x2a8c: 0x2a8e}


def attach_refs(regions, refs, dg=None):
    starts = [r['off'] for r in regions]
    per = collections.defaultdict(set)

    def region_index(v):
        i = bisect.bisect_right(starts, v) - 1
        if i < 0 or v >= regions[i]['off'] + regions[i]['size']:
            return None
        return i
    for v, kind, addr, func in refs:
        if kind in ('idx', 'imm') and v in CODE_BASE_ALIASES:
            v = CODE_BASE_ALIASES[v]
        i = region_index(v)
        if i is None:
            continue
        r = regions[i]
        # immediates are only trusted when they hit the region start exactly
        # (or any byte of a string block)
        if kind == 'imm' and v != r['off'] and r['type'] != 'strs':
            continue
        per[i].add(func)
    if dg is not None:           # far pointers stored in DGROUP itself
        for at in dg.rel:
            p = dg.far(at)
            if p and 'dg' in p:
                i = region_index(p['dg'])
                j = region_index(at)
                if i is not None and j is not None and i != j:
                    per[i].add('ptr@%04x' % at)
    for i, r in enumerate(regions):
        r['refs'] = sorted(per.get(i, ()))

# ----------------------------------------------------------------------------
# main
# ----------------------------------------------------------------------------


def check_regions(dg):
    problems = []
    end = 0
    for r in REGIONS:
        if r['off'] != end:
            problems.append('gap/overlap before %04x (expected %04x)' % (r['off'], end))
        ts = region_type_size(r['type'])
        if ts is not None and ts != r['size']:
            problems.append('%04x %s: type size %d != size %d' % (r['off'], r['name'], ts, r['size']))
        end = r['off'] + r['size']
    if end != len(dg.b):
        problems.append('map ends at %04x, DGROUP length %04x' % (end, len(dg.b)))
    if any(dg.b[BSS_START:]):
        problems.append('non-zero byte in _BSS range')
    for o in dg.rel:
        r = region_at(o)
        if r is None or not (r['type'].startswith('far') or r['name'] in
                             ('eh_Vars', '__startupTable', '__exitTable') or
                             r['type'] in ('DlgDesc', 'PagerDesc', 'MenuPopupDef[3]', 'MenuBarDef')):
            problems.append('relocation at %04x lands in %s' % (o, r and r['name']))
    return problems


def region_at(o):
    for r in REGIONS:
        if r['off'] <= o < r['off'] + r['size']:
            return r
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--exe', default=DEFAULT_EXE)
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--asm', default=None, help='Ghidra disassembly.asm for cross references '
                    '(default: %s if it exists)' % DEFAULT_ASM)
    ap.add_argument('--no-asm', action='store_true')
    ap.add_argument('--names-out', default=None, help='write offset/name/type/summary TSV')
    ap.add_argument('--dump-bin', default=None, help='write the raw DGROUP image')
    ap.add_argument('--check', action='store_true', help='validate the region map and exit')
    a = ap.parse_args()

    dg = DG(a.exe)
    probs = check_regions(dg)
    if a.check:
        print('\n'.join(probs) if probs else 'region map OK (%d regions)' % len(REGIONS))
        return 1 if probs else 0
    for p in probs:
        print('warning:', p, file=sys.stderr)

    asm = None if a.no_asm else (a.asm or (DEFAULT_ASM if os.path.exists(DEFAULT_ASM) else None))
    regions = [dict(r) for r in REGIONS]
    if asm:
        attach_refs(regions, scan_refs(asm, len(dg.b)), dg)

    for r in regions:
        r['end'] = h4(r['off'] + r['size'])
        r['bss'] = r['off'] >= BSS_START
        if not r['bss']:
            r['value'] = decode_generic(dg, r)
        r['off'] = h4(r['off'])

    s = dg.seginfo
    out = {
        'source': {
            'exe': os.path.basename(a.exe), 'segment_number': dg.segnum,
            'selector': '%04x' % dg.sel, 'file_offset': '0x%x' % s['fileoff'],
            'length': s['len'], 'minalloc': s['minalloc'], 'flags': '0x%04x' % s['flags'],
            'heap_size': dg.ne.heap, 'stack_size': dg.ne.stack,
            'bss_start': h4(BSS_START), 'relocation_count': len(dg.rel),
            'text_escape': 'bytes outside 0x20..0x7e (and backslash) are written as \\xNN',
        },
        'relocations': [{'at': h4(o), 'target': ptr_str(dg, dg.far(o))} for o in sorted(dg.rel)],
        'regions': regions,
        'tables': {
            'rooms': dec_rooms(dg),
            'clue_words': dec_words(dg),
            'ranks': dec_ranks(dg),
            'music': dec_music(dg),
            'menus': dec_menus(dg),
            'dialogs': dec_dialogs(dg),
            'hints': dec_hints(dg),
            'quiz': dec_quiz(dg),
            'player_anim': dec_player(dg),
            'climb': dec_climb(dg),
            'elves': dec_elves(dg),
        },
        'strings': all_strings(dg),
    }
    out['tables'].update(dec_misc(dg))

    if a.dump_bin:
        open(a.dump_bin, 'wb').write(dg.b)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, 'w', encoding='utf-8') as fp:
        json.dump(out, fp, indent=1)
    print('wrote %s (%d regions, %d relocations, %d strings)' %
          (a.out, len(regions), len(dg.rel), len(out['strings'])))

    if a.names_out:
        with open(a.names_out, 'w', encoding='utf-8', newline='\n') as fp:
            for r in regions:          # same column order as the other re/names/*.tsv (no header)
                summ = r['summary'] or ''
                extra = []
                extra.append('size 0x%x' % r['size'])
                rs = [x for x in r.get('refs', []) if not x.startswith('ptr@')]
                ps = [x[4:] for x in r.get('refs', []) if x.startswith('ptr@')]
                if rs:
                    extra.append('refs ' + ' '.join(rs[:12]) + (' +%d more' % (len(rs) - 12) if len(rs) > 12 else ''))
                if ps:
                    extra.append('pointed to from 11b8:' + (ps[0] if len(ps) == 1 else '%s..%s (%d ptrs)' % (ps[0], ps[-1], len(ps))))
                if not rs and r['owner'] not in ('-', ''):
                    extra.append('owner seg ' + r['owner'])
                fp.write('11b8:%s\t%s\t%s\t%s\n' % (r['off'], r['name'], r['type'],
                                                    (summ + ('; ' if summ else '') + '; '.join(extra))))
        print('wrote', a.names_out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
