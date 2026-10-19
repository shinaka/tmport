// The player modules of the DOS edition (src/dos/game/player.cpp, anim.cpp): the script timing
// rules and the player's movement compared tick for tick with the frame logger of the reference
// recordings (extracted/captures/dos/savestates/r2/fl_*, read with stepchanges.py): walking, the
// camera follow, climbing, tapped and held jumps, the net swing, the coin throw. The expected
// values are the logs' per-tick numbers (positions, states, key bits, coins) as change points.
//
// Alignment: a run that starts at log record t0 places the player at that record's values; engine
// tick e then corresponds to log tick e - 1 + t0, and a key first seen in log record t is pressed
// before engine tick t - t0 + 1.
#include <doctest/doctest.h>

#include <iterator>

#include "dos/game/player.h"
#include "test_dos_player_harness.h"

using namespace tmport;
using namespace tmport::dos;
using namespace dosplayer;

namespace {

// ---- reference logs (extracted/scratch/dos_w3_player/gen_expect.py) ---------------------------

// kWalk2: fl_walk2.sav records 0..543; 71 change points
constexpr LogPoint kWalk2[] = {
    {0, 0x1c0, 0x7f0, 0x120, 0x780, 10, 0x0, 5}, {88, 0x1c0, 0x7f0, 0x120, 0x780, 10, 0x8, 5},
    {89, 0x1c0, 0x7f0, 0x120, 0x780, 12, 0x8, 5}, {98, 0x1c8, 0x7f0, 0x120, 0x780, 12, 0x8, 5},
    {103, 0x1d0, 0x7f0, 0x120, 0x780, 12, 0x8, 5}, {108, 0x1d8, 0x7f0, 0x120, 0x780, 12, 0x8, 5},
    {113, 0x1e0, 0x7f0, 0x120, 0x780, 12, 0x8, 5}, {118, 0x1e8, 0x7f0, 0x120, 0x780, 12, 0x8, 5},
    {123, 0x1f0, 0x7f0, 0x120, 0x780, 12, 0x8, 5}, {128, 0x1f8, 0x7f0, 0x120, 0x780, 12, 0x8, 5},
    {133, 0x200, 0x7f0, 0x120, 0x780, 12, 0x8, 5}, {139, 0x208, 0x7f0, 0x120, 0x780, 12, 0x8, 5},
    {144, 0x210, 0x7f0, 0x120, 0x780, 12, 0x8, 5}, {149, 0x218, 0x7f0, 0x120, 0x780, 12, 0x8, 5},
    {154, 0x220, 0x7f0, 0x128, 0x780, 12, 0x8, 5}, {159, 0x228, 0x7f0, 0x130, 0x780, 12, 0x8, 5},
    {164, 0x230, 0x7f0, 0x138, 0x780, 12, 0x8, 5}, {169, 0x238, 0x7f0, 0x140, 0x780, 12, 0x8, 5},
    {174, 0x240, 0x7f0, 0x148, 0x780, 12, 0x8, 5}, {180, 0x248, 0x7f0, 0x150, 0x780, 12, 0x8, 5},
    {185, 0x250, 0x7f0, 0x158, 0x780, 12, 0x8, 5}, {190, 0x258, 0x7f0, 0x160, 0x780, 12, 0x8, 5},
    {195, 0x260, 0x7f0, 0x168, 0x780, 12, 0x8, 5}, {200, 0x268, 0x7f0, 0x170, 0x780, 12, 0x8, 5},
    {205, 0x270, 0x7f0, 0x178, 0x780, 12, 0x8, 5}, {210, 0x278, 0x7f0, 0x180, 0x780, 12, 0x8, 5},
    {215, 0x280, 0x7f0, 0x188, 0x780, 12, 0x8, 5}, {221, 0x288, 0x7f0, 0x190, 0x780, 12, 0x8, 5},
    {226, 0x290, 0x7f0, 0x198, 0x780, 12, 0x8, 5}, {231, 0x298, 0x7f0, 0x1a0, 0x780, 12, 0x8, 5},
    {236, 0x2a0, 0x7f0, 0x1a8, 0x780, 12, 0x8, 5}, {241, 0x2a8, 0x7f0, 0x1b0, 0x780, 12, 0x8, 5},
    {246, 0x2b0, 0x7f0, 0x1b8, 0x780, 12, 0x8, 5}, {251, 0x2b8, 0x7f0, 0x1c0, 0x780, 12, 0x8, 5},
    {256, 0x2c0, 0x7f0, 0x1c8, 0x780, 12, 0x8, 5}, {262, 0x2c8, 0x7f0, 0x1d0, 0x780, 12, 0x8, 5},
    {267, 0x2d0, 0x7f0, 0x1d8, 0x780, 12, 0x8, 5}, {272, 0x2d8, 0x7f0, 0x1e0, 0x780, 12, 0x8, 5},
    {277, 0x2e0, 0x7f0, 0x1e8, 0x780, 12, 0x8, 5}, {279, 0x2e0, 0x7f0, 0x1e8, 0x780, 12, 0x0, 5},
    {280, 0x2e0, 0x7f0, 0x1e8, 0x780, 10, 0x0, 5}, {294, 0x2e0, 0x7f0, 0x1e8, 0x780, 10, 0x4, 5},
    {295, 0x2e0, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5}, {304, 0x2d8, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5},
    {309, 0x2d0, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5}, {314, 0x2c8, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5},
    {319, 0x2c0, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5}, {324, 0x2b8, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5},
    {329, 0x2b0, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5}, {334, 0x2a8, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5},
    {339, 0x2a0, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5}, {345, 0x298, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5},
    {350, 0x290, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5}, {355, 0x288, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5},
    {360, 0x280, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5}, {365, 0x278, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5},
    {370, 0x270, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5}, {375, 0x268, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5},
    {380, 0x260, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5}, {386, 0x258, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5},
    {391, 0x250, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5}, {396, 0x248, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5},
    {401, 0x240, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5}, {406, 0x238, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5},
    {411, 0x230, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5}, {416, 0x228, 0x7f0, 0x1e8, 0x780, 11, 0x4, 5},
    {421, 0x220, 0x7f0, 0x1e0, 0x780, 11, 0x4, 5}, {427, 0x218, 0x7f0, 0x1d8, 0x780, 11, 0x4, 5},
    {432, 0x210, 0x7f0, 0x1d0, 0x780, 11, 0x4, 5}, {437, 0x208, 0x7f0, 0x1c8, 0x780, 11, 0x0, 5},
    {438, 0x208, 0x7f0, 0x1c8, 0x780, 10, 0x0, 5},
};
// kActions: fl_actions.sav records 0..618; 33 change points
constexpr LogPoint kActions[] = {
    {0, 0x1c0, 0x7f0, 0x120, 0x780, 10, 0x0, 5}, {55, 0x1c0, 0x7f0, 0x120, 0x780, 10, 0x10, 5},
    {56, 0x1c0, 0x7f0, 0x120, 0x780, 24, 0x10, 5}, {59, 0x1c0, 0x7f0, 0x120, 0x780, 24, 0x0, 5},
    {106, 0x1c0, 0x7f0, 0x120, 0x780, 25, 0x0, 5}, {107, 0x1c0, 0x7f0, 0x120, 0x780, 26, 0x0, 5},
    {136, 0x1c0, 0x7f0, 0x120, 0x780, 10, 0x0, 5}, {184, 0x1c0, 0x7f0, 0x120, 0x780, 10, 0x2, 5},
    {185, 0x1c0, 0x7f0, 0x120, 0x780, 10, 0x0, 5}, {313, 0x1c0, 0x7f0, 0x120, 0x780, 10, 0x24, 5},
    {314, 0x1c0, 0x7f0, 0x120, 0x780, 17, 0x24, 5}, {317, 0x1c0, 0x7f0, 0x120, 0x780, 17, 0x0, 5},
    {322, 0x1b8, 0x7f0, 0x120, 0x780, 17, 0x0, 5}, {329, 0x1b0, 0x7f0, 0x120, 0x780, 17, 0x0, 5},
    {336, 0x1a8, 0x7f0, 0x120, 0x780, 17, 0x0, 5}, {343, 0x1a0, 0x7f0, 0x120, 0x780, 17, 0x0, 5},
    {350, 0x198, 0x7f0, 0x120, 0x780, 17, 0x0, 5}, {357, 0x198, 0x7f0, 0x120, 0x780, 18, 0x0, 5},
    {358, 0x190, 0x7f0, 0x120, 0x780, 18, 0x0, 5}, {365, 0x188, 0x7f0, 0x120, 0x780, 18, 0x0, 5},
    {372, 0x188, 0x7f0, 0x120, 0x780, 10, 0x0, 5}, {442, 0x188, 0x7f0, 0x120, 0x780, 10, 0x28, 5},
    {443, 0x188, 0x7f0, 0x120, 0x780, 19, 0x28, 5}, {446, 0x188, 0x7f0, 0x120, 0x780, 19, 0x0, 5},
    {451, 0x190, 0x7f0, 0x120, 0x780, 19, 0x0, 5}, {458, 0x198, 0x7f0, 0x120, 0x780, 19, 0x0, 5},
    {465, 0x1a0, 0x7f0, 0x120, 0x780, 19, 0x0, 5}, {472, 0x1a8, 0x7f0, 0x120, 0x780, 19, 0x0, 5},
    {479, 0x1b0, 0x7f0, 0x120, 0x780, 19, 0x0, 5}, {486, 0x1b0, 0x7f0, 0x120, 0x780, 20, 0x0, 5},
    {487, 0x1b8, 0x7f0, 0x120, 0x780, 20, 0x0, 5}, {494, 0x1c0, 0x7f0, 0x120, 0x780, 20, 0x0, 5},
    {501, 0x1c0, 0x7f0, 0x120, 0x780, 10, 0x0, 5},
};
// kJumps: fl_jumps.sav records 0..751; 71 change points. Record 274 holds the grey-key E0 artefact
// (the jump bit dropped for one tick, an option that is off by default): its keys are not compared.
constexpr LogPoint kJumps[] = {
    {0, 0x1c0, 0x7f0, 0x120, 0x780, 10, 0x0, 5}, {55, 0x1c0, 0x7f0, 0x120, 0x780, 10, 0x8, 5},
    {56, 0x1c0, 0x7f0, 0x120, 0x780, 12, 0x8, 5}, {65, 0x1c8, 0x7f0, 0x120, 0x780, 12, 0x8, 5},
    {70, 0x1d0, 0x7f0, 0x120, 0x780, 12, 0x8, 5}, {75, 0x1d8, 0x7f0, 0x120, 0x780, 12, 0x8, 5},
    {80, 0x1e0, 0x7f0, 0x120, 0x780, 12, 0x8, 5}, {85, 0x1e8, 0x7f0, 0x120, 0x780, 12, 0x8, 5},
    {90, 0x1f0, 0x7f0, 0x120, 0x780, 12, 0x8, 5}, {95, 0x1f8, 0x7f0, 0x120, 0x780, 12, 0x8, 5},
    {100, 0x200, 0x7f0, 0x120, 0x780, 12, 0x8, 5}, {103, 0x200, 0x7f0, 0x120, 0x780, 12, 0x0, 5},
    {104, 0x200, 0x7f0, 0x120, 0x780, 10, 0x0, 5}, {195, 0x200, 0x7f0, 0x120, 0x780, 10, 0x28, 5},
    {196, 0x200, 0x7f0, 0x120, 0x780, 19, 0x28, 5}, {204, 0x208, 0x7f0, 0x120, 0x780, 19, 0x28, 5},
    {211, 0x210, 0x7f0, 0x120, 0x780, 19, 0x28, 5}, {218, 0x218, 0x7f0, 0x120, 0x780, 19, 0x28, 5},
    {225, 0x220, 0x7f0, 0x128, 0x780, 19, 0x28, 5}, {232, 0x228, 0x7f0, 0x130, 0x780, 19, 0x28, 5},
    {240, 0x230, 0x7f0, 0x138, 0x780, 19, 0x28, 5}, {247, 0x238, 0x7f0, 0x140, 0x780, 19, 0x28, 5},
    {254, 0x240, 0x7f0, 0x148, 0x780, 19, 0x28, 5}, {261, 0x248, 0x7f0, 0x150, 0x780, 19, 0x28, 5},
    {268, 0x248, 0x7f0, 0x150, 0x780, 20, 0x28, 5}, {269, 0x250, 0x7f0, 0x158, 0x780, 20, 0x28, 5},
    {274, 0x250, 0x7f0, 0x158, 0x780, 20, 0xffff, 5}, {275, 0x250, 0x7f0, 0x158, 0x780, 20, 0x28, 5},
    {276, 0x258, 0x7f0, 0x160, 0x780, 20, 0x28, 5}, {283, 0x258, 0x7f0, 0x160, 0x780, 19, 0x28, 5},
    {290, 0x258, 0x7f0, 0x160, 0x780, 19, 0x0, 5}, {291, 0x260, 0x7f0, 0x168, 0x780, 19, 0x0, 5},
    {298, 0x268, 0x7f0, 0x170, 0x780, 19, 0x0, 5}, {305, 0x270, 0x7f0, 0x178, 0x780, 19, 0x0, 5},
    {312, 0x278, 0x7f0, 0x180, 0x780, 19, 0x0, 5}, {319, 0x280, 0x7f0, 0x188, 0x780, 19, 0x0, 5},
    {326, 0x280, 0x7f0, 0x188, 0x780, 20, 0x0, 5}, {327, 0x288, 0x7f0, 0x190, 0x780, 20, 0x0, 5},
    {334, 0x290, 0x7f0, 0x198, 0x780, 20, 0x0, 5}, {341, 0x290, 0x7f0, 0x198, 0x780, 10, 0x0, 5},
    {386, 0x290, 0x7f0, 0x198, 0x780, 10, 0x28, 5}, {387, 0x290, 0x7f0, 0x198, 0x780, 19, 0x28, 5},
    {390, 0x290, 0x7f0, 0x198, 0x780, 19, 0x0, 5}, {395, 0x298, 0x7f0, 0x1a0, 0x780, 19, 0x0, 5},
    {402, 0x2a0, 0x7f0, 0x1a8, 0x780, 19, 0x0, 5}, {409, 0x2a8, 0x7f0, 0x1b0, 0x780, 19, 0x0, 5},
    {416, 0x2b0, 0x7f0, 0x1b8, 0x780, 19, 0x0, 5}, {423, 0x2b8, 0x7f0, 0x1c0, 0x780, 19, 0x0, 5},
    {430, 0x2b8, 0x7f0, 0x1c0, 0x780, 20, 0x0, 5}, {431, 0x2c0, 0x7f0, 0x1c8, 0x780, 20, 0x0, 5},
    {438, 0x2c8, 0x7f0, 0x1d0, 0x780, 20, 0x0, 5}, {445, 0x2c8, 0x7f0, 0x1d0, 0x780, 10, 0x0, 5},
    {491, 0x2c8, 0x7f0, 0x1d0, 0x780, 10, 0x1, 5}, {492, 0x2c8, 0x7f0, 0x1d0, 0x780, 10, 0x0, 5},
    {502, 0x2c8, 0x7f0, 0x1d0, 0x780, 10, 0x8, 5}, {503, 0x2c8, 0x7f0, 0x1d0, 0x780, 12, 0x8, 5},
    {512, 0x2d0, 0x7f0, 0x1d8, 0x780, 12, 0x8, 5}, {517, 0x2d8, 0x7f0, 0x1e0, 0x780, 12, 0x8, 5},
    {522, 0x2e0, 0x7f0, 0x1e8, 0x780, 12, 0x8, 5}, {527, 0x2e8, 0x7f0, 0x1f0, 0x780, 12, 0x8, 5},
    {532, 0x2f0, 0x7f0, 0x1f8, 0x780, 12, 0x8, 5}, {537, 0x2f8, 0x7f0, 0x200, 0x780, 12, 0x8, 5},
    {542, 0x300, 0x7f0, 0x208, 0x780, 12, 0x8, 5}, {547, 0x308, 0x7f0, 0x210, 0x780, 12, 0x8, 5},
    {553, 0x310, 0x7f0, 0x218, 0x780, 12, 0x8, 5}, {558, 0x318, 0x7f0, 0x220, 0x780, 12, 0x8, 5},
    {563, 0x320, 0x7f0, 0x228, 0x780, 12, 0x8, 5}, {568, 0x328, 0x7f0, 0x230, 0x780, 12, 0x8, 5},
    {573, 0x330, 0x7f0, 0x238, 0x780, 12, 0x8, 5}, {574, 0x330, 0x7f0, 0x238, 0x780, 12, 0x0, 5},
    {575, 0x330, 0x7f0, 0x238, 0x780, 10, 0x0, 5},
};
// kClimb: fl_climb.sav records 240..420; 26 change points
constexpr LogPoint kClimb[] = {
    {240, 0x7c8, 0x740, 0x6f0, 0x6d0, 15, 0x500, 14}, {247, 0x7c8, 0x740, 0x6f0, 0x6d0, 15, 0x501, 14},
    {249, 0x7c8, 0x738, 0x6f0, 0x6c8, 15, 0x501, 14}, {256, 0x7c8, 0x730, 0x6f0, 0x6c0, 15, 0x501, 14},
    {263, 0x7c8, 0x728, 0x6f0, 0x6b8, 15, 0x501, 14}, {270, 0x7c8, 0x720, 0x6f0, 0x6b0, 15, 0x501, 14},
    {278, 0x7c8, 0x718, 0x6f0, 0x6a8, 15, 0x501, 14}, {285, 0x7c8, 0x710, 0x6f0, 0x6a0, 15, 0x501, 14},
    {292, 0x7c8, 0x708, 0x6f0, 0x698, 15, 0x501, 14}, {299, 0x7c8, 0x700, 0x6f0, 0x690, 15, 0x501, 14},
    {307, 0x7c8, 0x6f8, 0x6f0, 0x688, 15, 0x501, 14}, {314, 0x7c8, 0x6f0, 0x6f0, 0x680, 15, 0x501, 14},
    {321, 0x7c8, 0x6e8, 0x6f0, 0x678, 15, 0x501, 14}, {328, 0x7c8, 0x6e0, 0x6f0, 0x670, 15, 0x501, 14},
    {336, 0x7c8, 0x6d8, 0x6f0, 0x668, 15, 0x501, 14}, {343, 0x7c8, 0x6d0, 0x6f0, 0x660, 15, 0x501, 14},
    {350, 0x7c8, 0x6c8, 0x6f0, 0x658, 15, 0x501, 14}, {357, 0x7c8, 0x6c0, 0x6f0, 0x650, 15, 0x501, 14},
    {365, 0x7c8, 0x6b8, 0x6f0, 0x648, 15, 0x501, 14}, {372, 0x7c8, 0x6b0, 0x6f0, 0x640, 15, 0x501, 14},
    {379, 0x7c8, 0x6a8, 0x6f0, 0x638, 15, 0x501, 14}, {386, 0x7c8, 0x6a0, 0x6f0, 0x630, 15, 0x501, 14},
    {394, 0x7c8, 0x698, 0x6f0, 0x628, 15, 0x501, 14}, {401, 0x7c8, 0x690, 0x6f0, 0x620, 15, 0x501, 14},
    {408, 0x7c8, 0x688, 0x6f0, 0x618, 15, 0x501, 14}, {415, 0x7c8, 0x680, 0x6f0, 0x610, 15, 0x501, 14},
};
// kTypematic: fl_typematic.sav records 800..910 (keys not compared: the Down repeats depend on the
// press's sub-tick phase); 4 change points
constexpr LogPoint kTypematic[] = {
    {800, 0x330, 0x7f0, 0x238, 0x780, 10, 0xffff, 5}, {808, 0x330, 0x7f0, 0x238, 0x780, 28, 0xffff, 4},
    {879, 0x330, 0x7f0, 0x238, 0x780, 5, 0xffff, 4}, {909, 0x330, 0x7f0, 0x238, 0x780, 10, 0xffff, 4},
};

template <size_t N>
CompareResult runLog(Harness& h, const LogPoint (&log)[N], uint64_t t1, uint16_t prevScript, std::string_view keys) {
    const uint64_t t0 = log[0].t;
    h.place(log[0], prevScript);
    h.script(keys);
    h.step(t1 - t0 + 1);
    return compareWithLog(h, log, N, t0, t1);
}

// The ticks (1, 2, ...) at which Anim_Tick moved the player (consumes g_playerDX/DY each tick).
struct MoveTick {
    int tick, dx, dy;
};
std::vector<MoveTick> runScript(DosGame& g, int ticks, bool holdTick3 = false) {
    std::vector<MoveTick> moves;
    for (int t = 1; t <= ticks; ++t) {
        g.stepGate().clear();
        if (holdTick3 && t == 3) g.stepGate().mark();  // a step already done in this frame
        animTick(g);
        if (g.player.obj.dx != 0 || g.player.obj.dy != 0) moves.push_back({t, g.player.obj.dx, g.player.obj.dy});
        g.player.obj.dx = 0;
        g.player.obj.dy = 0;
    }
    return moves;
}

}  // namespace

