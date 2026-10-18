# AUTOPLAY_FINDINGS: soak test of the whole game with the autoplayer

Author: autoplay area (`src/tools/autoplay/`, `tests/test_autoplay.cpp`). Date: 2026-10-18.
Only files of the autoplay area (plus the include lines in `CMakeLists.txt` and one dispatch line in
`src/cli/commands.cpp`) were changed. Problems in other areas are reported here, not fixed.

## The autoplayer

`tmport-cli --autoplay` plays the real game (`game::gameMain`) headless in its own copy of the
bootstrap's parts (`autoplay::Rig`: no window, a silent AudioSystem with no wave device and the null
MIDI back end, a manual clock by default or `--real-clock`). A bot reads `GameState` and GameMain's
trace at every input fetch and injects keyboard / mouse input into the engine's input queue (menu
keys as posted SDL events, through the menu bar). It signs in a new player (or a crafted one), walks
the room graph to the spots that hide treasures and the key (it reads the hidden contents: it is a
test bot), tosses coins at the right third, sees elves on the screen (frame buffer vs back page,
`autoplay_vision.h`; checked by `--autoplay-vision-check`: 96 % of visible elves within 30 px,
median error -1 px) and nets them, answers elf questions by clicking the answer whose file index is
0 (some wrong or left with Esc on purpose), buys nets, spends everything on purpose to get the
rescue coin pile, jumps the room-36 fountain, unlocks the gates, climbs the castle wall (route
planned from the ladder layout, keeps out of the wall creature's reach, or lets it push once on
purpose), gets through the celebration and the rank screen, and repeats. Exercises: File > Start
Another Game + resume, Exit + a new program run that resumes (also on the castle wall), See Rank,
help pager, Options > New Player Messages, Ctrl+S, clue replay.

Checks (`autoplay_checks.h`): coins / nets never negative (16-bit wrap) and above the HUD's two
digits only as a note; haveKey, clue words, gems, shop coins, hint, tiers, rooms in range; exactly
one key per level (current level: hidden + haveKey = 1, later levels 1, passed levels 0); treasures
per level by tier (hidden + gems found); climb points = treasures found; tier = rank / 2 everywhere;
no exit of a mountain room leads to another level, the level never goes down within a climb; quiz
history 0..3 (+ dealt bit), slots hold a permutation of the answers, the right answer never marked
tried, a slot of the current or a later level holds a dealt or missed question, at most 9 dealt;
after **every** save: users.dat header + table round-trip byte-identical through the parser, each
slot through `PlayerRecord` and through `GameState`, roster sorted, the player's slot equal to
`toRecord(state)`; climb end arithmetic (climbs + 1, saturating total, rank, promoted / champion
flags); the record loaded after Start Another Game / Exit + restart byte-identical to the record
left. Hangs: no progress for 3 game minutes, a climb longer than 60 game minutes, 20 M pumps
without the clock moving, a wall-clock watchdog thread (no engine pump for N s, exit code 4).
Crashes: signal handlers (exit code 5). Resources: hot spots, DAM frame slots, pause depths and
queued input at every climb set-up; process memory and handles sampled from outside.

