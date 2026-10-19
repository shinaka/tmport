// The DOS edition's entry point and its wave-2 preview (dos/app/dos_entry.h, dos_preview.h): the
// screen cycle through the DOS keyboard path, each frame equal to the composer's screen, the sound
// calls per screen (title tune, the pause in UI modes, area tunes, effects, Ctrl+S), the offline
// sound equal to the dos_audio render, and headless runs through the start-up with a frame-dump
// folder and a DOS --keys script. Needs TM_DATA_DOS; headless only (no window, nothing audible).
#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "app/bootstrap.h"
#include "app/launch_options.h"
#include "app_test_util.h"
#include "dos/app/dos_preview.h"
#include "dos/audio/dos_sound_renderer.h"
#include "dos_test_data.h"
#include "rtl/borland_rand.h"
#include "util/file_util.h"
#include "util/hash.h"
#include "vfs/directory_source.h"

using namespace tmport;
using namespace tmport::dos;
namespace fs = std::filesystem;

namespace {

uint64_t hashPixels(std::span<const uint32_t> px) {
    return fnv1a64(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(px.data()), px.size_bytes()));
}

float maxAbs(const std::vector<float>& s, size_t from, size_t to) {
    float m = 0.0f;
    for (size_t i = from; i < std::min(to, s.size()); ++i) m = std::max(m, std::abs(s[i]));
    return m;
}

std::pair<uint32_t, uint32_t> pngSize(const fs::path& p) {
    auto data = readWholeFile(p);
    if (!data || data->size() < 24) return {0, 0};
    auto be32 = [&](size_t o) {
        return (uint32_t((*data)[o]) << 24) | (uint32_t((*data)[o + 1]) << 16) | (uint32_t((*data)[o + 2]) << 8) | (*data)[o + 3];
    };
    return {be32(16), be32(20)};
}

PcKey key(const char* name) {
    const auto k = pcKeyByName(name);
    REQUIRE(k.has_value());
    return *k;
}

}  // namespace

