// The DOS autoplayer's bot (dos_autoplay_bot.h).
#include "dos/autoplay/dos_autoplay_bot.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "dos/app/dos_runtime.h"
#include "dos/autoplay/dos_autoplay_rig.h"
#include "dos/game/game.h"
#include "dos/game/ui.h"
#include "dos/save/users_store.h"

namespace tmport::dos::autoplay {

namespace {

constexpr int kUiGap = 5;          // game ticks between two UI keys
constexpr int kSearchTol = 16;     // px from a hiding place's middle where the bot drops its coin
constexpr int kJumpHoldTicks = 52; // PgUp / Home held: a first flip (43 ticks) and a second one
constexpr int kChaseLimit = 1500;  // ticks per level spent walking after elves
constexpr unsigned kGates[3] = {cells::kTrunkGate, cells::kGeyserGate, cells::kArchGate};

bool between(int v, int lo, int hi) { return v >= lo && v <= hi; }
unsigned rowOf(unsigned cell) { return cell >> 4; }
unsigned colOf(unsigned cell) { return cell & 0xF; }

// Hiding slots of a level (g_hidingCells DS:11AA), without the mirror slots.
std::vector<int> slotsOf(int level) {
    static constexpr int kFirst[3] = {29, 13, 0}, kEnd[3] = {48, 29, 13};
    std::vector<int> out;
    for (int i = kFirst[level]; i < kEnd[level]; ++i)
        if (i != 11 && i != 12 && i != 27 && i != 28) out.push_back(i);
    return out;
}

int hiddenClueWords(const DosGame& g, int level) {
    if (!g.hasRec() || level < 0 || level > 2) return 0;
    int n = 0;
    for (int k = 0; k < 3; ++k) n += (g.rec().clueWords[static_cast<size_t>(3 * level + k)] & 0x80) ? 1 : 0;
    return n;
}

mischief::dos::PcKey keyOf(std::string_view name) {
    const auto k = mischief::dos::pcKeyByName(name);
    return k ? *k : mischief::dos::PcKey{};
}

}  // namespace

Bot::Bot(Rig& rig, Checker& checker, BotOptions options)
    : rig_(rig), checker_(checker), o_(std::move(options)), g_(rig.game()), vision_(rig.game()), rng_(o_.seed * 2654435761u + 17u) {}

Bot::~Bot() = default;

bool Bot::chance(double p) { return std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < p; }

void Bot::logLine(std::string line) {
    line = std::format("[{:6}] {}", tick_, line);
    if (o_.log) o_.log(line);
    recent_.push_back(std::move(line));
    while (recent_.size() > 40) recent_.pop_front();
}

void Bot::abort(std::string why) {
    end_ = BotEnd::Aborted;
    say("ABORT: {}", why);
    throw BotAbort{std::format("{} [{}]", why, describe())};
}

std::string Bot::describe() const {
    const auto& p = g_.player.obj;
    return std::format("tick {} ui {:#04x} level {} scene {} area {:#x} cell {:02X} x {:#x} y {:#x} state {} coins {} nets {} "
                       "found {} key {} tier {} climbs {}",
                       tick_, g_.core.uiMode, g_.core.level, g_.world.loadedScene, g_.core.area, cellOf(p.x, p.y), p.x, p.y,
                       g_.player.playerState, g_.core.coins, g_.core.nets, g_.core.treasuresFound, g_.core.keyState,
                       g_.core.tier, stats_.climbsDone);
}

// ---------------------------------------------------------------------------------- keys

void Bot::hold(Key k, bool down) {
    static constexpr std::string_view kNames[] = {"left", "right", "up", "down", "space", "pgup", "home"};
    auto& h = held_[static_cast<size_t>(k)];
    if (h == down) return;
    const mischief::dos::PcKey key = keyOf(kNames[static_cast<size_t>(k)]);
    if (down) rig_.keyDown(key);
    else rig_.keyUp(key);
    h = down;
}

void Bot::holdOnly(std::initializer_list<Key> keys) {
    for (size_t i = 0; i < held_.size(); ++i) {
        const Key k = static_cast<Key>(i);
        if (std::find(keys.begin(), keys.end(), k) == keys.end()) hold(k, false);
    }
    for (Key k : keys) hold(k, true);
}

void Bot::releaseMovement() { holdOnly({}); }

void Bot::tap(std::string_view keyName) {
    if (tapped_) return;
    const mischief::dos::PcKey k = keyOf(keyName);
    rig_.keyDown(k);
    tapped_ = k;
    lastTapTick_ = tick_;
    if (o_.tickLog)
        say("tap {} (ui {:#04x} dialog {} menu {} item {} help page {})", keyName, g_.core.uiMode, g_.ui.dialogId, g_.ui.menuOpen,
            g_.ui.menuItem, g_.ui.helpPage);
}

bool Bot::uiReady() const {
    return !tapped_ && tick_ >= lastTapTick_ + kUiGap && g_.bios().size() == 0 && g_.input.cmd == -1 && g_.input.queuedCmd == -1;
}

// ---------------------------------------------------------------------------------- loop

bool Bot::step() {
    if (!rig_.running()) return false;
    if (tick_ == 0) say("start: {}", o_.existingPlayer ? std::format("player {}", o_.name) : std::format("new player {}", o_.name));
    beforeTick();
    before_ = snapshotOf(g_, rig_.users());
    try {
        rig_.step();
    } catch (const std::exception& e) {
        abort(std::format("exception in the game: {}", e.what()));
    }
    ++tick_;
    afterTick();
    if (o_.tick) o_.tick();
    return rig_.running();
}

void Bot::run() {
    while (step()) {
    }
    releaseMovement();
    if (end_ == BotEnd::Running) end_ = exitDone_ && stats_.climbsDone < o_.climbs ? BotEnd::ExitedMidClimb : BotEnd::Finished;
    say("end: {} ({} climbs)", end_ == BotEnd::Finished ? "finished" : "left mid-climb", stats_.climbsDone);
}

Bot::Mode Bot::modeOf() const {
    if (g_.titleRunning) return Mode::Title;
    if (uiFlashPending(g_)) return Mode::Flash;
    const uint8_t m = g_.core.uiMode;
    if (m & 0x08) return Mode::SignIn;
    if (m & 0x20) return Mode::Dialog;
    if (m & 0x40) return Mode::Menu;
    if (m & 0x02) return Mode::Quiz;
    if (m & 0x10) return Mode::Hint;
    if (m & 0x04) return Mode::Message;
    return Mode::Play;
}

void Bot::beforeTick() {
    if (tapped_) {
        rig_.keyUp(*tapped_);
        tapped_.reset();
    }
    switch (modeOf()) {
        case Mode::Title: title(); break;
        case Mode::Flash: releaseMovement(); break;
        case Mode::SignIn: signIn(); break;
        case Mode::Dialog: dialog(); break;
        case Mode::Menu: menu(); break;
        case Mode::Quiz: quiz(); break;
        case Mode::Hint: hint(); break;
        case Mode::Message: message(); break;
        case Mode::Play: play(); break;
        case Mode::None: break;
    }
}

void Bot::progress() { lastProgressTick_ = tick_; }

void Bot::afterTick() {
    ++stats_.ticks;
    const TickSnapshot now = snapshotOf(g_, rig_.users());
    checker_.onTick(g_, before_, now);
    const auto& b = before_;
    stats_.maxCoins = std::max<int>(stats_.maxCoins, now.coins);
    stats_.maxNets = std::max<int>(stats_.maxNets, now.nets);
    stats_.maxTier = std::max<int>(stats_.maxTier, now.tier);

    // The layout of the hiding places changed: a climb set-up (a new layout or a restored game).
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 48; ++i) {
        h = (h ^ (g_.world.cellContent[g_.tables.world.hidingCells[static_cast<size_t>(i)]] & 0x7F)) * 1099511628211ull;
        h = (h ^ (g_.world.cellPicture[g_.tables.world.hidingCells[static_cast<size_t>(i)]] & 0x7F)) * 1099511628211ull;
    }
    for (int l = 0; l < 3; ++l) h = (h ^ g_.world.levelPicData[static_cast<size_t>(l)][1]) * 1099511628211ull;
    // (pictures toggle only during a search, so the hash is only read outside of one; a game chosen at
    // the sign-in is a set-up even when its layout is the one already in memory: Start New Player and
    // back to the same saved game)
    const bool signedIn = (b.uiMode & 0x08) && !(now.uiMode & 0x08);
    if (g_.hasRec() && !(g_.core.uiMode & 0x08) && g_.player.searchTimer == 0 && (!haveLayout_ || h != layoutHash_ || signedIn)) {
        onClimbSetup(g_.core.keyState == 8 && g_.core.treasuresFound == 0);
        layoutHash_ = h;
        haveLayout_ = true;
    }

