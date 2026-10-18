# OTHR puzzler, hint and help-page text (TMT.DLL ids 11000-11611)

Scope: the three groups of text resources in `TMT.DLL`, Windows type **32515** (Mac type
`OTHR`): the elf **puzzlers** 11000-11358, the **Flutter hint** balloons 11500-11532 and the
**Help menu pages** 11600-11611. This document covers their byte format, the text-markup
language they use, and the EXE code that loads, selects, presents, speaks and scores them.

Evidence: Ghidra export `re\win\ghidra_export\` (functions cited as `SSSS:OOOO`); code that
Ghidra had not disassembled was listed with a headless script against a copy of the project
(`extracted\scratch\othr_puzzles\gh\DisRange.java`, `DecompAt.java`): the text-engine
control-code handlers `10b0:014e-0498`, the puzzle key handlers `1150:1128-1197`, the help
dialog `10c0:0cde` and the stub entry points `1180:0117-015a`. DGROUP (`11b8`) bytes were read
from `TMTWINCD.EXE` at file offset `0x23c00 + off`. Far pointers stored in DGROUP were resolved
with an NE relocation scan (`extracted\scratch\othr_puzzles\nereloc.py`). All 404 resources
were parsed with `tools\assets\othr_puzzles.py` (section 11).

Labels: **[obs]** = read directly from bytes, decompiled/disassembled code or decoder output.
**[inf]** = inference. Game text is identified by resource id or DGROUP offset, quoting at most
a couple of words.

Related specs: `code_1038-1050.md` (resource manager `GetResource` 1038:08c5, fore colour
`Pal_SetForeColor` 1048:02fd and the EGA colour table), `code_1008-1030.md` (input codes
`Input_GetCode` 1028:0131 and hot spots 1098:xxxx).

---

## 1. Summary

| Ids | Count | Content | Loaded by | Index |
|---|---|---|---|---|
| 11000-11358 | 359 | Puzzlers: question text plus 3 answer choices, levels 1-4 | `1150:10a4` `GetResource(11000 + puzzle)` | puzzle = id - 11000 |
| 11500-11532 | 33 | Hint balloons spoken by Flutter (tutorial, rank messages) | `1130:0068` via BNDL 327 entry *n* | hint *n* = id - 11500 |
| 11600-11611 | 12 | Help pages (About the Game, Keyboard, Mouse, Educational Focus) | `1180:0000(first, count, title)` | see section 10 |

* Every resource is a NUL-terminated byte string in an NE resource that is rounded up to
  512 bytes. The tail is zero padding. All 404 resources have one NUL terminator and an
  all-zero tail [obs].
* The text uses one markup language, interpreted by the text engine `10b0:00b5` [obs]:
  `0x0D`/`0x0A` = newline, `0x09` = tab, `0xFE` = return to the left margin on the same line,
  `0x80..0x8B` = key-symbol glyphs, and `0xFF op ... 0xFF` control codes (colour, underline,
  centring, comment). Section 3.
* A puzzler has no type, difficulty or answer field. Its layout is: question lines, then two
  empty lines (the third consecutive `CR` ends the question), then exactly three choices,
  each ending in `CR`. **Choice 0 is always the correct answer**. The game shuffles the display
  order per deal [obs].
* The difficulty level is given by the id range, through the table at DGROUP `0x1DE7`
  (`{0, 63, 179, 302, 359}`). Each record also has a comment code `FF 06 "L=n" FF` that agrees
  with the table for all 359 records [obs]. The renderer ignores the comment.
* Selection [obs]: for each trip up the mountain, `1150:0327` deals 9 puzzles (3 paths x 3
  elves) from the player's current level range. Each puzzle has a per-player history byte
  (0 never answered, 1 right first time, 2 last attempt wrong, 3 right after a miss). The deal
  takes 3 never-answered, 3 missed and 3 right-after-miss puzzles, with a fall-back chain when
  a pool is empty. The level goes up by one when at least 80 % of the level's puzzles have been
  answered right first time.
* Speech [obs]: puzzle *i*'s line *k* is SSND `base[i] + k` (table DGROUP `0x1990`, ids
  4000-6615 in `TM2..TM8SOUND.DLL`). Its choice *c* is `base[i] + nlines + c`, except for the
  16 puzzles listed at `0x1DC7`. Hints are SSND `7100 + n`.

---

## 2. Container and loading

* Type and id: NE resource type 32515 in `TMT.DLL`. The EXE maps any id >= 10000 to the Mac
  type `"OTHR"` (`1038:038a`; string at DGROUP `0x075A`) [obs].
* Size: the NE length field is in 512-byte units. Payload sizes are 85-281 bytes for
  puzzlers, 39-157 for hints and 182-677 for help pages. Three help pages (11605, 11608,
  11609) occupy 1024 bytes [obs].
* **Payload = bytes before the first `0x00`.** The type-15 Mac map entry has no length, so
  nothing else gives the true length [obs]. Reimplementation: read the resource and cut at the
  first NUL.
* `GetResource(id)` (1038:08c5 → 1038:076f → `Res_LoadCopy` 1038:03da) returns a **fresh
  heap copy** of the whole rounded resource. The original Windows resource is freed straight
  away. `1150:13c6` frees the copy with 1030:0082 after the puzzle screen [obs]. This matters
  because the puzzler parser **writes into the text** (it converts and terminates in place;
  section 4.3). Each display starts again from the original bytes.
* Character set: printable ASCII 0x20-0x7E, plus the codes in section 3. No other bytes occur
  [obs]. The puzzlers use only `CR` (0x0D) for newlines. They contain no LF, TAB, 0xFE or
  glyph bytes [obs].

---

## 3. Text markup (common to all three groups)

### 3.1 Byte classes

Dispatcher of the text engine `10b0:00b5`: the switch table `CS:0524` in segment 10b0 holds
the values `{09, 0A, 0D, 20, FE, FF}` and the handlers `{0393, 03CF, 03CF, 0418, 0412,
014E}` [obs].

| Byte(s) | Meaning [obs] |
|---|---|
| `00` | End of text. |
| `0D`, `0A` | Newline: `y += lineHeight`. `x =` left margin, or the centring position of the next line when centred mode is on (3.3). |
| `09` | Tab: `x = left + tab[i]` for the first tab stop *i* (0..7) with `left + tab[i] > x`. Default stops are 40, 80, ..., 320 px. Nothing happens if no stop is to the right. |
| `20` | Space, with word wrap (3.3). |
| `FE` | `x = left` with **no** line feed. Used with runs of spaces to build two-column tables in help pages (section 10). |
| `FF op ... FF` | Control code (3.2). |
| anything else | A glyph of the current game font (`1068:00f6` draws, `1068:00dd` gives the advance). Bytes `0x80..0x8B` are key symbols in the font (3.4). |

### 3.2 Control codes `FF op payload FF`

The handler for `FF` (`10b0:014e`) reads `op = next byte`. Ops 1..6 dispatch through table
`CS:0518` = `{016D, 028D, 02AE, 02D7, 0326, 037D}`. Each handler then skips forward to the
next `FF` byte inclusive. **Tokenising rule: a control code runs from an `FF` to the next
`FF`.** The payload therefore never contains `FF` [obs].

| Code | Name | Semantics [obs] | Occurs in |
|---|---|---|---|
| `FF 01 n FF` | arg | Insert caller argument string *n* (1-based, far-pointer array passed to the engine). The inserted text is measured and word-wrapped like inline text. | none |
| `FF 02 FF` | ul | Toggle underline mode. In underline mode every glyph is underlined (a 1-px line at `y + lineHeight - 1`, `1058:090f`), and `'_'` glyphs are **not drawn**, only advanced and underlined (`10b0:0662`). `FF02FF ____ FF02FF` therefore draws a blank answer line. | 288 codes in puzzlers (144 underlined runs of 4-6 `_` in 143 puzzles), 10 in help pages (column headings and phrases) |
| `FF 03 c FF` | color | `Pal_SetForeColor(c - 1)`. The argument is an EGA colour number + 1. Seen: `c=1` → EGA 0 black (normal), `c=5` → EGA 4 (170,0,0) dark red (emphasis). | 831 × `c=1`, 471 × `c=5` (puzzlers only) |
| `FF 04 t.. FF` | tabs | Set up to 8 tab stops: `tab[i] = *(u16*)p; p += 1` until `FF` or 8 stops. It reads a **word but advances one byte** (Mac-port bug), so only single-byte stops with garbage high bytes would result. | none |
| `FF 05 FF` | center | Skip to the closing `FF` first, then **toggle centred mode**. If it is now on, measure the rest of the current line (up to CR/LF/NUL, `10b0:053c`) and set `x = left + (width - lineWidth) / 2` (signed, truncating). | 253 puzzlers (first code), all 33 hints (after a leading CR) |
| `FF 06 text FF` | note | Ignored (comment). Puzzlers carry `L=1`..`L=4`. | 359 (one per puzzler) |
| `FF` + other op | — | Not handled: the engine jumps to the per-glyph epilogue and does **not** skip to the closing `FF`, so the payload would be drawn as glyphs. One consequence is that `FF FF` acts as a 2-byte no-op. | none |

### 3.3 Layout algorithm (`10b0:00b5`, draw and measure modes)

Arguments (near cdecl, offsets from BP) [obs]: `+4 left`, `+6 top`, `+8 width`, `+0A text
far*`, `+0E args far*`, `+12 slow flag`, `+14 outWidth far*`, `+18 outHeight far*`, `+1C draw
flag`. Wrappers: `Text_Draw(x,y,text)` 10b0:0000 = box width 512 (0x200), no args, slow 0.
`Text_DrawBox(x,y,w,text,args,slow)` 10b0:0023 (draw = 1; every caller in this document passes
args = NULL, slow = 0). `Text_Measure(text,args,w,&W,&H)` 10b0:0050 (draw = 0) /
`Text_Width(text)` 10b0:0080 (width 512, returns W; 0 for an empty string).

State: pen `x` (DI), pen `y`, `right = left + width - 1`, `minX = right`, `maxX = left`,
`spaceW = advance(' ')`, `lineH = font height (1068:00d9)`, flags `underline = 0`,
`center = 0`, `tab[8] = {40,80,...,320}`.

```
loop: c = *p++
  0        -> outWidth = maxX - minX; outHeight = (y - top) + lineH; return
  CR/LF    -> x = center ? left + (width - measureLine(p)) / 2 : left;  y += lineH
  TAB      -> first stop with left+tab[i] > x, if any: x = left + tab[i]
  0xFE     -> x = left
  ' '      -> wordW = measureWord(p) + spaceW            // measureWord stops at ' ',CR,LF,NUL
              if x + wordW > right:  skip following ' ' and TAB;  x = left;  y += lineH
              else: minX = min(minX,x); draw/advance ' '; maxX = max(maxX,x)
  0xFF     -> control code (3.2)
  other    -> minX = min(minX,x); x += draw ? drawChar(c) : advance(c); maxX = max(maxX,x)
  after each character (and code) : wait for timer slot 6 (10a8:0049)
