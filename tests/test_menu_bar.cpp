// In-engine menu bar (ui/menu_bar): model from the EXE, keyboard and mouse navigation through
// synthetic events, the command dispatch rules of Menu_OnCommand (1078:0135) and a headless
// render of the canvas with a menu open. Headless engine only; nothing opens a window.
#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <vector>

#include "mischief/gfx/presenter.h"
#include "mischief/test/app_test_util.h"
#include "mischief/timing/clock.h"
#include "mischief/util/file_util.h"
#include "platform/engine.h"
#include "test_data.h"
#include "text/font.h"
#include "ui/menu_bar.h"

using namespace tmport;
using namespace apptest;
using ui::MenuBar;
using ui::MenuBarModel;
using ui::MenuCommand;

namespace {

std::string str(const ui::MenuLabel& l) { return l.str(); }

struct Rig {
    mischief::ManualClock clock;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<text::Font> font;
    std::unique_ptr<MenuBar> menu;
    std::vector<uint16_t> ran;  // command ids of the handlers that ran

    explicit Rig(GameFiles& g) {
        EngineConfig cfg;
        cfg.clock = &clock;
        engine = std::make_unique<Engine>(g, mischief::gfx::ColorMode::Colour256, cfg);
        font = std::make_unique<text::Font>(text::Font::load(*g.res));
        menu = std::make_unique<MenuBar>(*engine, MenuBarModel::fromExe(*g.exe), *font);
        for (int c = 1; c < static_cast<int>(MenuCommand::Count); ++c) {
            const auto cmd = static_cast<MenuCommand>(c);
            menu->setHandler(cmd, [this, cmd] {
                for (const auto& m : menu->model().menus)
                    for (const auto& it : m.items)
                        if (it.command == cmd) ran.push_back(it.commandId);
            });
        }
    }
    // Centre of a title / item in canvas coordinates.
    std::pair<int, int> titleCentre(int m) const {
        const auto r = menu->titleRect(m);
        return {r.x + r.w / 2, r.y + r.h / 2};
    }
    std::pair<int, int> itemCentre(int m, int i) const {
        const auto r = menu->itemRect(m, i);
        return {r.x + r.w / 2, r.y + r.h / 2};
    }
};

}  // namespace

TEST_CASE("menu label: '&' mnemonics") {
    const std::string raw = "&File";
    auto l = ui::MenuLabel::parse(std::span(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()));
    CHECK(str(l) == "File");
    CHECK(l.mnemonicIndex == 0);
    CHECK(l.key == 'F');
    const std::string raw2 = "Save && E&xit";
    l = ui::MenuLabel::parse(std::span(reinterpret_cast<const uint8_t*>(raw2.data()), raw2.size()));
    CHECK(str(l) == "Save & Exit");
    CHECK(l.mnemonicIndex == 8);
    CHECK(l.key == 'X');
    CHECK(ui::menuCommandForHandler(0x1140, 0x0098) == MenuCommand::Exit);
    CHECK(ui::menuCommandForHandler(0x1140, 0x0099) == MenuCommand::None);
}

TEST_CASE("menu model: built from the EXE (MenuBarDef DS:13bf)") {
    TM_REQUIRE_GAME_DATA();
    const MenuBarModel m = MenuBarModel::fromExe(*g->exe);
    REQUIRE(m.menus.size() == 3);
    CHECK(str(m.menus[0].title) == "File");
    CHECK(str(m.menus[1].title) == "Options");
    CHECK(str(m.menus[2].title) == "Help");
    CHECK(m.menus[0].title.key == 'F');
    CHECK(m.menus[1].title.key == 'O');
    CHECK(m.menus[2].title.key == 'H');
    REQUIRE(m.menus[0].items.size() == 3);
    REQUIRE(m.menus[1].items.size() == 3);
    REQUIRE(m.menus[2].items.size() == 5);
    // Order and handlers (code_10f8-1148.md §3.8). Item texts are compared by their first word only.
    const MenuCommand expect[3][5] = {
        {MenuCommand::SeeRank, MenuCommand::StartAnotherGame, MenuCommand::Exit},
        {MenuCommand::NewPlayerMessages, MenuCommand::MusicAndSound, MenuCommand::Voice},
        {MenuCommand::HelpAboutGame, MenuCommand::HelpKeyboard, MenuCommand::HelpMouse, MenuCommand::HelpEducation,
         MenuCommand::AboutTM},
    };
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < m.menus[i].items.size(); ++j) {
            const auto& it = m.menus[i].items[j];
            CAPTURE(i);
            CAPTURE(j);
            CHECK(it.command == expect[i][j]);
            CHECK(it.commandId == i * 0x100 + j);
            CHECK(!it.label.text.empty());
            CHECK(it.label.mnemonicIndex == -1);  // the shipped items have no '&'
        }
    CHECK(str(m.menus[0].items[0].label).starts_with("See"));
    CHECK(str(m.menus[0].items[2].label).starts_with("Exit"));
    CHECK(str(m.menus[1].items[2].label).starts_with("Voice"));
    CHECK(m.menus[2].items[4].handlerSelector == 0x1140);
    CHECK(m.menus[2].items[4].handlerOffset == 0x0000);
    CHECK(m.find(0x0204) == &m.menus[2].items[4]);
    CHECK(m.find(0x0205) == nullptr);
    CHECK(m.find(0x0300) == nullptr);
}

