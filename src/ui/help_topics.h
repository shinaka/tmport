#pragma once
// Help-menu topics (segment 1180, "help.cpp"; re/docs/code_1168-1198.md §5): each topic is a run
// of OTHR text pages shown with the multi-page pager (Dlg_RunPager 10c0:0cde) through the static
// PagerDesc at DS:2cfe (x 8, y 22, width 480, 11 lines, footer 0, no pictures).

#include <array>
#include <cstdint>

#include "ui/dialog_desc.h"

namespace tmport {
class ExeTables;
class ResourceManager;
}  // namespace tmport

namespace tmport::ui {

class DialogSystem;

struct HelpTopic {
    int firstId;        // first OTHR text page
    int pageCount;
    uint16_t titleOff;  // DGROUP offset of the title string
};

// The four prologue-less entry points 1180:0117 / 0128 / 0139 / 014a push these constants
// (Help menu items 1..4; item 5, "About", is DlgDesc 1919 via 1140:0000).
inline constexpr std::array<HelpTopic, 4> kHelpTopics = {{
    {11600, 4, 0x2D1C},  // Help_AboutTheGame
    {11605, 2, 0x2D2B},  // Help_UsingTheKeyboard
    {11607, 1, 0x2D3E},  // Help_UsingTheMouse
    {11608, 4, 0x2D4E},  // Help_EducationalFocus
}};

// Steps 1-4 of 1180:0000 Help_ShowTopic: the PagerDesc of DS:2cfe with the topic's title and its
// pages loaded from OTHR firstId + i (text up to the first NUL). topic: 0..3. Throws on a
// missing resource.
PagerDesc buildHelpPager(const ExeTables& exe, const ResourceManager& res, int topic);
// 1180:0000 Help_ShowTopic: runs the pager; 1 = closed with "Done" on the last page, 0 = cancelled.
int showHelpTopic(DialogSystem& dialogs, const ExeTables& exe, const ResourceManager& res, int topic);

}  // namespace tmport::ui
