#pragma once
// The menu commands (ovl32 4106, and the Help handlers of ovl33 411d; re/gg/docs/code_ui.md §6.5):
// the handler of each drop-down item, identified by the overlay function its far pointer reaches
// through the stub, and the Yes/No message bundles the File and Options commands ask
// (MsgDesc_LoadRes / Dlg_YesNoRes). What each command then does to the game (g_menuRequest, the
// sound and hint options, Save & Exit) is the game's (wave 3).
//
//   File:    Start New Player (Yes/No 0x106C, default No), Return to Technology Center (0x106B, No),
//            Save & Exit (0x1064, No)
//   Options: Sound & Music (0x1065, default Yes), New Player Hints (0x1066, No)
//   Help:    the six topics 0x105D..0x1062 (gg/ui/help.h)
// A handler returning non-zero reopens the same menu (the help pager's Done, an Esc'd Save & Exit).

#include <cstdint>
#include <optional>

#include "gg/data/gg_exe.h"
#include "gg/data/resources.h"
#include "gg/ui/dialog.h"
#include "text/text_engine.h"

namespace tmport::gg {

class Archive;

enum class MenuCommand : uint8_t {
    None,                 // a NULL handler pointer: the item returns 1 (reopens)
    StartNewPlayer,       // 4106:00d7 Cmd_StartNewPlayer
    ReturnToCenter,       // 4106:014f Cmd_ReturnToCenter
    SaveAndExit,          // 4106:00b2 Cmd_SaveAndExit
    ToggleSound,          // 4106:011e Cmd_ToggleSound
    ToggleHints,          // 4106:00f8 Cmd_ToggleHints
    HelpAboutGame,        // 411d:01e0 Help_AboutGame
    HelpMovingAround,     // 411d:0204 Help_MovingAround
    HelpBuildingVehicles, // 411d:01ec Help_BuildingVehicles
    HelpFindingParts,     // 411d:01f8 Help_FindingParts
    HelpSolvingPuzzles,   // 411d:021c Help_SolvingPuzzles
    HelpChallengeRaces,   // 411d:0210 Help_ChallengeRaces
    Unknown,              // a pointer to anything else (a modded table)
};

// The command whose function a handler pointer reaches (stub entries are resolved through
// GgExe::resolveCode: 1e97:0025.. and 1e9b:0020..).
MenuCommand menuCommandOf(const GgExe& exe, GgExe::Address handler);
const char* menuCommandName(MenuCommand c);

// The Yes/No bundle a command asks (none for the Help topics).
std::optional<uint16_t> menuCommandQuestion(MenuCommand c);
inline constexpr uint16_t kMsgSaveAndExit = 0x1064;
inline constexpr uint16_t kMsgSoundAndMusic = 0x1065;
inline constexpr uint16_t kMsgNewPlayerHints = 0x1066;
inline constexpr uint16_t kMsgRemovePlayer = 0x106A;
inline constexpr uint16_t kMsgReturnToCenter = 0x106B;
inline constexpr uint16_t kMsgStartNewPlayer = 0x106C;

// 4106:0000 MsgDesc_LoadRes(id, &msg): a private copy of bundle {S title, S body, I defYes}.
struct YesNoMessage {
    Bundle bundle;
    text::Bytes title;  // into `bundle` (move-only for that reason)
    text::Bytes body;
    bool defYes = false;

    static YesNoMessage load(const Archive& archive, uint16_t id);
    YesNoMessage() = default;
    YesNoMessage(YesNoMessage&&) noexcept = default;
    YesNoMessage& operator=(YesNoMessage&&) noexcept = default;
    YesNoMessage(const YesNoMessage&) = delete;
    YesNoMessage& operator=(const YesNoMessage&) = delete;
};

// 4106:006d Dlg_YesNoRes(id): Dlg_YesNo of the bundle's message (gg/ui/dialog.h yesNoDesc).
OwnedDialog yesNoResDesc(const DialogStrings& strings, const Archive& archive, uint16_t id);

}  // namespace tmport::gg
