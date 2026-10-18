# Reference captures of the original game (GAPS.md P0-3 and extras)

Frames and videos of the **original** *Treasure Mountain!* (TMTWINCD.EXE v1.01) running under
Windows 3.1 in DOSBox-X, captured on 2026-10-17/18 with `tools/tmctl.py` (keyboard only, posted
messages, window parked off-screen). They are the ground truth the engine is compared against
(`docs/DESIGN.md` "Testing"). Every capture is an exact emulated frame; nothing was scaled.

The captures themselves are local data under `extracted/captures/ref/` (git-ignored, they show
game art). This document lists them, says how each one was reached and what it shows, and records
every observation that confirms or contradicts the specs. Game text is referred to by resource id
or DGROUP offset.

Tags: **[O]** observed in a capture or video, **[I]** inferred.

---

## 1. Set-up and tools

| Item | Value |
|---|---|
| Emulator | DOSBox-X 2026.10.01 (SDL1, x64), `tools/dosbox_tmwin.conf`: `machine=svga_s3`, `cycles=fixed 60000`, Pentium, 32 MB |
| Guest | Windows 3.1, 640×480×256, small fonts; game started by AUTOEXEC (`win d:\tmtwincd\tmtwincd.exe`), CD image mounted as D: |
| Game options | `TMTWIN.INI` `Win31UseWinG=TRUE` (the WinG drawing path is active, see §4.1) |
| Video refresh | 70.0071 Hz (AVI header). One video frame = 14.28 ms; all timings below are quantised to that |
| users.dat | `extracted/win31_c/TLCWIN/TMTWINCD/USERS.DAT` (guest C: is a host directory, so save states do **not** roll it back) |

Tools (all keyboard-only and focus-safe):

* `tools/tmctl.py` — extended in this work:
  * `key` tokens `hold:K:MS`, `down:K`, `up:K`, `wait:MS`, `fast:TEXT`. The game reads the arrow
    keys as held flags (`DS:06ee..06f4`, set by WM_KEYDOWN, cleared by WM_KEYUP,
    `code_1008-1030.md` §5), polled once per 100 ms tick, so a posted key-down … key-up pair is a
    true hold; no key repetition is needed. `hold:right:1500` walked exactly 15 ticks.
  * `rec start` / `rec stop OUT.avi` — DOSBox-X "Record video to AVI" (ZMBV, every emulated frame).
  * `start --park` now keeps re-parking for 45 s and every command re-parks while a park is in
    effect: DOSBox-X re-centres its window when the guest switches video mode during boot, which
    silently un-parked the window after a restart (see §7).
  * Save states are files (`tools/dosbox-x/bin/x64/Release/save/N.sav`) and survive DOSBox-X
    restarts (verified: states saved in the first session load in the second).
* `tools/avi_frames.py` (new) — pure-Python ZMBV decoder: `info`, `changes` (which 16×16 blocks
  changed in which frame, with emulated time), `extract` (PNG frames). Decoded frames are
  pixel-identical to `tmctl shot` screenshots (checked on the same state).
* `tools/crop_game_area.py` (new) — crops the 512×384 game area out of a 640×480 capture (§2).

Verification scripts (scratch, not part of the engine; `extracted/scratch/refcap/`): `match.py`
(locate a PMAP decoded from the user's `TMT256.DLL` in a capture, ±N px search, RGB tolerance 12
for the 6-bit VGA DAC), `pose.py` / `track.py` (identify the BNDL 300 player frame and x/y in every
changed video frame by diffing against the room background PMAP + detected spot pictures), `dlg.py`
(re-implements the `DlgDesc`/pager layout and drawing of `code_1098-10d8.md` §10.2–10.4 with the
FFNT renderer `tools/assets/ffnt.py` and compares pixel by pixel), `hud.py` (HUD of
`code_10e0-10f0.md` §5.1), `signin.py` (list rows of `code_1150-1160.md` §5.5), `burst.py`
(zero-delay key posting for M5), `curate.py` (builds `captures/ref/` from the scratch captures).

---

## 2. Where the game area sits in the 640×480 frame

Measured [O] and explained by `InitInstance` (`1018:0115`, `code_1008-1030.md` §2.1):

| Rows / columns | Content |
|---|---|
| rows 0..19 | caption (row 0 border, rows 1..18 caption bar, row 19 line) |
| rows 20..37 + row 38 | Win16 menu bar (File / Options / Help) and its bottom line — **only while a climb is played** |
| client area | starts at screen (1, 20) without menu, (1, 39) with menu |

The viewport origin is computed once as `x = ((640 − (2·1 + 512)) / 2) & ~3 = 60`,
`y = (480 − (2·1 + 20 + 18 + 384)) / 2 = 28` (client coordinates; SM_CXBORDER 1, SM_CYBORDER 1,
SM_CYCAPTION 20, SM_CYMENU 18). Hence:

| State | Game area (screen) | Seen in |
|---|---|---|
| no menu bar (title, Club Sign-In) | **(61, 48)..(572, 431)** | E1, E2 |
| menu bar present (climb, its dialogs, help) | **(61, 67)..(572, 450)** | E3–E13 |

So the game area moves down 19 px when the menu is installed (`GameMenu_Install`, `1120:0000`);
it is exactly centred only with the menu. Every pixel-exact comparison in §4 used these offsets
(PMAPs found at offset (0,0) from their spec positions). `crop_game_area.py` detects the menu bar
from the frame (row 25 white at x 600..635). The guest mouse pointer is outside the game area
(screen x 0..11, y ≈ 355) in all captures except E1 (see §7).

---

## 3. Save slots (state when this work ended)

DOSBox-X was left running and parked, at slot 4's state.

| Slot | State | Use |
|---|---|---|
| 1 | "Enter New Player Name" box, "Tester" typed, not submitted (original slot from before this work; the in-memory player list is empty) | E2, M5 typing tests |
| 2 | Club Sign-In list with **7 players** (Tester, Lpha, Bravo, Charlie, Delta, Echo, Foxtrot), "Start New Player" selected | E2 list, list-full |
| 3 | Room 9 at climb start, first hint popup (hint 0) up, voice playing | E3 |
| 4 | Room 9 free play, player at the room-9 start pose (233,142, frame 60), coins 10, nets 10, nothing found | base for E4/E5/E6/E11 |
| 5 | Room 13 (net shop), x ≈ 158 facing right, 10 coins / 10 nets | E13; the key is under spot 0 of this room |
| 6 | Room 13 after the key was found (hint 9 dismissed), facing left, 9 coins | gate tests |
| 7 | Room 11 by the cave door, an elf approaching from the right | net catch: `key wait:2800 space` catches it |
| 8 | Room 10, x = 15 facing right (Enter targets spot 0 = treasure) | E6 |
| 9 | Room 10, x = 268 facing right (Enter targets spot 2 = empty) | E6 |
| 10 | Room 11 at the gate with the key, x ≈ 330 facing left (`hold:up:250` opens the gate) | E4 gate / cut-scene |

All climb states (3–10) belong to the same climb (layout below). `users.dat` on disk now holds the
7 players of slot 2; Tester's record was written by Start Another Game from slot 4 (room 9,
nothing found). Spot contents of this climb, from that record (+0x500/+0x596, `ARCHITECTURE.md`
§4.3) [O]: room 10 = {kind 18: treasure, –, kind 99: empty}; room 11 = {–, gate, gate};
room 12 = {decoy, empty, –}; room 13 = {kind 10: **key**, shop, –}; room 14 = {decoy, empty, empty};
room 15 = {empty, –, –}; room 16 = {decoy, –, empty}; room 17 = {–, empty, empty};
room 18 = {empty, decoy, empty}; room 22 = {decoy, treasure, –}.

---

## 4. Observations against the specs

### 4.1 Dialog geometry (GAPS H2, `code_1098-10d8.md` §10) — confirmed, with one drawing quirk

Every game dialog captured was re-drawn by `dlg.py` from the spec alone (layout table §10.2,
drawing recipe §10.3, buttons §10.4, the FFNT 999 text renderer) and compared with the capture
pixel by pixel [O]:

