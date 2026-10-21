// The world modules (scene, camera, cells) against the DOSBox-X reference material of
// extracted/captures/dos (re/dos/docs/REFERENCE_CAPTURES.md): every save state's play field, as the
// engine redraws it from that state's DGROUP (camera, block grid, loaded scene), equals the back
// buffer (segment 2005) stored in the same state. Between frames the original's back buffer holds
// only the background (sprites are erased right after the present), so no actor has to be masked.
// Two runs of the r2 frame logs (the bridge break, the geyser hole) are replayed through all modules
// from the save states they started at and must match the logged positions, camera and player
// state tick for tick. The tests read the user's own captures at run time and store only per-tick
// numbers (no pixels); they skip when TM_DATA_DOS or TM_DOS_CAPTURES is missing.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "dos/data/dgroup_offsets.h"
#include "dos/game/camera.h"
#include "dos/game/game.h"
#include "dos/game/game_state.h"
#include "dos/game/load_state.h"
#include "dos/save/dosbox_state.h"
#include "dos_game_fixture.h"
#include "dos_test_data.h"
#include "mischief/util/file_util.h"

using namespace tmport;
using namespace tmport::dos;

namespace {

// ---------------------------------------------------------------- DOSBox-X save states (.sav)
// Read with the engine's reader (dos/save/dosbox_state.h): DGROUP, the back buffer (segment 2005).

uint16_t dsWord(std::span<const uint8_t> d, uint16_t off) { return static_cast<uint16_t>(d[off] | (d[off + 1u] << 8)); }

std::vector<std::filesystem::path> saveStates(const std::filesystem::path& captures) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const char* sub : {"ega", "r2", "tandy"}) {
        const auto dir = captures / "savestates" / sub;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == ".sav") out.push_back(e.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The PAN a scene shows and the one loaded before it on the way there (the climb, the end flow).
const char* panOfScene(uint16_t s) {
    static constexpr const char* kPans[] = {"MOUNTAIN", "MOUNT2N3", "MOUNT3", "CLOSE", "SHACK"};
    return kPans[s];
}
const char* previousPanOfScene(uint16_t s) {
    static constexpr const char* kPrev[] = {"OPENING", "MOUNTAIN", "MOUNT2N3", "MOUNT3", "CLOSE"};
    return kPrev[s];
}

// Back-buffer bytes that hold no scenery in a save state: the sprites of a state saved in the
// middle of a frame (between Sprites_DrawSorted and Sprites_EraseAll their draw records carry the
// "drawn" bit 15 in the owner word), and in the rank room the text RankChest_Update draws into the
// background (name, rank band, total: x bytes 0x1E..0x86, rows 0x1E..0x7F).
std::vector<bool> notScenery(const DosGfxFiles& files, std::span<const uint8_t> d, uint16_t scene) {
    std::vector<bool> mask(static_cast<size_t>(BackBuffer::kRowBytes) * BackBuffer::kHeight, false);
    auto cover = [&](int x0, int y0, int x1, int y1) {
        for (int y = std::max(y0, 0); y <= std::min(y1, BackBuffer::kHeight - 1); ++y)
            for (int x = std::max(x0, 0); x <= std::min(x1, BackBuffer::kRowBytes - 1); ++x)
                mask[static_cast<size_t>(y) * BackBuffer::kRowBytes + x] = true;
    };
    const PakFile& pak = files.pak(scene == 3 ? "CLOSE" : scene == 4 ? "SHACK" : "SPRITES");
    auto rec = [&](int i, uint16_t field) {
        return static_cast<int16_t>(dsWord(d, static_cast<uint16_t>(dg::kDrawRecs + dg::drawrec::kSize * i + field)));
    };
    for (int i = 0; i < SpriteRenderer::kRecords; ++i) {
        const auto owner = static_cast<uint16_t>(rec(i, dg::drawrec::kOwner));
        const auto frame = static_cast<uint16_t>(rec(i, dg::drawrec::kFrame));
        if (!(owner & 0x8000) || frame == 0xFFFF || (frame >> 1) >= pak.count()) continue;
        int x = rec(i, dg::drawrec::kX), y = rec(i, dg::drawrec::kY);
        const int o = owner & 0x7FFF;
        if (o != i && o < SpriteRenderer::kRecords) {  // a child: offset from its owner's record
            x += rec(o, dg::drawrec::kX);
            y += rec(o, dg::drawrec::kY);
        }
        const PakFrame& f = pak.frame(frame >> 1);
        const int sx = (frame & 1) ? x - (f.widthBytes + f.xOffset) : x + f.xOffset;
        cover(sx - 1, y + f.yOffset, sx + f.widthBytes, y + f.yOffset + f.height - 1);
    }
    if (scene == 4 && dsWord(d, dg::kFrameCamY) == 0x80) cover(0x1E, 0x1E, 0x86, 0x7F);
    return mask;
}


// ------------------------------------------------------------ replays of the r2 frame logs
// The DOSBox-X frame logger (extracted/scratch/dos_w1_runtime/framelog.py) recorded, at the start
// of every executed game tick, the player's x, y, the camera and the player state. A run replayed
// from the save state it started at, with the same key presses, must give the same values tick for
// tick: engine record e (after tick e) is log record e + 1. The original loads a scene from disk
// with Game_Tick frozen (g_quit): at a level boundary the logs lose 4 or 5 ticks, and so does the
// engine (its reference frame timing, DosRuntime long frames, PORTING_NOTES §2.5). The log has no
// record for those ticks, so the `stallTicks` engine records from `stallAt` on (after the step that
// precedes the load and the frozen ticks) are not compared.

struct LogChange {
    uint16_t t, x, y, camX, camY, state;  // log tick and the values from then on
};

// kBridgeL3: 87 change points of x, y, camera, state (log ticks)
const LogChange kBridgeL3[] = {
    {147, 0x160, 0x470, 0x80, 0x400, 0xc}, {156, 0x168, 0x470, 0x80, 0x400, 0xc},
    {161, 0x170, 0x470, 0x80, 0x400, 0xc}, {166, 0x178, 0x470, 0x80, 0x400, 0xc},
    {171, 0x180, 0x470, 0x88, 0x400, 0xc}, {176, 0x188, 0x470, 0x90, 0x400, 0xc},
    {181, 0x190, 0x470, 0x98, 0x400, 0xc}, {186, 0x198, 0x470, 0xa0, 0x400, 0xc},
    {191, 0x1a0, 0x470, 0xa8, 0x400, 0xc}, {197, 0x1a8, 0x470, 0xb0, 0x400, 0xc},
    {202, 0x1b0, 0x470, 0xb8, 0x400, 0xc}, {207, 0x1b8, 0x470, 0xc0, 0x400, 0xc},
    {212, 0x1c0, 0x470, 0xc8, 0x400, 0xc}, {217, 0x1c8, 0x470, 0xd0, 0x400, 0xc},
    {222, 0x1d0, 0x470, 0xd8, 0x400, 0xc}, {227, 0x1d8, 0x470, 0xe0, 0x400, 0xc},
    {232, 0x1e0, 0x470, 0xe8, 0x400, 0xc}, {238, 0x1e8, 0x470, 0xf0, 0x400, 0xc},
    {243, 0x1f0, 0x470, 0xf8, 0x400, 0xc}, {248, 0x1f8, 0x470, 0x100, 0x400, 0xc},
    {253, 0x200, 0x470, 0x108, 0x400, 0xc}, {258, 0x208, 0x470, 0x110, 0x400, 0xc},
    {263, 0x210, 0x470, 0x118, 0x400, 0xc}, {267, 0x210, 0x470, 0x118, 0x400, 0xa},
    {1082, 0x210, 0x470, 0x118, 0x400, 0xc}, {1091, 0x218, 0x470, 0x120, 0x400, 0xc},
    {1096, 0x220, 0x470, 0x128, 0x400, 0xc}, {1101, 0x228, 0x470, 0x130, 0x400, 0xc},
    {1106, 0x230, 0x470, 0x138, 0x400, 0xc}, {1108, 0x230, 0x470, 0x138, 0x400, 0x20},
    {1109, 0x230, 0x478, 0x138, 0x408, 0x20}, {1114, 0x230, 0x480, 0x138, 0x410, 0x20},
    {1119, 0x230, 0x488, 0x138, 0x418, 0x20}, {1124, 0x230, 0x490, 0x138, 0x420, 0x20},
    {1129, 0x230, 0x498, 0x138, 0x428, 0x20}, {1134, 0x230, 0x4a0, 0x138, 0x430, 0x20},
    {1139, 0x230, 0x4a8, 0x138, 0x438, 0x20}, {1144, 0x230, 0x4b0, 0x138, 0x440, 0x20},
    {1149, 0x230, 0x4b8, 0x138, 0x448, 0x20}, {1154, 0x230, 0x4c0, 0x138, 0x450, 0x20},
    {1159, 0x230, 0x4c8, 0x138, 0x458, 0x20}, {1164, 0x230, 0x4d0, 0x138, 0x460, 0x20},
    {1169, 0x230, 0x4d8, 0x138, 0x468, 0x20}, {1174, 0x230, 0x4e0, 0x138, 0x470, 0x20},
    {1179, 0x230, 0x4e8, 0x138, 0x478, 0x20}, {1184, 0x230, 0x4f0, 0x138, 0x480, 0x20},
    {1193, 0x230, 0x4f8, 0x138, 0x488, 0x20}, {1198, 0x230, 0x500, 0x138, 0x490, 0x20},
    {1203, 0x230, 0x508, 0x138, 0x498, 0x20}, {1208, 0x230, 0x510, 0x138, 0x4a0, 0x20},
    {1213, 0x230, 0x518, 0x138, 0x4a8, 0x20}, {1218, 0x230, 0x520, 0x138, 0x4b0, 0x20},
    {1223, 0x230, 0x528, 0x138, 0x4b8, 0x20}, {1228, 0x230, 0x530, 0x138, 0x4c0, 0x20},
    {1233, 0x230, 0x538, 0x138, 0x4c8, 0x20}, {1238, 0x230, 0x540, 0x138, 0x4d0, 0x20},
    {1243, 0x230, 0x548, 0x138, 0x4d8, 0x20}, {1248, 0x230, 0x550, 0x138, 0x4e0, 0x20},
    {1253, 0x230, 0x558, 0x138, 0x4e8, 0x20}, {1258, 0x230, 0x560, 0x138, 0x4f0, 0x20},
    {1263, 0x230, 0x568, 0x138, 0x4f8, 0x20}, {1268, 0x230, 0x570, 0x138, 0x500, 0x20},
    {1273, 0x230, 0x578, 0x138, 0x508, 0x20}, {1278, 0x230, 0x580, 0x138, 0x510, 0x20},
    {1283, 0x230, 0x588, 0x138, 0x518, 0x20}, {1288, 0x230, 0x590, 0x138, 0x520, 0x20},
    {1293, 0x230, 0x598, 0x138, 0x528, 0x20}, {1298, 0x230, 0x5a0, 0x138, 0x530, 0x20},
    {1303, 0x230, 0x5a8, 0x138, 0x538, 0x20}, {1308, 0x230, 0x5b0, 0x138, 0x540, 0x20},
    {1313, 0x230, 0x5b8, 0x138, 0x548, 0x20}, {1318, 0x230, 0x5c0, 0x138, 0x550, 0x20},
    {1323, 0x230, 0x5c8, 0x138, 0x558, 0x20}, {1328, 0x230, 0x5d0, 0x138, 0x560, 0x20},
    {1333, 0x230, 0x5d8, 0x138, 0x568, 0x20}, {1338, 0x230, 0x5e0, 0x138, 0x570, 0x20},
    {1343, 0x230, 0x5e8, 0x138, 0x578, 0x20}, {1348, 0x230, 0x5f0, 0x138, 0x580, 0x20},
    {1350, 0x230, 0x5f0, 0x138, 0x580, 0x13}, {1351, 0x238, 0x5f0, 0x140, 0x580, 0x13},
    {1358, 0x240, 0x5f0, 0x148, 0x580, 0x13}, {1365, 0x248, 0x5f0, 0x150, 0x580, 0x13},
    {1372, 0x250, 0x5f0, 0x158, 0x580, 0x13}, {1379, 0x250, 0x5f0, 0x158, 0x580, 0x14},
    {1380, 0x258, 0x5f0, 0x160, 0x580, 0x14}, {1387, 0x260, 0x5f0, 0x168, 0x580, 0x14},
    {1394, 0x260, 0x5f0, 0x168, 0x580, 0xa},
};

// kGeyserSlide: 53 change points of x, y, camera, state (log ticks)
const LogChange kGeyserSlide[] = {
    {162, 0x770, 0x3f0, 0x6f0, 0x380, 0xb}, {171, 0x768, 0x3f0, 0x6f0, 0x380, 0xb},
    {176, 0x760, 0x3f0, 0x6f0, 0x380, 0xb}, {181, 0x758, 0x3f0, 0x6f0, 0x380, 0xb},
    {186, 0x750, 0x3f0, 0x6f0, 0x380, 0xb}, {187, 0x750, 0x3f0, 0x6f0, 0x380, 0x1e},
    {188, 0x740, 0x3f8, 0x6e0, 0x388, 0x1e}, {195, 0x740, 0x400, 0x6e0, 0x390, 0x1e},
    {202, 0x740, 0x408, 0x6e0, 0x398, 0x1e}, {210, 0x740, 0x410, 0x6e0, 0x3a0, 0x1e},
    {217, 0x740, 0x418, 0x6e0, 0x3a8, 0x1e}, {224, 0x740, 0x420, 0x6e0, 0x3b0, 0x1e},
    {232, 0x740, 0x428, 0x6e0, 0x3b8, 0x1e}, {239, 0x740, 0x430, 0x6e0, 0x3c0, 0x1e},
    {246, 0x740, 0x438, 0x6e0, 0x3c8, 0x1e}, {254, 0x740, 0x440, 0x6e0, 0x3d0, 0x1e},
    {261, 0x740, 0x448, 0x6e0, 0x3d8, 0x1e}, {268, 0x740, 0x450, 0x6e0, 0x3e0, 0x1e},
    {276, 0x740, 0x458, 0x6e0, 0x3e8, 0x1e}, {283, 0x740, 0x460, 0x6e0, 0x3f0, 0x1e},
    {290, 0x740, 0x468, 0x6e0, 0x3f8, 0x1e}, {298, 0x740, 0x470, 0x6e0, 0x400, 0x1e},
    {309, 0x740, 0x478, 0x6e0, 0x408, 0x1e}, {316, 0x740, 0x480, 0x6e0, 0x410, 0x1e},
    {324, 0x740, 0x488, 0x6e0, 0x418, 0x1e}, {331, 0x740, 0x490, 0x6e0, 0x420, 0x1e},
    {338, 0x740, 0x498, 0x6e0, 0x428, 0x1e}, {346, 0x740, 0x4a0, 0x6e0, 0x430, 0x1e},
    {353, 0x740, 0x4a8, 0x6e0, 0x438, 0x1e}, {360, 0x740, 0x4b0, 0x6e0, 0x440, 0x1e},
    {368, 0x740, 0x4b8, 0x6e0, 0x448, 0x1e}, {375, 0x740, 0x4c0, 0x6e0, 0x450, 0x1e},
    {382, 0x740, 0x4c8, 0x6e0, 0x458, 0x1e}, {390, 0x740, 0x4d0, 0x6e0, 0x460, 0x1e},
    {397, 0x740, 0x4d8, 0x6e0, 0x468, 0x1e}, {404, 0x740, 0x4e0, 0x6e0, 0x470, 0x1e},
    {412, 0x740, 0x4e8, 0x6e0, 0x478, 0x1e}, {419, 0x740, 0x4f0, 0x6e0, 0x480, 0x1e},
    {426, 0x740, 0x4f8, 0x6e0, 0x488, 0x1e}, {434, 0x740, 0x500, 0x6e0, 0x490, 0x1e},
    {441, 0x740, 0x508, 0x6e0, 0x498, 0x1e}, {448, 0x740, 0x510, 0x6e0, 0x4a0, 0x1e},
    {456, 0x740, 0x518, 0x6e0, 0x4a8, 0x1e}, {463, 0x740, 0x520, 0x6e0, 0x4b0, 0x1e},
    {470, 0x740, 0x528, 0x6e0, 0x4b8, 0x1e}, {478, 0x740, 0x530, 0x6e0, 0x4c0, 0x1e},
    {485, 0x740, 0x538, 0x6e0, 0x4c8, 0x1e}, {492, 0x740, 0x540, 0x6e0, 0x4d0, 0x1e},
    {500, 0x740, 0x548, 0x6e0, 0x4d8, 0x1e}, {507, 0x740, 0x550, 0x6e0, 0x4e0, 0x1e},
    {514, 0x740, 0x558, 0x6e0, 0x4e8, 0x1e}, {522, 0x740, 0x560, 0x6e0, 0x4f0, 0x1e},
    {524, 0x740, 0x560, 0x6e0, 0x4f0, 0xa},
};

// kBridgeL2: 87 change points of x, y, camera, state (log ticks)
const LogChange kBridgeL2[] = {
    {90, 0x2d0, 0x5f0, 0x1d8, 0x580, 0xc}, {99, 0x2d8, 0x5f0, 0x1e0, 0x580, 0xc},
    {104, 0x2e0, 0x5f0, 0x1e8, 0x580, 0xc}, {109, 0x2e8, 0x5f0, 0x1f0, 0x580, 0xc},
    {114, 0x2f0, 0x5f0, 0x1f8, 0x580, 0xc}, {119, 0x2f8, 0x5f0, 0x200, 0x580, 0xc},
    {124, 0x300, 0x5f0, 0x208, 0x580, 0xc}, {129, 0x308, 0x5f0, 0x210, 0x580, 0xc},
    {134, 0x310, 0x5f0, 0x218, 0x580, 0xc}, {140, 0x318, 0x5f0, 0x220, 0x580, 0xc},
    {145, 0x320, 0x5f0, 0x228, 0x580, 0xc}, {150, 0x328, 0x5f0, 0x230, 0x580, 0xc},
    {155, 0x330, 0x5f0, 0x238, 0x580, 0xc}, {160, 0x338, 0x5f0, 0x240, 0x580, 0xc},
    {165, 0x340, 0x5f0, 0x248, 0x580, 0xc}, {170, 0x348, 0x5f0, 0x250, 0x580, 0xc},
    {175, 0x350, 0x5f0, 0x258, 0x580, 0xc}, {181, 0x358, 0x5f0, 0x260, 0x580, 0xc},
    {186, 0x360, 0x5f0, 0x268, 0x580, 0xc}, {191, 0x368, 0x5f0, 0x270, 0x580, 0xc},
    {196, 0x370, 0x5f0, 0x278, 0x580, 0xc}, {201, 0x378, 0x5f0, 0x280, 0x580, 0xc},
    {206, 0x380, 0x5f0, 0x288, 0x580, 0xc}, {211, 0x388, 0x5f0, 0x290, 0x580, 0xc},
    {216, 0x390, 0x5f0, 0x298, 0x580, 0xc}, {222, 0x398, 0x5f0, 0x2a0, 0x580, 0xc},
    {227, 0x3a0, 0x5f0, 0x2a8, 0x580, 0xc}, {232, 0x3a8, 0x5f0, 0x2b0, 0x580, 0xc},
    {237, 0x3b0, 0x5f0, 0x2b8, 0x580, 0xc}, {239, 0x3b0, 0x5f0, 0x2b8, 0x580, 0x20},
    {240, 0x3b0, 0x5f8, 0x2b8, 0x588, 0x20}, {245, 0x3b0, 0x600, 0x2b8, 0x590, 0x20},
    {250, 0x3b0, 0x608, 0x2b8, 0x598, 0x20}, {255, 0x3b0, 0x610, 0x2b8, 0x5a0, 0x20},
    {260, 0x3b0, 0x618, 0x2b8, 0x5a8, 0x20}, {265, 0x3b0, 0x620, 0x2b8, 0x5b0, 0x20},
    {270, 0x3b0, 0x628, 0x2b8, 0x5b8, 0x20}, {275, 0x3b0, 0x630, 0x2b8, 0x5c0, 0x20},
    {280, 0x3b0, 0x638, 0x2b8, 0x5c8, 0x20}, {285, 0x3b0, 0x640, 0x2b8, 0x5d0, 0x20},
    {290, 0x3b0, 0x648, 0x2b8, 0x5d8, 0x20}, {295, 0x3b0, 0x650, 0x2b8, 0x5e0, 0x20},
    {300, 0x3b0, 0x658, 0x2b8, 0x5e8, 0x20}, {305, 0x3b0, 0x660, 0x2b8, 0x5f0, 0x20},
    {310, 0x3b0, 0x668, 0x2b8, 0x5f8, 0x20}, {315, 0x3b0, 0x670, 0x2b8, 0x600, 0x20},
    {325, 0x3b0, 0x678, 0x2b8, 0x608, 0x20}, {330, 0x3b0, 0x680, 0x2b8, 0x610, 0x20},
    {335, 0x3b0, 0x688, 0x2b8, 0x618, 0x20}, {340, 0x3b0, 0x690, 0x2b8, 0x620, 0x20},
    {345, 0x3b0, 0x698, 0x2b8, 0x628, 0x20}, {350, 0x3b0, 0x6a0, 0x2b8, 0x630, 0x20},
    {355, 0x3b0, 0x6a8, 0x2b8, 0x638, 0x20}, {360, 0x3b0, 0x6b0, 0x2b8, 0x640, 0x20},
    {365, 0x3b0, 0x6b8, 0x2b8, 0x648, 0x20}, {370, 0x3b0, 0x6c0, 0x2b8, 0x650, 0x20},
    {375, 0x3b0, 0x6c8, 0x2b8, 0x658, 0x20}, {380, 0x3b0, 0x6d0, 0x2b8, 0x660, 0x20},
    {385, 0x3b0, 0x6d8, 0x2b8, 0x668, 0x20}, {390, 0x3b0, 0x6e0, 0x2b8, 0x670, 0x20},
    {395, 0x3b0, 0x6e8, 0x2b8, 0x678, 0x20}, {400, 0x3b0, 0x6f0, 0x2b8, 0x680, 0x20},
    {405, 0x3b0, 0x6f8, 0x2b8, 0x688, 0x20}, {410, 0x3b0, 0x700, 0x2b8, 0x690, 0x20},
    {415, 0x3b0, 0x708, 0x2b8, 0x698, 0x20}, {420, 0x3b0, 0x710, 0x2b8, 0x6a0, 0x20},
    {425, 0x3b0, 0x718, 0x2b8, 0x6a8, 0x20}, {430, 0x3b0, 0x720, 0x2b8, 0x6b0, 0x20},
    {435, 0x3b0, 0x728, 0x2b8, 0x6b8, 0x20}, {440, 0x3b0, 0x730, 0x2b8, 0x6c0, 0x20},
    {445, 0x3b0, 0x738, 0x2b8, 0x6c8, 0x20}, {450, 0x3b0, 0x740, 0x2b8, 0x6d0, 0x20},
    {455, 0x3b0, 0x748, 0x2b8, 0x6d8, 0x20}, {460, 0x3b0, 0x750, 0x2b8, 0x6e0, 0x20},
    {465, 0x3b0, 0x758, 0x2b8, 0x6e8, 0x20}, {470, 0x3b0, 0x760, 0x2b8, 0x6f0, 0x20},
    {475, 0x3b0, 0x768, 0x2b8, 0x6f8, 0x20}, {480, 0x3b0, 0x770, 0x2b8, 0x700, 0x20},
    {482, 0x3b0, 0x770, 0x2b8, 0x700, 0x13}, {483, 0x3b8, 0x770, 0x2c0, 0x700, 0x13},
    {490, 0x3c0, 0x770, 0x2c8, 0x700, 0x13}, {497, 0x3c8, 0x770, 0x2d0, 0x700, 0x13},
    {504, 0x3d0, 0x770, 0x2d8, 0x700, 0x13}, {511, 0x3d0, 0x770, 0x2d8, 0x700, 0x14},
    {512, 0x3d8, 0x770, 0x2e0, 0x700, 0x14}, {519, 0x3e0, 0x770, 0x2e8, 0x700, 0x14},
    {526, 0x3e0, 0x770, 0x2e8, 0x700, 0xa},
};

// kDoorwayB2: 5 change points of x, y, camera, state (log ticks)
const LogChange kDoorwayB2[] = {
    {56, 0x140, 0x5f0, 0xd8, 0x580, 0xd}, {69, 0x140, 0x5e8, 0xd8, 0x580, 0xd},
    {78, 0x140, 0x5e0, 0xd8, 0x580, 0xd}, {79, 0x440, 0x170, 0x400, 0x100, 0x9},
    {87, 0x440, 0x170, 0x400, 0x100, 0xa},
};

struct ReplayResult {
    int records = 0, mismatches = 0;
    std::string first;
};

ReplayResult replay(const DosGfxFiles& files, const std::filesystem::path& start, std::string_view keys,
                    std::span<const LogChange> log, LogChange initial, int ticks, int stallAt, int stallTicks) {
    ReplayResult r;
    // The whole moment of the state: DGROUP, the services, the PANs and bank found in RAM, the back
    // buffer, the screen, the rand seed and the PIT phase (dos/game/load_state.h).
    const DosboxState st = DosboxState::load(start);
    dosgame::GameFixture f(files);
    DosGame& g = f.g();
    const LoadedState loaded = loadDosboxState(g, st);
    for (const std::string& w : loaded.warnings) MESSAGE("state: " << w);
    REQUIRE(f.setScript(keys));
    for (const dosgame::TickRecord& e : f.step(ticks)) {
        const auto t = static_cast<int>(e.tick) + 1;
        ++r.records;
        if (static_cast<int>(e.tick) >= stallAt && static_cast<int>(e.tick) < stallAt + stallTicks) continue;  // no log record
        LogChange want = initial;
        for (const LogChange& c : log)
            if (c.t <= t) want = c;
        if (e.x != want.x || e.y != want.y || e.camX != want.camX || e.camY != want.camY || e.state != want.state) {
            if (r.mismatches++ == 0)
                r.first = std::format("engine tick {} (log {}): x {:#x} y {:#x} cam {:#x},{:#x} state {:#x}, log x {:#x} y "
                                      "{:#x} cam {:#x},{:#x} state {:#x}",
                                      e.tick, t, e.x, e.y, e.camX, e.camY, e.state, want.x, want.y, want.camX, want.camY,
                                      want.state);
        }
    }
    return r;
}

}  // namespace

