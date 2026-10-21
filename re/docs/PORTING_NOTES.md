# Porting notes: a C++17 / SDL3 engine for Treasure Mountain! (Windows CD)

How the original modules map onto a modern engine that loads the user's original files at run
time, which behaviours must be reproduced exactly, and where an asset-override / mod layer fits.
Read `ARCHITECTURE.md` first; the topic docs in `re/docs/` hold the algorithms.

Ground rules carried over from `re/CONTEXT.md`: the engine contains no game art, sound, text or
tables and no copied decompiled code. Everything game-specific is read from the user's
`TMTWINCD\` directory at start-up: `TMTWINCD.EXE` (DGROUP tables, cursors, dialog templates),
`TMT.DLL`, `TMT256.DLL` (and optionally `TMT16.DLL`), `TM0SOUND.DLL`..`TM8SOUND.DLL` and
`SOUNDS\{B,E}*.MID`.

Tags: **[O]** observed in the binary (via the topic docs), **[I]** inferred, **[R]** a
recommendation of this document.

---

## 1. Inputs and validation

| File | Needed for | Check [R] |
|---|---|---|
| `TMTWINCD.EXE` | DGROUP tables (rooms, words, animation tables, menus, dialog texts, voice-id tables, rank tables), cursors 128–130, icon 15000, dialog templates | NE header; auto-data segment 56, length 0x66A2; `"USR\x1A"` at DS:2101; `"RUND"` at DS:0997 |
| `TMT.DLL` | BNDL 300–415, FFNT 999, OTHR 10550–20308, RRGB 900 | type-15 name table maps BNDL/FFNT/OTHR/RRGB to 32513–32516 |
| `TMT256.DLL` | PMAP 1000–2448 (8 bpp, mostly RUND) | 764 PMAPs |
| `TMT16.DLL` | optional 16-colour art | 764 PMAPs, same ids and sizes |
| `TM0..8SOUND.DLL` | SSND 4000–6615, 7000–7552 | 2861 sounds in total |
| `SOUNDS\B*.MID`, `E*.MID` | 10 tunes × 2 | SMF format 0 |
| `users.dat` (optional) | import of existing players | 11 085 bytes, magic not required (the original ignores it on read) |

A missing file must stop the engine with a message naming it (the original posts a WM_QUIT that is
swallowed and later dereferences NULL, `sys_resources_saves.md` §4).

The engine must not depend on `C:\TLCWIN\TMTWINCD\`: keep settings and `users.dat` in the user's
preference directory (`SDL_GetPrefPath`) and offer a one-time import from the original path.

---

## 2. Module map: original → engine

Proposed engine modules (directories / main classes) and the original code they replace. "Data
from EXE" lists what the module reads from DGROUP at start-up (offsets in `dgroup_map.md`).

| Engine module | Main classes | Replaces | Data from EXE | Spec |
|---|---|---|---|---|
| `platform/` | `App`, `Window`, `EventPump`, `Clock`, `AudioDevice` | 1010 (WinMain, WndProc), 1018 (window, checks), 1008 (WinG), 1020 (cursor API), 1080 (Windows dialogs) | cursors 128–130 (EXE resources) | code_1008-1030 |
| `ne/` | `NeImage` (segments, relocations, resource table, type-15 name table) | KERNEL loader + `FindResource` | – | sys_resources_saves §5.1 |
| `data/` | `ExeTables` (DGROUP reader with relocation fix-ups) | DGROUP initialised data | all tables (§5) | dgroup_map, `tools/assets/dgroup.py` |
| `res/` | `ResourceManager`, `Resource`, `Bundle` | 1038 (type/DLL routing, BNDL trees, scratch, lock-in-place), 1030 (memory) | – | code_1038-1050 §2 |
| `gfx/` | `PixMap`, `RundCodec`, `Palette`, `FrameBuffer` (512×1055 indexed), `Blitter`, `Presenter`, `ScreenFx` | 1048 (palette), 1050 (blitters), 1058 (graphics core), 1070 (clear, dissolve, fade stubs) | EGA table DS:07d8, 16-colour table DS:0838, dissolve tables DS:09ae/09be | fmt_pmap, code_1058-1090 §4/§6 |
| `text/` | `Font` (FFNT), `TextLayout` | 1060, 1068, 10b0 | – | fmt_bndl_ffnt §3–5, code_1098-10d8 §7 |
| `anim/` | `DamEngine` (16 channels, frame table, sprite/erase/update lists), `ModalAnimPlayer` | 10b8, 10d8 | – | code_1098-10d8 §8–9, fmt_othr_tables §4 |
| `input/` | `InputQueue` (input codes), `HotspotTable` (50 slots), `HeldKeys`, `MouseState`, `CursorController` | 1028, 1098, 10a0, 1020 | – | code_1008-1030 §6, code_1098-10d8 §4 |
| `timing/` | `DeadlineTimers` (8 slots), `busyWait` replacements | 10a8 | – | code_1098-10d8 §6 |
| `audio/` | `WaveVoice` (single voice, SSND), `SpeechPlayer` (blocking, stop codes), `SfxPlayer`, `MidiSequencer`, `MusicManager`, `PauseNesting` | 1088, 1090, 1128, 1170, 10d0 | music tables DS:09fa/0a0e/0a22, stop lists DS:0f54/1414/1e01/1e17/1e1d/1e43 | fmt_sound |
| `ui/` | `GameDialog` (DlgDesc layout/draw/modal loop), `Pager`, `MenuBar`, `SystemPrompt` | 10c0, 1078, 1120, 1140, 1180, 1080 | DlgDescs DS:14d6..2234, MenuBarDef DS:13bf, button/footer strings DS:0b24..0b6d, PagerDesc DS:2cfe | code_1098-10d8 §10, code_1058-1090 §7 |
| `save/` | `UsersFile`, `UserRecord`, `Roster` | 1158, 1160 (load/save/sort/remove), 1040 | – | sys_resources_saves §6, code_1150-1160 §4–5 |
| `game/main` | `GameMain` (top-level state machine), `RestartToSignIn` / `QuitGame` exceptions | 1118, setjmp/longjmp | elves per tier DS:128e, cursor ids DS:1292 | code_10f8-1148 §8 |
| `game/title` | `TitleScreen` | 1138 | voice tables DS:141c/1424 | code_10f8-1148 §12 |
| `game/signin` | `SignInScreen` | 1160 | strings DS:2390–23ce, DlgDescs | code_1150-1160 §5 |
| `game/room` | `RoomScene` (compose, draw order, transitions), `RoomTable` | 1198, 1108 | rooms DS:23d8, slot x DS:2dd4/2dda | code_1168-1198 §8 |
| `game/player` | `Player` (controller, frame tables), `RoomLoop` | 10e0 | DS:0ba0–0e89 | code_10e0-10f0 §4 |
| `game/hud` | `Hud`, `Inventory`, `Rank` | 10e8 | DS:0ea0–0f66 | code_10e0-10f0 §5 |
| `game/celebration` | `Celebration`, `RankScreen` | 10f0 | DS:0f8c–1055 | code_10e0-10f0 §6 |
| `game/layout` | `ClimbLayout` (records → room slots), `Clues` | 1100 | word tables DS:1079–1287 | code_10f8-1148 §5, fmt_othr_tables §2–3 |
| `game/netshop`, `game/coinpile` | `NetShop`, `CoinPile` | 10f8, 1110 | DS:1066, 0e89 | code_10f8-1148 §4, §7 |
| `game/hints` | `HintQueue` | 1130 | DS:13d0, 1414 | code_10f8-1148 §11 |
| `game/quiz` | `QuizPool`, `QuestionParser`, `QuestionScreen` | 1150 | DS:1990, 1c60, 1dc7, 1de7, 1df1, 1e07 | code_1150-1160 §3, fmt_othr_puzzles |
| `game/elves` | `ElfManager`, `Elf`, `ElfBehaviour` | 1178 | DS:2ac8–2ca7, 128e | code_1168-1198 §4 |
| `game/castle` | `CastleClimb` | 1168 | DS:2950–2aaa | code_1168-1198 §2 |
| `game/hall` | `TreasureHall` | 1190 | DS:2d7e–2da8 | code_1168-1198 §7 |
| `game/cutscene` | `LevelCutscene`, `GateLock` | 1188 | DS:2d6d/2d71/2d75 | code_1168-1198 §6 |
| `game/help` | `HelpTopics` | 1180 | titles DS:2d1c.. | code_1168-1198 §5 |
| `rtl/` | `BorlandRand` (LCG + `R(n)`), `sprintf("%s"/"%lu")` | 1000 | – | code_1000 §5, §9 |

Not ported: 11a0/11a8 (Borland EH/string), 10c8 and 1148 (dead), 1108 (empty hooks), WinG,
GDI-heap tricks, `midimap.cfg` probing, the INI.

### 2.1 Suggested class model

The original has no classes; this is a design proposal [R]:

```
App ── EventPump, Clock, Window, AudioDevice
 └─ Engine
     ├─ GameFiles { NeImage exe, tmt, pmap, sound[9]; ExeTables tables; }
     ├─ ResourceManager (owns decoded caches; returns shared_ptr<const Resource>)
     ├─ FrameBuffer (512x1055 u8) + Presenter (palette → RGBA texture, scaling)
     ├─ DamEngine, HotspotTable, InputQueue, DeadlineTimers
     ├─ WaveVoice, MidiSequencer, MusicManager
     └─ GameMain (state machine; holds GameState)
          GameState { Player, Inventory, RoomTable (mutable copy), ClimbState (clues, layout),
                      QuizState, HintState, ElfManager, TreasureHall, Settings }
          Scenes: TitleScreen, SignInScreen, RoomLoop, QuestionScreen, HintPopup, LevelCutscene,
                  CastleClimb, Celebration, RankScreen, HelpPager, GameDialog