| DlgDesc | Type | X, Y, W, H | Buttons | Predicted px | Mismatches |
|---|---|---|---|---|---|
| 1533 Exit (default No; Tab → Yes; flash frame) | 8 | 103,108,305,167 | No (319,216), Yes (239,216), 64×21 | 50 935 | **0** |
| 14d6 Start Another Game | 8 | 86,70,340,243 | (337,254), (257,254) | 82 620 | 0 |
| 15cc New Player Messages (default Yes) | 8 | 137,70,238,243 | (286,254), (206,254) | 57 834 | 0 |
| 1625 Music and Sound | 8 | 148,118,215,148 | (274,207), (194,207) | 31 820 | 0 |
| 163f Voice | 8 | 160,118,191,148 | (262,207), (182,207) | 28 268 | 0 |
| 1919 About (x 6, y 10, w 484, 14 lines) | 1 | 6,10,499,351 | – | 175 149 | 0 |
| 21e6 Remove A Player's Name (`%s` = Tester, colour escapes) | 8 | 99,70,313,243 | (323,254), (243,254) | 76 059 | 0 |
| 2129 already on the list (`%s` = Tester) | 1 | 177,142,157,99 | – | 15 543 | 0 |
| 2200 list full | 1 | 119,95,273,194 | – | 52 962 | 0 |
| 221a Enter New Player Name + name box (140,182,192,36), text (152,191), caret cell | 0 | 105,102,301,180 | – | 54 100 | 0 |
| Pager 2cfe, "About the Game" pages 1–4 (types 5,6,6,7), keyboard 1–2 (5,7), mouse (4), educational 1 (5) | 4–7 | 8,22,495,303 | (30,291), (286,291), 192×24 | 149 985 each | 0 |

* The 19-px-per-line layout with the 17-px font (`code_1098-10d8.md` §10.2, §14) is what the
  original does: text lines advance 17 px, the box keeps the 19-px height, leaving blank space at
  the bottom (very visible in About, E11/12).
* **Quirk needed for pixel equality:** without it every box differs in exactly two rows: the
  bottom edge of the three nested frames is drawn one row higher than `FrameRect(x, y, w, h)`
  predicts (rows Y+H−10..Y+H−8 instead of Y+H−9..Y+H−7). This is the WinG frame routine
  `1008:050b`, which draws the bottom edge at `bottom − 2` (`code_1008-1030.md` §4.5). The same
  shift appears on the name box of the sign-in. With the quirk modelled, all boxes match exactly.
  **Contradicts** `PORTING_NOTES.md` §6.1 ("WinG frame off-by-one — drop"): to match the
  reference captures the port must keep it (at least in its fidelity mode).
* Default button highlight [O]: Exit / Start Another Game / Remove → "No" (button 0, right)
  highlighted; the three Options dialogs → "Yes" (button 1, left) highlighted, as in §2.8.
* Keyboard [O]: Tab moves the highlight; `n` with "Yes" highlighted un-highlights Yes and
  flashes No; Esc closes a type-8 box at once (no flash). Whether the option values stayed
  unchanged after Esc was not checked.
* Button flash (`Dlg_FlashButton` `10c0:06a4`, 4 × 62 ms) [O]: toggles at video frames
  127/131/136/140, box restored at 144 → intervals 57–71 ms (62 ms within one video frame), box
  closes ≈ 57 ms after the 4th toggle. The un-highlight of the other button and the first toggle
  land in the same frame.
* While a game dialog, hint or modal animation is up the Win16 menu bar is greyed
  (`Menu_EnableAll(0)`) [O]. The Options and Help popups are native Win16 menus drawn by Windows
  (captured in E11).

### 4.2 HUD layout (`code_10e0-10f0.md` §3.5, §5.1) — confirmed pixel-exact

`hud.py` rebuilt rows 287..383 (line at 287 colour 0, fill (0,288,512,96) colour 8, panel PMAP 1306
opaque at (5,291), key icon at (131,296) = BNDL 301 entry 2·level+(1−haveKey), coins / nets /
treasure points as `FF 05 FF` + digits centred in (271,339,w 32), (361,339,w 41), (463,339,w 41),
gem icons 1307/1308 at the tier-0 positions (469,315), (486,315)) and matched **49 664 / 49 664**
pixels in five states [O]: start (10/10/0), after the net shop (6/20/0), after a catch (11/9/0),
after a treasure (9/10/1, one gem filled), with the key (filled key PMAP 1300).
"CLUES:", its three yellow lines and the panel captions are part of PMAP 1306. Clue-box mode 2
(hint waiting): fill (10,296,190,76) colour 9 and CONTINUE (PMAP 1309) centred at (23,304) [O].
A coin spent with Enter is shown immediately: the HUD is redrawn in the Enter tick, one tick
before the toss animation starts (§4.6).

### 4.3 Room transitions (GAPS M4, `code_1168-1198.md` §8.2, `ARCHITECTURE.md` §3.1.5) — confirmed

| Transition | Observation [O] (video frames at 70 Hz) |
|---|---|
| Type 0, room 9 → 10 (walking/jumping off the right edge) | No black frame. Last room-9 frame shows jump frame 20 at x = 500 (> 482); ≈ 190 ms later room 10 is presented in one blit (frame 141 shows a capture tear: top rows already room 10). Pose and **jump state are kept**: the first room-10 tick shows frame 21 at x = 24 = (500 − 512) + dxRight[21] 36, then frame 1. Confirms "type-0 keeps the pose, x ± 512" and that only types 1/2 clear `g_jumping` (`code_10f8-1148.md` §8.2). |
| Type 1, door room 9 → 0 (hall, zone 0) | Door entry frames 9 (y 137) then 10 (y 132) at x 233 (y −5 per tick, §4.3.2 step 4); then the play-field turns **black for 6 frames (≈ 86 ms)** with the HUD untouched; room 0 appears at its start pose (230,142, frame 0, right). |
| Type 1, door room 0 → 9 | Black for **4 frames (≈ 57 ms)**, room 9 at its start pose (233,142, frame 60). |
| Type 1, door room 11 → 17 (zone 1) | **No black frame**: room 11 (frame 70) is followed directly by room 17 (frame 83) at its start pose (0,142, frame 0). |
| Gate room 11 → 23 (level 0 → 1) | Up in the gate third with the key → auto-walk, Room_Run ends; key icon cleared (`Inv_SetHaveKey(0)`) before the cut-scene; `LevelCutscene_Play(0)` part 1 over room 11 at 7 fps (10 video frames = 142.9 ms per step), black fill, PMAP 2200 at (0,20), part 2 at 7 fps, black fill, then room 23 at its start pose (330,142, frame 60). |

* M4 answered: transition type 1 shows a black play-field only for room 9 and the hall, nothing
  elsewhere. The black lasts as long as the redraw takes (57–86 ms here); no delay is coded
  (fade stubs `1070:011c/0120`). A port can show one black frame.
* The black before room 23 comes from the cut-scene's own fills (`code_1168-1198.md` §6.1 step
  6.6), not from `Room_Present`.
* Climb start (E3) shows the same pattern: black screen + menu bar, HUD presented over a black
  play-field, then room 9 (`Room_Present(1)` black fill is invisible there because the play-field
  is already black); the hourglass cursor appears during the loads (`Cursor_SetWait`, §8.2 step 2).

### 4.4 Movement per tick (E5, `code_10e0-10f0.md` §3.4 / §4.3, DS:0c89 / 0d33 / 0ddd) — confirmed exactly

Tracked with `track.py` (frame index, x = sprite left edge, y = sprite top) on every changed video
frame [O]:

| Action (from slot 4, x 233) | Frames | x per tick | y per tick |
|---|---|---|---|
| hold Right 1.5 s | 1,2,3,4,5,6,7,8,1,… | +15 +18 +11 +9 +21 +12 +20 +4 (repeats) | 140 139 136 141 136 138 135 140 |
| release | 0 | +0 | 142 |
| hold Left 1.2 s | 1..8 mirrored | −30 −8 −15 −6 −23 −12 −17 +1 | as above |
| Right held, Up tapped 150 ms | 12..21 then 1 | −1 0 +20 +19 +24 +50 +20 +57 +34 +36, then +15 | 142 148 126 126 97 85 92 97 116 148, then 140 |
| net swing (Space, x 15 right) | 24..34 | 0 +3 −17 +36 −4 −10 +8 −15 −31 +31 −1 | 142 149 113 73 135 179 162 83 124 136 141 |
| coin toss right (Enter) | 46..53, then 0 ×5 | 0 0 +5 +5 −8 0 −2 0 | 142 152 130 137 129 141 147 141, then 142 |
| coin toss left | 46..53 mirrored | 0 0 −8 −4 −10 −14 (+28 +8) | as above |

