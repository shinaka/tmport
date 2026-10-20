#include "gg/game/pz_gears.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/game.h"
#include "gg/gfx/cursor.h"
#include "gg/gfx/video.h"
#include "util/error.h"
#include "util/log.h"

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

PzGears::PzGears(const GgExe& exe, const Archive& archive) : exe_(exe), archive_(archive) {
    // The drive table DS:3902: the near pointers lead into ovl22; each target is one of the nine
    // handlers of code_puzzles_b.md §3.8 (0 = no relation).
    struct Known {
        uint16_t off;
        Drive fn;
    };
    const Known known[] = {{0x0ECC, &PzGears::gearToGear},   {0x0FF9, &PzGears::hRackToHRack}, {0x111D, &PzGears::hRackToButton},
                           {0x11DC, &PzGears::vRackToButton}, {0x129B, &PzGears::vRackToVRack}, {0x13BF, &PzGears::gearToHRack},
                           {0x14AE, &PzGears::hRackToGear},   {0x15C3, &PzGears::gearToVRack},  {0x16B2, &PzGears::vRackToGear}};
    for (size_t t = 0; t < drive_.size(); ++t)
        for (size_t u = 0; u < drive_[t].size(); ++u) {
            const auto off = static_cast<uint16_t>(exe.dsS16(static_cast<uint16_t>(dg::kGearsDriveTable + dg::kGearsDriveTableStride * t + 2 * u)));
            drive_[t][u] = nullptr;
            if (off == 0) continue;
            bool found = false;
            for (const Known& k : known)
                if (k.off == off) {
                    drive_[t][u] = k.fn;
                    found = true;
                }
            if (!found) log::warn("gears: drive table ({}, {}) points at 3b2f:{:04x}, not a handler", t, u, off);
        }
}

int PzGears::tableS16(uint16_t base, int i) const { return exe_.dsS16(static_cast<uint16_t>(base + 2 * i)); }

PzGears::Drive PzGears::driveHandler(int sourceType, int targetType) const {
    if (sourceType < 0 || sourceType >= static_cast<int>(drive_.size()) || targetType < 0 || targetType >= 11) return nullptr;
    return drive_[static_cast<size_t>(sourceType)][static_cast<size_t>(targetType)];
}

int PzGears::minDist2(int t, int u) const {
    return exe_.dsS16(static_cast<uint16_t>(dg::kGearsMinDist2 + dg::kGearsMinDist2Stride * static_cast<size_t>(t) + 2 * static_cast<size_t>(u)));
}

bool PzGears::meshes(int t, int u, int dy, int dx) const {
    // i16[2][2][5][5] indexed [type a][type b][|dy|][|dx|]
    return exe_.dsS16(static_cast<uint16_t>(dg::kGearsMesh + 100 * t + 50 * u + 10 * dy + 2 * dx)) != 0;
}

int PzGears::frames(int t) const { return tableS16(dg::kGearsFrames, t); }

// 3b2f:1b83 PzGears_LoadVariant
void PzGears::loadVariant(int variant) {
    const auto data = archive_.read(static_cast<uint16_t>(kVariantFirst + variant));  // Res_LoadCopy
    if (data.size() < kGridSize + 2) throw FormatError(std::format("gears: F {:04x} is {} bytes", kVariantFirst + variant, data.size()));
    GearsState& s = *st_;
    std::copy(data.begin(), data.begin() + static_cast<ptrdiff_t>(kGridSize), s.grid.begin());
    const int n = s16(data, kGridSize);
    if (n < 1 || kGridSize + 2 + kObjectSize * static_cast<size_t>(n) > data.size())
        throw FormatError(std::format("gears: {} objects do not fit F {:04x}", n, kVariantFirst + variant));
    s.objectCount = static_cast<int16_t>(n);
    s.objects.assign(static_cast<size_t>(n), Object{});
    for (int i = 0; i < n; ++i) s.objects[static_cast<size_t>(i)].load(data.data() + kGridSize + 2 + kObjectSize * static_cast<size_t>(i));
    variant_ = variant;
    initCrankRects();
    if (game_) setCrankZone(*game_);  // 3b2f:1bcf
}

