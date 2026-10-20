// PzEnergy's run on the game (pz_energy.h, "wave 3: the run"; ovl15 33cd,
// re/gg/docs/code_puzzles_a.md §4.3-§4.6). Owner: puzzles_a agent.
#include "gg/game/pz_energy.h"

#include <algorithm>
#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/flow.h"
#include "gg/game/game.h"
#include "gg/save/gg_state_file.h"
#include "gg/text/text.h"
#include "gg/ui/hints.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

constexpr uint16_t kWorkCode = 0x44C, kCheckCode = 0x44E, kHintCode = 0x44F, kEraserCode = 0x450, kPencilCode = 0x451;
constexpr uint16_t kFormFirst = 0x452, kFormLast = 0x455, kDeviceFirst = 0x456, kDeviceLast = 0x459, kGoBack = 0x5DD,
                   kPanel = 0x5DE;
// The fixed zones of PzEnergy_AddZones (DGROUP records without a name): eraser, pencil, Hint, check.
constexpr uint16_t kEraserZone = 0x2870, kPencilZone = 0x2886, kHintZone = 0x289C, kCheckZone = 0x28B2;
constexpr int kAreaW = 0x58, kAreaH = 0x22;
constexpr uint8_t kFillColour = 0x9D, kPulseColour = 0x83;
constexpr int kNoPosition = -0x8000;  // the drawing loops' "last pointer" locals start uninitialised [I]
constexpr size_t kMaxHint = 2000;

}  // namespace

// The word DS:ad72 + 0x12 d + 2 f (device d uses form f); the form -1 of an empty row reads the device's
// id word DS:ad70 + 0x12 d.
bool PzEnergy::uses(int d, int f) const {
    const auto& dev = st_->devices[static_cast<size_t>(std::clamp(d, 0, 3))];
    if (f < 0 || f > 7) return dev.id != 0;
    return dev.uses[static_cast<size_t>(f)] != 0;
}

int PzEnergy::deviceFrames(int d) const { return static_cast<int>(devices_[static_cast<size_t>(d)].size()); }

// 33cd:0000 PzEnergy_Run
int PzEnergy::run(GgGame& g, int variant) {
    game_ = &g;
    PuzzleUnwindGuard guard(g);  // the set-up yields too (the fades, the hint box)
    variant_ = variant;
    g.sound.pzLoadBank(kEffectBank);
    st_->mode = 0;
    v_ = readVariant(exe_, variant);  // 33cd:1274 PzEnergy_LoadVariant
    loadVariantIntoState();
    loadResources();
    // 33cd:1ba1 PzEnergy_DrawScreen
    Screen& screen = g.video.screen();
    puzzleLoadScreen(g, puzzleKindInfo(6).picture, puzzleKindInfo(6).palette);
    buildHintAndLabels(g, variant);
    puzzleInitPanel(g, formatOneInt(panelText_, st_->linesLeft));  // sprintf(buf[150], 0x1132, lines)
    for (int k = 0; k < 4; ++k) {
        if (st_->forms[static_cast<size_t>(k)].form == -1) {
            screen.setDrawColor(kFillColour);
            screen.fillRect(layout(11) - 4, layout(13 + k) - 4, 15, 15);
        }
        if (st_->devices[static_cast<size_t>(k)].id == -1) {
            screen.setDrawColor(kFillColour);
            screen.fillRect(layout(12) - 4, layout(13 + k) - 4, 15, 15);
        } else {
            screen.lpxDrawKeyed(layout(0), layout(1 + k), memberOf(devices_[static_cast<size_t>(k)], 0), 0);
        }
    }
    screen.saveToBack(0, 0, 320, 200);
    drawPencilCount(g.video);
    g.video.cursor().setCursorPicture(pencilCursor_.get());
    g.display.fadeOutIn();
    addZones(g);
    addConnectorZones(g);
    hintShow(g, 0x1052);
    g.timers().set(7, 0x1E);
    return loop(g, false);
}

