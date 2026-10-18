// The autoplayer (area autoplay, src/tools/autoplay/autoplay.h): a bot that plays the real game
// (game::gameMain) headless on a manual clock and checks the game's invariants while it plays.
// The data-free part checks the sprite finder; the game part needs TM_DATA (skips without it).
// No window, no audio device.

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "gfx/frame_buffer.h"
#include "test_data.h"
#include "tools/autoplay/autoplay.h"
#include "tools/autoplay/autoplay_vision.h"

using namespace tmport;
using namespace tmport::autoplay;

TEST_CASE("autoplay vision: sprites are the screen pixels that differ from the back page") {
    gfx::FrameBuffer fb;
    // Identical layers: nothing.
    CHECK(findSprites(fb, {}).empty());
    // A 40 x 60 "elf" at (200, 180) on the screen layer only, and a 6-px speck (ignored).
    for (int y = 180; y < 240; ++y)
        for (int x = 200; x < 240; ++x) fb.row(y)[x] = 7;
    for (int y = 200; y < 203; ++y)
        for (int x = 400; x < 406; ++x) fb.row(y)[x] = 9;
    auto blobs = findSprites(fb, {});
    REQUIRE(blobs.size() == 1);
    CHECK(blobs[0].x0 == 200);
    CHECK(blobs[0].x1 == 239);
    CHECK(blobs[0].y0 == 180);
    CHECK(blobs[0].y1 == 239);
    // A mask hides it (the player rectangle, the fountain, ...).
    CHECK(findSprites(fb, {{190, 0, 60, 400}}).empty());
    // The same picture on the back page: no difference any more.
    for (int y = 180; y < 240; ++y)
        for (int x = 200; x < 240; ++x) fb.row(y + gfx::FrameBuffer::kLayer1Y)[x] = 7;
    CHECK(findSprites(fb, {}).empty());
}

TEST_CASE("autoplay: one seed, one climb through the real game, no invariant violated") {
    TM_REQUIRE_GAME_DATA();
    RunOptions o;
    o.seed = 3;
    o.climbs = 1;
    o.prefDir = std::filesystem::temp_directory_path() / "tmport_autoplay_test" / "seed_3";
    o.audio = false;
    const RunResult r = runAutoplay(*g, o);
    INFO(summarize(r));
    INFO(r.abortReason);
    for (const auto& v : r.violations) INFO(v);
    for (const auto& l : r.recent) INFO(l);
    CHECK(r.abortReason.empty());
    CHECK(r.violations.empty());
    CHECK(r.stats.climbsDone == 1);
    CHECK(r.ok);
    // The climb went through every part of the game.
    CHECK(r.stats.gates >= 3);           // two level gates and the castle
    CHECK(r.stats.cutscenes == 2);       // levels 0 and 1
    CHECK(r.stats.keys == 3);            // one per level
    CHECK(r.stats.treasures == 6);       // tier 0: 2 per level
    CHECK(r.stats.finalTotal == 6);
    CHECK(r.saveChecks >= 3);            // climb end, exit, shutdown
    // Deterministic: the same seed plays the same game.
    const RunResult again = runAutoplay(*g, o);
    CHECK(again.ok);
    CHECK(again.stats.roomTicks == r.stats.roomTicks);
    CHECK(again.gameMs == r.gameMs);
    std::error_code ec;
    std::filesystem::remove_all(o.prefDir.parent_path(), ec);
}