```

* `measureLine`/`measureWord` (`10b0:053c(term, p, args, inArg, argPtr)`) sum the glyph
  advances up to `term`, CR, LF or NUL. They skip control codes and expand `FF 01 n`. `FE`
  and `TAB` are measured as glyphs [obs].
* A wrapped line restarts at `left`. It is **not** re-centred, even in centred mode [obs].
* `drawChar` (`10b0:0662`): if not underline or the glyph is not `'_'`, draw it. Then, if
  underline, draw the underline segment of the glyph's advance. If the slow flag is set,
  present that glyph rectangle at once (`1058:05bc`) [obs].

### 3.4 Glyph bytes (hints and help pages only)

`80` `81` `82` `83` `84 85` `86` `8B` occur [obs]. The help page 11605 pairs each with a
keyboard action. From that page they appear to be Left, Right, Up and Down arrows, a
two-glyph Spacebar (net), Esc (exit) and Enter/Return (drop coin, select answer). This is
confirmed by the FFNT key-cap glyphs (`fmt_bndl_ffnt.md` §3.2): 0x80–0x83 arrows, 0x84/0x85
Spacebar, 0x86 Esc, 0x8B Enter (0x8C Ctrl) [obs; open question 6 answered per ARCHITECTURE.md
§7.1 / GAPS.md §3.5].

---

## 4. Puzzler record format (11000-11358)

### 4.1 Grammar

```
puzzler   := header body CR CR CR choice CR choice CR choice [CR] NUL padding
header    := { FF code FF }                 ; before the first CR (observed variants below)
body      := CR line { CR line }            ; the first CR is converted to ' ' by the parser
line      := { text | FF code FF }          ; may be empty only as the single blank line
choice    := { printable ASCII }            ; no codes, no CR
```

Observed headers [obs], with counts:

| Header bytes | Count |
|---|---|
| `FF05FF FF0301FF FF06"L=n"FF` (centred) | 252 |
| `FF0301FF FF06"L=n"FF` (left-aligned) | 106 |
| `FF05FF FF06"L=n"FF` (11347; its `FF0301FF` follows the first CR) | 1 |

* Every puzzler starts with control codes followed by one `CR`. That CR is the "leading
  newline" which the parser turns into a space [obs].
* Within the body, **one empty line** (`CR CR`) may separate stanzas (30 puzzlers, listed
  in the JSON `summary.puzzlers_with_internal_blank_line`). Two consecutive empty lines end
  the question.
* Text lines (non-blank): 2-6 per puzzle (hist: 2×18, 3×24, 4×173, 5×99, 6×45). Display
  lines (`DAT_610e`, see 4.3): 4-8 [obs].
* Choices: exactly 3, 1-28 characters. They contain no codes. Some have a trailing space,
  for example 11010 choice 1 and 11073 choice 0 (8 cases). 358 records end the third choice
  with `CR`; 11347 ends it directly with NUL [obs].
* Emphasis: words drawn in dark red are bracketed `FF0305FF ... FF0301FF` (374 lines).
  Blanks are `FF02FF ____ FF02FF` (143 puzzles) [obs].

### 4.2 Answer determination

* There is **no answer marker**. The parser stores the choices in file order as choices
  0, 1, 2. `1150:0403` treats a click on display position `pos` as correct iff
  `perm[pos] == 0`, i.e. the choice that came **first in the file** [obs].
* Display order: `perm[3]` per slot is a random permutation drawn when the puzzle is dealt
  (7.4). It is persisted with the slot, so a re-shown puzzle keeps its order [obs].
* Wrong choices that were tried stay on screen in EGA 8 (dark grey) and lose their hot spot
  and keyboard focus [obs].

### 4.3 Parser `1150:0901(char far *text)` (exact)

Globals written [obs]: `DAT_60fe` (far*) = text, `DAT_610e` (u8) = display line count,
`DAT_610f` (u16) = number of speakable text lines, `DAT_6102/6104[3]` (far*) = choice
pointers, `DAT_6115[10]` (u16) = y offset of each text line.

```
seen = 0; nl = 0; done = -1; blanks = 0; isBlank[10] = {0}; n610e = 0
for p = text; *p != 0 && done == -1; p++:
    if *p == CR or *p == LF:
        if !seen: *p = ' '                         // leading newline(s) -> space (in place)
        else:
            if nl == 1: isBlank[n610e] = 1; blanks++   // second newline in a row: empty line
            elif nl == 2: *p = 0; done = 0              // third newline in a row: end of question
            if nl < 2: nl++
            n610e++                                     // also counts the terminator
    elif *p == 0xFF: advance p to the next 0xFF         // codes do not count as content
    else: seen = 1; nl = 0
