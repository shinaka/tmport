// Starting the DOS game from DOSBox-X save states (wave 4 harness): the shared inflate
// (util/inflate.h), the save-state reader (dos/save/dosbox_state.h) on synthetic states, and the
// loader (dos/game/load_state.h) on every state of the user's reference library (TM_DOS_CAPTURES):
// the files it finds in RAM, the round trip DGROUP -> engine -> DGROUP, and the picture the engine
// composes from the loaded state against the state's video memory. Only counts and numbers are
// stored here (no pixels, no game text).
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "app/bootstrap.h"
#include "app/launch_options.h"
#include "app_test_util.h"
#include "dos/data/dgroup_offsets.h"
#include "dos/game/game.h"
#include "dos/game/load_state.h"
#include "dos/gfx/palettes.h"
#include "dos/save/dosbox_state.h"
#include "dos_game_fixture.h"
#include "dos_test_data.h"
#include "mods/png_codec.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/inflate.h"
#include "util/png_writer.h"

using namespace tmport;
using namespace tmport::dos;

namespace {

std::vector<uint8_t> patternBytes(size_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1103515245u + 12345u;
        v[i] = static_cast<uint8_t>((i % 97 < 40) ? (i & 7) : (seed >> 16));  // runs and noise
    }
    return v;
}

// A zip archive of stored entries (what a DOSBox-X .sav is, with deflate replaced by "stored").
std::vector<uint8_t> storedZip(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& entries) {
    std::vector<uint8_t> z, central;
    auto put16 = [](std::vector<uint8_t>& o, uint32_t v) {
        o.push_back(static_cast<uint8_t>(v));
        o.push_back(static_cast<uint8_t>(v >> 8));
    };
    auto put32 = [&put16](std::vector<uint8_t>& o, uint32_t v) {
        put16(o, v & 0xFFFF);
        put16(o, v >> 16);
    };
    for (const auto& [name, data] : entries) {
        const auto local = static_cast<uint32_t>(z.size());
        put32(z, 0x04034b50);
        put16(z, 20), put16(z, 0), put16(z, 0), put16(z, 0), put16(z, 0);
        put32(z, 0), put32(z, static_cast<uint32_t>(data.size())), put32(z, static_cast<uint32_t>(data.size()));
        put16(z, static_cast<uint32_t>(name.size())), put16(z, 0);
        z.insert(z.end(), name.begin(), name.end());
        z.insert(z.end(), data.begin(), data.end());
        put32(central, 0x02014b50);
        put16(central, 20), put16(central, 20), put16(central, 0), put16(central, 0), put16(central, 0), put16(central, 0);
        put32(central, 0), put32(central, static_cast<uint32_t>(data.size())), put32(central, static_cast<uint32_t>(data.size()));
        put16(central, static_cast<uint32_t>(name.size())), put16(central, 0), put16(central, 0), put16(central, 0), put16(central, 0);
        put32(central, 0), put32(central, local);
        central.insert(central.end(), name.begin(), name.end());
    }
    const auto cdOffset = static_cast<uint32_t>(z.size());
    z.insert(z.end(), central.begin(), central.end());
    put32(z, 0x06054b50);
    put16(z, 0), put16(z, 0), put16(z, static_cast<uint32_t>(entries.size())), put16(z, static_cast<uint32_t>(entries.size()));
    put32(z, static_cast<uint32_t>(central.size())), put32(z, cdOffset), put16(z, 0);
    return z;
}

std::filesystem::path tempFile(const std::string& name) {
    std::error_code ec;
    auto dir = std::filesystem::temp_directory_path(ec) / "tmport_tests";
    std::filesystem::create_directories(dir, ec);
    return dir / name;
}

// RAM of a synthetic state: SST.EXE's DGROUP at segment `dsSeg` (the Turbo C copyright at DS:0004).
std::vector<uint8_t> syntheticRam(uint16_t dsSeg) {
    std::vector<uint8_t> ram(0xA0000, 0);
    const std::string_view sig = "Turbo-C - Copyright (c) 1988 Borland Intl.";
    std::memcpy(ram.data() + dsSeg * 16u + 4u, sig.data(), sig.size());
    return ram;
}