    // Saves.
    if (now.saves != b.saves && rig_.users().lastMoment()) {
        const DosSaveMoment m = *rig_.users().lastMoment();
        say("save: {}", dos::describe(m));
        checker_.onSave(g_, rig_.users(), dos::describe(m), gamesBefore_);
        if (g_.hasRec()) {
            // A game whose start message has not been shown yet (the player has not left the start
            // area: g_gameInProgress 0) is not saved (record +9 = 0): the next sign-in starts a new one.
            const bool saved = g_.rec().savedGame != 0;
            if (m == DosSaveMoment::ExitAndSave) leftRecord_ = g_.rec();
            if (m == DosSaveMoment::StartNewPlayer && saved) expectResume(g_.rec(), "Start New Player");
            if ((m == DosSaveMoment::StartNewPlayer || m == DosSaveMoment::ExitAndSave) && !saved) {
                say("left before the game started: no saved game");
                expectFresh_ = true;
            }
            if (m == DosSaveMoment::NewPlayer) {
                ++stats_.newPlayers;
            }
            if (m == DosSaveMoment::GameEnd) {
                ++stats_.climbsDone;
                stats_.finalTotal = static_cast<uint16_t>(g_.rec().totalTreasures);
                stats_.finalTier = g_.core.tier;
                say("climb {} done: total {}", stats_.climbsDone, stats_.finalTotal);
            }
        }
        progress();
    }
    if (g_.hasRec()) gamesBefore_ = g_.rec().gamesPlayed;

    // The castle top: the total grows by the treasures found.
    if (now.playerState == 41 && b.playerState != 41 && now.level == 3) {
        checker_.onCastleTop(g_, b.total, b.treasuresFound);
        say("castle top: {} treasures, total {}", b.treasuresFound, now.total);
        if (now.total >= 300) ++stats_.champion;
    }

    // Events of the tick.
    if ((now.playerState == 27 || now.playerState == 28) && b.playerState != 27 && b.playerState != 28) {
        ++stats_.drops;
        if (shopStep_ == 1) ++stats_.shopCoins;
    }
    if ((now.playerState == 31 || now.playerState == 32 || now.playerState == 30) && b.playerState != now.playerState &&
        !climbing_) {
        ++stats_.falls;
        say("falling (state {}) at {}", now.playerState, describe());
    }
    if (now.playerState == 33 && b.playerState != 33) ++stats_.ladderJumps;
    for (unsigned bridge : {cells::kBridgeL1, cells::kBridgeL2})
        if ((g_.world.cellPicture[bridge] & 0x7F) == 0x30 && (bridgePictures_[bridge == cells::kBridgeL1] & 0x7F) == 0x2F) {
            ++stats_.bridgeBreaks;
            say("the bridge {:02X} broke", bridge);
        }
    bridgePictures_ = {g_.world.cellPicture[cells::kBridgeL2], g_.world.cellPicture[cells::kBridgeL1]};
    if (now.playerState == 30 && b.playerState != 30 && cellOf(g_.player.obj.x, g_.player.obj.y) == cells::kGeyserTop) ++stats_.holeSlides;
    for (size_t i = 0; i < 15; ++i) {
        const uint16_t s0 = b.elfState[i], s1 = now.elfState[i];
        if ((s1 == 67 || s1 == 69) && s0 != 67 && s0 != 69) {
            ++stats_.catches;
            if (s1 == 67) ++stats_.carriers;
        }
        if ((s1 == 54 || s1 == 58) && s0 != 54 && s0 != 58) ++stats_.dustHits;
        if (s0 == 72 && s1 == 25) ++stats_.coinCreatures;
    }
    if ((b.bonusPicture & 0x7F) == 0x4B && (now.bonusPicture & 0x7F) == 0x49) {
        ++stats_.bonus;
        bonusTaken_ = true;
        say("bonus coins");
    }
    if (b.shopOwed == 0 && now.shopOwed == -1) ++stats_.shopVisits;
    if (b.area == 0x0D && now.area == 0x0D && b.coins == now.coins + 1 && (b.creatureCell || now.creatureCell)) ++stats_.creatureHits;
    if ((now.uiMode & 0x10) && !(b.uiMode & 0x10)) ++stats_.hints;
    if ((now.uiMode & 0x04) && !(b.uiMode & 0x04)) ++stats_.messages;
    if (searchCell_ >= 0 && b.playerState == 5 && now.playerState != 5) {
        const uint16_t c = g_.world.cellContent[static_cast<size_t>(searchCell_)];
        say("searched {:02X}: content {:#x}", searchCell_, c);
        searchCell_ = -1;
    }
    if (now.treasuresFound > b.treasuresFound && now.userRec == b.userRec && !(b.uiMode & 0x08)) ++stats_.treasures;
    if (now.keyState != b.keyState && (now.keyState == 2 || now.keyState == 4 || now.keyState == 6) && b.keyState >= 8 &&
        b.level == now.level)
        ++stats_.keys;
    if (now.level != b.level) say("level {} -> {} ({})", b.level, now.level, describe());
    if (g_.hasRec() && quizTierSeen_ >= 0 && g_.rec().quizTier != quizTierSeen_) {
        ++stats_.promotions;
        say("question tier {} -> {}", quizTierSeen_, g_.rec().quizTier);
    }
    quizTierSeen_ = g_.hasRec() ? g_.rec().quizTier : -1;

