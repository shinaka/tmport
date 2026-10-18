# TMTWINCD.EXE: resource manager, DLL and file handling, start-up checks, persistence

Scope: how the Windows CD edition of *Super Solvers Treasure Mountain!* (TMTWINCD.EXE v1.01) finds
and opens its files, how it emulates the Macintosh Resource Manager over Win16 resource DLLs, which
environment checks and dialogs run at start-up, what goes in `TMTWIN.INI`, and the exact layout and
algorithms of `USERS.DAT`. This document is meant to be complete enough to re-implement these parts
without the binary.

Conventions

* **[O]** = observed in decompiled code, disassembly, or bytes. **[I]** = inference.
* Addresses are Ghidra `SSSS:OOOO` (segment N = selector `0x1000 + (N-1)*8`). `DS:XXXX` and
  `DAT_11b8_XXXX` are DGROUP offsets (file offset `0x23c00 + XXXX` in TMTWINCD.EXE).
* All multi-byte integers are little-endian (x86). "far ptr" means 16:16 segment:offset.
* Game text is cited by DGROUP offset plus a few identifying words only.
* Related specs written by other agents: `code_1008-1030.md` (WinMain, WndProc, WinG, event queue),
  `code_1038-1050.md` (resource manager internals, palette remap, blitters), `fmt_pmap.md`,
  `fmt_bndl_ffnt.md`. Function names below follow `code_1038-1050.md` where the two overlap.
* Scratch evidence (scripts, extra Ghidra listings): `C:\Git\TM\extracted\scratch\sys_resources\`
  (`dec1.txt` = decompile of the restore routine 1160:137d, which the shared export lacks; `dec2.txt` =
  the sign-in key handlers 1160:05fc..0737; `dec3.txt` = WndProc close handlers; `ne_rsrc.py` = NE
  resource-table dump of the DLLs; `reloc.py` = NE relocation resolver used for 1140:0062 and
  1080:02ea).

---

## 1. Start-up sequence (overview)

`WinMain` = `1010:0000` **[O]**:

1. If `hPrevInstance != 0`: `1018:0000` enumerates top-level windows (callback `FIND_PREV_WND`
   1018:006b matches parentless windows whose `GetWindowWord(GWW_HINSTANCE)` equals hPrevInstance).
   If one is found, it is activated (restored first if iconic) and the new instance exits. The game
   is single-instance.
2. `1018:00a8` RegisterClass `"RMPDEClass"` (DS:04A6): style 0x2003 (CS_BYTEALIGNWINDOW|CS_VREDRAW|
   CS_HREDRAW), WndProc 1010:0196, icon resource 15000, cursor IDC_ARROW, background BLACK_BRUSH.
3. `1018:0115` InitInstance: saves hInstance (DS:3CDA), builds the INI path (§3.1), creates a
   **maximised, full-screen** window (class DS:04B1, title DS:04BC "Treasure Mountain!", style 0x01C80000 =
   WS_MAXIMIZE|WS_CAPTION|WS_SYSMENU, size SM_CXSCREEN×SM_CYSCREEN). The 512×384 game area is
   centred inside it: `x0 = ((CXSCREEN − (2·CXBORDER + 512)) / 2) & ~3` (DS:086E),
   `y0 = (CYSCREEN − (2·CYBORDER + CYCAPTION + CYMENU + 384)) / 2` (DS:0870). It stores
   `GetDC(hwnd)` at DS:007C and then runs the **environment checks** `1018:0281` (§2). If they
   pass, it calls `ShowWindow(hwnd, SW_SHOWNORMAL)` and `UpdateWindow`, and stores hwnd at DS:3CDC.
   nCmdShow is ignored: WinMain passes the constant 1 (`1010:0049 push 1`).
4. `1018:0563` creates the drawing surfaces, **loads all data DLLs** (§4), creates the palette,
   calls `timeBeginPeriod(16)`, sets DS:0498 = 1 (init done) and allocates the 280 000-byte resource
   scratch buffer (1038:0022).
5. `SetFocus(hwnd)`. It then pumps messages while DS:049E ≠ 0 or the window is iconic, and finally
   runs the game: `1068:0024` (load font FFNT 999), `1118:0000` (main game loop, §6.6).
6. Shutdown, after 1118:0000 returns: `1068:006b` (free font), `1038:0038` (free scratch),
   `1070:0000` (clear screen), `1088:0114` (stop wave output), `1090:02af` (close the MCI sequencer
   and song devices), release the DCs/palette/bitmap (GDI path) or `1008:0230` (WinG DC, bitmap and
   `FreeLibrary(WING.DLL)`). Then `FreeLibrary` on TMT.DLL, the pixmap DLL and TM0..TM8SOUND.DLL
   (in that order), then `timeEndPeriod(16)`.

---

## 2. Environment checks (`CheckSystemRequirements`, 1018:0281) [O]

The checks run in the order below. Any failure shows a modal dialog from the EXE's own resources
through `WinDlg_RunModal` (1080:0000, §2.3) and makes 1018:0281 return 0. InitInstance then fails and
WinMain returns 0, so the program ends. The checks run **before** the window is shown, and DS:3CDC
(the dialog parent) is still 0 at that point.

| # | Check (exact) | On failure |
|---|---|---|
| 1 | `g_isWin40 (DS:04A4) = IsWindows40()` (1018:07ff, §3.2). Not a failure condition. | — |
| 2 | `GlobalCompact(2 000 000)`; `h = GlobalAlloc(GMEM_FIXED, 1 850 000 /*0x1C3A90*/)`; on success `GlobalFree(h)` | Dialog `NOT_ENOUGH_MEMORY_DIALOG` (DS:04F0), OKDIALOGPROC, quit |
| 3 | `v = GetVersion()` low word, byte-swapped to `(major<<8)|minor`; require `v ≥ 0x030A` (3.10) | Dialog `NOTWINDOWS31` (DS:0509), quit |
| 4 | `GetWinFlags() & WF_ENHANCED (0x20)` | Dialog `Not386Enhanced` (DS:0516), quit |
| 5 | `hdc = GetDC(0)`; `planes = GetDeviceCaps(hdc, PLANES=14)`; `bpp = GetDeviceCaps(hdc, BITSPIXEL=12)`; `noPalette = !(GetDeviceCaps(CreateCompatibleDC(hdc), RASTERCAPS=38) & RC_PALETTE 0x100)` | — |
| 6 | Accept only (planes=1, bpp=8) or (planes=4, bpp=1) | Dialog `TooMuchColor` (DS:0525), quit. This covers hi-colour, true-colour and monochrome. |
| 7a | **8 bpp:** if `noPalette` → dialog `TooMuchColor` (DS:0532), quit. Otherwise, if the screen is not exactly 640×480 (SM_CXSCREEN, SM_CYSCREEN), run `CheckDisplayMode()` (1018:04d8, §2.2). | Quit if the user chose Exit |
| 7b | **16 colours (4 planes):** set `allow256 = 0` and always run `CheckDisplayMode()` | Quit if the user chose Exit |
| 8 | `CheckSoundHardware()` (1018:043a, §2.4) | Quit if the user chose Exit |
| 9 | `SelectWinGMode(allow256)` (1018:086d, §2.5) | — |
| 10 | If `g_useWinG (DS:0074)`: `WinG_Load()` (1008:0000). If it fails, **return 0 silently**: no dialog and no GDI fallback. | Silent quit |

There is **no** file-handle check. The dialog `NOT_ENOUGH_FILE_HANDLES` (DIALOG 5) exists in the
resources, but no code names it. `RUNIN16COLORPROC` (1080:02fa) is exported but never passed to
DialogBox. Both are dead **[O]**.

One more failure dialog comes later, from 1018:0563 on the non-WinG 256-colour path: if the game
cannot get at the bits of its 512×1055 off-screen DDB (it reads GDI's internal bitmap object through
the GDI heap selector returned by `SystemHeapInfo`, 1058:1525), it shows `IncompatibleVideo`
(DS:05D9, OKDIALOGPROC), closes MCI, frees the GDI objects and quits.

### 2.1 Dialog resources (TMTWINCD.EXE NE resources) [O]

The EXE's type-15 resource (file offset 0x2AC00) is a Windows 3.0 RT_NAMETABLE. It maps the
string names used by `DialogBox` to DIALOG ordinals. Entries are
`{u16 cb; u16 typeOrd=0x8005; u16 idOrd=0x8000|n; "" ; "NAME"}`. Templates are Win16 DLGTEMPLATEs
with caption "The Learning Company" and font "MS Sans Serif" 8 (DIALOG 4 has no font).

| DIALOG | Name | Controls (id: class) | Proc |
|---|---|---|---|
| 1 | INCOMPATIBLEVIDEO | 0xE11 static text, 1 OK | OKDIALOGPROC |
| 2 | NOT386ENHANCED | 1 OK, 0xE11 text | OKDIALOGPROC |
| 3 | NOTOPTIMUMDISPLAYMODE | 0xE11 text, 0x1069 text, **0xFC checkbox** "keep this message from appearing again", **0xFA Continue** (default), **0xFB Exit** | DESIGNEDFOR640X480PROC |
| 4 | NOTWINDOWS31 | 1 OK, two statics | OKDIALOGPROC |
| 5 | NOT_ENOUGH_FILE_HANDLES | 1 OK, text | (unused) |
| 6 | NOT_ENOUGH_MEMORY_DIALOG | 1 OK, text | OKDIALOGPROC |
| 7 | TOOBADNOSOUND | 0xE11 text, 0x1069 text, 0xFC checkbox, 0xFA Continue, 0xFB Exit | NOMIDIPROC |
| 8 | TOOMUCHCOLOR | 0xE11 text, 1 OK | OKDIALOGPROC |

(Dialog text lives in the dialog templates, Rsrc segments 11E8..1220. A port should read it from the
user's EXE or write its own wording.)

### 2.2 Dialog procedures and `WinDlg_RunModal` [O]

`WinDlg_RunModal(LPCSTR name, FARPROC proc)` = 1080:0000: sets DS:09DC = 1, pauses music if the app is
active (1010:05c8 → 1090:0536), `MakeProcInstance` + `DialogBox(hInst, name, DS:3CDC, proc)`,
resumes music unless iconic (1010:05db → 1090:0540), sets DS:09DC = 0, and returns the DialogBox
result.

All procs centre the dialog on the screen on WM_INITDIALOG (1080:005f:
`x = (CXSCREEN − w)/2, y = (CYSCREEN − h)/2`, MoveWindow).

* `OKDIALOGPROC` (1080:01b9): WM_COMMAND id 1 or 2 → `EndDialog(0)`.
* `NOMIDIPROC` (1080:0201) and `DESIGNEDFOR640X480PROC` (1080:026c; jump table at 1080:02ea):
  * id 0xFA (Continue) → `EndDialog(IsDlgButtonChecked(0xFC) ? 1 : 0)`.
  * id 0xFB (Exit) or 2 (IDCANCEL / close) → `EndDialog(-1)`.
  * id 0xFC (checkbox) → not handled (default processing).
  * DESIGNEDFOR640X480PROC's table at 1080:02ea (bytes read directly): ids `{0x0002, 0x00FA, 0x00FB,
    0x00FC}`, then handlers `{02d3, 02b7, 02d3, 02b5}`. 02d3 = `EndDialog(-1)`, return TRUE; 02b7 =
    `EndDialog(IsDlgButtonChecked(0xFC) != 0)`, return TRUE; 02b5 = return FALSE.

`CheckDisplayMode()` (1018:04d8) and `CheckSoundHardware()` (1018:043a) use these results the same
way:

```
if INI value (key K, default "TRUE") lstrcmp== "TRUE" and <condition>:
    r = WinDlg_RunModal(name, proc)
    if r == -1: return 1                 // user chose Exit -> abort start-up
    WriteProfileString([TMTWIN] K = (r != 0) ? "FALSE" : "TRUE")
