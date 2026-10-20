# ssg.exe (G&G DOS v1.0): user interface — fonts, text, dialogs, menus, help, attract, sign-in, saves, hints

Scope (wave 1, "UI" topic): the TLC UI library as *Gizmos & Gadgets!* uses it — fonts (ovl01 `2b16`),
the text renderer (ovl03 `2b89`), dialog boxes and buttons (ovl27 `3f56`, ovl28 `4034`), the menu bar
(ovl31 `40a4`) and its commands (ovl32 `4106`), help topics (ovl33 `411d`), the scrolling text viewer
(ovl34 `4140`) and the scroll bar (ovl30 `406a`) — and the screens built on it: the attract sequence
(ovl06 `2c11`), sign-in and roster (ovl07 `2caa`), `PLAYERS.DAT` (ovl02 `2b5a`), New Player Hints
(ovl12 `3060`), the options and the quit paths. Wave 3 implements these modules from this document
without reading the binary. §20 compares the modules with tmport's `src/text` and `src/ui`.

Evidence: the named Ghidra export `re/gg/ghidra_export/` (disassembly read for every function of
the overlays above; the decompiled C was used only as a guide, and every switch, every call that
passes a struct by value and every pascal call was read from the assembly), the bytes of `ssg.exe`
(DGROUP tables), the archives through the decoder outputs of `tools/gg/extract_all.py`, and the
DOSBox-X reference frames of `extracted/captures/gg/` (`w0/`, `r2/player2/`, `r2/puzzle/`), against
which the drawing model of this document was checked pixel by pixel (§21). Scratch tools in
`extracted/scratch/gg/wave1/ui/`: `uimodel.py` (a Python model of fonts, `Text_Render`, `Dlg_Layout`,
`Dlg_Draw`, buttons, the menu drop-down), `verify_ui.py`, `verify_signin.py`, `verify_view.py`,
`verify_story.py` (the capture checks), `seqattract.py` (attract animation scan), `dis.py`,
`dseg.py`, `xref.py`, `rd.py` (disassembly, cross references, bytes).

Notation: **[O]** observed in code, bytes, data or captures; **[I]** inferred; **[R]** recommendation
for the engine. Addresses are Ghidra `SSSS:OOOO` of the G&G project (`re/CONTEXT.md`, G&G section);
`DS:XXXX` is DGROUP `1ea7` (file offset `0xFC70 + XXXX`); overlay code is at the overlay's file offset +
`OOOO` (`overlay_map.tsv`). Names are those of `re/names/gg/`, which since the wave 1 merge include this topic's names (with
four corrections: `Roster_LoadOrCreate` was `Players_CreateFile`, `Roster_UpdateTyped` was
`Roster_Insert`, `g_rosterScrollBar` was `g_rosterList`, `g_hintSlotId` was `g_hintQueue`) and the
names of the other wave 1 topics for the services called here (`Zones_AddLocal`,
`Mouse_ForceDefault`, `Gfx_FrameRect`, `SndRes_PlayEffect`, `ScreenAnim_*`, `List_InsertSorted`, …;
`re/names/gg/README.md`). A *tick* is a PIT
tick (145.652 Hz, 6.866 ms). "Effect n of bank h" is sound handle h + n (`code_sound.md` §13). Game
text is cited by resource id or DGROUP offset only. Pascal routines (`Lpx_Draw`, `Lpx_DrawKeyed`,
`Gfx_DrawGlyph`) are written with their parameters in source order (x, y, picture, mode).

---

## 1. Modules and coverage

| Ovl / seg | Module | Role | Functions specified |
|---|---|---|---|
| ovl01 `2b16` | fonts | load, select, colour, draw string / char, metrics | 9 / 9 |
| ovl03 `2b89` | text | `Text_Render` (measure and draw), wrappers, run measure, char draw | 6 / 6 |
| ovl27 `3f56` | dialogs | layout, save-under, draw, zones, modal loop, pager, message / Yes-No / chimps boxes | 14 / 14 |
| ovl28 `4034` | buttons | text buttons (dialogs) and picture buttons (sign-in, workshop) | 11 / 11 |
| ovl30 `406a` | scroll bar | the sign-in list's scroll bar (ARCHITECTURE calls ovl30 a list box: the rows are drawn by ovl07) | 5 / 5 |
| ovl31 `40a4` | menu bar | install / remove, bar, drop-down loop, item zones | 6 / 6 |
| ovl32 `4106` | menu commands | File and Options handlers, Yes/No from resource bundles | 7 / 7 |
| ovl33 `411d` | help | topic loader and the six Help handlers | 7 / 7 |
| ovl34 `4140` | text viewer | the puzzles' scrolling instructions (Hint button) | 20 / 20 |
| ovl06 `2c11` | attract | state 1: title, story pages, animations, sign-in prompt | 9 / 9 |
| ovl07 `2caa` | sign-in | state 2: roster, typing, new player, removal, `PLAYERS.DAT` creation | 14 / 14 |
| ovl02 `2b5a` | players | `PLAYERS.DAT` open / read / write / save | 8 / 8 |
| ovl12 `3060` | hints | New Player Hint queue and box | 6 / 6 |

Callers in other topics are cited, not specified: the workshop, HUD and results (`code_vehicles_races.md`),
rooms, actors and set-up (`code_world.md`), puzzles (`code_puzzles_a.md`), the input fetch, zones,
timers and graphics primitives (`code_runtime.md`), sound (`code_sound.md`).

---

## 2. Runtime context

### 2.1 How UI code runs [O]

There is no UI "tick": every UI screen is a blocking loop that calls `Input_GetCode` (198d:0106,
`code_runtime.md` §9) and draws what changed. Two kinds of loops exist:

* **Framed loops**, 4 ticks per frame (`Timer_Set(0, 4)` … `Dirty_Flush` … `Timer_Wait(0)`): the
  attract animations (`Attract_PlayAnim` 2c11:0000) and the game loops that call `Hint_Update`.
* **Busy-poll loops** that call `Input_GetCode` as fast as the CPU allows, flush only after drawing,
  and rely on the mouse event handler's interrupt-time `Dirty_Flush` for the cursor: the dialog loop
  (3f56:06eb), the drop-down (40a4:02b4), the sign-in (2caa:0f7b), the story pages and the sign-in
  prompt (2c11:0736, 2c11:08cf), the text viewer (4140:078d). Their timing comes only from timer
  deadlines (§19). [R] In the engine every poll is a host pump (`PORTING_NOTES.md` §2.2).

`Input_GetCode` applies the global keys of `code_runtime.md` §9.3 inside every one of these loops:
Ctrl+S toggles the sound and returns 0; Esc calls `g_escHandler` DS:8656 when set and returns 0x1B;
Alt+H/F/O and F1..F3 open the menu through `g_menuHandler` DS:865a when it is set (the code is then
0); the HUD codes 6000..6003 and 5000..5100 call the HUD.

### 2.2 What each modal loop changes around itself [O]

| Loop | Time (`g_ticks`) | Sound | Local zones | Menu hook DS:865a | Cursor | Play mode DS:0096 | Input flushed |
|---|---|---|---|---|---|---|---|
| `Dlg_RunModal` 3f56:0655 | **frozen**: saved in `g_savedTicks` DS:6598 at entry (066e), written back at exit (08c3) | `Snd_Pause` / `Snd_Resume` | `Zones_Push` / `Zones_Pop` | saved, cleared, restored | `Mouse_ForceDefault(1)`, previous restored | saved, 0, restored | at entry and exit (`Input_Flush`) |
| `Menu_RunDropDown` 40a4:020b | runs | pause / resume | push on the first nesting level (`g_menuNesting` DS:3dd2) | cleared, then **set** to the drop-down at exit | forced, previous restored | saved, 0, restored | at entry and exit |
| `TextView_Run` 4140:0667 | runs | not paused | push / `Zones_ClearLocal` + pop | saved, cleared, restored | forced at entry, **unforced** (0) at exit | unchanged | no |
| `SignIn_Run` 2caa:0eb3 | runs | — | its own zones on the current list | — | arrow | set to 0 by the state (2caa:1249) | at entry |
| attract pages | runs | — | the full-screen zone DS:0a3a | — | hidden (`Mouse_Hide`) | — | no |

* The dialog loop is the only writer of `g_ticks` outside the timer interrupt (`code_runtime.md`
  §1.2): every deadline and every queued hint sees no time pass while a box is open, including the
  `Delay(4)` steps of a button flash and every page of the help pager. The sound tick runs on even
  values of `g_ticks`, so a box open for an odd number of ticks shifts the sound tick's phase by one
  PIT tick [O 1454:00c9; I effect].
* `Snd_Pause` is not nestable (`code_sound.md` §12.1): a dialog opened from a menu handler resumes
  the sound when it closes, and a drop-down that reopens after that handler (§6.5) then runs with
  the sound playing [O code; I effect].
* With the menu hook cleared, F1..F3 and Alt+H/F/O reach a dialog as raw codes 0x13B..0x13D, which it
  ignores; the global zones (menu titles, HUD) stay active under a dialog because only the local list
  is pushed: a click on the HUD inventory arrows while a box is open runs the HUD handlers
  [O zones; I reachability].

### 2.3 The shared buffer [O]

`g_resBuffer` DS:6574 (64 KB, every `Res_Read`) also holds the dialog save-under
(`Dlg_SaveUnder` 3f56:033e: `g_dlgSaveBuf` DS:bbb4 = `g_resBuffer`), the text viewer's save-under (at
`g_resBuffer` + 30000, 4140:06ca) and the menu bar pictures (bundle 1, read again by every
`Menu_DrawBar`). Every caller orders its reads so that nothing is overwritten while it is needed
(e.g. `Menu_RunDropDown` restores the box before it redraws the bar), but a resource read during a
dialog would corrupt the save-under. [R] Keep save-unders in their own buffers.

---

## 3. Fonts (ovl01 2b16)

### 3.1 Data [O]

Two `fnt` 0x1A resources (format: `fmt_graphics.md` §5) are loaded at start-up by `Font_Init`
(2b16:021c, called from `main`): resource 9 into `g_fonts[0]` DS:a040 (the UI font with key-cap glyphs)
and resource 10 into `g_fonts[1]` DS:a044 (the small font); font 0 is selected.

Font record (0x24 bytes, `Font_Load` 2b16:0000):

| Off | Type | Meaning |
|---|---|---|
| +0x00 | u16 | flags (file +4); bit 0 set: 1-bpp glyphs |
| +0x02 | u16 | record count (file +6) |
| +0x04 | u16 | height (file +8) |
| +0x06 | u16 | spacing added to every glyph advance (file +10); 2 for font 9, 1 for font 10 |
| +0x08 | u16 | set to 0, never read |
| +0x0A | u16 | **colour of this font** (`Font_SetColor`) |
| +0x0C | far | private copy of the resource |
| +0x10 | far | u16 width per code |
| +0x14 | far | u16 y offset per code |
| +0x18 | far | u16 per code, zeroed, never read |
| +0x1C | far | far pointer to each glyph's bitmap record (NULL when width 0) |
| +0x20 | far | glyph routine: `Gfx_DrawGlyph` 1399:0541 (flag bit 0) or `Lpx_DrawKeyed` 1399:02ee |

Selection state: `g_fontIndex` DS:1f66, `g_font` DS:1f68 (far record), `g_fontDrawGlyph` DS:a048,
`g_glyphColor` DS:1f6c (the colour `Gfx_DrawGlyph` paints with).

Key caps are ordinary glyphs of font 9 at codes 0x11 (Spacebar), 0x17 (Ctrl), 0x18 (Enter), 0x19..0x1C
(arrows), 0x1D (Ctrl+R), 0x1F (Esc), 16..62 pixels wide and 10 rows high (`fmt_graphics.md` §5); texts
embed them as raw bytes (the dialog footers DS:3e38 and DS:3e45 start with 0x1F and 0x18; the hint
footer DS:2472 contains 0x18), drawn by the same blitter in the text colour. Font 10 has only two
glyphs below 0x20: 0x18 (40 px) and 0x1D (9 px) [O `fnt.py` table].

### 3.2 Functions [O]

| Function | Behaviour |
|---|---|
| `Font_Select(n)` 2b16:0256 | g_fontIndex = n; g_font = g_fonts[n]; g_fontDrawGlyph = record +0x20; **g_glyphColor = record +0x0A**; returns the previous index |
| `Font_SetColor(c)` 2b16:0243 | record +0x0A = c and g_glyphColor = c (the colour belongs to the font: selecting the other font switches colour too) |
| `Font_CharWidth(c)` 2b16:03ea | 3 for 0x20; else width[c] + spacing — **also for codes without a glyph** (width 0 → spacing) |
| `Font_GetHeight()` 2b16:0418 | height + 1 (10 for both fonts): the line height of `Text_Render` |
| `Font_GetCurrent()` 2b16:0426 | far pointer to the selected record (the story page and the energy puzzle patch +0x06, §10.3) |
| `Font_DrawChar(x, y, c)` 2b16:0368 | 0x20: draws nothing, returns 3; width[c] = 0: draws nothing, **returns 0**; else glyph at (x, y + yoff[c]) via g_fontDrawGlyph, returns width[c] + spacing |
| `Font_DrawString(x, y, s)` 2b16:029c | per byte until NUL: CR or LF → y += **height + 2** (11), x = start x; 0x20 → x += 3; a negative (≥ 0x80) byte, a code > record count or a code without glyph → skipped with **no advance**; else glyph and x += width + spacing. No escapes, no wrapping |
| `Font_Load(id)` 2b16:0000, `Font_Init()` 2b16:021c | as above |

