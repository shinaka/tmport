#include "game/room/room_scene.h"

#include "anim/anim_bundle.h"
#include "game/room/room_gfx.h"
#include "game/room_table.h"
#include "res/resource_manager.h"
#include "util/log.h"

namespace tmport::game::room {

namespace {

std::vector<std::shared_ptr<const gfx::PixMap>> loadBundlePixmaps(const ResourceManager* res, int id) {
    std::vector<std::shared_ptr<const gfx::PixMap>> out;
    if (!res) return out;
    const auto b = res->bundle(id);
    if (!b) {
        log::warn("room scene: BNDL {} not found", id);
        return out;
    }
    for (const int m : b->ids) out.push_back(res->pixmap(m));
    return out;
}

const gfx::PixMap* at(const std::vector<std::shared_ptr<const gfx::PixMap>>& v, int i) {
    return i >= 0 && static_cast<size_t>(i) < v.size() ? v[static_cast<size_t>(i)].get() : nullptr;
}

const GateLockPos* gateLockFor(int room) {
    for (const GateLockPos& g : kGateLocks)
        if (g.room == room) return &g;
    return nullptr;
}

const NicheDef* nicheFor(int room) {
    for (const NicheDef& n : kNiches)
        if (n.room == room) return &n;
    return nullptr;
}

bool isShopRoom(int room) { return room == 13 || room == 24 || room == 36; }

}  // namespace

const gfx::PixMap* RoomScene::playerFrame(int f) const { return at(playerFrames_, f); }

const gfx::PixMap* RoomScene::bundleMember(int bndl, int index) {
    auto it = bundleCache_.find(bndl);
    if (it == bundleCache_.end()) it = bundleCache_.emplace(bndl, loadBundlePixmaps(d_.res, bndl)).first;
    return at(it->second, index);
}

const gfx::PixMap* RoomScene::levelTreasure(int n) {
    if (n < 0 || n >= d_.s.treasures.count || n >= kMaxTreasuresPerLevel) return nullptr;
    return bundleMember(kTreasureBndlFirstId + d_.s.treasures.index[static_cast<size_t>(n)], 0);
}

// ------------------------------------------------------------------------- climb resources

// 1198:0000 Scene_LoadCommon
void RoomScene::loadCommon() {
    if (playerFrames_.empty()) {
        playerFrames_ = loadBundlePixmaps(d_.res, kPlayerBndl);
        for (size_t i = 0; i < playerFrames_.size() && i < static_cast<size_t>(kPlayerFrameSlots); ++i) {
            const gfx::PixMap* pm = playerFrames_[i].get();
            sizes_.w[i] = static_cast<int16_t>(pm ? pm->width : 0);
            sizes_.h[i] = static_cast<int16_t>(pm ? pm->height : 0);
        }
    }
    if (d_.res) d_.hud.loadArt(*d_.res);
    if (signFrames_.empty()) signFrames_ = loadBundlePixmaps(d_.res, kShopSignBndl);
    // 1110:0000 CoinPile_Load
    if (d_.res) {
        coinPile_ = d_.res->pixmap(kCoinPilePmap);
        if (coinPile_) {
            d_.s.coinPile.w = static_cast<int16_t>(coinPile_->width);
            d_.s.coinPile.h = static_cast<int16_t>(coinPile_->height);
        }
    }
    lockFrames_ = loadBundlePixmaps(d_.res, kGateLockBndl);  // 1188:01ed GateLock_Load
    fountain_ = loadBundlePixmaps(d_.res, kFountainBndl);
}

// 1198:00b1 Scene_FreeCommon (the sign BNDL 302 is never freed; g_frameW/H keep their values)
void RoomScene::freeCommon() {
    playerFrames_.clear();
    d_.hud.freeArt();
    coinPile_.reset();
    lockFrames_.clear();
    fountain_.clear();
}

// ------------------------------------------------------------------------- per room

// 1100:0638 Spots_LoadRoomPictures
void RoomScene::loadRoomSlots() {
    const int room = d_.s.player.room;
    for (int i = 0; i < 3; ++i) {
        slotPics_[static_cast<size_t>(i)].reset();
        if (!RoomTable::valid(room) || !d_.res) continue;
        const uint8_t k = d_.s.rooms.spotKind(room, i);
        if (k < kSpotShop) slotPics_[static_cast<size_t>(i)] = d_.res->pixmap(kSpotPictureFirstId + k);
    }
}

// 1100:068a Spots_FreeRoomPictures
void RoomScene::freeRoomSlots() {
    for (auto& p : slotPics_) p.reset();
}

// 1190:009b TreasureHall_LoadRoom
void RoomScene::hallLoadRoom() {
    const int room = d_.s.player.room;
    if (d_.s.rooms.musicZone(room) != 0) return;
    const auto ped = hallPedestalTreasures(d_.s.rank.climbsDone, room);
    for (int i = 0; i < kHallPedCount; ++i) {
        const size_t k = static_cast<size_t>(i);
        d_.sc.hallPedTreasure[k] = ped[k];
        if (ped[k] < 0 || !d_.res) continue;
        try {
            anim::loadFrames(*d_.res, d_.anim, kTreasureBndlFirstId + ped[k], d_.rt.hallPedFrameBase[k]);
            hallScripts_[k] = anim::loadScript(*d_.res, kTreasureAnimFirstId + ped[k]);
        } catch (const std::exception& e) {
            log::warn("treasure hall: pedestal {}: {}", i, e.what());
        }
        hallFrame0_[k] = anim::bundleMember(*d_.res, kTreasureBndlFirstId + ped[k], 0);
    }
}

// 1190:01c5 TreasureHall_UnloadRoom (all six handles are reset, PORTING_NOTES.md §4.12)
void RoomScene::hallUnloadRoom() {
    for (int i = 0; i < kHallPedCount; ++i) {
        const size_t k = static_cast<size_t>(i);
        if (d_.sc.hallPedTreasure[k] >= 0 && d_.sc.hallPedAnim[k] >= 0 &&
            d_.anim.isRunning(d_.sc.hallPedAnim[k], hallScripts_[k]))
            d_.anim.stop(d_.sc.hallPedAnim[k], hallScripts_[k]);
        hallScripts_[k].reset();
        hallFrame0_[k].reset();
        d_.sc.hallPedTreasure[k] = -1;
        d_.sc.hallPedAnim[k] = -1;
    }
}

// 1190:02a0 TreasureHall_Update: a treasure animates only while the player's centre is in front
// of it (room 0: always). Empty pedestals are not started (the original starts a NULL script,
// which leaves the channel free; visually the same).
void RoomScene::hallUpdate() {
    const int room = d_.s.player.room;
    if (d_.s.rooms.musicZone(room) != 0) return;
    auto start = [&](size_t k, int x, int y) {
        if (d_.sc.hallPedTreasure[k] < 0 || !hallScripts_[k]) return;
        d_.sc.hallPedAnim[k] = static_cast<int16_t>(
            d_.anim.start(anim::layer::kHall, x, y, d_.rt.hallPedFrameBase[k], hallScripts_[k], 0, 0, anim::kChannelFlagErase));
    };
    if (room == 0) {
        if (d_.sc.hallPedAnim[0] < 0) start(0, kHallRoom0X, kHallRoom0Y);
        return;
    }
    const int cx = d_.s.player.w / 2 + d_.s.player.x;
    for (int i = 0; i < kHallPedCount; ++i) {
        const size_t k = static_cast<size_t>(i);
        const int px = d_.rt.hallPedX[k];
        if (cx < px || px + kHallTouchW < cx) {
            if (d_.sc.hallPedAnim[k] >= 0) {
                d_.anim.stop(d_.sc.hallPedAnim[k], hallScripts_[k]);
                d_.sc.hallPedAnim[k] = -1;
            }
        } else if (d_.sc.hallPedAnim[k] < 0) {
            start(k, px, d_.rt.hallPedY[k]);
        }
    }
}

// 1190:03a3 TreasureHall_Draw
void RoomScene::hallDraw(int yoff) {
    const int room = d_.s.player.room;
    if (d_.s.rooms.musicZone(room) != 0) return;
    if (room == 0) {
        if (d_.sc.hallPedTreasure[0] >= 0 && d_.sc.hallPedAnim[0] < 0 && hallFrame0_[0])
            d_.fb.drawSprite(kHallRoom0X, kHallRoom0Y + yoff, *hallFrame0_[0], false);
    } else {
        for (int i = 0; i < kHallPedCount; ++i) {
            const size_t k = static_cast<size_t>(i);
            if (d_.sc.hallPedTreasure[k] >= 0 && d_.sc.hallPedAnim[k] < 0 && hallFrame0_[k])
                d_.fb.drawSprite(d_.rt.hallPedX[k], d_.rt.hallPedY[k] + yoff, *hallFrame0_[k], false);
        }
    }
    d_.anim.drawSprites(anim::layer::kHall, yoff, kLayer1);
}

// ------------------------------------------------------------------------- composition

// 1198:0234 Room_DrawBackground
void RoomScene::drawBackground(int yoff) {
    gfx::FrameBuffer& fb = d_.fb;
    const int room = d_.s.player.room;
    fb.setFullClip();
    if (d_.res && RoomTable::valid(room)) {
        if (const auto bg = d_.res->pixmap(d_.s.rooms[room].bgPmap)) fb.drawOpaque(0, kPlayTop + kLayer1, *bg);
    }
    fb.copyRect(0, kPlayTop + kLayer1, kPlayW, kPlayH, 0, kPlayTop + kLayer2);
    if (isShopRoom(room)) netShopDrawSign(kLayer1);
    for (int i = 0; i < 3; ++i)
        if (const gfx::PixMap* pic = slotPics_[static_cast<size_t>(i)].get())
            fb.drawSprite(d_.rt.slotX[static_cast<size_t>(i)], kSlotY + kLayer1, *pic, false);
    if (yoff == 0) fb.copyRect(0, kPlayTop + kLayer1, kPlayW, kPlayH, 0, kPlayTop);
    fb.setPlayClip(yoff);
}

// 1198:0305 Room_DrawForeground, back to front.
void RoomScene::drawForeground(int yoff) {
    gfx::FrameBuffer& fb = d_.fb;
    const int room = d_.s.player.room;
    // 1. slot reveal on coin-toss steps with bit 1 set (post-increment values 2,3,6,7,10,11).
    for (int i = 0; i < 3; ++i) {
        const size_t k = static_cast<size_t>(i);
        const gfx::PixMap* pic = slotPics_[k].get();
        if (d_.p.coinTargetSlot != i || !pic) continue;
        const int sx = d_.rt.slotX[k];
        if ((d_.p.coinStep & 2) == 0 || d_.p.coinStep == 13) {
            fb.drawSprite(sx, kSlotY, *pic, false);
        } else {
            fb.copyRect(sx, kSlotY + kLayer2, kSlotW, kSlotH, sx, kSlotY);
            const uint8_t c = RoomTable::valid(room) ? d_.s.rooms.spotContent(room, i) : uint8_t{0xFF};
            const gfx::PixMap* obj = nullptr;
            if (c == kContentTreasure)
                obj = levelTreasure(d_.s.inv.gemsFound);
            else if (c == kContentKey)
                obj = d_.hud.art(d_.s.currentLevel() * 2);
            if (obj) fb.drawSprite(d_.rt.revealX[k], yoff + kRevealY, *obj, false);
        }
    }
    netShopTick(yoff);                                       // 2.
    gateLockDraw(yoff);                                      // 3.
    hallDraw(yoff);                                          // 4. (+ DAM layer 0x4D)
    d_.anim.drawSprites(anim::layer::kFore14, yoff, kLayer1);  // 5.
    fountainDraw(yoff);                                      // 6.
    d_.elves.drawAll(fb, room, yoff, d_.p.netStep, d_.hooks);  // 7.
    coinPileDraw(yoff);                                      // 8.
    playerDraw(yoff);                                        // 9.
    playerFxDraw(yoff);                                      // 10.
    d_.anim.drawSprites(anim::layer::kFore57, yoff, kLayer1);  // 11.
    d_.anim.drawSprites(anim::layer::kDefault, yoff, kLayer1);  // 12.
}

// 1198:0499 Room_EraseSlotReveal
void RoomScene::eraseSlotReveal() {
    const int room = d_.s.player.room;
    if (!RoomTable::valid(room)) return;
    for (int i = 0; i < 3; ++i) {
        if (slotPics_[static_cast<size_t>(i)] && d_.p.coinTargetSlot == i && (d_.p.coinStep & 2) != 0 &&
            d_.s.rooms.spotContent(room, i) > 1)
            restoreFromLayer(d_.fb, d_.rt.slotX[static_cast<size_t>(i)], kSlotY, kSlotW, kSlotH, kLayer2);
    }
}

// 1198:04f0 Room_DrawAll
void RoomScene::drawAll(int yoff) {
    d_.fb.setPlayClip(yoff);
    // RoomExtra_Draw (1108:0001) is empty.
    d_.anim.step();
    drawBackground(yoff);
    drawForeground(yoff);
    if (yoff == 0) d_.hud.redraw(d_.s);
    d_.fb.setFullClip();
}

// 1070:002d Screen_DissolveCopy, paced one pass per dissolvePassMs_.
void RoomScene::dissolveCopy(int sx, int sy, int w, int h, int dx, int dy) {
    for (int pass = 0; pass < 16; ++pass) {
        for (int row = 0; row < h; row += 2) {
            int col = -static_cast<int>(d_.rt.dissolveRowShift[static_cast<size_t>(row % 16)]);
            while (col < w - 2) {
                const int c = d_.rt.dissolveColOrder[static_cast<size_t>(pass)] + col;
                if (c >= 0 && c < w - 2) d_.fb.copyRect(sx + c, sy + row, 2, 2, dx + c, dy + row);
                col += 16;
            }
        }
        if (d_.present) d_.present(dx, dy, w, h);
        if (d_.pause && dissolvePassMs_ > 0) d_.pause(dissolvePassMs_);
    }
    d_.fb.copyRect(sx, sy, w, h, dx, dy);
    if (d_.present) d_.present(dx, dy, w, h);
}

// 1198:0527 Room_Present(t): bit 7 is computed and never read.
void RoomScene::present(uint8_t t) {
    const int room = d_.s.player.room;
    auto presentPlay = [&] {
        if (d_.present) d_.present(0, kPlayTop, kPlayW, kPlayH);
    };
    auto black = [&] {
        d_.fb.setForeColor(0);
        d_.fb.fillRect(0, kPlayTop, kPlayW, kPlayH);
        presentPlay();
        if (d_.pause && blackFrameMs_ > 0) d_.pause(blackFrameMs_);
    };
    const int kind = t & 0x7F;
    switch (kind) {
        case 0:
            drawAll(0);
            presentPlay();
            break;
        case 1:
            // "Fade" with the stubbed fades (DS:0860 = 4): black first for room 9 and the hall.
            if (room == kMountainStartRoom) black();
            // Fx_FadeOut_Stub(0x7ffe)
            if (d_.s.rooms.musicZone(room) == 0) {
                black();
                // Fx_FadeIn_Stub(0x7ffe)
                drawAll(0);
                presentPlay();
            } else if (room != kCastleRoom) {
                drawAll(0);
                presentPlay();
                // Fx_FadeIn_Stub(0x7ffe)
            }
            break;
        case 2:
            drawAll(kLayer1);
            dissolveCopy(0, kPlayTop + kLayer1, kPlayW, kPlayH, 0, kPlayTop);
            break;
        case 3:
        case 4:
        case 5:
        case 6:
            drawAll(kLayer1);
            d_.fb.copyRect(0, kPlayTop + kLayer1, kPlayW, kPlayH, 0, kPlayTop);
            presentPlay();
            break;
        default: break;
    }
    if (kind != 0) {
        drawBackground(kLayer1);
        if (d_.flushInput) d_.flushInput();
    }
}

// ------------------------------------------------------------------------- module draws

// 10f8:0000 NetShop_Tick: the door state machine runs once per frame (yoff 0 only).
void RoomScene::netShopTick(int yoff) {
    if (yoff != 0) return;
    gfx::FrameBuffer& fb = d_.fb;
    int16_t& state = d_.s.shop.state;
    const NicheDef* n = nicheFor(d_.s.player.room);
    switch (state) {
        case 1:
        case 3:
            if (n) {
                const int id = n->netPmap + (state == 3 ? 1 : 0);
                if (d_.res)
                    if (const auto pm = d_.res->pixmap(id)) fb.drawSprite(n->x, n->y, *pm, false);
                fb.copyRect(n->x, n->y, kNicheW, kNicheH, n->x, n->y + kLayer1);
            }
            state = static_cast<int16_t>(state == 1 ? 2 : 4);
            break;
        case 4:
        case 5:
        case 6: ++state; break;
        case 7:
            if (n) {
                fb.copyRect(n->x, n->y + kLayer2, kNicheW, kNicheH, n->x, n->y);
                fb.copyRect(n->x, n->y + kLayer2, kNicheW, kNicheH, n->x, n->y + kLayer1);
            }
            state = 0;
            break;
        default: break;  // 0 closed, 2 open
    }
}

// 10f8:0285 NetShop_DrawSign
void RoomScene::netShopDrawSign(int yoff) {
    const int room = d_.s.player.room;
    if (!isShopRoom(room)) return;
    const uint8_t level = d_.s.currentLevel();
    const int x = d_.rt.netSignX[level < 3 ? level : 0];
    if (yoff == 0) d_.fb.copyRect(x, kSignY + kLayer2, kSignW, kSignH, x, kSignY);
    if (const gfx::PixMap* pm = at(signFrames_, 4 - d_.s.shop.coinsLeft)) d_.fb.drawSprite(x, kSignY + yoff, *pm, false);
}

// 1188:0220 GateLock_Erase
void RoomScene::gateLockErase() {
    if (const GateLockPos* g = gateLockFor(d_.s.player.room))
        restoreFromLayer(d_.fb, g->x, g->y, kGateLockW, kGateLockH, kLayer1);
}

// 1188:0267 GateLock_Draw: frame 2 while unlocking (also stamped into the back page), 1 with the
// key, else 0.
void RoomScene::gateLockDraw(int yoff) {
    const GateLockPos* g = gateLockFor(d_.s.player.room);
    if (!g) return;
    const int f = d_.sc.gateUnlocking ? 2 : (d_.s.inv.haveKey ? 1 : 0);
    if (const gfx::PixMap* pm = at(lockFrames_, f)) d_.fb.drawSprite(g->x, g->y + yoff, *pm, false);
    if (d_.sc.gateUnlocking) d_.fb.copyRect(g->x, g->y, kGateLockW, kGateLockH, g->x, g->y + kLayer1);
}

// 1198:06b4 Room24Anim_Erase
void RoomScene::fountainErase() {
    if (d_.s.player.room == kFountainRoom)
        d_.fb.copyRect(kFountainX, kFountainY + kLayer1, kFountainW, kFountainH, kFountainX, kFountainY);
}

// 1198:06d5 Room24Anim_Draw: one frame per call (also one extra step per full-room compose).
void RoomScene::fountainDraw(int yoff) {
    if (d_.s.player.room != kFountainRoom) return;
    if (const gfx::PixMap* pm = at(fountain_, d_.sc.fountainFrame)) d_.fb.drawSprite(kFountainX, kFountainY + yoff, *pm, false);
    ++d_.sc.fountainFrame;
    if (d_.sc.fountainFrame >= static_cast<int16_t>(fountain_.size())) d_.sc.fountainFrame = 0;
}

// 1110:0059 CoinPile_Erase
void RoomScene::coinPileErase() {
    const CoinPileState& c = d_.s.coinPile;
    if (d_.s.player.room == c.room) restoreFromLayer(d_.fb, c.x, kCoinPileY, c.w, c.h, kLayer1);
}

// 1110:007d CoinPile_Draw
void RoomScene::coinPileDraw(int yoff) {
    const CoinPileState& c = d_.s.coinPile;
    if (d_.s.player.room == c.room && coinPile_) d_.fb.drawSprite(c.x, kCoinPileY + yoff, *coinPile_, false);
}

// 10e0:042c Player_Erase
void RoomScene::playerErase() {
    const PlayerState& pl = d_.s.player;
    restoreFromLayer(d_.fb, pl.x, pl.y, pl.w, pl.h, kLayer1);
}

// 10e0:0ff4 Player_Draw
void RoomScene::playerDraw(int yoff) {
    const PlayerState& pl = d_.s.player;
    if (d_.p.playerHidden) return;
    if (const gfx::PixMap* pm = playerFrame(pl.frame)) d_.fb.drawSprite(pl.x, pl.y + yoff, *pm, pl.facingLeft != 0);
}

// 10e0:1057 PlayerFx_Erase
void RoomScene::playerFxErase() {
    if (d_.p.fxFrame != 0) restoreFromLayer(d_.fb, d_.p.fxX, d_.p.fxY, d_.p.fxW, d_.p.fxH, kLayer1);
}

// 10e0:1079 PlayerFx_Draw (never mirrored)
void RoomScene::playerFxDraw(int yoff) {
    if (d_.p.fxFrame == 0) return;
    if (const gfx::PixMap* pm = playerFrame(d_.p.fxFrame)) d_.fb.drawSprite(d_.p.fxX, d_.p.fxY + yoff, *pm, false);
}

}  // namespace tmport::game::room