* All values equal `dxRight` (added facing right), `dxLeft` (subtracted facing left) and the
  absolute `frameY` of DS:0c89 / 0d33 / 0ddd, and the frame sequences equal `nextFrame` (DS:0c34),
  `netSwing` (0ba0) and `coinToss` (0bb8). The jump ends 21 → (255) → frame 1 as described in
  §4.3.3.
* Tick period: 100 ms ± one video frame (successive changes 6–8 frames apart, mean 7.0) [O].
* PMAPs of frames 0, 8, 24 and 46 (ids 1251, 1259, 1153, 1243) are pixel-identical [O, decoded];
  so the first tick of a net swing or coin toss (frame 24/46, dx 0, y 142) is invisible, and
  frame 8 differs from a stand only by y (140 vs 142). Frame 12 (1136) looks the same as frame 0.
* Start poses equal `RoomDef` +0..+5 [O]: room 9 (233,142,60,R), room 0 (230,142,0,R),
  room 17 (0,142,0,R), room 23 (330,142,60,R).
* Not covered: the room-36 fountain obstacle (level 2) and the castle screen (M8, E7).

### 4.5 Input (GAPS M5, `code_1008-1030.md` §6.2) — confirmed, real but narrow

Typing eight letters into the name box with different spacing between keys (one key-down/up pair
per letter) [O]:

| Spacing | Result | Lost |
|---|---|---|
| ≈ 240 ms (`text:`), ≈ 50 ms (`fast:`), 35 ms, 20 ms, 10 ms, 5 ms | all 8 letters | 0 |
| 2 ms | "Abdf" | 4 (every second letter) |
| 0 ms (all messages posted at once) | "Ab" | 6 |

The every-second-letter pattern is the signature of "only the first event of a message drain is
kept": two characters that reach the application queue within one drain lose the second. At any
human typing speed nothing is lost; the port's "lossless input" default can stay off without
practical effect. One unexplained case: the first letter of a name typed directly on the sign-in
list (type-to-create, right after `load 2`) was lost once ("Lpha"), not reproduced in two retries.

A different, polling-related loss [O]: a plain Up press (key held ≈ 80 ms) did not enter the room-9
door; `hold:up:250` always did. Movement and door/gate actions read the held-key flags once per
100 ms tick, so a tap shorter than a tick can be missed. The port should poll held state the same
way (or latch key-downs until the next tick if it wants to be friendlier).

### 4.6 Coin toss, reveal and net (E6, `ARCHITECTURE.md` §5.3, §5.5) — confirmed

* Targeted slot `clamp((facingLeft ? x−41 : x+78)/170, 0, 2)` [O]: right at x 15 → slot 0, right at
  x 268 → slot 2, left at x 230 → slot 1 (no picture there; coin lost, nothing drawn).
* The Enter tick spends the coin (HUD 10 → 9) and draws nothing else; the toss runs over the next
  13 ticks (1.3 s); step 13 resolves the content (treasure: counter 1, gem filled, hint 4 queued and
  shown two ticks later).
* Blink [O]: the spot picture is blanked from the pristine layer in the ticks that process steps
  1, 2, 5, 6, 9, 10, i.e. when `g_coinStep` **after** its increment is 2, 3, 6, 7, 10, 11 (the
  draw pass runs after `Player_Update`). `ARCHITECTURE.md` §5.3 "steps 2, 3, 6, 7, 10, 11" is
  right if read as the post-increment value; the first blink is visible together with toss frame
  46. The picture blinks for **every** targeted spot that has a picture, including empty ones
  (room 10 spot 2, content 0).
* Revealed objects [O]: treasure = PMAP 1700 (BNDL 330 entry 0, the base treasure of climb 0) at
  (49,152); key = PMAP 1300 (HUD art entry 2·level) at (49,152). Positions exact (`match.py`).
* Coin effect frames 54..59 appear from step 7 (sparkle where the coin lands, then POOF).
* Net catch [O]: Space tick: nets −1; the elf was caught at step 2 (coins +1 in that tick), carried
  in the net, released and fled (non-carrier). Confirms "+1 coin" and the step-2..7 window.
* Key found [O]: hint 9 popup and the filled key icon (PMAP 1300) in the HUD.

### 4.7 Hints and climb start (E3, `code_10f8-1148.md` §11, `ARCHITECTURE.md` §5.15) — confirmed

* Hint 0 then hint 2 at the first room tick of a new player [O]; neither returns after resuming
  the climb from the sign-in (bits consumed; the saved mask is `0xFFFFFFFA`).
* Popup layout exact [O]: bubble PMAP 2011 at (33,60), butterfly PMAP 2010 at (430,230), text
  OTHR 11500+e rendered at (83,92) width 321 — 0 mismatching text pixels for hints 0, 2 and 9.
* While the voice plays the clue box stays in mode 0; the CONTINUE button (mode 2) appears when
  the voice ends (`Hint_WaitDismiss`), Enter dismisses; the play-field is restored from layer 1
  in the next tick and the menu re-enabled.

### 4.8 Club Sign-In (E2, `code_1150-1160.md` §5) — confirmed, one inference wrong

* List rows pixel-exact for 1 and 7 players (12 308 and 49 232 px) [O]: bars 2181/2182 at
  (35, 57 + 25·i), name at (40, y+3), "Trainee" at (195, y+3), total right-aligned at 311,
  "Start New Player" centred at y+1. Panel PMAP 2180 at (27,54); the bottom panel PMAP 2183 at
  (0,288) already contains the "SIGN-IN …" text (matched 100 %). Backdrop PMAP 2100 at (0,20)
  both on restart and at program start (at start the back page still holds the title picture, as
  §5.4 inferred). Stars, Champion and thousands commas were not observable (all totals 0).
* Up/Down wrap [O]: Up from "Start New Player" → last player; Up from player 0 → "Start New
  Player"; Down from "Start New Player" → player 0.
* Ctrl+R on a player → dialog 21e6 with the name in colour 5 (red); answering No rewrote
  `users.dat` (new timestamp, identical bytes) — confirms "Remove Player writes even after No"
  (`sys_resources_saves.md` §6.5).
* Duplicate name → dialog 2129, then back to the name box; Esc → list. 7 players + Enter on "Start
  New Player" → dialog 2200. The list keeps file order for equal totals (stable sort).
* Caret blink half period 333 ms [O]: changes every 23–24 video frames (mean 333.3 ms).
* **Contradiction:** `code_1150-1160.md` §5.6 infers that DlgDesc 221a gives "X = 41 (box width
  430)" matching hot spot 0x4af. The capture shows the box at **X = 105, W = 301** (Y = 102,
  H = 180 and footerY = 253 are right). Hot spot 0x4af (41,253,424,23) is wider than the box; the
  rectangles are hard-coded, not derived from the layout.
* `users.dat` after the first save [O]: tier-0 counts per level 10 empty / 5 decoy / 2 treasure /
  1 key, gate slots rooms 11/25 (1,2) and 38 (0,1), shop slots 13/1, 24/0, 36/2 (`ARCHITECTURE.md`
  §5.1, §5.3); record room = 9; the spot contents match what the coin tosses revealed.

### 4.9 Title (E1, `code_10f8-1148.md` §12) — confirmed

* Title PMAP 2100 at (0,20) [O]; story pages 1–4: colour-11 box (33,59,284,196) with OTHR
  20105..20108 at (43,67) width 264 — pixel-exact except the mouse pointer.
* Page timeout **60.02 s** measured between two page draws (timer 4, 60 000 ms) [O].
* After page 4 (a key had been pressed earlier) the loop ends; animation 2 plays over the restored
  title picture; then the Club Sign-In appears over the title backdrop.

### 4.10 Net shop (E13, `code_10f8-1148.md` §4) — confirmed

Four coins at the sign (room 13 spot 1, content 0xFD) [O]: sign BNDL 302 frame `4 − shopCoinsLeft`
(PMAP 1350..1354) at (207,24); with the 4th coin the door opens in the same tick (PMAP 1320 at
(219,101)); auto-walk with walking frames at 5 px/tick to x = 220 (13 ticks); door-entry frames 9,
10; then frame 60, nets 10 → 20, sign back to 1350, empty niche PMAP 1321 for 4 ticks, door
restored (state 7). Coins 10 → 6.

### 4.11 Summary for the spec owners

