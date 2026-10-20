#include "gg/ui/attract.h"

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/text/text.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr int kMaxScriptWords = 64;  // a guard for the load walk (the shipped scripts have <= 8 words)

int16_t s16At(std::span<const uint8_t> b, size_t off) {
    return off + 2 <= b.size() ? static_cast<int16_t>(b[off] | (b[off + 1] << 8)) : int16_t{-1};
}

}  // namespace

AttractAnim AttractAnim::fromExe(const GgExe& exe, uint16_t dsOff) {
    AttractAnim a;
    const int n = exe.dsS16(dsOff);
    const GgExe::Address table = exe.dsFarPtr(static_cast<uint16_t>(dsOff + 2));
    if (n <= 0 || table.isNull()) return a;
    const uint16_t t = exe.dsOffsetOf(table);
    for (int c = 0; c < n; ++c) a.channels.push_back(exe.dsOffsetOf(exe.dsFarPtr(static_cast<uint16_t>(t + 4 * c))));
    return a;
}

// 2c11:0024..00c6: the load walk of every script
std::vector<uint16_t> AttractAnim::loadOrder(const GgExe& exe) const {
    std::vector<uint16_t> ids;
    for (const uint16_t script : channels) {
        for (int k = 0; k < kMaxScriptWords; ++k) {
            const int16_t w = exe.dsS16(static_cast<uint16_t>(script + 2 * k));
            if (w == -1) break;
            if (w > 0) ids.push_back(static_cast<uint16_t>(w));
            else if (w == -2) ++k;  // its operand
        }
    }
    return ids;
}

AttractAnims AttractAnims::fromExe(const GgExe& exe) {
    AttractAnims a;
    a.title = AttractAnim::fromExe(exe, dg::kAttractAnims);
    a.afterTitle = AttractAnim::fromExe(exe, static_cast<uint16_t>(dg::kAttractAnims + 6));
    a.afterStory = AttractAnim::fromExe(exe, static_cast<uint16_t>(dg::kAttractAnims + 12));
    a.beforePrompt = AttractAnim::fromExe(exe, static_cast<uint16_t>(dg::kAttractAnims + 18));
    return a;
}

// 2c11:0000 Attract_PlayAnim: the loads and Sprites_Reset
AttractAnimPlayer::AttractAnimPlayer(const GgExe& exe, const AttractAnim& anim, ScreenAnimHost& host)
    : dgroup_(exe.dgroup()), host_(host) {
    for (const uint16_t s : anim.channels) channels_.push_back(Channel{s});
    for (const uint16_t id : anim.loadOrder(exe)) {
        ids_.push_back(id);
        handles_.push_back(host_.load(id));  // 174b:0324 ScreenAnim_Load
    }
    host_.resetSprites();  // 1581:0484 Sprites_Reset
}

// 2c11:037c..039e: ScreenAnim_Free of every loaded instance, the last first
AttractAnimPlayer::~AttractAnimPlayer() {
    for (auto it = handles_.rbegin(); it != handles_.rend(); ++it) host_.release(*it);
}

int16_t AttractAnimPlayer::word(const Channel& ch, int k) const {
    return s16At(dgroup_, static_cast<size_t>(ch.script) + 2 * static_cast<size_t>(k));
}

int16_t AttractAnimPlayer::op(int c) const {
    const Channel& ch = channels_[static_cast<size_t>(c)];
    return word(ch, ch.pos);
}

bool AttractAnimPlayer::channelAtEnd(int c) const { return op(c) == -1; }

// 2c11:011b..0206
void AttractAnimPlayer::startChannels() {
    for (size_t c = 0; c < channels_.size(); ++c) {
        Channel& ch = channels_[c];
        if (!ch.ready) continue;
        ch.ready = false;
        const int16_t o = word(ch, ch.pos);
        if (o > 0) {
            // the first loaded instance with this id that is not running (the original's search has no
            // bound; the shipped scripts always find one)
            size_t k = 0;
            while (k < ids_.size() && (ids_[k] != o || host_.inList(handles_[k]))) ++k;
            if (k == ids_.size()) {
                log::warn("Attract_PlayAnim: no free instance of animation {:04x} (channel {})", o, c);
                continue;
            }
            ch.state = static_cast<int>(k);
            host_.startAt(handles_[k], static_cast<int>(c));  // 174b:02ab ScreenAnim_StartAt
        } else if (o == -2) {
            ch.state = -2;
            ch.until = frames_ + word(ch, ch.pos + 1);
        }
    }
}

// 2c11:021c (the frame count, after the flush) and 2c11:0247..02ef
void AttractAnimPlayer::advance() {
    ++frames_;
    for (Channel& ch : channels_) {
        if (ch.state >= 0) {
            if (host_.stopped(handles_[static_cast<size_t>(ch.state)])) {
                ids_[static_cast<size_t>(ch.state)] = -1;
                ++ch.pos;
                ch.ready = true;
            }
        } else if (ch.state == -2) {
            if (static_cast<uint16_t>(ch.until) < static_cast<uint16_t>(frames_)) {  // unsigned compare
                ch.ready = true;
                ch.pos += 2;
            }
        }
    }
}

