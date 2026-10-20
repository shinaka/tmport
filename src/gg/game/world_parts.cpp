// The part catalogue on the game (parts.h, "wave 3: the run"; re/gg/docs/code_vehicles_races.md §3,
// code_world.md §4.2, §4.3, §11.7): the catalogue of the race in GgState::world.parts, the carried lists
// (GgState::actors.playerParts / chimpParts: part indexes in list order, the original's 19c0 lists whose
// nodes are the part records themselves), the walks the HUD strip and the actors make over them, and
// the part half of the building block. Owner: world agent.
#include "gg/game/parts.h"

#include "gg/game/actors.h"
#include "gg/game/game.h"
#include "gg/game/hud.h"
#include "gg/game/world.h"
#include "gg/save/players_dat.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

// The position of a part in a carried list (the original walks its node pointers), -1 when it is not
// in the list.
int positionIn(const std::vector<int16_t>& list, int part) {
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i] == part) return static_cast<int>(i);
    return -1;
}

}  // namespace

// 153f:0002 Parts_LoadCatalogue: Res_LoadCopy(0x2f5 + g_raceIndex), the HUD icon bundle (+0x0a) into
// the cache, every record's icon, name copy and own index (PartCatalogue::load); the strip zones of
// the records (+0x48) start empty.
void partsLoadCatalogue(GgGame& g) {
    WorldObjects& w = g.worldObjects();
    w.unlinkPartZones(g);  // the old records' zones (Building_Free cleared the local list already)
    g.world.parts = PartCatalogue::load(g.archive(), g.cache, g.player.raceIndex);
    w.resetPartZones();
}

// 153f:00fd Parts_FreeCatalogue: PicCache_ReleaseBundle of the icon bundle, the name copies and the
// copy freed (g_levelDesc stays dangling in the original; the engine empties the catalogue).
void partsFreeCatalogue(GgGame& g) {
    PartCatalogue& c = g.world.parts;
    if (c.iconBundle != 0 || !c.parts.empty()) g.cache.releaseBundle(c.iconBundle);
    g.worldObjects().unlinkPartZones(g);
    c = PartCatalogue{};
}

// 153f:019f Part_Record
PartRecord& partRecord(GgGame& g, int part) { return g.world.parts.parts.at(static_cast<size_t>(part)); }
// 153f:015d Part_GetType
int partGetType(GgGame& g, int part) { return partRecord(g, part).category; }
// 153f:0173 Part_GetShelfPicId
uint16_t partGetShelfPicId(GgGame& g, int part) { return partRecord(g, part).shelfPicture; }
// 153f:0189 Part_IsNeeded
bool partIsNeeded(GgGame& g, int part) { return partRecord(g, part).required != 0; }
// 153f:0316 Parts_NeededCount: the description's +0x06
int partsNeededCount(GgGame& g) { return g.world.parts.neededCount; }

// 153f:0216 PartList_CountType(list, type, &last): every record of the list whose category (+0x08) is
// `type`; *last = the own index (+0x8c) of the last one, -1 when none.
int partListCountType(GgGame& g, const std::vector<int16_t>& list, int type, int* last) {
    int n = 0, l = -1;
    for (const int16_t p : list) {
        if (partRecord(g, p).category != type) continue;
        ++n;
        l = partRecord(g, p).index;
    }
    if (last) *last = l;
    return n;
}

// 153f:0276 PartList_FindType(list, type): the own index of the first record of that category. The
// original has no end check (it reads +0x8c of NULL when there is none; its callers know one exists):
// the engine returns -1 and logs.
int partListFindType(GgGame& g, const std::vector<int16_t>& list, int type) {
    for (const int16_t p : list)
        if (partRecord(g, p).category == type) return partRecord(g, p).index;
    log::debug("PartList_FindType: no part of type {} in the list (the original reads 0000:008c)", type);
    return -1;
}

// 153f:02bb PartList_CountHeldBy(actor): the records of the actor's list (+0x72: the player's own, the
// chimps' shared g_chimpParts) whose holder (+0x0c) is the actor. The projectile's list is NULL: 0.
int partListCountHeldBy(GgGame& g, int actor) {
    const std::vector<int16_t>* list = actor == kPlayerActor             ? &g.actors.playerParts
                                       : actor >= kFirstChimpActor && actor < kActorCount ? &g.actors.chimpParts
                                                                                    : nullptr;
    if (!list) return 0;
    int n = 0;
    for (const int16_t p : *list)
        if (partRecord(g, p).holder == actor) ++n;
    return n;
}

