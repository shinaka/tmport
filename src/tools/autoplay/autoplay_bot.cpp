#include "tools/autoplay/autoplay_bot.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <format>
#include <limits>
#include <queue>

#include "app/script_input.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/room/player.h"
#include "game/room_table.h"
#include "input/event_translator.h"
#include "platform/engine.h"
#include "res/game_files.h"
#include "save/settings.h"
#include "save/users_file.h"
#include "tools/autoplay/autoplay_checks.h"
#include "tools/autoplay/autoplay_rig.h"

namespace tmport::autoplay {

namespace {

using game::room::RoomTables;
namespace castle = game::scenes::castle;

constexpr uint16_t kQuizAnswerHotspot = 0xA80;  // + display position (quiz_screen.h quizhot)
constexpr uint16_t kSignInRowsHotspot = 0x4AE;  // player rows of the Club Sign-In (signin.h)
constexpr int kSignInRowH = 25, kSignInListY = 57;
constexpr int kRecentLines = 80;
constexpr int kInf = std::numeric_limits<int>::max() / 4;
// Moves of the room graph.
constexpr int kMoveNone = 0, kMoveLeft = 1, kMoveRight = 2, kMoveDoor = 3, kMoveGate = 4;
constexpr std::array<int, game::kLevelCount> kLevelFirst = {9, 23, 36}, kLevelLast = {22, 35, 48};

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

// The integer after `key` in `s` (e.g. "slot 4" -> 4), or `def`.
int intAfter(std::string_view s, std::string_view key, int def = -1) {
    const size_t p = s.find(key);
    if (p == std::string_view::npos) return def;
    std::string_view r = s.substr(p + key.size());
    while (!r.empty() && r.front() == ' ') r.remove_prefix(1);
    int v = def;
    const auto res = std::from_chars(r.data(), r.data() + r.size(), v);
    return res.ec == std::errc() ? v : def;
}

}  // namespace

// ------------------------------------------------------------------------------------- set-up

Bot::Bot(Rig& rig, Checker& checker, BotOptions options)
    : rig_(rig),
      checker_(checker),
      o_(std::move(options)),
      s_(rig.state()),
      rt_(RoomTables::fromExe(*rig.ctx().files.exe)),
      ct_(game::scenes::CastleTables::fromExe(*rig.ctx().files.exe)),
      rng_(o_.seed * 2654435761u + 12345u) {
    hookId_ = rig_.engine().addPumpHook([this](PumpPhase p) { onPump(p); });
    lastProgressMs_ = rig_.engine().clock().nowMs();
}

Bot::~Bot() { rig_.engine().removePumpHook(hookId_); }

template <class... Args>
void Bot::say(std::format_string<Args...> fmt, Args&&... args) {
    logLine("bot: " + std::format(fmt, std::forward<Args>(args)...));
}

void Bot::logLine(std::string line) {
    const uint64_t now = rig_.engine().clock().nowMs();
    std::string l = std::format("[{:>9}] {}", now, line);
    if (o_.log) o_.log(l);
    recent_.push_back(std::move(l));
    while (recent_.size() > kRecentLines) recent_.pop_front();
}

void Bot::abort(std::string why) {
    end_ = BotEnd::Aborted;
    logLine("ABORT: " + why);
    throw AutoplayAbort{why + " [" + describe() + "]"};
}

std::string Bot::describe() const {
    static constexpr const char* kModes[] = {"none", "title", "sign-in", "signed-in", "room", "hint", "quiz",
                                             "cut-scene", "castle", "celebration", "see-rank", "shutdown"};
    const auto& p = s_.player;
    return std::format("mode {} room {} x {} y {} frame {}{} coins {} nets {} key {} gems {} points {} clues {} total {} "
                       "climbs {} tier {} busy {} castle screen {}",
                       kModes[static_cast<int>(mode_)], p.room, p.x, p.y, p.frame, p.facingLeft ? " L" : "", s_.inv.coins,
                       s_.inv.nets, s_.inv.haveKey, s_.inv.gemsFound, s_.inv.climbPoints, s_.inv.numClues,
                       s_.rank.totalTreasures, s_.rank.climbsDone, s_.rank.rankTier, static_cast<int>(busy_), castleScreen_);
}

// -------------------------------------------------------------------------------------- input

Bot::KeyDef Bot::keyDef(std::string_view name) const {
    std::string error;
    const auto steps = app::parseScript(name, error);
    KeyDef k;
    if (!steps || steps->size() != 1 || (*steps)[0].kind != app::ScriptStep::Kind::Key) return k;
    const app::ScriptStep& st = (*steps)[0];
    k.key = st.keycode;
    k.scan = st.scancode;
    k.mod = st.mod;
    k.text = st.text;
    return k;
}

void Bot::inject(std::string_view key) {
    const KeyDef k = keyDef(key);
    if (k.key == 0) return;
    InputQueue& in = rig_.engine().input();
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = k.key;
    e.key.scancode = static_cast<SDL_Scancode>(k.scan);
    e.key.mod = k.mod;
    e.key.down = true;
    in.handleEvent(e);
    if (!k.text.empty()) {
        SDL_Event t{};
        t.type = SDL_EVENT_TEXT_INPUT;
        t.text.text = k.text.c_str();  // translated at once
        in.handleEvent(t);
    }
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    in.handleEvent(e);
}

void Bot::injectClick(int gx, int gy) {
    InputQueue& in = rig_.engine().input();
    const float cx = static_cast<float>(gx + EventTranslator::kGameOriginX) + 0.5f;
    const float cy = static_cast<float>(gy + EventTranslator::kGameOriginY) + 0.5f;
    SDL_Event m{};
    m.type = SDL_EVENT_MOUSE_MOTION;
    m.motion.x = cx;
    m.motion.y = cy;
    in.handleEvent(m);
    SDL_Event b{};
    b.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    b.button.button = SDL_BUTTON_LEFT;
    b.button.down = true;
    b.button.clicks = 1;
    b.button.x = cx;
    b.button.y = cy;
    in.handleEvent(b);
    b.type = SDL_EVENT_MOUSE_BUTTON_UP;
    b.button.down = false;
    in.handleEvent(b);
}

void Bot::post(std::string_view key) {
    const KeyDef k = keyDef(key);
    if (k.key == 0) return;
    Engine& eng = rig_.engine();
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = k.key;
    e.key.scancode = static_cast<SDL_Scancode>(k.scan);
    e.key.mod = k.mod;
    e.key.down = true;
    eng.postEvent(e);
    if (!k.text.empty()) {
        SDL_Event t{};
        t.type = SDL_EVENT_TEXT_INPUT;
        t.text.text = k.text.c_str();  // copied by postEvent
        eng.postEvent(t);
    }
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    eng.postEvent(e);
}

void Bot::injectKey(std::string_view key, bool down, bool repeat) {
    const KeyDef k = keyDef(key);
    if (k.key == 0) return;
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.key = k.key;
    e.key.scancode = static_cast<SDL_Scancode>(k.scan);
    e.key.mod = k.mod;
    e.key.down = down;
    e.key.repeat = repeat;
    rig_.engine().input().handleEvent(e);
}

namespace {
constexpr std::array<const char*, 4> kArrowNames = {"left", "right", "up", "down"};
}

void Bot::setHeld(int arrow, bool down) {
    const size_t k = static_cast<size_t>(arrow);
    if (held_[k] == down) return;
    held_[k] = down;
    injectKey(kArrowNames[k], down);
}

void Bot::releaseAll() {
    for (int k = 0; k < 4; ++k) setHeld(k, false);
}

// Arrows are held like keys on a keyboard: a key-down when the bot starts to hold one, a key-up
// when it lets go (the controllers poll the held flags once per tick). Every key-down is also an
// input code, so Enter / Space are only pressed when the queue is empty (see roomTick).
void Bot::apply(const Act& a) {
    if (a.esc) {
        releaseAll();
        inject("esc");
        return;
    }
    setHeld(0, a.left);
    setHeld(1, a.right);
    setHeld(2, a.up);
    setHeld(3, a.down);
    if (a.enter) inject("enter");
    if (a.space) inject("space");
    for (int r = 0; r < o_.autorepeat; ++r)
        for (int k = 0; k < 4; ++k)
            if (held_[static_cast<size_t>(k)]) injectKey(kArrowNames[static_cast<size_t>(k)], true, true);
}

bool Bot::rateOk(uint64_t ms) {
    const uint64_t now = rig_.engine().clock().nowMs();
    if (lastKeyMs_ != 0 && now - lastKeyMs_ < ms) return false;
    lastKeyMs_ = now;
    return true;
}

// -------------------------------------------------------------------------------------- trace

void Bot::onTrace(std::string_view l) {
    // The per-tick castle lines are frequent: keep them out of the log unless verbose.
    const bool tick = startsWith(l, "scene: castle: climb screen") || startsWith(l, "scene: castle: walk frame") ||
                      startsWith(l, "scene: sfx") || startsWith(l, "room: presentation pause");
    if (!tick || o_.log) logLine(std::string(l));
    if (!tick) progress();

    if (startsWith(l, "S5 title")) {
        mode_ = Mode::Title;
    } else if (startsWith(l, "S6 restart point")) {
        releaseAll();
        busy_ = Busy::None;
        menuKeys_.clear();
        menuNext_ = 0;
    } else if (startsWith(l, "S7 sign-in")) {
        mode_ = Mode::SignIn;
        signInStep_ = 0;
        signInCycles_ = 0;
        dialogExpected_ = false;
    } else if (startsWith(l, "S7 signed in:")) {
        mode_ = Mode::SignedIn;
        o_.name = s_.player.nameString();  // as stored (first letter upper-cased, lost keys)
        if (resumeExpected_ && l.find("existing player") != std::string_view::npos) {
            const auto rec = game::toRecord(s_);
            checker_.compareResume(*resumeExpected_, std::vector<uint8_t>(rec.begin(), rec.end()), resumeHow_);
            ++stats_.resumes;
            say("resume check after {}: {}", resumeHow_,
                std::equal(rec.begin(), rec.end(), resumeExpected_->begin(), resumeExpected_->end()) ? "record identical"
                                                                                                       : "RECORD DIFFERS");
            resumeExpected_.reset();
        }
        // From now on the player exists in the roster.
        o_.existingPlayer = true;
    } else if (startsWith(l, "S8 climb set-up:")) {
        mode_ = Mode::Room;
        const bool fresh = l.find("new climb") != std::string_view::npos;
        checker_.onClimbSetup(s_, fresh);
        climbStartMs_ = rig_.engine().clock().nowMs();
        busy_ = Busy::None;
        lastRoom_ = -1;
        huntTicksLevel_ = 0;
        huntLevel_ = -1;
        shopping_ = false;
        excursionsThisClimb_ = 0;
        castleScreen_ = 0;
        prevCoins_ = s_.inv.coins;
        prevNets_ = s_.inv.nets;
    } else if (startsWith(l, "S9 hint ")) {
        releaseAll();
        mode_ = Mode::Hint;
        modalReady_ = false;
        ++stats_.hints;
    } else if (startsWith(l, "S9 elf question: slot ")) {
        releaseAll();
        mode_ = Mode::Quiz;
        quizSlot_ = intAfter(l, "slot");
        quizStage_ = 0;
        quizPos_ = -1;
        quizAnswered_ = false;
        modalReady_ = false;
        ++stats_.questions;
    } else if (startsWith(l, "scene: question left with Esc")) {  // (before "scene: question ")
        quizStage_ = 2;
        ++stats_.escapes;
    } else if (startsWith(l, "scene: question ")) {
        quizStage_ = 1;
        quizSkip_ = 1;  // the screen's Input_Flush follows at once
    } else if (startsWith(l, "scene: answer position ")) {
        quizStage_ = 2;
        modalReady_ = false;
        if (l.find("correct") != std::string_view::npos)
            ++stats_.correct;
        else
            ++stats_.wrong;
    } else if (startsWith(l, "scene: clue box mode 2")) {
        modalReady_ = true;
    } else if (startsWith(l, "scene: clue box mode 0")) {
        if (mode_ == Mode::Hint || mode_ == Mode::Quiz) mode_ = Mode::Room;
    } else if (startsWith(l, "S9 room ")) {
        mode_ = Mode::Room;
        if (busy_ == Busy::Door || busy_ == Busy::Gate) busy_ = Busy::None;
        if (l.find("level change") != std::string_view::npos) ++stats_.gates;
    } else if (startsWith(l, "S10 level cut-scene")) {
        releaseAll();
        mode_ = Mode::Cutscene;
        animSkip_ = std::uniform_real_distribution<double>(0, 1)(rng_) < o_.skipAnimRate;
    } else if (startsWith(l, "scene: level cut-scene ")) {
        ++stats_.cutscenes;
    } else if (startsWith(l, "S11 castle climb")) {
        mode_ = Mode::Castle;
        castleWalking_ = true;
        castleScreen_ = 0;
        creature_ = Creature{};
        busy_ = Busy::None;
        prevCoins_ = s_.inv.coins;
        seekCreature_ = std::uniform_real_distribution<double>(0, 1)(rng_) < o_.creatureSeekRate;
        if (seekCreature_) say("castle: this time the wall creature may push me");
    } else if (startsWith(l, "scene: castle: layout ")) {
        castleLayout_ = intAfter(l, "layout", 0);
        planCastle();
    } else if (startsWith(l, "scene: castle: walk frame")) {
        castleWalking_ = true;
        castleScreen_ = 0;
    } else if (startsWith(l, "scene: castle: walk")) {
        if (!castleWalking_) ++stats_.falls;
        castleWalking_ = true;
        castleScreen_ = 0;
    } else if (startsWith(l, "scene: castle: climb screen ")) {
        castleWalking_ = false;
        castleScreen_ = intAfter(l, "screen", castleScreen_);
    } else if (startsWith(l, "scene: castle: creature screen ")) {
        creature_.screen = intAfter(l, "screen");
        creature_.column = intAfter(l, "column");
        creature_.y = intAfter(l, " y", 0);
        creature_.flip = intAfter(l, "flip", 0);
        creature_.seenTick = castleTickNo_;
        creature_.active = true;
    } else if (startsWith(l, "scene: castle: done")) {
        mode_ = Mode::None;
    } else if (startsWith(l, "S13 climb end")) {
        releaseAll();
        mode_ = Mode::Celebrate;
        rankShown_ = false;
        animSkip_ = std::uniform_real_distribution<double>(0, 1)(rng_) < o_.skipAnimRate;
        checker_.onClimbEnd(s_);
    } else if (startsWith(l, "scene: rank screen")) {
        rankShown_ = true;
    } else if (startsWith(l, "S13 celebrated:")) {
        ++stats_.climbsDone;
        const bool promoted = l.find("promoted") != std::string_view::npos;
        const bool champion = l.find("champion") != std::string_view::npos;
        stats_.promotions += promoted;
        stats_.champion += champion;
        stats_.finalTotal = s_.rank.totalTreasures;
        stats_.finalRank = s_.rank.rank;
        checker_.onCelebrated(s_, promoted, champion);
        if (stats_.climbsDone >= o_.climbs) exitRequested_ = true;
    } else if (startsWith(l, "save: ")) {
        std::string why(l.substr(6));
        if (const size_t p = why.find(" ("); p != std::string::npos) why.resize(p);
        checker_.onSave(s_, rig_.paths().usersDat, why);
    } else if (startsWith(l, "See Rank")) {
        releaseAll();
        menuReturn_ = mode_;
        mode_ = Mode::SeeRank;
        rankShown_ = false;
        // The menu bar runs the command inside the input fetch of a room tick: that tick's frame
        // timer is still armed, so only a later tick means "back in the room".
        Engine& e = rig_.engine();
        lastTickDeadline_ = e.clock().nowMs() + e.timers().remaining(DeadlineTimers::kFrameSlot);
    } else if (startsWith(l, "S12 shutdown")) {
        mode_ = Mode::Shutdown;
        if (end_ == BotEnd::Running) end_ = stats_.climbsDone >= o_.climbs ? BotEnd::Finished : BotEnd::ExitedMidClimb;
    }
}

void Bot::progress() { lastProgressMs_ = rig_.engine().clock().nowMs(); }

// --------------------------------------------------------------------------------------- pump

void Bot::onPump(PumpPhase phase) {
    ++heartbeat_;
    Engine& e = rig_.engine();
    const uint64_t now = e.clock().nowMs();
    if (now == lastClockMs_) {
        if (++pumpsSameClock_ > 20'000'000ull) abort("hang: 20 million pumps without the clock moving");
    } else {
        lastClockMs_ = now;
        pumpsSameClock_ = 0;
    }
    if (phase != PumpPhase::Fetch) return;
    {
        const auto& p = s_.player;
        const std::array<int, 11> key = {static_cast<int>(mode_), p.room, p.x, p.y, s_.inv.coins, s_.inv.nets,
                                         s_.inv.haveKey, s_.inv.gemsFound, s_.inv.numClues, castleScreen_,
                                         static_cast<int>(busy_)};
        if (key != progressKey_) {
            progressKey_ = key;
            lastProgressMs_ = now;
        }
    }
    if (now - lastProgressMs_ > o_.stallGameMs) abort(std::format("stall: no progress for {} s of game time", (now - lastProgressMs_) / 1000));
    if ((mode_ == Mode::Room || mode_ == Mode::Castle) && climbStartMs_ != 0 && now - climbStartMs_ > o_.maxClimbGameMs)
        abort(std::format("climb not finished after {} game minutes", (now - climbStartMs_) / 60000));

    if (rig_.inDialogFetch()) {
        dialogFetch();
        return;
    }
    if (dialogAnswered_) {
        // The dialog took the answer: the game polls elsewhere again.
        dialogAnswered_ = false;
        dialogExpected_ = false;
    }
    const uint64_t rem = e.timers().remaining(DeadlineTimers::kFrameSlot);
    const bool tickFetch = rem >= 90 && rem <= 100 && now + rem != lastTickDeadline_;
    if (menuStep()) {
        // The menu command runs inside a later fetch of this tick (the menu bar's pump hook): its
        // own fetches must not count as a new room tick.
        if (tickFetch) lastTickDeadline_ = now + rem;
        return;
    }
    switch (mode_) {
        case Mode::Title: titleFetch(); break;
        case Mode::SignIn: signInFetch(); break;
        case Mode::Hint: hintFetch(); break;
        case Mode::Quiz: quizFetch(); break;
        case Mode::Cutscene: animFetch(animSkip_); break;
        case Mode::Celebrate:
            if (animSkip_)
                animFetch(true);
            else if (rankShown_ && rateOk(300))
                inject("enter");
            break;
        case Mode::SeeRank:
            if (tickFetch && rankShown_) {
                mode_ = menuReturn_;  // back in the room
                lastTickDeadline_ = now + rem;
                if (mode_ == Mode::Room) roomTick();
                break;
            }
            seeRankFetch();
            break;
        case Mode::Room:
            if (tickFetch) {
                lastTickDeadline_ = now + rem;
                roomTick();
            }
            break;
        case Mode::Castle:
            if (tickFetch) {
                lastTickDeadline_ = now + rem;
                castleTick();
            }
            break;
        default: break;
    }
}

bool Bot::menuStep() {
    if (menuKeys_.empty()) return false;
    if (menuNext_ >= menuKeys_.size()) {
        menuKeys_.clear();
        menuNext_ = 0;
        return false;
    }
    Engine& e = rig_.engine();
    if (e.pendingPostedEvents() > 0) return true;
    if (!rateOk(40)) return true;
    post(menuKeys_[menuNext_]);
    say("menu key {}", menuKeys_[menuNext_]);
    ++menuNext_;
    return true;
}

void Bot::titleFetch() {
    if (rateOk(300)) inject("space");
}

void Bot::signInFetch() {
    if (!o_.existingPlayer) {
        // Type the name (the first printable key starts a new name), then Enter. Repeated until the
        // sign-in returns (keys lost in an Input_Flush only shorten the name).
        if (!rateOk(signInStep_ == 0 ? 400 : 120)) return;
        std::string n;
        for (char c : o_.name)
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) n += static_cast<char>(c | 0x20);
        if (n.empty()) n = "bot";
        if (signInStep_ < static_cast<int>(n.size())) {
            inject(std::string_view(&n[static_cast<size_t>(signInStep_)], 1));
            ++signInStep_;
        } else {
            inject("enter");
            signInStep_ = 0;
            if (++signInCycles_ > 12) abort("sign-in: the new player is not accepted");
        }
        return;
    }
    // Existing player: click its row (hot spot 0x4AE covers the player rows).
    if (!rateOk(400)) return;
    const HotspotTable& hs = rig_.engine().hotspots();
    const int slot = hs.find(kSignInRowsHotspot);
    if (slot < 0) return;  // the list is not drawn yet
    save::UsersTable t;
    save::readUsersFile(rig_.paths().usersDat, t);
    const int idx = t.findByName(o_.name);
    if (idx >= t.count) abort(std::format("sign-in: player {} is not in users.dat", o_.name));
    const HotspotTable::Entry& r = hs.slots()[static_cast<size_t>(slot)];
    injectClick(r.x + r.w / 2, kSignInListY + kSignInRowH * idx + kSignInRowH / 2);
    if (++signInCycles_ > 20) abort("sign-in: selecting the player does not work");
}