DAT_610f = n610e - 1 - blanks
y = 0; k = 0
for i in 0..9: if !isBlank[i]: DAT_6115[k++] = y;  y += lineH   // only the first DAT_610f are used
while *p == CR or *p == LF: p++
choice[0] = p; c = 1
while *p && c < 3:
    if *p == CR or *p == LF: *p = 0; choice[c++] = p + 1
    p++
```

Consequences for a reimplementation [obs]:

* The question string handed to the renderer = original bytes up to the third newline, with
  leading newline(s) replaced by spaces. For centred puzzles the first line therefore starts
  with one space, which counts in centring and box width. The question still ends with
  `CR CR` (the line break and the empty line), which moves the pen only.
* The third choice keeps its trailing `CR` (not replaced). It is harmless when drawn.
* `DAT_610e` = text lines + empty lines + 1. `local_24` has 10 entries, so at most 10 display
  lines are possible (the data has at most 8).
* With no terminator the code would write `choice[-1]` (index -1). This never happens in the
  data.
* A line that contains only control codes counts as empty, because codes do not reset `nl`.
  The data has no such line.

### 4.4 Statistics (from the tool) [obs]

| Level | Id range | Count | Centred | Voice 1/2/3 |
|---|---|---|---|---|
| 1 | 11000-11062 | 63 | 54 | 25/29/9 |
| 2 | 11063-11178 | 116 | 88 | 45/43/28 |
| 3 | 11179-11301 | 123 | 83 | 38/45/40 |
| 4 | 11302-11358 | 57 | 28 | 16/12/29 |

---

## 5. Puzzle taxonomy

### 5.1 What the program distinguishes [obs]

The EXE handles every puzzler the same way. The only per-puzzle data are:

1. **Level** = id range (table `0x1DE7`, section 6.1). The comment `L=n` duplicates it.
2. **Voice bank** `DGROUP 0x1C60[i]` ∈ {1,2,3} (124/129/106). It selects the speech bank
   for the praise, wrong-answer and clue-word phrases (7300/7400/7500 + k, section 8.6) [obs].
   It probably names the voice actor who recorded the puzzle [inf].
3. **Choices not spoken**: the 16 puzzle indices at `0x1DC7` = {8, 235, 257, 154, 50, 169,
   298, 299, 170, 176, 177, 178, 210, 231, 117, 148}. For these `DAT_6147 = 1`, and
   `1150:14d7` does not speak the choices [obs]. Their content is homophone pairs, number
   words versus digits and place value, where reading the choices aloud would give the
   answer away [inf]. Seven of them still have choice recordings in the sound DLLs (117, 148,
   176, 177, 178, 210, 231: id span = lines + 3). The other nine have only line recordings
   [obs].

### 5.2 Content categories (heuristic, [inf])

The records carry no type field. Content types are mixed within each level. The tool assigns
a `category_guess` from keywords in the question and the shape of the choices. The counts
below come from that heuristic; a few records may be misfiled.

Rules are tried in the order of the table. The first match wins.

| Category (guess) | L1 | L2 | L3 | L4 | Identifying cue (example ids) |
|---|---|---|---|---|---|
| Letter substitution | 3 | – | – | – | "Take away the c" (11030, 11046, 11047) |
| Rhyming words | 6 | 7 | 5 | – | "rhyme" (11001) |
| Vowel sounds | – | 6 | 4 | 1 | "short i", "long a" (11063) |
| Beginning sounds | 12 | 4 | 5 | – | "start(s) with / the same" (11007) |
| Ending sounds | 8 | 5 | – | – | "end(s) with", "End Sound" (11000) |
| Analogies | – | 4 | 4 | 6 | "… is/are to … as" (11093, 11358) |
| Opposites | 3 | 6 | 6 | – | "opposite", "Up is down" (11020) |
| Compound words | 2 | 3 | 2 | 1 | "Pan + cake" (11016) |
| Numbers and arithmetic | 16 | 33 | 25 | 13 | all-numeric choices, "how many", coins (11009) |
| Sequencing | – | 2 | 1 | – | "What do I do first?" (11116) |
| Comparisons | – | 3 | 2 | – | "-est" choices, "more than" (11070) |
| Riddles | 1 | 3 | 3 | 1 | "I am thinking of", "What am I?" (11004) |
| Classification / odd one out | 6 | 13 | 14 | 9 | "same", "not" (11012, 11025) |
| Cloze (vocabulary, science, reasoning) | 4 | 21 | 30 | 16 | a `____` blank and no other cue |
| Other questions (science, reasoning) | 2 | 6 | 22 | 10 | remainder |
| **Total** | 63 | 116 | 123 | 57 | |

The per-record guesses are in the JSON (`category_guess`). Level 1 has mostly phonics and
small-number problems. Levels 3-4 move to longer science and nature passages, place value
and analogies [obs from the counts, inf as a design intent].

---

## 6. Game data and state

### 6.1 Static tables in DGROUP [obs]

| Offset | Type | Content |
|---|---|---|
| `0x1990` | u16[359] | `speechBase[i]`: first SSND id of puzzle *i* (4000 … 6608). It increases monotonically; `speechBase[i+1] - speechBase[i]` = text lines + 3 (or + 0 for nine of the no-choice-speech puzzles). One exception: 11225 (base 5613, 5 lines) has one extra trailing id, 5621. Its PCM equals 5620 (the third-choice recording, 17816 samples) except for the very last sample byte [obs], so it is an unused duplicate. The ids the game uses for 11225 are still correct. The last puzzle (11358, base 6608) ends at 6615, the highest SSND id in `TM8SOUND.DLL` [obs]. |
| `0x1C60` | u8[359] | Voice bank 1..3 per puzzle. |
| `0x1DC7` | u16[16] | Puzzle indices whose choices are not spoken (5.1). |
| `0x1DE7` | u16[5] | Level start indices `{0, 63, 179, 302, 359}`. |
| `0x1DF1` | far*[4] | Correct-answer messages → DGROUP `0x1E69` ("Great job…"), `0x1EA1`, `0x1ED3`, `0x1F21`. Each starts with `FF05FF` and uses LF line breaks. |
| `0x1E07` | far*[4] | Wrong-answer messages → `0x1F5B` ("Sorry!…"), `0x1F94`, `0x1FE0`, `0x200F`. Same format. |
| `0x1F59` | char[2] | `"."`, drawn after the clue word. |
| `0x1E01`, `0x1E17` | u16[3] | Speech stop codes `{0x1B, 0x0D, 0}` (Esc, Enter). |
| `0x1E1D`, `0x1E43` | u16[19] | Speech stop codes during puzzle reading: `{0x126,0x128,0x125,0x127, 0x0D, 0x409, 0x1B, 0xA80..0xA82, 0xA70..0xA76, 0xA88, 0}`. |

Mapping of the far pointers: NE relocations at `11b8:1df1/1df5/1df9/1dfd` and
`11b8:1e07/1e0b/1e0f/1e13` [obs].

### 6.2 Runtime state [obs]

| DGROUP | Type | Meaning |
|---|---|---|
| `5F82` | u8 | Puzzle level 0..3 = levels 1..4; **4 = all puzzles (0..358)**. 0 for a new player (`1160:166c`). |
| `5F83` | u8[359] | History per puzzle: **0** never answered, **1** right on the first attempt, **2** last attempt wrong (not yet solved), **3** solved after at least one miss. **Bit 7** = dealt in the current trip (transient). |
| `5F3A` | Slot[9], 8 bytes each | The current trip's deal, slot = `path * 3 + elf` (path 0..2, elf 0..2). |
| `60EE`/`60F0` | u16 | Current level range [first, last] (inclusive). |
| `60F2/60F4/60F6/60F8` | u16 | Counts of history 0/1/2/3 in the range. |
| `1078` | u8 | Clue words won on the current path (0..3). The next clue word index. |
| `60FA` / `60FC` | u16 | Slot and puzzle being shown. |
| `6145` | u16 | Speech on for this screen (copy of `0B97`). |
| `6147` | u16 | 1 if the puzzle is in the no-choice-speech list. |
| `610E`, `610F`, `6102..`, `6115..`, `60FE` | | Parser outputs (4.3). |
| `6111/6113/6129` | i16 | Question box x, y, width. |
| `612B/612D[3]/6133/6135` | i16 | Choice box x, per-row y, width, height. |
| `613D/613F/6141/6143` | i16 | Speaker-button hot-spot rectangle. |
| `6139:613B` / `60EA:60EC` | far* | Loaded PMAP 2001 / loaded puzzler text (freed by `1150:13c6`). |

```
struct Slot {            // DGROUP 0x5F3A + 8*slot, little-endian
  u16 puzzle;            // +0 puzzle index 0..358, 0xFFFF = none (solved or empty)
  u8  perm[3];           // +2 perm[pos] = file choice index shown at display position pos
  u8  tried[3];          // +5 tried[pos] != 0: that display position was answered wrongly
};
```

### 6.3 Persistence (user record, `1160:0f28`) [obs]

The save routine writes, in each 0x62F-byte player record: `+0x0E` sound-effects flag
(`0B96`), `+0x0F` speech flag (`0B97`), `+0x11` u32 hint mask (`5F01`, section 9),
`+0x1F` `DAT_1078`, `+0x2F` level `5F82`, `+0x30..+0x196` the 359 history bytes **including
bit 7**, and `+0x197..+0x1DE` the 9 Slot structs. So a trip can be resumed with the same deal.
The load path is `Users_LoadSelected` (`1160:137d`, not a Ghidra function; `sys_resources_saves.md`
§6.6), the inverse of `1160:0f28` (open question 1 answered per ARCHITECTURE.md §7.1 C21).

---

## 7. Selection algorithm

### 7.1 RNG [obs]

`rand()` = `1000:0859`, the Borland LCG: `seed = seed * 0x015A4E35 + 1` (32-bit), return
`(seed >> 16) & 0x7FFF`. `srand(s)` = `1000:0842` sets `seed = s` (high word 0). The initial
seed is 1 (DGROUP `3044`). `1118:0000` calls `srand(timeGetTime())` at the top of each game
loop pass. Every "random number below N" in this module is computed as
`(int)((long)rand() * N / 0x8000)` with signed 32-bit arithmetic.

### 7.2 New trip: `1150:0327` (called by `1118:0000` when the scene is `0xFE`)

```
SetLevelRange()                                  // 1150:0000
for i in 0..358: status[i] &= 0x7F
CountStatuses()                                  // 1150:0057: n0,n1,n2,n3 over [first,last]
if level < 4:
    total = n0+n1+n2+n3
    if total*80 - n1*100 == 0 or total*80 < n1*100:   // n1 >= 80 % of the level
        level += 1; SetLevelRange(); CountStatuses()  // at most one step per trip
