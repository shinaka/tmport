#pragma once
// Room scene composition (segment 1198 scene.cpp; re/docs/code_1168-1198.md §8, ARCHITECTURE.md
// §3.1.4-3.1.5, REFERENCE_CAPTURES.md §4.3/§4.6/§4.10) together with the draw hooks it calls:
//   1198  Scene_LoadCommon / FreeCommon, Room_DrawBackground, Room_DrawForeground (exact
//         back-to-front order), Room_EraseSlotReveal, Room_DrawAll, Room_Present (transitions
//         0 instant, 1 black play-field for room 9 / the hall, 2 dissolve, 3..6 cut), fountain;
//   1100  spot pictures of the room (PMAP 1400+kind);
//   1188  gate lock sprite (BNDL 415);
//   1190  treasure hall pedestals (BNDL 330+t frames, DAM 20000+t on layer 0x4D);
//   10f8  net-shop door state machine and sign (BNDL 302, niche PMAPs 1320..1325);
//   1110  rescue coin pile drawing (PMAP 1699; the rule is game::updateCoinPile);
//   10e0  player and player-effects sprites (BNDL 300).
// All art comes from the user's files through the ResourceManager (decoded PixMaps are cached).

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "anim/anim_engine.h"
#include "anim/dam_script.h"
#include "game/game_hooks.h"
#include "game/game_state.h"
#include "game/room/elves.h"
#include "game/room/hud.h"
#include "game/room/room_state.h"
#include "game/room/room_tables.h"
#include "gfx/frame_buffer.h"
#include "gfx/pixmap.h"

namespace tmport {
class ResourceManager;
}

namespace tmport::game::room {

struct SceneDeps {
    gfx::FrameBuffer& fb;
    const ResourceManager* res;  // null: no pictures (tests)
    anim::AnimEngine& anim;
    const RoomTables& rt;
    GameState& s;
    PlayerRuntime& p;
    SceneRuntime& sc;
    ElfSystem& elves;
    Hud& hud;
    GameHooks& hooks;
    // Gfx_BlitToWindow.
    std::function<void(int x, int y, int w, int h)> present;
    // Lets time pass while the window shows what was presented (the black play-field of
    // transition 1, the dissolve passes). Empty: no pause.
    std::function<void(int ms)> pause;
    // Input_Flush at the end of Room_Present.
    std::function<void()> flushInput;
};

class RoomScene {
public:
    explicit RoomScene(SceneDeps d) : d_(std::move(d)) {}

    // How long the black play-field of transition 1 stays visible (the original shows it while
    // it redraws: 57..86 ms in the reference captures; no delay is coded). 0 = only presented.
    void setBlackFrameMs(int ms) { blackFrameMs_ = ms; }
    // One dissolve pass per this many ms (the original ran as fast as the blits allowed).
    void setDissolvePassMs(int ms) { dissolvePassMs_ = ms; }

    // --- climb resources (1198:0000 / 00b1) ------------------------------------------------------
    // Player frames + g_frameW/H, HUD art, sign, coin pile (sizes into GameState::coinPile), lock,
    // fountain.
    void loadCommon();
    void freeCommon();
    bool commonLoaded() const { return !playerFrames_.empty(); }
    const FrameSizes& frameSizes() const { return sizes_; }
    FrameSizes& frameSizes() { return sizes_; }
    const gfx::PixMap* playerFrame(int f) const;

    // --- per room ----------------------------------------------------------------------------------
    // 1100:0638 Spots_LoadRoomPictures (via 1198:0228) / 1100:068a.
    void loadRoomSlots();
    void freeRoomSlots();
    bool slotHasPicture(int i) const { return i >= 0 && i < 3 && slotPics_[static_cast<size_t>(i)] != nullptr; }
    // 1190:009b / 01c5 / 02a0 / 03a3.
    void hallLoadRoom();
    void hallUnloadRoom();
    void hallUpdate();
    void hallDraw(int yoff);

    // --- composition (1198) ------------------------------------------------------------------------
    void drawBackground(int yoff);   // 1198:0234 Room_DrawBackground
    void drawForeground(int yoff);   // 1198:0305 Room_DrawForeground
    void eraseSlotReveal();          // 1198:0499 Room_EraseSlotReveal
    void drawAll(int yoff);          // 1198:04f0 Room_DrawAll
    void present(uint8_t t);         // 1198:0527 Room_Present
    // 1070:002d Screen_DissolveCopy (transition 2).
    void dissolveCopy(int sx, int sy, int w, int h, int dx, int dy);

    // --- module draws called by Room_Run's erase phase and by the composition ------------------------
    void netShopTick(int yoff);      // 10f8:0000 NetShop_Tick
    void netShopDrawSign(int yoff);  // 10f8:0285 NetShop_DrawSign
    void gateLockErase();            // 1188:0220
    void gateLockDraw(int yoff);     // 1188:0267
    void fountainErase();            // 1198:06b4 Room24Anim_Erase
    void fountainDraw(int yoff);     // 1198:06d5 Room24Anim_Draw
    void coinPileErase();            // 1110:0059
    void coinPileDraw(int yoff);     // 1110:007d
    void playerErase();              // 10e0:042c
    void playerDraw(int yoff);       // 10e0:0ff4
    void playerFxErase();            // 10e0:1057
    void playerFxDraw(int yoff);     // 10e0:1079

    // The treasure picture shown for the n-th find of this level (BNDL 330 + index, member 0).
    const gfx::PixMap* levelTreasure(int n);

private:
    const gfx::PixMap* bundleMember(int bndl, int index);

    SceneDeps d_;
    int blackFrameMs_ = 70;
    int dissolvePassMs_ = 16;
    // Common resources.
    std::vector<std::shared_ptr<const gfx::PixMap>> playerFrames_;  // BNDL 300 (g_playerBndl DS:2db4)
    FrameSizes sizes_;
    std::vector<std::shared_ptr<const gfx::PixMap>> signFrames_;    // BNDL 302 (DS:5e80)
    std::shared_ptr<const gfx::PixMap> coinPile_;                    // PMAP 1699 (DS:1289)
    std::vector<std::shared_ptr<const gfx::PixMap>> lockFrames_;    // BNDL 415 (DS:2d79)
    std::vector<std::shared_ptr<const gfx::PixMap>> fountain_;      // BNDL 328 (DS:2dd0)
    // Per room.
    std::array<std::shared_ptr<const gfx::PixMap>, 3> slotPics_{};  // DS:106c g_spotPict
    std::array<anim::ScriptPtr, kHallPedCount> hallScripts_{};      // DS:65e8
    std::array<std::shared_ptr<const gfx::PixMap>, kHallPedCount> hallFrame0_{};  // BNDL 330+t member 0
    // Cache of bundle members (immutable decoded data).
    std::unordered_map<int, std::vector<std::shared_ptr<const gfx::PixMap>>> bundleCache_;
};

}  // namespace tmport::game::room