TEST_CASE("DOS player: script timing, Anim_Set against Anim_SetIfIdle (walk 41 ticks per 8 steps)") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    SpriteSlot& s1 = g.player.slots[1];

    // Anim_Set shows the first entry at once (a leading op is skipped), and the first tick
    // processes entry 0 again with its op: the walk's 8 steps at ticks 1, 6, ..., 36, the end at 41.
    animSet(g, 1, 5);
    CHECK(s1.anim == 5);
    CHECK(s1.frame == static_cast<uint16_t>(animScriptWord(g, g.tables, 5, 4)));
    auto moves = runScript(g, 45);
    REQUIRE(moves.size() == 8);
    for (size_t k = 0; k < moves.size(); ++k) {
        CHECK(moves[k].tick == 1 + 5 * static_cast<int>(k));
        CHECK(moves[k].dx == 4);  // bytes: 8 px
    }
    CHECK(s1.anim == 0);
    CHECK(s1.prev == 5);

    // Anim_SetIfIdle: no frame until the next tick, which processes entry 0: the same schedule.
    animClear(g, 1);
    animSetIfIdle(g, 1, 4);
    CHECK(s1.frame == 0xFFFF);
    moves = runScript(g, 45);
    REQUIRE(moves.size() == 8);
    CHECK(moves.front().tick == 1);
    CHECK(moves.front().dx == -4);
    CHECK(moves.back().tick == 36);

    // A pending step (g_stepDone at the tick's entry) holds every due advance for that tick.
    animClear(g, 1);
    animSetIfIdle(g, 1, 0xC);  // climb up: 4 entries, delay 6, dy -8
    moves = runScript(g, 30);
    REQUIRE(moves.size() == 4);
    CHECK(moves[1].tick == 8);
    animClear(g, 1);
    animSetIfIdle(g, 1, 0xC);
    moves = runScript(g, 30, true);  // tick 3 held: entry 0 was due at tick 1, so nothing changes
    REQUIRE(moves.size() == 4);
    CHECK(moves[0].tick == 1);
    CHECK(moves[1].tick == 8);
    animClear(g, 1);
    animSetIfIdle(g, 1, 0xC);
    g.stepGate().mark();
    animTick(g);  // tick 1 held: entry 0 is retried at the next tick
    CHECK(g.player.obj.dy == 0);
    CHECK(s1.delay == 0);
    moves = runScript(g, 30);
    REQUIRE(moves.size() == 4);
    CHECK(moves[0].tick == 1);  // one tick later than unheld

    // Anim_Queue: only when the last script differs; Anim_Stop keeps the id for the caller.
    animClear(g, 1);
    s1.prev = 0x16;
    animQueue(g, 1, 0x16);
    CHECK(s1.anim == 0);
    animQueue(g, 1, 0x19);
    CHECK(s1.anim == 0x19);
    // A script restarted in the tick its -10 ran processes entry 0 at the next tick: 41 per cycle.
    animClear(g, 1);
    animSetIfIdle(g, 1, 5);
    int stepsSeen = 0, lastStep = 0;
    for (int t = 1; t <= 82; ++t) {
        g.stepGate().clear();
        animTick(g);
        if (g.player.obj.dx != 0) {
            ++stepsSeen;
            lastStep = t;
        }
        g.player.obj.dx = 0;
        animSetIfIdle(g, 1, 5);  // PlrSt_12_WalkRight's "keep walking"
    }
    CHECK(stepsSeen == 16);
    CHECK(lastStep == 41 + 36);
}