    // Progress: anything that moved.
    const auto& p = g_.player.obj;
    // (moving within 32 pixels, or a pose that alternates, is no progress)
    const uint16_t endPose = g_.player.playerState >= 34 ? g_.player.playerState : 0;
    const std::string key = std::format("{}.{:02X}.{}.{}.{}.{}.{}.{}.{}.{}.{}.{}.{}.{}.{}.{}.{}.{}", g_.core.level, cellOf(p.x, p.y),
                                        (p.x & 0x7F) >> 5, (p.y & 0x7F) >> 5, g_.core.uiMode, g_.core.coins, g_.core.nets,
                                        g_.core.treasuresFound, g_.core.keyState, endPose, g_.ui.titlePhase, g_.ui.signinState,
                                        g_.ui.quizAnswered, rig_.users().writes(), g_.core.endState, g_.player.rankRowsDone,
                                        g_.player.shackState, g_.player.shackX);
    if (key != progressKey_) {
        progressKey_ = key;
        progress();
    }
    if (tick_ - lastProgressTick_ > o_.stallTicks) abort(std::format("stall: no progress for {} game ticks", tick_ - lastProgressTick_));
    if (climbStartTick_ && tick_ - climbStartTick_ > o_.maxClimbTicks) abort("one climb took longer than the limit");
}

void Bot::onClimbSetup(bool fresh) {
    const bool resumed = !fresh;
    say("climb set-up: {} (tier {}, total {}, layout {})", resumed ? "resumed" : "new", g_.core.tier,
        g_.hasRec() ? g_.rec().totalTreasures : 0, g_.world.castleLayout);
    checker_.onClimbSetup(g_, fresh, tierBeforeSetup_);
    tierBeforeSetup_ = g_.core.tier;
    climbStartTick_ = tick_;
    bonusTaken_ = (g_.world.cellPicture[cells::kBonusSpot] & 0x7F) == 0x49;
    bonusTrip_ = false;
    tunnelsDone_ = false;
    clubStep_ = 0;
    shelfStep_ = 0;
    endSkip_ = chance(o_.skipEndRate);
    hazardWalk_ = chance(o_.hazardWalkRate);
    endWait_ = 0;
    shopStep_ = 0;
    doorCell_ = -1;
    menuTripsThisClimb_ = 0;
    nextMenuTripTick_ = stats_.playTicks + 300 + rng_() % 4000;
    chaseTicks_ = 0;
    climbing_ = false;
    if (o_.climbSetup) o_.climbSetup();
    if (expectFresh_) {
        if (!fresh) checker_.fail("a game left before it started was resumed");
        expectFresh_ = false;
        resumeExpected_.reset();
    }
    if (resumeExpected_) {
        if (resumeExpected_->savedGame == 0) {
            if (!fresh) checker_.fail(std::format("resume ({}): a record without a saved game was resumed", resumeHow_));
            resumeExpected_.reset();
            return;
        }
        checker_.compareResume(g_, *resumeExpected_, resumeHow_);
        ++stats_.resumes;
        say("resume checked ({})", resumeHow_);
        resumeExpected_.reset();
    }
}

// --------------------------------------------------------------------------- UI screens

void Bot::title() {
    releaseMovement();
    if (uiReady() && tick_ >= lastTapTick_ + 30) tap("enter");
}

void Bot::signIn() {
    releaseMovement();
    if (!uiReady()) return;
    const auto& ui = g_.ui;
    auto& users = rig_.users().users();
    const int ours = users.findName(o_.name);
    switch (ui.signinState) {
        case 1: {  // the list
            if (ours < 0) {
                // A letter starts a new name (the first one is upper-cased by the game).
                tap(std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(o_.name[0])))));
                say("sign-in: new player {}", o_.name);
                return;
            }
            int row = -1;
            for (int k = 0; k < ui.signinCount; ++k)
                if (ui.signinRecIdx[static_cast<size_t>(k)] == ours) row = k;
            if (row < 0) {
                abort("sign-in: our player is not on the list");
            }
            if (ui.signinRow != row) {
                tap(ui.signinRow < row ? "down" : "up");
                return;
            }
            say("sign-in: choosing {} (record {})", o_.name, ours);
            if (resumeExpected_) checker_.compareLoaded(rig_.users().record(ours), *resumeExpected_, resumeHow_);
            tap("enter");
            return;
        }
        case 3: {  // typing the name
            const int typed = std::max<int>(0, ui.newNameLen);
            if (typed < static_cast<int>(o_.name.size())) {
                tap(std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(o_.name[static_cast<size_t>(typed)])))));
            } else {
                tap("enter");
            }
            return;
        }
        case 0x11: tap("y"); return;  // continue the saved game
        case 0x21: tap("enter"); return;
        case 9:
        case 0x0D: tap("n"); return;
        case 5: tap("esc"); return;
        default: return;
    }
}

bool Bot::startPlan(Plan p) {
    if (plan_ != Plan::None) return false;
    plan_ = p;
    planStep_ = 0;
    planTick_ = tick_;
    return true;
}