void Bot::dialogFetch() {
    if (!rateOk(80)) return;
    ++stats_.dialogs;
    if (!dialogExpected_) {
        ++stats_.unexpectedDialogs;
        say("unexpected dialog: Enter");
        inject("enter");
        return;
    }
    dialogAnswered_ = true;
    exitPendingTicks_ = 0;
    restartPendingTicks_ = 0;
    switch (dialogIntent_) {
        case DialogIntent::Yes:
            if (snapshotOnYes_) {
                const auto rec = game::toRecord(s_);
                leftRecord_.assign(rec.begin(), rec.end());
                snapshotOnYes_ = false;
                say("left the climb ({}) in room {}", snapshotHow_, s_.player.room);
                // Start Another Game: the sign-in that follows must load exactly this record.
                if (snapshotHow_ == "Start Another Game") expectResume(leftRecord_, snapshotHow_);
            }
            inject("y");
            break;
        case DialogIntent::No: inject("n"); break;
        case DialogIntent::Esc: inject("esc"); break;
        case DialogIntent::Enter: inject("enter"); break;
    }
}

void Bot::hintFetch() {
    if (modalReady_ && rateOk(80)) inject("enter");
}

void Bot::animFetch(bool skip) {
    if (skip && rateOk(300)) inject("space");
}

