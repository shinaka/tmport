// DOS game layer skeleton (src/dos/game/, wave 3): every declared module function links, the state
// initialises from the user's SST.EXE (spot checks against dgroup_offsets.h and the specs), the
// field registry is consistent, dumpToDgroup / loadFromDgroup round-trip the initial image, the
// quirk helpers read what the original reads, the sound facade keeps the DGROUP flags, and the
// scenario fixture runs. Only sizes, counts, addresses and numbers are compared (no game text).
#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <set>

#include "dos/data/dgroup_offsets.h"
#include "dos/game/anim.h"
#include "dos/game/camera.h"
#include "dos/game/cells.h"
#include "dos/game/cells2.h"
#include "dos/game/elves.h"
#include "dos/game/endgame.h"
#include "dos/game/game.h"
#include "dos/game/game_state.h"
#include "dos/game/hints.h"
#include "dos/game/hud.h"
#include "dos/game/init.h"
#include "dos/game/layout.h"
#include "dos/game/main_loop.h"
#include "dos/game/player.h"
#include "dos/game/quiz.h"
#include "dos/game/rank_chest.h"
#include "dos/game/scene.h"
#include "dos/game/search.h"
#include "dos/game/signin.h"
#include "dos/game/sound.h"
#include "dos/game/title.h"
#include "dos/game/ui.h"
#include "dos/save/users_dat.h"
#include "dos_game_fixture.h"
#include "dos_test_data.h"

using namespace tmport;
using namespace tmport::dos;

namespace {

using AnyFn = void (*)();
template <class F>
AnyFn fnRef(F f) {
    return reinterpret_cast<AnyFn>(f);
}

}  // namespace

