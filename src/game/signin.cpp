#include "game/signin.h"

#include <algorithm>
#include <format>

#include "audio/audio_system.h"
#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/rules.h"
#include "platform/engine.h"
#include "res/game_files.h"
#include "save/settings.h"
#include "text/text_renderer.h"
#include "timing/deadline_timers.h"
#include "ui/game_dialog.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport::game {

using namespace signin;

// ----------------------------------------------------------------------------- pure helpers

namespace signin {

// 1160:0000 ULongToDec
std::string ulongToDec(uint32_t v) {
    if (v == 0) return "0";
    std::string s;
    for (; v != 0; v /= 10) s.push_back(static_cast<char>('0' + v % 10));
    std::reverse(s.begin(), s.end());
    return s;
}

// 1160:0cdb SignIn_DrawNumberRight (the string part): a jump table on the length inserts a comma
// before the last 6 and the last 3 digits; lengths outside 1..7 fall to the default case, which
// only terminates the (empty) output.
std::string formatTotal(uint32_t v) {
    const std::string d = ulongToDec(v);
    const size_t n = d.size();
    if (n < 1 || n > 7) return {};
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        out.push_back(d[i]);
        const size_t left = n - 1 - i;  // digits still to come
        if (left == 6 || left == 3) out.push_back(',');
    }
    return out;
}

int rowY(int row, int rowH) { return static_cast<uint8_t>(row * rowH + kListY); }

RankMark rankMark(int rank) {
    RankMark m;
    if (rank <= 0) {
        m.kind = RankMark::Trainee;
    } else if (rank < 7) {
        m.kind = RankMark::Stars;
        m.stars = rank;
    } else {
        m.kind = RankMark::Champion;
    }
    return m;
}

int rowFromMouseY(int mouseY, int rowH) {
    if (rowH == 0) return 0;
    const int16_t q = static_cast<int16_t>(static_cast<int16_t>(mouseY - kListY) / static_cast<int16_t>(rowH));
    return static_cast<int8_t>(q);  // MOV [6171], AL
}

int rowFromMouseYClamped(int mouseY, int rowH, int count) {
    int s = rowFromMouseY(mouseY, rowH);
    if (s < 0)
        s = 0;
    else if (s > count - 1)
        s = count - 1;
    return s;
}

}  // namespace signin

void NameBuffer::reset() {
    for (int i = 0; i < kMaxNameLength; ++i) bytes[static_cast<size_t>(i)] = '_';
    bytes[kMaxNameLength] = 0;
    cursor = 0;
}

std::string NameBuffer::string() const {
    std::string s;
    for (char c : bytes) {
        if (c == 0) break;
        s.push_back(c);
    }
    return s;
}

void NameBuffer::put(uint8_t c) {
    if (cursor < 0 || cursor >= static_cast<int>(bytes.size())) return;  // memory safety only
    bytes[static_cast<size_t>(cursor++)] = static_cast<char>(c);
}

bool NameBuffer::backspace() {
    if (cursor <= 0) return false;
    bytes[static_cast<size_t>(--cursor)] = '_';
    return true;
}

bool NameBuffer::append(unsigned c, const CType& ct) {
    if (cursor >= kMaxNameLength || c >= 0xFF || !ct.printable(c) || c == '_') return false;
    bytes[static_cast<size_t>(cursor++)] = static_cast<char>(c);
    return true;
}

void NameBuffer::trimForSubmit() {
    int p = cursor - 1;
    while (p >= 0 && bytes[static_cast<size_t>(p)] == ' ') bytes[static_cast<size_t>(p--)] = '_';
    ++p;
    bytes[static_cast<size_t>(p)] = 0;
    cursor = p;
}

void NameBuffer::restoreAfterDuplicate() {
    if (cursor < kMaxNameLength) bytes[static_cast<size_t>(cursor)] = '_';
}

int NameBuffer::caretIndex() const {
    int i = 0;
    while (bytes[static_cast<size_t>(i)] != '_' && i < kMaxNameLength) ++i;
    return i;
}

