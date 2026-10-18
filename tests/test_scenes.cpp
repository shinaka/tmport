// Modal scenes (src/game/scenes): question-screen geometry of all 359 questions against an
// independent Python reference (extracted/scratch/scenes/quiz_layout_ref.py, built on
// tools/assets/ffnt.py + othr_puzzles.py; only the hash is stored), focus navigation and the
// judging flow through scripted input on a headless engine, the rank-screen band timing on a
// manual clock, and the castle-climb physics on synthetic input.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "anim/anim_player.h"
#include "data/exe_tables.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/layout.h"
#include "game/quiz.h"
#include "game/rules.h"
#include "game/scenes/castle_climb.h"
#include "game/scenes/celebration.h"
#include "game/scenes/hint_popup.h"
#include "game/scenes/level_cutscene.h"
#include "game/scenes/quiz_screen.h"
#include "input/input_codes.h"
#include "platform/engine.h"
#include "res/resource_manager.h"
#include "test_data.h"
#include "text/font.h"
#include "text/text_renderer.h"
#include "ui/game_dialog.h"
#include "util/hash.h"

using namespace tmport;
using namespace tmport::game;
using namespace tmport::game::scenes;

namespace {

struct ScriptAbort {};

// A headless engine on a manual clock with the services the scenes need (no audio: speech and
// effects are silent no-ops, Voice follows GameState::options).
struct SceneRig {
    GameFiles& files;
    ManualClock clock{1000};
    std::unique_ptr<Engine> engine;
    std::unique_ptr<text::Font> font;
    std::unique_ptr<text::TextRenderer> text;
    std::unique_ptr<ui::DialogSystem> dialogs;
    GameTables tables;
    GameState state;
    std::unique_ptr<AppContext> ctx;
    std::vector<uint16_t> codes;  // fed one per input fetch while the queue is empty
    size_t next = 0;
    std::function<void()> onPump;
    uint64_t lastPost = 0;