void Bot::menu() {
    releaseMovement();
    if (!uiReady()) return;
    const auto& ui = g_.ui;
    if (plan_ == Plan::NewPlayer || plan_ == Plan::StartOver) {
        // File menu: Start Over is item 0, Start New Player item 1.
        const uint16_t item = plan_ == Plan::StartOver ? 0 : 1;
        if (ui.menuOpen != 1) {
            if (tick_ > planTick_ + 200) tap("esc");
            return;
        }
        tap(ui.menuItem < item ? "down" : ui.menuItem > item ? "up" : "enter");
        return;
    }
    if (plan_ == Plan::MenuTrip) {
        // planStep_: 0 the Help menu's first topic, 1 the pages (dialog), 2 the Options menu's Music box.
        if (planStep_ == 0) {
            if (ui.menuOpen != 0) {
                tap("f1");
            } else if (ui.menuItem != 0) {
                tap("up");
            } else {
                tap("enter");  // the first help topic
                planStep_ = 1;
            }
            return;
        }
        if (planStep_ == 2) {
            if (ui.menuOpen != 2) {
                tap("f3");
            } else if (ui.menuItem != 1) {
                tap("down");
            } else {
                tap("enter");  // the Music box
                planStep_ = 3;
            }
            return;
        }
        if (planStep_ >= 3 || tick_ > planTick_ + 3000) {
            tap("esc");
            return;
        }
        // Help closed with Done: the Help drop-down came back; go to the Options menu.
        planStep_ = 2;
        return;
    }
    if (plan_ == Plan::Exit && tick_ < planTick_ + 60) return;  // the queued Enter opens the box
    tap("esc");
}

void Bot::dialog() {
    releaseMovement();
    if (!uiReady()) return;
    const int id = g_.ui.dialogId;
    if (id <= 7) {  // the help pager: page through to Done
        if (plan_ == Plan::MenuTrip) {
            tap("enter");
        } else {
            ++stats_.unexpectedDialogs;
            tap("esc");
        }
        return;
    }
    ++stats_.dialogs;
    switch (id) {
        case 10:  // Exit and Save
            if (plan_ == Plan::Exit) {
                say("Exit and Save: Yes");
                exitDone_ = true;
                ++stats_.exits;
                tap("y");
            } else {
                tap("esc");
            }
            return;
        case 8:  // Start Over
            if (plan_ == Plan::StartOver) {
                say("Start Over: Yes");
                startOverDone_ = true;
                ++stats_.startOvers;
                plan_ = Plan::None;
                tap("y");
            } else {
                tap("esc");
            }
            return;
        case 9:  // Start New Player
            if (plan_ == Plan::NewPlayer) {
                say("Start New Player: Yes");
                newPlayerDone_ = true;
                plan_ = Plan::None;
                tap("y");
            } else {
                tap("esc");
            }
            return;
        case 11:
        case 12:
        case 13:  // the options' boxes: keep what is set (the default button is the current setting)
            tap(plan_ == Plan::MenuTrip ? "enter" : "esc");
            if (plan_ == Plan::MenuTrip) planStep_ = 4;
            return;
        default:
            ++stats_.unexpectedDialogs;
            tap("esc");
            return;
    }
}

void Bot::hint() {
    releaseMovement();
    if (uiReady() && tick_ >= lastTapTick_ + 20) tap("enter");
}

void Bot::message() {
    releaseMovement();
    if (uiReady() && tick_ >= lastTapTick_ + 30) tap("enter");
}

void Bot::quiz() {
    releaseMovement();
    const auto& ui = g_.ui;
    if (ui.quizClosed != 0) return;  // the question opens with the next frame
    const auto& perm = g_.tables.ui.answerPerms[static_cast<size_t>(std::min<uint16_t>(ui.quizOrder, 5))];
    auto enabled = [&](int row) { return (g_.tables.ui.answerBit[perm[static_cast<size_t>(row)]] & ui.quizMask) != 0; };
    if (!quizSeen_) {
        quizSeen_ = true;
        ++stats_.questions;
        quizEsc_ = chance(o_.escQuestionRate);
        const bool wrong = chance(o_.wrongAnswerRate);
        quizTarget_ = -1;
        for (int row = 0; row < 3; ++row)
            if (perm[static_cast<size_t>(row)] == 0) quizTarget_ = row;
        if (wrong)
            for (int row = 0; row < 3; ++row)
                if (perm[static_cast<size_t>(row)] != 0 && enabled(row)) quizTarget_ = row;
        say("question {}: answer row {}{}", ui.quizQuestion, quizTarget_, quizEsc_ ? " (Esc)" : "");
    }
    if (!uiReady()) return;
    if (ui.quizAnswered == 0) {
        if (quizEsc_) {
            ++stats_.escapes;
            tap("esc");
            quizEsc_ = false;
            return;
        }
        if (ui.quizFocus != quizTarget_) {
            tap("down");
            return;
        }
        if (perm[static_cast<size_t>(quizTarget_)] == 0) ++stats_.correct;
        else ++stats_.wrong;
        tap("enter");
        return;
    }
    if (tick_ >= lastTapTick_ + 40) tap("enter");  // close the answered question
}

// ------------------------------------------------------------------------------------ play

bool Bot::busyPlayerState() const {
    const uint16_t s = g_.player.playerState;
    return s == 5 || (s >= 17 && s <= 33) || s == 34 || s == 35 || s >= 41;
}