void put16(std::vector<uint8_t>& ram, uint32_t at, uint16_t v) {
    ram[at] = static_cast<uint8_t>(v);
    ram[at + 1] = static_cast<uint8_t>(v >> 8);
}

std::vector<std::filesystem::path> referenceStates(const std::filesystem::path& captures) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const char* sub : {"ega", "r2", "tandy"})
        for (const auto& e : std::filesystem::directory_iterator(captures / "savestates" / sub, ec))
            if (e.path().extension() == ".sav") out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

// ------------------------------------------------------------------------------------- inflate

TEST_CASE("inflate: zlib round trips with the mod layer's deflate, raw streams, the output limit") {
    for (uint32_t seed : {1u, 7u, 99u}) {
        const std::vector<uint8_t> data = patternBytes(70000 + seed * 13, seed);
        const std::vector<uint8_t> z = mods::deflateZlib(data);
        CHECK(inflateZlib(z) == data);
        CHECK(mods::inflateZlib(z) == data);  // the PNG reader's entry point forwards to util
        // The raw deflate data between the 2-byte header and the Adler-32.
        const std::span<const uint8_t> raw(z.data() + 2, z.size() - 6);
        CHECK(inflateRaw(raw) == data);
        const std::vector<uint8_t> prefix = inflateRaw(raw, 1000, InflateLimit::Stop);
        REQUIRE(prefix.size() == 1000);
        CHECK(std::equal(prefix.begin(), prefix.end(), data.begin()));
        CHECK_THROWS_AS((void)inflateRaw(raw, 1000, InflateLimit::Throw), FormatError);
        CHECK_THROWS_AS((void)inflateZlib(z, 1000), FormatError);
    }
    // A stored block: BFINAL = 1, BTYPE = 00, LEN, NLEN, the bytes.
    const std::vector<uint8_t> stored = {0x01, 0x03, 0x00, 0xFC, 0xFF, 'a', 'b', 'c'};
    CHECK(inflateRaw(stored) == std::vector<uint8_t>{'a', 'b', 'c'});
    CHECK(inflateRaw(stored, 2, InflateLimit::Stop) == std::vector<uint8_t>{'a', 'b'});
    const std::vector<uint8_t> badLen = {0x01, 0x03, 0x00, 0x00, 0x00, 'a', 'b', 'c'};
    CHECK_THROWS_AS((void)inflateRaw(badLen), FormatError);
    const std::vector<uint8_t> badType = {0x07};
    CHECK_THROWS_AS((void)inflateRaw(badType), FormatError);
    std::vector<uint8_t> z = mods::deflateZlib(patternBytes(500, 3));
    z.back() ^= 1;
    CHECK_THROWS_AS((void)inflateZlib(z), FormatError);  // Adler-32 mismatch
}

// ---------------------------------------------------------------------- synthetic DOSBox-X states