// 33cd:004a..01dd: the main loop and the exit.
int PzEnergy::loop(GgGame& g, bool solved) {
    game_ = &g;
    PuzzleUnwindGuard guard(g);
    auto& p = g.puzzles;
    for (bool leave = false; !leave;) {
        const uint16_t code = puzzleGetCode(g);
        idleAnimate(g);  // every pass
        if (code >= kFormFirst && code <= kFormLast) {
            g.sound.pzPlay(0x17, 3);
            drawFromForm(g, code);
            continue;
        }
        if (code >= kDeviceFirst && code <= kDeviceLast) {
            g.sound.pzPlay(0x17, 3);
            drawFromDevice(g, code);
            continue;
        }
        switch (code) {
        case code::kMenuRequest1:
            p.leave = 2;
            leave = true;
            break;
        case code::kMenuRequest2:
            p.leave = 1;
            leave = true;
            break;
        case kWorkCode:
            if (st_->mode == 1) eraseLines(g);
            break;
        case kEraserCode: selectEraser(g); break;
        case kPencilCode: selectPencil(g); break;
        case kCheckCode:
            g.sound.pzPlay(0x2B, 5);
            g.video.screen().lpxDrawKeyed(12, 131, checkPressed_, 0);
            g.display.flush();
            puzzleWaitRelease(g);
            g.video.screen().restoreRect(12, 131, 37, 15);
            g.display.flush();
            sendEnergy(g);
            if (!solved && isSolved()) {
                solved = true;
                puzzleShowSolved(g, 0x27);  // jingle 0x27..0x2a
                puzzleAwardPoints(g, 0x31);
            }
            break;
        case kHintCode:
            g.video.screen().lpxDrawKeyed(11, 14, hintPressed_, 0);
            g.display.flush();
            g.sound.pzPlay(0x26, 5);
            puzzleWaitRelease(g);
            g.video.screen().restoreRect(11, 14, 38, 17);
            g.display.flush();
            puzzleShowHintText(g, hintText_);
            break;
        case kGoBack:
            g.sound.pzPlay(0x30, 5);
            puzzlePressGoBack(g);
            p.goThrough = 0;
            leave = true;
            break;
        case code::kEnter:
        case kPanel:
            if (solved) {
                p.goThrough = 1;
                leave = true;
            }
            break;
        case code::kEsc: cmdSaveAndExit(g); break;
        default: break;
        }
    }
    g.display.fadeOut();
    cleanup(g);
    g.sound.pzFreeBank();
    game_ = nullptr;
    return solved ? 1 : 0;
}

// 33cd:022a PzEnergy_SetMouseRange
void PzEnergy::setMouseRange(GgGame& g, bool drawing) {
    const CursorRecord& c = g.video.cursor().active();  // DS:7a70 / 7a72
    if (drawing) puzzleSetMouseRange(g, 0x3D, 0x12, 0x13F - c.width, 0x97);
    else puzzleSetMouseRange(g, 0, 0, 0x13F - c.width, 0xC7 - c.height);
}

// 33cd:02a0 PzEnergy_SendEnergy
void PzEnergy::sendEnergy(GgGame& g) {
    g.video.screen().setDrawColor(15);
    const int fx = lineFormX(), dx = lineDeviceX();
    for (int k = 0; k < 4; ++k)
        for (int d = 0; d < 4; ++d) {
            if (energyLine(*st_, k, d) == 0 || energySent(*st_, d, k) != 0) continue;
            const int yk = lineY(k), yd = lineY(d);
            animatePulse(g, fx, yk, dx, yd, false);
            if (!uses(d, st_->forms[static_cast<size_t>(k)].form)) {
                energyLine(*st_, k, d) = 0;
                animatePulse(g, dx, yd, fx, yk, true);  // the line retracts
                ++st_->linesLeft;
                drawPencilCount(g.video);
                g.video.screen().restoreRect(layout(0), layout(1 + d), kAreaW, kAreaH);
                g.display.flush();
            } else {
                animateDevice(g, d);
                energySent(*st_, d, k) = 1;
                animatePulse(g, dx, yd, fx, yk, false);
            }
        }
}

