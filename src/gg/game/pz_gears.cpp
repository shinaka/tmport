#include "gg/game/pz_gears.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/gfx/cursor.h"
#include "gg/gfx/video.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

constexpr size_t kGridSize = 0x114;
constexpr size_t kObjectSize = 20;

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

}  // namespace

PzGears::PzGears(const GgExe& exe, const Archive& archive) : exe_(exe), archive_(archive) {}

int PzGears::tableS16(uint16_t base, int i) const { return exe_.dsS16(static_cast<uint16_t>(base + 2 * i)); }

// 3b2f:1b83 PzGears_LoadVariant
void PzGears::loadVariant(int variant) {
    const auto data = archive_.read(static_cast<uint16_t>(kVariantFirst + variant));  // Res_LoadCopy
    if (data.size() < kGridSize + 2) throw FormatError(std::format("gears: F {:04x} is {} bytes", kVariantFirst + variant, data.size()));
    grid_.assign(data.begin(), data.begin() + static_cast<ptrdiff_t>(kGridSize));
    const int n = s16(data, kGridSize);
    if (n < 0 || kGridSize + 2 + kObjectSize * static_cast<size_t>(n) > data.size())
        throw FormatError(std::format("gears: {} objects do not fit F {:04x}", n, kVariantFirst + variant));
    objects_.clear();
    for (int i = 0; i < n; ++i) {
        const size_t o = kGridSize + 2 + kObjectSize * static_cast<size_t>(i);
        objects_.push_back({s16(data, o), s16(data, o + 2), s16(data, o + 4), s16(data, o + 6), s16(data, o + 8),
                            s16(data, o + 0x0A), s16(data, o + 0x0C), s16(data, o + 0x0E), s16(data, o + 0x10),
                            s16(data, o + 0x12)});
    }
    variant_ = variant;
    // TODO(wave 3): PzGears_InitCrankRects / PzGears_SetCrankZone are the crank handle's zone.
}

// 3b2f:1bd5 PzGears_LoadResources
void PzGears::loadResources(int variant) {
    (void)variant;  // the hint text 0x202b + v is the hint viewer's
    digits_ = Bundle::load(archive_, kDigits);
    gears_[0] = Bundle::load(archive_, kSmallGear);
    gears_[1] = Bundle::load(archive_, kLargeGear);
    buttons_ = Bundle::load(archive_, kButtons);
    grip_ = Bundle::load(archive_, kCrankGrip);
    crank_ = Bundle::load(archive_, kCrank);
    gripping = false;
    racks_ = Bundle::load(archive_, kRacks);
    blank_ = loadCursor(archive_, 4);
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    panelText_ = puzzleText(archive_, kPanelText);
}

// 3b2f:1ed0 PzGears_Run, up to the loop
void PzGears::setup(Video& video, Fonts& fonts, int variant) {
    loadVariant(variant);
    loadResources(variant);
    // TODO(wave 3): PzSnd_LoadBank(0x11c8) is the audio area's.
    video.cursor().setCursorPicture(cursor_.get());
    PuzzlePanel panel;
    panel.init(video.screen(), fonts, archive_, panelText_);
    // 3b2f:071c PzGears_DrawScreen
    drawPuzzlePicture(video, archive_, puzzleKindInfo(3).picture, puzzleKindInfo(3).palette);
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c) drawPeg(video.screen(), c, r, peg(c, r) ? 7 : 1);
    video.screen().saveToBack(0, 0, 320, 200);
    draw(video, true);
    countStock();
    drawInventory(video);
    puzzleFadeIn(video);
    // TODO(wave 3): PzGears_AddZones (the runtime's zones), Hint_Show (UI).
    saveHome();
}

// 3b2f:01e4 PzGears_DrawPeg
void PzGears::drawPeg(Screen& screen, int c, int r, uint8_t colour) const {
    screen.setDrawColor(colour);
    screen.hLine(c * 9 + 0x60, r * 9 + 0x27, 2);
    screen.hLine(c * 9 + 0x5F, r * 9 + 0x28, 4);
    screen.hLine(c * 9 + 0x60, r * 9 + 0x29, 2);
}