void Bot::seeRankFetch() {
    if (rankShown_ && rateOk(300)) inject("enter");
}

void Bot::quizFetch() {
    if (quizStage_ == 0) return;
    if (quizStage_ == 2) {
        if (modalReady_ && rateOk(100)) inject("enter");
        return;
    }
    if (quizSkip_ > 0) {
        --quizSkip_;
        return;
    }
    if (quizSlot_ < 0 || quizSlot_ >= game::kQuizSlotCount) return;
    const game::QuizSlot& sl = s_.quiz.slots[static_cast<size_t>(quizSlot_)];
    if (!quizAnswered_) {
        quizAnswered_ = true;
        int right = -1;
        std::vector<int> wrong;
        for (int k = 0; k < 3; ++k) {
            if (sl.tried[static_cast<size_t>(k)]) continue;
            if (sl.perm[static_cast<size_t>(k)] == 0)
                right = k;
            else
                wrong.push_back(k);
        }
        const double r = std::uniform_real_distribution<double>(0, 1)(rng_);
        if (r < o_.escQuestionRate)
            quizPos_ = -2;  // Esc
        else if (r < o_.escQuestionRate + o_.wrongAnswerRate && !wrong.empty())
            quizPos_ = wrong[static_cast<size_t>(rng_() % wrong.size())];
        else
            quizPos_ = right;
        say("question slot {} (q {}): {}", quizSlot_, sl.q,
            quizPos_ == -2 ? std::string("Esc") : std::format("position {}{}", quizPos_, quizPos_ == right ? " (right)" : " (wrong)"));
        if (right < 0) checker_.fail(std::format("quiz slot {}: the right answer is not selectable", quizSlot_));
    }
    if (!rateOk(150)) return;
    if (quizPos_ == -2) {
        inject("esc");
        return;
    }
    if (quizPos_ < 0) {
        inject("esc");
        return;
    }
    const HotspotTable& hs = rig_.engine().hotspots();
    const int slot = hs.find(static_cast<uint16_t>(kQuizAnswerHotspot + quizPos_));
    if (slot < 0) return;
    const HotspotTable::Entry& r = hs.slots()[static_cast<size_t>(slot)];
    injectClick(r.x + r.w / 2, r.y + r.h / 2);
}

