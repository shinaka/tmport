#include "gg/game/puzzles.h"

#include <format>
#include <stdexcept>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/data/resources.h"
#include "gg/game/pz_balance.h"
#include "gg/game/pz_electric.h"
#include "gg/game/pz_energy.h"
#include "gg/game/pz_gears.h"
#include "gg/game/pz_launch.h"
#include "gg/game/pz_magnets.h"
#include "gg/game/pz_pieces.h"
#include "gg/game/pz_simple.h"
#include "gg/gfx/cursor.h"
#include "gg/gfx/video.h"
#include "gg/text/font.h"
#include "gg/text/text.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

// 38e2:00d4 / the overlays' Screen_LoadPicture and PzSnd_LoadBank calls (code_puzzles_a.md §1.4).
constexpr std::array<PuzzleKindInfo, kPuzzleKinds> kKinds = {{
    {0x20B2, 0x20B3, 0x11CC, "electric circuits"},
    {0x204A, 0x204B, 0x11CA, "simple machines"},
    {0x2084, 0x2085, 0x11CA, "machine pieces"},
    {0x2021, 0x2022, 0x11C8, "gears"},
    {0x2119, 0x211A, 0x11C6, "balance"},
    {0x1133, 0x1134, 0x11CE, "launch"},
    {0x1093, 0x1094, 0x11D2, "energy forms"},
    {0x119F, 0x11A0, 0x11D0, "magnets"},
}};

}  // namespace

const PuzzleKindInfo& puzzleKindInfo(int kind) {
    if (kind < 0 || kind >= kPuzzleKinds) throw std::out_of_range(std::format("puzzle kind {}", kind));
    return kKinds[static_cast<size_t>(kind)];
}

int puzzleVariantCount(const GgExe& exe, int kind) {
    if (kind < 0 || kind >= kPuzzleKinds) throw std::out_of_range(std::format("puzzle kind {}", kind));
    return exe.dsS16(static_cast<uint16_t>(dg::kPuzzleVariants + 2 * kind));
}

// 38e2:00d4..00e6
void puzzleDispatchLine(Screen& screen) {
    screen.setDrawColor(0);
    screen.hLine(0, 9, 320);
}

// 38e2:0357 Puzzle_InitPanel
void PuzzlePanel::init(Screen& screen, Fonts& fonts, const Archive& archive, std::span<const uint8_t> text) {
    panel_ = archive.read(kPanelPicture);     // Res_LoadCopy
    goBack_ = archive.read(kGoBackPressed);   // Res_LoadCopy
    // TODO(wave 3): Zones_AddLocal(DS:3306), the Go Back zone, is the runtime's zone list.
    fonts.select(1);
    fonts.setColor(0);
    screen.lpxDrawKeyed(0, 165, panel_, 0);
    textDrawBox(screen, fonts, 4, 170, 208, text);
}

std::vector<uint8_t> puzzleText(const Archive& archive, uint16_t id) {
    const auto payload = archive.read(id);
    const auto t = textBytes(payload);
    return {t.begin(), t.end()};
}

void drawPuzzlePicture(Video& video, const Archive& archive, uint16_t picture, uint16_t palette) {
    std::vector<uint8_t> pic;
    video.loadPicture(archive, picture, palette, pic);  // Screen_LoadPicture
    video.screen().lpxDraw(0, 10, pic);
}

void puzzleFadeIn(Video& video) { video.flush(); }

std::unique_ptr<CursorRecord> loadCursor(const Archive& archive, uint16_t id) {
    return CursorRecord::fromResource(id, archive.read(id));
}

std::vector<std::pair<uint16_t, uint16_t>> runStoreCode(const GgExe& exe, uint16_t seg, uint16_t start, uint16_t end,
                                                        uint8_t modrm) {
    std::vector<std::pair<uint16_t, uint16_t>> stores;
    uint16_t pc = start;
    for (int guard = 0; pc != end; ++guard) {
        const auto code = exe.at(seg, pc);
        if (guard > 8192 || code.empty())
            throw FormatError(std::format("code tables {:04x}:{:04x}: no end before {:04x}", seg, pc, end));
        if (code.size() >= 6 && code[0] == 0xC7 && code[1] == modrm) {  // MOV word [..], imm16
            stores.emplace_back(static_cast<uint16_t>(code[2] | (code[3] << 8)), static_cast<uint16_t>(code[4] | (code[5] << 8)));
            pc = static_cast<uint16_t>(pc + 6);
        } else if (code.size() >= 3 && code[0] == 0xE9) {  // JMP rel16
            pc = static_cast<uint16_t>(pc + 3 + static_cast<int16_t>(code[1] | (code[2] << 8)));
        } else if (code.size() >= 2 && code[0] == 0xEB) {  // JMP rel8
            pc = static_cast<uint16_t>(pc + 2 + static_cast<int8_t>(code[1]));
        } else {
            throw FormatError(std::format("code tables {:04x}:{:04x}: unexpected opcode {:02x}", seg, pc, code[0]));
        }
    }
    return stores;
}

uint16_t codeWord(const GgExe& exe, uint16_t seg, uint16_t off) {
    const auto b = exe.at(seg, off);
    if (b.size() < 2) throw FormatError(std::format("code table {:04x}:{:04x} is outside the overlay", seg, off));
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}

bool spansOverlap(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2) {
    auto axis = [](int a, int wa, int b, int wb) {
        if (wb < wa) return (a <= b && b < a + wa) || (a < b + wb && b + wb <= a + wa);
        return (b <= a && a < b + wb) || (b < a + wa && a + wa <= b + wb);
    };
    return axis(x1, w1, x2, w2) && axis(y1, h1, y2, h2);
}

CursorRegion zoneAt(const GgExe& exe, uint16_t z, CursorRecord* record) {
    return {exe.dsS16(z), exe.dsS16(z + 2), exe.dsS16(z + 4), exe.dsS16(z + 6), exe.dsS16(z + 8), exe.dsS16(z + 0x0A), record};
}

CursorRegion cursorRegionAt(const GgExe& exe, uint16_t regionOff, CursorRecord* record) {
    return zoneAt(exe, static_cast<uint16_t>(regionOff + 8), record);
}

std::unique_ptr<Puzzle> makePuzzle(int kind, const GgExe& exe, const Archive& archive) {
    switch (kind) {
    case 0: return std::make_unique<PzElectric>(exe, archive);
    case 1: return std::make_unique<PzSimple>(exe, archive);
    case 2: return std::make_unique<PzPieces>(exe, archive);
    case 3: return std::make_unique<PzGears>(exe, archive);
    case 4: return std::make_unique<PzBalance>(exe, archive);
    case 5: return std::make_unique<PzLaunch>(exe, archive);
    case 6: return std::make_unique<PzEnergy>(exe, archive);
    case 7: return std::make_unique<PzMagnets>(exe, archive);
    default: throw std::out_of_range(std::format("puzzle kind {}", kind));
    }
}

std::unique_ptr<Puzzle> drawPuzzleScreen(Video& video, const GgExe& exe, const Archive& archive, Fonts& fonts,
                                         int kind, int variant, const PuzzleSceneValues& scene) {
    const int variants = puzzleVariantCount(exe, kind);
    if (variant < 0 || variant >= variants)
        throw std::out_of_range(std::format("puzzle kind {} has {} variants", kind, variants));
    auto pz = makePuzzle(kind, exe, archive);
    puzzleDispatchLine(video.screen());  // 38e2:00d4
    pz->setup(video, fonts, variant);
    pz->applyScene(video, scene);
    return pz;
}

}  // namespace tmport::gg