TEST_CASE("DOS preview: Enter cycles the screens, frames are the composer's, sound follows the original's calls") {
    TM_REQUIRE_DOS_DATA();
    const DosGfxFiles files = DosGfxFiles::open(*testdata::dosDataPath());
    DosOptions opt;
    opt.look = Look::Ega;
    opt.sound = SoundDevice::Tandy;
    opt.headless = true;
    DosRuntime rt;
    BorlandRand rnd;
    auto audio = makeDosAudioDevice(files.files(), opt.sound, [&rnd] { return rnd.rand(); });
    REQUIRE(audio);
    CHECK(audio->device() == SoundCard::Tandy);
    CHECK(makeDosAudioDevice(files.files(), SoundDevice::Off) == nullptr);
    DosPreview pv(files, opt, rt, {}, audio.get(), true);
    pv.keepOfflineSamples(true);
    rt.setClient(&pv);
    std::vector<std::pair<DosScreen, uint64_t>> frames;
    pv.setFrameSink([&](DosScreen s, std::span<const uint32_t> px) {
        CHECK(px.size() == 320u * 200u);
        frames.emplace_back(s, hashPixels(px));
    });
    pv.start();
    REQUIRE(frames.size() == 1);
    CHECK(frames[0].first == DosScreen::Title);

    ScriptedInput sc;
    for (uint64_t t : {20, 40, 60, 80, 100}) sc.tap(t, key("enter"));
    sc.tap(90, key("5"));  // effect 5 on the path
    sc.keyDown(120, key("lctrl"));  // Ctrl+S in the castle: music and sound off
    sc.tap(121, key("s"));
    sc.keyUp(123, key("lctrl"));
    sc.tap(140, key("space"));  // the title again (music stays off)
    sc.keyDown(150, key("lctrl"));  // Ctrl+S on the title: on again, tune(6) = 1
    sc.tap(151, key("s"));
    sc.keyUp(153, key("lctrl"));
    sc.tap(170, key("esc"));
    rt.setScript(sc);

    auto stepTo = [&](uint64_t n) { rt.stepGameTicks(n - rt.gameTicks()); };
    auto mark = [&] { return pv.offlineSamples().size(); };
    stepTo(10);
    CHECK(audio->status().musicId == 1);  // Title_Run: Music_Play(1)
    CHECK(audio->status().musicStatus != 0);
    const size_t title0 = mark();
    stepTo(19);
    CHECK(maxAbs(pv.offlineSamples(), title0, mark()) > 0.01f);
    stepTo(25);  // sign-in: g_uiMode 8 pauses the music (Sound_SyncState)
    CHECK(pv.screen() == DosScreen::SignIn);
    const size_t sign0 = mark();
    stepTo(39);
    CHECK(maxAbs(pv.offlineSamples(), sign0, mark()) < 0.01f);
    stepTo(45);  // clubhouse: Game_Start, tune(5) = 1, g_uiMode 0
    CHECK(pv.screen() == DosScreen::Clubhouse);
    CHECK(pv.area() == 5);
    CHECK(audio->status().musicId == 1);
    const size_t club0 = mark();
    stepTo(59);
    CHECK(maxAbs(pv.offlineSamples(), club0, mark()) > 0.01f);
    stepTo(65);  // quiz: area 0, tune 0, paused by g_uiMode 2
    CHECK(pv.screen() == DosScreen::Quiz);
    CHECK(audio->status().musicId == 0);
    const size_t quiz0 = mark();
    stepTo(79);
    CHECK(maxAbs(pv.offlineSamples(), quiz0, mark()) < 0.01f);
    stepTo(85);  // path: the music runs again
    CHECK(pv.screen() == DosScreen::Path);
    const size_t path0 = mark();
    stepTo(89);
    CHECK(maxAbs(pv.offlineSamples(), path0, mark()) > 0.01f);
    CHECK(audio->status().sfxStatus == 0);
    stepTo(92);
    // Playing (on the Tandy Sfx_Play's own Sfx_Stop sets the id back to 0xFFFF: no priority).
    CHECK(audio->status().sfxStatus != 0);
    stepTo(105);  // castle: area 0x0D, tune 2
    CHECK(pv.screen() == DosScreen::Castle);
    CHECK(pv.area() == 0x0D);
    CHECK(audio->status().musicId == 2);
    stepTo(130);
    CHECK_FALSE(pv.musicOn());
    CHECK(audio->status().musicStatus == 0);
    stepTo(145);
    CHECK(pv.screen() == DosScreen::Title);
    CHECK(audio->status().musicStatus == 0);  // Music_Play with the music off leaves it stopped
    stepTo(160);
    CHECK(pv.musicOn());
    CHECK(audio->status().musicId == 1);
    CHECK(audio->status().musicStatus != 0);
    stepTo(175);
    CHECK_FALSE(rt.running());  // Esc

    // Seven frames, in the order of kPreviewScreens, each the composer's screen in the look's palette.
    const std::vector<DosScreen> order = {DosScreen::Title, DosScreen::SignIn, DosScreen::Clubhouse, DosScreen::Quiz,
                                          DosScreen::Path,  DosScreen::Castle, DosScreen::Title};
    REQUIRE(frames.size() == order.size());
    const LookPalette pal = lookPalette(files.exe(), Look::Ega);
    for (size_t i = 0; i < order.size(); ++i) {
        CAPTURE(i);
        CHECK(frames[i].first == order[i]);
        const auto expected = toRgba32(composeScreen(files, defaultScreenSpec(order[i], Look::Ega)), pal);
        CHECK(frames[i].second == hashPixels(expected));
    }
}

TEST_CASE("DOS preview: the mouse pointer follows the mouse on the title; a click shows the next screen") {
    TM_REQUIRE_DOS_DATA();
    const DosGfxFiles files = DosGfxFiles::open(*testdata::dosDataPath());
    DosOptions opt;
    opt.look = Look::Mcga;
    opt.headless = true;
    DosRuntime rt;
    DosPreview pv(files, opt, rt, {}, nullptr, true);
    rt.setClient(&pv);
    std::vector<uint64_t> frames;
    pv.setFrameSink([&](DosScreen, std::span<const uint32_t> px) { frames.push_back(hashPixels(px)); });
    pv.start();
    ScriptedInput sc;
    ScriptEvent move;
    move.tick = 5;
    move.kind = ScriptEvent::Kind::MouseMove;
    move.x = 100;
    move.y = 60;
    sc.add(move);
    ScriptEvent press = move;
    press.tick = 10;
    press.kind = ScriptEvent::Kind::MouseButtons;
    press.buttons = MouseDriver::kLeft;
    sc.add(press);
    rt.setScript(sc);
    rt.stepGameTicks(12);
    REQUIRE(frames.size() == 3);
    DosScreenSpec spec = defaultScreenSpec(DosScreen::Title, Look::Mcga);
    spec.pointer = {{50, 60}};  // logical x 100 = 50 bytes
    const LookPalette pal = lookPalette(files.exe(), Look::Mcga);
    CHECK(frames[1] == hashPixels(toRgba32(composeScreen(files, spec), pal)));
    CHECK(frames[1] != frames[0]);
    CHECK(pv.screen() == DosScreen::SignIn);
}

