# DGROUP map: TMTWINCD.EXE data segment (selector 11b8)

Scope: the whole automatic data segment (DGROUP) of the Windows CD edition, *Super Solvers Treasure
Mountain!* `TMTWINCD.EXE` v1.01 (Borland C++ 1993). It covers every initialised table, string block,
pointer table and struct array, and the zero-initialised (BSS) variables. For each one it gives the
offset, size, layout, meaning and the code that uses it.

Companion files:

| File | Content |
|---|---|
| `tools/assets/dgroup.py` | Finds DGROUP through the NE header, applies the relocations, holds the full region map (669 regions), decodes all tables to JSON and writes the names TSV. |
| `extracted/previews/dgroup.json` | Output of the tool: source info, 198 relocations, 669 regions (with a generic value and cross references), semantic `tables`, and all 284 strings with offsets. |
| `re/names/dgroup.tsv` | One line per region: `11b8:offset`, name, type, summary + size + referencing functions. |

Conventions. **[O]** means observed in bytes, decompiled code or tool output. **[I]** means inferred.
An offset `XXXX` always means `11b8:XXXX`, which is file offset `0x23c00 + XXXX` in `TMTWINCD.EXE`.
Code addresses are Ghidra `SSSS:OOOO`. Most behaviour of the code that uses these tables is specified in
the `code_*.md` docs. This document gives the data layout and the indexing rules an engine needs.

---

## 1. Segment facts [O]

| Item | Value | Source |
|---|---|---|
| NE segment | 56 of 56 (auto-data segment number in NE header +0x0E) | NE header |
| Ghidra selector | `11b8` (= 0x1000 + (56−1)·8) | |
| File offset / length | `0x23c00`, 0x66a2 = 26274 bytes (sector shift 9) | segment table |
| Flags / minalloc | 0x0d51 = DATA (0x0001) + MOVEABLE (0x0010) + PRELOAD (0x0040) + RELOCINFO (0x0100) + DPL 3 (0x0c00); minalloc 0x66a2 | segment table |
| Initialised `_DATA` | `0000..387f` (14464 bytes) | bytes: everything from `3880` on is zero |
| `_BSS` | `3880..66a1` (11810 bytes) | zero in the file; C0W entry `1000:0000` clears it again |
| Local heap / stack | 0x1000 / 0x1400 bytes, added after `66a2` by the loader | NE header +0x10 / +0x12 |
| Relocations | 198, all type 3 (16:16 far pointer), internal references | segment relocation table |

