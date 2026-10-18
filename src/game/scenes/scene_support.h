#pragma once
// Shared plumbing of the modal game scenes (area "scenes": elf question 1150, hint popup 1130,
// level cut-scene 1188, celebration / rank screen 10f0, castle-wall climb 1168).
//
// Every scene is a free function taking the AppContext plus the game state/tables it needs, so the
// game area's GameMain can bind them into the room loop's hooks. What a scene needs from modules of
// other areas that are still being built (the HUD's clue box, the Exit command, the gate lock flag)
// comes in through SceneHooks; every member may be empty.
//
// SceneIo bundles the original's wait/draw/audio primitives on top of the AppContext:
//   Input_GetCode / Input_WaitCode / Input_Flush, Delay_ms, Timer_Set / Timer_Expired,
//   Gfx_BlitToWindow, SetCursorForced, Menu_IsEnabled / Menu_EnableAll, Sfx_Play / Sfx_Stop,
//   Wave_PlayResUntilEvent, Music_Halt / Music_Resume / Music_PlayZone, MenuCmd_Exit.

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/app_context.h"
#include "game/game_hooks.h"
#include "gfx/frame_buffer.h"
#include "gfx/pixmap.h"
#include "rtl/borland_rand.h"

namespace tmport {
class ExeTables;
class ResourceManager;
namespace audio {
class InputPort;
enum class StopList;
}  // namespace audio
namespace anim {
class AnimEngine;
}
}  // namespace tmport

namespace tmport::game::scenes {

// Services of other areas the scenes call. TODO(integration): GameMain binds the room area's HUD
// (Hud_SetClueBoxMode / Hud_Redraw), the app's MenuCmd_Exit and the gate-lock flag here.
struct SceneHooks {
    // 10e8:0658 Hud_SetClueBoxMode(mode): 0 = clue list (hot spots 99 + 0x409), 1 = answer prompt,
    // 2 = CONTINUE (both register the full-screen hot spot 0x409); redraws and presents the status
    // bar. Empty: nothing happens (headless tests).
    std::function<void(int mode)> setClueBoxMode;
    // 10e8:003b Hud_Redraw: the inventory changed (used by the built-in GameHooks adapter).
    std::function<void()> hudRedraw;
    // 1140:0098 MenuCmd_Exit: Yes/No DS:1533; Yes saves and throws QuitGame. Empty: the built-in
    // version (DialogSystem::runModal + AppContext::services.saveCurrentPlayer + throw QuitGame).
    std::function<void()> exitGame;
    // DS:2d60 gate_unlocking := 0 (cleared by LevelCutscene_Play 1188:0000 on every path).
    std::function<void()> clearGateUnlocking;
    // Side effects of the inventory rules (sfx, HUD redraw). Null: a built-in adapter that plays
    // effects through AppContext::audio and calls hudRedraw.
    GameHooks* gameHooks = nullptr;
    // The game's Borland RNG for the scenes' R(n) calls (quiz R(4), castle layout / creature).
    // Null: ctx.engine.rng(). GameMain passes the generator the room loop uses, so the calls stay
    // in the original's order (ARCHITECTURE.md §5.16).
    BorlandRand* rng = nullptr;
    // Optional event trace (tools and tests print it with the clock time).
    std::function<void(std::string_view event)> trace;
};

// RAII for `was = Menu_IsEnabled(); Menu_EnableAll(0); ... Menu_EnableAll(was)` on AppContext::menu.
class MenuDisable {
public:
    explicit MenuDisable(AppContext& ctx);
    ~MenuDisable();
    MenuDisable(const MenuDisable&) = delete;
    MenuDisable& operator=(const MenuDisable&) = delete;
    void restore();  // Menu_EnableAll(was) now (once)

private:
    AppContext& ctx_;
    bool was_ = false;
    bool done_ = false;
};

class SceneIo {
public:
    SceneIo(AppContext& ctx, const SceneHooks& hooks);
    ~SceneIo();
    SceneIo(const SceneIo&) = delete;
    SceneIo& operator=(const SceneIo&) = delete;

    AppContext& ctx() { return ctx_; }
    const SceneHooks& hooks() const { return hooks_; }
    Engine& engine() { return ctx_.engine; }
    gfx::FrameBuffer& fb();
    const ExeTables& exe() const;
    const ResourceManager& res() const;
    text::TextRenderer& text() { return ctx_.text; }
    int lineHeight() const;

