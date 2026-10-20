// Tests of the G&G UI library (src/gg/ui): the rules of re/gg/docs/code_ui.md that need no data
// (the hint queue, the thousands formatter), the tables read from ssg.exe (menus, ranks, dialog
// strings and keys), the layouts of the prebuilt boxes, the help topics, the sign-in roster model,
// the text viewer's layout and, with GG_CAPTURES, screens composed by the UI tool against the
// DOSBox-X frames (0 differing pixels expected). No game data or text is stored here: counts,
// coordinates and CRC-32 digests only; the names the roster tests type are ordinary letters.

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "gg/data/dgroup_offsets.h"
#include "gg/gfx/video.h"
#include "gg/save/players_dat.h"
#include "gg/text/font.h"
#include "gg/text/text.h"
#include "gg/ui/attract.h"
#include "gg/ui/dialog.h"
#include "gg/ui/help.h"
#include "gg/ui/hints.h"
#include "gg/ui/menu_bar.h"
#include "gg/ui/menu_commands.h"
#include "gg/ui/rank.h"
#include "gg/ui/scroll_bar.h"
#include "gg/ui/signin.h"
#include "gg/ui/text_viewer.h"
#include "gg/ui/ui_cli.h"
#include "gg_test_data.h"
#include "mods/png_codec.h"
#include "util/file_util.h"
#include "util/png_writer.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

std::span<const uint8_t> bytesOf(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

// Runs the UI tool; true when it ran and succeeded.
bool runTool(std::vector<std::string> args) {
    int code = -1;
    const bool took = uiCli(args, code);
    return took && code == 0;
}

struct Rgba {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    uint32_t at(int x, int y) const {
        const size_t o = (static_cast<size_t>(y) * w + x) * 4;
        return uint32_t{px[o]} | uint32_t{px[o + 1]} << 8 | uint32_t{px[o + 2]} << 16;
    }
};

Rgba readPng(const std::filesystem::path& p) {
    const auto bytes = readWholeFile(p);
    REQUIRE(bytes);
    const auto img = mods::decodePng(*bytes);
    return {static_cast<int>(img.width), static_cast<int>(img.height), img.rgba};
}

struct Box {
    int x, y, w, h;
};

// Differing pixels between a 320x200 tool output and a 640x400 capture inside `boxes` (all of the
// frame when empty).
int differing(const Rgba& out, const Rgba& cap, const std::vector<Box>& boxes = {}) {
    REQUIRE(out.w == 320);
    REQUIRE(cap.w == 640);
    int n = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            bool in = boxes.empty();
            for (const Box& b : boxes) in = in || (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h);
            if (in && out.at(x, y) != cap.at(2 * x, 2 * y)) ++n;
        }
    return n;
}

class ScriptHost final : public SignInHost {
public:
    int yesNo = 0;
    std::vector<uint16_t> messages;
    int saves = 0, effects = 0;
    int mx = 160, my = 100;
    int yesNoRes(uint16_t) override { return yesNo; }
    void messageRes(uint16_t id) override { messages.push_back(id); }
    void playTypeEffect() override { ++effects; }
    void saveRoster(const Roster&) override { ++saves; }
    uint32_t ticks() override { return 0; }
    int mouseX() override { return mx; }
    int mouseY() override { return my; }
};

std::string nameOf(const RosterEntry& e) { return e.nameString(); }

std::optional<uint16_t> idOpt(uint16_t id) { return id; }

}  // namespace

TEST_CASE("ui: Str_FormatThousands") {
    CHECK(formatThousands(0) == "0");
    CHECK(formatThousands(400) == "400");
    CHECK(formatThousands(1720) == "1,720");
    CHECK(formatThousands(750000) == "750,000");
    CHECK(formatThousands(9999999) == "9,999,999");
    CHECK(formatThousands(10000000).empty());  // no case for 8 digits (2bd4:0281)
}

