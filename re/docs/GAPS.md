# What is still missing before a faithful port: gap analysis and work list

Completeness review of the reverse-engineering corpus (`re/docs/*.md`, `re/names/*.tsv`) against
the Ghidra export (`re/win/ghidra_export/functions.tsv`, segment table) and the extracted
resources (`extracted/res/`). Each item says **what** is missing, **why** it matters for the port,
and **where** to look. Section 6 is the prioritised work list.

Method and scratch evidence (all in `extracted/scratch/synthesis/`):

* `coverage.py` → `coverage.txt`: every `functions.tsv` entry checked against every line of
  `re/names/*.tsv`; every code byte of every segment checked against Ghidra function extents.
* `rescheck.py` → `rescheck.txt`: every extracted resource classified against the format docs and
  against the ids the code (per the topic docs) and the live bundles actually request.
* Manual reading of all 17 topic docs; conflicting claims re-checked against bytes (listed in
  `ARCHITECTURE.md` §7.1).

Tags: **[O]** observed, **[I]** inferred.

---

## 1. Code coverage

### 1.1 Functions without a documented purpose [O]

| Group | Count | Bytes | Status |
|---|---|---|---|
| Ghidra functions in segments 1000–11a8 | 642 | – | **all 642 have a names-file entry** with signature and summary |
| Import thunks in segment 1248 | 122 | 488 | not in any names file; they are Ghidra's stubs for the 121 imports in `imports.tsv` (GDI 13, KERNEL 40, MMSYSTEM 12, TOOLHELP 2, USER 54). Purpose = the imported API; documented in the topic docs where used. No action needed. |
| Names entries with vague text | 0 | – | the only matches of "unknown" are RTL descriptions of `perror`/`signal` (false positives) |

Entry points that Ghidra never made into functions but the docs cover (each has a names entry):
`1000:` 36 RTL alternate entries; `1020:00b1`; `1038:0000`; `1050:0000`; `1058:0776`; `1068:00d5`;
`1090:03e6`; `1098:0130`, `0157`; `10c0:0cde` (help pager, 858 bytes, live); `1140:0000`, `0008`,
`0062`, `00c2`, `00cc`; `1148:0000–002e`; `1160:137d` (`Users_LoadSelected`, 693 bytes, live);
`1170:0082`; `1180:0117/0128/0139/014a`; RTL type descriptors in `11a0`/`11a8`.

### 1.2 Code bytes outside every Ghidra function [O]

13 755 of 111 839 code bytes (12.3 %) lie outside Ghidra's function extents. Classification:

| Region(s) | Bytes | What it is | Documented in |
|---|---|---|---|
| `10b0:01ce–053c` | 878 | body of `Text_Render` (`10b0:00b5`, Ghidra truncated it) + jump tables `0518`/`0524` | code_1098-10d8 §7, fmt_bndl_ffnt §8 |
| `10c0:0bff–1088` | 1161 | `Dlg_RunPager` `10c0:0cde` + tables `0c72`, `0cc6`, `1038`, `1040` | code_1098-10d8 §10.6 |
| `1178:0e25–1040` | 539 | switch cases 9–13 of `Elf_Beh5_Throw` (`1178:0cb0`) + jump table `101f` | code_1168-1198 §4.7 |
| `1160:137d–1632` | 693 | `Users_LoadSelected` | code_1150-1160 §5.7, sys_resources §6.6 |
| `1010:01dc–05c8` | 1004 | `MainWndProc` switch body + tables `0550/0558/0574/059e` | code_1008-1030 §5 |
| `1160:042c–0488`, `067c–0834`, `0dc8–0dd6` | 546 | tails of `02e0`/`0595` + key tables `046c`, `07b8`; switch table of `0cdb` | code_1150-1160 §5, sys_resources §6.7 |
| `1150:009f`, `01c5`, `132f–13c6` | 175 | switch tables of `0057`/`00a7`; tail of `100a` + key table `139e` | code_1150-1160 §3 |
| `1128:0031–00d6` | 165 | `Music_PlayZone` case code + tables `00a2/00bc` | code_10f8-1148 §10 |
| `1140:0000–0015`, `0062–008c`, `00c2–00f7`, `1148:*` | 163 | menu handlers, dead segment 1148 | code_10f8-1148 §13–14 |
| `10e0:0f9c–0ff4` | 88 | switch case bodies + tables `0fd4/0fe4` of `Player_Update` | code_10e0-10f0 §4.3.6 |
| `1080:02c2–02fa`, `0350–0388` | 112 | jump-table tails of two dialog procs | code_1058-1090 §8 |
| `1098:0130–017e` | 78 | dead `Hotspot_SaveAll/RestoreAll` | code_1098-10d8 §4 |
| small tables: `10b8:06d6` (14), `10c0:043a` (52), `10c0:0a10` (12), `10f8:0231` (16), `1138:0279` (10), `1168:00b2` (8), `1180:00ff` (92), `1198:06a6` (14), `1028:01e8` (14) | 232 | switch / jump tables and the Help topic stubs | respective docs |
| hidden small functions: `1020:00b1`, `1038:0000`, `1050:0000`, `1058:0776`, `1068:00d5`, `1090:03e6`, `1170:0082` | 161 | | respective docs |
| `10e8:003f`, `1050:02e1/04df/06a5/0897` | 12 | unreachable RET; epilogue tails | code_10e0-10f0 §1, code_1038-1050 §5.4 |
| segment 1000 RTL gaps (RTTI/EH tables, printf switch table, setvbuf, xmsg, signal, setargv, padding) | 3 897 | RTL | code_1000 §13 |
| segment 11a0/11a8 RTL gaps (EH tables, type descriptors, hidden entries) | 3 800 | RTL | code_11a0-11a8 §2 |
| **not mentioned anywhere**: `11a0:1675–1689` (20), `11a0:1de7–1df1` (10), `11a0:2bd9–2beb` (18), `1000:3605–3608` (3) | 51 | epilogue tails (`… call; pop di; pop si; leave; dec bp; retf`) of the preceding RTL functions [O bytes] | – (RTL, irrelevant) |