DealTrip()                                       // 1150:01d5

SetLevelRange(): if level < 4: first = start[level]; last = start[level+1] - 1
                 else:         first = 0; last = 358
```

### 7.3 Pool picker `1150:00a7(s, n, out[])` [obs]

```
avail = count[s]; count[s] = max(0, count[s] - n)          // global counters n0..n3
while n > 0 and avail > 0:
    r = rand() * avail / 0x8000                            // 0 .. avail-1
    i = first; seen = 0
    while seen <= r: if status[i] == s: seen++ ; i++        // full-byte compare: bit 7 excludes dealt ones
    i -= 1
    n -= 1; out[n] = i; status[i] |= 0x80; avail -= 1
if n > 0: recurse with s' = next(s), same n, same out      // next: 0→1, 1→2, 2→3, 3→0
```

The fall-back chain would recurse for ever if all four pools were empty, but every level has
at least 57 puzzles and a deal needs only 9.

### 7.4 Deal `1150:01d5` [obs]

```
pick[0..2] = Pool(0, 3)      // never answered        (fallback 1,2,3,0...)
pick[3..5] = Pool(2, 3)      // last attempt wrong    (fallback 3,0,1,...)
pick[6..8] = Pool(3, 3)      // right after a miss    (fallback 0,1,2,...)
for slot in 0..8: puzzle = 0xFFFF; perm = {FF,FF,FF}; tried = {0,0,0}
for i = 8 downto 0:                                  // random slot for each pick
    r = rand() * (i+1) / 0x8000;  slot = r-th (0-based) slot with puzzle == 0xFFFF
    slot.puzzle = pick[i]