    explicit SceneRig(GameFiles& f) : files(f) {
        EngineConfig cfg;
        cfg.clock = &clock;
        engine = std::make_unique<Engine>(files, gfx::ColorMode::Colour256, cfg);
        font = std::make_unique<text::Font>(text::Font::load(*files.res));
        text = std::make_unique<text::TextRenderer>(*font, &engine->frame(), &engine->presenter());
        dialogs = std::make_unique<ui::DialogSystem>(*engine, *font);
        tables = GameTables::load(*files.exe, *files.res);
        state = GameState::initial(tables);
        ctx = std::make_unique<AppContext>(AppContext{*engine, files, *font, *text, *dialogs});
        ctx->state = &state;
        lastPost = clock.nowMs();
        engine->addPumpHook([this](PumpPhase phase) {
            if (onPump) onPump();
            if (phase != PumpPhase::Fetch || !engine->input().empty()) return;
            // Not inside the Input_Flush that follows a screen draw: a code only arrives once the
            // clock moved on since the last one (every wait loop sleeps between polls).
            if (clock.nowMs() < lastPost + 2) return;
            if (next < codes.size()) {
                engine->postKey(codes[next++]);
                lastPost = clock.nowMs();
            } else if (clock.nowMs() > 600000) {
                throw ScriptAbort{};
            }
        });
    }
};

void put16(std::vector<uint8_t>& b, int v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

QuizSlot identitySlot(int q) {
    QuizSlot s;
    s.q = static_cast<int16_t>(q);
    s.perm = {0, 1, 2};
    s.tried = {0, 0, 0};
    return s;
}

}  // namespace

// ------------------------------------------------------------------------------------ quiz layout

TEST_CASE("scenes: question layout of all 359 questions matches the reference") {
    TM_REQUIRE_GAME_DATA();
    const text::Font font = text::Font::load(*g->res);
    text::TextRenderer measure(font);  // measure only
    const auto icon = g->res->pixmap(quiz::kSpeakerPmap);
    REQUIRE(icon);
    CHECK(icon->width == 44);
    CHECK(icon->height == 64);
    std::vector<uint8_t> blob;
    int overlaps = 0;
    for (int q = 0; q < kQuestionCount; ++q) {
        const auto raw = g->res->loadRaw(kQuestionFirstId + q);
        REQUIRE(raw);
        const auto pq = parseQuestion(*raw);
        REQUIRE(pq);
        const QuizSlot slot = identitySlot(q);
        const QuizLayout L = layoutQuestion(*pq, slot, measure, icon->width, icon->height);
        put16(blob, q);
        put16(blob, L.textX);
        put16(blob, L.textY);
        put16(blob, L.textW);
        put16(blob, L.numLines);
        for (int k = 0; k < L.numLines; ++k) put16(blob, L.lineY[static_cast<size_t>(k)]);
        put16(blob, L.ansX);
        put16(blob, L.ansW);
        put16(blob, L.ansH);
        for (int i = 0; i < 3; ++i) put16(blob, L.ansY[static_cast<size_t>(i)]);
        for (int i = 0; i < 3; ++i) put16(blob, L.ansTextX[static_cast<size_t>(i)]);
        for (int i = 0; i < 3; ++i) put16(blob, L.ansTextY[static_cast<size_t>(i)]);
        // Invariants: even box geometry, everything inside the scroll, answer rows lh+4 apart,
        // the speaker button clear of the answers, the k*lh hot-spot quirk only differs from the
        // highlight rows after a blank line.
        CHECK((L.textW % 2) == 0);
        CHECK((L.textX % 2) == 0);
        CHECK(L.textY >= 20);
        CHECK(L.ansY[2] + L.ansH <= 287);
        CHECK(L.ansY[1] - L.ansY[0] == L.lineHeight + 4);
        CHECK(L.ansX + L.ansW <= L.speaker.x);
        CHECK(L.speaker == gfx::Rect{434, 217, 52, 70});
        for (int k = 0; k < L.numLines; ++k)
            if (L.lineHotspot(k).y != L.lineHighlight(k).y) ++overlaps;
    }
    CHECK(blob.size() == 15336);
    CHECK(fnv1a64(blob) == 0xc64190d5ef517318ull);
    MESSAGE("line hot spots that differ from their highlight row (k*lh quirk): " << overlaps);
    CHECK(overlaps > 0);
}

// ----------------------------------------------------------------------- focus and judging flow

TEST_CASE("scenes: question focus navigation and judging through scripted input") {
    TM_REQUIRE_GAME_DATA();
    using namespace tmport::input;
    struct Case {
        const char* name;
        std::array<uint8_t, 3> tried;
        bool voice;
        std::vector<uint16_t> codes;
        int expectPos;
    };
    // perm = {2, 0, 1}: display position 1 holds the correct answer (index 0).
    const std::vector<Case> cases = {
        {"down down enter", {0, 0, 0}, false, {kDown, kDown, kEnter, kEnter}, 2},
        {"down wraps without voice", {0, 0, 0}, false, {kDown, kDown, kDown, kEnter, kEnter}, 0},
        {"up from first wraps to last", {0, 0, 0}, false, {kUp, kEnter, kEnter}, 2},
        {"tried position skipped", {0, 1, 0}, false, {kDown, kEnter, kEnter}, 2},
        {"voice: down past answers reaches the lines, up comes back", {0, 0, 0}, true,
         {kDown, kDown, kDown, kUp, kEnter, kEnter}, 2},
        {"voice: speaker toggle resets focus", {0, 0, 0}, true, {kDown, kLeft, kRight, kEnter, kEnter}, 0},
        {"mouse: press + release on answer 1", {0, 0, 0}, false,
         {uint16_t(quizhot::kAnswer0 + 1) | kLeftDownFlag, uint16_t(quizhot::kAnswer0 + 1), kEnter}, 1},
        {"mouse: release on an unfocused answer does nothing", {0, 0, 0}, false,
         {uint16_t(quizhot::kAnswer0 + 2), kEnter, kEnter}, 0},
    };
    for (const Case& c : cases) {
        const std::string name = c.name;
        CAPTURE(name);
        SceneRig rig(*g);
        rig.state.player.room = 9;  // level 0
        generateClimbLayout(rig.state.rooms, rig.state.clues, 0, rig.tables, rig.engine->rng());
        buildClueTexts(rig.state.clues, rig.tables.words);
        buildClueVoiceIds(rig.state.clues);
        rig.state.options.voiceOn = c.voice ? 1 : 0;
        QuizSlot& slot = rig.state.quiz.slots[1];
        slot.q = 5;
        slot.perm = {2, 0, 1};
        slot.tried = c.tried;
        rig.codes = c.codes;
        rig.engine->rng().srand(77);
        BorlandRand expectRng;
        expectRng.srand(77);
        const uint16_t coinsBefore = rig.state.inv.coins;
        const QuizResult r = askQuestion(*rig.ctx, rig.state, rig.tables, 1);
        CHECK(r.answered);
        CHECK(r.position == c.expectPos);
        CHECK(r.messageIndex == expectRng.random(4));
        const int wrongBefore = (c.tried[0] != 0) + (c.tried[1] != 0) + (c.tried[2] != 0);
        if (c.expectPos == 1) {
            CHECK(r.correct);
            CHECK(r.result == 1);
            CHECK(r.coins == (wrongBefore == 0 ? 2 : wrongBefore == 1 ? 1 : 0));
            CHECK(rig.state.inv.coins == coinsBefore + r.coins);
            CHECK(rig.state.inv.numClues == 1);
            CHECK(r.clueIndex == 0);
            CHECK(slot.q == kQuizNoQuestion);
            CHECK(rig.state.quiz.status[5] == (wrongBefore == 0 ? kQuizRightFirstTry : kQuizRightAfterMiss));
        } else {
            CHECK_FALSE(r.correct);
            CHECK(r.result == 0);
            CHECK(rig.state.inv.coins == coinsBefore);
            CHECK(slot.q == 5);
            CHECK(slot.tried[static_cast<size_t>(c.expectPos)] == 1);
            CHECK(rig.state.quiz.status[5] == kQuizMissed);
        }
        // Every question / result hot spot is gone again.
        const HotspotTable& hs = rig.engine->hotspots();
        for (uint16_t id = quizhot::kLine0; id <= quizhot::kSpeaker; ++id) CHECK_FALSE(hs.contains(id));
        CHECK_FALSE(hs.contains(quizhot::kAnyClick));
        CHECK_FALSE(rig.engine->frame().dirtyEnabled());
    }
}

TEST_CASE("scenes: Esc leaves a question without recording anything; a wrong answer then a right one") {
    TM_REQUIRE_GAME_DATA();
    SceneRig rig(*g);
    rig.state.player.room = 23;  // level 1
    generateClimbLayout(rig.state.rooms, rig.state.clues, 0, rig.tables, rig.engine->rng());
    buildClueTexts(rig.state.clues, rig.tables.words);
    buildClueVoiceIds(rig.state.clues);
    rig.state.options.voiceOn = 0;
    QuizSlot& slot = rig.state.quiz.slots[4];
    slot.q = 100;
    slot.perm = {1, 2, 0};  // correct answer at display position 2
    const QuizState before = rig.state.quiz;
    const uint32_t seed = rig.engine->rng().seed();
    rig.codes = {input::kDown, input::kEsc};
    QuizResult r = askQuestion(*rig.ctx, rig.state, rig.tables, 4);
    CHECK_FALSE(r.answered);
    CHECK(r.result == 0);
    CHECK(rig.state.quiz == before);
    CHECK(rig.engine->rng().seed() == seed);  // no R(4) on Esc

    // Catch 2: position 0 is wrong.
    rig.codes = {input::kEnter, input::kEnter};
    rig.next = 0;
    r = askQuestion(*rig.ctx, rig.state, rig.tables, 4);
    CHECK(r.answered);
    CHECK_FALSE(r.correct);
    CHECK(slot.tried == std::array<uint8_t, 3>{1, 0, 0});
    CHECK(rig.state.quiz.status[100] == kQuizMissed);
    // Catch 3: the focus starts on position 1 (0 is greyed); Down -> 2 = correct, +1 coin.
    const uint16_t coins = rig.state.inv.coins;
    rig.codes = {input::kDown, input::kEnter, input::kEnter};
    rig.next = 0;
    r = askQuestion(*rig.ctx, rig.state, rig.tables, 4);
    CHECK(r.correct);
    CHECK(r.position == 2);
    CHECK(r.coins == 1);
    CHECK(rig.state.inv.coins == coins + 1);
    CHECK(rig.state.quiz.status[100] == kQuizRightAfterMiss);
    CHECK(slot.q == kQuizNoQuestion);
}

// ----------------------------------------------------------------------------------- hint popup

TEST_CASE("scenes: hint popup draws bubble, butterfly and text and waits for a dismiss code") {
    TM_REQUIRE_GAME_DATA();
    SceneRig rig(*g);
    rig.state.options.voiceOn = 0;
    std::vector<int> modes;
    SceneHooks hooks;
    hooks.setClueBoxMode = [&](int m) { modes.push_back(m); };
    rig.codes = {input::kSpace, 0x4B0 | input::kLeftDownFlag, 0x4B0};  // Space and a press do not dismiss
    showHint(*rig.ctx, 0, hooks);
    CHECK(rig.next == 3);
    CHECK(modes == std::vector<int>{2, 0});
    CHECK_FALSE(rig.engine->hotspots().contains(hint::kHotspot));
    // The bubble's opaque pixels are on screen at (33,60).
    const auto bubble = g->res->pixmap(hint::kBubblePmap);
    REQUIRE(bubble);
    const auto& key = rig.engine->palette().keyTable();
    int checked = 0, same = 0;
    for (int y = 0; y < bubble->height; y += 7)
        for (int x = 0; x < bubble->width; x += 7) {
            const uint8_t p = bubble->at(x, y);
            if (key.test(p)) continue;
            ++checked;
            // Text may cover the bubble; count matches.
            same += rig.engine->frame().at(33 + x, 60 + y) == p;
        }
    CHECK(checked > 100);
    CHECK(same > checked * 8 / 10);
}

// -------------------------------------------------------------------------------- rank screen

TEST_CASE("scenes: rank band geometry") {
    for (int r = 0; r < 8; ++r) {
        const RankBand b0 = rankBand(r, 0), b15 = rankBand(r, 15);
        CHECK(b0.clipBottom == 213 - 16 * r);
        CHECK(b0.clipTop == 212 - 16 * r);
        CHECK(b15.clipTop == 197 - 16 * r);
        CHECK(b0.presentY == 197 - 16 * r);
    }
}

TEST_CASE("scenes: rank screen band fill timing on a manual clock") {
    TM_REQUIRE_GAME_DATA();
    SceneRig rig(*g);
    rig.state.rank.totalTreasures = 25;  // rank 2 (thresholds 0, 5, 25, ...)
    rig.state.player.setName("Tester");
    struct Change {
        uint64_t t;
        int top, bottom;
    };
    std::vector<Change> changes;
    gfx::ClipRect last{-1, -1, -1, -1};
    rig.engine->frame().setPlayClip(0);
    rig.onPump = [&] {
        const gfx::ClipRect c = rig.engine->frame().clip();
        if (c != last && c.bottom <= 213) changes.push_back({rig.clock.nowMs(), c.top, c.bottom});
        last = c;
        // Enter only once the bands are done (an Enter during the fill would be consumed).
        if (rig.next == 0 && !changes.empty() && rig.clock.nowMs() > changes.front().t + 3 * 800 + 100)
            rig.codes = {input::kEnter};
    };
    showRankScreen(*rig.ctx, rig.state, rig.tables);
    CHECK(rig.state.rank.rank == 2);
    REQUIRE(changes.size() == 3 * 16);
    for (size_t i = 0; i < changes.size(); ++i) {
        const int r = static_cast<int>(i / 16), k = static_cast<int>(i % 16);
        CAPTURE(i);
        CHECK(changes[i].bottom == 213 - 16 * r);
        CHECK(changes[i].top == 211 - 16 * r - k);  // after step k's draw the top moved up one row
        if (i > 0) CHECK(changes[i].t - changes[i - 1].t == 50);
    }
    CHECK_FALSE(rig.engine->hotspots().contains(rankscreen::kClickHotspot));
}

// ---------------------------------------------------------------------------- castle physics

namespace {

struct Castle {
    GameTables gt;
    GameState s;
    CastleTables t;
    CastleSizes z;
    BorlandRand rng;
    struct Rec : GameHooks {
        std::vector<int> sfx;
        int hud = 0;
        void playSfx(int n) override { sfx.push_back(n); }
        void hudChanged() override { ++hud; }
    } hooks;
    std::unique_ptr<CastleClimb> c;