| Item | Status |
|---|---|
| H1 end-to-end validation | Done for E1–E6, E11–E13 (this document) |
| H2 dialog geometry | Confirmed exactly; needs the WinG frame quirk (§4.1) |
| M4 transition type 1 | Confirmed: black only for room 9 / hall (§4.3) |
| M5 input loss | Confirmed but only for keys < ~5 ms apart; arrow taps < 1 tick can be missed (§4.5) |
| E5 movement tables | Confirmed exactly (§4.4) |
| HUD layout | Confirmed exactly (§4.2) |
| `PORTING_NOTES.md` §6.1 "WinG frame off-by-one: drop" | Should become **keep** (visible in every dialog) |
| `code_1150-1160.md` §5.6 [I] "X = 41, width 430" for 221a | Wrong: X 105, W 301 |
| `ARCHITECTURE.md` §5.3 blink steps | Clarify: post-increment `g_coinStep`; blinks for every targeted spot with a picture |
| `ARCHITECTURE.md` §2.5 / §5.4 sign-in backdrop at start | Confirmed (title picture) |

**Applied** (2026-10-18): `PORTING_NOTES.md` §6.1 (WinG frame → keep; also `code_1008-1030.md` §4.5),
`code_1150-1160.md` §5.6 (X 105, W 301; hot spots 0x4af/0x4b0 hard-coded), `ARCHITECTURE.md` §5.3
(post-increment blink steps; every targeted spot with a picture blinks), `GAPS.md` H2, M4, M5 and E5
marked resolved. The engine's text/dialog code (`src/text`, `src/ui`) reproduces every dialog and
help page of E2/E11/E12 and the hint/story texts of E1/E3 with 0 differing pixels
(`extracted/scratch/text_ui/compare_caps.py`).

---

## 5. Capture index

All PNGs are exact 640×480 frames (either `tmctl shot` or decoded from the AVI named in the
folder; frame numbers below are AVI frame indices at 70.0071 Hz). Folder =
`extracted/captures/ref/<scenario>/`.

### E1_title (new DOSBox-X session; the mouse pointer sits at screen (320,240) = game (259..270, 192..211))

| File | Reached by | Shows |
|---|---|---|
| 01_title_page0_anim1 | `start --park`, ≈ 40 s | title picture (PMAP 2100) with animation 1 |
| 02_story_page1 | page 0 timed out (60 s) | story page 1 box and text |
| 03_story_page2 | page 1 timed out | story page 2 |
| 04_story_page3 | Enter on page 2 (`e1_page2_timeout.avi` frame 200) | story page 3 |
| 05_story_page4_after_60s_timeout | no key for 60 s (same AVI, page switch at frame 4253) | story page 4 |
| 06_end_animation_over_title | Enter on page 4 | animation 2 over the title picture |
| 07_signin_at_startup_list | animation ended (`e1_end_anim.avi` frame 0) | sign-in at program start, backdrop = title |
| 08_signin_startup_name_entry | Enter on "Start New Player" | empty name box |
| e1_page2_timeout.avi | | 63 s: page 3 drawn at frame 51, page 4 at frame 4253 (60.02 s) |

### E2_signin

| File | Reached by | Shows |
|---|---|---|
| 01_name_entry_tester_caret_on | slot 1 | DlgDesc 221a, name box "Tester", caret on |
| 02_name_entry_caret_off | slot 1 (`caret.avi` f24) | caret off |
| 03_name_typed_10ms_per_key_no_loss | slot 1, 8× backspace, `burst.py abcdefgh 0.01` | all 8 letters |
| 04_name_typed_2ms_per_key_loss | same with 2 ms | "Abdf" |
| 05_name_typed_burst_loss | same with 0 ms | "Ab" |
| 06_list_tester_newplayer_selected | slot 4, File > Start Another Game > y | list with 1 player |
| 07_list_up_tester_selected | Up | Tester highlighted |
| 08_remove_player_dialog | Ctrl+R on Tester | DlgDesc 21e6 |
| 09_list_after_remove_no | n | list rebuilt, "Start New Player" selected |
| 10_name_entry_duplicate_typed | `text:Tester` on the list | name box "Tester" (type-to-create) |
| 11_already_on_list_dialog | Enter | DlgDesc 2129 |
| 12_resume_black_playfield | Enter on Tester (`e2_resume.avi` f102) | black screen + menu bar |
| 13_resume_room9_start_pose | | resumed climb, room 9 start pose, no hints |
| 14_list_7_players | 6 new players, then slot 4 > Start Another Game | 7 rows (= slot 2) |
| 15_list_full_dialog | Enter on "Start New Player" | DlgDesc 2200 |
| 16_name_typed_on_list_first_char_lost | after creating the players | "Lpha" (first letter lost once, §4.5) |
| caret.avi, e2_signin_restart.avi, e2_resume.avi | | caret blink; Start Another Game → list; list → room 9 |

### E3_climb_start (from slot 1, Enter; `e3_start.avi`, `e3_hint2.avi`)

| File | Shows |
|---|---|
| 01_name_box_closed (f49) | box restored, sign-in panel |
| 02_menu_bar_black_screen (f104) | screen cleared, menu bar installed (game area moves to y 67) |
| 03_hud_drawn_playfield_black (f106) | HUD presented, play-field black |
| 04_room9_start_pose (f113) | room 9, player frame 60 at (233,142) |
| 05_menu_disabled_before_hint (f117) | menu greyed |
| 06_hint0_voice_playing | hint 0 popup, clue box mode 0 (= slot 3) |
| 07_hint0_continue_button | voice ended: CONTINUE in the clue box |
| 08_hint0_dismissed (`e3_hint2.avi` f54) | play-field restored |
| 09_hint2_voice_playing / 10_hint2_continue_button | hint 2 |
| 11_room9_free_play | hints done (= slot 4) |

### E4_transitions

| File | Reached by | Shows |
|---|---|---|
| 01_room9_jump_frame20_x500 | slot 4, Right held + Up (`e5_jumpR.avi` f128) | last room-9 frame |
| 02_type0_room10_blit_tear (f141) | | capture tear during the room-10 blit |
| 03_type0_room10_frame21_x24 (f142) | | jump continues in room 10 |
| 04/05_door_room9_frame9/10 | slot 4, `hold:up:250` (`e4_door90.avi` f60, f67) | door entry |
| 06_type1_black_to_room0 (f78) | | black play-field |
| 07_room0_start_pose (f84) | | hall room 0 |
| 08_room0_under_door_x90 | walk left to x 90 | before entering the hall door |
| 09_type1_black_to_room9 / 10_room9_start_pose_again | `hold:up:250` (`e4_door09.avi` f69, f73) | black, room 9 |
| 11_room11_door_frame10 / 12_type1_room17_no_black | slot 7, `hold:up:250` (`e4_door11_17.avi` f70, f83) | no black between |
| 13–19 | slot 10, `hold:up:250` (`e4_gate.avi` f0, 60, 150, 210, 300, 386, 412) | gate with key, auto-walk, cut-scene part 1, black, part 2 (PMAP 2200), black, room 23 |

### E5_movement (slot 4; `e5_right.avi`, `e5_left.avi`, `e5_jumpR.avi`)

One file per tick; the name gives the player frame and x: 01 stand (frame 60, x 233);
02–09 walk right frames 1–8; 10 release (frame 0, x 449); 11–18 walk left frames 1–8;
19 release left (x 64); 20–29 jump frames 12–21 (29 already in room 10); 30 frame 1 after the
jump. Values in §4.4.

### E6_coin_net

| Files | Reached by | Shows |
|---|---|---|
| 01–16 | slot 8, Enter (`e6_coin_s0.avi` f50…159) | before; Enter tick (coins 9); steps 1–13 (blink off with treasure visible at steps 1,2,5,6,9,10); collected; hint 4 |
| 17–18 | slot 9, Enter (`e6_coin_s2.avi` f61, f145) | blink on an empty spot; coin lost |
| 19–20 | slot 9, `hold:left:120`, Enter (`e6_coin_s1L.avi` f64, f92) | mirrored toss into the middle third |
| 21–30 | slot 8, Space (`e6_net_r10.avi` f69…132) | net-swing frames 25–34 (32 clipped at x −15) |
| 31–36 | slot 7, `key wait:2800 space` (`e6_net_elf.avi` f238…338) | elf caught (coins 11, nets 9), carried, released, flees |
| 37–39 | slot 5, `hold:left:120`, Enter (`e6_key.avi` f59, f145; shot) | key under the bushes, collected, hint 9 + filled key icon |

### E11_dialogs