// 33cd:03c8 PzEnergy_AnimateDevice
void PzEnergy::animateDevice(GgGame& g, int d) {
    const int r = st_->devices[static_cast<size_t>(d)].id;
    const Bundle work = Bundle::load(archive_, static_cast<uint16_t>(r + 1));  // the working animation
    const auto reps = archive_.read(static_cast<uint16_t>(r + 2));             // {repeat count, final frame}
    const auto effects = archive_.read(static_cast<uint16_t>(r + 3));          // an effect per frame (-1 none)
    Screen& screen = g.video.screen();
    const int x = layout(0), y = layout(1 + d);
    screen.setDrawColor(kFillColour);
    auto& idle = st_->idleFrame[static_cast<size_t>(d)];
    for (;;) {  // the idle cycle is finished first
        g.timers().set(7, 0x1E);
        ++idle;
        if (deviceFrames(d) - 1 <= idle) break;
        screen.fillRect(x, y, kAreaW, kAreaH);
        screen.lpxDrawKeyed(x, y, memberOf(devices_[static_cast<size_t>(d)], idle), 0);
        g.display.flush();
        g.rt.timerWait(7);
    }
    const int repeats = s16(reps, 0);
    for (int rep = 0; rep < repeats; ++rep)
        for (int f = 0; f < static_cast<int>(work.size()); ++f) {
            screen.fillRect(x, y, kAreaW, kAreaH);
            screen.lpxDrawKeyed(x, y, work.member(static_cast<size_t>(f)).payload, 0);
            g.display.flush();
            const int effect = s16(effects, 2 * static_cast<size_t>(f));
            if (effect != -1) g.sound.pzPlay(effect, 3);
            g.rt.timerWait(7);
            g.timers().set(7, 0x1E);
        }
    idle = -1;  // the device keeps its final frame
    screen.lpxDrawKeyed(x, y, memberOf(work, s16(reps, 2)), 0);
    screen.saveToBack(x, y, kAreaW, kAreaH);
    g.sound.pzStopAll();
}

// 33cd:064d PzEnergy_IdleAnimate
void PzEnergy::idleAnimate(GgGame& g) {
    if (!g.rt.timerExpired(7)) return;
    Screen& screen = g.video.screen();
    screen.setDrawColor(kFillColour);
    for (int d = 0; d < 4; ++d) {
        auto& idle = st_->idleFrame[static_cast<size_t>(d)];
        if (st_->devices[static_cast<size_t>(d)].id == -1 || idle == -1) continue;
        screen.fillRect(layout(0), layout(1 + d), kAreaW, kAreaH);
        screen.lpxDrawKeyed(layout(0), layout(1 + d), memberOf(devices_[static_cast<size_t>(d)], idle), 0);
        ++idle;
        if (static_cast<uint16_t>(deviceFrames(d)) <= static_cast<uint16_t>(idle)) idle = 0;
        screen.saveToBack(layout(0), layout(1 + d), kAreaW, kAreaH);
    }
    g.timers().set(7, 0x1E);
    g.display.flush();
}

void PzEnergy::buildHint(int variant) {
    hintText_.clear();
    auto add = [&](std::span<const uint8_t> t) {
        hintText_.insert(hintText_.end(), t.begin(), t.end());
        hintText_.push_back('\n');
        hintText_.push_back('\n');
    };
    if (variant >= 0 && variant < 3) add(hintHeader_);
    for (int k = 0; k < 4; ++k) {
        const int f = st_->forms[static_cast<size_t>(k)].form;
        if (f != -1) add(textBytes(memberOf(formTexts_, f)));
    }
    if (hintText_.size() >= kMaxHint) log::debug("PzEnergy_BuildHintAndLabels: the hint is past its 2000-byte buffer");
    if (hintText_.size() >= 2) hintText_.resize(hintText_.size() - 2);  // the last LF LF removed
}