// 3b2f:1bd5 PzGears_LoadResources
void PzGears::loadResources(int variant) {
    digits_ = Bundle::load(archive_, kDigits);
    gears_[0] = Bundle::load(archive_, kSmallGear);
    gears_[1] = Bundle::load(archive_, kLargeGear);
    buttons_ = Bundle::load(archive_, kButtons);
    grip_ = Bundle::load(archive_, kCrankGrip);
    crank_ = Bundle::load(archive_, kCrank);
    st_->turning = false;  // the crank set in use: 0x2026
    racks_ = Bundle::load(archive_, kRacks);
    blank_ = loadCursor(archive_, 4);
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    hintPressed_ = archive_.read(kHintPressed);
    hintText_ = puzzleText(archive_, static_cast<uint16_t>(kHintTextFirst + variant));
    panelText_ = puzzleText(archive_, kPanelText);
}

void PzGears::flush(Video& video) {
    if (game_) game_->display.flush();
    else video.flush();
}

// 3b2f:1ed0 PzGears_Run, up to the loop (a tool's picture of it)
void PzGears::setup(Video& video, Fonts& fonts, int variant) {
    game_ = nullptr;
    loadVariant(variant);
    loadResources(variant);
    video.cursor().setCursorPicture(cursor_.get());
    PuzzlePanel panel;
    panel.init(video.screen(), fonts, archive_, panelText_);
    // PzGears_DrawScreen's drawing (on the game in PzGears_Run)
    drawPuzzlePicture(video, archive_, puzzleKindInfo(3).picture, puzzleKindInfo(3).palette);
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c) drawPeg(video.screen(), c, r, peg(c, r) ? 7 : 1);
    video.screen().saveToBack(0, 0, 320, 200);
    draw(video, true);
    countStock();
    drawInventory(video);
    puzzleFadeIn(video);
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
        Object& o = st_->objects[static_cast<size_t>(i)];
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
        case 6:  // the crank, with the hand on the handle while turning (DS:b6e2)
            screen.lpxDrawKeyed(o.col * 9 + 0x43, o.row * 9 + 0x0A, memberOf(st_->turning ? grip_ : crank_, o.state), 0);
            break;
        default: break;
        }
    }
    bool allDown = true, newDown = false;
    for (int i = 1; i < objectCount(); ++i) {
        const Object& o = st_->objects[static_cast<size_t>(i)];
        if (o.type < 7 || o.type > 10) continue;
        // "Went down" against the state PzGears_TurnCrank saved before the step (+0xe).
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
    if (sounds && game_) {
        // Effect 4 on every redraw while all buttons are down, else 3 when one went down in this step.
        if (allDown) game_->sound.pzPlay(4, 2);
        else if (newDown) game_->sound.pzPlay(3, 2);
    }
    screen.setClip(saved);
    flush(video);
    return {allDown, newDown};
}

// 3b2f:0033 PzGears_CountStock
void PzGears::countStock() {
    st_->stock = {0, 0};
    for (const Object& o : st_->objects)
        if (o.type >= 0 && o.type <= 1 && o.col == kNotPlaced) ++st_->stock[static_cast<size_t>(o.type)];
}

// 3b2f:00a9 PzGears_DrawInventory
void PzGears::drawInventory(Video& video) {
    Screen& screen = video.screen();
    for (int t = 1; t >= 0; --t) {
        const int n = st_->stock[static_cast<size_t>(t)];
        screen.lpxDrawKeyed(tableS16(dg::kGearsTensX, t), tableS16(dg::kGearsTensY, t), memberOf(digits_, n / 10), 0);
        screen.lpxDrawKeyed(tableS16(dg::kGearsUnitsX, t), tableS16(dg::kGearsUnitsY, t), memberOf(digits_, n % 10), 0);
        const auto pic = memberOf(gears_[t], 0);
        if (n == 0) screen.restoreRect(tableS16(dg::kGearsInvX, t), tableS16(dg::kGearsInvY, t), s16(pic, 6), s16(pic, 8));
        else screen.lpxDrawKeyed(tableS16(dg::kGearsInvX, t), tableS16(dg::kGearsInvY, t), pic, 0);
    }
    flush(video);
}

// 3b2f:1da8 PzGears_SaveHome
void PzGears::saveHome() {
    for (Object& o : st_->objects) {
        if (o.type <= 1) o.length = o.state;                    // gears: +8 = frame
        else if (o.type == 2 || o.type == 4) o.state = o.col;   // horizontal racks: +2 = column
        else if (o.type == 3 || o.type == 5) o.state = o.row;   // vertical racks: +2 = row
    }
}

std::string PzGears::describe() const {
    return std::format("gears variant {}: {} objects, {} large and {} small gears in the inventory", variant_,
                       st_->objects.size(), st_->stock[1], st_->stock[0]);
}

}  // namespace tmport::gg
