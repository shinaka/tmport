#pragma once
// Help topics (ovl33 411d; re/gg/docs/code_ui.md §6.6): the bundles 0x105D..0x1062 behind the Help
// menu's six items and the pager pages they make (Dlg_RunPager 3f56:0907, gg/ui/dialog.h).
//
//   member 0: I  page count n (<= 10)
//   member 1: S  title
//   per page p in order: I k_p picture count (<= 4)
//                        if k_p > 0: I dx array, I dy array, k_p x G pictures
//   then n x H page texts (FF 03 colour escapes)
// Shipped: 0x105D 4 pages (pictures on pages 1 and 3), 0x105E 2 (one picture), 0x105F, 0x1060,
// 0x1061 2, 0x1062 1.

#include <cstdint>
#include <optional>
#include <vector>

#include "gg/data/resources.h"
#include "gg/ui/dialog.h"
#include "gg/ui/menu_commands.h"
#include "text/text_engine.h"

namespace tmport::gg {

class GgGame;

class Archive;

inline constexpr uint16_t kHelpAboutGame = 0x105D;
inline constexpr uint16_t kHelpBuildingVehicles = 0x105E;
inline constexpr uint16_t kHelpFindingParts = 0x105F;
inline constexpr uint16_t kHelpMovingAround = 0x1060;
inline constexpr uint16_t kHelpChallengeRaces = 0x1061;
inline constexpr uint16_t kHelpSolvingPuzzles = 0x1062;

// The topic a Help handler shows (411d:01e0 .. 411d:021c); nullopt for the other commands.
std::optional<uint16_t> helpTopicOf(MenuCommand c);

// 411d:0000 Help_ShowTopic's view of a topic bundle (a private copy, as Res_LoadCopy).
struct HelpTopic {
    static constexpr int kMaxPages = 10;   // the stack arrays of Help_ShowTopic
    static constexpr int kMaxPictures = 4;

    struct Page {
        text::Bytes text;
        std::vector<DlgPicture> pictures;
    };

    Bundle bundle;
    text::Bytes title;        // into `bundle` (move-only for that reason)
    std::vector<Page> pages;

    HelpTopic() = default;
    HelpTopic(HelpTopic&&) noexcept = default;
    HelpTopic& operator=(HelpTopic&&) noexcept = default;
    HelpTopic(const HelpTopic&) = delete;
    HelpTopic& operator=(const HelpTopic&) = delete;

    // Throws tmport::FormatError for a bundle that does not have this shape (or more than 10 pages /
    // 4 pictures a page: the original's arrays would overflow).
    static HelpTopic load(const Archive& archive, uint16_t id);

    int pageCount() const { return static_cast<int>(pages.size()); }
    // The descriptor Dlg_RunPager shows for page p.
    DialogDesc pageDesc(const DialogStrings& strings, int page) const;
};


// ---- wave 3: the run (owner: ui agent; ui/commands.cpp) ---------------------------------------------
// 411d:0000 Help_ShowTopic(id): the topic's pages through Dlg_RunPager; the pager's result (1 Done
// reopens the Help menu, 0 Esc closes it). The six handlers 411d:01e0 Help_AboutGame, 01ec
// Help_BuildingVehicles, 01f8 Help_FindingParts, 0204 Help_MovingAround, 0210 Help_ChallengeRaces,
// 021c Help_SolvingPuzzles are runMenuCommand's (helpTopicOf).
int helpShowTopic(GgGame& g, uint16_t id);

}  // namespace tmport::gg
