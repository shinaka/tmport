#include "gg/ui/signin.h"

#include <algorithm>
#include <array>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/text/text.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr int kListX = 62, kListY = 46, kListW = 180, kRowH = 11;
constexpr int kNameX = 64;

// The C string at byte `from` of an index entry's 22 bytes (the name, or past it as the original's
// pointer arithmetic reads).
std::vector<uint8_t> entryString(const RosterEntry& e, int from) {
    std::array<uint8_t, kRosterEntrySize> raw{};
    e.toBytes(raw);
    std::vector<uint8_t> s;
    for (size_t i = static_cast<size_t>(std::max(from, 0)); i < raw.size() && raw[i] != 0; ++i) s.push_back(raw[i]);
    return s;
}

SignInVars varsOf(Roster& roster, int16_t& index, int16_t& typedLen, std::array<uint8_t, 16>& typed, uint8_t& typed16,
                  int16_t& clicked, uint32_t& lastClick, uint32_t& prevClick, int16_t& arrowRepeat, int16_t& arrowCount,
                  ScrollBar& bar) {
    SignInVars v;
    v.roster = &roster;
    v.index = &index;
    v.typedLen = &typedLen;
    v.typed = typed.data();
    v.typed16 = &typed16;
    v.clicked = &clicked;
    v.lastClick = &lastClick;
    v.prevClick = &prevClick;
    v.arrowRepeat = &arrowRepeat;
    v.arrowCount = &arrowCount;
    v.bar = &bar;
    return v;
}

}  // namespace

// 2caa:016e SignIn_DrawScreen
PaletteLoad signInDrawScreen(Video& video, const Archive& archive) {
    std::vector<uint8_t> picture;
    PaletteLoad load = video.loadPicture(archive, kSignInPicture, kSignInPalette, picture);  // Screen_LoadPicture
    Screen& s = video.screen();
    s.lpxDraw(0, 0, picture);
    s.lpxDrawKeyed(65, 28, archive.read(kSignInTitle), 0);  // Res_Read
    s.saveAll();                                            // Screen_SaveAll
    return load;
}

SignIn::SignIn(const GgExe& exe, const Archive& archive)
    : cancel_(PicButton::fromRecord(exe, dg::kBtnCancel)),
      newPlayer_(PicButton::fromRecord(exe, dg::kBtnNewPlayer)),
      startGame_(PicButton::fromRecord(exe, dg::kBtnStartGame)),
      barPics_(ScrollBarPictures::load(archive)),
      listZone_(Zone::fromRecord(exe.dsBytes(dg::kRosterZone, Zone::kRecordSize))),
      barZone_(Zone::fromRecord(exe.dsBytes(dg::kRosterScrollZone, Zone::kRecordSize))),
      ranks_(RankTable::fromExe(exe)) {
    own_.bar = ScrollBar::fromRecord(exe, dg::kRosterScrollBar);
    v_ = varsOf(own_.roster, own_.index, own_.typedLen, own_.typed, own_.typed16, own_.clicked, own_.lastClick,
                own_.prevClick, own_.arrowRepeat, own_.arrowCount, own_.bar);
    // 2caa:1382 SignIn_LoadButtons
    picBtnLoad(archive, cancel_, 250, 172);
    picBtnLoad(archive, newPlayer_, 197, 170);
    picBtnLoad(archive, startGame_, 146, 170);
}

void SignIn::bind(const SignInVars& vars) {
    if (!vars.complete()) {
        log::error("SignIn::bind: a variable is missing; the screen keeps its own");
        return;
    }
    v_ = vars;
}

std::span<const uint8_t> SignIn::typedBytes() const {
    return std::span<const uint8_t>(v_.typed, 16).first(static_cast<size_t>(std::clamp<int>(*v_.typedLen, 0, 16)));
}

int SignIn::typedWidth(Fonts& fonts) const {
    std::array<uint8_t, 17> buf{};
    std::copy_n(v_.typed, 16, buf.begin());
    buf[16] = *v_.typed16;
    return textWidth(fonts, buf);  // Text_Width up to the NUL
}