TEST_CASE("DOS player: walking and the camera follow, tick for tick (r2/fl_walk2)") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    // Right from log tick 88 to 279, Left from 294 to 437: state 12 one tick after the key, the first
    // 8-px step 9 ticks later, then 41 ticks per 8 steps; the camera scrolls once the player is at
    // 0x7C bytes (right) or 0x20 bytes (left) on the screen.
    const auto r = runLog(h, kWalk2, 543, 6, "89:down:right 280:up:right 295:down:left 438:up:left");
    MESSAGE("fl_walk2: " << r.records << " records, " << r.mismatches << " differ" << (r.first.empty() ? "" : ": " + r.first));
    CHECK(r.records == 544);
    CHECK(r.mismatches == 0);
}

TEST_CASE("DOS player: net swing, tapped jumps left and right, tick for tick (r2/fl_actions)") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    // Space at 55: net right (facing right) 50 + 1 + 29 = 80 ticks in states 24/25/26. Down at 184 on
    // the club door's path line: the terrain drops it. Home at 313: jump left, 43 + 15 = 58 ticks,
    // 7 steps of 8 px, the first 8 ticks after the jump state. PgUp at 442: the same to the right.
    const auto r = runLog(h, kActions, 618, 3,
                          "56:down:space 60:up:space 185:down:down 187:up:down 314:down:home 318:up:home 443:down:pgup "
                          "447:up:pgup");
    MESSAGE("fl_actions: " << r.records << " records, " << r.mismatches << " differ" << (r.first.empty() ? "" : ": " + r.first));
    CHECK(r.records == 619);
    CHECK(r.mismatches == 0);
    // The durations in engine ticks.
    auto firstTick = [&h](uint16_t state, uint64_t from) {
        for (uint64_t e = from; e <= h.recs().size(); ++e)
            if (h.at(e).state == state) return e;
        return uint64_t{0};
    };
    const uint64_t net = firstTick(24, 1), retract = firstTick(26, net), netEnd = firstTick(10, retract);
    CHECK(retract - net == 51);
    CHECK(netEnd - net == 80);
    const uint64_t jump = firstTick(17, netEnd), land = firstTick(18, jump), jumpEnd = firstTick(10, land);
    CHECK(land - jump == 43);
    CHECK(jumpEnd - jump == 58);
}