void CaretBlink::start(DeadlineTimers& timers) {
    colour_ = 0;
    timers.set(kCaretSlot, kCaretMs);
}

bool CaretBlink::update(bool expired, DeadlineTimers& timers) {
    if (!expired) return false;
    colour_ ^= 15;
    timers.set(kCaretSlot, kCaretMs);
    return true;
}

// ----------------------------------------------------------------------------- the screen

namespace {

// Game-state side effects of the rules called here: the audio is driven directly afterwards.
GameHooks& noHooks() { return nullHooks(); }

void copyName(PlayerState& p, const NameBuffer& n) {
    for (size_t i = 0; i < n.bytes.size(); ++i) p.name[i] = n.bytes[i];
}

}  // namespace

SignInScreen::SignInScreen(AppContext& ctx) : io_(ctx), ctype_(CType::fromExe(*ctx.files.exe)) {
    auto t = std::make_shared<GameTables>();
    t->rankThresholds = ctx.files.exe->array<uint32_t, kRankCount>(dg::kRankThresholds);  // DS:0ea0
    rankTables_ = std::move(t);
}

void SignInScreen::syncOptionsFromAudio() {
    GameState* st = io_.ctx().state;
    audio::AudioSystem* a = io_.audio();
    if (st && a) {
        st->options.soundOn = a->settings().soundOn;
        st->options.voiceOn = a->settings().voiceOn;
    }
}

// 1158:00bb Users_WriteFile: on failure the "Write Error" box (DS:2084); play goes on.
bool SignInScreen::writeUsers() {
    const save::Paths* paths = io_.ctx().paths;
    if (!paths) {
        // TODO(integration): AppContext::paths is set by the app; without it nothing is saved.
        log::warn("sign-in: no users.dat path, roster not written");
        return false;
    }
    if (save::writeUsersFile(paths->usersDat, table_)) return true;
    io_.ctx().dialogs.runModal(io_.dialog(dg::kDlgWriteError));
    return false;
}

void SignInScreen::saveRecord(const save::PlayerRecord& player, save::UsersTable* out) {
    const save::Paths* paths = io_.ctx().paths;
    save::UsersTable t;
    if (paths) {
        const save::SaveOutcome o = save::saveCurrentPlayer(paths->usersDat, player, userSel_, &t);
        if (!o.attempted) return;
        if (!o.written) io_.ctx().dialogs.runModal(io_.dialog(dg::kDlgWriteError));
    } else {
        if (!player.name.empty() && player.name[0] == save::kNoPlayerChar) return;
        log::warn("sign-in: no users.dat path, player not saved");
        save::mergePlayer(t, player, userSel_);
    }
    if (out) *out = t;
}

// 1160:0f28 Users_SaveCurrent
void SignInScreen::saveCurrent() {
    GameState* st = io_.ctx().state;
    if (!st || !st->player.signedIn()) return;
    syncOptionsFromAudio();
    saveRecord(toPlayerRecord(*st), nullptr);
}

// 1160:0dd6 Users_Load
void SignInScreen::loadUsers() {
    const save::Paths* paths = io_.ctx().paths;
    if (paths)
        save::readUsersFile(paths->usersDat, table_);  // missing / short file: empty roster
    else
        table_.clear();
    table_.recount();
}

// 1160:0e97 SignIn_LoadArt
void SignInScreen::loadArt() {
    const ResourceManager& res = *io_.ctx().files.res;
    const auto b = res.bundle(kArtBndl);
    if (!b || b->ids.size() < 4) throw Error(std::format("sign-in art BNDL {} not found", kArtBndl));
    auto get = [&](size_t i) {
        auto pm = res.pixmap(b->ids[i]);
        if (!pm) throw Error(std::format("sign-in art PMAP {} not found", b->ids[i]));
        return pm;
    };
    panel_ = get(0);
    rowNormal_ = get(1);
    rowHilite_ = get(2);
    bottom_ = get(3);
    rowW_ = rowNormal_->width;
    rowH_ = rowNormal_->height;
}

