#include "tools/autoplay/autoplay_rig.h"

#include <system_error>

#include "anim/anim_engine.h"
#include "anim/anim_player.h"
#include "app/engine_glue.h"
#include "app/menu_commands.h"
#include "audio/audio_system.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "mischief/util/log.h"
#include "platform/engine.h"
#include "res/game_files.h"
#include "save/settings.h"
#include "text/font.h"
#include "text/text_renderer.h"
#include "ui/game_dialog.h"
#include "ui/menu_bar.h"

namespace tmport::autoplay {

Rig::Rig(GameFiles& files, const RigOptions& o) {
    std::error_code ec;
    std::filesystem::create_directories(o.prefDir, ec);
    paths_ = std::make_unique<save::Paths>();
    paths_->prefDir = o.prefDir;
    paths_->usersDat = o.prefDir / "users.dat";
    paths_->iniPath = o.prefDir / "tmport.ini";
    paths_->screenshotDir = o.prefDir / "screenshots";

    EngineConfig cfg;
    if (o.manualClock) {
        manual_ = std::make_unique<mischief::ManualClock>(o.clockStart);
        cfg.clock = manual_.get();
    }
    // The autoplayer holds arrows like keys (down ... up, autoplay_bot.cpp); the tap latch only
    // matters for a press and release within one drain, which then still counts for one tick (as
    // with --keys --tap-latch).
    cfg.tapLatch = true;
    cfg.pauseOnFocusLoss = false;
    cfg.screenshotDir = paths_->screenshotDir;
    engine_ = std::make_unique<Engine>(files, mischief::gfx::ColorMode::Colour256, cfg);  // never opens a window

    if (o.audio) {
        try {
            audio::AudioConfig ac;
            ac.waveDevice = false;  // sounds finish at once: nothing is ever audible
            ac.midiBackend = audio::MidiBackendKind::Null;
            audio_ = audio::AudioSystem::create(files, ac);
        } catch (const std::exception& e) {
            mischief::log::warn("autoplay: audio: {} (running without an audio system)", e.what());
            audio_.reset();
        }
    }

    font_ = std::make_unique<text::Font>(text::Font::load(*files.res));
    text_ = std::make_unique<text::TextRenderer>(*font_, &engine_->frame(), &engine_->presenter());
    dialogs_ = std::make_unique<ui::DialogSystem>(*engine_, *font_);
    {
        audio::AudioSystem* a = audio_.get();
        anim::AnimSound sound;
        if (a) {
            sound.play = [a](int n) { a->sfxPlay(n); };
            sound.stop = [a] { a->sfxStop(); };
        }
        anim_ = std::make_unique<anim::AnimEngine>(engine_->frame(), anim::enginePresent(*engine_), std::move(sound));
    }
    menu_ = std::make_unique<ui::MenuBar>(*engine_, ui::MenuBarModel::fromExe(*files.exe), *font_);
    tables_ = std::make_unique<game::GameTables>(game::GameTables::load(*files.exe, *files.res));
    state_ = std::make_unique<game::GameState>(game::GameState::initial(*tables_));

    ctx_ = std::make_unique<AppContext>(AppContext{*engine_, files, *font_, *text_, *dialogs_});
    ctx_->audio = audio_.get();
    ctx_->anim = anim_.get();
    ctx_->menu = menu_.get();
    ctx_->state = state_.get();
    ctx_->paths = paths_.get();
    app::registerMenuCommands(*ctx_);
    glue_ = std::make_unique<app::EngineGlue>(*ctx_);
    app::mirrorAudioOptions(*ctx_);

    // As App::run does without a window: the dialogs poll the engine (input that a hook injects
    // arrives through the engine's fetch). Wrapped to tell the autoplayer that a dialog is up.
    Engine* eng = engine_.get();
    dialogs_->hooks().getInputCode = [this, eng] {
        ++dialogDepth_;
        ++dialogPolls_;
        struct Leave {
            int& d;
            ~Leave() { --d; }
        } leave{dialogDepth_};
        const uint16_t c = eng->getInputCode();
        if (c == 0) eng->clock().sleepMs(1);
        return c;
    };
}

Rig::~Rig() {
    glue_.reset();
    menu_.reset();
    anim_.reset();
    dialogs_.reset();
    text_.reset();
    if (audio_) audio_->music().stopScene();
    audio_.reset();
    ctx_.reset();
}

mischief::Clock& Rig::clock() { return engine_->clock(); }

}  // namespace tmport::autoplay