TEST_CASE("dos game skeleton: every declared module function links") {
    // Generated from the module headers (extracted/scratch/dos_w3_skeleton/gen_stubs.py linktable):
    // taking each function's address makes the linker resolve all of them.
    const AnyFn fns[] = {
        fnRef(static_cast<void (*)(DosGame&)>(&mainFrameBody)),
        fnRef(static_cast<void (*)(DosGame&, const TickInfo&)>(&gameTick)),
        fnRef(static_cast<void (*)(DosGame&)>(&framePresent)),
        fnRef(static_cast<void (*)(DosGame&)>(&gameShutdown)),
        fnRef(static_cast<void (*)(DosGame&)>(&mousePoll)),
        fnRef(static_cast<void (*)(DosGame&)>(&soundSyncState)),
        fnRef(static_cast<void (*)(DosGame&)>(&inputTick)),
        fnRef(static_cast<void (*)(DosGame&)>(&inputMouseWalk)),
        fnRef(static_cast<void (*)(DosGame&)>(&gameInit)),
        fnRef(static_cast<void (*)(DosGame&)>(&gameInitVars)),
        fnRef(static_cast<int (*)(DosGame&)>(&spritesLoadBegin)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&sceneUpdate)),
        fnRef(static_cast<int (*)(DosGame&, int)>(&sceneLoadPan)),
        fnRef(static_cast<void (*)(DosGame&, FileIndex)>(&loadSpriteBank)),
        fnRef(static_cast<void (*)(DosGame&)>(&frameBegin)),
        fnRef(static_cast<void (*)(DosGame&)>(&playfieldRedraw)),
        fnRef(static_cast<void (*)(DosGame&)>(&scrollUpdate)),
        fnRef(static_cast<bool (*)(DosGame&)>(&transitionDissolve)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellGetMask)),
        fnRef(static_cast<void (*)(DosGame&)>(&cellsUpdateVisible)),
        fnRef(static_cast<uint16_t (*)(DosGame&, int)>(&callCellObjectHandler)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&callCellVisibleHandler)),
        fnRef(static_cast<int (*)(DosGame&)>(&cellGroundCheckObj)),
        fnRef(static_cast<int (*)(DosGame&)>(&cellGroundCheckPlayer)),
        fnRef(static_cast<void (*)(DosGame&)>(&cellLevelBoundary01)),
        fnRef(static_cast<void (*)(DosGame&)>(&cellLevelBoundary12)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&cellDrawTreetop)),
        fnRef(static_cast<void (*)(DosGame&, int, int)>(&cellDrawGeyser)),
        fnRef(static_cast<void (*)(DosGame&, int, int, int)>(&cellDrawObject)),
        fnRef(static_cast<void (*)(DosGame&)>(&castlePlaceCreature)),
        fnRef(static_cast<void (*)(DosGame&)>(&castleCreatureHitCheck)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP0Void)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP5CaveDoorW)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP6CaveExitW)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP9CaveDoorE)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP10CaveExitE)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP34CaveUp)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP35UpperRoomDown)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP11ClubDoor)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP12StartRoom)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP7WallLeft)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP8WallRight)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP50ShelfEnter)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP51ShelfExit)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP52Shelf0to1)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP53Shelf1to0)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP54Shelf1to2)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP55Shelf2to1)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP65Shelf2to3)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP66Shelf3to2)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP30V30TrunkTop)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP17MineDoorW)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP37MineExitW)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP18MineDoorE)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP38MineExitE)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP44CastleDoor)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP23FallShaft)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP47Boundary01)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP19Boundary12)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP24Wall)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP46V46Boundary01)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP25TrunkShaft)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP31GateL0)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP26Unused)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP27Unused)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP28Path)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP29Unused)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP36V36NetShop)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP40V40Geyser)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP39V39GateL1)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP41V41GeyserTop)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP45V45Bridge)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP48Landing)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP56V56CastleHall)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP57V57CastleWall)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP60IceDoor)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP61IceCaveExit)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP62IceSlide)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP63V63CastleTop)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellBonusCoins)),
        fnRef(static_cast<void (*)(DosGame&)>(&cellV49Showcase)),
        fnRef(static_cast<void (*)(DosGame&)>(&cellV32AnimDecor)),
        fnRef(static_cast<void (*)(DosGame&)>(&cellV10)),
        fnRef(static_cast<void (*)(DosGame&)>(&cellV59)),
        fnRef(static_cast<void (*)(DosGame&)>(&cellV7)),
        fnRef(static_cast<void (*)(DosGame&)>(&cellV8)),
        fnRef(static_cast<void (*)(DosGame&, int, int)>(&cellConveyLeft)),
        fnRef(static_cast<void (*)(DosGame&, int, int, uint16_t, uint16_t)>(&cellConveyLeftIf)),
        fnRef(static_cast<void (*)(DosGame&, int, int)>(&cellConveyRight)),
        fnRef(static_cast<void (*)(DosGame&, int, int, uint16_t, uint16_t)>(&cellConveyRightIf)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP1)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP2)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP3)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP4)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP13)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP14)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP15)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP16)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP22)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP21)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP43)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP42GateL2)),
        fnRef(static_cast<void (*)(DosGame&)>(&castleCreatureSpawn)),
        fnRef(static_cast<void (*)(DosGame&)>(&playerUpdate)),
        fnRef(static_cast<void (*)(DosGame&, uint16_t)>(&callPlayerStateHandler)),
        fnRef(static_cast<void (*)(DosGame&)>(&playerMove)),
        fnRef(static_cast<void (*)(DosGame&)>(&objWrapWorldX)),
        fnRef(static_cast<void (*)(DosGame&)>(&playerNetLeft)),
        fnRef(static_cast<void (*)(DosGame&)>(&playerNetRight)),
        fnRef(static_cast<void (*)(DosGame&)>(&searchStart)),
        fnRef(static_cast<int (*)(DosGame&)>(&searchCanSearch)),
        fnRef(static_cast<void (*)(DosGame&)>(&endSeqSetup)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt00Idle)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt03Spawn)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt04Arrive)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt05Searching)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt09AfterTeleport)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt10OnPath)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt11WalkLeft)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt12WalkRight)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt13LookUp)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt14Front)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt15ClimbUp)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt16ClimbDown)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt17JumpLeft)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt18JumpLeftLand)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt19JumpRight)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt20JumpRightLand)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt21NetLeftSwing)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt22NetLeftHold)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt23NetLeftDone)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt24NetRightSwing)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt25NetRightHold)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt26NetRightDone)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt27CoinLeftDone)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt28CoinRightDone)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt29PassageUp)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt30SlideDown)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt31FallLeft)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt32FallRight)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt33LadderJump)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt34Chute)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt35TreasureRoomLand)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt41EndIntro)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt42EndHorn)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt43EndBeam)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt44EndSparkle)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt45EndChestOpen)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt46EndStep)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt47EndStep)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt48EndFlightInit)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt49EndFlightNext)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt50EndFlightStart)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt51EndFlightStep)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt52EndRank)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt53EndRankSparkle)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt54EndRankWait)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt55EndCrown)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt56EndCreature)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt57EndPlayer)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt58EndShowcaseCheck)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt59EndShowcaseStart)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt60EndShowcaseStep)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt61EndDone)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt63ChampSparkle)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt64ChampChest)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt65ChampArcs)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt66ChampScream)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt67ChampCrownFly)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt68ChampCatch)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt69ChampCrowned)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt70ChampSparkle)),
        fnRef(static_cast<void (*)(DosGame&)>(&plrSt71ChampSparkleWait)),
        fnRef(static_cast<void (*)(DosGame&)>(&animTick)),
        fnRef(static_cast<void (*)(DosGame&)>(&animInitSlots)),
        fnRef(static_cast<void (*)(DosGame&, int, int)>(&spritesSnapshot)),
        fnRef(static_cast<void (*)(DosGame&, int, int)>(&spritesDrawSorted)),
        fnRef(static_cast<void (*)(DosGame&, int, int)>(&spritesEraseAll)),
        fnRef(static_cast<void (*)(DosGame&, int, int)>(&animSetIfIdle)),
        fnRef(static_cast<void (*)(DosGame&, int, int)>(&animQueue)),
        fnRef(static_cast<void (*)(DosGame&, int, int)>(&animSet)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&animStop)),
        fnRef(static_cast<void (*)(DosGame&, int, int)>(&animStart)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&animClear)),
        fnRef(static_cast<void (*)(DosGame&)>(&searchUpdate)),
        fnRef(static_cast<void (*)(DosGame&)>(&treasurePickShowcase)),
        fnRef(static_cast<void (*)(DosGame&)>(&shelfRoomDrawShelf)),
        fnRef(static_cast<void (*)(DosGame&)>(&treasureAddCollected)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP64EndScreen)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP20CastleGate)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP70TreasureRoom)),
        fnRef(static_cast<void (*)(DosGame&)>(&treasureRoomLeave)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP71V71ToRankScreen)),
        fnRef(static_cast<uint16_t (*)(DosGame&)>(&cellP72RankScreen)),
        fnRef(static_cast<void (*)(DosGame&)>(&gameAdvanceEnd)),
        fnRef(static_cast<int (*)(DosGame&, int)>(&treasureIsCollected)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&shackPair0836Approach)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&shackPair0836Swap)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&shackPair0e2c)),
        fnRef(static_cast<void (*)(DosGame&)>(&shackPair065a)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&shackPair1c22)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&shackPair1858)),
        fnRef(static_cast<void (*)(DosGame&)>(&shackFly1e)),
        fnRef(static_cast<void (*)(DosGame&)>(&shackFly50)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&shackTrio4a4c4e)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&shackResetDisplay)),
        fnRef(static_cast<void (*)(DosGame&)>(&rankChestUpdate)),
        fnRef(static_cast<void (*)(DosGame&, int, int, int)>(&rankChestDrawStar)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&elvesSpawn)),
        fnRef(static_cast<void (*)(DosGame&)>(&elvesUpdate)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfThink)),
        fnRef(static_cast<void (*)(DosGame&, uint16_t)>(&callElfStateHandler)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfRelease)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfTurn)),
        fnRef(static_cast<void (*)(DosGame&, int, int)>(&elvesShiftNearby)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfCheckNet)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfNetCatch)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&elfWalkCommon)),
        fnRef(static_cast<void (*)(DosGame&)>(&coinElfCheck)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt01)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt05)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt02)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt04)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt09)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt13)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt10)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt12)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt17)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt21)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt18)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt20)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt25)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt28)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt45)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt46)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt47)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt49)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt50)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt51)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt52)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt53)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt54)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt55)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt56)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt57)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt58)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt59)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt60)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt61)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt70)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt71)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt72)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt73)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt74)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt64)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt62)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt63)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt65)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt66)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt03)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt68)),
        fnRef(static_cast<void (*)(DosGame&)>(&elfSt69)),
        fnRef(static_cast<void (*)(DosGame&)>(&layoutGenerate)),
        fnRef(static_cast<uint16_t (*)(const DosGame&)>(&layoutTimeSeed)),
        fnRef(static_cast<void (*)(DosGame&, int, int, int)>(&layoutPutInSlot)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&layoutPlaceKey)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&layoutPlaceTreasures)),
        fnRef(static_cast<void (*)(DosGame&)>(&layoutMirrorSharedCells)),
        fnRef(static_cast<void (*)(DosGame&)>(&layoutUnmarkSharedCells)),
        fnRef(static_cast<void (*)(DosGame&)>(&layoutClearSlots)),
        fnRef(static_cast<void (*)(DosGame&)>(&cellsAdjustForCpu)),
        fnRef(static_cast<void (*)(DosGame&)>(&cellsResetVariable)),
        fnRef(static_cast<int (*)(DosGame&)>(&castlePickLayout)),
        fnRef(static_cast<void (*)(DosGame&)>(&castleApplyLayout)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&clueBuildWords)),
        fnRef(static_cast<void (*)(DosGame&)>(&hintsUpdate)),
        fnRef(static_cast<void (*)(DosGame&)>(&hintsCheckTriggers)),
        fnRef(static_cast<int (*)(DosGame&)>(&hintsPickNext)),
        fnRef(static_cast<void (*)(DosGame&)>(&gameoverUpdateMessages)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&flutterShow)),
        fnRef(static_cast<void (*)(DosGame&)>(&menuUpdate)),
        fnRef(static_cast<void (*)(DosGame&)>(&menuMoveSelection)),
        fnRef(static_cast<void (*)(DosGame&)>(&dialogUpdate)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&callDialogConfirm)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&callDialogCancel)),
        fnRef(static_cast<void (*)(DosGame&)>(&menuCmdHelpPage)),
        fnRef(static_cast<void (*)(DosGame&)>(&menuCmdStartOver)),
        fnRef(static_cast<void (*)(DosGame&)>(&menuCmdStartNewPlayer)),
        fnRef(static_cast<void (*)(DosGame&)>(&menuCmdExitAndSave)),
        fnRef(static_cast<void (*)(DosGame&)>(&menuCmdNewPlayerHints)),
        fnRef(static_cast<void (*)(DosGame&)>(&menuCmdMusic)),
        fnRef(static_cast<void (*)(DosGame&)>(&menuCmdSound)),
        fnRef(static_cast<void (*)(DosGame&)>(&gameHandleHotkeys)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&msgBoxDraw)),
        fnRef(static_cast<void (*)(DosGame&, int, int, int, int, int, std::optional<std::string_view>)>(&boxDrawFrame)),
        fnRef(static_cast<void (*)(DosGame&, int, uint8_t, uint8_t, int)>(&boxSetupInput)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&buttonDraw)),
        fnRef(static_cast<void (*)(DosGame&)>(&buttonFlash)),
        fnRef(static_cast<void (*)(DosGame&, uint32_t)>(&delayTicks)),
        fnRef(static_cast<void (*)(DosGame&, int, bool)>(&menuBarDrawTitle)),
        fnRef(static_cast<void (*)(DosGame&)>(&menuHighlightItem)),
        fnRef(static_cast<int (*)(DosGame&)>(&helpLoadFile)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&helpOpen)),
        fnRef(static_cast<void (*)(DosGame&, int, int)>(&helpDrawPage)),
        fnRef(static_cast<int (*)(DosGame&, int, int, std::string_view)>(&textDraw)),
        fnRef(static_cast<int (*)(DosGame&, std::string_view)>(&textWidth)),
        fnRef(static_cast<void (*)(DosGame&, int, int, int, int)>(&gfxHLine)),
        fnRef(static_cast<void (*)(DosGame&, int, int, int, int)>(&gfxVLine)),
        fnRef(static_cast<void (*)(DosGame&, int, int, int, int, int)>(&gfxFillRectColor)),
        fnRef(static_cast<void (*)(DosGame&, int, int, int, int, int)>(&gfxFrameRect)),
        fnRef(static_cast<void (*)(DosGame&)>(&titleStart)),
        fnRef(static_cast<bool (*)(DosGame&)>(&titleFrame)),
        fnRef(static_cast<void (*)(DosGame&)>(&titleFinish)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&callTitlePhase)),
        fnRef(static_cast<void (*)(DosGame&)>(&titleClearBox)),
        fnRef(static_cast<void (*)(DosGame&)>(&titleStepDrawTitle)),
        fnRef(static_cast<void (*)(DosGame&)>(&titleStepWait)),
        fnRef(static_cast<void (*)(DosGame&, int, int, int, uint8_t)>(&titleDrawSprite)),
        fnRef(static_cast<void (*)(DosGame&)>(&titleStepStory1)),
        fnRef(static_cast<void (*)(DosGame&)>(&titleStepStory2)),
        fnRef(static_cast<void (*)(DosGame&)>(&titleStepStartWalkoff)),
        fnRef(static_cast<void (*)(DosGame&)>(&titleStepWaitWalkoff)),
        fnRef(static_cast<void (*)(DosGame&)>(&titleStepWaitActorGone)),
        fnRef(static_cast<void (*)(DosGame&)>(&titleSuperSolversPage)),
        fnRef(static_cast<void (*)(DosGame&)>(&titleResetPhase)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInUpdate)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInHandleInput)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInDraw)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInDrawPanel)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInDrawNameEntry)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInDrawList)),
        fnRef(static_cast<void (*)(DosGame&)>(&screenDrawBackdrop)),
        fnRef(static_cast<void (*)(DosGame&, uint8_t, uint8_t, uint8_t)>(&signInDrawTypedChar)),
        fnRef(static_cast<void (*)(DosGame&, bool)>(&signInDrawField)),
        fnRef(static_cast<void (*)(DosGame&, uint8_t, uint8_t, uint8_t, uint8_t, uint16_t, uint16_t)>(&signInHighlightRow)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInMoveHighlight)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInSetupZones)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInCmdSelect)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInCmdNewName)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInCmdRemove)),
        fnRef(static_cast<void (*)(DosGame&)>(&dialogCloseRedraw)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInDlgExitYes)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInDlgBack)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInDlgRemoveYes)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInDlgContinueAnswer)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInDlgReset)),
        fnRef(static_cast<void (*)(DosGame&)>(&signInDlgListFull)),
        fnRef(static_cast<void (*)(DosGame&)>(&dialogHandleButtons)),
        fnRef(static_cast<void (*)(DosGame&)>(&dialogHandleOk)),
        fnRef(static_cast<void (*)(DosGame&)>(&uiForceRedraw)),
        fnRef(static_cast<int (*)(DosGame&)>(&usersEnsureLoaded)),
        fnRef(static_cast<int (*)(DosGame&)>(&usersNameExists)),
        fnRef(static_cast<void (*)(DosGame&)>(&usersReadFile)),
        fnRef(static_cast<void (*)(DosGame&, DosSaveMoment)>(&usersWriteFile)),
        fnRef(static_cast<void (*)(DosGame&)>(&usersClearRecord)),
        fnRef(static_cast<void (*)(DosGame&)>(&usersAddPlayer)),
        fnRef(static_cast<void (*)(DosGame&)>(&usersSelectRecord)),
        fnRef(static_cast<void (*)(DosGame&)>(&usersInitNewPlayer)),
        fnRef(static_cast<void (*)(DosGame&)>(&usersLoadPlayer)),
        fnRef(static_cast<void (*)(DosGame&)>(&usersPack)),
        fnRef(static_cast<void (*)(DosGame&)>(&gameRestore)),
        fnRef(static_cast<void (*)(DosGame&)>(&gameNewSetup)),
        fnRef(static_cast<int (*)(DosGame&)>(&gameStart)),
        fnRef(static_cast<void (*)(DosGame&)>(&gameEnd)),
        fnRef(static_cast<void (*)(DosGame&)>(&quizUpdate)),
        fnRef(static_cast<void (*)(DosGame&)>(&quizDrawScreen)),
        fnRef(static_cast<void (*)(DosGame&)>(&quizDrawFrame)),
        fnRef(static_cast<int (*)(DosGame&)>(&quizPickQuestion)),
        fnRef(static_cast<void (*)(DosGame&)>(&quizSplitText)),
        fnRef(static_cast<void (*)(DosGame&)>(&quizSetupAnswers)),
        fnRef(static_cast<void (*)(DosGame&)>(&quizDrawQuestion)),
        fnRef(static_cast<void (*)(DosGame&)>(&quizDrawAnswers)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&quizSetFocus)),
        fnRef(static_cast<void (*)(DosGame&)>(&quizCorrect)),
        fnRef(static_cast<void (*)(DosGame&)>(&quizWrong)),
        fnRef(static_cast<void (*)(DosGame&)>(&clueReveal)),
        fnRef(static_cast<int (*)(DosGame&, std::span<const uint8_t>, size_t)>(&dictExpand)),
        fnRef(static_cast<int (*)(DosGame&)>(&quizLoadDictionary)),
        fnRef(static_cast<int (*)(DosGame&, bool)>(&quizLoadQuestions)),
        fnRef(static_cast<void (*)(DosGame&)>(&quizDeal)),
        fnRef(static_cast<void (*)(DosGame&)>(&quizPlanStatuses)),
        fnRef(static_cast<int (*)(DosGame&)>(&quizTierHasWork)),
        fnRef(static_cast<void (*)(DosGame&)>(&quizResetHistory)),
        fnRef(static_cast<void (*)(DosGame&)>(&quizFindTierEnd)),
        fnRef(static_cast<int (*)(DosGame&)>(&quizReorderSlots)),
        fnRef(static_cast<void (*)(DosGame&)>(&screenDrawOverlay)),
        fnRef(static_cast<void (*)(DosGame&, int)>(&callScreenPainter)),
        fnRef(static_cast<void (*)(DosGame&)>(&hudRedraw)),
        fnRef(static_cast<void (*)(DosGame&)>(&hudDrawPromptIcon)),
        fnRef(static_cast<void (*)(DosGame&, int, int, int, int)>(&hudDrawNumber)),
    };
    CHECK(std::size(fns) == 390);
    CHECK(std::all_of(std::begin(fns), std::end(fns), [](AnyFn f) { return f != nullptr; }));
}