// 33cd:0735 PzEnergy_BuildHintAndLabels
void PzEnergy::buildHintAndLabels(GgGame& g, int variant) {
    buildHint(variant);
    drawLabels(g.video.screen(), g.fonts);  // font 0, colour 0, spacing 1, the names, spacing 2
}

// 33cd:08f5 PzEnergy_DrawLines
void PzEnergy::drawLines(GgGame& g) {
    g.video.screen().restoreRect(0x3B, 0x10, 0xFE, 0x8D);
    for (int k = 0; k < 4; ++k)
        for (int d = 0; d < 4; ++d)
            if (energyLine(*st_, k, d) != 0) drawLine(g, lineFormX(), lineY(k), lineDeviceX(), lineY(d), 0);
    g.display.flush();
}

// 33cd:099c PzEnergy_DrawFromForm
void PzEnergy::drawFromForm(GgGame& g, int code) {
    if (st_->mode != 0) return;
    const int k = code - kFormFirst;
    const int sx = lineFormX(), sy = lineY(k);
    setMouseRange(g, true);
    int lastX = kNoPosition, lastY = kNoPosition;
    for (bool stop = false; !stop;) {
        const uint16_t c = puzzleGetCode(g);
        if (lastX != g.mouse().x() || lastY != g.mouse().y()) {
            lastX = g.mouse().x();
            lastY = g.mouse().y();
            drawLines(g);
            drawLine(g, sx, sy, g.mouse().x(), g.mouse().y(), 0);
            g.display.flush();
        }
        // The jump table CS:0acb over 0x44c..0x459: the work area or a form stops, a device completes.
        if (c < kWorkCode || c > kDeviceLast) continue;
        if (c == kWorkCode || (c >= kFormFirst && c <= kFormLast)) {
            stop = true;
        } else if (c >= kDeviceFirst) {
            const int d = c - kDeviceFirst;
            if (energyLine(*st_, k, d) == 1) continue;  // already drawn: the rubber line goes on
            g.sound.pzPlay(0x18, 3);
            energyLine(*st_, k, d) = 1;
            if (uses(d, st_->forms[static_cast<size_t>(k)].form)) energySatisfied(*st_, k, d) = 1;
            if (--st_->linesLeft == 0) {
                g.video.cursor().setCursorPicture(arrow_.get());
                st_->mode = -1;
            }
            drawPencilCount(g.video);
            stop = true;
        }
    }
    drawLines(g);
    setMouseRange(g, false);
}

// 33cd:0ae7 PzEnergy_DrawFromDevice
void PzEnergy::drawFromDevice(GgGame& g, int code) {
    if (st_->mode != 0) return;
    const int d = code - kDeviceFirst;
    const int sx = lineDeviceX(), sy = lineY(d);
    setMouseRange(g, true);
    int lastX = kNoPosition, lastY = kNoPosition;
    for (bool stop = false; !stop;) {
        const uint16_t c = puzzleGetCode(g);
        if (lastX != g.mouse().x() || lastY != g.mouse().y()) {
            lastX = g.mouse().x();
            lastY = g.mouse().y();
            drawLines(g);
            // The rubber line ends at the pencil's offset (the other one at the pointer: keep).
            drawLine(g, sx, sy, g.mouse().x(), g.mouse().y() + pencilCursor_->hotY, 0);
            g.display.flush();
        }
        // The jump table CS:0c22: the work area or a device stops, a form completes.
        if (c < kWorkCode || c > kDeviceLast) continue;
        if (c == kWorkCode || c >= kDeviceFirst) {
            stop = true;
        } else if (c >= kFormFirst && c <= kFormLast) {
            const int k = c - kFormFirst;
            if (energyLine(*st_, k, d) == 1) continue;
            g.sound.pzPlay(0x18, 3);
            energyLine(*st_, k, d) = 1;
            if (uses(d, st_->forms[static_cast<size_t>(k)].form)) energySatisfied(*st_, k, d) = 1;
            if (--st_->linesLeft == 0) {
                g.video.cursor().setCursorPicture(arrow_.get());
                st_->mode = -1;
            }
            drawPencilCount(g.video);
            stop = true;
        }
    }
    drawLines(g);
    setMouseRange(g, false);
}