TEST_CASE("ui: the New Player Hint queue") {
    HintQueue q;
    q.clear();
    q.queue(0x1048, 100, 1000, false, false);
    q.queue(0x1046, 50, 1000, false, false);
    q.queue(0x104A, 50, 1000, false, false);  // equal due times keep queue order
    CHECK(q.pendingIds() == std::vector<uint16_t>{0x1046, 0x104A, 0x1048});
    q.queue(0x1050, 0, 1000, true, false);  // hints off
    q.queue(0x1051, 0, 1000, false, true);  // already shown
    CHECK(q.pending() == 3);
    CHECK_FALSE(q.update(1050));  // due < now is strict
    CHECK(q.update(1051) == idOpt(0x1046));
    CHECK(q.update(1051) == idOpt(0x104A));
    CHECK_FALSE(q.update(1051));  // at most one per call; the next is not due
    bool cancelled = true;
    q.queue(0x1046, 0, 2000, false, false, [&] { return cancelled; });  // the free slot 1, due 2000
    CHECK(q.pendingIds() == std::vector<uint16_t>{0x1048, 0x1046});
    CHECK(q.update(2001) == idOpt(0x1048));
    CHECK_FALSE(q.update(2001));  // cancelled: dropped unseen
    CHECK(q.pending() == 0);
    // Hint_CompareDue subtracts the low words as a signed 16-bit value: a hint due 40000 ticks later
    // sorts before one due now, and blocks it (Hint_Update looks at the head only).
    HintQueue w;
    w.clear();
    w.queue(1, 0, 0, false, false);
    w.queue(2, 40000, 0, false, false);
    CHECK(w.pendingIds() == std::vector<uint16_t>{2, 1});
    CHECK_FALSE(w.update(1));
    // a sixth hint overflows the five slots but is kept (logged)
    HintQueue full;
    full.clear();
    for (uint16_t i = 0; i < 6; ++i) full.queue(static_cast<uint16_t>(0x1043 + i), i, 0, false, false);
    CHECK(full.pending() == 6);
}

TEST_CASE("ui data: ranks, dialog strings and the menu bar from ssg.exe") {
    GG_REQUIRE_DATA();
    const GgExe& exe = ggFiles->exe();
    const RankTable ranks = RankTable::fromExe(exe);
    CHECK(ranks.thresholds == std::array<uint32_t, 7>{5000, 30000, 60000, 120000, 200000, 300000, 750000});
    CHECK(ranks.rankOf(0) == 0);
    CHECK(ranks.rankOf(4999) == 0);
    CHECK(ranks.rankOf(5000) == 1);
    CHECK(ranks.rankOf(749999) == 6);
    CHECK(ranks.rankOf(750000) == 7);
    CHECK(ranks.rankOf(0xFFFFFFFFu) == 7);
    for (const auto& n : ranks.names) CHECK_FALSE(n.empty());

    const DialogStrings s = DialogStrings::fromExe(exe);
    CHECK(s.keys == std::array<uint16_t, 5>{'y', 'n', 'c', 'd', 'o'});
    CHECK(s.pagerKeys == std::array<uint16_t, 6>{0x151, 0x151, 0x149, 0x151, 0x149, 0x151});
    CHECK(s.footerGoBack.size() == 12);
    CHECK(s.footerGoBack[0] == 0x1F);  // the Esc key-cap glyph
    CHECK(s.footerGoOn.size() == 10);
    CHECK(s.footerGoOn[0] == 0x18);  // the Enter key-cap glyph
    CHECK(s.hintTitle.size() == 15);
    CHECK(s.hintFooter.size() == 20);
    for (const auto& l : s.buttonLabels) CHECK_FALSE(l.empty());

    const MenuBarDef bar = MenuBarDef::fromExe(exe);
    REQUIRE(bar.count() == 3);
    const MenuDef& help = bar.menus[kMenuHelp];
    const MenuDef& file = bar.menus[kMenuFile];
    const MenuDef& opts = bar.menus[kMenuOptions];
    CHECK(help.titleX == 108);
    CHECK(file.titleX == 0);
    CHECK(opts.titleX == 54);
    for (const MenuDef& m : bar.menus) CHECK(m.titleWidth == 54);
    CHECK(help.dropWidth == 164);
    CHECK(file.dropWidth == 220);
    CHECK(opts.dropWidth == 145);
    CHECK(help.handlers == std::vector<MenuCommand>{MenuCommand::HelpAboutGame, MenuCommand::HelpMovingAround,
                                                    MenuCommand::HelpBuildingVehicles, MenuCommand::HelpFindingParts,
                                                    MenuCommand::HelpSolvingPuzzles, MenuCommand::HelpChallengeRaces});
    CHECK(file.handlers ==
          std::vector<MenuCommand>{MenuCommand::StartNewPlayer, MenuCommand::ReturnToCenter, MenuCommand::SaveAndExit});
    CHECK(opts.handlers == std::vector<MenuCommand>{MenuCommand::ToggleSound, MenuCommand::ToggleHints});
    for (const MenuDef& m : bar.menus) {  // the items: one LF-separated line per handler
        int lines = 1;
        for (const uint8_t c : m.items) lines += c == '\n';
        CHECK(lines == m.itemCount);
    }
    CHECK(menuCommandQuestion(MenuCommand::SaveAndExit) == idOpt(0x1064));
    CHECK(helpTopicOf(MenuCommand::HelpSolvingPuzzles) == idOpt(0x1062));

    const auto titles = menuTitleZones(bar);
    REQUIRE(titles.size() == 3);
    CHECK(titles[0].x == 108);
    CHECK(titles[0].w == 56);
    CHECK(titles[0].h == 9);
    CHECK(titles[0].priority == 8);
    CHECK(titles[0].pressCode == 0);
    CHECK(titles[0].releaseCode == 0x13B);
    CHECK(titles[2].releaseCode == 0x13D);
}