return 0
```

So ticking the checkbox writes `FALSE`, which suppresses the dialog on later runs. Continuing
without the tick writes `TRUE`. The comparison uses `lstrcmp`, which is **case-sensitive**: any
value other than exactly `TRUE` counts as false.

### 2.3 Display condition

`CheckDisplayMode` (key `CheckDisplay`, dialog `NotOptimumDisplayMode` DS:05A4) runs in 16-colour
mode always, and in 256-colour mode only when the screen is not 640×480 (check 7a/7b above).

### 2.4 Sound hardware check (1018:043a) [O]

```
check  = (GetProfileString(TMTWIN, "CheckSound", "TRUE") == "TRUE")
wave   = WaveCheck()          // 1088:0000: DS:09F2 = (waveOutGetNumDevs() != 0); returns it
ok     = wave && MidiInit()   // 1090:0000 is called ONLY if a wave device exists
if !ok && check: dialog "TooBadNoSound" (DS:055B, NOMIDIPROC) as in §2.2
```

`MidiInit()` = 1090:0000 [O]. It returns 1 when MIDI music can be used and also leaves the MCI
sequencer open (device id DS:0A50, DS:0A4A = 1):

1. Builds the music directory DS:4AF4 = directory of the running EXE (GetModuleFileName, cut after
   the last `\` or `:`) + `"SOUNDS\"` (DS:0B16) (1090:023d, done only if DS:4AF4 is empty).
2. `midiOutGetDevCaps(MIDI_MAPPER = -1, &caps, 0x32)`. If it returns MMSYSERR_NODRIVER (6) → fail.
3. Opens `GetWindowsDirectory() + "\system\midimap.cfg"` (DS:0ACC) with `_lopen(…, OF_READ)`. If it
   cannot be opened → fail. Otherwise: `_llseek(6)`, read u16 `n`, `_llseek((n−1)·0x36 + 0x12)`,
   read 16 bytes, uppercase them (`toupper` 1000:0998), and `strstr` for each of `"ADLIB"`,
   `"AD LIB"`, `"MVI PRO"`, `"BAS"`, `"BASE"`, `"THUNDER BOARD"` (DS:0AE0..0AFE). Any hit sets
   DS:4B76 = 0 ("base-level" synth, MIDI files `B*.MID`). Otherwise DS:4B76 stays 1 ("extended" /
   General MIDI, `E*.MID`). Any seek or read failure skips this test (DS:4B76 stays 1) but MidiInit
   continues.
4. `MCI_OPEN` (0x803) with MCI_OPEN_TYPE `"sequencer"` (DS:0B0C) → DS:0A50. Opens song id 9000
   (1090:02e9; the file is `DS:4AF4 + ('B'|'E') + "castle.mid"`, see the sound spec) → DS:0A52.
   Then `MCI_STATUS` (0x814) with item MCI_SEQ_STATUS_PORT (0x4003). The song is closed (1090:0413).
   If the status call fails, or the port is not MIDI_MAPPER (0xFFFF), it closes the sequencer, clears
   DS:0A4A and fails. Otherwise it returns DS:0A4A (1).

### 2.5 WinG / GDI selection (`SelectWinGMode(allow256)`, 1018:086d) [O]

```
if g_isWin40:           g_useWinG = allow256          // INI ignored on Windows 95
elif allow256:          g_useWinG = (GetProfileString(TMTWIN,"UseWinG","TRUE") == "TRUE")
else:                   g_useWinG = 0
if argc >= 2 and allow256 and argv[1][0] in ('3','4'):
    g_useWinG = (argv[1][0] != '3')                    // '3' = GDI, '4' = WinG
    WriteProfileString(TMTWIN, "UseWinG", g_useWinG ? "TRUE" : "FALSE")
```

(argc/argv are the Borland RTL globals DS:6694 / far DS:6696.) `WinG_Load` (1008:0000) does
`LoadLibrary("WING.DLL")` (DS:048E). If the handle is ≥ 32, it fetches ordinals 1001 (WinGCreateDC),
1002 (RecommendDIBFormat), 1003 (CreateBitmap), 1004 (GetDIBPointer), 1005 (GetDIBColorTable),
1006 (SetDIBColorTable), 1010 (WinGBitBlt) and 1009 (WinGStretchBlt) into DS:3CBA..3CD8. It returns
`handle > 31`. **Fallback:** none at run time. If WinG is selected but missing, the game exits
silently. The only ways to force GDI are `UseWinG=FALSE` (Windows 3.1 only) or command-line argument
`3`.

Note: SETUP writes the key `Win31UseWinG` (SETUP.INS strings at 0x216E, 0x5624), **not**
`UseWinG`. The EXE never reads `Win31UseWinG`, so on a fresh install `UseWinG` defaults to TRUE
[O for both strings; I for the effect].

---

## 3. TMTWIN.INI

### 3.1 Path (`BuildIniPath`, 1018:01f5) [O]

```
n = GetModuleFileName(hInst, buf=DS:3CDE, 0x81)
scan backwards from buf+n for '\' or ':'; cut the string after it; SI = index of the separator
lstrcpy(buf, "C:\TLCWIN\TMTWINCD\")             // DS:04CF, overwrites the EXE directory
lstrcat(buf, (SI + 13 < 0x81) ? "TMTWIN.INI" : "?")   // DS:04E3 / DS:04EE
```

So the INI path is **always** `C:\TLCWIN\TMTWINCD\TMTWIN.INI`. The EXE path only affects the
length test. The INI and `users.dat` live on the hard disk because the program runs from the
read-only CD **[I]**. Win16 `WritePrivateProfileString` does not create directories, so if
`C:\TLCWIN\TMTWINCD` is missing, INI writes are lost and reads return the defaults **[I]**.

### 3.2 Keys (section `[TMTWIN]`, DS:053F and copies) [O]