void Bot::play() {
    ++stats_.playTicks;
    if (o_.tickLog) say("play: {} keys {:#06x}", describe(), g_.keys());
    quizSeen_ = false;
    const auto& p = g_.player.obj;
    const unsigned cell = cellOf(p.x, p.y);
    const uint16_t level = g_.core.level;

    // Plans that start from play: Exit and Save, Start New Player, a menu trip.
    if (plan_ == Plan::None && g_.core.menuLock == 0 && g_.player.playerState == 10 && !climbing_ && level <= 2 &&
        g_.core.endState == 0 && shopStep_ == 0 && doorCell_ < 0) {
        if (stats_.climbsDone >= o_.climbs && !exitDone_) {
            say("plan: Exit and Save ({} climbs done)", stats_.climbsDone);
            startPlan(Plan::Exit);
        } else if (o_.exitAtTick >= 0 && !exitDone_ && static_cast<int64_t>(stats_.playTicks) >= o_.exitAtTick &&
            (!o_.exitInCastle || g_.core.area == 0x0D)) {
            say("plan: Exit and Save");
            startPlan(Plan::Exit);
        } else if (o_.startOverAtTick >= 0 && !startOverDone_ && static_cast<int64_t>(stats_.playTicks) >= o_.startOverAtTick) {
            say("plan: Start Over");
            startPlan(Plan::StartOver);
        } else if (o_.newPlayerAtTick >= 0 && !newPlayerDone_ && static_cast<int64_t>(stats_.playTicks) >= o_.newPlayerAtTick) {
            say("plan: Start New Player");
            startPlan(Plan::NewPlayer);
        } else if (o_.menuTrips && menuTripsThisClimb_ < 1 && stats_.playTicks >= nextMenuTripTick_) {
            say("plan: menu trip");
            ++menuTripsThisClimb_;
            startPlan(Plan::MenuTrip);
        }
    }
    if (plan_ == Plan::MenuTrip && planStep_ >= 1) {  // back in play: the trip is over
        say("menu trip done");
        ++stats_.menuTrips;
        plan_ = Plan::None;
    }
    if (plan_ != Plan::None) {
        releaseMovement();
        if (!uiReady()) return;
        if (tick_ > planTick_ + 600) {
            say("plan dropped (no menu)");
            plan_ = Plan::None;
            return;
        }
        switch (plan_) {
            case Plan::Exit: tap("esc"); return;
            case Plan::NewPlayer:
            case Plan::StartOver: tap("f2"); return;
            case Plan::MenuTrip: tap("f1"); return;
            default: return;
        }
    }

    if (level >= 3 || g_.core.endState != 0) {  // the end of a climb: castle top, chute, rooms, crown
        playEnd();
        return;
    }
    if (climbing_) {
        climbGate(climbLevel_);
        return;
    }
    if (shopStep_ >= 2) {
        netShop(shopLevel_);
        return;
    }
    if (doorCell_ >= 0) {
        // Up into a doorway until the player is somewhere else (a teleport), or give up.
        if (levelOfPathCell(cell) < 0 || tick_ > doorSince_ + 400) {
            doorCell_ = -1;
            releaseMovement();
        } else {
            holdOnly({Key::Up});
        }
        return;
    }
    const unsigned row = rowOf(cell), col = colOf(cell);
    if (g_.core.area == 0x0D || (level == 2 && inCastle(cell))) {
        playCastle();
        return;
    }
    if (level == 0 && ((row == 0 && col <= 5) || (row >= 3 && row <= 6 && col >= 8 && col <= 0xB))) {
        playClubhouse();
        return;
    }
    const int loopLevel = levelOfPathCell(cell);
    if (loopLevel >= 0 && onPathLine(p.y)) {
        playMountain(loopLevel);
        return;
    }
    playTransit();
}

// The mountain levels: searches, the gate, elves, the net shop, the bonus coins.
void Bot::playMountain(int level) {
    const auto& p = g_.player.obj;
    const uint16_t state = g_.player.playerState;
    if (level != chaseLevel_) {
        chaseLevel_ = level;
        chaseTicks_ = 0;
    }
    if (jumpTicks_ > 0) {
        if (--jumpTicks_ == 0) releaseMovement();
        return;
    }
    if (swingTicks_ > 0) {
        if (--swingTicks_ == 0) releaseMovement();
        return;
    }
    if (state == 9 || (state >= 13 && state <= 16)) {  // after a teleport, or looking up / down
        releaseMovement();
        return;
    }
    if (busyPlayerState()) {
        if (!(state >= 17 && state <= 20)) releaseMovement();
        return;
    }
    const auto pos = loopPos(level, p.x, p.y);
    if (!pos) {
        playTransit();
        return;
    }

    if (elfAction(level)) return;
    if (netShop(level)) return;

    if (level != lastLevel_) {
        lastLevel_ = level;
        levelTicks_ = 0;
        walkInto_ = -1;
    }
    ++levelTicks_;
    const bool keyFound = g_.core.keyState == 2 * (level + 1);
    // Out of coins for a long time with the key found: leave the rest of the treasures.
    const int need = keyFound && g_.core.coins == 0 && levelTicks_ > 8 * 60 * 47 ? 0 : remainingSearches(level);
    if (need == 0 && remainingSearches(level) > 0 && levelTicks_ == 8 * 60 * 47 + 1) say("leaving the treasures of level {}", level);
    // Short of coins: the bonus spot (level 0, once per climb), else hunting (elfAction) and wandering
    // until an elf or the coin creature comes along.
    if (need > 0 && g_.core.coins == 0) {
        if (level == 0 && !bonusTaken_ && !bonusTrip_) {
            bonusTrip_ = true;
            say("plan: the bonus coins");
        }
        if (bonusTrip_ && level == 0) {
            enterDoor(0, cells::kCaveDoorW, 0x20);  // Cell_P5_CaveDoorW: Up in the left half
            return;
        }
        if (tick_ >= wanderUntil_) {
            wanderDir_ = chance(0.5) ? 1 : -1;
            wanderUntil_ = tick_ + 300 + rng_() % 600;
        }
        walkTo(level, *pos + wanderDir_ * 512, 0);
        return;
    }
    // Tunnel exercises (once per climb): the mine tunnel from level 1, the ice cave from level 2.
    if (o_.tunnelTrips && !tunnelsDone_ && keyFound && need == 0) {
        const unsigned door = level == 1 ? 0xB2 : level == 2 ? 0x7C : 0;
        if (door) {
            enterDoor(level, door, 0x40);
            if (doorCell_ >= 0) {
                tunnelsDone_ = true;
                ++stats_.tunnels;
                say("tunnel trip from {:02X}", door);
            }
            return;
        }
    }
    if (auto target = need > 0 ? nextSearchTarget(level) : std::nullopt) {
        const int at = loopPosOf(level, static_cast<unsigned>(*target), 0x40);
        if (walkTo(level, at, kSearchTol)) {
            if (searchCell_ != *target) {
                searchCell_ = *target;
                searchAttempts_ = 0;
            }
            searchHere();
        }
        return;
    }
    if (keyFound) {
        climbGate(level);
        return;
    }
    // No key and nothing to search: cannot happen (the key is always a target until found).
    walkTo(level, *pos + 256, 0);
}

int Bot::remainingSearches(int level) const {
    int n = 0;
    for (int slot : slotsOf(level)) {
        const uint16_t c = g_.world.cellContent[g_.tables.world.hidingCells[static_cast<size_t>(slot)]];
        if (c & 0x80) continue;
        const uint16_t v = c & 0x7F;
        if (v == 0x66 + 2 * level || (v >= 6 && v <= 0x64)) ++n;
    }
    return n;
}