TEST_CASE("menu bar: install / remove / enableAll (1120, 1078:01b1/01b5)") {
    TM_REQUIRE_GAME_DATA();
    Rig r(*g);
    MenuBar& m = *r.menu;
    CHECK_FALSE(m.installed());
    CHECK_FALSE(m.isEnabled());
    m.enableAll(true);  // no menu: no-op, Menu_IsEnabled stays 0
    CHECK_FALSE(m.isEnabled());
    CHECK_FALSE(r.engine->presenter().hasOverlay());
    m.install();
    CHECK(m.installed());
    CHECK(m.isEnabled());
    CHECK(r.engine->presenter().hasOverlay());
    {
        MenuBar::DisableScope off(&m);
        CHECK_FALSE(m.isEnabled());
        {
            MenuBar::DisableScope off2(&m);
            CHECK_FALSE(m.isEnabled());
        }
        CHECK_FALSE(m.isEnabled());  // restored to "was" = disabled
    }
    CHECK(m.isEnabled());
    m.remove();
    CHECK_FALSE(m.installed());
    CHECK_FALSE(m.isEnabled());
    CHECK_FALSE(r.engine->presenter().hasOverlay());
}

TEST_CASE("menu bar: keyboard navigation with synthetic events") {
    TM_REQUIRE_GAME_DATA();
    Rig r(*g);
    Engine& e = *r.engine;
    MenuBar& m = *r.menu;

    // Not installed: Alt+F is not the menu's.
    press(e, SDLK_F, SDL_KMOD_LALT);
    e.getInputCode();
    CHECK_FALSE(m.tracking());

    m.install();
    press(e, SDLK_F, SDL_KMOD_LALT);
    CHECK(e.getInputCode() == 0);  // consumed: the game sees nothing
    CHECK(m.tracking());
    CHECK(m.openMenu() == 0);
    CHECK(m.dropped());
    CHECK(m.selectedItem() == 0);

    press(e, SDLK_DOWN);
    e.getInputCode();
    CHECK(m.selectedItem() == 1);
    press(e, SDLK_DOWN);
    press(e, SDLK_DOWN);
    e.getInputCode();
    CHECK(m.selectedItem() == 0);  // wraps
    press(e, SDLK_UP);
    e.getInputCode();
    CHECK(m.selectedItem() == 2);

    press(e, SDLK_RIGHT);
    e.getInputCode();
    CHECK(m.openMenu() == 1);
    CHECK(m.selectedItem() == 0);
    press(e, SDLK_RIGHT);
    press(e, SDLK_RIGHT);
    e.getInputCode();
    CHECK(m.openMenu() == 0);  // wraps
    press(e, SDLK_LEFT);
    e.getInputCode();
    CHECK(m.openMenu() == 2);

    // Esc closes the drop-down, then leaves the menu loop.
    press(e, SDLK_ESCAPE);
    CHECK(e.getInputCode() == 0);
    CHECK(m.tracking());
    CHECK_FALSE(m.dropped());
    CHECK(m.openMenu() == 2);
    press(e, SDLK_ESCAPE);
    CHECK(e.getInputCode() == 0);  // the Esc never reaches the game
    CHECK_FALSE(m.tracking());

    // Letters and Ctrl+S typed while the menu is open are swallowed.
    press(e, SDLK_O, SDL_KMOD_LALT);
    e.postEvent(key(true, SDLK_Z));
    e.postEvent(textInput("z"));
    e.postEvent(key(false, SDLK_Z));
    CHECK(e.getInputCode() == 0);
    CHECK(m.openMenu() == 1);
    CHECK(r.ran.empty());

    // Enter chooses; the command is delivered by the same fetch, after the menu loop.
    press(e, SDLK_DOWN);
    press(e, SDLK_RETURN);
    CHECK(e.getInputCode() == 0);
    CHECK_FALSE(m.tracking());
    REQUIRE(r.ran.size() == 1);
    CHECK(r.ran[0] == 0x0101);  // Options > Music and Sound
    CHECK(m.isEnabled());       // re-enabled after the handler

    // After the menu, keys reach the game again.
    e.postEvent(key(true, SDLK_A));
    e.postEvent(textInput("a"));
    e.postEvent(key(false, SDLK_A));
    CHECK(e.getInputCode() == 'a');

    // F10 highlights the first title; Down opens it; a letter picks an item by its first letter.
    press(e, SDLK_F10);
    e.getInputCode();
    CHECK(m.tracking());
    CHECK(m.openMenu() == 0);
    CHECK_FALSE(m.dropped());
    press(e, SDLK_H);  // title mnemonic in bar mode
    e.getInputCode();
    CHECK(m.openMenu() == 2);
    CHECK(m.dropped());
    press(e, SDLK_U);  // "Using the Keyboard" (first item starting with U)
    e.getInputCode();
    REQUIRE(r.ran.size() == 2);
    CHECK(r.ran[1] == 0x0201);

    // A lone Alt toggles the bar.
    e.postEvent(key(true, SDLK_LALT, SDL_KMOD_LALT));
    e.postEvent(key(false, SDLK_LALT));
    e.getInputCode();
    CHECK(m.tracking());
    CHECK_FALSE(m.dropped());
    e.postEvent(key(true, SDLK_LALT, SDL_KMOD_LALT));
    e.postEvent(key(false, SDLK_LALT));
    e.getInputCode();
    CHECK_FALSE(m.tracking());
    CHECK(r.ran.size() == 2);
}

