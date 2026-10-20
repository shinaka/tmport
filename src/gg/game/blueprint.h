#pragma once
// The blueprint viewer of the workshop (ovl08 2dee:13eb..1a8f; re/gg/docs/code_vehicles_races.md §6),
// the drawing half: the blueprint bundle of the race, the opening frame, the overview page (the
// vehicle's name, the blueprint picture and its hot spots with their label cursors) and the part
// pages (title and text). The input loop, the hint and the swap-file save-under are wave 3's and the
// runtime's; the Done button is the UI library's picture button (drawn through a callback).
//
// Bundle DS:22c4[race]: member 0 'I' {x, y} of the picture inside the box (drawn at x + 18, y + 9),
// 1 'I' n pages, 2 'S' the vehicle name, 3.. n 'T' page texts, n 'G' labels (also hover cursors), n
// 'S' page titles, 'I' z hot spots, z 'M' {shape, x, y, w, h, page} (a zone at x + 18, y + 9, release
// 2000 + page). The picture is DS:22a6[race].

#include <functional>
#include <memory>
#include <vector>

#include "gg/data/resources.h"
#include "gg/gfx/cursor.h"

namespace tmport::gg {

class Archive;
class Fonts;
class GgExe;
class Screen;
class Video;

class BlueprintView {
public:
    static constexpr int kFrames = 6;
    static constexpr uint16_t kFrameFirst = 0x13;  // 0x13..0x18: 59 x 37 growing to 280 x 157
    static constexpr int kPageCodeBase = 2000;

    // A hot spot of the overview (a zone and a hover region with the page's label cursor).
    struct HotSpot {
        int shape = 0, x = 0, y = 0, w = 0, h = 0, page = 0;
    };

    BlueprintView(const GgExe& exe, const Archive& archive);

    // 2dee:13eb Blueprint_Load(DS:22c4[race]).
    void load(int raceIndex);
    // 2dee:17eb Blueprint_Open, the drawing: Blueprint_DrawFrame (2dee:005e: the six frame pictures
    // opaque at DS:217c, each flushed), font 0, colour 15, the title's 7th character '1' + level,
    // Blueprint_Load. The Done button (PicBtn_Load DS:2280 at (248, 142)) is the caller's.
    void open(Video& video, Fonts& fonts, int raceIndex, int level);
    // 2dee:1894 Blueprint_Run's redraw for page 1 (the overview) or page 2 (a part page): Blueprint_ClearBox
    // (2dee:0000: fills colour 0xda (25, 26, 267, 135), (78, 15, 214, 10), (26, 15, 50, 10), then the Done
    // button through `drawDone`), colour 15, the title at (26, 15); page 1: the name centred in x 78..292
    // at y 15, the picture keyed, the hover regions; page 2: the part's title centred, its text in a box
    // (27, 28) 257 wide; flush.
    void draw(Video& video, Fonts& fonts, int page, int part, const std::function<void(Screen&)>& drawDone);

    // The box the opening leaves: the last frame picture (0x18) at DS:217c[5].
    struct Box {
        int x = 0, y = 0, w = 0, h = 0;
    };
    Box box() const;

    int pageCount() const { return static_cast<int>(texts_.size()); }
    const std::vector<HotSpot>& hotSpots() const { return spots_; }
    std::span<const uint8_t> name() const { return name_; }

private:
    const GgExe& exe_;
    const Archive& archive_;
    int raceIndex_ = 0;
    std::vector<uint8_t> title_;  // the 'Level-N' string of DS:22a0 (a copy: the original patches DGROUP)
    Bundle bundle_;
    int picX_ = 0, picY_ = 0;
    std::span<const uint8_t> name_;
    std::vector<std::span<const uint8_t>> texts_, labels_, titles_;
    std::vector<HotSpot> spots_;
    std::vector<std::unique_ptr<CursorRecord>> cursors_;
    std::vector<CursorRegion> regions_;
};

}  // namespace tmport::gg
