#include "gg/game/pz_pieces.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/game/game.h"
#include "gg/gfx/cursor.h"
#include "gg/gfx/video.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

constexpr size_t kRecordSize = 18;

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

}  // namespace

PzPieces::PzPieces(const GgExe& exe, const Archive& archive) : exe_(exe), archive_(archive) {}

std::span<const uint8_t> PzPieces::picture(int i) const {
    return i >= 0 && i < kMaxPieces ? pictures_[static_cast<size_t>(i)] : std::span<const uint8_t>{};
}

// 3a4a:0bcb PzPieces_LoadVariant: Res_LoadTo(0x209c + v, g_pieces)
void PzPieces::loadVariant(int variant) {
    const auto rec = archive_.read(static_cast<uint16_t>(kVariantFirst + variant));
    for (size_t i = 0; i < st_->pieces.size(); ++i) {
        const size_t o = kRecordSize * i;
        PieceRec& p = st_->pieces[i];
        p = PieceRec{};
        if (o + kRecordSize <= rec.size()) p.load(rec.data() + o);
    }
    variant_ = variant;
}

void PzPieces::loadPictures(int variant) {
    bundle_ = Bundle::load(archive_, static_cast<uint16_t>(kBundleFirst + variant));  // Res_LoadCopy
    const int n = static_cast<int>(bundle_.size()) - 1;  // (first offset / 2 - 1) - 1
    if (n < 1 || n > kMaxPieces)
        throw FormatError(std::format("machine pieces: bundle {:04x} has {} pieces", kBundleFirst + variant, n));
    pictures_ = {};
    for (int i = 0; i < n; ++i) pictures_[static_cast<size_t>(i)] = memberOf(bundle_, i);
    machine_ = memberOf(bundle_, n);
    hintPressed_ = archive_.read(kHintPressed);
    box_ = Bundle::load(archive_, kBoxPics);
    digits_ = Bundle::load(archive_, kPuzzleDigits);
    panelText_ = puzzleText(archive_, kPanelText);
    variant_ = variant;
}

// 3a4a:0ca8 PzPieces_LoadResources
void PzPieces::loadResources(int variant) {
    loadPictures(variant);
    PiecesState& s = *st_;
    s.count = static_cast<int16_t>(bundle_.size() - 1);
    for (int i = 0; i < s.count; ++i) {
        PieceRec& p = s.pieces[static_cast<size_t>(i)];
        const auto pic = pictures_[static_cast<size_t>(i)];
        p.w = s16(pic, 6);  // +4 / +6 = the picture's width and height (LPX +6 / +8)
        p.h = s16(pic, 8);
        p.group = static_cast<int16_t>(i);
    }
    s.inBox = s.count;
}

void PzPieces::flush(Video& video) {
    if (game_) game_->display.flush();
    else video.flush();
}

// 3a4a:0000 PzPieces_Run, up to the loop (a tool's picture of it)
void PzPieces::setup(Video& video, Fonts& fonts, int variant) {
    game_ = nullptr;
    st_->held = kNoPiece;
    loadVariant(variant);
    loadResources(variant);
    // PzPieces_DrawScreen's drawing (drawScreen on the game); the copy of g_cursor is the game's.
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    blank_ = loadCursor(archive_, 4);
    drawPuzzlePicture(video, archive_, puzzleKindInfo(2).picture, puzzleKindInfo(2).palette);
    PuzzlePanel panel;
    panel.init(video.screen(), fonts, archive_, panelText_);
    video.screen().saveToBack(0, 0, 320, 200);
    video.screen().lpxDrawKeyed(37, 130, memberOf(digits_, st_->inBox), 0);  // not in the back page
    video.cursor().setCursorPicture(cursor_.get());
    puzzleFadeIn(video);
}

// 3a4a:053f PzPieces_RedrawWork
void PzPieces::redrawWork(Video& video) {
    Screen& screen = video.screen();
    screen.restoreRect(kWorkX, kWorkY, kWorkW, kWorkH);
    for (int i = 0; i < st_->count; ++i) {
        const PieceRec& p = st_->pieces[static_cast<size_t>(i)];
        if (p.placed != 1) continue;
        const auto pic = pictures_[static_cast<size_t>(i)];
        // The size of the test is the picture's (LPX +6 / +8), not the record's.
        if (p.x < kWorkX || p.x + s16(pic, 6) > kWorkRight || p.y < kWorkY || p.y + s16(pic, 8) > kWorkBottom) continue;
        screen.lpxDrawKeyed(p.x, p.y, pic, 0);
    }
    flush(video);
}

std::string PzPieces::describe() const {
    return std::format("machine pieces variant {}: {} pieces, {} in the box", variant_, st_->count, st_->inBox);
}

}  // namespace tmport::gg