TEST_CASE("DOS player: held and tapped jumps chain without landing, tick for tick (r2/fl_jumps)") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    // PgUp held 195..290 (typematic repeats keep the jump bit): state 19 for 72 ticks (0xF + 0x11),
    // a landing of 15, a second jump chained from state 20; a tap at 386; Up at 491 is dropped by
    // the path (no jump with Right afterwards); a walk at 502.
    const auto r = runLog(h, kJumps, 751, 3,
                          "56:down:right 104:up:right 196:down:pgup 291:up:pgup 387:down:pgup 391:up:pgup 492:down:up "
                          "494:up:up 503:down:right 575:up:right");
    MESSAGE("fl_jumps: " << r.records << " records, " << r.mismatches << " differ" << (r.first.empty() ? "" : ": " + r.first));
    CHECK(r.records == 752);
    CHECK(r.mismatches == 0);
}

TEST_CASE("DOS player: climbing 29 ticks per 4 steps with the camera, tick for tick (r2/fl_climb)") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    h.setScene(0);
    // On the trunk ladder (cells EF/DF, camera mode 0x400): Up at 247, 8 rows per step at 7, 7, 7, 8
    // ticks, the camera following every step (screen row 0x70 <= the 0x70 limit of mask 0x400).
    const auto r = runLog(h, kClimb, 420, 0xC, "8:down:up");
    MESSAGE("fl_climb: " << r.records << " records, " << r.mismatches << " differ" << (r.first.empty() ? "" : ": " + r.first));
    CHECK(r.records == 181);
    CHECK(r.mismatches == 0);
}

