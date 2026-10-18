#pragma once
// GameHooks: the side effects the deterministic game rules trigger in other areas. The rules
// change GameState only; whatever the original did besides (play an effect, redraw the HUD,
// stop/resume audio, spawn elves) goes through this small interface. Default = do nothing, so
// the rules run headless in tests.

#include <cstdint>

namespace tmport::game {

struct GameState;

class GameHooks {
public:
    virtual ~GameHooks() = default;
    // 1170:0000 Sfx_Play(n): SSND 7000+n, gated by g_soundOn.
    // TODO(integration): audio area (SfxPlayer).
    virtual void playSfx(int /*n*/) {}
    // 10e8:0040 Hud_RedrawNear: the inventory changed (coins, nets, key, gems, points).
    // TODO(integration): HUD module (game/hud, text_ui).
    virtual void hudChanged() {}
    // 1128:012f Audio_ToggleSound already flipped options.soundOn; on = new value
    // (off: Wave_Stop + Midi_Stop; on: Music_Resume). TODO(integration): audio area.
    virtual void soundToggled(bool /*on*/) {}
    // 1178:0251 Elf_SpawnForLevel(count): called by the climb set-up and the level change at the
    // point the original calls it (the RNG call order depends on it). TODO(integration): elves.
    virtual void spawnElves(GameState& /*state*/, int /*count*/) {}
    // 1178:0364 Elf_UnloadAll. TODO(integration): elves.
    virtual void unloadElves(GameState& /*state*/) {}
};

// A shared do-nothing instance.
GameHooks& nullHooks();

}  // namespace tmport::game