// --------------------------------------------------------------------------------------- room

int Bot::questionsLeftOnLevel() const {
    const int level = s_.currentLevel();
    if (level > 2) return 0;
    int n = 0;
    for (int i = 0; i < 3; ++i) n += s_.quiz.slots[static_cast<size_t>(3 * level + i)].q >= 0;
    return n;
}

void Bot::startBusy(Busy b, int limit) {
    busy_ = b;
    busyTicks_ = 0;
    busyLimit_ = limit;
    busyCoins_ = s_.inv.coins;
    busyNets_ = s_.inv.nets;
    busyRoom_ = s_.player.room;
}

bool Bot::busyStep() {
    if (busy_ == Busy::None) return false;
    ++busyTicks_;
    switch (busy_) {
        case Busy::Toss:
            if (busyTicks_ == 2) {
                // Started = step 1 of the toss animation ran in the tick after the Enter (coins alone
                // do not tell: one can also go to an elf's sparkle; frame 0 is in the table too).
                const bool tossing = s_.player.frame == rt_.coinToss[1];
                if (!tossing) {
                    say("toss did not start (Enter not taken: frame {})", s_.player.frame);
                    busy_ = Busy::None;
                    return false;
                }
                ++stats_.tosses;
            }
            if (busyTicks_ >= 15) {
                const uint8_t now = s_.rooms.spotContent(tossRoom_, tossSlot_);
                if (tossContent_ == game::kContentTreasure || tossContent_ == game::kContentKey) {
                    if (now == tossContent_) {
                        ++stats_.tossMisses;
                        say("toss at room {} slot {} missed (content still {})", tossRoom_, tossSlot_, now);
                    } else if (tossContent_ == game::kContentTreasure) {
                        ++stats_.treasures;
                    } else {
                        ++stats_.keys;
                    }
                } else if (tossContent_ == game::kSpotShop) {
                    ++stats_.shopCoins;
                }
                busy_ = Busy::None;
                return false;
            }
            return true;
        case Busy::Net:
            if (busyTicks_ == 2) {
                if (s_.inv.nets >= busyNets_) {
                    say("swing did not start (Space lost)");
                    busy_ = Busy::None;
                    return false;
                }
                ++stats_.swings;
            }
            if (s_.inv.coins > busyCoins_ && busyCoins_ != 0xFFFF) {
                ++stats_.catches;
                busyCoins_ = 0xFFFF;  // counted
            }
            if (busyTicks_ >= 12) {
                busy_ = Busy::None;
                return false;
            }
            return true;
        case Busy::Door:
        case Busy::Gate:
        case Busy::Jump:
            if (s_.player.room != busyRoom_ || busyTicks_ > busyLimit_) {
                if (busyTicks_ > busyLimit_ && busy_ != Busy::Jump) say("{} timed out", busy_ == Busy::Door ? "door" : "gate");
                busy_ = Busy::None;
                return false;
            }
            return true;
        default: busy_ = Busy::None; return false;
    }
}

void Bot::roomTick() {
    ++stats_.roomTicks;
    checker_.onRoomTick(s_);
    if (!checker_.violations().empty() && checker_.violations().size() > 50) abort("too many invariant violations");
    const auto& p = s_.player;
    stats_.maxCoins = std::max<int>(stats_.maxCoins, s_.inv.coins);
    stats_.maxNets = std::max<int>(stats_.maxNets, s_.inv.nets);
    stats_.minCoins = std::min<int>(stats_.minCoins, s_.inv.coins);
    if (p.room != lastRoom_) {
        if (busy_ != Busy::Toss && busy_ != Busy::Net) busy_ = Busy::None;
        lastRoom_ = p.room;
        roomTicksHere_ = 0;
    }
    ++roomTicksHere_;
    if (s_.inv.coins >= prevCoins_ + 10) ++stats_.piles;  // only the rescue pile pays 10 at once
    if (s_.inv.coins + 1 == prevCoins_ && !(busy_ == Busy::Toss && busyTicks_ <= 1) && p.room == lastRoom_ &&
        p.frame != rt_.coinToss[1])
        ++stats_.sparkleHits;
    // Fountain bumps: the bump animation (DS:0bd7 frames) starts.
    {
        const auto& bt = rt_.bump;
        const bool bumpNow = std::find(bt.begin() + 1, bt.end(), p.frame) != bt.end();
        const bool bumpBefore = std::find(bt.begin() + 1, bt.end(), lastFrame_) != bt.end();
        if (p.room == game::room::kFountainRoom && bumpNow && !bumpBefore) ++stats_.bumps;
    }
    lastFrame_ = p.frame;
    prevCoins_ = s_.inv.coins;
    prevNets_ = s_.inv.nets;
    const int level = s_.currentLevel();
    if (level != huntLevel_) {
        huntLevel_ = level;
        huntTicksLevel_ = 0;
    }
    // Scheduled exercises.
    if (o_.exitAtTick >= 0 && !exitDone_ && static_cast<int64_t>(stats_.roomTicks) >= o_.exitAtTick) exitRequested_ = true;
    if (o_.restartAtTick >= 0 && !restartDone_ && static_cast<int64_t>(stats_.roomTicks) >= o_.restartAtTick)
        restartRequested_ = true;
    // An Esc / menu sequence whose dialog never came (lost in an Input_Flush of the same tick,
    // e.g. a hint or a room change): ask again.
    if (exitPendingTicks_ > 0 && ++exitPendingTicks_ > 20) {
        say("Exit request lost: again");
        exitPendingTicks_ = 0;
        exitDone_ = false;
        exitRequested_ = true;
        --stats_.exits;
    }
    if (restartPendingTicks_ > 0 && ++restartPendingTicks_ > 40) {
        say("Start Another Game request lost: again");
        restartPendingTicks_ = 0;
        restartDone_ = false;
        restartRequested_ = true;
        --stats_.restarts;
    }
    if (busyStep()) {
        releaseAll();
        if (o_.tickLog) say("tick room {} x {} y {} frame {}{} coins {} nets {} busy {} ({})", p.room, p.x, p.y, p.frame,
                            p.facingLeft ? " L" : "", s_.inv.coins, s_.inv.nets, static_cast<int>(busy_), busyTicks_);
        return;
    }
    Act a = decideRoom();
    const auto& bt = rt_.bump;
    const bool animating = (p.frame >= game::room::kJumpFirst && p.frame <= game::room::kJumpLast) ||
                           std::find(bt.begin() + 1, bt.end(), p.frame) != bt.end();
    if ((a.enter || a.space) && animating) {
        // Jumping or bumped off the fountain: Player_Update would not take the key now.
        if (busy_ == Busy::Toss || busy_ == Busy::Net) busy_ = Busy::None;
        a = Act{};
    }
    if ((a.enter || a.space) && !rig_.engine().input().empty()) {
        // Input codes are still queued (Room_Run takes one per tick): the key would act later,
        // somewhere else. Wait for the queue to drain.
        ++stats_.keysWaited;
        if (busy_ == Busy::Toss || busy_ == Busy::Net) busy_ = Busy::None;
        a = Act{};
    }
    if (o_.tickLog)
        say("tick room {} x {} y {} frame {}{} coins {} nets {} -> {}{}{}{}{}{}{}", p.room, p.x, p.y, p.frame, p.facingLeft ? " L" : "",
            s_.inv.coins, s_.inv.nets, a.left ? "L" : "", a.right ? "R" : "", a.up ? "U" : "", a.down ? "D" : "",
            a.enter ? "Enter" : "", a.space ? "Space" : "", a.esc ? "Esc" : "");
    apply(a);
}