TEST_CASE("DOS player: the coin throw (71 ticks) and the search start (r2/fl_typematic)") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    // Cell F6 in front of the player: a hiding place (picture 0 is accepted) holding nothing.
    g.world.cellPicture[0xF6] = 0;
    g.world.cellContent[0xF6] = 0x80;
    h.place(kTypematic[0], 3);  // stopped after walking right: facing right
    // Down at log tick 807 (10.4 ms after tick 806, the phase that reproduces the log's repeats)
    // becomes the coin key on the path (Input_MouseWalk), held until 902. (Host key events carry
    // the current time: they are issued when the run has reached the tick before.)
    h.step(807 - 800);
    h.keyAt("down", true, 807 - 800 + 1, 10400);
    h.step(902 - 807);
    h.keyAt("down", false, 902 - 800 + 1, 5000);
    h.step(130 - h.ticks());
    // Exactly as the log while the coin flies (state 28 for 71 ticks) and the search runs.
    const auto r = compareWithLog(h, kTypematic, std::size(kTypematic), 800, 908);
    MESSAGE("fl_typematic: " << r.records << " records, " << r.mismatches << " differ" << (r.first.empty() ? "" : ": " + r.first));
    CHECK(r.records == 109);
    CHECK(r.mismatches == 0);
    CHECK(h.sound().calls().size() >= 2);  // the throw (sfx 5) and the POOF (sfx 0) of script 0x1B
    // The search counts frames: 37 frames from timer 0x39. In lockstep (one frame per tick) state 5
    // lasts 36 ticks; DOSBox-X ran the search frames at 17-21 ms and left state 5 after 30 ticks.
    uint64_t end = 0;
    for (uint64_t e = 80; e <= h.recs().size() && !end; ++e)
        if (h.at(e).state == 10) end = e;
    CHECK(end - 1 + 800 == 879 + 36);
    CHECK(g.player.searchTimer == 0);
    CHECK(g.core.menuLock == 0);
    CHECK(g.world.cellContent[0xF6] == 0x80);  // nothing found; already marked searched (bit 7)
    CHECK(g.core.treasuresFound == 0);
}