**Conclusion [O]:** no game code is undocumented. The remaining gaps are RTL epilogues. However,
the Ghidra export itself is incomplete (truncated bodies, missing functions), which forced every
analyst to write private disassembly scripts; see work item P1-6.

---

## 2. Resource coverage

### 2.1 Formats [O]

Every resource type in every DLL has a format spec; no id range is of unknown format:

| DLL / type | Count | Format doc | Notes |
|---|---|---|---|
| all DLLs, type 15 | 12 | RT_NAMETABLE, `sys_resources_saves.md` §5.1 | |
| TMT16/TMT256 PMAP | 764 + 764 | `fmt_pmap.md` | all decode; RUND re-encodes byte-identically |
| TMT.DLL BNDL | 79 | `fmt_bndl_ffnt.md` §2 | |
| TMT.DLL FFNT | 1 | `fmt_bndl_ffnt.md` §3 | |
| TMT.DLL OTHR | 476 | `fmt_othr_tables.md` (10550–10555, DAM 20000–20308, story 20105–20108), `fmt_othr_puzzles.md` (11000–11611) | 4+1+51+3+4+4+5 + 359+33+12 = 476 |
| TMT.DLL RRGB | 1 | `fmt_pmap.md` §5 | |
| TM0–8SOUND SSND | 2861 | `fmt_sound.md` §3 | |
| `SOUNDS\*.MID` | 20 | `fmt_sound.md` §8.1 (standard SMF) | |
| **TMTWINCD.EXE own resources**: cursors 128–130 (RT_CURSOR/RT_GROUP_CURSOR), icon 15000, DIALOG 1–8, name table | – | only mentioned (`sys_resources_saves.md` §2.1, `code_1008-1030.md` §6.4) | cursor and icon formats now specified and decoded by the engine (RT_GROUP_CURSOR/RT_CURSOR in `src/input/cursors.h`; RT_GROUP_ICON/RT_ICON, icon 15000 = one 32×32 16-colour entry, in `src/platform/exe_icon.h`); **gap** left: DLGTEMPLATE (P1-5) |

### 2.2 Resources never used by the Windows EXE [O unless marked]

