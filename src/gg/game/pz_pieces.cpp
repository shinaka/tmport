#include "gg/game/pz_pieces.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/gfx/cursor.h"
#include "gg/gfx/video.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

constexpr size_t kRecordSize = 18;
constexpr int kWorkX = 93, kWorkY = 14, kWorkW = 219, kWorkH = 142;  // 0x5d, 0x0e .. 0x138, 0x9c

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

}  // namespace

// The puzzle reads no table from ssg.exe; the parameter keeps the constructor in line with the others.
PzPieces::PzPieces(const GgExe& /*exe*/, const Archive& archive) : archive_(archive) {}

// 3a4a:0bcb PzPieces_LoadVariant: Res_LoadTo(0x209c + v, g_pieces)
void PzPieces::loadVariant(int variant) {
    const auto rec = archive_.read(static_cast<uint16_t>(kVariantFirst + variant));
    for (size_t i = 0; i < pieces_.size(); ++i) {
        const size_t o = kRecordSize * i;
        Piece& p = pieces_[i];
        p = Piece{};
        if (o + kRecordSize > rec.size()) continue;
        p.x = s16(rec, o);
        p.y = s16(rec, o + 2);
        p.w = s16(rec, o + 4);
        p.h = s16(rec, o + 6);
        p.dx = rec[o + 0x0C];
        p.dy = rec[o + 0x0D];
        p.group = s16(rec, o + 0x0E);
        p.placed = s16(rec, o + 0x10) == 1;
    }
    variant_ = variant;
}

// 3a4a:0ca8 PzPieces_LoadResources
void PzPieces::loadResources(int variant) {
    bundle_ = Bundle::load(archive_, static_cast<uint16_t>(kBundleFirst + variant));  // Res_LoadCopy
    count_ = static_cast<int>(bundle_.size()) - 1;  // (first offset / 2) - 2
    if (count_ < 1 || count_ > kMaxPieces)
        throw FormatError(std::format("machine pieces: bundle {:04x} has {} pieces", kBundleFirst + variant, count_));
    for (int i = 0; i < count_; ++i) {
        Piece& p = pieces_[static_cast<size_t>(i)];
        p.picture = memberOf(bundle_, i);
        p.w = s16(p.picture, 6);
        p.h = s16(p.picture, 8);
        p.group = i;
    }
    inBox = count_;
    machine_ = memberOf(bundle_, count_);
    box_ = Bundle::load(archive_, kBoxPics);
    digits_ = Bundle::load(archive_, kPuzzleDigits);
    panelText_ = puzzleText(archive_, kPanelText);
}

// 3a4a:0000 PzPieces_Run, up to the loop
void PzPieces::setup(Video& video, Fonts& fonts, int variant) {
    // TODO(wave 3): PzSnd_LoadBank(0x11ca) is the audio area's.
    held = kNoPiece;
    loadVariant(variant);
    loadResources(variant);
    // TODO(wave 3): PzPieces_InitZones / AddStaticZones (the runtime's zones), Hint_Show (UI).
    // 3a4a:0be5 PzPieces_DrawScreen (the copy of g_cursor it keeps is the runtime's cursor state)
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    blank_ = loadCursor(archive_, 4);
    drawPuzzlePicture(video, archive_, puzzleKindInfo(2).picture, puzzleKindInfo(2).palette);
    PuzzlePanel panel;
    panel.init(video.screen(), fonts, archive_, panelText_);
    video.screen().saveToBack(0, 0, 320, 200);
    video.screen().lpxDrawKeyed(37, 130, memberOf(digits_, inBox), 0);  // not in the back page
    video.cursor().setCursorPicture(cursor_.get());
    puzzleFadeIn(video);
}

// 3a4a:053f PzPieces_RedrawWork
void PzPieces::redrawWork(Video& video) const {
    Screen& screen = video.screen();
    screen.restoreRect(kWorkX, kWorkY, kWorkW, kWorkH);
    for (int i = 0; i < count_; ++i) {
        const Piece& p = pieces_[static_cast<size_t>(i)];
        if (!p.placed) continue;
        if (p.x < kWorkX || p.y < kWorkY || p.x + p.w > kWorkX + kWorkW || p.y + p.h > kWorkY + kWorkH) continue;
        screen.lpxDrawKeyed(p.x, p.y, p.picture, 0);
    }
    video.flush();
}

std::string PzPieces::describe() const {
    return std::format("machine pieces variant {}: {} pieces, {} in the box", variant_, count_, inBox);
}

}  // namespace tmport::gg