TEST_CASE("dos game skeleton: the field registry tiles DGROUP without overlaps") {
    std::vector<DgroupField> all = gameStateFields();
    const auto& svc = serviceFields();
    all.insert(all.end(), svc.begin(), svc.end());
    std::sort(all.begin(), all.end(), [](const DgroupField& a, const DgroupField& b) { return a.ds < b.ds; });
    std::set<std::string_view> names;
    for (size_t i = 0; i < all.size(); ++i) {
        INFO(all[i].name);
        CHECK(all[i].size > 0);
        CHECK(static_cast<size_t>(all[i].ds) + all[i].size <= kDgroupSize);
        CHECK(names.insert(all[i].name).second);
        if (i + 1 < all.size()) {
            INFO(all[i + 1].name);
            CHECK(static_cast<size_t>(all[i].ds) + all[i].size <= all[i + 1].ds);
        }
    }
    // Spot checks of addresses and sizes against dgroup_offsets.h.
    auto find = [&](std::string_view n) -> const DgroupField* {
        for (const auto& f : all)
            if (f.name == n) return &f;
        return nullptr;
    };
    REQUIRE(find("g_cellPicture"));
    CHECK(find("g_cellPicture")->ds == dg::kCellPicture);
    CHECK(find("g_cellPicture")->size == 2 * dg::kCellPictureCount);
    REQUIRE(find("g_elves"));
    CHECK(find("g_elves")->size == dg::elfrec::kSize * dg::elfrec::kCount);
    REQUIRE(find("g_spriteSlots"));
    CHECK(find("g_spriteSlots")->size == dg::slotrec::kSize * dg::slotrec::kCount);
    CHECK(find("g_spriteSlots")->ds + 2 * dg::slotrec::kSize == dg::kSlot2X);
    REQUIRE(find("g_animScriptData"));
    CHECK(find("g_animScriptData")->ds + find("g_animScriptData")->size == dg::kAnimScripts);
    REQUIRE(find("g_questionText"));
    CHECK(find("g_questionText")->ds + find("g_questionText")->size == dg::kDealPlan);
    REQUIRE(find("g_usersFile"));
    CHECK(find("g_usersFile")->size == kUsersFileSize);
    REQUIRE(find("g_userRec"));
    CHECK(find("g_userRec")->farPointer);
    CHECK(find("g_curObj")->farPointer);
    CHECK_FALSE(find("g_coins")->farPointer);
}

