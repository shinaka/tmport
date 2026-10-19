// Platform layer: the window icon decoder (RT_GROUP_ICON / RT_ICON from the user's EXE), the
// fullscreen hot keys (Alt+Enter / F11, taken before the menu bar and the game), the fullscreen
// setting, and the edition-neutral host under the Engine (event order, window state, F12, frame
// dumps). Headless only: nothing opens a window.
#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <cstring>
#include <format>
#include <initializer_list>
#include <memory>
#include <vector>

#include "app/launch_options.h"
#include "app_test_util.h"
#include "data/exe_tables.h"
#include "ne/ne_image.h"
#include "platform/engine.h"
#include "platform/engine_setup.h"
#include "platform/exe_icon.h"
#include "platform/host.h"
#include "save/settings.h"
#include "test_data.h"
#include "text/font.h"
#include "timing/clock.h"
#include "ui/menu_bar.h"
#include "util/error.h"
#include "util/hash.h"
#include "util/png_writer.h"

using namespace tmport;
using namespace apptest;

namespace {

void bytes(std::vector<uint8_t>& v, std::initializer_list<int> b) {
    for (int x : b) v.push_back(static_cast<uint8_t>(x));
}
void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
    put16(v, static_cast<uint16_t>(x & 0xFFFF));
    put16(v, static_cast<uint16_t>(x >> 16));
}

// BITMAPINFOHEADER of an icon DIB (biHeight = 2 * height).
std::vector<uint8_t> infoHeader(int w, int h, int bpp, uint32_t clrUsed = 0) {
    std::vector<uint8_t> v;
    put32(v, 40);
    put32(v, static_cast<uint32_t>(w));
    put32(v, static_cast<uint32_t>(h * 2));
    put16(v, 1);
    put16(v, static_cast<uint16_t>(bpp));
    put32(v, 0);  // BI_RGB
    put32(v, 0);
    put32(v, 0);
    put32(v, 0);
    put32(v, clrUsed);
    put32(v, 0);
    return v;
}

const uint8_t* px(const IconImage& img, int x, int y) { return img.rgba.data() + (static_cast<size_t>(y) * img.width + x) * 4; }

