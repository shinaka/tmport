#include "game/scenes/castle_climb.h"

#include <algorithm>
#include <format>

#include "audio/music_manager.h"
#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "game/rules.h"
#include "input/input_codes.h"
#include "mischief/util/error.h"
#include "mischief/util/log.h"
#include "platform/engine.h"
#include "res/resource_manager.h"

namespace tmport::game::scenes {

using namespace castle;

// ------------------------------------------------------------------------------------------ tables

CastleTables CastleTables::fromExe(const ExeTables& exe) {
    CastleTables t;
    for (size_t k = 0; k < 4; ++k)
        for (size_t s = 0; s < kScreens; ++s)
            for (size_t c = 0; c < kColumns; ++c)
                t.layouts[k][s][c] = exe.i16(dg::kClimbLayouts + static_cast<uint32_t>(2 * ((k * kScreens + s) * kColumns + c)));
    t.colX = exe.array<int16_t, kColumns>(dg::kClimbColX);
    for (uint32_t i = 0; i < 4; ++i) t.climbFrames[i] = exe.u8(dg::kClimbFrames + 2 * i);
    t.climbDx = exe.array<int16_t, 4>(dg::kClimbDx);
    t.climbUpDy = exe.array<int16_t, 4>(dg::kClimbUpDy);
    t.climbDownDy = exe.array<int16_t, 4>(dg::kClimbDownDy);
    for (uint32_t i = 0; i < 5; ++i) t.jumpFrames[i] = exe.u8(dg::kClimbJumpFrames + 2 * i);
    t.jumpDxLeft = exe.array<int16_t, 4>(dg::kClimbJumpDxLeft);
    t.jumpDxRight = exe.array<int16_t, 5>(dg::kClimbJumpDxRight);
    t.jumpDy = exe.array<int16_t, 5>(dg::kClimbJumpDy);
    // 1-based tables: the code adds 2*step to a base two bytes before element 1.
    for (uint32_t s = 1; s <= 7; ++s) {
        t.fallFrames[s] = exe.u8(dg::kClimbFallFrames - 2 + 2 * s);
        t.fallDx[s] = exe.i16(dg::kClimbFallDx - 2 + 2 * s);
        t.fallDy[s] = exe.i16(dg::kClimbFallDy - 2 + 2 * s);
    }
    for (uint32_t s = 1; s <= 6; ++s) {
        t.landFrames[s] = exe.u8(dg::kClimbLandFrames - 2 + 2 * s);
        t.landDx[s] = exe.i16(dg::kClimbLandDx - 2 + 2 * s);
        t.landY[s] = exe.i16(dg::kClimbLandY - 2 + 2 * s);
    }
    for (uint32_t k = 1; k <= 12; ++k) t.creatSeq[k] = exe.i16(dg::kClimbCreatSeq - 2 + 2 * k);
    t.spawnPermille = exe.array<int16_t, 4>(dg::kClimbCreatSpawnPermille);
    t.nextFrame = exe.array<uint8_t, 85>(dg::kNextFrame);
    t.dxRight = exe.array<int16_t, 85>(dg::kFrameDxRight);
    t.dxLeft = exe.array<int16_t, 85>(dg::kFrameDxLeft);
    t.frameY = exe.array<int16_t, 85>(dg::kFrameY);
    t.netFrames = exe.array<uint8_t, 12>(dg::kNetSwingFrames);
    t.emptySwingFrames = exe.array<uint8_t, 12>(dg::kEmptySwingFrames);
    t.coinFrames = exe.array<uint8_t, 14>(dg::kCoinTossFrames);
    t.coinFxFrames = exe.array<uint8_t, 14>(dg::kCoinFxFrames);
    return t;
}

namespace {

template <size_t N>
void fillSizes(const ResourceManager& res, int bndl, std::array<int16_t, N>& w, std::array<int16_t, N>& h) {
    const auto b = res.bundle(bndl);
    if (!b) throw mischief::Error(std::format("BNDL {} is missing from the game files", bndl));
    for (size_t i = 0; i < b->ids.size() && i < N; ++i) {
        const auto pm = res.pixmap(b->ids[i]);
        if (!pm) continue;
        w[i] = static_cast<int16_t>(pm->width);
        h[i] = static_cast<int16_t>(pm->height);
    }
}

}  // namespace

CastleSizes CastleSizes::fromResources(const ResourceManager& res) {
    CastleSizes z;
    fillSizes(res, kPlayerBndl, z.playerW, z.playerH);
    fillSizes(res, kClimbBndl, z.climbW, z.climbH);
    fillSizes(res, kCreatBndl, z.creatW, z.creatH);
    return z;
}

// ------------------------------------------------------------------------------------------- logic

CastleClimb::CastleClimb(GameState& s, const GameTables& gt, const CastleTables& t, const CastleSizes& z,
                         GameHooks& hooks, mischief::BorlandRand& rng, CastleOptions options)
    : s_(s), gt_(gt), t_(t), z_(z), hooks_(hooks), rng_(rng), options_(options) {}

int CastleClimb::cell(int scr, int col) const {
    if (layout < 0 || layout > 3 || scr < 0 || scr >= kScreens || col < 0 || col >= kColumns) return 0;
    return t_.layouts[static_cast<size_t>(layout)][static_cast<size_t>(scr)][static_cast<size_t>(col)];
}

void CastleClimb::pickLayout() { layout = pickCastleLayout(s_.rank.rankTier, rng_); }

bool CastleClimb::findLadderAtPlayer() {
    const PlayerState& p = s_.player;
    for (int c = 0; c < kColumns; ++c) {
        const int cx = t_.colX[static_cast<size_t>(c)];
        if (p.x < cx && cx < p.x + p.w) {
            const int k = cell(screen, c);
            if (k == 1 || (k == 2 && p.y > kHalfLadderMinY)) {
                column = c;
                return true;
            }
        }
    }
    return false;
}

void CastleClimb::startWalk() {
    PlayerState& p = s_.player;
    p.frame = 0;
    p.y = t_.frameY[0];
    p.facingLeft = 0;
    p.w = z_.playerW[0];
    p.h = z_.playerH[0];
    done = false;
    onLadder = false;
    fxFrame = 0;
    jumping = 0;
    coinStep = 0;
    emptySwingStep = 0;
    netStep = 0;
}

// Steps 1-2 of the input decode (latch, Hotspot_PollMouseDown) and the mouse position.
uint16_t CastleClimb::resolveInput(uint16_t input, const TickSense& sense, bool& follow, int& mx, int& my) const {
    if (input == 0) {
        if (follow && sense.mouseDown)
            input = kSteer;
        else
            input = sense.pollMouseDown ? sense.pollMouseDown() : 0;
    } else {
        follow = false;
    }
    mx = sense.mouseX;
    my = sense.mouseY;
    if (sense.readMouse) sense.readMouse(mx, my);
    return input;
}

void CastleClimb::walkUpdate(uint16_t input, const TickSense& sense) {
    PlayerState& p = s_.player;
    int mx = 0, my = 0;
    input = resolveInput(input, sense, walkMouseFollow, mx, my);
    bool L = sense.left, R = sense.right, U = sense.up, D = sense.down;
    const uint16_t h = input & 0x0FFF;
    auto upDown = [&] {
        if (my < p.y)
            U = true;
        else if (p.y + p.h < my)
            D = true;
    };
    if (h == kLeftEdge) {
        L = true;
        upDown();
    } else if (h == kRightEdge) {
        R = true;
        upDown();
    } else if (h == kSteer) {
        if (mx < p.x)
            L = true;
        else if (p.x + p.w < mx)
            R = true;
        upDown();
        walkMouseFollow = true;
    }

    auto dxFacing = [&](int frame) {
        return p.facingLeft ? -t_.dxLeft[static_cast<size_t>(frame)] : t_.dxRight[static_cast<size_t>(frame)];
    };
    if (netStep != 0) {
        p.frame = t_.netFrames[static_cast<size_t>(netStep)];
        p.x = static_cast<int16_t>(p.x + dxFacing(p.frame));
        p.y = t_.frameY[p.frame];
        if (netStep == 3)
            hooks_.playSfx(kSfxNetWhoosh);
        else if (netStep == 6)
            hooks_.playSfx(kSfxNet);
        if (++netStep > 11) netStep = 0;
    } else if (emptySwingStep != 0) {
        p.frame = t_.emptySwingFrames[static_cast<size_t>(emptySwingStep)];
        p.x = static_cast<int16_t>(p.x + dxFacing(p.frame));
        p.y = t_.frameY[p.frame];
        if (++emptySwingStep > 11) emptySwingStep = 0;
    } else if (coinStep != 0) {
        p.frame = t_.coinFrames[static_cast<size_t>(coinStep)];
        fxFrame = t_.coinFxFrames[static_cast<size_t>(coinStep)];
        if (!p.facingLeft) {
            p.x = static_cast<int16_t>(p.x + t_.dxRight[p.frame]);
            if (fxFrame != 0) fxX = t_.dxRight[static_cast<size_t>(fxFrame)] + p.x;
        } else {
            p.x = static_cast<int16_t>(p.x - t_.dxLeft[p.frame]);
            if (fxFrame != 0) fxX = p.x - t_.dxLeft[static_cast<size_t>(fxFrame)];
        }
        p.y = t_.frameY[p.frame];
        if (fxFrame != 0) {
            fxY = t_.frameY[static_cast<size_t>(fxFrame)];
            fxW = z_.playerW[static_cast<size_t>(fxFrame)];
            fxH = z_.playerH[static_cast<size_t>(fxFrame)];
        }
        if (coinStep == 4)
            hooks_.playSfx(kSfxCoinToss);
        else if (coinStep == 7)
            hooks_.playSfx(kSfxCoin);
        else if (coinStep == 8)
            hooks_.playSfx(kSfxNet);
        if (++coinStep > 13) coinStep = 0;
    } else if (input == input::kSpace) {
        if (s_.inv.nets == 0) {
            emptySwingStep = 1;
            queueHint(s_, s_.inv.coins == 0 ? 7 : 5, gt_);
        } else {
            netStep = 1;
        }
        addNets(s_, -1, hooks_);
    } else if (input == input::kEnter) {
        if (s_.inv.coins == 0)
            queueHint(s_, s_.inv.nets == 0 ? 7 : 6, gt_);
        else
            coinStep = 1;
        addCoins(s_, -1, hooks_);
    } else if (L || R) {
        p.facingLeft = L ? 1 : 0;  // Left wins over Right
        if (!U || jumping) {
            p.frame = t_.nextFrame[p.frame];
            if (p.frame == 0xFF) {
                p.frame = 1;
                jumping = 0;
            }
        } else {
            jumping = 1;
            p.frame = 12;
        }
        if (L)
            p.x = static_cast<int16_t>(p.x - t_.dxLeft[p.frame]);
        else  // 1168:09e3: the left table (quirk, see CastleOptions)
            p.x = static_cast<int16_t>(p.x + (options_.rightWalkUsesDxLeft ? t_.dxLeft[p.frame] : t_.dxRight[p.frame]));
        p.y = t_.frameY[p.frame];
    } else if (U) {
        onLadder = findLadderAtPlayer();
    } else if (!D) {
        p.frame = 0;
        p.y = t_.frameY[0];
    }
    p.w = z_.playerW[p.frame];
    p.h = z_.playerH[p.frame];
    if (p.x < kWalkMinX)
        p.x = kWalkMinX - 1;
    else if (p.x + p.w > kWalkMaxRight)
        p.x = static_cast<int16_t>(kWalkMaxRight + 1 - p.w);
}

void CastleClimb::startClimb() {
    PlayerState& p = s_.player;
    p.frame = 0;
    p.x = static_cast<int16_t>(t_.colX[static_cast<size_t>(column)] + kClimbXOffset);
    p.y = kClimbStartY;
    p.facingLeft = 0;
    p.w = z_.climbW[0];
    p.h = z_.climbH[0];
    creatFrame = 0;
    creatTick = 0;
    landStep = 0;
    fallStep = 0;
    jumpStep = 0;
    climbPhase = 0;
    nextScreen = 0;
    screen = 0;
}

void CastleClimb::updateCreature() {
    if (creatTick == 0) {
        if (screen == 0) return;
        if (!creatureSpawnRoll(s_.rank.rankTier, gt_, rng_)) return;
        creatTick = 1;
        int c = 0;
        do c = rng_.random(kColumns);
        while (cell(screen, c) == 1);
        creatCol = c;
        const int range = cell(screen, c) == 0 ? kScreenH : 121;
        creatY = rng_.random(range - z_.creatH[0]) + 20;
        if (c == 0)
            creatFlip = 0;
        else if (c == kColumns - 1)
            creatFlip = 1;
        else
            creatFlip = rng_.random(2);
        creatScreen = screen;
    } else if (creatScreen != screen) {
        creatTick = 0;
        return;
    }
    if (creatTick > 12) {
        creatTick = 0;
        return;
    }
    creatFrame = t_.creatSeq[static_cast<size_t>(creatTick)];
    if (creatFrame >= 0) {
        int x = t_.colX[static_cast<size_t>(creatCol)];
        if (creatFlip) x += z_.creatW[0] - z_.creatW[static_cast<size_t>(creatFrame & 3)];
        creatX = x + kCreatXOffset;
        if (creatTick == 2)
            hooks_.playSfx(kSfxCreatEmerge);
        else if (creatTick == 3)
            hooks_.playSfx(kSfxCreatReach);
    }
    ++creatTick;
}

void CastleClimb::climbUpdate(uint16_t input, const TickSense& sense) {
    PlayerState& p = s_.player;
    int mx = 0, my = 0;
    input = resolveInput(input, sense, climbMouseFollow, mx, my);
    bool L = sense.left, R = sense.right, U = sense.up, D = sense.down;
    const uint16_t h = input & 0x0FFF;
    if (h == kLeftEdge || h == kRightEdge) {
        (h == kLeftEdge ? L : R) = true;
        if (my < p.y)
            U = true;
        else if (p.y + p.h < my)
            D = true;
    } else if (h == kSteer) {
        if (mx < p.x)
            L = true;
        else if (p.x + p.w < mx)
            R = true;
        if (my < p.y || my < 20)
            U = true;
        else if (p.y + p.h < my)
            D = true;
        climbMouseFollow = true;
    }

    auto climbStep = [&](bool up) {
        const size_t ph = static_cast<size_t>(climbPhase);
        p.frame = t_.climbFrames[ph];
        p.x = static_cast<int16_t>(t_.colX[static_cast<size_t>(column)] + t_.climbDx[ph] + kClimbXOffset);
        p.y = static_cast<int16_t>(up ? p.y - t_.climbUpDy[ph] : p.y + t_.climbDownDy[ph]);
        if (!findLadderAtPlayer()) fallStep = 1;
        if (climbPhase == 1)
            hooks_.playSfx(kSfxClimb1);
        else if (climbPhase == 3)
            hooks_.playSfx(kSfxClimb3);
        if (up)
            climbPhase = climbPhase + 1 > 3 ? 0 : climbPhase + 1;
        else
            climbPhase = climbPhase < 1 ? 3 : climbPhase - 1;
    };

    if (jumpStep != 0) {
        int i = jumpStep, dx = 0;
        if (!p.facingLeft) {
            p.frame = t_.jumpFrames[static_cast<size_t>(i)];
            dx = t_.jumpDxRight[static_cast<size_t>(i)];
        } else {
            i = 4 - jumpStep;
            p.frame = t_.jumpFrames[static_cast<size_t>(i)];
            dx = -t_.jumpDxLeft[static_cast<size_t>(i)];
        }
        p.x = static_cast<int16_t>(p.x + dx);
        p.y = static_cast<int16_t>(p.y + t_.jumpDy[static_cast<size_t>(i)]);
        if (++jumpStep > 4) {
            jumpStep = 0;
            climbPhase = -1;
            p.facingLeft = 0;
            if (!findLadderAtPlayer()) fallStep = 1;
        }
    } else if (fallStep != 0) {
        const size_t f = static_cast<size_t>(fallStep);
        p.frame = t_.fallFrames[f];
        p.x = static_cast<int16_t>(p.x + t_.fallDx[f]);
        p.y = static_cast<int16_t>(p.y + t_.fallDy[f] + 32);
        if (fallStep == 1) hooks_.playSfx(kSfxFall);
        if (!findLadderAtPlayer()) {
            if (screen == 0 && p.y > kLandY) {
                fallStep = 0;
                landStep = 1;
            } else if (++fallStep > 7) {
                fallStep = 1;
            }
        } else {
            fallStep = 0;
            climbPhase = -1;
        }
    } else if (landStep != 0) {
        const size_t l = static_cast<size_t>(landStep);
        p.frame = t_.landFrames[l];
        p.x = static_cast<int16_t>(p.x + t_.landDx[l]);
        p.y = t_.landY[l];
        if (landStep == 1) hooks_.playSfx(kSfxLand);
        if (++landStep > 6) {
            landStep = 0;
            onLadder = false;
        }
    } else {
        if (climbPhase < 0) {  // re-attach to the ladder
            climbPhase = 0;
            p.frame = t_.climbFrames[0];
            p.x = static_cast<int16_t>(t_.colX[static_cast<size_t>(column)] + kClimbXOffset);
            p.facingLeft = 0;
        }
        if (U) {
            climbStep(true);
        } else if (D) {
            climbStep(false);
        } else if (L) {
            jumpStep = 1;
            p.facingLeft = 1;
        } else if (R) {
            jumpStep = 1;
            p.facingLeft = 0;
        }
        // The creature's push (at full reach, same screen, from the adjacent column, facing us).
        if (creatFrame == 3 && creatScreen == screen && p.y < creatY + kCreatReach &&
            creatY + kCreatReach < p.y + kCreatHitH) {
            if (column - 1 == creatCol && creatFlip == 0) {
                jumpStep = 1;
                p.facingLeft = 0;
                addCoins(s_, -1, hooks_);
            } else if (column + 1 == creatCol && creatFlip != 0) {
                jumpStep = 1;
                p.facingLeft = 1;
                addCoins(s_, -1, hooks_);
            }
        }
        if (screen == 0 && p.y > kClimbStartY) onLadder = false;  // stepped off the bottom
    }
    const size_t fr = std::min<size_t>(p.frame, z_.climbW.size() - 1);
    p.w = z_.climbW[fr];
    p.h = z_.climbH[fr];
    if (p.y < kScreenUpY) {
        nextScreen = screen + 1;
        nextY = p.y + kScreenH;
    } else if (p.y > kScreenDownY) {
        nextScreen = screen - 1;
        nextY = p.y - kScreenH;
    }
}

bool CastleClimb::climbTick(uint16_t input, const TickSense& sense) {
    updateCreature();
    climbUpdate(input, sense);
    return onLadder && nextScreen == screen;
}

bool CastleClimb::finishScreenChange() {
    if (nextScreen == screen) return false;
    if (nextScreen < kScreens) {
        screen = nextScreen;
        s_.player.y = static_cast<int16_t>(nextY);
        return true;
    }
    done = true;
    return false;
}

// ------------------------------------------------------------------------------------------- scene

namespace {

std::vector<std::shared_ptr<const gfx::PixMap>> loadFrames(const ResourceManager& res, int bndl) {
    const auto b = res.bundle(bndl);
    if (!b) throw mischief::Error(std::format("BNDL {} is missing from the game files", bndl));
    std::vector<std::shared_ptr<const gfx::PixMap>> v;
    for (int id : b->ids) v.push_back(res.pixmap(id));
    return v;
}

class CastleScene {
public:
    CastleScene(AppContext& ctx, GameState& s, const GameTables& t, const SceneHooks& hooks, CastleOptions options)
        : io_(ctx, hooks),
          s_(s),
          tables_(CastleTables::fromExe(*ctx.files.exe)),
          sizes_(CastleSizes::fromResources(*ctx.files.res)),
          climb_(s, t, tables_, sizes_, io_.gameHooks(), io_.rng(), options) {}