// 1160:0f1f SignIn_FreeArtAndFlush (1160:0f00 SignIn_FreeArt stops the scene music too)
void SignInScreen::freeArtAndFlush() {
    panel_.reset();
    rowNormal_.reset();
    rowHilite_.reset();
    bottom_.reset();
    if (auto* a = io_.audio()) a->music().stopScene();
    io_.flushInput();
}

// 1160:0e2d SignIn_DrawBackground
void SignInScreen::drawBackground() {
    gfx::FrameBuffer& fb = io_.engine().frame();
    io_.setCursorForced(true);
    fb.drawSprite(kPanelX, kPanelY, *panel_, false);
    fb.copyRect(0, 0, 512, 384, 0, gfx::FrameBuffer::kLayer1Y);
    io_.presentAll();
    // Display modes 3/4 call Fx_FadeIn_Stub(-2), which does nothing.
}

// 1160:0834 SignIn_ClearPanel
void SignInScreen::clearPanel() {
    gfx::FrameBuffer& fb = io_.engine().frame();
    io_.setCursorForced(true);
    fb.setForeColor(0);
    fb.fillRect(0, kBottomY, 512, kBottomH);
    io_.present(0, kBottomY, 512, kBottomH);
}

// 1160:0869 SignIn_DrawBottomPanel
void SignInScreen::drawBottomPanel() {
    gfx::FrameBuffer& fb = io_.engine().frame();
    fb.drawOpaqueOrFlippedWhiteKey(0, kBottomY, *bottom_, false);
    io_.present(0, kBottomY, 512, kBottomH);
}

// 1160:00ee SignIn_Run
SignInResult SignInScreen::run(bool restart) {
    Engine& eng = io_.engine();
    gfx::FrameBuffer& fb = eng.frame();
    audio::AudioSystem* audio = io_.audio();
    GameState* st = io_.ctx().state;
    result_.reset();

    table_ = save::UsersTable{};  // NewPtr(11081) + FarMemFill(0)
    if (st && st->player.signedIn()) {
        // A player is signed in ("Start Another Game"): save the game first; the merged table
        // is the roster shown.
        syncOptionsFromAudio();
        saveRecord(toPlayerRecord(*st), &table_);
        io_.trace(std::format("saved current player, sel {}", userSel_));
    } else {
        loadUsers();
    }
    io_.trace(std::format("roster: {} players", table_.count));
    loadArt();
    if (audio && !audio->music().isZonePlaying(kMusicZone)) audio->music().playZone(kMusicZone);
    fb.setFullClip();  // 10e0:0391 Gfx_ResetClip
    if (restart) {
        fb.setForeColor(0);
        fb.fillRect(0, 0, 512, 384);
        // g_gfxConst4 is 4: Fx_FadeOut_Stub(-2), which does nothing.
        auto pm = io_.ctx().files.res->pixmap(kBackdropPmap);
        if (!pm) throw Error(std::format("sign-in backdrop PMAP {} not found", kBackdropPmap));
        fb.drawOpaqueOrFlippedWhiteKey(0, 20, *pm, false);
        fb.copyRect(0, 0, 512, 384, 0, gfx::FrameBuffer::kLayer1Y);
    } else {
        fb.copyRect(0, gfx::FrameBuffer::kLayer1Y, 512, 384, 0, 0);  // whatever the back page holds
    }
    name_.reset();
    if (st) copyName(st->player, name_);  // g_playerName = "________": nobody signed in now
    drawBackground();
    while (name_.empty()) step();

    io_.setCursorForced(true);
    clearPanel();
    if (audio) audio->music().stopScene();
    freeArtAndFlush();
    fb.setForeColor(0);
    fb.fillRect(0, gfx::FrameBuffer::kLayer1Y, 512, 384);  // clear the back page
    // Fx_FadeOut_Stub(0x7ffe) returns 1: page 0 is cleared and NOT presented (the window keeps
    // the sign-in picture until the first room is drawn).
    fb.fillRect(0, 0, 512, 384);
    table_ = save::UsersTable{};  // DisposePtrDeep(g_users)

    if (!result_) throw Error("sign-in ended without a player");  // cannot happen
    if (st) copyName(st->player, name_);
    result_->name = name_.string();
    result_->userSel = userSel_;
    io_.trace(std::format("signed in: {} ({}), sel {}", result_->name, result_->newPlayer ? "new" : "existing", userSel_));
    return *result_;
}