| File | Reached by | Shows |
|---|---|---|
| 01_exit_default_no | slot 4, Esc | DlgDesc 1533 |
| 02_exit_tab_yes_highlighted / 03_exit_n_flash_on / 04_exit_flash_off / 05_exit_closed_restored | Tab, n (`e11_exit_no.avi` f100, 128, 131, 150) | highlight, flash, restore |
| 06_options_menu_open | slot 4, Alt+O | Win16 popup |
| 07_new_player_messages | Enter | DlgDesc 15cc (default Yes) |
| 08_options_menu_second_item / 09_music_and_sound / 10_voice | Esc, Alt+O, Down(, Down), Enter | DlgDesc 1625, 163f |
| 11_help_menu_open / 12_about_treasure_mountain | Alt+H; Up, Enter | DlgDesc 1919 |
| 13_file_menu_open / 14_start_another_game | slot 4, Alt+F; Down, Enter | DlgDesc 14d6 |

### E12_help (slot 4 state, Help menu via Alt+H)

01–04 "About the Game" pages 1–4 (Enter, then PgDn ×3); 05–06 "Using the Keyboard" pages 1–2;
07 "Using the Mouse" (single page, type 4); 08 "Educational Focus" page 1; 09 after Esc (room
restored).

### E13_netshop (slot 5, Enter ×4 at 1.7 s intervals; `e13_netshop.avi`)

01 sign empty; 02–04 sign with 1–3 coins (f145, 285, 415); 05 fourth coin: sign full, door open
with the net (f545); 06 auto-walk (f600); 07 arrived at x 220 (f635); 08–09 door entry frames 9,
10 (f649, 655); 10 net taken: frame 60, nets 20, sign reset, empty niche (f664); 11 door closed
4 ticks later (f690).

---

## 6. Not covered

* E7 castle climb (M8), E8 elf question, E9 busy rooms (M6), E10 celebration / rank screen (M7),
  E14 coin pile, E15 Ctrl+S, E16 mouse (H3, M10), E17/E18 audio and 16-colour mode. None needs a
  new tool except E16 (mouse) and the audio items; E8/E10 need a longer scripted play-through
  (catch a carrier elf; reach room 49), for which slots 6 and 10 are good starting points.
* Room-36 fountain obstacle and the castle `dxLeft` quirk (level 2 / room 49).
* Stars, Champion and comma formatting of the sign-in list (needs totals ≥ 5 / ≥ 1000).
* Turbo was not used, so timings are real-time emulated values.

## 7. Notes on the capture process

* After the DOSBox-X restart for E1, the window re-centred itself on the desktop when Windows 3.1
  switched video mode and stayed visible (never focused) until it was re-parked later in the
  session; `tmctl.py` now re-parks automatically (§1).
* The guest mouse pointer cannot be moved with `tmctl.py`; a fresh Windows session puts it at the
  screen centre (over the game area, E1 captures). Loading a state saved in the first session
  brings back the pointer position of that session (left edge, outside the game area).
* Loading a state does not touch `users.dat` (host directory), so a state may disagree with the
  file on disk; the game re-reads the file only in the sign-in and when saving.

---
---

# Round 2 (2026-10-18): elf questions, castle climb, celebration, hall, coin pile, Ctrl+S

Same DOSBox-X session as the end of round 1 (never restarted, window parked at (−4000,100) the
whole time, keyboard only, `tmctl.py` only). Everything below is **[O]** unless tagged **[I]**.
Frame numbers are AVI frame indices at 70.0071 Hz (one frame = 14.28 ms); "tick" = the game's
100-ms timer step.

## R1. Set-up changes

* **Host audio.** `tools/dosbox_tmwin.conf` routes the SB16 and the MIDI (`mididevice=win32`) to the
  host speakers. To keep the session silent, only DOSBox-X's own Windows audio session was muted
  (Core Audio `ISimpleAudioVolume` of `dosbox-x.exe`; script
  `extracted/scratch/refcap2/audio_state.ps1 -MuteDosbox 1`, run with PowerShell 7). The guest is
  unaffected (it still plays voice/MIDI into the emulated hardware); nothing else on the host was
  touched. Windows may remember the mute for that executable; `-MuteDosbox 0` undoes it.
* **`tools/tmctl.py state save|load|list NAME`** (new, additive): named save states in
  `extracted/captures/savestates/NAME.sav`, using slot 10 as scratch. Each state is stored with the
  guest's `USERS.DAT` (`NAME.users`) and `load` restores that file: the game keeps the signed-in
  player's **slot index** in memory and `Users_SaveCurrent` writes that index of the file
  (`sys_resources_saves.md` §6.5), so loading a state against a different `USERS.DAT` makes the
  next save overwrite another player (with a re-sorted file, Start Another Game from a Tester
  state would have written Tester over whoever is at index 0).
* **Scratch verifiers** (`extracted/scratch/refcap2/`, not part of the engine): `quizcheck.py`
  (question/praise/sorry screens from `code_1150-1160.md` §3.4–3.7), `rankcheck.py` /
  `rankscan.py` (rank screen, `code_10e0-10f0.md` §6.2), `signin2.py` (list rows incl. the
  8-digit rule), `track2.py` (player tracker that uses `frameY` as the absolute y, works over
  animated backgrounds), `climbtrack.py` (rebuilds `DrawScreenBackground` 1168:0299 per screen and
  matches climber / walker / creature frames), `hallanim.py` (pedestal animation vs player centre),
  `sheetavi.py`, `users.py` (dump/patch a record), `craft.py`, `curate2.py`. Round-1 tools
  (`match.py`, `hud.py`, `dlg.py`, `pose.py`) were reused unchanged.

### R1.1 Edits to the working-copy `USERS.DAT` (`extracted/win31_c/TLCWIN/TMTWINCD/USERS.DAT`)

Backup first: `extracted/captures/savestates/round1/USERS.DAT.round1_end` (the file at the start).
All edits were made while a state with Tester (index 0) signed in was loaded, so the game's own save
merged Tester into index 0 and kept the edited records (the sign-in entry re-reads the file, §6.5).

1. `craft.py r2` (state `r1_4` loaded): slots 1..6 replaced by copies of Tester's record with the
   name changed and these fields (offsets per `ARCHITECTURE.md` §4.3; `+0x011` hint mask = 0 for
   all six so that once-only hints do not interrupt):

   | Slot | Name | +0x09 tier | +0x0A total | +0x15 key | +0x1A gems | +0x1B climb pts | +0x1DF room | +0x62D climbs | Purpose |
   |---|---|---|---|---|---|---|---|---|---|
   | 1 | Gate1 | 0 | 24 | 1 | 0 | 1 | 25 | 0 | level-1 gate → level 2 → castle → celebration; M7 (24 + 1 reaches rank 2 = tier 1) |
   | 2 | Champ | 3 | 299 | 0 | – | 1 | 49 | 0 | castle with the wall creature (tier 3) |
   | 3 | Hall | 3 | 1000 | 0 | – | 0 | 0xFE | 20 | treasure hall with 5 rooms, "1,000" |
   | 4 | Million | 3 | 1 234 567 | – | – | – | 0xFE | 0 | 7-digit commas |
   | 5 | Big | 3 | 12 345 678 | – | – | – | 0xFE | 0 | 8-digit number |
   | 6 | Stars3 | 1 | 70 | – | – | – | 0xFE | 0 | 3 stars |

   Then File > Start Another Game > y: the sign-in saved Tester and re-sorted the file (Big,
   Million, Hall, Champ, Stars3, Gate1, Tester).
2. With Champ signed in (castle), slot 4 (Stars3) → **Champ0**: tier 0, total 299, climb pts 1,
   room 49 (champion celebration without the creature); then Start Another Game again.
3. Writes by the game itself: the climb-end saves of Gate1 and Champ0, Champ's save at Start
   Another Game, the Exit save of the M7 test (§R5.3).

At the end the working copy was restored byte-identical from the backup; every intermediate file is
kept as `NAME.users` next to the state it belongs to (§R9).

## R2. E8 elf question (`code_1150-1160.md` §3.5–3.8, `ARCHITECTURE.md` §5.6) — confirmed pixel-exact

**Reaching it.** Carriers are recognisable by the white scroll (BNDL 315, PMAPs 1627–1634; BNDL
316 has none). A scan of the level-0 ring found one in rooms 16 → 15; state `r2_e8_r15_a` (room 15,
player x ≈ 350 facing left, carrier approaching) + `key wait:2900 space` catches it
(`e8_try4.avi`): nets −1 in the Space tick (f257), coins 10 → 11 one tick later (f265), the elf is
carried, net step 7 at f300, then the question one tick later: menu greyed f307, clue box mode 1
f309, question screen f311. The elf was elf 0 of level 0: question 39 (record slot 0, perm
{2,0,1}, so display position 1 holds answer 0 = the correct one).