bool Bot::standRanges(int room, int slot, std::vector<Range>& out) const {
    out.clear();
    const auto& r = s_.rooms[room];
    const int w = std::max<int>(s_.player.w, 40);
    int minX = r.exitLeft == 0xFF ? game::room::kWallLeftX : 4;
    int maxX = r.exitRight == 0xFF ? game::room::kWallRightX - w : 512 - w - 36;
    const int dR = rt_.dxRight[game::room::kCoinLandFrame], dL = rt_.dxLeft[game::room::kCoinLandFrame];
    auto add = [&](int lo, int hi, bool left) {
        lo = std::max(lo, minX);
        hi = std::min(hi, maxX);
        // Room 36: no standing inside the fountain zone (a bump would push the player out).
        if (room == game::room::kFountainRoom) {
            const int zl = game::room::kObstacleLeft - game::room::kObstacleFootW + 1, zr = game::room::kObstacleRight - 1;
            if (lo <= zr && hi >= zl) {
                if (lo < zl) out.push_back({lo, zl - 1, left});
                if (hi > zr) out.push_back({zr + 1, hi, left});
                return;
            }
        }
        if (lo <= hi) out.push_back({lo, hi, left});
    };
    const int S = game::room::kSlotWidth;
    if (slot == 0) {
        add(-1000, S - 1 - dR, false);
        add(-1000, S - 1 + dL, true);
    } else if (slot == 1) {
        add(S - dR, 2 * S - 1 - dR, false);
        add(S + dL, 2 * S - 1 + dL, true);
    } else {
        add(2 * S - dR, 1000, false);
        add(2 * S + dL, 1000, true);
    }
    // Keep out of the door zone (Up there enters the door instead of the gate).
    if (r.doorX > -100 && (r.typeDoor == 1 || r.typeDoor == 2)) {
        std::vector<Range> split;
        for (const Range& g : out) {
            const int zl = r.doorX - w, zr = r.doorX;  // door when x < doorX < x + w
            if (g.lo <= zr && g.hi >= zl) {
                if (g.lo < zl) split.push_back({g.lo, zl - 1, g.facingLeft});
                if (g.hi > zr) split.push_back({zr + 1, g.hi, g.facingLeft});
            } else {
                split.push_back(g);
            }
        }
        out.swap(split);
    }
    return !out.empty();
}

int Bot::bfsFirstMove(int from, int to, bool allowGate, int* dist) const {
    if (dist) *dist = kInf;
    if (!game::RoomTable::valid(from) || !game::RoomTable::valid(to)) return kMoveNone;
    if (from == to) {
        if (dist) *dist = 0;
        return kMoveNone;
    }
    std::array<int, game::kRoomCount> d{}, first{};
    d.fill(-1);
    first.fill(kMoveNone);
    std::queue<int> q;
    d[static_cast<size_t>(from)] = 0;
    q.push(from);
    while (!q.empty()) {
        const int r = q.front();
        q.pop();
        const auto& def = s_.rooms[r];
        std::array<std::pair<int, int>, 4> moves = {{{kMoveLeft, def.exitLeft}, {kMoveRight, def.exitRight}, {kMoveDoor, 0xFF}, {kMoveGate, 0xFF}}};
        if (def.exitDoor != 0xFF && def.doorX > -100 && (def.typeDoor == 1 || def.typeDoor == 2)) moves[2].second = def.exitDoor;
        if (allowGate)
            for (size_t g = 0; g < game::kGateRooms.size(); ++g)
                if (game::kGateRooms[g] == r) moves[3].second = game::kGateTargets[g];
        for (const auto& [m, n] : moves) {
            if (n == 0xFF || !game::RoomTable::valid(n) || d[static_cast<size_t>(n)] >= 0) continue;
            d[static_cast<size_t>(n)] = d[static_cast<size_t>(r)] + 1;
            first[static_cast<size_t>(n)] = r == from ? m : first[static_cast<size_t>(r)];
            q.push(n);
        }
    }
    if (d[static_cast<size_t>(to)] < 0) return kMoveNone;
    if (dist) *dist = d[static_cast<size_t>(to)];
    return first[static_cast<size_t>(to)];
}

bool Bot::simulateJump(int x0, bool facingLeft) const {
    using namespace game::room;
    auto inZone = [](int x) { return x + kObstacleFootW > kObstacleLeft && x < kObstacleRight; };
    auto dxR = [&](int f) { return f >= 0 && f < kPlayerFrameSlots ? rt_.dxRight[static_cast<size_t>(f)] : int16_t{0}; };
    auto dxL = [&](int f) { return f >= 0 && f < kPlayerFrameSlots ? rt_.dxLeft[static_cast<size_t>(f)] : int16_t{0}; };
    auto next = [&](int f) { return f >= 0 && f < kPlayerFrameSlots ? int(rt_.nextFrame[static_cast<size_t>(f)]) : 0; };
    int x = x0, f = kJumpFirst;
    bool jumping = true;
    x += facingLeft ? -dxL(f) : dxR(f);
    for (int guard = 0; guard < 30; ++guard) {
        // Bounds of the tick (boundsAndExits).
        if (!jumping) return !inZone(x);
        if (inZone(x) && f > 14) {
            const size_t k = static_cast<size_t>(f - 15);
            const bool tooLow = k < 7 && ((!facingLeft && x <= rt_.obstacleMinXRight[k]) || (facingLeft && rt_.obstacleMaxXLeft[k] <= x));
            if (tooLow) return false;
        }
        // Next tick, the direction still held.
        f = next(f);
        if (f == 0xFF || f < kJumpFirst || f > kJumpLast) {
            f = 1;
            jumping = false;
        }
        x += facingLeft ? -dxL(f) : dxR(f);
    }
    return false;
}

Bot::Act Bot::jumpFountain(int targetX) {
    using namespace game::room;
    const auto& p = s_.player;
    Act a;
    const bool goRight = targetX > p.x;
    if (p.frame >= kJumpFirst && p.frame <= kJumpLast) {  // in the air: never turn round
        (p.facingLeft ? a.left : a.right) = true;
        return a;
    }
    if (bumpRetreat_ > 0) {
        --bumpRetreat_;
        (goRight ? a.left : a.right) = true;
        return a;
    }
    if (simulateJump(p.x, !goRight)) {
        (goRight ? a.right : a.left) = true;
        a.up = true;
        ++stats_.jumps;
        return a;
    }
    // A start further from the fountain that clears it?
    bool aheadOk = false;
    if (goRight) {
        for (int x = p.x + 1; x <= kObstacleLeft - kObstacleFootW; ++x)
            if (simulateJump(x, false)) aheadOk = true;
    } else {
        for (int x = p.x - 1; x >= kObstacleRight; --x)
            if (simulateJump(x, true)) aheadOk = true;
    }
    if (aheadOk) {
        (goRight ? a.right : a.left) = true;
    } else {
        bumpRetreat_ = 2 + static_cast<int>(rng_() % 3);
        (goRight ? a.left : a.right) = true;
    }
    return a;
}

Bot::Act Bot::walkToward(int x0, int target) {
    using namespace game::room;
    Act a;
    const int room = s_.player.room;
    if (s_.player.frame >= kJumpFirst && s_.player.frame <= kJumpLast) {
        // In the air: holding the other direction would turn the jump round (and at the fountain
        // that is a bump); keep going.
        (s_.player.facingLeft ? a.left : a.right) = true;
        return a;
    }
    if (room == kFountainRoom) {
        const bool leftSide = x0 + kObstacleFootW <= kObstacleLeft;
        const bool rightSide = x0 >= kObstacleRight;
        const bool crossR = leftSide && target >= kObstacleRight;
        const bool crossL = rightSide && target + kObstacleFootW <= kObstacleLeft;
        const bool inAir = s_.player.frame >= kJumpFirst && s_.player.frame <= kJumpLast;
        if (crossR || crossL || inAir) return jumpFountain(target);
        // A target inside the fountain zone (a bump would push the player back): stop at its edge.
        if (target + kObstacleFootW > kObstacleLeft && target < kObstacleRight)
            target = leftSide ? kObstacleLeft - kObstacleFootW : kObstacleRight;
    }
    if (target > x0 + 4)
        a.right = true;
    else if (target < x0 - 4)
        a.left = true;
    return a;
}