// 1160:029c SignIn_Step
void SignInScreen::step() {
    gfx::FrameBuffer& fb = io_.engine().frame();
    io_.setCursorForced(true);
    fb.copyRect(kPanelX, kPanelY + gfx::FrameBuffer::kLayer1Y, kPanelW, kPanelH, kPanelX, kPanelY);
    io_.present(kPanelX, kPanelY, kPanelW, kPanelH);
    if (table_.count == 0) {
        enterNewName();
    } else {
        drawList();
        listLoop();
    }
}

// 1160:051b SignIn_DrawList
void SignInScreen::drawList() {
    HotspotTable& hot = io_.engine().hotspots();
    io_.setCursorForced(true);
    hot.clear();
    for (int i = 0; i < table_.count; ++i) drawRow(i, true);
    hot.set(kHotRows, kListX, kListY, rowW_, table_.count * rowH_);
    drawRow(table_.count, false);
    hot.set(kHotNewRow, kListX, kListY + table_.count * rowH_, rowW_, rowH_);
}

// 1160:0b1b SignIn_DrawRow (normal: bar 2181; otherwise the highlighted bar 2182)
void SignInScreen::drawRow(int row, bool normal) {
    gfx::FrameBuffer& fb = io_.engine().frame();
    text::TextRenderer& tx = io_.ctx().text;
    fb.setForeColor(0);
    const int y = rowY(row, rowH_);
    fb.drawSprite(kListX, y, normal ? *rowNormal_ : *rowHilite_, false);
    if (row == table_.count) {
        const int w = tx.width(io_.dgString(dg::kStrStartNewPlayer));
        tx.draw(kListX + (rowW_ - w) / 2, y + kNewRowTextDy, io_.dgString(dg::kStrStartNewPlayer2));
    } else if (row >= 0 && row < save::kSlotCount) {
        const save::UserSlot& s = table_.slots[static_cast<size_t>(row)];
        const std::string name = s.name();
        tx.draw(kNameX, y + kRowTextDy, bytesOf(name));
        const RankMark m = rankMark(rankFromTotal(s.totalTreasures(), *rankTables_));  // 10e8:07a6
        switch (m.kind) {
            case RankMark::Trainee: tx.draw(kRankX, y + kRowTextDy, io_.dgString(dg::kStrRankTrainee)); break;
            case RankMark::Stars:
                for (int i = 0; i < m.stars; ++i)
                    tx.draw(kRankX + kStarDx * i, y + kRowTextDy, io_.dgString(dg::kStrRankStar));
                break;
            case RankMark::Champion: tx.draw(kRankX, y + kRowTextDy, io_.dgString(dg::kStrRankChampion)); break;
        }
        drawNumberRight(rowW_ + kTotalRightPad, y + kRowTextDy, s.totalTreasures());
    }
    io_.present(kListX, kListY, rowW_, table_.count * rowH_ + rowH_);
}

// 1160:0cdb SignIn_DrawNumberRight
void SignInScreen::drawNumberRight(int rightX, int y, uint32_t v) {
    text::TextRenderer& tx = io_.ctx().text;
    const std::string s = formatTotal(v);
    tx.draw(rightX - tx.width(bytesOf(s)), y, bytesOf(s));
}

void SignInScreen::listFullBox(bool forceCursor) {
    clearPanel();
    io_.ctx().dialogs.runModal(io_.dialog(dg::kDlgListFull));
    if (forceCursor) io_.setCursorForced(true);
    drawBottomPanel();
}

