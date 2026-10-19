// DosGame: construction, the service-held globals, DGROUP strings and dumps (game.h).
#include "dos/game/game.h"

#include <algorithm>
#include <stdexcept>

#include "dos/game/camera.h"
#include "dos/game/main_loop.h"
#include "dos/game/ui.h"

namespace tmport::dos {

namespace {

void put16(std::vector<uint8_t>& out, uint16_t ds, uint16_t v) {
    out[ds] = static_cast<uint8_t>(v & 0xFF);
    out[ds + 1u] = static_cast<uint8_t>(v >> 8);
}
void put32(std::vector<uint8_t>& out, uint16_t ds, uint32_t v) {
    put16(out, ds, static_cast<uint16_t>(v & 0xFFFF));
    put16(out, static_cast<uint16_t>(ds + 2), static_cast<uint16_t>(v >> 16));
}

// A NUL-terminated string starting at index `start` of a char buffer (to its end if no NUL).
std::string_view bufferString(const char* data, size_t size, size_t start) {
    if (start >= size) return {};
    const char* p = data + start;
    const size_t n = std::find(p, data + size, '\0') - p;
    return std::string_view(p, n);
}

}  // namespace

DosGame::DosGame(const DosGfxFiles& f, DosRuntime& runtime, SoundFacade& snd, DosUsersStore& u, const DosOptions& o)
    : files(f),
      options(o),
      tables(GameTables::load(f.exe())),
      help(HelpFile::parse(f.files().bytes(FileIndex::HelpDat))),
      puzzles(PuzzleFile::parse(f.files().bytes(FileIndex::AllPuzs))),
      dictionary(Dictionary::parse(f.files().bytes(FileIndex::PrgDict))),
      quizTiers(QuizTiers::read(f.exe(), puzzles)),
      rt(runtime),
      sound(snd),
      users(u),
      inputTables(InputTables::load(f.exe())),
      input(inputTables),
      palette(lookPalette(f.exe(), o.look)) {
    // The program as loaded: every global at its file value, BSS zero (Game_InitVars comes later).
    loadFromDgroup(static_cast<GameState&>(*this), initialDgroupImage(f.exe()));
}

DosGame::~DosGame() = default;

bool DosGame::stepDone() const { return rt.stepGate().done(); }
uint32_t DosGame::ticks() const { return rt.ticks(); }

UserRecord& DosGame::rec() {
    if (!hasRec()) throw std::logic_error("DosGame::rec: no player record selected (g_userRec)");
    return users.record(ui.userRec);
}

const UserRecord& DosGame::rec() const {
    if (!hasRec()) throw std::logic_error("DosGame::rec: no player record selected (g_userRec)");
    return users.users().records.at(static_cast<size_t>(ui.userRec));
}

std::string_view DosGame::dsString(uint16_t off) const {
    if (off >= kMessageTextStart && off < kMessageTextEnd) {
        const auto& t = elves.messageText;
        return bufferString(reinterpret_cast<const char*>(t.data()), t.size(), off - kMessageTextStart);
    }
    if (off >= dg::kAnswerText && off < dg::kAnswerText + 3 * 35) {
        const size_t rel = off - dg::kAnswerText;
        const auto& row = ui.answerText[rel / 35];
        return bufferString(row.data(), row.size(), rel % 35);
    }
    if (off >= dg::kQuestionText && off < dg::kQuestionText + ui.questionText.size())
        return bufferString(ui.questionText.data(), ui.questionText.size(), off - dg::kQuestionText);
    if (off >= 0xA6B8 && off < 0xA6B8 + ui.expandedText.size())
        return bufferString(ui.expandedText.data(), ui.expandedText.size(), off - 0xA6B8u);
    if (off >= dg::kNewName && off < dg::kNewName + ui.newName.size())
        return bufferString(ui.newName.data(), ui.newName.size(), off - dg::kNewName);
    if (off >= kUsersRecordBase && off < kUsersRecordBase + kUserRecordSize * kUserRecordCount) {
        const size_t rel = off - kUsersRecordBase;
        if (rel % kUserRecordSize != 0) return {};
        const auto& name = users.users().records[rel / kUserRecordSize].name;
        return bufferString(reinterpret_cast<const char*>(name.data()), name.size(), 0);
    }
    return exe().dsString(off);
}

const PakFrame* DosGame::bankFrame(uint16_t frameCode) const {
    if (!bank) return nullptr;
    const int k = frameCode >> 1;
    return k < bank->count() ? &bank->frame(k) : nullptr;
}

// ---- RuntimeClient (owner: mainloop agent) ----------------------------------------------------

// Timer_ISR 1000:0428 step 4: the sound sequencer, every PIT tick.
void DosGame::soundTick() { sound.pitTick(); }

// The start of each original frame (the runtime has just cleared g_stepDone).
//  * A dissolve still running: the frame continues the original's one long Frame_Present, which
//    has no Frame_Begin, so nothing is latched and g_stepDone keeps the value the ticks of that long
//    frame gave it (the runtime does not clear it for such a frame: FrameTiming::continueFrame). (They
//    are normally not gated: Anim_Tick does nothing while g_transition is set, and the DOSBox-X log
//    r2/fl_l2door_after shows no blocked tick during the 30-PIT-tick dissolve.)
//  * A Button_Flash wait running (ui.h uiFlashPending): the original is blocked inside Menu_Update or
//    SignIn_Update (43cb:0eb6, 72 PIT ticks), so there is no Frame_Begin either and g_stepDone keeps
//    its value across the frames of the wait, as for the dissolve.
//  * main 130f:003D: g_dirty = 0, then 130f:025a Frame_Begin. The title loop (Title_Run) has no
//    Frame_Begin; it clears g_dirty and g_stepDone itself (titleFrame, and the runtime). What the
//    latches start (a scene load, a full redraw) is noted for the frame timing (frameBeginTiming): on
//    the reference machine it begins right after the tick that asked for it (wave 5: the runtime runs
//    Game_Tick before Frame_Begin).
void DosGame::frameBegin() {
    latchedLoad = false;
    latchedRedraw = false;
    redrawCostDue = false;
    musicPlaysAtPrevBegin = musicPlaysAtBegin;
    musicPlaysAtBegin = musicPlays;
    if (dissolveRunning) {
        if (dissolveGate) stepGate().mark();
        return;
    }
    if (uiFlashPending(*this)) {
        if (flashGate) stepGate().mark();
        return;
    }
    core.dirty = 0;
    if (titleRunning) return;
    ::tmport::dos::frameBegin(*this);
    frameBeginTiming(*this);  // the reference timing of a scene load the latches start
}

// 130f:00fe Game_Tick.
void DosGame::gameTick(const TickInfo& info) { ::tmport::dos::gameTick(*this, info); }

// 130f:0007 main, the frame body.
void DosGame::frameBody() { mainFrameBody(*this); }

// ---- DGROUP dump with the service-held globals --------------------------------------------------

const std::vector<DgroupField>& serviceFields() {
    static const std::vector<DgroupField> fields = [] {
        std::vector<DgroupField> f;
        auto add = [&f](std::string_view name, uint16_t ds, uint16_t size) {
            DgroupField d;
            d.name = name;
            d.ds = ds;
            d.size = size;
            d.service = true;
            f.push_back(d);
        };
        add("g_keys", dg::kKeys, 2);
        add("g_jumpFromDiagonal", dg::kJumpFromDiagonal, 2);
        add("g_ctrlReleased", dg::kCtrlReleased, 2);
        add("g_stepDone", dg::kStepDone, 2);
        add("g_tick13", dg::kTick13, 2);
        add("g_secondTicks", dg::kSecondTicks, 1);
        add("g_seconds", dg::kSeconds, 2);
        add("g_ticks", dg::kTicks, 4);
        add("g_randSeed", dg::kRandSeed, 4);
        add("g_cmd", dg::kCmd, 2);
        add("g_cmdQueued", dg::kCmdQueued, 2);
        add("g_cmdConsumed", dg::kCmdConsumed, 2);
        add("g_lastBiosKey", dg::kLastBiosKey, 2);
        add("g_inputContext", dg::kInputContext, 2);
        add("g_mouseX", dg::kMouseX, 2);
        add("g_mouseY", dg::kMouseY, 2);
        add("g_mouseButtons", dg::kMouseButtons, 2);
        add("g_mouseXPrev", dg::kMouseXPrev, 2);
        add("g_mouseYPrev", dg::kMouseYPrev, 2);
        add("g_mouseButtonsPrev", dg::kMouseButtonsPrev, 2);
        add("g_mousePtrVisible", dg::kMousePtrVisible, 2);
        add("g_mouseIdleTicks", dg::kMouseIdleTicks, 2);
        add("g_drawOrder", dg::kDrawOrder, 2 * SpriteRenderer::kRecords);
        add("g_spritesChanged", dg::kSpritesChanged, 2);
        add("g_saveHeapReset", dg::kSaveHeapReset, 2);
        add("g_saveHeapHigh", dg::kSaveHeapHigh, 2);
        add("DS_37DA_clipWindow", 0x37DA, 8);
        add("DS_37AE_dirtyTracking", 0x37AE, 1);  // Gfx_SetDirtyTracking 1000:18af (Video_Init: 0xFF)
        add("g_quizTier5Start", dg::kQuizTier5Start, 4);  // words 5 and 6 of g_quizTierStart (Quiz_FindTierEnd)
        add("g_usersFile", dg::kUsersFile, static_cast<uint16_t>(kUsersFileSize));
        // The key lists (u16 count + keys) and zone tables (count byte + records, room as
        // InputTables::kZoneCapacity) of the input contexts, rewritten at run time by the UI code.
        add("g_keysPlay", dg::kKeysPlay, 2 + 2 * 13);
        add("g_keysMenu", dg::kKeysMenu, 2 + 2 * 9);
        add("g_keysDialog", dg::kKeysDialog, 2 + 2 * 11);
        add("g_keysName", dg::kKeysName, 2 + 2 * 69);
        const uint16_t zoneAddr[kInputContexts] = {dg::kZonesPlay, dg::kZonesMenu, dg::kZonesDialog,
                                                   dg::kZonesQuiz, dg::kSigninZones, dg::kZonesTitle};
        static constexpr std::string_view kZoneNames[kInputContexts] = {"g_zonesPlay", "g_zonesMenu", "g_zonesDialog",
                                                                       "g_zonesQuiz", "g_signinZones", "g_zonesTitle"};
        for (int ctx = 0; ctx < kInputContexts; ++ctx)
            add(kZoneNames[ctx], zoneAddr[ctx],
                static_cast<uint16_t>(1 + dg::zonerec::kSize * InputTables::kZoneCapacity[static_cast<size_t>(ctx)]));
        // The draw records hold far save pointers (heap positions: never comparable); compare x, y,
        // z, frame and owner only.
        DgroupField recs;
        recs.name = "g_drawRecs";
        recs.ds = dg::kDrawRecs;
        recs.size = static_cast<uint16_t>(dg::drawrec::kSize * SpriteRenderer::kRecords);
        recs.service = true;
        recs.farPointer = true;
        recs.segWordOffset = dg::drawrec::kSave;
        recs.stride = static_cast<uint16_t>(dg::drawrec::kSize);
        recs.skipLen = 4;
        f.push_back(recs);
        return f;
    }();
    return fields;
}

std::vector<uint8_t> dumpToDgroup(const DosGame& g) {
    std::vector<uint8_t> out = dumpToDgroup(static_cast<const GameState&>(g), initialDgroupImage(g.exe()));
    DosRuntime& rt = g.rt;
    const KbdIsr& isr = rt.keyboard().isr();
    put16(out, dg::kKeys, isr.keys);
    put16(out, dg::kJumpFromDiagonal, isr.jumpFromDiagonal ? 1 : 0);
    put16(out, dg::kCtrlReleased, isr.ctrlReleased ? 1 : 0);
    put16(out, dg::kStepDone, rt.stepGate().done() ? 1 : 0);
    const TickCounters& c = rt.counters();
    put16(out, dg::kTick13, static_cast<uint16_t>(c.cycle13));
    out[dg::kSecondTicks] = c.secondCountdown;
    put16(out, dg::kSeconds, c.seconds);
    put32(out, dg::kTicks, rt.ticks());
    put32(out, dg::kRandSeed, g.rand.seed());
    put16(out, dg::kCmd, static_cast<uint16_t>(g.input.cmd));
    put16(out, dg::kCmdQueued, static_cast<uint16_t>(g.input.queuedCmd));
    put16(out, dg::kCmdConsumed, g.input.deliverNext ? 1 : 0);
    put16(out, dg::kLastBiosKey, g.input.lastKey);
    put16(out, dg::kInputContext, static_cast<uint16_t>(g.input.context));
    const MousePointer& m = g.mouse;
    put16(out, dg::kMouseX, m.x);
    put16(out, dg::kMouseY, m.y);
    put16(out, dg::kMouseButtons, m.buttons);
    put16(out, dg::kMouseXPrev, m.prevX);
    put16(out, dg::kMouseYPrev, m.prevY);
    put16(out, dg::kMouseButtonsPrev, m.prevButtons);
    put16(out, dg::kMousePtrVisible, m.visible ? 1 : 0);
    put16(out, dg::kMouseIdleTicks, m.idleTicks);
    const SpriteRenderer& sp = g.sprites;
    for (int i = 0; i < SpriteRenderer::kRecords; ++i) {
        const DrawRecord& r = sp.records[static_cast<size_t>(i)];
        const auto base = static_cast<uint16_t>(dg::kDrawRecs + dg::drawrec::kSize * i);
        put16(out, static_cast<uint16_t>(base + dg::drawrec::kX), static_cast<uint16_t>(r.x));
        put16(out, static_cast<uint16_t>(base + dg::drawrec::kY), static_cast<uint16_t>(r.y));
        put16(out, static_cast<uint16_t>(base + dg::drawrec::kZ), static_cast<uint16_t>(r.z));
        put16(out, static_cast<uint16_t>(base + dg::drawrec::kFrame), r.frame);
        put16(out, static_cast<uint16_t>(base + dg::drawrec::kOwner), r.owner);
        put32(out, static_cast<uint16_t>(base + dg::drawrec::kSave), 0);  // a heap position: not modelled
        put16(out, static_cast<uint16_t>(dg::kDrawOrder + 2 * i), static_cast<uint16_t>(sp.drawOrder[static_cast<size_t>(i)]));
    }
    put16(out, dg::kSpritesChanged, sp.spritesChanged ? 1 : 0);
    put16(out, dg::kSaveHeapReset, sp.saveHeapReset ? 1 : 0);
    put16(out, dg::kSaveHeapHigh, sp.saveHeapHigh);
    put16(out, 0x37C8, sp.heapOffset);
    put16(out, 0x37CA, sp.heapSegment);
    put16(out, 0x37CC, sp.heapBase);
    const ClipWindow& clip = g.back.clip();
    put16(out, 0x37DA, static_cast<uint16_t>(clip.xMin));
    put16(out, 0x37DC, static_cast<uint16_t>(clip.xMax));
    put16(out, 0x37DE, static_cast<uint16_t>(clip.yMin));
    put16(out, 0x37E0, static_cast<uint16_t>(clip.yMax));
    out[0x37AE] = g.back.color();
    put16(out, dg::kQuizTier5Start, g.quizTiers.start(5));
    put16(out, static_cast<uint16_t>(dg::kQuizTier5Start + 2), g.quizTiers.start(6));
    const std::vector<uint8_t> usersBytes = g.users.users().bytes();
    std::copy(usersBytes.begin(), usersBytes.end(), out.begin() + dg::kUsersFile);
    // The zone and key lists of the input contexts (rewritten at run time by the UI code).
    for (int ctx = 0; ctx < kInputContexts; ++ctx) {
        const InputZoneTable& z = g.inputTables.zones(ctx);
        const uint16_t za = g.inputTables.zonesAddress(ctx);
        if (za != 0) {
            out[za] = z.count;
            for (size_t r = 0; r < z.records.size(); ++r) {
                const InputZone& zr = z.records[r];
                const size_t p = za + dg::zonerec::kFirst + dg::zonerec::kSize * r;
                if (p + dg::zonerec::kSize > out.size()) break;
                out[p + dg::zonerec::kX1] = zr.x1;
                out[p + dg::zonerec::kY1] = zr.y1;
                out[p + dg::zonerec::kX2] = zr.x2;
                out[p + dg::zonerec::kY2] = zr.y2;
                out[p + dg::zonerec::kHover] = static_cast<uint8_t>(zr.hover);
                out[p + dg::zonerec::kPress] = static_cast<uint8_t>(zr.press);
                out[p + dg::zonerec::kHeld] = static_cast<uint8_t>(zr.held);
                out[p + dg::zonerec::kRelease] = static_cast<uint8_t>(zr.release);
            }
        }
        const InputKeyMap& km = g.inputTables.keyMap(ctx);
        const uint16_t ka = g.inputTables.keysAddress(ctx);
        if (ka != 0 && !km.keys.empty()) {
            put16(out, ka, static_cast<uint16_t>(km.keys.size() - 1));
            for (size_t k = 1; k < km.keys.size(); ++k)
                if (ka + 2 * k + 1 < out.size()) put16(out, static_cast<uint16_t>(ka + 2 * k), km.keys[k]);
        }
    }
    return out;
}

}  // namespace tmport::dos