TEST_CASE("ui data: the layouts of the boxes") {
    GG_REQUIRE_DATA();
    const GgExe& exe = ggFiles->exe();
    const Archive& arc = ggFiles->archive();
    Fonts fonts = Fonts::load(arc);
    const DialogStrings s = DialogStrings::fromExe(exe);

    // code_ui.md §21: the chimps question (6, 46) 308 x 107, two buttons, focus 0
    const OwnedDialog chimps = askChimpsDesc(s, arc);
    const DialogLayout c = dlgLayout(fonts, chimps.desc);
    CHECK(c.x == 6);
    CHECK(c.y == 46);
    CHECK(c.w == 308);
    CHECK(c.h == 107);
    CHECK(c.buttonCount == 2);
    CHECK(c.buttons[0].zone.pressCode == 'y');
    CHECK(c.buttons[1].zone.releaseCode == 'n');
    CHECK(c.buttons[0].zone.priority == 5);
    CHECK(c.buttons[0].zone.y == c.buttons[1].zone.y);
    CHECK(c.buttons[1].zone.x - c.buttons[0].zone.x == c.buttonW + 10);
    CHECK(chimps.desc.flags == kDlgEscDisabled);

    // the File drop-down (0, 9) 242 x 54 (code_ui.md §6.4)
    const MenuBarDef bar = MenuBarDef::fromExe(exe);
    const DialogLayout f = dlgLayout(fonts, menuDropDownDesc(s, bar.menus[kMenuFile]));
    CHECK(f.x == 0);
    CHECK(f.y == 9);
    CHECK(f.w == 242);
    CHECK(f.h == 54);
    CHECK(f.bodyY == 13);
    const auto items = menuItemZones(bar.menus[kMenuFile], f);
    REQUIRE(items.size() == 5);
    CHECK(items[0].releaseCode == kMenuOutsideCode);
    CHECK(items[1].y == 13);
    CHECK(items[3].y == 33);
    CHECK(items[3].pressCode == 0xFD2);
    CHECK(items[4].y == 45);
    CHECK(items[4].releaseCode == 0x1B);
    for (const MenuDef& m : bar.menus) CHECK(dlgLayout(fonts, menuDropDownDesc(s, m)).h == m.itemCount * 10 + 24);

    // the help pager's fixed box: (19, 14), 282 x 177 with a title and a button row
    const HelpTopic about = HelpTopic::load(arc, kHelpAboutGame);
    for (int p = 0; p < about.pageCount(); ++p) {
        CAPTURE(p);
        const DialogLayout L = dlgLayout(fonts, about.pageDesc(s, p));
        CHECK(L.x == 19);
        CHECK(L.y == 14);
        CHECK(L.w == 282);
        CHECK(L.h == 177);
        CHECK(L.buttonCount == (p == 0 ? 1 : 2));
    }

    // Yes/No boxes: Esc footer, right-aligned buttons, the default from the bundle
    const OwnedDialog ret = yesNoResDesc(s, arc, kMsgReturnToCenter);
    CHECK(ret.desc.focus == 1);
    CHECK(ret.desc.align == DlgAlign::Right);
    const OwnedDialog snd = yesNoResDesc(s, arc, kMsgSoundAndMusic);
    CHECK(snd.desc.focus == 0);  // Sound and Music defaults to Yes
    const DialogLayout r = dlgLayout(fonts, ret.desc);
    CHECK(r.buttons[1].zone.x + r.buttonW == r.x + r.w - 16);  // the row ends 6 + 10 inside the box

    // the boxes verified against the captures (code_ui.md §21)
    const DialogLayout rl = dlgLayout(fonts, ret.desc);
    CHECK(rl.x == 5);
    CHECK(rl.y == 38);
    CHECK(rl.w * rl.h == 38440);
    const HintBox h1046 = HintBox::load(s, arc, 0x1046);
    const DialogLayout hl = dlgLayout(fonts, h1046.desc);
    CHECK(hl.w * hl.h == 28388);
    CHECK(hl.buttonCount == 0);
    // Dlg_MessageRes: 0x106F's footer flag comes from member 3 (the original's is uninitialised)
    const OwnedDialog full = messageResDesc(s, arc, 0x1069);
    CHECK_FALSE(full.desc.title);  // an 'I 0' title member
    const OwnedDialog disk = messageResDesc(s, arc, 0x106F);
    CHECK(disk.desc.footer);
}