**Layout** (`quizcheck.py`, play area rows 20..286, 136 704 predicted pixels, RGB tolerance 12):
PMAP 2000 opaque at (0,20); `lineSlots` 7, `numLines` 5 (one blank slot), lh 17, ansH 23,
textY = trunc((267 − (7·17 + 3·21))/2) + 20 = 62, textW 296 (even), textX 108, answers centred at
y 181/202/223 (ansY 178/199/220), ansX 241, ansW 30; speaker PMAP 2001 at (438,220). **0 mismatches
in all 27 states compared**: focus on answers 0/1/2, focus on lines 0..4 (highlight remap over
(textX, textY + lineY[k], textW, lh)), wrap-around, speaker focus, the reading highlights of lines
and answers (remap of (ansX, ansY+3, ansW, lh)), greyed answers (colour 8) with one and with two
tried positions, and the praise/sorry screens (below).

* **WinG frame quirk also here:** the two nested colour-4 focus frames and the speaker focus frame
  have their bottom edge one row high (rows y..y+h−2, as the dialogs of §4.1); with it they match
  exactly. `code_1150-1160.md` §3.5 should say so (the frame primitive is the WinG one, 1008:050b).
* Highlight remap confirmed: background → yellow (EGA 14), black → EGA 1, red (EGA 4) → EGA 6,
  yellow → EGA 1 (`PORTING_NOTES.md` §4.4, `code_1058-1090.md` `Gfx_HighlightRect`).
* Navigation (voice on): Down 0→1→2→10→…→14→0; Up from 0 → 14; Right/Left toggles the speaker
  focus; when the speaker gets focus the answer/line highlight is removed and `sel` resets to the
  first untried answer (Left then shows the frame on answer 0); Up/Down are ignored while the
  speaker has focus. With answer 0 tried: Up from 1 → 14, Down from 14 → 1 (0 is never focused);
  with 0 and 2 tried: Down from 1 → line 10. All as `Quiz_NextItem/PrevItem`.
* The question is **not** read automatically. Reading (`q_speakall.avi`, Enter on the speaker):
  line 0 highlighted f49, each next line replaces the previous one in the same frame (f319, 464,
  565, 670), the last line removed f840; answer 0 highlighted f892 (+743 ms = the 250 + 500 ms
  delays), removed f936; answer 1 at f971 (+500 ms), answer 2 at f1050 (+500 ms), removed f1094.
  The speaker frame stays.
* **Esc** (`q_esc.avi`): room restored three frames later; the remaining net steps run, the elf is
  released and walks away **still carrying the scroll** (f129); coins stay 11 (the +1 of the catch
  is kept), nothing else changes.
* **Correct, first try** (`q_correct1.avi`): praise screen right after Enter (f122): PMAP 2011 at
  (33,60) over the question screen, speaker icon redrawn, praise text m = 2 (DS:1ed3, three lines,
  clue word at y 136 + 3·17) — pixel-exact; m = 3 (DS:1f21, two lines, clue word at y 136 + 2·17)
  also pixel-exact in the second-try capture. CONTINUE (clue box mode 2) appears when the narration
  ends (f620, 7.1 s later); during the praise screen the HUD still shows 11 coins. Enter (f881):
  coins **13 (+2)**, menu re-enabled, the clue word in the clue box at (17,315) colour 15
  (pixel-exact with `hud.py` + text), the elf flees.
* **Wrong** (`q_wrong1.avi`): sorry screens m = 2 (DS:1fe0) and m = 3 (DS:200f) pixel-exact,
  CONTINUE after the 4.9-s narration. The elf keeps the question; re-caught (state
  `r2_e8_sorry_A`, then Enter, walk left 1.5 s, two swings) the same question comes back with the
  tried answer in colour 8 and the focus on the first untried answer.
* **Coins by try:** first try 11 → 13 (+2); second try 12 → 13 (+1; the 12 includes the +1 of the
  second catch); third try 13 → 13 (**+0**; the clue word is still won). Confirms §3.6 and
  `ARCHITECTURE.md` §5.6.

## R3. E7 castle-wall climb (`code_1168-1198.md` §2) — confirmed exactly, M8 confirmed

**Route** (`E7a_route_to_castle`). Gate1 resumed in room 25 at its start pose (83,142) with the
level-1 key icon filled. Up at x 98 entered the **door** (to room 30) instead of the gate: in
`Player_Update` the door test (`x < doorX(120) < x+w`) comes before the gate test
(`code_10e0-10f0.md` §4.3.3), so the player has to stand right of the door. At x 136: Up →
auto-walk 5 px/tick (f60–254), lock turned (f262), door-entry frames 9/10, key icon cleared
(f291), menu greyed, **cut-scene 1**: part 1 over room 25 (f300–357, 7 steps), black (f359), PMAP
2201 at (0,20) + part 2 (f364–443, 9 steps), black (f445), room 36 at its start pose (326,142,
frame 60) at f473. Step spacing 8–11 video frames in part 1 (mean 136 ms), 9–10 in part 2 (mean
141 ms): 7 fps as for cut-scene 0 (§4.3). Level 2 was then played with the key location from the
record (room 37 slot 2, kind 71): toss at x 294 facing right → key (level-2 key icon); room 38 at
x 112 (slot 1 = gate; its door at x 435 is not under the player); Up → auto-walk to 170 → room 49
with **no cut-scene and no black frame** (f193), key icon cleared.

**Room-36 fountain** (`code_10e0-10f0.md` §4.3.4, DS:0bd7/0bfe/0c26), tracked with `track2.py`:

| Action (from 326,142) | x per tick |
|---|---|
| walk left | the step to 296 lands in the zone → x := 326 (frame 1 drawn there); then bump frames 68,67,66,65,64,63,63,64,65,66,67,68 at x 326,318,325,340,331,341,341,331,340,325,318,326 (= −bumpDxL by step), y = frameY; then stand |
| jump left (Up+Left) | frames 12..21 at 336,325,315,306,259,232,189,114,83,67, then frame 1 at 37: every obstMaxXL test passed |
| jump left from x 400 | 410,399,389,380,333, then frame 17 drawn at **326** (306 > obstMaxXL[2] = 271 → bounce, jump cleared), then the bump sequence |

**Screen 0** (`castle_walk_RL.avi`, `castle_clamps.avi`): start x 80, y 142, frame 0, facing right
(x from `RoomDef` 49). **M8: walking right adds `dxLeft`**: x 80 → 110, 118, 133, 139, 162, 174, 191,
190 (+30,+8,+15,+6,+23,+12,+17,−1) instead of the `dxRight` steps of normal rooms
(+15,+18,+11,+9,+21,+12,+20,+4, §4.4); walking left subtracts the same `dxLeft`, so both walks are
exact mirrors in x. The coin toss (dx 0,+5,+5,−8,0,−2,0) and the net swing (+3,−17,+36,…) on this
screen use `dxRight` as specified and cost a coin / a net for nothing. Clamps: x + w > 496 →
x = 497 − w (frame 1 at 419, 3 at 425, 4 at 428, 5/6 at 426, 7 at 429; frames 2 and 8 land
unclamped at 427/428); x < 16 → 15 (16 stays 16).

**Climbing** (`climbtrack.py`, every value as in §2.7):
* Up at x 228 (226 < 256 < 228 + w): the climber appears one tick later already moved, frame 0 at
  (226,108) — the initial pose (226,120) of `ClimbLoop` is never drawn because the first loop tick
  climbs before drawing. Then frames 1,2,3,0,1 at (227,88), (226,76), (226,56), (226,44),
  (227,24): x = colX − 30 + climbDx[p], dy −12/−20 alternating; y −40 → screen 1 at y 227.
* Down: dy +12/+20 by phase; screen 1 → screen 0 when y > 231 (y −= 267); on screen 0 the climber
  steps off when y > 120 (frame 0 at (226,140), then walking frame 0 at (226,142)).
* Jump right: frames 6,7,8 at (244,125), (259,130), (294,125), then (313,119) = colX[3] − 30 (the
  last jump frame 9 and the re-attach frame 0 look the same: PMAPs 1104, 1148 and 1152 are
  byte-identical to 1100). Jump left: frames 8,7,6 at (294,125), (259,130), (244,136), then
  (226,130).