| Resource | Ids | Evidence |
|---|---|---|
| BNDL | 313, 318, 319, 402 | no immediate, table or formula produces them (`fmt_bndl_ffnt.md` §2.3) |
| DAM scripts | 20043–20050 (treasures 43–50 never shown in the hall), 20102 (BNDL 402) | `fmt_othr_tables.md` §4.5 |
| OTHR text | 11501 (hint 1 never queued), 11604 (help page, Mac "Quit" variant [I]) | `fmt_othr_puzzles.md` §9–10 |
| SSND | sfx 7004, 7019, 7026, 7028, 7032, 7036, 7038, 7039; hint 7101, 7133; 5621 (duplicate of 5620) | `fmt_sound.md` §7.2, §6.1; `fmt_othr_puzzles.md` §6.1 |
| MIDI | 9009 TITLE (zone −97 never requested) | `fmt_sound.md` §8.5 |
| PMAP, only in dead BNDL 402 | 2130–2169 (crowned wizard casting a spell) | this synthesis (`rescheck.txt`) |
| **PMAP referenced by nothing** (new finding) | backgrounds 1008–1009, 1015–1016, 1018–1020, 1026–1027, 1030–1031, 1039; player frames 1105–1118 (carrying the crown), 1135, 1181–1239 (incl. the "magic zapper" 1206–1237), 1263–1265 — **89 ids** | [I] this synthesis: not a member of any live BNDL, not in the room table (+0x12), not produced by any id formula of the docs (1400+k, 330+k, …); a scan of `disassembly.asm` for these values as immediates finds only false positives (DGROUP offsets, hot-spot ids). Code Ghidra did not export is covered by the docs' own raw scans. |
| EXE DIALOG 5 (`NOT_ENOUGH_FILE_HANDLES`), dialog proc `RUNIN16COLORPROC` | | `code_1058-1090.md` §12 |

Why it matters: a port does not need these, but the unused art (crown carrying, zapper, the wizard
spell, castle doors 1029–1031/1039 are partly used) suggests features of the Mac or DOS editions;
a mod layer could restore them. Where to look: `re/dos/ghidra_export` (DOS edition), the Mac edition
if available.

---

## 3. Open questions ranked by impact on the port

Impact scale: **High** = a faithful port would behave visibly differently if guessed wrong;
**Medium** = small visible or audible differences, or save-compatibility details; **Low** =
cosmetic, unreachable, or Windows-environment only; **None** = documentation curiosity.
"Resolved" items from the topic docs' open questions are listed at the end for traceability.

### 3.1 High

| # | Question | Why it matters | Where to look / how |
|---|---|---|---|
| H1 | **Largely resolved** (`REFERENCE_CAPTURES.md` §4, §R2–§R8): E1–E15 were compared with the running original and matched the specs (corrections applied to the topic docs); still open are E16 (mouse), E17 (audio) and E18 (16-colour mode). ~~No end-to-end validation against the running game.~~ | A single misread table (dx, frameY, slot x) or ordering would be visible everywhere. | DOSBox-X captures, §4 (items E1–E8). |
| H2 | **Resolved** (`REFERENCE_CAPTURES.md` §4.1, §4.11). ~~Exact dialog geometry with the real font~~: the 19-px-per-line layout with the 17-px font is what the original does; every captured DlgDesc and help page matches the spec pixel for pixel once the WinG frame quirk (bottom edge one row high, `code_1008-1030.md` §4.5) is modelled, which is therefore kept (`PORTING_NOTES.md` §6.1). The engine's `ui::DialogSystem` reproduces E11/E12/E2 with 0 differing pixels. | Every message box, Yes/No prompt and help page. | E11, E12 (done). |
| H3 | **Hot-spot priority** in clue-box mode 0 (right click on the clue box shadowed by 0x409) is inferred from the allocator (`code_10e0-10f0.md` OQ2). Same mechanism decides mouse behaviour on all overlapping spots. | Mouse play; replay-clues by right click works or not. | Trace the 50-slot table in the emulator (needs mouse injection, which `tmctl.py` cannot do) or a debugger watch on DS:4b7a. |

### 3.2 Medium

