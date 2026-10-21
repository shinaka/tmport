// Platform layer: the window icon decoder (RT_GROUP_ICON / RT_ICON from the user's EXE), the
// fullscreen hot keys (Alt+Enter / F11, taken before the menu bar and the game), the fullscreen
// setting, and the edition-neutral host under the Engine (event order, window state, F12, frame
// dumps). Headless only: nothing opens a window.
#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <cstring>
#include <format>
#include <memory>
#include <vector>

#include "app/launch_options.h"
#include "data/exe_tables.h"
#include "mischief/platform/host.h"
#include "mischief/test/app_test_util.h"
#include "mischief/timing/clock.h"
#include "mischief/util/error.h"
#include "mischief/util/hash.h"
#include "mischief/util/png_writer.h"
#include "ne/ne_image.h"
#include "platform/engine.h"
#include "platform/engine_setup.h"
#include "platform/exe_icon_ne.h"
#include "save/settings.h"
#include "test_data.h"
#include "text/font.h"
#include "ui/menu_bar.h"

using namespace tmport;
using namespace apptest;

namespace {

const uint8_t* px(const mischief::IconImage& img, int x, int y) { return img.rgba.data() + (static_cast<size_t>(y) * img.width + x) * 4; }

// A tiny on-the-fly copy of the EXE's icon at scale for looking at it (TM_APP_SHOTS=<dir>).
std::vector<uint8_t> enlarge(const mischief::IconImage& img, int s, uint8_t bg) {
    std::vector<uint8_t> out(static_cast<size_t>(img.width) * s * img.height * s * 4);
    for (int y = 0; y < img.height * s; ++y)
        for (int x = 0; x < img.width * s; ++x) {
            const uint8_t* p = px(img, x / s, y / s);
            uint8_t* d = out.data() + (static_cast<size_t>(y) * img.width * s + x) * 4;
            const bool on = p[3] != 0;
            const uint8_t check = ((x / 8 + y / 8) & 1) ? bg : static_cast<uint8_t>(bg - 40);
            d[0] = on ? p[0] : check;
            d[1] = on ? p[1] : check;
            d[2] = on ? p[2] : check;
            d[3] = 255;
        }
    return out;
}

}  // namespace

// ------------------------------------------------------------------------------------ icons

TEST_CASE("icon: the window icon of TMTWINCD.EXE (RT_GROUP_ICON 15000)") {
    TM_REQUIRE_GAME_DATA();
    const ne::NeImage& exe = g->exe->exe();
    const ne::Resource* grp = exe.findResource(14, kWindowIconGroupId);
    REQUIRE(grp != nullptr);
    const auto dir = mischief::parseIconDirectory(exe.readResource(*grp));
    REQUIRE(dir.size() == 1);
    CHECK(dir[0].width == 32);
    CHECK(dir[0].height == 32);
    CHECK(dir[0].colourCount == 16);

    const mischief::IconImage icon = loadGroupIcon(exe, kWindowIconGroupId);
    CHECK(icon.width == 32);
    CHECK(icon.height == 32);
    CHECK(icon.bitCount == 4);
    CHECK(icon.invertedPixels == 0);
    int opaque = 0;
    for (int y = 0; y < icon.height; ++y)
        for (int x = 0; x < icon.width; ++x) opaque += icon.opaqueAt(x, y);
    MESSAGE(std::format("opaque pixels: {} of 1024, rgba hash {:#018x}", opaque, mischief::fnv1a64(icon.rgba)));
    // Mask sanity: a figure with a transparent surround, not a filled square. Expected values from an
    // independent decode (Python, and Pillow's ICO reader on the same DIB): 500 opaque pixels.
    CHECK(opaque == 500);
    CHECK_FALSE(icon.opaqueAt(0, 0));
    CHECK_FALSE(icon.opaqueAt(31, 0));
    CHECK_FALSE(icon.opaqueAt(0, 31));
    CHECK(mischief::fnv1a64(icon.rgba) == 0x056f43f6a40b1542ull);

    const auto dirOut = outDir("icon");
    CHECK(mischief::png::writeRgba(dirOut / "icon15000.png", icon.width, icon.height, icon.rgba));
    CHECK(mischief::png::writeRgba(dirOut / "icon15000_x8.png", icon.width * 8, icon.height * 8, enlarge(icon, 8, 200)));

    CHECK_THROWS_AS(loadGroupIcon(exe, 15001), mischief::Error);
}