TEST_CASE("dos game skeleton: the state initialises from SST.EXE") {
    TM_REQUIRE_DOS_GAME_FILES();
    DosRuntime rt;
    RecordingSound sound;
    DosUsersStore users(std::filesystem::path{});
    DosGame g(*gameFiles, rt, sound, users, DosOptions{});
    const GameTables& t = g.tables;

    // World grid (code_world.md §3.1 examples).
    CHECK(g.world.cellType[0x02] == 12);
    CHECK(g.world.cellPicture[0x02] == 0x45);
    CHECK(g.world.cellType[0x7E] == 41);
    CHECK(g.world.cellType[0xEF] == 31);
    CHECK(t.world.variableCellDefaults == std::array<uint16_t, 9>{0x4B, 0x26, 0x2F, 0x26, 0x38, 0x2F, 0x47, 0x47, 0x40});
    CHECK(t.world.variableCells == std::array<uint16_t, 9>{0x26, 0x79, 0x84, 0x88, 0xAE, 0xB7, 0xBC, 0xEA, 0xEF});
    CHECK(t.world.creatureMask == std::array<uint16_t, 4>{0, 0x1FF, 0xFF, 0x3F});
    CHECK(t.world.hidingCells[0] == 0x77);
    CHECK(t.world.pictureAttrOffset == std::array<uint16_t, 3>{dg::kPictureAttrL0, dg::kPictureAttrL1, dg::kPictureAttrL2});
    for (int level = 0; level < 3; ++level) {
        static constexpr uint16_t kCount[] = {50, 38, 31}, kPlaces[] = {18, 13, 10};
        CHECK(g.world.levelPicData[level][dg::levelpic::kPictureCount / 2] == kCount[level]);
        CHECK(g.world.levelPicData[level][dg::levelpic::kPlaces / 2] == kPlaces[level]);
    }
    CHECK(t.world.cellPictureBefore == 0x08CF);  // the segment word of g_playerStateHandlers[71]

    // Player: treasure-room values, scripts (code_player.md §7.1, §10).
    CHECK(g.player.shackSavedShown == -1);
    CHECK(g.player.shackPartner1 == -1);
    CHECK(g.player.shackPartner2 == -1);
    CHECK(g.player.shackPartner1X == 0x50);
    CHECK(g.player.shackPartner2X == 0x50);
    CHECK(animScriptWord(g, t, 6, 0) == 10);  // script 6 = 10, 1, 164, -10
    CHECK(animScriptWord(g, t, 6, 1) == 1);
    CHECK(animScriptWord(g, t, 6, 2) == 164);
    CHECK(animScriptWord(g, t, 6, 3) == -10);
    CHECK(animScriptWordAt(g, dg::kAnimScript88Frame) == 6);
    CHECK(t.player.animScriptOffset[4] == 0x3DA0);
    CHECK(t.player.rankTable[0] == 5);
    CHECK(t.player.rankTable[1] == 1);
    CHECK(t.player.rankTable[2 * dg::kRankTablePairs] == -1);
    CHECK(t.player.endFlightPathY.size() == 27);
    CHECK(t.player.endShowcasePathY.size() == 17);
    CHECK(t.player.endFlightPathY.back() == -1);

    // Elves, hints.
    CHECK(t.elves.elfCounts[0] == std::array<uint16_t, 4>{3, 0, 3, 3});
    CHECK(t.elves.elfCounts[3][1] == 6);
    CHECK(t.elves.levelXRange[0][0] == 0x300);
    CHECK(t.elves.levelXRange[0][1] == 0x770);
    CHECK(t.elves.levelXRange[2][2] == 0x3FF);
    CHECK(g.elves.crownMsgToggle == 1);
    CHECK(*std::min_element(t.elves.messageOffset.begin(), t.elves.messageOffset.end()) == kMessageTextStart);
    for (uint16_t off : t.elves.messageOffset) {
        CHECK(off >= kMessageTextStart);
        CHECK(off < kMessageTextEnd);
    }
    CHECK(g.elves.messageText.back() == 0);  // the last text ends at DS:7CA2
    CHECK(t.elves.clueSchemes[1] == std::array<uint8_t, 3>{0, 1, 2});

    // UI.
    CHECK(g.ui.lineHeight == 12);
    CHECK(g.ui.signinInit == 1);
    CHECK(g.ui.musicBoxDefault == 1);
    CHECK(g.ui.hintsBoxDefault == 0);
    CHECK(t.ui.msgBoxes.size() == dg::kMsgBoxesCount);
    CHECK(t.ui.msgBoxes[1].x1 == 26);
    CHECK(t.ui.msgBoxes[5].type == 1);
    CHECK(t.ui.msgBoxes[22].type == 7);
    CHECK(t.ui.msgBoxes[0].title == 0);
    CHECK(t.ui.dialogBox[8] == 3);
    CHECK(t.ui.dialogBox[0] == -1);
    CHECK(t.ui.menuFirstDialog == std::array<uint16_t, 3>{0, 8, 11});
    CHECK(t.ui.userRecOffsets[1] == 3 + 360);
    CHECK(t.ui.signinRankThresholds == std::array<uint16_t, 7>{5, 25, 70, 115, 170, 230, 300});
    CHECK(t.ui.dealTemplate == std::array<uint8_t, 9>{0, 3, 2, 0, 3, 2, 0, 3, 2});
    CHECK(t.ui.clueWordText.size() == dg::kClueWordTextCount);
    CHECK(menuWrapAt(t, -2) == 0);
    CHECK(menuWrapAt(t, -1) == 2);
    CHECK(menuWrapAt(t, 0) == 0);
    CHECK(menuWrapAt(t, 1) == 1);
    CHECK(menuWrapAt(t, 2) == 2);
    CHECK(menuWrapAt(t, 3) == 0);

    // Core (sound flags in the file), BSS zero, the services.
    CHECK(g.core.soundOn == 1);
    CHECK(g.core.musicOn == 1);
    CHECK(g.core.musicGate == 0xFFFF);
    CHECK(g.core.soundDevice == 0xFFFF);
    CHECK(g.core.uiMode == 0);
    CHECK(g.core.coins == 0);
    CHECK(g.ui.userRec == -1);
    CHECK(g.rand.seed() == 1);
    CHECK(g.help.totalPages() == 15);
    CHECK(g.puzzles.tier5Start() == 123);
    CHECK(g.quizTiers.start(5) == 123);
    CHECK(g.inputTables.zones(0).records.size() == 11);
    CHECK(g.bank == nullptr);
}