TEST_CASE("ui data: help topics") {
    GG_REQUIRE_DATA();
    const Archive& arc = ggFiles->archive();
    const std::vector<std::pair<uint16_t, std::vector<size_t>>> expect = {
        {0x105D, {1, 0, 1, 0}}, {0x105E, {1, 0}}, {0x105F, {0, 0}}, {0x1060, {0, 0}}, {0x1061, {0, 0}}, {0x1062, {0}}};
    for (const auto& [id, pics] : expect) {
        CAPTURE(id);
        const HelpTopic t = HelpTopic::load(arc, id);
        REQUIRE(t.pageCount() == static_cast<int>(pics.size()));
        CHECK_FALSE(t.title.empty());
        for (size_t p = 0; p < pics.size(); ++p) {
            CHECK(t.pages[p].pictures.size() == pics[p]);
            CHECK_FALSE(t.pages[p].text.empty());
        }
    }
}

TEST_CASE("ui data: the sign-in roster model (typing, New Player, Backspace, Ctrl+R, duplicates)") {
    GG_REQUIRE_DATA();
    const GgExe& exe = ggFiles->exe();
    const Archive& arc = ggFiles->archive();
    Video v;
    v.init(exe, arc);
    Fonts fonts = Fonts::load(arc);
    fonts.select(1);  // SignIn_ShowScreen
    const UiCanvas ui = UiCanvas::of(v, fonts);
    signInDrawScreen(v, arc);
    SignIn si(exe, arc);
    ScriptHost host;

    // A new file: the first player gets slot 98 (code_ui.md §12.3).
    si.begin(PlayersDat::create().roster, 0);
    CHECK(si.typedLength() == -1);
    si.enter(ui, host);  // an empty roster runs New Player
    REQUIRE(si.count() == 1);
    CHECK(SignIn::points(si.roster().entries[0]) == -2);
    CHECK(si.roster().entries[0].slot == 98);
    CHECK(si.typedLength() == 0);
    CHECK_FALSE(si.startGameButton().enabled);  // a -2 entry with no character
    CHECK_FALSE(si.newPlayerButton().enabled);
    CHECK(si.cancelButton().enabled);
    CHECK(si.handleCode(ui, host, '1') == 0);  // the first character must be a letter
    CHECK(si.typedLength() == 0);
    for (const char c : std::string("ada")) CHECK(si.handleCode(ui, host, static_cast<uint8_t>(c)) == 0);
    CHECK(host.effects == 3);
    CHECK(nameOf(si.roster().entries[0]) == "Ada");  // capital first letter
    CHECK(si.typedLength() == 3);
    CHECK(si.startGameButton().enabled);
    CHECK(si.handleCode(ui, host, kCodeStartGame) == 0x0D);

    // A roster with two players (as the game saves them): typing selects by prefix or inserts.
    Roster r = PlayersDat::create().roster;
    auto put = [&](int i, const char* name, uint32_t points, uint16_t slot) {
        RosterEntry& e = r.entries[static_cast<size_t>(i)];
        e = RosterEntry{};
        for (size_t k = 0; name[k]; ++k) e.name[k] = static_cast<uint8_t>(name[k]);
        e.points = points;
        e.slot = slot;
    };
    put(0, "Ada", 400, 98);
    put(1, "Cyd", 7000, 1);
    r.entries[2].slot = 2;
    r.count = 2;
    si.begin(r, 0);
    si.enter(ui, host);
    CHECK(si.typedLength() == 0);
    CHECK(si.count() == 2);
    CHECK(si.newPlayerButton().enabled);
    si.handleCode(ui, host, 'c');  // a prefix of "Cyd": selects it, nothing inserted
    CHECK(si.index() == 1);
    CHECK(si.count() == 2);
    si.handleCode(ui, host, 'z');  // "Cz" matches nothing: a -1 entry in name order
    CHECK(si.count() == 3);
    CHECK(si.index() == 2);
    CHECK(SignIn::points(si.roster().entries[2]) == -1);
    CHECK(si.roster().entries[2].slot == 2);  // the first free slot
    CHECK(nameOf(si.roster().entries[2]) == "Cz");
    si.handleCode(ui, host, 8);  // Backspace: "C" selects Cyd again, the entry goes
    CHECK(si.count() == 2);
    CHECK(si.index() == 1);
    CHECK(si.roster().entries[2].slot == 2);  // the slot is free again
    si.handleCode(ui, host, 0x148);  // Up
    CHECK(si.index() == 0);
    CHECK(si.typedLength() == 0);

    // New Player: an empty -2 entry first; "Ada" again sorts before the old Ada; Enter = duplicate
    si.handleCode(ui, host, kCodeNewPlayer);
    CHECK(si.count() == 3);
    CHECK(si.index() == 0);
    CHECK(SignIn::points(si.roster().entries[0]) == -2);
    for (const char c : std::string("Ada")) si.handleCode(ui, host, static_cast<uint8_t>(c));
    CHECK(si.index() == 0);
    CHECK(nameOf(si.roster().entries[0]) == "Ada");
    CHECK(nameOf(si.roster().entries[1]) == "Ada");
    CHECK(si.handleCode(ui, host, 0x0D) == 0);
    REQUIRE(host.messages.size() == 1);
    CHECK(host.messages[0] == 0x1068);
    si.handleCode(ui, host, kCodeCancel);  // Cancel drops the entry in progress
    CHECK(si.count() == 2);

    // the 60-pixel limit of the small font (and 15 characters)
    si.handleCode(ui, host, kCodeNewPlayer);
    int accepted = 0;
    for (int i = 0; i < 20; ++i) {
        const int before = si.typedLength();
        si.handleCode(ui, host, 'm');
        accepted += si.typedLength() > before;
    }
    CHECK(accepted == si.typedLength());
    CHECK(textWidth(fonts, si.typedBytes()) < kMaxNameWidth);
    CHECK(accepted < kMaxNameLength);
    si.handleCode(ui, host, kCodeCancel);

    // Ctrl+R on a player: Yes removes it from the index at once (Roster_Save), its slot returns
    host.yesNo = 1;
    si.handleCode(ui, host, 0x150);  // Down to Cyd
    REQUIRE(si.index() == 1);
    si.handleCode(ui, host, 0x12);
    CHECK(host.saves == 1);
    CHECK(si.count() == 1);
    CHECK(si.roster().entries[1].slot == 1);
}