// A tiny on-the-fly copy of the EXE's icon at scale for looking at it (TM_APP_SHOTS=<dir>).
std::vector<uint8_t> enlarge(const IconImage& img, int s, uint8_t bg) {
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

TEST_CASE("icon: 4-bpp DIB with a 16-colour table and an AND mask (synthetic)") {
    std::vector<uint8_t> d = infoHeader(8, 2, 4);
    for (int i = 0; i < 16; ++i) {  // RGBQUAD: B, G, R, 0 -> colour i = RGB(0x30+i, 0x20+i, 0x10+i)
        d.push_back(static_cast<uint8_t>(0x10 + i));
        d.push_back(static_cast<uint8_t>(0x20 + i));
        d.push_back(static_cast<uint8_t>(0x30 + i));
        d.push_back(0);
    }
    // XOR rows bottom-up, 4 bytes each: the bottom row holds indices 8..15, the top row 0..7.
    bytes(d, {0x89, 0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67});
    // AND rows bottom-up, padded to 4 bytes: bottom row opaque, top row transparent at x 0 and 7.
    bytes(d, {0x00, 0x00, 0x00, 0x00, 0x81, 0x00, 0x00, 0x00});

    const IconImage img = decodeIconResource(d);
    CHECK(img.width == 8);
    CHECK(img.height == 2);
    CHECK(img.bitCount == 4);
    REQUIRE(img.rgba.size() == 8 * 2 * 4);
    CHECK_FALSE(img.opaqueAt(0, 0));
    CHECK_FALSE(img.opaqueAt(7, 0));
    CHECK(img.invertedPixels == 2);  // colours 0 and 7 under AND 1 are not black
    for (int x = 1; x < 7; ++x) {
        const uint8_t* p = px(img, x, 0);
        CHECK(p[0] == 0x30 + x);
        CHECK(p[1] == 0x20 + x);
        CHECK(p[2] == 0x10 + x);
        CHECK(p[3] == 255);
    }
    for (int x = 0; x < 8; ++x) {
        const uint8_t* p = px(img, x, 1);
        CHECK(p[0] == 0x30 + 8 + x);
        CHECK(p[3] == 255);
    }
    // Transparent pixels carry no colour.
    CHECK(px(img, 0, 0)[0] == 0);
}

TEST_CASE("icon: 1-bpp DIB with a BITMAPCOREHEADER, 8 bpp, errors") {
    {
        std::vector<uint8_t> d;
        put32(d, 12);
        put16(d, 8);
        put16(d, 2);  // one row of XOR + one row of AND
        put16(d, 1);
        put16(d, 1);
        bytes(d, {0, 0, 0, 255, 255, 255});  // RGBTRIPLE black, white
        bytes(d, {0xF0, 0, 0, 0});           // x 0..3 white, 4..7 black
        bytes(d, {0x0F, 0, 0, 0});           // x 4..7 transparent
        const IconImage img = decodeIconResource(d);
        CHECK(img.width == 8);
        CHECK(img.height == 1);
        CHECK(img.invertedPixels == 0);
        for (int x = 0; x < 4; ++x) {
            CHECK(px(img, x, 0)[0] == 255);
            CHECK(img.opaqueAt(x, 0));
        }
        for (int x = 4; x < 8; ++x) CHECK_FALSE(img.opaqueAt(x, 0));
    }
    {
        // 8 bpp, 2-entry table (biClrUsed = 2), 4x1.
        std::vector<uint8_t> d = infoHeader(4, 1, 8, 2);
        bytes(d, {1, 2, 3, 0, 4, 5, 6, 0});
        std::vector<uint8_t> good = d;
        bytes(good, {0, 1, 1, 0});
        bytes(good, {0, 0, 0, 0});
        const IconImage img = decodeIconResource(good);
        CHECK(px(img, 1, 0)[0] == 6);  // entry 1 = B4 G5 R6
        CHECK(px(img, 0, 0)[2] == 1);
        std::vector<uint8_t> bad = d;
        bytes(bad, {0, 5, 1, 0, 0, 0, 0, 0});  // index 5 outside the table
        CHECK_THROWS_AS(decodeIconResource(bad), FormatError);
    }
    std::vector<uint8_t> odd = infoHeader(4, 1, 4);
    odd[8] = 3;  // biHeight 3: not XOR + AND
    CHECK_THROWS_AS(decodeIconResource(odd), FormatError);
    std::vector<uint8_t> hdr20(40, 0);
    hdr20[0] = 20;
    CHECK_THROWS_AS(decodeIconResource(hdr20), FormatError);
    CHECK_THROWS_AS(decodeIconResource(infoHeader(32, 32, 4)), FormatError);  // truncated
}

TEST_CASE("icon: RT_GROUP_ICON directory") {
    std::vector<uint8_t> d;
    put16(d, 0);
    put16(d, 1);
    put16(d, 2);
    bytes(d, {32, 32, 16, 0});
    put16(d, 1);
    put16(d, 4);
    put32(d, 744);
    put16(d, 1);
    bytes(d, {0, 0, 0, 0});  // 256 x 256, >= 256 colours, planes/bitCount 0
    put16(d, 0);
    put16(d, 0);
    put32(d, 1000);
    put16(d, 2);
    const auto e = parseIconDirectory(d);
    REQUIRE(e.size() == 2);
    CHECK(e[0].width == 32);
    CHECK(e[0].colourCount == 16);
    CHECK(e[0].bitCount == 4);
    CHECK(e[0].bytesInRes == 744);
    CHECK(e[0].iconId == 1);
    CHECK(e[1].width == 256);
    CHECK(e[1].height == 256);
    CHECK(e[1].colourCount == 256);
    CHECK(e[1].iconId == 2);
    d[2] = 2;  // a cursor directory
    CHECK_THROWS_AS(parseIconDirectory(d), FormatError);
}

TEST_CASE("icon: the window icon of TMTWINCD.EXE (RT_GROUP_ICON 15000)") {
    TM_REQUIRE_GAME_DATA();
    const ne::NeImage& exe = g->exe->exe();
    const ne::Resource* grp = exe.findResource(14, kWindowIconGroupId);
    REQUIRE(grp != nullptr);
    const auto dir = parseIconDirectory(exe.readResource(*grp));
    REQUIRE(dir.size() == 1);
    CHECK(dir[0].width == 32);
    CHECK(dir[0].height == 32);
    CHECK(dir[0].colourCount == 16);

    const IconImage icon = loadGroupIcon(exe, kWindowIconGroupId);
    CHECK(icon.width == 32);
    CHECK(icon.height == 32);
    CHECK(icon.bitCount == 4);
    CHECK(icon.invertedPixels == 0);
    int opaque = 0;
    for (int y = 0; y < icon.height; ++y)
        for (int x = 0; x < icon.width; ++x) opaque += icon.opaqueAt(x, y);
    MESSAGE(std::format("opaque pixels: {} of 1024, rgba hash {:#018x}", opaque, fnv1a64(icon.rgba)));
    // Mask sanity: a figure with a transparent surround, not a filled square. Expected values from an
    // independent decode (Python, and Pillow's ICO reader on the same DIB): 500 opaque pixels.
    CHECK(opaque == 500);
    CHECK_FALSE(icon.opaqueAt(0, 0));
    CHECK_FALSE(icon.opaqueAt(31, 0));
    CHECK_FALSE(icon.opaqueAt(0, 31));
    CHECK(fnv1a64(icon.rgba) == 0x056f43f6a40b1542ull);

    const auto dirOut = outDir("icon");
    CHECK(png::writeRgba(dirOut / "icon15000.png", icon.width, icon.height, icon.rgba));
    CHECK(png::writeRgba(dirOut / "icon15000_x8.png", icon.width * 8, icon.height * 8, enlarge(icon, 8, 200)));

    CHECK_THROWS_AS(loadGroupIcon(exe, 15001), Error);
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
    ManualClock clock;
    EngineConfig cfg;
    cfg.clock = &clock;
    Engine e(*g, gfx::ColorMode::Colour256, cfg);
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
    const auto s = save::EngineSettings::fromIni(save::IniFile::parse("[engine]\nfullscreen = true\n"));
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

TEST_CASE("host: event order, window state, hot keys and F12 (headless)") {
    ManualClock clock;
    platform::HostConfig cfg;
    cfg.layout = gfx::PresenterLayout::dos1990();
    cfg.clock = &clock;
    cfg.screenshotDir = outDir("host_shots");
    cfg.screenshotPrefix = "hosttest";
    platform::Host host(cfg);
    CHECK_FALSE(host.hasWindow());
    CHECK(host.layout() == gfx::PresenterLayout::dos1990());

    std::vector<std::string> log;
    platform::HostHandlers h;
    h.filter = [&](const SDL_Event& e) {
        const bool eat = e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_A;
        if (eat) log.push_back("filter:a");
        return eat;
    };
    h.window = [&](const SDL_Event& e) { log.push_back(std::format("window:{}", static_cast<unsigned>(e.type))); };
    h.input = [&](const SDL_Event& e) {
        if (e.type == SDL_EVENT_TEXT_INPUT) log.push_back(std::string("text:") + e.text.text);
        else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP)
            log.push_back(std::format("{}:{}", e.type == SDL_EVENT_KEY_DOWN ? "down" : "up", static_cast<unsigned>(e.key.key)));
        else log.push_back(std::format("input:{}", static_cast<unsigned>(e.type)));
    };
    int activations = 0;
    h.activation = [&](bool) { ++activations; };
    host.setHandlers(std::move(h));

    host.postEvent(simple(SDL_EVENT_WINDOW_FOCUS_LOST));
    host.postEvent(key(true, SDLK_A));
    host.postEvent(key(true, SDLK_B));
    {
        std::string text = "xyz";
        host.postEvent(textInput(text.c_str()));
        text = "---";  // the host keeps its own copy
    }
    CHECK(host.pendingPostedEvents() == 4);
    host.dispatchPending();
    CHECK(host.pendingPostedEvents() == 0);
    CHECK_FALSE(host.focused());
    REQUIRE(log.size() == 4);
    CHECK(log[0] == std::format("window:{}", static_cast<unsigned>(SDL_EVENT_WINDOW_FOCUS_LOST)));
    CHECK(log[1] == "filter:a");
    CHECK(log[2] == std::format("down:{}", static_cast<unsigned>(SDLK_B)));
    CHECK(log[3] == "text:xyz");
    // Headless nothing freezes, whatever the focus.
    CHECK_FALSE(host.paused());
    host.pump();
    CHECK(activations == 0);
    host.setAppActive(false);
    host.setAppActive(false);
    host.setAppActive(true);
    CHECK(activations == 2);

    // Alt+Enter toggles fullscreen; the edition sees only the Alt press and its release.
    log.clear();
    host.postEvent(key(true, SDLK_LALT, SDL_KMOD_LALT));
    host.postEvent(key(true, SDLK_RETURN, SDL_KMOD_LALT));
    host.postEvent(key(false, SDLK_RETURN, SDL_KMOD_LALT));
    host.postEvent(key(false, SDLK_LALT));
    host.dispatchPending();
    CHECK(host.fullscreen());
    REQUIRE(log.size() == 2);
    CHECK(log[0] == std::format("down:{}", static_cast<unsigned>(SDLK_LALT)));
    CHECK(log[1] == std::format("up:{}", static_cast<unsigned>(SDLK_LALT)));
    press(host, SDLK_F11);
    host.dispatchPending();
    CHECK_FALSE(host.fullscreen());

    // F12 writes the game area (320x200) into the screenshot folder; the edition does not see it.
    log.clear();
    std::vector<uint32_t> frame(320 * 200, 0xFF5500AAu);
    host.presenter().presentFrame(frame);
    press(host, SDLK_F12);
    host.dispatchPending();
    CHECK(log.size() == 1);  // only the release
    int shots = 0;
    for (const auto& f : std::filesystem::directory_iterator(cfg.screenshotDir))
        if (pathToUtf8(f.path().filename()).starts_with("hosttest_")) ++shots;
    CHECK(shots == 1);

    // Close requests: sticky flag, counted, and delivered to the edition.
    log.clear();
    host.postEvent(simple(SDL_EVENT_WINDOW_CLOSE_REQUESTED));
    host.dispatchPending();
    CHECK(host.quitRequested());
    CHECK(host.closeRequests() == 1);
    REQUIRE(log.size() == 1);
    CHECK(log[0] == std::format("input:{}", static_cast<unsigned>(SDL_EVENT_WINDOW_CLOSE_REQUESTED)));
    host.clearQuitRequest();
    CHECK_FALSE(host.quitRequested());

    // The clock: a custom clock drives nowNs.
    CHECK(host.nowNs() == clock.nowMs() * 1'000'000ull);
    clock.advance(5);
    CHECK(host.nowNs() == clock.nowMs() * 1'000'000ull);

    // Frame dump of the canvas: 320x200.
    const auto dump = cfg.screenshotDir / "dump.png";
    REQUIRE(host.writeFrameDump(dump));
    auto data = readWholeFile(dump);
    REQUIRE(data);
    REQUIRE(data->size() > 24);
    CHECK((*data)[19] == 64);   // width 320 = 0x140 (low byte of the big-endian u32 at 16)
    CHECK((*data)[18] == 1);
    CHECK((*data)[23] == 200);  // height
}

TEST_CASE("engine: built on a host with the Windows layout") {
    TM_REQUIRE_GAME_DATA();
    ManualClock clock;
    EngineConfig cfg;
    cfg.clock = &clock;
    cfg.windowScale = 0;  // treated as 1, as before
    Engine e(*g, gfx::ColorMode::Colour256, cfg);
    CHECK(e.host().layout() == gfx::PresenterLayout::windowsCd());
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