| Key | Default | Read by | Written | Meaning |
|---|---|---|---|---|
| `Win40VersionNum` | `"858"` (DS:0694) | 1018:07ff | **Every start**, with the value just read | `IsWindows40 = (atoi(value) ≤ (major<<8 \| minor))`, unsigned compare. 858 = 0x35A ("3.90"). Win95 reports 3.95 (863) and qualifies; Win 3.1 (778) does not. |
| `UseWinG` | `"TRUE"` | 1018:086d (Win 3.x, 256 colours only) | Only on a command-line override `3`/`4` | WinG vs GDI back buffer |
| `CheckSound` | `"TRUE"` | 1018:043a | After the TooBadNoSound dialog | Show the no-sound dialog |
| `CheckDisplay` | `"TRUE"` | 1018:04d8 | After the NotOptimumDisplayMode dialog | Show the display-mode dialog |
| `CDDrive` | — | **never read by the EXE** | by SETUP | CD path for the installer; see §7 |
| `Win31UseWinG` | — | never read | by SETUP (FALSE, later TRUE after the WinG profiling test) | installer-only |

All profile I/O goes through `GetPrivateProfileString` / `WritePrivateProfileString` with the path
in DS:3CDE and an 80-byte buffer. These are the only four EXE functions that touch the INI
(1018:07ff, 043a, 04d8, 086d). The INI holds **no** game options. Music, sound, voice and
new-player-message settings are stored per player in USERS.DAT (§5, §6).

### 3.3 Initial contents written by SETUP [O strings; I for the pairing]

The InstallShield script `SETUP.INS` (string scan, scratch `setup_ins_strings.txt`) contains these
`[TMTWIN]` key/value strings in sequence, next to `TMTWIN.INI` (0x2112) and `\TLCWIN\TMTWINCD\`
(0x6D3):

| SETUP.INS offset | Key | Value written |
|---|---|---|
| 0x2144 / 0x2157 | `Win40VersionNum` | `858` |
| 0x216E / 0x217E | `Win31UseWinG` | `FALSE` |
| 0x2197 / 0x21A7 | `CheckDisplay` | `TRUE` |
| 0x21D3 / 0x21E1 | `CheckSound` | `TRUE` |
| 0x2247 (after `\TMTWINCD\` at 0x2215) | `CDDrive` | presumably `<drive>:\TMTWINCD\` |
| 0x5624 / 0x5634 | `Win31UseWinG` | `TRUE`, after the WinG display test (`profwing.exe`, 0x5569) |

So a freshly installed INI equals the EXE's built-in defaults, apart from the two keys the EXE
never reads. A port can treat a missing INI exactly like a fresh install.

---

## 4. DLL loading and colour-depth selection (1018:0563) [O]

1. **Surfaces.** In GDI mode (`g_useWinG == 0`), it reads planes and bpp of the screen DC,
   `SystemHeapInfo` (stores hGDISegment at DS:42A0), the window DC (DS:4296), and a compatible memory
   DC (DS:4298). It sets **`g_paletteDevice` (DS:086C) = RASTERCAPS(memDC) & RC_PALETTE**, creates the
   512×1055 off-screen DDB (1058:1493), and checks that its bits are reachable (else IncompatibleVideo,
   §2). 1058:1493 is the only place that measures memory after start-up **[O]**:
   ```
   i = -1
   do { before = GetFreeSpace(0); i++;
        bm[i] = CreateBitmap(512, 1055, planes, bpp, NULL);      // local array of 100 handles
        t = (int16)((int16)(512*1055) * bpp) / 8;                // 16-bit IMUL/IDIV at 1058:14dc
   } while ((u32)(GetFreeSpace(0) + (i32)t) > before);
   DeleteObject(bm[0..i-1]); return bm[i];
   ```
   **[I]** The intent was to keep creating bitmaps until one really consumed `w·h·bpp/8` bytes of
   global memory (a DDB whose bits the game can reach, not one held in display memory). The 16-bit
   arithmetic overflows: `t` = −512 at 8 bpp (so the loop normally stops after the first bitmap) and
   1984 at 1 bpp × 4 planes. A port does not need any of this.

   In WinG mode, step 1 instead calls `GetDC(hwnd)` and `WinG_CreateBackBuffer(512, 1055)`
   (1008:00ee), and DS:086C keeps its initialised value **1** (DGROUP image).
2. **Colour depth:** if `g_paletteDevice == 0` (only possible in the 16-colour 4-plane mode), it
   allocates a 4-bpp buffer of 512·1055/2 bytes (`GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT)`,
   1058:153b) and uses **TMT16.DLL**. Otherwise it uses **TMT256.DLL**.
3. LoadLibrary, by bare file name, storing each HINSTANCE:

| DGROUP | File (string at) | Contents |
|---|---|---|
| 0872 | `TMT256.DLL` (05EB) or `TMT16.DLL` (05F6) | PMAP pixmaps |
| 09F0 | `TMT.DLL` (0600) | BNDL, RRGB, FFNT, OTHR |
| 09DE | `TM0SOUND.DLL` (0608) | SSND 7000..7099 |
| 09E0 | `TM1SOUND.DLL` (0615) | SSND 7100..7999 |
| 09E2 | `TM2SOUND.DLL` (0622) | SSND 4000..4399 |
| 09E4 | `TM3SOUND.DLL` (062F) | SSND 4400..4799 |
| 09E6 | `TM4SOUND.DLL` (063C) | SSND 4800..5199 |
| 09E8 | `TM5SOUND.DLL` (0649) | SSND 5200..5599 |
| 09EA | `TM6SOUND.DLL` (0656) | SSND 5600..5999 |
| 09EC | `TM7SOUND.DLL` (0663) | SSND 6000..6399 |
| 09EE | `TM8SOUND.DLL` (0670) | SSND 6400..6799 |

   If a handle is < 32 (load error), the code calls `PostQuitMessage(0)` and **carries on**. No
   message is shown. All eleven libraries load at start and stay loaded until shutdown (the sound
   DLLs total about 104 MB, but Win16 LoadLibrary maps only headers and resource tables; resources
   are read on demand **[I]**).

   **The posted WM_QUIT is never acted on [O].** No code in the EXE compares a message with
   WM_QUIT (0x12). Every `PeekMessage(PM_REMOVE)` loop (WinMain pump 1010:0071, `Input_Flush`
   1028:0013, `Evt_GetNextEvent` 1028:004f) just translates and dispatches whatever it gets, and the
   only other PeekMessage calls (1088:0114, 1088:0175) filter for MM_WOM_DONE (0x3BD) only. So the
   game keeps running with the error code (< 32) stored as the DLL handle. **[I]** The first
   resource request routed to that DLL then fails in `FindResource`, `Res_LoadCopy` shows the
   "out of memory" box and returns NULL (§5.5), and the caller dereferences NULL. A port should
   instead refuse to start, with a clear message naming the missing file.
4. `1048:02b2` creates the 256-entry logical palette (or sets 16-colour mode when DS:086C = 0).
   In GDI mode it then calls `1070:0000` (clear), `timeBeginPeriod(16)`, sets DS:0498 = 1, and
   calls `Res_AllocScratch()`.

Search path [I]: Win16 `LoadLibrary("X.DLL")` looks in (1) the current directory, (2) the Windows
directory, (3) the system directory, (4) **the directory of the task's EXE**, (5) PATH, and (6)
mapped network directories. Because the EXE runs from `<CD>:\TMTWINCD\`, step 4 finds all DLLs on
the CD. WING.DLL is found in the system directory (installed from `WING.Z`).

---

## 5. Mac Resource Manager emulation (segments 1038 and 1030)

### 5.1 How a string type reaches an integer resource [O/I]

Every lookup calls `FindResource(hDll, MAKEINTRESOURCE(id), lpType)`, where `lpType` is a far
pointer to a 4-char Mac type string in DGROUP (`"BNDL"` DS:0741, `"RRGB"` 0746, `"FFNT"` 074B,
`"PMAP"` 0750, `"SSND"` 0755, `"OTHR"` 075A; the font loader passes `"FFNT"` at DS:09A8)
**[O]**. The DLLs' NE resource tables contain only integer types 0x7F01.. (stored as 0xFF01..).
Each DLL carries a **type-15 RT_NAMETABLE**, and Win16 KERNEL uses it to translate string names into
ordinals **[I: standard Win 3.0 mechanism; same algorithm as Wine's `NE_FindNameTableId`]**. Entry
format **[O]** (all entries are 12 bytes in every DLL; the table ends with a u16 0):

| off | size | field |
|---|---|---|
| 0 | 2 | `cb` = 12 (entry length) |
| 2 | 2 | type ordinal with bit 15 set: 0xFF01 = type 32513, 0xFF02 = 32514, … |
| 4 | 2 | resource id ordinal (no high bit), e.g. 0x03E8 = 1000 |
| 6 | 5 | type name, NUL-terminated, e.g. `"PMAP\0"` |
| 11 | 1 | id name: empty string `"\0"` |

Resolution: find the entry whose type name equals `lpType` (case-insensitive in KERNEL/Wine) and
whose id ordinal equals `id`. Then look up NE resource (type = ord & 0x7FFF, id = 0x8000|id). An
(type, id) pair that is not listed in the name table is **not found**. Verified per DLL
**[O, scratch `nt.py`]**: every name-table entry has a matching NE resource and vice versa.

| DLL | entries | name → ordinal |
|---|---|---|
| TMT.DLL | 557 | BNDL→32513 (79), FFNT→32514 (1), OTHR→32515 (476), RRGB→32516 (1) |
| TMT16.DLL / TMT256.DLL | 764 each | PMAP→32513 (ids 1000..2448) |
| TM0..TM8SOUND.DLL | 42/203/400×6/216 | SSND→32513 |

Re-implementation: parse each DLL's NE resource table and its type-15 table once, and build the map
`(typeName, id) → (file offset, length)`.

NE resource table (standard Win16 format; the values in brackets were observed in all 12 DLLs with
scratch `ne_rsrc.py`) **[O]**:

| Where | Field |
|---|---|
| file +0x3C | u32 offset of the NE header [0x90] |
| NE +0x24 | u16 offset of the resource table, relative to the NE header |
| rsrc +0 | u16 `alignShift` [9] |
| then, repeated | TYPEINFO `{u16 typeId; u16 count; u32 reserved}` followed by `count` NAMEINFO records. The list ends with `typeId == 0`. typeId has bit 15 set for an integer type [0x800F first, then 0xFF01, 0xFF02, …] |
| NAMEINFO (12 bytes) | `{u16 offset; u16 length; u16 flags; u16 id; u16 handle; u16 usage}`. File offset = `offset << alignShift`, length = `length << alignShift`, id has bit 15 set for an integer id. flags = 0x0C30 for every payload resource and 0x1C30 for the name table (bit 0x1000 = DISCARDABLE, 0x0010 = MOVEABLE, 0x0020 = PURE; **[I]** 0x0C00 is the usual DPL-3 bits) |

`length << alignShift` is what Win16 `SizeofResource` returns **[I: documented KERNEL behaviour;
consistent with RRGB 900 being treated as 1024 bytes]**, so it is the "rounded size" used
throughout §5.4–§5.8. The type-15 table is itself the resource `(0x800F, 0x8001)`.

### 5.2 id → Mac type (`Res_TypeForId`, 1038:038a) [O]

The game requests resources by **id only**. The type comes from the id range (signed 16-bit
compares):

| id | type |
|---|---|
| < 900 | BNDL |
| 900..949 | RRGB |
| 950..999 | FFNT |
| 1000..3999 | PMAP |
| 4000..9999 | SSND |
| ≥ 10000 | OTHR |

### 5.3 (type, id) → DLL [O]

The DLL is chosen by the **first character** of the type plus the id (same routing in 1038:00c9,
03da and 0ba8; 00c9/03da compare the id as signed, 0ba8 as unsigned, which makes no difference for
the ids in use):
`'P'` → DS:0872 (pixmap DLL). `'S'` → the sound DLL by id range in the §4 table. Any other `'S'` id
(6800..6999, 8000..9999) → no lookup at all (size 0; the load result is undefined or NULL).
Anything else → TMT.DLL (DS:09F0).

### 5.4 Sizes [O]

`Res_GetSize(type, id)` (1038:00c9) = `SizeofResource(FindResource(…))`, i.e. the **NE-aligned
length** (multiple of 512, alignment shift 9), not the payload length. For type `"BNDL"` the result
is **doubled** (`1000:1344(type, "BNDL" DS:073C, 4) == 0`; 1000:1344 is a `memcmp`-style compare of n
bytes). The return value is a u32 in DX:AX. No other size information exists: there is no
`GetHandleSize` equivalent. Callers use this rounded size, the per-member size table inside an
expanded BNDL, or a format's own header fields. (KERNEL `GlobalSize` is imported, but its only
reference is a relocation at 1000:3ab5 inside the Borland RTL heap code, not in the game **[O]**.)

### 5.5 Loading pipeline (`Res_LoadCopy(type, id)`, 1038:03da) [O]

```
size = Res_GetSize(type, id)                         // rounded; ×2 for BNDL
h    = dll_for(type, id)
hRes = LoadResource(h, FindResource(h, MAKEINTRESOURCE(id), type))
src  = LockResource(hRes)
if type[0]=='P' and g_paletteDevice:                 // 256-colour mode only
    PixMap_RemapToSystem(src)                         // 1058:1d28: in place, before the NULL test