TEST_CASE("DOS player: ladder jump 128 px in 91 ticks, falls, the torn net") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    uint16_t mask = 0x25FF;  // the castle ladder: Up/Down, ladder, camera mode, ladder jump
    setTerrainMaskHookForTests([&mask](DosGame& gg) { return gg.world.cellMask = mask; });
    h.place(LogPoint{0, 0x440, 0x5C0, 0x400, 0x540, 15, 0x2500, 31}, 0xC);
    g.core.slowCpu = 1;  // the quirk: a ladder jump clears the slow-CPU flag
    h.script("3:down:right 6:up:right");
    h.step(4);
    CHECK(g.player.playerState == 33);
    CHECK(g.core.slowCpu == 0);
    const uint64_t start = h.ticks();
    const uint16_t x0 = h.at(start).x;  // before the first entry (tick 4)
    h.step(95);
    // 16 of the 18 entries move 4 bytes (128 px); the script ends 91 ticks after Anim_Set, then
    // state 33 sees the ladder (mask 0x100) and goes back to climbing.
    CHECK(g.player.obj.x - x0 == 128);
    uint64_t back = 0;
    for (uint64_t e = start; e <= h.recs().size() && !back; ++e)
        if (h.at(e).state == 15) back = e;
    CHECK(back - start == 91);

    // A fall (mask 0x1000 without a ladder): 8 rows per 5 ticks with script 0x3F, then a flip.
    mask = 0x10FC;
    g.player.playerState = 10;
    h.place(LogPoint{0, 0x300, 0x370, 0x280, 0x300, 10, 0x1000, 31}, 2);
    h.step(1);
    CHECK(g.player.playerState == 31);
    const uint16_t y0 = g.player.obj.y;
    h.step(25);
    CHECK(g.player.obj.y - y0 == 5 * 8);
    mask = 0xFC;
    h.step(2);
    CHECK(g.player.playerState == 17);  // landed with a flip (0x10)

    // The net with no nets left: the torn-net scripts.
    mask = 0xFC;
    g.core.nets = 0;
    h.place(LogPoint{0, 0x300, 0x370, 0x280, 0x300, 10, 0, 31}, 2);
    playerNetLeft(g);
    CHECK(g.player.slots[1].anim == 0x15);
    CHECK(g.player.playerState == 21);
    g.core.nets = 3;
    animClear(g, 1);
    playerNetRight(g);
    CHECK(g.player.slots[1].anim == 0x17);
    CHECK(g.player.playerState == 24);
}