TEST_CASE("ui data: the scroll bar and the text viewer's layout") {
    GG_REQUIRE_DATA();
    const GgExe& exe = ggFiles->exe();
    const Archive& arc = ggFiles->archive();
    const ScrollBar sb = ScrollBar::fromRecord(exe, dg::kRosterScrollBar);
    CHECK(sb.x == 244);
    CHECK(sb.y == 26);
    CHECK(sb.w == 11);
    CHECK(sb.h == 109);
    CHECK(sb.page == 8);

    Fonts fonts = Fonts::load(arc);
    {  // ScrollBar_HandleClick: arrows, pages, the thumb's drag (406a:01b6)
        Screen screen;
        const UiCanvas ui{screen, fonts, {}};
        const ScrollBarPictures pics = ScrollBarPictures::load(arc);
        ScrollBar b = sb;
        b.max = 4;
        b.pos = 0;
        scrollBarDraw(ui, pics, b);
        CHECK(b.thumbY == b.y + 10);
        CHECK_FALSE(scrollBarHandleClick(ui, pics, b, 0x4701, b.y + b.h - 5, 100));  // below the down arrow
        CHECK(b.pos == 1);
        CHECK(b.nextRepeat == 127);
        CHECK(b.thumbY == b.y + (b.h - 31) * 1 / 4 + 10);
        CHECK_FALSE(scrollBarHandleClick(ui, pics, b, 0x8701, b.y + b.h - 5, 127));  // held: not yet (next < now)
        CHECK(b.pos == 1);
        CHECK_FALSE(scrollBarHandleClick(ui, pics, b, 0x8701, b.y + b.h - 5, 128));  // the repeat
        CHECK(b.pos == 2);
        CHECK_FALSE(scrollBarHandleClick(ui, pics, b, 0x4701, b.thumbY - 1, 200));  // above the thumb: a page
        CHECK(b.pos == 0);
        const auto grab = scrollBarHandleClick(ui, pics, b, 0x4701, b.thumbY + 4, 300);  // on the thumb
        REQUIRE(grab);
        CHECK(*grab == 4);
        int dragY = b.thumbY + 4, thumb = b.thumbY;
        scrollBarDragMove(ui, b, *grab, b.x + 3, b.y + b.h, dragY, thumb);  // clamped to y + h - 19
        CHECK(thumb == b.y + b.h - 19);
        scrollBarDragEnd(ui, pics, b, thumb, 400);
        CHECK(b.pos == 4);  // ((h - 29) * 4 + 39) / 78 -> max
        scrollBarDragEnd(ui, pics, b, 0x4701, 400);  // no move: the caller's register (the input code)
        CHECK(b.pos == 4);
        scrollBarSetRange(ui, pics, b, -3);
        CHECK(b.max == 0);
        CHECK(b.pos == 0);
    }
    fonts.select(1);
    const auto k0 = arc.read(0x20DD);
    const auto k5 = arc.read(0x1163);
    CHECK(TextViewLayout::countLines(fonts, text::untilNul(k0)) == 18);
    CHECK(TextViewLayout::countLines(fonts, text::untilNul(k5)) == 12);
    // synthetic: a long word does not wrap; a newline counts; escapes are skipped
    CHECK(TextViewLayout::countLines(fonts, bytesOf(std::string(40, 'm'))) == 0);
    CHECK(TextViewLayout::countLines(fonts, bytesOf("a\nb\nc")) == 2);
    CHECK(TextViewLayout::wordWidth(fonts, bytesOf("ab\xFF\x03\x05\xFF" "c d"), 0) ==
          fonts.charWidth('a') + fonts.charWidth('b') + fonts.charWidth('c'));

    Video v;
    v.init(exe, arc);
    const UiCanvas ui = UiCanvas::of(v, fonts);
    TextViewer view(exe, arc, text::untilNul(k0));
    ClickZones zones;
    view.open(ui, &zones);
    CHECK(view.scrollMax() == 8);
    CHECK(view.thumbStep() == 13);
    CHECK(view.thumbY() == 45);
    CHECK(ClickZones::listOf(zones.local()).size() == 6);  // close, text, up, down, thumb, page-down
    for (int i = 0; i < 20; ++i) {
        view.scrollBy(1);
        view.thumbDown();
    }
    CHECK(view.line() == 7);
    CHECK(view.thumbY() == 138);
    view.updateBarZones(zones);
    CHECK(ClickZones::listOf(zones.local()).size() == 6);  // page-up, thumb (no page-down at the end)
}

