# REVIEW_1: correctness review of the stable engine areas

Reviewer: read-only correctness review (no repository file outside `re/reviews/` was changed).
Date: 2026-10-18. Tree state: working tree as of this date; another agent edited
`src/game/room/room_run.h` during the review, so room-module line numbers may drift slightly.

Scope: `src/game` core (game_state, rules, layout, quiz, room_table, game_tables), `src/game/room/*`,
`src/game/scenes/*`, `src/game/title.*`, `src/game/signin.*`, `src/text`, `src/ui`, `src/anim`,
`src/audio` (except the SoundFont files), `src/input`, `src/save`, `src/res`, `src/gfx`, `src/ne`,
`src/vfs`, `src/data`. Priority: per-frame code and code that parses user files.

## How this was checked

| What | Command / tool | Result |
|---|---|---|
| Release build | `set BUILD_DIR=C:\Git\TM\build_review` + `scripts\build.bat` | builds, 0 warnings (MSVC /W4) |
| Unit tests | `build_review\tmtests.exe` with `TM_DATA=C:\Git\TM\extracted\wincd\TMTWINCD` | 258/258 test cases, 113 739 assertions pass |
| AddressSanitizer build | `scripts\build.bat "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /GR /EHsc /fsanitize=address /Zi"` into `build_review_asan` (first attempt failed on another agent's in-progress `tests/test_mods_game.cpp`; the retry built) | builds |
| Unit tests under ASan | `build_review_asan\tmtests.exe` (same TM_DATA) | 286/286 test cases, 120 085 assertions pass, no ASan report |
| Room loop stress under ASan | `tmroom --room-run --new-player Zed --seed {1234,99,4242} --quiz {correct,wrong,esc} --keys-file <2 500 random walk/jump/net/coin/click steps> --ticks 30000` (9 runs, 50 game-minutes each, manual clock; one run passes the level-0 gate) | no ASan report |
| Castle climb stress under ASan | `tmport-cli --scene castle --tier {0..3} --no-audio --keys-file <1 500 random steps>` | climb completes (tiers 0-2), no ASan report |
| Other scenes under ASan | `--scene celebrate/seerank/cutscene 0,1,2/hint 33,34/quiz 358 ...` | no ASan report |
| Headless app under ASan | `tmport-cli --headless --keys "space,...,a,n,n,enter,...,quit"` with a scratch `--pref-dir` | no ASan report |
| Targeted repros | scratch harnesses in `C:\Git\TM\extracted\scratch\review1\` (`harness.cpp`, `harness2.cpp`, `buftest.cpp`, `rooms.py`, `castle.py`, `qlines.py`), linked against `tmcore.lib` | see the findings |
| Fidelity spot checks | decompiled reference `re/win/ghidra_export/functions/` | see "Checked and found consistent" |

No window was opened, no audio was played (headless runs and `--no-audio`), DOSBox-X was not used.

## Summary

| # | Severity | Status | Area | Defect |
|---|---|---|---|---|
| H-1 | High | confirmed | save | One interrupted or failed `users.dat` write can erase every saved player |
| M-1 | Medium | confirmed (mechanism) | save | A stale slot index overwrites another player's slot (two instances, or a save after a failed write) |
| M-2 | Medium | confirmed (ASan) | room | `RoomTable` indexed with room 255; reachable from a room table that the mod validator accepts |
| M-3 | Medium | confirmed (hang) | game core | Layout tables with fewer than 3 records hang climb set-up without pumping events |
| L-1 | Low | confirmed (parser), plausible (UB) | quiz | Questions with more than 10 lines overrun `lineY` / `lineSlotOf` and collide hot-spot ids |
| L-2 | Low | plausible | ne / vfs | Size and shift fields from the user's files are trusted: shift UB, multi-GB allocations |
| L-3 | Low | plausible | audio | `waitWhilePlaying` has no exit if the output device stops consuming |

No critical finding (no crash, UB or hang reachable with the shipped game files was found).

---

## High

### H-1 One interrupted or failed `users.dat` write can erase every saved player — confirmed

**Where**
* `src/util/file_util.cpp:33-40` `writeWholeFile`: opens the target with `std::ios::trunc` (the old
  contents are gone at once), writes, and returns `static_cast<bool>(f)` **before** the
  `std::ofstream` destructor flushes and closes; an error at the final flush/close is never seen.
* `src/save/users_file.cpp:85-91` `readUsersFile`: a missing, unreadable or short file becomes an
  empty table; `:117-127` `saveCurrentPlayer` then merges the player into that empty table and
  rewrites the file; `:103-113` `mergePlayer` writes the player at the remembered index and does
  `count++` only once, so the player can land behind an empty slot 0.
* The same helper writes `tmport.ini` (`src/save/settings.cpp:100`).

**Failure scenario**
1. The process dies (killed, crash in another thread, power loss, logoff) during a save, or the disk
   fills up: `users.dat` is left shorter than 11 085 bytes (0 bytes if it dies right after the open).
   With a full disk the failure is not even reported: at the success check only 8 192 of the 11 085
   bytes have reached the OS (repro below), the rest is written by the destructor, unchecked, so the
   "Write Error" box (DS:2084) is not shown.
2. On the next start the roster is empty. Any save (a new player, or a still signed-in player's
   Exit / climb end) rewrites the file from the empty table: every other player is lost for good.
   The same happens if a read fails transiently (a backup, sync or antivirus tool holding the file in
   `%APPDATA%\TMPort\TMPort\`).

**Evidence**
* `buftest.cpp` (same code shape as `writeWholeFile`): `at the success check: ok=1, file size on
  disk=8192 of 11085` / `after the destructor: file size=11085`.
* `harness.cpp` H1, using the engine's `save::saveCurrentPlayer`: roster `[0]Ann(50) [1]Bob(30)
  [2]Cy(10)`; file truncated to 5 000 bytes (an interrupted write); Bob (slot 1) saves:
  `save attempted=1 written=1`, re-read `count=0`. Ann and Cy are zeroed; Bob sits in slot 1 behind
  an empty slot 0 and is invisible too.
* `re/docs/sys_resources_saves.md` §6.9 lists both behaviours as "edge cases worth reproducing or
  fixing"; `PORTING_NOTES.md` §4.10 requires the byte-exact format and save order, not the data loss.
  The existing test `tests/test_save.cpp` "save with a single player and with a missing file"
  codifies the deleted-file edge case, so this is a decision to take, not an accident.

**Suggested fix** (the file format stays byte-identical)
* `writeWholeFile` (or a dedicated `writeFileAtomically` used for `users.dat` and `tmport.ini`):
  write `users.dat.tmp`, `flush()` + `close()` and check, then replace the target
  (`std::filesystem::rename`, which replaces on POSIX and uses `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`
  with MSVC); keep the previous good file as `users.dat.bak`.
* In `saveCurrentPlayer`, distinguish "file absent" (empty roster, as the original) from "file present
  but unreadable or short": then do not overwrite; try `users.dat.bak`, else show the Write Error box.
* In `mergePlayer`, if `sel >= count` after the re-read, write the player at `count` (append) rather
  than behind empty slots.

---

## Medium

### M-1 A stale slot index overwrites another player's slot — confirmed (mechanism)

**Where** `src/save/users_file.cpp:103-113` (`mergePlayer` writes `slots[sel]` of the freshly re-read
file without checking that the slot still holds this player); `src/game/signin.cpp:189-201`
(`saveRecord` passes `userSel_`). No single-instance guard exists in the port (searched `src/` for
an instance mutex / lock file: none). The original refuses a second instance (`1018:0000`, ARCHITECTURE
§2.1 S1).

**Failure scenario**
* Two copies of `tmport` (double-click twice; one minimised and forgotten) share `users.dat`.
  Session 1 signs in Bob (slot 1), session 2 signs in Cy (slot 2). Cy passes Bob and session 2 saves:
  the file is re-sorted to `[Ann, Cy, Bob]`. Session 1 then saves Bob into slot 1, which now holds Cy.
* Single instance: a save whose write failed (H-1, or a transient lock) still updates `sel` to the
  merged order that never reached disk; the next save writes into the wrong slot.

**Evidence** `harness.cpp` H2 (engine code): start `[0]Ann(50) [1]Bob(30) [2]Cy(10)`; session 2 saves
Cy(40): `[0]Ann(50) [1]Cy(40) [2]Bob(30)`; session 1 saves Bob(32) with its stale index 1:
`[0]Ann(50) [1]Bob(32) [2]Bob(30)` — Cy's progress is gone and Bob is duplicated.

**Suggested fix**
* `mergePlayer`: use `sel` only if `slots[sel]` holds the player's exact name (the original's own
  assumption); otherwise re-find the name (`findByName`), and if absent append at `count`. Identical
  output to the original whenever its assumption holds.
* Add a single-instance guard (named mutex / lock file in the preference directory) — owner: the
  platform/app area (`main.cpp` / `src/app`).

### M-2 `RoomTable` indexed with room 255, reachable from a mod-validated room table — confirmed (ASan)

**Where** `src/game/room_table.h:46-47` (`operator[]` unchecked). The door path:
`src/game/room/player.cpp:249-250` (door test: `x < doorX < x + w` and type 1/2, no check of the
exit), `:311-312` (`nextRoom = r.exitDoor`), `src/game/room/room_run.cpp:253` (commit
`pl.room = nextRoom`), then `player.cpp:156` (`s_.rooms[pl.room]` in `boundsAndExits`). The mod
validator explicitly accepts 255 as "wall / no door" (`src/mods/table_overrides.cpp:206`) but leaves
`doorX`/`typeDoor` alone. A second path: `src/game/room/elves.cpp:257-260` (a fleeing elf moves to
`exitLeft` even when both exits are walls, giving `e.room = 255`; a later flee reads
`s.rooms[255]`).

**Failure scenario** A mod sets `"rooms": {"9": {"exitDoor": 255}}` to remove room 9's door (shipped
`doorX` 270, `typeDoor` 1 stay). The player presses Up in front of the old door: after the 2-tick
step-up the room becomes 255; `visitRoom` tolerates it (every helper there checks `valid()`), but the
next `Player_Update` reads `rooms[255]`, 7 140 bytes past the start of `GameState::rooms` and about
4 KB past the end of the heap-allocated `GameState`, so exits and levels are garbage (or an access
violation).

**Evidence** `harness2.cpp` (ASan build, engine `PlayerController`, EXE tables from TM_DATA):
`tick 2: doorStep 0 nextRoom 255` → `AddressSanitizer: heap-buffer-overflow ... READ of size 1 ...
in PlayerController::boundsAndExits player.cpp:157`, "4162 bytes after 3712-byte region" allocated
for `GameState`. The shipped table pairs every `exitDoor` 255 with `doorX` −100 (`rooms.py` dump), so
unmodded play is safe; the original has the same unchecked test.

**Suggested fix** Door test also requires `RoomTable::valid(r.exitDoor)` (no change with shipped
data); `RoomLoop::run` refuses to commit an invalid `nextRoom` (log, stay); the elf `-2` branch keeps
the room when the target is 0xFF; a bounds assert in `RoomTable::operator[]` (debug) or a checked
accessor. Mods side: when `exitDoor` is 255 require `doorX` −100 or `typeDoor` ∉ {1,2} (mods area).

### M-3 Layout tables with fewer than 3 records hang climb set-up — confirmed

**Where** `src/game/game_tables.cpp:96` rejects only an empty OTHR 10550+tier;
`src/game/layout.cpp:53-63` `pickLayoutRecords` re-rolls until three distinct records are drawn.

**Failure scenario** A mod replaces OTHR 10551 (raw bytes, no validation) with a table of 1 or 2
records; the first new climb of a tier-1 player never returns from `Layout_GenerateClimb`. Nothing
pumps events inside the loop, so the window stops responding and only killing the process helps
(which can then hit H-1).

**Evidence** `harness.cpp` H3: `pickLayoutRecords(2, rng)` did not return within 2 s ("endless
re-roll loop"). Shipped tables hold 500 records per tier.

**Suggested fix** Require at least 3 records per tier in `GameTables::load` (FormatError naming the
OTHR id), or fall back deterministically with a warning when `count < 3`.

---

## Low

### L-1 Questions with more than 10 lines overrun the line tables — confirmed (parser), plausible (UB)

**Where** `src/game/quiz.cpp:282` (`numLines = lineSlots - 1 - blankCount`, unbounded) vs
`src/game/quiz.h:115,124` (`lineSlotOf` has 10 entries) and `src/game/scenes/quiz_screen.h:71,84`
(`QuizLayout::lineY[10]`, `lineHighlight(k)` unchecked); used for every line in
`src/game/scenes/quiz_screen.cpp:129-131` (hot spots `kLine0 + k`), `:205-206` (`speakAll` →
`highlight(10 + k)` → `lineHighlight(k)`), `:432`.

**Failure scenario** A mod question (OTHR 11000+q) with 12 non-blank lines, Voice on, speaker button:
`lineHighlight(10..11)` reads past `lineY` (inside `QuizLayout`, so ASan does not flag it) and
highlights a garbage rectangle; the line hot-spot ids `0xA70 + k` reach `kPlayArea` (0xA7F) at 16
lines and the answer ids (0xA80..) from 17 lines, and `HotspotTable::set` then moves the answer's
rectangle onto the line, so clicking that line chooses an answer and the answer box stops reacting.

**Evidence** `harness.cpp` H4: a 12-line question parses with `numLines=12`, `lineSlotOf.size=10`,
`QuizLayout::lineY.size=10`. Shipped data: at most 6 non-blank lines (question 8; `qlines.py` over
all 359 questions), so unmodded play is safe.

**Suggested fix** Clamp `numLines` to 10 when parsing (log), or reject questions with more than 10
lines in the parser so the screen never sees them.

### L-2 Size and shift fields of the user's files are trusted — plausible

**Where** `src/ne/ne_image.cpp:55,67` (`alignShift`) and `:91,112-113` (`rsrcShift`) are used as shift
counts straight from the file (UB for ≥ 32; silent truncation for 16..31); `:297`
`readResource` resizes to the declared rounded length (up to `0xFFFF << shift`, about 4 GB,
zero-filled), and it runs during `NeImage::open` for the type-15 table.
`src/vfs/cd_image_source.cpp:242-243` reads a whole directory whose size comes from the ISO record
(up to 4 GB).

**Failure scenario** A damaged or foreign `TMT256.DLL` / CD image: multi-GB allocation (bad_alloc or
heavy paging) or UB instead of the clear "missing/invalid file" error the design asks for.
Not executed (it would commit gigabytes on this PC); evidence is the code.

**Suggested fix** Reject shift values above 15 and resource lengths that end beyond the file size plus
one alignment unit (FormatError naming the file); clamp ISO directory sizes to the image size.

### L-3 `waitWhilePlaying` has no exit if the output device stops consuming — plausible

**Where** `src/audio/audio_system.cpp:173-174` loops `while (voice_.isPlaying())`; `isPlaying()` ends
only when `SdlWaveOutput::done()` sees `SDL_GetAudioStreamQueued(stream) <= 0`
(`src/audio/sdl_wave_output.cpp:79-81`). Callers: the quiz praise and sorry screens
(`src/game/scenes/quiz_screen.cpp:263,291`), `jingleThenSpeak`.

**Failure scenario** The playback device disappears (USB headset unplugged with no other device, a
driver reset) while the praise jingle plays: the stream is no longer drained, the loop pumps forever
and ignores input, so the game is stuck on the result screen. Not reproduced (needs a device
removal).

**Suggested fix** Bound the wait by the sound length (`samples / rate` + a margin), or clear the stream
on `SDL_EVENT_AUDIO_DEVICE_REMOVED`.

---

## Observations (no action needed, or other owners)

* Castle creature placement re-rolls while the column has a full ladder
  (`src/game/scenes/castle_climb.cpp:281-282`); it would hang on a screen with five full ladders.
  The shipped layouts never have one (`castle.py`), and the castle tables are not in the mod
  table-override list, so it is unreachable today; keep it in mind if castle tables become moddable.
* `ExeTables::repointString` (mods) appends to the DGROUP image and can reallocate it, which would
  dangle the `string_view`s in `GameTables::words` / `ClueState::text`; it is documented to run before
  any view exists, and `bootstrap.cpp` loads `GameTables` afterwards. Worth an assert (mods/app).
* `MidiDriver` keeps the saved music position in 32 bits where the original truncates to 16 bits
  (`fmt_sound.md` §8.3 marks the audible effect as an inference); a deliberate, harmless deviation.
* A menu command can be delivered inside the `flushInput()` of `Room_Present` (Fetch pump during a
  room transition). That matches the original (Input_Flush dispatches WM_COMMAND), no change needed.

## Checked and found consistent

Thread safety: `MidiPlayer` (worker thread, every sequencer and back-end access under `mu_`, thread
started last and joined first), `MidiSequencer` note tracking (velocity-0 note-ons are note-offs),
`AudioSystem` member destruction order (music stream leaves the wave device first), no SDL C callback
in scope can see a C++ exception.

Parsers on user data, all bounds-checked: PMAP + RUND (truncation/overshoot), FFNT (glyph pool), SMF
(VLQ, running status, chunk lengths, tempo math in 64 bits), BNDL (count vs size, nesting depth 8),
SSND, RT_CURSOR, DGROUP accessors, DlgDesc/PagerDesc, the text renderer (span end = NUL, every skip
bounded), DAM interpreter (operands past the end read 0, op budget per tick, null frames skipped).

Save data robustness: out-of-range record values are tolerated — room (`setupClimb` resets it),
tier (clamped everywhere), quiz slots (`QuizScreen::run` validates the question), gems/clues/shop
counters (guarded loops and lookups).

Behaviour against the decompiled reference: `Room_Run` tick order (`10e0:007d`), the head of
`Player_Update` incl. coin slot and mouse walking (`10e0:0447`), `CoinPile_Update` (`1110:00aa`),
`Elf_UpdateAll` / `Elf_CheckNetCatch` (`1178:048d` / `03bb`), elf draw-time sounds (`1178:0766`),
`Room_EraseSlotReveal` (`1198:0499`), `Climb_Celebrate` incl. its skip paths (`10f0:0000`),
`Midi_PlayTrack/Stop/Pause/Resume/OnPlayDone` (`1090:0388..04bf`), `Quiz_CarryMissedToNextLevel`
(`1150:0375`), hot-spot allocator semantics (lowest free slot, hit test from slot 49 down, the
sign-in's `Hotspot_ClearAll` after a restart).

Portability: no Win32 outside `platform/`, the WinMM back end and a guarded default path in
`settings.cpp`; no `char`-signedness or `long`-size dependence found; little-endian dependence is
explicit (`static_assert` in `presenter.cpp`); no game text in the reviewed sources.

## Areas not reviewed (or only skimmed)

* `src/ui/game_dialog.cpp` layout/drawing (only the modal and pager loops and the music nesting were
  read); `src/ui/menu_bar.cpp` beyond command dispatch and tracking.
* `src/input/event_translator.cpp`, `cursors.cpp` beyond the resource decoder, the cursor controller.
* The command-line tools (`*_cli.cpp`, `room_tool_main.cpp`, `src/cli`), `gfx/image_export`,
  `util/png_writer`.
* 16-colour mode specifics (TMT16 path, nibble keys) beyond the decoder.
* Excluded by the task: `src/app`, `src/platform`, `main.cpp`, `src/mods`, `res/mod_overrides`, the
  SoundFont files in `src/audio` (`midi_backend_soundfont.*`, `sf2_*`, `midi_render.*`).
* Linux/macOS builds were not run (static review only). The tests themselves were not reviewed.