if src == NULL:
    MessageBox(NULL, DS:075F "You are out of memory.", DS:0776 "Memory Allocation Failed",
               MB_SYSTEMMODAL|MB_ICONHAND /*0x1010*/)
    return NULL
dst = NewPtr(size)                                   // 1030:0000
BlockMove(src, dst, BNDL ? size/2 : size)            // 1050:0e10, forward byte copy
GlobalUnlock(hRes); FreeResource(hRes)
g_inResourceLoad (DS:09D4) = 1; Input_Flush() (1028:0126); g_inResourceLoad = 0
return dst
```

* `PixMap_RemapToSystem` (1058:1d28) [O]: PMAP header = u16 width, u16 height. If the 4 bytes at +4
  are `"RUND"` (DS:0997; check in 1058:0566), the RLE stream starts at +8 (control byte c: bit7 = 0
  → c&0x7F literal bytes follow; bit7 = 1 → one byte repeated c&0x7F times). Each pixel or literal
  byte is translated through the 256-byte table at DS:4193, which the palette code (1048:010a,
  1048:03bf) builds. A raw PMAP has w·h bytes at +4, all translated. 16-colour PMAPs are not
  touched. Details: `fmt_pmap.md`, `code_1038-1050.md` §4.4.
* `Input_Flush` (1028:0126 → 1028:0013) runs a `PeekMessage(PM_REMOVE)` / Translate / Dispatch
  loop, then clears the pending event type DS:3D82 and the pending key DS:06EC. **Input that
  arrives during a resource load is thrown away.** Menu commands dispatched during the pump are
  ignored, because the menu dispatcher 1078:0135 does nothing while DS:09D4 = 1.
* **Error handling:** a failed `FindResource` passes 0 on to `LoadResource`, so a missing
  resource ends in the same "out of memory" message box and a NULL return. Callers do not test for
  NULL (for example 1038:076f compares the returned pointer's bytes directly).
* **Unrouted sound ids** (`'S'` type, id 6800..6999 or 8000..9999): no branch is taken, so the locked
  pointer `[BP-4]` and the resource handle in DI are never written. Neither is initialised at entry
  (1038:03da..03fa only stores the size) **[O]**. The result is undefined. A port should treat this
  as "not found". No game code is known to request such ids.
* **In-place remap [O/I]:** `PixMap_RemapToSystem` writes into KERNEL's loaded copy of the resource,
  not into the private copy. **[I]** This is harmless in practice because the payload resources are
  not discardable, and `FreeResource` right after the copy drops the usage count to 0 and frees
  the block, so the next load reads fresh bytes from the file. The exception: while
  `Res_LockInPlace` holds a PMAP, loading the same PMAP again would remap it twice. A port should
  remap the copy only.

### 5.6 BNDL trees (`GetResource`, 1038:076f; far wrapper 1038:08c5) [O]

On-disk BNDL: `"BNDL"`, u16 count at +4, u16 0 at +6, `count` × u32 member ids at +8 (low u16 =
id; all observed high words are 0). The 79 BNDLs hold 532 PMAP and 41 OTHR member references.
Counts range from 2 to 69, there are no nested BNDLs, and every member exists **[O, scratch
`bndl.py`]**.

```
GetResource(id):
    p = Res_LoadCopy(Res_TypeForId(id), id)          // BNDL buffers are allocated at 2× size
    if memcmp(p, "BNDL", 4) == 0:
        n = u16 at p+4
        for i in 0..n-1:
            sub = u16 at p+8+4i
            u32 at p+8+4(n+i) = Res_GetSize(Res_TypeForId(sub), sub)   // member's rounded size
            u32 at p+8+4i     = far ptr GetResource(sub)               // recursive load
    return p