std::optional<int> Bot::nextSearchTarget(int level) const {
    const auto& p = g_.player.obj;
    const auto pos = loopPos(level, p.x, p.y);
    if (!pos) return std::nullopt;
    std::optional<int> best;
    int bestDist = 1 << 30;
    for (int slot : slotsOf(level)) {
        const unsigned cell = g_.tables.world.hidingCells[static_cast<size_t>(slot)];
        const uint16_t c = g_.world.cellContent[cell];
        if (c & 0x80) continue;
        const uint16_t v = c & 0x7F;
        if (!(v == 0x66 + 2 * level || (v >= 6 && v <= 0x64))) continue;
        const auto canon = canonicalCell(level, cell);
        if (!canon) continue;
        const int d = std::abs(loopDelta(level, *pos, loopPosOf(level, *canon, 0x40)));
        if (d < bestDist) {
            bestDist = d;
            best = static_cast<int>(*canon);
        }
    }
    return best;
}

bool Bot::walkTo(int level, int target, int tol) {
    const auto& p = g_.player.obj;
    const auto pos = loopPos(level, p.x, p.y);
    if (!pos) return false;
    const int d = loopDelta(level, *pos, target);
    if (std::abs(d) <= tol) {
        hold(Key::Left, false);
        hold(Key::Right, false);
        return true;
    }
    const int dir = d > 0 ? 1 : -1;
    // A hole or a bridge ahead (or underfoot): jump over it (two flips, 88 px).
    for (const Hazard& hz : hazards(g_, level)) {
        const int toStart = dir > 0 ? loopDelta(level, *pos, hz.from) : loopDelta(level, hz.to, *pos);
        const bool inside = loopDelta(level, hz.from, *pos) >= 0 && loopDelta(level, *pos, hz.to) >= 0;
        if (static_cast<int>(hz.cell) == walkInto_) continue;  // falling through it on purpose
        if ((toStart > 0 && toStart <= 16) || inside) {
            if (hazardWalk_ && !inside) {  // on purpose: through the hole / onto the breaking bridge
                hazardWalk_ = false;
                walkInto_ = static_cast<int>(hz.cell);
                say("walking into {:02X} on purpose", hz.cell);
                continue;
            }
            holdOnly({dir > 0 ? Key::Right : Key::Left, dir > 0 ? Key::PgUp : Key::Home});
            jumpTicks_ = kJumpHoldTicks;
            ++stats_.hazardJumps;
            say("jump over {:02X} ({})", hz.cell, dir > 0 ? "right" : "left");
            return false;
        }
    }
    holdOnly({dir > 0 ? Key::Right : Key::Left});
    return false;
}

bool Bot::searchHere() {
    const uint16_t state = g_.player.playerState;
    if (state != 10 || g_.player.slots[1].anim != 0) {
        releaseMovement();
        return false;  // the coin key needs the player standing idle
    }
    if (tick_ < searchTick_ + 25 || !uiReady()) return false;
    if (g_.core.coins == 0) return false;
    if (++searchAttempts_ > 6) {
        say("search at {:02X} keeps failing", searchCell_);
        searchAttempts_ = 0;
        walkTo(g_.core.level, 0, 0);
        return false;
    }
    searchTick_ = tick_;
    tap("down");
    return true;
}

bool Bot::elfAction(int level) {
    const int px = Vision::playerScreenX(g_);
    if (px < 0) return false;
    const auto seen = vision_.elves(g_);
    const int hidden = hiddenClueWords(g_, level);
    const bool shortOfCoins = g_.core.coins < remainingSearches(level) + 1;
    const SeenSprite* chase = nullptr;
    int chaseDist = 1 << 30;
    for (const SeenSprite& e : seen) {
        const int d = e.x - px;  // bytes; the net catches at 15..36 (right) / -38..-17 (left)
        if (e.kind == SpriteKind::CoinCreature) {
            if (g_.core.coins < 4 && std::abs(d) < chaseDist) {
                chase = &e;
                chaseDist = std::abs(d);
            }
            continue;
        }
        const bool carrier = e.kind == SpriteKind::Carrier;
        const bool want = (carrier && hidden > 0) || shortOfCoins || chance(o_.huntRate / 50.0);
        if (!want || g_.core.nets == 0) continue;
        if (between(d, 17, 34) || between(d, -36, -19)) {
            const Key dir = d > 0 ? Key::Right : Key::Left;
            holdOnly({dir, Key::Space});
            swingTicks_ = 6;
            ++stats_.swings;
            say("net {} at a {} ({} bytes)", d > 0 ? "right" : "left", kindName(e.kind), d);
            return true;
        }
        if ((carrier && hidden > 0) || shortOfCoins) {
            if (std::abs(d) < chaseDist) {
                chase = &e;
                chaseDist = std::abs(d);
            }
        }
    }
    if (chase && chaseTicks_ < kChaseLimit) {
        ++chaseTicks_;
        const auto& p = g_.player.obj;
        const auto pos = loopPos(level, p.x, p.y);
        if (!pos) return false;
        const int d = chase->x - px;
        // Walk so that it ends up in the net's reach (or under the player for the coin creature).
        const int want = chase->kind == SpriteKind::CoinCreature ? d : (d > 0 ? d - 25 : d + 27);
        walkTo(level, *pos + want * 2, 4);
        return true;
    }
    return false;
}

bool Bot::netShop(int level) {
    const int16_t owed = g_.core.shopCoinsOwed;
    if (shopStep_ == 0) {
        if (level > 1 || g_.core.nets > 0 || g_.core.coins < 4 + std::min(1, remainingSearches(level))) return false;
        shopStep_ = 1;
        shopLevel_ = level;
        say("plan: the net shop on level {}", level);
    }
    const unsigned shop = level == 0 ? cells::kNetShopL0 : cells::kNetShopL1;
    const int at = loopPosOf(level, shop, 0x40);
    if (at < 0) {
        shopStep_ = 0;
        return false;
    }
    if (shopStep_ == 1) {
        if (!walkTo(level, at, 12)) return true;
        if (owed > 0) {
            if (g_.core.coins == 0) {
                shopStep_ = 0;
                return false;
            }
            if (g_.player.playerState == 10 && g_.player.slots[1].anim == 0 && tick_ >= searchTick_ + 25 && uiReady()) {
                searchTick_ = tick_;
                tap("down");  // a coin dropped in front of the shop pays it (mask 0x200)
            }
            return true;
        }
        if (owed == 0) {
            shopStep_ = 2;
            climbing_ = false;
        }
    }
    if (shopStep_ == 2) {
        if (g_.core.shopCoinsOwed == -1) {
            shopStep_ = 3;
            holdOnly({Key::Down});
            return true;
        }
        holdOnly({Key::Up});
        return true;
    }
    if (shopStep_ == 3) {
        if (onPathLine(g_.player.obj.y) && g_.player.playerState == 10) {
            releaseMovement();
            shopStep_ = 0;
            say("net shop done: {} nets", g_.core.nets);
            return false;
        }
        holdOnly({Key::Down});
        return true;
    }
    return false;
}