TEST_CASE("DOS preview: the offline sound equals the dos_audio render of the title tune") {
    TM_REQUIRE_DOS_DATA();
    const fs::path folder = *testdata::dosDataPath();
    const DosGfxFiles files = DosGfxFiles::open(folder);
    for (SoundDevice dev : {SoundDevice::Tandy, SoundDevice::Speaker}) {
        CAPTURE(static_cast<int>(dev));
        const SoundCard card = dev == SoundDevice::Tandy ? SoundCard::Tandy : SoundCard::Speaker;
        // The CLI's render (tmport --dos-sound <dev> tune 1, dos_sound_cli.cpp): a fresh renderer on
        // the folder's files, smooth mode at 48 kHz; the WAV is these samples x 0.7.
        auto tables = std::make_shared<const SoundTables>(SoundTables::fromExe(files.exe()));
        const SoundBank bank = SoundBank::load(vfs::DirectorySource(folder), card, tables);
        DosSoundRenderer r(bank, DosRenderConfig{});
        const OfflineRender cli = renderEntry(r, true, 1, 1, 12.0);  // the first 12 s
        const size_t n = cli.samples.size() - static_cast<size_t>(48000 * 0.2) - 512;  // without the ring-out and last tick

        DosOptions opt;
        opt.sound = dev;
        opt.headless = true;
        DosRuntime rt;
        auto audio = makeDosAudioDevice(files.files(), dev);
        DosPreview pv(files, opt, rt, {}, audio.get(), true);
        pv.keepOfflineSamples(true);
        rt.setClient(&pv);
        pv.start();
        while (pv.offlineSamples().size() < n) rt.stepGameTicks(10);
        const std::vector<float>& s = pv.offlineSamples();
        size_t differ = 0;
        for (size_t i = 0; i < n; ++i)
            if (s[i] != std::clamp(static_cast<float>(cli.samples[i]) * 0.7f, -1.0f, 1.0f)) ++differ;
        CHECK(differ == 0);
        CHECK(maxAbs(s, 0, n) > 0.05f);
    }
}

TEST_CASE("DOS preview: headless start-up with a frame-dump folder and a DOS --keys script") {
    TM_REQUIRE_DOS_DATA();
    const bool videoBefore = SDL_WasInit(SDL_INIT_VIDEO) != 0;
    const bool audioBefore = SDL_WasInit(SDL_INIT_AUDIO) != 0;
    const fs::path dir = apptest::outDir("dos_preview_run");
    std::ofstream(dir / "tmport.ini") << "[saves]\nimported_original = true\n";
    const fs::path dump = dir / "frames";
    const std::string data = pathToUtf8(*testdata::dosDataPath());
    const std::string pref = pathToUtf8(dir), ini = pathToUtf8(dir / "tmport.ini"), out = pathToUtf8(dump);
    std::vector<const char*> argv = {"tmport", "--edition", "dos", "--dos-preview", "--data", data.c_str(), "--headless", "--pref-dir", pref.c_str(),
                                     "--config", ini.c_str(), "--look", "tandy", "--frame-dump", out.c_str(),
                                     "--keys", "10:tap:enter, 20:tap:enter, 30:tap:3"};
    std::vector<std::string> errors;
    auto sink = [&](const app::StartupError& e) { errors.push_back(e.what()); };
    CHECK(app::runGame(static_cast<int>(argv.size()), const_cast<char**>(argv.data()), {}, sink) == 0);
    CHECK(errors.empty());
    for (const char* f : {"00_title_tandy.png", "01_signin_tandy.png", "02_clubhouse_tandy.png"}) {
        CAPTURE(f);
        CHECK(pngSize(dump / f) == std::pair<uint32_t, uint32_t>{320, 200});
    }
    CHECK_FALSE(fs::exists(dump / "03_quiz_tandy.png"));
    CHECK(fs::file_size(dump / "sound.wav") > 44u);
    // The engine's DOS save was imported from the folder (when it has a USERS.DAT).
    if (testdata::dosFiles() && testdata::dosFiles()->hasUsersDat()) CHECK(fs::file_size(dir / "dos_users.dat") == 3243u);
    if (!videoBefore) CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    if (!audioBefore) CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);

    // A script neither edition accepts is refused before anything starts (exit code 2).
    argv.back() = "10:hop:enter";
    CHECK(app::runGame(static_cast<int>(argv.size()), const_cast<char**>(argv.data()), {}, sink) == 2);
    // A Windows script with --edition dos: refused by the DOS parser.
    argv.back() = "enter,esc";
    CHECK(app::runGame(static_cast<int>(argv.size()), const_cast<char**>(argv.data()), {}, sink) == 2);
}
