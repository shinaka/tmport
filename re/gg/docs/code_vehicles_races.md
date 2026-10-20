# ssg.exe (G&G DOS v1.0): parts, HUD, workshop and blueprint, races, results, final movie

Scope (wave 1, "vehicles and races" topic): the part catalogue and the parts model (segment
`153f` and the inventory rules of `1834:08d7` / `1834:0990`), the HUD and its parts inventory
strip (`1a04`), the workshop and the blueprint viewer (ovl08 `2dee`, state 6), race preparation
(states 8, 10, 11 in `12e0`), the two race overlays (ovl24 `3dd6` land and alternative energy,
ovl25 `3e77` aircraft, ovl26 `3f50` rounding division), the results screen, rank message,
post-race menu and final movie (ovl10 `2fcb`, states 12..15), building completion and trophies.
Wave 3 engineers implement these modules from this document without reading the binary.

Evidence: the named Ghidra export `re/gg/ghidra_export/` (decompiled C and, wherever the C is
unreliable, the disassembly: every switch table here was read from the bytes), the bytes of
`ssg.exe`, and the decoded data of the user's archives (`tools/gg/ggarc.py`). Scratch scripts are
in `extracted/scratch/gg/wave1/vehicles/`: `dis.py` (disassembly per function), `exe.py` (DGROUP
and code-segment byte readers), `cat3.py` (part catalogue dump), `paths.py` (race paths),
`seqscan.py` (animation sets: opcodes and durations), `racesim.py` and `airsim.py` (reference
simulations of the two race models, used for the test vectors of §18). The spec was written by
static analysis; the round 2 captures that followed (`REFERENCE_CAPTURES.md` §9.2-§9.4, §10, §11)
confirm the race pacing, the count-up, the rank message after every win, the rewards, the
completion bonus, the movie and its palette, and agree with the race model (§19).

Notation: **[O]** observed in code, bytes or decoded data; **[I]** inferred; **[R]** recommendation
for the engine. Addresses are Ghidra
`SSSS:OOOO` of the G&G project; `DS:XXXX` is DGROUP `1ea7` (file offset `0xFC70 + XXXX`); overlay
code `SSSS:OOOO` is at the overlay's file offset + `OOOO` (`overlay_map.tsv`). Names are the
canonical ones of `re/names/gg/`, which since the wave 1 merge include this topic's names and
those of the world and platform topics used here (the parts helpers of `153f`, `Actor_AddPart` /
`Actor_RemovePart`, the HUD functions of `1a04`, the sprite list, screen animations, picture
remap, music cues, pick-up; `re/names/gg/README.md` records the merge). A *tick* is one PIT tick (145.652 Hz, 6.866 ms); a *sound tick* is one `Snd_Tick` (every
2nd PIT tick, 72.83 Hz). "R(n)" is Borland's `rand()·n / 0x8000` (`ARCHITECTURE.md` §8).
Fixed-point values are 16.16 in a u32 (high word = integer part). Picture-drawing calls are
written `Lpx_DrawKeyed(x, y, picture, mode)`: the function is Pascal (`RETF 0xa`), so the
decompiled C prints the arguments in reverse order (mode, picture, y, x) [O 1399:02ee];
`Lpx_Draw(x, y, picture)` likewise [O 1399:0335].

Cross-references: rooms, actors, chimps, the animation-sequence player (174b) and the state
machine are the world/sequencer topic; the sound driver and banks (1bb4, 1cdd) the platform topic;
dialogs and buttons the UI topic. Their interfaces with this topic are summarised where used.

---

## 1. Module overview

| Seg / ovl | Functions | Role |
|---|---|---|
| `153f` | `Parts_LoadCatalogue` 0002, `Parts_FreeCatalogue` 00fd, `Part_GetType` 015d, `Part_GetShelfPicId` 0173, `Part_IsNeeded` 0189, `Part_Record` 019f, `Parts_ShuffleDeck` 01b6, `Parts_DealNext` 0203, `PartList_CountType` 0216, `PartList_FindType` 0276, `PartList_CountHeldBy` 02bb, `Parts_NeededCount` 0316, `PartList_NextColumn` 0323, `PartList_PrevColumn` 0354, `Parts_SaveState` 0385, `Parts_RestoreState` 03c9 | the part catalogue of the race and its run-time fields (§3) |
| `1834` (part) | `Actor_AddPart` 08d7, `Actor_RemovePart` 0990 | inventory rules: at most 2 parts of a category, pairs (§3.5) |
| `1a04` | 27 functions, `Hud_Update` 0b69 | HUD: labels, counters, part name box, inventory strip and its commands (§4) |
| ovl08 `2dee` | 29 functions, `State_06_Workshop` 1a90, `Workshop_Run` 0c89, `Blueprint_Run` 1894 | workshop: shelf conveyor, vehicle assembly, paint, race-track door, blueprint viewer (§5, §6) |
| `12e0` (part) | `State_08_LeaveRoom` 09aa (vehicle branch), `State_10_ListVehicleParts` 0506, `State_11_Race` 0564, `Score_Add` 0ace | race preparation and dispatch (§7) |
| ovl24 `3dd6` | `Race_MapPathFrames` 0000, `Race_StepSpeed` 0034, `Race_AdvancePoint` 00e0, `Race_Run` 01ca, `Race_ComputePerformance` 044e, `Race_LandEnergy` 0762 | land and alternative-energy race (§8, §9) |
| ovl25 `3e77` | `AirRace_MapScenery` 0000, `AirRace_ComputePerformance` 0036, `AirRace_DrawScenery` 03c7, `AirRace_StepSpeed` 0428, `AirRace_AdvancePoint` 04cc, `AirRace_Run` 05e3, `Race_Aircraft` 0a23 | aircraft race (§8.2, §10) |
| ovl26 `3f50` | `Math_DivRound` 0000 | rounding division used by both performance computations (§8.1) |
| ovl10 `2fcb` | `Result_DrawScreen` 0000, `Result_RankUpMessage` 01dd, `State_14_RedrawResult` 0315, `State_12_RaceResult` 0325, `State_13_PostRaceMenu` 0573, `State_15_FinalMovie` 0640 | results, rank message, post-race menu, final movie (§11..§14) |
| `12e0` (part) | `Music_WorkshopCue` 00bd, `Music_MovieCue` 00ea (music cue callbacks) | workshop and final-movie music variations (§5.8, §14) |

**Randomness.** The only `rand()` calls reached from this topic are [O, callee lists in
`functions.tsv`; rand 1000:06c1 is called by none of 2dee, 3dd6, 3e77, 3f50, 2fcb, 1a04 or 153f
except through `Rand_Shuffle`]:

1. `Parts_ShuffleDeck` 153f:01b6 → `Rand_Shuffle` 2bd4:0000: 20·n calls at a new building
   visit (§3.4).
2. Recycling a part, in the workshop (code 1002) or from the HUD (6000): `Item_PlaceInRandomRoom` (1622:0f4e) re-places the
   part in a random room, one `rand()` per attempt (rooms spec) (§4.4, §5.6).

The races, the result and post-race screens, the blueprint and the movie call no `rand()`; the
animation sets they use contain neither of the two random opcodes of the sequence player (-34, -35
at 174b:00ec / 0111) [O `seqscan.py` over the sets 0x0c, 0x0d, 0x0f, 0x11, 0x1b, 0x1d,
0x1072..0x1087]. **The race outcome is fully determined by the two part lists**: no randomness,
no player input.

---

## 2. Runtime context used here (summaries of other topics)

* **Frame pacing.** `Timer_Set(0, n)` … draw … `Dirty_Flush` … `Timer_Wait(0)`; n = 4 in the
  workshop and the results, 8 in the races, 18 in the movie (§16). `Delay(n)` busy-waits n ticks
  while polling `Input_GetCode` (global keys act, other codes are lost) (ARCHITECTURE §3.2).
* **Sprite list (1581)** (`code_runtime.md` §11.3). `Sprites_Reset` (1581:0484) clears the
  queue; `Sprites_Add(rec)` (0496) queues a copy of a 14-byte sprite record `{u16 flags/mode, x,
  y, w, h, far picture}`; `Sprites_EraseAll` (04b6) erases every
  queued sprite (`Gfx_RestoreRect` of its rectangle unless flag 0x80) and clears the queue;
  `Sprites_DrawAll` (04fc) draws the queue in order with `Lpx_DrawKeyed(x, y, pic, mode =
  flags)` unless flag 0x10. `Obj_SetSizeFromPicture(rec)` (15df:0414) fills w and h from the
  picture. Later entries are drawn on top. `PicCache_LoadBundle(id, slots)` (15df:023b) loads a
  sprite bundle into the picture cache and returns its member count − 1; `Obj_DrawKeyed` (1473:03df)
  draws one sprite record at once.
* **Screen animations (174b).** `ScreenAnim_Load(id)` loads an `S`/`A` set; `ScreenAnim_Start(seq)`
  (174b:0285) activates it (`+0x24` = 1) in the active list DS:7a46, `ScreenAnim_StartAt(seq, i)`
  (02ab) inserts it at list position i, `ScreenAnim_Frame` (0510) = erase sprites, step every
  active sequence (`ScreenAnim_Step` 001a), draw sprites; `ScreenAnim_PlayModal(seq + 8)` plays one
  modally at 4 ticks per frame; `ScreenAnim_Free` (0444) frees; `ScreenAnim_StopAll` (02f6)
  empties the list. A
  sequence's word `+8` becomes 1 at opcode -1000 (finished, removed) or 2 at opcode -2 (finished,
  holding its last frame); `+0x22` selects the sequence of a set; each frame step lasts
  (first word of the sequence header + 1) animation frames [O 174b:001a, 0324].
* **Sound calls** (`code_sound.md` §13: `SndRes_PlayEffect`
  03c9, `SndRes_StopEffect` 0409, `SndRes_IsPlaying` 056d, `SndRes_PlayMusic` 031a,
  `SndRes_SelectArrangement` 0423, `SndRes_StopAllEffects` 0521, `SndRes_StopMusic` 03ef,
  `SndRes_GetSlot` 055f, `Snd_GetTick` 1bb4:0c0c; handles are indices into the sound table
  DS:9d48):
  `SndRes_LoadMusicBank(id)` / `SndRes_LoadEffectBank(id)` load id + DS:a0d8 (speaker id, or the
  FM arrangement id+1) and return a handle; effect k of a bank is handle + k.
  `1cdd:03c9(h, a, b)` starts effect h (driver 1bb4:10c1(slot, 0, b + 0x3f, a)); `1cdd:0409(h)`
  stops it; `1cdd:056d(h)` = it is playing; `1cdd:031a(music, 0, x)` starts a music bank;
  `1cdd:0423(music, s)` (`SndRes_SelectArrangement`) unmutes all 16 channels, then mutes the
  channels listed in the bank's s-th `M` member ("arrangement" s; s = 0 leaves every channel
  playing; `code_sound.md` §13.3); `1cdd:0521` stops every effect of the
  current effect bank DS:7a44; `1cdd:0237` / `1cdd:027d` free a music / effect bank;
  `1cdd:055f(h)` = the song slot, `1bb4:0c0c(slot)` = its running sound-tick count,
  `Snd_SetCue(slot, tick, callback)` calls a far function when the slot's tick count reaches the
  value [O, functions as cited; meaning from the platform notes].
* **Input.** `Input_GetCode` (198d:0106) returns zone codes (press | 0x4000 first poll, | 0x8000
  held; release code once), BIOS key words, 0x17a2/0x17a3 for the File-menu requests; it consumes
  the HUD codes 5000..5100 and 6000..6003 itself (§4.4) and calls the Esc hook DS:8656 before
  returning Esc (0x1B) [O 198d:022b]. `Input_WaitCode` (198d:02a5) waits for a non-zero code.
  `198d:00d4` pops one key-ring record and, if it is a key with a BIOS key waiting, reads and
  returns that key (the races call it once per frame and ignore the result: keys typed during a
  race are thrown away, one per frame) [O].