// 153f:0323 PartList_NextColumn(p): Node_Next until NULL or a record whose pair link (+0x1c) is
// negative (unpaired, or the lower part of a pair); the upper parts are skipped.
int partListNextColumn(GgGame& g, const std::vector<int16_t>& list, int part) {
    const int at = positionIn(list, part);
    if (at < 0) return -1;
    for (size_t i = static_cast<size_t>(at) + 1; i < list.size(); ++i)
        if (partRecord(g, list[i]).pair < 0) return list[i];
    return -1;
}

// 153f:0354 PartList_PrevColumn(p): the same backwards (Node_Prev).
int partListPrevColumn(GgGame& g, const std::vector<int16_t>& list, int part) {
    const int at = positionIn(list, part);
    for (int i = at - 1; i >= 0; --i)
        if (partRecord(g, list[static_cast<size_t>(i)]).pair < 0) return list[static_cast<size_t>(i)];
    return -1;
}

// 2bd4:0101 PartList_FindHeld(list, holder, start): from List_GetAt(start) on, cyclically, the first
// record held by `holder` and not built in (+0x36 & 0x8000); NULL for an empty list or after a full
// turn. List_GetAt gives the tail for an index past the end (unsigned compare), so a start at or past
// the count (a stale carried count, code_world.md §20 bug 7) tries the tail on every index up to the
// 16-bit wrap, then the list from 0 on and never comes back to `start`: the original hangs when no
// record qualifies. The engine returns -1 there and logs it.
int partListFindHeld(GgGame& g, const std::vector<int16_t>& list, int holder, int start) {
    const int count = static_cast<int>(list.size());
    if (count == 0) return -1;
    auto eligible = [&](int16_t p) {
        const PartRecord& r = partRecord(g, p);
        return !(r.flags & PartRecord::kInstalled) && r.holder == holder;
    };
    const auto first = static_cast<uint16_t>(start);
    if (first >= count) {
        if (eligible(list.back())) return list.back();  // every index from start to 0xffff is the tail
        for (const int16_t p : list)
            if (eligible(p)) return p;
        log::debug("PartList_FindHeld: start {} past {} parts and none held by {} (the original hangs)", start, count, holder);
        return -1;
    }
    int i = first;
    do {
        if (eligible(list[static_cast<size_t>(i)])) return list[static_cast<size_t>(i)];
        if (++i == count) i = 0;
    } while (i != first);
    return -1;
}

// 153f:0385 Parts_SaveState(dst): {holder +0x0c, flags +0x36} of every record in index order (block
// +0x014; 150 entries fit, the shipped catalogues have at most 36 records).
void partsSaveState(GgGame& g, BuildingBlock& block) {
    const auto& parts = g.world.parts.parts;
    if (parts.size() > block.parts.size())
        log::warn("Parts_SaveState: {} parts do not fit the block's {} entries (the original writes on)", parts.size(),
                  block.parts.size());
    for (size_t i = 0; i < parts.size() && i < block.parts.size(); ++i)
        block.parts[i] = PartState{parts[i].holder, parts[i].flags};
}

// 153f:03c9 Parts_RestoreState(src): for every record in index order: a saved holder other than -1 gets
// it back (Actor_AddPart, and for the player Hud_AddPart: the strip is laid out from each in turn),
// then the saved flags replace the record's (after Actor_AddPart kept only 0x4000 of them). Pairs are
// therefore rebuilt with the lower index as the lower part (code_vehicles_races.md §3.6).
void partsRestoreState(GgGame& g, const BuildingBlock& block) {
    auto& parts = g.world.parts.parts;
    for (size_t i = 0; i < parts.size() && i < block.parts.size(); ++i) {
        const PartState s = block.parts[i];
        const int part = static_cast<int>(i);
        if (s.holder != -1) {
            actorAddPart(g, s.holder, part);
            if (s.holder == 0) hudAddPart(g, part);
        }
        parts[i].flags = s.flags;
    }
}

}  // namespace tmport::gg
