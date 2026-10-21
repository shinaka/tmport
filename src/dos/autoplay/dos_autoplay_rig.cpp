// The headless DOS edition of the autoplayer (dos_autoplay_rig.h).
#include "dos/autoplay/dos_autoplay_rig.h"

#include <system_error>

#include "dos/app/dos_preview.h"
#include "dos/app/dos_runtime.h"
#include "dos/audio/dos_audio_device.h"
#include "dos/game/game.h"
#include "dos/game/main_loop.h"
#include "dos/game/sound.h"
#include "dos/gfx/dos_screens.h"
#include "dos/save/users_store.h"
#include "mischief/util/error.h"

namespace tmport::dos::autoplay {

namespace {

constexpr uint64_t kGameTicksPerSecond = 47;  // the layout clock's second (47.34 game ticks)

DosOptions gameOptions(const RigOptions& o) {
    DosOptions d;
    d.look = o.look;
    d.sound = o.sound;
    d.slowCpu = o.slowCpu;
    d.frameSpeed = o.frameSpeed;
    d.headless = true;
    d.prefDir = o.prefDir;
    return d;
}

}  // namespace

Rig::Rig(const DosGfxFiles& files, const RigOptions& o) : options_(o) {
    std::error_code ec;
    std::filesystem::create_directories(o.prefDir, ec);

    RuntimeOptions ro;
    ro.frameSpeed = o.frameSpeed;
    runtime_ = std::make_unique<DosRuntime>(ro);

    users_ = std::make_unique<DosUsersStore>(o.prefDir / std::filesystem::path(kDosUsersFileName));
    users_->load();  // the bot's own roster: nothing is imported from the DOS folder

    // As runGame: without sound the Tandy driver still runs (silently), because the game reads the
    // effect status; the AdLib driver draws its orchestration rand() from the game's generator.
    const SoundDevice device = o.sound == SoundDevice::Off ? SoundDevice::Tandy : o.sound;
    audio_ = makeDosAudioDevice(files.files(), device, [this] { return game_ ? game_->rand.rand() : 0; });
    if (!audio_) throw mischief::Error("the DOS sound driver could not be created");
    sound_ = std::make_unique<DosAudioSound>(*audio_, true);
    sound_->setDelaySource([this] { return runtime_->soundDelay(); });  // as runGame (wave 5)

    game_ = std::make_unique<DosGame>(files, *runtime_, *sound_, *users_, gameOptions(o));
    game_->layoutSeedOverride = static_cast<uint16_t>(o.clockSeed);
    syncSoundFlags(*game_);
    runtime_->setClient(game_.get());
    mainStart(*game_);
}

Rig::~Rig() {
    if (runtime_) runtime_->setClient(nullptr);
    game_.reset();
}

void Rig::keyDown(mischief::dos::PcKey k) { runtime_->keyDownNow(k); }
void Rig::keyUp(mischief::dos::PcKey k) { runtime_->keyUpNow(k); }
bool Rig::held(mischief::dos::PcKey k) const { return runtime_->keyboard().isHeld(k); }
void Rig::releaseAll() { runtime_->releaseAllKeys(); }

bool Rig::running() const { return runtime_->running() && !shutdown_; }
uint64_t Rig::ticks() const { return runtime_->gameTicks(); }

bool Rig::step() {
    if (!running()) return false;
    // time(): the clock seed plus the DOS seconds so far.
    game_->layoutSeedOverride = static_cast<uint16_t>(options_.clockSeed + runtime_->gameTicks() / kGameTicksPerSecond);
    runtime_->stepGameTicks(1);
    if (!runtime_->running() && !shutdown_) {
        // main 130f:00D1: the loop ended because g_quit was set.
        gameShutdown(*game_);
        shutdown_ = true;
    }
    return running();
}

}  // namespace tmport::dos::autoplay