TEST_CASE("menu bar: mouse") {
    TM_REQUIRE_GAME_DATA();
    Rig r(*g);
    Engine& e = *r.engine;
    MenuBar& m = *r.menu;
    m.install();

    // Press and release on "Options": it stays open; click "Voice".
    auto [tx, ty] = r.titleCentre(1);
    click(e, tx, ty);
    CHECK(e.getInputCode() == 0);
    CHECK(m.tracking());
    CHECK(m.openMenu() == 1);
    CHECK(m.dropped());
    CHECK(m.selectedItem() == -1);
    auto [ix, iy] = r.itemCentre(1, 2);
    e.postEvent(motion(ix + 0.5f, iy + 0.5f));
    e.getInputCode();
    CHECK(m.selectedItem() == 2);
    click(e, ix, iy);
    CHECK(e.getInputCode() == 0);
    CHECK_FALSE(m.tracking());
    REQUIRE(r.ran.size() == 1);
    CHECK(r.ran[0] == 0x0102);

    // Press on "File", drag to "Exit", release: chosen.
    auto [fx, fy] = r.titleCentre(0);
    auto [ex, ey] = r.itemCentre(0, 2);
    e.postEvent(motion(fx + 0.5f, fy + 0.5f));
    e.postEvent(button(true, fx + 0.5f, fy + 0.5f));
    e.postEvent(motion(ex + 0.5f, ey + 0.5f));
    e.postEvent(button(false, ex + 0.5f, ey + 0.5f));
    e.getInputCode();
    REQUIRE(r.ran.size() == 2);
    CHECK(r.ran[1] == 0x0002);

    // A click outside cancels and is swallowed; hovering another title switches menus.
    click(e, tx, ty);
    e.getInputCode();
    auto [hx, hy] = r.titleCentre(2);
    e.postEvent(motion(hx + 0.5f, hy + 0.5f));
    e.getInputCode();
    CHECK(m.openMenu() == 2);
    click(e, 320, 300);
    CHECK(e.getInputCode() == 0);
    CHECK_FALSE(m.tracking());
    CHECK(r.ran.size() == 2);

    // Clicks on the bar never reach the game, even off the titles; clicks below do.
    click(e, 600, 5);
    CHECK(e.getInputCode() == 0);
    CHECK_FALSE(m.tracking());
    e.hotspots().set(0x0777, 0, 0, 512, 384);
    click(e, 64 + 100, 48 + 100);
    std::vector<uint16_t> codes;
    for (int i = 0; i < 4; ++i)
        if (uint16_t c = e.getInputCode()) codes.push_back(c);
    CHECK(std::find(codes.begin(), codes.end(), 0x0777) != codes.end());

    // Disabled: a click on a title does not open it.
    m.enableAll(false);
    click(e, tx, ty);
    e.getInputCode();
    CHECK_FALSE(m.tracking());
    press(e, SDLK_O, SDL_KMOD_LALT);
    e.getInputCode();
    CHECK_FALSE(m.tracking());
}