for slot in 0..8:                                    // random choice order
    for c = 2 downto 0:
        r = rand() * (c+1) / 0x8000;  pos = r-th position with perm[pos] == 0xFF
        perm[pos] = c
```

Effects: a new player gets 9 never-seen puzzles per trip. Missed puzzles come back on later
trips, and so do puzzles that were solved only after a miss. Puzzles answered right first time
are re-dealt only after the other pools of the level run out. No puzzle appears twice in one
trip [obs].

### 7.5 Elves carry the slots [obs]

* `1178:0251(nElves)` runs on entry to a path. Elf *e* (< 3) gets `carriesPuzzle = 1` iff
  `slot[path*3 + e].puzzle != 0xFFFF` (`DAT_6396[e*9]`). Path = byte `0x23F2 + scene*0x1C`.
* A caught elf with a puzzle sets `DAT_2AB6 = e + 1` (`1178:0b1d`). The scene loop then calls
  `result = 1150:100a(path*3 + DAT_2AB6 - 1)` (`10e0:01b1-01db`) and stores the result in
  `DAT_0B9F`.
* After the puzzle: if the elf had no puzzle or the result is correct, the elf goes to state 4
  (leaves, [inf]). Otherwise `1178:01e3` re-initialises it, so it can be caught again [inf].

### 7.6 Carrying missed puzzles to the next path: `1150:0375(oldPath)` [obs]

`1118:0000` calls this when the player arrives on a new path. For `oldPath < 2`, each slot
`d` in `oldPath*3 .. oldPath*3+2` whose puzzle is not `0xFFFF` and whose history is 2 is
"swapped" with slot `d + 3` through three 8-byte struct copies:

```
tmp = slot[d];  slot[d+3] = tmp;  slot[d] = slot[d+3];     // as compiled
```

The copies are three calls of the Borland struct-copy helper `1000:02f7` with `CX = 8`. That
helper loads the **source** from the last-pushed far pointer (`LDS SI,[BP+6]`) and the
**destination** from the first-pushed one (`LES DI,[BP+0A]`). The pushes at `1150:03aa-03ef`,
in push order (destination, source), are (tmp, slot[d]), then (slot[d+3], tmp), then
(slot[d], slot[d+3]). Slot `d+3` is at `0x5F52 + 8*d` = `0x5F3A + 8*(d+3)` [obs].
The compiled sequence is a **broken swap**: slot `d+3` becomes a copy of slot `d` (including
`perm` and `tried`), and slot `d` is unchanged. The puzzle that was in `d+3` is dropped from
this trip. Its history byte keeps bit 7 until the next deal, so it simply comes back on a
later trip. Reproduce this behaviour exactly: the missed puzzle follows the player to the
same elf position on the next path, and the old path still holds it.

---

## 8. Presentation and answering: `1150:100a(slot)`

### 8.1 Flow [obs]

1. `DAT_60FA = slot`, `DAT_60FC = slot.puzzle`, `DAT_6145 = speechOn (0B97)`.
   `DAT_6147 = puzzle ∈ 0x1DC7 list`. Save and clear `DAT_0862`. Save the state of
   `1078:01b1` and set it to 0. Call `10e0:0063(1)` and `1128:00d6`.
2. `6139 = GetResource(2001)` (PMAP, speaker button). `60EA = GetResource(11000 + puzzle)`.
   `Parse(60EA)` (4.3).
3. `10e8:0658(1)`, `Draw()` (8.2). Focus = `NextFocus(-1)` (the first untried choice), drawn
   with `Highlight(focus, 1)`. Present the play area `1058:05bc(0, 20, 512, 267)`. Flush input.
4. Event loop on `Input_GetCode` (8.3) until an answer or Esc.
5. If an answer was chosen: `CheckAnswer(pos)` (8.5). On a correct answer award coins by the
   number of tried flags: **0 → +2, 1 → +1, 2 → +0** (`10e8:06ff(2|1, 0)` adds to `DAT_5E5A`,
   the coin count).
6. Clean up: free the resources (`13c6`), remove hot spots `0xA7F..0xA82`, `0xA70+k` and
   `0xA88`, and restore the saved states. Set `DAT_13CD = 0xFF`. Return **1 = correct, 0 =
   wrong or Esc**. The result local is `[BP-0x0A]`. `CheckAnswer` sets it (`1150:12d9`), and
   the Esc handler sets it to 0 (`1150:118a`) before it ends the loop [obs].

### 8.2 Layout (`1150:0a97`) [obs]

Coordinates are in the 512 × 384 logical screen. The play area is `x 0..511, y 20..286`
(`0x14`, height `0x10B`). `lineH` = font height.

| Item | Rule |
|---|---|
| Background | PMAP **2000** drawn at (0, 20) with `1058:0cf3` and flag 0, i.e. **opaque** (`1150:0ab4 PUSH 0`; corrected per ARCHITECTURE.md §7.1 C16; an earlier version said "white transparent"). |
| Choice row height | `rowH (6135) = lineH + 6`. Row pitch = `rowH - 2 = lineH + 4`. |
| Question top | `qy (6113) = (267 - (DAT_610e*lineH + 3*(lineH+4))) / 2 + 20` (signed /2). |
| Question box | `qw (6129) = Text_Width(question) + 6`, rounded **up** to even. `qx (6111) = (512 - qw)/2`, rounded **down** to even. Drawn with `Text_DrawBox(qx, qy, qw, question)` in EGA 0. |
| Choice *pos* (0..2) | `t = choice[perm[pos]]`, `w = Text_Width(t)`, `x = (512 - w)/2`, `y = DAT_610e*lineH + qy + (lineH+4)*pos`. Drawn with `Text_Draw(x, y, t)` in **EGA 14** (yellow) if untried, **EGA 8** (dark grey) if tried. `612D[pos] = y - 3`. |
| Choice box | Common to all rows: `612B = x_of_widest - 4`, `6133 = widest + 8`. Each row box is `(612B, 612D[pos], 6133, rowH)`. |
| Hot spots | `0xA7F` = whole play area (0, 20, 512, 267). `0xA80 + pos` for each untried position = its row box. If speech is on: `0xA70 + k` for `k < DAT_610f` = `(qx, qy + k*lineH, qw, lineH)` — this does **not** skip empty lines, unlike the highlight. `0xA88` = speaker button. |
| Saved copy | `1058:0b6a(0,20,512,267 → 0, 0x194)`: the clean screen is copied 384 px below in the back buffer. Highlights are removed by copying back (`1150:0e30`). |
| Speaker button | Only with speech on. PMAP 2001 at (438, 220) = `(0x1B6, 0xDC)`. Hot spot `(0x1B2, 0xD9, imgW+8, imgH+6)`. |

Highlights (`1150:0d2d(item, mode)`) [obs]:
* Choice with mode 1 (keyboard focus): two nested 1-px frames in EGA 4 at the row box and at
  the box inset by 1.
* Choice with mode 0 (being read aloud): colour remap `1058:19b2` of
  `(612B, 612D[pos]+3, 6133, lineH)`. The remap turns every colour into EGA 14, black into
  EGA 1, EGA 4 into EGA 6 and EGA 14 into EGA 1 (a yellow "marker").
* Text line *k* (item `10 + k`): the same remap of `(qx, qy + 6115[k], qw, lineH)`.
* Speaker focus (`1150:0ed6(1)`): two nested frames in EGA 4 around the speaker rectangle.

### 8.3 Input [obs]

Focus values: `0..2` = choice positions, `10..10+DAT_610f-1` = text lines (only with
speech on). There is also a separate flag `spk` = focus on the speaker button.

| Input code | Action |
|---|---|
| `0x0D` Enter | `spk` → read everything (8.4). Focus ≥ 10 → speak that line. Else **select the focused choice**. |
| `0x1B` Esc (also window close) | Leave with result 0. History and `tried` are unchanged. |
| `0x126` Up, `0x2126` | If not `spk`: `focus = PrevFocus(focus)` (`1150:0fcb`). |
| `0x128` Down, `0x2128` | If not `spk`: `focus = NextFocus(focus)` (`1150:0f85`). |
| `0x125` Left, `0x127` Right, `0x2125`, `0x2127` | If speech on: toggle `spk`. |
| hot spot `0xA80+pos` with flag 0x8000/0x4000 (button down) | `focus = pos`, `spk = 0`. |
| hot spot `0xA80+pos`, no flag or 0x2000 (button up) | If `pos == focus` and not `spk`: **select**. |
| hot spot `0xA70+k`, down / up | Focus on line k / speak line k (`1409(k)`). |
| hot spot `0xA88`, down / up | `spk = 1` / read everything. |

The `0x2125..0x2128` codes are right-button releases on hot spots with ids `0x125..0x128`.
No code registers hot spots 0x125..0x128, so these entries are dead (open question 3 answered per
ARCHITECTURE.md §7.1 C21).
`NextFocus(f)`: if `f ≥ 10` and `f+1 < 10+DAT_610f`, return `f+1`. Otherwise cycle through the
untried choices 0→1→2. After 2 come the text lines (`10`) when speech is on, else 0.
`PrevFocus` is the mirror image (choices 2→1→0, then the last line `9+DAT_610f`).

Redraw after every event (`1150:1273-12be`) [obs]:
* `spk` unchanged and focus changed: `Unhighlight(old)`, then `Highlight(new, 1)`.
* `spk` turned off: `Highlight(focus, 1)`, `SpeakerFocus(0)`.
* `spk` turned on: `Unhighlight(focus)`, `SpeakerFocus(1)`, `focus = NextFocus(-1)`.

### 8.4 Speech of the puzzle (only when `0B97` is set) [obs]

* Line *k*: `PlaySpeech(speechBase[puzzle] + k, stopCodes@0x1E1D)` (`1150:1409`).
* Choice at position *pos*: `PlaySpeech(speechBase + DAT_610f + perm[pos], stopCodes@0x1E43)`
  (`1150:14d7`). The id uses the **file** index. Skipped when `DAT_6147`.
* Read everything (`1150:1449`): for each text line, highlight it (mode 0), speak it and
  unhighlight it. Then wait **250 ms**. Then for each untried position: wait **500 ms**,
  highlight, speak, unhighlight. Stop as soon as a `PlaySpeech` returns nonzero (interrupted by
  one of the stop codes [inf from the return-value use]). Busy-wait timing uses `timeGetTime`
  (`10a8:0000`).
* SSND ids 4000-6615 are in `TM2SOUND`..`TM8SOUND.DLL`, 400 ids per DLL (4000-4399 in TM2, …,
  6400-6615 in TM8) [obs].

### 8.5 Answer check `1150:0403(pos)` [obs]

```
if perm[pos] == 0:                                  // correct
    status[puzzle] = (no tried flag set) ? 1 : 3     // full byte: also clears bit 7
    slot.puzzle = 0xFFFF
    RewardScreen(); NextClue()                        // 1150:049a, 10e8:06e7
    return 1