TEST_CASE("dos game skeleton: dumpToDgroup round-trips the initial image") {
    TM_REQUIRE_DOS_GAME_FILES();
    DosRuntime rt;
    RecordingSound sound;
    DosUsersStore users(std::filesystem::path{});
    DosGame g(*gameFiles, rt, sound, users, DosOptions{});
    const std::vector<uint8_t> image = initialDgroupImage(g.exe());
    REQUIRE(image.size() == kDgroupSize);

    // On the file's image, the dump of the freshly loaded state changes nothing.
    const std::vector<uint8_t> dumped = dumpToDgroup(static_cast<const GameState&>(g), image);
    CHECK(dumped == image);

    // On zeros, every field below the BSS carries the file's bytes (the fields cover what they say).
    const std::vector<uint8_t> zeros(kDgroupSize, 0);
    const std::vector<uint8_t> fieldsOnly = dumpToDgroup(static_cast<const GameState&>(g), zeros);
    size_t initBytes = 0;
    for (const DgroupField& f : gameStateFields()) {
        for (size_t i = 0; i < f.size; ++i) {
            const size_t a = f.ds + i;
            if (a >= dg::kInitSize) break;
            ++initBytes;
            if (fieldsOnly[a] != image[a]) {
                FAIL_CHECK("field " << f.name << " byte " << i << " differs from the file");
                break;
            }
        }
    }
    CHECK(initBytes > 0x2000);  // the grids, scripts and texts are covered

    // load(dump(s)) == s, also after changes in every section.
    g.world.cameraX = 0x1234;
    g.player.slots[3].obj = DsPtr{elfObjOff(2)};
    g.player.slots[3].frame = 0xFFFF;
    g.elves.elves[14].obj.dx = -1;
    g.ui.questionText[999] = 'x';
    g.core.coins = 42;
    g.ui.userRec = 3;
    const std::vector<uint8_t> d1 = dumpToDgroup(static_cast<const GameState&>(g), image);
    GameState back;
    loadFromDgroup(back, d1);
    CHECK(back.ui.userRec == 3);
    CHECK(back.world.cameraX == 0x1234);
    CHECK(back.player.slots[3].obj == DsPtr{elfObjOff(2)});
    CHECK(back.elves.elves[14].obj.dx == -1);
    CHECK(back.core.coins == 42);
    CHECK(dumpToDgroup(back, image) == d1);
    // The record pointers come from the record index.
    CHECK(d1[dg::kTotalTreasures] == static_cast<uint8_t>((kUsersRecordBase + 360 * 3 + 0x0A) & 0xFF));
    // diffDgroup names exactly the changed fields (far pointers by offset only).
    std::vector<uint8_t> d2 = d1;
    d2[dg::kCoins] = 43;
    d2[dg::kCurObj + 2] ^= 0x55;  // a different segment word only
    const auto diff = diffDgroup(d1, d2);
    REQUIRE(diff.size() == 1);
    CHECK(diff[0] == "g_coins");

    // The whole dump with the service-held globals.
    const std::vector<uint8_t> full = dumpToDgroup(g);
    REQUIRE(full.size() == kDgroupSize);
    CHECK(full[dg::kRandSeed] == 1);
    CHECK(full[dg::kKeys] == 0);
}

