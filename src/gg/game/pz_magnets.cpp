#include "gg/game/pz_magnets.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
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

std::pair<int, int> PzMagnets::magnetSize(const Magnet& m) const {
    const auto p = memberOf(magnets_, m.kind * 4 + m.orientation);
    return {s16(p, 6), s16(p, 8)};
}

// 35bb:1c03 PzMagnets_Run, up to PzMagnets_Loop
void PzMagnets::setup(Video& video, Fonts& fonts, int variant) {
    if (variant < 0 || static_cast<size_t>(variant) >= dg::kMagnetsLevelOfVariantCount)
        throw std::out_of_range(std::format("magnets variant {}", variant));
    const int levelPlus1 = static_cast<int8_t>(exe_.ds8(static_cast<uint16_t>(dg::kMagnetsLevelOfVariant + variant)));
    level_ = levelPlus1 - 1;
    // TODO(wave 3): PzSnd_LoadBank(0x11d0) is the audio area's.
    const auto table = archive_.read(kTargets);  // Res_LoadCopy
    if (level_ < 0 || level_ >= 30 || table.size() < 0x3C + 30 * 40 * 8)
        throw FormatError(std::format("magnets: level {} of variant {} outside D {:04x}", level_, variant, kTargets));
    const int n = s16(table, 2 * static_cast<size_t>(level_));
    targets_.clear();
    for (int t = 0; t < n && t < 40; ++t) {
        const size_t o = 0x3C + (static_cast<size_t>(level_) * 40 + static_cast<size_t>(t)) * 8;
        targets_.push_back({s16(table, o), s16(table, o + 2), s16(table, o + 4), s16(table, o + 6)});
    }
    // 35bb:1a47 PzMagnets_Init
    held = -1;
    for (int k = 0; k < 3; ++k) counts_[static_cast<size_t>(k)] = k + 9;
    heldOrientation = 0;
    for (Magnet& m : slots_) m = Magnet{};
    panelText_ = puzzleText(archive_, kPanelText);
    // 35bb:1a8f PzMagnets_LoadResources
    rotate_ = Bundle::load(archive_, kRotatePics);
    digits_ = Bundle::load(archive_, kDigits);
    magnets_ = Bundle::load(archive_, kMagnetPics);
    shadows_ = Bundle::load(archive_, kShadowPics);
    outlines_ = Bundle::load(archive_, kOutlinePics);
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    video.cursor().setCursorPicture(cursor_.get());
    // TODO(wave 3): the zones DS:298a, 29a0, 29b6, 29cc (the runtime's), Hint_Show (the UI's).
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
        counts_[static_cast<size_t>(k)] =
            static_cast<int8_t>(exe_.ds8(static_cast<uint16_t>(dg::kMagnetsStock + dg::kMagnetsStockStride * static_cast<size_t>(level_) +
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
void PzMagnets::drawInventory(Video& video) const {
    Screen& screen = video.screen();
    screen.restoreRect(8, 0x2F, 0x3A, 0x4B);
    for (int k = 0; k < 3; ++k) {
        if (counts_[static_cast<size_t>(k)] <= 0) continue;
        const auto pos = static_cast<uint16_t>(dg::kMagnetsInvPos + dg::kMagnetsInvPosStride * static_cast<size_t>(k));
        const auto cnt = static_cast<uint16_t>(dg::kMagnetsInvCountPos + dg::kMagnetsInvCountPosStride * static_cast<size_t>(k));
        screen.lpxDrawKeyed(exe_.dsS16(pos), exe_.dsS16(pos + 2), memberOf(magnets_, k * 4), 0);
        drawNumber(screen, exe_.dsS16(cnt), exe_.dsS16(cnt + 2), counts_[static_cast<size_t>(k)]);
    }
    video.flush();
}

bool PzMagnets::covers(int j, int i) const {
    const Magnet& a = slots_[static_cast<size_t>(j)];
    const Magnet& b = slots_[static_cast<size_t>(i)];
    const auto [aw, ah] = magnetSize(a);
    const auto [bw, bh] = magnetSize(b);
    return spansOverlap(a.x, a.y, aw + 2, ah + 2, b.x, b.y, bw - 2, bh - 1);
}

// 35bb:0456 PzMagnets_DrawTable
void PzMagnets::drawTable(Video& video) {
    Screen& screen = video.screen();
    screen.restoreRect(0x46, 0x17, 0xEF, 0x80);
    for (Magnet& m : slots_) m.drawn = false;
    for (;;) {
        bool pending = false, progress = false;
        for (int i = 0; i < kSlots; ++i) {
            Magnet& m = slots_[static_cast<size_t>(i)];
            if (m.kind == -1 || m.drawn) continue;
            pending = true;
            bool blocked = false;  // 35bb:0401 PzMagnets_CanDraw
            for (int j = 0; j < kSlots && !blocked; ++j) {
                const Magnet& o = slots_[static_cast<size_t>(j)];
                blocked = o.kind != -1 && !o.drawn && j != i && covers(j, i);
            }
            if (blocked) continue;
            const int pic = m.kind * 4 + m.orientation;
            screen.lpxDrawKeyed(m.x + 2, m.y + 2, memberOf(shadows_, pic), 0);
            screen.lpxDrawKeyed(m.x, m.y, memberOf(magnets_, pic), 0);
            m.drawn = true;
            progress = true;
        }
        if (!pending) break;
        if (!progress) {
            log::debug("PzMagnets_DrawTable: magnets cover each other (the original loops for ever)");
            break;
        }
    }
    video.flush();
}

std::string PzMagnets::describe() const {
    return std::format("magnets level {}: {} targets, inventory {}/{}/{}", level_, targets_.size(), counts_[0], counts_[1],
                       counts_[2]);
}

}  // namespace tmport::gg