* **Zones** (17ab): 22-byte records `{shape, x, y, w, h, priority, far next, press code (+0x10),
  release code (+0x12), tag (+0x14)}`. `17ab:02de(n, table)` pushes n records of a table onto the
  local list, `17ab:0294(zone)` one record, `17ab:0314(zone)` removes one, `17ab:0441(n, table)`
  removes n, `17ab:07f4(tag)` removes every zone with that tag, `17ab:0465` empties the local list
  [O]. The hit test takes the highest priority.

---

## 3. The parts model

### 3.1 The part catalogue resource (`D` 0x2F5 + race index) [O]

One per race index r = DS:4590 = building·5 + level (0..14), loaded as a private copy by
`Parts_LoadCatalogue` (153f:0002) at building set-up (state 4). Layout (all words little-endian):

| Offset | Size | Field | Read by |
|---|---|---|---|
| +0x00 | u16 | building 0..2 | nothing [O] |
| +0x02 | u16 | n = number of part records | 153f, 1622:0107, 12e0:0547 |
| +0x04 | u16 | number of categories including paint and decal (k + 2) | nothing [O] |
| +0x06 | u16 | k = number of required categories = parts to install for a complete vehicle | `Parts_NeededCount` 153f:0316; 2dee:00bb, 12a2, 1304 |
| +0x08 | u16 | workshop palette resource (a `P` range, colours 128..191) | `Workshop_LayoutShelf` 2dee:028f (`Pal_LoadRes`) |
| +0x0A | u16 | sprite bundle of the parts (HUD icons) | 153f:0002 via `PicCache_LoadBundle`; freed by 153f:00fd |
| +0x0C | u16 | paint width: 1 = 7 paint colours per part picture, 2 = 14 source colours mapped onto 7 | `Workshop_PaintSprite` 2dee:01a0 |
| +0x0E | u16[4] | source colour base of the paintable colours, by part index mod 4 (0x88 wood, 0x81 the others in the shipped data) | 2dee:01a0 |
| +0x16 | u8[15] | paintable flag per group of four parts (group = part index / 4) | 2dee:01a0 |
| +0x25 | far | patched at load: → the word table at +0x2D | nothing else |
| +0x29 | far | patched at load: → the first part record | nothing else |
| +0x2D | u16[k] | a category list (unused: never read; in race 2 it does not even match the parts' categories) | nothing [O, all readers of DS:6638 listed in §3.3] |
| +0x2D+2k | n × 0x8E | part records (§3.2) | |
| after the records | i16[] | **Morty's vehicle**: catalogue indices of his parts, terminated by -1 | `Race_LandEnergy`, `Race_Aircraft` via DS:6634 |

At load [O 153f:0002]: DS:6638 = far pointer to the copy, DS:663c/663e = far pointer to record 0,
DS:6634/6636 = far pointer to Morty's list; the sprite bundle +0x0A is loaded (`PicCache_LoadBundle`, its
member slots into a local array), and for each record i: +0x8C = i, +0x82/+0x84 = the picture of
member (+0x10) of that bundle (HUD icon), +0x86/+0x88 = `Res_LoadCopy(+0x16)` (the name string),
+0x68/+0x6A = 0. Returns n. `Parts_FreeCatalogue` (153f:00fd, from `Building_Free`) releases the
bundle, every name copy and the catalogue.

The first four records of every catalogue are the body-type parts (cars: body, aircraft:
envelope / airframe / cockpit, energy: hull) and carry the default colour byte +0x38 [O, all 15
catalogues].

### 3.2 The part record (0x8E bytes) [O offsets; meanings O where a reader is cited]

| Off | Type | In the file | At run time |
|---|---|---|---|
| +0x00 | far | 0 | next record in the holder's list (`19c0` lists) |
| +0x04 | far | 0 | previous record |
| +0x08 | u16 | **category** (0..59) | `Part_GetType` 153f:015d; selects the performance rule (§8) |
| +0x0A | u16 | **variant** 0..3 (grade): index into the performance tables; for paint (category 11) the paint colour | §8, §5.6 |
| +0x0C | i16 | -1 | **holder**: -1 in a room or nowhere, 0 the player, 2..11 a Cyber Chimp (actor slot) |
| +0x0E | u16 | **required**: 1 = counts for "parts needed" and the completion count; 0 for paint and decals | `Part_IsNeeded` 153f:0189 |
| +0x10 | u16 | member index of the HUD icon in the catalogue's sprite bundle (+0x0A) | 153f:0002 |
| +0x12 | u16 | picture resource shown on the workshop shelf (and as the carried cursor) | `Part_GetShelfPicId` 153f:0173 |
| +0x14 | u16 | picture resource drawn on the assembled vehicle (equal to +0x12 for most parts) | `Workshop_InstallPart` 2dee:046d |
| +0x16 | u16 | name string resource (`S`), loaded at +0x86 | 153f:0002 |
| +0x18 | u16 | instance count c (1, or 4 for wheels, tires, brakes) | 2dee:046d |
| +0x1A | u16 | 0 | set to 0x2238 by `Actor_AddPart` (meaning not traced) |
| +0x1C | i16 | -1 | **pair link** (§3.5): -1 none; partner index \| 0x8000 = this is the lower (first) part of a pair; partner index without the flag = this is the upper (second) part |
| +0x1E | i16[4] | x of each vehicle instance (workshop picture coordinates) | 2dee:046d |
| +0x26 | i16[4] | y of each instance | 2dee:046d |
| +0x2E | i16[4] | drawing layer of each instance (larger = in front) | 2dee:046d |
| +0x36 | u16 | 0 | **flags**: bit 0 upper shelf row; 0x4000 no points on pick-up (recycled once); 0x8000 installed on the vehicle |
| +0x38 | u8 | default race colour of a body part (4 or 5), used when no paint is applied | 2dee:046d |
| +0x3A | 14 B | 0 | HUD strip sprite `{flags, x, y, w, h, far icon}` (x = -1: not on the strip) |
| +0x48 | 22 B | 0 | HUD strip zone (release code 5000 + index at +0x5A, tag 4 at +0x5C) |
| +0x5E | 14 B | 0 | workshop shelf sprite `{flags, x, y, w, h, far picture +0x68}` |
| +0x6C | 22 B | 0 | no use seen |
| +0x82 | far | | HUD icon picture |
| +0x86 | far | | name string |
| +0x8A | u16 | | workshop state: bit 0 carried on the cursor, value 2 = unavailable on the shelf (installed, or recycled) |
| +0x8C | u16 | | own index |

Accessors: `Part_Record(i)` (153f:019f) = DS:663c + i·0x8E; `Part_GetType` (+8),
`Part_GetShelfPicId` (+0x12), `Part_IsNeeded` (+0x0E) [O].

### 3.3 Categories [O catalogue data, §8 code]

Categories are global numbers; a race's catalogue holds 3..9 required categories with 4 records
each (the four records of a category carry its variants 0..3, sometimes with a variant repeated),
plus paint (11, four colours) and decals (12, four designs, cosmetic). Which category
does what in the races is fixed by the code (§8.1 and §8.2); names below are only identifying
words.

| Races | Required categories (from +0x0E = 1 records) |
|---|---|
| 0 | 0 body, 1 wheels, 2 brakes |
| 1, 3 | 0, 1, 2, 3 small tires, 5 differential, 9 engine, 10 fuel tank (+8 windshield in 3) |
| 2, 4 | 0, 1, 2, 4 large tires, 5, 8, 9, 10 |
| 5..9 (aircraft) | 13..15; 16..21; 22..29; 40..47; 30..33, 35..39 |
| 10, 11 | 48..51; 4, 5, 53..57 |
| 12, 13 | 0, 1, 2, 4 (12) or 3 (13), 5, 6, 7 (+8 in 13) |
| 14 | 0, 1, 2, 4, 5, 8, 9, 58, 59 |

The readers of DS:6638 are 153f:0028..0418, 1622:00fa, 12e0:0547 and 2dee:00bb..1304; none reads
the word table at +0x2D or the fields +0x00 and +0x04 [O, all references].

### 3.4 Placement of the parts in the rooms [O 153f:01b6, 0203; caller rooms topic]

At a new building visit the rooms loader (1622:0085) calls `Parts_ShuffleDeck(m)` with m =
the first word of `I` 0x350 + race (the number of object slots of the building):

```
DS:6614 = 0
Rand_Shuffle(m, DS:659c)      # out[i] = i, then 10·m swaps: a = R(m), b = R(m), swap out[a], out[b]
for i < m: if DS:659c[i] >= n (part count): DS:659c[i] = 1000      # 1000 = a banana bunch
```

`Rand_Shuffle` makes **2 rand() calls per swap, a then b, 20·m in all**, in this order and before
anything else of the set-up that follows it. `Parts_DealNext` (153f:0203) returns
DS:659c[DS:6614++] to the room loader, which fills its object slots in room order. DS:7a00 =
m − n (banana slots).

### 3.5 Inventory rules [O 1834:08d7, 1834:0990, 18f5:0002]

Each actor slot has a list of held parts (actor +0x72 = far list, DS:7afc for the player) and a
count (actor +0x70, DS:7afa). Records are linked into these lists; a record is in at most one.

`PartList_CountType(list, cat, &last)` (153f:0216) counts the records of a category in a list and
returns the index (+0x8C) of the last one (-1 if none).

**`Actor_AddPart(a, i)`** (1834:08d7):

```
c = count of category(i) in actor a's list, last = its last match
if c >= 2: return 0                        (nothing happens; the caller checks before)
p = part i
p.flags (+0x36) &= 0x4000                  (keeps only "no points")
p.+0x8A = 0; p.+0x1A = 0x2238; p.holder = a
p.link (+0x1C) = last                       (-1, or the earlier part's index without the flag)
if last != -1: part[last].link = i | 0x8000
actor a count += 1; append p to actor a's list
return last != -1                           ("already had one of this category")
```