TEST_CASE("dos world: play-field redraws equal the back buffers of the DOSBox-X save states") {
    TM_REQUIRE_DOS_GAME_FILES();
    const auto captures = testdata::dosCapturesPath();
    if (!captures) {
        MESSAGE("TM_DOS_CAPTURES is not set: test skipped");
        return;
    }
    dosgame::FixtureOptions opts;
    opts.runGameInit = false;  // only the world state of each save is used
    dosgame::GameFixture f(*gameFiles, opts);
    DosGame& g = f.g();
    int compared = 0, scene3 = 0, scene4 = 0, slow = 0, masked = 0;
    for (const auto& path : saveStates(*captures)) {
        const DosboxState st = DosboxState::load(path);
        CAPTURE(path.filename().string());
        const auto d = st.dgroup();
        const auto backBuffer = st.backBuffer();
        const uint16_t scene = dsWord(d, dg::kLoadedScene);
        // In play only: no UI mode drawing over the field, the scene loaded, no search blink or
        // redraw pending between the last frame and the save, and not in a shelf room (the
        // collected treasures are drawn into the background there, ShelfRoom_DrawShelf).
        if (d[dg::kUiMode] != 0 || scene > 4 || scene != dsWord(d, dg::kLevel) || dsWord(d, dg::kSearchTimer) != 0 ||
            dsWord(d, dg::kFullRedrawReq) != 0 || dsWord(d, dg::kPartialRedrawReq) != 0)
            continue;
        if (d[dg::kInShelfRoom] || d[dg::kInShelfRoom + 1] || d[dg::kInShelfRoom + 2] || d[dg::kInShelfRoom + 3]) continue;
        loadFromDgroup(g, d);
        g.pan = &gameFiles->pan(panOfScene(scene));
        g.prevPan = &gameFiles->pan(previousPanOfScene(scene));
        std::copy(backBuffer.begin(), backBuffer.end(), g.back.bytes().begin());
        // The back buffer shows the camera latched by the last Frame_Begin (a tick may have moved
        // the live camera since): redraw the whole field there.
        g.world.cameraX = g.world.frameCamX;
        g.world.cameraY = g.world.frameCamY;
        g.world.fullRedrawReq = 1;
        g.world.partialRedrawReq = 0;
        g.core.uiMode = 0;
        frameBegin(g);
        playfieldRedraw(g);
        // A slow CPU never scrolls rows 112..143 horizontally (they keep an older picture).
        const int lastRow = g.core.slowCpu == 1 ? 0x6F : 0x8F;
        const auto mask = notScenery(*gameFiles, d, scene);
        int differing = 0;
        for (int y = 0x10; y <= lastRow; ++y)
            for (int x = 2; x <= 0x9D; ++x) {
                const size_t i = static_cast<size_t>(y) * BackBuffer::kRowBytes + x;
                if (!mask[i] && g.back.byteAt(x, y) != backBuffer[i]) ++differing;
            }
        masked += std::count(mask.begin(), mask.end(), true) != 0;
        CAPTURE(scene);
        CAPTURE(g.world.frameCamX);
        CAPTURE(g.world.frameCamY);
        CHECK(differing == 0);
        ++compared;
        scene3 += scene == 3;
        scene4 += scene == 4;
        slow += g.core.slowCpu == 1;
    }
    MESSAGE("compared " << compared << " states (" << scene3 << " castle top, " << scene4 << " treasure/rank room, " << slow
                        << " slow CPU, " << masked << " with sprites or rank-chest text masked)");
    CHECK(compared >= 100);
    CHECK(scene3 >= 1);  // the chute: stale MOUNT3 blocks under CLOSE.PAN
    CHECK(scene4 >= 1);
}