| # | Question | Why | Where |
|---|---|---|---|
| M1 | Spot content **1** (5–8 per level) behaves exactly like 0 in the Windows EXE (raw byte scans of all code). Lost feature or generator residue? (`fmt_othr_tables.md` OQ1, `code_10f8-1148.md` OQ1, `sys_resources_saves.md` OQ3) | Must be preserved in saves; if the DOS/Mac version gives it a meaning, a "complete" port might want it. | DOS export `re/dos/ghidra_export`, DOS data `TM-DOS\*.PAK`. |
| M2 | **Music behaviour on modern-equivalent setups**: does music play when `midimap.cfg` is missing (Win95+) given that `Midi_PlayTrack` ignores `midiOk`? (`fmt_sound.md` OQ4, `code_1058-1090.md` OQ2) | Decides whether "no music on Win95" was shipped behaviour; affects nothing in a port except expectations. | Run on Win 3.1 with/without `midimap.cfg`; DOSBox-X audio/MIDI recording. |
| M3 | **MCI time format** for pause/resume (`g_midiSavedPos` truncated to 16 bits) (`fmt_sound.md` OQ3). | Where music resumes after a dialog or focus change (castle.mid > 65.5 s). | Emulator: open a dialog after 66 s of CASTLE; listen. |
| M4 | **Resolved** (`REFERENCE_CAPTURES.md` §4.3). ~~Transition type 1 with the fade stubs~~: the play-field is black only for room 9 and the hall (57–86 ms, as long as the redraw takes; no delay is coded), nothing elsewhere; type 0 keeps the pose and jump state with x ± 512. A port can show one black frame. | Every door and gate transition. | E4 (done). |
| M5 | **Resolved** (`REFERENCE_CAPTURES.md` §4.5). ~~Input loss from "first event per drain"~~: real but narrow; no key is lost at spacings ≥ 5 ms (human typing), every second key is lost at 2 ms, 6 of 8 when posted at once. Separately, arrow taps shorter than one 100-ms tick can be missed because held-key flags are polled per tick. The port's lossless-input option can stay off by default. | Decide the default of the port's input mode. | E2 (done). |
| M6 | **Resolved; decision: keep** (per REFERENCE_CAPTURES §R4). ~~Dirty-rect overflow and unchecked animation arrays under the heaviest load~~: the hall's pedestal windows are disjoint and the hall has no elves, so at most one hall animation runs at a time; the busiest captured scenes (three elves, rooms 15/16) showed no drawing errors; the overflow cases were not reached. The port keeps full-frame presentation without fixed limits (`code_1098-10d8.md` OQ6). | Possible missed updates in the original; a port presenting full frames hides them. | E9 (done). |
| M7 | **Resolved: confirmed stale; decision: keep** (per REFERENCE_CAPTURES §R5.3). ~~Stale tier after an Exit from the rank screen~~: after Esc + Yes the record holds the new total with the old +0x09; the next climb runs with the old tier (`sys_resources_saves.md` §6.5, OQ8; `PORTING_NOTES.md` §6.1). | Save compatibility of record +0x09. | E10 (done). |
| M8 | **Resolved: confirmed; decision: keep** (per REFERENCE_CAPTURES §R3). ~~Castle screen 0 right-walk uses `dxLeft`~~: walking right from x 80 steps +30,+8,+15,+6,+23,+12,+17,−1 (`code_1168-1198.md` §2.6, OQ2; `PORTING_NOTES.md` §6.1). | Walk spacing on one screen. | E7 (done). |
| M9 | **Exact 16-colour palette** intensities (`fmt_pmap.md` OQ3, `code_1038-1050.md` OQ1, `code_1058-1090.md` OQ4). | Only if the port offers 16-colour mode. | Run Win 3.1 with the VGA 16-colour driver; capture. |
| M10 | **Class cursor** `LoadCursor(hInst, IDC_ARROW)` probably NULL: does the cursor flicker to the arrow between hook updates? (`code_1008-1030.md` OQ2) | Cursor look. | Manual run with mouse. |
| M11 | Meaning of each **sound effect**, the three **voice banks**, and the 7 silent-answer questions that do have recordings (`fmt_sound.md` OQ7/8). | Only for documentation and mod tooling; the ids the code plays are known. | Listen to `extracted/previews/wav/`. |

### 3.3 Low

| # | Question | Source |
|---|---|---|
| L1 | Unreachable rooms 19–21, 32–34, 45–47 and `RoomDef` +0x09/+0x0d/+0x10 | dgroup_map OQ2/3, fmt_othr_tables OQ2, code_10e0-10f0 OQ7 |
| L2 | Treasure-hall signed index after ≥ 128 climbs | code_1168-1198 OQ3, fmt_othr_tables OQ7 |
| L3 | `g_gfxConst4` (DS:0860 = 4) meaning | code_1058-1090 OQ1, code_1168-1198 OQ1, dgroup_map OQ8 |
| L4 | `MenuPopupDef` +0x04/+0x10/+0x12 | code_10f8-1148 OQ4, code_1058-1090 OQ3, dgroup_map OQ1 |
| L5 | Holding Down keeps the frame (intentional "duck"?) | code_10e0-10f0 OQ5 |
| L6 | Speaker icon drawn on the result screens even when voice is off | code_1150-1160 OQ3 |
| L7 | `midimap.cfg` record layout | sys_resources OQ5, code_1058-1090 OQ2, fmt_sound OQ2, dgroup_map OQ9 |
| L8 | Unused fields/bytes: user record +0x1E0..0x4FF and +0x62C, `ElfAnimSet` +0x02, channel `user0/1`, `g_showCoins/Nets`, `g_playerHidden`, `5e42`, `g_music4abe/5efd/13cd`, ~20 unreferenced DGROUP blocks | various |
| L9 | Hint 1 / sfx without trigger: Mac or DOS leftovers? | fmt_sound OQ5/6, fmt_othr_puzzles OQ7 |
| L10 | DOS `ALL_PUZS`, DOS `USERS.DAT` layout | fmt_othr_puzzles OQ9, sys_resources OQ6 |
| L11 | `1088:01a9` filter `(e & 0x0FFF)` also maps right releases (0x2409 → 0x409) | fmt_sound OQ10 |
| L12 | SSND header word at +0 | fmt_sound OQ1 |
| L13 | Creature erase with frame −1 visually identical? | code_1168-1198 OQ7 |
| L14 | `Win40VersionNum` = 858, Program Manager item command line, name-table resolution in KERNEL | code_1008-1030 OQ3, sys_resources OQ1/2 |