TEST_CASE("ui data: the attract descriptors and Attract_PlayAnim's channels") {
    GG_REQUIRE_DATA();
    const GgExe& exe = ggFiles->exe();
    const AttractAnims a = AttractAnims::fromExe(exe);
    CHECK(a.title.channels.size() == 2);
    CHECK(a.afterTitle.channels.size() == 3);
    CHECK(a.afterStory.channels.size() == 5);
    CHECK(a.beforePrompt.channels.size() == 3);
    CHECK(a.title.loadOrder(exe) == std::vector<uint16_t>{0x1005, 0x1007});
    CHECK(a.afterTitle.loadOrder(exe) == std::vector<uint16_t>{0x1015, 0x1005, 0x1007});
    CHECK(a.afterStory.loadOrder(exe).size() == 11);

    // a host that counts: animation 0x1015 (channel 0 of DS:2096) stops 3 frames after it starts
    struct Host final : ScreenAnimHost {
        std::vector<uint16_t> ids;
        std::vector<int> startedAt;
        int frames = 0;
        int load(uint16_t id) override {
            ids.push_back(id);
            startedAt.push_back(-1);
            return static_cast<int>(ids.size()) - 1;
        }
        void startAt(int h, int) override { startedAt[static_cast<size_t>(h)] = frames; }
        bool inList(int h) const override { return startedAt[static_cast<size_t>(h)] >= 0; }
        bool stopped(int h) const override {
            return ids[static_cast<size_t>(h)] == 0x1015 && startedAt[static_cast<size_t>(h)] >= 0 &&
                   frames >= startedAt[static_cast<size_t>(h)] + 3;
        }
        void frame() override { ++frames; }
        void release(int) override {}
        void resetSprites() override {}
    } host;
    AttractAnimPlayer p(exe, a.afterTitle, host);
    int f = 0;
    while (!p.channelAtEnd(0) && f < 1000) {
        p.runFrame();
        ++f;
    }
    CHECK(host.startedAt[0] == 51);  // wait 50: released after 51 frames
    CHECK(f == 54);
}