TEST_CASE("dos world: the level-3 bridge breaks and drops the player to level 2, tick for tick (r2/fl_bridge_l3)") {
    TM_REQUIRE_DOS_GAME_FILES();
    const auto captures = testdata::dosCapturesPath();
    if (!captures) return;
    const auto start = *captures / "savestates" / "r2" / "fl_l3_at_tree_tier2.sav";
    if (!std::filesystem::exists(start)) return;
    // Tier 2: walking right onto bridge 84, it breaks at x 0x230 (state 32, mask 0x1400), the player
    // falls 8 rows per 5 ticks through 94 (boundary: level 1) and A4, lands on B4's path at y 0x5F0 and
    // flips right to x 0x260 (states 19, 20, 10).
    const auto r = replay(*gameFiles, start, "146:down:right, 266:up:right, 1081:down:right, 1152:up:right", kBridgeL3,
                          {0, 0x160, 0x470, 0x80, 0x400, 0xA}, 1395, 1188, 4);
    MESSAGE("fl_bridge_l3: " << r.records << " records, " << r.mismatches << " differ" << (r.first.empty() ? "" : ": " + r.first));
    CHECK(r.records == 1395);
    CHECK(r.mismatches == 0);
}

TEST_CASE("dos world: the level-2 bridge breaks and drops the player to level 1, tick for tick (r2/fl_bridge_l2)") {
    TM_REQUIRE_DOS_GAME_FILES();
    const auto captures = testdata::dosCapturesPath();
    if (!captures) return;
    const auto start = *captures / "savestates" / "r2" / "fl_l2_at_nests_tier2.sav";
    if (!std::filesystem::exists(start)) return;
    // Tier 2: walking right onto bridge B7, it breaks at x 0x3B0, the player falls through C7
    // (boundary: level 1) and D7, lands on E7's path at y 0x770 and flips right to x 0x3E0.
    const auto r = replay(*gameFiles, start, "89:down:right, 256:up:right", kBridgeL2, {0, 0x2D0, 0x5F0, 0x1D8, 0x580, 0xA},
                          535, 319, 5);
    MESSAGE("fl_bridge_l2: " << r.records << " records, " << r.mismatches << " differ" << (r.first.empty() ? "" : ": " + r.first));
    CHECK(r.records == 535);
    CHECK(r.mismatches == 0);
}