```

Expanded BNDL in memory: `+0 "BNDL"`, `+4 u16 n`, `+8+4i` far pointer to member i, `+8+4(n+i)`
u32 rounded size of member i. Accessors: `Bndl_GetCount` 1038:0088 (u16 at +4), `Bndl_GetEntry`
1038:0061 (u32 at `+8+4i`; with `i ≥ n` it reads the size table), `Bndl_SetEntry` 1038:00a1.

### 5.7 Memory API (segment 1030) [O]

| Addr | Name | Semantics |
|---|---|---|
| 1030:0000 | `NewPtr(u32 size) → far ptr` | `GlobalAlloc(GMEM_MOVEABLE, size)`. On failure: `GlobalCompact(1 000 000)` and retry. If that fails too: MessageBox(NULL, DS:06FC "You are out of memory.", DS:0713 "Memory Allocation Failed", 0x1010). Returns `GlobalLock(h)` (NULL if h = 0). Memory is not zeroed. |
| 1030:005d | `DisposePtr(far ptr)` | `h = GlobalHandle(SELECTOROF(p)); GlobalUnlock(h); GlobalFree(h)` |
| 1030:0082 | `DisposePtrDeep(far ptr)` (= ReleaseResource) | If `p` starts with "BNDL", recursively dispose the n member pointers at p+8+4i. If `p+6` starts with "BNDL" (flat 'F' format), do the same relative to p+6. Then `DisposePtr(p)`. |
| 1050:0e10 | `BlockMove(src, dst, u32 n)` | Forward byte copy (overlap unsafe if dst > src) |

### 5.8 API surface used by the rest of the game

| Addr | Signature | Semantics | Callers |
|---|---|---|---|
| 1038:08c5 | `void far* GetResource(int id)` | §5.6. The caller owns the tree and frees it with `DisposePtrDeep` | 31 sites |
| 1038:08d4 | `u32 GetResourceInto(int id, void far* dst)` | `size = Res_GetSize(...)`; `t = GetResource(id)`; `BlockMove(t, dst, size)`; `DisposePtr(t)` (shallow, so BNDL members stay allocated and are now referenced from `dst`); returns size. `dst` must hold the **rounded** size, e.g. RRGB 900 = 1024 bytes into DS:3D90..418F (1118:0012) | 1 site |
| 1038:088a | `u32 Res_LoadToScratch(int id)` | `GetResourceInto(id, g_resScratch)`; updates the high-water mark DS:3D88. No bounds check against 280 000 | 24 sites |
| 1038:03da | `void far* Res_LoadCopy(char far* type, int id)` | §5.5 (direct type; only the font loader 1068:0000 uses it) | 1 |
| 1038:0ba8 | `u32 Res_LockInPlace(unsigned id)` | Frees the scratch buffer, loads and locks the resource **without copying** (PMAP remapped in place), pumps messages, sets `g_resScratch = p`, `DS:073A = hRes`. Returns the rounded size. Used for synchronous sound playback (1088:01a9) and 1138:0283 | 2 |
| 1038:0ef8 | `void Res_UnlockInPlace(void)` | `GlobalUnlock` + `FreeResource(DS:073A)`, clears it, then `Res_AllocScratch()` | 2 |
| 1038:0022 | `void Res_AllocScratch(void)` | `g_resScratch (DS:3D8C) = NewPtr(280 000)` | init |
| 1038:0038 | `void Res_FreeScratch(void)` | If allocated and nothing is locked in place: `DisposePtrDeep(scratch)`, then set it to NULL | shutdown, 0ba8 |
| 1038:0061 / 0088 / 00a1 | BNDL accessors | §5.6 | 36 / 9 / dead-code users |
| 1038:00c9 | `u32 Res_GetSize(char far* type, int id)` | §5.4 | internal |
| 1038:038a | `char far* Res_TypeForId(int id)` | §5.2 | internal |
| 1030:0000 / 005d / 0082 | NewPtr / DisposePtr / DisposePtrDeep | §5.7 | 15 / 2 (internal) / 33 |

Dead code (no references) [O]: 1038:0000, 1038:08eb (flat "F" header loader, called only from dead
code), 1038:093a, 1038:0956, 1038:09a9, 1038:09e4 and 1038:0ad2. The flat format is
`{'F', 0, u32 size, payload}`; see `code_1038-1050.md` §2.4.7.

### 5.9 Caching, locking, purging [O]

* **No cache.** Every `GetResource` reads the resource afresh from the DLL into a new moveable,
  locked global block. Pointers stay valid until the caller frees them (blocks stay locked).
* Resource handles are held only for the duration of the copy (GlobalUnlock + FreeResource
  immediately). The one exception is `Res_LockInPlace`, which keeps one resource locked until
  `Res_UnlockInPlace`.
* **Purging:** the game never purges explicitly. On allocation failure it calls
  `GlobalCompact(1 000 000)` and retries once, then shows the out-of-memory box. The shared 280 000-byte
  scratch block is the main reuse mechanism: most screens load their art there and the next load
  overwrites it.

### 5.10 Re-implementation sketch

```
struct ResRef { const uint8_t* data; uint32_t roundedSize; };
const char* typeForId(int id);                  // §5.2
Dll& dllFor(char t0, int id);                   // §5.3
ResRef find(type, id);                          // via the type-15 name table, §5.1
uint32_t resSize(type,id) { return find(type,id).roundedSize * (type=="BNDL" ? 2 : 1); }
Buffer loadCopy(type,id) {                      // copy roundedSize bytes, then remap PMAP if 256-colour
    Buffer b(resSize(type,id)); memcpy(b, find(...).data, find(...).roundedSize);
    if (type[0]=='P' && palette256) remapPmap(b); return b; }
