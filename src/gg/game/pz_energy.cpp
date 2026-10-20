#include "gg/game/pz_energy.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/gfx/cursor.h"
#include "gg/gfx/video.h"
#include "gg/text/font.h"
#include "gg/text/text.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

// The DGROUP window the variant code writes: DS:ad70 .. af58 (both cleared blocks).
constexpr uint16_t kWindow = 0xAD70;
constexpr size_t kWindowSize = 0xAF58 - 0xAD70;
constexpr uint16_t kForms = 0xADB8, kDevices = 0xAD70, kUses = 0xAD72, kSatisfied = 0xADBA, kBudget = 0xAE8A;
constexpr int kAreaW = 0x58, kAreaH = 0x22;
constexpr uint8_t kFillColour = 0x9D;

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

}  // namespace

std::vector<uint8_t> formatOneInt(std::span<const uint8_t> format, int value) {
    std::vector<uint8_t> out;
    bool done = false;
    for (size_t i = 0; i < format.size(); ++i) {
        if (format[i] == '%' && i + 1 < format.size()) {
            if (format[i + 1] == '%') {
                out.push_back('%');
                ++i;
                continue;
            }
            if (format[i + 1] == 'd' && !done) {
                const std::string n = std::to_string(value);
                out.insert(out.end(), n.begin(), n.end());
                done = true;
                ++i;
                continue;
            }
        }
        out.push_back(format[i]);
    }
    return out;
}

// 33cd:1274 PzEnergy_LoadVariant, interpreted
PzEnergy::Variant PzEnergy::readVariant(const GgExe& exe, int variant) {
    std::vector<uint8_t> mem(kWindowSize, 0);  // setmem of DS:adb8 (0x1a0) and DS:ad70 (0x120)
    auto store = [&](uint16_t a, uint16_t v) {
        if (a < kWindow || static_cast<size_t>(a - kWindow) + 2 > mem.size())
            throw FormatError(std::format("energy variant {}: a store to DS:{:04x} outside the variant tables", variant, a));
        mem[a - kWindow] = static_cast<uint8_t>(v);
        mem[a - kWindow + 1u] = static_cast<uint8_t>(v >> 8);
    };
    auto load = [&](uint16_t a) -> int16_t {
        return a >= kWindow && static_cast<size_t>(a - kWindow) + 2 <= mem.size() ? s16(mem, a - kWindow) : int16_t{0};
    };
    Variant v;
    if (variant >= 0 && variant <= 0x1B) {  // CMP BX,0x1b; JBE: larger values go straight to 1b23
        const uint16_t start = codeWord(exe, kCodeSeg, static_cast<uint16_t>(kVariantTable + 2 * variant));
        for (const auto& [a, value] : runStoreCode(exe, kCodeSeg, start, kVariantEnd, 0x06)) {
            store(a, value);
            ++v.stores;
        }
    }
    // 33cd:1b23: satisfied[k][d] = (word [ad72 + 0x12 d + 2 form[k]] == 0) (form -1 reads the device id)
    for (int k = 0; k < 4; ++k)
        for (int d = 0; d < 4; ++d) {
            const int f = load(static_cast<uint16_t>(kForms + 0x1A * k));
            auto& satisfied = v.satisfied[static_cast<size_t>(k)][static_cast<size_t>(d)];
            satisfied = load(static_cast<uint16_t>(kUses + 0x12 * d + 2 * f)) == 0 ? 1 : 0;
            store(static_cast<uint16_t>(kSatisfied + 0x1A * k + 2 * d), static_cast<uint16_t>(satisfied));
        }
    v.budget = load(kBudget);
    for (int k = 0; k < 4; ++k) v.form[static_cast<size_t>(k)] = load(static_cast<uint16_t>(kForms + 0x1A * k));
    for (int d = 0; d < 4; ++d) {
        v.device[static_cast<size_t>(d)] = static_cast<uint16_t>(load(static_cast<uint16_t>(kDevices + 0x12 * d)));
        for (int f = 0; f < 8; ++f)
            v.uses[static_cast<size_t>(d)][static_cast<size_t>(f)] = load(static_cast<uint16_t>(kUses + 0x12 * d + 2 * f));
    }
    return v;
}

PzEnergy::PzEnergy(const GgExe& exe, const Archive& archive) : exe_(exe), archive_(archive) {}

int PzEnergy::layout(int i) const { return exe_.dsS16(static_cast<uint16_t>(dg::kEnergyLayout + 2 * i)); }

std::vector<uint8_t> PzEnergy::formName(int f) const {
    const auto s = exe_.dsString(static_cast<uint16_t>(0x28EA + 20 * f));  // DS:28ea + 20 f
    return {s.begin(), s.end()};
}

