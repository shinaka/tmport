# DOS edition: non-picture data files and the EXE's data segment

Scope: everything in the 1990 DOS edition (`TM-DOS\`) that is not a picture or a sound:
the puzzles (`ALL_PUZS` + `PRG_DICT`), the help pages (`HELP.DAT`), the sign-in roster and
save records (`USERS.DAT`), the network/school files the EXE looks for, the text markup and
positioning used by all game text, and a map of the text and tables inside `SST.EXE`'s data
segment (DGROUP). Sound is in `fmt_sound.md`; pictures (`*.PAN`, `*.PAK`) are covered by
the graphics agent; game logic by the code agent.

Evidence: Ghidra export `re\dos\ghidra_export\` (addresses `SSSS:OOOO` as in that export);
a private headless project for code Ghidra had not disassembled
(`extracted\scratch\dos_data\ghidra\`). DGROUP is Ghidra segment `4d94`, load-image offset
`0x3d940`, file offset `0x3fb30` in `extracted\SST_unpacked.EXE`; "DS:xxxx" below means that
segment. Decoders: `tools\dos\puzzles.py`, `tools\dos\dos_text.py`,
`tools\dos\users_dos.py`; dumps in `extracted\previews\dos\data\`.

Labels: **[obs]** = read from bytes, disassembly or decoder output. **[inf]** = inference.
Game text is identified by offset with at most a few words quoted.

---

## 1. File table and loading [obs]

The EXE names its 18 data files in a table at **DS:0476** (`g_fileTable`), 22-byte records:
`name[13]` (NUL-padded), `disk` (u8: 1 or 2), 8 bytes never read by the loader; an all-zero
record follows at DS:0602. The engine reads it with `FileTable` (`src/dos/data/file_table.h`) and
looks every file up through it (`DosFiles`, §9).

| # | File | Disk | Loaded by | Into |
|---|---|---|---|---|
| 0-2 | mountain.pan, mount2n3.pan, mount3.pan | 2 | graphics | – |
| 3, 4 | close.pan, shack.pan | 1 | graphics | – |
| 5 | help.dat | 1 | `43cb:107a` (once) | DS:9325, max 0xED8 bytes |
| 6 | sprites.pak | 2 | `1437:0034` (scene change) | far buffer DS:37bc (0xC908 bytes, allocated in `1460:0003`) |
| 7 | all_puzs | 2 | `46ce:09ab` (every new game) | table → far buffer DS:37c4, texts after it |
| 8 | prg_dict | 1 | `46ce:095f` (start-up) | DS:b204, file length |
| 9 | users.dat | 1 | `47c7:0514` read / `47c7:055a` write | DS:ba14, 0xCAB bytes |
| 10-13 | sfx.ibm, tunes.ibm, sfx.tan, tunes.tan | 1 | `1460:022a` | static segments 415f / 41ca |
| 14-17 | opening.pan, begin.pak, close.pak, shack.pak | 1 | graphics (`1460:02c6` begin.pak; `1437:0034` loads index DS:8fca + 13 for scenes ≥ 3; `1460:05e8` loads .PAN files by index) | – |

* `1460:06c8(index, mode)` builds the path as `<dir> + name`, where `<dir>` is DS:036e for
  disk 1 and DS:03c9 for disk 2 (both default `A:\TLC\sst\`; `1460:08cc` replaces them with
  the directory of `argv[0]` on DOS 3+, or patches the drive letter). Modes are Turbo C
  flags: 0x8001 binary read-only, 0x8004 binary read/write, 0x8104 + create.
* On floppy drives A:/B: the loader first checks that the right disk is inserted
  (`150a:00c4`, §5). Reads retry on critical errors (`1460:07b4`, error dialog `153e:0137`).
* The disk numbers match the original two-disk set; `INSTALL.BAT` copies both disks into one
  directory, after which only the program directory matters.

---

## 2. ALL_PUZS and PRG_DICT: the elf puzzles

### 2.1 Format [obs]

* **ALL_PUZS** (43887 bytes) = 360 u16 offsets (0x2D0 bytes), then 360 NUL-terminated
  puzzle strings. Offset *i* is relative to the end of the table (puzzle 0 has offset 0,
  file position 0x2D0). The count 360 is fixed in code (`46ce:0d9a` loops to 0x168; the
  table read is 0x2D0 bytes in `46ce:09ab`).
* **PRG_DICT** (3328 bytes) = 208 entries of 16 bytes, each a NUL-padded word. Entries
  0..127 are used; 128..207 are empty. It is a **word-substitution table** for the puzzle
  text: in a puzzle string, any byte `b >= 0x80` stands for word `b & 0x7F`
  (expander `46ce:08f1`: copy bytes < 0x80, copy the dictionary word otherwise). Words are
  inserted without added spaces; the puzzle text supplies them. Examples: entry 0 is "the",
  entry 1 is a four-underscore blank, entry 88 is a back-quoted word (so emphasis marks can
  sit inside an entry).
* Quirk: PRG_DICT is read with its full file length into DS:b204, but USERS.DAT is later
  read into DS:ba14, 0x810 bytes further on, overwriting entries 129+. Only entries 0..127
  can be referenced, so nothing is lost [obs addresses; consequence inf].
* After expansion a puzzle is: 2 pad bytes (two spaces in all 360), the question lines
  (CR-separated, back-quotes mark highlighted words, see §6), `~`, then 3 answer lines each
  ended by CR (or NUL). **Answer 0 is the correct one.** 125 puzzles end with a Ctrl-Z (0x1A)
  before the NUL (editor residue). No puzzle uses `~XXXYYY`, `|NN` or TAB.

### 2.2 Presentation [obs]

* `46ce:0323` copies the question (up to `~`) to DS:a293 and the three answers to DS:a22a +
  35*n, and measures the widest answer.
* `46ce:02d9` picks one of 6 display orders, `rand() % 6`, from the table at DS:5e9c
  (`012 021 102 120 201 210`). The chosen slot is correct when its table value is 0.
* Each dealt puzzle has a choice mask (record +0x78, §4): bit *n* = answer *n* still shown,
  initially 7. A wrong pick clears that answer's bit (masks DS:5f8a = 1, 2, 4), so the next
  attempt offers fewer choices; picking the right answer also clears bit 0.
* Points (`46ce:05bb`) = DS:5f93[mask after the pick] = 2 if right first time (mask 6),
  1 after one miss, 0 after two. Right answer plays effect 10 and Flutter hint 15..18;
  wrong plays effect 11 and hint 19..22 (`46ce:069e`).

### 2.3 Levels and selection [obs]

* **Levels**: u16 table DS:5fd2 = `{0, 63, 180, 303, 360, X, 360}`: level 0 = puzzles 0..62,
  1 = 63..179, 2 = 180..302, 3 = 303..359. Entry 5 (`X`, DS:5fdc) is computed at load
  (`46ce:0d9a`) as the number of puzzles whose offset is at most `filelength - 0x7B00`
  (X = 123 for the shipped file); level 5 = puzzles X..359, whose text (30660 bytes) fits the
  single 0x7B00-byte read of `46ce:09ab` [obs; purpose inf]. Level 4 is never used.
* The player's level is record byte +0x13F. At each new game (`46ce:09ab`, `param = 0`),
  `46ce:0cd0` counts the level's puzzles by history state; if at least **70 %** are in
  state 1 the level goes up by one; after level 3 it becomes 5, and `46ce:0d60` clears the
  history from the start of the level just finished (puzzle 303) to the end of the array.
* **History**: 2 bits per puzzle in record +0x0C (90 bytes, puzzle *i* in byte *i*/4, bits
  `2*(i%4)`): 0 = never dealt, 1 = right first time, 2 = right after a miss, 3 = missed
  (`46ce:05bb` sets 1 or 2 when answered right, `46ce:069e` sets 3 on a wrong pick).
* **Deal** (`46ce:0b6a`, `46ce:0c39`): 9 puzzles per game. The wanted states per slot come
  from DS:5fe0 = `0 3 2 0 3 2 0 3 2` (3 unseen, 3 missed, 3 right-after-miss). When a state
  pool is empty, the slot and later slots of that state fall back via DS:5fec/5fe9:
  0→1, 1→0, 2→0, 3→2. A slot picks the (rand() % pool size)-th puzzle of its state in the
  level range, rejecting duplicates. The 9 indices go to record +0x66 (bit 15 = solved).
  Only the 9 dealt puzzles are expanded (into DS:a6b8.., far pointers at DS:a684).

### 2.4 Comparison with the Windows edition [obs, `puzzles.py --compare-win`]

The Windows CD puzzlers (TMT.DLL OTHR 11000-11358, `re\docs\fmt_othr_puzzles.md`) are a
revision of this set: 326 of the 360 DOS puzzles have an identical question and answers in
Windows, 33 more differ in wording, line breaks, one answer, or answer spacing (e.g. the
answer "3 inches" became "3"), and DOS puzzle 230 (level 2) was dropped (Windows has 359). Windows level starts are
`{0, 63, 179, 302, 359}`, the DOS ones `{0, 63, 180, 303, 360}`; some puzzles moved between
levels and positions. The same deal design (3 unseen / 3 missed / 3 right-after-miss,
fall-back chain, choice 0 correct) exists in both; DOS levels up at 70 % right-first-time,
Windows at 80 %. Note: `fmt_othr_puzzles.md` numbers the Windows history states
"2 = last attempt wrong, 3 = right after a miss", the opposite of the DOS values above.

---

## 3. HELP.DAT [obs]

* Layout: 4 bytes = pages per topic (`6 4 4 1`), then 15 u16 page offsets (absolute file
  positions, first = 0x22), then 15 NUL-terminated pages (3726 bytes total).
* Topics = the Help menu items whose titles are far pointers at DS:52ee (About the Game,
  Using the Keyboard, Using the Mouse, Getting More Hints).
* `43cb:10c4(topic, page, buffer)`: page index = sum of the counts of earlier topics + page;
  text drawn by the renderer of §6 at x = 0x0E, y = 0x25 with line height 10 (DS:5b3c is
  set to 10 for the page and back to 12 afterwards). Buttons: "Done"
  on a topic's last page, else "See Next Page"; "Go Back a Page" from page 1 on.
* Pages use `~XXXYYY` placement, CR line breaks and key glyphs; no `|NN` or TAB.
* Five parallel s16 tables of 15 entries, DS:5bed / 5c0b / 5c29 / 5c47 / 5c65, give per
  page two picture ids (-1 = none) and three placement values; pages 0, 1, 3 and 10 have
  pictures [obs values; meaning of the values: graphics agent].

---

## 4. USERS.DAT: sign-in roster and saved games [obs]

3243 bytes = 3-byte header + **9 records of 0x168 (360) bytes** at file offsets
`3 + 0x168*i` (offset table DS:6132; far pointers to the names at DS:6144). Read whole into
DS:ba14 (`47c7:0514`), written whole (`47c7:055a`, retried until no critical error). The
current player's record pointer is DS:9228; `47c7:09dd` derives the field pointers below.

**Header**: byte 0 is never referenced; **bytes 1-2 are one u16**, the word `g_saveHeapHigh`
DS:929f: `Game_Restore` loads it with a word move (47c7:0de0 `MOV AX,[ba15]; MOV [929f],AX`) and
`Users_Pack` stores it back (47c7:0f80 `MOV AX,[929f]; MOV [ba15],AX`) [O]. DS:929f is the
sprite save-heap end kept by `Sprites_DrawSorted` (4306:0664, `code_player.md` §3.5). The shipped
file has `00 A1 05` (DS:929f = 0x05A1).

**Record** (field names marked * are inferred from use):

| Off | Size | Field | Runtime copy | Evidence / notes |
|---|---|---|---|---|
| 0x000 | 9 | name, NUL-terminated (≤ 8 chars); empty = free slot | – | roster `47c7:0452`, list `47c7:01e0` |
| 0x009 | 1 | saved game: 0 = start a new game, else continue | – | `47c7:08b5`; cleared at game end `47c7:09a2` |
| 0x00A | 2 | total treasures (sign-in sort key; rank) | ptr DS:9138 | ranks DS:629d (§7.5); < 5 switches New Player Hints on (`47c7:0b19`) |
| 0x00C | 90 | puzzle history, 2 bits × 360 | ptr DS:8fd2 | §2.3 |
| 0x066 | 18 | 9 × u16 dealt puzzle (0xFFFF none, bit 15 solved) | ptr DS:921a | `46ce:0b6a`, `47c7:0bd6` |
| 0x078 | 9 | choice mask per dealt puzzle (7 = all) | ptr DS:9107 | §2.2 |
| 0x081 | 9 | clue words: 3 levels × (number, size, object) word-table indices, bit 7 set until earned | ptr DS:91fd | `46ce:079c` assigns, `46ce:06f0` clears bit 7 |
| 0x08A | 48 | table set to 0x80 for a new player | ptr DS:927e | `47c7:0a8b` |
| 0x0BA | 1 | unused | – | |
| 0x0BB | 57 | spot table A*: bytes ↔ u16 array DS:0caa via index tables DS:11aa (48) and DS:120a (9) | DS:0caa[] | `47c7:0c62` / `47c7:0e0e` |
| 0x0F4 | 48 | spot table B*: bytes ↔ u16 array DS:0faa via DS:11aa | DS:0faa[] | same |
| 0x124 | 12 | 6 × u16 = `g_levelPicData` DS:122e (u16[3][4]) elements {1,2,5,6,9,10} (index lists DS:62ab/62b1): per level the key picture and the unused word | DS:122e[] | same |
| 0x130 | 3 | zeroed for a new game | ptr DS:9209 | `47c7:0bd6` |
| 0x133 | 12 | zeroed for a new player | ptr DS:8fb6 | `47c7:0a8b` |
| 0x13F | 1 | puzzle level (0..3, then 5) | ptr DS:9101 | §2.3 |
| 0x140 | 2 | → DS:9130 | | |
| 0x142 | 2 | → DS:928f, current mountain level 0..2* (clue-word row) | | `49a6:0121`, `46ce:06f0` |
| 0x144 | 2 | → DS:9134 (new player 5) | | |
| 0x146 | 2 | → DS:910f | | |
| 0x148 | 2 | → DS:9113 | | |
| 0x14A | 2 | → DS:9120 (new game 8) | | |
| 0x14C | 2 | → DS:90f8 (new game 4) | | |
| 0x14E..0x155 | 4 × 2 | → DS:37e2, 37e4, 37ea, 37ec (new player 0x140, 0x70, 0xC0, 0)* screen/scroll positions | | `47c7:0a8b` |
| 0x156 | 2 | → DS:910b, room/scene number | | music choice `43cb:03cb` |
| 0x158 | 2 | → DS:92a1 (new game 4) | | |
| 0x15A | 2 | banked nets for the next game: written only by `Game_End` (= `g_nets`) and `Users_InitNewPlayer` (10), read only by `Users_LoadPlayer` (→ DS:910f); `Users_Pack`/`Game_Restore` do not touch it | | `47c7:09c2`, `0aad`, `0b3b` |
| 0x15C | 2 | banked coins: written only by `Game_End` (= `g_coins`) and `Users_InitNewPlayer` (5), read only by `Users_LoadPlayer` (→ DS:9134) | | `47c7:09ce`, `0a9e`, `0b2f` |
| 0x15E | 2 | music on (DS:63b6) | | loaded by `47c7:0c62`; `47c7:0b19` takes it only while DS:63b6 ≠ 0 |
| 0x160 | 2 | sound on (DS:63b4) | | as 0x15E |
| 0x162 | 2 | games played (DS:926e), +1 at game end | | `47c7:09a2` |
| 0x164 | 2 | → DS:8fb0 (new player 0xFFFF) | | |
| 0x166 | 2 | → DS:8f9e (low byte only) | | |

`users_dos.py` decodes the shipped file: slot 0 = a player "Rascal" with a saved game
(room 5, puzzle level 0, 9 dealt level-0 puzzles, clue words not yet earned); slots 1-8 are
all zero.

**Comparison with the Windows record** (`re\docs\ARCHITECTURE.md` §4.3, 7 × 1583 bytes
after a `"USR\x1A"` header):

| Item | DOS | Windows |
|---|---|---|
| Player slots | 9 | 7 |
| Name | +0x000, 9 bytes | +0x000, 9 bytes |
| Byte +0x009 | saved-game flag | rank tier |
| Total treasures | +0x00A, u16 | +0x00A, u32 |
| Settings | music +0x15E, sound +0x160 (u16); hints derived from treasures < 5 | music&sound / voice / hints bytes at +0x00E..0x010 |
| Puzzle history | 2 bits × 360 at +0x00C | 1 byte × 359 at +0x030 |
| Dealt puzzles | 9 × u16 at +0x066 + 9 masks at +0x078 | 9 × QuizSlot at +0x197 |
| Puzzle level | +0x13F (0..3, 5) | quiz tier +0x02F (0..4) |
| Clue words | 9 bytes at +0x081 | clue mask/values/plural at +0x020..0x02E |
| Spots | 57 + 48 bytes at +0x0BB / +0x0F4 | 150 + 150 bytes at +0x500 / +0x596 (50 rooms × 3) |
| Room | u16 +0x156 | byte +0x1DF |
| Games / climbs | games played +0x162 | climbs completed +0x62D |

The two records are not compatible; the shared ideas are the name, the treasure total at
+0x0A, a 2-state-bit puzzle history, 9 dealt puzzles and 3 × 3 clue words.

---

## 5. Network and school files [obs]

Start-up (`1460:0003`, code at `1460:011f..01ca`) builds paths in the program directory:

1. `ssrnet.dat`: if it exists (`access()` = `4c9e:0005`), DS:061a = 1 (network allowed).
2. `ssrquiet.dat`: if it does **not** exist, nothing more happens. If it exists and DOS is
   3.10 or later, the game asks `1460:05a3` whether the current drive or the program drive is
   remote; if either is and `ssrnet.dat` was absent, it prints the network
   message (DS:070b) and exits.
3. `1460:05a3` is **stubbed**: it returns 0 at once; the original body after it (dead code
   at `1460:05ad`) used INT 21h AX=4409h and tested bit 12 of DX ("drive is remote"). So the
   network refusal can never trigger in this build.

`TLCINFO.DAT` (`150a:0002`): read from the root of drive A: or B: (`"X:\"` + name, drive
letter patched in), 0xE9 bytes into DS:07ab. Used only when the game runs from a floppy
(`150a:00c4` returns OK at once for other drives, or when DS:07aa is 0):