// 1160:0595 SignIn_ListLoop (key table at 1160:07b8, 31 codes)
void SignInScreen::listLoop() {
    io_.setCursorForced(true);
    const int count = table_.count;
    userSel_ = count;
    int prev = userSel_;
    io_.flushInput();
    drawBottomPanel();
    bool done = false;
    while (!done) {
        if (userSel_ != prev) {
            drawRow(userSel_, false);
            prev = userSel_;
        }
        const uint16_t code = io_.waitCode();
        switch (code) {
            case 0x000D: case 0x04AC: case 0x24AC:  // Enter
                if (userSel_ == count) {
                    if (count >= kMaxPlayers) {
                        listFullBox(false);
                    } else {
                        userSel_ = count;
                        enterNewName();
                        io_.ctx().dialogs.restoreUnder();
                        done = true;
                    }
                } else {
                    loadSelected();
                    done = true;
                }
                break;
            case 0x0012: case 0x04A9: case 0x24A9:  // Ctrl+R
                if (userSel_ != count) {
                    removePlayer(userSel_);
                    done = true;
                }
                break;
            case 0x001B: case 0x04AF: case 0x24AF:  // Esc
                clearPanel();
                confirmExit();
                drawBottomPanel();
                break;
            case 0x0126: case 0x04AB: case 0x24AB:  // Up
                if (--userSel_ < 0) userSel_ = count;
                break;
            case 0x0128: case 0x04AA: case 0x24AA:  // Down
                if (++userSel_ == count + 1) userSel_ = 0;
                break;
            case kHotNewRow: case 0x2000 | kHotNewRow:  // release on "Start New Player"
                if (count >= kMaxPlayers) {
                    listFullBox(true);
                } else {
                    userSel_ = count;
                    enterNewName();
                    io_.setCursorForced(true);
                    io_.ctx().dialogs.restoreUnder();
                    done = true;
                }
                break;
            case kHotRows: case 0x2000 | kHotRows:  // release on a player row
                userSel_ = rowFromMouseY(io_.engine().input().mouseY(), rowH_);
                loadSelected();
                done = true;
                break;
            case 0x4000 | kHotNewRow: case 0x8000 | kHotNewRow:  // press on the new row
                userSel_ = count;
                break;
            case 0x4000 | kHotRows: case 0x8000 | kHotRows:  // press on a player row
                userSel_ = rowFromMouseYClamped(io_.engine().input().mouseY(), rowH_, count);
                break;
            case 0x44A9: case 0x44AA: case 0x44AB: case 0x44AC:
            case 0x84A9: case 0x84AA: case 0x84AB: case 0x84AC:
                break;
            default:
                // Type-to-create: a printable key other than '_' and space starts a new name.
                if (ctype_.printable(code) && code != '_' && code != ' ') {
                    if (count >= kMaxPlayers) {
                        listFullBox(false);
                    } else {
                        userSel_ = count;
                        name_.put(ctype_.toUpper(code));
                        enterNewName();
                        io_.ctx().dialogs.restoreUnder();
                        done = true;
                    }
                }
                break;
        }
        if (userSel_ != prev) {
            io_.setCursorForced(true);
            drawRow(prev, true);
        }
    }
    io_.engine().hotspots().clear();
}

// 1160:089f SignIn_RemovePlayer: the file is written whatever the answer.
void SignInScreen::removePlayer(int index) {
    clearPanel();
    ui::DlgDesc d = io_.dialog(dg::kDlgRemovePlayer);
    const std::string name = table_.slots[static_cast<size_t>(index)].name();
    d.body = ui::formatPercentS(io_.dgString(dg::kFmtRemoveConfirm), bytesOf(name));
    const int r = io_.ctx().dialogs.runModal(d);
    io_.trace(std::format("remove {} -> {}", name, r));
    if (r == 1) save::removeSlot(table_, index);
    writeUsers();
}

