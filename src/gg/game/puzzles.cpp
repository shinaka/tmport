#include "gg/game/puzzles.h"

#include <algorithm>
#include <exception>
#include <format>
#include <optional>
#include <stdexcept>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/data/resources.h"
#include "gg/game/game.h"
#include "gg/save/gg_state_file.h"
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

// 38e2:0357 Puzzle_InitPanel, the drawing (the game's puzzleInitPanel also pushes the Go Back zone)
void PuzzlePanel::init(Screen& screen, Fonts& fonts, const Archive& archive, std::span<const uint8_t> text) {
    load(archive);
    fonts.select(1);
    fonts.setColor(0);
    screen.lpxDrawKeyed(0, 165, panel_, 0);
    textDrawBox(screen, fonts, 4, 170, 208, text);
}

void PuzzlePanel::load(const Archive& archive) {
    panel_ = archive.read(kPanelPicture);     // Res_LoadCopy
    goBack_ = archive.read(kGoBackPressed);   // Res_LoadCopy
}

void PuzzlePanel::free() {
    panel_.clear();
    goBack_.clear();
}

// ---- the puzzles' common calls on the game (puzzles.h, "wave 3")

uint16_t puzzleGetCode(GgGame& g) { return InputCodes::stripHeld(g.rt.getInputCode()); }

void pzSndWaitEnd(GgGame& g, int i) {
    while (g.sound.pzIsActive(i) == 1) g.rt.idle();
}

void puzzleWaitRelease(GgGame& g) {
    while (g.mouse().buttons() != 0) g.rt.idle();
}

void puzzleLoadScreen(GgGame& g, uint16_t picture, uint16_t palette) {
    std::vector<uint8_t> pic;
    g.display.runLoad(g.video.loadPicture(g.archive(), picture, palette, pic));  // Screen_LoadPicture
    g.video.screen().lpxDraw(0, 10, pic);                                         // Lpx_Draw(0, 10)
}

Zone zoneFromDs(const GgExe& exe, uint16_t off) { return Zone::fromRecord(exe.dsBytes(off, Zone::kRecordSize)); }

CursorRecord& activeCursorCopy(Video& video) { return video.cursor().activeCopy(); }

void puzzleSetMouseRange(GgGame& g, int minX, int minY, int maxX, int maxY) {
    Cursor& c = g.video.cursor();
    int x = c.x(), y = c.y();
    if (x > maxX) x = maxX;  // only the upper bounds are clamped
    if (y > maxY) y = maxY;
    c.setPosition(x, y);
    g.mouse().driver().setRangeX(2 * minX, 2 * maxX);  // INT 33h fn 7: CX = 2 min, DX = 2 max
    g.mouse().driver().setRangeY(minY, maxY);          // fn 8
    CursorRecord& active = activeCursorCopy(g.video);  // DS:7a6c / 7a6e = the pointer
    active.saveX = x;
    active.saveY = y;
}

PuzzleUnwindGuard::PuzzleUnwindGuard(GgGame& g) : g_(g), exceptions_(std::uncaught_exceptions()) {}

PuzzleUnwindGuard::~PuzzleUnwindGuard() {
    if (std::uncaught_exceptions() <= exceptions_) return;
    try {
        g_.zones().clearLocal();
        g_.video.cursor().clearRegions();
        g_.video.cursor().forceDefault(0);
        g_.video.cursor().setCursorPicture(g_.video.cursor().arrow());
    } catch (...) {
        // nothing more to do while unwinding
    }
}

void restoreCursorPicture(GgGame& g, const GgStateFile& s, CursorRecord* rec) {
    Cursor& c = g.video.cursor();
    if (stateSamePointer(s, 0x7A4A, dg::kCursorArrow) && s.dsWord(0x0A52) == 0) {
        c.defaultRegion().record = rec;  // DS:0a6c; the active copy stays the arrow (DS:7a4a)
        return;
    }
    c.setCursorPicture(rec);
}

// ---- reading a DOSBox-X state's lists