Reproduce anything below with the seed: runs are deterministic (manual clock, the bot's own RNG).
`--trace` prints the game trace and the bot's decisions, `--trace-ticks` adds every room tick.

## Soak results (Release, `build_autoplay`, TM_DATA = extracted TMTWINCD)

| Run | Command | Result |
|---|---|---|
| Main soak | `tmport-cli --autoplay --seeds 1-40 --climbs 6 --soak` | 40/40 clean: 240 climbs, 8 crafted players became Champion, 0 violations, ~55 s wall |
| Second soak | `--seeds 21-40 --climbs 8 --soak` (earlier build) | 20/20 clean, 160 climbs |
| Long sessions | `--seeds 101-104 --climbs 32 --soak` | 4/4 fresh players reached Champion by accumulation (total 345), 128 climbs |
| Real clock | `--seed 2 --climbs 1 --real-clock` | clean, 154 s wall = game time |
| AddressSanitizer | `build_asan` (`/fsanitize=address`): `--seeds 1-12 --climbs 4 --soak`, `--seed 101 --climbs 12 --soak`, `--seeds 41-44 --climbs 2 --soak --autorepeat 2`, `--autoplay-vision-check`, `tmtests -tc="autoplay*"` | no ASan report, all clean |
| Unit tests | `build_autoplay\tmtests.exe` | 314/314 test cases, 120 542 assertions |

Coverage of the 240-climb main soak (sum over seeds): 548 k room ticks, 20 k castle ticks, 6 371
coin tosses (0 missed), 1 968 treasures, 720 keys, 720 gate passages, 8 823 net swings, 6 376 elves
caught, 1 958 questions (1 465 right, 382 wrong, 111 left with Esc), 782 hints, 18 rescue piles, 34
wall-creature pushes, 5 falls, 1 464 fountain jumps, 388 sparkle hits, 20 Start Another Game, 62
Exits (42 resumed, all records identical), 322 menu trips, 206 dialogs (0 unexpected), 364 save
round trips, 549 k invariant checks. Memory over a whole soak process: private bytes flat at ~23.8 MB
(working set ~31 MB), no growth over 40 seeds; handles see A-2.

## Findings

| # | Severity | Area (owner) | Finding |
|---|---|---|---|
| A-1 | Medium | input (`src/input/input_queue.cpp`, `event_translator.cpp`) | Lossless input mode: arrow key-downs (keyboard autorepeat included) pile up as input codes; Enter / Space act seconds late |
| A-2 | Low | audio (`src/audio/midi_player.cpp`) | Each MidiPlayer thread leaks SDL's per-thread waitable-timer handle |

### A-1 Lossless input mode: arrow codes pile up, Enter / Space act late — Medium

**Resolution (2026-10-18):** fixed in `input_queue.cpp`: in lossless mode an arrow key-down that is
identical to the event still at the end of the queue is merged into it. `--seed 4 --climbs 1
--autorepeat 3` now waits 0 ticks; regression test "queue: lossless mode does not build an arrow
backlog behind Enter".

**Mechanism (observed in the code).** The engine default is `InputMode::Lossless`
(`src/platform/engine.h:54`). Every `WM_KEYDOWN` of an arrow produces an event
(`input_queue.cpp` `dispatch`, `KeyDown` case: `setEvent(kKey, 0x100 | vk)`), and in lossless mode
each produced event is queued (`input_queue.cpp:156`) and later fetches return them one by one
(`:161`). The translator turns every SDL key-down into a `WM_KEYDOWN`, including SDL's autorepeat
events (`event_translator.cpp:148`, `e.key.repeat` is not looked at). `Room_Run` fetches **one** code
per 100 ms tick (`room_run.cpp:157`), and so does the castle wall (`castle_climb.cpp:562`). A held
arrow with the usual Windows repeat rate (~30 per second) adds ~3 codes per tick while one is taken:
the queue grows by ~2 codes per tick. After walking for T seconds about 20·T codes are queued; a
following Enter (coin toss) or Space (net) is only returned when they are drained, about 2·T seconds
later, unless a room change (`Room_Present` flushes) comes first. The original did not have this:
its drain keeps only the first event (`FirstEventPerDrain`, `input_queue.h`), so held keys never
build a backlog.

**Reproduction (real game, deterministic).** `--autorepeat N` makes the bot send N extra key-down
repeats per tick while it holds an arrow, like a keyboard; the bot then has to hold back Enter /
Space until the queue is empty (counted as "waited"):

```
build_autoplay\tmport-cli.exe --autoplay --seed 4 --climbs 1 --autorepeat 0   -> waited 0,    2353 ticks
build_autoplay\tmport-cli.exe --autoplay --seed 4 --climbs 1 --autorepeat 1   -> waited 52,   1863 ticks
build_autoplay\tmport-cli.exe --autoplay --seed 4 --climbs 1 --autorepeat 3   -> waited 2700, 4569 ticks
```

With 3 repeats per tick the bot spent 2 700 ticks (4.5 game minutes of a 7.6-minute climb) waiting
for queued arrow codes. A human player holding an arrow and then pressing Enter sees the toss start
late, after the player has stopped somewhere else (the toss uses the position at the time the code is
taken).

**Suggested fixes (owner's choice).** Ignore SDL key-down events with `repeat` for the arrows in the
translator (the held flag is already set; the original's WM_KEYDOWN repeats were swallowed by its
one-event drain), or coalesce consecutive identical arrow events in the lossless queue, or let the
room loop and the castle use the one-event-per-drain rule. `--autorepeat 3` is a ready regression
check (expect "waited 0").

### A-2 Each MidiPlayer thread leaks SDL's per-thread waitable timer — Low

**Resolution (2026-10-18):** fixed: `MidiPlayer::run()` calls `SDL_CleanupTLS()` before the thread ends.

**Mechanism.** `MidiPlayer` runs its sequencer on a `std::thread` (`midi_player.cpp:16`) that sleeps
with `SDL_DelayNS` (`:101`). On Windows SDL implements that with a high-resolution waitable timer kept
in SDL thread-local storage (`SDL_systimer.c`, `SDL_GetWaitableTimer`), released only by SDL's TLS
cleanup, which runs for SDL-created threads or when a foreign thread calls `SDL_CleanupTLS()` before
it exits. The worker never calls it, so every AudioSystem destroyed leaves one timer handle behind.

**Reproduction.** Handle count of one process that plays 60 single-climb runs (a new Rig with its
AudioSystem per run), sampled from outside (`extracted\scratch\autoplay\memsoak.ps1`, which starts the process without a
window and polls its working set, private bytes and handle count every second):

```
tmport-cli --autoplay --seeds 101-160 --climbs 1              -> handles 151 -> 201
tmport-cli --autoplay --seeds 101-160 --climbs 1 --no-audio   -> handles 143 -> 143
```

Impact: one handle per AudioSystem; the game creates one per process, so only tools and tests that
create many (tmtests' audio cases, the autoplay soak) accumulate them. Fix: call `SDL_CleanupTLS()`
at the end of `MidiPlayer::run()`.

## Checked and consistent with the original (no action)

Each of these first tripped a naive invariant; the code matches the spec, so the checks were adapted:

* A wrong answer stores history 2 over the whole byte, dropping the dealt bit (`quiz.cpp`
  `quizJudgeAnswer`, ARCHITECTURE §5.6 "status := 2"); the slot keeps the question.
* The level-change carry-over ("broken swap", 1150:0375) leaves the missed question in the old
  level's slot; after the copy is answered on the next level the stale slot shows history 3. Only
  dead slots of passed levels are affected.
* Coins grow past 99 in long sessions (seen 106 in a 6-climb champion run, over 1 700 in a 139-climb
  session); the HUD shows them modulo 100 (ARCHITECTURE §5.7). Reported as a note by the soak.
* The clue box's hot spots (99, 0x409) stay registered after a climb ends; the count is flat (2) at
  every later climb set-up and a sign-in clears the table. Not a leak.
* GameMain traces "S10 level cut-scene: old level 2" for the castle gate; `LevelCutscene_Play`
  returns at once for level 2 as in the original (1188:0000 signed compare).
* Sparkle hits: a coin can go to an elf's sparkle while the bump / toss logic ignores input; the bot
  now tells a started toss by the first toss frame, not by the coin count.

## How to run

```
set BUILD_DIR=C:\Git\TM\build_autoplay
scripts\build.bat
set TM_DATA=C:\Git\TM\extracted\wincd\TMTWINCD
build_autoplay\tmport-cli.exe --autoplay --seeds 1-40 --climbs 6 --soak
build_autoplay\tmport-cli.exe --autoplay --seed 7 --climbs 2 --trace          (one run, every decision)
build_autoplay\tmport-cli.exe --autoplay-vision-check --seeds 8 --ticks 400   (elf detection accuracy)
build_autoplay\tmtests.exe -tc="autoplay*"                                    (one seed, one climb, twice)
```

An unknown option prints the usage (`autoplayUsage()` in `src/tools/autoplay/autoplay.cpp`). Logs of
the runs above (soak, long sessions, ASan, real clock, handle samples) are in
`extracted\scratch\autoplay\`.
