#include "gg/game/blueprint.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/gfx/video.h"
#include "gg/text/font.h"
#include "gg/text/text.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

constexpr uint8_t kBoxColour = 0xDA;
constexpr uint8_t kTextColour = 15;
constexpr int kBoxX = 18, kBoxY = 9;    // the picture and hot spots are relative to the box
constexpr int kNameX = 78, kNameW = 214;

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

// The bytes of an 'S' / 'T' member up to its NUL.
std::span<const uint8_t> textOf(std::span<const uint8_t> p) {
    size_t n = 0;
    while (n < p.size() && p[n] != 0) ++n;
    return p.first(n);
}

// (214 - w) / 2 + 78 with the 16-bit signed division of the original (CWD, SUB AX,DX, SAR).
int centred(int w) {
    const int d = kNameW - w;
    return (d - (d < 0 ? -1 : 0)) / 2 + kNameX;
}

}  // namespace

BlueprintView::BlueprintView(const GgExe& exe, const Archive& archive) : exe_(exe), archive_(archive) {
    const auto title = exe.dsString(exe.dsOffsetOf(exe.dsFarPtr(dg::kBlueprintTitle)));
    title_.assign(title.begin(), title.end());
}

// 2dee:13eb Blueprint_Load
void BlueprintView::load(int raceIndex) {
    raceIndex_ = raceIndex;
    bundle_ = Bundle::load(archive_, exe_.ds16(static_cast<uint16_t>(dg::kBlueprintBundles + 2 * raceIndex)));
    const auto members = bundle_.members();
    if (members.size() < 3) throw FormatError(std::format("blueprint bundle {:04x}: {} members", bundle_.id(), members.size()));
    picX_ = s16(members[0].payload, 0) + kBoxX;
    picY_ = s16(members[0].payload, 2) + kBoxY;
    const int n = s16(members[1].payload, 0);
    name_ = textOf(members[2].payload);
    size_t m = 3;
    auto next = [&]() -> std::span<const uint8_t> {
        if (m >= members.size()) throw FormatError(std::format("blueprint bundle {:04x} ends early", bundle_.id()));
        return members[m++].payload;
    };
    texts_.clear();
    labels_.clear();
    titles_.clear();
    cursors_.clear();
    for (int i = 0; i < n; ++i) texts_.push_back(textOf(next()));
    for (int i = 0; i < n; ++i) {
        labels_.push_back(next());
        cursors_.push_back(CursorRecord::fromPicture(labels_.back()));  // Mouse_LoadCursors
    }
    for (int i = 0; i < n; ++i) titles_.push_back(textOf(next()));
    const int z = s16(next(), 0);
    spots_.clear();
    regions_.clear();
    for (int i = 0; i < z; ++i) {
        const auto r = next();
        HotSpot h{s16(r, 0), s16(r, 2) + kBoxX, s16(r, 4) + kBoxY, s16(r, 6), s16(r, 8), s16(r, 10)};
        spots_.push_back(h);
        // The hover record: the zone (priority 5) and the page's label cursor.
        const auto page = static_cast<size_t>(h.page);
        regions_.push_back({h.shape, h.x, h.y, h.w, h.h, 5, h.page >= 0 && page < cursors_.size() ? cursors_[page].get() : nullptr});
    }
    // The click zones (release 2000 + page) are linked into the runtime's list by Blueprint_Run
    // (game/workshop.cpp, from hotSpots()).
}

// 2dee:17eb Blueprint_Open
void BlueprintView::open(Video& video, Fonts& fonts, int raceIndex, int level) {
    // Screen_SaveToSwap(18, 9, 280, 163) is the runtime's (an in-memory save-under in the engine).
    for (int i = 0; i < kFrames; ++i) {  // 2dee:005e Blueprint_DrawFrame
        const auto o = static_cast<uint16_t>(dg::kBlueprintFramePos + dg::kBlueprintFramePosStride * static_cast<size_t>(i));
        const auto pic = archive_.read(static_cast<uint16_t>(kFrameFirst + i));  // Res_Read
        video.screen().lpxDraw(exe_.dsS16(o), exe_.dsS16(static_cast<uint16_t>(o + 2)), pic);
        video.flush();
    }
    fonts.select(0);
    fonts.setColor(kTextColour);
    if (title_.size() > 6) title_[6] = static_cast<uint8_t>('1' + level);
    load(raceIndex);
}

BlueprintView::Box BlueprintView::box() const {
    const auto o = static_cast<uint16_t>(dg::kBlueprintFramePos + dg::kBlueprintFramePosStride * (kFrames - 1));
    const LpxHeader h = parseLpx(archive_.read(static_cast<uint16_t>(kFrameFirst + kFrames - 1)));
    return {exe_.dsS16(o), exe_.dsS16(static_cast<uint16_t>(o + 2)), h.width, h.height};
}

// 2dee:1894 Blueprint_Run, the redraw
void BlueprintView::draw(Video& video, Fonts& fonts, int page, int part, const std::function<void(Screen&)>& drawDone) {
    Screen& screen = video.screen();
    // 2dee:0000 Blueprint_ClearBox (the Done button's zone is the runtime's)
    screen.setDrawColor(kBoxColour);
    screen.fillRect(0x19, 0x1A, 0x10B, 0x87);
    screen.fillRect(0x4E, 0x0F, 0xD6, 10);
    screen.fillRect(0x1A, 0x0F, 0x32, 10);
    if (drawDone) drawDone(screen);
    fonts.setColor(kTextColour);
    fonts.drawString(screen, 0x1A, 0x0F, title_);
    video.cursor().clearRegions();  // MouseRegion_Clear
    if (page == 1) {
        fonts.drawString(screen, centred(textWidth(fonts, name_)), 0x0F, name_);
        const auto pic = archive_.read(exe_.ds16(static_cast<uint16_t>(dg::kBlueprintPics + 2 * raceIndex_)));  // Res_Read
        screen.lpxDrawKeyed(picX_, picY_, pic, 0);
        video.cursor().addRegions(regions_);  // MouseRegion_AddArray
    } else if (part >= 0 && static_cast<size_t>(part) < titles_.size()) {
        fonts.drawString(screen, centred(textWidth(fonts, titles_[static_cast<size_t>(part)])), 0x0F, titles_[static_cast<size_t>(part)]);
        textDrawBox(screen, fonts, 0x1B, 0x1C, 0x101, texts_[static_cast<size_t>(part)]);
    }
    video.flush();
}

}  // namespace tmport::gg