namespace {

// A far pointer at a physical address as a DS offset when it points into DGROUP.
std::optional<uint16_t> dsTarget(const GgStateFile& s, uint32_t linear) {
    const auto b = s.bytesAt(linear, 4);
    if (b.size() < 4) return std::nullopt;
    const uint16_t off = static_cast<uint16_t>(b[0] | (b[1] << 8));
    const uint16_t seg = static_cast<uint16_t>(b[2] | (b[3] << 8));
    if (seg != s.dgroupSegment() || (off == 0 && seg == 0)) return std::nullopt;
    return off;
}

}  // namespace

std::vector<uint16_t> stateLocalZones(const GgStateFile& s) {
    std::vector<uint16_t> out;
    const uint32_t ds = static_cast<uint32_t>(s.dgroupSegment()) * 16;
    auto next = dsTarget(s, ds + dg::kZonesLocal);
    for (int i = 0; next && i < 60; ++i) {
        out.push_back(*next);
        next = dsTarget(s, ds + *next + 0x0C);
    }
    return out;
}

std::vector<uint16_t> stateCursorRegions(const GgStateFile& s) {
    std::vector<uint16_t> out;
    const uint32_t ds = static_cast<uint32_t>(s.dgroupSegment()) * 16;
    const uint32_t head = s.farPointerAt(0x0A8C);  // the list head {far first, far last}
    if (head == 0) return out;
    auto next = dsTarget(s, head);
    for (int i = 0; next && i < 60; ++i) {
        out.push_back(*next);
        next = dsTarget(s, ds + *next);
    }
    return out;
}

bool stateSamePointer(const GgStateFile& s, uint16_t off, uint16_t other) {
    const uint32_t a = s.dsDword(off), b = s.dsDword(other);
    return a != 0 && a == b;
}

bool stateAtMainLoop(const GgStateFile& s, const PuzzleLoopMarkers& m, std::string& why) {
    const auto& cpu = s.cpu();
    if (!cpu) {
        why = "the state has no CPU registers";
        return false;
    }
    auto in = [](std::span<const uint16_t> set, uint16_t v) { return std::find(set.begin(), set.end(), v) != set.end(); };
    for (const auto& [seg, off] : s.file().stackWords(*cpu, 0x100)) {
        if (in(m.mainReturns, off)) return true;
        if (in(m.subReturns, off)) {
            why = std::format("the state is inside a call of the puzzle's dispatch (return offset {:04x}), not in its main loop", off);
            return false;
        }
    }
    // The overlays run above the resident code (image segments from 2a80 on).
    if (s.toImageSegment(cpu->cs) >= 0x2A80 && cpu->ip >= m.loopFirst && cpu->ip <= m.loopLast) return true;
    why = std::format("the state is not in the puzzle's main loop (CPU at {:04x}:{:04x})", s.toImageSegment(cpu->cs), cpu->ip);
    return false;
}

bool relinkStateZones(GgGame& g, const GgStateFile& s, const std::vector<std::pair<uint16_t, Zone*>>& zones,
                      std::string& why) {
    std::vector<std::pair<uint16_t, Zone*>> known = zones;
    known.emplace_back(dg::kPzGoBackZone, &g.puzzlesObjects().goBackZone);
    known.emplace_back(dg::kPzPanelZone, &g.puzzlesObjects().panelZone);
    const auto order = stateLocalZones(s);
    std::vector<Zone*> linked;
    for (const uint16_t off : order) {
        const auto it = std::find_if(known.begin(), known.end(), [off](const auto& k) { return k.first == off; });
        if (it == known.end()) {
            why = std::format("a click zone the puzzle does not know (DS:{:04x})", off);
            return false;
        }
        *it->second = Zone::fromRecord(s.dgroup().subspan(off, Zone::kRecordSize));
        linked.push_back(it->second);
    }
    g.zones().clearLocal();
    for (auto z = linked.rbegin(); z != linked.rend(); ++z) g.zones().addLocal(**z);  // the head is added last
    return true;
}

// The default of the puzzle classes that cannot be resumed (puzzles.h).
bool Puzzle::restoreFromState(GgGame&, const GgStateFile&, std::string& why) {
    why = "this puzzle kind cannot be resumed from a state";
    return false;
}

int Puzzle::resume(GgGame&) { return 0; }

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