* Fall (jump into the empty column 2 of screen 2): frame 10 at (220,164), … screen change at y 260
  → −7, frame 14 at (227,23) on screen 1, ladder grabbed in the same tick, frame 0 at (226,23) next.
  Fall to the ground (column 0): …, frame 10 at (46,110) on screen 0 → y > 109 → landing frames
  18..23 at (52,142), (38,189), (56,172), (98,168), (99,152), (99,143), then walking at (99,142).
  Matches fallDx / fallDy(+32) / landDx / landY exactly.
* Screens of layout 0 drawn as `DrawScreenBackground` predicts: screen 1 full ladders in columns
  1–3, screen 3 a full ladder in column 3 and the stub (PMAP 1092) in column 1.
* **Wall creature** (Champ, tier 3, `champ_creature.avi`): appears in column 0 only (screen 1 has
  full ladders in 1–3) at x = colX[0] − 41 = 41, y ∈ {36, 40, 77, 122, 144, 153, 163} (all within
  20..197); frames 0,1,2,3,3,3,3,3,2,1,0, then hidden (12-tick cycle). With the climber in column 1
  at y 119 and the creature at y 163 (119 < 215 < 229) the push happened in the tick the creature
  reached frame 3 (f430): coins 10 → 9 in that tick, then jump frames 6,7,8 into column 2. A
  creature at y 36 (88 ∉ (119,229)) did not push. At tier 3 the pushes were frequent enough to
  knock the climber twice into the empty column 2 of screen 2 (falls), so the champion run used the
  tier-0 copy Champ0 (§R1.1).

## R4. E9 treasure hall (`code_1168-1198.md` §7, `ARCHITECTURE.md` §5.14) — confirmed exactly

