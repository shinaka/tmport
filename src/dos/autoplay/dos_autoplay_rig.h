#pragma once
// The headless DOS edition the autoplayer plays (area autoplay, wave 5): the same parts as the real
// start-up (dos/app/dos_entry.cpp runGame: the DOS runtime, the sound driver of the chosen device,
// the users store in the preference folder, DosGame, mainStart), but never with a window or an
// audible device, and stepped one game tick at a time by the caller (lockstep, as the headless
// --keys runs). The bot presses keys between ticks; they reach Kbd_ISR and the BIOS right before the
// next game tick, like a key pressed on the reference machine just before it.
//
// A second Rig on the same preference folder is "the program started again" (dos_users.dat persists).
// Layout_Generate's srand value (time() in the original) comes from a deterministic clock: the run's
// clock seed plus one per 47 game ticks (a DOS second), so runs are reproducible.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

#include "edition/dos_options.h"

namespace mischief::dos {
struct PcKey;
}  // namespace mischief::dos

namespace tmport::dos {
class DosGfxFiles;
class DosGame;
class DosRuntime;
class DosUsersStore;
class DosAudioDevice;
class DosAudioSound;
}  // namespace tmport::dos

namespace tmport::dos::autoplay {

struct RigOptions {
    std::filesystem::path prefDir;  // dos_users.dat lives here (created when missing; nothing imported)
    Look look = Look::Ega;
    // The sound driver (speaker, tandy, adlib) runs offline and silent; Off = the Tandy driver, as the
    // game does without sound (the effect status DS:6448 must keep its timing).
    SoundDevice sound = SoundDevice::Off;
    bool slowCpu = false;
    FrameSpeed frameSpeed = FrameSpeed::Reference;
    uint32_t clockSeed = 0;  // the layout clock: srand value = clockSeed + game tick / 47
};

class Rig {
public:
    Rig(const DosGfxFiles& files, const RigOptions& options);
    ~Rig();
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    DosGame& game() { return *game_; }
    const DosGame& game() const { return *game_; }
    DosRuntime& runtime() { return *runtime_; }
    DosUsersStore& users() { return *users_; }
    const RigOptions& options() const { return options_; }

    // Key events at the current virtual time (applied right before the next game tick).
    void keyDown(mischief::dos::PcKey k);
    void keyUp(mischief::dos::PcKey k);
    bool held(mischief::dos::PcKey k) const;
    void releaseAll();

    // One game tick (with its frame). False once the game has quit (g_quit after a frame): then
    // Game_Shutdown has run.
    bool step();
    bool running() const;
    uint64_t ticks() const;

private:
    RigOptions options_;
    std::unique_ptr<DosRuntime> runtime_;
    std::unique_ptr<DosUsersStore> users_;
    std::unique_ptr<DosAudioDevice> audio_;
    std::unique_ptr<DosAudioSound> sound_;
    std::unique_ptr<DosGame> game_;
    bool shutdown_ = false;
};

}  // namespace tmport::dos::autoplay