TEST_CASE("dos world: the geyser hole slides the player down to level 2, tick for tick (r2/fl_geyser_slide)") {
    TM_REQUIRE_DOS_GAME_FILES();
    const auto captures = testdata::dosCapturesPath();
    if (!captures) return;
    const auto start = *captures / "savestates" / "r2" / "fl_level3_start.sav";
    if (!std::filesystem::exists(start)) return;
    // Walking left from x 0x770 on level 3: state 30 at x 0x750, the next tick snaps to 0x740 (the
    // camera follows) and slides 8 rows; 8 rows per 7-8 ticks through 8E (boundary: level 2) and 9E
    // to AE, where the slide ends at y 0x560 (state 10).
    const auto r = replay(*gameFiles, start, "161:down:left, 220:up:left", kGeyserSlide, {0, 0x770, 0x3F0, 0x6F0, 0x380, 0xA},
                          535, 304, 4);
    MESSAGE("fl_geyser_slide: " << r.records << " records, " << r.mismatches << " differ" << (r.first.empty() ? "" : ": " + r.first));
    CHECK(r.records == 535);
    CHECK(r.mismatches == 0);
}

TEST_CASE("dos world: the mine-tunnel doorway and its dissolve, tick for tick (r2/fl_l2door)") {
    TM_REQUIRE_DOS_GAME_FILES();
    const auto captures = testdata::dosCapturesPath();
    if (!captures) return;
    const auto start = *captures / "savestates" / "r2" / "fl_l2door.sav";
    if (!std::filesystem::exists(start)) return;
    // Up in front of doorway B2: the look-up rise, the door fires at y 0x5E0 (below 0x68 in the cell),
    // the player lands in the tunnel at (0x440, 0x170) with camera (0x400, 0x100); the dissolve keeps
    // Player_Update off until it ends, then state 9 gives way to 10.
    const auto r = replay(*gameFiles, start, "55:down:up, 70:up:up", kDoorwayB2, {0, 0x140, 0x5F0, 0xD8, 0x580, 0xA}, 195,
                          100000, 0);
    MESSAGE("fl_l2door: " << r.records << " records, " << r.mismatches << " differ" << (r.first.empty() ? "" : ": " + r.first));
    CHECK(r.records == 195);
    CHECK(r.mismatches == 0);
}