// 33cd:0c3e PzEnergy_IsSolved
bool PzEnergy::isSolved() {
    for (int k = 0; k < 4; ++k) {
        if (st_->forms[static_cast<size_t>(k)].form == -1) continue;
        for (int d = 0; d < 4; ++d)
            if (energySatisfied(*st_, k, d) == 0) return false;
    }
    return true;
}

// 33cd:0c71 PzEnergy_SelectEraser
void PzEnergy::selectEraser(GgGame& g) {
    g.sound.pzPlay(0x16, 3);
    g.video.cursor().setCursorPicture(eraserCursor_.get());
    drawPencilCount(g.video);
    st_->mode = 1;
}

// 33cd:0c97 PzEnergy_EraseLines
void PzEnergy::eraseLines(GgGame& g) {
    int lastX = kNoPosition, lastY = kNoPosition;
    while (g.mouse().buttons() != 0) {
        if (g.mouse().x() != lastX || g.mouse().y() != lastY) {
            const int px = g.mouse().x() + eraserCursor_->hotX, py = g.mouse().y() + eraserCursor_->hotY;
            for (int k = 0; k < 4; ++k)
                for (int d = 0; d < 4; ++d) {
                    if (energyLine(*st_, k, d) == 0 ||
                        !pointOnLine(lineFormX(), lineY(k), lineDeviceX(), lineY(d), px, py))
                        continue;
                    energySatisfied(*st_, k, d) = uses(d, st_->forms[static_cast<size_t>(k)].form) ? 0 : 1;
                    energyLine(*st_, k, d) = 0;
                    energySent(*st_, d, k) = 0;
                    ++st_->linesLeft;
                    drawPencilCount(g.video);
                    drawLines(g);
                    g.sound.pzPlay(0x0B, 3);
                }
            lastX = g.mouse().x();
            lastY = g.mouse().y();
        }
        g.rt.idle();  // the loop polls nothing but the button word
    }
}

// 33cd:0db4 PzEnergy_SelectPencil
void PzEnergy::selectPencil(GgGame& g) {
    if (st_->linesLeft <= 0) return;
    g.video.cursor().setCursorPicture(pencilCursor_.get());
    g.sound.pzPlay(0x16, 3);
    st_->mode = 0;
}

// 33cd:0dde PzEnergy_AddZones
void PzEnergy::addZones(GgGame& g) {
    eraserZone_ = zoneFromDs(exe_, kEraserZone);
    pencilZone_ = zoneFromDs(exe_, kPencilZone);
    hintZone_ = zoneFromDs(exe_, kHintZone);
    checkZone_ = zoneFromDs(exe_, kCheckZone);
    workZone_ = zoneFromDs(exe_, dg::kEnergyWorkZone);
    g.zones().addLocal(eraserZone_);
    g.zones().addLocal(pencilZone_);
    g.zones().addLocal(hintZone_);
    g.zones().addLocal(checkZone_);
    g.zones().addLocal(workZone_);
}

// 33cd:0e0c PzEnergy_DrawLine
void PzEnergy::drawLine(GgGame& g, int x1, int y1, int x2, int y2, int colour) {
    Screen& screen = g.video.screen();
    if (x2 < x1) {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }
    const int dx = x2 - x1;
    int sy = 1, dy = y2 - y1;
    if (y1 >= y2) {
        sy = -1;
        dy = y1 - y2;
    }
    int e = 2 * dy - dx;
    screen.setDrawColor(static_cast<uint8_t>(colour));
    if (dx == 0 && dy != 0) {
        screen.vLine(x1, sy == 1 ? y1 : y2, dy + 1);
        return;
    }
    int x = x1 - 1, y = y1;
    for (int i = 0; i < dx; ++i) {  // the column x2 is never drawn
        ++x;
        screen.putPixel(x, y, static_cast<uint8_t>(colour));
        for (; e >= 0; e -= 2 * dx) {
            y += sy;
            screen.putPixel(x, y, static_cast<uint8_t>(colour));
        }
        e += 2 * dy;
    }
}