Bot::Act Bot::moveTowardRange(const std::vector<Range>& ranges, bool& inside) {
    inside = false;
    const auto& p = s_.player;
    const int x = p.x;
    int best = kInf;
    Act bestAct;
    for (const Range& r : ranges) {
        if (x >= r.lo && x <= r.hi && (p.facingLeft != 0) == r.facingLeft) {
            inside = true;
            return {};
        }
        int cost = kInf;
        Act act;
        if (x >= r.lo && x <= r.hi) {
            // Inside but facing the other way: one step in the range's direction turns the player
            // round; take it when it stays inside (walk tables: the frame after the current one).
            const int f = p.frame >= 0 && p.frame < game::room::kPlayerFrameSlots ? p.frame : 0;
            const int nf = rt_.nextFrame[static_cast<size_t>(f)];
            const int step = nf < game::room::kPlayerFrameSlots
                                 ? (r.facingLeft ? rt_.dxLeft[static_cast<size_t>(nf)] : rt_.dxRight[static_cast<size_t>(nf)])
                                 : 30;
            const int x2 = r.facingLeft ? x - step : x + step;
            if (x2 >= r.lo && x2 <= r.hi) {
                (r.facingLeft ? act.left : act.right) = true;
                if (std::abs(step) < best) {
                    best = std::abs(step);
                    bestAct = act;
                }
                continue;
            }
        }
        if (!r.facingLeft) {
            if (x < r.lo) {
                cost = r.lo - x;
                act = walkToward(x, (r.lo + std::min(r.hi, r.lo + 60)) / 2);
            } else {
                cost = (x - r.lo) + 40;  // go left past the range, then come back walking right
                act = walkToward(x, r.lo - 30);
            }
        } else {
            if (x > r.hi) {
                cost = x - r.hi;
                act = walkToward(x, (r.hi + std::max(r.lo, r.hi - 60)) / 2);
            } else {
                cost = (r.hi - x) + 40;
                act = walkToward(x, r.hi + 30);
            }
        }
        if (cost < best) {
            best = cost;
            bestAct = act;
        }
    }
    return bestAct;
}

Bot::Act Bot::goToRoom(int target) {
    const auto& p = s_.player;
    const auto& def = s_.rooms[p.room];
    int dist = 0;
    const int m = bfsFirstMove(p.room, target, s_.inv.haveKey != 0, &dist);
    Act a;
    switch (m) {
        case kMoveLeft: return walkToward(p.x, -200);
        case kMoveRight: return walkToward(p.x, 800);
        case kMoveDoor: {
            const int w = std::max<int>(p.w, 30);
            if (p.x < def.doorX && def.doorX < p.x + w) {
                a.up = true;
                startBusy(Busy::Door, 12);
                return a;
            }
            return walkToward(p.x, def.doorX - w / 2);
        }
        case kMoveGate: {
            for (int k = 0; k < 3; ++k) {
                if (s_.rooms.spotContent(p.room, k) != game::kSpotGate) continue;
                if (game::room::coinTargetSlot(p.x, p.facingLeft != 0, rt_) == k &&
                    !(def.doorX > -100 && p.x < def.doorX && def.doorX < p.x + p.w)) {
                    a.up = true;
                    startBusy(Busy::Gate, 400);
                    say("gate in room {} (slot {})", p.room, k);
                    return a;
                }
            }
            std::vector<Range> all, one;
            for (int k = 0; k < 3; ++k)
                if (s_.rooms.spotContent(p.room, k) == game::kSpotGate && standRanges(p.room, k, one))
                    all.insert(all.end(), one.begin(), one.end());
            bool inside = false;
            return moveTowardRange(all, inside);
        }
        default: break;
    }
    if (target != p.room) say("room {} cannot be reached from room {}: wandering", target, p.room);
    return wander();
}

Bot::Act Bot::wander() {
    const auto& p = s_.player;
    const auto& def = s_.rooms[p.room];
    if (wanderDir_ == 0) wanderDir_ = (rng_() & 1) ? 1 : -1;
    if (wanderDir_ < 0 && def.exitLeft == 0xFF) wanderDir_ = 1;
    if (wanderDir_ > 0 && def.exitRight == 0xFF) wanderDir_ = -1;
    return walkToward(p.x, wanderDir_ > 0 ? 800 : -200);
}

Bot::Act Bot::goToSpot(int room, int slot, bool& ready) {
    ready = false;
    if (s_.player.room != room) return goToRoom(room);
    std::vector<Range> ranges;
    if (!standRanges(room, slot, ranges)) {
        say("no place to stand for room {} slot {}", room, slot);
        return {};
    }
    bool inside = false;
    Act a = moveTowardRange(ranges, inside);
    if (inside && game::room::coinTargetSlot(s_.player.x, s_.player.facingLeft != 0, rt_) == slot) ready = true;
    return a;
}

std::vector<VisRect> Bot::spriteMasks() const {
    std::vector<VisRect> m;
    const auto& p = s_.player;
    m.push_back({p.x - 10, 0, std::max<int>(p.w, 40) + 20, 400});
    if (s_.coinPile.room == p.room) m.push_back({s_.coinPile.x - 4, 0, s_.coinPile.w + 8, 400});
    if (p.room == game::room::kFountainRoom)
        m.push_back({game::room::kFountainX - 4, game::room::kFountainY - 4, game::room::kFountainW + 8, game::room::kFountainH + 8});
    for (const auto& n : game::room::kNiches)
        if (n.room == p.room) m.push_back({n.x - 2, n.y - 2, game::room::kNicheW + 4, game::room::kNicheH + 4});
    for (const auto& g : game::room::kGateLocks)
        if (g.room == p.room) m.push_back({g.x - 4, g.y - 4, game::room::kGateLockW + 8, game::room::kGateLockH + 8});
    return m;
}

Bot::Act Bot::hunt(bool& active) {
    using namespace game::room;
    active = false;
    const auto& p = s_.player;
    if (s_.rooms.inHall(p.room) || p.room == game::kCastleRoom) return {};
    const auto blobs = findSprites(rig_.engine().frame(), spriteMasks());
    if (blobs.empty()) return {};
    const int pc = p.x + p.w / 2;
    const SpriteBlob* best = nullptr;
    for (const SpriteBlob& b : blobs)
        if (!best || std::abs(b.centre() - pc) < std::abs(best->centre() - pc)) best = &b;
    if (std::abs(best->centre() - pc) > 330) return {};  // too far: not worth the walk
    active = true;
    ++huntTicksLevel_;
    const int ex = estimateElfX(*best);
    const int f1 = rt_.netSwing[1];
    const int xR = ex - kNetReachRight - rt_.dxRight[static_cast<size_t>(f1)];
    const int xL = ex - kNetReachLeft + rt_.dxLeft[static_cast<size_t>(f1)];
    constexpr int kTol = 22;
    if (p.room == kFountainRoom) {
        auto inZone = [](int x) { return x + kObstacleFootW > kObstacleLeft && x < kObstacleRight; };
        const bool leftSide = p.x + kObstacleFootW <= kObstacleLeft;
        // Only elves that can be netted from this side of the fountain.
        const bool okR = !inZone(xR) && (xR + kObstacleFootW <= kObstacleLeft) == leftSide;
        const bool okL = !inZone(xL) && (xL + kObstacleFootW <= kObstacleLeft) == leftSide;
        if (!okR && !okL) {
            active = false;
            --huntTicksLevel_;
            return {};
        }
    }
    Act a;
    if (!p.facingLeft && std::abs(p.x - xR) <= kTol) {
        a.space = true;
        return a;
    }
    if (p.facingLeft && std::abs(p.x - xL) <= kTol) {
        a.space = true;
        return a;
    }
    // Next x if the direction is held one more tick (the walk tables).
    const int f = p.frame;
    const int nf = f >= 0 && f < kPlayerFrameSlots ? rt_.nextFrame[static_cast<size_t>(f)] : 1;
    const int stepR = nf < kPlayerFrameSlots ? rt_.dxRight[static_cast<size_t>(nf)] : 10;
    const int stepL = nf < kPlayerFrameSlots ? rt_.dxLeft[static_cast<size_t>(nf)] : 10;
    if (p.x < xR - kTol) {
        if (!p.facingLeft && std::abs(p.x + stepR - xR) > std::abs(p.x - xR) && std::abs(p.x - xR) <= 30) a.space = true;
        else return walkToward(p.x, xR);
        return a;
    }
    if (p.x > xL + kTol) {
        if (p.facingLeft && std::abs(p.x - stepL - xL) > std::abs(p.x - xL) && std::abs(p.x - xL) <= 30) a.space = true;
        else return walkToward(p.x, xL);
        return a;
    }
    // Too close: back off toward the nearer stand point, then turn round.
    return (p.x - xR < xL - p.x) ? walkToward(p.x, xR - 40) : walkToward(p.x, xL + 40);
}