Glyph bitmaps (`Gfx_DrawGlyph`, pascal: x, y, far bitmap, 0): `u8 full bytes per row, u8 bits in the
partial byte, u16 rows`, then rows × (full + (bits > 0)) bytes, **bit 0 = leftmost pixel**; set bits are
painted in g_glyphColor, clear bits left alone, no clipping beyond the primitives' own [O].

Quirks [O code]: `Font_DrawString` accepts a code equal to the record count (font 10 has 127 records:
code 0x7F reads one entry past the arrays), `Font_CharWidth` and `Font_DrawChar` do no range check
at all (codes ≥ 0x80 index backwards). Code 0x7F can be typed into a sign-in name (§11.5). [R] Treat
out-of-range codes as glyph-less.

---

## 4. Text renderer (ovl03 2b89)

### 4.1 Interface [O]

`Text_Render` (2b89:007a, near, 13 word arguments, all from the two wrappers):

```
Text_Render(x, y, w, far char *s, far char far* far *subst, int unused,
            far int *outW, far int *outH, int draw)
```

| Wrapper | Call |
|---|---|
| `Text_DrawBox(x, y, w, s)` 2b89:0000 | `Text_Render(x, y, w, s, NULL, 0, NULL, NULL, 1)` |
| `Text_Measure(s, w, outW, outH)` 2b89:002a | `Text_Render(0, 0, w, s, NULL, 0, outW, outH, 0)` |
| `Text_Width(s)` 2b89:0056 | `Text_Measure(s, 320, &w, NULL)`; returns w |

The substitution table is always NULL and the sixth argument (TM Windows' per-glyph flush) is passed
on to `Text_DrawChar` and never read. Text uses the selected font and its colour; nothing is clipped.

### 4.2 Algorithm [O 2b89:007a..03ad]

```
right = x + w - 1;  spaceW = Font_CharWidth(' ') (3);  lh = Font_GetHeight() (10)
pen = x;  maxX = x;  cy = y;  underline = 0;  centre = 0
loop: c = *p++
  0x00: stop
  0x0A, 0x0D (2b89:028f):
      if centre: pen = x + half(w - MeasureRun(0x0A, p)); maxX = max(maxX, pen)
      else:      pen = x
      cy += lh
  0x20 (2b89:02d1):
      run = MeasureRun(0x20, p)                      # the next word, see 4.3
      if pen + spaceW + run > right:                 # wrap; the space is not drawn
          skip every following 0x20 and 0x09 byte    # TAB: vestige of TM Windows (no TAB code here)
          pen = x; cy += lh                          # never re-centred
      else:
          pen += draw ? Text_DrawChar(pen, cy, lh, ' ', underline) : spaceW   (both 3)
          maxX = max(maxX, pen)
  0xFF: k = *p++ (escapes, 4.4)
  other (2b89:0342):
      pen += draw ? Text_DrawChar(pen, cy, lh, c, underline) : Font_CharWidth(c)
      maxX = max(maxX, pen)
end: if outW: *outW = maxX - x;  if outH: *outH = cy - y + lh
```

* `half(v)` is C `v / 2` on 16-bit ints (CWD; SUB AX,DX; SAR AX,1: truncates toward zero).
* Only a space can break a line: the first word of a text, a word after a line break and a word
  longer than the box never wrap; characters are never clipped or wrapped individually.
* `Text_DrawChar(x, y, lh, c, underline)` (2b89:0466): `Font_DrawChar(x, y, c)` and, when
  underlining, `Gfx_HLine(x, y + lh - 1, advance)` in **`g_drawColor`** (not the text colour).
* The measured width is the right-most pen position minus x (it includes the spacing after the last
  glyph; an empty text gives 0); the height counts the last line, also an empty one after a final
  CR.

### 4.3 `Text_MeasureRun(delim, p, subst, inSubst, sp)` 2b89:03ca [O]

Hand-written assembly (LODSB loops), unlike TM Windows' C routine (10b0:053c, which adds
`Font_CharWidth` and stops at the delimiter everywhere). It sums **width[c] + 1** per byte (the raw
table width plus one, **not** `Font_CharWidth`: font 9's spacing is 2, so a word measures one pixel
per character narrower than it draws; a space inside a centred line measures 1, not 3) until
`delim`, NUL, LF or CR. `FF 01 n` measures substitution n−1 and then skips the escape's bytes up to
the next FF; any other `FF k` skips up to and including the next FF. Inside a substitution reached from the main loop the delimiter is ignored (AH is cleared:
spaces do not end the run, NUL resumes the main text); when called with inSubst (from the
substitution branch) it starts in `sp` and does stop at the delimiter.

The wrap test therefore sees `pen + 3 + Σ(width + 1)` against `x + w − 1` while the word then occupies
`Σ(width + spacing)`: with font 9 a word can overrun the box by up to one pixel per character; with
font 10 (spacing 1) the two agree. All captures agree with this rule (§21).

### 4.4 Escapes (`FF k`, table CS:03ae for k = 1..6) [O]

| Bytes | Effect | Bytes consumed |
|---|---|---|
| `FF 01 n …` | substitute subst[n−1] (§4.5); after it, skip to the next FF | `FF 01 n` + everything up to and including the next FF |
| `FF 02 …` | underline = !underline | up to and including the next FF |
| `FF 03 c …` | `Font_SetColor(c − 1)` — **also when only measuring**, and the colour is **not restored** at the end of the text | up to and including the next FF |
| `FF 04` | nothing | `FF 04` only (what follows is ordinary text) |
| `FF 05 …` | skip to FF; centre = !centre; when it becomes 1: pen = x + half(w − MeasureRun(0x0A, p)), maxX = max(maxX, pen) | up to and including the next FF |
| `FF 06 …` | comment (the designer notes of the hints) | up to and including the next FF |
| `FF k`, k = 0 or k > 6 | nothing | `FF k` only (a `FF 00` swallows the terminating NUL and runs on) |

The data uses `FF 03 c FF` (part texts, help pages) and `FF 06 note FF` (hints) only (`fmt_data.md`
§6). The character switch (CS:03ba) has exactly four keys: 0x0A, 0x0D, 0x20, 0xFF.

### 4.5 Substitutions [O, unused]

`FF 01 n` sets the substitution pointer to subst[n−1] (2b89:0102) and draws its bytes until its NUL
with the same space/wrap logic (2b89:0126..01cd; the wrap skips spaces and TABs in the substitution),
treating every other byte, CR and LF included, as a glyph; at its end the main text resumes after
the escape's closing FF. Both wrappers pass NULL, so a text containing `FF 01` would read through a
NULL pointer; none does.

---

## 5. Dialog boxes and buttons (ovl27 3f56, ovl28 4034, ovl32 4106)

### 5.1 The descriptor (0x32 bytes, passed by value with `F_SPUSH`) [O]

| Off | Type | Field | Notes |
|---|---|---|---|
| +0x00 | far char* | title | NULL = no title line |
| +0x04 | far char* | body | wrapped by `Text_Render` |
| +0x08 | far char* | footer | NULL = none; a footer line is a click zone that sends Esc |
| +0x0C | i16 | n buttons | 0..3 (3 records at `g_dlgButtons` DS:bbbc; more would overrun DS:bc10..) |
| +0x0E | far (far char*)* | button labels | |
| +0x12 | far u16* | button keys | key i is both the press and the release code of button i's zone |
| +0x16 | i16 | focus | initially focused (inverted) button |
| +0x18 | i16 | alignment | 0 left, 1 centred, 2 right (other values: undefined x) |
| +0x1A, +0x1C | i16 | x, y | −1 = centred on 320×200 |
| +0x1E | i16 | body width | −1 = measured |
| +0x20 | i16 | body height | −1 = measured; else **value + 6** |
| +0x22 | i16 | n pictures | |
| +0x24 | far (far LPX*)* | pictures | drawn opaque (`Lpx_Draw`) |
| +0x28, +0x2C | far i16* | picture dx, dy | relative to (box x + 4, body y − 2) |
| +0x30 | u16 | flags | bit 0 compact (2 px less at the top), bit 1 Esc disabled |

No descriptor is stored in DGROUP: the helpers of §5.7 build them on the stack.

### 5.2 Layout: `Dlg_Layout(desc)` 3f56:0000 [O]

Called by `Dlg_Draw` with font 0 selected (lh = 10). Results go to DGROUP (`g_dlgX` DS:bc26, `g_dlgY`
bc24, `g_dlgW` bc22, `g_dlgH` bc20, `g_dlgInnerW` bc28, `g_dlgBodyX/Y/W` bc1e/bc1c/bc16,
`g_dlgTitleX/Y` bc1a/bc18, `g_dlgFooterX/Y` bc14/bc12, `g_dlgLineH` bc10, `g_dlgPicX/Y` bbba/bbb8).

```
(bw, bh) = Text_Measure(body, 280)
bodyW = desc.bodyW == -1 ? bw : desc.bodyW
h     = desc.bodyH == -1 ? bh : desc.bodyH + 6
W = bodyW;  H = h + 17;  titleY = bodyY = 6;  btnY = footerY = h + 11
if flags & 1: H, titleY, bodyY, btnY, footerY -= 2
if title:  tw = Text_Width(title);  W = max(W, tw);  H, bodyY, btnY, footerY += lh + 4
if footer: fw = Text_Width(footer); W = max(W, fw);  H += lh + 3;  footerY += 2
W += 12;  innerW = W
if n > 0: bW = max Text_Width(label i) + 20;  bH = lh + 2;  rowW = n*(bW + 10) + 10
          W = max(W, rowW);  H += bH + 8;  btnY += 4;  footerY += bH + 8
W += 10
x = desc.x == -1 ? half(320 - W) : desc.x;   y = desc.y == -1 ? half(200 - H) : desc.y
titleX = x + half(W - 2 - tw);  bodyX = x + half(W - 2 - bodyW);  footerX = x + half(W - 2 - fw)
titleY += y; bodyY += y; footerY += y
button row start: align 0: x + 10;  1: x + half(W - 2 - rowW) + 10;  2: x + W - 6 - n*(bW + 10)
button i = zone {shape 0, start + i*(bW+10), y + btnY, bW, bH, priority 5, next NULL,
                 press = release = key[i]} + label (+0x16) + enabled 1 (+0x1a)   (28 bytes)
if nPics > 0: picX = x + 4;  picY = bodyY - 2
```

The title, footer and body widths come from `Text_Width` / `Text_Measure` in font 0, so `FF 03`
escapes in the body change font 0's colour already during the layout (`Dlg_Draw` then sets it).

### 5.3 Draw recipe: `Dlg_Draw(desc, saveUnder)` 3f56:0389 [O, verified §21]

```
save g_drawColor; prev = Font_Select(0); Dlg_Layout(desc)
if saveUnder: Dlg_SaveUnder()              # Gfx_GetImage(x, y, W, H) into g_resBuffer
colour 15: FillRect(x, y, W, H)
fx, fy, fw, fh = x + 4, y + 4, W - 10, H - 10;  if flags & 1: fy -= 2; fh += 2
colour 0: twice { fw += 2; fh += 2; fy -= 1; fx -= 1; Gfx_FrameRect(fx, fy, fw, fh) }
           # frames (x+3, y+3, W-8, H-8) and (x+2, y+2, W-6, H-6); compact: y+1 / y+0
colour 7 (shadow): VLine(fx + fw + i, fy + 2, fh) for i = 0, 1; HLine(fx + 2, fy + fh + i, fw) for i = 0, 1
fx += 2; fw -= 4                              # separators: x + 4, width W - 10
if title: Font_SetColor(0); Font_DrawString(titleX, titleY, title); colour 0: HLine(x+4, titleY + lh + 1, W-10)
Font_SetColor(0); Text_DrawBox(bodyX, bodyY, bodyW, body)
if footer: colour 0: HLine(x+4, footerY - 2, W-10); Font_SetColor(0); Font_DrawString(footerX, footerY, footer)
for each picture i: Lpx_Draw(picX + dx[i], picY + dy[i], picture[i])     # after the body: covers text
if n > 0: Btn_Draw(button i) for each i; Btn_ToggleFocus(button[focus])
Dirty_Flush; restore g_drawColor; Font_Select(prev)        # font 0's colour stays 0
```

The 2-pixel shadow lies inside the box rectangle; the box's outer two pixels stay colour 15.

### 5.4 Buttons (4034) [O, verified §21]

`Btn_Draw(button by value)` 4034:0000 — a rounded rectangle:

```
colour 15: FillRect(x-1, y, w+2, h); FillRect(x, y-1, w, 1); FillRect(x, y+h, w, 1)
colour (+0x1a enabled ? 0 : 7): HLine(x+1, y, w-2); HLine(x+1, y+h-1, w-2); VLine(x, y+1, h-2); VLine(x+w-1, y+1, h-2)
    and one pixel each at (x+1, y+1), (x+1, y+h-2), (x+w-2, y+1), (x+w-2, y+h-2)
label: Font_DrawString(x + half(w - Text_Width(label)), y + half(h - Font_GetHeight()) + 1, label)
       in the selected font's current colour
```