    Castle(GameFiles& g, CastleOptions o = {})
        : gt(GameTables::load(*g.exe, *g.res)),
          s(GameState::initial(gt)),
          t(CastleTables::fromExe(*g.exe)),
          z(CastleSizes::fromResources(*g.res)) {
        c = std::make_unique<CastleClimb>(s, gt, t, z, hooks, rng, o);
        const RoomDef& d = s.rooms[kCastleRoom];
        s.player.room = kCastleRoom;
        s.player.x = d.startX;
        s.player.y = d.startY;
    }
};

TickSense held(bool l, bool r, bool u, bool d) {
    TickSense t;
    t.left = l;
    t.right = r;
    t.up = u;
    t.down = d;
    return t;
}

}  // namespace

TEST_CASE("scenes: castle tables from the EXE") {
    TM_REQUIRE_GAME_DATA();
    const CastleTables t = CastleTables::fromExe(*g->exe);
    // Structure facts (code_1168-1198.md §2.3): ladders 87 px apart; every jump returns to the
    // same height and moves one column; up / down cycles cancel; layouts share screens 0..2.
    for (int c = 1; c < castle::kColumns; ++c) CHECK(t.colX[c] - t.colX[c - 1] == 87);
    int jr = 0, jl = 0, jdy = 0;
    for (int i = 1; i <= 4; ++i) jr += t.jumpDxRight[i];
    for (int i = 0; i < 4; ++i) jl += t.jumpDxLeft[i];
    for (int i = 1; i <= 4; ++i) jdy += t.jumpDy[i];
    CHECK(jr == 87);
    CHECK(jl == 87);
    CHECK(jdy == 0);
    int up = 0, down = 0;
    for (int i = 0; i < 4; ++i) {
        up += t.climbUpDy[i];
        down += t.climbDownDy[i];
    }
    CHECK(up == down);
    for (int k = 1; k < 4; ++k)
        for (int s = 0; s < 3; ++s) CHECK(t.layouts[k][s] == t.layouts[0][s]);
    CHECK(t.layouts[0][0][2] == 1);  // the ladder painted on screen 0
    CHECK(t.creatSeq[12] == -1);
    CHECK(t.creatSeq[4] == 3);
    int walkL = 0, walkR = 0;
    for (int f = 1; f <= 8; ++f) {
        walkL += t.dxLeft[f];
        walkR += t.dxRight[f];
    }
    CHECK(walkL == walkR);
}

TEST_CASE("scenes: castle screen-0 walking (dxLeft quirk), ladder grab") {
    TM_REQUIRE_GAME_DATA();
    for (bool quirk : {true, false}) {
        CAPTURE(quirk);
        CastleOptions o;
        o.rightWalkUsesDxLeft = quirk;
        Castle k(*g, o);
        CastleClimb& c = *k.c;
        c.startWalk();
        CHECK(k.s.player.frame == 0);
        CHECK(k.s.player.y == k.t.frameY[0]);
        int x = k.s.player.x;
        for (int tick = 1; tick <= 8; ++tick) {
            c.walkUpdate(0, held(false, true, false, false));
            CHECK(k.s.player.frame == tick);
            x += quirk ? k.t.dxLeft[tick] : k.t.dxRight[tick];
            CHECK(k.s.player.x == x);
            CHECK(k.s.player.y == k.t.frameY[tick]);
            CHECK(k.s.player.facingLeft == 0);
        }
        c.walkUpdate(0, held(false, false, false, false));
        CHECK(k.s.player.frame == 0);
        CHECK(k.s.player.y == k.t.frameY[0]);
    }
    // Stand under the painted ladder (column 2) and press Up; Up away from it does nothing.
    Castle k(*g);
    CastleClimb& c = *k.c;
    c.layout = 0;
    c.startWalk();
    c.walkUpdate(0, held(false, false, true, false));
    CHECK_FALSE(c.onLadder);  // x 80: no ladder
    k.s.player.x = static_cast<int16_t>(k.t.colX[2] - k.s.player.w / 2);
    c.walkUpdate(0, held(false, false, false, false));
    REQUIRE(k.s.player.x < k.t.colX[2]);
    REQUIRE(k.t.colX[2] < k.s.player.x + k.s.player.w);
    c.walkUpdate(0, held(false, false, true, false));
    CHECK(c.onLadder);
    CHECK(c.column == 2);
    // Space and Enter still cost a net and a coin.
    Castle e(*g);
    e.c->startWalk();
    e.c->walkUpdate(input::kSpace, held(false, false, false, false));
    CHECK(e.s.inv.nets == 9);
    CHECK(e.c->netStep == 1);
    for (int i = 0; i < 11; ++i) e.c->walkUpdate(0, held(false, false, false, false));
    CHECK(e.c->netStep == 0);
    e.c->walkUpdate(input::kEnter, held(false, false, false, false));
    CHECK(e.s.inv.coins == 9);
    CHECK(e.c->coinStep == 1);
}

TEST_CASE("scenes: castle climbing, screen change, jumps and falls") {
    TM_REQUIRE_GAME_DATA();
    Castle k(*g);
    CastleClimb& c = *k.c;
    c.layout = 0;
    c.column = 2;
    c.startClimb();
    c.onLadder = true;  // set by WalkLoop's FindLadderAtPlayer
    CHECK(k.s.player.x == k.t.colX[2] - 30);
    CHECK(k.s.player.y == 120);
    // Climb up screen 0 until the screen change.
    int y = 120, ticks = 0;
    while (c.climbTick(0, held(false, false, true, false)) && ticks < 100) {
        const int ph = (ticks) % 4;
        y -= k.t.climbUpDy[ph];
        CHECK(k.s.player.y == y);
        CHECK(k.s.player.frame == k.t.climbFrames[ph]);
        ++ticks;
    }
    REQUIRE(c.onLadder);
    CHECK(c.nextScreen == 1);
    CHECK(c.nextY == k.s.player.y + 267);
    CHECK(c.finishScreenChange());
    CHECK(c.screen == 1);
    // Screen 1 has ladders in columns 1..3: jump right from 2 to 3 (4 ticks, +87, same height).
    const int x0 = k.s.player.x, y0 = k.s.player.y;
    c.climbTick(0, held(false, true, false, false));  // starts the jump
    CHECK(c.jumpStep == 1);
    for (int i = 0; i < 4; ++i) c.climbTick(0, held(false, false, false, false));
    CHECK(c.jumpStep == 0);
    CHECK(c.fallStep == 0);
    CHECK(c.column == 3);
    CHECK(k.s.player.x == x0 + 87);
    CHECK(k.s.player.y == y0);
    // Re-attach on the next tick.
    c.climbTick(0, held(false, false, false, false));
    CHECK(c.climbPhase == 0);
    CHECK(k.s.player.x == k.t.colX[3] - 30);
    // Jump right again: column 4 has no ladder -> fall, down to screen 0, landing.
    c.climbTick(0, held(false, true, false, false));
    for (int i = 0; i < 4; ++i) c.climbTick(0, held(false, false, false, false));
    CHECK(c.fallStep == 1);
    std::vector<int> sfx;
    int guard = 0;
    while (c.onLadder && guard++ < 200) {
        const bool more = c.climbTick(0, held(false, false, false, false));
        if (!more && c.finishScreenChange()) CHECK(c.screen == 0);
    }
    CHECK(c.screen == 0);
    CHECK_FALSE(c.onLadder);
    CHECK(c.landStep == 0);
    CHECK(k.s.player.y == k.t.landY[6]);
    CHECK(std::count(k.hooks.sfx.begin(), k.hooks.sfx.end(), castle::kSfxLand) == 1);
    CHECK(std::count(k.hooks.sfx.begin(), k.hooks.sfx.end(), castle::kSfxFall) >= 1);
}

TEST_CASE("scenes: castle creature spawn, cycle and push") {
    TM_REQUIRE_GAME_DATA();
    Castle k(*g);
    CastleClimb& c = *k.c;
    k.s.rank.rankTier = 3;
    k.rng.srand(1234);
    c.layout = 0;
    c.screen = 0;
    for (int i = 0; i < 50; ++i) c.updateCreature();
    CHECK(c.creatTick == 0);  // never on screen 0
    c.screen = 1;
    int guard = 0;
    while (c.creatTick == 0 && guard++ < 1000) c.updateCreature();
    REQUIRE(c.creatTick == 2);
    CHECK(c.cell(1, c.creatCol) != 1);
    CHECK(c.creatY >= 20);
    CHECK(c.creatY + k.z.creatH[0] <= 287);
    std::vector<int> frames{c.creatFrame};
    for (int i = 0; i < 11; ++i) {
        c.updateCreature();
        frames.push_back(c.creatFrame);
    }
    CHECK(frames == std::vector<int>{0, 1, 2, 3, 3, 3, 3, 3, 2, 1, 0, -1});
    c.updateCreature();
    CHECK(c.creatTick == 0);
    // Push: at full reach in the column right of the climber, facing left.
    c.column = 2;
    c.startClimb();
    c.screen = 1;
    c.climbPhase = 0;
    c.creatFrame = 3;
    c.creatScreen = 1;
    c.creatCol = 3;
    c.creatFlip = 1;
    c.creatY = k.s.player.y;  // creatY + 52 inside (y, y + 110)
    const uint16_t coins = k.s.inv.coins;
    c.climbUpdate(0, held(false, false, false, false));
    CHECK(c.jumpStep == 1);
    CHECK(k.s.player.facingLeft == 1);
    CHECK(k.s.inv.coins == coins - 1);
    // Facing the wrong way: no push.
    c.jumpStep = 0;
    c.creatFlip = 0;
    c.climbUpdate(0, held(false, false, false, false));
    CHECK(c.jumpStep == 0);
    CHECK(k.s.inv.coins == coins - 1);
}

TEST_CASE("scenes: castle off the top of screen 3 ends the climb") {
    TM_REQUIRE_GAME_DATA();
    Castle k(*g);
    CastleClimb& c = *k.c;
    c.layout = 0;
    c.column = 3;  // screen 3 of layout 0 has its ladder in column 3
    c.startClimb();
    c.onLadder = true;
    c.screen = c.nextScreen = 3;
    k.s.player.y = 0;
    int guard = 0;
    while (c.climbTick(0, held(false, false, true, false)) && guard++ < 100) {
    }
    CHECK(c.nextScreen == 4);
    CHECK_FALSE(c.finishScreenChange());
    CHECK(c.done);
}

// ------------------------------------------------------------------ cut-scene and celebration

TEST_CASE("scenes: level cut-scene paths") {
    TM_REQUIRE_GAME_DATA();
    SceneRig rig(*g);
    int cleared = 0;
    SceneHooks hooks;
    hooks.clearGateUnlocking = [&] { ++cleared; };
    // Into the castle: no cut-scene, only the flag.
    CHECK(playLevelCutscene(*rig.ctx, 2, hooks) == -1);
    CHECK(cleared == 1);
    // Space during part 1 skips part 2.
    rig.codes = {input::kSpace};
    const uint64_t t0 = rig.clock.nowMs();
    CHECK(playLevelCutscene(*rig.ctx, 0, hooks) == anim::kModalSpace);
    CHECK(cleared == 2);
    CHECK(rig.clock.nowMs() - t0 < 500);
    // Played out: both parts at 7 fps, the play-field black at the end.
    rig.codes.clear();
    rig.next = 0;
    CHECK(playLevelCutscene(*rig.ctx, 1, hooks) == anim::kModalFinished);
    CHECK(cleared == 3);
    const auto& fb = rig.engine->frame();
    const uint8_t black = rig.engine->palette().pen(0);
    int nonBlack = 0;
    for (int y = 20; y < 287; y += 3)
        for (int x = 0; x < 512; x += 3) nonBlack += fb.at(x, y) != black;
    CHECK(nonBlack == 0);
}

TEST_CASE("scenes: celebration rules, skip with Enter, Exit dialog on the rank screen") {
    TM_REQUIRE_GAME_DATA();
    SceneRig rig(*g);
    rig.state.rank.climbsDone = 3;
    rig.state.rank.totalTreasures = 20;
    rig.state.inv.climbPoints = 6;  // 26 -> rank 2, promoted
    rig.codes = {input::kEnter};
    const CelebrationInfo c = celebrate(*rig.ctx, rig.state, rig.tables);
    CHECK(rig.state.rank.climbsDone == 4);
    CHECK(rig.state.rank.totalTreasures == 26);
    CHECK(c.rank == 2);
    CHECK(c.promoted);
    CHECK_FALSE(c.champion);
    CHECK(c.deliveredTreasure == 3);
    CHECK(c.musicZone == -96);
    CHECK(rig.state.hints.pending == rig.tables.rankPromoHints[2]);
    // Rank screen: Esc -> Exit dialog; "n" keeps going, Enter ends; then Esc + "y" quits.
    int saves = 0;
    rig.ctx->services.saveCurrentPlayer = [&] { ++saves; };
    std::vector<uint16_t> dlgKeys = {'n'};
    size_t dk = 0;
    rig.dialogs->hooks().getInputCode = [&]() -> uint16_t {
        if (dk < dlgKeys.size()) return dlgKeys[dk++];
        rig.clock.sleepMs(1);
        return 0;
    };
    rig.codes = {input::kEsc};
    rig.next = 0;
    bool quit = false, enterQueued = false;
    const uint64_t t0 = rig.clock.nowMs();
    rig.onPump = [&] {
        // Enter once the bands are done and the "n" was answered.
        if (!enterQueued && dk == 1 && rig.next == 1 && rig.clock.nowMs() > t0 + 5000) {
            rig.codes.push_back(input::kEnter);
            enterQueued = true;
        }
    };
    try {
        showRankScreen(*rig.ctx, rig.state, rig.tables);
    } catch (const QuitGame&) {
        quit = true;
    }
    CHECK_FALSE(quit);
    CHECK(enterQueued);
    CHECK(rig.next == 2);
    CHECK(saves == 0);
    dlgKeys = {'y'};
    dk = 0;
    rig.codes = {input::kEsc};
    rig.next = 0;
    rig.onPump = nullptr;
    try {
        showRankScreen(*rig.ctx, rig.state, rig.tables);
    } catch (const QuitGame&) {
        quit = true;
    }
    CHECK(quit);
    CHECK(saves == 1);
}