* to recognise the inserted disk: the string at file offset 0xC0 (DS:086b) must equal the
  expected label DS:0424 ("Disk 1") or DS:044d ("Disk 2"), else the "Please insert Disk n"
  prompt loops (`150a:00f5`, `150a:0244`);
* at start-up, if file byte 0 is 8, both labels are replaced by "Program Disk" (DS:06e6)
  [inf: single-disk 720 KB media].

**When the files are absent** (the user's installation): `ssrnet.dat`/`ssrquiet.dat` absent
→ no network test at all; `TLCINFO.DAT` absent → irrelevant on a hard disk. A port can
ignore all three.

---

## 6. Text encoding and positioning [obs, renderer `43cb:0b63`]

All game strings are 7-bit ASCII, NUL-terminated, drawn with the game's bitmap font by
`43cb:0b63(x, y, far string)` (returns the line count). Coordinates are in **2-pixel units
horizontally** (the 320-pixel screen is 160 units; glyphs are not drawn at x ≥ 0x9A) and
pixels vertically [inf from the /2 and the clip value].

| Byte(s) | Meaning |
|---|---|
| 0x00 | end |
| 0x0D or 0x0A | new line: x = left margin, y += line height DS:5b3c (callers set 10 or 12) |
| `~XXXYYY` | 7 bytes: x = XXX / 2 and left margin = that x; y = YYY (decimal digits). XXX is a pixel column 0..319, YYY a pixel row |
| `` ` `` | toggle highlight colour (pattern 0x4444) / normal colour (DS:9215) |
| `\|NN` | colour NN (2 decimal digits), stored as `NN * 0x11` |
| 0x09 `NN` | tab: x += NN (2 decimal digits, 2-pixel units) |
| 0x01..0x1F (others) | key-symbol glyphs of the game font, e.g. 0x1B/0x1C up/down, 0x1E/0x1D left/right, 0x07 0x06 space bar, 0x05 Enter, 0x02 menu key [inf from the sentences they appear in] |
| other | glyph `c & 0x7F` |

* Dialogs and menus use CR-separated lines in one string; each drop-down menu keeps all
  its items in one string (DS:5262 the Help menu, DS:52a7 the File menu, DS:52d1 the
  hints/music/sound settings menu [inf: menu names from hint 13 and the item texts]).
* Flutter hint balloons are drawn at x = 0x14, y = 0x22 (`49a6:0565`) unless the text starts
  with `~XXXYYY`; most longer hints and all rank messages carry one or more `~` moves.
* Some hints are patched before display (`49a6:026f`): the treasure count still needed is
  written as two digits at the offset DS:7d4d[rank] in the hint text, an `s` or space at
  DS:7d65[rank], and the champion's name (8 chars) into the "Today ... won back the crown"
  hint (DS:7b9d + 0x15).
* The intro and "other Super Solvers" screens (DS:8a5a..8d81) use LF line breaks, TAB
  indents and `|NN` colours.
* The Windows edition's markup is different (`0xFF op ... 0xFF` control codes); the DOS
  markup is a small fixed set and needs its own renderer.

---

## 7. SST.EXE data segment map

### 7.1 Addressing [obs]

* `SST.EXE` is EXEPACK-compressed; the engine's `tools\unexepack.py` restores a 297680-byte file (the 0x21F0-byte header plus
  the 288992-byte load image).
  Header 0x21F0 bytes, so a Ghidra address `SSSS:OOOO` is file offset
  `0x21F0 + (SSSS - 0x1000) * 16 + OOOO`, and DS:xxxx is file offset `0x3fb30 + xxxx`.
* DGROUP: 0x8FA0 bytes initialised (to the end of the file), BSS to 0xCC1F, stack at
  SS = DGROUP + 0xCC2 paragraphs (MZ SS:SP = 4a56:00e6 relative).
* Far pointers stored in DGROUP are **not relocated** in the file: segment 0x3D94 = DGROUP.
  `dos_text.load_image()` finds DGROUP from the entry point (`MOV DX,imm16` at CS:IP) and
  checks the Turbo C copyright at DS:0004.

### 7.2 Initialised data (DS:0000-8F9F)

| DS range | Content | Used by |
|---|---|---|
| 0000-00F5 | Turbo C RTL: copyright (0004), "Divide error"/"Abnormal program termination" (002f), argc/argv (006b/006d), DOS version (007d), errno (007f) | RTL |
| 00F6-00FF | treasure-room display state with its initial values (`g_shackSavedShown` .. `g_shackPartner2X`) | 176a |
| 0100-010F | CHAMPION flash timer (0100), slow-CPU box-saved flag (0102), screen painters far[3] (0104) | 130f |
| 0110-0139 | `g_rankTable`: u16 pairs (5,1) (25,8) (70,16) (115,24) (170,32) (230,40) (300,48) (400,56) (9999,66) (9999,86), 0xFFFF — treasure threshold and rank chest rows (`RankChest_Update`, end sequence) | 130f |
| 013A | "CHAMPION" | 130f |
| 0144-0173, 0174-0183 | MCGA palette (16 × RGB, 6-bit), CGA dither patterns (16 bytes) | graphics |
| 0184-036B | three tables of 4-byte hiding-place attribute records at 0184, 0250, 02ec (far pointers at 1246), per mountain level | clue logic `46ce:079c` |
| 036E, 03C9 | program / disk-2 directory buffers | §1 |
| 0424, 044D | expected disk labels | §5 |
| 0476-0617 | file table (18 × 22 and an all-zero record) | §1 |
| 0618-061B | saved video mode, boot drive, network flag, saved keyboard flags | `1460:0003` |
| 0620-0630 | `-S` letters and device map | `fmt_sound.md` §2.2 |
| 0631-0638 | far pointers to 036E / 03C9 | `1460:06c8` |
| 063C-07A8 | start-up messages (memory, graphics adapter, network), "Program Disk", ssrnet/ssrquiet names | `1460:0887` |
| 07AA-0893 | disk-check flag, TLCINFO.DAT buffer (0xE9) | §5 |
| 0894-08A4 | "X:\", "TLCINFO.DAT" | §5 |
| 08A6-0CA9 | far code-pointer tables into segments 1568, 176a, 18cf, and small u16 tables | code agent |
| 0CAA-12F1 | u16 arrays 0caa / 0faa (saved spot tables), index tables 11aa / 120a, words at 1225, far pointers 1246 | `47c7:0c62` |
| 1300-130B | machine info: 1300 machine id (0x21 Tandy), 1301 floppy count, 1302-1304 drive flags, 1307 BIOS equipment word, 1309 ISR busy flag | `1000:04a1`, ISR |
| 130C-3C1B | mostly zero: work buffers; small u16 tables at 150c and 2e4c | – |
| 3C1C-3D35 | second palette, EGA mapping and index tables | graphics |
| 3D36-4F59 | sprite animation scripts (u16 words; negative values are commands, -5 *id* = play effect *id*) | `4306:0022` |
| 4F5A-5261 | 194 far pointers to the scripts | `4306:0022` |
| 5262-59CB | menu item lists, dialog titles and texts, disk/error messages, "ONE MOMENT PLEASE" | 43cb, 47c7, 153e |
| 59CC-5C91 | menu/dialog layout data; help-page picture tables 5bed-5c82 (§3) | 43cb |
| 5C92-5D31 | button texts ("to Go Back", "Done", "See Next Page", ...) and help topic titles | 43cb |
| 5D32-5FF3 | puzzle and clue tables: 5e9c answer orders, 5eae 2-bit masks, 5eb2 word-table pointers, 5f86-5fb2 clue/answer tables, 5fd2 level table, 5fe0 deal template, 5fe9/5fec fall-backs, 5ff0 masks | 46ce |
| 5FF4-6131 | the clue/word strings (§7.4) | 46ce, 49a6 |
| 6132-6167 | record offsets and name pointers for USERS.DAT | 47c7 |
| 6168-62B6 | sign-in screen state, rank names 625d, thresholds 629d, index lists 62ab/62b1 | 47c7 |
| 62B7-63A3 | sign-in screen texts ("Select Player Name", "New Player", key prompts) | 47c7 |
| 63A4-6BBD | sound driver data (`fmt_sound.md` §5.1, §5.4) | 491b |
| 6BC0-7CA2 | Flutter hint texts | 49a6 |
| 7CA3-7D36 | 37 far pointers to the hints (§7.3) | `49a6:0565` |
| 7D37-7D7A | hint patch tables: thresholds 7d37, hint per rank 7d45, digit offsets 7d4d, plural offsets 7d65, end-of-game hint ids 7d75 | `49a6:026f` |
| 7D7B-7F2F | keyboard tables: accepted keys per screen, ASCII→scan-code pairs (7e2a) | input code |
| 7F30-8123 | screen hot-spot rectangles (8-byte records), far pointer arrays 80db / 80f3, answer-box rectangles 80b2 | `43cb:12cf`, `46ce:03ff` |
| 8124-8147 | OPL operator maps | 4ac9 |
| 816C-8A0E | 24 AdLib instruments, their pointers (88bc), orchestration rows (891c, 8988, 89f4) | `fmt_sound.md` §5.5 |
| 8A0F-8A4E | AdLib/intro work words | 4ac9, 4ba4 |
| 8A48-8D80 | "Press ... to go on", intro story pages, Super Solvers series screen | 4ba4 |
| 8D81-8E19 | small tables (not analysed) | – |
| 8E1A-8F9F | Turbo C RTL: ctype table, stdio, month tables, "EST"/"EDT" | RTL |

### 7.3 Flutter hints (DS:7ca3, 37 entries) [obs]

| Ids | Purpose (from the text and callers) |
|---|---|
| 0-13 | New Player Hints: introduction, movement keys, net, coins, treasure found, worn net, no coins, locked gate, key found, climbing, tunnel slide, ladders, File menu. One state byte per hint at DS:923e (15 bytes; 1 = waiting for its trigger, 0 = due, 0x80 = shown; set to 1 at `47c7:0b19`, triggers in `49a6:0121`, the highest due hint is shown by `49a6:022b`) |
| 14 | placeholder "msg 14" (never shown) |
| 15-18 | right answer + new clue word (`46ce:05bb` picks 15 + rand()%4) |
| 19-22 | wrong answer / missed (`46ce:069e` picks 19 + rand()%4) |
| 23-30 | rank messages for Trainee .. 6 stars and the crown (index from DS:7d45 by rank) |
| 31-34 | end-of-climb messages (none / one / some / all treasures; DS:7d75) |
| 35, 36 | champion messages (name patched into 35) |

### 7.4 Word lists (DS:5eb2, 53 far pointers) [obs]

| Index | Group |
|---|---|
| 0-8 | number words one..nine |
| 9-14 | digits 1..6 |
| 15-18 | shape words (4) |
| 19-28 | objects, singular (10) |
| 29-33 | objects that only occur in the plural (5) |
| 34 | empty string |
| 35-44 | the 10 objects, plural |
| 45-48 | size adjectives (4) |
| 49-52 | superlatives |

The clue words stored in USERS.DAT +0x81 are indices into this table (a number, a size and
an object per level).

**Hiding-place attribute records** (DS:0184 level 0, 51 records; DS:0250 level 1, 39;
DS:02ec level 2, 32; 4 bytes each; far pointers at DS:1246). `46ce:079c` reads attribute *a*
of record *r* as `(byte[r*4 + B[a]] & M[a]) >> S[a]` and turns value *v* (1-based) into word
`W[a] + v - 1` of the word table [obs]:

| *a* | Byte `B` (DS:5fa9) | Mask `M` | Shift `S` | Word base `W` (DS:5fae) | Words |
|---|---|---|---|---|---|
| 0 | 0 | 0x0F (DS:5f9b) | 0 (DS:5fa2) | 0 | number words |
| 1 | 2 | 0xE0 | 5 | 15 | shapes |
| 2 | 2 | 0x1F | 0 | 19 | objects (singular and plural, indices 19..44) |
| 3, 4 | 1, 3 | one of 0xC0/0x30/0x0C/0x03 (DS:5f9e), picked at random until non-zero | 6/4/2/0 (DS:5fa5) | 45 | size words |

Record 0 of each table is all zero (the record number comes from DS:9295 [inf: 1-based
hiding-place number]). Which three attributes a level uses comes from DS:5fb3 (3 bytes per
row). Example: level-0 record 1 = `61 40 21 44` reads as one / (shape 1) / tree / big [obs
decode; the high nibble of byte 0 is not read here].

### 7.5 Ranks [obs]

Names at DS:625d, 8-byte slots: Trainee, then 1 to 6 asterisks (stars), then the font glyph
0x1A (crown). Rank = number of thresholds at DS:629d (5, 25, 70, 115, 170, 230, 300) that the
total treasures (record +0x0A) reach. The string "CHAMPION" (DS:013a) is separate.

### 7.6 BSS (DS:8FA0-CC1F) [obs]

| DS | Content |
|---|---|
| 8FA0-9324 | game state variables (e.g. 9101/9107/91fd/921a/9228/9138 record pointers, 910b room, 912c video, 9136 start-up drive, 923c CPU flag) |
| 9325-A1FC | HELP.DAT buffer (0xED8) |
| A22A-A2F8, A293 | three answer strings (35 bytes each), question text |
| A684-A6B7 | 9 far pointers to expanded puzzles, history counts (a6ac), level range (a6aa, a224) |
| A6B8-B1F7 | expanded texts of the 9 dealt puzzles |
| B204-BA13 | PRG_DICT buffer (2064 bytes usable) |
| BA14-C6BE | USERS.DAT image (0xCAB) |
| C6C1-C7A3 | sign-in list work |
| C7A4-CB78 | AdLib driver state (channel flags c7a5, F-number table c7bd, ...) |

---

## 8. Decoder outputs

| Command | Output (in `extracted\previews\dos\data\`) |
|---|---|
| `python -P tools/dos/puzzles.py` | `puzzles.txt` (all 360 with level, offsets, dictionary use), `puzzles.json` (plus the Windows comparison) |
| `python -P tools/dos/dos_text.py all` | `prg_dict.txt`, `help_dat.txt` (pages + picture tables), `dgroup_text.txt` (file table, help titles, word table, ranks, hints, every string in DGROUP) |
| `python -P tools/dos/users_dos.py` | `users_dat.txt`, `users_dat.json` |

## 9. Porting notes [inf]

* Everything above can be read from the user's files at runtime. The DGROUP tables
  (hints, menus, dialogs, words, ranks, envelopes, levels) live in the EXEPACKed SST.EXE, so
  the backend needs an in-memory EXEPACK unpacker (a few dozen lines, the engine's `tools\unexepack.py`)
  and should verify the image (load image of 288992 bytes and the copyright at DS:0004) before using
  fixed DS offsets.
* The DOS edition has 9 save slots and its own record layout; the backend should keep its
  own save file rather than writing USERS.DAT (or write it only on request, in the exact
  layout of §4).
* The puzzle deal, history, choice masks and points are data-driven by the DS tables cited;
  reproduce the 70 % level-up and the fall-back chain exactly.
* Engine (wave 2, `src/dos/data/`): `DosFiles` opens the folder (case-insensitive names, the checks
  of `PORTING_NOTES.md` §1, errors that name the file); `FileTable` (§1); `PuzzleFile`,
  `Dictionary`, `splitPuzzle` and `QuizTiers` (§2: offsets, `Dict_Expand`, `Quiz_SplitText`, the
  0x7B00 windows and DS:5fd2 with word 5 computed); `HelpFile` and `helpPagePictures` (§3);
  `dgroup_offsets.h` (every DGROUP table and global of §7 under its canonical name).

## 10. Open questions

* ~~USERS.DAT fields 0x08A / 0x124 / 0x130 / 0x133~~: answered by wave 1 (collected treasure ids,
  per-level picture data, found-treasure masks, shelf bits; `ARCHITECTURE.md` §6, `code_ui.md` §9.8).
* ~~DS:929f~~: the high-water mark of the sprite save heap (`g_saveHeapHigh`, `code_player.md` §3.1).
* ~~DS:0100 pairs, code pointers at DS:08a6~~: the rank chest table DS:0110 and the cell handler
  tables (`code_world.md` §3.1).
