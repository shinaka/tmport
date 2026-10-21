#pragma once
// Loading DAM scripts and animation frames from the user's TMT.DLL / TMT256.DLL
// (re/docs/code_1098-10d8.md §9.1; fmt_othr_tables.md §4.2, §4.5).

#include <vector>

#include "anim/anim_engine.h"
#include "anim/dam_script.h"
#include "res/resource_manager.h"

namespace tmport::anim {

// An OTHR resource as a DAM script (bytes with the NE-rounded size, as GetResource loads it).
// Throws mischief::Error if the resource does not exist. A wrong tag is only logged: the
// interpreter itself checks nothing but "DOP".
ScriptPtr loadScript(const ResourceManager& res, int id);

struct LoadedBundle {
    int bndlId = 0;
    ScriptPtr script;          // member 0
    int frameCount = 0;        // members - 1 (the return value of Anim_LoadBundle)
    std::vector<int> memberIds;
};

// 10d8:0000 Anim_LoadBundle: member 0 of BNDL bndlId is the DAM script, members 1..n-1 go into
// frame slots frameBase..frameBase+n-2. Throws mischief::Error if the BNDL does not exist; a
// missing PMAP member leaves its slot empty (logged).
LoadedBundle loadBundle(const ResourceManager& res, AnimEngine& anim, int bndlId, int frameBase);

// The frame loop of TreasureHall_LoadRoom (1190:009b): every member of a picture bundle
// (BNDL 330+t) into slots frameBase.. . Returns the member count. Throws like loadBundle.
int loadFrames(const ResourceManager& res, AnimEngine& anim, int bndlId, int frameBase);

// Member `index` of a picture bundle as a PixMap (e.g. the delivered treasure, BNDL
// 330 + (climbs-1) % 43 member 0, stored in slot 80 by 10f0:00d8). nullptr if missing.
std::shared_ptr<const gfx::PixMap> bundleMember(const ResourceManager& res, int bndlId, int index);

}  // namespace tmport::anim