void AttractAnimPlayer::runFrame() {
    startChannels();
    host_.frame();  // 174b:0510 ScreenAnim_Frame
    advance();
}

// 2c11:03a7 Attract_DrawPictureSet
void attractDrawPictureSet(Screen& screen, const Archive& archive, uint16_t id) {
    const Bundle set = Bundle::load(archive, id);  // Res_Read
    for (size_t i = 0; i + 1 < set.size(); i += 2) {
        const auto pos = set.member(i).payload;
        if (pos.empty() || pos[0] == 0xFF) break;
        screen.lpxDrawKeyed(s16At(pos, 0), s16At(pos, 2), set.member(i + 1).payload, 0);
    }
}

// 2c11:043f Attract_TitleAnim: the background
PaletteLoad attractTitleBackground(Video& video, const Archive& archive) {
    std::vector<uint8_t> picture;
    PaletteLoad load = video.loadPicture(archive, kTitlePicture, kTitlePalette, picture);  // Screen_LoadPicture
    video.screen().lpxDraw(0, 0, picture);
    video.screen().saveAll();  // Screen_SaveAll
    attractDrawPictureSet(video.screen(), archive, kTitlePictureSet);
    return load;
}

// 2c11:04bc..0570: the title art of phases 2 and 3
void attractDrawTitleArt(Screen& screen, const Bundle& titleArt, int phase) {
    if (phase == 2) {
        screen.lpxDrawKeyed(59, 9, titleArt.member(0).payload, 0);
    } else if (phase == 3) {
        screen.lpxDrawKeyed(52, 51, titleArt.member(1).payload, 0);
        screen.lpxDrawKeyed(99, 138, titleArt.member(2).payload, 0);
        screen.lpxDrawKeyed(0, 181, titleArt.member(3).payload, 0);
    }
}

// 2c11:05cf Attract_AfterTitle
void attractAfterTitle(Video& video, const Archive& archive) {
    video.screen().restoreAll();  // Screen_RestoreAll
    video.palette().save();       // Pal_Save
    video.palette().store(parsePaletteRange(archive.read(kAfterTitlePalette)));  // Pal_LoadRes
}

// 2c11:0630 Attract_StoryPage
PaletteLoad attractStoryPage(Video& video, Fonts& fonts, const Archive& archive, uint16_t textId) {
    Screen& s = video.screen();
    PaletteLoad load;
    if (textId == kStoryFirstText) {
        std::vector<uint8_t> picture;
        load = video.loadPicture(archive, kStoryPicture, kStoryPalette, picture);
        s.lpxDraw(0, 0, picture);
        s.saveAll();
        const auto lineRes = archive.read(kStoryLine);  // Res_Read
        const auto line = text::untilNul(lineRes);
        fonts.setColor(15);
        fonts.select(0);
        fonts.current().setSpacing(1);  // Font_GetCurrent()+6
        fonts.drawString(s, 0xA0 - textWidth(fonts, line) / 2, 0xBE, line);
        fonts.current().setSpacing(2);
    }
    const auto pageRes = archive.read(textId);
    s.setDrawColor(0xA3);
    s.fillRect(0x39, 0x26, 0xB1, 0x35);
    fonts.select(1);
    fonts.setColor(0);
    textDrawBox(s, fonts, 0x39, 0x26, 0xB1, text::untilNul(pageRes));
    return load;
}

// 2c11:077f Attract_AfterStory
void attractAfterStory(Screen& screen) {
    screen.restoreRect(0x39, 0x26, 0xB1, 0x35);
    screen.restoreRect(0, 0xBE, 0x140, 0x0A);
}

// 2c11:07e5 Attract_BeforePrompt
PaletteLoad attractBeforePrompt(Video& video, const Archive& archive) {
    std::vector<uint8_t> picture;
    PaletteLoad load = video.loadPicture(archive, kTitlePicture, kAfterTitlePalette, picture);
    video.screen().lpxDraw(0, 0, picture);
    video.screen().saveAll();
    return load;
}

// 2c11:084c Attract_SignInPrompt
PaletteLoad attractSignInPrompt(Video& video, Fonts& fonts, const Archive& archive) {
    Screen& s = video.screen();
    std::vector<uint8_t> picture;
    PaletteLoad load = video.loadPicture(archive, 0x1029, 0x102A, picture);
    s.lpxDraw(0, 0, picture);
    s.lpxDrawKeyed(0x5D, 0x21, archive.read(kPromptPicture), 0);
    const auto textRes = archive.read(kPromptText);
    fonts.setColor(15);
    fonts.select(0);
    fonts.drawString(s, 0x4A, 0x79, text::untilNul(textRes));
    return load;
}

}  // namespace tmport::gg