// 2caa:1240 State_02_SignIn, the part before SignIn_Run
void SignIn::begin(const Roster& roster, uint32_t now) {
    if (&roster != v_.roster) R() = roster;
    cancel_.enabled = R().count == 0;
    newPlayer_.enabled = R().count > 0;
    startGame_.enabled = newPlayer_.enabled;
    *v_.clicked = 0;
    idx() = 0;
    tlen() = -1;
    bar().max = std::max(R().count - 8, 0);
    bar().nextRepeat = now;
    bar().pos = 0;
}

// 2caa:0eb3 SignIn_Run, its entry: the drawing and the zones
void SignIn::show(const UiCanvas& ui, ClickZones* zones) {
    ui.screen.saveAll();
    drawList(ui, false, -1, -1);
    picBtnDraw(ui, cancel_);
    picBtnDraw(ui, newPlayer_);
    picBtnDraw(ui, startGame_);
    scrollBarDraw(ui, barPics_, bar());
    if (zones) {
        zones->addLocal(listZone_);
        btnAddZone(*zones, cancel_.zone);
        btnAddZone(*zones, newPlayer_.zone);
        btnAddZone(*zones, startGame_.zone);
        zones->addLocal(barZone_);
    }
    ui.dirtyFlush();
}

// 2caa:0f4d SignIn_Run, its entry after the input flush
void SignIn::start(const UiCanvas& ui, SignInHost& host) {
    if (tlen() == -1) {
        tlen() = 0;
        if (R().count == 0) handleCode(ui, host, kCodeNewPlayer);
    }
    *v_.lastClick = 0;
}

void SignIn::leave(ClickZones& zones) {
    // 2caa:11d3: the buttons, then the bar and the list
    btnRemoveZone(zones, startGame_.zone);
    btnRemoveZone(zones, newPlayer_.zone);
    btnRemoveZone(zones, cancel_.zone);
    zones.removeLocal(&barZone_);
    zones.removeLocal(&listZone_);
}

// 2caa:0000 SignIn_HideButtons
void SignIn::hideButtons(const UiCanvas& ui) {
    picBtnErase(ui, cancel_);
    picBtnErase(ui, newPlayer_);
    picBtnErase(ui, startGame_);
}

// 2caa:0025 SignIn_ShowButtons
void SignIn::showButtons(const UiCanvas& ui) {
    picBtnDraw(ui, cancel_);
    picBtnDraw(ui, newPlayer_);
    picBtnDraw(ui, startGame_);
    ui.dirtyFlush();
}

// 2caa:04ae SignIn_DrawScoreRank(points, x, y)
void SignIn::drawScoreRank(const UiCanvas& ui, uint32_t points, int x, int y) {
    if (static_cast<int32_t>(points) < 0) return;
    const std::string s = formatThousands(points);  // Str_FormatThousands
    const auto bytes = text::asBytes(s);
    const int w = textWidth(ui.fonts, bytes);
    ui.fonts.drawString(ui.screen, x + 100 - w, y + 1, bytes);
    ui.fonts.drawString(ui.screen, x + 0x69, y + 1, text::asBytes(ranks_.nameOf(points)));  // Rank_FromPoints
}

// 2caa:053a SignIn_DrawList, one row
void SignIn::drawRow(const UiCanvas& ui, int k, int e) {
    Screen& s = ui.screen;
    Fonts& f = ui.fonts;
    const int rowY = kListY + kRowH * k;
    const RosterEntry& en = R().entries[static_cast<size_t>(e)];
    const auto name = entryString(en, 0);
    if (e == idx()) {
        s.setDrawColor(0);
        s.fillRect(kListX, rowY, kListW, kRowH);
        if (tlen() > 0) {
            std::vector<uint8_t> buf = name;  // strcpy, then cut at the typed length
            buf.resize(std::min(buf.size(), static_cast<size_t>(tlen())));
            if (buf.size() == static_cast<size_t>(tlen()) && buf.back() == 0x1F) buf.pop_back();
            f.setColor(14);
            f.drawString(s, kNameX, rowY + 1, buf);
            f.setColor(11);
            f.drawString(s, kNameX + textWidth(f, buf), rowY + 1, entryString(en, tlen()));
        } else {
            f.setColor(11);
            f.drawString(s, kNameX, rowY + 1, name);
        }
    } else {
        s.restoreRect(kListX, rowY, kListW, kRowH);
        f.setColor(15);
        f.drawString(s, kNameX, rowY + 1, name);
    }
    drawScoreRank(ui, en.points, kNameX, rowY);
}

