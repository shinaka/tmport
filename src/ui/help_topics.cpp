#include "ui/help_topics.h"

#include <format>

#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "mischief/util/error.h"
#include "res/resource_manager.h"
#include "text/text_renderer.h"
#include "ui/game_dialog.h"

namespace tmport::ui {

PagerDesc buildHelpPager(const ExeTables& exe, const ResourceManager& res, int topic) {
    if (topic < 0 || topic >= static_cast<int>(kHelpTopics.size()))
        throw mischief::Error(std::format("help topic {} does not exist (0..{})", topic, kHelpTopics.size() - 1));
    const HelpTopic& t = kHelpTopics[static_cast<size_t>(topic)];
    PagerDesc p = PagerDesc::fromExe(exe, dg::kHelpDesc);
    const auto title = exe.cstr(t.titleOff);
    p.title = std::vector<uint8_t>(title.begin(), title.end());
    p.pageCount = t.pageCount;
    p.texts.clear();
    for (int i = 0; i < t.pageCount; ++i) {
        // 1180:0000 step 3 (the "11605, i == 1 -> 11606" special case gives the same id).
        const int id = t.firstId + i;
        auto body = res.loadRaw(ResType::OTHR, id);
        if (!body) throw mischief::MissingFileError("TMT.DLL", std::format("OTHR {} (help page) not found in TMT.DLL", id));
        const mischief::text::Bytes s = mischief::text::untilNul(*body);
        p.texts.emplace_back(s.begin(), s.end());
    }
    // Nothing in this build stores pictures into the help arrays (DS:2cc6/2ce2/2cf0 stay zero).
    p.pics.assign(static_cast<size_t>(t.pageCount), nullptr);
    p.picDx.assign(static_cast<size_t>(t.pageCount), 0);
    p.picDy.assign(static_cast<size_t>(t.pageCount), 0);
    return p;
}

int showHelpTopic(DialogSystem& dialogs, const ExeTables& exe, const ResourceManager& res, int topic) {
    return dialogs.runPager(buildHelpPager(exe, res, topic));
}

}  // namespace tmport::ui