**`Actor_RemovePart(a, i, mine)`** (1834:0990): finds part i in actor a's list (with holder == a,
or any holder when `mine` = 0); actor count −1, holder = -1, unlinks it; if it had a partner, the
partner takes its place in the list (when the removed part was the lower one, the upper is moved
to the removed part's list position) and the partner's link becomes -1. Returns (count of the
category before the removal) − 1, i.e. the number of that category still held.

So **the player can hold at most two parts of a category**; the second one sits "above" the first
(upper shelf row in the workshop, lower HUD row in the strip) and **only one of a pair can be
installed** (§5.6).

**Pick-up** (`Player_PickUpItem` 18f5:0002, world topic) [O]. Only a room object that is
enabled (object +8 ≠ 1) is taken. If its part index (object +0x1E) is 1000: `Projectiles_Add(5)`,
remove the object, DS:7a00 −1. Otherwise, with c = the player's count of that category:

* c = 2: the part stays; `Hud_FlashPart(part, 1)` (1a04:0690) flashes the two held parts in the
  strip; hint 0x1047 queued (delay 0).
* else: effect DS:7a44+1 (0, 4); `had = Actor_AddPart(0, i)`; `Hud_AddPart(i)` (1a04:076b); hint
  0x1044 (optional part) or 0x104A (required part) queued; object removed from the room; HUD dirty
  bit 1; if not `had` and required: **parts needed DS:4582 −1**; if DS:4582 = 0: hint 0x1050 queued;
  unless flag 0x4000: **`Score_Add(20)`** (12e0:0ace, without the HUD flag).

A required part that is recycled or given away increments DS:4582 again when no part of its
category is left (§4.4, §5.6).

### 3.6 Saving and restoring [O 153f:0385, 03c9]

`Parts_SaveState(dst)` writes, for every record in index order, two words: holder (+0x0C) and
flags (+0x36), into the building block at +0x14 (`Players_SaveCurrent` 2b5a:01ea; 4·n bytes of the
0x258-byte area). `Parts_RestoreState(src)` (state 4, resume) does, for every index i: if the saved
holder ≠ -1, `Actor_AddPart(holder, i)` and, for holder 0, `Hud_AddPart(i)`; then flags = the
saved flags. Because the gives run in index order, **pairs are rebuilt with the lower-indexed
part as the lower one**, whatever the pick-up order was [O; consequence I]. The installed vehicle
is rebuilt from flag 0x8000 when the workshop is entered (§5.3). Links, positions and +0x8A are
not saved.

### 3.7 Other helpers [O]

| Function | Behaviour |
|---|---|
| `PartList_FindType(list, cat)` 153f:0276 | index of the first part of the category in a list (no NULL check: the caller knows one exists) |
| `PartList_CountHeldBy(a)` 153f:02bb | parts in actor a's list whose holder is a (chimps, world topic) |
| `PartList_NextColumn(p)` / `PartList_PrevColumn(p)` 153f:0323 / 0354 | next / previous record in the list whose link is < 0 (a lower or unpaired part; uppers are skipped) |
| `Part_GetPartner(p)` 2dee:0c25 | NULL if link = -1, else `Part_Record(link & 0x7FFF)` |

---

## 4. The HUD (segment 1a04)

The HUD occupies rows 165..199 (`Hud_DrawBar` 1a04:00eb: fill colour 0x16, top 2 rows colour 0x17).
It is drawn in every room, in the workshop and in the races and results (counters only).

### 4.1 Layout and resources [O 1a04:025b, 018e, 02e9, 0058, 0588]

`Hud_Init` (1a04:025b, from `Game_Init`): sprite bundle 0x1058 → picture slots DS:875c..8764 (5
members: three labels, the recycle icon, the arrow); digit bundle 0x105A (copy at DS:878e) → 10
far pointers DS:8766 (digits 0..9); flash state DS:874c = -1, DS:8746 = 3.

| Element | Position (x, y) | Source |
|---|---|---|
| labels "parts needed", "bananas", "score" | (218, 166), (218, 177), (218, 188) | DS:875c, 875e, 8760 (`Hud_DrawLabels` 018e, bits 4/2/1 of its argument) |
| parts needed, 2 digits | x 297, y 166 | DS:4582, digit colours 1 |
| projectiles ("bananas"), 3 digits | x 286, y 177 | DS:457e, colours 0 |
| score, 6 digits | x 253, y 188 | u32 DS:4572, colours 2 |
| part name box | (218, 168), width 102, font 1, colour 0 | far text DS:879a (`Hud_DrawInfoText` 0a11, over a cleared label area 011c) |
| inventory strip | x 20..172, rows y 168 (lower) and 184 (upper) | §4.3 (rooms other than 0 only) |
| strip arrows | (2, 173) and mirrored (mode 2) at (178, 173) | DS:8764 |
| recycle icon | (191, 172) | DS:8762 |

`Hud_DrawNumber(x, y, value, digits, scheme)` (1a04:0058): selects the digit colours
(`Hud_SetDigitColors` 000e: remaps the 10 digit pictures in place from colour triple
DS:131a[old] to DS:131a[scheme]; DS:1318 = the current scheme; triples are 3 colours + FF), then
draws `digits` digits left to right 11 px apart using u32 division by DS:1384[digits] (10^(digits−1))
and its remainder. `Hud_DrawCounters` (02e9) draws all three and keeps the shown values in
DS:8792..8798.

### 4.2 `Hud_Update(skip)` and the dirty bits DS:132c [O 1a04:0b69]

`Hud_Update(m)` first clears the bits in m (`DS:132c &= ~m`), then:

1. If the labels are not shown (DS:132a = 0) and DS:5c1f ≠ 0: DS:132a = 1; end any strip
   selection (`Hud_CancelCursorPart` 0a58, bit 1); labels and counters redrawn. (The name box replaces the
   labels; DS:5c1f = 1 is set by the race state, a HUD recycle and `Input_GetHeldKeys` whenever a
   held key is seen (198d:02cd: the player walks), so the labels come back then; it is set to 0 at
   the entry of the workshop and of a warehouse room; only `Hud_Update` reads it.)
2. Bit 0x40, flash: every DS:8746 + 1 calls, toggle the flashed sprites (DS:8742, and DS:873e if
   set) between cleared (fill 0x16) and drawn; DS:874c counts the toggles down, the bit is cleared
   after it passes 0.
3. Bit 0x8000, full redraw: remove the strip zones (tag 4), bar, labels, counters, the strip if
   DS:08c2 (current room) ≠ 0; all bits cleared.
4. Bit 0x4000: bar and the score label; leaves only bit 4.
5. Bit 1: strip (`Hud_DrawStrip` 0588). Bit 8: projectiles (and effect DS:9dc0+5 (0, 5)). Bit
   0x10: name box. Bit 2: parts needed. Bit 4: score (and effect DS:9dc0+1 (0, 5): **every score
   redraw clicks**).
6. If any bit was set: `Gfx_SaveToBack(0, 165, 320, 16)`: screen rows 165..180 (the redrawn bar,
   labels and strip) are saved into the back page, so that a later sprite erase restores the new
   HUD there [O call; why only these 16 of the HUD's 35 rows is not traced].

DS:9dc0 is the global effect bank of `SndRes_Init` (1cdd:001e). Callers pass the bits to *skip*:
the workshop calls `Hud_Update(1)` (no strip), the race state `Hud_Update(0)`, the result count-up
`Hud_Update(0xFFFB)` (score only).

### 4.3 The inventory strip [O 1a04:0348, 0588, 076b]

`Hud_AddPart(i)` (076b): takes the player's list tail (the part just given), clears its HUD sprite,
sets the icon (+0x82), x = -1, the zone code 5000 + i and tag 4, shows its name, and lays the strip
out from that part (`Hud_LayoutStrip(p, 0)`).

`Hud_LayoutStrip(p, mode)` (0348): starting from the lower part of p, it walks backwards (unless
mode 2) and forwards (unless mode 1) through the lower parts, alternating, giving each a slot while
the accumulated icon widths + 4 stay below 153 px, and hiding (x = -1) the others and their
partners. Then it assigns x positions from 20 in list order, step = icon width + 4, lower parts at
y 168 and their upper partners at y 184 with the same x; DS:8758 = first shown, DS:8754 = last
shown.

`Hud_DrawStrip` (0588): clears (20, 167, 157 × 33) with colour 0x16, draws every shown icon keyed,
fixes each icon's zone rectangle (2bd4:00c1) and adds the zone, draws the recycle icon and the two
arrows, and adds the arrow zones DS:1330 (6002 at (2, 173, 12 × 23), 6003 at (178, 173, 12 × 23),
priority 10, tag 4). Installed parts are shown but cannot be selected.

### 4.4 Strip commands (codes consumed inside `Input_GetCode`) [O]

| Code | Function | Behaviour |
|---|---|---|
| 6002 | `Hud_ScrollLeftArrow` 08ad | if no flash runs: lay out from the lower part after the last shown (mode 1), effect DS:9dc0+6 (0, 5), bit 1 |
| 6003 | `Hud_ScrollRightArrow` 084c | the same from the lower part before the first shown (mode 2) |
| 5000 + i | `Hud_SelectPart(i)` 090e | if no flash runs and part i is not installed: remove the strip zones, DS:1326 = i, the cursor becomes the icon (hot spot at its centre), the icon's place is cleared, mouse bounds (0, 165, 319, 34), add the carry zones DS:135c (6001 the whole HUD, priority 15; in rooms other than 0 also 6000 the recycle icon (191, 172, 22 × 24), priority 20), effect DS:9dc0+4 (0, 5); in all cases show the part's name |
| 6001 | `Hud_PutBackCursorPart` 0b5b | end the selection (normal cursor, full mouse range, remove the carry zones, DS:1326 = -1), full redraw |
| 6000 | `Hud_TrashCursorPart` 0a82 | the neighbour for the new layout (`Hud_StripNeighbour` 01fa: partner, else next lower, else previous lower); required? ; `Actor_RemovePart(0, i, 1)`; normal cursor; flags \|= 0x4000; **`Item_PlaceInRandomRoom(i)` (1622:0f4e) re-places the part in a random other room (rand)**; DS:1326 = -1; carry zones removed; relayout; DS:5c1f = 1; if none of the category is left and the part is required: DS:4582 +1; full redraw; effect DS:9dc0+3 (0, 5); hint 0x1049 queued |

`Hud_FlashPart(i, both)` (0690): finds the held part of part i's category, makes it visible in
the strip, then flashes it (and its partner when `both`): 6 toggles, one every 4 calls (DS:874c
= 5, DS:8746 = 3).

### 4.5 The HUD in the workshop

In room 0 (DS:08c2 = 0) the strip is never drawn and its zones never exist, so codes 5000+n and
6000..6003 do not occur there; the workshop shows the same parts on its own shelf (§5). The name box
shows the name of the part being carried (`Hud_SetInfoText` 0754, bit 0x10) and stays (DS:5c1f =
0) [O 2dee:0b3d, 1c5b].

---

## 5. The workshop (state 6, ovl08)

### 5.1 State [O]

| DS | Type | Meaning |
|---|---|---|
| 457c | i16 | **blueprint seen** this level (first word of the 20-byte live block DS:457c..458f, saved with the building) |
| 4580 | i16 | **tube destination room** (sector shown in the workshop); initial = DS:00b2[level] = 1, 2, 2, 3, 3; a tube taken in a warehouse room stores that room (18f5:0535) |
| 4582 | i16 | parts still needed (HUD) |
| 4584 | i16 | **required parts installed** on the vehicle |
| 4586 | i16 | the 100-point completion bonus has been given |
| 4588 | i16 | **paint colour** applied (-1 none) |
| 458a | i16 | cancels hint 0x1046 |
| 5c10 | u8[15] | **race colour** per race index (player record +0x16AE): paint colour 0..3, or the body's default 4/5 |
| 7a44 | i16 | current effect bank (the workshop loads 0x29) |
| 9dc4 | i16 | workshop music bank (0x27); DS:9dc2 = the word its loader (1cdd:008d) takes from the bank's `L` member and the cue tick: 4902h / 4905h, far beyond the song length, so the cue never fires (`code_sound.md` §14.2) |
| 4556 / 4558 | i16 | music cue count / arrangement (§5.8) |
| 2198 | i16 | blueprint open |
| 21d8 | i16 | priority of the shelf-area zone 1009 (1, or 10 while carrying) |
| 22a4 | i16 | carrying: -1 no; otherwise the carried part's flags without 0x8000 (bit 0 = taken from the upper row) |
| a07c | far | carried part |
| a080 | i16 | shelf needs redrawing (non-zero also during a scroll) |
| a084 | i16 | scroll distance left (px) |
| a086 | i16 | scroll direction: 0 none, -1 parts move left, 1 parts move right |
| a0b4 | far | list of vehicle sprites (created by `Workshop_Init` 2dee:1db5) |
| a0cc | far | pool of 20 vehicle sprite records of 0x36 bytes (allocated per visit) |
| a0b8, a0bc, a0c0, a0c4 | far | conveyor animations (sets 0x0d, 0x0c, 0x11, 0x0f) |
| a0c8 | i16 | number of lower-row parts on the shelf |
| a0ca | i16 | next state: 8 (leave by the tube or to the race) or 16 |
| a0d0 | i16 | race-track door open |
| a088 | i16 | state of the workshop sub-sequence (never written, always 0) |

A **vehicle sprite record** (0x36 bytes, pool DS:a0cc) [O 2dee:046d]: +0x08 in use; +0x0A layer;
+0x0C..+0x19 a sprite record (+0x0C flags 0, +0x0E x, +0x10 y, +0x12 w, +0x14 h, +0x16/+0x18
picture); +0x1A a zone (x, y, w, h of the sprite, priority = layer + 20, release code 4000 + part
index, tag 6); +0x30 part index; +0x32 far part record.

### 5.2 Entry (`State_06_Workshop` 2dee:1a90) [O]

1. `Pal_Save`; `Pal_LoadRes(0x2B)` (colours 32..63: the 4 paint ramps); play mode off (DS:0096 = 0);
   DS:0094 = 1; Esc hook DS:8656 = `Workshop_DropCarried` (2dee:0c4d); a0d0 = 0; empty the vehicle
   list; allocate the sprite pool (0x438 bytes, all free).
2. **Trophies**: bundle 0x1A (5 trophy pictures, one per level); for each building b and each
   i = 0..DS:4596[b]: picture i keyed at x = DS:22e2[i] (240, 247, 257, 269, 279), y = DS:22ec[b]
   (79, 96, 111) − 2i. The bundle is released.
3. The tube sign: picture 0x1F + building at (5, 26), picture 0x21 + DS:4580 (sector) at (104, 25);
   `Screen_SavePlayArea` (the drawn screen becomes the back page).
4. Sound: effect bank 0x29 → DS:7a44, music bank 0x27 → DS:9dc4; DS:4558 = 1, DS:4556 = 0;
   arrangement 1; start the music (`1cdd:031a(music, 0, 1)`); `Snd_SetCue(slot, DS:9dc2, Music_WorkshopCue)`.
5. `Workshop_LayoutShelf(1, 160)` (§5.3); `Hud_Update(1)`; `Workshop_DrawShelf`;
   `Workshop_DrawVehicle`; load the conveyor animations 0x0f, 0x11, 0x0c, 0x0d; DS:5c1f = 0.
6. Effect 10 (0, 5); `Delay(15)`; `Workshop_UpdateRaceDoor(0)` (§5.6, silent); `Screen_FadeOutIn`.
7. Animation set 0x1B, sequence 1 (the player arriving through the tube, effects 7, 8, 9 of the
   bank; the set also uses opcode -45, which busy-waits until an effect has ended), played
   modally, freed.
8. `Workshop_Run` (§5.4).
9. `Seq_Run(DS:219a, &DS:a088)`: DS:a088 is 0 and entry 0 of DS:219a is NULL, so **nothing
   runs** (dead code; DS:219e holds a far pointer to `Workshop_Run`, and the words after the NULL at
   DS:21a2 are the shelf zone records of §5.3, not table entries) [O].
10. Release the vehicle pictures (`Workshop_ReleaseVehiclePics` 0797), free the 4 animations; if
    the vehicle is not finished (DS:00be = 0): set 0x1B sequence 0 (leaving by the tube) modally.
11. `Pal_FadeOut`; release the shelf pictures (03b2); free the pool; empty the local zones;
    `Pal_Restore`; Esc hook cleared; free the music and effect banks. Return DS:a0ca.

### 5.3 The screen: shelf conveyor, vehicle, zones [O]

**Shelf layout** `Workshop_LayoutShelf(load, x0 = 160)` (028f): load the level palette (catalogue
+0x08); DS:a0c8 = 0; for each part of the player's list, in list order: +0x8A = 0, sprite flags 0;
if `load`, the sprite picture = `PicCache_Load(+0x12)`; w, h from the picture; then

* upper part (link ≥ 0 without the flag): x = the x of its lower partner, y = 77 − h, flags |= 1;
* lower or single part: x = x0 − w/2, y = 113 − h, flags &= ~1, x0 −= 100, DS:a0c8 += 1;
* if `load` and installed (0x8000): `Workshop_InstallPart` (vehicle sprites) and +0x8A |= 2.

So the lower parts stand 100 px apart on a conveyor, the first centred at x 160; only the part at
the centre is fully inside the visible window x 93..226 (`Workshop_DrawShelf` 0804 clips to
DS:01e4/01e6 = 0x5D..0xE2 and draws every part that is neither installed nor carried with
`Obj_DrawKeyed`).

**Current part.** `Workshop_ListShelfParts(arr)` (0bb2) lists the lower parts (flag bit 0 clear)
in list order; `Workshop_Run` keeps the index SI (0 at entry), the current part cur = arr[SI] (at
entry the list head) and its partner. `Workshop_SetShelfZones(p)` (0ab3) removes and re-adds the two
zones DS:21a2 / DS:21b8 with the rectangles of p (code **1004**) and of its partner if any (code
**1005**), priority 5, tag 5.

**Vehicle.** `Workshop_InstallPart(p)` (046d) takes c = +0x18 free pool records and for each
instance j: layer = +0x2E[j], x = +0x1E[j], y = +0x26[j], picture = `PicCache_Load(+0x14)`, part
index and pointer; paint: if DS:4588 ≠ -1, `Workshop_PaintSprite(rec, DS:4588, 0)` and DS:5c10[race]
= DS:4588; else if the part index is < 4, DS:5c10[race] = byte +0x38. It adds the record's zone
(code 4000 + index) and inserts the record into DS:a0b4 **sorted by layer, ascending** (comparator
`Workshop_CompareLayer` 0401 = b.layer − a.layer; insertion before the first record with a larger
layer, so equal layers keep insertion order). `Workshop_DrawVehicle` (070a) draws the list in order
(back to front), loading a missing picture. `Workshop_UninstallPart(i)` (060f) frees every record
of part i (removing its zone, releasing its picture; if a paint is applied and the part's +0x14 =
+0x12, first undoing the paint on the shared picture), decrements DS:4584 if the part is required,
and returns the record.

**Static zones** DS:21ce (7 records, tag 5, pushed at entry) [O bytes]:

| Code | Rectangle (x, y, w, h) | Priority | What |
|---|---|---|---|
| 1009 | 92, 45, 136, 68 | DS:21d8 (1 / 10) | the shelf: drop the carried part back |
| 1000 | 113, 35, 34, 14 | 7 | conveyor left arrow |
| 1001 | 173, 35, 34, 14 | 7 | conveyor right arrow |
| 1002 | 48, 97, 45, 41 | 5 | recycler |
| 1003 | 237, 36, 46, 30 | 5 | blueprint screen |
| 1008 | 296, 107, 24, 56 | 5 | race-track door |
| 328 (0x148) | 13, 34, 30, 130 | 5 | the tube (same code as the Up key) |

Dynamic: 1004 / 1005 (shelf parts), 4000 + i (installed parts, priority layer + 20), 1006 =
DS:2268 (72, 117, 180, 48, priority 50, tag 5): the workbench, present only while a part is
carried.

**Paint** `Workshop_PaintSprite(rec, c, undo)` (01a0) [O]: with h = catalogue +0x0C and q = the
part index: if the paintable flag byte +0x16 + q/4 is 0, nothing. Otherwise build the paint list P
= 0x20 + 7·(c mod 4) + t for t = 0..6, each entry written twice when h = 2, and the source list S =
(word +0x0E + 2·(q mod 4)) + t for t = 0..7h−1, both FF-terminated; remap the record's picture in
place with `Gfx_RecolorPicture(pic, S, P)` (1473:077e; paint) or `Gfx_RecolorPicture(pic, P, S)` (undo). `Gfx_RecolorPicture(pic, from,
to)` builds an identity table, sets table[from[t]] = to[t] until to[t] = FF, and remaps every pixel
of the picture (`Gfx_RemapPicture`). Because pictures are shared through the picture cache, the
remap changes every sprite and the shelf view of the same resource. **Quirk**: with h = 2 the undo
maps each paint colour back to only the second of its two source colours, so painting twice loses
half of the source shades of 14-colour parts (aircraft and races 10, 11) until the picture is
reloaded [O code, I visible effect].

### 5.4 `Workshop_Run` (2dee:0c89): per-frame order [O]

Set-up: cur = head of the player's list, SI = 0, last index = DS:a0c8 − 1, partner = cur's partner;
DS:a080 = 1, DS:22a4 = -1, DS:a086 = 0; DS:21d8 = 1; push the 7 static zones; shelf zones; array
of lower parts; `Hint_Queue(0x1046, 0x490 ticks, &DS:458a)`; if the player holds parts (DS:7afa ≠
0) `Hint_Show(0x1045)`; clear the sprite queue (`Sprites_Reset`).

```
loop:
    if leaving: remove tag-5 zones; Workshop_DropCarried; return -1
    Hint_Update
    Timer_Set(0, 4)
    if a080: restore the shelf background (92,45,136,32) and (92,82,136,31)           0a8c
    Room_DrawAnimLayer(0)                                    # room 0's animated objects
    ScreenAnim_Frame                                         # conveyor animations
    if a086 == 1:  a080 = 1;  a086 = Workshop_ScrollStepRight()      0986
    if a086 == -1: a080 = -1; a086 = Workshop_ScrollStepLeft()       0880
    if a080: Workshop_DrawShelf; a080 = a086; if a086 == 0: Workshop_SetShelfZones(cur)
    if vehicle changed: Gfx_RestoreRect(72, 117, 180, 48); Workshop_DrawVehicle
    Hud_Update(1)
    Dirty_Flush
    if DS:4586: Hint_Show(0x1053)                            # shown once (hint flags)
    Workshop_UpdateRaceDoor(1)
    Timer_Wait(0)
    code = (DS:457c == 0) ? 1003 : Input_GetCode()           # first visit opens the blueprint
    dispatch (§5.5)
```

**Scroll steps** (0880 left / 0986 right): if neither effect 4 (start) nor 5 (motor) plays, start
5 (0, 2); DS:a084 −= 4; move every part of the list by 4 px (left: x −= 4, right: x += 4); when
DS:a084 reaches 0: stop the 4 conveyor animations, stop effect 5, play effect 6 (0, 3). The step
after that moves 0 px, stops them again, plays effect 6 **a second time** and ends the scroll. A
scroll of 100 px therefore takes **26 frames** (104 ticks) and plays the stop effect twice [O].

### 5.5 Workshop input dispatch [O switch CS:13ab, 16 codes, read from the bytes]

| Code | Handler | Effect |
|---|---|---|
| 0 | – | nothing |
| 27 (Esc) | 0e6b | (the Esc hook has already dropped any carried part) `Cmd_SaveAndExit`: save and quit to DOS |
| 0x17a3 / 0x17a2 | 0e22 / 0e36 | leave: DS:a0ca = 16, DS:455a = 2 (new player) / 3 (hub) |
| 328 (tube, or Up) | 0e4a | actor 0 +0x36 = -1, call the player's callback (actor 0 +0x88) with 0, DS:458a = 1, DS:a0ca = 8, leave → state 8 → room DS:4580 |
| 1009 (shelf) | 0e73 | `Workshop_DropCarried` (0c4d): if carrying, normal cursor, +0x8A &= ~3, remove zone 1006, DS:22a4 = -1, redraw shelf, HUD full redraw |
| 1000 / 331 (Left) | 0f51 | if no scroll and not carrying and SI > 0: effect 4 (0, 1); DS:a084 = 100, DS:a086 = -1; SI −1; new current and partner; the gear sets 0x0c / 0x0d get sequence 0, all four conveyor animations are activated |
| 1001 / 333 (Right) | 100e | if no scroll and not carrying and SI < last: effect 4; DS:a084 = 100, DS:a086 = 1; SI +1; the gear sets get sequence 1 |
| 1004 (lower part) | 0ee7 | if not unavailable (+0x8A bit 1): carry it (§5.6), DS:22a4 = 0; *may install* = partner absent or partner not installed |
| 1005 (upper part) | 0e7a | if present and available: carry it, DS:22a4 = 1; *may install* = the lower part not installed |
| 1006 (workbench) | 1188 | install or paint (§5.6) |
| 1002 (recycler) | 10c9 | recycle the carried part (§5.6) |
| 1003 (blueprint) | 12d2 | if not carrying: `Blueprint_Run`; a non-zero result r leaves with DS:a0ca = 16, DS:455a = r |
| 1008 (race door) | 12fa | if not carrying: if DS:4584 = catalogue +0x06: leave with DS:a0ca = 8 and **DS:00be = 1 (vehicle done)**; else `Hint_Show(0x1054)` |
| other ≥ 4000 (signed) | 1330 | installed part i = code − 4000, if not carrying: take it off the vehicle (§5.6) |

Edge cases [O code, I reach]: 1004 and 1005 do not test whether a part is already carried. While a
part is carried the shelf-area zone 1009 has priority 10 and covers the shelf (the part rectangles
lie inside it), so a click there drops the carried part instead; a part picture sticking out of
92..227 × 45..112 would let a second carry start, which would leave the first part flagged as
carried (invisible on the shelf) and add the workbench zone twice. The vehicle part zones
(priority ≥ 20) lose against the workbench zone (50) while carrying, so a click on the vehicle then
installs or paints.

### 5.6 Carrying, installing, painting, recycling; validity and completion [O]

**Carry** `Workshop_CarryPart(p)` (0b3d): name to the HUD; +0x8A |= 1; the cursor becomes the
shelf picture with its hot spot at the centre (17ab:0222); mouse bounds (0, 9, 319, 154); add
the workbench zone 1006. DS:a07c = p, DS:21d8 = 10, DS:a080 = 1, effect 0 (0, 1) from the shelf.

**Install** (1006, a non-paint part, only if *may install*): normal cursor; +0x8A = 2; flags |=
0x8000; `Workshop_InstallPart`; if required, DS:4584 +1; effect 1 (0, 1). Then (also after a
paint): HUD full redraw, remove zone 1006, DS:22a4 = -1, vehicle redraw, DS:21d8 = 1, and **if
DS:4584 = k and DS:4586 = 0: DS:4586 = 1, `Score_Add(100)`** (once per level). If *may install*
is 0 the click does nothing (the part stays on the cursor).

**Paint** (1006 with a category-11 part): c = its variant; for every vehicle sprite: if a paint is
applied, undo it (`Workshop_PaintSprite(rec, DS:4588, 1)`), play effect 0xC (0, 1), paint with c,
DS:5c10[race] = c. Then DS:4588 = c, the paint part goes back to the shelf (+0x8A &= ~3), normal
cursor. The paint part is not installed and not used up; there is no *may install* check.

**Take off** (4000 + i): DS:21d8 = 10; `Workshop_UninstallPart(i)`; flags &= 0x7FFF; DS:22a4 =
flags & 0x7FFF (non-negative: "carrying"); carry it; vehicle redraw; *may install* = 1; effect 3
(0, 1). It can be dropped back on the shelf (1009), reinstalled (1006) or recycled (1002).

**Recycle** (1002, while carrying): effect 2 (0, 1); remove zone 1006; normal cursor; +0x8A = 2;
flags |= 0x4000; `Hud_StripAnchorFor(i)`; required?; `Actor_RemovePart(0, i, 1)`;
`Hud_RefreshStrip(neighbour)` (removes the tag-4 zones, HUD full redraw); **`Item_PlaceInRandomRoom(i)` (1622:0f4e): the part
goes to a random other room (rand, rooms spec)**; release its shelf picture; DS:21d8 = 1; if no part
of the category is left and it is required, DS:4582 +1; DS:22a4 = -1; shelf redraw;
`Hint_Show(0x1049)`; HUD full redraw. The recycled part gives no points when picked up again. (The
shelf layout and the current index are not recomputed until the next visit: the recycled part's
place stays empty [O; consequence I].)

**Validity.** The vehicle is complete when DS:4584 (installed required parts) equals catalogue
+0x06 (k). Each required category can contribute at most once because the player holds at most two
parts of a category and only one of a pair can be installed; paint and decals are optional. There
is no other rule: no category check against the blueprint, no order [O].

**Race-track door** `Workshop_UpdateRaceDoor(sound)` (00a6): if the door is closed (a0d0 = 0) and
the vehicle is complete, picture 0x26, else if open and incomplete, picture 0x25, drawn keyed at
(306, 80); animation set 0x1D with sequence a0d0 (0 = opening, 1 = closing); a0d0 ^= 1; with
`sound`: effect 0xD (0, 3) before and, after closing, effect 0xE (0, 4); the animation is played
modally; then `Gfx_SaveToBack(299, 80, 21, 85)` (the door area becomes part of the back page)
[O; the picture positions are from the disassembly arguments].

### 5.7 Leaving

`Workshop_Run` returns -1 always; the state returns DS:a0ca: 8 (tube: state 8 → 5 → room
DS:4580; race door: state 8 with DS:00be = 1 → save → state 10) or 16 (menu, blueprint menu
codes) [O 2dee:0e22..1317, 12e0:09aa].

### 5.8 Workshop sounds and music [O]

Effect bank 0x29 (handle DS:7a44): 0 take from the shelf, 1 install, 2 recycle, 3 take off the
vehicle, 4 conveyor start, 5 conveyor motor, 6 conveyor stop, 7..9 the tube animation, 10 entry,
0xC paint, 0xD door moving, 0xE door closed; all started as `1cdd:03c9(bank + k, 0, b)` with b
as given above.

Music bank 0x27: arrangement 1 at entry. The cue callback `Music_WorkshopCue` (12e0:00bd) would
count its firings at DS:4556 and at the second set DS:4558 = 2 and, unless the blueprint is open,
select arrangement 2; but its cue tick DS:9dc2 (4902h / 4905h) lies beyond the song, whose loop
resets the counter, so **it never fires** and the workshop always plays arrangement 1
(`code_sound.md` §14.2). The blueprint uses arrangement 3 and restores DS:4558 on closing.

---

## 6. The blueprint viewer [O 2dee:17eb, 1894, 185d, 13eb, 174f]

Opened by code 1003 (automatically on the first frame of a level while DS:457c = 0).

### 6.1 Opening

`Blueprint_Open` (17eb): `Zones_Push`; DS:2198 = 1; `Screen_SaveToSwap(18, 9, 280, 163)` (the
`gizmo004.dat` save-under); **DS:457c = 1**; the opening animation `Blueprint_DrawFrame` (005e):
pictures 0x13..0x18 (59×37 growing to 280×157) drawn opaque at the (x, y) pairs DS:217c, each
followed by `Dirty_Flush`, with no delay; font 0, colour 15; the title string DS:22a0 ("Level-N",
its 7th character set to '1' + level); `Blueprint_Load(DS:22c4[race])`; the Done button DS:2280
(code 1007) at (248, 142); music arrangement 3.

### 6.2 The blueprint bundle

DS:22c4[race]: 0x2C..0x30, 0xFB..0xFF, 0x269..0x26D) [O 2dee:13eb]:

| Member | Type | Content |
|---|---|---|
| 0 | `I` | x, y of the blueprint picture inside the box (drawn at x + 18, y + 9) |
| 1 | `I` | n, number of part pages |
| 2 | `S` | vehicle name |
| 3 .. 3+n−1 | `T` | page text of each part |
| next n | `G` | label picture of each part (also turned into hover cursors, `Mouse_LoadCursors`) |
| next n | `S` | page title of each part |
| next | `I` | z, number of hot spots |
| next z | `M` | 12-byte hot spot: u16 shape, x, y, w, h, page index → a zone (x + 18, y + 9, w, h, priority 5, release 2000 + page) and a hover record that shows the page's label picture as the cursor (17ab:0771) |

The blueprint picture is DS:22a6[race] (0x31..0x33, 0x100..0x104, 0x26E..0x272).

Confirmed for all 15 bundles [O wave 2]: member count = 4 + 3n + z; each `M` hover record's cursor is
the label of the record's page index, and its zone priority is 5. The workshop palette under the
blueprint is room 0's bundle palette (0x0B member 0, colours 192..255), then `Pal_LoadRes(0x2B)`
(32..63), then `Pal_LoadRes`(catalogue +8 = 0x105..0x109, 128..191) from `Workshop_LayoutShelf`
2dee:028f; this matched DS:024e of the three r2 blueprint states and w0/09.

### 6.3 The loop

`Blueprint_Run` (1894) has **no frame pacing**: it redraws only when needed and otherwise
polls `Input_GetCode` in a tight loop.

* Redraw: clear the box (`Blueprint_ClearBox` 0000: fills colour 0xDA at (25, 26, 267, 135),
  (78, 15, 214, 10), (26, 15, 50, 10); re-adds the Done button), the title at (26, 15), hot spots
  and hover cursors off; page 1 (overview): the vehicle name centred in x 78..292, the blueprint
  picture keyed, hot spots and hover cursors on; page 2: the part's title centred, its text in a box
  at (27, 28), width 257. Then `Hint_Show(0x1043)` (first time only).
* Input (switch CS:1a70): 0x43EF / 0x83EF (Done pressed / held) → button down (4034:0304); 0x17D4
  → button up (4034:032f); 13 (Enter) or 1007 (Done released) → on page 1 close, on page 2 back
  to page 1; 0x17A2 / 0x17A3 → close and return 3 / 2; any other code ≥ 2000 → page 2 for page
  index code − 2000.

### 6.4 Closing

`Blueprint_Close` (185d): `Screen_RestoreFromSwap(18, 9)`, remove the Done button, `Blueprint_Free`
(174f), DS:2198 = 0, music arrangement DS:4558, `Zones_Pop`. Returns 0, 2 or 3.

---

## 7. Race preparation and dispatch (states 8, 10, 11) [O]

* **State 8** (12e0:09aa) with DS:00be = 1: DS:00be = 0; DS:459c[b] = 1, DS:0094 = 1;
  `Players_SaveCurrent` (copies the live state into the building block and clears DS:0094); → 10.
* **State 10** (12e0:0506): DS:6616 = the indices of every part with holder 0 and flag 0x8000, in
  index order, then -1 (at most 14 + terminator; DS:6634 follows); DS:455e (rematch) = 0; → 11.
  Paint is never in this list (it is not installed); decals are.
* **State 11** (12e0:0564): `Pal_Save`; DS:0a30 = 1 (17ab:0890; cursor hidden [I]); DS:5c1f = 1;
  `Hud_Update(0)`; building 1 → `Race_Aircraft`, buildings 0 and 2 → `Race_LandEnergy`;
  DS:4560 = the result (0 won, 1 lost). If not a rematch and won: **DS:4596[b] +1 and DS:5c0e +1**.
  DS:0a30 = 0 (17ab:0885). → 12, or **14 after a rematch** (no result count-up, no points, no
  counters).

---

## 8. Vehicle performance

Both races compute a small set of numbers once per race, for the player (list DS:6616) and for
Morty (list at DS:6634), with the same code. Every value is a 16.16 u32 read from a table in
DGROUP indexed by the part's variant; the engine reads the tables from `ssg.exe` by address.

### 8.1 Land and alternative energy: `Race_ComputePerformance(racer, list)` (3dd6:044e) [O]

Near function (`INC BP` frame); read from the disassembly (the decompiled C is wrong). Locals:
max = DS:3b34[race] (u32 base top speed per race index; 0 for the aircraft indices), cap = the same,
acc = 0, acc2 = 0, gear = 0, power (**uninitialised**), A (**uninitialised**), B = the caller's DI
(**not initialised here**: it holds the value `PicCache_LoadBundle` returned for the race's first sprite
bundle in §9.1 step 4, the bundle's member count minus 1).

For each index in the list (until -1), with v = the part's variant, a switch on the category (19
values at CS:0716, targets at CS:073c):

| Category | Rule | Table (u32 per variant) |
|---|---|---|
| 0 body | max −= T[v] | DS:3a64 |
| 1, 53 | max −= T[v] | DS:3a84 |
| 3 (small tires) | cap = T[v] | DS:3a54 |
| 4, 52 | acc += T[v] | DS:3ac4 |
| 5 differential | gear = T[v] | DS:3ad4 |
| 6, 57 | A = v | |
| 7, 56, 58 | B = v | |
| 8 | max −= T[v] | DS:3ae4 |
| 9 engine / motor | max −= T[v]; power = T2[v] | DS:3ab4, T2 DS:3a94 |
| 48 | acc += T[v] | DS:3af4 |
| 50 | max −= T[v] | DS:3b04 |
| 51 | acc += T[v]; max −= T2[v] | DS:3b14, T2 DS:3b24 |
| 55 | acc += T[v]; max −= T2[v] | DS:3a74, T2 DS:3a64 |
| 2, 10, 11, 12, 54, 59, any other | nothing | |

Then a switch on the race index (CS:06f8, 15 entries):

| Race | Rule |
|---|---|
| 0 | acc = 0x4CCD (0.3), **replacing** any sum; acc2 stays 0 |
| 1, 3 | acc += DivRound(power, gear); acc2 = acc |
| 2, 4 | cap = max; acc += DivRound(power, gear); acc2 = acc |
| 5..9 | nothing (never reached) |
| 10 | acc2 = acc |
| 11..14 | power = DS:3aa4[min(A, B)] (signed min); if gear ≠ 0: acc += DivRound(power, gear); acc2 = acc |

Stored into the racer (§9.2): +0x32 acc2, +0x36 acc, +0x26 max, +0x22 = min(cap, max) (signed 32-bit
compare), +0x1C = 0.

`Math_DivRound(a, b)` (3f50:0000) [O]: takes two 32-bit arguments but divides **a by the high word
of b only** (the integer part of a 16.16 value), with both signs removed first; quotient rounded
half up (remainder ≥ ceil(d/2) adds 1); negated if the signs differed. Division by a zero high word
would fault: races 1..4 call it without testing gear (every race-1..4 vehicle has a differential,
category 5, which is required).

**Original bugs to keep** [O code, I effect]:

* **Race 14 uses an uninitialised A.** Its catalogue has no category 6 or 57, so A keeps the stack
  word left at that address: the segment of the private copy of the race's second sprite bundle
  (written by `PicCache_LoadBundle` at the same frame depth when it loaded the bundle). On a real machine that
  is a heap segment, normally below 0x8000 and so larger than B as a signed number: min(A, B) = B,
  the fuel-cell variant. A segment of 0x8000 or more (a heap above 512 KB) would make the index
  negative and read words before DS:3aa4. The engine should use B and document the choice [I].
* B for races 12 and 13 comes from category 7, for 11 from 56, for 14 from 58: all present.
* In race 0 the sum of the acceleration contributions is discarded (no category of race 0 adds
  one anyway).

### 8.2 Aircraft: `AirRace_ComputePerformance(racer, list)` (3e77:0036) [O]

Locals: speed0 = DS:3dbe[level] (u32 base speed per level), acc0 = 0. For each part, a switch on
category − 13 (35 entries at CS:0381, categories 13..47):

| Categories | Rule | Tables |
|---|---|---|
| 13, 14, 24, 25, 26, 27, 28, 29, 30, 31, 33, 36, 37, 38, 39, 40, 41, 42, 43, 46, 47 | speed0 −= T[v] | 3baa, 3bb6, 3c4a (24 and 25 share it), 3c6a, 3c7a, 3c8a, 3c9a, 3d2a, 3d3a, 3d5a, 3d6a, 3d7a, 3d8a, 3d9a, 3caa, 3cba, 3cca, 3cda, 3d0a, 3d1a (in that order) |
| 16..23, 32, 44, 45 | acc0 += T[v] | 3bce, 3bde, 3bee, 3bfa, 3c0a, 3c1a, 3c2a, 3c3a, 3d4a, 3cea, 3cfa |
| 15 | acc0 = T[v] (**replaces** the sum) | 3bc6 |
| 34, 35, other | nothing | |

The tables are packed: some hold 3 entries, most 4 (the next table starts right after); index only
with variants that exist for the category. Then by level (CS:0377):

| Level | speeds[0..2] | accs[0..2] |
|---|---|---|
| 0, 2, 3, 4 | speed0, speed0, speed0 | acc0, acc0, acc0 |
| 1 (glider) | 1.5 (0x18000), 5.0, 1.5 | −acc0, 0.3 (0x4CCD), −acc0 |

Stored: racer +0x1C + 4m = speeds[m], +0x28 + 4m = accs[m] for the three flight modes m.

---

## 9. The land and alternative-energy race (ovl24)

### 9.1 Set-up `Race_LandEnergy` (3dd6:0762) [O]

1. Music bank DS:3994[b] (0xF7 Automotive, 0xF9 Alternative Energy), arrangement 1, started
   (`1cdd:031a(music, 0, 1)`); effect bank 0xF4 (speaker: all five effects are empty holes; FM
   0xF5).
2. Background picture DS:399a[r] with palette DS:399a[r] + 1 (`Screen_LoadPicture`), drawn at
   (0, 0); `Screen_SavePlayArea`; `Pal_LoadRes(0xF6)` (colours 32..63: the race paint ramps).
3. Player path = copy of `R` DS:39f4[r], Morty's path = copy of `R` DS:3a12[r].
4. Paint: lists from = 0xC1..0xC5 and to = 0x20 + 5·DS:5c10[r] + t (t = 0..4), FF-terminated;
   every member of sprite bundle DS:39b8[r] (the player's vehicle frames) is remapped with them
   (`Gfx_RecolorPicture`). Morty's frames (bundle DS:39d6[r]) keep their colours.
5. `Race_MapPathFrames(path, slots)` (3dd6:0000) replaces every path record's frame index (≥ 0)
   with the picture-cache slot of that bundle member, up to the record whose code is -1: the player's
   path with bundle DS:39b8[r], Morty's with DS:39d6[r].
6. Performance: `Race_ComputePerformance(DS:bae8, DS:6616)`, then `(DS:baae, Morty's list)`.
7. Sprite queue cleared; Morty's sprite (DS:bab2) and the player's (DS:baec) set from point 0 of
   their paths and queued; `Sprites_DrawAll`; `Screen_FadeOutIn`.
8. `Race_Run`; then free the bundles and paths, `Pal_FadeOut`, free the music and effect banks;
   return the result.

Per-race DGROUP tables (15 words by race index; aircraft entries 0) [O]: DS:399a background,
DS:39b8 / DS:39d6 sprite bundles, DS:39f4 / DS:3a12 paths, DS:3a30 **extra laps** (0, 1, 2, 1, 2,
–, 1, 1, 1, 1, 1), DS:3b34 base speed (u32); per building: DS:3994 music, DS:3a4e music intro
length (300, 300, 200 sound ticks).

### 9.2 The racer record (0x3A bytes: player DS:bae8, Morty DS:baae) [O]

| Off | Type | Meaning |
|---|---|---|
| +0x00 | u16 | id bit: 1 player, 2 Morty |
| +0x04 | 14 B | sprite record (+0x06 x, +0x08 y, +0x0E far picture) |
| +0x12 | u32 | speed, path points per frame (16.16) |
| +0x16 | i16 | path index |
| +0x18 | far | path |
| +0x1C | i16 | mode: 0 open road, 1 limited section |
| +0x1E | i16 | finished |
| +0x20 | i16 | laps still to run before the finish counts |
| +0x22 | u32 | limit in limited sections (min(cap, max)) |
| +0x26 | u32 | top speed (max) |
| +0x32 | u32 | acceleration in limited sections (acc2) |
| +0x36 | u32 | acceleration (acc) |

### 9.3 Paths (`R` resources) [O format and codes]

An `R` resource is an array of 6-byte records {i16 x, i16 y, i16 code}. Code ≥ 0: a drawable point
(sprite at (x, y), frame = bundle member code); negative codes are commands that occupy a record
but no point. The file ends with a record of code -1. Path points are 1 px apart along the course
(x or y changes by 1 per point).

Land race commands (`Race_AdvancePoint` 3dd6:00e0, table CS:01b0 indexed by code + 14) [O]:

| Code | Meaning |
|---|---|
| -14 | stop effect (handle + x) |
| -13 | start effect (handle + x) with (0, 10) |
| -5 | lap: if laps > 0: laps −1, index = 0 (the next point read is index 1) |
| -4 | mode = 0 |
| -3 | mode = 1 |
| -2 | finish line: if laps = 0: finished = 1, the result bits DS:bb22 \|= id, return 1 |
| -12..-6, -1, ≥ 0, < -14 | a point: stop here |

`Race_AdvancePoint(racer)`: repeat { index +1; read the record; apply a command and continue, or
stop at a point }. It returns 1 if the finish was crossed in this call (it does not stop at the
finish: it continues to the next point).

The two paths of a race have the finish at the same index (both racers run the same number of
points); laps add the whole path again [O data].

### 9.4 Start of the race `Race_Run` (3dd6:01ca) [O]

Racers: ids 1 / 2, finished 0, speed 0, index 0, laps = DS:3a30[r], DS:bb22 = 0.

1. **Music intro wait**: busy-wait (no frames, no input) until the music slot's sound-tick count
   reaches DS:3a4e[b] (300 = 4.12 s, or 200 = 2.75 s).
2. Automotive only, level > 0: start effect 0 with (0, 10) and effect 1 with (1, 10); if effect
   0's handle is valid (not a speaker hole), busy-wait until it stops.

### 9.5 Per-frame order (8 ticks per frame) [O 3dd6:028e..0406]

```
SI = -1; last = -1                                   # music arrangement to apply
loop:
    Timer_Set(0, 8)
    if SI != last: arrangement SI (1cdd:0423); last = SI     # from frame 2 on
    Race_StepSpeed(player); Race_StepSpeed(Morty)
    Sprites_EraseAll                               # erase both sprites
    if not player.finished: repeat int(player.speed) times: if Race_AdvancePoint(player): break
    if not Morty.finished:  repeat int(Morty.speed) times:  if Race_AdvancePoint(Morty): break
    Morty sprite = path point (x, y, frame) at its index; queue
    player sprite likewise; queue                     # drawn after Morty: on top
    Sprites_DrawAll                                # draw
    SI = (player.index >= Morty.index) ? 2 : 3        # who leads chooses the music
    Dirty_Flush
    198d:00d4                                         # one typed key thrown away
    Timer_Wait(0)
until player.finished or Morty.finished
1cdd:0521                                             # stop the race effects
```

`Race_StepSpeed(racer)` (3dd6:0034) [O]: mode 0: speed += acc, clamped to max (signed compare of the
high words, then unsigned low words). Mode 1: if speed > limit: speed −= 0x3333 (0.2) and nothing
else; otherwise speed += acc2, clamped to limit.

**Only the integer part of the speed moves the vehicle**; the fraction is kept in the speed but
not carried over as distance [O]. The player has no control at all during the race.

### 9.6 Result [O 3dd6:0409..0447]

DS:bb22 = 1 (only the player finished in the frame): **won (0)**; 2: lost (1); 3 (both in the
same frame): won if player speed ≥ Morty speed (32-bit signed compare), else lost. The return
value is stored in DS:4560 by state 11.

---

## 10. The aircraft race (ovl25)

### 10.1 Set-up `Race_Aircraft` (3e77:0a23) [O]

1. `Pal_LoadRes(0x245)`, `Pal_LoadRes(0xF6)`; fill (0, 9, 320, 155) with colour 0xE0 (sky); the
   scenery `W` 0x264 + level (§10.3) as a copy; ground pictures 0x247 opaque at (0, 131) and 0x246
   keyed at (0, 111); `Screen_SavePlayArea`.
2. Music bank 0x262 (DS:bbb2), effect bank 0x260 (DS:bbb0).
3. Scenery bundle 0x248 (19 members); `AirRace_MapScenery` (3e77:0000) maps the frame index of both
   scenery layers to picture slots (8-byte records, until code -1).
4. Player path = copy of `R` DS:3b96[level], frames from bundle DS:3b82[level] (painted as in
   §9.1 step 4); Morty's path = copy of `R` DS:3ba0[level], bundle DS:3b8c[level]. Paths mapped with
   `Race_MapPathFrames` (through the ovl24 stub).
5. Level 1 only: bundle 0x24F and the launch paths 0x252 / 0x253 (59 records each), mapped.
6. `AirRace_ComputePerformance(DS:bb64, DS:6616)`, `(DS:bb28, Morty's list)`.
7. Music started (`1cdd:031a(music, 0, 1)`), arrangement 1; `AirRace_Run`; `Pal_FadeOut`; music
   stopped (1cdd:03ef), banks and copies freed. Returns the result.

Per-level tables (5 words) [O]: DS:3b78 propeller frames (3, 1, 3, 3, 1), DS:3b82 / DS:3b8c sprite
bundles, DS:3b96 / DS:3ba0 paths, DS:3daa camera limits (i16 pairs: player, Morty = 1140, 1170 for
every level), DS:3dbe base speed (u32).

### 10.2 The aircraft racer record (0x3C bytes: player DS:bb64, Morty DS:bb28) [O]

| Off | Type | Meaning |
|---|---|---|
| +0x00 | 14 B | sprite record (+0x02 x, +0x04 y, +0x0A far picture) |
| +0x0E | u32 | speed (px per frame along the course) |
| +0x12 | i16 | path index |
| +0x14 | i16 | flight mode 0..2 (**never initialised**: §10.6) |
| +0x16 | i16 | finished |
| +0x18 | far | path |
| +0x1C | u32[3] | target speed per mode |
| +0x28 | u32[3] | acceleration per mode |
| +0x34 | u32 | distance travelled (16.16) |
| +0x38 | u32 | distance at the previous frame |

### 10.3 Scenery `W` 0x264..0x268 [O format, I meaning]

`u16 c1; c1 × 8-byte records (layer 1); u16 c2; c2 × 8-byte records (layer 2)`; each list ends with
its record of frame -1 (counted in c). Record: {i16 x, i16 y, i16 frame (member of bundle 0x248),
u16 slot (filled at load)}. Layer 1 holds the distant 320-px backdrop tiles, layer 2 near objects.
`AirRace_DrawScenery(list, scroll)` (03c7) draws every record keyed at (x − scroll, y).

### 10.4 Paths and commands [O `AirRace_AdvancePoint` 3e77:04cc, table CS:05c7]

The same `R` format as §9.3, with these commands (code + 14 = 0..13):

| Code | Meaning |
|---|---|
| -14 | stop effect (handle + x) |
| -13 | if effect (handle + x) is not playing: start it with (y >> 8, y & 0xFF) |
| -12 | jump: index += x − 1 (x is negative at the ends of the paths: the vehicle circles over its last points) |
| -11, -9 | mode = 0 |
| -10 | mode = 2 (unused by the data) |
| -8 | mode = 1 |
| -7, -6 | nothing, continue |
| -2 | finished = 1, return 1, continue |
| -1 | DS:bb26 = 1, stop (never reached in practice; DS:bb26 is never read) |
| -5..-3, ≥ 0, < -14 | a point: stop |

### 10.5 Per-frame order (8 ticks per frame) [O 3e77:066d..09fe]

```
anim = 0; first = 1; music = 1; launch = 0                   # launch frame counter (level 1)
camera (u32 DS:3b74) = 0; camera10 (DS:3b70) = 0
loop:
    Timer_Set(0, 8)
    arrangement music (1cdd:0423), every frame
    Sprites_EraseAll; Gfx_RestoreRect(0, 80, 320, 85)      # erase sprites and the low sky band
    AirRace_StepSpeed(player); prev = dist; dist += speed
    AirRace_StepSpeed(Morty);  prev = dist; dist += speed
    if not player.finished: repeat int(dist) − int(prev) times: if AirRace_AdvancePoint(player): break
    if not Morty.finished:  likewise
    AirRace_DrawScenery(layer 1, int(camera10)); AirRace_DrawScenery(layer 2, int(camera))
    player sprite: x = path.x − int(camera), y = path.y, picture = slot(path.frame) + anim; queue
    Morty sprite likewise; queue                              # drawn after the player: on top
    if level 1 and launch < 58:
        if launch == 0: start effect 2 with (1, 3)
        draw the two launch sprites from paths 0x252 / 0x253 at index launch (x − camera); queue
        launch += 1; if launch == 58: stop effect 2
    Sprites_DrawAll
    delta = 0
    if player.index >= Morty.index: music = 2
        if 160 < player.x(screen) < DS:3daa[level].player: delta = player.speed
    if Morty.index > player.index: music = 3
        if 160 < Morty.x(screen) < DS:3daa[level].Morty: delta = Morty.speed
    camera += delta; camera10 = Math_DivRound(camera, 10)    # 16.16 / 10, rounded
    if first: Screen_FadeOutIn; Delay(146); first = 0 else Dirty_Flush
    anim = (anim + 1) mod DS:3b78[level]
    198d:00d4
    Timer_Wait(0)
until player.finished or Morty.finished
return player.finished ? 0 : 1
```

`AirRace_StepSpeed(racer)` (0428): speed += accs[mode]; if accs[mode] ≥ 0, clamp to at most
speeds[mode], else to at least speeds[mode].

Notes [O code; I effect]:

* Here the distance is accumulated with its fraction, unlike the land race.
* The camera follows the leader by the leader's speed while the leader's screen x is past 160; the
  upper limits 1140/1170 compare a screen x and so never bind (they look meant for world x).
* When both finish in the same frame, **the player wins** (no speed comparison).
* The sprite picture is "slot of frame + anim": it assumes the bundle's members got consecutive
  picture-cache slots. The engine should index the bundle by (frame + anim) instead [R].
* Level 1 (the glider): from the start the speed is clamped up to 1.5 (mode 0 target, negative
  acceleration); at the -8 point (mode 1) it climbs by 0.3 per frame to 5.0 (the tow); after the
  -9 point it decays by acc0 (the sum of the drag values) per frame towards 1.5.

### 10.6 Quirk: the flight mode is never reset

`AirRace_Run` does not initialise +0x14 [O 3e77:05e3..0665]. Each racer keeps the mode left by the
previous aircraft race of the session (0 after a race in which it passed the -9 point, which every
course has early; 0 at program start, BSS). Only level 1 has modes that differ, so a racer that
ended a previous aircraft race before its -9 point would start the glider race in mode 1 [I]. Keep
it.

---

## 11. Results (states 12 and 14, ovl10)

### 11.1 `Result_DrawScreen` (2fcb:0000) [O]

* Picture 0x1076 with palette 0x1077, drawn at (0, 0); bundle 0x1078 (5 cover pictures m0..m4).
* Won (DS:4560 = 0): names = player (DS:4562), then Morty (far string DS:22fa → "Morty"); DS:a0d4 =
  1 if DS:4596[b] < 4 (a next race exists in this building); if DS:5c0e = 15: DS:a0d4 = 0, final =
  1, DS:0a30 = 1 (17ab:0890). Lost: names Morty, player; DS:a0d2 = 1.
* Covers over the buttons that are not available: if DS:a0d4 = 0, m0 at (75, 73) and m2 at (79, 58)
  (next race); if DS:a0d2 = 0, m0 at (139, 73) and m3 at (143, 58) (warehouse); if final, m0 at
  (203, 73) and m4 at (207, 58) (hub) and m1 at (5, 82) (rematch).
* Font 0: "1st -" / "2nd -" (DS:2442 / 2448) at (5, 32) / (5, 41) in colour 15, the names at
  (40, 32) in colour 2 and (40, 41) in colour 4. `Screen_SavePlayArea`; bundle released.

`State_14_RedrawResult` (0315): `Result_DrawScreen`, `Screen_FadeOutIn`, → 13 (after a rematch).

### 11.2 `State_12_RaceResult` (2fcb:0325) [O]

1. Effect bank 0x108F → DS:7a44; `Result_DrawScreen`; animations 0x107D (A) and 0x107F + 2·level
   (B).
2. Won: animations 0x1074 (W1) and 0x1079 (W2), music 0x1089; reward per frame = I(0x1071)[DS:5c0e
   − 1] · 1000 / 50 (u32), **50 frames**. Lost: 0x107B (W1) and 0x1072 (W2), music 0x108B, no reward.
3. Music started (`1cdd:031a(music, 0, 0)`); W1 and W2 activated.
4. Loop (4 ticks per frame):

```
Timer_Set(0, 4)
ScreenAnim_Frame                            # animations
if frames_left: frames_left −= 1; score += step; HUD bit 4; Hud_Update(0xFFFB)   # one click each
if first: Screen_FadeOutIn else Dirty_Flush
if W1 finished (+8 != 0) and A, B not started: insert A at list position 0, B at 1
done = A finished
Timer_Wait(0)
until done
```

5. Busy-wait until the music stops; free the four animations; empty the active animation list
   (`ScreenAnim_StopAll`); free the music and effect banks.
6. Won: `Result_RankUpMessage`; lost: `Hint_Show(0x104F)`. → 15 if DS:5c0e = 15, else 13.

The reward table `I` 0x1071 holds 15 words indexed by the number of races won in all (the n-th
win of the game, not the level), in thousands of points [O]. The loop lasts until A (started when
W1 holds its last frame) holds its last frame: about 148 + 72 frames when won, 95 + 72 when lost
(from the sets' step timing, `seqscan.py`), so the 50-frame count-up always completes [I]. If it did
not, the rest of the reward would be lost (the count-up runs only inside the loop) [O].

Effects: W1 0x1074 starts effect 0 of bank 0x108F at about frame 52; A starts effect 1 at about
frame 33 of its run (opcode -36) [O data, I frame numbers].

Sound, seen in all four race captures [O wave 2, `code_sound.md` §15]: the results music at tick P
(0x1089 won, 0x108B lost); global effect 1 at P, then every 2 sound ticks from P + 19 for 49 plays
during the count-up; effect 0 of 0x108F at P + 121 and effect 1 at P + 381; a loss has no count-up.

### 11.3 Rank message `Result_RankUpMessage` (2fcb:01dd) [O]

After **every won race**: r = `Rank_FromPoints(score)` (number of thresholds DS:1f9c ≤ score,
0..7). If r < 7: the text = bundle 0x1070 member k, the rank name DS:1f7c[r], member k + 1, the
next threshold DS:1f9c[r] formatted with thousands commas (`Str_FormatThousands`) unless r = 6,
member k + 2; k = 0, or 3 when r = 6 (the last step before the top rank also requires winning all
15 races). Shown with `Dlg_MessageBox` (descriptor by value {0, 0, far text, far DS:3e45}). At the
top rank no message is shown.

---

## 12. The post-race menu (state 13) [O 2fcb:0573]

Zones DS:22fe (5 records, tag 0) pushed; `Input_WaitCode` loop (no frame pacing; the cursor moves
at interrupt time):

| Code | Rectangle (x, y, w, h) | Condition | Action |
|---|---|---|---|
| 2000 rematch | 5, 82, 29, 27 | always | DS:455e = 1 → **11** (same vehicle list DS:6616) |
| 2001 next race | 76, 56, 34, 67 | DS:a0d4 ≠ 0 | → **4**, leave the building |
| 2002 warehouse | 140, 56, 34, 67 | DS:a0d2 ≠ 0 (lost) | → **5** (back into the current room, the workshop) |
| 2003 hub | 204, 56, 34, 67 | always | → **3**, leave the building |
| 2004 Save & Exit | 281, 62, 33, 70 | always | DS:459c[b] = (lost ? 1 : 0); `Cmd_SaveAndExit` (save, exit to DOS) |

Leaving the building (2001, 2003): if won, DS:459c[b] = 0 and `Players_SaveCurrent`; in both cases
`Building_Free`. Then (all exits): empty the local zones, `Pal_FadeOut`, `Pal_Restore`.

After a loss and "hub", nothing is saved: the file keeps the building block written at state 8
(before the race), so the next visit resumes in the workshop with the vehicle built [O; consequence
I].

**Possible original bug** [I, from the code paths; not tested]: win a race (counters +1), choose
rematch, lose it (state 14 enables the warehouse), go to the warehouse, re-enter the workshop and
race again: state 10 clears the rematch flag, so a win counts again: DS:4596[b] and DS:5c0e are
incremented twice for one level, the next level is skipped and the reward index jumps. The hub only
refuses a building whose counter equals 4 (12e0:0456), so a counter of 5 would send the next visit
to race index b·5 + 5 [O compare; I consequence]. Keep the behaviour; confirm with a capture (§19).

---

## 13. Building completion, trophies, the end [O]

* A building is complete when DS:4596[b] = 4 (5 wins). Then the result screen covers "next race"
  (DS:a0d4 = 0), and the hub answers a click on the building with message 0x106E.
* Trophies: the workshop shows, for every building, one trophy per race won (§5.2 step 2). The hub
  draws one per race won too, each recoloured with the vehicle colour of that race: colours
  0x53..0x57 of the trophy picture become 0x20 + 5·DS:5c10[race] + t (12e0:023d..0299, hub topic),
  so the paint chosen in the workshop shows on the hub's trophies.
* DS:5c0e (races won in all) = 15 after the 15th counted win: the result screen covers every button
  but Save & Exit and state 12 goes to state 15.

---

## 14. The final movie (state 15, 2fcb:0640) [O]

```
music = SndRes_LoadMusicBank(0x30CA) → DS:4552; DS:4554 = 1
Snd_SetCue(slot(music), 0x250, Music_MovieCue)        # 12e0:00ea
1cdd:031a(music, 0, 1); arrangement 1
effects = SndRes_LoadEffectBank(0x108D)
Movie_Play(0x3000, 0x30CA, 18, DS:236c, effects)         # 202 frames, 18 ticks each (fmt_graphics §7)
free the effect bank
Input_WaitCode()                                           # any code: key, click, menu request
if farheapcheck() < 0: exit(2)
free the music bank
DS:459c[b] = 0; Players_SaveCurrent
→ 17 (returns 0 → entry 18 is NULL → Seq_Run ends → exit(0))
```

**Cue callback `Music_MovieCue`** (12e0:00ea) (each time the song reaches 0x250 = 592 sound ticks, 8.13 s): DS:4554
+1; values 2, 3, 4 → arrangement 2, 3, 4; 5 → arrangement 0 (every channel plays); 6 → nothing;
7 → DS:4554 = 0 (the next firing gives 1 → arrangement 1, and the cycle restarts) [O]. The song
loops and its section restart resets the tick counter, so the cue fires once per pass
(`code_sound.md` §5, §14.3): pass by pass 1, 2, 3, 4, all, all, all, 1, …

**Palette**: none is loaded. The screen and palette are those left by the results (0x1076/0x1077
over colours 0..63; colours 32..63 still hold the race palette 0xF6, unused by the movie). The
rank message box in between does not touch the palette. The movie draws straight to VRAM over the
results picture (frame 0 is a full rewrite of rows 0..164) [O 2fcb:0640; fmt_graphics §7.4].

The cue table DS:236c and the frame coding are specified in `fmt_graphics.md` §7.

---

## 15. Sounds triggered (summary) [O]

| Where | Bank | Effects / music |
|---|---|---|
| pick-up (world) | DS:7a44 + 1 | (0, 4) |
| HUD | DS:9dc0 (global) | +1 score redraw, +3 recycle, +4 select, +5 projectile redraw, +6 strip scroll; all (0, 5) |
| workshop | 0x29; music 0x27 | §5.8 |
| land race | 0xF4/0xF5; music 0xF7 / 0xF9 | start effects 0, 1 (Automotive levels 1..4); path codes -13 / -14; arrangements 1 → 2/3 |
| aircraft race | 0x260; music 0x262 | path codes -13 / -14; level 1 launch effect 2 (1, 3); arrangements 1 → 2/3 |
| results | 0x108F; music 0x1089 (won) / 0x108B (lost) | animation effects 0, 1 |
| final movie | 0x108D; music 0x30CA | movie cues (fmt_graphics §7.2), music cue §14 |

---

## 16. Timing summary

| Screen | Pacing | Notes |
|---|---|---|
| workshop | 4 ticks per frame (2dee:0d38) | entry `Delay(15)`; scroll 26 frames; door animation modal |
| blueprint | none: polled input, redraw on change | opening animation = 6 flushes, no delay |
| land race | 8 ticks (18.21 frames/s) | intro: busy-wait until the music has played 300 (Automotive) or 200 (Alternative Energy) sound ticks, then (Automotive levels 1..4) until start effect 0 ends; no frames meanwhile |
| aircraft race | 8 ticks | first frame: fade in, then `Delay(146)` (1.0 s) inside the frame |
| results | 4 ticks | count-up 50 frames (200 ticks); loop until the animations hold; then the music must end |
| post-race menu | none (`Input_WaitCode`) | |
| movie | 18 ticks per frame + the cue pauses (`Delay`) | |

Fades are retrace-paced (platform topic). The busy-waits on sound (race intro, start effect,
results music) depend on the sound driver running. They end with sound off too: Ctrl+S plays a
silence song that steals the channels while the other songs keep running silently, and `-SN`
keeps the interpreters running (`code_sound.md` §5, §12.2, §12.3) [I]. Only a speaker effect
that is a `C FF` hole has an invalid slot, and the start-effect wait skips those (§9.4).

---

## 17. Quirks, bugs and porting notes

**Keep** (they change what the player sees or the race outcome) [O unless marked]:

1. Land race: only the integer part of the speed moves the vehicle; the fraction is not carried
   over (§9.5). Aircraft race: the distance keeps its fraction (§10.5).
2. Ties: land race → the faster racer wins (player on equal speed); aircraft race → the player
   wins (§9.6, §10.5).
3. Race 0 replaces the acceleration with 0.3; races 1..4 compute `Math_DivRound(power, gear)`
   with the divisor's integer part only (§8.1).
4. Race 14 reads an uninitialised local as the motor variant; use the fuel-cell variant (B) and
   say so in the engine (§8.1) [I].
5. The aircraft flight mode is not reset between races (§10.6); the camera limits never bind
   (§10.5).
6. Morty's vehicle is a fixed part list per race; there is no randomness and no player input in
   either race; keys typed during a race are discarded one per frame, the global keys (Ctrl+S, Esc,
   menu keys) do not act there, and mouse latches survive until the next `Input_GetCode` (§2).
7. The race intro waits on the music (no frames drawn) and, in the Automotive building from level
   1, on start effect 0 (§9.4).
8. Workshop: one part per category, at most two held; the second of a pair sits on the upper row;
   only one of a pair can be installed; completion = installed required parts = catalogue +0x06;
   100 points once per level on completion; the race-track door animates open/closed with the
   completion state (§5.6).
9. Workshop conveyor: 100 px in 26 frames, the stop sound twice (§5.4). A recycled part leaves an
   empty place on the shelf until the next visit (§5.6).
10. Painting is a palette-index remap of shared cached pictures; undoing it on 14-colour parts loses
    half of the source shades (§5.3). The paint part is never installed: it does not affect
    performance and is not in the race list; its colour is kept per race in DS:5c10 (§7).
11. Esc in the workshop drops the carried part, then Save & Exit (§5.5). The first frame of a level
    opens the blueprint without input (§5.4); the blueprint loop is not paced (§6.3).
12. After every win the rank message appears unless the top rank is reached (§11.3); the count-up
    clicks once per frame for 50 frames (§11.2).
13. Restoring a building rebuilds pairs in catalogue order, not pick-up order (§3.6).
14. The possible double count after a lost rematch (§12) [I].
15. The workshop's `Seq_Run` over DS:219a runs nothing (§5.2 step 9); the category list in the
    catalogue (+0x2D) is never read (§3.1).

**Porting notes** [R]:

* Read every number of §8..§10 (performance tables, base speeds, laps, intro lengths, camera
  limits, propeller frame counts, resource id tables) from `ssg.exe` by DGROUP address, as the TM
  DOS engine reads SST.EXE; the category → rule mapping of §8 is code (the original's switch
  tables) and belongs in the engine.
* Keep 16.16 values in 32-bit integers; the original's comparisons are signed on the high word and
  unsigned on the low word, i.e. a signed 32-bit compare; `Math_DivRound` divides by a 16-bit
  integer (§8.1).
* Implement both race models as pure functions of (race, player list, Morty's list) returning the
  per-frame positions and the result, so that the test vectors of §18 become unit tests.
* The busy-waits (race intro, start effect, results music, the blueprint loop) must pump the host
  like every other wait of the engine.
* The `gizmo004.dat` save-under of the blueprint can be kept in memory (PORTING_NOTES §11).

---

## 18. Test vectors (derived with `racesim.py` / `airsim.py`) [I, simulation of this spec]

The simulations implement §8..§10 exactly as written and read every table from the user's files.
They give an implementation something to compare with; they are derived numbers, not observations.

| Race | Morty's top speed / acc (px per frame) | Morty finishes at frame | Fastest player vehicle finishes at | Share of required-part combinations that beat Morty |
|---|---|---|---|---|
| 0 | 4.8 / 0.3 | 62 | 53 | 50 % |
| 1 | 5.8 / 0.5 | 235 | 194 | 27 % |
| 2 | 6.76 / 1.025 | 290 | 249 | 14 % |
| 3 | 8.36 (limit 7.0) / 1.0 | 266 | 262 | 0.4 % |
| 4 | 9.77 / 0.62 | 338 | 334 | 3.2 % |
| 10 | 4.3 / 0.06 | 329 | 272 | 22 % |
| 11..14 | 5.2, 6.2, 7.76 (7.0), 8.17 | 251, 215, 317, 295 | 242, 213, 312, 275 | 14, 1.0, 0.2, 2.2 % |
| aircraft 0..4 | 2.7, (glider), 5.59, 6.4, 8.6 | 401, 255, 322, 323, 279 | 318, 238, 297, 308, 273 | 44, 20, 2.3, 1.4, 0.9 % |

Frames are counted from the first race frame (frame 1 = the first `Timer_Set(0, 8)`), excluding the
intro waits. Race 14 assumes min(A, B) = B (§8.1).

---

## 19. Open questions and capture requests

1. **Captures.** Round 2 (`REFERENCE_CAPTURES.md` §9.2-§9.4, §10, §11) captured (a) a level-0
   race in each building from a workshop state (`r2/automotive/11_vehicle_built`,
   `r2_automotive_race1_won.avi`, the Aircraft race won and lost, Alternative Energy won); (b) the
   aircraft race at level 0 only; (c) the results won and lost with the count-up (50 updates at 4
   ticks) and the rank box; (d) the workshop (shelf pages, placing and removing parts, the
   assembled vehicle; no recycle); (e) the blueprint pages; (f) the final movie (no palette load,
   18-tick frames, 10.06-s `Delay` cues). Durations from fade-in to fade-out: Automotive won 6.7 s,
   Aircraft won 19.5 s and lost 22.9 s, Alternative Energy won 17.3 s. The lost aircraft race fits
   the model: Morty finishes at frame 401 (§18), 401 × 8 ticks + the 146-tick intro delay = 23.0 s
   [I]. Rewards observed: +4000 for the first win (`I` 0x1071[0] × 1000) and +300 000 for the 15th
   (§11.2); +100 for the first completion only. Still missing: (g) the rematch double count of
   §12, races of levels 2-5 against the test vectors, a recycle, and a frame log of both sprites'
   positions.
2. Race 14: the actual value of the uninitialised A in DOSBox-X (a state at race start: the
   segment of the copy of bundle DS:39d6[14]); whether it is ever ≥ 0x8000.
3. *Answered (§16):* with sound off or device 0 the songs keep running silently, so the sound
   busy-waits end as with sound on [I from `code_sound.md` §5 and §12].
4. Part field +0x1A (set to 0x2238 when given to an actor): its reader (chimps?).
5. Code 0x17D4 in the blueprint loop (button release): which module produces it.
6. *Answered (`code_runtime.md` §10.3):* the set-ups draw on the screen rows and
   `Screen_SavePlayArea` then copies the screen into the back page (wave 0 called it
   `Screen_RestorePlayArea`), so later sprite erases restore that background.
7. Whether the score clicks of the count-up (50 effect starts in 50 frames) are audible as separate
   clicks (sound driver restart behaviour).
8. The exact frame counts of the result animations (the estimate in §11.2 ignores possible
   sound-wait opcodes).