// 2caa:053a SignIn_DrawList
void SignIn::drawList(const UiCanvas& ui, bool flush, int a, int b) {
    int top = bar().pos;
    const int rows = std::min<int>(R().count, kRosterRows);
    if (b >= 0 && rows != 0) {
        if (idx() < top) {
            top = idx();
            scrollBarSetPos(ui, barPics_, bar(), idx());
            b = -1;
        }
        if (top + 7 <= idx()) {
            top = idx() - 7;
            scrollBarSetPos(ui, barPics_, bar(), top);
            b = -1;
        }
    }
    for (int k = 0; k < rows; ++k) {
        const int e = top + k;
        if (b >= 0 && e != a && e != b) continue;
        if (e < 0 || e >= kRosterEntryCount) continue;  // the original reads outside the index
        drawRow(ui, k, e);
        if (flush) ui.dirtyFlush();
    }
    if ((b == -1 || b > rows) && rows < kRosterRows) {
        ui.screen.restoreRect(kListX, kListY + kRowH * rows, kListW, kRowH);
        if (flush) ui.dirtyFlush();
    }
    const bool valid = indexValid();
    const int32_t p = valid ? points(entry(idx())) : 0;
    const bool start = valid && !(p == -2 && tlen() <= 0);
    if (start != startGame_.enabled) {
        startGame_.enabled = start;
        picBtnDraw(ui, startGame_);
        ui.dirtyFlush();
    }
    const bool newp = !(valid && p < 0);
    if (newp != newPlayer_.enabled) {
        newPlayer_.enabled = newp;
        picBtnDraw(ui, newPlayer_);
        ui.dirtyFlush();
    }
    const bool cancel = rows != 0 && !newPlayer_.enabled;
    if (cancel != cancel_.enabled) {
        cancel_.enabled = cancel;
        picBtnDraw(ui, cancel_);
        ui.dirtyFlush();
    }
}

// 2caa:01c5 Roster_UpdateTyped
bool SignIn::updateTyped(const UiCanvas& ui, SignInHost& host, uint8_t c) {
    const int old = R().count;
    if (R().count > 0 && indexValid() && points(entry(idx())) < 0) {
        // an entry is in progress: remove it (New Player's keeps its marker while characters remain)
        if (tlen() > 0 && points(entry(idx())) == -2) {
            typedAt(tlen()++) = 0x1F;
            c = 0x1F;
        }
        const uint16_t slot = entry(idx()).slot;
        --R().count;
        for (int i = idx(); i < R().count; ++i) entry(i) = entry(i + 1);
        entry(R().count).slot = slot;  // its slot is free again, next in line
        if (R().count <= idx()) idx() = static_cast<int16_t>(R().count - 1);
    }
    if (tlen() <= 0) return R().count != old;
    int e = 0;
    for (; e < R().count; ++e) {
        RosterEntry& en = entry(e);
        uint8_t a = 0, b = 0;
        for (int i = 0; i < tlen(); ++i) {
            a = typedAt(i);
            b = i < static_cast<int>(kPlayerNameSize) ? en.name[static_cast<size_t>(i)] : 0;
            if (b == 0 && points(en) < 0 && i + 1 < static_cast<int>(kPlayerNameSize)) {
                // an entry in progress with a shorter name gets the typed byte (never happens)
                b = a;
                en.name[static_cast<size_t>(i)] = a;
                en.name[static_cast<size_t>(i + 1)] = 0;
            }
            if (a != b) break;
        }
        if (b == 0 && a == 0x1F) break;  // insert here
        if (a == b) {                    // a prefix of this name: select it
            idx() = static_cast<int16_t>(e);
            return R().count != old;
        }
        if (static_cast<int8_t>(b) > static_cast<int8_t>(a)) break;  // signed char compare
    }
    // insert at e
    if (e < R().count && points(entry(e)) < 0) return false;
    if (R().count >= kRosterMaxPlayers) {
        hideButtons(ui);
        host.messageRes(0x1069);  // the list is full
        showButtons(ui);
        --tlen();
        return false;
    }
    const uint16_t slot = entry(R().count).slot;
    for (int i = R().count; i > e; --i) entry(i) = entry(i - 1);
    RosterEntry& ne = entry(e);
    ne.slot = slot;
    ne.points = c == 0x1F ? 0xFFFFFFFEu : 0xFFFFFFFFu;
    typedAt(tlen()) = 0;
    while (tlen() > 0 && static_cast<int8_t>(typedAt(tlen() - 1)) < 0x20) {
        --tlen();
        typedAt(tlen()) = 0;  // strips the 0x1F marker
    }
    ne.name.fill(0);
    for (int i = 0; i < tlen() && i < static_cast<int>(kPlayerNameSize) - 1; ++i)
        ne.name[static_cast<size_t>(i)] = typedAt(i);  // strcpy
    ++R().count;
    idx() = static_cast<int16_t>(e);
    return R().count != old;
}