// 1160:04f6 SignIn_ConfirmExit: Yes ends the program (Game_ExitNow, longjmp 2).
void SignInScreen::confirmExit() {
    if (io_.ctx().dialogs.runModal(io_.dialog(dg::kDlgTitleExit)) == 1) {
        freeArtAndFlush();
        // g_playerName as it is now: the shutdown save (1118:03eb Users_SaveCurrent) sees it. With
        // an empty roster a partly typed name is still in it, and the original then saves it.
        if (GameState* st = io_.ctx().state) copyName(st->player, name_);
        io_.trace("exit confirmed");
        throw QuitGame{};
    }
    io_.setCursorForced(true);
}

// 1160:02e0 SignIn_EnterNewName (key table at 1160:046c, 7 codes)
void SignInScreen::enterNewName() {
    Engine& eng = io_.engine();
    HotspotTable& hot = eng.hotspots();
    io_.setCursorForced(true);
    nameDone_ = false;
    clearPanel();
    hot.clear();
    hot.set(kHotEnter, 198, 155, 88, 19);   // body line "Then press <Enter>": acts as Enter
    hot.set(kHotEsc, 41, 253, 424, 23);     // footer line "<Esc> to Go Back": acts as Esc
    io_.flushInput();
    caret_.start(eng.timers());
    io_.ctx().dialogs.draw(io_.dialog(dg::kDlgEnterName), true);
    drawNameBox();
    drawCaret(caret_.colour());
    io_.trace("name entry");
    bool done = false;
    while (!done) {
        const uint16_t code = io_.getCode();  // non-blocking
        if (caret_.update(io_.timerExpired(kCaretSlot), eng.timers())) drawCaret(caret_.colour());
        switch (code) {
            case 0x001B: case kHotEsc: case 0x2000 | kHotEsc:  // cancel
                if (table_.slots[0].empty()) {  // empty roster: Esc asks to exit the program
                    io_.setCursorForced(true);
                    io_.ctx().dialogs.restoreUnder();
                    confirmExit();
                }
                name_.reset();
                done = true;
                break;
            case 0x4000 | kHotEsc: case 0x4000 | kHotEnter: case 0x8000 | kHotEsc: case 0x8000 | kHotEnter:
                break;  // presses ignored
            default:
                if (code != 0 && code != '_' && !(code == ' ' && name_.empty())) {
                    if (ctype_.printable(code) && name_.empty())
                        name_.bytes[0] = static_cast<char>(ctype_.toUpper(code));  // overwritten by NameKey
                    nameKey(code);
                    if (nameDone_) done = true;
                }
                break;
        }
    }
    io_.setCursorForced(true);
    hot.remove(kHotEnter);
    hot.remove(kHotEsc);
    io_.ctx().dialogs.restoreUnder();
    drawBottomPanel();
}

// 1160:0957 SignIn_NameKey
void SignInScreen::nameKey(uint16_t code) {
    io_.setCursorForced(true);
    drawNameBox();
    if (code == 0x08) {
        if (name_.backspace()) drawNameBox();
    } else if (code == 0x0D || code == kHotEnter) {
        if (name_.empty()) return;
        name_.trimForSubmit();
        const std::string name = name_.string();
        int i = 0;
        while (i < table_.count && table_.slots[static_cast<size_t>(i)].name() != name) ++i;  // FarStrCmp
        if (i == table_.count) {
            initNewPlayer();
        } else {
            io_.ctx().dialogs.restoreUnder();
            ui::DlgDesc d = io_.dialog(dg::kDlgAlreadyOnList);
            d.body = ui::formatPercentS(io_.dgString(dg::kFmtAlreadyOnList), bytesOf(name));
            io_.trace(std::format("duplicate {}", name));
            io_.ctx().dialogs.runModal(d);
            io_.ctx().dialogs.draw(io_.dialog(dg::kDlgEnterName), true);
            name_.restoreAfterDuplicate();
            drawNameBox();
        }
    } else if (name_.append(code, ctype_)) {
        drawNameBox();
    }
}