### 3.4 None (unused code or Windows mechanics)

DOP script tag, F9/FB opcodes, sprite mode bits, opaque channels, dialog types 2/3/9 (only for
mods: implement as specified); shadow table and dead colour routines; WinG/GDI internals;
VK 0x29–0x2C jump-table entries; `g_wingRect2/Stride`; WAVEHDR flags; RTL internals
(`code_1000.md` OQ1–7, `code_11a0-11a8.md` OQ1–6); `FFNT` byte 0; `F9` byte-sized fields; `FC/FB`
pad byte.

### 3.5 Topic-doc open questions already answered by another doc

| Question (doc) | Answer |
|---|---|
| Frame offsets / hotspots for animations (fmt_pmap OQ1) | DAM `FD dx dy` relative to the channel origin; player/elf/climb offsets in DGROUP tables |
| White-key draw always mirrored (fmt_pmap OQ2) | unused by shipped data (no sprite has flag bit 1) |
| Item-selection variables (fmt_bndl_ffnt OQ1) | 65ce = climbs done, 5e61 = treasures found this climb, 0ec3[tier] = count, 23f2/23f3 = room level/zone (`ARCHITECTURE.md` §5.11) |
| DAM FD flag / DOP / F9 / DS:52e2 (fmt_bndl_ffnt OQ3–5) | mode bits 0 mirror / 1 stamp; DOP = no dirty tracking; F9 = update rects; 52e2 = erase-rect list |
| Record load path (fmt_othr_puzzles OQ1) | `Users_LoadSelected` 1160:137d |
| PlaySpeech return value (fmt_othr_puzzles OQ2, fmt_sound) | 1 = interrupted by a stop code or Ctrl+S |
| Who registers 0x409 / 0x125..0x128 (fmt_othr_puzzles OQ3, code_1150-1160 OQ6) | 0x409 = `Hud_SetClueBoxMode(1/2)`; 0x125..0x128 never registered |
| Glyph meanings 0x80–0x8B (fmt_othr_puzzles OQ6, code_1098-10d8 OQ4) | FFNT key-cap glyphs (`fmt_bndl_ffnt.md` §3.2): 0x86 Esc, 0x8B Enter, 0x80–0x83 arrows, 0x84/85 Spacebar, 0x8C Ctrl |
| Typewriter timer slot 6 (code_1098-10d8 OQ3) | never armed (11 `Timer_Set` call sites use 0/4/7) |
| Rooms 1..8, 0xFE placement, 5e61 (code_10e0-10f0 OQ10–12) | hall rebuild; only rooms 11/25/38; first-treasure rule |
| Gate walk target (code_1168-1198 OQ6) | `10e0:0447` constants 330/281/170 |
| Cut-scene part 2 frames (code_1168-1198 OQ4) | 20201/20205 draw BNDL 410/412 cels (frame base 0) |
| `onEsc` contract of `Anim_PlayModal` (code_10f8-1148 OQ12) | after the callback returns the loop continues while the channel runs: a "No" resumes the animation |
| Re-entering a lower level (code_1150-1160 OQ4) | impossible (room table) |
| Question 225 narration (code_1150-1160 OQ5) | no shift; 5621 is an unused duplicate (`ARCHITECTURE.md` C15) |
| BNDL 327 leak consumer (code_1038-1050 OQ8) | `Text_Render` draws immediately and keeps no pointer |
| Hint 5 reachability (old fmt_sound draft) | reachable (`10e0:0a82`, `1168:08cb`) |

