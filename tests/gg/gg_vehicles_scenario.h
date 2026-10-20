#pragma once
// Scenario set-up for the vehicles tests (wave 3): a GameFixture put into a round 2 DOSBox-X state of the
// workshop, the races or the results. The heap structures of other modules that the vehicles module
// reads (the part catalogue with its holders, pairs and flags; the player's carried list) come from the
// owners' restores (worldRestoreFromState, actorsRestoreFromState) when they provide them, else from the
// state's RAM here, so the vehicles tests do not depend on the other areas' progress.

#include <filesystem>
#include <optional>
#include <string>

#include "gg/save/gg_state_file.h"
#include "gg_game_fixture.h"

namespace vehscen {

// The part catalogue of g_raceIndex (records parsed, the names loaded; no HUD icons) with each record's
// holder (+0x0c), pair link (+0x1c) and flags (+0x36), and the player's carried list (actor 0 +0x72),
// read from the state's RAM.
bool restoreParts(tmport::gg::GgGame& g, const tmport::gg::GgStateFile& s, std::string& why);

// f.loadState(sav), the parts (the owners' restores or restoreParts) and vehiclesRestoreFromState; the
// picture presented. Returns the state file, or nullopt with the reason in `why`.
std::optional<tmport::gg::GgStateFile> loadVehiclesState(gggame::GameFixture& f, const std::filesystem::path& sav,
                                                         std::string& why);

}  // namespace vehscen