// 1160:0ab0 SignIn_DrawNameBox
void SignInScreen::drawNameBox() {
    gfx::FrameBuffer& fb = io_.engine().frame();
    fb.setForeColor(15);
    fb.fillRect(kBoxX, kBoxY, kBoxW, kBoxH);
    fb.setForeColor(0);
    fb.frameRect(kBoxX, kBoxY, kBoxW, kBoxH);  // WinG bottom-edge quirk inside frameRect
    name_.upperFirst(ctype_);
    io_.ctx().text.draw(kBoxTextX, kBoxTextY, bytesOf(name_.string()));  // the '_' cells are drawn too
    io_.present(kBoxX, kBoxY, kBoxW, kBoxH);
}

// 1160:0488 SignIn_DrawCaret: a 8x10 block after the typed characters.
void SignInScreen::drawCaret(int colour) {
    gfx::FrameBuffer& fb = io_.engine().frame();
    const int n = name_.caretIndex();
    name_.bytes[static_cast<size_t>(n)] = 0;
    fb.setForeColor(colour);
    const int x = kCaretX + io_.ctx().text.width(bytesOf(name_.string()));
    fb.fillRect(x, kCaretY, kCaretW, kCaretH);
    io_.present(x, kCaretY, kCaretW, kCaretH);
    if (n < kMaxNameLength) name_.bytes[static_cast<size_t>(n)] = '_';
}

// 1160:137d Users_LoadSelected
void SignInScreen::loadSelected() {
    const int sel = std::clamp(userSel_, 0, save::kSlotCount - 1);
    const save::UserSlot& slot = table_.slots[static_cast<size_t>(sel)];
    // strcpy(g_playerName, rec.name): bytes after the NUL keep their values.
    for (size_t i = 0; i < save::kNameSize; ++i) {
        name_.bytes[i] = static_cast<char>(slot.bytes[i]);
        if (slot.bytes[i] == 0) break;
    }
    SignInResult r;
    r.newPlayer = false;
    r.slot = slot;
    r.record = save::PlayerRecord::fromSlot(slot);
    GameState* st = io_.ctx().state;
    audio::AudioSystem* a = io_.audio();
    if (st) {
        syncOptionsFromAudio();
        copyName(st->player, name_);
        loadPlayerRecord(*st, slot.bytes, noHooks());  // field copy + the sound quirk on the state
    }
    if (a) {
        // Sign-in can switch Music and Sound off but never on; Voice is copied (1160:13ce..1415).
        a->applySignInSettings(slot.u8(save::off::kSoundOn), slot.u8(save::off::kVoiceOn));
        if (st) syncOptionsFromAudio();
    }
    if (st) copyName(st->player, name_);
    result_ = r;
    io_.trace(std::format("load slot {}", sel));
}

// 1160:1632 Users_InitNewPlayer
void SignInScreen::initNewPlayer() {
    GameState* st = io_.ctx().state;
    audio::AudioSystem* a = io_.audio();
    SignInResult r;
    r.newPlayer = true;
    if (st) {
        syncOptionsFromAudio();
        game::initNewPlayer(*st, noHooks());
        copyName(st->player, name_);
    }
    if (a) {
        a->onNewPlayer();  // Music and Sound on for a new player (Audio_ToggleSound)
        if (st) syncOptionsFromAudio();
    }
    if (st) {
        r.record = toPlayerRecord(*st);
    } else {
        r.record = save::PlayerRecord{};  // new-player defaults
        r.record.name = name_.string();
        if (a) {
            r.record.soundOn = a->settings().soundOn;
            r.record.voiceOn = a->settings().voiceOn;
        }
    }
    nameDone_ = true;
    result_ = r;
    io_.trace("new player");
}

SignInResult runSignIn(AppContext& ctx, bool restart) {
    SignInScreen s(ctx);
    return s.run(restart);
}

}  // namespace tmport::game