Tree getResource(id) { recursive per §5.6 }
```

Keep the rounded sizes: some callers copy that many bytes into fixed DGROUP buffers. Remap after
the copy instead of in the source bytes.

---

## 6. USERS.DAT (player roster and saved games)

### 6.1 File access [O]

* Path: `c:\tlcwin\tmtwincd\users.dat` (DS:20C7 for reading, DS:20E4 for writing). Hard-coded, so
  the CD drive and EXE location do not matter.
* **Load** `Users_ReadFile(buf, len)` (1158:0040):
  `fd = open(path, O_RDONLY|O_BINARY /*0x8001*/)`. If `fd < 0`: zero `buf` and return 0.
  Otherwise `read(fd, hdr, 4)` (the header is read but **not checked**),
  `n = read(fd, buf, len)`, `close(fd)`. If `n == len`, return 1. Otherwise zero `buf` and return 0.
* **Save** `Users_WriteFile(buf, len)` (1158:00bb): `mkdir("c:\tlcwin")` (DS:20AA) and
  `mkdir("c:\tlcwin\tmtwincd")` (DS:20B4); errors are ignored (Borland `mkdir` = INT 21h AH=39h,
  1000:0522). Then `fd = open(path, O_WRONLY|O_CREAT|O_TRUNC|O_BINARY /*0x8302*/, S_IREAD|S_IWRITE
  /*0x180*/)`, `write(fd, "USR\x1A", 4)` (DS:2101), `write(fd, buf, len)`, `close`. If either write
  is short (or the open failed), it shows the in-game message box descriptor DS:2084 (title DS:209E
  "Write Error", text DS:2048 "Writing to the disk has failed…") via 10C0:0AE5. No retry.
* Borland RTL used: `open` 1000:1495, `read` 1000:1ae2, `write` 1000:1ea4, `close` 1000:0f82
  (thin wrappers in segment 1040).

### 6.2 File layout [O]

| File offset | Size | Content |
|---|---|---|
| 0 | 4 | Magic `55 53 52 1A` ("USR" + Ctrl-Z). Written on save, ignored on load. |
| 4 | 7 × 0x62F = 0x2B49 (11 081) | 7 player slots, 0x62F (1583) bytes each, stored back-to-back |

Total file size = **11 085 bytes**. There is **no checksum, version or count field**. The number of
players is the count of leading slots whose first byte (`name[0]`) is non-zero, at most 7
(1160:0dd6: `count = 0; while (count < 7 && slot[count].name[0]) count++`). Slots are kept
**sorted by total treasures, descending** (§6.5).

### 6.3 Slot record layout (0x62F bytes) [O: offsets/types from 1160:0f28 (save) and 1160:137d (restore)]

"Var" is the DGROUP global the field is copied from (on save) and to (on restore). The "New player"
column lists the values set by 1160:1632 when a player is created. "Meaning" is **[I]** unless
marked otherwise. These variables belong to the game-state code. Names and meanings for +0x15..+0x2C
and the spot arrays follow the gameplay specs `code_10e0-10f0.md` §3.3 and `code_10f8-1148.md`
§3.2–§3.5, which observed them; this document checked only the offsets and copy code.

| Off | Size | Type | Var | New player | Meaning |
|---|---|---|---|---|---|
| 0x000 | 9 | char[9] | DS:224E | typed name | Player name, NUL-terminated, at most 8 chars. Written with `strcpy`, so bytes after the NUL keep whatever was there before. [O] |
| 0x009 | 1 | u8 | 5E58 | 0 | Mountain tier = rank/2 (0..3), recomputed after each climb by 10e8:0790 [O]. Selects per-tier tables and the clue set OTHR 10550+tier (1100:04b5) [O] |
| 0x00A | 4 | u32 | 5E62 | 0 | **Total treasures** (lifetime score). Sort key. Drives rank/stars (§6.8). Saturates at 0xFFFFFFF0 (10f0:030d) [O] |
| 0x00E | 1 | u8 | 0B96 | (see §6.6) | **Music and Sound on** (0/1) [O: toggled by 1128:012f, Options menu handler 1140:00cc] |
| 0x00F | 1 | u8 | 0B97 | unchanged | **Voice on** (0/1) [O: 1140:00f7 "Do you want the Voice on?"] |
| 0x010 | 1 | u8 | 5F00 | 1 | **New Player Messages on** [O: 1140:0015] |
| 0x011 | 4 | u32 | 5F01 | 0xFFFFFFFF | Bitmask of new-player messages not yet shown. A bit is cleared when its message is shown (1130:0000). Turning the option off clears it to 0, turning it on sets all bits [O] |
| 0x015 | 1 | u8 | 5E59 | — | **Has the key** for the current level (`g_haveKey` in `code_10e0-10f0.md`). Set when the spot with content 3 is found (10e0:0447 → 10e8:06c9(1)), selects a status-bar picture (10e8:026e) [O] |
| 0x016 | 2 | u16 | 5E5A | 10 | **Coins** (`g_coins`; Enter uses one). 2-digit display at x = 0x10F (10e8:02cf); changed via 10e8:06ff, which refuses to go negative; +10 refill pickup when coins = nets = 0 (1110:00aa) [O] |
| 0x018 | 2 | u16 | 5E5C | 10 | **Nets** (`g_nets`; Space uses one). Display 10e8:034d, change 10e8:0720 [O] |
| 0x01A | 1 | u8 | 5E5E | — | Treasure (gem) icons filled on the status bar for the current level, out of DS:0EC3[tier]. Reset per level (10e8:0741), incremented by 10e8:074a [O] |
| 0x01B | 2 | u16 | 5E5F | — | **Treasures found in the current climb** (`g_climbPoints`). Added to the total at climb end (10f0:030d) [O] |
| 0x01D | 2 | i16 | 5E7E | 4 | **Coins still to insert at the net shop** (`g_shopCoinsLeft`, 4..0; the shop is the spot with content 0xFD, 10e0:0447). Reset to 4 by 10f8:0241 [O] |
| 0x01F | 1 | u8 | 1078 | — | **Clue words earned** on the current level (0..3), drawn by 10e8:019f, incremented by 10e8:06e7 [O] |
| 0x020 | 3 | u8[3] | 5EA8..5EAA | (climb start) | `g_clueFlags[level]`, level 0..2: which attributes are named (bit 8 count, 4 size, 2 shape, 1 noun). Chosen at climb start by 1100:04b5 from OTHR 10550+tier [O] |
| 0x023 | 9 | u8[3][3] | 5EAB.. | (climb start) | `g_clueAttr[level][k]`: the selected attribute values, packed in the order count, size, shape, noun (1100:06cc, from OTHR 10555) [O] |
| 0x02C | 3 | u8[3] | 5EB4.. | (climb start) | `g_cluePlural[level]` = (key object's count > 1) (1100:06cc). Selects the word table DS:10B9 instead of DS:1079 [O] (plural forms [I]). The key's object kind `g_keyKind` (5EC9) is **not** saved: only 1100:06cc reads it, at climb start |
| 0x02F | 1 | u8 | 5F82 | 0 | Question difficulty grade 0..4, raised by performance (1150:0327) [O] |
| 0x030 | 359 | u8[0x167] | 5F83.. | all 0 (1150:0314) | Per-question history (values 0..3 counted by 1150:0057; bit 7 is a flag cleared at climb start) [O] |
| 0x197 | 72 | 9 × 8 bytes | 5F3A.. | (climb start) | Per-climb question assignments: `{u16 id (0xFFFF = none); u8 order[3] (random permutation of 0..2); u8 done[3]}` (1150:01d5) [O] |
| 0x1DF | 1 | u8 | 5CCA | **0xFE** | **Current location** (0..49). 0xFE = "between climbs / start a new climb" [O: 1118:0000] |
| 0x1E0 | 800 | — | — | — | **Unused.** Never written or read. Keeps whatever the buffer held (zeros for a new file) [O] |
| 0x500 | 150 | 50 × u8[3] | 23EC + 28·i + {0,1,2} | (0xFF at climb start) | **Spot object kind** for room i = 0..49, spot 0..2 (room table DS:23D8, stride 0x1C, field +0x14): 0..111 = picture PMAP 1400+kind and index into OTHR 10555; 0xFD = net-shop coin slot; 0xFE = locked gate; 0xFF = no spot. Reset to 0xFF and refilled by 1100:04b5 at climb start [O; values per `code_10f8-1148.md` §3.2] |
| 0x596 | 150 | 50 × u8[3] | 23EF + 28·i + {0,1,2} | (0xFF at climb start) | **Spot content** (field +0x17): 0 = nothing / already taken, 1 = (treated like 0), 2 = treasure, 3 = the level's key, 0xFD = net-shop slot, 0xFE = locked gate, 0xFF = no spot (10e0:0447) [O] |
| 0x62C | 1 | — | — | — | Unused [O] |
| 0x62D | 2 | u16 | 65CE | 0 | **Climbs completed**, i.e. castle treasures won (`g_climbsDone`, ARCHITECTURE.md §7.1 C5). Incremented once per finished climb by 10f0:0000. 0 makes the next climb start at location 9 instead of 0 (1118:0000). Also selects the celebration BNDL `0x14A + (n−1) mod 43` (10f0:0000) [O] |

"(climb start)" means 1160:1632 does not set the field. Because a new player's location byte is
0xFE, the main loop re-initialises these fields before play (1118:0000: 1150:0327, 1100:04b5, …),
so the stale values (from the previous player in memory) never matter [O/I].

C view (packed, little-endian):

```c
#pragma pack(1)
struct TmUserSlot {               /* 0x62F bytes */
    char     name[9];             /* 0x000 */
    uint8_t  tier;                /* 0x009 */
    uint32_t totalTreasures;      /* 0x00A */
    uint8_t  musicSoundOn;        /* 0x00E */
    uint8_t  voiceOn;             /* 0x00F */
    uint8_t  newPlayerMsgsOn;     /* 0x010 */
    uint32_t newPlayerMsgMask;    /* 0x011 */
    uint8_t  haveKey;             /* 0x015 */
    uint16_t coins;               /* 0x016 */
    uint16_t nets;                /* 0x018 */
    uint8_t  areaTreasures;       /* 0x01A */
    uint16_t climbTreasures;      /* 0x01B */
    int16_t  shopCoinsLeft;       /* 0x01D */
    uint8_t  clueWordsFound;      /* 0x01F */
    uint8_t  clueFlags[3];        /* 0x020 */
    uint8_t  clueAttr[3][3];      /* 0x023 */
    uint8_t  cluePlural[3];       /* 0x02C */
    uint8_t  questionGrade;       /* 0x02F */
    uint8_t  questionHist[359];   /* 0x030 */
    struct { uint16_t id; uint8_t order[3]; uint8_t done[3]; } quest[9]; /* 0x197 */
    uint8_t  location;            /* 0x1DF */
    uint8_t  unused1[800];        /* 0x1E0 */
    uint8_t  spotKind[50][3];     /* 0x500 */
    uint8_t  spotContent[50][3];  /* 0x596 */
    uint8_t  unused2;             /* 0x62C */
    uint16_t climbsCompleted;     /* 0x62D */
};
struct TmUsersFile { char magic[4] /* "USR\x1A" */; struct TmUserSlot slot[7]; }; /* 11085 bytes */
```

### 6.4 Sign-in list state [O]

* `DS:6172` far pointer to the 0x2B49-byte in-memory slot table (NewPtr). It exists only during
  the Club Sign-In and inside a save; it is freed afterwards.
* `DS:6170` (s8) player count. `DS:6171` (s8) current or selected slot. `DS:224E` (char[9]) the
  current player's name. `"________"` (8 underscores, copied from DS:2390/2399) means "no player
  signed in". All code tests `name[0] == '_'`.
* Sign-in entry `1160:00ee(again)` **[O]**: `DS:6172 = NewPtr(0x2B49)` zero-filled. If nobody is
  signed in it calls `Users_Load()` (1160:0dd6); otherwise it calls `Users_SaveCurrent()` (which
  loads first). Then it draws the screen (`again` = 0 at start-up, 1 after Start Another Game) and
  sets `DS:224E = "________"`. It loops on `1160:029c` (list and name-entry input) until `name[0] !=
  '_'`, and finally `DisposePtrDeep(DS:6172)`, `DS:6172 = NULL`. Choosing Exit on this screen
  (1160:04f6, descriptor DS:2234) `longjmp`s out with code 2 while the name is still underscores.
  The catch-path save is then a no-op, and the table is leaked (harmless).

### 6.5 Save algorithm (`Users_SaveCurrent`, 1160:0f28) [O]

```
if name[0] == '_': return                       // nobody signed in
tmp = (DS:6172 == NULL); if tmp: DS:6172 = NewPtr(0x2B49), zeroed
Users_Load()                                    // 1160:0dd6: re-read the file, recount players
s = &slot[DS:6171]
strcpy(s->name, DS:224E); copy every field in §6.3 from its DGROUP variable into s
if DS:6170 <= DS:6171: DS:6170++                // a new player was appended at index == count
if DS:6170 > 1:
    stable insertion sort of slot[0..count-1] by totalTreasures, DESCENDING
        (an element moves up past entries whose total is strictly smaller, unsigned compare)
    DS:6171 = index of the first slot whose name equals DS:224E (byte-exact compare 1160:009f)