Run-time layout [I, standard Win16 / Borland]: one segment of 0x66a2 + 0x1000 + 0x1400 = 0x8aa2 bytes.
SS == DS (Borland's EH code addresses `SS:0010..001b`, the same bytes as `DS:0010..001b`).

### 1.1 Far pointers stored in DGROUP

A far pointer is `u16 offset; u16 selector`, little-endian. In the file each one holds the fix-up
chain placeholder `FF FF 00 00` (offset 0xFFFF = end of chain). The real target is only in the
relocation record, so a reader **must** apply the relocation table. `dgroup.py` does this.
- 171 relocations point into DGROUP itself (string and array pointers).
- 27 point at code: `1000` (8), `1140` (6), `1178` (6), `1180` (4), `11a0` (2), `10f0` (1).
  The complete list is in §6.

A NULL far pointer is stored as `00 00 00 00` with no relocation.

### 1.2 Traps when reading the Ghidra export

* **`s_flowers_11b8_11b8`.** The DGROUP selector value `0x11b8` is also the offset of the string
  `"flowers"` (`11b8:11b8`, a plural noun). Ghidra therefore labels every `PUSH DS` /
  segment-immediate as `s_flowers_11b8_11b8`. In decompiled code such as `FUN_1038_08d4(900, 0x3d90,
  s_flowers_11b8_11b8)`, it means "segment = DGROUP". It is not a reference to the string.
* **1-based tables.** Some tables are indexed with a 1-based step (`t[step−1]`). The compiler folds
  the −1 into the displacement, so the instruction reads `[2·step + base]` with `base` = first
  element − element size. That base lies inside the previous table, so Ghidra and simple xref scans
  attribute the access to the wrong variable. Affected tables (code base → first real element):
  `141a→141c`, `1422→1424` (title narration), `2a3e→2a40`, `2a4c→2a4e`, `2a5a→2a5c`, `2a68→2a6a`,
  `2a74→2a76`, `2a80→2a82`, `2a8c→2a8e` (climb tables). Other 1-based tables keep a dummy element 0
  (e.g. the noun pointer tables, `0ba0..0bd7`).
* **Immediates.** Values such as `PUSH 0x1c84` (= 7300 decimal, a sound id) look like DGROUP
  offsets. `dgroup.py --asm` only accepts an immediate as a reference if it equals a region start or
  falls inside a string block, and if `DS` is pushed or loaded next to it.

### 1.3 Text encoding [O]

Strings are NUL-terminated 8-bit text in the game font (FFNT 999, Mac Roman-like; codes 0x82, 0x83,
0x86, 0x8b, … are key icons, see `fmt_bndl_ffnt.md` §3.2). The text renderer `10b0` interprets
`0x0d`/`0x0a` (new line), `0x09` (tab), `0xFE` (carriage return without line feed) and escapes
`FF 01 n FF` (substitution), `FF 02 FF` (underline), `FF 03 c FF` (colour c−1), `FF 04 … FF` (tabs),
`FF 05 FF` (toggle centring). See `code_1098-10d8.md` §7.2. In the JSON, bytes outside 0x20..0x7e
and the backslash are written as `\xNN`, so every string can be round-tripped exactly.

---

## 2. The tool

```
python -P tools/assets/dgroup.py                 # -> extracted/previews/dgroup.json (+ xrefs from the Ghidra export if present)
python -P tools/assets/dgroup.py --check         # validate the region map: contiguous, sizes match types, BSS all zero, relocations land in pointer fields
python -P tools/assets/dgroup.py --names-out re/names/dgroup.tsv
options: --exe PATH  --out PATH  --asm disassembly.asm | --no-asm  --dump-bin dgroup.bin
```

JSON top level:

| Key | Content |
|---|---|
| `source` | Segment number, selector, file offset, length, minalloc, flags, heap/stack, BSS start, relocation count |
| `relocations[]` | `{at, target}`. Target is `11b8:xxxx` or a code address. |
| `regions[]` | `{off, end, size, name, type, owner, summary, bss, value, refs}`. `value` = generic decode (integers, arrays, resolved far pointers, strings, or `{hex}`/`{zero}`) for initialised regions. `refs` = functions found in the disassembly plus `ptr@xxxx` for DGROUP pointers that point at the region. |
| `tables.rooms[50]` | §4.16 |
| `tables.clue_words` | §4.10, with voice offsets |
| `tables.ranks`, `tables.music`, `tables.menus`, `tables.dialogs`, `tables.hints`, `tables.quiz`, `tables.player_anim`, `tables.climb`, `tables.elves` | §4 sections |
| `tables.help`, `bndl_pager`, `cutscene`, `hall`, `cursor_ids`, `palette`, `users`, `hud_templates`, `dialog_strings`, `init_app_strings`, `rtl` | misc |
| `strings[]` | Every string in a string-typed region: `{off, text, region}` |

Fields marked `_source` / `_note` (music zone switch, help topic ids, hint/quiz/clue voice formulas)
are **code** constants copied into the JSON for convenience, with their code address. They are not
read from DGROUP.

---

## 3. Block map

Borland emits one code segment per module and lays out each module's initialised data in link order.
DGROUP is therefore a sequence of per-module blocks [O: the owners come from the xrefs; I: the
"module" grouping]. "Owner" = the code segment(s) that use the block.

### 3.1 Initialised data (`0000..387f`)

| Range | Owner | Content | § |
|---|---|---|---|
| 0000–002f | KERNEL, C0W/EH | Win16 instance header; Borland EH per-instance words | 4.1 |
| 0030–0073 | 1000 (C0W) | RTL start-up variables, copyright string | 4.1 |
| 0074–048d | 1008 | WinG state, LOGPALETTE template | 4.2 |
| 048e–04a5 | 1008/1010/1018 | "WING.DLL", window/app flags | 4.2 |
| 04a6–06e1 | 1018 | InitApp string block (class, title, INI, dialog templates, DLL names) | 4.3 |
| 06e2–06fb | 1010/1020/1028 | input state (cursor, mouse, arrow keys) | 4.3 |
| 06fc–07d3 | 1030/1038 | memory / resource-manager strings and state | 4.4 |
| 07d4–085f | 1048 | palette state, EGA colour table | 4.5 |
| 0860–09ad | 1058/1060/1068/1010 | clip rect, dirty-rect state, shadow remap, RUND tag, font | 4.5 |
| 09ae–09dd | 1070/1078/1080 | dissolve tables, menu state, dialog flag | 4.5 |
| 09de–09f9 | 1010/1018/1088 | DLL handles, wave state | 4.6 |
| 09fa–0b1d | 1090 | MIDI music tables and strings | 4.6 |
| 0b1e–0b95 | 10b8/10c0/10c8 | animation counters, dialog button strings, static pager | 4.7 |
| 0b96–0b9f | game | option flags and per-frame player flags | 4.8 |
| 0ba0–0e9f | 10e0 | **player animation tables** | 4.8 |
| 0ea0–0f6f | 10e8 | **rank thresholds**, HUD gem positions, HUD text templates | 4.9 |
| 0f70–1063 | 10f0 | celebration / rank-screen pointers and **rank names** | 4.9 |
| 1064–1077 | 10f8/1100 | net shop, hiding-place pictures | 4.10 |
| 1078–1287 | 1100 | **clue word tables** (nouns, plurals, counts, sizes, shapes) | 4.10 |
| 1288–1299 | 1110/1118 | coin pile, elves per tier, cursor ids | 4.11 |
| 129a–13c5 | 1120 | **menu bar definition** | 4.12 |
| 13c6–142b | 1128/1130/1138 | music controller, **hint table**, title narration | 4.13 |
| 142c–198d | 1140 | **menu-command dialogs** (DlgDesc + texts) | 4.14 |
| 198e–2047 | 1150 | **quiz narration tables** and result texts | 4.15 |
| 2048–2105 | 1158 | users.dat paths, write-error dialog | 4.15 |
| 2106–23d7 | 1160 | sign-in dialogs and strings | 4.15 |
| 23d8–294f | shared | **room table** `RoomDef[50]` | 4.16 |
| 2950–2ab1 | 1168 | **castle-wall climb tables** | 4.17 |
| 2ab2–2ca9 | 1170/1178 | sfx pointer, **elf behaviour scripts** | 4.18 |
| 2caa–2d5f | 1180 | help pager | 4.19 |
| 2d60–2de1 | 1188/1190/1198 | cut-scene ids, treasure-hall pedestals, room scene | 4.19 |
| 2de2–387f | 1000/11a0/11a8 | Borland RTL data | 4.20 |

### 3.2 BSS (`3880..66a1`)

| Range | Owner | Content | § |
|---|---|---|---|
| 3880–3cd9 | 1008 | WinG rect, BITMAPINFO, colour table, WinG entry points | 5 |
| 3cda–3d8f | 1010–1038 | hInstance/hwnd, INI path, cursors, pending event, resource scratch | 5 |
| 3d90–418f | 1118 | palette RGB buffer (RRGB 900) | 5 |
| 4190–42a1 | 1048/1050/1058 | colour key, logical→physical map, DCs, selectors | 5 |
| 42a2–4aa1 | 1058 | dirty rectangles [256] | 5 |
| 4aa2–4b79 | 1068/1088/1090 | font state, wave header, sounds dir, MIDI flags | 5 |
| 4b7a–4f61 | 1098 | hot spots [50] ×2 | 5 |
| 4f62–5c3d | 10a8/10b8 | timers, animation engine (channels, frames, sprites, rects) | 5 |
| 5c3e–5cc9 | 10c0/10c8/10d0 | dialog layout, pager arrays | 5 |
| 5cca–5e53 | 10e0 | player state, frame sizes | 5 |
| 5e54–5f12 | 10e8–1130 | inventory/score, celebration, clues, coin pile, restart jmp_buf, options | 5 |
| 5f13–6149 | 1138/1150 | intro animation, quiz state | 5 |
| 615e–6227 | 1160/1168 | sign-in, climb state | 5 |
| 6228–65ff | 1178/1190 | elves, treasure hall | 5 |
| 6600–66a1 | 1000/11a0 | RTL (atexit table, EH scratch, argv, heap descriptor) | 5 |

---

## 4. Initialised data, block by block

Notation: `name` @offset, size, type: meaning. "init" = value in the EXE image. Sizes and offsets
are hex.

### 4.1 Start-up / runtime header (0000–0073) [O]

| Off | Size | Name | Type | Meaning |
|---|---|---|---|---|
| 0000 | 10 | win16_instanceHeader | bytes | Reserved by Windows (pLocalHeap 06, pAtomTable 08, pStackTop 0A, pStackMin 0C, pStackBottom 0E) [I, Win16 convention]. Zero in the file. |
| 0010 | 4 | eh_CtorCount | u32 | Borland EH object counter (accessed as SS:0010) |
| 0014 | 2 | eh_FrameChainHead | u16 | EH frame-chain head, set to 0xFFFF by `1000:001b` |
| 0016 | 4 | eh_VarsPtr | far | → `31b2` (set by `1000:2e99`) |
| 001a | 2 | eh_Magic | u16 | 0xFEED marker |
| 001c | 14 | rtl_reserved001c | bytes | zero, no reference found |
| 0030–0043 | 2 each | errno, _protected, _cpuType, _8087, _pszCmdLineSeg, _hInstance, _hPrevInstance, _pszCmdLineOff, _cmdShow, _osversion | | C0W start-up variables (InitTask results) |
| 0044 | 1 | rtl_unk0044 | u8 | zero, no reference found |
| 0045 | 2 | _WinAllocFlag | u16 | extra GlobalAlloc flags for malloc (init 0) |
| 0047 | 2 | rtl_unk0047 | u16 | init 1, no reference found |
| 0049 | 2b | s_BorlandCopyright | cstr | RTL copyright signature |

### 4.2 Graphics / WinG / window flags (0074–04a5) [O]

| Off | Size | Name | Init | Meaning |
|---|---|---|---|---|
| 0074 | 2 | g_useWinG | 1 | 1 = WinG back buffer, 0 = GDI DDB. Overridden by INI `UseWinG`. |
| 0076/0078 | 2+2 | g_bufWidth/Height | 0 | back buffer 512 × 1055 at run time (code draws layers at y offsets 0, 0x180 and 0x300; the third layer is partial) |
| 007a | 2 | unused007a | 0 | no reference |
| 007c, 007e | 2+2 | g_hdcWindow, g_hdcWinG | | HDCs (WinG path) |
| 0080 | 4 | g_wingBits | | far pointer to the WinG DIB bits |
| 0084 | 2 | g_dibOrientation | −1 | top-down DIB (biHeight = −h) |
| 0086, 0088 | 2+2 | g_hbmWinGOld, g_hWinGLib | | |
| 008a | 404 | g_logPalTemplate | `00 03 00 01` + zeros | LOGPALETTE: `u16 palVersion=0x300, u16 palNumEntries=256, PALETTEENTRY[256]` (4 bytes each, all 0) |
| 048e | 9 | s_WING_DLL | | "WING.DLL" |
| 0498 | 2 | g_initDone | 0 | enables WM_PAINT presenting |
| 049a | 2 | g_appActive | 1 | 0 while the app is inactive or in a menu/dialog; the event pump spins while 0 |
| 049c | 2 | g_quitRequested | 0 | |
| 049e | 2 | g_holdMainLoop | 1 | cleared by WM_CREATE |
| 04a0 | 2 | g_unused04a0 | 1 | cleared by WM_CREATE, never read |
| 04a2 | 2 | g_noResumeOnMenuExit | 0 | never written |
| 04a4 | 2 | g_isWin40 | 0 | result of `IsWindows40` (1018:07ff) |

### 4.3 InitApp strings and input state (04a6–06fb) [O]

`strs_initApp` 04a6..06e1 (0x23c bytes, 61 strings, used by segment 1018). Each string is listed by
offset in `dgroup.json`. Purposes:

| Offsets | Purpose |
|---|---|
| 04a6, 04b1 | window class name (RegisterClass, CreateWindow) |
| 04bc | window title |
| 04cf, 04e3, 04ee | INI directory `C:\TLCWIN\TMTWINCD\`, file `TMTWIN.INI`, fallback `"?"` |
| 04f0, 0509, 0516, 0525/0532, 055b, 05a4, 05d9 | EXE dialog template names (not enough memory, not Windows 3.1, not 386 enhanced, too much colour, no sound, not optimum display mode, incompatible video) |
| 053f/0546/0551…, 0569/0570…, 0586/058d…, 05ba/05c1…, 067d/0684/0694, 0698/069f, 06af/06b6…, 06c8/06cf… | INI section `TMTWIN`, keys `CheckSound`, `CheckDisplay`, `Win40VersionNum` (default "858"), `UseWinG`, and their "TRUE"/"FALSE" default/compare values |
| 05eb, 05f6, 0600, 0608..0670 | DLL names TMT256, TMT16, TMT, TM0SOUND..TM8SOUND (stride 13) |

Input state:

| Off | Size | Name | Init | Meaning |
|---|---|---|---|---|
| 06e2 | 1 | g_cursorForced | 0 | force the arrow cursor |
| 06e3 | 2 | g_mouseButtonDown | 0 | left button held |
| 06e5 | 4 | g_mouseMoveHook | NULL | callback after WM_MOUSEMOVE |
| 06e9 | 2 | g_curCursorIndex | −1 | |
| 06ec | 2 | g_pendingInputCode | 0 | one-slot input look-ahead |
| 06ee/06f0/06f2/06f4 | 2 each | g_keyUpHeld/Down/Left/RightHeld | 0 | arrow keys held |
| 06f6/06f8 | 2 each | g_mouseX/Y | | mouse in 512×384 game coordinates |
| 06fa | 2 | unused06fa | 0 | no reference |

### 4.4 Memory and resource manager (06fc–07d3) [O]

* `strs_1030` 06fc..0735: out-of-memory message box text (06fc) and caption (0713), and "BNDL" at
  072c and 0731 (`1030:0000/0082`).
* 0736 `g_resCachedTree` (far, only used by the unreferenced `1038:0000`), 073a `g_resLockedHandle` (u16).
* `strs_1038` 073c..07d3: the Mac resource type names "BNDL" 0741, "RRGB" 0746, "FFNT" 074b,
  "PMAP" 0750, "SSND" 0755, "OTHR" 075a returned by `Res_TypeForId`; "BNDL" compare copies at 073c,
  078f, 0794, 0799, 079e; out-of-memory text/caption pairs at 075f/0776 and 07a3/07ba.

### 4.5 Palette, blitter, font, dissolve, menu state (07d4–09dd) [O]

| Off | Size | Name | Init / layout | Meaning |
|---|---|---|---|---|
| 07d4 | 2 | g_hPalette | 0 | game HPALETTE |
| 07d6 | 2 | g_curPaletteId | −1 | last RRGB id loaded |
| 07d8 | 60 | g_egaRGB | `u16 r,g,b` ×16 | the 16 standard colours indexed by colour number 0..15: components 0/85/170/255, except colour 6 = (170,85,0) brown and colour 7 = (192,192,192) light grey |
| 0838 | 20 | g_vga16Index | u16[16] | 16-colour pixel value for each EGA colour |
| 0858 | 2 | g_paletteFirstLoad | 1 | `Pal_SetFromRGB` acts only once |
| 085a | 5 | s_RRGB | "RRGB" | |
| 0860 | 2 | g_gfxConst4 | 4 | never written; the fade stubs run only if it is 3 or 4 |
| 0862 | 2 | g_dirtyEnabled | 0 | record dirty rects while 1 |
| 0864/0866/0868/086a | 2 each | g_clipTop/Bottom/Left/Right | 0, 0x180, 0, 0x200 | inclusive clip rectangle for sprite blits |
| 086c | 2 | g_paletteDevice | 1 | 1 = 256-colour (TMT256.DLL), 0 = 16-colour (TMT16.DLL) |
| 086e/0870 | 2+2 | g_viewOriginX/Y | | client position of the 512×384 game area (x multiple of 4) |
| 0872 | 2 | g_hGfxLib | | handle of TMT256/TMT16 |
| 0874 | 2 | g_dirtyCount | 0 | entries in `g_dirtyRects` (42a2) |
| 0876 | 100 | g_shadowTable | u8[256] | colour remap table for the shadow / masked-remap blits (`1058:0f7e`). Stored as logical indices; converted to physical once by `1048:010a`. |
| 0976 | 21 | g_invertSpecTemplate | `10` + 32×`00` | remap spec template `{u8 n; {u8 new, old}[n]}` copied by `Gfx_InvertRect16` |
| 0997 | 5 | s_RUND | "RUND" | PMAP compression tag (`fmt_pmap.md`) |
| 099c | 4 | g_rowPitch | 512 | offscreen row width for the glyph blitters (1060) |
| 09a0..09a6 | 2 each | g_tm* | 0 | WM_CREATE text metrics (unused) |
| 09a8 | 5 | s_FFNT | "FFNT" | |
| 09ae | 10 | g_dissolveColOrder | `0b 0d 05 0e 07 01 0f 02 00 03 08 04 09 0c 06 0a` | Screen_DissolveCopy (1070): column phase of pass p |
| 09be | 10 | g_dissolveRowShift | `0d 0b 06 0a 03 01 0f 00 0c 05 07 02 04 0e 09 08` | per-row (y mod 16) column shift |
| 09ce | 4 | g_menuDef | NULL | current `MenuBarDef far*` |
| 09d2 | 2 | g_menusEnabled | 0 | |
| 09d4 | 2 | g_inResourceLoad | 0 | set while 1038 pumps messages during loads |
| 09d6/09d9 | 3+3 | s_fmt_s1/2 | "%s" | |
| 09dc | 2 | g_inDialog | 0 | |

The full arrays are in `tables.palette` of the JSON. The dissolve algorithm is in `code_1058-1090.md`.

### 4.6 DLL handles, wave, MIDI music (09de–0b1d) [O]

| Off | Size | Name | Meaning |
|---|---|---|---|
| 09de | 12 | g_hSoundLib | u16[9]: handles of TM0SOUND..TM8SOUND.DLL |
| 09f0 | 2 | g_hTmtLib | TMT.DLL |
| 09f2 | 2 | g_waveAvail | |
| 09f4 | 4 | g_waveFmtPtr | → `4ac2` while a wave device is open |
| 09f8 | 2 | g_waveState | 1 playing, −1 idle (init −1) |
| **09fa** | 14 | **g_musicIds** | i16[10] = 9000..9009 |
| **0a0e** | 14 | **g_musicLoopPos** | i16[10] = 0 except index 6 = −1 (play once) |
| **0a22** | 28 | **g_musicFiles** | far[10] → strings 0a56, 0a62, 0a6f, 0a7b, 0a87, 0a93, 0a9f, 0aaa, 0ab5, 0ac1 |
| 0a4a | 2 | g_midiOk | |
| 0a4c | 2 | g_curMusic | init −1 |
| 0a4e | 2 | g_midiPaused | |
| 0a50/0a52 | 2+2 | g_seqDev / g_trackDev | MCI device ids |
| 0a54 | 2 | g_midiSavedPos | |
| 0a56 | 76 | strs_musicFiles | 10 names, **each with a leading placeholder byte** (a space) |
| 0acc | 14 | s_midimapCfg | `\system\midimap.cfg` |
| 0ae0 | 2c | strs_fmSynthNames | 6 driver-name substrings (ADLIB family, MVI, BAS, THUNDER BOARD) |
| 0b0c | a | s_sequencer | MCI device type |
| 0b16 | 8 | s_SOUNDS | sub-directory `SOUNDS\` |

**MIDI file name table** (index = position of the id in `g_musicIds`):

| idx | id | file template | loop |
|---|---|---|---|
| 0 | 9000 | ` castle.mid` | 0 |
| 1 | 9001 | ` clubhse.mid` | 0 |
| 2 | 9002 | ` finale.mid` | 0 |
| 3 | 9003 | ` level1.mid` | 0 |
| 4 | 9004 | ` level2.mid` | 0 |
| 5 | 9005 | ` level3.mid` | 0 |
| 6 | 9006 | ` open1.mid` | −1 (once) |
| 7 | 9007 | ` open2.mid` | 0 |
| 8 | 9008 | ` throne.mid` | 0 |
| 9 | 9009 | ` title.mid` | 0 |

`Midi_OpenTrack` (1090:02e9) overwrites byte 0 **in place** with `'B'` (base / FM set) or `'E'`
(extended / GM set). It then appends the name to `g_soundsDir` (`<exe dir>\SOUNDS\`). The choice is
`g_midiExtended` (4b76): 'B' when the MIDI-mapper setup in `midimap.cfg` names one of the
`strs_fmSynthNames` substrings (`fmt_sound.md` §8). The music *zone* codes stored in `RoomDef.music`
(0, 1, 3, 5, 7) and the intro codes (−94..−98) are mapped to these ids by a switch in code
(`Music_PlayZone` 1128:001a), not by a DGROUP table: 0→9001, 1/2→9003, 3/4→9004, 5/6→9005, 7→9000,
−98→9002, −97→9009, −96→9008, −95→9006, −94→9007.

### 4.7 Animation counters, dialog strings, static pager (0b1e–0b95) [O]

* 0b1e / 0b20 / 0b22 `g_animEraseCount` / `g_animSpriteCount` / `g_animUpdateCount` (i16): list
  lengths of the animation engine (`10b8`).
* `strs_dialogButtons` 0b24..0b6f: footer kind 1 text (0b24, starts with key glyph 0x86), footer
  kind 2 (0b31), button labels at 0b40 (go back a page), 0b4f (see next page), 0b5d (done),
  0b62 (cancel), 0b69 (yes), 0b6d (no). `Dlg_Layout` stores these as **near** offsets in
  `g_dlgFooter` (5c62) and `g_btnLabel[2]` (5c68).
* 0b70 `g_dlgSaveBuf` (far): save-under pixmap of the open dialog. 0b74: 4 unreferenced zero bytes.
* 0b78 `g_bndlPagerDesc` (PagerDesc, 0x1e bytes, layout §4.19): title NULL, footer 0,
  texts → 5c74, pageCount 0, x 11, y 14, width 285, lines 11, pics → 5c90, picDx → 5cac,
  picDy → 5cba. It is only used by the dead `10c8:0000`.

### 4.8 Option flags and player animation tables (0b96–0e9f) [O]

Flags (u8):

| Off | Name | Init | Meaning |
|---|---|---|---|
| 0b96 | g_soundOn | 1 | music and sound effects (user record +0x0e) |
| 0b97 | g_voiceOn | 1 | voice narration (user record +0x0f) |
| 0b98 | g_gateWalkStarted | 0 | one-frame pulse: auto-walk to the gate started (elves leave) |
| 0b99 | g_inputLocked | 0 | auto-walk in progress |
| 0b9a | g_jumping | 0 | in jump frames 12..21 |
| 0b9b | g_unused0b9b | 0 | cleared each frame, never set |
| 0b9c | g_rankPromoted | 0 | |
| 0b9d | g_redrawRequest | 0 | 1 rebuild room, 2 restore play area |
| 0b9e | g_coinTargetSlot | 0xFF | slot hit by the coin toss |
| 0b9f | g_elfQuizResult | 0 | |

Player animation tables (all used by `Player_Update` 10e0:0447 and the climb copy in 1168).
Frames index BNDL 300 (`g_playerBndl` 2db4). Step tables are **1-based with a dummy element 0**:

| Off | Size | Name | Type | Indexing / values |
|---|---|---|---|---|
| 0ba0 | c | g_netSwingFrames | u8[12] | [step 1..11] = frames 24..34 |
| 0bac | c | g_emptySwingFrames | u8[12] | [step 1..11] = 35..45 |
| 0bb8 | e | g_coinTossFrames | u8[14] | [step 1..13] = 46..53, then 0 |
| 0bc6 | e | g_coinFxFrames | u8[14] | FX sprite frame per step: 54..59 at steps 7..12, else 0 |
| 0bd4 | 3 | g_doorFrames | u8[3] | [1..2] = 9, 10 (back view) |
| 0bd7 | d | g_bumpFrames | u8[13] | [1..12] = 68..63, 63..68 |
| 0be4 | 1a | g_bumpDxRight | i16[13] | added per step when facing right (sum 0) |
| 0bfe | 1a | g_bumpDxLeft | i16[13] | subtracted per step when facing left (sum 0) |
| 0c18 | e | g_obstacleMinXRight | i16[7] | room 36 obstacle: minimum x for jump frames 15..21, facing right |
| 0c26 | e | g_obstacleMaxXLeft | i16[7] | maximum x for jump frames 15..21, facing left |
| 0c34 | 55 | g_nextFrame | u8[85] | successor frame for walk/jump; 255 = end of jump |
| 0c89 | aa | g_frameDxRight | i16[85] | **added** to x when entering frame f facing right |
| 0d33 | aa | g_frameDxLeft | i16[85] | **subtracted** from x when entering frame f facing left |
| 0ddd | aa | g_frameY | i16[85] | **absolute** y of the sprite's top in frame f |
| 0e87 | 2 | g_mouseWalkLatched | u16 | |
| 0e89 | 6 | g_shopWalkX | i16[3] | auto-walk x to the net shop per level: 220, 112, 378 |
| 0e8f | 10 | g_room9RankHints | i16[8] | hint event by rank: 11, 13, 16, 19, 22, 25, 28, 31 (`Room9_QueueRankHint` 10e0:10b2) |
| 0e9f | 1 | pad | | |

x is the sprite's left edge, and the sprite is mirrored when facing left. That is why there are two
dx tables. Frame catalogue and the movement state machine: `code_10e0-10f0.md` §3.4 / §4. The frame
sizes are not in the image; they are read from the PMAP headers into `g_frameW/H` (5cd6/5d80).

### 4.9 HUD, ranks, celebration (0ea0–1063) [O]

| Off | Size | Name | Type | Values / meaning |
|---|---|---|---|---|
| 0ea0 | 20 | **g_rankThresholds** | u32[8] | 0, 5, 25, 70, 115, 170, 230, 300. `Rank_FromTotal(total)` (10e8:07a6): `i=0; while (i<8 && thr[i] <= total) i++; return i-1` → rank 0..7. `g_rankTier` = rank/2. |
| 0ec0 | 1 | g_clueBoxMode | u8 | 0 clue list, 1 answer prompt, 2 CONTINUE |
| 0ec1/0ec2 | 1+1 | g_showCoins/Nets | u8 | init 1, never changed |
| 0ec3 | 4 | **g_gemsNeeded** | u8[4] | treasures per climb by tier: 2, 3, 4, 5 |
| 0ec7 | 28 | g_gemX | i16[4][5] | HUD gem icon x [tier][slot]; unused slots 0 |
| 0eef | 28 | g_gemY | i16[4][5] | HUD gem icon y [tier][slot] |
| 0f17 | 24 | str_AnswerPrompt | cstr | `FF 05 FF` + 3-line "click on an answer" prompt (clue box mode 1) |
| 0f3b | 19 | str_ClickToGoOn | cstr | unreferenced |
| 0f54 | 6 | g_clueAbortKeys | u16[3] | {0x0d, 0x1b, 0} |
| 0f5a / 0f60 / 0f66 | 6/6/9 | tmpl_Coins / tmpl_Nets / tmpl_Points | bytes | `FF 05 FF` + "00" / "00" / "00000" + NUL: counter text templates. `10e8:02db/0359/04be` copy them to a stack buffer (`F_SCOPY@`, CX = 6/6/9) and write the digits into the copy. |
| 0f70..0f88 | 4 each | g_celebBndl323, g_celebScript20301, g_treasureBndl, g_celebScript20302, g_celebBndl324, g_celebBndl325, g_rankImage | far | resources loaded by the celebration / rank screen (10f0) |
| 0f8c | 1c | g_rankThresholdLabels | far[7] | → 1005, 1007, 100a, 100d, 1011, 1015, 1019 ("5".."300"), drawn at (121, 181−16·i) |
| 0fa8 | 20 | **g_rankNames** | far[8] | → 101d, 1028, 102d, 1033, 103a, 1042, 104b, 1055: `FF 05 FF` + Trainee / 1..6 stars / Champion |
| 0fc8 | 10 | g_rankPromoHints | i16[8] | hint event when promoted to rank r: −1, 12, 15, 18, 21, 24, 27, 30 |
| 0fd8 | 10 | g_rankStayHints | i16[8] | hint when not promoted: 10, 14, 17, 20, 23, 26, 29, 32 |
| 0fe8 | 6 | fmt_Name | cstr | `FF 05 FF %s` |
| 0fee | 17 | fmt_TotalTreasures | cstr | `FF 05 FF` "Total Treasures %lu" |
| 1005 | 18 | strs_rankThresholds | strs | 7 label strings |
| 101d | 46 | strs_rankNames | strs | 8 rank strings |

HUD gem positions [O]:

| tier | x | y |
|---|---|---|
| 0 | 469, 486 | 315, 315 |
| 1 | 477, 469, 486 | 306, 323, 323 |
| 2 | 469, 486, 469, 486 | 306, 306, 323, 323 |
| 3 | 477, 469, 486, 469, 486 | 298, 308, 308, 322, 322 |

### 4.10 Net shop, hiding places, clue words (1064–1287) [O]

| Off | Size | Name | Type | Meaning |
|---|---|---|---|---|
| 1064 | 2 | g_netShopState | i16 | NetShop_Tick state 0..7 |
| 1066 | 6 | g_netSignX | i16[3] | net-shop sign x per level: 207, 99, 365 (y = 0x18) |
| 106c | c | g_spotPict | far[3] | pictures of the 3 hiding places of the current room: PMAP `1400 + RoomDef.slotObject[i]` if < 0xFD, else NULL (`1100:0638`) |
| 1078 | 1 | g_numClues | u8 | clue words collected on the current level (0..3) |
| **1079** | 40 | **g_strNoun** | far[16] | [0] → 1141 (""), [1..15] → nouns |
| **10b9** | 40 | **g_strNounPlural** | far[16] | [0] → 1141, [1..15] → plurals |
| **10f9** | 20 | **g_strCount** | far[8] | [0] → 1141, [1..7] → count words |
| **1119** | 14 | **g_strSize** | far[5] | [0] → 1141, [1..4] → size words |
| **112d** | 14 | **g_strShape** | far[5] | [0] → 1141, [1..4] → shape words |
| 1141 | 13d | strs_clueWords | strs | the 46 word strings (1141 is the empty string) |
| 127e | 2 | unk127e | i16 | init −1, no reference found |
| 1280 | 8 | unk1280 | bytes | zero, no reference found |

**Hiding-place attribute vocabulary.** These are the words of the clue system. A hiding-place
object kind k (0..111) has four attributes `{count, size, shape, noun}` in OTHR 10555 (4 bytes per
kind, `fmt_othr_tables.md`). A value v ≥ 1 selects word v of the matching table. The attribute
values index these tables directly:

| v | noun (1079) / plural (10b9) | string offs | count (10f9) | size (1119) | shape (112d) |
|---|---|---|---|---|---|
| 1 | tree / trees | 1142 / 11ab | one 1225 | big 1248 | round 125e |
| 2 | bush / bushes | 1147 / 11b1 | two 1229 | small 124c | triangle 1264 |
| 3 | flower / flowers | 114c / 11b8 | three 122d | tall 1252 | square 126d |
| 4 | flag / flags | 1153 / 11c0 | four 1233 | short 1257 | rectangle 1274 |
| 5 | mushroom / mushrooms | 1158 / 11c6 | five 1238 | | |
| 6 | sign / signs | 1161 / 11d0 | six 123d | | |
| 7 | berry / berries | 1166 / 11d6 | seven 1241 | | |
| 8 | acorn / acorns | 116c / 11de | | | |
| 9 | nest / nests | 1172 / 11e5 | | | |
| 10 | pinecone / pinecones | 1177 / 11eb | | | |
| 11 | rock / rocks | 1180 / 11f5 | | | |
| 12 | beehive / beehives | 1185 / 11fb | | | |
| 13 | snail / snails | 118d / 1204 | | | |
| 14 | shovel / shovels | 1193 / 120b | | | |
| 15 | woodpecker hole / woodpecker holes | 119a / 1213 | | | |

**Clue text and voice pairing** (`Clue_BuildTexts` 1100:0793, `Clue_BuildVoiceIds` 1100:08ae, code):
for each level L the bits of `g_clueFlags[L]` (5ea8) select attributes in the fixed order count (8),
size (4), shape (2), noun (1). The k-th selected value `v = g_clueAttr[L][k]` (5eab) gives
`g_clueText[L][k]` (5e84) and `g_clueVoice[L][k]` (5eb7):

| attribute | text | voice offset |
|---|---|---|
| count | `g_strCount[v]` | v + 0x25 (38..44) |
| size | `g_strSize[v]` | v + 0x2c (45..48) |
| shape | `g_strShape[v]` | v + 0x30 (49..52) |
| noun | `g_cluePlural[L] ? g_strNounPlural[v] : g_strNoun[v]` | plural ? v + 0x16 (23..37) : v + 7 (8..22) |

The spoken clip is SSND `bankBase + offset`. `bankBase` = 7300 / 7400 / 7500 for the narrator bank
`g_quizVoiceBank[q]` (1c60) of the question just answered (§4.15) on the praise screen (`1150:06ac`);
the HUD clue replay (`10e8:01ef`) always uses bank 1 = 7300 (ARCHITECTURE.md §7.1 C20). Offsets 0..7 of each bank are
the praise/sorry messages, 8..52 the 45 clue words. `g_cluePlural[L] = (count attribute of the key
object > 1)`, also when count is not one of the selected attributes.

### 4.11 Coin pile, GameMain constants (1288–1299) [O]

| Off | Size | Name | Values |
|---|---|---|---|
| 1288 | 1 | g_coinPileRoom | init 0xFF (none); 0xFE = armed |
| 1289 | 4 | g_coinPilePmap | far, PMAP 1699 |
| 128e | 4 | g_elfCountByTier | u8[4] = 6, 8, 10, 12 (elves per level by rank tier) |
| 1292 | 8 | g_cursorIds | u16[4] = 128, 129, 130, 0xFFFF: EXE cursor resources loaded by `Cursor_LoadSet` (1020:0000) into `g_cursors` (3d64) |

### 4.12 Menu bar definition (129a–13c5) [O]

Installed by `1120:0000` (`Menu_SetMenuBar(&g_gameMenuBar)`) and removed by `1120:000d`.

```
MenuBarDef   @13bf (6 bytes): i16 count = 3; MenuPopupDef far* popups (-> 1383)
MenuPopupDef (0x14 bytes, 3 records at 1383, 1397, 13ab):
  +00 char far* title         ('&' marks the accelerator)
  +04 i16       unk04         63 / 78 / 66: title width [I, from G&G] (not read by 1078)
  +06 i16       itemCount
  +08 char far* items         LF-separated item texts; 1078 replaces each LF with NUL in place
  +0c FARPROC far* handlers   itemCount far function pointers
  +10 i16       unk10         0x84 / 0x81 / 0x8f = 132 / 129 / 143: drop-down width [I, from G&G] (not read)
  +12 i16       unk12         0x46 / 0x88 / 0x01 = 70 / 136 / 1: title x [I, from G&G] (not read)
```

| Popup | Title @ | Items @ | Handlers @ | Items → handler (command id = popup·0x100 + item) |
|---|---|---|---|---|
| 0 | 1311 "&File" | 1317 | 1338 | See Rank → `10f0:0487`; Start Another Game → `1140:0062`; Exit → `1140:0098` |
| 1 | 1344 "&Options" | 134d | 1377 | New Player Messages → `1140:0015`; Music and Sound → `1140:00cc`; Voice → `1140:00f7` |
| 2 | 129a "&Help" | 12a0 | 12fd | About the Game → `1180:0117`; Using the Keyboard → `1180:0128`; Using the Mouse → `1180:0139`; Educational Focus → `1180:014a`; About Treasure Mountain! → `1140:0000` |

The 5th Help handler is relocated to `1140:0000` (`MenuCmd_AboutTM`; ARCHITECTURE.md §7.1 C6).

**The three unread fields** (answers former open question 1) [O values, I meaning]. TM Windows
never reads +0x04, +0x10 or +0x12: `Menu_SetMenuBar` (1078:0000) copies each record (F_SCOPY) and
uses only +0x06 and +0x08 to build a Win16 menu. *Gizmos & Gadgets* (DOS, 1993) links the same TLC
menu library with the identical 0x14-byte record and draws its menu bar itself, and there all three
fields are read (G&G addresses as in `re/gg/docs/ARCHITECTURE.md` §5.5): the title's click zone is
(x = +0x12, y 0, w = +0x04 + 2, h 9) (40a4:0035..0078), the title picture is drawn at x = +0x12
(40a4:04db), and the drop-down box is opened at x = +0x12, y 9, width +0x10 (40a4:056c). Read the
same way, TM Windows' values (the records at file offsets 0x24F83, 0x24F97 and 0x24FAB of
`TMTWINCD.EXE`, re-read for this note) lay out a self-drawn bar exactly, with 3-px gaps:

| Popup | Title x (+0x12) | Title width (+0x04) | Next title x | Drop-down width (+0x10) |
|---|---|---|---|---|
| 2 Help | 1 | 66 | 1 + 66 + 3 = 70 | 143 |
| 0 File | 70 | 63 | 70 + 63 + 3 = 136 | 132 |
| 1 Options | 136 | 78 | – | 129 |

So the data was authored for a DOS-style menu bar drawn by the game (titles left to right Help,
File, Options; the Win16 menu follows the record order instead). G&G's own values are title width
54 for all three and x 0 / 54 / 108 for File / Options / Help. No TM Windows code depends on these
fields; an engine can ignore them.

### 4.13 Music controller, hints, title narration (13c6–142b) [O]

| Off | Size | Name | Init | Meaning |
|---|---|---|---|---|
| 13c6 | 4 | g_musicActive | 0 | u32: 1 after `Music_PlayZone` |
| 13ca | 2 | g_musicId | −1 | tune id chosen by the last `Music_PlayZone` |
| 13cc | 1 | unk13cc | 0 | no reference |
| 13cd | 1 | g_unk13cd | 0xFF | written 0/1/0xFF by 1150/1160, never read |
| 13ce | 2 | g_hintPending | −1 | queued hint event (consumed by Room_Run) |
| **13d0** | 44 | **g_hintBit** | | i16[34]: event → bit in `g_hintUnseen` (5f01), −1 = always shown |
| 1414 | 8 | g_hintVoiceStopKeys | | u16[4] = {0x0d, 0x1b, 0x4b0, 0}: input codes that cut a hint voice short |
| 141c | 8 | g_titleVoiceFirst | | i16[4] for story pages 1..4: 7150, 7153, 7156, 7158 (code base 141a) |
| 1424 | 8 | g_titleVoiceCount | | i16[4]: 3, 3, 2, 2 clips (code base 1422) |

**Hint (message ↔ voice) table.** Hint event e (0..33) shows text OTHR `11500 + e` (entry e of BNDL
327) and plays SSND `7100 + e` (`1130`). It is shown once per player unless `g_hintBit[e] = −1`:

```
g_hintBit = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1,10,11,12, -1,13,14, -1,15,16, -1,17,18,
             -1,19,20, -1,21,22, -1,23,24, -1,25 }