---

## 4. Behaviours to observe in the original (DOSBox-X plan)

Tooling: `tools/tmctl.py` (keyboard only, `shot` = exact 640×480 frame, `save/load` states, `turbo`).
Only one agent may drive the emulator. Mouse-dependent items are marked **(mouse)** and need a
manual session. Audio cannot be captured through `tmctl.py`; items marked **(audio)** need DOSBox-X's
own WAV/MIDI recording or a person listening. For tick-exact captures use `turbo off` and a save
state at the start of each scenario; read `users.dat` after a save to know the hidden contents.

| # | Scenario | Steps (keys) | Capture / compare with |
|---|---|---|---|
| E1 | Title and attract loop | start; wait; `shot` per page; wait > 60 s on a page; `enter` through pages; `space` | page layout (`code_10f8-1148.md` §12), 60 s timeout, end animation at (32,21); (audio) OPEN1 → OPEN2, narration clips |
| E2 | Club Sign-In | new player via `"text:Name"` typed quickly; `enter`; duplicate name; `ctrl+r` remove; `up/down` wrap; 7 players → list full | list geometry (row 25 px from y 57, x 40/195), stars/Trainee/Champion, commas, caret 333 ms, dialog boxes; lost keystrokes (M5) |
| E3 | First climb start | sign in a new player | room 9 start pose, hints 0 and 2 order and popup layout (bubble 33,60; butterfly 430,230; text 83,92 w 321), HUD panel |
| E4 | Room transitions | walk right 9 → 10 (type 0); door 9 → 0 and back (type 1); gate to level 1 | instant redraw with x ± 512 vs black flash (M4) |
| E5 | Movement tables | hold `right` N ticks from a known x; `left`; `up`+`right` jump; walk into the room-36 fountain | x/y per tick vs DS:0c89/0d33/0ddd, bump sequence, obstacle thresholds. **Done** for walking, jumping, net swing and coin toss: every value equals the tables exactly, tick 100 ms (`REFERENCE_CAPTURES.md` §4.4); the room-36 fountain obstacle and the castle `dxLeft` quirk (M8) are **done** as well (per REFERENCE_CAPTURES §R3) |
| E6 | Coin toss and net swing | `enter` in each third facing left/right; `space` near an elf | targeted slot rule, blink steps 2,3,6,7,10,11, reveal positions (49/220/391, 152), coin/nets counters, elf catch (+1 coin) |
| E7 | Castle climb | reach room 49 (or a save state); walk right/left on screen 0; climb; let the creature push | right-walk `dxLeft` quirk (M8), ladder layouts, creature cycle 12 ticks, push −1 coin. **Verified** (per REFERENCE_CAPTURES §R3): every value as specified, M8 confirmed |
| E8 | Elf question | catch a carrier elf | screen layout (`code_1150-1160.md` §3.5), focus frames, reading highlight, wrong answer greyed, praise/sorry screens, coins +2/+1/0, Esc. **Verified** (per REFERENCE_CAPTURES §R2): pixel-exact in 27 states (with the WinG frame quirk) |
| E9 | Busy rooms | treasure hall with 6 pedestals animating; a room with many elves | missed updates (M6). **Verified** (per REFERENCE_CAPTURES §R4): hall exactly as specified; one hall animation at a time; no drawing errors seen |
| E10 | Celebration, rank screen, saves | finish a climb; skip with `enter`/`space`; `esc` on the rank screen, Yes | animation order, 4 fps, band fill 16 × 50 ms, then `users.dat` byte +0x09 (M7). **Verified** (per REFERENCE_CAPTURES §R5): order, timing and rank screen pixel-exact; a skip also skips the rank screen; M7 confirmed |
| E11 | Game dialogs | `esc` in a room (Exit Yes/No), Options dialogs (`alt+o`), About (`alt+h` … ), Start Another Game | pixel geometry vs `code_1098-10d8.md` §10.2–10.4 (H2), default button highlight, flash 4 × 62 ms |
| E12 | Help pager | `alt+h`, each topic; `pgdn`/`pgup`; `esc` | page layout x 8, y 22, w 480, 11 lines; button labels |
| E13 | Net shop | toss 4 coins at the sign in room 13 | sign frames 1350–1355, auto-walk to x 220, door states (4-tick empty niche), nets +10 |
| E14 | Coin pile | spend all coins and nets; change room | pile appears in the next non-hall room, not the current one; +10 coins. **Verified** (per REFERENCE_CAPTURES §R6); a pile left behind may move back into the room where the player ran out |
| E15 | Ctrl+S and voice | `ctrl+s` during a hint voice and during the title narration | voice cut, both flags toggled, music restarted from the beginning on re-enable. **Verified visually** (per REFERENCE_CAPTURES §R7): the hint is dismissed at once, the title page is unaffected; the audio side is not captured (P1-3) |
| E16 | Mouse items (mouse) | right click on the clue box; drag-walk; cursor shapes | H3, M10 |
| E17 | No `midimap.cfg` (audio) | remove `\WINDOWS\SYSTEM\midimap.cfg`; start | TOOBADNOSOUND dialog; does music play? (M2) |
| E18 | 16-colour mode | switch Win 3.1 to the VGA driver | palette of TMT16 art, highlight colours (M9) |

