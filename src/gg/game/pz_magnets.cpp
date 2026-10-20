#include "gg/game/pz_magnets.h"

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

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

}  // namespace

PzMagnets::PzMagnets(const GgExe& exe, const Archive& archive) : exe_(exe), archive_(archive) {}

std::pair<int, int> PzMagnets::magnetSize(int i) const {
    const MagnetRec& m = st_->slots[static_cast<size_t>(i)];
    const auto p = memberOf(magnets_, m.kind * 4 + m.orientation);
    return {s16(p, 6), s16(p, 8)};
}

std::pair<int, int> PzMagnets::pole(int picture, int p) const {
    const auto o = static_cast<uint16_t>(dg::kMagnetsPoles + dg::kMagnetsPolesStride * static_cast<size_t>(picture) + 2 * static_cast<size_t>(p));
    return {static_cast<int8_t>(exe_.ds8(o)), static_cast<int8_t>(exe_.ds8(static_cast<uint16_t>(o + 1)))};
}

void PzMagnets::loadTargets(int variant) {
    if (variant < 0 || static_cast<size_t>(variant) >= dg::kMagnetsLevelOfVariantCount)
        throw std::out_of_range(std::format("magnets variant {}", variant));
    const int levelPlus1 = static_cast<int8_t>(exe_.ds8(static_cast<uint16_t>(dg::kMagnetsLevelOfVariant + variant)));
    st_->level = static_cast<int16_t>(levelPlus1 - 1);
    const int level = st_->level;
    const auto table = archive_.read(kTargets);  // Res_LoadCopy
    if (level < 0 || level >= 30 || table.size() < 0x3C + 30 * 40 * 8)
        throw FormatError(std::format("magnets: level {} of variant {} outside D {:04x}", level, variant, kTargets));
    const int n = s16(table, 2 * static_cast<size_t>(level));
    targets_.clear();
    for (int t = 0; t < n && t < 40; ++t) {
        const size_t o = 0x3C + (static_cast<size_t>(level) * 40 + static_cast<size_t>(t)) * 8;
        targets_.push_back({s16(table, o), s16(table, o + 2), s16(table, o + 4), s16(table, o + 6)});
    }
}

// 35bb:1a8f PzMagnets_LoadResources
void PzMagnets::loadPictures() {
    rotate_ = Bundle::load(archive_, kRotatePics);
    digits_ = Bundle::load(archive_, kDigits);
    magnets_ = Bundle::load(archive_, kMagnetPics);
    shadows_ = Bundle::load(archive_, kShadowPics);
    outlines_ = Bundle::load(archive_, kOutlinePics);
    hintPressed_ = archive_.read(kHintPressed);
}

void PzMagnets::flush(Video& video) {
    if (game_) game_->display.flush();
    else video.flush();
}

// 35bb:1c03 PzMagnets_Run, up to PzMagnets_Loop (a tool's picture of it)
void PzMagnets::setup(Video& video, Fonts& fonts, int variant) {
    game_ = nullptr;
    loadTargets(variant);
    init();
    panelText_ = puzzleText(archive_, kPanelText);
    loadPictures();
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    video.cursor().setCursorPicture(cursor_.get());
    drawPuzzlePicture(video, archive_, puzzleKindInfo(7).picture, puzzleKindInfo(7).palette);
    PuzzlePanel panel;
    panel.init(video.screen(), fonts, archive_, panelText_);
    drawTargets(video.screen());
    video.screen().saveToBack(0, 0, 320, 200);
    drawInventory(video);
    puzzleFadeIn(video);
}

// 35bb:1b59 PzMagnets_DrawTargets
void PzMagnets::drawTargets(Screen& screen) {
    for (int k = 0; k < 3; ++k)
        st_->counts[static_cast<size_t>(k)] =
            static_cast<int8_t>(exe_.ds8(static_cast<uint16_t>(dg::kMagnetsStock + dg::kMagnetsStockStride * static_cast<size_t>(st_->level) +
                                                             static_cast<size_t>(k))));
    for (const Target& t : targets_) screen.lpxDrawKeyed(t.x, t.y, memberOf(outlines_, t.b * 2 + t.a), 0);
}

// 35bb:0127 PzMagnets_DrawNumber
void PzMagnets::drawNumber(Screen& screen, int x, int y, int n) const {
    if (n / 10 > 0) {
        const auto tens = memberOf(digits_, n / 10);
        screen.lpxDrawKeyed(x, y, tens, 0);
        x += s16(tens, 6) + 1;
    }
    screen.lpxDrawKeyed(x, y, memberOf(digits_, n % 10), 0);
}

// 35bb:01ac PzMagnets_DrawInventory
void PzMagnets::drawInventory(Video& video) {
    Screen& screen = video.screen();
    screen.restoreRect(8, 0x2F, 0x3A, 0x4B);
    for (int k = 0; k < 3; ++k) {
        const int n = st_->counts[static_cast<size_t>(k)];
        if (n <= 0) continue;
        const auto pos = static_cast<uint16_t>(dg::kMagnetsInvPos + dg::kMagnetsInvPosStride * static_cast<size_t>(k));
        const auto cnt = static_cast<uint16_t>(dg::kMagnetsInvCountPos + dg::kMagnetsInvCountPosStride * static_cast<size_t>(k));
        screen.lpxDrawKeyed(exe_.dsS16(pos), exe_.dsS16(pos + 2), memberOf(magnets_, k * 4), 0);
        drawNumber(screen, exe_.dsS16(cnt), exe_.dsS16(cnt + 2), n);
    }
    flush(video);
}

bool PzMagnets::covers(int j, int i) const {
    const MagnetRec& a = st_->slots[static_cast<size_t>(j)];
    const MagnetRec& b = st_->slots[static_cast<size_t>(i)];
    const auto [aw, ah] = magnetSize(j);
    const auto [bw, bh] = magnetSize(i);
    return spansOverlap(a.x, a.y, aw + 2, ah + 2, b.x, b.y, bw - 2, bh - 1);
}

// 35bb:0456 PzMagnets_DrawTable
void PzMagnets::drawTable(Video& video) {
    Screen& screen = video.screen();
    screen.restoreRect(0x46, 0x17, 0xEF, 0x80);
    for (MagnetRec& m : st_->slots) m.drawn = 0;
    while (!allDrawn()) {
        bool progress = false;
        for (int i = 0; i < kSlots; ++i) {
            MagnetRec& m = st_->slots[static_cast<size_t>(i)];
            if (m.kind == -1 || m.drawn != 0 || !canDraw(i)) continue;
            const int pic = m.kind * 4 + m.orientation;
            screen.lpxDrawKeyed(m.x + 2, m.y + 2, memberOf(shadows_, pic), 0);
            screen.lpxDrawKeyed(m.x, m.y, memberOf(magnets_, pic), 0);
            m.drawn = 1;
            progress = true;
        }
        if (!progress) {
            log::debug("PzMagnets_DrawTable: magnets cover each other (the original loops for ever)");
            break;
        }
    }
    flush(video);
}

std::string PzMagnets::describe() const {
    return std::format("magnets level {}: {} targets, inventory {}/{}/{}", st_->level, targets_.size(), st_->counts[0],
                       st_->counts[1], st_->counts[2]);
}

}  // namespace tmport::gg