TEST_CASE("ui data: screens composed by the UI tool (CRC-32 of their pixels)") {
    GG_REQUIRE_DATA();
    const auto dir = testdata::scratchDir("gg_ui_crc");
    const auto data = pathToUtf8(ggFiles->folder());
    const std::vector<std::pair<std::vector<std::string>, uint32_t>> cases = {
        {{"title", "--phase", "3", "--frame", "9"}, 0x6b5cce67u},
        {{"title", "--phase", "1", "--frame", "20"}, 0xd5500c6au},
        {{"after-title", "--frame", "80"}, 0x19e9b7ebu},
        {{"story1"}, 0x85692a04u},
        {{"story6"}, 0x1f27aa46u},
        {{"after-story", "--frame", "300"}, 0xc22cde27u},
        {{"before-prompt", "--frame", "30"}, 0xa9759d19u},
        {{"prompt"}, 0xf442bfe0u},
        {{"signin", "--new-file", "--cursor", "160,100"}, 0x84c8d506u},
        {{"chimps", "--new-file", "--type", "ada", "--cursor", "164,180"}, 0x38f0e393u},
        {{"hint:104f"}, 0x7c0c8331u},
        {{"message:106f"}, 0x6857eb27u},
        {{"menu:help:3"}, 0xbc73bc64u},
        {{"help:105d:3"}, 0xdef225c7u},
        {{"textview:20dd:5"}, 0x82264cb7u},
    };
    for (const auto& [a, crc] : cases) {
        CAPTURE(a[0]);
        const auto out = dir / "screen.png";
        std::vector<std::string> args = {"--gg-screen", a[0], pathToUtf8(out), "--data", data};
        args.insert(args.end(), a.begin() + 1, a.end());
        REQUIRE(runTool(args));
        const Rgba img = readPng(out);
        CHECK(png::crc32(img.px) == crc);
    }
}