TEST_CASE("menu bar: the menu loop freezes like an inactive window (activation hook)") {
    TM_REQUIRE_GAME_DATA();
    Rig r(*g);
    Engine& e = *r.engine;
    MenuBar& m = *r.menu;
    std::vector<int> act;
    e.setActivationHook([&](bool on) { act.push_back(on ? 1 : 0); });
    std::vector<PumpPhase> frozen;
    const int hook = e.addPumpHook([&](PumpPhase p) {
        if (p == PumpPhase::Frozen) frozen.push_back(p);
    });
    m.install();
    press(e, SDLK_F, SDL_KMOD_LALT);
    press(e, SDLK_DOWN);
    e.getInputCode();
    // Headless: the hold runs until the queued events are used up, deactivating around it.
    REQUIRE(act.size() == 2);
    CHECK(act[0] == 0);  // WM_ENTERMENULOOP -> App_Deactivate
    CHECK(act[1] == 1);  // WM_EXITMENULOOP -> App_Activate
    CHECK(m.tracking());
    // Events that arrive while the menu is open are handled inside the hold.
    act.clear();
    press(e, SDLK_DOWN);
    e.pump();  // a busy pump also runs the menu loop (but delivers no command)
    CHECK(m.selectedItem() == 2);
    press(e, SDLK_RETURN);
    e.pump();
    CHECK_FALSE(m.tracking());
    CHECK(m.commandPending());
    CHECK(r.ran.empty());  // busy pumps do not dispatch
    e.getInputCode();
    CHECK_FALSE(m.commandPending());
    REQUIRE(r.ran.size() == 1);
    CHECK(r.ran[0] == 0x0002);
    e.removePumpHook(hook);
}

TEST_CASE("menu bar: dispatch rules (disabled, resource load, re-entrancy, unknown)") {
    TM_REQUIRE_GAME_DATA();
    Rig r(*g);
    MenuBar& m = *r.menu;

    CHECK_FALSE(m.dispatch(0x0002));  // removed
    CHECK(m.stats().droppedDisabled == 1);
    m.install();
    m.enableAll(false);
    CHECK_FALSE(m.dispatch(0x0002));  // disabled
    CHECK(m.stats().droppedDisabled == 2);
    m.enableAll(true);
    {
        MenuBar::ResourceLoadScope load(&m);
        CHECK_FALSE(m.dispatch(0x0002));  // g_inResourceLoad
        CHECK(m.stats().droppedLoad == 1);
    }
    CHECK_FALSE(m.dispatch(0x0305));  // no such item
    CHECK(m.stats().droppedUnknown == 1);
    CHECK(r.ran.empty());

    // During the handler the menus are off; a nested command is dropped.
    bool sawDisabled = false, nestedAccepted = true;
    m.setHandler(MenuCommand::AboutTM, [&] {
        sawDisabled = !m.isEnabled() && m.inHandler();
        CHECK_FALSE(m.open(0, true, true));  // cannot open while a handler runs
        nestedAccepted = m.dispatch(0x0100);
    });
    CHECK(m.dispatch(0x0204));
    CHECK(sawDisabled);
    CHECK_FALSE(nestedAccepted);
    CHECK(m.stats().droppedReentrant == 1);
    CHECK(m.isEnabled());
    CHECK(m.stats().delivered == 1);

    // A handler that throws (Exit / Start Another Game) leaves the menu usable.
    m.setHandler(MenuCommand::Exit, [] { throw std::runtime_error("quit"); });
    CHECK_THROWS(m.dispatch(0x0002));
    CHECK_FALSE(m.inHandler());
    CHECK(m.isEnabled());

    // Queued commands are delivered only by an input fetch, and dropped if disabled meanwhile.
    m.postCommand(0x0200);
    r.engine->pump();
    CHECK(m.commandPending());
    m.enableAll(false);
    r.engine->getInputCode();
    CHECK_FALSE(m.commandPending());
    CHECK(m.stats().droppedDisabled == 3);
    m.enableAll(true);
    m.postCommand(0x0200);
    r.engine->getInputCode();
    REQUIRE(!r.ran.empty());
    CHECK(r.ran.back() == 0x0200);
    // A load that pumps (Input_Flush after a load) drops a chosen command.
    m.postCommand(0x0201);
    {
        MenuBar::ResourceLoadScope load(&m);
        r.engine->flushInput();
    }
    CHECK(m.stats().droppedLoad == 2);
    CHECK(r.ran.back() == 0x0200);
}