// 33cd:0000 PzEnergy_Run, up to the loop
void PzEnergy::setup(Video& video, Fonts& fonts, int variant) {
    // TODO(wave 3): PzSnd_LoadBank(0x11d2) is the audio area's.
    mode = 0;
    variant_ = variant;
    v_ = readVariant(exe_, variant);
    // 33cd:1cc7 PzEnergy_LoadResources
    digits_ = Bundle::load(archive_, kPuzzleDigits);
    pencil_ = archive_.read(kPencil);
    panelText_ = puzzleText(archive_, kPanelFormat);
    arrow_ = loadCursor(archive_, kPuzzleCursor);
    pencilCursor_ = loadCursor(archive_, 7);
    pencilCursor_->hotX = 0;
    pencilCursor_->hotY = 3;
    eraserCursor_ = loadCursor(archive_, 8);
    eraserCursor_->hotX = 0;
    eraserCursor_->hotY = 0x12;
    for (int d = 0; d < 4; ++d)
        if (v_.device[static_cast<size_t>(d)] != 0xFFFF) {
            devices_[static_cast<size_t>(d)] = Bundle::load(archive_, v_.device[static_cast<size_t>(d)]);
            idleFrame[static_cast<size_t>(d)] = 0;
        }
    // 33cd:1ba1 PzEnergy_DrawScreen
    Screen& screen = video.screen();
    drawPuzzlePicture(video, archive_, puzzleKindInfo(6).picture, puzzleKindInfo(6).palette);
    drawLabels(screen, fonts);
    PuzzlePanel panel;
    panel.init(screen, fonts, archive_, formatOneInt(panelText_, v_.budget));
    for (int k = 0; k < 4; ++k) {
        if (v_.form[static_cast<size_t>(k)] == -1) {
            screen.setDrawColor(kFillColour);
            screen.fillRect(layout(11) - 4, layout(13 + k) - 4, 15, 15);
        }
        if (v_.device[static_cast<size_t>(k)] == 0xFFFF) {
            screen.setDrawColor(kFillColour);
            screen.fillRect(layout(12) - 4, layout(13 + k) - 4, 15, 15);
        } else {
            screen.lpxDrawKeyed(layout(0), layout(1 + k), memberOf(devices_[static_cast<size_t>(k)], 0), 0);
        }
    }
    screen.saveToBack(0, 0, 320, 200);
    drawPencilCount(video);
    video.cursor().setCursorPicture(pencilCursor_.get());
    puzzleFadeIn(video);
    // TODO(wave 3): PzEnergy_AddZones / AddConnectorZones (the runtime's zones), Hint_Show (UI);
    // Timer_Set(7, 30) starts the idle animation.
}

// 33cd:0735 PzEnergy_BuildHintAndLabels (the drawing; the hint text is the viewer's)
void PzEnergy::drawLabels(Screen& screen, Fonts& fonts) const {
    fonts.select(0);
    fonts.setColor(0);
    fonts.current().setSpacing(1);
    for (int k = 0; k < 4; ++k) {
        const int f = v_.form[static_cast<size_t>(k)];
        if (f == -1) continue;
        const auto name = formName(f);
        const int x = layout(11) - 4 - textWidth(fonts, name);
        fonts.drawString(screen, x, layout(13 + k) - 4, name);
    }
    fonts.current().setSpacing(2);
}

// 33cd:1135 PzEnergy_DrawPencilCount
void PzEnergy::drawPencilCount(Video& video) const {
    Screen& screen = video.screen();
    screen.restoreRect(10, 0x38, 0x28, 0x21);
    if (v_.budget > 0) {
        screen.lpxDrawKeyed(0x0E, 0x3F, pencil_, 0);
        screen.lpxDrawKeyed(0x25, 0x46, memberOf(digits_, v_.budget), 0);
    }
    video.flush();
}

void PzEnergy::drawIdleFrame(Screen& screen, int d, int n) const {
    screen.setDrawColor(kFillColour);
    screen.fillRect(layout(0), layout(1 + d), kAreaW, kAreaH);
    screen.lpxDrawKeyed(layout(0), layout(1 + d), memberOf(devices_[static_cast<size_t>(d)], n), 0);
    screen.saveToBack(layout(0), layout(1 + d), kAreaW, kAreaH);
}

void PzEnergy::applyScene(Video& video, const PuzzleSceneValues& scene) {
    bool any = false;
    for (int d = 0; d < 4; ++d) {
        const auto it = scene.find(std::format("device{}", d));
        if (it == scene.end() || v_.device[static_cast<size_t>(d)] == 0xFFFF || it->second < 0) continue;
        // DS:ae20[d] is the frame the next tick draws; the one on the screen is the one before.
        const int count = static_cast<int>(devices_[static_cast<size_t>(d)].size());
        const int shown = (it->second + count - 1) % std::max(count, 1);
        drawIdleFrame(video.screen(), d, shown);
        idleFrame[static_cast<size_t>(d)] = it->second;
        any = true;
    }
    if (any) video.flush();
}

std::string PzEnergy::describe() const {
    int rows = 0;
    for (const int f : v_.form) rows += f != -1;
    return std::format("energy forms variant {}: {} forms, {} lines, {} stores", variant_, rows, v_.budget, v_.stores);
}

}  // namespace tmport::gg