```

Keep the original's global-state style inside `GameState` (one struct per original module) so
that the topic-doc pseudocode maps line by line; wrap it in classes only at module boundaries.

---

## 3. Control flow: replacing Win16 blocking loops

The original is written as nested blocking loops: every modal screen (title, sign-in, room loop,
question, dialog, pager, modal animation, speech wait) spins on `Input_GetCode` / `timeGetTime` and
lets Windows run only inside the event fetch. Menu commands run *inside* whichever loop is pumping
messages (WM_COMMAND → `Menu_OnCommand`), and Exit / Start Another Game leave via `longjmp`.

Recommended structure [R]:

1. **Keep the nesting.** Run the game logic on the main thread as ordinary nested functions, and
   make every wait primitive pump SDL events and present the frame:
   * `InputQueue::getCode()` = pump events (SDL3 requires the main thread), translate them into
     the original event model (§4.7), return the next input code or 0.
   * `DeadlineTimers::expired(slot)` / `waitUntil(slot)` / `delayMs(ms)` = poll the clock, pumping
     events (and, for fidelity, **discarding** input that the original would have missed during
     busy waits: `Delay_ms` drops nothing because it does not pump; the input is seen afterwards).
   * `WaveVoice::isPlaying()` = query the audio stream.
   This keeps every topic-doc state machine in its original shape and makes the port testable
   against the docs.
2. **Menu commands** are delivered only from inside a pump, never during a resource load (the
   original blocks them with `g_inResourceLoad`), and never re-entrantly (the original disables the
   menu bar around handlers and modal sequences: `Menu_EnableAll(0)`).
3. **Non-local exits**: throw `RestartToSignIn` (value 1) or `QuitGame` (value 2) from the handlers;
   catch them in `GameMain` at the place of the original `setjmp` (`1118:004c`). Before catching,
   the original's reset block frees all per-climb resources; with RAII this is automatic, but the
   state reset (`g_playerHidden`, steps, `g_coinTargetSlot`, …, `code_10f8-1148.md` §8.1 step 6) must
   be done explicitly.
4. **Focus loss**: while the window is inactive the original freezes game logic (the event fetch
   does not return), cuts the wave voice and pauses MIDI. Reproduce: `EventPump` blocks while
   unfocused or minimised [R: optional "pause on focus loss" setting, default on].
5. **Window close / OS quit request**: convert to input code 0x1B (Esc); the game's own Esc
   handling asks for confirmation. Do not close the window directly.

---

## 4. Per-module notes and must-keep behaviours

### 4.1 NE image reader

* Parse MZ → NE header (offset at file +0x3C); segment table (NE +0x22, shift NE +0x32);
  per-segment relocation records (needed to resolve far pointers stored in DGROUP: in the file they
  hold `FF FF 00 00` chains); resource table (NE +0x24): `alignShift`, TYPEINFO/NAMEINFO records,
  file offset = `offset << shift`, length = `length << shift` (rounded size).
* Type lookup: map the Mac type name through the DLL's type-15 table (`{u16 12, u16 typeOrd|0x8000,
  u16 id, "TYPE\0", "\0"}`); equivalently hard-code BNDL/PMAP/SSND = 32513, FFNT = 32514,
  OTHR = 32515, RRGB = 32516 per DLL.
* Payload true lengths are not stored: PMAP/SSND/BNDL/DAM have their own length fields; OTHR text
  ends at the first NUL. Keep the rounded size available: `GetResourceInto` copies the rounded size
  (RRGB 900 = 1024 bytes into a 1024-byte buffer).

### 4.2 Resource manager

* Type from id range and DLL from type + id exactly as `sys_resources_saves.md` §5.2–5.3; ids that
  no DLL serves (SSND 6800–6999, 8000–9999) are "not found" (the original is undefined).
* `GetResource(id)` returns a private copy; BNDL members are loaded recursively and in order. The
  quiz parser edits its copy in place, so always hand out copies (or parse into a structure).
* No cache in the original; the engine may cache decoded immutable data freely. Do not emulate
  the 280 000-byte scratch buffer or its leak quirks.
* PMAP colour remapping on load is a Windows palette artefact; skip it (§4.3).

### 4.3 PMAP, palette, transparency

* Decode per `fmt_pmap.md`: header `u16 w, u16 h`; TMT256 = 8 bpp raw or `"RUND"` + PackBits-style
  RLE whose packets cross rows (decode the whole `w·h` stream, then slice rows); TMT16 = 4 bpp, row
  stride `(w+1)/2`, high nibble = left pixel.
* Palette: RRGB 900 (256 × RGB), set once, never animated.
* Compose in **logical** indices; transparency = pixel whose RGB is (0,0,0xAA) (indices 15 and 241;
  only 15 occurs). 16-colour art: key nibble 3, colours = Windows VGA device palette
  (`fmt_pmap.md` §1).
* The white-key mirrored draw (`1058:0cf3` with flag ≠ 0) is never used by shipped data; implement it
  for mods only (key = RGB FFFFFF, i.e. indices 0 and 77).
* UI pen colours are EGA numbers 0..15 (DS:07d8). In 256-colour mode the original uses the nearest
  palette index; the port can draw the EGA RGB directly when composing in RGB, or precompute
  "nearest RRGB index" to stay in indexed space.

### 4.4 Frame buffer, drawing and presentation

* One 512×1055 8-bit buffer with the three layers of `ARCHITECTURE.md` §3.1.1; the clip rectangle is
  inclusive and global; `PixMap_DrawSprite` is keyed, clipped, optionally mirrored about its own box
  and adds a dirty rect; opaque draws and rectangle copies are unclipped.
* Keep the per-tick erase → update → draw order and the foreground draw order
  (`ARCHITECTURE.md` §3.1.4). Many visuals depend on erasing from layer 1 and on layer 2 holding the
  background without slot pictures.
* Presentation: the original presents rectangles (dirty list, max 256, overflow silently dropped)
  [O]. A port may present the whole 512×384 every tick [R]; the only visible difference is that the
  original could miss an update when more than 256 rects are queued (not known to happen).
* Window: the original centres 512×384 in a maximised 640×480 window with a caption and menu bar
  and fills the rest black. Offer integer scaling with letterboxing; draw the menu bar in-engine
  (§4.11).
* Transitions: implement `Room_Present` cases 0 (instant) and 1 (with the stubs: for room 9 and
  zone 0, black fill + present before the redraw). Implement the dissolve (case 2) for mods; shipped
  room data never selects it [O this synthesis]. Pace the dissolve at one pass per 1/60 s [R]; the
  original ran it as fast as 17 full blits allowed.
* `Gfx_HighlightRect` (reading highlight) is a LUT over the destination: every colour → yellow
  (EGA 14), black → EGA 1, EGA 4 → EGA 6, EGA 14 → EGA 1 (nearest indices). Un-highlight by
  restoring from the back page.

### 4.5 Font and text

* FFNT: index glyphs by raw byte (no transcoding); key-cap glyphs 0x80–0x95 must render as in the
  font. Keep all text as raw bytes end to end; only the menu bar needs conversion if drawn natively.
* `TextLayout` must reproduce `Text_Render` byte for byte: wrap only at spaces with the test
  `x + spaceW + nextWordW > x0 + w − 1`; wrapped lines are not re-centred; CR and LF each start a
  line; centring uses signed `/2`; `FF 02` underline hides `_`; `FF 03 c` sets EGA colour c−1;
  `FE` returns to the margin; `FF 01 n` substitution and the `FF 04` tab-stop quirk exist but are
  unused. Measure and draw share one routine, so the quiz box width, dialog sizes and centring all
  depend on it.
* No typewriter effect (timer slot 6 is never armed).

### 4.6 Animation (DAM)

* Implement the opcode set exactly (`code_1098-10d8.md` §8.1), including: unknown bytes are 1-byte
  no-ops; `FA` count 0 loops forever and does not end the frame; the stop flag turns the next opcode
  into `FF`; `Anim_Start` returns 0 both for channel 0 and for "no free channel"; a script that ends
  `FE FF` erases its last frame, one that ends `FD … FF` (only 20300) leaves it on screen.
* Frame table: 100 slots; animation bundles fill from their base; treasure-hall pedestals use
  20/30/…/70; slot 80 = delivered treasure (set before DAM 20302).
* Sizes: the original never bounds-checks (100 sprites, 100 erase rects, 16 update rects). Use
  growable vectors [R].
* Modal player: first frame immediately, then every `1000/fps` ms (integer division); Enter returns
  1 and Space 2 when skippable; Esc calls the `onEsc` callback (the quit dialog) and the animation
  **continues** afterwards if the player answered No (`10d8:0082` loops while the channel runs).

### 4.7 Input

Translate SDL3 events into the original event model [R], then into input codes exactly as
`Input_GetCode` does (`code_1008-1030.md` §6.3):

| SDL event | Original | Input code |
|---|---|---|
| text/character keys, Enter, Esc, Backspace, Space | WM_CHAR | the byte (0x0D, 0x1B, 0x08, 0x20, …) |
| Ctrl+R / Ctrl+S | WM_CHAR 0x12 / 0x13 | 0x12 (remove player) / 0x13 (sound toggle: handled inside the pump, then also returned) |
| PgUp, PgDn, End, Home, arrows, Ins, Del | WM_KEYDOWN VK 0x21..0x2E | 0x100 + VK; arrows also set/clear the held flags on key down/up |
| mouse move | WM_MOUSEMOVE | updates mouse x/y in game coordinates; calls the cursor hook |
| left/right button down/up | WM_xBUTTONxxx | hot-spot id under the pointer with flags 0x8000 / none / 0x4000 / 0x2000; 0 when no hot spot |
| window close / quit | WM_CLOSE, SC_CLOSE, QUERYENDSESSION | 0x1B |

Must keep:
* the 50-slot hot-spot allocator (lowest free slot, reuse of the same id, hit test from slot 49
  down, half-open rectangles); priorities between overlapping spots depend on it;
* held-arrow flags polled by the player controller (movement is not event-driven);
* mouse-directed walking (hot spot 0x600 latched while the button is held);
* `Input_Flush` after resource loads and around modal screens (input typed during loads is lost);
* the speech wait discards non-matching input and leaves the interrupting code queued.

Optional [R]: the original keeps only the **first** event of each message drain and drops the rest
(fast typing in the name box can lose characters). Offer a "lossless input" setting, default off
for fidelity.

Cursors: arrow by default; over the play-field (0 ≤ x ≤ 512, 20 ≤ y < 287) the left/right walking
cursors (EXE GROUP_CURSOR 129/130) chosen by the direction of the last mouse movement; forced arrow
during modal screens. Read the cursors from the EXE (Win16 cursor resources) or use system cursors.

### 4.8 Timing

| What | Value | Where |
|---|---|---|
| Game tick (room loop, castle climb, title, treasure hall) | 100 ms | timer slot 7 |
| Celebration animations | 4 fps (250 ms) | `Anim_PlayModal` |
| Level cut-scenes | 7 fps (142 ms, integer `1000/7`) | `Anim_PlayModal` |
| Title page timeout | 60 s | timer slot 4 |
| Rank band fill | 16 × 50 ms per rank | timer slot 4 |
| Name caret blink | 333 ms half period | timer slot 0 |
| Dialog button flash | 4 × 62 ms | `Delay_ms` |
| Quiz read-aloud pauses | 250 ms after the lines, 500 ms before each answer | `Delay_ms` |
| Net-shop door, coin toss, net swing, bump, elves, creature | counted in ticks | per module |

One logic update per tick, no catch-up: if a tick overruns, the next one starts late. Use a
millisecond clock (`SDL_GetTicks`); the 49.7-day wrap of `timeGetTime` is irrelevant.

### 4.9 Audio

* **Wave voice**: one stream; any new sound (effect or speech) stops the current one; effects are
  fire-and-forget; speech is blocking with stop codes (`fmt_sound.md` §5.2); a jingle (sfx 12/8) is
  allowed to finish before praise/sorry speech starts. SSND = 8-bit unsigned mono PCM at the header
  rate (always 22050 Hz): feed it to an `SDL_AudioStream` as `SDL_AUDIO_U8`.
* **Settings**: `soundOn` gates music and effects, `voiceOn` gates speech; Ctrl+S toggles both
  (both on only if both were off); sign-in can switch sound off but never on (keep this quirk:
  `1160:13ce..1415`); a new player switches it on.
* **Music**: SDL3 has no MIDI. Options [R]: (a) Windows: own SMF sequencer + `midiOutShortMsg` to
  the system synth (closest to the original MCI path); (b) portable: an embedded software synth
  (e.g. TinySoundFont/FluidSynth with a user-supplied General MIDI SoundFont for the `E` files, or an
  OPL emulator such as libADLMIDI for an authentic "base-level" rendering of the `B` files). Default
  to the `E` files [R].
* Music rules to keep: one tune at a time; every tune loops from its start except OPEN1 (once); the
  title switches to OPEN2 when OPEN1 stops (BOPEN1 has ~54 s of trailing silence, EOPEN1 does not);
  music stops for elf questions and hints and restarts from the beginning afterwards; it pauses and
  resumes (at the saved position) around game dialogs, the pager, Windows dialogs, menus and focus
  loss; zone changes only restart music when the zone differs; rooms 0 and 9 start their music
  after the transition.

### 4.10 Saves and settings

* `users.dat` byte-exact (`ARCHITECTURE.md` §4.3): read 4-byte header (ignored) + 11 081 bytes;
  short file = empty roster; write = header `"USR\x1A"` + table; save = re-read, merge current
  player, stable insertion sort by total treasures (descending, unsigned), re-find the player's
  index by byte-exact name, write. Keep the unused bytes (+0x1E0..0x4FF, +0x62C) as read; keep spot
  content value 1 unchanged.
* Save points and their order (`sys_resources_saves.md` §6.5) matter for compatibility of the
  stored values (climb-end save before the score is added; Exit saves twice; Remove Player writes
  even after "No").
* Robustness deviations (`re/reviews/REVIEW_1.md` H-1, M-1; same format, same save order): writes are
  atomic (`users.dat.tmp` + rename) and keep the replaced valid roster as `users.dat.bak` (`tmport.ini`
  likewise); a missing file is an empty roster, but a present short/unreadable one is replaced by the
  backup's roster or, without a valid backup, never overwritten (Write Error box DS:2084); the merge
  writes slot `sel` only if it holds the player's name, else re-finds it or appends at `count`.
* The INI keys are start-up check flags only; replace them with an engine config file. Nothing in
  the INI is gameplay state.

### 4.11 UI

* Game dialogs (`DlgDesc`): reproduce the layout tables of `code_1098-10d8.md` §10.2–10.4 exactly
  (they assume 19 px per text line; actual line advance is the font height 17). Types 1 (message)
  and 8 (Yes/No: "No" is button 0 on the right) are used; 0 (non-modal, name entry) via `Dlg_Draw`;
  4–7 are pager pages. Save-under and restore are pixel copies.
* Menu bar: the original is a Win16 menu (File: See Rank / Start Another Game / Exit; Options: New
  Player Messages / Music and Sound / Voice; Help: four topics / About). Draw it in-engine above the
  game area (SDL3 has no native menu bar) with keyboard access (Alt+F/O/H) [R]. It exists only while
  a climb is being played and is disabled during modal sequences. Command handlers return to the
  pumping loop; See Rank saves and restores the play-field.
* Start-up warning dialogs (`TOOBADNOSOUND`, `NOTOPTIMUMDISPLAYMODE`, …) are Windows-environment
  checks; drop them [R] or replace them with the engine's own error reporting.

### 4.12 Game logic modules

Port each module from its topic doc; notes on traps:

* **GameMain**: the climb loop; new-climb generation only when room = 0xFE; music deferral for rooms
  0/9; the reset block before sign-in; the end-of-climb order (save before celebration).
* **Player controller**: the frame tables are absolute y and per-frame dx; `dxRight` is added when
  facing right, `dxLeft` subtracted when facing left; the sprite is mirrored when facing left and x is
  always the left edge. The state precedence of `code_10e0-10f0.md` §4.3.2 decides what a key does.
* **Room 36 fountain**: the obstacle test uses per-jump-frame thresholds (DS:0c18/0c26).
* **Quiz**: the broken swap; one attempt per catch; the line hot spots use `k·lh` while highlights
  skip blank lines; Esc records nothing; silent-answer list; the question text is not read
  automatically.
* **Elves**: only one throw at a time (global state); a fleeing elf's −2 step leaves even when
  confined; `elf_drawYOff` is not applied to the net-ride and thrower draws.
* **Treasure hall**: do not start animations for empty pedestals (the original leaves a stale
  channel handle; visually equivalent).
* **Castle climb**: walking right on screen 0 adds `dxLeft` (copy-paste quirk, keep it); skip the
  creature erase when its frame is −1 (visually equivalent).
* **Celebration**: the skip path leaks resources in the original (irrelevant).

---

## 5. Data the engine reads from `TMTWINCD.EXE`

All offsets are DGROUP (`dgroup_map.md`); far pointers inside these tables must be resolved through
DGROUP's relocation records.

| Group | Offsets |
|---|---|
| Player animation | 0ba0, 0bac, 0bb8, 0bc6, 0bd4, 0bd7, 0be4, 0bfe, 0c18, 0c26, 0c34, 0c89, 0d33, 0ddd, 0e89 |
| HUD / rank | 0ea0, 0ec3, 0ec7, 0eef, 0f17, 0f54, 0f5a, 0f60, 0f66, 0f8c, 0fa8, 0fc8, 0fd8, 0e8f, 0fe8, 0fee |
| Clue words | 1079, 10b9, 10f9, 1119, 112d (pointer tables → strings 1141..1287) |
| Net shop, coin pile, elves per tier, cursors | 1066, 128e, 1292 |
| Menus | 13bf (MenuBarDef), 1383 (popups), item strings 1311..137f |
| Music | 09fa, 0a0e, 0a22 (file name templates with a placeholder first byte) |
| Hints, title | 13d0, 1414, 141c, 1424 |
| Dialogs | DlgDesc 14d6, 1533, 15cc, 1625, 163f, 1919, 2084, 2129, 21e6, 2200, 221a, 2234; button/footer strings 0b24..0b6d |
| Quiz | 1990 (u16[360]), 1c60, 1dc7, 1de7, 1df1, 1e07, 1e01, 1e17, 1e1d, 1e43, 1f59 |
| Sign-in | 2106, 2143, 2257, 2390, 23a2, 23b3, 23c4, 23cc, 23ce |
| Rooms | 23d8 (50 × 0x1C; make a mutable copy) |
| Castle climb | 2950..2aaa (note the 1-based tables, `dgroup_map.md` §1.2) |
| Elves | 2ac8, 2acb..2c1f (behaviour scripts), 2c31..2c7d |
| Help, cut-scenes, hall, scene | 2cfe, 2d1c.., 2d6d, 2d71, 2d75, 2d90, 2d9c, 2da8, 2dd4, 2dda |
| Colours, effects | 07d8, 0838, 09ae, 09be |

`tools/assets/dgroup.py` is the reference decoder (`--check` validates the region map).

---

## 6. Behaviours that must be preserved exactly

| Area | Behaviour | Why it matters |
|---|---|---|
| RNG | Borland LCG `seed = seed·0x015A4E35 + 1`, `(seed>>16)&0x7FFF`; scaling `rand()·n/32768` (signed 32-bit); `srand(low16(timeGetTime()))` once per climb-loop pass | layouts, quiz deals, elf behaviour; also needed for replay tests (allow a fixed seed in debug builds [R]) |
| Tick | 100 ms logic tick, one update per tick | all movement and animation speeds are per tick |
| Layout | layout-record → room-slot table; slot x {26,197,368}, y 90; reveal x {49,220,391}, y 152; thirds of 170 px | where hiding places and treasures appear |
| Controls | Space = net, Enter = coin, arrows (held), Up = door/gate/jump, `c` = replay clues, Esc = quit prompt, Ctrl+S = sound | |
| Hot spots | allocator and priority | mouse behaviour on overlapping spots |
| Economy | coins/nets start 10/10; costs and gains of `ARCHITECTURE.md` §5.7; refusal below 0; display modulo 100 | |
| Quiz | deal, tiers, promotion 80 %, coins +2/+1/0, broken swap, one attempt per catch | |
| Score | thresholds {0,5,25,70,115,170,230,300}; tier = rank/2; saturating total | |
| Hints | once-only bits consumed when queued; always-shown events; one pending | |
| Audio | single voice; cut rules; music restart after hints/questions; OPEN1 → OPEN2 | |
| Saves | file format, sort, merge-on-save, save order, sound-on-load quirk | compatibility with existing `users.dat` |
| Text | `Text_Render` wrap/centre rules | box sizes and line breaks of every text screen |

### 6.1 Quirks: keep, make optional, or drop

| Quirk | Source | Recommendation [R] |
|---|---|---|
| Castle screen 0: walking right adds `dxLeft` | 1168:09e3 | **keep** (confirmed in the original, M8, per REFERENCE_CAPTURES §R3) |
| Stale tier byte (+0x09) after an Exit from the end-of-climb screens | 1118:03e3, 10f0 | **keep** (confirmed, M7, per REFERENCE_CAPTURES §R5.3; save compatibility) |
| Holding Down keeps the current frame instead of standing | 10e0:0447 | keep |
| Quiz line hot spots use `k·lh` (ignore blank lines) | 1150:0a97 | keep |
| Broken swap of missed questions | 1150:0375 | keep (save compatibility) |
| Sign-in never turns sound on | 1160:137d | keep |
| Numbers with ≥ 8 digits drawn as empty text on the sign-in list | 1160:0cdb | keep (unreachable in practice) |
| Right click on the clue box shadowed by hot spot 0x409 | 10e8:0658 | keep by porting the allocator |
| Only the first event per message drain is kept | 1028:004f | optional |
| Transition type 1 black flash in room 9 / hall | 1198:0527 | keep (verify) |
| Treasure-hall signed index after 128+ climbs | 1190:009b | keep the arithmetic, or clamp [R: clamp] |
| Creature erase with frame −1 | 1168:10d2 | drop (equivalent) |
| WinG frame off-by-one (bottom edge of `FrameRect` one row high, `1008:050b`) | 1008 | **keep** (corrected per `REFERENCE_CAPTURES.md` §4.1/§4.11: visible in every game dialog and in the sign-in name box; pixel equality with the captures needs it; implemented in `gfx::FrameBuffer::frameRect`) |
| Stale `WM_MOUSEMOVE` coordinates, VK 0x29–0x2C jump table | 1010 | drop |
| Missing-DLL behaviour, uninitialised sound ids, scratch/BNDL 327 leaks, celebration leak | 1018, 1038, 10f0 | drop |
| Tab-stop and substitution measure quirks in the text engine | 10b0 | keep (unused by shipped text; matters only for mods) |
| A save after a deleted/short/unreadable `users.dat` drops the other players; a stale `sel` overwrites another player's slot | 1160:0f28, 1158:0040 | **drop** (REVIEW_1 H-1 / M-1; see §4.10) |
| SSND sample counts take in one stray byte after the trailing silence in ~930 of the 2861 sounds (`..., 0x80, 0x80, 0xCE`): an audible click at the end of the sound | 1088:0014 (plays `count` bytes) | **drop**: playback fades the last 32 samples to 0x80 (`audio::fadeOutTail`, `SdlWaveOutput::start`); decoded sounds and WAV exports keep the original bytes |
| Unchecked data-driven limits: door/elf moves to room 0xFF, layout re-roll with < 3 records, > 10 question lines | 10e0:0447, 1178, 1100:04b5, 1150:0901 | **drop** (REVIEW_1 M-2 / M-3 / L-1; unreachable with shipped data, reachable with mods) |

---

## 7. Asset-override and mod layer

> **Implemented** (`src/mods/`): the formats, folder layout, constraints and tools as built are
> documented for users in [`docs/MODDING.md`](../../docs/MODDING.md); its "Limits" section lists
> what of the plan below is not done.

The original addresses every asset by a numeric id and every table by a DGROUP offset, which gives
natural hook points [R]:

1. **Resource override** at `ResourceManager::find(type, id)`: before reading the DLL, look in
   `mods/<TYPE>/<id>.*`. Suggested formats: PMAP → PNG (indexed with RRGB 900, or RGBA with the
   key colour (0,0,170) or alpha), SSND → WAV (any rate; resampled), OTHR text → raw bytes file
   (keep the byte encoding and `FF` escapes), DAM → binary script or a small text assembler,
   BNDL → JSON list of ids, RRGB → 768-byte file, FFNT → binary.
2. **Table override** at `ExeTables`: JSON files keyed by table name (rooms, clue words, rank
   thresholds/names, gem positions, menus, dialog texts, hint table, voice-id tables, animation
   tables). The engine copies tables into mutable structures anyway (the original edits several in
   place), so overrides are cheap.
3. **Text / localisation**: a string table keyed by resource id or DGROUP offset, still rendered with
   FFNT byte codes; a replacement font is an FFNT override. Text layout depends on glyph widths, so a
   font change re-flows text but needs no code change.
4. **Music**: per-tune override `mods/MIDI/<9000..9009>.mid` (or OGG for pre-rendered tracks), and a
   choice of B/E set and synth.
5. **High-resolution art**: allow an override PNG to declare a scale factor (e.g. 2×) while keeping
   the logical width/height. Constraints: the code uses logical PMAP sizes for layout (player frame
   widths `g_frameW`, sign-in row size from PMAP 2181, hit tests, centring), so logical sizes must stay
   those of the original resource.
6. **Scripts and data-driven behaviour**: DAM already drives cut-scenes and treasure animations;
   implement the unused features (F9, FB, mirror/stamp bits, opaque channels, "DOP" tag, dialog types
   2/3/9) so that mods can use them.

Constraints a mod must respect (validate on load): resource ids and types (the id range decides the
type), BNDL member counts the code indexes (player 69 frames, HUD 10, sign 6, fountain cycles any
count, elf sets ≤ 10 frames), question text grammar (three CR end the question, three answers,
answer 0 correct), and voice-id tables matching the question line counts.

---

## 8. Verification strategy

* **Decoders**: compare against the Python reference tools (`tools/assets/pmap.py` incl. byte-exact
  RUND re-encoding, `ssnd.py`, `bndl.py`, `ffnt.py`, `othr_tables.py`, `othr_puzzles.py`,
  `dgroup.py`). Port their `verify` modes as unit tests.
* **RNG and generation**: Borland `rand` test vectors (`code_1000.md` §5); layout generation from a
  fixed seed compared with `othr_tables.py` (`BorlandRand`, `pick_records`, `place_record`).
* **Text layout**: render OTHR 11605 and 11000 and compare with the `ffnt.py` previews.
* **Reference captures**: drive the original under DOSBox-X with the engine's `tools/tmctl.py` (keyboard only) and
  compare frames (`GAPS.md` §4 lists the scenes).
* **Save compatibility**: round-trip real `users.dat` files byte for byte through load/save.

## 9. Suggested milestones

1. NE reader, ExeTables, ResourceManager, PMAP/RRGB decode, FrameBuffer + Presenter (show any PMAP).
2. FFNT + TextLayout + GameDialog (render all dialog descriptors and help pages).
3. Input/hot spots/timers + Title + Sign-In + users.dat.
4. RoomScene + Player + HUD + layout generation (walk the mountain, search spots, net shop).
5. DAM engine + treasure hall + cut-scenes + celebration + rank screen.
6. Elves + quiz + hints.
7. Castle climb, audio (wave, MIDI), menus, options.
8. Fidelity pass against emulator captures; mod layer.

---

## Open questions

1. Which MIDI back end gives acceptable fidelity on all platforms (system synth on Windows,
   SoundFont or OPL emulation elsewhere)? A design decision; the original's B/E choice was driven by
   the installed synth.
2. Should the port reproduce the "first event per drain" input loss by default? Fidelity vs comfort.
3. Is 16-colour mode (TMT16) worth supporting? It needs the VGA palette and nibble key 3; the
   exact driver intensities are unknown (`fmt_pmap.md` OQ3).
4. How should the Win16 menu bar look in the port (in-engine bar vs. overlay menu)? The original's
   behaviour (disabled during modal sequences, handlers run inside pumping loops) is defined; the look
   is not.
5. Behaviours still to be confirmed by running the original (`GAPS.md` §4) may change some
   recommendations in §6.1.
