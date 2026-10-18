#include "game/scenes/scene_support.h"

#include <algorithm>
#include <format>

#include "anim/anim_engine.h"
#include "anim/anim_player.h"
#include "audio/audio_system.h"
#include "audio/engine_input_port.h"
#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "game/game_state.h"
#include "platform/engine.h"
#include "res/resource_manager.h"
#include "text/text_renderer.h"
#include "ui/dialog_desc.h"
#include "ui/game_dialog.h"
#include "ui/menu_bar.h"
#include "util/error.h"

namespace tmport::game::scenes {

// ------------------------------------------------------------------------------------ MenuDisable

MenuDisable::MenuDisable(AppContext& ctx) : ctx_(ctx) {
    if (ctx_.menu) {
        was_ = ctx_.menu->isEnabled();  // 1078:01b1 Menu_IsEnabled
        ctx_.menu->enableAll(false);    // 1078:01b5 Menu_EnableAll(0)
    }
}

MenuDisable::~MenuDisable() { restore(); }

void MenuDisable::restore() {
    if (done_) return;
    done_ = true;
    if (ctx_.menu) ctx_.menu->enableAll(was_);
}

// ---------------------------------------------------------------------------------------- SceneIo

// The inventory rules' side effects when GameMain gave no GameHooks: effects through the audio
// system, HUD redraws through SceneHooks::hudRedraw.
class SceneIo::AdapterHooks final : public GameHooks {
public:
    explicit AdapterHooks(SceneIo& io) : io_(io) {}
    void playSfx(int n) override { io_.sfxPlay(n); }
    void hudChanged() override {
        if (io_.hooks().hudRedraw) io_.hooks().hudRedraw();
    }

private:
    SceneIo& io_;
};

SceneIo::SceneIo(AppContext& ctx, const SceneHooks& hooks)
    : ctx_(ctx), hooks_(hooks), adapter_(std::make_unique<AdapterHooks>(*this)) {
    port_ = std::make_unique<audio::EngineInputPort>(ctx_.engine, ctx_.audio);
}

SceneIo::~SceneIo() = default;

gfx::FrameBuffer& SceneIo::fb() { return ctx_.engine.frame(); }
const ExeTables& SceneIo::exe() const { return *ctx_.files.exe; }
const ResourceManager& SceneIo::res() const { return *ctx_.files.res; }
int SceneIo::lineHeight() const { return ctx_.font.height(); }

uint16_t SceneIo::getCode() { return ctx_.engine.getInputCode(); }
uint16_t SceneIo::waitCode() { return ctx_.engine.waitInputCode(); }
void SceneIo::flushInput() { ctx_.engine.flushInput(); }
void SceneIo::delayMs(uint32_t ms) { ctx_.engine.delayMs(ms); }
void SceneIo::timerSet(int slot, int32_t ms) { ctx_.engine.timerSet(slot, ms); }
bool SceneIo::timerExpired(int slot) { return ctx_.engine.timerExpired(slot); }

void SceneIo::present(int x, int y, int w, int h) { ctx_.engine.presenter().blitToWindow(fb(), x, y, w, h); }

void SceneIo::setCursorForced(bool on) { ctx_.engine.cursor().setForced(on); }

std::shared_ptr<const gfx::PixMap> SceneIo::pixmap(int id) const {
    auto pm = res().pixmap(id);
    if (!pm) throw Error(std::format("PMAP {} is missing from the game files", id));
    return pm;
}

void SceneIo::highlightRect(int x, int y, int w, int h) {
    fb().highlightRect(x, y, w, h, ctx_.engine.palette().highlightLut());
}

bool SceneIo::voiceOn() const {
    if (ctx_.audio) return ctx_.audio->voiceOn();
    return ctx_.state ? ctx_.state->options.voiceOn != 0 : false;
}

bool SceneIo::soundOn() const {
    if (ctx_.audio) return ctx_.audio->soundOn();
    return ctx_.state ? ctx_.state->options.soundOn != 0 : false;
}

void SceneIo::sfxPlay(int n) {
    tracef("sfx {}", 7000 + n);
    if (ctx_.audio) ctx_.audio->sfxPlay(n);
}

void SceneIo::sfxStop() {
    if (ctx_.audio) ctx_.audio->sfxStop();
}

bool SceneIo::playUntilEvent(int soundId, audio::StopList list) {
    if (!ctx_.audio) {
        tracef("speech {} (no audio)", soundId);
        return false;
    }
    tracef("speech {} start", soundId);
    const bool interrupted = ctx_.audio->playUntilEvent(soundId, ctx_.audio->stopCodes(list), *port_);
    tracef("speech {} {}", soundId, interrupted ? "interrupted" : "done");
    return interrupted;
}

void SceneIo::waitWhilePlaying() {
    if (ctx_.audio) ctx_.audio->waitWhilePlaying(*port_);
}

void SceneIo::musicHalt() {
    trace("music halt");
    if (ctx_.audio) ctx_.audio->music().halt();
}

void SceneIo::musicResume() {
    trace("music resume");
    if (ctx_.audio) ctx_.audio->music().resume();
}

void SceneIo::musicPlayZone(int zone) {
    tracef("music zone {}", zone);
    if (ctx_.audio) ctx_.audio->music().playZone(zone);
}

int SceneIo::musicZone() const { return ctx_.audio ? ctx_.audio->music().zone() : 0; }

void SceneIo::setClueBoxMode(int mode) {
    tracef("clue box mode {}", mode);
    if (hooks_.setClueBoxMode) hooks_.setClueBoxMode(mode);
}

void SceneIo::exitGame() {
    trace("exit dialog");
    if (hooks_.exitGame) {
        hooks_.exitGame();
        return;
    }
    // 1140:0098 MenuCmd_Exit: GameDlg_RunModal(DS:1533); Yes -> (Scene_FreeCommon, Treasures_Free:
    // RAII here) Users_SaveCurrent, Game_ExitNow (longjmp(DS:5eda, 2)).
    const ui::DlgDesc d = ui::DlgDesc::fromExe(exe(), dg::kDlgExit);
    if (ctx_.dialogs.runModal(d) == 1) {
        if (ctx_.services.saveCurrentPlayer) ctx_.services.saveCurrentPlayer();
        throw QuitGame();
    }
}

void SceneIo::clearGateUnlocking() {
    if (hooks_.clearGateUnlocking) hooks_.clearGateUnlocking();
}

GameHooks& SceneIo::gameHooks() { return hooks_.gameHooks ? *hooks_.gameHooks : *adapter_; }

BorlandRand& SceneIo::rng() { return hooks_.rng ? *hooks_.rng : ctx_.engine.rng(); }

anim::AnimEngine& SceneIo::anim() {
    if (ctx_.anim) return *ctx_.anim;  // its FC / FB sound binding is the app's (anim_engine.h)
    if (!ownAnim_) {
        // FC / FB opcodes -> Sfx_Play / Sfx_Stop.
        anim::AnimSound snd;
        snd.play = [this](int n) { sfxPlay(n); };
        snd.stop = [this] { sfxStop(); };
        ownAnim_ = std::make_unique<anim::AnimEngine>(fb(), anim::enginePresent(ctx_.engine), std::move(snd));
    }
    return *ownAnim_;
}

void SceneIo::trace(std::string_view event) const {
    if (hooks_.trace) hooks_.trace(event);
}

// ---------------------------------------------------------------------------------------- helpers

void restoreFromLayer(gfx::FrameBuffer& fb, int x, int y, int w, int h, int dy) {
    const gfx::ClipRect& c = fb.clip();
    if (x < c.left) {
        w -= c.left - x;
        x = c.left;
    } else if (c.right < x + w - 1) {
        w = c.right - x + 1;
    }
    if (y < c.top) {
        h -= c.top - y;
        y = c.top;
    } else if (c.bottom < y + h - 1) {
        h = c.bottom - y + 1;
    }
    if (w > 0 && h > 0) fb.copyRect(x, y + dy, w, h, x, y);
}

std::vector<uint8_t> formatBytes(std::span<const uint8_t> fmt, std::span<const uint8_t> str, uint32_t num) {
    std::vector<uint8_t> out;
    out.reserve(fmt.size() + str.size() + 10);
    for (size_t i = 0; i < fmt.size(); ++i) {
        const uint8_t c = fmt[i];
        if (c != '%' || i + 1 >= fmt.size()) {
            out.push_back(c);
            continue;
        }
        size_t j = i + 1;
        if (fmt[j] == '%') {
            out.push_back('%');
            i = j;
            continue;
        }
        if (fmt[j] == 'l') ++j;
        if (j < fmt.size() && fmt[j] == 's') {
            out.insert(out.end(), str.begin(), str.end());
            i = j;
        } else if (j < fmt.size() && (fmt[j] == 'u' || fmt[j] == 'd')) {
            for (char d : std::to_string(num)) out.push_back(static_cast<uint8_t>(d));
            i = j;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::vector<uint8_t> loadText(const ResourceManager& res, int id) {
    auto raw = res.loadRaw(id);
    if (!raw) return {};
    const auto end = std::find(raw->begin(), raw->end(), uint8_t{0});
    return std::vector<uint8_t>(raw->begin(), end);
}

}  // namespace tmport::game::scenes