TEST_CASE("ui captures: screens composed by the UI tool are pixel exact") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto dir = testdata::scratchDir("gg_ui_captures");
    const auto data = pathToUtf8(ggFiles->folder());
    struct Case {
        const char* capture;
        std::vector<std::string> args;
        std::vector<Box> boxes;  // empty: the whole frame
    };
    const auto w0p = testdata::ggCapture("savestates/w0/03_signin.players");
    const auto p2p = testdata::ggCapture("savestates/r2/player2/02_signin_roster.players");
    if (!w0p || !p2p) return;
    const std::vector<Case> cases = {
        {"w0/01_title.png", {"title", "--phase", "3", "--frame", "9"}, {}},
        {"w0/02_story1.png", {"story1"}, {}},
        {"w0/04_signin_name.png", {"name", "--players", pathToUtf8(*w0p), "--type", "ada", "--cursor", "160,100"}, {}},
        {"w0/05_chimps_dialog.png", {"chimps", "--players", pathToUtf8(*w0p), "--type", "ada", "--cursor", "164,180"}, {}},
        {"r2/player2/04_name_typed.png",
         {"name", "--players", pathToUtf8(*p2p), "--new", "--type", "bob", "--cursor", "219,180"},
         {}},
        {"w0/08_building_hint.png", {"hint:1043", "--cursor", "62,85"}, {{7, 48, 306, 104}}},
        {"w0/24_return_dialog.png", {"yesno:106b", "--cursor", "107,27"}, {{0, 0, 320, 9}, {5, 38, 310, 124}}},
        {"r2/resume/03_after_save_exit.png", {"yesno:1064", "--cursor", "99,36"}, {{0, 0, 320, 9}, {12, 48, 296, 104}}},
        {"w0/23_file_menu.png", {"menu:file:0", "--cursor", "14,4"}, {{0, 0, 320, 9}, {0, 9, 242, 54}}},
        {"r2/menus/03_help_topic1.png", {"help:105d:0", "--cursor", "160,17"}, {{0, 0, 320, 9}, {19, 14, 282, 177}}},
    };
    for (const Case& c : cases) {
        CAPTURE(c.capture);
        const auto cap = testdata::ggCapture(c.capture);
        if (!cap) continue;
        const auto out = dir / "screen.png";
        std::vector<std::string> args = {"--gg-screen", c.args[0], pathToUtf8(out), "--data", data};
        args.insert(args.end(), c.args.begin() + 1, c.args.end());
        REQUIRE(runTool(args));
        CHECK(differing(readPng(out), readPng(*cap), c.boxes) == 0);
    }
}
