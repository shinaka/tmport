#include "gg/ui/menu_commands.h"

#include <array>
#include <utility>

#include "gg/data/archive.h"

namespace tmport::gg {

namespace {

// The functions the handler tables reach (the targets of the 1e97 and 1e9b stub entries).
constexpr std::array<std::pair<GgExe::Address, MenuCommand>, 11> kCommands = {{
    {{0x4106, 0x00d7}, MenuCommand::StartNewPlayer},
    {{0x4106, 0x014f}, MenuCommand::ReturnToCenter},
    {{0x4106, 0x00b2}, MenuCommand::SaveAndExit},
    {{0x4106, 0x011e}, MenuCommand::ToggleSound},
    {{0x4106, 0x00f8}, MenuCommand::ToggleHints},
    {{0x411d, 0x01e0}, MenuCommand::HelpAboutGame},
    {{0x411d, 0x0204}, MenuCommand::HelpMovingAround},
    {{0x411d, 0x01ec}, MenuCommand::HelpBuildingVehicles},
    {{0x411d, 0x01f8}, MenuCommand::HelpFindingParts},
    {{0x411d, 0x021c}, MenuCommand::HelpSolvingPuzzles},
    {{0x411d, 0x0210}, MenuCommand::HelpChallengeRaces},
}};

}  // namespace

MenuCommand menuCommandOf(const GgExe& exe, GgExe::Address handler) {
    if (handler.isNull()) return MenuCommand::None;
    const GgExe::Address f = exe.resolveCode(handler);
    for (const auto& [addr, cmd] : kCommands)
        if (addr == f) return cmd;
    return MenuCommand::Unknown;
}

const char* menuCommandName(MenuCommand c) {
    switch (c) {
        case MenuCommand::None: return "none";
        case MenuCommand::StartNewPlayer: return "Cmd_StartNewPlayer";
        case MenuCommand::ReturnToCenter: return "Cmd_ReturnToCenter";
        case MenuCommand::SaveAndExit: return "Cmd_SaveAndExit";
        case MenuCommand::ToggleSound: return "Cmd_ToggleSound";
        case MenuCommand::ToggleHints: return "Cmd_ToggleHints";
        case MenuCommand::HelpAboutGame: return "Help_AboutGame";
        case MenuCommand::HelpMovingAround: return "Help_MovingAround";
        case MenuCommand::HelpBuildingVehicles: return "Help_BuildingVehicles";
        case MenuCommand::HelpFindingParts: return "Help_FindingParts";
        case MenuCommand::HelpSolvingPuzzles: return "Help_SolvingPuzzles";
        case MenuCommand::HelpChallengeRaces: return "Help_ChallengeRaces";
        case MenuCommand::Unknown: break;
    }
    return "unknown";
}

std::optional<uint16_t> menuCommandQuestion(MenuCommand c) {
    switch (c) {
        case MenuCommand::StartNewPlayer: return kMsgStartNewPlayer;  // 4106:00d7
        case MenuCommand::ReturnToCenter: return kMsgReturnToCenter;  // 4106:014f
        case MenuCommand::SaveAndExit: return kMsgSaveAndExit;        // 4106:00b2
        case MenuCommand::ToggleSound: return kMsgSoundAndMusic;      // 4106:011e
        case MenuCommand::ToggleHints: return kMsgNewPlayerHints;     // 4106:00f8
        default: return std::nullopt;
    }
}

// 4106:0000 MsgDesc_LoadRes
YesNoMessage YesNoMessage::load(const Archive& archive, uint16_t id) {
    YesNoMessage m;
    m.bundle = Bundle::load(archive, id);  // Res_LoadCopy
    m.title = text::untilNul(m.bundle.member(0).payload);
    m.body = text::untilNul(m.bundle.member(1).payload);
    const auto flag = m.bundle.member(2).payload;
    m.defYes = flag.size() >= 2 && (flag[0] | flag[1]) != 0;
    return m;
}

// 4106:006d Dlg_YesNoRes
OwnedDialog yesNoResDesc(const DialogStrings& strings, const Archive& archive, uint16_t id) {
    YesNoMessage m = YesNoMessage::load(archive, id);
    OwnedDialog o;
    o.desc = yesNoDesc(strings, m.title, m.body, m.defYes);
    o.bundle = std::move(m.bundle);  // the spans point into its heap bytes, which the move keeps
    return o;
}

}  // namespace tmport::gg