// ------------------------------------------------------------------------------ fullscreen

namespace {

SDL_Event keyEv(bool down, SDL_Keycode k, SDL_Keymod mod, bool repeat = false) {
    SDL_Event e = key(down, k, mod);
    e.key.repeat = repeat;
    return e;
}

// Alt down, Enter down/up with Alt, Alt up: what the keyboard sends for Alt+Enter.
template <class E>
void altEnter(E& eng) {
    eng.postEvent(keyEv(true, SDLK_LALT, SDL_KMOD_LALT));
    eng.postEvent(keyEv(true, SDLK_RETURN, SDL_KMOD_LALT));
    eng.postEvent(keyEv(false, SDLK_RETURN, SDL_KMOD_LALT));
    eng.postEvent(keyEv(false, SDLK_LALT, SDL_KMOD_NONE));
}

}  // namespace

TEST_CASE("engine: Alt+Enter and F11 toggle fullscreen; the menu bar and the game never see them") {
    TM_REQUIRE_GAME_DATA();
    mischief::ManualClock clock;
    EngineConfig cfg;
    cfg.clock = &clock;
    Engine e(*g, mischief::gfx::ColorMode::Colour256, cfg);
    const text::Font font = text::Font::load(*g->res);
    ui::MenuBar menu(e, ui::MenuBarModel::fromExe(*g->exe), font);
    menu.install();
    REQUIRE_FALSE(e.fullscreen());

    altEnter(e);
    CHECK(e.getInputCode() == 0);  // no 0x0D for the game
    CHECK(e.fullscreen());
    CHECK_FALSE(menu.tracking());  // the Alt release did not open the bar
    CHECK(e.getInputCode() == 0);

    press(e, SDLK_F11);
    CHECK(e.getInputCode() == 0);
    CHECK_FALSE(e.fullscreen());
    e.postEvent(keyEv(true, SDLK_F11, SDL_KMOD_NONE, true));  // auto-repeat: no toggle
    e.postEvent(keyEv(false, SDLK_F11, SDL_KMOD_NONE));
    CHECK(e.getInputCode() == 0);
    CHECK_FALSE(e.fullscreen());

    // Ctrl+Alt+Enter (AltGr+Enter) and Ctrl+F11 are not hot keys.
    press(e, SDLK_RETURN, static_cast<SDL_Keymod>(SDL_KMOD_LALT | SDL_KMOD_LCTRL));
    press(e, SDLK_F11, SDL_KMOD_LCTRL);
    e.getInputCode();
    CHECK_FALSE(e.fullscreen());

    // A lone Alt still opens the bar afterwards.
    e.postEvent(keyEv(true, SDLK_LALT, SDL_KMOD_LALT));
    e.postEvent(keyEv(false, SDLK_LALT, SDL_KMOD_NONE));
    e.getInputCode();
    REQUIRE(menu.tracking());
    CHECK_FALSE(menu.dropped());

    // Inside the menu loop: Alt+Enter toggles and neither chooses nor closes anything.
    press(e, SDLK_DOWN);  // drop the File menu, first item selected
    e.getInputCode();
    REQUIRE(menu.dropped());
    const int item = menu.selectedItem();
    altEnter(e);
    e.getInputCode();
    CHECK(e.fullscreen());
    CHECK(menu.tracking());
    CHECK(menu.dropped());
    CHECK(menu.selectedItem() == item);
    CHECK_FALSE(menu.commandPending());
    CHECK(menu.stats().delivered == 0);
    press(e, SDLK_ESCAPE);
    press(e, SDLK_ESCAPE);
    e.getInputCode();
    CHECK_FALSE(menu.tracking());

    // Plain Enter still reaches the game.
    press(e, SDLK_RETURN);
    CHECK(e.getInputCode() == 0x0D);

    // Scripted Alt+Enter (--keys "alt+enter": no separate Alt key events): a later lone Alt still
    // opens the bar, because a new Alt press ends the swallowing of the release.
    press(e, SDLK_RETURN, SDL_KMOD_LALT);
    e.getInputCode();
    CHECK_FALSE(e.fullscreen());
    e.postEvent(keyEv(true, SDLK_LALT, SDL_KMOD_LALT));
    e.postEvent(keyEv(false, SDLK_LALT, SDL_KMOD_NONE));
    e.getInputCode();
    CHECK(menu.tracking());
    press(e, SDLK_ESCAPE);
    e.getInputCode();
    CHECK_FALSE(menu.tracking());
    e.setFullscreen(true);
    e.setFullscreen(false);
    CHECK_FALSE(e.fullscreen());
}