---

## 5. Inconsistent claims across the docs

`ARCHITECTURE.md` §7.1 lists 26 conflicts with their resolution and evidence; §7.2 lists the name
aliases. Docs that should be corrected (work item P0-1):

**Status: done (P0-1, P0-2).** Every fix below has been applied to the named doc; each corrected claim
carries "(corrected per ARCHITECTURE.md §7.1 Cn)" or an "Answered" note in its open-question list. C10,
C20 and C24 were applied as well (`code_1168-1198.md` OQ6, `dgroup_map.md` §4.10, `fmt_sound.md`
§6.2). The names files hold one canonical name per address and all docs use them; `ARCHITECTURE.md`
§7.2 records the decisions (`WinDlg_RunModal` 1080:0000, `GameDlg_RunModal` 10c0:0ae5, `Music_Halt`
1128:00d6, `Music_StopScene` 1128:00ef, …). The table is kept as a record of what was wrong.

| Doc | Fix |
|---|---|
| `code_10f8-1148.md` | BNDL 328 members (§1.1, §16) → 1380–1383; layer 2 rows (§2.1) → 788..1054; input 0x13 "Pause" (§2.2, §10.2, §12.5) → Ctrl+S; `Room_Present` bit 7 (§1.1) → never read |
| `code_10e0-10f0.md` | layer 2 rows (§2.1); "room-36 tree animation" (§4.1) → fountain; `1170:005b` "waits" (§2.3, §6.2) → stops at once; OQ10–12 answered; `FF 05 FF` (§2.2) → centring toggle |
| `code_1000.md` | §10: the restart branch is reachable; second `longjmp` at `1140:0062` with value 1 |
| `fmt_othr_tables.md` | §3.4: 0xFD sign is BNDL 302 (not 310) and is the net shop; §4.3: only 20300 leaves its last frame on screen |
| `fmt_othr_puzzles.md` | §8.2: PMAP 2000 is drawn opaque; OQ1–4, OQ6 answered |
| `code_1150-1160.md` | §3.4 and OQ5: question 225 has no narration shift (5621 duplicate) |
| `fmt_bndl_ffnt.md` | §2.4.1: count = `DS:0ec3[g_rankTier]`; `DS:5cca` = room; labels of BNDL 302/310/311–320 |
| `dgroup_map.md` | §4.19: `2dd0` is the fountain, not a tree; §4.12 note about the old 1168 draft can go |
| `fmt_pmap.md` | §8: 1600–1608 are the thrower projectile |
| `fmt_sound.md` | §7.2: sfx 8/31 "snowball" → sparkle/coin projectile |
| `code_1058-1090.md` | §10.1: "no music without midimap.cfg" → depends on whether MCI opens the file (C18) |
| names files | adopt one name per address (`ARCHITECTURE.md` §7.2), especially `Music_Stop` (1128:00d6 vs 00ef) and `Dlg_RunModal` (1080:0000 vs 10c0:0ae5) |

---

## 6. Prioritised work list

### P0: do before implementation starts

| Id | What | Why | Where |
|---|---|---|---|
| P0-1 | **Done.** Apply the corrections of §5 to the topic docs | implementers will read the topic docs directly; 10 of them carried at least one wrong claim | `re/docs/*.md` |
| P0-2 | **Done.** Unify names (`ARCHITECTURE.md` §7.2) across `re/names/*.tsv` and docs; Ghidra export regenerated with the canonical names | `Music_Stop` and `Dlg_RunModal` meant different functions in different docs | `re/names/` |
| P0-3 | Golden captures for the core loop: E3, E4, E5, E6, E11 | validates tick rate, movement tables, transitions, HUD and dialog geometry before engine work is tuned (H1, H2) | §4 |

