// Command-line tools (src/cli): the help overview names every tool group, --help topics, the
// scene tool with the game's comma-separated scripts (the castle wall), and --mods reaching the
// tools that open the game files themselves (cli/tool_mods.h). Headless; no window, no sound.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cli/commands.h"
#include "cli/options.h"
#include "cli/tool_mods.h"
#include "mischief/test/app_test_util.h"
#include "mischief/util/file_util.h"
#include "res/game_files.h"
#include "test_data.h"

using namespace tmport;

namespace {

// argv for cli::run from strings (kept alive by the vector).
struct Argv {
    std::vector<std::string> args;
    std::vector<char*> ptrs;
    explicit Argv(std::vector<std::string> a) : args(std::move(a)) {
        for (auto& s : args) ptrs.push_back(s.data());
        ptrs.push_back(nullptr);
    }
    int argc() const { return static_cast<int>(args.size()); }
    char** argv() { return ptrs.data(); }
};

// A mod folder whose only file replaces the text OTHR 11605 (engine-written text, no game data).
std::filesystem::path textMod(const char* name) {
    const auto dir = apptest::outDir(name);
    std::filesystem::create_directories(dir / "OTHR");
    std::ofstream(dir / "OTHR" / "11605.txt", std::ios::binary) << "Hello from a test mod.\r\n";
    return dir;
}

}  // namespace

TEST_CASE("cli: the help overview names every tool command and topic") {
    const std::string o = cli::toolsOverview();
    for (const char* cmd : {"--info", "--dump-pmap", "--dump-sheet", "--headless-frame", "--view", "--render-dialog",
                            "--render-help", "--render-text", "--render-font", "--text-metrics", "--render-dam", "--dam-info",
                            "--dam-check", "--screen", "--scene", "--room-run", "--dump-ssnd", "--midi-dump", "--midi-render",
                            "--make-test-soundfont", "--export-mod-kit", "--check-mods", "--mods", "--data"})
        CHECK_MESSAGE(o.find(cmd) != std::string::npos, cmd);
    for (const char* topic : {"game", "inspect", "text", "anim", "screen", "scene", "room", "audio", "mods", "all"})
        CHECK_MESSAGE(o.find(topic) != std::string::npos, topic);
    CHECK(cli::printHelp("no-such-topic") == 2);  // one line on stderr

    std::string error;
    Argv a({"tmport", "--help", "scene"});
    auto opts = cli::parseArgs(a.argc(), a.argv(), error);
    REQUIRE(opts);
    CHECK(opts->command == cli::Command::Help);
    CHECK(opts->helpTopic == "scene");
    Argv b({"tmport", "--help", "--16"});  // an option after --help is not a topic
    opts = cli::parseArgs(b.argc(), b.argv(), error);
    REQUIRE(opts);
    CHECK(opts->helpTopic.empty());
}

TEST_CASE("cli: the castle scene runs a comma-separated script and writes its frames") {
    TM_REQUIRE_GAME_DATA();
    const auto out = apptest::outDir("cli_castle");
    std::error_code ec;
    std::filesystem::remove_all(out / "castle", ec);
    // Walk right to the ladder (held arrows: a press is shorter than the 100 ms tick), climb.
    Argv a({"tmport", "--scene", "castle", "--tier", "2", "--data", *testdata::dataPath(), "--out", mischief::pathToUtf8(out / "castle"),
            "--shot-every", "1000", "--keys", "wait:500,hold:right:1200,hold:up:3000,click:100,200,end"});
    CHECK(cli::run(a.argc(), a.argv()) == 0);
    for (const char* f : {"0000.png", "0001.png", "0004.png", "final.png"})
        CHECK_MESSAGE(std::filesystem::exists(out / "castle" / f), f);

    // A malformed token stops the tool before the scene (exit code 2, no frames).
    std::filesystem::remove_all(out / "bad", ec);
    Argv b({"tmport", "--scene", "castle", "--data", *testdata::dataPath(), "--out", mischief::pathToUtf8(out / "bad"), "--keys",
            "wait:500,no-such-key"});
    CHECK(cli::run(b.argc(), b.argv()) == 2);
    CHECK(!std::filesystem::exists(out / "bad" / "final.png"));
}

TEST_CASE("cli: --mods reaches the tools that open the game files") {
    TM_REQUIRE_GAME_DATA();
    const auto mod = textMod("cli_mod");
    const auto dp = mischief::pathFromUtf8(*testdata::dataPath());

    SUBCASE("applyToolMods") {
        GameFiles files = GameFiles::open(dp);
        const auto before = files.res->loadRaw(ResType::OTHR, 11605);
        REQUIRE(before);
        CHECK(cli::applyToolMods(files, {}) == 0);
        CHECK(cli::applyToolMods(files, {mischief::pathToUtf8(mod / "no_such_folder"), mischief::pathToUtf8(mod)}) == 1);
        const auto after = files.res->loadRaw(ResType::OTHR, 11605);
        REQUIRE(after);
        CHECK(std::string(after->begin(), after->begin() + 5) == "Hello");
        CHECK(*after != *before);
    }
    SUBCASE("a text render through cli::run") {
        const auto out = apptest::outDir("cli_mod_out");
        const auto plain = out / "plain.png", modded = out / "modded.png";
        Argv a({"tmport", "--render-text", "11605", "16", "40", "300", mischief::pathToUtf8(plain), "--data", mischief::pathToUtf8(dp)});
        REQUIRE(cli::run(a.argc(), a.argv()) == 0);
        Argv b({"tmport", "--render-text", "11605", "16", "40", "300", mischief::pathToUtf8(modded), "--data", mischief::pathToUtf8(dp), "--mods",
                mischief::pathToUtf8(mod)});
        REQUIRE(cli::run(b.argc(), b.argv()) == 0);
        const auto p = mischief::readWholeFile(plain), m = mischief::readWholeFile(modded);
        REQUIRE(p);
        REQUIRE(m);
        CHECK(*p != *m);
    }
}
