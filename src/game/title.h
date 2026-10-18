#pragma once
// Title / story attract sequence (segment 1138; re/docs/code_10f8-1148.md §12,
// ARCHITECTURE.md §2.4, REFERENCE_CAPTURES.md §4.9).
//
//   page 0      PMAP 2100 opaque at (0,20), whole screen saved to the back page, presented;
//   pages 1..4  colour-11 box (33,59,284,196), OTHR 20104+page at (43,67) width 264, presented;
//   all pages   DAM 20100 (BNDL 400, frame slots 0..9) at (0,20) on layer 0, ticked every
//               100 ms (timer 7); each page waits up to 60 s (timer 4) and plays its narration
//               clips (SSND, tables DS:141c / DS:1424) when Voice is on;
//   input       Enter / click (hot spot 0x4B0) = next page, and after any key the loop ends
//               after page 4; Space = end now; Esc = "Exit" Yes/No (DlgDesc DS:2234), Yes quits;
//               Ctrl+S = sound toggle (inside the event fetch) and cuts the clip when voice is off;
//   music       zone -95 (OPEN1, once), then -94 (OPEN2, loops) once OPEN1 no longer plays;
//   ending      the title picture is restored and DAM 20101 (BNDL 401, frames after 400's) plays
//               at (32,21) until it ends, a key, or 60 s.
//
// Uses AppContext::anim when set (else an engine-local AnimEngine) and AppContext::audio when set
// (without audio there is no narration or music: the Voice-off path of the original).

#include <array>
#include <cstdint>
#include <memory>

#include "anim/dam_script.h"
#include "app/app_context.h"
#include "game/screens_common.h"

namespace tmport::anim {
class AnimEngine;
}

namespace tmport::game {

enum class TitleResult {
    Quit = 0,      // Esc + "Yes": the program ends (Title_Run returns 0)
    Continue = 1,  // go on to the Club Sign-In
};

// Fixed facts of 1138 (code constants, not game data).
namespace title {
inline constexpr int kTitlePmap = 2100;          // 0x834
inline constexpr int kPageTextFirst = 20104;     // OTHR 20104 + page (pages 1..4)
inline constexpr int kAnim1Bndl = 400;           // DAM 20100 + PMAP 2101..2110
inline constexpr int kAnim2Bndl = 401;           // DAM 20101 + PMAP 2111..2129
inline constexpr int kPages = 5;                 // 0 = title picture, 1..4 = story
inline constexpr int kEndPage = 5;               // Title_WaitPage(5): the ending animation
inline constexpr int kPageTimeoutMs = 60000;     // timer 4
inline constexpr int kTickMs = 100;              // timer 7
inline constexpr int kBoxX = 33, kBoxY = 59, kBoxW = 284, kBoxH = 196, kBoxColour = 11;
inline constexpr int kTextX = 43, kTextY = 67, kTextW = 264;
inline constexpr int kAnim1X = 0, kAnim1Y = 20, kAnim2X = 32, kAnim2Y = 21;
inline constexpr uint16_t kHotspot = 0x4B0;      // (0,20,512,267): a click acts as Enter
inline constexpr int kMusicOpen1 = -95, kMusicOpen2 = -94;
}  // namespace title

class TitleScreen {
public:
    explicit TitleScreen(AppContext& ctx);
    ~TitleScreen();
    TitleScreen(const TitleScreen&) = delete;
    TitleScreen& operator=(const TitleScreen&) = delete;

    // 1138:007a Title_Run.
    TitleResult run();

    void setTrace(ScreenTrace t) { io_.setTrace(std::move(t)); }

private:
    void loadAnims();                           // 1138:0000 Title_LoadAnims
    void freeAnims();                           // 1138:0037 Title_FreeAnims
    void drawPage(int page);                    // the switch of Title_Run
    int waitPage(int page);                     // 1138:043d Title_WaitPage
    int waitStep(int voiceId, bool isLast);     // 1138:0283 Title_WaitStep
    bool voiceOn() const;

    ScreenIo io_;
    anim::AnimEngine* anim_ = nullptr;
    std::unique_ptr<anim::AnimEngine> ownAnim_;  // used when AppContext::anim is null
    anim::ScriptPtr script1_, script2_;          // g_titleScript1 / g_titleScript2
    int frames1_ = 0;                            // g_titleFrames1 (frame count of BNDL 400)
    int frames2_ = 0;
    int anim1_ = -1, anim2_ = -1;                // g_titleAnim1 / g_titleAnim2 (DS:5f1b / 5f27)
    bool musicPending_ = false;                  // g_titleMusicPending (DS:5f35)
    std::array<int16_t, 4> voiceFirst_{};        // DS:141c
    std::array<int16_t, 4> voiceCount_{};        // DS:1424
};

// Convenience: TitleScreen(ctx).run().
TitleResult runTitle(AppContext& ctx);

}  // namespace tmport::game