Users_WriteFile(table, 0x2B49)                       // header + 7 slots
if tmp: DisposePtrDeep(table); DS:6172 = NULL
```

Because a save always re-reads the file first, it **merges** the current player into the file's
present contents. Other slots come from disk, not from memory.

**When saves happen** [O]:

1. **Entering the Club Sign-In** with a player signed in (1160:00ee). This happens after "Start
   Another Game" (1140:0062: confirm DS:14D6, then `longjmp(DS:5EDA, 1)` back to the main loop).
   This `longjmp` is 1000:3309, a Borland wrapper over KERNEL `Throw`; the matching `setjmp`,
   1000:32e5, wraps `Catch` and runs in 1118:0000. The sign-in is also entered at start-up, when no
   player is signed in yet, so nothing is saved then.
2. **End of every climb** (1118:0000, after the location loop ends at location 0xFE). Note the
   order: the save happens **before** 10f0:0000 adds the climb's treasures to the total, increments
   climbs-completed and shows the end screens, and before 10e8:0790 recomputes the tier. Just before
   the save, the main loop clears the clue-word count (DS:1078 = 0), calls 10e8:06c9(0) (clears
   5E59) and 10e8:0741 (clears 5E5E). On disk the record therefore has `location = 0xFE`, the old
   total, `climbTreasures` still set, and those three fields at 0. The updated values reach disk at
   the next save (next climb end, Start Another Game, or Exit).

   **Exit during the end-of-climb screens is safe [O].** 10f0:0000 first runs `climbsCompleted++`
   and 10f0:030d (total += climbTreasures, saturating). Only then does it show its screens: through
   10d8:0082, whose Esc handler is the far callback 1140:0098 (Exit) passed as an argument, and
   through the rank screen 10f0:054b, which calls 1140:0098 itself (10f0:0679, 10f0:0789). An Exit
   there therefore saves the new total and climb count. One staleness remains: the tier byte (+0x09)
   is recomputed only by 10e8:0790 **after** 10f0:0000 returns, so such an Exit saves the old tier.
   The `location == 0xFE` branch of the main loop does not recompute it. After a restore, the next
   climb therefore uses the old tier's tables, until 10e8:0790 corrects the tier at that climb's end
   **[O, confirmed in the original: M7, per REFERENCE_CAPTURES §R5.3]**: after Esc + Yes on the rank
   screen the record held total 25, climbs 1, room 0xFE and +0x09 = 0 although rank 2 needs tier 1, and
   the next climb ran with the tier-0 gem count; skipping the celebration with Enter instead lets
   `Rank_UpdateTier` run (tier 1). Decision: keep (save compatibility). Data is lost only if the
   process dies between the climb-end save and the next save.
3. **Exit** (1140:0098): confirm with descriptor DS:1533; on Yes, run cleanup (1198:00b1,
   1198:01e1), save, then `longjmp(DS:5EDA, 2)`. The main loop's catch path (1118:0000 tail) then
   saves **again**, which is harmless. Callers of 1140:0098: File menu item 2 (items 0 and 1 are See
   Rank 10f0:0487 and Start Another Game 1140:0062; see `code_10f8-1148.md`, `code_1058-1090.md`),
   Esc in the room loop (10e0:0191), the castle-wall climb (1168:0501, 1168:0b21), and the
   end-of-climb screens (point 2).

   The window Close box, WM_CLOSE, WM_QUERYENDSESSION and SC_CLOSE become a quit event (1010:0346
   sets DS:3D82 = 1; 1010:0387 sets DS:049C = 1), which `Input_GetCode` (1028:0131) reports as the
   Esc code 0x1B. The game's own Esc handling then leads to the exit prompt **[O for the mapping; I
   that every exit path ends in 1140:0098]**. WM_QUERYENDSESSION returns 0, so the game **refuses a
   Windows shutdown** and asks the player instead. A Windows session therefore cannot end while the
   game runs unless the game's own exit (and save) path runs, or the game is killed.

   "Start Another Game" (1140:0062..008b) is not a Ghidra function; it was read from the bytes and
   relocations: `if 10c0:0ae5(DS:14D6) == 1 { DS:13CE = 0xFFFF; 1128:00EF(); longjmp(DS:5EDA, 1) }`.
   The save then happens on entry to the sign-in (point 1).
4. **Remove a player** (§6.7) rewrites the file at once.

### 6.6 Restore on sign-in (1160:137d; not in the shared export, see scratch `dec1.txt`) [O]

When an existing player is chosen (Enter on their row, 1160:06ee → 0725; or a mouse click,
1160:0645 → 0653), `DS:6171` = the row and every field in §6.3 is copied back from
`slot[DS:6171]` to its DGROUP variable, including the name. The one exception is
**Music and Sound**:

```
if slot.musicSoundOn != DS:0B96 and DS:0B96 != 0:
    ToggleMusicSound()      // 1128:012f: sets 0B96 = 0 and stops wave and MIDI
    DS:0B96 = slot.musicSoundOn