### P1: needed for fidelity, can run in parallel with implementation

| Id | What | Why | Where |
|---|---|---|---|
| P1-1 | **Done** (E1, E2, E12, E13: `REFERENCE_CAPTURES.md` §4; E7, E8, E9, E10, E14, E15: per REFERENCE_CAPTURES §R2–§R8). ~~Remaining captures E1, E2, E7, E8, E10, E12–E15~~ | per-scene reference frames for regression tests | §4 |
| P1-2 | Mouse session E16 (hot-spot priority, cursor) | H3, M10 | manual DOSBox-X run or debugger watch on DS:4b7a |
| P1-3 | Audio session: E1 music switch, E15, M3 resume position, E17 | M2, M3, music rules | DOSBox-X MIDI/WAV recording |
| P1-4 | Decide port policies: input loss (M5), quirks table (`PORTING_NOTES.md` §6.1), MIDI back end, 16-colour support | design decisions that depend on P1-1..3 | PORTING_NOTES |
| P1-5 | Spec for the EXE's own resources: ~~Win16 RT_CURSOR / RT_GROUP_CURSOR (hotspot, AND/XOR masks), RT_GROUP_ICON~~ (done: `src/input/cursors.h`, `src/platform/exe_icon.h`), DLGTEMPLATE | the port reads cursors (129/130 walking cursors) and optionally dialog texts from the user's EXE | `extracted/wincd/TMTWINCD/TMTWINCD.EXE` resource table (`tools/ne_resources.py`) |
| P1-6 | Re-export Ghidra with the hidden functions created and truncated bodies fixed (`10b0:00b5`, `1178:0cb0`, `10c0:0cde`, `1160:137d`, `1140:*`, `1010:0196` body), then regenerate `functions.tsv`, `disassembly.asm` and the DGROUP `refs` column | future verification without private scripts; `dgroup_map.md` OQ10 | `tools/ghidra_scripts/FindMoreFunctions.java` on a copy of `re/ghidra_proj` |
| P1-7 | **Not needed** (M6 settled per REFERENCE_CAPTURES §R4: one hall animation at a time; the port has no fixed limits). ~~Measure peak loads (dirty rects, sprites) in E9~~ | M6 | – |

### P2: completeness

| Id | What | Why | Where |
|---|---|---|---|
| P2-1 | DOS edition cross-check: content value 1, unused sfx, hint 1, crown/zapper frames, `ALL_PUZS` | M1, L9, L10, §2.2 | `re/dos/ghidra_export`, `TM-DOS\` |
| P2-2 | Listen to sfx, voice banks, 7101/7133, the 7 silent-answer recordings | M11; labels for mod tooling | `extracted/previews/wav/` |
| P2-3 | Confirm that the 89 unreferenced PMAPs are really unused (dynamic trace of every `FindResource` id during a full play-through) | §2.2 | DOSBox-X debugger breakpoint on `1038:03da`/`0ba8` |
| P2-4 | `midimap.cfg` layout from a real Windows 3.1 install | L7 | Win 3.1 image in DOSBox-X |
| P2-5 | Clean up remaining Low items (L1–L14) in the docs | tidy specs | topic docs |

### P3: ignore unless needed

RTL internals (segments 1000/11a0/11a8 beyond `rand`, `sprintf`, `setjmp`), WinG/GDI tricks, dead
code (10c8, 1148, shadow/recolour blits, `Wave_PlaySync`, flat resource format), Win16 dialog-proc
details.

---

## Open questions

1. Is a 640×480 Windows 3.1 frame (with caption and menu bar) the right reference for captures, or
   should captures crop the 512×384 game area only? `tmctl.py shot` returns the full 640×480 frame;
   the game area offset depends on window metrics (`code_1008-1030.md` §2.1).
2. Can `tmctl.py` be extended to inject mouse events into the guest without stealing the host mouse
   (DOSBox-X mouse emulation API)? That would close H3, M10 and E16 automatically.
3. Is the Mac edition available? Several unused assets and Mac-style structures (MenuPopupDef
   fields, DOP tag, F9 rects, the 800-byte record gap, FFNT byte 0) could be explained by it.
4. Should the docs' "[I]" claims about the 89 unused PMAPs be confirmed dynamically (P2-3) before the
   port's asset tooling treats them as optional?