// 3b2f:024f PzGears_Draw
std::pair<bool, bool> PzGears::draw(Video& video, bool sounds) {
    Screen& screen = video.screen();
    screen.restoreRect(0x54, 1, 0xE2, 0xA0);
    const ClipRect saved = screen.clip();
    screen.setClip({0x56, 0x134, 0x17, 0x98});
    for (int i = objectCount() - 1; i >= 0; --i) {
        Object& o = objects_[static_cast<size_t>(i)];
        o.dir = 0;
        o.visited = 0;
        switch (o.type) {
        case 0:
        case 1:
            if (o.col == kNotPlaced) break;
            screen.lpxDrawKeyed(tableS16(dg::kGearsPicX, o.type) + o.col * 9, tableS16(dg::kGearsPicY, o.type) + o.row * 9,
                                memberOf(gears_[o.type], o.state), 0);
            break;
        case 2:  // horizontal plain
            for (int k = 0; k < o.length; ++k) screen.lpxDrawKeyed(k * 6 + o.col * 3 + 0x60, o.row * 9 + 0x2B, memberOf(racks_, 1), 0);
            break;
        case 3:  // vertical plain
            for (int k = 0; k < o.length; ++k) screen.lpxDrawKeyed(o.col * 9 + 0x63, k * 6 + o.row * 3 + 0x27, memberOf(racks_, 3), 0);
            break;
        case 4:  // horizontal toothed
            for (int k = 0; k < o.length; ++k) screen.lpxDrawKeyed(k * 6 + o.col * 3 + 0x60, o.row * 9 + 0x28, memberOf(racks_, 0), 0);
            break;
        case 5:  // vertical toothed
            for (int k = 0; k < o.length; ++k) screen.lpxDrawKeyed(o.col * 9 + 0x61, k * 6 + o.row * 3 + 0x27, memberOf(racks_, 2), 0);
            break;
        case 6:  // the crank
            screen.lpxDrawKeyed(o.col * 9 + 0x43, o.row * 9 + 0x0A, memberOf(gripping ? grip_ : crank_, o.state), 0);
            break;
        default: break;
        }
    }
    bool allDown = true, newDown = false;
    for (int i = 1; i < objectCount(); ++i) {
        const Object& o = objects_[static_cast<size_t>(i)];
        if (o.type < 7 || o.type > 10) continue;
        if (o.state > o.savedState) newDown = true;
        else if (o.state == 0) allDown = false;
        int x = 0, y = 0, member = o.state;
        switch (o.type) {
        case 7: x = o.col * 3 + 0x60; y = o.row * 9 + 0x22; break;
        case 8: x = o.col * 3 + 0x5E; y = o.row * 9 + 0x22; member += 2; break;
        case 9: x = o.col * 9 + 0x5B; y = o.row * 3 + 0x27; member += 6; break;
        default: x = o.col * 9 + 0x5B; y = o.row * 3 + 0x29; member += 4; break;
        }
        screen.lpxDrawKeyed(x, y, memberOf(buttons_, member), 0);
    }
    // TODO(wave 3): with `sounds`, effect 4 (all down) or 3 (a new one), prio 2 (the audio area).
    (void)sounds;
    screen.setClip(saved);
    video.flush();
    return {allDown, newDown};
}

// 3b2f:0033 PzGears_CountStock
void PzGears::countStock() {
    stock = {0, 0};
    for (const Object& o : objects_)
        if (o.type < 2 && o.type >= 0 && o.col == kNotPlaced) ++stock[static_cast<size_t>(o.type)];
}

// 3b2f:00a9 PzGears_DrawInventory
void PzGears::drawInventory(Video& video) const {
    Screen& screen = video.screen();
    for (int t = 1; t >= 0; --t) {
        const int n = stock[static_cast<size_t>(t)];
        screen.lpxDrawKeyed(tableS16(dg::kGearsTensX, t), tableS16(dg::kGearsTensY, t), memberOf(digits_, n / 10), 0);
        screen.lpxDrawKeyed(tableS16(dg::kGearsUnitsX, t), tableS16(dg::kGearsUnitsY, t), memberOf(digits_, n % 10), 0);
        const auto pic = memberOf(gears_[t], 0);
        if (n == 0) screen.restoreRect(tableS16(dg::kGearsInvX, t), tableS16(dg::kGearsInvY, t), s16(pic, 6), s16(pic, 8));
        else screen.lpxDrawKeyed(tableS16(dg::kGearsInvX, t), tableS16(dg::kGearsInvY, t), pic, 0);
    }
    video.flush();
}

// 3b2f:1da8 PzGears_SaveHome
void PzGears::saveHome() {
    for (Object& o : objects_) {
        if (o.type <= 1) o.length = o.state;           // +8 = frame
        else if (o.type == 2 || o.type == 4) o.state = o.col;  // horizontal racks: +2 = column
        else if (o.type == 3 || o.type == 5) o.state = o.row;  // vertical racks: +2 = row
    }
}

std::string PzGears::describe() const {
    return std::format("gears variant {}: {} objects, {} large and {} small gears in the inventory", variant_,
                       objects_.size(), stock[1], stock[0]);
}

}  // namespace tmport::gg