```

So a player saved with sound **off** turns sound off. A player saved with sound **on** does
**not** turn it back on if it is currently off. A **new** player always turns it on (1160:1632
calls the toggle if 0B96 == 0). Voice (0B97) and new-player-message state are restored
unconditionally.

On return, the main loop resumes the saved climb when `location != 0xFE`. Per-climb data
(clues, question assignments, spot contents, counters) comes from the record. With
`location == 0xFE` a new climb is generated (1118:0000).

### 6.7 Roster editing rules (sign-in screen, segment 1160) [O]

* **Max 7 players.** Choosing "Start New Player" (row index == count) or typing a printable key on
  the list when count ≥ 7 shows descriptor DS:2200 ("The list is full…", text DS:2271).
* **Name entry** (1160:02e0 / 0957): the buffer DS:224E starts as 8 underscores, cursor at the start.
  Accepted keys are bytes < 0xFF with Borland ctype `(_ctype[c+1] & 0xDE) != 0` (digit, upper,
  lower, hex, punctuation, blank) except `'_'`. Space is refused as the first character. Backspace
  (8) moves back and puts `'_'` back. At most 8 characters. The display routine (1160:0ab0)
  **uppercases name[0]** in place. Other characters are stored as typed. Enter (0x0D) or the OK
  button (code 0x4B0) requires name[0] ≠ '_'. Trailing spaces become `'_'`, and a NUL is written
  after the last kept character.
* **Duplicates:** the new name is compared byte-exactly (case-sensitive) with slots 0..count−1. A
  match shows descriptor DS:2129 ("<name> is already on the list", template DS:2106). Otherwise
  1160:1632 initialises the new player (defaults in §6.3; DS:6176 = 1), with slot index
  `DS:6171 = count`. The record is **not written** until the first save (§6.5).
* **Esc** during name entry cancels it (the name is reset to underscores). If there are no players
  at all, Esc offers to exit the program (1160:04f6, descriptor DS:2234).
* **Remove player** (input code 0x12, i.e. Ctrl+R as a character **[I]**, or the Remove button
  code 0x4A9; 1160:06a1 → 089f): confirm
  with descriptor DS:21E6 (title DS:225A, text DS:2143 naming the player). On Yes:
  `for k = idx+1..6: slot[k−1] = slot[k]`; `slot[6].name[0] = 0` (table offset 0x251A);
  `if (count) count--`. The file is then written. The bytes of slot 6 other than name[0] are left as
  a stale copy. Note that the file is written even if the user says No. Removing the player who was
  signed in before the sign-in screen opened is safe: 1160:00ee has already saved them and reset
  DS:224E to underscores, so no later save can re-add them.
* List-screen input map (1160:0595; key table 1160:07B8, 31 codes; handlers at 1160:07F6) [O]:
  0x0D Enter / 0x4AC OK → select the highlighted player (restore, §6.6), or start a new player on
  the "Start New Player" row. 0x4AE click on a row → select that row
  (`row = (mouseY DS:06F8 − 0x39) / 25`). 0x12 / 0x4A9 → remove. 0x126 Up / 0x4AB → previous row,
  wrapping to the "Start New Player" row. 0x128 Down / 0x4AA → next row, wrapping to 0.
  0x4AD → new player. 0x1B Esc / 0x4AF → exit-program prompt (1160:04f6). Mouse codes carry
  flag bits set by 1028:0131 (0x8000 = event kind 3, 0x4000 = kind 5, 0x2000 = kind 6). The
  0x24A9..0x24AF variants run the same handlers as the plain codes. The 0x44A9..0x44AE and
  0x84A9..0x84AE variants only move the highlight: 0x?4AD → the "Start New Player" row, 0x?4AE →
  the row under the pointer (clamped to the list). The others are no-ops (1160:0695..069E).
  Any other printable key (not space or '_') starts a new player with that character, uppercased.
  Row height = DS:2257 = 25 px, list origin y = 0x39.

### 6.8 Rank and stars (derived, not stored) [O]

`Rank(total)` (10e8:07a6) = `(number of thresholds T[k] ≤ total) − 1`, using
`T = {0, 5, 25, 70, 115, 170, 230, 300}` (8 × u32 at DS:0EA0). The result is 0..7. The sign-in list
(1160:0b1b) shows the name at x = 0x28, then rank 0 → "Trainee" (DS:23C4), ranks 1..6 → that many
star glyphs (string DS:23CC = `*`, at x = 0xC3 + 8k), rank 7 → "Champion" (DS:23CE), then the total
as a number (1160:0cdb). Tier (record +0x09) = rank / 2.

### 6.9 Edge cases worth reproducing or fixing [O/I]

* If `users.dat` is deleted while the game runs, the next save reloads an empty table but writes
  the current player at their old index `DS:6171`. The count becomes 1, and the player may sit
  after an empty slot 0, where the next load does not see them **[I from the code path]**.
* A file of the wrong length (short read) is treated as empty, and the next save overwrites it.
  Extra bytes past 11 085 are ignored.
* The header is never validated, so any 11 085-byte file loads.

### 6.10 DOS USERS.DAT (curiosity only)

`TM-DOS\USERS.DAT` is 3 243 bytes. It starts `00 A1 05`, and one record with the name "Rascal"
begins at offset 3 (non-zero data up to offset 0x168). It has no "USR" magic. The size is consistent
with a 3-byte header + 8 × 405-byte records **[I, unverified]**. The format is unrelated to, and
incompatible with, the Windows file.

---

## 7. CD path logic and where files are expected

**`CDDrive=` is never read by TMTWINCD.EXE** [O: the string does not occur in the EXE; the EXE
imports nothing for drive enumeration such as GetDriveType]. Only SETUP.INS writes it
(`[TMTWIN] CDDrive` near SETUP.INS string offset 0x2247, next to `\TMTWINCD\`). The EXE finds its
data relative to **its own location** and the Windows search path:

| File | How it is located | Expected place in the original install |
|---|---|---|
| `TMTWINCD.EXE` | started from the Program Manager item | On the CD: `<CD>:\TMTWINCD\` **[I]**: TLC.Z holds only the icons (SETUP.PKG lists TMTWINCD.ICO, TLS16.ICO, TLS256.ICO), so the EXE is not copied |
| `TMT.DLL`, `TMT16.DLL` / `TMT256.DLL`, `TM0..TM8SOUND.DLL` | `LoadLibrary("<name>")` | Same directory as the EXE (Win16 search step 4) |
| `SOUNDS\B*.MID`, `SOUNDS\E*.MID` | `<EXE dir>\SOUNDS\` + 'B'/'E' + name, opened through MCI (1090:023d, 1090:02e9) | `<CD>:\TMTWINCD\SOUNDS\` |
| `WING.DLL` (WinG mode only) | `LoadLibrary("WING.DLL")` | Windows system directory (installed from WING.Z) |
| `midimap.cfg` | `GetWindowsDirectory() + "\system\midimap.cfg"` | Windows 3.1 MIDI Mapper setup |
| `TMTWIN.INI` | Hard-coded `C:\TLCWIN\TMTWINCD\TMTWIN.INI` | Created by SETUP. Its directory must exist for writes to succeed |
| `users.dat` | Hard-coded `c:\tlcwin\tmtwincd\users.dat` | Directories created on save |

There is no other file I/O: `_lopen` is used only for midimap.cfg, and the Borland `open` only for
users.dat. A modern port should let the user point at a directory holding `TMTWINCD\` (DLLs and
`SOUNDS\`) and keep its own writable location for settings and saves. Optionally, it can import an
existing `users.dat`.

---

## 8. Which settings persist where (summary)

| Setting | Stored in | Notes |
|---|---|---|
| Show the "not 640×480 256-colour" warning | INI `CheckDisplay` | Checkbox in the dialog |
| Show the "no MIDI/WAV driver" warning | INI `CheckSound` | Checkbox in the dialog |
| WinG vs GDI | INI `UseWinG` (Win 3.1), forced WinG on Win95 | Command line `3`/`4` overrides and saves |
| Windows-4.0 threshold | INI `Win40VersionNum` | Rewritten every start |
| Music and Sound on/off | USERS.DAT slot +0x0E | Per player. Restore never turns it on (§6.6). Key event code 0x13 (Ctrl+S **[I]**; 1028:004f) toggles Music-and-Sound **and** Voice together: both on if both were off, otherwise both off (1128:0151) |
| Voice on/off | USERS.DAT slot +0x0F | Per player (Options menu, 1140:00f7) |
| New Player Messages | USERS.DAT slot +0x10, plus the shown-message mask at +0x11 | Per player (Options menu, 1140:0015) |
| Game progress, score, rank | USERS.DAT | §6.3 |

---

## 9. Key DGROUP variables (this topic)

| DS: | Type | Meaning |
|---|---|---|
| 0074 | u16 | g_useWinG (initial 1) |
| 007C | HDC | GetDC(main window) |
| 0088 | HINSTANCE | WING.DLL |
| 0498 | u16 | init complete |
| 04A4 | u16 | g_isWin40 |
| 0872, 09DE..09EE, 09F0 | HINSTANCE | DLL handles (§4) |
| 086C | u16 | g_paletteDevice (initial 1). 0 = 16-colour mode |
| 086E / 0870 | i16 | game-area origin in the client area |
| 09D4 | u16 | resource load in progress (menu commands blocked) |
| 09DC | u16 | system dialog active |
| 09F2 | u16 | wave device present |
| 0A4A / 0A50 / 0A52 | u16 | MIDI available / MCI sequencer id / MCI song id |
| 3CDA / 3CDC | HANDLE | hInstance / main hwnd |
| 3CDE | char[0x81] | INI path |
| 3D88 | u32 | scratch high-water mark |
| 3D8C | far ptr | g_resScratch |
| 073A | HGLOBAL | resource held by Res_LockInPlace |
| 4AF4 | char[] | `<EXE dir>\SOUNDS\` |
| 4B76 | u16 | 1 = extended (E*.MID), 0 = base (B*.MID) |
| 224E | char[9] | current player name ("________" = none) |
| 6170 / 6171 | s8 | player count / current slot |
| 6172 | far ptr | slot table (sign-in only) |
| 6176 | u8 | set to 1 by 1160:1632 when a new player is created |
| 5EDA | Catch buffer | `setjmp` target in 1118:0000: `longjmp` value 1 = Start Another Game, 2 = Exit |
| 049C / 3D82 | i16 | quit requested (WM_CLOSE / WM_QUERYENDSESSION) / pending event type (1 = quit, from SC_CLOSE) |
| 6694 / 6696 | int / far ptr | argc / argv |

---

## Open questions

1. **Win16 name-table resolution.** The claim that KERNEL (Win 3.1 and Win95) resolves
   `FindResource(…, "PMAP")` through RT_NAMETABLE rests on the observed table format and on Wine's
   implementation. It has not been checked against Microsoft's KERNEL. It does not affect a port.
2. **Program Manager item contents.** The InstallShield script (SETUP.INS) was only string-scanned.
   The exact command line and working directory of the installed icon (presumably
   `<CDDrive>\TMTWINCD\TMTWINCD.EXE`, working directory `C:\TLCWIN\TMTWINCD`) were not decoded.
   Whether SETUP also creates `C:\TLCWIN\TMTWINCD\users.dat` is unknown.
3. **Spot content value 1** (slot +0x596 bytes) has no distinct behaviour in any code path found
   by the gameplay spec. If real save files contain it, a port must keep it unchanged.
4. **The 800-byte gap at slot +0x1E0..+0x4FF** is never touched. It may be space left over from the
   Mac save format. Its contents in real save files are whatever was in the buffer (zeros for a fresh
   file).
5. **midimap.cfg layout.** The offsets used (`u16` at 6, 0x36-byte records starting at 0x12, 16-byte
   name) are from the code. They were not checked against a real Windows 3.1 midimap.cfg. On Win95
   the file may be missing, in which case MidiInit fails and the TooBadNoSound dialog appears.
6. **DOS USERS.DAT record size** (405?) is unverified. No reference to the DOS `users.dat` string
   (DOS DGROUP 0x053C) was found in the decoded DOS disassembly, so the file was only examined as a
   curiosity.
7. **Crash after a missing DLL** (§4) is inferred. The WM_QUIT being swallowed is observed, but
   which resource request comes first, and whether Windows shows a GP fault or the game limps on,
   was not run.
8. ~~**Stale tier after an Exit from the end-of-climb screens**~~ **Answered (per
   REFERENCE_CAPTURES §R5.3): confirmed stale; decision: keep** (§6.5 point 2).

Resolved in this revision (formerly open): end-of-climb exit ordering (§6.5 point 2, now observed
safe apart from the stale tier byte) and the LoadLibrary-failure WM_QUIT behaviour (§4, never acted
on).