else:                                               // wrong
    status[puzzle] = 2                              // clears bit 7
    tried[pos] = 1
    WrongScreen()                                   // 1150:0748
    return 0
```

There is **one attempt per catch**. After a wrong answer the screen closes, the elf keeps
the puzzle, and the next catch shows it again with the tried choice greyed out. With two
wrong choices tried, the remaining choice is the answer and is worth no coins.
`NextClue()`: `DAT_1078++`. When it reaches 2, request hint 3 (section 9).

### 8.6 Reward and wrong-answer screens [obs]

Reward (`1150:049a`):
1. `praise = rand()*4/0x8000` (0..3). Play SFX `7000 + 12` = **7012** (`1170:0000`, only if
   `0B96`).
2. Draw PMAP 2001 at (438, 220), then PMAP **2011** (scratch load) at (33, 60).
3. `Text_DrawBox(33, 136, 416, praiseText[praise])` in EGA 0 (`0x1DF1` table).
4. Clue word: far pointer at DGROUP `0x5E84 + (path*3 + DAT_1078)*4`. Drawn in **EGA 4**
   centred on x = 241 (`x = 0xF1 - w/2`), at `y = 136 + lineH * (praise == 2 ? 3 : 2)`. Then
   `"."` (DGROUP `0x1F59`) in EGA 0 right after it.
5. Present, then loop on `1088:0175()` until it returns 0. That function pumps
   `MM_WOM_DONE` (0x3BD) and returns `DAT_09F8 != -1`, i.e. "a wave is still playing" [obs].
   If speech is on: hot spot `0x0D` covers the whole screen (512 × 384), so any click produces
   the code `0x0D`, the same as Enter, which is a speech stop code. Then
   `1150:06ac(voice, praise, clueSnd)` plays `bank + praise`, then
   `bank + clueSnd`. Here `bank = 7300/7400/7500` for voice 1/2/3 and
   `clueSnd = u16 DGROUP 0x5EB7[path*3 + DAT_1078]`. The second phrase is skipped if the first
   was interrupted.
6. If speech was not interrupted: wait for `0x0D`, `0x1B`, `0x20`, `0x409` or `0x2409`.

Wrong (`1150:0748`): `idx = rand()*4/0x8000`. Play SFX **7008**. Draw PMAP 2001 and 2011 as
above, then `Text_DrawBox(33, 136, 416, wrongText[idx])` (`0x1E07` table). If speech is on,
play `bank + 4 + idx` (7304/7404/7504 + idx, `1150:0891`). Wait as above.

Per voice bank (TM1SOUND.DLL 7300-7352, 7400-7452, 7500-7552): `+0..3` praise phrases
matching the 4 texts, `+4..7` wrong-answer phrases, and higher ids = clue words indexed by
`0x5EB7` [inf for the clue-word part; the clue tables belong to the clue/scene spec].

---

## 9. Hint balloons 11500-11532

Format [obs]: `CR FF05FF line {CR line}` NUL. A leading newline, then centred lines separated
by CR (sometimes an empty line). Glyph bytes appear only in 11502 (`84 85`), 11503 (`8B`) and
11509 (`82`). No other codes.

Display (`1130:0068(n)`) [obs]: PMAP 2011 at (33, 60), PMAP 2010 at (430, 230), then
`GetResource(327)` = BNDL listing the 33 ids 11500..11532 (`"BNDL"`, u32 count = 33, u32 ids).
The text is `bundleEntry(n)` (`1038:0061`). Hot spot `0x4B0` covers the full screen.
`Text_DrawBox(83, 92, 321, text)` in EGA 0. Then `1130:0166(n)`: if speech is on, play SSND
**7100 + n** with stop codes `{0x0D, 0x1B, 0x4B0, 0}` (DGROUP `0x1414`). If not interrupted,
wait for `0x0D`, `0x409`, `0x1B`, `0x4B0` or `0x24B0`.

Note: because of the leading CR, the first line is one line below the box top. Long lines
would wrap at 321 px without re-centring (3.3).

Request (`1130:0000(n)`) [obs]: one hint can be pending (`DAT_13CE`, −1 = none). If none is
pending: `bit = hintBit[n]` (i16[34] at DGROUP `0x13D0`). If `bit < 0`, the hint always
shows. Otherwise it shows only if bit `bit` of the u32 mask `DAT_5F01` is set, and that bit
is then cleared. The mask is saved at user record +0x11, which makes it a "show once" set.
The scene loop `10e0:007d` displays the pending hint and resets `DAT_13CE`.

`hintBit` = `{0..9 → 0..9, 10 → −1, 11 → 10, 12 → 11, 13 → 12, 14 → −1, 15 → 13, 16 → 14,
17 → −1, …, 30 → 23, 31 → 24, 32 → −1, 33 → 25}` [obs]. All "Good job, *rank*" messages
are repeatable. Promotion and "As a *rank*" messages show once.

Triggers [obs] (all 14 far call sites of `1130:0000` are known, from the relocation scan):

| n (id) | Trigger |
|---|---|
| 0 (11500) | `10e0:014b`, every scene-loop pass (shows once) |
| 1 (11501) | **never requested** in the Windows EXE |
| 2 (11502) | `10e0:0155`, every pass after hint 0 |
| 3 (11503) | `10e8:06f8`, second clue word of a path |
| 4 (11504) | `10e8:074a`, treasure found |
| 5/6/7 (11505-07) | out of nets with coins left / out of coins with nets left / out of both (`10e0:0a7b-0ad6`, `1168:08c4-091f`; coins = `5E5A`, nets = `5E5C`) |
| 8 (11508) | `10e0:0d79`, at the lock without the key [inf from text] |
| 9 (11509) | `10e0:078e`, key found |
| rank tables | `0x0E8F` = {11,13,16,19,22,25,28,31}: trip start by rank (`10e0:10b2`, scene 9 with `DAT_65CE != 0`). `0x0FC8` = {−1,12,15,18,21,24,27,30}: promotion. `0x0FD8` = {10,14,17,20,23,26,29,32}: trip ended without promotion (`10f0:07f5`, chosen by `DAT_0B9C`). Rank index 0 trainee … 7 champion: `10e8:07a6(total treasures DAT_5E62)` at trip start and `DAT_5E78` at trip end. A promotion to rank 0 cannot happen [inf], so the `−1` entry (which would index `hintBit[−1]`, outside the table) is never used. |

Index 33 (bit 25, SSND 7133 exists) has no resource and no requester [obs].

---

## 10. Help pages 11600-11611

The Help menu (DGROUP `0x129A` "&Help" item list, handler far pointers at `0x12FD..0x130D`)
calls tiny stubs in segment 1180 [obs]:

| Stub | Call | Title (DGROUP) | Pages |
|---|---|---|---|
| `1180:0117` | `Help(11600, 4, 0x2D1C)` | About the Game | 11600-11603 |
| `1180:0128` | `Help(11605, 2, 0x2D2B)` | Using the Keyboard | 11605, 11606 |
| `1180:0139` | `Help(11607, 1, 0x2D3E)` | Using the Mouse | 11607 |
| `1180:014a` | `Help(11608, 4, 0x2D4E)` | Educational Focus | 11608-11611 |
| (5th item) | `1140:0000` | About Treasure Mountain! | – |

`Help = 1180:0000(first, count, title far*)` [obs]: it loads `GetResource(first + i)` into
`DAT_2CAA[i]` (far*[7]). The special case `first == 11605 && i == 1 → first + 1` gives the
same id, so it has no effect. The switch on `first - 11600` (table `1180:0105`) has only empty
cases. The routine fills the dialog descriptor at DGROUP `0x2CFE` (+0 title far*, +5 far* →
`0x2CAA` pages, +9 u8 page count, +0x12 far* → `0x2CC6` per-page pictures, which it clears,
+0x16/+0x1A per-page byte tables) and runs the paged dialog `10c0:0cde`. That dialog steps
through the pages with buttons (hot spots `0x44C`/`0x44D`, apparently Next/Previous [inf]) and
draws page *i* with `Text_DrawBox(…, pages[i])` (`10c0:0764`) [obs]. Afterwards the routine
frees the pages.

**11604 is never loaded by the Windows EXE** [obs]. It is the same as 11606 except for the
last action word ("Quit" vs "Exit"), so it is probably the Mac variant [inf].

Format [obs]: plain paragraphs. A single CR is a forced line break; the box width does the
wrapping. `FF02FF … FF02FF` underlines column headings (11605, 11607) or phrases (11609).
Two-column key tables are written as `label FE <spaces> value`: `FE` returns to the left
margin and the run of spaces pads out to the second column. Bullets in 11608 are
`'-' FE <4 spaces> text`. Glyphs `80..86, 8B` appear in 11604-11606.

---

## 11. Tool: `tools\assets\othr_puzzles.py`

```
python -P tools\assets\othr_puzzles.py [--res extracted\res\TMT.DLL\32515]
        [--exe extracted\wincd\TMTWINCD\TMTWINCD.EXE] [--out extracted\previews\othr_puzzles.json]
        [--sounds extracted\res]
