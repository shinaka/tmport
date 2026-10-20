#include "gg/ui/help.h"

#include <format>

#include "gg/data/archive.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

int16_t wordAt(std::span<const uint8_t> p, size_t i) {
    return 2 * i + 2 <= p.size() ? static_cast<int16_t>(p[2 * i] | (p[2 * i + 1] << 8)) : int16_t{0};
}

}  // namespace

std::optional<uint16_t> helpTopicOf(MenuCommand c) {
    switch (c) {
        case MenuCommand::HelpAboutGame: return kHelpAboutGame;                // 411d:01e0
        case MenuCommand::HelpBuildingVehicles: return kHelpBuildingVehicles;  // 411d:01ec
        case MenuCommand::HelpFindingParts: return kHelpFindingParts;          // 411d:01f8
        case MenuCommand::HelpMovingAround: return kHelpMovingAround;          // 411d:0204
        case MenuCommand::HelpChallengeRaces: return kHelpChallengeRaces;      // 411d:0210
        case MenuCommand::HelpSolvingPuzzles: return kHelpSolvingPuzzles;      // 411d:021c
        default: return std::nullopt;
    }
}

// 411d:0000 Help_ShowTopic: the arrays it builds on its stack
HelpTopic HelpTopic::load(const Archive& archive, uint16_t id) {
    HelpTopic t;
    t.bundle = Bundle::load(archive, id);
    const Bundle& b = t.bundle;
    auto member = [&](size_t i) {
        if (i >= b.size()) throw FormatError(std::format("help topic {:04x}: member {} missing", id, i));
        return b.member(i);
    };
    const int n = wordAt(member(0).payload, 0);
    if (n < 0 || n > kMaxPages) throw FormatError(std::format("help topic {:04x}: {} pages", id, n));
    t.title = text::untilNul(member(1).payload);
    size_t m = 2;
    t.pages.resize(static_cast<size_t>(n));
    for (Page& page : t.pages) {
        const int k = wordAt(member(m++).payload, 0);
        if (k < 0 || k > kMaxPictures) throw FormatError(std::format("help topic {:04x}: {} pictures", id, k));
        if (k == 0) continue;
        const auto dxs = member(m++).payload;
        const auto dys = member(m++).payload;
        for (int i = 0; i < k; ++i)
            page.pictures.push_back({member(m++).payload, wordAt(dxs, static_cast<size_t>(i)), wordAt(dys, static_cast<size_t>(i))});
    }
    for (Page& page : t.pages) page.text = text::untilNul(member(m++).payload);
    return t;
}

DialogDesc HelpTopic::pageDesc(const DialogStrings& strings, int page) const {
    const Page& p = pages.at(static_cast<size_t>(page));
    return pagerPageDesc(strings, pageCount(), page, title, p.text, p.pictures);
}

}  // namespace tmport::gg
