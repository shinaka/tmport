// The menu commands and the help topics on the game (menu_commands.h, help.h: "wave 3: the run";
// code_ui.md §6.5, §6.6). Owner: ui agent.
#include "gg/ui/help.h"
#include "gg/ui/menu_commands.h"

#include "gg/game/flow.h"
#include "gg/game/game.h"

namespace tmport::gg {

// 4106:00d7 Cmd_StartNewPlayer: Yes sets g_menuRequest DS:130e = 2 (Input_GetCode then answers 0x17A3)
int cmdStartNewPlayer(GgGame& g) {
    if (dlgYesNoRes(g, kMsgStartNewPlayer) == 1) g.input().menuRequest = 2;
    return 0;
}

// 4106:014f Cmd_ReturnToCenter: Yes sets g_menuRequest = 1 (0x17A2)
int cmdReturnToCenter(GgGame& g) {
    if (dlgYesNoRes(g, kMsgReturnToCenter) == 1) g.input().menuRequest = 1;
    return 0;
}

// 4106:011e Cmd_ToggleSound: Yes -> g_soundOff = 0, No -> 1, Esc leaves it; then DS:1ad4 = g_soundOff
// and SndRes_ApplySoundOption (also after Esc)
int cmdToggleSound(GgGame& g) {
    const int r = dlgYesNoRes(g, kMsgSoundAndMusic);
    if (r == 0) g.player.soundOff = 1;
    else if (r == 1) g.player.soundOff = 0;
    g.sound.setSoundMuted(g.player.soundOff);
    return 0;
}

// 4106:00f8 Cmd_ToggleHints: the question is "hints off?": Yes -> g_hintsOff = 1, No -> 0, Esc leaves it
int cmdToggleHints(GgGame& g) {
    const int r = dlgYesNoRes(g, kMsgNewPlayerHints);
    if (r == 0) g.player.hintsOff = 0;
    else if (r == 1) g.player.hintsOff = 1;
    return 0;
}

// The handler table's call (40a4:03bb CALLF [handlers + 4 row]; a NULL entry answers 1)
int runMenuCommand(GgGame& g, MenuCommand c) {
    switch (c) {
        case MenuCommand::SaveAndExit: return cmdSaveAndExit(g);  // 4106:00b2 (flow)
        case MenuCommand::StartNewPlayer: return cmdStartNewPlayer(g);
        case MenuCommand::ReturnToCenter: return cmdReturnToCenter(g);
        case MenuCommand::ToggleSound: return cmdToggleSound(g);
        case MenuCommand::ToggleHints: return cmdToggleHints(g);
        default:
            // Help_AboutGame .. Help_SolvingPuzzles (ovl33): Help_ShowTopic(their topic)
            if (const auto id = helpTopicOf(c)) return helpShowTopic(g, *id);
            return 1;
    }
}

// 411d:0000 Help_ShowTopic: the topic's private copy, its pages through the pager; the pager's answer
// (1 Done reopens the Help menu, 0 Esc closes it)
int helpShowTopic(GgGame& g, uint16_t id) {
    const HelpTopic topic = HelpTopic::load(g.archive(), id);  // Res_LoadCopy
    return dlgRunPager(g, topic);
}

}  // namespace tmport::gg