```

* Reads the raw resources. If the EXE is present, it also reads the DGROUP tables of section
  6.1 and the hint tables. It finds DGROUP through the NE header (auto-data segment), not a
  hard-coded offset.
* Tokenises every payload with the dispatcher of 3.1/3.2. It emits a lossless `markup` string
  (`\n` = CR, `{color:5}`, `{ul}`, `{center}`, `{note:L=1}`, `{lm}` = 0xFE, `{g:82}` = glyph,
  `{{`/`}}` = literal braces) and **round-trips it back to the exact bytes** for all 404
  records.
* Puzzlers: an exact re-implementation of `1150:0901`. Per record: level (from the table and
  from the tag), header codes, `question_markup` as rendered, `display_line_count`
  (`DAT_610e`), `text_line_count` (`DAT_610f`), empty-line indices, per-line markup, plain
  text, emphasis runs, blank flag and speech id, the 3 choices (with `correct`, speech id or
  null), `speech` {base, ids_used, ids_allocated, voice, choices_spoken, choice_recordings},
  `category_guess`, and byte accounting.
* Hints: show-once bit, speech id, triggers, lines, glyphs. Help pages: dialog title, page
  number, stub address, lines.
* Speech cross-check (when `--sounds` points at the extracted `TMnSOUND.DLL\32513` folders):
  every SSND id that a puzzle uses (lines, plus choices unless in the `0x1DC7` list), every
  hint id `7100 + n` and the praise and wrong-answer phrases `7300/7400/7500 + 0..7` must
  exist. The last puzzle's span is bounded by the highest id in 4000-6999 (6615). Missing
  ids are listed per record (`speech.missing_sound_ids`) and in `summary.sound_check`.
* Result on the CD data [obs]: 359 + 33 + 12 records, **0 unparsed bytes, 0 non-zero padding
  bytes, 0 records with issues, 0 missing SSND ids**. Every level tag matches the EXE table.
  Every speech span matches lines (+3) except 11225 (+4, recorded as a note).

---

## 12. Re-implementation checklist

1. Load OTHR id → cut at the first NUL. Keep a private copy, because the parser edits it.
2. Text engine: 3.1-3.3 exactly. Colours are EGA numbers through the shared palette mapping.
   Underline mode hides `_`. Centring toggles. `FE` returns to the margin. Wrapping is greedy
   by words and never re-centres.
3. Puzzler parse: 4.3. Correct answer = file choice 0.
4. Per player persist: level (u8), history u8[359] (with bit 7), Slot[9], hint mask u32.
5. New trip: 7.2-7.4 with the Borland LCG if bit-exact deals are wanted. Path change: 7.6
   (broken swap).
6. Puzzle screen: 8.1-8.6. One attempt per catch. Coins +2/+1/0. Clue word = path clue
   `DAT_1078`, then `DAT_1078++`.
7. Speech ids: lines `base+k`. Choices `base+lines+fileIndex` unless in the 16-entry list.
   Praise/wrong/clue phrases from the voice bank. Hints `7100+n`.

---

## Open questions

1. ~~Where is the user record loaded?~~ **Answered (ARCHITECTURE.md §7.1 C21)**: `Users_LoadSelected`
   `1160:137d` (not a Ghidra function), the inverse of `1160:0f28` (`sys_resources_saves.md` §6.6).
2. ~~`1088:01a9` return value~~ **Answered (ARCHITECTURE.md §7.1 C21)**: 1 = interrupted by a listed
   stop code or by Ctrl+S (0x13), 0 = played to the end (`code_1058-1090.md` §9). (Whether a missing
   SSND returns 0 at once is moot: the nine puzzles without choice recordings are excluded by the
   list.)
3. ~~Who registers `0x125..0x128` and `0x409`?~~ **Answered (ARCHITECTURE.md §7.1 C21)**: 0x409 is
   the full-screen hot spot of `Hud_SetClueBoxMode` (`10e8:0658`) modes 1/2; 0x125..0x128 are never
   registered (dead table entries).
4. ~~Meaning of the calls that bracket the puzzle screen~~ **Answered (GAPS.md §5)**: `DAT_0862` =
   `g_dirtyEnabled` (record dirty rects), `1078:01b1/01b5` = `Menu_IsEnabled` / `Menu_EnableAll`,
   `10e0:0063` = `SetCursorForced`, `10e8:0658` = `Hud_SetClueBoxMode`, `1128:00d6/00dc` =
   `Music_Halt` / `Music_Resume` (music stops for the question and restarts from the beginning).
5. The clue-word tables `0x5E84` (far*[9]) and `0x5EB7` (u16[9]) are filled elsewhere (clue
   selection per path). Their source resources and the exact clue-word SSND numbering belong
   to the clue/scene spec.
6. ~~Glyph meanings `0x80..0x8B`~~ **Answered (GAPS.md §3.5)**: FFNT key-cap glyphs
   (`fmt_bndl_ffnt.md` §3.2): 0x80–0x83 arrows, 0x84/0x85 Spacebar, 0x86 Esc, 0x8B Enter, 0x8C Ctrl.
7. Hint 1 (11501, a doorway tutorial line) and the extra speech id 7133 are unused in the
   Windows build. Were they used by the Mac build's opening scene?
8. (Resolved) 11225 has 9 allocated speech ids for 8 used. Id 5621 duplicates 5620 (the PCM
   differs only in the last sample) and is never played. No open point remains.
9. The DOS edition keeps its puzzlers in `TM-DOS\ALL_PUZS` (a u16 offset table followed by
   data). It was not analysed here; it could confirm the content taxonomy or a type field.