// The rescroll of the typing and Backspace paths: the selection kept within rows 0..6 of the view.
void SignIn::keepIndexVisible(const UiCanvas& ui, int& redrawFrom) {
    const int top = bar().pos;
    if (idx() < top) {
        redrawFrom = -1;
        scrollBarSetPos(ui, barPics_, bar(), idx());
    } else if (top + 6 <= idx()) {
        scrollBarSetPos(ui, barPics_, bar(), idx() - 6);
    }
}

// 2caa:082e SignIn_HandleCode
int SignIn::handleCode(const UiCanvas& ui, SignInHost& host, uint16_t code) {
    const bool up = code == 0x148, down = code == 0x150;
    int16_t& repeat = *v_.arrowRepeat;  // the arrow throttle DS:a065 / DS:a067: it never throttles
    if (up || down) repeat = 0;
    if (code == 0x12) {  // Ctrl+R: remove the selected player
        if (indexValid() && points(entry(idx())) >= 0) {
            hideButtons(ui);
            if (host.yesNoRes(0x106A) == 1) {
                entry(idx()).points = 0xFFFFFFFFu;
                tlen() = 0;
                updateTyped(ui, host, 8);
                scrollBarSetRange(ui, barPics_, bar(), R().count - 8);
                drawList(ui, true, idx(), -1);
                host.saveRoster(R());  // the index is written at once; the record stays
            }
            showButtons(ui);
        }
        return 0;
    }
    const uint16_t m = code & 0x0FFF;
    if (m == cancel_.zone.releaseCode) {  // DS:2102: drop the entry in progress
        tlen() = 0;
        updateTyped(ui, host, 8);
        scrollBarSetRange(ui, barPics_, bar(), R().count - 8);
        drawList(ui, true, idx(), -1);
        return 0;
    }
    if (m == newPlayer_.zone.releaseCode) {  // DS:2122
        if (R().count >= kRosterMaxPlayers) {
            hideButtons(ui);
            host.messageRes(0x1069);
            showButtons(ui);
            return 0;
        }
        int prev = idx();
        typedAt(tlen()++) = 0x1F;
        if (updateTyped(ui, host, 0x1F)) {
            prev = -1;
            scrollBarSetRange(ui, barPics_, bar(), R().count - 8);
            const int top = bar().pos;
            if (idx() < top) scrollBarSetPos(ui, barPics_, bar(), idx());
            else if (top + 7 <= idx()) scrollBarSetPos(ui, barPics_, bar(), idx() - 6);
        }
        drawList(ui, true, idx(), prev);
        return 0;
    }
    const int rows = std::min<int>(R().count, kRosterRows);
    const int mx = host.mouseX(), my = host.mouseY();
    if (m == kCodeRosterList && mx >= 0x3A && mx < 0xF2 && my >= kListY && kListY + kRowH * rows > my) {
        if (indexValid() && points(entry(idx())) < 0) return 0;  // an entry in progress keeps the selection
        const int row = (my - kListY) / kRowH + bar().pos;
        if (row < 0 || row >= R().count) {
            *v_.clicked = 0;
            return 0;
        }
        const int old = idx();
        idx() = static_cast<int16_t>(row);
        *v_.lastClick = host.ticks();  // the double-click stamp (never read)
        if (old != idx()) {
            *v_.prevClick = 0;
            *v_.lastClick = 0;
            tlen() = 0;
            drawList(ui, true, idx(), old);
        }
        *v_.clicked = 1;
        return 0;
    }
    *v_.clicked = 0;
    if (up) {
        *v_.arrowCount = 0;
        repeat = 2;
        if (idx() > 0 && R().count > idx() && points(entry(idx())) >= 0) {
            tlen() = 0;
            const int old = idx()--;
            drawList(ui, true, idx(), old);
        }
        return 0;
    }
    if (repeat == 2) repeat = 0;
    if (down) {
        *v_.arrowCount = 0;
        repeat = 3;
        if (idx() >= 0 && R().count - 1 > idx() && points(entry(idx())) >= 0) {
            tlen() = 0;
            const int old = idx()++;
            drawList(ui, true, idx(), old);
        }
        return 0;
    }
    if (repeat == 3) repeat = 0;
    if (code == 8) {  // Backspace (and Del)
        if (tlen() != 0) --tlen();
        int prev = idx();
        if (updateTyped(ui, host, 8)) prev = -1;
        const int top = bar().pos;
        if (idx() < top) {
            prev = -1;
            scrollBarSetPos(ui, barPics_, bar(), idx());
        }
        if (top + 6 <= idx()) {
            prev = -1;
            scrollBarSetPos(ui, barPics_, bar(), idx() - 6);
        }
        scrollBarSetRange(ui, barPics_, bar(), R().count - 8);
        drawList(ui, true, idx(), prev == idx() ? prev : -1);
        return 0;
    }
    if (code >= 0x20 && code < 0x80) {
        if (tlen() >= kMaxNameLength) return 0;
        uint16_t c = code;
        if (tlen() == 0 || typedAt(0) == 0x1F) {  // the first character: a capital letter
            if (c >= 'a' && c <= 'z') c = static_cast<uint16_t>(c - 0x20);
            if (c < 'A' || c > 'Z') return 0;
        }
        if (tlen() > 0 && typedAt(tlen() - 1) == 0x1F) --tlen();
        typedAt(tlen()++) = static_cast<uint8_t>(c);
        if (typedWidth(ui.fonts) >= kMaxNameWidth) {  // Text_Width up to the NUL
            --tlen();
            typedAt(tlen()) = 0;
            return 0;
        }
        host.playTypeEffect();
        int prev = idx();
        if (updateTyped(ui, host, static_cast<uint8_t>(c))) {
            prev = -1;
            scrollBarSetRange(ui, barPics_, bar(), R().count - 8);
        }
        keepIndexVisible(ui, prev);
        drawList(ui, true, idx(), prev == idx() ? prev : -1);
        return 0;
    }
    // Enter, Start Game
    if (!indexValid()) return 0;
    if (m == startGame_.zone.releaseCode) code = 0x0D;  // DS:2142
    if (code != 0x0D) return 0;
    if (points(entry(idx())) >= 0) return 0x0D;  // a player: sign in
    if (tlen() > 0 && typedAt(tlen() - 1) == ' ') {
        while (tlen() > 0 && typedAt(tlen() - 1) == ' ') {
            --tlen();
            updateTyped(ui, host, 8);
        }
        const int top = bar().pos;
        if (idx() < top) scrollBarSetPos(ui, barPics_, bar(), idx());
        if (top + 6 <= idx()) scrollBarSetPos(ui, barPics_, bar(), idx() - 6);
        scrollBarSetRange(ui, barPics_, bar(), R().count - 8);
        drawList(ui, true, idx(), -1);
    }
    if (idx() >= 0 && R().count - 1 > idx()) {  // the duplicate check: the next entry only
        const RosterEntry& next = entry(idx() + 1);
        bool same = true;
        for (int i = 0; i <= tlen() && i < static_cast<int>(kPlayerNameSize); ++i)
            if (typedAt(i) != next.name[static_cast<size_t>(i)]) {
                same = false;
                break;
            }
        if (same) {
            hideButtons(ui);
            host.messageRes(0x1068);
            showButtons(ui);
            return 0;
        }
    }
    return 0x0D;
}

}  // namespace tmport::gg