TEST_CASE("dos game skeleton: quirk helpers") {
    CHECK(cellIndexOf(0x140, 0x70) == 0x02);
    CHECK(cellIndexOf(0x7FF, 0x7F0) == 0xFF);
    CHECK(cellIndexOf(0xFFFF, 0x80) == 0x1FF + 0x10);  // x = -1: column 0x1FF, not masked
    GameState s;
    s.world.cellContent[255] = 0xE8;
    s.world.cellContent[0] = 0x1234;
    s.world.cellType[0] = 0x05;
    s.world.cellType[1] = 0x01;
    GameTables t;
    t.world.hidingCells[0] = 0x77;
    t.world.cellPictureBefore = 0x08CF;
    CHECK(hidingCellAt(s, t, -1) == 0xE8);
    CHECK(hidingCellAt(s, t, 0) == 0x77);
    CHECK(cellTypeRaw(s, 256) == 0x34);
    CHECK(cellTypeRaw(s, 257) == 0x12);
    CHECK(cellPictureRaw(s, t, -1) == 0x08CF);
    CHECK(cellPictureRaw(s, t, 256) == 0x0105);
    UserRecord r;
    r.quizSlots[0] = 0xAB12;
    setHistoryByte(r, 89, 0x77);
    setHistoryByte(r, 90, 0xCD);
    CHECK(r.quizHistory[89] == 0x77);
    CHECK(r.quizSlots[0] == 0xABCD);
    CHECK(historyByte(r, 90) == 0xCD);
    CHECK(elfIndexOf(DsPtr{kPlayerObjOff}) == -1);
    CHECK(elfIndexOf(DsPtr{elfObjOff(14)}) == 14);
    s.elves.elves[3].obj.x = 0x321;
    CHECK(objAt(s, DsPtr{elfObjOff(3)}).x == 0x321);
    CHECK_THROWS(objAt(s, DsPtr{0}));
}