Bot::Act Bot::decideRoom() {
    const auto& p = s_.player;
    const int room = p.room;
    Act a;
    // 1. Scheduled exercises (with an empty input queue, so that the key acts in this tick).
    if (((exitRequested_ && !exitDone_) || (restartRequested_ && !restartDone_)) && !rig_.engine().input().empty()) return a;
    if (exitRequested_ && !exitDone_) {
        exitDone_ = true;
        exitPendingTicks_ = 1;
        dialogExpected_ = true;
        dialogIntent_ = DialogIntent::Yes;
        snapshotOnYes_ = stats_.climbsDone < o_.climbs;  // mid-climb: remember the record for the resume check
        snapshotHow_ = "Exit";
        ++stats_.exits;
        say("Esc + Yes ({})", stats_.climbsDone < o_.climbs ? "exit mid-climb" : "all climbs done");
        a.esc = true;
        return a;
    }
    if (restartRequested_ && !restartDone_) {
        restartDone_ = true;
        restartPendingTicks_ = 1;
        dialogExpected_ = true;
        dialogIntent_ = DialogIntent::Yes;
        snapshotOnYes_ = true;
        snapshotHow_ = "Start Another Game";
        ++stats_.restarts;
        menuKeys_ = {"alt+f", "down", "enter"};
        menuNext_ = 0;
        releaseAll();
        say("File > Start Another Game");
        return a;
    }
    if (o_.menuExcursions && excursionsThisClimb_ < 3 && stats_.roomTicks >= nextExcursionTick_ && busy_ == Busy::None) {
        nextExcursionTick_ = stats_.roomTicks + 300 + rng_() % 900;
        releaseAll();
        ++excursionsThisClimb_;
        ++stats_.menuTrips;
        switch (rng_() % 5) {
            case 0:
                menuKeys_ = {"alt+f", "enter"};  // See Rank
                say("menu: File > See Rank");
                break;
            case 1:
                menuKeys_ = {"alt+h", "enter"};  // first help topic (pager)
                dialogExpected_ = true;
                dialogIntent_ = DialogIntent::Esc;
                say("menu: Help (first topic)");
                break;
            case 2:
                menuKeys_ = {"alt+o", "enter"};  // New Player Messages: Yes (all hints again)
                dialogExpected_ = true;
                dialogIntent_ = DialogIntent::Yes;
                say("menu: Options > New Player Messages > Yes");
                break;
            case 3:
                inject("ctrl+s");  // Music and Sound + Voice off ... and on again below
                menuKeys_ = {"ctrl+s"};
                say("Ctrl+S twice");
                break;
            default:
                if (s_.inv.numClues > 0) {
                    inject("c");
                    say("clue replay");
                }
                break;
        }
        return a;
    }
    // Exercise: spend everything on purpose (the rescue coin pile must come).
    if (o_.exhaustLevel >= 0 && !exhausted_ && s_.currentLevel() == o_.exhaustLevel && !s_.rooms.inHall(room)) {
        if (s_.inv.coins == 0 && s_.inv.nets == 0) {
            exhausted_ = true;
            ++stats_.exhausted;
            say("out of coins and nets on purpose in room {}", room);
        } else if (s_.inv.nets > 0) {
            a.space = true;
            startBusy(Busy::Net, 12);
            return a;
        } else {
            tossRoom_ = room;
            tossSlot_ = game::room::coinTargetSlot(p.x, p.facingLeft != 0, rt_);
            tossContent_ = s_.rooms.spotContent(room, tossSlot_);
            a.enter = true;
            startBusy(Busy::Toss, 15);
            return a;
        }
    }
    // 2. The rescue coin pile in this room: walk over it.
    if (s_.coinPile.room == room) {
        int target = s_.coinPile.x + s_.coinPile.w / 2 - p.w / 2;
        if (room == game::room::kFountainRoom) {
            using namespace game::room;
            const bool reachL = s_.coinPile.x <= kObstacleLeft - kObstacleFootW + p.w;  // touchable from the left side
            const bool reachR = s_.coinPile.x + s_.coinPile.w >= kObstacleRight;       // ... from the right side
            const bool leftSide = p.x + kObstacleFootW <= kObstacleLeft;
            if (!(leftSide ? reachL : reachR)) target = leftSide ? kObstacleRight + 10 : 20;  // jump across it
        }
        return walkToward(p.x, target);
    }
    // 3. The treasure hall: to the mountain.
    if (s_.rooms.inHall(room)) return goToRoom(game::kMountainStartRoom);
    const int level = s_.currentLevel();
    if (level > 2) return a;
    // 4. What is left on this level.
    struct Target {
        int room, slot, dist;
    };
    std::vector<Target> targets;
    for (int r = kLevelFirst[static_cast<size_t>(level)]; r <= kLevelLast[static_cast<size_t>(level)]; ++r)
        for (int k = 0; k < 3; ++k) {
            const uint8_t c = s_.rooms.spotContent(r, k);
            if (c != game::kContentTreasure && c != game::kContentKey) continue;
            int d = kInf;
            bfsFirstMove(room, r, false, &d);
            if (d < kInf) targets.push_back({r, k, d});
        }
    const int need = static_cast<int>(targets.size());
    const int coins = s_.inv.coins, nets = s_.inv.nets;
    // 5. Nothing left at all: walk on (the pile appears in the next room entered).
    if (coins == 0 && nets == 0) return wander();
    // 6. Nets running low and coins to spare: the net shop of this level.
    const int shopRoom = game::kShopRooms[static_cast<size_t>(level)];
    if (!shopping_ && nets <= 2 && coins >= need + s_.shop.coinsLeft + 1) {
        shopping_ = true;
        say("going shopping: {} coins, {} nets, shop wants {}", coins, nets, s_.shop.coinsLeft);
    }
    if (shopping_ && s_.shop.state != 0) return a;  // the shop door is open: the net is being taken
    if (shopping_) {
        if (nets > 2 || coins < 1) {
            if (nets > 2) ++stats_.shopBuys;
            shopping_ = false;
        } else {
            for (int k = 0; k < 3; ++k) {
                if (s_.rooms.spotContent(shopRoom, k) != game::kSpotShop) continue;
                bool ready = false;
                Act m = goToSpot(shopRoom, k, ready);
                if (ready) {
                    tossRoom_ = shopRoom;
                    tossSlot_ = k;
                    tossContent_ = game::kSpotShop;
                    startBusy(Busy::Toss, 15);
                    Act t;
                    t.enter = true;
                    return t;
                }
                return m;
            }
            shopping_ = false;
        }
    }
    // 7. Elves: for coins and clue words (and to see questions).
    const bool wantHunt = nets > 0 && (coins < need + 1 || (questionsLeftOnLevel() > 0 && huntTicksLevel_ < 900 &&
                                                            std::uniform_real_distribution<double>(0, 1)(rng_) < o_.huntRate));
    if (wantHunt && huntTicksLevel_ < 2500) {
        bool active = false;
        Act h = hunt(active);
        if (active) {
            if (h.space) {
                startBusy(Busy::Net, 12);
            }
            return h;
        }
    }
    // 8. No coins, nets left and no elf in reach: swing anyway (a passing elf may be caught; once
    //    the nets are gone too the rescue pile comes).
    if (coins == 0) {
        a.space = true;
        startBusy(Busy::Net, 12);
        return a;
    }
    // 9. The nearest spot that hides something.
    if (!targets.empty()) {
        const Target* best = &targets[0];
        for (const Target& t : targets)
            if (t.dist < best->dist) best = &t;
        bool ready = false;
        Act m = goToSpot(best->room, best->slot, ready);
        if (ready) {
            tossRoom_ = best->room;
            tossSlot_ = best->slot;
            tossContent_ = s_.rooms.spotContent(best->room, best->slot);
            startBusy(Busy::Toss, 15);
            say("toss at room {} slot {} (content {}) from x {}{}", best->room, best->slot, tossContent_, p.x,
                p.facingLeft ? " facing left" : "");
            Act t;
            t.enter = true;
            return t;
        }
        return m;
    }
    // 10. Everything found: through the gate.
    if (s_.inv.haveKey) return goToRoom(game::kGateTargets[static_cast<size_t>(level)]);
    say("nothing to do on level {} (no key, no targets)", level);
    return a;
}

