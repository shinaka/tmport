#include "dos/gfx/dos_screens.h"

#include <algorithm>
#include <format>
#include <stdexcept>

#include "dos/data/dgroup_offsets.h"
#include "dos/gfx/blitter.h"
#include "dos/gfx/scenery.h"
#include "dos/text/text_draw.h"
#include "dos/text/ui_draw.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"

namespace tmport::dos {

namespace {

// DGROUP offsets used by the painters (code_ui.md §8, §9.3, §10.5, §11; code_world.md §3.1).
constexpr uint16_t kDsCellPictures = dg::kCellPicture;  // 16x16 words
constexpr uint16_t kDsHidingCells = dg::kHidingCells;   // 48 words
constexpr uint16_t kDsVariableCells = dg::kVariableCells; // 9 words
constexpr uint16_t kDsCastleLayouts = dg::kCastleLayouts; // 4 x 20 words
constexpr uint16_t kDsPressEnter = dg::kStrTitlePrompt;    // title line
constexpr uint16_t kDsSparkleX = dg::kTitleSparkleX, kDsSparkleY = dg::kTitleSparkleY;  // 12 words each
constexpr uint16_t kDsSignInHeading = dg::kSigninHeadings; // 2 far pointers (remove mode, normal)
constexpr uint16_t kDsRankLabels = dg::kRankNames;    // 8 bytes per rank
constexpr uint16_t kDsRankThresholds = dg::kSigninRankThresholds;// 7 signed words
constexpr uint16_t kDsNewPlayer = dg::kStrNewPlayer;
constexpr uint16_t kDsSignInKeys = dg::kStrSigninKeyLine;
constexpr uint16_t kDsClueWords = dg::kClueWordText;     // 53 far pointers
constexpr uint16_t kDsAnswerOrders = dg::kAnswerPerms;  // 6 x 3 bytes
constexpr uint16_t kDsAnswerShown = dg::kAnswerBit3;   // mask bit per answer (Quiz_DrawAnswers)
constexpr uint16_t kDsAnswerZone = dg::kAnswerBit2;    // mask bit per answer (Quiz_SetupAnswers)

constexpr int kUsersHeader = 3, kUserRecord = 0x168, kUserCount = 9;

std::string_view farString(const DosExe& exe, uint16_t dsOff) {
    const auto b = exe.farPtrAtDs(dsOff);
    const auto nul = std::find(b.begin(), b.end(), uint8_t{0});
    return {reinterpret_cast<const char*>(b.data()), static_cast<size_t>(nul - b.begin())};
}

std::vector<uint8_t> readFile(const std::filesystem::path& p, const std::string& what) {
    auto bytes = mischief::readWholeFile(p);
    if (!bytes) throw mischief::MissingFileError(what, "cannot read " + mischief::pathToUtf8(p));
    return std::move(*bytes);
}

// The USERS.DAT image of the sign-in and of a saved game (empty when there is none).
std::vector<uint8_t> loadUsers(const DosGfxFiles& files, const DosScreenSpec& spec) {
    std::vector<uint8_t> data;
    if (!spec.usersFile.empty()) {
        data = readFile(spec.usersFile, "USERS.DAT");
    } else if (!spec.usersImage.empty()) {
        data = spec.usersImage;
    } else if (files.files().hasUsersDat()) {
        const auto b = files.files().bytes(FileIndex::UsersDat);
        data.assign(b.begin(), b.end());
    }
    if (!data.empty() && data.size() < static_cast<size_t>(kUsersHeader + kUserCount * kUserRecord))
        throw mischief::FormatError((spec.usersFile.empty() ? std::string("USERS.DAT") : mischief::pathToUtf8(spec.usersFile)) +
                          ": not a DOS USERS.DAT (3243 bytes)");
    return data;
}

std::span<const uint8_t> userRecord(const std::vector<uint8_t>& users, int slot) {
    if (users.empty() || slot < 0 || slot >= kUserCount) return {};
    return std::span<const uint8_t>(users).subspan(static_cast<size_t>(kUsersHeader + slot * kUserRecord), kUserRecord);
}

int16_t rd16s(std::span<const uint8_t> b, size_t at) { return static_cast<int16_t>(b[at] | (b[at + 1] << 8)); }

void drawFrame(BackBuffer& buf, const PakFile& pak, int frame, int x, int y, uint8_t flags) {
    if (frame >= 0 && frame < pak.count()) drawSprite(buf, pak.frame(frame), x, y, flags);
}

// Hud_Redraw 130f:07a5 with a full redraw requested (DS:922E = 1, so no actors), then the parts
// below it that a full redraw forces (counters, key and clue box, prompt icon).
void drawHud(BackBuffer& buf, const DosGfxFiles& files, const DosScreenSpec& spec, std::span<const uint16_t> pictures) {
    const DosExe& exe = files.exe();
    const DosFont& font = files.font();
    static constexpr struct { int x1, y1, x2, y2; uint8_t fill; } kFrameFills[] = {  // 130f:07bc..08a8
        {0, 0x00, 0x9F, 0x0A, 0xFF}, {0, 0x0B, 0x9F, 0x0E, 0x00}, {3, 0x0E, 0x9C, 0x0E, 0xFF},
        {2, 0x0F, 0x9D, 0x0F, 0xFF}, {0, 0x0E, 0x00, 0x91, 0x00}, {1, 0x0F, 0x01, 0x90, 0x0F},
        {1, 0x12, 0x01, 0x8D, 0xFF}, {0x9F, 0x0E, 0x9F, 0x91, 0x00}, {0x9E, 0x0F, 0x9E, 0x90, 0xF0},
        {0x9E, 0x12, 0x9E, 0x8D, 0xFF}, {0, 0x91, 0x9F, 0xC7, 0x00}, {3, 0x91, 0x9C, 0x91, 0xFF},
        {2, 0x90, 0x9D, 0x90, 0xFF}};
    for (const auto& f : kFrameFills) buf.fillRect(f.x1, f.y1, f.x2, f.y2, f.fill);
    TextStyle style;
    for (int i = 0; i < 3; ++i) drawMenuTitle(buf, font, style, exe, i, false);
    drawPlayfield(buf, files.pan(spec.pan), pictures, SceneryCamera::at(spec.camX, spec.camY));

    const PakFile& spr = files.pak("SPRITES");
    // Counters (130f:0948..): box, coin bag, net, treasure icon, markers, numbers (unsigned compares).
    const auto coins = static_cast<uint16_t>(spec.coins), nets = static_cast<uint16_t>(spec.nets);
    drawFrame(buf, spr, 60, 0, 0, 0);
    drawFrame(buf, spr, coins > 10 ? 64 : coins > 4 ? 65 : 66, 0, 0, 0);
    drawFrame(buf, spr, nets > 3 ? 67 : nets > 0 ? 68 : 69, 0, 0, 0);
    drawFrame(buf, spr, 70, 0, 0, 0);
    for (int k = 0; k <= spec.tier + 1; ++k)
        drawFrame(buf, spr, (spec.foundMask >> k) & 1 ? 193 + spec.level : 192, k % 2 == 0 ? 0x8D : 0x90, 0xA4 + 6 * k, 0);
    drawNumber(buf, font, 0x6E, 0xBA, 0xCCCC, spec.coins);
    drawNumber(buf, font, 0x7D, 0xBA, 0xCCCC, spec.nets);
    drawNumber(buf, font, 0x8F, 0xBA, 0xCCCC, spec.found);
    // Key and clue words.
    drawFrame(buf, spr, 58, 0, 0, 0);
    if (spec.keyState != 0) drawFrame(buf, spr, 50 + spec.keyState / 2, 6, 0xBE, 0);
    style.ink = 0xCCCC;
    for (int k = 2; k >= 0; --k) {
        const int w = spec.clueWords[static_cast<size_t>(k)];
        if (w >= 0 && w < 0x80) textDraw(buf, font, style, 0x0C, 0xBA - 13 * (2 - k), farString(exe, static_cast<uint16_t>(kDsClueWords + 4 * w)));
    }
    // Hud_DrawHintIcon 130f:0c2e.
    drawFrame(buf, spr, 59, 0, 0, 0);
    if (spec.prompt != 0) drawFrame(buf, spr, 60 + spec.prompt / 2, 0, 0, 0);
}

void drawActors(BackBuffer& buf, const PakFile& pak, const std::vector<ActorSpec>& actors, int from, int to, bool titleMode) {
    SpriteRenderer r;
    for (const ActorSpec& a : actors) {
        if (a.slot < 0 || a.slot >= SpriteRenderer::kRecords) continue;
        DrawRecord& rec = r.records[static_cast<size_t>(a.slot)];
        rec.x = static_cast<int16_t>(a.x);
        rec.y = static_cast<int16_t>(a.y);
        rec.z = static_cast<int16_t>(a.z);
        rec.frame = a.frame;
    }
    r.drawSorted(buf, pak, from, to, titleMode, true);
}

// TitleStep_DrawTitle 4ba4:0045, then one pass of the title loop's sprites (4ba4:0434):
// Sprites_DrawSorted(0, 3) in title mode with the figure (script 0x5B, slot 1 at 0,0), a sparkle
// (script 0x5C, slot 2) and the mouse pointer (script 1, slot 0).
BackBuffer composeTitle(const DosGfxFiles& files, const DosScreenSpec& spec) {
    BackBuffer buf;
    const DosExe& exe = files.exe();
    drawFullScreenPicture(buf, files.pan("OPENING"));
    textDraw(buf, files.font(), TextStyle{0x0000, 10}, 0x11, 0xA0, exe.dsString(kDsPressEnter));
    std::vector<ActorSpec> actors = {{1, 6, 0, 0, 0}};
    if (spec.sparkleSpot >= 0 && spec.sparkleSpot < 12)
        actors.push_back({2, static_cast<uint16_t>(spec.sparkleFrame * 2), exe.dsS16(static_cast<uint16_t>(kDsSparkleX + 2 * spec.sparkleSpot)),
                          exe.dsS16(static_cast<uint16_t>(kDsSparkleY + 2 * spec.sparkleSpot)), 0});
    if (spec.pointer) actors.push_back({0, 4, spec.pointer->first, spec.pointer->second, 0});
    for (const ActorSpec& a : spec.actors) actors.push_back(a);
    drawActors(buf, files.pak("BEGIN"), actors, 0, 3, true);
    return buf;
}

// SignIn_Draw 47c7:03cf: SignIn_DrawPanel 47c7:00e7 (backdrop, panel, the BEGIN 31 heading picture,
// heading text, SignIn_DrawList 47c7:01e0), the key line and the highlight on the new-player row
// (SignIn_MoveHighlight 47c7:071a).
// SignIn_DrawPanel 47c7:00e7 (list mode); returns the number of players listed (DS:C6CC).
int drawSignInPanel(BackBuffer& buf, const DosGfxFiles& files, const DosScreenSpec& spec) {
    const DosExe& exe = files.exe();
    const DosFont& font = files.font();
    drawFullScreenPicture(buf, files.pan("OPENING"));
    buf.fillRect(5, 0x1E, 0x6F, 0xAD, 0xAA);
    drawFrame(buf, files.pak("BEGIN"), 31, 0, 0, 0x40);
    TextStyle style{0x0000, 12};
    textDraw(buf, font, style, 0x18, 0x34, farString(exe, kDsSignInHeading + 4));
    style.ink = 0xFFFF;
    // The roster: records with a name, sorted by total (record +0x0A) with the original's shell sort.
    const auto users = loadUsers(files, spec);
    std::vector<int> totals;
    std::vector<std::string_view> names;
    for (int i = 0; i < kUserCount; ++i) {
        const auto rec = userRecord(users, i);
        if (rec.empty() || rec[0] == 0) continue;
        const auto nameEnd = std::find(rec.begin(), rec.end(), uint8_t{0});
        names.emplace_back(reinterpret_cast<const char*>(rec.data()), static_cast<size_t>(nameEnd - rec.begin()));
        totals.push_back(rd16s(rec, 0x0A));
    }
    const int n = static_cast<int>(names.size());
    for (int gap = n / 2; gap > 0; gap /= 2)
        for (int i = gap; i < n; ++i)
            for (int j = i - gap; j >= 0 && totals[static_cast<size_t>(j)] < totals[static_cast<size_t>(j + gap)]; j -= gap) {
                std::swap(totals[static_cast<size_t>(j)], totals[static_cast<size_t>(j + gap)]);
                std::swap(names[static_cast<size_t>(j)], names[static_cast<size_t>(j + gap)]);
            }
    int y = 0x41;
    for (int k = 0; k < n; ++k, y += 0x0B) {
        int rank = 0;
        while (rank < 7 && exe.dsS16(static_cast<uint16_t>(kDsRankThresholds + 2 * rank)) <= totals[static_cast<size_t>(k)]) ++rank;
        textDraw(buf, font, style, 8, y, names[static_cast<size_t>(k)]);
        textDraw(buf, font, style, 0x3B, y, exe.dsString(static_cast<uint16_t>(kDsRankLabels + 8 * rank)));
        textDraw(buf, font, style, 0x5E, y, std::to_string(totals[static_cast<size_t>(k)]));  // itoa, signed
    }
    textDraw(buf, font, style, 8, y, exe.dsString(kDsNewPlayer));
    return n;
}

BackBuffer composeSignIn(const DosGfxFiles& files, const DosScreenSpec& spec) {
    BackBuffer buf;
    const int n = drawSignInPanel(buf, files, spec);
    textDraw(buf, files.font(), TextStyle{0xFFFF, 12}, 4, 0xBD, files.exe().dsString(kDsSignInKeys));
    const int hy = static_cast<uint8_t>(0x41 + 11 * n);
    exchangeColors(buf, 6, hy, 0x6E, static_cast<uint8_t>(hy + 9), 0xFFFF, 0xAAAA);
    return buf;
}

std::vector<uint16_t> gridFor(const DosGfxFiles& files, const DosScreenSpec& spec) {
    auto pictures = initialCellPictures(files.exe());
    if (spec.restoreGrid) {
        const auto users = loadUsers(files, spec);
        const auto rec = userRecord(users, spec.usersSlot);
        if (!rec.empty() && rec[0] != 0) restoreCellPictures(files.exe(), rec, pictures);
    }
    for (const auto& [cell, picture] : spec.cellPictures)
        if (cell >= 0 && cell < static_cast<int>(pictures.size())) pictures[static_cast<size_t>(cell)] = picture;
    return pictures;
}

// The play screen: Hud_Redraw (full), then the actors (Sprites_DrawSorted(0, 13), play-field clip).
BackBuffer composePlay(const DosGfxFiles& files, const DosScreenSpec& spec) {
    BackBuffer buf;
    const auto pictures = gridFor(files, spec);
    drawHud(buf, files, spec, pictures);
    drawActors(buf, files.pak(spec.pan == "CLOSE" ? "CLOSE" : spec.pan == "SHACK" ? "SHACK" : "SPRITES"), spec.actors, 0, 13, false);
    return buf;
}

// Quiz_DrawScreen 46ce:0271: Hud_Redraw (full, no actors), Quiz_DrawFrame 46ce:028d,
// Quiz_DrawQuestion 46ce:0482, Quiz_DrawAnswers 46ce:049e with Quiz_SetFocus 46ce:052d.
BackBuffer composeQuiz(const DosGfxFiles& files, const DosScreenSpec& spec) {
    BackBuffer buf;
    const DosExe& exe = files.exe();
    const DosFont& font = files.font();
    const auto pictures = gridFor(files, spec);
    drawHud(buf, files, spec, pictures);
    const PakFile& spr = files.pak("SPRITES");
    drawFrame(buf, spr, 57, 0, 0, 0x40);
    drawFrame(buf, spr, 94, 0x8E, 0x8E, 0x60);
    drawFrame(buf, spr, 103, 0x8E, 0x8E, 0x60);
    const Puzzle q = loadPuzzle(files, spec.puzzle);
    textDraw(buf, font, TextStyle{0xFFFF, 12}, 10, 0x1C, q.question);
    int widest = 0;  // DS:A6A8 (Quiz_SplitText)
    for (const auto& a : q.answers) widest = std::max(widest, textWidth(font, a));
    bool focused = false;
    for (int i = 0; i < 3; ++i) {
        const int a = exe.ds8(static_cast<uint16_t>(kDsAnswerOrders + 3 * std::clamp(spec.order, 0, 5) + i));
        const bool shown = (exe.ds8(static_cast<uint16_t>(kDsAnswerShown + a)) & spec.mask) != 0;
        textDraw(buf, font, TextStyle{static_cast<uint16_t>(shown ? 0xFFFF : 0xDDDD), 12}, 10, 0x6C + 12 * i, q.answers[static_cast<size_t>(a)]);
        if (shown && !focused && (exe.ds8(static_cast<uint16_t>(kDsAnswerZone + a)) & spec.mask) != 0) {
            // Zone 9+i (Quiz_SetupAnswers 46ce:03ff): x 10..widest+12, y 0x6A+12i..+12; the row is
            // framed in cyan (erasing the old focus, the same row here) and then in red.
            focused = true;
            const int y1 = 0x6A + 12 * i;
            frameRect(buf, 10 - 1, y1, widest + 12 - 1, y1 + 12, 0x77);
            frameRect(buf, 10 - 1, y1, widest + 12 - 1, y1 + 12, 0x44);
        }
    }
    return buf;
}

void putBackground(BackBuffer& buf, const std::vector<uint8_t>& logical) {
    if (logical.size() < static_cast<size_t>(BackBuffer::kWidth) * BackBuffer::kHeight) return;
    for (int y = 0; y < BackBuffer::kHeight; ++y)
        for (int x = 0; x < BackBuffer::kWidth; ++x) buf.setPixel(x, y, logical[static_cast<size_t>(y) * BackBuffer::kWidth + x]);
}

// Menu_Update opening drop-down `menu` (43cb:0001): MenuBar_DrawTitle(menu, on), MsgBox_Draw(menu),
// Menu_HighlightItem.
BackBuffer composeMenu(const DosGfxFiles& files, const DosScreenSpec& spec) {
    BackBuffer buf;
    putBackground(buf, spec.background);
    const int menu = std::clamp(spec.menu, 0, 2);
    TextStyle style;
    drawMenuTitle(buf, files.font(), style, files.exe(), menu, true);
    const BoxGeometry g = drawMsgBox(buf, files.font(), files.exe(), BoxTexts::fromExe(files.exe()), menu);
    highlightMenuItem(buf, files.exe(), menu, spec.item, g);
    return buf;
}

BackBuffer composeMsgBox(const DosGfxFiles& files, const DosScreenSpec& spec) {
    BackBuffer buf;
    putBackground(buf, spec.background);
    drawMsgBox(buf, files.font(), files.exe(), BoxTexts::fromExe(files.exe()), std::clamp(spec.box, 0, 22), spec.button);
    return buf;
}

// SignIn_DrawNameEntry 47c7:0189 after typing `typedName`: the panel, box 15, the field frame, the
// characters as SignIn_DrawCaret 47c7:05a9 drew them when typed, SignIn_DrawField 47c7:05d4(1).
BackBuffer composeNameEntry(const DosGfxFiles& files, const DosScreenSpec& spec) {
    BackBuffer buf;
    const DosFont& font = files.font();
    drawSignInPanel(buf, files, spec);
    drawMsgBox(buf, font, files.exe(), BoxTexts::fromExe(files.exe()), 15);
    frameRect(buf, 0x30, 0x6F, 0x70, 0x84, 0xFF);
    const std::string& name = spec.typedName;
    for (size_t i = 0; i < name.size(); ++i)
        font.drawChar(buf, 0xFFFF, static_cast<uint8_t>(textWidth(font, name.substr(0, i)) + 0x35), 0x75, static_cast<uint8_t>(name[i]));
    int x1 = static_cast<uint8_t>(textWidth(font, name) + 0x36), x2 = x1 + 4;
    buf.fillRect(x1, 0x76, x2, 0x7E, spec.cursorOn ? 0xFF : 0xCC);  // the block cursor
    buf.fillRect(x2 + 1, 0x7E, 0x6F, 0x7E, 0xCC);
    for (int k = static_cast<int>(name.size()) + 1; k <= 7; ++k) {  // one underscore per free character cell
        x1 = x2 + 2;
        x2 = x1 + 4;
        buf.fillRect(x1, 0x7E, x2, 0x7E, 0xFF);
    }
    return buf;
}

}  // namespace

// ------------------------------------------------------------------------------------- files

DosGfxFiles DosGfxFiles::open(const std::filesystem::path& dir) {
    DosGfxFiles f(DosFiles::open(dir));
    f.font_ = DosFont::fromExe(f.exe());
    auto add = [&](auto& map, const char* stem, FileIndex index, auto load) {
        const std::string& name = f.files_.fileTable().name(index);
        map.emplace(stem, load(f.files_.bytes(index), mischief::toUpperAscii(name)));
    };
    add(f.pans_, "OPENING", FileIndex::OpeningPan, PanFile::load);
    add(f.pans_, "MOUNTAIN", FileIndex::MountainPan, PanFile::load);
    add(f.pans_, "MOUNT2N3", FileIndex::Mount2n3Pan, PanFile::load);
    add(f.pans_, "MOUNT3", FileIndex::Mount3Pan, PanFile::load);
    add(f.pans_, "CLOSE", FileIndex::ClosePan, PanFile::load);
    add(f.pans_, "SHACK", FileIndex::ShackPan, PanFile::load);
    add(f.paks_, "SPRITES", FileIndex::SpritesPak, PakFile::load);
    add(f.paks_, "BEGIN", FileIndex::BeginPak, PakFile::load);
    add(f.paks_, "CLOSE", FileIndex::ClosePak, PakFile::load);
    add(f.paks_, "SHACK", FileIndex::ShackPak, PakFile::load);
    return f;
}

const PanFile& DosGfxFiles::pan(std::string_view stem) const { return pans_.at(mischief::toUpperAscii(stem)); }
const PakFile& DosGfxFiles::pak(std::string_view stem) const { return paks_.at(mischief::toUpperAscii(stem)); }

// ------------------------------------------------------------------------------------- screens

std::optional<DosScreen> parseDosScreen(std::string_view name) {
    const std::string n = mischief::toLowerAscii(name);
    if (n == "title") return DosScreen::Title;
    if (n == "signin" || n == "sign-in") return DosScreen::SignIn;
    if (n == "clubhouse" || n == "start") return DosScreen::Clubhouse;
    if (n == "quiz") return DosScreen::Quiz;
    if (n == "path" || n == "level1") return DosScreen::Path;
    if (n == "castle") return DosScreen::Castle;
    if (n == "menu") return DosScreen::Menu;
    if (n == "msgbox" || n == "box") return DosScreen::MsgBox;
    if (n == "newname" || n == "nameentry") return DosScreen::NameEntry;
    return std::nullopt;
}

std::string_view dosScreenName(DosScreen s) {
    switch (s) {
        case DosScreen::Title: return "title";
        case DosScreen::SignIn: return "signin";
        case DosScreen::Clubhouse: return "clubhouse";
        case DosScreen::Quiz: return "quiz";
        case DosScreen::Path: return "path";
        case DosScreen::Castle: return "castle";
        case DosScreen::Menu: return "menu";
        case DosScreen::MsgBox: return "msgbox";
        case DosScreen::NameEntry: return "newname";
    }
    return "title";
}

DosScreenSpec defaultScreenSpec(DosScreen screen, Look look) {
    DosScreenSpec s;
    s.screen = screen;
    switch (screen) {
        case DosScreen::Title:
            // The sparkle and pointer visible in each capture (mouse at the driver's centre, clamped).
            switch (look) {
                case Look::Ega: s.sparkleSpot = 0; s.sparkleFrame = 40; s.pointer = {{159, 101}}; break;
                case Look::Tandy: s.sparkleSpot = 0; s.sparkleFrame = 38; s.pointer = {{159, 101}}; break;
                case Look::Mcga: s.sparkleSpot = 10; s.sparkleFrame = 40; s.pointer = {{158, 82}}; break;
                case Look::Cga: s.sparkleSpot = 5; s.sparkleFrame = 37; s.pointer = {{159, 146}}; break;
            }
            break;
        case DosScreen::SignIn:
        case DosScreen::Menu:
        case DosScreen::MsgBox:
        case DosScreen::NameEntry: break;
        case DosScreen::Clubhouse:  // new game (47c7:0b19): camera (0xC0, 0), player (0x140, 0x70), SPRITES 82
            s.pan = "MOUNTAIN";
            s.camX = 0xC0;
            s.coins = 5;
            s.nets = 10;
            s.actors = {{1, 82 * 2, ((0x140 - 0xC0) >> 1) + 2, 0x70 + 0x10, 0}};
            break;
        case DosScreen::Quiz:  // the catch position of ega/029, puzzle 61 in display order 0
            s.pan = "MOUNTAIN";
            s.camX = 0x120;
            s.camY = 0x780;
            s.restoreGrid = true;
            s.coins = 7;
            s.nets = 8;
            s.prompt = 2;
            s.puzzle = 61;
            break;
        case DosScreen::Path:  // ega/042, the locked tree-trunk gate
            s.pan = "MOUNTAIN";
            s.camX = 0x708;
            s.camY = 0x700;
            s.restoreGrid = true;
            s.cellPictures = {{0xEF, 0x40}};  // the trunk still locked (the saved game has it opened: 0x4C)
            s.coins = 8;
            s.nets = 8;
            s.prompt = 4;
            s.clueWords = {0x80, 0x80, 40};
            s.actors = {{1, 73 * 2, 126, 128, 0}};  // the player, found by matching the capture
            break;
        case DosScreen::Castle:  // ega/099, the ladder shaft
            s.pan = "MOUNT3";
            s.camX = 0x198;
            s.camY = 0x278;
            s.restoreGrid = true;
            s.coins = 31;
            s.nets = 0;
            s.keyState = 6;
            s.clueWords = {3, 46, 42};  // the level-3 clue words of that game
            s.actors = {{1, 172 * 2, 86, 120, 0}};
            break;
    }
    return s;
}

BackBuffer composeScreen(const DosGfxFiles& files, const DosScreenSpec& spec) {
    switch (spec.screen) {
        case DosScreen::Title: return composeTitle(files, spec);
        case DosScreen::SignIn: return composeSignIn(files, spec);
        case DosScreen::Quiz: return composeQuiz(files, spec);
        case DosScreen::Clubhouse:
        case DosScreen::Path:
        case DosScreen::Castle: return composePlay(files, spec);
        case DosScreen::Menu: return composeMenu(files, spec);
        case DosScreen::MsgBox: return composeMsgBox(files, spec);
        case DosScreen::NameEntry: return composeNameEntry(files, spec);
    }
    return {};
}

// ------------------------------------------------------------------------------- stand-ins

std::vector<uint16_t> initialCellPictures(const DosExe& exe) {
    std::vector<uint16_t> p(256);
    for (size_t i = 0; i < p.size(); ++i) p[i] = exe.ds16(static_cast<uint16_t>(kDsCellPictures + 2 * i));
    return p;
}

void restoreCellPictures(const DosExe& exe, std::span<const uint8_t> record, std::vector<uint16_t>& pictures) {
    if (record.size() < static_cast<size_t>(kUserRecord) || pictures.size() < 256) return;
    auto at = [&](int cell) -> uint16_t& { return pictures[static_cast<size_t>(cell & 0xFF)]; };
    for (int i = 0; i < 48; ++i) at(exe.ds16(static_cast<uint16_t>(kDsHidingCells + 2 * i))) = record[static_cast<size_t>(0xBB + i)];
    for (int j = 0; j < 9; ++j) at(exe.ds16(static_cast<uint16_t>(kDsVariableCells + 2 * j))) = record[static_cast<size_t>(0xEB + j)];
    for (const auto [from, to] : {std::pair{0xE3, 0xFD}, std::pair{0xAB, 0xC0}, std::pair{0x77, 0x86}})  // 1a54:06dc
        for (int k = 0; k < 3; ++k) at(to + k) = at(from + k);
    const int layout = record[0x166];  // low byte only (Game_Restore)
    int p = layout * 20;
    for (int r = 0; r < 5; ++r)  // 1a54:08dc
        for (int c = 0; c < 4; ++c, ++p) at(0x32 + 16 * r + c) = exe.ds16(static_cast<uint16_t>(kDsCastleLayouts + 2 * p));
    at(0xBF) = 0x41;  // 1a54:0803 Cells_AdjustForCpu, fast CPU (the cell types it changes are not pictures)
    at(0x7E) = 0x37;
    if (at(0x84) == 0x2E) at(0x84) = 0x2F;
    if (at(0xB7) == 0x2E) at(0xB7) = 0x2F;
}

Puzzle loadPuzzle(const DosGfxFiles& files, int index) {
    if (index < 0 || index >= PuzzleFile::kCount) throw std::out_of_range(std::format("no puzzle {}", index));
    const PuzzleFile puzzles = PuzzleFile::parse(files.files().bytes(FileIndex::AllPuzs));
    return puzzles.puzzle(index, Dictionary::parse(files.files().bytes(FileIndex::PrgDict)));
}

}  // namespace tmport::dos