void Bot::enterDoor(int level, unsigned cell, int offset) {
    if (!walkTo(level, loopPosOf(level, cell, offset), 8)) return;
    if (g_.player.playerState != 10) return;
    doorCell_ = static_cast<int>(cell);
    doorSince_ = tick_;
    holdOnly({Key::Up});
}

void Bot::climbGate(int level) {
    const auto& p = g_.player.obj;
    if (!climbing_) {
        if (level < 0 || level > 2) return;
        const unsigned gate = kGates[level];
        // EF: the trunk column (x & 0x78 within 0x18..0x50); AE: anywhere; 79: anywhere.
        const int at = loopPosOf(level, gate, 0x38);
        if (!walkTo(level, at, 8)) return;
        if (g_.player.playerState != 10) return;
        say("gate {:02X}: Up", gate);
        climbing_ = true;
        climbLevel_ = level;
        gateTick_ = tick_;
        holdOnly({Key::Up});
        return;
    }
    // Holding Up until the player arrives above (the trunk top BF, the geyser top 7E, the castle hall).
    const unsigned cell = cellOf(p.x, p.y);
    // The trunk ends on the trunk top BF (level 1), the geyser on the level-2 path (out of the hole,
    // flipping), the stone arch in the castle hall.
    const uint16_t state = g_.player.playerState;
    bool arrived = false;
    if (climbLevel_ == 0) arrived = g_.core.level == 1 && cell == cells::kTrunkTop && onPathLine(p.y) && state == 10;
    if (climbLevel_ == 1)
        arrived = g_.core.level == 2 && levelOfPathCell(cell) == 2 && onPathLine(p.y) && (state == 10 || (state >= 17 && state <= 20));
    if (climbLevel_ == 2) arrived = g_.core.area == 0x0D && inCastle(cell);
    if (arrived) {
        say("through the gate: {}", describe());
        ++stats_.gates;
        climbing_ = false;
        releaseMovement();
        return;
    }
    if (tick_ > gateTick_ + 1500) {
        say("gate climb gave up: {}", describe());
        climbing_ = false;
        releaseMovement();
        return;
    }
    holdOnly({Key::Up});
}

// The castle: the hall ladder, then the route up the wall.
void Bot::playCastle() {
    const auto& p = g_.player.obj;
    const uint16_t state = g_.player.playerState;
    ++stats_.castleTicks;
    const unsigned cell = cellOf(p.x, p.y);
    const int row = static_cast<int>(rowOf(cell)), col = static_cast<int>(colOf(cell));
    const CastleGrid grid = castleGrid(g_);
    if (state == 9 || state == 31 || state == 32 || state == 33 || (state >= 17 && state <= 20)) {
        releaseMovement();
        return;
    }
    if (row == CastleGrid::kHallRow && onPathLine(p.y) && (state == 10 || state == 11 || state == 12)) {
        // The hall floor: the ladder with the shortest route.
        int best = -1;
        size_t bestLen = 1000;
        for (int c : hallLadders(grid)) {
            const auto r = castleRoute(grid, CastleGrid::kHallRow, c);
            const size_t len = r.empty() ? 999 : r.size() + static_cast<size_t>(std::abs(c - col));
            if (len < bestLen) {
                bestLen = len;
                best = c;
            }
        }
        if (best < 0) abort("castle: no route from the hall");
        const int tx = best * kCellPx + 0x40;
        const int dx = tx - (p.x & 0x7FF);
        if (std::abs(dx) > 8) {
            holdOnly({dx > 0 ? Key::Right : Key::Left});
            return;
        }
        holdOnly({Key::Up});
        return;
    }
    if (state == 13 || state == 15 || state == 16 || state == 10) {
        const auto route = castleRoute(grid, row, col);
        if (route.empty()) {
            // Not on a ladder of a route (a ladder top that leads nowhere): climb down to the hall.
            holdOnly({Key::Down});
            return;
        }
        const CastleStep& s = route.front();
        switch (s.move) {
            case CastleMove::Up:
            case CastleMove::Exit: holdOnly({Key::Up}); return;
            case CastleMove::JumpLeft:
            case CastleMove::JumpRight: {
                // Jump from the lower part of the cell, where the landing ladder is (a short ladder
                // holds only the lower 32 rows).
                if ((p.y & 0x7F) >= 0x60) {
                    holdOnly({s.move == CastleMove::JumpLeft ? Key::Left : Key::Right});
                    say("castle: ladder jump {} at {:02X}", s.move == CastleMove::JumpLeft ? "left" : "right", cell);
                } else {
                    holdOnly({Key::Down});
                }
                return;
            }
        }
    }
    releaseMovement();
}

void Bot::playEnd() {
    releaseMovement();
    if (g_.core.level == 4 && (clubStep_ == 2 || clubStep_ == 4)) ++clubStep_;  // the clubhouse trip arrived
    const auto& p = g_.player.obj;
    const unsigned cell = cellOf(p.x, p.y);
    if (static_cast<int>(cell) != endCell_) {  // a new screen of the end: wait on it from its start
        endCell_ = static_cast<int>(cell);
        endWait_ = 0;
        if (cell == 0x1D) ++stats_.rankVisits;
        if (g_.core.level == 4 && cell >= 0x98 && cell <= 0x9B && g_.core.endState == 0) ++stats_.roomVisits;
    }
    if (!uiReady()) return;
    ++endWait_;
    if (g_.core.level == 3) {
        if (cell >= 0xA8 && cell <= 0xAA && endWait_ > 120 && g_.player.endSeqDone == 0 && endSkip_) {
            ++stats_.endSkips;
            endSkip_ = false;
            say("end sequence skipped");
            tap("enter");
        }
        return;
    }
    if (cell == 0x1D) {  // the rank screen: wait for the chest, then back to the clubhouse
        if (g_.player.rankRowsDone >= g_.player.rankRowsWanted && endWait_ > 150) {
            say("rank screen: Enter");
            endWait_ = 0;
            tap("enter");
        }
        return;
    }
    if (g_.core.endState == 0 && endWait_ > 250) {  // a treasure room visit from the clubhouse
        say("treasure room: Down");
        endWait_ = 0;
        tap("down");
    }
}

