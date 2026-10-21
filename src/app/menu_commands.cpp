#include "app/menu_commands.h"

#include "app/engine_glue.h"
#include "audio/audio_system.h"
#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "game/game_state.h"
#include "mischief/util/log.h"
#include "res/game_files.h"
#include "ui/dialog_desc.h"
#include "ui/game_dialog.h"
#include "ui/help_topics.h"
#include "ui/menu_bar.h"

namespace tmport::app {

namespace {

// GameDlg_RunModal on a static DlgDesc, optionally with the default button patched (+0x0b): the
// handlers store the current option value there before the call.
int runDialog(AppContext& ctx, uint16_t off, int defaultButton = -1) {
    ui::DlgDesc d = ui::DlgDesc::fromExe(*ctx.files.exe, off);
    if (defaultButton >= 0) d.defaultButton = static_cast<uint8_t>(defaultButton);
    return ctx.dialogs.runModal(d);
}

}  // namespace

void menuCmdAbout(AppContext& ctx) { runDialog(ctx, dg::kDlgAbout); }

void menuCmdNewPlayerMessages(AppContext& ctx) {
    const uint8_t hintsOn = ctx.state ? ctx.state->options.hintsOn : 1;
    const int r = runDialog(ctx, dg::kDlgNewPlayerMessages, hintsOn);
    if (r < 0 || !ctx.state) return;
    ctx.state->options.hintsOn = static_cast<uint8_t>(r);
    if (r == 0) {
        ctx.state->hints.pending = -1;
        ctx.state->hints.unseen = 0;
    } else {
        ctx.state->hints.unseen = 0xFFFFFFFFu;
    }
}

void menuCmdMusicAndSound(AppContext& ctx) {
    const uint8_t soundOn = ctx.audio ? ctx.audio->settings().soundOn : (ctx.state ? ctx.state->options.soundOn : 1);
    const int r = runDialog(ctx, dg::kDlgMusicAndSound, soundOn);
    if (r < 0) return;
    if (ctx.audio) {
        ctx.audio->setSoundFromOptions(r != 0);  // toggles only if the answer differs
        mirrorAudioOptions(ctx);
    } else if (ctx.state) {
        ctx.state->options.soundOn = static_cast<uint8_t>(r);
    }
}

void menuCmdVoice(AppContext& ctx) {
    const uint8_t voiceOn = ctx.audio ? ctx.audio->settings().voiceOn : (ctx.state ? ctx.state->options.voiceOn : 1);
    const int r = runDialog(ctx, dg::kDlgVoice, voiceOn);
    if (r < 0) return;
    if (ctx.audio) {
        ctx.audio->setVoiceOn(r != 0);
        mirrorAudioOptions(ctx);
    } else if (ctx.state) {
        ctx.state->options.voiceOn = static_cast<uint8_t>(r);
    }
}

void menuCmdStartAnotherGame(AppContext& ctx) {
    if (runDialog(ctx, dg::kDlgStartAnotherGame) != 1) return;
    if (ctx.state) ctx.state->hints.pending = -1;
    if (ctx.audio) ctx.audio->music().stopScene();
    throw RestartToSignIn{};
}

void menuCmdExit(AppContext& ctx) {
    if (runDialog(ctx, dg::kDlgExit) != 1) return;
    // Scene_FreeCommon / Treasures_Free: the port's scene objects free themselves while the
    // exception unwinds.
    if (ctx.services.saveCurrentPlayer) ctx.services.saveCurrentPlayer();
    throw QuitGame{};
}

void menuCmdHelp(AppContext& ctx, int topic) { ui::showHelpTopic(ctx.dialogs, *ctx.files.exe, *ctx.files.res, topic); }

void menuCmdSeeRank(AppContext& ctx) {
    if (ctx.services.seeRank)
        ctx.services.seeRank();
    else
        mischief::log::debug("menu: See Rank is not available yet");  // TODO(integration): game area (10f0:0487)
}

void registerMenuCommands(AppContext& ctx) {
    ui::MenuBar* m = ctx.menu;
    if (!m) return;
    using ui::MenuCommand;
    AppContext* c = &ctx;
    m->setHandler(MenuCommand::SeeRank, [c] { menuCmdSeeRank(*c); });
    m->setHandler(MenuCommand::StartAnotherGame, [c] { menuCmdStartAnotherGame(*c); });
    m->setHandler(MenuCommand::Exit, [c] { menuCmdExit(*c); });
    m->setHandler(MenuCommand::NewPlayerMessages, [c] { menuCmdNewPlayerMessages(*c); });
    m->setHandler(MenuCommand::MusicAndSound, [c] { menuCmdMusicAndSound(*c); });
    m->setHandler(MenuCommand::Voice, [c] { menuCmdVoice(*c); });
    m->setHandler(MenuCommand::HelpAboutGame, [c] { menuCmdHelp(*c, 0); });
    m->setHandler(MenuCommand::HelpKeyboard, [c] { menuCmdHelp(*c, 1); });
    m->setHandler(MenuCommand::HelpMouse, [c] { menuCmdHelp(*c, 2); });
    m->setHandler(MenuCommand::HelpEducation, [c] { menuCmdHelp(*c, 3); });
    m->setHandler(MenuCommand::AboutTM, [c] { menuCmdAbout(*c); });
}

}  // namespace tmport::app