TEST_CASE("dos game skeleton: the sound facade keeps the DGROUP flags") {
    TM_REQUIRE_DOS_GAME_FILES();
    DosRuntime rt;
    RecordingSound sound;
    DosUsersStore users(std::filesystem::path{});
    DosGame g(*gameFiles, rt, sound, users, DosOptions{});
    setMusicOn(g, false);
    setMusicGate(g, 0);
    musicPlay(g, tuneForArea(5));
    musicPlay(g, tuneForArea(15));  // no tune: no call
    sfxPlay(g, 4);
    CHECK(g.core.musicOn == 0);
    CHECK(g.core.musicGate == 0);
    CHECK(sfxBlockActive(g));
    using Op = RecordingSound::Op;
    CHECK(sound.calls() == std::vector<RecordingSound::Call>{{Op::MusicOn, 0}, {Op::MusicGate, 0}, {Op::MusicPlay, 1}, {Op::SfxPlay, 4}});
    CHECK(tuneForArea(0) == 0);
    CHECK(tuneForArea(6) == 1);
    CHECK(tuneForArea(14) == 2);
}

TEST_CASE("dos game skeleton: the scenario fixture steps and records") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    f.placePlayer(0x140, 0x70, 0xC0, 0, 0);
    CHECK(f.g().pan == &gameFiles->pan("MOUNTAIN"));
    CHECK(f.g().bank == &gameFiles->pak("SPRITES"));
    REQUIRE(f.setScript("2:down:right, 5:up:right"));
    const auto log = f.step(10);
    REQUIRE(log.size() == 10);
    CHECK(log.front().tick == 1);
    CHECK(log.back().tick == 10);
    CHECK(log.back().pitTicks == 50);
    CHECK(log.back().x == 0x140);  // with stub modules nothing moves
    CHECK(f.runtime().keyboard().bytesDelivered() >= 2);
    CHECK_FALSE(dosgame::GameFixture::changes(log).empty());
    if (const auto caps = testdata::dosCapturesPath()) {
        const auto users = *caps / "USERS_after_exit_level2_Ada.DAT";
        if (std::filesystem::exists(users)) {
            CHECK(f.loadUsers(users));
            f.selectRecord(1);
            CHECK(f.g().hasRec());
            CHECK_FALSE(f.g().rec().empty());
        }
    }
}