Hall player, 20 climbs: rooms = min(9, (20−2)/6 + 2) = **5** (0..4). Room 0: treasure 19 at
(358,73), animating from the start; HUD with the five tier-3 gem icons (`hud.py` 0 mismatches).
Rooms 1–3 (bg 1037): treasures 18..13, 12..7, 6..1 at x {112,143,174,272,303,334}, y
{40,92,40,92,40,92}; frame 0 of BNDL 330+idx matched at offset (0,0) with 100 % for all 18. Room 4
(bg **1038**): treasure 0 on pedestal 0, five empty pedestals, and a wall: x = 437 − w (frames 1–7)
as in any wall room. Stepping through room 1 (`hall_room1_walk.avi`): exactly the pedestal whose
window [x, x+30] contains the player's centre animates (centres 132, 165, 198, 297, 330 → pedestals
0..4; centre 374 → none) and it returns to frame 0 when the player moves on. With climbs = 1
(Gate1's next climb) room 0 shows treasure 0.

**M6:** the pedestal windows are disjoint and the hall has no elves, so at most one hall animation
runs at a time [O windows; I no elves]; the busiest scenes seen were three elves with the player in
rooms 15/16, without visible drawing errors. The overflow cases of `code_1098-10d8.md` OQ6 were
not reached.

## R5. E10 celebration, rank screen, M7 (`code_10e0-10f0.md` §6, `ARCHITECTURE.md` §2.10) — confirmed

### R5.1 Gate1 (not champion; total 24 + 1) — `gate1_celebration.avi`
* Climbing off screen 3 (last climb tick f106) → black screen, menu bar removed (f115). The
  climb-end save is made here: on disk Gate1 has room 0xFE, the **old** total 24, climb points 1,
  key/gems/clues 0 (`sys_resources_saves.md` §6.5 point 2).
* PMAP 2299 at (0,20) presented alone (f178), DAM 20300 first step f179, then one step every 17–18
  video frames (250 ms = 4 fps) through DAM 20302 (the treasure, BNDL 330 entry 0 = image slot 80,
  flies into the chest) and DAM **20301**; PMAP 2202 + DAM 20308 (the climber sliding down) at
  f1488; rank screen at f1684 (11 steps of 20308 later). PMAP 2299 → PMAP 2202 took 18.7 s here and
  26.25 s in the champion run (20305 instead of 20301), i.e. the tick counts of the scripts
  (24 + 32 + 20 = 76, 24 + 32 + 48 = 104) within ±1 step (the champion path loads BNDL 324 in
  between).
* Rank screen (`rankcheck.py`, 137 216 px, **0 mismatches** at f1684, f1741 and f1853): PMAP 2380,
  name centred in (134,55, w 236), the total line in (128,217, w 245), labels at (121, 181−16i),
  band PMAP 2381 at (84,86) clipped to rows 212−16r−k .. 213−16r, rank names centred in
  (121, 197−16r, w 259). Iteration k of rank r identified frame by frame (`rankscan.py`): rank 0
  becomes visible at k = 1 (k = 0 draws rows 212–213, below the PMAP), one iteration per 3–4 video
  frames (50 ms), 56–57 frames (0.80 s) per rank: ranks 0, 1, 2 finished at f1741, f1797, f1853.
  The Exit dialog 1533 over the rank screen (no menu bar, game area at y 48) is pixel-exact too.

### R5.2 Champion (Champ0, total 299 + 1) — `champ_celebration.avi`
DAM 20300 → 20302 → **20305** (the crown) → PMAP 2202 + 20308 (f2137/2138) → rank screen f2332, all
eight bands filled one after the other, last change f2779 (8 × 56 frames); the final screen
("Champion", total 300) pixel-exact. File > See Rank (Hall, 1000) shows the same screen inside the
game frame (menu greyed) and restores the room afterwards.

### R5.3 Esc + Yes on the rank screen → **M7 confirmed**
From state `r2_gate1_rank`: Esc → Exit dialog 1533 → `y` → the program ends (Program Manager).
`USERS.DAT` afterwards, Gate1: total **25**, climbs **1**, room 0xFE, climb points still 1, and byte
**+0x09 = 0** — the stale tier (rank 2 needs tier 1). Re-launching the game and choosing Gate1
starts the next climb in room 0 with **two** gem icons (tier-0 `DS:0ec3`; `hud.py` 0 mismatches for
tier 0, 184 for tier 1): the stale tier is used for the whole next climb, as
`sys_resources_saves.md` §6.5 inferred. Contrast: skipping the celebration with Enter during DAM
20300 (`gate1_skip_enter.avi`) erases the sprites, shows **no rank screen** (§6.1 step 10 returns
at once) and the next climb has **three** gem icons (`Rank_UpdateTier` ran: tier 1).

## R6. E14 rescue coin pile (`code_10f8-1148.md` §7, `ARCHITECTURE.md` §5.9) — confirmed

From the sound-off room-9 state: 10 coins tossed (room 9 has no spots: coins lost), Enter with 0
coins → hint 6, 10 nets swung, Space with neither → hint 7 (empty swing). Then: door to the hall →
room 0: **no pile** (zone 0); back to room 9: **no pile** (the room where the player ran out);
right to room 10: pile PMAP 1699 at **(266,230)** (x = 50 + R(412), y 230) already in the first
presented room-10 frame (f187); left back to room 9: the pile **moved** to room 9 at x 282 (after
spawning, the no-spawn room is 0xFF, so even the room where the player ran out is allowed now);
walking into it: picked up in the first tick with 282 + 52 ≥ playerX (x 350 → 327), coins 0 → 10
in the same frame.

## R7. E15 Ctrl+S (visual side) — confirmed

* **During a hint voice** (`e15_hint2_ctrls.avi`, hint 2 started by Enter on hint 0, Ctrl+S 1.2 s
  later): the voice is cut and the hint is **dismissed** at once: CONTINUE (mode 2) is drawn for one
  frame (f206/207) and the play-field is restored in the next frame (f208), as `Hint_WaitDismiss`
  (`code_10f8-1148.md` §11.4: `Wave_PlayResUntilEvent` returns 1 for 0x13, the wait loop is
  skipped). Both flags were toggled: the next hint (6, Enter without coins) showed CONTINUE
  immediately (voice off → no `Wave_PlayResUntilEvent`).
* **During the title narration** (`e15_title_ctrls.avi`, title reached by relaunching the game from
  Program Manager, state `r2_title_p0`): Ctrl+S on story page 1 (and again later) changed nothing on
  screen: the page is not redrawn or advanced, the title animation keeps its 10-Hz steps, and the
  page changes only on Enter (`Title_WaitStep`, §12.5). Audio was not captured.

## R8. Sign-in list with stars, Champion and commas (`code_1150-1160.md` §5.5) — confirmed

`signin2.py` on the crafted roster (`E2b_signin_ranks/01`): **49 232 px, 0 mismatches** for
Champion with an **empty** number for 12 345 678 (8 digits, `1160:0cdb`), Champion with
"1,234,567" drawn right-aligned at 311 **over** the word Champion (no clipping), Champion + "1,000"
touching, six stars for 299, three for 70, one for 24, Trainee for 0; rows in descending total
order; equal totals (Champ, Champ0) keep file order (stable sort). After M7 the program-start list
shows Gate1 with two stars (25).

## R9. Save slots and states (end of round 2)

DOSBox-X was left running and parked at the state of **slot 4** (room 9 free play, the state it
had at the start of round 2), and the working-copy `USERS.DAT` was restored byte-identical to the
round-1 file. **Slots 1–10 are unchanged** (the table in §3 still applies; slot 10 was used as
scratch and its round-1 file put back; copies of all round-1 slot files and of the round-1
`USERS.DAT` are in `extracted/captures/savestates/round1/`).

Round-2 states (`python -P tools/tmctl.py state load round2/NAME`, which also restores the
matching `USERS.DAT`; `round2/r1_1 .. r1_10` are copies of the round-1 slots):

| NAME | State |
|---|---|
| r2_room9_soundoff | room 9 free play after Ctrl+S: Music&Sound **and** Voice off, 10 coins / 10 nets |
| r2_e14_coins0_nets1 | room 9, coins 0, nets 1 (one Space arms the pile) |
| r2_e14_room9_armed | room 9 after the hall round trip, pile armed (no-spawn room 9) |
| r2_e14_room10_pile | room 10 with the pile at (266,230) |
| r2_e8_r15_a | room 15, carrier elf approaching: `key wait:2900 space` catches it |
| r2_e8_quiz_A | question 39 on screen, nothing tried, voice on |
| r2_e8_sorry_A / r2_e8_sorry_A2 | sorry screen (CONTINUE) after the first / second wrong answer |
| r2_e8_quiz_A_tried0 / r2_e8_quiz_A_tried02 | re-caught elf, position 0 / positions 0 and 2 greyed |
| r2_signin_crafted | Club Sign-In with the crafted roster (Big … Tester) |
| r2_signin_roster2 | the same with Champ0 instead of Stars3 |
| r2_signin_startup_afterM7 | sign-in at program start reading the post-M7 file |
| r2_gate1_room25 / r2_gate1_room25_x136 | Gate1 in room 25 with the level-1 key at its start pose / at x 136 (Up = gate) |
| r2_gate1_room36 | room 36 start pose (fountain tests) |
| r2_gate1_room38 | room 38 at x 112 with the level-2 key (Up → room 49) |
| r2_gate1_castle0 | castle screen 0, tier 0 (layout 0, no creature) |
| r2_gate1_screen1 / r2_gate1_screen2_col3 / r2_gate1_after_fall / r2_gate1_screen3 | climbing states (screen 1 col 2 y 119; screen 2 col 3; screen 1 col 2 y 23; screen 3 col 3, hold Up = celebration) |
| r2_gate1_rank | rank screen after Gate1's climb (Esc + y = the M7 test) |
| r2_progman | Program Manager after that exit: Enter relaunches the game (title after ≈ 12 s) |
| r2_title_p0 | title page 0 of that fresh start |
| r2_gate1_climb2_room0 | Gate1's next climb in room 0 with the stale tier |
| r2_champ_castle0 / r2_champ_screen1 | Champ (tier 3): castle screen 0 / screen 1 with the creature active |
| r2_champ0_castle0 / r2_champ0_rank | Champ0 (tier 0): castle screen 0 / champion rank screen |
| r2_hall_room0 / r2_hall_room1 | Hall (20 climbs, champion): hall room 0 / room 1 |

## R10. Capture index (round 2)

All PNGs are exact 640×480 frames in `extracted/captures/ref/<folder>/` (`tmctl shot`, or decoded
from the AVI stored in the same folder). File names carry the frame / x / y where useful.

| Folder | Files | Content |
|---|---|---|
| E8_quiz | 01–04 catch (`e8_try4.avi` f265, 286, 300, 311); 05–17 focus states; 18–23 reading highlight (`q_speakall.avi` f100 … 1100); 24–25 Esc (`q_esc.avi` f93, 129); 26–28 praise first try, coins 13; 29–30 sorry; 31–34 re-caught with answer 0 greyed and the skipping focus; 35–36 second try (+1); 37–41 second wrong answer, two greyed, third try (+0); 8 AVIs |
| E7_castle | 01 screen 0 start; 02–09 walk right (`dxLeft`, M8); 10–17 walk left; 18–19 clamps; 20–25 grab and climb to screen 1; 26–29 jump right; 30–34 screen 2 and jump left; 35–37 fall and re-grab; 38–49 fall to the ground and landing frames 18–23; 50 screen 3; 51–54 climb down and step off; 55–57 coin/net wasted on screen 0; 58–65 creature cycle and push (tier 3); 66 pushed into a fall; 7 AVIs |
| E7a_route_to_castle | 01–02 room 25; 03–15 gate, cut-scene 1 parts 1/2, room 36; 16–23 fountain bump; 24–28 jump over the fountain; 29–30 late jump bounced; 31 key in room 37; 32–35 room 38 gate → room 49; 5 AVIs |
| E9_hall | 01 room 0 (treasure 19, tier-3 HUD); 02–07 room 1 and each pedestal animating; 08–09 rooms 2, 3; 10–11 room 4 (bg 1038, one treasure, wall); 12 room 0 with climbs = 1; 2 AVIs |
| E10_celebration | 01–20 Gate1: black, PMAP 2299, DAM 20300/20302/20301, PMAP 2202 + 20308, rank screen and band iterations; 21–23 Esc + y, Program Manager, next climb with tier 0 (M7); 24–31 champion run; 32–33 See Rank; 34–37 Enter skip (no rank screen, next climb tier 1); 4 AVIs |
| E14_coinpile | 01–05 spending coins/nets, hints 6 and 7; 06–07 hall and room 9 without the pile; 08–09 room 10 with the pile; 10 pile moved to room 9; 11–12 pickup; 3 AVIs |
| E15_ctrls | 01–04 Ctrl+S during hint 2's voice; 05–08 Ctrl+S during the title narration; 2 AVIs |
| E2b_signin_ranks | 01 stars/Champion/commas list; 02 list with Champ0; 03 program-start list after M7 |

245 new files (214 PNG, 31 AVI).

## R11. Summary for the spec owners

| Item | Status |
|---|---|
| E8 quiz layout `code_1150-1160.md` §3.5 | Confirmed pixel-exact (27 states); add the WinG bottom-edge quirk for the focus/speaker frames |
| E8 selection order, Esc, coins +2/+1/0, greyed answers, praise/sorry screens | Confirmed |
| Reading highlight timing §3.8 | Confirmed (250 ms + 500 ms before the first answer, 500 ms between answers) |
| **M8** castle right walk uses `dxLeft` | **Confirmed** (keep); coin/net on screen 0 use `dxRight` |
| Castle climb, jumps, falls, landing, creature §2.5–2.8 | Confirmed value by value; the initial climb pose (colX−30, 120) is never drawn |
| Door before gate in `Player_Update` §4.3.3 | Confirmed (Up under room 25's door enters room 30 even with the key) |
| Room-36 fountain §4.3.4 | Confirmed (bump table, obstMaxXL threshold at frame 17); closes the E5 leftover |
| Cut-scene 1 (level 1 → 2) | 7 fps, PMAP 2201, black before part 2 and before room 36; level 2 → 49: no cut-scene, no black |
| E9 hall §7 | Confirmed (room count, indices, positions, animation window, last room) |
| E10 order / 4 fps / band fill 16 × 50 ms | Confirmed; Enter/Space skip also skips the rank screen (worth stating explicitly in §6.1) |
| **M7** tier after Exit on the rank screen | **Confirmed stale** (+0x09 = 0 with total 25); the next climb runs with the old tier |
| E14 coin pile §7 | Confirmed; add "a pile left behind may move back into the room where the player ran out" |
| E15 Ctrl+S | Confirmed: dismisses a hint whose voice is playing (CONTINUE shows for one frame); the title page is unaffected |
| Sign-in stars/Champion/commas §5.5 | Confirmed pixel-exact, incl. the empty string for 8 digits and the overprint of 7-digit totals |
| M6 | Not reachable in the hall (one animation at a time); not stressed elsewhere |

Still not covered: E16 (mouse: H3, M10), E17/M2/M3 (audio), E18/M9 (16-colour mode), Esc during
the celebration animations. The level-0 → 1 cut-scene is round 1's E4.