TEST_CASE("menu bar: headless render of the canvas with a menu open") {
    TM_REQUIRE_GAME_DATA();
    Rig r(*g);
    Engine& e = *r.engine;
    MenuBar& m = *r.menu;
    // The title picture as the game area.
    auto pm = g->res->pixmap(2100, mischief::gfx::ColorMode::Colour256);
    REQUIRE(pm);
    e.frame().drawOpaque(0, 20, *pm);
    e.presentAll();
    const std::vector<uint8_t> before(e.presenter().canvas().begin(), e.presenter().canvas().end());
    m.install();
    press(e, SDLK_O, SDL_KMOD_LALT);
    press(e, SDLK_DOWN);
    e.getInputCode();
    REQUIRE(m.openMenu() == 1);
    REQUIRE(m.selectedItem() == 1);

    const auto c = e.presenter().canvas();
    auto px = [&](int x, int y) {
        const size_t i = (static_cast<size_t>(y) * 640 + x) * 4;
        return std::array<uint8_t, 3>{c[i], c[i + 1], c[i + 2]};
    };
    using RGB = std::array<uint8_t, 3>;
    CHECK(px(630, 5) == RGB{255, 255, 255});                  // bar background
    CHECK(px(630, MenuBar::kBarHeight - 1) == RGB{0, 0, 0});  // bar bottom line
    const auto t = m.titleRect(1);
    CHECK(px(t.x + 1, t.y + 1) == RGB{0, 0, 128});            // highlighted title
    const auto box = m.dropDownRect(1);
    CHECK(px(box.x, box.y + 5) == RGB{0, 0, 0});              // drop-down frame
    const auto sel = m.itemRect(1, 1);
    CHECK(px(sel.x + 1, sel.y + 1) == RGB{0, 0, 128});        // selected item
    const auto other = m.itemRect(1, 0);
    CHECK(px(other.x + 1, other.y + 1) == RGB{255, 255, 255});
    // The game area outside the drop-down is untouched.
    for (int y = 48; y < 432; y += 37)
        for (int x = box.x + box.w + MenuBar::kShadow + 1; x < 576; x += 41) {
            const size_t i = (static_cast<size_t>(y) * 640 + x) * 4;
            CHECK(c[i] == before[i]);
        }

    const auto dir = outDir("menu_render");
    {
        CHECK(e.presenter().writeCanvasPng(dir / "menu_open_options.png"));
        // Disabled look (grey titles) for comparison.
        press(e, SDLK_ESCAPE);
        press(e, SDLK_ESCAPE);
        e.getInputCode();
        m.enableAll(false);
        CHECK(e.presenter().writeCanvasPng(dir / "menu_disabled.png"));
        m.enableAll(true);
        press(e, SDLK_H, SDL_KMOD_LALT);
        press(e, SDLK_UP);
        e.getInputCode();
        CHECK(e.presenter().writeCanvasPng(dir / "menu_open_help.png"));
    }
    // Closing restores the canvas exactly.
    press(e, SDLK_ESCAPE);
    press(e, SDLK_ESCAPE);
    e.getInputCode();
    m.remove();
    const auto after = e.presenter().canvas();
    CHECK(std::equal(after.begin(), after.end(), before.begin()));
}