* `Btn_ToggleFocus(button)` 4034:016b: `Gfx_XorRect` (^0x0F: 15 ↔ 0) of (x+1, y+2, w−2, h−4),
  (x+2, y+1, w−4, 1) and (x+2, y+h−2, w−4, 1), then `Dirty_Flush`: the focused button is inverted.
* `Btn_Flash(button)` 4034:01d6: 4 × {`Btn_ToggleFocus`; `Delay(4)`} — 16 ticks plus drawing, ending
  in the starting state. `Delay` runs `Input_GetCode` (global keys act, codes are lost).
* `Btn_AddZone` / `Btn_RemoveZone` (4034:02e4 / 02f4): `Zones_AddLocal` / `Zones_RemoveLocal` of the
  record (its first 22 bytes are a zone).

**Picture buttons** (sign-in, workshop) are 0x20-byte records: a 22-byte zone, +0x16 bundle id,
+0x18 enabled, +0x1A/+0x1C/+0x1E picture-cache indices of the bundle's first three pictures
(normal, pressed, disabled [I order from use]):

| Function | Behaviour |
|---|---|
| `PicBtn_Load(x, y, btn)` 4034:0201 | `PicCache_LoadBundle(btn+0x16, btn+0x1a)`; enabled = 1; zone x, y; w, h from the first picture (+6/+8) |
| `PicBtn_Draw(btn)` 4034:027c | `Lpx_DrawKeyed(x, y, enabled ? pic[+0x1a] : pic[+0x1e], 0)` (no flush) |
| `PicBtn_DrawPressed` 0304 / `PicBtn_DrawNormal` 032f | keyed picture +0x1C / +0x1A, `Dirty_Flush` |
| `PicBtn_Erase(btn)` 02c4 | `Gfx_RestoreRect(x, y, w, h)` (from the back page) |
| `PicBtn_Free(btn)` 026a | `PicCache_ReleaseBundle(btn+0x16)` |

### 5.5 Dialog zones [O]

`Dlg_AddZones(desc)` 3f56:05d7 adds each button record with `Zones_AddLocal`, and, when the descriptor
has a footer, the footer zone `g_dlgFooterZone` DS:3dd4 = {rect, x = box x, y = footerY, w = box W,
h 10, priority 5, press = release = 0x1B}: **a click on the footer line is Esc**. `Dlg_RemoveZones`
(3f56:061f) removes the same zones.

### 5.6 Modal loop: `Dlg_RunModal(desc, saveUnder, restoreUnder)` 3f56:0655 [O]

Near, callee pops 0x36 bytes. Entry and exit as §2.2. Then:

```
Dlg_Draw(desc, saveUnder); Zones_Push(); Dlg_AddZones(desc); Input_Flush()
sel = desc.focus; done = 0
while !done:
    code = Input_GetCode()                         # busy poll
    switch code (CS:08da):
      0x0000: nothing
      0x000D Enter: if n > 0: Btn_Flash(button[sel]);  done        (n = 0: returns sel = focus)
      0x001B Esc:   if !(flags & 2): sel = -1; done
      0x014B Left:  if n > 1: toggle(sel); sel = sel - 1, wrapping to n - 1; toggle(sel)
      0x014D Right: if n > 1: toggle(sel); sel = sel + 1, wrapping to 0; toggle(sel)
      other:  if code < 0x80: code = tolower(code)
              for each button i (key k = +0x10):
                  if code == k | 0x4000 or code == k | 0x8000:   # pressed / held over the button
                      move the focus to i (toggle off sel, toggle on i); next poll
                  elif code == k:                                # key typed, or released over it
                      if sel != i: toggle off sel; sel = i
                      Btn_Flash(button[i]); done
if restoreUnder: Dlg_RestoreUnder()      # Gfx_PutImage at (x, y) from the buffer, Dirty_Flush
restore hook, play mode; Snd_Resume; Dlg_RemoveZones; Zones_Pop; Input_Flush;
Mouse_ForceDefault(previous); Font_Select(previous); g_ticks = g_savedTicks
return sel
```

* When the selection moves by key/release to an unfocused button, that button is flashed without
  being toggled on first, so it ends unfocused (invisible: the box closes) [O 3f56:081f..0851].
* Letter keys (`y`, `n`, …) are lower case in the key arrays, so upper-case typing works.
* `Dlg_Show(desc)` 3f56:08ee = `Dlg_RunModal(desc, 1, 1)`.

### 5.7 Prebuilt boxes [O]

| Function | Descriptor | Returns |
|---|---|---|
| `Dlg_MessageBox({title, body, footer})` 3f56:0b1c (12 bytes by value) | n 0, align 1, centred, measured, flags 0 | 0 Enter, −1 Esc / footer click |
| `Dlg_YesNo({title, body, defYes})` 3f56:0bb6 (10 bytes by value) | footer `s_footerGoBack` DS:3e38 ("Esc to Go Back" with the Esc key-cap), labels Yes/No (DS:3dea), keys `y`/`n` (DS:3e16), focus = defYes ? 0 : 1, align 2 (right) | 1 Yes, 0 No, −1 Esc / footer |
| `Dlg_AskChimps()` 3f56:0c5e | body = member 0 of bundle 0x1067 (private copy), no title / footer, labels With Chimps / Without Chimps (DS:3e2c), keys `y`/`n`, focus 0, align 1, **flags 2 (Esc disabled)** | 1 With, 0 Without |
| `Dlg_MessageRes(id)` 3f56:0d2a | copy of bundle id = {member 0 title, member 1 body, member 2 footer flag}: title = NULL when member 0's first word is 0 (an `I 0` member), else its string; footer = DS:3e38 ("Esc to Go Back") for flag 0, `s_footerGoOn` DS:3e45 ("Enter to Go On") for flag 1, **an uninitialised stack pointer** for any other value | via `Dlg_MessageBox` |
| `Dlg_YesNoRes(id)` 4106:006d | `MsgDesc_LoadRes(id)` (4106:0000): copy of bundle {S title, S body, I defYes} → `Dlg_YesNo` | 1 / 0 / −1 |

Message bundles in the data [O]: Yes/No 0x1064 Save & Exit (default No), 0x1065 Sound and Music
(default Yes), 0x1066 New Player Hints (No), 0x106A Remove a player (No), 0x106B Return to
Technology Center (No), 0x106C Start New Player (No); messages {I 0, S, I flag} 0x1068 duplicate name
(flag 0), 0x1069 list full, 0x106D, 0x106E (flag 1). 0x106F (disk full, §12.1) has four members
{I, S, S, I}: `Dlg_MessageRes` reads the second **string** as the footer flag, so its footer is an
uninitialised pointer [O bug]. [R] Use member 3 (0 → "Esc to Go Back").

### 5.8 Help pager: `Dlg_RunPager(n, title, pages, picCounts, pics, dxs, dys)` 3f56:0907 [O]

Fixed geometry for every page: x 19, y 14, body width 260, body height 120 (box 282 × 177 with a title
and a button row), footer none, flags 0. Buttons by page:

| Page | Labels (DS) | Keys | Focus | Align |
|---|---|---|---|---|
| only page | Done (3dfe) | PgDn 0x151 (3e20) | 0 | right |
| first of several | See Next Page (3e02) | PgDn (3e22) | 0 | right |
| middle | Go Back a Page, See Next Page (3e06) | PgUp 0x149, PgDn (3e24) | 1 | centred |
| last | Go Back a Page, Done (3e0e) | PgUp, PgDn (3e28) | 1 | centred |

```
one page: return Dlg_Show(desc) >= 0 ? 1 : 0
several:  page = 0; save = 1
          while 0 <= page < n:
              r = Dlg_RunModal(desc(page), save, restoreUnder = 0); save = 0
              r == -1: page = -1           (Esc)
              r == 0:  page = page == 0 ? 1 : page - 1
              r == 1:  page += 1           (See Next Page / Done)
          Dlg_RestoreUnder(); return page < 0 ? 0 : 1
```

Each page is a separate modal loop (time frozen per page). Pictures: picCounts NULL → none; else
`picCounts[page]` pictures from `pics[page]` at `dxs[page][i]`, `dys[page][i]`.

### 5.9 Who uses the dialogs [O]

`Dlg_Draw`: `Dlg_RunModal`, `Menu_DrawDropDown`. `Dlg_MessageBox`: `Hint_Show`, `Result_RankUpMessage`
(2fcb:01dd). `Dlg_MessageRes`: `Players_WriteError`, `Roster_UpdateTyped`, `SignIn_HandleCode`,
`State_03_Hub` (0x106D, 0x106E). `Dlg_YesNoRes`: the menu commands, `SignIn_HandleCode` (0x106A).
`Dlg_AskChimps`: `State_02_SignIn`. `Dlg_RunPager`: `Help_ShowTopic`.

---

## 6. Menu bar and commands (ovl31 40a4, ovl32 4106, ovl33 411d)

### 6.1 Data (read from DGROUP) [O]

