#include "game/room/player.h"

#include <cstdlib>

#include "game/rules.h"

namespace tmport::game::room {

namespace {

// Sound effects of the controller (SSND 7000 + n; code_10e0-10f0.md §7).
constexpr int kSfxCoinLands = 0;
constexpr int kSfxNetSwish2 = 6;
constexpr int kSfxGateLevel0 = 17, kSfxGateLevel1 = 15, kSfxGateLevel2 = 16;
constexpr int kSfxJump12 = 23, kSfxJump14 = 35, kSfxJump22 = 24;  // only 35 is reachable
constexpr int kSfxNetWhoosh = 30;
constexpr int kSfxCoinToss = 34;
constexpr int kSfxBump = 40;

constexpr uint8_t kEndOfJump = 0xFF;

int16_t i16(int v) { return static_cast<int16_t>(v); }

}  // namespace

int coinTargetSlot(int x, bool facingLeft, const RoomTables& t) {
    const int probe = facingLeft ? x - t.dxLeft[kCoinLandFrame] : x + t.dxRight[kCoinLandFrame];
    int slot = probe / kSlotWidth;  // C division truncates toward zero
    if (slot < 0) slot = 0;
    else if (slot > 2) slot = 2;
    return slot;
}

// 10e0:1033 Player_WalkTo
void PlayerController::walkTo(int x, int y, uint8_t arg3, uint8_t arriveAction) {
    p_.walkActive = 1;
    p_.walkTargetX = i16(x);
    p_.walkTargetY = i16(y);
    p_.walkArg3 = arg3;
    p_.walkArriveAction = arriveAction;
}

void PlayerController::moveFrame(bool left) {
    PlayerState& pl = s_.player;
    if (left)
        pl.x = i16(pl.x - dxL(pl.frame));
    else
        pl.x = i16(pl.x + dxR(pl.frame));
    pl.y = fy(pl.frame);
}

// 10e0:0447, case NetSwing (g_netStep 1..11)
void PlayerController::netSwing() {
    PlayerState& pl = s_.player;
    pl.frame = rt_.netSwing[p_.netStep];
    if (!pl.facingLeft) {
        pl.x = i16(pl.x + dxR(pl.frame));
        if (p_.netStep == 1) p_.netX = i16(pl.x + kNetReachRight);
    } else {
        pl.x = i16(pl.x - dxL(pl.frame));
        if (p_.netStep == 1) p_.netX = i16(pl.x + kNetReachLeft);
    }
    pl.y = fy(pl.frame);
    if (p_.netStep == 3)
        hooks_.playSfx(kSfxNetWhoosh);
    else if (p_.netStep == 6)
        hooks_.playSfx(kSfxNetSwish2);
    ++p_.netStep;
    if (p_.netStep > 11) p_.netStep = 0;
}

// Empty-handed swing (g_emptySwingStep 1..11): same movement rule, no sounds.
void PlayerController::emptySwing() {
    PlayerState& pl = s_.player;
    pl.frame = rt_.emptySwing[p_.emptySwingStep];
    pl.x = i16(pl.x + (pl.facingLeft ? -dxL(pl.frame) : dxR(pl.frame)));
    pl.y = fy(pl.frame);
    ++p_.emptySwingStep;
    if (p_.emptySwingStep > 11) p_.emptySwingStep = 0;
}

// Coin toss (g_coinStep 1..13). The target slot is fixed at step 1; step 13 resolves the content.
void PlayerController::coinToss(int slot) {
    PlayerState& pl = s_.player;
    pl.frame = rt_.coinToss[p_.coinStep];
    p_.fxFrame = rt_.coinFx[p_.coinStep];
    if (!pl.facingLeft) {
        pl.x = i16(pl.x + dxR(pl.frame));
        if (p_.fxFrame) p_.fxX = i16(dxR(p_.fxFrame) + pl.x);
    } else {
        pl.x = i16(pl.x - dxL(pl.frame));
        if (p_.fxFrame) p_.fxX = i16(pl.x - dxL(p_.fxFrame));
    }
    pl.y = fy(pl.frame);
    if (p_.fxFrame) {
        p_.fxY = fy(p_.fxFrame);
        p_.fxW = sizes_.width(p_.fxFrame);
        p_.fxH = sizes_.height(p_.fxFrame);
    }
    switch (p_.coinStep) {
        case 1: p_.coinTargetSlot = static_cast<uint8_t>(slot); break;
        case 4: hooks_.playSfx(kSfxCoinToss); break;
        case 7: hooks_.playSfx(kSfxCoinLands); break;
        case 8: hooks_.playSfx(kSfxNetSwish2); break;
        case 13: {
            const TossOutcome o = resolveCoinToss(s_, p_.coinTargetSlot, t_, hooks_);
            if (o.result == TossResult::ShopOpens) {
                walkTo(o.walkToX, kWalkY, 1, 2);
                p_.inputLocked = 1;
                pl.frame = kStandFrame;
                // shop.state = 1 (net-shop door opens) was set by resolveCoinToss.
            }
            break;
        }
        default: break;
    }
    ++p_.coinStep;
    if (p_.coinStep > 13) {
        p_.coinStep = 0;
        p_.coinTargetSlot = 0xFF;
    }
}

// Auto-walk (g_walkActive): 5 px in x and 3 px in y per tick toward the target, walking frames;
// the dx and frameY tables are not applied. Arrival starts the door step-up.
void PlayerController::walkStep() {
    PlayerState& pl = s_.player;
    if (pl.x == p_.walkTargetX && pl.y == p_.walkTargetY) {
        p_.doorStep = 1;
        if (p_.walkArriveAction == 1 && p_.walkActive == 1) {
            scene_.gateUnlocking = 1;
            const uint8_t level = s_.rooms.level(pl.room);
            if (level == 0) hooks_.playSfx(kSfxGateLevel0);
            else if (level == 1) hooks_.playSfx(kSfxGateLevel1);
            else if (level == 2) hooks_.playSfx(kSfxGateLevel2);
        }
        return;
    }
    pl.frame = next(pl.frame);
    pl.facingLeft = p_.walkTargetX < pl.x ? 1 : 0;
    if (pl.x != p_.walkTargetX) {
        const int d = std::abs(pl.x - p_.walkTargetX);
        const int step = d < kWalkStepX ? d : kWalkStepX;
        pl.x = i16(pl.x + (pl.x < p_.walkTargetX ? step : -step));
    }
    if (pl.y != p_.walkTargetY) {
        const int d = std::abs(pl.y - p_.walkTargetY);
        const int step = d < kWalkStepY ? d : kWalkStepY;
        pl.y = i16(pl.y + (pl.y < p_.walkTargetY ? step : -step));
    }
}

// 10e0:0da7: walls, the room-36 fountain, room exits (after free movement only).
void PlayerController::boundsAndExits() {
    PlayerState& pl = s_.player;
    const RoomDef& r = s_.rooms[pl.room];
    if (r.exitLeft == 0xFF && pl.x < kWallLeftX + 1) pl.x = kWallLeftX;
    if (r.exitRight == 0xFF && pl.x + sizes_.width(pl.frame) > kWallRightX - 1)
        pl.x = i16(kWallRightX - sizes_.width(pl.frame));
    if (pl.room == kFountainRoom && pl.x + kObstacleFootW > kObstacleLeft && pl.x < kObstacleRight) {
        if (!p_.jumping) {
            pl.x = pl.facingLeft ? kObstacleBounceLeft : kObstacleBounceRight;
            p_.bumpStep = 1;
        } else if (pl.frame > 14) {
            const size_t k = static_cast<size_t>(pl.frame - 15);
            const bool tooLow = k < 7 && ((!pl.facingLeft && pl.x <= rt_.obstacleMinXRight[k]) ||
                                          (pl.facingLeft && rt_.obstacleMaxXLeft[k] <= pl.x));
            if (tooLow) {
                pl.x = pl.facingLeft ? kObstacleBounceLeft : kObstacleBounceRight;
                p_.bumpStep = 1;
                p_.jumping = 0;
            }
        }
    }
    if (pl.x < kExitLeftX) {
        p_.nextRoom = r.exitLeft;
        p_.roomTransition = r.typeLeft;
        if (p_.roomTransition != 1 && p_.roomTransition != 2) {
            p_.entryX = i16(pl.x + 512);
            p_.entryY = pl.y;
            p_.entryFrame = pl.frame;
            p_.entryFacing = pl.facingLeft;
        }
    } else if (pl.x > kExitRightX) {
        p_.nextRoom = r.exitRight;
        p_.roomTransition = r.typeRight;
        if (p_.roomTransition != 1 && p_.roomTransition != 2) {
            p_.entryX = i16(pl.x - 512);
            p_.entryY = pl.y;
            p_.entryFrame = pl.frame;
            p_.entryFacing = pl.facingLeft;
        }
    }
}

// 10e0:0f55: every path ends here.
void PlayerController::tail() {
    PlayerState& pl = s_.player;
    if (p_.nextRoom != pl.room && (p_.roomTransition == 1 || p_.roomTransition == 2) && RoomTable::valid(p_.nextRoom)) {
        const RoomDef& n = s_.rooms[p_.nextRoom];
        p_.entryX = n.startX;
        p_.entryY = n.startY;
        p_.entryFrame = n.startFrame;
        p_.entryFacing = n.startFacingLeft;
    }
    pl.w = sizes_.width(pl.frame);
    pl.h = sizes_.height(pl.frame);
}

// 10e0:0b47..0bf6 (the walk/jump cases) and 10e0:0c22..0d9d (no horizontal key).
void PlayerController::freeMovement(bool left, bool right, bool up, int slot, PlayerInput& in) {
    PlayerState& pl = s_.player;
    if (left || right) {
        pl.facingLeft = left ? 1 : 0;
        if (up && !p_.jumping) {
            p_.jumping = 1;
            pl.frame = kJumpFirst;
        } else {
            pl.frame = next(pl.frame);
            switch (pl.frame) {  // CS switch tables 10e0:0fd4 / 0fe4
                case 12: hooks_.playSfx(kSfxJump12); break;
                case 14: hooks_.playSfx(kSfxJump14); break;
                case 22: hooks_.playSfx(kSfxJump22); break;
                case kEndOfJump:
                    pl.frame = 1;
                    p_.jumping = 0;
                    break;
                default: break;
            }
        }
        moveFrame(left);
        boundsAndExits();
        return;
    }
    if (p_.jumping) {
        pl.frame = next(pl.frame);
        if (pl.frame < kJumpFirst || pl.frame > kJumpLast) {
            pl.frame = 1;
            p_.jumping = 0;
        } else if (pl.frame == 12) {
            hooks_.playSfx(kSfxJump12);
        } else if (pl.frame == 14) {
            hooks_.playSfx(kSfxJump14);
        } else if (pl.frame == 22) {
            hooks_.playSfx(kSfxJump22);
        }
        moveFrame(pl.facingLeft != 0);
    } else if (up) {
        const RoomDef& r = s_.rooms[pl.room];
        if (pl.x < r.doorX && r.doorX < pl.x + pl.w && (r.typeDoor == 1 || r.typeDoor == 2)) {
            p_.doorStep = 1;
        } else if (s_.rooms.spotContent(pl.room, slot) == kSpotGate) {
            const GateOutcome g = tryGate(s_, t_);  // without the key: hint 8
            if (g.opens) {
                p_.inputLocked = 1;
                pl.frame = kStandFrame;
                p_.gateWalkStarted = 1;
                if (g.walkToX >= 0) walkTo(g.walkToX, kWalkY, 0, 1);
            }
        }
    } else if (!in.downHeld() && pl.frame != kFrontFrame) {
        // Holding Down (or the front-view frame 60) keeps the frame.
        pl.frame = kStandFrame;
        pl.y = rt_.frameY[0];
    }
    boundsAndExits();
}

// 10e0:0447 Player_Update
void PlayerController::update(PlayerInput& in) {
    PlayerState& pl = s_.player;

    // 4.3.1 intent: mouse-directed walking (hot spot 0x600, latched while the button is held).
    if (p_.inputEvent == 0) {
        if (p_.mouseWalkLatched == 0 || !in.mouseButtonDown())
            p_.inputEvent = in.pollMouseDown();
        else
            p_.inputEvent = kHotPlayField;
    } else {
        p_.mouseWalkLatched = 0;
    }
    bool left = in.leftHeld();
    bool right = in.rightHeld();
    bool up = in.upHeld();
    const int slot = coinTargetSlot(pl.x, pl.facingLeft != 0, rt_);
    const uint16_t spot = p_.inputEvent & 0xFFF;
    if (spot == kHotLeftEdge || spot == kHotRightEdge || spot == kHotPlayField) {
        const int mx = in.mouseX(), my = in.mouseY();
        if (mx < pl.x)
            left = true;
        else if (pl.x + pl.w < mx)
            right = true;
        if (my < pl.y) up = true;
        p_.mouseWalkLatched = 1;
    }

    // 4.3.2 exclusive states, in this order.
    if (p_.netStep != 0) {
        netSwing();
    } else if (p_.emptySwingStep != 0) {
        emptySwing();
    } else if (p_.coinStep != 0) {
        coinToss(slot);
    } else if (p_.doorStep != 0) {
        pl.frame = rt_.door[p_.doorStep];
        pl.y = i16(pl.y - 5);
        ++p_.doorStep;
        if (p_.doorStep > 2) {
            p_.doorStep = 0;
            if (!p_.walkActive) {
                const RoomDef& r = s_.rooms[pl.room];
                p_.nextRoom = r.exitDoor;
                p_.roomTransition = r.typeDoor;
            } else {
                p_.arriveAction = p_.walkArriveAction;
                p_.walkActive = 0;
                if (stampGateLock_) stampGateLock_();  // GateLock_Draw(0x180)
            }
        }
    } else if (p_.walkActive) {
        walkStep();
    } else if (p_.arriveAction != 0) {
        if (p_.arriveAction == 1) {
            for (const GateWalk& g : kGateWalks)
                if (g.room == pl.room) p_.nextRoom = static_cast<uint8_t>(g.toRoom);
            p_.roomTransition = 1;
        } else if (p_.arriveAction == 2) {
            // 10f8:0241 NetShop_OnNetTaken
            p_.playerHidden = 0;
            pl.frame = kFrontFrame;
            pl.w = sizes_.width(kFrontFrame);
            pl.h = sizes_.height(kFrontFrame);
            p_.inputLocked = 0;
            netShopTakeNet(s_, hooks_);  // state 3, 4 coins left, +10 nets
        }
        p_.arriveAction = 0;
    } else if (p_.bumpStep != 0) {
        pl.frame = rt_.bump[p_.bumpStep];
        pl.x = i16(pl.x + (pl.facingLeft ? -rt_.bumpDxLeft[p_.bumpStep] : rt_.bumpDxRight[p_.bumpStep]));
        pl.y = fy(pl.frame);
        if (p_.bumpStep == 6) hooks_.playSfx(kSfxBump);
        ++p_.bumpStep;
        if (p_.bumpStep > 12) p_.bumpStep = 0;
    } else if (p_.playerHidden) {
        // nothing
    } else if (p_.inputEvent == 0x20) {
        if (pressNetKey(s_, t_, hooks_))
            p_.netStep = 1;
        else
            p_.emptySwingStep = 1;
    } else if (p_.inputEvent == 0x0D) {
        if (pressCoinKey(s_, t_, hooks_)) p_.coinStep = 1;
    } else if (p_.inputLocked) {
        // nothing
    } else {
        freeMovement(left, right, up, slot, in);
    }
    tail();
}

}  // namespace tmport::game::room
