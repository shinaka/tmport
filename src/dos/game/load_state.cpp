// Starting the DOS game from a DOSBox-X save state (load_state.h).
#include "dos/game/load_state.h"

#include <algorithm>
#include <array>
#include <format>
#include <string_view>

#include "dos/data/dgroup_offsets.h"
#include "dos/game/game.h"
#include "dos/game/sound.h"
#include "dos/gfx/pak.h"
#include "dos/gfx/pan.h"

namespace tmport::dos {

namespace {

constexpr uint16_t kDirtyLists = 0x154C;  // DS:154C: a span-list head word per pair of rows (100)
constexpr uint16_t kInGameTick = 0x1309;  // g_inGameTick: Timer_ISR's re-entrancy guard around Game_Tick
// Timer_ISR 1000:0428 (code_runtime.md §2.3): the g_ticks increment ends at 042f+, the BIOS clock
// call returns to 044a, Sound_Tick to 046a, where DEC DS:3D34 follows.
constexpr uint16_t kTimerIsrSegment = 0x1000;
constexpr uint16_t kTimerIsrTicksAdded = 0x042F, kTimerIsrAfterBios = 0x044A, kTimerIsrAfterSound = 0x046A,
                   kTimerIsrCountdown = 0x046A;
constexpr std::array<std::string_view, 6> kPanStems = {"OPENING", "MOUNTAIN", "MOUNT2N3", "MOUNT3", "CLOSE", "SHACK"};
constexpr std::array<std::pair<std::string_view, FileIndex>, 4> kPakFiles = {{{"SPRITES", FileIndex::SpritesPak},
                                                                               {"BEGIN", FileIndex::BeginPak},
                                                                               {"CLOSE", FileIndex::ClosePak},
                                                                               {"SHACK", FileIndex::ShackPak}}};

// The PAN whose decoded map is at the start of the state's map buffer (Scene_LoadPan writes it from
// offset 0), and the PAN before it: the one whose bytes fill the rest best (the stale-buffer quirk,
// code_world.md §6.10). nullptr when no PAN matches.
std::pair<const PanFile*, const PanFile*> identifyPans(const DosGfxFiles& files, const DosboxState& s) {
    const auto map = s.bytesAt(s.farPointerAt(dg::kMapPtr), PanFile::kMapBufferSize);
    if (map.empty()) return {nullptr, nullptr};
    const PanFile* cur = nullptr;
    for (std::string_view stem : kPanStems) {
        const PanFile& p = files.pan(stem);
        const size_t n = 2 * p.mapWordCount();
        if (n > map.size() || !std::equal(map.begin(), map.begin() + static_cast<std::ptrdiff_t>(n), p.mapBuffer().begin())) continue;
        if (!cur || p.mapWordCount() > cur->mapWordCount()) cur = &p;
    }
    if (!cur) return {nullptr, nullptr};
    auto curWrote = [cur](size_t i) {
        return i < 2 * cur->mapWordCount() || i >= PanFile::kMapBufferSize - cur->mapPackedSize();
    };
    const PanFile* prev = nullptr;
    size_t bestEqual = 0;
    for (std::string_view stem : kPanStems) {
        const PanFile& p = files.pan(stem);
        if (&p == cur) continue;
        size_t compared = 0, equal = 0;
        for (size_t i = 0; i < 2 * p.mapWordCount() && i < map.size(); ++i) {
            if (curWrote(i)) continue;
            ++compared;
            equal += map[i] == p.mapBuffer()[i];
        }
        if (compared > 0 && equal == compared && equal > bestEqual) {
            prev = &p;
            bestEqual = equal;
        }
    }
    return {cur, prev};
}

// The PAK loaded whole into the sprite bank (DS:37BC), by its first bytes (count, header, offsets).
const PakFile* identifyBank(const DosGfxFiles& files, const DosboxState& s, std::string& stemOut) {
    for (const auto& [stem, index] : kPakFiles) {
        const auto file = files.files().bytes(index);
        const size_t n = std::min<size_t>(file.size(), 512);
        const auto bank = s.bytesAt(s.farPointerAt(dg::kSpriteBank), n);
        if (!bank.empty() && std::equal(bank.begin(), bank.end(), file.begin())) {
            stemOut = stem;
            return &files.pak(stem);
        }
    }
    return nullptr;
}

// A background saved by Sprite_SaveUnder at a physical address of the save heap: the opaque frame
// {0, w, h, x, y, ...} with rows of L = w | 0x80 followed by w bytes (fmt_graphics.md §5.3).
std::optional<SavedRect> savedRectAt(const DosboxState& s, uint32_t linear) {
    const auto head = s.bytesAt(linear, 16);
    if (head.empty()) return std::nullopt;
    SavedRect r;
    r.widthBytes = s.wordAt(linear + 2);
    r.height = s.wordAt(linear + 4);
    r.x = static_cast<int16_t>(s.wordAt(linear + 6));
    r.y = static_cast<int16_t>(s.wordAt(linear + 8));
    if (r.widthBytes <= 0 || r.widthBytes > BackBuffer::kRowBytes || r.height <= 0 || r.height > BackBuffer::kHeight) return std::nullopt;
    uint32_t p = linear + 16;
    for (int y = 0; y < r.height; ++y) {
        if (s.byteAt(p) != (static_cast<uint8_t>(r.widthBytes) | 0x80)) return std::nullopt;
        const auto row = s.bytesAt(p + 1, static_cast<size_t>(r.widthBytes));
        if (row.empty()) return std::nullopt;
        r.bytes.insert(r.bytes.end(), row.begin(), row.end());
        p += 1 + static_cast<uint32_t>(r.widthBytes);
    }
    return r;
}

// The timer interrupt the state was saved in had not counted its game-tick countdown yet: the CPU in
// Timer_ISR 1000:0428 after the g_ticks increment and before DEC DS:3D34 (1000:042f..046a), or below
// it in the BIOS clock (return address 1000:044a on the stack) or in Sound_Tick (1000:046a). The
// reference state r2/fl_outside2 was saved inside Sound_Tick: its next game tick came 4 PIT ticks
// later with DS:3D34 = 4 (frame logs fl_walk2 and the others started from it).
bool insideTimerBeforeCountdown(const DosboxState& s) {
    const auto& cpu = s.cpu();
    if (!cpu) return false;
    if (s.toImageSegment(cpu->cs) == kTimerIsrSegment && cpu->ip > kTimerIsrTicksAdded && cpu->ip <= kTimerIsrCountdown)
        return true;
    for (const auto& [seg, ip] : s.stackReturns())
        if (seg == kTimerIsrSegment && (ip == kTimerIsrAfterBios || ip == kTimerIsrAfterSound)) return true;
    return false;
}

// The dirty spans not presented yet (normally the backgrounds Sprites_EraseAll put back after the
// last present): per pair of rows a list of 6-byte nodes {next, head slot, start byte, end byte}
// (Dirty_AddRect 1000:1181 / Dirty_MergeSpan). Returns the spans added.
int restoreDirtySpans(DirtyRegions& dirty, const DosboxState& s) {
    dirty.clear();
    int spans = 0;
    for (int pair = 0; pair < DirtyRegions::kRowPairs; ++pair) {
        uint16_t node = s.dsWord(static_cast<uint16_t>(kDirtyLists + 2 * pair));
        for (int n = 0; node != 0 && n < DirtyRegions::kUnits; ++n) {
            const int start = s.dsByte(static_cast<uint16_t>(node + 4)), end = s.dsByte(static_cast<uint16_t>(node + 5));
            if (end > start) {
                dirty.addRect(start, 2 * pair, 2, end - start);
                ++spans;
            }
            node = s.dsWord(node);
        }
    }
    return spans;
}

void restoreKeyboardAndCounters(DosGame& g, const DosboxState& s) {
    KbdIsr& isr = g.rt.keyboard().isr();
    isr.keys = s.dsWord(dg::kKeys);
    isr.jumpFromDiagonal = s.dsWord(dg::kJumpFromDiagonal) != 0;
    isr.ctrlReleased = s.dsWord(dg::kCtrlReleased) != 0;
    if (s.dsWord(dg::kStepDone) != 0) g.stepGate().mark();
    else g.stepGate().clear();
    TickCounters& c = g.counters();
    c.cycle13 = static_cast<int16_t>(s.dsWord(dg::kTick13));
    c.secondCountdown = s.dsByte(dg::kSecondTicks);
    c.seconds = s.dsWord(dg::kSeconds);
}

void restoreInput(DosGame& g, const DosboxState& s) {
    InputPoll& in = g.input;
    in.cmd = static_cast<int16_t>(s.dsWord(dg::kCmd));
    in.queuedCmd = static_cast<int16_t>(s.dsWord(dg::kCmdQueued));
    in.deliverNext = s.dsWord(dg::kCmdConsumed) != 0;
    in.lastKey = s.dsWord(dg::kLastBiosKey);
    in.context = static_cast<int16_t>(s.dsWord(dg::kInputContext));

    // The zone tables and key lists the UI code rewrites (Box_SetupInput, SignIn_SetupZones, ...);
    // the command lists are never rewritten, so they come from the EXE for the state's key count.
    for (int ctx = 0; ctx < kInputContexts; ++ctx) {
        if (const uint16_t za = g.inputTables.zonesAddress(ctx); za != 0) {
            InputZoneTable& z = g.inputTables.zones(ctx);
            z.count = s.dsByte(za);
            for (size_t r = 0; r < z.records.size(); ++r) {
                const auto p = static_cast<uint16_t>(za + dg::zonerec::kFirst + dg::zonerec::kSize * r);
                InputZone& zr = z.records[r];
                zr.x1 = s.dsByte(static_cast<uint16_t>(p + dg::zonerec::kX1));
                zr.y1 = s.dsByte(static_cast<uint16_t>(p + dg::zonerec::kY1));
                zr.x2 = s.dsByte(static_cast<uint16_t>(p + dg::zonerec::kX2));
                zr.y2 = s.dsByte(static_cast<uint16_t>(p + dg::zonerec::kY2));
                zr.hover = static_cast<int8_t>(s.dsByte(static_cast<uint16_t>(p + dg::zonerec::kHover)));
                zr.press = static_cast<int8_t>(s.dsByte(static_cast<uint16_t>(p + dg::zonerec::kPress)));
                zr.held = static_cast<int8_t>(s.dsByte(static_cast<uint16_t>(p + dg::zonerec::kHeld)));
                zr.release = static_cast<int8_t>(s.dsByte(static_cast<uint16_t>(p + dg::zonerec::kRelease)));
            }
        }
        const uint16_t ka = g.inputTables.keysAddress(ctx);
        if (ka == 0 || g.inputTables.keyMap(ctx).keys.empty()) continue;
        const uint16_t cmds = g.exe().readFarPtr(static_cast<uint16_t>(dg::kCmdLists + 4 * ctx)).off;
        const size_t n = std::min<size_t>(s.dsWord(ka), 69);  // the longest list (name entry) has 69 keys
        InputKeyMap m;
        m.keys.assign(n + 1, 0);
        m.commands.assign(n + 1, -1);
        for (size_t i = 1; i <= n; ++i) {
            m.keys[i] = s.dsWord(static_cast<uint16_t>(ka + 2 * i));
            if (cmds != 0) m.commands[i] = g.exe().dsS16(static_cast<uint16_t>(cmds + 2 * i));
        }
        g.inputTables.setKeyMap(ctx, std::move(m));
    }

    MousePointer& m = g.mouse;
    m.x = s.dsWord(dg::kMouseX);
    m.y = s.dsWord(dg::kMouseY);
    m.buttons = s.dsWord(dg::kMouseButtons);
    m.prevX = s.dsWord(dg::kMouseXPrev);
    m.prevY = s.dsWord(dg::kMouseYPrev);
    m.prevButtons = s.dsWord(dg::kMouseButtonsPrev);
    m.visible = s.dsWord(dg::kMousePtrVisible) != 0;
    m.idleTicks = s.dsWord(dg::kMouseIdleTicks);
    // The driver: where Mouse_Poll last read it (nothing moves a pointer in the reference runs).
    MouseDriver& drv = g.rt.mouse();
    drv.present = g.core.mouse == -1;
    drv.setLogicalPosition(2 * m.x, m.y);
    drv.setButtons(m.buttons);
}

// The draw records, their order and the save heap; returns how many backgrounds a state saved in
// the middle of a frame got back (Sprites_EraseAll on the restored saves).
int restoreSprites(DosGame& g, const DosboxState& s, std::vector<std::string>& warnings) {
    SpriteRenderer& sp = g.sprites;
    bool drawnMarks = false;
    for (int i = 0; i < SpriteRenderer::kRecords; ++i) {
        const auto base = static_cast<uint16_t>(dg::kDrawRecs + dg::drawrec::kSize * i);
        DrawRecord& r = sp.records[static_cast<size_t>(i)];
        r.x = static_cast<int16_t>(s.dsWord(static_cast<uint16_t>(base + dg::drawrec::kX)));
        r.y = static_cast<int16_t>(s.dsWord(static_cast<uint16_t>(base + dg::drawrec::kY)));
        r.z = static_cast<int16_t>(s.dsWord(static_cast<uint16_t>(base + dg::drawrec::kZ)));
        r.frame = s.dsWord(static_cast<uint16_t>(base + dg::drawrec::kFrame));
        r.owner = s.dsWord(static_cast<uint16_t>(base + dg::drawrec::kOwner));
        r.save.reset();
        if (r.owner & 0x8000) {
            drawnMarks = true;
            // Only a save of the pass in progress counts: it lies in the heap between DS:37CC (the
            // start after a reset) and DS:37C8 (the end so far). A record picked but not saved yet (the
            // state was saved inside its turn) still holds last frame's pointer and drew nothing; a
            // clipped-away sprite has none.
            const uint16_t off = s.dsWord(static_cast<uint16_t>(base + dg::drawrec::kSave));
            const uint16_t seg = s.dsWord(static_cast<uint16_t>(base + dg::drawrec::kSave + 2));
            if (r.frame != 0xFFFF && seg != 0 && seg == s.dsWord(0x37CA) && off >= s.dsWord(0x37CC) && off < s.dsWord(0x37C8)) {
                r.save = savedRectAt(s, s.farPointerAt(static_cast<uint16_t>(base + dg::drawrec::kSave)));
                if (!r.save) warnings.push_back(std::format("draw record {}: the saved background is unreadable", i));
            }
        }
        sp.drawOrder[static_cast<size_t>(i)] = static_cast<int16_t>(s.dsWord(static_cast<uint16_t>(dg::kDrawOrder + 2 * i)));
        sp.drawOrder[static_cast<size_t>(i)] = std::clamp(sp.drawOrder[static_cast<size_t>(i)], 0, SpriteRenderer::kRecords - 1);
    }
    sp.spritesChanged = s.dsWord(dg::kSpritesChanged) != 0;
    sp.saveHeapReset = s.dsWord(dg::kSaveHeapReset) != 0;
    sp.saveHeapHigh = s.dsWord(dg::kSaveHeapHigh);
    sp.heapOffset = s.dsWord(0x37C8);
    sp.heapSegment = s.toImageSegment(s.dsWord(0x37CA));
    sp.heapBase = s.dsWord(0x37CC);
    if (!drawnMarks) return 0;
    // Saved between Sprites_DrawSorted and Sprites_EraseAll: the back buffer holds the sprites. Put
    // their backgrounds back in reverse draw order (g_spriteSlotsDrawn entries), then clear the marks.
    int restored = 0;
    for (int j = std::clamp<int>(g.core.spriteSlotsDrawn, 0, SpriteRenderer::kRecords) - 1; j >= 0; --j) {
        const DrawRecord& r = sp.records[static_cast<size_t>(sp.drawOrder[static_cast<size_t>(j)])];
        if (r.frame != 0xFFFF && r.save && (r.owner & 0x8000)) {
            drawSprite(g.back, r.save->image(), 0, 0, sprite_flags::kCgaNormal);
            ++restored;
        }
    }
    for (DrawRecord& r : sp.records) {
        r.owner &= 0x7FFF;
        r.save.reset();
    }
    sp.saveHeapReset = true;
    sp.spritesChanged = false;
    return restored;
}

}  // namespace

Look stateLook(const DosboxState& s) {
    switch (static_cast<int16_t>(s.dsWord(dg::kVideoType))) {
        case 2:
        case 4: return Look::Cga;
        case 3: return Look::Tandy;
        case 5: return Look::Mcga;
        default: return Look::Ega;
    }
}

SoundDevice stateSoundDevice(const DosboxState& s) {
    switch (s.dsWord(dg::kSoundDevice)) {
        case 0: return SoundDevice::Speaker;
        case 2: return SoundDevice::AdLib;
        default: return SoundDevice::Tandy;  // 1 Tandy, 3 the PS/1 card (a Tandy PSG on another port)
    }
}

LoadedState loadDosboxState(DosGame& g, const DosboxState& s) {
    LoadedState out;
    // ---- the state
    loadFromDgroup(static_cast<GameState&>(g), s.dgroup());
    CoreState& c = g.core;
    if (videoTypeOf(g.options.look) != c.videoType) {
        out.warnings.push_back(std::format("the state was saved with g_videoType {}; drawing the {} look", c.videoType,
                                           lookName(g.options.look)));
        c.videoType = static_cast<int16_t>(videoTypeOf(g.options.look));
    }
    g.titleRunning = false;
    g.dissolveRunning = false;
    g.dissolveGate = false;
    g.flashGate = false;
    g.shutdownDone = false;
    if (c.quit != 0) out.warnings.push_back("g_quit is set in the state: the game ends after the first frame");
    if (g.ui.flashStep != 0) g.ui.flashStep = 0;  // engine-side (no DGROUP counterpart): no wait to resume

    // ---- the globals held by wave-2 objects (the inverse of dumpToDgroup(const DosGame&))
    restoreKeyboardAndCounters(g, s);
    restoreInput(g, s);
    if (auto users = UsersDat::parse(s.dgroup().subspan(dg::kUsersFile, kUsersFileSize))) g.users.users() = *users;
    else out.warnings.push_back("the USERS.DAT image in RAM (DS:BA14) does not parse");
    BiosKeyboard& bios = g.bios();
    bios.flush();
    bios.setShiftFlags(s.biosShiftFlags());
    for (uint16_t k : s.biosKeys()) out.biosKeys += bios.push(k) ? 1 : 0;

    // ---- the files in the buffers
    const auto [pan, prev] = identifyPans(g.files, s);
    g.pan = pan;
    g.prevPan = prev ? prev : pan;
    if (pan) out.pan = pan->name();
    else out.warnings.push_back("no PAN matches the map buffer");
    if (prev) out.prevPan = prev->name();
    g.bank = identifyBank(g.files, s, out.bank);
    if (!g.bank) out.warnings.push_back("no PAK matches the sprite bank");

    // ---- the back buffer, the sprites drawn into it, the clip window and dirty tracking
    const auto bb = s.backBuffer();
    std::copy(bb.begin(), bb.end(), g.back.bytes().begin());
    g.back.setClip(ClipWindow{static_cast<int16_t>(s.dsWord(0x37DA)), static_cast<int16_t>(s.dsWord(0x37DC)),
                              static_cast<int16_t>(s.dsWord(0x37DE)), static_cast<int16_t>(s.dsWord(0x37E0))});
    g.back.setColor(s.dsByte(0x37AE));
    out.dirtySpans = restoreDirtySpans(g.back.dirty(), s);
    out.spritesRestored = restoreSprites(g, s, out.warnings);
    out.midFrame = out.spritesRestored > 0;
    // A state saved inside Sprites_DrawSorted 4306:0497 holds its play-field clip window; the rest of
    // that call, which the engine does not run, sets the full window again (4306:06BF..06D1), so the
    // next Hud_Redraw's sprites are not clipped (ega/castle_top, r2/fl_coin_after, ...).
    if (out.midFrame) g.back.setClip(ClipWindow{0, 0x9F, 0, 0xC7});

    // ---- the screen
    if (const auto& screen = s.screen()) {
        std::copy(screen->begin(), screen->end(), g.screen.bytes().begin());
        out.screenFromVideo = true;
    } else {
        g.screen = composeShownFrame(g);
        out.warnings.push_back("the video memory was not found: the screen is composed from the back buffer");
    }
    ++g.screenVersion;

    // ---- sound: the flags, then the driver's effect and tune blocks, which go on where the state left
    // them (soundRestoreDriver; no AdLib rand() is drawn)
    syncSoundFlags(g);
    soundRestoreDriver(g, s.dgroup());
    if (s.dsWord(dg::kSfxBlock + dg::sndblock::kStatus) != 0 && s.dsWord(dg::kSfxBlock + dg::sndblock::kId) != 0xFFFF)
        out.restartedSfx = s.dsWord(dg::kSfxBlock + dg::sndblock::kId);
    if (s.dsWord(dg::kMusicBlock + dg::sndblock::kStatus) != 0 && s.dsWord(dg::kMusicBlock + dg::sndblock::kId) != 0xFFFF)
        out.restartedTune = s.dsWord(dg::kMusicBlock + dg::sndblock::kId);
    g.rand.setRawSeed(s.dsDword(dg::kRandSeed));

    // ---- the time base: the next game tick on the interrupt DS:3D34 says
    out.ticks = s.dsDword(dg::kTicks);
    out.countdown = static_cast<uint8_t>(std::min<uint8_t>(s.dsByte(dg::kGameTickCount), 4));
    out.insideGameTick = s.dsWord(kInGameTick) != 0;
    out.insideTimerBeforeCountdown = insideTimerBeforeCountdown(s);
    if (const auto& cpu = s.cpu())
        out.cpu = std::format("{:04x}:{:04x}", s.toImageSegment(cpu->cs), cpu->ip);
    // PIT ticks until the next game tick: countdown + 1, or countdown when the interrupt in progress
    // has yet to count it (0: that very interrupt runs Game_Tick; the runtime needs one PIT tick).
    int wait = out.countdown + (out.insideTimerBeforeCountdown ? 0 : 1);
    const int firstTickAfter = wait;  // in the original's g_ticks: ticks + wait
    if (wait == 0) wait = 1;
    out.firstTickTicks = out.ticks + static_cast<uint32_t>(firstTickAfter);
    if (g.rt.pitTicks() == 0 && g.rt.frames() == 0) {
        // A fresh runtime: its g_ticks go on from the state's (DosRuntime::resumeAt).
        g.rt.resumeAt(out.firstTickTicks - static_cast<uint32_t>(wait), static_cast<uint32_t>(wait));
    } else if (g.rt.pitTicks() % kPitTicksPerGameTick == 0) {
        g.rt.stepPitTicks(static_cast<uint64_t>(kPitTicksPerGameTick - wait));
    } else {
        out.phaseExact = false;
        out.warnings.push_back("the runtime is inside a game tick: the PIT phase is not set");
    }
    const uint64_t firstTickPit = (g.rt.pitTicks() / kPitTicksPerGameTick + 1) * kPitTicksPerGameTick;
    const uint32_t firstTickRuntime = static_cast<uint32_t>(firstTickPit) + (g.rt.ticks() - static_cast<uint32_t>(g.rt.pitTicks()));
    out.ticksOffset = out.firstTickTicks - firstTickRuntime;
    return out;
}

BackBuffer composeShownFrame(const DosGame& g) {
    BackBuffer frame = g.back;
    if (!g.bank) return frame;
    SpriteRenderer sprites = g.sprites;
    for (DrawRecord& r : sprites.records) {
        r.owner &= 0x7FFF;
        r.save.reset();
    }
    sprites.drawSorted(frame, *g.bank, 0, std::clamp<int>(g.core.spriteSlotsDrawn, 0, SpriteRenderer::kRecords),
                       g.core.uiMode == 0x80, true);
    return frame;
}

}  // namespace tmport::dos