// 33cd:0ee7 PzEnergy_AnimatePulse
void PzEnergy::animatePulse(GgGame& g, int x1, int y1, int x2, int y2, bool erase) {
    int sy = 1, dy = y2 - y1;
    if (y1 >= y2) {
        sy = -1;
        dy = y1 - y2;
    }
    int sx = 1, dx = x2 - x1;
    if (x1 >= x2) {
        sx = -1;
        dx = x1 - x2;
    }
    int e = 2 * dy - dx;
    int x = x1 - sx, y = y1;
    int frame = 3;
    g.timers().set(7, 5);
    const int effect = sx < 1 ? 3 : 2;  // to the right 2, to the left 3
    g.sound.pzPlay(effect, 3);
    for (int i = 0; i < dx; ++i) {
        x += sx;
        if (frame-- == 0) {  // a frame every 4th x step
            g.rt.timerWait(7);
            drawLines(g);
            if (erase) drawLine(g, x2, y2, x, y, 0);  // what is left of the line
            drawPulse(g, x, y);
            g.timers().set(7, 5);
            frame = 3;
        }
        for (; e >= 0; e -= 2 * dx) y += sy;
        e += 2 * dy;
    }
    drawLines(g);
    g.sound.pzStop(effect);
}

// 33cd:0ff7 PzEnergy_DrawPulse
void PzEnergy::drawPulse(GgGame& g, int x, int y) {
    Screen& screen = g.video.screen();
    screen.setDrawColor(kPulseColour);
    screen.hLine(x - 1, y - 2, 3);
    screen.hLine(x - 2, y - 1, 2);
    screen.hLine(x + 1, y - 1, 2);
    screen.hLine(x - 2, y, 1);
    screen.hLine(x + 2, y, 1);
    screen.hLine(x - 2, y + 1, 2);
    screen.hLine(x + 1, y + 1, 2);
    screen.hLine(x - 1, y + 2, 3);
    g.display.flush();
}

// 33cd:10a9 PzEnergy_PointOnLine
bool PzEnergy::pointOnLine(int x1, int y1, int x2, int y2, int px, int py) {
    if (x2 < x1) {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }
    const int dx = x2 - x1;
    int sy = 1, dy = y2 - y1;
    if (y1 >= y2) {
        sy = -1;
        dy = y1 - y2;
    }
    int e = 2 * dy - dx;
    int x = x1 - 1, y = y1;
    for (int i = 0; i < dx; ++i) {  // never for a vertical line
        ++x;
        if (x == px && y == py) return true;
        for (; e >= 0; e -= 2 * dx) {
            y += sy;
            if (x == px && y == py) return true;
        }
        e += 2 * dy;
    }
    return false;
}

// 33cd:1189 PzEnergy_AddConnectorZones
void PzEnergy::addConnectorZones(GgGame& g) {
    for (int k = 0; k < 4; ++k) {
        if (st_->forms[static_cast<size_t>(k)].form == -1) continue;
        Zone& z = formZones_[static_cast<size_t>(k)];
        z = Zone::circle(static_cast<int16_t>(layout(5)), static_cast<int16_t>(layout(7 + k)), 7, 0,  // DS:28d2, DS:28d6[k]
                         static_cast<uint16_t>(kFormFirst + k), 0);
        z.h = 7;
        z.group = 0;
        g.zones().addLocal(z);
    }
    for (int d = 0; d < 4; ++d) {
        if (st_->devices[static_cast<size_t>(d)].id == -1) continue;
        Zone& z = deviceZones_[static_cast<size_t>(d)];
        z = Zone::circle(static_cast<int16_t>(layout(6)), static_cast<int16_t>(layout(7 + d)), 7, 0,  // DS:28d4, DS:28d6[d]
                         static_cast<uint16_t>(kDeviceFirst + d), 0);
        z.h = 7;
        z.group = 0;
        g.zones().addLocal(z);
    }
}