TEST_CASE("DOS player: Player_Move camera thresholds, the transition re-anchor and the row wrap") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    setTerrainMaskHookForTests([](DosGame& gg) { return gg.world.cellMask = 0xFC; });
    PlayerState& p = g.player;
    WorldState& w = g.world;
    // On the screen (0x60 bytes from the left edge): moves the player, not the camera.
    p.obj = Obj{0x200, 0x370, 4, 0};
    w.cameraX = 0x140;
    w.cameraY = 0x300;
    p.playerScrX = 0x60;
    p.playerScrY = 0x70;
    playerMove(g);
    CHECK(p.obj.x == 0x208);
    CHECK(w.cameraX == 0x140);
    CHECK(w.camStepX == 0);
    CHECK(p.slots[1].x == 0x64 + 2);
    CHECK(p.slots[1].y == 0x70 + 0x10);
    CHECK(p.slots[1].z == 0x33);  // on the path line: depth 0x32 + 1
    CHECK(p.obj.dx == 0);
    // At 0x7C bytes moving right: the camera scrolls, the player stays on the screen; a step.
    g.stepGate().clear();
    p.obj.dx = 4;
    p.playerScrX = 0x7C;
    w.cameraX = static_cast<uint16_t>(p.obj.x - 2 * 0x7C);
    playerMove(g);
    CHECK(w.camStepX == 4);
    CHECK(p.slots[1].x == 0x7C + 2);
    CHECK(g.stepDone());
    // Slow scroll doubles the step.
    w.slowScroll = 1;
    p.obj.dx = 4;
    p.playerScrX = 0x7C;
    w.cameraX = static_cast<uint16_t>(p.obj.x - 2 * 0x7C);
    const uint16_t xBefore = p.obj.x;
    playerMove(g);
    CHECK(p.obj.x - xBefore == 16);
    CHECK(w.camStepX == 8);
    w.slowScroll = 0;
    // The strip wrap: past x 0x7FF the player moves to the start of the next row; the camera
    // (0x88 bytes to his left, x 0xFF7C unsigned) wraps back to the end of the previous row.
    p.obj = Obj{0x7FC, 0x370, 4, 0};
    p.playerScrX = 0x40;
    p.playerScrY = 0x70;
    playerMove(g);
    CHECK(p.obj.x == 0x004);
    CHECK(p.obj.y == 0x3F0);
    CHECK(w.cameraY == 0x300);
    CHECK(w.cameraX == 0x77C);
    CHECK(p.slots[1].x == 0x44 + 2);
    // Left of the strip start (x >= 0x8000): back one row.
    p.obj = Obj{0x002, 0x3F0, -4, 0};
    w.curObj = DsPtr{kPlayerObjOff};
    p.obj.x = static_cast<uint16_t>(p.obj.x - 8);
    objWrapWorldX(g);
    CHECK(p.obj.x == 0x7FA);
    CHECK(p.obj.y == 0x370);
    // g_transition & 2: the camera keeps its x, y snaps to the row, the screen offset is recomputed.
    setTerrainMaskHookForTests([](DosGame& gg) {
        Obj& o = gg.player.obj;
        o.x = 0x6A4;  // a teleport inside the handler
        o.y = 0x1F0;
        gg.world.transition = 2;
        gg.world.cameraX = 0x600;
        return gg.world.cellMask = 0x14F0;
    });
    p.obj = Obj{0x400, 0x500, 0, 0};
    p.playerScrX = 0x10;
    p.playerScrY = 0x40;
    playerMove(g);
    CHECK(w.cameraY == 0x180);
    CHECK(w.cameraX == 0x600);
    CHECK(p.playerScrX == 0x52);
    CHECK(p.slots[1].x == 0x52 + 2);
    CHECK(p.slots[1].y == 0x70 + 0x10);
    CHECK(p.playerMask == 0x14F0);
}

TEST_CASE("DOS player: frozen states, the spawn and the coin key without coins") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    for (int frozen : {1, 2, 6, 7, 8, 36, 37, 38, 39, 40, 62, 72, 100}) {
        const auto s = static_cast<uint16_t>(frozen);
        g.player.playerState = s;
        g.keys() = 0xFF;
        callPlayerStateHandler(g, s);
        CHECK(g.player.playerState == s);
        CHECK(g.player.slots[1].anim == 0);
    }
    g.player.playerState = 3;
    callPlayerStateHandler(g, 3);
    CHECK(g.player.playerState == 4);
    // Arrival on a ladder off its centre: snapped to the cell's middle, a redraw.
    g.player.obj = Obj{0x4A3, 0x340, 0, 0};
    g.world.curObj = DsPtr{kPlayerObjOff};
    g.keys() = 0x0500;
    const uint16_t redraws = g.world.fullRedrawReq;
    callPlayerStateHandler(g, 4);
    CHECK(g.player.obj.x == 0x4C0);
    CHECK(g.world.fullRedrawReq == redraws + 1);
    CHECK(g.player.playerState == 15);
    CHECK(g.player.slots[1].anim == 0x1E);
    // The coin key with no coins: hint 7, and hint 6 when the nets are gone too.
    animClear(g, 1);
    g.player.slots[1].prev = 3;
    g.player.obj = Obj{0x300, 0x370, 0, 0};
    g.player.playerState = 10;
    g.core.coins = 0;
    g.core.nets = 0;
    g.elves.hintFlags[6] = 1;
    g.elves.hintFlags[7] = 0x81;  // shown already: stays
    g.keys() = 0x40;
    callPlayerStateHandler(g, 10);
    CHECK(g.player.playerState == 10);
    CHECK(g.elves.hintFlags[6] == 0);
    CHECK(g.elves.hintFlags[7] == 0x81);
    // With coins: one coin out at once, the toss script, the coin 25 bytes to the right.
    g.core.coins = 3;
    callPlayerStateHandler(g, 10);
    CHECK(g.core.coins == 2);
    CHECK(g.player.playerState == 28);
    CHECK(g.player.slots[1].anim == 0x1B);
    CHECK(g.player.slots[2].x == 25);
}