TEST_CASE("DosboxState: a synthetic Tandy state (DGROUP, relocation, back buffer, video page, BIOS keys)") {
    const uint16_t dsSeg = 0x4000;  // DGROUP 4D94 at 4000: relocation -0xD94
    std::vector<uint8_t> ram = syntheticRam(dsSeg);
    const uint32_t ds = dsSeg * 16u;
    put16(ram, ds + dg::kTicks, 0x5678);
    put16(ram, ds + dg::kTicks + 2, 0x0012);
    put16(ram, ds + dg::kSpriteBank, 0x0010);       // far pointer {0x0010, 0x2345}
    put16(ram, ds + dg::kSpriteBank + 2, 0x2345);
    const uint32_t bb = (0x2005u - 0xD94u) * 16u + 0x10u;
    for (int i = 0; i < 32000; ++i) ram[bb + static_cast<uint32_t>(i)] = static_cast<uint8_t>(i * 7);
    for (int y = 0; y < 200; ++y)  // the Tandy page: four banks of 0x2000, 160-byte rows
        for (int x = 0; x < 160; ++x)
            ram[0x98000u + static_cast<uint32_t>((y & 3) * 0x2000 + (y >> 2) * 160 + x)] = static_cast<uint8_t>(y ^ x);
    put16(ram, 0x41A, 0x3C);  // head at the last slot: the buffer wraps
    put16(ram, 0x41C, 0x20);
    put16(ram, 0x43C, 0x1C0D);
    put16(ram, 0x41E, 0x011B);
    ram[0x417] = 0x20;
    std::vector<uint8_t> memory(0x88, 0);
    memory.insert(memory.end(), ram.begin(), ram.end());
    const std::string machine = "MCH_TANDY";
    const auto path = tempFile("synthetic_tandy.sav");
    REQUIRE(writeWholeFile(path, storedZip({{"Machine_Type", std::vector<uint8_t>(machine.begin(), machine.end())},
                                            {"Memory", memory}})));
    const DosboxState s = DosboxState::load(path);
    CHECK(s.machine() == DosboxState::Machine::Tandy);
    CHECK(s.machineName() == machine);
    CHECK(s.dgroupSegment() == dsSeg);
    CHECK(s.relocation() == -0xD94);
    CHECK(s.dgroup().size() == DosboxState::kDgroupBytes);
    CHECK(s.dsDword(dg::kTicks) == 0x125678u);
    CHECK(s.farPointerAt(dg::kSpriteBank) == 0x23450u + 0x10u);
    CHECK(s.imageLinear(0x2005, 0x10) == bb);
    CHECK(s.toImageSegment(0x1271) == 0x2005);
    CHECK(s.toImageSegment(0) == 0);
    const auto back = s.backBuffer();
    REQUIRE(back.size() == 32000);
    CHECK(back[12345] == static_cast<uint8_t>(12345 * 7));
    REQUIRE(s.screen().has_value());
    const std::vector<uint8_t>& screen = *s.screen();
    REQUIRE(screen.size() == 32000);
    int wrong = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 160; ++x) wrong += screen[static_cast<size_t>(y * 160 + x)] != static_cast<uint8_t>(y ^ x);
    CHECK(wrong == 0);
    CHECK(s.biosKeys() == std::vector<uint16_t>{0x1C0D, 0x011B});
    CHECK(s.biosShiftFlags() == 0x20);
    CHECK(DosboxState::usersFileOf("a/b/fl_walk2.sav") == std::filesystem::path("a/b/fl_walk2.users"));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_CASE("DosboxState: a synthetic EGA state (planes after the latch table, the row table, doubled pixels)") {
    const uint16_t dsSeg = 0x45B8;
    std::vector<uint8_t> ram = syntheticRam(dsSeg);
    const uint32_t ds = dsSeg * 16u;
    for (int y = 0; y < 200; ++y) put16(ram, ds + 0x2EEE + 2u * static_cast<uint32_t>(y), static_cast<uint16_t>(0x4000 + y * 0x50));
    // Video memory: 64 KB addresses x 4 planes; the latch table at A000:0000..000F, then two pixels.
    std::vector<uint8_t> planes(4 * 0x10000, 0);
    for (int k = 0; k < 16; ++k)
        for (int p = 0; p < 4; ++p) planes[static_cast<size_t>(4 * k + p)] = (k >> p) & 1 ? 0xC0 : 0x00;
    auto setPixel = [&planes](int x, int y, int c) {  // game pixel = two screen pixels
        const size_t a = 4 * (0x4000u + static_cast<size_t>(y) * 0x50u + static_cast<size_t>(x >> 2));
        const int bit = 7 - 2 * (x & 3);
        for (int p = 0; p < 4; ++p)
            if ((c >> p) & 1) planes[a + static_cast<size_t>(p)] |= static_cast<uint8_t>(3 << (bit - 1));
    };
    setPixel(0, 0, 15);
    setPixel(5, 10, 9);
    setPixel(319, 199, 6);
    std::vector<uint8_t> vga(0x1234, 0x5A);  // the state header before the video memory
    vga.insert(vga.end(), planes.begin(), planes.end());
    std::vector<uint8_t> memory(0x88, 0);
    memory.insert(memory.end(), ram.begin(), ram.end());
    const std::string machine = "MCH_EGA";
    const auto path = tempFile("synthetic_ega.sav");
    REQUIRE(writeWholeFile(path, storedZip({{"Machine_Type", std::vector<uint8_t>(machine.begin(), machine.end())},
                                            {"Memory", memory},
                                            {"Vga", vga}})));
    const DosboxState s = DosboxState::load(path);
    CHECK(s.machine() == DosboxState::Machine::Ega);
    CHECK(s.relocation() == -0x7DC);
    REQUIRE(s.screen().has_value());
    const std::vector<uint8_t>& px = *s.screen();
    auto pixel = [&px](int x, int y) {
        const uint8_t b = px[static_cast<size_t>(y * 160 + (x >> 1))];
        return (x & 1) ? (b & 15) : (b >> 4);
    };
    CHECK(pixel(0, 0) == 15);
    CHECK(pixel(1, 0) == 0);
    CHECK(pixel(5, 10) == 9);
    CHECK(pixel(4, 10) == 0);
    CHECK(pixel(319, 199) == 6);
    CHECK(std::count(px.begin(), px.end(), uint8_t{0}) == 32000 - 3);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_CASE("DosboxState: files that are not states of SST.EXE") {
    const auto path = tempFile("not_a_state.sav");
    REQUIRE(writeWholeFile(path, std::vector<uint8_t>{'P', 'K', 0, 0}));
    CHECK_THROWS_AS((void)DosboxState::load(path), Error);
    std::vector<uint8_t> memory(0x88 + 0x20000, 0);  // RAM without SST.EXE's DGROUP
    REQUIRE(writeWholeFile(path, storedZip({{"Memory", memory}})));
    CHECK_THROWS_AS((void)DosboxState::load(path), FormatError);
    CHECK_THROWS_AS((void)DosboxState::load(tempFile("missing_state.sav")), Error);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_CASE("--dos-load-state is a game option") {
    std::vector<const char*> argv = {"tmport", "--edition", "dos", "--dos-load-state", "a/b/fl_walk2.sav", "--headless"};
    CHECK(app::isGameInvocation(static_cast<int>(argv.size()), const_cast<char**>(argv.data())));
    std::string error;
    const auto o = app::parseLaunchArgs(static_cast<int>(argv.size()), const_cast<char**>(argv.data()), error);
    REQUIRE(o.has_value());
    CHECK(o->dosLoadState == "a/b/fl_walk2.sav");
    std::vector<const char*> missing = {"tmport", "--dos-load-state"};
    CHECK_FALSE(app::parseLaunchArgs(static_cast<int>(missing.size()), const_cast<char**>(missing.data()), error));
}

TEST_CASE("--dos-load-state: a headless run continues from the state (users file, tick 0, g_ticks of the trace)") {
    TM_REQUIRE_DOS_DATA();
    const auto sav = testdata::dosCapture("savestates/r2/fl_walk2.sav");
    const auto start = testdata::dosCapture("savestates/r2/fl_outside2.sav");
    if (!sav || !start) {
        MESSAGE("TM_DOS_CAPTURES (r2/fl_outside2) not found: test skipped");
        return;
    }
    const DosboxState s = DosboxState::load(*start);
    const std::filesystem::path dir = apptest::outDir("dos_load_state_run");
    std::ofstream(dir / "tmport.ini") << "[saves]\nimported_original = true\n";
    const std::string data = pathToUtf8(*testdata::dosDataPath());
    const std::string pref = pathToUtf8(dir), ini = pathToUtf8(dir / "tmport.ini"), state = pathToUtf8(*start);
    const std::string trace = pathToUtf8(dir / "trace.csv"), dump0 = "0:" + pathToUtf8(dir / "d0.bin"),
                      dump10 = "10:" + pathToUtf8(dir / "d10.bin");
    std::vector<const char*> argv = {"tmport", "--edition", "dos", "--data", data.c_str(), "--headless", "--pref-dir",
                                     pref.c_str(), "--config", ini.c_str(), "--dos-load-state", state.c_str(), "--dos-trace",
                                     trace.c_str(), "--dos-dump-state", dump0.c_str(), "--dos-dump-state", dump10.c_str(),
                                     "--keys", "89:down:right, 100:up:right"};
    std::vector<std::string> errors;
    CHECK(app::runGame(static_cast<int>(argv.size()), const_cast<char**>(argv.data()), {},
                       [&](const app::StartupError& e) { errors.push_back(e.what()); }) == 0);
    CHECK(errors.empty());
    // The state's USERS.DAT copy is the preference folder's DOS save now.
    CHECK(readWholeFile(dir / "dos_users.dat") == readWholeFile(DosboxState::usersFileOf(*start)));
    // Tick 0 is the loaded moment: DS:0000..CC1F as the state holds it (g_ticks included).
    const auto d0 = readWholeFile(dir / "d0.bin");
    REQUIRE(d0.has_value());
    REQUIRE(d0->size() == kDgroupSize);
    CHECK(static_cast<uint32_t>((*d0)[dg::kTicks] | (*d0)[dg::kTicks + 1] << 8 | (*d0)[dg::kTicks + 2] << 16 |
                                static_cast<uint32_t>((*d0)[dg::kTicks + 3]) << 24) == s.dsDword(dg::kTicks));
    CHECK(std::filesystem::exists(dir / "d10.bin.png"));
    // The trace: row 1 is the first game tick after the state, at the original's g_ticks (this state was
    // saved inside Sound_Tick, so 4 PIT ticks later, not DS:3D34 + 1 = 5).
    std::ifstream in(dir / "trace.csv");
    std::string line;
    std::vector<std::string> rows;
    while (std::getline(in, line))
        if (!line.empty() && line[0] != '#' && line.rfind("tick,", 0) != 0) rows.push_back(line);
    REQUIRE(rows.size() >= 100);
    CHECK(rows[0].rfind(std::format("1,{},", s.dsDword(dg::kTicks) + 4), 0) == 0);
    CHECK(rows[1].rfind(std::format("2,{},", s.dsDword(dg::kTicks) + 9), 0) == 0);
}

// ------------------------------------------------------------------- the reference state library

TEST_CASE("load state: every reference state loads; DGROUP round trip, files found, the shown frame") {
    TM_REQUIRE_DOS_GAME_FILES();
    TM_REQUIRE_DOS_CAPTURES();
    const auto states = referenceStates(*captures);
    // Fields the load does not carry over as they are: g_ticks of a state saved inside the timer
    // interrupt that runs Game_Tick (DosRuntime::resumeAt starts one PIT tick before it), and for a
    // state saved inside a frame the draw marks and flags Sprites_EraseAll resets and the clip window
    // the rest of Sprites_DrawSorted restores.
    const std::vector<std::string_view> midFrame = {"g_drawRecs", "g_spritesChanged", "g_saveHeapReset", "DS_37DA_clipWindow"};
    std::vector<DgroupField> fields = gameStateFields();
    for (const DgroupField& f : serviceFields()) fields.push_back(f);
    int loaded = 0, exact = 0, screens = 0, midFrames = 0, withSfx = 0, withTune = 0, insideTimer = 0, insideTick = 0;
    std::map<std::string, int> differingPictures, pendingPictures;
    for (const auto& path : states) {
        const std::string name = path.parent_path().filename().string() + "/" + path.stem().string();
        CAPTURE(name);
        const DosboxState s = DosboxState::load(path);
        dosgame::FixtureOptions opts;
        opts.options.look = stateLook(s);
        dosgame::GameFixture f(*gameFiles, opts);
        DosGame& g = f.g();
        const LoadedState ls = loadDosboxState(g, s);
        for (const std::string& w : ls.warnings) MESSAGE(name << ": " << w);
        CHECK(ls.warnings.empty());
        CHECK(ls.phaseExact);
        CHECK(!ls.cpu.empty());
        CHECK(!ls.pan.empty());
        CHECK(!ls.bank.empty());
        ++loaded;
        midFrames += ls.midFrame;
        withSfx += ls.restartedSfx >= 0;
        withTune += ls.restartedTune >= 0;
        // DGROUP -> engine -> DGROUP.
        const std::vector<uint8_t> dumped = dumpToDgroup(g);
        const auto diff = diffDgroup(dumped, s.dgroup(), &fields);
        std::vector<std::string_view> unexpected;
        for (std::string_view d : diff) {
            // Quiz_FindTierEnd computes words 5-6 of g_quizTierStart at the first sign-in; the engine
            // has them from the start (DosGame::quizTiers).
            const bool tiersLater = d == "g_quizTier5Start" && s.dsDword(dg::kQuizTier5Start) == 0;
            const bool resumedEarly = d == "g_ticks" && ls.countdown == 0 && ls.insideTimerBeforeCountdown;
            const bool ok = tiersLater || resumedEarly ||
                            (ls.midFrame && std::find(midFrame.begin(), midFrame.end(), d) != midFrame.end());
            if (!ok) unexpected.push_back(d);
        }
        for (std::string_view d : unexpected) MESSAGE(name << ": " << d << " differs after the round trip");
        CHECK(unexpected.empty());
        exact += unexpected.empty();
        // The engine's picture of the loaded moment against the state's video memory.
        if (s.screen()) {
            ++screens;
            // Outside the dirty spans the screen must equal the back buffer with the sprites as last
            // drawn; inside them a frame in progress may have drawn what is not presented yet.
            const BackBuffer shown = composeShownFrame(g);
            int px = 0, pending = 0;
            for (int y = 0; y < BackBuffer::kHeight; ++y)
                for (int x = 0; x < BackBuffer::kWidth; ++x) {
                    if (shown.pixel(x, y) == g.screen.pixel(x, y)) continue;
                    if (g.back.dirty().isDirty(x / 2, y)) ++pending;
                    else ++px;
                }
            // (A state saved inside Sprites_DrawSorted can hold a sprite drawn over a span the last
            // present did not cover: the level-3 geyser states differ by 3 pixels at a cloud's edge.)
            if (!ls.midFrame) CHECK(px == 0);
            if (px) differingPictures[name] = px;
            if (pending) pendingPictures[name] = pending;
            // TM_DOS_LOAD_STATE_DUMP=<folder>: both pictures of every differing state, for a look.
            if (const auto dir = getEnv("TM_DOS_LOAD_STATE_DUMP"); px && dir) {
                const auto out = pathFromUtf8(*dir);
                std::error_code ec;
                std::filesystem::create_directories(out, ec);
                const std::string stem = path.parent_path().filename().string() + "_" + path.stem().string();
                png::writeRgba(out / (stem + "_screen.png"), BackBuffer::kWidth, BackBuffer::kHeight, toRgba(g.screen, g.palette));
                png::writeRgba(out / (stem + "_composed.png"), BackBuffer::kWidth, BackBuffer::kHeight, toRgba(shown, g.palette));
            }
        }
        // The first game tick after the load comes on the interrupt DS:3D34 said, and the runtime's
        // PIT count plus ticksOffset is the original's g_ticks.
        const auto first = f.step(1);
        REQUIRE(first.size() == 1);
        CHECK(first[0].pitTicks + ls.ticksOffset == ls.firstTickTicks);
        CHECK(ls.firstTickTicks == ls.ticks + ls.countdown + (ls.insideTimerBeforeCountdown ? 0 : 1));
        insideTimer += ls.insideTimerBeforeCountdown;
        insideTick += ls.insideGameTick;
    }
    std::string list, pendingList;
    for (const auto& [n, px] : differingPictures) list += std::format(" {}={}", n, px);
    for (const auto& [n, px] : pendingPictures) pendingList += std::format(" {}={}", n, px);
    MESSAGE(std::format("{} states loaded ({} exact round trips, {} saved inside a frame, {} inside the timer interrupt before "
                        "its countdown, {} inside Game_Tick, {} with an effect and {} with a tune restarted); {} screens: {} "
                        "differ from the composed frame outside the dirty spans{}; {} inside them (drawn, not presented yet):{}",
                        loaded, exact, midFrames, insideTimer, insideTick, withSfx, withTune, screens, differingPictures.size(),
                        list, pendingPictures.size(), pendingList));
    CHECK(loaded >= 120);
    CHECK(screens == loaded);
}