// ------------------------------------------------------------------------------------- castle

int Bot::castleCell(int screen, int col) const {
    if (castleLayout_ < 0 || castleLayout_ > 3 || screen < 0 || screen >= castle::kScreens || col < 0 || col >= castle::kColumns)
        return 0;
    return ct_.layouts[static_cast<size_t>(castleLayout_)][static_cast<size_t>(screen)][static_cast<size_t>(col)];
}

void Bot::planCastle() {
    // Choose one column per screen: a full ladder whose continuation on the next screen holds the
    // climber at its bottom; consecutive choices joined by ladders at the bottom of the screen.
    auto holdsLow = [&](int s, int c) { const int k = castleCell(s, c); return k == 1 || k == 2; };
    auto upOk = [&](int s, int c) { return castleCell(s, c) == 1 && (s == castle::kScreens - 1 || holdsLow(s + 1, c)); };
    auto lateral = [&](int s, int a, int b) {
        const int d = a < b ? 1 : -1;
        for (int c = a; c != b + d; c += d)
            if (!holdsLow(s, c)) return false;
        return true;
    };
    int bestCost = kInf;
    std::array<int, castle::kScreens> best{};
    for (int code = 0; code < 625; ++code) {
        std::array<int, castle::kScreens> t{};
        int x = code;
        for (int s = 0; s < castle::kScreens; ++s) {
            t[static_cast<size_t>(s)] = x % 5;
            x /= 5;
        }
        bool ok = true;
        int cost = 0;
        for (int s = 0; s < castle::kScreens && ok; ++s) {
            if (!upOk(s, t[static_cast<size_t>(s)])) ok = false;
            if (s > 0 && ok) {
                if (!lateral(s, t[static_cast<size_t>(s - 1)], t[static_cast<size_t>(s)])) ok = false;
                cost += std::abs(t[static_cast<size_t>(s)] - t[static_cast<size_t>(s - 1)]);
            }
        }
        if (ok && cost < bestCost) {
            bestCost = cost;
            best = t;
        }
    }
    if (bestCost == kInf) {
        checker_.note(std::format("castle layout {}: no clean ladder route found; climbing greedily", castleLayout_));
        for (int s = 0; s < castle::kScreens; ++s) {
            best[static_cast<size_t>(s)] = 2;
            for (int c = 0; c < castle::kColumns; ++c)
                if (castleCell(s, c) == 1) best[static_cast<size_t>(s)] = c;
        }
    }
    castleTarget_ = best;
    say("castle layout {}: columns {} {} {} {}", castleLayout_, best[0], best[1], best[2], best[3]);
}

void Bot::castleTick() {
    ++stats_.castleTicks;
    ++castleTickNo_;
    if (creature_.active && castleTickNo_ - creature_.seenTick > 12) creature_.active = false;
    if (s_.inv.coins + 1 == prevCoins_ && !castleWalking_) {
        ++stats_.pushes;
        if (seekCreature_) say("castle: pushed by the wall creature");
        seekCreature_ = false;  // once is enough
    }
    prevCoins_ = s_.inv.coins;
    const auto& p = s_.player;
    Act a;
    if (o_.exitInCastleAtTick >= 0 && !exitDone_ && static_cast<int64_t>(castleTickNo_) >= o_.exitInCastleAtTick) {
        // Exercise: Exit on the castle wall; the next run resumes the climb in room 49.
        exitDone_ = true;
        dialogExpected_ = true;
        dialogIntent_ = DialogIntent::Yes;
        snapshotOnYes_ = true;
        snapshotHow_ = "Exit (castle wall)";
        ++stats_.exits;
        say("Esc + Yes on the castle wall (screen {})", castleScreen_);
        a.esc = true;
        apply(a);
        return;
    }
    if (castleWalking_) {
        const int c = castleTarget_[0];
        const int cx = ct_.colX[static_cast<size_t>(c)];
        if (p.x < cx && cx < p.x + p.w) {
            a.up = true;
        } else if (p.x + p.w / 2 < cx) {
            a.right = true;
        } else {
            a.left = true;
        }
        apply(a);
        return;
    }
    // On the wall: the column under the climber.
    int col = 0, bestD = kInf;
    for (int c = 0; c < castle::kColumns; ++c) {
        const int d = std::abs(ct_.colX[static_cast<size_t>(c)] + castle::kClimbXOffset - p.x);
        if (d < bestD) {
            bestD = d;
            col = c;
        }
    }
    const int s = std::clamp(castleScreen_, 0, castle::kScreens - 1);
    const int target = castleTarget_[static_cast<size_t>(s)];
    // Exercise: get pushed once (the push, the jump away and maybe a fall). The creature pushes a
    // climber in the column it faces, inside its reach band.
    if (seekCreature_ && creature_.active && creature_.screen == s && p.frame < 24) {
        const int tc = creature_.column + (creature_.flip == 0 ? 1 : -1);
        const int b = creature_.y + castle::kCreatReach;
        const int k = castleCell(s, tc);
        if (tc >= 0 && tc < castle::kColumns && (k == 1 || (k == 2 && p.y > castle::kHalfLadderMinY + 24))) {
            if (col != tc) {
                const int next = col + (tc > col ? 1 : -1);
                const int kn = castleCell(s, next);
                if (kn == 1 || (kn == 2 && p.y > castle::kHalfLadderMinY + 24)) (tc > col ? a.right : a.left) = true;
            } else if (p.y >= b - 8) {
                a.up = true;
            } else if (p.y + castle::kCreatHitH <= b + 8) {
                a.down = true;
            }
            apply(a);
            return;
        }
    }
    // The wall creature: keep out of its reach band (playerY < creatY + 52 < playerY + 110) when
    // it is next to the climber and facing him.
    if (creature_.active && creature_.screen == s &&
        ((creature_.column == col - 1 && creature_.flip == 0) || (creature_.column == col + 1 && creature_.flip != 0))) {
        const int b = creature_.y + castle::kCreatReach;
        if (p.y < b && b < p.y + castle::kCreatHitH) {
            const int up = p.y + castle::kCreatHitH - b, down = b - p.y;
            (up <= down ? a.up : a.down) = true;
        } else if (b <= p.y) {
            // The band is above: wait below it (or move sideways away from it).
        } else {
            a.up = true;
        }
        apply(a);
        return;
    }
    if (col != target) {
        const int next = col + (target > col ? 1 : -1);
        const int k = castleCell(s, next);
        if (k == 1 || (k == 2 && p.y > castle::kHalfLadderMinY + 24)) {
            (target > col ? a.right : a.left) = true;
        } else if (k == 2) {
            a.down = true;  // a half ladder only holds low: climb down first
        } else {
            a.up = true;  // no way across here; climb on
        }
    } else {
        a.up = true;
    }
    apply(a);
}

}  // namespace tmport::autoplay