```

Events 10, 14, 17, 20, 23, 26, 29 and 32 (end-of-climb rank results) are always shown. Event 33 is
never raised. The event triggers are listed in `code_10f8-1148.md` §3.9.

**Title story narration** (`1138:043d`): page p (1..4) plays SSND `first[p] .. first[p]+count[p]−1`,
i.e. 7150–7152, 7153–7155, 7156–7157, 7158–7159.

### 4.14 Menu-command dialogs (142c–198d) [O]

Each body text is followed by its `DlgDesc`. The captions are grouped at 1933..198d.

**DlgDesc** (0x1a bytes; consumers `GameDlg_RunModal` 10c0:0ae5, `Dlg_Draw` 10c0:0764; full semantics in
`code_1098-10d8.md` §2.8 and §10):

```
+00 u16       type          0 non-modal box, 1 info box (OK / click), 8 Yes/No (returns 1 Yes, 0 No, -1 Esc)
+02 char far* title         NULL = no title bar
+06 u8        footer        0 none, 1 = text 0b24, 2 = text 0b31
+07 char far* body          (CR = line break; FF escapes)
+0b u8        defaultButton / current value (patched by the caller before the call)
+0c i16 x, +0e i16 y        -1 = centre in 512x384
+10 i16 textWidth           -1 = measure
+12 i16 lineCount           -1 = 1 + number of CR/LF
+14 PixMap far* picture     NULL
+18 u8 picDx, +19 u8 picDy
```

| DlgDesc | type | footer | default | caption @ | body @ (first words) | Used by |
|---|---|---|---|---|---|---|
| 14d6 | 8 | 1 | 0 | 1933 Start Another Game | 142c "You are about to return to the Club Sign-In…" | 1140:0062 |
| 1533 | 8 | 1 | 0 | 1946 Exit | 14f0 "Do you want to exit the program?…" | 1140:0098 |
| 15cc | 8 | 1 | 1 (= g_hintsOn, patched at 15d7) | 194b New Player Messages | 154d "The New Player Messages help you…" | 1140:0015 |
| 1625 | 8 | 1 | 1 (= g_soundOn, 1630) | 195f Music and Sound | 15e6 "Do you want the Music and Sound on?" | 1140:00cc |
| 163f | 8 | 1 | 1 (= g_voiceOn, 164a) | 196f Voice | 160a "Do you want the Voice on?" | 1140:00f7 |
| 1919 | 1 | 2 | 0; x 6, y 10, w 484, 14 lines | 1975 About Treasure Mountain! | 1659 product blurb (703 bytes) | 1140:0000 |

### 4.15 Quiz, users.dat and sign-in (198e–23d7) [O]

| Off | Size | Name | Type | Meaning |
|---|---|---|---|---|
| 198e | 2 | g_quizReadingAll | i16 | |
| **1990** | 2d0 | **g_quizVoiceBase** | u16[360] | first narration SSND of question q (0..358), 4000..6608, strictly increasing; [359] = 6616 = end sentinel. `voice_count = base[q+1] − base[q]` = question lines + 3 answers [I] (values 4..9). |
| **1c60** | 167 | **g_quizVoiceBank** | u8[359] | narrator bank 1/2/3 per question (124/129/106 questions) → praise/sorry/clue-word base 7300/7400/7500 |
| 1dc7 | 20 | g_quizSilentAnswers | i16[16] | 8, 235, 257, 154, 50, 169, 298, 299, 170, 176, 177, 178, 210, 231, 117, 148: answers never spoken |
| 1de7 | a | g_quizTierStart | i16[5] | 0, 63, 179, 302, 359: tier t uses questions [start[t], start[t+1]−1]; tier ≥4 uses all |
| 1df1 | 10 | g_quizPraiseText | far[4] | → 1e69, 1ea1, 1ed3 (3-line variant), 1f21 |
| 1e01 | 6 | g_quizResultStopKeys | u16[3] | {0x1b, 0x0d, 0} |
| 1e07 | 10 | g_quizSorryText | far[4] | → 1f5b, 1f94, 1fe0, 200f |
| 1e17 | 6 | g_quizSorryStopKeys | u16[3] | {0x1b, 0x0d, 0} |
| 1e1d | 26 | g_quizLineStopKeys | u16[19] | 0x126, 0x128, 0x125, 0x127 (arrows), 0x0d, 0x409, 0x1b, 0xa80–0xa82, 0xa70–0xa76, 0xa88, 0 |
| 1e43 | 26 | g_quizAnswerStopKeys | u16[19] | same contents, used for answers |
| 1e69 | 1df | strs_quizResults | strs | 4 praise texts and 4 sorry texts (`FF 05 FF` + LF-separated lines); "." at 1f59 (drawn after the clue word) |

**Quiz message ↔ voice pairing** (code 1150:049a/0748/06ac, 1409, 14d7): question q has text OTHR
`11000 + q`. Line k plays `g_quizVoiceBase[q] + k`. Answer a (0 = correct) plays
`g_quizVoiceBase[q] + nLines + a`. Praise text m (0..3, random) plays `bankBase + m`, followed by the
clue word `bankBase + g_clueVoice[level][g_numClues]`. Sorry text m plays `bankBase + 4 + m`, with
`bankBase = 7300 + 100·(g_quizVoiceBank[q] − 1)`. The table data contains only banks 1..3. The
input-code lists end with 0. Codes ≥ 0x100 are `0x100|VK` keys or hot-spot ids (`code_1008-1030.md`
§6.3).

users.dat (1158):

| Off | Size | Name | Content |
|---|---|---|---|
| 2048 | 3c | s_textWriteError | body of the write-error box |
| 2084 | 1a | g_dlgWriteError | DlgDesc type 1, footer 2, title → 209e |
| 209e | c | s_capWriteError | caption |
| 20aa / 20b4 | a / 13 | s_dirTlcwin / s_dirTmtwincd | `c:\tlcwin`, `c:\tlcwin\tmtwincd` (created with mkdir) |
| 20c7 / 20e4 | 1d / 1d | s_usersDatR / s_usersDatW | `c:\tlcwin\tmtwincd\users.dat` (read / write copies) |
| 2101 | 5 | g_usersMagic | `55 53 52 1A 00` = "USR\x1A": file signature (then 7 × 1583-byte UserRecord, `code_1150-1160.md` §5.1) |

Sign-in (1160):

| Off | Size | Name | Content |
|---|---|---|---|
| 2106 | 23 | s_fmtAlreadyOnList | printf format (`FF 03 05 FF %s FF 03 01 FF` …) |
| 2129 | 1a | g_dlgAlreadyOnList | DlgDesc type 1, footer 2, no title, body patched at 2130 (→ sprintf buffer) |
| 2143 | a3 | s_fmtRemoveConfirm | printf format (%s = name) |
| 21e6 | 1a | g_dlgRemovePlayer | DlgDesc type 8, footer 1, title → 225a, body patched at 21ed |
| 2200 | 1a | g_dlgListFull | DlgDesc type 1, footer 2, body → 2271 |
| 221a | 1a | g_dlgEnterName | DlgDesc type 0 (non-modal), footer 1, title → 22f4, body → 230a |
| 2234 | 1a | g_dlgTitleExit | DlgDesc type 8, footer 1, title → 233c, body → 2341 (used by 1138:0372 and 1160:04fa) |
| 224e | 9 | g_playerName | char[9], init "________" ('_' = empty cell; name[0]=='_' → nobody signed in) |
| 2257 | 2 | g_rowH | i16, init 25 (row height = height of PMAP 2181) |
| 2259 | 1 | g_signInBackdrop | u8 |
| 225a / 2271 / 22f4 / 230a / 233c / 2341 | | captions and bodies of the boxes above | |
| 2390 / 2399 | 9 / 9 | s_nameBlank(2) | "________" |
| 23a2 / 23b3 | 11 / 11 | s_startNewPlayer(2) | list row label (measured / drawn) |
| 23c4 / 23cc / 23ce | 8 / 2 / 9 | rank strings | list row rank (`1160:0bf5..0c8f`): r = `Rank_FromTotal(rec.totalTreasures)`; r = 0 draws "Trainee" at x 195, r = 1..6 draws "*" r times at x 195 + 8·i, r = 7 draws "Champion"; y = row y + 3 |

### 4.16 Room table `g_rooms` (23d8–294f) [O]

50 records × 28 (0x1c) bytes, index = room number (`g_room` 5cca). Record r is at `23d8 + 0x1c·r`.

```
+00 i16 startX        entry x for a type-1/2 transition into this room
+02 i16 startY        142 in every room
+04 u8  startFrame    0 or 60 (front view)
+05 u8  startFacingLeft
+06 u8  exitLeft      room through the left edge, 0xFF = wall (x clamped >= 75)
+07 u8  exitRight     0xFF = wall (x+w clamped <= 437)
+08 u8  exitDoor      room behind the door
+09 u8  (0xFF in all rooms, unused)
+0a u8  typeLeft      transition type of the left exit (1/2 = use destination's start pose)
+0b u8  typeRight
+0c u8  typeDoor      door enterable only if 1 or 2 (all rooms: 1)
+0d u8  (1 in all rooms, unused)
+0e i16 doorX         door x, -100 = no door
+10 i16 (-100 in all rooms, unused)
+12 u16 bgPmap        background PMAP id (drawn by 1198:0234)
+14 u8  slotObject[3] hiding-place picture per third of the screen (PMAP 1400+n; 0xFD/0xFE markers; 0xFF none). Runtime (1100:04b5/0000); 0 in the image
+17 u8  slotContent[3] 0xFF nothing, 0 taken, 2 treasure, 3 key, 0xFD net-shop coin slot, 0xFE locked gate. Runtime; 0 in the image
+1a u8  level         0, 1, 2 = mountain levels; 3 = castle-wall climb (room 49)
+1b u8  music         zone code for Music_PlayZone (0 hall, 1/3/5 levels, 7 climb)
```

Columns are 170 px wide: slot = x / 170 clamped to 0..2. Slot picture x = `scene_slotX` (2dd4) =
26, 197, 368. Revealed treasure/key x = `scene_revealX` (2dda) = 49, 220, 391. The slot fields are
saved per player (user record 0x500 / 0x596, 150 bytes each).

Static values (`tables.rooms` in the JSON) [O]:

| room | start x,y | frame / faceL | exits L / R / door | types L / R / door | door x | bg PMAP | level | music |
|---|---|---|---|---|---|---|---|---|
| 0 | 230,142 | 0 / 0 | - / - / 9 | 1 / 0 / 1 | 117 | 1036 | 0 | 0 |
| 1 | 0,142 | 0 / 0 | 0 / - / - | 0 / 0 / 1 | - | 1037 | 0 | 0 |
| 2 | 0,142 | 0 / 0 | 1 / - / - | 0 / 0 / 1 | - | 1037 | 0 | 0 |
| 3 | 0,142 | 0 / 0 | 2 / - / - | 0 / 0 / 1 | - | 1037 | 0 | 0 |
| 4 | 0,142 | 0 / 0 | 3 / - / - | 0 / 0 / 1 | - | 1037 | 0 | 0 |
| 5 | 0,142 | 0 / 0 | 4 / - / - | 0 / 0 / 1 | - | 1037 | 0 | 0 |
| 6 | 0,142 | 0 / 0 | 5 / - / - | 0 / 0 / 1 | - | 1037 | 0 | 0 |
| 7 | 0,142 | 0 / 0 | 6 / - / - | 0 / 0 / 1 | - | 1037 | 0 | 0 |
| 8 | 0,142 | 0 / 0 | 7 / - / - | 0 / 0 / 1 | - | 1038 | 0 | 0 |
| 9 | 233,142 | 60 / 0 | 16 / 10 / 0 | 0 / 0 / 1 | 270 | 1003 | 0 | 1 |
| 10 | 231,142 | 60 / 0 | 9 / 11 / - | 0 / 0 / 1 | - | 1004 | 0 | 1 |
| 11 | 78,142 | 60 / 0 | 10 / 12 / 17 | 0 / 0 / 1 | 115 | 1005 | 0 | 1 |
| 12 | 231,142 | 60 / 0 | 11 / 13 / - | 0 / 0 / 1 | - | 1006 | 0 | 1 |
| 13 | 231,142 | 60 / 0 | 12 / 14 / - | 0 / 0 / 1 | - | 1007 | 0 | 1 |
| 14 | 231,142 | 60 / 0 | 13 / 15 / - | 0 / 0 / 1 | - | 1010 | 0 | 1 |
| 15 | 288,142 | 60 / 0 | 14 / 16 / 22 | 0 / 0 / 1 | 325 | 1011 | 0 | 1 |
| 16 | 231,142 | 60 / 0 | 15 / 9 / - | 0 / 0 / 1 | - | 1012 | 0 | 1 |
| 17 | 0,142 | 0 / 0 | 11 / 18 / - | 1 / 0 / 1 | - | 1001 | 0 | 1 |
| 18 | 231,142 | 60 / 0 | 17 / 22 / - | 0 / 0 / 1 | - | 1000 | 0 | 1 |
| 19 | 231,142 | 60 / 0 | 18 / 20 / - | 0 / 0 / 1 | - | 1000 | 0 | 1 |
| 20 | 231,142 | 60 / 0 | 19 / 21 / - | 0 / 0 / 1 | - | 1000 | 0 | 1 |
| 21 | 231,142 | 60 / 0 | 20 / 22 / - | 0 / 0 / 1 | - | 1000 | 0 | 1 |
| 22 | 352,142 | 0 / 1 | 18 / 15 / - | 0 / 1 / 1 | - | 1002 | 0 | 1 |
| 23 | 330,142 | 60 / 0 | 29 / 24 / - | 0 / 0 / 1 | - | 1013 | 1 | 3 |
| 24 | 231,142 | 60 / 0 | 23 / 25 / - | 0 / 0 / 1 | - | 1014 | 1 | 3 |
| 25 | 83,142 | 60 / 0 | 24 / 26 / 30 | 0 / 0 / 1 | 120 | 1017 | 1 | 3 |
| 26 | 231,142 | 60 / 0 | 25 / 27 / - | 0 / 0 / 1 | - | 1021 | 1 | 3 |
| 27 | 382,142 | 60 / 0 | 26 / 28 / 35 | 0 / 0 / 1 | 419 | 1022 | 1 | 3 |
| 28 | 231,142 | 60 / 0 | 27 / 29 / - | 0 / 0 / 1 | - | 1023 | 1 | 3 |
| 29 | 231,142 | 60 / 0 | 28 / 23 / - | 0 / 0 / 1 | - | 1024 | 1 | 3 |
| 30 | 0,142 | 0 / 0 | 25 / 31 / - | 1 / 0 / 1 | - | 1001 | 1 | 3 |
| 31 | 231,142 | 60 / 0 | 30 / 35 / - | 0 / 0 / 1 | - | 1000 | 1 | 3 |
| 32 | 231,142 | 60 / 0 | 31 / 33 / - | 0 / 0 / 1 | - | 1000 | 1 | 3 |
| 33 | 231,142 | 60 / 0 | 32 / 34 / - | 0 / 0 / 1 | - | 1000 | 1 | 3 |
| 34 | 231,142 | 60 / 0 | 33 / 35 / - | 0 / 0 / 1 | - | 1000 | 1 | 3 |
| 35 | 352,142 | 0 / 1 | 31 / 27 / - | 0 / 1 / 1 | - | 1002 | 1 | 3 |
| 36 | 326,142 | 60 / 0 | 42 / 37 / - | 0 / 0 / 1 | - | 1025 | 2 | 5 |
| 37 | 231,142 | 60 / 0 | 36 / 38 / - | 0 / 0 / 1 | - | 1028 | 2 | 5 |
| 38 | 398,142 | 60 / 0 | 37 / 39 / 43 | 0 / 0 / 1 | 435 | 1029 | 2 | 5 |
| 39 | 231,142 | 60 / 0 | 38 / 40 / - | 0 / 0 / 1 | - | 1032 | 2 | 5 |
| 40 | 231,142 | 60 / 0 | 39 / 41 / - | 0 / 0 / 1 | - | 1033 | 2 | 5 |
| 41 | 231,142 | 60 / 0 | 40 / 42 / - | 0 / 0 / 1 | - | 1034 | 2 | 5 |
| 42 | 132,142 | 60 / 0 | 41 / 36 / 48 | 0 / 0 / 1 | 169 | 1035 | 2 | 5 |
| 43 | 0,142 | 0 / 0 | 38 / 44 / - | 1 / 0 / 1 | - | 1001 | 2 | 5 |
| 44 | 231,142 | 60 / 0 | 43 / 48 / - | 0 / 0 / 1 | - | 1000 | 2 | 5 |
| 45 | 231,142 | 60 / 0 | 44 / 46 / - | 0 / 0 / 1 | - | 1000 | 2 | 5 |
| 46 | 231,142 | 60 / 0 | 45 / 47 / - | 0 / 0 / 1 | - | 1000 | 2 | 5 |
| 47 | 231,142 | 60 / 0 | 46 / 48 / - | 0 / 0 / 1 | - | 1000 | 2 | 5 |
| 48 | 352,142 | 0 / 1 | 44 / 42 / - | 0 / 1 / 1 | - | 1002 | 2 | 5 |
| 49 | 80,142 | 0 / 0 | - / - / - | 1 / 1 / 1 | - | 1040 | 3 | 7 |

Topology [O, from the table]:
- Each mountain level is a ring of 7–8 rooms: 9→10→…→16→9, 23→…→29→23 and 36→…→42→36.
- Each ring has a side passage entered through a door: level 0 via room 11 (door → 17) and room 15
  (door → 22); level 1 via 25 → 30 and 27 → 35; level 2 via 38 → 43 and 42 → 48. The passage is
  17 → 18 → 22 (level 0), 30 → 31 → 35 (level 1) and 43 → 44 → 48 (level 2).
- Rooms 19–21, 32–34 and 45–47 have exits but no room leads into them: 18's right exit is 22,
  31's is 35, 44's is 48. They all use the generic background 1000. **[I]** These are
  vestigial rooms.
- Room 0 (castle treasure hall entrance) has its door to room 9, and room 9's door leads back to
  room 0. In the image, hall rooms 1–8 link only leftwards (k → k−1). `TreasureHall_BuildRooms`
  (1190:0000) **rewrites** the exits and backgrounds of rooms 0..8 at every game start: with
  n = `g_climbsDone` it uses `rooms = n < 2 ? 2 : min(9, (n−2)/6 + 2)`, sets room 0 right = 1,
  rooms 1..rooms−2 left/right = i∓1 with bg 1037, and the last room right = 0xFF with bg 1038
  (`code_1168-1198.md` §7.2).
- The gate rooms are 11, 25, 38 and the net-shop rooms 13, 24, 36. Both come from code and OTHR
  data, not from this table (`fmt_othr_tables.md` §3.4).

### 4.17 Castle-wall climb tables (2950–2ab1) [O]

| Off | Size | Name | Values |
|---|---|---|---|
| 2950 | a0 | cl_layouts | i16[4][4][5]: [layout k][screen 0..3][ladder column 0..4], 0 none, 1 full ladder, 2 lower half. `cl_layout = &cl_layouts[k]`, `k = rand()·(g_rankTier+1)/0x8000` (`1168:0047`), so tier t picks one of layouts 0..t at random. |
| 29f0 | a | cl_colX | 82, 169, 256, 343, 430 (ladder centre x) |
| 29fa | 8 | cl_climbFrames | i16[4] (phase 0..3) = 0, 1, 2, 3 (BNDL 321 frames; low byte used) |
| 2a02 | 8 | cl_climbDx | 0, 1, 0, 0 |
| 2a0a | 8 | cl_climbUpDy | 12, 20, 12, 20 |
| 2a12 | 8 | cl_climbDownDy | 20, 12, 20, 12 |
| 2a1a | a | cl_jumpFrames | i16[5] idx 0..4 = 5..9 |
| 2a24 | 8 | cl_jumpDxLeft | idx 0..3 = 18, 15, 35, 19 (magnitude) |
| 2a2c | a | cl_jumpDxRight | idx 1..4 = 18, 15, 35, 19 ([0] = 0) |
| 2a36 | a | cl_jumpDy | idx 0..4 = −6, 6, 5, −5, −6 |
| 2a40 | e | cl_fallFrames | step 1..7 = 10..16 (code base 2a3e) |
| 2a4e | e | cl_fallDx | step 1..7 = −6, 2, −2, 0, 7, 0, −1 (base 2a4c) |
| 2a5c | e | cl_fallDy | step 1..7 = 2, −1, −1, 2, −2, 1, −1, before +32 (base 2a5a) |
| 2a6a | c | cl_landFrames | step 1..6 = 18..23 (base 2a68) |
| 2a76 | c | cl_landDx | step 1..6 = 6, −14, 18, 42, 1, 0 (base 2a74) |
| 2a82 | c | cl_landY | step 1..6 = 142, 189, 172, 168, 152, 143 (absolute; base 2a80) |
| 2a8e | 18 | cl_creatSeq | tick 1..12 = 0, 1, 2, 3, 3, 3, 3, 3, 2, 1, 0, −1 (−1 = hidden; base 2a8c) |
| 2aa6 / 2aa8 | 2+2 | cl_walkMouseFollow / cl_climbMouseFollow | 0 |
| 2aaa | 8 | cl_creatSpawnPermille | by tier: 0, 50, 100, 200 |

Layouts [O] (rows = screens 0..3, columns 0..4):

| k | screen 0 | screen 1 | screen 2 | screen 3 |
|---|---|---|---|---|
| 0 | 0 0 1 0 0 | 0 1 1 1 0 | 0 1 0 1 0 | 0 0 0 1 0 |
| 1 | 0 0 1 0 0 | 0 1 1 1 0 | 0 1 0 1 0 | 0 1 0 0 0 |
| 2 | 0 0 1 0 0 | 0 1 1 1 0 | 0 1 0 1 0 | 0 0 1 2 0 |
| 3 | 0 0 1 0 0 | 0 1 1 1 0 | 0 1 0 1 0 | 0 2 1 0 0 |

Algorithm: `code_1168-1198.md` §2.

### 4.18 Sound pointer and elf tables (2ab2–2ca9) [O]

| Off | Size | Name | Meaning |
|---|---|---|---|
| 2ab2 | 4 | sfx_current | currently loaded SSND (1170) |
| 2ab6 | 1 | g_caughtElf | 1-based index of the elf in the net; 0 none |
| 2ab7 | 2 | elf_count | active elves |
| 2ab9 | 8 | unk2ab9 | zero, no reference found |
| 2ac1 | 1 | elf_confined | elves may not change rooms |
| 2ac2 / 2ac4 / 2ac6 | 2 each | elf_netBusy / elf_throwState (0..13) / elf_projFrame (init −1) | |
| 2ac8 | 3 | elf_levelStartRoom | 11, 25, 38 (first room for elf placement per level) |

**Elf behaviour scripts.** Each behaviour b has a frame script `int8 frames[]` terminated by −1
(loop/finish) or −2 (leave the room), plus parallel tables of x delta and **absolute** y per step.
Behaviour 4 also has a sound table. The handler for behaviour b is `elf_behHandlers[b]`. Frames index
the elf's `ElfAnimSet` (BNDL per set, §5).

| b | frames @ (len) | dx @ | y @ | extra | handler |
|---|---|---|---|---|---|
| 0 | 2acb (17) = 0,0,1,1,…,7,7,−1 | 2adc i16[16] | 2afc i16[16] | | 1178:0a6d |
| 1 | 2b1c (17) = 0,0,…,7,7,−1 | 2b2d i16[16] | 2b4d i16[16] | | 1178:0a90 |
| 2 | 2b6d (11) = 0,0,…,4,4,−1 | 2b78 i16[10] | 2b8c i16[10] | | 1178:0ab3 |
| 3 | 2ba0 (17) = 0,0,…,7,7,−1 | 2bb1 i16[16] | 2bd1 i16[16] | | 1178:0ad6 |
| 4 | 2bf1 (10) = 0..8,−2 | 2bfb i16[9] | 2c0d i16[9] | sfx 2c1f i16[9] = −1,−1,−1,1,−1,1,1,37,−1 (−1 none, else Sfx id) | 1178:0af9 |
| 5 | (throw, no script) | | | | 1178:0cb0 |

Other elf tables:

| Off | Size | Name | Values |
|---|---|---|---|
| 2c31 | a | elf_netFrames | i8[10] by net step: 0,0,0,0,1,2,3,4,5,−1 (−1 = release) |
| 2c3b / 2c4d | 12 / 12 | elf_netDx / elf_netY | i16[9]: 0,0,0,0,4,2,1,0,0 / 190,190,190,190,194,196,197,198,198 |
| 2c5f / 2c69 / 2c73 | a each | elf_throwDxR / elf_throwDxL / elf_throwY | i16[5] by throw step: 0,−5,−9,−9,−5 / 0,−3,−4,−5,0 / 195,190,182,179,190 |
| 2c7d | 12 | elf_projY | i16[9] by projectile frame: 181,175,173,164,169,175,166,176,190 |
| 2c8f | 18 | elf_behHandlers | far[6] (relocated): 1178:0a6d, 0a90, 0ab3, 0ad6, 0af9, 0cb0 |
| 2ca7 | 2 | elf_projSteps | |

The dx / y arrays per step are in `tables.elves` in the JSON.

### 4.19 Help pager, cut-scene, treasure hall, room scene (2caa–2de1) [O]

**PagerDesc** (0x1e bytes, consumer `10c0:0cde`):

```
+00 char far* title   +04 u8 footer   +05 char far* far* texts   +09 u8 pageCount
+0a i16 x  +0c i16 y  +0e i16 textWidth  +10 i16 lineCount
+12 PixMap far* far* pics   +16 u16 far* picDx   +1a u16 far* picDy   (low bytes used)
```

| Off | Size | Name | Meaning |
|---|---|---|---|
| 2caa | 1c | help_pages | far[7]: help page texts, `Res_Load(firstId + i)` |
| 2cc6 | 1c | help_pics | far[7]: page pictures (freed after the viewer) |
| 2ce2 / 2cf0 | e / e | help_picDx / help_picDy | u16[7] |
| 2cfe | 1e | help_desc | PagerDesc: title runtime, footer 0, texts → 2caa, count runtime, x 8, y 22, width 480, 11 lines, pics → 2cc6, dx → 2ce2, dy → 2cf0 |
| 2d1c | 44 | strs_helpTitles | 2d1c, 2d2b, 2d3e, 2d4e: titles of the 4 help topics (pages OTHR 11600–11603, 11605–11606, 11607, 11608–11611, chosen in code 1180:0117..014a) |
| 2d60 | 1 | gate_unlocking | |
| 2d61 / 2d65 / 2d69 | 4 each | cut_bndl / cut_script1 / cut_script2 | far |
| 2d6d | 4 | cut_bndlIds | u16[2] = 410, 412 (level 0→1, 1→2 cut-scenes) |
| 2d71 | 4 | cut_script2Ids | u16[2] = 20201, 20205 (OTHR animation scripts) |
| 2d75 | 4 | cut_bgIds | u16[2] = 2200, 2201 (PMAP backgrounds) |
| 2d79 | 4 | gate_lockBndl | far, BNDL 415 |
| 2d7e | 6 | hall_pedTreasure | i8[6] init −1 (treasure index per pedestal) |
| 2d84 | c | hall_pedAnim | i16[6] init −1 |
| 2d90 / 2d9c | c / c | hall_pedX / hall_pedY | 112, 143, 174, 272, 303, 334 / 40, 92, 40, 92, 40, 92 |
| 2da8 | c | hall_pedFrameBase | 20, 30, 40, 50, 60, 70 (base into `g_animFrames`) |
| 2db4 | 4 | g_playerBndl | far, BNDL 300 |
| 2db8 | 4 | unused2db8 | no reference |
| 2dbc | 14 | scene_levelTreasures | far[5]: treasure BNDLs (330+t) of this level's chests |
| 2dd0 | 4 | scene_fountainBndl | far, BNDL 328 (room-36 fountain with frogs, PMAP 1380–1383; not a tree: corrected per ARCHITECTURE.md §7.1 C9) |
| 2dd4 / 2dda | 6 / 6 | scene_slotX / scene_revealX | 26, 197, 368 / 49, 220, 391 |
| 2de0 | 2 | scene_fountainFrame | fountain animation frame 0..3 |

### 4.20 Borland RTL data (2de2–387f) [O]

Only needed if the engine wants bit-exact RTL behaviour. `rand()` matters: the climb layout, clue
choice and quiz picks all use it.

| Off | Size | Name | Meaning |
|---|---|---|---|
| 2de2 | 20 | s_AbnormalTermination | abort message |
| 2e02..2e10 | | _atexitcnt, _exitbuf, _exitfopen, _exitopen (far, all init 1000:0218), __exitCleanupDone | exit machinery |
| 2e12 | 190 | _streams | FILE[20] (20 bytes each; stdin 2e12, stdout 2e26, stderr 2e3a, stdaux 2e4e, stdprn 2e62) |
| 2fa2 / 2fa4 | 2 / 28 | _nfile (20) / _openfd u16[20] | |
| 2fcc / 2fce / 2fd0 | 2 each | _fmode 0x4000, _notUmask 0xFFFF, _doserrno | |
| 2fd2 | 59 | _dosErrorToSV | i8[89] |
| 302c | 6 | __xalloc | static xalloc ("Out of memory" at 3036) |
| 3032 | 4 | _new_handler | far, init 1000:055c |
| **3044** | 4 | **__randSeed** | u32, init 1. Borland LCG: `seed = seed·0x015A4E35 + 1; return (seed >> 16) & 0x7FFF` (`1000:0859`; `code_1000.md`) |
| 3048 | 101 | _ctype | u8[257] (index c+1) |
| 314a / 3151 | 7 / 60 | s_null "(null)", __vprinterClass u8[96] | printf tables |
| 31b2 | 128 | eh_Vars | EH globals (+0A terminate → 11a0:00da, +0E unexpected → 11a0:02b8, relocated) |
| 32da | c0 | _sys_errlist | far[48] → strings 339c..357a; the unused errno numbers point at single NUL bytes 353d..3546 and 358c..3596 (empty strings) |
| 339a | 2 | _sys_nerr | 48 |
| 339c | 210 | strs_sysErrList | errno messages, "Unknown error" (3597), ": " (35a5), LF (35a8), CR (35aa) |
| 35ac / 35ae | 2+2 | __stdinBufSet / __stdoutBufSet | |
| 35b0 / 35b9 | 9 / 9 | s_notype / s_BCCxh1 | RTTI / EH signatures |
| 35c2..35cc | 2 each | string::case_sensitive 1, paranoid_check 0, skip_white 1, initial_capac 63, resize_inc 64, freeboard 63 | Borland `string` class statics |
| 35ce | 5a | strs_stringRange | 3 × "String reference out of range" |
| 3628 | 30 | s_fpFormatsNotLinked | printf / scanf (3628, 362d) |
| 3658 | c | eh_InstanceCtx | far[3] → SS:0010 / 0014 / 0016 |
| 3664 | c | unk3664 | zero, no reference |
| 3670 / 3672 / 3674 | 2/2/4 | eh_TaskTableCap 30, eh_CachedSS, eh_CurCtx | |
| 3678 | 2 | unk3678 | |
| 367a | 6f | strs_setargv | 3 out-of-memory messages |
| 36ea / 36ee | 4 / 1b | _argv0 / s_OutOfMemSetargv0 | |
| 370a / 370e | 4+4 | _ReadHook / _WriteHook | NULL |
| 3712 / 372a / 3730 | 18 / 6 / 6 | __sigHandlers far[6], __sigNumbers {2,4,8,11,15,22}, __sigSubcode | |
| 3736 | 124 | strs_fpErrors | "Floating Point: " + reason messages |
| 385a / 385c | 2 / 6 | __realcvt (near 1000:2a7e) / __scanfFloatHooks | |
| 3862 | 18 | __startupTable | 4 records `{u8 kind (0 near, 1 far), u8 prio, u16 off, u16 seg}`: 1000:06ad prio 0x20, near 08fa prio 2, 1000:2fb8 prio 10, 1000:32aa prio 9 |
| 387a | 6 | __exitTable | 1 record: 1000:0703 prio 0x20 |

---

## 5. BSS (3880–66a1)

All zero at start. Only the regions that matter to the game logic or its persistence are detailed.
The others are listed with size and owner in `dgroup.tsv`. "rec +x" = field of the persisted
UserRecord (`code_1150-1160.md` §5.1).

### 5.1 Platform layer [O]

| Off | Size | Name | Meaning |
|---|---|---|---|
| 3880 | 8 / 8 / 2 | g_wingRect, g_wingRect2, g_wingStride (3890) | WinG buffer geometry |
| 3892 | 28 | g_wingBmi | BITMAPINFOHEADER |
| 38ba | 400 | g_wingColorTable | RGBQUAD[256] |
| 3cba | 20 | p_WinG | far[8]: WING.DLL ordinals 1001, 1002, 1003, 1004, 1005, 1006, 1010, 1009 (CreateDC, RecommendDIBFormat, CreateBitmap, GetDIBPointer, Get/SetDIBColorTable, BitBlt, StretchBlt) |
| 3cda / 3cdc | 2 / 2 | g_hInstance / g_hwndMain | |
| 3cde | 82 | g_iniPath | char[130] |
| 3d60 | 4 | g_lpfnEnumProc | |
| 3d64 | 1e | g_cursors | HCURSOR[15] |
| 3d82 / 3d84 / 3d86 | 2 each | g_evType (0 none, 1 quit, 2 key, 3 L-down, 4 L-up, 5 R-down, 6 R-up), g_evP1, g_evP2 | pending event |
| 3d88 / 3d8c | 4 / 4 | g_resScratchMax / g_resScratch | 280000-byte resource scratch buffer |
| 3d90 | 400 | g_paletteRGB | RRGB 900 copied in by GameMain (`GetResourceInto(900, DS:3d90)` 1118:0012, then `Pal_SetFromRGB(DS:3d90)`). 1024 bytes = the NE-padded size of RRGB 900 in TMT.DLL (`extracted/res/TMT.DLL/32516/900` is 0x400 bytes; 768 bytes of RGB data) [O]. |
| 4190 | 1 | g_transparentIndex | sprite colour key |
| 4191 / 4293 | 2 / 2 | g_foreColorPhys / g_foreColorIndex | |
| 4193 | 100 | g_logToPhys | u8[256] logical → physical index |
| 4296..42a0 | 2 each | g_hdcScreen, g_hdcMem, g_hbmBack, g_selPlanarBits, g_selPixels, g_hGdiHeap | |
| 42a2 | 800 | g_dirtyRects | `{i16 x,y,w,h}[256]` |
| 4aa2..4aba | | g_fonts far[3], g_curFont, g_fontHeight, g_fontSeg, g_fontWidths, g_fontOffLo, g_fontOffHi, g_fontBits | FFNT state (`fmt_bndl_ffnt.md`) |
| 4ac2 / 4ad2 / 4ad4 | 10 / 2 / 20 | g_waveFmt (PCMWAVEFORMAT {1,1,rate,rate,1,8}) / g_hWaveOut / g_waveHdr (WAVEHDR) | |
| 4af4 | 82 | g_soundsDir | `<exe dir>\SOUNDS\` |
| 4b76 / 4b78 | 2 / 2 | g_midiExtended / g_musicIdx | |
| 4b7a | 1f4 | g_hotspots | `{u16 id; i16 x, y, w, h}[50]` (id 0 = free; highest index wins hit tests) |
| 4d6e | 1f4 | g_hotspotsSaved | backup (dead code; overflow target of Hotspot_Set) |
| 4f62 | 20 | g_timerDeadline | u32[8] timeGetTime deadlines (slot 7 = 100 ms frame timer) |

### 5.2 Animation engine (4f82–5c3d) [O; capacities I]

| Off | Size | Name | Element |
|---|---|---|---|
| 4f82 | 1d0 | g_animChan[16] | 29 bytes: `+00 u8 layer, +01 u8 stop, +02 u8 flags, +03 i16 x, +05 i16 y, +07 i16 frameBase, +09 i16 loopCount[2], +0d i16 user0, +0f i16 user1, +11 far onEnd, +15 far cursor (NULL = free), +19 far base` |
| 5152 | 190 | g_animFrames[100] | far PixMap* indexed by `frameBase + frame` (`5292` = slot 80 holds the current treasure image) |
| 52e2 | 384 | g_animEraseRects[100] | 9 bytes `{u8 noDirty; i16 x, y, w, h}` |
| 5666 | 578 | g_animSprites[100] | 14 bytes `{u8 layer; u8 noDirty; i16 x, y; u8 mode; far pixmap; u8 sflags; i16 frame}` |
| 5bde | 60 | g_animUpdateRects[16] | 6 bytes `{i16 x; u8 y; i16 w; u8 h}` |

The capacities 100/100/100/16 follow from the spacing. The code checks no bounds.

### 5.3 Dialog layout (5c3e–5cc9) [O]

5c3e/5c42/5c46/5c4a `g_btnX/Y/W/H` i16[2]. 5c4e: 4 unreferenced bytes. 5c52..5c60: `g_dlgX`, `g_dlgY`,
`g_dlgW`, `g_dlgH`, `g_dlgMarginL`, `g_dlgMarginT`, `g_dlgMarginB`, `g_dlgTitleX`. 5c62 `g_dlgFooter`
(near char*). 5c64/5c66 footer x/y. 5c68 `g_btnLabel` near char*[2]. 5c6c `g_btnLabelX` i16[2]. 5c70
`g_btnState` i16[2]. 5c74/5c90/5cac/5cba: the arrays of `g_bndlPagerDesc` (far[7], far[7], i16[7],
i16[7]). 5cc8 `g_musicPauseDepth` i8.

### 5.4 Game state (5cca–6177) [O]

| Off | Size | Name | Meaning |
|---|---|---|---|
| 5cca | 1 | g_room | current room 0..49; 0xFE = climb finished (rec +0x1df) |
| 5ccb | 1 | g_nextRoom | room change at end of frame |
| 5ccc / 5cce | 2+2 | g_playerX / g_playerY | sprite top-left |
| 5cd0 / 5cd1 | 1+1 | g_playerFrame / g_playerFacingLeft | |
| 5cd2 / 5cd4 | 2+2 | g_playerW / g_playerH | size of the current frame |
| 5cd6 / 5d80 | aa / aa | g_frameW / g_frameH | i16[85], from the PMAP headers of BNDL 300 (1198:0000) |
| 5e2a..5e52 | | player action state | g_roomTransition, g_doorStep, g_coinStep, g_walkActive, g_arriveAction, g_playerHidden, g_netStep, g_emptySwingStep, g_netX, g_fxFrame/X/Y/W/H, g_walkTargetX/Y, g_walkArg3, g_walkArriveAction, g_prevShopCoins, g_entryX/Y/Frame/Facing, g_inputEvent, g_bumpStep, g_lastMouseX/Y (offsets in `dgroup.tsv`) |
| 5e54 | 4 | g_hudArt | BNDL 301 |
| 5e58 | 1 | g_rankTier | rank/2 (rec +0x09) |
| 5e59 | 1 | g_haveKey | rec +0x15 |
| 5e5a / 5e5c | 2+2 | g_coins / g_nets | rec +0x16 / +0x18 |
| 5e5e | 1 | g_gemsFound | rec +0x1a |
| 5e5f | 2 | g_climbPoints | rec +0x1b |
| 5e61 | 1 | g_treasureCount2 | |
| 5e62 | 4 | g_totalTreasures | u32, rec +0x0a, input of `Rank_FromTotal` |
| 5e66..5e7c | | celebration pointers, g_celebImageCount, g_rank (5e78), g_seeRankSave | |
| 5e7e | 2 | g_shopCoinsLeft | 4..0 (rec +0x1d) |
| 5e80 | 4 | g_shopSignBndl | BNDL 302 |
| 5e84 | 24 | g_clueText | far char*[3 levels][3]: pointers into the clue-word tables (§4.10) |
| 5ea8 | 3 | g_clueFlags | per level attribute mask (rec +0x20) |
| 5eab | 9 | g_clueAttr | u8[3][3] selected values in mask order (rec +0x23) |
| 5eb4 | 3 | g_cluePlural | rec +0x2c |
| 5eb7 | 12 | g_clueVoice | u16[3][3] voice offsets (§4.10) |
| 5ec9 | 3 | g_keyKind | object kind hiding the key per level (OTHR 10555 index) |
| 5ecc | 6 | unk5ecc | no reference |
| 5ed2..5ed7 | | g_coinPileNoSpawnRoom, g_coinPileX, g_coinPileW, g_coinPileH | (pile y fixed at 230) |
| 5eda | 18 | g_restartJmp | jmp_buf (KERNEL CATCHBUF + Borland words at +0x14/+0x16); longjmp 1 = restart at sign-in, 2 = exit |
| 5ef2 / 5ef3 | 1+1 | g_isChampion / g_newClimb | |
| 5ef4 | 9 | unk5ef4 | no reference |
| 5efd / 5efe | 1 / 2 | g_music5efd / g_musicZone | |
| 5f00 | 1 | g_hintsOn | rec +0x10 |
| 5f01 | 4 | g_hintUnseen | u32 bitmask, bit n = once-only hint n not yet shown (rec +0x11) |
| 5f05 | e | unk5f05 | no reference |
| 5f13..5f36 | | title animation state | BNDL 400/401 pointers and scripts, anim slots, g_titleMusicPending (5f35) |
| 5f3a | 48 | g_quizSlots | QuizSlot[9] (slot = 3·level + elf): `{i16 q (−1 none); u8 perm[3]; u8 tried[3]}` (rec +0x197) |
| 5f82 | 1 | g_quizTier | rec +0x2f |
| 5f83 | 167 | g_quizStatus | u8[359]: 0 unseen, 1 right first try, 2 last answer wrong, 3 right after retry; bit 7 = picked (rec +0x30) |
| 60ea..6147 | | quiz runtime | text resource, range, counts, current slot/question, answer pointers, layout, speaker icon (`code_1150-1160.md` §3.2) |
| 6149 | 15 | unk6149 | no reference |
| 615e..6176 | | sign-in | row bar pixmaps, BNDL 322, name cursor, g_rowW, g_userCount, g_userSel, g_users (far UserRecord[7]), g_nameDone |

### 5.5 Climb, elves, hall, RTL (6178–66a1) [O]

| Off | Size | Name | Meaning |
|---|---|---|---|
| 6178..6190 | 2 each | cl_input, cl_onLadder, cl_done, cl_screen, cl_nextScreen, cl_nextY, cl_fullPresent, cl_fadePending, cl_column, cl_climbPhase, cl_jumpStep, cl_fallStep, cl_landStep | climb state |
| 6192 / 6196 / 61c6 | 4 / 30 / 30 | cl_climbBndl (BNDL 321), cl_climbW/H i16[24] | |
| 61f6 / 61fa / 61fe | 4 each | ladder PMAPs 1090 / 1091 / 1092 | |
| 6202 / 6206 / 620e | 4 / 8 / 8 | cl_creatBndl (BNDL 326), cl_creatW/H i16[4] | |
| 6216..6222 | 2 each | creature tick, x, y, frame, flip, column, screen | |
| 6224 | 4 | cl_layout | far → `cl_layouts[k]` |
| 6228 | 15e | elf_animSets | ElfAnimSet[7], 0x32 bytes: `+00 i16 frameCount, +02 i16 1, +04 i16 behaviour, +06 far frames[10], +2e far bndl`. Set → BNDL: 0→316, 1→317, 2→312, 3→315, 4→320, 5→314, 6→311 |
| 6386 | 240 | elf_actors | Elf[32], 0x12 bytes: `+00 i16 set (−1 free), +02 behaviour, +04 step, +06 x, +08 y, +0a facing, +0c room, +0e inNet, +10 hasItem` |
| 65c6 / 65ca / 65cc | 4 / 2 / 2 | elf_projBndl (BNDL 310), elf_projX, elf_drawYOff | |
| 65ce | 2 | g_climbsDone | completed climbs (rec +0x62d); treasure BNDL = 330 + (n−1) mod 43 |
| 65d0 / 65e8 | 18 / 18 | hall_treasureBndl far[6] / hall_treasureAnim far[6] (OTHR 20000+t) | |
| 6600 | 80 | _atexittbl | far[32] |
| 6680 | 4 | unk6680 | no reference |
| 6684 | 1 | __fputcChar | |
| 6686 / 668a | 4 / 4 | eh_CleanupFnTmp / eh_FilterFnTmp | |
| 668e / 668f | 1 / 1 | g_badTypeid / g_badCast | static exception objects |
| 6690 | 4 | eh_TaskTable | |
| 6694 / 6696 / 669a | 2 / 4 / 2 | _argc, _argv, __argvCap | |
| 669c | 6 | __heapDesc | far-heap descriptor (last byte of DGROUP image at 66a1) |

---

## 6. Pointer tables and code pointers (all 198 relocations) [O]

**No C++ vtables live in DGROUP.** The only code-pointer arrays are the ones below. The game modules
are C-style, and the RTL's `xmsg`/`string` classes have no virtual functions here [O: the relocation
targets].

| At (DGROUP) | Count | Targets |
|---|---|---|
| 0a22..0a46 | 10 | → MIDI name strings 0a56..0ac1 |
| 0b7d, 0b8a, 0b8e, 0b92 | 4 | g_bndlPagerDesc fields → 5c74, 5c90, 5cac, 5cba (BSS) |
| 0f8c..0fc4 | 15 | rank threshold labels → 1005..1019; rank names → 101d..1055 |
| 1079..113d | 50 | clue-word pointer tables → 1141..1274 |
| 12fd..130d | 5 | Help handlers → 1180:0117, 1180:0128, 1180:0139, 1180:014a, 1140:0000 |
| 1338..1340 | 3 | File handlers → 10f0:0487, 1140:0062, 1140:0098 |
| 1377..137f | 3 | Options handlers → 1140:0015, 1140:00cc, 1140:00f7 |
| 1383..13c1 | 10 | MenuPopupDef title/items/handlers pointers and MenuBarDef.popups → 1311, 1317, 1338, 1344, 134d, 1377, 129a, 12a0, 12fd, 1383 |
| 14d8, 14dd, 1535, 153a, 15ce, 15d3, 1627, 162c, 1641, 1646, 191b, 1920 | 12 | DlgDesc title/body → captions 1933..1975, bodies 142c..1659 |
| 1df1..1e13 | 8 | quiz praise/sorry texts |
| 2086, 208b, 2130, 21e8, 21ed, 2207, 221c, 2221, 2236, 223b | 10 | sign-in / users.dat DlgDesc title/body (2130 and 21ed are later overwritten with sprintf buffers) |
| 2c8f..2ca3 | 6 | elf behaviour handlers → 1178:0a6d, 0a90, 0ab3, 0ad6, 0af9, 0cb0 |
| 2d03, 2d10, 2d14, 2d18 | 4 | help_desc arrays → 2caa, 2cc6, 2ce2, 2cf0 |
| 2e04, 2e08, 2e0c | 3 | RTL exit hooks → 1000:0218 |
| 3032 | 1 | _new_handler → 1000:055c |
| 31bc, 31c0 | 2 | EH terminate / unexpected → 11a0:00da, 11a0:02b8 |
| 32da..3396 | 48 | _sys_errlist → 339c..3596 |
| 3864, 3870, 3876, 387c | 4 | start-up / exit table functions → 1000:06ad, 1000:2fb8, 1000:32aa, 1000:0703 |

(The exact list is `relocations[]` in the JSON.)

---

## 7. Engine guidance [I]

* **Load at run time rather than embed.** All game-content tables here (room table, clue
  vocabulary, rank thresholds/names, gem positions, menus, dialog texts, hint/narration id tables,
  quiz voice tables, player/climb/elf animation tables, MIDI names) are data in the user's
  `TMTWINCD.EXE`. An engine can read DGROUP the way `dgroup.py` does (NE header → auto-data
  segment → relocations) and decode the same offsets, so no game text or tables need to be in the
  engine. A version check: DGROUP length 0x66a2 and the `"USR\x1a"` signature at 2101.
* **Mutable copies.** The original modifies some of these tables in place: MIDI names byte 0, menu
  item LF → NUL, DlgDesc `defaultButton` (+0x0b) and body pointers (2130, 21ed), the room table
  slot fields, and the exits/backgrounds of hall rooms 0..8 (1190:0000). Copy the decoded data into engine structures
  and keep the originals immutable.
* **1-based indexing.** Keep the dummy element 0 where the table has one. For the climb fall/land/
  creature tables, index `step − 1` into the real array (§1.2).
* **Pointers to the same string.** Index 0 of every clue-word table points to the empty string at
  1141. The two "________" strings and the two "Start New Player" strings are separate copies.

---

## Open questions

1. (Answered, inferred from *Gizmos & Gadgets*) `MenuPopupDef` +0x04, +0x10 and +0x12 are never
   read by `1078`; they are the title width, drop-down width and title x of a self-drawn menu bar,
   the way G&G's copy of the same TLC menu library reads them. TM Windows' values fit that reading
   exactly with 3-px gaps (§4.12).
2. `RoomDef` +0x09 (always 0xFF), +0x0d (always 1) and +0x10 (always −100): no reader found. They
   could be an unused fourth exit (exit/type/x).
3. Rooms 19–21, 32–34 and 45–47 cannot be entered through any static exit. Does the code ever set
   `g_nextRoom` to them, e.g. through layout records or the hall code? `1100:0000` gives them no
   hiding places.
4. (Resolved) Hall rooms 1–8: their exits are rebuilt at run time by 1190:0000 (§4.16).
5. `g_quizVoiceBase[359]` = 6616 looks like an end sentinel. No code was seen reading index 359.
   Treat `voice_count = base[q+1] − base[q] = nLines + 3` as inferred until checked against the OTHR
   question texts.
6. Unreferenced bytes: 001c–002f, 0044, 0047 (=1), 007a, 06fa, 0b74, 127e (=−1) / 1280, 13cc, 2ab9,
   2db8, 3664, 3678 and the BSS holes 4abc, 4ac0, 5c4e, 5ecc, 5ef4, 5f05, 5f29, 5f37, 6149, 6680. No
   reference was found in the Ghidra disassembly export, which does not cover all ~14 KB of code
   outside recognised functions. Some may be used by unexported code.
7. (Resolved) `g_paletteRGB` (3d90) is 1024 bytes because `GetResourceInto` copies the whole NE
   resource, and RRGB 900 is 0x400 bytes after NE padding. An engine must not copy more than 0x400.
8. `g_gfxConst4` (0860) = 4 gates the fade stubs (only if 3 or 4). Its intended meaning (a
   display-mode or palette-mode enum?) is unknown.
9. The `strs_fmSynthNames` matching rule (which text in `midimap.cfg` is compared, and whether the
   match is case-sensitive) is described in `fmt_sound.md` §8. It was not re-verified here.
10. Several references in the `refs` column are missing for functions that Ghidra did not export
    (e.g. `1140:0000`, `1140:0062`, `1140:00cc`, which use `14d6`, `1625`, `1919`). Those regions
    carry the owner segment instead. A rerun of the xref scan over a fuller disassembly would
    complete them.