    void run();

private:
    void loadResources();      // 1168:00ba
    void drawBackground();     // 1168:0299
    void walkLoop();           // 1168:0442
    void climbLoop();          // 1168:0a6d
    TickSense sense();
    uint16_t tickInput();      // Timer_Set(7,100); Input_GetCode; Esc -> MenuCmd_Exit
    void endTick();            // present if requested, flush dirty rects, wait for the timer
    void drawPixmap(const std::vector<std::shared_ptr<const gfx::PixMap>>& v, int i, int x, int y, bool mirror);

    SceneIo io_;
    GameState& s_;
    CastleTables tables_;
    CastleSizes sizes_;
    CastleClimb climb_;
    std::vector<std::shared_ptr<const gfx::PixMap>> player_, climber_, creature_;
    std::shared_ptr<const gfx::PixMap> ladderFull_, ladderLow_, ladderStub_;
    bool fullPresent_ = false, fadePending_ = false;
    int presentH_ = gfx::FrameBuffer::kPlayHeight;
};

void CastleScene::loadResources() {
    const ResourceManager& res = io_.res();
    player_ = loadFrames(res, kPlayerBndl);
    climber_ = loadFrames(res, kClimbBndl);
    creature_ = loadFrames(res, kCreatBndl);
    ladderFull_ = io_.pixmap(kLadderFull);
    ladderLow_ = io_.pixmap(kLadderLow);
    ladderStub_ = io_.pixmap(kLadderStub);
}

void CastleScene::drawPixmap(const std::vector<std::shared_ptr<const gfx::PixMap>>& v, int i, int x, int y, bool mirror) {
    if (i < 0 || i >= static_cast<int>(v.size()) || !v[static_cast<size_t>(i)]) return;
    io_.fb().drawSprite(x, y, *v[static_cast<size_t>(i)], mirror);
}

void CastleScene::drawBackground() {
    gfx::FrameBuffer& fb = io_.fb();
    const int scr = climb_.screen;
    fb.drawOpaqueOrFlippedWhiteKey(0, gfx::FrameBuffer::kPlayTop, *io_.pixmap(scr == 0 ? kBackground0 : kBackground), false);
    if (scr != 0) {
        for (int c = 0; c < kColumns; ++c) {
            const int x = tables_.colX[static_cast<size_t>(c)] + kLadderDx;
            const int k = climb_.cell(scr, c);
            if (k == 1)
                fb.drawSprite(x, kLadderFullY, *ladderFull_, false);
            else if (k == 2)
                fb.drawSprite(x, kLadderLowY, *ladderLow_, false);
            else if (climb_.cell(scr - 1, c) == 1)
                fb.drawSprite(x, kLadderStubY, *ladderStub_, false);
        }
    }
    fb.setForeColor(0);
    fb.hline(0, gfx::FrameBuffer::kPlayTop + gfx::FrameBuffer::kPlayHeight, 512);
    fb.copyRect(0, gfx::FrameBuffer::kPlayTop, 512, gfx::FrameBuffer::kPlayHeight, 0,
                gfx::FrameBuffer::kPlayTop + gfx::FrameBuffer::kLayer1Y);
}

TickSense CastleScene::sense() {
    InputQueue& in = io_.engine().input();
    TickSense t;
    t.left = in.leftHeld;
    t.right = in.rightHeld;
    t.up = in.upHeld;
    t.down = in.downHeld;
    t.mouseDown = in.mouseButtonDown();
    t.mouseX = in.mouseX();
    t.mouseY = in.mouseY();
    t.pollMouseDown = [&in] { return in.pollMouseDown(); };
    t.readMouse = [&in](int& x, int& y) {
        x = in.mouseX();
        y = in.mouseY();
    };
    return t;
}

uint16_t CastleScene::tickInput() {
    io_.timerSet(7, 100);
    const uint16_t c = io_.getCode();
    if (c == input::kEsc) io_.exitGame();
    gfx::FrameBuffer& fb = io_.fb();
    fb.setPlayClip(0);
    fb.setDirtyEnabled(true);
    fb.clearDirtyRects();
    return c;
}

void CastleScene::endTick() {
    if (fullPresent_) {
        io_.present(0, gfx::FrameBuffer::kPlayTop, 512, presentH_);
        fullPresent_ = false;
        fadePending_ = false;  // Fx_FadeIn_Stub: a stub
    }
    io_.engine().flushDirtyRects();
    while (!io_.timerExpired(7)) {
    }
}

void CastleScene::walkLoop() {
    HotspotTable& hs = io_.engine().hotspots();
    hs.set(kSteer, 0, 20, 512, 208);
    hs.set(kFloorClick, 0, 228, 512, 59);
    hs.set(kLeftEdge, 0, 20, 0, 208);
    hs.set(kRightEdge, 512, 20, 0, 208);
    climb_.startWalk();
    io_.trace("castle: walk");
    PlayerState& p = s_.player;
    gfx::FrameBuffer& fb = io_.fb();
    do {
        const uint16_t c = tickInput();
        if (climb_.fxFrame != 0) restoreFromLayer(fb, climb_.fxX, climb_.fxY, climb_.fxW, climb_.fxH, gfx::FrameBuffer::kLayer1Y);
        restoreFromLayer(fb, p.x, p.y, p.w, p.h, gfx::FrameBuffer::kLayer1Y);
        climb_.walkUpdate(c, sense());
        io_.tracef("castle: walk frame {} x {} y {}{}", p.frame, p.x, p.y, p.facingLeft ? " L" : "");
        drawPixmap(player_, p.frame, p.x, p.y, p.facingLeft != 0);
        if (climb_.fxFrame != 0) drawPixmap(player_, climb_.fxFrame, climb_.fxX, climb_.fxY, false);
        presentH_ = gfx::FrameBuffer::kPlayHeight + 1;
        endTick();
    } while (!climb_.onLadder);
    restoreFromLayer(fb, p.x, p.y, p.w, p.h, gfx::FrameBuffer::kLayer1Y);
    io_.engine().flushDirtyRects();
    hs.remove(kSteer);
    hs.remove(kFloorClick);
    hs.remove(kLeftEdge);
    hs.remove(kRightEdge);
}

void CastleScene::climbLoop() {
    HotspotTable& hs = io_.engine().hotspots();
    hs.set(kSteer, 0, 20, 512, 267);
    hs.set(kLeftEdge, 0, 20, 0, 267);
    hs.set(kRightEdge, 512, 20, 0, 267);
    climb_.startClimb();
    io_.tracef("castle: climb column {}", climb_.column);
    PlayerState& p = s_.player;
    gfx::FrameBuffer& fb = io_.fb();
    do {
        bool more = true;
        do {
            const uint16_t c = tickInput();
            // 1168:10d2 EraseCreature (skipped for frame -1: the original restores a stale strip).
            if (climb_.creatTick != 0 && climb_.creatFrame >= 0)
                restoreFromLayer(fb, climb_.creatX, climb_.creatY, sizes_.creatW[static_cast<size_t>(climb_.creatFrame & 3)],
                                 sizes_.creatH[static_cast<size_t>(climb_.creatFrame & 3)], gfx::FrameBuffer::kLayer1Y);
            restoreFromLayer(fb, p.x, p.y, p.w, p.h, gfx::FrameBuffer::kLayer1Y);
            const int tickBefore = climb_.creatTick;
            more = climb_.climbTick(c, sense());
            io_.tracef("castle: climb screen {} frame {} x {} y {}", climb_.screen, p.frame, p.x, p.y);
            if (climb_.creatTick == 2 && tickBefore == 0)
                io_.tracef("castle: creature screen {} column {} y {} flip {}", climb_.creatScreen, climb_.creatCol,
                           climb_.creatY, climb_.creatFlip);
            // 1168:1101 DrawCreature, 1168:10a7 DrawClimber (never mirrored).
            if (climb_.creatTick != 0 && climb_.creatFrame >= 0 && climb_.creatScreen == climb_.screen)
                drawPixmap(creature_, climb_.creatFrame, climb_.creatX, climb_.creatY, climb_.creatFlip != 0);
            drawPixmap(climber_, p.frame, p.x, p.y, false);
            presentH_ = gfx::FrameBuffer::kPlayHeight;
            endTick();
        } while (more);
        if (!climb_.onLadder) {
            restoreFromLayer(fb, p.x, p.y, p.w, p.h, gfx::FrameBuffer::kLayer1Y);
            io_.engine().flushDirtyRects();
        }
        if (climb_.finishScreenChange()) {
            io_.tracef("castle: screen {}", climb_.screen);
            fullPresent_ = true;
            drawBackground();
        }
    } while (climb_.onLadder && !climb_.done);
    hs.remove(kSteer);
    hs.remove(kLeftEdge);
    hs.remove(kRightEdge);
}

void CastleScene::run() {
    if (io_.musicZone() != kMusicZone) io_.musicPlayZone(kMusicZone);  // normally already started
    io_.fb().setPlayClip(0);
    climb_.creatTick = 0;
    climb_.screen = 0;
    climb_.done = false;
    loadResources();
    climb_.pickLayout();
    io_.tracef("castle: layout {}", climb_.layout);
    drawBackground();
    fadePending_ = true;
    fullPresent_ = true;
    while (!climb_.done) {
        walkLoop();
        climbLoop();
    }
    io_.trace("castle: done");
    s_.player.room = kRoomNewClimb;  // 1118: g_room := 0xFE after ClimbScene_Run
}

}  // namespace

void runCastleClimb(AppContext& ctx, GameState& s, const GameTables& t, const SceneHooks& hooks, CastleOptions options) {
    CastleScene scene(ctx, s, t, hooks, options);
    scene.run();
}

}  // namespace tmport::game::scenes