TEST_CASE("DOS player: walking, jumps, the net and climbing through the real main loop and cells (r2/fl_*)") {
    TM_REQUIRE_DOS_GAME_FILES();
    // The whole game tick (Game_Tick with Mouse_Poll, Input_Tick, Input_MouseWalk, Elves_Update,
    // Cells_UpdateVisible), the frame body and the world's cell handlers instead of the harness.
    struct Run {
        const LogPoint* log;
        size_t n;
        uint64_t t1;
        uint16_t prev;
        const char* keys;
    };
    const Run runs[] = {
        {kWalk2, std::size(kWalk2), 543, 6, "89:down:right 280:up:right 295:down:left 438:up:left"},
        {kActions, std::size(kActions), 618, 3,
         "56:down:space 60:up:space 185:down:down 187:up:down 314:down:home 318:up:home 443:down:pgup 447:up:pgup"},
        {kJumps, std::size(kJumps), 751, 3,
         "56:down:right 104:up:right 196:down:pgup 291:up:pgup 387:down:pgup 391:up:pgup 492:down:up 494:up:up "
         "503:down:right 575:up:right"},
        {kClimb, std::size(kClimb), 420, 0xC, "8:down:up"},
    };
    for (const Run& run : runs) {
        dosgame::GameFixture f(*gameFiles);
        DosGame& g = f.g();
        f.selectRecord(0);
        g.rec().setName("tester");
        const LogPoint& p0 = run.log[0];
        const uint64_t t0 = p0.t;
        f.placePlayer(p0.x, p0.y, p0.camX, p0.camY, 0, p0.state);
        g.core.coins = p0.coins;
        g.core.keyState = 2;  // the level-0 key found: the trunk is a ladder (Cell_P31_GateL0)
        g.keys() = p0.keys;
        g.player.playerMask = static_cast<uint16_t>(0x00FF | (p0.keys & 0xFF00));
        g.player.slots[1].prev = run.prev;
        g.world.fullRedrawReq = 1;
        REQUIRE(f.setScript(run.keys));
        const auto log = f.step(static_cast<int>(run.t1 - t0));
        // Fixture record k is the state after engine tick k + 1 = log record t0 + k + 1 (keys
        // excluded: the fixture takes them after the tick, the logger before the next one).
        int mismatches = 0;
        std::string first;
        for (uint64_t t = t0 + 1; t <= run.t1 && t - t0 - 1 < log.size(); ++t) {
            const auto& r = log[static_cast<size_t>(t - t0 - 1)];
            const LogPoint& l = logAt(run.log, run.n, t);
            if (r.x != l.x || r.y != l.y || r.camX != l.camX || r.camY != l.camY || r.state != l.state || r.coins != l.coins) {
                if (mismatches++ == 0)
                    first = std::format("t={}: x={:#x} y={:#x} cam=({:#x},{:#x}) state={} / log x={:#x} y={:#x} cam=({:#x},{:#x}) state={}",
                                        t, r.x, r.y, r.camX, r.camY, r.state, l.x, l.y, l.camX, l.camY, l.state);
            }
        }
        MESSAGE("real loop: " << log.size() << " records, " << mismatches << " differ" << (first.empty() ? "" : ": " + first));
        CHECK(mismatches == 0);
    }
}

TEST_CASE("DOS player: Sprites_Snapshot freezes ended scripts; Sprites_DrawSorted orders by z and counts g_dirty") {
    TM_REQUIRE_DOS_GAME_FILES();
    Harness h(*gameFiles);
    DosGame& g = h.g();
    animInitSlots(g);
    CHECK(g.sprites.saveHeapReset);
    CHECK(g.sprites.drawOrder[12] == 12);
    auto setSlot = [&g](int s, int16_t x, int16_t y, int16_t z, uint16_t frame, uint16_t prev) {
        SpriteSlot& sl = g.player.slots[static_cast<size_t>(s)];
        sl.x = x;
        sl.y = y;
        sl.z = z;
        sl.frame = frame;
        sl.owner = static_cast<uint16_t>(s);
        sl.anim = 0;
        sl.prev = prev;
    };
    setSlot(3, 0x20, 0x40, 0x30, 2 * 10, 0);    // a static sprite (prev 0): copied
    setSlot(4, 0x30, 0x40, 0x30, 2 * 11, 0);    // the same depth: ties go to the higher index first
    setSlot(5, 0x40, 0x40, 0x50, 2 * 12, 0x7E); // an ended script: its record stays frozen
    g.world.sceneWanted = g.core.level;
    spritesSnapshot(g, 0, kSpriteSlots);
    CHECK(g.sprites.records[3].x == 0x20);
    CHECK(g.sprites.records[4].frame == 2 * 11);
    CHECK(g.sprites.records[5].frame == 0xFFFF);  // skipped: still the Anim_InitSlots record
    CHECK(g.sprites.spritesChanged);
    g.player.slots[3].x = 0x28;
    g.player.slots[5].prev = 0;  // a direct slot write (prev = 0) makes it visible
    spritesSnapshot(g, 0, kSpriteSlots);
    CHECK(g.sprites.records[3].x == 0x28);
    CHECK(g.sprites.records[5].frame == 2 * 12);
    // Not the wanted scene: nothing is copied.
    g.world.sceneWanted = static_cast<uint16_t>(g.core.level + 1);
    g.player.slots[3].x = 0x2C;
    spritesSnapshot(g, 0, kSpriteSlots);
    CHECK(g.sprites.records[3].x == 0x28);
    // Drawn by descending z (slot 5 first), ties to the higher index (4 before 3); g_dirty counts them.
    g.core.dirty = 0;
    spritesDrawSorted(g, 0, kSpriteSlots);
    CHECK(g.core.dirty == 3);
    CHECK(g.sprites.drawOrder[0] == 5);
    CHECK(g.sprites.drawOrder[1] == 4);
    CHECK(g.sprites.drawOrder[2] == 3);
    spritesEraseAll(g, 0, kSpriteSlots);
    CHECK_FALSE(g.sprites.spritesChanged);
    CHECK(g.sprites.saveHeapReset);
}