// 33cd:1dd9 PzEnergy_Cleanup
void PzEnergy::cleanup(GgGame& g) {
    g.zones().clearLocal();
    g.video.cursor().clearRegions();
    g.video.cursor().setCursorPicture(g.video.cursor().arrow());
    puzzleFreePanel(g);
}

// ---- starting from a DOSBox-X state (puzzles.h)

bool PzEnergy::restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) {
    // 33cd:004a / 0050 the main loop's poll (Input_GetCode, Input_StripHeld) and 0059 its idle animation; the eraser (009e), the two drawing
    // loops (00b3, 00c9), the check (011e), the success (0137, 013f), the hint text (018f), Go Back
    // (01a3) and Save & Exit (01bf).
    static constexpr uint16_t kMain[] = {0x004F, 0x0055, 0x005C};
    static constexpr uint16_t kSub[] = {0x00A1, 0x00B6, 0x00CC, 0x0121, 0x013C, 0x0144, 0x0194, 0x01A8, 0x01C4};
    if (!stateAtMainLoop(s, {kMain, kSub, 0x004A, 0x01CA}, why)) return false;
    game_ = &g;
    bind(g.puzzles.energy);
    const int variant = g.puzzles.variant;
    variant_ = variant;
    v_ = readVariant(exe_, variant);
    const auto idle = st_->idleFrame;  // the resources' load sets 0: keep the state's
    loadResources();
    st_->idleFrame = idle;
    buildHint(variant);
    g.puzzlesObjects().panel.load(g.archive());
    // The puzzle's effect bank, as the state's RAM holds it (DS:b1b0, the slots DS:b146): without it the
    // slots keep their start-up handles and PzSnd_WaitEnd after a solve waits on the room's music.
    g.sound.pzLoadBank(kEffectBank);
    eraserZone_ = zoneFromDs(exe_, kEraserZone);
    pencilZone_ = zoneFromDs(exe_, kPencilZone);
    hintZone_ = zoneFromDs(exe_, kHintZone);
    checkZone_ = zoneFromDs(exe_, kCheckZone);
    workZone_ = zoneFromDs(exe_, dg::kEnergyWorkZone);
    std::vector<std::pair<uint16_t, Zone*>> zones = {{kEraserZone, &eraserZone_},
                                                     {kPencilZone, &pencilZone_},
                                                     {kHintZone, &hintZone_},
                                                     {kCheckZone, &checkZone_},
                                                     {dg::kEnergyWorkZone, &workZone_}};
    for (int i = 0; i < 4; ++i) {
        zones.emplace_back(static_cast<uint16_t>(dg::kEnergyDeviceZones + 0x16 * i), &deviceZones_[static_cast<size_t>(i)]);
        zones.emplace_back(static_cast<uint16_t>(dg::kEnergyFormZones + 0x16 * i), &formZones_[static_cast<size_t>(i)]);
    }
    if (!relinkStateZones(g, s, zones, why)) return false;
    if (!stateCursorRegions(s).empty() && stateCursorRegions(s).front() != 0x0A54) {
        why = "a cursor region the puzzle does not have";
        return false;
    }
    g.video.cursor().clearRegions();
    // The default region's cursor: the pencil DS:af3c, the eraser DS:af40 or the hand DS:af44.
    if (stateSamePointer(s, 0x0A6C, 0xAF3C)) restoreCursorPicture(g, s, pencilCursor_.get());
    else if (stateSamePointer(s, 0x0A6C, 0xAF40)) restoreCursorPicture(g, s, eraserCursor_.get());
    else if (stateSamePointer(s, 0x0A6C, 0xAF44)) restoreCursorPicture(g, s, arrow_.get());
    else {
        why = "the cursor is neither the pencil, the eraser nor the hand";
        return false;
    }
    return true;
}

int PzEnergy::resume(GgGame& g) {
    bool solved = false;
    for (const Zone* z = g.zones().local(); z; z = z->next)
        if (z == &g.puzzlesObjects().panelZone) solved = true;
    return loop(g, solved);
}

}  // namespace tmport::gg