`MenuBarDef` = {u16 count, far `MenuDef[count]`}: `g_menuBar` DS:4053 = {3, DS:4017}. `MenuDef` (0x14
bytes, the layout of TM Windows' DS:13bf / 1383; `re/docs/dgroup_map.md` §4.12):

| Off | Field | G&G use |
|---|---|---|
| +0x00 | far char* title | not used for drawing (a padded string; the bar shows pictures) |
| +0x04 | i16 title width | title zone width − 2 |
| +0x06 | i16 item count | ≤ 8 (10 zones per drop-down) |
| +0x08 | far char* items | LF-separated, drawn as the drop-down body |
| +0x0C | far (far fn)* handlers | far code pointers to stub entries, `int far handler(void)`, NULL allowed |
| +0x10 | i16 drop-down width | body wrap width |
| +0x12 | i16 title x | bar position |

Record i is menu index i: 0 Help (DS:4017, x 108), 1 File (DS:402b, x 0), 2 Options (DS:403f, x 54);
F1/Alt+H, F2/Alt+F, F3/Alt+O (`code_runtime.md` §9.3). `g_menuBarNone` DS:405a = {0, NULL}.
`s_unusedFileItems` DS:3f2b (another title's File items) and the extra handler words around DS:3f58
are never referenced.

### 6.2 Install and remove: `Menu_SetMenuBar(bar)` 40a4:0000 [O]

* bar ≠ NULL: `g_menuBarCur` DS:bc70 = bar; `Menu_DrawBar(-1)`; `g_menuHandler` = 1e94:0025 (stub entry
  of `Menu_RunDropDown`); for each record i: `Zone_Init(&g_menuTitleZones[i], x = +0x12, y 0,
  w = +0x04 + 2, h 9, priority 8, press 0, release 0x13B + i)`, `Zones_AddGlobal` — the title zones
  answer on **release**, and a release over a title is the same code as its F key.
* bar = NULL: hook cleared; title zones removed (`Zones_RemoveGlobal`); g_menuBarCur = DS:405a; colour 0
  `FillRect(0, 0, 320, 9)` (the strip goes black).
* Installed by `State_05_EnterRoom` (12e0:097c) and `State_09_Puzzle` (38e2:0018); removed by
  `State_08_LeaveRoom` (12e0:09b5), `Building_Free` (12e0:05eb) and the puzzle exit (38e2:009c). The
  bar therefore exists in the warehouse rooms, the workshop and the puzzles, not in the attract,
  sign-in, hub, races or results [O].
* Title zones overlap by 2 px (x 54..55 File/Options, 108..109 Options/Help); with equal priority the
  zone added last wins, which is Options in both overlaps [I from `code_runtime.md` §8.3].

### 6.3 Bar drawing: `Menu_DrawBar(sel)` 40a4:04db [O, verified]

`Res_Read(1)` (bundle of 6 pictures 53 × 9); colour 0 `FillRect(0, 0, 320, 9)`; for i = count−1 down
to 0: `Lpx_DrawKeyed(record[i].x, 0, member (i == sel ? i + 3 : i), 0)`; `Dirty_Flush`. Members 0..2 are
the normal titles in record order (Help, File, Options), 3..5 the highlighted ones.

### 6.4 Drop-down: `Menu_RunDropDown(i)` 40a4:020b [O, verified]

Entered from `Input_GetCode` through the hook with the menu index. Ignored when i < 0 or ≥ count.

```
clear hook; Snd_Pause; save play mode; prevForce = Mouse_ForceDefault(1); play mode 0; Input_Flush
if g_menuNesting == 0: Zones_Push;  g_menuNesting += 1
open(i):
    Menu_DrawBar(i); Menu_DrawDropDown(record i)          # 40a4:056c, below
    Menu_AddItemZones(record i)                           # 40a4:00ee, below
    row = 0; highlight(row)
    loop: prev = row; code = Input_GetCode()              # busy poll
      table CS:0493 (18 codes):
        0x0000            nothing
        0x000D Enter      choose(row)
        0x001B Esc        close (i = -1)
        0x0FCF            close (no zone or key produces it)
        0x013B..0x0144 (F1..F10): j = code - 0x13B; if j valid and j != i: switch to menu j
        0x0148 Up         row - 1, wrapping to count - 1
        0x0150 Down       row + 1, wrapping to 0
        0x014B Left       i - 1, wrapping to count - 1: switch
        0x014D Right      i + 1, wrapping to 0: switch
        other: m = code & 0xFFF; if m >= 0xFD0:
                   pressed/held (0x4000/0x8000): row = m - 0xFD0      (highlight follows the mouse)
                   released: choose(m - 0xFD0)
               (0x44F, a click outside, and everything else: ignored)
      if not leaving and row != prev: XOR row prev, XOR row, Dirty_Flush each
    choose(r): Dlg_RestoreUnder(); Menu_DrawBar(-1); Menu_RemoveItemZones
               res = handlers[r] ? handlers[r]() : 1
               res == 0: close;  res != 0: open(i) again (same menu)
    switch / close: if the box was not restored yet: Dlg_RestoreUnder(); Menu_RemoveItemZones
    if not closing: open(new i)
close: Menu_DrawBar(-1); g_menuHandler = Menu_RunDropDown; restore play mode; Input_Flush
       g_menuNesting -= 1; if <= 0: Zones_Pop, g_menuNesting = 0
       Mouse_ForceDefault(prevForce); Snd_Resume
```

* `Menu_DrawDropDown(rec)` 40a4:056c: `Dlg_Draw(desc, saveUnder 1)` with title NULL, body = items,
  footer DS:3e38 ("Esc to Go Back"), n 0, align 1, x = +0x12, y 9, body width = +0x10, body height =
  count·10 − 10, flags 1 (compact). Hence box H = count·10 + 24, body at (x + half(W − 2 − dropW), 13),
  item r on the line at y 13 + 10r [O, verified on the File menu: box (0, 9) 242 × 54].
* Highlight: `Gfx_XorRect(x + 4, 12 + 10r, g_dlgInnerW, 10)` (^0x0F turns the black text white on a
  black bar) [O, verified].
* `Menu_AddItemZones(rec)` 40a4:00ee: `Mem_Alloc(220)` → `g_menuItemZones` DS:bc2a; zone 0 = (0, 12,
  320, 188) priority 20, release **0x44F**; item r = (x + 4, 13 + 10r, g_dlgInnerW, 10) priority 21,
  press = release = 0xFD0 + r; a last zone (x + 4, count·10 + 15, g_dlgInnerW, 11) priority 21,
  release 0x1B (the footer: a click = Esc = close). `Menu_RemoveItemZones` (01c7) removes count + 2
  zones and frees them.
* The outside zone only swallows clicks: the menu does **not** close on a click outside (the code
  0x44F is ignored), and a click on the open menu's own title does nothing; a click on another title
  switches menus (rows 9..11 between the bar and the zone have no zone). Time is **not** frozen.
* A key code that `Input_GetCode` turns into a global action keeps working (Ctrl+S; Esc calls
  `g_escHandler` too, e.g. `Hud_EscHook` in the warehouse).

### 6.5 Items and handlers [O]

| Menu | Item | Handler (stub → function) | Action | Returns |
|---|---|---|---|---|
| File | Start New Player | 1e97:002a `Cmd_StartNewPlayer` 4106:00d7 | Yes/No 0x106C; Yes: `g_menuRequest` DS:130e = 2 | 0 |
| File | Return to Technology Center | 1e97:0039 `Cmd_ReturnToCenter` 4106:014f | Yes/No 0x106B; Yes: DS:130e = 1 | 0 |
| File | Save & Exit | 1e97:0025 `Cmd_SaveAndExit` 4106:00b2 | Yes/No 0x1064; Yes: `Players_SaveCurrent`, `exit(0)` | the Yes/No result: No 0 closes the menu, **Esc −1 reopens the File menu** |
| Options | Sound & Music | 1e97:0034 `Cmd_ToggleSound` 4106:011e | Yes/No 0x1065 (default Yes): Yes → `g_soundOff` DS:45cc = 0, No → 1, Esc → unchanged; then DS:1ad4 = DS:45cc; `SndRes_ApplySoundOption` | 0 |
| Options | New Player Hints | 1e97:002f `Cmd_ToggleHints` 4106:00f8 | Yes/No 0x1066 ("hints off?"): Yes → `g_hintsOff` DS:45a2 = 1, No → 0, Esc → unchanged | 0 |
| Help | About the Game | 1e9b:0020 `Help_AboutGame` 411d:01e0 | `Help_ShowTopic(0x105D)` | pager result: 1 (Done) **reopens the Help menu**, 0 (Esc) closes |
| Help | Moving Around | 1e9b:002f `Help_MovingAround` 411d:0204 | topic 0x1060 | same |
| Help | Building Vehicles | 1e9b:0025 `Help_BuildingVehicles` 411d:01ec | topic 0x105E | same |
| Help | Finding Parts | 1e9b:002a `Help_FindingParts` 411d:01f8 | topic 0x105F | same |
| Help | Solving Puzzles | 1e9b:0039 `Help_SolvingPuzzles` 411d:021c | topic 0x1062 | same |
| Help | Challenge Races | 1e9b:0034 `Help_ChallengeRaces` 411d:0210 | topic 0x1061 | same |

`g_menuRequest` reaches the game as the input code 0x17A2 (1) or 0x17A3 (2) from the `Input_GetCode`
call that ran the menu hook (`code_runtime.md` §9.3); the active loop answers with state 16 and
`g_leaveRequest` DS:455a = 3 or 2 (`code_world.md`; ARCHITECTURE §2.11).

### 6.6 Help topics: `Help_ShowTopic(id)` 411d:0000 [O]

Private copy of bundle id, laid out as:

```
member 0: I  page count n (≤ 10)
member 1: S  title
per page p (in order): I k_p  picture count (≤ 4)
                       if k_p > 0: I dx array, I dy array, k_p × G pictures
then n × H  page texts
```

It builds the arrays on its stack and calls `Dlg_RunPager(n, title, texts, counts, pics, dxs, dys)`;
the pager result is returned (§6.5). Shipped topics: 0x105D 4 pages (pictures on pages 1 and 3),
0x105E 2 pages (one picture), 0x105F, 0x1060, 0x1061 2 pages, 0x1062 1 page [O bundle shapes].
Page texts use `FF 03` colour escapes (`fmt_data.md` §6).

---

## 7. Hot keys and options (summary) [O]

| Key / input | Where | Effect |
|---|---|---|
| Ctrl+S (0x13) | every `Input_GetCode` (also in dialogs, menus, the attract) | `Snd_ToggleOption` 1cdd:05c8: DS:1ad4 ^= 1, DS:45cc ^= 1, apply (`code_sound.md` §12.2) |
| Alt+H/F/O, F1/F2/F3, click on a title | wherever the bar is installed and no modal loop runs | `Menu_RunDropDown(0/1/2)` |
| Esc | hub, rooms, workshop, puzzles, post-race | `Cmd_SaveAndExit` (Yes/No) through each loop or `g_escHandler` |
| Esc | sign-in | `Cmd_SaveAndExit` with saving disabled (§11.4) |
| Esc | attract | quit at once |
| Enter / Esc / Left / Right / letters | dialogs | §5.6 |

Options [O]:

* **Sound**: the live switch DS:1ad4 (`g_soundMuted`) and the player's option DS:45cc (`g_soundOff`,
  record +0x6A). The sign-in of an existing player copies DS:45cc into DS:1ad4 and applies it
  (2caa:136a). **A new player's record always has DS:45cc = 0**: `Player_InitRecord` (2b5a:0000) writes
  DS:45cc = DS:1ad4 and then overwrites the whole record (memcpy at 2b5a:0042), and the sign-in does
  not apply it. If the sound was switched off before (by the previous player or Ctrl+S in the
  attract), it stays off with DS:45cc = 0; the next Ctrl+S then inverts the two flags (sound on,
  option "off") [O code; I effect]. `code_sound.md` §12.2 says the new record copies DS:1ad4: that
  store is dead.
* **New Player Hints**: DS:45a2 (record +0x40), §13.
* **With / Without Chimps**: DS:4578 (record +0x16), asked once for a new player (§11.8); no other
  writer, so it cannot be changed later.
* Command-line switches: `code_runtime.md` §2.2.

---

## 8. Scrolling text viewer (ovl34 4140) [O, verified §21]

`TextView_Run(far char *text)` 4140:0667, the Hint button of every puzzle (`Puzzle_ShowHintText`
38e2:0286). A panel at the left of the screen with the text in the small font, two arrow buttons and,
for long texts, a scroll bar.

### 8.1 Set-up [O 4140:066f..078a]

```
save and clear g_menuHandler; Zones_Push; Mouse_ForceDefault(1); Mouse_SetHotSpot(NULL, 0, 0)
pics = Res_LoadCopy(0x11D5)               # arrows: 0 up, 1 down, 2 up pressed, 3 down pressed
Res_Read(0x11D4)                          # the 129x138 panel, stays at the start of g_resBuffer
Gfx_GetImage(0, 28, 129, 138, g_resBuffer + 30000)        # save-under
g_drawColor = 0; Font_Select(1); Font_SetColor(0)
zones: g_viewCloseZone DS:40a2, g_viewTextZone 408c, g_viewUpZone 4060, g_viewDownZone 4076
g_viewScrollMax DS:bc78 = max(1, TextView_CountLines(text) - 10)
if g_viewScrollMax != 1: zones g_viewPageUpZone 40b8, g_viewThumbZone 40ce, g_viewPageDownZone 40e4
g_viewThumbStep DS:bc74 = 104 / g_viewScrollMax;  g_viewThumbY DS:bc76 = 45;  line = 0
TextView_Redraw(pics, text, up 0, down 0, line)
```

`TextView_Redraw` (4140:0605): `TextView_DrawPanel` (panel keyed at (0, 28), up arrow at (117, 29),
down arrow at (117, 149), pressed or not), `TextView_DrawText(text, line)`, `TextView_DrawBar` when
g_viewScrollMax ≠ 1 (track: colour 15 `FillRect(118, 45, 9, 104)`; thumb: `FillRect(118, thumbY, 9,
11)` colour 15 and `Gfx_FrameRect` colour 0; then `TextView_UpdateBarZones`), `Dirty_Flush`.

### 8.2 The viewer's own text layout (`TextView_DrawText` 4140:027a, `TextView_CountLines` 03d7) [O]

Not `Text_Render`: a simpler wrap with its own constants.

```
x = 3; y = 31; line = 0; atWordStart = 0
per byte c:
  FF:    skip to and including the next FF          (all escapes, FF 03 colours included, are ignored)
  0:     end
  LF/CR: line += 1; if line > first: y += lh; x = 3; atWordStart = 1
  ' ':   x += Font_CharWidth(' ') (3); atWordStart = 1
  other: if atWordStart:
             ww = TextView_WordWidth(text + i)       # Σ Font_CharWidth up to space/TAB/CR/LF/NUL
             if x + ww >= 116 and ww < 108:          # wrap (the byte is examined again)
                 line += 1; if line > first: y += lh; x = 3; continue
         atWordStart = 0
         if Font_CharWidth(c) != 0 and TextView_GlyphVisible(x, y, c):
             if line >= first: Font_DrawChar(x, y, c)
             x += Font_CharWidth(c)                  # an invisible glyph does not advance x
```

`TextView_GlyphVisible`: x ≥ 3, y ≥ 31, x + width < 116, y + lh − 1 < 163 (13 visible lines, y 31..151).
Lines before `first` are laid out at y 31 without drawing. `TextView_CountLines` runs the same layout
and returns the number of line breaks (lines − 1).

### 8.3 Input loop [O, switch CS:0a8c on `Input_StripHeld(Input_GetCode())`]

| Code | Zone | Effect |
|---|---|---|
| 0x1B | Esc | close |
| 0x51F | `g_viewCloseZone` (0,0,320,200) priority 501 | close: a click anywhere outside the viewer's other zones |
| 0x51E | `g_viewTextZone` (0,28,129,138) priority 502 | ignored (shields the text) |
| 0x51A | up arrow (117,29) 11×16 press | pressed picture; `Timer_Set(6, 146)`; while the left button is held (`g_mouseButtons` DS:0a0c ≠ 0) and the pointer is on the arrow: when `Timer_Expired(6)`: line −1, thumb up, redraw, `Timer_Set(6, 29)`; then the normal picture |
| 0x51B | up arrow release | `TextView_FlashArrow` (pressed 7 ticks: `Timer_Set(6, 7)`, `Timer_Wait(6)`), line −1, thumb up, redraw |
| 0x51C / 0x51D | down arrow (117,149) press / release | the same downwards |
| 0x520 | track above the thumb | line −10, thumb up 10 steps, redraw |
| 0x522 | track below the thumb | line +10, thumb down 10 steps |
| 0x521 | thumb | drag: while held, if the pointer is in the track below / above the thumb: one step and one line, redraw, and the pointer is warped onto the thumb (`TextView_WarpMouse(122, thumbY + 10)` / `(122, thumbY)`, INT 33h fn 4) |

Exit: `Zones_ClearLocal`, free the arrows, `Gfx_PutImage(0, 28, saved)`, `Dirty_Flush`,
`Mouse_ForceDefault(0)` (not the previous value), `Zones_Pop`, hook restored. **Time is not frozen**
(no write of `g_ticks`; timer slot 6 only) [O]. The cursor's hot spot stays (0, 0).

Quirks [O]: the line (`TextView_ScrollBy` clamps to 0..g_viewScrollMax−1) and the thumb
(`TextView_ThumbUp/Down` step by `g_viewThumbStep` and snap to 45 or 138 within one step of the end)
move independently, so they can disagree near the ends; an unscrollable text (≤ 11 line breaks) has
no bar but the arrows still step the (clamped) line.

---

## 9. Scroll bar (ovl30 406a) [O]

A vertical scroll bar record (20 bytes): +0 x, +2 y, +4 w, +6 h, +8 pos (first visible row), +0xA max,
+0xC page, +0xE u32 next repeat tick, +0x12 thumb y. The only one is `g_rosterScrollBar` DS:20dc =
{244, 26, 11, 109, …, page 8}.

| Function | Behaviour |
|---|---|
| `ScrollBar_Draw(sb)` 406a:003f | `Res_Read(0x102E)`; colour 0xC8 `FillRect(x, y, w, h)`; colour 0xF1 `Gfx_FrameRect`; arrows keyed at (x, y) and (x, y + h − 11): members 1/2, or 3/4 when max ≤ 0; if max > 0: thumbY = y + (h − 31)·pos / max + 10 (+0x12), `ScrollBar_DrawThumb(x, thumbY)` |
| `ScrollBar_DrawThumb(x, y)` 406a:0000 | colour 0xF1 `Gfx_FrameRect(x, y, 11, 11)`, colour 0xCB `FillRect(x+1, y+1, 9, 9)` |
| `ScrollBar_SetPos(sb, p)` 406a:013e | p clamped to 0..max; if it changed: pos = p, draw, `Dirty_Flush` |
| `ScrollBar_SetRange(sb, m)` 406a:017f | m = max(0, m); if it changed: max = m, pos = min(pos, m), draw, flush |
| `ScrollBar_HandleClick(sb, code)` 406a:01b6 | nothing when max ≤ 0. First poll of a press (code & 0x4000): act now; later polls (held or release): act only when `g_ticks` > next. Act by pointer y: above y + 10 → pos − 1; below y + h − 10 → pos + 1; above the thumb → pos − page; below thumbY + 11 → pos + page; on the thumb → drag (below). After acting: next = tick + 27 |

Thumb drag: offset = pointerY − thumbY; loop `Input_GetCode` while the code has bit 15 (held over the
zone): when the pointer moved (and is within the bar's x range) the thumb is drawn at pointerY − offset
clamped to [y + 9, y + h − 19] after `Gfx_RestoreRect` of the whole bar from the back page. At the end
pos = ((thumbY − (y + 10))·max + half(h − 31)) / (h − 31), via `ScrollBar_SetPos` (forced redraw).

---

## 10. Attract sequence (state 1, ovl06 2c11)

### 10.1 `State_01_Attract` 2c11:0913 [O]

```
Mouse_Hide()                                   # the cursor is not drawn on these pages
g_effectBank DS:7a44 = SndRes_LoadEffectBank(0x1022)     # 14 effects
g_musicBank DS:9dc4 = SndRes_LoadMusicBank(0x1020); SndRes_PlayMusic(it, section 0, loop 1)
Zones_AddLocal(DS:0a3a)                        # whole screen, priority 5, release 0x0D: a click = Enter
g_leaveRequest DS:455a = 2
Seq_Run(g_attractSequence DS:20a8, &local = 1)
Pal_FadeOut; Zones_ClearLocal; SndRes_FreeMusicBank; SndRes_FreeEffectBank; Mouse_Show
return g_leaveRequest                          # 2 → sign-in; -1 → main Seq_Run stops → exit(0)
```

Sub-sequence DS:20a8 (far pointers to 1e28 stub entries): 1 `Attract_TitleAnim`, 2
`Attract_AfterTitle`, 3..8 `Attract_StoryPage` ×6, 9 `Attract_AfterStory`, 10 `Attract_BeforePrompt`,
11 `Attract_SignInPrompt`, 12 NULL. Every step returns 0 (next), −1 (stop the attract: Space → sign-in;
Esc → also sets DS:455a = −1 → quit) or 1 (restart at step 1).

### 10.2 `Attract_PlayAnim(anim, fade, flags, …)` 2c11:0000 [O]

`anim` = {u16 channel count, far table of far channel scripts}. A script is a u16 list: n > 0 =
start screen animation n (`ScreenAnim_*`, resources 0x1005.., `code_world.md` §19.1), −2 t = wait t
frames, −1 = end. Every positive id occurring in any script is loaded once per occurrence
(`ScreenAnim_Load`) before the first frame.

```
Sprites_Reset
if flags & 2: Timer_Set(1, vararg0 * 146)        # seconds
if flags & 4: watch = next vararg                # channel index
frame = 0; every channel ready at position 0
loop:
    Timer_Set(0, 4)
    for each ready channel c: op = script[c][pos]:
        n > 0:  the first loaded instance whose id slot still holds n and that is not in the
                active list (+0x24 = 0): ScreenAnim_StartAt(inst, c)
        -2:     until[c] = frame + script[c][pos + 1]
        (-1: stays)
    ScreenAnim_Frame; Dirty_Flush; frame += 1; Timer_Wait(0)
    first frame only: fade 1 → Pal_FadeIn, fade 2 → Pal_FadeFromSaved
    for each channel: waiting and until < frame → pos += 2, ready
                      running and the instance's +8 != 0 (finished or holding) → its id slot = -1
                      (never reused), pos += 1, ready
    if flags & 2 and Timer_Expired(1): result 0, leave
    if flags & 8: result = Input_GetCode(); Enter, Esc or Space → leave   (other codes are lost)
    if flags & 4 and script[watch][pos[watch]] == -1: leave
leave: ScreenAnim_Free of every loaded instance; return result
```

A wait of t frames releases the channel after t + 1 frames. The four descriptors: DS:2090 (title,
2 channels: 0x1005, 0x1007 — both loop forever), DS:2096 (3: wait 50, 0x1015 / 0x1005 / 0x1007),
DS:209c (5 channels with waits of 20..577 frames, 0x100B..0x1013), DS:20a2 (3: wait 20, 0x1017 /
0x1005 / 0x1007) [O bytes]. The attract animations use no random step (ops −34/−35), only play/stop
effect steps (−36/−37) on bank 0x1022 [O data, `seqattract.py`].

### 10.3 The steps [O]

| Step | Function | Screen | Wait / end | Keys |
|---|---|---|---|---|
| 1 | `Attract_TitleAnim` 2c11:043f | `Screen_LoadPicture(0x1002, 0x1000)` + `Lpx_Draw(0, 0)`; `Screen_SaveAll`; `Attract_DrawPictureSet(0x1001)` (bundle of `{I x, y}`, `G` pairs, ended by an `I` member whose first byte is 0xFF); title pictures = copy of bundle 0x1028 | phase 1: `PlayAnim(DS:2090, fade 1, flags 10, 1 s)`; on time-out: effect 12 (priority 4), title picture 0 keyed at (59, 9), phase 2 (3 s); on time-out: pictures 1..3 at (52, 51), (99, 138), (0, 181), effect 13, phase 3 (5 s); time-out → next | Enter → next (2); Space → −1; Esc → quit |
| 2 | `Attract_AfterTitle` 2c11:05cf | `Screen_RestoreAll` (the background without the title art); `Pal_Save`; `Pal_LoadRes(0x1004)` (shadow only) | `PlayAnim(DS:2096, fade 2 = from the saved palette, flags 12, channel 0)`; then `Pal_FadeOut`, `SndRes_StopAllEffects`, `g_storyTextId` DS:a04e = 0x1019 | as above |
| 3..8 | `Attract_StoryPage` 2c11:0630 | first page only: `Screen_LoadPicture(0x1009, 0x100A)`, `Lpx_Draw(0, 0)`, `Screen_SaveAll`, the line 0x1024 in font 0, colour 15, **spacing patched to 1** (record +6 via `Font_GetCurrent`), at (160 − half(Text_Width), 190), spacing back to 2. Every page: colour 0xA3 `FillRect(57, 38, 177, 53)`; font 1 colour 0 `Text_DrawBox(57, 38, 177, text DS:a04e)`; first page `Screen_FadeOutIn`, others `Dirty_Flush` | `Timer_Set(1, 0x111C)`; busy poll until a key or `Timer_Expired(1)`; then DS:a04e += 1 | Enter → next; Space → −1; Esc → −1 + quit; time-out → next (it also overrides a key read in the same poll: the result becomes 0, an Esc's DS:455a = −1 stays) |
| 9 | `Attract_AfterStory` 2c11:077f | `Gfx_RestoreRect(57, 38, 177, 53)` and `(0, 190, 320, 10)` (erase box and line) | `PlayAnim(DS:209c, fade 1 (no effect: not faded), 12, channel 2)`; `Pal_FadeOut` | Enter / Space / Esc as step 2 |
| 10 | `Attract_BeforePrompt` 2c11:07e5 | `Screen_LoadPicture(0x1002, 0x1004)`, draw, `Screen_SaveAll` | `PlayAnim(DS:20a2, fade 1, 12, channel 0)`; `Pal_FadeOut` | same |
| 11 | `Attract_SignInPrompt` 2c11:084c | `Screen_LoadPicture(0x1029, 0x102A)` (the sign-in background), picture 0x1026 keyed at (93, 33), text 0x1025 in font 0 colour 15 at (74, 121); `Screen_FadeOutIn` | `Timer_Set(1, 0x111C)`; busy poll; time-out returns **1 → restart at step 1** (also over a key read in the same poll); `Pal_FadeOut` | Enter or Space → 0 → step 12 NULL → sign-in; Esc → quit |

Font state left behind [O]: `Font_SetColor` acts on the selected font. Story page 1 sets font 0's
colour to 15 (font 0 is selected since start-up) and then selects font 1 with colour 0; step 11's
`Font_SetColor(15)` runs while font 1 is still selected (font 1's colour becomes 15) and then
selects font 0, whose colour is still 15 from page 1, so the prompt is white. Font 0's spacing is
back to 2.

---

## 11. Sign-in and roster (state 2, ovl07 2caa)

### 11.1 State [O]

| DS | Name | Type | Meaning |
|---|---|---|---|
| a075 | `g_roster` | far | private copy of the file's first 0x8A0 bytes: header + 100 index entries (§12.2) |
| a079 | `g_rosterIndex` | i16 | selected entry (later: the signed-in player's entry, written by every save) |
| a054 | `g_typedName` | char[16] | the name being typed; 0x1F = placeholder for an empty new entry |
| a071 | `g_typedLen` | i16 | typed characters; −1 right after entry |
| 20dc | `g_rosterScrollBar` | rec | §9; +8 (DS:20e4) = first visible row |
| 20f0 / 2110 / 2130 | `g_btnCancel` / `g_btnNewPlayer` / `g_btnStartGame` | picture buttons | +0x18 (DS:2108 / 2128 / 2148) = enabled |
| 2150, 2166 | `g_rosterZone`, `g_rosterScrollZone` | zones | list (62, 46) 180×88 priority 4 release 0x700; bar (244, 26) 11×109 priority 5 press = release 0x701 |
| 1f7a | `g_saveDisabled` | i16 | §11.4 |
| a065, a067, a069, a06d, a073 | (vestiges) | | written, never read: an arrow-key throttle that never throttles, a double-click time stamp pair, a "clicked" flag |

Index entry points: ≥ 0 a real player; **−1 (0xFFFFFFFF) = an entry being typed**; **−2 (0xFFFFFFFE)
= an entry created by New Player** (its name sorts by the placeholder 0x1F, i.e. first) [O].

### 11.2 Screen [O, verified §21]

`SignIn_ShowScreen` (2caa:01b3): `Font_Select(1)`; `SignIn_DrawScreen` (2caa:016e): `Screen_LoadPicture
(0x1029, 0x102A)`, `Lpx_Draw(0, 0)`, the title picture 0x1027 keyed at (65, 28), `Screen_SaveAll`
(the back page is the empty list); `Screen_FadeOutIn`. Buttons (`SignIn_LoadButtons` 2caa:1382):
Cancel bundle 0x102D at (250, 172) 49×20, New Player 0x102C at (197, 170), Start Game 0x102B at
(146, 170) (42×24, sizes from the pictures).

Rows (`SignIn_DrawList`, §11.7): 8 visible, row k at y 46 + 11k, cell (62, y, 180, 11); name at
(64, y + 1); points right-aligned at x 164 (`Str_FormatThousands`, thousands commas) and the rank
name at x 169 (`Rank_FromPoints`, names DS:1f7c), in the name's colour; nothing for negative points.
Selected row: black bar (colour 0 fill); name colour 11, or, while typing, the typed part in colour 14
followed by the rest of the stored name in colour 11. Other rows: background restored from the back
page, colour 15.

### 11.3 `State_02_SignIn` 2caa:1240 [O]

```
g_kbdPlayMode = 0; g_inBuilding DS:0094 = 0
Roster_LoadOrCreate()                                  # §12.3
if DS:0098 == -1: DS:0098 = SndRes_LoadMusicBank(0x1020)
SignIn_LoadButtons()   # buttons; DS:7a44 = SndRes_LoadEffectBank(0x102F); play DS:0098 section 0 looping, arrangement 1
SignIn_ShowScreen()
Cancel enabled = (count == 0); New Player enabled = (count > 0); Start Game enabled = New Player's
g_signinClicked = 0; g_rosterIndex = 0; g_typedLen = -1
scroll bar: max = max(0, count - 8); next = now; pos = 0
code = SignIn_Run()
selected entry (index g_rosterIndex):
  points < 0 (new player):
      Player_InitRecord(); g_withChimps DS:4578 = (Dlg_AskChimps() != 0)
      entry.points = 0; memcpy(g_player DS:4562, entry, 22)    # name, points 0, slot
      Players_SaveCurrent(); Roster_Save()
  else:
      Players_LoadRecord(entry.slot); DS:1ad4 = g_soundOff; SndRes_ApplySoundOption()
Pal_FadeOut; SignIn_Free()                             # roster copy, button bundles, effect bank
return 0                                               # → state 3 (hub)
```

The `code != 0x1B` test at 2caa:12db can never fail (`SignIn_Run` only returns after a successful
Enter / Start Game). The title music (DS:0098) keeps playing into the hub; state 4 frees it.

### 11.4 `SignIn_Run` 2caa:0eb3 [O]

```
Screen_SaveAll; SignIn_DrawList(0, -1, -1); PicBtn_Draw ×3; ScrollBar_Draw(g_rosterScrollBar)
Zones_AddLocal(list zone); Btn_AddZone ×3 (Cancel, New Player, Start Game); Zones_AddLocal(bar zone)
Dirty_Flush; Input_Flush; poll Input_GetCode until it returns 0
if g_typedLen == -1: g_typedLen = 0; if count == 0: SignIn_HandleCode(0x703)   # empty roster: New Player
pressed = 0                                            # which picture button is shown pressed (1..3)
loop: code = Input_GetCode() (busy, copying the click time stamp while 0)
  table CS:1210 (12 codes):
    0x4706, 0x8706   Cancel pressed/held:     show Cancel pressed (if enabled), others normal
    0x4707, 0x8707   New Player pressed/held
    0x4708, 0x8708   Start Game pressed/held
    (each release first draws any other shown-pressed button normal; a disabled button's release
     and Enter while Start Game is disabled are ignored entirely)
    0x0702           Cancel released:         normal picture; SignIn_HandleCode(0x702)
    0x0703           New Player released:     normal picture; SignIn_HandleCode(0x703)
    0x0704, 0x000D   Start Game released, Enter: normal picture;
                                              if SignIn_HandleCode(code) == 0x0D: done
    0x001B           Esc: SignIn_HideButtons; g_saveDisabled = 1; Cmd_SaveAndExit(); g_saveDisabled = 0;
                     SignIn_ShowButtons       # Yes exits to DOS without saving anything
    0x0705           SndRes_Nop (no zone or key produces 0x705)
  other codes: any shown pressed button back to normal; then
    (code & 0xFFF) == 0x701: ScrollBar_HandleClick(bar, code); SignIn_DrawList(1, -1, -1)   (every poll)
    else: 0x153 (Del) → 8; if SignIn_HandleCode(code) == 0x0D: done
done: remove the zones; return code
```

A press shown on a button is not undone when the pointer leaves it until another code arrives;
releasing outside every zone produces no code (the button stays drawn pressed until the next code)
[O; I visibility].

### 11.5 `SignIn_HandleCode(code)` 2caa:082e [O]

Checked in this order:

| Code | Condition | Effect |
|---|---|---|
| 0x148 Up / 0x150 Down | (clear DS:a065, the dead throttle) | handled below |
| 0x12 Ctrl+R | index valid and entry is a real player | `SignIn_HideButtons`; `Dlg_YesNoRes(0x106A)`; Yes: entry.points = −1, g_typedLen = 0, `Roster_UpdateTyped(8)` (removes it), `ScrollBar_SetRange(count − 8)`, `SignIn_DrawList(1, index, −1)`, **`Roster_Save()`** (the index is written at once; the record stays in its slot); `SignIn_ShowButtons`; return 0 |
| 0x702 (Cancel) | | g_typedLen = 0; `Roster_UpdateTyped(8)` (drops the in-progress entry); set range; redraw (prev −1) |
| 0x703 (New Player) | count ≥ 99 | hide buttons, `Dlg_MessageRes(0x1069)` (list full), show buttons |
| 0x703 | else | typed[len++] = 0x1F; if `Roster_UpdateTyped(0x1F)`: set range; scroll so that the index is visible (top = index if index < top; top = index − 6 if index ≥ top + 7); redraw |
| 0x700 (list release) | cursor position (DS:7a88 / 7a86, not the release record's) x 58..241 and y 46..(46 + 11·min(count, 8) − 1) | if the selected entry is in progress (points < 0): ignored. Else row = (y − 46)/11 + top; if row < count: stamp the click time; if row ≠ index: len = 0, index = row, redraw rows; DS:a073 = 1 |
| Up | (after the above) | if 0 < index < count and the entry is a real player: len = 0, index − 1, redraw rows (the list scrolls in `SignIn_DrawList`) |
| Down | | if 0 ≤ index < count − 1 and a real player: len = 0, index + 1, redraw |
| 8 Backspace (and Del) | | if len > 0: len −= 1; `Roster_UpdateTyped(8)`; keep the index visible (top = index, or index − 6); set range; redraw |
| 0x20..0x7F | len ≥ 15 | ignored |
| | len = 0 or typed[0] = 0x1F | lower case → upper case; anything outside 'A'..'Z' ignored (the first character must be a letter) |
| | | if the last typed byte is 0x1F: drop it; typed[len++] = c; **if `Text_Width(typed)` ≥ 60 (small font): undo and ignore**; effect 0 of bank DS:7a44 (0x102F, loop 0, priority 1); `Roster_UpdateTyped(c)`; set range if the count changed; keep the index visible; redraw |
| other < 0x20 or ≥ 0x80 | index invalid | ignored |
| 0x704 → 0x0D, or 0x0D | entry is a real player | **return 0x0D** (sign in) |
| | entry in progress | if the typed name ends in spaces: strip them one by one with `Roster_UpdateTyped(8)`, rescroll, set range, redraw; if index < count − 1 and typed[0..len] (NUL included) equals entry[index + 1].name: hide buttons, `Dlg_MessageRes(0x1068)` (duplicate), show buttons, return 0; else return 0x0D |
| anything else | | ignored (returns 0) |

So a name has 1..15 characters, starts with an upper-case letter, may then contain any byte
0x20..0x7F (Ctrl+Backspace gives 0x7F, which font 10 draws out of range, §3.2), must be narrower than
60 pixels in the small font, and loses trailing spaces.

### 11.6 `Roster_UpdateTyped(c)` 2caa:01c5 [O]

The typed buffer drives the list: typing either jumps to an existing name or places a new entry.

```
old = count
if count > 0 and entry[index].points < 0:                  # an entry is in progress: remove it
    if len > 0 and entry.points == -2: typed[len++] = 0x1F; c = 0x1F    # keep New Player's marker
    slot = entry.slot; count -= 1; shift entries index+1..count down by one
    entry[count].slot = slot                                 # its slot is free again, next in line
    if count <= index: index = count - 1
if len <= 0: return count != old
for e = 0 .. count-1:                                        # name order, signed char compares
    compare typed[i] with entry[e].name[i] for i < len (stop at the first difference)
      (an entry in progress with a shorter name gets the typed character copied in: never happens)
    if the entry's byte is 0 and the typed byte is 0x1F: insert here
    if all len bytes matched: index = e; return count != old         # prefix match: select it
    if typed byte < entry byte: insert here
insert at e (e == count or entry[e] is a real player):
    if count >= 99: hide buttons; Dlg_MessageRes(0x1069); show buttons; len -= 1; return 0
    slot = entry[count].slot; shift entries e..count-1 up by one; entry[e].slot = slot
    entry[e].points = (c == 0x1F) ? -2 : -1
    typed[len] = 0; strip trailing bytes < 0x20 from typed (the 0x1F marker)
    strcpy(entry[e].name, typed); count += 1; index = e
    return count != old
```

Consequences [O]:

* **Typing on a selected player** (no New Player): the typed text selects the first name it is a
  prefix of — typing "A" while "Ada" exists selects Ada, the "A" drawn in yellow inside "Ada" — and a
  byte that matches no name inserts a new −1 entry at its sorted place.
* **After New Player** the marker 0x1F rides at the end of the typed text during every search, so no
  prefix match can happen: the new −2 entry always stays and sorts before any name that begins with
  the same characters (typing "Ada" when Ada exists places the new "Ada" just before the old one; the
  duplicate check on Enter, which compares with entry index + 1 only, catches exactly this case).
  New Player itself inserts an empty entry (name "") at the top.
* Backspace to an empty name removes the in-progress entry, −2 included (the marker is kept only while
  at least one character remains).
* The order is plain byte order (`'a'` sorts after `'Z'`). Slots: a new entry takes the slot stored in the first
free index entry (`entry[count]`); a removed entry's slot goes back there [O; matches the r2 capture:
Bob after Ada took slot 1].

### 11.7 `SignIn_DrawList(flush, a, b)` 2caa:053a [O, verified §21]

```
top = scrollbar.pos; rows = min(count, 8)
if b >= 0 and rows > 0:                                     # partial redraw: keep the selection visible
    if index < top: top = index; ScrollBar_SetPos(index); b = -1
    if index >= top + 7: top = index - 7; ScrollBar_SetPos(top); b = -1
for k in 0..rows-1 (entry e = top + k, rowY = 46 + 11k):
    if b < 0 or e == a or e == b: draw row k (§11.2); if flush: Dirty_Flush
if (b == -1 or b > rows) and rows < 8: Gfx_RestoreRect(62, rowY_last + 11, 180, 11) (+ flush)
Start Game enabled = index valid and not (entry.points == -2 and len <= 0)
New Player enabled = not (index valid and entry.points < 0)
Cancel enabled     = rows != 0 and New Player disabled
each enable that changed: PicBtn_Draw + Dirty_Flush
```

Only one row below the list is cleared, enough because the count changes by one at a time. Scrolling
with the bar does not move the selection, which can scroll out of view.

### 11.8 New player, existing player [O]

See §11.3. The chimps question cannot be cancelled (Esc disabled, no footer). A new player's record
is the blank record of §12.4 with the name, points 0, its slot and the chimps choice, saved at once
with `Players_SaveCurrent` and followed by `Roster_Save`, so both the record and the full index reach
the file before the hub appears [O; matches the w0 `.players` copies].

---

## 12. PLAYERS.DAT (ovl02 2b5a, ovl07 2caa)

### 12.1 File functions [O]

All open `players.dat` (`s_playersDat` DS:1f6e) in the current directory; handle `g_playersFile` DS:a04c.

| Function | Behaviour |
|---|---|
| `Players_Open(mode, fatal)` 2b5a:004c | mode 1: O_RDONLY\|O_BINARY; else O_RDWR\|O_BINARY, plus O_CREAT\|O_TRUNC (perm 0x180) when mode & 4. Open failure: fatal → `exit(1)` (no message), else returns 0. Mode 1 reads the 8 header bytes and returns 0 unless the version word is 12 (the handle then stays open); success → `lseek(0)`, returns 1 |
| `Players_Read(buf, n, fatal)` 2b5a:00db | short read with fatal → `exit(1)` |
| `Players_Write(buf, n)` 2b5a:0156 | short write → `Players_WriteError` |
| `Players_WriteError()` 2b5a:010a | `Pal_FadeIn`; `Dlg_MessageRes(0x106F)` (garbage footer, §5.7); `Res_Read(0x106F)`; `Gfx_Shutdown` (text mode); **`unlink("players.dat")`**; prints member 2 with `Dos_PrintString`; `exit(1)` |
| `Players_Close()` 2b5a:0189 | close |
| `Players_LoadRecord(slot)` 2b5a:0198 | open mode 1 fatal; `lseek(0x8A0 + slot·0x16BD)`; read 0x16BD bytes into `g_player` DS:4562 (fatal); close |
| `Players_SaveCurrent()` 2b5a:01ea | §12.5 |
| `Player_InitRecord()` 2b5a:0000 | §12.4 |
| `Roster_LoadOrCreate()` 2caa:004f | §12.3 |
| `Roster_Save()` 2caa:0145 | open mode 2 fatal; write `g_roster` (0x8A0 bytes); close |

### 12.2 Header and index [O]

| Off | Size | Field |
|---|---|---|
| 0x000 | 4 | `USR` 0x1A (words 0x5355, 0x1A52) |
| 0x004 | 2 | version 12 |
| 0x006 | 2 | player count |
| 0x008 + 22e | 22 | index entry e (e = 0..99): char name[16], u32 points, u16 slot — a copy of the record's first 22 bytes |

Entries 0..count−1 are the players in name order; entries count..98 hold the free slots in the order
they will be taken; entry 99 is never used (it is written only at creation, with uninitialised heap
bytes from `Mem_Alloc`). Points −1/−2 exist only in memory (§11.1).

### 12.3 Creation and the slot-98 anomaly: `Roster_LoadOrCreate` 2caa:004f [O]

```
g_roster = Mem_Alloc(0x8A0)
if Players_Open(1, 0):                 # exists and version 12
    Players_Read(g_roster, 0x8A0, fatal); Players_Close; return
Players_Open(6, fatal)                 # create / truncate (a second handle if the version was wrong)
header: magic, version 12, count 0;  entries 0..98: zeros with slot = e;  entry 99: not initialised
Players_Write(g_roster, 0x8A0)
Player_InitRecord(); Players_Close()
for slot = 0..98: g_playerSlot DS:4576 = slot; Players_SaveCurrent()
```

Each `Players_SaveCurrent` writes the record **and the 22-byte index entry at position
`g_rosterIndex`**, which is 0 at that point (BSS). So entry 0 is rewritten 99 times with {"", 0, slot}
and ends as {"", 0, 98}: the shipped file's "slot 98 listed twice, slot 0 never" is this loop, not an
editing accident (`fmt_data.md` §8.2 open question) [O]. Consequences: the first new player gets slot
98 (seen in w0), slot 0 is never used, and a 99th player would share slot 98 with the first [I].

### 12.4 The record (0x16BD bytes = `g_player` DS:4562..5C1E) [O except where marked]

| Off | DS | Size | Field | Writers / readers |
|---|---|---|---|---|
| +0x000 | 4562 | 16 | name (≤ 15 chars + NUL) | sign-in |
| +0x010 | 4572 | 4 | u32 points `g_score` | `Score_Add` (12e0:0ace, +20 per new part), puzzle rewards (`code_puzzles_a.md`), race rewards (2fcb:0325), HUD |
| +0x014 | 4576 | 2 | slot `g_playerSlot` | sign-in / creation |
| +0x016 | 4578 | 2 | `g_withChimps` 1 / 0 | sign-in (new player only); chimp set-up |
| +0x018 | 457a | 2 | — | never referenced |
| +0x01A | 457c..458f | 20 | live state of the building in progress: 457c blueprint seen, 457e bananas, 4580 tube room, 4582 parts needed, 4584 required parts installed, 4586 completion bonus given, 4588 paint colour, 458a / 458c cancel flags of hints 0x1046 / 0x1048, 458e set-up coin | `code_world.md` §4, `code_vehicles_races.md` |
| +0x02E | 4590 | 2 | race index = building·5 + level | state 4 |
| +0x030 | 4592 | 2 | building (−1 = none yet) | hub |
| +0x032 | 4594 | 2 | level | state 4 |
| +0x034 | 4596 | 3 × 2 | races won per building, −1..4 | state 11 |
| +0x03A | 459c | 3 × 2 | building has a saved block (resume) | §12.5, state 8, post-race, final movie |
| +0x040 | 45a2 | 2 | hints off | Options menu |
| +0x042 | 45a4 | 19 × 2 | hint shown, one word per hint 0x1043..0x1055 | `Hint_Show` |
| +0x068 | 45ca | 2 | — | never referenced |
| +0x06A | 45cc | 2 | sound off | Ctrl+S, Options, sign-in (§7) |
| +0x06C | 45ce | 2 | — | never referenced |
| +0x06E | 45d0 | 8 × 33 × 2 | puzzle variant solved: + kind·0x42 + variant·2 (≤ 32 variants used per kind) | `code_puzzles_a.md` §1.3 |
| +0x27E | 47e0 | 3 × 0x6BA | per-building blocks (§12.6) | save / state 4 |
| +0x16AC | 5c0e | 2 | races won in all (15 = the end) | state 11 |
| +0x16AE | 5c10 | 15 | vehicle colour per race index | workshop (`code_vehicles_races.md`) |

`Player_InitRecord` (2b5a:0000): a zeroed record with +0x30 = −1 and +0x34/+0x36/+0x38 = −1, copied to
DS:4562 (its store DS:45cc = DS:1ad4 happens before the copy and is lost, §7) [O; matches the r2
`.players` record of "Bob"].

### 12.5 Saving: `Players_SaveCurrent()` 2b5a:01ea [O]

```
if g_saveDisabled DS:1f7a: return                      # sign-in Esc path
if g_inBuilding DS:0094:
    g_buildingInProgress[g_building] = 1
    block = DS:47e0 + g_building·0x6BA
    memcpy(block, DS:457c, 0x14); Parts_SaveState(block + 0x14)
    Rooms_SaveState(block + 0x26C, block + 0x302); Actors_SaveState(block + 0x492)
    g_inBuilding = 0                                   # a later save does not rewrite the block
Players_Open(2, fatal)
lseek(0x8A0 + g_playerSlot·0x16BD); Players_Write(DS:4562, 0x16BD)
lseek(8 + g_rosterIndex·22); write(DS:4562, 22)        # unchecked; keeps the index points current
Players_Close()
```

Only the player's record and index entry are written; the file never changes size. Round 2 saw
it (REFERENCE_CAPTURES §9.5): Save & Exit from a building changed 295 bytes (26 in the record head,
267 in the building block, 2 in the index entry's points); a second player took slot 1 = the index
entry count, changing 7 bytes (the count, entry 1's name, the record's name). A missing or
read-only file at save time ends the program with exit code 1 and no message. Callers [O]:

| Caller | When | Block saved? |
|---|---|---|
| `State_02_SignIn` 2caa:1351 | new player (then `Roster_Save`) | no |
| `Cmd_SaveAndExit` 4106:00c5 | menu or Esc, answered Yes; then `exit(0)` | if in a building |
| `State_08_LeaveRoom` 12e0:09aa | vehicle finished, before the race (it sets the in-progress flag and `g_inBuilding`) | yes |
| `State_16_SaveAndLeave` 12e0:012f | Start New Player / Return to Technology Center | if in a building |
| `State_13_PostRaceMenu` 2fcb:0617 | leaving the post-race menu after a win: DS:459c[b] = 0 first (2fcb:0611); after a loss nothing is saved; its Save & Exit sets DS:459c[b] = lost ? 1 : 0 before `Cmd_SaveAndExit` (2fcb:05f1) | no (`g_inBuilding` was cleared by state 8's save) |
| `State_15_FinalMovie` 2fcb:06e0 | after the movie: DS:459c[b] = 0, save | no |
| `Roster_LoadOrCreate` | creation, 99 blank records | no |

Ctrl+R saves only the index (`Roster_Save`): the removed player's record stays in its slot until the
slot is reused.

### 12.6 Per-building blocks and resume [O, contents per `code_world.md` §4.2]

Block (0x6BA bytes) of building b at record +0x27E + b·0x6BA: +0x000 the 20 live bytes, +0x014 per part
{i16 holder, u16 flags}, +0x26C one byte (7 or 9) per door object, +0x302 per room item lists,
+0x492 12 × 0x2E bytes of actor state. State 4 resumes a building whose flag is set **without a
prompt** (12e0:0820), restoring these in the same order.

---

## 13. New Player Hints (ovl12 3060)

### 13.1 Queue [O]

5 slots `g_hintSlots` DS:a0de, 18 bytes each: +0 list node (8), +8 id (−1 = free; slot 0's is
`g_hintSlotId` DS:a0e6), +0xA u32 due tick, +0xE far cancel flag (NULL = none). The pending slots are
also linked, in due order, in `g_hintList` DS:a0da (`List_InsertSorted` with `Hint_CompareDue`).

| Function | Behaviour |
|---|---|
| `Hint_Init()` 3060:0013 | `List_New` → DS:a0da; all ids −1 (from `Game_Init`) |
| `Hint_Queue(id, u16 delay, far *cancel)` 3060:0037 | ignored if hints are off or the hint was shown; first free slot (none free: **slot 5, past the array**, overwriting DS:a138..); due = `g_ticks` + delay; inserted before the first pending hint due strictly later (equal due times keep queue order) |
| `Hint_CompareDue(a, b)` 3060:0000 | b.due − a.due on the **low words** only |
| `Hint_Update()` 3060:00bc | head = first pending; if head.due < `g_ticks` (strict): if cancel is NULL or *cancel == 0: `Hint_Show(id)`; remove it, id = −1. At most one per call |
| `Hint_ClearQueue()` 3060:01fb | `List_Clear`; all ids −1 |
| `Hint_Show(id)` 3060:013b | below |

`Hint_Update` runs once per frame in the warehouse room loop and the workshop loop only
(`code_world.md` §14, `code_vehicles_races.md` §5), so a hint queued elsewhere waits for the next frame
of one of them; `Hint_ClearQueue` runs when a warehouse room is left. A cancelled hint is dropped
without being marked shown and can be queued again (e.g. 0x1046 at every workshop entry).

`Hint_Show(id)` 3060:013b:

```
if not g_hintsOff and not g_hintSeen[id - 0x1043]:
    Hud_EscHook()                                   # puts a part held on the cursor back
    prev = Font_Select(0); force = Mouse_ForceDefault(1)
    text = Res_LoadCopy(id)                         # 'H': FF 06 designer note FF, then the text
    h = SndRes_LoadSong(0x1056)                     # chime: 0x1056 speaker, 0x1057 FM
    g_hintBox DS:2456 body = text
    SndRes_PlayEffect(h, loop 0, priority 0x32)
    Dlg_MessageBox(g_hintBox)                       # title DS:2462 "New Player Hint", footer DS:2472
    SndRes_FreeSong(h); Mouse_ForceDefault(force); Mem_Free(text); Font_Select(prev)
g_hintSeen[id - 0x1043] = 1                         # always, also when hints are off
```

The dialog loop pauses the sound before it draws the box (`code_sound.md` §12.1), so the chime is
cut a few instructions after it starts and is freed at the end: it is heard at most for a sound
tick that happens to fall into that window [O code; I audibility; check against a capture].

### 13.2 Triggers [O]

| Id | Event | Call | Delay | Cancel flag |
|---|---|---|---|---|
| 0x1043 | the blueprint viewer opens (2dee:19df) | `Hint_Show` | — | — |
| 0x1044 / 0x104A | a part picked up that is not needed / needed (18f5:00ec) | `Hint_Queue` | 0 | — |
| 0x1045 | the workshop is entered while parts are carried (2dee:0d25) | `Hint_Show` | — | — |
| 0x1046 | workshop entry (2dee:0d13) | `Hint_Queue` | 0x490 = 1168 ticks (8.02 s) | DS:458a: set when the player leaves through the tube (2dee:0e57); reset by state 4 at level 0 |
| 0x1047 | a third part of a type is refused (18f5:0175) | `Hint_Queue` | 0 | — |
| 0x1048 | warehouse room entry (12e0:0691) | `Hint_Queue` | 0x2238 = 8760 ticks (60.14 s) | DS:458c: set when a door is used (18f5:053e / 056e) |
| 0x1049 | a part thrown in the trash: in a room (HUD code 6000, 1a04:0b51) / in the workshop (2dee:1179) | `Hint_Queue` 0 / `Hint_Show` | 0 | — |
| 0x104B / 0x1055 | first tube exit into the hall, With / Without Chimps (sequence step −43 with −1, 1834:056e) | `Hint_Queue` | 5 | — |
| 0x104D | knocked down by a chimp (step −43 0x104D in the player sets 0x1033 / 0x1034) | `Hint_Queue` | 0 | — |
| 0x104F | race lost (2fcb:055a) | `Hint_Show` | — | — |
| 0x1050 | parts needed reaches 0 (18f5:012c) | `Hint_Queue` | 0 | — |
| 0x1051 | the last banana is gone (1834:03f6, 0428) | `Hint_Queue` | 0 | — |
| 0x1052 | every puzzle start (the eight `Pz*_Run`) | `Hint_Show` | — | — |
| 0x1053 | the workshop completion bonus has been given (2dee:0ddb) | `Hint_Show` | — | — |
| 0x1054 | race door with an incomplete vehicle (2dee:1328) | `Hint_Show` | — | — |
| 0x104C, 0x104E | never triggered (designer notes: first walk over a part, first race won) | — | — | — |

---

## 14. Quit and exit paths [O]

| Path | What happens |
|---|---|
| Esc in the attract | DS:455a = −1 → the main `Seq_Run` stops → `exit(0)` (`Game_Shutdown`), no question, no save |
| Save & Exit (File menu; Esc in the hub, rooms, workshop, puzzles, post-race; hub zone 0xBBC) | `Dlg_YesNoRes(0x1064)` (focus No): Yes → `Players_SaveCurrent` → `exit(0)`; No → back; Esc → back (from the menu: the File menu reopens) |
| Esc in the sign-in | the same box with saving disabled: Yes exits without writing |
| Start New Player / Return to Technology Center | Yes/No 0x106C / 0x106B → `g_menuRequest` → code 0x17A3 / 0x17A2 → state 16 (save) → state 2 / 3 |
| End of the game | final movie, save, end of the main sequence (`code_vehicles_races.md` §14) |
| `players.dat` write error | message, file deleted, text-mode message, `exit(1)` (§12.1) |
| `players.dat` open or read failure at save / load | `exit(1)` without a message |

`exit` runs `Game_Shutdown` (`code_runtime.md` §4).

---

## 15. Sounds triggered by the UI [O]

| Where | Sound |
|---|---|
| attract | music bank 0x1020, section 0, looping; effect bank 0x1022: effects 12 and 13 at the title phases (loop 0, priority 4), screen-animation steps −36 / −37 play / stop effects of the bank |
| sign-in | music bank 0x1020 (handle DS:0098) section 0 looping, arrangement 1 (`SndRes_SelectArrangement(h, 1)`); effect 0 of bank 0x102F for every accepted typed character (loop 0, priority 1) |
| New Player Hint | song 0x1056 / 0x1057, priority 0x32 (+0x3F), cut by the box (§13.1) |
| dialogs, drop-downs | `Snd_Pause` / `Snd_Resume` around the loop |
| Options > Sound & Music, Ctrl+S | `SndRes_ApplySoundOption` (the silence song, `code_sound.md` §12.2) |

Buttons, menus, the help pager and the text viewer make no sound.

---

## 16. Randomness [O]

No UI module calls `rand()`: none of 2b16, 2b89, 3f56, 4034, 406a, 40a4, 4106, 411d, 4140, 2c11, 2caa,
2b5a, 3060 contains a call, and the attract's screen animations contain no random step (−34/−35) in
the shipped data (`seqattract.py`). The only indirect call is through `ScreenAnim_Step` (174b:001a),
which no attract sequence reaches. The UI therefore never changes the order of `rand()` calls; the
w0 replays agree (deterministic up to the warehouse, `REFERENCE_CAPTURES.md` §3.2).

---

## 17. Input codes and zones used by the UI (summary) [O]

| Block | Meaning |
|---|---|
| 0x0D (release) | attract full-screen zone DS:0a3a (a click = Enter) |
| 0x13B..0x13D (release) | menu titles (global zones, priority 8) |
| 0x44F (release) | drop-down outside zone (ignored) |
| 0x51A..0x522 | text viewer (§8.3) |
| 0x700..0x708 | sign-in (§11.4) |
| 0xFD0 + row | drop-down items |
| button key | dialog buttons: 'y', 'n' (Yes/No, chimps), 0x149 / 0x151 (pager) |
| 0x1B (release) | dialog and drop-down footers |

Keys reach the UI as BIOS words (ASCII, or 0x100 | scan): Enter 0x0D, Esc 0x1B, Backspace 0x08,
Ctrl+R 0x12, Ctrl+S 0x13, Up 0x148, Down 0x150, Left 0x14B, Right 0x14D, PgUp 0x149, PgDn 0x151,
Del 0x153, F1..F10 0x13B..0x144, Alt+H/F/O 0x123 / 0x121 / 0x118.

---

## 18. Data read from ssg.exe and the archives [O]

| Where | Format | Use |
|---|---|---|
| DS:4053, 4017, 402b, 403f | `MenuBarDef`, `MenuDef` ×3 (§6.1) with far strings and far handler tables | menu bar |
| DS:3dea..3e2a | far label tables, u16 key tables (§5.7, §5.8) | Yes/No, pager |
| DS:3e2c | far labels With / Without Chimps | chimps box |
| DS:3e38, 3e45, 2462, 2472 | NUL-terminated strings with key-cap bytes | dialog footers, hint box title and footer |
| DS:3dd4 | 22-byte zone (x, y, w patched) | dialog footer |
| DS:4060..40e4 | 7 zones (§8.3; y / h of three patched) | text viewer |
| DS:20dc, 20f0, 2110, 2130, 2150, 2166 | scroll bar record, 3 picture buttons, 2 zones (§11.1) | sign-in |
| DS:20a8 | far table of 1e28 stub entries (§10.1) | attract sequence |
| DS:2090, 2096, 209c, 20a2 → DS:205c.. → DS:2008.. | animation descriptors, channel tables, u16 scripts (§10.2) | attract |
| DS:0a3a | 22-byte zone | attract click |
| DS:1f6e | "players.dat" | file name |
| DS:1f7c, 1f9c | far rank names, u32 thresholds (`Rank_FromPoints`) | sign-in rows |
| resources 9, 10 | `fnt` fonts | §3 |
| 1 | bundle of 6 `G` (menu titles) | §6.3 |
| 0x1000..0x102A | attract and sign-in pictures, palettes, texts (§10.3, §11.2) | |
| 0x102B..0x102E | picture-button bundles `G G G L`, scroll bar bundle `G ×5 L` | sign-in |
| 0x102F, 0x1020, 0x1022 | effect / music banks | §15 |
| 0x1043..0x1055 | `H` hint texts | §13 |
| 0x1056 / 0x1057 | hint chime songs | §13 |
| 0x105D..0x1062 | help topic bundles (§6.6) | help |
| 0x1064..0x106F | message and Yes/No bundles (§5.7) | dialogs |
| 0x1067 | {S body, I} | chimps box |
| 0x11D4, 0x11D5 | text viewer panel, arrow bundle | §8 |

---

## 19. Timing (PIT ticks) [O]

| What | Value |
|---|---|
| attract animation frames | 4 ticks (`Attract_PlayAnim`) |
| title phases | `Timer_Set(1, n·146)`, n = 1, 3, 5 (1.00, 3.01, 5.01 s; tested once per frame with `Timer_Expired`, so they end on the first frame after tick > deadline) |
| story pages, sign-in prompt | 0x111C = 4380 ticks (30.07 s) from after the page is drawn (and faded in on the first page) |
| attract waits | −2 t: t + 1 frames of 4 ticks |
| button flash | 4 × `Delay(4)` (16 ticks + drawing), time frozen afterwards by the dialog |
| dialogs, help pages | time frozen |
| text viewer arrows | first repeat after 146 ticks, then every 29; release flash 7 ticks (slot 6) |
| sign-in scroll bar | repeat every 27 ticks (0x1B) |
| hints | 0x490 (8.02 s), 0x2238 (60.14 s), 5 ticks |
| palette fades | retrace-paced (`code_runtime.md` §13.4) |

---

## 20. tmport comparison: what to generalise

| tmport module | TM Windows original | G&G | Verdict |
|---|---|---|---|
| `src/text/text_renderer` | `Text_Render` 10b0:00b5 | `Text_Render` 2b89:007a, same structure (§4) | **generalise** behind a dialect: control set (TM: TAB, FE, `FF 04` tab stops; G&G: none, `FF 04` = 2-byte no-op); `MeasureRun` advance (TM: `charWidth`; G&G: raw width + 1, space included); extent (TM: maxX − minX, `Text_Width("")` = 0 special case; G&G: maxX − x0); underline (TM: `_` becomes a blank; G&G: none, line in `g_drawColor`); per-glyph flush (TM only); the substitution delimiter quirk (G&G: delimiter ignored inside a substitution reached from the main run); colour sink (TM: frame-buffer pen; G&G: the font's own colour); the unused Timer_Wait(6) (TM only). Font behind an interface: `charWidth(c)` (G&G: 3 for space, width + spacing, spacing for glyph-less codes), `rawWidth(c)`, `drawChar` returning the advance (G&G: 0 for glyph-less codes), `lineHeight` (G&G: height + 1) |
| `src/text/font` | FFNT, MSB-first, spacing built in | `fnt` 0x1A, LSB-first, y offsets, spacing field, per-font colour | **new** G&G font class implementing the interface |
| `src/ui/menu_bar` | Win16 menu replaced by an engine-drawn bar | self-drawn bar with picture titles, dialog-drawn drop-downs | **split**: the data model (`MenuBarModel::fromExe`) is the same 0x14-byte record — generalise it to keep +0x04 title width, +0x10 drop-down width, +0x12 title x, and to map handlers by far pointer to a game-specific command enum (G&G: stub entries 1e97:0025..0039, 1e9b:0020..0039). The interaction and drawing (Win3.1 look, mnemonics, command posting, `Menu_EnableAll`) stay TM-only; G&G gets a new `MenuBar` following §6 (keys, zones, highlight XOR, handler return value reopens the menu, no click-outside close, sound pause, no time freeze) |
| `src/ui/game_dialog` | 10c0: `DlgDesc` by type, 2 buttons, 3 frames, `Delay_ms(62)` flash, nested music pause, 512×384 centring | 3f56/4034: 0x32 descriptor, 0..3 buttons with key arrays and alignment, 2 frames + 2-px shadow, `Delay(4)` ticks flash, time freeze, non-nested sound pause, 320×200 centring, wrap 280, pictures | **pattern only**: write `gg::ui::Dialogs` from §5; share the text renderer, a save-under helper and the modal-loop host (engine pump, input source, delay) |
| `src/ui/dialog_desc` | static 0x1A-byte `DlgDesc` / `PagerDesc` in DGROUP | no static descriptors; message boxes come from resource bundles `{S/I title, S body, I flag}` | **not reusable**: a G&G bundle decoder (§5.7) replaces it |
| `src/ui/help_topics` | help pager data | help topic bundles (§6.6) | pattern |

---

## 21. Verification [O]

The model of this document (`uimodel.py`) was rendered over the DOSBox-X frames and compared pixel by
pixel (6-bit RGB, base palette DS:024e for colours 0..15, the capture's own colours for the others):

| Frame | Content | Result |
|---|---|---|
| `w0/05_chimps_dialog` | `Dlg_AskChimps`: box (6, 46) 308 × 107, two buttons, focus inverted | 0 of 32 956 pixels differ |
| `w0/24_return_dialog` | `Dlg_YesNoRes(0x106B)`: title, 3-line body, footer with key-cap, right-aligned Yes/No, No focused | 0 of 38 440 |
| `w0/11_workshop_hint` | hint 0x1046 box | 0 of 28 388 |
| `w0/08_building_hint`, `13_…`, `16_…` | hints 0x1043, 0x104B, 0x1052 | only the cursor differs (48, 13, 13 px) |
| `w0/23_file_menu` | File drop-down with row 0 highlighted (box (0, 9) 242 × 54) | 0 of 12 936; bar pictures consistent |
| `r2/player2/02_signin_roster`, `03_new_player`, `04_name_typed`, `w0/03_signin` | sign-in rows: selection bar, colours 11 / 14 / 15, points and rank | 0 of 1 980..2 133 each |
| `r2/puzzle/k0_02_hint_text`, `k5_09_hint_text` | text viewer: panel, arrows, wrapped text (texts 0x20DD, 0x1163), scroll bar | cursor only (1 and 4 px) |
| `w0/02_story1` | story page 1: wrapped box text in font 1, the font-0 line drawn with spacing 1 | 0 of 1 592 glyph pixels |

Not checked against a capture: the help pager (no capture), the scroll bar of a long roster, the
Ctrl+R and duplicate boxes, the attract timing beyond `REFERENCE_CAPTURES.md` §4.

---

## 22. Porting notes [R]

* Implement `Text_Render`, `Dlg_Layout`, `Dlg_Draw` and the sign-in rows exactly as §4, §5, §11.7: they
  are pixel-exact against the captures, including the width+1 measure rule and font 0's spacing patch
  on the first story page.
* Model the dialog time freeze as a clock offset (`PORTING_NOTES.md` §2.2); keep the drop-down and the
  text viewer unfrozen.
* Keep: the footer click = Esc; the Esc-disabled chimps box; Enter selects the focused button; the
  default buttons from the bundles (Save & Exit and Start New Player default to No); menu handlers'
  return values (Help and an Esc'd Save & Exit reopen their menu); clicks outside a drop-down do not
  close it; the pager's fixed box; the sign-in's prefix search, 15-character / 60-pixel name limit,
  capital first letter, Ctrl+R saving the index at once; resume without a prompt; time not frozen in
  the text viewer; `Hint_Show` marking hints as seen while hints are off; the strict `due < tick`.
* Fix or guard (they change nothing a player sees in the shipped data): the uninitialised footer of
  message 0x106F; `Font_*` index range (code 0x7F in names); the hint queue's slot-5 overflow; the
  low-word due comparison; `Dlg_MessageRes`' footer flag for unexpected values; the handle leak on a
  version mismatch. Do not delete `players.dat` on a write error (`PORTING_NOTES.md` §9).
* Decide (wave 3): whether a new player inherits the live sound switch (original: record says on,
  switch unchanged, §7) — keep the original for fidelity, offer a fix as an option; whether to keep
  the creation loop's slot-98 anomaly (keep: files stay byte-compatible).
* Save-unders, the text viewer's saved area and the menu pictures live in `g_resBuffer` in the
  original; the engine can give each its own buffer without visible change (§2.3).

---

## Open questions

1. The hint chime: is any of it audible before the box pauses the sound (§13.1)? An OPL capture of a
   hint (e.g. the first warehouse room) answers it.
2. The help pager and Ctrl+R / duplicate / full-list boxes have no capture yet; a capture of each would
   confirm §5.8 and §11.5 geometry (the code is the verified dialog code).
3. `Dlg_MessageRes(0x106F)`'s footer pointer is stack garbage: what the original shows (or whether it
   crashes) depends on the stack contents; only a disk-full run in DOSBox-X can tell.
4. Whether the 2-pixel title-zone overlaps (§6.2) are reachable in practice in the way inferred from
   the hit-test tie rule.
5. Picture order inside the picture-button bundles (normal, pressed, disabled) is inferred from how
   the sign-in uses them; the bundles' third picture was not compared with a disabled-button capture
   beyond the r2 frames (which match).
