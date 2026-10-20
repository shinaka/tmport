#include "gg_vehicles_scenario.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/game/actors.h"
#include "gg/game/game.h"
#include "gg/game/parts.h"
#include "gg/game/workshop.h"
#include "gg/game/world.h"

using namespace tmport;
using namespace tmport::gg;

namespace vehscen {

namespace {

uint16_t rd16(std::span<const uint8_t> b, size_t o) { return o + 2 <= b.size() ? static_cast<uint16_t>(b[o] | (b[o + 1] << 8)) : 0; }
uint32_t farAt(std::span<const uint8_t> b, size_t o) {
    const uint32_t off = rd16(b, o), seg = rd16(b, o + 2);
    return off == 0 && seg == 0 ? 0 : seg * 16 + off;
}

}  // namespace

bool restoreParts(GgGame& g, const GgStateFile& s, std::string& why) {
    const int race = g.raceIndex();
    if (race < 0 || race > 14) {
        why = std::format("no building in progress (race index {})", race);
        return false;
    }
    PartCatalogue cat = PartCatalogue::parse(g.archive().read(static_cast<uint16_t>(PartCatalogue::kFirstId + race)));
    const uint32_t base = s.farPointerAt(dg::kParts);
    if (base == 0) {
        why = "g_parts is NULL in the state";
        return false;
    }
    for (size_t i = 0; i < cat.count(); ++i) {
        PartRecord& p = cat.parts[i];
        const auto rec = s.bytesAt(base + static_cast<uint32_t>(i * PartRecord::kSize), PartRecord::kSize);
        if (rec.size() != PartRecord::kSize) {
            why = "the part records are not in the state's RAM";
            return false;
        }
        p.holder = static_cast<int16_t>(rd16(rec, 0x0C));
        p.pair = static_cast<int16_t>(rd16(rec, 0x1C));
        p.flags = rd16(rec, 0x36);
        p.index = static_cast<int>(i);
        p.name = g.archive().read(p.nameId);  // Res_LoadCopy(+0x16)
    }
    g.world.parts = std::move(cat);
    // The player's list: actor 0 +0x72 (DS:7afc) -> {far head, far tail}; the nodes are part records.
    std::vector<int16_t> list;
    if (const uint32_t header = s.farPointerAt(static_cast<uint16_t>(dg::kActors + 0x72)); header != 0) {
        uint32_t node = farAt(s.bytesAt(header, 8), 0);
        for (int guard = 0; node != 0 && guard < 64; ++guard) {
            if (node < base || (node - base) % PartRecord::kSize != 0) {
                why = "a carried-list node outside the part records";
                return false;
            }
            list.push_back(static_cast<int16_t>((node - base) / PartRecord::kSize));
            node = farAt(s.bytesAt(node, 4), 0);
        }
    }
    g.actors.playerParts = std::move(list);
    return true;
}

std::optional<GgStateFile> loadVehiclesState(gggame::GameFixture& f, const std::filesystem::path& sav, std::string& why) {
    const auto missing = f.loadState(sav);
    if (!missing) {
        why = "not a G&G state";
        return std::nullopt;
    }
    std::optional<GgStateFile> s;
    try {
        s.emplace(GgStateFile::load(sav));
    } catch (const std::exception& e) {
        why = e.what();
        return std::nullopt;
    }
    GgGame& g = f.g();
    std::string w1, w2;
    const bool owners = worldRestoreFromState(g, *s, w1) && actorsRestoreFromState(g, *s, w2) && g.world.parts.count() > 0;
    if (!owners && g.raceIndex() >= 0 && g.raceIndex() <= 14 && !restoreParts(g, *s, why)) return std::nullopt;
    if (!vehiclesRestoreFromState(g, *s, why)) return std::nullopt;
    f.present();
    return s;
}

}  // namespace vehscen