    // --- input / timing -------------------------------------------------------------------------
    uint16_t getCode();                   // 1028:0131 Input_GetCode (0 = none)
    uint16_t waitCode();                  // 10a0:0000 Input_WaitCode
    void flushInput();                    // 1028:0126 Input_Flush
    void delayMs(uint32_t ms);            // 10a8:0000 Delay_ms (busy: input is seen afterwards)
    void timerSet(int slot, int32_t ms);  // 10a8:002a Timer_Set
    bool timerExpired(int slot);          // 10a8:0066 Timer_Expired (pumps)

    // --- screen -----------------------------------------------------------------------------------
    void present(int x, int y, int w, int h);  // 1058:05bc Gfx_BlitToWindow
    void setCursorForced(bool on);             // 10e0:0063 SetCursorForced
    // Res_LoadToScratch / GetResource of a PMAP; throws tmport::Error if it does not exist.
    std::shared_ptr<const gfx::PixMap> pixmap(int id) const;
    // 1058:19b2 Gfx_HighlightRect with the palette's reading-highlight LUT.
    void highlightRect(int x, int y, int w, int h);

    // --- audio (no-ops without an audio system) ---------------------------------------------------
    bool voiceOn() const;  // DS:0b97 g_voiceOn
    bool soundOn() const;  // DS:0b96 g_soundOn
    void sfxPlay(int n);   // 1170:0000 Sfx_Play
    void sfxStop();        // 1170:005b Sfx_Stop
    // 1088:01a9 Wave_PlayResUntilEvent(id, list): true if interrupted (not gated by voiceOn).
    bool playUntilEvent(int soundId, audio::StopList list);
    void waitWhilePlaying();  // `while (Wave_IsPlaying());`
    void musicHalt();         // 1128:00d6 Music_Halt
    void musicResume();       // 1128:00dc Music_Resume
    void musicPlayZone(int zone);  // 1128:0000 Music_PlayZone
    int musicZone() const;         // DS:5efe (0 without audio)

    // --- other areas ------------------------------------------------------------------------------
    void setClueBoxMode(int mode);  // 10e8:0658 (hook)
    void exitGame();                // 1140:0098 MenuCmd_Exit (hook or built-in; may throw QuitGame)
    void clearGateUnlocking();      // DS:2d60 := 0 (hook)
    GameHooks& gameHooks();
    BorlandRand& rng();
    // The DAM engine: AppContext::anim (whose FC / FB sound binding is the app's), or one owned
    // by this SceneIo (created on first use, sound through sfxPlay / sfxStop).
    anim::AnimEngine& anim();

    void trace(std::string_view event) const;
    template <class... Args>
    void tracef(std::string_view fmt, Args&&... args) const;

private:
    class AdapterHooks;
    AppContext& ctx_;
    const SceneHooks& hooks_;
    std::unique_ptr<audio::InputPort> port_;
    std::unique_ptr<AdapterHooks> adapter_;
    std::unique_ptr<anim::AnimEngine> ownAnim_;
};

// 10e0:03a6 Gfx_RestoreFromLayer(x, y, w, h, dy): clip to the clip rectangle (with the original's
// `else if` quirk: a rectangle clipped on the left/top is not clipped on the right/bottom), then
// copy (x, y + dy) -> (x, y) (a dirty rect is added when enabled).
void restoreFromLayer(gfx::FrameBuffer& fb, int x, int y, int w, int h, int dy);

// The sprintf subset the scenes need (rank screen 10f0:054b): copies fmt and replaces "%s" with
// str, "%lu" / "%ld" / "%u" / "%d" with num (decimal), "%%" with '%'. Raw bytes in and out.
std::vector<uint8_t> formatBytes(std::span<const uint8_t> fmt, std::span<const uint8_t> str, uint32_t num);

// NUL-terminated bytes of an OTHR text resource (empty if missing).
std::vector<uint8_t> loadText(const ResourceManager& res, int id);

}  // namespace tmport::game::scenes

#include <format>

template <class... Args>
void tmport::game::scenes::SceneIo::tracef(std::string_view fmt, Args&&... args) const {
    if (hooks_.trace) trace(std::vformat(fmt, std::make_format_args(args...)));
}