TEST_CASE("fullscreen setting: tmport.ini [engine] fullscreen and --fullscreen / --windowed") {
    const auto s = save::EngineSettings::fromIni(mischief::save::IniFile::parse("[engine]\nfullscreen = true\n"));
    CHECK(s.fullscreen);
    CHECK(engineConfigFromSettings(s).fullscreen);
    CHECK_FALSE(engineConfigFromSettings(save::EngineSettings{}).fullscreen);

    auto parse = [](std::vector<const char*> a) {
        a.insert(a.begin(), "tmport");
        std::string err;
        return app::parseLaunchArgs(static_cast<int>(a.size()), const_cast<char**>(a.data()), err);
    };
    auto o = parse({"--fullscreen"});
    REQUIRE(o);
    CHECK(o->fullscreen);
    CHECK_FALSE(o->windowed);
    o = parse({"--fullscreen", "--windowed"});
    REQUIRE(o);
    CHECK_FALSE(o->fullscreen);
    CHECK(o->windowed);
    std::vector<const char*> a = {"tmport", "--windowed", "--data", "X"};
    CHECK(app::isGameInvocation(static_cast<int>(a.size()), const_cast<char**>(a.data())));
}

// ------------------------------------------------------------------------------------- host

TEST_CASE("engine: built on a host with the Windows layout") {
    TM_REQUIRE_GAME_DATA();
    mischief::ManualClock clock;
    EngineConfig cfg;
    cfg.clock = &clock;
    cfg.windowScale = 0;  // treated as 1, as before
    Engine e(*g, mischief::gfx::ColorMode::Colour256, cfg);
    CHECK(e.host().layout() == mischief::gfx::PresenterLayout::windowsCd());
    CHECK(&e.presenter() == &e.host().presenter());
    CHECK(&e.clock() == &clock);
    CHECK(e.host().config().windowScale == 1);
    CHECK(e.host().config().screenshotPrefix == "tmport");
    CHECK(e.presenter().canvas().size() == 640u * 480u * 4u);
    // Events posted through the engine wait in the host until the next fetch.
    press(e, SDLK_RETURN);
    CHECK(e.pendingPostedEvents() == 2);
    CHECK(e.host().pendingPostedEvents() == 2);
    CHECK(e.getInputCode() == 0x0D);
    CHECK(e.pendingPostedEvents() == 0);
    // The host's close request reaches the input model as 0x1B.
    e.postEvent(simple(SDL_EVENT_WINDOW_CLOSE_REQUESTED));
    CHECK(e.getInputCode() == 0x1B);
    CHECK(e.quitRequested());
}