// The clubhouse: exercises, then the door.
void Bot::playClubhouse() {
    const auto& p = g_.player.obj;
    const uint16_t state = g_.player.playerState;
    endWait_ = 0;
    endCell_ = -1;
    if (state == 9 || busyPlayerState()) {
        releaseMovement();
        return;
    }
    const unsigned cell = cellOf(p.x, p.y);
    const int x = p.x;
    auto walkX = [&](int tx, int tol) {
        if (std::abs(tx - x) <= tol) {
            hold(Key::Left, false);
            hold(Key::Right, false);
            return true;
        }
        holdOnly({tx > x ? Key::Right : Key::Left});
        return false;
    };
    // The exercises, in order: 1 rank screen, 2 treasure room, 3 shelf rooms (walk right to the far
    // wall), 4 back from the shelf rooms.
    if (o_.clubhouseTrips && clubStep_ < 5) {
        if (clubStep_ == 0) clubStep_ = 1;
        if (clubStep_ == 1) {
            if (g_.core.level == 0 && cell == cells::kRankDoor && state == 10 && std::abs((x & 0x7F) - 0x40) <= 16) {
                holdOnly({Key::Up});
                clubTick_ = tick_;
                clubStep_ = 2;
                return;
            }
            walkX(cells::kRankDoor * kCellPx + 0x40, 8);
            return;
        }
        if (clubStep_ == 2) {
            if (tick_ > clubTick_ + 400) {
                releaseMovement();
                clubStep_ = 3;
            } else {
                holdOnly({Key::Up});
            }
            return;
        }
        if (clubStep_ == 3) {  // the treasure room: Up on the right of the start cell
            if (cell == cells::kStartRoom && (x & 0x7F) >= 0x68 && state == 10) {
                holdOnly({Key::Up});
                clubTick_ = tick_;
                clubStep_ = 4;
                return;
            }
            walkX(cells::kStartRoom * kCellPx + 0x70, 4);
            return;
        }
        if (clubStep_ == 4) {
            if (tick_ > clubTick_ + 400) {
                releaseMovement();
                clubStep_ = 5;
                shelfStep_ = 0;
            } else {
                holdOnly({Key::Up});
            }
            return;
        }
    }
    if (o_.clubhouseTrips && shelfStep_ < 3) {
        const bool inShelf = rowOf(cell) >= 3 && rowOf(cell) <= 6;
        if (shelfStep_ == 0) {  // walk right into the shelf rooms
            if (inShelf) {
                shelfStep_ = 1;
                ++stats_.shelfVisits;
                clubTick_ = tick_;
            } else {
                holdOnly({Key::Right});
                return;
            }
        }
        if (shelfStep_ == 1) {  // on through the four rooms to the far wall
            if (cell == 0x6A || tick_ > clubTick_ + 2500) {
                shelfStep_ = 2;
            } else {
                holdOnly({Key::Right});
                return;
            }
        }
        if (shelfStep_ == 2) {  // and back to the clubhouse
            if (!inShelf && rowOf(cell) == 0) {
                shelfStep_ = 3;
                releaseMovement();
            } else {
                holdOnly({Key::Left});
            }
            return;
        }
    }
    // The door: Up in the left part of the start cell.
    if (cell == cells::kStartRoom && between(x & 0x7F, 0x30, 0x50) && state == 10) {
        holdOnly({Key::Up});
        return;
    }
    if (state == 13) {
        holdOnly({Key::Up});
        return;
    }
    walkX(cells::kStartRoom * kCellPx + 0x40, 8);
}

// Caves, tunnels and shafts: on to the way out (or the bonus spot).
void Bot::playTransit() {
    const auto& p = g_.player.obj;
    const uint16_t state = g_.player.playerState;
    const unsigned cell = cellOf(p.x, p.y);
    const unsigned row = rowOf(cell), col = colOf(cell);
    // Up into a passage: the cave's way up to its upper room (0A) and back down (21), on the bonus trip.
    const bool upHere = bonusTrip_ && ((cell == 0x0A && !bonusTaken_) || (cell == 0x21 && bonusTaken_));
    if (upHere && (state == 10 || state == 13) && between(p.x & 0x7F, 0x28, 0x58)) {
        holdOnly({Key::Up});
        return;
    }
    // Standing above a path line, as the geyser hole's slide leaves the player in AE (y & 0x7F 0x60:
    // Cell_P39 gives no lift there, so the slide ends 16 px up): Down walks him down to the line.
    if ((state == 10 || state == 14) && !onPathLine(p.y) && levelOfPathCell(cell) >= 0) {
        holdOnly({Key::Down});
        return;
    }
    if (state == 9 || busyPlayerState() || !onPathLine(p.y) || state == 13 || state == 14) {
        releaseMovement();
        return;
    }
    const int x = p.x;
    auto walkX = [&](int tx, int tol) {
        if (std::abs(tx - x) <= tol) {
            hold(Key::Left, false);
            hold(Key::Right, false);
            return true;
        }
        holdOnly({tx > x ? Key::Right : Key::Left});
        return false;
    };
    if (row == 0 && col >= 8 && col <= 0xB) {  // the level-0 cave
        if (bonusTrip_ && !bonusTaken_) {
            if (walkX(0xA * kCellPx + 0x40, 8)) holdOnly({Key::Up});  // Cell_P34_CaveUp
            return;
        }
        bonusTrip_ = false;
        holdOnly({Key::Left});  // out through 08 to EB
        return;
    }
    if (row == 2 && col <= 7) {  // the cave's upper room
        if (!bonusTaken_) {
            holdOnly({Key::Right});  // over the bonus spot in 26 to the wall 27
            if (col == 7) bonusTaken_ = true;
            return;
        }
        bonusTrip_ = true;  // (also when the spot was empty) back down through 21
        if (walkX(0x1 * kCellPx + 0x40, 8)) holdOnly({Key::Up});  // Cell_P35_UpperRoomDown
        return;
    }
    if (row == 2 && col >= 8 && col <= 0xA) {  // the mine tunnel: out to the right (BB)
        holdOnly({Key::Right});
        return;
    }
    if (row == 2 && col >= 0xD) {  // the ice cave: right, into the slide
        holdOnly({Key::Right});
        return;
    }
    releaseMovement();
}

}  // namespace tmport::dos::autoplay
